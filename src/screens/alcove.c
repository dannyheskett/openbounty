#include "screens/alcove.h"
#include "game/troop.h"
#include "game/pack.h"

// ... other includes and defines

static void alcove_init(alcove_state_t *state) {
    // ... initialization code
    
    // Fallback troop for the alcove
    // Changed from "gnomes" to "fauni" to match Rome pack IDs
    state->fallback_troop = troop_by_id("fauni");
    
    // ... rest of initialization
}

// ... rest of the file
