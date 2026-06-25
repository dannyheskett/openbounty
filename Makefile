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

# Build type: `make` is DEBUG (-O0 -g, fast to compile, debuggable);
# `make release` is OPTIMIZED + portable (-O2, static libgcc so the binary
# runs on other glibc machines). The release target re-invokes make with
# BUILD=release, which swaps these flags.
BUILD ?= debug
CFLAGS_debug   := -O0 -g
CFLAGS_release := -O2 -DNDEBUG
# Extra flags injected into every object group (engine lib, autoplay, shell).
# Used for temporary build-time switches like -DOB_TRACE_PRES. Empty by default.
OB_EXTRA_CFLAGS ?=
CFLAGS  := -std=c99 -Wall -Wextra $(CFLAGS_$(BUILD)) $(OB_EXTRA_CFLAGS) -I$(RAYLIB)/include -I$(MINIH264_INC) -I$(MINIMP4_INC) -Isrc -Iengine/include -Iautoplay -Itools -Ibuild -Ithird_party/cjson -Ithird_party/miniz
# Debug link: normal dynamic linking against the system libraries.
LDFLAGS_debug   := -L$(RAYLIB)/lib -lraylib -lm -lpthread -ldl -lrt -lX11
# Release link: libgcc static so the binary runs on glibc systems without a
# matching toolchain installed. Glibc itself stays dynamic (full-static glibc
# breaks NSS/X11 lookups). The extra -lXrandr/Xi/Xinerama/Xcursor/asound
# symbols come in via raylib's GLFW backend at runtime; static-linking exposes
# them.
LDFLAGS_release := -L$(RAYLIB)/lib -lraylib -lm -lpthread -ldl -lrt \
                   -lX11 -lXrandr -lXi -lXinerama -lXcursor \
                   -lasound \
                   -static-libgcc -Wl,-Bsymbolic
LDFLAGS := $(LDFLAGS_$(BUILD))

ENGINE_SRC := engine/game.c engine/map.c engine/fog.c engine/pack.c engine/tile.c engine/savegame.c engine/state_serialize.c engine/savepath.c engine/tables.c engine/adventure.c engine/resources.c engine/pending.c engine/player_io.c engine/flows.c engine/flow_resolve.c engine/step.c engine/spells_adventure.c engine/fatal.c engine/assets_bytes.c engine/combat.c engine/combat_log.c
# Autoplay module (docs/OPENBOUNTY-SPEC.md §36). Engine-only (no raylib, no src/):
# the objects link into the game/release binaries (so --autoplay [--headless]
# works on the single main binary, AP-084) and into the test binary, and are
# linked engine-pure by the library boundary check (AP-041/AP-120). No separate
# autoplay binary exists. Defined here so the binary rules below can reference it.
AUTOPLAY_SRC := autoplay/autoplay.c autoplay/goals.c autoplay/worldsnap.c autoplay/plan.c autoplay/prereq.c autoplay/recording.c autoplay/primitives.c autoplay/planner.c autoplay/exec_move.c autoplay/exec_fight.c autoplay/exec_recruit.c autoplay/exec_town.c
AUTOPLAY_OBJ_DIR := build/$(BUILD)/objs/autoplay
AUTOPLAY_OBJ     := $(patsubst %.c,$(AUTOPLAY_OBJ_DIR)/%.o,$(AUTOPLAY_SRC))
SHELL_SRC  := src/main.c src/shell_menu.c src/shell_tempdeath.c src/shell_weekend.c src/shell_audience.c src/shell_cheats.c src/shell_gate.c src/shell_fastquit.c src/shell_frame.c src/shell_promptdispatch.c src/shell_actions.c src/shell_autoplay.c src/shell_earlyexit.c src/assets.c src/pack_select.c src/recorder.c src/audio.c src/encode_mp4.c src/encode_mp4_h264.c src/encode_mp4_mux.c src/encode_dialog.c src/bfont.c src/tile_cache.c src/sprites.c src/views.c src/ui.c src/screenshot.c src/combat_loop.c src/combat_render.c src/combat_replay.c src/palette.c src/chrome.c src/hud.c src/map_render.c src/overlay.c src/views_render.c src/input.c src/input_host.c src/frame_host.c src/prompt.c src/startup.c src/end_cartoon.c src/screens/home_castle.c src/screens/recruit_soldiers.c src/screens/own_castle.c src/screens/dwelling.c src/screens/alcove.c src/screens/end_game.c
TOOL_SRC   := tools/extract.c tools/extract_io.c tools/extract_unpack.c tools/extract_lzw.c tools/extract_vga.c tools/extract_png.c tools/extract_chrome.c tools/extract_gamejson.c
VENDOR_SRC := third_party/cjson/cJSON.c third_party/miniz/miniz.c

# Full single-translation-unit source list for the cross-compile targets
# (windows, mac) which build from $(SRC) directly rather than the per-object
# Linux path. MUST include $(AUTOPLAY_SRC): src/shell_autoplay.c + src/main.c
# include autoplay.h and call into the autoplay module, so a $(SRC) build that
# omits it fails to compile/link (the --autoplay flag lives on the main binary).
SRC := $(SHELL_SRC) $(ENGINE_SRC) $(AUTOPLAY_SRC) $(TOOL_SRC) $(VENDOR_SRC)
SRC_DEV := $(SRC)
# One game binary. `make` -> build/debug/openbounty ; `make release` ->
# build/release/openbounty (BUILD=release). Per-build object dirs below keep
# the two configs from clobbering each other's .o files.
OUT := build/$(BUILD)/openbounty

# Pack zips: one per top-level dir under assets/. Each becomes
# build/$(BUILD)/assets/<name>.openbounty — IN THE BINARY'S OWN DIRECTORY so
# discovery step 3 (<exe-dir>/assets/*.openbounty) finds them next to the
# binary from any cwd. Built per-config because the binary lives under
# build/$(BUILD)/. Today only assets/kings-bounty/ exists; the pattern
# handles N games.
PACK_NAMES := $(notdir $(patsubst %/,%,$(wildcard assets/*/)))
PACK_DIR   := build/$(BUILD)/assets
PACKS := $(addprefix $(PACK_DIR)/,$(addsuffix .openbounty,$(PACK_NAMES)))

OUT_TEST      := build/openbounty-test
OUT_ENGLIB    := build/libobengine.a
LIBTEST_STAMP := build/libtest-pass.stamp

# Default build: compile + link the game and its asset packs. Nothing else —
# no test binary, no test run. Use `make test` to build and run the suite.
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

# Shell + tool object files. Compiled once, linked into each binary.
# Pattern rule below keeps the build incremental — touching one shell
# .c file rebuilds only that object plus the dependent binaries.
SHELL_OBJ := $(patsubst %.c,build/$(BUILD)/objs/shell/%.o,$(SHELL_SRC))
TOOL_OBJ  := $(patsubst %.c,build/$(BUILD)/objs/shell/%.o,$(TOOL_SRC))

build/$(BUILD)/objs/shell/%.o: %.c build/version.h | build
	@mkdir -p $(dir $@)
	gcc $(CFLAGS) -MMD -MP -c $< -o $@

# Game = libobengine.a (engine + vendored cJSON/miniz) + shell objects
# + tool objects (extract). No recompile of engine sources for the game.
# `make` only compiles + links — it does NOT run the tests (use `make test`).
$(OUT): $(SHELL_OBJ) $(TOOL_OBJ) $(AUTOPLAY_OBJ) $(OUT_ENGLIB) build/version.h | build
	@mkdir -p $(dir $@)
	gcc $(CFLAGS) $(SHELL_OBJ) $(TOOL_OBJ) $(AUTOPLAY_OBJ) $(OUT_ENGLIB) -o $(OUT) $(LDFLAGS)

# Each pack zip rebuilds when any file under its assets/<name>/ tree
# changes. Using $(shell find) at parse time means: run `make` after
# editing assets to repackage; touching a single asset is enough.
# The engine binary itself does the zipping via --pack-dir.
$(PACK_DIR):
	mkdir -p $(PACK_DIR)

define PACK_RULE
$(PACK_DIR)/$(1).openbounty: $$(shell find assets/$(1) -type f \! -name '*.xcf' \! -name '*.psd' \! -name '*:Zone.Identifier' 2>/dev/null) $(OUT) | $(PACK_DIR)
	./$(OUT) --pack-dir assets/$(1) $(PACK_DIR)/$(1).openbounty
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

# Release build: the SAME game binary, optimized (-O2) and portably linked.
# Re-invokes make with BUILD=release so all flags/paths switch consistently;
# the output is build/release/openbounty (vs build/debug/openbounty for `make`).
# Target 'all' (not $(OUT)/$(PACKS)) — those expand in THIS make where
# BUILD=debug, so passing them would re-request the debug paths. 'all' is
# re-evaluated by the sub-make under BUILD=release and resolves to the
# release paths.
release:
	$(MAKE) BUILD=release all

run-release: release
	./build/release/openbounty

# ---------------------------------------------------------------------------
# Windows cross-compile (x64 + x86, static, single-binary with embedded assets)
# ---------------------------------------------------------------------------
WIN_CFLAGS_COMMON := -std=c99 -Wall -Wextra -O2 -Isrc -Iengine/include -Iautoplay -Itools -Ibuild -Ithird_party/cjson -Ithird_party/miniz -I$(MINIH264_INC) -I$(MINIMP4_INC) -DWIN32 -D_WIN32
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
               -Isrc -Iengine/include -Iautoplay -Itools -Ibuild -Ithird_party/cjson -Ithird_party/miniz
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

dist-linux: release
	@rm -rf $(STAGING)/linux && mkdir -p $(STAGING)/linux/openbounty-$(OPENBOUNTY_VERSION_SLUG)
	cp build/release/openbounty $(STAGING)/linux/openbounty-$(OPENBOUNTY_VERSION_SLUG)/openbounty
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
TEST_AUTOPLAY := $(wildcard tests/autoplay/*.c)
TEST_ONLY_SRC := $(TEST_SHARED) $(TEST_UNIT) $(TEST_REGR) $(TEST_E2E) $(TEST_AUTOPLAY)

# Unit-test binary: shell sources (minus main.c, which defines main())
# + test sources + the autoplay module sources (minus its runner main())
# + libobengine.a. The test binary links the shell, so it may exercise
# autoplay_run directly; the engine-only boundary is enforced separately by
# the $(OUT_AUTOPLAY) link, not here.
TEST_SRC := $(filter-out src/main.c,$(SHELL_SRC)) $(TOOL_SRC) \
            $(AUTOPLAY_SRC) \
            $(TEST_ONLY_SRC)

$(OUT_TEST): $(TEST_SRC) $(OUT_ENGLIB) build/version.h | build
	gcc $(CFLAGS) -Iautoplay -Ithird_party/greatest -Itests $(TEST_SRC) $(OUT_ENGLIB) -o $(OUT_TEST) $(LDFLAGS)

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
ENGLIB_CFLAGS := -std=c99 -Wall -Wextra -O2 -fPIC $(OB_EXTRA_CFLAGS) -Iengine/headless -Iengine/include -Ibuild -Ithird_party/cjson -Ithird_party/miniz -DOB_HEADLESS

$(ENGLIB_OBJ_DIR)/%.o: %.c | $(ENGLIB_OBJ_DIR) build/version.h
	@mkdir -p $(dir $@)
	gcc $(ENGLIB_CFLAGS) -MMD -MP -c $< -o $@

$(ENGLIB_OBJ_DIR):
	mkdir -p $@

# Header-dependency tracking. -MMD -MP (above) emits a .d file next to
# each .o listing the headers it included; -include pulls those in so an
# edited header (e.g. a struct-layout change in internal.h) forces a
# rebuild of every object that includes it. Without this, the per-object
# rules track only the .c file, so an incremental `make` after editing a
# header produces STALE objects compiled against the old layout — a
# notorious source of mismatched-struct segfaults. The minus on -include
# suppresses the not-found warning on a clean tree.
ALL_OBJS := $(SHELL_OBJ) $(TOOL_OBJ) $(ENGLIB_OBJ)
-include $(ALL_OBJS:.o=.d)

$(OUT_ENGLIB): $(ENGLIB_OBJ) | build
	ar rcs $@ $(ENGLIB_OBJ)

# Single test command. greatest runs the entire suite: unit tests,
# combat-formula digests, everything. The library-boundary check is a
# build-time link verification — $(LIBTEST_STAMP) is in $(all) so a
# regression where engine code depends on shell headers will fail at
# `make all` time.
test: $(OUT_TEST)
	@./$(OUT_TEST)

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
#
# This link ALSO includes the autoplay objects ($(AUTOPLAY_OBJ)) so the
# autoplay module's engine-only boundary is build-enforced too (AP-041/AP-120)
# without a separate binary: autoplay has no main() of its own, so consumer.c
# provides the entry point, and the link uses only -lm -lpthread (no raylib,
# no -Isrc). If any autoplay (or engine) object reaches a shell symbol, this
# fails and `make all` fails. No binary is emitted — the link is discarded.
# Uniform player-IO emit guard (engine/include/player_io.h, M6). The engine must
# raise informational messages ONLY through the player-IO queue
# (player_io_message), never the open_dialog host callback directly — that was the
# pre-refactor "out-of-band" path that let the shell and autoplay diverge. Engine
# source (excluding the host_noop default impl and the ui_host.h declaration) must
# contain zero `open_dialog(` call sites. A regression here fails `make all`.
ENGINE_NODIALOG_SRC := $(filter-out engine/host_noop.c,$(ENGINE_SRC))

$(LIBTEST_STAMP): tests/library/consumer.c engine/host_noop.c $(AUTOPLAY_OBJ) $(OUT_ENGLIB) | build
	@bad=`grep -nE '\bopen_dialog[[:space:]]*\(' $(ENGINE_NODIALOG_SRC) 2>/dev/null || true`; \
	if [ -n "$$bad" ]; then \
	  echo "uniform-io guard FAILED: engine calls open_dialog directly (use player_io_message):"; \
	  echo "$$bad"; \
	  exit 1; \
	fi
	gcc $(LIBTEST_CFLAGS) tests/library/consumer.c engine/host_noop.c \
	    $(AUTOPLAY_OBJ) \
	    -Wl,--whole-archive $(OUT_ENGLIB) -Wl,--no-whole-archive \
	    -o $@.tmp $(LIBTEST_LDFLAGS)
	@rm -f $@.tmp
	@touch $@

# ---------------------------------------------------------------------------
# autoplay objects (docs/OPENBOUNTY-SPEC.md §36). Compiled ENGINE-ONLY — only engine
# include paths, NO -Isrc and NO raylib include dir — so the module stays
# engine-pure. The objects link into the game/release/test binaries; there is
# NO separate autoplay binary (autoplay is the --autoplay [--headless] flag on
# the main binary, AP-084). The engine-only boundary is build-enforced by the
# library boundary stamp above, which links these objects with only
# -lm -lpthread (AP-041/AP-120).
# ---------------------------------------------------------------------------
AUTOPLAY_CFLAGS := -std=c99 -Wall -Wextra $(CFLAGS_$(BUILD)) $(OB_EXTRA_CFLAGS) -Iautoplay -Iengine/headless -Iengine/include -Ithird_party/cjson

$(AUTOPLAY_OBJ_DIR)/%.o: %.c | build
	@mkdir -p $(dir $@)
	gcc $(AUTOPLAY_CFLAGS) -MMD -MP -c $< -o $@

-include $(AUTOPLAY_OBJ:.o=.d)

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

.PHONY: all run release run-release windows windows-debug mac clean test extract extract-pack dist dist-linux dist-windows dist-mac
