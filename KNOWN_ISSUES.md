# Known issues

## Audio hitch when clicking the title bar

Clicking back into the window by its title bar makes the music hitch for a
moment. Clicking inside the picture does not.

Pressing on the title bar starts Windows' own move/size loop, and the game's
main loop does not run until the button is released. Audio is produced by the
game loop (the sound CPU advances with guest frames), so for that time no
audio is produced and the output underflows: about 100 ms, roughly 3,200
samples, per click in the audio stats. Switching focus without a title-bar
click (`SetForegroundWindow`) caused no stall and no underflow.

This is in the framework's desktop host, not this game. A fix would keep the
frame loop running during the modal loop, or produce audio off the main
thread.

## VSync should be "adaptive"

With `VSync = 1` (the framework's default for a new `config.ini`) the music
skips and briefly slows down during play. The game loop waits on the display
(about 13 ms per frame, measured 59.97 fps against the SNES's 60.10). When a
refresh is missed the wait doubles, and the pacing clock drops that time
instead of catching up, so no audio is produced for it. With `VSync = 0` the
music is clean but the court tears.

Set this in `config.ini` under `[Graphics]`, or with the launcher's VSync
control:

    VSync = adaptive

The display is then synced normally, and only a frame that misses a refresh
is shown immediately. Play-checked: no tearing, no music skips. The default
lives in the framework (`snesrecomp/runner/src/desktop/mmx_config.c` and the
default `config.ini` text in `host_main.c`). Changing it there would help every
port.

## Sylvester's character-select screen: teal line at the bottom

Not a port bug. The line also appears in bsnes, so it is in the original game.

## Trace builds need `-include errno.h` on MinGW

`snesrecomp/runner/src/debug_server.c` uses `EIO` and `ENODATA` without
including `<errno.h>`, so `-DSNESRECOMP_ENABLE_TRACE=ON` fails to compile with
the MinGW clang toolchain. Normal builds are unaffected. For a diagnostic
build:

    cmake -S . -B build-trace -DSNESRECOMP_ENABLE_TRACE=ON -DCMAKE_C_FLAGS="-include errno.h"

The fix belongs in the framework.

## Framework fixes not upstream yet

The `snesrecomp` submodule comes from a fork branch
(`SlippyGetBackHere/snesrecomp`, `local/looney-tunes-b-ball`) carrying four
fixes on top of upstream, also kept as patches in `patches/`: `snesrecomp-hdma-line0.patch` (HDMA
applied one line late in the frame driver),
`snesrecomp-generate-coverage-dir.patch` (`generate --coverage-dir`, so
captures from different builds combine),
`snesrecomp-nested-deadline-yield.patch` (an interpreted callee of compiled
code stops at the frame deadline) and `snesrecomp-anchor-band-2bpp.patch`
(the widescreen anchor band also moves 2bpp layers, for the HUD text). See
`patches/README.md`.

## Coverage, and gameplay running interpreted

Compiled code is seeded from three coverage captures in `recomp/coverage/`:
attract mode and the menus, a first tournament match, and a full one-player
tournament (played with the "Team 1 always wins" testing mod). Add captures
as more of the game is played (snesrecomp/docs/COVERAGE_FEEDBACK.md), then
run `tools/regen.sh`.

Much of gameplay's per-frame work still runs on the interpreter, which is
correct but not native. The main loop at `$80:9D60` calls each subsystem
with `JSL`, and the analyzer leaves 147 variants interpreter-only
(`src/gen/program_manifest.json`, disposition `lle_only`), mostly because it
cannot prove how a callee exits (`unproven_callee_exit`). One unproven
callee makes every caller interpreter-only. The portrait decompressor's exits
are declared in `recomp/bank83.cfg` (traced from its code), which compiled
its callers; the remaining roots (for example `$81:B948` under `$81:A8FC`,
`$80:AC32`, `$00:B696`, `$82:DF33`, `$81:8019`) each need the same kind of
evidence before a declaration. Frames take about 2-4 ms, so this is not a
speed problem today.
