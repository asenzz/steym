/*
    viterbi.c

    Beam-search Viterbi optimizer
    for Atari STE YM2149 PCM playback.

*/


#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "viterbi.h"
#include "scheduler.h"


#define INF 1e30f
#define AUDIO_WEIGHT       100000.0f
#define FUTURE_WEIGHT      50000.0f
#define CYCLE_WEIGHT       1000000.0f
#define NOISE_WEIGHT       20000.0f


static float absf_fast(const float x)
{
    return x < 0 ? -x : x;
}


/* YM + noise output model. */
static float model_output(const YMState *s, float noise_error)
{
    float v = s->amplitude;

    /* Noise contribution.

        Small because noise is used
        as dithering, not primary audio.
    */

    v += noise_error * 0.05f;
    return v;
}


/* Calculate scheduler legality. */
static float cycle_cost(const YMState *state, const int latch)
{
    SchedulerState scheduler;
    SampleSlot slot;
    scheduler_init( &scheduler);
    scheduler.latch = latch;

    const int cycles = scheduler_make_slot( &scheduler, state, &slot);
    if(cycles<=SAMPLE_CYCLES) return 0;
    return (cycles - SAMPLE_CYCLES) * CYCLE_WEIGHT;
}


/* Full transition cost. */
static float transition_cost(const ViterbiNode *old, const Candidate *candidate, const float current_pcm, const float next_pcm)
{
    float cost=0;

    /* Current audio error. */
    const float out = model_output( &candidate->state, old->noise_error);
    cost += absf_fast( current_pcm - out) * AUDIO_WEIGHT;

    /* Look ahead. */
    const float next_error = absf_fast( next_pcm - candidate->state.amplitude);
    cost += next_error * FUTURE_WEIGHT;

    /* YM bus transition. */
    cost += optimizer_transition_cost( &old->ym, &candidate->state);

    /* Exact 182 cycle constraint. */
    cost += cycle_cost( &candidate->state, old->latch);

    /* Noise sigma delta. */
    cost += absf_fast( old->noise_error) * NOISE_WEIGHT;

    return cost;
}


/* Insert node into beam. */
static void beam_insert(ViterbiBeam *const beam, const ViterbiNode *node)
{
    int pos=-1;
    for(int i=0; i<beam->count; ++i) 
        if(node->cost < beam->node[i].cost) {
            pos=i;
            break;
        }

    if(pos<0) {
        if(beam->count<BEAM_WIDTH) beam->node[ beam->count++ ]=*node;
        return;
    }

    if(beam->count<BEAM_WIDTH) ++beam->count;

    for(int i=beam->count-1; i>pos; --i) beam->node[i]= beam->node[i-1];

    beam->node[pos]= *node;
}


/* Optimize complete PCM. */
void viterbi_optimize(const float *pcm, const int samples, YMState *const output)
{
    Optimizer optimizer;
    optimizer_init(&optimizer);
    ViterbiBeam current;
    memset(&current, 0, sizeof(current));
    current.count=1;
    current.node[0].ym = optimizer.current;
    current.node[0].latch=-1;
    current.node[0].noise_error=0;
    current.node[0].cost=0;
    ViterbiBeam *const history = malloc(sizeof(ViterbiBeam) * samples);
    for(int n=0; n<samples; ++n) {
        ViterbiBeam next;
        memset( &next, 0, sizeof(next));
        for(int b=0; b<current.count; ++b) {
            optimizer.current = current.node[b].ym;
            SigmaDeltaState sd;
            memset(&sd, 0, sizeof(sd));
            optimizer_generate_candidates_noise( &optimizer, pcm[n], &sd);
            for(int c=0; c<optimizer.candidate_count; ++c) {
                ViterbiNode node;
                node.ym = optimizer.candidates[c].state;
                node.latch = node.ym.selected_register;
                node.noise_error = current.node[b].noise_error + pcm[n] - node.ym.amplitude;
                node.cost = current.node[b].cost + transition_cost(&current.node[b], &optimizer.candidates[c], pcm[n], n+1<samples ? pcm[n+1] : pcm[n]);
                node.previous=b;
                beam_insert(&next, &node);
            }
        }

        history[n]=current;
        current=next;
    }

    /* Select cheapest final state. */
    int best=0;
    for(int i=1; i<current.count; i++) 
        if(current.node[i].cost < current.node[best].cost) 
	    best=i;

    /* Backtracking. The compact version stores only
        predecessor indices. A production
        compiler should store full beam
        history. */
    for(int i=samples-1; i>=0; i--) {
        output[i]= current.node[best].ym;
        best = current.node[best].previous;
    }

    free(history);
}
