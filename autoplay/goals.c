// autoplay/goals.c
//
// PlanStep model implementation. Phase 2: chest goals.

#include "goals.h"

#include <stdio.h>
#include <string.h>

const char *planstep_kind_str(PlanKind k) {
    switch (k) {
    case STEP_CHEST:           return "chest";
    case STEP_ARTIFACT:        return "artifact";
    case STEP_NAVMAP:          return "navmap";
    case STEP_ORB:             return "orb";
    case STEP_ALCOVE:          return "alcove";
    case STEP_SIEGE_WEAPONS:   return "siege-weapons";
    case STEP_MONSTER_CASTLE:  return "monster-castle";
    case STEP_VILLAIN:         return "villain";
    case STEP_SCEPTER:         return "scepter";
    case STEP_FOE:             return "foe";
    case STEP_RECRUIT_HOME:    return "recruit-home";
    case STEP_RECRUIT_DWELLING:return "recruit-dwelling";
    case STEP_RENT_BOAT:       return "rent-boat";
    case STEP_TAKE_CONTRACT:   return "take-contract";
    case STEP_CLEAR_FOE:       return "clear-foe";
    case STEP_BUY_SPELLS:      return "buy-spells";
    case STEP_TRAVEL_ZONE:     return "travel-zone";
    case STEP_RECOMPOSE_ARMY:  return "recompose-army";
    case STEP_WAIT_WEEKS:      return "wait-weeks";
    case STEP_RECOMPOSE_FLY:   return "recompose-fly";
    case STEP_CAST_SPELL:      return "cast-spell";
    default:                   return "?";
    }
}

// Is tile (x,y) in zone `zone_id` recorded in g->consumed[]? This is the
// canonical "object picked up" signal — every pickup path calls
// GameAddConsumed.
static bool tile_consumed(const Game *g, const char *zone_id, int x, int y) {
    if (!g || !zone_id) return false;
    for (int i = 0; i < g->consumed_count; i++) {
        const TileMutation *tm = &g->consumed[i];
        if (tm->x == x && tm->y == y && strcmp(tm->zone, zone_id) == 0) {
            return true;
        }
    }
    return false;
}

// Append one goal to the set (bounds-checked). Returns the new goal or NULL.
static PlanStep *step_push(PlanStepSet *set) {
    if (set->count >= STEP_MAX) return NULL;
    PlanStep *gl = &set->steps[set->count++];
    memset(gl, 0, sizeof *gl);
    return gl;
}

int plansteps_enumerate_chests(PlanStepSet *set, const Game *g, const Map *map,
                           int zone_index) {
    if (!set || !g || !g->res || !map) return 0;
    if (zone_index < 0 || zone_index >= g->res->zone_count) return 0;
    const ResZone *z = &g->res->zones[zone_index];

    int added = 0;
    for (int i = 0; i < z->chest_count; i++) {
        const ResZoneChest *c = &z->chests[i];
        // The zone's chests[] is the SALT PLACEHOLDER list (REQ-231): salting
        // rewrites a fraction of these slots into dwellings, artifacts,
        // navmaps, orbs, telecaves, or foes. Only a slot whose LIVE tile is
        // still an actual treasure chest is a chest objective; the rewritten
        // ones are enumerated by their true kind (artifact/navmap/orb from
        // g->placements[]) or are not objectives at all (dwellings/telecaves).
        // Emitting a STEP_CHEST for a rewritten slot creates a phantom goal
        // that can never complete (wrong done-predicate, the tile bounces) —
        // so gate on the salted map, not the static placeholder list.
        const Tile *t = MapGetTile(map, c->x, c->y);
        if (!t || t->interactive != INTERACT_TREASURE_CHEST) continue;

        PlanStep *gl = step_push(set);
        if (!gl) break;
        gl->kind = STEP_CHEST;
        gl->tile_bound = true;
        gl->arrival = ARRIVE_CONSUME;
        gl->target.x = c->x;
        gl->target.y = c->y;
        gl->zone_index = zone_index;
        // Most chests are anonymous (ResZoneChest.id empty) -> handle stays "",
        // and the tile is the physical key. Use the id if the pack
        // names this chest.
        if (c->id[0]) snprintf(gl->handle, sizeof gl->handle, "%.23s", c->id);
        snprintf(gl->label, sizeof gl->label, "chest at (%d,%d)", c->x, c->y);
        added++;
    }
    return added;
}

int plansteps_enumerate_noncombat(PlanStepSet *set, const Game *g, const Map *map,
                              int zone_index) {
    if (!set || !g || !g->res || !map) return 0;
    if (zone_index < 0 || zone_index >= g->res->zone_count) return 0;
    const ResZone *z = &g->res->zones[zone_index];
    const char *zone_id = z->id;

    int added = plansteps_enumerate_chests(set, g, map, zone_index);

    // Artifacts / navmap / orb: salted into g->placements[] with coords + kind.
    for (int i = 0; i < g->placement_count; i++) {
        const SaltedPlacement *pl = &g->placements[i];
        if (strcmp(pl->zone, zone_id) != 0) continue;
        PlanKind k;
        const char *name;
        switch (pl->kind) {
        case INTERACT_ARTIFACT: k = STEP_ARTIFACT; name = "artifact"; break;
        case INTERACT_NAVMAP:   k = STEP_NAVMAP;   name = "navmap";   break;
        case INTERACT_ORB:      k = STEP_ORB;      name = "orb";      break;
        default: continue;
        }
        PlanStep *gl = step_push(set);
        if (!gl) break;
        gl->kind = k;
        gl->tile_bound = true;
        gl->arrival = ARRIVE_CONSUME;
        gl->target.x = pl->x;
        gl->target.y = pl->y;
        gl->zone_index = zone_index;
        // Placement carries a stable id (e.g. "navmap_0", "orb_0", artifact id).
        if (pl->id[0]) snprintf(gl->handle, sizeof gl->handle, "%.23s", pl->id);
        snprintf(gl->label, sizeof gl->label, "%s%s%s at (%d,%d)", name,
                 pl->id[0] ? " " : "", pl->id[0] ? pl->id : "", pl->x, pl->y);
        added++;
    }

    // Alcove (magic): a fixed zone feature at res->zones[].magic_alcove_x/y.
    if (z->magic_alcove_x >= 0 && z->magic_alcove_y >= 0) {
        PlanStep *gl = step_push(set);
        if (gl) {
            gl->kind = STEP_ALCOVE;
            gl->tile_bound = true;
            gl->arrival = ARRIVE_ALCOVE;
            gl->target.x = z->magic_alcove_x;
            gl->target.y = z->magic_alcove_y;
            gl->zone_index = zone_index;
            snprintf(gl->label, sizeof gl->label, "alcove (magic) at (%d,%d)",
                     z->magic_alcove_x, z->magic_alcove_y);
            added++;
        }
    }

    // Siege weapons: bought at a town. Not tile-bound — the planner resolves
    // the target to the nearest reachable town at execution time.
    {
        PlanStep *gl = step_push(set);
        if (gl) {
            gl->kind = STEP_SIEGE_WEAPONS;
            gl->tile_bound = false;
            gl->arrival = ARRIVE_BUY_SIEGE;
            gl->zone_index = zone_index;
            snprintf(gl->label, sizeof gl->label, "buy siege weapons (at a town)");
            added++;
        }
    }

    return added;
}

int plansteps_enumerate_combat(PlanStepSet *set, const Game *g, int zone_index) {
    if (!set || !g || !g->res) return 0;
    if (zone_index < 0 || zone_index >= g->res->zone_count) return 0;
    const char *zone_id = g->res->zones[zone_index].id;

    int added = 0;
    for (int i = 0; i < GAME_CASTLES; i++) {
        const CastleRecord *cr = &g->castles[i];
        if (!cr->id[0]) continue;
        // Castle coords + zone come from the Resources catalog (CastleRecord
        // carries no coords). Only this zone's castles.
        const ResCastle *rc = resources_castle_by_id(g->res, cr->id);
        if (!rc || strcmp(rc->zone, zone_id) != 0) continue;

        if (cr->owner_kind == CASTLE_OWNER_MONSTERS) {
            PlanStep *gl = step_push(set);
            if (!gl) break;
            gl->kind = STEP_MONSTER_CASTLE;
            gl->tile_bound = true;
            gl->arrival = ARRIVE_SIEGE;
            gl->target.x = rc->x; gl->target.y = rc->y;   // the gate tile
            gl->zone_index = zone_index;
            snprintf(gl->handle, sizeof gl->handle, "%s", cr->id);
            snprintf(gl->label, sizeof gl->label, "monster castle %s (%d,%d)",
                     cr->id, rc->x, rc->y);
            added++;
        } else if (cr->owner_kind == CASTLE_OWNER_VILLAIN && cr->villain_id[0]) {
            PlanStep *gl = step_push(set);
            if (!gl) break;
            gl->kind = STEP_VILLAIN;
            gl->tile_bound = true;
            gl->arrival = ARRIVE_SIEGE;
            gl->target.x = rc->x; gl->target.y = rc->y;
            gl->zone_index = zone_index;
            // id is the VILLAIN id (for done-detection via villains_caught),
            // but we also need the castle id to navigate/siege; store villain
            // id here and resolve the castle by villain at execution.
            snprintf(gl->handle, sizeof gl->handle, "%s", cr->villain_id);
            // Bounded widths keep GCC from flagging truncation; real villain
            // (<=13) and castle (<=10) ids fit well within these caps, so the
            // formatted label is byte-identical to an unbounded "%s %s".
            snprintf(gl->label, sizeof gl->label,
                     "villain %.14s @ castle %.13s", cr->villain_id, cr->id);
            added++;
        }
    }
    return added;
}

int plansteps_enumerate_foes(PlanStepSet *set, const Game *g, int zone_index) {
    if (!set || !g || !g->res) return 0;
    if (zone_index < 0 || zone_index >= g->res->zone_count) return 0;
    const char *zone_id = g->res->zones[zone_index].id;

    int added = 0;
    // Data-driven: walk the live g->foes[] array (populated at GameInit from
    // game.json wandering_armies[]) — NO hardcoded foe list. One STEP_FOE per
    // HOSTILE, ALIVE foe in this zone. Same live-hostile predicate as the driver's
    // seek_destroy_target, so enumeration and the driver agree on what a foe is.
    for (int i = 0; i < g->foe_count; i++) {
        const FoeState *fs = &g->foes[i];
        if (!fs->alive || fs->friendly) continue;          // hostiles only
        if (strcmp(fs->zone, zone_id) != 0) continue;      // this zone only

        PlanStep *gl = step_push(set);
        if (!gl) break;
        gl->kind = STEP_FOE;
        gl->tile_bound = true;
        // The foe tile IS the target, reached by a walk-ONTO step: the generic
        // tile-bound path (steps 4-8) routes to it and step 6b fights it the
        // instant the next step lands on the foe. ARRIVE_CONSUME keeps it out of
        // the STEP_CLEAR_FOE / ARRIVE_SIEGE branches and the bouncer set.
        gl->arrival = ARRIVE_CONSUME;
        gl->target.x = fs->x;
        gl->target.y = fs->y;
        gl->zone_index = zone_index;
        // placement_id (the pack's stable wandering-army id) is the handle used by
        // the done-predicate (foe coords drift; the id does not).
        snprintf(gl->handle, sizeof gl->handle, "%.23s", fs->placement_id);
        snprintf(gl->label, sizeof gl->label, "foe %s at (%d,%d)",
                 fs->placement_id, fs->x, fs->y);
        added++;
    }
    return added;
}

int plansteps_enumerate_scepter(PlanStepSet *set, const Game *g) {
    if (!set || !g || !g->res) return 0;
    if (!g->scepter.zone[0]) return 0;
    int zi = -1;
    for (int i = 0; i < g->res->zone_count; i++)
        if (strcmp(g->res->zones[i].id, g->scepter.zone) == 0) { zi = i; break; }
    if (zi < 0) return 0;
    PlanStep *gl = step_push(set);
    if (!gl) return 0;
    gl->kind = STEP_SCEPTER;
    gl->tile_bound = true;
    gl->arrival = ARRIVE_SCEPTER;
    gl->target.x = g->scepter.x;
    gl->target.y = g->scepter.y;
    gl->zone_index = zi;
    // Zone-qualify the label like every other multi-zone objective ("[zone] …")
    // so the checklist/decision log read uniformly and the all-zone universe
    // invariant (every label bracketed) holds.
    snprintf(gl->label, sizeof gl->label, "[%s] scepter (%d,%d)",
             g->scepter.zone, g->scepter.x, g->scepter.y);
    return 1;
}

bool planstep_is_done(const PlanStep *gl, const Game *g) {
    if (!gl || !g || !g->res) return false;
    if (gl->zone_index < 0 || gl->zone_index >= g->res->zone_count) return false;
    const char *zone_id = g->res->zones[gl->zone_index].id;
    switch (gl->kind) {
    case STEP_CHEST:
    case STEP_ARTIFACT:
    case STEP_NAVMAP:
    case STEP_ORB:
        // Consumable: done once its tile is in consumed[] (uniform signal).
        return tile_consumed(g, zone_id, gl->target.x, gl->target.y);
    case STEP_ALCOVE:
        // Magic learned (the alcove visit sets knows_magic + consumes the tile).
        return g->stats.knows_magic;
    case STEP_SIEGE_WEAPONS:
        return g->stats.siege_weapons != 0;
    case STEP_MONSTER_CASTLE: {
        // Done = player owns it and has garrisoned it.
        const CastleRecord *cr = GameFindCastleConst(g, gl->handle);
        if (!cr || cr->owner_kind != CASTLE_OWNER_PLAYER) return false;
        for (int s = 0; s < GAME_ARMY_SLOTS; s++)
            if (cr->garrison[s].id[0] && cr->garrison[s].count > 0) return true;
        return false;   // owned but not yet garrisoned
    }
    case STEP_VILLAIN: {
        // Done = the villain that held this castle is caught.
        const VillainDef *v = gl->handle[0] ? villain_by_id(gl->handle) : NULL;
        if (!v) return false;
        return g->contract.villains_caught[v->index];
    }
    case STEP_SCEPTER:
        // Done = the scepter has been recovered (the search on its tile won).
        return g->stats.won;
    case STEP_FOE: {
        // Done = the hostile foe is gone: purged from the live array or marked
        // defeated (foe->alive cleared on a combat win in flow_resolve.c and
        // flows.c). Keyed by the STABLE placement_id, NOT by (x,y): a live
        // wandering foe's coords drift each tick (GameFoesFollow). Placement
        // ids are NOT unique across zones (the numbering restarts in every
        // zone), and the engine's GameFindFoe scopes its match to the HERO's
        // current zone — so resolve the foe here by the OBJECTIVE's zone plus
        // id, independent of where the hero happens to be standing.
        if (!gl->handle[0]) return true;
        for (int i = 0; i < g->foe_count; i++) {
            const FoeState *fs = &g->foes[i];
            if (strcmp(fs->placement_id, gl->handle) != 0) continue;
            if (strcmp(fs->zone, zone_id) != 0) continue;
            return !fs->alive;
        }
        return true;   // purged from the live array = gone
    }
    // ENABLING primitives: "done" = the enabling effect is in place, so
    // the planner releases the constrained step once it has achieved it.
    case STEP_TAKE_CONTRACT:
        // Done when the wanted villain's contract is active (or any contract, if
        // no specific villain was named).
        return gl->handle[0] ? (strcmp(g->contract.active_id, gl->handle) == 0)
                             : (g->contract.active_id[0] != '\0');
    case STEP_RENT_BOAT:
        return g->boat.has_boat;
    case STEP_TRAVEL_ZONE:
        // Done = the hero stands in the destination zone (handle = zone id).
        return gl->handle[0] && strcmp(g->position.zone, gl->handle) == 0;
    case STEP_RECRUIT_HOME:
    case STEP_RECRUIT_DWELLING:
        // A recruit step is a one-shot action with no persistent "done" flag of
        // its own; the executor marks it complete by releasing p->cur after
        // buying. For the planstep_is_done predicate (used to skip already-met
        // steps) treat it as not-pre-satisfiable: it must be performed.
        return false;
    default:
        return false;
    }
}

void planstepset_progress(const PlanStepSet *set, const Game *g,
                      int *out_done, int *out_total) {
    // Only OBJECTIVE primitives count toward the milestone checklist; enabling
    // primitives (recruit/boat/contract) are means, not milestones.
    int done = 0, total = 0;
    if (set) {
        for (int i = 0; i < set->count; i++) {
            if (!planstep_is_objective(set->steps[i].kind)) continue;
            total++;
            if (planstep_is_done(&set->steps[i], g)) done++;
        }
    }
    if (out_done)  *out_done  = done;
    if (out_total) *out_total = total;
}

int planstepset_next(const PlanStepSet *set, const Game *g) {
    if (!set) return -1;
    // Phase 2: enumeration order (chests only). The safe->power->combat tiers
    // slot in here as more kinds arrive.
    for (int i = 0; i < set->count; i++) {
        if (!planstep_is_done(&set->steps[i], g)) return i;
    }
    return -1;
}
