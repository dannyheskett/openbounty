// engine/player_io.c -- uniform player-IO request queue. See player_io.h.
//
// The queue is a flat FIFO living inside Game: enqueue / front / idle / ack,
// with answer = pop. It is the authoritative transport for informational
// messages and view-opens; player_io_answer routes each decision to the
// matching flow_apply_* core in flow_resolve.c, so every consumer -- the shell
// prompt dispatcher, the autoplay responder, the demo agent -- resolves a
// decision through this one seam rather than through a per-channel callback.

#include "player_io.h"

#include <stdio.h>
#include <string.h>

#include "game.h"          // full Game definition (PlayerIoQueue field)
#include "pending.h"       // the decision scratch this queue mirrors
#include "flow_resolve.h"  // flow_apply_* cores + RecruitParams/FriendlyParams
#include "combat.h"        // CombatResult

// Copy a possibly-NULL C string into a fixed buffer, always NUL-terminated.
static void copy_str(char *dst, int cap, const char *src) {
    if (cap <= 0) return;
    if (!src) { dst[0] = '\0'; return; }
    int n = (int)strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, (size_t)n);
    dst[n] = '\0';
}

void player_io_reset(Game *g) {
    if (!g) return;
    memset(&g->player_io, 0, sizeof g->player_io);
    g->player_io.head = 0;
    g->player_io.count = 0;
}

// Reserve the next free slot at the tail, zero it, set role + text. Returns the
// slot to fill, or NULL if the queue is full.
static PlayerRequest *enqueue(Game *g, ReqRole role,
                              const char *header, const char *body) {
    if (!g) return NULL;
    PlayerIoQueue *q = &g->player_io;
    if (q->count >= PLAYER_IO_QUEUE_CAP) {
        // Full queue: silently drop the OLDEST entry to make room for the new one.
        q->head = (q->head + 1) % PLAYER_IO_QUEUE_CAP;
        q->count--;
    }
    int idx = (q->head + q->count) % PLAYER_IO_QUEUE_CAP;
    PlayerRequest *r = &q->slot[idx];
    memset(r, 0, sizeof *r);
    r->role = role;
    copy_str(r->header, sizeof r->header, header);
    copy_str(r->body, sizeof r->body, body);
    q->count++;
    return r;
}

PlayerRequest *player_io_enqueue_decision(Game *g, PendingFlow flow,
                                          ReqPromptKind kind,
                                          const char *header,
                                          const char *body) {
    PlayerRequest *r = enqueue(g, REQ_DECISION, header, body);
    if (!r) return NULL;
    r->flow = flow;
    r->prompt_kind = kind;
    return r;
}

PlayerRequest *player_io_enqueue_message(Game *g,
                                         const char *header, const char *body) {
    return enqueue(g, REQ_MESSAGE, header, body);
}

PlayerRequest *player_io_enqueue_view(Game *g, ViewKind view,
                                      const char *header, const char *body) {
    PlayerRequest *r = enqueue(g, REQ_VIEW, header, body);
    if (!r) return NULL;
    r->view = view;
    return r;
}

PlayerRequest *player_io_raise_decision(Game *g, PendingFlow flow,
                                        ReqPromptKind kind,
                                        const char *header, const char *body) {
    // The queue is a MIRROR of the authoritative pending_flow, and the
    // engine raises exactly ONE decision at a time. Worldsnap restore (plan
    // simulation) can leave a stale decision mirror in the queue (the queue is in
    // Game and reverts; pending_flow does not), so before mirroring the new
    // decision, evict any decision already queued -- there is only ever one live.
    // Messages are left intact (they drain on their own). Without this, stale
    // decision mirrors accumulate across simulations and overflow the queue.
    if (g) {
        PlayerIoQueue *q = &g->player_io;
        int kept = 0;
        for (int i = 0; i < q->count; i++) {
            int idx = (q->head + i) % PLAYER_IO_QUEUE_CAP;
            if (q->slot[idx].role == REQ_DECISION) continue;   // drop stale decision
            // Compact kept (message) entries toward the head.
            int dst = (q->head + kept) % PLAYER_IO_QUEUE_CAP;
            if (dst != idx) q->slot[dst] = q->slot[idx];
            kept++;
        }
        q->count = kept;
    }
    return player_io_enqueue_decision(g, flow, kind, header, body);
}

PlayerRequest *player_io_message(Game *g, const char *header, const char *body) {
    return player_io_enqueue_message(g, header, body);
}

PlayerRequest *player_io_raise_view(Game *g, ViewKind view, bool replace,
                                    const char *header, const char *body) {
    // One engine view is presented at a time, like decisions. A stale REQ_VIEW
    // can linger in the queue after a worldsnap restore (queue is in Game and
    // reverts; the shell stack does not), so evict any already-queued view before
    // mirroring the new one -- keep messages/decisions intact. Without this, stale
    // view mirrors accumulate across plan simulations and overflow the queue.
    if (g) {
        PlayerIoQueue *q = &g->player_io;
        int kept = 0;
        for (int i = 0; i < q->count; i++) {
            int idx = (q->head + i) % PLAYER_IO_QUEUE_CAP;
            if (q->slot[idx].role == REQ_VIEW) continue;   // drop stale view
            int dst = (q->head + kept) % PLAYER_IO_QUEUE_CAP;
            if (dst != idx) q->slot[dst] = q->slot[idx];
            kept++;
        }
        q->count = kept;
    }
    PlayerRequest *r = player_io_enqueue_view(g, view, header, body);
    if (!r) return NULL;
    r->view_replace = replace;
    return r;
}

const PlayerRequest *player_io_front(const Game *g) {
    if (!g) return NULL;
    const PlayerIoQueue *q = &g->player_io;
    if (q->count <= 0) return NULL;
    return &q->slot[q->head];
}

bool player_io_idle(const Game *g) {
    return !g || g->player_io.count <= 0;
}

// Pop the front request (advance head, shrink count). Internal helper.
static void pop_front(Game *g) {
    PlayerIoQueue *q = &g->player_io;
    if (q->count <= 0) return;
    q->head = (q->head + 1) % PLAYER_IO_QUEUE_CAP;
    q->count--;
    if (q->count == 0) q->head = 0;   // normalize when empty (tidy snapshots)
}

// Translate the host-supplied combat outcome into the engine CombatResult the
// combat-bearing apply-cores expect.
static CombatResult to_combat_result(PlayerIoCombatOutcome o) {
    return (o == PLAYER_IO_COMBAT_WON) ? COMBAT_RESULT_WIN : COMBAT_RESULT_LOSS;
}

void player_io_answer(Game *g, Map *map, Fog *fog, const Resources *res,
                      FlowAnswer ans, PlayerIoCombatOutcome outcome,
                      PlayerIoPresentation *out_pres) {
    PlayerIoPresentation pres;
    memset(&pres, 0, sizeof pres);
    pres.dismiss_view = VIEW_NONE;
    pres.chain_slot = -1;

    if (!g) { if (out_pres) *out_pres = pres; return; }

    // ROUTE ON THE GLOBAL pending_flow, not the queue front. pending_flow is
    // the authoritative source of truth for which decision is live: it is what
    // the emit sites set, what both hosts' step-gates check, and -- unlike the
    // per-Game queue -- it survives worldsnap restore (the queue is part of
    // Game and reverts on restore, so during plan simulation the queue front and
    // pending_flow can momentarily disagree; routing on the queue would then
    // apply the WRONG flow and corrupt the simulated plan). The queue is a
    // mirror kept in lockstep by popping it here.
    PendingFlow flow = pending_flow;
    if (flow == FLOW_NONE) { if (out_pres) *out_pres = pres; return; }

    // The decision payload is mirrored in the pending_* scratch (the emit
    // sites set both). The engine performs only state mutation; combat is already
    // resolved (outcome) and rendering / temp-death / week-end / view-dismiss are
    // returned as directives.
    switch (flow) {
    case FLOW_SEARCH: {
        bool won = false, game_over = false; int wk = 0;
        flow_apply_search(g, res, ans, &won, &game_over, &wk);
        pres.won_game = won;
        pres.game_over = game_over;
        pres.week_commission = wk;
        break;
    }
    case FLOW_DISMISS_ARMY: {
        int slot = -1;
        bool chain = flow_apply_dismiss_army(g, ans, &slot);
        if (chain && slot >= 0) {
            pres.chain_dismiss_last = true;
            pres.chain_slot = slot;
        }
        break;
    }
    case FLOW_DISMISS_LAST:
        if (flow_apply_dismiss_last(ans)) pres.temp_death = true;
        break;
    case FLOW_SIEGE_MONSTER:
        if (outcome != PLAYER_IO_COMBAT_NOT_RUN) {
            if (flow_apply_siege_monster(g, pending_castle_id,
                                         to_combat_result(outcome)))
                pres.temp_death = true;
        }
        break;
    case FLOW_SIEGE_VILLAIN:
        if (outcome != PLAYER_IO_COMBAT_NOT_RUN) {
            if (flow_apply_siege_villain(g, res, pending_castle_id,
                                         to_combat_result(outcome)))
                pres.temp_death = true;
        }
        break;
    case FLOW_ATTACK_FOE:
        if (outcome != PLAYER_IO_COMBAT_NOT_RUN) {
            if (flow_apply_attack_foe(g, map, pending_foe_id,
                                      pending_foe_x, pending_foe_y,
                                      to_combat_result(outcome)))
                pres.temp_death = true;
        }
        break;
    case FLOW_CHEST_CHOICE:
        flow_apply_chest_choice(g, pending_chest_gold, pending_chest_leadership,
                                ans);
        break;
    case FLOW_DISCARD_SPELL:
        flow_apply_discard_spell(g, pending_discard_spell_idx, ans);
        break;
    case FLOW_ALCOVE:
        flow_apply_alcove(g, map, res, ans);
        pres.dismiss_view = VIEW_ALCOVE;
        break;
    case FLOW_RECRUIT: {
        RecruitParams params = {
            .troop_id = pending_dwelling_troop,
            .zone     = pending_dwelling_zone,
            .x        = pending_dwelling_x,
            .y        = pending_dwelling_y,
        };
        flow_apply_recruit(g, &params, ans);
        pres.dismiss_view = VIEW_DWELLING;
        break;
    }
    case FLOW_ACCEPT_FRIENDLY: {
        FriendlyParams params = {
            .troop_id = pending_dwelling_troop,
            .count    = pending_friendly_count,
            .foe_id   = pending_friendly_foe_id,
            .zone     = pending_dwelling_zone,
            .x        = pending_dwelling_x,
            .y        = pending_dwelling_y,
        };
        flow_apply_accept_friendly(g, map, &params, ans);
        break;
    }
    case FLOW_NAVIGATE: {
        int wk = 0;
        if (flow_apply_navigate(g, map, fog,
                                (const char (*)[32])pending_nav_zones,
                                pending_nav_count, ans, &wk))
            pres.week_commission = wk;
        break;
    }
    case FLOW_NONE: default: break;
    }

    // Pop the mirror queue's front IF it matches the flow we just answered (it
    // may be empty/stale after a worldsnap restore -- see the routing note above;
    // in that case there is nothing to pop and pending_flow stays the truth).
    {
        const PlayerRequest *front = player_io_front(g);
        if (front && front->role == REQ_DECISION && front->flow == flow)
            pop_front(g);
    }
    // Clear the mirrored scratch for the flow we just answered (the hosts used
    // to do this per-case; centralized here so neither drains stale state).
    PendingFlow answered = flow;
    // A chained DISMISS_LAST is opened by the caller; do NOT clear pending_flow
    // to FLOW_NONE here in that case (the caller raises the next decision).
    if (!pres.chain_dismiss_last) pending_flow = FLOW_NONE;
    switch (answered) {
    case FLOW_CHEST_CHOICE:
        pending_chest_gold = 0; pending_chest_leadership = 0; break;
    case FLOW_DISCARD_SPELL:
        pending_discard_spell_idx = -1; break;
    case FLOW_SIEGE_MONSTER:
    case FLOW_SIEGE_VILLAIN:
        pending_castle_id[0] = '\0'; break;
    case FLOW_ATTACK_FOE:
        pending_foe_id[0] = '\0'; pending_foe_x = pending_foe_y = -1; break;
    case FLOW_RECRUIT:
        pending_dwelling_troop[0] = '\0'; pending_dwelling_zone[0] = '\0';
        pending_dwelling_x = pending_dwelling_y = -1; break;
    case FLOW_ACCEPT_FRIENDLY:
        pending_dwelling_troop[0] = '\0'; pending_dwelling_zone[0] = '\0';
        pending_friendly_foe_id[0] = '\0'; pending_friendly_count = 0;
        pending_dwelling_x = pending_dwelling_y = -1; break;
    case FLOW_NAVIGATE:
        pending_nav_count = 0; break;
    default: break;
    }

    if (out_pres) *out_pres = pres;
}

void player_io_ack(Game *g) {
    if (!g) return;
    const PlayerRequest *r = player_io_front(g);
    if (!r) return;
    if (r->role == REQ_DECISION) return;   // decisions need an answer, not an ack
    pop_front(g);
}

int player_io_drain_messages(Game *g) {
    if (!g) return 0;
    int n = 0;
    for (;;) {
        const PlayerRequest *r = player_io_front(g);
        // Drain passive requests (messages AND views); stop at a decision, which
        // must be answered, not acked.
        if (!r || r->role == REQ_DECISION) break;
        pop_front(g);
        n++;
    }
    return n;
}
