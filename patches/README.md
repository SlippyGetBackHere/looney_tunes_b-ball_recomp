# Local framework patches

The `snesrecomp` submodule is pinned to upstream
`a6be9756285f26ccc74539c811d4d23d57d8c33d` (see `framework_pins.txt`). The
patches below are applied to its working tree; none has been pushed or
offered upstream yet.

## snesrecomp-hdma-line0.patch

`snes_beam_frame_driver_draw_ppu_frame_observed()` primed line 1 with each
HDMA channel's first transfer and then restored the channel, so the first
real `dma_doHdma()` ran only after line 1. On hardware HDMA runs once in the
H-blank of V=0, before line 1, and once after each line; here every table
entry after the first landed one line late.

In this game the HUD text layer (BG3) is switched on and off by HDMA on
`$212C`. One line late, BG3 stayed on for one extra row and showed a 1-pixel
line of whatever tile row sits below the text: between the portraits at
line 207 and near the scoreboard at line 23 on the court. bsnes shows
neither. With the patch both are gone on the same deterministic attract
frame (3901), and the per-line state (`ppu_lines`) shows every HDMA change
one line earlier. Play-checked on the court and character select.

Checked on this game only. The driver is shared by every title that uses it,
so check another title before offering it upstream.

## snesrecomp-generate-coverage-dir.patch

Taken unchanged from the Strike Gunner S.T.G port, where it was written.
`generate --coverage-dir <dir>` passes every capture in a directory to the
emitter as a historical manifest, each validated on its own, so captures from
different builds combine. Without it `generate` only takes
`--profile-manifest`, which refuses to merge captures from different builds
("cannot merge captures from different ROMs/modules/mappers/builds"). Every
rebuild changes the build digest, so a second capture could never be added.
The launcher's Generate & rebuild also passes `recomp/coverage` when it
exists. `tools/regen.sh` uses `--coverage-dir recomp/coverage`.

## snesrecomp-nested-deadline-yield.patch

A nested interpreter run -- compiled code calling a routine the analyzer left
interpreter-only -- never checked the frame deadline: `_interp_run_core`
checked it only in the frame driver's own top-level run (`auto_quiescent`).
A long interpreted callee then ran straight past vblank and NMI landed
mid-screen. The patch makes a nested run poll the same
`interp_bridge_lle_master_deadline_reached()` every compiled block polls
(same guards: a scheduler frame owns the run and this is inside its bounce)
and, when it fires, arm the existing deadline unwind at that instruction, the
way the nested-frame hand-off in `bridge_cooperative_yield()` does. Nested
frames pass it outward and the scheduler frame publishes the PC as the
resume point.

Needed by the decompressor exit declaration in `recomp/bank83.cfg`. With the
declaration and without the patch, the character-select black squares came
back (frames 3537-3538 of the scripted route), with a 48,000-clock overshoot
at the decompressor. With both: no flashes, the decompressor's deadline
report lands 8 clocks past the deadline, the court frame is clean, a scripted
full match ends TEAM 1 WINS with matching final stats, the stats screen
matches a build without either change, real-time audio has no underflows
(two of three runs; the third had one host-noise burst), and
`tests/interp816/bridge_test.c` passes 186/186 (as without the patch).

## Applying

After a fresh clone or `git submodule update`, from the project root:

    git -C snesrecomp apply ../patches/snesrecomp-hdma-line0.patch
    git -C snesrecomp apply ../patches/snesrecomp-generate-coverage-dir.patch
    git -C snesrecomp apply ../patches/snesrecomp-nested-deadline-yield.patch

Moving to a newer upstream framework: check out the new commit in
`snesrecomp`, re-apply, and drop any patch upstream has absorbed.
