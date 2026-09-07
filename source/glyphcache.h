#ifndef GLYPHCACHE_H
#define GLYPHCACHE_H

/* Which slot holds a given (character, size), and which slot to reuse when
 * none does.
 *
 * This is the arithmetic behind text.c, separated from the rasteriser and the
 * textures for the same reason ui_geom.h is separated from ui_utils.h:
 * everything here only computes, so the host tests can reach it. text.c owns
 * the GRRLIB textures and FreeType; this owns nothing but bookkeeping and does
 * not know what a glyph looks like.
 *
 * Worth separating because the failure mode is quiet. A cache that returns the
 * wrong slot draws the wrong letter, which is obvious; a cache whose eviction
 * is subtly wrong draws the RIGHT letters while leaking a texture per glyph
 * until the console runs out of memory an hour in, which is not obvious at all
 * and is not reproducible by looking at a screenshot.
 */

/* Entries, not bytes. Sized against what a frame actually asks for: a HUD, a
 * score and a line or two of prompt is well under a hundred distinct
 * (char, size) pairs, and the largest screen in any game here -- a full
 * scoreboard -- is around 150. Above that, eviction starts costing a
 * rasterisation per frame and the cache stops earning its keep, so the number
 * to raise if that happens is this one. */
#define GLYPH_CACHE_MAX 192

typedef struct {
    unsigned short ch;      /* 0 marks an empty slot; a NUL glyph is not cached */
    unsigned short size;
    unsigned int   used;    /* LRU stamp, from the cache's own counter */
} GlyphSlot;

typedef struct {
    GlyphSlot slots[GLYPH_CACHE_MAX];
    unsigned int clock;
    int count;
    /* Counters, so a game or a bench can see whether the cache is working
       rather than inferring it from a frame rate. */
    int hits, misses, evictions;
} GlyphCache;

/* What a lookup did, which is what tells the owner whether the payload in the
 * returned slot is valid, absent, or somebody else's and needing freeing. */
typedef enum {
    GLYPH_HIT = 0,        /* slot holds (ch, size); its payload is valid */
    GLYPH_MISS_FRESH,     /* slot was empty; fill it */
    GLYPH_MISS_EVICTED    /* slot held another glyph; free that payload, then fill */
} GlyphLookup;

void glyph_cache_init(GlyphCache *c);

/* Find or reserve the slot for (ch, size). Never fails and never returns a
 * negative slot: when the cache is full it evicts the least recently used
 * entry, because a glyph that cannot be cached still has to be drawn and
 * refusing here would mean the caller needed a second code path for it. */
GlyphLookup glyph_cache_get(GlyphCache *c, unsigned int ch, unsigned int size,
                            int *slot);

/* Forget everything. The owner must release every payload first -- this
 * clears the bookkeeping and has no way to reach the textures. */
void glyph_cache_clear(GlyphCache *c);

#endif
