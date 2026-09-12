// src/modern/overlay.c
//
// The overlay for a pack that declared render.mode "modern": square tiles, a
// TrueType face, one panel rect (REQ-430h), the inverted cursor row (REQ-430e)
// and the dimmed scene beneath (REQ-430g). Modern UI work happens here.
//
// The DOS original's overlay is in src/legacy/overlay.c and is frozen: it must
// not be edited to serve anything in this file.
//
// Called only through the dispatcher in src/overlay.c.

#include "overlay.h"
#include "overlay_impl.h"
#include "touch.h"
#include "select.h"
#include "layout.h"
#include "palette.h"
#include "views.h"
#include "bfont.h"
#include "ui.h"
#include "resources.h"
#include <stdio.h>
#include <string.h>

// Width of a glyph + 1px spacing between glyphs . Bitmap font is
// fixed-pitch 8x8 with no extra spacing so chars-per-line at width W is
// W / BFONT_GLYPH_W.
#define GW  BFONT_GLYPH_W
#define GH  BFONT_GLYPH_H

// Body rows per page in the bottom message panel. Single-sourced: both the
// renderer (draw_dialog_ex) and the page-count the pager reads
// (overlay_dialog_page_count) must use the same value, or dialog_advance and
// the display disagree about how many pages a message has.
#define DLG_BOTTOM_BODY_LINES 7

// ---------------------------------------------------------------------------
// Bottom message frame: a solid black strip covering the map viewport from
// the status bar downward, plus a 1px yellow frame. This rendering reserves the
// entire bottom portion of the screen for long messages (KB_BottomBox); we
// draw into the map viewport because that's the biggest available area.
// ---------------------------------------------------------------------------

static void draw_panel(int x, int y, int w, int h, Color bg) {
    DrawRectangle(x, y, w, h, bg);
    ui_window_frame(x, y, w, h, PAL_CLR(YELLOW));
}

// Pages the current dialog body wraps to in the bottom panel. Counts WRAPPED
// lines the same way draw_dialog_ex renders them -- same bfont_take_line, same
// CL_PANEL_COLS width, same rows-per-page -- so the pager (dialog_advance)
// and the display never disagree. The old pager counted raw newlines, so a
// long word-wrapped paragraph with few newlines was scored as one page and
// its overflow was unreachable.
int modern_overlay_dialog_page_count(void) {
    const char *body = dialog_body_text();
    if (!body || !body[0]) return 1;
    int lines = 0;
    const char *p = body;
    char line[128];
    while (*p) {
        int max_w = CL_PANEL_W - 2 * CL_PANEL_PAD_X;
        if (bfont_take_line(&p, max_w, line, (int)sizeof line) <= 0) break;
        lines++;
    }
    int pages = (lines + DLG_BOTTOM_BODY_LINES - 1) / DLG_BOTTOM_BODY_LINES;
    return pages < 1 ? 1 : pages;
}

// ---------------------------------------------------------------------------
// Dialog (bottom message framestyle).
// ---------------------------------------------------------------------------


// Dialog draw modes:
//   BOTTOM        -- bottom box (CL_PANEL_*, 30 cols x ~10 rows).
//                   Sits over the bottom frame so adventure-mode sidebar
//                   stays visible.
//   CENTERED_MODAL -- victory layout: a 36-col x 16-row
//                   border, centered on screen (RECT_Text(16,36) +
//                   RECT_Center). Used for the victory banner so the
//                   dialog floats over the still-rendered battlefield
//                   instead of replacing the bottom frame.
typedef enum {
    DLG_MODE_BOTTOM = 0,
    DLG_MODE_CENTERED_MODAL,
} DialogMode;

static void draw_dialog_ex(DialogMode mode);

void modern_overlay_draw_dialog(void)          { draw_dialog_ex(DLG_MODE_BOTTOM); }
void modern_overlay_draw_dialog_centered(void) { draw_dialog_ex(DLG_MODE_CENTERED_MODAL); }

static void draw_dialog_ex(DialogMode mode) {
    const char *hdr = dialog_header_text();
    const char *body = dialog_body_text();

    // Horizontal padding is independent of vertical: pad_x sets the text
    // inset and the wrap width, pad_y only buys space the glyph rows have
    // to share. They were one value, and at 4px the vertical budget did not
    // add up -- see the row arithmetic below.
    int pad_x, pad_y, header_gap;
    int x, y, w, h, body_lines;

    if (mode == DLG_MODE_CENTERED_MODAL) {
        // Victory layout: 36 cols x 16 rows
        // of glyphs, centered on the 320x200 screen.
        w = 36 * GW;
        h = 16 * GH;                    // 128px, rows 0..127 (border on 127)
        x = CL_CENTER_ON_SCREEN_X(w);
        y = CL_CENTER_ON_SCREEN_Y(h);
        body_lines = 14;  // leaves room for header + spinner
        // pad_y + header(8) + gap + 14*8 must clear the bottom border:
        //   2 + 8 + 4 + 112 = 126  -> last row 125, border 127. Fits.
        // (Was pad 4 + gap 8 = 132, overflowing the panel by 4px.)
        pad_x = 4;          // 288px wide; 4px inset still yields 35 cols
        pad_y = 2;
        header_gap = 4;
    } else {
        // Bottom-frame rectangle, same as every
        // persistent menu (home castle, own castle, dwelling, ...).
        x = CL_PANEL_X;
        y = CL_PANEL_Y;
        w = CL_PANEL_W;
        h = CL_PANEL_H;                 // 68px, rows 0..67 (border on 67)
        body_lines = DLG_BOTTOM_BODY_LINES;
        // Glyphs are GH=8 tall and stack with no leading, so 7 body rows
        // plus a 1-row header cost a fixed 64px. That leaves 4px for
        // pad_y + gap, and the last glyph row must stop short of the
        // border:
        //   2 + 8 + 1 + 56 = 67  -> last row 66, border 67. Fits.
        // (Was pad 4 + gap 2 = 70, clipping the 7th line by 2px.)
        pad_x = CL_PANEL_PAD_X;   // 1, so max_chars comes out at 30
        pad_y = 2;
        header_gap = 1;
    }

    // Count header rows (newline-separated).
    int header_rows = 0;
    if (hdr && hdr[0]) {
        header_rows = 1;
        for (const char *p = hdr; *p; p++) if (*p == '\n') header_rows++;
    }

    draw_panel(x, y, w, h, PAL_CLR(DBLUE));

    int tx = x + pad_x;
    int ty = y + pad_y;
    // Wrap width in pixels. A proportional face has no column count to wrap
    // by, so both dialog modes measure the panel's own inner width.
    int max_w = w - 2 * pad_x;

    if (header_rows) {
        const char *hp = hdr;
        char hline[128];
        // The header is prose and wraps like the body -- the audience passes
        // the king's words as the header.
        int rows_left = DLG_BOTTOM_BODY_LINES + 1;
        for (int i = 0; i < rows_left && *hp != '\0'; i++) {
            if (bfont_take_line(&hp, max_w, hline, (int)sizeof(hline)) <= 0) break;
            if (mode == DLG_MODE_CENTERED_MODAL) {
                bfont_draw_centered(hline, x + w / 2, ty, PAL_CLR(YELLOW));
            } else {
                bfont_draw(hline, tx, ty, PAL_CLR(YELLOW));
            }
            ty += GH;
        }
        ty += header_gap;
    }

    // Body: word-wrap to panel width; skip to current page and draw body_lines rows.
    const char *p = body ? body : "";
    int current_page = dialog_page_current();
    int lines_skipped = 0;
    char line[128];

    while (*p && lines_skipped < current_page * body_lines) {
        int got = bfont_take_line(&p, max_w, line, (int)sizeof(line));
        if (got <= 0) break;
        lines_skipped++;
    }

    int lines_drawn = 0;
    while (*p && lines_drawn < body_lines) {
        int got = bfont_take_line(&p, max_w,
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

void modern_overlay_draw_menu(void) {
    const char *title = views_menu_title();
    int count = views_menu_entry_count();
    int cursor = views_menu_cursor();

    // Size the panel to the menu content.
    int row_h = GH + 2 * CL_UI;
    // A proportional face has no column count to size by: the widest entry,
    // or the title, sets the width.
    int widest = title ? bfont_text_width(title) : 0;
    for (int i = 0; i < count; i++) {
        const char *label = views_menu_entry_label(i);
        if (!label) continue;
        char buf[64];
        snprintf(buf, sizeof buf, "%s >", label);
        int lw = bfont_text_width(buf);
        if (lw > widest) widest = lw;
    }
    // Shrink-to-fit, but capped at the standard panel width rather than
    // growing to whatever the widest entry needs.
    int need = widest + GW + 16 * CL_UI;
    int w = (need > CL_PANEL_STD_W) ? CL_PANEL_STD_W : need;
    int h = (count + 2) * row_h + 8 * CL_UI;   // title + entries + hint
    int x = CL_CENTER_IN_PANE_X(w);
    int y = CL_CENTER_IN_PANE_Y(h);

    draw_panel(x, y, w, h, PAL_CLR(DBLUE));

    int tx = x + 6 * CL_UI;
    int ty = y + 4 * CL_UI;
    if (title) {
        bfont_draw_centered(title, x + w / 2, ty, PAL_CLR(YELLOW));
        ty += row_h + 2 * CL_UI;
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

        sel_row(x, ty, w, row_h, tx + GW + 4 * CL_UI, buf, sel, fg, PAL_CLR(DBLUE),
                TOUCH_LIST_MENU, i);
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
// draw_location takes (loc_id, troop_id, frame) -- the screen
// advances `frame` from SYN ticks. We mirror that contract.
//
// Exposed via screens_draw_location_backdrop() below for screen modules.
static void draw_location_backdrop(const Game *g, const Sprites *s,
                                   LocKind kind, int troop_idx,
                                   int troop_frame) {
    (void)g;
    // The backdrop is fixed-size content, so it belongs in the content rect
    // like the view panels do -- anchoring it to the map pane pinned it to the
    // top-left corner of a pane many times its size. 240x102 is its authored
    // size in the 320x200 design space.
    int bd_x = CL_CONTENT_X;
    int bd_y = CL_CONTENT_Y;
    int bd_w = 240 * CL_UI;
    int bd_h = 102 * CL_UI;
    Texture2D bd = loc_texture(s, kind);
    if (bd.id && bd.width > 0) {
        ui_blit(bd, bd_x, bd_y, bd_w, bd_h);
    } else {
        DrawRectangle(bd_x, bd_y, bd_w, bd_h, PAL_CLR(BLACK));
    }

    // Animated troop sprite: 4-frame strip pinned to bottom-left, inset
    // one sprite-width (x = troop_w * 1). Lifted a few pixels off the backdrop
    // bottom so it clears the menu/dialog panel drawn just below (otherwise the
    // sprite's feet overlap the panel's top border).
    const int troop_lift = 4 * CL_UI;
    if (s && troop_idx >= 0 && troop_idx < 25) {
        // troop_frame arrives as a free-running tick; the troop's own
        // declared cycle length decides where in the strip that lands.
        int frame = sprites_frame(troop_frame, s->troop_anim_frames[troop_idx]);
        Texture2D ts = s->troop_anim[troop_idx][frame];
        if (!ts.id) ts = s->troop_sprite[troop_idx];
        if (ts.id && ts.width > 0) {
            // Tile-shaped: this is the same troop sprite the combat field and
            // the army roster draw, so it gets the same slot.
            int tw = CL_TILE_W;
            int th = CL_TILE_H;
            ui_blit(ts, bd_x + tw, bd_y + bd_h - th - troop_lift, tw, th);
        }
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
void modern_overlay_draw_location_backdrop(const Game *g, const Sprites *s,
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

// Town only (modern): the backdrop fills the whole map pane width and the
// full height left above the full-width panel, instead of the fixed 240x102
// card every other location screen uses. The source crops on width to match
// the target aspect ratio, so the art fills the box with no gap and no
// stretch -- same idea as a CSS "cover" background.
static void draw_town_backdrop(const Sprites *s, int troop_idx,
                                int troop_frame, int bd_h) {
    int bd_x = CL_MAP_X, bd_y = CL_MAP_Y, bd_w = CL_MAP_W;
    Texture2D bd = loc_texture(s, LOC_TOWN);
    if (bd.id && bd.width > 0 && bd.height > 0) {
        float scale = (float)bd_h / (float)bd.height;
        float src_w = (float)bd_w / scale;
        if (src_w > bd.width) src_w = (float)bd.width;
        Rectangle src = { ((float)bd.width - src_w) / 2.0f, 0, src_w, (float)bd.height };
        Rectangle dst = { (float)bd_x, (float)bd_y, (float)bd_w, (float)bd_h };
        DrawTexturePro(bd, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
    } else {
        DrawRectangle(bd_x, bd_y, bd_w, bd_h, PAL_CLR(BLACK));
    }

    const int troop_lift = 4 * CL_UI;
    if (s && troop_idx >= 0 && troop_idx < 25) {
        int frame = sprites_frame(troop_frame, s->troop_anim_frames[troop_idx]);
        Texture2D ts = s->troop_anim[troop_idx][frame];
        if (!ts.id) ts = s->troop_sprite[troop_idx];
        if (ts.id && ts.width > 0) {
            int tw = CL_TILE_W, th = CL_TILE_H;
            ui_blit(ts, bd_x + tw, bd_y + bd_h - th - troop_lift, tw, th);
        }
    }
}

void modern_overlay_draw_town(const Game *g, const Sprites *s) {
    const char *name = views_town_display_name();
    const char *info = views_town_info_text();
    int rows = views_town_row_count();
    int cursor = views_town_cursor();

    // Town view doesn't own a SYN-tick frame counter yet; derive a
    // free-running tick from real time at the SYN cadence (~150ms per
    // frame -> 6.7fps).
    int troop_idx = town_backdrop_troop(g, name);
    int town_frame = (int)(GetTime() * 6.66);

    // Menu panel. Header is 2 rows (Town of NAME + GP=NK) plus the A..E rows.
    int row_h = GH + CL_UI;
    int pad = 4 * CL_UI;
    int lines = 2 /* header rows */ + rows;
    int h = lines * row_h + 2 * pad + 4 * CL_UI;

    // Town gets the whole map side of the screen: a full-pane-width panel, and
    // the backdrop maxed out to fill the height left above it. Every other
    // location screen shares the standard panel rect (REQ-430h) via
    // draw_location_backdrop.
    int w = CL_MAP_W;
    int x = CL_MAP_X;
    int y = CL_MAP_Y + CL_MAP_H - h;
    draw_town_backdrop(s, troop_idx, town_frame, CL_MAP_H - h);

    draw_panel(x, y, w, h, PAL_CLR(DBLUE));

    int tx = x + pad;
    // : header rendered at `text->y - fs->h/4 - fs->h/8`,
    // a few pixels above the inner-text top.
    int ty = y + pad - row_h / 4 - row_h / 8;
    if (ty < y + CL_UI) ty = y + CL_UI;

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
    // formats `"                    GP=%dK\n"` -- 20 spaces of leading pad
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
        bool live = !(info && info[0]);
        // The cursor row inverted; a tap on a row selects and confirms it
        // (views_town_update reads TOUCH_LIST_TOWN).
        sel_row(x, ty, w, row_h, tx, row, sel, fg, PAL_CLR(DBLUE),
                live ? TOUCH_LIST_TOWN : 0, r);
        ty += row_h;
    }

    // Info overlay (popups like "Not enough gold!" + gather-information
    // intel). The bottom box replaces the bottom-frame text while
    // keeping the same yellow-bordered blue interior -- the popup occupies
    // the menu's rect with the menu text replaced. We mirror that: same
    // panel color, same rect, draw the popup background over the menu
    // text we just rendered. (The menu rows underneath are visually
    // covered.)
    if (info && info[0]) {
        draw_panel(x, y, w, h, PAL_CLR(DBLUE));

        int itx = x + pad;
        int ity = y + pad;
        int max_w = w - 2 * pad;
        int body_lines = lines;        // popup uses the full menu height
        const char *p = info;
        char line[128];
        int nl = 0;
        while (*p && nl < body_lines) {
            bfont_take_line(&p, max_w, line, (int)sizeof(line));
            bfont_draw(line, itx, ity, PAL_CLR(WHITE));
            ity += row_h;
            nl++;
        }
    }
}

// ---------------------------------------------------------------------------
// Options screen (O key). A single text panel below the status bar
// listing every adventure keybinding. Matches options_menu() 
// (OpenKB's game.c:5144-5275). Esc or any key closes.
// ---------------------------------------------------------------------------

void modern_overlay_draw_options(const Game *g) {
    // : movement-reference rows on top
    // (8 direction keys + numpad equivalents), then the lettered command
    // list below. One unified blue panel below the status bar.
    int pad = 3 * CL_UI;
    // The list decides the panel. Movement rows on top, then the keybinds;
    // when they will not fit the pane in one column they go in two.
    int kb_n = (g && g->res) ? g->res->ui.keybind_count : 0;
    int fit = (CL_MAP_H - 2 * pad) / GH - 8 - 1;
    int kb_cols = (kb_n > fit) ? 2 : 1;
    int kb_rows = (kb_n + kb_cols - 1) / kb_cols;
    int rows = 8 + 1 + kb_rows;
    int w = (kb_cols == 2) ? CL_PANEL_WIDE_W : CL_PANEL_STD_W;
    int h = rows * GH + 2 * pad;
    if (h > CL_MAP_H) h = CL_MAP_H;
    // Anchored to the content rect, not the pane. The panel is a fixed 28
    // columns, so on a wide pane the pane's left edge strands it in the
    // corner. The content rect IS the pane in legacy, so this stays at 16.
    int x = CL_CONTENT_X;
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
    if (kb_cols == 1 && n > max_row) n = max_row;

    int col_w = (w - 2 * pad) / kb_cols;
    int per_col = (n + kb_cols - 1) / kb_cols;
    int ty0 = ty;
    for (int i = 0; i < n; i++) {
        const ResKeybind *kb = &ui->keybinds[i];
        // Skip mount-conditional entries if they don't apply.
        if (strcmp(kb->key, "F") == 0 && g->character.mount == MOUNT_FLY) continue;
        if (strcmp(kb->key, "L") == 0 && g->character.mount != MOUNT_FLY) continue;
        if (strcmp(kb->key, "N") == 0 && g->character.mount != MOUNT_SAIL) continue;
        char buf[48];
        snprintf(buf, sizeof(buf), "%-4s %s", kb->key, kb->label);
        int cx = tx + (kb_cols == 2 ? (i / per_col) * col_w : 0);
        int cy = kb_cols == 2 ? ty0 + (i % per_col) * GH : ty;
        bfont_draw(buf, cx, cy, PAL_CLR(WHITE));
        if (kb_cols == 1) ty += GH;
    }
}

// ---------------------------------------------------------------------------
// Toast -- small strip at the top of the map area.
// ---------------------------------------------------------------------------

void modern_overlay_draw_toast(void) {
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

void modern_overlay_draw_controls(const Game *g) {
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
    int rows = vis + 2;                           // title, settings, Scale
    int w = CL_PANEL_STD_W;
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
        Color vfg = fg;   // value colour; inverted with the row
        sel_row(x, ty, w, GH + 2, tx, label, is_selected && !disabled, fg, PAL_CLR(DBLUE), 0, 0);
        if (is_selected && !disabled) vfg = PAL_CLR(DBLUE);
        // Touch: rows answer to their digit (select + advance in one).
        touch_region(x, ty, w, GH + 2, KEY_ONE + k);

        int val = g->stats.options[i];
        const char *type = g->res->controls.items[i].type;
        if (strcmp(type, "bool") == 0) {
            const char *text = (val == 1) ? ui_ctl->controls_on
                                          : ui_ctl->controls_off;
            int tw = (int)bfont_measure(text).x;
            bfont_draw(text, x + w - pad - tw, ty, vfg);
        } else {
            int range = g->res->controls.items[i].range;
            if (range > 10) range = 10;
            int sx = x + w - pad - range * GW;
            for (int n = 0; n < range; n++) {
                char buf[2] = { (char)('0' + n), 0 };
                Color nc;
                if (disabled) nc = PAL_CLR(DGREY);
                else if (vfg.r == PAL_CLR(DBLUE).r && vfg.g == PAL_CLR(DBLUE).g && vfg.b == PAL_CLR(DBLUE).b)
                                nc = (n == val) ? PAL_CLR(WHITE) : PAL_CLR(DBLUE);   // inverted row: the set digit stands out
                else            nc = (n == val) ? PAL_CLR(YELLOW) : PAL_CLR(WHITE);
                bfont_draw(buf, sx + n * GW, ty, nc);
            }
        }
        ty += GH + 2;
    }

    // Scale: appended by the shell, not part of the pack's controls. Backed by
    // present.c rather than stats.options[], because a display preference must
    // not travel inside a save file. There is no legacy counterpart: a legacy
    // pack looks exactly as it did before render modes existed.
    {
        bool is_selected = (cursor == vis);
        Color fg = is_selected ? PAL_CLR(YELLOW) : PAL_CLR(WHITE);
        char label[48];
        snprintf(label, sizeof(label), "%c Scale", '1' + vis);
        sel_row(x, ty, w, GH + 2, tx, label, is_selected, fg, PAL_CLR(DBLUE), 0, 0);
        touch_region(x, ty, w, GH + 2, KEY_ONE + vis);

        int sc = views_controls_scale_value();
        char val[16];
        snprintf(val, sizeof(val), "%dx", sc);
        int vw = (int)bfont_measure(val).x;
        bfont_draw(val, x + w - pad - vw, ty, is_selected ? PAL_CLR(DBLUE) : fg);
    }
}

// ---------------------------------------------------------------------------
// The dimmed scene beneath a detail view, prompt or dialog (REQ-430g).
// ---------------------------------------------------------------------------

void modern_overlay_dim_scene(void) {
    const Resources *r = resources_current();
    int a = overlay_dim_alpha(r ? r->render.dim : 0);
    if (a == 0) return;
    // The whole chrome interior: map pane plus sidebar, not the status band
    // or the frame, so the frame keeps its weight and the band stays legible.
    Color shade = { 0, 0, 0, (unsigned char)a };
    DrawRectangle(CL_MAP_X, CL_MAP_Y, CL_MAP_W + CL_SIDEBAR_W, CL_MAP_H, shade);
}
