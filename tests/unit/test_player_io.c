// Uniform player-IO request queue mechanics (engine/include/player_io.h).
// Exercises the engine-owned FIFO embedded in Game: enqueue (decision/message/
// view), front/idle peeking, ack popping, answer popping, FIFO ordering,
// capacity bound, and reset. Queue mechanics only -- player_io_answer's flow
// routing is covered by the e2e flow suites.

#include "greatest.h"
#include "player_io.h"
#include "game.h"
#include "pending.h"   // pending_flow -- the routing source of truth
#include "flow_resolve.h"   // flow_apply_discard_spell
#include "flow_answer.h"    // FlowAnswer

#include <stdlib.h>
#include <string.h>

// The queue touches only g->player_io, so a zero-initialized Game on the heap is
// a sufficient, cheap fixture (no pack/resources needed). Heap, not stack: Game
// is large.
static Game *fresh_game(void) {
    Game *g = calloc(1, sizeof *g);
    if (g) player_io_reset(g);
    return g;
}

TEST empty_queue_is_idle(void) {
    Game *g = fresh_game();
    ASSERT(g);
    ASSERT(player_io_idle(g));
    ASSERT_EQ(NULL, player_io_front(g));
    free(g);
    PASS();
}

TEST enqueue_message_then_ack(void) {
    Game *g = fresh_game(); ASSERT(g);

    PlayerRequest *r = player_io_enqueue_message(g, "Found", "100 gold");
    ASSERT(r);
    ASSERT(!player_io_idle(g));

    const PlayerRequest *f = player_io_front(g);
    ASSERT(f);
    ASSERT_EQ(REQ_MESSAGE, f->role);
    ASSERT_STR_EQ("Found", f->header);
    ASSERT_STR_EQ("100 gold", f->body);

    player_io_ack(g);
    ASSERT(player_io_idle(g));
    free(g);
    PASS();
}

TEST enqueue_view_then_ack(void) {
    Game *g = fresh_game(); ASSERT(g);

    PlayerRequest *r = player_io_enqueue_view(g, VIEW_DWELLING, "Plains", "");
    ASSERT(r);
    const PlayerRequest *f = player_io_front(g);
    ASSERT_EQ(REQ_VIEW, f->role);
    ASSERT_EQ(VIEW_DWELLING, f->view);

    player_io_ack(g);
    ASSERT(player_io_idle(g));
    free(g);
    PASS();
}

// ack must NOT pop a decision (a decision needs an answer, not an ack).
TEST ack_does_not_pop_a_decision(void) {
    Game *g = fresh_game(); ASSERT(g);

    // Use FLOW_DISMISS_LAST: the router keys off the global pending_flow
    // (not the queue front -- see player_io.c routing note), and this flow's
    // apply-core (flow_apply_dismiss_last) reads ONLY the answer, touching no
    // Game/Resources state -- safe on a bare calloc'd Game. This test is about
    // role discipline (ack vs answer), not flow routing (covered e2e).
    pending_flow = FLOW_DISMISS_LAST;
    player_io_enqueue_decision(g, FLOW_DISMISS_LAST, REQ_PROMPT_YES_NO,
                               "Decide", "?");
    ASSERT(!player_io_idle(g));
    player_io_ack(g);                       // wrong tool for a decision
    ASSERT(!player_io_idle(g));             // still outstanding
    const PlayerRequest *f = player_io_front(g);
    ASSERT_EQ(REQ_DECISION, f->role);
    ASSERT_EQ(REQ_PROMPT_YES_NO, f->prompt_kind);

    // answer pops it (CANCEL -> dismiss-last not confirmed, no state touched).
    FlowAnswer ans = { .kind = FLOW_ANS_CANCEL, .number = 0 };
    player_io_answer(g, NULL, NULL, NULL, ans, PLAYER_IO_COMBAT_NOT_RUN, NULL);
    ASSERT(player_io_idle(g));
    pending_flow = FLOW_NONE;               // tidy the global for later tests
    free(g);
    PASS();
}

// answer must NOT pop a message (the front is a MESSAGE, not the matching
// decision -- even though pending_flow is set, the router only pops a front whose
// role is REQ_DECISION and whose flow matches).
TEST answer_does_not_pop_a_message(void) {
    Game *g = fresh_game(); ASSERT(g);
    player_io_enqueue_message(g, "Note", "hi");
    pending_flow = FLOW_DISMISS_LAST;       // a live decision, but no queued one
    FlowAnswer ans = { .kind = FLOW_ANS_CANCEL, .number = 0 };
    player_io_answer(g, NULL, NULL, NULL, ans,
                     PLAYER_IO_COMBAT_NOT_RUN, NULL);   // routes flow, but...
    ASSERT(!player_io_idle(g));                   // ...the MESSAGE is still queued
    pending_flow = FLOW_NONE;
    player_io_ack(g);
    ASSERT(player_io_idle(g));
    free(g);
    PASS();
}

// Requests drain in FIFO order regardless of role.
TEST fifo_ordering(void) {
    Game *g = fresh_game(); ASSERT(g);
    player_io_enqueue_message(g, "A", "");
    player_io_enqueue_view(g, VIEW_TOWN, "B", "");
    player_io_enqueue_message(g, "C", "");

    ASSERT_STR_EQ("A", player_io_front(g)->header);
    player_io_ack(g);
    ASSERT_STR_EQ("B", player_io_front(g)->header);
    player_io_ack(g);
    ASSERT_STR_EQ("C", player_io_front(g)->header);
    player_io_ack(g);
    ASSERT(player_io_idle(g));
    free(g);
    PASS();
}

// The ring buffer wraps correctly: enqueue+drain past capacity many times keeps
// FIFO order and never corrupts.
TEST ring_wraps_cleanly(void) {
    Game *g = fresh_game(); ASSERT(g);
    char buf[16];
    for (int round = 0; round < 50; round++) {
        snprintf(buf, sizeof buf, "m%d", round);
        ASSERT(player_io_enqueue_message(g, buf, ""));
        ASSERT_STR_EQ(buf, player_io_front(g)->header);
        player_io_ack(g);
        ASSERT(player_io_idle(g));
    }
    free(g);
    PASS();
}

// Filling to capacity succeeds; a further enqueue silently drops the OLDEST entry
// to make room (returns the new slot, not NULL) and never overflows. The queue
// stays at CAP, and the newest request is retained at the tail.
TEST capacity_bound_drops_overflow(void) {
    Game *g = fresh_game(); ASSERT(g);
    for (int i = 0; i < PLAYER_IO_QUEUE_CAP; i++)
        ASSERT(player_io_enqueue_message(g, "x", ""));
    // Queue is full; one more evicts the oldest and is accepted (non-NULL).
    ASSERT(player_io_enqueue_message(g, "overflow", ""));
    // Still exactly CAP requests, and the last one drained is the newcomer.
    int drained = 0;
    const char *last = "";
    while (!player_io_idle(g)) { last = player_io_front(g)->header; player_io_ack(g); drained++; }
    ASSERT_EQ(PLAYER_IO_QUEUE_CAP, drained);
    ASSERT_STR_EQ("overflow", last);
    free(g);
    PASS();
}

// reset clears a non-empty queue back to idle.
TEST reset_clears_queue(void) {
    Game *g = fresh_game(); ASSERT(g);
    player_io_enqueue_message(g, "a", "");
    player_io_enqueue_view(g, VIEW_TOWN, "b", "");
    ASSERT(!player_io_idle(g));
    player_io_reset(g);
    ASSERT(player_io_idle(g));
    ASSERT_EQ(NULL, player_io_front(g));
    free(g);
    PASS();
}

// Long header/body are truncated to the buffer, always NUL-terminated (no
// overflow of the fixed PlayerRequest text fields).
TEST long_text_is_truncated_safely(void) {
    Game *g = fresh_game(); ASSERT(g);
    char big[PLAYER_IO_BODY_CAP + 200];
    memset(big, 'Z', sizeof big);
    big[sizeof big - 1] = '\0';
    PlayerRequest *r = player_io_enqueue_message(g, big, big);
    ASSERT(r);
    ASSERT(strlen(r->header) < (size_t)PLAYER_IO_HEADER_CAP);
    ASSERT(strlen(r->body)   < (size_t)PLAYER_IO_BODY_CAP);
    free(g);
    PASS();
}

// player_io_message enqueues a REQ_MESSAGE; drain_messages acks all leading
// messages but stops at a decision.
TEST drain_messages_stops_at_decision(void) {
    Game *g = fresh_game(); ASSERT(g);
    player_io_message(g, "A", "");
    player_io_message(g, "B", "");
    player_io_enqueue_decision(g, FLOW_ALCOVE, REQ_PROMPT_YES_NO, "C", "");
    player_io_message(g, "D", "");   // after the decision -- must NOT be drained

    int n = player_io_drain_messages(g);
    ASSERT_EQ(2, n);                 // A, B drained; stopped at the decision
    const PlayerRequest *f = player_io_front(g);
    ASSERT_EQ(REQ_DECISION, f->role);
    ASSERT_EQ(FLOW_ALCOVE, f->flow);
    free(g);
    PASS();
}

// raise_decision evicts a stale queued decision (one live decision invariant)
// but preserves queued messages -- the fix for stale decision mirrors accumulating
// across worldsnap restores in plan simulation.
TEST raise_decision_evicts_stale_decision_keeps_messages(void) {
    Game *g = fresh_game(); ASSERT(g);
    player_io_message(g, "msg1", "");
    player_io_raise_decision(g, FLOW_SIEGE_MONSTER, REQ_PROMPT_YES_NO, "old", "");
    player_io_message(g, "msg2", "");
    // Raise a new decision: the OLD decision is evicted, both messages kept.
    player_io_raise_decision(g, FLOW_ATTACK_FOE, REQ_PROMPT_YES_NO, "new", "");

    // Expect queue: [msg1, msg2, new-decision] -- exactly one decision, FIFO msgs.
    const PlayerRequest *f = player_io_front(g);
    ASSERT_STR_EQ("msg1", f->header);
    player_io_ack(g);
    ASSERT_STR_EQ("msg2", player_io_front(g)->header);
    player_io_ack(g);
    const PlayerRequest *d = player_io_front(g);
    ASSERT_EQ(REQ_DECISION, d->role);
    ASSERT_EQ(FLOW_ATTACK_FOE, d->flow);   // the NEW decision, not the stale one
    free(g);
    PASS();
}

// raise_view enqueues a REQ_VIEW carrying the replace flag + context, and
// drain_messages (passive drain) acks views as well as messages.
TEST raise_view_and_passive_drain(void) {
    Game *g = fresh_game(); ASSERT(g);
    PlayerRequest *r = player_io_raise_view(g, VIEW_TOWN, /*replace=*/true,
                                            NULL, NULL);
    ASSERT(r);
    const PlayerRequest *f = player_io_front(g);
    ASSERT_EQ(REQ_VIEW, f->role);
    ASSERT_EQ(VIEW_TOWN, f->view);
    ASSERT(f->view_replace);
    // Passive drain acks views (and messages), so autoplay never accumulates one.
    player_io_message(g, "m", "");                  // a trailing message too
    int n = player_io_drain_messages(g);
    ASSERT_EQ(2, n);                                // the view + the message
    ASSERT(player_io_idle(g));
    free(g);
    PASS();
}

// raise_view evicts a stale queued view (one-live-view invariant) but keeps
// messages/decisions -- the fix for stale view mirrors across worldsnap restores.
TEST raise_view_evicts_stale_view_keeps_others(void) {
    Game *g = fresh_game(); ASSERT(g);
    player_io_message(g, "msg", "");
    player_io_raise_view(g, VIEW_DWELLING, false, NULL, NULL);   // stale view
    player_io_raise_view(g, VIEW_ALCOVE, false, NULL, NULL);     // new view

    // Queue: [msg, alcove-view] -- the dwelling view was evicted, message kept.
    ASSERT_STR_EQ("msg", player_io_front(g)->header);
    player_io_ack(g);
    const PlayerRequest *v = player_io_front(g);
    ASSERT_EQ(REQ_VIEW, v->role);
    ASSERT_EQ(VIEW_ALCOVE, v->view);   // the NEW view, not the stale dwelling
    free(g);
    PASS();
}

// ---- FLOW_DISCARD_SPELL core (open-world combat-spell discard) -------------
// flow_apply_discard_spell touches only g->spells.counts[], so a zeroed Game is a
// sufficient fixture. YES frees one charge; NO/cancel/invalid/empty are no-ops.

TEST discard_spell_yes_frees_one_charge(void) {
    Game *g = fresh_game();
    ASSERT(g);
    g->spells.counts[2] = 3;                 // 3 charges of combat spell idx 2
    FlowAnswer yes = { .kind = FLOW_ANS_YES };
    flow_apply_discard_spell(g, 2, yes);
    ASSERT_EQ(2, g->spells.counts[2]);       // one freed
    flow_apply_discard_spell(g, 2, yes);
    ASSERT_EQ(1, g->spells.counts[2]);       // a second press frees another
    free(g);
    PASS();
}

TEST discard_spell_no_is_noop(void) {
    Game *g = fresh_game();
    ASSERT(g);
    g->spells.counts[2] = 3;
    FlowAnswer no = { .kind = FLOW_ANS_NO };
    flow_apply_discard_spell(g, 2, no);
    ASSERT_EQ(3, g->spells.counts[2]);       // unchanged
    FlowAnswer cancel = { .kind = FLOW_ANS_CANCEL };
    flow_apply_discard_spell(g, 2, cancel);
    ASSERT_EQ(3, g->spells.counts[2]);
    free(g);
    PASS();
}

TEST discard_spell_guards_empty_and_invalid(void) {
    Game *g = fresh_game();
    ASSERT(g);
    FlowAnswer yes = { .kind = FLOW_ANS_YES };
    g->spells.counts[5] = 0;
    flow_apply_discard_spell(g, 5, yes);     // already empty -> no underflow
    ASSERT_EQ(0, g->spells.counts[5]);
    flow_apply_discard_spell(g, -1, yes);    // invalid index -> safe no-op
    flow_apply_discard_spell(g, 99, yes);    // out of range -> safe no-op
    flow_apply_discard_spell(NULL, 0, yes);  // NULL game -> safe no-op
    free(g);
    PASS();
}

SUITE(unit_player_io_suite) {
    RUN_TEST(discard_spell_yes_frees_one_charge);
    RUN_TEST(discard_spell_no_is_noop);
    RUN_TEST(discard_spell_guards_empty_and_invalid);
    RUN_TEST(empty_queue_is_idle);
    RUN_TEST(drain_messages_stops_at_decision);
    RUN_TEST(raise_decision_evicts_stale_decision_keeps_messages);
    RUN_TEST(raise_view_and_passive_drain);
    RUN_TEST(raise_view_evicts_stale_view_keeps_others);
    RUN_TEST(enqueue_message_then_ack);
    RUN_TEST(enqueue_view_then_ack);
    RUN_TEST(ack_does_not_pop_a_decision);
    RUN_TEST(answer_does_not_pop_a_message);
    RUN_TEST(fifo_ordering);
    RUN_TEST(ring_wraps_cleanly);
    RUN_TEST(capacity_bound_drops_overflow);
    RUN_TEST(reset_clears_queue);
    RUN_TEST(long_text_is_truncated_safely);
}
