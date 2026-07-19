// autoplay/recording.c -- the write-only recording sink (AP-021, AP-022).

#include "recording.h"

#include <stdlib.h>
#include <string.h>

#include "exec_ledger.h"

static RecSink s_sink;
static bool s_truncated;   // sticky: a push was dropped at capacity

bool recsink_truncated(void) { return s_truncated; }

RecSink *recsink(void) { return &s_sink; }

bool recsink_init(int cap) {
    recsink_free();
    s_truncated = false;
    s_sink.prims = (RecPrim *)calloc((size_t)cap, sizeof(RecPrim));
    if (!s_sink.prims) return false;
    s_sink.cap = cap;
    s_sink.count = 0;
    return true;
}

void recsink_free(void) {
    free(s_sink.prims);
    s_sink.prims = NULL;
    s_sink.count = s_sink.cap = 0;
}

int recsink_mark(void) { return s_sink.count; }

void recsink_rollback(int mark) {
    if (mark >= 0 && mark <= s_sink.count) s_sink.count = mark;
}

int recsink_save_copy(RecPrim **out) {
    if (out) *out = NULL;
    if (!out) return -1;
    if (!s_sink.prims || s_sink.count <= 0) return 0;
    RecPrim *p = (RecPrim *)malloc((size_t)s_sink.count * sizeof *p);
    if (!p) return -1;
    memcpy(p, s_sink.prims, (size_t)s_sink.count * sizeof *p);
    *out = p;
    return s_sink.count;
}

void recsink_restore_copy(const RecPrim *prims, int n) {
    if (!s_sink.prims || n < 0) return;
    if (n > s_sink.cap) n = s_sink.cap;   // unreachable; belt and braces
    if (n > 0 && prims)
        memcpy(s_sink.prims, prims, (size_t)n * sizeof *s_sink.prims);
    s_sink.count = n;
}

// FNV-1a over the state fields that identify "the same world moment" for
// replay purposes (AP-022). Deliberately NOT the whole struct: the fingerprint
// must be cheap and stable across in-memory layout details.
static uint32_t fnv1a(uint32_t h, const void *data, size_t n) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

uint32_t rec_world_fp(const Game *g) {
    uint32_t h = 2166136261u;
    if (!g) return h;
    h = fnv1a(h, g->position.zone, strlen(g->position.zone));
    h = fnv1a(h, &g->position.x, sizeof g->position.x);
    h = fnv1a(h, &g->position.y, sizeof g->position.y);
    h = fnv1a(h, &g->stats.days_left, sizeof g->stats.days_left);
    h = fnv1a(h, &g->stats.gold, sizeof g->stats.gold);
    h = fnv1a(h, &g->stats.leadership_current,
              sizeof g->stats.leadership_current);
    h = fnv1a(h, &g->travel_mode, sizeof g->travel_mode);
    h = fnv1a(h, g->contract.active_id, strlen(g->contract.active_id));
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        h = fnv1a(h, g->army[i].id, strlen(g->army[i].id));
        h = fnv1a(h, &g->army[i].count, sizeof g->army[i].count);
    }
    h = fnv1a(h, g->spells.counts, sizeof g->spells.counts);
    return h;
}

static RecPrim *push(const Game *g) {
    if (!s_sink.prims || s_sink.count >= s_sink.cap) {
        // A dropped push makes the recording unreplayable; the run is failed
        // at the end (recsink_truncated) rather than trusted.
        s_truncated = true;
        watchdog_hit("recsink");
        return NULL;
    }
    RecPrim *p = &s_sink.prims[s_sink.count++];
    memset(p, 0, sizeof *p);
    p->fp = rec_world_fp(g);
    return p;
}

void rec_push_move(const Game *g, int dx, int dy) {
    RecPrim *p = push(g);
    if (!p) return;
    p->kind = REC_MOVE;
    p->dx = (int8_t)dx;
    p->dy = (int8_t)dy;
}

RecPrim *rec_push_answer(const Game *g, int flow, FlowAnswer ans,
                         PlayerIoCombatOutcome outcome) {
    RecPrim *p = push(g);
    if (!p) return NULL;
    p->kind = REC_ANSWER;
    p->flow = (uint8_t)flow;
    p->ans_kind = (uint8_t)ans.kind;
    p->number = ans.number;
    p->outcome = (uint8_t)outcome;
    return p;
}

static void fill_action(RecPrim *p, RecActionKind action, const char *id,
                        int a, int b) {
    p->kind = REC_ACTION;
    p->action = (uint8_t)action;
    p->a = a;
    p->b = b;
    if (id) {
        size_t n = 0;
        while (n + 1 < sizeof p->id && id[n]) { p->id[n] = id[n]; n++; }
        p->id[n] = '\0';
    }
}

void rec_push_action(const Game *g, RecActionKind action, const char *id,
                     int a, int b) {
    RecPrim *p = push(g);
    if (!p) return;
    fill_action(p, action, id, a, b);
}

// Like rec_push_action but stamps a caller-supplied PRE-mutation fingerprint,
// for the rare action whose id is only known after the mutation ran (e.g.
// RA_TAKE_CONTRACT, whose id is GameTakeNextContract's return). Capture
// `pre_fp = rec_world_fp(g)` BEFORE the mutation and pass it here.
void rec_push_action_fp(uint32_t pre_fp, RecActionKind action, const char *id,
                        int a, int b) {
    if (!s_sink.prims || s_sink.count >= s_sink.cap) {
        s_truncated = true;
        watchdog_hit("recsink");
        return;
    }
    RecPrim *p = &s_sink.prims[s_sink.count++];
    memset(p, 0, sizeof *p);
    p->fp = pre_fp;
    fill_action(p, action, id, a, b);
}
