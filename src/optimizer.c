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
    if (opt == NULL)
        return;

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
/*
 * Statistical YM2149 output model.
 *
 * Assumptions:
 * - An enabled tone generator has approximately 50% duty cycle.
 * - An enabled noise generator has approximately 50% duty cycle.
 * - Tone and noise are statistically independent.
 * - A disabled generator contributes a logical high to the mixer.
 *
 * This estimates average output. It does not simulate the actual
 * YM2149 noise sequence or its temporal correlation.
 */
static float ym_output(const YMState *const state)
{
    float output = 0.0f;

    for (uint8_t ch = 0; ch < 3; ++ch) {
        const float level = ym_volume[state->volume[ch]];

        const int tone_enabled =
            (state->mixer & (1u << ch)) == 0;

        const int noise_enabled =
            (state->mixer & (1u << (ch + 3u))) == 0;

        float duty = 1.0f;

        if (tone_enabled)
            duty *= 0.5f;

        if (noise_enabled)
            duty *= 0.5f;

        output += level * duty;
    }

    return output;
}

/* Transition cost. This models the STE YM bus. */
uint16_t optimizer_transition_cost(const YMState *const old, const YMState *const next)
{
    uint16_t cost = 0;
    uint8_t latch = old->selected_register < 0
        ? UINT8_MAX
        : (uint8_t)old->selected_register;

    /* Keep this order in sync with scheduler.c. */
    static const uint8_t order[] = { 7, 6, 8, 9, 10 };
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
        const uint8_t reg = order[i];
        int changed = 0;

        switch (reg) {
            case 6: changed = old->noise_period != next->noise_period; break;
            case 7: changed = old->mixer != next->mixer; break;
            case 8: changed = old->volume[0] != next->volume[0]; break;
            case 9: changed = old->volume[1] != next->volume[1]; break;
            case 10: changed = old->volume[2] != next->volume[2]; break;
            default: break;
        }

        if (!changed)
            continue;

        if (latch != reg) {
            cost = (uint16_t)(cost + 20u);
            latch = reg;
        }
        cost = (uint16_t)(cost + 20u);
    }
    return cost;
}


/* Add candidate. */
static void add_candidate(Optimizer *const opt, const YMState *const state, const float target)
{
    if (opt == NULL || state == NULL || !isfinite(target) ||
        opt->candidate_count >= MAX_CANDIDATES)
        return;

    Candidate *const c = &opt->candidates[opt->candidate_count];
    ++(opt->candidate_count);
    c->state = *state;
    c->state.amplitude = ym_output(&c->state);
    c->audio_error = fabsf(target - c->state.amplitude);
    c->cycle_cost = optimizer_transition_cost(&opt->current, &c->state);
    c->total_cost = c->audio_error * AUDIO_WEIGHT + c->cycle_cost;
}


/* Generate nearby tone states. */
void optimizer_generate_candidates(Optimizer *const opt, const float target)
{
    if (opt == NULL || !isfinite(target))
        return;

    opt->candidate_count = 0;
    YMState base = opt->current;
    for (int8_t a = -2; a <= 2; ++a) {
        const int8_t va = base.volume[0] + a;
        if (va < 0 || va > 15) continue;
        for (int8_t b = -2; b <= 2; ++b) {
            const int8_t vb = base.volume[1] + b;
            if (vb < 0 || vb > 15) continue;
            for (int8_t c = -2; c <=2; ++c) {
                const int8_t vc = base.volume[2] + c;
                if (vc < 0 || vc > 15) continue;
                YMState s = base;
                s.volume[0] = va;
                s.volume[1] = vb;
                s.volume[2] = vc;
                add_candidate(opt, &s, target);
            }
        }
    }
}



/* Add noise-assisted candidates. */
#define YM_DITHER_NOISE_PERIOD 8u

void optimizer_generate_candidates_noise(
    Optimizer *const opt,
    const float target,
    SigmaDeltaState *const sd)
{
    if (opt == NULL || !isfinite(target))
        return;

    opt->candidate_count = 0;

    const float full_scale = optimizer_output_max();

    if (!isfinite(full_scale) || full_scale <= 0.0f)
        return;

    float decision_target = target;

    if (sd != NULL) {
        if (!isfinite(sd->error))
            return;

        decision_target += sd->error;

        if (!isfinite(decision_target))
            return;
    }

    /* Saturate the quantizer input to the representable range. */
    if (decision_target < 0.0f)
        decision_target = 0.0f;
    else if (decision_target > full_scale)
        decision_target = full_scale;

    optimizer_generate_candidates(opt, decision_target);

    const YMState base = opt->current;

    /*
     * Add noise-assisted candidates.
     * The noise generator's period is fixed in this statistical model;
     * accumulated PCM error is not used to fabricate a noise waveform.
     */
    for (uint8_t ch = 0; ch < 3; ++ch) {
        YMState state = base;

        /* Enable noise on this channel: mixer bits 3, 4 and 5. */
        state.mixer = (uint8_t)(
            state.mixer & (uint8_t)~(1u << (ch + 3u)));

        state.noise_period = YM_DITHER_NOISE_PERIOD;

        /*
         * add_candidate() calculates amplitude from the statistical
         * mixer model and computes error against decision_target.
         */
        add_candidate(opt, &state, decision_target);
    }
}


int pcm_to_quantizer_target(
    const float pcm,
    float *const target)
{
    if (target == NULL || !isfinite(pcm))
        return 0;

    /*
     * This mapping assumes normalized signed PCM in [-1, 1]
     * and a unipolar YM amplitude model in [0, full_scale].
     */
    float sample = pcm;

    if (sample < -1.0f)
        sample = -1.0f;
    else if (sample > 1.0f)
        sample = 1.0f;

    const float full_scale = optimizer_output_max();

    if (!isfinite(full_scale) || full_scale <= 0.0f)
        return 0;

    *target = (sample + 1.0f) * (0.5f * full_scale);

    return isfinite(*target);
}

/* Sigma-delta feedback. */
#include <math.h>

/*
 * Bounded first-order error feedback:
 *
 *   e[n+1] = clamp(e[n] + input[n] - output[n],
 *                  -full_scale, +full_scale)
 *
 * The bound is derived from the output model's full-scale range.
 * Input and output MUST use the same amplitude units.
 */
int optimizer_update_sigma_delta(
    SigmaDeltaState *const sd,
    const float input,
    const float output)
{
    if (sd == NULL ||
        !isfinite(sd->error) ||
        !isfinite(input) ||
        !isfinite(output))
        return 0;

    const double full_scale = (double)optimizer_output_max();

    if (!isfinite(full_scale) || full_scale <= 0.0)
        return 0;

    /* Use double precision for the intermediate calculation. */
    double error =
        (double)sd->error +
        (double)input -
        (double)output;

    if (!isfinite(error))
        return 0;

    /* Anti-windup: keep the error within the representable range. */
    if (error > full_scale)
        error = full_scale;
    else if (error < -full_scale)
        error = -full_scale;

    sd->error = (float)error;

    return isfinite(sd->error);
}

float optimizer_output_max(void)
{
    return 3.0f * ym_volume[15];
}

/* Select best current candidate. Used by scheduler fallback. */
int optimizer_best_candidate(Optimizer *const opt)
{
    if (opt == NULL)
        return -1;

    float best = 1e30f;
    int index = -1;
    for (uint16_t i = 0; i < opt->candidate_count; ++i) {
        if(opt->candidates[i].total_cost < best) {
            best = opt->candidates[i].total_cost;
            index = (int)i;
        }
    }
    return index;
}


/* Commit chosen state. */
void optimizer_commit(Optimizer *const opt, const YMState *const state)
{
    if (opt != NULL && state != NULL)
        opt->current = *state;
}
