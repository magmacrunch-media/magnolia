# magnolia

A lightweight Wii homebrew game engine by [magmacrunch media](https://magmacrunch.com). Provides rendering, input, scoring, audio, and UI utilities for games built with devkitPPC and GRRLIB.

Named after the song "Magnolia" published by magmacrunch music.

Current version: **0.3.0** — see [CHANGELOG.md](CHANGELOG.md) for what changed.

## Quick Start

### 1. Clone or copy magnolia into your project

```
my-game/
├── Makefile
├── source/
│   └── main.c
└── ../magnolia/        ← this repo
    ├── magnolia.h
    ├── source/
    └── font/
```

### 2. Update your Makefile

```makefile
SOURCES     := source ../magnolia/source ../magnolia/font
INCLUDES    := ../magnolia ../magnolia/source ../magnolia/font source
LIBS        := -lgrrlib -lpngu -lfreetype -lpng -ljpeg -lz -lbrotlidec -lbrotlicommon -lbz2 -lfat -lasnd -lwiiuse -lbte -logc -lm
```

**And FreeType's headers on the include line.** `source/text.c` calls FreeType
directly to rasterise glyphs into the cache, so the game's `INCLUDE` needs the
directory `ft2build.h` lives in — one line, in the `export INCLUDE` block:

```makefile
export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(TOPDIR)/$(dir)) \
                   -I$(PORTLIBS_PATH)/ppc/include/freetype2 \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR) -I$(CURDIR)/$(BUILD) -I$(LIBOGC_INC)
```

`-lfreetype` was already in `LIBS`, because GRRLIB needs it; this adds only the
headers. Miss it and the build stops at `ft2build.h: No such file or directory`
while compiling the engine, which at least names the problem — unlike most of
what goes wrong in a devkitPro Makefile.

Games build magnolia's sources directly — there is no `libmagnolia.a` linking step.

### 3. Include and use

```c
#include "magnolia.h"

int main(void) {
    const MagnoliaConfig cfg = {
        .app_name     = "my-game",   /* -> sd:/apps/my-game/ */
        .max_scores   = 10,
        .overscan_pct = 6
    };

    /* Brings up SD, video, font, UI metrics and scoring in the right order.
       Returns 0 when everything came up. A negative return means the engine is
       usable but degraded — see the return code table below. */
    int status = magnolia_init(&cfg);
    if (status == -2) return 1;   /* video never came up — nothing useful is possible */

    input_init();

    GameStateMachine gs;
    gamestate_init(&gs);

    while (1) {
        input_scan();
        if (input_home_pressed()) break;

        if (gamestate_current(&gs) == GS_PLAYING) {
            /* your game */
        } else if (gamestate_update(&gs, score)) {
            /* just entered GS_PLAYING — reset your world */
        }

        renderer_draw_background();
        renderer_finish();
    }

    magnolia_shutdown();
    return 0;
}
```

### 4. Verify the engine still stands alone

```bash
cd magnolia && make      # builds libmagnolia.a
```

This compiles the engine with **no game directory on the include path**. It is
not how games consume magnolia — they build the sources directly — but if
engine code ever reaches back into a game header, this build fails immediately
instead of the coupling surfacing in the next game months later. Run it before
committing engine changes.

## Init return codes

`magnolia_init()` returns 0 when everything came up. A negative return means
the engine is usable but degraded — query the accessors and tell the player
rather than silently rendering placeholder art.

| Code | Meaning | Query |
|------|---------|-------|
| `0` | All OK | — |
| `-1` | SD card not mounted | `magnolia_sd_mounted()` |
| `-2` | Video failed (nothing further is useful) | — return immediately |
| `-3` | Font missing, video is up | `magnolia_fonts_loaded()` |

A negative return is not a failure — the engine is running, but the player may
see placeholder art or be unable to save. Check the specific condition and
surface it in-game (e.g. "SD card not inserted — scores will not be saved").

## Architecture

The engine is 12 modules in `magnolia/source/`, included through the umbrella
`magnolia.h`. The dependency graph:

```
core ─── renderer ─── ui_utils ─── theme
 │           │            │
 │           ├────────────┼──── menu
 │           │            │
 │           ├────────────┼──── scoring
 │           │            │
 │           ├────────────┼──── prefs
 │           │
 │           ├────────────┼──── audio
 │           │
 │           └────────────┼──── clock
 │
 └── input ───────────────┼──── gamestate
```

**`core`** owns the boot order and the SD paths. **`renderer`** owns the video
and font. Everything else hangs off those two. `gamestate` depends on `input`
for the controller seam; `menu` depends on `gamestate` for the state machine;
`ui_utils` depends on `renderer` for the screen geometry.

## Starting a new game

```bash
tools/new-game.sh my-game
```

Creates `../my-game` beside the engine with a Makefile, `meta.xml`, a `main.c`
skeleton and staging targets. The Makefile carries the `bin2s` asset-embedding
rules and the deploy targets — the parts that are non-obvious, and the parts that
silently drift when they are retyped for each new game.

## Deployment

```bash
make deploy     # stage sdcard/apps/my-game/ (local development)
make dolphin    # push to Dolphin's SD card folder (emulator)
make card SD=/mnt/e   # install onto a real SD card (merges, preserves saves)
make wii WIILOAD=tcp:<wii-ip>  # send to a running console over the network
```

`make card` merges — only `boot.dol` and `meta.xml` are overwritten, so saved
scores and settings survive. `make wii` sends the `.dol` over the network and
runs it immediately; nothing is written to the card.

See [wiki: Getting Started](../../wiki/Getting-Started) for full deployment details.

## Testing

```bash
make        # builds libmagnolia.a; needs devkitPPC
make test   # runs the host tests; needs only a C compiler
```

`make test` covers every engine module that is free of libogc — `prefs`,
`scoring`, `menu`, `gamestate`, `theme`, `input_state`, `timestep` and
`ui_geom` — and runs
each as its own binary (`make test-menu` and friends run one at a time). The
source list per binary is written out in the Makefile rather than wildcarded; it
is the record of which modules are host-clean.

**Storage** is where this started. Both `prefs` and `scoring` are about files,
and an emulated SD card can report itself mounted while refusing every write —
so the tests that can run on your own machine are the ones that can tell a
broken save from a broken card. Ask `prefs_persisted()` and
`scoring_persisted()` if you want to show the player which it is;
`magnolia_init()` probes the card with a real write, so both answer for the
current card from boot rather than from whenever a setting next changes.

**`menu`** gets the exhaustive sweep its header claims: every grid shape a game
might ask for, every start position, every move — the cursor stays in range, the
window follows it, and every item stays reachable.

**`gamestate`** reaches libogc only through `input.h`, so its test links
`tests/fake_input.c` in place of `source/input.c` and drives the real state
machine on the host.

**`input_state`** is the half of `input` that counts rather than reads — press,
hold and release edges, and auto-repeat, for each of up to four players. It was
untestable while it sat one call below `WPAD_ButtonsHeld()`, which meant the only
way to check that a second player had their own hold counters was to stand in
front of a console with two controllers. `tests/fake_input.c` now feeds this real
code rather than reimplementing it, so the presses the `gamestate` cases see are
computed by the same lines the console runs.

**`timestep`** is the accumulator behind `clock_fixed_steps()`. The claim worth
asserting is that a second of real time buys the same number of logic steps
however the frames it arrived in were shaped — sixty even ones, a hundred and
twenty short ones, or ten long ones.

**`theme`** is pure arithmetic guarded by `#ifdef GEKKO`, so it compiles on the
host. The test checks HSL primaries, secondaries, complementaries, grey, black,
white, wrap-around, and `theme_generate()` ranges over 2000 palettes.

**`ui_geom`** is the arithmetic behind `ui_utils`: the TV-safe rectangle, the
projection of the design space onto it, and the word wrap. It was unreachable
here only because it shared a file with the GRRLIB calls, which meant the way to
find out where a layout landed was to cross-compile it and look at a television —
a poor way to learn that PAL is 528 lines tall rather than 480. The tests state
both video modes outright. The wrap is the part worth having: it cannot measure
text without the font, so it is handed a measurer, the way `input_state` is
handed the buttons rather than reading the hardware. The test passes a monospace
one, which is not a simplification — Press Start 2P is a monospaced pixel font.

A word wider than its column overflows rather than being split, which is
deliberate and which two games' screens are laid out around. A word too long for
the line buffer is the one case that cannot overflow, since there is nowhere to
hold it, so it is broken across lines instead — it used to disappear, which made
the buffer size visible on screen in the one way it should never be.

CI runs both of these on every push; see `.github/workflows/ci.yml`. The host
tests need only a C compiler, so they run on a GitHub-hosted runner. The
standalone `make` runs in devkitPro's own container on a GitHub-hosted runner
too, with GRRLIB built from source into the portlibs tree -- it is the only
automated check `renderer`, `sprite`, `audio`, `core`, `input` and `ui_utils`
get, since none of them can be reached without libogc.

Games can be driven without a controller: see the `AUTOSTART_GAMEPLAY` and
`DEBUG_HEARTBEAT_FRAMES` hooks in the generated `config.h`. For `printf` to reach
Dolphin's log, its `Logger.ini` needs `OSREPORT = True` and `WriteToFile = True`;
both default to False, which makes a working trace look like a dead one.

## Design rule

magnolia holds what more than one game needs. Anything shaped by a single game
belongs in that game. Concretely: sprite loading and the origin concept are
engine; a particular character roster is not. A high-score table is engine; a
jetman's gravity is not. When something looks reusable but has exactly one
consumer, leave it in the game until a second one appears — an abstraction
designed against one example usually fits only that example.

## Engine Modules

| Module | Header | Description |
|--------|--------|-------------|
| **core** | `core.h` | `magnolia_init()` bring-up, init status, SD asset paths |
| **renderer** | `renderer.h` | GRRLIB init, TTF font, real video-mode geometry, frame flush |
| **sprite** | `sprite.h` | Texture load/free, draw-at-origin, scaled draw, sprite sheets, mirroring |
| **input** | `input.h` | Up to four Wiimotes: press/hold/release edges, per-frame snapshots, auto-repeat |
| **audio** | `audio.h` | PCM music loop + SFX over ASND, mono/stereo, any rate |
| **scoring** | `scoring.h` | High-score tables (one or many) with SD JSON persistence |
| **prefs** | `prefs.h` | Persisted int key/value store for player preferences |
| **gamestate** | `gamestate.h` | Score-attack shell, pre-run menu, pause, initials editor |
| **menu** | `menu.h` | Grid/list cursor with wrapping and a scrolling window |
| **clock** | `clock.h` | Frame counter, delta time, optional fixed timestep, easing |
| **theme** | `theme.h` | HSL→RGB palette generator, complementary colours |
| **ui_utils** | `ui_utils.h` | Design-space → TV-safe layout, text, panels, wrapping |

### Not in the engine, by design

`player`, `stars` and `characters` used to live here and now belong to the games
that own them. They encoded one game's decisions — jetman physics, a specific
starfield, a fixed character roster — and forced the engine to depend on that
game's `config.h`. If a second game needs something similar, generalise it then,
against two real examples.

### Screen geometry

Never hardcode 640×480. `renderer_screen_width()`/`_height()` report the running
video mode (NTSC 640×480, PAL 640×528). UI should be authored in the fixed
`UI_DESIGN_WIDTH`×`UI_DESIGN_HEIGHT` design space and drawn through the
`ui_map_*` helpers, which project it into the TV-safe area — consumer sets clip
roughly 5–10% of every edge.

### Audio format

Assets are raw signed 16-bit little-endian stereo at 48kHz:

```bash
ffmpeg -i in.ogg -f s16le -acodec pcm_s16le -ar 48000 -ac 2 out.pcm
```

Clips are held decoded in main RAM, so format is a memory decision before it is a
fidelity one:

| Format | Per second | Per minute |
|---|---|---|
| 48kHz stereo | ~192 KB | ~11.5 MB |
| 48kHz mono | ~96 KB | ~5.8 MB |
| 24kHz mono | ~48 KB | ~2.9 MB |

Against the Wii's 24MB, a two-minute track at 48kHz stereo does not fit at all.
Keep short effects at 48kHz stereo — they are what the player hears most
sharply — and pass a lower rate or mono for a long music loop via
`audio_play_music_fmt()`. Trim rather than stream.

### More than one player

`input_scan()` samples every connected controller, and every query takes a player
index: `input_pressed(1, INPUT_BTN_A)` is player two's A. Player one has a
shorthand — `input_a_pressed()` and friends are the same calls with the index
filled in — so a single-player game reads exactly as it did.

Three things a two-player game needs that per-button accessors could not express:

```c
input_pressed(p, INPUT_BTN_A);      // the frame it went down
input_held(p, INPUT_BTN_A);         // still down (blocking, walking, charging)
input_released(p, INPUT_BTN_A);     // the frame it came up
const InputPad *pad = input_snapshot(p);   // the whole frame, as a value
```

`input_snapshot()` is the one worth knowing about. A frame of input is a value
you can copy and keep, which is what an input buffer is made of — a motion input
is a pattern over roughly eight frames, and no amount of per-button accessors
will describe one. The engine deliberately stops here: buffering frames and
recognising patterns in them is a game's decision, not an engine's.

### Sprite sheets

A uniform grid of frames in one texture — cells of `frame_w` × `frame_h`, counted
left-to-right then top-to-bottom. This is the format SPRITE//FORGE exports and
all three MagmaCrunch engines read, so a sheet feeds adenosine, magnolia and
texastoast alike; the canonical spec is `adenosine/packages/rpg/API.md`, and
changing it is a three-repo change.

```c
SpriteSheet fighter;
sprite_sheet_load(&fighter, "sd:/apps/game/fighter.png", 64, 96, 32, 96);
sprite_sheet_draw(&fighter, frame, x, y);
sprite_sheet_draw_ex(&fighter, frame, x, y, 1.0f, 1.0f, facing_left, tint);
```

The origin is the *frame's*, not the sheet's, and is stated at load time — where
a character's feet are is a property of how the art was drawn, and re-deriving it
per draw site is how two call sites end up disagreeing about where the ground is.
Frame sizes that do not divide the image leave the sheet empty rather than
drawing the plausible part of a mis-exported asset.

Mirroring reflects a frame about its own origin: the origin's distance from the
left edge becomes its distance from the right, so a character anchored at the
front foot stays anchored there when they turn around. Reach for
`sprite_sheet_draw_ex()` rather than `GRRLIB_BMFX_FlipH()` — that one builds a
mirrored copy of the texture pixel by pixel, which is fine once at load time and
ruinous once per frame per character.

### Fixed timestep

`clock_dt()` reports however long the last frame really took, which is right for
anything continuous — a fade, a slide, a starfield. It is wrong for anything
whose rules are written in frames. Run "three frames of startup, twelve of
recovery" against a delta that varies with SD reads and the numbers stop meaning
anything: a combo that worked on a clean frame drops on a busy one.

The engine offers a fixed step and does not impose one. A game that never calls
`clock_set_fixed_hz()` sees no change at all.

```c
clock_set_fixed_hz(60);
...
for (int i = 0; i < clock_fixed_steps(); i++) world_step(clock_fixed_dt());
world_draw();                    // once, however many steps ran
```

Drawing stays outside the loop — stepping is how often the rules run, not how
often the screen is painted. A frame that owes more than `TIMESTEP_MAX_STEPS` was
a stall rather than a slow frame, and the backlog is dropped rather than repaid,
because a game that sprints to catch up after a load is worse than one that lost
the time.

## Used by

Each game now lives in one repository holding every version of it. The Wii port
built on magnolia is under `wii/`, beside the browser version it was ported
from — so the reference implementation for a behaviour is in the same checkout
as the port.

- [moonlight-drift](https://github.com/magmacrunch-media/moonlight-drift) — Jetman-style one-button cave flyer, 24 characters
- [george-boole](https://github.com/magmacrunch-media/george-boole) — logic-gate puzzle with per-mode leaderboards
- [texas-holdem-lava-dome](https://github.com/magmacrunch-media/texas-holdem-lava-dome) — solo Texas Hold'Em against a rising dome

A game's Makefile expects magnolia checked out beside its repository, so the
engine resolves at `../../magnolia` from inside the game's `wii/` folder.

## Dependencies

| Package | Source |
|---------|--------|
| devkitPPC | [devkitPro](https://devkitpro.org) |
| GRRLIB 4.6 | [github.com/GRRLIB/GRRLIB](https://github.com/GRRLIB/GRRLIB) |
| libogc | Bundled with devkitPro |
| ppc-libpng, ppc-freetype, ppc-libjpeg-turbo | `dkp-pacman` |
| libfat-ogc | `dkp-pacman` |
| libasnd (audio) | Bundled with devkitPro |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for code style, testing, and commit conventions.

## Font

Includes **Press Start 2P** by [CodeMan38](https://fonts.google.com/specimen/Press+Start+2P), embedded as a C array via `raw2c`.

Copyright 2012 The Press Start 2P Project Authors (cody@zone38.net), with
Reserved Font Name "Press Start 2P". Licensed under the SIL Open Font License
1.1 — the full text is in [font/OFL.txt](font/OFL.txt). That notice also lives
in the `.ttf`'s own name table, so it travels inside the embedded array and into
any binary built from it.

## License

Apache 2.0 — same as adenosine. The bundled font is the one exception: Press
Start 2P stays under OFL 1.1, as above.
