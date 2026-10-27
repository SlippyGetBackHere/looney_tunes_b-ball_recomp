/*
 * Shot clock off -- trusted plugin for the "Shot clock off" feature of the
 * looneytunesbball.gameplay package (mods/preloaded/packages/).
 *
 * The game's timers tick in $81:DED2, once a frame: the game clock in $0DA1
 * (seconds) with its frame counter $0DA3, and the shot clock in $0DA5
 * (seconds, reset to 15 on every change of possession) with its frame counter
 * $0DA7. Each frame counter counts 59 down to 0; when it wraps, its seconds
 * count drops by one. The shot clock is stopped while $0DA9 is set, and the
 * violation follows from $0DA5 reaching 0.
 *
 * Once a frame, after the guest's frame, during court play ($0107 set), the
 * shot clock's frame counter is put back to 59. It never wraps, so the shot
 * clock never counts down and never runs out: it stays at the 15 it is reset
 * to, and the game clock runs as normal. Nothing else is written. Disabled,
 * the plugin is never activated and the frame callback is never registered:
 * the stock game is unchanged.
 */
#include <stdbool.h>
#include <stdint.h>

#include "common_rtl.h"
#include "game_rtl.h"
#include "mod_runtime.h"

#define LTBB_COURT_FLAG      0x0107
#define LTBB_SHOT_CLOCK_SUB  0x0DA7   /* frames left in the current second */
#define LTBB_SUB_FULL        59

static bool s_active;

static void NoShotClockFrame(void)
{
  if (!s_active || g_ram[LTBB_COURT_FLAG] != 1)
    return;
  g_ram[LTBB_SHOT_CLOCK_SUB] = LTBB_SUB_FULL;
  g_ram[LTBB_SHOT_CLOCK_SUB + 1] = 0;
}

static void ActivateNoShotClock(void)
{
  s_active = true;
  snes_mod_register_frame_callback(NoShotClockFrame);
}

static void ResetNoShotClock(void)
{
  s_active = false;
}

SNES_MOD_CONSTRUCTOR(RegisterLtbbNoShotClock)
{
  snes_mod_register_reset_callback(ResetNoShotClock);
  snes_mod_register_activation_plugin("looneytunesbball.no_shot_clock",
                                      ActivateNoShotClock);
}
