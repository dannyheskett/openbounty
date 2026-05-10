// Test-only forward-declarations for src/combat.c internal helpers.
// The corresponding functions are marked "/* exposed for tests */"
// in combat.c instead of `static`. Production code does not depend
// on this header.

#ifndef OB_TEST_COMBAT_INTERNAL_H
#define OB_TEST_COMBAT_INTERNAL_H

#include "combat.h"
#include "game.h"

// RNG helpers
int  combat_rand(Combat *c, int min, int max);
void combat_seed_rng(Combat *c, const Game *g, CombatMode mode,
                     const CombatTarget *target);

// Unit init / control / morale
void combat_init_unit(CombatUnit *u, int troop_idx, int count);
bool unit_under_control(const Game *g, int troop_idx, int count);
bool units_are_friendly(const Combat *c, int sA, int iA, int sB, int iB);
int  morale_to_rank(char r);

// Geometry / state predicates
bool combat_test_dead(const Combat *c, int side);
bool combat_unit_surrounded(const Combat *c, int side, int slot);
bool combat_cell_passes_filter(const Combat *c, int x, int y,
                               int caster_side, int filter);
bool unit_touching(const CombatUnit *a, const CombatUnit *b);

// Spell helpers
int  spell_damage_value(int base, int sp);
int  spell_damage(Combat *c, int t_side, int t_slot, int dmg);
void spell_clone(Combat *c, int t_side, int t_slot, int sp);
void spell_teleport(Combat *c, int from_side, int from_slot, int to_x, int to_y);
int  spell_freeze(Combat *c, int t_side, int t_slot);
void spell_resurrect(Combat *c, int t_side, int t_slot, int sp);

// Compaction (used after kills)
void combat_compact(Combat *c);

// AI helpers
unsigned char ai_pick_target(const Combat *c, int side, int slot, bool nearby);
void          unit_move_offset(const Combat *c, const CombatUnit *self,
                               int tx, int ty, int *ox, int *oy);

// Damage core. Returns kill count (number of full troops killed).
int  combat_deal_damage(Combat *c, int a_side, int a_id,
                        int t_side, int t_id,
                        bool is_ranged, bool is_external,
                        int  external_damage, bool retaliation);

#endif
