// engine/resources_strings.c -- the pack's strings: banners, the combat log
// and the UI labels (resources_load's strings half).

#include "resources_internal.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- Strings ---------------------------------------------------------------

static void parse_end_text(ResEndText *dst, cJSON *obj) {
    if (!cJSON_IsObject(obj)) return;
    res_copy_str(dst->header, sizeof(dst->header), res_json_str(obj, "header", ""));
    res_copy_str(dst->body,   sizeof(dst->body),   res_json_str(obj, "body", ""));
    res_copy_str(dst->footer, sizeof(dst->footer), res_json_str(obj, "footer", ""));
}

static void parse_banners(ResBanners *b, cJSON *obj, Resources *res) {
    // No defaults: every key MUST come from the pack. A missing key is
    // recorded (and printed) and hard-fails the load -- never a silent English
    // fallback. (obj may be NULL if the pack omits the whole section.)
    #define SET_BANNER(field, key) do { \
        const char *s = cJSON_IsObject(obj) ? res_json_str(obj, key, NULL) : NULL; \
        if (s) res_copy_str(b->field, sizeof(b->field), s); \
        else { fprintf(stdout, "resources: pack missing string key '%s'\n", key); \
               res->strings_missing++; } \
    } while (0)
    SET_BANNER(chest_gold,        "chest_gold");
    // Optional (modern): a pack without them shows the rows as A and B.
    #define SET_BANNER_OPT(field, key) do { \
        const char *s = cJSON_IsObject(obj) ? res_json_str(obj, key, NULL) : NULL; \
        res_copy_str(b->field, sizeof(b->field), s ? s : ""); \
    } while (0)
    SET_BANNER_OPT(chest_gold_title, "chest_gold_title");
    SET_BANNER_OPT(chest_gold_found, "chest_gold_found");
    SET_BANNER_OPT(chest_gold_take,  "chest_gold_take");
    SET_BANNER_OPT(chest_gold_share, "chest_gold_share");
    SET_BANNER_OPT(gmr_spell_in_fight, "gmr_spell_in_fight");
    SET_BANNER_OPT(goto_title, "goto_title");
    SET_BANNER_OPT(goto_to, "goto_to");
    SET_BANNER_OPT(goto_today, "goto_today");
    SET_BANNER_OPT(goto_days, "goto_days");
    SET_BANNER_OPT(goto_no_route, "goto_no_route");
    SET_BANNER_OPT(goto_go, "goto_go");
    SET_BANNER_OPT(goto_cancel, "goto_cancel");
    SET_BANNER_OPT(gm_goto, "gm_goto");
    SET_BANNER_OPT(gmd_goto, "gmd_goto");
    SET_BANNER_OPT(rail_goto, "rail_goto");
    SET_BANNER_OPT(gmr_spell_on_map,   "gmr_spell_on_map");
    #undef SET_BANNER_OPT
    SET_BANNER(chest_commission,  "chest_commission");
    SET_BANNER(chest_spell_power, "chest_spell_power");
    SET_BANNER(chest_max_spells,  "chest_max_spells");
    SET_BANNER(chest_new_spell,   "chest_new_spell");
    SET_BANNER(chest_empty,       "chest_empty");
    SET_BANNER(town_header,             "town_header");
    SET_BANNER(town_intro, "town_intro");
    SET_BANNER(town_intro_inland, "town_intro_inland");
    SET_BANNER(town_visit, "town_visit");
    SET_BANNER(cv_wanted, "cv_wanted");
    SET_BANNER(cv_no_contract_hint, "cv_no_contract_hint");
    SET_BANNER(puzzle_legend, "puzzle_legend");
    SET_BANNER(gate_travel, "gate_travel");
    SET_BANNER(worldmap_all, "worldmap_all");
    SET_BANNER(worldmap_you, "worldmap_you");
    SET_BANNER(worldmap_boat, "worldmap_boat");
    SET_BANNER(worldmap_boat_elsewhere, "worldmap_boat_elsewhere");
    SET_BANNER(worldmap_no_boat, "worldmap_no_boat");
    SET_BANNER(spell_bridge_prompt_modern, "spell_bridge_prompt_modern");
    SET_BANNER(save_done_title, "save_done_title");
    SET_BANNER(save_done, "save_done");
    SET_BANNER(town_gold_label,         "town_gold_label");
    SET_BANNER(town_row_contract,       "town_row_contract");
    SET_BANNER(town_row_boat_rent,      "town_row_boat_rent");
    SET_BANNER(town_row_boat_cancel,    "town_row_boat_cancel");
    SET_BANNER(town_row_info,           "town_row_info");
    SET_BANNER(town_row_spell,          "town_row_spell");
    SET_BANNER(town_row_spell_none,     "town_row_spell_none");
    SET_BANNER(town_row_siege_buy,      "town_row_siege_buy");
    SET_BANNER(town_row_siege_owned,    "town_row_siege_owned");
    SET_BANNER(town_contract_new,       "town_contract_new");
    SET_BANNER(town_contract_none,      "town_contract_none");
    SET_BANNER(town_boat_vacate_first,  "town_boat_vacate_first");
    SET_BANNER(town_no_gold,            "town_no_gold");
    SET_BANNER(town_intel_unavailable,  "town_intel_unavailable");
    SET_BANNER(town_intel_castle_under, "town_intel_castle_under");
    SET_BANNER(town_intel_owner_rule,   "town_intel_owner_rule");
    {   // Optional: a sign's title as the message's header (#135).
        const char *s = cJSON_IsObject(obj) ? res_json_str(obj, "signpost_header", NULL) : NULL;
        res_copy_str(b->signpost_header, sizeof(b->signpost_header), s ? s : "");
    }
    {   // Optional: a pack that reports at the castle gate (#139).
        const char *s = cJSON_IsObject(obj) ? res_json_str(obj, "castle_gate_owner", NULL) : NULL;
        res_copy_str(b->castle_gate_owner, sizeof(b->castle_gate_owner), s ? s : "");
    }
    SET_BANNER(town_intel_owner_none,   "town_intel_owner_none");
    SET_BANNER(town_intel_owner_player, "town_intel_owner_player");
    SET_BANNER(town_intel_owner_king,   "town_intel_owner_king");
    SET_BANNER(town_intel_count_named,  "town_intel_count_named");
    SET_BANNER(town_intel_count_numeric,"town_intel_count_numeric");
    SET_BANNER(town_intel_monsters_generic, "town_intel_monsters_generic");
    SET_BANNER(town_intel_no_garrison,  "town_intel_no_garrison");
    SET_BANNER(town_intel_artifact,      "town_intel_artifact");
    SET_BANNER(town_intel_artifact_none, "town_intel_artifact_none");
    SET_BANNER(artifact_found,           "artifact_found");
    SET_BANNER(artifact_map_piece,       "artifact_map_piece");
    SET_BANNER(castle_header,            "castle_header");
    SET_BANNER(castle_siege_monsters,    "castle_siege_monsters");
    SET_BANNER(castle_uncharted,         "castle_uncharted");
    // Optional: only a pack with world.castle_gate_report asks under a report.
    res_copy_str(b->castle_siege_ask, sizeof b->castle_siege_ask,
             cJSON_IsObject(obj) ? res_json_str(obj, "castle_siege_ask", "") : "");
    SET_BANNER(search_nothing,           "search_nothing");
    SET_BANNER(zone_unreachable,         "zone_unreachable");
    SET_BANNER(town_spell_unavailable,  "town_spell_unavailable");
    SET_BANNER(town_spell_at_cap,       "town_spell_at_cap");
    SET_BANNER(town_spell_can_learn,    "town_spell_can_learn");
    SET_BANNER(town_siege_already,      "town_siege_already");
    SET_BANNER(town_siege_purchased,    "town_siege_purchased");
    SET_BANNER(town_menu_contract,      "town_menu_contract");
    SET_BANNER(town_menu_boat_rent,     "town_menu_boat_rent");
    SET_BANNER(town_menu_boat_cancel,   "town_menu_boat_cancel");
    SET_BANNER(town_menu_info,          "town_menu_info");
    SET_BANNER(town_menu_spell,         "town_menu_spell");
    SET_BANNER(town_menu_siege,         "town_menu_siege");
    SET_BANNER(town_contract_confirm,   "town_contract_confirm");
    SET_BANNER(foe_fight, "foe_fight");
    SET_BANNER(foe_evade, "foe_evade");
    SET_BANNER(foe_evade_blocked, "foe_evade_blocked");
    // Optional: only a pack that gates a foe on one troop needs the words.
    {
        const char *s = cJSON_IsObject(obj) ? res_json_str(obj, "foe_requires_troop", NULL) : NULL;
        if (s) res_copy_str(b->foe_requires_troop, sizeof b->foe_requires_troop, s);
    }
    SET_BANNER(castle_menu_recruit, "castle_menu_recruit");
    SET_BANNER(castle_continue, "castle_continue");
    SET_BANNER(castle_menu_audience, "castle_menu_audience");
    SET_BANNER(castle_menu_garrison, "castle_menu_garrison");
    SET_BANNER(castle_menu_withdraw, "castle_menu_withdraw");
    SET_BANNER(castle_action_audience, "castle_action_audience");
    SET_BANNER(castle_invite_recruit, "castle_invite_recruit");
    SET_BANNER(castle_invite_audience, "castle_invite_audience");
    SET_BANNER(castle_invite_garrison, "castle_invite_garrison");
    SET_BANNER(castle_invite_withdraw, "castle_invite_withdraw");
    SET_BANNER(castle_have, "castle_have");
    SET_BANNER(castle_in_garrison, "castle_in_garrison");
    SET_BANNER(castle_needs_leadership, "castle_needs_leadership");
    SET_BANNER(castle_can_recruit, "castle_can_recruit");
    SET_BANNER(castle_rank, "castle_rank");
    SET_BANNER(castle_next_rank, "castle_next_rank");
    SET_BANNER(castle_needed, "castle_needed");
    SET_BANNER(castle_gain_leadership, "castle_gain_leadership");
    SET_BANNER(castle_gain_commission, "castle_gain_commission");
    SET_BANNER(castle_gain_spells, "castle_gain_spells");
    SET_BANNER(castle_gain_spell_power, "castle_gain_spell_power");
    SET_BANNER(castle_action_promotion, "castle_action_promotion");
    SET_BANNER(castle_action_blessing, "castle_action_blessing");
    SET_BANNER(castle_action_tribute, "castle_action_tribute");
    SET_BANNER(castle_tribute_confirm, "castle_tribute_confirm");
    SET_BANNER(castle_artifacts, "castle_artifacts");
    SET_BANNER(castle_over_leadership, "castle_over_leadership");
    SET_BANNER(castle_cost, "castle_cost");
    SET_BANNER(castle_no_troops, "castle_no_troops");
    SET_BANNER(town_temple_needs_rites, "town_temple_needs_rites");
    SET_BANNER(town_back, "town_back");
    SET_BANNER(town_menu_boat, "town_menu_boat");
    SET_BANNER(town_action_spell, "town_action_spell");
    SET_BANNER(town_action_siege, "town_action_siege");
    SET_BANNER(town_action_owned, "town_action_owned");
    SET_BANNER(town_boat_no_master, "town_boat_no_master");
    SET_BANNER(town_boat_rented, "town_boat_rented");
    SET_BANNER(town_boat_returned, "town_boat_returned");
    SET_BANNER(town_siege_lore, "town_siege_lore");
    SET_BANNER(town_confirm_boat_rent, "town_confirm_boat_rent");
    SET_BANNER(town_confirm_boat_cancel, "town_confirm_boat_cancel");
    SET_BANNER(town_confirm_spell, "town_confirm_spell");
    SET_BANNER(town_confirm_siege, "town_confirm_siege");
    SET_BANNER(spell_time_stop,                "spell_time_stop");
    SET_BANNER(spell_find_villain_no_contract, "spell_find_villain_no_contract");
    SET_BANNER(spell_find_villain_success,     "spell_find_villain_success");
    SET_BANNER(spell_find_villain_none,        "spell_find_villain_none");
    SET_BANNER(spell_bridge_prompt,            "spell_bridge_prompt");
    SET_BANNER(spell_bridge_built,             "spell_bridge_built");
    SET_BANNER(spell_bridge_invalid,           "spell_bridge_invalid");
    SET_BANNER(spell_castle_gate_none,         "spell_castle_gate_none");
    SET_BANNER(spell_town_gate_none,           "spell_town_gate_none");
    SET_BANNER(spell_instant_army_fizzle,      "spell_instant_army_fizzle");
    SET_BANNER(spell_instant_army_no_room,     "spell_instant_army_no_room");
    SET_BANNER(spell_instant_army_success,     "spell_instant_army_success");
    SET_BANNER(spell_raise_control_success,    "spell_raise_control_success");
    SET_BANNER(encounter_join_named,           "encounter_join_named");
    SET_BANNER(encounter_join_numeric,         "encounter_join_numeric");
    SET_BANNER(encounter_join_title,           "encounter_join_title");
    SET_BANNER(encounter_wanderers,            "encounter_wanderers");
    SET_BANNER(encounter_hostile_header,       "encounter_hostile_header");
    SET_BANNER(encounter_hostile_unknown,      "encounter_hostile_unknown");
    SET_BANNER(encounter_hostile_count_named,  "encounter_hostile_count_named");
    SET_BANNER(encounter_hostile_count_numeric,"encounter_hostile_count_numeric");
    SET_BANNER(alcove_offer,                   "alcove_offer");
    SET_BANNER(alcove_already,                 "alcove_already");
    SET_BANNER(temple_title, "temple_title");
    SET_BANNER(temple_learn, "temple_learn");
    SET_BANNER(location_leave, "location_leave");
    SET_BANNER(alcove_offer_modern, "alcove_offer_modern");
    SET_BANNER(gmd_hero, "gmd_hero");
    SET_BANNER(gmd_world, "gmd_world");
    SET_BANNER(gmd_game, "gmd_game");
    SET_BANNER(gmd_back_up, "gmd_back_up");
    SET_BANNER(gmd_unit, "gmd_unit");
    SET_BANNER(fv_your_army, "fv_your_army");
    SET_BANNER(loc_joined, "loc_joined");
    SET_BANNER(count_heading, "count_heading");
    SET_BANNER(count_of_lead, "count_of_lead");
    SET_BANNER(count_of_army, "count_of_army");
    SET_BANNER(count_of_garrison, "count_of_garrison");
    SET_BANNER(count_cost, "count_cost");
    SET_BANNER(count_recruit, "count_recruit");
    SET_BANNER(count_garrison, "count_garrison");
    SET_BANNER(count_withdraw, "count_withdraw");
    SET_BANNER(count_cancel, "count_cancel");
    SET_BANNER(count_min, "count_min");
    SET_BANNER(count_max, "count_max");
    SET_BANNER(capture_title, "capture_title");
    SET_BANNER(capture_contract, "capture_contract");
    SET_BANNER(capture_free, "capture_free");
    SET_BANNER(capture_promoted, "capture_promoted");
    SET_BANNER(temple_intro, "temple_intro");
    SET_BANNER(dwelling_intro, "dwelling_intro");
    SET_BANNER(gmd_leave, "gmd_leave");
    SET_BANNER(gmd_army, "gmd_army");
    SET_BANNER(gmd_character, "gmd_character");
    SET_BANNER(gmd_contract, "gmd_contract");
    SET_BANNER(gmd_puzzle, "gmd_puzzle");
    SET_BANNER(gmd_dismiss, "gmd_dismiss");
    SET_BANNER(gmd_map, "gmd_map");
    SET_BANNER(gmd_cast, "gmd_cast");
    SET_BANNER(gmd_search, "gmd_search");
    SET_BANNER(gmd_fly, "gmd_fly");
    SET_BANNER(gmd_land, "gmd_land");
    SET_BANNER(gmd_end_week, "gmd_end_week");
    SET_BANNER(gmd_rest, "gmd_rest");
    SET_BANNER(gmd_sail, "gmd_sail");
    SET_BANNER(gmd_controls, "gmd_controls");
    SET_BANNER(gmd_save, "gmd_save");
    SET_BANNER(gmd_load, "gmd_load");
    SET_BANNER(gmd_new_game, "gmd_new_game");
    SET_BANNER(gmd_exit, "gmd_exit");
    SET_BANNER(gmd_back, "gmd_back");
    SET_BANNER(gmd_debug, "gmd_debug");
    SET_BANNER(gmd_wait, "gmd_wait");
    SET_BANNER(gmd_shoot, "gmd_shoot");
    SET_BANNER(gmd_unit_fly, "gmd_unit_fly");
    SET_BANNER(gmd_combat_cast, "gmd_combat_cast");
    SET_BANNER(gmd_combat_army, "gmd_combat_army");
    SET_BANNER(gmd_combat_character, "gmd_combat_character");
    SET_BANNER(gmd_give_up, "gmd_give_up");
    SET_BANNER(gmr_no_troops, "gmr_no_troops");
    SET_BANNER(gmr_not_sailing, "gmr_not_sailing");
    SET_BANNER(gmr_no_saves, "gmr_no_saves");
    SET_BANNER(gmr_no_shots, "gmr_no_shots");
    SET_BANNER(gmr_adjacent, "gmr_adjacent");
    SET_BANNER(gmr_cannot_fly, "gmr_cannot_fly");
    SET_BANNER(gmr_one_spell, "gmr_one_spell");
    SET_BANNER(gmr_no_magic, "gmr_no_magic");
    SET_BANNER(gmr_no_spell_held, "gmr_no_spell_held");
    SET_BANNER(gmr_empty_slot, "gmr_empty_slot");
    SET_BANNER(gmc_overwrite, "gmc_overwrite");
    SET_BANNER(gmc_load, "gmc_load");
    SET_BANNER(gmc_exit, "gmc_exit");
    SET_BANNER(dwelling_recruit_row, "dwelling_recruit_row");
    SET_BANNER(alcove_taught,                  "alcove_taught");
    SET_BANNER(alcove_no_gold,                 "alcove_no_gold");
    SET_BANNER(no_spell_banner,                "no_spell_banner");
    SET_BANNER(new_game_intro,                 "new_game_intro");
    SET_BANNER(combat_victory_named,           "combat_victory_named");
    SET_BANNER(combat_victory_unnamed,         "combat_victory_unnamed");
    SET_BANNER(dwelling_recruit_prompt,        "dwelling_recruit_prompt");
    SET_BANNER(dwelling_none_this_week,        "dwelling_none_this_week");
    SET_BANNER(dwelling_empty,                 "dwelling_empty");
    SET_BANNER(telecave_teleport,              "telecave_teleport");
    SET_BANNER(telecave_inert,                 "telecave_inert");
    SET_BANNER(navmap_pickup,                  "navmap_pickup");
    SET_BANNER(crystal_ball_pickup,            "crystal_ball_pickup");
    SET_BANNER(astrology_header,               "astrology_header");
    SET_BANNER(astrology_body,                 "astrology_body");
    SET_BANNER(temp_death,                     "temp_death");
    SET_BANNER(combat_give_up_header,          "combat_give_up_header");
    SET_BANNER(combat_give_up_body,            "combat_give_up_body");
    SET_BANNER(signpost_with_body,             "signpost_with_body");
    SET_BANNER(signpost_title_only,            "signpost_title_only");
    SET_BANNER(budget_header,                  "budget_header");
    SET_BANNER(budget_on_hand,                 "budget_on_hand");
    SET_BANNER(budget_payment,                 "budget_payment");
    SET_BANNER(budget_boat,                    "budget_boat");
    SET_BANNER(budget_army,                    "budget_army");
    SET_BANNER(budget_balance,                 "budget_balance");
    {   // Optional: a pack whose unpaid troops leave (#141).
        const char *s = cJSON_IsObject(obj) ? res_json_str(obj, "week_troops_left", NULL) : NULL;
        res_copy_str(b->week_troops_left, sizeof(b->week_troops_left), s ? s : "");
    }
    {   // Optional: a pack whose learned spells renew each week (#157).
        const char *s = cJSON_IsObject(obj) ? res_json_str(obj, "week_spell_renewed", NULL) : NULL;
        res_copy_str(b->week_spell_renewed, sizeof(b->week_spell_renewed), s ? s : "");
        s = cJSON_IsObject(obj) ? res_json_str(obj, "spell_combat_only", NULL) : NULL;
        res_copy_str(b->spell_combat_only, sizeof(b->spell_combat_only), s ? s : "");
    }
    SET_BANNER(status_days_left,               "status_days_left");
    SET_BANNER(status_time_stop,               "status_time_stop");
    SET_BANNER(body_save_confirm,              "body_save_confirm");
    SET_BANNER(body_search,                    "body_search");
    SET_BANNER(body_dismiss_pick,              "body_dismiss_pick");
    SET_BANNER(body_dismiss_last,              "body_dismiss_last");
    SET_BANNER(body_home_castle,               "body_home_castle");
    SET_BANNER(body_navigate_row,              "body_navigate_row");
    // Optional: only a pack that ships the sailing picture asks the question,
    // so a pack without the scene is not required to word it (REQ-221c).
    {
        const char *s = cJSON_IsObject(obj) ? res_json_str(obj, "body_navigate_confirm", NULL) : NULL;
        if (s) res_copy_str(b->body_navigate_confirm, sizeof b->body_navigate_confirm, s);
    }
    SET_BANNER(body_no_continents,             "body_no_continents");
    SET_BANNER(body_must_be_sailing,           "body_must_be_sailing");
    SET_BANNER(cannot_garrison_last,           "cannot_garrison_last");
    SET_BANNER(no_troop_slots,                 "no_troop_slots");
    SET_BANNER(army_cannot_handle,             "army_cannot_handle");
    SET_BANNER(spell_unavailable,              "spell_unavailable");
    SET_BANNER(spell_not_known,                "spell_not_known");
    SET_BANNER(spell_unknown,                  "spell_unknown");
    #undef SET_BANNER
}

// ---- Combat log strings (game.json strings.combat_log) ------------
// Defaults follow docs/OPENBOUNTY-SPEC.md section 25.11 verbatim.


static void parse_combat_log(ResCombatLog *cl, cJSON *obj, Resources *res) {
    #define SET_CL(field, key) do { \
        const char *s = cJSON_IsObject(obj) ? res_json_str(obj, key, NULL) : NULL; \
        if (s) res_copy_str(cl->field, sizeof(cl->field), s); \
        else { fprintf(stdout, "resources: pack missing string key '%s'\n", key); \
               res->strings_missing++; } \
    } while (0)
    SET_CL(melee_hit,        "melee_hit");
    SET_CL(retaliate,        "retaliate");
    SET_CL(ranged_hit,       "ranged_hit");
    SET_CL(ranged_no_effect, "ranged_no_effect");
    {   // Optional (#131).
        const char *s = cJSON_IsObject(obj) ? res_json_str(obj, "melee_no_kill", NULL) : NULL;
        res_copy_str(cl->melee_no_kill, sizeof(cl->melee_no_kill), s ? s : "");
    }
    SET_CL(no_effect_msg,    "no_effect_msg");
    SET_CL(fly,              "fly");
    SET_CL(move,             "move");
    SET_CL(frozen,           "frozen");
    SET_CL(immune,           "immune");
    SET_CL(cloned,           "cloned");
    SET_CL(resurrected,      "resurrected");
    SET_CL(teleported,       "teleported");
    SET_CL(only_one_spell,   "only_one_spell");
    SET_CL(no_spell_type,    "no_spell_type");
    SET_CL(cannot_cast,      "cannot_cast");
    SET_CL(cast_fireball,    "cast_fireball");
    SET_CL(cast_lightning,   "cast_lightning");
    SET_CL(cast_turn_undead, "cast_turn_undead");
    SET_CL(cant_shoot,       "cant_shoot");
    SET_CL(no_ammo,          "no_ammo");
    SET_CL(cant_fly,         "cant_fly");
    #undef SET_CL
}

// ---- UI labels (game.json strings.ui / .menu / .stats / .army_view /
//                 .morale / .count_buckets / .difficulty / .keybinds /
//                 .startup) ----------------------------------------------



// Override one buffer field if the JSON object has a string at `key`.
#define UI_SET(field, key) do { \
    const char *s = cJSON_IsObject(obj) ? res_json_str(obj, key, NULL) : NULL; \
    if (s) res_copy_str(ui->field, sizeof(ui->field), s); \
    else { fprintf(stdout, "resources: pack missing string key '%s'\n", key); \
           res->strings_missing++; } \
} while (0)

static void parse_count_buckets(ResCountBucket **outp, int *out_n, cJSON *arr) {
    int cap = cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;
    if (!RES_TABLE_ALLOC(*outp, *out_n, cap)) return;
    ResCountBucket *out = *outp;
    int n = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (n >= cap) break;
        if (!cJSON_IsObject(it)) continue;
        out[n].threshold = res_json_int(it, "max", 0x7FFFFFFF);
        res_copy_str(out[n].label, sizeof(out[n].label),
                 res_json_str(it, "label", ""));
        n++;
    }
    *out_n = n;
}

// A strings group the pack must have is absent: refused like a missing key.
static void missing_group(Resources *res, const char *group) {
    fprintf(stdout, "resources: pack missing string group '%s'\n", group);
    res->strings_missing++;
}

static void parse_ui(Resources *res, cJSON *root_strings) {
    ResUI *ui = &res->ui;
    // No defaults: keys come only from the pack (missing -> recorded, hard-fail).

    cJSON *jui = cJSON_GetObjectItem(root_strings, "ui");
    if (cJSON_IsObject(jui)) {
        cJSON *obj = jui;
        UI_SET(press_esc_to_exit, "press_esc_to_exit");
        UI_SET(fv_hp, "fv_hp");
        UI_SET(fv_dmg, "fv_dmg");
        UI_SET(cv_army, "cv_army");
        UI_SET(cv_magic, "cv_magic");
        UI_SET(cv_campaign, "cv_campaign");
        UI_SET(cv_leadership, "cv_leadership");
        UI_SET(cv_commission, "cv_commission");
        UI_SET(cv_gold, "cv_gold");
        UI_SET(cv_spell_power, "cv_spell_power");
        UI_SET(cv_spell_capacity, "cv_spell_capacity");
        UI_SET(cv_captured, "cv_captured");
        UI_SET(cv_artifacts, "cv_artifacts");
        UI_SET(cv_castles, "cv_castles");
        UI_SET(cv_followers, "cv_followers");
        UI_SET(cv_score, "cv_score");
        UI_SET(cv_days, "cv_days");
        UI_SET(cv_sacred, "cv_sacred");
        UI_SET(cv_continents, "cv_continents");
        UI_SET(cv_honours, "cv_honours");
        UI_SET(cv_blessed, "cv_blessed");
        UI_SET(cv_tributes, "cv_tributes");
        UI_SET(cv_rites, "cv_rites");
        UI_SET(cv_yes, "cv_yes");
        UI_SET(cv_no, "cv_no");
        UI_SET(cv_next, "cv_next");
        UI_SET(cv_top_rank, "cv_top_rank");
        UI_SET(key_esc, "key_esc");
        UI_SET(pad_back, "pad_back");
        UI_SET(give_up_header_modern, "give_up_header_modern");
        UI_SET(quit_to_dos_prompt, "quit_to_dos_prompt");
        UI_SET(out_of_control,    "out_of_control");
        UI_SET(worldmap_hint_your_map,  "worldmap_hint_your_map");
        UI_SET(worldmap_hint_whole_map, "worldmap_hint_whole_map");
    } else {
        missing_group(res, "ui");
    }

    cJSON *jmenu = cJSON_GetObjectItem(root_strings, "menu");
    if (cJSON_IsObject(jmenu)) {
        cJSON *obj = jmenu;
        UI_SET(menu_root_title,    "root_title");
        UI_SET(menu_views_title,   "views_title");
        UI_SET(menu_options_title, "options_title");
        cJSON *items = cJSON_GetObjectItem(jmenu, "items");
        if (cJSON_IsObject(items)) {
            obj = items;
            UI_SET(menu_back,      "back");
            UI_SET(menu_exit,      "exit");
            UI_SET(menu_save,      "save");
            UI_SET(menu_load,      "load");
            UI_SET(menu_new_game,  "new_game");
            UI_SET(menu_views,     "views");
            UI_SET(menu_options,   "options");
            UI_SET(menu_army,      "army");
            UI_SET(menu_spells,    "spells");
            UI_SET(menu_character, "character");
            UI_SET(menu_contract,  "contract");
            UI_SET(menu_puzzle,    "puzzle");
            UI_SET(menu_view_map,  "view_map");
            UI_SET(gm_title, "gm_title");
            UI_SET(gm_hero, "gm_hero");
            UI_SET(gm_world, "gm_world");
            UI_SET(gm_game, "gm_game");
            UI_SET(gm_army, "gm_army");
            UI_SET(gm_character, "gm_character");
            UI_SET(gm_contract, "gm_contract");
            UI_SET(gm_puzzle, "gm_puzzle");
            UI_SET(gm_dismiss, "gm_dismiss");
            UI_SET(gm_map, "gm_map");
            UI_SET(gm_cast, "gm_cast");
            UI_SET(gm_search, "gm_search");
            UI_SET(gm_fly, "gm_fly");
            UI_SET(gm_land, "gm_land");
            UI_SET(gm_end_week, "gm_end_week");
            UI_SET(gm_rest, "gm_rest");
            UI_SET(gm_sail, "gm_sail");
            UI_SET(gm_controls, "gm_controls");
            UI_SET(gm_save, "gm_save");
            UI_SET(gm_load, "gm_load");
            UI_SET(gm_new_game, "gm_new_game");
            UI_SET(gm_exit, "gm_exit");
            UI_SET(gm_back, "gm_back");
            UI_SET(gm_close, "gm_close");
            UI_SET(gm_debug, "gm_debug");
            UI_SET(gm_actions, "gm_actions");
            UI_SET(gm_unit, "gm_unit");
            UI_SET(gm_wait, "gm_wait");
            UI_SET(gm_shoot, "gm_shoot");
            UI_SET(gm_give_up, "gm_give_up");
        } else {
            missing_group(res, "menu.items");
        }
    } else {
        missing_group(res, "menu");
    }

    cJSON *jstats = cJSON_GetObjectItem(root_strings, "stats");
    if (cJSON_IsObject(jstats)) {
        cJSON *obj = jstats;
        UI_SET(stat_leadership,         "leadership");
        UI_SET(stat_commission,         "commission");
        UI_SET(stat_gold,               "gold");
        UI_SET(stat_spell_power,        "spell_power");
        UI_SET(stat_max_spells,         "max_spells");
        UI_SET(stat_villains_caught,    "villains_caught");
        UI_SET(stat_artifacts_found,    "artifacts_found");
        UI_SET(stat_castles_garrisoned, "castles_garrisoned");
        UI_SET(stat_followers_killed,   "followers_killed");
        UI_SET(stat_current_score,      "current_score");
    } else {
        missing_group(res, "stats");
    }

    cJSON *jav = cJSON_GetObjectItem(root_strings, "army_view");
    if (cJSON_IsObject(jav)) {
        cJSON *obj = jav;
        UI_SET(army_skill,      "skill");
        UI_SET(army_move,       "move");
        UI_SET(army_morale,     "morale");
        UI_SET(army_hit_points, "hit_points");
        UI_SET(army_damage,     "damage");
        UI_SET(army_g_cost,     "g_cost");
    } else {
        missing_group(res, "army_view");
    }

    cJSON *jmor = cJSON_GetObjectItem(root_strings, "morale");
    if (cJSON_IsObject(jmor)) {
        cJSON *obj = jmor;
        UI_SET(morale_normal, "normal");
        UI_SET(morale_low,    "low");
        UI_SET(morale_high,   "high");
    } else {
        missing_group(res, "morale");
    }

    cJSON *jcb = cJSON_GetObjectItem(root_strings, "count_buckets");
    if (cJSON_IsObject(jcb)) {
        parse_count_buckets(&ui->count_buckets_army_view,
                            &ui->count_buckets_army_view_n,
                            cJSON_GetObjectItem(jcb, "army_view"));
        parse_count_buckets(&ui->count_buckets_instant_army,
                            &ui->count_buckets_instant_army_n,
                            cJSON_GetObjectItem(jcb, "instant_army"));
    }

    cJSON *jdiff = cJSON_GetObjectItem(root_strings, "difficulty");
    if (cJSON_IsObject(jdiff)) {
        static const char *keys[4] = { "easy", "normal", "hard", "impossible" };
        for (int i = 0; i < 4; i++) {
            cJSON *e = cJSON_GetObjectItem(jdiff, keys[i]);
            if (!cJSON_IsObject(e)) continue;
            const char *l = res_json_str(e, "label", NULL);
            const char *m = res_json_str(e, "score_mult", NULL);
            if (l) res_copy_str(ui->difficulty[i].label,
                            sizeof(ui->difficulty[i].label), l);
            if (m) res_copy_str(ui->difficulty[i].score_mult,
                            sizeof(ui->difficulty[i].score_mult), m);
        }
    }

    cJSON *jkb = cJSON_GetObjectItem(root_strings, "keybinds");
    int kb_cap = res_json_len(jkb);
    int kb_n = 0;
    if (cJSON_IsArray(jkb) && RES_TABLE_ALLOC(ui->keybinds, kb_n, kb_cap)) {
        (void)kb_n;
        int n = 0;
        cJSON *e;
        cJSON_ArrayForEach(e, jkb) {
            if (n >= kb_cap) break;
            if (!cJSON_IsObject(e)) continue;
            res_copy_str(ui->keybinds[n].key, sizeof(ui->keybinds[n].key),
                     res_json_str(e, "key", ""));
            res_copy_str(ui->keybinds[n].label, sizeof(ui->keybinds[n].label),
                     res_json_str(e, "label", ""));
            n++;
        }
        ui->keybind_count = n;
    }

    cJSON *jstart = cJSON_GetObjectItem(root_strings, "startup");
    if (cJSON_IsObject(jstart)) {
        cJSON *obj = jstart;
        UI_SET(startup_controls_hint,        "controls_hint");
        UI_SET(startup_class_select_hint,    "class_select_hint");
        UI_SET(startup_class_picker_missing, "class_picker_missing");
        UI_SET(startup_save_picker_title,    "save_picker_title");
        UI_SET(startup_save_picker_empty,    "save_picker_empty");
        UI_SET(startup_save_picker_new_game, "save_picker_new_game");
        UI_SET(startup_new_game_table_header,"new_game_table_header");
        UI_SET(startup_new_game_select_hint, "new_game_select_hint");
    } else {
        missing_group(res, "startup");
    }

    cJSON *jctl = cJSON_GetObjectItem(root_strings, "controls");
    if (cJSON_IsObject(jctl)) {
        cJSON *obj = jctl;
        UI_SET(controls_title, "title");
        UI_SET(controls_on,    "on");
        UI_SET(controls_off,   "off");
    } else {
        missing_group(res, "controls");
    }

    cJSON *jpr = cJSON_GetObjectItem(root_strings, "prompts");
    if (cJSON_IsObject(jpr)) {
        cJSON *obj = jpr;
        UI_SET(prompt_text_hint,          "text_hint");
        UI_SET(prompt_numeric_range_hint, "numeric_range_hint");
        UI_SET(prompt_yes_no_hint,        "yes_no_hint");
        UI_SET(prompt_yes,                "yes");
        UI_SET(prompt_no,                 "no");
        UI_SET(prompt_numeric_5_hint,     "numeric_5_hint");
    } else {
        missing_group(res, "prompts");
    }

    cJSON *jmisc = cJSON_GetObjectItem(root_strings, "ui");
    if (cJSON_IsObject(jmisc)) {
        cJSON *obj = jmisc;
        UI_SET(combat_spells_title,      "combat_spells_title");
        UI_SET(combat_spells_col_combat, "combat_spells_col_combat");
        UI_SET(combat_spells_prompt,     "combat_spells_prompt");
        UI_SET(combat_moves,             "combat_moves");
        UI_SET(combat_shots,             "combat_shots");
        UI_SET(combat_round,             "combat_round");
        UI_SET(dwelling_kind_plains,       "dwelling_kind_plains");
        UI_SET(dwelling_kind_forest,       "dwelling_kind_forest");
        UI_SET(dwelling_kind_hill,         "dwelling_kind_hill");
        UI_SET(dwelling_kind_dungeon,      "dwelling_kind_dungeon");
        UI_SET(dwelling_recruit_how_many,  "dwelling_recruit_how_many");
        UI_SET(dwelling_info_available,    "dwelling_info_available");
        UI_SET(dwelling_info_cost,         "dwelling_info_cost");
        UI_SET(dwelling_info_gold,         "dwelling_info_gold");
        UI_SET(dwelling_info_recruit_cap,  "dwelling_info_recruit_cap");
        UI_SET(recruit_soldiers_title,    "recruit_soldiers_title");
        UI_SET(recruit_soldiers_how_many, "recruit_soldiers_how_many");
        UI_SET(own_castle_mode_garrison, "own_castle_mode_garrison");
        UI_SET(own_castle_mode_remove,   "own_castle_mode_remove");
        UI_SET(home_castle_recruit, "home_castle_recruit");
        UI_SET(home_castle_audience, "home_castle_audience");
        UI_SET(own_castle_row_garrison, "own_castle_row_garrison");
        UI_SET(own_castle_row_remove, "own_castle_row_remove");
        UI_SET(worldmap_row_your_map, "worldmap_row_your_map");
        UI_SET(worldmap_row_whole_map, "worldmap_row_whole_map");
        UI_SET(title_new_adventure, "title_new_adventure");
        UI_SET(title_load_adventure, "title_load_adventure");
        UI_SET(title_credits, "title_credits");
        // Optional: required only of a pack with an intro (parse_intro checks).
        res_copy_str(ui->title_intro, sizeof ui->title_intro, res_json_str(obj, "title_intro", ""));
        UI_SET(new_game_confirm, "new_game_confirm");
        UI_SET(hero_name_label, "hero_name_label");
        UI_SET(gate_title_town,          "gate_title_town");
        UI_SET(gate_title_castle,        "gate_title_castle");
        UI_SET(gate_footer_hint,         "gate_footer_hint");
        UI_SET(recruit_col_hint,         "recruit_col_hint");
    }

    cJSON *jtoasts = cJSON_GetObjectItem(root_strings, "toasts");
    if (cJSON_IsObject(jtoasts)) {
        cJSON *obj = jtoasts;
        UI_SET(toast_save_cancelled, "save_cancelled");
        UI_SET(toast_save_ok,        "save_ok");
        UI_SET(toast_save_failed,    "save_failed");
        UI_SET(toast_load_cancelled, "load_cancelled");
        UI_SET(toast_load_ok,        "load_ok");
        UI_SET(toast_load_failed,    "load_failed");
        UI_SET(toast_new_game,       "new_game");
    } else {
        missing_group(res, "toasts");
    }

    cJSON *jcv = cJSON_GetObjectItem(root_strings, "contract_view");
    if (cJSON_IsObject(jcv)) {
        cJSON *obj = jcv;
        UI_SET(cv_title_no_contract, "title_no_contract");
        UI_SET(cv_label_name,        "label_name");
        UI_SET(cv_label_alias,       "label_alias");
        UI_SET(cv_label_reward,      "label_reward");
        UI_SET(cv_label_last_seen,   "label_last_seen");
        UI_SET(cv_label_castle,      "label_castle");
        UI_SET(cv_alias_none,        "alias_none");
        UI_SET(cv_castle_unknown,    "castle_unknown");
        UI_SET(cv_features_header,   "features_header");
        UI_SET(cv_crimes_header,     "crimes_header");
    } else {
        missing_group(res, "contract_view");
    }

    cJSON *jsv = cJSON_GetObjectItem(root_strings, "spells_view");
    if (cJSON_IsObject(jsv)) {
        cJSON *obj = jsv;
        UI_SET(sv_title,         "title");
        UI_SET(sv_combat_col,    "combat_col");
        UI_SET(sv_adventure_col, "adventure_col");
    } else {
        missing_group(res, "spells_view");
    }

    cJSON *jdt = cJSON_GetObjectItem(root_strings, "dialog_titles");
    if (cJSON_IsObject(jdt)) {
        cJSON *obj = jdt;
        UI_SET(dt_teleport_cave,  "teleport_cave");
        UI_SET(dt_crystal_ball,   "crystal_ball");
        UI_SET(dt_foes,           "foes");
        UI_SET(dt_alcove_offer,   "alcove_offer");
        UI_SET(dt_alcove_result,  "alcove_result");
        UI_SET(dt_search,         "search");
        UI_SET(dt_dismiss_army,   "dismiss_army");
        UI_SET(dt_dismiss_last,   "dismiss_last");
        UI_SET(dt_navigate,       "navigate");
        UI_SET(dt_lose_fallback,  "lose_fallback");
        UI_SET(dt_win_fallback,   "win_fallback");
        UI_SET(dt_combat_victory, "combat_victory");
    } else {
        missing_group(res, "dialog_titles");
    }
}

#undef UI_SET

void res_parse_strings(Resources *res, cJSON *obj) {
    // Banners + UI labels load defaults even when strings/* is missing, so
    // every dialog and HUD draw call has a body to render.
    parse_banners(&res->banners,
                  cJSON_IsObject(obj) ? cJSON_GetObjectItem(obj, "banners") : NULL, res);
    parse_combat_log(&res->combat_log,
                     cJSON_IsObject(obj) ? cJSON_GetObjectItem(obj, "combat_log") : NULL, res);
    parse_ui(res, obj);
    if (!cJSON_IsObject(obj)) return;
    // Each class's words: banners.class_desc_<its id>, for whatever classes
    // the pack declares.
    {
        cJSON *bo = cJSON_GetObjectItem(obj, "banners");
        for (int i = 0; i < res->classes_count && res->class_hero; i++) {
            char key[64];
            snprintf(key, sizeof key, "class_desc_%s", res->classes[i].id);
            const char *d = cJSON_IsObject(bo) ? res_json_str(bo, key, NULL) : NULL;
            res_copy_str(res->class_hero[i].desc, sizeof res->class_hero[i].desc, d ? d : "");
        }
    }
    parse_end_text(&res->win_text,  cJSON_GetObjectItem(obj, "win"));
    parse_end_text(&res->lose_text, cJSON_GetObjectItem(obj, "lose"));

    cJSON *vd = cJSON_GetObjectItem(obj, "villain_descriptions");
    int vd_cap = res_json_len(vd);
    if (cJSON_IsObject(vd) && RES_TABLE_ALLOC(res->villain_descs, res->villain_desc_count, vd_cap)) {
        cJSON *entry;
        cJSON_ArrayForEach(entry, vd) {
            if (res->villain_desc_count >= vd_cap) break;
            const char *id = entry->string;
            if (!id || !id[0]) continue;
            ResVillainDesc *d = &res->villain_descs[res->villain_desc_count++];
            memset(d, 0, sizeof(*d));
            res_copy_str(d->id,       sizeof(d->id),       id);
            res_copy_str(d->alias,    sizeof(d->alias),    res_json_str(entry, "alias", ""));
            res_copy_str(d->features, sizeof(d->features), res_json_str(entry, "features", ""));
            res_copy_str(d->crimes,   sizeof(d->crimes),   res_json_str(entry, "crimes", ""));
        }
    }

    cJSON *ti = cJSON_GetObjectItem(obj, "town_invitations");
    int ti_cap = res_json_len(ti);
    if (cJSON_IsObject(ti) && RES_TABLE_ALLOC(res->town_invites, res->town_invite_count, ti_cap)) {
        cJSON *entry;
        cJSON_ArrayForEach(entry, ti) {
            if (res->town_invite_count >= ti_cap) break;
            if (!entry->string || !entry->string[0] || !cJSON_IsObject(entry)) continue;
            ResTownInvite *v = &res->town_invites[res->town_invite_count++];
            res_copy_str(v->id,          sizeof(v->id),          entry->string);
            res_copy_str(v->contracts,   sizeof(v->contracts),   res_json_str(entry, "contracts", ""));
            res_copy_str(v->boat,        sizeof(v->boat),        res_json_str(entry, "boat", ""));
            res_copy_str(v->information, sizeof(v->information), res_json_str(entry, "information", ""));
            res_copy_str(v->temple,      sizeof(v->temple),      res_json_str(entry, "temple", ""));
            res_copy_str(v->siege,       sizeof(v->siege),       res_json_str(entry, "siege", ""));
        }
    }

    cJSON *td = cJSON_GetObjectItem(obj, "town_docks");
    int td_cap = res_json_len(td);
    if (cJSON_IsObject(td) && RES_TABLE_ALLOC(res->town_docks, res->town_dock_count, td_cap)) {
        cJSON *entry;
        cJSON_ArrayForEach(entry, td) {
            if (res->town_dock_count >= td_cap) break;
            if (!entry->string || !entry->string[0] || !cJSON_IsString(entry)) continue;
            ResTownDock *d = &res->town_docks[res->town_dock_count++];
            res_copy_str(d->id,   sizeof(d->id),   entry->string);
            res_copy_str(d->text, sizeof(d->text), entry->valuestring);
        }
    }

    cJSON *sl = cJSON_GetObjectItem(obj, "spell_lore");
    int sl_cap = res_json_len(sl);
    if (cJSON_IsObject(sl) && RES_TABLE_ALLOC(res->spell_lore, res->spell_lore_count, sl_cap)) {
        cJSON *entry;
        cJSON_ArrayForEach(entry, sl) {
            if (res->spell_lore_count >= sl_cap) break;
            if (!entry->string || !entry->string[0] || !cJSON_IsString(entry)) continue;
            ResSpellLore *l = &res->spell_lore[res->spell_lore_count++];
            res_copy_str(l->id,   sizeof(l->id),   entry->string);
            res_copy_str(l->text, sizeof(l->text), entry->valuestring);
        }
    }

    cJSON *sb = cJSON_GetObjectItem(obj, "spell_brief");
    int sb_cap = res_json_len(sb);
    if (cJSON_IsObject(sb) && RES_TABLE_ALLOC(res->spell_brief, res->spell_brief_count, sb_cap)) {
        cJSON *entry;
        cJSON_ArrayForEach(entry, sb) {
            if (res->spell_brief_count >= sb_cap) break;
            if (!entry->string || !entry->string[0] || !cJSON_IsString(entry)) continue;
            ResSpellLore *l = &res->spell_brief[res->spell_brief_count++];
            res_copy_str(l->id,   sizeof(l->id),   entry->string);
            res_copy_str(l->text, sizeof(l->text), entry->valuestring);
        }
    }
}
