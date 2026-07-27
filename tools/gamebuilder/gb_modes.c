// Catalog, Strings, Art, Palette, Validate and Package modes.
//
// All of them edit the DOM directly, so a change here and a change in the raw
// JSON view cannot disagree. Forms cover the common fields; anything a form
// does not know about is still reachable through the raw JSON panel, so the
// editor is never a dead end (GB-120).

#include "gb_ui.h"

#include "raygui.h"
#include "assets.h"
#include "tile_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROW_H 22

// Which catalogs the Catalog mode lists, in the order an author works through
// them. Zones are edited on the map, so they are not here.
static const char *CATALOGS[] = {
    "troops", "spells", "artifacts", "villains", "classes", "castles", "towns"
};
#define NCAT ((int)(sizeof CATALOGS / sizeof *CATALOGS))

typedef struct {
    int      catalog;
    int      entry;
    Vector2  list_scroll, form_scroll;
    bool     raw;                 // raw JSON instead of the form
    char     edit_buf[256];
    char     edit_key[64];
    bool     editing;

    int      string_group;
    char     filter[64];
    bool     filter_edit;

    int      art_category;
    int      art_index;

    GbFindings findings;
    bool       findings_fresh;

    char     pkg_out[GB_PATH_MAX];
} GbModeState;

static GbModeState M;

static const char *str_of(cJSON *o, const char *k, const char *fb) {
    cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsString(v) ? v->valuestring : fb;
}

// --- a tiny form widget -------------------------------------------------------
//
// raygui's text box edits a char buffer, so committing a field means writing
// back into the DOM with the right JSON type. Doing that in one place keeps
// every catalog form honest about types -- a number that becomes a string is
// the kind of edit that loads fine and breaks at runtime.

static bool field_row(cJSON *parent, cJSON *item, const char *key,
                      Rectangle r, GbUndo *undo, bool *dirty) {
    GuiLabel((Rectangle){ r.x, r.y, 150, r.height }, key);
    Rectangle box = { r.x + 155, r.y, r.width - 165, r.height };

    char buf[256];
    if (cJSON_IsString(item)) snprintf(buf, sizeof buf, "%s", item->valuestring);
    else if (cJSON_IsNumber(item)) snprintf(buf, sizeof buf, "%g", item->valuedouble);
    else if (cJSON_IsBool(item)) snprintf(buf, sizeof buf, "%s",
                                          cJSON_IsTrue(item) ? "true" : "false");
    else return false;                     // arrays/objects: raw JSON only

    bool mine = M.editing && strcmp(M.edit_key, key) == 0;
    if (mine) {
        if (GuiTextBox(box, M.edit_buf, sizeof M.edit_buf, true) ||
            IsKeyPressed(KEY_ENTER)) {
            cJSON *before = cJSON_Duplicate(item, 1);
            cJSON *after = NULL;
            if (cJSON_IsNumber(item)) after = cJSON_CreateNumber(atof(M.edit_buf));
            else if (cJSON_IsBool(item))
                after = cJSON_CreateBool(strcmp(M.edit_buf, "true") == 0);
            else after = cJSON_CreateString(M.edit_buf);
            gb_undo_push_json(undo, TextFormat("edit %s", key), parent, key, -1,
                              before, after);
            cJSON_ReplaceItemInObject(parent, key, after);
            cJSON_Delete(before);
            M.editing = false;
            *dirty = true;
            return true;
        }
    } else {
        if (GuiTextBox(box, buf, sizeof buf, false)) {
            M.editing = true;
            snprintf(M.edit_key, sizeof M.edit_key, "%s", key);
            snprintf(M.edit_buf, sizeof M.edit_buf, "%s", buf);
        }
    }
    return false;
}

// --- Catalog ------------------------------------------------------------------

static void draw_catalog(GbWorkspace *ws, GbUndo *undo, int top) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    int listw = 260;

    // catalog picker
    for (int i = 0; i < NCAT; i++) {
        Rectangle r = { 8.0f + i * 92, (float)top + 6, 88, 22 };
        if (GuiButton(r, CATALOGS[i])) { M.catalog = i; M.entry = 0; M.editing = false; }
        if (M.catalog == i) DrawRectangleLinesEx(r, 2, YELLOW);
    }
    int y0 = top + 34;

    cJSON *arr = cJSON_GetObjectItem(ws->doc, CATALOGS[M.catalog]);
    if (!cJSON_IsArray(arr)) {
        DrawText(TextFormat("This pack has no '%s' array.", CATALOGS[M.catalog]),
                 16, y0 + 10, 14, GRAY);
        return;
    }
    int n = cJSON_GetArraySize(arr);

    // entry list
    Rectangle list = { 8, (float)y0, (float)listw, (float)(h - y0 - 40) };
    Rectangle view;
    GuiScrollPanel(list, NULL, (Rectangle){ 0, 0, list.width - 16,
                                            (float)(n * ROW_H + 4) },
                   &M.list_scroll, &view);
    BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
    for (int i = 0; i < n; i++) {
        Rectangle r = { list.x + 2, list.y + M.list_scroll.y + 2 + i * ROW_H,
                        list.width - 20, ROW_H - 2 };
        if (r.y + r.height < view.y || r.y > view.y + view.height) continue;
        cJSON *e = cJSON_GetArrayItem(arr, i);
        bool hov = CheckCollisionPointRec(GetMousePosition(), r) &&
                   CheckCollisionPointRec(GetMousePosition(), view);
        if (i == M.entry) DrawRectangleRec(r, (Color){ 60, 90, 140, 255 });
        else if (hov)     DrawRectangleRec(r, (Color){ 44, 44, 54, 255 });
        DrawText(TextFormat("%s", str_of(e, "name", str_of(e, "id", "?"))),
                 (int)r.x + 6, (int)r.y + 4, 12, RAYWHITE);
        if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            M.entry = i; M.editing = false;
        }
    }
    EndScissorMode();

    if (M.entry >= n) M.entry = n - 1;
    if (M.entry < 0) return;
    cJSON *e = cJSON_GetArrayItem(arr, M.entry);
    if (!e) return;

    // form / raw toggle
    int fx = listw + 20;
    if (GuiButton((Rectangle){ (float)fx, (float)y0, 100, 22 },
                  M.raw ? "Show form" : "Show raw JSON")) M.raw = !M.raw;

    // Live sprite preview (GB-225): seeing the thing you are editing catches
    // a wrong sprite path instantly, where a filename never would.
    const char *sprite = str_of(e, "sprite", str_of(e, "portrait", NULL));
    if (sprite) {
        Texture2D t = LoadAssetTexture(sprite);
        if (t.id) DrawTexturePro(t, (Rectangle){ 0, 0, (float)t.width,
                                                 (float)t.height },
                                 (Rectangle){ (float)(w - 120), (float)y0,
                                              96, 68 },
                                 (Vector2){ 0, 0 }, 0, WHITE);
    }

    Rectangle form = { (float)fx, (float)(y0 + 28), (float)(w - fx - 16),
                       (float)(h - y0 - 70) };
    if (M.raw) {
        char *txt = cJSON_Print(e);
        if (txt) {
            int lines = 1;
            for (char *p = txt; *p; p++) if (*p == '\n') lines++;
            Rectangle v2;
            GuiScrollPanel(form, NULL, (Rectangle){ 0, 0, 900,
                                                    (float)(lines * 14 + 8) },
                           &M.form_scroll, &v2);
            BeginScissorMode((int)v2.x, (int)v2.y, (int)v2.width, (int)v2.height);
            char *p = txt; int row = 0; char line[400];
            while (*p) {
                char *nl = strchr(p, '\n');
                size_t ln = nl ? (size_t)(nl - p) : strlen(p);
                if (ln > sizeof line - 1) ln = sizeof line - 1;
                memcpy(line, p, ln); line[ln] = 0;
                float ly = form.y + M.form_scroll.y + 4 + row * 14;
                if (ly > v2.y - 14 && ly < v2.y + v2.height)
                    DrawText(line, (int)(form.x + 6), (int)ly, 11,
                             (Color){ 190, 200, 190, 255 });
                if (!nl) break;
                p = nl + 1; row++;
            }
            EndScissorMode();
            cJSON_free(txt);
        }
        return;
    }

    Rectangle v3;
    int fields = 0;
    for (cJSON *c = e->child; c; c = c->next) fields++;
    GuiScrollPanel(form, NULL, (Rectangle){ 0, 0, form.width - 16,
                                            (float)(fields * (ROW_H + 4) + 8) },
                   &M.form_scroll, &v3);
    BeginScissorMode((int)v3.x, (int)v3.y, (int)v3.width, (int)v3.height);
    int row = 0;
    bool dirty = false;
    for (cJSON *c = e->child; c; c = c->next, row++) {
        Rectangle r = { form.x + 8, form.y + M.form_scroll.y + 6 + row * (ROW_H + 4),
                        form.width - 30, ROW_H };
        if (r.y + r.height < v3.y || r.y > v3.y + v3.height) continue;
        if (!c->string) continue;
        if (cJSON_IsArray(c) || cJSON_IsObject(c)) {
            GuiLabel((Rectangle){ r.x, r.y, 150, r.height }, c->string);
            DrawText(cJSON_IsArray(c)
                     ? TextFormat("[%d items]  -- edit in raw JSON",
                                  cJSON_GetArraySize(c))
                     : "{...}  -- edit in raw JSON",
                     (int)r.x + 158, (int)r.y + 4, 11, GRAY);
            continue;
        }
        field_row(e, c, c->string, r, undo, &dirty);
    }
    EndScissorMode();
    if (dirty) { ws->dirty = true; gb_workspace_reproject(ws); }
}

// --- Strings ------------------------------------------------------------------

static void draw_strings(GbWorkspace *ws, GbUndo *undo, int top) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    cJSON *strings = cJSON_GetObjectItem(ws->doc, "strings");
    if (!cJSON_IsObject(strings)) {
        DrawText("This pack declares no strings block. The engine will use its "
                 "built-in English text for everything.", 16, top + 16, 13, GRAY);
        return;
    }

    // group picker
    int gx = 8, gi = 0, sel = -1;
    for (cJSON *g = strings->child; g; g = g->next, gi++) {
        if (!g->string) continue;
        int bw = MeasureText(g->string, 12) + 18;
        Rectangle r = { (float)gx, (float)top + 6, (float)bw, 22 };
        if (GuiButton(r, g->string)) M.string_group = gi;
        if (M.string_group == gi) { DrawRectangleLinesEx(r, 2, YELLOW); sel = gi; }
        gx += bw + 4;
        if (gx > w - 200) break;
    }
    (void)sel;

    cJSON *group = NULL;
    gi = 0;
    for (cJSON *g = strings->child; g; g = g->next, gi++)
        if (gi == M.string_group) { group = g; break; }
    if (!group) return;

    GuiLabel((Rectangle){ 8, (float)top + 32, 60, 20 }, "Filter");
    if (GuiTextBox((Rectangle){ 68, (float)top + 32, 260, 20 },
                   M.filter, sizeof M.filter, M.filter_edit))
        M.filter_edit = !M.filter_edit;

    int y0 = top + 58;
    int n = 0;
    for (cJSON *s = group->child; s; s = s->next) n++;
    Rectangle box = { 8, (float)y0, (float)(w - 16), (float)(h - y0 - 30) };
    Rectangle view;
    GuiScrollPanel(box, NULL, (Rectangle){ 0, 0, box.width - 16,
                                           (float)(n * (ROW_H + 2) + 8) },
                   &M.list_scroll, &view);
    BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
    int row = 0;
    bool dirty = false;
    for (cJSON *s = group->child; s; s = s->next) {
        if (!s->string || !cJSON_IsString(s)) continue;
        if (M.filter[0] && !strstr(s->string, M.filter) &&
            !strstr(s->valuestring, M.filter)) continue;
        Rectangle r = { box.x + 6, box.y + M.list_scroll.y + 6 + row * (ROW_H + 2),
                        box.width - 26, ROW_H };
        row++;
        if (r.y + r.height < view.y || r.y > view.y + view.height) continue;

        // Substitution tokens are load-bearing: a string that loses %NAME%
        // renders wrong in game and nothing catches it later (GB-231).
        bool has_token = strchr(s->valuestring, '%') != NULL;
        DrawText(s->string, (int)r.x, (int)r.y + 5, 11,
                 has_token ? (Color){ 220, 200, 120, 255 } : GRAY);
        Rectangle tb = { r.x + 230, r.y, r.width - 230, r.height };
        field_row(group, s, s->string, (Rectangle){ r.x + 75, r.y,
                                                    r.width - 75, r.height },
                  undo, &dirty);
        (void)tb;
    }
    EndScissorMode();
    if (dirty) { ws->dirty = true; gb_workspace_reproject(ws); }
    DrawText("Yellow keys contain %TOKENS% -- keep them, or the line renders "
             "wrong in game.", 12, h - 22, 11, (Color){ 200, 180, 110, 255 });
}

// --- Art ----------------------------------------------------------------------

static const char *ART_DIRS[] = { "tiles", "troops", "villains", "sprites",
                                  "ui", "combat", "classes", "font" };
#define NART ((int)(sizeof ART_DIRS / sizeof *ART_DIRS))

static void draw_art(GbWorkspace *ws, int top) {
    int h = GetScreenHeight();
    for (int i = 0; i < NART; i++) {
        Rectangle r = { 8.0f + i * 86, (float)top + 6, 82, 22 };
        if (GuiButton(r, ART_DIRS[i])) { M.art_category = i; M.art_index = 0; }
        if (M.art_category == i) DrawRectangleLinesEx(r, 2, YELLOW);
    }
    int y0 = top + 36;

    // The pack's own art references come from tile_codes and the sprite
    // blocks; listing those rather than scanning the folder means a file that
    // exists but nothing points at shows up as unreferenced, which is the more
    // useful direction (GB-244).
    DrawText(TextFormat("art/%s", ART_DIRS[M.art_category]), 12, y0, 13,
             LIGHTGRAY);
    int y = y0 + 22, shown = 0, missing = 0;

    if (M.art_category == 0) {
        cJSON *tc = cJSON_GetObjectItem(ws->doc, "tile_codes");
        for (cJSON *c = tc ? tc->child : NULL; c; c = c->next) {
            const char *art = str_of(c, "art", NULL);
            if (!art) continue;
            Texture2D t = tile_cache_get(art);
            int col = shown % 10, rw = shown / 10;
            int px = 16 + col * 108, py = y + rw * 74;
            if (py > h - 80) break;
            if (t.id) {
                DrawTexturePro(t, (Rectangle){ 0, 0, (float)t.width,
                                               (float)t.height },
                               (Rectangle){ (float)px, (float)py, 96, 68 },
                               (Vector2){ 0, 0 }, 0, WHITE);
            } else {
                DrawRectangle(px, py, 96, 68, (Color){ 60, 20, 20, 255 });
                DrawText("missing", px + 22, py + 30, 10, RED);
                missing++;
            }
            DrawText(art, px, py + 68, 9, GRAY);
            Rectangle hit = { (float)px, (float)py, 96, 68 };
            if (CheckCollisionPointRec(GetMousePosition(), hit)) {
                DrawRectangleLinesEx(hit, 2, YELLOW);
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    char rel[GB_PATH_MAX];
                    snprintf(rel, sizeof rel, "art/tiles/%s.png", art);
                    if (!gb_pixel_open(rel))
                        DrawText("could not open", px, py + 30, 10, RED);
                }
            }
            shown++;
        }
    } else {
        DrawText("Art in this category is referenced from the catalogs.",
                 16, y, 12, GRAY);
        DrawText("Click a tile in the tiles category to edit it pixel by pixel.",
                 16, y + 18, 12, DARKGRAY);
    }
    DrawText("Click any tile to open it in the pixel editor.", 12, h - 40,
             11, (Color){ 150, 170, 200, 255 });
    DrawText(TextFormat("%d referenced, %d missing", shown, missing),
             12, h - 22, 12, missing ? (Color){ 255, 140, 120, 255 }
                                     : (Color){ 140, 200, 140, 255 });
}

// --- Palette ------------------------------------------------------------------

static void draw_palette(GbWorkspace *ws, int top) {
    int h = GetScreenHeight();
    DrawText("Pack palette -- 256 colours, 768 bytes", 12, top + 8, 13, LIGHTGRAY);
    DrawText("The first 16 are the reserved named indices.", 12, top + 26, 11,
             GRAY);
    (void)ws;
    // The palette is loaded by the shell's palette.c into PAL[]; drawing it
    // from there keeps this view honest about what the game will use.
    extern Color PAL[];
    for (int i = 0; i < 256; i++) {
        int col = i % 16, row = i / 16;
        Rectangle r = { 12.0f + col * 34, (float)top + 50 + row * 26, 32, 24 };
        DrawRectangleRec(r, PAL[i]);
        if (i < 16) DrawRectangleLinesEx(r, 1, (Color){ 255, 255, 255, 120 });
        if (CheckCollisionPointRec(GetMousePosition(), r))
            DrawText(TextFormat("#%d  %d,%d,%d", i, PAL[i].r, PAL[i].g, PAL[i].b),
                     12, h - 22, 12, RAYWHITE);
    }
}

// --- Validate -----------------------------------------------------------------

static void draw_validate(GbWorkspace *ws, int top,
                          char *status, size_t status_sz) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    if (GuiButton((Rectangle){ 8, (float)top + 6, 120, 24 }, "Run checks")) {
        extern void gb_collect_grids(MapGrid ***g, bool **loaded, int *n);
        MapGrid **grids; bool *loaded; int n;
        gb_collect_grids(&grids, &loaded, &n);
        gb_validate(&M.findings, ws, grids, loaded, n);
        M.findings_fresh = true;
        snprintf(status, status_sz, "%d finding(s)", M.findings.count);
    }
    DrawText("Findings are advisory. Nothing here blocks packaging.",
             140, top + 12, 12, GRAY);

    int y0 = top + 40;
    if (!M.findings_fresh) {
        DrawText("Press Run checks.", 16, y0 + 10, 13, GRAY);
        return;
    }
    for (int t = 0; t < GB_TIER_COUNT; t++)
        DrawText(TextFormat("%s: %d", gb_tier_name(t), M.findings.by_tier[t]),
                 12 + t * 160, y0, 12,
                 M.findings.by_tier[t] ? (Color){ 255, 180, 120, 255 }
                                       : (Color){ 140, 200, 140, 255 });
    y0 += 24;

    Rectangle box = { 8, (float)y0, (float)(w - 16), (float)(h - y0 - 24) };
    Rectangle view;
    GuiScrollPanel(box, NULL, (Rectangle){ 0, 0, box.width - 16,
                                           (float)(M.findings.count * 34 + 8) },
                   &M.list_scroll, &view);
    BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
    for (int i = 0; i < M.findings.count; i++) {
        const GbFinding *f = &M.findings.item[i];
        float ry = box.y + M.list_scroll.y + 6 + i * 34;
        if (ry + 34 < view.y || ry > view.y + view.height) continue;
        DrawText(TextFormat("[%s] %s", gb_tier_name(f->tier), f->where),
                 (int)box.x + 8, (int)ry, 11, (Color){ 220, 200, 120, 255 });
        DrawText(f->what, (int)box.x + 8, (int)ry + 14, 12, RAYWHITE);
    }
    EndScissorMode();
    if (M.findings.count == 0)
        DrawText("No findings. The pack is structurally sound.", 16, y0 + 10, 13,
                 (Color){ 140, 200, 140, 255 });
}

// --- Package ------------------------------------------------------------------

static void draw_package(GbWorkspace *ws, int top,
                         char *status, size_t status_sz) {
    int h = GetScreenHeight();
    DrawText("Package this pack into a .openbounty archive", 12, top + 10, 14,
             RAYWHITE);

    if (!M.pkg_out[0] && ws->res_valid)
        snprintf(M.pkg_out, sizeof M.pkg_out, "%s.openbounty",
                 ws->res.pack_id[0] ? ws->res.pack_id : "pack");

    GuiLabel((Rectangle){ 12, (float)top + 40, 90, 22 }, "Output file");
    GuiTextBox((Rectangle){ 106, (float)top + 40, 520, 22 },
               M.pkg_out, sizeof M.pkg_out, true);
    DrawText(TextFormat("written next to the pack, in %s", ws->root),
             12, top + 66, 11, GRAY);

    // Pre-package summary (GB-322): what is going out, and what is still
    // outstanding, before anything is written.
    int y = top + 96;
    DrawText("SUMMARY", 12, y, 12, LIGHTGRAY); y += 20;
    DrawText(TextFormat("Pack id      %s", ws->res_valid ? ws->res.pack_id : "?"),
             12, y, 12, RAYWHITE); y += 18;
    DrawText(TextFormat("Zones        %d", gb_zone_count(ws->doc)), 12, y, 12,
             RAYWHITE); y += 18;
    DrawText(TextFormat("Unsaved      %s", ws->dirty ? "YES -- save first" : "no"),
             12, y, 12, ws->dirty ? (Color){ 255, 150, 100, 255 } : RAYWHITE);
    y += 18;
    if (M.findings_fresh) {
        DrawText(TextFormat("Findings     %d (advisory)", M.findings.count),
                 12, y, 12, M.findings.count ? (Color){ 255, 200, 120, 255 }
                                             : RAYWHITE);
    } else {
        DrawText("Findings     not checked -- see the Validate tab", 12, y, 12,
                 GRAY);
    }
    y += 30;

    // What is still missing, next to the button that ships it.
    extern void gb_collect_grids(MapGrid ***g, bool **loaded, int *n);
    MapGrid **grids; bool *loaded; int ng;
    gb_collect_grids(&grids, &loaded, &ng);
    GbChecklist C;
    gb_checklist_build(&C, ws, grids, loaded, ng);
    int cy = top + 96, cx = 380;
    DrawText(TextFormat("COMPLETENESS  %d/%d", C.done_count, C.count),
             cx, cy, 12, LIGHTGRAY);
    cy += 20;
    for (int i = 0; i < C.count && cy < h - 40; i++) {
        DrawText(C.item[i].done ? "[x]" : "[ ]", cx, cy, 12,
                 C.item[i].done ? (Color){ 140, 200, 140, 255 }
                                : (Color){ 200, 160, 100, 255 });
        DrawText(C.item[i].what, cx + 28, cy, 12,
                 C.item[i].done ? GRAY : RAYWHITE);
        if (!C.item[i].done && C.item[i].hint[0])
            DrawText(C.item[i].hint, cx + 300, cy, 11, DARKGRAY);
        cy += 17;
    }

    if (GuiButton((Rectangle){ 12, (float)y, 160, 28 }, "Build .openbounty")) {
        char out[GB_PATH_MAX * 2], err[512];
        snprintf(out, sizeof out, "%s/%s", ws->root, M.pkg_out);
        if (gb_package(ws, out, err, sizeof err))
            snprintf(status, status_sz, "Wrote %s", out);
        else
            snprintf(status, status_sz, "%s", err);
    }
    DrawText("Nothing blocks this build. Outstanding findings are yours to "
             "judge.", 12, h - 22, 11, GRAY);
}

// --- dispatch -----------------------------------------------------------------

void gb_mode_draw(int mode, GbWorkspace *ws, GbUndo *undo, int top,
                  char *status, size_t status_sz) {
    if (!ws->open) return;
    switch (mode) {
        case 2: draw_catalog(ws, undo, top); break;
        case 3: draw_strings(ws, undo, top); break;
        case 4: draw_art(ws, top); break;
        case 5: draw_palette(ws, top); break;
        case 6: draw_validate(ws, top, status, status_sz); break;
        case 7: draw_package(ws, top, status, status_sz); break;
        default: break;
    }
}
