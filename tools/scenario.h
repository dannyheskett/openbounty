// JSON scenario loader + step interpreter for playtest. Each scenario
// is one .json file under tools/scenarios/. The runner spawns a fresh
// game per scenario, drives the title flow per `setup`, then walks the
// steps array.

#ifndef SCENARIO_H
#define SCENARIO_H

#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <dirent.h>

#include "playtest_lib.h"

typedef enum {
    STEP_KEY,
    STEP_KEYS,
    STEP_TEXT,
    STEP_FRAMES,
    STEP_CHEAT,
    STEP_WALK,
    STEP_WALK_TO,
    STEP_BFS_TO,
    STEP_BFS_TO_REPLAN,
    STEP_LET_VILLAIN_CASTLE_IN,
    STEP_LET_CASTLE,
    STEP_LET_TOWN,
    STEP_LET_DWELLING_IN,
    STEP_LET_FOE,
    STEP_EXPECT_PROMPT,
    STEP_EXPECT_DIALOG,
    STEP_DRAIN_DIALOGS,
    STEP_RESOLVE_COMBAT,
    STEP_AUTO_COMBAT,
    STEP_SNAPSHOT_BASELINE,
    STEP_ASSERT,
    STEP_ASSERT_GE,
    STEP_ASSERT_ARRAY_LEN,
    STEP_ASSERT_HERO_AT,
    STEP_SAVE,
    STEP_LOAD,
    STEP_RESPAWN,
    STEP_WAIT_DAYS,
    STEP_SNAP,
    STEP_ASSERT_SNAP_HASH,
    STEP_QUIT,
    STEP_NOTE,
    // ---- shorthand wrappers (compose existing primitives) ----
    STEP_OPEN_VIEW,         // key:LETTER + frames
    STEP_DISMISS_VIEW,      // key:ESCAPE + frames + drain_dialogs
    STEP_ASSERT_OVERWORLD,  // mode == "adventure" && view == 0 && !game_over
} StepKind;

typedef enum {
    VAL_NONE,
    VAL_INT,
    VAL_BOOL,
    VAL_STR,
} ValueKind;

typedef struct {
    ValueKind kind;
    long long  i;
    bool       b;
    char       s[128];
} Value;

// Tagged-union step record.
typedef struct {
    StepKind kind;
    // Generic fields used by multiple kinds.
    char a[64];      // primary string (key name, troop, op, ref name, …)
    char b[64];      // secondary string (zone, kind, on_foe, …)
    char c[64];      // tertiary string (on_chest, …)
    char d[64];      // quaternary string (on_dialog, body_contains, …)
    int  i, j, k;    // ints (x, y, n, max, …)
    Value value;     // for assert
    char path[128];  // for assert / read
    char op[8];      // for assert ("==", ">=", etc.)
    char ref[64];    // baseline ref name (snapshot binding)
} Step;

// Runtime bindings produced by `let_*` / `snapshot_baseline` steps.
typedef enum { BIND_NONE, BIND_LOC, BIND_BASELINE } BindKind;
typedef struct {
    char     name[32];
    BindKind kind;
    int      x, y;
    char     id[32];
    int      gold, score, leadership, killed, days_left;
} Binding;

#define SCENARIO_MAX_BINDINGS 16

typedef struct {
    char name[64];
    char description[256];
    uint64_t seed;
    char class_id[16];
    char char_name[16];
    char difficulty[16];
    bool auto_combat;
    char load_save[256];
    char tags[8][32];
    int  tag_count;
    int  max_seconds;
    Step *steps;
    int   step_count;
    Binding bindings[SCENARIO_MAX_BINDINGS];
    int     binding_count;
} Scenario;

bool scenario_load(const char *path, Scenario *out);
void scenario_free(Scenario *s);
bool scenario_run(Scenario *s, int fd, RunStats *rs);

// "A".."D" for the class screen — derived from setup.class.
const char *scenario_class_letter(const Scenario *s);

// Filter expression: substring-match against scenario name and tags;
// "!FOO" inverts (excludes scenarios whose name/tags contain FOO).
bool scenario_matches_filter(const char *file_path, const char *filter);

#endif
