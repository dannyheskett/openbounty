#ifndef OB_PALETTE_H
#define OB_PALETTE_H

#include "raylib.h"

// 256-color VGA palette loaded from the asset pack at startup. Initialized
// by palette_init() before any rendering. Index 0..15 are the canonical
// first-16 entries used by chrome/text rendering through the PAL_*
// constants below; 16..255 are the remaining VGA colors used by sprites,
// tiles, and backdrops.

#define PAL_SIZE 256

// Named indices for the first 16 palette entries. Used by chrome/text
// code through PAL_CLR(). Additional colors should reference the raw
// palette by index.
typedef enum {
    PAL_IDX_BLACK      = 0,
    PAL_IDX_DBLUE      = 1,
    PAL_IDX_DGREEN     = 2,
    PAL_IDX_DCYAN      = 3,
    PAL_IDX_DRED       = 4,
    PAL_IDX_MAGENTA    = 5,
    PAL_IDX_BROWN      = 6,
    PAL_IDX_GREY       = 7,
    PAL_IDX_DGREY      = 8,
    PAL_IDX_BLUE       = 9,
    PAL_IDX_GREEN      = 10,
    PAL_IDX_CYAN       = 11,
    PAL_IDX_RED        = 12,
    PAL_IDX_VIOLET     = 13,
    PAL_IDX_YELLOW     = 14,
    PAL_IDX_WHITE      = 15,
} PalIndex;

extern Color PAL[PAL_SIZE];

// Load the 256-color palette from the given path (768 raw bytes, 8-bit
// RGB triples). Returns true on success; on failure the canonical
// first-16 fallback is installed and rendering still works with the
// chrome-only color set.
bool palette_init(const char *path);

// Named accessors for render code. PAL_CLR(YELLOW), PAL_CLR(DRED), etc.
#define PAL_CLR(name) (PAL[PAL_IDX_##name])

#endif
