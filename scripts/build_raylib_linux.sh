#!/usr/bin/env bash
# Build raylib from source on a Linux runner and overwrite the bundled
# static archive at third_party/raylib-install/lib/libraylib.a.
#
# Why: the .a committed to the repo was built on a host with glibc
# 2.38+, which exposes versioned symbols (__isoc23_sscanf, etc.) that
# Ubuntu 22.04 (glibc 2.35) lacks. Linking that prebuilt .a on a 22.04
# runner fails with "undefined reference to __isoc23_sscanf". By
# rebuilding raylib inside the runner we always link against the
# runner's libc and the resulting binary works on 22.04 and newer.
#
# Pinned to raylib 6.0 (matches the version OpenBounty's source headers
# come from). Bump RAYLIB_TAG when upgrading.

set -euo pipefail

RAYLIB_TAG="${RAYLIB_TAG:-6.0}"
RAYLIB_SRC_DIR="${RAYLIB_SRC_DIR:-third_party/raylib}"
RAYLIB_INSTALL_DIR="${RAYLIB_INSTALL_DIR:-third_party/raylib-install}"

if [ ! -d "$RAYLIB_SRC_DIR" ]; then
    git clone --depth 1 --branch "$RAYLIB_TAG" \
        https://github.com/raysan5/raylib "$RAYLIB_SRC_DIR"
fi

# Build the static archive only. PLATFORM_DESKTOP_GLFW is the standard
# Linux desktop target. We do not need shared library, examples, or
# extras — the engine links libraylib.a directly.
make -C "$RAYLIB_SRC_DIR/src" clean
make -C "$RAYLIB_SRC_DIR/src" \
    PLATFORM=PLATFORM_DESKTOP_GLFW \
    RAYLIB_LIBTYPE=STATIC \
    -j"$(nproc)"

mkdir -p "$RAYLIB_INSTALL_DIR/lib"
cp "$RAYLIB_SRC_DIR/src/libraylib.a" "$RAYLIB_INSTALL_DIR/lib/libraylib.a"

echo "[build_raylib_linux] Replaced $RAYLIB_INSTALL_DIR/lib/libraylib.a"
nm "$RAYLIB_INSTALL_DIR/lib/libraylib.a" 2>/dev/null | grep -c 'isoc23' || true
