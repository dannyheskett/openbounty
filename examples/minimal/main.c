// examples/minimal/main.c
//
// A minimal external consumer of libobengine.a. Demonstrates linking
// the engine library, initializing a game, walking the hero, and
// inspecting state — all without raylib, audio, X11, or any other
// shell dependency.

#include "game.h"
#include "map.h"
#include "fog.h"
#include "pack.h"
#include "resources.h"
#include "step.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *pack_dir = argc > 1 ? argv[1] : "assets/kings-bounty";

    // Open the pack and load resources.
    Pack *pack = pack_open(pack_dir);
    if (!pack) {
        fprintf(stderr, "minimal: cannot open pack at %s\n", pack_dir);
        return 1;
    }
    pack_stack_push(pack);

    Resources *res = calloc(1, sizeof *res);
    if (!res || !resources_load(res, "game.json")) {
        fprintf(stderr, "minimal: resources_load failed\n");
        return 1;
    }

    // Init game world.
    Game *game = calloc(1, sizeof *game);
    Map  *map  = calloc(1, sizeof *map);
    Fog  *fog  = calloc(1, sizeof *fog);
    game->res  = res;
    game->seed = 42;
    GameInit(game, "Hero", /*pclass=*/0, /*difficulty=*/1, NULL);
    FogInit(fog);
    MapLoadZoneWithPlacements(map, res, res->world.starting_zone, game);

    printf("Hero %s the %s, seed=%llu, starting at (%d, %d) in %s\n",
           game->character.name,
           game->character.cls.rank_title,
           (unsigned long long)game->seed,
           game->position.x, game->position.y,
           res->world.starting_zone);

    // Walk east 10 tiles.
    int moved = 0;
    for (int i = 0; i < 10; i++) {
        if (step_try(game, map, fog, res, /*dx=*/1, /*dy=*/0)) moved++;
    }
    printf("After 10 attempted east steps: position (%d, %d), %d succeeded, "
           "gold=%d, days_left=%d\n",
           game->position.x, game->position.y, moved,
           game->stats.gold, game->stats.days_left);

    free(fog);
    free(map);
    free(game);
    resources_free(res);
    free(res);
    pack_stack_clear();
    return 0;
}
