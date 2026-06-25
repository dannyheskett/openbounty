// autoplay/exec_fight.c
//
// Executor COMBAT helpers (see exec.h / docs/EXECUTOR-REFACTOR.md):
//   exec_fight    — predict -> run headless -> answer (folds combat_policy.c)  [P1]
//   exec_garrison — garrison the weakest stack into a won castle  (HELPER #6)  [here]
//
// combat_policy.c (the player-side policy + pre-fight prediction probes) is folded
// into this TU below — its functions are exec_fight's combat internals.

#include "exec.h"

#include <stdio.h>    // printf — the army-gated DECLINE signal (unified stdout log)
#include <stdlib.h>   // malloc for the per-fight CombatTurnRecord
#include <string.h>   // memset

#include "tables.h"        // troop_by_id / TroopDef.hit_points (weakest-stack ranking)
#include "game.h"          // GameFindFoe / GameFindCastleConst / FoeState / CastleRecord
#include "combat.h"        // combat_run_headless_* / CombatTarget / CombatMode / CombatResult
#include "combat_policy.h" // autoplay_combat_policy (the player-side policy callback)
#include "recruit.h"       // predict_combat_survivors (prediction; folds into fight_predict at P6)
#include "player_io.h"     // player_io_answer / PlayerIoPresentation / PLAYER_IO_COMBAT_*
#include "pending.h"       // pending_flow / pending_foe_id / pending_castle_id / FLOW_*

// HELPER #5 — exec_fight. Resolve the combat the caller's step provoked. The mode,
// target, and win-bar are all DERIVED from the pending flow, so there is exactly one
// place that knows how to fight the current tile (callers — exec_slay, exec_siege,
// and exec_travel's blocker-clear — just step on and call this; no target-building
// is duplicated at the call sites).
//
// FOLD NOTE: drives combat_policy.c (autoplay_combat_policy) and the survivor
// prediction (predict_combat_survivors) for now; at P6 those become this helper's
// private internals — fight_policy (the callback by pointer), fight_predict, and the
// probe_* kit — with no change to this signature.
bool exec_fight(Game *g, Map *map, RecSink *rec) {
    // A friendly foe stepped-onto or collided-with during the approach leaves
    // FLOW_ACCEPT_FRIENDLY at the queue head, masking the hostile FLOW_ATTACK_FOE the
    // target raised behind it — so this would otherwise see "no combat" and defer a
    // winnable foe forever. Drain the free join(s) first (always consumed, never
    // charged); bounded, and bail if the head does not advance (never spin).
    for (int drain = 0; pending_flow == FLOW_ACCEPT_FRIENDLY && drain < GAME_ARMY_SLOTS;
         drain++) {
        PendingFlow before = pending_flow;
        FlowAnswer join = { FLOW_ANS_YES, 0 };
        exec_answer(g, map, join, rec);
        if (pending_flow == before) break;
    }

    PendingFlow flow = pending_flow;
    CombatMode mode;
    int min_survivors;
    CombatTarget tgt; memset(&tgt, 0, sizeof tgt);

    if (flow == FLOW_ATTACK_FOE) {
        mode = COMBAT_MODE_FOE;
        min_survivors = 1;                              // a foe falls to a plain win
        FoeState *foe = pending_foe_id[0] ? GameFindFoe(g, pending_foe_id) : NULL;
        tgt.name = "Hostile band";
        tgt.seed_key = pending_foe_id;
        if (foe) { tgt.garrison = foe->garrison; tgt.garrison_slots = GAME_ARMY_SLOTS; }
    } else if (flow == FLOW_SIEGE_MONSTER || flow == FLOW_SIEGE_VILLAIN) {
        bool villain = (flow == FLOW_SIEGE_VILLAIN);
        mode = COMBAT_MODE_CASTLE;
        min_survivors = villain ? 1 : CASTLE_GARRISON_MIN_SURVIVORS;
        const CastleRecord *cr =
            pending_castle_id[0] ? GameFindCastleConst(g, pending_castle_id) : NULL;
        tgt.name = villain ? "Villain" : "Castle garrison";
        tgt.seed_key = pending_castle_id;
        if (cr) { tgt.garrison = (Unit *)cr->garrison; tgt.garrison_slots = GAME_ARMY_SLOTS; }
    } else {
        return false;                                   // no combat was provoked here
    }

    // Predict on a discarded copy; enter only on a goal-win (win + enough survivors).
    // post_hp is the hp-worth of the hero's SURVIVING army (for the preservation gate).
    int surv = 0;
    long post_hp = 0;
    CombatResult pred =
        predict_combat_eval(g, mode, &tgt, NULL, &surv, NULL, NULL, &post_hp);
    PlayerIoPresentation pres;
    if (pred != COMBAT_RESULT_WIN || surv < min_survivors) {
        // ARMY-GATED DECLINE — DOCUMENT THE SIGNAL. A SLAY/SIEGE that defers
        // "blocked: executor" is ambiguous (unreachable vs unwinnable); this line makes
        // the held-army-too-weak case explicit and correlatable in the unified log: the
        // target id, whether the prediction was an outright LOSS or a win too costly to
        // hold (survivors below the bar), and the survivor shortfall.
        if (pred != COMBAT_RESULT_WIN)
            printf("exec_fight: DECLINE %s '%s' — predicted LOSS (held army too weak)\n",
                   mode == COMBAT_MODE_CASTLE ? "castle" : "foe",
                   tgt.seed_key ? tgt.seed_key : "(?)");
        else
            printf("exec_fight: DECLINE %s '%s' — would win but only %d survivor(s) < %d "
                   "needed to hold (held army too weak)\n",
                   mode == COMBAT_MODE_CASTLE ? "castle" : "foe",
                   tgt.seed_key ? tgt.seed_key : "(?)", surv, min_survivors);
        // Decline: answer NO so the engine resolves the flow without a fight. The
        // planner sees false and restores (army-gated: not winnable / too costly yet).
        FlowAnswer no = { FLOW_ANS_NO, 0 };
        player_io_answer(g, map, /*fog=*/NULL, g->res, no,
                         PLAYER_IO_COMBAT_NOT_RUN, &pres);
        if (rec && rec->prims) {
            RecPrim rp; memset(&rp, 0, sizeof rp);
            rp.kind = REC_ANSWER;
            rp.ans = no;
            rp.outcome = PLAYER_IO_COMBAT_NOT_RUN;
            rp.flow = flow;
            rp.rec_combat_index = -1;
            recbuf_push(rec->prims, rp);
        }
        return false;
    }

    // ARMY PRESERVATION (foe-only): even after the army-build pipeline fielded the
    // best-surviving composition (exec_slay), a fight that WINS only Pyrrhically — the
    // surviving army worth below PRESERVE_MIN_RATIO_PCT% of what was committed — is
    // DECLINED. Keeping the army intact is worth more than this one foe; the planner
    // defers and retries it once the hero is stronger. Scale-free ratio, see exec.h.
    if (mode == COMBAT_MODE_FOE) {
        long committed = army_hp_worth(g->army);
        if (committed > 0 && post_hp * 100 < committed * PRESERVE_MIN_RATIO_PCT) {
            printf("exec_fight: DECLINE foe '%s' — poor survival ratio "
                   "(committed=%ld survive=%ld = %ld%%)\n",
                   tgt.seed_key ? tgt.seed_key : "(?)", committed, post_hp,
                   post_hp * 100 / committed);
            FlowAnswer no = { FLOW_ANS_NO, 0 };
            player_io_answer(g, map, /*fog=*/NULL, g->res, no,
                             PLAYER_IO_COMBAT_NOT_RUN, &pres);
            if (rec && rec->prims) {
                RecPrim rp; memset(&rp, 0, sizeof rp);
                rp.kind = REC_ANSWER;
                rp.ans = no;
                rp.outcome = PLAYER_IO_COMBAT_NOT_RUN;
                rp.flow = flow;
                rp.rec_combat_index = -1;
                recbuf_push(rec->prims, rp);
            }
            return false;
        }
    }

    // Win: run the authoritative fight, recording the per-turn sequence when a sink
    // is present (visible mode animates it before applying the re-resolved outcome).
    int rec_combat_index = -1;
    if (rec && rec->combats) {
        CombatTurnRecord crec; memset(&crec, 0, sizeof crec);
        crec.cap = 256 * 64;                            // cap_rounds * 64 actions
        crec.entries = malloc((size_t)crec.cap * sizeof *crec.entries);
        if (!crec.entries) crec.cap = 0;
        combat_run_headless_rec(g, mode, &tgt, 256, autoplay_combat_policy, NULL, &crec);
        rec_combat_index = combatreclist_push(rec->combats, crec);
    } else {
        combat_run_headless_ex(g, mode, &tgt, 256, autoplay_combat_policy, NULL);
    }
    FlowAnswer yes = { FLOW_ANS_YES, 0 };
    player_io_answer(g, map, /*fog=*/NULL, g->res, yes, PLAYER_IO_COMBAT_WON, &pres);

    if (rec && rec->prims) {
        RecPrim rp; memset(&rp, 0, sizeof rp);
        rp.kind = REC_ANSWER;
        rp.ans = yes;
        rp.outcome = PLAYER_IO_COMBAT_WON;
        rp.flow = flow;
        rp.combat_mode = mode;
        rp.rec_combat_index = rec_combat_index;
        recbuf_push(rec->prims, rp);
    }
    return true;
}

// HELPER #6 — exec_garrison. Garrison the WEAKEST surviving stack (lowest
// count*hit_points; tie-break lowest slot) into a freshly-captured castle, so the
// strongest army stays with the hero for later sieges. The engine refuses (non-zero)
// if it would empty the army (a single-survivor win) or the castle is not the hero's
// to garrison, in which case we return false and record nothing. Records
// RA_GARRISON_WEAKEST for exact replay on success.
bool exec_garrison(Game *g, const char *castle_id, RecSink *rec) {
    int weakest = -1; long worst = -1;
    for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
        if (!g->army[s].id[0] || g->army[s].count <= 0) continue;
        const TroopDef *td = troop_by_id(g->army[s].id);
        long power = (long)g->army[s].count * (td ? td->hit_points : 1);
        if (worst < 0 || power < worst) { worst = power; weakest = s; }
    }
    if (weakest < 0) return false;
    if (GameGarrisonTroop(g, castle_id, weakest) != 0) return false;   // refused
    rec_push_action(rec, RA_GARRISON_WEAKEST, castle_id, 0, 0);
    return true;
}

// ===================== folded from combat_policy.c (P6: collapse sub-solver into the executor) =====================
//
// Dedicated player-side combat policy + pre-fight prediction.
//
// Built entirely on the engine's public combat primitives and catalog DATA
// (TroopDef fields, unit counts) — no hardcoded game dynamics (no specific
// troop/spell ids, no baked damage numbers). "Dangerous / ranged" is read from
// TroopDef.ranged_ammo; "low-HP stack to finish" is read from unit count.
//
// ADVENTURE spell use is deliberately NOT done here yet: the spell-index ->
// effect mapping currently lives only in src/combat_loop.c as shell constants,
// and copying it here would hardcode game dynamics into autoplay. It will be
// driven through a single engine cast entry point (shared by shell + autoplay)
// rather than duplicated — see the follow-up. The unit-tactics policy below is
// the win-tuned core (target priority, ranged neutralization, positioning).

#include "combat_policy.h"

#include <stdlib.h>   // malloc/free (Game copy is too large for the stack)
#include <string.h>   // memset (probe kit)
#include "tables.h"   // troop_by_index, TroopDef
#include "game.h"

// ---- Probe kit (Phase I): measure candidate actions, never model them ------
// A Combat is a flat value (units and maps inline; the only pointers are
// heroes[]). Copying it — plus the acting hero's Game, since casts mutate
// spell counts through heroes[side] — gives an EXACT, side-effect-free
// preview: the copy carries the same rng_state, so the probed rolls are
// precisely the rolls the real action will see, and candidates compared from
// the same state are compared fairly. No damage formula, morale multiplier,
// or ability rule is duplicated here; the engine itself produces every value.
// Static scratch: the policy never nests probes (the engine calls inside a
// probe never re-enter the policy), and the whole planner is single-threaded.
static Combat s_probe_combat;
static Game   s_probe_hero;

// Effective HP of one side: per-creature HP x count minus accumulated injury.
static long probe_side_hp(const Combat *c, int side) {
    long t = 0;
    for (int s = 0; s < COMBAT_SLOTS; s++) {
        const CombatUnit *u = &c->units[side][s];
        if (u->troop_idx < 0 || u->count <= 0) continue;
        const TroopDef *td = troop_by_index(u->troop_idx);
        long hp = td ? td->hit_points : 1;
        t += (long)u->count * hp - u->injury;
    }
    return t;
}

typedef struct {
    long hp_removed;    // enemy effective HP destroyed by the action
    long our_hp_lost;   // our effective HP lost (retaliation)
    bool eliminated;    // the target stack died
    int  kills;         // creatures killed in the target stack
} ProbeOut;

// Preview attacker (side,slot) hitting (t_side,t_slot). The CALLER guarantees
// legality (shots/adjacency); the probe just measures the engine's outcome.
static void probe_attack(const Combat *c, int side, int slot,
                         int t_side, int t_slot, bool is_ranged,
                         ProbeOut *out) {
    memset(out, 0, sizeof *out);
    s_probe_combat = *c;
    if (c->heroes[side]) {
        s_probe_hero = *c->heroes[side];
        s_probe_combat.heroes[side] = &s_probe_hero;
    }
    long pre_them = probe_side_hp(&s_probe_combat, t_side);
    long pre_us   = probe_side_hp(&s_probe_combat, side);
    int  pre_cnt  = s_probe_combat.units[t_side][t_slot].count;
    combat_hit_unit(&s_probe_combat, side, slot, t_side, t_slot, is_ranged);
    out->hp_removed  = pre_them - probe_side_hp(&s_probe_combat, t_side);
    out->our_hp_lost = pre_us - probe_side_hp(&s_probe_combat, side);
    out->kills       = pre_cnt - s_probe_combat.units[t_side][t_slot].count;
    out->eliminated  = s_probe_combat.units[t_side][t_slot].count <= 0;
}

// The damage OUTPUT enemy stack `e` can apply on its next activation, probed:
// a ranged shot at our largest stack when it has ammo and is not pinned, else
// its melee swing at the adjacent friendly it touches (first by slot), else 0
// (it spends the turn walking). HP-denominated, comparable with hp_removed.
static long probe_enemy_output(const Combat *c, int side, int e_slot) {
    int e_side = 1 - side;
    const CombatUnit *e = &c->units[e_side][e_slot];
    if (e->troop_idx < 0 || e->count <= 0 || e->frozen) return 0;
    const TroopDef *etd = troop_by_index(e->troop_idx);
    // Adjacent friendly target (first by slot) for the melee case.
    int adj = -1;
    for (int s = 0; s < COMBAT_SLOTS && adj < 0; s++) {
        const CombatUnit *u = &c->units[side][s];
        if (u->troop_idx < 0 || u->count <= 0) continue;
        int dx = u->x - e->x, dy = u->y - e->y;
        if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) adj = s;
    }
    ProbeOut po;
    if (etd && etd->ranged_ammo > 0 && e->shots > 0 &&
        !combat_unit_surrounded(c, e_side, e_slot)) {
        // Shoot our largest stack (the engine AI's far-target style).
        int big = -1; long bign = -1;
        for (int s = 0; s < COMBAT_SLOTS; s++) {
            const CombatUnit *u = &c->units[side][s];
            if (u->troop_idx < 0 || u->count <= 0) continue;
            if ((long)u->count > bign) { bign = u->count; big = s; }
        }
        if (big < 0) return 0;
        probe_attack(c, e_side, e_slot, side, big, /*is_ranged=*/true, &po);
        return po.hp_removed;
    }
    if (adj >= 0) {
        probe_attack(c, e_side, e_slot, side, adj, /*is_ranged=*/false, &po);
        return po.hp_removed;
    }
    return 0;   // out of reach this activation
}

// ---- Casting policy --------------------------------------------------------
// Probe one cast on a copy; returns true when the engine accepted it and
// fills the HP deltas (their loss, our gain) plus the post-cast state copy in
// s_probe_combat (for two-step probes like teleport-then-attack).
static bool probe_cast(const Combat *c, int side, int spell_idx,
                       int t_side, int t_slot, int dest_x, int dest_y,
                       long *their_hp_lost, long *our_hp_gained) {
    s_probe_combat = *c;
    if (c->heroes[side]) {
        s_probe_hero = *c->heroes[side];
        s_probe_combat.heroes[side] = &s_probe_hero;
    }
    int e_side = 1 - side;
    long pre_them = probe_side_hp(&s_probe_combat, e_side);
    long pre_us   = probe_side_hp(&s_probe_combat, side);
    if (combat_cast_spell(&s_probe_combat, side, spell_idx, t_side, t_slot,
                          dest_x, dest_y) != COMBAT_CAST_OK)
        return false;
    if (their_hp_lost)
        *their_hp_lost = pre_them - probe_side_hp(&s_probe_combat, e_side);
    if (our_hp_gained)
        *our_hp_gained = probe_side_hp(&s_probe_combat, side) - pre_us;
    return true;
}

// Cast at most ONE combat spell this player activation (latched by the
// engine's spells_this_round), chosen by PROBED VALUE — every candidate cast
// is previewed on a copy and scored in HP terms on one scale:
//   damage spells:   enemy HP destroyed + the output removed with any kills;
//   freeze:          the frozen enemy's probed next-activation output;
//   resurrect/clone: our HP restored/added;
//   teleport:        the probed melee swing our strongest stack can land
//                    after being dropped beside the highest-output enemy.
// The best strictly-positive candidate is cast; charges are never burned on
// a no-op. Deterministic: fixed enumeration, strict >, first-best wins.
// DIGEST SAFETY: no-ops when c->heroes[side]==NULL (AI-vs-AI digest path).
static bool try_cast_round(Combat *c, int side) {
    Game *g = c->heroes[side];
    if (!g) return false;                         // digest-path no-op
    if (!g->stats.knows_magic) return false;
    if (c->spells_this_round >= 1) return false;
    const int *cnt = g->spells.counts;
    int e_side = 1 - side;

    long best_val = 0;                            // floor: strictly positive
    int  bs = -1, bts = 0, btsl = 0, bx = 0, by = 0;
    #define CONSIDER(IDX, TSIDE, TSLOT, DX, DY, VAL)                         \
        do { if ((VAL) > best_val) { best_val = (VAL); bs = (IDX);           \
             bts = (TSIDE); btsl = (TSLOT); bx = (DX); by = (DY); } } while (0)

    // Damage spells: probe each enemy stack as the target.
    static const int DMG[3] = { COMBAT_SPELL_FIREBALL, COMBAT_SPELL_LIGHTNING,
                                COMBAT_SPELL_TURN_UNDEAD };
    for (int d = 0; d < 3; d++) {
        if (cnt[DMG[d]] <= 0) continue;
        for (int es = 0; es < COMBAT_SLOTS; es++) {
            const CombatUnit *e = &c->units[e_side][es];
            if (e->troop_idx < 0 || e->count <= 0) continue;
            long lost = 0;
            if (!probe_cast(c, side, DMG[d], e_side, es, 0, 0, &lost, NULL))
                continue;
            if (lost <= 0) continue;              // immune / no effect
            long eout = probe_enemy_output(c, side, es);
            int  post = s_probe_combat.units[e_side][es].count;
            long out_removed = post <= 0 ? eout
                               : eout * (e->count - post) / e->count;
            CONSIDER(DMG[d], e_side, es, 0, 0, lost + out_removed);
        }
    }
    // Freeze: deny the highest-output, not-yet-frozen enemy its activation.
    if (cnt[COMBAT_SPELL_FREEZE] > 0) {
        for (int es = 0; es < COMBAT_SLOTS; es++) {
            const CombatUnit *e = &c->units[e_side][es];
            if (e->troop_idx < 0 || e->count <= 0 || e->frozen) continue;
            long eout = probe_enemy_output(c, side, es);
            if (eout <= 0) continue;
            if (!probe_cast(c, side, COMBAT_SPELL_FREEZE, e_side, es, 0, 0,
                            NULL, NULL)) continue;            // immune
            if (!s_probe_combat.units[e_side][es].frozen) continue;
            CONSIDER(COMBAT_SPELL_FREEZE, e_side, es, 0, 0, eout);
        }
    }
    // Resurrect / clone: probe our own stacks; value = our HP gained.
    static const int OWN[2] = { COMBAT_SPELL_RESURRECT, COMBAT_SPELL_CLONE };
    for (int o = 0; o < 2; o++) {
        if (cnt[OWN[o]] <= 0) continue;
        for (int fs = 0; fs < COMBAT_SLOTS; fs++) {
            const CombatUnit *f = &c->units[side][fs];
            if (f->troop_idx < 0 || f->count <= 0) continue;
            long gain = 0;
            if (!probe_cast(c, side, OWN[o], side, fs, 0, 0, NULL, &gain))
                continue;
            if (gain <= 0) continue;
            CONSIDER(OWN[o], side, fs, 0, 0, gain);
        }
    }
    // Teleport: drop our strongest stack (by effective HP) beside the
    // highest-output enemy; value = the melee swing it can then land, probed
    // on the post-teleport state. Fixed cell order mirrors the fly picker.
    if (cnt[COMBAT_SPELL_TELEPORT] > 0) {
        int us = -1; long ushp = 0;
        for (int fs = 0; fs < COMBAT_SLOTS; fs++) {
            const CombatUnit *f = &c->units[side][fs];
            if (f->troop_idx < 0 || f->count <= 0) continue;
            const TroopDef *ftd = troop_by_index(f->troop_idx);
            long hp = (long)f->count * (ftd ? ftd->hit_points : 1) - f->injury;
            if (hp > ushp) { ushp = hp; us = fs; }
        }
        int te = -1; long teout = -1;  // -1: an out-of-reach enemy
                                       // (output 0) is still a target
        for (int es = 0; es < COMBAT_SLOTS; es++) {
            const CombatUnit *e = &c->units[e_side][es];
            if (e->troop_idx < 0 || e->count <= 0) continue;
            long eo = probe_enemy_output(c, side, es);
            if (eo > teout) { teout = eo; te = es; }
        }
        if (us >= 0 && te >= 0) {
            const CombatUnit *e = &c->units[e_side][te];
            const CombatUnit *f = &c->units[side][us];
            int fdx = e->x - f->x, fdy = e->y - f->y;
            bool already_adj = fdx >= -1 && fdx <= 1 && fdy >= -1 && fdy <= 1;
            if (!already_adj) {
                for (int dy = 1; dy >= -1; dy--) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = e->x + dx, ny = e->y + dy;
                        if (!combat_in_bounds(nx, ny)) continue;
                        if (c->omap[ny][nx] || c->umap[ny][nx]) continue;
                        if (!probe_cast(c, side, COMBAT_SPELL_TELEPORT,
                                        side, us, nx, ny, NULL, NULL))
                            continue;
                        // Two-step: probe the melee swing from the
                        // post-teleport state (s_probe_combat).
                        ProbeOut po;
                        probe_attack(&s_probe_combat, side, us, e_side, te,
                                     /*is_ranged=*/false, &po);
                        long val = po.hp_removed - po.our_hp_lost +
                                   (po.eliminated ? teout : 0);
                        CONSIDER(COMBAT_SPELL_TELEPORT, side, us, nx, ny, val);
                        dy = -2; break;           // first legal cell only
                    }
                }
            }
        }
    }
    #undef CONSIDER

    if (bs < 0) return false;
    return combat_cast_spell(c, side, bs, bts, btsl, bx, by) == COMBAT_CAST_OK;
}

int autoplay_combat_policy(Combat *c, void *ctx) {
    (void)ctx;
    if (c->unit_id < 0) return 0;
    int side = c->side;        // COMBAT_SIDE_PLAYER when this is invoked
    int slot = c->unit_id;
    CombatUnit *u = &c->units[side][slot];
    const TroopDef *t = troop_by_index(u->troop_idx);
    if (!t) { u->acted = true; return 1; }
    if (u->frozen) { u->acted = true; return 1; }

    // CASTING: at most one cast per player activation (latched by the
    // engine's spells_this_round, reset each side wakeup). This is a STANDALONE
    // action — it does NOT consume this unit's move/attack, so the unit still
    // proceeds to its steps below. No-ops when heroes[side]==NULL (digest safety)
    // or no owned spell has a useful legal target. Same policy runs in the
    // predictor and the live fight, so predictions reflect casting.
    try_cast_round(c, side);

    // ATTACK SELECTION (Phase I): enumerate every legal attack this unit has
    // — a ranged shot at each enemy (when ammo remains and we're not pinned)
    // and a melee swing at each ADJACENT enemy — probe each on a copy, and
    // take the highest-value one. Value, all HP-denominated and produced by
    // the engine itself:
    //     probed enemy HP destroyed
    //   + the enemy OUTPUT removed (its probed next-activation damage scaled
    //     by the fraction of its creatures killed — eliminations remove the
    //     whole turn, chip damage a proportional share)
    //   - our probed retaliation losses (zero for ranged; zero against a
    //     defender that has already retaliated this round — free damage).
    // Deterministic: fixed enumeration order, strict >, first-best wins.
    int enemy_side = 1 - side;
    bool can_shoot = (u->shots > 0) && !combat_unit_surrounded(c, side, slot);
    long best_val = -1;
    int  best_slot = -1; bool best_ranged = false;
    for (int es = 0; es < COMBAT_SLOTS; es++) {
        const CombatUnit *e = &c->units[enemy_side][es];
        if (e->troop_idx < 0 || e->count <= 0) continue;
        int dx = e->x - u->x, dy = e->y - u->y;
        bool adjacent = dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1;
        long eout = probe_enemy_output(c, side, es);
        int  ecnt = e->count;
        for (int r = 0; r < 2; r++) {
            bool ranged = (r == 0);
            if (ranged && !can_shoot) continue;
            if (!ranged && !adjacent) continue;
            ProbeOut po;
            probe_attack(c, side, slot, enemy_side, es, ranged, &po);
            long out_removed = po.eliminated
                                 ? eout
                                 : (ecnt > 0 ? eout * po.kills / ecnt : 0);
            long val = po.hp_removed + out_removed - po.our_hp_lost;
            if (val > best_val) {
                best_val = val; best_slot = es; best_ranged = ranged;
            }
        }
    }
    // Take the best attack whenever ANY attack is legal: declining to swing
    // never avoids the enemy's damage (they hit us regardless), it only
    // forfeits ours — the value ranks candidates, it is not a floor.
    if (best_slot >= 0) {
        combat_hit_unit(c, side, slot, enemy_side, best_slot, best_ranged);
        u->acted = true;
        return 1;
    }
    // No worthwhile attack (or none legal): move toward the enemy whose
    // probed next-activation output is highest (neutralize the biggest
    // threat); ties -> lowest slot.
    int focus = -1; long focus_out = -1;
    for (int es = 0; es < COMBAT_SLOTS; es++) {
        const CombatUnit *e = &c->units[enemy_side][es];
        if (e->troop_idx < 0 || e->count <= 0) continue;
        long eo = probe_enemy_output(c, side, es);
        if (eo > focus_out) { focus_out = eo; focus = es; }
    }
    if (focus < 0) { u->acted = true; return 1; }
    const CombatUnit *tgt = &c->units[enemy_side][focus];

    // 3. Fly toward the focus if able (FLY ability + flights left), landing in
    //    a free adjacent cell. Iteration order mirrors combat_ai_action for
    //    deterministic tie-breaks.
    if ((t->abilities & TROOP_ABIL_FLY) && u->flights > 0) {
        for (int dy = 1; dy >= -1; dy--) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                int nx = tgt->x + dx, ny = tgt->y + dy;
                if (!combat_in_bounds(nx, ny)) continue;
                if (c->omap[ny][nx] || c->umap[ny][nx]) continue;
                if (combat_fly_unit(c, side, slot, nx, ny)) {
                    u->acted = true;
                    return 1;
                }
            }
        }
    }

    // 4. Walk toward the focus.
    {
        int ox, oy;
        unit_move_offset(c, u, tgt->x, tgt->y, &ox, &oy);
        if ((ox != 0 || oy != 0) &&
            combat_move_unit(c, side, slot, ox, oy) != 0) {
            u->acted = true;
            return 1;
        }
    }

    // 5. Nothing better — pass.
    u->acted = true;
    return 1;
}

// ---- Pre-fight prediction ---------------------------------------------------

CombatResult autoplay_predict_combat_ex(const Game *g, CombatMode mode,
                                        const CombatTarget *target,
                                        int cap_rounds, bool *out_capped) {
    if (out_capped) *out_capped = false;
    if (!g) return COMBAT_RESULT_LOSS;
    // Snapshot the global world RNG so the prediction leaves it untouched
    // (combat itself seeds from Combat.rng_state, but be strict anyway).
    uint64_t rng = GameRngSnapshot();
    // Run the fight on a discarded COPY of the game: a winning fight mutates
    // gold/army, which must not touch the live state. Game is tens of KB —
    // far too large for the stack here: this is a LEAF of the deepest planner
    // recursion (plan_build -> ... -> autoplay_step -> predict), and a frame
    // that size at that depth overflowed the stack in visible mode.
    // Heap-allocate it.
    Game *tmp = malloc(sizeof *tmp);
    if (!tmp) { GameRngRestore(rng); return COMBAT_RESULT_LOSS; }
    *tmp = *g;                     // value copy; only pointer is res (read-only)
    // Use the recording variant only to read the terminal CombatTurnRecord.result
    // (1 win / 2 loss / 0 cap-out): a fight that exhausts max_actions returns LOSS
    // like a real wipe but is NOT a clean win — surface that (WS-10) so a capped
    // fight is never silently treated as winnable. A throwaway record buffer
    // suffices; we read only .result, then discard. (The record is observation-
    // only, so this is byte-identical to the NULL-record path — the pinned
    // combat digests are unaffected.)
    CombatTurnRecord rec = { 0 };
    rec.cap = cap_rounds > 0 ? cap_rounds * 64 : 4096;
    rec.entries = malloc((size_t)rec.cap * sizeof *rec.entries);
    if (!rec.entries) rec.cap = 0;
    CombatResult r = combat_run_headless_rec(tmp, mode, target, cap_rounds,
                                             autoplay_combat_policy, NULL, &rec);
    if (out_capped) *out_capped = (rec.result == 0);   // neither win nor loss
    free(rec.entries);
    free(tmp);
    GameRngRestore(rng);
    return r;
}

CombatResult autoplay_predict_combat(const Game *g, CombatMode mode,
                                     const CombatTarget *target,
                                     int cap_rounds) {
    return autoplay_predict_combat_ex(g, mode, target, cap_rounds, NULL);
}
