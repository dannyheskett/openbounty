// src/shell_tempdeath.c

#include "shell_tempdeath.h"

#include "tables.h"
#include "ui.h"

void perform_temp_death(Game *g, Map *map, Fog *fog, const Resources *res) {
    // The transition itself is engine-owned (one implementation for the
    // shell and autoplay); the shell adds only the banner.
    GameTempDeath(g, map, fog, res);

    char body[RES_BANNER_LEN];
    resources_format_template(body, sizeof body, res->banners.temp_death,
                              NULL, 0);
    player_io_message(g, NULL, body);
}
