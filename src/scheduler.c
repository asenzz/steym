/*
    scheduler.c

    Final STE YM2149 scheduler.

    Converts YM states into
    fixed 182-cycle slots.

*/


#include <string.h>
#include <math.h>
#include "scheduler.h"


#define YM_SELECT_COST 16
#define YM_WRITE_COST  16


static const uint8_t order[] =
{
    7,
    6,
    8,
    9,
    10
};


static uint8_t get_register(const YMState *s, const int r)
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
    slot->count++;
}


/* Calculate exact scheduler cost. */
static int calculate_cycles(SampleSlot *const slot)
{
    int cycles=0;
    int latch = slot->initial_latch;
    for(int i=0; i<slot->count; ++i) {
        if(latch != slot->op[i].reg) {
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
int scheduler_make_slot(SchedulerState *const scheduler, const YMState *const input, SampleSlot *const slot)
{
    memset(slot, 0, sizeof(*slot));
    slot->initial_latch = scheduler->latch;
    for (unsigned i=0; i<sizeof(order); ++i) {
        const int r = order[i];
        const uint8_t value = get_register(input, r);
        if(scheduler->reg[r] != value) add_operation(slot, r, value);
    }
    calculate_cycles(slot);
    return slot->cycles;
}


/* Score alternative candidate. */
static float candidate_score(const Candidate *candidate, const int cycles)
{
    int overflow = cycles - SAMPLE_CYCLES;
    if(overflow<0) overflow=0;
    return candidate->audio_error * 100000.0f + candidate->cycle_cost + overflow * 1000000.0f;
}


/* Feedback search. */
int scheduler_make_slot_feedback( SchedulerState *const scheduler, const Candidate *candidates, const int candidate_count, YMState *const chosen, SampleSlot *const best_slot)
{
    float best = 1e30f;
    int best_index=-1;

    for(int i=0; i<candidate_count; ++i) {
        SampleSlot test;
        int cycles = scheduler_make_slot(scheduler, &candidates[i].state, &test);
        float score = candidate_score(&candidates[i], cycles);

        /* Legal slots strongly preferred. */
        if(cycles <= SAMPLE_CYCLES) score -= 500000.0f;

        if(score < best) {
            best = score;
            best_index=i;
            *best_slot = test;
        }
    }

    if(best_index < 0) return -1;

    *chosen = candidates[best_index].state;
    return best_slot->cycles;
}


/* Commit emitted slot. The next slot starts with the correct YM latch. */
void scheduler_commit_slot(SchedulerState *const scheduler, const SampleSlot *slot)
{
    for(int i=0; i<slot->count; ++i) 
        scheduler->reg[ slot->op[i].reg ] = slot->op[i].value;
    scheduler->latch = slot->final_latch;
}


/* Append final slot. */
void scheduler_append_slot(ScheduledStream *const stream, const SampleSlot *slot)
{
    stream->slots[ stream->count++ ] = *slot;
}


/* Initialize. */
void scheduler_init(SchedulerState *const scheduler)
{
    memset(scheduler, 0, sizeof(*scheduler));
    scheduler->latch=-1;
    for(int i=0;i<16;++i)
        scheduler->reg[i]=0xff;
}
