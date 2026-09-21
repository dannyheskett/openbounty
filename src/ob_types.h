// src/ob_types.h
//
// The geometry, colour and texture types the shell draws with, decoupled from
// raylib so an iOS build (which links no raylib at all) still compiles the
// shared C. On every raylib platform -- desktop, web, Android -- this is just
// <raylib.h>, so those builds are byte-for-byte unchanged.
//
// This is the shell's counterpart to engine/headless/raylib_stub.h, which does
// the same job for the engine: there, the stub is enough because the engine
// never draws. Here the declarations are real and ios/ implements them.
//
// See docs/IOS-BACKEND-SPIKE.md for the full surface and why it is split this
// way.

#ifndef OB_TYPES_H
#define OB_TYPES_H

#if !defined(PLATFORM_IOS)

#include "raylib.h"

#else   // PLATFORM_IOS: no raylib -- provide the compatible surface ourselves.

#include <stdbool.h>

// Layout-identical to raylib's, so the shared code's struct literals and
// field accesses are unchanged.
typedef struct { float x, y; }                  Vector2;
typedef struct { float x, y, width, height; }   Rectangle;
typedef struct { unsigned char r, g, b, a; }    Color;

// A texture is an opaque handle plus its size: the shell only ever asks for
// width/height and hands the handle back to gfx_*. `id` indexes the Metal
// backend's texture table (0 = none), matching how raylib's `id` is a GL name.
typedef struct {
    unsigned int id;
    int width, height;
    int mipmaps, format;    // unused on iOS; present so sizeof/layout match
} Texture2D;

// The offscreen frame buffer (src/present.c). `texture` is what gets blitted.
typedef struct {
    unsigned int id;
    Texture2D texture;
    Texture2D depth;
} RenderTexture2D;

// CPU-side decoded pixels, as produced by stb_image.
typedef struct {
    void *data;
    int width, height;
    int mipmaps, format;
} Image;

// raylib's named colours, exact RGBA, so colour literals in the shell resolve.
#define BLACK     ((Color){   0,   0,   0, 255 })
#define WHITE     ((Color){ 255, 255, 255, 255 })
#define BLANK     ((Color){   0,   0,   0,   0 })
#define LIGHTGRAY ((Color){ 200, 200, 200, 255 })
#define GRAY      ((Color){ 130, 130, 130, 255 })
#define DARKGRAY  ((Color){  80,  80,  80, 255 })
#define YELLOW    ((Color){ 253, 249,   0, 255 })
#define GOLD      ((Color){ 255, 203,   0, 255 })
#define ORANGE    ((Color){ 255, 161,   0, 255 })
#define RED       ((Color){ 230,  41,  55, 255 })
#define MAROON    ((Color){ 190,  33,  55, 255 })
#define GREEN     ((Color){   0, 228,  48, 255 })
#define BLUE      ((Color){   0, 121, 241, 255 })
#define PURPLE    ((Color){ 200, 122, 255, 255 })
#define MAGENTA   ((Color){ 255,   0, 255, 255 })

// Texture filter / wrap modes. Values match raylib's enums so the call sites
// that name them need no change; only POINT and CLAMP are used by the pack's
// pixel art, but the rest are declared so a stray reference still compiles.
enum { TEXTURE_FILTER_POINT = 0, TEXTURE_FILTER_BILINEAR = 1 };
enum { TEXTURE_WRAP_REPEAT = 0, TEXTURE_WRAP_CLAMP = 1 };

#endif  // PLATFORM_IOS

#endif  // OB_TYPES_H
