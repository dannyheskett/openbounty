#ifndef ADVENTURE_H
#define ADVENTURE_H

#include <stdbool.h>
#include "map.h"   // Tile struct and interact/terrain enums

// Tile walkability in each travel mode.
// Returns true if the hero can enter the tile.
bool adventure_walkable_on_foot(const Tile *t);
bool adventure_walkable_in_boat(const Tile *t);
bool adventure_walkable_in_flight(const Tile *t);

// Result of stepping onto an interactive tile. Fields are set by
// adventure_handle_interact() and read by the caller to update Game state.
typedef struct {
    bool opened_dialog;    // handler produced a dialog
    bool bounce_back;      // caller should revert the move (castle/town gate)
    int  artifact_idx;     // global artifact index to claim, or -1
    bool entered_town;     // hero stepped onto a town tile; main.c opens VIEW_TOWN
    // When entered_town: the town tile's metadata, copied so main.c doesn't
    // need to re-fetch the tile. town_id is empty when the tile has none.
    char town_id[24];
    int  town_boat_x, town_boat_y;
    bool opened_chest;     // stepped onto a treasure_chest — main.c rolls loot
    bool opened_dwelling;  // stepped onto a dwelling_* tile — show info dialog
    Interact dwelling_kind; // DWELLING_PLAINS/FOREST/HILLS/DUNGEON
    bool opened_alcove;    // stepped onto Archmage's alcove — step.c routes flow
    bool opened_castle;    // stepped onto a castle_gate — main.c shows visit flow
    char castle_id[24];    // when opened_castle: the castle id from the tile
    bool opened_orb;       // stepped onto orb tile — main.c triggers reveal + dialog
    bool opened_telecave;  // stepped onto telecave — main.c teleports to paired cave
    char telecave_id[24];  // when opened_telecave: placement id ("telecave_0" etc.)
    bool opened_navmap;    // stepped onto navmap — main.c unlocks next continent
    char navmap_id[24];    // when opened_navmap: placement id ("navmap_N")
    // has one foe tile. Stepping on it triggers either an attack
    // prompt or a join-for-gold prompt depending on the foe's `friendly`
    // flag in g->foes[]. main.c does that lookup; adventure just surfaces
    // the placement id and the bounce_back gate (only hostiles bounce —
    // friendlies are walked-onto and recruit dialog opens in place).
    bool opened_foe;
    char foe_id[24];       // when opened_foe: placement id
} InteractResult;

// Handle a step onto an interactive tile. Pops a dialog if applicable.
// `zone` is the hero's current zone id, used to resolve which artifact
// an INTERACT_ARTIFACT tile grants.
InteractResult adventure_handle_interact(const Tile *t, const char *zone);

#endif
