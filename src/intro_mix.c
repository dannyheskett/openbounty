// The Introduction's sound for --intro-movie (src/intro_mix.h).

#include "intro_mix.h"
#include "intro.h"
#include "audio.h"
#include "assets_bytes.h"
#include "raylib.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void intro_mix_render(const ResIntro *in, float master, int rate,
                      const IntroClip *theme,
                      const char *const *paths, const IntroClip *clips, int nclips,
                      float *out, size_t n) {
    if (!out) return;
    memset(out, 0, n * sizeof *out);
    if (!in || rate <= 0) return;
    float music = 0, sfx = 0;
    audio_intro_levels(&music, &sfx);

    if (theme && theme->s)
        for (size_t i = 0; i < n && i < theme->n; i++)
            out[i] += theme->s[i] * master * music * (float)intro_sound_gain(in, (double)i / rate);

    // Each sound plays from its cue to its end, or to its next cue.
    for (int b = 0; b < in->beat_count; b++) {
        const ResIntroBeat *beat = &in->beats[b];
        for (int s = 0; s < beat->sound_count; s++) {
            const ResIntroSound *snd = &beat->sounds[s];
            int k = 0;
            while (k < nclips && strcmp(paths[k], snd->path) != 0) k++;
            if (k == nclips || !clips[k].s) continue;
            double at = beat->start + snd->at;
            double next = 1e300;
            for (int b2 = 0; b2 < in->beat_count; b2++)
                for (int s2 = 0; s2 < in->beats[b2].sound_count; s2++) {
                    const ResIntroBeat *o = &in->beats[b2];
                    double at2 = o->start + o->sounds[s2].at;
                    if (at2 > at && at2 < next && strcmp(o->sounds[s2].path, snd->path) == 0) next = at2;
                }
            float g = master * sfx * (float)snd->gain * (float)intro_sound_gain(in, at);
            size_t from = (size_t)llround(at * rate);
            size_t to = next < 1e299 ? (size_t)llround(next * rate) : n;
            for (size_t j = 0; j < clips[k].n && from + j < to && from + j < n; j++)
                out[from + j] += clips[k].s[j] * g;
        }
    }
}

// A pack file decoded to mono floats at `rate`; an empty clip if it will not.
static IntroClip load_clip(const char *path, int rate) {
    IntroClip c = { 0 };
    size_t sz = 0;
    const unsigned char *bytes = LoadAssetBytes(path, &sz);
    const char *ext = strrchr(path, '.');
    if (!bytes || sz == 0 || !ext) return c;
    Wave w = LoadWaveFromMemory(ext, bytes, (int)sz);
    if (!w.data || w.frameCount == 0) { UnloadWave(w); return c; }
    WaveFormat(&w, rate, 32, 1);
    c.s = LoadWaveSamples(w);
    c.n = c.s ? w.frameCount : 0;
    UnloadWave(w);
    return c;
}

short *intro_mix_pcm(const Resources *res, int rate, size_t *frames) {
    if (frames) *frames = 0;
    if (!res || rate <= 0 || res->intro.scene_count <= 0) return NULL;
    const ResIntro *in = &res->intro;

    // Every distinct sound file, decoded once.
    int count = 0;
    for (int b = 0; b < in->beat_count; b++) count += in->beats[b].sound_count;
    const char **paths = calloc((size_t)count + 1, sizeof *paths);
    IntroClip *clips = calloc((size_t)count + 1, sizeof *clips);
    if (!paths || !clips) { free(paths); free(clips); return NULL; }
    int nclips = 0;
    for (int b = 0; b < in->beat_count; b++)
        for (int s = 0; s < in->beats[b].sound_count; s++) {
            const char *p = in->beats[b].sounds[s].path;
            int k = 0;
            while (k < nclips && strcmp(paths[k], p) != 0) k++;
            if (k < nclips) continue;
            paths[nclips] = p;
            clips[nclips++] = load_clip(p, rate);
        }
    IntroClip theme = res->audio.intro_path[0] ? load_clip(res->audio.intro_path, rate) : (IntroClip){ 0 };

    bool any = theme.s != NULL;
    for (int k = 0; k < nclips; k++) any = any || clips[k].s != NULL;
    size_t n = (size_t)ceil(in->total * rate);
    float *mix = any ? malloc(n * sizeof *mix) : NULL;
    short *pcm = mix ? malloc(n * sizeof *pcm) : NULL;
    if (pcm) {
        intro_mix_render(in, 1.0f, rate, &theme, paths, clips, nclips, mix, n);
        for (size_t i = 0; i < n; i++) {
            float v = mix[i] < -1 ? -1 : mix[i] > 1 ? 1 : mix[i];
            pcm[i] = (short)lrintf(v * 32767.0f);
        }
        if (frames) *frames = n;
    }
    free(mix);
    if (theme.s) UnloadWaveSamples((float *)theme.s);
    for (int k = 0; k < nclips; k++) if (clips[k].s) UnloadWaveSamples((float *)clips[k].s);
    free(paths);
    free(clips);
    return pcm;
}
