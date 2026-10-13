/*
    devpac.c

    Compatibility wrapper for the original Devpac backend API.
    The command-line compiler uses the same scheduler and code generator
    directly; keep this entry point consistent with that implementation.
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "devpac.h"
#include "player_codegen.h"

void devpac_generate(FILE *const out, const YMState *const states, const int count)
{
    if (out == NULL || states == NULL || count <= 0) {
        (void)fprintf(stderr, "devpac_generate: invalid output, states, or count\n");
        return;
    }

    const size_t sample_count = (size_t)count;
    if (sample_count > SIZE_MAX / sizeof(SampleSlot)) {
        (void)fprintf(stderr, "devpac_generate: sample count is too large\n");
        return;
    }

    ScheduledStream stream = {0};
    stream.slots = malloc(sample_count * sizeof(*stream.slots));
    if (stream.slots == NULL) {
        (void)fprintf(stderr, "devpac_generate: allocation failed\n");
        return;
    }

    SchedulerState scheduler;
    scheduler_init(&scheduler);

    for (size_t i = 0; i < sample_count; ++i) {
        SampleSlot slot;
        const uint16_t cycles = scheduler_make_slot(&scheduler, &states[i], &slot);

        if (cycles > MAX_SLOT_OPERATION_CYCLES) {
            (void)fprintf(stderr,
                          "devpac_generate: sample %zu exceeds the cycle budget\n", i);
            free(stream.slots);
            return;
        }

        scheduler_append_slot(&stream, &slot);
        scheduler_commit_slot(&scheduler, &slot);
    }

    if (player_codegen_generate(out, &stream) != 0)
        (void)fprintf(stderr, "devpac_generate: code generation failed\n");

    free(stream.slots);
}
