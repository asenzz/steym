#ifndef STE_YM_VITERBI_H
#define STE_YM_VITERBI_H

#include <stddef.h>
#include <stdint.h>

#include "scheduler.h"

#define BEAM_WIDTH 64

typedef struct
{
    YMState ym;

    /* Register shadow and actual YM latch for this search path. */
    SchedulerState scheduler;

    /* Noise sigma-delta state. */

    float noise_error;

    float cost;

    int previous;

} ViterbiNode;


typedef struct
{
    ViterbiNode node[BEAM_WIDTH];

    uint8_t count;

} ViterbiBeam;


/* Returns 0 on success and -1 on invalid input, allocation failure,
   or when no cycle-feasible path can be found. */
int viterbi_optimize(const float *pcm, size_t samples, YMState *output);


#endif
