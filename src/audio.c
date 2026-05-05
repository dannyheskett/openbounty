// OpenBounty audio module: PC-speaker tunes + OGG music.
//
// Tunes are pre-rendered WAV files extracted from KB.EXE by tools/extract.
// raylib's LoadSound/PlaySound handles the playback. The pack ships four
// tunes: walk, bump, chest, defeat. Their paths come from game.json's
// audio.tunes block.
//
// Music is two pre-loaded raylib Music streams (openworld + combat).
// audio_set_track stops the current and plays the new — hard cut.
//
// Mixing model:
//   - SFX is stereo (each mono synth sample is duplicated to L+R) so
//     it sits in the same spatial field as the music.
//   - Master volume slider (controls option index 7, range 0..9)
//     scales both streams.
//   - Ducking: while a tune plays, music smooths down to DUCK_FACTOR
//     of its base volume; recovers smoothly when the tune ends.
//
// Sounds and Music can be muted independently via the controls.options
// toggles defined in game.json.

#include "audio.h"
#include "assets.h"
#include "raylib.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Volume tuning constants. Both kept under 1.0 so master=9 + tune-on-music
// stays within int16 headroom.
#define MUSIC_HEADROOM   0.50f
#define SFX_HEADROOM     0.80f
#define DUCK_FACTOR      0.20f
#define DUCK_ATTACK      0.40f   // per-frame smoothing toward duck (snap down)
#define DUCK_RELEASE     0.10f   // per-frame smoothing toward 1.0 (gentle up)

// Module state.
static bool        s_inited        = false;
static bool        s_sfx_enabled   = true;
static bool        s_music_enabled = true;
static float       s_master        = 0.5f;   // 0..1, mapped from 0..9 slider
static float       s_duck_level    = 1.0f;

// Tune sounds, indexed by AudioTuneId.
static Sound       s_tunes[AUDIO_TUNE_COUNT];
static bool        s_have_tune[AUDIO_TUNE_COUNT];

static Music       s_music_openworld;
static Music       s_music_combat;
static bool        s_have_openworld = false;
static bool        s_have_combat    = false;
// Backing bytes for each Music stream. raylib's stb_vorbis context
// keeps a pointer into these for the life of the stream. The bytes are
// owned by the active pack (src/pack.c) and stay valid until pack
// close, so we don't free them here.
static const unsigned char *s_openworld_bytes = NULL;
static const unsigned char *s_combat_bytes    = NULL;
static AudioTrack  s_active_track   = AUDIO_TRACK_NONE;

// ---------- helpers ---------------------------------------------------------

static Music *track_music(AudioTrack t) {
    if (t == AUDIO_TRACK_OPENWORLD && s_have_openworld) return &s_music_openworld;
    if (t == AUDIO_TRACK_COMBAT    && s_have_combat)    return &s_music_combat;
    return NULL;
}

static float effective_music_volume(void) {
    if (!s_music_enabled) return 0.0f;
    return s_master * MUSIC_HEADROOM * s_duck_level;
}

static void apply_music_volume(void) {
    Music *m = track_music(s_active_track);
    if (m) SetMusicVolume(*m, effective_music_volume());
}

// ---------- public API ------------------------------------------------------

void audio_init(const Resources *res) {
    if (s_inited) return;

    // ALSA dumps a wall of "function snd_func_concat returned error"
    // noise to stderr when no device is available (common in WSL,
    // headless CI). Redirect stderr to /dev/null around InitAudioDevice
    // and IsAudioDeviceReady so a missing device degrades silently.
    fflush(stderr);
    int saved_stderr = dup(STDERR_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }

    InitAudioDevice();
    bool ready = IsAudioDeviceReady();

    if (saved_stderr >= 0) {
        fflush(stderr);
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stderr);
    }

    if (!ready) {
        fprintf(stderr, "[audio] no audio device available; controls disabled\n");
        return;
    }

    // Tune sounds: load each WAV out of the active pack. Missing
    // entries leave their slot empty; audio_play_tune skips silently.
    if (res) {
        const char *paths[AUDIO_TUNE_COUNT] = {
            res->audio.tune_walk,
            res->audio.tune_bump,
            res->audio.tune_chest,
            res->audio.tune_defeat,
        };
        for (int i = 0; i < AUDIO_TUNE_COUNT; i++) {
            if (!paths[i] || !paths[i][0]) continue;
            size_t sz = 0;
            const unsigned char *bytes = LoadAssetBytes(paths[i], &sz);
            if (!bytes || sz == 0) continue;
            Wave w = LoadWaveFromMemory(".wav", bytes, (int)sz);
            if (!w.data) continue;
            s_tunes[i] = LoadSoundFromWave(w);
            UnloadWave(w);
            s_have_tune[i] = (s_tunes[i].stream.buffer != NULL);
        }
    }

    // Music streams. raylib's LoadMusicStreamFromMemory keeps a pointer
    // into the bytes for the lifetime of the stream — those bytes are
    // owned by the active pack and stay valid until pack close.
    if (res) {
        size_t sz = 0;
        if (res->audio.openworld_path[0]) {
            s_openworld_bytes = LoadAssetBytes(res->audio.openworld_path, &sz);
            if (s_openworld_bytes && sz > 0) {
                s_music_openworld = LoadMusicStreamFromMemory(".ogg",
                                        s_openworld_bytes, (int)sz);
                if (s_music_openworld.stream.buffer) {
                    s_music_openworld.looping = true;
                    s_have_openworld = true;
                }
            }
        }
        if (res->audio.combat_path[0]) {
            s_combat_bytes = LoadAssetBytes(res->audio.combat_path, &sz);
            if (s_combat_bytes && sz > 0) {
                s_music_combat = LoadMusicStreamFromMemory(".ogg",
                                     s_combat_bytes, (int)sz);
                if (s_music_combat.stream.buffer) {
                    s_music_combat.looping = true;
                    s_have_combat = true;
                }
            }
        }
    }

    s_inited = true;
}

void audio_shutdown(void) {
    if (!s_inited) return;

    if (s_have_openworld) UnloadMusicStream(s_music_openworld);
    if (s_have_combat)    UnloadMusicStream(s_music_combat);
    s_openworld_bytes = NULL;
    s_combat_bytes    = NULL;
    s_have_openworld = s_have_combat = false;
    s_active_track = AUDIO_TRACK_NONE;

    for (int i = 0; i < AUDIO_TUNE_COUNT; i++) {
        if (s_have_tune[i]) UnloadSound(s_tunes[i]);
        s_have_tune[i] = false;
    }

    CloseAudioDevice();
    s_inited = false;
}

void audio_tick(void) {
    if (!s_inited) return;

    // Ducking: target = DUCK_FACTOR while any tune is playing, else 1.0.
    bool any_tune = false;
    for (int i = 0; i < AUDIO_TUNE_COUNT && !any_tune; i++) {
        if (s_have_tune[i] && IsSoundPlaying(s_tunes[i])) any_tune = true;
    }
    float target = any_tune ? DUCK_FACTOR : 1.0f;
    float rate = (target < s_duck_level) ? DUCK_ATTACK : DUCK_RELEASE;
    s_duck_level += (target - s_duck_level) * rate;

    apply_music_volume();

    Music *m = track_music(s_active_track);
    if (m) UpdateMusicStream(*m);
}

bool audio_is_available(void) {
    return s_inited;
}

void audio_set_sounds_enabled(bool on) {
    s_sfx_enabled = on;
}

void audio_set_music_enabled(bool on) {
    if (s_music_enabled == on) return;
    s_music_enabled = on;
    Music *m = track_music(s_active_track);
    if (!m) return;
    if (on) {
        // Music stream may have never been started (init had toggle off,
        // or audio_set_track was called while disabled). PlayMusicStream
        // is idempotent — safe to call even if it's already going, and
        // it correctly starts a never-played stream.
        if (!IsMusicStreamPlaying(*m)) PlayMusicStream(*m);
    } else {
        StopMusicStream(*m);
    }
}

void audio_set_master_volume(int v) {
    if (v < 0) v = 0;
    if (v > 9) v = 9;
    s_master = (float)v / 9.0f;
}

void audio_play_tune(AudioTuneId t) {
    if (!s_inited || !s_sfx_enabled) return;
    if (t < 0 || t >= AUDIO_TUNE_COUNT) return;
    if (!s_have_tune[t]) return;
    SetSoundVolume(s_tunes[t], s_master * SFX_HEADROOM);
    PlaySound(s_tunes[t]);
}

void audio_set_track(AudioTrack t) {
    if (!s_inited) return;
    if (t == s_active_track) return;

    // Stop the old.
    Music *old = track_music(s_active_track);
    if (old && IsMusicStreamPlaying(*old)) StopMusicStream(*old);

    s_active_track = t;

    // Start the new (only if music toggle is on).
    if (!s_music_enabled) return;
    Music *m = track_music(t);
    if (m) PlayMusicStream(*m);
}
