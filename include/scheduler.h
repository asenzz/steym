#ifndef STE_YM_SCHEDULER_H
#define STE_YM_SCHEDULER_H


#include <stdint.h>

#include "optimizer.h"


#define SAMPLE_CYCLES 182

#define MAX_OPERATIONS 16



typedef struct
{
    uint8_t reg;

    uint8_t value;

} YMOperation;



typedef struct
{
    YMOperation op[MAX_OPERATIONS];

    int count;

    int cycles;

    int initial_latch;

    int final_latch;


} SampleSlot;



typedef struct
{
    SampleSlot *slots;

    int count;


} ScheduledStream;



typedef struct
{
    uint8_t reg[16];


    int latch;


} SchedulerState;



void
scheduler_init(
        SchedulerState *state);



int
scheduler_make_slot(
        SchedulerState *state,

        const YMState *state_in,

        SampleSlot *slot);



int
scheduler_make_slot_feedback(
        SchedulerState *state,

        const Candidate *candidates,

        int candidate_count,

        YMState *chosen,

        SampleSlot *slot);



void
scheduler_append_slot(
        ScheduledStream *stream,

        const SampleSlot *slot);


void
scheduler_commit_slot(
        SchedulerState *scheduler,

        const SampleSlot *slot);

#endif
