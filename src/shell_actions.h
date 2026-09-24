// src/shell_actions.h
//
// Adventure-mode input action dispatcher. Maps an InputState's action
// field to the corresponding game-state mutation or view push.

#ifndef OB_SHELL_ACTIONS_H
#define OB_SHELL_ACTIONS_H

#include "input.h"
#include "shell_ctx.h"
#include "resources.h"

void shell_dispatch_action(ShellCtx *ctx, const InputState *in);

// Modern: the province picker's rows -- the provinces the hero can sail for
// (pending_nav_zones), each answering its number -- set on the prompt just
// opened. Legacy reads the numbered body instead.
void shell_navigate_choices(const Resources *res);

#endif
