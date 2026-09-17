// Game, Map and Fog own heap tables, so a copy by `=` shares them: the copy's
// writes land in the original and both free the same memory. GameCopy, FogCopy
// and MapAlloc are the only way to duplicate one. This scan fails the build on
// a by-value copy anywhere in the engine, shell, autoplay, demo or GameBuilder.

#include "greatest.h"

#include <dirent.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// Each pattern is one shape of a by-value copy.
static const char *const COPY_PATTERNS[] = {
    // Game x = *g;   Fog f = *fog;   Map m = *map;
    "(^|[^A-Za-z0-9_])(Game|Fog|Map)[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*=[[:space:]]*\\*",
    // x = *g;   *tmp = *game;   s = *ctx->g;
    "(^|[^=!<>])=[[:space:]]*\\*[[:space:]]*(g|game|src|hero|fog|map|ctx->g|ctx->fog|ctx->map)[[:space:]]*;",
    // *g = x;   *fog = snap->fog;
    "(^|[;{}])[[:space:]]*\\*[[:space:]]*(g|game|fog|ctx->g|ctx->fog)[[:space:]]*=[^=]",
    // snap->game = ...;   world.continent_fog[i] = ...;
    "(snap->game|snap->fog|continent_fog\\[[^]]*\\])[[:space:]]*=[^=]",
};
#define NPAT (sizeof COPY_PATTERNS / sizeof *COPY_PATTERNS)

static int scan_file(const char *path, regex_t *re, char *first, size_t firstsz) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char line[4096];
    int hits = 0, ln = 0;
    while (fgets(line, sizeof line, f)) {
        ln++;
        const char *code = line;
        while (*code == ' ' || *code == '\t') code++;
        if (code[0] == '/' && (code[1] == '/' || code[1] == '*')) continue;   // a comment
        if (code[0] == '*') continue;                                        // a comment's body
        for (size_t i = 0; i < NPAT; i++) {
            if (regexec(&re[i], line, 0, NULL, 0) != 0) continue;
            if (!first[0]) snprintf(first, firstsz, "%s:%d: %s", path, ln, code);
            hits++;
            break;
        }
    }
    fclose(f);
    return hits;
}

static int scan_tree(const char *root, regex_t *re, char *first, size_t firstsz) {
    DIR *d = opendir(root);
    if (!d) return 0;
    int hits = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", root, e->d_name);
        DIR *sub = opendir(path);
        if (sub) {
            closedir(sub);
            hits += scan_tree(path, re, first, firstsz);
            continue;
        }
        size_t len = strlen(e->d_name);
        if (len < 2 || (strcmp(e->d_name + len - 2, ".c") != 0 &&
                        strcmp(e->d_name + len - 2, ".h") != 0)) continue;
        hits += scan_file(path, re, first, firstsz);
    }
    closedir(d);
    return hits;
}

TEST no_game_map_or_fog_is_copied_by_value(void) {
    regex_t re[NPAT];
    for (size_t i = 0; i < NPAT; i++)
        ASSERT_EQ(0, regcomp(&re[i], COPY_PATTERNS[i], REG_EXTENDED | REG_NOSUB));
    char first[1024] = { 0 };
    int hits = 0;
    static const char *const roots[] = { "engine", "src", "autoplay", "demo", "tools/gamebuilder" };
    for (size_t r = 0; r < sizeof roots / sizeof *roots; r++)
        hits += scan_tree(roots[r], re, first, sizeof first);
    for (size_t i = 0; i < NPAT; i++) regfree(&re[i]);
    ASSERT_EQm(first, 0, hits);
    PASS();
}

// The scan itself catches each shape.
TEST the_scan_catches_every_copy_shape(void) {
    static const char *const bad[] = {
        "    Game keep = *g;\n",
        "        *tmp = *g;\n",
        "    s_sim = *ctx->g;\n",
        "    *g = keep;\n",
        "    *fog = snap->fog;\n",
        "    snap->game = x;\n",
        "        g->world.continent_fog[old_zi] = f;\n",
        "    Fog f = *fog;\n",
    };
    static const char *const good[] = {
        "    GameCopy(&keep, g);\n",
        "    if (a == *g) x();\n",
        "    int v = *value;\n",
        "    const Tile *t = &MAP_TILE(m, x, y);\n",
        "    Game game = { 0 };\n",
        "        .game = &game, .map = &map,\n",
        "    const Game *g = ((const Ctx *)ctx)->g;\n",
        "    Fog *fog = calloc(1, sizeof *fog);\n",
    };
    regex_t re[NPAT];
    for (size_t i = 0; i < NPAT; i++)
        ASSERT_EQ(0, regcomp(&re[i], COPY_PATTERNS[i], REG_EXTENDED | REG_NOSUB));
    for (size_t b = 0; b < sizeof bad / sizeof *bad; b++) {
        bool hit = false;
        for (size_t i = 0; i < NPAT; i++) hit = hit || regexec(&re[i], bad[b], 0, NULL, 0) == 0;
        ASSERTm(bad[b], hit);
    }
    for (size_t k = 0; k < sizeof good / sizeof *good; k++) {
        bool hit = false;
        for (size_t i = 0; i < NPAT; i++) hit = hit || regexec(&re[i], good[k], 0, NULL, 0) == 0;
        ASSERT_FALSEm(good[k], hit);
    }
    for (size_t i = 0; i < NPAT; i++) regfree(&re[i]);
    PASS();
}

SUITE(unit_no_state_copy_suite) {
    RUN_TEST(the_scan_catches_every_copy_shape);
    RUN_TEST(no_game_map_or_fog_is_copied_by_value);
}
