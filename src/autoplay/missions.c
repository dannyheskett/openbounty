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

// Per-zone unreachable-chest skiplist check. Populated when nav
// returns no-path; subsequent picks ignore that tile.
static bool chest_skip_contains(const AutoplayState *st, int x, int y) {
    if (!st) return false;
    int packed = (y << 8) | (x & 0xff);
    for (int i = 0; i < st->chest_skip_count; i++) {
        if (st->chest_skip[i] == packed) return true;
    }
    return false;
}

bool ap_pick_safe_acquisition(const Game *g, const Map *m,
                              const AutoplayState *st,
                              int *out_x, int *out_y) {
    if (!g || !m) return false;
    int best_x = -1, best_y = -1, best_d = -1;
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            const Tile *t = &m->tiles[y][x];
            if (!is_acquisition(t->interactive)) continue;
            if (chest_skip_contains(st, x, y)) continue;
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
                             const AutoplayState *st,
                             int *out_x, int *out_y) {
    if (!g || !m) return false;
    int best_x = -1, best_y = -1, best_d = -1;
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            const Tile *t = &m->tiles[y][x];
            if (!is_acquisition(t->interactive)) continue;
            if (chest_skip_contains(st, x, y)) continue;
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

// Pick the nearest acquisition tile guarded ONLY by foes we can
// beat, AND not on the skiplist (unreachable from prior attempts).
// Returns false when no winnable+reachable chest remains.
bool ap_pick_winnable_acquisition(const Game *g, const Map *m,
                                  const AutoplayState *st,
                                  int *out_x, int *out_y) {
    if (!g || !m) return false;
    int my_hp = ap_army_total_hp(g);
    int best_x = -1, best_y = -1, best_d = -1;
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            const Tile *t = &m->tiles[y][x];
            if (!is_acquisition(t->interactive)) continue;
            if (chest_skip_contains(st, x, y)) continue;
            bool unwinnable = false;
            for (int i = 0; i < g->foe_count; i++) {
                const FoeState *f = &g->foes[i];
                if (!f->alive || f->friendly) continue;
                if (strcmp(f->zone, g->position.zone) != 0) continue;
                int dx = (f->x > x) ? f->x - x : x - f->x;
                int dy = (f->y > y) ? f->y - y : y - f->y;
                if (dx > 2 || dy > 2) continue;
                int foe_hp = ap_effective_foe_hp(f);
                bool winnable =
                    (foe_hp == 0) || (my_hp * 2 >= foe_hp * 3);
                if (!winnable) { unwinnable = true; break; }
            }
            if (unwinnable) continue;
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

// Effective foe HP, weighted for hard-to-kill abilities. REGEN
// stacks heal partial damage each round, so for a normal army that
// can't burst past hp_each, a regen unit costs roughly 2× its raw
// HP in attrition. Returns hp value padded by the regen surcharge.
int ap_effective_foe_hp(const FoeState *foe) {
    if (!foe) return 0;
    int hp = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (!foe->garrison[i].id[0] || foe->garrison[i].count == 0) continue;
        const TroopDef *t = troop_by_id(foe->garrison[i].id);
        if (!t) continue;
        int stack_hp = t->hit_points * foe->garrison[i].count;
        if (t->abilities & TROOP_ABIL_REGEN) {
            // Double-count regen stacks — partial damage resets,
            // so to a non-spell army each regen unit absorbs about
            // 2× its raw HP before dying.
            stack_hp *= 2;
        }
        hp += stack_hp;
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

static bool villain_caught(const Game *g, const char *villain_id);

// Leadership-target slot in AutoplayState.module_scratch.
//   [9] = leadership headroom needed to beat the hardest enemy castle
//         in the current mission_zone. Computed lazily by
//         leadership_target_for_zone(); reset to 0 when the zone
//         changes or all castles are cleared.
#define MS_LEAD_TARGET 9

// Scan every monster + uncaught-villain castle in zone_id and return
// the largest garrison HP. Used by CHEST_GRIND to size a leadership
// target: enough troop-hp to soak the toughest castle we'll face.
// Returns 0 if no enemy castles remain.
static int max_enemy_castle_hp_in_zone(const Game *g,
                                       const char *zone_id) {
    int best = 0;
    for (int i = 0; i < GAME_CASTLES; i++) {
        const CastleRecord *cr = &g->castles[i];
        if (!cr->id[0]) continue;
        if (cr->owner_kind == CASTLE_OWNER_PLAYER) continue;
        if (cr->owner_kind == CASTLE_OWNER_VILLAIN &&
            villain_caught(g, cr->villain_id)) continue;
        const ResCastle *rc = g->res
            ? resources_castle_by_id(g->res, cr->id) : NULL;
        if (!rc || strcmp(rc->zone, zone_id) != 0) continue;
        int hp = castle_garrison_hp(cr);
        if (hp > best) best = hp;
    }
    return best;
}

// Average gold cost per 1 hp of army. Knights: 1000g / 35hp ≈ 28.6.
// Archers: 250g / 10hp = 25. Mixed buy across the recruit pool
// averages ~30. Used to convert the projected gold pool into the
// army-hp we can afford, which is the leadership ceiling worth
// banking.
#define AP_GOLD_PER_HP 30

// Compute (and cache in MS_LEAD_TARGET) the gold-balanced leadership
// target for the current zone. The formula:
//
//   projected_gold = current_gold
//                  + Σ chest_gold_offering    (peek every unopened GOLD chest)
//                  + Σ castle_garrison_spoils (5 × spoils_factor × count per stack)
//                  + Σ villain_contract_rewards (uncaught villains in zone)
//
//   lead_target    = projected_gold / 30
//
// Per chest GOLD/LEADERSHIP prompt: take leadership while
// leadership_base < lead_target, gold otherwise. This pairs each
// leadership point with ~30g of gold we'll actually have to spend
// recruiting up to it.
static int leadership_target_for_zone(const Game *g,
                                      const Map *m,
                                      AutoplayState *st,
                                      const char *zone_id) {
    int cached = st->module_scratch[MS_LEAD_TARGET];
    if (cached > 0) return cached;

    // 1. Current gold.
    int projected_gold = g->stats.gold;

    // 2. Sum offered gold across every unopened GOLD chest in zone.
    int zone_index = 0;
    if (g->res) {
        for (int i = 0; i < g->res->zone_count; i++) {
            if (strcmp(g->res->zones[i].id, zone_id) == 0) {
                zone_index = i; break;
            }
        }
    }
    int chest_gold_sum = 0;
    int chest_lead_sum = 0;
    int chest_count = 0;
    int chest_gold_count = 0;
    if (m) {
        for (int y = 0; y < m->height; y++) {
            for (int x = 0; x < m->width; x++) {
                const Tile *t = MapGetTile(m, x, y);
                if (!t) continue;
                if (t->interactive != INTERACT_TREASURE_CHEST) continue;
                ChestPending cp = { 0, 0 };
                ChestOutcome co =
                    GamePeekChest(g, zone_index, x, y, &cp);
                chest_count++;
                if (co == CHEST_OUTCOME_GOLD) {
                    chest_gold_count++;
                    chest_gold_sum += cp.pending_gold;
                    chest_lead_sum += cp.pending_leadership;
                }
            }
        }
    }
    projected_gold += chest_gold_sum;

    // 3. Sum garrison spoils for every uncaptured enemy castle in zone.
    //    Combat formula (engine/combat.c:262): spoils += spoils_factor × 5 × count
    int spoils_sum = 0;
    int castle_count = 0;
    for (int i = 0; i < GAME_CASTLES; i++) {
        const CastleRecord *cr = &g->castles[i];
        if (!cr->id[0]) continue;
        if (cr->owner_kind == CASTLE_OWNER_PLAYER) continue;
        if (cr->owner_kind == CASTLE_OWNER_VILLAIN &&
            villain_caught(g, cr->villain_id)) continue;
        const ResCastle *rc = g->res
            ? resources_castle_by_id(g->res, cr->id) : NULL;
        if (!rc || strcmp(rc->zone, zone_id) != 0) continue;
        castle_count++;
        for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
            if (!cr->garrison[s].id[0] ||
                cr->garrison[s].count <= 0) continue;
            const TroopDef *td = troop_by_id(cr->garrison[s].id);
            if (!td) continue;
            spoils_sum += td->spoils_factor * 5 * cr->garrison[s].count;
        }
    }
    projected_gold += spoils_sum;

    // 4. Sum uncaught villain contract rewards for villains in zone.
    int villain_reward_sum = 0;
    int villain_count = 0;
    if (g->res) {
        int n = villains_count();
        for (int i = 0; i < n; i++) {
            const VillainDef *v = villain_by_index(i);
            if (!v) continue;
            if (strcmp(v->zone, zone_id) != 0) continue;
            if (v->index >= 0 && v->index < 17 &&
                g->contract.villains_caught[v->index]) continue;
            villain_reward_sum += v->reward;
            villain_count++;
        }
    }
    projected_gold += villain_reward_sum;

    int target = projected_gold / AP_GOLD_PER_HP;
    if (target < 1) target = 1;
    st->module_scratch[MS_LEAD_TARGET] = target;

    AP_LOG("[mission] lead_target computed for zone=%s: "
           "current_gold=%d chests_total=%d gold_chests=%d "
           "chest_gold_offered=%d chest_lead_offered=%d "
           "castles=%d spoils=%d villains=%d rewards=%d "
           "→ projected_gold=%d → lead_target=%d (gold/hp=%d)",
           zone_id, g->stats.gold,
           chest_count, chest_gold_count,
           chest_gold_sum, chest_lead_sum,
           castle_count, spoils_sum,
           villain_count, villain_reward_sum,
           projected_gold, target, AP_GOLD_PER_HP);

    return target;
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

// Pick the weakest monster castle in zone whose HP is at or below
// `hp_cap`. Returns NULL when none qualify — caller can treat that as
// "no winnable target right now, advance to villains".
const CastleRecord *ap_pick_winnable_monster_castle(
    const Game *g, const char *zone_id, int hp_cap) {
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
        if (hp > hp_cap) continue;
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

// Internal: is this castle a valid CASTLE_GRIND target?
// Monsters always qualify. Villain castles qualify when the villain
// is not yet caught. Player castles never qualify.
static bool castle_is_enemy(const Game *g, const CastleRecord *cr) {
    if (!cr || !cr->id[0]) return false;
    if (cr->owner_kind == CASTLE_OWNER_MONSTERS) return true;
    if (cr->owner_kind == CASTLE_OWNER_VILLAIN) {
        return !villain_caught(g, cr->villain_id);
    }
    return false;
}

const CastleRecord *ap_pick_winnable_castle(
    const Game *g, const char *zone_id, int hp_cap) {
    const CastleRecord *best = NULL;
    int best_hp = 0;
    for (int i = 0; i < GAME_CASTLES; i++) {
        const CastleRecord *cr = &g->castles[i];
        if (!castle_is_enemy(g, cr)) continue;
        const ResCastle *rc = g->res
            ? resources_castle_by_id(g->res, cr->id) : NULL;
        if (!rc || strcmp(rc->zone, zone_id) != 0) continue;
        int hp = castle_garrison_hp(cr);
        if (hp > hp_cap) continue;
        if (!best || hp < best_hp) {
            best = cr; best_hp = hp;
        }
    }
    return best;
}

const CastleRecord *ap_pick_weakest_enemy_castle(
    const Game *g, const char *zone_id) {
    const CastleRecord *best = NULL;
    int best_hp = 0;
    for (int i = 0; i < GAME_CASTLES; i++) {
        const CastleRecord *cr = &g->castles[i];
        if (!castle_is_enemy(g, cr)) continue;
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
    if (!cls || cls->rank_count == 0) return 0;
    // Target the max-rank ceiling. Yes, rank-ups wipe
    // accumulated chest leadership (engine/game.c:1232) — but the
    // pre-rank-up benefit (larger army HP cap during the chest /
    // monster grind that happens BEFORE the next villain capture)
    // is the whole point. Going under-leadership leaves us
    // permanently capped at the base rank's army size, which is
    // worse than rebuilding after a rank-up wipe.
    int target = 0;
    for (int i = 0; i < cls->rank_count; i++) {
        if (cls->ranks[i].leadership > target)
            target = cls->ranks[i].leadership;
    }
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
    case MISSION_MAGIC_GRIND:       return "MAGIC_GRIND";
    case MISSION_PATHS_END_SPELLS:  return "PATHS_END_SPELLS";
    case MISSION_SAFE_ACQUIRE:      return "SAFE_ACQUIRE";
    case MISSION_REHOME_RECRUIT:    return "REHOME_RECRUIT";
    case MISSION_CHEST_GRIND:       return "CHEST_GRIND";
    case MISSION_CASTLE_GRIND:      return "CASTLE_GRIND";
    case MISSION_DWELLING_GNOMES:   return "DWELLING_GNOMES";
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
        // Drop the leadership target cache when we cross into a new
        // zone — the next CHEST_GRIND will recompute it from that
        // zone's own castle HPs. Same goes for the chest skiplist:
        // unreachable chests are per-zone (different boat state /
        // map blockers per zone).
        if (strcmp(st->mission_zone, zone) != 0) {
            st->module_scratch[MS_LEAD_TARGET] = 0;
            for (int i = 0; i < (int)(sizeof(st->chest_skip) /
                                       sizeof(st->chest_skip[0])); i++) {
                st->chest_skip[i] = -1;
            }
            st->chest_skip_count = 0;
        }
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
    st->module_scratch[MS_LEAD_TARGET] = 0;
    for (int i = 0; i < MAX_ZONES; i++) st->zone_solved[i] = false;
    for (int i = 0; i < (int)(sizeof(st->chest_skip) /
                               sizeof(st->chest_skip[0])); i++) {
        st->chest_skip[i] = -1;
    }
    st->chest_skip_count = 0;
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
    // sub 2: row-buy loop. Skip row 0 (militia for Knight class) —
    // we recruit gnomes from a forest dwelling immediately after.
    if (sub == 2) {
        int s = st->module_scratch[11];
        if (s < 0) s = 0;
        int row = s >> 3;
        int ks  = s & 7;
        if (row == 0) {  // skip militia row
            st->module_scratch[11] = (1 << 3) | 0;
            row = 1;
        }
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
        // Visit forest dwelling to recruit gnomes (replaces militia).
        advance_to(st, MISSION_DWELLING_GNOMES, g->position.zone);
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
        // Chest A/B prompt. Take leadership (B) while EITHER:
        // Take leadership (B) while leadership_base < lead_target,
        // gold (A) afterwards. Target = projected_gold / 30
        // (gold-balanced; see leadership_target_for_zone).
        int lead_target = leadership_target_for_zone(g, m, st, zone);
        int letter = (g->stats.leadership_base < lead_target)
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
        if (!ap_pick_safe_acquisition(g, m, st, &tx, &ty)) {
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
        // No foe-safe path. Skip this chest and re-pick on the
        // next tick. Only advance to REHOME_RECRUIT once we've
        // exhausted the safe-pickable chests.
        int cap = (int)(sizeof(st->chest_skip) /
                        sizeof(st->chest_skip[0]));
        if (st->chest_skip_count < cap) {
            st->chest_skip[st->chest_skip_count++] =
                (ty << 8) | (tx & 0xff);
            AP_LOG("[mission] SAFE: no path to (%d,%d) — skip "
                   "(skip_count=%d) and re-pick",
                   tx, ty, st->chest_skip_count);
            st->module_scratch[14] = -1;
            st->module_scratch[15] = -1;
            return (ApCmd){ "SAFE:skip_unreachable", 0,
                            assert_always_true };
        }
        AP_LOG("[mission] SAFE: no safe path to (%d,%d) and "
               "skiplist full — advancing", tx, ty);
        st->module_scratch[14] = -1;
        st->module_scratch[15] = -1;
        advance_to(st, MISSION_REHOME_RECRUIT, zone);
        return (ApCmd){ "SAFE:no_path", 0, assert_always_true };
    }
    snprintf(cmd, sizeof cmd, "SAFE:nav(%d,%d)", tx, ty);
    return (ApCmd){ cmd, key, assert_always_true };
}

// MISSION_REHOME_RECRUIT: delegate to ap_rehome_and_recruit. On
// completion, advance to mission_resume_kind if set (caller specified
// where to come back to), otherwise default to CHEST_GRIND for the
// SAFE_ACQUIRE → REHOME → CHEST_GRIND base chain.
static ApCmd handle_rehome_recruit(const Game *g, const Map *m,
                                   AutoplayState *st,
                                   bool *out_phase_done,
                                   AutoplayPhase *out_next_phase) {
    // Before recruiting (which drains gold), detour to ALCOVE if
    // we can afford it and don't yet know magic. Home-zone only —
    // the alcove sits in continentia and we'd otherwise sail away
    // from a foreign zone just to visit it.
    int alcove_cost = g->res ? g->res->economy.alcove_cost : 5000;
    if (strcmp(g->position.zone, "continentia") == 0 &&
        !g->stats.knows_magic && g->stats.gold > alcove_cost &&
        st->mission_resume_kind == 0) {
        AP_LOG("[mission] REHOME: pre-recruit gold=%d > alcove=%d "
               "— detour to ALCOVE first",
               g->stats.gold, alcove_cost);
        advance_to(st, MISSION_ALCOVE, st->mission_zone);
        return (ApCmd){ "REHOME:divert_alcove", 0,
                        assert_always_true };
    }
    // Reserve handled by the recruit screen's RECRUIT_GOLD_RESERVE
    // constant (1000g); no need to gate at row-start.
    ApCmd r = ap_rehome_and_recruit(g, m, st, /*reserve=*/0,
        (AutoplayPhase)0, (AutoplayPhase)0,
        out_phase_done, out_next_phase);
    if (*out_phase_done) {
        *out_phase_done = false;
        int next = st->mission_resume_kind;
        // Clear the resume slot so a future REHOME_RECRUIT call
        // without setting it falls through to the default.
        st->mission_resume_kind = 0;
        if (next == 0 || next == MISSION_REHOME_RECRUIT) {
            next = MISSION_CHEST_GRIND;
        }
        // Opportunistic detour: if we just finished REHOME at
        // continentia, don't yet know magic, and have enough gold
        // for the alcove, head there before going back out. Skip
        // for non-home zones — the alcove only lives at home.
        int alcove_cost = g->res ? g->res->economy.alcove_cost : 5000;
        if (strcmp(g->position.zone, "continentia") == 0 &&
            !g->stats.knows_magic &&
            g->stats.gold > alcove_cost &&
            next != MISSION_ALCOVE) {
            AP_LOG("[mission] REHOME: gold=%d > alcove=%d — detour "
                   "to ALCOVE", g->stats.gold, alcove_cost);
            next = MISSION_ALCOVE;
        }
        advance_to(st, next, st->mission_zone);
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
        advance_to(st, MISSION_CASTLE_GRIND, zone);
        return (ApCmd){ "GRIND:wrong_zone", 0, assert_always_true };
    }
    // Refill-before-critical: if the army has bled below 50% of
    // leadership capacity, divert to REHOME_RECRUIT *now* —
    // before the next foe fight (where we'd be weak enough to
    // get trapped). module_scratch[10] caches the last hp value
    // we used as the refill check so we don't keep diverting
    // when REHOME can't actually buy more (gold-limited).
    if (!prompt_is_active() && !dialog_is_active()) {
        int my_hp   = ap_army_total_hp(g);
        int my_lead = g->stats.leadership_current;
        // Refill threshold: hp < 50% of leadership capacity.
        // Leadership_current scales with rank + chest bonuses;
        // 50% of that is the natural "I'm getting weak" marker.
        int threshold = my_lead / 2;
        int last_refill_hp = st->module_scratch[10];
        if (my_hp < threshold && my_hp != last_refill_hp) {
            AP_LOG("[mission] CHEST_GRIND: army hp=%d < 50%% of "
                   "lead=%d (threshold=%d) — divert to REHOME",
                   my_hp, my_lead, threshold);
            st->module_scratch[10] = my_hp;
            st->module_scratch[14] = -1;
            st->module_scratch[15] = -1;
            st->mission_resume_kind = MISSION_CHEST_GRIND;
            advance_to(st, MISSION_REHOME_RECRUIT, zone);
            return (ApCmd){ "GRIND:divert_refill", 0,
                            assert_always_true };
        }
    }
    if (prompt_is_active()) {
        const char *kind = prompt_kind_str();
        if (kind && strcmp(kind, "yes_no") == 0) {
            // yes_no covers hostile foe encounters, friendly
            // recruits, and castle attacks. Only hostile foes
            // get the over-match check — friendlies and other
            // yes_no prompts default to Y. Hostile foe is
            // identified by pending_foe_id mapping to a FoeState
            // whose friendly==false.
            const char *fid = pending_foe_id;
            const FoeState *fs = (fid && fid[0])
                                     ? GameFindFoeConst(g, fid)
                                     : NULL;
            bool hostile = fs && !fs->friendly;
            if (hostile) {
                int my_hp  = ap_army_total_hp(g);
                int foe_hp = ap_effective_foe_hp(fs);
                // Fight when my_hp × 2 >= foe_hp × 3 (army ≥
                // 1.5× effective foe). foe_hp doubles REGEN
                // stacks so trolls cost us 2× their raw HP. If
                // outmatched, drop the chest target and decline
                // — re-pick a different chest next tick.
                bool worth_fighting =
                    (foe_hp == 0) || (my_hp * 2 >= foe_hp * 3);
                if (!worth_fighting) {
                    // Track decline count in module_scratch[8].
                    // After 3 declines, give up on CHEST_GRIND —
                    // the unkillable foe is blocking too many
                    // chests; better to move on to CASTLE_GRIND.
                    int declines = st->module_scratch[8];
                    if (declines < 0) declines = 0;
                    declines++;
                    st->module_scratch[8] = declines;
                    AP_LOG("[mission] GRIND: DECLINE #%d foe='%s' "
                           "my_hp=%d effective_foe_hp=%d (ratio %.2f)",
                           declines, fid, my_hp, foe_hp,
                           (float)my_hp / (float)foe_hp);
                    if (declines >= 3) {
                        AP_LOG("[mission] GRIND: %d declines — "
                               "advance to CASTLE_GRIND",
                               declines);
                        st->module_scratch[8]  = 0;
                        st->module_scratch[14] = -1;
                        st->module_scratch[15] = -1;
                        advance_to(st, MISSION_CASTLE_GRIND, zone);
                        return (ApCmd){ "GRIND:n_foe_giveup",
                                        KEY_N,
                                        assert_always_true };
                    }
                    st->module_scratch[14] = -1;
                    st->module_scratch[15] = -1;
                    return (ApCmd){ "GRIND:n_foe", KEY_N,
                                    assert_always_true };
                }
                // Successful fight — reset decline counter.
                st->module_scratch[8] = 0;
                AP_LOG("[mission] GRIND: fight foe='%s' "
                       "my_hp=%d effective_foe_hp=%d (ratio %.2f)",
                       fid, my_hp, foe_hp,
                       (float)my_hp / (float)foe_hp);
            }
            *out_phase_done = true;
            *out_next_phase = AP_FLOW_COMBAT;
            return (ApCmd){ "GRIND:y_foe", KEY_Y,
                            assert_always_true };
        }
        if (kind && strcmp(kind, "text") == 0) {
            return (ApCmd){ "GRIND:enter", KEY_ENTER,
                            assert_prompt_gone };
        }
        // Chest A/B prompt — same rule as SAFE_ACQUIRE: B
        // (leadership) while below gold-balanced lead_target,
        // A (gold) afterwards.
        int lead_target = leadership_target_for_zone(g, m, st, zone);
        int letter = (g->stats.leadership_base < lead_target)
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
        // Two-pass: prefer foe-free chests, then chests guarded only
        // by winnable foes. Unwinnable chests are skipped entirely.
        bool picked = ap_pick_safe_acquisition(g, m, st, &tx, &ty);
        if (!picked) {
            picked = ap_pick_winnable_acquisition(g, m, st, &tx, &ty);
            if (picked) {
                AP_LOG("[mission] CHEST_GRIND pass2 (winnable-foe) "
                       "target=(%d,%d)", tx, ty);
            }
        } else {
            AP_LOG("[mission] CHEST_GRIND pass1 (foe-free) "
                   "target=(%d,%d)", tx, ty);
        }
        if (!picked) {
            AP_LOG("[mission] CHEST_GRIND done in %s "
                   "(remaining chests all unwinnable)", zone);
            st->module_scratch[14] = -1;
            st->module_scratch[15] = -1;
            advance_to(st, MISSION_CASTLE_GRIND, zone);
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
        // might be reachable via water.
        if (!g->boat.has_boat && g->stats.gold > 500) {
            AP_LOG("[mission] GRIND: no path to (%d,%d) from (%d,%d) "
                   "boat=0 — diverting to RENT_BOAT",
                   tx, ty, g->position.x, g->position.y);
            st->mission_resume_kind = MISSION_CHEST_GRIND;
            advance_to(st, MISSION_RENT_BOAT, zone);
            return (ApCmd){ "GRIND:divert_rent", 0,
                            assert_always_true };
        }
        // Unreachable chest. Add to skiplist and drop sticky target
        // so the next tick re-picks a different chest. If the skip
        // list is full, halt (extremely rare).
        int cap = (int)(sizeof(st->chest_skip) /
                        sizeof(st->chest_skip[0]));
        if (st->chest_skip_count < cap) {
            st->chest_skip[st->chest_skip_count++] =
                (ty << 8) | (tx & 0xff);
            AP_LOG("[mission] GRIND: no path to (%d,%d) — skip "
                   "(skip_count=%d) and re-pick",
                   tx, ty, st->chest_skip_count);
            st->module_scratch[14] = -1;
            st->module_scratch[15] = -1;
            return (ApCmd){ "GRIND:skip_unreachable", 0,
                            assert_always_true };
        }
        AP_LOG("[mission] GRIND: no path to (%d,%d) and skiplist "
               "full (%d) — advance to CASTLE_GRIND",
               tx, ty, st->chest_skip_count);
        st->module_scratch[14] = -1;
        st->module_scratch[15] = -1;
        advance_to(st, MISSION_CASTLE_GRIND, zone);
        return (ApCmd){ "GRIND:skip_full", 0,
                        assert_always_true };
    }
    snprintf(cmd, sizeof cmd, "GRIND:nav(%d,%d)", tx, ty);
    return (ApCmd){ cmd, key, assert_always_true };
}

// MISSION_CASTLE_GRIND: unified enemy-castle queue.
//
//   1. Pick weakest enemy castle (monster or uncaught-villain) in
//      zone with garrison HP ≤ army × 1.5. That's the target.
//   2. If target is a villain and active_id doesn't match: BFS to
//      nearest town, cycle contracts until match (free, no gold).
//   3. Refill army before engaging; if refill is a no-op AND army
//      ≥ castle hp, attack with what we have; otherwise end run
//      (rank ceiling can't grow without other-zone villains).
//   4. After attacking, REHOME-recruit between castles to refill.
//   5. When no winnable castle remains in zone, mark zone solved
//      and SAIL_TO_NEXT. If no other zones, MISSION_DONE.
static ApCmd handle_castle_grind(const Game *g, const Map *m,
                                 AutoplayState *st,
                                 bool *out_phase_done,
                                 AutoplayPhase *out_next_phase) {
    const char *zone = st->mission_zone;
    if (strcmp(g->position.zone, zone) != 0) {
        for (int i = 0; i < g->res->zone_count && i < MAX_ZONES; i++) {
            if (strcmp(g->res->zones[i].id, zone) == 0) {
                st->zone_solved[i] = true;
            }
        }
        advance_to(st, MISSION_SAIL_TO_NEXT, "");
        return (ApCmd){ "CASTLE:wrong_zone", 0,
                        assert_always_true };
    }
    // Always pick the weakest enemy castle in this zone. No
    // threshold — we fight in absolute HP order. Refill happens
    // between every castle (below).
    const CastleRecord *cr = ap_pick_weakest_enemy_castle(g, zone);
    if (!cr) {
        AP_LOG("[mission] CASTLE_GRIND done in %s — all enemy "
               "castles cleared", zone);
        for (int i = 0; i < g->res->zone_count && i < MAX_ZONES; i++) {
            if (strcmp(g->res->zones[i].id, zone) == 0) {
                st->zone_solved[i] = true;
            }
        }
        advance_to(st, MISSION_SAIL_TO_NEXT, "");
        return (ApCmd){ "CASTLE:zone_clear", 0,
                        assert_always_true };
    }
    bool is_villain = (cr->owner_kind == CASTLE_OWNER_VILLAIN);
    // Need siege weapons to enter — divert if missing.
    if (!g->stats.siege_weapons && g->stats.gold > 3000) {
        AP_LOG("[mission] CASTLE_GRIND: no siege — divert to RENT");
        st->mission_resume_kind = MISSION_CASTLE_GRIND;
        advance_to(st, MISSION_RENT_BOAT, zone);
        return (ApCmd){ "CASTLE:divert_siege", 0,
                        assert_always_true };
    }
    // Villain prerequisite: matching contract. Cycle (free) at any
    // town until the active contract names our target villain.
    if (is_villain) {
        bool contract_matches =
            (g->contract.active_id[0] &&
             strcmp(g->contract.active_id, cr->villain_id) == 0);
        if (!contract_matches) {
            AP_LOG("[mission] CASTLE_GRIND: villain target=%s "
                   "contract=%s — cycle for match",
                   cr->villain_id,
                   g->contract.active_id[0]
                       ? g->contract.active_id : "(none)");
            char tid[24];
            if (!pick_nearest_town(g, m, tid, sizeof tid)) {
                AP_LOG("[mission] CASTLE_GRIND: no town in zone — "
                       "END RUN (can't cycle contract)");
                advance_to(st, MISSION_DONE, "");
                *out_phase_done = true;
                *out_next_phase = AP_FLOW_DONE;
                return (ApCmd){ "CASTLE:no_town", 0,
                                assert_always_true };
            }
            if (views_active() == VIEW_TOWN) {
                // Cycle: press A repeatedly until the active
                // contract matches our target villain. ap_town_action
                // handles the prompt accept/decline.
                char action[64];
                snprintf(action, sizeof action,
                         "contract_villain:%s", cr->villain_id);
                return ap_town_action(g, m, st, action,
                                      (AutoplayPhase)0,
                                      (AutoplayPhase)0,
                                      out_phase_done,
                                      out_next_phase);
            }
            return ap_nav_to_town(g, m, st, tid,
                                  (AutoplayPhase)0, (AutoplayPhase)0,
                                  out_phase_done, out_next_phase);
        }
    }
    // Refill before EVERY castle, exactly once per castle. Track
    // which castle we last refilled for in module_scratch[13] (as a
    // hash of the castle id). When we change target (castle id
    // changes), force a fresh refill.
    if (views_active() == VIEW_NONE && !dialog_is_active() &&
        !prompt_is_active()) {
        int my_hp = ap_army_total_hp(g);
        int castle_hp = 0;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (!cr->garrison[i].id[0] ||
                cr->garrison[i].count == 0) continue;
            const TroopDef *t = troop_by_id(cr->garrison[i].id);
            if (t) castle_hp += t->hit_points * cr->garrison[i].count;
        }
        // FNV-1a hash of cr->id to fit in module_scratch[13].
        unsigned target_tag = 2166136261u;
        for (const char *p = cr->id; *p; p++) {
            target_tag ^= (unsigned char)*p;
            target_tag *= 16777619u;
        }
        int target_tag_i = (int)(target_tag & 0x7FFFFFFF);
        int last_refilled_tag = st->module_scratch[13];
        AP_LOG("[mission] CASTLE_GRIND check: target=%s owner=%s "
               "my_hp=%d castle=%d refilled_for=%d cur_tag=%d",
               cr->id, is_villain ? "VILLAIN" : "MONSTER",
               my_hp, castle_hp, last_refilled_tag, target_tag_i);
        if (last_refilled_tag != target_tag_i) {
            AP_LOG("[mission] CASTLE_GRIND: refill before %s",
                   cr->id);
            st->module_scratch[13] = target_tag_i;
            st->mission_resume_kind = MISSION_CASTLE_GRIND;
            advance_to(st, MISSION_REHOME_RECRUIT, zone);
            return (ApCmd){ "CASTLE:refill", 0,
                            assert_always_true };
        }
        // After REHOME completes, visit a forest dwelling to fill
        // remaining leadership with gnomes (12 g/hp vs militia at
        // 25 g/hp). Module slot [12] mirrors [13] for this step:
        // tagged with the same target so we do it exactly once per
        // castle.
        int last_gnomes_tag = st->module_scratch[12];
        if (last_gnomes_tag != target_tag_i) {
            AP_LOG("[mission] CASTLE_GRIND: gnomes top-up before %s",
                   cr->id);
            st->module_scratch[12] = target_tag_i;
            advance_to(st, MISSION_DWELLING_GNOMES, zone);
            return (ApCmd){ "CASTLE:gnomes", 0,
                            assert_always_true };
        }
    }
    return ap_nav_to_castle(g, m, st, cr->id,
                            (AutoplayPhase)0, (AutoplayPhase)0,
                            out_phase_done, out_next_phase);
}

// MISSION_DWELLING_GNOMES: nav to nearest forest dwelling in zone
// (any one — gnomes is the only forest troop we want), recruit max
// gnomes. Idempotent: skips if no forest dwelling exists in zone.
// Falls through to CASTLE_GRIND when done.
static ApCmd handle_dwelling_gnomes(const Game *g, const Map *m,
                                    AutoplayState *st,
                                    bool *out_phase_done,
                                    AutoplayPhase *out_next_phase) {
    const char *zone = st->mission_zone;
    // Pick the next mission to advance to when done. When the
    // pre-castle tag (module_scratch[12]) is a positive castle-id
    // hash, we got here from CASTLE_GRIND and should return there.
    // Otherwise (default -1 from init, or post-STARTUP_RECRUIT)
    // continue the standard chain: ALCOVE.
    int next_mission = (st->module_scratch[12] > 0)
                           ? MISSION_CASTLE_GRIND
                           : MISSION_ALCOVE;
    if (strcmp(g->position.zone, zone) != 0) {
        advance_to(st, next_mission, zone);
        return (ApCmd){ "DWELL:wrong_zone", 0, assert_always_true };
    }
    ApCmd r = ap_recruit_at_dwelling(g, m, st, NULL, "gnomes", 9999,
        (AutoplayPhase)0, (AutoplayPhase)0,
        out_phase_done, out_next_phase);
    if (*out_phase_done) {
        *out_phase_done = false;
        advance_to(st, next_mission, zone);
    }
    return r;
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

// MISSION_ALCOVE: home-zone-only. Visit the magic alcove in
// continentia if knows_magic is false. Any non-home zone short-
// circuits to SAFE_ACQUIRE — the only alcove lives at home, and
// we don't sail back there mid-zone just for it.
static ApCmd handle_alcove(const Game *g, const Map *m,
                           AutoplayState *st,
                           bool *out_phase_done,
                           AutoplayPhase *out_next_phase) {
    if (strcmp(g->position.zone, "continentia") != 0) {
        advance_to(st, MISSION_SAFE_ACQUIRE, g->position.zone);
        return (ApCmd){ "ALCOVE:not_home", 0, assert_always_true };
    }
    if (g->stats.knows_magic) {
        advance_to(st, MISSION_MAGIC_GRIND, g->position.zone);
        return (ApCmd){ "ALCOVE:already", 0, assert_always_true };
    }
    // Alcove costs alcove_cost gold (5000g on stock). Defer if we
    // can't afford — the chest/monster grind will come back here
    // later once we've accumulated enough.
    int alcove_cost = g->res ? g->res->economy.alcove_cost : 5000;
    if (g->stats.gold <= alcove_cost) {
        AP_LOG("[mission] ALCOVE: gold=%d < cost=%d — defer past "
               "grind", g->stats.gold, alcove_cost);
        advance_to(st, MISSION_SAFE_ACQUIRE, g->position.zone);
        return (ApCmd){ "ALCOVE:defer_funds", 0, assert_always_true };
    }
    int ax, ay;
    if (!ap_find_interact(m, (int)INTERACT_ALCOVE, NULL, &ax, &ay)) {
        advance_to(st, MISSION_SAFE_ACQUIRE, g->position.zone);
        return (ApCmd){ "ALCOVE:none", 0, assert_always_true };
    }
    return ap_visit_alcove(g, m, st, ax, ay,
                           (AutoplayPhase)0, (AutoplayPhase)0,
                           out_phase_done, out_next_phase);
}

// Combat spell priority (higher = buy first). Adventure spells
// excluded — we don't use them in fights.
static int spell_combat_priority(const char *id) {
    if (!id) return 0;
    if (strcmp(id, "fireball")    == 0) return 100;
    if (strcmp(id, "lightning")   == 0) return 80;
    if (strcmp(id, "turn_undead") == 0) return 70;
    if (strcmp(id, "freeze")      == 0) return 50;
    if (strcmp(id, "clone")       == 0) return 30;
    if (strcmp(id, "teleport")    == 0) return 20;
    if (strcmp(id, "resurrect")   == 0) return 10;
    return 0; // adventure spells
}

// Pick the next town to visit. Walks g->towns[] looking for the
// best-priority combat spell we haven't yet topped up on. Returns
// the town's id and the spell id it sells. Returns false if no
// useful town remains (either all spells stocked or cap reached).
static bool magic_pick_next_town(const Game *g,
                                 const char *zone,
                                 char *out_town, int town_cap,
                                 const char **out_spell) {
    if (!g) return false;
    int known = GameKnownSpells(g);
    if (known >= g->stats.max_spells) return false;
    int best_pri = 0;
    const TownRecord *best = NULL;
    const char *best_spell = NULL;
    for (int i = 0; i < GAME_TOWNS; i++) {
        const TownRecord *t = &g->towns[i];
        if (!t->id[0]) continue;
        if (!t->spell_for_sale[0]) continue;
        // Must be in current zone (look up via resources).
        const ResTown *rt = resources_town_by_id(g->res, t->id);
        if (!rt) continue;
        // Find which zone this town belongs to.
        bool in_zone = false;
        for (int zi = 0; zi < g->res->zone_count; zi++) {
            const ResZone *z = &g->res->zones[zi];
            if (strcmp(z->id, zone) != 0) continue;
            for (int ti = 0; ti < z->town_count; ti++) {
                if (strcmp(z->towns[ti].id, t->id) == 0) {
                    in_zone = true; break;
                }
            }
            break;
        }
        if (!in_zone) continue;
        int pri = spell_combat_priority(t->spell_for_sale);
        if (pri == 0) continue; // adventure spell — skip
        if (pri > best_pri) {
            best_pri = pri;
            best = t;
            best_spell = t->spell_for_sale;
        }
    }
    if (!best) return false;
    int k = 0;
    while (k + 1 < town_cap && best->id[k]) {
        out_town[k] = best->id[k]; k++;
    }
    out_town[k] = '\0';
    if (out_spell) *out_spell = best_spell;
    return true;
}

// MISSION_MAGIC_GRIND: visit zone towns, buy combat spells until
// max_spells cap reached or no useful town remains. Idempotent:
// re-entry tops up after combats deplete the stockpile.
static ApCmd handle_magic_grind(const Game *g, const Map *m,
                                AutoplayState *st,
                                bool *out_phase_done,
                                AutoplayPhase *out_next_phase) {
    static char cmd[64];
    const char *zone = st->mission_zone;
    if (!g->stats.knows_magic) {
        AP_LOG("[mission] MAGIC_GRIND: knows_magic=0 — skip");
        advance_to(st, MISSION_PATHS_END_SPELLS, zone);
        return (ApCmd){ "MAGIC:no_magic", 0, assert_always_true };
    }
    int known = GameKnownSpells(g);
    if (known >= g->stats.max_spells) {
        AP_LOG("[mission] MAGIC_GRIND: at cap (%d/%d) — advance",
               known, g->stats.max_spells);
        // ESC out of any town view first.
        if (views_active() == VIEW_TOWN) {
            return (ApCmd){ "MAGIC:esc_town", KEY_ESCAPE,
                            assert_always_true };
        }
        advance_to(st, MISSION_PATHS_END_SPELLS, zone);
        return (ApCmd){ "MAGIC:cap_reached", 0,
                        assert_always_true };
    }
    // Handle dialogs / info panels.
    if (views_town_info_text() != NULL) {
        return (ApCmd){ "MAGIC:space_info", KEY_SPACE,
                        assert_always_true };
    }
    if (dialog_is_active()) {
        return (ApCmd){ "MAGIC:space", KEY_SPACE,
                        assert_dialog_closed };
    }
    // Find next town with a useful spell we don't have at cap.
    char tid[24];
    const char *want_spell = NULL;
    if (!magic_pick_next_town(g, zone, tid, sizeof tid, &want_spell)) {
        AP_LOG("[mission] MAGIC_GRIND: no useful town in %s — advance",
               zone);
        if (views_active() == VIEW_TOWN) {
            return (ApCmd){ "MAGIC:esc_done", KEY_ESCAPE,
                            assert_always_true };
        }
        advance_to(st, MISSION_PATHS_END_SPELLS, zone);
        return (ApCmd){ "MAGIC:done", 0, assert_always_true };
    }
    // If we're already in a town, check if it's the target. If so,
    // press D to buy. Otherwise ESC out to nav elsewhere.
    if (views_active() == VIEW_TOWN) {
        const char *cur = views_town_record_key();
        if (cur && strcmp(cur, tid) == 0) {
            snprintf(cmd, sizeof cmd, "MAGIC:d[%s]",
                     want_spell ? want_spell : "?");
            return (ApCmd){ cmd, KEY_D, assert_always_true };
        }
        // Wrong town — exit and nav to the right one.
        return (ApCmd){ "MAGIC:esc_wrong_town", KEY_ESCAPE,
                        assert_always_true };
    }
    // Not in a town — nav to the target.
    snprintf(cmd, sizeof cmd, "MAGIC:nav[%s/%s]", tid,
             want_spell ? want_spell : "?");
    (void)cmd;
    return ap_nav_to_town(g, m, st, tid,
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
        advance_to(st, MISSION_SAFE_ACQUIRE, g->position.zone);
        return (ApCmd){ "SPELLS:done", 0, assert_always_true };
    }
    return (ApCmd){ "SPELLS:esc", KEY_ESCAPE, assert_always_true };
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
    case MISSION_MAGIC_GRIND:
        return handle_magic_grind(g, m, st, out_phase_done,
                                  out_next_phase);
    case MISSION_PATHS_END_SPELLS:
        return handle_paths_end_spells(g, m, st, out_phase_done,
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
    case MISSION_CASTLE_GRIND:
        return handle_castle_grind(g, m, st, out_phase_done,
                                   out_next_phase);
    case MISSION_DWELLING_GNOMES:
        return handle_dwelling_gnomes(g, m, st, out_phase_done,
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
