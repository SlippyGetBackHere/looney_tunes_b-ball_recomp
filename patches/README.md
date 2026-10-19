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

## Applying

After a fresh clone or `git submodule update`, from the project root:

    git -C snesrecomp apply ../patches/snesrecomp-hdma-line0.patch

Moving to a newer upstream framework: check out the new commit in
`snesrecomp`, re-apply, and drop any patch upstream has absorbed.
