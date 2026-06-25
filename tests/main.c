// OpenBounty test runner. Layered into three categories by suite-name
// prefix (matches the source-directory layout):
//
//   tests/unit/        — single-function or small-scope state checks
//   tests/regression/  — pinned golden outputs (digests, save fixtures)
//   tests/e2e/         — multi-step flows exercising integration paths
//
// One binary, one greatest runner. Filter with -s:
//   ./build/openbounty-test -s unit_         # just unit
//   ./build/openbounty-test -s regression_   # just regression
//   ./build/openbounty-test -s e2e_          # just e2e

#include "greatest.h"

GREATEST_MAIN_DEFS();

// ---- unit ------------------------------------------------------------------
SUITE_EXTERN(unit_terrain_suite);
SUITE_EXTERN(unit_map_suite);
SUITE_EXTERN(unit_map_more_suite);
SUITE_EXTERN(unit_map_overlay_suite);
SUITE_EXTERN(unit_fog_suite);
SUITE_EXTERN(unit_tables_suite);
SUITE_EXTERN(unit_tables_defensive_suite);
SUITE_EXTERN(unit_state_suite);
SUITE_EXTERN(unit_state_json_suite);
SUITE_EXTERN(unit_player_io_suite);
SUITE_EXTERN(unit_resources_suite);
SUITE_EXTERN(unit_pack_suite);
SUITE_EXTERN(unit_combat_rng_suite);
SUITE_EXTERN(unit_combat_unit_suite);
SUITE_EXTERN(unit_combat_geom_suite);
SUITE_EXTERN(unit_combat_damage_suite);
SUITE_EXTERN(unit_combat_spells_suite);
SUITE_EXTERN(unit_combat_ai_suite);

// ---- regression ------------------------------------------------------------
SUITE_EXTERN(regression_combat_digests_suite);
SUITE_EXTERN(regression_save_fixture_suite);

// ---- e2e -------------------------------------------------------------------
SUITE_EXTERN(e2e_game_flow_suite);
SUITE_EXTERN(e2e_chest_suite);
SUITE_EXTERN(e2e_contract_suite);
SUITE_EXTERN(e2e_economy_suite);
SUITE_EXTERN(e2e_score_suite);
SUITE_EXTERN(e2e_combat_input_suite);
SUITE_EXTERN(e2e_save_suite);
SUITE_EXTERN(e2e_save_more_suite);

// ---- executor (flat helpers + primitives; isolation unit tests) ------------
SUITE_EXTERN(exec_move_suite);
SUITE_EXTERN(exec_misc_suite);
SUITE_EXTERN(exec_fight_suite);
SUITE_EXTERN(exec_recruit_suite);
SUITE_EXTERN(exec_town_suite);
SUITE_EXTERN(exec_prim_suite);
SUITE_EXTERN(autoplay_run_suite);   // end-to-end: planner() run + replay determinism

// (The old autoplay-LAYER integration suites — nav / combat_policy / plan / boot /
// chest / fly / intervention / mode_parity / baseline / travel_zone / scepter /
// worldsnap / prereq — are deleted; they exercised the collapsing nav/combat_policy/
// plan modules. The executor's own coverage is the exec_* suites above; broader
// autoplay integration tests get recreated against the flat executor later.)

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();

    // unit
    RUN_SUITE(unit_terrain_suite);
    RUN_SUITE(unit_map_suite);
    RUN_SUITE(unit_map_more_suite);
    RUN_SUITE(unit_map_overlay_suite);
    RUN_SUITE(unit_fog_suite);
    RUN_SUITE(unit_tables_suite);
    RUN_SUITE(unit_tables_defensive_suite);
    RUN_SUITE(unit_state_suite);
    RUN_SUITE(unit_state_json_suite);
    RUN_SUITE(unit_player_io_suite);
    RUN_SUITE(unit_resources_suite);
    RUN_SUITE(unit_pack_suite);
    RUN_SUITE(unit_combat_rng_suite);
    RUN_SUITE(unit_combat_unit_suite);
    RUN_SUITE(unit_combat_geom_suite);
    RUN_SUITE(unit_combat_damage_suite);
    RUN_SUITE(unit_combat_spells_suite);
    RUN_SUITE(unit_combat_ai_suite);

    // regression
    RUN_SUITE(regression_combat_digests_suite);
    RUN_SUITE(regression_save_fixture_suite);

    // e2e
    RUN_SUITE(e2e_game_flow_suite);
    RUN_SUITE(e2e_chest_suite);
    RUN_SUITE(e2e_contract_suite);
    RUN_SUITE(e2e_economy_suite);
    RUN_SUITE(e2e_score_suite);
    RUN_SUITE(e2e_combat_input_suite);
    RUN_SUITE(e2e_save_suite);
    RUN_SUITE(e2e_save_more_suite);

    // executor (flat helpers + primitives)
    RUN_SUITE(exec_move_suite);
    RUN_SUITE(exec_misc_suite);
    RUN_SUITE(exec_fight_suite);
    RUN_SUITE(exec_recruit_suite);
    RUN_SUITE(exec_town_suite);
    RUN_SUITE(exec_prim_suite);
    RUN_SUITE(autoplay_run_suite);

    GREATEST_MAIN_END();
}
