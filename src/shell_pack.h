// src/shell_pack.h
//
// Which pack the game plays. On Android and iOS, the one bundled with the app.
// Elsewhere, `--pack <name|path>`; without it, discovery (pack_discover): one
// pack opens directly and several open the picker. With none at all, a
// first-run extraction from KB.EXE, or a dialog saying how to get a pack.

#ifndef OB_SHELL_PACK_H
#define OB_SHELL_PACK_H

#include <stddef.h>

#include "pack.h"

// Open the game's pack into `*out`, its path into `pack_path`. Returns -1 to
// carry on, or the process exit code: 0 when the player closed the picker,
// 1 after a failure the user has been told about.
int shell_open_game_pack(const char *pack_arg, Pack **out, char *pack_path, size_t cap);

#endif
