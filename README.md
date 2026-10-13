Steym by Žarko ASEN 2026

`steymc` converts signed 16-bit mono little-endian raw PCM into Devpac-compatible
68000 assembly for the Atari STE YM2149. The generated source defines a callable
`ym_player` routine. Generate it as `generated_player.s`, assemble it together
with `runtime/player.s`, and call `player_play` from an Atari program.

The player uses nominal 182-CPU-cycle slots. At an 8 MHz CPU clock this is
approximately 43,956 samples/second, not 44,076. Instruction and I/O timing
estimates must still be checked against the intended assembler and Atari STE
emulator/hardware.

Build on x86-64 Linux with:

```sh
make
```

Build a native m68k executable when the Atari cross/native toolchain is installed:

```sh
make clean
make CC=m68k-atari-mintelf-gcc
```

The output is generated assembly, not a complete Atari executable. Assemble and
link the generated source with the playback wrapper separately.

**Memory limitations:** the correct full-beam backtracking implementation
retains one complete beam per PCM sample. On the tested x86-64 build,
`sizeof(ViterbiBeam)` is 2,820 bytes, or about 118 MiB per second of 43,956 Hz
audio, before PCM, output states, and scheduled slots. Therefore 4 MiB is not
enough for normal-length clips when running the compiler on an STe. The
full-history implementation is retained for correctness; checkpoint/replay or
another memory-reduction design is required for long clips on the Atari.

The generated player is also unrolled: every sample has its own padded code
slot. A slot containing no register writes is about 92 bytes of 68000 machine
code, so a 43,956-sample second is on the order of 3.9 MiB before the surrounding
program and operating-system memory. Verify the assembled executable size
against the available Atari memory. A compact data stream and looping player
would require a separate playback design.
