# bench

Measurement programs for the engine. **Not a game, and not built by the
top-level `make`** — `cd bench && make`, then run the `.dol` and read the log.

Everything here prints through `printf`, which reaches Dolphin's log via
`SYS_STDIO_Report(true)` in `magnolia_init()`. Set `OSREPORT = True` and
`WriteToFile = True` in Dolphin's `Logger.ini`; both default to False, which
makes a working trace look like a dead one.

## textbench

What drawing text costs, and where the cost is. It exists because the obvious
guess was wrong: text was slow, the obvious suspect was per-pixel plotting into
the framebuffer, and the fix for that would have been batching and would have
bought nothing.

The measurement that settled it is the pair of contrasts — the same glyph count
at four times the pixel area, and `GRRLIB_WidthTTF`, which measures without
rasterising anything. The first barely moved and the second cost almost as much
as drawing, so the cost was FreeType per glyph, and the fix was a cache.

Keep those cases if this is extended. A benchmark that only reports totals tells
you something is slow; these tell you *which* thing, which is the only part that
chooses a fix.

Numbers are from Dolphin and are inflated by emulation — read them against each
other, not as hardware figures.
