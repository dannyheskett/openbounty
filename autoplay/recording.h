// autoplay/recording.h
//
// The recorded-primitive types for the pre-planned executor (WS-4).
//
// plan_build proves each committed step by simulation; as it proves a step it
// RECORDS the exact primitives that completed it — the (dx,dy) adventure moves
// and the prompt answers (with the combat outcome + per-turn CombatTurnRecord
// for a fight). The executor (plan_exec_step) then APPLIES this recording on the
// live world with NO planner, NO nav search, and NO re-planning at execute time.
// Each fight IS re-resolved at execute time, but by the SAME deterministic engine
// combat the planner used to prove it (a fight is a pure function of seed +
// encounter identity + mode), so the proven outcome recurs exactly and execution
// re-decides nothing. Headless and visible reach the IDENTICAL world state; the
// only mode difference is that visible mode ANIMATES the recorded CombatTurnRecord
// (WS-7) before that re-resolved outcome is applied — a passive draw that never
// touches world state.
//
// This is what makes the final run a single, pre-decided, fully deterministic
// path (the user's binding contract) and fixes the intervention-drop bug (an
// intervention's primitives live in the same recording, applied in order
// regardless of step index).
//
// Engine-only: all referenced types are engine headers.

#ifndef OB_AUTOPLAY_RECORDING_H
#define OB_AUTOPLAY_RECORDING_H

#include <stdint.h>

#include "flow_answer.h"   // FlowAnswer
#include "player_io.h"     // PlayerIoCombatOutcome
#include "pending.h"       // PendingFlow
#include "combat.h"        // CombatTurnRecord, CombatMode

// One recorded primitive: an adventure move, an answered flow, or a direct
// engine action the planner performed WITHOUT a flow (garrison / recruit-home /
// rent-boat / take-contract / buy-siege). The executor replays each verbatim, so
// every state mutation the proving simulation made is reproduced with no
// re-decision.
typedef enum {
    REC_MOVE = 0,   // GameStep(dx,dy) — one adventure step
    REC_ANSWER,     // player_io_answer of a pending flow
    REC_ACTION,     // a direct engine action (no flow) — see RecActionKind
} RecPrimKind;

// Direct engine actions the planner performs inline (not via a pending flow).
typedef enum {
    RA_GARRISON_WEAKEST = 0,  // garrison weakest surviving stack into act_id castle
    RA_RECRUIT_HOME,          // GameBuyTroop over the WHOLE home pool (no target)
    RA_RENT_BOAT,             // GameRentBoat(act_x, act_y, current zone)
    RA_TAKE_CONTRACT,         // GameTakeNextContract once
    RA_BUY_SIEGE,             // GameBuySiege
    RA_RECRUIT_TYPE,          // GameBuyTroop(act_id, max) — one specific troop type
                              // (home-pool subset for the RO army target)
    RA_DISMISS_TYPE,          // dismiss the army stack whose id == act_id (free slot)
    RA_SET_ARMY,              // recruit-optimizer (RO): atomically REPLACE the army
                              // with the proven-winning composition in set_army[] —
                              // dismiss every current stack, then GameBuyTroop each
                              // set_army[i] at its exact count. One primitive so the
                              // executor + visible run reproduce the re-composition
                              // byte-identically (no fragile multi-tick routing).
    RA_BUY_SPELLS,            // GameBuySpell(act_id) — buy the combat spell for sale
                              // at town act_id (spell economy).
    RA_RECRUIT_TYPE_N,        // GameBuyTroop(act_id, act_x) — buy EXACTLY act_x of one
                              // troop type (the minimal garrisonable 2nd stack before
                              // a monster-castle siege, WS-1 RC#1). Distinct from
                              // RA_RECRUIT_TYPE (which buys the leadership/gold max):
                              // an exact small count keeps gold for downstream boats.
    RA_CAST_ADV_SPELL,        // GameApplyAdventureSpellEffect(g, act_x) — apply the
                              // adventure spell at index act_x (the generic lever:
                              // raise_control leadership boost etc.). Recorded right
                              // before the RA_SET_ARMY rebuild it enables, and replayed
                              // in the SAME executor step (many effects are temporary,
                              // e.g. the leadership boost resets at a week boundary).
    RA_WAIT_WEEK,             // GameSpendWeek — strategic wait banking commission
                              // income (offered only when a strictly-stronger
                              // army recompose becomes affordable afterward).
                              // One primitive per waited week for exact replay.
    RA_DISCARD_SPELL,         // discard ONE charge of spell index act_x (the
                              // engine's own field-discard mechanic,
                              // flow_apply_discard_spell): frees spellbook
                              // room against the max_spells cap so the assault
                              // purchases fit. Windfall charges (chests) can
                              // blow far past the cap, which only gates BUYING.
    RA_GATE_TELEPORT,         // GameGateTeleport to a VISITED castle via the
                              // gate spell: zero days, any boat left behind in
                              // the origin zone, the charge consumed inside the
                              // engine call. act_id = destination ZONE id,
                              // act_x/act_y = the gate tile (gate destinations
                              // carry no stable id — matched by zone + tile).
                              // Used by the boss-assault sequence so a
                              // week-transient leadership boost survives until
                              // the siege.
    RA_TAKE_OFF,              // mount RIDE -> FLY. Legal only when the hero is
                              // riding and EVERY army stack is a flying troop
                              // with skill >= 2 (GamePlayerCanFly) — the same
                              // gate as the shell's fly action. In flight all
                              // tiles are passable and interactives don't fire.
    RA_LAND,                  // mount FLY -> RIDE. Legal only on plain grass
                              // with no interactive overlay and no foot blocker
                              // — the same gate as the shell's land action.
    RA_TRAVEL_ZONE,           // GameSwitchZone(act_id) + GameSpendWeek — cross-zone
                              // travel. Legality is identical to the shell's zone
                              // picker: the hero must be sailing (TRAVEL_BOAT) and
                              // the target zone discovered (its navmap collected).
                              // Spends the remainder of the current week, with normal
                              // week-end processing. The applier calls the same
                              // engine core the shell uses, so headless and visible
                              // replays reproduce the switch byte-identically.
    RA_SEARCH,                // FLOW_SEARCH on the current tile (the scepter dig).
                              // On the buried-scepter tile this recovers the
                              // scepter and wins (flow_apply_search sets
                              // g->stats.won); the same engine core the shell's
                              // S action uses, so headless and visible replays
                              // reproduce the win byte-identically.
} RecActionKind;

// One troop stack in an RA_SET_ARMY target army.
typedef struct { char id[24]; int count; } RecArmyStack;

typedef struct {
    RecPrimKind kind;

    // REC_MOVE
    int8_t dx, dy;

    // REC_ANSWER
    FlowAnswer            ans;       // the answer the policy chose
    PlayerIoCombatOutcome outcome;   // WON/LOST/NOT_RUN (combat flows)
    PendingFlow           flow;      // which flow this answered (sanity + render)
    // Combat identity (only meaningful when outcome != NOT_RUN): lets the visible
    // animator know a fight is being applied. The CombatTurnRecord itself is
    // stored separately in the Plan (combat records are large; only fights carry
    // one), referenced by rec_combat_index.
    CombatMode combat_mode;
    int        rec_combat_index;     // index into Plan.combat[], or -1

    // REC_ACTION
    RecActionKind action;            // which direct engine action
    char          act_id[24];        // castle id (garrison) — empty otherwise
    int           act_x, act_y;      // dock coords (rent boat) — else 0
    // RA_SET_ARMY: the full target composition (GAME_ARMY_SLOTS = 5 stacks).
    RecArmyStack  set_army[5];
    int           set_army_n;
} RecPrim;

// A growable append-only buffer of recorded primitives. The Plan owns one for
// the whole committed sequence; plan_build appends, the executor reads.
typedef struct {
    RecPrim *items;
    int      count;
    int      cap;
} RecBuf;

// A growable list of per-fight combat records (each fight's CombatTurnRecord).
// A REC_ANSWER combat primitive references its record by index into this list.
typedef struct {
    CombatTurnRecord *items;   // each .entries is a separate heap allocation
    int               count;
    int               cap;
} CombatRecList;

// The recording sink the planner writes into while proving a step. NULL on the
// planner means "not recording" (e.g. the count_admissible productivity probe,
// which only measures and must not record). plan_build points this at a TEMP
// sink per candidate; on admission the temp tail is committed into the Plan.
typedef struct {
    RecBuf        *prims;     // append REC_MOVE / REC_ANSWER here
    CombatRecList *combats;   // append per-fight CombatTurnRecords here
    // A recruit/recompose the executor makes mid-attempt STACKS via normal admission:
    // when the attempt succeeds the whole recording (acquisition included) is kept, so
    // the army helps every later objective. A FAILED attempt rolls back in full — a
    // wasted recruit detour must not be locked in (it would maroon the hero at a far
    // dwelling with spent gold), so there is deliberately no persist-on-failure hook.
} RecSink;

// Append helpers (defined in recording.c). Grow-on-demand; safe with NULL sink.
void recbuf_push(RecBuf *b, RecPrim p);
// Push a combat record (takes ownership of rec.entries) and return its index.
int  combatreclist_push(CombatRecList *l, CombatTurnRecord rec);
void recbuf_free(RecBuf *b);
void combatreclist_free(CombatRecList *l);

#endif // OB_AUTOPLAY_RECORDING_H
