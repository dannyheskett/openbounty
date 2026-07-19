// engine/include/view_kind.h
//
// ViewKind enum: which full-screen overlay (if any) is currently
// active. Shared by engine (state_serialize emits it; flows query it
// before opening prompts) and shell (renderer switches on it).

#ifndef OB_ENGINE_VIEW_KIND_H
#define OB_ENGINE_VIEW_KIND_H

typedef enum {
    VIEW_NONE = 0,

    VIEW_MENU,        // Esc / O - unified Game Menu (view switch + save/load/quit)
    VIEW_CHARACTER,   // V - portrait + stats + inventory belt
    VIEW_ARMY,        // A - 5 troop rows with counts & stats
    VIEW_SPELLS,      // U - spellbook, combat vs adventuring columns
    VIEW_GATE,        // Town/Castle Gate destination picker (cursored panel)
    VIEW_CONTRACT,    // I - wanted villain's portrait + bounty
    VIEW_PUZZLE,      // P - 5x5 puzzle grid (villain/artifact covers)
    VIEW_WORLDMAP,    // M / TAB - full-continent minimap modal
    VIEW_OPTIONS,     // O - keybind reference screen
    VIEW_CONTROLS,    // C - Controls settings panel (delay/sounds/etc.)

    VIEW_TOWN,             // in-town menu (contract / boat / info / spell / siege)
    VIEW_HOME_CASTLE,      // Castle of King Maximus: Recruit / Audience
    VIEW_OWN_CASTLE,       // Garrison / Remove troops in a player castle
    VIEW_DWELLING,         // Recruit at outdoor dwelling (Plains/Forest/Hill/Dungeon)
    VIEW_ALCOVE,           // Archmage Aurange offer
    VIEW_RECRUIT_SOLDIERS, // Sub-screen of HOME_CASTLE

    VIEW_WIN,
    VIEW_LOSE,
} ViewKind;

#endif
