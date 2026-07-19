// engine/raylib_stub.h
//
// Headless replacement for raylib.h. Provides just enough types and
// no-op functions to let engine-subset source files compile and link
// without libraylib.
//
// Used by every headless consumer of the engine -- the engine archive
// itself, the autoplay and demo objects, the test binary, and the
// library-boundary check -- which drive engine logic by function call:
// no window, no audio, no rendering. The shell links real raylib and
// renders normally.
//
// Selection happens at the build level: source files include
// "raylib.h", and the build either resolves that to this stub
// (engine/raylib_stub.h via -Iengine and a small bridge header below)
// or to real raylib.
//
// IMPORTANT: this header MUST NOT be visible when building the game
// binary. The Makefile arranges -I order so that real raylib wins for
// shell builds, and this stub wins for headless builds.

#ifndef OB_RAYLIB_STUB_H
#define OB_RAYLIB_STUB_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// ---- Types ------------------------------------------------------------------

typedef struct Color    { unsigned char r, g, b, a; } Color;
typedef struct Vector2  { float x, y; }                Vector2;
typedef struct Vector3  { float x, y, z; }             Vector3;
typedef struct Rectangle { float x, y, width, height; } Rectangle;

typedef struct Image {
    void *data;
    int width, height, mipmaps, format;
} Image;

typedef struct Texture2D {
    unsigned int id;
    int width, height, mipmaps, format;
} Texture2D;
typedef Texture2D Texture;

typedef struct RenderTexture2D {
    unsigned int id;
    Texture2D texture;
    Texture2D depth;
} RenderTexture2D;
typedef RenderTexture2D RenderTexture;

typedef struct AudioStream {
    void *buffer;
    void *processor;
    unsigned int sampleRate;
    unsigned int sampleSize;
    unsigned int channels;
} AudioStream;

typedef struct Sound { AudioStream stream; unsigned int frameCount; } Sound;
typedef struct Music { AudioStream stream; unsigned int frameCount; bool looping; int ctxType; void *ctxData; } Music;
typedef struct Wave  { unsigned int frameCount; unsigned int sampleRate; unsigned int sampleSize; unsigned int channels; void *data; } Wave;

typedef struct Font   { int baseSize; int glyphCount; int glyphPadding; Texture2D texture; void *recs; void *glyphs; } Font;

// raylib's TraceLogLevel
#define LOG_ALL       0
#define LOG_TRACE     1
#define LOG_DEBUG     2
#define LOG_INFO      3
#define LOG_WARNING   4
#define LOG_ERROR     5
#define LOG_FATAL     6
#define LOG_NONE      7

// Common color constants (declared as static so each TU has its own
// copy -- no link conflicts).
static const Color LIGHTGRAY = { 200, 200, 200, 255 };
static const Color GRAY      = { 130, 130, 130, 255 };
static const Color DARKGRAY  = {  80,  80,  80, 255 };
static const Color YELLOW    = { 253, 249,   0, 255 };
static const Color GOLD      = { 255, 203,   0, 255 };
static const Color ORANGE    = { 255, 161,   0, 255 };
static const Color PINK      = { 255, 109, 194, 255 };
static const Color RED       = { 230,  41,  55, 255 };
static const Color MAROON    = { 190,  33,  55, 255 };
static const Color GREEN     = {   0, 228,  48, 255 };
static const Color LIME      = {   0, 158,  47, 255 };
static const Color DARKGREEN = {   0, 117,  44, 255 };
static const Color SKYBLUE   = { 102, 191, 255, 255 };
static const Color BLUE      = {   0, 121, 241, 255 };
static const Color DARKBLUE  = {   0,  82, 172, 255 };
static const Color PURPLE    = { 200, 122, 255, 255 };
static const Color VIOLET    = { 135,  60, 190, 255 };
static const Color DARKPURPLE= { 112,  31, 126, 255 };
static const Color BEIGE     = { 211, 176, 131, 255 };
static const Color BROWN     = { 127, 106,  79, 255 };
static const Color DARKBROWN = {  76,  63,  47, 255 };
static const Color WHITE     = { 255, 255, 255, 255 };
static const Color BLACK     = {   0,   0,   0, 255 };
static const Color BLANK     = {   0,   0,   0,   0 };
static const Color MAGENTA   = { 255,   0, 255, 255 };
static const Color RAYWHITE  = { 245, 245, 245, 255 };

// Flags
#define FLAG_WINDOW_RESIZABLE 0x00000004
#define FLAG_MSAA_4X_HINT     0x00000020
#define FLAG_WINDOW_HIDDEN    0x00000080

#define TEXTURE_FILTER_POINT      0
#define TEXTURE_FILTER_BILINEAR   1
#define TEXTURE_WRAP_REPEAT       0
#define TEXTURE_WRAP_CLAMP        1
#define TEXTURE_WRAP_MIRROR_REPEAT 2

// PixelFormat enum (subset).
#define PIXELFORMAT_UNCOMPRESSED_GRAYSCALE   1
#define PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA  2
#define PIXELFORMAT_UNCOMPRESSED_R5G6B5      3
#define PIXELFORMAT_UNCOMPRESSED_R8G8B8      4
#define PIXELFORMAT_UNCOMPRESSED_R5G5B5A1    5
#define PIXELFORMAT_UNCOMPRESSED_R4G4B4A4    6
#define PIXELFORMAT_UNCOMPRESSED_R8G8B8A8    7
#define PIXELFORMAT_UNCOMPRESSED_R32         8
#define PIXELFORMAT_UNCOMPRESSED_R32G32B32   9
#define PIXELFORMAT_UNCOMPRESSED_R32G32B32A32 10

static inline void SetTextureWrap(Texture2D t, int wrap)       { (void)t; (void)wrap; }

// ---- Function stubs ---------------------------------------------------------
//
// All no-ops or sensible defaults. None of these should ever be called
// in a headless run; if they are, behavior is benign.

static inline void  InitWindow(int w, int h, const char *t)  { (void)w; (void)h; (void)t; }
static inline void  CloseWindow(void)                        {}
static inline bool  WindowShouldClose(void)                  { return false; }
static inline int   GetScreenWidth(void)                     { return 640; }
static inline int   GetScreenHeight(void)                    { return 400; }
static inline void  SetWindowMinSize(int w, int h)           { (void)w; (void)h; }
static inline void  SetWindowSize(int w, int h)              { (void)w; (void)h; }
static inline void  SetTargetFPS(int n)                      { (void)n; }
static inline void  SetExitKey(int k)                        { (void)k; }
static inline void  SetConfigFlags(unsigned int f)           { (void)f; }
static inline void  ToggleFullscreen(void)                   {}
static inline void  SetTraceLogLevel(int l)                  { (void)l; }

// Time: monotonic counter so engine code that calls GetTime() (e.g.
// combat animation timer) gets a consistent value that advances on
// demand. Playtest driver can manually advance it if needed.
static double _ob_stub_time;
static inline double GetTime(void) { return _ob_stub_time; }
static inline void   _ob_stub_advance_time(double secs) { _ob_stub_time += secs; }

static inline void  BeginDrawing(void)                       {}
static inline void  EndDrawing(void)                         {}
static inline void  ClearBackground(Color c)                 { (void)c; }
static inline void  BeginTextureMode(RenderTexture2D t)      { (void)t; }
static inline void  EndTextureMode(void)                     {}

static inline RenderTexture2D LoadRenderTexture(int w, int h) {
    RenderTexture2D rt = {0};
    rt.texture.width = w; rt.texture.height = h;
    return rt;
}
static inline void UnloadRenderTexture(RenderTexture2D rt) { (void)rt; }

static inline Texture2D LoadTexture(const char *path)        { Texture2D t = {0}; (void)path; return t; }
static inline Texture2D LoadTextureFromImage(Image img)      { Texture2D t = {0}; (void)img; return t; }
static inline void      UnloadTexture(Texture2D t)           { (void)t; }
static inline void      SetTextureFilter(Texture2D t, int f) { (void)t; (void)f; }
static inline void      GenTextureMipmaps(Texture2D *t)      { (void)t; }

static inline Image LoadImage(const char *p)                 { Image i = {0}; (void)p; return i; }
static inline Image LoadImageFromMemory(const char *t, const unsigned char *d, int s) { Image i = {0}; (void)t; (void)d; (void)s; return i; }
static inline Image LoadImageFromTexture(Texture2D t)        { Image i = {0}; (void)t; return i; }
static inline void  UnloadImage(Image i)                     { (void)i; }
static inline void  ImageFormat(Image *img, int f)           { (void)img; (void)f; }
static inline void  ImageResize(Image *img, int w, int h)    { (void)img; (void)w; (void)h; }
static inline void  ImageColorReplace(Image *img, Color a, Color b) { (void)img; (void)a; (void)b; }
static inline void  ImageFlipVertical(Image *img)            { (void)img; }
static inline Image GenImageColor(int w, int h, Color c)     { Image i = {0}; i.width = w; i.height = h; (void)c; return i; }
static inline bool  ExportImage(Image img, const char *path) { (void)img; (void)path; return true; }
static inline unsigned char *ExportImageToMemory(Image img, const char *fmt, int *out_len) {
    (void)img; (void)fmt; if (out_len) *out_len = 0; return NULL;
}
static inline void MemFree(void *p)                          { (void)p; }
static inline Wave  LoadWave(const char *p)                  { Wave w = {0}; (void)p; return w; }
static inline Wave  LoadWaveFromMemory(const char *t, const unsigned char *d, int s) { Wave w = {0}; (void)t; (void)d; (void)s; return w; }
static inline void  UnloadWave(Wave w)                       { (void)w; }
static inline Sound LoadSoundFromWave(Wave w)                { Sound s = {0}; (void)w; return s; }
static inline Music LoadMusicStreamFromMemory(const char *t, const unsigned char *d, int s) { Music m = {0}; (void)t; (void)d; (void)s; return m; }
static inline void  SetMusicPan(Music m, float p)            { (void)m; (void)p; }

static inline void DrawTexture(Texture2D t, int x, int y, Color c)           { (void)t; (void)x; (void)y; (void)c; }
static inline void DrawTextureV(Texture2D t, Vector2 p, Color c)             { (void)t; (void)p; (void)c; }
static inline void DrawTextureEx(Texture2D t, Vector2 p, float r, float s, Color c) { (void)t; (void)p; (void)r; (void)s; (void)c; }
static inline void DrawTextureRec(Texture2D t, Rectangle s, Vector2 p, Color c) { (void)t; (void)s; (void)p; (void)c; }
static inline void DrawTexturePro(Texture2D t, Rectangle s, Rectangle d, Vector2 o, float r, Color c) { (void)t; (void)s; (void)d; (void)o; (void)r; (void)c; }
static inline void DrawText(const char *t, int x, int y, int sz, Color c)    { (void)t; (void)x; (void)y; (void)sz; (void)c; }
static inline void DrawRectangle(int x, int y, int w, int h, Color c)        { (void)x; (void)y; (void)w; (void)h; (void)c; }
static inline void DrawRectangleLines(int x, int y, int w, int h, Color c)   { (void)x; (void)y; (void)w; (void)h; (void)c; }
static inline void DrawRectangleLinesEx(Rectangle r, float t, Color c)       { (void)r; (void)t; (void)c; }
static inline void DrawRectangleRounded(Rectangle r, float rd, int seg, Color c)         { (void)r; (void)rd; (void)seg; (void)c; }
static inline void DrawRectangleRoundedLines(Rectangle r, float rd, int seg, Color c) { (void)r; (void)rd; (void)seg; (void)c; }
static inline void BeginScissorMode(int x, int y, int w, int h)              { (void)x; (void)y; (void)w; (void)h; }
static inline void EndScissorMode(void)                                      {}
static inline bool FileExists(const char *p)                                 { (void)p; return false; }
static inline bool IsSoundPlaying(Sound s)                                   { (void)s; return false; }
static inline void DrawLine(int x1, int y1, int x2, int y2, Color c)         { (void)x1; (void)y1; (void)x2; (void)y2; (void)c; }
static inline void DrawCircle(int x, int y, float r, Color c)                { (void)x; (void)y; (void)r; (void)c; }
static inline void DrawPixel(int x, int y, Color c)                          { (void)x; (void)y; (void)c; }
static inline int  MeasureText(const char *t, int sz)                        { (void)t; (void)sz; return 0; }

static inline void InitAudioDevice(void)                     {}
static inline void CloseAudioDevice(void)                    {}
static inline bool IsAudioDeviceReady(void)                  { return false; }
static inline Sound LoadSound(const char *p)                 { Sound s = {0}; (void)p; return s; }
static inline void UnloadSound(Sound s)                      { (void)s; }
static inline void PlaySound(Sound s)                        { (void)s; }
static inline void StopSound(Sound s)                        { (void)s; }
static inline void SetSoundVolume(Sound s, float v)          { (void)s; (void)v; }
static inline Music LoadMusicStream(const char *p)           { Music m = {0}; (void)p; return m; }
static inline void UnloadMusicStream(Music m)                { (void)m; }
static inline void PlayMusicStream(Music m)                  { (void)m; }
static inline void StopMusicStream(Music m)                  { (void)m; }
static inline void UpdateMusicStream(Music m)                { (void)m; }
static inline void SetMusicVolume(Music m, float v)          { (void)m; (void)v; }
static inline bool IsMusicReady(Music m)                     { (void)m; return false; }
static inline bool IsMusicStreamPlaying(Music m)             { (void)m; return false; }

// Input -- never reached in a headless run (the headless driver
// short-circuits the harness shim entirely), but the symbols need to
// resolve for files that compile-link them.
static inline bool IsKeyPressed(int k)                       { (void)k; return false; }
static inline bool IsKeyDown(int k)                          { (void)k; return false; }
static inline int  GetKeyPressed(void)                       { return 0; }
static inline int  GetCharPressed(void)                      { return 0; }
static inline Vector2 GetMousePosition(void)                 { Vector2 v = {0,0}; return v; }
static inline bool IsMouseButtonPressed(int b)               { (void)b; return false; }

// Gamepad
static inline bool  IsGamepadAvailable(int gp)                       { (void)gp; return false; }
static inline bool  IsGamepadButtonPressed(int gp, int b)            { (void)gp; (void)b; return false; }
static inline bool  IsGamepadButtonDown(int gp, int b)               { (void)gp; (void)b; return false; }
static inline float GetGamepadAxisMovement(int gp, int axis)         { (void)gp; (void)axis; return 0.0f; }

// Misc time / event helpers
static inline float GetFrameTime(void)                       { return 0.0f; }
static inline void  PollInputEvents(void)                    {}
static inline void  WaitTime(double s)                       { (void)s; }

// Gamepad enums
#define GAMEPAD_BUTTON_LEFT_FACE_DOWN     5
#define GAMEPAD_BUTTON_LEFT_FACE_UP       1
#define GAMEPAD_BUTTON_LEFT_FACE_LEFT     4
#define GAMEPAD_BUTTON_LEFT_FACE_RIGHT    2
#define GAMEPAD_BUTTON_RIGHT_FACE_DOWN    7
#define GAMEPAD_BUTTON_RIGHT_FACE_UP      3
#define GAMEPAD_BUTTON_RIGHT_FACE_LEFT    6
#define GAMEPAD_BUTTON_RIGHT_FACE_RIGHT   8
#define GAMEPAD_BUTTON_LEFT_TRIGGER_1     9
#define GAMEPAD_BUTTON_LEFT_TRIGGER_2     10
#define GAMEPAD_BUTTON_RIGHT_TRIGGER_1    11
#define GAMEPAD_BUTTON_RIGHT_TRIGGER_2    12
#define GAMEPAD_BUTTON_MIDDLE_LEFT        13
#define GAMEPAD_BUTTON_MIDDLE             14
#define GAMEPAD_BUTTON_MIDDLE_RIGHT       15
#define GAMEPAD_BUTTON_LEFT_THUMB         16
#define GAMEPAD_BUTTON_RIGHT_THUMB        17
#define GAMEPAD_AXIS_LEFT_X               0
#define GAMEPAD_AXIS_LEFT_Y               1
#define GAMEPAD_AXIS_RIGHT_X              2
#define GAMEPAD_AXIS_RIGHT_Y              3
#define GAMEPAD_AXIS_LEFT_TRIGGER         4
#define GAMEPAD_AXIS_RIGHT_TRIGGER        5

// Re-export OB_KEY_* as KEY_* so existing code that uses raylib's names
// compiles. This is the headless equivalent of "raylib defines these".
#include "input_keys.h"

#define KEY_NULL           OB_KEY_NULL
#define KEY_SPACE          OB_KEY_SPACE
#define KEY_ZERO           OB_KEY_ZERO
#define KEY_ONE            OB_KEY_ONE
#define KEY_TWO            OB_KEY_TWO
#define KEY_THREE          OB_KEY_THREE
#define KEY_FOUR           OB_KEY_FOUR
#define KEY_FIVE           OB_KEY_FIVE
#define KEY_SIX            OB_KEY_SIX
#define KEY_SEVEN          OB_KEY_SEVEN
#define KEY_EIGHT          OB_KEY_EIGHT
#define KEY_NINE           OB_KEY_NINE
#define KEY_A              OB_KEY_A
#define KEY_B              OB_KEY_B
#define KEY_C              OB_KEY_C
#define KEY_D              OB_KEY_D
#define KEY_E              OB_KEY_E
#define KEY_F              OB_KEY_F
#define KEY_G              OB_KEY_G
#define KEY_H              OB_KEY_H
#define KEY_I              OB_KEY_I
#define KEY_J              OB_KEY_J
#define KEY_K              OB_KEY_K
#define KEY_L              OB_KEY_L
#define KEY_M              OB_KEY_M
#define KEY_N              OB_KEY_N
#define KEY_O              OB_KEY_O
#define KEY_P              OB_KEY_P
#define KEY_Q              OB_KEY_Q
#define KEY_R              OB_KEY_R
#define KEY_S              OB_KEY_S
#define KEY_T              OB_KEY_T
#define KEY_U              OB_KEY_U
#define KEY_V              OB_KEY_V
#define KEY_W              OB_KEY_W
#define KEY_X              OB_KEY_X
#define KEY_Y              OB_KEY_Y
#define KEY_Z              OB_KEY_Z
#define KEY_GRAVE          OB_KEY_GRAVE
#define KEY_ESCAPE         OB_KEY_ESCAPE
#define KEY_ENTER          OB_KEY_ENTER
#define KEY_TAB            OB_KEY_TAB
#define KEY_BACKSPACE      OB_KEY_BACKSPACE
#define KEY_RIGHT          OB_KEY_RIGHT
#define KEY_LEFT           OB_KEY_LEFT
#define KEY_DOWN           OB_KEY_DOWN
#define KEY_UP             OB_KEY_UP
#define KEY_PAGE_UP        OB_KEY_PAGE_UP
#define KEY_PAGE_DOWN      OB_KEY_PAGE_DOWN
#define KEY_HOME           OB_KEY_HOME
#define KEY_END            OB_KEY_END
#define KEY_CAPS_LOCK      OB_KEY_CAPS_LOCK
#define KEY_SCROLL_LOCK    OB_KEY_SCROLL_LOCK
#define KEY_NUM_LOCK       OB_KEY_NUM_LOCK
#define KEY_F1             OB_KEY_F1
#define KEY_F2             OB_KEY_F2
#define KEY_F3             OB_KEY_F3
#define KEY_F4             OB_KEY_F4
#define KEY_F5             OB_KEY_F5
#define KEY_F6             OB_KEY_F6
#define KEY_F7             OB_KEY_F7
#define KEY_F8             OB_KEY_F8
#define KEY_F9             OB_KEY_F9
#define KEY_F10            OB_KEY_F10
#define KEY_F11            OB_KEY_F11
#define KEY_F12            OB_KEY_F12
#define KEY_LEFT_SHIFT     OB_KEY_LEFT_SHIFT
#define KEY_LEFT_CONTROL   OB_KEY_LEFT_CONTROL
#define KEY_LEFT_ALT       OB_KEY_LEFT_ALT
#define KEY_LEFT_SUPER     OB_KEY_LEFT_SUPER
#define KEY_RIGHT_SHIFT    OB_KEY_RIGHT_SHIFT
#define KEY_RIGHT_CONTROL  OB_KEY_RIGHT_CONTROL
#define KEY_RIGHT_ALT      OB_KEY_RIGHT_ALT
#define KEY_RIGHT_SUPER    OB_KEY_RIGHT_SUPER
#define KEY_KP_0           OB_KEY_KP_0
#define KEY_KP_1           OB_KEY_KP_1
#define KEY_KP_2           OB_KEY_KP_2
#define KEY_KP_3           OB_KEY_KP_3
#define KEY_KP_4           OB_KEY_KP_4
#define KEY_KP_5           OB_KEY_KP_5
#define KEY_KP_6           OB_KEY_KP_6
#define KEY_KP_7           OB_KEY_KP_7
#define KEY_KP_8           OB_KEY_KP_8
#define KEY_KP_9           OB_KEY_KP_9
#define KEY_KP_ENTER       OB_KEY_KP_ENTER

#endif // OB_RAYLIB_STUB_H
