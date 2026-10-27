// The Glory of Rome's lairs rise with the province (#106): each zone's salt
// settings are its own, every preferred troop exists and hosts a dwelling,
// and the last province's range holds the strongest troops, elephants in.

#include "greatest.h"
#include "pack.h"
#include "resources.h"

#include <stdlib.h>
#include <string.h>

static int troop_index(const Resources *r, const char *id) {
    for (int i = 0; i < r->troops_count; i++)
        if (strcmp(r->troops[i].id, id) == 0) return i;
    return -1;
}

TEST rome_lairs_rise_with_the_province(void) {
    Pack *p = pack_open("assets/glory-of-rome");
    ASSERT(p);
    pack_stack_push(p);
    Resources *r = calloc(1, sizeof *r);
    bool ok = r && resources_load(r, "game.json");
    // Every check is read into these flags before any assert, so a failure
    // never leaves the Rome pack on the stack for later suites.
    int zones = ok ? r->zone_count : 0;
    bool windows_ok = true, rising = true, troops_ok = true, lists_own = true;
    int prev_min = -1;
    for (int zi = 0; zi < zones; zi++) {
        const ResZone *z = &r->zones[zi];
        // The range is a real, rising window of the catalog.
        if (z->salt.dwelling_range_min < 0 ||
            z->salt.dwelling_range_max < z->salt.dwelling_range_min ||
            z->salt.dwelling_range_max >= r->troops_count) windows_ok = false;
        if (z->salt.dwelling_range_min <= prev_min) rising = false;
        prev_min = z->salt.dwelling_range_min;
        // Every preferred troop is in the catalog and can host a dwelling.
        if (z->salt.preferred_troop_count <= 0) troops_ok = false;
        for (int i = 0; i < z->salt.preferred_troop_count; i++) {
            int ti = troop_index(r, z->salt.preferred_troops[i]);
            if (ti < 0 || strcmp(r->troops[ti].dwelling, "castle") == 0) troops_ok = false;
        }
        // No province copies the first one's list (the bug: all four did).
        if (zi > 0) {
            const ResZone *z0 = &r->zones[0];
            bool same = z->salt.preferred_troop_count == z0->salt.preferred_troop_count;
            for (int i = 0; same && i < z->salt.preferred_troop_count; i++)
                same = strcmp(z->salt.preferred_troops[i], z0->salt.preferred_troops[i]) == 0;
            if (same) lists_own = false;
        }
    }
    // The last province's window starts above the mid-catalog troops and
    // reaches the elephants.
    int elephants = ok ? troop_index(r, "elephanti") : -1;
    int last_min = zones > 0 ? r->zones[zones - 1].salt.dwelling_range_min : -1;
    int last_max = zones > 0 ? r->zones[zones - 1].salt.dwelling_range_max : -1;
    if (r) resources_free(r);
    free(r);
    pack_stack_pop();

    ASSERT(ok);
    ASSERT_EQ(4, zones);
    ASSERT(windows_ok);
    ASSERT(rising);
    ASSERT(troops_ok);
    ASSERT(lists_own);
    ASSERT(elephants >= 0);
    ASSERT(last_min >= 20);
    ASSERT(elephants >= last_min && elephants <= last_max);
    PASS();
}

// Its castles rise with the province too (#105): each province's lowest
// garrison tier is above the one before and not below that one's highest, and
// the last province's castles roll from the top tier, as King's Bounty's do.
TEST rome_castles_rise_with_the_province(void) {
    Pack *p = pack_open("assets/glory-of-rome");
    ASSERT(p);
    pack_stack_push(p);
    Resources *r = calloc(1, sizeof *r);
    bool ok = r && resources_load(r, "game.json");
    // Each province's lowest and highest garrison tier, read before any assert
    // so a failure never leaves the Rome pack on the stack for later suites.
    int zones = ok ? r->zone_count : 0;
    int lo[4], hi[4];
    bool in_range = true;
    for (int zi = 0; zi < zones && zi < 4; zi++) {
        lo[zi] = RES_SPAWN_TIERS; hi[zi] = -1;
        for (int i = 0; i < r->castle_count; i++) {
            const ResCastle *c = &r->castles[i];
            if (strcmp(c->zone, r->zones[zi].id) != 0 || c->special.flow[0]) continue;
            if (c->difficulty_tier < 0 || c->difficulty_tier >= RES_SPAWN_TIERS) in_range = false;
            if (c->difficulty_tier < lo[zi]) lo[zi] = c->difficulty_tier;
            if (c->difficulty_tier > hi[zi]) hi[zi] = c->difficulty_tier;
        }
    }
    if (r) resources_free(r);
    free(r);
    pack_stack_pop();

    ASSERT(ok);
    ASSERT_EQ(4, zones);
    ASSERT(in_range);
    for (int zi = 0; zi < zones; zi++) {
        ASSERT(hi[zi] >= 0);                         // every province has castles
        if (zi == 0) continue;
        ASSERT(lo[zi] > lo[zi - 1]);
        ASSERT(lo[zi] >= hi[zi - 1]);
    }
    ASSERT_EQ(RES_SPAWN_TIERS - 1, lo[zones - 1]);  // Oriens: the top tier throughout
    PASS();
}

SUITE(unit_rome_salt_suite) {
    RUN_TEST(rome_lairs_rise_with_the_province);
    RUN_TEST(rome_castles_rise_with_the_province);
}
