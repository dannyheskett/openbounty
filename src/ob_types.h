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

// Key ids. Values are raylib's KeyboardKey, so every call site keeps naming
// KEY_* and the injected-key queue in src/input_host.c carries the same ids on
// every platform. Only the keys this game actually reads are listed -- iOS has
// no keyboard, but the touch layer injects these ids, so they must exist.
enum {
    KEY_NULL = 0,
    KEY_BACK = 4,
    KEY_SPACE = 32,
    KEY_ZERO = 48,
    KEY_ONE = 49,
    KEY_TWO = 50,
    KEY_THREE = 51,
    KEY_FOUR = 52,
    KEY_FIVE = 53,
    KEY_A = 65,
    KEY_B = 66,
    KEY_C = 67,
    KEY_D = 68,
    KEY_E = 69,
    KEY_F = 70,
    KEY_G = 71,
    KEY_I = 73,
    KEY_L = 76,
    KEY_M = 77,
    KEY_N = 78,
    KEY_O = 79,
    KEY_P = 80,
    KEY_Q = 81,
    KEY_S = 83,
    KEY_U = 85,
    KEY_V = 86,
    KEY_W = 87,
    KEY_Y = 89,
    KEY_GRAVE = 96,
    KEY_ESCAPE = 256,
    KEY_ENTER = 257,
    KEY_BACKSPACE = 259,
    KEY_RIGHT = 262,
    KEY_LEFT = 263,
    KEY_DOWN = 264,
    KEY_UP = 265,
    KEY_PAGE_UP = 266,
    KEY_PAGE_DOWN = 267,
    KEY_HOME = 268,
    KEY_END = 269,
    KEY_CAPS_LOCK = 280,
    KEY_SCROLL_LOCK = 281,
    KEY_NUM_LOCK = 282,
    KEY_KP_0 = 320,
    KEY_KP_1 = 321,
    KEY_KP_2 = 322,
    KEY_KP_3 = 323,
    KEY_KP_4 = 324,
    KEY_KP_5 = 325,
    KEY_KP_6 = 326,
    KEY_KP_7 = 327,
    KEY_KP_8 = 328,
    KEY_KP_9 = 329,
    KEY_KP_ENTER = 335,
    KEY_LEFT_SHIFT = 340,
    KEY_LEFT_CONTROL = 341,
    KEY_LEFT_ALT = 342,
    KEY_LEFT_SUPER = 343,
    KEY_RIGHT_SHIFT = 344,
    KEY_RIGHT_CONTROL = 345,
    KEY_RIGHT_ALT = 346,
    KEY_RIGHT_SUPER = 347,
};

// Gamepad ids, likewise raylib's values. iOS ships no pad support yet; the
// input_host calls answer false, and these exist so input.c compiles.
enum {
    GAMEPAD_BUTTON_LEFT_FACE_UP = 1,
    GAMEPAD_BUTTON_LEFT_FACE_RIGHT = 2,
    GAMEPAD_BUTTON_LEFT_FACE_DOWN = 3,
    GAMEPAD_BUTTON_LEFT_FACE_LEFT = 4,
    GAMEPAD_BUTTON_RIGHT_FACE_UP = 5,
    GAMEPAD_BUTTON_RIGHT_FACE_RIGHT = 6,
    GAMEPAD_BUTTON_RIGHT_FACE_DOWN = 7,
    GAMEPAD_BUTTON_RIGHT_FACE_LEFT = 8,
    GAMEPAD_BUTTON_LEFT_TRIGGER_1 = 9,
    GAMEPAD_BUTTON_LEFT_TRIGGER_2 = 10,
    GAMEPAD_BUTTON_RIGHT_TRIGGER_1 = 11,
    GAMEPAD_BUTTON_RIGHT_TRIGGER_2 = 12,
    GAMEPAD_BUTTON_MIDDLE_LEFT = 13,
    GAMEPAD_BUTTON_MIDDLE_RIGHT = 15,
    GAMEPAD_AXIS_LEFT_X = 0,
    GAMEPAD_AXIS_LEFT_Y = 1,
};

// Texture filter / wrap modes. Values match raylib's enums so the call sites
// that name them need no change; only POINT and CLAMP are used by the pack's
// pixel art, but the rest are declared so a stray reference still compiles.
enum { TEXTURE_FILTER_POINT = 0, TEXTURE_FILTER_BILINEAR = 1 };
enum { TEXTURE_WRAP_REPEAT = 0, TEXTURE_WRAP_CLAMP = 1 };

#endif  // PLATFORM_IOS

#endif  // OB_TYPES_H
