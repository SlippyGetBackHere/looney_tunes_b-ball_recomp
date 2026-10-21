# Widescreen and Adaptive View

Work in progress. The court widens to 16:9 with Adaptive View margins
(below); sprites are not widened yet. This file records which invariants of
`snesrecomp/docs/WIDESCREEN_PATTERNS.md` are verified, and how, in the order
that document prescribes.

Turn it on with the launcher's Widescreen option (`[Graphics] Widescreen = 1`).
`SNESRECOMP_WS_EXTRA=<px>` pins the per-side margin for tests (0 forces 4:3);
`SNESRECOMP_WS_COURT=0` keeps every scene pillarboxed (P14 kill switch).

## What the court is made of

Measured from PPU dumps during a scripted exhibition match (`ppuw.tsv`,
`regs.json`, VRAM):

- **BG2**: the court -- buildings, the wall scoreboards, the floor -- held
  in VRAM as a strip of four 256-px screens at `$7000-$7FFF` (1024 px; both
  scoreboards, both keys, the centre circle at x 512). BG2 shows two adjacent
  screens of it: the BG2SC base steps one screen at a time (`$71`, `$75`,
  `$79`) and the scroll stays 0-255, so the scroll alone jumps by 256 when
  the view crosses a screen boundary (234, 241, 248, 254, 3, 6 ... while the
  camera moves smoothly). HDMA rewrites `BG2HOFS` every scanline for the
  floor's perspective. The court's true x on a line is screen * 256 + that
  line's scroll. Building rows are authored from x 120 to 904 (tile 0,
  transparent, outside -- the backdrop shows through as green); floor rows
  span the whole strip. Rows are streamed in vertically (`VMADD` writes to
  `$73C0/$77C0/$7BC0/$7FC0`: one row across all four screens).
- **BG1**: only the near basket's pole and arm, sliding through its own
  screens at `$6400/$6800/$6C00` the same way, one scroll per frame. The
  hoop's rim, backboard and net are sprites.
- **BG3**: the HUD text, unscrolled, switched on and off per line by HDMA on
  `$212C` (see `patches/snesrecomp-hdma-line0.patch`).
- **Sprites**: the players, the ball, and the bottom HUD (portraits, money).

So the plain map wrap is wrong in the margins whenever the view nears a
screen boundary -- the neighbouring screen is in VRAM, just outside the two
the PPU was told to show. A first attempt treated the 512-px map as the whole
court and blacked out whatever lay past it; because the scroll wraps every
256 px of travel, that blanked a side where the court continues and flipped
the black side at every boundary (flicker), and past the authored building
rows it showed the green backdrop.

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

## P1-P4 / P2b -- court margins (implemented)

`src/ltbb_widescreen.c` (`LtbbWidescreenDraw`, called from `GameDrawPpuFrame`):

- At 4:3 (no margin) it is the plain frame draw, so the 4:3 picture is the
  game's own: `tools/ws_baseline.py` matches on all three routes.
- Off the court (`$0107 == 0`) both margins are black: menus, character
  select, breaks and the stats screen stay authentic.
- On the court, margins come from the framework's world-keyed shadow tilemap
  (`snes/ws_shadow.h`). Each frame every cell of the BG2 and BG1 strips is
  supplied from VRAM keyed by court column (`WsShadowForceTile`), and BG2's
  world origin is set per scanline from that line's scroll -- read from this
  frame's HDMA table in WRAM, the value the PPU renders (P1) -- through the
  frame driver's per-line observer, so the perspective floor lines up.
- The margins stop at the court's authored ends, found each frame from the
  strip row under the top screen line; past them the side is black
  (Adaptive View).
- BG3 (HUD text) is clamped to its native 256 columns.
- The renderer leaves undrawn margin columns as they were, so they are
  cleared after the draw.
- The window is sized for the widescreen frame (`window_base_width` in
  `src/main.c`): the host creates it before the first frame is prepared, so
  sizing from that 256-px width gave a 4:3 window that letterboxed the 16:9
  picture. Now 1284x720 at scale 3 with Widescreen on, 960x720 without.

Checked in real time on the `match` route with `SNESRECOMP_WS_EXTRA=43`,
every frame 8000-11000: across a screen boundary (scroll 254 -> 3) the
frames are seamless, with real court in both margins; no frame flips its
black side (the first attempt flipped at every boundary); no green; black
only at the left end of the court, 17 px where the camera stops short of the
buildings' first tile. The 4:3 baseline still matches.

## Not started

P7/P8 (sprite culling and the OAM emitter): players and the ball are still
culled at the native edge, and the game's off-screen arrow points at players
who would now be inside the view. Bottom HUD sprites stay native (whether to
anchor them outward is a choice for later). P15. Tournament screens and
two-player games are not yet checked in widescreen.
