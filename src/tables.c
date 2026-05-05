#include "tables.h"
#include "resources.h"
#include <string.h>

// Catalog data (TROOPS, SPELLS, CLASSES, VILLAINS, ARTIFACTS) lives in
// assets/<game>/game.json and is loaded into the Resources struct by
// resources_load(). The lookup functions in this table module delegate
// through the resource singleton — see resources.c for their
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
