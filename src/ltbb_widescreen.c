/*
 * Looney Tunes B-Ball -- widescreen and Adaptive View (presentation only).
 *
 * docs/WIDESCREEN.md has the measurements behind every rule here. In short:
 * the court is a strip of four 256-px tilemap screens in VRAM ($7000-$7FFF,
 * 1024 px). BG2 shows two adjacent screens of it -- the BG2SC base steps one
 * screen at a time -- plus a 0-255 scroll, with an HDMA scroll per scanline
 * for the floor's perspective. So a scanline's true court x is
 * screen * 256 + that line's scroll, and the plain map wrap is wrong in the
 * margins whenever the view nears a screen boundary: the neighbouring screen
 * is in VRAM, but outside the two the PPU was told to show.
 *
 * Margins are therefore drawn from the whole strip through the framework's
 * world-keyed shadow tilemap (snes/ws_shadow.h): each frame every cell of the
 * strip is supplied keyed by its court column, and each scanline's world
 * origin is set from that line's own scroll (read from this frame's HDMA
 * table), so the perspective floor lines up. BG1 (the near stanchion) slides
 * through its own screens the same way, with one scroll per frame.
 *
 * The margins never go past the court's authored ends: there the bare side
 * is black, as the framework's Adaptive View does at a map boundary.
 *
 * With no margin (4:3) nothing here runs and the picture is the game's own,
 * bit for bit (tools/ws_baseline.py). Off the court -- menus, character
 * select, breaks, the stats screen -- the margins stay black. Nothing reads
 * or writes guest state beyond WRAM bytes and VRAM the game has written.
 *
 * Sprites in the margins -- players, and the hoop's rim, backboard and net
 * (only its pole is BG1) -- come from src/ltbb_oam_widen.c (P7/P8).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "beam_frame_driver.h"
#include "common_rtl.h"
#include "ltbb_widescreen.h"
#include "snes/dma.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "snes/ws_shadow.h"

extern Snes *g_snes;
extern int g_ws_extra;

/* $0107 is 1 exactly while the court is presented (docs/WIDESCREEN.md, P5). */
#define LTBB_COURT_FLAG 0x0107
#define LTBB_BG2HOFS 0x0F
#define LTBB_LAYER_BG1 0
#define LTBB_LAYER_BG2 1
#define LTBB_LAYER_BG3_MASK 0x04
/* BG2's strip: four screens from $7000. BG1's: three screens from $6400. */
#define LTBB_BG2_STRIP_BASE 0x7000
#define LTBB_BG2_STRIP_SCREENS 4
#define LTBB_BG1_STRIP_BASE 0x6400
#define LTBB_BG1_STRIP_SCREENS 3
/* Shadow rows to supply: screen row + vertical scroll spans < 512 px. */
#define LTBB_SHADOW_ROWS 64

/* Per-side margin for a 16:9 window from 4:3: 256 * (16/9) / (4/3) = 341.3. */
#define LTBB_WS_EXTRA_16_9 43

static int s_court_enabled = -1;
static bool s_shadow_active;
/* This frame's drawn margins, for LtbbWidescreenEndDraw. */
static int s_left, s_right;
/* True when the renderer composed this frame's bare sides itself. */
static bool s_hud_full;
/* This frame's BG2 world x per screen row, for the per-line observer. */
static int32_t s_bg2_row_world[224];

int LtbbWidescreenFrameWidth(int widescreen_enabled)
{
  int extra = PpuWsExtraOverride();
  if (extra < 0)
    extra = widescreen_enabled ? LTBB_WS_EXTRA_16_9 : 0;
  return 256 + 2 * extra;
}

/* A byte the game keeps in WRAM. Returns -1 for any other address, so the
 * caller falls back rather than touching the bus. */
static int WramByte(uint8_t bank, uint16_t addr)
{
  if (bank == 0x7E || bank == 0x7F)
    return g_ram[((uint32_t)(bank & 1) << 16) | addr];
  if ((bank & 0x7F) <= 0x3F && addr < 0x2000)
    return g_ram[addr];
  return -1;
}

static int WramWord(uint8_t bank, uint16_t addr)
{
  int lo = WramByte(bank, addr), hi = WramByte(bank, (uint16_t)(addr + 1));
  return lo < 0 || hi < 0 ? -1 : lo | hi << 8;
}

static int Signed10(int v) { v &= 0x3FF; return v >= 512 ? v - 1024 : v; }

/* BG2's horizontal scroll for each of the 224 screen rows, from the HDMA
 * table the render walk is about to run (P1: the values the PPU renders).
 * Follows the hardware walk: the first transfer lands on the first visible
 * line; a plain entry holds its value for its line count, a repeat entry
 * transfers every line. Returns false when no table can be read. */
static bool Bg2RowScroll(int rows[224])
{
  const Dma *dma = g_snes->dma;
  for (int i = 0; i < 8; i++) {
    const DmaChannel *ch = &dma->channel[i];
    if (!ch->hdmaActive || ch->bAdr != LTBB_BG2HOFS || (ch->mode & 7) != 2)
      continue;
    uint16_t ptr = ch->aAdr;
    int cur = g_ppu->hScroll[LTBB_LAYER_BG2], r = 0;
    while (r < 224) {
      int header = WramByte(ch->aBank, ptr++);
      if (header < 0)
        return false;
      if (header == 0)
        break;
      int count = header & 0x7F ? header & 0x7F : 128;
      bool repeat = (header & 0x80) != 0;
      uint8_t dbank = ch->aBank;
      uint16_t dptr = ptr;
      if (ch->indirect) {
        int p = WramWord(ch->aBank, ptr);
        if (p < 0)
          return false;
        ptr = (uint16_t)(ptr + 2);
        dbank = ch->indBank;
        dptr = (uint16_t)p;
      }
      for (int n = 0; n < count && r < 224; n++) {
        if (n == 0 || repeat) {
          int v = WramWord(dbank, dptr);
          if (v < 0)
            return false;
          dptr = (uint16_t)(dptr + 2);
          cur = v;
        }
        rows[r++] = cur;
      }
      if (!ch->indirect)
        ptr = dptr;
    }
    while (r < 224)
      rows[r++] = cur;
    return true;
  }
  return false;
}

/* Which screen of its strip a layer's two-screen map starts at, or -1. */
static int StripScreen(int layer, uint16_t strip_base, int screens)
{
  int base = PPU_bgTilemapAdr(g_ppu, layer);
  if (!PPU_bgTilemapWider(g_ppu, layer) || base < strip_base)
    return -1;
  int screen = (base - strip_base) / 0x400;
  return screen + 1 < screens ? screen : -1;
}

/* Supply every cell of a layer's strip to its shadow tilemap, keyed by court
 * column and by the wrapped screen row the renderer asks for. */
static void SupplyStrip(int layer, uint16_t strip_base, int screens)
{
  const uint16_t *vram = g_ppu->vram;
  for (int col = 0; col < screens * 32; col++) {
    const uint16_t *screen = vram + ((strip_base + (col >> 5) * 0x400) & 0x7FFF);
    for (int row = 0; row < LTBB_SHADOW_ROWS; row++)
      WsShadowForceTile(layer, (uint32_t)col, (uint32_t)row,
                        screen[(row & 31) * 32 + (col & 31)]);
  }
}

/* The court's authored ends in court pixels, from the strip row that holds
 * the top screen line: the first and last non-empty tile. */
static void CourtEnds(int *left_px, int *right_px)
{
  const uint16_t *vram = g_ppu->vram;
  int row = ((g_ppu->vScroll[LTBB_LAYER_BG2] + 1) >> 3) & 31;
  int first = -1, last = -1;
  for (int col = 0; col < LTBB_BG2_STRIP_SCREENS * 32; col++) {
    uint16_t e = vram[(LTBB_BG2_STRIP_BASE + (col >> 5) * 0x400 + row * 32 + (col & 31)) & 0x7FFF];
    if (e) {
      if (first < 0)
        first = col;
      last = col;
    }
  }
  *left_px = first < 0 ? 0 : first * 8;
  *right_px = last < 0 ? LTBB_BG2_STRIP_SCREENS * 256 : (last + 1) * 8;
}

static void Bg2LineObserver(const struct Ppu *ppu, unsigned line, void *context)
{
  (void)ppu; (void)context;
  /* `line` (1-based) was just drawn; set the origin for the next one. */
  if (line < 224)
    WsShadowSetWorld(LTBB_LAYER_BG2, (uint32_t)s_bg2_row_world[line], 0);
}

/* Deactivate both shadow layers (the framework activates what was
 * registered since its last frame call, so it takes two). */
static void ShadowOff(void)
{
  if (!s_shadow_active)
    return;
  WsShadowFrame(g_ppu);
  WsShadowFrame(g_ppu);
  s_shadow_active = false;
}

/* Returns true when the frame should be drawn with the per-line observer. */
static bool CourtMargins(int extra, int *left, int *right)
{
  int bg2_screen = StripScreen(LTBB_LAYER_BG2, LTBB_BG2_STRIP_BASE, LTBB_BG2_STRIP_SCREENS);
  int rows[224];
  if (bg2_screen < 0 || !Bg2RowScroll(rows))
    return false;
  for (int r = 0; r < 224; r++)
    s_bg2_row_world[r] = bg2_screen * 256 + Signed10(rows[r]);

  WsShadowSetWorld(LTBB_LAYER_BG2, (uint32_t)s_bg2_row_world[0], 0);
  int bg1_screen = StripScreen(LTBB_LAYER_BG1, LTBB_BG1_STRIP_BASE, LTBB_BG1_STRIP_SCREENS);
  if (bg1_screen >= 0)
    WsShadowSetWorld(LTBB_LAYER_BG1,
                     (uint32_t)(bg1_screen * 256 + Signed10(g_ppu->hScroll[LTBB_LAYER_BG1])), 0);
  WsShadowFrame(g_ppu);
  SupplyStrip(LTBB_LAYER_BG2, LTBB_BG2_STRIP_BASE, LTBB_BG2_STRIP_SCREENS);
  if (bg1_screen >= 0)
    SupplyStrip(LTBB_LAYER_BG1, LTBB_BG1_STRIP_BASE, LTBB_BG1_STRIP_SCREENS);
  s_shadow_active = true;

  int court_left, court_right;
  CourtEnds(&court_left, &court_right);
  int x = s_bg2_row_world[0];
  int room_left = x - court_left, room_right = court_right - (x + 256);
  *left = room_left < 0 ? 0 : (room_left < extra ? room_left : extra);
  *right = room_right < 0 ? 0 : (room_right < extra ? room_right : extra);
  return true;
}

/* The bottom HUD -- each team's two portraits (sprites, the first 16 OAM
 * slots on the court, which the game emits first and caches) and its money
 * and power-name text (BG3, the bottom lines) -- moves out to the frame
 * edges in widescreen court play: the left team's group left, the right
 * team's right. It moves by the full margin, not the live one, so it stays
 * put when the court ends and a side goes black (HUD-always-visible: the
 * renderer blacks the bare side itself and keeps the HUD's pixels there).
 * BG3 above the HUD (centred captions such as "TEAM 1 WINS!") keeps its
 * native 256 columns, as BG3 does in every other scene. BG3 is 2bpp here;
 * patches/snesrecomp-anchor-band-2bpp.patch makes the anchor band apply to
 * it. */
#define LTBB_HUD_TOP 176            /* first screen line of the bottom HUD */
#define LTBB_HUD_OAM_SLOTS 16       /* the four 32x32 portraits */
#define LTBB_HUD_LEFT_END 120       /* x < this: left team's group */
#define LTBB_HUD_RIGHT_START 136    /* x >= this: right team's group */

static void HudPlacement(bool court)
{
  if (court) {
    PpuSetWidescreenLayerClamp(g_ppu, 0);
    PpuSetWidescreenLayerClampBand(g_ppu, 2, 0, LTBB_HUD_TOP);
    PpuSetWidescreenLayerAnchorBand(g_ppu, 2, LTBB_HUD_TOP, 224,
                                    LTBB_HUD_LEFT_END, LTBB_HUD_RIGHT_START);
    PpuSetWsHudOamBand(g_ppu, 224, LTBB_HUD_LEFT_END, LTBB_HUD_RIGHT_START);
    PpuSetWsHudOamShiftRange(g_ppu, 0, LTBB_HUD_OAM_SLOTS);
    PpuSetWidescreenHudAlwaysVisible(g_ppu, true);
  } else {
    PpuSetWidescreenLayerClamp(g_ppu, LTBB_LAYER_BG3_MASK);
    PpuSetWidescreenLayerClampBand(g_ppu, 2, 0, 0);
    PpuSetWidescreenLayerAnchorBand(g_ppu, 2, 0, 0, 0, 0);
    PpuSetWsHudOamBand(g_ppu, 0, 0, 0);
    PpuSetWsHudOamShiftRange(g_ppu, 0, 0);
    PpuSetWidescreenHudAlwaysVisible(g_ppu, false);
  }
}

bool LtbbWidescreenCourtActive(void)
{
  if (g_ws_extra <= 0)
    return false;
  if (s_court_enabled < 0) {
    const char *e = getenv("SNESRECOMP_WS_COURT");   /* P14 kill switch */
    s_court_enabled = !(e && e[0] == '0');
  }
  return s_court_enabled && g_ram[LTBB_COURT_FLAG] == 1;
}

bool LtbbWidescreenBeginDraw(void)
{
  const int extra = g_ws_extra;
  if (extra <= 0) {
    ShadowOff();
    return false;   /* 4:3: leave the PPU exactly as the host set it */
  }

  if (s_court_enabled < 0) {
    const char *e = getenv("SNESRECOMP_WS_COURT");   /* P14 kill switch */
    s_court_enabled = !(e && e[0] == '0');
  }

  int left = 0, right = 0;
  bool observed = false;
  if (s_court_enabled && g_ram[LTBB_COURT_FLAG] == 1)
    observed = CourtMargins(extra, &left, &right);
  if (!observed)
    ShadowOff();
  HudPlacement(observed);
  s_hud_full = observed;
  PpuSetExtraSideSpace(g_ppu, left, right, 0);
  s_left = left;
  s_right = right;
  return observed;
}

void LtbbWidescreenDraw(void)
{
  if (LtbbWidescreenBeginDraw())
    snes_beam_frame_driver_draw_ppu_frame_observed(Bg2LineObserver, NULL);
  else
    snes_beam_frame_driver_draw_ppu_frame();
  LtbbWidescreenEndDraw();
}

/* The renderer leaves margin columns it was told not to draw untouched, so
 * they still hold whatever an earlier frame drew there (the framework leaves
 * pillarboxing to the caller). Clear them, so a bare side is black rather
 * than a stale copy of earlier court. On the court the renderer composes the
 * full width itself (HudPlacement), black where the court has ended except
 * for the HUD, so there is nothing to clear. */
void LtbbWidescreenEndDraw(void)
{
  const int extra = g_ws_extra;
  if (extra <= 0 || !g_ppu->renderBuffer || s_hud_full)
    return;
  const int width = 256 + 2 * extra;
  const int lpad = extra - s_left, rpad = extra - s_right;
  if (lpad <= 0 && rpad <= 0)
    return;
  for (int y = 0; y < 224; y++) {
    uint8_t *row = g_ppu->renderBuffer + (size_t)y * g_ppu->renderPitch;
    if (lpad > 0)
      memset(row, 0, (size_t)lpad * 4);
    if (rpad > 0)
      memset(row + (size_t)(width - rpad) * 4, 0, (size_t)rpad * 4);
  }
}
