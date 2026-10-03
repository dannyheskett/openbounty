// The Introduction (#154): a script resolves to one timeline (beats end to
// end, for_each over the villain catalog); King's Bounty has none; and a
// malformed script refuses the load like a missing string.

#include "greatest.h"
#include "cJSON.h"
#include "fixtures.h"
#include "pack.h"
#include "resources.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INTRO_DIR "/tmp/ob_intro_pack"

TEST kings_bounty_has_no_intro(void) {
    Resources *r = fx_load_resources();
    ASSERT(r);
    bool has = resources_has_intro(r);
    bool label = r->ui.title_intro[0] != '\0';
    resources_free(r);
    free(r);
    ASSERT_FALSE(has);
    ASSERT_FALSE(label);
    PASS();
}

// ---- A malformed script refuses the load -----------------------------------
//
// An overlay over King's Bounty: its game.json with "intro" added, its strings
// with (or without) the intro's, and the script itself.

static cJSON *read_json(const char *path) {
    size_t n = 0;
    const unsigned char *bytes = pack_stack_read(path, &n);   // borrowed
    if (!bytes) return NULL;
    char *text = malloc(n + 1);
    if (!text) return NULL;
    memcpy(text, bytes, n);
    text[n] = '\0';
    cJSON *j = cJSON_Parse(text);
    free(text);
    return j;
}

static bool write_json(const char *path, cJSON *j) {
    char *text = cJSON_Print(j);
    FILE *f = text ? fopen(path, "wb") : NULL;
    bool ok = f && fputs(text, f) >= 0;
    if (f) fclose(f);
    free(text);
    return ok;
}

// Load King's Bounty with `script` as its intro. `with_label` adds
// ui.title_intro; the strings always carry intro.hello. Returns the load
// result; *beats holds the beat count on success. Pushes and pops only its
// own two packs: popping closes a pack, and the shared fixture pack may sit
// below.
static bool s_timeline_ok;   // the last good load: beats end to end, villains in order

static bool load_with_intro(const char *script, bool with_label, int *beats) {
    *beats = -1;
    s_timeline_ok = false;
    Pack *kb = pack_open(FIXTURE_PACK_DIR);
    if (!kb) return false;
    pack_stack_push(kb);
    cJSON *game = read_json("game.json");
    cJSON *strings = read_json("strings/en.json");
    bool ok = game && strings && system("rm -rf " INTRO_DIR " && mkdir -p " INTRO_DIR "/strings") == 0;
    if (ok) {
        cJSON_AddStringToObject(game, "intro", "intro.json");
        cJSON *ui = cJSON_GetObjectItem(strings, "ui");
        if (with_label && ui) cJSON_AddStringToObject(ui, "title_intro", "Introduction");
        cJSON *grp = cJSON_AddObjectToObject(strings, "intro");
        cJSON_AddStringToObject(grp, "hello", "Hear the news, %NAME%!");
        FILE *f = fopen(INTRO_DIR "/intro.json", "wb");
        ok = f && fputs(script, f) >= 0;
        if (f) fclose(f);
        ok = ok && write_json(INTRO_DIR "/game.json", game) &&
             write_json(INTRO_DIR "/strings/en.json", strings);
    }
    cJSON_Delete(game);
    cJSON_Delete(strings);
    Pack *overlay = ok ? pack_open(INTRO_DIR) : NULL;
    bool loaded = false;
    if (overlay) {
        pack_stack_push(overlay);
        Resources *r = calloc(1, sizeof *r);
        loaded = r && resources_load(r, "game.json");
        if (loaded) {
            *beats = r->intro.beat_count;
            // End to end, and each villain beat names its villain, in order.
            bool ok = r->intro.total > 0;
            double t = 0;
            int v = 0;
            for (int i = 0; i < r->intro.beat_count; i++) {
                const ResIntroBeat *b = &r->intro.beats[i];
                if (b->start < t - 1e-9 || b->start > t + 1e-9 || b->dur <= 0) ok = false;
                t = b->start + b->dur;
                if (b->actor_count == 1) {
                    if (v >= r->villains_count || !b->caption ||
                        !strstr(b->caption, r->villains[v].name)) ok = false;
                    v++;
                }
            }
            s_timeline_ok = ok && v == r->villains_count &&
                            t > r->intro.total - 1e-9 && t < r->intro.total + 1e-9 &&
                            resources_intro_beat_at(&r->intro, 0) == &r->intro.beats[0] &&
                            resources_intro_beat_at(&r->intro, r->intro.total) == NULL;
        }
        if (r) resources_free(r);
        // Load again: a freed intro leaves nothing behind.
        if (loaded) {
            loaded = resources_load(r, "game.json") && r->intro.beat_count == *beats;
            resources_free(r);
        }
        free(r);
        pack_stack_pop();   // the overlay
    }
    pack_stack_pop();       // King's Bounty
    return loaded;
}

#define BEAT_SAY    "{\"say\": \"hello\"}"
#define WRAP(beats) "{\"scenes\": [{\"id\": \"a\", \"fade_in\": 0, \"fade_out\": 0, \"beats\": [" beats "]}]}"

TEST a_well_formed_intro_loads_and_reloads(void) {
    int beats = 0;
    bool ok = load_with_intro(WRAP(BEAT_SAY ", {\"for_each\": \"villain\", \"say\": \"hello\","
                                   " \"actors\": [{\"villain\": \"*\", \"at\": [1, 2]}]}"),
                              true, &beats);
    Resources *kb = fx_load_resources();
    int villains = kb ? kb->villains_count : 0;
    if (kb) { resources_free(kb); free(kb); }
    ASSERT(ok);
    ASSERT_EQ(1 + villains, beats);
    ASSERTm("beats end to end, the villains in catalog order", s_timeline_ok);
    PASS();
}

TEST an_unknown_caption_key_refuses_the_load(void) {
    int beats = 0;
    ASSERT_FALSE(load_with_intro(WRAP("{\"say\": \"no_such_key\"}"), true, &beats));
    PASS();
}

TEST a_beat_with_no_length_refuses_the_load(void) {
    int beats = 0;
    ASSERT_FALSE(load_with_intro(WRAP("{\"backdrop\": \"art/x.png\"}"), true, &beats));
    PASS();
}

TEST an_unknown_portrait_refuses_the_load(void) {
    int beats = 0;
    ASSERT_FALSE(load_with_intro(WRAP("{\"say\": \"hello\", \"face\": \"nobody\"}"), true, &beats));
    PASS();
}

TEST an_intro_without_its_menu_label_refuses_the_load(void) {
    int beats = 0;
    ASSERT_FALSE(load_with_intro(WRAP(BEAT_SAY), false, &beats));
    PASS();
}

SUITE(unit_intro_suite) {
    RUN_TEST(kings_bounty_has_no_intro);
    RUN_TEST(a_well_formed_intro_loads_and_reloads);
    RUN_TEST(an_unknown_caption_key_refuses_the_load);
    RUN_TEST(a_beat_with_no_length_refuses_the_load);
    RUN_TEST(an_unknown_portrait_refuses_the_load);
    RUN_TEST(an_intro_without_its_menu_label_refuses_the_load);
}
