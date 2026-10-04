// The Introduction player (#154). See intro.h; the script's format is
// PACK-FORMAT section 2.4 and its rules OPENBOUNTY-SPEC REQ-430u.

#include "intro.h"
#include "assets.h"
#include "audio.h"
#include "bfont.h"
#include "frame_host.h"
#include "input_host.h"
#include "layout.h"
#include "present.h"
#include "screenshot.h"
#include "touch.h"
#include "ui.h"
#include "modern/page.h"
#include "modern/uikit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INTRO_FACE_PX   96    // a portrait loop's frame
#define INTRO_LINES     5     // the caption band's lines
#define INTRO_LINE_CAP  200

bool intro_available(const Resources *res) {
    return CL_IS_MODERN && resources_has_intro(res);
}

// ---- layout ------------------------------------------------------------------

void intro_layout(int sw, int sh, int fw, int fh, int line_h, IntroLayout *o) {
    memset(o, 0, sizeof *o);
    if (fw <= 0 || fh <= 0) return;
    int band_min = INTRO_LINES * line_h + 2 * UK_INSET;
    int k = 1;
    while (fw * (k + 1) <= sw && fh * (k + 1) + band_min <= sh) k++;
    o->scale = k;
    o->pic_w = fw * k;
    o->pic_h = fh * k;
    // The face at the largest whole multiple the room below the picture
    // allows (at most the picture's own), the band tall enough for it.
    int room = sh - o->pic_h;
    int fs = 0;
    while ((fs + 1) * INTRO_FACE_PX + 2 * UK_INSET <= room && fs + 1 <= k) fs++;
    int band = fs > 0 ? fs * INTRO_FACE_PX + 2 * UK_INSET : band_min;
    if (band < band_min) band = band_min;
    if (band > room) band = room > 0 ? room : 0;
    o->band_h = band;
    o->pic_x = (sw - o->pic_w) / 2;
    o->pic_y = (sh - (o->pic_h + band)) / 2;
    if (o->pic_y < 0) o->pic_y = 0;
    o->band_y = o->pic_y + o->pic_h;
    o->face_scale = fs;
    o->face_x = o->pic_x + UK_INSET;
    o->face_y = o->band_y + UK_INSET;
    o->text_x = fs > 0 ? o->face_x + fs * INTRO_FACE_PX + UK_INSET : o->pic_x + UK_INSET;
    o->text_y = o->band_y + UK_INSET;
    o->text_w = o->pic_x + o->pic_w - UK_INSET - o->text_x;
    if (o->text_w < 0) o->text_w = 0;
}

// ---- textures: loaded on entry, freed on exit ------------------------------

typedef struct { char path[RES_PATH_LEN]; Texture2D tex; } IntroTex;
static IntroTex *s_tex = NULL;
static int s_tex_n = 0, s_tex_cap = 0;

static Texture2D intro_tex(const char *path) {
    if (!path || !path[0]) return (Texture2D){ 0 };
    for (int i = 0; i < s_tex_n; i++)
        if (strcmp(s_tex[i].path, path) == 0) return s_tex[i].tex;
    if (s_tex_n >= s_tex_cap) {
        int cap = s_tex_cap ? s_tex_cap * 2 : 128;
        IntroTex *grown = realloc(s_tex, (size_t)cap * sizeof *grown);
        if (!grown) return (Texture2D){ 0 };
        s_tex = grown;
        s_tex_cap = cap;
    }
    IntroTex *e = &s_tex[s_tex_n++];
    snprintf(e->path, sizeof e->path, "%s", path);
    e->tex = LoadAssetTexture(path);
    if (e->tex.id) gfx_texture_point(e->tex);
    return e->tex;
}

static void intro_load_all(const Resources *res) {
    if (s_tex_n > 0) return;
    for (int i = 0; i < res->intro.beat_count; i++) {
        const ResIntroBeat *b = &res->intro.beats[i];
        intro_tex(b->backdrop);
        intro_tex(b->still);
        for (int a = 0; a < b->actor_count; a++)
            for (int f = 0; f < b->actors[a].frame_count; f++) intro_tex(b->actors[a].frames[f]);
        for (int f = 0; f < b->face_count; f++) intro_tex(b->face[f]);
    }
}

void intro_release(void) {
    for (int i = 0; i < s_tex_n; i++)
        if (s_tex[i].tex.id) gfx_texture_free(s_tex[i].tex);
    free(s_tex);
    s_tex = NULL;
    s_tex_n = s_tex_cap = 0;
}

// ---- drawing -----------------------------------------------------------------

static double clamp01(double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static double smooth(double k) { return k * k * (3 - 2 * k); }

// Where `from` -> `to` stands at k (0..1), in whole art pixels.
static ResIntroPt lerp_pt(ResIntroPt from, ResIntroPt to, double k) {
    return (ResIntroPt){ (int)lround(from.x + (to.x - from.x) * k),
                         (int)lround(from.y + (to.y - from.y) * k) };
}

static void blit_tinted(Texture2D t, int x, int y, int scale, bool mirror, unsigned char a) {
    if (!t.id) return;
    Rectangle src = { 0, 0, mirror ? -(float)t.width : (float)t.width, (float)t.height };
    Rectangle dst = { (float)x, (float)y, (float)(t.width * scale), (float)(t.height * scale) };
    gfx_texture_draw(t, src, dst, (Color){ 255, 255, 255, a });
}

// Rain: streaks falling across the picture, slanting a little, each its own
// fixed column, speed and phase, so the drops are a pure function of time.
#define INTRO_RAIN_DROPS 70
#define INTRO_FLASH_S    0.25   // a lightning flash's length
static void draw_rain(const ResIntro *in, double tb, const IntroLayout *l, unsigned char a) {
    for (int i = 0; i < INTRO_RAIN_DROPS; i++) {
        unsigned h = (unsigned)i * 2654435761u;
        double speed = 90 + (h % 60);                       // art pixels a second
        double fall = in->frame_h + 8;
        double y = fmod(((h >> 8) % 1000) / 1000.0 * fall + tb * speed, fall) - 4;
        int x = (int)((h >> 16) % (unsigned)in->frame_w) + (int)(y / 6);
        x %= in->frame_w;
        gfx_rect(l->pic_x + x * l->scale, l->pic_y + (int)y * l->scale,
                 l->scale > 1 ? l->scale / 2 + 1 : 1, 3 * l->scale,
                 (Color){ 190, 200, 225, (unsigned char)(a * 0.55) });
    }
}

// One beat's picture at time t: the backdrop through the moving frame
// window, then its actors, each on screen from its start to its end, then
// the weather, all at alpha a.
static void draw_picture(const ResIntro *in, const ResIntroBeat *b, double t,
                         const IntroLayout *l, unsigned char a) {
    double k = b->dur > 0 ? clamp01((t - b->start) / b->dur) : 1;
    if (b->smooth) k = smooth(k);
    ResIntroPt pan = lerp_pt(b->pan_from, b->pan_to, k);
    Texture2D bg = intro_tex(b->backdrop);
    if (bg.id) {
        int sw = in->frame_w < bg.width ? in->frame_w : bg.width;
        int sh = in->frame_h < bg.height ? in->frame_h : bg.height;
        int ox = (in->frame_w - sw) / 2 * l->scale, oy = (in->frame_h - sh) / 2 * l->scale;
        gfx_texture_draw(bg, (Rectangle){ (float)pan.x, (float)pan.y, (float)sw, (float)sh },
                         (Rectangle){ (float)(l->pic_x + ox), (float)(l->pic_y + oy),
                                      (float)(sw * l->scale), (float)(sh * l->scale) },
                         (Color){ 255, 255, 255, a });
    }
    double tb = t - b->start;   // seconds into the beat
    for (int i = 0; i < b->actor_count; i++) {
        const ResIntroActor *act = &b->actors[i];
        if (act->frame_count <= 0 || tb < act->start || tb >= act->end) continue;
        // The move runs across the actor's own time on screen.
        double ka = clamp01((tb - act->start) / (act->end - act->start));
        if (b->smooth) ka = smooth(ka);
        int f = (int)floor((tb - act->start) * act->fps);
        if (!act->loop) f = f < act->frame_count ? f : act->frame_count - 1;   // once, then hold
        else f = ((f % act->frame_count) + act->frame_count) % act->frame_count;
        ResIntroPt p = lerp_pt(act->at, act->to, ka);
        // Faded up after its start and away before its end, when asked.
        double fa = 1;
        if (act->fade_in > 0)  fa = fmin(fa, clamp01((tb - act->start) / act->fade_in));
        if (act->fade_out > 0) fa = fmin(fa, clamp01((act->end - tb) / act->fade_out));
        Texture2D t = intro_tex(act->frames[f]);
        int dx = l->pic_x + (p.x - pan.x) * l->scale, dy = l->pic_y + (p.y - pan.y) * l->scale;
        if (act->crop_w > 0 && t.id) {
            // Only part of the frame (a portrait's head and shoulders on a notice).
            float cw = (float)act->crop_w;
            gfx_texture_draw(t, (Rectangle){ (float)act->crop_x, (float)act->crop_y,
                                             act->mirror ? -cw : cw, (float)act->crop_h },
                             (Rectangle){ (float)dx, (float)dy, (float)(act->crop_w * l->scale),
                                          (float)(act->crop_h * l->scale) },
                             (Color){ 255, 255, 255, (unsigned char)(a * fa) });
        } else {
            blit_tinted(t, dx, dy, l->scale, act->mirror, (unsigned char)(a * fa));
        }
    }
    if (b->rain) draw_rain(in, tb, l, a);
    for (int i = 0; i < b->flash_count; i++) {
        double since = tb - b->flashes[i];
        if (since >= 0 && since < INTRO_FLASH_S)   // white, dying away
            gfx_rect(l->pic_x, l->pic_y, l->pic_w, l->pic_h,
                     (Color){ 255, 255, 255, (unsigned char)(a * 0.8 * (1 - since / INTRO_FLASH_S)) });
    }
}

// Words on the film are white, as the credits are: the band is black.
#define INTRO_INK WHITE

// A card: text set in the middle of the picture, shadowed to read on any art.
static void draw_card(const char *text, const IntroLayout *l) {
    int lines = 1;
    for (const char *c = text; *c; c++) lines += *c == '\n';
    int y = l->pic_y + (l->pic_h - lines * uk_line_h()) / 2;
    int cx = l->pic_x + l->pic_w / 2;
    uk_words_centred(text, cx + 1, y + 1, l->pic_w - 2 * UK_INSET, lines + 2, BLACK);
    uk_words_centred(text, cx, y, l->pic_w - 2 * UK_INSET, lines + 2, INTRO_INK);
}

// The caption typed on: wrapped once over the whole line, so words never
// jump from one line to the next as it types. With a speaker it sits beside
// the face; a narrator's line (no face) is centred under the picture, each
// line placed by its full width so it types on from a fixed left edge.
static bool draw_caption(const char *text, const IntroLayout *l, int typed, bool centred) {
    const char *p = text;
    char line[INTRO_LINE_CAP];
    int y = l->text_y;
    int w = centred ? l->pic_w - 2 * UK_INSET : l->text_w;
    int fit = (l->band_h - 2 * UK_INSET) / uk_line_h();   // as many lines as the band holds
    if (fit < INTRO_LINES) fit = INTRO_LINES;
    for (int n = 0; *p && n < fit; n++) {
        if (bfont_take_line(&p, w, line, (int)sizeof line) <= 0) break;
        int len = (int)strlen(line);
        if (typed <= 0) return true;
        int x = centred ? l->pic_x + (l->pic_w - bfont_text_width(line)) / 2 : l->text_x;
        if (typed < len) line[typed] = '\0';
        typed -= len;
        bfont_draw(line, x, y, INTRO_INK);
        y += uk_line_h();
    }
    return typed < 0;
}

static void intro_draw(const Resources *res, double t) {
    const ResIntro *in = &res->intro;
    IntroLayout l;
    intro_layout(CL_SCREEN_W, CL_SCREEN_H, in->frame_w, in->frame_h, uk_line_h(), &l);
    gfx_clear(BLACK);
    page_bare();
    const ResIntroBeat *b = resources_intro_beat_at(in, t);
    if (!b) return;
    int z = present_get_zoom();
    if (z < 1) z = 1;

    if (b->still[0]) {
        // A picture shown whole (the title), at the largest whole multiple.
        Texture2D st = intro_tex(b->still);
        if (st.id) {
            int k = ui_fit_scale(st.width, st.height, CL_SCREEN_W, CL_SCREEN_H);
            double a = b->dissolve > 0 ? clamp01((t - b->start) / b->dissolve) : 1;
            blit_tinted(st, (CL_SCREEN_W - st.width * k) / 2, (CL_SCREEN_H - st.height * k) / 2,
                        k, false, (unsigned char)(255 * a));
        }
    } else {

    gfx_clip_begin(l.pic_x * z, l.pic_y * z, l.pic_w * z, l.pic_h * z);
    if (b->dissolve > 0 && t - b->start < b->dissolve && b > in->beats && !b[-1].still[0]) {
        const ResIntroBeat *prev = b - 1;
        draw_picture(in, prev, prev->start + prev->dur - 1e-6, &l, 255);   // its last frame
        draw_picture(in, b, t, &l, (unsigned char)(255 * clamp01((t - b->start) / b->dissolve)));
    } else {
        draw_picture(in, b, t, &l, 255);
    }
    gfx_clip_end();
    if (b->card) draw_card(b->card, &l);
    }

    if (b->caption) {
        int typed = (int)((t - b->start) * in->type_cps);
        bool speaker = l.face_scale > 0 && b->face_count > 0;
        bool typing = draw_caption(b->caption, &l, typed, !speaker);
        if (speaker) {
            // The speaker talks while the line types, then rests.
            int f = typing ? (int)((t - b->start) * UK_FACE_FPS) % b->face_count : 0;
            blit_tinted(intro_tex(b->face[f]), l.face_x, l.face_y, l.face_scale, false, 255);
        }
    }

    // Each scene fades up from black and down to it.
    const ResIntroScene *sc = &in->scenes[b->scene];
    double fade = 1;
    if (sc->fade_in > 0) fade = fmin(fade, clamp01((t - sc->start) / sc->fade_in));
    if (sc->fade_out > 0) fade = fmin(fade, clamp01((sc->start + sc->dur - t) / sc->fade_out));
    if (fade < 1)
        gfx_rect(0, 0, CL_SCREEN_W, CL_SCREEN_H,
                 (Color){ 0, 0, 0, (unsigned char)(255 * (1 - smooth(fade))) });
}

void intro_gallery_draw(RenderTexture2D *rt, const Resources *res, double t) {
    if (!rt || !res) return;
    intro_load_all(res);
    present_refit(rt);
    present_begin(rt);
    intro_draw(res, t);
    present_end();
}

bool run_intro(RenderTexture2D *rt, const Resources *res) {
    if (!rt || !intro_available(res)) return true;
    input_host_flush(0.25);   // the key or tap that chose the row
    // One black frame first, so the menu goes at once; the loading hides in
    // the first scene's fade from black.
    present_refit(rt);
    present_begin(rt);
    gfx_clear(BLACK);
    present_end();
    present_scaled(*rt);
    frame_host_end_frame();

    intro_load_all(res);
    audio_intro_begin(res);
    const ResIntro *in = &res->intro;
    const ResIntroScene *last = &in->scenes[in->scene_count - 1];
    double t0 = frame_host_time();
    double heard = -1;   // sounds up to here have been started
    bool closed = false;
    while (!(closed = frame_host_should_close())) {
        double t = frame_host_time() - t0;
        if (t >= in->total || ui_any_key_pressed()) break;
        // Each sound starts as the timeline passes it.
        for (int i = 0; i < in->beat_count; i++) {
            const ResIntroBeat *b = &in->beats[i];
            for (int s = 0; s < b->sound_count; s++) {
                double at = b->start + b->sounds[s].at;
                if (at > heard && at <= t) audio_intro_sound(b->sounds[s].path, (float)b->sounds[s].gain);
            }
        }
        heard = t;
        // The theme fades with the last scene.
        double out = last->fade_out > 0 ? clamp01((in->total - t) / last->fade_out) : 1;
        audio_intro_gain((float)out);
        audio_tick();
        present_refit(rt);
        present_begin(rt);
        intro_draw(res, t);
        present_end();
        present_scaled(*rt);
        frame_host_end_frame();
        screenshot_tick(*rt, "intro");
    }
    audio_intro_end();
    intro_release();
    // Nothing pressed here carries into the menu.
    frame_host_poll_events();
    input_host_clear_injected();
    touch_forget_tap();
    return !closed;
}
