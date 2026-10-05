// The Introduction (#154): a script resolves to one timeline (beats end to
// end, for_each over the villain catalog); King's Bounty has none; and a
// malformed script refuses the load like a missing string.

#include "greatest.h"
#include "cJSON.h"
#include "fixtures.h"
#include "intro.h"
#include "intro_mix.h"
#include "audio.h"
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
static int    s_sounds;       // the last good load: its sound count, and the first one's start and gain
static double s_sound_at, s_sound_gain;

static bool load_with_intro(const char *script, bool with_label, int *beats) {
    *beats = -1;
    s_timeline_ok = false;
    s_sounds = 0;
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
            for (int i = 0; i < r->intro.beat_count; i++)
                for (int k = 0; k < r->intro.beats[i].sound_count; k++) {
                    if (s_sounds++ == 0) {
                        s_sound_at   = r->intro.beats[i].sounds[k].at;
                        s_sound_gain = r->intro.beats[i].sounds[k].gain;
                    }
                }
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

// Actor timing, one-shot actions and weather parse.
TEST staged_actors_and_weather_load(void) {
    int beats = 0;
    bool ok = load_with_intro(WRAP(
        "{\"backdrop\": \"art/a.png\", \"weather\": \"rain\", \"flashes\": [0.5, 1.5], \"duration\": 3,"
        " \"actors\": [{\"frames\": [\"art/s0.png\", \"art/s1.png\"], \"start\": 1, \"end\": 2.5,"
        "               \"loop\": false, \"at\": [0, 10], \"to\": [100, 10]}]}"), true, &beats);
    ASSERT(ok);
    ASSERT_EQ(1, beats);
    PASS();
}

// Sound effects: a file, started some seconds into the beat, at a gain.
TEST sounds_load(void) {
    int beats = 0;
    bool ok = load_with_intro(WRAP(
        "{\"duration\": 2, \"sounds\": [{\"file\": \"audio/thunder.wav\", \"at\": 0.5, \"gain\": 0.6},"
        "                              {\"file\": \"audio/crowd.wav\"}]}"), true, &beats);
    ASSERT(ok);
    ASSERT_EQ(2, s_sounds);
    ASSERT_IN_RANGE(0.5, s_sound_at, 1e-9);
    ASSERT_IN_RANGE(0.6, s_sound_gain, 1e-9);
    PASS();
}

TEST a_malformed_sound_refuses_the_load(void) {
    int beats = 0;
    ASSERT_FALSE(load_with_intro(WRAP("{\"duration\": 2, \"sounds\": [{\"at\": 1}]}"), true, &beats));
    ASSERT_FALSE(load_with_intro(WRAP(
        "{\"duration\": 2, \"sounds\": [{\"file\": \"audio/t.wav\", \"at\": 2}]}"), true, &beats));
    ASSERT_FALSE(load_with_intro(WRAP(
        "{\"duration\": 2, \"sounds\": [{\"file\": \"audio/t.wav\", \"gain\": 2}]}"), true, &beats));
    ASSERT_FALSE(load_with_intro(WRAP("{\"duration\": 2, \"sounds\": \"audio/t.wav\"}"), true, &beats));
    PASS();
}

TEST an_actor_outside_its_beat_refuses_the_load(void) {
    int beats = 0;
    ASSERT_FALSE(load_with_intro(WRAP(
        "{\"duration\": 2, \"actors\": [{\"frames\": [\"art/s.png\"], \"start\": 1.5, \"end\": 1}]}"),
        true, &beats));
    ASSERT_FALSE(load_with_intro(WRAP(
        "{\"duration\": 2, \"actors\": [{\"frames\": [\"art/s.png\"], \"start\": 3}]}"),
        true, &beats));
    PASS();
}

TEST unknown_weather_refuses_the_load(void) {
    int beats = 0;
    ASSERT_FALSE(load_with_intro(WRAP("{\"duration\": 2, \"weather\": \"snow\"}"), true, &beats));
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

// The film fits any screen: the picture at a whole multiple with room for
// the caption band, centred; the face never runs into the text.
TEST intro_layout_fits_the_screen(void) {
    static const int screens[][2] = { { 800, 504 }, { 640, 400 }, { 1125, 553 }, { 320, 200 } };
    for (size_t i = 0; i < sizeof screens / sizeof screens[0]; i++) {
        int sw = screens[i][0], sh = screens[i][1];
        IntroLayout l;
        intro_layout(sw, sh, 240, 102, 16, &l);
        ASSERT(l.scale >= 1);
        ASSERT_EQ(240 * l.scale, l.pic_w);
        ASSERT_EQ(102 * l.scale, l.pic_h);
        ASSERT(l.pic_x >= 0 && l.pic_x + l.pic_w <= sw);
        ASSERT(l.pic_y >= 0 && l.band_y + l.band_h <= sh);
        ASSERT_EQ(l.pic_y + l.pic_h, l.band_y);
        if (l.face_scale > 0) {
            ASSERT(l.face_x + 96 * l.face_scale < l.text_x);
            ASSERT(l.face_y + 96 * l.face_scale <= l.band_y + l.band_h);
        }
        ASSERT(l.text_w > 0);
    }
    // The reference screen: three times the art, a 96 px face beside the text.
    IntroLayout l;
    intro_layout(800, 504, 240, 102, 16, &l);
    ASSERT_EQ(3, l.scale);
    ASSERT_EQ(1, l.face_scale);
    PASS();
}


// --intro-movie's mix: a 10 s intro at 100 Hz, one scene fading out over
// its last 2 s, a two-beat timeline with one sound cued twice.
static void mix_fixture(ResIntro *in, ResIntroScene *sc, ResIntroBeat *bt, ResIntroSound *snd) {
    memset(in, 0, sizeof *in);
    memset(sc, 0, sizeof *sc);
    memset(bt, 0, 2 * sizeof *bt);
    memset(snd, 0, 2 * sizeof *snd);
    sc->start = 0; sc->dur = 10; sc->fade_out = 2; sc->beat_count = 2;
    bt[0].start = 0; bt[0].dur = 5;
    bt[1].start = 5; bt[1].dur = 5;
    strcpy(snd[0].path, "audio/boom.wav"); snd[0].at = 1;   snd[0].gain = 0.5;
    strcpy(snd[1].path, "audio/boom.wav"); snd[1].at = 0.5; snd[1].gain = 0.5;
    bt[0].sounds = &snd[0]; bt[0].sound_count = 1;    // at 1.0 s
    bt[1].sounds = &snd[1]; bt[1].sound_count = 1;    // at 5.5 s
    in->scenes = sc; in->scene_count = 1;
    in->beats = bt; in->beat_count = 2;
    in->total = 10;
}

TEST the_movie_mix_follows_the_players_levels_and_fade(void) {
    ResIntro in; ResIntroScene sc; ResIntroBeat bt[2]; ResIntroSound snd[2];
    mix_fixture(&in, &sc, bt, snd);
    float music = 0, sfx = 0;
    audio_intro_levels(&music, &sfx);
    static float ones[1000], out[1000];
    for (int i = 0; i < 1000; i++) ones[i] = 1;
    IntroClip theme = { ones, 1000 };
    intro_mix_render(&in, 1.0f, 100, &theme, NULL, NULL, 0, out, 1000);
    ASSERT_IN_RANGE(music, out[100], 1e-6);              // full until the fade
    ASSERT_IN_RANGE(music * 0.5f, out[900], 1e-6);       // halfway through it
    ASSERT_IN_RANGE(0.0f, out[999], music * 0.01f);      // gone at the end
    intro_mix_render(&in, 0.5f, 100, &theme, NULL, NULL, 0, out, 1000);
    ASSERT_IN_RANGE(music * 0.5f, out[100], 1e-6);       // master scales it
    PASS();
}

TEST the_movie_mix_starts_each_sound_at_its_cue_and_restarts_it(void) {
    ResIntro in; ResIntroScene sc; ResIntroBeat bt[2]; ResIntroSound snd[2];
    mix_fixture(&in, &sc, bt, snd);
    float sfx = 0;
    audio_intro_levels(NULL, &sfx);
    static float boom[600], out[1000];
    for (int i = 0; i < 600; i++) boom[i] = (float)(i + 1);   // its position, so a restart shows
    IntroClip clip = { boom, 600 };
    const char *paths[1] = { "audio/boom.wav" };
    intro_mix_render(&in, 1.0f, 100, NULL, paths, &clip, 1, out, 1000);
    ASSERT_EQ_FMT(0.0f, out[99], "%f");                         // nothing before the cue
    ASSERT_IN_RANGE(1 * sfx * 0.5f, out[100], 1e-4);            // its first sample at 1.0 s
    ASSERT_IN_RANGE(450 * sfx * 0.5f, out[549], 1e-3);          // still playing at 5.49 s
    ASSERT_IN_RANGE(1 * sfx * 0.5f, out[550], 1e-4);            // and from the top at 5.5 s
    const char *other[1] = { "audio/other.wav" };
    intro_mix_render(&in, 1.0f, 100, NULL, other, &clip, 1, out, 1000);
    ASSERT_EQ_FMT(0.0f, out[100], "%f");                        // no clip, no sound
    PASS();
}

SUITE(unit_intro_suite) {
    RUN_TEST(intro_layout_fits_the_screen);
    RUN_TEST(kings_bounty_has_no_intro);
    RUN_TEST(a_well_formed_intro_loads_and_reloads);
    RUN_TEST(staged_actors_and_weather_load);
    RUN_TEST(sounds_load);
    RUN_TEST(a_malformed_sound_refuses_the_load);
    RUN_TEST(the_movie_mix_follows_the_players_levels_and_fade);
    RUN_TEST(the_movie_mix_starts_each_sound_at_its_cue_and_restarts_it);
    RUN_TEST(an_actor_outside_its_beat_refuses_the_load);
    RUN_TEST(unknown_weather_refuses_the_load);
    RUN_TEST(an_unknown_caption_key_refuses_the_load);
    RUN_TEST(a_beat_with_no_length_refuses_the_load);
    RUN_TEST(an_unknown_portrait_refuses_the_load);
    RUN_TEST(an_intro_without_its_menu_label_refuses_the_load);
}
