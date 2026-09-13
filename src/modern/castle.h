// src/modern/castle.h
//
// The modern castle screens, built on the town screen's layout (REQ-430l): the
// home castle (Recruit, Audience) and a castle the hero owns (Garrison,
// Withdraw). A main page of sections, each opening a page of rows and Back;
// Esc goes back a level. Moving troops or recruiting opens the count stepper.
// Legacy never includes this header; its castle screens are unchanged.

#ifndef OB_MODERN_CASTLE_H
#define OB_MODERN_CASTLE_H

#include "game.h"
#include <stdbool.h>

typedef enum {
    MC_MENU = 0,
    MC_RECRUIT,     // home castle
    MC_AUDIENCE,    // home castle
    MC_GARRISON,    // own castle: army -> castle
    MC_WITHDRAW,    // own castle: castle -> army
    MC_PROMOTION,   // home castle: the award, after a promotion (win/lose size)
} McPage;

// Bind the screen to a castle as its view opens (home = the audience castle).
void modern_castle_open(const Game *g, bool home, const char *castle_id);
// Input for one frame. True when the hero leaves the castle.
bool modern_castle_update(Game *g);

bool        modern_castle_is_home(void);
const char *modern_castle_id(void);
McPage      modern_castle_page(void);
int         modern_castle_cursor(void);      // on the page shown
int         modern_castle_rows(const Game *g);
// Row i's label; *troop_id (may be NULL) names the troop the row stands for.
void        modern_castle_row(const Game *g, int i, char *out, int cap,
                              const char **troop_id);
// The count stepper, while one is open.
bool        modern_castle_stepper(int *value, int *max);
// A result message until the next key, or NULL.
const char *modern_castle_message(void);
// The last audience: 0 none yet this visit, else its GameAudienceOutcome + 1;
// *needed the enemies still wanted, *rank the rank index promoted to.
int         modern_castle_audience(int *needed, int *rank);
// economy.audiences: which audience the Audience page's last answer is for.
typedef enum { MC_AUD_PROMOTION = 0, MC_AUD_BLESSING, MC_AUD_TRIBUTE } McAudience;
// The last Blessing or Tribute this visit: *result 0 none yet, Blessing its
// GameBlessingOutcome + 1, Tribute 1 paid or 2 short; *needed the artifacts
// missing or gold short; *gain what it gave.
McAudience  modern_castle_audience_result(int *result, int *needed, GameAudienceGain *gain);
// A tribute waits on Yes/No: true once, writing the question into body. The
// main loop opens the prompt and on Yes calls modern_castle_confirm_yes.
bool        modern_castle_take_confirm(const Game *g, char *body, int cap);
void        modern_castle_confirm_yes(Game *g);
// The five castle troops in recruit order (by cost); returns the count.
int         modern_castle_pool(int *out, int cap);

#endif
