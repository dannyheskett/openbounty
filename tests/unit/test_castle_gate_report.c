// A castle gate reports on its castle in a pack that sets
// world.castle_gate_report (#71): a message box without siege weapons, the
// report above the siege question with them. Without the flag (the fixture
// pack, as King's Bounty) the gate bounces the hero back in silence.

#include "greatest.h"
#include "fixtures.h"
#include "game.h"
#include "player_io.h"
#include "resources.h"
#include "step.h"
#include "tables.h"

#include <stdio.h>
#include <string.h>

// Stand the hero on the gate tile of the first castle of `kind` in the hero's
// zone and step onto the castle. Returns the castle, or NULL if none fits.
static const ResCastle *step_to_castle(Resources *res, Game *g, Map *m, Fog *f,
                                       CastleOwnerKind kind) {
    for (int i = 0; i < res->castle_count; i++) {
        const ResCastle *rc = &res->castles[i];
        const CastleRecord *cr = GameFindCastleConst(g, rc->id);
        if (!cr || cr->owner_kind != kind) continue;
        if (strcmp(rc->zone, g->position.zone) != 0) continue;
        if (rc->gate_x != rc->x || rc->gate_y != rc->y + 1) continue;
        g->position.x = g->position.last_x = rc->gate_x;
        g->position.y = g->position.last_y = rc->gate_y;
        player_io_reset(g);
        GameStep(g, m, f, res, 0, -1);
        return rc;
    }
    return NULL;
}

TEST gate_without_the_flag_is_silent(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    ASSERT_FALSE(res->world.castle_gate_report);   // the fixture pack declares none
    g->stats.siege_weapons = false;
    const ResCastle *rc = step_to_castle(res, g, m, f, CASTLE_OWNER_MONSTERS);
    ASSERT(rc);
    ASSERT(player_io_idle(g));
    ASSERT_EQ(rc->gate_y, g->position.y);         // bounced back to the gate
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST gate_reports_a_monster_castle_without_siege_weapons(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    res->world.castle_gate_report = true;
    g->stats.siege_weapons = false;
    const ResCastle *rc = step_to_castle(res, g, m, f, CASTLE_OWNER_MONSTERS);
    ASSERT(rc);
    const PlayerRequest *r = player_io_front(g);
    ASSERT(r);
    ASSERT_EQ(REQ_MESSAGE, r->role);
    char want[128];
    ResTemplateVar vars[] = { { "NAME", rc->name } };
    resources_format_template(want, sizeof want, res->banners.castle_header, vars, 1);
    ASSERT_STR_EQ(want, r->header);
    char report[PLAYER_IO_BODY_CAP];
    ASSERT(GameCastleReport(g, rc->id, report, sizeof report));
    ASSERT_STR_EQ(report, r->body);                // the town's report, word for word
    ASSERT(strstr(r->body, res->banners.town_intel_owner_none));
    const CastleRecord *cr = GameFindCastleConst(g, rc->id);
    const TroopDef *t = troop_by_id(cr->garrison[0].id);
    ASSERT(t && strstr(r->body, t->name));        // its troops, in vague words
    ASSERT_EQ(rc->gate_y, g->position.y);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST gate_puts_the_report_above_the_siege_question(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    res->world.castle_gate_report = true;
    snprintf(res->banners.castle_siege_ask, sizeof res->banners.castle_siege_ask, "Lay siege?");
    g->stats.siege_weapons = true;
    const ResCastle *rc = step_to_castle(res, g, m, f, CASTLE_OWNER_MONSTERS);
    ASSERT(rc);
    const PlayerRequest *r = player_io_front(g);
    ASSERT(r);
    ASSERT_EQ(REQ_DECISION, r->role);
    ASSERT_EQ(FLOW_SIEGE_MONSTER, r->flow);
    char report[PLAYER_IO_BODY_CAP], want[PLAYER_IO_BODY_CAP + 16];
    ASSERT(GameCastleReport(g, rc->id, report, sizeof report));
    snprintf(want, sizeof want, "%s\nLay siege?", report);
    ASSERT_STR_EQ(want, r->body);
    fx_free_game_full(res, g, m, f);
    PASS();
}

TEST gate_reports_a_villain_and_makes_the_castle_known(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    res->world.castle_gate_report = true;
    g->stats.siege_weapons = false;
    const ResCastle *rc = step_to_castle(res, g, m, f, CASTLE_OWNER_VILLAIN);
    ASSERT(rc);
    const CastleRecord *cr = GameFindCastleConst(g, rc->id);
    ASSERT(cr->known);                            // contract scenes name it from now on
    const PlayerRequest *r = player_io_front(g);
    ASSERT(r);
    const VillainDef *v = villain_by_id(cr->villain_id);
    ASSERT(v && strstr(r->body, v->name));
    fx_free_game_full(res, g, m, f);
    PASS();
}

SUITE(unit_castle_gate_report_suite) {
    RUN_TEST(gate_without_the_flag_is_silent);
    RUN_TEST(gate_reports_a_monster_castle_without_siege_weapons);
    RUN_TEST(gate_puts_the_report_above_the_siege_question);
    RUN_TEST(gate_reports_a_villain_and_makes_the_castle_known);
}
