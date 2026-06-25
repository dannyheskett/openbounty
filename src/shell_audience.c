// src/shell_audience.c

#include "shell_audience.h"

#include <stdio.h>
#include <string.h>

#include "pending.h"
#include "tables.h"
#include "ui.h"

// %NAME%/%RANK%/%NEEDED%/%S% substitution for audience text.
static void audience_substitute(const Game *game, int needed,
                                const char *src, char *out, size_t out_sz) {
    if (out_sz == 0) return;
    char *dst = out;
    char *end = out + out_sz - 1;
    while (*src && dst < end) {
        if (*src == '%') {
            const char *tok = src + 1;
            const char *close = strchr(tok, '%');
            if (close) {
                int len = (int)(close - tok);
                const char *sub = NULL;
                char nbuf[16];
                if (len == 4 && !strncmp(tok, "NAME", 4)) {
                    sub = game->character.name;
                } else if (len == 4 && !strncmp(tok, "RANK", 4)) {
                    sub = game->character.cls.rank_title;
                } else if (len == 6 && !strncmp(tok, "NEEDED", 6)) {
                    snprintf(nbuf, sizeof(nbuf), "%d",
                             needed > 0 ? needed : 0);
                    sub = nbuf;
                } else if (len == 1 && tok[0] == 'S') {
                    sub = (needed == 1) ? "" : "s";
                }
                if (sub) {
                    while (*sub && dst < end) *dst++ = *sub++;
                    src = close + 1;
                    continue;
                }
            }
        }
        *dst++ = *src++;
    }
    *dst = '\0';
}

// Two-step flow:
//   1. KB_BottomBox(NULL, "Trumpets announce ... (space)", MSG_PAUSE)
//   2. KB_BottomBox(message, "", MSG_PAUSE)  ← message has king's words
// We open the fanfare dialog now and stash the king's message in
// pending_audience_message for the generic dialog-dismiss path to chain
// into a second open_dialog.
void run_audience_dialog(Game *game, const ResCastle *rc) {
    if (!rc) return;
    const ClassDef *cls = class_by_id(game->character.cls.id);
    int caught = GameVillainsCaught(game);
    int rank   = game->character.cls.rank_index;
    int needed = 0;
    if (cls && rank + 1 < cls->rank_count) {
        needed = cls->ranks[rank + 1].villains_needed - caught;
    }
    bool at_final_rank = !cls || rank + 1 >= cls->rank_count;

    const char *branch;
    if (needed > 0) {
        branch = rc->special.audience_more_needed;
    } else if (!at_final_rank) {
        GameMaybeRankUp(game);
        // After rank-up, character.cls.rank_title now reflects the new
        // rank — substitution below picks that up.
        branch = rc->special.audience_rank_up;
    } else {
        branch = rc->special.audience_final_rank;
    }

    // Build the king's message (page 2) with substitutions applied.
    audience_substitute(game, needed, branch ? branch : "",
                        pending_audience_message,
                        sizeof(pending_audience_message));

    // Open the fanfare dialog first (page 1).
    char fanfare[400];
    audience_substitute(game, needed,
                        rc->special.audience_intro[0]
                            ? rc->special.audience_intro
                            : "Trumpets announce your\n"
                              "arrival with regal fanfare.\n\n"
                              "King Maximus rises from his\n"
                              "throne to greet you and\n"
                              "proclaims:           (space)",
                        fanfare, sizeof(fanfare));
    player_io_message(game, NULL, fanfare);
}
