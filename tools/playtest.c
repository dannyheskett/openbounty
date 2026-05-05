// playtest: scenario runner for the openbounty harness.
//
// Phase 0 of the testing refactor: the original hardcoded scenario
// (drive title → BFS to a villain castle → siege → assert) lives here
// inline and uses the primitives from playshow_lib. Subsequent phases
// move this scenario into a JSON file under tools/scenarios/ and add a
// scenario interpreter; this file shrinks to a thin runner.

#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#include "playtest_lib.h"
#include "scenario.h"

#define SOCK_PATH "/tmp/openbounty-playtest.sock"

static GameProcess g_gp = { .child = -1, .fd = -1, .sock_path = "" };

static void cleanup(int sig) {
    (void)sig;
    if (g_gp.child > 0) kill(g_gp.child, SIGTERM);
    if (g_gp.fd >= 0)   close(g_gp.fd);
    if (g_gp.sock_path[0]) unlink(g_gp.sock_path);
    _exit(1);
}

// Phase-0 hardcoded scenario: same flow as the original playtest. Phase 1
// moves this into tools/scenarios/win_one_castle.json and replaces the
// body with a scenario_run() call.
static int run_legacy_scenario(int fd) {
    RunStats rs = { 0 };

    if (!pt_run_title_new_game(fd, "A", "Bot", "easy")) {
        char status[131072] = { 0 };
        pt_send_cmd(fd, "state", NULL, 0, status, sizeof status);
        size_t L = strlen(status);
        if (L > 600) status[600] = '\0';
        pt_say("FATAL: couldn't reach adventure mode. State[%zu B]:\n  %s", L, status);
        return 4;
    }

    int hx = -1, hy = -1;
    if (!pt_get_hero_xy(fd, &hx, &hy)) {
        pt_say("FATAL: couldn't read hero position");
        return 4;
    }
    pt_say("hero at (%d,%d)", hx, hy);

    pt_assert_str_eq (fd, "mode", "adventure", &rs);
    pt_assert_str_eq (fd, "character.class", "knight", &rs);
    pt_assert_str_eq (fd, "character.name", "Bot", &rs);
    pt_assert_str_eq (fd, "position.zone", "continentia", &rs);
    pt_assert_int_ge (fd, "stats.gold", 1, &rs);
    pt_assert_int_ge (fd, "stats.leadership_current", 1, &rs);
    pt_assert_bool_eq(fd, "stats.game_over", false, &rs);

    int gold_pre   = pt_read_int(fd, "stats.gold", 0);
    int score_pre  = pt_read_int(fd, "stats.score", 0);
    int killed_pre = pt_read_int(fd, "stats.followers_killed", 0);
    pt_say("baseline: gold=%d score=%d followers_killed=%d",
           gold_pre, score_pre, killed_pre);

    WalkMap m;
    if (!pt_fetch_map(fd, &m)) { pt_say("FATAL: dump_map failed"); return 6; }
    pt_say("map %dx%d", m.w, m.h);

    int gx = -1, gy = -1;
    char *cid = NULL;
    char path[PLAYSHOW_MAP_MAX * PLAYSHOW_MAP_MAX];
    int  plen = -1;
    for (int skip = 0; skip < 16; skip++) {
        free(cid);
        cid = pt_find_villain_castle_n(fd, "continentia", skip, &gx, &gy);
        if (!cid) break;
        plen = pt_bfs_path(&m, hx, hy, gx, gy, path, sizeof path);
        pt_say("candidate '%s' gate (%d,%d) -> path %s",
               cid, gx, gy, plen >= 0 ? "OK" : "unreachable");
        if (plen >= 0) break;
    }
    if (!cid || plen < 0) {
        pt_say("FATAL: no foot-reachable villain castle in continentia");
        free(cid); return 5;
    }
    pt_say("target castle '%s' gate (%d,%d), path len %d", cid, gx, gy, plen);

    pt_cmd_ok(fd, "auto_combat:on");

    bool reached = false, hard_fail = false;
    int hops = 0;
    while (hops++ < 12) {
        WalkResult wr = pt_walk_path(fd, path);
        pt_frames(fd, 15);
        if (wr == WALK_FAILED) { pt_say("FATAL: walk_path errored at hop %d", hops); hard_fail = true; break; }
        if (wr == WALK_DONE) {
            const char *cls = pt_classify_prompt(fd);
            int curx = -1, cury = -1; pt_get_hero_xy(fd, &curx, &cury);
            char pdesc[256]; pt_describe_prompt(fd, pdesc, sizeof pdesc);
            pt_say("walk done at hop %d: hero (%d,%d), gate (%d,%d), %s",
                   hops, curx, cury, gx, gy, pdesc);
            if (strcmp(cls, "siege") == 0 || strcmp(cls, "audience") == 0) reached = true;
            else { pt_say("FATAL: arrived at end of path with no siege prompt (class=%s)", cls); hard_fail = true; }
            break;
        }
        const char *cls = pt_classify_prompt(fd);
        char pdesc[256]; pt_describe_prompt(fd, pdesc, sizeof pdesc);
        pt_say("encounter at hop %d (%s): %s", hops, cls, pdesc);
        if (strcmp(cls, "foe") == 0) {
            int n = pt_resolve_combat(fd, true);
            pt_say("  resolved foe combat, dismissed %d page(s)", n);
            cJSON *root = pt_fetch_state(fd);
            cJSON *go = pt_json_path(root, "stats.game_over");
            bool dead = cJSON_IsTrue(go);
            cJSON_Delete(root);
            if (dead) { pt_say("FATAL: hero died in foe combat"); hard_fail = true; break; }
        } else if (strcmp(cls, "chest") == 0) {
            pt_key(fd, "A"); pt_frames(fd, 20);
            pt_drain_dialogs(fd, 4);
        } else if (strcmp(cls, "siege") == 0 || strcmp(cls, "audience") == 0) {
            reached = true; break;
        } else if (strcmp(cls, "dialog") == 0) {
            pt_drain_dialogs(fd, 4);
        } else {
            pt_say("FATAL: unhandled prompt blocked walk: %s", pdesc);
            hard_fail = true; break;
        }
        if (!pt_get_hero_xy(fd, &hx, &hy)) { pt_say("FATAL: lost hero position"); hard_fail = true; break; }
        if (!pt_fetch_map(fd, &m))         { pt_say("FATAL: dump_map failed during replan"); hard_fail = true; break; }
        plen = pt_bfs_path(&m, hx, hy, gx, gy, path, sizeof path);
        if (plen < 0) { pt_say("FATAL: gate unreachable from (%d,%d)", hx, hy); hard_fail = true; break; }
        pt_say("  replanned: hero (%d,%d) -> gate (%d,%d), %d steps", hx, hy, gx, gy, plen);
    }

    if (hard_fail) { free(cid); return 7; }
    if (!reached)  { pt_say("FAIL: gave up after %d hops without reaching gate", hops); free(cid); return 8; }

    pt_say("running auto-combat at castle...");
    int total_dismissed = pt_resolve_combat(fd, true);
    pt_say("combat done; dismissed %d dialog page(s)", total_dismissed);
    int extra = pt_drain_dialogs(fd, 8);
    if (extra) pt_say("drained %d more adventure dialog(s)", extra);

    pt_assert_str_eq (fd, "mode", "adventure", &rs);
    pt_assert_bool_eq(fd, "stats.game_over", false, &rs);
    int killed_post = pt_read_int(fd, "stats.followers_killed", 0);
    pt_say("followers_killed: %d -> %d", killed_pre, killed_post);
    int caught = pt_villains_caught(fd);
    pt_say("villains_caught = %d", caught);
    pt_assert_int_ge(fd, "stats.score", score_pre, &rs);
    pt_say("assertions: %d passed, %d failed", rs.asserts_passed, rs.asserts_failed);

    free(cid);
    if (rs.asserts_failed > 0) { pt_say("FAIL: %d assertion(s) failed", rs.asserts_failed); return 1; }
    if (caught >= 1) { pt_say("PASS: captured >=1 villain"); return 0; }
    pt_say("PASS: %d assertion(s) ok (no villain captured this run)", rs.asserts_passed);
    return 0;
}

// Run a single scenario file. Spawns its own game process per scenario.
// Returns 0 on PASS, nonzero on FAIL.
static int run_scenario_file(const char *path, const char *seed_override,
                             int *out_passed, int *out_failed) {
    Scenario s = { 0 };
    if (!scenario_load(path, &s)) {
        pt_say("[%s] FAIL: could not load scenario", path);
        return 2;
    }
    if (seed_override && seed_override[0]) {
        s.seed = strtoull(seed_override, NULL, 0);
    }
    char seed_str[32];
    snprintf(seed_str, sizeof seed_str, "%llu", (unsigned long long)s.seed);

    GameProcess gp = { .child = -1, .fd = -1, .sock_path = "" };
    char sock[256];
    static int s_seq = 0;
    snprintf(sock, sizeof sock, "/tmp/openbounty-playtest-%d-%d.sock",
             (int)getpid(), s_seq++);
    unlink(sock);
    if (pt_spawn_game(s.seed ? seed_str : NULL, NULL, sock, &gp) != 0) {
        pt_say("[%s] FAIL: spawn", s.name);
        scenario_free(&s);
        return 3;
    }
    g_gp = gp;

    if (!pt_run_title_new_game(gp.fd, scenario_class_letter(&s),
                               s.char_name[0] ? s.char_name : "Bot",
                               s.difficulty[0] ? s.difficulty : "easy")) {
        pt_say("[%s] FAIL: title flow did not reach adventure mode", s.name);
        pt_shutdown_game(&gp);
        scenario_free(&s);
        return 4;
    }
    if (s.auto_combat) pt_cmd_ok(gp.fd, "auto_combat:on");

    RunStats rs = { 0 };
    bool ok = scenario_run(&s, gp.fd, &rs);
    if (out_passed) *out_passed += rs.asserts_passed;
    if (out_failed) *out_failed += rs.asserts_failed;

    if (ok && rs.asserts_failed == 0) {
        pt_say("[%s] PASS (%d asserts)", s.name, rs.asserts_passed);
    } else {
        pt_say("[%s] FAIL (%d ok, %d failed)", s.name,
               rs.asserts_passed, rs.asserts_failed);
    }

    pt_shutdown_game(&gp);
    g_gp.child = -1; g_gp.fd = -1; g_gp.sock_path[0] = 0;
    scenario_free(&s);
    return (ok && rs.asserts_failed == 0) ? 0 : 1;
}

static int compare_strs(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

// Match against multiple comma-separated filter expressions. "!FOO" excludes;
// plain "FOO" is informational and not enforced as a positive include
// (use --filter '!nightly,!flaky' to exclude both).
static bool match_filters(const char *file_path, const char *filter) {
    if (!filter || !filter[0]) return true;
    char buf[256];
    snprintf(buf, sizeof buf, "%s", filter);
    char *save = NULL;
    for (char *tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        if (!scenario_matches_filter(file_path, tok)) return false;
    }
    return true;
}

static int run_suite(const char *dir, const char *filter,
                     const char *seed_override, bool keep_going) {
    DIR *dp;
    extern int closedir(DIR *);
    dp = opendir(dir);
    if (!dp) { pt_say("could not open suite dir: %s", dir); return 2; }
    struct dirent *e;
    char **files = NULL;
    int nfiles = 0, cap = 0;
    while ((e = readdir(dp))) {
        const char *name = e->d_name;
        size_t nl = strlen(name);
        if (nl < 6 || strcmp(name + nl - 5, ".json") != 0) continue;
        if (nfiles == cap) {
            cap = cap ? cap * 2 : 16;
            files = realloc(files, sizeof(char *) * cap);
        }
        char *full = malloc(strlen(dir) + 1 + nl + 1);
        sprintf(full, "%s/%s", dir, name);
        files[nfiles++] = full;
    }
    closedir(dp);
    qsort(files, nfiles, sizeof(char *), compare_strs);

    int total_pass = 0, total_fail = 0;
    int scen_pass = 0, scen_fail = 0;
    for (int i = 0; i < nfiles; i++) {
        if (filter && filter[0] && !match_filters(files[i], filter)) continue;
        int rc = run_scenario_file(files[i], seed_override, &total_pass, &total_fail);
        if (rc == 0) scen_pass++;
        else { scen_fail++; if (!keep_going) { for (int j = 0; j < nfiles; j++) free(files[j]); free(files); return 1; } }
    }
    for (int i = 0; i < nfiles; i++) free(files[i]);
    free(files);
    pt_say("──────");
    pt_say("%d passed, %d failed (asserts: %d ok, %d fail)",
           scen_pass, scen_fail, total_pass, total_fail);
    return scen_fail > 0 ? 1 : 0;
}

int main(int argc, char **argv) {
    bool slow = false;
    bool keep_going = false;
    const char *seed_arg = NULL;
    const char *scenario_path = NULL;
    const char *suite_dir = NULL;
    const char *filter = NULL;
    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--slow") == 0) slow = true;
        else if (strcmp(argv[i], "--keep-going") == 0) keep_going = true;
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) seed_arg = argv[++i];
        else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) scenario_path = argv[++i];
        else if (strcmp(argv[i], "--suite") == 0 && i + 1 < argc) suite_dir = argv[++i];
        else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) filter = argv[++i];
    }
    pt_set_pause_ms(slow ? 600 : 80);
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    if (scenario_path) {
        int p = 0, f = 0;
        return run_scenario_file(scenario_path, seed_arg, &p, &f);
    }
    if (suite_dir) {
        return run_suite(suite_dir, filter, seed_arg, keep_going);
    }

    // Legacy default: run the hardcoded Phase-0 scenario.
    if (pt_spawn_game(seed_arg, NULL, SOCK_PATH, &g_gp) != 0) {
        pt_say("FATAL: could not spawn game");
        return 2;
    }
    pt_say("spawned openbounty pid=%d, sock=%s, seed=%s",
           g_gp.child, g_gp.sock_path, seed_arg ? seed_arg : "(time-based)");
    pt_say("connected");
    int rc = run_legacy_scenario(g_gp.fd);
    pt_shutdown_game(&g_gp);
    return rc;
}
