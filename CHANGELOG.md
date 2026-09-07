# Changelog

All notable changes to the magnolia engine are documented here.

## v0.3.0 (unreleased)

The second-game release: George Boole and Lava Dome drove the extraction of
shared modules, the host-side test suite, and the deployment targets that every
game now starts with -- and, latterly, the input, sprite-sheet and timestep work
that a game with two players in front of it needs.

### Text rendering

- **Glyph cache** — `source/text.c` rasterises each (character, size) once
  into a texture and blits it thereafter, caching the metrics alongside so
  measuring is free too. `ui_draw_text_shadow`, `ui_draw_centered_text`,
  `ui_draw_text_centered_in` and `ui_text_width` all go through it; nothing
  moves on screen, because the cached path reproduces `GRRLIB_PrintfTTF`'s
  positioning exactly.

  Drawing text was the most expensive thing this engine did, and not for the
  reason it looks like. Measured in Dolphin with `bench/`:

  | | before | after |
  |---|---|---|
  | 20 glyphs at size 12 | 5839 us | 44 us |
  | 20 glyphs at size 24 | 6403 us | 50 us |
  | 20 glyphs at size 48 | 8574 us | 57 us |
  | measuring 20 glyphs | 5755 us | 12 us |

  A 60fps frame is 16667us, so twenty glyphs used to be a third of one. The
  cost tracked the **number** of glyphs and barely noticed their size — four
  times the pixel area cost 10% more — and `GRRLIB_WidthTTF`, which rasterises
  nothing at all, cost almost as much as drawing. It was FreeType, about 290us
  per glyph under emulation, not the pixels and not the GX calls. That is why
  the fix is a cache and not batching, and it is worth having measured rather
  than assumed: the obvious guess was per-pixel plotting, and the obvious fix
  for that would have bought nothing.

  What this cost in practice: jovian-humanitarian-conflict's results card —
  five centred strings over an otherwise static screen — ran at **12fps**, and
  during play every frame that drew a score popup hit the engine's `dt` cap,
  so the game **discarded time** in proportion to how much was happening. Both
  are gone: the card runs at 60fps and no frame reaches the cap.

  The cache holds `GLYPH_CACHE_MAX` (192) entries with LRU eviction. A full
  game frame plus a results card measured 76 entries and 229KB, with no
  evictions over ~12,000 lookups.

- **Kerning disables the cache rather than being ignored.** The cached path
  advances by each glyph's own advance and does not ask FreeType for pair
  adjustments, because that would be a per-frame FreeType call again.
  magnolia's bundled font has no kerning table, but a game may load its own,
  so `text_init()` checks the face and falls back to `GRRLIB_PrintfTTF` when
  it kerns. A slow correct string beats a fast wrong one.

- **BREAKING, for one line: games need FreeType's headers on their include
  path.** `-lfreetype` was already in every game's `LIBS` because GRRLIB needs
  it; `text.c` needs the headers too. Add to the `export INCLUDE` block:

  ```makefile
  -I$(PORTLIBS_PATH)/ppc/include/freetype2 \
  ```

  `template/Makefile` and all five games in this tree were updated in the same
  commit. A game that misses it fails at `ft2build.h: No such file or
  directory` while compiling the engine.

- `source/glyphcache.c` holds the slot bookkeeping and is free of GRRLIB,
  FreeType and libogc, so `make test-glyphcache` exercises the real shipped
  eviction logic on the host — the same split as `ui_geom` under `ui_utils`.
  Worth the split because the dangerous failure is quiet: a cache that reports
  a fresh slot as evicted frees memory it never allocated, and one that reports
  an eviction as fresh leaks a texture per glyph until a 24MB console runs out
  an hour in. Neither is visible on a screenshot.

- `bench/` — the program the numbers above come from. Not a game and not built
  by `make`; see `bench/README.md`.

### Engine core

- **printf now reaches Dolphin's log** — `magnolia_init()` calls
  `SYS_STDIO_Report(true)`, which routes stdout and stderr to the OSReport UART.
  Without it libogc leaves stdout attached to nothing, so **every `printf` in
  every game built on this engine was silently discarded**, across all three
  shipped games. `template/README.md` has been documenting the Logger.ini half
  of this the whole time, so the advice to "run the `.dol` and read the log"
  produced a 0-byte log however Logger.ini was set — a symptom that reads as
  logging being switched off rather than as nothing having been sent, which is
  why it survived three games. `george-boole` has a `printf` at
  `wii/source/main.c:93` that has therefore never produced a line; it does now.
  The call is first in `magnolia_init()`, ahead of `renderer_init()`, so a
  `printf` tracing a video failure is not lost to the failure it is reporting.
  Costs nothing where nothing is listening — on hardware with no USB Gecko the
  writes go to an absent EXI device, the same place they went before — but keep
  `printf` out of per-frame code, since an EXI write is not free.
- **magnolia_init() reorders boot** — video comes up before `fatInitDefault()`, so a
  slow or wedged SD mount shows a splash screen instead of a blank framebuffer.
  Adds `renderer_splash()` for status frames during bring-up.
- **The card is unmounted on the way out** — `magnolia_init()` mounted it and
  nothing ever let go, in the engine or in any of the three games. Teardown now
  mirrors bring-up: the card first, then video, the reverse of the order they
  came up in. Whether libfat was holding anything back is not something that can
  be settled off-hardware, which is much of the reason to close the pair rather
  than reason about it — silent unwritten saves are a failure this engine has
  already been caught by twice.
- **App directory auto-created** — `magnolia_init()` creates `sd:/apps/<app_name>/`
  before anything tries to persist into it. Without this, `fopen(..., "w")` fails
  when the parent is missing, and libfat does not create one implicitly.
- **Save diagnostics** — `prefs_persisted()` and `scoring_persisted()` report whether
  the last save reached the card. `magnolia_init()` probes with a real write, so
  the answer is valid from boot.
- **scoring actually probes** — the line above was only ever true of `prefs`.
  `scoring_init()` loaded and left the flag optimistic, so `scoring_persisted()`
  reported a healthy card until the first qualifying run — the whole stretch of
  play a game would want to warn the player about. It now probes too, writing
  beside the score file and removing it again rather than re-saving the table.
- **A short read is a failed read** — `prefs` and `scoring` both ignored `fread`'s
  return and terminated the buffer at the length the file *claimed*, leaving the
  parser to walk whatever `malloc` last left there. `audio` had always checked.
- **`ScoreEntry` is back to who and how well** — `moves` and `highest_earned`
  were added to it during this cycle and are removed again before the release.
  They were one game's statistics: a puzzle's move count and its best merged
  tile. They travelled through the engine's struct, its public API, its save
  format and its tests, and not one site ever read them back — write-only from
  end to end. `scoring_add_entry()` takes initials and a score again.
  Cards written while the keys existed still load; the parser skips them rather
  than migrating, and the next save rewrites the file without them. A game that
  wants to keep more about a run than this is where that mechanism should be
  designed, against what it actually puts on screen.

### New modules

- **prefs** — Persisted int key/value store for player preferences. Replaces
  hand-rolled settings readers in individual games.
- **scoring grows tables** — Named tables persist to `scores-<id>.json` beside the
  default `scores.json`. `scoring_increment()` supports per-table overflow bonuses.
  Backwards-compatible: old single-table saves still load.
- **menu** — Grid/list cursor with wrapping and a scrolling window. A one-column
  grid is a vertical list. Exhaustively tested: every shape, every start position,
  every move.
- **gamestate** — Grows `GS_MENU` and `GS_PAUSED`. The pre-run character select that
  was a loose flag is now a state.
- **ui_utils** — Design-space text measurement, centring, filled panels with
  rounded corners, a modal scrim, and word wrap.
- **clock** — Frame counter, delta time, and easing functions for tile animations.
- **theme** — HSL-to-RGB palette generator with complementary colours. Guarded on
  `#ifdef GEKKO` so it compiles on the host for testing.

### Sprites and audio

- **Memory-backed loaders** — `sprite_load_mem()`, `audio_load_sfx_mem()`,
  `audio_play_music_mem()`. Assets linked into the binary cannot go missing or
  fall out of step with the code.
- **Per-axis scaled sprite drawing** — `sprite_draw_scaled_xy()` takes separate
  `scaleX` and `scaleY` for non-square framebuffer pixels (16:9, PAL).

### Input

- **Button 1 and 2** added to the Wiimote wrapper.

### Testing

- **Host-side test suite** — `prefs`, `scoring`, `menu`, `gamestate`, and `theme` run on
  any machine with a C compiler. No devkitPPC, no console, no emulator.
- **CI on every push** — GitHub Actions runs the host tests on a GitHub-hosted
  runner.
- **The console build runs in CI too** — the standalone `make` now runs in
  devkitPro's own container, with GRRLIB built from source, on a GitHub-hosted
  runner. It had been held for a self-hosted machine that turned out not to be
  needed. This is the only automated check `renderer`, `sprite`, `audio`, `core`,
  `input` and `ui_utils` get: over half the engine is bound to libogc and cannot
  be host-tested, so until now nothing but a local `make` stood between a broken
  console build and the next game to pick the engine up.
- **`-Werror` on the host tests** — CONTRIBUTING already asked for warning-clean
  builds; now CI enforces it rather than trusting it. The cross build keeps plain
  `-Wall -Wextra`, since a toolchain bump can raise warnings inside third-party
  headers and that should not stop the build.
- **`make test-theme`** — HSL primaries, secondaries, complementaries, grey, black,
  white, wrap-around, and `theme_generate()` range checks over 2000 palettes.

- **`ui_geom`** — the safe-area geometry, design-space projection and word wrap,
  split out of `ui_utils.c` so they can be asserted. `ui_utils.c` keeps every
  GRRLIB call and forwards the arithmetic; `ui_utils.h` is unchanged, so games
  compile against it exactly as before. The wrap takes a measurer rather than
  calling the font directly, the same seam `input_state` uses for buttons.
  The port was checked against the old implementation over 1540 text/width/size
  combinations before landing: identical output in every one.
- **A word too long for the line buffer is broken, not dropped** — over about
  128 characters it used to vanish outright, nothing drawn and nothing said,
  while a word merely too wide for the *column* has always been allowed to run
  over. The cliff between overflowing and disappearing was the size of a buffer
  nobody looking at the screen can see. Checked against the old behaviour over
  the same 1540 combinations: only the over-long cases changed.
- **A wrapped paragraph beginning with a newline** no longer draws uninitialised
  memory. `ui_draw_text_wrapped` left its line buffer unterminated, and text
  opening with a newline reached the draw call before anything was written to it.

### Tooling

- **`tools/new-game.sh`** — Scaffolds a game from `template/` with bin2s rules,
  deploy targets, and a host-test target.
- **`make card SD=/mnt/e`** — Installs onto a mounted SD card (merges, preserves
  saves).
- **`make wii WIILOAD=tcp:<ip>`** — Sends a build to a running console over the
  network (temporary, nothing written to card).
- **`make dolphin`** — Clears the app directory on every deploy (dev loop).

### Documentation

- **AGENTS.md** — No-AI-attribution rule for commits, PRs, and release notes.
- **README.md** — Rewritten for the real API, module table updated, design rule
  documented.
- **font/OFL.txt** — The SIL Open Font License 1.1 text now ships beside the
  Press Start 2P `.ttf` the engine distributes. The licence was named in the
  README but its text was nowhere in the repository.

### Two-player groundwork

Groundwork for a local two-player game. Three engine gaps stood between magnolia
and any game with two people in front of it; none of them were genre decisions,
so all three are closed here and everything genuinely fighting-game-shaped —
hitboxes, movelists, round flow, a roster — stays in the game.

### Input: more than one player

- **Every query takes a player index.** `input_scan()` samples all four
  controller channels; `input_pressed(1, INPUT_BTN_A)` is player two's A. Before
  this, `input.c` passed a hardcoded `0` to every WPAD call, so a second
  controller was invisible to the engine.
- **Releases and held states for every button.** `input_held()`,
  `input_released()`. Previously only `A` had a held query and nothing could
  report a button coming up, which rules out blocking, charging and hold-to-aim.
- **`input_snapshot()`** returns a player's whole frame as a copyable value —
  what an input buffer is made of. Recognising patterns in those frames stays
  with the game.
- **The zero-argument spellings are unchanged.** `input_a_pressed()` and friends
  are now player-0 wrappers; all three shipped games compile and build untouched.
- **`input_state.c` split out of `input.c`**, leaving `input.c` as the WPAD read
  and putting edges and auto-repeat somewhere they can be tested. Two behaviours
  changed as a result: hold counters are per-player (they were one global, so one
  player holding a direction advanced everyone's repeat), and the repeat edge is
  settled once per frame rather than inside the query — `input_dir_repeat()` used
  to be able to fire twice if a caller asked twice on the delay frame, which the
  header already promised it would not.

### Sprite sheets and mirroring

- **`SpriteSheet`** — uniform grid of frames in one texture, cells counted
  left-to-right then top-to-bottom. This is the cross-engine format AGENTS.md
  already described and named `sprite.c` as the reader of; until now `sprite.c`
  could only draw whole textures.
- **Mirroring** via `sprite_sheet_draw_ex()` and `sprite_draw_ex()`, reflected
  about the frame's own origin so an anchor point does not move when a character
  turns around. Drawn through GRRLIB's `*Quad` calls, which take explicit
  corners — a negative `scaleX` through `GRRLIB_DrawImg()` reflects about the
  draw position instead, landing the art a full width away.
- Unflipped draws still go through `GRRLIB_DrawImg`/`DrawTile` exactly as before,
  so no shipped game's output moves.
- Frame sizes that do not divide the image leave the sheet empty rather than
  drawing the plausible part of a mis-exported asset.

### Optional fixed timestep

- **`clock_set_fixed_hz()` / `clock_fixed_steps()` / `clock_fixed_dt()`**, with
  the accumulator in a host-clean `timestep.c`. `clock_dt()` is unchanged and
  still the right answer for anything continuous; a game that never asks for a
  fixed step sees no difference.
- Rules written in frames — three frames of startup, twelve of recovery — need a
  step that does not vary with SD reads. A frame owing more than
  `TIMESTEP_MAX_STEPS` is treated as a stall and its backlog dropped, rather than
  repaid over the following frames as a burst of speed.

### Tests

- **`make test-input`** (54 checks) and **`make test-timestep`** (26 checks).
  Neither module could be tested before the splits; both now assert the things
  that were being taken on trust, including that a second player has their own
  hold counters and that a second of real time buys the same number of steps
  however the frames it arrived in were shaped.
- **`tests/fake_input.c` no longer reimplements `input.h`.** It feeds the real
  `input_state.c`, so the presses the `gamestate` cases see are computed by the
  same lines the console runs — the copy could previously drift from the original.
- **Repaired `make test`**, which had not compiled since v0.3.0's
  "Add moves and highest_earned to ScoreEntry": that commit widened
  `scoring_add_entry()` to four arguments and updated no test. CI had been red
  since. The call sites were repaired and the round trip asserted. Those two
  fields have since been removed again — see above — so what `test_storage`
  carries forward from that work is the case that outlived them: a card written
  with keys this build no longer emits still loads, and its scores do not
  absorb the retired values.

## v0.2.0

Breaking release. Under 0.x semver a breaking change bumps the minor.

### Breaking changes

- **`renderer_init()` replaced by `magnolia_init(MagnoliaConfig)`** — sequences
  SD, video, font, UI metrics and scoring. Returns a status so games can report a
  degraded start.
- **`scoring_init()` takes a path and capacity** — no more hardcoded `SCORES_PATH`.
- **player, stars, characters moved out** — they encoded one game's decisions and
  forced the engine to depend on that game's `config.h`.

### Added

- **Audio module** — PCM16 music loop and fire-and-forget SFX over ASND. Voice 0
  reserved for music. Format is `VOICE_STEREO_16BIT_LE` (little-endian on a
  big-endian console).
- **Score-attack gamestate** — title → ready → playing → game over → initials →
  high scores, plus the initials editor.
- **Standalone build** — `make` builds `libmagnolia.a` with no game on the include
  path, so any reach into a game header is an immediate build failure.
- **Sprite origin concept** — `sprite_load()` takes an origin point so exported art
  lands exactly where you draw it.

### Fixed

- Typo in README.md.
- Project name origin revised in README.

## v0.1.0

Initial release.

- Engine init, GRRLIB renderer, TTF font, frame flush.
- Wiimote input with D-pad, hold state, and auto-repeat.
- Sprite loading and drawing.
- Apache 2.0 license.
