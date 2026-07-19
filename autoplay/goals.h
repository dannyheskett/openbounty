// autoplay/goals.h
//
// The objective universe (AP-040..AP-042): one PlanStepSet covering every
// objective in the world for the pack + seed under play, with a single
// uniform per-kind completion predicate.

#ifndef OB_AUTOPLAY_GOALS_H
#define OB_AUTOPLAY_GOALS_H

#include <stdbool.h>

#include "game.h"
#include "map.h"

#define STEP_MAX 512

typedef enum {
    STEP_CHEST = 0,
    STEP_ARTIFACT,
    STEP_NAVMAP,
    STEP_ORB,
    STEP_ALCOVE,
    STEP_SIEGE_WEAPONS,    // prerequisite candidate only (AP-041), never enumerated
    STEP_MONSTER_CASTLE,
    STEP_VILLAIN,
    STEP_FOE,
    STEP_SCEPTER,
} PlanKind;

typedef struct {
    PlanKind kind;
    int      zone_index;   // index into res->zones
    int      x, y;         // target tile (gate tile for castles)
    char     handle[32];   // placement / castle / villain id ("" when n/a)
    char     label[48];    // display label for defers / verbose
} PlanStep;

typedef struct {
    PlanStep steps[STEP_MAX];
    int      count;
} PlanStepSet;

// Enumerate every objective (AP-040): consumables per zone map, monster and
// villain castles, hostile wandering foes, the scepter last. Loads each zone
// into the caller-supplied scratch map. Returns false when the scratch map or
// a zone fails to load.
bool plansteps_enumerate(const Game *g, Map *scratch, PlanStepSet *out);

// The uniform completion predicate (AP-042).
bool planstep_is_done(const Game *g, const PlanStep *step);

// Zone-scoped foe lookup (placement ids repeat across zones; AP-042 matches
// id + zone, never id alone).
const FoeState *plan_find_foe(const Game *g, const char *placement_id,
                              int zone_index);

const char *plan_kind_name(PlanKind k);

#endif
