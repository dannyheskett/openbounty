// src/ai/ai_mission.c

#include "ai_mission.h"

#include <stdio.h>
#include <string.h>

#include "resources.h"
#include "tile.h"

const char *ai_mission_name(AiMission m) {
    switch (m) {
        case AI_MISSION_PLAY_ZONE:      return "play_zone";
        case AI_MISSION_GO_TO_DOCK:     return "go_to_dock";
        case AI_MISSION_RENT_BOAT:      return "rent_boat";
        case AI_MISSION_BOARD_BOAT:     return "board_boat";
        case AI_MISSION_SAIL_NEXT:      return "sail_next";
        case AI_MISSION_FIND_SCEPTER:   return "find_scepter";
        case AI_MISSION_SEARCH_SCEPTER: return "search_scepter";
        case AI_MISSION_DONE:           return "done";
    }
    return "?";
}

// True when the hero is currently standing on the tile of an
// INTERACT_TOWN. The town's id is filled into `town_id_out` (24 chars).
static bool hero_on_town(const Game *g, const Map *m, char *town_id_out,
                         size_t town_id_sz) {
    const Tile *t = MapGetTile(m, g->position.x, g->position.y);
    if (!t || t->interactive != INTERACT_TOWN) return false;
    if (town_id_out && town_id_sz > 0) {
        snprintf(town_id_out, town_id_sz, "%s", t->id);
    }
    return true;
}

// True iff the current zone has any dock-capable town reachable
// somewhere on the map. This is a static check on the zone definition,
// not a BFS — if the zone has no coastal town at all, GO_TO_DOCK will
// never resolve and we shouldn't pick it.
static bool zone_has_dock_town(const Game *g) {
    const ResZone *z = resources_zone_by_id(g->res, g->position.zone);
    if (!z) return false;
    for (int i = 0; i < z->town_count; i++) {
        const ResZoneTown *zt = &z->towns[i];
        const ResTown *rt = resources_town_by_id(g->res, zt->id);
        if (!rt) continue;
        if (rt->boat_x >= 0 && rt->boat_y >= 0) return true;
    }
    return false;
}

// True iff the current zone has neighbors in the navigate graph.
// Without neighbors, SAIL_NEXT can't possibly succeed.
static bool zone_has_neighbors(const Game *g) {
    const ResZone *z = resources_zone_by_id(g->res, g->position.zone);
    return (z && z->neighbor_count > 0);
}

// True iff the player's boat is parked in the current zone (i.e.
// reachable on foot for a board attempt).
static bool boat_parked_here(const Game *g) {
    return g->boat.has_boat &&
           g->boat.zone[0] &&
           strcmp(g->boat.zone, g->position.zone) == 0 &&
           g->boat.x >= 0 && g->boat.y >= 0;
}

AiMission ai_mission_update(AiMission current, const AiMissionCtx *ctx) {
    if (!ctx || !ctx->g) return current;
    const Game *g = ctx->g;

    if (g->stats.game_over) return AI_MISSION_DONE;

    switch (current) {

    case AI_MISSION_PLAY_ZONE: {
        // Stay in PLAY_ZONE while there's anything productive to do
        // on foot in this zone. Only leave when the zone is fully
        // exhausted AND we have somewhere else to go.
        if (!ctx->zone_exhausted) return AI_MISSION_PLAY_ZONE;

        // Zone is exhausted. Pick the next mission:
        //   - have boat parked here → BOARD_BOAT (cheapest exit)
        //   - have coastal town we can reach → GO_TO_DOCK
        //   - neither → DONE (stuck on this continent forever)
        if (boat_parked_here(g) && zone_has_neighbors(g)) {
            return AI_MISSION_BOARD_BOAT;
        }
        if (!g->boat.has_boat && zone_has_dock_town(g) &&
            zone_has_neighbors(g)) {
            return AI_MISSION_GO_TO_DOCK;
        }
        return AI_MISSION_DONE;
    }

    case AI_MISSION_GO_TO_DOCK: {
        // If we already have a boat (e.g. we walked through a town
        // that auto-rented somehow, or weekly state changed),
        // shortcut to boarding.
        if (boat_parked_here(g)) return AI_MISSION_BOARD_BOAT;

        // Standing on a town tile → switch to RENT_BOAT so the driver
        // invokes the town's BOAT row. The town view will open this
        // tick; RENT_BOAT consumes it next tick.
        char town_id[24];
        if (hero_on_town(g, ctx->m, town_id, sizeof town_id)) {
            // Only swap if the town actually has a dock. If not, walk
            // away (PLAY_ZONE will eventually re-route us).
            const ResTown *rt = resources_town_by_id(g->res, town_id);
            if (rt && rt->boat_x >= 0 && rt->boat_y >= 0) {
                return AI_MISSION_RENT_BOAT;
            }
        }

        // No dock-capable town in this zone at all → bail.
        if (!zone_has_dock_town(g)) return AI_MISSION_PLAY_ZONE;
        return AI_MISSION_GO_TO_DOCK;
    }

    case AI_MISSION_RENT_BOAT: {
        // Driver fires the BOAT row this frame. By next tick has_boat
        // will be true if the rent succeeded; if not (gold short), we
        // fall back to PLAY_ZONE rather than looping. Note: while the
        // VIEW_TOWN modal is still open we haven't yet handed back to
        // the driver — ai_clear_ui dismisses the view first.
        if (boat_parked_here(g)) return AI_MISSION_BOARD_BOAT;
        // No boat after the rent attempt → rent must have failed.
        // Drop back to PLAY_ZONE to earn more gold (commission ticks
        // every week-end).
        return AI_MISSION_PLAY_ZONE;
    }

    case AI_MISSION_BOARD_BOAT: {
        // travel_mode flips to TRAVEL_BOAT the moment we step onto
        // the boat tile. That's our trigger to switch to SAIL_NEXT.
        if (g->travel_mode == TRAVEL_BOAT) return AI_MISSION_SAIL_NEXT;
        // Lost the boat while walking to it (weekly upkeep) → back
        // to dock.
        if (!g->boat.has_boat) {
            if (zone_has_dock_town(g)) return AI_MISSION_GO_TO_DOCK;
            return AI_MISSION_PLAY_ZONE;
        }
        // Boat exists but parked in a different zone (only possible
        // if some prior shell action moved it) → re-evaluate.
        if (!boat_parked_here(g)) return AI_MISSION_PLAY_ZONE;
        return AI_MISSION_BOARD_BOAT;
    }

    case AI_MISSION_SAIL_NEXT: {
        // The driver fires INPUT_ACTION_NEW_CONTINENT and the
        // navigate-numeric prompt opens. After the player picks a
        // neighbor zone, GameSwitchZone runs and position.zone
        // changes.
        //
        // We MUST leave SAIL_NEXT the moment the zone changes, even
        // if we arrived on water. Otherwise the next tick re-fires
        // NEW_CONTINENT and we ping-pong between continents whose
        // hero_spawn is on water. PLAY_ZONE's tactical layer
        // handles drifting to shore.
        if (ctx->zone_changed) return AI_MISSION_PLAY_ZONE;
        if (g->travel_mode == TRAVEL_WALK) {
            // We somehow disembarked without switching zones (stuck
            // on the shore of the same zone). Re-evaluate.
            return AI_MISSION_PLAY_ZONE;
        }
        // ticks_since_sail watchdog: if we've sat in SAIL_NEXT for
        // many ticks (prompt opened but we ping-ponged inside the
        // prompt or something else broke), bail rather than wedging.
        if (ctx->ticks_since_sail > 60) return AI_MISSION_PLAY_ZONE;
        return AI_MISSION_SAIL_NEXT;
    }

    case AI_MISSION_FIND_SCEPTER:
    case AI_MISSION_SEARCH_SCEPTER:
        // Endgame missions not yet wired up — scepter pursuit is
        // task #7. The driver never sets these for now.
        return AI_MISSION_PLAY_ZONE;

    case AI_MISSION_DONE:
        return AI_MISSION_DONE;
    }
    return current;
}
