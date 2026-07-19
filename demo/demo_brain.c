// demo/demo_brain.c
//
// The player's decision loop: one small action per tick -- answer the open
// prompt, transact in the town/castle being stood in, or pick the most
// attractive activity and take one step toward it. Everything is judged from
// player-visible state (the fog, the prompt banners, the views' records) and
// public rules knowledge (the troop catalog, the morale chart, the burial
// rule). No rollback: a fight entered is resolved by the real engine on the
// one live timeline and its outcome is lived with. Fights ARE weighed before
// entry -- demo_predict_boost runs the engine's own combat on a throwaway copy
// of the world to pick the smallest raise-control cast that wins -- but the
// copy is discarded and the live world only ever moves forward.

#include "demo.h"
#include "demo_internal.h"
#include "demo_combat_policy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adventure.h"
#include "combat.h"
#include "flow_resolve.h"
#include "pending.h"
#include "player_io.h"
#include "step.h"
#include "tables.h"
#include "tile.h"

// Fight margin (x10) used when SCOUTING: a wandering foe is only worth
// walking toward when our power exceeds its garrison's by this factor.
// The fight-entry decision itself is made later by the engine's own
// prediction (demo_predict_boost), not by this margin.
#define DEMO_MARGIN_FIELD  15
// Gold the player keeps in pocket through purchases (boat fare + slack).
#define DEMO_GOLD_RESERVE  1500
// Consecutive idle weeks before the player concludes waiting is pointless.
#define DEMO_MAX_WAITS     10
// Ticks with an unchanged world before the stuck watchdog ends the run.
#define DEMO_STUCK_TICKS   400

static DemoState s_st;
DemoState *demo_state(void) { return &s_st; }

// ---- Judgment: army power from the public catalog ---------------------------

static long troop_power(const TroopDef *td, int count) {
    if (!td || count <= 0) return 0;
    long dmg = (td->melee_min + td->melee_max) / 2;
    if (td->ranged_ammo > 0) {
        long r = (td->ranged_min + td->ranged_max) / 2;
        if (r > dmg) dmg = r;
    }
    if (dmg < 1) dmg = 1;
    return (long)count * td->hit_points * dmg * (td->skill_level + 5) / 10;
}

long demo_units_power(const Unit *units, int nslots) {
    long p = 0;
    for (int i = 0; i < nslots; i++) {
        if (!units[i].id[0] || units[i].count <= 0) continue;
        p += troop_power(troop_by_id(units[i].id), units[i].count);
    }
    return p;
}

long demo_army_power(const Game *g) {
    long p = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        const ArmyStack *s = &g->army[i];
        if (!s->id[0] || s->count <= 0) continue;
        const TroopDef *td = troop_by_id(s->id);
        long sp = troop_power(td, s->count);
        // An out-of-control stack fights for the other side (REQ-272).
        if (td && (long)td->hit_points * s->count > g->stats.leadership_current)
            p -= sp;
        else
            p += sp;
    }
    return p;
}

// Would adding `troop_id` drop any stack (or the newcomer) to Low morale?
static bool morale_safe_with(const Game *g, const char *troop_id) {
    const TroopDef *nt = troop_by_id(troop_id);
    if (!nt) return false;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (!g->army[i].id[0] || g->army[i].count <= 0) continue;
        const TroopDef *o = troop_by_id(g->army[i].id);
        if (!o || strcmp(o->id, nt->id) == 0) continue;
        if (morale_result(nt->morale_group, o->morale_group) == 'L') return false;
        if (morale_result(o->morale_group, nt->morale_group) == 'L') return false;
    }
    return true;
}

// Room for a new troop: a matching stack or an empty slot.
static bool army_has_room(const Game *g, const char *troop_id) {
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (g->army[i].id[0] && strcmp(g->army[i].id, troop_id) == 0) return true;
        if (!g->army[i].id[0] || g->army[i].count == 0) return true;
    }
    return false;
}

// Buyable count of troop under PERMANENT control (leadership_base -- the weekly
// reset floor, so recruits never defect at the next week boundary) and the
// gold reserve. Held counts of the same troop eat into the control budget.
static int base_control_cap(const Game *g, const TroopDef *td) {
    if (!td || td->hit_points <= 0) return 0;
    long held = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++)
        if (g->army[i].id[0] && strcmp(g->army[i].id, td->id) == 0)
            held += g->army[i].count;
    long cap = g->stats.leadership_base / td->hit_points - held;
    return cap > 0 ? (int)cap : 0;
}

// How many of `td` the player would buy right now under all the purchase
// rules (morale fit, slot room, permanent control, gold reserve, upkeep
// headroom, minimum meaningful size). The ONE arithmetic shared by the
// dwelling prompt answer and the dwelling goal scan, so a target is never
// picked that the prompt will refuse (that mismatch livelocked runs).
static int recruit_count_for(const Game *g, const TroopDef *td) {
    if (!td || td->recruit_cost <= 0) return 0;
    if (!morale_safe_with(g, td->id) || !army_has_room(g, td->id)) return 0;
    // Until the siege kit is owned, its price is part of the untouchable
    // reserve -- castles gate the whole mid-game and recruiting must never
    // starve the purchase (a travel escort is exempt via the caller's gate).
    long reserve = DEMO_GOLD_RESERVE;
    if (!g->stats.siege_weapons && demo_army_power(g) >= 150)
        reserve += g->res->economy.siege_cost + 500;
    long afford = ((long)g->stats.gold - reserve) / td->recruit_cost;
    int cap = base_control_cap(g, td);
    int n = (afford < cap) ? (int)afford : cap;
    // The engine refuses any buy beyond LIVE leadership (battle losses can
    // pull leadership_current under base until the week resets it).
    int live_cap = GameMaxRecruitable(g, td->id);
    if (n > live_cap) n = live_cap;
    // Peacetime: standing upkeep capped at HALF the commission so the purse
    // accumulates. WAR (a castle to take): the cap comes off -- the purse
    // exists to become the army the week it is needed (measured on seed 1:
    // leadership-sized militia beats every blocking garrison).
    if (!s_st.war) {
        long headroom = (long)g->stats.commission_weekly / 2 -
                        GameArmyWeeklyUpkeep(g);
        long per_upkeep = td->recruit_cost / 10;
        if (per_upkeep > 0 && headroom > 0 && n > headroom / per_upkeep)
            n = (int)(headroom / per_upkeep);
        if (headroom <= 0) n = 0;
        if (demo_army_power(g) >= 150 &&
            (n < 4 || (long)n * td->recruit_cost < 400)) n = 0;
    }
    return n > 0 ? n : 0;
}

// ---- Declined-fight memory ---------------------------------------------------

static long declined_at(const char *id) {
    for (int i = 0; i < s_st.declined_count; i++)
        if (strcmp(s_st.declined[i].id, id) == 0) return s_st.declined[i].at_power;
    return -1;
}

static void note_declined_ex(const char *id, long power, bool lost) {
    for (int i = 0; i < s_st.declined_count; i++)
        if (strcmp(s_st.declined[i].id, id) == 0) {
            s_st.declined[i].at_power = power;
            s_st.declined[i].lost = lost;
            return;
        }
    if (s_st.declined_count >= DEMO_DECLINED_MAX) return;
    DemoDeclined *d = &s_st.declined[s_st.declined_count++];
    snprintf(d->id, sizeof d->id, "%s", id);
    d->at_power = power;
    d->lost = lost;
}
static void note_declined(const char *id, long power) {
    note_declined_ex(id, power, false);
}

// A fight we actually LOST at (or below) this power replays the same loss --
// combat is seed-keyed. A mere decline carries no such certainty.
bool demo_known_loss(const char *id, long ours) {
    for (int i = 0; i < s_st.declined_count; i++)
        if (strcmp(s_st.declined[i].id, id) == 0)
            return s_st.declined[i].lost &&
                   ours <= s_st.declined[i].at_power;
    return false;
}

// Worth another look only once the army outgrew the last decline by a quarter.
static bool retry_worthy(const char *id, long ours) {
    long at = declined_at(id);
    return at < 0 || ours > at + at / 4;
}

// ---- Castle intel learned at siege prompts -----------------------------------

static const DemoIntel *intel_find(const char *castle_id) {
    for (int i = 0; i < s_st.intel_count; i++)
        if (strcmp(s_st.intel[i].id, castle_id) == 0) return &s_st.intel[i];
    return NULL;
}

static void intel_put(const char *castle_id, bool villain, const char *vid) {
    for (int i = 0; i < s_st.intel_count; i++)
        if (strcmp(s_st.intel[i].id, castle_id) == 0) {
            s_st.intel[i].villain = villain;
            snprintf(s_st.intel[i].villain_id, sizeof s_st.intel[i].villain_id,
                     "%s", vid ? vid : "");
            return;
        }
    if (s_st.intel_count >= DEMO_INTEL_MAX) return;
    DemoIntel *in = &s_st.intel[s_st.intel_count++];
    snprintf(in->id, sizeof in->id, "%s", castle_id);
    in->villain = villain;
    snprintf(in->villain_id, sizeof in->villain_id, "%s", vid ? vid : "");
}

// ---- Blocked-spot memory -------------------------------------------------------

#define DEMO_BLOCK_TICKS 400
// A place that KILLED us repels the frontier for much longer -- walking back
// into a death pocket was measured re-wiping the army seven times a run.
#define DEMO_DEATH_BLOCK_TICKS 6000
// Death pockets repel a neighborhood, not just the tile.
#define DEMO_BLOCK_RADIUS 2

bool demo_spot_blocked(const Game *g, int x, int y) {
    for (int i = 0; i < s_st.blocked_n; i++) {
        if (strcmp(s_st.blocked[i].zone, g->position.zone) != 0) continue;
        if (s_st.ticks - s_st.blocked_at[i] >= s_st.blocked_len[i]) continue;
        int dx = x - s_st.blocked[i].x, dy = y - s_st.blocked[i].y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        int r = (s_st.blocked_len[i] > DEMO_BLOCK_TICKS) ? DEMO_BLOCK_RADIUS : 0;
        if (dx <= r && dy <= r) return true;
    }
    return false;
}

static bool spot_blocked(const Game *g, int x, int y) {
    return demo_spot_blocked(g, x, y);
}

static void block_spot_len(const Game *g, int x, int y, int len) {
    int slot = s_st.blocked_n < 16 ? s_st.blocked_n : 0;
    for (int i = 0; i < s_st.blocked_n; i++)
        if (s_st.ticks - s_st.blocked_at[i] >= s_st.blocked_len[i]) { slot = i; break; }
    if (slot == s_st.blocked_n && s_st.blocked_n < 16) s_st.blocked_n++;
    s_st.blocked[slot].x = x;
    s_st.blocked[slot].y = y;
    snprintf(s_st.blocked[slot].zone, sizeof s_st.blocked[slot].zone, "%s",
             g->position.zone);
    s_st.blocked_at[slot] = s_st.ticks;
    s_st.blocked_len[slot] = len;
}

static void block_spot(const Game *g, int x, int y) {
    block_spot_len(g, x, y, DEMO_BLOCK_TICKS);
}

// ---- Temp death (the game's own defeat handling, engine calls only) ---------

static void demo_temp_death(Game *g, Map *map, Fog *fog, const Resources *res) {
    // Remember where we died: the frontier must stop pulling the hero back
    // into this pocket while the army is anywhere near this size.
    block_spot_len(g, g->position.x, g->position.y, DEMO_DEATH_BLOCK_TICKS);
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        g->army[i].id[0] = '\0';
        g->army[i].count = 0;
    }
    g->stats.siege_weapons = 0;
    const TroopDef *peas = troop_by_id("peasants");
    if (peas) {
        snprintf(g->army[0].id, sizeof g->army[0].id, "%s", peas->id);
        g->army[0].count = 20;
    }
    g->boat.has_boat = false;
    g->boat.x = g->boat.y = -1;
    g->boat.zone[0] = '\0';
    for (int zi = 0; zi < res->zone_count; zi++) {
        if (!res->zones[zi].is_home) continue;
        GameSwitchZone(g, map, fog, res->zones[zi].id);
        g->position.x = res->zones[zi].home_spawn_x;
        g->position.y = res->zones[zi].home_spawn_y;
        g->position.last_x = g->position.x;
        g->position.last_y = g->position.y;
        break;
    }
    g->character.mount = MOUNT_RIDE;
    g->travel_mode = TRAVEL_WALK;
    // The zone switch re-attaches a boat when the zone's hero_spawn is open
    // water (engine quirk): an unusable boat billing 500/wk forever -- half
    // the weekly commission. Cancel the phantom rental.
    if (demo_verbose())
        fprintf(stderr, "[dbg] temp-death boat=%d at (%d,%d)\n",
                (int)g->boat.has_boat, g->boat.x, g->boat.y);
    if (g->boat.has_boat) GameCancelBoat(g);
    printf("[DEMO] defeated, sent home with 20 peasants (day %d)\n",
           g->stats.days_left);
}

// ---- Combat at the prompt ----------------------------------------------------

// The engine's own combat is a pure function of (seed, encounter identity,
// mode) -- REQ-395/REQ-500 -- so running the whole fight on a DISCARDED copy
// predicts the live outcome exactly, and the per-turn record it produces is
// the very animation the viewer should see. One prediction serves both the
// decision and the presentation.
// The player-side combat driver. Demo's tuned combat policy
// (demo_combat_policy.c) is engine-typed and battle-only, and the measured
// difference at the margin is decisive: at leadership 150 the first wandering
// camp on seed 1 is lost under the stock enemy AI and won under this policy.
static int demo_player_fn(Combat *c, void *ctx) {
    (void)ctx;
    return demo_combat_policy(c, NULL);
}

// Smallest raise_control cast count k (0..held charges) whose leadership
// boost flips (mode,tgt) to a predicted win at the survivor bar; -1 when even
// every held charge loses. The copy's leadership is lifted by the engine's own
// per-cast amount, so prediction and the real pre-fight casts agree exactly.
static int demo_predict_boost(const Game *g, CombatMode mode,
                              const CombatTarget *tgt, int min_survivors,
                              CombatTurnRecord *rec, int *out_surv) {
    int ridx = spell_index_by_adventure_effect(ADV_EFFECT_RAISE_CONTROL);
    int held = (g->stats.knows_magic && ridx >= 0) ? g->spells.counts[ridx] : 0;
    for (int k = 0; k <= held; k++) {
        Game *tmp = malloc(sizeof *tmp);
        if (!tmp) return -1;
        *tmp = *g;
        tmp->stats.leadership_current += k * GameRaiseControlAmount(g);
        if (rec) rec->count = 0;
        CombatResult r = combat_run_headless_rec(tmp, mode, tgt, 256,
                                                 demo_player_fn, NULL, rec);
        int surv = 0;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++)
            if (tmp->army[i].id[0] && tmp->army[i].count > 0) surv++;
        free(tmp);
        if (r == COMBAT_RESULT_WIN && surv >= min_survivors) {
            if (out_surv) *out_surv = surv;
            return k;
        }
    }
    return -1;
}

// Fight or decline the combat flow that is up. The decision is the engine's
// own predicted outcome; a monster castle additionally needs two surviving
// stacks so one can garrison the win. Returns true (one action either way).
static bool resolve_combat_flow(Game *g, Map *map, Fog *fog,
                                const Resources *res) {
    bool siege = (pending_flow == FLOW_SIEGE_MONSTER ||
                  pending_flow == FLOW_SIEGE_VILLAIN);
    bool villain = (pending_flow == FLOW_SIEGE_VILLAIN);
    CombatTarget tgt;
    memset(&tgt, 0, sizeof tgt);
    char key[32] = { 0 };
    if (siege) {
        snprintf(key, sizeof key, "%s", pending_castle_id);
        const CastleRecord *cr = GameFindCastleConst(g, key);
        tgt.name = "Castle garrison";
        tgt.seed_key = key;
        if (cr) {
            tgt.garrison = cr->garrison;
            tgt.garrison_slots = GAME_ARMY_SLOTS;
        }
        intel_put(key, villain, (villain && cr) ? cr->villain_id : NULL);
    } else {
        snprintf(key, sizeof key, "%s", pending_foe_id);
        const FoeState *foe = GameFindFoeConst(g, key);
        tgt.name = "Hostile band";
        tgt.seed_key = key;
        if (foe) {
            tgt.garrison = foe->garrison;
            tgt.garrison_slots = GAME_ARMY_SLOTS;
        }
    }
    long ours = demo_army_power(g);
    // A villain castle is only worth a siege while its contract is active --
    // a contract-less win doesn't count the catch.
    bool contract_ok = !villain ||
        (g->contract.active_id[0] && GameFindCastleConst(g, key) &&
         strcmp(GameFindCastleConst(g, key)->villain_id,
                g->contract.active_id) == 0);
    // Cornered: no other move exists -- take the fight at any odds (losing
    // runs the game's own temp death, the escape from a sealed pocket).
    bool desperate = s_st.cornered && !siege;
    if (desperate) s_st.cornered = false;

    DemoHooks *hk = demo_hooks();
    CombatTurnRecord rec;
    memset(&rec, 0, sizeof rec);
    if (hk->animate) {
        rec.cap = 256 * 64;
        rec.entries = malloc((size_t)rec.cap * sizeof *rec.entries);
        if (!rec.entries) rec.cap = 0;
    }
    int surv = 0;
    int bar = (siege && !villain) ? 2 : 1;
    int boost = tgt.garrison
        ? demo_predict_boost(g, siege ? COMBAT_MODE_CASTLE : COMBAT_MODE_FOE,
                             &tgt, bar, (rec.cap > 0) ? &rec : NULL, &surv)
        : -1;
    bool accept = desperate || (contract_ok && boost >= 0);

    PlayerIoPresentation pres;
    if (!accept) {
        FlowAnswer no = { FLOW_ANS_NO, 0 };
        player_io_answer(g, map, fog, res, no, PLAYER_IO_COMBAT_NOT_RUN, &pres);
        if (contract_ok) note_declined(key, ours);
        free(rec.entries);
        return true;
    }
    // The raise-control lift the prediction priced: cast it for real, right
    // before the fight, so the live battle is the predicted one.
    if (boost > 0) {
        printf("[DEMO] raise control x%d before the fight (day %d)\n",
               boost, g->stats.days_left);
        for (int i = 0; i < boost; i++) GameCastRaiseControl(g);
    }
    // Show the fight the prediction recorded, then apply the identical live
    // resolution (same seed key, same armies -- byte-deterministic).
    if (hk->animate && rec.count > 0)
        hk->animate(hk->ctx, siege ? COMBAT_MODE_CASTLE : COMBAT_MODE_FOE, &rec);
    free(rec.entries);
    CombatMode mode = siege ? COMBAT_MODE_CASTLE : COMBAT_MODE_FOE;
    CombatResult r = combat_run_headless_ex(g, mode, &tgt, 256,
                                            demo_player_fn, NULL);
    FlowAnswer yes = { FLOW_ANS_YES, 0 };
    player_io_answer(g, map, fog, res, yes,
                     r == COMBAT_RESULT_WIN ? PLAYER_IO_COMBAT_WON
                                            : PLAYER_IO_COMBAT_LOST, &pres);
    if (r == COMBAT_RESULT_WIN) {
        printf("[DEMO] won %s %s (day %d)\n", siege ? "siege of" : "fight vs",
               key, g->stats.days_left);
        if (siege) {
            // Keep the castle: an empty player garrison repopulates back to
            // monsters at the week end. Park the weakest stack (and keep
            // parking while the survivors out-eat the weekly commission).
            do {
                int weak = -1;
                long worst = 0;
                int stacks = 0;
                for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
                    if (!g->army[i].id[0] || g->army[i].count <= 0) continue;
                    stacks++;
                    long p = troop_power(troop_by_id(g->army[i].id),
                                         g->army[i].count);
                    if (weak < 0 || p < worst) { weak = i; worst = p; }
                }
                if (stacks < 2 || weak < 0) break;
                if (GameGarrisonTroop(g, key, weak) != 0) break;
            } while (GameWeeklyNetGold(g) < 0);
        }
    } else {
        printf("[DEMO] lost %s %s (day %d)\n",
               siege ? "siege of" : "fight vs", key, g->stats.days_left);
        note_declined_ex(key, ours + ours / 2, true);
        if (pres.temp_death) demo_temp_death(g, map, fog, res);
    }
    return true;
}

// ---- Prompt answering ----------------------------------------------------------

static bool answer_flow(Game *g, Map *map, Fog *fog, const Resources *res) {
    PlayerIoPresentation pres;
    if (demo_verbose())
        fprintf(stderr, "[dbg] flow=%d at (%d,%d)\n", (int)pending_flow,
                g->position.x, g->position.y);
    switch (pending_flow) {
    case FLOW_CHEST_CHOICE: {
        // Leadership is forever -- the army-size currency every fight needs;
        // gold only when truly broke.
        FlowAnswer a = { g->stats.gold < 500 ? FLOW_ANS_1 : FLOW_ANS_2, 0 };
        player_io_answer(g, map, fog, res, a, PLAYER_IO_COMBAT_NOT_RUN, &pres);
        return true;
    }
    case FLOW_RECRUIT: {
        int n = recruit_count_for(g, troop_by_id(pending_dwelling_troop));
        if (demo_verbose())
            fprintf(stderr, "[dbg] recruit-answer troop=%s n=%d dw=(%d,%d)\n",
                    pending_dwelling_troop, n, pending_dwelling_x,
                    pending_dwelling_y);
        // A refused offer stays refused until the purse/army changes: skip
        // this dwelling for a while instead of re-peeking it every tick.
        int dwx = pending_dwelling_x, dwy = pending_dwelling_y;
        if (n < 1) block_spot(g, dwx, dwy);
        long gold_before = g->stats.gold;
        FlowAnswer a = { n > 0 ? FLOW_ANS_YES : FLOW_ANS_NO, n };
        player_io_answer(g, map, fog, res, a, PLAYER_IO_COMBAT_NOT_RUN, &pres);
        // A YES the engine silently refused (location/leadership rc paths
        // spend nothing) would re-offer forever: skip this dwelling a while.
        if (n > 0 && g->stats.gold == gold_before) block_spot(g, dwx, dwy);
        return true;
    }
    case FLOW_ACCEPT_FRIENDLY: {
        // Free troops: yes when they'd stay under control; the foe is consumed
        // either way.
        const TroopDef *td = troop_by_id(pending_dwelling_troop);
        bool ok = true;
        if (td && (long)td->hit_points * pending_friendly_count >
                      g->stats.leadership_base)
            ok = false;
        if (td && !morale_safe_with(g, td->id)) ok = false;
        FlowAnswer a = { ok ? FLOW_ANS_YES : FLOW_ANS_NO, 0 };
        player_io_answer(g, map, fog, res, a, PLAYER_IO_COMBAT_NOT_RUN, &pres);
        return true;
    }
    case FLOW_ALCOVE: {
        FlowAnswer a = { (!g->stats.knows_magic &&
                          g->stats.gold > res->economy.alcove_cost)
                             ? FLOW_ANS_YES : FLOW_ANS_NO, 0 };
        player_io_answer(g, map, fog, res, a, PLAYER_IO_COMBAT_NOT_RUN, &pres);
        return true;
    }
    case FLOW_SIEGE_MONSTER:
    case FLOW_SIEGE_VILLAIN:
    case FLOW_ATTACK_FOE:
        return resolve_combat_flow(g, map, fog, res);
    default: {
        // A flow this player has no policy for: bow out.
        FlowAnswer a = { FLOW_ANS_NO, 0 };
        player_io_answer(g, map, fog, res, a, PLAYER_IO_COMBAT_NOT_RUN, &pres);
        return true;
    }
    }
}

// ---- Standing-in-a-place business ---------------------------------------------

// Transactions available while the town marker is set. One action per call.
static bool town_business(Game *g, const Resources *res) {
    if (!g->position.in_town[0]) return false;
    DemoHooks *hk = demo_hooks();
    // Gather information (free): the town names its intel castle's ruler --
    // the game's own scouting screen. Remember what it teaches.
    {
        const ResTown *rt = resources_town_by_id(res, g->position.in_town);
        if (rt && rt->intel_castle[0]) {
            const CastleRecord *cr = GameFindCastleConst(g, rt->intel_castle);
            if (cr && cr->owner_kind == CASTLE_OWNER_VILLAIN)
                intel_put(cr->id, true, cr->villain_id);
            else if (cr && cr->owner_kind == CASTLE_OWNER_MONSTERS)
                intel_put(cr->id, false, NULL);
        }
    }
    if (!g->contract.active_id[0]) {
        if (hk->town) hk->town(hk->ctx, DEMO_TOWN_CONTRACT);
        if (GameTakeNextContract(g)) {
            printf("[DEMO] contract: %s (day %d)\n", g->contract.active_id,
                   g->stats.days_left);
            return true;
        }
    }
    // Siege weapons: the gate of every hostile castle silently bounces the
    // hero without them (DOS-faithful) -- the one-time purchase unlocks sieges.
    if (!g->stats.siege_weapons &&
        g->stats.gold > res->economy.siege_cost + 500) {
        if (hk->town) hk->town(hk->ctx, DEMO_TOWN_SIEGE);
        if (GameBuySiege(g) == SIEGE_BUY_OK) {
            printf("[DEMO] bought siege weapons (day %d)\n", g->stats.days_left);
            return true;
        }
    }
    // Raise-control charges are the fight-size lever: stock the book whenever
    // this town sells them (the town menu names its spell -- player-legal).
    if (g->stats.knows_magic) {
        int ridx = spell_index_by_adventure_effect(ADV_EFFECT_RAISE_CONTROL);
        const SpellDef *rsp = spell_by_index(ridx);
        const TownRecord *tr = NULL;
        for (int i = 0; i < GAME_TOWNS; i++)
            if (g->towns[i].id[0] &&
                strcmp(g->towns[i].id, g->position.in_town) == 0) {
                tr = &g->towns[i];
                break;
            }
        if (rsp && tr && tr->spell_for_sale[0] &&
            spell_index_by_id(tr->spell_for_sale) == ridx &&
            GameKnownSpells(g) < g->stats.max_spells &&
            g->stats.gold > rsp->cost + 600) {
            if (hk->town) hk->town(hk->ctx, DEMO_TOWN_SPELL);
            int bought = 0;
            while (g->stats.gold > rsp->cost + 600 &&
                   GameBuySpell(g, g->position.in_town) == SPELL_BUY_OK)
                bought++;
            if (bought > 0) {
                printf("[DEMO] bought %d raise_control charge%s (day %d)\n",
                       bought, bought == 1 ? "" : "s", g->stats.days_left);
                return true;
            }
        }
    }
    // Rent only when a goal actually needs the water (the weekly fee bites).
    // The standing reserve exists to protect this very fare -- renting is what
    // it was saved for, so only the fare itself gates here. An existing but
    // UNBOARDABLE boat (the engine can strand one on open water -- e.g. the
    // water hero_spawn attach) is cancelled first: re-renting parks it at
    // this town's dock, which is boardable by design.
    if (s_st.want_boat && g->stats.gold > GameBoatCost(g) + 100) {
        const ResTown *rt = resources_town_by_id(res, g->position.in_town);
        if (rt && rt->boat_x >= 0) {
            if (g->boat.has_boat && GameCancelBoat(g) != BOAT_CANCEL_OK)
                return false;
            if (hk->town) hk->town(hk->ctx, DEMO_TOWN_BOAT);
            if (GameRentBoat(g, rt->boat_x, rt->boat_y, rt->zone) ==
                BOAT_RENT_OK) {
                printf("[DEMO] rented a boat at %s (day %d)\n", rt->id,
                       g->stats.days_left);
                s_st.want_boat = false;
                return true;
            }
        }
    }
    return false;
}

// The home-pool troop purchase that adds the most power right now, or NULL.
static const TroopDef *best_home_buy(const Game *g, int *out_n) {
    const TroopDef *best = NULL;
    int best_n = 0;
    long best_p = 0;
    for (int i = 0; i < troops_count(); i++) {
        const TroopDef *td = troop_by_index(i);
        if (!td || strcmp(td->dwelling, "castle") != 0) continue;
        if (td->recruit_cost <= 0) continue;
        int n = recruit_count_for(g, td);
        if (n < 1) continue;
        long p = troop_power(td, n);
        if (p > best_p) { best = td; best_n = n; best_p = p; }
    }
    if (out_n) *out_n = best_n;
    return best;
}

// Home-castle recruiting while standing on the audience gate.
static bool home_business(Game *g) {
    if (!g->position.home_castle[0]) return false;
    if (g->stats.gold <= 600) return false;
    if (!g->stats.siege_weapons && demo_army_power(g) >= 150 && !s_st.war)
        return false;
    int n = 0;
    const TroopDef *best = best_home_buy(g, &n);
    if (!best) return false;
    if (GameBuyTroop(g, best->id, n) != 0) return false;
    printf("[DEMO] recruited %d %s (day %d)\n", n, best->id,
           g->stats.days_left);
    return true;
}

// ---- The scan: what does the player see worth doing? ---------------------------

typedef struct {
    int  pickup_x, pickup_y, pickup_d;
    int  friendly_x, friendly_y, friendly_d;
    int  alcove_x, alcove_y, alcove_d;
    int  town_x, town_y, town_d;             // unvisited town
    int  anytown_x, anytown_y, anytown_d;    // nearest town at all (boat rent)
    int  castle_x, castle_y, castle_d;       // peek/fight-worthy castle gate
    int  foe_x, foe_y, foe_d;                // winnable-looking hostile
    int  dwell_x, dwell_y, dwell_d;          // recruitable dwelling
    int  home_x, home_y, home_d;             // home gate, when recruiting there pays
    int  charge_x, charge_y, charge_d;       // visited town selling raise_control
} DemoScan;

static void scan_zone(const Game *g, const Map *map, const Fog *fog,
                      const Resources *res, const DemoField *pf, DemoScan *sc) {
    memset(sc, -1, sizeof *sc);
    long ours = demo_army_power(g);
    bool rich = g->stats.gold > 4000;
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            if (!FogSeen(fog, x, y)) continue;
            const Tile *t = MapGetTile(map, x, y);
            if (!t || t->interactive == INTERACT_NONE) continue;
            if (spot_blocked(g, x, y)) continue;
            int d = demo_field_dist(pf, map, x, y);
            if (d < 0) continue;
            switch (t->interactive) {
            case INTERACT_TREASURE_CHEST:
            case INTERACT_ARTIFACT:
            case INTERACT_NAVMAP:
            case INTERACT_ORB:
                if (sc->pickup_d < 0 || d < sc->pickup_d) {
                    sc->pickup_x = x; sc->pickup_y = y; sc->pickup_d = d;
                }
                break;
            case INTERACT_ALCOVE:
                if (!g->stats.knows_magic &&
                    g->stats.gold > res->economy.alcove_cost + 7000 &&
                    (sc->alcove_d < 0 || d < sc->alcove_d)) {
                    sc->alcove_x = x; sc->alcove_y = y; sc->alcove_d = d;
                }
                break;
            case INTERACT_TOWN: {
                if (sc->anytown_d < 0 || d < sc->anytown_d) {
                    sc->anytown_x = x; sc->anytown_y = y; sc->anytown_d = d;
                }
                const TownRecord *tr = NULL;
                for (int i = 0; i < GAME_TOWNS; i++)
                    if (g->towns[i].id[0] &&
                        strcmp(g->towns[i].id, t->id) == 0) {
                        tr = &g->towns[i];
                        break;
                    }
                if ((!tr || !tr->visited) && (sc->town_d < 0 || d < sc->town_d)) {
                    sc->town_x = x; sc->town_y = y; sc->town_d = d;
                }
                break;
            }
            case INTERACT_CASTLE_GATE: {
                if (!t->id[0]) break;
                if (!g->stats.siege_weapons) break;   // gates bounce without them
                const ResCastle *rc = resources_castle_by_id(res, t->id);
                if (!rc || resources_castle_is_home(rc)) break;
                const CastleRecord *cr = GameFindCastleConst(g, t->id);
                if (cr && cr->visited && cr->owner_kind == CASTLE_OWNER_PLAYER)
                    break;                        // ours already
                // What the prompts taught us: a villain castle is a target
                // only while its lord is under contract; monster castles are
                // gated by the declined-power memory; unknown owners are
                // always worth a look (the peek is free).
                const DemoIntel *in = intel_find(t->id);
                if (in && in->villain &&
                    (!g->contract.active_id[0] ||
                     strcmp(in->villain_id, g->contract.active_id) != 0))
                    break;
                if (in && !retry_worthy(t->id, ours)) break;
                if (sc->castle_d < 0 || d < sc->castle_d) {
                    sc->castle_x = x; sc->castle_y = y; sc->castle_d = d;
                }
                break;
            }
            case INTERACT_FOE: {
                const FoeState *foe = NULL;
                for (int i = 0; i < g->foe_count; i++)
                    if (g->foes[i].alive &&
                        strcmp(g->foes[i].zone, g->position.zone) == 0 &&
                        g->foes[i].x == x && g->foes[i].y == y) {
                        foe = &g->foes[i];
                        break;
                    }
                if (!foe) break;
                if (foe->friendly) {
                    if (sc->friendly_d < 0 || d < sc->friendly_d) {
                        sc->friendly_x = x; sc->friendly_y = y; sc->friendly_d = d;
                    }
                    break;
                }
                long theirs = demo_units_power(foe->garrison, GAME_ARMY_SLOTS);
                if (theirs <= 0 || ours * 10 < theirs * DEMO_MARGIN_FIELD) break;
                if (!retry_worthy(foe->placement_id, ours)) break;
                if (sc->foe_d < 0 || d < sc->foe_d) {
                    sc->foe_x = x; sc->foe_y = y; sc->foe_d = d;
                }
                break;
            }
            case INTERACT_DWELLING_PLAINS:
            case INTERACT_DWELLING_FOREST:
            case INTERACT_DWELLING_HILLS:
            case INTERACT_DWELLING_DUNGEON: {
                // While saving for siege weapons, only a travel escort is
                // recruited (the kit unlocks everything castles give).
                if (!g->stats.siege_weapons && demo_army_power(g) >= 150 &&
                    !s_st.war) break;
                if (!rich) break;
                // A dwelling already inspected is a target only if the buy
                // arithmetic would actually purchase there today.
                const DwellingState *ds = NULL;
                for (int i = 0; i < g->dwelling_count; i++)
                    if (g->dwellings[i].x == x && g->dwellings[i].y == y &&
                        strcmp(g->dwellings[i].zone, g->position.zone) == 0) {
                        ds = &g->dwellings[i];
                        break;
                    }
                if (ds && (ds->count <= 0 ||
                           recruit_count_for(g, troop_by_id(ds->troop_id)) < 1))
                    break;
                if (sc->dwell_d < 0 || d < sc->dwell_d) {
                    sc->dwell_x = x; sc->dwell_y = y; sc->dwell_d = d;
                }
                break;
            }
            default:
                break;
            }
        }
    }
    // Castles the town intel located are targets by their map position (the
    // intel screen and the world map are the player's own knowledge) -- no
    // need to have laid eyes on the gate tile first.
    if (g->stats.siege_weapons) {
        long ours_i = demo_army_power(g);
        for (int i = 0; i < s_st.intel_count; i++) {
            const DemoIntel *in = &s_st.intel[i];
            const CastleRecord *cr = GameFindCastleConst(g, in->id);
            if (cr && cr->visited && cr->owner_kind == CASTLE_OWNER_PLAYER)
                continue;
            if (in->villain &&
                (!g->contract.active_id[0] ||
                 strcmp(in->villain_id, g->contract.active_id) != 0))
                continue;
            if (!retry_worthy(in->id, ours_i)) continue;
            const ResCastle *rc = resources_castle_by_id(res, in->id);
            if (!rc || strcmp(rc->zone, g->position.zone) != 0) continue;
            if (spot_blocked(g, rc->x, rc->y)) continue;
            int d = demo_field_dist(pf, map, rc->x, rc->y);
            if (d >= 0 && (sc->castle_d < 0 || d < sc->castle_d)) {
                sc->castle_x = rc->x; sc->castle_y = rc->y; sc->castle_d = d;
            }
        }
    }

    // A visited town that sells raise_control is worth a restock trip whenever
    // the book has room: charges are the fight-size lever (the shop stock is
    // player memory -- the town menu named its spell on the visit).
    if (g->stats.knows_magic) {
        int ridx = spell_index_by_adventure_effect(ADV_EFFECT_RAISE_CONTROL);
        const SpellDef *rsp = spell_by_index(ridx);
        if (rsp && GameKnownSpells(g) < g->stats.max_spells &&
            g->stats.gold > rsp->cost + 600) {
            for (int i = 0; i < GAME_TOWNS; i++) {
                const TownRecord *tr = &g->towns[i];
                if (!tr->id[0] || !tr->visited || !tr->spell_for_sale[0]) continue;
                if (spell_index_by_id(tr->spell_for_sale) != ridx) continue;
                const ResTown *rt = resources_town_by_id(res, tr->id);
                if (!rt || strcmp(rt->zone, g->position.zone) != 0) continue;
                int d = demo_field_dist(pf, map, rt->x, rt->y);
                if (d >= 0 && (sc->charge_d < 0 || d < sc->charge_d)) {
                    sc->charge_x = rt->x; sc->charge_y = rt->y; sc->charge_d = d;
                }
            }
        }
    }

    // The home castle sells the good troops: worth the trip whenever a real
    // purchase is possible. Location is public knowledge (the King's castle);
    // the walk there still needs explored ground (field dist).
    const ResCastle *home = resources_home_castle(res);
    if (home && strcmp(home->zone, g->position.zone) == 0 &&
        (s_st.war || g->stats.siege_weapons || demo_army_power(g) < 150) &&
        g->stats.gold > 600 && best_home_buy(g, NULL)) {
        int d = demo_field_dist(pf, map, home->x, home->y);
        if (d >= 0) { sc->home_x = home->x; sc->home_y = home->y; sc->home_d = d; }
    }
}

// ---- Zone travel ------------------------------------------------------------------

// Where should the player sail next? -1 = nowhere useful.
static int pick_sail_zone(const Game *g, const Resources *res) {
    int cur = resources_zone_index(res, g->position.zone);
    // The active contract names the villain's home zone (the contract view
    // tells the player as much).
    if (g->contract.active_id[0]) {
        const VillainDef *v = villain_by_id(g->contract.active_id);
        if (v) {
            int zi = resources_zone_index(res, v->zone);
            if (zi >= 0 && zi < GAME_CONTINENTS && zi != cur &&
                g->world.zones_discovered[zi])
                return zi;
        }
    }
    for (int zi = 0; zi < res->zone_count && zi < GAME_CONTINENTS; zi++) {
        if (zi == cur || !g->world.zones_discovered[zi]) continue;
        if (!s_st.zone_done[zi]) return zi;
    }
    // A scepter candidate in another zone still justifies the trip.
    for (int i = 0; i < s_st.cand_count; i++)
        if (strcmp(s_st.cand[i].zone, g->position.zone) != 0) {
            int zi = resources_zone_index(res, s_st.cand[i].zone);
            if (zi >= 0 && zi != cur) return zi;
        }
    return -1;
}

// ---- Digging ---------------------------------------------------------------------

static bool dig_here(Game *g, const Resources *res) {
    DemoState *st = &s_st;
    bool won = false, over = false;
    int comm = 0;
    FlowAnswer yes = { FLOW_ANS_YES, 0 };
    st->digs++;
    printf("[DEMO] digging at %s (%d,%d), candidate %d of %d (day %d)\n",
           g->position.zone, g->position.x, g->position.y,
           1, st->cand_total, g->stats.days_left);
    flow_apply_search(g, res, yes, &won, &over, &comm);
    if (!won && st->searched_count < DEMO_SEARCHED_MAX) {
        DemoSpot *s = &st->searched[st->searched_count++];
        snprintf(s->zone, sizeof s->zone, "%s", g->position.zone);
        s->x = g->position.x;
        s->y = g->position.y;
    }
    return true;
}

// ---- One step of a committed walk ---------------------------------------------------

static bool walk_toward(Game *g, Map *map, Fog *fog, const Resources *res,
                        int gx, int gy) {
    int dx = 0, dy = 0;
    if (!demo_path_step(g, map, fog, gx, gy, &dx, &dy)) return false;
    if (dx == 0 && dy == 0) return false;   // already there
    PendingFlow flow_before = pending_flow;
    bool t_before = g->position.in_town[0] != 0;
    bool h_before = g->position.home_castle[0] != 0;
    bool o_before = g->position.own_castle[0] != 0;
    bool moved = GameStep(g, map, fog, res, dx, dy);
    // A bounced step still ACTED when THIS step fired an interaction (a flow
    // opened, a town/castle marker newly set). A refused step with no effect
    // reports false -- the caller drops and remembers the dead goal.
    bool fired = (pending_flow != FLOW_NONE && flow_before == FLOW_NONE) ||
                 (!t_before && g->position.in_town[0]) ||
                 (!h_before && g->position.home_castle[0]) ||
                 (!o_before && g->position.own_castle[0]);
    if (!moved && !fired) {
        int adx = gx - g->position.x, ady = gy - g->position.y;
        if (adx >= -1 && adx <= 1 && ady >= -1 && ady <= 1)
            block_spot(g, gx, gy);   // the lunge itself is dead
    }
    return moved || fired;
}

// ---- The tick -------------------------------------------------------------------------

bool demo_brain_tick(Game *g, Map *map, Fog *fog, const Resources *res) {
    DemoState *st = &s_st;
    if (st->done) return false;
    st->ticks++;

    if (g->stats.won) {
        st->done = true;
        st->won = true;
        snprintf(st->end_reason, sizeof st->end_reason, "scepter recovered");
        return false;
    }
    if (g->stats.game_over) {
        st->done = true;
        snprintf(st->end_reason, sizeof st->end_reason, "out of days");
        return false;
    }

    // Stuck watchdog: a cheap world signature; unchanged too long = no ideas.
    {
        long sig = g->stats.gold * 31L + g->stats.days_left * 7L +
                   g->position.x * 131L + g->position.y * 17L +
                   g->consumed_count * 977L + GameVillainsCaught(g) * 3163L +
                   GameArtifactsFound(g) * 419L + g->position.zone[0] * 13L;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++)
            sig += g->army[i].count * (i + 2L);
        if (sig == st->last_sig) {
            if (++st->same_sig_ticks > DEMO_STUCK_TICKS) {
                st->done = true;
                st->stuck = true;
                snprintf(st->end_reason, sizeof st->end_reason, "stuck");
                return false;
            }
        } else {
            st->last_sig = sig;
            st->same_sig_ticks = 0;
        }
    }

    player_io_drain_messages(g);

    // Boat economics: the rental bills 500 a week -- half the base commission.
    // A boat that hasn't sailed in ~2-3 weeks of decisions is a money leak; a
    // player cancels and re-rents when the water calls again.
    if (g->travel_mode == TRAVEL_BOAT) st->last_sail_tick = st->ticks;
    if (g->boat.has_boat && g->travel_mode == TRAVEL_WALK &&
        st->ticks - st->last_sail_tick > 800) {
        if (GameCancelBoat(g) == BOAT_CANCEL_OK)
            printf("[DEMO] cancelled the boat rental (day %d)\n",
                   g->stats.days_left);
    }

    // An open prompt is always the next decision.
    if (pending_flow != FLOW_NONE) {
        st->waits = 0;
        PendingFlow before = pending_flow;
        bool acted = answer_flow(g, map, fog, res);
        if (pending_flow == before) {
            // The queue lost the decision request (it is a bounded 8-slot
            // FIFO; a raise into a full queue is dropped) -- the answer
            // routed nowhere and the flow would hang forever. Drop the
            // phantom prompt and avoid its tile for a while.
            pending_reset();
            block_spot(g, pending_dwelling_x, pending_dwelling_y);
        }
        return acted;
    }

    // Business at the spot we're standing on (set by the last step).
    if (town_business(g, res)) { st->waits = 0; return true; }
    if (home_business(g)) { st->waits = 0; return true; }

    // Insolvent and broke: shed the weakest stack (upkeep relief).
    if (GameWeeklyNetGold(g) < 0 && g->stats.gold < GameArmyWeeklyUpkeep(g) &&
        GameArmyStackCount(g) > 1) {
        int weak = -1;
        long worst = 0;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (!g->army[i].id[0] || g->army[i].count <= 0) continue;
            long p = troop_power(troop_by_id(g->army[i].id), g->army[i].count);
            if (weak < 0 || p < worst) { weak = i; worst = p; }
        }
        if (weak >= 0) {
            FlowAnswer pick = { (PromptAnswer)(FLOW_ANS_1 + weak), 0 };
            int slot_out = 0;
            if (!flow_apply_dismiss_army(g, pick, &slot_out)) {
                st->waits = 0;
                return true;
            }
        }
    }

    st->cornered = false;   // a one-shot intent; stale by the next decision
    st->rings_off = false;

    // COMMITTED GOAL: keep walking to the chosen target until it resolves.
    // Re-picking every tick is how the loop dithered between equidistant
    // targets and re-walked ground it had already consumed.
    if (st->goal_active) {
        bool valid = strcmp(st->goal_zone, g->position.zone) == 0;
        if (valid && st->goal_interact != (int)INTERACT_NONE) {
            const Tile *t = MapGetTile(map, st->goal_x, st->goal_y);
            valid = t && (int)t->interactive == st->goal_interact;
        }
        if (valid) {
            st->rings_off = st->goal_rings_off;
            bool stepped = walk_toward(g, map, fog, res, st->goal_x, st->goal_y);
            st->rings_off = false;
            // A bouncer goal (gate/town/dwelling/foe tile) never gets stood
            // ON -- arrival is the adjacent step that fired its flow. Clear
            // the goal there or the walk re-bounces forever.
            int adx = st->goal_x - g->position.x;
            int ady = st->goal_y - g->position.y;
            if (adx >= -1 && adx <= 1 && ady >= -1 && ady <= 1)
                st->goal_active = false;
            if (stepped) {
                st->waits = 0;
                return true;
            }
        }
        st->goal_active = false;   // arrived or invalid: decide fresh
    }

    demo_scepter_update(g, fog);

    static DemoField pf;
replan:
    demo_field_build(g, map, fog, &pf);
    DemoScan sc;
    scan_zone(g, map, fog, res, &pf, &sc);
    st->war = sc.castle_d >= 0;
    if (!st->war && g->contract.active_id[0])
        for (int i = 0; i < st->intel_count && !st->war; i++)
            if (st->intel[i].villain &&
                strcmp(st->intel[i].villain_id, g->contract.active_id) == 0)
                st->war = true;

    // Calendar pressure: with a workable candidate list and the clock dying,
    // dig now -- each dig costs the search fee in days.
    DemoSpot cand;
    bool have_cand = demo_scepter_nearest(g, &cand);
    int fee = res->tuning.search_cost_days > 0 ? res->tuning.search_cost_days : 10;
    // Digs cost real days (the search fee each): dig early ONLY when the
    // deduction is certain (one candidate); dig on a short list only under
    // calendar pressure; never churn through a long list.
    bool forced_dig = have_cand && st->cand_total >= 1 && st->cand_total <= 3 &&
                      g->stats.days_left < fee * st->cand_total + 30;
    // The endgame: when the run will die with time for only a few digs, dig
    // regardless of list length -- losing with candidates untried scores the
    // same as losing without them, and any hit wins the game.
    if (have_cand && !forced_dig && g->stats.days_left < 6 * fee + 10)
        forced_dig = true;

    int gx = -1, gy = -1;
    const char *why = NULL;

    if (forced_dig && strcmp(cand.zone, g->position.zone) == 0) {
        if (g->position.x == cand.x && g->position.y == cand.y) {
            st->waits = 0;
            return dig_here(g, res);
        }
        gx = cand.x; gy = cand.y; why = "dig";
    } else if (sc.friendly_d >= 0) {
        gx = sc.friendly_x; gy = sc.friendly_y; why = "join";
    } else if (sc.pickup_d >= 0) {
        gx = sc.pickup_x; gy = sc.pickup_y; why = "pickup";
    } else if (sc.alcove_d >= 0) {
        gx = sc.alcove_x; gy = sc.alcove_y; why = "alcove";
    } else if (sc.charge_d > 0) {
        gx = sc.charge_x; gy = sc.charge_y; why = "charges";
    } else if (sc.town_d >= 0) {
        gx = sc.town_x; gy = sc.town_y; why = "town";
    } else if (sc.castle_d >= 0) {
        gx = sc.castle_x; gy = sc.castle_y; why = "castle";
    } else if (sc.foe_d >= 0) {
        gx = sc.foe_x; gy = sc.foe_y; why = "foe";
    } else if (!g->stats.siege_weapons && sc.anytown_d > 0 &&
               g->stats.gold > res->economy.siege_cost + 500) {
        gx = sc.anytown_x; gy = sc.anytown_y; why = "armory";
    } else if (!st->war && sc.castle_d < 0 && st->cand_total == 0 &&
               pick_sail_zone(g, res) >= 0) {
        // A discovered continent waits, no campaign target holds us here,
        // and the puzzle fragment matches nothing explored -- the scepter
        // and the remaining lords are elsewhere. Cross now; chests and
        // skirmishes don't outrank the ocean.
        int dest = pick_sail_zone(g, res);
        if (g->travel_mode == TRAVEL_BOAT) {
            int zi2 = resources_zone_index(res, g->position.zone);
            if (zi2 >= 0 && zi2 < GAME_CONTINENTS) st->zone_done[zi2] = true;
            int comm = 0;
            printf("[DEMO] sailing to %s (day %d)\n",
                   res->zones[dest].id, g->stats.days_left);
            if (GameSwitchZone(g, map, fog, res->zones[dest].id)) {
                GameSpendWeek(g, &comm);
                FogReveal(fog, map, g->position.x, g->position.y,
                          res->world.fog_sight);
                st->waits = 0;
                return true;
            }
        } else if (g->boat.has_boat &&
                   strcmp(g->boat.zone, g->position.zone) == 0 &&
                   demo_field_dist(&pf, map, g->boat.x, g->boat.y) >= 0) {
            gx = g->boat.x; gy = g->boat.y; why = "board";
        } else if (!g->boat.has_boat && sc.anytown_d >= 0 &&
                   g->stats.gold > GameBoatCost(g) + 100) {
            st->want_boat = true;
            gx = sc.anytown_x; gy = sc.anytown_y; why = "rent";
        }
        if (gx < 0 && sc.dwell_d >= 0) {
            gx = sc.dwell_x; gy = sc.dwell_y; why = "recruit";
        }
    } else if (sc.dwell_d >= 0) {
        gx = sc.dwell_x; gy = sc.dwell_y; why = "recruit";
    } else if (sc.home_d > 0) {
        gx = sc.home_x; gy = sc.home_y; why = "home";
    }

    if (gx < 0) {
        // Nothing seen worth doing: idle dig, explore, sail, or wait.
        if (have_cand && st->cand_total == 1 &&
            strcmp(cand.zone, g->position.zone) == 0) {
            if (g->position.x == cand.x && g->position.y == cand.y) {
                st->waits = 0;
                return dig_here(g, res);
            }
            gx = cand.x; gy = cand.y; why = "dig";
        } else {
            int fx, fy;
            if (demo_frontier_pick(g, map, fog, &pf, &fx, &fy)) {
                gx = fx; gy = fy; why = "explore";
            }
        }
    }

    if (gx < 0) {
        // Zone exhausted on foot. The water is the other half of the map:
        // rent/board a boat to keep exploring, or sail to another zone.
        int zi = resources_zone_index(res, g->position.zone);
        int dest = pick_sail_zone(g, res);
        if (dest >= 0 && g->travel_mode == TRAVEL_BOAT) {
            if (zi >= 0 && zi < GAME_CONTINENTS) st->zone_done[zi] = true;
            int comm = 0;
            printf("[DEMO] sailing to %s (day %d)\n",
                   res->zones[dest].id, g->stats.days_left);
            if (GameSwitchZone(g, map, fog, res->zones[dest].id)) {
                GameSpendWeek(g, &comm);
                FogReveal(fog, map, g->position.x, g->position.y,
                          res->world.fog_sight);
                st->waits = 0;
                return true;
            }
        } else {
            // No USABLE boat: one parked where no foot tile can board it is
            // as good as none. Walk to a town and (re-)rent -- the dock parks
            // it boardable, and the boat layer opens the water frontier.
            bool boat_usable = false;
            if (g->boat.has_boat &&
                strcmp(g->boat.zone, g->position.zone) == 0)
                for (int y = 0; y < MAP_MAX_H && !boat_usable; y++)
                    for (int x = 0; x < MAP_MAX_W; x++)
                        if (pf.dist[1][y][x] >= 0) { boat_usable = true; break; }
            if (!boat_usable && sc.anytown_d >= 0 &&
                g->stats.gold > GameBoatCost(g) + 100) {
                st->want_boat = true;
                gx = sc.anytown_x; gy = sc.anytown_y; why = "rent";
            }
        }
    }

    if (demo_verbose()) {
        int reach = 0, reach_boat = 0;
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) {
                if (pf.dist[0][y][x] >= 0 || pf.dist[1][y][x] >= 0) reach++;
                if (pf.dist[1][y][x] >= 0) reach_boat++;
            }
        fprintf(stderr, "[dbg] t=%d pos=(%d,%d) why=%s goal=(%d,%d) reach=%d/%d "
                "gold=%d pw=%ld pick_d=%d foe_d=%d cast_d=%d home_d=%d "
                "town_d=%d any_d=%d dwell_d=%d net=%d siege=%d waits=%d cand=%d/%d lead=%d/%d war=%d intel=%d boat=%d(%d,%d,%s)\n",
                st->ticks, g->position.x, g->position.y,
                why ? why : "-", gx, gy, reach, reach_boat, g->stats.gold,
                demo_army_power(g), sc.pickup_d, sc.foe_d, sc.castle_d,
                sc.home_d, sc.town_d, sc.anytown_d, sc.dwell_d,
                GameWeeklyNetGold(g), g->stats.siege_weapons, st->waits,
                st->cand_count, st->cand_total,
                g->stats.leadership_base, g->stats.leadership_current,
                (int)st->war, st->intel_count,
                (int)g->boat.has_boat, g->boat.x, g->boat.y,
                g->boat.zone[0] ? g->boat.zone : "-");
        if (reach <= 8)
            for (int y = 0; y < 64; y++)
                for (int x = 0; x < 64; x++)
                    if (pf.dist[0][y][x] >= 0 || pf.dist[1][y][x] >= 0)
                        fprintf(stderr, "[dbg]  reachcell (%d,%d) d=%d/%d\n",
                                x, y, pf.dist[0][y][x], pf.dist[1][y][x]);
        if (g->boat.has_boat && strcmp(g->boat.zone, g->position.zone) == 0) {
            for (int y = g->boat.y - 2; y <= g->boat.y + 2; y++) {
                fprintf(stderr, "[dbg] boatnb ");
                for (int x = g->boat.x - 2; x <= g->boat.x + 2; x++) {
                    const Tile *t = MapGetTile(map, x, y);
                    fprintf(stderr, "(%2d,%2d T%d I%d f%d d%d)", x, y,
                            t ? (int)t->terrain : -1,
                            t ? (int)t->interactive : -1,
                            (int)FogSeen(fog, x, y),
                            MapInBounds(map, x, y) ? pf.dist[0][y][x] : -9);
                }
                fprintf(stderr, "\n");
            }
        }
        if (reach <= 4) {
            for (int dy = -2; dy <= 2; dy++) {
                fprintf(stderr, "[dbg]   ");
                for (int dx = -2; dx <= 2; dx++) {
                    int x = g->position.x + dx, y = g->position.y + dy;
                    const Tile *t = MapGetTile(map, x, y);
                    fprintf(stderr, "(%2d,%2d T%d I%d f%d b%d w%d)", x, y,
                            t ? (int)t->terrain : -1,
                            t ? (int)t->interactive : -1,
                            (int)FogSeen(fog, x, y),
                            t ? (int)t->blocks_foot : -1,
                            t ? (int)adventure_walkable_on_foot(t) : -1);
                }
                fprintf(stderr, "\n");
            }
        }
    }
    if (gx >= 0) {
        // Commit to static targets; foes drift and stay re-aimed per tick.
        if (why && strcmp(why, "foe") != 0 && strcmp(why, "join") != 0) {
            const Tile *t = MapGetTile(map, gx, gy);
            st->goal_active = true;
            st->goal_x = gx;
            st->goal_y = gy;
            snprintf(st->goal_zone, sizeof st->goal_zone, "%s",
                     g->position.zone);
            st->goal_interact = t ? (int)t->interactive : (int)INTERACT_NONE;
            st->goal_rings_off = st->rings_off;
        }
        if (walk_toward(g, map, fog, res, gx, gy)) {
            st->waits = 0;
            return true;
        }
        st->goal_active = false;
    }

    // Sealed in by our own caution? The lethal-foe rings can wall off the
    // whole goal list from a distance (even the town two steps away). Brave
    // them once: replan with the rings off -- the margin still declines any
    // suicidal prompt, and a collision bounces free.
    if (gx < 0 && !st->rings_off) {
        st->rings_off = true;
        goto replan;
    }

    // Nothing else to do: the hostile foes camping the roads ARE the
    // frontier now. Hunt for ANY seal-breaker: test every reachable foe,
    // weakest first, and challenge the first one the (raise-boosted)
    // prediction beats. Only when none is winnable does patience -- and
    // finally the any-odds last resort -- take over.
    {
        const FoeState *foes_r[GAME_MAX_FOES];
        long power_r[GAME_MAX_FOES];
        int nr = 0;
        for (int i = 0; i < g->foe_count; i++) {
            const FoeState *f = &g->foes[i];
            if (!f->alive || f->friendly) continue;
            if (strcmp(f->zone, g->position.zone) != 0) continue;
            if (!FogSeen(fog, f->x, f->y)) continue;
            if (demo_field_dist(&pf, map, f->x, f->y) < 0) continue;
            foes_r[nr] = f;
            power_r[nr] = demo_units_power(f->garrison, GAME_ARMY_SLOTS);
            nr++;
        }
        for (int a = 1; a < nr; a++)             // insertion sort by power
            for (int b = a; b > 0 && power_r[b] < power_r[b - 1]; b--) {
                const FoeState *tf = foes_r[b]; foes_r[b] = foes_r[b - 1];
                foes_r[b - 1] = tf;
                long tp = power_r[b]; power_r[b] = power_r[b - 1];
                power_r[b - 1] = tp;
            }
        const FoeState *target = NULL;
        for (int i = 0; i < nr && !target; i++) {
            CombatTarget jt;
            memset(&jt, 0, sizeof jt);
            jt.name = "Hostile band";
            jt.seed_key = foes_r[i]->placement_id;
            jt.garrison = foes_r[i]->garrison;
            jt.garrison_slots = GAME_ARMY_SLOTS;
            if (demo_predict_boost(g, COMBAT_MODE_FOE, &jt, 1, NULL, NULL) >= 0)
                target = foes_r[i];
        }
        // The any-odds last resort (wipes the army, forfeits the kit): after
        // doubled patience -- or IMMEDIATELY when sealed in a dead pocket,
        // where nothing is reachable and waiting changes nothing: the loss's
        // temp death is a free teleport home, and days are the resource that
        // actually runs out.
        bool pocket_dead = false;
        if (!target && nr > 0) {
            int reach = 0;
            for (int y = 0; y < MAP_MAX_H && reach <= 12; y++)
                for (int x = 0; x < MAP_MAX_W; x++)
                    if (pf.dist[0][y][x] >= 0 || pf.dist[1][y][x] >= 0) reach++;
            pocket_dead = reach <= 12;
        }
        // A rich army does NOT throw itself away the moment it's sealed --
        // a short wait first (the seal may be misread); a pauper army loses
        // nothing and leaves at once. Full patience applies outside pockets.
        bool cheap_life = demo_army_power(g) < 200;
        if (!target && nr > 0 &&
            ((pocket_dead && (cheap_life || st->waits >= 4)) ||
             st->waits >= 2 * DEMO_MAX_WAITS))
            target = foes_r[0];
        if (target) {
            int adx = target->x - g->position.x;
            int ady = target->y - g->position.y;
            st->waits = 0;
            if (adx >= -1 && adx <= 1 && ady >= -1 && ady <= 1 && (adx || ady)) {
                st->cornered = true;
                printf("[DEMO] challenging %s, nothing else left (day %d)\n",
                       target->placement_id, g->stats.days_left);
                GameStep(g, map, fog, res, adx, ady);
                return true;
            }
            if (walk_toward(g, map, fog, res, target->x, target->y))
                return true;
        }
    }

    // Desperate dig: with patience gone, a short candidate list beats idling.
    if (have_cand && st->cand_total <= 4 &&
        strcmp(cand.zone, g->position.zone) == 0 &&
        st->waits >= DEMO_MAX_WAITS) {
        if (g->position.x == cand.x && g->position.y == cand.y)
            return dig_here(g, res);
        if (walk_toward(g, map, fog, res, cand.x, cand.y)) return true;
    }

    bool saving = !g->stats.siege_weapons && GameWeeklyNetGold(g) > 0 &&
                  g->stats.gold <= res->economy.siege_cost + 2000;
    if ((st->waits < 2 * DEMO_MAX_WAITS || saving) && !g->stats.game_over) {
        int comm = 0;
        GameSpendWeek(g, &comm);
        st->waits++;
        return true;
    }

    st->done = true;
    st->stuck = true;
    snprintf(st->end_reason, sizeof st->end_reason, "no moves left");
    return false;
}
