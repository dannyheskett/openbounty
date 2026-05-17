// OpenBounty unit-test runner. Each suite is defined in its own
// tests/unit/test_*.c file; main.c just imports and runs them all.

#include "greatest.h"

GREATEST_MAIN_DEFS();

SUITE_EXTERN(terrain_suite);
SUITE_EXTERN(map_suite);
SUITE_EXTERN(chest_suite);
SUITE_EXTERN(save_suite);
SUITE_EXTERN(tables_suite);
SUITE_EXTERN(state_suite);

SUITE_EXTERN(combat_rng_suite);
SUITE_EXTERN(combat_unit_suite);
SUITE_EXTERN(combat_geom_suite);
SUITE_EXTERN(combat_damage_suite);
SUITE_EXTERN(combat_spells_suite);

SUITE_EXTERN(resources_suite);
SUITE_EXTERN(save_more_suite);
SUITE_EXTERN(map_more_suite);
SUITE_EXTERN(score_suite);
SUITE_EXTERN(game_flow_suite);
SUITE_EXTERN(combat_ai_suite);
SUITE_EXTERN(save_fixture_suite);
SUITE_EXTERN(contract_suite);
SUITE_EXTERN(economy_suite);
SUITE_EXTERN(fog_suite);
SUITE_EXTERN(map_overlay_suite);
SUITE_EXTERN(state_json_suite);
SUITE_EXTERN(tables_defensive_suite);
SUITE_EXTERN(combat_input_suite);
SUITE_EXTERN(pack_suite);
SUITE_EXTERN(combat_digests_suite);

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(terrain_suite);
    RUN_SUITE(map_suite);
    RUN_SUITE(chest_suite);
    RUN_SUITE(save_suite);
    RUN_SUITE(tables_suite);
    RUN_SUITE(state_suite);
    RUN_SUITE(combat_rng_suite);
    RUN_SUITE(combat_unit_suite);
    RUN_SUITE(combat_geom_suite);
    RUN_SUITE(combat_damage_suite);
    RUN_SUITE(combat_spells_suite);
    RUN_SUITE(resources_suite);
    RUN_SUITE(save_more_suite);
    RUN_SUITE(map_more_suite);
    RUN_SUITE(score_suite);
    RUN_SUITE(game_flow_suite);
    RUN_SUITE(combat_ai_suite);
    RUN_SUITE(save_fixture_suite);
    RUN_SUITE(contract_suite);
    RUN_SUITE(economy_suite);
    RUN_SUITE(fog_suite);
    RUN_SUITE(map_overlay_suite);
    RUN_SUITE(state_json_suite);
    RUN_SUITE(tables_defensive_suite);
    RUN_SUITE(combat_input_suite);
    RUN_SUITE(pack_suite);
    RUN_SUITE(combat_digests_suite);
    GREATEST_MAIN_END();
}
