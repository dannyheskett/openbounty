// The Introduction (#154): Glory of Rome's script resolves to one contiguous
// timeline whose captions, faces and art all exist; King's Bounty has none;
// and a malformed script refuses the load like a missing string.

#include "greatest.h"
#include "cJSON.h"
#include "fixtures.h"
#include "pack.h"
#include "resources.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INTRO_DIR "/tmp/ob_intro_pack"

TEST rome_intro_resolves_to_one_timeline(void) {
    Pack *p = pack_open("assets/glory-of-rome");
    ASSERT(p);
    pack_stack_push(p);
    Resources *r = calloc(1, sizeof *r);
    bool ok = r && resources_load(r, "game.json");
    // Read everything into flags first, so a failure never leaves the Rome
    // pack on the stack for later suites.
    bool has = ok && resources_has_intro(r);
    int scenes = ok ? r->intro.scene_count : 0;
    bool label = ok && r->ui.title_intro[0];
    bool contiguous = true, durations = true, fades = true, captions = true, faces = true;
    double t = 0;
    for (int i = 0; ok && i < r->intro.beat_count; i++) {
        const ResIntroBeat *b = &r->intro.beats[i];
        if (b->start < t - 1e-9 || b->start > t + 1e-9) contiguous = false;
        if (b->dur <= 0) durations = false;
        t = b->start + b->dur;
        if (b->caption && (!b->caption[0] || strchr(b->caption, '%'))) captions = false;
        if (b->card && (!b->card[0] || strchr(b->card, '%'))) captions = false;
        if (strchr(b->backdrop, '%')) captions = false;
        if (b->caption && b->face_count == 0 && b->face) faces = false;
    }
    for (int s = 0; ok && s < scenes; s++) {
        const ResIntroScene *sc = &r->intro.scenes[s];
        if (sc->fade_in + sc->fade_out > sc->dur) fades = false;
        if (sc->beat_count <= 0) durations = false;
    }
    double total = ok ? r->intro.total : 0;
    bool ends = ok && t > total - 1e-9 && t < total + 1e-9;

    // The wanted notices: one beat per villain, in catalog order.
    int wanted = 0;
    bool order = true;
    for (int i = 0; ok && i < r->intro.beat_count; i++) {
        const ResIntroBeat *b = &r->intro.beats[i];
        if (strcmp(r->intro.scenes[b->scene].id, "wanted") != 0) continue;
        const char *text = b->caption ? b->caption : b->card;
        if (wanted < r->villains_count && b->actor_count == 1) {
            if (!text || !strstr(text, r->villains[wanted].name)) order = false;
            wanted++;
        }
    }
    int villains = ok ? r->villains_count : -1;

    // Every intro path is listed in the art manifest and is in the pack.
    bool listed = true, present = true;
    ResArtList art = { 0 };
    if (ok) resources_art_manifest(r, &art);
    for (int i = 0; ok && i < r->intro.beat_count; i++) {
        const ResIntroBeat *b = &r->intro.beats[i];
        const char *paths[64];
        int np = 0;
        if (b->backdrop[0]) paths[np++] = b->backdrop;
        for (int a = 0; a < b->actor_count && np < 60; a++)
            for (int f = 0; f < b->actors[a].frame_count && np < 60; f++)
                paths[np++] = b->actors[a].frames[f];
        for (int f = 0; f < b->face_count && np < 63; f++) paths[np++] = b->face[f];
        for (int k = 0; k < np; k++) {
            bool in_list = false;
            for (int m = 0; m < art.n && !in_list; m++) in_list = strcmp(art.path[m], paths[k]) == 0;
            if (!in_list) listed = false;
            size_t sz = 0;
            const unsigned char *bytes = pack_stack_read(paths[k], &sz);
            if (!bytes || sz == 0) { present = false; fprintf(stdout, "intro art missing: %s\n", paths[k]); }
        }
    }
    resources_art_list_free(&art);

    // The beat at the bounds of the timeline.
    bool bounds = ok && resources_intro_beat_at(&r->intro, -0.1) == NULL &&
                  resources_intro_beat_at(&r->intro, 0) == &r->intro.beats[0] &&
                  resources_intro_beat_at(&r->intro, total) == NULL &&
                  resources_intro_beat_at(&r->intro, r->intro.beats[1].start) == &r->intro.beats[1];

    if (r) resources_free(r);
    free(r);
    pack_stack_pop();

    ASSERT(ok);
    ASSERT(has);
    ASSERT_EQ(5, scenes);
    ASSERT(label);
    ASSERTm("beats lie end to end", contiguous && ends);
    ASSERT(durations);
    ASSERTm("a scene's fades fit inside it", fades);
    ASSERTm("every caption resolved, no %TOKEN% left", captions);
    ASSERT(faces);
    ASSERTm("the intro runs two to ten minutes", total >= 120 && total <= 600);
    ASSERT_EQ(villains, wanted);
    ASSERTm("the wanted notices run in catalog order", order);
    ASSERTm("every intro path is in the art manifest", listed);
    ASSERTm("every intro path is in the pack", present);
    ASSERT(bounds);
    PASS();
}

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
static bool load_with_intro(const char *script, bool with_label, int *beats) {
    *beats = -1;
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
        if (loaded) *beats = r->intro.beat_count;
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
    RUN_TEST(rome_intro_resolves_to_one_timeline);
    RUN_TEST(kings_bounty_has_no_intro);
    RUN_TEST(a_well_formed_intro_loads_and_reloads);
    RUN_TEST(an_unknown_caption_key_refuses_the_load);
    RUN_TEST(a_beat_with_no_length_refuses_the_load);
    RUN_TEST(an_unknown_portrait_refuses_the_load);
    RUN_TEST(an_intro_without_its_menu_label_refuses_the_load);
}
