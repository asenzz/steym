/*
    scheduler.c

    Final STE YM2149 scheduler.

    Converts YM states into
    fixed 182-cycle slots.

*/


#include <string.h>
#include <math.h>
#include "scheduler.h"


#define YM_SELECT_COST 20
#define YM_WRITE_COST  20


static const uint8_t order[] =
{
    7,
    6,
    8,
    9,
    10
};


static uint8_t get_register(const YMState *s, const uint8_t r)
{
    switch(r)
    {
        case 6: return s->noise_period;

        case 7: return s->mixer;

        case 8: return s->volume[0];

        case 9: return s->volume[1];

        case 10: return s->volume[2];

        default: return 0;
    }
}



/* Add operation. */
static void add_operation(SampleSlot *const slot, const uint8_t reg, const uint8_t value)
{
    if(slot->count >= MAX_OPERATIONS) return;
    slot->op[slot->count].reg = reg;
    slot->op[slot->count].value = value;
    ++(slot->count);
}


/* Estimate the 68000 instruction cycles for this operation sequence. */
static uint16_t calculate_cycles(SampleSlot *const slot)
{
    uint16_t cycles = 0;
    uint8_t latch = slot->initial_latch;

    for (uint8_t i = 0; i < slot->count; ++i) {
        if (latch != slot->op[i].reg) {
            cycles += YM_SELECT_COST;
            latch = slot->op[i].reg;
        }

        cycles += YM_WRITE_COST;
    }

    slot->final_latch = latch;
    slot->cycles = cycles;

    return cycles;
}


/* Create a raw slot from YM state. */
uint16_t scheduler_make_slot(const SchedulerState *state, const YMState *state_in, SampleSlot *const slot)
{
    if (state == NULL || state_in == NULL || slot == NULL)
        return SAMPLE_CYCLES + 1;

    memset(slot, 0, sizeof(*slot));
    slot->initial_latch = state->latch;
    for (uint8_t i = 0; i < sizeof(order); ++i) {
        const uint8_t r = order[i];
        const uint8_t value = get_register(state_in, r);
        if(state->reg[r] != value) add_operation(slot, r, value);
    }
    return calculate_cycles(slot);
}


/* Score alternative candidate. */
static float candidate_score(const Candidate *candidate, const uint16_t cycles)
{
    (void)cycles;
    return candidate->audio_error * 100000.0f + candidate->cycle_cost;
}


/* Feedback search. */
uint16_t scheduler_make_slot_feedback(const SchedulerState *state, const Candidate *candidates, const uint8_t candidate_count, YMState *const chosen, SampleSlot *const slot)
{
    if (state == NULL || candidates == NULL || candidate_count == 0 ||
        chosen == NULL || slot == NULL)
        return SAMPLE_CYCLES + 1;

    float best = INFINITY;
    uint16_t best_index = UINT16_MAX;
    SampleSlot best_slot;

    for (uint16_t i = 0; i < candidate_count; ++i) {
        SampleSlot test;
        const uint16_t cycles = scheduler_make_slot(state, &candidates[i].state, &test);
        if (cycles > MAX_SLOT_OPERATION_CYCLES)
            continue;

        const float score = candidate_score(&candidates[i], cycles);
        if (isfinite(score) && score < best) {
            best = score;
            best_index = i;
            best_slot = test;
        }
    }

    if (best_index == UINT16_MAX)
        return SAMPLE_CYCLES + 1;

    *chosen = candidates[best_index].state;
    *slot = best_slot;
    return best_slot.cycles;
}


/* Commit emitted slot. The next slot starts with the correct YM latch. */
void scheduler_commit_slot(SchedulerState *const scheduler, const SampleSlot *slot)
{
    if (scheduler == NULL || slot == NULL)
        return;

    for(uint8_t i = 0; i < slot->count; ++i)
        if (slot->op[i].reg < REG_COUNT)
            scheduler->reg[ slot->op[i].reg ] = slot->op[i].value;
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
