#!/usr/bin/env bash
# Build raylib from source for Android and install its headers + per-ABI static
# archive into third_party/raylib-install-android/<abi>/{include,lib} (the paths
# the Makefile's `android` target expects). The install dir is gitignored, so
# CI runs this before `make android`.
#
# raylib ships a first-class PLATFORM_ANDROID target in src/Makefile that drives
# the NDK's Clang toolchain directly (no Gradle). We build a STATIC archive per
# ABI; the game links libraylib.a into libgloryofrome.so. Pinned to raylib 6.0 to
# match the desktop builds; bump RAYLIB_TAG to move.
#
# Usage:
#   scripts/build_raylib_android.sh --ndk <dir> [--api <level>] [--arch <name>]...
#
#   --ndk   the Android NDK root (required)
#   --api   target API level (default 24)
#   --arch  a raylib arch name to build, repeatable (default arm64);
#           valid: arm64 arm x86_64 x86
#
# Everything comes from the command line: the script reads no environment
# variables.

set -euo pipefail

RAYLIB_TAG="6.0"
RAYLIB_SRC_DIR="third_party/raylib"
INSTALL_DIR="third_party/raylib-install-android"
ANDROID_NDK=""
ANDROID_API="24"
ANDROID_ARCHES=""

usage() {
    echo "usage: $0 --ndk <dir> [--api <level>] [--arch <name>]..." >&2
    exit 2
}

while [ $# -gt 0 ]; do
    case "$1" in
        --ndk)  [ $# -ge 2 ] || usage; ANDROID_NDK="$2"; shift 2 ;;
        --api)  [ $# -ge 2 ] || usage; ANDROID_API="$2"; shift 2 ;;
        --arch) [ $# -ge 2 ] || usage; ANDROID_ARCHES="$ANDROID_ARCHES $2"; shift 2 ;;
        *) echo "[build_raylib_android] unknown argument: $1" >&2; usage ;;
    esac
done

[ -n "$ANDROID_NDK" ] || usage
[ -d "$ANDROID_NDK" ] || { echo "[build_raylib_android] no NDK at $ANDROID_NDK" >&2; exit 2; }
[ -n "$ANDROID_ARCHES" ] || ANDROID_ARCHES="arm64"

if [ ! -d "$RAYLIB_SRC_DIR" ]; then
    git clone --depth 1 --branch "$RAYLIB_TAG" \
        https://github.com/raysan5/raylib "$RAYLIB_SRC_DIR"
fi

# Map raylib's ANDROID_ARCH names to the Android ABI directory names used inside
# an APK's lib/ tree (and by the Makefile when it assembles the .so).
abi_for_arch() {
    case "$1" in
        arm64)  echo "arm64-v8a" ;;
        arm)    echo "armeabi-v7a" ;;
        x86_64) echo "x86_64" ;;
        x86)    echo "x86" ;;
        *) echo "unknown-arch:$1" >&2; return 1 ;;
    esac
}

# raylib's Android backend ignores eglChooseConfig's result, so when no
# framebuffer configuration matches its request -- 24-bit depth, which the
# Android emulator's software GL does not offer for an ES2 context -- it
# carries on with an unset config and dies at eglCreateContext with
# EGL_BAD_CONFIG and no diagnosis. The patch asks for 24, then 16, then no
# depth buffer, and says which it got. Idempotent: the raylib tree is shared
# with the other build scripts and survives between runs.
PATCH="$(cd "$(dirname "$0")" && pwd)/raylib-android-eglconfig.patch"
if git -C "$RAYLIB_SRC_DIR" apply --reverse --check "$PATCH" 2>/dev/null; then
    echo "[build_raylib_android] EGL config patch already applied"
else
    git -C "$RAYLIB_SRC_DIR" apply "$PATCH"
    echo "[build_raylib_android] applied EGL config patch"
fi

for arch in $ANDROID_ARCHES; do
    abi="$(abi_for_arch "$arch")"
    echo "[build_raylib_android] building raylib $RAYLIB_TAG for $arch ($abi), API $ANDROID_API"

    make -C "$RAYLIB_SRC_DIR/src" clean
    make -C "$RAYLIB_SRC_DIR/src" \
        PLATFORM=PLATFORM_ANDROID \
        RAYLIB_LIBTYPE=STATIC \
        ANDROID_NDK="$ANDROID_NDK" \
        ANDROID_ARCH="$arch" \
        ANDROID_API_VERSION="$ANDROID_API" \
        CUSTOM_CFLAGS=-w \
        -j"$(nproc)"

    dest="$INSTALL_DIR/$abi"
    mkdir -p "$dest/include" "$dest/lib"
    cp "$RAYLIB_SRC_DIR/src/raylib.h" \
       "$RAYLIB_SRC_DIR/src/raymath.h" \
       "$RAYLIB_SRC_DIR/src/rlgl.h" "$dest/include/"
    cp "$RAYLIB_SRC_DIR/src/libraylib.a" "$dest/lib/"

    echo "[build_raylib_android] installed -> $dest"
done
