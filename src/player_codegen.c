/*
    player_codegen.c

    Generate a callable, straight-line 68000 YM2149 player routine.
    Each slot is padded to the configured cycle count.
*/

#include <stddef.h>
#include <stdio.h>

#include "player_codegen.h"

#define YM_SELECT "$ff8800"
#define YM_DATA   "$ff8802"

/* These are estimates for the emitted 68000 memory-write instructions. */
#define MOVE_BYTE_CYCLES 20
#define NOP_CYCLES        4
#define DELAY_CYCLES SLOT_PADDING_CYCLES /* moveq #1,d0 (4) + taken dbra (10) */

static void emit_select(FILE *const out, const uint8_t reg)
{
    (void)fprintf(out, "\tmove.b #%u,%s\n", (unsigned)reg, YM_SELECT);
}

static void emit_data(FILE *const out, const uint8_t value)
{
    (void)fprintf(out, "\tmove.b #%u,%s\n", (unsigned)value, YM_DATA);
}

static void emit_initial_state(FILE *const out)
{
    /* Keep these values synchronized with scheduler_init() in scheduler.c. */
    (void)fprintf(out,
        "\n; Initialize the YM registers before timed playback slots\n"
        "\tmove.b #7,%s\n\tmove.b #%u,%s\n"
        "\tmove.b #6,%s\n\tmove.b #%u,%s\n"
        "\tmove.b #8,%s\n\tmove.b #%u,%s\n"
        "\tmove.b #9,%s\n\tmove.b #%u,%s\n"
        "\tmove.b #10,%s\n\tmove.b #%u,%s\n",
        YM_SELECT, (unsigned)YM_INITIAL_MIXER, YM_DATA,
        YM_SELECT, (unsigned)YM_INITIAL_NOISE_PERIOD, YM_DATA,
        YM_SELECT, (unsigned)YM_INITIAL_VOLUME, YM_DATA,
        YM_SELECT, (unsigned)YM_INITIAL_VOLUME, YM_DATA,
        YM_SELECT, (unsigned)YM_INITIAL_VOLUME, YM_DATA);
}

static int emit_slot(FILE *const out, const SampleSlot *const slot,
                     const size_t index)
{
    int cycles = 0;
    int latch = slot->initial_latch;

    (void)fprintf(out, "\n;--------------------------------\n");
    (void)fprintf(out, "; sample slot %zu\n", index);
    (void)fprintf(out, "; input latch = %d\n", latch);

    for (uint8_t i = 0; i < slot->count; ++i) {
        const YMOperation *const op = &slot->op[i];
        if (op->reg >= REG_COUNT)
            return -1;

        if (latch != op->reg) {
            emit_select(out, op->reg);
            cycles += MOVE_BYTE_CYCLES;
            latch = op->reg;
        }

        emit_data(out, op->value);
        cycles += MOVE_BYTE_CYCLES;
    }

    if (cycles != slot->cycles || latch != slot->final_latch) {
        (void)fprintf(stderr,
                      "player_codegen: slot %zu disagrees with scheduler metadata\n",
                      index);
        return -1;
    }

    /* The scheduler guarantees room for the 14-cycle delay plus NOP padding. */
    if (cycles + DELAY_CYCLES > SAMPLE_CYCLES)
        return -1;

    /* D0 is caller-saved. The taken DBRA skips the NOP and costs 10 cycles. */
    (void)fprintf(out, "\tmoveq #1,d0\n");
    (void)fprintf(out, "\tdbra d0,steYm_pad_end_%zu\n", index);
    (void)fprintf(out, "\tnop\n");
    (void)fprintf(out, "steYm_pad_end_%zu:\n", index);
    cycles += DELAY_CYCLES;

    while (cycles + NOP_CYCLES <= SAMPLE_CYCLES) {
        (void)fprintf(out, "\tnop\n");
        cycles += NOP_CYCLES;
    }

    if (cycles != SAMPLE_CYCLES) {
        (void)fprintf(stderr, "player_codegen: slot %zu cannot be padded to %u cycles\n",
                      index, (unsigned)SAMPLE_CYCLES);
        return -1;
    }

    (void)fprintf(out, "; estimated cycles=%d\n", cycles);
    return ferror(out) ? -1 : 0;
}

int player_codegen_generate(FILE *const out, const ScheduledStream *const stream)
{
    if (out == NULL || stream == NULL ||
        (stream->count != 0 && stream->slots == NULL))
        return -1;

    (void)fprintf(out,
        ";=================================================\n"
        "; Atari STE YM2149 generated player\n"
        "; Nominal %u-cycle slots (instruction timing is estimated)\n"
        "; Entry point: ym_player; returns with RTS\n"
        ";=================================================\n\n"
        "\tsection text\n\n"
        "ym_player:\n",
        (unsigned)SAMPLE_CYCLES);

    emit_initial_state(out);

    for (size_t i = 0; i < stream->count; ++i) {
        const SampleSlot *const slot = &stream->slots[i];

        if (slot->count > MAX_OPERATIONS ||
            slot->cycles > MAX_SLOT_OPERATION_CYCLES)
            return -1;
        if (i == 0 && slot->initial_latch != YM_INITIAL_LATCH) {
            (void)fprintf(stderr,
                          "player_codegen: first slot latch does not match startup state\n");
            return -1;
        }
        if (i > 0 && slot->initial_latch != stream->slots[i - 1].final_latch) {
            (void)fprintf(stderr,
                          "player_codegen: latch discontinuity at slot %zu\n", i);
            return -1;
        }
        if (emit_slot(out, slot, i) != 0)
            return -1;
    }

    (void)fprintf(out, "\n\trts\n");
    return ferror(out) ? -1 : 0;
}
