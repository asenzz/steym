/*
    devpac.c

    Atari STE YM2149 backend

    Converts optimized YMState[]
    into Devpac 68000 assembly.

*/


#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "devpac.h"
#include "optimizer.h"


#define YM_PORT_SELECT "$ff8800"
#define YM_PORT_DATA   "$ff8802"


/* YM registers used by the player.
       R6  noise period
       R7  mixer
       R8  volume A
       R9  volume B
       R10 volume C
*/


#define YM_REGISTER_COUNT 16


typedef struct
{
    uint8_t value[YM_REGISTER_COUNT];

    /* Current YM register latch.
       -1 means unknown. */

    int selected_register;
} YMShadow;


/* Register update order.
   The order is chosen to:
      1. avoid unnecessary latch movement
      2. update mixer before volumes
      3. keep DAC writes grouped
*/
static const uint8_t register_order[] = {7, 6, 8, 9, 10 };


/* Convert optimized state into YM register values. */
static void state_to_registers(const YMState *state, uint8_t reg[YM_REGISTER_COUNT])
{
    memset(reg, 0, YM_REGISTER_COUNT);
    reg[6] = state->noise_period;
    reg[7] = state->mixer;
    reg[8] = state->volume[0];
    reg[9] = state->volume[1];
    reg[10] = state->volume[2];
}


/* Emit YM register select. */
static void emit_select(FILE *const out, const int reg)
{
    fprintf(out, "\tmove.b #%d,%s\n", reg, YM_PORT_SELECT);
}


/* Emit YM data write. */
static void emit_write(FILE *const out, const uint8_t value)
{
    fprintf(out, "\tmove.b #%u,%s\n", value, YM_PORT_DATA);
}


/* Emit one register modification. Returns estimated 68000 cycles. */
static int emit_register_change(FILE *const out, YMShadow *const shadow, const int reg, const uint8_t value)
{
    /* Nothing changed. */
    if(shadow->value[reg] == value) return 0;

    int cycles=0;
    /* Reuse YM latch. */
    if(shadow->selected_register != reg) {
        emit_select(out, reg);
        shadow->selected_register = reg;

        /* Approximate: move.b immediate -> YM address */
        cycles += 16;
    }

    emit_write(out, value);
    /* Approximate: move.b immediate -> YM data */

    cycles += 16;
    shadow->value[reg]= value;

    return cycles;
}


/* Emit one audio frame. */
static int emit_frame(FILE *const out, YMShadow *const shadow, const YMState *state)
{
    uint8_t reg[YM_REGISTER_COUNT];
    state_to_registers(state, reg);
    int cycles=0;
    fprintf(out, "\n; YM frame\n");
    for(unsigned i=0; i<sizeof(register_order); ++i) {
        int r = register_order[i];
        cycles += emit_register_change(out, shadow, r, reg[r]);
    }
    fprintf(out, "; estimated cycles %d\n", cycles);
    return cycles;
}


/* Generate complete Devpac player. */
void devpac_generate(FILE *const out, const YMState *states, const int count)
{
    YMShadow shadow;
    memset(&shadow, 0, sizeof(shadow));
    for(int i=0; i<YM_REGISTER_COUNT; ++i) 
        /* Force first write. */
        shadow.value[i]=0xff;

    shadow.selected_register=-1;
    fprintf(out, ";-------------------------------------------------\n");
    fprintf(out, "; Atari STE YM2149 generated player\n");
    fprintf(out, ";-------------------------------------------------\n\n");

    fprintf(out, "\tsection text\n\n");

    fprintf(out, "ym_player:\n");

    for(int i=0; i<count; ++i) {
        fprintf(out, "\n; sample %d\n", i);
        emit_frame(out, &shadow, &states[i]);
    }

    fprintf(out, "\n\trts\n");
}

