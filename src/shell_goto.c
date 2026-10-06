// src/shell_goto.c -- Goto's walk (see shell_goto.h).

#include "shell_goto.h"
#include "pending.h"
#include "player_io.h"
#include "prompt.h"
#include "ui.h"
#include "views.h"

#include <string.h>

// A held tap's repeat (src/touch.c REPEAT_INTERVAL): the walk keeps the pace
// a finger would.
#define GOTO_BEAT 0.15

static GotoPath s_path;
static int      s_next;              // the step to take next
static int      s_x, s_y;            // where the hero stands before it
static char     s_zone[sizeof ((Game *)0)->position.zone];
static bool     s_active;
static double   s_due;

void shell_goto_start(const Game *g, const GotoPath *p) {
    if (!g || !p || p->n <= 0) { s_active = false; return; }
    s_path = *p;
    s_next = 0;
    s_x = g->position.x;
    s_y = g->position.y;
    memcpy(s_zone, g->position.zone, sizeof s_zone);
    s_due = 0;
    s_active = true;
}

void shell_goto_cancel(void) { s_active = false; }
bool shell_goto_active(void) { return s_active; }

// Something waits on the player, or a screen is up.
static bool interrupted(const Game *g) {
    return !player_io_idle(g) || pending_flow != FLOW_NONE ||
           pending_week_phase != WK_PHASE_NONE || pending_foe_bounce ||
           views_active() != VIEW_NONE || dialog_is_active() || prompt_is_active() ||
           g->stats.game_over;
}

bool shell_goto_next(const Game *g, double now, int *dx, int *dy) {
    if (!s_active || !g) return false;
    if (s_next >= s_path.n || interrupted(g) ||
        g->position.x != s_x || g->position.y != s_y ||
        strcmp(g->position.zone, s_zone) != 0) {
        s_active = false;
        return false;
    }
    if (now < s_due) return false;
    s_due = now + GOTO_BEAT;
    *dx = s_path.dx[s_next];
    *dy = s_path.dy[s_next];
    return true;
}

void shell_goto_after_step(const Game *g, bool moved) {
    if (!s_active || !g) return;
    if (!moved) { s_active = false; return; }
    s_x += s_path.dx[s_next];
    s_y += s_path.dy[s_next];
    s_next++;
    if (s_next >= s_path.n || interrupted(g) ||
        g->position.x != s_x || g->position.y != s_y)
        s_active = false;
}
