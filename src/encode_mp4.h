#ifndef OB_ENCODE_MP4_H
#define OB_ENCODE_MP4_H

#include <stdbool.h>
#include <stddef.h>

// Read <src_dir>/manifest.ndjson + tick_*.png, encode H.264 baseline
// (visually lossless), mux into MP4 at out_path. Synchronous. Calls
// `cb` after each frame is encoded so the caller can render a progress
// dialog.

typedef struct {
    int         current;       // 0..total-1
    int         total;
    double      elapsed_s;
    const char *status;        // "Reading manifest", "Encoding", "Muxing", "Done"
} EncodeProgress;

typedef void (*encode_progress_fn)(const EncodeProgress *p, void *user);

bool mp4_encode_dir(const char *src_dir, const char *out_path,
                    encode_progress_fn cb, void *user,
                    char *err_buf, size_t err_cap);

// A sound track to go with the frames: signed 16-bit samples, interleaved
// when there is more than one channel, starting with the first frame.
typedef struct {
    const short *pcm;
    size_t       frames;     // samples per channel
    int          rate;       // 8000..48000
    int          channels;   // 1 or 2
} EncodeAudio;

// As mp4_encode_dir, with `audio` (NULL: silent) encoded as AAC-LC
// (vo-aacenc) on a second track.
bool mp4_encode_dir_av(const char *src_dir, const char *out_path,
                       const EncodeAudio *audio,
                       encode_progress_fn cb, void *user,
                       char *err_buf, size_t err_cap);

#endif
