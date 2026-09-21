// src/audio_backend.h
//
// The platform half of the audio module. src/audio.c keeps all the policy --
// which tune plays, the master volume, the ducking curve, the mute toggles --
// and calls this for the four things a platform actually has to do: open the
// device, hold a decoded sound, hold a streamed track, and play them.
//
// Two backends:
//   src/audio_raylib.c   raylib's audio (desktop / web / Android)
//   ios/audio_ios.mm     AVAudioEngine + stb_vorbis (iOS, links no raylib)
//
// Both kinds of asset arrive as BYTES from the pack (src/assets.c reads
// everything into memory; nothing here ever opens a path): four short .wav
// tunes, decoded once and kept, and two .ogg tracks, streamed.
//
// The handles are deliberately opaque indices rather than a backend's own
// struct, so audio.c holds no platform type. 0 is a valid handle; the `ok`
// flags in audio.c say whether one was loaded, exactly as they did before.

#ifndef OB_AUDIO_BACKEND_H
#define OB_AUDIO_BACKEND_H

#include <stdbool.h>

// Ids, not pointers: audio.c holds no platform type. The names avoid raylib's
// own Sound / AudioStream, which this header must be able to sit beside.
typedef int AudioSoundId;    // a decoded one-shot
typedef int AudioStreamId;   // a streamed track
#define AUDIO_NONE (-1)

// ---- device ---------------------------------------------------------------

void audio_backend_open(void);
void audio_backend_close(void);
bool audio_backend_ready(void);

// ---- one-shots ------------------------------------------------------------

// `ext` is the extension including the dot (".wav"), as the decoder wants it.
AudioSoundId audio_backend_sound_load(const char *ext,
                                    const unsigned char *bytes, int size);
void       audio_backend_sound_free(AudioSoundId s);
void       audio_backend_sound_play(AudioSoundId s);
bool       audio_backend_sound_playing(AudioSoundId s);
void       audio_backend_sound_volume(AudioSoundId s, float v);

// ---- streams --------------------------------------------------------------
//
// The bytes must stay alive for the life of the stream: a decoder reads them
// as it goes. audio.c owns them and frees them after audio_backend_stream_free.
AudioStreamId audio_backend_stream_load(const char *ext,
                                      const unsigned char *bytes, int size,
                                      bool loop);
void        audio_backend_stream_free(AudioStreamId m);
void        audio_backend_stream_play(AudioStreamId m);
void        audio_backend_stream_stop(AudioStreamId m);
bool        audio_backend_stream_playing(AudioStreamId m);
void        audio_backend_stream_volume(AudioStreamId m, float v);
// Top up the stream's buffers. Called once a frame for the active track.
void        audio_backend_stream_update(AudioStreamId m);

#endif // OB_AUDIO_BACKEND_H
