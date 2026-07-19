#!/usr/bin/env bash
# Build a WebAssembly raylib static archive with Emscripten and drop it
# into third_party/raylib-install-web/ (the path the Makefile's `web`
# target expects).
#
# Why a separate archive: the committed desktop .a files are native
# object code. The web target needs raylib compiled to wasm32 by emcc,
# against raylib's PLATFORM_WEB backend (GLES2 -> WebGL2, GLFW emulated
# by Emscripten's -sUSE_GLFW=3 shim) rather than the desktop GLFW one.
#
# Pinned to raylib 6.0 to match the version OpenBounty's source headers
# come from, same as the linux/mac/windows scripts. Bump all four
# together when upgrading.
#
# Prerequisite: emsdk activated, so emcc/emmake are on PATH. From the
# repo root:
#     source third_party/emsdk/emsdk_env.sh
#     ./scripts/build_raylib_web.sh

set -euo pipefail

RAYLIB_TAG="6.0"
RAYLIB_SRC_DIR="third_party/raylib"
RAYLIB_INSTALL_DIR="third_party/raylib-install-web"

if ! command -v emmake >/dev/null 2>&1; then
    echo "[build_raylib_web] emmake not on PATH." >&2
    echo "[build_raylib_web] Run: source third_party/emsdk/emsdk_env.sh" >&2
    exit 1
fi

if [ ! -d "$RAYLIB_SRC_DIR" ]; then
    git clone --depth 1 --branch "$RAYLIB_TAG" \
        https://github.com/raysan5/raylib "$RAYLIB_SRC_DIR"
fi

# PLATFORM_WEB selects raylib's Emscripten backend; GRAPHICS_API_OPENGL_ES2
# is the WebGL-compatible renderer. emmake puts emcc in front of the compiler
# so every object lands as wasm32. The clean is mandatory: the raylib source
# tree is shared with the desktop scripts, so a stale native .o would
# silently poison the archive. -w silences raylib's own warnings, which are
# not actionable here.
emmake make -C "$RAYLIB_SRC_DIR/src" clean
emmake make -C "$RAYLIB_SRC_DIR/src" \
    PLATFORM=PLATFORM_WEB \
    GRAPHICS=GRAPHICS_API_OPENGL_ES2 \
    RAYLIB_LIBTYPE=STATIC \
    CUSTOM_CFLAGS=-w \
    -j"$(nproc)"

mkdir -p "$RAYLIB_INSTALL_DIR/lib" "$RAYLIB_INSTALL_DIR/include"
# PLATFORM_WEB may name its archive libraylib.web.a (raylib's Makefile
# suffixes by platform) or plain libraylib.a depending on the tag. Install
# either under the plain name so -lraylib resolves as on every other target.
if [ -f "$RAYLIB_SRC_DIR/src/libraylib.web.a" ]; then
    cp "$RAYLIB_SRC_DIR/src/libraylib.web.a" "$RAYLIB_INSTALL_DIR/lib/libraylib.a"
else
    cp "$RAYLIB_SRC_DIR/src/libraylib.a" "$RAYLIB_INSTALL_DIR/lib/libraylib.a"
fi
# Headers are byte-identical across all four install dirs (same 6.0 tag);
# copying them here keeps this dir self-contained like the others.
for h in raylib.h raymath.h rlgl.h; do
    cp "$RAYLIB_SRC_DIR/src/$h" "$RAYLIB_INSTALL_DIR/include/$h"
done

echo "[build_raylib_web] Wrote $RAYLIB_INSTALL_DIR/lib/libraylib.a"
