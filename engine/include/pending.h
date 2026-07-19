#ifndef OB_PENDING_H
#define OB_PENDING_H

// Prompt-flow scratch state shared across modules: the same fields are
// written by the step module and read by the prompt resolver, so they live
// in one shared translation unit rather than in either caller.
//
// The enum tags which prompt flow is pending; the pending_* fields carry
// the parameters that the resolver needs after the user answers. Setting
// these is the responsibility of whichever module opens the prompt;
// engine/player_io.c player_io_answer routes on them and clears
// pending_flow back to FLOW_NONE once the answer is applied.

typedef enum {
    FLOW_NONE = 0,
    FLOW_SEARCH,           // S: "Search (y/n)?"
    FLOW_DISMISS_ARMY,     // D: "Dismiss which troop (1-5)?"
    FLOW_RECRUIT,          // dwelling: "Recruit how many?"
    FLOW_NAVIGATE,         // N: "Go to which continent? (1..5)"
    FLOW_ALCOVE,           // archmage alcove: accept magic lesson? (y/n)
    FLOW_DISMISS_LAST,     // dismiss last army: "sent back to King" (y/n)
    FLOW_SIEGE_MONSTER,    // monster castle: "Lay siege (y/n)?"
    FLOW_SIEGE_VILLAIN,    // villain castle: "Lay siege (y/n)?"
    FLOW_ATTACK_FOE,       // hostile foe: "Attack (y/n)?"
    FLOW_CHEST_CHOICE,     // gold chest: pick gold (A) or leadership (B)
    FLOW_ACCEPT_FRIENDLY,  // friendly foe: "Accept (y/n)?" -- free join
    FLOW_DISCARD_SPELL,    // open-world: selecting a COMBAT spell (uncastable in
                           // the field) offers to DISCARD one charge to free a
                           // spellbook slot (against the max_spells cap): "(y/n)?"
} PendingFlow;

extern PendingFlow pending_flow;

// Dwelling / recruit-prompt scratch.
extern char pending_dwelling_troop[32];
extern char pending_dwelling_zone[24];
extern int  pending_dwelling_x, pending_dwelling_y;

// Friendly-foe accept state -- set by start_foe_friendly_flow, consumed
// by FLOW_ACCEPT_FRIENDLY dispatch.
extern int  pending_friendly_count;
extern char pending_friendly_foe_id[40];

// Audience-with-king two-step state. issues two KB_BottomBox
// calls: trumpet fanfare -> king's message. Stash the second body here so
// the dialog-dismiss path can chain into open_dialog.
extern char pending_audience_message[700];

// Navigate-continent prompt state -- neighbor zone ids in the order shown
// in the numeric picker (1-based: index 0 matches key "1").
extern char pending_nav_zones[5][32];
extern int  pending_nav_count;

// Own-castle / siege carry state. Castle id survives across the visit
// prompt sequence and the FLOW_SIEGE_* y/n -> combat -> resolve chain.
extern char pending_castle_id[24];

// Hostile-foe attack prompt state.
extern char pending_foe_id[24];
extern int  pending_foe_x, pending_foe_y;

// Gold-chest choice prompt state .
extern int pending_chest_gold;
extern int pending_chest_leadership;

// Discard-spell prompt state -- the spellbook index (0..13) of the combat spell
// the player chose to discard; set by dispatch_adventure_spell, consumed by the
// FLOW_DISCARD_SPELL dispatch.
extern int pending_discard_spell_idx;

// Week-end two-screen sequence .
// Set by schedule_week_end (flows.c); drained before each frame's input
// by pump_week_end_dialog (main.c) so the dialog cycles in order.
typedef enum {
    WK_PHASE_NONE = 0,
    WK_PHASE_ASTROLOGY,
    WK_PHASE_BUDGET,
} WeekPhase;
extern WeekPhase pending_week_phase;
extern int       pending_week_id;
extern int       pending_week_paid;
extern int       pending_astrology_troop_idx;

// Reset every pending-flow global to its empty/cleared state. These are process
// globals (historically file-statics in main.c), so they LEAK across in-process
// games -- a stale pending_flow from one game would make the next game's first
// tick try to answer a phantom prompt. GameInit calls this so each fresh game
// starts with no pending decision.
void pending_reset(void);

#endif
