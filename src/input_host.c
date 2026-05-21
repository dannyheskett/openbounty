#include "input_host.h"
#include "raylib.h"

#include <string.h>

// Queue capacity. Press-edge events live for exactly one frame after
// they're queued; the scenario typically queues a handful per frame.
#define IH_PRESS_CAP   32
#define IH_CHAR_CAP    32
#define IH_DOWN_CAP    16

// Press events live in a FIFO queue. Each event represents one
// key-press that should look (to the game) like the player pressed
// that key for exactly one frame. At most one event is "active" at
// a time — within the active event's frame, IsKeyPressed(key)
// returns true for every call (mirroring real raylib edge-trigger),
// and GetKeyPressed drains the active event. input_host_tick
// retires the active event and promotes the next queued one.
typedef struct {
    int  key;
} PressEvent;

typedef struct {
    int key;
    int frames_left;
} DownEvent;

static bool       s_scripted = false;
static PressEvent s_press[IH_PRESS_CAP];
static int        s_press_count = 0;
// The current frame's active key. -1 when there is no active key.
// IsKeyPressed / GetKeyPressed only ever match the active key (or
// drain it, in GetKeyPressed's case).
static int        s_active_key = -1;
static bool       s_active_drained = false;  // GetKeyPressed sets this
static int        s_chars[IH_CHAR_CAP];
static int        s_chars_head = 0;
static int        s_chars_tail = 0;
static DownEvent  s_down[IH_DOWN_CAP];

// Promotion rate-limit (see header). Defaults to 1 = promote every
// tick. Visible mode bumps this to roughly match a real player's
// typing speed.
static int        s_promote_period   = 1;
static int        s_promote_countdown = 0;

void input_host_use_raylib(void) {
    s_scripted = false;
}

void input_host_use_queue(void) {
    s_scripted = true;
    s_press_count = 0;
    s_active_key = -1;
    s_active_drained = false;
    s_chars_head = s_chars_tail = 0;
    s_promote_period   = 1;
    s_promote_countdown = 0;
    memset(s_down, 0, sizeof(s_down));
}

void input_host_set_promotion_period(int ticks_per_promotion) {
    if (ticks_per_promotion < 1) ticks_per_promotion = 1;
    s_promote_period = ticks_per_promotion;
    s_promote_countdown = 0;
}

bool input_host_is_scripted(void) {
    return s_scripted;
}

void input_host_queue_key(int key) {
    if (s_press_count < IH_PRESS_CAP) {
        s_press[s_press_count].key = key;
        s_press_count++;
    }
}

void input_host_queue_key_down(int key, int frames) {
    for (int i = 0; i < IH_DOWN_CAP; i++) {
        if (s_down[i].frames_left <= 0) {
            s_down[i].key = key;
            s_down[i].frames_left = frames;
            return;
        }
    }
}

int input_host_queue_depth(void) {
    return s_press_count;
}

void input_host_queue_char(int codepoint) {
    int next = (s_chars_tail + 1) % IH_CHAR_CAP;
    if (next != s_chars_head) {
        s_chars[s_chars_tail] = codepoint;
        s_chars_tail = next;
    }
}

void input_host_tick(void) {
    if (!s_scripted) return;

    // Promotion rate-limit: a key, once promoted, is "active" for
    // exactly ONE frame (matching real raylib's edge-triggered
    // IsKeyPressed). Between promotions, the active key is cleared
    // — pollers see "no key pressed" — and we wait
    // s_promote_period ticks before promoting the next queued event.
    // This lets visible-mode pace movement to roughly one step per
    // promote_period frames instead of one step per frame.
    s_active_key = -1;
    s_active_drained = false;
    if (s_promote_countdown > 0) {
        s_promote_countdown--;
        for (int i = 0; i < IH_DOWN_CAP; i++) {
            if (s_down[i].frames_left > 0) s_down[i].frames_left--;
        }
        return;
    }
    s_promote_countdown = s_promote_period - 1;

    if (s_press_count > 0) {
        s_active_key = s_press[0].key;
        for (int i = 1; i < s_press_count; i++) {
            s_press[i - 1] = s_press[i];
        }
        s_press_count--;
    }

    // Held-down keys count down by one frame each tick.
    for (int i = 0; i < IH_DOWN_CAP; i++) {
        if (s_down[i].frames_left > 0) s_down[i].frames_left--;
    }
}

bool input_key_pressed(int key) {
    if (!s_scripted) return IsKeyPressed(key);
    return s_active_key == key;
}

bool input_key_down(int key) {
    if (!s_scripted) return IsKeyDown(key);
    for (int i = 0; i < IH_DOWN_CAP; i++) {
        if (s_down[i].frames_left > 0 && s_down[i].key == key) return true;
    }
    return false;
}

int input_get_key_pressed(void) {
    if (!s_scripted) return GetKeyPressed();
    if (s_active_key < 0 || s_active_drained) return 0;
    s_active_drained = true;
    return s_active_key;
}

int input_get_char_pressed(void) {
    if (!s_scripted) return GetCharPressed();
    if (s_chars_head == s_chars_tail) return 0;
    int ch = s_chars[s_chars_head];
    s_chars_head = (s_chars_head + 1) % IH_CHAR_CAP;
    return ch;
}
