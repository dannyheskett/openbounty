// Generic per-zone autoplay solver. See missions.h.

#include "autoplay/missions.h"
#include "autoplay/macros.h"
#include "autoplay/nav.h"

#include "raylib.h"
#include "ui.h"
#include "views.h"
#include "prompt.h"
#include "tables.h"
#include "pending.h"
#include "combat.h"

#include <stdio.h>
#include <string.h>

#define MAX_ZONES 4

// -------------------------------------------------------------------------
// Runtime queries
// -------------------------------------------------------------------------

bool ap_find_interact(const Map *m, int want_interact,
                      const char *want_id_or_null,
                      int *out_x, int *out_y) {
    if (!m) return false;
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            const Tile *t = &m->tiles[y][x];
            if ((int)t->interactive != want_interact) continue;
            if (want_id_or_null && want_id_or_null[0] &&
                strcmp(t->id, want_id_or_null) != 0) continue;
            *out_x = x; *out_y = y;
            return true;
        }
    }
    return false;
}

bool ap_foe_in_pursuit_range(const Game *g, int x, int y) {
    if (!g) return false;
    for (int i = 0; i < g->foe_count; i++) {
        const FoeState *f = &g->foes[i];
        if (!f->alive || f->friendly) continue;
        if (strcmp(f->zone, g->position.zone) != 0) continue;
        int dx = (f->x > x) ? f->x - x : x - f->x;
        int dy = (f->y > y) ? f->y - y : y - f->y;
        if (dx <= 2 && dy <= 2) return true;
    }
    return false;
}

static bool is_acquisition(Interact ix) {
    return ix == INTERACT_TREASURE_CHEST ||
           ix == INTERACT_ARTIFACT ||
           ix == INTERACT_NAVMAP ||
           ix == INTERACT_ORB;
}

static int manhattan(int ax, int ay, int bx, int by) {
    int dx = (ax > bx) ? ax - bx : bx - ax;
    int dy = (ay > by) ? ay - by : by - ay;
    return dx + dy;
}

bool ap_pick_safe_acquisition(const Game *g, const Map *m,
                              int *out_x, int *out_y) {
    if (!g || !m) return false;
    int best_x = -1, best_y = -1, best_d = -1;
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            const Tile *t = &m->tiles[y][x];
            if (!is_acquisition(t->interactive)) continue;
            if (ap_foe_in_pursuit_range(g, x, y)) continue;
            int d = manhattan(g->position.x, g->position.y, x, y);
            if (best_d < 0 || d < best_d) {
                best_d = d; best_x = x; best_y = y;
            }
        }
    }
    if (best_d < 0) return false;
    *out_x = best_x; *out_y = best_y;
    return true;
}

bool ap_pick_any_acquisition(const Game *g, const Map *m,
                             int *out_x, int *out_y) {
    if (!g || !m) return false;
    int best_x = -1, best_y = -1, best_d = -1;
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            const Tile *t = &m->tiles[y][x];
            if (!is_acquisition(t->interactive)) continue;
            int d = manhattan(g->position.x, g->position.y, x, y);
            if (best_d < 0 || d < best_d) {
                best_d = d; best_x = x; best_y = y;
            }
        }
    }
    if (best_d < 0) return false;
    *out_x = best_x; *out_y = best_y;
    return true;
}

int ap_estimate_foe_hp(const FoeState *foe) {
    if (!foe) return 0;
    int hp = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (!foe->garrison[i].id[0] || foe->garrison[i].count == 0) continue;
        const TroopDef *t = troop_by_id(foe->garrison[i].id);
        if (t) hp += t->hit_points * foe->garrison[i].count;
    }
    return hp;
}

static int castle_garrison_hp(const CastleRecord *cr) {
    if (!cr) return 0;
    int hp = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (!cr->garrison[i].id[0] || cr->garrison[i].count == 0) continue;
        const TroopDef *t = troop_by_id(cr->garrison[i].id);
        if (t) hp += t->hit_points * cr->garrison[i].count;
    }
    return hp;
}

const CastleRecord *ap_pick_weakest_monster_castle(
    const Game *g, const char *zone_id) {
    const CastleRecord *best = NULL;
    int best_hp = 0;
    for (int i = 0; i < GAME_CASTLES; i++) {
        const CastleRecord *cr = &g->castles[i];
        if (!cr->id[0]) continue;
        if (cr->owner_kind != CASTLE_OWNER_MONSTERS) continue;
        const ResCastle *rc = g->res
            ? resources_castle_by_id(g->res, cr->id) : NULL;
        if (!rc || strcmp(rc->zone, zone_id) != 0) continue;
        int hp = castle_garrison_hp(cr);
        if (!best || hp < best_hp) {
            best = cr; best_hp = hp;
        }
    }
    return best;
}

static bool villain_caught(const Game *g, const char *villain_id) {
    const VillainDef *v = villain_by_id(villain_id);
    if (!v) return false;
    if (v->index < 0 || v->index >= (int)(sizeof(g->contract.villains_caught) /
                                          sizeof(g->contract.villains_caught[0])))
        return false;
    return g->contract.villains_caught[v->index];
}

const CastleRecord *ap_pick_next_villain(
    const Game *g, const char *zone_id) {
    for (int i = 0; i < GAME_CASTLES; i++) {
        const CastleRecord *cr = &g->castles[i];
        if (!cr->id[0]) continue;
        if (cr->owner_kind != CASTLE_OWNER_VILLAIN) continue;
        if (villain_caught(g, cr->villain_id)) continue;
        const ResCastle *rc = g->res
            ? resources_castle_by_id(g->res, cr->id) : NULL;
        if (!rc || strcmp(rc->zone, zone_id) != 0) continue;
        return cr;
    }
    return NULL;
}

bool ap_pick_next_unsolved_zone(const Game *g, const AutoplayState *st,
                                char *out_zone, int cap) {
    if (!g || !g->res) return false;
    for (int i = 0; i < g->res->zone_count && i < MAX_ZONES; i++) {
        if (!g->world.zones_discovered[i]) continue;
        if (st->zone_solved[i]) continue;
        const ResZone *z = &g->res->zones[i];
        int k = 0;
        while (k + 1 < cap && z->id[k]) { out_zone[k] = z->id[k]; k++; }
        out_zone[k] = '\0';
        return true;
    }
    return false;
}

bool ap_pick_any_town(const Map *m, int *out_x, int *out_y,
                      char *out_id, int id_cap) {
    if (!m) return false;
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            const Tile *t = &m->tiles[y][x];
            if (t->interactive != INTERACT_TOWN) continue;
            *out_x = x; *out_y = y;
            int k = 0;
            while (k + 1 < id_cap && t->id[k]) { out_id[k] = t->id[k]; k++; }
            out_id[k] = '\0';
            return true;
        }
    }
    return false;
}

// Pick the town nearest the hero in the current zone.
static bool pick_nearest_town(const Game *g, const Map *m,
                              char *out_id, int id_cap) {
    if (!g || !m) return false;
    int best_d = -1;
    int best_x = -1, best_y = -1;
    const char *best_id = NULL;
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            const Tile *t = &m->tiles[y][x];
            if (t->interactive != INTERACT_TOWN) continue;
            int d = manhattan(g->position.x, g->position.y, x, y);
            if (best_d < 0 || d < best_d) {
                best_d = d; best_x = x; best_y = y;
                best_id = t->id;
            }
        }
    }
    if (best_d < 0) return false;
    (void)best_x; (void)best_y;
    int k = 0;
    while (k + 1 < id_cap && best_id[k]) { out_id[k] = best_id[k]; k++; }
    out_id[k] = '\0';
    return true;
}

int ap_leadership_until_next_rank(const Game *g) {
    if (!g) return 0;
    const ClassDef *cls = class_by_id(g->character.cls.id);
    if (!cls) return 0;
    int r = g->character.cls.rank_index;
    if (r + 1 >= cls->rank_count) return 0;
    int target = cls->ranks[r + 1].leadership;
    int have = g->stats.leadership_base;
    return (target > have) ? target - have : 0;
}

// -------------------------------------------------------------------------
// Mission dispatcher
// -------------------------------------------------------------------------

const char *ap_mission_name(int k) {
    switch (k) {
    case MISSION_INTRO:             return "INTRO";
    case MISSION_VISIT_TOWN:        return "VISIT_TOWN";
    case MISSION_STARTUP_RECRUIT:   return "STARTUP_RECRUIT";
    case MISSION_ALCOVE:            return "ALCOVE";
    case MISSION_PATHS_END_SPELLS:  return "PATHS_END_SPELLS";
    case MISSION_GHOST_DWELLING:    return "GHOST_DWELLING";
    case MISSION_SAFE_ACQUIRE:      return "SAFE_ACQUIRE";
    case MISSION_REHOME_RECRUIT:    return "REHOME_RECRUIT";
    case MISSION_CHEST_GRIND:       return "CHEST_GRIND";
    case MISSION_MONSTER_GRIND:     return "MONSTER_GRIND";
    case MISSION_VILLAIN_GRIND:     return "VILLAIN_GRIND";
    case MISSION_SAIL_TO_NEXT:      return "SAIL_TO_NEXT";
    case MISSION_RENT_BOAT:         return "RENT_BOAT";
    case MISSION_DONE:              return "DONE";
    }
    return "?";
}
#define mission_name(k) ap_mission_name(k)

static void advance_to(AutoplayState *st, int mission, const char *zone) {
    st->mission_kind = mission;
    st->mission_substep = 0;
    st->module_scratch[14] = -1;
    st->module_scratch[15] = -1;
    if (zone) {
        size_t k = 0;
        while (k + 1 < sizeof(st->mission_zone) && zone[k]) {
            st->mission_zone[k] = zone[k]; k++;
        }
        st->mission_zone[k] = '\0';
    }
    AP_LOG("[mission] -> %s zone=%s",
           mission_name(mission), st->mission_zone);
}

void ap_mission_reset(AutoplayState *st) {
    st->mission_kind = MISSION_INTRO;
    st->mission_substep = 0;
    st->mission_zone[0] = '\0';
    for (int i = 0; i < MAX_ZONES; i++) st->zone_solved[i] = false;
    AP_LOG("[mission] reset -> INTRO");
}

// -------------------------------------------------------------------------
// Individual mission handlers — each returns one ApCmd per tick.
// They drive sub-state via st->module_scratch[].
// -------------------------------------------------------------------------

// MISSION_INTRO: dismiss the intro dialog, advance.
static ApCmd handle_intro(const Game *g, const Map *m,
                          AutoplayState *st,
                          bool *out_phase_done,
                          AutoplayPhase *out_next_phase) {
    (void)m; (void)out_phase_done; (void)out_next_phase;
    if (dialog_is_active()) {
        return (ApCmd){ "INTRO:space", KEY_SPACE,
                        assert_dialog_closed };
    }
    advance_to(st, MISSION_VISIT_TOWN, g->position.zone);
    return (ApCmd){ "INTRO:done", 0, assert_always_true };
}

// MISSION_VISIT_TOWN: walk to nearest town, buy siege + boat with
// the starter gold, then exit. Done when both owned OR can't afford
// what's missing.
static ApCmd handle_visit_town(const Game *g, const Map *m,
                               AutoplayState *st,
                               bool *out_phase_done,
                               AutoplayPhase *out_next_phase) {
    const int siege_cost = g->res ? g->res->economy.siege_cost : 3000;
    const int boat_cost  = g->res ? g->res->economy.boat_cost_normal : 500;
    bool need_boat  = !g->boat.has_boat && g->stats.gold > boat_cost;
    bool need_siege = !g->stats.siege_weapons &&
                      g->stats.gold > siege_cost;
    if (!need_boat && !need_siege) {
        if (views_active() == VIEW_TOWN) {
            return (ApCmd){ "VISIT:esc_town", KEY_ESCAPE,
                            assert_always_true };
        }
        if (views_active() != VIEW_NONE) {
            return (ApCmd){ "VISIT:esc_other", KEY_ESCAPE,
                            assert_always_true };
        }
        AP_LOG("[mission] VISIT_TOWN done: siege=%d boat=%d gold=%d",
               (int)g->stats.siege_weapons,
               (int)g->boat.has_boat, g->stats.gold);
        advance_to(st, MISSION_STARTUP_RECRUIT, g->position.zone);
        return (ApCmd){ "VISIT:done", 0, assert_always_true };
    }
    if (views_town_info_text() != NULL) {
        return (ApCmd){ "VISIT:space_info", KEY_SPACE,
                        assert_always_true };
    }
    if (dialog_is_active()) {
        return (ApCmd){ "VISIT:space", KEY_SPACE,
                        assert_dialog_closed };
    }
    if (views_active() == VIEW_TOWN) {
        if (need_siege) {
            return (ApCmd){ "VISIT:e_siege", KEY_E,
                            assert_always_true };
        }
        return (ApCmd){ "VISIT:b_boat", KEY_B,
                        assert_always_true };
    }
    // Not in town yet — nav to nearest.
    char tid[24];
    if (!pick_nearest_town(g, m, tid, sizeof tid)) {
        AP_LOG("[mission] VISIT_TOWN: no town in zone — HALT");
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_DONE;
        return (ApCmd){ "VISIT:no_town", 0, assert_always_true };
    }
    return ap_nav_to_town(g, m, st, tid,
                          (AutoplayPhase)0, (AutoplayPhase)0,
                          out_phase_done, out_next_phase);
}

// MISSION_STARTUP_RECRUIT: walk to maximus castle, row-buy A..E with
// 9999 each, exit.
static ApCmd handle_startup_recruit(const Game *g, const Map *m,
                                    AutoplayState *st,
                                    bool *out_phase_done,
                                    AutoplayPhase *out_next_phase) {
    (void)m; (void)out_phase_done; (void)out_next_phase;
    int sub = st->mission_substep;

    // sub 0: walk to gate (UP from spawn).
    if (sub == 0) {
        if (views_active() == VIEW_HOME_CASTLE) {
            st->mission_substep = 1;
            return (ApCmd){ "STARTUP_RECRUIT:in_castle", 0,
                            assert_always_true };
        }
        if (dialog_is_active()) {
            return (ApCmd){ "STARTUP_RECRUIT:dlg", KEY_SPACE,
                            assert_dialog_closed };
        }
        // Heading to (11,57). Use ap_nav_step if not at gate-south.
        if (g->position.x != 11 || g->position.y != 57) {
            int key = ap_nav_step(g, m, 11, 57);
            if (key == 0) {
                return (ApCmd){ "STARTUP_RECRUIT:up", KEY_UP,
                                assert_always_true };
            }
            return (ApCmd){ "STARTUP_RECRUIT:nav", key,
                            assert_always_true };
        }
        return (ApCmd){ "STARTUP_RECRUIT:up", KEY_UP,
                        assert_always_true };
    }
    // sub 1: press A to open recruit.
    if (sub == 1) {
        if (views_active() == VIEW_RECRUIT_SOLDIERS) {
            st->mission_substep = 2;
            st->module_scratch[11] = 0;
            return (ApCmd){ "STARTUP_RECRUIT:in_recruit", 0,
                            assert_always_true };
        }
        return (ApCmd){ "STARTUP_RECRUIT:a", KEY_A,
                        assert_always_true };
    }
    // sub 2: row-buy loop.
    if (sub == 2) {
        int s = st->module_scratch[11];
        if (s < 0) s = 0;
        int row = s >> 3;
        int ks  = s & 7;
        if (row >= 5) {
            st->mission_substep = 3;
            return (ApCmd){ "STARTUP_RECRUIT:esc_recruit",
                            KEY_ESCAPE, assert_always_true };
        }
        switch (ks) {
        case 0: st->module_scratch[11] = (row << 3) | 1;
            return (ApCmd){ "STARTUP_RECRUIT:letter", KEY_A + row,
                            assert_always_true };
        case 1: case 2: case 3:
            st->module_scratch[11] = (row << 3) | (ks + 1);
            return (ApCmd){ "STARTUP_RECRUIT:9", KEY_NINE,
                            assert_always_true };
        default:
            st->module_scratch[11] = ((row + 1) << 3) | 0;
            return (ApCmd){ "STARTUP_RECRUIT:enter", KEY_ENTER,
                            assert_always_true };
        }
    }
    // sub 3: ESC out of castle.
    if (views_active() == VIEW_NONE) {
        AP_LOG("[mission] STARTUP_RECRUIT done: gold=%d hp=%d",
               g->stats.gold, ap_army_total_hp(g));
        advance_to(st, MISSION_SAFE_ACQUIRE, g->position.zone);
        return (ApCmd){ "STARTUP_RECRUIT:done", 0,
                        assert_always_true };
    }
    return (ApCmd){ "STARTUP_RECRUIT:esc_castle", KEY_ESCAPE,
                    assert_always_true };
}

// MISSION_SAFE_ACQUIRE: pick nearest acquisition tile not in foe
// pursuit range, nav using foe-avoiding step. When none remain,
// advance.
static ApCmd handle_safe_acquire(const Game *g, const Map *m,
                                 AutoplayState *st,
                                 bool *out_phase_done,
                                 AutoplayPhase *out_next_phase) {
    (void)out_phase_done; (void)out_next_phase;
    static char cmd[64];
    const char *zone = st->mission_zone;
    if (strcmp(g->position.zone, zone) != 0) {
        // Wrong zone — should have been handled by SAIL_TO_NEXT.
        advance_to(st, MISSION_REHOME_RECRUIT, g->position.zone);
        return (ApCmd){ "SAFE:wrong_zone", 0, assert_always_true };
    }
    if (prompt_is_active()) {
        const char *kind = prompt_kind_str();
        if (kind && strcmp(kind, "yes_no") == 0) {
            // Foe encounter — route through combat. POST_COMBAT
            // returns us to the mission dispatcher.
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_COMBAT;
            return (ApCmd){ "SAFE:y_foe", KEY_Y, assert_always_true };
        }
        if (kind && strcmp(kind, "text") == 0) {
            return (ApCmd){ "SAFE:enter", KEY_ENTER,
                            assert_prompt_gone };
        }
        // Chest A/B prompt — leadership while below next rank, else gold.
        int letter = (ap_leadership_until_next_rank(g) > 0)
                         ? KEY_B : KEY_A;
        return (ApCmd){ "SAFE:chest", letter, assert_prompt_gone };
    }
    if (dialog_is_active()) {
        return (ApCmd){ "SAFE:space", KEY_SPACE,
                        assert_dialog_closed };
    }
    // Sticky target — stored in module_scratch[14],[15]. Re-pick
    // only when the target tile no longer has an acquisition
    // interactive, or when we never picked.
    int tx = st->module_scratch[14];
    int ty = st->module_scratch[15];
    bool need_pick = (tx < 0 || ty < 0);
    if (!need_pick) {
        const Tile *t = MapGetTile(m, tx, ty);
        if (!t || !is_acquisition(t->interactive) ||
            ap_foe_in_pursuit_range(g, tx, ty)) {
            need_pick = true;
        }
    }
    if (need_pick) {
        if (!ap_pick_safe_acquisition(g, m, &tx, &ty)) {
            AP_LOG("[mission] SAFE_ACQUIRE done in %s — advancing", zone);
            st->module_scratch[14] = -1;
            st->module_scratch[15] = -1;
            advance_to(st, MISSION_REHOME_RECRUIT, zone);
            return (ApCmd){ "SAFE:done", 0, assert_always_true };
        }
        st->module_scratch[14] = tx;
        st->module_scratch[15] = ty;
        const Tile *gt = MapGetTile(m, tx, ty);
        AP_LOG("[mission] SAFE_ACQUIRE new target (%d,%d) "
               "interact=%d id='%s' from pos=(%d,%d)",
               tx, ty,
               gt ? (int)gt->interactive : -1,
               gt ? gt->id : "?",
               g->position.x, g->position.y);
    }
    int key = ap_nav_step_avoiding_foes(g, m, tx, ty);
    if (key == 0) {
        if (!g->boat.has_boat && g->stats.gold > 500) {
            st->mission_resume_kind = MISSION_SAFE_ACQUIRE;
            AP_LOG("[mission] SAFE: no foe-safe path to (%d,%d) "
                   "boat=0 — diverting to RENT_BOAT "
                   "(resume=%d=%s)", tx, ty,
                   st->mission_resume_kind,
                   ap_mission_name(st->mission_resume_kind));
            advance_to(st, MISSION_RENT_BOAT, zone);
            return (ApCmd){ "SAFE:divert_rent", 0,
                            assert_always_true };
        }
        AP_LOG("[mission] SAFE: no safe path to (%d,%d) — advancing",
               tx, ty);
        st->module_scratch[14] = -1;
        st->module_scratch[15] = -1;
        advance_to(st, MISSION_REHOME_RECRUIT, zone);
        return (ApCmd){ "SAFE:no_path", 0, assert_always_true };
    }
    snprintf(cmd, sizeof cmd, "SAFE:nav(%d,%d)", tx, ty);
    return (ApCmd){ cmd, key, assert_always_true };
}

// MISSION_REHOME_RECRUIT: delegate to ap_rehome_and_recruit; on
// completion advance to CHEST_GRIND.
static ApCmd handle_rehome_recruit(const Game *g, const Map *m,
                                   AutoplayState *st,
                                   bool *out_phase_done,
                                   AutoplayPhase *out_next_phase) {
    ApCmd r = ap_rehome_and_recruit(g, m, st, /*reserve=*/0,
        (AutoplayPhase)0, (AutoplayPhase)0,
        out_phase_done, out_next_phase);
    if (*out_phase_done) {
        *out_phase_done = false;
        advance_to(st, MISSION_CHEST_GRIND, st->mission_zone);
    }
    return r;
}

// MISSION_CHEST_GRIND: nearest acquisition, foes accepted.
static ApCmd handle_chest_grind(const Game *g, const Map *m,
                                AutoplayState *st,
                                bool *out_phase_done,
                                AutoplayPhase *out_next_phase) {
    (void)out_phase_done; (void)out_next_phase;
    static char cmd[64];
    const char *zone = st->mission_zone;
    if (strcmp(g->position.zone, zone) != 0) {
        advance_to(st, MISSION_MONSTER_GRIND, zone);
        return (ApCmd){ "GRIND:wrong_zone", 0, assert_always_true };
    }
    if (prompt_is_active()) {
        const char *kind = prompt_kind_str();
        if (kind && strcmp(kind, "yes_no") == 0) {
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_COMBAT;
            return (ApCmd){ "GRIND:y_foe", KEY_Y,
                            assert_always_true };
        }
        if (kind && strcmp(kind, "text") == 0) {
            return (ApCmd){ "GRIND:enter", KEY_ENTER,
                            assert_prompt_gone };
        }
        int letter = (ap_leadership_until_next_rank(g) > 0)
                         ? KEY_B : KEY_A;
        return (ApCmd){ "GRIND:chest", letter,
                        assert_prompt_gone };
    }
    if (dialog_is_active()) {
        return (ApCmd){ "GRIND:space", KEY_SPACE,
                        assert_dialog_closed };
    }
    int tx = st->module_scratch[14];
    int ty = st->module_scratch[15];
    bool need_pick = (tx < 0 || ty < 0);
    if (!need_pick) {
        const Tile *t = MapGetTile(m, tx, ty);
        if (!t || !is_acquisition(t->interactive)) need_pick = true;
    }
    if (need_pick) {
        if (!ap_pick_any_acquisition(g, m, &tx, &ty)) {
            AP_LOG("[mission] CHEST_GRIND done in %s", zone);
            st->module_scratch[14] = -1;
            st->module_scratch[15] = -1;
            advance_to(st, MISSION_MONSTER_GRIND, zone);
            return (ApCmd){ "GRIND:done", 0, assert_always_true };
        }
        st->module_scratch[14] = tx;
        st->module_scratch[15] = ty;
        const Tile *gt = MapGetTile(m, tx, ty);
        AP_LOG("[mission] CHEST_GRIND new target (%d,%d) "
               "interact=%d id='%s' from pos=(%d,%d)",
               tx, ty,
               gt ? (int)gt->interactive : -1,
               gt ? gt->id : "?",
               g->position.x, g->position.y);
    }
    int key = ap_nav_step(g, m, tx, ty);
    if (key == 0) {
        // No path. If hero has no boat, try renting one — the goal
        // might be reachable via water. Otherwise halt.
        if (!g->boat.has_boat && g->stats.gold > 500) {
            AP_LOG("[mission] GRIND: no path to (%d,%d) from (%d,%d) "
                   "boat=0 — diverting to RENT_BOAT",
                   tx, ty, g->position.x, g->position.y);
            st->mission_resume_kind = MISSION_CHEST_GRIND;
            advance_to(st, MISSION_RENT_BOAT, zone);
            return (ApCmd){ "GRIND:divert_rent", 0,
                            assert_always_true };
        }
        AP_LOG("[mission] GRIND: no path to (%d,%d) from (%d,%d) "
               "boat=%d gold=%d — HALT",
               tx, ty, g->position.x, g->position.y,
               (int)g->boat.has_boat, g->stats.gold);
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_DONE;
        return (ApCmd){ "GRIND:no_path_halt", 0,
                        assert_always_true };
    }
    snprintf(cmd, sizeof cmd, "GRIND:nav(%d,%d)", tx, ty);
    return (ApCmd){ cmd, key, assert_always_true };
}

// MISSION_MONSTER_GRIND: iterate monster castles in zone. After each
// capture, divert to REHOME_RECRUIT then back here.
static ApCmd handle_monster_grind(const Game *g, const Map *m,
                                  AutoplayState *st,
                                  bool *out_phase_done,
                                  AutoplayPhase *out_next_phase) {
    const char *zone = st->mission_zone;
    if (strcmp(g->position.zone, zone) != 0) {
        advance_to(st, MISSION_VILLAIN_GRIND, zone);
        return (ApCmd){ "MONSTER:wrong_zone", 0,
                        assert_always_true };
    }
    const CastleRecord *cr = ap_pick_weakest_monster_castle(g, zone);
    if (!cr) {
        AP_LOG("[mission] MONSTER_GRIND done in %s", zone);
        advance_to(st, MISSION_VILLAIN_GRIND, zone);
        return (ApCmd){ "MONSTER:done", 0, assert_always_true };
    }
    // Monster castles silently bounce the hero if siege_weapons==0.
    // Divert to a town to buy siege (and a boat if missing).
    if (!g->stats.siege_weapons && g->stats.gold > 3000) {
        AP_LOG("[mission] MONSTER_GRIND: no siege weapons — "
               "diverting to RENT_BOAT/SIEGE");
        st->mission_resume_kind = MISSION_MONSTER_GRIND;
        advance_to(st, MISSION_RENT_BOAT, zone);
        return (ApCmd){ "MONSTER:divert_siege", 0,
                        assert_always_true };
    }
    // Use the existing nav-to-castle helper. After capture the engine
    // opens VIEW_OWN_CASTLE; the helper transitions out, we then
    // re-enter this mission and pick the next castle.
    return ap_nav_to_castle(g, m, st, cr->id,
                            (AutoplayPhase)0, (AutoplayPhase)0,
                            out_phase_done, out_next_phase);
}

// MISSION_VILLAIN_GRIND: per villain in zone:
//   - if no contract: walk to any town in zone, take contract for this zone
//   - then nav to villain castle, fight
//   - on capture, divert to REHOME_RECRUIT, then back here
static ApCmd handle_villain_grind(const Game *g, const Map *m,
                                  AutoplayState *st,
                                  bool *out_phase_done,
                                  AutoplayPhase *out_next_phase) {
    const char *zone = st->mission_zone;
    if (strcmp(g->position.zone, zone) != 0) {
        // Mark zone solved (we may need to revisit) and advance.
        for (int i = 0; i < g->res->zone_count && i < MAX_ZONES; i++) {
            if (strcmp(g->res->zones[i].id, zone) == 0) {
                st->zone_solved[i] = true;
            }
        }
        advance_to(st, MISSION_SAIL_TO_NEXT, "");
        return (ApCmd){ "VILLAIN:wrong_zone", 0,
                        assert_always_true };
    }
    const CastleRecord *cr = ap_pick_next_villain(g, zone);
    if (!cr) {
        AP_LOG("[mission] VILLAIN_GRIND done in %s", zone);
        for (int i = 0; i < g->res->zone_count && i < MAX_ZONES; i++) {
            if (strcmp(g->res->zones[i].id, zone) == 0) {
                st->zone_solved[i] = true;
            }
        }
        advance_to(st, MISSION_SAIL_TO_NEXT, "");
        return (ApCmd){ "VILLAIN:done", 0, assert_always_true };
    }
    // Have contract for this villain? Otherwise go take one.
    if (!g->contract.active_id[0] ||
        strcmp(g->contract.active_id, cr->villain_id) != 0) {
        // Take a contract at any town in zone.
        int tx, ty;
        char tid[24];
        if (!ap_pick_any_town(m, &tx, &ty, tid, sizeof tid)) {
            AP_LOG("[mission] VILLAIN: no town in zone — skip");
            for (int i = 0; i < g->res->zone_count && i < MAX_ZONES; i++) {
                if (strcmp(g->res->zones[i].id, zone) == 0) {
                    st->zone_solved[i] = true;
                }
            }
            advance_to(st, MISSION_SAIL_TO_NEXT, "");
            return (ApCmd){ "VILLAIN:no_town", 0,
                            assert_always_true };
        }
        // In town view: take contract for zone.
        if (views_active() == VIEW_TOWN) {
            char action[48];
            snprintf(action, sizeof action, "contract_zone:%s", zone);
            return ap_town_action(g, m, st, action,
                                  (AutoplayPhase)0, (AutoplayPhase)0,
                                  out_phase_done, out_next_phase);
        }
        // Not in town — nav there.
        return ap_nav_to_town(g, m, st, tid,
                              (AutoplayPhase)0, (AutoplayPhase)0,
                              out_phase_done, out_next_phase);
    }
    // Have correct contract — nav to villain's castle.
    return ap_nav_to_castle(g, m, st, cr->id,
                            (AutoplayPhase)0, (AutoplayPhase)0,
                            out_phase_done, out_next_phase);
}

// MISSION_RENT_BOAT: nav to nearest town, enter VIEW_TOWN, buy
// missing siege weapons + boat, ESC out, then resume the original
// mission (mission_resume_kind).
static ApCmd handle_rent_boat(const Game *g, const Map *m,
                              AutoplayState *st,
                              bool *out_phase_done,
                              AutoplayPhase *out_next_phase) {
    static char cmd[64];
    const int siege_cost = g->res ? g->res->economy.siege_cost : 3000;
    const int boat_cost  = g->res ? g->res->economy.boat_cost_normal : 500;
    bool need_boat  = !g->boat.has_boat && g->stats.gold > boat_cost;
    bool need_siege = !g->stats.siege_weapons &&
                      g->stats.gold > siege_cost;
    // All done (have what we wanted OR can't afford)? Resume.
    if (!need_boat && !need_siege) {
        AP_LOG("[mission] RENT_BOAT done — boat=(%d,%d) siege=1, "
               "resume=%d=%s",
               g->boat.x, g->boat.y,
               st->mission_resume_kind,
               ap_mission_name(st->mission_resume_kind));
        if (views_active() == VIEW_TOWN) {
            return (ApCmd){ "RENT:esc_town", KEY_ESCAPE,
                            assert_always_true };
        }
        if (views_active() != VIEW_NONE) {
            return (ApCmd){ "RENT:esc_other", KEY_ESCAPE,
                            assert_always_true };
        }
        advance_to(st, st->mission_resume_kind, st->mission_zone);
        return (ApCmd){ "RENT:done", 0, assert_always_true };
    }
    if (views_town_info_text() != NULL) {
        return (ApCmd){ "RENT:space_info", KEY_SPACE,
                        assert_always_true };
    }
    if (dialog_is_active()) {
        return (ApCmd){ "RENT:space", KEY_SPACE,
                        assert_dialog_closed };
    }
    if (views_active() == VIEW_TOWN) {
        // In town — buy siege first (E), then boat (B).
        if (need_siege) {
            return (ApCmd){ "RENT:e_siege", KEY_E,
                            assert_always_true };
        }
        return (ApCmd){ "RENT:b", KEY_B, assert_always_true };
    }
    // Find nearest town in current zone.
    char tid[24];
    if (!pick_nearest_town(g, m, tid, sizeof tid)) {
        AP_LOG("[mission] RENT_BOAT: no town in zone — HALT");
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_DONE;
        return (ApCmd){ "RENT:no_town", 0, assert_always_true };
    }
    ApCmd r = ap_nav_to_town(g, m, st, tid,
                             (AutoplayPhase)0, (AutoplayPhase)0,
                             out_phase_done, out_next_phase);
    // ap_nav_to_town sets out_phase_done when VIEW_TOWN opens; we
    // suppress the transition so the next tick re-enters
    // handle_rent_boat with VIEW_TOWN active.
    if (*out_phase_done) {
        *out_phase_done = false;
        *out_next_phase = st->phase;
    }
    snprintf(cmd, sizeof cmd, "RENT:nav[%s]", tid);
    (void)cmd;
    return r;
}

// MISSION_SAIL_TO_NEXT: pick next discovered unsolved zone, sail.
static ApCmd handle_sail_to_next(const Game *g, const Map *m,
                                 AutoplayState *st,
                                 bool *out_phase_done,
                                 AutoplayPhase *out_next_phase) {
    char dest[24];
    if (!ap_pick_next_unsolved_zone(g, st, dest, sizeof dest)) {
        AP_LOG("[mission] all zones solved — game done");
        advance_to(st, MISSION_DONE, "");
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_DONE;
        return (ApCmd){ "SAIL:all_done", 0, assert_always_true };
    }
    if (strcmp(g->position.zone, dest) == 0) {
        AP_LOG("[mission] arrived in zone %s", dest);
        advance_to(st, MISSION_SAFE_ACQUIRE, dest);
        return (ApCmd){ "SAIL:arrived", 0, assert_always_true };
    }
    return ap_sail_to_zone(g, m, st, dest,
                           (AutoplayPhase)0, (AutoplayPhase)0,
                           out_phase_done, out_next_phase);
}

// MISSION_ALCOVE: visit the magic alcove if any tile of that
// interactive exists in this zone and knows_magic is false.
static ApCmd handle_alcove(const Game *g, const Map *m,
                           AutoplayState *st,
                           bool *out_phase_done,
                           AutoplayPhase *out_next_phase) {
    if (g->stats.knows_magic) {
        advance_to(st, MISSION_PATHS_END_SPELLS, g->position.zone);
        return (ApCmd){ "ALCOVE:already", 0, assert_always_true };
    }
    int ax, ay;
    if (!ap_find_interact(m, (int)INTERACT_ALCOVE, NULL, &ax, &ay)) {
        // No alcove in this zone — skip.
        advance_to(st, MISSION_PATHS_END_SPELLS, g->position.zone);
        return (ApCmd){ "ALCOVE:none", 0, assert_always_true };
    }
    return ap_visit_alcove(g, m, st, ax, ay,
                           (AutoplayPhase)0, (AutoplayPhase)0,
                           out_phase_done, out_next_phase);
}

// MISSION_PATHS_END_SPELLS: nav to paths_end town, buy fireballs.
// Substeps: 0 = nav-to-town, 1 = buy spells, 2 = exit.
static ApCmd handle_paths_end_spells(const Game *g, const Map *m,
                                     AutoplayState *st,
                                     bool *out_phase_done,
                                     AutoplayPhase *out_next_phase) {
    int sub = st->mission_substep;
    if (sub == 0) {
        if (views_active() == VIEW_TOWN) {
            st->mission_substep = 1;
            return (ApCmd){ "SPELLS:in_town", 0,
                            assert_always_true };
        }
        return ap_nav_to_town(g, m, st, "paths_end",
                              (AutoplayPhase)0, (AutoplayPhase)0,
                              out_phase_done, out_next_phase);
    }
    if (sub == 1) {
        ApCmd r = ap_buy_spells_at_town(g, m, st, "fireball",
            g->stats.max_spells,
            (AutoplayPhase)0, (AutoplayPhase)0,
            out_phase_done, out_next_phase);
        if (*out_phase_done) {
            *out_phase_done = false;
            st->mission_substep = 2;
        }
        return r;
    }
    // Exit town.
    if (views_active() == VIEW_NONE) {
        advance_to(st, MISSION_GHOST_DWELLING, g->position.zone);
        return (ApCmd){ "SPELLS:done", 0, assert_always_true };
    }
    return (ApCmd){ "SPELLS:esc", KEY_ESCAPE, assert_always_true };
}

// MISSION_GHOST_DWELLING: recruit ghosts to add a strong stack vs
// undead/magic foes. ap_recruit_at_dwelling skips if already in army.
static ApCmd handle_ghost_dwelling(const Game *g, const Map *m,
                                   AutoplayState *st,
                                   bool *out_phase_done,
                                   AutoplayPhase *out_next_phase) {
    ApCmd r = ap_recruit_at_dwelling(g, m, st, NULL, "ghosts", 9999,
        (AutoplayPhase)0, (AutoplayPhase)0,
        out_phase_done, out_next_phase);
    if (*out_phase_done) {
        *out_phase_done = false;
        advance_to(st, MISSION_SAFE_ACQUIRE, g->position.zone);
    }
    return r;
}

// -------------------------------------------------------------------------
// Top-level dispatch
// -------------------------------------------------------------------------

ApCmd ap_mission_tick(const Game *g, const Map *m,
                      AutoplayState *st,
                      bool *out_phase_done,
                      AutoplayPhase *out_next_phase) {
    switch (st->mission_kind) {
    case MISSION_INTRO:
        return handle_intro(g, m, st, out_phase_done, out_next_phase);
    case MISSION_VISIT_TOWN:
        return handle_visit_town(g, m, st, out_phase_done,
                                 out_next_phase);
    case MISSION_STARTUP_RECRUIT:
        return handle_startup_recruit(g, m, st, out_phase_done,
                                      out_next_phase);
    case MISSION_ALCOVE:
        return handle_alcove(g, m, st, out_phase_done, out_next_phase);
    case MISSION_PATHS_END_SPELLS:
        return handle_paths_end_spells(g, m, st, out_phase_done,
                                       out_next_phase);
    case MISSION_GHOST_DWELLING:
        return handle_ghost_dwelling(g, m, st, out_phase_done,
                                     out_next_phase);
    case MISSION_SAFE_ACQUIRE:
        return handle_safe_acquire(g, m, st, out_phase_done,
                                   out_next_phase);
    case MISSION_REHOME_RECRUIT:
        return handle_rehome_recruit(g, m, st, out_phase_done,
                                     out_next_phase);
    case MISSION_CHEST_GRIND:
        return handle_chest_grind(g, m, st, out_phase_done,
                                  out_next_phase);
    case MISSION_MONSTER_GRIND:
        return handle_monster_grind(g, m, st, out_phase_done,
                                    out_next_phase);
    case MISSION_VILLAIN_GRIND:
        return handle_villain_grind(g, m, st, out_phase_done,
                                    out_next_phase);
    case MISSION_SAIL_TO_NEXT:
        return handle_sail_to_next(g, m, st, out_phase_done,
                                   out_next_phase);
    case MISSION_RENT_BOAT:
        return handle_rent_boat(g, m, st, out_phase_done,
                                out_next_phase);
    case MISSION_DONE: default:
        *out_phase_done = true;
        *out_next_phase = AP_FLOW_DONE;
        return (ApCmd){ "MISSION:DONE", 0, assert_always_true };
    }
}
