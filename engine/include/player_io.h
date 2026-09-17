// engine/include/player_io.h
//
// Uniform player-IO request queue (this header is the canonical reference). One
// engine-owned FIFO carries EVERY player-facing moment the engine raises --
// decisions, informational messages, and full-screen view-opens -- as ordered
// PlayerRequest entries. Both consumers drain the SAME queue: the human shell UI
// renders the front request and answers it; the autoplay responder answers it
// programmatically. Only WHO answers differs; the emit/consume contract is one.
//
// The queue is the authoritative transport for informational messages and
// view-opens, and every decision is raised through it alongside the pending-flow
// scratch (pending.h) that still carries the decision payload. One transport for
// both consumers is what keeps headless and visible play from diverging.
//
// STORAGE: the queue lives INSIDE the Game struct (PlayerIoQueue field), so
// autoplay's full-world snapshot/restore captures it through GameCopy -- no
// hidden mutable global. Its slots are heap, grown as requests arrive, and
// released by GameFree.
//
// Engine-pure: this header pulls only engine types and is callable from a
// consumer that links libobengine.a with -lm -lpthread (no shell deps).

#ifndef OB_ENGINE_PLAYER_IO_H
#define OB_ENGINE_PLAYER_IO_H

#include <stdbool.h>

#include "pending.h"        // PendingFlow + the decision scratch fields it names
#include "view_kind.h"      // ViewKind
#include "dwelling_kind.h"  // DwellingKind
#include "flow_answer.h"    // FlowAnswer (leaf header; no back-edge to game.h)
#include "map.h"            // Map (anonymous-struct typedef -- must be the real
#include "fog.h"            // Fog  decl, not a `struct Map` forward-decl, which
#include "resources.h"     // Resources  would be a different, incomplete type)

// NOTE: this header is included by game.h (Game embeds PlayerIoQueue), so it must
// NOT include flow_resolve.h -- flow_resolve.h includes game.h, which would create
// a cycle. The answer type lives in the leaf header flow_answer.h, included by
// both this header and flow_resolve.h. (pending.h / view_kind.h / dwelling_kind.h
// / map.h / fog.h / resources.h are all leaf headers with no back-edge to game.h.)

// Forward-declare ONLY Game: it is not yet defined when game.h includes this
// header (Game embeds PlayerIoQueue). Map/Fog/Resources are anonymous-struct
// typedefs (map.h/fog.h/resources.h above), so they must be the real
// declarations -- a `struct Map` forward-decl would be a DIFFERENT, incomplete
// type. The API takes Game*; player_io.c includes game.h for the full def.
#include "game_fwd.h"

// What ROLE a player-facing request plays. A consumer dispatches on this.
typedef enum {
    REQ_NONE = 0,    // empty slot
    REQ_DECISION,    // the player must choose (yes/no, A/B, numeric, text)
    REQ_MESSAGE,     // informational; the player acknowledges (no choice)
    REQ_VIEW,        // a full-screen view is presented; ack closes it
} ReqRole;

// The prompt shape a REQ_DECISION uses, so the renderer knows what widget to
// draw and the responder knows what answer space is valid. Mirrors the shell's
// PromptKind without leaking the shell type into the engine.
typedef enum {
    REQ_PROMPT_NONE = 0,
    REQ_PROMPT_YES_NO,    // yes / no
    REQ_PROMPT_AB,        // A / B picker (A=1, B=2)
    REQ_PROMPT_NUMERIC,   // 1..N picker (N = prompt_max)
    REQ_PROMPT_TEXT,      // multi-digit numeric entry (<= prompt_max digits/value)
} ReqPromptKind;

// Text capacities mirror the shell prompt/dialog buffers (pending.h sizes the
// largest body -- the audience message -- at 700).
#define PLAYER_IO_HEADER_CAP 120
#define PLAYER_IO_BODY_CAP   700

// One queued request. A flat value (no pointers) so Game stays value-copyable.
// Role-specific fields are grouped; unused groups are zero. The decision
// scratch deliberately mirrors the pending.h field set.
// A message's picture hint: who or what it is about, for a shell that can show
// it (the modern in-lay dialog). Presentation only; nothing reads it back.
typedef enum {
    REQ_FACE_NONE = 0,
    REQ_FACE_VILLAIN,     // face_index = VillainDef.index
    REQ_FACE_TROOP,       // face_index = TroopDef.index
    REQ_FACE_ARTIFACT,    // face_index = artifact index
    REQ_FACE_PORTRAIT,    // face_index = the pack's portrait index (a person)
    REQ_FACE_SCENE,       // face_index = class index: the hero's temporary-death scene
} ReqFace;

// WHAT KIND OF MOMENT this is, chosen by the engine at the call site and never
// inferred by a renderer. Each kind has exactly one drawing function in the
// modern shell; the legacy shell ignores the kind and draws as it always has.
typedef enum {
    PIO_NONE = 0,
    PIO_NOTE,             // title + words + Continue, on the foot of the screen
    PIO_NOTE_IN_PLACE,    // the open screen shows the words in its own rows
    PIO_NOTE_FACE,        // the same, with a portrait beside the words
    PIO_NOTE_SCENE,       // a full-width scene: backdrop, words, Continue
    PIO_NOTE_OVER_FIELD,  // a note centred over a full-screen field (combat)
    PIO_ASK,              // a question on the foot (yes/no, A/B)
    PIO_ASK_FACE,         // a question with a portrait
    PIO_ASK_IN_PLACE,     // the open screen shows the question in its own rows
    PIO_ASK_SCENE,        // a question drawn as a full scene of its own (a foe)
    PIO_ASK_OVER_FIELD,   // a question centred over a full-screen field (combat)
    PIO_ASK_NUMBER,       // "how many?" on the foot
    PIO_ASK_NUMBER_IN_PLACE,  // "how many?" inside the open screen
    PIO_ASK_CHOICE,       // pick one of N (the host opens its own picker)
    PIO_ASK_SELF,         // queued and answered by the caller; nothing is drawn
    PIO_SCREEN,           // present a full screen
} ReqKind;

typedef struct {
    ReqRole role;
    ReqKind kind;         // what the engine called this moment
    char    header[PLAYER_IO_HEADER_CAP];
    char    body[PLAYER_IO_BODY_CAP];
    ReqFace face;             // role == REQ_MESSAGE: the picture hint
    int     face_index;

    // ---- role == REQ_DECISION -------------------------------------------
    PendingFlow    flow;          // which decision this is (FLOW_*)
    ReqPromptKind  prompt_kind;   // widget shape
    int            prompt_max;    // numeric/text: max choice / max value
    int            prompt_digits; // text: max digits

    // Decision payload (superset of pending.h scratch; only the fields the
    // flow needs are set where the flow opens).
    char dwelling_troop[32];
    char dwelling_zone[24];
    int  dwelling_x, dwelling_y;
    int  friendly_count;
    char friendly_foe_id[40];
    char nav_zones[5][32];
    int  nav_count;
    char castle_id[24];
    char foe_id[24];
    int  foe_x, foe_y;
    int  chest_gold, chest_leadership;

    // ---- role == REQ_VIEW -----------------------------------------------
    ViewKind     view;            // which screen to present
    bool view_replace;            // true: reset stack to this (VIEW_TOWN); false: push
    DwellingKind dwelling_kind;   // VIEW_DWELLING context
    int  view_pop, view_cost, view_gold, view_cap;  // dwelling recruit numbers
    char view_record_key[24];     // VIEW_TOWN town record key
    int  view_boat_x, view_boat_y;// VIEW_TOWN boat spawn
    bool view_won;                // VIEW_WIN/VIEW_LOSE outcome
} PlayerRequest;

// FIFO held inside Game: a ring over heap slots that doubles when full, so no
// request is ever dropped.
typedef struct {
    PlayerRequest *slot;   // heap, cap entries
    int            cap;
    int            head;   // index of the front (oldest) request
    int            count;  // number of outstanding requests
} PlayerIoQueue;

// ---- Lifecycle -------------------------------------------------------------

// Reset the queue to empty. Called by GameInit; safe to call anytime.
void player_io_reset(Game *g);

// ---- Producer: one helper per kind ----------------------------------------
//
// The engine names the moment; nothing downstream guesses. Each returns the
// enqueued request so the caller can fill the payload its flow needs (the
// dwelling numbers, the foe id, ...), or NULL when out of memory.
//
// The ask helpers ALSO open the host prompt (ui_host.h) that the shell renders,
// so a site cannot raise one without the other. player_io_ask_choice leaves the
// picker to the host (the shell opens its own numeric list), and
// player_io_ask_self opens nothing -- it is for a caller that answers its own
// decision at once (the autoplay replay), which no player ever sees.

PlayerRequest *player_io_note      (Game *g, const char *title, const char *body);
PlayerRequest *player_io_note_face (Game *g, const char *title, const char *body,
                                    ReqFace face, int face_index);
PlayerRequest *player_io_note_in_place(Game *g, const char *title, const char *body);
PlayerRequest *player_io_note_scene(Game *g, const char *title, const char *body,
                                    int scene_index);

PlayerRequest *player_io_ask       (Game *g, PendingFlow flow, ReqPromptKind prompt,
                                    const char *title, const char *body);
PlayerRequest *player_io_ask_face  (Game *g, PendingFlow flow, ReqPromptKind prompt,
                                    const char *title, const char *body,
                                    ReqFace face, int face_index);
PlayerRequest *player_io_ask_in_place(Game *g, PendingFlow flow, ReqPromptKind prompt,
                                    const char *title, const char *body);
PlayerRequest *player_io_ask_scene (Game *g, PendingFlow flow, ReqPromptKind prompt,
                                    const char *title, const char *body);
PlayerRequest *player_io_ask_number(Game *g, PendingFlow flow,
                                    const char *title, const char *body,
                                    int digits, int max_value);
PlayerRequest *player_io_ask_number_in_place(Game *g, PendingFlow flow,
                                    const char *title, const char *body,
                                    int digits, int max_value);
PlayerRequest *player_io_ask_choice(Game *g, PendingFlow flow,
                                    const char *title, const char *body, int count);
PlayerRequest *player_io_ask_self  (Game *g, PendingFlow flow, ReqPromptKind prompt);
PlayerRequest *player_io_screen    (Game *g, ViewKind view, bool replace,
                                    const char *title, const char *body);

// ---- Consumer (both the shell UI and the autoplay responder) ---------------

// Peek the front (oldest) outstanding request, or NULL if the queue is idle.
const PlayerRequest *player_io_front(const Game *g);

// True when no request is outstanding.
bool player_io_idle(const Game *g);

// Combat outcome the caller supplies for a combat-bearing decision (siege /
// attack-foe). The engine NEVER renders combat: the host resolves it
// (shell: RunCombat; autoplay: combat_run_headless_ex) and passes the result
// here. COMBAT_NOT_RUN means "the player declined / the flow isn't combat" -- the
// router then performs no combat mutation.
typedef enum {
    PLAYER_IO_COMBAT_NOT_RUN = 0,  // declined or non-combat flow
    PLAYER_IO_COMBAT_WON,
    PLAYER_IO_COMBAT_LOST,
} PlayerIoCombatOutcome;

// Presentation directives the engine cannot perform itself (the host-side layer):
// after answering a decision, the router fills this so the HOST does the
// host-side work (render the win cartoon, show end screens, run temp-death,
// schedule the week-end, dismiss a view, open a chained prompt). The shell
// performs them with raylib; autoplay performs the headless-relevant ones (e.g.
// it records temp-death) and ignores the purely-visual ones. Both read the SAME
// directives from the SAME router -- that is the parity win.
typedef struct {
    bool won_game;          // search hit the scepter -> win cartoon + win screen
    bool game_over;         // time expired -> lose screen
    bool temp_death;        // a fight was lost (or last army dismissed) -> temp-death
    int  week_commission;   // > 0 -> host schedules the week-end with this value
    ViewKind dismiss_view;  // != VIEW_NONE -> host pops this view if it is on top
    bool chain_dismiss_last;// DISMISS_ARMY hit the last stack -> open DISMISS_LAST
    int  chain_slot;        //   ... the selected slot for the chained confirm
} PlayerIoPresentation;

// Answer the front REQ_DECISION: routes `ans` (+ the combat `outcome` for
// combat-bearing flows) to the matching flow_apply_* core (engine state
// mutation), fills `*out_pres` with the host-side presentation directives (may
// be NULL if the caller wants none), then pops the request and clears the
// mirrored pending_flow scratch. No-op if the front is not a REQ_DECISION.
//
// This is the SINGLE shared decision router both the human shell
// (prompt_dispatch_tick, src/shell_promptdispatch.c) and the autoplay
// responder (exec_answer_pending, autoplay/exec.h) call, so both resolve a
// flow through one engine path rather than a per-driver switch. The engine performs only state
// mutation; combat and rendering stay with the host.
void player_io_answer(Game *g, Map *map, Fog *fog, const Resources *res,
                      FlowAnswer ans, PlayerIoCombatOutcome outcome,
                      PlayerIoPresentation *out_pres);

// Acknowledge the front REQ_MESSAGE or REQ_VIEW (dismiss the message / close the
// view) and pop it. No-op if the front is a REQ_DECISION (those need an answer).
void player_io_ack(Game *g);

// Ack and pop EVERY leading REQ_MESSAGE *and* REQ_VIEW at the front of the queue
// (stopping at the first REQ_DECISION). Autoplay calls this to drain the passive
// requests it does not display -- it has no UI -- keeping the queue from filling
// during long simulation/execution loops where engine mutators (captures,
// purchases, pickups, screen-opens) enqueue requests between decision ticks. By
// acking REQ_VIEWs immediately, autoplay never accumulates an engine view: this
// is what makes the shell view-stack overflow structurally impossible under
// autoplay. Returns the count drained.
int player_io_drain_messages(Game *g);

#endif // OB_ENGINE_PLAYER_IO_H
