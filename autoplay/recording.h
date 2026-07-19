// autoplay/recording.h
//
// The write-only recording sink (AP-003, AP-021). Every engine-changing action
// a helper performs is emitted here as one RecPrim, stamped with a
// world-fingerprint of the PRE-action state (AP-022), so that
// headless == visible == replay, byte-for-byte. An attempt marks the sink and
// rolls it back with its world snapshot (AP-030); the search additionally
// slices the sink into PER-EDGE deltas and rebuilds a node's recording by
// concatenating them from the root down (AP-206).

#ifndef OB_AUTOPLAY_RECORDING_H
#define OB_AUTOPLAY_RECORDING_H

#include <stdbool.h>
#include <stdint.h>

#include "game.h"
#include "flow_answer.h"
#include "player_io.h"   // PlayerIoCombatOutcome

typedef enum {
    REC_MOVE = 0,    // one GameStep (dx, dy)
    REC_ANSWER,      // a pending-flow answer (+ combat outcome when combat-bearing)
    REC_ACTION,      // a direct engine mutation, tagged by RecActionKind
} RecKind;

typedef enum {
    RA_NONE = 0,
    RA_GARRISON,        // GameGarrisonTroop(id=castle, a=slot)
    RA_UNGARRISON,      // GameUngarrisonTroop(id=castle, a=slot)
    RA_DISMISS,         // dismiss army slot a (raised flow + answer)
    RA_BUY_TROOP,       // GameBuyTroop(id=troop, a=count)
    RA_BUY_SPELL,       // GameBuySpell(id=town)
    RA_BUY_SIEGE,       // GameBuySiege()
    RA_RENT_BOAT,       // GameRentBoat(a=x, b=y, id=zone)
    RA_CANCEL_BOAT,     // GameCancelBoat()
    RA_TAKE_CONTRACT,   // GameTakeNextContract()
    RA_CAST_ADV_SPELL,  // adventure-spell effect (id=spell id, a=dx, b=dy for bridge)
    RA_GATE_TOWN,       // GameGateTeleport town gate (id=dest zone, a=x, b=y)
    RA_GATE_CASTLE,     // GameGateTeleport castle gate (id=dest zone, a=x, b=y)
    RA_TRAVEL_ZONE,     // navigate flow to neighbor zone (id=zone)
    RA_SEARCH,          // search-this-tile flow, answered YES
    RA_SPEND_WEEK,      // GameSpendDays to the next week boundary
    RA_MOUNT_FLY,       // GameMountFly (RIDE -> FLY)
    RA_LAND,            // GameLandHere (FLY -> RIDE)
    RA_DISCARD_SPELL,   // field-discard one charge (a=spell idx, FLOW_DISCARD_SPELL)
    RA_DISMISS_LAST,    // dismiss the LAST stack (a=slot): the "sent back to
                        // King" confirm chain -> temp death (the maroon escape)
} RecActionKind;

typedef struct {
    uint8_t  kind;      // RecKind
    uint8_t  action;    // RecActionKind (REC_ACTION only)
    uint8_t  ans_kind;  // PromptAnswer (REC_ANSWER only)
    uint8_t  outcome;   // PlayerIoCombatOutcome (REC_ANSWER only)
    int8_t   dx, dy;    // REC_MOVE only
    uint8_t  flow;      // PendingFlow being answered (REC_ANSWER only)
    int32_t  a, b;      // action args
    int32_t  number;    // FlowAnswer.number (REC_ANSWER only)
    char     id[32];    // action id payload; must hold any catalog id
                        // (sized to CAT_ID_LEN -- spell/troop/zone ids fit)
    uint32_t fp;        // world fingerprint of the PRE-action state (AP-022)
} RecPrim;

typedef struct {
    RecPrim *prims;
    int      count;
    int      cap;
} RecSink;

// The one active sink for the run (autoplay drives one game at a time).
RecSink *recsink(void);
bool     recsink_init(int cap);      // heap-allocates the prim array
void     recsink_free(void);
int      recsink_mark(void);         // current length, for rollback
void     recsink_rollback(int mark); // truncate back to a mark
bool     recsink_truncated(void);    // a push was dropped at capacity (sticky)

// Deep-copy the recording [0..count) into a malloc'd array written to *out
// (*out NULL when empty). Returns count, or -1 on allocation failure. The
// search saves the pre-search prefix as the root's delta this way: jumping
// between lines of play needs the CONTENT restored, not just the length
// (recsink_rollback only truncates).
int      recsink_save_copy(RecPrim **out);
// Overwrite the recording with a saved prefix: prims[0..n) copied in,
// count = n. n never exceeds cap (every prefix was saved from this sink).
void     recsink_restore_copy(const RecPrim *prims, int n);

// The ONE world-fingerprint function (AP-022): FNV-1a over
// zone/position/day/gold/leadership/travel-mode/contract/army/spellbook.
// Recorder and replay both use it, so they cannot disagree on "same state".
uint32_t rec_world_fp(const Game *g);

// Push helpers. Each stamps rec_world_fp(g) of the PRE-action state, so the
// caller MUST push BEFORE the engine mutation (replay checks the fingerprint as
// a pre-condition). rec_push_answer returns the prim so a caller that must run
// the effect first (combat, to learn the outcome) can push pre-effect and then
// patch p->outcome.
void rec_push_move(const Game *g, int dx, int dy);
RecPrim *rec_push_answer(const Game *g, int flow, FlowAnswer ans,
                         PlayerIoCombatOutcome outcome);
void rec_push_action(const Game *g, RecActionKind action, const char *id,
                     int a, int b);
// Stamp a caller-supplied PRE-mutation fingerprint (for actions whose id is
// only known after the mutation, e.g. RA_TAKE_CONTRACT).
void rec_push_action_fp(uint32_t pre_fp, RecActionKind action, const char *id,
                        int a, int b);

#endif
