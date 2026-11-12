/*
    player_codegen.c

    Generate a callable, straight-line 68000 YM2149 player routine.
    Each slot is padded to the configured cycle count.
*/

#include <inttypes.h>
#include <stdio.h>

#include "player_codegen.h"

#define YM_SELECT "($ffff8800).w"
#define YM_DATA   "($ffff8802).w"

/* Atari ST estimates include the PSG wait state for every write. */
#define NOP_CYCLES 4u

/* One write per padding run instead of up to 46 stdio calls per slot. */
static const char nop_padding[] =
    "\tnop\n\tnop\n\tnop\n\tnop\n"
    "\tnop\n\tnop\n\tnop\n\tnop\n"
    "\tnop\n\tnop\n\tnop\n\tnop\n"
    "\tnop\n\tnop\n\tnop\n\tnop\n"
    "\tnop\n\tnop\n\tnop\n\tnop\n"
    "\tnop\n\tnop\n\tnop\n\tnop\n"
    "\tnop\n\tnop\n\tnop\n\tnop\n"
    "\tnop\n\tnop\n\tnop\n\tnop\n"
    "\tnop\n\tnop\n\tnop\n\tnop\n"
    "\tnop\n\tnop\n\tnop\n\tnop\n"
    "\tnop\n\tnop\n\tnop\n\tnop\n"
    "\tnop\n\tnop\n";

_Static_assert((MAX_SLOT_OPERATION_CYCLES + NOP_CYCLES) % NOP_CYCLES == 0,
               "slot padding requires a whole number of NOPs");
_Static_assert(sizeof(nop_padding) - 1 >=
               (MAX_SLOT_OPERATION_CYCLES + NOP_CYCLES) / NOP_CYCLES *
                   (sizeof("\tnop\n") - 1),
               "padding text must hold the longest empty slot");

static void emit_select(FILE *const out, const uint8_t reg)
{
    (void)fprintf(out, "\tmove.b #%u,%s\n", (unsigned)reg, YM_SELECT);
}

static void emit_data(FILE *const out, const uint8_t reg, const uint8_t value)
{
    if (reg == 7) {
        /* Preserve the caller's I/O-port directions held in D1. */
        (void)fprintf(out,
            "\tmoveq #%u,d0\n\tor.b d1,d0\n\tmove.b d0,%s\n",
            (unsigned)(value & 0x3fu), YM_DATA);
    } else {
        (void)fprintf(out, "\tmove.b #%u,%s\n", (unsigned)value, YM_DATA);
    }
}

static void emit_initial_state(FILE *const out)
{
    /* Save R7's I/O directions before programming any audio mixer bits. */
    (void)fprintf(out,
        "\n; Initialize the YM registers before timed playback slots\n"
        "\tmove.b #7,%s\n\tmoveq #0,d1\n"
        "\tmove.b %s,d1\n\tandi.b #$c0,d1\n",
        YM_SELECT, YM_SELECT);
    emit_data(out, 7, YM_INITIAL_MIXER);
    emit_select(out, 6);
    emit_data(out, 6, YM_INITIAL_NOISE_PERIOD);
    emit_select(out, 8);
    emit_data(out, 8, YM_INITIAL_VOLUME);
    emit_select(out, 9);
    emit_data(out, 9, YM_INITIAL_VOLUME);
    emit_select(out, 10);
    emit_data(out, 10, YM_INITIAL_VOLUME);
}

/* Reject inconsistent metadata before emitting this slot's instructions. */
static int8_t validate_slot(const SampleSlot *const slot, const uint32_t index)
{
    if (slot->count > MAX_OPERATIONS ||
        slot->cycles > MAX_SLOT_OPERATION_CYCLES)
        return -1;

    uint8_t cycles = 0;
    uint8_t latch = slot->initial_latch;
    for (uint8_t i = 0; i < slot->count; ++i) {
        const uint8_t reg = slot->op[i].reg;
        if (reg >= REG_COUNT)
            return -1;
        if (latch != reg) {
            cycles += YM_REGISTER_ACCESS_CYCLES;
            latch = reg;
        }
        cycles = (uint8_t)(cycles + (reg == 7
            ? YM_MIXER_WRITE_CYCLES : YM_REGISTER_ACCESS_CYCLES));
    }

    if (cycles != slot->cycles || latch != slot->final_latch) {
        (void)fprintf(stderr,
                      "player_codegen: slot %" PRIu32 " disagrees with scheduler metadata\n",
                      index);
        return -1;
    }
    return 0;
}

static int8_t emit_slot(FILE *const out, const SampleSlot *const slot,
                       const uint32_t index)
{
    uint8_t latch = slot->initial_latch;

    if (fprintf(out,
                "\n;--------------------------------\n"
                "; sample slot %" PRIu32 "\n"
                "; input latch = %u\n", index, (unsigned)latch) < 0)
        return -1;

    for (uint8_t i = 0; i < slot->count; ++i) {
        const YMOperation *const op = &slot->op[i];
        if (latch != op->reg) {
            emit_select(out, op->reg);
            latch = op->reg;
        }
        emit_data(out, op->reg, op->value);
        if (ferror(out))
            return -1;
    }

    /* ST RAM bus timing and NOP padding use four-cycle steps. */
    const unsigned slot_cycles = MAX_SLOT_OPERATION_CYCLES + (index % 2u) * NOP_CYCLES;
    const size_t padding_bytes = (slot_cycles - slot->cycles) / NOP_CYCLES *
                                 (sizeof("\tnop\n") - 1);
    if (padding_bytes != 0 &&
        fwrite(nop_padding, 1, padding_bytes, out) != padding_bytes)
        return -1;

    return fprintf(out, "; estimated cycles=%u\n", slot_cycles) < 0 ? -1 : 0;
}

int8_t player_codegen_generate(FILE *const out, const ScheduledStream *const stream)
{
    if (out == NULL || ferror(out) || stream == NULL ||
        (stream->count != 0 && stream->slots == NULL))
        return -1;

    (void)fprintf(out,
        ";=================================================\n"
        "; Atari STE YM2149 generated player\n"
        "; Average %u cycles per slot (instruction timing is estimated)\n"
        "; Entry point: ym_player; returns with RTS\n"
        "; Requires supervisor mode, masked interrupts, exclusive YM/bus access\n"
        "; Clobbers D0/D1 and condition codes; mutes R8-R10 on return\n"
        ";=================================================\n\n"
        "\tsection text\n\n"
        "ym_player:\n",
        (unsigned)SAMPLE_CYCLES);

    if (ferror(out))
        return -1;
    emit_initial_state(out);
    if (ferror(out))
        return -1;

    for (uint32_t i = 0; i < stream->count; ++i) {
        const SampleSlot *const slot = &stream->slots[i];

        if (i == 0 && slot->initial_latch != YM_INITIAL_LATCH) {
            (void)fprintf(stderr,
                          "player_codegen: first slot latch does not match startup state\n");
            return -1;
        }
        if (i > 0 && slot->initial_latch != stream->slots[i - 1].final_latch) {
            (void)fprintf(stderr,
                          "player_codegen: latch discontinuity at slot %" PRIu32 "\n", i);
            return -1;
        }
        if (validate_slot(slot, i) != 0 || emit_slot(out, slot, i) != 0)
            return -1;
    }

    (void)fprintf(out,
        "\n; Silence all channels after the last complete sample slot\n"
        "\tmove.b #8,%s\n\tmove.b #0,%s\n"
        "\tmove.b #9,%s\n\tmove.b #0,%s\n"
        "\tmove.b #10,%s\n\tmove.b #0,%s\n\trts\n",
        YM_SELECT, YM_DATA, YM_SELECT, YM_DATA, YM_SELECT, YM_DATA);
    return ferror(out) ? -1 : 0;
}
