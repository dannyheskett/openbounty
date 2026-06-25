// tests/library/consumer.c
//
// Minimal external-style consumer of libobengine.a. Initializes a
// game, takes several steps, dumps a snapshot to stderr (so it can
// be inspected when debugging), and exits 0 on success.
//
// This binary links ONLY libobengine.a + engine/host_noop.c + libc/
// libm/pthread. If the link succeeds, the engine's public API is
// self-contained — any shell dependency leakage will fail the link.
//
// Run via `make test-library`. CI gate: catches regressions where
// engine code starts calling shell functions not declared in
// ui_host.h.

#include "game.h"
#include "map.h"
#include "fog.h"
#include "pack.h"
#include "resources.h"
#include "step.h"
#include "state_serialize.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *PACK_DIR = "assets/kings-bounty";

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    Pack *pack = pack_open(PACK_DIR);
    if (!pack) {
        fprintf(stderr, "consumer: pack_open(%s) failed\n", PACK_DIR);
        return 1;
    }
    pack_stack_push(pack);

    Resources *res = calloc(1, sizeof *res);
    if (!res || !resources_load(res, "game.json")) {
        fprintf(stderr, "consumer: resources_load failed\n");
        return 1;
    }

    Game *game = calloc(1, sizeof *game);
    Map  *map  = calloc(1, sizeof *map);
    Fog  *fog  = calloc(1, sizeof *fog);
    if (!game || !map || !fog) {
        fprintf(stderr, "consumer: alloc failed\n");
        return 1;
    }

    game->res  = res;
    game->seed = 42;
    GameInit(game, "LibTest", /*pclass=*/0, /*difficulty=*/1, NULL);
    FogInit(fog);
    if (!MapLoadZoneWithPlacements(map, res, res->world.starting_zone, game)) {
        fprintf(stderr, "consumer: MapLoadZoneWithPlacements failed\n");
        return 1;
    }

    // Take a few steps so we exercise step.c, flows, etc.
    int moved = 0;
    for (int i = 0; i < 5; i++) {
        if (GameStep(game, map, fog, res, /*dx=*/1, /*dy=*/0)) moved++;
        if (game->stats.game_over) break;
    }

    // Confirm we can serialize state.
    cJSON *snap = state_build_snapshot(game, NULL, map, fog,
                                       "library-test", NULL, 0, 0);
    if (!snap) {
        fprintf(stderr, "consumer: state_build_snapshot returned NULL\n");
        return 1;
    }
    char *json = cJSON_PrintUnformatted(snap);
    if (!json) {
        fprintf(stderr, "consumer: cJSON_PrintUnformatted returned NULL\n");
        cJSON_Delete(snap);
        return 1;
    }
    fprintf(stderr, "consumer: ran %d steps (%d moved). Snapshot %zu bytes.\n",
            5, moved, strlen(json));
    free(json);
    cJSON_Delete(snap);

    free(fog);
    free(map);
    free(game);
    resources_free(res);
    free(res);
    pack_stack_clear();
    return 0;
}
