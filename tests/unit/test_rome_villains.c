// The Glory of Rome's villain-only troops (#200): the Ifrit and the Athanatoi
// are fielded only in villain garrisons. No dwelling sells them, no spawn pool
// or lair list rolls them, and no hand-placed army carries them, so the hero
// can never recruit or clone one.

#include "greatest.h"
#include "pack.h"
#include "resources.h"

#include <stdlib.h>
#include <string.h>

static const char *const VILLAIN_ONLY[] = { "ifrit", "athanatoi" };
#define N_VILLAIN_ONLY 2

static int villain_only(const char *id) {
    for (int i = 0; i < N_VILLAIN_ONLY; i++)
        if (strcmp(id, VILLAIN_ONLY[i]) == 0) return i;
    return -1;
}

TEST rome_villain_only_troops_stay_with_the_villains(void) {
    Pack *p = pack_open("assets/glory-of-rome");
    ASSERT(p);
    pack_stack_push(p);
    Resources *r = calloc(1, sizeof *r);
    bool ok = r && resources_load(r, "game.json");
    // Every check is read into these flags before any assert, so a failure
    // never leaves the Rome pack on the stack for later suites.
    bool found[N_VILLAIN_ONLY] = { false }, fielded[N_VILLAIN_ONLY] = { false };
    bool unsold = true, unpooled = true, unplaced = true;
    for (int i = 0; ok && i < r->troops_count; i++) {
        int k = villain_only(r->troops[i].id);
        if (k < 0) continue;
        found[k] = true;
        if (r->troops[i].dwelling[0] || r->troops[i].max_population != 0 ||
            r->troops[i].growth_per_week != 0) unsold = false;
    }
    for (int kind = 0; ok && kind < RES_SPAWN_TIERS; kind++)
        for (int s = 0; s < r->spawn.pool_count[kind]; s++)
            if (villain_only(r->spawn.troop_pool[kind][s]) >= 0) unpooled = false;
    for (int zi = 0; ok && zi < r->zone_count; zi++) {
        const ResZone *z = &r->zones[zi];
        for (int i = 0; i < z->salt.preferred_troop_count; i++)
            if (villain_only(z->salt.preferred_troops[i]) >= 0) unpooled = false;
        for (int i = 0; i < z->dwelling_count; i++)
            if (villain_only(z->dwellings[i].troop) >= 0) unplaced = false;
        for (int i = 0; i < z->army_count; i++)
            for (int s = 0; s < z->armies[i].army_stacks; s++)
                if (villain_only(z->armies[i].army_id[s]) >= 0) unplaced = false;
    }
    for (int v = 0; ok && v < r->villains_count; v++)
        for (int s = 0; s < 5; s++) {
            int k = villain_only(r->villains[v].army_troops[s]);
            if (k >= 0 && r->villains[v].army_counts[s] > 0) fielded[k] = true;
        }
    if (r) resources_free(r);
    free(r);
    pack_stack_pop();

    ASSERT(ok);
    for (int k = 0; k < N_VILLAIN_ONLY; k++) {
        ASSERTm(VILLAIN_ONLY[k], found[k]);
        ASSERTm(VILLAIN_ONLY[k], fielded[k]);
    }
    ASSERT(unsold);
    ASSERT(unpooled);
    ASSERT(unplaced);
    PASS();
}

SUITE(unit_rome_villains_suite) {
    RUN_TEST(rome_villain_only_troops_stay_with_the_villains);
}
