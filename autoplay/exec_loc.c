// autoplay/exec_loc.c
//
// Small executor helpers shared across the flat set (AP-002): zone lookups,
// recorded calendar spends (week / rest-day), passive-queue pumping, the
// gate-charge law's cast site (AP-110/AP-111), spell purchases, raise casts,
// and the garrison/ungarrison/dismiss engine touches.

#include "exec.h"

#include <stdio.h>
#include <string.h>

#include "diag.h"
#include "exec_ledger.h"
#include "flows.h"
#include "pending.h"
#include "player_io.h"
#include "recording.h"
#include "tables.h"

static bool s_wait_allowed = false;

void exec_set_wait_allowed(bool on) { s_wait_allowed = on; }
bool exec_wait_allowed(void) { return s_wait_allowed; }

const char *exec_cause_name(ExecCause c) {
    switch (c) {
    case EXEC_CAUSE_NONE:            return "none";
    case EXEC_CAUSE_OTHER:           return "other";
    case EXEC_CAUSE_NO_WINNING_ARMY: return "no-winning-army";
    case EXEC_CAUSE_STRANDED:        return "stranded";
    case EXEC_CAUSE_TIME:            return "time";
    case EXEC_CAUSE_PREFOUGHT:       return "prefought";
    case EXEC_CAUSE_GOLD:            return "gold";
    case EXEC_CAUSE_STOCK:           return "stock";
    case EXEC_CAUSE_LEADERSHIP:      return "leadership";
    case EXEC_CAUSE_REACH:           return "reach";
    }
    return "?";
}

int zone_index_of(const Resources *res, const char *zone_id) {
    if (!res || !zone_id) return -1;
    for (int i = 0; i < res->zone_count; i++)
        if (strcmp(res->zones[i].id, zone_id) == 0) return i;
    return -1;
}

int hero_zone_index(const ExecCtx *ctx) {
    return zone_index_of(ctx->res, ctx->g->position.zone);
}

void exec_pump_passive(ExecCtx *ctx) {
    player_io_drain_messages(ctx->g);
    // Headless ack of the two week-end screens (astrology -> budget): the
    // scheduling globals are shell-presentation state; clearing them is the
    // "any key" dismiss.
    pending_week_phase = WK_PHASE_NONE;
}

bool exec_spend_week(ExecCtx *ctx, int acct_tag) {
    Game *g = ctx->g;
    if (g->stats.game_over) return false;
    // Spend to the engine's next week boundary, crossing exactly one. The
    // boundary is keyed on days ELAPSED (passed = start_days - days_left), the
    // same phase GameWeekId/astrology use -- NOT on days_left. Using
    // days_left % wd would tie the spend to the absolute day budget, whose
    // phase shifts with the difficulty; the elapsed phase is identical for
    // every budget at the same point in the run, so game length cannot change
    // play.
    int wd = ctx->res->time.week_days > 0 ? ctx->res->time.week_days : 5;
    int start_days = ctx->res->time.days_per_difficulty[
        (int)g->character.difficulty];
    int into_week = (start_days - g->stats.days_left) % wd;
    int spend = (into_week == 0) ? wd : wd - into_week;
    rec_push_action(g, RA_SPEND_WEEK, NULL, spend, 0);
    int before = g->stats.days_left;
    GameSpendDays(g, spend, NULL);
    day_acct_add((DayAcctTag)acct_tag, before - g->stats.days_left);
    exec_pump_passive(ctx);
    return !g->stats.game_over;
}

int gate_spell_index(bool town_gate) {
    return spell_index_by_adventure_effect(town_gate ? ADV_EFFECT_GATE_TOWN
                                                     : ADV_EFFECT_GATE_CASTLE);
}

// The spellbook split (exec.h BookBudget): all thresholds in law-floor units
// scaled by the transport gates the pack actually defines.
void exec_book_budget(const Game *g, BookBudget *out) {
    const int F = GATE_LAW_MIN_CHARGES;
    int transports = (gate_spell_index(false) >= 0 ? 1 : 0) +
                     (gate_spell_index(true) >= 0 ? 1 : 0);
    out->floor_all = transports * F;
    int tier1 = out->floor_all + 2 * F + 1;   // castle bulk 2F opens here
    int tier2 = out->floor_all + 4 * F;       // castle bulk 3F opens here
    out->castle_gate_want = F;
    if (g->stats.max_spells >= tier2)      out->castle_gate_want = 3 * F;
    else if (g->stats.max_spells >= tier1) out->castle_gate_want = 2 * F;
    out->town_participates = g->stats.max_spells >= out->floor_all + 3 * F;
    out->stop_clamp = out->floor_all + 4 * F;
}

int spell_charges(const Game *g, int spell_idx) {
    if (!g || spell_idx < 0 || spell_idx >= GAME_SPELLBOOK_SLOTS) return 0;
    return g->spells.counts[spell_idx];
}

// The town (id) selling `spell_idx` this game, or NULL.
static const char *town_selling(const Game *g, int spell_idx) {
    const SpellDef *sd = spell_by_index(spell_idx);
    if (!sd) return NULL;
    for (int i = 0; i < GAME_TOWNS; i++) {
        if (!g->towns[i].id[0]) continue;
        if (strcmp(g->towns[i].spell_for_sale, sd->id) == 0)
            return g->towns[i].id;
    }
    return NULL;
}

// Does the town at the gate destination's tile sell this gate spell (the
// seller exemption, AP-110)?
static bool dest_is_seller(const ExecCtx *ctx, bool town_gate,
                           const char *dest_zone, int x, int y) {
    if (!town_gate) return false;
    int idx = gate_spell_index(true);
    const SpellDef *sd = idx >= 0 ? spell_by_index(idx) : NULL;
    if (!sd) return false;
    for (int i = 0; i < ctx->res->town_count; i++) {
        const ResTown *t = &ctx->res->towns[i];
        if (strcmp(t->zone, dest_zone) != 0) continue;
        // Gate landings default to the town's boat coords (REQ-322).
        if (!((t->gate_x == x && t->gate_y == y) ||
              (t->boat_x == x && t->boat_y == y) ||
              (t->x == x && t->y == y)))
            continue;
        const TownRecord *tr = NULL;
        for (int k = 0; k < GAME_TOWNS; k++) {
            if (strcmp(ctx->g->towns[k].id, t->id) == 0) {
                tr = &ctx->g->towns[k];
                break;
            }
        }
        return tr && strcmp(tr->spell_for_sale, sd->id) == 0;
    }
    return false;
}

// Public probe of the seller exemption (AP-110): is this town-gate landing
// the gate seller's own town? The mover's in-zone leg scorer needs it to
// model the 1-charge cast the law allows.
bool exec_gate_dest_is_seller(ExecCtx *ctx, bool town_gate,
                              const char *dest_zone, int x, int y) {
    return dest_is_seller(ctx, town_gate, dest_zone, x, y);
}

bool exec_buy_spell_at(ExecCtx *ctx, const char *town_id) {
    Game *g = ctx->g;
    if (!g->position.in_town[0] || strcmp(g->position.in_town, town_id) != 0)
        return false;
    int mk = recsink_mark();
    rec_push_action(g, RA_BUY_SPELL, town_id, 0, 0);
    SpellBuyResult r = GameBuySpell(g, town_id);
    if (r != SPELL_BUY_OK) { recsink_rollback(mk); return false; }
    return true;
}

// Cast a gate under the law (AP-110/AP-111): legal only from a supply of two
// (or one, to the seller); a landing at the seller refills to the floor when
// gold allows.
bool exec_gate_to(ExecCtx *ctx, bool town_gate, const char *dest_zone,
                  int dest_x, int dest_y) {
    Game *g = ctx->g;
    if (!g->stats.knows_magic) return false;   // play-legality (REQ-323)
    int idx = gate_spell_index(town_gate);
    if (idx < 0) return false;
    int have = spell_charges(g, idx);
    bool seller = dest_is_seller(ctx, town_gate, dest_zone, dest_x, dest_y);
    if (have < GATE_LAW_MIN_CHARGES && !(seller && have >= 1)) return false;

    GateDestination dest;
    memset(&dest, 0, sizeof dest);
    snprintf(dest.zone, sizeof dest.zone, "%s", dest_zone);
    dest.x = dest_x;
    dest.y = dest_y;
    const SpellDef *sd = spell_by_index(idx);
    if (!sd) return false;
    int cast_mark = recsink_mark();
    rec_push_action(g, town_gate ? RA_GATE_TOWN : RA_GATE_CASTLE,
                    dest_zone, dest_x, dest_y);
    if (!GameGateTeleport(g, ctx->map, ctx->fog, &dest, sd->id)) {
        recsink_rollback(cast_mark);
        return false;
    }
    if (ob_diag_verbose())
        printf("[NAV] gate teleport (%s) -> %s (%d,%d) day=%d\n",
               town_gate ? "town" : "castle", dest_zone, dest_x, dest_y,
               g->stats.days_left);
    exec_pump_passive(ctx);
    // Seller refill (AP-111): the exemption's refill is enforced, not assumed.
    // The landing tile sits beside the town (REQ-322): step in first when the
    // teleport did not already put the hero in the town menu.
    if (seller && spell_charges(g, idx) < GATE_LAW_MIN_CHARGES) {
        const char *tid = town_selling(g, idx);
        if (tid && (!g->position.in_town[0] ||
                    strcmp(g->position.in_town, tid) != 0)) {
            const ResTown *rt2 = resources_town_by_id(ctx->res, tid);
            if (rt2 && strcmp(rt2->zone, g->position.zone) == 0) {
                int dx = rt2->x > g->position.x ? 1
                       : rt2->x < g->position.x ? -1 : 0;
                int dy = rt2->y > g->position.y ? 1
                       : rt2->y < g->position.y ? -1 : 0;
                if (dx || dy) exec_recorded_step(ctx, dx, dy);
                exec_answer_pending(ctx, true);
            }
        }
        if (tid && g->position.in_town[0] &&
            strcmp(g->position.in_town, tid) == 0) {
            while (spell_charges(g, idx) < GATE_LAW_MIN_CHARGES &&
                   exec_buy_spell_at(ctx, tid)) {}
        }
    }
    return true;
}

// Buy gate charges up to `want` at the selling town (travels there).
bool exec_stock_gate_charges(ExecCtx *ctx, bool town_gate, int want) {
    Game *g = ctx->g;
    int idx = gate_spell_index(town_gate);
    if (idx < 0) return false;
    if (spell_charges(g, idx) >= want) return true;
    // Book full => the buy at the seller returns SPELL_BUY_AT_CAP
    // (engine/game.c GameBuySpell: GameKnownSpells >= max_spells), so the whole
    // trip to the seller is futile. Skip it: the run otherwise sails to the
    // gate seller and back to fail this same purchase ~50 times, burning the
    // calendar on crossings (measured on seed 8: 419/594 committed days).
    if (GameKnownSpells(g) >= g->stats.max_spells) return false;
    const char *tid = town_selling(g, idx);
    if (!tid) {
        if (ob_diag_verbose())
            printf("[NAV] gate stock (%s) failed: no seller town this game\n",
                   town_gate ? "town" : "castle");
        return false;
    }
    const ResTown *rt = resources_town_by_id(ctx->res, tid);
    if (!rt) return false;
    NavPoint tp = { zone_index_of(ctx->res, rt->zone), rt->x, rt->y };
    ExecCause cc = EXEC_CAUSE_NONE;
    int sm_before = g->stats.days_left;
    long sm_cross = ledger_committed(DAY_ACCT_CROSSING);
    int sm_r = move_to(ctx, &tp, 1, true, NULL, &cc);
    ledger_book_move(DAY_ACCT_STOCKMOVE, sm_before, g->stats.days_left, sm_cross);
    if (sm_r < 0) {
        if (ob_diag_verbose())
            printf("[NAV] gate stock (%s) failed: seller %s (%d,%d,%s) "
                   "unreachable from (%d,%d,%s) cause=%s\n",
                   town_gate ? "town" : "castle", tid, rt->x, rt->y, rt->zone,
                   g->position.x, g->position.y, g->position.zone,
                   exec_cause_name(cc));
        return false;
    }
    while (spell_charges(g, idx) < want) {
        if (!exec_buy_spell_at(ctx, tid)) break;
    }
    if (ob_diag_verbose() && spell_charges(g, idx) < want)
        printf("[NAV] gate stock (%s) short at seller %s: have=%d want=%d "
               "gold=%d known=%d/%d in_town=%s\n",
               town_gate ? "town" : "castle", tid, spell_charges(g, idx),
               want, g->stats.gold, GameKnownSpells(g), g->stats.max_spells,
               g->position.in_town);
    return spell_charges(g, idx) >= want;
}

// Top up held gate kits to the law floor and no further (AP-112).
bool exec_topup_gate_kit(ExecCtx *ctx) {
    bool ok = true;
    for (int town = 0; town < 2; town++) {
        int idx = gate_spell_index(town == 1);
        if (idx < 0) continue;
        int have = spell_charges(ctx->g, idx);
        if (have > 0 && have < GATE_LAW_MIN_CHARGES)
            ok = exec_stock_gate_charges(ctx, town == 1,
                                         GATE_LAW_MIN_CHARGES) && ok;
    }
    return ok;
}

// Discard one charge of a combat spell through the engine's own field flow
// (FLOW_DISCARD_SPELL) to free a slot against the max_spells cap.
bool exec_discard_spell(ExecCtx *ctx, int spell_idx) {
    Game *g = ctx->g;
    if (spell_idx < 0 || spell_idx >= GAME_SPELLBOOK_SLOTS) return false;
    if (g->spells.counts[spell_idx] <= 0) return false;
    pending_discard_spell_idx = spell_idx;
    pending_flow = FLOW_DISCARD_SPELL;
    player_io_raise_decision(g, FLOW_DISCARD_SPELL, REQ_PROMPT_YES_NO,
                             NULL, NULL);
    rec_push_action(g, RA_DISCARD_SPELL, NULL, spell_idx, 0);
    FlowAnswer yes = { FLOW_ANS_YES, 0 };
    PlayerIoPresentation pres;
    player_io_answer(g, ctx->map, ctx->fog, ctx->res, yes,
                     PLAYER_IO_COMBAT_NOT_RUN, &pres);
    exec_pump_passive(ctx);
    return true;
}

// Evict one charge of adventure junk (AP-112): a spell no plan carries --
// not the one being stocked, not a transport gate, not raise / instant army /
// time stop (the logistics currencies). Chest rewards accumulate such
// charges and no cast-off path drains them; at a full book they jam every
// stocking loop. The engine's own discard flow accepts any spell.
bool exec_discard_junk_spell(ExecCtx *ctx, int keep_idx) {
    Game *g = ctx->g;
    int ri = spell_index_by_adventure_effect(ADV_EFFECT_RAISE_CONTROL);
    int ia = spell_index_by_adventure_effect(ADV_EFFECT_INSTANT_ARMY);
    int ts = spell_index_by_adventure_effect(ADV_EFFECT_TIME_STOP);
    int cg = gate_spell_index(false);
    int tg = gate_spell_index(true);
    int best = -1, best_cost = 0;
    for (int si = 0; si < spells_count(); si++) {
        const SpellDef *sd = spell_by_index(si);
        if (!sd || sd->kind != SPELL_KIND_ADVENTURE) continue;
        int idx = sd->index;
        if (idx == keep_idx || idx == ri || idx == ia || idx == ts ||
            idx == cg || idx == tg)
            continue;
        if (g->spells.counts[idx] <= 0) continue;
        if (best < 0 || sd->cost < best_cost) {
            best = idx;
            best_cost = sd->cost;
        }
    }
    return best >= 0 && exec_discard_spell(ctx, best);
}

bool exec_cast_raise(ExecCtx *ctx) {
    Game *g = ctx->g;
    if (!g->stats.knows_magic) return false;   // play-legality (REQ-323)
    int idx = spell_index_by_adventure_effect(ADV_EFFECT_RAISE_CONTROL);
    if (idx < 0 || g->spells.counts[idx] <= 0) return false;
    const SpellDef *sd = spell_by_index(idx);
    if (!sd) return false;
    rec_push_action(g, RA_CAST_ADV_SPELL, sd->id, 0, 0);
    GameCastRaiseControl(g);
    return true;
}

bool exec_garrison_slot(ExecCtx *ctx, const char *castle_id, int slot) {
    int mk = recsink_mark();
    rec_push_action(ctx->g, RA_GARRISON, castle_id, slot, 0);
    if (GameGarrisonTroop(ctx->g, castle_id, slot) != 0) {
        recsink_rollback(mk); return false;
    }
    return true;
}

bool exec_ungarrison_slot(ExecCtx *ctx, const char *castle_id, int slot) {
    int mk = recsink_mark();
    rec_push_action(ctx->g, RA_UNGARRISON, castle_id, slot, 0);
    if (GameUngarrisonTroop(ctx->g, castle_id, slot) != 0) {
        recsink_rollback(mk); return false;
    }
    return true;
}

bool exec_dismiss_slot(ExecCtx *ctx, int slot) {
    Game *g = ctx->g;
    if (slot < 0 || slot >= GAME_ARMY_SLOTS) return false;
    if (!g->army[slot].id[0] || g->army[slot].count <= 0) return false;
    // Never dismiss the last stack (that is temp-death, never a plan).
    int occupied = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++)
        if (g->army[i].id[0] && g->army[i].count > 0) occupied++;
    if (occupied <= 1) return false;
    pending_flow = FLOW_DISMISS_ARMY;
    player_io_raise_decision(g, FLOW_DISMISS_ARMY, REQ_PROMPT_NUMERIC,
                             NULL, NULL);
    rec_push_action(g, RA_DISMISS, NULL, slot, 0);
    FlowAnswer ans = { (PromptAnswer)(FLOW_ANS_1 + slot), 0 };
    PlayerIoPresentation pres;
    player_io_answer(g, ctx->map, ctx->fog, ctx->res, ans,
                     PLAYER_IO_COMBAT_NOT_RUN, &pres);
    exec_pump_passive(ctx);
    return true;
}

// The DISMISS-LAST ESCAPE: dismissing the final stack is the game's own
// always-available exit -- the "sent back to King in disgrace" confirm chain
// ends in a temp-death teleport home (the same one a lost fight takes). The
// last resort for a marooned hero with no adjacent foe to lose to: a boatless
// dockless pocket has no other door. Costs the whole standing army.
bool exec_dismiss_last_escape(ExecCtx *ctx) {
    Game *g = ctx->g;
    int last = -1, occupied = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++)
        if (g->army[i].id[0] && g->army[i].count > 0) {
            occupied++;
            last = i;
        }
    if (last < 0) return false;
    // Shed every other stack first through the plain dismiss.
    int guard = GAME_ARMY_SLOTS;
    while (occupied > 1 && guard-- > 0) {
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (i == last || !g->army[i].id[0] || g->army[i].count <= 0)
                continue;
            exec_dismiss_slot(ctx, i);
            break;
        }
        occupied = 0;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++)
            if (g->army[i].id[0] && g->army[i].count > 0) occupied++;
    }
    if (occupied != 1) return false;
    // The last stack: numeric dismiss chains into the confirm; YES is the
    // temp death (player_io FLOW_DISMISS_ARMY -> chain_dismiss_last ->
    // FLOW_DISMISS_LAST, the exact shell path).
    pending_flow = FLOW_DISMISS_ARMY;
    player_io_raise_decision(g, FLOW_DISMISS_ARMY, REQ_PROMPT_NUMERIC,
                             NULL, NULL);
    int dismiss_mark = recsink_mark();
    rec_push_action(g, RA_DISMISS_LAST, NULL, last, 0);
    FlowAnswer pick = { (PromptAnswer)(FLOW_ANS_1 + last), 0 };
    PlayerIoPresentation pres;
    player_io_answer(g, ctx->map, ctx->fog, ctx->res, pick,
                     PLAYER_IO_COMBAT_NOT_RUN, &pres);
    if (!pres.chain_dismiss_last) {
        recsink_rollback(dismiss_mark);
        return false;
    }
    pending_flow = FLOW_DISMISS_LAST;
    player_io_raise_decision(g, FLOW_DISMISS_LAST, REQ_PROMPT_YES_NO,
                             NULL, NULL);
    FlowAnswer yes = { FLOW_ANS_YES, 0 };
    PlayerIoPresentation pres2;
    player_io_answer(g, ctx->map, ctx->fog, ctx->res, yes,
                     PLAYER_IO_COMBAT_NOT_RUN, &pres2);
    if (!pres2.temp_death) return false;
    exec_temp_death(ctx);
    exec_pump_passive(ctx);
    return true;
}
