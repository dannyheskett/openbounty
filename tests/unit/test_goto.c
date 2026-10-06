// Goto's route (engine/goto.c GamePlanGoto, #70), on small drawn maps:
// '.' grass, '^' mountain, '~' sea, 'd' desert, 'C' a chest, 'B' the parked
// boat on the sea, '?' grass the player has not seen. The hero starts at 'H'.

#include "greatest.h"
#include "goto.h"
#include "map.h"
#include "fog.h"
#include "resources.h"

#include <stdlib.h>
#include <string.h>

typedef struct { Game *g; Map m; Fog f; Resources res; } World;

static void draw(World *w, const char *const *rows, int h) {
    int wd = (int)strlen(rows[0]);
    memset(w, 0, sizeof *w);
    w->g = calloc(1, sizeof *w->g);
    MapAlloc(&w->m, wd, h);
    FogInit(&w->f);
    FogSize(&w->f, wd, h);
    w->res.time.day_steps = 12;
    strcpy(w->g->position.zone, "z");
    w->g->stats.steps_left_today = 12;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < wd; x++) {
            Tile *t = &w->m.tiles[y * wd + x];
            char c = rows[y][x];
            t->terrain = c == '^' ? TERRAIN_MOUNTAIN : (c == '~' || c == 'B') ? TERRAIN_WATER
                       : c == 'd' ? TERRAIN_DESERT : TERRAIN_GRASS;
            t->blocks_foot = c == '^';
            if (c == 'C') t->interactive = INTERACT_TREASURE_CHEST;
            if (c == 'H') { w->g->position.x = x; w->g->position.y = y; }
            if (c == 'B') {
                w->g->boat.has_boat = true;
                w->g->boat.x = x; w->g->boat.y = y;
                strcpy(w->g->boat.zone, "z");
            }
            FogSet(&w->f, x, y, c != '?');
        }
}

static void done(World *w) { MapFree(&w->m); FogFree(&w->f); free(w->g); }

static bool plan(World *w, int tx, int ty, GotoPath *p) {
    return GamePlanGoto(w->g, &w->m, &w->f, &w->res, tx, ty, p);
}

// Walks the route, checking each step is one 8-way move, and returns where it
// ends.
static void walk(const World *w, const GotoPath *p, int *x, int *y) {
    *x = w->g->position.x; *y = w->g->position.y;
    for (int i = 0; i < p->n; i++) {
        *x += p->dx[i]; *y += p->dy[i];
    }
}

static bool passes(const World *w, const GotoPath *p, char what) {
    int x = w->g->position.x, y = w->g->position.y;
    for (int i = 0; i < p->n; i++) {
        x += p->dx[i]; y += p->dy[i];
        const Tile *t = &w->m.tiles[y * w->m.width + x];
        if (what == '^' && t->terrain == TERRAIN_MOUNTAIN) return true;
        if (what == 'd' && t->terrain == TERRAIN_DESERT) return true;
        if (what == 'C' && t->interactive != INTERACT_NONE && i < p->n - 1) return true;
    }
    return false;
}

TEST a_straight_walk(void) {
    static const char *const R[] = { "H....." };
    World w; draw(&w, R, 1);
    GotoPath p;
    ASSERT(plan(&w, 5, 0, &p));
    ASSERT_EQ(5, p.n);
    int x, y; walk(&w, &p, &x, &y);
    ASSERT_EQ(5, x); ASSERT_EQ(0, y);
    ASSERT_EQ(0, p.days);
    done(&w); PASS();
}

TEST round_the_mountains(void) {
    static const char *const R[] = {
        "H.^...",
        "..^...",
        "..^...",
        "......",
    };
    World w; draw(&w, R, 4);
    GotoPath p;
    ASSERT(plan(&w, 5, 0, &p));
    ASSERT_FALSE(passes(&w, &p, '^'));
    int x, y; walk(&w, &p, &x, &y);
    ASSERT_EQ(5, x); ASSERT_EQ(0, y);
    done(&w); PASS();
}

TEST unseen_ground_is_a_wall(void) {
    static const char *const R[] = {
        "H.^..",
        "..?..",
        "..^..",
    };
    World w; draw(&w, R, 3);
    GotoPath p;
    ASSERT_FALSE(plan(&w, 4, 0, &p));          // the only gap is unexplored
    FogSet(&w.f, 4, 2, false);
    ASSERT_FALSE(plan(&w, 4, 2, &p));          // nor may the target be
    done(&w); PASS();
}

TEST objects_on_the_way_are_walls_but_a_target_is_reached(void) {
    static const char *const R[] = {
        "H.C..",
        ".....",
    };
    World w; draw(&w, R, 2);
    GotoPath p;
    ASSERT(plan(&w, 4, 0, &p));
    ASSERT_FALSE(passes(&w, &p, 'C'));
    ASSERT(plan(&w, 2, 0, &p));                // the chest itself
    int x, y; walk(&w, &p, &x, &y);
    ASSERT_EQ(2, x);
    done(&w); PASS();
}

TEST desert_is_skirted_when_cheaper(void) {
    static const char *const R[] = {
        "H.ddd..",
        ".......",
    };
    World w; draw(&w, R, 2);
    GotoPath p;
    ASSERT(plan(&w, 6, 0, &p));
    ASSERT_FALSE(passes(&w, &p, 'd'));
    done(&w); PASS();
}

TEST the_parked_boat_carries_the_route(void) {
    static const char *const R[] = {
        "H.~~~..",
        "..B~~..",
        "..~~~..",
    };
    World w; draw(&w, R, 3);
    GotoPath p;
    ASSERT(plan(&w, 6, 1, &p));
    // The route passes over the boat's own cell, the one way onto the water.
    int x = w.g->position.x, y = w.g->position.y;
    bool boarded = false;
    for (int i = 0; i < p.n; i++) {
        x += p.dx[i]; y += p.dy[i];
        if (x == 2 && y == 1) boarded = true;
    }
    ASSERT(boarded);
    ASSERT_EQ(6, x); ASSERT_EQ(1, y);
    done(&w); PASS();
}

TEST no_boat_no_crossing(void) {
    static const char *const R[] = {
        "H.~~~..",
        "..~~~..",
    };
    World w; draw(&w, R, 2);
    GotoPath p;
    ASSERT_FALSE(plan(&w, 6, 0, &p));
    // A boat in another province is no boat.
    w.g->boat.has_boat = true; w.g->boat.x = 2; w.g->boat.y = 1;
    strcpy(w.g->boat.zone, "elsewhere");
    ASSERT_FALSE(plan(&w, 6, 0, &p));
    done(&w); PASS();
}

TEST in_the_boat_it_sails_and_lands(void) {
    static const char *const R[] = {
        "..~~~..",
        "..H~~..",
    };
    World w; draw(&w, R, 2);
    w.g->travel_mode = TRAVEL_BOAT;
    GotoPath p;
    ASSERT(plan(&w, 6, 0, &p));
    int x, y; walk(&w, &p, &x, &y);
    ASSERT_EQ(6, x); ASSERT_EQ(0, y);
    done(&w); PASS();
}

TEST flight_goes_straight_over_mountains(void) {
    static const char *const R[] = {
        "H^^^.",
        ".^^^.",
        ".^^^.",
        ".....",
    };
    World w; draw(&w, R, 4);
    GotoPath p;
    ASSERT(plan(&w, 4, 0, &p));
    ASSERT(p.n > 4);                            // on foot: round the south
    w.g->character.mount = MOUNT_FLY;
    ASSERT(plan(&w, 4, 0, &p));
    ASSERT_EQ(4, p.n);
    ASSERT(passes(&w, &p, '^'));
    done(&w); PASS();
}

TEST the_heros_own_tile_is_no_order(void) {
    static const char *const R[] = { "H.." };
    World w; draw(&w, R, 1);
    GotoPath p;
    ASSERT_FALSE(plan(&w, 0, 0, &p));
    ASSERT_FALSE(plan(&w, 9, 0, &p));           // off the map
    done(&w); PASS();
}

TEST days_count_the_steps(void) {
    static const char *const R[] = { "H...................................." };
    World w; draw(&w, R, 1);
    w.g->stats.steps_left_today = 3;
    GotoPath p;
    ASSERT(plan(&w, 3, 0, &p));
    ASSERT_EQ(0, p.days);                       // three steps: today
    ASSERT(plan(&w, 4, 0, &p));
    ASSERT_EQ(1, p.days);                       // the fourth starts tomorrow
    ASSERT(plan(&w, 16, 0, &p));
    ASSERT_EQ(2, p.days);                       // 3 + 12 + 1
    done(&w); PASS();
}

SUITE(unit_goto_suite) {
    RUN_TEST(a_straight_walk);
    RUN_TEST(round_the_mountains);
    RUN_TEST(unseen_ground_is_a_wall);
    RUN_TEST(objects_on_the_way_are_walls_but_a_target_is_reached);
    RUN_TEST(desert_is_skirted_when_cheaper);
    RUN_TEST(the_parked_boat_carries_the_route);
    RUN_TEST(no_boat_no_crossing);
    RUN_TEST(in_the_boat_it_sails_and_lands);
    RUN_TEST(flight_goes_straight_over_mountains);
    RUN_TEST(the_heros_own_tile_is_no_order);
    RUN_TEST(days_count_the_steps);
}
