/*
    main.c

    Atari STE YM2149 PCM compiler

    Usage:
        steymc input.raw output.s

    Input:
        signed 16-bit mono PCM

    Output:
        Devpac 68000 assembly
*/


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "optimizer.h"
#include "viterbi.h"
#include "scheduler.h"
#include "player_codegen.h"


static int16_t *load_pcm(const char *filename, size_t *const samples)
{
    if (filename == NULL || samples == NULL)
        return NULL;

    FILE *const f = fopen(filename, "rb");
    if (!f) {
        perror(filename);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        perror("load_pcm:fseek");
        fclose(f);
        return NULL;
    }
    const long size = ftell(f);
    if (size <= 0 || size % 2 != 0 || fseek(f, 0, SEEK_SET) != 0) {
	    fprintf(stderr, "load_pcm: invalid input size or seek failure\n");
        fclose(f);
        return NULL;
    }

    const size_t byte_count = (size_t)size;
    const size_t count = byte_count / 2;
    if (count > SIZE_MAX / sizeof(int16_t)) {
        fprintf(stderr, "load_pcm: input is too large\n");
        fclose(f);
        return NULL;
    }

    int16_t *const data = malloc(count * sizeof(*data));
    if (!data) {
        perror("load_pcm:malloc");
        fclose(f);
        return NULL;
    }

    /* The input format is signed 16-bit little-endian PCM on every host. */
    if (fread(data, 1, byte_count, f) != byte_count) {
	    fprintf(stderr, "load_pcm: short read from '%s'\n", filename);
        free(data);
        fclose(f);
	    return NULL;
    }
    fclose(f);

    const unsigned char *const bytes = (const unsigned char *)data;
    for (size_t i = 0; i < count; ++i) {
        const uint16_t bits = (uint16_t)bytes[2 * i] |
                              (uint16_t)((uint16_t)bytes[2 * i + 1] << 8);
        const int32_t signed_value = (bits & UINT16_C(0x8000))
            ? (int32_t)bits - INT32_C(65536)
            : (int32_t)bits;
        data[i] = (int16_t)signed_value;
    }

    *samples = count;
    return data;
}


/* Convert 16-bit PCM to normalized floating point. */
static float *convert_pcm(const int16_t *const input, const size_t count)
{
    if (input == NULL || count == 0 || count > SIZE_MAX / sizeof(float))
        return NULL;

    float *const out = malloc(sizeof(float) * count);
    if (!out) return NULL;
    for (size_t i = 0; i < count; ++i) out[i] = (float)input[i] / 32768.f;
    return out;
}


/* Generate scheduled stream. */
static ScheduledStream *schedule_states(const YMState *states, const size_t count)
{
    if (states == NULL || count == 0 || count > UINT32_MAX ||
        count > SIZE_MAX / sizeof(SampleSlot)) {
        fprintf(stderr, "schedule_states: invalid or too large sample count\n");
        return NULL;
    }

    ScheduledStream *stream = malloc(sizeof(ScheduledStream));
    if (!stream) {
        fprintf(stderr, "malloc failed allocating stream of %zu bytes\n", sizeof(ScheduledStream));
        return NULL;
    }

    stream->slots = malloc(sizeof(SampleSlot) * count);
    if (!stream->slots) {
        fprintf(stderr, "malloc failed allocating slots of %zu bytes\n", sizeof(SampleSlot) * count);
        free(stream);
        return NULL;
    }
    stream->count=0;
    SchedulerState scheduler;
    scheduler_init(&scheduler);

    for (size_t i = 0; i < count; ++i) {
        /*
         * Current Viterbi API exposes only one state per sample.
         * Wrap it as a single candidate so it passes through the
         * same scheduler-feedback and validation interface.
         */
        const Candidate candidate = { .state = states[i], .audio_error = 0.f, .cycle_cost = 0, .total_cost = 0.f };

        YMState chosen;
        SampleSlot slot;
        const uint16_t cycles = scheduler_make_slot_feedback(
            &scheduler, &candidate, 1, &chosen, &slot);

        /*
         * Reject infeasible slots. Do not append them and do not
         * commit their register changes to the scheduler shadow.
         */
        if (cycles > MAX_SLOT_OPERATION_CYCLES) {
            fprintf(stderr,
                    "error: no cycle-feasible slot at sample %zu; operation limit is %u cycles\n",
                    i, (unsigned)MAX_SLOT_OPERATION_CYCLES);
            free(stream->slots);
            free(stream);
            return NULL;
        }

        scheduler_append_slot(stream, &slot);
        scheduler_commit_slot(&scheduler, &slot);
    }

    return stream;
}


int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s input.raw output.s\n", argv[0]);
        return 1;
    }

    const char *input = argv[1];
    const char *output = argv[2];
    size_t count = 0;
    int16_t *pcm16 = load_pcm(input, &count);
    if (pcm16 == NULL) {
        fprintf(stderr, "cannot load PCM\n");
        return 1;
    }

    printf("Loaded %zu samples\n", count);
    float *const pcm = convert_pcm(pcm16, count);
    if (pcm == NULL) {
	    perror("pcm");
	    free(pcm16);
	    return 2;
    }
    free(pcm16);

    if (count > SIZE_MAX / sizeof(YMState)) {
        fprintf(stderr, "YM state allocation size overflows size_t\n");
        free(pcm);
        return 1;
    }

    YMState *const states = calloc(count, sizeof(YMState));
    if (states == NULL) {
        perror("states");
        free(pcm);
        return 1;
    }

    printf("Running YM2149 optimizer...\n");
    if (viterbi_optimize(pcm, count, states) != 0) {
        free(states);
        free(pcm);
        fprintf(stderr, "Viterbi optimization failed\n");
        return 2;
    }
    free(pcm);
    printf("Scheduling 182-cycle slots...\n");

    ScheduledStream *stream = schedule_states(states, count);
    free(states);
    if(!stream) {
	fprintf(stderr, "Unable to schedule PCM within the cycle budget\n");
	return 3;
    }

    FILE *const out = fopen(output, "w");
    if(!out) {
        perror(output);
        free(stream->slots);
        free(stream);
        return 1;
    }

    printf("Generating Devpac source...\n");
    const int generation_error = player_codegen_generate(out, stream);
    const int output_error = ferror(out);
    const int close_error = fclose(out);
    if (generation_error != 0 || output_error || close_error != 0) {
        perror(output);
        free(stream->slots);
        free(stream);
        return 4;
    }
    free(stream->slots);
    free(stream);
    printf("Done: %s\n", output);

    return 0;
}
