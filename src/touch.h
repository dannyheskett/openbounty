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
//   Chrome -- on-screen buttons (combat bar, ESC, digit pad, keyboard)
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

// A tile viewport: a tap in (x, y, w, h) picks the direction from the cell
// whose top-left corner is (cell_x, cell_y) -- the hero's, or the unit's --
// toward the tap (sign per axis) and injects the matching numpad direction
// key. A tap ON that cell injects center_key (0 = ignore). Holding repeats
// the injection, one discrete keypress per beat, so "one step per keypress"
// holds at the engine boundary.
void touch_region_map(int x, int y, int w, int h,
                      int cell_x, int cell_y, int tile_w, int tile_h,
                      int center_key);

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
    TOUCH_LIST_TOWN,         // town visit menu rows
    TOUCH_LIST_RECRUIT,      // recruit soldiers troop rows
    TOUCH_LIST_CASTLE,       // own-castle garrison slots
    TOUCH_LIST_COMBAT_SPELLS,// combat spell menu rows
    TOUCH_LIST_PROMPT,       // yes/no prompt rows
    TOUCH_LIST_CLASS,        // class picker columns
    TOUCH_LIST_TEXTSEL,      // letter selector cells
    TOUCH_LIST_COMBAT_ACTIONS, // modern combat action menu rows
    TOUCH_LIST_CLASS_CONFIRM,  // the class picker's Continue / Cancel rows
    TOUCH_LIST_RAIL,           // the left rail's five icons
    TOUCH_LIST_COMBAT_PANEL,   // the combat command panel's rows
    TOUCH_LIST_HUD,            // the HUD sidebar's five panels
};
void touch_region_row(int x, int y, int w, int h, int list_id, int row);
// A list taller than its space: a vertical drag over it moves it a row per
// `step` pixels (injecting Up/Down), and a tap on it resolves on release.
void touch_region_scroll(int x, int y, int w, int h, int step);
int  touch_tapped_row(int list_id);   // row tapped, -1 = none this frame
// Forget this frame's tap: a page that closes on it must not leave it for
// the page that opens next, which reads before it has drawn a frame.
void touch_forget_tap(void);

// Absolute-cell grid (the combat target picker): a tap reports the tile it
// landed on instead of a direction, so the screen can jump its cursor
// there and confirm in one go.
enum { TOUCH_GRID_COMBAT = 1 };
void touch_region_grid(int x, int y, int w, int h,
                       int tile_w, int tile_h, int grid_id);
bool touch_tapped_cell(int grid_id, int *cx, int *cy);

// On-screen chrome, requested per frame by the active screen.
enum {
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
// A touch control's size in window pixels -- Apple's 44pt / Android's 48dp,
// tracked as 11% of the window's short side with a 44px floor. Legacy's window
// buttons are sized from it.
int  touch_unit(void);

// The unit in DESIGN pixels -- what a widget measures against. Modern: a row's
// height (ml_row_h), fixed for the session; legacy: touch_unit in design pixels.
int  touch_unit_design(void);

// A region the player aims at deliberately (the top band, a corner). Inside
// its own rect it wins whatever registered first, so chrome that overlaps the
// world -- a band grown to a touch unit over the map's top row -- takes the
// tap instead of stepping the hero. No extra reach: only its own rect.
void touch_region_priority(int x, int y, int w, int h, int key);

void touch_draw_chrome(void);

// The page on top this frame (src/modern/page.c): the rect it covers, the key
// a tap inside it presses and the key a tap outside it presses (0: nothing).
// A MODAL page owns the screen: nothing registered before it -- under it --
// takes a tap, and the near-miss allowance reaches only its own regions and
// only inside it. A page that is not modal (the bridge's message) blocks
// what is under its own rect and lets the rest take taps. A tap on nothing
// is the page's. The last call in a frame wins -- the page on top.
void touch_page(int x, int y, int w, int h, int inside_key, int outside_key, bool modal);

// Per-frame tick, called from frame_host_end_frame after the yield.
void touch_frame(void);

// What the last frame offered a finger, after touch_frame has cleared the
// registry. For checks (the --gallery's tap check); play never reads these.
// A tap at design pixel (sx, sy): a row gives *list_id and *row, a button
// *key. False when nothing is there.
bool touch_last_hit(int sx, int sy, int *list_id, int *row, int *key);
// The rect of row `row` of `list_id`, or of the button for `key`.
bool touch_last_row_rect(int list_id, int row, int *x, int *y, int *w, int *h);
bool touch_last_key_rect(int key, int *x, int *y, int *w, int *h);
// The key the last frame's page would press for a tap at (sx, sy) that no
// region took (0: nothing), or -1 when no page was up.
int  touch_last_page_key(int sx, int sy);
// Everything a tap at (sx, sy) would do over the last frame's regions: the
// chrome, a square hit, the near-miss allowance, the page, the any-key -- the
// frame's own rule. False when it would do nothing.
bool touch_last_resolve(int sx, int sy, int *list_id, int *row, int *key);

#endif
