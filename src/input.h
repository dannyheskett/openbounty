#ifndef OB_INPUT_H
#define OB_INPUT_H

#include <stdbool.h>

// Overworld input bindings. One step per keypress; no auto-repeat;
// letter keys trigger specific game actions.
//
// Key -> action map:
//   A        - view_army
//   C        - view_controls (settings panel)
//   F        - fly (when riding)
//   L        - land (when flying)
//   I        - view_contract
//   M        - view_map (worldmap)
//   P        - view_puzzle
//   S        - search area
//   U        - use magic (cast spell menu)
//   V        - view_character
//   W        - end_week (wait / skip to weekend)
//   Q        - save_quit (prompt)
//   Ctrl+Q   - fast_quit
//   D        - dismiss_army
//   O        - options_menu  (the keybind reference screen)
//   N        - new_continent (sail across)
//
// Arrow keys + numpad 1-9 + Home/End/PgUp/PgDn -- one-step movement.

typedef enum {
    INPUT_ACTION_NONE = 0,
    // View screens
    INPUT_ACTION_VIEW_ARMY,
    INPUT_ACTION_VIEW_CHARACTER,
    INPUT_ACTION_VIEW_CONTRACT,
    INPUT_ACTION_VIEW_PUZZLE,
    INPUT_ACTION_VIEW_MAP,
    INPUT_ACTION_VIEW_CONTROLS,
    // Actions
    INPUT_ACTION_CAST_SPELL,
    INPUT_ACTION_SEARCH,
    INPUT_ACTION_END_WEEK,
    INPUT_ACTION_FLY,
    INPUT_ACTION_LAND,
    INPUT_ACTION_DISMISS_ARMY,
    INPUT_ACTION_NEW_CONTINENT,
    // Meta
    INPUT_ACTION_OPTIONS_MENU,
    INPUT_ACTION_SAVE_QUIT,
    INPUT_ACTION_FAST_QUIT,
    INPUT_ACTION_REST,          // numpad 5: rest one day in place
} InputAction;

typedef struct {
    int          dx, dy;         // -1, 0, or 1 per axis
    InputAction  action;
} InputState;

InputState input_poll(void);

// True for one frame when the player taps the gamepad's cancel button
// (B / Circle). Adventure-mode `input_poll` doesn't surface this -- it's
// for view-dismiss / prompt-cancel callers that already key off Esc.
// Returns false when the harness is active or no pad is connected.
bool gamepad_pressed_cancel(void);

#endif
