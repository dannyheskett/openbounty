#ifndef OB_ENCODE_DIALOG_H
#define OB_ENCODE_DIALOG_H

#include "raylib.h"

// Render a centered "Encoding video..." progress dialog and synchronously
// run the encoder. Blocks until encoding completes. Returns true on
// success. After completion, holds a "Done" panel for ~2.5s (or until
// any key) before returning. `rt` is the same 320x200 RenderTexture2D
// used by the rest of the game.
bool encode_dialog_session(RenderTexture2D *rt, const char *src_dir, const char *out_path);

#endif
