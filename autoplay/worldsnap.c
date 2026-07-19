// autoplay/worldsnap.c -- atomic attempt snapshot/rollback (AP-030..AP-033).

#include "worldsnap.h"

#include "pending.h"
#include "spells_adventure.h"

// Calendar-death latch: set when a restore discards a world that died on the
// calendar; never cleared by a restore (it is outside Game). See worldsnap.h.
static bool s_calendar_dead = false;

void worldsnap_reset_calendar_dead(void) { s_calendar_dead = false; }
bool worldsnap_calendar_dead(void) { return s_calendar_dead; }

void worldsnap_capture(WorldSnapshot *snap, const Game *g, const Map *map,
                       const Fog *fog) {
    if (!snap || !g || !map || !fog) return;
    snap->game = *g;
    snap->map = *map;
    snap->fog = *fog;
    snap->rng = GameRngSnapshot();
    ledger_snap(&snap->ledger);
}

void worldsnap_restore(const WorldSnapshot *snap, Game *g, Map *map, Fog *fog) {
    if (!snap || !g || !map || !fog) return;
    // Latch BEFORE the overwrite: the world being discarded is the live one.
    // If it died on the calendar, the restore is about to resurrect the run
    // (positive days_left, game_over cleared) -- record the death so the planner
    // stops instead of continuing on a calendar a larger budget still has.
    // Narrow: latch only when the world being RESTORED TO (the committed
    // baseline) is itself out of days -- not when a rolled-back speculative
    // attempt merely drove its scratch copy to day 0.
    if (snap->game.stats.game_over && snap->game.stats.days_left == 0 &&
        !snap->game.stats.won)
        s_calendar_dead = true;
    *g = snap->game;
    *map = snap->map;
    *fog = snap->fog;
    GameRngRestore(snap->rng);
    ledger_unsnap(&snap->ledger);
    // A capture is always taken at FLOW_NONE; the pending flow scratch and the
    // adventure-spell UI continuations are process globals outside Game, so a
    // restore must reset them explicitly (AP-031, AP-033).
    pending_reset();
    spells_adventure_reset_ui();
}

// FNV-1a over the whole value state, minus Game's res pointer (shared,
// read-only, position-dependent). Test-facing (AP-032).
static uint32_t fnv1a(uint32_t h, const void *data, size_t n) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

uint32_t worldsnap_fingerprint(const Game *g, const Map *map, const Fog *fog) {
    uint32_t h = 2166136261u;
    if (!g || !map || !fog) return h;
    Game tmp = *g;
    tmp.res = NULL;   // exclude the shared pointer from the byte hash
    h = fnv1a(h, &tmp, sizeof tmp);
    h = fnv1a(h, map, sizeof *map);
    h = fnv1a(h, fog, sizeof *fog);
    return h;
}
