// tests/unit/test_combat_digests.c
//
// Golden-digest regression for the combat damage formula. Each case
// runs combat_test_digest() with a SEED:ATTACKER:N:DEFENDER:N:ROUNDS
// spec and asserts the resulting 64-bit hex digest matches the golden.
//
// A digest mismatch means the formula behavior changed — either
// intentional (regenerate goldens) or regression (bisect).
//
// Cases exercise distinct code paths: low/high skill, SCYTHE attacker,
// MAGIC-vs-IMMUNE pairs, large stacks (turn_count snapshotting),
// same-troop symmetry, unit-type advantages, edge counts, seed
// variance, magic/undead matchups.

#include "greatest.h"
#include "combat.h"
#include "fixtures.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *spec;        // "SEED:ATTACKER:N:DEFENDER:N:ROUNDS"
    uint64_t    expected;    // golden digest
} DigestCase;

static const DigestCase CASES[] = {
    // Original Phase 14 set
    { "12345:peasants:50:orcs:30:5",     0xa25e1141f58e75d3ULL },
    { "99:knights:10:dragons:5:3",       0xb7c1e9858f457aafULL },
    { "777:demons:20:peasants:100:4",    0x0fb49e22fa60e35cULL },
    { "42:archers:25:wolves:20:6",       0x265821b6835569f8ULL },
    { "1:dragons:5:peasants:100:1",      0x5e51c26b49093a9cULL },
    // Same-troop matchups (uniform combat — exercises symmetry)
    { "1:peasants:50:peasants:50:5",     0xbcf382e16c5a05c4ULL },
    { "1:archers:20:archers:20:5",       0x3fd3d372e8f3254cULL },
    { "1:pikemen:15:pikemen:15:5",       0x6c6dfc8cc97d3706ULL },
    { "1:knights:8:knights:8:5",         0x14f9c77620c785ecULL },
    { "1:dragons:3:dragons:3:3",         0xe2be84f3c28dd790ULL },
    // Asymmetric (unit-type advantages)
    { "42:archers:20:peasants:100:4",    0x9fd00c8626b2b9f2ULL },
    { "42:knights:10:orcs:20:5",         0x6a33d496cd284eb9ULL },
    { "42:sprites:30:ogres:5:4",         0x4aa3f46c732c1791ULL },
    { "42:wolves:20:militia:40:4",       0x0b91a3557756238eULL },
    { "42:ghosts:10:skeletons:20:5",     0x1b2d1c4719873d4eULL },
    // Edge counts
    { "7:dragons:1:peasants:200:3",      0x9f8d7de4dc70890dULL },
    { "7:peasants:1:peasants:1:2",       0x4f23ac9fa0839fbdULL },
    { "7:trolls:50:trolls:50:8",         0xb8d2d54fdddefb59ULL },
    // Seed variance — same matchup, different seeds
    { "100:archers:25:orcs:15:5",        0x07792e312e0a38a5ULL },
    { "200:archers:25:orcs:15:5",        0xbbdf349cae479291ULL },
    { "300:archers:25:orcs:15:5",        0x288a8154ba9816a4ULL },
    { "400:archers:25:orcs:15:5",        0xa8c990b62a50a7c0ULL },
    { "500:archers:25:orcs:15:5",        0x80caddaf7e21a98eULL },
    // Magic / undead matchups
    { "13:vampires:10:zombies:30:5",     0x73f3eabad93626e6ULL },
    { "21:archmages:5:demons:8:4",       0x0da6c5a81262affeULL },
};

#define CASE_COUNT (sizeof(CASES) / sizeof(CASES[0]))

// Parse "SEED:ATTACKER:N:DEFENDER:N:ROUNDS" into its 6 fields.
// Returns true on success.
static bool parse_spec(const char *spec, uint64_t *seed,
                       char *atk, size_t atk_cap, int *an,
                       char *def, size_t def_cap, int *dn,
                       int *rounds) {
    char buf[256];
    snprintf(buf, sizeof buf, "%s", spec);
    char atk_buf[64], def_buf[64];
    unsigned long seed_in = 0;
    int an_in = 0, dn_in = 0, rounds_in = 0;
    if (sscanf(buf, "%lu:%63[^:]:%d:%63[^:]:%d:%d",
               &seed_in, atk_buf, &an_in, def_buf, &dn_in, &rounds_in) != 6) {
        return false;
    }
    *seed = (uint64_t)seed_in;
    snprintf(atk, atk_cap, "%s", atk_buf);
    snprintf(def, def_cap, "%s", def_buf);
    *an = an_in;
    *dn = dn_in;
    *rounds = rounds_in;
    return true;
}

TEST combat_digest_case(size_t idx) {
    const DigestCase *c = &CASES[idx];
    uint64_t seed = 0;
    char     atk[64] = "", def[64] = "";
    int      an = 0, dn = 0, rounds = 0;
    if (!parse_spec(c->spec, &seed, atk, sizeof atk, &an,
                    def, sizeof def, &dn, &rounds)) {
        FAILm("malformed test case spec");
    }
    uint64_t got = combat_test_digest(seed, atk, an, def, dn, rounds);
    if (got != c->expected) {
        // greatest truncates FAILm messages at ~16 chars, so print
        // the full mismatch to stderr ourselves before failing.
        fprintf(stderr,
                "\n  digest mismatch: spec=%s expected=%016llx got=%016llx\n",
                c->spec,
                (unsigned long long)c->expected,
                (unsigned long long)got);
        FAIL();
    }
    PASS();
}

SUITE(regression_combat_digests_suite) {
    // Lazy-load resources so troop_by_id() can resolve catalog entries.
    Resources *res = fx_load_resources();
    if (!res) {
        fprintf(stderr,
                "regression_combat_digests_suite: fx_load_resources failed\n");
        return;
    }
    for (size_t i = 0; i < CASE_COUNT; i++) {
        RUN_TESTp(combat_digest_case, i);
    }
    resources_free(res);
    free(res);
}
