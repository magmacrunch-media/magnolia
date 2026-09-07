/* textbench -- what is actually slow about drawing text on magnolia?
 *
 * Scratch measurement, not a test. The question it answers: does
 * GRRLIB_PrintfTTF cost scale with the number of GLYPHS drawn, or with their
 * PIXEL AREA? The two imply different fixes -- batching versus caching -- and
 * guessing between them is how an afternoon gets spent on the wrong one.
 *
 * Times each case with gettime() around a fixed repeat count, and prints
 * microseconds per call to the Dolphin log. Needs OSREPORT and WriteToFile in
 * Logger.ini.
 */
#include <stdio.h>
#include <string.h>

#include <ogc/lwp_watchdog.h>

#include <magnolia.h>
#include "text.h"

#define REPS 40

static u64 time_us(void (*fn)(void), int reps) {
    u64 start, end;
    int i;
    /* One untimed pass: the first call of anything here pays for FreeType
       setting a pixel size and for whatever GX state it touches, and that is
       not what is being measured. */
    fn();
    start = gettime();
    for (i = 0; i < reps; i++) fn();
    end = gettime();
    return ticks_to_microsecs(end - start) / (u64)reps;
}

/* -- the cases -------------------------------------------------------- */

static void c_ttf_20x12(void) {
    /* 20 glyphs at size 12 */
    GRRLIB_PrintfTTF(20, 60, ttf_font, "ABCDEFGHIJKLMNOPQRST", 12, 0xFFFFFFFF);
}
static void c_ttf_20x24(void) {
    /* the same 20 glyphs at size 24 -- four times the pixel area */
    GRRLIB_PrintfTTF(20, 90, ttf_font, "ABCDEFGHIJKLMNOPQRST", 24, 0xFFFFFFFF);
}
static void c_ttf_10x24(void) {
    /* half the glyphs, same size -- half the area */
    GRRLIB_PrintfTTF(20, 130, ttf_font, "ABCDEFGHIJ", 24, 0xFFFFFFFF);
}
static void c_ttf_20x48(void) {
    GRRLIB_PrintfTTF(20, 180, ttf_font, "ABCDEFGHIJKLMNOPQRST", 48, 0xFFFFFFFF);
}
static void c_width(void) {
    /* measuring only -- rasterises nothing, so it isolates FreeType's
       per-glyph metric work from the drawing */
    (void)GRRLIB_WidthTTF(ttf_font, "ABCDEFGHIJKLMNOPQRST", 24);
}
static void c_rects_20(void) {
    /* 20 textured quads, for scale: this is what a cached glyph would cost */
    int i;
    for (i = 0; i < 20; i++) {
        GRRLIB_Rectangle(20 + i * 12, 260, 10, 24, 0xFF00FFFF, true);
    }
}
/* -- the same work, through the cache ------------------------------- */
static void c_cached_20x12(void) { text_draw(20, 60,  "ABCDEFGHIJKLMNOPQRST", 12, 0xFFFFFFFF); }
static void c_cached_20x24(void) { text_draw(20, 90,  "ABCDEFGHIJKLMNOPQRST", 24, 0xFFFFFFFF); }
static void c_cached_20x48(void) { text_draw(20, 180, "ABCDEFGHIJKLMNOPQRST", 48, 0xFFFFFFFF); }
static void c_cached_width(void) { (void)text_width("ABCDEFGHIJKLMNOPQRST", 24); }

/* A results card's worth: five centred strings, which is three passes each
   through ui_draw_centered_text -- measure, then shadow, then text. This is
   the case that was costing jovian 30 seconds per 360 frames. */
static void c_card(void) {
    ui_draw_centered_text(120, "EXEMPLARY", 26, 0xFFC247FF);
    ui_draw_centered_text(170, "4800", 30, 0xE8EEFFFF);
    ui_draw_centered_text(230, "ESCORTED 8   LOST 0", 14, 0xE8EEFFFF);
    ui_draw_centered_text(256, "KILLS 3   STRIKES 0", 14, 0x6B7699FF);
    ui_draw_centered_text(360, "PRESS A TO FLY AGAIN", 14, 0x5FF0FFFF);
}

static void c_rects_400(void) {
    int i;
    for (i = 0; i < 400; i++) {
        GRRLIB_Rectangle(20 + (i % 40) * 12, 300 + (i / 40) * 3, 10, 3,
                         0x00FF00FF, true);
    }
}

int main(void) {
    const MagnoliaConfig cfg = { "textbench", 10, 6 };
    int reported = 0;

    if (magnolia_init(&cfg) == -2) return 1;
    input_init();

    while (1) {
        input_scan();
        if (input_home_pressed()) break;

        renderer_draw_background();

        if (!reported) {
            u64 t12, t24, t24h, t48, tw, tr20, tr400;

            t12  = time_us(c_ttf_20x12,  REPS);
            t24  = time_us(c_ttf_20x24,  REPS);
            t24h = time_us(c_ttf_10x24,  REPS);
            t48  = time_us(c_ttf_20x48,  REPS);
            tw   = time_us(c_width,      REPS);
            tr20 = time_us(c_rects_20,   REPS);
            tr400= time_us(c_rects_400,  REPS);

            printf("bench: ttf 20 glyphs @12 = %llu us\n", t12);
            printf("bench: ttf 20 glyphs @24 = %llu us   (4x the area of @12)\n", t24);
            printf("bench: ttf 10 glyphs @24 = %llu us   (half the glyphs of @24)\n", t24h);
            printf("bench: ttf 20 glyphs @48 = %llu us   (16x the area of @12)\n", t48);
            printf("bench: WidthTTF 20 @24   = %llu us   (metrics only, no raster)\n", tw);
            printf("bench: 20 filled rects   = %llu us   (what a cached glyph costs)\n", tr20);
            printf("bench: 400 filled rects  = %llu us\n", tr400);
            printf("bench: a 60fps frame is 16667 us\n");
            {
                u64 k12, k24, k48, kw, card;
                int entries, hits, misses, evictions;
                unsigned long bytes;

                k12  = time_us(c_cached_20x12, REPS);
                k24  = time_us(c_cached_20x24, REPS);
                k48  = time_us(c_cached_20x48, REPS);
                kw   = time_us(c_cached_width, REPS);
                card = time_us(c_card,         REPS);

                printf("bench: cache enabled = %d\n", text_cache_enabled());
                printf("bench: cached 20 @12   = %llu us\n", k12);
                printf("bench: cached 20 @24   = %llu us\n", k24);
                printf("bench: cached 20 @48   = %llu us\n", k48);
                printf("bench: cached width 20 = %llu us\n", kw);
                printf("bench: a results card  = %llu us  (5 centred strings)\n", card);

                text_stats(&entries, &hits, &misses, &evictions, &bytes);
                printf("bench: entries=%d hits=%d misses=%d evictions=%d bytes=%lu\n",
                       entries, hits, misses, evictions, bytes);
            }

            printf("bench: done\n");
            reported = 1;
        }

        renderer_finish();
    }

    magnolia_shutdown();
    return 0;
}
