// The command-line flags are listed once (src/cli_flags.c): --help prints that
// list, and README §3's flag table and OPENBOUNTY-SPEC REQ-480 must name
// exactly those flags, so none of the three drifts from the others.

#include "greatest.h"
#include "cli_flags.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    if (buf) buf[n] = '\0';
    fclose(f);
    return buf;
}

// The text from `start` up to `end` (exclusive), or NULL when either is missing.
static char *section(const char *text, const char *start, const char *end) {
    const char *a = text ? strstr(text, start) : NULL;
    const char *b = a ? strstr(a + strlen(start), end) : NULL;
    if (!a || !b) return NULL;
    size_t n = (size_t)(b - a);
    char *out = malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, a, n);
    out[n] = '\0';
    return out;
}

static bool listed(const char *flag) {
    for (int i = 0; i < cli_flag_count; i++)
        if (strcmp(cli_flags[i].flag, flag) == 0) return true;
    return false;
}

TEST readme_flag_table_matches_the_list(void) {
    char *readme = slurp("README.md");
    ASSERT(readme);
    char *table = section(readme, "### `build/debug/openbounty`, the game",
                          "### `build/openbounty-test`");
    free(readme);
    ASSERT(table);
    // Every listed flag has its row.
    for (int i = 0; i < cli_flag_count; i++) {
        char row[64];
        snprintf(row, sizeof row, "\n| `%s`", cli_flags[i].flag);
        ASSERTm(cli_flags[i].flag, strstr(table, row) != NULL);
    }
    // Every row names a listed flag.
    for (const char *p = strstr(table, "\n| `--"); p; p = strstr(p + 1, "\n| `--")) {
        char flag[48] = { 0 };
        const char *s = p + 4;
        size_t n = strcspn(s, "`");
        if (n >= sizeof flag) n = sizeof flag - 1;
        memcpy(flag, s, n);
        ASSERTm(flag, listed(flag));
    }
    free(table);
    PASS();
}

TEST spec_names_every_flag(void) {
    char *spec = slurp("docs/OPENBOUNTY-SPEC.md");
    ASSERT(spec);
    char *req = section(spec, "- **REQ-480.**", "### 34.2");
    free(spec);
    ASSERT(req);
    for (int i = 0; i < cli_flag_count; i++) {
        char name[64];
        snprintf(name, sizeof name, "`%s", cli_flags[i].flag);
        ASSERTm(cli_flags[i].flag, strstr(req, name) != NULL);
    }
    free(req);
    PASS();
}

SUITE(unit_cli_flags_suite) {
    RUN_TEST(readme_flag_table_matches_the_list);
    RUN_TEST(spec_names_every_flag);
}
