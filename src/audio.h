#ifndef OB_AUDIO_H
#define OB_AUDIO_H

#include <stdbool.h>

#include "resources.h"
#include "ui_host.h"   // AudioTuneId + audio_play_tune (engine-shared)

// Shell-side audio API. The engine-facing parts (AudioTuneId enum +
// audio_play_tune) live in engine/include/ui_host.h. This header adds
// the shell-only lifecycle and settings calls used by main.c.

typedef enum {
    AUDIO_TRACK_NONE,
    AUDIO_TRACK_OPENWORLD,
    AUDIO_TRACK_COMBAT,
    AUDIO_TRACK_INTRO,       // the Introduction's theme (audio_intro_begin)
} AudioTrack;

// Lifecycle. Opening the playback device can block for a long time -- tens
// of seconds under WSL, where the sound server may be slow to answer -- so
// audio_init does NOT wait for it: it starts the open on a background thread
// and returns at once. audio_tick, on the main thread, finishes the job when
// the device answers (loads the tunes and music streams and starts any track
// already asked for). Only InitAudioDevice / IsAudioDeviceReady run off the
// main thread; every other raylib audio call stays on it.
//
// audio_init_blocking opens the device and loads everything before returning,
// for --autoplay and --demo: their game state is byte-exact, and a
// no-device fallback landing mid-run would change it.
//
// audio_shutdown closes everything; safe to call while the open is still
// pending or after it failed.
void audio_init(const Resources *res);
void audio_init_blocking(const Resources *res);
void audio_shutdown(void);

typedef enum {
    AUDIO_PENDING,       // the device is still being opened
    AUDIO_READY,         // playing
    AUDIO_UNAVAILABLE,   // no device: the audio controls are disabled
} AudioStatus;
AudioStatus audio_status(void);

// True iff a playback device is open and loaded.
bool audio_is_available(void);

// Per-frame tick. Drives raylib's UpdateMusicStream for whichever
// track is active. Cheap when no track is playing.
void audio_tick(void);

// Settings. Toggling sounds_enabled OFF gates future tunes; a tune
// already playing is left to finish.
// Toggling music_enabled OFF stops the active track; toggling back
// ON resumes (does not restart from the top).
void audio_set_sounds_enabled(bool on);
void audio_set_music_enabled(bool on);

// Master volume slider, 0..9 (UI scale matching the controls panel).
// 0 = mute, 9 = full. Applied multiplicatively to both music and SFX.
void audio_set_master_volume(int v);

// Switch the background music track. Hard cut. Pass AUDIO_TRACK_NONE
// to silence music without disabling the toggle.
void audio_set_track(AudioTrack t);

// The Introduction's theme (game.json audio.tracks.intro). Begin opens the
// device if it is not yet open (the startup screens run before main's
// audio_init), loads the track and plays it from the top, whatever the Music
// option; it stays silent if the pack has none or the device answers more
// than 1.5 s late. Gain scales it 0..1 for the closing fade. End frees it and
// restores the track that played before. The caller pumps audio_tick.
void audio_intro_begin(const Resources *res);
void audio_intro_gain(float g);
void audio_intro_end(void);
// One of the intro's sound effects (an intro.json beat's "sounds"): a .wav in
// the pack, played at gain 0..1 (times the master volume and the closing
// fade), whatever the Sounds option. Freed, and so stopped, by audio_intro_end.
void audio_intro_sound(const char *path, float gain);

#endif
