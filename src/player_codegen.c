/*
    player_codegen.c

    Atari STE YM2149 straight-line
    68000 player generator.

    Input:
        ScheduledStream

    Output:
        Devpac assembly


    The generated code assumes:

        Timer B interrupt
        fixed 182 cycles/sample

*/


#include <stdio.h>
#include "player_codegen.h"


#define YM_SELECT "$ff8800"
#define YM_DATA   "$ff8802"

#define SAMPLE_CYCLES 182

/* Approximate cycle costs.
   Used for validation only. */

#define MOVE_BYTE_CYCLES 16
#define NOP_CYCLES        4


/* Emit YM register select. */
static void emit_select(FILE *const out, const uint8_t reg)
{
    fprintf(out, "\tmove.b #%u,%s\n", reg, YM_SELECT);
}



/* Emit YM data write. */
static void emit_data(FILE *const out, const uint8_t value)
{
    fprintf(out, "\tmove.b #%u,%s\n", value, YM_DATA);
}



/* Emit one sample slot. The scheduler already decided the operation ordering. */
static int emit_slot(FILE *const out, const SampleSlot *const slot)
{
    int cycles = 0;
    int latch = slot->initial_latch;

    fprintf(out, "\n;--------------------------------\n");
    fprintf(out, "; sample slot\n");
    fprintf(out, "; input latch = %d\n", latch);

    for(int i=0; i<slot->count; ++i) {
        const YMOperation *op = &slot->op[i];

        /* Only select register if the YM latch differs. */
        if(latch != op->reg) {
            emit_select(out, op->reg);
            cycles += MOVE_BYTE_CYCLES;
            latch = op->reg;
        }

        emit_data(out, op->value);
        cycles += MOVE_BYTE_CYCLES;
    }

    /* Pad to exactly 182 cycles. The scheduler should already guarantee this. */
    while(cycles < SAMPLE_CYCLES) {
        fprintf(out, "\tnop\n");
        cycles += NOP_CYCLES;
    }

    fprintf(out, "; cycles=%d\n", cycles);
    if(cycles > SAMPLE_CYCLES) fprintf(out, "; !!! OVERFLOW !!!\n");

    return cycles;
}

/* Generate complete player. */
void player_codegen_generate(FILE *const out, const ScheduledStream *const stream)
{
    fprintf(out, ";=================================================\n");
    fprintf(out, "; Atari STE YM2149 generated player\n");
    fprintf(out, "; Fixed 182 cycle sample slots\n");
    fprintf(out, ";=================================================\n\n");
    fprintf(out, "\tsection text\n\n");
    fprintf(out, "ym_timer_b_irq:\n");

    for(int i=0; i<stream->count; ++i) (void) emit_slot(out, &stream->slots[i]);

    fprintf(out, "\trte\n");
}
