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
#include <stdlib.h>
#include <string.h>

#include "game.h"          // full Game definition (PlayerIoQueue field)
#include "pending.h"       // the decision scratch this queue mirrors
#include "flow_resolve.h"  // flow_apply_* cores + RecruitParams/FriendlyParams
#include "combat.h"        // CombatResult
#include "ui_host.h"       // the host prompt an ask opens

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
    g->player_io.head = 0;
    g->player_io.count = 0;
}

// Make room for one more request: double the ring, oldest entry first at 0.
static bool queue_grow(PlayerIoQueue *q) {
    if (q->count < q->cap) return true;
    int ncap = q->cap > 0 ? q->cap * 2 : 8;
    PlayerRequest *ns = malloc((size_t)ncap * sizeof *ns);
    if (!ns) return false;
    for (int i = 0; i < q->count; i++) ns[i] = q->slot[(q->head + i) % q->cap];
    free(q->slot);
    q->slot = ns;
    q->cap = ncap;
    q->head = 0;
    return true;
}

// Reserve the next free slot at the tail, zero it, set role + text. Returns the
// slot to fill, or NULL if the queue is full.
static PlayerRequest *enqueue(Game *g, ReqRole role,
                              const char *header, const char *body) {
    if (!g) return NULL;
    PlayerIoQueue *q = &g->player_io;
    if (!queue_grow(q)) return NULL;
    int idx = (q->head + q->count) % q->cap;
    PlayerRequest *r = &q->slot[idx];
    memset(r, 0, sizeof *r);
    r->role = role;
    copy_str(r->header, sizeof r->header, header);
    copy_str(r->body, sizeof r->body, body);
    q->count++;
    return r;
}

// ---- one helper per kind ----------------------------------------------------

static PlayerRequest *note_of(Game *g, ReqKind kind, const char *title, const char *body) {
    PlayerRequest *r = enqueue(g, REQ_MESSAGE, title, body);
    if (r) r->kind = kind;
    return r;
}

PlayerRequest *player_io_note(Game *g, const char *title, const char *body) {
    return note_of(g, PIO_NOTE, title, body);
}

PlayerRequest *player_io_note_face(Game *g, const char *title, const char *body,
                                   ReqFace face, int face_index) {
    PlayerRequest *r = note_of(g, PIO_NOTE_FACE, title, body);
    if (r) { r->face = face; r->face_index = face_index; }
    return r;
}

PlayerRequest *player_io_note_in_place(Game *g, const char *title, const char *body) {
    return note_of(g, PIO_NOTE_IN_PLACE, title, body);
}

PlayerRequest *player_io_note_scene(Game *g, const char *title, const char *body,
                                    int scene_index) {
    PlayerRequest *r = note_of(g, PIO_NOTE_SCENE, title, body);
    if (r) { r->face = REQ_FACE_SCENE; r->face_index = scene_index; }
    return r;
}

PlayerRequest *player_io_note_scene_event(Game *g, const char *title,
                                          const char *body, int scene_index) {
    PlayerRequest *r = note_of(g, PIO_NOTE_SCENE, title, body);
    if (r) { r->face = REQ_FACE_EVENT; r->face_index = scene_index; }
    return r;
}

// Evict every queued request of `role`, keeping the rest in order. The queue
// mirrors the authoritative pending_flow / view stack, and only ONE decision and
// ONE view are ever live: a worldsnap restore (plan simulation) can leave a
// stale mirror behind, which would otherwise pile up.
static void evict_role(Game *g, ReqRole role) {
    if (!g) return;
    PlayerIoQueue *q = &g->player_io;
    int kept = 0;
    for (int i = 0; i < q->count; i++) {
        int idx = (q->head + i) % q->cap;
        if (q->slot[idx].role == role) continue;
        int dst = (q->head + kept) % q->cap;
        if (dst != idx) q->slot[dst] = q->slot[idx];
        kept++;
    }
    q->count = kept;
}

// Every ask: the queued request, the mirrored prompt kind, and the host prompt
// the shell renders. `open_host` is false only for the self-answered kind.
static PlayerRequest *ask_of(Game *g, ReqKind kind, PendingFlow flow,
                             ReqPromptKind prompt, const char *title, const char *body,
                             int digits, int max_value, bool open_host) {
    evict_role(g, REQ_DECISION);
    PlayerRequest *r = enqueue(g, REQ_DECISION, title, body);
    if (r) {
        r->kind = kind;
        r->flow = flow;
        r->prompt_kind = prompt;
        r->prompt_digits = digits;
        r->prompt_max = max_value;
    }
    if (open_host) {
        const char *h = title ? title : "";
        const char *b = body ? body : "";
        if (prompt == REQ_PROMPT_YES_NO)      prompt_yes_no_open(h, b);
        else if (prompt == REQ_PROMPT_AB)     prompt_ab_open(h, b);
        else if (prompt == REQ_PROMPT_TEXT)   prompt_text_input_open(h, b, digits, max_value);
        // REQ_PROMPT_NUMERIC: the host opens its own picker (the shell's list).
    }
    if (kind != PIO_ASK_SELF) prompt_set_req_kind(kind);
    return r;
}

PlayerRequest *player_io_ask(Game *g, PendingFlow flow, ReqPromptKind prompt,
                             const char *title, const char *body) {
    return ask_of(g, PIO_ASK, flow, prompt, title, body, 0, 0, true);
}

PlayerRequest *player_io_ask_face(Game *g, PendingFlow flow, ReqPromptKind prompt,
                                  const char *title, const char *body,
                                  ReqFace face, int face_index) {
    PlayerRequest *r = ask_of(g, PIO_ASK_FACE, flow, prompt, title, body, 0, 0, true);
    if (r) { r->face = face; r->face_index = face_index; }
    prompt_set_req_face((int)face, face_index);
    return r;
}

PlayerRequest *player_io_ask_in_place(Game *g, PendingFlow flow, ReqPromptKind prompt,
                                      const char *title, const char *body) {
    return ask_of(g, PIO_ASK_IN_PLACE, flow, prompt, title, body, 0, 0, true);
}

PlayerRequest *player_io_ask_scene(Game *g, PendingFlow flow, ReqPromptKind prompt,
                                   const char *title, const char *body) {
    return ask_of(g, PIO_ASK_SCENE, flow, prompt, title, body, 0, 0, true);
}

PlayerRequest *player_io_ask_number(Game *g, PendingFlow flow,
                                    const char *title, const char *body,
                                    int digits, int max_value) {
    return ask_of(g, PIO_ASK_NUMBER, flow, REQ_PROMPT_TEXT, title, body,
                  digits, max_value, true);
}

PlayerRequest *player_io_ask_number_in_place(Game *g, PendingFlow flow,
                                             const char *title, const char *body,
                                             int digits, int max_value) {
    return ask_of(g, PIO_ASK_NUMBER_IN_PLACE, flow, REQ_PROMPT_TEXT, title, body,
                  digits, max_value, true);
}

PlayerRequest *player_io_ask_choice(Game *g, PendingFlow flow,
                                    const char *title, const char *body, int count) {
    return ask_of(g, PIO_ASK_CHOICE, flow, REQ_PROMPT_NUMERIC, title, body,
                  0, count, true);
}

PlayerRequest *player_io_ask_self(Game *g, PendingFlow flow, ReqPromptKind prompt) {
    return ask_of(g, PIO_ASK_SELF, flow, prompt, NULL, NULL, 0, 0, false);
}

PlayerRequest *player_io_screen(Game *g, ViewKind view, bool replace,
                                const char *title, const char *body) {
    evict_role(g, REQ_VIEW);
    PlayerRequest *r = enqueue(g, REQ_VIEW, title, body);
    if (r) {
        r->kind = PIO_SCREEN;
        r->view = view;
        r->view_replace = replace;
    }
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
    q->head = (q->head + 1) % q->cap;
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
        } else if (pending_foe_bounce) {
            flow_apply_evade_bounce(g, map, pending_foe_id,
                                    pending_foe_back_x, pending_foe_back_y,
                                    pending_foe_back_travel,
                                    pending_foe_back_boat_x, pending_foe_back_boat_y);
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
        pending_foe_id[0] = '\0'; pending_foe_x = pending_foe_y = -1;
        pending_foe_bounce = false; break;
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
