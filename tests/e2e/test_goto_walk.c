// Goto's walk (src/shell_goto.c, #70) on the fixture game: a planned route is
// walked with ordinary GameSteps, one a beat, and stops the moment anything
// waits on the player, the step fails, or the hero is moved off the route.

#include "greatest.h"
#include "fixtures.h"
#include "game.h"
#include "goto.h"
#include "step.h"
#include "pending.h"
#include "player_io.h"
#include "shell_goto.h"
#include "prompt.h"

#include <stdlib.h>

// A seen, plain tile a few steps from the hero, with a route to it.
static bool near_target(Game *g, Map *m, Fog *f, const Resources *res, GotoPath *p,
                        int *tx, int *ty) {
    prompt_dismiss();                    // nothing left open by an earlier suite
    FogRevealRect(f, m, g->position.x, g->position.y, 8, 8);
    for (int r = 3; r <= 6; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                int x = g->position.x + dx, y = g->position.y + dy;
                const Tile *t = MapGetTile(m, x, y);
                if (!t || t->interactive != INTERACT_NONE || t->terrain != TERRAIN_GRASS) continue;
                if (GamePlanGoto(g, m, f, res, x, y, p) && p->n >= 3) { *tx = x; *ty = y; return true; }
            }
    return false;
}

// Runs the walk the way the main loop does, a beat apart, until it ends.
static int walk(Game *g, Map *m, Fog *f, const Resources *res) {
    double now = 0;
    int steps = 0;
    for (int frame = 0; frame < 5000 && shell_goto_active(); frame++, now += 0.05) {
        int dx, dy;
        if (!shell_goto_next(g, now, &dx, &dy)) continue;
        bool moved = GameStep(g, m, f, res, dx, dy);
        shell_goto_after_step(g, moved);
        if (moved) steps++;
    }
    return steps;
}

TEST the_walk_arrives(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    GotoPath p; int tx, ty;
    ASSERT(near_target(g, m, f, res, &p, &tx, &ty));
    shell_goto_start(g, &p);
    ASSERT(shell_goto_active());
    int steps = walk(g, m, f, res);
    ASSERT_FALSE(shell_goto_active());
    if (player_io_idle(g) && pending_flow == FLOW_NONE) {   // nothing met on the way
        ASSERT_EQ(p.n, steps);
        ASSERT_EQ(tx, g->position.x);
        ASSERT_EQ(ty, g->position.y);
    }
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST anything_waiting_stops_it(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    GotoPath p; int tx, ty;
    ASSERT(near_target(g, m, f, res, &p, &tx, &ty));
    int dx, dy;

    // A question the player must answer.
    shell_goto_start(g, &p);
    pending_flow = FLOW_CHEST_CHOICE;
    ASSERT_FALSE(shell_goto_next(g, 1.0, &dx, &dy));
    ASSERT_FALSE(shell_goto_active());
    pending_flow = FLOW_NONE;

    // A message in the queue.
    shell_goto_start(g, &p);
    player_io_note(g, "A sign", "Words.");
    ASSERT_FALSE(shell_goto_next(g, 1.0, &dx, &dy));
    ASSERT_FALSE(shell_goto_active());
    player_io_reset(g);

    // The hero moved by something else (a load, a gate).
    shell_goto_start(g, &p);
    g->position.x += 1;
    ASSERT_FALSE(shell_goto_next(g, 1.0, &dx, &dy));
    ASSERT_FALSE(shell_goto_active());
    g->position.x -= 1;

    // A step that does not move the hero.
    shell_goto_start(g, &p);
    ASSERT(shell_goto_next(g, 1.0, &dx, &dy));
    shell_goto_after_step(g, false);
    ASSERT_FALSE(shell_goto_active());

    // The beat: one step, then nothing until it has passed.
    shell_goto_start(g, &p);
    ASSERT(shell_goto_next(g, 1.0, &dx, &dy));
    ASSERT(GameStep(g, m, f, res, dx, dy));
    shell_goto_after_step(g, true);
    if (shell_goto_active()) ASSERT_FALSE(shell_goto_next(g, 1.05, &dx, &dy));
    shell_goto_cancel();

    fx_free_game_full(res, g, m, f);
    PASS();
}

SUITE(e2e_goto_walk_suite) {
    RUN_TEST(the_walk_arrives);
    RUN_TEST(anything_waiting_stops_it);
}
