// src/shell_promptdispatch.h
//
// Bottom-frame prompt-result dispatcher. When a yes/no, A/B, numeric,
// or text-input prompt resolves, this routes the result through the
// pending_flow continuation (search, dismiss-army, siege, attack-foe,
// chest, alcove, recruit, accept-friendly, navigate).

#ifndef OB_SHELL_PROMPTDISPATCH_H
#define OB_SHELL_PROMPTDISPATCH_H

#include <stdbool.h>

#include "shell_ctx.h"

// Per-frame pump. Returns true if a prompt was active (so the caller
// should skip the rest of its input handling). The internal chained-
// prompt case is handled inside.
bool prompt_dispatch_tick(ShellCtx *ctx);

#endif
