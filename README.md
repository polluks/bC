# bC — batari Basic to C transpiler

bC reads [batari Basic](https://github.com/batari-Basic/batari-Basic) (bB) source
and emits C89 code for [cc65](https://cc65.github.io/)'s **atari2600** target,
letting you build Atari 2600 ROMs with a real optimizing C toolchain.

Like bB itself, the transpiler front-end is built with lex/yacc (flex + bison).

## Pipeline

    game.bas ──bc──▶ game.c ──cl65 -t atari2600──▶ game.bin (4K)

The generated file is self-contained: it includes `runtime/bc_kernel.c`, a
simple display kernel (playfield + 2 players, NTSC/PAL timing) plus runtime
helpers. Unused helpers are compiled out automatically.

## Requirements

- flex, bison, a C compiler (host build)
- cc65 with the `atari2600` target (V2.19 known to work)
- GNU make

## Build & run

    make            # builds ./bc and examples/demo.bas -> demo.bin
    ./bc -o out.c mygame.bas
    cl65 -t atari2600 -Os -Iruntime -c -o out.o out.c
    cl65 -t atari2600 -C /usr/local/share/cc65/cfg/atari2600.cfg -o out.bin out.o

or just:

    make demo.bin   # end-to-end: examples/demo.bas -> demo.bin

Emulate `demo.bin` in your favourite 2600 emulator (Stella, etc.).

## Supported language subset

- variables `a`–`z`, `temp1`–`temp7`, `dim x = var` / `dim x = $80`
- `const name = expr` (compile-time folded)
- arithmetic/bitwise expressions with bB precedence, 8-bit wrap semantics
- comparisons `= == <> != < > <= >=`, boolean `&& || !`, bit read `x{3}`
- data tables: `data`, `sdata` (+ `sread`)
- blocks: `playerN:`, `playfield:` (32-wide, mirrored), `pfcolors:`,
  `bkcolors:`, `asm ... end`
- playfield commands: `pfpixel/pfhline/pfvline/pfscroll/pfclear`,
  `pfread(x,y)`
- control flow: labels, `goto/gosub/return/pop` (gosub via dispatch
  trampoline, depth 8), `on..goto/on..gosub`, `if..then..else`,
  multi-statement branches after `then`
- loops: `for/to/step/next`, `while/wend`, `do/loop [while|until]`, `exit`
- system names: TIA/RIOT registers (`COLUP0`, `SWCHA`, …), `score`
  (`+ - =` as decimal helpers), `rand`, `rand16`, `collision(a,b)`
  (all standard pairs), joystick macros `joy0left` etc.
- `drawscreen`, `reboot`, `set tv ntsc|pal`, `set romsize 4k`

## Limitations / deviations

- 4K ROMs only; no bank switching, no multisprite/DPC+ kernels
- score digits are maintained in RAM but not drawn on screen
- player positioning uses a coarse strobe loop (~3 px resolution)
- sprites are 8×16 double-height, drawn from RAM buffers copied once at init
- `lives:` block not implemented

## Layout

    src/bc.l        lexer (flex)
    src/bc.y        grammar (bison)
    src/codegen.c   AST -> C emission, symbol tables
    runtime/        bc_runtime.h + bc_kernel.c (included by generated code)
    examples/       demo.bas

## Status

Experimental. The kernel is intentionally minimal; treat it as a starting
point for cc65-based 2600 homebrew rather than a drop-in bB replacement.
