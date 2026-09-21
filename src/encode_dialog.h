#ifndef OB_ENCODE_DIALOG_H
#define OB_ENCODE_DIALOG_H

#include "gfx.h"

#if !defined(PLATFORM_IOS)

// Render a centered "Encoding video..." progress dialog and synchronously
// run the encoder. Blocks until encoding completes. Returns true on
// success. After completion, holds a "Done" panel for ~2.5s (or until
// any key) before returning. `rt` is the same 320x200 RenderTexture2D
// used by the rest of the game.
bool encode_dialog_session(RenderTexture2D *rt, const char *src_dir, const char *out_path);

#else

// The video recorder is a desktop subsystem: src/encode_dialog.c and the
// minih264/minimp4 encoders are not in the iOS build. Nothing there can ask
// to encode, so the session simply reports that it did not run.
#include <stdbool.h>
static inline bool encode_dialog_session(RenderTexture2D *rt, const char *src_dir,
                                         const char *out_path) {
    (void)rt; (void)src_dir; (void)out_path;
    return false;
}

#endif

#endif
