# Widescreen and Adaptive View

The court widens to 16:9 with Adaptive View margins, sprites drawn in them,
the bottom HUD anchored to the screen edges and the off-screen arrows moved
to the widened edges (below). This file records which invariants of
`snesrecomp/docs/WIDESCREEN_PATTERNS.md` are verified, and how, in the order
that document prescribes.

Widescreen is on by default. The launcher's "Aspect ratio" row switches
between "16:9 widescreen" and the original "4:3". The framework's launcher has
no Widescreen row for SNES ports (`docs/UPSTREAM_ISSUES.md`, item 7), so the
port puts the choice on the aspect row (`src/main.c`). The host stores that
row as `[Graphics] DisplayAspect` under its own names: `4:3` (index 0) means
widescreen and `8:7` (index 1) means the original picture. Both choices keep
the 4:3 CRT pixel aspect. `[Graphics] Widescreen` mirrors the choice and is
rewritten from it every frame.
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
- BG3 (HUD text) is clamped to its native 256 columns above the bottom HUD
  (line 176); the bottom HUD is anchored to the edges (below).
- The renderer blacks the bare side itself on the court (HUD-always-visible,
  below). Elsewhere it leaves undrawn margin columns as they were, so they are
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

## P7/P8 -- sprites in the margins (implemented)

The game builds its OAM shadow (`$1200` low table, `$1400` high table) once a
frame in the routine at `$80:C3C0`. It culls each object against the native
screen (object x plus `$0544,X` against 256, x plus `$056C,X` against 0; an
off-screen object gets bit `$0200` of `$04F4,X` and no sprites), then each
metasprite piece (`$7E:2500+`; x in [-15, 256) for 16x16 pieces, [-7, 256)
for 8x8). Its x >= 0 path never sets the 9th X bit, so no limit change alone
could place a piece past x 256.

`recomp/bank00.cfg` replaces the routine (`hle_func $C3C0`) with
`HleLtbbOamEmit` (`src/ltbb_oam_widen.c`), which runs the original: it does
the first instruction (`STZ $1400`) itself, charges the 5 cycles the compiled
block charges for it, and hands the caller's JSL frame to the compiled body
declared at `$C3C3` (`func LtbbOamEmitBody`), whose RTL pops it. In
widescreen court play only, a read-only pass then replays the original's
decisions, finds where its sprites end, and appends the pieces it culled at
the native edge that lie in the margins into the free slots after them,
hiding its previous extras that the game's high-water mark (`$0106`) would
not. It writes nothing but those OAM-shadow slots and charges no CPU time.
`SNESRECOMP_WS_OAM=0` turns it off (P14).

Checked:

- 4:3: the baseline matches on all three routes (3701 frames).
- Simulation: the turbo `match` route at 4:3 and at 16:9, 25 WRAM dumps
  across the whole match: identical everywhere outside the OAM shadow
  (`$1200-$141F`).
- 16:9, the same route with the pass on and off: sprites appear in the
  margins in 1107 of 2426 sampled frames -- players, and the hoop's rim,
  backboard and net. The native area is unchanged except in 7 frames, where
  an object the game culled as a whole has a piece reaching up to 4 px past
  the native edge, now drawn.

Known limit: margin sprites take the lowest sprite priority and are dropped
when all 128 slots are in use.

## Bottom HUD anchored to the edges (implemented)

The bottom HUD is the four portraits and BG3 text between them: each
team's money (between its two portraits) and power-up name. The portraits
are OAM slots 0-15: the emitter re-emits them first whenever `$0DB5` is 0 and
then stores the slot after them in `$0DB5`, which ended every emission of a
full match at `$40` (16 slots).

In widescreen court play, `HudPlacement()` in `src/ltbb_widescreen.c` moves
both outward: content left of x 120 moves left by the margin and content from
x 136 moves right by it. That applies to BG3 from line 176 down
(`PpuSetWidescreenLayerAnchorBand`) and to OAM slots 0-15
(`PpuSetWsHudOamBand` / `PpuSetWsHudOamShiftRange`). Above line 176 BG3
stays clamped to the native columns. Off the court nothing moves.
Presentation only: the PPU places the layers; no game memory is touched.

Two things were needed beyond setting the bands:

- BG3 is 2bpp in this mode, and the framework applied the anchor band only
  to 4bpp layers, so the text stayed put while its portraits moved and the
  money ended up against a portrait.
  `patches/snesrecomp-anchor-band-2bpp.patch` fixes that.
- The shift followed the live margin, so at a court end, where a side goes
  black, the portraits and text slid inward with the court.
  `PpuSetWidescreenHudAlwaysVisible` makes them use the full margin. The
  renderer then composes the full width and blacks everything past the court
  end except the HUD's pixels (and sprites), so the HUD stays at the frame
  edges.

Checked on 16:9 frames of the whole `match` route: above line 176 every
frame is identical to the build before these two changes. Below it, the
portraits sit at the frame edges, also at the court's left end (17 px of
black), with each team's money between its two portraits as at 4:3. The 4:3
baseline matches on all three routes.

## Off-screen arrows (implemented)

When a player is culled at the native screen edge, the game shows an arrow
at that edge in the player's palette. The arrows are objects `$18`, `$1A`,
`$1C` and `$1E`, one for each of players 0, 2, 4 and 6 (found from WRAM and
OAM dumps of the `match` route). They are camera-relative in y, use tiles
`$A0-$B7`, and sit at x 16-20 on the left and 236-245 on the right.

In widescreen court play, the same post-pass as the margin sprites
(`AdaptArrows` in `src/ltbb_oam_widen.c`) handles them:

- A player drawn on the widened screen (the original's object cull against
  256 + margin and -margin) gets its arrow hidden.
- A player past the margin too keeps its arrow, moved out by the margin on
  the side the game put it.

Only the arrow's own OAM-shadow slots change. The flag the game logic reads
(`$04F4` bit `$0200`, used by AI and sound routines in banks `$80`-`$82`) is
left as the original set it.

Checked:

- 4:3: the baseline matches on all three routes (3701 frames) after the HUD
  and arrow changes.
- Simulation: 51 WRAM dumps of court play, 4:3 against 16:9: identical
  outside the OAM shadow.
- 16:9: in dumps where the game shows an arrow, arrows for players visible
  in a margin are gone. The others sit at the widened edges (x 275 on the
  right, around -29 on the left, with a 43-px margin) and point the way
  their players went.

## Open

A report from play: part of Daffy's sprite appeared separated from him. Not
reproduced yet. On the `match` route, comparing every sampled 16:9 frame with
4:3 found no difference above the HUD other than arrows, and the sprites
seen over the scoreboard (a player who has jumped past the top of the
screen) are the same at 4:3. Ruled out: player pieces in the HUD's OAM slots
(`$0DB5` was always `$40`) and the per-line sprite limit (`NoSpriteLimits`).

## Not started

P15. Tournament screens and two-player games are not yet checked in
widescreen.
