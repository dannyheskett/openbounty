#include "demo/demo_brain.h"
#include "game/troop.h"
#include "game/army.h"

// ... other includes and defines

void demo_reset_army(demo_state_t *state) {
    // Reset the demo army to starting troops
    // Changed from "peasants" to "coloni" to match Rome pack IDs
    army_clear(&state->player_army);
    army_add_troop(&state->player_army, troop_by_id("coloni"), 10);
    
    // ... rest of reset logic
}

// ... rest of the file
