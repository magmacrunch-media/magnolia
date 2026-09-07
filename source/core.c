#include <fat.h>
#include <ogc/system.h>     /* SYS_STDIO_Report */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "core.h"
#include "renderer.h"
#include "scoring.h"
#include "prefs.h"
#include "ui_utils.h"
#include "text.h"

static int  sd_mounted = 0;
static char app_name[64]     = "magnolia";
static char scores_path[160];
static char prefs_path[160];
static char asset_buf[192];

int magnolia_init(const MagnoliaConfig *cfg) {
    int status = 0;

    /* Route stdout and stderr to the OSReport UART, which is what makes printf
       visible in Dolphin's log. Without this call libogc leaves stdout attached
       to nothing at all, so every printf in a game built on this engine is
       silently discarded -- and template/README.md has been telling people to
       enable OSREPORT in Logger.ini and read the output, which produced a
       0-byte log however Logger.ini was set. The documentation was describing
       this line; it just was not here.

       First, before renderer_init(), so that a printf tracing a failure in
       bringing up video is not itself lost to the failure it is reporting.

       Costs nothing where nothing is listening: on a real console with no USB
       Gecko in the slot the writes go to an absent EXI device and are
       discarded, which is the same place they went before. Games should still
       keep printf out of per-frame code -- an EXI write is not free. */
    SYS_STDIO_Report(true);

    if (cfg && cfg->app_name) {
        snprintf(app_name, sizeof(app_name), "%s", cfg->app_name);
    }

    /* Video comes up first, before anything slow. fatInitDefault() talks to IOS
       and can take seconds or wedge outright; doing it before GRRLIB_Init()
       leaves the video interface unconfigured, so a stall shows as a blank or
       garbage screen with no way to tell where it stopped. */
    int rc = renderer_init();
    if (rc == -1) return -2;          /* no video: nothing further is useful */
    if (rc < 0) status = -3;          /* font missing, but we can still draw */

    if (cfg && cfg->overscan_pct >= 0) {
        ui_set_overscan_pct(cfg->overscan_pct);
    }

    /* The glyph cache, straight after the font it caches and before anything
       draws through ui_utils. Its return code is deliberately not folded into
       `status`: every text call works whether or not the cache came up, so a
       game has nothing to decide here and a degraded return would only make
       callers handle a case that does not exist. What it costs when it is off
       is speed, and text.h explains how much. */
    text_init();

    /* Prove the display is live, then name the step that might hang. */
    renderer_splash("STARTING", NULL);
    renderer_splash("MOUNTING SD CARD", "reading sprites and audio");

    sd_mounted = fatInitDefault() ? 1 : 0;
    if (!sd_mounted) status = -1;

    /* Create the app directory before anything tries to persist into it.
       fopen(..., "w") fails outright when the parent directory is missing, and
       libfat will not create one implicitly -- so without this, every score and
       preference write fails silently on any card where the folder is not
       already there. On a real console it usually is, because installing the app
       created it, which is exactly why this went unnoticed: the save path only
       breaks on the cards nobody installs to, which includes every fresh
       emulator card. Both calls are harmless when the directory exists. */
    if (sd_mounted) {
        char dir[176];
        mkdir("sd:/apps", 0777);
        snprintf(dir, sizeof(dir), "sd:/apps/%s", app_name);
        mkdir(dir, 0777);
    }

    int max = (cfg && cfg->max_scores > 0) ? cfg->max_scores : MAGNOLIA_MAX_SCORES;
    snprintf(scores_path, sizeof(scores_path), "sd:/apps/%s/scores.json", app_name);
    scoring_init(scores_path, max);

    snprintf(prefs_path, sizeof(prefs_path), "sd:/apps/%s/settings.json", app_name);
    prefs_init(prefs_path);

    return status;
}

void magnolia_shutdown(void) {
    /* Torn down in the reverse of the order it came up: the card first, then
       video. magnolia_init() brings video up before mounting so that a slow
       mount has a screen to report itself on, and the same reasoning run
       backwards puts the unmount before GRRLIB_Exit().

       fatInitDefault() mounts every device it finds, but every path this engine
       writes is under sd:/apps/, so that is the volume whose cache has anything
       in it worth flushing. Guarded on sd_mounted because unmounting a volume
       that never mounted is not a no-op worth relying on, and cleared after so
       magnolia_sd_mounted() does not go on claiming a card that has been let
       go of. */
    if (sd_mounted) {
        fatUnmount("sd:");
        sd_mounted = 0;
    }

    /* Before renderer_shutdown(), which frees the font these were rasterised
       from -- and before GRRLIB_Exit(), which is what actually owns the
       textures being handed back. */
    text_shutdown();

    renderer_shutdown();
}

int magnolia_sd_mounted(void)   { return sd_mounted; }
int magnolia_fonts_loaded(void) { return renderer_fonts_loaded(); }

const char *magnolia_asset_path(const char *leaf) {
    snprintf(asset_buf, sizeof(asset_buf), "sd:/apps/%s/%s", app_name, leaf ? leaf : "");
    return asset_buf;
}
