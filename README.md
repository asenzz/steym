Steym by Žarko ASEN 2026

`steymc` converts signed 16-bit mono little-endian raw PCM into Devpac-compatible
68000 assembly for the Atari STE YM2149. The compiler can be built for x86-64
or m68k; its generated player always targets a plain Motorola 68000.

Build on x86-64 Linux with:

```sh
make
```

Build with a GCC 13.2 Atari cross toolchain, when installed:

```sh
make CC=m68k-atari-mintelf-gcc CFLAGS="-Os -Wall -Wextra" CPPFLAGS= LDFLAGS= LDLIBS=-lm
```

The Makefile detects an m68k compiler and supplies `-m68000 -msoft-float` during
compilation and linking. It tracks compiler/options and included headers, so
changing between native and cross builds rebuilds the objects. Override
`ARCH_FLAGS` only when intentionally choosing another target configuration.
Keep the toolchain's normal `int`/library ABI: use the explicit narrow internal
types instead of globally adding `-mshort`. The compiler and libraries must agree
on their ABI and have compatible 68000 software-float runtime libraries.

The output is assembly, not a complete Atari executable. Generate it as
`generated_player.s` in the assembler's include path and assemble
`runtime/player.s`, which includes it. Do not assemble and link a second copy
of the generated file. The wrapper exports `player_play`.

Playback requires supervisor mode (for example, enter through XBIOS `Supexec`).
The synchronous wrapper saves SR, masks interrupts for initialization, the whole
clip and final mute, then restores SR. The caller must give it exclusive use of
the YM and CPU bus; stop competing DMA bus masters/blitter first. Long clips
hold off normal OS interrupt service. D0/D1 are clobbered; the wrapper restores the
incoming SR, while raw `ym_player` clobbers condition codes. Mixer R7's I/O-port
directions are preserved, and R8-R10 are silenced on return.
Calling `ym_player` directly requires the same supervisor/interrupt/ownership
conditions to be established by the caller.

The player alternates 180- and 184-cycle slots for an average of 182 CPU
cycles per sample, or about 44,073 Hz at the nominal PAL STE clock of
8,021,247 Hz. Raw PCM has no rate metadata. Use 44,073 Hz input to match
playback, or 44,100 Hz input for about 0.06% slower playback. The generator
uses absolute-short PSG addresses, models their wait states, and pads slots
with NOPs. Actual timing still needs emulator or hardware verification.

The optimizer keeps its local volume refinement and adds DAC anchors across the
full range, so full/zero transitions need not ramp by two volume steps per slot.
Noise gating can be disabled again. The YM amplitude curve and noise model are
approximations; they do not simulate the actual noise waveform, analog mixing,
or the intermediate levels while several registers are written within a slot.

Memory limitations remain significant. Compact backtracking stores six register
and latch bytes plus one byte-sized predecessor per beam node, and a byte-sized
beam count: 449 bytes/sample on the tested native and m68k ABIs, about 18.9 MiB
per second at 44,073 Hz, before other buffers. Amplitudes are reconstructed only
for the chosen path with the same output model used during search. Normal clips
still do not fit when running the compiler on a 4 MiB STE; checkpoint/replay or another
bounded-memory search design is needed. Software floating point and the beam
search also make this an offline compiler, with no real-time conversion claim.

The player is unrolled: an empty slot occupies 90 or 92 bytes of 68000 code,
about 3.8 MiB/second at the assumed rate, before startup, cleanup and the host
program. A compact data stream with a looping player requires a separate design.
