# ---------------------------------------------------------------------------
# Linux (dev + release)
# ---------------------------------------------------------------------------
RAYLIB       := third_party/raylib-install
RAYLIB_WIN64 := third_party/raylib-install-win64
RAYLIB_WIN32 := third_party/raylib-install-win32
RAYLIB_MAC   := third_party/raylib-install-mac

# Single-header video pipeline: minih264 (encoder) + minimp4 (muxer).
# No -l flag, both libraries compile into src/encode_mp4.c.
MINIH264_INC := third_party/minih264
MINIMP4_INC  := third_party/minimp4

# Version: a single integer (1, 2, 3, ...). The release workflow passes
# OPENBOUNTY_VERSION explicitly from the dispatched release-N tag. For
# local dev builds with no override, derive from the most recent
# release-N tag (digits only) and fall back to 0 if there are no tags.
# RELEASE_VERSION is the project-neutral name the release workflow passes, so every
# repo's release.yml is byte-identical. OPENBOUNTY_VERSION still works as an explicit
# override (command-line vars beat ?=), and a bare `make dist` still derives from tags.
RELEASE_VERSION         ?= $(shell git tag --list 'release-*' 2>/dev/null | sed -n 's/^release-\([1-9][0-9]*\)$$/\1/p' | sort -n | tail -1 | grep . || echo 0)
OPENBOUNTY_VERSION         ?= $(RELEASE_VERSION)
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
CFLAGS  := -std=c99 -Wall -Wextra $(CFLAGS_$(BUILD)) $(OB_EXTRA_CFLAGS) -I$(RAYLIB)/include -I$(MINIH264_INC) -I$(MINIMP4_INC) -Isrc -Iengine/include -Idemo -Iautoplay -Itools -Ibuild -Ithird_party/cjson -Ithird_party/miniz
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

# Demo mode (demo/), the human-like player agent. Engine-only: includes nothing
# from src/, and src/ may not include demo/.
DEMO_SRC := demo/demo.c demo/demo_brain.c demo/demo_path.c demo/demo_scepter.c demo/demo_combat_policy.c
DEMO_OBJ_DIR := build/$(BUILD)/objs/demo
DEMO_OBJ     := $(patsubst %.c,$(DEMO_OBJ_DIR)/%.o,$(DEMO_SRC))

# Autoplay (autoplay/), the headless automated player / pack-winnability
# oracle (docs/AUTOPLAY-SPECS.md). Engine-only like demo/: includes nothing
# from src/ or demo/, and demo/ includes nothing from autoplay/. The one shell
# adapter that knows autoplay is src/shell_autoplay.c (shell -> autoplay ->
# engine).
AUTOPLAY_SRC := autoplay/autoplay.c autoplay/planner.c autoplay/goals.c autoplay/prereq.c autoplay/baltree.c autoplay/search.c autoplay/primitives.c autoplay/exec_move.c autoplay/exec_fight.c autoplay/exec_recruit.c autoplay/exec_loc.c autoplay/recording.c autoplay/worldsnap.c autoplay/plan.c autoplay/exec_replay.c autoplay/exec_ledger.c autoplay/diag.c
AUTOPLAY_OBJ_DIR := build/$(BUILD)/objs/autoplay
AUTOPLAY_OBJ     := $(patsubst %.c,$(AUTOPLAY_OBJ_DIR)/%.o,$(AUTOPLAY_SRC))
SHELL_SRC  := src/main.c src/plat_android.c src/plat_ios.c src/safe_area.c src/gfx_raylib.c src/layout.c src/present.c src/shell_menu.c src/shell_tempdeath.c src/shell_weekend.c src/shell_audience.c src/shell_cheats.c src/shell_gate.c src/shell_fastquit.c src/shell_frame.c src/shell_promptdispatch.c src/shell_actions.c src/shell_demo.c src/shell_autoplay.c src/shell_earlyexit.c src/shell_gallery.c src/assets.c src/pack_select.c src/recorder.c src/audio.c src/audio_raylib.c src/encode_mp4.c src/encode_mp4_h264.c src/encode_mp4_mux.c src/encode_dialog.c src/bfont.c src/text.c src/font_raylib.c src/select.c src/textsel.c src/tilevar.c src/tile_cache.c src/sprites.c src/views.c src/ui.c src/screenshot.c src/combat_loop.c src/combat_render.c src/combat_replay.c src/palette.c src/chrome.c src/lattice.c src/hud.c src/map_render.c src/overlay.c src/legacy/overlay.c src/modern/overlay.c src/views_render.c src/legacy/views_render.c src/modern/views_render.c src/legacy/prompt.c src/modern/prompt.c src/modern/mlayout.c src/modern/castle.c src/modern/mlist.c src/modern/saveslots.c src/modern/gamemenu.c src/modern/location.c src/modern/uikit.c src/modern/page.c src/modern/rail.c src/input.c src/input_host.c src/touch.c src/uitouch.c src/frame_host.c src/prompt.c src/startup.c src/end_cartoon.c src/screens/home_castle.c src/screens/recruit_soldiers.c src/screens/own_castle.c src/screens/dwelling.c src/screens/alcove.c src/screens/end_game.c
# plat_android.c is NOT here: its non-Android branch is two no-ops, and
# main.c calls them on every platform.
IOS_SKIP := src/gfx_raylib.c src/frame_host.c src/input_host.c \
            src/audio_raylib.c src/font_raylib.c \
            src/recorder.c src/encode_mp4.c src/encode_mp4_h264.c \
            src/encode_mp4_mux.c src/encode_dialog.c src/screenshot.c \
            src/shell_gallery.c src/pack_select.c
IOS_CHECK_SRC := $(filter-out $(IOS_SKIP),$(SHELL_SRC))
# The iOS backends' plain-C half. Checked with the shell files below, so a
# break in them is caught here rather than on a macOS runner ten minutes later.
IOS_OWN_C     := ios/host_ios.c ios/image_ios.c ios/font_ios.c ios/vorbis_impl.c

TOOL_SRC   := tools/extract.c tools/extract_io.c tools/extract_unpack.c tools/extract_lzw.c tools/extract_vga.c tools/extract_png.c tools/extract_chrome.c tools/extract_gamejson.c
VENDOR_SRC := third_party/cjson/cJSON.c third_party/miniz/miniz.c

# Full single-translation-unit source list for the cross-compile targets
# (windows, mac) which build from $(SRC) directly rather than the per-object
# Linux path.
SRC := $(SHELL_SRC) $(ENGINE_SRC) $(DEMO_SRC) $(AUTOPLAY_SRC) $(TOOL_SRC) $(VENDOR_SRC)
SRC_DEV := $(SRC)
# One game binary. `make` -> build/debug/openbounty ; `make release` ->
# build/release/openbounty (BUILD=release). Per-build object dirs below keep
# the two configs from clobbering each other's .o files.
OUT := build/$(BUILD)/openbounty

# Pack zips: one per top-level dir under assets/. Each becomes
# build/$(BUILD)/assets/<name>.openbounty, IN THE BINARY'S OWN DIRECTORY so
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
TOUCH_STAMP := build/touch-guard.stamp
PAGE_STAMP  := build/page-guard.stamp
# The iOS purity check (rule further down, next to the library-boundary one).
IOS_CHECK_STAMP := build/ios-purity.stamp

# Default build: compile + link the game and its asset packs, plus the
# library-boundary check (engine + demo + autoplay must link with only
# -lm -lpthread). No test binary, no test run, use `make test` for those.
all: $(OUT) $(PACKS) $(LIBTEST_STAMP) $(IOS_CHECK_STAMP) $(TOUCH_STAMP) $(PAGE_STAMP)

# Generate build/version.h from $(OPENBOUNTY_VERSION). Marked .PHONY-style
# (FORCE prereq) so it always runs, the cmp/mv inside only rewrites the
# file when the value actually changes, so unchanged versions don't
# trigger spurious downstream rebuilds. Every binary target lists this
# as a prerequisite, and main.c's --version / --help include it.
.PHONY: FORCE
FORCE:
build/version.h: FORCE | build
	@printf '#ifndef OB_VERSION_H\n#define OB_VERSION_H\n#define OPENBOUNTY_VERSION "%s"\n#endif\n' "$(OPENBOUNTY_VERSION)" > $@.tmp
	@if ! cmp -s $@.tmp $@ 2>/dev/null; then mv $@.tmp $@; else rm $@.tmp; fi

# Shell + tool object files. Compiled once, linked into each binary.
# Pattern rule below keeps the build incremental, touching one shell
# .c file rebuilds only that object plus the dependent binaries.
SHELL_OBJ := $(patsubst %.c,build/$(BUILD)/objs/shell/%.o,$(SHELL_SRC))
TOOL_OBJ  := $(patsubst %.c,build/$(BUILD)/objs/shell/%.o,$(TOOL_SRC))

build/$(BUILD)/objs/shell/%.o: %.c build/version.h Makefile | build
	@mkdir -p $(dir $@)
	gcc $(CFLAGS) -MMD -MP -c $< -o $@

# Game = libobengine.a (engine + vendored cJSON/miniz) + shell objects
# + tool objects (extract). No recompile of engine sources for the game.
# `make` only compiles + links, it does NOT run the tests (use `make test`).
$(OUT): $(SHELL_OBJ) $(TOOL_OBJ) $(DEMO_OBJ) $(AUTOPLAY_OBJ) $(OUT_ENGLIB) build/version.h Makefile | build
	@mkdir -p $(dir $@)
	gcc $(CFLAGS) $(SHELL_OBJ) $(TOOL_OBJ) $(DEMO_OBJ) $(AUTOPLAY_OBJ) $(OUT_ENGLIB) -o $(OUT) $(LDFLAGS)

# Each pack zip rebuilds when any file under its assets/<name>/ tree
# changes. Using $(shell find) at parse time means: run `make` after
# editing assets to repackage; touching a single asset is enough.
# The engine binary itself does the zipping via --pack-dir.
$(PACK_DIR):
	mkdir -p $(PACK_DIR)

# The binary that zips a pack. Normally the native dev build, but a target
# whose host cannot build that one overrides it: the iOS job runs on macOS,
# where the default build's Linux link flags (-lX11, -lrt) do not apply, so it
# passes PACK_TOOL=build/openbounty-mac after `make mac`.
PACK_TOOL ?= $(OUT)

define PACK_RULE
$(PACK_DIR)/$(1).openbounty: $$(shell find assets/$(1) -type f \! -name '*.xcf' \! -name '*.psd' \! -name '*:Zone.Identifier' 2>/dev/null) $(PACK_TOOL) | $(PACK_DIR)
	./$(PACK_TOOL) --pack-dir assets/$(1) $(PACK_DIR)/$(1).openbounty
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
# Target 'all' (not $(OUT)/$(PACKS)), those expand in THIS make where
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
WIN_CFLAGS_COMMON := -std=c99 -Wall -Wextra -O2 -Isrc -Iengine/include -Idemo -Iautoplay -Itools -Ibuild -Ithird_party/cjson -Ithird_party/miniz -I$(MINIH264_INC) -I$(MINIMP4_INC) -DWIN32 -D_WIN32
# -mwindows hides the console; keep it for a GUI app.
# -static links libgcc/libstdc++/winpthread statically so no DLLs are needed.
# --stack=8MB matches the Linux default. Several main.c locals are big
# (Resources is ~3.9 MB, Map ~848 KB), Windows default 1 MB stack
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

$(OUT_WIN64): $(SRC) build/version.h Makefile
	$(WIN64_CC) $(WIN64_CFLAGS) $(SRC) -o $(OUT_WIN64) $(WIN64_LDFLAGS)

$(OUT_WIN32): $(SRC) build/version.h Makefile
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

$(OUT_WIN64_DEBUG): $(SRC) build/version.h Makefile
	$(WIN64_CC) $(WIN64_CFLAGS) $(SRC) -o $(OUT_WIN64_DEBUG) $(WIN64_LDFLAGS_DEBUG)

$(OUT_WIN32_DEBUG): $(SRC) build/version.h Makefile
	$(WIN32_CC) $(WIN32_CFLAGS) $(SRC) -o $(OUT_WIN32_DEBUG) $(WIN32_LDFLAGS_DEBUG)

# ---------------------------------------------------------------------------
# macOS build (universal binary: arm64 + x86_64, embedded assets, static
# raylib). Intended for CI on a GitHub macos-* runner, Apple Silicon and
# Intel Macs both run the resulting binary. The shipped libraylib.a is a
# universal archive (Mach-O fat) so a single clang invocation produces a
# fat output without needing a second compile pass.
# ---------------------------------------------------------------------------
MAC_CC      := clang
MAC_ARCHES  := -arch arm64 -arch x86_64
MAC_CFLAGS  := -std=c99 -Wall -Wextra -O2 $(MAC_ARCHES) \
               -I$(RAYLIB_MAC)/include -I$(MINIH264_INC) -I$(MINIMP4_INC) \
               -Isrc -Iengine/include -Idemo -Iautoplay -Itools -Ibuild -Ithird_party/cjson -Ithird_party/miniz
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

$(OUT_MAC): $(SRC) build/version.h Makefile
	$(MAC_CC) $(MAC_CFLAGS) $(SRC) -o $(OUT_MAC) $(MAC_LDFLAGS)

# ---------------------------------------------------------------------------
# WebAssembly build (browser). Compiles the SAME $(SRC) as every other
# target -- no source is trimmed. The desktop-only files (recorder,
# encode_mp4, extract) use opendir/mkdtemp/clock_gettime/localtime_r,
# all of which Emscripten's libc and MEMFS provide, so they compile and
# link unchanged; their features simply aren't reachable from the web
# shell, which offers human play only.
#
# ASYNCIFY is the load-bearing flag. The shell's blocking loops -- eight
# in src/startup.c, plus a whole battle in src/combat_loop.c, the ending
# cartoon, and the top-level loop -- are all spelled
# `while (!frame_host_should_close())`. raylib's web backend implements
# WindowShouldClose() as emscripten_sleep(12) + return false, so with
# ASYNCIFY every one of those loops yields to the browser each iteration
# with no source change. Note it never returns true: each loop must exit
# on its own key/state condition, which they all do.
#
# Prerequisite: emsdk installed and activated, so emcc is on PATH:
#     source third_party/emsdk/emsdk_env.sh
# raylib for wasm comes from scripts/build_raylib_web.sh.
# ---------------------------------------------------------------------------
RAYLIB_WEB := third_party/raylib-install-web
EMCC       := emcc

WEB_CFLAGS := -std=c99 -Wall -Wextra -O2 -DPLATFORM_WEB \
              -I$(RAYLIB_WEB)/include -I$(MINIH264_INC) -I$(MINIMP4_INC) \
              -Isrc -Iengine/include -Idemo -Iautoplay -Itools -Ibuild \
              -Ithird_party/cjson -Ithird_party/miniz

# -sSTACK_SIZE=8388608 mirrors the Windows target's --stack=8388608 and for
#   the same reason: several main.c locals are large (Resources ~3.9 MB,
#   Map ~848 KB) and Emscripten's 64 KB default stack overflows instantly.
# -sINITIAL_MEMORY=67108864 (64 MiB) is a FIXED heap, deliberately not
#   ALLOW_MEMORY_GROWTH: a growable wasm heap hands out resizable
#   ArrayBuffers, which browsers reject in WebGL texImage2D. Measured peak
#   over boot / save load / roaming with zone reloads is a flat 32.8 MB --
#   growth never fired -- so 64 MiB is 2x headroom.
# -lidbfs.js provides the IDBFS the shell mounts at /saves so saves survive
#   a page reload (engine/savepath.c's __EMSCRIPTEN__ branch).
# A wasm module embeds its pack, so there is one build PER PACK, each in its
#   own build/web/<pack>/ directory: --preload-file bakes that pack into
#   openbounty.data and the shell passes the matching path via --pack. The pack
#   is embedded, never redistributed as a loose .openbounty file, so the release
#   workflow's asset guard is unaffected -- and `web` is deliberately NOT part
#   of `dist`.
WEB_LDFLAGS := -L$(RAYLIB_WEB)/lib -lraylib -lidbfs.js \
               -sUSE_GLFW=3 -sASYNCIFY -sINITIAL_MEMORY=67108864 \
               -sSTACK_SIZE=8388608 -sFORCE_FILESYSTEM \
               -sEXPORTED_RUNTIME_METHODS=FS,IDBFS,addRunDependency,removeRunDependency

# The packs that get a web build. `make web` builds both, and dist-web
# packages each as its own zip (openbounty-* for King's Bounty, gloryofrome-*
# for Glory of Rome).
WEB_PACK_NAMES := kings-bounty glory-of-rome
WEB_OUTS       := $(foreach p,$(WEB_PACK_NAMES),build/web/$(p)/openbounty.html)
OUT_WEB_ROME   := build/web/glory-of-rome/openbounty.html

web: $(WEB_OUTS)
web-kings-bounty: build/web/kings-bounty/openbounty.html
web-glory-of-rome: $(OUT_WEB_ROME)

# $(call WEB_RULE,<pack-name>) -- one emcc link per pack. The shell is copied
# per pack with @@PACK@@ replaced by the path --preload-file maps it to, so the
# two builds never share a file and cannot pick up each other's pack.
define WEB_RULE
build/web/$(1)/openbounty.html: $$(SRC) web/shell.html $$(PACK_DIR)/$(1).openbounty build/version.h Makefile
	@command -v $$(EMCC) >/dev/null 2>&1 || { \
	  echo "make web: emcc not on PATH."; \
	  echo "  source third_party/emsdk/emsdk_env.sh"; exit 1; }
	@test -f $$(RAYLIB_WEB)/lib/libraylib.a || { \
	  echo "make web: missing $$(RAYLIB_WEB)/lib/libraylib.a."; \
	  echo "  ./scripts/build_raylib_web.sh"; exit 1; }
	@mkdir -p build/web/$(1)
	sed 's|@@PACK@@|/assets/$(1).openbounty|g' web/shell.html > build/web/$(1)/shell.html
	$$(EMCC) $$(WEB_CFLAGS) $$(SRC) -o $$@ $$(WEB_LDFLAGS) \
	    --preload-file $$(PACK_DIR)/$(1).openbounty@/assets/$(1).openbounty \
	    --shell-file build/web/$(1)/shell.html
endef
$(foreach p,$(WEB_PACK_NAMES),$(eval $(call WEB_RULE,$(p))))

# Serve the built games locally. Browsers refuse to fetch the .wasm/.data
# over file://, so a real HTTP server is required to run them at all.
web-serve: $(WEB_OUTS)
	@for p in $(WEB_PACK_NAMES); do \
	  echo "$$p: http://localhost:8080/$$p/openbounty.html"; \
	done
	@cd build/web && python3 -m http.server 8080

# ---------------------------------------------------------------------------
# Android build (NativeActivity APK, no Gradle). CI-only: needs the NDK + SDK
# build-tools, both provided by the setup-android action. Mirrors raylib's
# upstream Makefile.Android flow: cross-compile the game + the NDK's
# native_app_glue into libgloryofrome.so, then package + sign an APK with
# aapt / zipalign / apksigner.
#
# MOBILE SHIPS GLORY OF ROME ONLY. The pack goes into the APK's assets/ and
# src/plat_android.c opens it from there; there is no pack discovery, no
# picker, and King's Bounty (DOS-extracted, copyright-restricted) is never
# packaged.
#
# Pass the toolchain on the make command line, never through the environment:
#   make android ANDROID_NDK=<ndk root> ANDROID_SDK_ROOT=<sdk root>
# ---------------------------------------------------------------------------
ANDROID_GOALS := android android-play dist-android dist-android-play
ifneq ($(filter $(ANDROID_GOALS),$(MAKECMDGOALS)),)
ifneq ($(filter environment%,$(origin ANDROID_NDK) $(origin ANDROID_SDK_ROOT)),)
$(error ANDROID_NDK and ANDROID_SDK_ROOT come from the make command line, not the environment)
endif
ifeq ($(ANDROID_NDK),)
$(error pass ANDROID_NDK=<ndk root> on the make command line)
endif
ifeq ($(ANDROID_SDK_ROOT),)
$(error pass ANDROID_SDK_ROOT=<sdk root> on the make command line)
endif
endif
ANDROID_API          ?= 24
# The shipped ABI. arm64-v8a is every Android phone Play still serves, and is
# what the APK and the AAB carry. It is overridable for one reason: the CI
# emulator smoke test runs on x86_64 runners and needs an x86_64 APK, which is
# built separately and never shipped.
ANDROID_ABI          ?= arm64-v8a
ANDROID_BUILD_TOOLS  ?= 36.0.0
ANDROID_PLATFORM_VER ?= 36

ANDROID_APP_NAME := gloryofrome
ANDROID_PACK     := glory-of-rome

# versionCode must be a monotonically increasing integer for Play uploads; drive
# it off the release number (unique + monotonic). Clamp to >=1 for local builds
# where OPENBOUNTY_VERSION is 0 (no release tags yet). versionName is the
# human-facing string. Both are injected at package time (aapt/aapt2 flags), so
# the manifest values are just fallbacks.
ANDROID_VERSION_CODE ?= $(OPENBOUNTY_VERSION)
ifeq ($(ANDROID_VERSION_CODE),0)
ANDROID_VERSION_CODE := 1
endif
ANDROID_VERSION_NAME ?= 1.0.$(ANDROID_VERSION_CODE)

RAYLIB_ANDROID := third_party/raylib-install-android/$(ANDROID_ABI)

ANDROID_TOOLCHAIN := $(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64
# The NDK names its compiler after the target triple, which is not the ABI
# name, so the two have to be mapped.
ANDROID_TRIPLE_arm64-v8a   := aarch64-linux-android
ANDROID_TRIPLE_x86_64      := x86_64-linux-android
ANDROID_TRIPLE_armeabi-v7a := armv7a-linux-androideabi
ANDROID_TRIPLE_x86         := i686-linux-android
ANDROID_TRIPLE    := $(ANDROID_TRIPLE_$(ANDROID_ABI))
ifeq ($(ANDROID_TRIPLE),)
$(error unknown ANDROID_ABI "$(ANDROID_ABI)")
endif
ANDROID_CC        := $(ANDROID_TOOLCHAIN)/bin/$(ANDROID_TRIPLE)$(ANDROID_API)-clang
NATIVE_APP_GLUE   := $(ANDROID_NDK)/sources/android/native_app_glue

ANDROID_SDK_BT := $(ANDROID_SDK_ROOT)/build-tools/$(ANDROID_BUILD_TOOLS)
ANDROID_JAR    := $(ANDROID_SDK_ROOT)/platforms/android-$(ANDROID_PLATFORM_VER)/android.jar

# The whole game, unchanged: every translation unit the desktop build has.
# Trimming the desktop-only subsystems (recorder, gallery, demo, autoplay,
# extractor) needs stubs for what main.c calls, which is a separate change --
# correctness first, size later.
ANDROID_SRC     := $(SRC)
ANDROID_CFLAGS  := -std=c99 -Wall -Wextra -O2 -DPLATFORM_ANDROID -fPIC \
                   -I$(RAYLIB_ANDROID)/include -I$(NATIVE_APP_GLUE) \
                   -I$(MINIH264_INC) -I$(MINIMP4_INC) \
                   -Isrc -Iengine/include -Idemo -Iautoplay -Itools -Ibuild \
                   -Ithird_party/cjson -Ithird_party/miniz
# raylib wraps fopen at link time (-Wl,--wrap=fopen) so file access routes
# through the Android asset manager; libraylib.a references __real_fopen, which
# only exists when this flag is present. Without it, dlopen of the .so fails at
# launch with "cannot locate symbol __real_fopen". (The pack itself does not
# rely on the wrap -- see src/plat_android.c.)
#
# -z max-page-size=16384 gives the .so 16 KB-aligned LOAD segments. Google Play
# requires 16 KB page-size support for apps targeting Android 15+; NDK r26's
# linker still defaults to 4 KB, so we set it explicitly.
ANDROID_LDFLAGS := -shared -L$(RAYLIB_ANDROID)/lib -lraylib \
                   -Wl,--wrap=fopen \
                   -Wl,-z,max-page-size=16384,-z,common-page-size=16384 \
                   -llog -landroid -lEGL -lGLESv2 -lOpenSLES -lm -lc -ldl

ANDROID_OBJ_DIR := build/obj-android
# Object paths mirror the source tree: src/overlay.c, src/legacy/overlay.c and
# src/modern/overlay.c are three different files with one basename.
ANDROID_OBJ     := $(patsubst %.c,$(ANDROID_OBJ_DIR)/%.o,$(ANDROID_SRC)) \
                   $(ANDROID_OBJ_DIR)/native_app_glue.o

ANDROID_APK_DIR  := build/android
ANDROID_LIB      := $(ANDROID_APK_DIR)/lib/$(ANDROID_ABI)/lib$(ANDROID_APP_NAME).so
ANDROID_ASSETS   := build/android-assets
ANDROID_APK      := build/$(ANDROID_APP_NAME).apk
ANDROID_KEYSTORE ?= build/debug.keystore

ANDROID_JAVA_SRC := android/java/com/danheskett/gloryofrome/GloryOfRomeActivity.java
ANDROID_DEX      := build/dex/classes.dex
JAVAC            ?= javac

android: $(ANDROID_APK)

# native_app_glue is vendored NDK source (not ours); it trips -Wextra's
# unused-parameter, so build this one object without it to keep the log clean.
$(ANDROID_OBJ_DIR)/native_app_glue.o: $(NATIVE_APP_GLUE)/android_native_app_glue.c
	@mkdir -p $(dir $@)
	$(ANDROID_CC) $(ANDROID_CFLAGS) -Wno-unused-parameter -c $< -o $@

# build/version.h is generated (main.c's --version includes it), so every
# Android object waits on it exactly as the native objects do.
$(ANDROID_OBJ_DIR)/%.o: %.c build/version.h
	@mkdir -p $(dir $@)
	$(ANDROID_CC) $(ANDROID_CFLAGS) -MMD -MP -c $< -o $@

$(ANDROID_LIB): $(ANDROID_OBJ)
	@mkdir -p $(dir $@)
	$(ANDROID_CC) $(ANDROID_OBJ) -o $@ $(ANDROID_LDFLAGS)

# The pack, staged where aapt's -A expects it. Built by the normal pack rule.
$(ANDROID_ASSETS)/$(ANDROID_PACK).openbounty: $(PACK_DIR)/$(ANDROID_PACK).openbounty
	@mkdir -p $(ANDROID_ASSETS)
	cp $< $@

# Compile GloryOfRomeActivity.java against the platform jar, then dex it.
# -source/-target 8 keeps the bytecode dex-friendly; android.jar on the
# classpath resolves the framework APIs.
$(ANDROID_DEX): $(ANDROID_JAVA_SRC)
	@rm -rf build/java-classes && mkdir -p build/java-classes $(dir $@)
	$(JAVAC) -source 1.8 -target 1.8 -Xlint:-options \
	    -classpath $(ANDROID_JAR) -d build/java-classes $(ANDROID_JAVA_SRC)
	$(ANDROID_SDK_BT)/d8 --min-api $(ANDROID_API) --lib $(ANDROID_JAR) \
	    --output build/dex build/java-classes/com/danheskett/gloryofrome/*.class

# Throwaway debug keystore for signing. Real distributable builds sign with a
# keystore supplied from a CI secret instead.
$(ANDROID_KEYSTORE):
	@mkdir -p $(dir $@)
	keytool -genkeypair -keystore $@ -storepass android -keypass android \
	    -alias $(ANDROID_APP_NAME) -keyalg RSA -keysize 2048 -validity 10000 \
	    -dname "CN=Glory of Rome, O=OpenBounty, C=US"

$(ANDROID_APK): $(ANDROID_LIB) $(ANDROID_DEX) $(ANDROID_KEYSTORE) \
                $(ANDROID_ASSETS)/$(ANDROID_PACK).openbounty \
                android/AndroidManifest.xml android/res/values/styles.xml
	# -S compiles android/res (the fullscreen/cutout theme); -A adds the pack.
	$(ANDROID_SDK_BT)/aapt package -f -M android/AndroidManifest.xml \
	    -S android/res -A $(ANDROID_ASSETS) -I $(ANDROID_JAR) \
	    --version-code $(ANDROID_VERSION_CODE) --version-name $(ANDROID_VERSION_NAME) \
	    -F build/$(ANDROID_APP_NAME).unaligned.apk
	# Store the native lib at lib/<abi>/ inside the APK (path relative to cwd).
	(cd $(ANDROID_APK_DIR) && $(ANDROID_SDK_BT)/aapt add \
	    ../../build/$(ANDROID_APP_NAME).unaligned.apk lib/$(ANDROID_ABI)/lib$(ANDROID_APP_NAME).so)
	# Store classes.dex at the APK root (path relative to cwd = build/dex).
	(cd build/dex && $(ANDROID_SDK_BT)/aapt add \
	    ../$(ANDROID_APP_NAME).unaligned.apk classes.dex)
	$(ANDROID_SDK_BT)/zipalign -f 4 \
	    build/$(ANDROID_APP_NAME).unaligned.apk build/$(ANDROID_APP_NAME).aligned.apk
	$(ANDROID_SDK_BT)/apksigner sign --ks $(ANDROID_KEYSTORE) \
	    --ks-pass pass:android --key-pass pass:android \
	    --out $@ build/$(ANDROID_APP_NAME).aligned.apk
	@rm -f build/$(ANDROID_APP_NAME).unaligned.apk build/$(ANDROID_APP_NAME).aligned.apk
	@echo "[android] built $@"

-include $(ANDROID_OBJ:.o=.d)

# ---------------------------------------------------------------------------
# Android App Bundle (.aab) for Google Play. Play only accepts AABs for new
# apps, and the legacy `aapt` (v1) above cannot emit one, so this path uses
# `aapt2` (proto resources) + `bundletool`. Kept fully separate from the
# sideload APK target: same libgloryofrome.so and the same pack, different
# packaging + a real upload key. Signed with the upload key; Google's Play App
# Signing re-signs the delivered APKs, so this signature only has to satisfy
# the Play upload check.
#
# Signing defaults to the throwaway debug keystore so `make android-play`
# works locally to exercise the pipeline; CI overrides PLAY_* with the real
# upload keystore (from a secret) to produce an uploadable bundle.
# ---------------------------------------------------------------------------
ANDROID_AAB        := build/$(ANDROID_APP_NAME).aab
BUNDLETOOL_VERSION ?= 1.17.2
BUNDLETOOL         ?= build/bundletool.jar
# Checksum of bundletool-all-$(BUNDLETOOL_VERSION).jar. Bump both together.
BUNDLETOOL_SHA256  ?= 2d4ad908faea64047c1cc9cb747e6aa667c6ab192e09607bd16b67246a8cd6ae

PLAY_KEYSTORE   ?= $(ANDROID_KEYSTORE)
PLAY_KEY_ALIAS  ?= $(ANDROID_APP_NAME)
PLAY_STORE_PASS ?= android
PLAY_KEY_PASS   ?= android

android-play: $(ANDROID_AAB)

# Downloaded to .tmp and renamed only after the checksum matches. The rename
# matters as much as the check: this jar is run with `java -jar` in the same job
# that has just decoded the Play upload keystore to disk, and leaving a rejected
# download at $@ would let the next run's existence test accept it unverified.
$(BUNDLETOOL):
	@mkdir -p $(dir $@)
	curl -fsSL -o $@.tmp \
	    https://github.com/google/bundletool/releases/download/$(BUNDLETOOL_VERSION)/bundletool-all-$(BUNDLETOOL_VERSION).jar
	echo "$(BUNDLETOOL_SHA256)  $@.tmp" | sha256sum -c - || { rm -f $@.tmp; exit 1; }
	mv $@.tmp $@

# Exported rather than passed on the command line, so the passwords reach
# jarsigner through the environment and appear in neither the build log nor the
# process table.
$(ANDROID_AAB): export PLAY_STORE_PASS_ENV = $(PLAY_STORE_PASS)
$(ANDROID_AAB): export PLAY_KEY_PASS_ENV   = $(PLAY_KEY_PASS)
$(ANDROID_AAB): $(ANDROID_LIB) $(ANDROID_DEX) $(BUNDLETOOL) $(PLAY_KEYSTORE) \
                $(ANDROID_ASSETS)/$(ANDROID_PACK).openbounty \
                android/AndroidManifest.xml android/res/values/styles.xml
	@rm -rf build/aab && mkdir -p build/aab/module/manifest \
	    build/aab/module/lib/$(ANDROID_ABI) build/aab/module/dex \
	    build/aab/module/assets
	# Compile android/res, then link into a *protobuf* APK (bundletool's input).
	$(ANDROID_SDK_BT)/aapt2 compile --dir android/res -o build/aab/res.zip
	$(ANDROID_SDK_BT)/aapt2 link --proto-format -o build/aab/proto.apk \
	    -I $(ANDROID_JAR) --manifest android/AndroidManifest.xml \
	    -R build/aab/res.zip --auto-add-overlay \
	    --version-code $(ANDROID_VERSION_CODE) --version-name $(ANDROID_VERSION_NAME)
	# Re-lay the proto APK into bundletool's base-module layout, add the .so,
	# the dex and the pack.
	cd build/aab && unzip -qo proto.apk -d proto
	mv build/aab/proto/AndroidManifest.xml build/aab/module/manifest/AndroidManifest.xml
	mv build/aab/proto/resources.pb        build/aab/module/resources.pb
	mv build/aab/proto/res                 build/aab/module/res
	cp $(ANDROID_LIB) build/aab/module/lib/$(ANDROID_ABI)/lib$(ANDROID_APP_NAME).so
	cp $(ANDROID_DEX) build/aab/module/dex/classes.dex
	cp $(ANDROID_ASSETS)/$(ANDROID_PACK).openbounty build/aab/module/assets/
	cd build/aab/module && zip -qr ../module.zip manifest resources.pb res lib dex assets
	java -jar $(BUNDLETOOL) build-bundle --modules=build/aab/module.zip --output=$@
	# Sign the bundle (JAR signature) with the upload key.
	@jarsigner -keystore $(PLAY_KEYSTORE) -storepass:env PLAY_STORE_PASS_ENV \
	    -keypass:env PLAY_KEY_PASS_ENV -sigalg SHA256withRSA -digestalg SHA-256 \
	    $@ $(PLAY_KEY_ALIAS)
	@echo "[android] built $@ (versionCode $(ANDROID_VERSION_CODE), versionName $(ANDROID_VERSION_NAME))"

# ---------------------------------------------------------------------------
# iOS (native Metal, no raylib). CI-only: needs Xcode's toolchain, which exists
# on macOS alone -- there is no Mac here, so the FIRST build of every one of
# these files is the macOS runner (docs/IOS-BACKEND.md).
#
#   ios-sim  -- Simulator .app (arm64 simulator, unsigned) for CI screenshots.
#   ios      -- device .ipa (arm64, unsigned unless a signing identity is set).
#
# Assembled by hand (clang + Info.plist + zip), no Xcode project, mirroring the
# no-Gradle Android target. The Metal shader is compiled at RUNTIME from source
# (ios/gfx_metal.mm), so no offline Metal compiler is needed either.
#
# CHECKPOINT 2 of the spike: this builds the app shell and the renderer only --
# the game's C is not linked yet, and the app draws the renderer self-test.
# ---------------------------------------------------------------------------
IOS_MIN        ?= 15.0
IOS_APP_NAME   := GloryOfRome
IOS_BUNDLE_ID  := com.danheskett.gloryofrome
# CFBundleVersion must increase with every App Store upload, so it tracks the
# release number exactly like ANDROID_VERSION_CODE.
IOS_BUILD_NUMBER ?= $(OPENBOUNTY_VERSION)
ifeq ($(IOS_BUILD_NUMBER),0)
IOS_BUILD_NUMBER := 1
endif
IOS_VERSION_NAME ?= 1.0.$(IOS_BUILD_NUMBER)
# Signing is opt-in: set IOS_SIGN_IDENTITY (and IOS_PROFILE) to produce a
# submittable .ipa. Unset, the build stays unsigned.
IOS_SIGN_IDENTITY ?=
IOS_PROFILE       ?=
IOS_TEAM_ID       ?=
# The App Store icon. One 1024x1024 PNG: actool derives every other size, so
# there is no icon art to resize by hand. Absent, an unsigned build still runs
# (the Simulator does not care); a signed build stops, because Apple rejects an
# iconless upload outright.
IOS_ICON := ios/Assets.xcassets/AppIcon.appiconset/icon-1024.png

IOS_MM_SRC  := ios/ios_main.mm ios/gfx_metal.mm ios/plat_ios.mm \
               ios/audio_ios.mm
# The game itself: the shell minus the desktop-only subsystems (the same list
# the iOS purity check uses), the engine, and the iOS backends. No raylib, no
# demo/autoplay drivers, no extractor.
IOS_C_SRC   := $(IOS_CHECK_SRC) $(ENGINE_SRC) $(DEMO_SRC) $(AUTOPLAY_SRC) \
               $(TOOL_SRC) \
               ios/host_ios.c ios/image_ios.c ios/font_ios.c ios/vorbis_impl.c \
               third_party/cjson/cJSON.c third_party/miniz/miniz.c
IOS_CFLAGS  := -std=c99   -Wall -Wextra -O2 -DPLATFORM_IOS -Isrc -Iios \
               -Iengine/include -Idemo -Iautoplay -Itools -Ibuild \
               -Ithird_party/cjson -Ithird_party/miniz -Ithird_party/stb
IOS_MMFLAGS := -std=c++17 -fobjc-arc -Wall -Wextra -O2 -DPLATFORM_IOS \
               -Isrc -Iios -Iengine/include -Idemo -Iautoplay -Itools -Ibuild \
               -Ithird_party/cjson -Ithird_party/miniz -Ithird_party/stb
IOS_FRAMEWORKS := -framework UIKit -framework Metal -framework QuartzCore \
                  -framework CoreGraphics -framework AVFoundation \
                  -framework Foundation

IOS_DEPS := $(IOS_MM_SRC) $(IOS_C_SRC) $(wildcard ios/*.h src/*.h src/*/*.h) \
            ios/Info.plist build/version.h $(PACK_DIR)/$(ANDROID_PACK).openbounty

# $(call ios_build,<sdk>,<target-triple>,<app-dir>,<obj-dir>) -- compile + link
# the app binary into <app-dir>/$(IOS_APP_NAME) and copy the Info.plist.
define ios_build
	@rm -rf $(4) && mkdir -p $(3) $(4)
	for f in $(IOS_C_SRC);  do o=$(4)/$$(echo $$f | tr / _ | sed 's/\.c$$/.o/');  xcrun -sdk $(1) clang   -target $(2) $(IOS_CFLAGS)  -c $$f -o $$o || exit 1; done
	for f in $(IOS_MM_SRC); do o=$(4)/$$(echo $$f | tr / _ | sed 's/\.mm$$/.o/'); xcrun -sdk $(1) clang++ -target $(2) $(IOS_MMFLAGS) -c $$f -o $$o || exit 1; done
	xcrun -sdk $(1) clang++ -target $(2) $(4)/*.o $(IOS_FRAMEWORKS) -o $(3)/$(IOS_APP_NAME)
	sed -e "s|<string>1</string>|<string>$(IOS_BUILD_NUMBER)</string>|" \
	    -e "s|<string>1.0</string>|<string>$(IOS_VERSION_NAME)</string>|" \
	    ios/Info.plist > $(3)/Info.plist
	# Glory of Rome only: the one pack, as a bundle resource.
	cp $(PACK_DIR)/$(ANDROID_PACK).openbounty $(3)/
endef

IOS_SIM_APP := build/ios-sim/$(IOS_APP_NAME).app
ios-sim: $(IOS_SIM_APP)
$(IOS_SIM_APP): $(IOS_DEPS)
	$(call ios_build,iphonesimulator,arm64-apple-ios$(IOS_MIN)-simulator,build/ios-sim/$(IOS_APP_NAME).app,build/ios-sim/obj)
	@echo "[ios] built $@"

IOS_APP_DIR := build/ios-device/Payload/$(IOS_APP_NAME).app
IOS_IPA     := build/$(IOS_APP_NAME).ipa
ios: $(IOS_IPA)
# A .ipa is only a zip of Payload/<App>.app, but an App Store upload wants a
# bundle shaped the way Xcode shapes one. Everything below is a key Xcode would
# have injected and a hand-assembled bundle lacks; each one has rejected a real
# upload somewhere, so none of it is decoration.
$(IOS_IPA): $(IOS_DEPS)
	$(call ios_build,iphoneos,arm64-apple-ios$(IOS_MIN),$(IOS_APP_DIR),build/ios-device/obj)
	@# Device-platform keys. Device farms read CFBundleSupportedPlatforms on
	@# upload and refuse a bundle without it.
	plist=$(IOS_APP_DIR)/Info.plist; \
	/usr/libexec/PlistBuddy \
	    -c "Add :CFBundleSupportedPlatforms array" \
	    -c "Add :CFBundleSupportedPlatforms:0 string iPhoneOS" \
	    -c "Add :DTPlatformName string iphoneos" \
	    -c "Add :UIRequiredDeviceCapabilities array" \
	    -c "Add :UIRequiredDeviceCapabilities:0 string arm64" \
	    -c "Set :CFBundleVersion $(IOS_BUILD_NUMBER)" \
	    -c "Set :CFBundleShortVersionString $(IOS_VERSION_NAME)" \
	    "$$plist"
	@# App icon. actool compiles the catalog to Assets.car and writes the
	@# CFBundleIcons keys into a partial plist, which is merged in. A signed
	@# build with no icon art is a wasted upload, so it stops here instead.
	@if [ -f "$(IOS_ICON)" ]; then \
	    xcrun actool ios/Assets.xcassets --compile $(IOS_APP_DIR) \
	        --platform iphoneos --minimum-deployment-target $(IOS_MIN) \
	        --target-device iphone --target-device ipad --app-icon AppIcon \
	        --output-partial-info-plist build/ios-device/assetcatalog.plist >/dev/null; \
	    /usr/libexec/PlistBuddy -c "Merge build/ios-device/assetcatalog.plist" \
	        $(IOS_APP_DIR)/Info.plist; \
	    plutil -replace CFBundleIconName -string AppIcon $(IOS_APP_DIR)/Info.plist; \
	elif [ -n "$(IOS_SIGN_IDENTITY)" ]; then \
	    echo "error: $(IOS_ICON) is missing; the App Store rejects an iconless upload" >&2; exit 1; \
	else \
	    echo "[ios] no app icon yet ($(IOS_ICON)); fine for a test build, not for the store"; \
	fi
	@# Toolchain provenance. App Store review reads DTXcodeBuild to identify
	@# the toolchain and refuses a build that appears to come from none.
	@# DTPlatformVersion is the SDK version, NOT the deployment target.
	plist=$(IOS_APP_DIR)/Info.plist; \
	xcode_ver=$$(xcodebuild -version | sed -n '1s/^Xcode //p'); \
	xcode_build=$$(xcodebuild -version | sed -n '2s/^Build version //p'); \
	sdk_ver=$$(xcrun --sdk iphoneos --show-sdk-version); \
	sdk_build=$$(xcrun --sdk iphoneos --show-sdk-build-version); \
	plat_ver=$$(xcrun --sdk iphoneos --show-sdk-platform-version); \
	dtxcode=$$(echo $$xcode_ver | awk -F. '{printf "%02d%d%d", $$1, $$2+0, $$3+0}'); \
	plutil -replace DTXcode             -string "$$dtxcode"         $$plist; \
	plutil -replace DTXcodeBuild        -string "$$xcode_build"     $$plist; \
	plutil -replace DTSDKName           -string "iphoneos$$sdk_ver" $$plist; \
	plutil -replace DTSDKBuild          -string "$$sdk_build"       $$plist; \
	plutil -replace DTPlatformVersion   -string "$$plat_ver"        $$plist; \
	plutil -replace DTPlatformBuild     -string "$$sdk_build"       $$plist; \
	plutil -replace DTCompiler          -string "com.apple.compilers.llvm.clang.1_0" $$plist; \
	plutil -replace BuildMachineOSBuild -string "$$(sw_vers -buildVersion)" $$plist; \
	echo "[ios] toolchain: Xcode $$xcode_ver ($$xcode_build), iphoneos SDK $$sdk_ver ($$sdk_build)"
	@# Sign, when an identity is supplied. The entitlements must be a subset
	@# of the provisioning profile's, so they stay minimal.
	@if [ -n "$(IOS_SIGN_IDENTITY)" ]; then \
	    if [ -z "$(IOS_PROFILE)" ]; then echo "error: IOS_SIGN_IDENTITY set but IOS_PROFILE is empty" >&2; exit 1; fi; \
	    if [ -z "$(IOS_TEAM_ID)" ]; then echo "error: IOS_SIGN_IDENTITY set but IOS_TEAM_ID is empty" >&2; exit 1; fi; \
	    cp "$(IOS_PROFILE)" $(IOS_APP_DIR)/embedded.mobileprovision; \
	    ents=build/ios-device/entitlements.plist; \
	    printf '%s\n' \
	      '<?xml version="1.0" encoding="UTF-8"?>' \
	      '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
	      '<plist version="1.0"><dict>' \
	      '  <key>application-identifier</key><string>$(IOS_TEAM_ID).$(IOS_BUNDLE_ID)</string>' \
	      '  <key>com.apple.developer.team-identifier</key><string>$(IOS_TEAM_ID)</string>' \
	      '  <key>get-task-allow</key><false/>' \
	      '</dict></plist>' > $$ents; \
	    codesign --force --timestamp=none \
	        --sign "$(IOS_SIGN_IDENTITY)" --entitlements $$ents $(IOS_APP_DIR); \
	    codesign --verify --strict --verbose=2 $(IOS_APP_DIR); \
	fi
	cd build/ios-device && rm -f ../$(IOS_APP_NAME).ipa && zip -qr ../$(IOS_APP_NAME).ipa Payload
	@if [ -n "$(IOS_SIGN_IDENTITY)" ]; then \
	    echo "[ios] built $@ (signed: $(IOS_SIGN_IDENTITY), build $(IOS_BUILD_NUMBER))"; \
	else \
	    echo "[ios] built $@ (unsigned)"; \
	fi

# The .ipa under its release name, next to the desktop archives. The pack rule
# that keeps .openbounty out of every archive does not apply here: Rome's pack
# is ours and has to be inside the app.
dist-ios: $(IOS_IPA)
	@mkdir -p $(DIST)
	cp $(IOS_IPA) $(DIST)/gloryofrome-$(OPENBOUNTY_VERSION_SLUG)-ios-arm64.ipa

# ---------------------------------------------------------------------------
# Distribution archives (consumed by GitHub Actions release workflow).
# Each `dist-<platform>` target stages the platform-specific binary plus
# README.txt (rendered from dist/README.txt.in with $(OPENBOUNTY_VERSION)
# substituted), LICENSE, and NOTICES.md, then archives them. The output
# lands in dist/ next to the build/ tree.
#
# CRITICAL: no .openbounty pack file is ever included in a release
# archive, the asset pack is DOS-extracted and copyright-restricted.
# The release workflow has a CI-side belt-and-braces check that fails
# if any dist/*.tar.gz or dist/*.zip contains a .openbounty file.
# ---------------------------------------------------------------------------
DIST    := dist
STAGING := build/staging

# `dist` covers the desktop platforms only. dist-web is deliberately NOT in
# it: it needs emsdk on PATH, which the desktop-only release paths do not.
# The release workflow invokes it from its own job.
dist: dist-linux dist-windows dist-mac

dist-linux: release
	@rm -rf $(STAGING)/linux && mkdir -p $(STAGING)/linux/openbounty-$(OPENBOUNTY_VERSION_SLUG)
	cp build/release/openbounty $(STAGING)/linux/openbounty-$(OPENBOUNTY_VERSION_SLUG)/openbounty
	sed "s/<version>/$(OPENBOUNTY_VERSION_DISPLAY)/g" $(DIST)/README.txt.in > $(STAGING)/linux/openbounty-$(OPENBOUNTY_VERSION_SLUG)/README.txt
	cp LICENSE NOTICES.md $(STAGING)/linux/openbounty-$(OPENBOUNTY_VERSION_SLUG)/
	@mkdir -p $(DIST)
	tar -czf $(DIST)/openbounty-$(OPENBOUNTY_VERSION_SLUG)-linux-x86_64.tar.gz \
	    -C $(STAGING)/linux openbounty-$(OPENBOUNTY_VERSION_SLUG)

# One recipe line per command, not a `for` loop. The loop's backslash
# continuations made the whole body a single shell command, so make only ever
# saw the exit status of its last one -- the zip. A failing `cp LICENSE
# NOTICES.md` left make reporting success and shipped archives with no licence
# text. The other five games are written this way; this one was the exception.
dist-windows: $(OUT_WIN64) $(OUT_WIN32)
	@rm -rf $(STAGING)/win-x86_64 $(STAGING)/win-i686 && mkdir -p $(DIST) \
	    $(STAGING)/win-x86_64/openbounty-$(OPENBOUNTY_VERSION_SLUG) \
	    $(STAGING)/win-i686/openbounty-$(OPENBOUNTY_VERSION_SLUG)
	cp $(OUT_WIN64) $(STAGING)/win-x86_64/openbounty-$(OPENBOUNTY_VERSION_SLUG)/openbounty.exe
	sed "s/<version>/$(OPENBOUNTY_VERSION_DISPLAY)/g" $(DIST)/README.txt.in > $(STAGING)/win-x86_64/openbounty-$(OPENBOUNTY_VERSION_SLUG)/README.txt
	cp LICENSE NOTICES.md $(STAGING)/win-x86_64/openbounty-$(OPENBOUNTY_VERSION_SLUG)/
	cp $(OUT_WIN32) $(STAGING)/win-i686/openbounty-$(OPENBOUNTY_VERSION_SLUG)/openbounty.exe
	sed "s/<version>/$(OPENBOUNTY_VERSION_DISPLAY)/g" $(DIST)/README.txt.in > $(STAGING)/win-i686/openbounty-$(OPENBOUNTY_VERSION_SLUG)/README.txt
	cp LICENSE NOTICES.md $(STAGING)/win-i686/openbounty-$(OPENBOUNTY_VERSION_SLUG)/
	(cd $(STAGING)/win-x86_64 && zip -qr ../../../$(DIST)/openbounty-$(OPENBOUNTY_VERSION_SLUG)-windows-x86_64.zip openbounty-$(OPENBOUNTY_VERSION_SLUG))
	(cd $(STAGING)/win-i686 && zip -qr ../../../$(DIST)/openbounty-$(OPENBOUNTY_VERSION_SLUG)-windows-i686.zip openbounty-$(OPENBOUNTY_VERSION_SLUG))

dist-mac: $(OUT_MAC)
	@rm -rf $(STAGING)/mac && mkdir -p $(STAGING)/mac/openbounty-$(OPENBOUNTY_VERSION_SLUG)
	cp $(OUT_MAC) $(STAGING)/mac/openbounty-$(OPENBOUNTY_VERSION_SLUG)/openbounty
	codesign --force --sign - --options runtime $(STAGING)/mac/openbounty-$(OPENBOUNTY_VERSION_SLUG)/openbounty
	sed "s/<version>/$(OPENBOUNTY_VERSION_DISPLAY)/g" $(DIST)/README.txt.in > $(STAGING)/mac/openbounty-$(OPENBOUNTY_VERSION_SLUG)/README.txt
	cp LICENSE NOTICES.md $(STAGING)/mac/openbounty-$(OPENBOUNTY_VERSION_SLUG)/
	@mkdir -p $(DIST)
	(cd $(STAGING)/mac && zip -qr ../../../$(DIST)/openbounty-$(OPENBOUNTY_VERSION_SLUG)-macos-universal.zip openbounty-$(OPENBOUNTY_VERSION_SLUG))

# ---------------------------------------------------------------------------
# Glory of Rome desktop packages: the same binary as the OpenBounty archives
# above, plus the Rome pack in assets/ beside it -- the directory pack
# discovery already searches (src/main.c), so the game starts with no flags.
# Rome's pack is ours to ship; King's Bounty's never is, and the release
# workflow checks both halves of that rule.
# ---------------------------------------------------------------------------
ROME_PACK_FILE := $(PACK_DIR)/glory-of-rome.openbounty
ROME_SLUG      := gloryofrome-$(OPENBOUNTY_VERSION_SLUG)

# $(call rome_stage,<staging-dir>,<binary>,<binary-name>)
define rome_stage
	@rm -rf $(1) && mkdir -p $(1)/$(ROME_SLUG)/assets
	cp $(2) $(1)/$(ROME_SLUG)/$(3)
	cp $(ROME_PACK_FILE) $(1)/$(ROME_SLUG)/assets/glory-of-rome.openbounty
	sed "s/<version>/$(OPENBOUNTY_VERSION_DISPLAY)/g" $(DIST)/README-rome.txt.in > $(1)/$(ROME_SLUG)/README.txt
	cp LICENSE NOTICES.md $(1)/$(ROME_SLUG)/
	@mkdir -p $(DIST)
endef

dist-rome-linux: release $(ROME_PACK_FILE)
	$(call rome_stage,$(STAGING)/rome-linux,build/release/openbounty,openbounty)
	tar -czf $(DIST)/$(ROME_SLUG)-linux-x86_64.tar.gz -C $(STAGING)/rome-linux $(ROME_SLUG)

dist-rome-windows: $(OUT_WIN64) $(OUT_WIN32) $(ROME_PACK_FILE)
	$(call rome_stage,$(STAGING)/rome-win64,$(OUT_WIN64),openbounty.exe)
	(cd $(STAGING)/rome-win64 && zip -qr ../../../$(DIST)/$(ROME_SLUG)-windows-x86_64.zip $(ROME_SLUG))
	$(call rome_stage,$(STAGING)/rome-win32,$(OUT_WIN32),openbounty.exe)
	(cd $(STAGING)/rome-win32 && zip -qr ../../../$(DIST)/$(ROME_SLUG)-windows-i686.zip $(ROME_SLUG))

dist-rome-mac: $(OUT_MAC) $(ROME_PACK_FILE)
	$(call rome_stage,$(STAGING)/rome-mac,$(OUT_MAC),openbounty)
	codesign --force --sign - --options runtime $(STAGING)/rome-mac/$(ROME_SLUG)/openbounty
	(cd $(STAGING)/rome-mac && zip -qr ../../../$(DIST)/$(ROME_SLUG)-macos-universal.zip $(ROME_SLUG))

# All four emitted files are required to run it: the .js loader, the .wasm
# module, the .data pack image, and the .html shell. Serve them over HTTP --
# browsers refuse to fetch .wasm/.data over file://.
# Two zips, one per game: openbounty-* carries King's Bounty and gloryofrome-*
# Glory of Rome, each with its pack embedded in the .data image. The site pulls
# each by its prefix into its own URL.
# $(call web_stage,<pack-name>,<zip-prefix>,<readme-template>)
define web_stage
	@rm -rf $(STAGING)/web-$(2) && mkdir -p $(STAGING)/web-$(2)/$(2)-$(OPENBOUNTY_VERSION_SLUG)-web
	cp build/web/$(1)/openbounty.html build/web/$(1)/openbounty.js \
	   build/web/$(1)/openbounty.wasm build/web/$(1)/openbounty.data \
	   $(STAGING)/web-$(2)/$(2)-$(OPENBOUNTY_VERSION_SLUG)-web/
	sed "s/<version>/$(OPENBOUNTY_VERSION_DISPLAY)/g" $(3) > $(STAGING)/web-$(2)/$(2)-$(OPENBOUNTY_VERSION_SLUG)-web/README.txt
	cp LICENSE NOTICES.md $(STAGING)/web-$(2)/$(2)-$(OPENBOUNTY_VERSION_SLUG)-web/
	@mkdir -p $(DIST)
	(cd $(STAGING)/web-$(2) && zip -qr ../../../$(DIST)/$(2)-$(OPENBOUNTY_VERSION_SLUG)-web-wasm.zip $(2)-$(OPENBOUNTY_VERSION_SLUG)-web)
endef

dist-web: build/web/kings-bounty/openbounty.html $(OUT_WEB_ROME)
	$(call web_stage,kings-bounty,openbounty,$(DIST)/README.txt.in)
	$(call web_stage,glory-of-rome,gloryofrome,$(DIST)/README-rome.txt.in)

# Android ships as the APK and the AAB themselves -- no archive, no README
# alongside: a store artifact is a single signed file. Both carry the Glory of
# Rome pack inside them, which is ours to distribute (the release workflow's
# guard is about King's Bounty's DOS-extracted pack, which never reaches here).
dist-android: $(ANDROID_APK)
	@mkdir -p $(DIST)
	cp $(ANDROID_APK) $(DIST)/gloryofrome-$(OPENBOUNTY_VERSION_SLUG)-android-arm64.apk

dist-android-play: $(ANDROID_AAB)
	@mkdir -p $(DIST)
	cp $(ANDROID_AAB) $(DIST)/gloryofrome-$(OPENBOUNTY_VERSION_SLUG)-android.aab

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
# + test sources + libobengine.a.
TEST_SRC := $(filter-out src/main.c,$(SHELL_SRC)) $(TOOL_SRC) \
            $(DEMO_SRC) $(AUTOPLAY_SRC) \
            $(TEST_ONLY_SRC)

$(OUT_TEST): $(TEST_SRC) $(OUT_ENGLIB) build/version.h Makefile | build
	gcc $(CFLAGS) -Ithird_party/greatest -Itests $(TEST_SRC) $(OUT_ENGLIB) -o $(OUT_TEST) $(LDFLAGS)

# ---------------------------------------------------------------------------
# libobengine.a, engine compiled as a static archive. Consumers link
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

$(ENGLIB_OBJ_DIR)/%.o: %.c Makefile | $(ENGLIB_OBJ_DIR) build/version.h
	@mkdir -p $(dir $@)
	gcc $(ENGLIB_CFLAGS) -MMD -MP -c $< -o $@

$(ENGLIB_OBJ_DIR):
	mkdir -p $@

# Header-dependency tracking. -MMD -MP (above) emits a .d file next to
# each .o listing the headers it included; -include pulls those in so an
# edited header (e.g. a struct-layout change in internal.h) forces a
# rebuild of every object that includes it. Without this, the per-object
# rules track only the .c file, so an incremental `make` after editing a
# header produces STALE objects compiled against the old layout, a
# notorious source of mismatched-struct segfaults. The minus on -include
# suppresses the not-found warning on a clean tree.
ALL_OBJS := $(SHELL_OBJ) $(TOOL_OBJ) $(ENGLIB_OBJ)
-include $(ALL_OBJS:.o=.d)

$(OUT_ENGLIB): $(ENGLIB_OBJ) | build
	ar rcs $@ $(ENGLIB_OBJ)

# Single test command. greatest runs the entire suite: unit tests,
# combat-formula digests, everything. The library-boundary check is a
# build-time link verification, $(LIBTEST_STAMP) is in $(all) so a
# regression where engine code depends on shell headers will fail at
# `make all` time.
test: $(OUT_TEST)
	@./$(OUT_TEST)

# ---------------------------------------------------------------------------
# Library boundary check. Compiles a minimal consumer against
# libobengine.a + engine/host_noop.c using ONLY engine include paths
# (no -Isrc) and only -lm -lpthread (no raylib, no X11). If the engine
# starts depending on shell headers or shell symbols, this build fails
# and `make all` fails. No binary is emitted, the link verifies symbol
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
# This link ALSO includes the demo objects ($(DEMO_OBJ)) and the autoplay
# objects ($(AUTOPLAY_OBJ)) so both agents' engine-only boundaries are
# build-enforced too, without a separate binary: consumer.c provides the entry
# point, and the link uses only -lm -lpthread (no raylib, no -Isrc). If any
# demo, autoplay, or engine object reaches a shell symbol, this fails and
# `make all` fails. No binary is emitted, the link is discarded.
# Uniform player-IO emit guard (engine/include/player_io.h, M6). The engine must
# raise informational messages ONLY through the player-IO queue
# (player_io_message), never the open_dialog host callback directly, that was the
# pre-refactor "out-of-band" path. Engine source (excluding the host_noop default
# impl and the ui_host.h declaration) must contain zero `open_dialog(` call sites.
# A regression here fails `make all`.
ENGINE_NODIALOG_SRC := $(filter-out engine/host_noop.c,$(ENGINE_SRC))

# Touch guard (src/uitouch.h). A screen says what a thing IS; the widget layer
# decides how a finger finds it -- what size it must be, what beats what, and
# which mode is which. The raw region API may be called from src/uitouch.c
# alone, and a page's own tap rule (touch_page) from the page engine
# (src/modern/page.c) alone. A regression fails `make all`.
TOUCH_GUARD_SRC := $(filter-out src/uitouch.c src/touch.c,$(SHELL_SRC))
TOUCH_PAGE_GUARD_SRC := $(filter-out src/modern/page.c,$(TOUCH_GUARD_SRC))

$(TOUCH_STAMP): $(TOUCH_GUARD_SRC) | build
	@bad=`grep -nE '\btouch_region[a-z_]*[[:space:]]*\(' $(TOUCH_GUARD_SRC) 2>/dev/null || true`; \
	bad="$$bad`grep -nE '\btouch_page[[:space:]]*\(' $(TOUCH_PAGE_GUARD_SRC) 2>/dev/null || true`"; \
	if [ -n "$$bad" ]; then \
	  echo "touch guard FAILED: a screen registers a raw region or a page's taps (use src/uitouch.h, src/modern/page.h):"; \
	  echo "$$bad"; \
	  exit 1; \
	fi
	@touch $@

# Page guard (src/modern/page.h). Where a modern panel goes, and the ring and
# the dim around it, are the page engine's alone. No shell file draws a ring
# or a panel of its own except the page engine (src/modern/page.c), the frame
# (src/chrome.c) and the lattice itself (src/lattice.c); legacy's panels go
# through legacy_window_frame and legacy_panel_frame (src/ui.c), which draw
# nothing in modern. A regression fails `make all`.
PAGE_GUARD_SRC := $(filter-out src/modern/page.c src/chrome.c src/lattice.c src/ui.c,$(SHELL_SRC))

$(PAGE_STAMP): $(PAGE_GUARD_SRC) | build
	@bad=`grep -nE '\b(lattice_ring|uk_panel|ui_window_frame|ui_panel_frame)[[:space:]]*\(' $(PAGE_GUARD_SRC) 2>/dev/null || true`; \
	if [ -n "$$bad" ]; then \
	  echo "page guard FAILED: a file draws a panel of its own (use src/modern/page.h):"; \
	  echo "$$bad"; \
	  exit 1; \
	fi
	@touch $@

$(LIBTEST_STAMP): tests/library/consumer.c engine/host_noop.c $(DEMO_OBJ) $(AUTOPLAY_OBJ) $(OUT_ENGLIB) | build
	@bad=`grep -nE '\bopen_dialog[[:space:]]*\(' $(ENGINE_NODIALOG_SRC) 2>/dev/null || true`; \
	if [ -n "$$bad" ]; then \
	  echo "uniform-io guard FAILED: engine calls open_dialog directly (use player_io_message):"; \
	  echo "$$bad"; \
	  exit 1; \
	fi
	gcc $(LIBTEST_CFLAGS) tests/library/consumer.c engine/host_noop.c \
	    $(DEMO_OBJ) $(AUTOPLAY_OBJ) \
	    -Wl,--whole-archive $(OUT_ENGLIB) -Wl,--no-whole-archive \
	    -o $@.tmp $(LIBTEST_LDFLAGS)
	@rm -f $@.tmp
	@touch $@

# ---------------------------------------------------------------------------
# iOS purity check: every shell file the iOS build will compile must type-check
# with -DPLATFORM_IOS and NO raylib include path at all. iOS links no raylib
# (docs/IOS-BACKEND.md); this is what keeps the seams honest on a machine
# with no Apple toolchain, long before a macOS runner ever sees the code.
#
# The excluded list is the desktop-only subsystems -- recorder, mp4 encoder,
# screenshot, gallery, pack picker, demo and autoplay drivers, combat replay --
# plus the six backends that ARE raylib by definition.

$(IOS_CHECK_STAMP): $(IOS_CHECK_SRC) $(IOS_OWN_C) $(wildcard src/*.h src/*/*.h ios/*.h) build/version.h | build
	@for f in $(IOS_CHECK_SRC) $(IOS_OWN_C); do \
	  gcc -fsyntax-only -std=c99 -Wall -Wextra -DPLATFORM_IOS \
	      -Isrc -Iios -Iengine/include -Idemo -Iautoplay -Itools -Ibuild \
	      -Ithird_party/cjson -Ithird_party/miniz -Ithird_party/stb $$f || exit 1; \
	done
	@touch $@

# ---------------------------------------------------------------------------
# demo + autoplay objects, each compiled ENGINE-ONLY (no -Isrc, no raylib) and
# WITHOUT the other agent's include dir, so engine-purity AND the demo/autoplay
# independence are all build-enforced: an include across any fence fails to
# compile.
# ---------------------------------------------------------------------------
DEMO_CFLAGS := -std=c99 -Wall -Wextra $(CFLAGS_$(BUILD)) $(OB_EXTRA_CFLAGS) -Idemo -Iengine/headless -Iengine/include -Ithird_party/cjson

$(DEMO_OBJ_DIR)/%.o: %.c Makefile | build
	@mkdir -p $(dir $@)
	gcc $(DEMO_CFLAGS) -MMD -MP -c $< -o $@

-include $(DEMO_OBJ:.o=.d)

AUTOPLAY_CFLAGS := -std=c99 -Wall -Wextra $(CFLAGS_$(BUILD)) $(OB_EXTRA_CFLAGS) -Iautoplay -Iengine/headless -Iengine/include -Ithird_party/cjson

$(AUTOPLAY_OBJ_DIR)/%.o: %.c Makefile | build
	@mkdir -p $(dir $@)
	gcc $(AUTOPLAY_CFLAGS) -MMD -MP -c $< -o $@

-include $(AUTOPLAY_OBJ:.o=.d)

# ---------------------------------------------------------------------------
# extract, produce a .openbounty pack (or a loose tree, with --out-dir)
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

.PHONY: all run release run-release windows windows-debug mac web web-kings-bounty web-glory-of-rome web-serve android android-play dist-android dist-android-play ios ios-sim dist-ios dist-rome-linux dist-rome-windows dist-rome-mac clean test extract extract-pack dist dist-linux dist-windows dist-mac dist-web
