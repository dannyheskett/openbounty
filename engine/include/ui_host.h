// engine/include/ui_host.h
//
// Functions the engine calls into but does NOT define. Consumers of
// libobengine.a (the game's shell, or a headless playtest driver) must
// provide implementations at link time.
//
// The signatures below are stable — they are the engine→host boundary.
// If you're writing a new consumer, you must implement every function
// here (no-ops are fine in headless contexts).

#ifndef OB_ENGINE_UI_HOST_H
#define OB_ENGINE_UI_HOST_H

#include <stdbool.h>

// ---- Modal prompts ---------------------------------------------------------
// Bottom-frame yes/no, numeric, A/B, or text-entry prompts. The engine
// opens them via these calls; the host renders + reads input + resolves
// them via prompt_update(). prompt_is_active() is also queried by the
// engine to gate its own behavior.

void prompt_yes_no_open(const char *header, const char *body);
void prompt_numeric_open(const char *header, const char *body, int max_choice);
void prompt_ab_open(const char *header, const char *body);
void prompt_text_input_open(const char *header, const char *body,
                            int max_digits, int max_value);
int  prompt_text_input_value(void);
bool prompt_is_active(void);
void prompt_dismiss(void);
const char *prompt_kind_str(void);
const char *prompt_header_text(void);
const char *prompt_body_text(void);

// ---- Bottom-frame dialogs --------------------------------------------------

void open_dialog(const char *header, const char *body);
void open_dialog_flags(const char *header, const char *body, int flags);
bool dialog_is_active(void);
const char *dialog_header_text(void);
const char *dialog_body_text(void);

// ---- View stack ------------------------------------------------------------
// The set of full-screen overlays (army, spells, character, etc.) is
// host-managed; the engine queries which is active for state-serializer
// output, and asks the host to open specific screens for events that
// happen mid-step (entering a town, finding an alcove, etc.).

int  views_active(void);   // returns the host's view-id enum; 0 = none

void screen_home_castle_open(void);
void screen_own_castle_open(int castle_index);
void screen_dwelling_open(int dwelling_index);
void screen_alcove_open(void);
void screen_end_game_open(bool won, const char *body);
void views_open_town(int town_index);

// Engine state queries that depend on shell-managed UI state. The game
// binary defines main_fast_quit_active() in main.c (the Ctrl+Q prompt
// is render-side). Headless consumers return false.
bool main_fast_quit_active(void);

// ---- Audio events ----------------------------------------------------------
// Engine emits abstract events; the host translates to actual playback.

typedef enum {
    AUDIO_TUNE_WALK   = 0,
    AUDIO_TUNE_BUMP,
    AUDIO_TUNE_CHEST,
    AUDIO_TUNE_DEFEAT,
    AUDIO_TUNE_COUNT
} AudioTuneId;

void audio_play_tune(AudioTuneId t);

// ---- Recorder --------------------------------------------------------------
// Engine notifies the host at canonical mutation sites so the host can
// snapshot state. Host may capture, log, or no-op.

void recorder_capture(const char *trigger);
void recorder_pending_snap(const char *path);
void recorder_attach_combat(const void *combat);   // void* = Combat*

#endif
