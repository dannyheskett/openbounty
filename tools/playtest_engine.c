// tools/playtest_engine.c
//
// In-process playtest driver. Links the engine source directly (with
// the raylib stub replacing real raylib) and drives the game via
// function calls — no socket, no fork, no rendered window, no 60fps
// wallclock pacing.
//
// This is the headless companion to tools/playtest.c (which spawns
// build/openbounty as a child and drives it over an AF_UNIX socket).
// The two share the JSON scenario format (tools/scenario.c) but not
// the execution model.
//
// First cut: minimal CLI that initialises a game, runs a fixed number
// of step attempts in a direction, and dumps state. Once that's
// working, scenario JSON dispatch can be wired in.

#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "map.h"
#include "fog.h"
#include "pack.h"
#include "resources.h"
#include "step.h"
#include "savegame.h"
#include "state_serialize.h"
#include "combat.h"
#include "tables.h"
#include "cJSON.h"

static const char *PACK_DIR_DEFAULT = "assets/kings-bounty";

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --pack <dir>      pack directory (default: assets/kings-bounty)\n"
        "  --seed <N>        deterministic seed (default: 42)\n"
        "  --class <0-3>     0=knight 1=paladin 2=sorceress 3=barbarian (default: 0)\n"
        "  --name <s>        hero name (default: Bot)\n"
        "  --difficulty <N>  0=easy 1=normal 2=hard 3=impossible (default: 1)\n"
        "  --steps <DIR:N>   walk N tiles in direction DIR (U/D/L/R/NE/NW/SE/SW)\n"
        "                    may repeat\n"
        "  --dump            print state JSON at end\n"
        "  --quiet           suppress per-step logging\n",
        argv0);
}

static int dir_to_dxdy(const char *dir, int *dx, int *dy) {
    if (!dir || !*dir) return -1;
    *dx = 0; *dy = 0;
    if (strcmp(dir, "U")  == 0) { *dy = -1; return 0; }
    if (strcmp(dir, "D")  == 0) { *dy =  1; return 0; }
    if (strcmp(dir, "L")  == 0) { *dx = -1; return 0; }
    if (strcmp(dir, "R")  == 0) { *dx =  1; return 0; }
    if (strcmp(dir, "NE") == 0) { *dx =  1; *dy = -1; return 0; }
    if (strcmp(dir, "NW") == 0) { *dx = -1; *dy = -1; return 0; }
    if (strcmp(dir, "SE") == 0) { *dx =  1; *dy =  1; return 0; }
    if (strcmp(dir, "SW") == 0) { *dx = -1; *dy =  1; return 0; }
    return -1;
}

typedef struct {
    const char *dir;
    int n;
} StepBatch;

#define MAX_BATCHES 64

int main(int argc, char **argv) {
    const char  *pack_dir   = PACK_DIR_DEFAULT;
    unsigned long seed      = 42;
    int           pclass    = 0;
    const char   *name      = "Bot";
    int           difficulty = 1;
    bool          dump      = false;
    bool          quiet     = false;
    StepBatch     batches[MAX_BATCHES];
    int           nbatch    = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage(argv[0]); return 0;
        } else if (strcmp(a, "--pack") == 0 && i + 1 < argc) {
            pack_dir = argv[++i];
        } else if (strcmp(a, "--seed") == 0 && i + 1 < argc) {
            seed = strtoul(argv[++i], NULL, 0);
        } else if (strcmp(a, "--class") == 0 && i + 1 < argc) {
            pclass = atoi(argv[++i]);
        } else if (strcmp(a, "--name") == 0 && i + 1 < argc) {
            name = argv[++i];
        } else if (strcmp(a, "--difficulty") == 0 && i + 1 < argc) {
            difficulty = atoi(argv[++i]);
        } else if (strcmp(a, "--steps") == 0 && i + 1 < argc) {
            if (nbatch >= MAX_BATCHES) {
                fprintf(stderr, "too many --steps batches (max %d)\n", MAX_BATCHES);
                return 2;
            }
            char *spec = argv[++i];
            char *colon = strchr(spec, ':');
            if (!colon) { fprintf(stderr, "bad --steps: expected DIR:N\n"); return 2; }
            *colon = '\0';
            batches[nbatch].dir = spec;
            batches[nbatch].n   = atoi(colon + 1);
            nbatch++;
        } else if (strcmp(a, "--dump") == 0) {
            dump = true;
        } else if (strcmp(a, "--quiet") == 0) {
            quiet = true;
        } else {
            fprintf(stderr, "unknown arg: %s\n", a);
            usage(argv[0]);
            return 2;
        }
    }

    // Open pack.
    Pack *pack = pack_open(pack_dir);
    if (!pack) {
        fprintf(stderr, "failed to open pack: %s\n", pack_dir);
        return 1;
    }
    pack_stack_push(pack);

    // Load resources.
    Resources *res = calloc(1, sizeof *res);
    if (!res || !resources_load(res, "game.json")) {
        fprintf(stderr, "failed to load resources from %s\n", pack_dir);
        return 1;
    }

    // Init game.
    Game *game = calloc(1, sizeof *game);
    Map  *map  = calloc(1, sizeof *map);
    Fog  *fog  = calloc(1, sizeof *fog);
    if (!game || !map || !fog) {
        fprintf(stderr, "alloc failed\n");
        return 1;
    }
    game->res  = res;
    game->seed = seed;
    GameInit(game, name, pclass, difficulty, NULL);
    FogInit(fog);
    if (!MapLoadZoneWithPlacements(map, res, res->world.starting_zone, game)) {
        fprintf(stderr, "failed to load starting zone %s\n",
                res->world.starting_zone);
        return 1;
    }

    if (!quiet) {
        fprintf(stderr, "engine-playtest: seed=%lu class=%d name=%s difficulty=%d zone=%s\n",
                seed, pclass, name, difficulty, res->world.starting_zone);
        fprintf(stderr, "engine-playtest: hero start=(%d,%d) gold=%d days=%d\n",
                game->position.x, game->position.y,
                game->stats.gold, game->stats.days_left);
    }

    // Walk the batches.
    int total_steps = 0, successful = 0;
    for (int b = 0; b < nbatch; b++) {
        int dx, dy;
        if (dir_to_dxdy(batches[b].dir, &dx, &dy) != 0) {
            fprintf(stderr, "bad dir: %s\n", batches[b].dir);
            return 2;
        }
        if (!quiet) {
            fprintf(stderr, "engine-playtest: step batch %s × %d\n",
                    batches[b].dir, batches[b].n);
        }
        for (int k = 0; k < batches[b].n; k++) {
            total_steps++;
            bool moved = step_try(game, map, fog, res, dx, dy);
            if (moved) successful++;
            if (game->stats.game_over) {
                if (!quiet) {
                    fprintf(stderr, "engine-playtest: game over after %d steps\n",
                            total_steps);
                }
                goto done;
            }
        }
    }

done:
    if (!quiet) {
        fprintf(stderr, "engine-playtest: ran %d steps (%d successful), end=(%d,%d) gold=%d days=%d\n",
                total_steps, successful,
                game->position.x, game->position.y,
                game->stats.gold, game->stats.days_left);
    }

    if (dump) {
        cJSON *snap = state_build_snapshot(game, NULL, map, fog,
                                           "playtest:end", NULL, 0, 0);
        if (snap) {
            char *json = cJSON_PrintUnformatted(snap);
            if (json) {
                fputs(json, stdout);
                fputc('\n', stdout);
                free(json);
            }
            cJSON_Delete(snap);
        } else {
            fprintf(stderr, "state_build_snapshot failed\n");
        }
    }

    free(fog);
    free(map);
    free(game);
    resources_free(res);
    free(res);
    pack_stack_clear();
    return 0;
}
