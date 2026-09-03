#ifndef OB_TOUCH_H
#define OB_TOUCH_H

#include <stdbool.h>

// Touch layer: turns taps into the same key events the keyboard produces,
// injected through input_host so every screen keeps its existing key
// handling (see input_host.h). Two mechanisms:
//
//   Regions -- a screen registers tappable rects while it handles input /
//   draws; the registry is cleared every frame, so a region lives exactly
//   as long as the screen that wants it. A tap inside injects the key.
//
//   Chrome -- on-screen buttons (action bar, ESC, digit pad, keyboard)
//   drawn AFTER the game's blit, in window pixels, over the letterbox
//   margins. Screens request chrome per frame; it renders only once a real
//   touch has been seen (input_touch_active), so keyboard/mouse desktop
//   sessions look exactly as before.
//
// Frame shape: screens register regions + requests during the frame;
// present_scaled draws the chrome; frame_host_end_frame calls touch_frame,
// which clears last frame's injected keys, hit-tests the tap against this
// frame's regions, injects, and resets the registry for the next frame.

// Tappable rect in design-space pixels (the render target's space).
void touch_region(int x, int y, int w, int h, int key);

// Tap anywhere in the game area injects `key`. For any-key dialogs and
// dismiss-on-any-key views. Explicit regions win over this.
void touch_region_any(int key);

// A tile viewport: a tap picks the direction from the tapped tile toward /
// away from the centre tile (sign per axis) and injects the matching
// numpad direction key. A tap ON the centre tile injects center_key
// (0 = ignore). Holding repeats the injection, one discrete keypress per
// beat, so "one step per keypress" holds at the engine boundary.
void touch_region_map(int x, int y, int w, int h,
                      int tile_w, int tile_h,
                      int center_tx, int center_ty, int center_key);

// Cursor lists: the renderer tags each row's rect with a list id + row
// index; the screen's update code asks which row was tapped and applies
// its own cursor/confirm logic. One tap = select + confirm, decided by
// the screen. Row ids live here so renderer and updater agree.
enum {
    TOUCH_LIST_MENU = 1,     // game menu (views.c menu stack)
    TOUCH_LIST_GATE,         // gate destination picker
    TOUCH_LIST_SPELLS,       // spell panel, row = column * 7 + slot
    TOUCH_LIST_CONTROLS,     // controls/settings panel
    TOUCH_LIST_STARTUP,      // startup cursored menus (one at a time)
    TOUCH_LIST_PACKS,        // pack picker
};
void touch_region_row(int x, int y, int w, int h, int list_id, int row);
int  touch_tapped_row(int list_id);   // row tapped, -1 = none this frame

// Absolute-cell grid (the combat target picker): a tap reports the tile it
// landed on instead of a direction, so the screen can jump its cursor
// there and confirm in one go.
enum { TOUCH_GRID_COMBAT = 1 };
void touch_region_grid(int x, int y, int w, int h,
                       int tile_w, int tile_h, int grid_id);
bool touch_tapped_cell(int grid_id, int *cx, int *cy);

// On-screen chrome, requested per frame by the active screen.
enum {
    TOUCH_CHROME_ADVENTURE = 1 << 0,   // adventure action bar
    TOUCH_CHROME_COMBAT    = 1 << 1,   // combat action bar
    TOUCH_CHROME_BACK      = 1 << 2,   // ESC button, top-right
    TOUCH_CHROME_CONFIRM   = 1 << 3,   // Enter button, next to ESC
    TOUCH_CHROME_DIGITS    = 1 << 4,   // 0-9 / backspace / enter pad
    TOUCH_CHROME_KEYBOARD  = 1 << 5,   // A-Z keyboard for name entry
};
void touch_request(unsigned chrome);

// Prompt answer bars: labelled buttons for the keys the active prompt
// reads. Drawn with the same bar layout as the action bars.
void touch_request_prompt_yesno(void);        // Yes -> Y, No -> N
void touch_request_prompt_numeric(int max);   // "1".."5" -> KEY_ONE+i
void touch_request_prompt_ab(void);           // "A"/"B" -> KEY_A/KEY_B

// Called from present_scaled, inside the frame's draw. Renders requested
// chrome in window pixels and registers its window-space regions.
void touch_draw_chrome(void);

// Per-frame tick, called from frame_host_end_frame after the yield.
void touch_frame(void);

#endif
