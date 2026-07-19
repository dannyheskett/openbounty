// engine/include/ui_host.h
//
// Functions the engine calls into but does NOT define. Consumers of
// libobengine.a (the raylib shell, the test binary, or any headless
// driver) must provide implementations at link time.
//
// The signatures below are stable -- they are the engine->host boundary.
// engine/host_noop.c provides default no-op implementations; consumers
// can link it as a starting point, or replace with real ones.
//
// Event vocabulary (see per-section docs below):
//   Modal prompts:  engine opens; host renders + reads input.
//   Dialogs:        engine opens; host renders, host dismisses on input.
//   View stack:     host owns the stack; engine queries which is active.
//   Screen openers: engine asks host to open a specific screen on a
//                   step event (entered town, found alcove, etc).
//   Audio events:   engine emits abstract events; host may play sound.
//   Recorder pings: engine notifies on canonical mutations; host may snap.

#ifndef OB_ENGINE_UI_HOST_H
#define OB_ENGINE_UI_HOST_H

#include <stdbool.h>

#include "game.h"
#include "dwelling_kind.h"
#include "view_kind.h"

// ---- Modal prompts ---------------------------------------------------------
//
// Bottom-frame yes/no, A/B, and text-entry prompts. The engine opens a
// prompt during a step (e.g. "Search this area?", "Buy how many?"), the
// host's render/input loop drives it, and the host signals back to the
// engine via dismissal: the next engine tick observes
// `prompt_is_active() == false` and reads the result via host-specific
// channels (the game's `prompt_update()` returns it, captured by step.c
// flows). Engine code that opens a prompt then returns; the next step
// call picks up the resolved state.
//
// Lifecycle:
//   1. Engine calls prompt_*_open(...)
//   2. Host renders the prompt and consumes input until resolved
//   3. Host dismisses (sets internal state so prompt_is_active() = false)
//   4. Engine's next step sees no active prompt and proceeds
//
// `prompt_is_active`, `prompt_kind_str`, `prompt_header_text`,
// `prompt_body_text` are also read by `state_serialize` so JSON
// snapshots include current prompt state.

void prompt_yes_no_open(const char *header, const char *body);
void prompt_ab_open(const char *header, const char *body);
void prompt_text_input_open(const char *header, const char *body,
                            int max_digits, int max_value);
bool prompt_is_active(void);
const char *prompt_kind_str(void);   // "yes_no" | "ab" | "text" | "none"
const char *prompt_header_text(void);
const char *prompt_body_text(void);

// ---- Bottom-frame dialogs --------------------------------------------------
//
// Non-blocking informational dialogs (chest contents, signposts,
// encounter banners, etc). Engine calls open_dialog with the composed
// text; host displays it; host dismisses on input (the dialog is
// purely informational, no engine state depends on its dismissal).
//
// `dialog_is_active` and the text accessors are used by state_serialize
// to include dialog content in JSON snapshots.

void open_dialog(const char *header, const char *body);
bool dialog_is_active(void);
const char *dialog_header_text(void);
const char *dialog_body_text(void);

// ---- View stack ------------------------------------------------------------
//
// Full-screen overlays (army, spells, contract, town, etc.). The host
// owns the stack; engine queries via `views_active()` for state
// serialization and to gate behavior (e.g. flows.c checks if the WIN
// view is active before re-opening it).
//
// Engine asks the host to push a specific screen via the screen_*_open
// callbacks. These are invoked from step.c when the hero steps onto a
// location tile (town, castle, alcove, dwelling) and the appropriate
// screen needs to come up.
//
// Lifecycle:
//   1. GameStep detects the hero entered a special tile
//   2. Engine calls screen_<kind>_open with context (Game *, ids, etc.)
//   3. Host pushes the view onto its stack and renders it
//   4. User dismisses (ESC); host pops the view
//   5. Engine's next step sees views_active() == VIEW_NONE and resumes
//      normal stepping

ViewKind views_active(void);

// Open the town view. display_name = human-readable ("Riverton"),
// record_key = map-local id ("town_000") used as a key into
// Game.towns[], boat_x/y = where a rented boat should spawn.
void views_open_town(const char *display_name, const char *record_key,
                     int boat_x, int boat_y);

// Castle of King Maximus. Pushes a recruit-soldiers + audience screen.
void screen_home_castle_open(Game *g);

// Player-owned castle. castle_id matches the castle entry in
// Game.castles[]. Host shows garrison + remove troops UI.
void screen_own_castle_open(Game *g, const char *castle_id);

// Archmage Aurange's alcove. Host shows the offer to teach magic.
void screen_alcove_open(Game *g);

// Outdoor dwelling (Plains/Forest/Hill/Dungeon). Host shows location
// backdrop + recruit banner. troop_id selects the animated troop
// sprite; dwelling_pop = how many available; recruit_cost = gold each;
// gold = player's current gold; cap = max recruitable given army
// space and leadership.
void screen_dwelling_open(Game *g,
                          DwellingKind kind,
                          const char *troop_id,
                          int dwelling_pop,
                          int recruit_cost,
                          int gold,
                          int cap);

// End-of-game screen. won = true -> VIEW_WIN; false -> VIEW_LOSE.
// body = already-substituted text (engine composes from
// res.win_text / res.lose_text with %NAME%/%RANK%/%SCORE% filled in).
void screen_end_game_open(bool won, const char *body);

// ---- Engine state queries dependent on shell-managed UI state --------------
//
// Engine reads these to gate its own behavior. The game binary defines
// main_fast_quit_active() in main.c (the Ctrl+Q "Quit without saving?"
// prompt is a render-side modal that hijacks the status bar). When
// active, the engine skips advancing time-sensitive flows. Headless
// consumers return false.

bool main_fast_quit_active(void);

// ---- Audio events ----------------------------------------------------------
//
// Engine emits abstract sound-effect events at canonical points:
// hero walked, bumped into an obstacle, opened a chest, was defeated.
// The host either translates to actual playback or no-ops. There is no
// engine-side audio state -- events are fire-and-forget.

typedef enum {
    AUDIO_TUNE_WALK   = 0,  // step succeeded (movement happened)
    AUDIO_TUNE_BUMP,        // step blocked (obstacle / off-map)
    AUDIO_TUNE_CHEST,       // treasure chest opened
    AUDIO_TUNE_DEFEAT,      // hero lost combat / died
    AUDIO_TUNE_COUNT
} AudioTuneId;

void audio_play_tune(AudioTuneId t);

// ---- Recorder --------------------------------------------------------------
//
// Engine notifies the host at canonical mutation sites -- every
// completed step, every combat hit, every flow trigger. The host may
// snapshot state (the game's recorder serializes + framebuffer-PNGs)
// or no-op. `trigger` is a short tag like "step:right",
// "combat:hit:s0:u1->s1:u2:42", "flow:foe:friendly".

void recorder_capture(const char *trigger);


#endif
