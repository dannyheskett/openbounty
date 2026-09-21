// src/audio_backend.h for iOS: AVAudioEngine for playback, stb_vorbis for the
// two streamed .ogg tracks. src/audio.c keeps every decision -- which tune,
// the master volume, the ducking curve, the mute toggles -- so this is only
// "hold a decoded sound, hold a stream, play them".
//
// The one-shots are four short PCM .wav files from the pack, decoded once into
// AVAudioPCMBuffers. The music is two vorbis streams; stb_vorbis decodes the
// whole track into memory at load (each is a couple of minutes of mono/stereo
// PCM, a few MB) rather than feeding buffers incrementally, because a looping
// AVAudioPlayerNode can then schedule the same buffer with .Loops and needs no
// top-up at all -- which is why audio_backend_stream_update does nothing here.

#import <AVFoundation/AVFoundation.h>

extern "C" {
#include "audio_backend.h"
}

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY

#include <stdlib.h>
#include <string.h>

#define SOUND_MAX  16
#define STREAM_MAX  8

static AVAudioEngine        *s_engine;
static AVAudioMixerNode     *s_mixer;
static bool                  s_ready;

typedef struct {
    bool used;
    AVAudioPlayerNode *node;
    AVAudioPCMBuffer  *buf;
    bool loop;
} Voice;

static Voice s_sounds[SOUND_MAX];
static Voice s_streams[STREAM_MAX];

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

void audio_backend_open(void) {
    @autoreleasepool {
        NSError *err = nil;
        // Ambient: the game is not the phone's primary purpose, so it mixes
        // with whatever the player already has playing and goes silent on the
        // ring switch, which is what a casual game should do.
        [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryAmbient
                                               error:&err];
        [[AVAudioSession sharedInstance] setActive:YES error:&err];

        s_engine = [[AVAudioEngine alloc] init];
        s_mixer = s_engine.mainMixerNode;
        if (![s_engine startAndReturnError:&err]) {
            NSLog(@"openbounty: audio engine failed: %@", err);
            return;
        }
        s_ready = true;
    }
}

void audio_backend_close(void) {
    @autoreleasepool {
        [s_engine stop];
        s_engine = nil;
        s_mixer = nil;
        s_ready = false;
    }
}

bool audio_backend_ready(void) { return s_ready; }

// ---------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------

// A minimal RIFF/WAVE reader for the pack's tunes: 16-bit PCM, mono, as
// tools/extract writes them. Anything else is refused rather than guessed at.
static AVAudioPCMBuffer *decode_wav(const unsigned char *b, int size) {
    if (size < 44 || memcmp(b, "RIFF", 4) != 0 || memcmp(b + 8, "WAVE", 4) != 0)
        return nil;
    int channels = 1, rate = 22050, bits = 16;
    const unsigned char *p = b + 12;
    const unsigned char *end = b + size;
    const unsigned char *data = NULL;
    int data_len = 0;
    while (p + 8 <= end) {
        unsigned len = (unsigned)p[4] | ((unsigned)p[5] << 8) |
                       ((unsigned)p[6] << 16) | ((unsigned)p[7] << 24);
        if (memcmp(p, "fmt ", 4) == 0 && p + 8 + 16 <= end) {
            channels = p[10] | (p[11] << 8);
            rate     = p[12] | (p[13] << 8) | (p[14] << 16) | (p[15] << 24);
            bits     = p[30] | (p[31] << 8);
        } else if (memcmp(p, "data", 4) == 0) {
            data = p + 8;
            data_len = (int)len;
            if (data + data_len > end) data_len = (int)(end - data);
            break;
        }
        p += 8 + len + (len & 1);
    }
    if (!data || data_len <= 0 || bits != 16 || channels < 1) return nil;

    AVAudioFormat *fmt =
        [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32
                                         sampleRate:(double)rate
                                           channels:(AVAudioChannelCount)channels
                                        interleaved:NO];
    AVAudioFrameCount frames = (AVAudioFrameCount)(data_len / (2 * channels));
    AVAudioPCMBuffer *buf =
        [[AVAudioPCMBuffer alloc] initWithPCMFormat:fmt frameCapacity:frames];
    if (!buf) return nil;
    buf.frameLength = frames;
    const short *pcm = (const short *)(const void *)data;
    for (int c = 0; c < channels; c++) {
        float *dst = buf.floatChannelData[c];
        for (AVAudioFrameCount i = 0; i < frames; i++)
            dst[i] = pcm[i * channels + c] / 32768.0f;
    }
    return buf;
}

static AVAudioPCMBuffer *decode_ogg(const unsigned char *b, int size) {
    int channels = 0, rate = 0;
    short *pcm = NULL;
    int frames = stb_vorbis_decode_memory(b, size, &channels, &rate, &pcm);
    if (frames <= 0 || !pcm) return nil;

    AVAudioFormat *fmt =
        [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32
                                         sampleRate:(double)rate
                                           channels:(AVAudioChannelCount)channels
                                        interleaved:NO];
    AVAudioPCMBuffer *buf =
        [[AVAudioPCMBuffer alloc] initWithPCMFormat:fmt
                                      frameCapacity:(AVAudioFrameCount)frames];
    if (!buf) { free(pcm); return nil; }
    buf.frameLength = (AVAudioFrameCount)frames;
    for (int c = 0; c < channels; c++) {
        float *dst = buf.floatChannelData[c];
        for (int i = 0; i < frames; i++)
            dst[i] = pcm[i * channels + c] / 32768.0f;
    }
    free(pcm);
    return buf;
}

// ---------------------------------------------------------------------------
// Voices
// ---------------------------------------------------------------------------

static int voice_load(Voice *table, int max, AVAudioPCMBuffer *buf, bool loop) {
    if (!s_ready || !buf) return AUDIO_NONE;
    int slot = -1;
    for (int i = 0; i < max; i++) if (!table[i].used) { slot = i; break; }
    if (slot < 0) return AUDIO_NONE;

    AVAudioPlayerNode *node = [[AVAudioPlayerNode alloc] init];
    [s_engine attachNode:node];
    [s_engine connect:node to:s_mixer format:buf.format];
    table[slot].used = true;
    table[slot].node = node;
    table[slot].buf  = buf;
    table[slot].loop = loop;
    return slot;
}

static Voice *voice_at(Voice *table, int max, int h) {
    if (h < 0 || h >= max || !table[h].used) return NULL;
    return &table[h];
}

static void voice_play(Voice *v) {
    if (!v) return;
    [v->node stop];
    AVAudioPlayerNodeBufferOptions opts =
        v->loop ? AVAudioPlayerNodeBufferLoops : 0;
    [v->node scheduleBuffer:v->buf atTime:nil options:opts completionHandler:nil];
    [v->node play];
}

// ---------------------------------------------------------------------------
// src/audio_backend.h
// ---------------------------------------------------------------------------

AudioSoundId audio_backend_sound_load(const char *ext,
                                      const unsigned char *bytes, int size) {
    (void)ext;
    @autoreleasepool { return voice_load(s_sounds, SOUND_MAX,
                                         decode_wav(bytes, size), false); }
}

void audio_backend_sound_free(AudioSoundId s) {
    Voice *v = voice_at(s_sounds, SOUND_MAX, s);
    if (!v) return;
    [v->node stop];
    [s_engine detachNode:v->node];
    v->used = false; v->node = nil; v->buf = nil;
}

void audio_backend_sound_play(AudioSoundId s) {
    voice_play(voice_at(s_sounds, SOUND_MAX, s));
}

bool audio_backend_sound_playing(AudioSoundId s) {
    Voice *v = voice_at(s_sounds, SOUND_MAX, s);
    return v && v->node.isPlaying;
}

void audio_backend_sound_volume(AudioSoundId s, float vol) {
    Voice *v = voice_at(s_sounds, SOUND_MAX, s);
    if (v) v->node.volume = vol;
}

AudioStreamId audio_backend_stream_load(const char *ext,
                                        const unsigned char *bytes, int size,
                                        bool loop) {
    (void)ext;
    @autoreleasepool { return voice_load(s_streams, STREAM_MAX,
                                         decode_ogg(bytes, size), loop); }
}

void audio_backend_stream_free(AudioStreamId m) {
    Voice *v = voice_at(s_streams, STREAM_MAX, m);
    if (!v) return;
    [v->node stop];
    [s_engine detachNode:v->node];
    v->used = false; v->node = nil; v->buf = nil;
}

void audio_backend_stream_play(AudioStreamId m) {
    voice_play(voice_at(s_streams, STREAM_MAX, m));
}

void audio_backend_stream_stop(AudioStreamId m) {
    Voice *v = voice_at(s_streams, STREAM_MAX, m);
    if (v) [v->node stop];
}

bool audio_backend_stream_playing(AudioStreamId m) {
    Voice *v = voice_at(s_streams, STREAM_MAX, m);
    return v && v->node.isPlaying;
}

void audio_backend_stream_volume(AudioStreamId m, float vol) {
    Voice *v = voice_at(s_streams, STREAM_MAX, m);
    if (v) v->node.volume = vol;
}

// Nothing to top up: the whole track is one looping buffer (see the header
// comment). Kept because src/audio.c calls it once a frame on every platform.
void audio_backend_stream_update(AudioStreamId m) { (void)m; }
