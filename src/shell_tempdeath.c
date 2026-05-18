// src/shell_tempdeath.c

#include "shell_tempdeath.h"

#include "tables.h"
#include "ui.h"

void perform_temp_death(Game *g, Map *map, Fog *fog, const Resources *res) {
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        g->army[i].id[0] = '\0';
        g->army[i].count = 0;
    }
    g->stats.siege_weapons = 0;

    const TroopDef *peas = troop_by_id("peasants");
    if (peas) {
        size_t k = 0;
        while (k + 1 < sizeof(g->army[0].id) && peas->id[k]) {
            g->army[0].id[k] = peas->id[k]; k++;
        }
        g->army[0].id[k] = '\0';
        g->army[0].count = 20;
    }

    for (int zi = 0; zi < res->zone_count; zi++) {
        if (!res->zones[zi].is_home) continue;
        GameSwitchZone(g, map, fog, res->zones[zi].id);
        g->position.x = res->zones[zi].home_spawn_x;
        g->position.y = res->zones[zi].home_spawn_y;
        g->position.last_x = g->position.x;
        g->position.last_y = g->position.y;
        break;
    }
    g->character.mount = MOUNT_RIDE;
    g->travel_mode = TRAVEL_WALK;

    char body[RES_BANNER_LEN];
    resources_format_template(body, sizeof body, res->banners.temp_death,
                              NULL, 0);
    open_dialog(NULL, body);
}
