/*
    scheduler.c

    Final STE YM2149 scheduler.

    Converts YM states into
    alternating 180/184-cycle slots.

*/


#include <string.h>
#include <math.h>
#include "scheduler.h"




static const uint8_t order[] =
{
    7,
    6,
    8,
    9,
    10
};

_Static_assert(sizeof(order) / sizeof(order[0]) <= MAX_OPERATIONS,
               "slot storage must hold every scheduled register");


/* Create a raw slot from YM state, preserving the defined register order. */
uint8_t scheduler_make_slot(const SchedulerState *state, const YMState *state_in, SampleSlot *const slot)
{
    if (state == NULL || state_in == NULL || slot == NULL)
        return SAMPLE_CYCLES + 1;

    const uint8_t values[] = {
        state_in->mixer, state_in->noise_period,
        state_in->volume[0], state_in->volume[1], state_in->volume[2]
    };
    _Static_assert(sizeof(values) / sizeof(values[0]) ==
                   sizeof(order) / sizeof(order[0]),
                   "each scheduled register needs one value");

    memset(slot, 0, sizeof(*slot));
    slot->initial_latch = state->latch;
    uint8_t latch = state->latch;
    uint8_t cycles = 0;
    for (uint8_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
        const uint8_t reg = order[i];
        if (state->reg[reg] == values[i])
            continue;

        slot->op[slot->count++] = (YMOperation){ .reg = reg, .value = values[i] };
        if (latch != reg) {
            cycles = (uint8_t)(cycles + YM_REGISTER_ACCESS_CYCLES);
            latch = reg;
        }
        cycles = (uint8_t)(cycles + (reg == 7
            ? YM_MIXER_WRITE_CYCLES : YM_REGISTER_ACCESS_CYCLES));
    }
    slot->final_latch = latch;
    slot->cycles = cycles;
    return cycles;
}


/* Score alternative candidate. */
static float candidate_score(const Candidate *candidate, const uint8_t cycles)
{
    return candidate->audio_error * 100000.0f + cycles;
}


/* Feedback search. */
uint8_t scheduler_make_slot_feedback(const SchedulerState *state, const Candidate *candidates, const uint8_t candidate_count, YMState *const chosen, SampleSlot *const slot)
{
    if (state == NULL || candidates == NULL || candidate_count == 0 ||
        chosen == NULL || slot == NULL)
        return SAMPLE_CYCLES + 1;

    float best = INFINITY;
    uint8_t best_index = UINT8_MAX;
    SampleSlot best_slot;

    for (uint8_t i = 0; i < candidate_count; ++i) {
        if (!(candidates[i].audio_error >= 0.0f))
            continue;

        SampleSlot test;
        const uint8_t cycles = scheduler_make_slot(state, &candidates[i].state, &test);
        if (cycles > MAX_SLOT_OPERATION_CYCLES)
            continue;

        const float score = candidate_score(&candidates[i], cycles);
        if (isfinite(score) && score < best) {
            best = score;
            best_index = i;
            best_slot = test;
        }
    }

    if (best_index == UINT8_MAX)
        return SAMPLE_CYCLES + 1;

    *chosen = candidates[best_index].state;
    *slot = best_slot;
    return best_slot.cycles;
}


/* Commit emitted slot. The next slot starts with the correct YM latch. */
void scheduler_commit_slot(SchedulerState *const scheduler, const SampleSlot *slot)
{
    if (scheduler == NULL || slot == NULL || slot->count > MAX_OPERATIONS)
        return;

    /* Validate operations and latch continuity before modifying the shadow. */
    uint8_t latch = slot->initial_latch;
    for (uint8_t i = 0; i < slot->count; ++i) {
        if (slot->op[i].reg >= REG_COUNT)
            return;
        latch = slot->op[i].reg;
    }
    if (slot->initial_latch != scheduler->latch || latch != slot->final_latch)
        return;

    for (uint8_t i = 0; i < slot->count; ++i)
        scheduler->reg[slot->op[i].reg] = slot->op[i].value;
    scheduler->latch = slot->final_latch;
}


/* Append final slot. */
void scheduler_append_slot(ScheduledStream *const stream, const SampleSlot *slot)
{
    if (stream == NULL || stream->slots == NULL || slot == NULL ||
        stream->count == UINT32_MAX)
        return;

    stream->slots[ stream->count ] = *slot;
    ++(stream->count);
}


/* Initialize. */
void scheduler_init(SchedulerState *const state)
{
    if (state == NULL)
        return;

    /*
     * Match the known startup writes emitted at the beginning of ym_player:
     * R7=mixer YM_INITIAL_MIXER, R6=noise period YM_INITIAL_NOISE_PERIOD,
     * R8-R10 volume YM_INITIAL_VOLUME. The last write
     * selects R10, so the real latch is known to be 10 before sample slot 0.
     */
    memset(state, 0xff, sizeof(*state));
    state->reg[6] = (uint8_t)YM_INITIAL_NOISE_PERIOD;
    state->reg[7] = (uint8_t)YM_INITIAL_MIXER;
    state->reg[8] = (uint8_t)YM_INITIAL_VOLUME;
    state->reg[9] = (uint8_t)YM_INITIAL_VOLUME;
    state->reg[10] = (uint8_t)YM_INITIAL_VOLUME;
    state->latch = (uint8_t)YM_INITIAL_LATCH;
}
