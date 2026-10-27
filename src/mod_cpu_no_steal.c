/*
 * CPU doesn't steal -- trusted plugin for the "CPU doesn't steal" feature of
 * the looneytunesbball.gameplay package (mods/preloaded/packages/).
 *
 * Every player is driven by buttons, whoever controls it. Once a frame the
 * update at $83:F38F copies each controller's newly pressed buttons into the
 * player it controls ($83:F3B6: $0814,X, joypad bit layout), runs the AI
 * ($81:8000), which for every player whose $04F4,X has bits $0006 set
 * clears $0814,X and presses virtual buttons there instead, then runs the
 * player logic ($82:B91D), which acts on $0814,X. A CPU defender steals in
 * two ways: chasing the ball handler it presses Y ($4000) on a random roll
 * ($81:80C2), and once within 12 units of the ball handler it presses X
 * ($0040) for a swipe at close range ($81:946C, distance from $82:CA4C;
 * seen taking the ball from a human player who walked up to a defender that
 * Y was already cleared from). B ($8000) is the jump that blocks and
 * rebounds. On offense Y passes and B shoots. (A, the special, is never
 * pressed by the AI: in a scripted match no CPU player had it in $0814,X, so
 * the CPU's powers are triggered some other way and are not covered here.)
 * $10D5 is the team with the ball, 0 or 2, and a player's team is
 * $80:F502,X: players 0 and 2 are team 0, 4 and 6 team 2.
 *
 * The update at $83:F38F runs on the interpreter (the analyzer leaves it
 * interpreter-only), so it executes the bytes in the cartridge buffer. While
 * the feature is on, its call to the player logic (JSL $82:B91D at $83:F3AE)
 * is pointed at $83:FF40, unused padding with a helper declared there
 * (recomp/bank83.cfg). During court play the helper clears the chosen
 * buttons from the $0814,X of every AI-driven player on defense, when the
 * team with the ball has a human player on it (or, with the teammate option,
 * whoever has the ball), then runs $82:B91D. Options: steals never (Y and X
 * cleared, the default) or only at close range (Y cleared, X kept); also
 * for your CPU teammate; no jumps (B cleared). Human players are never
 * touched, nor the offense (passes and shots). The original bytes are
 * checked before they are changed (a mismatch leaves the ROM alone) and put
 * back when the feature is turned off, and the helper is then never reached:
 * the stock game is unchanged. If the update is ever compiled, the change
 * stops having any effect.
 *
 * A player counts as human once it has gone 60 frames in a row without the
 * AI flag on the court, and stops counting as human only after 600 frames
 * in a row with it (so a change of game mode corrects itself): the game
 * hands a human's player to the AI for moments such as an inbound, and its
 * team must still count as human then. The marks are kept across breaks, so
 * a new court scene starts with them in place. The CPU players'
 * flags are cleared too, but only briefly -- when a court scene starts and by
 * $81:D74C -- for at most 14 frames in a scripted match, while a human's
 * player went over 1,000 frames at a time. (Marking a player human the
 * first time its flag was clear marked every CPU player human at the start
 * of each scene, so the CPU teammate was treated as an opponent's defender.)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common_rtl.h"
#include "cpu_state.h"
#include "game_rtl.h"
#include "mod_runtime.h"
#include "snes/cart.h"
#include "snes/snes.h"

extern Snes *g_snes;

#define LTBB_PACKAGE        "looneytunesbball.gameplay"
#define LTBB_FEATURE        "cpu_no_steal"
#define LTBB_COURT_FLAG     0x0107
#define LTBB_PLAYER_FLAGS   0x04F4   /* ,X: bits $0006 set while AI-driven */
#define LTBB_PLAYER_PRESSED 0x0814   /* ,X: buttons pressed this frame */
#define LTBB_OFFENSE_TEAM   0x10D5   /* 0 or 2 */
#define LTBB_AI_DRIVEN      0x0006
#define LTBB_BUTTON_B       0x8000   /* jump: block, rebound */
#define LTBB_BUTTON_Y       0x4000   /* steal, chasing */
#define LTBB_BUTTON_X       0x0040   /* steal, close range */
#define LTBB_PLAYERS        4
#define LTBB_HUMAN_FRAMES   60       /* frames in a row without the AI flag */
#define LTBB_CPU_FRAMES     600      /* frames in a row with it */
#define LTBB_CALL_SITE      0x01F3AE   /* $83:F3AE in the LoROM file */

static const uint8_t kStockCall[4] = { 0x22, 0x1D, 0xB9, 0x82 };   /* JSL $82:B91D */
static const uint8_t kModCall[4] = { 0x22, 0x40, 0xFF, 0x83 };     /* JSL $83:FF40 */

static bool s_active;
static bool s_patched;
static uint16_t s_mask;
static bool s_teammate;   /* also the CPU teammates of human players */
static bool s_human_seen[LTBB_PLAYERS];
static uint8_t s_free_frames[LTBB_PLAYERS];   /* frames in a row without the AI flag */
static uint16_t s_ai_frames[LTBB_PLAYERS];    /* frames in a row with it */

static uint16_t ReadWord(uint32_t addr)
{
  return (uint16_t)(g_ram[addr] | (g_ram[addr + 1] << 8));
}

static uint16_t Team(int obj) { return obj >= 4 ? 2 : 0; }   /* $80:F502,X */

static void MaskCpuDefenders(void)
{
  if (g_ram[LTBB_COURT_FLAG] != 1) {
    memset(s_free_frames, 0, sizeof(s_free_frames));
    memset(s_ai_frames, 0, sizeof(s_ai_frames));
    return;
  }
  for (int i = 0; i < LTBB_PLAYERS; i++) {
    if (ReadWord(LTBB_PLAYER_FLAGS + 2 * i) & LTBB_AI_DRIVEN) {
      s_free_frames[i] = 0;
      if (s_ai_frames[i] < LTBB_CPU_FRAMES && ++s_ai_frames[i] >= LTBB_CPU_FRAMES)
        s_human_seen[i] = false;
    } else {
      s_ai_frames[i] = 0;
      if (s_free_frames[i] < LTBB_HUMAN_FRAMES && ++s_free_frames[i] >= LTBB_HUMAN_FRAMES)
        s_human_seen[i] = true;
    }
  }
  const uint16_t offense = ReadWord(LTBB_OFFENSE_TEAM);
  bool offense_human = false;
  for (int i = 0; i < LTBB_PLAYERS; i++)
    if (s_human_seen[i] && Team(2 * i) == offense)
      offense_human = true;
  if (!offense_human && !s_teammate)
    return;
  for (int i = 0; i < LTBB_PLAYERS; i++) {
    const int obj = 2 * i;
    if (Team(obj) == offense || !(ReadWord(LTBB_PLAYER_FLAGS + obj) & LTBB_AI_DRIVEN))
      continue;
    const uint16_t pressed = ReadWord(LTBB_PLAYER_PRESSED + obj) & (uint16_t)~s_mask;
    g_ram[LTBB_PLAYER_PRESSED + obj] = (uint8_t)pressed;
    g_ram[LTBB_PLAYER_PRESSED + obj + 1] = (uint8_t)(pressed >> 8);
  }
}

static uint8_t *CallSite(void)
{
  if (!g_snes || !g_snes->cart || !g_snes->cart->rom ||
      g_snes->cart->romSize < LTBB_CALL_SITE + sizeof(kStockCall))
    return NULL;
  return g_snes->cart->rom + LTBB_CALL_SITE;
}

static void PatchCall(void)
{
  uint8_t *p = CallSite();
  if (!p || s_patched)
    return;
  if (memcmp(p, kStockCall, sizeof(kStockCall)) != 0) {
    fprintf(stderr, "[cpu_no_steal] player update not as expected; left alone\n");
    return;
  }
  memcpy(p, kModCall, sizeof(kModCall));
  s_patched = true;
}

static void RestoreCall(void)
{
  uint8_t *p = CallSite();
  if (p && s_patched)
    memcpy(p, kStockCall, sizeof(kStockCall));
  s_patched = false;
}

/* hle_func $83:FF40 (recomp/bank83.cfg), reached only through the changed
 * call: the caller's JSL frame is on the stack, and $82:B91D's RTL pops it. */
RecompReturn HleLtbbPlayerLogic(CpuState *cpu)
{
  if (s_active)
    MaskCpuDefenders();
  return interp_tier_run_call_frame(cpu, 0x82B91D, 0x83FF40, 3, NULL);
}

static void Option(const char *id, char *value, uint32_t cap, const char *fallback)
{
  if (!snes_mod_runtime_feature_option_value_c(LTBB_PACKAGE, LTBB_FEATURE, id,
                                               value, cap) || !value[0])
    snprintf(value, cap, "%s", fallback);
}

static bool OptionOn(const char *id, bool fallback)
{
  char value[16];
  Option(id, value, sizeof(value), fallback ? "true" : "false");
  return strcmp(value, "false") != 0;
}

static void ActivateCpuNoSteal(void)
{
  s_active = true;
  char steals[16];
  Option("steals", steals, sizeof(steals), "never");
  s_mask = LTBB_BUTTON_Y;
  if (strcmp(steals, "close") != 0)
    s_mask |= LTBB_BUTTON_X;
  s_teammate = OptionOn("teammate", false);
  if (OptionOn("block_jumps", false))
    s_mask |= LTBB_BUTTON_B;
  memset(s_human_seen, 0, sizeof(s_human_seen));
  memset(s_free_frames, 0, sizeof(s_free_frames));
  memset(s_ai_frames, 0, sizeof(s_ai_frames));
  PatchCall();
}

static void ResetCpuNoSteal(void)
{
  s_active = false;
  memset(s_human_seen, 0, sizeof(s_human_seen));
  memset(s_free_frames, 0, sizeof(s_free_frames));
  memset(s_ai_frames, 0, sizeof(s_ai_frames));
  RestoreCall();
}

SNES_MOD_CONSTRUCTOR(RegisterLtbbCpuNoSteal)
{
  snes_mod_register_reset_callback(ResetCpuNoSteal);
  snes_mod_register_activation_plugin("looneytunesbball.cpu_no_steal",
                                      ActivateCpuNoSteal);
}
