/*
    optimizer.c

    Atari STE YM2149 PCM optimizer.

    Responsibilities:

      - YM state representation
      - analogue output estimation
      - candidate generation
      - transition cost calculation
      - sigma-delta noise candidates
*/


#include <string.h>
#include <math.h>
#include "optimizer.h"

#define YM_REGISTER_COUNT 16
#define AUDIO_WEIGHT 100000.0f

/* Approximate YM2149 logarithmic
    volume curve.
    This can later be replaced with
    a measured STE amplifier curve. */

static const float ym_volume[16] =
{
    0.000000f,
    0.004800f,
    0.006800f,
    0.009600f,
    0.013700f,
    0.019300f,
    0.027300f,
    0.038500f,

    0.054400f,
    0.076900f,
    0.108700f,
    0.153800f,
    0.217400f,
    0.307700f,
    0.435900f,
    0.617000f
};



/* Initialize optimizer. */

void optimizer_init(Optimizer *const opt)
{
    memset( opt, 0, sizeof(*opt));
    opt->current.volume[0]=0;
    opt->current.volume[1]=0;
    opt->current.volume[2]=0;
    opt->current.mixer=0xff;
    opt->current.noise_period=1;
    opt->current.selected_register=-1;
    opt->current.amplitude=0.0f;
}


/* Estimate YM DAC output. */
static float ym_output(const YMState *const state)
{
    return ym_volume[ state->volume[0] ] + ym_volume[ state->volume[1] ] + ym_volume[ state->volume[2] ];
}


/* Transition cost. This models the STE YM bus. */
int optimizer_transition_cost(const YMState *const old, const YMState *const next)
{
    int cost = 0;
    for(uint8_t ch=0; ch<3; ++ch) {
        const uint8_t reg = 8 + ch;
        if(old->volume[ch] != next->volume[ch]) 
            cost += old->selected_register == reg ? 16 : 32;
    }
    if(old->mixer != next->mixer) cost += old->selected_register == 7 ? 16 : 32;
    if(old->noise_period != next->noise_period) cost += old->selected_register == 6 ? 16 : 32;
    return cost;
}


/* Add candidate. */
static void add_candidate(Optimizer *const opt, const YMState *const state, const float target)
{
    if(opt->candidate_count>=MAX_CANDIDATES) return;
    Candidate *const c = &opt->candidates[opt->candidate_count++];
    memset(c, 0, sizeof(*c));
    c->state=*state;
    c->state.amplitude = ym_output(&c->state);
    c->audio_error = fabsf(target - c->state.amplitude);
    c->cycle_cost = optimizer_transition_cost(&opt->current, &c->state);
    c->total_cost = c->audio_error * AUDIO_WEIGHT + c->cycle_cost;
}


/* Generate nearby tone states. */
void optimizer_generate_candidates(Optimizer *const opt, const float target)
{
    opt->candidate_count=0;
    YMState base = opt->current;
    for(int8_t a=-2;a<=2;++a) {
        const int va = base.volume[0]+a;
        if(va<0 || va>15) continue;
        for(int8_t b=-2;b<=2;++b) {
            const int vb = base.volume[1]+b;
            if(vb<0 || vb>15) continue;
            for(int8_t c=-2;c<=2;++c) {
                const int vc = base.volume[2]+c;
                if(vc<0 || vc>15) continue;
                YMState s=base;
                s.volume[0]=va;
                s.volume[1]=vb;
                s.volume[2]=vc;
                add_candidate(opt, &s, target);
            }
        }
    }
}



/* Add noise-assisted candidates. */
void optimizer_generate_candidates_noise(Optimizer *const opt, const float target, SigmaDeltaState *const sd)
{
    optimizer_generate_candidates(opt, target);
    if(sd == NULL) return;

    /* Sigma-delta prediction. Positive error means the previous output was too low, so prefer slightly louder/noisier states.
       Negative error means reduce output. */
    float correction = sd->error;
    YMState base = opt->current;

    /* Add noise assisted states. YM2149 mixer: bit 3 = noise A bit 4 = noise B bit 5 = noise C */
    for(int8_t ch=0; ch<3; ++ch) {
        YMState s = base;

        /* Enable noise on channel. */
        s.mixer &= ~(1 << (3 + ch));

        /* Adjust noise period using sigma-delta feedback. YM noise period: 1..31 */
        int period = 8;
        if(correction > 0.0f) period--;
        else if(correction < 0.0f) period++;

        if(period < 1) period=1;
        if(period > 31) period=31;

        s.noise_period = period;

        /* Approximate noise contribution. */
        s.amplitude += correction * 0.02f;
        add_candidate(opt, &s, target);
    }
}


/* Sigma-delta feedback. */
void optimizer_update_sigma_delta(SigmaDeltaState *const sd, const float input, const float output)
{
    sd->error += input - output;
}


/* Select best current candidate. Used by scheduler fallback. */
int optimizer_best_candidate(Optimizer *const opt)
{
    float best=1e30f;
    int index=-1;
    for(int i=0; i<opt->candidate_count; ++i) {
        if(opt->candidates[i].total_cost < best) {
            best = opt->candidates[i].total_cost;
            index=i;
        }
    }
    return index;
}


/* Commit chosen state. */
void optimizer_commit(Optimizer *const opt, const YMState *const state)
{
    opt->current = *state;
}
