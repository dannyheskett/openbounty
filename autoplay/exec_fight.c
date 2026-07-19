// autoplay/exec_fight.c
//
// The player-side combat policy (AP-084), the fight resolver, and the win
// predictor (AP-133, AP-183). The SAME policy drives the predictor's
// simulations and the live headless fight, so a predicted result matches the
// live one; combat RNG is a pure function of (seed, encounter identity, mode)
// (REQ-395), so the match is exact.
//
// Policy shape: every candidate action (attack / fly / step / pass, plus at
// most one probed spell cast per activation) is APPLIED on a scratch copy and
// scored on one HP scale:
//     enemy HP removed - our HP lost - enemy_reply(post-action state).
// The SUSTAIN term licenses a melee stack to kite only while our side has
// standing output -- a shooter with ammo, or magic that actually FIRED this
// round (the cast latch, AP-084). Charge possession never sustains.

#include "exec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "flow_resolve.h"
#include "pending.h"
#include "player_io.h"
#include "recording.h"
#include "tables.h"

// ---- scratch ----------------------------------------------------------------

static Combat s_probe_combat;
static Game   s_probe_hero;
static Combat s_cand_combat;
static Game   s_cand_hero;

static long side_hp(const Combat *c, int side) {
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
    long hp_removed;
    long our_hp_lost;
    bool eliminated;
} ProbeOut;

static void probe_attack(const Combat *c, int side, int slot,
                         int t_side, int t_slot, bool is_ranged,
                         ProbeOut *out) {
    memset(out, 0, sizeof *out);
    s_probe_combat = *c;
    Game *hero = c->heroes[COMBAT_SIDE_PLAYER];
    if (hero && hero != &s_probe_hero && hero != &s_cand_hero) {
        s_probe_hero = *hero;
        s_probe_combat.heroes[COMBAT_SIDE_PLAYER] = &s_probe_hero;
    }
    long pre_them = side_hp(&s_probe_combat, t_side);
    long pre_us   = side_hp(&s_probe_combat, side);
    combat_hit_unit(&s_probe_combat, side, slot, t_side, t_slot, is_ranged);
    out->hp_removed  = pre_them - side_hp(&s_probe_combat, t_side);
    out->our_hp_lost = pre_us - side_hp(&s_probe_combat, side);
    out->eliminated  = s_probe_combat.units[t_side][t_slot].count <= 0;
}

// The output enemy stack e can land on its next activation, probed.
static long probe_enemy_output(const Combat *c, int side, int e_slot) {
    int e_side = 1 - side;
    const CombatUnit *e = &c->units[e_side][e_slot];
    if (e->troop_idx < 0 || e->count <= 0 || e->frozen) return 0;
    const TroopDef *etd = troop_by_index(e->troop_idx);
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
        int big = -1;
        long bign = -1;
        for (int s = 0; s < COMBAT_SLOTS; s++) {
            const CombatUnit *u = &c->units[side][s];
            if (u->troop_idx < 0 || u->count <= 0) continue;
            if ((long)u->count > bign) { bign = u->count; big = s; }
        }
        if (big < 0) return 0;
        probe_attack(c, e_side, e_slot, side, big, true, &po);
        return po.hp_removed;
    }
    if (adj >= 0) {
        probe_attack(c, e_side, e_slot, side, adj, false, &po);
        return po.hp_removed;
    }
    return 0;
}

static void cand_begin(const Combat *c) {
    if (c->heroes[COMBAT_SIDE_PLAYER])
        s_cand_hero = *c->heroes[COMBAT_SIDE_PLAYER];
}

static void cand_setup(const Combat *c) {
    s_cand_combat = *c;
    if (c->heroes[COMBAT_SIDE_PLAYER])
        s_cand_combat.heroes[COMBAT_SIDE_PLAYER] = &s_cand_hero;
}

static long cand_reply(int side) {
    long t = 0;
    for (int es = 0; es < COMBAT_SLOTS; es++)
        t += probe_enemy_output(&s_cand_combat, side, es);
    return t;
}

static long cand_score(int side, long pre_them, long pre_us) {
    int e_side = 1 - side;
    long removed = pre_them - side_hp(&s_cand_combat, e_side);
    long lost    = pre_us   - side_hp(&s_cand_combat, side);
    return removed - lost - cand_reply(side);
}

// ---- casting (probed, by outcome; one per activation) -------------------------

static bool probe_cast(const Combat *c, int side, int spell_idx,
                       int t_side, int t_slot, int dest_x, int dest_y,
                       long *their_hp_lost, long *our_hp_gained) {
    s_probe_combat = *c;
    if (c->heroes[COMBAT_SIDE_PLAYER]) {
        s_probe_hero = *c->heroes[COMBAT_SIDE_PLAYER];
        s_probe_combat.heroes[COMBAT_SIDE_PLAYER] = &s_probe_hero;
    }
    int e_side = 1 - side;
    long pre_them = side_hp(&s_probe_combat, e_side);
    long pre_us   = side_hp(&s_probe_combat, side);
    if (combat_cast_spell(&s_probe_combat, side, spell_idx, t_side, t_slot,
                          dest_x, dest_y) != COMBAT_CAST_OK)
        return false;
    if (their_hp_lost)
        *their_hp_lost = pre_them - side_hp(&s_probe_combat, e_side);
    if (our_hp_gained)
        *our_hp_gained = side_hp(&s_probe_combat, side) - pre_us;
    return true;
}

static bool try_cast_round(Combat *c, int side) {
    Game *g = c->heroes[side];
    if (!g) return false;
    if (!g->stats.knows_magic) return false;
    if (c->spells_this_round >= 1) return false;
    const int *cnt = g->spells.counts;
    int e_side = 1 - side;

    long best_val = 0;
    int bs = -1, bts = 0, btsl = 0, bx = 0, by = 0;
#define CONSIDER(IDX, TSIDE, TSLOT, DX, DY, VAL)                              \
    do { if ((VAL) > best_val) { best_val = (VAL); bs = (IDX);                \
         bts = (TSIDE); btsl = (TSLOT); bx = (DX); by = (DY); } } while (0)

    int spell_n = spells_count();
    for (int si = 0; si < spell_n; si++) {
        const SpellDef *sd = spell_by_index(si);
        if (!sd || sd->kind != SPELL_KIND_COMBAT) continue;
        int idx = sd->index;
        if (idx < 0 || idx >= GAME_SPELLBOOK_SLOTS || cnt[idx] <= 0) continue;
        int filter = combat_spell_target_filter(idx);

        if (filter == PICK_FILTER_ENEMY || filter == PICK_FILTER_UNDEAD) {
            for (int es = 0; es < COMBAT_SLOTS; es++) {
                const CombatUnit *e = &c->units[e_side][es];
                if (e->troop_idx < 0 || e->count <= 0) continue;
                bool was_frozen = e->frozen;
                long lost = 0;
                if (!probe_cast(c, side, idx, e_side, es, 0, 0, &lost, NULL))
                    continue;
                if (lost > 0) {
                    long eout = probe_enemy_output(c, side, es);
                    int post = s_probe_combat.units[e_side][es].count;
                    long out_removed = post <= 0 ? eout
                                     : eout * (e->count - post) / e->count;
                    CONSIDER(idx, e_side, es, 0, 0, lost + out_removed);
                } else if (!was_frozen &&
                           s_probe_combat.units[e_side][es].frozen) {
                    long eout = probe_enemy_output(c, side, es);
                    if (eout > 0) CONSIDER(idx, e_side, es, 0, 0, eout);
                }
            }
        } else if (filter == PICK_FILTER_FRIENDLY) {
            for (int fs = 0; fs < COMBAT_SLOTS; fs++) {
                const CombatUnit *f = &c->units[side][fs];
                if (f->troop_idx < 0 || f->count <= 0) continue;
                long gain = 0;
                if (!probe_cast(c, side, idx, side, fs, 0, 0, NULL, &gain))
                    continue;
                if (gain > 0) CONSIDER(idx, side, fs, 0, 0, gain);
            }
        } else if (filter == PICK_FILTER_ANY_UNIT) {
            // Teleport, STRIKE direction: drop our strongest stack beside the
            // highest-output enemy; value = the probed follow-up swing.
            int us = -1;
            long ushp = 0;
            for (int fs = 0; fs < COMBAT_SLOTS; fs++) {
                const CombatUnit *f = &c->units[side][fs];
                if (f->troop_idx < 0 || f->count <= 0) continue;
                const TroopDef *ftd = troop_by_index(f->troop_idx);
                long hp = (long)f->count * (ftd ? ftd->hit_points : 1)
                          - f->injury;
                if (hp > ushp) { ushp = hp; us = fs; }
            }
            int te = -1;
            long teout = -1;
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
                bool adj = fdx >= -1 && fdx <= 1 && fdy >= -1 && fdy <= 1;
                if (!adj) {
                    for (int dy = 1; dy >= -1; dy--) {
                        for (int dx = -1; dx <= 1; dx++) {
                            if (!dx && !dy) continue;
                            int nx = e->x + dx, ny = e->y + dy;
                            if (!combat_in_bounds(nx, ny)) continue;
                            if (c->omap[ny][nx] || c->umap[ny][nx]) continue;
                            if (!probe_cast(c, side, idx, side, us, nx, ny,
                                            NULL, NULL))
                                continue;
                            ProbeOut po;
                            probe_attack(&s_probe_combat, side, us, e_side, te,
                                         false, &po);
                            long val = po.hp_removed - po.our_hp_lost +
                                       (po.eliminated ? teout : 0);
                            CONSIDER(idx, side, us, nx, ny, val);
                            dy = -2;
                            break;
                        }
                    }
                }
            }
        }
    }
#undef CONSIDER

    if (bs < 0) return false;
    return combat_cast_spell(c, side, bs, bts, btsl, bx, by) == COMBAT_CAST_OK;
}

// ---- the policy ---------------------------------------------------------------

int autoplay_combat_policy(Combat *c, void *ctx) {
    (void)ctx;
    if (c->unit_id < 0) return 0;
    int side = c->side;
    int slot = c->unit_id;
    CombatUnit *u = &c->units[side][slot];
    const TroopDef *t = troop_by_index(u->troop_idx);
    if (!t) { u->acted = true; return 1; }
    if (u->frozen) { u->acted = true; return 1; }

    try_cast_round(c, side);

    int enemy_side = 1 - side;
    bool can_shoot = (u->shots > 0) && !combat_unit_surrounded(c, side, slot);
    // SUSTAIN (AP-084): a shooter with ammo, or a strictly-positive cast that
    // actually FIRED this round (the latch). Possession never sustains.
    bool sustain = false;
    for (int fs = 0; fs < COMBAT_SLOTS && !sustain; fs++) {
        const CombatUnit *f = &c->units[side][fs];
        if (f->troop_idx < 0 || f->count <= 0) continue;
        if (f->shots > 0 && !combat_unit_surrounded(c, side, fs)) sustain = true;
    }
    if (!sustain && c->spells_this_round >= 1) sustain = true;

    long pre_them = side_hp(c, enemy_side);
    long pre_us   = side_hp(c, side);
    cand_begin(c);

    enum { ACT_NONE, ACT_ATTACK, ACT_FLY, ACT_MOVE, ACT_PASS } best_kind =
        ACT_NONE;
    int best_es = -1, best_x = 0, best_y = 0;
    bool best_ranged = false;
    long best_val = 0;

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
                best_val = val;
                best_kind = ACT_ATTACK;
                best_es = es;
                best_ranged = ranged;
            }
        }
    }

    if ((t->abilities & TROOP_ABIL_FLY) && u->flights > 0) {
        int safe_x = -1, safe_y = -1;
        long safe_d = -1;
        for (int cy = 0; cy < COMBAT_H; cy++) {
            for (int cx = 0; cx < COMBAT_W; cx++) {
                if (cx == u->x && cy == u->y) continue;
                if (c->omap[cy][cx] || c->umap[cy][cx]) continue;
                bool touches = false;
                long dmin = -1;
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
                if ((sustain || can_shoot) && dmin > safe_d) {
                    safe_d = dmin;
                    safe_x = cx;
                    safe_y = cy;
                }
                if (!touches) continue;
                cand_setup(c);
                if (!combat_fly_unit(&s_cand_combat, side, slot, cx, cy))
                    continue;
                s_cand_combat.units[side][slot].acted = true;
                long val = cand_score(side, pre_them, pre_us);
                if (best_kind == ACT_NONE || val > best_val) {
                    best_val = val;
                    best_kind = ACT_FLY;
                    best_x = cx;
                    best_y = cy;
                }
            }
        }
        if (safe_x >= 0) {
            cand_setup(c);
            if (combat_fly_unit(&s_cand_combat, side, slot, safe_x, safe_y)) {
                s_cand_combat.units[side][slot].acted = true;
                long val = cand_score(side, pre_them, pre_us);
                if (best_kind == ACT_NONE || val > best_val) {
                    best_val = val;
                    best_kind = ACT_FLY;
                    best_x = safe_x;
                    best_y = safe_y;
                }
            }
        }
    }

    if (u->moves > 0) {
        for (int oy = -1; oy <= 1; oy++) {
            for (int ox = -1; ox <= 1; ox++) {
                if (!ox && !oy) continue;
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
                if (combat_move_unit(&s_cand_combat, side, slot, ox, oy) != 1)
                    continue;
                s_cand_combat.units[side][slot].acted = true;
                long val = cand_score(side, pre_them, pre_us);
                if (best_kind == ACT_NONE || val > best_val) {
                    best_val = val;
                    best_kind = ACT_MOVE;
                    best_x = ox;
                    best_y = oy;
                }
            }
        }
    }

    {
        cand_setup(c);
        s_cand_combat.units[side][slot].acted = true;
        long val = cand_score(side, pre_them, pre_us);
        if (best_kind == ACT_NONE || val > best_val) {
            best_val = val;
            best_kind = ACT_PASS;
        }
    }

    switch (best_kind) {
    case ACT_ATTACK:
        combat_hit_unit(c, side, slot, enemy_side, best_es, best_ranged);
        u->acted = true;
        break;
    case ACT_FLY:
        if (!combat_fly_unit(c, side, slot, best_x, best_y)) u->acted = true;
        break;
    case ACT_MOVE:
        if (!combat_move_unit(c, side, slot, best_x, best_y)) u->acted = true;
        break;
    default:
        u->acted = true;
        break;
    }
    return 1;
}

// ---- fight resolution (live) ---------------------------------------------------

// Build the CombatTarget from the live pending flow, exactly as the shell does.
static bool build_pending_target(Game *g, CombatMode *out_mode,
                                 CombatTarget *out_tgt) {
    memset(out_tgt, 0, sizeof *out_tgt);
    if (pending_flow == FLOW_SIEGE_MONSTER ||
        pending_flow == FLOW_SIEGE_VILLAIN) {
        *out_mode = COMBAT_MODE_CASTLE;
        CastleRecord *cr = GameFindCastle(g, pending_castle_id);
        const ResCastle *rc = resources_castle_by_id(g->res, pending_castle_id);
        const VillainDef *v = (cr && cr->villain_id[0])
                                  ? villain_by_id(cr->villain_id) : NULL;
        out_tgt->name = v && v->name[0] ? v->name
                       : (rc && rc->name[0] ? rc->name : pending_castle_id);
        out_tgt->seed_key = pending_castle_id;
        if (cr) {
            out_tgt->garrison = cr->garrison;
            out_tgt->garrison_slots = GAME_ARMY_SLOTS;
        }
        return true;
    }
    if (pending_flow == FLOW_ATTACK_FOE) {
        *out_mode = COMBAT_MODE_FOE;
        FoeState *foe = pending_foe_id[0] ? GameFindFoe(g, pending_foe_id)
                                          : NULL;
        out_tgt->name = "Hostile band";
        out_tgt->seed_key = pending_foe_id;
        if (foe) {
            out_tgt->garrison = foe->garrison;
            out_tgt->garrison_slots = GAME_ARMY_SLOTS;
        }
        return true;
    }
    return false;
}

// The temp-death transition is engine-owned (GameTempDeath -- the same call
// src/shell_tempdeath.c wraps), so headless and visible defeats produce the
// identical world. The planner only enters fights the simulation wins, so a
// live loss is a determinism defect -- but the handler keeps a diverged world
// legal.
void exec_temp_death(ExecCtx *ctx) {
    GameTempDeath(ctx->g, ctx->map, ctx->fog, ctx->res);
}

bool exec_fight(ExecCtx *ctx, bool want_fight, CombatResult *out_result) {
    Game *g = ctx->g;
    PendingFlow flow = pending_flow;
    if (flow != FLOW_SIEGE_MONSTER && flow != FLOW_SIEGE_VILLAIN &&
        flow != FLOW_ATTACK_FOE)
        return false;

    if (!want_fight) {
        FlowAnswer no = { FLOW_ANS_NO, 0 };
        rec_push_answer(g, flow, no, PLAYER_IO_COMBAT_NOT_RUN);
        PlayerIoPresentation pres;
        player_io_answer(g, ctx->map, ctx->fog, ctx->res, no,
                         PLAYER_IO_COMBAT_NOT_RUN, &pres);
        if (out_result) *out_result = COMBAT_RESULT_LOSS;
        return true;
    }

    CombatMode mode;
    CombatTarget tgt;
    if (!build_pending_target(g, &mode, &tgt)) return false;

    // Record the YES answer with the PRE-combat fingerprint (replay checks it
    // before re-running the combat), then run combat and patch the outcome onto
    // the prim once it is known.
    FlowAnswer yes = { FLOW_ANS_YES, 0 };
    RecPrim *ap = rec_push_answer(g, flow, yes, PLAYER_IO_COMBAT_NOT_RUN);
    CombatResult r = combat_run_headless_ex(g, mode, &tgt, COMBAT_MAX_ROUNDS,
                                            autoplay_combat_policy, NULL);
    PlayerIoCombatOutcome oc = (r == COMBAT_RESULT_WIN)
                                   ? PLAYER_IO_COMBAT_WON
                                   : PLAYER_IO_COMBAT_LOST;
    if (ap) ap->outcome = (uint8_t)oc;
    PlayerIoPresentation pres;
    player_io_answer(g, ctx->map, ctx->fog, ctx->res, yes, oc, &pres);
    if (pres.temp_death) exec_temp_death(ctx);
    exec_pump_passive(ctx);
    if (out_result) *out_result = r;
    return true;
}

// ---- prediction (AP-133, AP-183) ------------------------------------------------

// Grow a garrison copy through `weeks` of the engine's own astrology schedule.
static void grow_garrison(const Game *g, Unit *gar, int weeks) {
    if (weeks <= 0) return;
    int week_now = GameWeekId(g);
    for (int w = 1; w <= weeks; w++) {
        int creature = GamePickAstrologyCreature(g, week_now + w);
        const TroopDef *astro = troop_by_index(creature);
        if (!astro || !astro->id[0] || astro->growth_per_week <= 0) continue;
        for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
            if (gar[s].count <= 0) continue;
            if (strcmp(gar[s].id, astro->id) != 0) continue;
            gar[s].count += astro->growth_per_week;
        }
    }
}

// Memo table (AP-133): keyed by the full sim-input signature.
#define PREDICT_CACHE_N 8192
static struct {
    uint32_t key;
    uint8_t  used, win;
    long     ai_hp;
} s_cache[PREDICT_CACHE_N];

static uint32_t mix(uint32_t h, const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}

static Game s_sim_game;   // throwaway world copy for simulations

bool predict_combat_cached(const ExecCtx *ctx, CombatMode mode,
                           const char *seed_key, const char *display_name,
                           const Unit *garrison,
                           const ArmyStack *army_override,
                           int leadership_what_if,
                           const int *book_what_if,
                           int grow_weeks,
                           long *out_ai_hp) {
    const Game *g = ctx->g;

    uint32_t key = 2166136261u;
    key = mix(key, &mode, sizeof mode);
    if (seed_key) key = mix(key, seed_key, strlen(seed_key));
    for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
        key = mix(key, garrison[s].id, strlen(garrison[s].id));
        key = mix(key, &garrison[s].count, sizeof garrison[s].count);
    }
    const ArmyStack *army = army_override ? army_override : g->army;
    for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
        key = mix(key, army[s].id, strlen(army[s].id));
        key = mix(key, &army[s].count, sizeof army[s].count);
    }
    int lead = leadership_what_if >= 0 ? leadership_what_if
                                       : g->stats.leadership_current;
    key = mix(key, &lead, sizeof lead);
    const int *book = book_what_if ? book_what_if : g->spells.counts;
    key = mix(key, book, sizeof(int) * GAME_SPELLBOOK_SLOTS);
    key = mix(key, &grow_weeks, sizeof grow_weeks);
    uint32_t seed_mix = (uint32_t)(g->seed & 0xFFFFFFFFu);
    key = mix(key, &seed_mix, sizeof seed_mix);
    key = mix(key, &g->stats.knows_magic, sizeof g->stats.knows_magic);
    key = mix(key, &g->stats.spell_power, sizeof g->stats.spell_power);

    int slot_i = (int)(key % PREDICT_CACHE_N);
    if (s_cache[slot_i].used && s_cache[slot_i].key == key) {
        if (out_ai_hp) *out_ai_hp = s_cache[slot_i].ai_hp;
        return s_cache[slot_i].win != 0;
    }

    // Build the throwaway world (AP-133): live copy + what-ifs, fought under
    // RNG snapshot/restore so plan-time replays never perturb the live RNG.
    uint64_t rng = GameRngSnapshot();
    s_sim_game = *g;
    if (army_override)
        memcpy(s_sim_game.army, army_override,
               sizeof(ArmyStack) * GAME_ARMY_SLOTS);
    if (leadership_what_if >= 0)
        s_sim_game.stats.leadership_current = leadership_what_if;
    if (book_what_if)
        memcpy(s_sim_game.spells.counts, book_what_if,
               sizeof(int) * GAME_SPELLBOOK_SLOTS);

    Unit gar[GAME_ARMY_SLOTS];
    memcpy(gar, garrison, sizeof gar);
    grow_garrison(g, gar, grow_weeks);

    CombatTarget tgt = { 0 };
    tgt.name = display_name ? display_name : (seed_key ? seed_key : "target");
    tgt.seed_key = seed_key;
    tgt.garrison = gar;
    tgt.garrison_slots = GAME_ARMY_SLOTS;

    CombatTurnRecord rec = { 0 };
    CombatResult r = combat_run_headless_rec(&s_sim_game, mode, &tgt, COMBAT_MAX_ROUNDS,
                                             autoplay_combat_policy, NULL,
                                             &rec);
    GameRngRestore(rng);

    s_cache[slot_i].key = key;
    s_cache[slot_i].used = 1;
    s_cache[slot_i].win = (r == COMBAT_RESULT_WIN) ? 1 : 0;
    s_cache[slot_i].ai_hp = rec.ai_hp;
    if (out_ai_hp) *out_ai_hp = rec.ai_hp;
    if (ob_diag_verbose())
        printf("[RECRUIT-SIM] target=%s mode=%d lead=%d grow=%d -> %s ai_hp=%ld\n",
               seed_key ? seed_key : "?", (int)mode, lead, grow_weeks,
               r == COMBAT_RESULT_WIN ? "WIN" : "LOSS", rec.ai_hp);
    return r == COMBAT_RESULT_WIN;
}

bool exec_fight_winnable(ExecCtx *ctx, CombatMode mode, const char *seed_key,
                         const char *display_name, const Unit *garrison,
                         int grow_weeks, long *out_ai_hp) {
    return predict_combat_cached(ctx, mode, seed_key, display_name, garrison,
                                 NULL, -1, NULL, grow_weeks, out_ai_hp);
}
