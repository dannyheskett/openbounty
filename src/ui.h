#ifndef OB_UI_H
#define OB_UI_H

#include "raylib.h"
#include <stdbool.h>

// ---- Texture blit --------------------------------------------------------
// Draw `t` stretched to exactly (x, y, w, h). No-op when the texture is
// missing (id 0), so callers don't each need a guard.
//
// Use this for every piece of art instead of blitting at tex.width/tex.height.
// Both packs author their art in the legacy 320x200 design space -- 48x34
// tiles and troops, 240x102 backdrops, 96x102 portraits -- so drawing at the
// native size means drawing at 1x inside a buffer that is scaled up. The size
// that is correct is always the slot the art goes in: a map or combat cell is
// CL_TILE_W/H, anything else is its design size times CL_UI.
void ui_blit(Texture2D t, int x, int y, int w, int h);

// As ui_blit, but flipped horizontally. Sprites are authored facing right;
// combat mirrors the AI side rather than shipping a second strip.
void ui_blit_mirrored(Texture2D t, int x, int y, int w, int h);

// Forward decl: the engine Game (carries the player-IO request queue).
typedef struct Game Game;

// UI layer: dialog (press-any-key message), toast (transient banner), and
// the any-key helper. The pause menu itself lives in src/views.c. Nothing
// persistent lives here -- the Game struct owns state; this module owns
// presentation and transient UI flags.
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
//   MSG_PADDED -- prepend "\n\n\n" to the body so short messages sit
//   visually centered in the fixed-size bottom panel.  etc.
//   show this padding in their verbatim banners; it's a layout
//   decision, not localizable content, so it lives in the renderer
//   rather than the data string.
#define MSG_FLAG_NONE    0
#define MSG_FLAG_PADDED  0x04   //  MSG_PADDED bit value

void open_dialog_flags(const char *header, const char *body, int flags);

bool dialog_is_active(void);
void dialog_dismiss(void);

// Drain the engine's player-IO message queue into the shell dialog . When no
// shell dialog is currently up and the queue front is a REQ_MESSAGE, this copies
// that message into the dialog renderer and acks the queue entry (so the engine's
// uniform messages render through the existing dialog UI). Call once per frame
// before the dialog input gate. Returns true if it surfaced a new message.
bool shell_pump_player_io_message(Game *g);

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
