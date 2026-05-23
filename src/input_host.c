#include "input_host.h"
#include "raylib.h"

#include <stdio.h>
#include <string.h>

// Tiny FIFO. Only used for: (a) the 3 startup-wizard keys queued
// before the main loop starts, and (b) the combat picker's
// direction+confirm sequence. Per-tick autoplay overworld inputs use
// the live-key path (ap_set_key) directly, bypassing this queue.
#define IH_PRESS_CAP   8
#define IH_CHAR_CAP    32
#define IH_DOWN_CAP    16

typedef struct {
    int key;
    int frames_left;
} DownEvent;

static bool s_scripted = false;

// Live key for the current tick. -1 = no key pressed. autoplay's
// per-tick handler sets this via ap_set_key (or clears via
// ap_clear_key); input_host_tick may overwrite it by popping from
// the FIFO when the FIFO is non-empty.
static int  s_live_key = -1;

static int  s_press[IH_PRESS_CAP];
static int  s_press_count = 0;

static int  s_chars[IH_CHAR_CAP];
static int  s_chars_head = 0;
static int  s_chars_tail = 0;

static DownEvent s_down[IH_DOWN_CAP];

void input_host_use_raylib(void) {
    s_scripted = false;
}

void input_host_use_queue(void) {
    s_scripted = true;
    s_live_key = -1;
    s_press_count = 0;
    s_chars_head = s_chars_tail = 0;
    memset(s_down, 0, sizeof(s_down));
}

bool input_host_is_scripted(void) {
    return s_scripted;
}

// ----- Live-key API ---------------------------------------------------------

void ap_set_key(int key) {
    if (s_scripted) s_live_key = key;
}

void ap_clear_key(void) {
    if (s_scripted) s_live_key = -1;
}

// ----- FIFO -----------------------------------------------------------------

void input_host_queue_key(int key) {
    if (s_press_count < IH_PRESS_CAP) {
        s_press[s_press_count++] = key;
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
    int active = (s_live_key >= 0) ? 1 : 0;
    return s_press_count + active;
}

void input_host_queue_char(int codepoint) {
    int next = (s_chars_tail + 1) % IH_CHAR_CAP;
    if (next != s_chars_head) {
        s_chars[s_chars_tail] = codepoint;
        s_chars_tail = next;
    }
}

// ----- Tick -----------------------------------------------------------------

void input_host_tick(void) {
    if (!s_scripted) return;
    // Each tick starts fresh: clear the live key. If the FIFO has
    // anything, pop one into the live key. Otherwise leave live key
    // cleared — autoplay's per-tick handler (or combat driver) will
    // set it via ap_set_key for THIS tick.
    s_live_key = -1;
    if (s_press_count > 0) {
        s_live_key = s_press[0];
        for (int i = 1; i < s_press_count; i++) s_press[i - 1] = s_press[i];
        s_press_count--;
    }
    for (int i = 0; i < IH_DOWN_CAP; i++) {
        if (s_down[i].frames_left > 0) s_down[i].frames_left--;
    }
}

// ----- Engine-facing input reads -------------------------------------------

bool input_key_pressed(int key) {
    if (!s_scripted) return IsKeyPressed(key);
    return s_live_key == key;
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
    int k = s_live_key;
    s_live_key = -1;  // drain on read so a second call doesn't re-fire
    return (k > 0) ? k : 0;
}

int input_get_char_pressed(void) {
    if (!s_scripted) return GetCharPressed();
    if (s_chars_head == s_chars_tail) return 0;
    int ch = s_chars[s_chars_head];
    s_chars_head = (s_chars_head + 1) % IH_CHAR_CAP;
    return ch;
}
