// src/shell_cheats.c

#include "shell_cheats.h"

#include <stdio.h>

#include "tables.h"
#include "ui.h"
#include "end_cartoon.h"
#include "flows.h"

static bool s_cheat_menu_active = false;

bool cheat_menu_is_active(void) {
    return s_cheat_menu_active;
}

static const char *const CHEAT_MENU_BODY =
    "G  Gold        N  Zone\n"
    "V  Leadership  O  Fog\n"
    "M  Magic       W  Win\n"
    "U  Spells      L  Lose\n"
    "S  Siege\n"
    "F  Flight\n"
    "F10/ESC  close";

CheatResult cheat_menu_tick(Game *game, Map *map, Fog *fog,
                            const Resources *res,
                            const Sprites *sprites,
                            RenderTexture2D *render_target) {
    if (IsKeyPressed(KEY_F10) && !s_cheat_menu_active) {
        s_cheat_menu_active = true;
        player_io_message(game, "Debug menu", CHEAT_MENU_BODY);
        // Drain the keypress queue so the same-frame F10 doesn't
        // immediately close the menu we just opened.
        while (GetKeyPressed() != 0) { }
        return CHEAT_OPENED;
    }
    if (!s_cheat_menu_active) return CHEAT_IDLE;

    int ck = GetKeyPressed();
    if (ck == KEY_ESCAPE || ck == KEY_F10) {
        s_cheat_menu_active = false;
        dialog_dismiss();
        return CHEAT_DISMISSED;
    }
    if (ck < KEY_A || ck > KEY_Z) return CHEAT_IDLE;

    char letter = (char)('A' + (ck - KEY_A));
    char body[160];
    body[0] = '\0';
    switch (letter) {
    case 'G':
        game->stats.gold += 50000;
        snprintf(body, sizeof body, "Gold +50000");
        break;
    case 'V':
        game->stats.leadership_current += 100;
        game->stats.leadership_base += 100;
        snprintf(body, sizeof body, "Leadership +100");
        break;
    case 'M':
        game->stats.spell_power += 1;
        game->stats.max_spells += 1;
        snprintf(body, sizeof body, "Magic boosted");
        break;
    case 'U': {
        int n = spells_count();
        for (int si = 0; si < n && si < 14; si++) {
            game->spells.counts[si] += 1;
        }
        snprintf(body, sizeof body, "+1 of every spell");
        break;
    }
    case 'S':
        game->stats.siege_weapons = 1;
        snprintf(body, sizeof body, "Siege weapons granted");
        break;
    case 'F':
        game->character.mount = MOUNT_FLY;
        snprintf(body, sizeof body, "Flight granted");
        break;
    case 'N': {
        int found = -1;
        for (int zi = 0; zi < res->zone_count; zi++) {
            if (!game->world.zones_discovered[zi]) {
                game->world.zones_discovered[zi] = true;
                found = zi;
                break;
            }
        }
        if (found >= 0)
            snprintf(body, sizeof body, "Revealed: %s",
                     res->zones[found].id);
        else
            snprintf(body, sizeof body, "All zones already known");
        break;
    }
    case 'O': {
        int revealed = 0;
        for (int y = 0; y < map->height; y++) {
            for (int x = 0; x < map->width; x++) {
                if (!fog->seen[y][x]) {
                    fog->seen[y][x] = true;
                    revealed++;
                }
            }
        }
        snprintf(body, sizeof body, "Map fog cleared (%d tiles)", revealed);
        break;
    }
    case 'W':
        s_cheat_menu_active = false;
        dialog_dismiss();
        run_end_cartoon(render_target, res, sprites);
        show_win_game(game, res);
        return CHEAT_DISPATCHED_TERMINAL;
    case 'L':
        s_cheat_menu_active = false;
        dialog_dismiss();
        show_lose_game(game, res);
        return CHEAT_DISPATCHED_TERMINAL;
    default:
        snprintf(body, sizeof body, "Unknown cheat: %c", letter);
        break;
    }
    s_cheat_menu_active = false;
    dialog_dismiss();
    if (body[0]) player_io_message(game, "Debug", body);
    return CHEAT_DISPATCHED;
}
