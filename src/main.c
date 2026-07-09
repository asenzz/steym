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


static int16_t *load_pcm(const char *filename, int *const samples)
{
    FILE *const f = fopen(filename, "rb");

    if(!f) {
        perror(filename);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    rewind(f);
    if(size<=0 || size%2) {
	perror("size");
        fclose(f);
        return NULL;
    }

    const uint32_t count = size/2;
    int16_t *const data = malloc(size);

    if(!data) {
        perror("data");
        fclose(f);
        return NULL;
    }

    if (fread(data, 2, count, f) != count) {
	perror("fread");
	return NULL;
    }
    fclose(f);

    *samples=count;
    return data;
}


/* Convert 16-bit PCM to normalized floating point. */
static float * convert_pcm(const int16_t *input, const int count)
{
    float *const out = malloc(sizeof(float) * count);
    if(!out) return NULL;

    for(int i=0; i<count; ++i) 
        out[i] = (float)input[i] / 32768.0f;

    return out;
}


/* Generate scheduled stream. */
static ScheduledStream * schedule_states(const YMState *states, const int count)
{
    ScheduledStream *stream = malloc(sizeof(ScheduledStream));
    if(!stream) return NULL;

    stream->slots = malloc(sizeof(SampleSlot) * count);
    stream->count=0;
    SchedulerState scheduler;
    scheduler_init(&scheduler);

    for(int i=0; i<count; ++i) {
        SampleSlot slot;
        scheduler_make_slot(&scheduler, &states[i], &slot);

        /* Keep compiler alive even if optimizer produced an impossible state. */
        if(slot.cycles>SAMPLE_CYCLES)
            fprintf(stderr, "warning: sample %d overflow %d cycles\n", i, slot.cycles);

        scheduler_append_slot(stream, &slot);
        scheduler_commit_slot(&scheduler, &slot);
    }

    return stream;
}


int main(int argc, char **argv)
{
    if(argc<3) {
        fprintf(stderr, "usage: %s input.raw output.s\n", argv[0]);
        return 1;
    }

    const char *input = argv[1];
    const char *output = argv[2];
    int count=0;
    int16_t *pcm16 = load_pcm(input, &count);

    if(!pcm16) {
        fprintf(stderr, "cannot load PCM\n");
        return 1;
    }

    printf("Loaded %d samples\n", count);
    float *const pcm = convert_pcm(pcm16, count);
    if(!pcm) {
	perror("pcm");
	return 2;
    }
    free(pcm16);

    YMState *const states = calloc(count, sizeof(YMState));
    if(!states) {
	perror("states");
        free(pcm);
        return 1;
    }

    printf("Running YM2149 optimizer...\n");
    viterbi_optimize(pcm, count, states);
    free(pcm);
    printf("Scheduling 182-cycle slots...\n");

    ScheduledStream *stream = schedule_states(states, count);
    if(!stream) {
	perror("stream");
	return 3;
    }
    free(states);

    FILE *const out = fopen(output, "w");
    if(!out) {
        perror(output);
        return 1;
    }

    printf("Generating Devpac source...\n");
    player_codegen_generate(out, stream);
    fclose(out);
    free(stream->slots);
    free(stream);
    printf("Done: %s\n", output);

    return 0;
}
