;========================================================
; Atari STE YM2149 playback wrapper
;
; The generated source defines ym_player as a normal 68000
; subroutine that plays all slots in sequence and returns with RTS.
; Generate the source as generated_player.s in the assembler's
; include path, assemble both files, then call player_play.
;========================================================

        section text
        xdef    player_play

player_play:
        jsr     ym_player
        rts

        include "generated_player.s"
