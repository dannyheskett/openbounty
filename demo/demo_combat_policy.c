// demo/demo_combat_policy.c
//
// The player-side combat policy for demo mode. Tuned to WIN (finish low-HP
// stacks, neutralize ranged, use spells when magic is known) and deterministic
// (no wall-clock, no unseeded randomness) -- distinct from the enemy
// combat_ai_action. Owned by demo/: no dependency on any other agent module.
// Engine-only: built on the public combat primitives in combat.h.

#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "combat.h"
#include "tables.h"
#include "spells_adventure.h"
#include "demo_combat_policy.h"

// Static scratch: the policy never nests probes (the engine calls inside a probe
// never re-enter the policy), and the driver is single-threaded. The candidate-
// evaluation scratch (s_cand_combat) is defined with the candidate kit below;
// s_cand_hero is declared here so probe_attack can recognize it and skip the
// dominant ~93KB Game memcpy when a hero pointer already targets scratch.
static Combat s_probe_combat;
static Game   s_probe_hero;
static Game   s_cand_hero;

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
// The PLAYER hero (the only side that ever has one) is rebound to the scratch
// copy whether it is the attacker or the DEFENDER: combat_deal_damage writes
// followers_killed through heroes[t_side], so a defender-side live pointer
// would leak probe side-effects into the real Game (score pollution).
// EXACTNESS/COST: when the hero pointer already targets policy scratch
// (s_probe_hero / s_cand_hero) the ~93KB Game copy is skipped -- the only
// hero field a probed hit writes is followers_killed, which nothing in combat
// reads back, so writing it into scratch changes no probed value.
static void probe_attack(const Combat *c, int side, int slot,
                         int t_side, int t_slot, bool is_ranged,
                         ProbeOut *out) {
    (void)side;
    memset(out, 0, sizeof *out);
    s_probe_combat = *c;
    Game *hero = c->heroes[COMBAT_SIDE_PLAYER];
    if (hero && hero != &s_probe_hero && hero != &s_cand_hero) {
        s_probe_hero = *hero;
        s_probe_combat.heroes[COMBAT_SIDE_PLAYER] = &s_probe_hero;
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

// ---- Candidate buffer: threat-aware action selection ------------------------
// A SECOND scratch state holding one candidate action's post-action world, kept
// separate from s_probe_combat because scoring a candidate calls
// probe_enemy_output on it, which itself probes through s_probe_combat.
// The hero Game is copied ONCE per activation (cand_begin), not per candidate:
// no candidate action mutates the hero except the combat-inert
// followers_killed tally, and sizeof(Game) makes the per-candidate copy the
// policy's dominant cost.
static Combat s_cand_combat;

static void cand_begin(const Combat *c) {
    if (c->heroes[COMBAT_SIDE_PLAYER])
        s_cand_hero = *c->heroes[COMBAT_SIDE_PLAYER];
}

static void cand_setup(const Combat *c) {
    s_cand_combat = *c;
    if (c->heroes[COMBAT_SIDE_PLAYER])
        s_cand_combat.heroes[COMBAT_SIDE_PLAYER] = &s_cand_hero;
}

// Every enemy's probed next-activation output against us on the candidate's
// post-action state -- the threat we would face after taking this action.
static long cand_reply(int side) {
    long t = 0;
    for (int es = 0; es < COMBAT_SLOTS; es++)
        t += probe_enemy_output(&s_cand_combat, side, es);
    return t;
}

// Score the candidate state on ONE HP scale:
//     enemy HP removed - our HP lost - enemy_reply(post-action state).
// pre_them / pre_us are the effective side HPs before the action (computed
// once per activation by the caller).
static long cand_score(int side, long pre_them, long pre_us) {
    int e_side = 1 - side;
    long removed = pre_them - probe_side_hp(&s_cand_combat, e_side);
    long lost    = pre_us   - probe_side_hp(&s_cand_combat, side);
    return removed - lost - cand_reply(side);
}

// ---- Casting policy --------------------------------------------------------
// Probe one cast on a copy; returns true when the engine accepted it and
// fills the HP deltas (their loss, our gain) plus the post-cast state copy in
// s_probe_combat (for two-step probes like teleport-then-attack).
static bool probe_cast(const Combat *c, int side, int spell_idx,
                       int t_side, int t_slot, int dest_x, int dest_y,
                       long *their_hp_lost, long *our_hp_gained) {
    s_probe_combat = *c;
    if (c->heroes[COMBAT_SIDE_PLAYER]) {
        s_probe_hero = *c->heroes[COMBAT_SIDE_PLAYER];
        s_probe_combat.heroes[COMBAT_SIDE_PLAYER] = &s_probe_hero;
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
// engine's spells_this_round), chosen by PROBED VALUE -- every candidate cast
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

    // Consider EVERY combat-kind spell the hero actually holds -- data-driven, no hardcoded
    // spell-id list. The engine's combat_spell_target_filter() classifies each spell's target
    // space; we dispatch the probe + value strategy by that filter, so any combat spell in the
    // catalog is handled. Values stay on one HP scale (see the function comment).
    int spell_n = spells_count();
    for (int si = 0; si < spell_n; si++) {
        const SpellDef *sd = spell_by_index(si);
        if (!sd || sd->kind != SPELL_KIND_COMBAT) continue;
        int idx = sd->index;
        if (idx < 0 || idx >= GAME_SPELLBOOK_SLOTS || cnt[idx] <= 0) continue;   // counts[] is sized GAME_SPELLBOOK_SLOTS (game.h)
        int filter = combat_spell_target_filter(idx);

        if (filter == PICK_FILTER_ENEMY || filter == PICK_FILTER_UNDEAD) {
            // Enemy-targeted. Score by OUTCOME, not by spell id: a cast that removes HP is
            // damage (value = HP destroyed + the output removed by any kills); a cast that
            // deals no HP but applies a disable -- detected via the frozen flag -- is control
            // (value = the denied next-activation output). Turn-undead on a non-undead stack
            // simply yields no effect and scores nothing.
            for (int es = 0; es < COMBAT_SLOTS; es++) {
                const CombatUnit *e = &c->units[e_side][es];
                if (e->troop_idx < 0 || e->count <= 0) continue;
                bool was_frozen = e->frozen;
                long lost = 0;
                if (!probe_cast(c, side, idx, e_side, es, 0, 0, &lost, NULL))
                    continue;                                 // immune / illegal
                if (lost > 0) {
                    long eout = probe_enemy_output(c, side, es);
                    int  post = s_probe_combat.units[e_side][es].count;
                    long out_removed = post <= 0 ? eout
                                       : eout * (e->count - post) / e->count;
                    CONSIDER(idx, e_side, es, 0, 0, lost + out_removed);
                } else if (!was_frozen && s_probe_combat.units[e_side][es].frozen) {
                    long eout = probe_enemy_output(c, side, es);
                    if (eout > 0) CONSIDER(idx, e_side, es, 0, 0, eout);
                }
            }
        } else if (filter == PICK_FILTER_FRIENDLY) {
            // Own-targeted (resurrect / clone): value = our HP restored / added.
            for (int fs = 0; fs < COMBAT_SLOTS; fs++) {
                const CombatUnit *f = &c->units[side][fs];
                if (f->troop_idx < 0 || f->count <= 0) continue;
                long gain = 0;
                if (!probe_cast(c, side, idx, side, fs, 0, 0, NULL, &gain))
                    continue;
                if (gain > 0) CONSIDER(idx, side, fs, 0, 0, gain);
            }
        } else if (filter == PICK_FILTER_ANY_UNIT) {
            // Displacement (teleport), BOTH directions the pick filter allows.
            // (a) STALL: throw an enemy WALKER to the cell farthest from our
            // army. The engine's gait is one cell per activation and an
            // activation spent walking deals nothing, so every step of added
            // distance is one denied activation: value = probed output x
            // added steps (all engine quantities). Shooters and flyers gain
            // nothing from distance and are skipped by the value itself.
            for (int es = 0; es < COMBAT_SLOTS; es++) {
                const CombatUnit *e = &c->units[e_side][es];
                if (e->troop_idx < 0 || e->count <= 0) continue;
                const TroopDef *etd = troop_by_index(e->troop_idx);
                if (etd && (etd->abilities & TROOP_ABIL_FLY)) continue;   // flies back in one hop
                if (etd && etd->ranged_ammo > 0 && e->shots > 0) continue; // range ignores distance
                long eout = probe_enemy_output(c, side, es);
                if (eout <= 0) continue;
                // Nearest friendly (Chebyshev - the engine step metric) before/after.
                long d_now = 0;
                { long best = -1;
                  for (int fs = 0; fs < COMBAT_SLOTS; fs++) {
                      const CombatUnit *f = &c->units[side][fs];
                      if (f->troop_idx < 0 || f->count <= 0) continue;
                      long ddx = f->x - e->x, ddy = f->y - e->y;
                      if (ddx < 0) ddx = -ddx;
                      if (ddy < 0) ddy = -ddy;
                      long d = ddx > ddy ? ddx : ddy;
                      if (best < 0 || d < best) best = d;
                  }
                  d_now = best < 0 ? 0 : best; }
                int bx2 = -1, by2 = -1; long d_far = d_now;
                for (int cy = 0; cy < COMBAT_H; cy++) for (int cx = 0; cx < COMBAT_W; cx++) {
                    if (c->omap[cy][cx] || c->umap[cy][cx]) continue;
                    long best = -1;
                    for (int fs = 0; fs < COMBAT_SLOTS; fs++) {
                        const CombatUnit *f = &c->units[side][fs];
                        if (f->troop_idx < 0 || f->count <= 0) continue;
                        long ddx = f->x - cx, ddy = f->y - cy;
                        if (ddx < 0) ddx = -ddx;
                      if (ddy < 0) ddy = -ddy;
                        long d = ddx > ddy ? ddx : ddy;
                        if (best < 0 || d < best) best = d;
                    }
                    if (best > d_far) { d_far = best; bx2 = cx; by2 = cy; }
                }
                if (bx2 >= 0 && d_far > d_now &&
                    probe_cast(c, side, idx, e_side, es, bx2, by2, NULL, NULL))
                    CONSIDER(idx, e_side, es, bx2, by2, eout * (d_far - d_now));
            }
            // (b) STRIKE: drop our strongest stack (by effective HP) beside the
            // highest-output enemy; value = the melee swing it can then land, probed on the
            // post-teleport state. Fixed cell order mirrors the fly picker.
            int us = -1; long ushp = 0;
            for (int fs = 0; fs < COMBAT_SLOTS; fs++) {
                const CombatUnit *f = &c->units[side][fs];
                if (f->troop_idx < 0 || f->count <= 0) continue;
                const TroopDef *ftd = troop_by_index(f->troop_idx);
                long hp = (long)f->count * (ftd ? ftd->hit_points : 1) - f->injury;
                if (hp > ushp) { ushp = hp; us = fs; }
            }
            int te = -1; long teout = -1;  // -1: an out-of-reach enemy (output 0) is a target
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
                            if (!probe_cast(c, side, idx, side, us, nx, ny, NULL, NULL))
                                continue;
                            // Two-step: probe the melee swing from the post-teleport state.
                            ProbeOut po;
                            probe_attack(&s_probe_combat, side, us, e_side, te,
                                         /*is_ranged=*/false, &po);
                            long val = po.hp_removed - po.our_hp_lost +
                                       (po.eliminated ? teout : 0);
                            CONSIDER(idx, side, us, nx, ny, val);
                            dy = -2; break;           // first legal cell only
                        }
                    }
                }
            }
        }
    }
    #undef CONSIDER

    if (bs < 0) return false;
    if (combat_cast_spell(c, side, bs, bts, btsl, bx, by) != COMBAT_CAST_OK)
        return false;
    return true;
}

int demo_combat_policy(Combat *c, void *ctx) {
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
    // action -- it does NOT consume this unit's move/attack, so the unit still
    // proceeds to its steps below. No-ops when heroes[side]==NULL (digest safety)
    // or no owned spell has a useful legal target. Same policy runs in the
    // predictor and the live fight, so predictions reflect casting.
    try_cast_round(c, side);

    // ACTION SELECTION: ONE unified candidate set on ONE HP scale.
    // Candidates: every legal attack (a ranged shot / melee swing per enemy),
    // a fly to the first free cell beside each enemy, one walked step toward
    // each enemy, and pass. Each is APPLIED on a copy (cand_setup) and scored
    //     enemy_hp_removed - our_hp_lost - enemy_reply(post-action state)
    // where enemy_reply is the sum of every enemy's probed next-activation
    // output on the candidate's post state. The reply term is what a bare
    // exchange value cannot see: an attack is charged for the retaliation it
    // provokes and credited for the output its kills remove; a move is charged
    // for stepping into reach (or credited for pinning a shooter); passing is
    // credited for staying out of reach. Every value is produced by the engine
    // on copies -- no formula, morale rule, or ability is restated here.
    // Deterministic: fixed enumeration order, strict >, first-best wins -- and
    // the order (attacks, fly, move, pass-as-floor) makes ties prefer
    // progress, so the policy cannot dither at range forever.
    int enemy_side = 1 - side;
    bool can_shoot = (u->shots > 0) && !combat_unit_surrounded(c, side, slot);
    // SUSTAIN: does our side have standing output that does not require this
    // stack to engage -- a shooter with ammo left, or magic that actually
    // FIRED this round? While it does, keeping a melee stack out of the enemy
    // gait's reach is winning play (the kite a human rides every hard fight);
    // when it runs dry, distance stops paying and the stack must close and
    // trade. Magic sustains only via the round's cast latch
    // (spells_this_round), set exactly when a strictly-positive probed cast
    // happened this round -- by this activation's try_cast_round above or an
    // earlier stack's. Held charges whose every probe is zero (resurrect with
    // nothing dead, freeze vs all-IMMUNE) are NOT standing output: counting
    // them let a melee stack kite an idle book forever and handed the engine's
    // no-progress cutoff a false LOSS with the enemy untouched.
    bool sustain = false;
    for (int fs = 0; fs < COMBAT_SLOTS && !sustain; fs++) {
        const CombatUnit *f = &c->units[side][fs];
        if (f->troop_idx < 0 || f->count <= 0) continue;
        if (f->shots > 0 && !combat_unit_surrounded(c, side, fs)) sustain = true;
    }
    if (!sustain && c->spells_this_round >= 1) sustain = true;
    long pre_them = probe_side_hp(c, enemy_side);
    long pre_us   = probe_side_hp(c, side);
    cand_begin(c);   // one hero copy per activation; candidates share it

    enum { ACT_NONE, ACT_ATTACK, ACT_FLY, ACT_MOVE, ACT_PASS } best_kind = ACT_NONE;
    int  best_es = -1, best_x = 0, best_y = 0; bool best_ranged = false;
    long best_val = 0;   // meaningful only once best_kind != ACT_NONE

    for (int es = 0; es < COMBAT_SLOTS; es++) {
        const CombatUnit *e = &c->units[enemy_side][es];
        if (e->troop_idx < 0 || e->count <= 0) continue;
        int dx = e->x - u->x, dy = e->y - u->y;
        bool adjacent = dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1;
        for (int r = 0; r < 2; r++) {
            bool ranged = (r == 0);
            if (ranged && !can_shoot) continue;
            if (!ranged && !adjacent) continue;
            cand_setup(c);
            combat_hit_unit(&s_cand_combat, side, slot, enemy_side, es, ranged);
            long val = cand_score(side, pre_them, pre_us);
            if (best_kind == ACT_NONE || val > best_val) {
                best_val = val; best_kind = ACT_ATTACK;
                best_es = es; best_ranged = ranged;
            }
        }
    }
    // FLY: every free cell is a candidate -- beside a shooter (pin), beside a
    // victim (strike next), or away from the melee line (kite). The board is
    // 6x5, so the whole set costs less than one bad landing. The engine's own
    // legality check (bounds/omap/umap) prunes; the score decides.
    if ((t->abilities & TROOP_ABIL_FLY) && u->flights > 0) {
        // Candidate landings: every free cell TOUCHING an enemy (pin a shooter,
        // stage a strike) plus -- only while sustain pays for distance -- the one
        // free cell FARTHEST from all enemies (the kite refuge). The remaining
        // cells are dominated by one of these two classes; scoring the whole
        // board tripled prediction cost for no decision change.
        int safe_x = -1, safe_y = -1; long safe_d = -1;
        for (int cy = 0; cy < COMBAT_H; cy++) for (int cx = 0; cx < COMBAT_W; cx++) {
            if (cx == u->x && cy == u->y) continue;
            if (c->omap[cy][cx] || c->umap[cy][cx]) continue;
            bool touches = false; long dmin = -1;
            for (int es2 = 0; es2 < COMBAT_SLOTS; es2++) {
                const CombatUnit *e2 = &c->units[enemy_side][es2];
                if (e2->troop_idx < 0 || e2->count <= 0) continue;
                long adx = e2->x - cx, ady = e2->y - cy;
                if (adx < 0) adx = -adx;
                if (ady < 0) ady = -ady;
                long d = adx > ady ? adx : ady;
                if (d <= 1) touches = true;
                if (dmin < 0 || d < dmin) dmin = d;
            }
            if ((sustain || can_shoot) && dmin > safe_d) { safe_d = dmin; safe_x = cx; safe_y = cy; }
            if (!touches) continue;
            cand_setup(c);
            if (!combat_fly_unit(&s_cand_combat, side, slot, cx, cy)) continue;
            s_cand_combat.units[side][slot].acted = true;
            long val = cand_score(side, pre_them, pre_us);
            if (best_kind == ACT_NONE || val > best_val) {
                best_val = val; best_kind = ACT_FLY;
                best_x = cx; best_y = cy;
            }
        }
        if (safe_x >= 0) {
            cand_setup(c);
            if (combat_fly_unit(&s_cand_combat, side, slot, safe_x, safe_y)) {
                s_cand_combat.units[side][slot].acted = true;
                long val = cand_score(side, pre_them, pre_us);
                if (best_kind == ACT_NONE || val > best_val) {
                    best_val = val; best_kind = ACT_FLY;
                    best_x = safe_x; best_y = safe_y;
                }
            }
        }
    }
    // WALK: all eight steps, not just the steps TOWARD an enemy. A step away
    // is how a stack stays out of a walker's one-cell gait -- the kite a human
    // plays every fight -- and the reply term prices both directions on the
    // same scale.
    if (u->moves > 0) {
        for (int oy = -1; oy <= 1; oy++) for (int ox = -1; ox <= 1; ox++) {
            if (!ox && !oy) continue;
            // No sustain: steps must CLOSE distance to some enemy, since
            // there is no standing output to buy time with -- see the fly
            // note above.
            if (!sustain && !can_shoot) {
                bool closes = false;
                for (int es2 = 0; es2 < COMBAT_SLOTS && !closes; es2++) {
                    const CombatUnit *e2 = &c->units[enemy_side][es2];
                    if (e2->troop_idx < 0 || e2->count <= 0) continue;
                    long dx0 = e2->x - u->x, dy0 = e2->y - u->y;
                    if (dx0 < 0) dx0 = -dx0;
                    if (dy0 < 0) dy0 = -dy0;
                    long d0 = dx0 > dy0 ? dx0 : dy0;
                    long dx1 = e2->x - (u->x + ox), dy1 = e2->y - (u->y + oy);
                    if (dx1 < 0) dx1 = -dx1;
                    if (dy1 < 0) dy1 = -dy1;
                    long d1 = dx1 > dy1 ? dx1 : dy1;
                    closes = d1 < d0;
                }
                if (!closes) continue;
            }
            cand_setup(c);
            if (combat_move_unit(&s_cand_combat, side, slot, ox, oy) != 1) continue;
            s_cand_combat.units[side][slot].acted = true;
            long val = cand_score(side, pre_them, pre_us);
            if (best_kind == ACT_NONE || val > best_val) {
                best_val = val; best_kind = ACT_MOVE;
                best_x = ox; best_y = oy;
            }
        }
    }

    // Pass -- evaluated LAST with strict >, so on a tie every progressing
    // action (attack/fly/move, in enumeration order) beats standing still:
    // the policy can hold back only when doing so is STRICTLY better, never
    // dithering at range while a shooter whittles it down.
    {
        cand_setup(c);
        s_cand_combat.units[side][slot].acted = true;
        long val = cand_score(side, pre_them, pre_us);
        if (best_kind == ACT_NONE || val > best_val) {
            best_val = val; best_kind = ACT_PASS;
        }
    }

    switch (best_kind) {
    case ACT_ATTACK:
        combat_hit_unit(c, side, slot, enemy_side, best_es, best_ranged);
        u->acted = true;
        break;
    case ACT_FLY:
        // The engine decrements flights and latches acted at zero; an unspent
        // flight re-activates this unit and the policy re-plans from the new
        // cell -- full move_rate usage, the multi-step turn a human takes.
        if (!combat_fly_unit(c, side, slot, best_x, best_y)) u->acted = true;
        break;
    case ACT_MOVE:
        // Same chaining through the engine's moves counter (a step into a
        // hostile cell IS the melee strike and ends the turn, engine rule).
        if (!combat_move_unit(c, side, slot, best_x, best_y)) u->acted = true;
        break;
    default:   // ACT_PASS / ACT_NONE (no enemy alive): stand.
        u->acted = true;
        break;
    }
    return 1;
}

