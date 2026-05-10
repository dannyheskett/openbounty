// Primitives for driving the openbounty harness from a test client.
// Lifted from the original tools/playtest.c so the runner and scenario
// interpreter share one well-tested set of helpers.
//
// Threading model: single-threaded. The lib owns a per-call cJSON parse
// (callers don't need to free).

#ifndef OB_PLAYSHOW_LIB_H
#define OB_PLAYSHOW_LIB_H

#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "cJSON.h"

#define PLAYSHOW_MAP_MAX 64

// ---- run stats: per-scenario counters --------------------------------
typedef struct {
    int  asserts_passed;
    int  asserts_failed;
} RunStats;

void rs_pass(RunStats *rs, const char *path, const char *got);
void rs_fail(RunStats *rs, const char *path,
             const char *expected, const char *got);

// ---- transport -------------------------------------------------------
int  pt_connect_socket(const char *path, int retries_ms);
int  pt_send_cmd(int fd, const char *cmd,
                 char *header, size_t cap_h,
                 char *status, size_t cap_s);
bool pt_cmd_ok(int fd, const char *cmd);
cJSON *pt_fetch_state(int fd);

// ---- pacing ----------------------------------------------------------
void pt_set_pause_ms(int ms);
int  pt_pause_ms(void);
void pt_zsleep(int ms);

// ---- input convenience ----------------------------------------------
bool pt_key(int fd, const char *k);
bool pt_frames(int fd, int n);
void pt_say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// ---- walkability + BFS ----------------------------------------------
typedef struct {
    int w, h;
    unsigned char walk[PLAYSHOW_MAP_MAX][PLAYSHOW_MAP_MAX];
} WalkMap;

bool pt_fetch_map(int fd, WalkMap *out);
int  pt_bfs_path(const WalkMap *m, int sx, int sy, int tx, int ty,
                 char *out, size_t cap);

typedef enum { WALK_DONE, WALK_INTERRUPTED, WALK_FAILED } WalkResult;
WalkResult pt_walk_path(int fd, const char *path);

// ---- state lookups ---------------------------------------------------
bool pt_get_hero_xy(int fd, int *x, int *y);
// Returns malloc'd id (caller frees) and writes (x,y). Skip first
// `skip` matches.
char *pt_find_villain_castle_n(int fd, const char *zone, int skip,
                               int *out_x, int *out_y);
bool  pt_find_castle_by_id(int fd, const char *id, int *out_x, int *out_y);
bool  pt_find_town_by_id(int fd, const char *id, int *out_x, int *out_y);
bool  pt_find_dwelling_in(int fd, const char *zone, int skip,
                          int *out_x, int *out_y, char *out_troop, size_t cap);
bool  pt_find_foe(int fd, const char *placement_id,
                  int *out_x, int *out_y, bool *out_friendly);

// ---- prompt classification ------------------------------------------
bool         pt_ui_blocking(int fd);
const char  *pt_classify_prompt(int fd);   // foe|siege|chest|audience|dialog|other|""
void         pt_describe_prompt(int fd, char *out, size_t cap);
int          pt_drain_dialogs(int fd, int max_attempts);
int          pt_resolve_combat(int fd, bool press_y_first);
int          pt_villains_caught(int fd);

// ---- assertions (RunStats-aware) ------------------------------------
cJSON *pt_json_path(cJSON *root, const char *path);
int    pt_read_int(int fd, const char *path, int fallback);
bool   pt_read_str(int fd, const char *path, char *buf, size_t cap);

bool pt_assert_int_eq (int fd, const char *path, int want, RunStats *rs);
bool pt_assert_int_ge (int fd, const char *path, int floor, RunStats *rs);
bool pt_assert_int_le (int fd, const char *path, int ceil, RunStats *rs);
bool pt_assert_str_eq (int fd, const char *path, const char *want, RunStats *rs);
bool pt_assert_bool_eq(int fd, const char *path, bool want, RunStats *rs);
bool pt_assert_hero_at(int fd, int x, int y, RunStats *rs);
bool pt_assert_array_len(int fd, const char *path,
                         const char *op, int value, RunStats *rs);

// ---- lifecycle: spawn the game + drive title flow -------------------
typedef struct {
    pid_t child;
    int   fd;
    char  sock_path[256];
} GameProcess;

// Spawns ./build/openbounty with --harness sock_path [--seed N]
// [--load PATH]. Returns 0 on success.
int  pt_spawn_game(const char *seed_arg, const char *load_path,
                   const char *sock_path, GameProcess *out);
void pt_shutdown_game(GameProcess *gp);

// Drive class pick → name entry → difficulty → past intro dialogs.
// Returns when state.mode == "adventure" or fails after timeout.
// class_letter ∈ "A".."D" (Knight..Barbarian).
// difficulty   ∈ "easy"|"normal"|"hard"|"impossible".
bool pt_run_title_new_game(int fd, const char *class_letter,
                           const char *name, const char *difficulty);

#endif
