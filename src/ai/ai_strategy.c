// src/ai/ai_strategy.c

#include "ai_strategy.h"
#include "ai_nav.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "resources.h"
#include "tile.h"

static void set_label(AiGoal *out, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(out->label, sizeof out->label, fmt, ap);
    va_end(ap);
}

// Resolve castle's gate coords in current zone, or (-1,-1) if not here.
static bool castle_in_zone(const Game *g, const char *castle_id,
                           int *out_x, int *out_y) {
    const ResCastle *rc = resources_castle_by_id(g->res, castle_id);
    if (!rc) return false;
    if (strcmp(rc->zone, g->position.zone) != 0) return false;
    int gx = (rc->gate_x >= 0) ? rc->gate_x : rc->x;
    int gy = (rc->gate_y >= 0) ? rc->gate_y : rc->y + 1;
    *out_x = gx;
    *out_y = gy;
    return true;
}

// Lookup helpers for "is this tile already used up".
static bool tile_skip_visited_town(const Game *g, const Tile *t) {
    if (!t || t->interactive != INTERACT_TOWN) return false;
    if (!t->id[0]) return false;
    for (int i = 0; i < GAME_TOWNS; i++) {
        if (g->towns[i].id[0] && strcmp(g->towns[i].id, t->id) == 0
            && g->towns[i].visited) {
            return true;
        }
    }
    return false;
}

static bool tile_skip_owned_castle(const Game *g, const Tile *t) {
    if (!t || t->interactive != INTERACT_CASTLE_GATE) return false;
    if (!t->id[0]) return false;
    for (int i = 0; i < GAME_CASTLES; i++) {
        if (g->castles[i].id[0]
            && strcmp(g->castles[i].id, t->id) == 0
            && g->castles[i].owner_kind == CASTLE_OWNER_PLAYER) {
            return true;
        }
    }
    return false;
}

// Audience castles (King Maximus) are the win-condition for the
// scepter endgame. Stepping into one without the scepter just opens a
// home_castle view we can't usefully navigate, so skip them until the
// endgame strategy is wired up.
static bool tile_skip_audience_castle(const Game *g, const Tile *t) {
    if (!t || t->interactive != INTERACT_CASTLE_GATE) return false;
    if (!t->id[0]) return false;
    const ResCastle *rc = resources_castle_by_id(g->res, t->id);
    if (!rc) return false;
    return rc->special.flow[0] != '\0';
}

static bool is_dwelling(Interact i) {
    return i == INTERACT_DWELLING_PLAINS  || i == INTERACT_DWELLING_FOREST
        || i == INTERACT_DWELLING_HILLS   || i == INTERACT_DWELLING_DUNGEON;
}

// Skip dwellings whose state row exists and has count == 0 — they're
// drained for this week. Also skip dwellings we can't afford a single
// troop at (gold < recruit_cost or leadership cap = 0).
static bool tile_skip_drained_dwelling(const Game *g, const Tile *t,
                                       int x, int y) {
    if (!t || !is_dwelling(t->interactive)) return false;
    for (int i = 0; i < g->dwelling_count; i++) {
        const DwellingState *d = &g->dwellings[i];
        if (d->x != x || d->y != y) continue;
        if (strcmp(d->zone, g->position.zone) != 0) continue;
        if (d->count <= 0) return true;
        // Can we afford and accommodate at least one?
        const TroopDef *td = troop_by_id(d->troop_id);
        if (!td) return true;
        if (g->stats.gold < td->recruit_cost) return true;
        if (GameMaxRecruitable(g, td->id) < 1) return true;
        return false;
    }
    return false;
}

// Walk the map for a tile matching a given Interact kind. Picks the
// shortest BFS distance from the hero.
static bool find_nearest_interact_bfs(const Game *g, const Map *m,
                                      Interact kind, AiMoveMode mode,
                                      int *out_x, int *out_y, int *out_dist) {
    int best = -1;
    int best_x = -1, best_y = -1;
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            const Tile *t = MapGetTile(m, x, y);
            if (!t || t->interactive != kind) continue;
            if (tile_skip_visited_town(g, t))         continue;
            if (tile_skip_owned_castle(g, t))         continue;
            if (tile_skip_audience_castle(g, t))      continue;
            if (tile_skip_drained_dwelling(g, t, x, y)) continue;
            // Alcove: skip if we already know magic, otherwise visiting
            // just bounces off the "already learned" dialog every time
            // (the tile isn't consumed).
            if (t->interactive == INTERACT_ALCOVE && g->stats.knows_magic) {
                continue;
            }
            AiStep s = ai_path_step(m, mode,
                                    g->position.x, g->position.y,
                                    x, y, true);
            if (!s.ok) continue;
            if (best < 0 || s.dist < best) {
                best   = s.dist;
                best_x = x;
                best_y = y;
            }
        }
    }
    if (best < 0) return false;
    *out_x    = best_x;
    *out_y    = best_y;
    if (out_dist) *out_dist = best;
    return true;
}

// Find the nearest tile bordering unseen territory — i.e. a seen,
// walkable tile that has at least one unseen 4-neighbor. Walking there
// expands fog and exposes new interactive tiles.
static bool find_nearest_fog_frontier(const Game *g, const Map *m,
                                      const Fog *fog, AiMoveMode mode,
                                      int *out_x, int *out_y) {
    if (!fog) return false;
    int best = -1, bx = -1, by = -1;
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            if (!FogSeen(fog, x, y)) continue;
            // Frontier?
            bool has_unseen = false;
            static const int nx[4] = { 1,-1, 0, 0 };
            static const int ny[4] = { 0, 0, 1,-1 };
            for (int k = 0; k < 4; k++) {
                int ax = x + nx[k], ay = y + ny[k];
                if (ax < 0 || ay < 0 || ax >= m->width || ay >= m->height) continue;
                if (!FogSeen(fog, ax, ay)) { has_unseen = true; break; }
            }
            if (!has_unseen) continue;
            if (!ai_walkable(m, x, y, mode, false)) continue;
            // avoid_interact=true matches the driver's step planning,
            // so a frontier we say is reachable is actually reachable
            // by the same path the driver will take.
            AiStep s = ai_path_step(m, mode, g->position.x, g->position.y,
                                    x, y, true);
            if (!s.ok) continue;
            if (best < 0 || s.dist < best) {
                best = s.dist;
                bx = x; by = y;
            }
        }
    }
    if (best < 0) return false;
    *out_x = bx;
    *out_y = by;
    return true;
}

AiGoal ai_strategy_pick(const Game *g, const Map *m, const Fog *fog) {
    AiGoal out = { -1, -1, "", false };
    if (!g || !m) return out;
    AiMoveMode mode = ai_move_mode_for(g);

    // (0) Scepter target — the win condition. Honest progression
    // gate: a human player only learns the scepter's coordinates by
    // assembling the puzzle map, which requires capturing every
    // villain AND collecting every artifact. The AI mirrors that
    // gate here — even though it reads g->scepter directly, it only
    // ACTS on that knowledge after the same prerequisites are met.
    // Without this gate the AI cheats to the win on tick ~500 with
    // an empty army; with it, every win is one the human game would
    // also have allowed at the same state.
    if (GameVillainsCaught(g) >= 17 && GameArtifactsFound(g) >= 8 &&
        g->scepter.zone[0] &&
        strcmp(g->scepter.zone, g->position.zone) == 0 &&
        g->scepter.x >= 0 && g->scepter.y >= 0) {
        AiStep s = ai_path_step(m, mode, g->position.x, g->position.y,
                                g->scepter.x, g->scepter.y, true);
        if (s.ok) {
            out.gx = g->scepter.x; out.gy = g->scepter.y; out.ok = true;
            set_label(&out, "scepter@%d,%d", out.gx, out.gy);
            return out;
        }
    }

    // (1) Contract villain whose castle is known and lives in this zone.
    if (g->contract.active_id[0]) {
        for (int i = 0; i < GAME_CASTLES; i++) {
            const CastleRecord *cr = &g->castles[i];
            if (!cr->id[0]) continue;
            if (cr->owner_kind != CASTLE_OWNER_VILLAIN) continue;
            if (strcmp(cr->villain_id, g->contract.active_id) != 0) continue;
            if (!cr->known) continue;
            int gx, gy;
            if (castle_in_zone(g, cr->id, &gx, &gy)) {
                AiStep s = ai_path_step(m, mode, g->position.x, g->position.y,
                                        gx, gy, true);
                if (s.ok) {
                    out.gx = gx; out.gy = gy; out.ok = true;
                    set_label(&out, "contract:%s", cr->id);
                    return out;
                }
            }
        }
    }

    // (2) Nearest unvisited town — buy spells, get intel.
    {
        int gx, gy, d;
        if (find_nearest_interact_bfs(g, m, INTERACT_TOWN, mode, &gx, &gy, &d)) {
            out.gx = gx; out.gy = gy; out.ok = true;
            set_label(&out, "town@%d,%d", gx, gy);
            return out;
        }
    }

    // (3) Nearest castle gate that isn't home / current player castle.
    {
        int gx, gy, d;
        if (find_nearest_interact_bfs(g, m, INTERACT_CASTLE_GATE, mode,
                                      &gx, &gy, &d)) {
            out.gx = gx; out.gy = gy; out.ok = true;
            set_label(&out, "castle@%d,%d", gx, gy);
            return out;
        }
    }

    // (4) Nearest artifact, then chest, then dwelling. Telecaves
    // are excluded — they're persistent teleporters, and routing to
    // one just bounces us across the map without consuming the tile,
    // producing a perpetual loop.
    static const Interact pickup_kinds[] = {
        INTERACT_ARTIFACT, INTERACT_TREASURE_CHEST,
        INTERACT_DWELLING_PLAINS, INTERACT_DWELLING_FOREST,
        INTERACT_DWELLING_HILLS,  INTERACT_DWELLING_DUNGEON,
        INTERACT_ORB, INTERACT_NAVMAP, INTERACT_ALCOVE,
    };
    for (size_t i = 0; i < sizeof pickup_kinds / sizeof pickup_kinds[0]; i++) {
        int gx, gy, d;
        if (find_nearest_interact_bfs(g, m, pickup_kinds[i], mode,
                                      &gx, &gy, &d)) {
            out.gx = gx; out.gy = gy; out.ok = true;
            set_label(&out, "pickup:%d@%d,%d", (int)pickup_kinds[i], gx, gy);
            return out;
        }
    }

    // (5) Explore: walk to the nearest tile that borders unseen fog.
    // Stepping onto it expands the visible area and exposes new tiles.
    {
        int gx, gy;
        if (find_nearest_fog_frontier(g, m, fog, mode, &gx, &gy)) {
            out.gx = gx; out.gy = gy; out.ok = true;
            set_label(&out, "explore@%d,%d", gx, gy);
            return out;
        }
    }

    // No tactical goal in this zone — the mission layer will read
    // ok=false and may transition us to GO_TO_DOCK / BOARD_BOAT to
    // leave the continent. Don't fabricate a "corner" wander goal:
    // that produces purposeless movement that wedges the AI for many
    // ticks before stuck detection fires.
    return out;
}

bool ai_zone_exhausted(const Game *g, const Map *m, const Fog *fog) {
    // Bedrock predicate: PLAY_ZONE stays active as long as anything
    // in the strategy's tactical priorities is reachable. Implemented
    // by asking the strategy once and checking ok. Cheap to do —
    // a few BFS over a 64x64 grid.
    AiGoal goal = ai_strategy_pick(g, m, fog);
    return !goal.ok;
}
