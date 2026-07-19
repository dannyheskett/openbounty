#!/usr/bin/env bash
# Cross-compile raylib for Windows on a Linux runner using mingw-w64.
# Builds both x86_64 and i686 static archives and drops them into
# third_party/raylib-install-win64/lib/libraylib.a and
# third_party/raylib-install-win32/lib/libraylib.a (the locations the
# Makefile already expects).
#
# Why: same reason as build_raylib_linux.sh, the bundled .a files were
# built on a host with a different mingw runtime (libgcc / winpthread
# ABI), and linking against them on a fresh GitHub runner can fail with
# undefined references or runtime crashes. Rebuild against the runner's
# mingw toolchain to guarantee ABI match.
#
# Pinned to raylib 6.0 to match the version OpenBounty's source headers
# come from.

set -euo pipefail

RAYLIB_TAG="${RAYLIB_TAG:-6.0}"
RAYLIB_SRC_DIR="${RAYLIB_SRC_DIR:-third_party/raylib}"
INSTALL_WIN64="${INSTALL_WIN64:-third_party/raylib-install-win64}"
INSTALL_WIN32="${INSTALL_WIN32:-third_party/raylib-install-win32}"

if [ ! -d "$RAYLIB_SRC_DIR" ]; then
    git clone --depth 1 --branch "$RAYLIB_TAG" \
        https://github.com/raysan5/raylib "$RAYLIB_SRC_DIR"
fi

build_win() {
    local arch_label="$1"   # win64 | win32
    local cc="$2"           # x86_64-w64-mingw32-gcc | i686-w64-mingw32-gcc
    local ar="$3"           # x86_64-w64-mingw32-ar  | i686-w64-mingw32-ar
    local install_dir="$4"

    echo "[build_raylib_windows] Building raylib for $arch_label"
    make -C "$RAYLIB_SRC_DIR/src" clean
    make -C "$RAYLIB_SRC_DIR/src" \
        PLATFORM=PLATFORM_DESKTOP_GLFW \
        OS=Windows_NT \
        CC="$cc" \
        AR="$ar" \
        RAYLIB_LIBTYPE=STATIC \
        -j"$(nproc)"

    mkdir -p "$install_dir/lib" "$install_dir/include"
    cp "$RAYLIB_SRC_DIR/src/libraylib.a" "$install_dir/lib/libraylib.a"
    # Headers too: the install dirs are gitignored, so this script is the
    # only thing that puts raylib.h where the Makefile's -I expects it.
    cp "$RAYLIB_SRC_DIR/src/raylib.h" \
       "$RAYLIB_SRC_DIR/src/raymath.h" \
       "$RAYLIB_SRC_DIR/src/rlgl.h" "$install_dir/include/"
    echo "[build_raylib_windows] Wrote $install_dir/{lib,include}"
}

build_win win64 x86_64-w64-mingw32-gcc x86_64-w64-mingw32-ar "$INSTALL_WIN64"
build_win win32 i686-w64-mingw32-gcc   i686-w64-mingw32-ar   "$INSTALL_WIN32"
