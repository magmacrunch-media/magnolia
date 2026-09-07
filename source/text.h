#ifndef TEXT_H
#define TEXT_H

#include <grrlib.h>

/* Cached text drawing.
 *
 * ---------------------------------------------------------------------
 * Why this exists.
 *
 * GRRLIB_PrintfTTF asks FreeType for every glyph on every call, and that is
 * the whole cost of drawing text on this engine. Measured in Dolphin against
 * magnolia's own font:
 *
 *     20 glyphs at size 12   5839 us
 *     20 glyphs at size 24   6403 us     four times the pixel area, +10%
 *     10 glyphs at size 24   3242 us     half the glyphs, half the cost
 *     20 glyphs at size 48   8574 us     sixteen times the area, +47%
 *     GRRLIB_WidthTTF, 20    5755 us     MEASURES ONLY -- rasterises nothing
 *     20 filled rectangles      1 us
 *
 * Cost tracks the number of glyphs and barely notices their size, and
 * GRRLIB_WidthTTF -- which draws nothing at all -- costs almost as much as
 * drawing. So it is not the pixels and it is not the GX calls: it is FreeType,
 * about 290us per glyph under emulation. A 60fps frame is 16667us, so twenty
 * glyphs is a third of a frame and a centred string costs THREE passes over
 * its glyphs, because ui_draw_centered_text measures once and then draws twice
 * for the shadow.
 *
 * This rasterises each (character, size) once into a texture and blits it
 * thereafter, and caches the metrics with it so measuring is free too.
 *
 * ---------------------------------------------------------------------
 * What it does not do.
 *
 * Kerning. The cached path advances by each glyph's own advance and does not
 * ask FreeType for pair adjustments, because that would be a per-frame
 * FreeType call again and would put back most of what this removes. magnolia's
 * bundled font has no kerning table, so nothing changes for it -- and rather
 * than assume that of a font a game might load itself, text_init() checks and
 * DISABLES the cache when the face reports kerning, falling back to GRRLIB.
 * A slow correct string beats a fast wrong one.
 */

/* Brings the cache up against the font renderer.c loaded. Returns 0 when the
 * cache is live, -1 when there is no font (nothing will draw either way), and
 * -2 when the font kerns and the cache has therefore been left off. In every
 * case the draw and measure calls below work; only their speed differs. */
int  text_init(void);

/* Frees every cached texture. Safe to call when never initialised. */
void text_shutdown(void);

/* Drop the cached glyphs, keeping the counters. For a game that changes font
 * or has just walked a screen of text it will not show again. */
void text_reset(void);

/* Draw and measure, in real screen pixels. Positioning matches
 * GRRLIB_PrintfTTF exactly -- baseline at y + size, each glyph at its own
 * bitmap_left / bitmap_top -- so switching a call site to these does not move
 * anything on screen. */
void text_draw(int x, int y, const char *s, unsigned int size, u32 color);
u32  text_width(const char *s, unsigned int size);

/* Introspection. `bytes` is texture memory currently held. Reported so a game
 * can print them rather than infer the cache is working from a frame rate --
 * the failure this guards is a cache that thrashes, which looks exactly like
 * no cache at all. */
void text_stats(int *entries, int *hits, int *misses, int *evictions,
                unsigned long *bytes);
int  text_cache_enabled(void);

#endif
