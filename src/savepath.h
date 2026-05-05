#ifndef SAVEPATH_H
#define SAVEPATH_H

#include <stdbool.h>
#include <stddef.h>

// Fill `out` with the full save-file path for slot N (0..9):
//   "<data_dir>/save_<N>.dat"
// Ensures the directory exists. Returns false on failure.
bool SavePathGetSlot(int slot, char *out, size_t out_size);

// Compatibility: legacy single-slot accessor. Equivalent to
// SavePathGetSlot(0, ...). Kept so existing save/load callers still work
// until they adopt the slot picker.
bool SavePathGet(char *out, size_t out_size);

// Fill `out` with the writable save directory (no filename).
bool SavePathGetDir(char *out, size_t out_size);

// Override the save directory. Pass NULL to clear. Used by --save-dir
// CLI flag. Path is *not* validated until first use; if it doesn't
// exist or can't be created, save/load operations will fail.
void SavePathSetDirOverride(const char *dir);

// Fill `out` with the writable packs directory: `<save_dir>/packs/`.
// Ensures the directory exists. Returns false on failure.
bool SavePathGetPacksDir(char *out, size_t out_size);

// Number of save slots exposed to the UI ().
#define SAVE_SLOT_COUNT 10

#endif
