#include "frame_host.h"
#include "input_host.h"
#include "ui_host.h"
#include "ui.h"
#include "overlay.h"     // overlay_dialog_page_count (renderer owns the wrap)
#include "player_io.h"   // engine player-IO message queue
#include "raylib.h"
#include "recorder.h"
#include <string.h>
#include <stdio.h>

// ---- any-key helper --------------------------------------------------------
bool ui_any_key_pressed(void) {
    int k = input_get_key_pressed();
    while (k != 0) {
        if (k != KEY_LEFT_SHIFT && k != KEY_RIGHT_SHIFT &&
            k != KEY_LEFT_CONTROL && k != KEY_RIGHT_CONTROL &&
            k != KEY_LEFT_ALT && k != KEY_RIGHT_ALT &&
            k != KEY_LEFT_SUPER && k != KEY_RIGHT_SUPER &&
            k != KEY_CAPS_LOCK && k != KEY_NUM_LOCK && k != KEY_SCROLL_LOCK) {
            return true;
        }
        k = input_get_key_pressed();
    }
    return false;
}

// ---- Dialog ----------------------------------------------------------------
static bool dialog_active = false;
static char dialog_header[256];
static char dialog_body[512];
static int  dialog_page = 0;    // current page offset for pagination

static void copy_to(char *dst, size_t dst_sz, const char *src) {
    size_t n = 0;
    if (src) {
        while (n + 1 < dst_sz && src[n]) { dst[n] = src[n]; n++; }
    }
    dst[n] = '\0';
}

void open_dialog(const char *header, const char *body) {
    open_dialog_flags(header, body, MSG_FLAG_NONE);
}

// MSG_PADDED handling lives here so callers don't have to know about
// layout. PADDED prepends "\n\n\n" to push the body down toward the
// vertical center of the fixed-size bottom panel. Data strings stay
// clean; layout decisions stay in the renderer.
void open_dialog_flags(const char *header, const char *body, int flags) {
    copy_to(dialog_header, sizeof(dialog_header), header);
    if ((flags & MSG_FLAG_PADDED) && body) {
        char padded[sizeof(dialog_body)];
        // 3 newlines + body, truncated if needed.
        int n = 0;
        if (n + 1 < (int)sizeof(padded)) padded[n++] = '\n';
        if (n + 1 < (int)sizeof(padded)) padded[n++] = '\n';
        if (n + 1 < (int)sizeof(padded)) padded[n++] = '\n';
        for (const char *p = body; *p && n + 1 < (int)sizeof(padded); p++) {
            padded[n++] = *p;
        }
        padded[n] = '\0';
        copy_to(dialog_body, sizeof(dialog_body), padded);
    } else {
        copy_to(dialog_body, sizeof(dialog_body), body);
    }
    dialog_active = true;
    dialog_page = 0;
    {
        // dialog_header can be up to 256 bytes; cap the trace tag at 64
        // and let snprintf truncate cleanly (only used for debug trace).
        char tag[320];
        snprintf(tag, sizeof tag, "dialog:open:%.64s", dialog_header);
        recorder_capture(tag);
    }
}

bool dialog_is_active(void)   { return dialog_active; }
void dialog_dismiss(void)     {
    bool was = dialog_active;
    dialog_active = false; dialog_page = 0;
    if (was) recorder_capture("dialog:close");
}

const char *dialog_header_text(void) { return dialog_header; }
const char *dialog_body_text(void)   { return dialog_body;   }

bool shell_pump_player_io_message(Game *g) {
    // One message at a time: only surface a queued REQ_MESSAGE when the dialog
    // slot is free, so each message gets its own press-any-key dismissal (the
    // engine raises them one per interaction; FIFO order is preserved). A queued
    // DECISION at the front blocks message pumping -- it must be answered first
    // (the decision path owns the prompt UI), and ack does not pop decisions.
    if (!g || dialog_active) return false;
    const PlayerRequest *r = player_io_front(g);
    if (!r || r->role != REQ_MESSAGE) return false;
    open_dialog(r->header[0] ? r->header : NULL, r->body);
    player_io_ack(g);   // consumed: it now lives in the shell dialog
    return true;
}

int dialog_page_current(void)  { return dialog_page; }
void dialog_page_next(void)    { dialog_page++; }

// Advance to the next page if available. Returns true if advanced, false if on last page.
bool dialog_advance(void) {
    const char *body = dialog_body;
    if (!body || !body[0]) return false;

    // Page count comes from the renderer, which word-wraps to the panel
    // width. Counting raw newlines here (the old approach) under-counted a
    // wrapped paragraph and left its overflow unreachable.
    int pages = overlay_dialog_page_count();
    if (dialog_page + 1 < pages) {
        dialog_page++;
        return true;
    }
    return false;
}

// ---- Toast -----------------------------------------------------------------
static char   toast_text[128];
static double toast_until;

void toast_show(const char *msg) {
    copy_to(toast_text, sizeof(toast_text), msg);
    toast_until = frame_host_time() + 2.0;
}

const char *toast_text_current(void) {
    if (!toast_text[0] || frame_host_time() >= toast_until) return NULL;
    return toast_text;
}

