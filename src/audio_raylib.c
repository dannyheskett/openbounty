// raylib backend for src/audio_backend.h: the device, four decoded tunes and
// two streamed tracks, exactly as src/audio.c used to call raylib for them.
// iOS compiles ios/audio_ios.mm instead and never builds this file.
//
// Handles are indices into two small fixed tables rather than raylib's Sound /
// Music structs, so audio.c holds no platform type. The tables are sized by
// what a pack can declare: four tunes and two tracks today, with room to spare
// -- a handle past the end is refused rather than growing, because nothing in
// the pack format can ask for more.

#include "audio_backend.h"

#if !defined(PLATFORM_IOS)

#include "raylib.h"

#define SOUND_MAX  16
#define STREAM_MAX  8

static Sound s_sounds[SOUND_MAX];
static bool  s_sound_used[SOUND_MAX];
static Music s_streams[STREAM_MAX];
static bool  s_stream_used[STREAM_MAX];

void audio_backend_open(void)  { InitAudioDevice(); }
void audio_backend_close(void) { CloseAudioDevice(); }
bool audio_backend_ready(void) { return IsAudioDeviceReady(); }

AudioSoundId audio_backend_sound_load(const char *ext,
                                    const unsigned char *bytes, int size) {
    int slot = -1;
    for (int i = 0; i < SOUND_MAX; i++) if (!s_sound_used[i]) { slot = i; break; }
    if (slot < 0) return AUDIO_NONE;
    // The Wave is decoded into the Sound and is not needed afterwards.
    Wave w = LoadWaveFromMemory(ext, bytes, size);
    if (!w.data) return AUDIO_NONE;
    s_sounds[slot] = LoadSoundFromWave(w);
    UnloadWave(w);
    if (!s_sounds[slot].stream.buffer) return AUDIO_NONE;    // decoder refused
    s_sound_used[slot] = true;
    return slot;
}

static bool sound_ok(AudioSoundId s) {
    return s >= 0 && s < SOUND_MAX && s_sound_used[s];
}

void audio_backend_sound_free(AudioSoundId s) {
    if (!sound_ok(s)) return;
    UnloadSound(s_sounds[s]);
    s_sound_used[s] = false;
}

void audio_backend_sound_play(AudioSoundId s) {
    if (sound_ok(s)) PlaySound(s_sounds[s]);
}

bool audio_backend_sound_playing(AudioSoundId s) {
    return sound_ok(s) && IsSoundPlaying(s_sounds[s]);
}

void audio_backend_sound_volume(AudioSoundId s, float v) {
    if (sound_ok(s)) SetSoundVolume(s_sounds[s], v);
}

AudioStreamId audio_backend_stream_load(const char *ext,
                                      const unsigned char *bytes, int size,
                                      bool loop) {
    int slot = -1;
    for (int i = 0; i < STREAM_MAX; i++) if (!s_stream_used[i]) { slot = i; break; }
    if (slot < 0) return AUDIO_NONE;
    // raylib's stb_vorbis context keeps a pointer INTO these bytes: the caller
    // owns them and must outlive the stream.
    s_streams[slot] = LoadMusicStreamFromMemory(ext, bytes, size);
    if (!s_streams[slot].stream.buffer) return AUDIO_NONE;   // decoder refused
    s_streams[slot].looping = loop;
    s_stream_used[slot] = true;
    return slot;
}

static bool stream_ok(AudioStreamId m) {
    return m >= 0 && m < STREAM_MAX && s_stream_used[m];
}

void audio_backend_stream_free(AudioStreamId m) {
    if (!stream_ok(m)) return;
    UnloadMusicStream(s_streams[m]);
    s_stream_used[m] = false;
}

void audio_backend_stream_play(AudioStreamId m) {
    if (stream_ok(m)) PlayMusicStream(s_streams[m]);
}

void audio_backend_stream_stop(AudioStreamId m) {
    if (stream_ok(m)) StopMusicStream(s_streams[m]);
}

bool audio_backend_stream_playing(AudioStreamId m) {
    return stream_ok(m) && IsMusicStreamPlaying(s_streams[m]);
}

void audio_backend_stream_volume(AudioStreamId m, float v) {
    if (stream_ok(m)) SetMusicVolume(s_streams[m], v);
}

void audio_backend_stream_update(AudioStreamId m) {
    if (stream_ok(m)) UpdateMusicStream(s_streams[m]);
}

#endif // !PLATFORM_IOS
