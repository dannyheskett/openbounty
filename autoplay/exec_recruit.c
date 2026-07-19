// autoplay/exec_recruit.c
//
// THE RECRUITING SUBSYSTEM ("the module"): sources (AP-122), the plan catalog
// (AP-130), the SIM FUNNEL (AP-131), the leadership lift (AP-132), the kit
// axes (AP-136/AP-180), fight-day garrison growth (AP-183), the in-place
// restock accumulation (AP-137), commit (AP-140..142), the single-relaxation
// cause probes (AP-134), the scarce-winner queries (AP-054), and the
// gold-wait site (AP-161).
//
// Every constraint here is an engine quantity (AP-121): gold, per-stack
// control, spell-charge capacity, source stock, travel legality, weekly
// economics. The funnel builds candidates with pure arithmetic, collapses
// them (fold ~3%, dominate, at most SIM_RUNGS rungs), and simulates only the
// rungs through the one oracle (predict_combat_cached).

#include "exec.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "exec_ledger.h"
#include "flow_answer.h"
#include "pending.h"
#include "player_io.h"
#include "recording.h"
#include "spells_adventure.h"
#include "tables.h"

#define SIM_RUNGS   16
#define SIM_FOLD_DEN 32        // fold threshold: < 1/32 (~3%) hp-worth movement
#define PLAN_MAX_GROUPS 40
// Upper bound on enumerable sources, derived from engine maxima: every
// garrison stack + every dwelling row + the home-castle pool (at most one
// per troop) + the instant army.
#define MAX_SOURCES (GAME_CASTLES * GAME_ARMY_SLOTS + GAME_MAX_DWELLINGS + \
                     CAT_TROOPS_MAX + 1)
// Candidate arena. Endgame enumerations saturate it (the "cand-arena"
// watchdog counter measures how often), so the arena's edge participates
// in which winner the funnel picks -- raising it to 65536 changed choices
// enough to flip 3 of 30 validation seeds and was reverted. The value is
// therefore behavior-locked at its long-validated size; treat any change
// as a full re-validation.
#define MAX_CANDS   16384

// ---- sources (AP-122) ---------------------------------------------------------

typedef enum {
    RSRC_GARRISON = 0,
    RSRC_INZONE_DWELLING,
    RSRC_INSTANT_ARMY,
    RSRC_HOME_CASTLE,
    RSRC_OFFZONE_DWELLING,
} RecruitSourceKind;

typedef struct {
    RecruitSourceKind kind;
    int  troop_idx;
    int  avail;          // stock (INT_MAX/4 for effectively unbounded)
    int  unit_cost;
    int  zone_index;     // -1 = anywhere (instant army)
    int  x, y;
    char src_id[24];     // owning castle for a garrison stack
} RecruitSource;

static RecruitSource s_sources[MAX_SOURCES];
static int s_source_n;

// ---- realize-failure memory (AP-138) -------------------------------------------
//
// A chosen winner whose realize starved of STOCK proved the paper quotes
// wrong for that (target, troop): the shortfall-sim rejected the partial
// fill and every purchasable source ran dry mid-plan. Re-picking the
// identical plan next cycle would be a deterministic loop -- so remember the
// starved troop per target and skip plan groups that rely on it. This is
// recruiter memory, not world state: it deliberately survives the attempt
// rollback and resets per run.
#define REALIZE_EXCL_MAX 128
static struct {
    char seed_key[32];
    int  troop;
} s_excl[REALIZE_EXCL_MAX];
static int s_excl_n;
static int s_realize_stock_troop = -1;   // starved troop of the last realize
static int s_realize_reserve;            // approach money the last realize banked

// Reset the recruiter's cross-attempt memory: once per run, and again on every
// search node restore (AP-208) -- the search jumps between lines of play and
// must not carry one line's failure memory or banked reserve into another.
void recruit_exclusions_reset(void) {
    s_excl_n = 0;
    s_realize_stock_troop = -1;
    s_realize_reserve = 0;
}

static bool excl_has(const char *seed_key, int troop) {
    if (!seed_key) return false;
    for (int i = 0; i < s_excl_n; i++)
        if (s_excl[i].troop == troop &&
            strcmp(s_excl[i].seed_key, seed_key) == 0)
            return true;
    return false;
}

static void excl_add(const char *seed_key, int troop) {
    if (!seed_key || troop < 0 || s_excl_n >= REALIZE_EXCL_MAX) return;
    if (excl_has(seed_key, troop)) return;
    snprintf(s_excl[s_excl_n].seed_key, sizeof s_excl[s_excl_n].seed_key,
             "%s", seed_key);
    s_excl[s_excl_n].troop = troop;
    s_excl_n++;
}

// Drop every exclusion for one target. Returns how many were dropped. Used
// when the exclusions have exhausted the target's winner set -- the memory
// was recorded at the wallet of a PAST failure, and a richer attempt
// deserves the full universe again (a permanently-poisoned last villain is
// a lost game).
static int excl_clear(const char *seed_key) {
    if (!seed_key) return 0;
    int kept = 0, dropped = 0;
    for (int i = 0; i < s_excl_n; i++) {
        if (strcmp(s_excl[i].seed_key, seed_key) == 0) {
            dropped++;
            continue;
        }
        s_excl[kept++] = s_excl[i];
    }
    s_excl_n = kept;
    return dropped;
}


static void add_source(RecruitSourceKind kind, int troop_idx, int avail,
                       int unit_cost, int zone_index, int x, int y,
                       const char *src_id) {
    if (s_source_n >= MAX_SOURCES || avail <= 0 || troop_idx < 0) return;
    RecruitSource *s = &s_sources[s_source_n++];
    s->kind = kind;
    s->troop_idx = troop_idx;
    s->avail = avail;
    s->unit_cost = unit_cost;
    s->zone_index = zone_index;
    s->x = x;
    s->y = y;
    s->src_id[0] = '\0';
    if (src_id) snprintf(s->src_id, sizeof s->src_id, "%s", src_id);
}

// Array order IS the buy preference (AP-122).
static void recruit_sources_enumerate(const ExecCtx *ctx) {
    const Game *g = ctx->g;
    s_source_n = 0;
    int hz = hero_zone_index(ctx);

    // 1. Garrisons (already paid).
    for (int i = 0; i < GAME_CASTLES; i++) {
        const CastleRecord *cr = &g->castles[i];
        if (!cr->id[0] || cr->owner_kind != CASTLE_OWNER_PLAYER) continue;
        const ResCastle *rc = resources_castle_by_id(ctx->res, cr->id);
        if (!rc) continue;
        int zi = zone_index_of(ctx->res, rc->zone);
        for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
            if (!cr->garrison[s].id[0] || cr->garrison[s].count <= 0) continue;
            const TroopDef *t = troop_by_id(cr->garrison[s].id);
            if (!t) continue;
            add_source(RSRC_GARRISON, t->index, cr->garrison[s].count, 0,
                       zi, rc->x, rc->y, cr->id);
        }
    }
    // 2. In-zone dwellings.
    for (int i = 0; i < g->dwelling_count; i++) {
        const DwellingState *d = &g->dwellings[i];
        if (!d->troop_id[0] || d->count <= 0) continue;
        const TroopDef *t = troop_by_id(d->troop_id);
        if (!t) continue;
        int zi = zone_index_of(ctx->res, d->zone);
        if (zi != hz) continue;
        add_source(RSRC_INZONE_DWELLING, t->index, d->count, t->recruit_cost,
                   zi, d->x, d->y, NULL);
    }
    // 3. Instant army (the class's conjured troop, castable anywhere --
    //    casting needs knows_magic, REQ-323). Troop and per-cast count come
    //    from the engine's own cast arithmetic (GameInstantArmyTroop /
    //    GameInstantArmyPerCast), so a re-tuned pack prices the source the
    //    way casts deliver.
    if (g->stats.knows_magic) {
        int ia_idx = spell_index_by_adventure_effect(ADV_EFFECT_INSTANT_ARMY);
        const SpellDef *sd = ia_idx >= 0 ? spell_by_index(ia_idx) : NULL;
        const TroopDef *t = GameInstantArmyTroop(g);
        if (sd && t) {
            int per = GameInstantArmyPerCast(g);
            int unit = sd->cost / per + 1;
            add_source(RSRC_INSTANT_ARMY, t->index, INT_MAX / 4, unit,
                       -1, -1, -1, NULL);
        }
    }
    // 4. Home-castle pool (dwelling == "castle" troops, effectively unbounded).
    {
        const ResCastle *home = NULL;
        for (int i = 0; i < ctx->res->castle_count; i++) {
            if (resources_castle_is_home(&ctx->res->castles[i])) {
                home = &ctx->res->castles[i];
                break;
            }
        }
        if (home) {
            int zi = zone_index_of(ctx->res, home->zone);
            for (int ti = 0; ti < troops_count(); ti++) {
                const TroopDef *t = troop_by_index(ti);
                if (!t || strcmp(t->dwelling, "castle") != 0) continue;
                add_source(RSRC_HOME_CASTLE, ti, INT_MAX / 4, t->recruit_cost,
                           zi, home->x, home->y, home->id);
            }
        }
    }
    // 5. Off-zone dwellings -- never excluded.
    for (int i = 0; i < g->dwelling_count; i++) {
        const DwellingState *d = &g->dwellings[i];
        if (!d->troop_id[0] || d->count <= 0) continue;
        const TroopDef *t = troop_by_id(d->troop_id);
        if (!t) continue;
        int zi = zone_index_of(ctx->res, d->zone);
        if (zi == hz) continue;
        add_source(RSRC_OFFZONE_DWELLING, t->index, d->count, t->recruit_cost,
                   zi, d->x, d->y, NULL);
    }
}

// ---- quotes (AP-131) -----------------------------------------------------------

typedef struct {
    int held;                  // already in the army
    int n;                     // sources for this troop, cheapest-first
    int src[MAX_SOURCES];
    int stock[MAX_SOURCES];    // possibly relaxed (probe mode)
} TroopQuote;

static TroopQuote s_quotes[CAT_TROOPS_MAX];

static void quotes_build(const ExecCtx *ctx, bool relax_stock) {
    const Game *g = ctx->g;
    memset(s_quotes, 0, sizeof s_quotes);
    for (int ti = 0; ti < troops_count() && ti < CAT_TROOPS_MAX; ti++) {
        const TroopDef *t = troop_by_index(ti);
        TroopQuote *q = &s_quotes[ti];
        for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
            if (g->army[s].id[0] && strcmp(g->army[s].id, t->id) == 0)
                q->held += g->army[s].count;
        }
        // Gather this troop's sources in array order, then cheapest-first.
        for (int si = 0; si < s_source_n && q->n < MAX_SOURCES; si++) {
            if (s_sources[si].troop_idx != ti) continue;
            q->src[q->n] = si;
            int stock = s_sources[si].avail;
            if (relax_stock &&
                (s_sources[si].kind == RSRC_INZONE_DWELLING ||
                 s_sources[si].kind == RSRC_OFFZONE_DWELLING)) {
                int cap = t->max_population;
                if (cap > stock) stock = cap;   // the restock ceiling (AP-054)
            }
            q->stock[q->n] = stock;
            q->n++;
        }
        // cheapest-first (stable)
        for (int a = 0; a < q->n; a++) {
            for (int b = a + 1; b < q->n; b++) {
                if (s_sources[q->src[b]].unit_cost <
                    s_sources[q->src[a]].unit_cost) {
                    int ts = q->src[a]; q->src[a] = q->src[b]; q->src[b] = ts;
                    int tk = q->stock[a]; q->stock[a] = q->stock[b];
                    q->stock[b] = tk;
                }
            }
        }
    }
}

// Fill `want` units of troop ti cheapest-first (quote_fill): returns units
// filled; accumulates gold and the toured zone mask.
static int quote_fill(const ExecCtx *ctx, int ti, int want,
                      long *gold, unsigned *zones_mask) {
    const TroopQuote *q = &s_quotes[ti];
    int got = q->held < want ? q->held : want;
    int need = want - got;
    int hz = hero_zone_index(ctx);
    for (int i = 0; i < q->n && need > 0; i++) {
        const RecruitSource *s = &s_sources[q->src[i]];
        int take = q->stock[i] < need ? q->stock[i] : need;
        if (take <= 0) continue;
        need -= take;
        got += take;
        if (gold) *gold += (long)take * s->unit_cost;
        if (zones_mask && s->zone_index >= 0 && s->zone_index != hz &&
            s->kind != RSRC_GARRISON)   // garrisons travel by gate: ~zero days
            *zones_mask |= 1u << s->zone_index;
    }
    return got;
}

// ---- the plan catalog (AP-130) ---------------------------------------------------

typedef struct {
    int  n;
    int  troops[GAME_ARMY_SLOTS];   // catalog indices, slot order
} PlanGroup;

static PlanGroup s_groups[PLAN_MAX_GROUPS];
static int s_group_n;

static int troop_strength_cmp(const void *a, const void *b) {
    const TroopDef *ta = troop_by_index(*(const int *)a);
    const TroopDef *tb = troop_by_index(*(const int *)b);
    int ha = ta ? ta->hit_points : 0, hb = tb ? tb->hit_points : 0;
    if (ha != hb) return hb - ha;
    return (*(const int *)a) - (*(const int *)b);
}

// A troop is plan-eligible when any stock exists (held or buyable).
static bool troop_available(int ti) {
    const TroopQuote *q = &s_quotes[ti];
    if (q->held > 0) return true;
    for (int i = 0; i < q->n; i++)
        if (q->stock[i] > 0) return true;
    return false;
}

static void plan_build(void) {
    s_group_n = 0;
    int all[CAT_TROOPS_MAX], all_n = 0;
    for (int ti = 0; ti < troops_count() && ti < CAT_TROOPS_MAX; ti++)
        if (troop_available(ti)) all[all_n++] = ti;
    qsort(all, (size_t)all_n, sizeof all[0], troop_strength_cmp);

    // Pure morale groups (a high-morale army is reachable only as one
    // group). The group set comes from the catalog, ascending -- the same
    // deterministic enumeration order on any pack.
    char groups[CAT_TROOPS_MAX];
    int group_letters = troop_morale_groups(groups, (int)sizeof groups);
    for (int gl = 0; gl < group_letters; gl++) {
        char grp = groups[gl];
        PlanGroup *pg = &s_groups[s_group_n];
        pg->n = 0;
        for (int i = 0; i < all_n && pg->n < GAME_ARMY_SLOTS; i++) {
            const TroopDef *t = troop_by_index(all[i]);
            if (t && t->morale_group == grp) pg->troops[pg->n++] = all[i];
        }
        if (pg->n > 0 && s_group_n < PLAN_MAX_GROUPS) s_group_n++;
    }
    // Mixed spread: the strongest troops regardless of group.
    if (s_group_n < PLAN_MAX_GROUPS) {
        PlanGroup *pg = &s_groups[s_group_n];
        pg->n = 0;
        for (int i = 0; i < all_n && pg->n < GAME_ARMY_SLOTS; i++)
            pg->troops[pg->n++] = all[i];
        if (pg->n > 0) s_group_n++;
    }
    // Singletons: one pure stack per available troop. Multi-slot groups fill
    // EVERY slot to the control cap, pricing the whole spread; a lone shooter
    // stack (no retaliation taken) can win at a fraction of that gold, and
    // only a 1-troop group ever builds it.
    for (int i = 0; i < all_n && s_group_n < PLAN_MAX_GROUPS; i++) {
        PlanGroup *pg = &s_groups[s_group_n];
        pg->n = 1;
        pg->troops[0] = all[i];
        s_group_n++;
    }
}

// A plan group relying on an excluded troop is skipped for that target
// (realize-failure memory, AP-138).
static bool group_excluded(const char *seed_key, const PlanGroup *pg) {
    for (int i = 0; i < pg->n; i++)
        if (excl_has(seed_key, pg->troops[i])) return true;
    return false;
}

// ---- candidates -------------------------------------------------------------------

typedef struct {
    int   group, order;         // catalog coordinates
    int   kit_spell;            // -1 none, else combat-spell index
    int   kit_charges;
    int   k;                    // raise casts
    int   troops[GAME_ARMY_SLOTS];
    int   counts[GAME_ARMY_SLOTS];
    int   n;
    long  hp_worth;
    int   days;
    long  gold;
    unsigned zones_mask;
    bool  realizable;           // gold fits the live wallet
    bool  win;                  // set by simulation
} Candidate;

static Candidate s_cands[MAX_CANDS];
static int s_cand_n;

typedef struct {
    const ExecCtx        *ctx;
    const RecruitRequest *req;
    long                  wallet;      // possibly relaxed (probe mode)
    int                   k_max;       // possibly relaxed
    bool                  probe_mode;  // existence, not cost
} SearchArgs;

static int raise_spell_cost(void) {
    int idx = spell_index_by_adventure_effect(ADV_EFFECT_RAISE_CONTROL);
    const SpellDef *sd = idx >= 0 ? spell_by_index(idx) : NULL;
    return sd ? sd->cost : 0;
}

static int kit_spell_cost(int spell_idx) {
    const SpellDef *sd = spell_by_index(spell_idx);
    return sd ? sd->cost : 0;
}

// Can the approach to an off-zone castle fight ride a ZERO-DAY castle gate
// (an owned destination in the fight's zone, charges castable or buyable)?
// Then no week boundary resets the pre-cast lift and no gate re-arm premium
// applies to the plan's price.
static const char *town_selling_spell(const Game *g, int spell_idx);
static bool fight_zone_gate_ready(const ExecCtx *ctx, const char *fight_zone) {
    const Game *g = ctx->g;
    if (!g->stats.knows_magic) return false;
    int cg = gate_spell_index(false);
    if (cg < 0) return false;
    // Castable now, or buyable -- the siege approach PRE-STOCKS the castle
    // gates before an off-zone crossing (siege_or_slay), so a buyable book
    // really does become a zero-day approach.
    bool charges_ok = spell_charges(g, cg) >= GATE_LAW_MIN_CHARGES;
    if (!charges_ok) {
        const SpellDef *sd = spell_by_index(cg);
        charges_ok = sd && town_selling_spell(g, cg) &&
                     g->stats.gold >= 2 * sd->cost;
    }
    if (!charges_ok) return false;
    GateDestination dests[GAME_GATE_DESTS_MAX];
    int n = GameGateDestinations((Game *)g, GATE_DEST_CASTLE, dests,
                                 GAME_GATE_DESTS_MAX);
    for (int i = 0; i < n; i++)
        if (strcmp(dests[i].zone, fight_zone) == 0) return true;
    return false;
}

// Achievable raise casts (AP-132): held charges + wallet-affordable ones,
// capped at RAISE_K_MAX. Casting needs knows_magic (play-legality: the shell
// gates the spellbook on it, REQ-323). Cycling buy->cast->buy evades the
// book cap.
static int achievable_k(const Game *g, long wallet) {
    if (!g->stats.knows_magic) return 0;
    int idx = spell_index_by_adventure_effect(ADV_EFFECT_RAISE_CONTROL);
    if (idx < 0) return 0;
    int held = g->spells.counts[idx];
    int cost = raise_spell_cost();
    long afford = cost > 0 ? wallet / cost : 0;
    long k = held + afford;
    if (k > RAISE_K_MAX) k = RAISE_K_MAX;
    return (int)k;
}

// Build one candidate (pure arithmetic -- no engine call).
static void build_candidate(const SearchArgs *a, int gi, int order,
                            int kit_spell, int kit_charges, int k) {
    if (s_cand_n >= MAX_CANDS) {
        watchdog_hit("cand-arena");   // saturated tail can hide winners
        return;
    }
    const Game *g = a->ctx->g;
    const PlanGroup *pg = &s_groups[gi];
    Candidate *c = &s_cands[s_cand_n];
    memset(c, 0, sizeof *c);
    c->group = gi;
    c->order = order;
    c->kit_spell = kit_spell;
    c->kit_charges = kit_charges;
    c->k = k;

    // Size against the SUSTAINABLE leadership (AP-132): base + the plan's own
    // k casts re-armed at the gate. leadership_current can carry a transient
    // lift from an earlier fight that a week boundary erases mid-realize --
    // sizing on it builds armies that collapse out of control before the
    // fight.
    int lead = g->stats.leadership_base + k * GameRaiseControlAmount(g);
    long gold = 0;
    unsigned zmask = 0;
    for (int s = 0; s < pg->n; s++) {
        int ti = order ? pg->troops[pg->n - 1 - s] : pg->troops[s];
        const TroopDef *t = troop_by_index(ti);
        if (!t || t->hit_points <= 0) continue;
        int cap = lead / t->hit_points;         // per-stack control (REQ-272)
        if (cap <= 0) continue;
        int got = quote_fill(a->ctx, ti, cap, &gold, &zmask);
        if (got <= 0) continue;
        c->troops[c->n] = ti;
        c->counts[c->n] = got;
        c->hp_worth += (long)got * t->hit_points;
        c->n++;
    }
    if (c->n == 0) return;

    // Charge purchases priced into the plan (AP-070): the raise shortfall and
    // the kit book's full combat-charge shortfall at pack prices.
    {
        int idx = spell_index_by_adventure_effect(ADV_EFFECT_RAISE_CONTROL);
        int held = idx >= 0 ? g->spells.counts[idx] : 0;
        int short_k = k - held;
        if (short_k > 0) gold += (long)short_k * raise_spell_cost();
        // An OFF-ZONE castle fight whose approach must SAIL re-arms the
        // lift AT THE GATE (AP-132): the crossing's week boundary resets
        // the pre-cast lift, so the full k charges are bought twice.
        // Pricing it lets the R-B order prefer deliverable shapes instead
        // of paper winners the deliverability check rejects. A zero-day
        // castle-gate approach into the fight's zone keeps the lift -- no
        // premium there.
        if (k > 0 && a->req->combat_mode == COMBAT_MODE_CASTLE &&
            a->req->seed_key) {
            const ResCastle *rc3 =
                resources_castle_by_id(a->ctx->res, a->req->seed_key);
            if (rc3 && strcmp(rc3->zone, g->position.zone) != 0 &&
                !fight_zone_gate_ready(a->ctx, rc3->zone))
                gold += (long)k * raise_spell_cost();
        }
    }
    if (kit_spell >= 0 && kit_charges > 0) {
        int held = g->spells.counts[kit_spell];
        int shortfall = kit_charges - held;
        if (shortfall > 0) gold += (long)shortfall * kit_spell_cost(kit_spell);
    }
    c->gold = gold;
    c->zones_mask = zmask;
    // R-B days key (plan_days_qt): one week quantum per DISTINCT off-zone zone
    // the plan's buys actually tour.
    int zones = 0;
    for (unsigned m = zmask; m; m >>= 1) zones += (int)(m & 1u);
    c->days = zones * a->ctx->res->time.week_days;
    c->realizable = (gold <= a->wallet);
    s_cand_n++;
}

// The R-B total order: days, then gold, then enumeration order (AP-131.2).
static int cand_cmp(const void *pa, const void *pb) {
    const Candidate *a = (const Candidate *)pa;
    const Candidate *b = (const Candidate *)pb;
    if (a->days != b->days) return a->days - b->days;
    if (a->gold != b->gold) return a->gold < b->gold ? -1 : 1;
    return (int)(a - b);
}

// The army AS THE BUYS WILL FIELD IT (AP-133): live stacks that are in the
// plan keep their slot positions (buys merge into them); new plan troops
// append in plan order. The projection and the realized army share one order.
static void cand_fielded_army(const Game *g, const Candidate *c,
                              ArmyStack out[GAME_ARMY_SLOTS]) {
    memset(out, 0, sizeof(ArmyStack) * GAME_ARMY_SLOTS);
    bool placed[GAME_ARMY_SLOTS] = { false };
    int n = 0;
    for (int s = 0; s < GAME_ARMY_SLOTS && n < GAME_ARMY_SLOTS; s++) {
        if (!g->army[s].id[0] || g->army[s].count <= 0) continue;
        for (int p = 0; p < c->n; p++) {
            const TroopDef *t = troop_by_index(c->troops[p]);
            if (!t || placed[p]) continue;
            if (strcmp(t->id, g->army[s].id) != 0) continue;
            snprintf(out[n].id, sizeof out[n].id, "%s", t->id);
            out[n].count = c->counts[p];
            placed[p] = true;
            n++;
            break;
        }
    }
    for (int p = 0; p < c->n && n < GAME_ARMY_SLOTS; p++) {
        if (placed[p]) continue;
        const TroopDef *t = troop_by_index(c->troops[p]);
        if (!t) continue;
        snprintf(out[n].id, sizeof out[n].id, "%s", t->id);
        out[n].count = c->counts[p];
        n++;
    }
}

// The one oracle (AP-133): simulate the candidate as it will be fielded.
static bool cand_sim(const SearchArgs *a, const Candidate *c) {
    const Game *g = a->ctx->g;
    ArmyStack army[GAME_ARMY_SLOTS];
    cand_fielded_army(g, c, army);
    int lead = g->stats.leadership_base + c->k * GameRaiseControlAmount(g);
    int book[GAME_SPELLBOOK_SLOTS];
    memcpy(book, g->spells.counts, sizeof book);
    if (c->kit_spell >= 0 && c->kit_spell < GAME_SPELLBOOK_SLOTS &&
        book[c->kit_spell] < c->kit_charges)
        book[c->kit_spell] = c->kit_charges;
    int grow_weeks = a->req->grow_weeks + c->days /
                     (a->ctx->res->time.week_days > 0
                          ? a->ctx->res->time.week_days : 5);
    return predict_combat_cached(a->ctx, a->req->combat_mode, a->req->seed_key,
                                 a->req->display_name, a->req->garrison, army,
                                 lead, book, grow_weeks, NULL);
}

// The funnel (AP-131): build -> fold -> dominate -> rung -> sim(+bisect).
// Returns the index of the winning candidate in s_cands, or -1.
// ---- recruit-search memoization (AP-208) ------------------------------------
// search_all is a pure, expensive query (up to seconds for a hard endgame
// villain) and the snapshot-tree search re-descends through the same fights.
// Cache its result keyed on a COMPLETE fingerprint of everything the search
// reads: the hero force + economy, the recruit sources (dwellings, castle
// garrisons, the working zone, discovered continents), the class/seed, the
// search args, and the realize-failure exclusions. NOT the map, calendar, or
// exact (x,y): the search is map-free, calendar-free (budget independence), and
// prices sources by zone -- leaving those out keeps the key from being
// invalidated by unrelated progress. Enabled only around the search's own
// expansions (recruit_cache_enable), so other callers never touch it.
#define RCACHE_N (1 << 14)
typedef struct { uint64_t key; bool used, found; Candidate winner; } RCacheEntry;
static RCacheEntry s_rcache[RCACHE_N];
static bool s_rcache_on;
static long s_rcache_hits, s_rcache_misses;

void recruit_cache_enable(bool on) { s_rcache_on = on; }
void recruit_cache_reset(void) {
    memset(s_rcache, 0, sizeof s_rcache);
    s_rcache_hits = s_rcache_misses = 0;
}
void recruit_cache_stats(long *hits, long *misses) {
    if (hits) *hits = s_rcache_hits;
    if (misses) *misses = s_rcache_misses;
}

static uint64_t rfnv(uint64_t h, const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    while (n--) { h ^= *b++; h *= 1099511628211ULL; }
    return h;
}
static uint64_t recruit_fp(const SearchArgs *a) {
    const Game *g = a->ctx->g;
    uint64_t h = 1469598103934665603ULL;
    for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
        h = rfnv(h, g->army[s].id, sizeof g->army[s].id);
        h = rfnv(h, &g->army[s].count, sizeof g->army[s].count);
    }
    h = rfnv(h, &g->stats.gold, sizeof g->stats.gold);
    h = rfnv(h, &g->stats.leadership_base, sizeof g->stats.leadership_base);
    h = rfnv(h, &g->stats.leadership_current, sizeof g->stats.leadership_current);
    h = rfnv(h, &g->stats.spell_power, sizeof g->stats.spell_power);
    h = rfnv(h, &g->stats.max_spells, sizeof g->stats.max_spells);
    h = rfnv(h, &g->stats.knows_magic, sizeof g->stats.knows_magic);
    h = rfnv(h, &g->stats.siege_weapons, sizeof g->stats.siege_weapons);
    h = rfnv(h, g->spells.counts, sizeof g->spells.counts);
    h = rfnv(h, &g->dwelling_count, sizeof g->dwelling_count);
    for (int i = 0; i < g->dwelling_count; i++) {
        h = rfnv(h, g->dwellings[i].troop_id, sizeof g->dwellings[i].troop_id);
        h = rfnv(h, &g->dwellings[i].count, sizeof g->dwellings[i].count);
        h = rfnv(h, g->dwellings[i].zone, sizeof g->dwellings[i].zone);
    }
    for (int i = 0; i < GAME_CASTLES; i++) {
        h = rfnv(h, &g->castles[i].owner_kind, sizeof g->castles[i].owner_kind);
        h = rfnv(h, g->castles[i].villain_id, sizeof g->castles[i].villain_id);
        for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
            h = rfnv(h, g->castles[i].garrison[s].id,
                     sizeof g->castles[i].garrison[s].id);
            h = rfnv(h, &g->castles[i].garrison[s].count,
                     sizeof g->castles[i].garrison[s].count);
        }
    }
    h = rfnv(h, g->position.zone, sizeof g->position.zone);
    h = rfnv(h, g->world.zones_discovered, sizeof g->world.zones_discovered);
    h = rfnv(h, &g->character, sizeof g->character);
    h = rfnv(h, &g->seed, sizeof g->seed);
    h = rfnv(h, &a->wallet, sizeof a->wallet);
    h = rfnv(h, &a->k_max, sizeof a->k_max);
    h = rfnv(h, &a->probe_mode, sizeof a->probe_mode);
    h = rfnv(h, &a->req->mode, sizeof a->req->mode);
    h = rfnv(h, &a->req->combat_mode, sizeof a->req->combat_mode);
    if (a->req->seed_key)
        h = rfnv(h, a->req->seed_key, strlen(a->req->seed_key) + 1);
    if (a->req->garrison)
        for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
            h = rfnv(h, a->req->garrison[s].id, sizeof a->req->garrison[s].id);
            h = rfnv(h, &a->req->garrison[s].count,
                     sizeof a->req->garrison[s].count);
        }
    h = rfnv(h, &a->req->grow_weeks, sizeof a->req->grow_weeks);
    h = rfnv(h, &s_excl_n, sizeof s_excl_n);
    h = rfnv(h, s_excl, sizeof(s_excl[0]) * (size_t)s_excl_n);
    return h;
}

static int search_all_inner(const SearchArgs *a);

// Memoizing wrapper: a complete-key cache lookup, else the real search (whose
// winner is copied into the cache and re-served from arena slot 0 on a hit).
static int search_all(const SearchArgs *a) {
    if (!s_rcache_on) return search_all_inner(a);
    uint64_t key = recruit_fp(a);
    RCacheEntry *e = &s_rcache[key & (RCACHE_N - 1)];
    if (e->used && e->key == key) {
        s_rcache_hits++;
        if (!e->found) return -1;
        s_cands[0] = e->winner;
        s_cand_n = 1;
        return 0;
    }
    s_rcache_misses++;
    int win = search_all_inner(a);
    e->key = key;
    e->used = true;
    e->found = (win >= 0);
    if (win >= 0) e->winner = s_cands[win];
    return win;
}

static int search_all_inner(const SearchArgs *a) {
    const Game *g = a->ctx->g;
    recruit_sources_enumerate(a->ctx);
    quotes_build(a->ctx, false);
    plan_build();
    s_cand_n = 0;

    int kit_list[GAME_SPELLBOOK_SLOTS + 1];   // every combat spell + spell-less
    int kit_n = 0;
    kit_list[kit_n++] = -1;   // spell-less
    if (g->stats.knows_magic) {
        // Every castable combat spell is a kit axis (AP-180).
        for (int si = 0; si < spells_count(); si++) {
            const SpellDef *sd = spell_by_index(si);
            if (sd && sd->kind == SPELL_KIND_COMBAT &&
                kit_n < (int)(sizeof kit_list / sizeof kit_list[0]))
                kit_list[kit_n++] = sd->index;
        }
    }
    int charge_steps[4] = { 1, 2, 4, 8 };
    int kmax = achievable_k(g, a->wallet);
    if (a->k_max > kmax) kmax = a->k_max;

    // BUILD: the flat enumeration, folded per cell (AP-131.1).
    for (int gi = 0; gi < s_group_n; gi++) {
        // The arena never unfills once full (a fold needs a successful build,
        // which can't happen at the cap), so the enumeration tail is pure spin
        // -- stop instead of walking it (the top cand-arena watchdog cost).
        if (s_cand_n >= MAX_CANDS) break;
        if (group_excluded(a->req->seed_key, &s_groups[gi])) continue;
        for (int order = 0; order < 2; order++) {
            for (int ki = 0; ki < kit_n; ki++) {
                int kit = kit_list[ki];
                int cn = kit < 0 ? 1 : 4;
                for (int ci = 0; ci < cn; ci++) {
                    int charges = kit < 0 ? 0 : charge_steps[ci];
                    long last_hp = -1;
                    for (int k = 0; k <= kmax; k++) {
                        if (s_cand_n >= MAX_CANDS) break;
                        int before = s_cand_n;
                        build_candidate(a, gi, order, kit, charges, k);
                        if (s_cand_n == before) continue;
                        Candidate *c = &s_cands[s_cand_n - 1];
                        if (last_hp >= 0 &&
                            c->hp_worth - last_hp <
                                (last_hp / SIM_FOLD_DEN)) {
                            // FOLD: keep the cheapest member (the earlier one).
                            s_cand_n--;
                            continue;
                        }
                        last_hp = c->hp_worth;
                    }
                }
            }
        }
    }
    int universe = s_cand_n;

    // Drop candidates over the search's budget (a winner at the wallet that
    // dies at the fieldable budget is the ONE silent drop -- logged, AP-171).
    // Probe wallets apply too: a funded-wait bisect probes existence AT a
    // budget, and the gold-relax probe passes an unbounded wallet explicitly.
    {
        // The drop is reported ONCE per search, not once per candidate: a
        // single fight can price thousands of over-budget stacks, and a line
        // each buries every other channel (measured: 3.3M of 3.3M verbose
        // lines on one seed). The tally plus the CHEAPEST dropped stack is
        // what diagnosis needs -- how many missed, and by how much.
        bool log_drops = !a->probe_mode && ob_diag_verbose();
        int  kept = 0;
        long drop_n = 0, drop_gold = 0, drop_hp = 0;
        int  drop_days = 0;
        for (int i = 0; i < s_cand_n; i++) {
            if (s_cands[i].realizable) {
                s_cands[kept++] = s_cands[i];
            } else if (log_drops) {
                if (drop_n == 0 || s_cands[i].gold < drop_gold) {
                    drop_gold = s_cands[i].gold;
                    drop_days = s_cands[i].days;
                    drop_hp = s_cands[i].hp_worth;
                }
                drop_n++;
            }
        }
        s_cand_n = kept;
        if (drop_n > 0)
            printf("[RECRUIT] unrealizable drop: n=%ld cheapest gold=%ld "
                   "days=%d hp=%ld\n", drop_n, drop_gold, drop_days, drop_hp);
    }
    int budget_kept = s_cand_n;
    if (s_cand_n == 0) return -1;

    // DOMINATE (AP-131.2): sort by the R-B order; drop candidates no stronger
    // than one already kept at <= cost with the same kit and >= charges.
    qsort(s_cands, (size_t)s_cand_n, sizeof s_cands[0], cand_cmp);
    {
        int kept = 0;
        for (int i = 0; i < s_cand_n; i++) {
            bool dominated = false;
            for (int j = 0; j < kept && !dominated; j++) {
                const Candidate *p = &s_cands[j];
                const Candidate *c = &s_cands[i];
                if (p->days <= c->days && p->gold <= c->gold &&
                    p->kit_spell == c->kit_spell &&
                    p->kit_charges >= c->kit_charges &&
                    p->hp_worth >= c->hp_worth)
                    dominated = true;
            }
            if (!dominated) s_cands[kept++] = s_cands[i];
        }
        s_cand_n = kept;
    }
    int pareto = s_cand_n;

    // RUNG (AP-131.3): per days-class the cheapest, the strongest, and
    // gold-even rungs between -- at most SIM_RUNGS total. The whole budget is
    // spent: sparse rungs miss winners that sit between them and misreport
    // a reachable fight as no-winner.
    static int rungs[SIM_RUNGS];
    int rung_n = 0;
    {
        int class_start[8], class_end[8], classes = 0;
        int i = 0;
        while (i < s_cand_n && classes < 8) {
            int j = i;
            while (j < s_cand_n && s_cands[j].days == s_cands[i].days) j++;
            class_start[classes] = i;
            class_end[classes] = j;
            classes++;
            i = j;
        }
        int per_class = classes > 0 ? SIM_RUNGS / classes : SIM_RUNGS;
        if (per_class < 2) per_class = 2;
        for (int c2 = 0; c2 < classes && rung_n < SIM_RUNGS; c2++) {
            int lo = class_start[c2], hi = class_end[c2];
            int n2 = hi - lo;
            // cheapest
            rungs[rung_n++] = lo;
            if (n2 <= 1 || rung_n >= SIM_RUNGS) continue;
            // strongest in class
            int best = lo;
            for (int t = lo; t < hi; t++)
                if (s_cands[t].hp_worth > s_cands[best].hp_worth) best = t;
            if (best != lo) rungs[rung_n++] = best;
            // gold-even rungs between
            int extra = per_class - 2;
            for (int e2 = 1; e2 <= extra && rung_n < SIM_RUNGS; e2++) {
                int pick = lo + (n2 * e2) / (extra + 1);
                if (pick == lo || pick == best) continue;
                rungs[rung_n++] = pick;
            }
        }
    }
    if (ob_diag_verbose())
        printf("[SIM-STACK] universe=%d kept=%d pareto=%d rungs=%d\n",
               universe, budget_kept, pareto, rung_n);

    // SIM + REFINE (AP-131.4).
    if (a->probe_mode) {
        // Existence: strongest-first, first win stops.
        for (int r = rung_n - 1; r >= 0; r--) {
            if (cand_sim(a, &s_cands[rungs[r]])) return rungs[r];
        }
        return -1;
    }
    int first_win = -1, last_loss = -1;
    for (int r = 0; r < rung_n; r++) {
        if (ob_diag_verbose())
            printf("[SIM-RUNG] idx=%d days=%d gold=%ld hp=%ld kit=%d k=%d\n",
                   rungs[r], s_cands[rungs[r]].days, s_cands[rungs[r]].gold,
                   s_cands[rungs[r]].hp_worth, s_cands[rungs[r]].kit_spell,
                   s_cands[rungs[r]].k);
        if (cand_sim(a, &s_cands[rungs[r]])) {
            first_win = rungs[r];
            break;
        }
        last_loss = rungs[r];
    }
    if (first_win < 0) return -1;
    // Bisect the frontier gap between the last losing rung and the winner.
    if (last_loss >= 0 && first_win > last_loss + 1) {
        int lo = last_loss, hi = first_win;
        while (hi - lo > 1) {
            int mid = lo + (hi - lo) / 2;
            if (cand_sim(a, &s_cands[mid])) hi = mid;
            else lo = mid;
        }
        first_win = hi;
    }
    return first_win;
}

// ---- probes (AP-134) ---------------------------------------------------------------

// Total buyable stock across the world's weekly-restocking dwellings (the only
// sources a wait can grow; instant-army/home-castle are unbounded sentinels and
// garrisons do not restock). THE STOCK WAIT compares this week over week: when
// it stops rising, every dwelling has hit its ceiling and more waiting is
// futile. Reads restock state, never days_left.
static long dwelling_stock_total(const ExecCtx *ctx) {
    recruit_sources_enumerate(ctx);
    long total = 0;
    for (int si = 0; si < s_source_n; si++)
        if (s_sources[si].kind == RSRC_INZONE_DWELLING ||
            s_sources[si].kind == RSRC_OFFZONE_DWELLING)
            total += s_sources[si].avail;
    return total;
}

// Existence probe with the dwelling stocks lifted to their restock ceiling
// (max_population) and an unbounded wallet: TRUE means the fight is winnable
// once the world's weekly restock refills the sources -- the probe the
// probe-cause path and THE STOCK WAIT share.
static bool stock_relaxed_wins(ExecCtx *ctx, const RecruitRequest *req) {
    recruit_sources_enumerate(ctx);
    quotes_build(ctx, true);
    plan_build();
    s_cand_n = 0;
    SearchArgs a2 = { ctx, req, INT_MAX / 4, 0, true };
    int kmax = achievable_k(ctx->g, a2.wallet);
    for (int gi = 0; gi < s_group_n; gi++)
        for (int order = 0; order < 2; order++)
            build_candidate(&a2, gi, order, -1, 0, kmax);
    for (int i = s_cand_n - 1; i >= 0; i--)
        if (cand_sim(&a2, &s_cands[i])) return true;
    return false;
}

static ExecCause rfw_probe_cause(ExecCtx *ctx, const RecruitRequest *req) {
    SearchArgs a = { ctx, req, ctx->g->stats.gold, 0, true };
    // relax gold
    a.wallet = INT_MAX / 4;
    int gwin = search_all(&a);
    if (ob_diag_verbose())
        printf("[RECRUIT] probe %s: gold-relax=%s (k_cap=%d lead=%d sp=%d "
               "magic=%d)\n",
               req->seed_key ? req->seed_key : "?", gwin >= 0 ? "WIN" : "no",
               achievable_k(ctx->g, INT_MAX / 4),
               ctx->g->stats.leadership_current, ctx->g->stats.spell_power,
               (int)ctx->g->stats.knows_magic);
    if (gwin >= 0) return EXEC_CAUSE_GOLD;
    // relax stock (sources lifted to max_population)
    if (stock_relaxed_wins(ctx, req)) return EXEC_CAUSE_STOCK;
    // relax leadership
    a.wallet = INT_MAX / 4;
    a.k_max = RAISE_K_MAX;
    if (search_all(&a) >= 0) return EXEC_CAUSE_LEADERSHIP;
    return EXEC_CAUSE_NO_WINNING_ARMY;
}

// ---- realize (AP-137, AP-140..142) ---------------------------------------------------

// The approach reserve (AP-141): gold the realize must NOT consume -- an
// off-zone fight's transport (the siege pre-stock's gate charges) and, with
// no zero-day approach at all, the at-gate raise re-arm. A realize that
// spends the whole wallet strands a committed winner at a sail crossing.
// (s_realize_reserve is declared up top so recruit_exclusions_reset can clear
// it -- see that function.)

static const char *town_selling_spell(const Game *g, int spell_idx) {
    const SpellDef *sd = spell_by_index(spell_idx);
    if (!sd) return NULL;
    for (int i = 0; i < GAME_TOWNS; i++) {
        if (!g->towns[i].id[0]) continue;
        if (strcmp(g->towns[i].spell_for_sale, sd->id) == 0)
            return g->towns[i].id;
    }
    return NULL;
}

// Buy `want` charges of spell_idx (travels to the seller). Cycles casts of
// raise_control when the book is full and casting is the point (cycling
// re-arm, AP-132).
static bool stock_spell_charges(ExecCtx *ctx, int spell_idx, int want,
                                bool cast_excess_raise) {
    Game *g = ctx->g;
    if (spell_charges(g, spell_idx) >= want) return true;
    const char *tid = town_selling_spell(g, spell_idx);
    if (!tid) return false;
    const ResTown *rt = resources_town_by_id(ctx->res, tid);
    if (!rt) return false;
    NavPoint tp = { zone_index_of(ctx->res, rt->zone), rt->x, rt->y };
    ExecCause cc;
    int rm_before = g->stats.days_left;
    long rm_cross = ledger_committed(DAY_ACCT_CROSSING);
    int rm_r = move_to(ctx, &tp, 1, true, NULL, &cc);
    ledger_book_move(DAY_ACCT_RECRUITMOVE, rm_before, g->stats.days_left,
                     rm_cross);
    if (rm_r < 0) return false;
    exec_answer_pending(ctx, true);
    // The realize's approach reserve binds every purchase EXCEPT the raise
    // spell itself (the lift is the reserve's beneficiary): a kit or
    // transport stock that spends into the reserve strands the committed
    // winner at the gate un-liftable.
    bool is_raise =
        spell_idx == spell_index_by_adventure_effect(ADV_EFFECT_RAISE_CONTROL);
    const SpellDef *tsd = spell_by_index(spell_idx);
    int guard = 64;
    while (spell_charges(g, spell_idx) < want && guard-- > 0) {
        if (!is_raise && tsd && s_realize_reserve > 0 &&
            g->stats.gold - tsd->cost < s_realize_reserve)
            break;
        if (exec_buy_spell_at(ctx, tid)) continue;
        // Book at cap (the buy is refused, not clamped): free a slot with
        // harmless cast-offs -- raise (leadership persists), instant army,
        // a live time-stop window -- then the cheapest combat charge that
        // is not the one being stocked.
        if (GameKnownSpells(g) >= g->stats.max_spells) {
            if (cast_excess_raise && exec_cast_raise(ctx)) continue;
            int ia = spell_index_by_adventure_effect(ADV_EFFECT_INSTANT_ARMY);
            if (ia >= 0 && ia != spell_idx && g->spells.counts[ia] > 0) {
                const SpellDef *ia_sd = spell_by_index(ia);
                int cast_mark = recsink_mark();
                rec_push_action(g, RA_CAST_ADV_SPELL, ia_sd->id, 0, 0);
                if (GameApplyAdventureSpellEffect(g, ia)) {
                    exec_pump_passive(ctx);
                    continue;
                }
                recsink_rollback(cast_mark);
            }
            int ts2 = spell_index_by_adventure_effect(ADV_EFFECT_TIME_STOP);
            if (ts2 >= 0 && ts2 != spell_idx && g->spells.counts[ts2] > 0) {
                const SpellDef *ts_sd = spell_by_index(ts2);
                rec_push_action(g, RA_CAST_ADV_SPELL, ts_sd->id, 0, 0);
                GameCastTimeStop(g);
                continue;
            }
            int cheapest = -1, ccost = 0;
            for (int si = 0; si < spells_count(); si++) {
                const SpellDef *sd2 = spell_by_index(si);
                if (!sd2 || sd2->kind != SPELL_KIND_COMBAT) continue;
                if (sd2->index == spell_idx) continue;
                if (g->spells.counts[sd2->index] <= 0) continue;
                if (cheapest < 0 || sd2->cost < ccost) {
                    cheapest = sd2->index;
                    ccost = sd2->cost;
                }
            }
            if (cheapest >= 0 && exec_discard_spell(ctx, cheapest)) continue;
            if (exec_discard_junk_spell(ctx, spell_idx)) continue;
            if (!cast_excess_raise && exec_cast_raise(ctx)) continue;
        }
        break;
    }
    return spell_charges(g, spell_idx) >= want;
}

// Stock the kit book to its absolute per-index target (AP-141).
static bool stock_combat_spells(ExecCtx *ctx, int kit_spell, int kit_target) {
    if (kit_spell < 0 || kit_target <= 0) return true;
    return stock_spell_charges(ctx, kit_spell, kit_target, false);
}

// Planner-facing charge stocking (the Time Stop logistics trip).
bool exec_stock_spell_charges_public(ExecCtx *ctx, int spell_idx, int want) {
    return stock_spell_charges(ctx, spell_idx, want, false);
}

// Bank or dismiss displaced stacks (preservation before dismissal, AP-141).
static void bank_or_dismiss(ExecCtx *ctx, const Candidate *c) {
    Game *g = ctx->g;
    for (int s = GAME_ARMY_SLOTS - 1; s >= 0; s--) {
        if (!g->army[s].id[0] || g->army[s].count <= 0) continue;
        bool keep = false;
        for (int i = 0; i < c->n; i++) {
            const TroopDef *t = troop_by_index(c->troops[i]);
            if (t && strcmp(t->id, g->army[s].id) == 0) keep = true;
        }
        if (keep) continue;
        // Preservation: garrison at the own castle we are standing at.
        if (g->position.own_castle[0] &&
            exec_garrison_slot(ctx, g->position.own_castle, s))
            continue;
        exec_dismiss_slot(ctx, s);
    }
    GameCompactArmy(g);
}

// Buy the maximal stack of troop `ti` across every source in greedy
// nearest-next order (plan_buy_troop, AP-141). Returns units acquired.
static int plan_buy_troop(ExecCtx *ctx, int ti, int want) {
    Game *g = ctx->g;
    const TroopDef *t = troop_by_index(ti);
    if (!t) return 0;
    int held = 0;
    for (int s = 0; s < GAME_ARMY_SLOTS; s++)
        if (strcmp(g->army[s].id, t->id) == 0) held += g->army[s].count;
    // Sources that refused this call (unreachable / cast-starved) are skipped
    // on retry so the buy FALLS THROUGH to the next source in preference
    // order instead of abandoning the slot.
    struct { int kind, x, y; } failed[MAX_SOURCES];
    int failed_n = 0;
    // Watchdog, not a semantic bound: bulk plans legitimately sweep dozens
    // of small dwellings (militia stocks are ~100/dwelling), so the guard
    // scales with the ask; the no-progress checks below do the real gating.
    int guard = 16 + want / 16;
    int lift_trips = 0;   // seller round-trips for the arrival re-lift
    while (held < want && guard-- > 0) {
        recruit_sources_enumerate(ctx);
        // nearest-next: in-zone dwellings and garrisons first (source order
        // is the buy preference; within it, the mover picks the cheapest).
        int best = -1;
        for (int si = 0; si < s_source_n; si++) {
            if (s_sources[si].troop_idx != ti) continue;
            if (s_sources[si].avail <= 0) continue;
            bool skip = false;
            for (int f2 = 0; f2 < failed_n; f2++)
                if (failed[f2].kind == (int)s_sources[si].kind &&
                    failed[f2].x == s_sources[si].x &&
                    failed[f2].y == s_sources[si].y)
                    skip = true;
            if (skip) continue;
            best = si;
            break;
        }
        if (best < 0) {
            if (ob_diag_verbose())
                printf("[RECRUIT] buy %s: no source (held=%d want=%d)\n",
                       t->id, held, want);
            break;
        }
        const RecruitSource *src = &s_sources[best];
        int room = GameMaxRecruitable(g, t->id);
        if (room <= 0) {
            // Control-full RIGHT NOW -- but a lift can still open room: held
            // raise charges cast at the source (the arrival branch), or a
            // seller trip for more (the ratchet). Only when neither remains
            // is this truly control-bound. Casting HERE would waste the lift
            // on cross-zone plans: the transit's week boundary resets it.
            int ri0 = spell_index_by_adventure_effect(
                ADV_EFFECT_RAISE_CONTROL);
            bool lift_possible = g->stats.knows_magic && ri0 >= 0 &&
                                 (g->spells.counts[ri0] > 0 ||
                                  lift_trips < 16);
            if (!lift_possible) {
                if (ob_diag_verbose())
                    printf("[RECRUIT] buy %s: control-bound (lead=%d held=%d "
                           "want=%d)\n", t->id,
                           g->stats.leadership_current, held, want);
                break;   // control-bound: no lift left to open room
            }
        }
        int take = want - held;
        if (room > 0 && take > room) take = room;
        if (take > src->avail) take = src->avail;
        if (src->unit_cost > 0) {
            int spendable = g->stats.gold - s_realize_reserve;
            if (spendable < 0) spendable = 0;
            int afford = spendable / src->unit_cost;
            if (take > afford) take = afford;
            if (take <= 0) {
                if (ob_diag_verbose())
                    printf("[RECRUIT] buy %s: gold-bound (gold=%d cost=%d "
                           "reserve=%d)\n", t->id, g->stats.gold,
                           src->unit_cost, s_realize_reserve);
                break;   // gold-bound
            }
        }

        if (src->kind == RSRC_INSTANT_ARMY) {
            // Cycle buy -> cast -> buy: the book cannot hold the whole cast
            // stock at once (the same cycling the raise re-arm uses).
            int ia = spell_index_by_adventure_effect(ADV_EFFECT_INSTANT_ARMY);
            bool progressed2 = false;
            if (ia >= 0) {
                int per = GameInstantArmyPerCast(g);
                int casts = (take + per - 1) / per;
                int cguard = casts + 8;
                while (casts > 0 && cguard-- > 0) {
                    if (g->spells.counts[ia] <= 0) {
                        int room3 = g->stats.max_spells - GameKnownSpells(g);
                        int batch = room3 < casts ? room3 : casts;
                        if (batch < 1 ||
                            !stock_spell_charges(ctx, ia, batch, false))
                            break;
                    }
                    const SpellDef *cyc_sd = spell_by_index(ia);
                    while (casts > 0 && g->spells.counts[ia] > 0) {
                        rec_push_action(g, RA_CAST_ADV_SPELL,
                                        cyc_sd->id, 0, 0);
                        if (!GameApplyAdventureSpellEffect(g, ia)) {
                            casts = 0;
                            break;
                        }
                        progressed2 = true;
                        casts--;
                        exec_pump_passive(ctx);
                    }
                }
            }
            if (!progressed2 && failed_n < MAX_SOURCES) {
                failed[failed_n].kind = (int)src->kind;
                failed[failed_n].x = src->x;
                failed[failed_n].y = src->y;
                failed_n++;
                continue;   // fall through to the next source
            }
        } else if (src->kind == RSRC_GARRISON) {
            NavPoint tp = { src->zone_index, src->x, src->y };
            ExecCause cc;
            int rm_before = g->stats.days_left;
            long rm_cross = ledger_committed(DAY_ACCT_CROSSING);
            int rm_r = move_to(ctx, &tp, 1, true, NULL, &cc);
            ledger_book_move(DAY_ACCT_RECRUITMOVE, rm_before,
                             g->stats.days_left, rm_cross);
            if (rm_r < 0) {
                if (ob_diag_verbose())
                    printf("[RECRUIT] buy %s: source unreachable kind=%d "
                           "dst=(%d,%d,z%d) cause=%s\n", t->id,
                           (int)src->kind, tp.x, tp.y, tp.zone_index,
                           exec_cause_name(cc));
                if (failed_n < MAX_SOURCES) {
                    failed[failed_n].kind = (int)src->kind;
                    failed[failed_n].x = src->x;
                    failed[failed_n].y = src->y;
                    failed_n++;
                    continue;   // fall through to the next source
                }
                break;
            }
            exec_answer_pending(ctx, true);
            const CastleRecord *cr = GameFindCastleConst(g, src->src_id);
            if (!cr || !g->position.own_castle[0]) {
                if (ob_diag_verbose())
                    printf("[RECRUIT] buy %s: garrison break own=%s\n",
                           t->id, g->position.own_castle);
                break;
            }
            for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
                if (strcmp(cr->garrison[s].id, t->id) == 0 &&
                    cr->garrison[s].count > 0) {
                    exec_ungarrison_slot(ctx, src->src_id, s);
                    break;
                }
            }
        } else if (src->kind == RSRC_HOME_CASTLE) {
            NavPoint tp = { src->zone_index, src->x, src->y };
            ExecCause cc;
            int rm_before = g->stats.days_left;
            long rm_cross = ledger_committed(DAY_ACCT_CROSSING);
            int rm_r = move_to(ctx, &tp, 1, true, NULL, &cc);
            ledger_book_move(DAY_ACCT_RECRUITMOVE, rm_before,
                             g->stats.days_left, rm_cross);
            if (rm_r < 0) {
                if (ob_diag_verbose())
                    printf("[RECRUIT] buy %s: source unreachable kind=%d "
                           "dst=(%d,%d,z%d) cause=%s\n", t->id,
                           (int)src->kind, tp.x, tp.y, tp.zone_index,
                           exec_cause_name(cc));
                if (failed_n < MAX_SOURCES) {
                    failed[failed_n].kind = (int)src->kind;
                    failed[failed_n].x = src->x;
                    failed[failed_n].y = src->y;
                    failed_n++;
                    continue;   // fall through to the next source
                }
                break;
            }
            exec_answer_pending(ctx, true);
            if (!g->position.home_castle[0]) {
                if (ob_diag_verbose())
                    printf("[RECRUIT] buy %s: home break pos=(%d,%d)\n",
                           t->id, g->position.x, g->position.y);
                break;
            }
            // The room quoted at the loop top is stale after travel: a week
            // boundary en route resets the raise lift and the engine REFUSES
            // (not clamps) an over-cap buy. Re-lift with held charges and
            // BUY THE INSTALLMENT the lift opens -- a k=14 plan can never
            // carry its whole lift in a capped book, so the buys ratchet:
            // cast, buy to the cap, trip for more charges only when the
            // room is truly zero, re-approach, repeat.
            {
                int room2 = GameMaxRecruitable(g, t->id);
                int ri2 = spell_index_by_adventure_effect(
                    ADV_EFFECT_RAISE_CONTROL);
                while (room2 < take && ri2 >= 0 &&
                       g->spells.counts[ri2] > 0) {
                    if (!exec_cast_raise(ctx)) break;
                    room2 = GameMaxRecruitable(g, t->id);
                }
                if (room2 <= 0 && ri2 >= 0 && lift_trips < 16) {
                    // Nothing buyable even after casting everything held:
                    // travel for a fresh lift kit, as full as the book
                    // affords -- every trip is two crossings of calendar.
                    // Refill the castle-gate book first when it is under
                    // the law floor: the partially-built army pays upkeep
                    // every week the ratchet travels, so zero-day crossings
                    // are what keep the plan solvent. The refill must
                    // room-force (the generic stocker) -- at a full book the
                    // plain gate stocker is refused, the trips fall back to
                    // sails, and every sail's week boundary resets the lift
                    // the trip just built.
                    int gi3 = gate_spell_index(false);
                    if (gi3 >= 0 &&
                        spell_charges(g, gi3) < 2 * GATE_LAW_MIN_CHARGES &&
                        g->stats.gold > AUTOPLAY_RICH_WALLET(ctx->res))
                        stock_spell_charges(ctx, gi3,
                                            4 * GATE_LAW_MIN_CHARGES, false);
                    int ws = g->stats.max_spells - GameKnownSpells(g) +
                             g->spells.counts[ri2] - 1;
                    if (ws < 2) ws = 2;
                    bool stocked = stock_spell_charges(ctx, ri2, ws, true);
                    if (!stocked && ws > 2)
                        stocked = stock_spell_charges(ctx, ri2, 2, true);
                    if (ob_diag_verbose())
                        printf("[RECRUIT] lift re-stock trip %d: %s "
                               "(held=%d room=%d take=%d lead=%d)\n",
                               lift_trips, stocked ? "ok" : "failed",
                               g->spells.counts[ri2], room2, take,
                               g->stats.leadership_current);
                    if (stocked) {
                        lift_trips++;
                        continue;   // re-approach with the fresh lift kit
                    }
                }
                if (take > room2) take = room2;
                if (take <= 0) {
                    if (failed_n < MAX_SOURCES) {
                        failed[failed_n].kind = (int)src->kind;
                        failed[failed_n].x = src->x;
                        failed[failed_n].y = src->y;
                        failed_n++;
                        continue;
                    }
                    break;
                }
            }
            int buy_mk = recsink_mark();
            rec_push_action(g, RA_BUY_TROOP, t->id, take, 0);
            if (GameBuyTroop(g, t->id, take) != 0) {
                recsink_rollback(buy_mk);
                if (ob_diag_verbose())
                    printf("[RECRUIT] buy %s: GameBuyTroop refused take=%d\n",
                           t->id, take);
                break;
            }
        } else {
            // Dwelling: step onto the tile; answer the recruit flow with the
            // exact count (the flow clamps to the live caps and decrements
            // the dwelling stock -- the honest human path).
            NavPoint tp = { src->zone_index, src->x, src->y };
            ExecCause cc;
            int rm_before = g->stats.days_left;
            long rm_cross = ledger_committed(DAY_ACCT_CROSSING);
            int rm_r = move_to(ctx, &tp, 1, true, NULL, &cc);
            ledger_book_move(DAY_ACCT_RECRUITMOVE, rm_before,
                             g->stats.days_left, rm_cross);
            if (rm_r < 0) {
                if (ob_diag_verbose())
                    printf("[RECRUIT] buy %s: source unreachable kind=%d "
                           "dst=(%d,%d,z%d) cause=%s\n", t->id,
                           (int)src->kind, tp.x, tp.y, tp.zone_index,
                           exec_cause_name(cc));
                if (failed_n < MAX_SOURCES) {
                    failed[failed_n].kind = (int)src->kind;
                    failed[failed_n].x = src->x;
                    failed[failed_n].y = src->y;
                    failed_n++;
                    continue;   // fall through to the next source
                }
                break;
            }
            if (pending_flow == FLOW_RECRUIT) {
                FlowAnswer ans = { FLOW_ANS_YES, take };
                rec_push_answer(g, FLOW_RECRUIT, ans, PLAYER_IO_COMBAT_NOT_RUN);
                PlayerIoPresentation pres;
                player_io_answer(g, ctx->map, ctx->fog, ctx->res, ans,
                                 PLAYER_IO_COMBAT_NOT_RUN, &pres);
                exec_pump_passive(ctx);
            } else if (!exec_answer_pending(ctx, true)) {
                if (ob_diag_verbose())
                    printf("[RECRUIT] buy %s: dwelling no-flow pos=(%d,%d) "
                           "dwell=%s\n", t->id, g->position.x, g->position.y,
                           g->position.dwelling_troop);
                break;
            }
        }

        int now = 0;
        for (int s = 0; s < GAME_ARMY_SLOTS; s++)
            if (strcmp(g->army[s].id, t->id) == 0) now += g->army[s].count;
        if (now <= held) {
            if (ob_diag_verbose())
                printf("[RECRUIT] buy %s: source refused (kind=%d take=%d "
                       "hero=(%d,%d,%s) in_town=%s home=%s own=%s dwell=%s "
                       "flow=%d)\n",
                       t->id, (int)src->kind, take, g->position.x,
                       g->position.y, g->position.zone,
                       g->position.in_town, g->position.home_castle,
                       g->position.own_castle, g->position.dwelling_troop,
                       (int)pending_flow);
            // An UNBOUNDED source refusing is a transient over-cap race (a
            // week boundary between the clamp and the buy resets the lift):
            // with lift capacity still available the ratchet recovers --
            // blacklisting the pool would end the plan mid-ratchet.
            if (src->kind == RSRC_HOME_CASTLE) {
                int rih = spell_index_by_adventure_effect(
                    ADV_EFFECT_RAISE_CONTROL);
                if (rih >= 0 && (g->spells.counts[rih] > 0 ||
                                 lift_trips < 16))
                    continue;
            }
            if (failed_n < MAX_SOURCES) {
                failed[failed_n].kind = (int)src->kind;
                failed[failed_n].x = src->x;
                failed[failed_n].y = src->y;
                failed_n++;
                continue;   // fall through to the next source
            }
            break;   // no progress: source refused
        }
        held = now;
    }
    return held;
}

// Realize the winner in place (AP-137): buy slot by slot; a slot that cannot
// fill in one trip banks held progress and spends a restock week (wait-allowed
// pass only), bounded by the calendar.
static bool realize_plan_body(ExecCtx *ctx, const Candidate *c,
                              const RecruitRequest *req,
                              ExecCause *out_cause) {
    Game *g = ctx->g;
    bank_or_dismiss(ctx, c);

    // The approach reserve for an off-zone castle fight carrying a lift:
    //  - castle gates castable NOW: nothing to fund (zero-day approach).
    //  - gates BUYABLE (the loose gate-ready promise): fund exactly the two
    //    charges the siege pre-stock will buy.
    //  - no zero-day approach at all: fund the at-gate raise re-arm (the
    //    same premium the candidate priced).
    s_realize_reserve = 0;
    if (g->stats.knows_magic && c->k > 0 &&
        req->combat_mode == COMBAT_MODE_CASTLE && req->seed_key) {
        const ResCastle *rcz = resources_castle_by_id(ctx->res, req->seed_key);
        // A crossing sits between the buys and the gate whenever the fight
        // is off-zone OR the buy tour leaves the zone (the approach then
        // crosses BACK) -- either way the week boundary threatens the lift.
        if (rcz && (strcmp(rcz->zone, g->position.zone) != 0 ||
                    c->zones_mask != 0)) {
            // Neither the gate charges nor the gate-ready promise survive
            // the realize's own touring -- fund the approach's two charges
            // AND the full at-gate raise re-arm unconditionally. (The PRICE
            // premium stays gate-ready-waived for candidate ordering; the
            // working capital does not gamble.)
            int cgz = gate_spell_index(false);
            const SpellDef *gsd = cgz >= 0 ? spell_by_index(cgz) : NULL;
            if (gsd) s_realize_reserve = 2 * gsd->cost;
            s_realize_reserve += c->k * raise_spell_cost();
        }
    }
    if (ob_diag_verbose())
        printf("[REALIZE] start gold=%d plan_gold=%ld reserve=%d\n",
               g->stats.gold, c->gold, s_realize_reserve);

    // Kit charges first (costliest decisive charges cannot be evicted by the
    // raise hoard, AP-141). A shortfall here is NOT fatal by itself: the
    // verdict sims the LIVE book, so a kit that stocked short either still
    // wins (commit is honest) or fails the verdict and rolls back -- the
    // sim is the gate, not the shelf count (an endgame book crowded with
    // undiscardable transport charges can cap a c=8 kit at 7).
    if (c->kit_spell >= 0 && c->kit_charges > 0)
        stock_combat_spells(ctx, c->kit_spell, c->kit_charges);
    if (ob_diag_verbose())
        printf("[REALIZE] post-kit gold=%d\n", g->stats.gold);

    // bank_raise_to_k (AP-132): a plan with k lifts must LIFT BEFORE BUYING --
    // the per-stack cap the buys are legal under is the LIFTED leadership.
    // Stock 2k charges when the book allows (k cast now for the buys, k kept
    // for the at-gate re-lift after any week reset), cycling buy -> cast ->
    // buy when the book cannot hold the whole stock at once.
    bool buys_over_base = false;
    for (int s2 = 0; s2 < c->n; s2++) {
        const TroopDef *t2 = troop_by_index(c->troops[s2]);
        if (!t2 || t2->hit_points <= 0) continue;
        if (c->counts[s2] > g->stats.leadership_base / t2->hit_points)
            buys_over_base = true;
    }
    if (c->k > 0 && buys_over_base) {
        int ri = spell_index_by_adventure_effect(ADV_EFFECT_RAISE_CONTROL);
        if (ri >= 0) {
            // Whether the lift can be pre-cast depends on where the buys
            // are: same-zone plans cast now (the lift survives to the
            // shops); a plan touring OFF-ZONE sources must CARRY the lift
            // as held charges instead -- every crossing is a week boundary
            // that resets leadership, and a charge cast before it is a
            // charge burned. The buy loop's arrival re-lift casts them at
            // each source (and its seller ratchet refills mid-plan).
            bool tours_off_zone =
                (c->zones_mask & ~(1u << (unsigned)hero_zone_index(ctx)))
                != 0;
            if (tours_off_zone) {
                stock_spell_charges(ctx, ri, 2 * c->k, false);
            } else {
                stock_spell_charges(ctx, ri, 2 * c->k, true);
                int lifted = 0;
                while (lifted < c->k &&
                       g->stats.leadership_current <
                           g->stats.leadership_base +
                               c->k * GameRaiseControlAmount(g)) {
                    if (g->spells.counts[ri] <= 0 &&
                        !stock_spell_charges(ctx, ri, 1, true))
                        break;
                    if (!exec_cast_raise(ctx)) break;
                    lifted++;
                }
                // Keep the gate's k charges topped where the book allows.
                if (g->spells.counts[ri] < c->k)
                    stock_spell_charges(ctx, ri, c->k, true);
            }
        }
    }

    if (ob_diag_verbose())
        printf("[REALIZE] post-lift gold=%d lead=%d\n", g->stats.gold,
               g->stats.leadership_current);

    for (int s = 0; s < c->n; s++) {
        int want = c->counts[s];
        int got = plan_buy_troop(ctx, c->troops[s], want);
        if (ob_diag_verbose()) {
            const TroopDef *td2 = troop_by_index(c->troops[s]);
            printf("[REALIZE] slot %d (%s): got=%d want=%d gold=%d\n",
                   s, td2 ? td2->id : "?", got, want, g->stats.gold);
        }
        int rounds = 0;
        while (got < want) {
            // Accept a shortfall the oracle still wins with; otherwise restock.
            Candidate probe = *c;
            probe.counts[s] = got;
            SearchArgs a = { ctx, req, g->stats.gold, 0, false };
            if (got > 0 && cand_sim(&a, &probe)) break;
            if (!exec_wait_allowed()) {
                if (out_cause) *out_cause = EXEC_CAUSE_STOCK;
                s_realize_stock_troop = c->troops[s];
                return false;
            }
            // Fixed safety backstop, calendar-free: exec_spend_week's own TIME
            // check fires first on every reachable path and the bought-nothing
            // exit below catches a stalled slot, so this cap only trips on an
            // unbounded slow-fill -- and it never varies with the day budget.
            if (++rounds > RECRUIT_RESTOCK_MAX_ROUNDS) {
                watchdog_hit("restock-weeks");
                if (out_cause) *out_cause = EXEC_CAUSE_STOCK;
                s_realize_stock_troop = c->troops[s];
                return false;
            }
            if (!exec_spend_week(ctx, DAY_ACCT_RECRUIT)) {
                if (out_cause) *out_cause = EXEC_CAUSE_TIME;
                return false;
            }
            int before = got;
            got = plan_buy_troop(ctx, c->troops[s], want);
            if (got <= before) {
                // A restock week that bought NOTHING proves waiting cannot
                // fill this slot (the sources are not restocking or not
                // reachable) -- the wait is no longer engine-bounded progress.
                if (out_cause) *out_cause = EXEC_CAUSE_STOCK;
                s_realize_stock_troop = c->troops[s];
                return false;
            }
            // No calendar look-ahead: keep restocking while the week buys
            // progress. The bought-nothing exit above and the engine's own TIME
            // failure (exec_spend_week) are the only stops, so the day budget never
            // changes how long this slot is pursued.
        }
    }

    // Raise stock for the at-gate lift, capped at what the BOOK CAN HOLD
    // (AP-141): buying past the cap cast-cycles the excess into a lift that
    // is already standing -- burning the re-arm money the at-gate recovery
    // needs. The held charges plus the preserved wallet ARE the re-arm.
    if (c->k > 0) {
        int idx = spell_index_by_adventure_effect(ADV_EFFECT_RAISE_CONTROL);
        if (idx >= 0 && g->spells.counts[idx] < c->k) {
            int room = g->stats.max_spells - GameKnownSpells(g) +
                       g->spells.counts[idx];
            int want = c->k < room ? c->k : room;
            if (want > g->spells.counts[idx])
                stock_spell_charges(ctx, idx, want, false);
        }
        // At-gate deliverability (AP-132): unless the lift is STANDING now --
        // and will SURVIVE the approach: a cross-zone fight's sail crosses a
        // week boundary that resets it -- the gate needs k casts from held
        // charges plus wallet-funded re-arm cycling. A k the book cannot
        // hold and the drained wallet cannot re-buy commits a paper winner
        // that loses its live verify -- fail the realize honestly instead
        // (GOLD: money re-opens it).
        bool lift_standing =
            g->stats.leadership_current >=
            g->stats.leadership_base + c->k * GameRaiseControlAmount(g);
        bool fight_off_zone = false;
        {
            const char *fz = NULL;
            if (req->combat_mode == COMBAT_MODE_CASTLE && req->seed_key) {
                const ResCastle *rc2 =
                    resources_castle_by_id(ctx->res, req->seed_key);
                if (rc2) fz = rc2->zone;
            }
            // A zero-day castle-gate approach keeps the standing lift --
            // only a sail-bound crossing needs the re-arm reserve.
            if (fz && strcmp(fz, g->position.zone) != 0 &&
                !fight_zone_gate_ready(ctx, fz))
                fight_off_zone = true;
        }
        if (idx >= 0 && (!lift_standing || fight_off_zone)) {
            int cost = raise_spell_cost();
            long rearm = cost > 0 ? g->stats.gold / cost : 0;
            if (g->spells.counts[idx] + rearm < c->k) {
                if (ob_diag_verbose())
                    printf("[RECRUIT] realize fails at-gate lift: k=%d "
                           "held=%d rearm=%ld gold=%d\n", c->k,
                           g->spells.counts[idx], rearm, g->stats.gold);
                // A lift the book cannot hold and the spent-down wallet
                // cannot re-buy is a STOCK failure of the plan SHAPE: route
                // it through the realize-failure memory so the retry loop
                // excludes it and picks a deliverable winner instead of
                // re-choosing this plan every cycle.
                if (c->n > 0) s_realize_stock_troop = c->troops[0];
                if (out_cause) *out_cause = EXEC_CAUSE_STOCK;
                return false;
            }
        }
    }
    return true;
}

// Realize with the mover's restock loop off: these trips carry exact
// candidate armies a mid-move teleport would scramble.
static bool realize_plan(ExecCtx *ctx, const Candidate *c,
                         const RecruitRequest *req, ExecCause *out_cause) {
    exec_set_gate_restock_suppressed(true);
    bool ok = realize_plan_body(ctx, c, req, out_cause);
    exec_set_gate_restock_suppressed(false);
    // s_realize_reserve stays readable (exec_last_realize_reserve) until the
    // next exec_recruit entry: the siege approach that follows this realize
    // keys its pre-stock on whether the plan banked approach money.
    return ok;
}

int exec_last_realize_reserve(void) { return s_realize_reserve; }

// ---- the raise ladder (AP-132) ------------------------------------------------------

int exec_raise_k_for_win(ExecCtx *ctx, const RecruitRequest *req) {
    Game *g = ctx->g;
    if (!g->stats.knows_magic) return -1;
    int kmax = achievable_k(g, g->stats.gold);
    int amt = GameRaiseControlAmount(g);
    for (int k = 1; k <= kmax; k++) {
        int lead = g->stats.leadership_current + k * amt;
        if (predict_combat_cached(ctx, req->combat_mode, req->seed_key,
                                  req->display_name, req->garrison, NULL,
                                  lead, NULL, req->grow_weeks, NULL))
            return k;
    }
    return -1;
}

bool exec_raise_for_fight(ExecCtx *ctx, const RecruitRequest *req, int k) {
    (void)req;
    Game *g = ctx->g;
    int idx = spell_index_by_adventure_effect(ADV_EFFECT_RAISE_CONTROL);
    if (idx < 0) return false;
    if (g->spells.counts[idx] >= k) return true;
    return stock_spell_charges(ctx, idx, k, true);
}

bool exec_rearm_raise_for(ExecCtx *ctx, int k, int reserve_gold) {
    Game *g = ctx->g;
    int idx = spell_index_by_adventure_effect(ADV_EFFECT_RAISE_CONTROL);
    if (idx < 0) return false;
    int cast = 0;
    while (cast < k) {
        if (g->spells.counts[idx] > 0) {
            if (!exec_cast_raise(ctx)) break;
            cast++;
            continue;
        }
        // Cycling re-arm: buy more charges, never spending below the
        // caller's reserve (the same reserve rule the realize enforces).
        const char *tid = town_selling_spell(g, idx);
        const SpellDef *sd = spell_by_index(idx);
        if (!tid || !sd) break;
        if (g->stats.gold - sd->cost < reserve_gold) break;
        if (!stock_spell_charges(ctx, idx, 1, true)) break;
    }
    return cast >= k;
}

// ---- entry (AP-120, AP-140) -----------------------------------------------------------

bool exec_recruit(ExecCtx *ctx, const RecruitRequest *req,
                  ExecCause *out_cause) {
    Game *g = ctx->g;
    if (ob_diag_verbose())
        printf("[RECRUIT] enter %s: gold=%d lead=%d/%d wait=%d\n",
               req->seed_key ? req->seed_key : "?", g->stats.gold,
               g->stats.leadership_current, g->stats.leadership_base,
               (int)exec_wait_allowed());
    s_realize_reserve = 0;
    SearchArgs a = { ctx, req, g->stats.gold, 0, false };
    int win = search_all(&a);
    if (win < 0 && excl_clear(req->seed_key) > 0) {
        // The realize-failure memory exhausted this target's winner set:
        // clear it and search the full universe once -- the exclusions were
        // recorded at the wallet of a past failure (AP-138).
        if (ob_diag_verbose())
            printf("[RECRUIT] exclusions reset for %s: re-searching\n",
                   req->seed_key ? req->seed_key : "?");
        win = search_all(&a);
    }
    if (win < 0 && exec_wait_allowed()) {
        // THE FUNDED WAIT (AP-053): shape the wait world FIRST (the trim
        // dismisses held stacks -- quote credit the bisect must not price),
        // then bisect the smallest budget that buys any winner, verify weekly
        // commission can EXCEED it with one week of slack, play for it, and
        // search again on the richer world.
        exec_prepare_gold_wait(ctx);
        SearchArgs probe = { ctx, req, INT_MAX / 4, 0, true };
        if (search_all(&probe) >= 0) {
            long lo = g->stats.gold, hi = INT_MAX / 4;
            for (int it = 0; it < 24 && lo + 1 < hi; it++) {
                long mid = lo + (hi - lo) / 2;
                SearchArgs pa = { ctx, req, mid, 0, true };
                if (search_all(&pa) >= 0) hi = mid;
                else lo = mid;
            }
            ExecCause gc = EXEC_CAUSE_NONE;
            // Fund the plan PLUS a fixed working-capital margin (1/16 of
            // the budget): charge-trip ratchets, transport refills, and the
            // lift's own consumption are overhead the candidate price does
            // not carry. The calendar bound still applies.
            long funded_target = hi + hi / REALIZE_OVERHEAD_DEN;
            // GROWTH-AWARE WAIT (AP-183 applied to the wait itself): the
            // bisect priced TODAY's garrison, but the wait's own weeks grow
            // it. Re-probe at the garrison as it will stand when the wallet
            // arrives; a target that has receded farther than the wait
            // delivers is never funded -- on a long calendar the TIME bound
            // alone approves 90-week waits that growth makes futile (the
            // wait-spiral: each futile wait grows every remaining target).
            {
                int wnet = GameWeeklyNetGold(g);
                long W = wnet > 0
                    ? (funded_target - g->stats.gold + wnet - 1) / wnet
                    : 0;
                if (wnet > 0 && W > 0) {
                    RecruitRequest grown = *req;
                    grown.grow_weeks = req->grow_weeks + (int)W;
                    SearchArgs gprobe = { ctx, &grown, INT_MAX / 4, 0, true };
                    if (search_all(&gprobe) < 0) {
                        if (ob_diag_verbose())
                            printf("[RECRUIT] funded-wait %s: refused, no "
                                   "winner at the %ld-week-grown garrison\n",
                                   req->seed_key ? req->seed_key : "?", W);
                        if (out_cause) *out_cause = EXEC_CAUSE_GOLD;
                        return false;
                    }
                    long glo = g->stats.gold, ghi = INT_MAX / 4;
                    for (int it = 0; it < 24 && glo + 1 < ghi; it++) {
                        long mid = glo + (ghi - glo) / 2;
                        SearchArgs pa2 = { ctx, &grown, mid, 0, true };
                        if (search_all(&pa2) >= 0) ghi = mid;
                        else glo = mid;
                    }
                    long grown_target = ghi + ghi / REALIZE_OVERHEAD_DEN;
                    if (grown_target > funded_target + W * wnet) {
                        // Receding target: the growth added more budget than
                        // the whole wait delivers. Waiting is futile -- defer
                        // so cheaper candidates run first.
                        if (ob_diag_verbose())
                            printf("[RECRUIT] funded-wait %s: refused, "
                                   "receding target (now=%ld grown=%ld "
                                   "W=%ld net=%d)\n",
                                   req->seed_key ? req->seed_key : "?",
                                   funded_target, grown_target, W, wnet);
                        if (out_cause) *out_cause = EXEC_CAUSE_GOLD;
                        return false;
                    }
                    if (grown_target > funded_target)
                        funded_target = grown_target;
                }
            }
            bool funded = exec_ensure_gold(ctx, (int)funded_target, &gc);
            if (ob_diag_verbose()) {
                printf("[RECRUIT] funded-wait %s: budget=%ld funded=%d "
                       "cause=%s gold=%d net=%d comm=%d upkeep=%d day=%d "
                       "army:",
                       req->seed_key ? req->seed_key : "?", hi, (int)funded,
                       exec_cause_name(gc), g->stats.gold,
                       GameWeeklyNetGold(g), g->stats.commission_weekly,
                       GameArmyWeeklyUpkeep(g), g->stats.days_left);
                for (int s = 0; s < GAME_ARMY_SLOTS; s++)
                    if (g->army[s].id[0] && g->army[s].count > 0)
                        printf(" %s×%d", g->army[s].id, g->army[s].count);
                printf("\n");
            }
            if (funded) {
                // Search at the bisect's own proven budget, not the full
                // wallet: the 16-rung sampler spreads over the realizable
                // set, and widening the set re-spaces the rungs off the
                // winner the bisect just proved. The surplus stays for the
                // realize's working capital.
                a.wallet = hi;
                win = search_all(&a);
                if (win < 0) {
                    a.wallet = g->stats.gold;
                    win = search_all(&a);
                }
                if (win < 0) {
                    // The rung sampler can miss a winner the existence
                    // probe sees (the wait's growth moved the frontier off
                    // the sampled rungs): take the probe's own winner --
                    // the strongest realizable candidate -- rather than
                    // deferring a fight the wait just funded.
                    SearchArgs pw = { ctx, req, g->stats.gold, 0, true };
                    win = search_all(&pw);
                    if (win >= 0 && ob_diag_verbose())
                        printf("[RECRUIT] funded probe fallback %s: "
                               "hp=%ld gold=%ld\n",
                               req->seed_key ? req->seed_key : "?",
                               s_cands[win].hp_worth, s_cands[win].gold);
                }
            } else if (out_cause) {
                *out_cause = gc;
            }
        } else if (ob_diag_verbose()) {
            printf("[RECRUIT] funded-wait %s: no winner at any budget\n",
                   req->seed_key ? req->seed_key : "?");
        }
    }
    if (win < 0 && exec_wait_allowed()) {
        // THE STOCK WAIT -- the funded wait's twin for troop supply. A search
        // that fails even at an unbounded wallet is not money-bound; when
        // lifting the dwelling stocks to their restock ceiling turns up a
        // winner, the binding constraint is troop supply that the world's
        // own weekly restock will refill (the week boundary repopulates or
        // grows dwellings). Waiting for restock is engine-bounded exactly
        // like waiting for income, and is taken the same way: only under
        // the armed wait-allowed pass, re-searching on the real world each
        // week, growth-aware (the target garrison grows weekly too -- when
        // even the restock ceiling no longer wins the grown fight, the
        // target has receded and the wait stops). No calendar bound: the
        // receded-target stop and the engine's own TIME failure
        // (exec_spend_week) end the wait, so the day budget never changes it. The
        // wait-world prep already ran in the funded-wait block above.
        if (stock_relaxed_wins(ctx, req)) {
            int weeks = 0;
            long stock_prev = dwelling_stock_total(ctx);
            while (win < 0) {
                if (!exec_spend_week(ctx, DAY_ACCT_RECRUIT)) break;
                weeks++;
                SearchArgs again = { ctx, req, g->stats.gold, 0, false };
                win = search_all(&again);
                if (win >= 0) break;
                if (!stock_relaxed_wins(ctx, req)) break;   // receded
                // Budget-independent stop: a week that added no new dwelling
                // stock anywhere means every restocking source has reached its
                // ceiling; more waiting cannot field a winner and only grows
                // the target. This reads restock state, never days_left, so the
                // wait ends at the same point for every budget.
                long stock_now = dwelling_stock_total(ctx);
                if (stock_now <= stock_prev) break;
                stock_prev = stock_now;
            }
            if (ob_diag_verbose())
                printf("[RECRUIT] stock-wait %s: weeks=%d %s gold=%d "
                       "day=%d\n",
                       req->seed_key ? req->seed_key : "?", weeks,
                       win >= 0 ? "WIN" : "no-winner", g->stats.gold,
                       g->stats.days_left);
        }
    }
    if (win < 0) {
        ExecCause cause = rfw_probe_cause(ctx, req);
        if (out_cause && *out_cause == EXEC_CAUSE_NONE) *out_cause = cause;
        else if (out_cause && cause != EXEC_CAUSE_NO_WINNING_ARMY)
            *out_cause = cause;
        if (ob_diag_verbose())
            printf("[RECRUIT] no winner for %s -> cause=%s\n",
                   req->seed_key ? req->seed_key : "?",
                   exec_cause_name(out_cause ? *out_cause : cause));
        return false;
    }
    // Realize with realize-failure memory (AP-138): a winner whose realize
    // starves of STOCK is remembered and excluded for this target; the
    // re-search (at the spent-down wallet -- the waste is recorded, the
    // outer attempt rollback restores it) picks the next-cheapest winner
    // that CAN be bought. The exclusion survives the rollback, so the next
    // cycle's full-wallet search skips the proven-unbuyable shape instead
    // of looping on it.
    for (int tries = 0; tries < 4; tries++) {
        Candidate chosen = s_cands[win];   // the winner IS the plan (AP-131.4)
        if (ob_diag_verbose())
            printf("[RECRUIT] project %s: hp=%ld gold=%ld days=%d kit=%d c=%d "
                   "k=%d\n",
                   req->seed_key ? req->seed_key : "?", chosen.hp_worth,
                   chosen.gold, chosen.days, chosen.kit_spell,
                   chosen.kit_charges, chosen.k);
        ExecCause rc = EXEC_CAUSE_NONE;
        s_realize_stock_troop = -1;
        if (realize_plan(ctx, &chosen, req, &rc)) {
            if (ob_diag_verbose()) {
                printf("[RECRUIT] commit %s: realized lead=%d/%d army:",
                       req->seed_key ? req->seed_key : "?",
                       g->stats.leadership_current, g->stats.leadership_base);
                for (int s = 0; s < GAME_ARMY_SLOTS; s++)
                    if (g->army[s].id[0] && g->army[s].count > 0)
                        printf(" %s×%d", g->army[s].id, g->army[s].count);
                printf("\n");
            }
            return true;
        }
        if (rc != EXEC_CAUSE_STOCK || s_realize_stock_troop < 0) {
            if (out_cause) *out_cause = rc;
            return false;
        }
        excl_add(req->seed_key, s_realize_stock_troop);
        if (ob_diag_verbose()) {
            const TroopDef *xt = troop_by_index(s_realize_stock_troop);
            printf("[RECRUIT] realize-excluded %s for %s: re-searching "
                   "(gold=%d)\n", xt ? xt->id : "?",
                   req->seed_key ? req->seed_key : "?", g->stats.gold);
        }
        a.wallet = g->stats.gold;
        win = search_all(&a);
        if (win < 0) break;
    }
    if (out_cause) *out_cause = EXEC_CAUSE_STOCK;
    if (ob_diag_verbose())
        printf("[RECRUIT] no realizable winner for %s -> cause=stock\n",
               req->seed_key ? req->seed_key : "?");
    return false;
}

// ---- scarce-winner queries (AP-054) ----------------------------------------------------

bool recruit_winner_finite_draw(ExecCtx *ctx, const RecruitRequest *req,
                                int *out_draw) {
    memset(out_draw, 0, sizeof(int) * CAT_TROOPS_MAX);
    SearchArgs a = { ctx, req, ctx->g->stats.gold, 0, false };
    int win = search_all(&a);
    if (win < 0) return false;
    const Candidate *c = &s_cands[win];
    for (int s = 0; s < c->n; s++) {
        int ti = c->troops[s];
        const TroopQuote *q = &s_quotes[ti];
        int need = c->counts[s] - q->held;
        for (int i = 0; i < q->n && need > 0; i++) {
            const RecruitSource *src = &s_sources[q->src[i]];
            if (src->kind == RSRC_INSTANT_ARMY ||
                src->kind == RSRC_HOME_CASTLE)
                continue;   // unbounded sources never bind
            int take = q->stock[i] < need ? q->stock[i] : need;
            if (take > 0) {
                out_draw[ti] += take;
                need -= take;
            }
        }
    }
    return true;
}

bool recruit_winner_survives_less(ExecCtx *ctx, const RecruitRequest *req,
                                  const int *deduct, bool restock_ceiling) {
    recruit_sources_enumerate(ctx);
    // Deduct the rival's draw from the finite sources.
    for (int si = 0; si < s_source_n; si++) {
        RecruitSource *s = &s_sources[si];
        if (s->kind == RSRC_INSTANT_ARMY || s->kind == RSRC_HOME_CASTLE)
            continue;
        int d = deduct[s->troop_idx];
        if (d <= 0) continue;
        int take = d < s->avail ? d : s->avail;
        s->avail -= take;
    }
    quotes_build(ctx, restock_ceiling);
    plan_build();
    s_cand_n = 0;
    SearchArgs a = { ctx, req, ctx->g->stats.gold, 0, true };
    int kmax = achievable_k(ctx->g, a.wallet);
    for (int gi = 0; gi < s_group_n; gi++)
        for (int order = 0; order < 2; order++)
            build_candidate(&a, gi, order, -1, 0, kmax);
    for (int i = s_cand_n - 1; i >= 0; i--)
        if (cand_sim(&a, &s_cands[i])) return true;
    return false;
}

// ---- the gold-wait site (AP-161) --------------------------------------------------------

// Shape the world for a long gold wait: return the rental, trim the army to
// its single cheapest stack, and swap even that one out when it still bleeds
// (the wait's only job is income; the realize rebuys after). Idempotent --
// callers that bisect a budget MUST run this first so the bisect prices the
// same post-trim world the wait will deliver (a held stack is quote credit;
// dismissing it after the bisect invalidates the budget).
void exec_prepare_gold_wait(ExecCtx *ctx) {
    Game *g = ctx->g;
    if (!exec_wait_allowed()) return;
    int guard = GAME_ARMY_SLOTS;
    bool trimmed = false;
    while (guard-- > 0) {
        int worst = -1, worst_upkeep = 0;
        int occupied = 0;
        for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
            if (!g->army[s].id[0] || g->army[s].count <= 0) continue;
            occupied++;
            int up = GameStackWeeklyUpkeep(g->army[s].id, g->army[s].count);
            if (up > worst_upkeep) { worst_upkeep = up; worst = s; }
        }
        if (occupied <= 1 || worst < 0) break;
        if (ob_diag_verbose() && !trimmed) {
            trimmed = true;
            printf("[RECRUIT] gold-wait trim begins: gold=%d army:",
                   g->stats.gold);
            for (int s = 0; s < GAME_ARMY_SLOTS; s++)
                if (g->army[s].id[0] && g->army[s].count > 0)
                    printf(" %s×%d", g->army[s].id, g->army[s].count);
            printf("\n");
        }
        if (g->position.own_castle[0] &&
            exec_garrison_slot(ctx, g->position.own_castle, worst))
            continue;
        if (!exec_dismiss_slot(ctx, worst)) break;
    }
    // The one remaining stack can still exceed the weekly commission on its
    // own: swap it for a single unit from the cheapest-upkeep in-zone
    // dwelling, buying first so the army never empties.
    {
        int pig = -1, occupied2 = 0;
        for (int s = 0; s < GAME_ARMY_SLOTS; s++)
            if (g->army[s].id[0] && g->army[s].count > 0) {
                occupied2++;
                pig = s;
            }
        long pig_upkeep = pig >= 0
            ? GameStackWeeklyUpkeep(g->army[pig].id, g->army[pig].count) : 0;
        if (occupied2 == 1 &&
            pig_upkeep * GOLD_WAIT_PIG_DEN > g->stats.commission_weekly) {
            int hz = hero_zone_index(ctx);
            int bi = -1, bcost = 0, bup = 0;
            for (int i = 0; i < g->dwelling_count; i++) {
                const DwellingState *d = &g->dwellings[i];
                if (!d->troop_id[0] || d->count <= 0) continue;
                if (zone_index_of(ctx->res, d->zone) != hz) continue;
                const TroopDef *t = troop_by_id(d->troop_id);
                if (!t || t->recruit_cost >= g->stats.gold) continue;
                int up = GameStackWeeklyUpkeep(d->troop_id, 1);
                if ((long)up >= pig_upkeep) continue;
                if (bi < 0 || up < bup ||
                    (up == bup && t->recruit_cost < bcost)) {
                    bi = i;
                    bcost = t->recruit_cost;
                    bup = up;
                }
            }
            if (bi >= 0) {
                const DwellingState *d = &g->dwellings[bi];
                NavPoint dp = { hz, d->x, d->y };
                ExecCause cc3;
                int rm_before = g->stats.days_left;
                long rm_cross = ledger_committed(DAY_ACCT_CROSSING);
                int rm_r = move_to(ctx, &dp, 1, true, NULL, &cc3);
                ledger_book_move(DAY_ACCT_RECRUITMOVE, rm_before,
                                 g->stats.days_left, rm_cross);
                if (rm_r >= 0) {
                    if (pending_flow == FLOW_RECRUIT) {
                        FlowAnswer one = { FLOW_ANS_YES, 1 };
                        rec_push_answer(g, FLOW_RECRUIT, one,
                                        PLAYER_IO_COMBAT_NOT_RUN);
                        PlayerIoPresentation pres;
                        player_io_answer(g, ctx->map, ctx->fog, ctx->res,
                                         one, PLAYER_IO_COMBAT_NOT_RUN,
                                         &pres);
                        exec_pump_passive(ctx);
                    } else {
                        exec_answer_pending(ctx, true);
                    }
                    int stacks = 0;
                    for (int s = 0; s < GAME_ARMY_SLOTS; s++)
                        if (g->army[s].id[0] && g->army[s].count > 0)
                            stacks++;
                    if (stacks > 1) exec_dismiss_slot(ctx, pig);
                    GameCompactArmy(g);
                }
            }
        }
    }
    // The rental LAST, and only when its fare is what keeps the weekly net
    // under water -- a boat is 500/week of insurance, but it is also the
    // only ride home from a dockless shore, and a wait that funds in a
    // week or two never repays stranding the buys' crossing.
    if (g->boat.has_boat && GameWeeklyNetGold(g) <= 0) {
        int cancel_mk = recsink_mark();
        rec_push_action(g, RA_CANCEL_BOAT, NULL, 0, 0);
        if (GameCancelBoat(g) == BOAT_CANCEL_OK) {
            if (ob_diag_verbose())
                printf("[RECRUIT] gold-wait boat return at (%d,%d,%s)\n",
                       g->position.x, g->position.y, g->position.zone);
        } else {
            recsink_rollback(cancel_mk);
        }
    }
}

static bool ensure_gold_inner(ExecCtx *ctx, int need, bool allow_trim,
                              ExecCause *out_cause);

bool exec_ensure_gold(ExecCtx *ctx, int need, ExecCause *out_cause) {
    return ensure_gold_inner(ctx, need, true, out_cause);
}

// The no-trim ensure (mid-move sites): waits on the STANDING army's income
// only. Trimming belongs to the planner-level wait sites -- a committed
// fight army must never be dismissed to afford a boat fare (a bleeding
// army's negative net fails this fast, which is the correct answer there).
bool exec_ensure_gold_no_trim(ExecCtx *ctx, int need, ExecCause *out_cause) {
    return ensure_gold_inner(ctx, need, false, out_cause);
}

static bool ensure_gold_inner(ExecCtx *ctx, int need, bool allow_trim,
                              ExecCause *out_cause) {
    Game *g = ctx->g;
    if (g->stats.gold >= need) return true;
    if (!exec_wait_allowed()) {
        if (out_cause) *out_cause = EXEC_CAUSE_GOLD;
        if (ob_diag_verbose())
            printf("[ENSURE] need=%d gold=%d -> no-wait\n", need,
                   g->stats.gold);
        return false;
    }
    if (allow_trim) exec_prepare_gold_wait(ctx);
    int net = GameWeeklyNetGold(g);
    if (net <= 0) {
        if (out_cause) *out_cause = EXEC_CAUSE_GOLD;
        if (ob_diag_verbose())
            printf("[ENSURE] need=%d gold=%d net=%d -> bleeding\n", need,
                   g->stats.gold, net);
        return false;
    }
    long shortfall = (long)need - g->stats.gold;
    long weeks = (shortfall + net - 1) / net;
    // No calendar look-ahead: run the income weeks the shortfall needs and let
    // the engine's own TIME failure (exec_spend_week) stop the wait if the days
    // run out mid-save. Calendar-free, so the day budget never changes the decision.
    for (long w = 0; w < weeks; w++) {
        if (!exec_spend_week(ctx, DAY_ACCT_GOLDWAIT)) {
            if (out_cause) *out_cause = EXEC_CAUSE_TIME;
            return false;
        }
        if (g->stats.gold >= need) break;
    }
    if (g->stats.gold >= need) return true;
    if (out_cause) *out_cause = EXEC_CAUSE_GOLD;
    if (ob_diag_verbose())
        printf("[ENSURE] need=%d gold=%d net=%d weeks=%ld -> waited-short\n",
               need, g->stats.gold, net, weeks);
    return false;
}
