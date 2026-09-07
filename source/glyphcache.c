/* See glyphcache.h. Bookkeeping only: no GRRLIB, no FreeType, no libogc. */
#include <string.h>

#include "glyphcache.h"

void glyph_cache_init(GlyphCache *c) {
    memset(c, 0, sizeof(*c));
    /* clock starts at 1 so that a stamp of 0 is unambiguously "never used",
       which matters when every slot is still empty and the LRU scan runs. */
    c->clock = 1;
}

void glyph_cache_clear(GlyphCache *c) {
    int hits = c->hits, misses = c->misses, evictions = c->evictions;
    glyph_cache_init(c);
    /* Counters survive a clear. They describe the run, not the current
       contents, and zeroing them here would hide the very churn that makes
       somebody call this. */
    c->hits = hits;
    c->misses = misses;
    c->evictions = evictions;
}

GlyphLookup glyph_cache_get(GlyphCache *c, unsigned int ch, unsigned int size,
                            int *slot) {
    int i, empty = -1, oldest = 0;
    unsigned int oldest_used = 0xFFFFFFFFu;

    /* A linear scan, deliberately. GLYPH_CACHE_MAX comparisons of two shorts
       is nothing next to what a miss costs -- a FreeType glyph load, measured
       at roughly 290us under emulation -- and a hash here would be a second
       thing to get wrong for no measurable gain. */
    for (i = 0; i < GLYPH_CACHE_MAX; i++) {
        GlyphSlot *s = &c->slots[i];
        if (s->ch == 0) {
            if (empty < 0) empty = i;
            continue;
        }
        if (s->ch == (unsigned short)ch && s->size == (unsigned short)size) {
            s->used = c->clock++;
            c->hits++;
            *slot = i;
            return GLYPH_HIT;
        }
        if (s->used < oldest_used) { oldest_used = s->used; oldest = i; }
    }

    c->misses++;

    if (empty >= 0) {
        GlyphSlot *s = &c->slots[empty];
        s->ch = (unsigned short)ch;
        s->size = (unsigned short)size;
        s->used = c->clock++;
        c->count++;
        *slot = empty;
        return GLYPH_MISS_FRESH;
    }

    {
        GlyphSlot *s = &c->slots[oldest];
        s->ch = (unsigned short)ch;
        s->size = (unsigned short)size;
        s->used = c->clock++;
        c->evictions++;
        *slot = oldest;
        return GLYPH_MISS_EVICTED;
    }
}
