// engine/resources_intro.c -- the pack's Introduction script (game.json
// "intro"), parsed and checked for resources_load.

#include "resources_internal.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- Introduction (game.json "intro") --------------------------------------
//
// The pack's opening cinematic, a separate script file named by game.json
// (PACK-FORMAT section 2.4). Resolved here to a flat list of beats on one timeline:
// caption keys to text (strings/<lang>.json "intro"), portrait and villain ids
// to frame lists, and each "for_each": "villain" beat to one beat per villain
// (catalog order, optionally "from" a position for "count" villains).
// Runs after parse_strings, while the strings JSON is still open.

#define INTRO_TEXT_MAX 2048
#define INTRO_FRAME_MAX 256   // RD Pro's largest side; nothing wider is authored

static double json_num(const cJSON *obj, const char *key, double fallback) {
    cJSON *v = obj ? cJSON_GetObjectItem(obj, key) : NULL;
    return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}

static ResIntroPt json_pt(const cJSON *v, ResIntroPt fallback) {
    if (cJSON_GetArraySize(v) != 2) return fallback;
    cJSON *x = cJSON_GetArrayItem(v, 0), *y = cJSON_GetArrayItem(v, 1);
    if (!cJSON_IsNumber(x) || !cJSON_IsNumber(y)) return fallback;
    return (ResIntroPt){ x->valueint, y->valueint };
}

// A villain's wanted-poster frames as a heap list: the declared anim, else the
// <portrait-stem>_NN siblings the shell derives (kings-bounty's addressing).
int res_villain_frame_paths(const VillainDef *v, char (**out)[RES_PATH_LEN]) {
    int n = v->anim_count > 0 ? v->anim_count : OB_ANIM_FRAMES_DEFAULT;
    *out = calloc((size_t)n, sizeof **out);
    if (!*out) return 0;
    if (v->anim_count > 0) {
        for (int f = 0; f < n; f++) res_copy_str((*out)[f], RES_PATH_LEN, v->anim[f]);
        return n;
    }
    char stem[RES_PATH_LEN];
    res_copy_str(stem, sizeof stem, v->portrait);
    size_t sl = strlen(stem);
    if (sl >= 7 && stem[sl - 7] == '_' && stem[sl - 4] == '.') stem[sl - 7] = '\0';
    else if (sl >= 4 && stem[sl - 4] == '.') stem[sl - 4] = '\0';
    for (int f = 0; f < n; f++)
        if (snprintf((*out)[f], RES_PATH_LEN, "%s_%02d.png", stem, f) >= RES_PATH_LEN)
            (*out)[f][0] = '\0';   // too long a path names no frame
    return n;
}

static int intro_villain_index(const Resources *res, const char *id) {
    for (int i = 0; id && i < res->villains_count; i++)
        if (strcmp(res->villains[i].id, id) == 0) return i;
    return -1;
}

// Copy a portrait's loop into a heap list; false when the id is unknown.
static bool intro_portrait_frames(const Resources *res, const char *id,
                                  char (**out)[RES_PATH_LEN], int *count) {
    int pi = resources_portrait_index(res, id);
    if (pi < 0 || res->portraits[pi].anim_count <= 0) return false;
    const ResPortrait *p = &res->portraits[pi];
    *out = calloc((size_t)p->anim_count, sizeof **out);
    if (!*out) return false;
    for (int f = 0; f < p->anim_count; f++) res_copy_str((*out)[f], RES_PATH_LEN, p->anim[f]);
    *count = p->anim_count;
    return true;
}

// The %TOKEN%s a beat may use: %DAYS% (normal difficulty's day budget)
// everywhere, and the villain's own on a for_each villain beat.
typedef struct {
    char reward[16], days[16];
    ResTemplateVar vars[6];
    int n;
} IntroVars;

static void intro_vars_for(const Resources *res, int vi, IntroVars *iv) {
    iv->n = 0;
    snprintf(iv->days, sizeof iv->days, "%d", res->time.days_per_difficulty[1]);
    iv->vars[iv->n++] = (ResTemplateVar){ "DAYS", iv->days };
    if (vi < 0) return;
    const VillainDef *v = &res->villains[vi];
    const ResVillainDesc *d = resources_villain_desc(res, v->id);
    const ResZone *z = NULL;
    for (int i = 0; i < res->zone_count; i++)
        if (strcmp(res->zones[i].id, v->zone) == 0) z = &res->zones[i];
    snprintf(iv->reward, sizeof iv->reward, "%d", v->reward);
    iv->vars[iv->n++] = (ResTemplateVar){ "NAME", v->name };
    iv->vars[iv->n++] = (ResTemplateVar){ "ALIAS", d ? d->alias : "" };
    iv->vars[iv->n++] = (ResTemplateVar){ "REWARD", iv->reward };
    iv->vars[iv->n++] = (ResTemplateVar){ "ZONE", z ? z->name : "" };
    iv->vars[iv->n++] = (ResTemplateVar){ "ZONE_SCENE", z ? z->treasure_scene : "" };
}

// A caption/card key resolved against strings "intro", filled, heap-copied.
static char *intro_text(Resources *res, const cJSON *jstr, const char *key,
                        const IntroVars *iv, int beat_no) {
    const char *src = cJSON_IsObject(jstr) ? res_json_str(jstr, key, NULL) : NULL;
    if (!src) {
        fprintf(stdout, "resources: pack missing string key 'intro.%s' (intro beat %d)\n",
                key, beat_no);
        res->strings_missing++;
        return NULL;
    }
    char buf[INTRO_TEXT_MAX];
    resources_format_template(buf, sizeof buf, src, iv->vars, iv->n);
    size_t len = strlen(buf) + 1;
    char *out = malloc(len);
    if (out) memcpy(out, buf, len);
    return out;
}

static void intro_error(Resources *res, int beat_no, const char *what) {
    fprintf(stdout, "resources: intro beat %d: %s\n", beat_no, what);
    res->intro_errors++;
}

// Fill one beat from its JSON, as villain `vi` (-1 unless for_each).
static void intro_fill_beat(Resources *res, ResIntroBeat *b, const cJSON *jb,
                            const cJSON *jstr, int vi, int beat_no,
                            double type_cps, double read_cps, double min_hold) {
    IntroVars iv;
    intro_vars_for(res, vi, &iv);

    char path[RES_PATH_LEN];
    resources_format_template(path, sizeof path, res_json_str(jb, "backdrop", ""), iv.vars, iv.n);
    res_copy_str(b->backdrop, sizeof b->backdrop, path);
    cJSON *pan = cJSON_GetObjectItem(jb, "pan");
    if (cJSON_GetArraySize(pan) == 2) {
        b->pan_from = json_pt(cJSON_GetArrayItem(pan, 0), b->pan_from);
        b->pan_to   = json_pt(cJSON_GetArrayItem(pan, 1), b->pan_from);
    }
    b->smooth   = strcmp(res_json_str(jb, "ease", "linear"), "smooth") == 0;
    b->dissolve = json_num(jb, "dissolve", 0);

    cJSON *ja = cJSON_GetObjectItem(jb, "actors");
    int na = cJSON_IsArray(ja) ? cJSON_GetArraySize(ja) : 0;
    if (na > 0 && (b->actors = calloc((size_t)na, sizeof *b->actors)) != NULL) {
        cJSON *a;
        cJSON_ArrayForEach(a, ja) {
            ResIntroActor *act = &b->actors[b->actor_count++];
            cJSON *jf = cJSON_GetObjectItem(a, "frames");
            const char *pid = res_json_str(a, "portrait", NULL);
            const char *vid = res_json_str(a, "villain", NULL);
            int sources = (jf != NULL) + (pid != NULL) + (vid != NULL);
            if (sources != 1) { intro_error(res, beat_no, "an actor needs exactly one of frames, portrait, villain"); continue; }
            if (jf) {
                res_parse_path_list(jf, &act->frames, &act->frame_count);
                if (act->frame_count == 0) intro_error(res, beat_no, "an actor's frames list is empty");
            } else if (pid) {
                if (!intro_portrait_frames(res, pid, &act->frames, &act->frame_count))
                    intro_error(res, beat_no, "an actor names an unknown portrait");
            } else {
                int v = strcmp(vid, "*") == 0 ? vi : intro_villain_index(res, vid);
                if (v < 0) intro_error(res, beat_no, "an actor names an unknown villain");
                else act->frame_count = res_villain_frame_paths(&res->villains[v], &act->frames);
            }
            act->fps    = json_num(a, "fps", 6.67);
            act->at     = json_pt(cJSON_GetObjectItem(a, "at"), (ResIntroPt){ 0, 0 });
            act->to     = json_pt(cJSON_GetObjectItem(a, "to"), act->at);
            act->mirror = cJSON_IsTrue(cJSON_GetObjectItem(a, "mirror"));
            act->loop   = !cJSON_IsFalse(cJSON_GetObjectItem(a, "loop"));
            act->start  = json_num(a, "start", 0);
            act->end    = json_num(a, "end", -1);   // -1: to the beat's end (set below)
            act->fade_in  = json_num(a, "fade_in", 0);
            act->fade_out = json_num(a, "fade_out", 0);
            cJSON *jc = cJSON_GetObjectItem(a, "crop");
            if (cJSON_GetArraySize(jc) == 4) {
                act->crop_x = cJSON_GetArrayItem(jc, 0)->valueint;
                act->crop_y = cJSON_GetArrayItem(jc, 1)->valueint;
                act->crop_w = cJSON_GetArrayItem(jc, 2)->valueint;
                act->crop_h = cJSON_GetArrayItem(jc, 3)->valueint;
                if (act->crop_w <= 0 || act->crop_h <= 0 || act->crop_x < 0 || act->crop_y < 0)
                    intro_error(res, beat_no, "an actor's crop must be [x, y, w, h], w and h positive");
            }
        }
    }

    const char *say  = res_json_str(jb, "say", NULL);
    const char *card = res_json_str(jb, "card", NULL);
    if (say)  b->caption = intro_text(res, jstr, say, &iv, beat_no);
    if (card) b->card    = intro_text(res, jstr, card, &iv, beat_no);
    const char *face = res_json_str(jb, "face", NULL);
    if (face && !intro_portrait_frames(res, face, &b->face, &b->face_count))
        intro_error(res, beat_no, "the face names an unknown portrait");

    const char *weather = res_json_str(jb, "weather", NULL);
    if (weather && strcmp(weather, "rain") == 0) b->rain = true;
    else if (weather) intro_error(res, beat_no, "weather must be \"rain\"");
    cJSON *jfl = cJSON_GetObjectItem(jb, "flashes");
    int nfl = cJSON_IsArray(jfl) ? cJSON_GetArraySize(jfl) : 0;
    if (nfl > 0 && (b->flashes = calloc((size_t)nfl, sizeof *b->flashes)) != NULL) {
        cJSON *f;
        cJSON_ArrayForEach(f, jfl) if (cJSON_IsNumber(f)) b->flashes[b->flash_count++] = f->valuedouble;
    }
    cJSON *jsn = cJSON_GetObjectItem(jb, "sounds");
    if (jsn && !cJSON_IsArray(jsn)) intro_error(res, beat_no, "sounds must be a list");
    int nsn = cJSON_IsArray(jsn) ? cJSON_GetArraySize(jsn) : 0;
    if (nsn > 0 && (b->sounds = calloc((size_t)nsn, sizeof *b->sounds)) != NULL) {
        cJSON *s;
        cJSON_ArrayForEach(s, jsn) {
            ResIntroSound *snd = &b->sounds[b->sound_count++];
            res_copy_str(snd->path, sizeof snd->path, res_json_str(s, "file", ""));
            snd->at   = json_num(s, "at", 0);
            snd->gain = json_num(s, "gain", 1);
            if (!snd->path[0]) intro_error(res, beat_no, "a sound needs a file");
            if (snd->gain < 0 || snd->gain > 1) intro_error(res, beat_no, "a sound's gain must be 0..1");
        }
    }

    cJSON *jd = cJSON_GetObjectItem(jb, "duration");
    if (cJSON_IsNumber(jd)) {
        b->dur = jd->valuedouble;
    } else if (say) {
        // Typed on, then held long enough to read (and never less than min_hold).
        double len = b->caption ? (double)strlen(b->caption) : 0;
        double hold = len / read_cps;
        b->dur = len / type_cps + (hold > min_hold ? hold : min_hold);
    } else {
        intro_error(res, beat_no, "a beat needs a duration or a caption");
    }
    if (b->dur <= 0 && cJSON_IsNumber(jd)) intro_error(res, beat_no, "a beat's duration must be positive");

    // An actor's time on screen lies within the beat.
    for (int i = 0; i < b->actor_count; i++) {
        ResIntroActor *act = &b->actors[i];
        if (act->end < 0) act->end = b->dur;
        if (act->start < 0 || act->end <= act->start || act->start >= b->dur)
            intro_error(res, beat_no, "an actor's start and end must lie within the beat, start before end");
    }
    for (int i = 0; i < b->sound_count; i++)
        if (b->sounds[i].at < 0 || b->sounds[i].at >= b->dur)
            intro_error(res, beat_no, "a sound must start within the beat");
}

// A for_each beat's villains: catalog positions [from, from + count), clamped.
static void intro_villain_range(const Resources *res, const cJSON *jb, int *v0, int *v1) {
    int from  = res_json_int(jb, "from", 0);
    int count = res_json_int(jb, "count", res->villains_count);
    if (from < 0) from = 0;
    if (from > res->villains_count) from = res->villains_count;
    if (count < 0) count = 0;
    *v0 = from;
    *v1 = from + count < res->villains_count ? from + count : res->villains_count;
}

void res_parse_intro(Resources *res, const cJSON *jpath, const cJSON *strings_root) {
    if (!cJSON_IsString(jpath) || !jpath->valuestring[0]) return;   // no intro
    char *txt = res_slurp(jpath->valuestring);
    cJSON *root = txt ? cJSON_Parse(txt) : NULL;
    free(txt);
    if (!root) {
        fprintf(stdout, "resources: intro '%s': cannot read or parse\n", jpath->valuestring);
        res->intro_errors++;
        return;
    }
    if (!res->ui.title_intro[0]) {
        fprintf(stdout, "resources: pack missing string key 'ui.title_intro' (it has an intro)\n");
        res->strings_missing++;
    }
    const cJSON *jstr = cJSON_GetObjectItem(strings_root, "intro");
    ResIntro *in = &res->intro;
    ResIntroPt frame = json_pt(cJSON_GetObjectItem(root, "frame"), (ResIntroPt){ 240, 102 });
    in->frame_w = frame.x;
    in->frame_h = frame.y;
    if (frame.x <= 0 || frame.y <= 0 || frame.x > INTRO_FRAME_MAX || frame.y > INTRO_FRAME_MAX)
        intro_error(res, 0, "frame must be 1..256 a side");
    in->type_cps = json_num(root, "type_cps", 28);
    double read_cps = json_num(root, "read_cps", 14);
    double min_hold = json_num(root, "min_hold", 2.0);
    if (in->type_cps <= 0 || read_cps <= 0) intro_error(res, 0, "type_cps and read_cps must be positive");
    if (in->type_cps <= 0) in->type_cps = 28;
    if (read_cps <= 0) read_cps = 14;

    // Pass 1: count. A for_each beat is one beat per villain from "from" on.
    cJSON *jscenes = cJSON_GetObjectItem(root, "scenes");
    int nscenes = cJSON_IsArray(jscenes) ? cJSON_GetArraySize(jscenes) : 0;
    int nbeats = 0;
    cJSON *js, *jb;
    cJSON_ArrayForEach(js, jscenes) {
        cJSON_ArrayForEach(jb, cJSON_GetObjectItem(js, "beats")) {
            const char *fe = res_json_str(jb, "for_each", NULL);
            if (!fe) { nbeats++; continue; }
            int v0, v1;
            intro_villain_range(res, jb, &v0, &v1);
            if (strcmp(fe, "villain") != 0) intro_error(res, nbeats + 1, "for_each must be \"villain\"");
            else nbeats += v1 - v0;
        }
    }
    if (nscenes == 0 || nbeats == 0) {
        intro_error(res, 0, "the intro has no scenes or no beats");
        cJSON_Delete(root);
        return;
    }
    if (!RES_TABLE_ALLOC(in->scenes, in->scene_count, nscenes) ||
        !RES_TABLE_ALLOC(in->beats, in->beat_count, nbeats)) {
        cJSON_Delete(root);
        return;
    }

    // Pass 2: fill, laying every beat end to end on one timeline.
    double t = 0;
    cJSON_ArrayForEach(js, jscenes) {
        ResIntroScene *sc = &in->scenes[in->scene_count];
        res_copy_str(sc->id, sizeof sc->id, res_json_str(js, "id", ""));
        sc->fade_in    = json_num(js, "fade_in", 1.0);
        sc->fade_out   = json_num(js, "fade_out", 1.0);
        sc->start      = t;
        sc->first_beat = in->beat_count;
        cJSON_ArrayForEach(jb, cJSON_GetObjectItem(js, "beats")) {
            const char *fe = res_json_str(jb, "for_each", NULL);
            int v0 = -1, v1 = 0;          // one pass, as no villain
            if (fe) {
                if (strcmp(fe, "villain") != 0) continue;
                intro_villain_range(res, jb, &v0, &v1);
            }
            for (int vi = v0; (fe ? vi < v1 : vi == v0) && in->beat_count < nbeats; vi++) {
                ResIntroBeat *b = &in->beats[in->beat_count++];
                b->scene = in->scene_count;
                intro_fill_beat(res, b, jb, jstr, fe ? vi : -1, in->beat_count,
                                in->type_cps, read_cps, min_hold);
                b->start = t;
                t += b->dur > 0 ? b->dur : 0;
            }
        }
        sc->beat_count = in->beat_count - sc->first_beat;
        sc->dur = t - sc->start;
        if (sc->beat_count == 0) intro_error(res, 0, "a scene has no beats");
        if (sc->fade_in + sc->fade_out > sc->dur) intro_error(res, 0, "a scene's fades are longer than the scene");
        in->scene_count++;
    }
    in->total = t;
    cJSON_Delete(root);
}

void res_intro_free(ResIntro *in) {
    for (int i = 0; in->beats && i < in->beat_count; i++) {
        ResIntroBeat *b = &in->beats[i];
        for (int a = 0; b->actors && a < b->actor_count; a++) free(b->actors[a].frames);
        free(b->actors);
        free(b->face);
        free(b->caption);
        free(b->card);
        free(b->flashes);
        free(b->sounds);
    }
    free(in->beats);
    free(in->scenes);
    memset(in, 0, sizeof *in);
}

bool resources_has_intro(const Resources *r) {
    return r && r->intro.beat_count > 0 && r->intro.total > 0;
}

const ResIntroBeat *resources_intro_beat_at(const ResIntro *in, double t) {
    if (!in || t < 0 || t >= in->total) return NULL;
    for (int i = 0; i < in->beat_count; i++) {
        const ResIntroBeat *b = &in->beats[i];
        if (t < b->start + b->dur) return b;
    }
    return NULL;
}
