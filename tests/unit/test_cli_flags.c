// The command-line flags are listed once (src/cli_flags.c): --help prints that
// list, and README §3's flag table and OPENBOUNTY-SPEC REQ-480 must name
// exactly those flags, so none of the three drifts from the others.

#include "greatest.h"
#include "cli_flags.h"

#include <stdarg.h>
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

static int parse(CliOptions *o, int n, ...) {
    char *argv[16] = { "openbounty" };
    va_list ap;
    va_start(ap, n);
    for (int k = 0; k < n && k < 15; k++) argv[k + 1] = va_arg(ap, char *);
    va_end(ap);
    return cli_parse(n + 1, argv, o);
}

TEST parse_reads_values_and_modifiers(void) {
    CliOptions o;
    ASSERT_EQ(-1, parse(&o, 0));
    ASSERT_EQ(-1, o.seed_index);
    ASSERT_EQ(0, o.vp_lo);
    ASSERT_EQ(255, o.vp_hi);
    ASSERT_EQ(-1, parse(&o, 7, "--pack", "glory-of-rome", "--seed", "42",
                        "--autoplay", "--autoplay-level=hard", "--headless"));
    ASSERT_STR_EQ("glory-of-rome", o.pack_arg);
    ASSERT_EQ(42, o.seed_index);
    ASSERT(o.autoplay_mode && o.headless_mode);
    ASSERT_EQ(2, o.autoplay_level);
    ASSERT_EQ(-1, parse(&o, 3, "--validate-pack", "3", "9"));
    ASSERT(o.validate_pack);
    ASSERT_EQ(3, o.vp_lo);
    ASSERT_EQ(9, o.vp_hi);
    ASSERT_EQ(-1, parse(&o, 2, "--window", "1125x553"));
    ASSERT_EQ(1125, o.want_win_w);
    ASSERT_EQ(553, o.want_win_h);
    ASSERT_EQ(-1, parse(&o, 1, "--movie"));
    ASSERT(o.movie_requested);
    ASSERT_EQ(NULL, o.movie_path_arg);
    PASS();
}

TEST parse_is_strict(void) {
    CliOptions o;
    ASSERT_EQ(2, parse(&o, 1, "--no-such-flag"));
    ASSERT_EQ(2, parse(&o, 1, "--pack"));               // a value is missing
    ASSERT_EQ(2, parse(&o, 1, "--gallery"));
    ASSERT_EQ(2, parse(&o, 2, "--seed", "256"));        // out of range
    ASSERT_EQ(2, parse(&o, 2, "--seed", "0x10"));       // decimal only
    ASSERT_EQ(2, parse(&o, 3, "--validate-pack", "9", "3"));
    ASSERT_EQ(2, parse(&o, 1, "--autoplay-level=medium"));
    ASSERT_EQ(2, parse(&o, 2, "--window", "big"));
    PASS();
}

SUITE(unit_cli_flags_suite) {
    RUN_TEST(readme_flag_table_matches_the_list);
    RUN_TEST(spec_names_every_flag);
    RUN_TEST(parse_reads_values_and_modifiers);
    RUN_TEST(parse_is_strict);
}
