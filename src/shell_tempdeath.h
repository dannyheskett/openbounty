// src/shell_tempdeath.h
//
// perform_temp_death(): combat-defeat / last-army-dismissal handler.
// Teleports the hero home, wipes army, removes siege weapons, grants
// 20 peasants, dismounts. Opens the "King dismisses you" dialog.

#ifndef OB_SHELL_TEMPDEATH_H
#define OB_SHELL_TEMPDEATH_H

#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"

void perform_temp_death(Game *g, Map *map, Fog *fog, const Resources *res);

#endif
