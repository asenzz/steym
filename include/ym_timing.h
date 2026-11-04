#ifndef STE_YM_TIMING_H
#define STE_YM_TIMING_H

/* Immediate byte store to absolute-short PSG address: 16 + 4 wait. */
#define YM_REGISTER_ACCESS_CYCLES 20u

/* MOVEQ + OR.B + data-register store: 4 + 4 + 12 + 4 wait. */
#define YM_MIXER_WRITE_CYCLES 24u

/* Alternate 180/184-cycle slots for a 182-cycle average. */
#define SAMPLE_CYCLES 182u
#define MAX_SLOT_OPERATION_CYCLES (SAMPLE_CYCLES - 2u)

#endif
