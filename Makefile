# ---------------------------------------------------------------------------
# Linux (dev + release)
# ---------------------------------------------------------------------------
RAYLIB       := third_party/raylib-install
RAYLIB_WIN64 := third_party/raylib-install-win64
RAYLIB_WIN32 := third_party/raylib-install-win32
RAYLIB_MAC   := third_party/raylib-install-mac

# Single-header video pipeline: minih264 (encoder) + minimp4 (muxer).
# No -l flag — both libraries compile into src/encode_mp4.c.
MINIH264_INC := third_party/minih264
MINIMP4_INC  := third_party/minimp4

# Version: a single integer (1, 2, 3, ...). The release workflow passes
# OPENBOUNTY_VERSION explicitly from the dispatched release-N tag. For
# local dev builds with no override, derive from the most recent
# release-N tag (digits only) and fall back to 0 if there are no tags.
OPENBOUNTY_VERSION         ?= $(shell git tag --list 'release-*' 2>/dev/null | sed -n 's/^release-\([1-9][0-9]*\)$$/\1/p' | sort -n | tail -1 | grep . || echo 0)
OPENBOUNTY_VERSION_DISPLAY := build $(OPENBOUNTY_VERSION)
OPENBOUNTY_VERSION_SLUG    := build-$(OPENBOUNTY_VERSION)

CFLAGS  := -std=c99 -Wall -Wextra -O2 -I$(RAYLIB)/include -I$(MINIH264_INC) -I$(MINIMP4_INC) -Isrc -Itools -Ibuild -Ithird_party/cjson -Ithird_party/miniz
LDFLAGS := -L$(RAYLIB)/lib -lraylib -lm -lpthread -ldl -lrt -lX11
# Linux release: same CFLAGS as dev, but link libgcc statically so the
# binary runs on glibc systems without matching toolchain installed.
# Glibc itself stays dynamic (full-static glibc breaks NSS/X11 lookups).
# The extra -lXrandr/Xi/Xinerama/Xcursor/asound symbols come in via
# raylib's GLFW backend at runtime; static-linking exposes them.
LDFLAGS_RELEASE := -L$(RAYLIB)/lib -lraylib -lm -lpthread -ldl -lrt \
                   -lX11 -lXrandr -lXi -lXinerama -lXcursor \
                   -lasound \
                   -static-libgcc -Wl,-Bsymbolic

SRC := src/main.c src/map.c src/fog.c src/assets.c src/pack.c src/pack_select.c src/tile.c src/savegame.c src/state_serialize.c src/recorder.c src/audio.c src/encode_mp4.c src/encode_mp4_h264.c src/encode_mp4_mux.c src/encode_dialog.c src/savepath.c src/tables.c src/game.c src/bfont.c src/tile_cache.c src/sprites.c src/adventure.c src/views.c src/ui.c src/resources.c src/screenshot.c src/harness_input.c src/harness.c src/combat.c src/combat_render.c src/pending.c src/flows.c src/step.c src/spells_adventure.c src/palette.c src/chrome.c src/hud.c src/map_render.c src/overlay.c src/views_render.c src/input.c src/prompt.c src/startup.c src/end_cartoon.c src/fatal.c src/screens/home_castle.c src/screens/recruit_soldiers.c src/screens/own_castle.c src/screens/dwelling.c src/screens/alcove.c src/screens/end_game.c tools/extract.c tools/extract_io.c tools/extract_unpack.c tools/extract_lzw.c tools/extract_vga.c tools/extract_png.c tools/extract_chrome.c tools/extract_gamejson.c third_party/cjson/cJSON.c third_party/miniz/miniz.c
SRC_DEV := $(SRC)
OUT := build/openbounty
OUT_RELEASE := build/openbounty-release

# Pack zips: one per top-level dir under assets/. Each becomes
# build/assets/<name>.openbounty next to the binary so discovery step 3
# (<exe-dir>/assets/*.openbounty) finds them from any cwd. Today only
# assets/kings-bounty/ exists, but the pattern handles N games.
PACK_NAMES := $(notdir $(patsubst %/,%,$(wildcard assets/*/)))
PACKS := $(addprefix build/assets/,$(addsuffix .openbounty,$(PACK_NAMES)))

all: $(OUT) $(PACKS)

# Generate build/version.h from $(OPENBOUNTY_VERSION). Marked .PHONY-style
# (FORCE prereq) so it always runs — the cmp/mv inside only rewrites the
# file when the value actually changes, so unchanged versions don't
# trigger spurious downstream rebuilds. Every binary target lists this
# as a prerequisite, and main.c's --version / --help include it.
.PHONY: FORCE
FORCE:
build/version.h: FORCE | build
	@printf '#ifndef OB_VERSION_H\n#define OB_VERSION_H\n#define OPENBOUNTY_VERSION "%s"\n#endif\n' "$(OPENBOUNTY_VERSION)" > $@.tmp
	@if ! cmp -s $@.tmp $@ 2>/dev/null; then mv $@.tmp $@; else rm $@.tmp; fi

# The main binary depends on tests passing — `make` runs the unit
# suite before linking the game. A test failure stops the build.
$(OUT): $(SRC_DEV) build/version.h build/test-pass.stamp | build
	gcc $(CFLAGS) $(SRC_DEV) -o $(OUT) $(LDFLAGS)

build/mkpack: tools/mkpack.c third_party/miniz/miniz.c | build
	gcc -std=c99 -Wall -Wextra -O2 -Ithird_party/miniz \
	    tools/mkpack.c third_party/miniz/miniz.c -o $@

# Each pack zip rebuilds when any file under its assets/<name>/ tree
# changes. Using $(shell find) at parse time means: run `make` after
# editing assets to repackage; touching a single asset is enough.
build/assets:
	mkdir -p build/assets

define PACK_RULE
build/assets/$(1).openbounty: $$(shell find assets/$(1) -type f \! -name '*.xcf' \! -name '*.psd' \! -name '*:Zone.Identifier' 2>/dev/null) build/mkpack | build/assets
	./build/mkpack assets/$(1) build/assets/$(1).openbounty
endef
$(foreach pn,$(PACK_NAMES),$(eval $(call PACK_RULE,$(pn))))

# Internal stamp: builds the test binary (rule below), runs it,
# touches a marker on success. Re-runs only when sources change.
build/test-pass.stamp: build/openbounty-test
	@./build/openbounty-test >/dev/null
	@touch $@

build:
	mkdir -p build

run: $(OUT)
	./$(OUT)

# Release build: same engine binary as `all`, just under a different
# name. Assets are no longer embedded — they ride alongside as
# build/assets/<game>.openbounty.
release: $(OUT_RELEASE) $(PACKS)

$(OUT_RELEASE): $(SRC) build/version.h | build
	gcc $(CFLAGS) $(SRC) -o $(OUT_RELEASE) $(LDFLAGS_RELEASE)

run-release: $(OUT_RELEASE)
	./$(OUT_RELEASE)

# ---------------------------------------------------------------------------
# Windows cross-compile (x64 + x86, static, single-binary with embedded assets)
# ---------------------------------------------------------------------------
WIN_CFLAGS_COMMON := -std=c99 -Wall -Wextra -O2 -Isrc -Itools -Ibuild -Ithird_party/cjson -Ithird_party/miniz -I$(MINIH264_INC) -I$(MINIMP4_INC) -DWIN32 -D_WIN32
# -mwindows hides the console; keep it for a GUI app.
# -static links libgcc/libstdc++/winpthread statically so no DLLs are needed.
# --stack=8MB matches the Linux default. Several main.c locals are big
# (Resources is ~3.9 MB, Map ~848 KB) — Windows default 1 MB stack
# overflows on launch. Linux ships with 8 MB by default; mirror that.
WIN_LDFLAGS_COMMON := -lraylib -lopengl32 -lgdi32 -lwinmm -lpthread -lws2_32 -mwindows -static -static-libgcc -Wl,--stack,8388608

WIN64_CC := x86_64-w64-mingw32-gcc
WIN32_CC := i686-w64-mingw32-gcc
WIN64_CFLAGS  := $(WIN_CFLAGS_COMMON) -I$(RAYLIB_WIN64)/include
WIN64_LDFLAGS := -L$(RAYLIB_WIN64)/lib $(WIN_LDFLAGS_COMMON)
WIN32_CFLAGS  := $(WIN_CFLAGS_COMMON) -I$(RAYLIB_WIN32)/include
WIN32_LDFLAGS := -L$(RAYLIB_WIN32)/lib $(WIN_LDFLAGS_COMMON)

OUT_WIN64 := build/openbounty-x64.exe
OUT_WIN32 := build/openbounty-x86.exe

windows: $(OUT_WIN64) $(OUT_WIN32)

$(OUT_WIN64): $(SRC) build/version.h
	$(WIN64_CC) $(WIN64_CFLAGS) $(SRC) -o $(OUT_WIN64) $(WIN64_LDFLAGS)

$(OUT_WIN32): $(SRC) build/version.h
	$(WIN32_CC) $(WIN32_CFLAGS) $(SRC) -o $(OUT_WIN32) $(WIN32_LDFLAGS)

# Diagnostic Windows builds: same as `windows` but WITHOUT -mwindows,
# so the .exe keeps an attached console. Run from cmd.exe / PowerShell
# to see stderr, raylib trace logs, and printf output. Useful for
# triaging "the .exe doesn't run, nothing happens" failures.
WIN_LDFLAGS_DEBUG := $(filter-out -mwindows,$(WIN_LDFLAGS_COMMON))
WIN64_LDFLAGS_DEBUG := -L$(RAYLIB_WIN64)/lib $(WIN_LDFLAGS_DEBUG)
WIN32_LDFLAGS_DEBUG := -L$(RAYLIB_WIN32)/lib $(WIN_LDFLAGS_DEBUG)
OUT_WIN64_DEBUG := build/openbounty-x64-debug.exe
OUT_WIN32_DEBUG := build/openbounty-x86-debug.exe

windows-debug: $(OUT_WIN64_DEBUG) $(OUT_WIN32_DEBUG)

$(OUT_WIN64_DEBUG): $(SRC) build/version.h
	$(WIN64_CC) $(WIN64_CFLAGS) $(SRC) -o $(OUT_WIN64_DEBUG) $(WIN64_LDFLAGS_DEBUG)

$(OUT_WIN32_DEBUG): $(SRC) build/version.h
	$(WIN32_CC) $(WIN32_CFLAGS) $(SRC) -o $(OUT_WIN32_DEBUG) $(WIN32_LDFLAGS_DEBUG)

# ---------------------------------------------------------------------------
# macOS build (universal binary: arm64 + x86_64, embedded assets, static
# raylib). Intended for CI on a GitHub macos-* runner — Apple Silicon and
# Intel Macs both run the resulting binary. The shipped libraylib.a is a
# universal archive (Mach-O fat) so a single clang invocation produces a
# fat output without needing a second compile pass.
# ---------------------------------------------------------------------------
MAC_CC      := clang
MAC_ARCHES  := -arch arm64 -arch x86_64
MAC_CFLAGS  := -std=c99 -Wall -Wextra -O2 $(MAC_ARCHES) \
               -I$(RAYLIB_MAC)/include -I$(MINIH264_INC) -I$(MINIMP4_INC) \
               -Isrc -Itools -Ibuild -Ithird_party/cjson -Ithird_party/miniz
# -Itools is required for src/main.c's #include "extract.h".
# (Linux/Windows CFLAGS already have it; mac was missing.)
# raylib on macOS links against several system frameworks for windowing,
# input, and OpenGL. -framework GLUT is unused at runtime but raylib's
# OpenGL glue references it; harmless to include.
MAC_LDFLAGS := $(MAC_ARCHES) -L$(RAYLIB_MAC)/lib -lraylib -lpthread \
               -framework Cocoa -framework IOKit -framework CoreVideo \
               -framework OpenGL

OUT_MAC := build/openbounty-mac

mac: $(OUT_MAC)

$(OUT_MAC): $(SRC) build/version.h
	$(MAC_CC) $(MAC_CFLAGS) $(SRC) -o $(OUT_MAC) $(MAC_LDFLAGS)

# ---------------------------------------------------------------------------
# Distribution archives (consumed by GitHub Actions release workflow).
# Each `dist-<platform>` target stages the platform-specific binary plus
# README.txt (rendered from dist/README.txt.in with $(OPENBOUNTY_VERSION)
# substituted), LICENSE, and NOTICES.md, then archives them. The output
# lands in dist/ next to the build/ tree.
#
# CRITICAL: no .openbounty pack file is ever included in a release
# archive — the asset pack is DOS-extracted and copyright-restricted.
# The release workflow has a CI-side belt-and-braces check that fails
# if any dist/*.tar.gz or dist/*.zip contains a .openbounty file.
# ---------------------------------------------------------------------------
DIST    := dist
STAGING := build/staging

dist: dist-linux dist-windows dist-mac

dist-linux: $(OUT_RELEASE)
	@rm -rf $(STAGING)/linux && mkdir -p $(STAGING)/linux/openbounty-$(OPENBOUNTY_VERSION_SLUG)
	cp $(OUT_RELEASE) $(STAGING)/linux/openbounty-$(OPENBOUNTY_VERSION_SLUG)/openbounty
	sed "s/<version>/$(OPENBOUNTY_VERSION_DISPLAY)/g" $(DIST)/README.txt.in > $(STAGING)/linux/openbounty-$(OPENBOUNTY_VERSION_SLUG)/README.txt
	cp LICENSE NOTICES.md $(STAGING)/linux/openbounty-$(OPENBOUNTY_VERSION_SLUG)/
	@mkdir -p $(DIST)
	tar -czf $(DIST)/openbounty-$(OPENBOUNTY_VERSION_SLUG)-linux-x86_64.tar.gz \
	    -C $(STAGING)/linux openbounty-$(OPENBOUNTY_VERSION_SLUG)

dist-windows: $(OUT_WIN64) $(OUT_WIN32)
	@for arch in x86_64 i686; do \
	  case $$arch in x86_64) src=$(OUT_WIN64);; i686) src=$(OUT_WIN32);; esac; \
	  rm -rf $(STAGING)/win-$$arch && mkdir -p $(STAGING)/win-$$arch/openbounty-$(OPENBOUNTY_VERSION_SLUG); \
	  cp $$src $(STAGING)/win-$$arch/openbounty-$(OPENBOUNTY_VERSION_SLUG)/openbounty.exe; \
	  sed "s/<version>/$(OPENBOUNTY_VERSION_DISPLAY)/g" $(DIST)/README.txt.in > $(STAGING)/win-$$arch/openbounty-$(OPENBOUNTY_VERSION_SLUG)/README.txt; \
	  cp LICENSE NOTICES.md $(STAGING)/win-$$arch/openbounty-$(OPENBOUNTY_VERSION_SLUG)/; \
	  mkdir -p $(DIST); \
	  (cd $(STAGING)/win-$$arch && zip -qr ../../../$(DIST)/openbounty-$(OPENBOUNTY_VERSION_SLUG)-windows-$$arch.zip openbounty-$(OPENBOUNTY_VERSION_SLUG)); \
	done

dist-mac: $(OUT_MAC)
	@rm -rf $(STAGING)/mac && mkdir -p $(STAGING)/mac/openbounty-$(OPENBOUNTY_VERSION_SLUG)
	cp $(OUT_MAC) $(STAGING)/mac/openbounty-$(OPENBOUNTY_VERSION_SLUG)/openbounty
	codesign --force --sign - --options runtime $(STAGING)/mac/openbounty-$(OPENBOUNTY_VERSION_SLUG)/openbounty
	sed "s/<version>/$(OPENBOUNTY_VERSION_DISPLAY)/g" $(DIST)/README.txt.in > $(STAGING)/mac/openbounty-$(OPENBOUNTY_VERSION_SLUG)/README.txt
	cp LICENSE NOTICES.md $(STAGING)/mac/openbounty-$(OPENBOUNTY_VERSION_SLUG)/
	@mkdir -p $(DIST)
	(cd $(STAGING)/mac && zip -qr ../../../$(DIST)/openbounty-$(OPENBOUNTY_VERSION_SLUG)-macos-universal.zip openbounty-$(OPENBOUNTY_VERSION_SLUG))

# ---------------------------------------------------------------------------
# Unit tests (second binary, links the same SRC minus main.c plus
# tests/unit/*.c with greatest as the framework).
# ---------------------------------------------------------------------------
TEST_SRC := $(filter-out src/main.c,$(SRC)) \
            tests/unit/stubs.c \
            tests/unit/fixtures.c \
            tests/unit/main.c \
            tests/unit/test_terrain.c \
            tests/unit/test_map.c \
            tests/unit/test_chest.c \
            tests/unit/test_save.c \
            tests/unit/test_tables.c \
            tests/unit/test_state.c \
            tests/unit/test_combat_rng.c \
            tests/unit/test_combat_unit.c \
            tests/unit/test_combat_geom.c \
            tests/unit/test_combat_damage.c \
            tests/unit/test_combat_spells.c \
            tests/unit/test_resources.c \
            tests/unit/test_save_more.c \
            tests/unit/test_map_more.c \
            tests/unit/test_score.c \
            tests/unit/test_game_flow.c \
            tests/unit/test_combat_ai.c \
            tests/unit/test_save_fixture.c \
            tests/unit/test_contract.c \
            tests/unit/test_economy.c \
            tests/unit/test_fog.c \
            tests/unit/test_map_overlay.c \
            tests/unit/test_state_json.c \
            tests/unit/test_tables_defensive.c \
            tests/unit/test_pack.c \
            tests/unit/test_combat_input.c

OUT_TEST := build/openbounty-test

$(OUT_TEST): $(TEST_SRC) build/version.h | build
	gcc $(CFLAGS) -Ithird_party/greatest -Itests/unit $(TEST_SRC) -o $(OUT_TEST) $(LDFLAGS)

# ---------------------------------------------------------------------------
# Tests:
#   make test                    — runs everything: unit suite, combat digest,
#                                  and every scenario under tools/scenarios/.
#   make test-scenario FILE=path — runs one scenario file. SLOW=1 paces input
#                                  to ~600ms/key so you can watch the window.
# ---------------------------------------------------------------------------
build/openbounty-playtest: tools/playtest.c tools/playtest_lib.c tools/scenario.c third_party/cjson/cJSON.c | build
	gcc -std=c99 -Wall -Wextra -O2 -Ithird_party/cjson -Itools \
	    tools/playtest.c tools/playtest_lib.c tools/scenario.c \
	    third_party/cjson/cJSON.c \
	    -o build/openbounty-playtest

# Default `test` runs the fast unit suite only. Use `test-combat` for
# the combat digest checks, `test-scenarios` for scripted full-game
# scenarios, or `test-all` to run all three in sequence.
test: $(OUT_TEST)
	@./$(OUT_TEST)

test-combat: $(OUT)
	@./scripts/combat_regression.sh

test-scenarios: $(OUT) build/openbounty-playtest
	@./build/openbounty-playtest --suite tools/scenarios --filter '!nightly,!flaky'

test-all: test test-combat test-scenarios

test-scenario: $(OUT) build/openbounty-playtest
	@./build/openbounty-playtest --scenario $(FILE) $(if $(SLOW),--slow,)

# ---------------------------------------------------------------------------
# extract — produce a .openbounty pack (or a loose tree, with --out-dir)
# from a user's copy of the DOS distribution (KB.EXE + 256.CC). The
# extractor is now part of the engine binary; these targets are thin
# wrappers around `./build/openbounty --extract`.
# ---------------------------------------------------------------------------
extract: $(OUT)
	@./$(OUT) --extract $(EXTRACT_ARGS)

# Regenerate the shipped asset tree (assets/kings-bounty/) from the
# user's DOS files at legacy/bin/. Run this after any extractor change
# that affects game.json or the asset outputs, then commit the diff.
extract-pack: $(OUT)
	@./$(OUT) --extract --out-dir assets/kings-bounty

# ---------------------------------------------------------------------------
# clean removes generated build/ + dist artifacts, but keeps dist/README.txt.in
# (a committed source template that the dist-* targets render with the
# current version).
clean:
	rm -rf build
	rm -f dist/*.tar.gz dist/*.zip

.PHONY: all run release run-release windows windows-debug mac clean test test-combat test-scenarios test-all test-scenario extract extract-pack dist dist-linux dist-windows dist-mac
