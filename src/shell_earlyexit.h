// src/shell_earlyexit.h
//
// CLI early-exit modes: --pack-dir (zip a loose asset tree) and
// --extract (build an asset pack from a user's KB.EXE distribution).
// Both run to completion and return; no window opens.

#ifndef OB_SHELL_EARLYEXIT_H
#define OB_SHELL_EARLYEXIT_H

// Run --pack-dir mode. Returns the process exit code.
int shell_run_pack_dir_mode(const char *src, const char *dst);

// Run --extract mode. If `out_dir` is non-NULL, emit a loose asset
// tree there; else zip into <user-data>/openbounty/<pack_id>.openbounty.
// Returns the process exit code.
int shell_run_extract_mode(const char *out_dir);

#endif
