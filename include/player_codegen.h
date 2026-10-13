#ifndef STE_YM_PLAYER_CODEGEN_H
#define STE_YM_PLAYER_CODEGEN_H


#include <stdio.h>
#include "scheduler.h"


/* Returns 0 on success and -1 if the stream is invalid or output fails. */
int player_codegen_generate(FILE *out, const ScheduledStream *stream);


#endif
