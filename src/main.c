/*
 * Looney Tunes B-Ball — desktop host shim.
 *
 * Identity and hooks, nothing else. The host itself is the framework's
 * (snesrecomp/runner/src/desktop/host_main.h): the pre-boot launcher, ROM
 * resolution and digest checks, config.ini and keybinds.ini, the window and
 * the SDL / OpenGL presenters, audio, gamepads, the in-game save-state
 * browser and rewind filmstrip, the OSD, the pacing clock, crash handlers and
 * the post-mortem report, mod packages, Generate & rebuild, and the netplay
 * barrier when the project is built with it. A fix there reaches this
 * project on a submodule pull; nothing in this file needs to change for it.
 *
 * This file used to BE the host -- a ~700-line copy made at scaffold time --
 * so every project scaffolded before a fix kept the bug. Two ports of the
 * same game, a week apart, had different hosts.
 *
 * Nothing here ever ships a ROM. The player supplies one; the host makes
 * that easy (launcher, then positional argument, then a copy beside the
 * executable, then the rom.cfg cache, then a file picker) and checks what it
 * is handed against the digests in rom_identity.txt.
 *
 * Grow this file only with what is specific to THIS title: a custom
 * presenter (prepare_frame / draw_frame), an SPC player, a pacing rule
 * (keep_pacing_debt), a Mods provider. See SnesDesktopHostGame for the
 * complete list of hooks and what each is for.
 */

#include "host_main.h"
#include "game_rtl.h"
#include "ltbb_widescreen.h"
#include "desktop/config.h"
#include "desktop/display_aspect.h"
#include "snesrecomp_rom_identity.h"  /* generated from rom_identity.txt */

#ifndef __ANDROID__
#define SDL_MAIN_HANDLED 1
#endif
#include "desktop/sdl_compat.h"
#ifdef __ANDROID__
/* SDLActivity loads libmain.so and dlsyms its SDL_main. */
#define main SDL_main
#endif

#ifndef SNES_GAME_VERSION
#define SNES_GAME_VERSION "dev"
#endif

/* The launcher's "Aspect ratio" row: 16:9 widescreen (the default) or the
 * original 4:3. The host stores the row's index as [Graphics] DisplayAspect
 * and names indices after its own SNES pixel aspects, so in config.ini
 * "DisplayAspect = 4:3" (index 0) means widescreen and "8:7" (index 1) means
 * the original picture. The Widescreen key mirrors the choice for reading.
 * The picture always keeps the authentic 4:3 CRT pixel aspect
 * (LtbbComputeViewport). SNESRECOMP_WS_EXTRA pins the margin for tests. */
static const char *const kLtbbAspectLabels[] = { "16:9 widescreen", "4:3" };

static bool LtbbWidescreenWanted(void)
{
  return g_config.display_aspect == 0;
}

/* The court's margins are decided per frame in src/ltbb_widescreen.c. */
static void LtbbPrepareFrame(int drawable_w, int drawable_h, int *frame_w, int *frame_h)
{
  (void)drawable_w; (void)drawable_h;
  g_config.widescreen = LtbbWidescreenWanted();
  *frame_w = LtbbWidescreenFrameWidth(g_config.widescreen);
  *frame_h = 224;
}

/* Window width at scale 1, for the frame the aspect choice will produce. The
 * host creates the window before the first frame is prepared, when its frame
 * width is still 256, so sizing from that left a 4:3 window that letterboxed
 * the 342-px frame. Same 4:3-on-240 rule as the host default (256 -> 320,
 * 342 -> 428, i.e. 16:9). */
static int LtbbWindowBaseWidth(int frame_w)
{
  (void)frame_w;
  return (LtbbWidescreenFrameWidth(LtbbWidescreenWanted()) * 5 + 2) / 4;
}

/* The authentic 4:3 CRT pixel aspect for both choices; without this the host
 * would read index 1 of the aspect row as its 8:7 square-pixel choice. */
static void LtbbComputeViewport(int frame_w, int frame_h, int drawable_w,
                                int drawable_h, SnesDisplayViewport *viewport)
{
  SnesDisplayAspect_ComputeViewport(frame_w, frame_h, drawable_w, drawable_h,
                                    kSnesDisplayAspect_Crt4x3,
                                    g_config.ignore_aspect_ratio, false, viewport);
}

static const SnesDesktopHostGame kGameHost = {
    .display_name        = "Looney Tunes B-Ball",
    .window_title        = "Looney Tunes B-Ball",
    .region              = SNESRECOMP_ROM_REGION,
    .rom_file            = SNESRECOMP_ROM_FILE,
    .expected_sha256_hex = SNESRECOMP_ROM_EXPECTED_SHA256,
    .expected_crc32_hex  = SNESRECOMP_ROM_EXPECTED_CRC32,
    .game_id             = SNESRECOMP_ROM_GAME_ID,
    .build_version       = SNES_GAME_VERSION,
    .game_info           = &kGameInfo,
    .widescreen_supported = 1,
    .native_widescreen   = 1,
    .prepare_frame       = LtbbPrepareFrame,
    .window_base_width   = LtbbWindowBaseWidth,
    .compute_viewport    = LtbbComputeViewport,
    .display_aspect_supported = 1,
    .aspect_labels       = kLtbbAspectLabels,
    .num_aspect_labels   = 2,
    .aspect_setting_label = "Aspect ratio",
    .aspect_setting_help = "16:9 widens the court; menus and other screens keep their 4:3 picture. 4:3 is the original picture.",
    .num_players         = 2,
    /* Battery-backed SRAM shows the launcher's SAVES panel. Leave NULL for a
     * title without one. The path is exe-relative. */
    .sram_path           = "saves/save.srm",
};

#ifndef __ANDROID__
#undef main   /* desktop: keep plain main() even if SDL_main.h remapped it */
#endif
int main(int argc, char **argv)
{
    return snesrecomp_desktop_main(&kGameHost, argc, argv);
}
