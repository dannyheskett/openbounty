// autoplay/search.c -- the snapshot-tree search: the whole oracle (AP-200).
//
// A node is a REACHED WORLD STATE carrying everything needed to resume from
// it: its world snapshot, its planner memory (PlannerRun), the recording
// prims ITS OWN EDGE appended, and its ordered candidate objectives. One
// expansion = restore the node and attempt EXACTLY ONE objective through the
// planner step core (planner.h) -- never a playout, never a rollout to the end
// of the game. A successful attempt captures the child state into the tree;
// the goal test is "no objective still open"; the committed line is a node's
// recording, rebuilt as the root-to-node delta chain. Nothing is ever
// simulated twice: every edge was simulated once, when it was created.
//
// ORDERING IS STRICT DEPTH-FIRST (see node_key): the AVL frontier is keyed on
// the negated creation sequence, so pop_min returns the NEWEST node -- the
// current line's tip -- and a re-inserted parent keeps its older key, so a
// child's whole subtree drains before the parent's next branch. That is exact
// backtracking DFS expressed on a tree, and pop_max evicts the ROOT-MOST
// alternatives, which are the last resort. THE CURRENT LINE HAS ABSOLUTE
// PRIORITY; backtracking happens only on exhaustion. Two best-first keys were
// measured and abandoned first -- f = g + h over elapsed days, and
// fewest-open-first -- because both turned the frontier into a swamp of
// near-identical siblings (segments cost 0-2 days, successful reorderings
// collapse into states already seen) and the descent never committed to a
// line: 118k expansions at 14/280 done, 155k at 12/280.
//
// The expansion set is BOUNDED AND DECLARED (AP-203): the per-node branching
// cap (SEARCH_MAX_CHILDREN), the frontier beam (SEARCH_MAX_LIVE_NODES via
// baltree_pop_max), the stagnation cut back to the root
// (SEARCH_STAGNATION_CUT), the line-local cycle rules, and the dead-leaf
// drop. Memory is bounded the classic-tree way besides: refcounted lineage
// frees a discarded branch up to its first still-referenced ancestor. Every
// one of these can only weaken a NOT-SOLVED claim, never a SOLVED one.
//
// Deterministic: same build + seed => same keys, same insertion order, same
// pops, same plan.

// _POSIX_C_SOURCE: clock_gettime (the timing counters). Same idiom as
// src/recorder.c; must precede every include.
#define _POSIX_C_SOURCE 200809L

#include "search.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "autoplay.h"
#include "baltree.h"
#include "diag.h"
#include "exec_ledger.h"  // watchdog_hit, LedgerSnap
#include "game.h"         // GameRngSnapshot (node snapshots)
#include "pending.h"      // pending_reset (node restore seam)
#include "planner.h"      // the step core
#include "recording.h"
#include "spells_adventure.h"  // spells_adventure_reset_ui (restore seam)
#include "worldsnap.h"    // worldsnap_reset_calendar_dead

// A node models EVERY open objective as a branch: a state whose top few
// candidates all fail must fall through to the rest, exactly as a
// cheapest-first cycle head does -- so the cap is the enumeration bound,
// not a model choice.
#define SEARCH_MAX_CAND       STEP_MAX
#define SEARCH_MAX_LIVE_NODES 256   // node beam: ~1 MB per live node

// Resolve-progress hook (autoplay.h). NULL in every headless run; the visible
// shell registers one to draw a progress frame and to allow cancellation.
static AutoplayProgressFn s_progress_fn = NULL;
static void              *s_progress_ud = NULL;
static bool               s_progress_cancelled = false;
void autoplay_set_progress(AutoplayProgressFn fn, void *ud) {
    s_progress_fn = fn;
    s_progress_ud = ud;
}
bool autoplay_progress_cancelled(void) { return s_progress_cancelled; }

// The first objective the committed line left undone (for --validate-pack).
static char s_unmet_label[64];
static char s_unmet_cause[96];
const char *autoplay_unmet_label(void) { return s_unmet_label; }
const char *autoplay_unmet_cause(void) { return s_unmet_cause; }
#define SEARCH_MAX_EXPANSIONS 200000 // runaway watchdog (far above any real run)

// ---- timing instrumentation (always on, cheap counters) ---------------------
typedef struct {
    long   n;
    double sum_ms, max_ms;
    long   buckets[32];            // bucket i: [2^i .. 2^(i+1)) microseconds
} SwStat;

static double sw_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}
static void sw_note(SwStat *s, double ms) {
    s->n++;
    s->sum_ms += ms;
    if (ms > s->max_ms) s->max_ms = ms;
    double us = ms * 1e3;
    int b = 0;
    while (b < 31 && us >= 2.0) { us /= 2.0; b++; }
    s->buckets[b]++;
}
static double sw_p50_ms(const SwStat *s) {
    long seen = 0;
    for (int b = 0; b < 32; b++) {
        seen += s->buckets[b];
        if (seen * 2 >= s->n)
            return (double)(1u << b) / 1e3;   // bucket lower bound, in ms
    }
    return 0.0;
}
// The full per-stage distribution: --verbose only.
static void sw_print(const char *tag, const SwStat *s) {
    if (s->n == 0) return;
    printf("[SEARCH] timing %-8s n=%-6ld p50=%.2fms mean=%.2fms max=%.1fms "
           "total=%.1fs\n", tag, s->n, sw_p50_ms(s),
           s->sum_ms / (double)s->n, s->max_ms, s->sum_ms / 1e3);
}

// One field of the default run's single timing line: total seconds and call
// count, the two numbers that answer "where did the wall clock go".
static void sw_brief(const char *tag, const SwStat *s) {
    printf(" %s=%.1fs/%ld", tag, s->sum_ms / 1e3, s->n);
}

static SwStat s_sw_step;      // one planner_step (the attempt segment)
static SwStat s_sw_open;      // planner_open (enumeration) per expansion
static SwStat s_sw_cand;      // planner_candidates (pricing) per new node
static SwStat s_sw_capture;   // worldsnap_capture per child
static SwStat s_sw_restore;   // worldsnap_restore per expansion

// NOTE: there is deliberately NO global duplicate-state set. A closed set
// without reopening is unsound for this search: a state first reached by a
// sibling line that later dies would block the main line from ever entering
// it (measured: the DFS descent stalled at 28/280 behind exactly that).
// Cycle protection is LINE-LOCAL instead: a child whose state signature
// equals any ANCESTOR on its own path is a true loop and is dropped -- a
// path that revisits its own state can never be needed -- and consecutive
// KEPT edges (world kept, nothing completed: the PREFOUGHT shape) are
// capped, because an unbounded KEPT chain is a calendar-burning descent at
// constant progress (measured: 15k expansions pinned at 28/280, tip 385
// days deep, once the global set was removed).

static uint64_t fnv64(uint64_t h, const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
    return h;
}
// Signature of the LIVE world (Game+Map+Fog+rng): the ancestry cycle check.
static uint64_t sig_live(const ExecCtx *ctx) {
    uint64_t h = 1469598103934665603ULL;
    h = fnv64(h, ctx->g,   sizeof *ctx->g);
    h = fnv64(h, ctx->map, sizeof *ctx->map);
    h = fnv64(h, ctx->fog, sizeof *ctx->fog);
    uint64_t rng = GameRngSnapshot();
    h = fnv64(h, &rng, sizeof rng);
    return h ? h : 1;
}

#define SEARCH_MAX_KEPT_STREAK 3   // consecutive KEPT edges allowed per line

// Stagnation cut: a descent that has completed NOTHING for this many
// expansions is a doomed region (measured on seed 17: low-calendar subtrees
// fail with cause=time ~170 expansions per node, and DFS would drain them
// all before returning to the root's alternatives). The cut prunes the
// frontier back to the ROOT, whose next untried candidate starts a fresh
// descent under a different opening objective. The root is exempt from the
// branching cap for exactly this reason.
#define SEARCH_STAGNATION_CUT 3000

// ---- the tree ---------------------------------------------------------------

// The node snapshot is the FULL world (WorldSnapshot: Game + Map + Fog +
// rng + ledger, ~1 MB). The slim variant (Game only, map rebuilt by a zone
// reload) was measured WRONG: a mid-game fresh stamp is not byte-equal to
// the boot map plus the line's incremental writes (foes stamped at moved
// positions, cleared-tile residue), and the recording cannot reproduce a
// map the line did not derive from prims (seed 15 diverged at prim 32 of
// its own replay). Memory is paid for with a smaller beam.
typedef WorldSnapshot NodeSnap;

typedef struct SearchNode {
    struct SearchNode *parent;      // lineage (recording rebuild); ref held
    int            refcount;        // frontier + children references
    NodeSnap      *snap;            // ~100 KB; freed with the node
    RecPrim       *delta;           // prims THIS edge appended
    int            delta_n;
    PlannerRun    *prun;            // planner memory at this state (heap:
                                    //   ~90 KB, copied per node)
    PlanCand      *cand;            // heap: cand_n entries (full open order)
    int            cand_n, next_cand;
    bool           waited;          // the one wait-armed re-list happened
    bool           sacrificed;      // the one sacrifice-escape expansion ran
    long           seq;             // creation order (the DFS key; reinserts
                                    //   keep it, so a child outranks its
                                    //   parent and drains first)
    int            children_made;   // true branching taken from this node
    uint64_t       sig;             // state signature (ancestry cycle check)
    int            kept_streak;     // consecutive KEPT edges ending here
    int            g_days;          // elapsed days at this state (progress line)
    int            done_n, open_n;  // progress (reporting)
} SearchNode;

// True branching per node: failures fall through the WHOLE candidate list
// (exactly the greedy cycle), but once a node has fathered this many child
// states, its remaining alternatives are trimmed -- bounded fanout keeps the
// backtracking tree tractable; the beam/watchdog semantics already declare
// the modelled set is bounded.
#define SEARCH_MAX_CHILDREN 4

static long s_node_seq;             // monotonic per search_run

static void node_snap_capture(NodeSnap *s, const ExecCtx *ctx) {
    worldsnap_capture(s, ctx->g, ctx->map, ctx->fog);
}

static void node_snap_restore(const NodeSnap *s, ExecCtx *ctx) {
    // worldsnap_restore includes the cross-play seams (pending reset,
    // spell-UI reset); nav/recruit seams are applied by node_restore.
    worldsnap_restore(s, ctx->g, ctx->map, ctx->fog);
}

static int node_key(const SearchNode *d) {
    // STRICT DEPTH-FIRST ordering (measured, twice): both f = g + h and
    // fewest-open-first turned the frontier into a swamp of near-identical
    // siblings -- segments cost 0-2 days, successful reorderings collapse
    // into seen states, and the descent never commits (118k expansions
    // 14/280 done; 155k expansions 12/280 done). Depth-first tree
    // discipline is what works here: THE CURRENT LINE HAS ABSOLUTE
    // PRIORITY. pop_min = newest creation = the line's tip; a reinserted
    // parent keeps its original (older) seq, so its child and the child's
    // whole subtree drain before the parent's next branch -- exact
    // backtracking DFS on the AVL. pop_max eviction trims the OLDEST
    // (root-most) pending alternatives first, which are the last resort.
    return (int)(-d->seq);
}

static void node_unref(SearchNode *d) {
    while (d && --d->refcount <= 0) {
        SearchNode *p = d->parent;
        free(d->snap);
        free(d->delta);
        free(d->prun);
        free(d->cand);
        free(d);
        d = p;                      // release the lineage up to the first
    }                               //   ancestor still referenced
}

// baltree_free callback on teardown: every frontier reference drops.
static void node_unref_cb(void *p) { node_unref((SearchNode *)p); }

// Rebuild the recording of `d` into the sink: concatenate deltas root->d.
static void node_rebuild_recording(const SearchNode *d) {
    const SearchNode *chain[1024];
    int n = 0;
    const SearchNode *c = d;
    for (; c && n < 1024; c = c->parent) chain[n++] = c;
    if (c) watchdog_hit("search-depth");            // chain[] overflow: loud
    recsink_restore_copy(NULL, 0);                  // start empty
    RecSink *rs = recsink();
    for (int i = n - 1; i >= 0; i--) {
        const SearchNode *e = chain[i];
        if (e->delta_n <= 0) continue;
        int room = rs->cap - rs->count;
        int take = e->delta_n <= room ? e->delta_n : room;
        memcpy(rs->prims + rs->count, e->delta,
               (size_t)take * sizeof *rs->prims);
        rs->count += take;
    }
}

// Fill a node's candidate list (heap-sized to the actual count) from the
// step core's ordering. The scratch buffer keeps node memory proportional
// to the OPEN count, not STEP_MAX.
static bool node_fill_candidates(ExecCtx *ctx, SearchNode *d) {
    static PlanCand scratch[SEARCH_MAX_CAND];
    double t_cand = sw_now_ms();
    int n = planner_candidates(ctx, d->prun, scratch, SEARCH_MAX_CAND);
    sw_note(&s_sw_cand, sw_now_ms() - t_cand);
    free(d->cand);
    d->cand = NULL;
    d->cand_n = 0;
    d->next_cand = 0;
    if (n > 0) {
        d->cand = (PlanCand *)malloc((size_t)n * sizeof *d->cand);
        if (!d->cand) return false;
        memcpy(d->cand, scratch, (size_t)n * sizeof *d->cand);
        d->cand_n = n;
    }
    // The ordering HEAD is the decision this node is about to make: the tiers
    // (keystone, scarce-winner, alcove) show up here as a reordered front.
    if (ob_diag_verbose()) {
        printf("[SEARCH] order seq=%ld n=%d head:", d->seq, n);
        for (int i = 0; i < n && i < 6; i++) printf(" %s", scratch[i].handle);
        printf("\n");
    }
    return true;
}

// Capture the live world as a child of `parent` (NULL for the root). The
// recording sink currently holds parent's recording plus this edge's prims,
// so the edge delta is the tail past `parent_rec_n`. Returns NULL on
// allocation failure -- the branch is skipped, never half-captured.
static SearchNode *node_capture(ExecCtx *ctx, SearchNode *parent,
                                int parent_rec_n, const PlannerRun *prun,
                                int start_days_left, int obj_total) {
    SearchNode *d = (SearchNode *)calloc(1, sizeof *d);
    if (!d) return NULL;
    d->snap = (NodeSnap *)malloc(sizeof *d->snap);
    d->prun = (PlannerRun *)malloc(sizeof *d->prun);
    if (!d->snap || !d->prun) {
        free(d->snap);
        free(d->prun);
        free(d);
        return NULL;
    }
    *d->prun = *prun;

    RecSink *rs = recsink();
    d->delta_n = rs->count - parent_rec_n;
    if (d->delta_n < 0) d->delta_n = 0;   // cannot happen; belt and braces
    if (d->delta_n > 0) {
        d->delta = (RecPrim *)malloc((size_t)d->delta_n * sizeof *d->delta);
        if (!d->delta) {
            free(d->snap);
            free(d->prun);
            free(d);
            return NULL;
        }
        memcpy(d->delta, rs->prims + parent_rec_n,
               (size_t)d->delta_n * sizeof *d->delta);
    }

    double t_cap = sw_now_ms();
    node_snap_capture(d->snap, ctx);
    sw_note(&s_sw_capture, sw_now_ms() - t_cap);

    d->parent = parent;
    if (parent) parent->refcount++;       // the child holds its lineage
    d->refcount = 1;                      // the frontier's reference
    d->seq = ++s_node_seq;
    d->g_days = start_days_left - ctx->g->stats.days_left;

    // Progress, UNIVERSE-NORMALIZED (AP-205): each node's
    // enumeration SHRINKS as objectives complete (dead foes and consumed
    // tiles drop out), so done-within-the-universe is meaningless across
    // nodes. The OPEN count (universe total - done in universe) is the same
    // in every view; true progress = root total - open. planner_report under
    // quiet refreshes done flags and hands back both counts.
    {
        int t_u = 0;
        int d_u = planner_report(ctx, d->prun, &t_u);
        d->open_n = t_u - d_u;
        if (d->open_n < 0) d->open_n = 0;
        d->done_n = obj_total - d->open_n;
    }

    // Candidates are priced LAZILY at the node's first pop, not here: a
    // frontier node that never gets expanded never pays the pricing pass
    // (measured: ~40% of created nodes are never popped).
    d->cand = NULL;
    d->cand_n = -1;                       // unpriced marker
    d->next_cand = 0;
    d->waited = false;
    return d;
}

// Restore `d` as the live world: its full world snapshot + the cross-play
// seams + the rebuilt recording content + a fresh enumeration for the step
// core.
static bool node_restore(ExecCtx *ctx, SearchNode *d) {
    double t_res = sw_now_ms();
    node_snap_restore(d->snap, ctx);
    worldsnap_reset_calendar_dead();
    nav_cache_invalidate();
    recruit_exclusions_reset();
    node_rebuild_recording(d);
    sw_note(&s_sw_restore, sw_now_ms() - t_res);
    double t_open = sw_now_ms();
    bool ok = planner_open(ctx, d->prun);
    sw_note(&s_sw_open, sw_now_ms() - t_open);
    return ok;
}

bool search_run(ExecCtx *ctx, int *out_best_done, int *out_total) {
    int start_days_left = ctx->g->stats.days_left;
    s_progress_cancelled = false;

    memset(&s_sw_step, 0, sizeof s_sw_step);
    memset(&s_sw_open, 0, sizeof s_sw_open);
    memset(&s_sw_cand, 0, sizeof s_sw_cand);
    memset(&s_sw_capture, 0, sizeof s_sw_capture);
    memset(&s_sw_restore, 0, sizeof s_sw_restore);

    BalTree *frontier = baltree_new();
    PlannerRun *scratch_run = (PlannerRun *)calloc(1, sizeof *scratch_run);
    if (!frontier || !scratch_run) {
        free(scratch_run);
        if (frontier) baltree_free(frontier, NULL);
        if (out_best_done) *out_best_done = 0;
        if (out_total) *out_total = 0;
        return false;
    }
    s_node_seq = 0;
    double last_progress_ms = sw_now_ms();   // hook cadence clock (see below)
    recruit_cache_reset();
    recruit_cache_enable(true);
    planner_set_quiet(true);            // expansions print no verdicts

    // The root node: the boot state, planner memory fresh.
    worldsnap_reset_calendar_dead();
    exec_set_wait_allowed(false);
    int obj_total = 0;
    SearchNode *best = NULL;            // most-done state seen (MISS report)
    bool hit = false;
    long expansions = 0, evicted = 0, childless = 0, looped = 0;
    long stat_ok = 0, stat_kept = 0, stat_fail = 0, stat_term = 0;
    long last_best_x = 0, restarts = 0;

    if (!planner_open(ctx, scratch_run)) {
        // Enumeration failure: no verdict possible (caller fails the run).
        recruit_cache_enable(false);
        planner_set_quiet(false);
        baltree_free(frontier, NULL);
        free(scratch_run);
        if (out_best_done) *out_best_done = 0;
        if (out_total) *out_total = 0;
        return false;
    }
    {
        int t = 0;
        planner_report(ctx, scratch_run, &t);   // quiet: only counts
        obj_total = t;
    }
    planner_set_quiet(true);
    SearchNode *root = node_capture(ctx, NULL, recsink_mark() /*as parent len:
                       the sink's current content is the pre-search prefix --
                       the root's delta is empty*/, scratch_run,
                       start_days_left, obj_total);
    if (!root) {
        recruit_cache_enable(false);
        planner_set_quiet(false);
        baltree_free(frontier, NULL);
        free(scratch_run);
        if (out_best_done) *out_best_done = 0;
        if (out_total) *out_total = 0;
        return false;
    }
    // The pre-search recording prefix belongs to the root's "delta" so
    // rebuilds reproduce it: re-point the root delta at the whole sink.
    {
        free(root->delta);
        root->delta = NULL;
        root->delta_n = recsink_save_copy(&root->delta);
        if (root->delta_n < 0) root->delta_n = 0;
    }
    root->sig = sig_live(ctx);
    best = root;
    root->refcount++;                   // `best` holds a reference
    baltree_insert(frontier, node_key(root), root);

    SearchNode *hit_node = NULL;
    while (baltree_size(frontier) > 0) {
        if (++expansions > SEARCH_MAX_EXPANSIONS) {
            watchdog_hit("search-expansions");
            break;
        }
        if (expansions - last_best_x > SEARCH_STAGNATION_CUT &&
            (root->cand_n < 0 || root->next_cand < root->cand_n)) {
            // Doomed region: prune the frontier back to the root and let its
            // next candidate open a fresh descent (see SEARCH_STAGNATION_CUT).
            long dropped = 0;
            for (;;) {
                SearchNode *n2 = (SearchNode *)baltree_pop_min(frontier, NULL);
                if (!n2) break;
                if (n2 == root) continue;       // re-inserted below
                node_unref(n2);
                dropped++;
            }
            baltree_insert(frontier, node_key(root), root);
            restarts++;
            last_best_x = expansions;
            if (ob_diag_verbose()) {
                printf("[SEARCH] restart %ld: stagnant descent pruned "
                       "(%ld nodes); root branch %d/%d opens\n", restarts,
                       dropped, root->next_cand, root->cand_n);
                fflush(stdout);
            }
        }
        SearchNode *d = (SearchNode *)baltree_pop_min(frontier, NULL);

        // First pop: restore and price the candidate list (lazy -- see
        // node_capture). The restore doubles as this visit's restore. The
        // delta base (parent_rec_n) is captured BEFORE the pricing pass:
        // the cycle head runs LOGISTICS, which may record prims and mutate
        // the world -- those prims belong to the CHILD's edge delta, or the
        // rebuilt recording silently loses the stocking trips whose effects
        // its fingerprints assume (measured: all four seeds diverged
        // mid-recording at exactly such an orphaned segment).
        bool restored = false;
        int parent_rec_n = -1;
        if (d->cand_n < 0) {
            if (!node_restore(ctx, d)) { node_unref(d); continue; }
            parent_rec_n = recsink_mark();
            if (!node_fill_candidates(ctx, d)) { node_unref(d); continue; }
            restored = true;
        }

        // A node retires when its true branching is spent
        // (SEARCH_MAX_CHILDREN) or its candidates ran dry. A
        // failure-starved node (no child at all) gets ONE wait-armed
        // re-list (the no-burn law at node granularity) and one
        // sacrifice-escape expansion (the deliberate defeat, AP-056)
        // before it exhausts.
        bool capped = d->parent != NULL &&
                      d->children_made >= SEARCH_MAX_CHILDREN;
        if (capped || d->next_cand >= d->cand_n) {
            bool starved = !capped && d->children_made == 0;
            if (starved && !d->waited && d->cand_n > 0 &&
                !d->prun->wait_armed) {
                if (!node_restore(ctx, d)) { node_unref(d); continue; }
                d->waited = true;
                d->prun->wait_armed = true;
                if (node_fill_candidates(ctx, d) && d->cand_n > 0) {
                    baltree_insert(frontier, node_key(d), d);
                    continue;
                }
            }
            if (starved && !d->sacrificed && d->open_n > 0) {
                d->sacrificed = true;
                if (!node_restore(ctx, d)) { node_unref(d); continue; }
                int parent_rec_n2 = recsink_mark();
                *scratch_run = *d->prun;
                if (planner_step_sacrifice(ctx, scratch_run)) {
                    planner_refresh_done(ctx, scratch_run);
                    SearchNode *esc = node_capture(ctx, d, parent_rec_n2,
                                                   scratch_run,
                                                   start_days_left,
                                                   obj_total);
                    if (esc) {
                        d->children_made++;
                        baltree_insert(frontier, node_key(esc), esc);
                    }
                }
            }
            node_unref(d);              // exhausted: release on discard
            continue;
        }

        const PlanCand *c = &d->cand[d->next_cand++];
        if (!restored) {
            if (!node_restore(ctx, d)) { node_unref(d); continue; }
            parent_rec_n = recsink_mark();
        }
        // One line per expansion: which state the search is standing on and
        // what it is about to try from there. The attempt's own result lands
        // on the [PLANNER] channel.
        if (ob_diag_verbose())
            printf("[SEARCH] node seq=%ld depth=%d done=%d/%d days-left=%d "
                   "branch=%d/%d children=%d try=%s\n",
                   d->seq, d->g_days, d->done_n, obj_total,
                   ctx->g->stats.days_left, d->next_cand, d->cand_n,
                   d->children_made, c->handle);

        bool still_open = d->next_cand < d->cand_n || d->children_made == 0;
        if (still_open)
            baltree_insert(frontier, node_key(d), d);

        *scratch_run = *d->prun;        // the child evolves a COPY
        double t_step = sw_now_ms();
        PlannerStepResult r = planner_step(ctx, scratch_run, c);
        sw_note(&s_sw_step, sw_now_ms() - t_step);
        planner_refresh_done(ctx, scratch_run);   // per-step truth for nodes

        // NOTE: d's frontier reference (when !still_open) is dropped only at
        // the END of this iteration -- a child captured below takes a lineage
        // reference to d first.
        SearchNode *child = NULL;
        if (r == PLANNER_STEP_FAIL || r == PLANNER_STEP_TERMINAL) {
            childless++;
            if (r == PLANNER_STEP_FAIL) stat_fail++; else stat_term++;
            // Persist the failure memory into the node so its remaining
            // branches see it (ordering tiers key off stuck/ever_failed).
            if (r == PLANNER_STEP_FAIL && still_open) *d->prun = *scratch_run;
        } else {
            if (r == PLANNER_STEP_OK) stat_ok++; else stat_kept++;
            int streak = r == PLANNER_STEP_KEPT ? d->kept_streak + 1 : 0;
            uint64_t child_sig = sig_live(ctx);
            bool loop = streak >= SEARCH_MAX_KEPT_STREAK;
            for (const SearchNode *a = d; a && !loop; a = a->parent)
                if (a->sig == child_sig) loop = true;
            // DEAD LEAF: a child born with the calendar exhausted and work
            // still open can only fail every attempt with cause=time --
            // a single line of play dies once there, but a tree would re-drain
            // dead region from every sibling (measured on seed 17: 196k
            // time-failures to the watchdog). Not inserted. Declared trade:
            // an all-zero-day-gate finish from exactly 0 days is given up.
            bool dead = ctx->g->stats.days_left <= 0;
            if (loop) {
                looped++;
            } else if (dead) {
                stat_term++;
            } else {
                child = node_capture(ctx, d, parent_rec_n, scratch_run,
                                     start_days_left, obj_total);
                if (child) {
                    child->sig = child_sig;
                    child->kept_streak = streak;
                    d->children_made++;
                }
            }
        }
        if (!still_open) node_unref(d); // the last frontier reference

        if (child) {
            if (child->done_n > best->done_n) {
                node_unref(best);
                best = child;
                child->refcount++;
                last_best_x = expansions;   // real progress: the cut re-arms
            }
            if (child->open_n == 0) {   // GOAL: every objective done
                hit = true;
                hit_node = child;
                child->refcount++;      // keep through teardown
                baltree_insert(frontier, node_key(child), child);
                break;
            }
            baltree_insert(frontier, node_key(child), child);
            while (baltree_size(frontier) > SEARCH_MAX_LIVE_NODES) {
                node_unref((SearchNode *)baltree_pop_max(frontier, NULL));
                evicted++;
            }
        }

        if ((expansions & 1023) == 0 && !ob_diag_quiet()) {
            printf("[SEARCH] x=%ld done=%d/%d depth=%d frontier=%d "
                   "evict=%ld ok=%ld kept=%ld fail=%ld term=%ld loop=%ld\n",
                   expansions, best->done_n, obj_total, best->g_days,
                   baltree_size(frontier), evicted, stat_ok, stat_kept,
                   stat_fail, stat_term, looped);
            fflush(stdout);
        }
        // Resolve-progress hook (visible mode draws a frame + polls ESC). On a
        // TIME cadence, not an expansion count: an easy seed clears in fewer
        // than one print-interval of expansions, so an expansion gate would
        // never fire the hook -- the bar would sit at its placeholder and ESC
        // would go unpolled for the whole resolve. ~10x/sec keeps the bar
        // live and cancellation responsive for every seed. A false return
        // cancels the search -- the host asked to stop.
        if (s_progress_fn) {
            double now = sw_now_ms();
            if (now - last_progress_ms >= 100.0) {
                last_progress_ms = now;
                if (!s_progress_fn(best->done_n, obj_total, s_progress_ud)) {
                    s_progress_cancelled = true;
                    break;
                }
            }
        }
    }

    // Commit the outcome state: the winning node on a HIT, the most-advanced
    // node on a MISS -- either way its world and rebuilt recording become
    // live, and its planner memory supplies the truthful [UNMET] causes.
    // Nothing is re-simulated to produce the committed line.
    SearchNode *commit = hit ? hit_node : best;
    planner_set_quiet(false);
    s_unmet_label[0] = '\0';
    s_unmet_cause[0] = '\0';
    if (commit) {
        node_restore(ctx, commit);
        planner_set_print_verdict(false);   // the driver owns the verdict line
        int t2 = 0;
        planner_report(ctx, commit->prun, &t2);
        planner_set_print_verdict(true);
        // The first objective left undone, for a validation report.
        planner_first_unmet(commit->prun, s_unmet_label, sizeof s_unmet_label,
                            s_unmet_cause, sizeof s_unmet_cause);
    }

    // Timing: one compact line by default, the full distributions plus the
    // memo hit rate under --verbose. The counters themselves are always kept --
    // they are how every performance question about this search gets answered.
    // All silenced when a caller renders its own report (--validate-pack).
    if (ob_diag_quiet()) {
        /* caller owns the output */
    } else if (ob_diag_verbose()) {
        sw_print("step", &s_sw_step);
        sw_print("open", &s_sw_open);
        sw_print("cand", &s_sw_cand);
        sw_print("capture", &s_sw_capture);
        sw_print("restore", &s_sw_restore);
        long rh = 0, rm = 0;
        recruit_cache_stats(&rh, &rm);
        printf("[SEARCH] recruit-cache: hits=%ld misses=%ld (%.0f%% hit)\n",
               rh, rm, (rh + rm) ? 100.0 * rh / (double)(rh + rm) : 0.0);
    } else {
        printf("[SEARCH] timing");
        sw_brief("step", &s_sw_step);
        sw_brief("open", &s_sw_open);
        sw_brief("cand", &s_sw_cand);
        sw_brief("capture", &s_sw_capture);
        sw_brief("restore", &s_sw_restore);
        printf("\n");
    }
    if (!ob_diag_quiet())
        printf("[SEARCH] done: expansions=%ld best_done=%d/%d frontier=%d "
               "evicted=%ld restarts=%ld ok=%ld kept=%ld fail=%ld term=%ld "
               "loop=%ld %s\n",
               expansions, best ? best->done_n : 0, obj_total,
               baltree_size(frontier), evicted, restarts, stat_ok, stat_kept,
               stat_fail, stat_term, looped, hit ? "HIT" : "MISS");

    int best_done_out = best ? best->done_n : 0;
    if (hit_node) node_unref(hit_node);
    if (best) node_unref(best);
    baltree_free(frontier, node_unref_cb);
    free(scratch_run);
    planner_close();                    // the step core's attempt snapshot
    recruit_cache_enable(false);
    if (out_best_done) *out_best_done = best_done_out;
    if (out_total) *out_total = obj_total;
    return hit;
}
