/* Slot bookkeeping for the glyph cache.
 *
 * glyphcache.c is pure -- no GRRLIB, no FreeType, no libogc -- so the real
 * shipped translation unit links here and nothing below is a reimplementation.
 *
 * What this guards, in order of how much it would hurt:
 *
 *  1. Eviction reporting. The caller frees a texture when and only when it is
 *     told GLYPH_MISS_EVICTED. Report a fresh slot as evicted and it frees
 *     memory it never allocated; report an eviction as fresh and it leaks one
 *     texture per glyph, forever, on a console with 24MB. Neither shows up on
 *     a screenshot and only the second is survivable for a while, which is
 *     what makes it the dangerous one.
 *
 *  2. That a hit is a hit. A cache that misses on something it holds is not
 *     wrong on screen -- it redraws the same glyph correctly -- it is merely
 *     as slow as having no cache, which is exactly the condition this whole
 *     module exists to fix and exactly the one a frame rate cannot distinguish
 *     from success.
 *
 *  3. Identity. (ch, size) is the key; a glyph must not be served at the wrong
 *     size, which WOULD show, at the worst possible moment -- the first frame
 *     a game draws the same letter at two sizes.
 *
 *  4. LRU order. Evicting the most recently used entry instead of the least
 *     turns a cache into a thrash under exactly the load it is meant to help.
 */

#include "harness.h"
#include "../source/glyphcache.h"

int main(void) {
    printf("glyph cache\n");

    /* -- identity and hits -------------------------------------------- */
    {
        GlyphCache c;
        int a = -1, b = -1, again = -1;
        glyph_cache_init(&c);

        check_int(glyph_cache_get(&c, 'A', 12, &a), GLYPH_MISS_FRESH,
                  "the first ask for a glyph is a fresh miss");
        check_int(glyph_cache_get(&c, 'A', 12, &again), GLYPH_HIT,
                  "the second ask for the same glyph is a hit");
        check_int(again, a, "and it comes back in the slot it was put in");

        /* The same character at another size is a different glyph. */
        check_int(glyph_cache_get(&c, 'A', 24, &b), GLYPH_MISS_FRESH,
                  "the same character at another size is a different glyph");
        check(b != a, "and gets a slot of its own");

        check_int(c.count, 2, "two entries held");
        check_int(c.hits, 1, "one hit counted");
        check_int(c.misses, 2, "two misses counted");
        check_int(c.evictions, 0, "nothing evicted yet");
    }

    /* -- filling it exactly to capacity ------------------------------- */
    {
        GlyphCache c;
        int i, slot, distinct_ok = 1;
        int seen[GLYPH_CACHE_MAX];
        glyph_cache_init(&c);
        memset(seen, 0, sizeof(seen));

        for (i = 0; i < GLYPH_CACHE_MAX; i++) {
            GlyphLookup r = glyph_cache_get(&c, 33 + (i % 90), 8 + i, &slot);
            if (r != GLYPH_MISS_FRESH) distinct_ok = 0;
            if (slot < 0 || slot >= GLYPH_CACHE_MAX || seen[slot]) distinct_ok = 0;
            seen[slot] = 1;
        }
        check(distinct_ok,
              "filling to capacity gives every glyph a fresh slot of its own");
        check_int(c.count, GLYPH_CACHE_MAX, "and the cache is exactly full");
        check_int(c.evictions, 0, "with nothing evicted on the way");

        /* One more must evict, and must say so. */
        {
            GlyphLookup r = glyph_cache_get(&c, 'Z', 999, &slot);
            check_int(r, GLYPH_MISS_EVICTED,
                      "the entry past capacity evicts, and reports it");
            check_int(c.evictions, 1, "one eviction counted");
            check_int(c.count, GLYPH_CACHE_MAX,
                      "and the cache stays full rather than growing");
        }
    }

    /* -- LRU order ----------------------------------------------------- */
    {
        GlyphCache c;
        int i, slot, first_slot = -1, victim;
        glyph_cache_init(&c);

        /* Fill it. The first glyph inserted is the least recently used. */
        for (i = 0; i < GLYPH_CACHE_MAX; i++) {
            glyph_cache_get(&c, 33 + (i % 90), 8 + i, &slot);
            if (i == 0) first_slot = slot;
        }
        /* Touch everything EXCEPT the first, so it stays the oldest. */
        for (i = 1; i < GLYPH_CACHE_MAX; i++) {
            glyph_cache_get(&c, 33 + (i % 90), 8 + i, &slot);
        }
        glyph_cache_get(&c, 'Q', 777, &victim);
        check_int(victim, first_slot,
                  "the least recently used entry is the one evicted");

        /* And now re-touching the survivor set must still all hit: an
           eviction that quietly clobbered a second slot would show here. */
        {
            int all_hit = 1;
            for (i = 1; i < GLYPH_CACHE_MAX; i++) {
                if (glyph_cache_get(&c, 33 + (i % 90), 8 + i, &slot) != GLYPH_HIT) {
                    all_hit = 0;
                }
            }
            check(all_hit, "evicting one entry disturbs no other");
        }
    }

    /* -- a hot working set never evicts -------------------------------
     * The realistic case: a HUD redrawing the same handful of glyphs every
     * frame. If this ever starts evicting, the cache has stopped paying for
     * itself and GLYPH_CACHE_MAX is the number to look at.
     */
    {
        GlyphCache c;
        const char *hud = "0123456789 SCOREx";
        int frame, i, slot;
        glyph_cache_init(&c);

        for (frame = 0; frame < 600; frame++) {
            for (i = 0; hud[i]; i++) {
                glyph_cache_get(&c, (unsigned char)hud[i], 20, &slot);
                glyph_cache_get(&c, (unsigned char)hud[i], 12, &slot);
            }
        }
        check_int(c.evictions, 0,
                  "600 frames of a realistic HUD evict nothing");
        check(c.hits > c.misses * 100,
              "and are served overwhelmingly from cache");
    }

    /* -- clear keeps the counters -------------------------------------- */
    {
        GlyphCache c;
        int slot;
        glyph_cache_init(&c);
        glyph_cache_get(&c, 'A', 12, &slot);
        glyph_cache_get(&c, 'A', 12, &slot);
        glyph_cache_clear(&c);
        check_int(c.count, 0, "clearing empties the cache");
        check_int(glyph_cache_get(&c, 'A', 12, &slot), GLYPH_MISS_FRESH,
                  "so a glyph it used to hold misses again");
        check(c.hits == 1, "but the run's counters survive the clear");
    }

    return report();
}
