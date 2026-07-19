#include "adventure.h"
#include "map.h"
#include "tables.h"
#include "resources.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Walkability rules ported from OpenKB's game.c (around line 6507).
//
// Walk mode:
//   - Grass / desert: walkable (desert zeroes day-step budget via GameOnStep).
//   - Interactive tiles (castle_gate, town, chest, sign, dwelling, foe,
//     artifact): walkable -- stepping on them fires the handler.
//   - Bridges: walkable.
//   - Water: blocked (unless boarding the boat -- caller handles that).
//   - Castle walls, forest, mountain: blocked (blocks_foot).
//
// Boat mode:
//   - Water / bridge: walkable.
//   - Land: walkable (counts as disembark -- caller detects the terrain
//     change and leaves the boat on the previous water tile).
bool adventure_walkable_on_foot(const Tile *t) {
    if (!t) return false;
    if (t->interactive != INTERACT_NONE) return true;
    if (t->is_bridge) return true;
    if (t->blocks_foot) return false;
    return TerrainWalkable(t->terrain);
}

bool adventure_walkable_in_boat(const Tile *t) {
    if (!t) return false;
    if (t->is_bridge) return true;
    if (t->terrain == TERRAIN_WATER) return true;
    // Stepping onto non-water = disembark (caller handles boat placement).
    return adventure_walkable_on_foot(t);
}

// Fly mode: IS_INTERACTIVE check is bypassed (game.c:6522 --
// `if (IS_INTERACTIVE(m) && game->mount != KBMOUNT_FLY)`). Interactive
// tiles still aren't triggered when stepped on mid-flight. Here we
// allow any tile whose terrain is non-blocking (water, grass, desert,
// forest, mountains) and skip the blocks_foot gate.
bool adventure_walkable_in_flight(const Tile *t) {
    if (!t) return false;
    // Allow all terrain types, including water, since fly bypasses the
    // usual ground restrictions. The only hard-stop is the map bounds,
    // which MapGetTile enforces (returns NULL).
    return true;
}

// Resolve a tile's artifact id to its continent-local slot index (0 or 1).
// Tries the artifact catalog first (placement ids are catalog ids like
// "ring_of_heroism"); falls back to parsing trailing digits of legacy
// "artifact_N"-style ids.
static int artifact_local_from_id(const char *id) {
    if (!id || !id[0]) return -1;
    const ArtifactDef *a = artifact_by_id(id);
    if (a) return a->local_idx;
    const char *p = strrchr(id, '_');
    if (!p) return -1;
    char *end = NULL;
    long n = strtol(p + 1, &end, 10);
    if (end == p + 1) return -1;
    return (int)n;
}

InteractResult adventure_handle_interact(const Tile *t, const char *zone) {
    InteractResult r = { 0 };
    r.artifact_idx = -1;
    r.town_boat_x = -1;
    r.town_boat_y = -1;
    if (!t || t->interactive == INTERACT_NONE) return r;

    if (t->interactive == INTERACT_SIGN) {
        // sign format (game.c:3060): header is empty; body reads
        //   A sign reads:
        //
        //   "Title
        //    body line
        //    body line"
        const Resources *res = resources_current();
        char body[TILE_SIGN_TITLE_LEN + TILE_SIGN_BODY_LEN + 32];
        if (res && t->sign_body[0]) {
            ResTemplateVar vars[] = {
                { "TITLE", t->sign_title },
                { "BODY",  t->sign_body },
            };
            resources_format_template(body, sizeof body,
                                      res->banners.signpost_with_body,
                                      vars, 2);
        } else if (res) {
            ResTemplateVar vars[] = { { "TITLE", t->sign_title } };
            resources_format_template(body, sizeof body,
                                      res->banners.signpost_title_only,
                                      vars, 1);
        } else if (t->sign_body[0]) {
            snprintf(body, sizeof(body),
                     "A sign reads:\n\n\"%s\n%s\"",
                     t->sign_title, t->sign_body);
        } else {
            snprintf(body, sizeof(body),
                     "A sign reads:\n\n\"%s\"",
                     t->sign_title);
        }
        // Carry the sign text out in the result; the caller (step.c, which has a
        // Game*) raises it via player_io_message so it flows through the uniform
        // queue . adventure_handle_interact stays Game-free.
        r.dialog_header[0] = '\0';
        snprintf(r.dialog_body, sizeof r.dialog_body, "%s", body);
        r.opened_dialog = true;
        return r;
    }

    if (t->interactive == INTERACT_TOWN) {
        // Hand control to step.c, which opens VIEW_TOWN. We bounce back so
        // the hero ends up on the tile they entered from (on town exit
        // the hero's current position swaps with last_x/last_y).
        r.entered_town = true;
        int n = 0;
        while (n + 1 < (int)sizeof(r.town_id) && t->id[n]) {
            r.town_id[n] = t->id[n];
            n++;
        }
        r.town_id[n] = '\0';
        r.town_boat_x = t->boat_spawn_x;
        r.town_boat_y = t->boat_spawn_y;
        r.bounce_back = true;
        return r;
    }

    if (t->interactive == INTERACT_CASTLE_GATE) {
        // Surface the castle id to step.c so it can run the castle visit flow.
        r.opened_castle = true;
        int n = 0;
        while (n + 1 < (int)sizeof(r.castle_id) && t->id[n]) {
            r.castle_id[n] = t->id[n];
            n++;
        }
        r.castle_id[n] = '\0';
        r.bounce_back = true;
        return r;
    }

    if (t->interactive == INTERACT_ARTIFACT) {
        int local = artifact_local_from_id(t->id);
        int idx   = artifact_index_for_tile(zone, local);
        const ArtifactDef *a = artifact_by_index(idx);
        if (a) {
            // Surface the index; step.c claims it and mutates the tile so
            // the artifact disappears from the map .
            r.artifact_idx = idx;
        }
        // Unrecognized artifact: no dialog. The artifact pickup either
        // succeeds (above) or silently drops the player back; engine
        // doesn't surface a fallback message.
        return r;
    }

    if (t->interactive == INTERACT_TREASURE_CHEST) {
        // Surface the chest to step.c for the random loot roll + tile
        // mutation . We don't bounce back -- the hero
        // stays on the now-cleared tile.
        r.opened_chest = true;
        return r;
    }

    if (t->interactive == INTERACT_ORB) {
        // Surface the orb to step.c for fog reveal and dialog.
        // We don't bounce back -- the hero stays on the now-cleared tile.
        r.opened_orb = true;
        return r;
    }

    if (t->interactive == INTERACT_DWELLING_PLAINS ||
        t->interactive == INTERACT_DWELLING_FOREST ||
        t->interactive == INTERACT_DWELLING_HILLS ||
        t->interactive == INTERACT_DWELLING_DUNGEON) {
        // visit_dwelling: recruit troops via text-input modal.
        // Until we have a numeric-input prompt we surface the tile so
        // step.c can open the recruit prompt.
        r.opened_dwelling = true;
        r.dwelling_kind   = t->interactive;
        r.bounce_back     = true;
        return r;
    }

    if (t->interactive == INTERACT_ALCOVE) {
        // Archmage's alcove .
        // step.c handles the offer/accept/already-knows flow.
        r.opened_alcove = true;
        r.bounce_back   = true;
        return r;
    }

    if (t->interactive == INTERACT_TELECAVE) {
        // : teleport to the paired cave.
        // We carry the placement id so step.c can resolve the pairing.
        r.opened_telecave = true;
        int n = 0;
        while (n + 1 < (int)sizeof(r.telecave_id) && t->id[n]) {
            r.telecave_id[n] = t->id[n]; n++;
        }
        r.telecave_id[n] = '\0';
        return r;
    }

    if (t->interactive == INTERACT_NAVMAP) {
        // stepping on a navmap unlocks sailing to the target
        // continent. We surface the tile so step.c can set the flag,
        // consume the tile, and show a "Map of X" dialog.
        r.opened_navmap = true;
        int n = 0;
        while (n + 1 < (int)sizeof(r.navmap_id) && t->id[n]) {
            r.navmap_id[n] = t->id[n]; n++;
        }
        r.navmap_id[n] = '\0';
        return r;
    }

    if (t->interactive == INTERACT_FOE) {
        // accept_foe (OpenKB's game.c:3574): one
        // tile type. step.c looks up the foe in g->foes[] and decides
        // friendly (recruit) vs hostile (combat) by the foe's `friendly`
        // flag, then sets bounce_back appropriately.
        r.opened_foe = true;
        int n = 0;
        while (n + 1 < (int)sizeof(r.foe_id) && t->id[n]) {
            r.foe_id[n] = t->id[n]; n++;
        }
        r.foe_id[n] = '\0';
        return r;
    }

    // Unrecognized interactive tile -- every tile in a normal game has a
    // known type, so this branch is unreachable in practice. Return
    // empty without surfacing any dialog (no port-authored fallback).
    return r;
}
