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

#endif
