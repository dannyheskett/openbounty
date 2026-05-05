#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>

// Overworld input bindings. One step per keypress; no auto-repeat;
// letter keys trigger specific game actions.
//
// Key → action map:
//   A        — view_army
//   C        — view_controls (settings panel)
//   F        — fly (when riding)
//   L        — land (when flying)
//   I        — view_contract
//   M        — view_map (worldmap)
//   P        — view_puzzle
//   S        — search area
//   U        — use magic (cast spell menu)
//   V        — view_character
//   W        — end_week (wait / skip to weekend)
//   Q        — save_quit (prompt)
//   Ctrl+Q   — fast_quit
//   D        — dismiss_army
//   O        — options_menu  (the keybind reference screen)
//   N        — new_continent (sail across)
//
// Arrow keys + numpad 1-9 + Home/End/PgUp/PgDn — one-step movement.

typedef enum {
    CL_ACTION_NONE = 0,
    // View screens
    CL_ACTION_VIEW_ARMY,
    CL_ACTION_VIEW_CHARACTER,
    CL_ACTION_VIEW_CONTRACT,
    CL_ACTION_VIEW_PUZZLE,
    CL_ACTION_VIEW_MAP,
    CL_ACTION_VIEW_CONTROLS,
    // Actions
    CL_ACTION_CAST_SPELL,
    CL_ACTION_SEARCH,
    CL_ACTION_END_WEEK,
    CL_ACTION_FLY,
    CL_ACTION_LAND,
    CL_ACTION_DISMISS_ARMY,
    CL_ACTION_NEW_CONTINENT,
    // Meta
    CL_ACTION_OPTIONS_MENU,
    CL_ACTION_SAVE_QUIT,
    CL_ACTION_FAST_QUIT,
    CL_ACTION_REST,          // numpad 5: rest one day in place
} ClassicAction;

typedef struct {
    int            dx, dy;         // -1, 0, or 1 per axis
    ClassicAction  action;
} ClassicInput;

ClassicInput input_poll(void);

// True for one frame when the player taps the gamepad's cancel button
// (B / Circle). Adventure-mode `input_poll` doesn't surface this — it's
// for view-dismiss / prompt-cancel callers that already key off Esc.
// Returns false when the harness is active or no pad is connected.
bool gamepad_pressed_cancel(void);

#endif
