#ifndef OB_PACK_H
#define OB_PACK_H

#include <stdbool.h>
#include <stddef.h>

// Game pack reader. A pack is either a `.openbounty` ZIP file or a loose
// directory tree (dev-mode iteration). Either way, opening a pack
// decompresses or slurps every file into RAM, and pack_read returns
// borrowed pointers that callers MUST NOT free. Pointers stay valid
// until pack_close.
//
// The engine maintains a global pack stack with last-wins lookup, so
// later-pushed packs override earlier entries.

typedef struct Pack Pack;

// Open a pack from `path`. Auto-detects: directory → loose-tree mode,
// regular file → ZIP mode. Returns NULL on failure (logs to stderr).
Pack *pack_open(const char *path);
void  pack_close(Pack *p);

// Borrow bytes for pack-relative entry `rel` (e.g. "art/font/kb-font.png").
// Returns NULL if not present in the pack. *out_size is set on success
// (and zeroed on failure when non-NULL). Pointer remains valid until
// pack_close. Callers MUST NOT free.
const unsigned char *pack_read(const Pack *p, const char *rel, size_t *out_size);

// Pack identity, read at open time from the embedded game.json.
// Returns "" if the pack lacks the field.
const char *pack_id(const Pack *p);
const char *pack_name(const Pack *p);
const char *pack_kind(const Pack *p);
const char *pack_path(const Pack *p);

// Whole-pack content hash (FNV1a-64 over the zip file bytes), as a
// 16-char lowercase hex string. Empty for loose-directory packs (dev
// mode). Advisory only -- saves embed it for diagnostic purposes;
// nothing currently gates on it.
const char *pack_hash(const Pack *p);

// ---- Global pack stack -----------------------------------------------------
// Lookup walks top-down: the topmost pack containing `rel` wins.

void pack_stack_push(Pack *p);
void pack_stack_pop(void);
void pack_stack_clear(void);
const unsigned char *pack_stack_read(const char *rel, size_t *out_size);
const Pack *pack_stack_top(void);

// ---- Discovery -------------------------------------------------------------

#define PACK_ENTRY_PATH_MAX 512
#define PACK_ENTRY_NAME_MAX 64
#define PACK_DISCOVER_MAX   16

typedef struct {
    char path[PACK_ENTRY_PATH_MAX];   // absolute path to the .openbounty
    char name[PACK_ENTRY_NAME_MAX];   // filename without extension
} PackEntry;

// Scan, in order, for available packs:
//   1. cwd/*.openbounty
//   2. <user-data>/openbounty/*.openbounty
//   3. <exe-dir>/assets/*.openbounty       (bundled with the binary)
// Earlier source wins on duplicate names. Fills `out` up to `cap` entries.
// Returns the count discovered.
int pack_discover(PackEntry *out, int cap);

// Resolve a `--pack <arg>` CLI value to an absolute path.
// - If `arg` contains '/' or '\' or ends in ".openbounty" → treat as
//   path; verify it exists.
// - Otherwise (bare name) → search the same roots as pack_discover for
//   "<arg>" or "<arg>.openbounty" (file or directory).
// Returns true and fills `out` on success; false otherwise.
bool pack_resolve_arg(const char *arg, char *out, size_t cap);

// Zip every file under `src_dir` into `out_zip` (a `.openbounty`
// archive). Skips editor scratch files. Returns true on success.
// Used by --extract to package the just-emitted loose tree, and by
// the standalone tools/mkpack.c command-line equivalent.
bool pack_zip_dir(const char *src_dir, const char *out_zip);

// Recursively delete a directory tree. Used to clean up the temp
// extraction dir after pack_zip_dir succeeds.
bool pack_rmtree(const char *path);

#endif
