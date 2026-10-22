/*
 * Looney Tunes B-Ball -- sprites in the widescreen margins (WIDESCREEN_PATTERNS
 * P7/P8), presentation only.
 *
 * The game builds its OAM shadow ($1200 low table, $1400 high table) once a
 * frame in the routine at $80:C3C0. Every object gets an object-level cull
 * against the native screen (screen x + half-width vs 256, x + left extent vs
 * 0; an off-screen object gets bit $0200 of $04F4,X and no sprites), then
 * every piece of its metasprite at $7E:2500+ a piece-level cull (x in
 * [-15, 256) for 16x16 pieces, [-7, 256) for 8x8, y in the same band above
 * 224). Its x >= 0 path never sets the 9th X bit, so no limit change alone
 * could draw a piece past x 256. So in widescreen a player or the hoop
 * vanishes at the native edge while the court continues into the margin.
 *
 * The routine is replaced (recomp/bank00.cfg, hle_func $C3C0) by a helper
 * that runs the original: it does the routine's first instruction itself
 * (STZ $1400), charges exactly what the compiled block charges for it, and
 * hands the caller's own JSL frame to the compiled body declared at $C3C3,
 * whose RTL consumes it. Game state, CPU time and the stack are therefore
 * the original's in every mode, 4:3 and widescreen alike.
 *
 * Only during widescreen court play, after the original returns, a read-only
 * pass replays the original's decisions to find where its sprites ended,
 * then appends the pieces it culled at the native edge that fall inside the
 * margins, into the free OAM slots after them. It changes no game state
 * other than those OAM-shadow slots (and their high-table bits), charges no
 * CPU time, and hides the extras it wrote last frame that the game's own
 * high-water mark ($0106) would not hide. Margin sprites therefore take the
 * lowest sprite priority and are dropped when all 128 slots are in use.
 *
 * The same pass adapts the game's off-screen arrows. Objects $18/$1A/$1C/$1E
 * are the arrows for players 0/2/4/6: the game shows one at the native edge
 * while its player is culled there. In widescreen a player in a margin is
 * drawn, so its arrow is hidden; a player past the margin too keeps its
 * arrow, moved out by the margin to the widened edge on the same side. Only
 * the arrow's own OAM-shadow slots change; the cull flag the game logic
 * reads ($04F4 bit $0200) is left as the original set it.
 *
 * SNESRECOMP_WS_OAM=0 turns the pass off (P14).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common_rtl.h"
#include "cpu_state.h"
#include "ltbb_widescreen.h"

extern int g_ws_extra;

#define LTBB_OAM_LOW 0x1200
#define LTBB_OAM_HIGH 0x1400
#define LTBB_OAM_END 0x0200        /* 128 slots * 4 bytes */
#define LTBB_OAM_HIDDEN_XY 0xE800  /* x 0, y 232: what the game parks slots at */

/* A piece the original culled at the native edge that lies in a margin. */
typedef struct {
  int16_t x, y;
  uint16_t attr;
  bool large;
} LtbbExtra;

/* The OAM-shadow slots one arrow object filled, [first, end). */
typedef struct {
  uint16_t first, end;
} LtbbSpan;

#define LTBB_ARROW_OBJ 0x18        /* arrow for player p is object $18 + p */
#define LTBB_PLAYERS 4

static int s_enabled = -1;
static uint16_t s_extra_end;   /* first slot byte past last frame's extras */

static uint16_t Rd(CpuState *cpu, uint8_t bank, uint16_t addr) { return cpu_read16(cpu, bank, addr); }
static uint16_t Wram7E(CpuState *cpu, uint16_t addr) { return cpu_read16(cpu, 0x7E, addr); }
static bool Neg(uint16_t v) { return (v & 0x8000) != 0; }

/* Replays the original's emission for the state it just ran on. Returns the
 * byte offset just past its last OAM slot; with `extras` it also collects
 * the margin pieces (up to `cap`) and the slots each arrow object filled.
 * `db5` is $0DB5 as it was on entry. */
static uint16_t Replay(CpuState *cpu, uint8_t db, uint16_t d, uint16_t db5,
                       int margin, LtbbExtra *extras, int cap, int *count,
                       LtbbSpan *arrows)
{
  uint16_t y = db5;
  int k = db5 ? 0x1E : 0x26;
  *count = 0;
  const uint16_t cam_x = Rd(cpu, 0x00, (uint16_t)(d + 0x38));
  const uint16_t cam_y = Rd(cpu, 0x00, (uint16_t)(d + 0x3A));
  const uint16_t dp40 = Rd(cpu, 0x00, (uint16_t)(d + 0x40));

  for (; k >= 0; k -= 2) {
    const uint16_t obj = Rd(cpu, db, (uint16_t)(0x000A + k));
    const uint16_t flags = Rd(cpu, db, (uint16_t)(0x04F4 + obj));
    if (flags & 0x0010)
      continue;
    uint16_t x84, base_y;
    bool native_obj = true;
    bool camera_y = true;
    bool c454 = false;
    if (obj < 8) {
      if ((flags & 0x2000) && Rd(cpu, db, 0x0CA3) != 1)
        continue;
      c454 = true;
    } else if (obj < Rd(cpu, db, 0x1151)) {
      c454 = true;
    } else {
      x84 = Rd(cpu, db, (uint16_t)(0x010A + obj));
      /* $C445: objects $18-$1F are camera-relative in y, the rest screen-space. */
      camera_y = obj < 0x20 && obj >= 0x18;
    }
    if (c454) {
      uint16_t t = (uint16_t)(Rd(cpu, db, (uint16_t)(0x024C + obj)) + dp40);
      x84 = (uint16_t)(Rd(cpu, db, (uint16_t)(0x015C + obj)) - Rd(cpu, db, t) - cam_x);
      uint16_t right = (uint16_t)(x84 + Rd(cpu, db, (uint16_t)(0x0544 + obj)));
      uint16_t left = (uint16_t)(x84 + Rd(cpu, db, (uint16_t)(0x056C + obj)));
      native_obj = Neg((uint16_t)(right - 0x0100)) && !Neg(left);
      if (!native_obj) {
        if (!extras)
          continue;
        /* The same tests against the widened screen. */
        if (!Neg((uint16_t)(right - (0x0100 + margin))) || (int16_t)left < -margin)
          continue;
      }
    }
    uint16_t a = camera_y ? (uint16_t)(0 - cam_y) : 0;
    base_y = (uint16_t)(a + Rd(cpu, db, (uint16_t)(0x01AC + obj)));
    uint16_t y86 = (uint16_t)(base_y - Rd(cpu, db, (uint16_t)(0x02C4 + obj)));
    uint16_t attr7e = Rd(cpu, db, (uint16_t)(0x04CC + obj)) | Rd(cpu, db, (uint16_t)(0x0454 + obj));
    uint16_t ptr = (uint16_t)(Rd(cpu, db, (uint16_t)(0xE9C7 + obj)) + Rd(cpu, db, (uint16_t)(0x0A08 + obj)));
    uint16_t pieces = Wram7E(cpu, (uint16_t)(0x2500 + ptr));
    if (!pieces)
      continue;
    ptr = (uint16_t)(ptr + 2);
    const bool arrow = obj >= LTBB_ARROW_OBJ && obj < LTBB_ARROW_OBJ + 2 * LTBB_PLAYERS;
    if (arrow && arrows)
      arrows[(obj - LTBB_ARROW_OBJ) >> 1].first = y;
    for (;;) {
      uint16_t attr = Wram7E(cpu, (uint16_t)(0x2500 + ptr));
      bool large = !(attr & 0x2000);
      if (!large && attr == 0xFFFF) {
        /* $C5E4: switch the rest of the list to the object's base y and
         * palette $0C00; not a piece, so the count is not decremented. */
        y86 = base_y;
        attr7e = (uint16_t)((attr7e & 0xF1FF) | 0x0C00);
        ptr = (uint16_t)(ptr + 2);
        continue;
      }
      uint16_t oam_attr = large ? (uint16_t)(attr | attr7e) : (uint16_t)((attr & 0xDFFF) | attr7e);
      const uint16_t edge = large ? 0xFFF1 : 0xFFF9;   /* -15 / -7 */
      uint16_t px = (uint16_t)(Wram7E(cpu, (uint16_t)(0x2502 + ptr)) + x84);
      uint16_t py = (uint16_t)(Wram7E(cpu, (uint16_t)(0x2504 + ptr)) + y86);
      bool x_native = Neg(px) ? !Neg((uint16_t)(px - edge)) : px < 0x0100;
      bool y_ok = !Neg((uint16_t)(py - edge)) && Neg((uint16_t)(py - 0x00E0));
      if (native_obj && x_native && y_ok) {
        y = (uint16_t)(y + 4);
        if (arrow && arrows)
          arrows[(obj - LTBB_ARROW_OBJ) >> 1].end = y;
        if (y >= LTBB_OAM_END)
          return y;
      } else if (extras && !arrow && y_ok && *count < cap) {
        int sx = (int16_t)px, size = large ? 16 : 8;
        if (sx + size > -margin && sx < 256 + margin) {
          extras[*count].x = (int16_t)sx;
          extras[*count].y = (int16_t)py;
          extras[*count].attr = oam_attr;
          extras[*count].large = large;
          (*count)++;
        }
      }
      if (--pieces == 0)
        break;
      ptr = (uint16_t)(ptr + 6);
    }
  }
  return y;
}

/* True when player `p` (object 0/2/4/6) is drawn on the widened screen: the
 * original's object cull ($C454) against 256 + margin and -margin. */
static bool PlayerInWidened(CpuState *cpu, uint8_t db, uint16_t d, uint16_t p, int margin)
{
  if (Rd(cpu, db, (uint16_t)(0x04F4 + p)) & 0x0010)
    return false;
  const uint16_t cam_x = Rd(cpu, 0x00, (uint16_t)(d + 0x38));
  const uint16_t dp40 = Rd(cpu, 0x00, (uint16_t)(d + 0x40));
  uint16_t t = (uint16_t)(Rd(cpu, db, (uint16_t)(0x024C + p)) + dp40);
  uint16_t x84 = (uint16_t)(Rd(cpu, db, (uint16_t)(0x015C + p)) - Rd(cpu, db, t) - cam_x);
  int right = (int16_t)(uint16_t)(x84 + Rd(cpu, db, (uint16_t)(0x0544 + p)));
  int left = (int16_t)(uint16_t)(x84 + Rd(cpu, db, (uint16_t)(0x056C + p)));
  return right < 256 + margin && left >= -margin;
}

/* Writes slot `s`'s x (9 bits, the high-table bit kept with its size bit). */
static void SetSlotX(CpuState *cpu, uint16_t s, int x)
{
  cpu_write8(cpu, 0x7E, (uint16_t)(LTBB_OAM_LOW + s), (uint8_t)(x & 0xFF));
  const unsigned slot = s >> 2;
  const uint16_t word = (uint16_t)(LTBB_OAM_HIGH + ((slot >> 2) & ~1u));
  const unsigned shift = (slot & 3) * 2 + ((slot >> 2) & 1) * 8;
  uint16_t hw = cpu_read16(cpu, 0x7E, word);
  hw = (uint16_t)((hw & ~(1u << shift)) | ((x < 0 || x >= 256 ? 1u : 0u) << shift));
  cpu_write16(cpu, 0x7E, word, hw);
}

/* Reads slot `s`'s x as the original wrote it (9 bits, signed). */
static int SlotX(CpuState *cpu, uint16_t s)
{
  const unsigned slot = s >> 2;
  const uint16_t word = (uint16_t)(LTBB_OAM_HIGH + ((slot >> 2) & ~1u));
  const unsigned shift = (slot & 3) * 2 + ((slot >> 2) & 1) * 8;
  int x = cpu_read8(cpu, 0x7E, (uint16_t)(LTBB_OAM_LOW + s));
  return (cpu_read16(cpu, 0x7E, word) >> shift) & 1 ? x - 256 : x;
}

static void HideSlots(CpuState *cpu, uint16_t from, uint16_t to)
{
  for (uint16_t s = from; s < to && s < LTBB_OAM_END; s = (uint16_t)(s + 4)) {
    cpu_write16(cpu, 0x7E, (uint16_t)(LTBB_OAM_LOW + s), LTBB_OAM_HIDDEN_XY);
    cpu_write16(cpu, 0x7E, (uint16_t)(LTBB_OAM_LOW + s + 2), 0);
  }
}

/* Hide last frame's extras that the game's own clear will not reach: it hides
 * [end of its sprites, $0106) and leaves everything past $0106 alone. */
static void HideOldExtras(CpuState *cpu, uint16_t from)
{
  uint16_t game_hw = cpu_read16(cpu, 0x7E, 0x0106);
  if (s_extra_end > game_hw)
    HideSlots(cpu, from > game_hw ? from : game_hw, s_extra_end);
  s_extra_end = 0;
}

/* An arrow whose player is drawn in a margin is hidden; otherwise it moves
 * out by the margin, keeping the side the original put it on. */
static void AdaptArrows(CpuState *cpu, uint8_t db, uint16_t d, const LtbbSpan *arrows, int margin)
{
  for (uint16_t i = 0; i < LTBB_PLAYERS; i++) {
    const LtbbSpan *a = &arrows[i];
    if (a->end <= a->first)
      continue;
    if (PlayerInWidened(cpu, db, d, (uint16_t)(2 * i), margin)) {
      HideSlots(cpu, a->first, a->end);
      continue;
    }
    const int shift = SlotX(cpu, a->first) < 128 ? -margin : margin;
    for (uint16_t s = a->first; s < a->end; s = (uint16_t)(s + 4))
      SetSlotX(cpu, s, SlotX(cpu, s) + shift);
  }
}

static void AfterEmit(CpuState *cpu, uint8_t db, uint16_t d, uint16_t db5)
{
  if (s_enabled < 0) {
    const char *e = getenv("SNESRECOMP_WS_OAM");
    s_enabled = !(e && e[0] == '0');
  }
  if (!s_enabled || !LtbbWidescreenCourtActive()) {
    if (s_extra_end)
      HideOldExtras(cpu, 0);
    return;
  }
  LtbbExtra extras[128];
  LtbbSpan arrows[LTBB_PLAYERS] = {{0, 0}};
  int n = 0;
  uint16_t y = Replay(cpu, db, d, db5, g_ws_extra, extras, 128, &n, arrows);
  HideOldExtras(cpu, y);
  AdaptArrows(cpu, db, d, arrows, g_ws_extra);
  for (int i = 0; i < n && y < LTBB_OAM_END; i++, y = (uint16_t)(y + 4)) {
    const LtbbExtra *e = &extras[i];
    cpu_write16(cpu, 0x7E, (uint16_t)(LTBB_OAM_LOW + y),
                (uint16_t)(((uint16_t)e->y & 0xFF) << 8 | ((uint16_t)e->x & 0xFF)));
    cpu_write16(cpu, 0x7E, (uint16_t)(LTBB_OAM_LOW + y + 2), e->attr);
    /* High table: 2 bits per slot (x bit 8, size), 8 slots per word. */
    const unsigned slot = y >> 2;
    const uint16_t word = (uint16_t)(LTBB_OAM_HIGH + ((slot >> 2) & ~1u));
    const unsigned shift = (slot & 3) * 2 + ((slot >> 2) & 1) * 8;
    const unsigned bits = (e->x < 0 || e->x >= 256 ? 1u : 0u) | (e->large ? 2u : 0u);
    uint16_t hw = cpu_read16(cpu, 0x7E, word);
    cpu_write16(cpu, 0x7E, word, (uint16_t)((hw & ~(3u << shift)) | (bits << shift)));
  }
  s_extra_end = y;
}

/* hle_func $C3C0 (recomp/bank00.cfg). */
/* The bottom HUD's portraits are the first slots the emitter writes, and
 * $0DB5 holds the slot byte just past them. That is 16 slots only for some
 * expressions: a portrait that animates to one piece fewer leaves $0DB5 at
 * $3C, and the next slot holds a game sprite. The HUD anchor must shift only
 * the portrait slots, and only those of the sprite table the PPU is drawing,
 * which is the one the game copied at the last vblank -- one emission behind
 * the latest. So each emission records its portrait count and slots, and the
 * draw picks the record whose slots the PPU's OAM holds. */
#define LTBB_HUD_MAX_SLOTS 16

typedef struct {
  uint8_t n;
  uint16_t words[2 * LTBB_HUD_MAX_SLOTS];
} LtbbHudRecord;

static LtbbHudRecord s_hud[2];
static int s_hud_last;

static void RecordHud(CpuState *cpu, uint8_t db)
{
  int n = cpu_read16(cpu, db, 0x0DB5) / 4;
  LtbbHudRecord *h = &s_hud[s_hud_last ^= 1];
  h->n = (uint8_t)(n > LTBB_HUD_MAX_SLOTS ? LTBB_HUD_MAX_SLOTS : n);
  for (int i = 0; i < 2 * h->n; i++)
    h->words[i] = cpu_read16(cpu, 0x7E, (uint16_t)(LTBB_OAM_LOW + 2 * i));
}

int LtbbHudPortraitSlots(const uint16_t *oam)
{
  const LtbbHudRecord *a = &s_hud[0], *b = &s_hud[1];
  if (a->n < b->n) {
    const LtbbHudRecord *t = a; a = b; b = t;
  }
  /* Longer first: a shorter record's slots are a prefix of a longer one's. */
  if (a->n && !memcmp(oam, a->words, (size_t)a->n * 4))
    return a->n;
  if (b->n && !memcmp(oam, b->words, (size_t)b->n * 4))
    return b->n;
  return b->n;
}

RecompReturn HleLtbbOamEmit(CpuState *cpu)
{
  const uint8_t db = cpu->DB;
  const uint16_t d = cpu->D;
  const uint16_t db5 = cpu_read16(cpu, db, 0x0DB5);
  const uint32_t bank = (cpu->PB & 0x80) ? 0x800000u : 0;

  /* $C3C0: STZ $1400 (16-bit, 5 cycles), charged as the compiled block
   * charges it; the compiled body from $C3C3 charges the rest. */
  cpu_write16(cpu, db, LTBB_OAM_HIGH, 0);
  cpu->cycles += 5;
  cpu->master_cycles += 5 * ((g_memsel && (cpu->PB & 0x80)) ? 6 : 8);

  /* The caller's JSL frame is already on the stack; the body's RTL pops it. */
  RecompReturn r = cpu_dispatch_call_pc_pushed(cpu, bank | 0xC3C3, bank | 0xC3C0, 3, NULL);
  if (r == RECOMP_RETURN_NORMAL) {
    RecordHud(cpu, db);
    AfterEmit(cpu, db, d, db5);
  }
  return r;
}
