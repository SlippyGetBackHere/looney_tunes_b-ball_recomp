/*
 * Change tournament difficulty -- trusted plugin for the "Change tournament
 * difficulty" feature of the looneytunesbball.gameplay package
 * (mods/preloaded/packages/).
 *
 * The main menu's difficulty is $0CE9, 0-9 (shown as 1-10). Selecting a
 * tournament mode ($115D 5 or 6) sets $1165, and two places then lock the
 * difficulty at 4 (shown as 5):
 * - Left/right on the Difficulty line goes to $81:C86A, which with $1165 set
 *   plays the error sound and blinks the line instead of changing the value
 *   ($81:C8B0).
 * - The menu refresh at $81:CB35, called from the input handler whenever the
 *   cursor moves (JSR at $C722, $C74B, $C894), writes 4 to $0CE9 and redraws
 *   it while $1165 is set ($81:CB68).
 * Both callers are in the menu's input routine at $81:C7AC, which runs on the
 * interpreter (the analyzer leaves it interpreter-only), so it executes the
 * bytes in the cartridge buffer.
 * When the game starts, $80:D043 takes the tournament's base from the menu
 * ($0CF3 = $0CE9 - 1) and zeroes the round ($117F). Before every match the
 * tournament map ($82:F04B) is left with a button press, which increments
 * the round ($82:F0C0), and the match is then played at min($0CF3 + round, 9)
 * ($80:F731): the first match at the menu's difficulty, each later one a
 * step harder.
 *
 * While the feature is on, these bytes of the input routine are changed in
 * the cartridge buffer:
 * - the branch at $81:C86D (BEQ, taken when no tournament is selected) is
 *   made unconditional (BRA), so left/right change the difficulty in every
 *   mode;
 * - the three calls to the refresh are pointed at $81:FF60, unused padding
 *   with a helper declared there (recomp/bank81.cfg), which runs the refresh
 *   with $1165 read as 0, so it no longer forces 4, and puts $1165 back;
 *   nothing else in the refresh reads it.
 * The tournament then starts from the chosen difficulty and rises each round
 * as in the original. Difficulty 1 makes the base -1, which is right: the
 * round is at least 1 by the time it is added (checked: the first match is
 * played at 1).
 *
 * All original bytes are checked before any is changed (a mismatch leaves the
 * ROM alone) and put back when the feature is turned off. Off, nothing is
 * changed and the helper is never reached: the menu is the original. If the
 * input routine is ever compiled, the change stops having any effect.
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

#define LTBB_TOURNAMENT       0x1165

/* LoROM file offsets of bank $81 code, with the original and changed bytes. */
typedef struct {
  uint32_t offset;
  uint8_t stock[3], mod[3];
  uint8_t len;
} LtbbRomEdit;

static const LtbbRomEdit kEdits[] = {
  { 0x00C86D, { 0xF0 }, { 0x80 }, 1 },                           /* BEQ -> BRA */
  { 0x00C722, { 0x20, 0x35, 0xCB }, { 0x20, 0x60, 0xFF }, 3 },   /* JSR $CB35 -> $FF60 */
  { 0x00C74B, { 0x20, 0x35, 0xCB }, { 0x20, 0x60, 0xFF }, 3 },
  { 0x00C894, { 0x20, 0x35, 0xCB }, { 0x20, 0x60, 0xFF }, 3 },
};
#define LTBB_EDITS (sizeof(kEdits) / sizeof(kEdits[0]))

static bool s_active;
static bool s_patched;

static uint8_t *RomAt(uint32_t offset, uint32_t bytes)
{
  if (!g_snes || !g_snes->cart || !g_snes->cart->rom ||
      g_snes->cart->romSize < offset + bytes)
    return NULL;
  return g_snes->cart->rom + offset;
}

static void PatchMenu(void)
{
  if (s_patched)
    return;
  for (size_t i = 0; i < LTBB_EDITS; i++) {
    const uint8_t *p = RomAt(kEdits[i].offset, kEdits[i].len);
    if (!p || memcmp(p, kEdits[i].stock, kEdits[i].len) != 0) {
      fprintf(stderr, "[tournament_difficulty] menu code not as expected; left alone\n");
      return;
    }
  }
  for (size_t i = 0; i < LTBB_EDITS; i++)
    memcpy(RomAt(kEdits[i].offset, kEdits[i].len), kEdits[i].mod, kEdits[i].len);
  s_patched = true;
}

static void RestoreMenu(void)
{
  if (s_patched)
    for (size_t i = 0; i < LTBB_EDITS; i++) {
      uint8_t *p = RomAt(kEdits[i].offset, kEdits[i].len);
      if (p)
        memcpy(p, kEdits[i].stock, kEdits[i].len);
    }
  s_patched = false;
}

/* hle_func $81:FF60 (recomp/bank81.cfg), reached only through the changed
 * calls: the caller's JSR frame is on the stack, and the refresh's RTS pops
 * it. */
RecompReturn HleLtbbMenuRefresh(CpuState *cpu)
{
  const uint32_t bank = (uint32_t)cpu->PB << 16;
  const uint16_t tournament = cpu_read16(cpu, cpu->DB, LTBB_TOURNAMENT);
  const bool hide = s_active && tournament != 0;
  if (hide)
    cpu_write16(cpu, cpu->DB, LTBB_TOURNAMENT, 0);
  RecompReturn r = cpu_dispatch_call_pc_pushed(cpu, bank | 0xCB35, bank | 0xFF60, 2, NULL);
  if (hide)
    cpu_write16(cpu, cpu->DB, LTBB_TOURNAMENT, tournament);
  return r;
}

static void ActivateTournamentDifficulty(void)
{
  s_active = true;
  PatchMenu();
}

static void ResetTournamentDifficulty(void)
{
  s_active = false;
  RestoreMenu();
}

SNES_MOD_CONSTRUCTOR(RegisterLtbbTournamentDifficulty)
{
  snes_mod_register_reset_callback(ResetTournamentDifficulty);
  snes_mod_register_activation_plugin("looneytunesbball.tournament_difficulty",
                                      ActivateTournamentDifficulty);
}
