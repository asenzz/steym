#ifndef STE_YM_SCHEDULER_H
#define STE_YM_SCHEDULER_H

#include <stdint.h>

#include "optimizer.h"

#define SAMPLE_CYCLES 182
#define SLOT_PADDING_CYCLES 14
#define MAX_SLOT_OPERATION_CYCLES (SAMPLE_CYCLES - SLOT_PADDING_CYCLES)
#define MAX_OPERATIONS 16
#define REG_COUNT 16

#define YM_INITIAL_NOISE_PERIOD 1u
#define YM_INITIAL_MIXER 255u
#define YM_INITIAL_VOLUME 0u
#define YM_INITIAL_LATCH 10u

typedef struct
{
    uint8_t reg;
    uint8_t value;
} YMOperation;



typedef struct
{
    YMOperation op[MAX_OPERATIONS];

    uint8_t count;
    uint16_t cycles;
    uint8_t initial_latch;
    uint8_t final_latch;
} SampleSlot;

typedef struct
{
    SampleSlot *slots;
    uint32_t count;
} ScheduledStream;

typedef struct
{
    uint8_t reg[REG_COUNT];
    uint8_t latch;
} SchedulerState;


/* Initialize the register shadow to match player_codegen's YM startup writes. */
void scheduler_init(SchedulerState *state);

uint16_t scheduler_make_slot(const SchedulerState *state, const YMState *state_in, SampleSlot *slot);

uint16_t scheduler_make_slot_feedback(
    const SchedulerState *state,
    const Candidate *candidates,
    uint8_t candidate_count,
    YMState *chosen,
    SampleSlot *slot);

void scheduler_append_slot(ScheduledStream *stream, const SampleSlot *slot);

void scheduler_commit_slot(SchedulerState *scheduler, const SampleSlot *slot);

#endif
