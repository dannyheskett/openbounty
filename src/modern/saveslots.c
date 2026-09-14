// src/modern/saveslots.c -- see saveslots.h.

#include "modern/saveslots.h"
#include "resources.h"
#include <stdio.h>
#include <string.h>

void saveslots_scan(SlotSet *s) {
    memset(s, 0, sizeof(*s));
    const Resources *r = resources_current();
    const char *pid = (r && r->pack_id[0]) ? r->pack_id : NULL;
    for (int i = 0; i < SAVE_SLOT_COUNT; i++) {
        char path[512];
        if (!SavePathGetSlot(pid, i, path, sizeof(path))) continue;
        if (SaveGameReadHeader(path, &s->hdrs[i]) == SAVE_OK && s->hdrs[i].exists)
            s->existing++;
    }
}

bool saveslots_row(void *ctx, int i, char *label, char *right, int cap) {
    const SlotSet *slots = (const SlotSet *)ctx;
    const Resources *r = resources_current();
    right[0] = '\0';
    if (slots->hdrs[i].exists) {
        snprintf(label, (size_t)cap, "%2d  %-10.10s %-13.13s", i + 1,
                 slots->hdrs[i].name, slots->hdrs[i].rank_title);
        snprintf(right, 48, "%dd", slots->hdrs[i].days_left);
    } else {
        snprintf(label, (size_t)cap, "%2d  %s", i + 1,
                 r ? r->ui.startup_save_picker_empty : "(empty)");
    }
    return true;
}
