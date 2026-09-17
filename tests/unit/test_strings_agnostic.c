// Guard tests for the string-agnostic engine (the strings refactor).
//
//  1. Every shipped pack satisfies the full required-key manifest. The loader
//     hard-fails when a pack omits any user-facing key, so a successful load
//     is proof of a complete pack -- no engine fallback can mask a gap.
//
//  2. No engine/shell source passes a bare string literal to a text renderer.
//     All user-facing wording must come from the pack; a literal in
//     bfont_draw / bfont_draw_centered / open_dialog is a leak. Dev-only
//     tooling (autoplay, demo, movie encode) and the cursor glyph are the only
//     allowlisted exceptions.

#include "greatest.h"
#include "fixtures.h"
#include "pack.h"
#include "resources.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- 1. pack completeness --------------------------------------------------

static bool pack_loads_complete(const char *dir) {
    Pack *p = pack_open(dir);
    if (!p) return false;
    pack_stack_push(p);
    Resources *r = calloc(1, sizeof *r);
    bool ok = r && resources_load(r, "game.json");
    if (r) { resources_free(r); free(r); }
    pack_stack_pop();   // pops AND closes p (owns it after push)
    return ok;
}

TEST kings_bounty_pack_has_every_string(void) {
    ASSERTm("kings-bounty pack failed strict load (missing string keys)",
            pack_loads_complete("assets/kings-bounty"));
    PASS();
}

TEST glory_of_rome_pack_has_every_string(void) {
    ASSERTm("glory-of-rome pack failed strict load (missing string keys)",
            pack_loads_complete("assets/glory-of-rome"));
    PASS();
}

// ---- 2. no hardcoded user-facing strings -----------------------------------

// Text renderers whose first string-literal argument is always user-facing
// wording (never a format spec, by convention -- callers snprintf into a
// buffer first). A literal here is a pack-string leak.
static const char *const RENDER_CALLS[] = {
    "bfont_draw(\"",
    "bfont_draw_centered(\"",
    "open_dialog(\"",
};

// Dev-only tooling that never ships as pack content.
static bool file_allowlisted(const char *path) {
    static const char *const files[] = {
        "src/main.c",          // no-pack / demo / autoplay CLI dialogs
        "src/shell_autoplay.c",// oracle progress overlay
        "src/shell_demo.c",    // demo-mode telemetry
        "src/encode_dialog.c", // movie-encode dev tool
        "src/shell_gallery.c", // --gallery capture harness: sample wording only
        "src/shell_cheats.c",  // --debug cheats: never reachable in a shipped game
    };
    for (size_t i = 0; i < sizeof files / sizeof *files; i++) {
        if (strstr(path, files[i])) return true;
    }
    return false;
}

// The only literal allowed at a renderer: the selection-cursor glyph.
static bool literal_allowlisted(const char *lit) {
    return strcmp(lit, ">") == 0;
}

// Calls that raise a message or a question to the player: their words are pack
// strings too, wherever they sit in the argument list. A Roman artifact that
// talks about a "scepter" is what this catches.
static const char *const RAISE_CALLS[] = {
    "player_io_note(",
    "player_io_note_in_place(",
    "player_io_note_face(",
    "player_io_note_scene(",
    "player_io_ask(",
    "player_io_ask_in_place(",
    "player_io_ask_face(",
    "player_io_ask_scene(",
    "player_io_ask_number(",
    "player_io_ask_number_in_place(",
    "player_io_ask_choice(",
};

// Wording, as opposed to an id, a format spec or an escape: four or more
// letters that are not a %-conversion, and either a space or a capitalised
// word -- so "An uncharted castle." is caught and the id "time_stop" is not.
static bool literal_has_words(const char *lit) {
    int letters = 0;
    bool spaced = false, capitalised = false;
    for (const char *p = lit; *p; p++) {
        if (*p == '\\' && p[1]) { p++; continue; }              // an escape
        if (*p == '%' && p[1]) { p++; continue; }                // a conversion
        if (*p == ' ') { spaced = true; continue; }
        if (*p >= 'A' && *p <= 'Z') { capitalised = true; letters++; continue; }
        if (*p >= 'a' && *p <= 'z') letters++;
    }
    return letters >= 4 && (spaced || capitalised);
}

// Flag every worded literal inside a player_io_* call, which may run over
// several lines. Returns the count; `first` takes the first offender.
static int scan_raise_calls(const char *path, char *first, size_t firstsz) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char line[2048];
    int lineno = 0, hits = 0, depth = 0, call_line = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        const char *p = line;
        if (depth == 0) {
            const char *at = NULL;
            for (size_t t = 0; t < sizeof RAISE_CALLS / sizeof *RAISE_CALLS; t++) {
                const char *h = strstr(line, RAISE_CALLS[t]);
                if (h && (!at || h < at)) at = h + strlen(RAISE_CALLS[t]);
            }
            if (!at) continue;
            depth = 1;
            call_line = lineno;
            p = at;
        }
        for (; *p; p++) {
            if (*p == '(') { depth++; continue; }
            if (*p == ')') { if (--depth <= 0) { depth = 0; break; } continue; }
            if (strncmp(p, "spell_header(", 13) == 0) {
                // spell_header(id, "Name"): the literal is a fallback the
                // strict loader makes unreachable. Skip the whole call.
                int d2 = 0;
                for (p += 12; *p; p++) {
                    if (*p == '(') d2++;
                    else if (*p == ')') { if (--d2 <= 0) break; }
                }
                if (!*p) break;
                continue;
            }
            if (*p != '"') continue;
            char buf[256];
            size_t n = 0;
            for (p++; *p && *p != '"' && n + 1 < sizeof buf; p++) {
                if (*p == '\\' && p[1]) buf[n++] = *p++;
                buf[n++] = *p;
            }
            buf[n] = '\0';
            if (!literal_has_words(buf)) continue;
            hits++;
            if (first[0] == '\0')
                snprintf(first, firstsz, "%s:%d player_io_*(.. \"%s\" ..)", path, call_line, buf);
        }
    }
    fclose(f);
    return hits;
}

// Scan one .c file. Returns the violation count; writes the first offender
// (file:line + literal) into `first` when non-empty.
static int scan_c_file(const char *path, char *first, size_t firstsz) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char line[2048];
    int lineno = 0, hits = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        for (size_t t = 0; t < sizeof RENDER_CALLS / sizeof *RENDER_CALLS; t++) {
            const char *tok = RENDER_CALLS[t];
            const char *at = strstr(line, tok);
            if (!at) continue;
            const char *lit = at + strlen(tok);         // literal content start
            const char *end = strchr(lit, '"');         // closing quote
            char buf[256];
            size_t n = end ? (size_t)(end - lit) : 0;
            if (n >= sizeof buf) n = sizeof buf - 1;
            memcpy(buf, lit, n);
            buf[n] = '\0';
            if (literal_allowlisted(buf)) continue;
            hits++;
            if (first[0] == '\0') {
                snprintf(first, firstsz, "%s:%d %s%s\"...)", path, lineno, tok, buf);
            }
        }
    }
    fclose(f);
    return hits;
}

// Recursively scan every non-allowlisted *.c file under `root`.
static int scan_tree(const char *root, char *first, size_t firstsz) {
    DIR *d = opendir(root);
    if (!d) return 0;
    int hits = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;   // skip ., .., dotfiles
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", root, e->d_name);
        DIR *sub = opendir(path);
        if (sub) {                            // directory: recurse
            closedir(sub);
            hits += scan_tree(path, first, firstsz);
            continue;
        }
        size_t len = strlen(e->d_name);
        if (len < 2 || strcmp(e->d_name + len - 2, ".c") != 0) continue;
        if (file_allowlisted(path)) continue;
        hits += scan_c_file(path, first, firstsz);
        hits += scan_raise_calls(path, first, firstsz);
    }
    closedir(d);
    return hits;
}

TEST engine_and_shell_have_no_hardcoded_strings(void) {
    char first[1024] = { 0 };
    int hits = 0;
    hits += scan_tree("engine", first, sizeof first);
    hits += scan_tree("src",    first, sizeof first);
    ASSERT_EQm(first, 0, hits);
    PASS();
}

SUITE(unit_strings_agnostic_suite) {
    RUN_TEST(kings_bounty_pack_has_every_string);
    RUN_TEST(glory_of_rome_pack_has_every_string);
    RUN_TEST(engine_and_shell_have_no_hardcoded_strings);
}
