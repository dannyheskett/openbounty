#ifndef OB_SAVEPATH_H
#define OB_SAVEPATH_H

#include <stdbool.h>
#include <stddef.h>

// Fill `out` with the full save-file path for slot N (0..9):
//   "<user_data>/openbounty/saves/<pack_id>/save_<N>.dat"
// (or `<override>/save_<N>.dat` when --save-dir is set; override is flat).
// Ensures the directory exists. Returns false on failure.
bool SavePathGetSlot(const char *pack_id, int slot, char *out, size_t out_size);

// Fill `out` with the user-data root: the platform-specific
// "<user_data>/openbounty" directory. This is where loose pack files
// live and where the saves/ subtree is rooted.
bool SavePathGetDir(char *out, size_t out_size);

// Override the save directory. Pass NULL to clear. Used by --save-dir
// CLI flag. When set, saves are written flat as <override>/save_<N>.dat
// with no pack-id segregation.
void SavePathSetDirOverride(const char *dir);

// Fill `out` with the directory containing the running executable
// (no trailing separator). Returns false if the platform call fails.
bool SavePathGetExeDir(char *out, size_t out_size);

// Push completed save writes to persistent storage. On desktop this is a
// no-op -- fwrite already reached the filesystem. On the web build the save
// tree lives in an IDBFS mount held in memory, so it must be flushed to
// IndexedDB or it is lost on page reload. Call after each SaveGameWrite.
void SavePathFlush(void);

// Number of save slots exposed to the UI.
#define SAVE_SLOT_COUNT 10

#endif
