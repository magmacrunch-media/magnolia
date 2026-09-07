#---------------------------------------------------------------------------------
# magnolia - Wii game engine library by magmacrunch
#---------------------------------------------------------------------------------
# Games consume the sources directly; add this engine to your own Makefile with:
#   SOURCES  := source ../magnolia/source ../magnolia/font
#   INCLUDES := ../magnolia ../magnolia/source ../magnolia/font source
#
# This Makefile builds the engine STANDALONE into libmagnolia.a. Its only purpose
# is to prove the engine compiles with no game on the include path -- if engine
# code ever reaches for a game header again, this build fails immediately rather
# than the coupling being discovered by the next game months later.
#
#   make          build libmagnolia.a
#   make test     run all host-side tests (no devkitPPC, no console)
#   make test-menu, test-gamestate, test-input, ...   run just one of them
#   make clean
#---------------------------------------------------------------------------------

# The host tests deliberately need no cross-compiler: requiring devkitPPC to run
# them would put them out of reach on the machine where they are most useful.
# Named once, so the guard below and the rules further down cannot drift apart --
# `make test-menu` on a laptop with no cross-compiler has to work too.
HOST_TESTS := test-storage test-menu test-gamestate test-theme test-input test-timestep \
              test-ui-geom test-glyphcache

ifeq ($(filter test $(HOST_TESTS),$(MAKECMDGOALS)),)
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC. export DEVKITPPC=<path to>devkitPPC")
endif
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO. export DEVKITPRO=<path to>devkitPro")
endif
endif

PREFIX  := $(DEVKITPPC)/bin/powerpc-eabi-
CC      := $(PREFIX)gcc
AR      := $(PREFIX)ar

TARGET  := libmagnolia.a
BUILD   := build

MACHDEP := -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float

# Deliberately engine-only: source/, font/ and the toolchain. No game directory.
INCLUDE := -I$(CURDIR) -I$(CURDIR)/source -I$(CURDIR)/font \
           -I$(DEVKITPRO)/libogc/include \
           -I$(DEVKITPRO)/portlibs/wii/include \
           -I$(DEVKITPRO)/portlibs/ppc/include \
           -I$(DEVKITPRO)/portlibs/ppc/include/freetype2

CFLAGS  := -g -O2 -Wall -Wextra $(MACHDEP) $(INCLUDE)

SOURCES := $(wildcard source/*.c) $(wildcard font/*.c)
OBJS    := $(patsubst %.c,$(BUILD)/%.o,$(SOURCES))

.PHONY: all clean test $(HOST_TESTS)

all: $(TARGET)

# Anything in source/ that is free of libogc is tested on the machine you are
# sitting at, in a second, with no emulator in the loop. That matters more than
# it sounds: an emulated SD card can refuse every write while reporting itself
# mounted, and a test that runs on the host is the one that can tell a broken
# save from a broken card.
#
# The source list per binary is written out rather than wildcarded. It is the
# record of which engine modules are host-clean -- a wildcard would cheerfully
# try to link renderer.c and fail with something far less informative.
HOSTCC     ?= cc
# -Werror because CONTRIBUTING already requires warning-clean builds, and
# without it that rule is honour-system: CI stays green on a new warning and
# nobody finds out until the warnings are too many to read. Only the host
# tests get it. The cross build is left alone deliberately -- a devkitPPC
# bump can introduce warnings inside third-party headers, and stopping the
# console build over someone else's header is a different trade.
HOSTCFLAGS := -Wall -Wextra -Werror -I source -I tests

test: $(HOST_TESTS)

test-storage: | $(BUILD)
	@$(HOSTCC) $(HOSTCFLAGS) -o $(BUILD)/$@ \
	    tests/test_storage.c source/prefs.c source/scoring.c
	@$(BUILD)/$@

test-menu: | $(BUILD)
	@$(HOSTCC) $(HOSTCFLAGS) -o $(BUILD)/$@ \
	    tests/test_menu.c source/menu.c
	@$(BUILD)/$@

# gamestate.c reaches libogc only through input.h, so the test binary links a
# host stand-in for the hardware half instead of source/input.c. The seam was
# already there; nothing in the shipping code changes to make this possible.
# input_state.c is the real thing -- the fake only says which buttons are down,
# so the presses and repeats these cases see are computed by shipping code.
test-gamestate: | $(BUILD)
	@$(HOSTCC) $(HOSTCFLAGS) -o $(BUILD)/$@ \
	    tests/test_gamestate.c tests/fake_input.c \
	    source/input_state.c source/gamestate.c source/scoring.c
	@$(BUILD)/$@

# The edge and auto-repeat arithmetic, split out of input.c so it could be
# asserted rather than eyeballed on a console with two controllers in hand.
test-input: | $(BUILD)
	@$(HOSTCC) $(HOSTCFLAGS) -o $(BUILD)/$@ \
	    tests/test_input.c source/input_state.c
	@$(BUILD)/$@

# The fixed-step accumulator. Whether a second of real time always buys the
# same number of logic steps is an arithmetic claim, and this is where it can
# be one -- clock.c itself cannot be built here, since it is a call to gettime().
test-timestep: | $(BUILD)
	@$(HOSTCC) $(HOSTCFLAGS) -o $(BUILD)/$@ \
	    tests/test_timestep.c source/timestep.c
	@$(BUILD)/$@

# The safe-area geometry, the design-space projection and the word wrap, split
# out of ui_utils.c so they could be asserted. The rest of that file is GRRLIB
# calls; this half decides where every glyph lands, including on the video mode
# most games are never run against.
test-ui-geom: | $(BUILD)
	@$(HOSTCC) $(HOSTCFLAGS) -o $(BUILD)/$@ 	    tests/test_ui_geom.c source/ui_geom.c
	@$(BUILD)/$@

# theme.c is nothing but arithmetic and was unreachable here only because its
# header asked for libogc to get one typedef. Guarded now, so it runs. -lm
# because the HSL conversion is the one engine module that uses libm.
test-theme: | $(BUILD)
	@$(HOSTCC) $(HOSTCFLAGS) -o $(BUILD)/$@ \
	    tests/test_theme.c source/theme.c -lm
	@$(BUILD)/$@

# glyphcache.c is the slot bookkeeping behind text.c, and is free of GRRLIB,
# FreeType and libogc for exactly that reason -- see the header. text.c itself
# is the half that talks to the hardware and is not testable here.
test-glyphcache: | $(BUILD)
	@$(HOSTCC) $(HOSTCFLAGS) -o $(BUILD)/$@ \
	    tests/test_glyphcache.c source/glyphcache.c
	@$(BUILD)/$@

$(TARGET): $(OBJS)
	@echo "archiving ... $@"
	@$(AR) rcs $@ $(OBJS)
	@echo "built $@ ($(words $(OBJS)) objects)"

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "$<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET)
