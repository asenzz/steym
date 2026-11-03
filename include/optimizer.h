#ifndef STE_YM_OPTIMIZER_H
#define STE_YM_OPTIMIZER_H


#include <stdbool.h>
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

    /* Difference between target PCM and generated YM output. */
    float audio_error;

    /* Estimated 68000/YM bus cost. */
    uint8_t cycle_cost;

    /* Combined optimization metric. */
    float total_cost;
} Candidate;

/* 125 local states + 16 DAC anchors + 1 noise-off + 3 noise-on. */
#define MAX_CANDIDATES 145

_Static_assert(MAX_CANDIDATES <= UINT8_MAX, "candidate count must fit uint8_t");
_Static_assert(MAX_CANDIDATES - 1 <= INT16_MAX, "candidate index must fit int16_t");

/* Optimizer state. */
typedef struct
{
    YMState current;
    Candidate candidates[MAX_CANDIDATES];
    uint8_t candidate_count;
} Optimizer;


/* Sigma-delta error feedback state. */
typedef struct
{
    float error;
} SigmaDeltaState;


/* Initialize optimizer. */
void optimizer_init(Optimizer *opt);

/* Generate possible YM states around the current state. */
void optimizer_generate_candidates(Optimizer *opt, float target);

/* Generate candidates including YM noise dithering. */
void optimizer_generate_candidates_noise(Optimizer *opt, float target, SigmaDeltaState *sd);

uint8_t optimizer_transition_cost(const YMState *old, const YMState *next);

/* Existing optimizer helpers. */
int16_t optimizer_best_candidate(Optimizer *opt);
void optimizer_commit(Optimizer *opt, const YMState *state);

/* Maximum output amplitude supported by the YM output model. */
float optimizer_output_max(void);

bool optimizer_update_sigma_delta(SigmaDeltaState *sd, float input, float output);

bool pcm_to_quantizer_target(
    const float pcm,
    float *const target);

#endif
