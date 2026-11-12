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


#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "optimizer.h"
#include "ym_timing.h"

#define YM_REGISTER_COUNT 16
#define AUDIO_WEIGHT 100000.0f
#define YM_VOLUME_MAX 0.617000f
#define YM_FULL_SCALE (3.0f * YM_VOLUME_MAX)
#define YM_MIXER_DISABLE_TONE_NOISE 0x3FU
#define YM_MIXER_NOISE_BITS 0x38U

/* Approximate logarithmic curve, pre-scaled for 1/4, 1/2 and full duty.
   Powers of two preserve the former runtime products exactly. */

#define YM_VOLUME_CURVE(scale) \
    0.000000f * (scale), 0.004800f * (scale), 0.006800f * (scale), 0.009600f * (scale), \
    0.013700f * (scale), 0.019300f * (scale), 0.027300f * (scale), 0.038500f * (scale), \
    0.054400f * (scale), 0.076900f * (scale), 0.108700f * (scale), 0.153800f * (scale), \
    0.217400f * (scale), 0.307700f * (scale), 0.435900f * (scale), YM_VOLUME_MAX * (scale)

static const float ym_volume[3][16] = {
    { YM_VOLUME_CURVE(0.25f) },
    { YM_VOLUME_CURVE(0.5f) },
    { YM_VOLUME_CURVE(1.0f) }
};

#undef YM_VOLUME_CURVE


static bool volumes_valid(const YMState *const s)
{
    return s->volume[0] <= 15u && s->volume[1] <= 15u && s->volume[2] <= 15u;
}

static float clampf(float value, float min, float max)
{
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}



/* Initialize optimizer. */
void optimizer_init(Optimizer *const opt)
{
    if (opt == NULL)
        return;

    memset(opt, 0, sizeof *opt);
    opt->current = (YMState){
        .mixer = 0xFFu,
        .noise_period = 1u,
        .selected_register = -1
    };
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

float optimizer_state_output(const YMState *const state)
{
    const uint8_t m = state->mixer;

    const uint8_t idx0 = (uint8_t)((m & 1u) + ((m & 8u) >> 3));
    const uint8_t idx1 = (uint8_t)(((m & 2u) >> 1) + ((m & 16u) >> 4));
    const uint8_t idx2 = (uint8_t)(((m & 4u) >> 2) + ((m & 32u) >> 5));

    return ym_volume[idx0][state->volume[0]]
         + ym_volume[idx1][state->volume[1]]
         + ym_volume[idx2][state->volume[2]];
}

/* Five distinct registers: five selections, four data writes and one mixer. */
_Static_assert(9u * YM_REGISTER_ACCESS_CYCLES + YM_MIXER_WRITE_CYCLES <= UINT8_MAX,
    "transition cost must fit uint8_t");

static inline void add_register_cost(uint8_t *const cost, uint8_t *const latch, const uint8_t reg, const bool changed)
{
    if (!changed) return;

    if (*latch != reg) {
        *cost = (uint8_t)(*cost + YM_REGISTER_ACCESS_CYCLES);
        *latch = reg;
    }
    *cost = (uint8_t)(*cost + (reg == 7u ? YM_MIXER_WRITE_CYCLES : YM_REGISTER_ACCESS_CYCLES));
}

/* Transition cost. This models the STE YM bus. */
uint8_t optimizer_transition_cost(const YMState *const old, const YMState *const next)
{
    uint8_t cost = 0;
    uint8_t latch = (old->selected_register >= 0 && old->selected_register < YM_REGISTER_COUNT)
        ? (uint8_t)old->selected_register
        : UINT8_MAX;

    /* Keep this order in sync with scheduler.c. */
    add_register_cost(&cost, &latch, 7u,  old->mixer != next->mixer);
    add_register_cost(&cost, &latch, 6u,  old->noise_period != next->noise_period);
    add_register_cost(&cost, &latch, 8u,  old->volume[0] != next->volume[0]);
    add_register_cost(&cost, &latch, 9u,  old->volume[1] != next->volume[1]);
    add_register_cost(&cost, &latch, 10u, old->volume[2] != next->volume[2]);
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
    const float amplitude = optimizer_state_output(state);
    c->state = *state;
    c->state.amplitude = amplitude;
    c->audio_error = fabsf(target - amplitude);
    c->cycle_cost = optimizer_transition_cost(&opt->current, &c->state);
    c->total_cost = c->audio_error * AUDIO_WEIGHT + (float)c->cycle_cost;
}

/* Generate nearby tone states. */
void optimizer_generate_candidates(Optimizer *const opt, const float target)
{
    if (opt == NULL)
        return;

    opt->candidate_count = 0;
    if (!isfinite(target) || !volumes_valid(&opt->current))
        return;
    YMState base = opt->current;
    for (int8_t a = -2; a <= 2; ++a) {
        const int8_t va = (int8_t)(base.volume[0] + a);
        if (va < 0 || va > 15) continue;
        for (int8_t b = -2; b <= 2; ++b) {
            const int8_t vb = (int8_t)(base.volume[1] + b);
            if (vb < 0 || vb > 15) continue;
            for (int8_t c = -2; c <=2; ++c) {
                const int8_t vc = (int8_t)(base.volume[2] + c);
                if (vc < 0 || vc > 15) continue;
                YMState s = base;
                s.volume[0] = (uint8_t)va;
                s.volume[1] = (uint8_t)vb;
                s.volume[2] = (uint8_t)vc;
                add_candidate(opt, &s, target);
            }
        }
    }

    /* A YM volume write can jump directly to any level. Keep DAC-only
       anchors across the full range as well as the local refinement. */
    for (uint8_t volume = 0; volume < 16; ++volume) {
        YMState s = base;
        s.mixer = (uint8_t)(s.mixer | YM_MIXER_DISABLE_TONE_NOISE);
        s.volume[0] = (uint8_t)volume;
        s.volume[1] = (uint8_t)volume;
        s.volume[2] = (uint8_t)volume;
        add_candidate(opt, &s, target);
    }
}



/* Add noise-assisted candidates. */
#define YM_DITHER_NOISE_PERIOD 8u

void optimizer_generate_candidates_noise(
    Optimizer *const opt,
    const float target,
    SigmaDeltaState *const sd)
{
    if (opt == NULL)
        return;

    opt->candidate_count = 0;
    if (!isfinite(target) || !volumes_valid(&opt->current))
        return;

    float decision_target = target;

    if (sd != NULL) {
        if (!isfinite(sd->error))
            return;
        decision_target += sd->error;
        if (!isfinite(decision_target))
            return;
    }

    decision_target = clampf(decision_target, 0.0f, YM_FULL_SCALE);

    optimizer_generate_candidates(opt, decision_target);

    const YMState base = opt->current;

    /* Noise gating must be reversible; a single mixer write disables it. */
    YMState unmodulated = base;
    unmodulated.mixer = (uint8_t)(unmodulated.mixer | YM_MIXER_NOISE_BITS);
    if (unmodulated.mixer != base.mixer)
        add_candidate(opt, &unmodulated, decision_target);

    for (uint8_t ch = 0u; ch < 3u; ++ch) {
        YMState state = base;
        state.mixer = (uint8_t)(state.mixer & (uint8_t)~(1u << (ch + 3u)));
        state.noise_period = YM_DITHER_NOISE_PERIOD;
        add_candidate(opt, &state, decision_target);
    }
}


bool pcm_to_quantizer_target(
    const float pcm,
    float *const target)
{
    if (target == NULL || !isfinite(pcm))
        return false;

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
        return false;

    *target = (sample + 1.0f) * (0.5f * full_scale);

    return isfinite(*target);
}

/* Sigma-delta feedback. */

/*
 * Bounded first-order error feedback:
 *
 *   e[n+1] = clamp(e[n] + input[n] - output[n],
 *                  -full_scale, +full_scale)
 *
 * The bound is derived from the output model's full-scale range.
 * Input and output MUST use the same amplitude units.
 */
bool optimizer_update_sigma_delta(
    SigmaDeltaState *const sd,
    const float input,
    const float output)
{
    if (sd == NULL ||
        !isfinite(sd->error) ||
        !isfinite(input) ||
        !isfinite(output))
        return false;

    const float full_scale = YM_FULL_SCALE;
    const float error = sd->error + input - output;

    sd->error = clampf(error, -full_scale, full_scale);
    return isfinite(sd->error);
}

float optimizer_output_max(void)
{
    return YM_FULL_SCALE;
}

/* Select best current candidate. Used by scheduler fallback. */
int16_t optimizer_best_candidate(Optimizer *const opt)
{
    if (opt == NULL || opt->candidate_count > MAX_CANDIDATES)
        return -1;

    float best = INFINITY;
    int16_t index = -1;
    for (uint8_t i = 0; i < opt->candidate_count; ++i) {
        const float cost = opt->candidates[i].total_cost;
        if (isfinite(cost) && cost >= 0.0f && cost < best) {
            best = cost;
            index = (int16_t)i;
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
