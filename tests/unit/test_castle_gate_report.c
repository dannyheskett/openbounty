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
    ASSERT(GameCastleGateReport(g, rc->id, report, sizeof report));
    ASSERT_STR_EQ(report, r->body);                // the report in its gate form
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
    ASSERT(GameCastleGateReport(g, rc->id, report, sizeof report));
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

static int count_of(const char *hay, const char *needle) {
    int n = 0;
    for (const char *p = hay; (p = strstr(p, needle)) != NULL; p += strlen(needle)) n++;
    return n;
}

// Under a title that names the castle, the gate opens with castle_gate_owner
// and never names it again; the town's informant still names it (#139).
TEST gate_report_does_not_repeat_the_castle_name(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    res->world.castle_gate_report = true;
    snprintf(res->banners.castle_gate_owner, sizeof res->banners.castle_gate_owner,
             "Under %%OWNER%%'s rule.\n\n");
    const ResCastle *rc = NULL;
    for (int i = 0; i < res->castle_count && !rc; i++)
        if (GameFindCastleConst(g, res->castles[i].id)) rc = &res->castles[i];
    ASSERT(rc && rc->name[0]);
    char gate[PLAYER_IO_BODY_CAP], town[PLAYER_IO_BODY_CAP];
    ASSERT(GameCastleGateReport(g, rc->id, gate, sizeof gate));
    ASSERT(GameCastleReport(g, rc->id, town, sizeof town));
    ASSERT_EQ(0, count_of(gate, rc->name));
    ASSERT_EQ(0, strncmp(gate, "Under ", 6));
    ASSERT(count_of(town, rc->name) >= 1);
    // A pack without the string keeps the full opening at the gate too.
    res->banners.castle_gate_owner[0] = '\0';
    ASSERT(GameCastleGateReport(g, rc->id, gate, sizeof gate));
    ASSERT_STR_EQ(town, gate);
    fx_free_game_full(res, g, m, f);
    PASS();
}

// Two stacks of one troop read as one line where the pack reports at the
// gate (#139); without the flag (King's Bounty) each slot keeps its line.
TEST same_troop_stacks_share_a_line(void) {
    Resources *res; Game *g; Map *m; Fog *f;
    ASSERT(fx_init_game_full(&res, &g, &m, &f, NULL, FIXTURE_SEED));
    CastleRecord *cr = NULL;
    for (int i = 0; i < g->castle_count && !cr; i++)
        if (g->castles[i].owner_kind == CASTLE_OWNER_MONSTERS) cr = &g->castles[i];
    ASSERT(cr);
    const TroopDef *t = troop_by_index(1);
    ASSERT(t);
    memset(cr->garrison, 0, sizeof cr->garrison);
    for (int s = 0; s < 2; s++) {
        ASSERT(snprintf(cr->garrison[s].id, sizeof cr->garrison[s].id, "%s", t->id)
               < (int)sizeof cr->garrison[s].id);
        cr->garrison[s].count = 3;
    }
    char report[PLAYER_IO_BODY_CAP];
    res->world.castle_gate_report = false;
    ASSERT(GameCastleReport(g, cr->id, report, sizeof report));
    ASSERT_EQ(2, count_of(report, t->name));
    res->world.castle_gate_report = true;
    ASSERT(GameCastleReport(g, cr->id, report, sizeof report));
    ASSERT_EQ(1, count_of(report, t->name));
    const char *label = GameNumberName(g, 6);      // the line counts both stacks
    if (label[0]) ASSERT(strstr(report, label));
    fx_free_game_full(res, g, m, f);
    PASS();
}

SUITE(unit_castle_gate_report_suite) {
    RUN_TEST(gate_without_the_flag_is_silent);
    RUN_TEST(gate_reports_a_monster_castle_without_siege_weapons);
    RUN_TEST(gate_puts_the_report_above_the_siege_question);
    RUN_TEST(gate_reports_a_villain_and_makes_the_castle_known);
    RUN_TEST(gate_report_does_not_repeat_the_castle_name);
    RUN_TEST(same_troop_stacks_share_a_line);
}
