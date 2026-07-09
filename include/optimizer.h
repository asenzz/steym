#ifndef STE_YM_OPTIMIZER_H
#define STE_YM_OPTIMIZER_H


#include <stdint.h>



/*
    YM2149 optimized state.

    This is the output of the optimizer
    and the input to the Devpac backend.

*/

typedef struct
{
    uint8_t mixer;

    uint8_t volume[3];

    uint8_t noise_period;


    /* YM register latch position.
        -1 after reset
        0..15 otherwise
    */
    int8_t selected_register;


    /* Estimated analog output level. */
    float amplitude;

} YMState;



/* Candidate state. Generated during optimization. */

typedef struct
{
    YMState state;

    /* Difference between target PCM
        and generated YM output. */
    float audio_error;

    /* Estimated 68000/YM bus cost. */
    int cycle_cost;

    /* Combined optimization metric. */
    float total_cost;

} Candidate;

#define MAX_CANDIDATES 256

/* Optimizer state. */
typedef struct
{
    YMState current;

    Candidate candidates[MAX_CANDIDATES];

    int candidate_count;

} Optimizer;


/* Sigma-delta error feedback state. */
typedef struct
{
    float error;
} SigmaDeltaState;


/* Initialize optimizer. */
void optimizer_init(Optimizer *const opt);

/* Generate possible YM states around the current state. */
void optimizer_generate_candidates(Optimizer *const opt, const float target);

/* Generate candidates including YM noise dithering. */
void optimizer_generate_candidates_noise(Optimizer *const opt, const float target, SigmaDeltaState *const sd);

/* Run Viterbi optimization. Converts PCM samples into a YM state sequence. */
void optimizer_optimize(Optimizer *const opt, const float *samples, const int count, YMState *const output);

/* Cost evaluation for a candidate. */
float optimizer_state_cost(const Candidate *candidate, const float target, SigmaDeltaState *const sd);

/* Commit chosen state. */
void optimizer_commit_state(Optimizer *const opt, Candidate *const chosen, SigmaDeltaState *const sd, const float target);

int optimizer_transition_cost(const YMState *old, const YMState *next);

#endif
