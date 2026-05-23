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

CFLAGS  := -std=c99 -Wall -Wextra -O2 -I$(RAYLIB)/include -I$(MINIH264_INC) -I$(MINIMP4_INC) -Isrc -Iengine/include -Itools -Ibuild -Ithird_party/cjson -Ithird_party/miniz
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

ENGINE_SRC := engine/game.c engine/map.c engine/fog.c engine/pack.c engine/tile.c engine/savegame.c engine/state_serialize.c engine/savepath.c engine/tables.c engine/adventure.c engine/resources.c engine/pending.c engine/flows.c engine/step.c engine/spells_adventure.c engine/fatal.c engine/assets_bytes.c engine/combat.c engine/combat_log.c
SHELL_SRC  := src/main.c src/shell_menu.c src/shell_tempdeath.c src/shell_weekend.c src/shell_audience.c src/shell_cheats.c src/shell_fastquit.c src/shell_frame.c src/shell_promptdispatch.c src/shell_actions.c src/shell_earlyexit.c src/assets.c src/pack_select.c src/recorder.c src/audio.c src/encode_mp4.c src/encode_mp4_h264.c src/encode_mp4_mux.c src/encode_dialog.c src/bfont.c src/tile_cache.c src/sprites.c src/views.c src/ui.c src/screenshot.c src/combat_loop.c src/combat_render.c src/palette.c src/chrome.c src/hud.c src/map_render.c src/overlay.c src/views_render.c src/input.c src/input_host.c src/frame_host.c src/prompt.c src/startup.c src/end_cartoon.c src/screens/home_castle.c src/screens/recruit_soldiers.c src/screens/own_castle.c src/screens/dwelling.c src/screens/alcove.c src/screens/end_game.c src/autoplay/core.c src/autoplay/flow.c src/autoplay/nav.c
TOOL_SRC   := tools/extract.c tools/extract_io.c tools/extract_unpack.c tools/extract_lzw.c tools/extract_vga.c tools/extract_png.c tools/extract_chrome.c tools/extract_gamejson.c
VENDOR_SRC := third_party/cjson/cJSON.c third_party/miniz/miniz.c

SRC := $(SHELL_SRC) $(ENGINE_SRC) $(TOOL_SRC) $(VENDOR_SRC)
SRC_DEV := $(SRC)
OUT := build/openbounty
OUT_RELEASE := build/openbounty-release

# Pack zips: one per top-level dir under assets/. Each becomes
# build/assets/<name>.openbounty next to the binary so discovery step 3
# (<exe-dir>/assets/*.openbounty) finds them from any cwd. Today only
# assets/kings-bounty/ exists, but the pattern handles N games.
PACK_NAMES := $(notdir $(patsubst %/,%,$(wildcard assets/*/)))
PACKS := $(addprefix build/assets/,$(addsuffix .openbounty,$(PACK_NAMES)))

OUT_TEST      := build/openbounty-test
OUT_ENGLIB    := build/libobengine.a
LIBTEST_STAMP := build/libtest-pass.stamp

all: $(OUT) $(OUT_TEST) $(OUT_ENGLIB) $(LIBTEST_STAMP) $(PACKS)

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

# Shell + tool object files. Compiled once, linked into each binary.
# Pattern rule below keeps the build incremental — touching one shell
# .c file rebuilds only that object plus the dependent binaries.
SHELL_OBJ := $(patsubst %.c,build/objs/shell/%.o,$(SHELL_SRC))
TOOL_OBJ  := $(patsubst %.c,build/objs/shell/%.o,$(TOOL_SRC))

build/objs/shell/%.o: %.c build/version.h | build
	@mkdir -p $(dir $@)
	gcc $(CFLAGS) -c $< -o $@

# The main binary depends on tests passing — `make` runs the unit
# suite before linking the game. A test failure stops the build.
#
# Game = libobengine.a (engine + vendored cJSON/miniz) + shell objects
# + tool objects (extract). No recompile of engine sources for the game.
$(OUT): $(SHELL_OBJ) $(TOOL_OBJ) $(OUT_ENGLIB) build/version.h build/test-pass.stamp | build
	gcc $(CFLAGS) $(SHELL_OBJ) $(TOOL_OBJ) $(OUT_ENGLIB) -o $(OUT) $(LDFLAGS)

# Each pack zip rebuilds when any file under its assets/<name>/ tree
# changes. Using $(shell find) at parse time means: run `make` after
# editing assets to repackage; touching a single asset is enough.
# The engine binary itself does the zipping via --pack-dir.
build/assets:
	mkdir -p build/assets

define PACK_RULE
build/assets/$(1).openbounty: $$(shell find assets/$(1) -type f \! -name '*.xcf' \! -name '*.psd' \! -name '*:Zone.Identifier' 2>/dev/null) $(OUT) | build/assets
	./$(OUT) --pack-dir assets/$(1) build/assets/$(1).openbounty
endef
$(foreach pn,$(PACK_NAMES),$(eval $(call PACK_RULE,$(pn))))

# Internal stamp: builds the test binary (rule below), runs it,
# touches a marker on success. Re-runs only when sources change.
build/test-pass.stamp: $(OUT_TEST)
	@./$(OUT_TEST) >/dev/null
	@touch $@

build:
	mkdir -p build

run: $(OUT)
	./$(OUT)

# Release build: same engine binary as `all`, just under a different
# name. Assets are no longer embedded — they ride alongside as
# build/assets/<game>.openbounty.
release: $(OUT_RELEASE) $(PACKS)

$(OUT_RELEASE): $(SHELL_OBJ) $(TOOL_OBJ) $(OUT_ENGLIB) build/version.h | build
	gcc $(CFLAGS) $(SHELL_OBJ) $(TOOL_OBJ) $(OUT_ENGLIB) -o $(OUT_RELEASE) $(LDFLAGS_RELEASE)

run-release: $(OUT_RELEASE)
	./$(OUT_RELEASE)

# ---------------------------------------------------------------------------
# Windows cross-compile (x64 + x86, static, single-binary with embedded assets)
# ---------------------------------------------------------------------------
WIN_CFLAGS_COMMON := -std=c99 -Wall -Wextra -O2 -Isrc -Iengine/include -Itools -Ibuild -Ithird_party/cjson -Ithird_party/miniz -I$(MINIH264_INC) -I$(MINIMP4_INC) -DWIN32 -D_WIN32
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
               -Isrc -Iengine/include -Itools -Ibuild -Ithird_party/cjson -Ithird_party/miniz
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
# tests/{unit,regression,e2e}/*.c with greatest as the framework).
# ---------------------------------------------------------------------------
TEST_SHARED  := tests/stubs.c tests/fixtures.c tests/main.c
TEST_UNIT    := $(wildcard tests/unit/*.c)
TEST_REGR    := $(wildcard tests/regression/*.c)
TEST_E2E     := $(wildcard tests/e2e/*.c)
TEST_ONLY_SRC := $(TEST_SHARED) $(TEST_UNIT) $(TEST_REGR) $(TEST_E2E)

# Unit-test binary: shell sources (minus main.c and the autoplay
# routine, which calls shell_run_game — a function only the game
# binary defines) + test sources + libobengine.a.
TEST_SRC := $(filter-out src/main.c src/autoplay/core.c src/autoplay/flow.c src/autoplay/nav.c,$(SHELL_SRC)) $(TOOL_SRC) $(TEST_ONLY_SRC)

$(OUT_TEST): $(TEST_SRC) $(OUT_ENGLIB) build/version.h | build
	gcc $(CFLAGS) -Ithird_party/greatest -Itests $(TEST_SRC) $(OUT_ENGLIB) -o $(OUT_TEST) $(LDFLAGS)

# ---------------------------------------------------------------------------
# libobengine.a — engine compiled as a static archive. Consumers link
# against this + their own renderer. cJSON and miniz are vendored inside
# (the archive is self-contained). External consumers add
# -Iengine/include and link -lobengine.
#
# Engine sources compile against engine/headless/raylib.h (the stub) so
# the library itself has no raylib dependency.
# ---------------------------------------------------------------------------
ENGLIB_SRC := $(ENGINE_SRC) $(VENDOR_SRC)
ENGLIB_OBJ_DIR := build/objs/englib
ENGLIB_OBJ := $(patsubst %.c,$(ENGLIB_OBJ_DIR)/%.o,$(ENGLIB_SRC))
ENGLIB_CFLAGS := -std=c99 -Wall -Wextra -O2 -fPIC -Iengine/headless -Iengine/include -Isrc -Ibuild -Ithird_party/cjson -Ithird_party/miniz -DOB_HEADLESS

$(ENGLIB_OBJ_DIR)/%.o: %.c | $(ENGLIB_OBJ_DIR) build/version.h
	@mkdir -p $(dir $@)
	gcc $(ENGLIB_CFLAGS) -c $< -o $@

$(ENGLIB_OBJ_DIR):
	mkdir -p $@

$(OUT_ENGLIB): $(ENGLIB_OBJ) | build
	ar rcs $@ $(ENGLIB_OBJ)

# Single test command. greatest runs the entire suite: unit tests,
# combat-formula digests, everything. The library-boundary check is a
# build-time link verification — $(LIBTEST_STAMP) is in $(all) so a
# regression where engine code depends on shell headers will fail at
# `make all` time.
test: $(OUT_TEST)
	@./$(OUT_TEST)

# Autoplay: runs the autoplay routine, which drives the real game
# binary through the input/frame shims and asserts on the end-state.
# Builds the game first (which already depends on `make test`), runs
# the routine with --seed 1, asserts exit 0. Window stays hidden;
# pass --visible to watch a run interactively, e.g.
#   ./build/openbounty --seed 1 --autoplay --visible
test-autoplay: $(OUT)
	@printf '[autoplay] '
	@if ./$(OUT) --seed 1 --autoplay >/tmp/openbounty-autoplay.log 2>&1; then \
	    echo PASS; \
	else \
	    echo FAIL; \
	    echo "--- log: /tmp/openbounty-autoplay.log ---"; \
	    tail -40 /tmp/openbounty-autoplay.log; \
	    exit 1; \
	fi

# ---------------------------------------------------------------------------
# Library boundary check. Compiles a minimal consumer against
# libobengine.a + engine/host_noop.c using ONLY engine include paths
# (no -Isrc) and only -lm -lpthread (no raylib, no X11). If the engine
# starts depending on shell headers or shell symbols, this build fails
# and `make all` fails. No binary is emitted — the link verifies symbol
# resolution and is discarded; a stamp file records success.
# ---------------------------------------------------------------------------
LIBTEST_CFLAGS := -std=c99 -Wall -Wextra -O2 -Iengine/headless -Iengine/include -Ithird_party/cjson
LIBTEST_LDFLAGS := -lm -lpthread

# --whole-archive forces every .o in libobengine.a to be pulled into
# the link, even if the consumer doesn't reference it. Without this,
# dead engine code that calls shell symbols would slip through because
# static-archive linking only pulls referenced objects. The boundary
# check needs to verify EVERY engine object resolves with only host
# callbacks; --whole-archive makes that comprehensive.
$(LIBTEST_STAMP): tests/library/consumer.c engine/host_noop.c $(OUT_ENGLIB) | build
	gcc $(LIBTEST_CFLAGS) tests/library/consumer.c engine/host_noop.c \
	    -Wl,--whole-archive $(OUT_ENGLIB) -Wl,--no-whole-archive \
	    -o $@.tmp $(LIBTEST_LDFLAGS)
	@rm -f $@.tmp
	@touch $@

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

.PHONY: all run release run-release windows windows-debug mac clean test test-autoplay extract extract-pack dist dist-linux dist-windows dist-mac
