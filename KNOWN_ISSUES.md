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

## Framework patch carried locally

`patches/snesrecomp-hdma-line0.patch` (HDMA one line late in the frame driver)
must be applied to the `snesrecomp` submodule after a fresh clone or a
submodule update. See `patches/README.md`.

## Coverage

Compiled code is seeded from one coverage capture (attract mode, menus, part
of a game). Code no capture has reached runs on the interpreter, which is
correct but slower. Add captures to
`recomp/coverage/` (snesrecomp/docs/COVERAGE_FEEDBACK.md) as more of the game
is played, and run `tools/regen.sh` after each.
