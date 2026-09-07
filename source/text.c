/* See text.h. The half that touches FreeType and GRRLIB; the slot bookkeeping
 * is in glyphcache.c, which the host tests link on its own. */
#include <ft2build.h>
#include FT_FREETYPE_H

#include <grrlib.h>
#include <string.h>

#include "text.h"
#include "glyphcache.h"
#include "renderer.h"

/* Payload, parallel to the cache's slots. Kept as separate arrays rather than
   inside GlyphSlot so glyphcache.c can stay free of GRRLIB types and remain
   host-testable. */
static GRRLIB_texImg *g_tex[GLYPH_CACHE_MAX];
static short g_left[GLYPH_CACHE_MAX];
static short g_top[GLYPH_CACHE_MAX];
static short g_adv[GLYPH_CACHE_MAX];

static GlyphCache g_cache;
static int g_enabled;
static unsigned long g_bytes;

static void free_slot(int slot) {
    if (g_tex[slot]) {
        /* GRRLIB rounds nothing for us; this is what we allocated. */
        g_bytes -= (unsigned long)g_tex[slot]->w * g_tex[slot]->h * 4u;
        GRRLIB_FreeTexture(g_tex[slot]);
        g_tex[slot] = NULL;
    }
}

int text_init(void) {
    memset(g_tex, 0, sizeof(g_tex));
    glyph_cache_init(&g_cache);
    g_bytes = 0;
    g_enabled = 0;

    if (!ttf_font || !ttf_font->face) return -1;
    /* See the header: the cached path does not kern, so it is not used for a
       font that does. Correctness first; this engine has exactly one bundled
       font and it does not kern. */
    if (ttf_font->kerning) return -2;

    g_enabled = 1;
    return 0;
}

void text_shutdown(void) {
    int i;
    for (i = 0; i < GLYPH_CACHE_MAX; i++) free_slot(i);
    glyph_cache_init(&g_cache);
    g_bytes = 0;
    g_enabled = 0;
}

void text_reset(void) {
    int i;
    for (i = 0; i < GLYPH_CACHE_MAX; i++) free_slot(i);
    glyph_cache_clear(&g_cache);
    g_bytes = 0;
}

int text_cache_enabled(void) { return g_enabled; }

void text_stats(int *entries, int *hits, int *misses, int *evictions,
                unsigned long *bytes) {
    if (entries)   *entries   = g_cache.count;
    if (hits)      *hits      = g_cache.hits;
    if (misses)    *misses    = g_cache.misses;
    if (evictions) *evictions = g_cache.evictions;
    if (bytes)     *bytes     = g_bytes;
}

/* Rasterise one glyph into the given slot. Returns 0 on success.
 *
 * A blank glyph -- a space -- caches with a NULL texture and its advance, which
 * is the whole point of caching it: without an entry, every space in every
 * string would be a FreeType load for a bitmap with no pixels in it. */
static int raster(int slot, unsigned int ch, unsigned int size) {
    FT_Face face = (FT_Face)ttf_font->face;
    FT_GlyphSlot gs;
    unsigned int w, h, tw, th, x, y;
    GRRLIB_texImg *tex;

    if (FT_Set_Pixel_Sizes(face, 0, size)) return -1;
    if (FT_Load_Char(face, (FT_ULong)ch, FT_LOAD_RENDER)) return -1;

    gs = face->glyph;
    g_left[slot] = (short)gs->bitmap_left;
    g_top[slot]  = (short)gs->bitmap_top;
    g_adv[slot]  = (short)(gs->advance.x >> 6);

    w = gs->bitmap.width;
    h = gs->bitmap.rows;
    if (w == 0 || h == 0) { g_tex[slot] = NULL; return 0; }

    /* GX tiles RGBA8 in 4x4 blocks, so a texture whose dimensions are not
       multiples of four is read past its own allocation. Round up and leave
       the margin transparent. */
    tw = (w + 3u) & ~3u;
    th = (h + 3u) & ~3u;

    tex = GRRLIB_CreateEmptyTexture(tw, th);
    if (!tex) { g_tex[slot] = NULL; return -1; }

    /* Every pixel written explicitly, including the padding: an empty texture
       from GRRLIB is not documented to be cleared, and a glyph fringed with
       whatever was in that memory is the kind of fault that shows up on one
       console and not another. */
    for (y = 0; y < th; y++) {
        for (x = 0; x < tw; x++) {
            u32 px = 0x00000000u;
            if (x < w && y < h) {
                /* White with the glyph's coverage in the alpha, so that
                   GRRLIB_DrawImg's colour argument tints it at draw time and
                   one cached glyph serves every colour it is asked for. */
                px = 0xFFFFFF00u | (u32)gs->bitmap.buffer[y * gs->bitmap.pitch + x];
            }
            GRRLIB_SetPixelTotexImg(x, y, tex, px);
        }
    }
    GRRLIB_FlushTex(tex);

    g_tex[slot] = tex;
    g_bytes += (unsigned long)tw * th * 4u;
    return 0;
}

/* The slot for (ch, size), rasterising it if needed. -1 if it cannot be had. */
static int slot_for(unsigned int ch, unsigned int size) {
    int slot;
    GlyphLookup r = glyph_cache_get(&g_cache, ch, size, &slot);
    if (r == GLYPH_HIT) return slot;
    if (r == GLYPH_MISS_EVICTED) free_slot(slot);
    else g_tex[slot] = NULL;
    if (raster(slot, ch, size) != 0) {
        /* Leave the slot claimed but empty rather than half-filled: the
           metrics were set before the texture failed, so it still advances
           correctly and simply draws nothing. */
        g_tex[slot] = NULL;
    }
    return slot;
}

void text_draw(int x, int y, const char *s, unsigned int size, u32 color) {
    int pen_x = x;
    int pen_y = y + (int)size;   /* GRRLIB_PrintfTTF's baseline convention */
    const unsigned char *p;

    if (!s) return;
    if (!g_enabled) { GRRLIB_PrintfTTF(x, y, ttf_font, s, size, color); return; }

    for (p = (const unsigned char *)s; *p; p++) {
        int slot = slot_for(*p, size);
        if (slot < 0) continue;
        if (g_tex[slot]) {
            GRRLIB_DrawImg((f32)(pen_x + g_left[slot]),
                           (f32)(pen_y - g_top[slot]),
                           g_tex[slot], 0.0f, 1.0f, 1.0f, color);
        }
        pen_x += g_adv[slot];
    }
}

u32 text_width(const char *s, unsigned int size) {
    int total = 0;
    const unsigned char *p;

    if (!s) return 0;
    if (!g_enabled) return GRRLIB_WidthTTF(ttf_font, s, size);

    for (p = (const unsigned char *)s; *p; p++) {
        int slot = slot_for(*p, size);
        if (slot >= 0) total += g_adv[slot];
    }
    return (u32)total;
}
