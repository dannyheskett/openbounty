// src/modern/mlayout.c -- the five modern layouts (see mlayout.h).

#include "mlayout.h"
#include "layout.h"
#include "bfont.h"

// The margin every map panel keeps from the map pane's edges: the screen's
// own spacing (the band between the pane and the HUD), so a panel sits inside
// the pane with the same border the pane sits inside the screen with. A layout
// with no band falls back to the panel padding.
int ml_space(void) {
    return CL_SIDEBAR_GAP > 0 ? CL_SIDEBAR_GAP : ML_PAD;
}

// Small: six text lines plus padding, the pane's width less the margin, the
// margin above the pane's bottom edge. Sized from the font rather than the
// tile, so a message has room to breathe whatever face the pack ships.
static MlArea s_area = ML_AREA_MAP;

void ml_set_area(MlArea a) { s_area = a; }

static bool    s_has_field;
static ML_Rect s_field;

void ml_set_field(ML_Rect r) { s_field = r; s_has_field = true; }
void ml_clear_field(void)    { s_has_field = false; }
bool ml_field(ML_Rect *out) {
    if (s_has_field && out) *out = s_field;
    return s_has_field;
}

ML_Rect ml_area(void) {
    if (s_area == ML_AREA_FULL) return ml_full();
    // The DECLARED screen, never the grown one: a panel over a screen keeps
    // the size it was drawn for and centres (layout.h, REQ-528).
    if (s_area == ML_AREA_SCREEN)
        return (ML_Rect){ CL_SCREEN_BASE_X, CL_SCREEN_BASE_Y,
                          CL_SCREEN_BASE_W, CL_SCREEN_BASE_H };
    // A panel over the MAP anchors on the pane the player can see, not on the
    // declared rect centred inside it: the bottom box belongs on the foot of
    // the map as it always has, and a centred panel belongs in the middle of
    // it. The panels keep their declared sizes -- each clamps its own width
    // and height to what it was drawn for (ml_small below, uk_inlay, ml_large).
    return (ML_Rect){ CL_MAP_X, CL_MAP_Y, CL_MAP_W, CL_MAP_H };
}

ML_Rect ml_small(void) {
    int S = ml_space();
    ML_Rect a = ml_area();
    int h = ML_SMALL_LINES * BFONT_GLYPH_H + 2 * ML_PAD;
    // Never wider than the pane the pack declared, whatever the map has grown
    // to, and centred on the pane it sits on.
    int w = a.w - 2 * S;
    if (w > CL_PANE_BASE_W - 2 * S) w = CL_PANE_BASE_W - 2 * S;
    ML_Rect r = { a.x + (a.w - w) / 2, a.y + a.h - S - h, w, h };
    return r;
}

ML_Rect ml_large(void) {
    // Six by four tiles, never closer to the pane's edges than the margin.
    int S = ml_space();
    ML_Rect a = ml_area();
    int w = 6 * CL_TILE_W, h = 4 * CL_TILE_H;
    if (w > a.w - 2 * S) w = a.w - 2 * S;
    if (h > a.h - 2 * S) h = a.h - 2 * S;
    ML_Rect r = { a.x + (a.w - w) / 2, a.y + (a.h - h) / 2, w, h };
    return r;
}

// Full screen covers the pane, the band and the HUD edge to edge: it is a
// screen, not a panel on the map, and its views are laid out in whole tiles
// (Army is five rows of 96 filling the 480 exactly), which a margin would cut.
ML_Rect ml_full(void) {
    // Pane plus the band and the HUD, all at the sizes the pack declared:
    // 672 + 8 + 96 for Rome. The gap is whatever sits between the live pane
    // and the sidebar, which growth does not change.
    int gap = CL_SIDEBAR_X - (CL_MAP_X + CL_MAP_W);
    int w = CL_PANE_BASE_W + gap + CL_SIDEBAR_W;
    ML_Rect r = { CL_PANE_BASE_X, CL_PANE_BASE_Y, w, CL_PANE_BASE_H };
    return r;
}

// The smallest integer scale at which the backdrop covers the location
// panel's width. At 652 wide that is 3 (720): a whole-number scale keeps the
// pixel art square, and the overshoot is cropped evenly off the two sides.
int ml_loc_scale(void) {
    int w = CL_PANE_BASE_W - 2 * ml_space();
    int s = (w + ML_BACKDROP_W - 1) / ML_BACKDROP_W;
    return s < 1 ? 1 : s;
}

ML_Rect ml_loc_backdrop(void) {
    int S = ml_space();
    int h = ML_BACKDROP_H * ml_loc_scale();
    if (h > CL_PANE_BASE_H - 2 * S) h = CL_PANE_BASE_H - 2 * S;
    ML_Rect r = { CL_PANE_BASE_X + S, CL_PANE_BASE_Y + S, CL_PANE_BASE_W - 2 * S, h };
    return r;
}

// Directly under the backdrop, sharing its edge, down to the margin above the
// pane's bottom.
ML_Rect ml_loc_text(void) {
    int S = ml_space();
    ML_Rect b = ml_loc_backdrop();
    ML_Rect r = { b.x, b.y + b.h, b.w, CL_PANE_BASE_Y + CL_PANE_BASE_H - S - (b.y + b.h) };
    return r;
}

int ml_row_h(void) {
    int h = CL_TILE_H / 2;
    int min = BFONT_GLYPH_H + ML_PAD;
    return h < min ? min : h;
}

int ml_cols(ML_Rect r) {
    int gw = BFONT_GLYPH_W;
    return gw > 0 ? (r.w - 2 * ML_PAD) / gw : 0;
}

int ml_lines(ML_Rect r) {
    int gh = BFONT_GLYPH_H;
    return gh > 0 ? (r.h - 2 * ML_PAD) / gh : 0;
}
