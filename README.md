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

The player now uses nominal 184-cycle slots. At an assumed 8 MHz CPU clock this
is approximately 43,478 samples/second. Raw PCM has no sample-rate metadata, so
prepare input at the intended playback rate. The generator forces absolute-short
PSG addresses and models their wait states; slots use NOP-only padding to avoid
the former DBRA/RAM alignment problem. These are timing estimates that still
require assembly and verification in an Atari STE emulator and on hardware.

The optimizer keeps its local volume refinement and adds DAC anchors across the
full range, so full/zero transitions need not ramp by two volume steps per slot.
Noise gating can be disabled again. The YM amplitude curve and noise model are
approximations; they do not simulate the actual noise waveform, analog mixing,
or the intermediate levels while several registers are written within a slot.

Memory limitations remain significant. Compact backtracking stores only states,
byte-sized predecessors and a byte-sized beam count. On the tested x86-64 ABI,
this is 836 bytes/sample instead of the former 2,820, about 34.7 MiB per second
at 43,478 Hz, before other buffers. The tested GCC m68k ABI uses 706 bytes/sample;
actual sizes depend on the toolchain ABI. Thus normal clips still
do not fit when running the compiler on a 4 MiB STE; checkpoint/replay or another
bounded-memory search design is needed. Software floating point and the beam
search also make this an offline compiler, with no real-time conversion claim.

The player is unrolled: an empty 184-cycle slot occupies 92 bytes of 68000 code,
about 3.8 MiB/second at the assumed rate, before startup, cleanup and the host
program. A compact data stream with a looping player requires a separate design.
