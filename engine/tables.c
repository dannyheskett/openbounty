#include "tables.h"
#include "resources.h"
#include <string.h>

// Catalog data (TROOPS, SPELLS, CLASSES, VILLAINS, ARTIFACTS) lives in
// assets/<game>/game.json and is loaded into the Resources struct by
// resources_load(). The lookup functions in this table module delegate
// through the resource singleton -- see resources.c for their
// implementations. Pure rules stay here.

void class_stats_at_rank(const ClassDef *c, int rank_index,
                         int *leadership, int *max_spells,
                         int *spell_power, int *commission) {
    if (!c) return;
    if (rank_index < 0) rank_index = 0;
    if (rank_index >= c->rank_count) rank_index = c->rank_count - 1;
    int l = 0, m = 0, p = 0, cm = 0;
    for (int i = 0; i <= rank_index; i++) {
        l  += c->ranks[i].leadership;
        m  += c->ranks[i].max_spells;
        p  += c->ranks[i].spell_power;
        cm += c->ranks[i].commission;
    }
    if (leadership)  *leadership  = l;
    if (max_spells)  *max_spells  = m;
    if (spell_power) *spell_power = p;
    if (commission)  *commission  = cm;
}

// ----- Adventure spell effects ----------------------------------------------
// THE id->effect table. GameApplyAdventureSpellEffect (spells_adventure.c) and
// the per-effect cast helpers dispatch on this classification, so pack spell ids
// bind to engine behavior in exactly one place. A pack id absent from this table
// is an adventure spell the engine cannot apply (ADV_EFFECT_NONE).
AdventureEffect spell_adventure_effect(int spell_idx) {
    const SpellDef *sp = spell_by_index(spell_idx);
    if (!sp || sp->kind != SPELL_KIND_ADVENTURE) return ADV_EFFECT_NONE;
    if (strcmp(sp->id, "bridge")        == 0) return ADV_EFFECT_BRIDGE;
    if (strcmp(sp->id, "time_stop")     == 0) return ADV_EFFECT_TIME_STOP;
    if (strcmp(sp->id, "find_villain")  == 0) return ADV_EFFECT_FIND_VILLAIN;
    if (strcmp(sp->id, "castle_gate")   == 0) return ADV_EFFECT_GATE_CASTLE;
    if (strcmp(sp->id, "town_gate")     == 0) return ADV_EFFECT_GATE_TOWN;
    if (strcmp(sp->id, "instant_army")  == 0) return ADV_EFFECT_INSTANT_ARMY;
    if (strcmp(sp->id, "raise_control") == 0) return ADV_EFFECT_RAISE_CONTROL;
    return ADV_EFFECT_NONE;
}

int spell_index_by_adventure_effect(AdventureEffect e) {
    if (e == ADV_EFFECT_NONE) return -1;
    // The effect->index map is constant for a given loaded catalog, but this
    // scan is a hot path in the autoplay mover (millions of calls per run, each
    // doing spell_adventure_effect strcmps). Memoize it and rebuild only when
    // the catalog identity changes (a different pack loaded). 16 > the number
    // of AdventureEffect values.
    static const SpellDef *s_cat0 = NULL;
    static int s_count = -1;
    static int s_map[16];
    const SpellDef *cat0 = spell_by_index(0);
    int cnt = spells_count();
    if (cat0 != s_cat0 || cnt != s_count) {
        for (int k = 0; k < 16; k++) s_map[k] = -1;
        for (int i = 0; i < cnt; i++) {
            AdventureEffect ae = spell_adventure_effect(i);
            if (ae > ADV_EFFECT_NONE && (int)ae < 16 && s_map[ae] < 0)
                s_map[ae] = i;
        }
        s_cat0 = cat0;
        s_count = cnt;
    }
    if ((int)e < 16) return s_map[e];
    for (int i = 0; i < cnt; i++)
        if (spell_adventure_effect(i) == e) return i;
    return -1;
}

// ----- Puzzle grid -----------------------------------------------------------
// The classic 5x5 puzzle layout: which entity covers each cell of the puzzle
// view (row-major). Non-negative = villain catalog index; negative = artifact
// index encoded as -(index)-1. THE one source: the shell's puzzle view draws
// from it and demo mode's scepter deduction reads it; each revealed cell (i,j)
// shows the scepter-zone map tile at (scepter.x-2+i, scepter.y-2+j), clamped
// to map bounds.
static const signed char PUZZLE_GRID[5][5] = {
    { -1,  7, -2,  6, -3 },
    {  5, 15, 14, 13,  4 },
    { -4, 12, 16, 11, -5 },
    {  3, 10,  9,  8,  2 },
    { -6,  1, -7,  0, -8 },
};

int puzzle_grid_entity(int row, int col) {
    if (row < 0 || row > 4 || col < 0 || col > 4) return 0;
    return PUZZLE_GRID[row][col];
}

// ----- Morale --------------------------------------------------------------
// Chart lives in game.json (combat.morale_chart) and is loaded into
// the Resources singleton; see resources.c parse_combat.
char morale_result(char my_group, char their_group) {
    int a = my_group    - 'A';
    int b = their_group - 'A';
    if (a < 0 || a > 4 || b < 0 || b > 4) return 'N';
    const Resources *r = resources_current();
    if (!r) return 'N';
    return r->morale_chart[a][b];
}
