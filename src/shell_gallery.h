// src/shell_gallery.h -- --gallery <dir>: capture every modern screen to PNG
// (a layout audit tool; see shell_gallery.c).

#ifndef OB_SHELL_GALLERY_H
#define OB_SHELL_GALLERY_H

#include "game.h"
#include "map.h"
#include "fog.h"
#include "resources.h"
#include "sprites.h"
#include "gfx.h"

#if !defined(PLATFORM_IOS)

int gallery_run(Game *g, Map *m, Fog *f, const Resources *res, const Sprites *s,
                RenderTexture2D *rt, const char *dir);

#else

// The gallery is a desktop layout-audit tool (it writes a PNG per screen), so
// src/shell_gallery.c is not in the iOS build. main.c reaches this only from a
// command-line flag, and iOS has no command line.
static inline int gallery_run(Game *g, Map *m, Fog *f, const Resources *res,
                              const Sprites *s, RenderTexture2D *rt,
                              const char *dir) {
    (void)g; (void)m; (void)f; (void)res; (void)s; (void)rt; (void)dir;
    return 2;
}

#endif

#endif
