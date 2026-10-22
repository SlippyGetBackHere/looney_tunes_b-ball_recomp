# Framework issues found by this port

Problems found while porting Looney Tunes B-Ball that belong in the shared
framework (`snesrecomp`, pinned in `framework_pins.txt`) rather than in this
project. Nothing here has been reported or offered upstream yet. Each entry
says what is wrong, how it showed up here, what this project does about it
today, and what a fix upstream would look like.

| # | Area | This project today |
|---|---|---|
| 1 | HDMA applied one line late | local patch |
| 2 | `generate` cannot combine captures from different builds | local patch (from Strike Gunner) |
| 3 | Nested interpreter runs ignore the frame deadline | local patch |
| 4 | `debug_server.c` misses `<errno.h>` | build flag for trace builds |
| 5 | VSync default makes audio skip | `VSync = adaptive` in config.ini |
| 6 | Title-bar click stalls audio | documented, not worked around |
| 7 | Launcher has no widescreen control for SNES ports | aspect row repurposed in `src/main.c` |
| 8 | Widescreen anchor band ignored on 2bpp layers | local patch |

The four patches are in `patches/` with their evidence in
`patches/README.md`; the summaries below do not repeat all of it.

## 1. HDMA applied one line late

`snesrecomp/runner/src/beam_frame_driver.c`,
`snes_beam_frame_driver_draw_ppu_frame_observed()`.

The driver primed line 1 with each HDMA channel's first transfer and then
restored the channel, so the first real `dma_doHdma()` ran only after line 1.
On hardware HDMA runs in the H-blank of line 0 and after every line, so every
table entry after the first landed one line late. Here BG3 (switched per line
through `$212C`) stayed on one row too long and drew 1-pixel lines on the
court and under the scoreboard; bsnes shows neither.

Fix: `snesrecomp-hdma-line0.patch` (run `dma_doHdma()` for line 0 in place of
the priming). Checked on this game only; the driver is shared, so another
HDMA-heavy title should be checked before it goes upstream.

## 2. `generate` cannot combine captures from different builds

`snesrecomp_cli.py generate` takes coverage only through
`--profile-manifest`, which refuses to merge captures whose build digests
differ. Every rebuild changes the digest, so a second capture can never be
added to the first.

Fix: `snesrecomp-generate-coverage-dir.patch`, written for the Strike Gunner
S.T.G port and carried unchanged: `--coverage-dir <dir>` validates each
capture on its own and passes all of them as historical manifests. Two ports
now carry the same patch, which is a good reason to upstream it.

## 3. Nested interpreter runs ignore the frame deadline

`snesrecomp/runner/src/interp_bridge.c`, `_interp_run_core()`.

When compiled code calls a routine the analyzer left interpreter-only, the
nested interpreter run never polls the frame deadline; only the frame
driver's top-level run does. A long interpreted callee (here the portrait
decompressor, 48,000 clocks over) runs past vblank and NMI lands mid-screen:
black squares flashed on character select.

Fix: `snesrecomp-nested-deadline-yield.patch` polls
`interp_bridge_lle_master_deadline_reached()` in nested runs under the same
guards compiled blocks use, and arms the existing deadline unwind there.
`tests/interp816/bridge_test.c` passes 186/186 with and without it.

## 4. `debug_server.c` misses `<errno.h>`

`snesrecomp/runner/src/debug_server.c` uses `EIO` and `ENODATA` without
including `<errno.h>`. With the MinGW clang toolchain,
`-DSNESRECOMP_ENABLE_TRACE=ON` does not compile. Normal builds are not
affected.

This project passes `-DCMAKE_C_FLAGS="-include errno.h"` for trace builds
(`KNOWN_ISSUES.md`). Fix: add the include.

## 5. VSync default makes audio skip

`snesrecomp/runner/src/desktop/mmx_config.c` and the default `config.ini`
text in `host_main.c` default to `VSync = 1`. The display runs slightly
slower than the SNES (59.97 fps measured against 60.10). When a refresh is
missed the wait doubles, and the pacing clock drops that time instead of
catching up, so no audio is produced for it. The music skips and briefly
slows. `VSync = 0` cures the audio but tears.

`VSync = adaptive` syncs normally and shows only a late frame immediately: no
tearing and no skips, play-checked here. Fix: make adaptive the default, or
let the pacing clock catch up after a missed refresh.

## 6. Title-bar click stalls audio

Pressing on the window's title bar enters Windows' modal move/size loop, and
the host's main loop stops until the button is released. Audio comes from
the game loop (the sound CPU advances with guest frames), so output
underflows for that time: about 100 ms, around 3,200 samples, per click.
Focus changes without a title-bar press do not stall.

Not worked around here (`KNOWN_ISSUES.md`). Fix: keep frames running during
the modal loop (for example from an SDL event watch or a timer on the modal
loop), or produce audio off the main thread.

## 7. Launcher has no widescreen control for SNES ports

`snesrecomp/runner/src/desktop/host_main.c` and `launcher_video.h`.

- The host passes `widescreen_supported` to the launcher, but the SNES video
  page has no Widescreen row, and the host never copies `g_config.widescreen`
  into the launcher settings or back (`ls->widescreen` is untouched both
  ways, around lines 2600-2750). A port that supports widescreen can only be
  switched by editing `config.ini`.
- The Toggle Widescreen key prints "see the launcher's Mods page", which has
  no such control for this port.
- A port that supplies its own `aspect_labels` still has its choice stored as
  `[Graphics] DisplayAspect` under the host's own SNES aspect names and
  clamped to their three values. This port puts "16:9 widescreen" and "4:3"
  on that row (`src/main.c`), so in `config.ini` `DisplayAspect = 4:3` means
  widescreen and `8:7` means the original picture. The comments in
  `src/main.c` and `tools/baseline/config.ini` explain this.

Fix: a Widescreen row on the SNES launcher page when `widescreen_supported`
is set, synced with `g_config.widescreen` in both directions. Separately, a
port's custom aspect row could be stored as a plain index, or under its own
labels.

## 8. Widescreen anchor band ignored on 2bpp layers

`snesrecomp/runner/src/snes/ppu.c`, `PpuDrawBackground_2bpp()`.

`PpuSetWidescreenLayerAnchorBand()` is documented for any layer, but only
the 4bpp draws apply it. On a 2bpp layer it is silently ignored. This game's
HUD text is on BG3, which is 2bpp, so the money and power-up names stayed in
place while their portraits (sprites) moved out to the edges, and the money
ended up against a portrait.

Fix: `snesrecomp-anchor-band-2bpp.patch` calls the same
`PpuApplyLayerAnchorBand()` in the 2bpp draw (one line). The 8bpp and mode-7
draws still do not apply it; this game does not need them.
