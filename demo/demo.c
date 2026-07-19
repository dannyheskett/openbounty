// demo/demo.c
//
// Demo-mode entry: boot (headless), the run loop, and the result report.
// Mirrors the engine-pure consumer pattern (pack + Resources + Game/Map/Fog,
// no src/): demo/ links libobengine.a and nothing else.

#include "demo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "demo_internal.h"
#include "pack.h"
#include "pending.h"
#include "tile.h"

#define DEMO_MANIFEST   "game.json"
// Absolute tick ceiling: a runaway guard far beyond any real playthrough
// (a full game is tens of thousands of decisions), never a pacing knob.
#define DEMO_MAX_TICKS  200000

static bool s_verbose = false;
void demo_set_verbose(bool on) { s_verbose = on; }
bool demo_verbose(void) { return s_verbose; }

static DemoHooks s_hooks;
DemoHooks *demo_hooks(void) { return &s_hooks; }
void demo_set_hooks(DemoCombatAnimator animate, DemoTownPresenter town,
                    void *ctx) {
    s_hooks.animate = animate;
    s_hooks.town = (void (*)(void *, int))town;
    s_hooks.ctx = ctx;
}

bool demo_begin(Game *g, Map *map, Fog *fog, const Resources *res) {
    (void)map; (void)res;
    if (!g || !fog) return false;
    DemoState *st = demo_state();
    memset(st, 0, sizeof *st);
    st->cand_reveals = -1;
    printf("[DEMO] begin: seed=%d world=%llu days=%d gold=%d\n",
           g->seed_index, (unsigned long long)g->seed, g->stats.days_left,
           g->stats.gold);
    return true;
}

bool demo_tick(Game *g, Map *map, Fog *fog, const Resources *res) {
    return demo_brain_tick(g, map, fog, res);
}

void demo_result(const Game *g, DemoResult *out) {
    const DemoState *st = demo_state();
    memset(out, 0, sizeof *out);
    out->seed_index = g->seed_index;
    out->won = st->won;
    out->out_of_days = !st->won && g->stats.game_over;
    out->stuck = st->stuck;
    out->days_used = g->res->time.days_per_difficulty[g->character.difficulty] -
                     g->stats.days_left;
    out->score = GameComputeScore(g);
    out->villains = GameVillainsCaught(g);
    out->artifacts = GameArtifactsFound(g);
    out->castles = GameCastlesOwned(g);
    out->searches = st->digs;
    out->ticks = st->ticks;
    // Per-kind pickup tallies, classified exactly: each consumed tile is
    // looked up in its zone's salted map (chests/navmaps/orbs share the
    // consumed[] record with artifacts and the alcove).
    Map *m = malloc(sizeof *m);
    if (m) {
        for (int zi = 0; zi < g->res->zone_count; zi++) {
            const char *zid = g->res->zones[zi].id;
            bool any = false;
            for (int i = 0; i < g->consumed_count && !any; i++)
                any = strcmp(g->consumed[i].zone, zid) == 0;
            if (!any) continue;
            if (!MapLoadZoneWithPlacements(m, g->res, zid, g)) continue;
            for (int i = 0; i < g->consumed_count; i++) {
                if (strcmp(g->consumed[i].zone, zid) != 0) continue;
                const Tile *t = MapGetTile(m, g->consumed[i].x,
                                           g->consumed[i].y);
                if (!t) continue;
                switch (t->interactive) {
                case INTERACT_TREASURE_CHEST: out->chests++;  break;
                case INTERACT_NAVMAP:         out->navmaps++; break;
                case INTERACT_ORB:            out->orbs++;    break;
                default: break;
                }
            }
        }
        free(m);
    }
    for (int i = 0; i < GAME_TOWNS; i++)
        if (g->towns[i].id[0] && g->towns[i].visited) out->towns++;
}

void demo_end(void) {
    DemoState *st = demo_state();
    memset(st, 0, sizeof *st);
}

bool demo_run(const DemoConfig *cfg, DemoResult *out) {
    if (!cfg || !out || cfg->seed_index < 0 || cfg->seed_index > 255 ||
        !cfg->pack_dir || !cfg->pack_dir[0])
        return false;
    memset(out, 0, sizeof *out);

    Pack *pack = pack_open(cfg->pack_dir);
    if (!pack) return false;
    pack_stack_push(pack);

    Resources *res = calloc(1, sizeof *res);
    if (!res || !resources_load(res, DEMO_MANIFEST)) {
        free(res);
        pack_stack_pop();
        return false;
    }
    Game *game = calloc(1, sizeof *game);
    Map *map = calloc(1, sizeof *map);
    Fog *fog = calloc(1, sizeof *fog);
    if (!game || !map || !fog) {
        free(fog); free(map); free(game);
        resources_free(res); free(res);
        pack_stack_pop();
        return false;
    }

    game->res = res;
    GameInitSeeded(game, DEMO_HERO_NAME, DEMO_HERO_CLASS, DEMO_HERO_DIFFICULTY,
                   NULL, cfg->seed_index);
    FogInit(fog);
    bool ok = MapLoadZoneWithPlacements(map, res, res->world.starting_zone, game);
    if (ok) {
        GameApplyTileMutations(game, map, game->position.zone);
        FogReveal(fog, map, game->position.x, game->position.y,
                  res->world.fog_sight);
        demo_begin(game, map, fog, res);
        int t = 0;
        while (demo_tick(game, map, fog, res) && ++t < DEMO_MAX_TICKS) {}
        demo_result(game, out);
        printf("[DEMO OVER] %s, %s: days=%d score=%d villains=%d artifacts=%d "
               "castles=%d digs=%d chests=%d navmaps=%d orbs=%d towns=%d "
               "ticks=%d\n",
               out->won ? "WON" : (out->out_of_days ? "LOST" : "STUCK"),
               demo_state()->end_reason[0] ? demo_state()->end_reason : "?",
               out->days_used, out->score, out->villains, out->artifacts,
               out->castles, out->searches, out->chests, out->navmaps,
               out->orbs, out->towns, out->ticks);
        demo_end();
    }

    pending_reset();
    free(fog);
    free(map);
    free(game);
    resources_free(res);
    free(res);
    pack_stack_pop();
    return ok;
}
