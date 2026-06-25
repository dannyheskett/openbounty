#include "state_serialize.h"
#include "pack.h"
#include "game.h"
#include "combat.h"
#include "map.h"
#include "fog.h"
#include "tile.h"
#include "tables.h"
#include "ui_host.h"   // dialog_*, prompt_*, views_active (snapshot reads)
#include "adventure.h"
#include "resources.h"
#include "savegame.h"   // SAVE_VERSION
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ----- Enum <-> string helpers ----------------------------------------------
// Lifted from savegame.c. Centralized here so the two consumers (file
// save / in-memory trace) share one definition.

static const char *difficulty_id(Difficulty d) {
    switch (d) {
        case DIFFICULTY_NORMAL:     return "normal";
        case DIFFICULTY_HARD:       return "hard";
        case DIFFICULTY_IMPOSSIBLE: return "impossible";
        case DIFFICULTY_EASY: default: return "easy";
    }
}
static const char *mount_id(Mount m) {
    switch (m) {
        case MOUNT_SAIL: return "sail";
        case MOUNT_FLY:  return "fly";
        case MOUNT_RIDE: default: return "ride";
    }
}
static const char *castle_owner_id(CastleOwnerKind k) {
    switch (k) {
        case CASTLE_OWNER_MONSTERS: return "monsters";
        case CASTLE_OWNER_VILLAIN:  return "villain";
        case CASTLE_OWNER_SPECIAL:  return "special";
        case CASTLE_OWNER_PLAYER: default: return "player";
    }
}

// Fog row encoding: ceil(width/4) hex chars per row, MSB = leftmost tile.
static char *encode_fog_row(const Fog *fog, int row, int width) {
    int nibbles = (width + 3) / 4;
    char *out = (char *)malloc(nibbles + 1);
    if (!out) return NULL;
    for (int n = 0; n < nibbles; n++) {
        int v = 0;
        for (int b = 0; b < 4; b++) {
            int x = n * 4 + b;
            if (x < width && FogSeen(fog, x, row)) {
                v |= (1 << (3 - b));
            }
        }
        out[n] = (v < 10) ? ('0' + v) : ('a' + (v - 10));
    }
    out[nibbles] = '\0';
    return out;
}

// Unit array (troops + counts).
static cJSON *build_unit_array(const Unit *arr, int n) {
    cJSON *out = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        if (!arr[i].id[0] || arr[i].count == 0) continue;
        cJSON *u = cJSON_CreateObject();
        cJSON_AddStringToObject(u, "troop", arr[i].id);
        cJSON_AddNumberToObject(u, "count", arr[i].count);
        cJSON_AddItemToArray(out, u);
    }
    return out;
}

// ----- Tile window (harness-only) -------------------------------------------
// 5x5 tile window centered on the hero, mirrors  CLEVEL viewport.

#define WIN_RADIUS 2
#define WIN_SIZE   (WIN_RADIUS * 2 + 1)

static const char *terrain_short(Terrain t) {
    switch (t) {
        case TERRAIN_GRASS:    return "grass";
        case TERRAIN_FOREST:   return "forest";
        case TERRAIN_MOUNTAIN: return "mountain";
        case TERRAIN_WATER:    return "water";
        case TERRAIN_DESERT:   return "desert";
        default:               return "?";
    }
}

static cJSON *build_tile_window(const Game *g, const Map *map, const Fog *fog) {
    cJSON *root = cJSON_CreateObject();
    if (!map) {
        cJSON_AddNullToObject(root, "window");
        return root;
    }
    int hx = g ? g->position.x : 0;
    int hy = g ? g->position.y : 0;
    int x0 = hx - WIN_RADIUS;
    int y0 = hy - WIN_RADIUS;

    cJSON *origin = cJSON_CreateArray();
    cJSON_AddItemToArray(origin, cJSON_CreateNumber(x0));
    cJSON_AddItemToArray(origin, cJSON_CreateNumber(y0));
    cJSON_AddItemToObject(root, "origin", origin);

    // Pick the right walkability predicate based on the hero's current
    // travel mode. The harness consumer reads `walkable` to decide
    // whether `key:right` would actually move the hero — far more useful
    // than guessing from raw terrain + blocks_foot.
    bool walking = (g && g->travel_mode == TRAVEL_WALK);
    bool flying  = (g && g->character.mount == MOUNT_FLY);

    cJSON *win = cJSON_CreateArray();
    for (int dy = 0; dy < WIN_SIZE; dy++) {
        cJSON *row = cJSON_CreateArray();
        for (int dx = 0; dx < WIN_SIZE; dx++) {
            int tx = x0 + dx, ty = y0 + dy;
            const Tile *t = MapGetTile(map, tx, ty);
            if (!t) {
                cJSON_AddItemToArray(row, cJSON_CreateNull());
                continue;
            }
            cJSON *cell = cJSON_CreateObject();
            cJSON_AddStringToObject(cell, "t", terrain_short(t->terrain));
            if (t->interactive != INTERACT_NONE) {
                cJSON_AddStringToObject(cell, "i",
                                        InteractToString(t->interactive));
            }
            if (t->blocks_foot) cJSON_AddNumberToObject(cell, "b", 1);
            if (t->id[0]) cJSON_AddStringToObject(cell, "id", t->id);
            if (t->art[0]) cJSON_AddStringToObject(cell, "art", t->art);
            if (t->is_bridge) cJSON_AddNumberToObject(cell, "br", 1);

            // Resolved "would the hero step here right now?" predicate.
            // Mirrors the check in main.c::AdventureTryStep.
            bool walkable;
            if (flying)      walkable = adventure_walkable_in_flight(t);
            else if (walking) walkable = adventure_walkable_on_foot(t);
            else              walkable = adventure_walkable_in_boat(t);
            // Treat the boat tile as walkable (boarding) — but only when the
            // boat is in the hero's CURRENT zone. A boat left behind in another
            // zone by a gate spell (boat.zone != position.zone) must not make a
            // coord-matching tile here look boardable. Mirrors step.c boarding.
            if (g && g->boat.has_boat &&
                tx == g->boat.x && ty == g->boat.y &&
                (g->boat.zone[0] == '\0' ||
                 strcmp(g->boat.zone, g->position.zone) == 0)) walkable = true;
            cJSON_AddBoolToObject(cell, "walk", walkable);

            cJSON_AddItemToArray(row, cell);
        }
        cJSON_AddItemToArray(win, row);
    }
    cJSON_AddItemToObject(root, "window", win);

    cJSON *fogarr = cJSON_CreateArray();
    for (int dy = 0; dy < WIN_SIZE; dy++) {
        cJSON *row = cJSON_CreateArray();
        for (int dx = 0; dx < WIN_SIZE; dx++) {
            int tx = x0 + dx, ty = y0 + dy;
            int seen = (fog && tx >= 0 && ty >= 0 && FogSeen(fog, tx, ty)) ? 1 : 0;
            cJSON_AddItemToArray(row, cJSON_CreateNumber(seen));
        }
        cJSON_AddItemToArray(fogarr, row);
    }
    cJSON_AddItemToObject(root, "fog", fogarr);
    return root;
}

// ----- Combat substate ------------------------------------------------------

static cJSON *build_combat(const Combat *c) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "side", c->side);
    cJSON_AddNumberToObject(root, "unit_id", c->unit_id);
    cJSON_AddNumberToObject(root, "turn", c->turn);
    cJSON_AddNumberToObject(root, "phase", c->phase);
    cJSON_AddBoolToObject(root, "first_kill_seen", c->first_kill_seen);
    cJSON_AddNumberToObject(root, "stacks_destroyed", c->stacks_destroyed);
    cJSON_AddStringToObject(root, "banner", c->banner);
    cJSON_AddNumberToObject(root, "result", c->result);
    cJSON_AddBoolToObject(root, "picker_active", c->picker_active);
    cJSON *cur = cJSON_CreateArray();
    cJSON_AddItemToArray(cur, cJSON_CreateNumber(c->cursor_x));
    cJSON_AddItemToArray(cur, cJSON_CreateNumber(c->cursor_y));
    cJSON_AddItemToObject(root, "cursor", cur);

    cJSON *units = cJSON_CreateArray();
    for (int sd = 0; sd < COMBAT_SIDES; sd++) {
        cJSON *side = cJSON_CreateArray();
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            const CombatUnit *u = &c->units[sd][i];
            if (u->troop_idx < 0) {
                cJSON_AddItemToArray(side, cJSON_CreateNull());
                continue;
            }
            const TroopDef *td = troop_by_index(u->troop_idx);
            cJSON *uo = cJSON_CreateObject();
            cJSON_AddStringToObject(uo, "id", td ? td->id : "");
            cJSON_AddNumberToObject(uo, "count", u->count);
            cJSON_AddNumberToObject(uo, "x", u->x);
            cJSON_AddNumberToObject(uo, "y", u->y);
            cJSON_AddBoolToObject(uo, "acted", u->acted);
            cJSON_AddBoolToObject(uo, "frozen", u->frozen);
            cJSON_AddBoolToObject(uo, "ooc", u->out_of_control);
            cJSON_AddNumberToObject(uo, "hit_flash", u->hit_flash);
            cJSON_AddNumberToObject(uo, "shots", u->shots);
            cJSON_AddNumberToObject(uo, "moves", u->moves);
            cJSON_AddItemToArray(side, uo);
        }
        cJSON_AddItemToArray(units, side);
    }
    cJSON_AddItemToObject(root, "units", units);
    return root;
}

// ----- Top-level snapshot builder -------------------------------------------

cJSON *state_build_snapshot(const Game *g,
                            const Combat *combat,
                            const Map *map,
                            const Fog *fog,
                            const char *trigger,
                            const char *snap_path,
                            uint64_t seq,
                            uint64_t ms) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", SAVE_VERSION);

    if (trigger && trigger[0]) {
        cJSON_AddStringToObject(root, "trigger", trigger);
    }
    if (snap_path && snap_path[0]) {
        cJSON_AddStringToObject(root, "snap_path", snap_path);
    }
    if (seq) {
        cJSON_AddNumberToObject(root, "seq", (double)seq);
    }
    if (ms) {
        cJSON_AddNumberToObject(root, "ms", (double)ms);
    }

    if (!g) {
        cJSON_AddStringToObject(root, "mode", "unattached");
        return root;
    }

    cJSON_AddStringToObject(root, "mode", combat ? "combat" : "adventure");
    cJSON_AddNumberToObject(root, "seed", (double)g->seed);

    // Tag the save with the active pack id so the loader can refuse a
    // cross-pack load (where troop / spell / villain ids from the
    // other pack would resolve to bogus catalog entries here).
    // pack_hash is the FNV1a-64 of the whole pack zip -- advisory only;
    // nothing currently gates on it.
    if (g->res && g->res->pack_id[0]) {
        cJSON_AddStringToObject(root, "pack_id", g->res->pack_id);
    }
    {
        const char *h = pack_hash(pack_stack_top());
        if (h && h[0]) cJSON_AddStringToObject(root, "pack_hash", h);
    }

    // ---- character ----
    {
        cJSON *c = cJSON_CreateObject();
        cJSON_AddStringToObject(c, "name", g->character.name);
        cJSON_AddStringToObject(c, "class", g->character.cls.id);
        cJSON_AddNumberToObject(c, "rank_index", g->character.cls.rank_index);
        cJSON_AddStringToObject(c, "rank_title", g->character.cls.rank_title);
        cJSON_AddStringToObject(c, "difficulty",
                                difficulty_id(g->character.difficulty));
        cJSON_AddStringToObject(c, "mount", mount_id(g->character.mount));
        cJSON_AddItemToObject(root, "character", c);
    }

    // ---- stats ----
    {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddNumberToObject(s, "gold", g->stats.gold);
        cJSON_AddNumberToObject(s, "commission_weekly", g->stats.commission_weekly);
        cJSON_AddNumberToObject(s, "leadership_base", g->stats.leadership_base);
        cJSON_AddNumberToObject(s, "leadership_current", g->stats.leadership_current);
        cJSON_AddNumberToObject(s, "followers_killed", g->stats.followers_killed);
        cJSON_AddNumberToObject(s, "score", g->stats.score);
        cJSON_AddNumberToObject(s, "spell_power", g->stats.spell_power);
        cJSON_AddNumberToObject(s, "max_spells", g->stats.max_spells);
        cJSON_AddBoolToObject  (s, "knows_magic", g->stats.knows_magic);
        cJSON_AddNumberToObject(s, "siege_weapons", g->stats.siege_weapons);
        cJSON_AddNumberToObject(s, "time_stop", g->stats.time_stop);
        cJSON_AddNumberToObject(s, "steps_left_today", g->stats.steps_left_today);
        cJSON_AddNumberToObject(s, "days_left", g->stats.days_left);
        cJSON_AddBoolToObject  (s, "game_over", g->stats.game_over);
        cJSON_AddBoolToObject  (s, "won", g->stats.won);
        cJSON_AddNumberToObject(s, "last_commission", g->stats.last_commission);
        cJSON *opts = cJSON_CreateArray();
        for (int i = 0; i < 8; i++) {
            cJSON_AddItemToArray(opts, cJSON_CreateNumber(g->stats.options[i]));
        }
        cJSON_AddItemToObject(s, "options", opts);
        cJSON_AddItemToObject(root, "stats", s);
    }

    // ---- position ----
    {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "zone", g->position.zone);
        cJSON_AddNumberToObject(p, "x", g->position.x);
        cJSON_AddNumberToObject(p, "y", g->position.y);
        cJSON_AddNumberToObject(p, "last_x", g->position.last_x);
        cJSON_AddNumberToObject(p, "last_y", g->position.last_y);
        cJSON_AddBoolToObject  (p, "facing_left", g->position.facing_left);
        cJSON_AddStringToObject(p, "travel_mode",
            g->travel_mode == TRAVEL_BOAT ? "boat" : "walk");
        cJSON_AddBoolToObject  (p, "hud_visible", g->hud_visible);
        cJSON_AddItemToObject(root, "position", p);
    }

    // ---- army ----
    {
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (!g->army[i].id[0] || g->army[i].count == 0) continue;
            cJSON *u = cJSON_CreateObject();
            cJSON_AddNumberToObject(u, "slot", i);
            cJSON_AddStringToObject(u, "troop", g->army[i].id);
            cJSON_AddNumberToObject(u, "count", g->army[i].count);
            cJSON_AddItemToArray(arr, u);
        }
        cJSON_AddItemToObject(root, "army", arr);
    }

    // ---- spells ----
    {
        cJSON *sp = cJSON_CreateObject();
        for (int i = 0; i < spells_count() && i < 14; i++) {
            const SpellDef *sd = spell_by_index(i);
            if (!sd) continue;
            cJSON_AddNumberToObject(sp, sd->id, g->spells.counts[i]);
        }
        cJSON_AddItemToObject(root, "spells", sp);
    }

    // ---- contract ----
    {
        cJSON *c = cJSON_CreateObject();
        cJSON_AddStringToObject(c, "active", g->contract.active_id);
        cJSON *cycle = cJSON_CreateArray();
        for (int i = 0; i < 5; i++) {
            cJSON_AddItemToArray(cycle, cJSON_CreateString(g->contract.cycle[i]));
        }
        cJSON_AddItemToObject(c, "cycle", cycle);
        cJSON_AddNumberToObject(c, "last_contract", g->contract.last_contract);
        cJSON_AddNumberToObject(c, "max_contract",  g->contract.max_contract);
        cJSON *caught = cJSON_CreateArray();
        for (int i = 0; i < 17; i++) {
            if (g->contract.villains_caught[i]) {
                const VillainDef *v = villain_by_index(i);
                if (v) cJSON_AddItemToArray(caught, cJSON_CreateString(v->id));
            }
        }
        cJSON_AddItemToObject(c, "villains_caught", caught);
        cJSON_AddItemToObject(root, "contract", c);
    }

    // ---- artifacts ----
    {
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < 8; i++) {
            if (g->artifacts.found[i]) {
                const ArtifactDef *a = artifact_by_index(i);
                if (a) cJSON_AddItemToArray(arr, cJSON_CreateString(a->id));
            }
        }
        cJSON *aobj = cJSON_CreateObject();
        cJSON_AddItemToObject(aobj, "found", arr);
        cJSON_AddItemToObject(root, "artifacts", aobj);
    }

    // ---- world ----
    {
        cJSON *w = cJSON_CreateObject();
        cJSON *disc = cJSON_CreateArray();
        cJSON *orbs = cJSON_CreateArray();
        int nz = (g->res ? g->res->zone_count : 0);
        if (nz > GAME_CONTINENTS) nz = GAME_CONTINENTS;
        for (int i = 0; i < nz; i++) {
            const ResZone *z = &g->res->zones[i];
            if (g->world.zones_discovered[i])
                cJSON_AddItemToArray(disc, cJSON_CreateString(z->id));
            if (g->world.orbs_found[i])
                cJSON_AddItemToArray(orbs, cJSON_CreateString(z->id));
        }
        cJSON_AddItemToObject(w, "zones_discovered", disc);
        cJSON_AddItemToObject(w, "orbs_found", orbs);
        // Puzzle reveal state is derived from g->contract.villains_caught[]
        // and g->artifacts.found[], not stored separately. Older save
        // files carried a "puzzle_revealed" array here; we drop it on
        // write and ignore it on read.
        cJSON_AddItemToObject(root, "world", w);
    }

    // ---- boat ----
    {
        cJSON *b = cJSON_CreateObject();
        cJSON_AddBoolToObject  (b, "has_boat", g->boat.has_boat);
        cJSON_AddNumberToObject(b, "x", g->boat.x);
        cJSON_AddNumberToObject(b, "y", g->boat.y);
        cJSON_AddStringToObject(b, "zone", g->boat.zone);
        cJSON_AddItemToObject(root, "boat", b);
    }

    // ---- towns ----
    {
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < GAME_TOWNS; i++) {
            if (!g->towns[i].id[0]) continue;
            cJSON *t = cJSON_CreateObject();
            cJSON_AddStringToObject(t, "id", g->towns[i].id);
            cJSON_AddBoolToObject  (t, "visited", g->towns[i].visited);
            if (g->towns[i].spell_for_sale[0])
                cJSON_AddStringToObject(t, "spell_for_sale",
                                        g->towns[i].spell_for_sale);
            const ResTown *rt = resources_town_by_id(g->res, g->towns[i].id);
            if (rt) {
                cJSON_AddStringToObject(t, "zone", rt->zone);
                cJSON_AddNumberToObject(t, "x", rt->x);
                cJSON_AddNumberToObject(t, "y", rt->y);
                cJSON_AddNumberToObject(t, "gate_x", rt->gate_x);
                cJSON_AddNumberToObject(t, "gate_y", rt->gate_y);
            }
            cJSON_AddItemToArray(arr, t);
        }
        cJSON_AddItemToObject(root, "towns", arr);
    }

    // ---- castles ----
    {
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < GAME_CASTLES; i++) {
            if (!g->castles[i].id[0]) continue;
            cJSON *c = cJSON_CreateObject();
            cJSON_AddStringToObject(c, "id", g->castles[i].id);
            cJSON_AddBoolToObject  (c, "visited", g->castles[i].visited);
            cJSON_AddBoolToObject  (c, "known",   g->castles[i].known);
            cJSON_AddStringToObject(c, "owner",
                castle_owner_id(g->castles[i].owner_kind));
            if (g->castles[i].villain_id[0])
                cJSON_AddStringToObject(c, "villain", g->castles[i].villain_id);
            // Pull tile coords from the resources catalog so harness
            // drivers can navigate to a castle without parsing the
            // full asset JSON themselves.
            const ResCastle *rc =
                resources_castle_by_id(g->res, g->castles[i].id);
            if (rc) {
                cJSON_AddStringToObject(c, "zone", rc->zone);
                cJSON_AddNumberToObject(c, "x", rc->x);
                cJSON_AddNumberToObject(c, "y", rc->y);
                cJSON_AddNumberToObject(c, "gate_x", rc->gate_x);
                cJSON_AddNumberToObject(c, "gate_y", rc->gate_y);
            }
            cJSON_AddItemToObject(c, "garrison",
                build_unit_array(g->castles[i].garrison, GAME_ARMY_SLOTS));
            cJSON_AddItemToArray(arr, c);
        }
        cJSON_AddItemToObject(root, "castles", arr);
    }

    // ---- scepter ----
    {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "zone", g->scepter.zone);
        cJSON_AddNumberToObject(s, "x", g->scepter.x);
        cJSON_AddNumberToObject(s, "y", g->scepter.y);
        cJSON_AddItemToObject(root, "scepter", s);
    }

    // ---- consumed tiles ----
    {
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < g->consumed_count; i++) {
            cJSON *m = cJSON_CreateObject();
            cJSON_AddStringToObject(m, "zone", g->consumed[i].zone);
            cJSON_AddNumberToObject(m, "x", g->consumed[i].x);
            cJSON_AddNumberToObject(m, "y", g->consumed[i].y);
            cJSON_AddItemToArray(arr, m);
        }
        cJSON_AddItemToObject(root, "consumed", arr);
    }

    // ---- placements (salted) ----
    {
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < g->placement_count; i++) {
            const SaltedPlacement *p = &g->placements[i];
            cJSON *m = cJSON_CreateObject();
            cJSON_AddStringToObject(m, "zone", p->zone);
            cJSON_AddNumberToObject(m, "x", p->x);
            cJSON_AddNumberToObject(m, "y", p->y);
            cJSON_AddStringToObject(m, "kind",
                InteractToString((Interact)p->kind));
            cJSON_AddStringToObject(m, "id", p->id);
            cJSON_AddItemToArray(arr, m);
        }
        cJSON_AddItemToObject(root, "placements", arr);
    }

    // ---- foes ----
    {
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < g->foe_count; i++) {
            const FoeState *f = &g->foes[i];
            cJSON *m = cJSON_CreateObject();
            cJSON_AddStringToObject(m, "zone", f->zone);
            cJSON_AddNumberToObject(m, "x", f->x);
            cJSON_AddNumberToObject(m, "y", f->y);
            cJSON_AddStringToObject(m, "id", f->placement_id);
            cJSON_AddBoolToObject  (m, "alive", f->alive);
            cJSON_AddBoolToObject  (m, "friendly", f->friendly);
            cJSON *gar = cJSON_CreateArray();
            for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
                cJSON *u = cJSON_CreateObject();
                cJSON_AddStringToObject(u, "id", f->garrison[s].id);
                cJSON_AddNumberToObject(u, "count", f->garrison[s].count);
                cJSON_AddItemToArray(gar, u);
            }
            cJSON_AddItemToObject(m, "garrison", gar);
            cJSON_AddItemToArray(arr, m);
        }
        cJSON_AddItemToObject(root, "foes", arr);
    }

    // ---- map_state (fog per continent, hex-per-row) ----
    // keeps fog per continent. We snapshot every discovered zone
    // into world.continent_fog[]; the *active* zone's live fog wins over
    // its snapshot (which may be stale from before recent moves).
    if (map && fog && g && g->res) {
        cJSON *mstate = cJSON_CreateObject();
        for (int zi = 0; zi < g->res->zone_count && zi < GAME_CONTINENTS; zi++) {
            if (!g->world.zones_discovered[zi]) continue;
            const ResZone *rz = &g->res->zones[zi];
            const Fog *src = &g->world.continent_fog[zi];
            int w = rz->width;
            int h = rz->height;
            // Use the live fog (and live map dims) for the active zone.
            bool is_active = (map->name[0] && strcmp(rz->id, map->name) == 0);
            if (is_active) {
                src = fog;
                w = map->width;
                h = map->height;
            }
            cJSON *cobj = cJSON_CreateObject();
            cJSON *rows = cJSON_CreateArray();
            for (int y = 0; y < h; y++) {
                char *row = encode_fog_row(src, y, w);
                if (row) {
                    cJSON_AddItemToArray(rows, cJSON_CreateString(row));
                    free(row);
                }
            }
            cJSON_AddItemToObject(cobj, "fog", rows);
            cJSON_AddItemToObject(mstate, rz->id, cobj);
        }
        cJSON_AddItemToObject(root, "map_state", mstate);
    }

    // ---- HARNESS-ONLY FIELDS BELOW (additive; savegame loader ignores) ----

    // ---- 5x5 tile window ----
    if (map) {
        cJSON_AddItemToObject(root, "tiles", build_tile_window(g, map, fog));
    }

    // ---- combat substate ----
    if (combat) {
        cJSON_AddItemToObject(root, "combat", build_combat(combat));
    }

    // ---- dialog ----
    if (dialog_is_active()) {
        cJSON *d = cJSON_CreateObject();
        const char *h = dialog_header_text();
        const char *b = dialog_body_text();
        cJSON_AddStringToObject(d, "title", h ? h : "");
        cJSON_AddStringToObject(d, "body",  b ? b : "");
        cJSON_AddItemToObject(root, "dialog", d);
    }

    // ---- prompt (yes/no, numeric picker, A/B, text input) ----
    if (prompt_is_active()) {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "kind", prompt_kind_str());
        const char *ph = prompt_header_text();
        const char *pb = prompt_body_text();
        cJSON_AddStringToObject(p, "title", ph ? ph : "");
        cJSON_AddStringToObject(p, "body",  pb ? pb : "");
        cJSON_AddItemToObject(root, "prompt", p);
    }

    // ---- fast-quit y/n in the status bar (Ctrl+Q) ----
    // This isn't routed through prompt.c — it's a separate status-bar
    // modal that consumes input until y/n. Expose it as a synthetic
    // prompt so the harness consumer knows to send y or n.
    {
        extern bool main_fast_quit_active(void);
        if (main_fast_quit_active()) {
            cJSON *p = cJSON_CreateObject();
            cJSON_AddStringToObject(p, "kind", "yes_no");
            cJSON_AddStringToObject(p, "title", "fast_quit");
            cJSON_AddStringToObject(p, "body",
                "Quit without saving (y/n)");
            cJSON_AddItemToObject(root, "fast_quit_prompt", p);
        }
    }

    // ---- view stack top ----
    {
        ViewKind v = views_active();
        cJSON_AddNumberToObject(root, "view", (int)v);
    }

    return root;
}
