#ifndef OB_UI_H
#define OB_UI_H

#include <stdbool.h>

// UI layer: dialog (press-any-key message), toast (transient banner),
// pause menu (Save/Load/New/Quit). Nothing persistent lives here — the
// Game struct owns state; this module owns presentation and transient
// UI flags.
//
// Drawing happens in the render path (src/overlay.c).
// This module owns only state and transitions; renderers read via the
// *_text() accessors.

// ---- Dialog --------------------------------------------------------------
// open_dialog() sets a bottom-screen dialog box with a header and body.
// Any key while a dialog is open dismisses it.
void open_dialog(const char *header, const char *body);

// Flags for open_dialog_flags.  MSG_* flags from
// . Currently only padding is implemented; others can be
// added as needed.
//
//   MSG_PADDED — prepend "\n\n\n" to the body so short messages sit
//   visually centered in the fixed-size bottom panel.  etc.
//   show this padding in their verbatim banners; it's a layout
//   decision, not localizable content, so it lives in the renderer
//   rather than the data string.
#define MSG_FLAG_NONE    0
#define MSG_FLAG_PADDED  0x04   //  MSG_PADDED bit value

void open_dialog_flags(const char *header, const char *body, int flags);

bool dialog_is_active(void);
void dialog_dismiss(void);

// Read-only accessors for renderers.
const char *dialog_header_text(void);
const char *dialog_body_text(void);

// Dialog pagination (for multi-page text like at King's castle).
int  dialog_page_current(void);
void dialog_page_next(void);
bool dialog_advance(void);  // Advance to next page if available; returns true if advanced

// Toast accessors too.
const char *toast_text_current(void);   // NULL if no active toast

// ---- Toast ---------------------------------------------------------------
// Transient banner near the top of the playfield. Lasts a few seconds.
void toast_show(const char *msg);

// ---- Utility -------------------------------------------------------------
// Returns true if any non-modifier key was pressed this frame. Drains the
// raylib key-press queue.
bool ui_any_key_pressed(void);

#endif
