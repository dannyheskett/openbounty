// autoplay/planner.c
//
// THE PLANNER (AP-050..AP-058): a re-entrant STEP CORE, not a loop.
// planner_open enumerates the universe and initializes run state,
// planner_candidates runs one cycle head (logistics, mover pricing, ordering,
// the promotion tiers), planner_step performs ONE atomic attempt under a world
// snapshot, planner_step_sacrifice is the deliberate-defeat escape,
// planner_refresh_done re-reads the done predicates, planner_report prints the
// truthful end-of-line causes. The snapshot-tree search (search.c) drives
// these one decision at a time; nothing here plays a whole game. All planner
// memory for one line of play rides in the caller's PlannerRun rather than in
// statics, so a search node can carry it.

#include "planner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "exec.h"
#include "exec_ledger.h"
#include "goals.h"
#include "pending.h"
#include "prereq.h"
#include "primitives.h"
#include "recording.h"
#include "spells_adventure.h"
#include "worldsnap.h"

// Keystone arm (AP-055). The keystone promotes the first in-zone open
// objective of this kind to the front while none of that kind is done yet.
static const int s_keystone_kind = STEP_VILLAIN;

// Suppress the end-of-line [UNMET]/[VERDICT READY]/ledger report. The search
// sets this while expanding -- a node's report is read for its counts only --
// and clears it for the one committed node.
static bool s_quiet = false;
void planner_set_quiet(bool on) { s_quiet = on; }

// Suppress ONLY the [VERDICT READY] line (keeping [UNMET]/ledger).
static bool s_print_verdict = true;
void planner_set_print_verdict(bool on) { s_print_verdict = on; }

static WorldSnapshot *s_snap;   // heap: Game+Map+Fog is ~1MB (attempt atomics)
static PlanStepSet s_set;       // the CURRENT enumeration (planner_open)

// ---- identity-keyed failure history (planner.h ObjHist) ---------------------
// A planner_open on a progressed world re-enumerates a SHRUNK universe, so
// enumeration indices shift between opens; ever_failed/stuck_cycles follow
// the objective by identity hash instead.
static uint32_t obj_key(const PlanStep *s) {
    uint32_t h = 2166136261u;
    const unsigned char *p;
    int v[4] = { (int)s->kind, s->zone_index, s->x, s->y };
    p = (const unsigned char *)v;
    for (size_t i = 0; i < sizeof v; i++) { h ^= p[i]; h *= 16777619u; }
    for (const char *c = s->handle; *c; c++) { h ^= (unsigned char)*c; h *= 16777619u; }
    return h ? h : 1;   // 0 is the empty-slot sentinel
}

static ObjHist *hist_slot(PlannerRun *run, uint32_t key) {
    size_t cap = sizeof run->hist / sizeof run->hist[0];
    size_t i = key % cap;
    for (size_t probes = 0; probes < cap; probes++) {
        if (run->hist[i].key == 0 || run->hist[i].key == key) {
            run->hist[i].key = key;
            return &run->hist[i];
        }
        i = (i + 1) % cap;
    }
    return NULL;   // full (cannot happen: 2x slots per objective)
}

static void hist_store(PlannerRun *run, const PlanStep *s, const ObjState *st) {
    ObjHist *h = hist_slot(run, obj_key(s));
    if (!h) return;
    h->ever_failed = st->ever_failed ? 1 : 0;
    h->stuck_cycles = (uint8_t)(st->stuck_cycles > 255 ? 255
                                                       : st->stuck_cycles);
    if (st->defer_cause != EXEC_CAUSE_NONE || st->defer_why[0]) {
        h->defer_cause = (uint8_t)st->defer_cause;
        snprintf(h->defer_why, sizeof h->defer_why, "%.63s",
                 st->defer_why);
    }
}

static void hist_load(PlannerRun *run, const PlanStep *s, ObjState *st) {
    ObjHist *h = hist_slot(run, obj_key(s));
    if (!h) return;
    st->ever_failed = h->ever_failed != 0;
    st->stuck_cycles = h->stuck_cycles;
    st->defer_cause = (ExecCause)h->defer_cause;
    snprintf(st->defer_why, sizeof st->defer_why, "%s", h->defer_why);
}

// Spell logistics (AP-055): with magic learned and a solvent wallet, keep the
// transport book stocked -- gate charges make crossings and long legs zero-day
// teleports (AP-092), and stop charges convert gold into free steps (the
// engine's own gold->calendar exchange). Runs once at the start of each
// cycle head. Each stocking trip is atomic like any attempt.
static void planner_logistics(ExecCtx *ctx) {
    if (worldsnap_calendar_dead()) return;   // calendar death is terminal
    WorldSnapshot *snap = s_snap;
    // Solvency floor: the pack's own price of entering the spell economy
    // (the alcove fee) -- below it the wallet belongs to the army.
    if (!ctx->g->stats.knows_magic ||
        ctx->g->stats.gold <= ctx->res->economy.alcove_cost)
        return;
    BookBudget budget;
    exec_book_budget(ctx->g, &budget);
    // Book allocation (AP-112): surplus raise charges beyond a small reserve
    // are cast off (the temporary lift is harmless), and combat charges the
    // plans are not carrying are field-discarded (the engine's own flow), so
    // the shared capacity can hold the transport book.
    {
        int ri = spell_index_by_adventure_effect(ADV_EFFECT_RAISE_CONTROL);
        while (ri >= 0 && ctx->g->spells.counts[ri] > 2 &&
               ctx->g->stats.max_spells - GameKnownSpells(ctx->g) < 4) {
            if (!exec_cast_raise(ctx)) break;
        }
        int guard2 = GAME_SPELLBOOK_SLOTS * 4;
        while (ctx->g->stats.max_spells - GameKnownSpells(ctx->g) < 4 &&
               guard2-- > 0) {
            // cheapest combat spell first; kits re-buy on demand
            int cheapest = -1, cheapest_cost = 0;
            for (int si2 = 0; si2 < spells_count(); si2++) {
                const SpellDef *sd2 = spell_by_index(si2);
                if (!sd2 || sd2->kind != SPELL_KIND_COMBAT) continue;
                if (ctx->g->spells.counts[sd2->index] <= 0) continue;
                if (cheapest < 0 || sd2->cost < cheapest_cost) {
                    cheapest = sd2->index;
                    cheapest_cost = sd2->cost;
                }
            }
            if (cheapest >= 0 && exec_discard_spell(ctx, cheapest)) continue;
            if (exec_discard_junk_spell(ctx, -1)) continue;
            break;
        }
    }
    for (int town2 = 0; town2 < 2; town2++) {
        // Book split (exec_book_budget): castle gates at the law floor
        // always -- in bulk once the book widens (every held charge above
        // the floor is one free crossing, the endgame's dominant day sink);
        // town gates only when the book affords both transports beside the
        // stop charges.
        if (town2 == 1 && !budget.town_participates) continue;
        int gi2 = gate_spell_index(town2 == 1);
        if (gi2 < 0) continue;
        // The book caps TOTAL charges (GameKnownSpells sums counts), so the
        // two transports share it: the stop stock takes what remains -- the
        // room-forcing below clears the junk charges so both fit.
        int want2 = (town2 == 0) ? budget.castle_gate_want
                                 : GATE_LAW_MIN_CHARGES;
        if (spell_charges(ctx->g, gi2) >= want2) continue;
        if (ctx->g->stats.max_spells - GameKnownSpells(ctx->g) < 2)
            continue;
        int mark = recsink_mark();
        worldsnap_capture(snap, ctx->g, ctx->map, ctx->fog);
        if (!exec_stock_gate_charges(ctx, town2 == 1, want2)) {
            worldsnap_restore(snap, ctx->g, ctx->map, ctx->fog);
            recsink_rollback(mark);
        }
    }
    int ts = spell_index_by_adventure_effect(ADV_EFFECT_TIME_STOP);
    int room = ctx->g->stats.max_spells - GameKnownSpells(ctx->g);
    if (ob_diag_verbose()) {
        printf("[PLANNER] logistics: gold=%d max_spells=%d known=%d "
               "room=%d ts_charges=%d\n", ctx->g->stats.gold,
               ctx->g->stats.max_spells, GameKnownSpells(ctx->g),
               room, ts >= 0 ? ctx->g->spells.counts[ts] : -1);
        if (room <= 2) {
            printf("[PLANNER] book:");
            for (int si4 = 0; si4 < spells_count(); si4++) {
                const SpellDef *sd4 = spell_by_index(si4);
                if (!sd4 || ctx->g->spells.counts[sd4->index] <= 0) continue;
                printf(" %s×%d", sd4->id, ctx->g->spells.counts[sd4->index]);
            }
            printf("\n");
        }
    }
    if (ts >= 0 && ctx->g->spells.counts[ts] == 0) {
        int mark = recsink_mark();
        worldsnap_capture(snap, ctx->g, ctx->map, ctx->fog);
        // Force book room for the stop stock: cast off leftover raise and
        // instant-army charges (their effects are harmless here) and discard
        // surplus combat charges, keeping the castle-gate law floor
        // untouched. Every slot freed here is a stop charge -- sp*10 free
        // steps -- so the forcing is as deep as the book allows.
        int want_room = ctx->g->stats.max_spells - budget.floor_all;
        if (want_room > budget.stop_clamp) want_room = budget.stop_clamp;
        int guard3 = GAME_SPELLBOOK_SLOTS * 4;
        while (ctx->g->stats.max_spells - GameKnownSpells(ctx->g) < want_room &&
               guard3-- > 0) {
            if (exec_cast_raise(ctx)) continue;
            int ia3 = spell_index_by_adventure_effect(
                ADV_EFFECT_INSTANT_ARMY);
            const SpellDef *sd3 = ia3 >= 0 ? spell_by_index(ia3) : NULL;
            if (sd3 && ctx->g->spells.counts[ia3] > 0) {
                int cast_mark = recsink_mark();
                rec_push_action(ctx->g, RA_CAST_ADV_SPELL, sd3->id, 0, 0);
                if (GameApplyAdventureSpellEffect(ctx->g, ia3)) {
                    exec_pump_passive(ctx);
                    continue;
                }
                recsink_rollback(cast_mark);
            }
            int cheapest3 = -1, cheapest3_cost = 0;
            for (int si3 = 0; si3 < spells_count(); si3++) {
                const SpellDef *sd3b = spell_by_index(si3);
                if (!sd3b || sd3b->kind != SPELL_KIND_COMBAT) continue;
                if (ctx->g->spells.counts[sd3b->index] <= 0) continue;
                if (cheapest3 < 0 || sd3b->cost < cheapest3_cost) {
                    cheapest3 = sd3b->index;
                    cheapest3_cost = sd3b->cost;
                }
            }
            if (cheapest3 >= 0 && exec_discard_spell(ctx, cheapest3))
                continue;
            if (exec_discard_junk_spell(ctx, ts)) continue;
            break;
        }
        int room2 = ctx->g->stats.max_spells - GameKnownSpells(ctx->g);
        int want = room2;
        if (want > budget.stop_clamp) want = budget.stop_clamp;
        if (want < 1 ||
            !exec_stock_spell_charges_public(ctx, ts, want)) {
            worldsnap_restore(snap, ctx->g, ctx->map, ctx->fog);
            recsink_rollback(mark);
        }
    }
}

// One atomic attempt (AP-030, AP-051): stranding pre-gate, gate-kit re-stock,
// unmet hard gates in try-order, then the objective through execute_why.
// Success admits (unless stranded); PREFOUGHT keeps the world without
// admitting; every other failure rolls back whole.
static bool planner_attempt(ExecCtx *ctx, const PlanStep *step, ObjState *st) {
    if (exec_step_strands(ctx, step)) {
        st->defer_cause = EXEC_CAUSE_STRANDED;
        snprintf(st->defer_why, sizeof st->defer_why, "%s:sealed", step->label);
        return false;
    }
    int mark = recsink_mark();
    worldsnap_capture(s_snap, ctx->g, ctx->map, ctx->fog);

    // Gate-kit re-stock (AP-051): held gate kits top up to the law floor.
    // The top-up must not leave the transport book POORER than it began --
    // roll back only that harm; a partial top-up that kept or grew both
    // books stands.
    {
        int ci0 = gate_spell_index(false), ti0 = gate_spell_index(true);
        int cb = ci0 >= 0 ? spell_charges(ctx->g, ci0) : 0;
        int tb = ti0 >= 0 ? spell_charges(ctx->g, ti0) : 0;
        exec_topup_gate_kit(ctx);
        int ca = ci0 >= 0 ? spell_charges(ctx->g, ci0) : 0;
        int ta = ti0 >= 0 ? spell_charges(ctx->g, ti0) : 0;
        if (ca < cb || ta < tb) {
            worldsnap_restore(s_snap, ctx->g, ctx->map, ctx->fog);
            recsink_rollback(mark);
            worldsnap_capture(s_snap, ctx->g, ctx->map, ctx->fog);
        }
    }

    // Hard gates (AP-060) under the same snapshot.
    PlanStep gates[2];
    int gate_n = prereq_unmet(ctx, step, gates, 2);
    for (int i = 0; i < gate_n; i++) {
        ExecCause gc = EXEC_CAUSE_NONE;
        char gw[96];
        if (!execute_why(ctx, &gates[i], gw, sizeof gw, &gc)) {
            st->defer_cause = gc;
            snprintf(st->defer_why, sizeof st->defer_why, "%s", gw);
            worldsnap_restore(s_snap, ctx->g, ctx->map, ctx->fog);
            recsink_rollback(mark);
            return false;
        }
    }

    ExecCause cause = EXEC_CAUSE_NONE;
    char why[96];
    bool ok = execute_why(ctx, step, why, sizeof why, &cause);
    bool done = planstep_is_done(ctx->g, step);
    if (ob_diag_verbose())
        printf("[PLANNER] attempt %-32s ok=%d done=%d cause=%s why=%s "
               "day=%d gold=%d\n",
               step->label, (int)ok, (int)done, exec_cause_name(cause), why,
               ctx->g->stats.days_left, ctx->g->stats.gold);

    if (ok && done && cause != EXEC_CAUSE_STRANDED) {
        // The stranded rule (AP-051): an attempt that accomplished its goal
        // admits UNLESS the hero ended marooned -- a success that seals the
        // hero in rolls back like a failure.
        int ax[GAME_MAX_FOES], ay[GAME_MAX_FOES], an = 0;
        char an_id[GAME_MAX_FOES][32];
        for (int i = 0; i < ctx->g->foe_count && an < GAME_MAX_FOES; i++) {
            const FoeState *f = &ctx->g->foes[i];
            if (!f->alive || f->friendly) continue;
            if (strcmp(f->zone, ctx->g->position.zone) != 0) continue;
            int ddx = f->x - ctx->g->position.x;
            int ddy = f->y - ctx->g->position.y;
            if (ddx < 0) ddx = -ddx;
            if (ddy < 0) ddy = -ddy;
            int d = ddx > ddy ? ddx : ddy;
            if (d > AUTOPLAY_PURSUIT_RADIUS) continue;   // beyond pursuit relevance
            long hp = 0;
            if (!exec_fight_winnable(ctx, COMBAT_MODE_FOE, f->placement_id,
                                     "Hostile band", f->garrison, 0, &hp)) {
                ax[an] = f->x;
                ay[an] = f->y;
                snprintf(an_id[an], sizeof an_id[an], "%s", f->placement_id);
                an++;
            }
        }
        int reach_nodes = move_reachable_nodes_avoid(ctx, ax, ay, an);
        if (ob_diag_verbose() && reach_nodes < AUTOPLAY_POCKET_NODES) {
            printf("[PLANNER] maroon probe: hero=(%d,%d,%s) mode=%d reach=%d "
                   "an=%d\n", ctx->g->position.x, ctx->g->position.y,
                   ctx->g->position.zone, (int)ctx->g->travel_mode,
                   reach_nodes, an);
            for (int dy2 = -1; dy2 <= 1; dy2++)
                for (int dx2 = -1; dx2 <= 1; dx2++) {
                    const Tile *t2 = MapGetTile(ctx->map,
                                                ctx->g->position.x + dx2,
                                                ctx->g->position.y + dy2);
                    if (t2)
                        printf("[PLANNER]  n(%+d,%+d) art=%s terr=%d int=%d "
                               "blocks=%d\n", dx2, dy2, t2->art,
                               (int)t2->terrain, (int)t2->interactive,
                               (int)t2->blocks_foot);
                }
        }
        if (step->kind != STEP_SCEPTER && reach_nodes < AUTOPLAY_POCKET_NODES) {
            // Second chance 1: fight the jaw open from INSIDE -- the sealer
            // is an objective, and the pocket often holds its own funding.
            bool fought_open = false;
            ExecCause jaw_cause = EXEC_CAUSE_NONE;
            for (int a2 = 0; a2 < an && !fought_open; a2++) {
                for (int b2 = 0; b2 < s_set.count; b2++) {
                    const PlanStep *bs = &s_set.steps[b2];
                    if (bs->kind != STEP_FOE) continue;
                    if (bs->zone_index != hero_zone_index(ctx)) continue;
                    if (strcmp(bs->handle, an_id[a2]) != 0) continue;
                    ExecCause bc = EXEC_CAUSE_NONE;
                    char bw[96];
                    if (execute_why(ctx, bs, bw, sizeof bw, &bc) &&
                        planstep_is_done(ctx->g, bs))
                        fought_open = true;
                    else if (jaw_cause == EXEC_CAUSE_NONE)
                        jaw_cause = bc;
                    break;
                }
            }
            if (fought_open &&
                move_reachable_nodes_avoid(ctx, NULL, NULL, 0) >=
                    AUTOPLAY_POCKET_NODES) {
                // The jaw fell; both the objective and the sealer commit.
            } else
            // Second chance 2: walk out of the jaw before judging. Only a
            // failed escape is a maroon.
            if (!move_escape_jaw(ctx, ax, ay, an)) {
                // The truthful cause: when the jaw fight was money-bound the
                // objective is GOLD-deferred (a richer wallet re-opens it);
                // otherwise it is marooned behind the sealer.
                st->defer_cause = (jaw_cause == EXEC_CAUSE_GOLD ||
                                   jaw_cause == EXEC_CAUSE_STOCK)
                                      ? jaw_cause
                                      : EXEC_CAUSE_STRANDED;
                snprintf(st->defer_why, sizeof st->defer_why, "%s:marooned",
                         step->label);
                if (ob_diag_verbose()) {
                    const char *blocker = "-";
                    long bd = 1 << 20;
                    for (int a2 = 0; a2 < an; a2++) {
                        long dx2 = ax[a2] - ctx->g->position.x;
                        long dy2 = ay[a2] - ctx->g->position.y;
                        if (dx2 < 0) dx2 = -dx2;
                        if (dy2 < 0) dy2 = -dy2;
                        long d2 = dx2 > dy2 ? dx2 : dy2;
                        if (d2 < bd) {
                            bd = d2;
                            blocker = an_id[a2];
                        }
                    }
                    printf("[PLANNER] %s succeeded but marooned the hero; "
                           "rolled back (blocker=%s z%d jaw=%s)\n",
                           step->label, blocker, hero_zone_index(ctx),
                           exec_cause_name(jaw_cause));
                }
                worldsnap_restore(s_snap, ctx->g, ctx->map, ctx->fog);
                recsink_rollback(mark);
                return false;
            }
        }
        st->done = true;
        st->defer_why[0] = '\0';
        st->defer_cause = EXEC_CAUSE_NONE;
        return true;
    }
    st->defer_cause = cause;
    snprintf(st->defer_why, sizeof st->defer_why, "%s", why);
    st->ever_failed = true;
    if (cause == EXEC_CAUSE_PREFOUGHT) {
        // Keep the world without admitting (AP-051): the contract-less win
        // reset the lord's garrison -- real progress a restore would erase.
        return false;
    }
    worldsnap_restore(s_snap, ctx->g, ctx->map, ctx->fog);
    recsink_rollback(mark);
    return false;
}

// The scarce-winner conflict tier (AP-054): before a non-held-win siege best
// is attempted, probe each other open siege candidate with the two pure
// queries; a fragile winner is attempted first. One swap per cycle.
static int scarce_winner_swap(ExecCtx *ctx, const PlanStepSet *set,
                              const ObjState *st, int *order, int order_n) {
    if (order_n < 2) return -1;
    const PlanStep *best = &set->steps[order[0]];
    if (best->kind != STEP_MONSTER_CASTLE && best->kind != STEP_VILLAIN)
        return -1;

    // Held-win best needs no shopping -- no conflict possible.
    const CastleRecord *cr = NULL;
    if (best->kind == STEP_VILLAIN) {
        for (int i = 0; i < GAME_CASTLES; i++)
            if (ctx->g->castles[i].owner_kind == CASTLE_OWNER_VILLAIN &&
                strcmp(ctx->g->castles[i].villain_id, best->handle) == 0)
                cr = &ctx->g->castles[i];
    } else {
        cr = GameFindCastleConst(ctx->g, best->handle);
    }
    if (!cr) return -1;
    long hp;
    if (exec_fight_winnable(ctx, COMBAT_MODE_CASTLE, cr->id, best->label,
                            cr->garrison, 0, &hp))
        return -1;

    RecruitRequest best_req = { RECRUIT_FOR_WIN, COMBAT_MODE_CASTLE, cr->id,
                                best->label, cr->garrison, 0 };
    static int best_draw[CAT_TROOPS_MAX];
    if (!recruit_winner_finite_draw(ctx, &best_req, best_draw)) return -1;

    for (int oi = 1; oi < order_n; oi++) {
        const PlanStep *other = &set->steps[order[oi]];
        if (other->kind != STEP_MONSTER_CASTLE &&
            other->kind != STEP_VILLAIN)
            continue;
        if (st[order[oi]].done) continue;
        const CastleRecord *ocr = NULL;
        if (other->kind == STEP_VILLAIN) {
            for (int i = 0; i < GAME_CASTLES; i++)
                if (ctx->g->castles[i].owner_kind == CASTLE_OWNER_VILLAIN &&
                    strcmp(ctx->g->castles[i].villain_id, other->handle) == 0)
                    ocr = &ctx->g->castles[i];
        } else {
            ocr = GameFindCastleConst(ctx->g, other->handle);
        }
        if (!ocr) continue;
        RecruitRequest other_req = { RECRUIT_FOR_WIN, COMBAT_MODE_CASTLE,
                                     ocr->id, other->label, ocr->garrison, 0 };
        static int other_draw[CAT_TROOPS_MAX];
        if (!recruit_winner_finite_draw(ctx, &other_req, other_draw))
            continue;   // no live winner -- nothing to protect
        // The other's winner dies under the best's draw...
        if (recruit_winner_survives_less(ctx, &other_req, best_draw, false))
            continue;
        // ...while the best's own winner survives the reverse draw (live, or
        // recoverable at the restock ceiling).
        if (!recruit_winner_survives_less(ctx, &best_req, other_draw, false) &&
            !recruit_winner_survives_less(ctx, &best_req, other_draw, true))
            continue;
        if (ob_diag_verbose())
            printf("[PLANNER] scarce-winner swap: %s ahead of %s\n",
                   other->label, best->label);
        return oi;
    }
    return -1;
}

// ---- the step core (planner.h) ---------------------------------------------

bool planner_open(ExecCtx *ctx, PlannerRun *run) {
    static Map scratch;   // enumeration-time zone loads
    if (!s_snap) {
        s_snap = (WorldSnapshot *)malloc(sizeof *s_snap);
        if (!s_snap) return false;
    }
    if (!plansteps_enumerate(ctx->g, &scratch, &s_set)) return false;
    // NOTE: no live-map reload here. Enumeration uses its own scratch map,
    // so ctx->map is untouched -- and a mid-game fresh stamp is NOT
    // byte-equal to the map the play derived (foes stamped at moved
    // positions, cleared-tile residue), which breaks recording replay for
    // any caller that opens on a progressed world (the snapshot-tree
    // search). The caller owns the live map's fidelity.

    // Fresh per-enumeration state: done from the world (the predicate is the
    // only truth), failure history by identity from run->hist (empty on a
    // fresh run; carried along a search line).
    memset(run->st, 0, sizeof run->st);
    for (int i = 0; i < s_set.count; i++) {
        run->st[i].done = planstep_is_done(ctx->g, &s_set.steps[i]);
        if (!run->st[i].done) hist_load(run, &s_set.steps[i], &run->st[i]);
    }
    prereq_dump(ctx, &s_set);   // verbose-only map validation (AP-060)
    return true;
}

void planner_close(void) {
    free(s_snap);
    s_snap = NULL;
}

int planner_candidates(ExecCtx *ctx, PlannerRun *run, PlanCand *out,
                       int cap) {
    if (worldsnap_calendar_dead() || cap < 1) return 0;
    // The armed wait pass covers the WHOLE cycle head, not just the attempt:
    // logistics stocking trips and mover pricing may wait when the run is
    // armed. planner_step re-applies the flag per attempt.
    exec_set_wait_allowed(run->wait_armed);
    // Age every still-open objective that has already failed once: one more
    // cycle stuck. Calendar-free -- the keystone promotion (AP-055) keys off
    // this count, not elapsed days, so the day budget never shifts the decision.
    for (int i = 0; i < s_set.count; i++)
        if (!run->st[i].done && run->st[i].ever_failed) {
            run->st[i].stuck_cycles++;
            hist_store(run, &s_set.steps[i], &run->st[i]);
        }
    planner_logistics(ctx);

    // Open objectives + the finale gate (AP-052).
    int order[STEP_MAX];
    int costs[STEP_MAX];
    NavPoint pts[STEP_MAX];
    int open_n = 0;
    int scepter_i = -1;
    for (int i = 0; i < s_set.count; i++) {
        if (run->st[i].done) continue;
        if (s_set.steps[i].kind == STEP_SCEPTER) {
            scepter_i = i;
            continue;
        }
        order[open_n] = i;
        pts[open_n].zone_index = s_set.steps[i].zone_index;
        pts[open_n].x = s_set.steps[i].x;
        pts[open_n].y = s_set.steps[i].y;
        open_n++;
    }
    if (open_n == 0) {
        // The finale (AP-052): the scepter is offered only when every other
        // objective is done.
        if (scepter_i < 0) return 0;
        const PlanStep *s = &s_set.steps[scepter_i];
        out[0].kind = (int)s->kind;
        out[0].zone_index = s->zone_index;
        out[0].x = s->x;
        out[0].y = s->y;
        snprintf(out[0].handle, sizeof out[0].handle, "%s", s->handle);
        out[0].step_index = scepter_i;
        return 1;
    }

    // Cheapest-first by the mover's one-relaxation quote (AP-050).
    // Cross-zone candidates tie at the sentinel; tie-break by zone so a
    // sail is followed by that whole zone's work, not a zigzag of seas.
    {
        int all_costs[STEP_MAX];
        move_price_all(ctx, pts, open_n, all_costs);
        for (int i = 0; i < open_n; i++) costs[i] = all_costs[i];
        for (int a = 0; a < open_n; a++)
            for (int b = a + 1; b < open_n; b++) {
                bool swap2 = costs[b] < costs[a];
                if (costs[b] == costs[a] &&
                    s_set.steps[order[b]].zone_index <
                        s_set.steps[order[a]].zone_index)
                    swap2 = true;
                if (swap2) {
                    int tc = costs[a]; costs[a] = costs[b]; costs[b] = tc;
                    int to = order[a]; order[a] = order[b]; order[b] = to;
                }
            }
    }

    // First-villain keystone (AP-055/AP-058): while NO objective of the
    // keystone kind is done yet, promote the cheapest IN-ZONE one to the
    // front; once one lands, greedy's natural order resumes.
    if (s_keystone_kind >= 0) {
        int done_k = 0;
        for (int i = 0; i < s_set.count; i++)
            if ((int)s_set.steps[i].kind == s_keystone_kind && run->st[i].done)
                done_k++;
        if (done_k == 0) {
            int hz = zone_index_of(ctx->res, ctx->g->position.zone);
            for (int oi = 0; oi < open_n; oi++) {
                if ((int)s_set.steps[order[oi]].kind != s_keystone_kind)
                    continue;
                if (s_set.steps[order[oi]].zone_index != hz) continue;
                if (oi > 0) {
                    int t = order[oi];
                    memmove(&order[1], &order[0],
                            sizeof(int) * (size_t)oi);
                    order[0] = t;
                }
                break;
            }
        }
    }

    // The scarce-winner conflict tier (AP-054): one swap per cycle.
    {
        int swap = scarce_winner_swap(ctx, &s_set, run->st, order, open_n);
        if (swap > 0) {
            int t = order[swap];
            memmove(&order[1], &order[0], sizeof(int) * (size_t)swap);
            order[0] = t;
        }
    }

    // The keystone tier (AP-081): the alcove unlocks the entire spell
    // economy. A keystone STUCK for more than KEYSTONE_STUCK_CYCLES planner
    // cycles goes FIRST the moment money exists.
    if (!ctx->g->stats.knows_magic &&
        ctx->g->stats.gold >= ctx->res->economy.alcove_cost) {
        for (int oi = 0; oi < open_n; oi++) {
            if (s_set.steps[order[oi]].kind != STEP_ALCOVE) continue;
            if (!run->st[order[oi]].ever_failed ||
                run->st[order[oi]].stuck_cycles <= KEYSTONE_STUCK_CYCLES)
                break;
            if (oi > 0) {
                int t = order[oi];
                memmove(&order[1], &order[0], sizeof(int) * (size_t)oi);
                order[0] = t;
            }
            break;
        }
    }

    int n = open_n < cap ? open_n : cap;
    for (int oi = 0; oi < n; oi++) {
        const PlanStep *s = &s_set.steps[order[oi]];
        out[oi].kind = (int)s->kind;
        out[oi].zone_index = s->zone_index;
        out[oi].x = s->x;
        out[oi].y = s->y;
        snprintf(out[oi].handle, sizeof out[oi].handle, "%s", s->handle);
        out[oi].step_index = order[oi];
    }
    return n;
}

// Resolve a candidate against the CURRENT enumeration: the transient index
// when it still matches, else an identity scan (the enumeration may have
// shifted under a caller holding an older candidate).
static int cand_resolve(const PlanCand *c) {
    if (c->step_index >= 0 && c->step_index < s_set.count) {
        const PlanStep *s = &s_set.steps[c->step_index];
        if ((int)s->kind == c->kind && s->zone_index == c->zone_index &&
            s->x == c->x && s->y == c->y &&
            strcmp(s->handle, c->handle) == 0)
            return c->step_index;
    }
    for (int i = 0; i < s_set.count; i++) {
        const PlanStep *s = &s_set.steps[i];
        if ((int)s->kind == c->kind && s->zone_index == c->zone_index &&
            s->x == c->x && s->y == c->y &&
            strcmp(s->handle, c->handle) == 0)
            return i;
    }
    return -1;
}

PlannerStepResult planner_step(ExecCtx *ctx, PlannerRun *run,
                               const PlanCand *cand) {
    int i = cand_resolve(cand);
    if (i < 0 || run->st[i].done) return PLANNER_STEP_FAIL;
    run->cycles_used++;

    exec_set_wait_allowed(run->wait_armed);
    bool ok = planner_attempt(ctx, &s_set.steps[i], &run->st[i]);
    exec_set_wait_allowed(false);

    // NOTE: done flags of OTHER objectives are deliberately NOT refreshed
    // here. Refreshing is the CALLER's call (planner_refresh_done): an
    // in-passing completion that stays stale until then is part of the
    // cheapest-first decision fabric. The search wants per-step truth --
    // each node's progress count must be exact -- so it refreshes after
    // every step.
    hist_store(run, &s_set.steps[i], &run->st[i]);

    if (worldsnap_calendar_dead()) return PLANNER_STEP_TERMINAL;
    if (ctx->g->stats.game_over && !ctx->g->stats.won)
        return PLANNER_STEP_TERMINAL;
    if (ok) {
        // A success re-arms deferral and the escape budget (AP-053): the
        // world moved, so waiting and sacrificing are justified again later.
        run->st[i].stuck_cycles = 0;
        hist_store(run, &s_set.steps[i], &run->st[i]);
        run->wait_armed = false;
        run->sacrifices = 0;
        return PLANNER_STEP_OK;
    }
    if (run->st[i].defer_cause == EXEC_CAUSE_PREFOUGHT)
        return PLANNER_STEP_KEPT;
    return PLANNER_STEP_FAIL;
}

bool planner_step_sacrifice(ExecCtx *ctx, PlannerRun *run) {
    // Two zero-success cycles and a sealing foe at arm's reach: the game's
    // own escape hatch is a deliberate defeat -- temp death teleports the
    // hero home for the price of the standing army. Bounded by the
    // sacrifices count (not the calendar) and recorded like any fight
    // (AP-056).
    if (run->sacrifices >= 4) return false;
    const FoeState *adj = NULL;
    for (int f2 = 0; f2 < ctx->g->foe_count; f2++) {
        const FoeState *f = &ctx->g->foes[f2];
        if (!f->alive || f->friendly) continue;
        if (strcmp(f->zone, ctx->g->position.zone) != 0) continue;
        int ddx = f->x - ctx->g->position.x;
        int ddy = f->y - ctx->g->position.y;
        if (ddx < 0) ddx = -ddx;
        if (ddy < 0) ddy = -ddy;
        if ((ddx > ddy ? ddx : ddy) <= GAME_FOE_FOLLOW_RANGE) {
            adj = f;
            break;
        }
    }
    if (adj) {
        run->sacrifices++;
        if (ob_diag_verbose())
            printf("[PLANNER] sacrifice escape %d: attacking %s to "
                   "temp-death home\n", run->sacrifices, adj->placement_id);
        NavPoint fp2 = { hero_zone_index(ctx), adj->x, adj->y };
        ExecCause cc2;
        int esc_before = ctx->g->stats.days_left;
        long esc_cross = ledger_committed(DAY_ACCT_CROSSING);
        move_to(ctx, &fp2, 1, true, NULL, &cc2);
        ledger_book_move(DAY_ACCT_OTHER, esc_before,
                         ctx->g->stats.days_left, esc_cross);
        if (pending_flow == FLOW_ATTACK_FOE) {
            CombatResult rr2;
            exec_fight(ctx, true, &rr2);
            run->wait_armed = false;
            return true;   // fresh cycles from home (or from a win)
        }
        return false;
    }
    if (exec_dismiss_last_escape(ctx)) {
        // No foe to lose to (a boatless, dockless pocket): the game's OTHER
        // escape is dismissing the last stack, whose "sent back to King"
        // confirm is the same temp-death home. Same budget, same accounting.
        run->sacrifices++;
        if (ob_diag_verbose())
            printf("[PLANNER] sacrifice escape %d: dismissed the "
                   "army to temp-death home\n", run->sacrifices);
        run->wait_armed = false;
        return true;   // fresh cycles from home
    }
    return false;
}

void planner_refresh_done(ExecCtx *ctx, PlannerRun *run) {
    // A siege can complete a foe objective in passing and vice versa -- the
    // predicate is the only truth.
    for (int k = 0; k < s_set.count; k++)
        if (!run->st[k].done)
            run->st[k].done = planstep_is_done(ctx->g, &s_set.steps[k]);
}

bool planner_done(const ExecCtx *ctx, const PlannerRun *run) {
    (void)ctx;
    for (int i = 0; i < s_set.count; i++)
        if (!run->st[i].done) return false;
    return s_set.count > 0;
}

void planner_first_unmet(const PlannerRun *run, char *out_label, int label_cap,
                         char *out_cause, int cause_cap) {
    if (out_label && label_cap > 0) out_label[0] = '\0';
    if (out_cause && cause_cap > 0) out_cause[0] = '\0';
    for (int i = 0; i < s_set.count; i++) {
        if (run->st[i].done) continue;
        if (out_label && label_cap > 0)
            snprintf(out_label, (size_t)label_cap, "%s", s_set.steps[i].label);
        if (out_cause && cause_cap > 0) {
            // Prefer the objective's own last-attempt detail; fall back to the
            // typed cause name when it left none.
            const char *why = run->st[i].defer_why[0]
                                  ? run->st[i].defer_why
                                  : exec_cause_name(run->st[i].defer_cause);
            snprintf(out_cause, (size_t)cause_cap, "%s", why);
        }
        return;
    }
}

// One unmet objective's truthful "why": the text its own LAST attempt left,
// or the finale rule when the scepter is merely waiting on the others (saying
// so beats the bare "cause=none why=-" that state would otherwise print).
#define UNMET_CAUSE_N ((int)EXEC_CAUSE_REACH + 1)
static const char *unmet_why(const PlanStep *step, const ObjState *st,
                             int open_other, char *buf, size_t buf_sz) {
    if (step->kind == STEP_SCEPTER && st->defer_cause == EXEC_CAUSE_NONE &&
        open_other > 0) {
        snprintf(buf, buf_sz, "finale-gated:%d-objectives-unmet", open_other);
        return buf;
    }
    return st->defer_why[0] ? st->defer_why : "-";
}

int planner_report(ExecCtx *ctx, PlannerRun *run, int *out_total) {
    int done_n = 0;
    int open_other = 0;   // unmet non-scepter objectives (the finale gate)
    for (int i = 0; i < s_set.count; i++) {
        run->st[i].done = planstep_is_done(ctx->g, &s_set.steps[i]);
        if (run->st[i].done) done_n++;
        else if (s_set.steps[i].kind != STEP_SCEPTER) open_other++;
    }
    // The done-flag refresh above always runs (planner_first_unmet reads it);
    // only the [UNMET]/verdict/ledger PRINT is silenced -- by s_quiet (probe
    // rollouts) or ob_diag_quiet (a caller rendering its own report).
    if (!s_quiet && !ob_diag_quiet()) {
        int unmet_n = s_set.count - done_n;
        if (ob_diag_verbose()) {
            // Every unmet objective, named, with its own last attempt's cause.
            for (int i = 0; i < s_set.count; i++) {
                if (run->st[i].done) continue;
                char gated[64];
                printf("[UNMET] %s cause=%s why=%s\n", s_set.steps[i].label,
                       exec_cause_name(run->st[i].defer_cause),
                       unmet_why(&s_set.steps[i], &run->st[i], open_other,
                                 gated, sizeof gated));
            }
        } else if (unmet_n > 0) {
            // Default output: a miss can leave hundreds of objectives open, so
            // roll them up by cause -- one line each, most common first, with
            // a representative objective. Bounded by the cause count.
            int n_by[UNMET_CAUSE_N], ex_by[UNMET_CAUSE_N];
            memset(n_by, 0, sizeof n_by);
            for (int c = 0; c < UNMET_CAUSE_N; c++) ex_by[c] = -1;
            for (int i = 0; i < s_set.count; i++) {
                if (run->st[i].done) continue;
                int c = (int)run->st[i].defer_cause;
                if (c < 0 || c >= UNMET_CAUSE_N) c = (int)EXEC_CAUSE_OTHER;
                n_by[c]++;
                if (ex_by[c] < 0) ex_by[c] = i;
            }
            // No denominator here: this report runs against the COMMITTED
            // node's enumeration, which has shrunk as objectives completed
            // (AP-205), so its total is not the root total the verdict line
            // prints. The unmet COUNT is the same in either frame.
            printf("[UNMET] %d objectives unmet, by cause:\n", unmet_n);
            for (;;) {
                int top = -1;
                for (int c = 0; c < UNMET_CAUSE_N; c++)
                    if (n_by[c] > 0 && (top < 0 || n_by[c] > n_by[top]))
                        top = c;                  // ties keep enum order
                if (top < 0) break;
                int i = ex_by[top];
                char gated[64];
                printf("[UNMET]   %-16s n=%-4d e.g. %s why=%s\n",
                       exec_cause_name((ExecCause)top), n_by[top],
                       s_set.steps[i].label,
                       unmet_why(&s_set.steps[i], &run->st[i], open_other,
                                 gated, sizeof gated));
                n_by[top] = 0;
            }
        }
        if (s_print_verdict)
            printf("[VERDICT READY] %d/%d completed; verdict=%s\n", done_n,
                   s_set.count,
                   done_n == s_set.count ? "SOLVED" : "NOT-SOLVED");
        ledger_report();
    }
    if (out_total) *out_total = s_set.count;
    return done_n;
}

