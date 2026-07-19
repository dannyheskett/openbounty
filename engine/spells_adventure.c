#include "spells_adventure.h"

#include <stdio.h>
#include <string.h>

#include "tables.h"
#include "tile.h"
#include "ui_host.h"     // prompt_yes_no_open
#include "resources.h"
#include "pending.h"     // FLOW_DISCARD_SPELL + pending_discard_spell_idx
#include "player_io.h"   // player_io_raise_decision / REQ_PROMPT_YES_NO

BridgeState bridge_state = BRIDGE_STATE_NONE;
GateState   gate_state   = GATE_STATE_NONE;
int         gate_mode    = 0;

void spells_adventure_reset_ui(void) {
    bridge_state = BRIDGE_STATE_NONE;
    gate_state   = GATE_STATE_NONE;
    gate_mode    = 0;
}

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
    player_io_message(g, spell_header("time_stop", "Time Stop"), msg);
}

static void cast_find_villain(Game *g) {
    const ResBanners *bn = &g->res->banners;
    char msg[256];
    if (!g->contract.active_id[0]) {
        resources_format_template(msg, sizeof msg,
                                  bn->spell_find_villain_no_contract, NULL, 0);
        player_io_message(g, spell_header("find_villain", "Find Villain"), msg);
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
            player_io_message(g, spell_header("find_villain", "Find Villain"), msg);
            return;
        }
    }
    resources_format_template(msg, sizeof msg,
                              bn->spell_find_villain_none, NULL, 0);
    player_io_message(g, spell_header("find_villain", "Find Villain"), msg);
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
    int idx = spell_index_by_adventure_effect(ADV_EFFECT_BRIDGE);
    if (idx < 0 || g->spells.counts[idx] <= 0) return;
    char msg[RES_BANNER_LEN];
    resources_format_template(msg, sizeof msg,
                              g->res->banners.spell_bridge_prompt, NULL, 0);
    player_io_message(g, spell_header("bridge", "Bridge"), msg);
    bridge_state = BRIDGE_STATE_DIRECTION;
}

static void cast_castle_gate(Game *g) {
    int idx = spell_index_by_adventure_effect(ADV_EFFECT_GATE_CASTLE);
    if (idx < 0 || g->spells.counts[idx] <= 0) return;
    const ResBanners *bn = &g->res->banners;
    char msg[RES_BANNER_LEN];
    // Count visited, GATE-ELIGIBLE castles (exclude the home/audience castle --
    // the spell can't gate there). If none, show the "none" banner and stop;
    // otherwise arm GATE_STATE_SELECT and let the shell show the destination
    // picker (shell_gate.c). The charge is consumed only on a chosen teleport,
    // never on cast or cancel.
    int visited_count = 0;
    for (int i = 0; i < GAME_CASTLES && i < g->res->castle_count; i++) {
        if (!g->castles[i].visited) continue;
        if (resources_castle_is_home(&g->res->castles[i])) continue;
        if (!g->res->castles[i].zone[0]) continue;
        visited_count++;
    }
    if (visited_count == 0) {
        resources_format_template(msg, sizeof msg,
                                  bn->spell_castle_gate_none, NULL, 0);
        player_io_message(g, spell_header("castle_gate", "Castle Gate"), msg);
        return;
    }
    gate_state = GATE_STATE_SELECT;
    gate_mode = 0;
}

static void cast_town_gate(Game *g) {
    int idx = spell_index_by_adventure_effect(ADV_EFFECT_GATE_TOWN);
    if (idx < 0 || g->spells.counts[idx] <= 0) return;
    const ResBanners *bn = &g->res->banners;
    char msg[RES_BANNER_LEN];
    int visited_count = 0;
    int tn = g->res->town_count < GAME_TOWNS ? g->res->town_count : GAME_TOWNS;
    for (int i = 0; i < tn; i++) {
        if (g->towns[i].visited && g->res->towns[i].zone[0]) visited_count++;
    }
    if (visited_count == 0) {
        resources_format_template(msg, sizeof msg,
                                  bn->spell_town_gate_none, NULL, 0);
        player_io_message(g, spell_header("town_gate", "Town Gate"), msg);
        return;
    }
    gate_state = GATE_STATE_SELECT;
    gate_mode = 1;
}

// The class's conjured troop and per-cast count -- the single definitions of
// the instant-army arithmetic; the cast below and every planning layer share
// them (declared in game.h).
const TroopDef *GameInstantArmyTroop(const Game *g) {
    // troop_id = classes[class][rank].instant_army
    const ClassDef *cls = class_by_id(g->character.cls.id);
    if (!cls) return NULL;
    int rank = g->character.cls.rank_index;
    if (rank < 0 || rank >= cls->rank_count) rank = 0;
    return troop_by_index(cls->ranks[rank].instant_army);
}

int GameInstantArmyPerCast(const Game *g) {
    // number = (spell_power + 1) * instant_army_multiplier[rank]
    const ClassDef *cls = class_by_id(g->character.cls.id);
    int rank = g->character.cls.rank_index;
    if (!cls || rank < 0 || rank >= cls->rank_count) rank = 0;
    const ResTuning *tn = &g->res->tuning;
    int mult_rank = (rank >= 0 && rank < 4) ? rank : 0;
    int multiplier = tn->instant_army_multiplier[mult_rank];
    if (multiplier < 1) multiplier = 1;
    int count = (g->stats.spell_power + 1) * multiplier;
    return count < 1 ? 1 : count;
}

static void cast_instant_army(Game *g) {
    int idx = spell_index_by_adventure_effect(ADV_EFFECT_INSTANT_ARMY);
    if (idx < 0 || g->spells.counts[idx] <= 0) return;

    const ResBanners *bn = &g->res->banners;
    char msg[RES_BANNER_LEN];

    const TroopDef *troop = GameInstantArmyTroop(g);
    if (!troop || !troop->id[0]) {
        resources_format_template(msg, sizeof msg,
                                  bn->spell_instant_army_fizzle, NULL, 0);
        player_io_message(g, spell_header("instant_army", "Instant Army"), msg);
        return;
    }
    int count = GameInstantArmyPerCast(g);

    int rc = GameAddTroop(g, troop->id, count);
    if (rc != 0) {
        resources_format_template(msg, sizeof msg,
                                  bn->spell_instant_army_no_room, NULL, 0);
        player_io_message(g, spell_header("instant_army", "Instant Army"), msg);
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
    player_io_message(g, spell_header("instant_army", "Instant Army"), msg);
}

static void cast_raise_control(Game *g) {
    int idx = spell_index_by_adventure_effect(ADV_EFFECT_RAISE_CONTROL);
    if (idx < 0 || g->spells.counts[idx] <= 0) return;
    // ONE formula: GameCastRaiseControl (engine/game.c) consumes the charge and
    // applies GameRaiseControlAmount, including its min-100 clamp. This wrapper
    // only adds the banner -- it never recomputes the amount.
    int amount = GameRaiseControlAmount(g);
    GameCastRaiseControl(g);
    char msg[RES_BANNER_LEN], abuf[16];
    snprintf(abuf, sizeof abuf, "%d", amount);
    ResTemplateVar vars[] = { { "AMOUNT", abuf } };
    resources_format_template(msg, sizeof msg,
                              g->res->banners.spell_raise_control_success,
                              vars, 1);
    player_io_message(g, spell_header("raise_control", "Raise Control"), msg);
}

void dispatch_adventure_spell(Game *g, int spell_idx) {
    const SpellDef *sp = spell_by_index(spell_idx);
    if (sp && sp->kind == SPELL_KIND_COMBAT) {
        // Combat spells can't be cast in the open world. Rather than a dead
        // "unavailable" message, offer to DISCARD one charge to free a spellbook
        // slot (the book is capped by max_spells; combat spells you can't use in
        // the field otherwise occupy that cap). Only offer if a charge exists.
        if (g->spells.counts[spell_idx] <= 0) {
            player_io_message(g, NULL, g->res->banners.spell_not_known);
            return;
        }
        char body[256];
        snprintf(body, sizeof body,
                 "%s cannot be cast in the field.\n\n"
                 "Discard it to free a spell slot?",
                 sp->name[0] ? sp->name : "This spell");
        const char *header = sp->name[0] ? sp->name : "Spell";
        pending_discard_spell_idx = spell_idx;
        pending_flow = FLOW_DISCARD_SPELL;
        prompt_yes_no_open(header, body);
        player_io_raise_decision(g, FLOW_DISCARD_SPELL, REQ_PROMPT_YES_NO,
                                 header, body);
        return;
    }
    if (!sp || sp->kind != SPELL_KIND_ADVENTURE) {
        player_io_message(g, NULL, g->res->banners.spell_unavailable);
        return;
    }
    if (g->spells.counts[spell_idx] <= 0) {
        player_io_message(g, NULL, g->res->banners.spell_not_known);
        return;
    }
    if (!GameApplyAdventureSpellEffect(g, spell_idx))
        player_io_message(g, NULL, g->res->banners.spell_unknown);
}

// Apply the EFFECT of adventure spell `spell_idx`. The id->effect mapping lives
// in ONE table (spell_adventure_effect, tables.c); this dispatches on the typed
// classification, so no second copy of the ids exists. Each cast_* consumes one
// charge and mutates g. Shared by the UI dispatcher above and by headless
// callers (autoplay's generic spell economy: "apply this spell, did it flip the
// objective?" -- no spell id known to autoplay). The cast_* helpers enqueue a
// player_io message; in a headless Game-copy probe that queue is drained and
// discarded, so this is safe to call on a throwaway copy. Returns true if the
// slot classified to a known adventure effect (an effect was applied).
bool GameApplyAdventureSpellEffect(Game *g, int spell_idx) {
    if (!g) return false;
    switch (spell_adventure_effect(spell_idx)) {
    case ADV_EFFECT_BRIDGE:        cast_bridge(g);        return true;
    case ADV_EFFECT_TIME_STOP:     cast_time_stop(g);     return true;
    case ADV_EFFECT_FIND_VILLAIN:  cast_find_villain(g);  return true;
    case ADV_EFFECT_GATE_CASTLE:   cast_castle_gate(g);   return true;
    case ADV_EFFECT_GATE_TOWN:     cast_town_gate(g);     return true;
    case ADV_EFFECT_INSTANT_ARMY:  cast_instant_army(g);  return true;
    case ADV_EFFECT_RAISE_CONTROL: cast_raise_control(g); return true;
    case ADV_EFFECT_NONE:          break;
    }
    return false;
}
