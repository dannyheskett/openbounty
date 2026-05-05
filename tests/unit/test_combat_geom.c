// Tests for combat geometry / state predicates.

#include "greatest.h"
#include "combat_internal.h"

#include <string.h>

static CombatUnit make_unit(int x, int y, int troop_idx, int count) {
    CombatUnit u;
    combat_init_unit(&u, troop_idx, count);
    u.x = x;
    u.y = y;
    return u;
}

TEST touching_orthogonal_neighbors(void) {
    CombatUnit a = make_unit(2, 2, 0, 1);
    CombatUnit b1 = make_unit(3, 2, 0, 1);  // east
    CombatUnit b2 = make_unit(2, 3, 0, 1);  // south
    CombatUnit b3 = make_unit(1, 2, 0, 1);  // west
    CombatUnit b4 = make_unit(2, 1, 0, 1);  // north
    ASSERT(unit_touching(&a, &b1));
    ASSERT(unit_touching(&a, &b2));
    ASSERT(unit_touching(&a, &b3));
    ASSERT(unit_touching(&a, &b4));
    PASS();
}

TEST touching_diagonals(void) {
    CombatUnit a = make_unit(2, 2, 0, 1);
    CombatUnit ne = make_unit(3, 1, 0, 1);
    CombatUnit se = make_unit(3, 3, 0, 1);
    ASSERT(unit_touching(&a, &ne));
    ASSERT(unit_touching(&a, &se));
    PASS();
}

TEST touching_same_cell(void) {
    CombatUnit a = make_unit(2, 2, 0, 1);
    CombatUnit b = make_unit(2, 2, 0, 1);
    ASSERT(unit_touching(&a, &b));
    PASS();
}

TEST touching_too_far(void) {
    CombatUnit a = make_unit(2, 2, 0, 1);
    CombatUnit far_e = make_unit(4, 2, 0, 1);   // 2 east
    CombatUnit far_d = make_unit(4, 4, 0, 1);   // 2,2
    ASSERT_FALSE(unit_touching(&a, &far_e));
    ASSERT_FALSE(unit_touching(&a, &far_d));
    PASS();
}

TEST cell_filter_any_passes_anything(void) {
    Combat c;
    memset(&c, 0, sizeof c);
    for (int s = 0; s < COMBAT_SIDES; s++) {
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            c.units[s][i].troop_idx = -1;
        }
    }
    // filter=0 (any tile in bounds) — treat as permissive.
    // Use cell coords (0..5, 0..4) which are valid combat board.
    ASSERT(combat_cell_passes_filter(&c, 0, 0, 0, /*filter=*/0));
    ASSERT(combat_cell_passes_filter(&c, 4, 4, 0, /*filter=*/0));
    PASS();
}

SUITE(combat_geom_suite) {
    RUN_TEST(touching_orthogonal_neighbors);
    RUN_TEST(touching_diagonals);
    RUN_TEST(touching_same_cell);
    RUN_TEST(touching_too_far);
    RUN_TEST(cell_filter_any_passes_anything);
}
