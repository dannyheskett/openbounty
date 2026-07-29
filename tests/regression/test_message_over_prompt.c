// Issue #19 -- "Simultaneous events obscure battlefield".
//
// One engine step can raise a message AND a decision: stepping onto a treasure
// chest queues the chest result as a REQ_MESSAGE, and the hostile foe that
// walks onto the hero in that same step (GameFoesFollow -> start_foe_hostile_flow)
// opens the attack yes/no prompt right behind it. The shell then has a dialog
// and a prompt up on the same frame.
//
// The defect: the prompt was dispatched first, so answering "yes" entered
// RunCombat with the chest dialog still open. Combat draws an open dialog as a
// centered modal over the battlefield, and the main loop -- which owns dialog
// input -- is suspended inside RunCombat, so the message covered the fight and
// no key could dismiss it.
//
// The contract these tests pin: while a dialog is up, prompt_dispatch_tick
// defers (returns false) so main.c's if/else chain reaches its dialog branch.
// Dismiss the message first, then the prompt answers.

#include "greatest.h"

#include "player_io.h"
#include "pending.h"
#include "prompt.h"
#include "shell_ctx.h"
#include "shell_promptdispatch.h"
#include "ui.h"

#include <stdlib.h>
#include <string.h>

// Game is large; keep it on the heap. Only the player-IO queue is touched, so a
// zeroed Game is a sufficient fixture (no pack/resources needed).
static Game *fresh_game(void) {
    Game *g = calloc(1, sizeof *g);
    if (g) player_io_reset(g);
    return g;
}

// Reproduce what the step leaves behind: the chest message queued first, then
// the attack decision raised with its prompt already open at the emit site.
static void raise_chest_message_then_foe_attack(Game *g) {
    player_io_message(g, NULL, "You found 500 gold!");
    pending_flow = FLOW_ATTACK_FOE;
    memcpy(pending_foe_id, "foe-1", 6);
    prompt_yes_no_open("Foes", "You encounter:\n  20 Orcs\n\nAttack");
    player_io_raise_decision(g, FLOW_ATTACK_FOE, REQ_PROMPT_YES_NO,
                             "Foes", "You encounter:\n  20 Orcs\n\nAttack");
}

static void clear_shell_state(Game *g) {
    dialog_dismiss();
    prompt_dismiss();
    pending_reset();
    if (g) player_io_reset(g);
}

// The step really does leave both outstanding, message first.
TEST step_can_leave_a_message_queued_under_a_live_prompt(void) {
    Game *g = fresh_game(); ASSERT(g);
    clear_shell_state(g);

    raise_chest_message_then_foe_attack(g);

    const PlayerRequest *front = player_io_front(g);
    ASSERT(front);
    ASSERT_EQm("the chest message must sit in FRONT of the attack decision",
               REQ_MESSAGE, front->role);
    ASSERTm("the emit site opened the attack prompt", prompt_is_active());

    clear_shell_state(g);
    free(g);
    PASS();
}

// The regression itself: with the message pumped into the dialog, the prompt
// must NOT dispatch -- dispatching is what ran combat under a live dialog.
TEST prompt_dispatch_defers_while_a_message_dialog_is_up(void) {
    Game *g = fresh_game(); ASSERT(g);
    clear_shell_state(g);

    raise_chest_message_then_foe_attack(g);

    // The shell's per-frame pump moves the queued message into the dialog.
    ASSERTm("pump did not surface the queued message",
            shell_pump_player_io_message(g));
    ASSERT(dialog_is_active());
    ASSERTm("the attack prompt is still up underneath", prompt_is_active());

    ShellCtx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.game = g;

    ASSERT_FALSEm("prompt dispatch must defer to the open dialog "
                  "(dispatching here is what entered combat with the message "
                  "stuck on the battlefield)",
                  prompt_dispatch_tick(&ctx));
    ASSERTm("deferring must not answer or close the prompt", prompt_is_active());
    ASSERT_EQm("the flow is still pending", FLOW_ATTACK_FOE, pending_flow);
    ASSERTm("the dialog is still there for the player to dismiss",
            dialog_is_active());

    clear_shell_state(g);
    free(g);
    PASS();
}

// Once the message is dismissed the prompt owns input again.
TEST prompt_dispatch_resumes_after_the_dialog_is_dismissed(void) {
    Game *g = fresh_game(); ASSERT(g);
    clear_shell_state(g);

    raise_chest_message_then_foe_attack(g);
    shell_pump_player_io_message(g);
    dialog_dismiss();

    ShellCtx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.game = g;

    // No key is pressed in a headless run, so prompt_update returns NONE and
    // the dispatcher just reports "prompt is up" -- input is its again.
    ASSERTm("prompt dispatch must resume once the dialog is gone",
            prompt_dispatch_tick(&ctx));
    ASSERT(prompt_is_active());

    clear_shell_state(g);
    free(g);
    PASS();
}

SUITE(regression_message_over_prompt_suite) {
    RUN_TEST(step_can_leave_a_message_queued_under_a_live_prompt);
    RUN_TEST(prompt_dispatch_defers_while_a_message_dialog_is_up);
    RUN_TEST(prompt_dispatch_resumes_after_the_dialog_is_dismissed);
}
