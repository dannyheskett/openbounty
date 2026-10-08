#include <assert.h>
#include "game/pack.h"
#include "game/troop.h"
#include "game/salt.h"

void test_rome_salt_troop_pools() {
    // Test that salt tier_troop_pool uses Rome IDs
    const pack_t *pack = pack_load("glory-of-rome");
    assert(pack != NULL);
    
    // Example check: Zone 0 should have coloni, lares, tirones
    const zone_t *zone = pack_get_zone(pack, 0);
    assert(zone != NULL);
    
    const troop_t *t1 = troop_by_id("coloni");
    const troop_t *t2 = troop_by_id("lares");
    const troop_t *t3 = troop_by_id("tirones");
    
    assert(t1 != NULL && t2 != NULL && t3 != NULL);
    
    // Verify they are in the pool
    // Assuming a function to check pool membership exists
    assert(salt_troop_in_pool(&zone->salt, t1));
    assert(salt_troop_in_pool(&zone->salt, t2));
    assert(salt_troop_in_pool(&zone->salt, t3));
    
    pack_free(pack);
}

void test_rome_salt_preferred_troops() {
    const pack_t *pack = pack_load("glory-of-rome");
    assert(pack != NULL);
    
    const zone_t *zone = pack_get_zone(pack, 0);
    assert(zone != NULL);
    
    const troop_t *t1 = troop_by_id("coloni");
    const troop_t *t2 = troop_by_id("lares");
    
    assert(t1 != NULL && t2 != NULL);
    
    assert(zone->preferred_troops[0] == t1);
    assert(zone->preferred_troops[1] == t2);
    
    pack_free(pack);
}
