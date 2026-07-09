#ifndef STE_YM_VITERBI_H
#define STE_YM_VITERBI_H

#include "optimizer.h"

#define BEAM_WIDTH 64

typedef struct
{
    YMState ym;

    /* YM register latch state. Needed because it changes future cycle cost. */

    int latch;

    /* Noise sigma-delta state. */

    float noise_error;

    float cost;

    int previous;

} ViterbiNode;


typedef struct
{
    ViterbiNode node[BEAM_WIDTH];

    int count;

} ViterbiBeam;


void viterbi_optimize(const float *pcm, const int samples, YMState *const output);


#endif
