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
    PlayerRequest *msg = player_io_message(g, NULL, body);
    // Modern: the Emperor who summoned you, in the in-lay.
    for (int i = 0; msg && CL_IS_MODERN && i < res->castle_count; i++) {
        if (!resources_castle_is_home(&res->castles[i])) continue;
        int idx = resources_portrait_index(res, res->castles[i].special.portrait);
        if (idx >= 0) { msg->face = REQ_FACE_PORTRAIT; msg->face_index = idx; }
        break;
    }
}
