// engine/game_foes.c -- the foes on the map following the hero
// (GameFoesFollow).

#include "game.h"
#include "game_internal.h"
#include "map.h"
#include "adventure.h"
#include "ui_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Integer Euclidean distance: isqrt32(dx^2 + dy^2).
// Scaled x1000 internally so single-step differences (e.g. cardinal vs
// diagonal) don't round-collapse to equal integers and starve the picker
// of tie-breaking resolution. The absolute value doesn't matter -- only
// ordering does.
static unsigned foe_dist_sq(int x1, int y1, int x2, int y2) {
    int dx = x2 - x1;
    int dy = y2 - y1;
    return (unsigned)(dx * dx + dy * dy);
}

// Keep foes off the eight tiles surrounding a castle gate. This is a house
// rule, NOT a restoration: the original's 0x00 test does not prevent it,
// because the approach tile below a gate is grass in both. Without it a foe
// parks on the doorstep of the hero's own castle. See issue #22.
static bool adjacent_to_castle_gate(const Map *map, int x, int y) {
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            const Tile *n = MapGetTile(map, x + dx, y + dy);
            if (n && n->interactive == INTERACT_CASTLE_GATE) return true;
        }
    }
    return false;
}

static bool foe_can_stand(const Map *map, int x, int y) {
    if (!MapInBounds(map, x, y)) return false;
    const Tile *t = MapGetTile(map, x, y);
    if (!t) return false;
    if (t->blocks_foot) return false;
    // The original accepts a candidate tile only when its map byte is 0x00,
    // i.e. grass (OPENKB-SPEC.md:6234, foe_closest_offset at play.c:1738).
    // Deliberately NOT TerrainWalkable(), which also admits desert: desert is
    // a non-zero byte and impassable to foes, which is what makes it a refuge
    // for the hero. Hero movement is unaffected; adventure_walkable_on_foot
    // still crosses desert at 40 movement points per tile.
    if (t->terrain != TERRAIN_GRASS) return false;
    // Bridges have no terrain of their own: TerrainFromArt falls through to
    // GRASS for art it does not recognise, so the test above lets them past.
    // A bridge is a non-zero byte, so the original rejects it; without this a
    // foe walks over water and follows the hero across a barrier.
    if (t->is_bridge) return false;
    // Treat any stamped tile byte as an obstacle -- the foe can't
    // sit on a chest, castle gate, another foe, etc. Our INTERACT_NONE
    // check captures the same rule.
    if (t->interactive != INTERACT_NONE) return false;
    if (adjacent_to_castle_gate(map, x, y)) return false;
    return true;
}

// A zone event's tile (a vista such as Galliae's Temple of Ocean) is a
// landmark, not open grass: a foe standing there would hide it, and its
// leaving would repaint the cell as plain ground (MapClearInteractive).
static bool foe_on_event_tile(const Game *g, const char *zone, int x, int y) {
    const ResZone *z = (g && g->res) ? resources_zone_by_id(g->res, zone) : NULL;
    for (int k = 0; z && k < z->event_count; k++)
        if (z->events[k].x == x && z->events[k].y == y) return true;
    return false;
}

// Authoritative occupancy: does any LIVE foe other than `except_idx`, in `zone`,
// sit at (x,y)? Stamp-independent -- the map's INTERACT_FOE overlay can momentarily
// disagree with real foe positions, so the anti-stacking gate consults g->foes[]
// directly rather than trusting the tile byte.
static bool foe_occupies(const Game *g, const char *zone, int x, int y,
                         int except_idx) {
    for (int j = 0; j < g->foe_count; j++) {
        if (j == except_idx) continue;
        const FoeState *o = &g->foes[j];
        if (!o->alive) continue;
        if (o->x != x || o->y != y) continue;
        if (strcmp(o->zone, zone) != 0) continue;
        return true;
    }
    return false;
}

int GameFoesFollow(Game *g, Map *map) {
    if (!g || !map) return -1;
    int tx = g->position.last_x;
    int ty = g->position.last_y;
    int collided = -1;
    // STAMP RE-SYNC: every live in-zone foe NOT on the hero's current tile must
    // carry its INTERACT_FOE stamp before we read the map for standability below.
    // A foe that stepped onto the hero (collision path) is intentionally left
    // unstamped while the hero shares its tile; once the hero moves away nothing
    // else re-stamps it, leaving an unstamped "phantom" the occupancy proxy can't
    // see (another foe stacks on it) and the stamp-based combat trigger can't
    // engage. Re-establish the invariant here so foe_can_stand and the step-onto
    // combat trigger are both correct. (Skips the hero's tile: the collided foe
    // stays unstamped, preserving the `collided` contract below.)
    for (int i = 0; i < g->foe_count; i++) {
        const FoeState *f = &g->foes[i];
        if (!f->alive) continue;
        if (strcmp(f->zone, g->position.zone) != 0) continue;
        if (f->x == g->position.x && f->y == g->position.y) continue;
        MapStampFoe(map, f->x, f->y, f->placement_id);
    }
    // Iterate ALL foes -- friendly and hostile -- through foes_follow.
    // The friendly/hostile distinction only matters at attack time.
    for (int i = 0; i < g->foe_count; i++) {
        FoeState *f = &g->foes[i];
        if (!f->alive) continue;
        if (strcmp(f->zone, g->position.zone) != 0) continue;
        if (f->is_static) continue;   // fixed guardian: never moves
        // Range gate (OpenKB's play.c:823-829): foe must be within
        // GAME_FOE_FOLLOW_RANGE tiles on each axis of the hero's previous
        // position.
        int diff_x = f->x - tx; if (diff_x < 0) diff_x = -diff_x;
        int diff_y = f->y - ty; if (diff_y < 0) diff_y = -diff_y;
        if (diff_x > GAME_FOE_FOLLOW_RANGE || diff_y > GAME_FOE_FOLLOW_RANGE)
            continue;

        // Evaluate all 9 neighborhood cells (foe_closest_offset,
        // play.c:1738). The center is always eligible (foe can stand still).
        // Non-center obstacles get a sentinel distance, so any real cell
        // beats them. The hero's current tile is NOT excluded --
        // allows a foe to step onto the hero, which becomes the combat
        // trigger via the "stepped on a foe" check at game.c:6552.
        const unsigned SENTINEL = 0xFFFFFFFFu;
        unsigned best_dist = SENTINEL;
        int      best_x = f->x;
        int      best_y = f->y;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int nx = f->x + dx;
                int ny = f->y + dy;
                if (!MapInBounds(map, nx, ny)) continue;
                bool is_center = (dx == 0 && dy == 0);
                bool is_hero_tile = (nx == g->position.x &&
                                     ny == g->position.y);
                // A flying hero is untouchable (issue #12): the foe must not
                // step onto the hero's tile (no combat), nor chase the hero
                // across the water / trees / castle no-go tiles it is flying
                // over. Skip that tile entirely so only real, foe-standable
                // ground is ever considered. Gated to real play: the autoplay
                // oracle (oracle_mode) keeps the legacy behavior its search is
                // tuned against -- wandering-foe contact gates no objective, so
                // the winnability verdict is unchanged. The visible replay runs
                // with oracle_mode set, so it stays consistent with the resolve.
                if (is_hero_tile && g->character.mount == MOUNT_FLY &&
                    !g->oracle_mode)
                    continue;
                // Used ONLY for the anti-stacking exemption below, not for
                // walkability: a foe standing on the hero (the collision that
                // triggers combat) is not a stack, so a second foe is still
                // allowed to target that tile. Drops away in the boat, where
                // the hero sits on water no land foe can occupy anyway.
                bool hero_reachable = (is_hero_tile &&
                                       g->travel_mode != TRAVEL_BOAT);
                // `if (i || j)` gate: only non-center cells get the obstacle
                // penalty, matching the original's `if (i != 0 or j != 0)`.
                //
                // The hero's tile gets NO exemption here. foe_closest_offset
                // (OPENKB-SPEC.md:6234) tests the map byte of all eight
                // non-center cells without caring where the player is, so a
                // hero standing on any non-zero tile simply cannot be reached.
                // That is what makes desert a refuge rather than merely
                // impassable: foes neither cross it nor attack into it. The
                // same now holds for bridges, towns and the other interactive
                // tiles, and for the castle-gate approach.
                //
                // The hero's tile is not exempt: exempting it would let a foe
                // on adjacent grass reach a hero standing anywhere at all.
                if (!is_center && !foe_can_stand(map, nx, ny))
                    continue;
                if (!is_center && foe_on_event_tile(g, f->zone, nx, ny))
                    continue;
                // Anti-stacking, stamp-independent: never target a tile another
                // live foe already holds (two foes may never share a spot). The
                // hero's tile is exempt -- a foe stepping onto the hero is the
                // combat trigger, not a stack.
                if (!is_center && !hero_reachable &&
                    foe_occupies(g, f->zone, nx, ny, i))
                    continue;
                unsigned d = foe_dist_sq(nx, ny, tx, ty);
                if (d < best_dist) {
                    best_dist = d;
                    best_x = nx;
                    best_y = ny;
                }
            }
        }
        if (best_x == f->x && best_y == f->y) continue;

        // Move: clear the old tile -- foe stamp only (never a pickup the
        // hero-tile exception let the foe stand on).
        MapClearFoeStamp(map, f->x, f->y);
        f->x = best_x;
        f->y = best_y;

        // If the foe stepped onto the hero, surface that to the caller and
        // do NOT stamp the foe tile (the hero is on it). Caller will fire
        // the attack/recruit flow against this foe.
        if (best_x == g->position.x && best_y == g->position.y) {
            collided = i;
            continue;
        }

        // Otherwise stamp the new tile with the foe.
        Tile *dst = &MAP_TILE(map, best_x, best_y);
        dst->interactive = INTERACT_FOE;
        TileSetId(map, dst, f->placement_id);
        TileSetArt(map, dst, map->army_art[0] ? map->army_art : "wandering_army");
    }
    return collided;
}
