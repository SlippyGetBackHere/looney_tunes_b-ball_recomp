/*
 * Team 1 always wins -- trusted plugin for the "Team 1 always wins" feature
 * of the looneytunesbball.testing package (mods/preloaded/packages/). A
 * testing aid: the tournament is hard to beat, and a playthrough for
 * coverage captures needs to reach every round and the ending.
 *
 * The game keeps each team's score as a 16-bit word, team 1 at $0D43 and
 * team 2 at $0D45 ($0D43,Y with Y = 0 or 2). Baskets add to it ($81:DF5F,
 * $82:FF2E) and the result is decided by comparing the two ($80:E670,
 * $80:E6B5, $80:E8EE, $81:E209). The scoreboard words at $0D47/$0D49 are
 * only display copies the game writes from these; raising the copy alone
 * changed the scoreboard but not who won.
 *
 * Once a frame, after the guest's frame, as soon as team 2 has scored and
 * team 1 is not ahead, team 1 is raised to team 2's score plus two. Team 2's
 * score is never touched, so every scoring path still runs; the game's own
 * code then decides the match for team 1. The game refreshes a team's
 * scoreboard copy only when that team scores, so the copy is written too
 * and the scoreboard does not show a stale number. Nothing
 * is written before team 2's first basket, so boot and the menus are
 * untouched. Disabled, the plugin is never activated and the frame callback
 * is never registered: the stock game is unchanged.
 */
#include <stdbool.h>
#include <stdint.h>

#include "common_rtl.h"
#include "game_rtl.h"
#include "mod_runtime.h"

#define LTBB_SCORE_TEAM1 0x0D43
#define LTBB_SCORE_TEAM2 0x0D45
#define LTBB_BOARD_TEAM1 0x0D47 /* scoreboard copy of team 1's score */
#define LTBB_LEAD 2

static bool s_active;

static uint16_t ReadWord(uint32_t addr)
{
  return (uint16_t)(g_ram[addr] | (g_ram[addr + 1] << 8));
}

static void Team1WinsFrame(void)
{
  if (!s_active)
    return;
  uint16_t team1 = ReadWord(LTBB_SCORE_TEAM1);
  uint16_t team2 = ReadWord(LTBB_SCORE_TEAM2);
  if (team2 != 0 && team1 <= team2) {
    uint16_t lead = (uint16_t)(team2 + LTBB_LEAD);
    g_ram[LTBB_SCORE_TEAM1] = (uint8)lead;
    g_ram[LTBB_SCORE_TEAM1 + 1] = (uint8)(lead >> 8);
    g_ram[LTBB_BOARD_TEAM1] = (uint8)lead;
    g_ram[LTBB_BOARD_TEAM1 + 1] = (uint8)(lead >> 8);
  }
}

static void ActivateTeam1Wins(void)
{
  s_active = true;
  snes_mod_register_frame_callback(Team1WinsFrame);
}

static void ResetTeam1Wins(void)
{
  s_active = false;
}

SNES_MOD_CONSTRUCTOR(RegisterLtbbTestingMods)
{
  snes_mod_register_reset_callback(ResetTeam1Wins);
  snes_mod_register_activation_plugin("looneytunesbball.team1_wins",
                                      ActivateTeam1Wins);
}
