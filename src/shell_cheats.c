// src/shell_cheats.c

#include "shell_cheats.h"

#include <stdio.h>

#include "tables.h"
#include "ui.h"
#include "end_cartoon.h"
#include "flows.h"

static const char *const CHEAT_LABELS[CHEAT_COUNT] = {
    [CHEAT_GOLD]       = "Gold +50000",
    [CHEAT_LEADERSHIP] = "Leadership +100",
    [CHEAT_MAGIC]      = "Magic boost",
    [CHEAT_SPELLS]     = "+1 of every spell",
    [CHEAT_SIEGE]      = "Siege weapons",
    [CHEAT_FLIGHT]     = "Flight",
    [CHEAT_ZONE]       = "Reveal a zone",
    [CHEAT_FOG]        = "Clear map fog",
    [CHEAT_WIN]        = "Win",
    [CHEAT_LOSE]       = "Lose",
};

// What each cheat does, for the Debug page's description panel.
static const char *const CHEAT_DESCS[CHEAT_COUNT] = {
    [CHEAT_GOLD]       = "Adds 50,000 gold to your purse.",
    [CHEAT_LEADERSHIP] = "Adds 100 to your leadership.",
    [CHEAT_MAGIC]      = "Adds 1 to spell power and 1 to spell capacity.",
    [CHEAT_SPELLS]     = "Adds one charge of every spell to your book.",
    [CHEAT_SIEGE]      = "Gives you siege weapons.",
    [CHEAT_FLIGHT]     = "Lets your army fly.",
    [CHEAT_ZONE]       = "Reveals the next undiscovered continent.",
    [CHEAT_FOG]        = "Clears the fog from this continent's map.",
    [CHEAT_WIN]        = "Ends the game as a victory.",
    [CHEAT_LOSE]       = "Ends the game as a defeat.",
};

const char *cheat_desc(CheatAction a) {
    return (a >= 0 && a < CHEAT_COUNT) ? CHEAT_DESCS[a] : "";
}

const char *cheat_label(CheatAction a) {
    return (a >= 0 && a < CHEAT_COUNT) ? CHEAT_LABELS[a] : "";
}

CheatResult cheat_apply(CheatAction a, Game *game, Map *map, Fog *fog,
                        const Resources *res, const Sprites *sprites,
                        RenderTexture2D *render_target) {
    char body[160];
    body[0] = '\0';
    switch (a) {
    case CHEAT_GOLD:
        game->stats.gold += 50000;
        snprintf(body, sizeof body, "Gold +50000");
        break;
    case CHEAT_LEADERSHIP:
        game->stats.leadership_current += 100;
        game->stats.leadership_base += 100;
        snprintf(body, sizeof body, "Leadership +100");
        break;
    case CHEAT_MAGIC:
        game->stats.spell_power += 1;
        game->stats.max_spells += 1;
        snprintf(body, sizeof body, "Magic boosted");
        break;
    case CHEAT_SPELLS: {
        int n = spells_count();
        for (int si = 0; si < n && si < 14; si++) {
            game->spells.counts[si] += 1;
        }
        snprintf(body, sizeof body, "+1 of every spell");
        break;
    }
    case CHEAT_SIEGE:
        game->stats.siege_weapons = 1;
        snprintf(body, sizeof body, "Siege weapons granted");
        break;
    case CHEAT_FLIGHT:
        game->character.mount = MOUNT_FLY;
        snprintf(body, sizeof body, "Flight granted");
        break;
    case CHEAT_ZONE: {
        int found = -1;
        for (int zi = 0; zi < res->zone_count; zi++) {
            if (!game->world.zones_discovered[zi]) {
                game->world.zones_discovered[zi] = true;
                found = zi;
                break;
            }
        }
        if (found >= 0)
            snprintf(body, sizeof body, "Revealed: %s", res->zones[found].id);
        else
            snprintf(body, sizeof body, "All zones already known");
        break;
    }
    case CHEAT_FOG: {
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
    case CHEAT_WIN:
        run_end_cartoon(render_target, res, sprites, game);
        show_win_game(game, res);
        return CHEAT_DISPATCHED_TERMINAL;
    case CHEAT_LOSE:
        show_lose_game(game, res);
        return CHEAT_DISPATCHED_TERMINAL;
    default:
        break;
    }
    if (body[0]) player_io_message(game, "Debug", body);
    return CHEAT_DISPATCHED;
}
