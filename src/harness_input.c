// OpenBounty input shim — see harness_input.h.

#define OB_HARNESS_INPUT_INTERNAL_H
#include "harness_input.h"
#include "raylib.h"
#include <string.h>

// ---- queue state -----------------------------------------------------------
//
// The queue holds events synthesized by the harness:
//
//   pressed_keys[]  — keys that should report IsKeyPressed=true once
//                     this frame, then clear at frame_end.
//   down_keys[]     — keys that should report IsKeyDown=true until an
//                     explicit release. Persistent across frames.
//   chars[]         — codepoints to drain one-at-a-time via
//                     GetCharPressed. Cleared at frame_end.
//
// Sized for the worst case the harness will ever realistically see in
// one frame (a few keys + one char). 16 each is plenty.

#define MAX_QUEUE 16

static bool s_active = false;
static int  s_pressed_keys[MAX_QUEUE];
static int  s_pressed_count = 0;
static int  s_down_keys[MAX_QUEUE];
static int  s_down_count = 0;
static int  s_chars[MAX_QUEUE];
static int  s_chars_head = 0;
static int  s_chars_tail = 0;
static int  s_get_key_idx = 0;     // cursor into pressed_keys for GetKeyPressed

void harness_set_active(bool active);
void harness_set_active(bool active) { s_active = active; }

bool harness_active(void) { return s_active; }

void harness_queue_key_pressed(int key) {
    if (s_pressed_count < MAX_QUEUE) {
        s_pressed_keys[s_pressed_count++] = key;
    }
}

void harness_queue_key_down(int key) {
    for (int i = 0; i < s_down_count; i++) {
        if (s_down_keys[i] == key) return;  // already held
    }
    if (s_down_count < MAX_QUEUE) {
        s_down_keys[s_down_count++] = key;
    }
}

void harness_queue_key_release(int key) {
    for (int i = 0; i < s_down_count; i++) {
        if (s_down_keys[i] == key) {
            // Compact the array.
            for (int j = i + 1; j < s_down_count; j++) {
                s_down_keys[j - 1] = s_down_keys[j];
            }
            s_down_count--;
            return;
        }
    }
}

void harness_queue_char(int codepoint) {
    int next = (s_chars_tail + 1) % MAX_QUEUE;
    if (next != s_chars_head) {
        s_chars[s_chars_tail] = codepoint;
        s_chars_tail = next;
    }
}

void harness_frame_end(void) {
    s_pressed_count = 0;
    s_get_key_idx = 0;
    // chars: drain whatever the game didn't pull this frame so they
    // don't leak into the next. Same semantics as raylib's per-frame
    // char queue.
    s_chars_head = s_chars_tail;
    // down_keys persist across frames — they're released explicitly.
}

// ---- shim wrappers ---------------------------------------------------------

bool harness_key_pressed(int key) {
    if (s_active) {
        for (int i = 0; i < s_pressed_count; i++) {
            if (s_pressed_keys[i] == key) {
                // Match raylib semantics: each call returns the edge
                // exactly once. We do this by marking the slot consumed
                // with a sentinel (-1).
                s_pressed_keys[i] = -1;
                return true;
            }
        }
        return false;
    }
    return IsKeyPressed(key);
}

bool harness_key_down(int key) {
    if (s_active) {
        for (int i = 0; i < s_down_count; i++) {
            if (s_down_keys[i] == key) return true;
        }
        // A pressed key also implies "down" for this frame, matching
        // raylib (a real keystroke registers as both pressed and down
        // on the frame it edges).
        for (int i = 0; i < s_pressed_count; i++) {
            if (s_pressed_keys[i] == key) return true;
        }
        return false;
    }
    return IsKeyDown(key);
}

int harness_get_key_pressed(void) {
    if (s_active) {
        // Return the next un-consumed pressed key. Each call advances
        // the cursor so we walk the queue in order. raylib's
        // GetKeyPressed returns 0 when the queue is empty.
        while (s_get_key_idx < s_pressed_count) {
            int k = s_pressed_keys[s_get_key_idx++];
            if (k > 0) return k;
        }
        return 0;
    }
    return GetKeyPressed();
}

int harness_get_char_pressed(void) {
    if (s_active) {
        if (s_chars_head == s_chars_tail) return 0;
        int c = s_chars[s_chars_head];
        s_chars_head = (s_chars_head + 1) % MAX_QUEUE;
        return c;
    }
    return GetCharPressed();
}
