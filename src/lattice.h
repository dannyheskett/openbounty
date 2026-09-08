// src/lattice.h
//
// The gold lattice: the shell's own chrome, drawn in code. A modern pack that
// ships no chrome bitmap gets its frame, the bar under the status line, the
// HUD panel borders and every window border (prompts, views, location menus)
// from here, so the one pattern separates the world from the HUD and each
// piece of the HUD from the rest.
//
// The pattern is a cross-hatch: two diagonals of gold on dark wood, bright at
// the crossings, one repeat every LATTICE_PITCH unit pixels. It is built once
// as a small texture at the pack's ui_scale and tiled from the screen origin,
// so bands and borders anywhere on the screen line up with one another.

#ifndef OB_LATTICE_H
#define OB_LATTICE_H

#define LATTICE_PITCH 8   // pattern repeat, in unit (ui_scale) pixels

// Fill the rect with the pattern and rail it: a one-unit gold line on every
// edge, a one-unit dark line inside the rails. A band thinner than four
// units is rails only.
void lattice_fill(int x, int y, int w, int h);

// A ring of the pattern inside the rect: bands `l`, `r`, `t`, `b` thick on
// the four sides, railed on the outer and the inner edge. The interior is
// not touched.
void lattice_ring(int x, int y, int w, int h, int l, int r, int t, int b);

// Release the pattern texture. Safe when nothing was built.
void lattice_shutdown(void);

#endif
