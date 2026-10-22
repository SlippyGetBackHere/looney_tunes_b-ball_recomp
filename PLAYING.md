# Playing Looney Tunes B-Ball

A native PC version of the 1995 SNES basketball game **Looney Tunes B-Ball**,
made by recompiling the game's code, with an optional 16:9 widescreen court.
If anything looks, sounds or plays differently from the console, please
report it.

**You need your own copy of the game.** The download contains no game data.
The first time you run it, it builds the game on your PC from your ROM file.

## What you need

- Windows 10 or 11, 64-bit
- A ROM of **Looney Tunes B-Ball (USA)**, which the setup checks:
  - CRC32 `6d3bc96f`
  - SHA-256 `76ba0fc1f5c1f39354bb3173a600f23915f1191f400f7d525d220b4b3c8d958d`

  Other versions or modified ROMs are refused.
- About 2 GB of free disk space
- An internet connection the first time, if the build tools are not already
  on your PC (see below)

## First run

1. Unzip the download into a folder of its own and start
   `LooneyTunesBBallSNESRecomp.exe`.
2. Pick your ROM file. The launcher checks it and shows a badge.
3. Click **Generate & rebuild**. If the build tools (`cmake-clang-v1`) are not
   on your PC yet, the launcher offers to download them first. They are
   shared with other recompiled games and only downloaded once.
4. The launcher closes and a console window opens and builds the game,
   which takes a few minutes. Let it finish; the launcher then reopens with a
   **Play** button.

If the console stops with "Configure failed" or "Build failed", it waits for
you. Please copy its last lines into your report.

Windows may warn that the program is from an unknown publisher, because the
release is not signed.

## Playing again

The finished game is in the `build` folder: start
`build\LooneyTunesBBallSNESRecomp.exe`. Your settings, save states and logs
are kept in `build` as well.

## Controls

Game controllers (Xbox, PlayStation, Switch Pro and similar) are detected
when plugged in. On the keyboard:

| SNES button | Key |
|---|---|
| D-pad | Arrow keys |
| B | Z |
| A | X |
| Y | A |
| X | S |
| L / R | C / V |
| Start | Enter |
| Select | Right Shift |

**Two players:** both players start on the same keyboard keys, so give the
second player a controller, or change the `[player2]` keys in
`build\keybinds.ini`.

Keyboard shortcuts:

| Key | Does |
|---|---|
| Shift+F1 … Shift+F10 | Save state to slot 1–10 |
| F1 … F10 | Load state from slot 1–10 |
| F11 | Save-state browser |
| F12 | Rewind |
| P | Pause |
| Tab (hold) | Fast forward |
| Alt+Enter | Fullscreen |
| Ctrl+R | Reset |
| Keypad + / − | Volume |

On a controller, Select+R opens the save-state browser and Select+R3 opens
rewind. The launcher's settings can turn rewind off or change how far back it
reaches.

## Options

In the launcher, or in `build\config.ini` under `[Graphics]`:

- **Aspect ratio**: **16:9 widescreen** (the default) widens the court to
  fill a 16:9 screen; **4:3** is the original picture. In widescreen,
  players, the ball and the hoops carry on into the extra space, the bottom
  HUD sits at the screen edges, and the arrow that points to a player off
  the screen comes from the widened edge. At the far ends of the court,
  past the game's own artwork, the side is black. Menus and other screens
  stay 4:3. The game plays the same either way. (In `config.ini` this is
  stored as `DisplayAspect = 4:3` for widescreen and `8:7` for the original
  picture, a naming quirk of the framework.)
- **VSync: set it to adaptive.** With plain VSync on (`VSync = 1`) the music
  can skip and briefly slow down, because the game waits on the display and
  loses time whenever it misses a refresh. With VSync off the court tears.
  `VSync = adaptive` has neither problem.

## Mods

The launcher's **Mods** page has one package, **Testing aids**, off by
default:

- **Team 1 always wins**: team 1 (the left score on the scoreboard) is kept
  two points ahead once team 2 has scored, so every match ends in a team 1
  win, tournament included. Both teams still play and score normally. Meant
  for seeing the whole game while testing.

When reporting a problem, say whether it was on.

## Known issues

See `KNOWN_ISSUES.md`. In short:

- Clicking the window's title bar makes the music hitch for a moment.
  Clicking inside the picture does not.
- Sylvester's character-select screen has a teal line at the bottom. The
  original game does this too.
- In widescreen, part of Daffy's sprite was once seen separated from him
  while the game was paused during an inbound. Not reproduced yet; a save
  state of it would help.

## Reporting a problem

Please include:

1. What happened and what you expected, and where in the game.
2. Whether it happens every time, and whether it happens in 4:3 as well as
   widescreen.
3. `build\last_run_report.json` from the session where it happened (it is
   overwritten on the next start, so copy it first). It records where your
   ROM file is on your PC; edit that path out if you would rather not share
   it.
4. A screenshot or short video for anything visual, a recording for sound.
5. For setup problems, the last lines of the console window.

A save state from just before the problem (the files in `build\saves`) helps
most of all, but save states hold the console's video and sound memory,
which is copied from the game. Don't post them publicly: say you have one,
and send it privately if asked.
