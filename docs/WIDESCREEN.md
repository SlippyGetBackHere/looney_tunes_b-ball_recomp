# Widescreen and Adaptive View

Work in progress. Nothing is widened yet; 4:3 is the only view. This file
records which invariants of `snesrecomp/docs/WIDESCREEN_PATTERNS.md` are
verified, and how, in the order that document prescribes.

## What the court is made of

Measured from PPU dumps during a scripted exhibition match (`ppuw.tsv`,
`regs.json`, VRAM):

- **BG2** (Mode 1, 64x32 map, 512 px): almost the whole court -- buildings,
  the wall scoreboard, and the floor. HDMA rewrites `BG2HOFS` every scanline
  for the floor's perspective. Its scroll reaches 1015, so the court is wider
  than the map: the game streams it through the map as a ring buffer.
- **BG1** (64x32): only the near basket's stanchion, positioned like an
  object; one scroll per frame, 0-215 over a match.
- **BG3**: the HUD text, unscrolled, switched on and off per line by HDMA on
  `$212C` (see `patches/snesrecomp-hdma-line0.patch`).
- **Sprites**: the players, the ball, and the bottom HUD (portraits, money).

So the court is a bounded, streamed arena: a wider view needs its margin
columns filled from the game's own court data (P2) and its sprite culling
widened (P7/P8).

## P16 -- 4:3 baseline

`tools/ws_baseline.py` runs a scripted route through the built executable,
hashes the presented frames and records or compares the list
(`tools/baseline/<route>.sha1`; hashes only, no images or ROM data). It runs
with an empty mod state beside the executable and restores the real one.

| route | mode | frames sampled |
|---|---|---|
| `attract` | turbo, boot + attract demo incl. its court game | 437 (frames 1-6977, every 16th) |
| `match` | turbo, menus, random teams, full exhibition match, result and stats | 2675 (3201-45985, every 16th) |
| `charselect` | real time, switching through the roster | 589 (3200-3788, every frame) |

In turbo the host renders only frames with `frame % 16 == 1`, so turbo
routes sample exactly those. Recorded, then re-run in compare mode: 0 of 3701
frames differ. The baseline is tied to the current generated code: after a
regen that changes what is compiled, re-record it before widescreen work
resumes.

    python tools/ws_baseline.py compare match

## P5/P6 -- court gate: `$0107`

`$0107` is 1 exactly while the court scene is presented and 0 otherwise.
Evidence (WRAM + PPU dumps every 160 frames of the `match` route, every 160
frames of the attract demo, and a pause/replay route):

- 0 through boot, the title, menus, character select and the attract intro;
- 1 from the first court frame, in the attract demo's game and in a match;
- 0 on the halftime and quarter-break screens, 1 again when play resumes;
- 1 while paused and during the instant replay -- the court is still what is
  on screen, which is what a widescreen view needs;
- 0 from the stats screen after the final buzzer.

Of 449 samples, 250 court and 199 not, it disagreed with the court/not-court
label once: a court frame still fading in under forced blank.

The gate is about the court being *presented*, not about the player pipeline
being live (P6): this port widens presentation and culling, not spawning or
progression (P9-P13 do not apply to a single-court game), so presentation is
the property that matters. Not yet covered by a route: tournament screens,
options, credits, two-player games.

## Not started

P1-P4 / P2b (court margins and Adaptive View at the court's ends), P7/P8
(sprite culling and the OAM emitter), HUD placement, P14 kill-switches, P15.
