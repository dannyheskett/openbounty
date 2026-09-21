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
# Required env:
#   ANDROID_NDK        path to the NDK root (CI's setup-android exports this)
# Optional env:
#   ANDROID_API        target API level         (default 24)
#   ANDROID_ARCHES     space-separated raylib arch names to build (default arm64)
#                      valid: arm64 arm x86_64 x86

set -euo pipefail

RAYLIB_TAG="${RAYLIB_TAG:-6.0}"
RAYLIB_SRC_DIR="${RAYLIB_SRC_DIR:-third_party/raylib}"
INSTALL_DIR="${RAYLIB_ANDROID_INSTALL_DIR:-third_party/raylib-install-android}"
ANDROID_API="${ANDROID_API:-24}"
ANDROID_ARCHES="${ANDROID_ARCHES:-arm64}"

: "${ANDROID_NDK:?set ANDROID_NDK to the Android NDK root}"

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
