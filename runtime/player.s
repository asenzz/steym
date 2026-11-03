;========================================================
; Atari STE YM2149 playback wrapper
;
; The generated source defines ym_player as a normal 68000
; subroutine that plays all slots in sequence and returns with RTS.
; Generate the source as generated_player.s in the assembler's
; include path, assemble this wrapper, then call player_play.
;
; Caller must already be in supervisor mode (for example via
; XBIOS Supexec). This synchronous routine owns the CPU/YM for
; the whole clip: maskable interrupts remain disabled throughout
; initialization, playback, and final mute. Do not call TOS or
; use concurrent YM access, DMA bus masters, or the blitter.
; Long clips also delay normal OS interrupt service.
; Clobbers D0/D1; restores the incoming SR.
;========================================================

        section text
        xdef    player_play

player_play:
        move.w  sr,-(sp)
        ori.w   #$0700,sr
        jsr     ym_player
        move.w  (sp)+,sr
        rts

        include "generated_player.s"
