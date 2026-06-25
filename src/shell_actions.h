// src/shell_actions.h
//
// Adventure-mode input action dispatcher. Maps an InputState's action
// field to the corresponding game-state mutation or view push.

#ifndef OB_SHELL_ACTIONS_H
#define OB_SHELL_ACTIONS_H

#include "input.h"
#include "shell_ctx.h"

void shell_dispatch_action(ShellCtx *ctx, const InputState *in);

#endif
