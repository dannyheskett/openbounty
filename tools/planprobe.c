// tools/planprobe.c — THROWAWAY. Boots continentia on a seed, runs plan_build
// once, prints admitted/unadmitted/verdict. Tells us whether the boat/foe fixes
// raised the PROVEN-admissible objective count past the old 10/92 greedy figure.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "map.h"
#include "fog.h"
#include "pack.h"
#include "resources.h"
#include "plan.h"

#define PACK "assets/kings-bounty"
#define MANIFEST "game.json"

int main(int argc, char **argv) {
    uint64_t seed = (argc > 1) ? strtoull(argv[1], NULL, 10) : 1;
    Pack *pk = pack_open(PACK);
    if (!pk) { fprintf(stderr, "pack_open failed\n"); return 2; }
    pack_stack_push(pk);
    Resources *res = calloc(1, sizeof *res);
    if (!resources_load(res, MANIFEST)) { fprintf(stderr, "res load failed\n"); return 2; }
    Game *g = calloc(1, sizeof *g);
    Map *map = calloc(1, sizeof *map);
    Fog *fog = calloc(1, sizeof *fog);
    g->res = res; g->seed = seed;
    GameInit(g, "PlanProbe", 0, 1, NULL);
    FogInit(fog);
    if (!MapLoadZoneWithPlacements(map, res, res->world.starting_zone, g)) {
        fprintf(stderr, "map load failed\n"); return 2;
    }
    Plan plan;
    if (!plan_build(g, map, fog, res, /*diag=*/NULL,
                    /*zone_scope=*/-1, /*demo=*/NULL, &plan)) {
        fprintf(stderr, "plan_build failed\n"); return 2;
    }
    printf("seed=%llu verdict=%s admitted=%d unadmitted=%d total=%d\n",
           (unsigned long long)seed, autoplay_verdict_str(plan.verdict),
           plan.admitted_count, plan.unadmitted_count, plan.set.count);
    return 0;
}
