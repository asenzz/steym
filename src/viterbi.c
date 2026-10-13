/*
    viterbi.c

    Beam-search Viterbi optimizer for Atari STE YM2149 PCM playback.
*/

#include <float.h>
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
    const uint16_t cycles)
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

/* Keep the lowest-cost BEAM_WIDTH nodes, in ascending cost order. */
static void beam_insert(ViterbiBeam *const beam, const ViterbiNode *const node)
{
    uint16_t pos = UINT16_MAX;

    if (beam == NULL || node == NULL ||
        !isfinite(node->cost) || node->cost < 0.0f)
        return;

    for (uint16_t i = 0; i < beam->count; ++i) {
        if (node->cost < beam->node[i].cost) {
            pos = i;
            break;
        }
    }

    if (pos == UINT16_MAX) {
        if (beam->count < BEAM_WIDTH)
            beam->node[beam->count++] = *node;
        return;
    }

    if (beam->count < BEAM_WIDTH)
        ++beam->count;

    for (uint16_t i = (uint16_t)(beam->count - 1); i > pos; --i)
        beam->node[i] = beam->node[i - 1];

    beam->node[pos] = *node;
}

/* Optimize the complete PCM sequence. Returns 0 on success, -1 on failure. */
int viterbi_optimize(const float *const pcm, const size_t samples,
                     YMState *const output)
{
    if (pcm == NULL || output == NULL || samples == 0) {
        fprintf(stderr, "viterbi_optimize: invalid input or empty sample sequence.\n");
        return -1;
    }

    if (samples > SIZE_MAX / sizeof(ViterbiBeam)) {
        fprintf(stderr, "viterbi_optimize: sample count is too large.\n");
        return -1;
    }

    int result = -1;
    ViterbiBeam *history = NULL;
    ViterbiBeam *current = NULL;
    ViterbiBeam *next = NULL;
    Optimizer *optimizer = NULL;

    history = calloc(samples, sizeof(*history));
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

        memset(next, 0, sizeof(*next));

        for (uint8_t b = 0; b < current->count; ++b) {
            const ViterbiNode *const parent = &current->node[b];

            if (!isfinite(parent->noise_error) || !isfinite(parent->cost))
                continue;

            optimizer->current = parent->ym;

            /* Candidate generation receives this path's feedback residual. */
            SigmaDeltaState parent_sd = { .error = parent->noise_error };
            optimizer_generate_candidates_noise(optimizer, target, &parent_sd);

            for (uint16_t c = 0; c < optimizer->candidate_count; ++c) {
                const Candidate *const candidate = &optimizer->candidates[c];
                if (!isfinite(candidate->state.amplitude))
                    continue;

                /* Compute the transition from this path's actual register shadow. */
                SampleSlot slot;
                const uint16_t cycles = scheduler_make_slot(
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
                node.previous = (int)b;

                const float step_cost = transition_cost(
                    parent, candidate, target, next_target, cycles);
                const double total_cost = (double)parent->cost + step_cost;

                if (!isfinite(step_cost) || step_cost < 0.0f ||
                    !isfinite(total_cost) || total_cost > (double)FLT_MAX)
                    continue;

                node.cost = (float)total_cost;
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

        /* history[n] is the beam preceding sample n; children point into it. */
        history[n] = *current;
        ViterbiBeam *const temp = current;
        current = next;
        next = temp;
    }

    if (current->count == 0)
        goto cleanup;

    size_t best = 0;
    for (size_t i = 1; i < current->count; ++i) {
        if (current->node[i].cost < current->node[best].cost)
            best = i;
    }

    /* Backtrack through each preceding beam, not repeatedly through the final beam. */
    const ViterbiBeam *beam = current;
    for (size_t t = samples; t > 0; --t) {
        if (best >= beam->count) {
            fprintf(stderr, "viterbi_optimize: invalid backpointer at sample %zu.\n", t - 1);
            goto cleanup;
        }

        const ViterbiNode *const node = &beam->node[best];
        output[t - 1] = node->ym;

        if (t > 1) {
            const int previous = node->previous;
            beam = &history[t - 1];

            if (previous < 0 || (size_t)previous >= beam->count) {
                fprintf(stderr, "viterbi_optimize: invalid predecessor at sample %zu.\n", t - 1);
                goto cleanup;
            }
            best = (size_t)previous;
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
