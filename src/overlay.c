#include "overlay.h"
#include "layout.h"
#include "palette.h"
#include "views.h"
#include "prompt.h"
#include "screens/home_castle.h"
#include "screens/recruit_soldiers.h"
#include "screens/own_castle.h"
#include "screens/dwelling.h"
#include "screens/alcove.h"
#include "screens/end_game.h"
#include "bfont.h"
#include "ui.h"
#include "views_render.h"
#include <stdio.h>
#include <string.h>

// Width of a glyph + 1px spacing between glyphs . Bitmap font is
// fixed-pitch 8x8 with no extra spacing so chars-per-line at width W is
// W / BFONT_GLYPH_W.
#define GW  BFONT_GLYPH_W
#define GH  BFONT_GLYPH_H

// ---------------------------------------------------------------------------
// Bottom message frame: a solid black strip covering the map viewport from
// the status bar downward, plus a 1px yellow frame. This rendering reserves the
// entire bottom portion of the screen for long messages (KB_BottomBox); we
// draw into the map viewport because that's the biggest available area.
// ---------------------------------------------------------------------------

static void draw_panel(int x, int y, int w, int h, Color bg) {
    // Rounded blue panel with yellow border .
    Rectangle r = { (float)x, (float)y, (float)w, (float)h };
    float roundness = 0.05f;
    int segments = 6;
    DrawRectangleRounded(r, roundness, segments, bg);
    DrawRectangleRoundedLines(r, roundness, segments, PAL_CLR(YELLOW));
}

// Word-wrap helper. Breaks `text` into lines of at most `max_chars`
// chars each, writing one line per call and advancing `*p`. Returns the
// number of chars consumed (0 when done).
static int consume_line(const char **p, int max_chars,
                        char *out, int out_sz) {
    int n = 0;
    // Skip leading spaces/tabs (but not newlines — they're significant).
    while (**p == ' ' || **p == '\t') (*p)++;
    // Copy until newline, end, or max_chars hit.
    while (**p && **p != '\n' && n + 1 < out_sz && n < max_chars) {
        out[n++] = **p; (*p)++;
    }
    // If we stopped mid-word, back up to last space so we break at word.
    if (**p && **p != '\n' && n >= max_chars) {
        int back = n;
        while (back > 0 && out[back - 1] != ' ') back--;
        if (back > 0) {
            // Rewind source pointer by (n - back) and truncate output.
            int over = n - back;
            *p -= over;
            n = back;
        }
    }
    out[n] = '\0';
    if (**p == '\n') (*p)++;
    return n + 1;  // consumed at least newline or text
}

// ---------------------------------------------------------------------------
// Dialog (bottom message framestyle).
// ---------------------------------------------------------------------------


// Dialog draw modes:
//   BOTTOM        — bottom box (CL_PANEL_*, 30 cols × ~10 rows).
//                   Sits over the bottom frame so adventure-mode sidebar
//                   stays visible.
//   CENTERED_MODAL — victory layout: a 36-col × 16-row
//                   border, centered on screen (RECT_Text(16,36) +
//                   RECT_Center). Used for the victory banner so the
//                   dialog floats over the still-rendered battlefield
//                   instead of replacing the bottom frame.
typedef enum {
    DLG_MODE_BOTTOM = 0,
    DLG_MODE_CENTERED_MODAL,
} DialogMode;

static void draw_dialog_ex(DialogMode mode);
static void draw_dialog(void) { draw_dialog_ex(DLG_MODE_BOTTOM); }

void overlay_draw_dialog(void)          { draw_dialog_ex(DLG_MODE_BOTTOM); }
void overlay_draw_dialog_centered(void) { draw_dialog_ex(DLG_MODE_CENTERED_MODAL); }

static void draw_dialog_ex(DialogMode mode) {
    const char *hdr = dialog_header_text();
    const char *body = dialog_body_text();

    int pad = 4;
    int x, y, w, h, body_lines;

    if (mode == DLG_MODE_CENTERED_MODAL) {
        // Victory layout: 36 cols × 16 rows
        // of glyphs, centered on the 320×200 screen.
        w = 36 * GW;
        h = 16 * GH;
        x = (CL_SCREEN_W - w) / 2;
        y = (CL_SCREEN_H - h) / 2;
        body_lines = 14;  // leaves room for header + spinner
    } else {
        // Bottom-frame rectangle, same as every
        // persistent menu (home castle, own castle, dwelling, …).
        x = CL_PANEL_X;
        y = CL_PANEL_Y;
        w = CL_PANEL_W;
        h = CL_PANEL_H;
        body_lines = 7;
    }

    // Count header rows (newline-separated).
    int header_rows = 0;
    if (hdr && hdr[0]) {
        header_rows = 1;
        for (const char *p = hdr; *p; p++) if (*p == '\n') header_rows++;
    }

    draw_panel(x, y, w, h, PAL_CLR(DBLUE));

    int tx = x + pad;
    int ty = y + pad;
    int max_chars = (w - 2 * pad) / GW;

    if (header_rows) {
        const char *hp = hdr;
        char hline[128];
        for (int i = 0; i < header_rows; i++) {
            consume_line(&hp, max_chars, hline, (int)sizeof(hline));
            if (mode == DLG_MODE_CENTERED_MODAL) {
                bfont_draw_centered(hline, x + w / 2, ty, PAL_CLR(YELLOW));
            } else {
                bfont_draw(hline, tx, ty, PAL_CLR(YELLOW));
            }
            ty += GH;
        }
        ty += (mode == DLG_MODE_CENTERED_MODAL) ? GH : 2;  // gap
    }

    // Body: word-wrap to panel width; skip to current page and draw body_lines rows.
    const char *p = body ? body : "";
    int current_page = dialog_page_current();
    int lines_skipped = 0;
    char line[128];

    while (*p && lines_skipped < current_page * body_lines) {
        int got = consume_line(&p, max_chars, line, (int)sizeof(line));
        if (got <= 0) break;
        lines_skipped++;
    }

    int lines_drawn = 0;
    while (*p && lines_drawn < body_lines) {
        int got = consume_line(&p, max_chars,
                               line, (int)sizeof(line));
        if (got <= 0) break;
        bfont_draw(line, tx, ty, PAL_CLR(WHITE));
        ty += GH;
        lines_drawn++;
    }

}

// ---------------------------------------------------------------------------
// Game menu (nested, cursor-driven).
// ---------------------------------------------------------------------------

static void draw_menu(void) {
    const char *title = views_menu_title();
    int count = views_menu_entry_count();
    int cursor = views_menu_cursor();

    // Size the panel to the menu content.
    int row_h = GH + 2;
    int w = 160;
    int h = (count + 2) * row_h + 8;   // title + entries + hint
    int x = CL_MAP_X + (CL_MAP_W - w) / 2;
    int y = CL_MAP_Y + (CL_MAP_H - h) / 2;

    draw_panel(x, y, w, h, PAL_CLR(DBLUE));

    int tx = x + 6;
    int ty = y + 4;
    if (title) {
        bfont_draw_centered(title, x + w / 2, ty, PAL_CLR(YELLOW));
        ty += row_h + 2;
    }

    for (int i = 0; i < count; i++) {
        const char *label = views_menu_entry_label(i);
        if (!label) continue;
        bool is_sub = views_menu_entry_is_submenu(i);
        bool sel = (i == cursor);

        Color fg = sel ? PAL_CLR(YELLOW) : PAL_CLR(WHITE);
        char buf[64];
        if (is_sub) snprintf(buf, sizeof(buf), "%s >", label);
        else        snprintf(buf, sizeof(buf), "%s", label);

        if (sel) bfont_draw(">", tx, ty, PAL_CLR(YELLOW));
        bfont_draw(buf, tx + GW + 4, ty, fg);
        ty += row_h;
    }
}

// ---------------------------------------------------------------------------
// Town menu (bottom frame, A..E letter rows).
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Location-backdrop renderer . Picks one of 6 backdrop
// PNGs and animates a troop sprite at the bottom-left, replacing the world
// map area. Used by every "location screen" view (TOWN, HOME_CASTLE,
// OWN_CASTLE, DWELLING, ALCOVE, RECRUIT_SOLDIERS).
//
// The backdrop is 240x102; we draw at the map area's top-left (16,22). The
// troop sprite is inset one sprite-width and pinned to the bottom of the
// backdrop, drawn beneath the bottom panel.
// ---------------------------------------------------------------------------

typedef enum {
    LOC_NONE = 0,
    LOC_CASTLE,
    LOC_TOWN,
    LOC_PLAINS,
    LOC_FOREST,
    LOC_HILLCAVE,
    LOC_DUNGEON,
} LocKind;

static Texture2D loc_texture(const Sprites *s, LocKind kind) {
    if (!s) return (Texture2D){ 0 };
    switch (kind) {
        case LOC_CASTLE:   return s->castle_backdrop;
        case LOC_TOWN:     return s->town_backdrop;
        case LOC_PLAINS:   return s->plains_backdrop;
        case LOC_FOREST:   return s->forest_backdrop;
        case LOC_HILLCAVE: return s->hillcave_backdrop;
        case LOC_DUNGEON:  return s->dungeon_backdrop;
        case LOC_NONE: default: return (Texture2D){ 0 };
    }
}

// Draw the backdrop + animated troop. troop_idx is a Game troop index
// (0..24); troop_frame is the 0..3 animation frame the screen owns.
// draw_location takes (loc_id, troop_id, frame) — the screen
// advances `frame` from SYN ticks. We mirror that contract.
//
// Exposed via screens_draw_location_backdrop() below for screen modules.
static void draw_location_backdrop(const Game *g, const Sprites *s,
                                   LocKind kind, int troop_idx,
                                   int troop_frame) {
    (void)g;
    int bd_x = CL_MAP_X;
    int bd_y = CL_MAP_Y;
    int bd_w = 240;
    int bd_h = 102;
    Texture2D bd = loc_texture(s, kind);
    if (bd.id && bd.width > 0) {
        Rectangle src = { 0, 0, (float)bd.width, (float)bd.height };
        Rectangle dst = { (float)bd_x, (float)bd_y,
                          (float)bd_w, (float)bd_h };
        DrawTexturePro(bd, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
    } else {
        DrawRectangle(bd_x, bd_y, bd_w, bd_h, PAL_CLR(BLACK));
    }

    // Animated troop sprite: 4-frame strip pinned to bottom-left, inset
    // one sprite-width (x = troop_w * 1). Lifted a few pixels off the backdrop
    // bottom so it clears the menu/dialog panel drawn just below (otherwise the
    // sprite's feet overlap the panel's top border).
    enum { TROOP_BOTTOM_LIFT = 4 };
    if (s && troop_idx >= 0 && troop_idx < 25) {
        int frame = troop_frame & 3;
        Texture2D ts = s->troop_anim[troop_idx][frame];
        if (!ts.id) ts = s->troop_sprite[troop_idx];
        if (ts.id && ts.width > 0) {
            int tw = ts.width;
            int th = ts.height;
            Rectangle tsrc = { 0, 0, (float)tw, (float)th };
            Rectangle tdst = { (float)(bd_x + tw),
                               (float)(bd_y + bd_h - th - TROOP_BOTTOM_LIFT),
                               (float)tw, (float)th };
            DrawTexturePro(ts, tsrc, tdst,
                           (Vector2){ 0, 0 }, 0.0f, WHITE);
        }
    }

    // Black fill below the backdrop to clear any residual map-area pixels
    // before the bottom panel is drawn over them.
    if (bd_y + bd_h < CL_MAP_Y + CL_MAP_H) {
        DrawRectangle(bd_x, bd_y + bd_h,
                      bd_w, CL_MAP_Y + CL_MAP_H - (bd_y + bd_h),
                      PAL_CLR(BLACK));
    }
}

// Public bridge for screen modules in src/screens/. Takes an int
// for loc_kind so the LocKind enum can stay private to this file.
// Constants (must match LocKind enum order):
//   1 = LOC_CASTLE  2 = LOC_TOWN     3 = LOC_PLAINS
//   4 = LOC_FOREST  5 = LOC_HILLCAVE 6 = LOC_DUNGEON
//
// `troop_frame` is the 0..3 animation frame the caller owns. The
// screens advance their own frame from SYN ticks (e.g. recruit_soldiers
// game.c:2148 / 2216-2217).
void screens_draw_location_backdrop(const Game *g, const Sprites *s,
                                    int loc_kind, int troop_idx,
                                    int troop_frame) {
    LocKind k = LOC_NONE;
    switch (loc_kind) {
        case 1: k = LOC_CASTLE;   break;
        case 2: k = LOC_TOWN;     break;
        case 3: k = LOC_PLAINS;   break;
        case 4: k = LOC_FOREST;   break;
        case 5: k = LOC_HILLCAVE; break;
        case 6: k = LOC_DUNGEON;  break;
        default: break;
    }
    draw_location_backdrop(g, s, k, troop_idx, troop_frame);
}

static int town_backdrop_troop(const Game *g, const char *key) {
    // Deterministic pick per (seed, town id). Limit the pool to troops
    // that dwell at castles (militia/archers/pikemen/cavalry/knights) --
    // the "civilian" army that would idle in a human town. Matches the
    // home_troops[] filter used for recruit_soldiers
    // (dwells == DWELLING_CASTLE).
    int nt = troops_count();
    int pool[32];
    int npool = 0;
    for (int i = 0; i < nt && npool < 32; i++) {
        const TroopDef *t = troop_by_index(i);
        if (!t) continue;
        if (strcmp(t->dwelling, "castle") == 0) pool[npool++] = i;
    }
    if (npool < 1) {
        // Fallback: use the whole catalog.
        if (nt < 1) nt = 1;
        unsigned long h = g ? g->seed ^ 0xA1B2C3u : 0;
        for (const char *p = key; p && *p; p++) h = h * 131u + (unsigned char)*p;
        return (int)(h % (unsigned long)nt);
    }
    unsigned long h = g ? g->seed ^ 0xA1B2C3u : 0;
    for (const char *p = key; p && *p; p++) h = h * 131u + (unsigned char)*p;
    return pool[h % (unsigned long)npool];
}

static void draw_town(const Game *g, const Sprites *s) {
    const char *name = views_town_display_name();
    const char *info = views_town_info_text();
    int rows = views_town_row_count();
    int cursor = views_town_cursor();

    // visit_town draws the town backdrop at (16, 22) and
    // animates a random castle-class troop on top. Delegated to the
    // generic location-backdrop helper. Town view doesn't own a SYN-tick
    // frame counter yet; derive a 4-frame index from real time at the
    // SYN cadence (~150ms per frame → 6.7fps).
    int troop_idx = town_backdrop_troop(g, name);
    int town_frame = ((int)(GetTime() * 6.66)) & 3;
    draw_location_backdrop(g, s, LOC_TOWN, troop_idx, town_frame);

    // Menu panel: placed below the backdrop in the bottom-frame area.
    // Header is 2 rows (Town of NAME + GP=NK) 
    int row_h = GH + 1;
    int pad = 4;
    int lines = 2 /* header rows */ + rows;
    int h = lines * row_h + 2 * pad + 4;
    int w = CL_MAP_W + CL_SIDEBAR_W;
    int x = CL_MAP_X;
    int y = CL_MAP_Y + CL_MAP_H - h;

    draw_panel(x, y, w, h, PAL_CLR(DBLUE));

    int tx = x + pad;
    // : header rendered at `text->y - fs->h/4 - fs->h/8`,
    // a few pixels above the inner-text top.
    int ty = y + pad - row_h / 4 - row_h / 8;
    if (ty < y + 1) ty = y + 1;

    // Header row 1: "Town of <name>" (templates from strings.banners).
    const ResBanners *bn = (g && g->res) ? &g->res->banners : NULL;
    char header[96];
    if (bn) {
        ResTemplateVar vars[] = { { "NAME", (name && name[0]) ? name : "" } };
        resources_format_template(header, sizeof header, bn->town_header,
                                  vars, 1);
    } else {
        snprintf(header, sizeof header, "Town of %s",
                 (name && name[0]) ? name : "");
    }
    bfont_draw(header, tx, ty, PAL_CLR(YELLOW));
    ty += row_h;

    // Header row 2: GP=<gold/1000>K, right-aligned. 
    // formats `"                    GP=%dK\n"` — 20 spaces of leading pad
    // followed by the label, which on a 30-col panel puts GP at the right
    // edge. We keep that visual by right-aligning to the panel.
    char gp[24];
    if (bn) {
        char gbuf[16];
        int gold_k = g ? (g->stats.gold / 1000) : 0;
        snprintf(gbuf, sizeof gbuf, "%d", gold_k);
        ResTemplateVar vars[] = { { "GOLD", gbuf } };
        resources_format_template(gp, sizeof gp, bn->town_gold_label,
                                  vars, 1);
    } else {
        snprintf(gp, sizeof gp, "GP=%dK", g ? (g->stats.gold / 1000) : 0);
    }
    Vector2 gpm = bfont_measure(gp);
    bfont_draw(gp, x + w - (int)gpm.x - pad, ty, PAL_CLR(YELLOW));
    ty += row_h + 2;

    // Rows A..E.
    for (int r = 0; r < rows; r++) {
        char row[96];
        views_town_row_text(g, r, row, sizeof(row));
        bool sel = (r == cursor);
        Color fg = sel ? PAL_CLR(YELLOW) : PAL_CLR(WHITE);
        bfont_draw(row, tx, ty, fg);
        ty += row_h;
    }

    // Info overlay (popups like "Not enough gold!" + gather-information
    // intel). The bottom box replaces the bottom-frame text while
    // keeping the same yellow-bordered blue interior — the popup occupies
    // the menu's rect with the menu text replaced. We mirror that: same
    // panel color, same rect, draw the popup background over the menu
    // text we just rendered. (The menu rows underneath are visually
    // covered.)
    if (info && info[0]) {
        draw_panel(x, y, w, h, PAL_CLR(DBLUE));

        int itx = x + pad;
        int ity = y + pad;
        int max_chars = (w - 2 * pad) / GW;
        int body_lines = lines;        // popup uses the full menu height
        const char *p = info;
        char line[128];
        int nl = 0;
        while (*p && nl < body_lines) {
            consume_line(&p, max_chars, line, (int)sizeof(line));
            bfont_draw(line, itx, ity, PAL_CLR(WHITE));
            ity += row_h;
            nl++;
        }
    }
}

// ---------------------------------------------------------------------------
// Options screen (O key). A single text panel below the status bar
// listing every adventure keybinding. Matches options_menu() 
// (game.c:5144-5275). Esc or any key closes.
// ---------------------------------------------------------------------------

static void draw_options(const Game *g) {
    // : movement-reference rows on top
    // (8 direction keys + numpad equivalents), then the lettered command
    // list below. One unified blue panel below the status bar.
    int pad = 3;
    int cols = 28;
    int rows = 19;
    int w = cols * GW + 2 * pad;
    int h = rows * GH + 2 * pad;
    int x = CL_MAP_X;
    int y = CL_STATUS_Y + CL_STATUS_H + CL_BAR_H;

    draw_panel(x, y, w, h, PAL_CLR(DBLUE));

    int tx = x + pad;
    int ty = y + pad;

    // Movement reference.
    static const struct { const char *keys; const char *label; } mv[8] = {
        { "\x18 or 2",   "Move Down"      },  // arrow-glyph codepoint, plus numpad 2
        { "\x1B or 4",   "Move Left"      },
        { "\x1A or 6",   "Move Right"     },
        { "\x19 or 8",   "Move Up"        },
        { "END or 1",    "Down Left"      },
        { "PGDN or 3",   "Down Right"     },
        { "HOME or 7",   "Up Left"        },
        { "PGUP or 9",   "Up Right"       },
    };
    for (int i = 0; i < 8; i++) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%-10s %s", mv[i].keys, mv[i].label);
        bfont_draw(buf, tx, ty, PAL_CLR(WHITE));
        ty += GH;
    }

    // Items are laid out as "<key label>  <item name>" per line, matching
    // options_menu rendering order. Source: res.ui.keybinds.
    const ResUI *ui = (g && g->res) ? &g->res->ui : NULL;
    int n = ui ? ui->keybind_count : 0;
    int max_row = (y + h - pad - ty) / GH;
    if (n > max_row) n = max_row;

    for (int i = 0; i < n; i++) {
        const ResKeybind *kb = &ui->keybinds[i];
        // Skip mount-conditional entries if they don't apply.
        if (strcmp(kb->key, "F") == 0 && g->character.mount == MOUNT_FLY) continue;
        if (strcmp(kb->key, "L") == 0 && g->character.mount != MOUNT_FLY) continue;
        if (strcmp(kb->key, "N") == 0 && g->character.mount != MOUNT_SAIL) continue;
        char buf[48];
        snprintf(buf, sizeof(buf), "%-4s %s", kb->key, kb->label);
        bfont_draw(buf, tx, ty, PAL_CLR(WHITE));
        ty += GH;
    }
}

// ---------------------------------------------------------------------------
// Toast — small strip at the top of the map area.
// ---------------------------------------------------------------------------

static void draw_toast(void) {
    const char *msg = toast_text_current();
    if (!msg) return;
    Vector2 m = bfont_measure(msg);
    int w = (int)m.x + 8;
    int h = GH + 4;
    int x = CL_MAP_X + (CL_MAP_W - w) / 2;
    int y = CL_MAP_Y + 2;
    draw_panel(x, y, w, h, PAL_CLR(BLACK));
    bfont_draw(msg, x + 4, y + 2, PAL_CLR(YELLOW));
}

// ---------------------------------------------------------------------------
// Controls settings panel .
// ---------------------------------------------------------------------------

static void draw_controls(const Game *g) {
    if (!g || !g->res) return;
    int count = g->res->controls.count;
    int cursor = views_controls_cursor();
    if (cursor >= count) cursor = count ? count - 1 : 0;

    // Visible settings: skip anything marked hidden (CGA in our data).
    int vis_idx[8];
    int vis = 0;
    for (int i = 0; i < count && vis < 8; i++) {
        if (g->res->controls.items[i].hidden) continue;
        vis_idx[vis++] = i;
    }
    if (vis == 0) return;

    // controls_menu opens flush against the left edge of the map
    // area so the live game stays visible to the right (page 5 of refs).
    int pad = 3;
    int cols = 18;
    int rows = vis + 1;  // +1 for the "Controls" title row
    int w = cols * GW + 2 * pad;
    int h = rows * (GH + 2) + 2 * pad;
    int x = CL_MAP_X;
    int y = CL_STATUS_Y + CL_STATUS_H + CL_BAR_H;

    draw_panel(x, y, w, h, PAL_CLR(DBLUE));

    int tx = x + pad;
    int ty = y + pad;

    // Title row (highlighted, centered-ish).
    const ResUI *ui_ctl = (g && g->res) ? &g->res->ui : NULL;
    bfont_draw(ui_ctl ? ui_ctl->controls_title : " Controls ",
               tx, ty, PAL_CLR(YELLOW));
    ty += GH + 2;

    for (int k = 0; k < vis; k++) {
        int i = vis_idx[k];
        bool is_selected = (i == cursor);
        bool disabled    = views_controls_row_disabled(g, i);
        Color fg;
        if (disabled) {
            fg = PAL_CLR(DGREY);
        } else {
            fg = is_selected ? PAL_CLR(YELLOW) : PAL_CLR(WHITE);
        }

        char label[48];
        snprintf(label, sizeof(label), "%c %s",
                 '1' + k, g->res->controls.items[i].label);
        bfont_draw(label, tx, ty, fg);

        int val = g->stats.options[i];
        const char *type = g->res->controls.items[i].type;
        if (strcmp(type, "bool") == 0) {
            const char *text = (val == 1)
                ? (ui_ctl ? ui_ctl->controls_on  : "On")
                : (ui_ctl ? ui_ctl->controls_off : "Off");
            int tw = (int)bfont_measure(text).x;
            bfont_draw(text, x + w - pad - tw, ty, fg);
        } else {
            int range = g->res->controls.items[i].range;
            if (range > 10) range = 10;
            int sx = x + w - pad - range * GW;
            for (int n = 0; n < range; n++) {
                char buf[2] = { (char)('0' + n), 0 };
                Color nc;
                if (disabled) nc = PAL_CLR(DGREY);
                else          nc = (n == val) ? PAL_CLR(YELLOW) : PAL_CLR(WHITE);
                bfont_draw(buf, sx + n * GW, ty, nc);
            }
        }
        ty += GH + 2;
    }
}

// ---------------------------------------------------------------------------
// Top-level dispatcher.
// ---------------------------------------------------------------------------

void overlay_draw(const Game *g, const Map *m, const Fog *f,
                          const Sprites *s) {
    ViewKind v = views_active();

    if (v == VIEW_OPTIONS) {
        draw_options(g);
    } else if (v == VIEW_CONTROLS) {
        draw_controls(g);
    } else if (v == VIEW_MENU) {
        draw_menu();
    } else if (v == VIEW_TOWN) {
        draw_town(g, s);
    } else if (v == VIEW_HOME_CASTLE) {
        screen_home_castle_draw(g, s);
    } else if (v == VIEW_RECRUIT_SOLDIERS) {
        screen_recruit_soldiers_draw(g, s);
    } else if (v == VIEW_OWN_CASTLE) {
        screen_own_castle_draw(g, s);
    } else if (v == VIEW_DWELLING) {
        screen_dwelling_draw(g, s);
    } else if (v == VIEW_ALCOVE) {
        screen_alcove_draw(g, s);
    } else if (v == VIEW_WIN || v == VIEW_LOSE) {
        screen_end_game_draw(g, s);
    } else if (v == VIEW_ARMY      || v == VIEW_CHARACTER ||
               v == VIEW_CONTRACT  || v == VIEW_PUZZLE    ||
               v == VIEW_WORLDMAP  || v == VIEW_SPELLS) {
        views_render_draw(g, m, f, s);
    }

    if (dialog_is_active()) {
        draw_dialog();
    }

    // Modal prompt (yes/no, numeric picker) renders over dialogs since it
    // replaces the bottom frame entirely.
    if (prompt_is_active()) {
        prompt_draw();
    }

    // Toast always last so it floats above other layers.
    draw_toast();
}
