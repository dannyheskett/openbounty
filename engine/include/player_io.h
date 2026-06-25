// engine/include/player_io.h
//
// Uniform player-IO request queue (this header is the canonical reference). One
// engine-owned FIFO carries EVERY player-facing moment the engine raises —
// decisions, informational messages, and full-screen view-opens — as ordered
// PlayerRequest entries. Both consumers drain the SAME queue: the human shell UI
// renders the front request and answers it; the autoplay responder answers it
// programmatically. Only WHO answers differs; the emit/consume contract is one.
//
// This replaces the three divergent channels that exist today (pending-flow
// scratch globals in pending.h; open_dialog info banners; screen_*_open view
// callbacks in ui_host.h), which let the shell and autoplay consume player
// moments through unrelated code — the defect behind the visible-mode view-stack
// overflow and the headless/visible divergence.
//
// MIGRATION STATE (M1): this queue is ADDITIVE and not yet authoritative. The
// engine populates it alongside the old callbacks; no consumer drains it for
// real yet. Later slices (M2..M5) flip each channel onto it and remove the old
// paths. The API and types here are the stable contract those slices build on.
//
// STORAGE: the queue lives INSIDE the Game struct (PlayerIoQueue field), so
// autoplay's full-world snapshot/restore and save/load capture it
// automatically and state_serialize reads it from g — no hidden mutable global.
// Game stays flat and value-copyable (the queue is a fixed-size array, no owned
// pointers), so `Game tmp = *g;` remains a sound deep copy.
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
#include "map.h"            // Map (anonymous-struct typedef — must be the real
#include "fog.h"            // Fog  decl, not a `struct Map` forward-decl, which
#include "resources.h"     // Resources  would be a different, incomplete type)

// NOTE: this header is included by game.h (Game embeds PlayerIoQueue), so it must
// NOT include flow_resolve.h — flow_resolve.h includes game.h, which would create
// a cycle. The answer type lives in the leaf header flow_answer.h, included by
// both this header and flow_resolve.h. (pending.h / view_kind.h / dwelling_kind.h
// / map.h / fog.h / resources.h are all leaf headers with no back-edge to game.h.)

// Forward-declare ONLY Game: it is not yet defined when game.h includes this
// header (Game embeds PlayerIoQueue). Map/Fog/Resources are anonymous-struct
// typedefs (map.h/fog.h/resources.h above), so they must be the real
// declarations — a `struct Map` forward-decl would be a DIFFERENT, incomplete
// type. The API takes Game*; player_io.c includes game.h for the full def.
typedef struct Game Game;

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
// largest body — the audience message — at 700).
#define PLAYER_IO_HEADER_CAP 120
#define PLAYER_IO_BODY_CAP   700

// One queued request. A flat value (no pointers) so Game stays value-copyable.
// Role-specific fields are grouped; unused groups are zero. The decision
// scratch deliberately mirrors the pending.h field set so M2 can move the
// existing flows onto the queue verbatim.
typedef struct {
    ReqRole role;
    char    header[PLAYER_IO_HEADER_CAP];
    char    body[PLAYER_IO_BODY_CAP];

    // ---- role == REQ_DECISION -------------------------------------------
    PendingFlow    flow;          // which decision this is (FLOW_*)
    ReqPromptKind  prompt_kind;   // widget shape
    int            prompt_max;    // numeric/text: max choice / max value
    int            prompt_digits; // text: max digits

    // Decision payload (superset of pending.h scratch; only the fields the
    // flow needs are set). M2 fills these where the flow opens.
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

// Fixed-capacity FIFO held inside Game. Capacity is generous: at most a few
// requests are ever outstanding (a decision, an info message, a view), but a
// week-end can chain a couple, so 8 leaves ample headroom while staying small.
#define PLAYER_IO_QUEUE_CAP 8

typedef struct {
    PlayerRequest slot[PLAYER_IO_QUEUE_CAP];
    int           head;   // index of the front (oldest) request
    int           count;  // number of outstanding requests
} PlayerIoQueue;

// ---- Lifecycle -------------------------------------------------------------

// Reset the queue to empty. Called by GameInit; safe to call anytime.
void player_io_reset(Game *g);

// ---- Producer (engine-internal; called where flows/messages/views are raised)
// Each returns a pointer to the newly enqueued request for the caller to fill
// remaining role-specific fields, or NULL if the queue is full (a programming
// error — log + drop, never overflow silently). header/body are copied.

PlayerRequest *player_io_enqueue_decision(Game *g, PendingFlow flow,
                                          ReqPromptKind kind,
                                          const char *header, const char *body);
PlayerRequest *player_io_enqueue_message(Game *g,
                                         const char *header, const char *body);
PlayerRequest *player_io_enqueue_view(Game *g, ViewKind view,
                                      const char *header, const char *body);

// Convenience used at the engine emit sites (step.c / flows.c) when a decision
// is raised: enqueues a REQ_DECISION carrying `flow` + `kind` + header/body so
// the queue mirrors the pending_flow the site also sets (M2 keeps the pending_*
// scratch as the payload source of truth; later slices move the payload onto the
// request and drop the scratch). A NULL return (queue full) is a no-op the
// caller can ignore — the legacy pending_flow path still drives behavior in M2.
PlayerRequest *player_io_raise_decision(Game *g, PendingFlow flow,
                                        ReqPromptKind kind,
                                        const char *header, const char *body);

// Raise an informational MESSAGE through the queue (M3). The engine's uniform
// "show the player some text" entry point — it REPLACES the engine's direct
// open_dialog() host-callback calls, so messages flow through the one queue both
// the shell and autoplay consume. The shell drains the front REQ_MESSAGE into its
// dialog renderer; autoplay acks it. Unlike open_dialog this takes a Game* so the
// message lands in that game's queue (no hidden global) and is captured by
// worldsnap/save/serialize. NULL return (queue full) is an ignorable no-op;
// header may be NULL (untitled banner).
PlayerRequest *player_io_message(Game *g, const char *header, const char *body);

// Raise a full-screen VIEW through the queue (M4). The engine's uniform "present
// screen X" entry point — it REPLACES the direct shell view push the screen
// openers (views_open_town / screen_*_open / screen_end_game_open) used to do.
// The shell's per-frame view sync (human play) pushes the matching screen onto
// its local view stack and acks the request when the player dismisses it;
// autoplay, which has no UI, acks it immediately and so NEVER accumulates an
// engine view (this is what kills the views_push stack-overflow defect). `replace`
// true means the view should reset the shell stack to a single entry (VIEW_TOWN
// semantics, formerly views_set); false means push atop it (formerly views_push).
// The caller fills any role==REQ_VIEW context fields on the returned request
// (dwelling numbers, town key, won flag). NULL return (queue full) is ignorable.
PlayerRequest *player_io_raise_view(Game *g, ViewKind view, bool replace,
                                    const char *header, const char *body);

// ---- Consumer (both the shell UI and the autoplay responder) ---------------

// Peek the front (oldest) outstanding request, or NULL if the queue is idle.
const PlayerRequest *player_io_front(const Game *g);

// True when no request is outstanding.
bool player_io_idle(const Game *g);

// Combat outcome the caller supplies for a combat-bearing decision (siege /
// attack-foe). The engine NEVER renders combat: the host resolves it
// (shell: RunCombat; autoplay: combat_run_headless_ex) and passes the result
// here. COMBAT_NOT_RUN means "the player declined / the flow isn't combat" — the
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
// directives from the SAME router — that is the parity win.
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
// (prompt_dispatch_tick) and the autoplay responder (planner_answer_pending)
// call — collapsing their previously-divergent per-flow switches into one
// engine path (mode parity). The engine performs only state
// mutation; combat and rendering stay with the host.
void player_io_answer(Game *g, Map *map, Fog *fog, const Resources *res,
                      FlowAnswer ans, PlayerIoCombatOutcome outcome,
                      PlayerIoPresentation *out_pres);

// Acknowledge the front REQ_MESSAGE or REQ_VIEW (dismiss the message / close the
// view) and pop it. No-op if the front is a REQ_DECISION (those need an answer).
void player_io_ack(Game *g);

// Ack and pop EVERY leading REQ_MESSAGE *and* REQ_VIEW at the front of the queue
// (stopping at the first REQ_DECISION). Autoplay calls this to drain the passive
// requests it does not display — it has no UI — keeping the queue from filling
// during long simulation/execution loops where engine mutators (captures,
// purchases, pickups, screen-opens) enqueue requests between decision ticks. By
// acking REQ_VIEWs immediately, autoplay never accumulates an engine view: this
// is what makes the shell view-stack overflow structurally impossible under
// autoplay (M4). Returns the count drained.
int player_io_drain_messages(Game *g);

#endif // OB_ENGINE_PLAYER_IO_H
