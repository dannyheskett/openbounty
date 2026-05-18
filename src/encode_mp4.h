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

#endif
