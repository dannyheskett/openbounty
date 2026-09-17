// src/shell_tempdeath.c

#include "shell_tempdeath.h"

#include "tables.h"
#include "ui.h"
#include "layout.h"
#include "player_io.h"

void perform_temp_death(Game *g, Map *map, Fog *fog, const Resources *res) {
    // The transition itself is engine-owned (one implementation for the
    // shell and autoplay); the shell adds only the banner.
    GameTempDeath(g, map, fog, res);

    char body[RES_BANNER_LEN];
    resources_format_template(body, sizeof body, res->banners.temp_death,
                              NULL, 0);
    // Modern: the hero's disgraced scene when the pack has one, else the
    // Emperor who summoned you, in the in-lay. Legacy: plain words.
    const ClassDef *cls = class_by_id(g->character.cls.id);
    if (CL_IS_MODERN && cls && cls->index >= 0 && cls->index < res->classes_count &&
        res->class_hero[cls->index].disgraced[0]) {
        player_io_note_scene(g, NULL, body, cls->index);
        return;
    }
    int portrait = -1;
    for (int i = 0; CL_IS_MODERN && i < res->castle_count; i++) {
        if (!resources_castle_is_home(&res->castles[i])) continue;
        portrait = resources_portrait_index(res, res->castles[i].special.portrait);
        break;
    }
    if (portrait >= 0) player_io_note_face(g, NULL, body, REQ_FACE_PORTRAIT, portrait);
    else               player_io_note(g, NULL, body);
}
