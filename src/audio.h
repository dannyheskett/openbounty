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

// Lifecycle. audio_init opens the raylib audio device, loads the
// music streams (if paths in `res` resolve), and primes the
// PC-speaker tune synthesizer. audio_shutdown closes everything;
// safe to call even if init partially failed.
void audio_init(const Resources *res);
void audio_shutdown(void);

// True iff audio_init successfully opened a playback device. UI code
// uses this to gray out the music/sounds/volume controls when no
// device is available.
bool audio_is_available(void);

// Per-frame tick. Drives raylib's UpdateMusicStream for whichever
// track is active. Cheap when no track is playing.
void audio_tick(void);

// Settings. Toggling sounds_enabled OFF silences in-flight tunes.
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
