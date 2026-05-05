#!/usr/bin/env bash
# Build a universal (arm64 + x86_64) raylib static archive on a macOS
# runner. Drops the result into
# third_party/raylib-install-mac/lib/libraylib.a (the path the Makefile
# already expects).
#
# Why: same reason as build_raylib_linux.sh — the bundled .a was built
# against a specific clang/macOS-SDK combo. Rebuilding inside the
# runner guarantees ABI compatibility with whatever Xcode is installed.
#
# Pinned to raylib 6.0 to match the version OpenBounty's source headers
# come from.

set -euo pipefail

RAYLIB_TAG="${RAYLIB_TAG:-6.0}"
RAYLIB_SRC_DIR="${RAYLIB_SRC_DIR:-third_party/raylib}"
INSTALL_MAC="${INSTALL_MAC:-third_party/raylib-install-mac}"

if [ ! -d "$RAYLIB_SRC_DIR" ]; then
    git clone --depth 1 --branch "$RAYLIB_TAG" \
        https://github.com/raysan5/raylib "$RAYLIB_SRC_DIR"
fi

# Build the universal archive in two passes (arm64 then x86_64), then
# lipo them together. raylib's Makefile doesn't natively support
# multi-arch with -arch flags in CFLAGS because clang refuses to emit
# precompiled objects with mixed archs in some places. Two builds + lipo
# is the standard recipe and what raylib's own CI does.
ARCH_DIR="$(pwd)/build/raylib-mac"
rm -rf "$ARCH_DIR"
mkdir -p "$ARCH_DIR/arm64" "$ARCH_DIR/x86_64"

build_arch() {
    local arch="$1"
    echo "[build_raylib_mac] Building raylib for $arch"
    make -C "$RAYLIB_SRC_DIR/src" clean
    make -C "$RAYLIB_SRC_DIR/src" \
        PLATFORM=PLATFORM_DESKTOP_GLFW \
        RAYLIB_LIBTYPE=STATIC \
        CUSTOM_CFLAGS="-arch $arch" \
        -j"$(sysctl -n hw.ncpu)"
    cp "$RAYLIB_SRC_DIR/src/libraylib.a" "$ARCH_DIR/$arch/libraylib.a"
}

build_arch arm64
build_arch x86_64

mkdir -p "$INSTALL_MAC/lib"
lipo -create -output "$INSTALL_MAC/lib/libraylib.a" \
    "$ARCH_DIR/arm64/libraylib.a" \
    "$ARCH_DIR/x86_64/libraylib.a"

echo "[build_raylib_mac] Wrote universal $INSTALL_MAC/lib/libraylib.a"
lipo -info "$INSTALL_MAC/lib/libraylib.a"
