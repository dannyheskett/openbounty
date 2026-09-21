// Every player-facing moment is raised by name.
//
// The engine says WHAT KIND of moment it is at the call site -- a note, a note
// in the open screen, a note with a face, a scene, an ask, an ask in place, an
// ask as a scene, a how-many, a pick-one, a self-answered ask, a screen -- and
// each kind has exactly one drawing function. Three rules keep it that way, and
// this scan fails the build when one is broken:
//
//   1. The old generic calls are gone; nothing may name them again.
//   2. Only engine/player_io.c sets a request's face: a face comes from the
//      helper that names the kind, never from poking the request afterwards.
//   3. Engine code does not open a host prompt itself -- the ask helpers do, so
//      a site cannot raise a decision without its prompt or the other way round.

#include "greatest.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct { const char *needle; const char *why; bool engine_only; } Rule;

static const Rule RULES[] = {
    { "player_io_message(",          "deleted: use player_io_note*",        false },
    { "player_io_raise_decision(",   "deleted: use player_io_ask*",         false },
    { "player_io_raise_view(",       "deleted: use player_io_screen",       false },
    { "player_io_enqueue_message(",  "deleted: use player_io_note*",        false },
    { "player_io_enqueue_decision(", "deleted: use player_io_ask*",         false },
    { "player_io_enqueue_view(",     "deleted: use player_io_screen",       false },
    { "->face =",                    "a face belongs to the helper that names the kind", false },
    { "prompt_yes_no_open(",         "the ask helper opens the host prompt", true },
    { "prompt_ab_open(",             "the ask helper opens the host prompt", true },
    { "prompt_text_input_open(",     "the ask helper opens the host prompt", true },
};
#define NRULES (sizeof RULES / sizeof *RULES)

// engine/player_io.c is where the helpers live, and host_noop.c defines the
// host prompts for a headless consumer; both name these on purpose.
static bool exempt(const char *path) {
    return strstr(path, "engine/player_io.c") || strstr(path, "engine/host_noop.c") ||
           strstr(path, "engine/include/ui_host.h") || strstr(path, "engine/include/player_io.h");
}

static int scan_file(const char *path, bool engine, char *first, size_t firstsz) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char line[4096];
    int hits = 0, ln = 0;
    while (fgets(line, sizeof line, f)) {
        ln++;
        const char *code = line;
        while (*code == ' ' || *code == '\t') code++;
        if (code[0] == '/' && (code[1] == '/' || code[1] == '*')) continue;
        if (code[0] == '*') continue;
        for (size_t i = 0; i < NRULES; i++) {
            if (RULES[i].engine_only && !engine) continue;
            if (!strstr(line, RULES[i].needle)) continue;
            if (!first[0])
                snprintf(first, firstsz, "%s:%d: %s -- %s", path, ln, RULES[i].needle, RULES[i].why);
            hits++;
            break;
        }
    }
    fclose(f);
    return hits;
}

static int scan_tree(const char *root, bool engine, char *first, size_t firstsz) {
    DIR *d = opendir(root);
    if (!d) return 0;
    int hits = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", root, e->d_name);
        DIR *sub = opendir(path);
        if (sub) { closedir(sub); hits += scan_tree(path, engine, first, firstsz); continue; }
        size_t len = strlen(e->d_name);
        if (len < 2 || (strcmp(e->d_name + len - 2, ".c") != 0 &&
                        strcmp(e->d_name + len - 2, ".h") != 0)) continue;
        if (exempt(path)) continue;
        hits += scan_file(path, engine, first, firstsz);
    }
    closedir(d);
    return hits;
}

TEST every_moment_is_raised_by_name(void) {
    char first[1024] = { 0 };
    int hits = 0;
    hits += scan_tree("engine", true,  first, sizeof first);
    hits += scan_tree("src",    false, first, sizeof first);
    hits += scan_tree("autoplay", false, first, sizeof first);
    hits += scan_tree("demo",   false, first, sizeof first);
    ASSERT_EQm(first, 0, hits);
    PASS();
}

SUITE(unit_io_kinds_suite) {
    RUN_TEST(every_moment_is_raised_by_name);
}
