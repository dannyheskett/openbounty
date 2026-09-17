// A determinism reference for the engine. For five catalog worlds of a pack it
// starts a game, saves it, takes 300 scripted steps -- directions from a fixed
// generator, every decision answered No and every message acknowledged, so no
// combat is entered -- and saves again, into <out-dir>/world<N>_{init,steps}.json.
//
// Run it on two builds and `cmp` the outputs: a refactor that must not change
// behaviour leaves every file byte-identical.
//
//     build/statedump <pack-dir> <out-dir>
//
// Engine-only, like consumer.c: libobengine.a, -lm -lpthread, no raylib.

#include "game.h"
#include "map.h"
#include "fog.h"
#include "pack.h"
#include "player_io.h"
#include "resources.h"
#include "savegame.h"
#include "step.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void answer_all(Game *g, Map *m, Fog *f, const Resources *res) {
    for (int guard = 0; guard < 64 && !player_io_idle(g); guard++) {
        const PlayerRequest *r = player_io_front(g);
        if (!r) break;
        if (r->role == REQ_DECISION) {
            FlowAnswer no = { FLOW_ANS_NO, 0 };
            player_io_answer(g, m, f, res, no, PLAYER_IO_COMBAT_NOT_RUN, NULL);
        } else {
            player_io_ack(g);
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: statedump <pack-dir> <out-dir>\n"); return 2; }
    Pack *pack = pack_open(argv[1]);
    if (!pack) { fprintf(stderr, "statedump: cannot open %s\n", argv[1]); return 1; }
    pack_stack_push(pack);
    Resources *res = calloc(1, sizeof *res);
    if (!res || !resources_load(res, "game.json")) { fprintf(stderr, "statedump: load failed\n"); return 1; }

    for (int world = 0; world < 5; world++) {
        Game *g = calloc(1, sizeof *g);
        Map  *m = calloc(1, sizeof *m);
        Fog  *f = calloc(1, sizeof *f);
        if (!g || !m || !f) return 1;
        g->res = res;
        GameInitSeeded(g, "Reference", world % 4, 1, NULL, world);
        FogInit(f);
        if (!MapLoadZoneWithPlacements(m, res, g->position.zone, g)) {
            fprintf(stderr, "statedump: zone load failed\n");
            return 1;
        }
        char path[1024];
        snprintf(path, sizeof path, "%s/world%d_init.json", argv[2], world);
        if (SaveGameWrite(path, g, m, f) != SAVE_OK) { fprintf(stderr, "statedump: save failed\n"); return 1; }

        unsigned lcg = 12345u + (unsigned)world;
        for (int step = 0; step < 300; step++) {
            lcg = lcg * 1103515245u + 12345u;
            int d = (int)((lcg >> 16) % 8);
            static const int dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
            static const int dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
            GameStep(g, m, f, res, dx[d], dy[d]);
            answer_all(g, m, f, res);
            if (g->stats.game_over) break;
        }
        snprintf(path, sizeof path, "%s/world%d_steps.json", argv[2], world);
        if (SaveGameWrite(path, g, m, f) != SAVE_OK) { fprintf(stderr, "statedump: save failed\n"); return 1; }
        GameFree(g); free(g); MapFree(m); free(m); FogFree(f); free(f);
    }
    printf("statedump: %s -> %s\n", argv[1], argv[2]);
    return 0;
}
