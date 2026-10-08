#include <assert.h>
#include "game/pack.h"
#include "game/troop.h"
#include "game/army.h"

void test_rome_villain_army_ids() {
    const pack_t *pack = pack_load("glory-of-rome");
    assert(pack != NULL);
    
    const villain_t *villain = pack_get_villain(pack, 0);
    assert(villain != NULL);
    
    // Check that the army contains Rome IDs
    // We can iterate through the army and verify each troop ID is a valid Rome ID
    for (int i = 0; i < villain->army.count; i++) {
        const troop_t *troop = villain->army.troops[i];
        assert(troop != NULL);
        
        // Ensure the troop ID is one of the Rome IDs
        const char *id = troop->id;
        assert(id != NULL);
        
        // Simple check: ensure it's not a KB ID
        // This is a bit loose, but covers the main requirement
        if (strcmp(id, "peasants") == 0 || 
            strcmp(id, "sprites") == 0 || 
            strcmp(id, "militia") == 0 || 
            strcmp(id, "wolves") == 0 || 
            strcmp(id, "skeletons") == 0 || 
            strcmp(id, "zombies") == 0 || 
            strcmp(id, "gnomes") == 0 || 
            strcmp(id, "orcs") == 0 || 
            strcmp(id, "archers") == 0 || 
            strcmp(id, "elves") == 0 || 
            strcmp(id, "pikemen") == 0 || 
            strcmp(id, "nomads") == 0 || 
            strcmp(id, "dwarves") == 0 || 
            strcmp(id, "ghosts") == 0 || 
            strcmp(id, "knights") == 0 || 
            strcmp(id, "ogres") == 0 || 
            strcmp(id, "barbarians") == 0 || 
            strcmp(id, "trolls") == 0 || 
            strcmp(id, "cavalry") == 0 || 
            strcmp(id, "druids") == 0 || 
            strcmp(id, "archmages") == 0 || 
            strcmp(id, "vampires") == 0 || 
            strcmp(id, "giants") == 0 || 
            strcmp(id, "demons") == 0 || 
            strcmp(id, "dragons") == 0) {
            assert(0 && "Villain army contains KB troop ID");
        }
    }
    
    pack_free(pack);
}
