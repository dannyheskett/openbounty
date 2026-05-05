#include "combat_render.h"
#include "tables.h"
#include "bfont.h"
#include "palette.h"
#include "layout.h"
#include "chrome.h"
#include "raylib.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// Combat renderer (Phase 8 v2). Uses  combat tileset
// directly:
//   s->combat_tile[0]      grass field background
//   s->combat_tile[1..3]   random obstacles (boulder, tree, mound)
//   s->combat_tile[4]      castle ornament
//   s->combat_tile[5..10]  castle wall pieces (codes from
//                           castle_omap)
//   s->combat_tile[11..14] cursor sprites (rings)
//
// Layout (320x200 design space, chrome y=22..191):
//   - Combat occupies the full inner width 288 (16..303) so cells
//     are 48x34. No sidebar in combat mode (per kb_074/kb_075/kb_078).
//   - Title bar text rendered into the existing chrome status
//     strip via chrome_draw_with_status.
//
// Geometry verified against:
//   - kb_074 (castle siege)
//   - kb_075 (open field with foe)
//   - kb_078 (post-first-kill mode title bar)
//   - kb_080 (in-combat spells modal — Phase 12 path)
//   - kb_038 (in-combat controls overlay — Phase 13 path)
//   - kb_066 (victory dialog — Phase 11 path)

// ----- Title bar -------------------------------------------------------------

void combat_format_title(const Combat *c, const Game *g, char *buf, int cap) {
    if (!buf || cap <= 0) return;
    // DOS reference (kb_036, kb_040, kb_044): action banners replace the
    // "Options / ..." title bar text directly — there is no separate
    // banner strip at the bottom of the field. When a banner is set,
    // emit it as the title; the next turn's reset clears it.
    if (c->banner[0]) {
        // Copy verbatim — banner already fits COMBAT_BANNER_LEN; adding
        // a leading " " would risk overflowing the title buffer (same
        // size as banner). DOS title-bar text starts at the same x as
        // the banner's first char, so no padding is needed.
        snprintf(buf, cap, "%s", c->banner);
        (void)g;
        return;
    }
    if (!c->first_kill_seen) {
        const char *name = "—";
        int moves = 0;
        int shots = 0;
        if (c->unit_id >= 0) {
            const CombatUnit *u = &c->units[c->side][c->unit_id];
            const TroopDef *t = troop_by_index(u->troop_idx);
            if (t && t->name[0]) name = t->name;
            moves = u->moves;
            shots = u->shots;
        }
        // DOS reference (kb_030, kb_043) appends ",Sn" only when the
        // active unit has remaining shots; melee-only stacks show "Mn"
        // alone (kb_029).
        if (shots > 0) {
            snprintf(buf, cap, " Options / %s M%d,S%d", name, moves, shots);
        } else {
            snprintf(buf, cap, " Options / %s M%d", name, moves);
        }
    } else {
        const char *p_name = "Army";
        const char *f_name = "Foe";
        int pmax = 0, fmax = 0;
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            const CombatUnit *up = &c->units[COMBAT_SIDE_PLAYER][i];
            if (up->count > pmax) {
                pmax = up->count;
                const TroopDef *t = troop_by_index(up->troop_idx);
                if (t && t->name[0]) p_name = t->name;
            }
            const CombatUnit *uf = &c->units[COMBAT_SIDE_AI][i];
            if (uf->count > fmax) {
                fmax = uf->count;
                const TroopDef *t = troop_by_index(uf->troop_idx);
                if (t && t->name[0]) f_name = t->name;
            }
        }
        snprintf(buf, cap, " %s vs %s killing %d",
                 p_name, f_name, c->stacks_destroyed);
    }
    (void)g;
}

// ----- Log -------------------------------------------------------------------

void combat_log(Combat *c, const char *fmt, ...) {
    if (!c) return;
    char line[COMBAT_LOG_LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (c->log_count < COMBAT_LOG_LINES) {
        snprintf(c->log_lines[c->log_count], COMBAT_LOG_LINE_LEN, "%s", line);
        c->log_count++;
    } else {
        for (int i = 1; i < COMBAT_LOG_LINES; i++) {
            memcpy(c->log_lines[i - 1], c->log_lines[i], COMBAT_LOG_LINE_LEN);
        }
        snprintf(c->log_lines[COMBAT_LOG_LINES - 1], COMBAT_LOG_LINE_LEN,
                 "%s", line);
    }
    snprintf(c->banner, COMBAT_BANNER_LEN, "%s", line);
}

void combat_log_template(Combat *c, const char *template_str,
                         const ResTemplateVar *vars, int nvars) {
    if (!c || !template_str || !template_str[0]) return;
    char line[COMBAT_LOG_LINE_LEN];
    resources_format_template(line, sizeof line, template_str, vars, nvars);
    if (c->log_count < COMBAT_LOG_LINES) {
        snprintf(c->log_lines[c->log_count], COMBAT_LOG_LINE_LEN, "%s", line);
        c->log_count++;
    } else {
        for (int i = 1; i < COMBAT_LOG_LINES; i++) {
            memcpy(c->log_lines[i - 1], c->log_lines[i], COMBAT_LOG_LINE_LEN);
        }
        snprintf(c->log_lines[COMBAT_LOG_LINES - 1], COMBAT_LOG_LINE_LEN,
                 "%s", line);
    }
    snprintf(c->banner, COMBAT_BANNER_LEN, "%s", line);
}

// ----- Cell math -------------------------------------------------------------

static void cell_origin(int gx, int gy, int *px, int *py) {
    *px = CL_COMBAT_X + gx * CL_COMBAT_CELL_W;
    *py = CL_COMBAT_Y + gy * CL_COMBAT_CELL_H;
}

// ----- Tile draw -------------------------------------------------------------

static void draw_tile(const Sprites *s, int idx, int px, int py) {
    if (idx < 0 || idx >= 15) return;
    Texture2D t = s->combat_tile[idx];
    if (t.id == 0) return;
    Rectangle src = { 0, 0, (float)t.width, (float)t.height };
    Rectangle dst = { (float)px, (float)py, (float)t.width, (float)t.height };
    DrawTexturePro(t, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
}

// ----- Unit + count badge ----------------------------------------------------

static void draw_unit(const CombatUnit *u, int side,
                      const Sprites *sprites) {
    if (u->troop_idx < 0 || u->count == 0) return;
    int px, py;
    cell_origin(u->x, u->y, &px, &py);
    Texture2D tex = sprites->troop_anim[u->troop_idx][u->frame & 3];
    if (tex.id == 0) tex = sprites->troop_sprite[u->troop_idx];
    if (tex.id != 0) {
        // Sprites face right by default. AI side faces left → mirror via
        // negative source width. Sprite cell pitch is 48x34, matching
        // unit-sprite native size, so they fill the cell exactly.
        Rectangle src = { 0, 0, (float)tex.width, (float)tex.height };
        if (side == COMBAT_SIDE_AI) src.width = -src.width;
        Rectangle dst = { (float)px, (float)py,
                          (float)tex.width, (float)tex.height };
        DrawTexturePro(tex, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
    }
    // Count badge: white digits on black band, centered horizontally,
    // anchored at the bottom of the cell — matches kb_074 / kb_075 /
    // kb_078 placement.
    char buf[16];
    snprintf(buf, sizeof buf, "%d", u->count);
    Vector2 m = bfont_measure(buf);
    int bx = px + (CL_COMBAT_CELL_W - (int)m.x) / 2;
    int by = py + CL_COMBAT_CELL_H - BFONT_GLYPH_H - 1;
    DrawRectangle(bx - 1, by - 1, (int)m.x + 2, BFONT_GLYPH_H + 2,
                  PAL_CLR(BLACK));
    bfont_draw(buf, bx, by, PAL_CLR(WHITE));
}

// ----- Field draw ------------------------------------------------------------

void combat_render_frame(const Combat *c, const Game *g,
                         const Sprites *sprites) {
    // Full-screen black so any letterbox area outside the chrome stays
    // dark; the chrome bitmap composites the frame on top, and the
    // combat field sits inside the inner area.
    DrawRectangle(0, 0, CL_SCREEN_W, CL_SCREEN_H, PAL_CLR(BLACK));

    // Tile the field with frame_00 (grass background). One tile per
    // cell; the tileset's grass colour matches the original field
    // green so this also serves as the open-field backdrop.
    for (int y = 0; y < COMBAT_H; y++) {
        for (int x = 0; x < COMBAT_W; x++) {
            int px, py;
            cell_origin(x, y, &px, &py);
            draw_tile(sprites, 0, px, py);
        }
    }

    // Stamp obstacles. omap codes:
    //   1, 2, 3      → field obstacles (frames 1, 2, 3)
    //   5..10        → castle wall pieces (frames 5..10)
    // Codes match  random obstacle generation +
    //  castle_omap exactly so castle siege walls render in the
    // same positions as the spec describes.
    for (int y = 0; y < COMBAT_H; y++) {
        for (int x = 0; x < COMBAT_W; x++) {
            unsigned char code = c->omap[y][x];
            if (!code) continue;
            int px, py;
            cell_origin(x, y, &px, &py);
            draw_tile(sprites, code, px, py);
        }
    }

    // Units (over obstacles since they stand on the field). AI faces
    // left, player faces right — see draw_unit.
    for (int s = 0; s < COMBAT_SIDES; s++) {
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            draw_unit(&c->units[s][i], s, sprites);
        }
    }

    // Damage burst (comtile frame 4) over any unit with hit_flash > 0.
    // Mirrors . Painted after units
    // so the splat sits on top.
    for (int s = 0; s < COMBAT_SIDES; s++) {
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            const CombatUnit *u = &c->units[s][i];
            if (u->troop_idx < 0 || u->hit_flash <= 0) continue;
            int px, py;
            cell_origin(u->x, u->y, &px, &py);
            draw_tile(sprites, 4, px, py);
        }
    }

    // Target cursor: frames 11..14 are a single 4-frame ring animation
    // used by pick_target only. pick_target draws (frame+11) at
    // the cursor cell (game.c:5621-5630). DOS does NOT draw a ring
    // around the active unit during normal play (kb_029-044) — the
    // unit's own sprite-frame cycling indicates whose turn it is.
    if (c->picker_active) {
        int idx = 11 + (c->cursor_frame & 3);
        int px, py;
        cell_origin(c->cursor_x, c->cursor_y, &px, &py);
        draw_tile(sprites, idx, px, py);
    }

    // Title bar via the chrome path. classic_chrome_draw_with_
    // status paints the border, status fill, bar strip, and our title
    // text — same chrome adventure mode uses, so combat sits inside
    // the same yellow frame visible in kb_074 / kb_075 / kb_078.
    char title[COMBAT_BANNER_LEN];
    combat_format_title(c, g, title, sizeof title);
    chrome_draw_with_status(g, sprites, title);

    // No bottom-of-field banner — DOS routes action banners through the
    // title bar (combat_format_title above). The unit log buffer remains
    // available for a future combat-log scroll panel.
}
