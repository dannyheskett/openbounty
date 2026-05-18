#include "spells_adventure.h"

#include <stdio.h>
#include <string.h>

#include "tables.h"
#include "tile.h"
#include "ui.h"
#include "resources.h"

BridgeState bridge_state = BRIDGE_STATE_NONE;
GateState   gate_state   = GATE_STATE_NONE;
int         gate_mode    = 0;

const char *spell_header(const char *spell_id, const char *fallback) {
    const SpellDef *sp = spell_by_id(spell_id);
    return (sp && sp->name[0]) ? sp->name : fallback;
}

static void cast_time_stop(Game *g) {
    GameCastTimeStop(g);
    char msg[256], sbuf[16];
    snprintf(sbuf, sizeof sbuf, "%d", g->stats.time_stop);
    ResTemplateVar vars[] = { { "STEPS", sbuf } };
    resources_format_template(msg, sizeof msg,
                              g->res->banners.spell_time_stop, vars, 1);
    open_dialog(spell_header("time_stop", "Time Stop"), msg);
}

static void cast_find_villain(Game *g) {
    const ResBanners *bn = &g->res->banners;
    char msg[256];
    if (!g->contract.active_id[0]) {
        resources_format_template(msg, sizeof msg,
                                  bn->spell_find_villain_no_contract, NULL, 0);
        open_dialog(spell_header("find_villain", "Find Villain"), msg);
        return;
    }
    GameCastFindVillain(g);
    const char *villain_id = g->contract.active_id;
    for (int i = 0; i < GAME_CASTLES; i++) {
        if (g->castles[i].known &&
            strcmp(g->castles[i].villain_id, villain_id) == 0) {
            // Look up the castle's display name; fall back to id if
            // missing from the resource catalog.
            const ResCastle *rc =
                resources_castle_by_id(g->res, g->castles[i].id);
            const char *castle_label =
                (rc && rc->name[0]) ? rc->name : g->castles[i].id;
            ResTemplateVar vars[] = { { "CASTLE", castle_label } };
            resources_format_template(msg, sizeof msg,
                                      bn->spell_find_villain_success, vars, 1);
            open_dialog(spell_header("find_villain", "Find Villain"), msg);
            return;
        }
    }
    resources_format_template(msg, sizeof msg,
                              bn->spell_find_villain_none, NULL, 0);
    open_dialog(spell_header("find_villain", "Find Villain"), msg);
}

int try_build_bridge(Game *g, Map *map, int dx, int dy) {
    // Build bridge in direction (dx, dy). Returns number of tiles placed.
    const char *bridge_art = (dy != 0) ? "bridge_v" : "bridge_h";

    int built = 0;
    for (int i = 1; i <= 5; i++) {
        int nx = g->position.x + i * dx;
        int ny = g->position.y + i * dy;

        if (!MapInBounds(map, nx, ny)) break;

        const Tile *t = MapGetTile(map, nx, ny);
        if (t->terrain != TERRAIN_WATER) break;

        map->tiles[ny][nx].art[0] = '\0';
        map->tiles[ny][nx].terrain = TERRAIN_GRASS;
        map->tiles[ny][nx].interactive = INTERACT_NONE;
        map->tiles[ny][nx].blocks_foot = false;
        map->tiles[ny][nx].is_bridge = true;
        strncpy(map->tiles[ny][nx].art, bridge_art, TILE_ART_NAME_LEN - 1);

        built++;
        if (built >= 2) break;  // builds max 2 tiles (or up to 5 water tiles)
    }
    return built;
}

static void cast_bridge(Game *g) {
    int idx = spell_index_by_id("bridge");
    if (idx < 0 || g->spells.counts[idx] <= 0) return;
    char msg[RES_BANNER_LEN];
    resources_format_template(msg, sizeof msg,
                              g->res->banners.spell_bridge_prompt, NULL, 0);
    open_dialog(spell_header("bridge", "Bridge"), msg);
    bridge_state = BRIDGE_STATE_DIRECTION;
}

static void cast_castle_gate(Game *g) {
    int idx = spell_index_by_id("castle_gate");
    if (idx < 0 || g->spells.counts[idx] <= 0) return;
    const ResBanners *bn = &g->res->banners;
    char msg[RES_BANNER_LEN];
    int visited_count = 0;
    for (int i = 0; i < GAME_CASTLES; i++) {
        if (g->castles[i].visited) visited_count++;
    }
    if (visited_count == 0) {
        resources_format_template(msg, sizeof msg,
                                  bn->spell_castle_gate_none, NULL, 0);
        open_dialog(spell_header("castle_gate", "Castle Gate"), msg);
        return;
    }
    resources_format_template(msg, sizeof msg,
                              bn->spell_castle_gate_choose, NULL, 0);
    open_dialog(spell_header("castle_gate", "Castle Gate"), msg);
    gate_state = GATE_STATE_SELECT;
    gate_mode = 0;
}

static void cast_town_gate(Game *g) {
    int idx = spell_index_by_id("town_gate");
    if (idx < 0 || g->spells.counts[idx] <= 0) return;
    const ResBanners *bn = &g->res->banners;
    char msg[RES_BANNER_LEN];
    int visited_count = 0;
    for (int i = 0; i < GAME_TOWNS; i++) {
        if (g->towns[i].visited) visited_count++;
    }
    if (visited_count == 0) {
        resources_format_template(msg, sizeof msg,
                                  bn->spell_town_gate_none, NULL, 0);
        open_dialog(spell_header("town_gate", "Town Gate"), msg);
        return;
    }
    resources_format_template(msg, sizeof msg,
                              bn->spell_town_gate_choose, NULL, 0);
    open_dialog(spell_header("town_gate", "Town Gate"), msg);
    gate_state = GATE_STATE_SELECT;
    gate_mode = 1;
}

static void cast_instant_army(Game *g) {
    int idx = spell_index_by_id("instant_army");
    if (idx < 0 || g->spells.counts[idx] <= 0) return;

    const ResBanners *bn = &g->res->banners;
    char msg[RES_BANNER_LEN];

    //  instant_troop: troop_id = classes[class][rank].instant_army
    const ClassDef *cls = class_by_id(g->character.cls.id);
    if (!cls) {
        resources_format_template(msg, sizeof msg,
                                  bn->spell_instant_army_fizzle, NULL, 0);
        open_dialog(spell_header("instant_army", "Instant Army"), msg);
        return;
    }
    int rank = g->character.cls.rank_index;
    if (rank < 0 || rank >= cls->rank_count) rank = 0;
    int troop_idx = cls->ranks[rank].instant_army;
    const TroopDef *troop = troop_by_index(troop_idx);
    if (!troop || !troop->id[0]) {
        resources_format_template(msg, sizeof msg,
                                  bn->spell_instant_army_fizzle, NULL, 0);
        open_dialog(spell_header("instant_army", "Instant Army"), msg);
        return;
    }

    // number = (spell_power + 1) * instant_army_multiplier[rank]
    const ResTuning *tn = &g->res->tuning;
    int mult_rank = (rank >= 0 && rank < 4) ? rank : 0;
    int multiplier = tn->instant_army_multiplier[mult_rank];
    if (multiplier < 1) multiplier = 1;
    int count = (g->stats.spell_power + 1) * multiplier;
    if (count < 1) count = 1;

    int rc = GameAddTroop(g, troop->id, count);
    if (rc != 0) {
        resources_format_template(msg, sizeof msg,
                                  bn->spell_instant_army_no_room, NULL, 0);
        open_dialog(spell_header("instant_army", "Instant Army"), msg);
        return;
    }

    g->spells.counts[idx]--;
    const ResUI *ui = &g->res->ui;
    const char *qty = resources_count_bucket_label(
        ui->count_buckets_instant_army,
        ui->count_buckets_instant_army_n,
        count, "");
    ResTemplateVar vars[] = {
        { "QTY",   qty },
        { "TROOP", troop->name },
    };
    resources_format_template(msg, sizeof msg,
                              bn->spell_instant_army_success, vars, 2);
    open_dialog(spell_header("instant_army", "Instant Army"), msg);
}

static void cast_raise_control(Game *g) {
    int idx = spell_index_by_id("raise_control");
    if (idx < 0 || g->spells.counts[idx] <= 0) return;
    g->spells.counts[idx]--;
    //  raise_control: leadership += spell_power * 100.
    int amount = g->stats.spell_power * 100;
    g->stats.leadership_current += amount;
    char msg[RES_BANNER_LEN], abuf[16];
    snprintf(abuf, sizeof abuf, "%d", amount);
    ResTemplateVar vars[] = { { "AMOUNT", abuf } };
    resources_format_template(msg, sizeof msg,
                              g->res->banners.spell_raise_control_success,
                              vars, 1);
    open_dialog(spell_header("raise_control", "Raise Control"), msg);
}

void dispatch_adventure_spell(Game *g, int spell_idx) {
    const SpellDef *sp = spell_by_index(spell_idx);
    if (!sp || sp->kind != SPELL_KIND_ADVENTURE) {
        open_dialog(NULL, g->res->banners.spell_unavailable);
        return;
    }
    if (g->spells.counts[spell_idx] <= 0) {
        open_dialog(NULL, g->res->banners.spell_not_known);
        return;
    }
    if      (strcmp(sp->id, "bridge")        == 0) cast_bridge(g);
    else if (strcmp(sp->id, "time_stop")     == 0) cast_time_stop(g);
    else if (strcmp(sp->id, "find_villain")  == 0) cast_find_villain(g);
    else if (strcmp(sp->id, "castle_gate")   == 0) cast_castle_gate(g);
    else if (strcmp(sp->id, "town_gate")     == 0) cast_town_gate(g);
    else if (strcmp(sp->id, "instant_army")  == 0) cast_instant_army(g);
    else if (strcmp(sp->id, "raise_control") == 0) cast_raise_control(g);
    else open_dialog(NULL, g->res->banners.spell_unknown);
}
