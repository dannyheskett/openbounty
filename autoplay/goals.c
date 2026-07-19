// autoplay/goals.c -- objective enumeration + the done predicate (AP-040..042).

#include "goals.h"

#include <stdio.h>
#include <string.h>

#include "tables.h"
#include "tile.h"

static void set_label(PlanStep *s, const char *prefix, const char *what) {
    snprintf(s->label, sizeof s->label, "%s:%s", prefix, what);
}

static bool s_step_overflow;   // set when STEP_MAX drops an objective

static PlanStep *add_step(PlanStepSet *set, PlanKind kind, int zi,
                          int x, int y, const char *handle) {
    if (set->count >= STEP_MAX) {
        // A truncated objective universe must never produce a verdict: the
        // caller fails the enumeration (and the run) instead of reporting
        // CLEARED over a world it silently did not cover.
        s_step_overflow = true;
        return NULL;
    }
    PlanStep *s = &set->steps[set->count++];
    memset(s, 0, sizeof *s);
    s->kind = kind;
    s->zone_index = zi;
    s->x = x;
    s->y = y;
    if (handle) {
        size_t n = 0;
        while (n + 1 < sizeof s->handle && handle[n]) {
            s->handle[n] = handle[n]; n++;
        }
        s->handle[n] = '\0';
    }
    return s;
}

// Consumable tiles (chests, artifacts, navmaps, orbs) + the alcove, read from
// the loaded zone map so salt-time placements are included (AP-040).
static void enumerate_noncombat(const Game *g, const Map *map, int zi,
                                PlanStepSet *out) {
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            const Tile *t = MapGetTile(map, x, y);
            if (!t) continue;
            PlanStep *s = NULL;
            // The format bounds its own worst case so -Wformat-truncation
            // is provably clean at every -O level: %.31s caps the zone id
            // (RES_ID_LEN-1; ids are NUL-terminated below that), and the
            // & (MAP_MAX_W-1) masks are identities (loop bounds are the map
            // dims, capped at 64) that hand the compiler the 0..63 range.
            // Worst case 31+1+2+1+2+NUL = 38 fits coord; 39 (not 40) keeps
            // set_label's longest "%s:%s" region ("artifact:" into label[48])
            // provably un-truncated from coord's array size alone.
            char coord[39];
            snprintf(coord, sizeof coord, "%.31s:%d,%d", g->res->zones[zi].id,
                     x & (MAP_MAX_W - 1), y & (MAP_MAX_H - 1));
            switch (t->interactive) {
            case INTERACT_TREASURE_CHEST:
                s = add_step(out, STEP_CHEST, zi, x, y, t->id);
                if (s) set_label(s, "chest", coord);
                break;
            case INTERACT_ARTIFACT:
                s = add_step(out, STEP_ARTIFACT, zi, x, y, t->id);
                if (s) set_label(s, "artifact", coord);
                break;
            case INTERACT_NAVMAP:
                s = add_step(out, STEP_NAVMAP, zi, x, y, t->id);
                if (s) set_label(s, "navmap", coord);
                break;
            case INTERACT_ORB:
                s = add_step(out, STEP_ORB, zi, x, y, t->id);
                if (s) set_label(s, "orb", coord);
                break;
            case INTERACT_ALCOVE:
                s = add_step(out, STEP_ALCOVE, zi, x, y, t->id);
                if (s) set_label(s, "alcove", coord);
                break;
            default:
                break;
            }
        }
    }
}

// Monster + villain castles (AP-040). The gate tile is the target; the King's
// (special) castle is never an objective.
static void enumerate_combat(const Game *g, PlanStepSet *out) {
    for (int i = 0; i < GAME_CASTLES; i++) {
        const CastleRecord *cr = &g->castles[i];
        if (!cr->id[0]) continue;
        if (cr->owner_kind == CASTLE_OWNER_SPECIAL) continue;
        const ResCastle *rc = resources_castle_by_id(g->res, cr->id);
        if (!rc) continue;
        int zi = -1;
        for (int z = 0; z < g->res->zone_count; z++) {
            if (strcmp(g->res->zones[z].id, rc->zone) == 0) { zi = z; break; }
        }
        if (zi < 0) continue;
        // The interactive CASTLE_GATE tile is the castle's (x, y); gate_x/y is
        // the Castle Gate SPELL's landing tile below it (REQ-228).
        if (cr->owner_kind == CASTLE_OWNER_VILLAIN && cr->villain_id[0]) {
            PlanStep *s = add_step(out, STEP_VILLAIN, zi,
                                   rc->x, rc->y, cr->villain_id);
            if (s) set_label(s, "villain", cr->villain_id);
        } else if (cr->owner_kind == CASTLE_OWNER_MONSTERS ||
                   cr->owner_kind == CASTLE_OWNER_VILLAIN) {
            PlanStep *s = add_step(out, STEP_MONSTER_CASTLE, zi,
                                   rc->x, rc->y, cr->id);
            if (s) set_label(s, "castle", cr->id);
        } else if (cr->owner_kind == CASTLE_OWNER_PLAYER) {
            // Already owned at enumeration time (never true at boot; kept so a
            // re-enumeration on a live world stays total).
            PlanStep *s = add_step(out, STEP_MONSTER_CASTLE, zi,
                                   rc->x, rc->y, cr->id);
            if (s) set_label(s, "castle", cr->id);
        }
    }
}

// Hostile wandering foes (AP-040). Friendly foes are recruit opportunities,
// not objectives.
static void enumerate_foes(const Game *g, PlanStepSet *out) {
    for (int i = 0; i < g->foe_count; i++) {
        const FoeState *f = &g->foes[i];
        if (!f->alive || f->friendly || !f->placement_id[0]) continue;
        int zi = -1;
        for (int z = 0; z < g->res->zone_count; z++) {
            if (strcmp(g->res->zones[z].id, f->zone) == 0) { zi = z; break; }
        }
        if (zi < 0) continue;
        PlanStep *s = add_step(out, STEP_FOE, zi, f->x, f->y, f->placement_id);
        if (s) set_label(s, "foe", f->placement_id);
    }
}

static void enumerate_scepter(const Game *g, PlanStepSet *out) {
    int zi = -1;
    for (int z = 0; z < g->res->zone_count; z++) {
        if (strcmp(g->res->zones[z].id, g->scepter.zone) == 0) { zi = z; break; }
    }
    if (zi < 0) zi = 0;
    PlanStep *s = add_step(out, STEP_SCEPTER, zi, g->scepter.x, g->scepter.y,
                           "scepter");
    if (s) set_label(s, "scepter", g->scepter.zone);
}

bool plansteps_enumerate(const Game *g, Map *scratch, PlanStepSet *out) {
    if (!g || !scratch || !out) return false;
    out->count = 0;
    s_step_overflow = false;
    // Every zone in the pack: zone_count <= GAME_CONTINENTS is an engine
    // load contract (GameInit fails loudly past capacity).
    for (int zi = 0; zi < g->res->zone_count; zi++) {
        if (!MapLoadZoneWithPlacements(scratch, g->res,
                                       g->res->zones[zi].id, g))
            return false;
        GameApplyTileMutations(g, scratch, g->res->zones[zi].id);
        enumerate_noncombat(g, scratch, zi, out);
    }
    enumerate_combat(g, out);
    enumerate_foes(g, out);
    enumerate_scepter(g, out);   // added last (AP-040)
    if (s_step_overflow) {
        printf("[PLANNER] objective universe exceeds STEP_MAX (%d); "
               "cannot evaluate this pack\n", STEP_MAX);
        return false;
    }
    return true;
}

const FoeState *plan_find_foe(const Game *g, const char *placement_id,
                              int zone_index) {
    if (!g || !placement_id || !placement_id[0]) return NULL;
    if (zone_index < 0 || zone_index >= g->res->zone_count) return NULL;
    const char *zone = g->res->zones[zone_index].id;
    for (int i = 0; i < g->foe_count; i++) {
        if (strcmp(g->foes[i].placement_id, placement_id) != 0) continue;
        if (strcmp(g->foes[i].zone, zone) != 0) continue;
        return &g->foes[i];
    }
    return NULL;
}

static bool tile_consumed(const Game *g, int zi, int x, int y) {
    const char *zone = g->res->zones[zi].id;
    for (int i = 0; i < g->consumed_count; i++) {
        if (g->consumed[i].x == x && g->consumed[i].y == y &&
            strcmp(g->consumed[i].zone, zone) == 0)
            return true;
    }
    return false;
}

bool planstep_is_done(const Game *g, const PlanStep *step) {
    if (!g || !step) return false;
    switch (step->kind) {
    case STEP_CHEST:
    case STEP_ARTIFACT:
    case STEP_NAVMAP:
    case STEP_ORB:
        return tile_consumed(g, step->zone_index, step->x, step->y);
    case STEP_ALCOVE:
        return g->stats.knows_magic;
    case STEP_SIEGE_WEAPONS:
        return g->stats.siege_weapons != 0;
    case STEP_MONSTER_CASTLE: {
        const CastleRecord *cr = GameFindCastleConst(g, step->handle);
        return cr && cr->owner_kind == CASTLE_OWNER_PLAYER;
    }
    case STEP_VILLAIN: {
        const VillainDef *v = villain_by_id(step->handle);
        return v && v->index >= 0 && v->index < CAT_VILLAINS_MAX &&
               g->contract.villains_caught[v->index];
    }
    case STEP_FOE: {
        // Placement ids are NOT unique across zones (the numbering restarts
        // per zone) -- match id + zone, never id alone (AP-042).
        const FoeState *f = plan_find_foe(g, step->handle, step->zone_index);
        return !f || !f->alive;
    }
    case STEP_SCEPTER:
        return g->stats.won;
    }
    return false;
}

const char *plan_kind_name(PlanKind k) {
    switch (k) {
    case STEP_CHEST:          return "chest";
    case STEP_ARTIFACT:       return "artifact";
    case STEP_NAVMAP:         return "navmap";
    case STEP_ORB:            return "orb";
    case STEP_ALCOVE:         return "alcove";
    case STEP_SIEGE_WEAPONS:  return "siege-weapons";
    case STEP_MONSTER_CASTLE: return "castle";
    case STEP_VILLAIN:        return "villain";
    case STEP_FOE:            return "foe";
    case STEP_SCEPTER:        return "scepter";
    }
    return "?";
}
