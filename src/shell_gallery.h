// src/shell_gallery.h -- --gallery <dir>: capture every modern screen to PNG
// (a layout audit tool; see shell_gallery.c).

#ifndef OB_SHELL_GALLERY_H
#define OB_SHELL_GALLERY_H

#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"
#include "sprites.h"
#include "raylib.h"

int gallery_run(Game *g, Map *m, Fog *f, const Resources *res, const Sprites *s,
                RenderTexture2D *rt, const char *dir);

#endif
