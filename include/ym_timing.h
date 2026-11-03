#ifndef STE_YM_TIMING_H
#define STE_YM_TIMING_H

/* Immediate byte store to absolute-short PSG address: 16 + 4 wait. */
#define YM_REGISTER_ACCESS_CYCLES 20u

/* MOVEQ + OR.B + data-register store: 4 + 4 + 12 + 4 wait. */
#define YM_MIXER_WRITE_CYCLES 24u

/* Code in Atari ST RAM uses 4-cycle bus alignment; pad only with NOPs. */
#define SAMPLE_CYCLES 184u
#define SLOT_PADDING_CYCLES 0u
#define MAX_SLOT_OPERATION_CYCLES (SAMPLE_CYCLES - SLOT_PADDING_CYCLES)

#endif
