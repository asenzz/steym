/*
    viterbi.c

    Beam-search Viterbi optimizer for Atari STE YM2149 PCM playback.
*/

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "viterbi.h"

#define AUDIO_WEIGHT  100000.0f
#define FUTURE_WEIGHT 50000.0f
#define NOISE_WEIGHT  20000.0f

static float absf_fast(const float x)
{
    return x < 0.0f ? -x : x;
}

/* Full transition cost, including the scheduler's modeled cycle count. */
static float transition_cost(
    const ViterbiNode *old,
    const Candidate *candidate,
    const float current_target,
    const float next_target,
    const uint8_t cycles)
{
    float decision_target = current_target + old->noise_error;
    const float full_scale = optimizer_output_max();
    if (decision_target < 0.0f)
        decision_target = 0.0f;
    else if (decision_target > full_scale)
        decision_target = full_scale;

    float cost = absf_fast(decision_target - candidate->state.amplitude)
                 * AUDIO_WEIGHT;
    cost += absf_fast(next_target - candidate->state.amplitude)
            * FUTURE_WEIGHT;
    cost += (float)cycles;
    cost += absf_fast(old->noise_error) * NOISE_WEIGHT;
    return cost;
}

/* Backtracking needs only the emitted state and predecessor, not search costs,
   feedback accumulators or register shadows. */
typedef struct
{
    YMState ym[BEAM_WIDTH];
    int8_t previous[BEAM_WIDTH];
    uint8_t count;
} ViterbiTrace;

_Static_assert(BEAM_WIDTH > 0 && BEAM_WIDTH <= INT8_MAX + 1 &&
               BEAM_WIDTH <= UINT8_MAX,
               "beam indices and counts must fit their byte-sized types");

/* Keep the lowest-cost BEAM_WIDTH nodes, in ascending cost order. */
static void beam_insert(ViterbiBeam *const beam, const ViterbiNode *const node)
{
    if (beam == NULL || node == NULL ||
        !isfinite(node->cost) || node->cost < 0.0f)
        return;

    if (beam->count == BEAM_WIDTH &&
        node->cost >= beam->node[BEAM_WIDTH - 1].cost)
        return;

    /* Upper bound preserves the original ordering of equal-cost nodes. */
    uint8_t pos = 0;
    uint8_t end = beam->count;
    while (pos < end) {
        const uint8_t mid = (uint8_t)(pos + (end - pos) / 2u);
        if (node->cost < beam->node[mid].cost)
            end = mid;
        else
            pos = (uint8_t)(mid + 1u);
    }

    if (beam->count < BEAM_WIDTH)
        ++beam->count;

    for (uint8_t i = (uint8_t)(beam->count - 1); i > pos; --i)
        beam->node[i] = beam->node[i - 1];

    beam->node[pos] = *node;
}

/* Optimize the complete PCM sequence. Returns 0 on success, -1 on failure. */
int8_t viterbi_optimize(const float *const pcm, const size_t samples,
                     YMState *const output)
{
    if (pcm == NULL || output == NULL || samples == 0) {
        fprintf(stderr, "viterbi_optimize: invalid input or empty sample sequence.\n");
        return -1;
    }

    if (samples > SIZE_MAX / sizeof(ViterbiTrace)) {
        fprintf(stderr, "viterbi_optimize: sample count is too large.\n");
        return -1;
    }

    int8_t result = -1;
    ViterbiTrace *history = NULL;
    ViterbiBeam *current = NULL;
    ViterbiBeam *next = NULL;
    Optimizer *optimizer = NULL;

    history = malloc(samples * sizeof(*history));
    optimizer = malloc(sizeof(*optimizer));
    current = calloc(1, sizeof(*current));
    next = malloc(sizeof(*next));
    if (history == NULL || optimizer == NULL || current == NULL || next == NULL) {
        fprintf(stderr, "viterbi_optimize: allocation failed for %zu samples.\n", samples);
        goto cleanup;
    }

    optimizer_init(optimizer);

    current->count = 1;
    current->node[0].ym = optimizer->current;
    scheduler_init(&current->node[0].scheduler);
    current->node[0].ym.selected_register =
        (int8_t)current->node[0].scheduler.latch;
    current->node[0].noise_error = 0.0f;
    current->node[0].cost = 0.0f;
    current->node[0].previous = -1;

    for (size_t n = 0; n < samples; ++n) {
        float target;
        float next_target;

        if (!pcm_to_quantizer_target(pcm[n], &target) ||
            !pcm_to_quantizer_target(pcm[n + 1 < samples ? n + 1 : n],
                                     &next_target)) {
            fprintf(stderr, "viterbi_optimize: invalid PCM value at sample %zu.\n", n);
            goto cleanup;
        }

        next->count = 0;

        for (uint8_t b = 0; b < current->count; ++b) {
            const ViterbiNode *const parent = &current->node[b];

            if (!isfinite(parent->noise_error) || !isfinite(parent->cost))
                continue;

            optimizer->current = parent->ym;

            /* Candidate generation receives this path's feedback residual. */
            SigmaDeltaState parent_sd = { .error = parent->noise_error };
            optimizer_generate_candidates_noise(optimizer, target, &parent_sd);

            for (uint8_t c = 0; c < optimizer->candidate_count; ++c) {
                const Candidate *const candidate = &optimizer->candidates[c];
                if (!isfinite(candidate->state.amplitude))
                    continue;

                /* Compute the transition from this path's actual register shadow. */
                SampleSlot slot;
                const uint8_t cycles = scheduler_make_slot(
                    &parent->scheduler, &candidate->state, &slot);

                /* Reserve time for the delay sequence as well as YM writes. */
                if (cycles > MAX_SLOT_OPERATION_CYCLES)
                    continue;

                SchedulerState next_scheduler = parent->scheduler;
                scheduler_commit_slot(&next_scheduler, &slot);

                /* Every child has its own feedback accumulator. */
                SigmaDeltaState child_sd = { .error = parent->noise_error };
                if (!optimizer_update_sigma_delta(
                        &child_sd, target, candidate->state.amplitude))
                    continue;

                ViterbiNode node = {0};
                node.ym = candidate->state;
                node.scheduler = next_scheduler;
                node.ym.selected_register = next_scheduler.latch == UINT8_MAX
                    ? -1
                    : (int8_t)next_scheduler.latch;
                node.noise_error = child_sd.error;
                node.previous = (int8_t)b;

                const float step_cost = transition_cost(
                    parent, candidate, target, next_target, cycles);
                const float total_cost = parent->cost + step_cost;

                if (!isfinite(step_cost) || step_cost < 0.0f ||
                    !isfinite(total_cost))
                    continue;

                node.cost = total_cost;
                if (!isfinite(node.cost))
                    continue;

                beam_insert(next, &node);
            }
        }

        if (next->count == 0) {
            fprintf(stderr,
                    "viterbi_optimize: no cycle-feasible candidates at sample %zu.\n",
                    n);
            goto cleanup;
        }

        /* Remove a common offset to limit long-run floating-point growth. */
        const float base_cost = next->node[0].cost;
        for (uint8_t i = 0; i < next->count; ++i) {
            next->node[i].cost -= base_cost;
            if (!isfinite(next->node[i].cost)) {
                fprintf(stderr, "viterbi_optimize: non-finite beam cost at sample %zu.\n", n);
                goto cleanup;
            }
        }

        history[n].count = next->count;
        for (uint8_t i = 0; i < next->count; ++i) {
            history[n].ym[i] = next->node[i].ym;
            history[n].previous[i] = next->node[i].previous;
        }
        ViterbiBeam *const temp = current;
        current = next;
        next = temp;
    }

    if (current->count == 0)
        goto cleanup;

    uint8_t best = 0; /* beam_insert keeps the cheapest node first. */
    for (size_t t = samples; t > 0; --t) {
        const ViterbiTrace *const trace = &history[t - 1];
        if (best >= trace->count) {
            fprintf(stderr, "viterbi_optimize: invalid backpointer at sample %zu.\n", t - 1);
            goto cleanup;
        }

        output[t - 1] = trace->ym[best];
        if (t > 1) {
            const int8_t previous = trace->previous[best];
            if (previous < 0 || (uint8_t)previous >= history[t - 2].count) {
                fprintf(stderr, "viterbi_optimize: invalid predecessor at sample %zu.\n", t - 1);
                goto cleanup;
            }
            best = (uint8_t)previous;
        }
    }

    result = 0;

cleanup:
    free(history);
    free(current);
    free(next);
    free(optimizer);
    return result;
}
