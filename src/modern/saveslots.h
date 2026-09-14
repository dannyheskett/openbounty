// src/modern/saveslots.h -- the 10 save slots as standard rows, shared by the
// title's load picker and the in-game menu's Save and Load pages.

#ifndef OB_MODERN_SAVESLOTS_H
#define OB_MODERN_SAVESLOTS_H

#include "savegame.h"
#include "savepath.h"
#include <stdbool.h>

typedef struct {
    SaveHeader hdrs[SAVE_SLOT_COUNT];
    int        existing;
} SlotSet;

// Read every slot's header for the loaded pack.
void saveslots_scan(SlotSet *s);
// An MlRowFn (src/modern/mlist.h): "N  Name  Rank" with the days left at the
// right, or "N  (empty)". ctx is the SlotSet.
bool saveslots_row(void *ctx, int i, char *label, char *right, int cap);

#endif
