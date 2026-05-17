// engine/host_noop.c
//
// Default no-op implementations of every callback declared in
// engine/include/ui_host.h. Consumers that don't need real UI (e.g.
// headless playtest, balance analyzer, server) can link this file to
// satisfy the engine's link-time dependencies.
//
// Real game/shell consumers (the openbounty binary) implement these
// for real and don't link this file.

#include "ui_host.h"

#include <stddef.h>

// ---- Modal prompts ---------------------------------------------------------
void prompt_yes_no_open(const char *h, const char *b)        { (void)h; (void)b; }
void prompt_ab_open(const char *h, const char *b)            { (void)h; (void)b; }
void prompt_text_input_open(const char *h, const char *b,
                            int d, int v)                    { (void)h; (void)b; (void)d; (void)v; }
bool prompt_is_active(void)                                  { return false; }
const char *prompt_kind_str(void)                            { return "none"; }
const char *prompt_header_text(void)                         { return ""; }
const char *prompt_body_text(void)                           { return ""; }

// ---- Bottom-frame dialogs --------------------------------------------------
void open_dialog(const char *h, const char *b)               { (void)h; (void)b; }
bool dialog_is_active(void)                                  { return false; }
const char *dialog_header_text(void)                         { return ""; }
const char *dialog_body_text(void)                           { return ""; }

// ---- View stack ------------------------------------------------------------
ViewKind views_active(void)                                  { return VIEW_NONE; }
void views_open_town(const char *n, const char *k,
                     int bx, int by)                         { (void)n; (void)k; (void)bx; (void)by; }
void screen_home_castle_open(Game *g)                        { (void)g; }
void screen_own_castle_open(Game *g, const char *cid)        { (void)g; (void)cid; }
void screen_alcove_open(Game *g)                             { (void)g; }
void screen_dwelling_open(Game *g, DwellingKind k,
                          const char *tid,
                          int pop, int cost, int gold, int cap) {
    (void)g; (void)k; (void)tid; (void)pop; (void)cost; (void)gold; (void)cap;
}
void screen_end_game_open(bool won, const char *body)        { (void)won; (void)body; }

// ---- Audio events ----------------------------------------------------------
void audio_play_tune(AudioTuneId t)                          { (void)t; }

// ---- Recorder --------------------------------------------------------------
void recorder_capture(const char *trigger)                   { (void)trigger; }

// ---- Engine state queries dependent on shell state -------------------------
bool main_fast_quit_active(void)                             { return false; }
