#include "input_host.h"
#include "input.h"
#include "raylib.h"

#define GAMEPAD_ID 0
#define GAMEPAD_AXIS_DEADZONE 0.5f

// Direction from one-key presses — arrows, numpad, and the VGA
// keypad equivalents (Home/End/PgUp/PgDn).
static void poll_direction(InputState *in) {
    in->dx = 0; in->dy = 0;

    // Cardinal
    if      (input_key_pressed(KEY_UP)    || input_key_pressed(KEY_KP_8)) in->dy = -1;
    else if (input_key_pressed(KEY_DOWN)  || input_key_pressed(KEY_KP_2)) in->dy =  1;
    if      (input_key_pressed(KEY_LEFT)  || input_key_pressed(KEY_KP_4)) in->dx = -1;
    else if (input_key_pressed(KEY_RIGHT) || input_key_pressed(KEY_KP_6)) in->dx =  1;

    // Diagonals via numpad or Home/End/PgUp/PgDn.
    if      (input_key_pressed(KEY_KP_7) || input_key_pressed(KEY_HOME))      { in->dx = -1; in->dy = -1; }
    else if (input_key_pressed(KEY_KP_9) || input_key_pressed(KEY_PAGE_UP))   { in->dx =  1; in->dy = -1; }
    else if (input_key_pressed(KEY_KP_1) || input_key_pressed(KEY_END))       { in->dx = -1; in->dy =  1; }
    else if (input_key_pressed(KEY_KP_3) || input_key_pressed(KEY_PAGE_DOWN)) { in->dx =  1; in->dy =  1; }
}

// Adventure-mode gamepad polling. D-pad / left stick → 8-direction
// movement; A=search, X=cast spell, Y=end week, LB=army view,
// RB=character view, LT=fly, RT=land, Start=worldmap, Back=options.
// B (cancel) is handled via gamepad_pressed_cancel() so non-adventure
// callers (view dismiss, prompt cancel) can also see it.
//
// Gamepad input is additive to keyboard input; the pad doesn't disable
// keys. If the pad isn't connected, IsGamepadAvailable returns false
// and every check no-ops.
static void poll_gamepad(InputState *in) {
    // Autoplay/test mode: gamepads aren't scriptable, and on headless
    // boxes raylib's gamepad layer can produce phantom button/axis
    // readings that override the scripted keyboard input. Skip.
    if (input_host_is_scripted()) return;
    if (!IsGamepadAvailable(GAMEPAD_ID)) return;

    // Movement: d-pad first, fall back to left stick.
    int dx = 0, dy = 0;
    if (IsGamepadButtonPressed(GAMEPAD_ID, GAMEPAD_BUTTON_LEFT_FACE_LEFT))  dx = -1;
    if (IsGamepadButtonPressed(GAMEPAD_ID, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) dx =  1;
    if (IsGamepadButtonPressed(GAMEPAD_ID, GAMEPAD_BUTTON_LEFT_FACE_UP))    dy = -1;
    if (IsGamepadButtonPressed(GAMEPAD_ID, GAMEPAD_BUTTON_LEFT_FACE_DOWN))  dy =  1;

    if (dx == 0 && dy == 0) {
        // Stick: edge-trigger so one push = one step (matches keyboard).
        static bool stick_held = false;
        float ax = GetGamepadAxisMovement(GAMEPAD_ID, GAMEPAD_AXIS_LEFT_X);
        float ay = GetGamepadAxisMovement(GAMEPAD_ID, GAMEPAD_AXIS_LEFT_Y);
        bool engaged = (ax >  GAMEPAD_AXIS_DEADZONE || ax < -GAMEPAD_AXIS_DEADZONE ||
                        ay >  GAMEPAD_AXIS_DEADZONE || ay < -GAMEPAD_AXIS_DEADZONE);
        if (engaged && !stick_held) {
            if (ax >  GAMEPAD_AXIS_DEADZONE) dx =  1;
            if (ax < -GAMEPAD_AXIS_DEADZONE) dx = -1;
            if (ay >  GAMEPAD_AXIS_DEADZONE) dy =  1;
            if (ay < -GAMEPAD_AXIS_DEADZONE) dy = -1;
            stick_held = true;
        } else if (!engaged) {
            stick_held = false;
        }
    }
    if (dx != 0 || dy != 0) { in->dx = dx; in->dy = dy; }

    // Action buttons. Only fire if no keyboard action already set this
    // frame so keyboard takes precedence on keyboard+pad systems.
    if (in->action != INPUT_ACTION_NONE) return;

    if      (IsGamepadButtonPressed(GAMEPAD_ID, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))   in->action = INPUT_ACTION_SEARCH;
    else if (IsGamepadButtonPressed(GAMEPAD_ID, GAMEPAD_BUTTON_RIGHT_FACE_LEFT))   in->action = INPUT_ACTION_CAST_SPELL;
    else if (IsGamepadButtonPressed(GAMEPAD_ID, GAMEPAD_BUTTON_RIGHT_FACE_UP))     in->action = INPUT_ACTION_END_WEEK;
    else if (IsGamepadButtonPressed(GAMEPAD_ID, GAMEPAD_BUTTON_LEFT_TRIGGER_1))    in->action = INPUT_ACTION_VIEW_ARMY;
    else if (IsGamepadButtonPressed(GAMEPAD_ID, GAMEPAD_BUTTON_RIGHT_TRIGGER_1))   in->action = INPUT_ACTION_VIEW_CHARACTER;
    else if (IsGamepadButtonPressed(GAMEPAD_ID, GAMEPAD_BUTTON_LEFT_TRIGGER_2))    in->action = INPUT_ACTION_FLY;
    else if (IsGamepadButtonPressed(GAMEPAD_ID, GAMEPAD_BUTTON_RIGHT_TRIGGER_2))   in->action = INPUT_ACTION_LAND;
    else if (IsGamepadButtonPressed(GAMEPAD_ID, GAMEPAD_BUTTON_MIDDLE_RIGHT))      in->action = INPUT_ACTION_VIEW_MAP;
    else if (IsGamepadButtonPressed(GAMEPAD_ID, GAMEPAD_BUTTON_MIDDLE_LEFT))       in->action = INPUT_ACTION_OPTIONS_MENU;
}

bool gamepad_pressed_cancel(void) {
    if (input_host_is_scripted()) return false;
    if (!IsGamepadAvailable(GAMEPAD_ID)) return false;
    return IsGamepadButtonPressed(GAMEPAD_ID, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
}

InputState input_poll(void) {
    InputState in = { 0, 0, INPUT_ACTION_NONE };

    poll_direction(&in);

    // Letter keys — adventure-mode action bindings.
    bool ctrl = input_key_down(KEY_LEFT_CONTROL) || input_key_down(KEY_RIGHT_CONTROL);

    if      (input_key_pressed(KEY_A))                 in.action = INPUT_ACTION_VIEW_ARMY;
    else if (input_key_pressed(KEY_C))                 in.action = INPUT_ACTION_VIEW_CONTROLS;
    else if (input_key_pressed(KEY_F))                 in.action = INPUT_ACTION_FLY;
    else if (input_key_pressed(KEY_L))                 in.action = INPUT_ACTION_LAND;
    else if (input_key_pressed(KEY_I))                 in.action = INPUT_ACTION_VIEW_CONTRACT;
    else if (input_key_pressed(KEY_M))                 in.action = INPUT_ACTION_VIEW_MAP;
    else if (input_key_pressed(KEY_P))                 in.action = INPUT_ACTION_VIEW_PUZZLE;
    else if (input_key_pressed(KEY_S))                 in.action = INPUT_ACTION_SEARCH;
    else if (input_key_pressed(KEY_U))                 in.action = INPUT_ACTION_CAST_SPELL;
    else if (input_key_pressed(KEY_V))                 in.action = INPUT_ACTION_VIEW_CHARACTER;
    else if (input_key_pressed(KEY_W))                 in.action = INPUT_ACTION_END_WEEK;
    else if (input_key_pressed(KEY_D))                 in.action = INPUT_ACTION_DISMISS_ARMY;
    else if (input_key_pressed(KEY_O))                 in.action = INPUT_ACTION_OPTIONS_MENU;
    else if (input_key_pressed(KEY_N))                 in.action = INPUT_ACTION_NEW_CONTINENT;
    else if (input_key_pressed(KEY_KP_5))              in.action = INPUT_ACTION_REST;
    else if (ctrl && input_key_pressed(KEY_Q))         in.action = INPUT_ACTION_FAST_QUIT;
    else if (input_key_pressed(KEY_Q))                 in.action = INPUT_ACTION_SAVE_QUIT;

    poll_gamepad(&in);

    return in;
}
