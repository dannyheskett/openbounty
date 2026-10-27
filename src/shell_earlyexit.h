// src/shell_earlyexit.h
//
// CLI early-exit modes: --pack-dir (zip a loose asset tree) and
// --extract (build an asset pack from a user's KB.EXE distribution).
// Both run to completion and return; no window opens.

#ifndef OB_SHELL_EARLYEXIT_H
#define OB_SHELL_EARLYEXIT_H

#include <stdbool.h>
#include <stddef.h>

// Run --pack-dir mode. Returns the process exit code.
int shell_run_pack_dir_mode(const char *src, const char *dst);

// Run --extract mode. If `out_dir` is non-NULL, emit a loose asset
// tree there; else zip into <user-data>/openbounty/<pack_id>.openbounty.
// Returns the process exit code.
int shell_run_extract_mode(const char *out_dir);

// Where --extract reads KB.EXE and its files: legacy/bin/ when it holds
// KB.EXE, else the current directory; NULL when neither does.
const char *shell_extract_input_dir(void);

// Extract from `in_dir` and zip the pack into <user-data>/<pack_id>.openbounty,
// copying that path to `out_zip` (when given). False on any failure, reported.
bool shell_extract_to_user_dir(const char *in_dir, char *out_zip, size_t cap);

#endif
