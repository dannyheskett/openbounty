#include "startup.h"
#include "layout.h"
#include "palette.h"
#include "chrome.h"
#include "bfont.h"
#include "savegame.h"
#include "screenshot.h"
#include "tables.h"
#include "resources.h"
#include "raylib.h"
#include "harness_input.h"
#include "harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GW BFONT_GLYPH_W
#define GH BFONT_GLYPH_H

// ---------------------------------------------------------------------------
// Local frame boilerplate: begin the 320x200 render texture, clear to
// black, and end after the caller has drawn into it. We don't blit the
// chrome bitmap — the pre-game screens are full-panel modal dialogs.
// ---------------------------------------------------------------------------

static void safe_copy(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = 0;
    while (n + 1 < dst_sz && src[n]) { dst[n] = src[n]; n++; }
    dst[n] = '\0';
}

static void frame_begin(RenderTexture2D *rt) {
    BeginTextureMode(*rt);
    ClearBackground(PAL_CLR(BLACK));
}

// Draw the class-select cartoon as the screen backdrop, so dialog
// panels (credits, save picker, new-game name/difficulty, new-game
// intro) sit visually over the character-selection screen.
static void draw_class_picker_backdrop(const Sprites *sprites) {
    if (!sprites || !sprites->class_picker.id) return;
    Texture2D t = sprites->class_picker;
    int pw = t.width;
    int ph = t.height;
    int bx = (CL_SCREEN_W - pw) / 2;
    int by = (CL_SCREEN_H - ph) / 2;
    Rectangle src = { 0, 0, (float)pw, (float)ph };
    Rectangle dst = { (float)bx, (float)by, (float)pw, (float)ph };
    DrawTexturePro(t, src, dst, (Vector2){0,0}, 0.0f, WHITE);
}

static void draw_class_picker_status_hint(const Resources *res) {
    DrawRectangle(0, 0, CL_SCREEN_W, GH + 2, PAL_CLR(DRED));
    const char *hint = (res && res->ui.startup_class_select_hint[0])
        ? res->ui.startup_class_select_hint
        : "Select Char A-D or L-Load saved game";
    bfont_draw_centered(hint, CL_SCREEN_W / 2, 1, PAL_CLR(WHITE));
}

static void frame_end(RenderTexture2D *rt) {
    EndTextureMode();

    BeginDrawing();
    ClearBackground(BLACK);
    int win_w = GetScreenWidth();
    int win_h = GetScreenHeight();
    int sx = win_w / CL_SCREEN_W;
    int sy = win_h / CL_SCREEN_H;
    int scale = (sx < sy) ? sx : sy;
    if (scale < 2) scale = 2;
    int dst_w = CL_SCREEN_W * scale;
    int dst_h = CL_SCREEN_H * scale;
    int dst_x = (win_w - dst_w) / 2;
    int dst_y = (win_h - dst_h) / 2;
    Rectangle src = { 0, 0,
                      (float)rt->texture.width,
                      -(float)rt->texture.height };
    Rectangle dst = { (float)dst_x, (float)dst_y,
                      (float)dst_w, (float)dst_h };
    DrawTexturePro(rt->texture, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
    EndDrawing();

    // Backtick captures a screenshot of the target we just drew.
    screenshot_tick(*rt, "shot");
}

static void panel(int x, int y, int w, int h) {
    DrawRectangle(x, y, w, h, PAL_CLR(DBLUE));
    DrawRectangleLines(x, y, w, h, PAL_CLR(YELLOW));
}

// Drain any queued typed characters from raylib's input queue. Call this
// on exit from a screen that commits on a letter key (e.g. class select,
// save-picker shortcuts) so the next screen's name/text input doesn't
// receive the committing keystroke.
static void drain_char_queue(void) {
    while (harness_get_char_pressed() != 0) { /* discard */ }
}

// Cycle raylib's key-edge state to the next frame. harness_key_pressed() compares
// currentKeyState against previousKeyState; both are refreshed on
// PollInputEvents(). Without this call, a key handled in one while-loop
// would still register as "pressed" in the next screen's first iteration
// — e.g. ESC on the name screen would immediately re-fire on the class
// select screen and exit the program.
static void advance_input_frame(void) {
    PollInputEvents();
}

// Helper: true if any key was pressed this frame (other than pure
// modifier keys).  behavior.
static bool any_key_pressed(void) {
    int k = harness_get_key_pressed();
    while (k != 0) {
        if (k != KEY_LEFT_SHIFT && k != KEY_RIGHT_SHIFT &&
            k != KEY_LEFT_CONTROL && k != KEY_RIGHT_CONTROL &&
            k != KEY_LEFT_ALT && k != KEY_RIGHT_ALT &&
            k != KEY_LEFT_SUPER && k != KEY_RIGHT_SUPER &&
            k != KEY_CAPS_LOCK && k != KEY_NUM_LOCK && k != KEY_SCROLL_LOCK) {
            return true;
        }
        k = harness_get_key_pressed();
    }
    return false;
}

// Show a full-screen splash (texture centered on `bg_color`) for 2 seconds,
// or until the player presses any key. Returns false if the window is closed.
static bool run_splash(RenderTexture2D *rt,
                       Texture2D tex,
                       Color bg_color) {
    if (!tex.id) return true;   // Missing asset: skip silently.
    double start_time = GetTime();
    double timeout = 2.5;
    while (!WindowShouldClose()) {
        harness_tick();
        // Auto-advance after 2 seconds or on any key press
        if (GetTime() - start_time >= timeout || any_key_pressed()) return true;

        BeginTextureMode(*rt);
        ClearBackground(bg_color);
        int ix = (CL_SCREEN_W - tex.width)  / 2;
        int iy = (CL_SCREEN_H - tex.height) / 2;
        Rectangle src = { 0, 0, (float)tex.width, (float)tex.height };
        Rectangle dst = { (float)ix, (float)iy,
                          (float)tex.width, (float)tex.height };
        DrawTexturePro(tex, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
        EndTextureMode();

        BeginDrawing();
        ClearBackground(BLACK);
        int win_w = GetScreenWidth();
        int win_h = GetScreenHeight();
        int sx = win_w / CL_SCREEN_W;
        int sy = win_h / CL_SCREEN_H;
        int scale = (sx < sy) ? sx : sy;
        if (scale < 2) scale = 2;
        int dst_w = CL_SCREEN_W * scale;
        int dst_h = CL_SCREEN_H * scale;
        int dst_x = (win_w - dst_w) / 2;
        int dst_y = (win_h - dst_h) / 2;
        Rectangle osrc = { 0, 0,
                           (float)rt->texture.width,
                           -(float)rt->texture.height };
        Rectangle odst = { (float)dst_x, (float)dst_y,
                           (float)dst_w, (float)dst_h };
        DrawTexturePro(rt->texture, osrc, odst,
                       (Vector2){ 0, 0 }, 0.0f, WHITE);
        EndDrawing();

        screenshot_tick(*rt, "shot");
    }
    return false;
}

// ---------------------------------------------------------------------------
// Save-slot picker. Shows 10 rows with each slot's character name +
// class + zone + days-left summary. Slot 10 acts as "New Game".
// Returns: action=LOAD with slot set, or action=NEW with slot set.
// ---------------------------------------------------------------------------

typedef struct {
    SaveHeader hdrs[SAVE_SLOT_COUNT];
    int   existing;
} SlotSet;

static void scan_slots(SlotSet *s) {
    memset(s, 0, sizeof(*s));
    const Resources *r = resources_current();
    const char *pid = (r && r->pack_id[0]) ? r->pack_id : NULL;
    for (int i = 0; i < SAVE_SLOT_COUNT; i++) {
        char path[512];
        if (!SavePathGetSlot(pid, i, path, sizeof(path))) continue;
        if (SaveGameReadHeader(path, &s->hdrs[i]) == SAVE_OK &&
            s->hdrs[i].exists) {
            s->existing++;
        }
    }
}

static bool run_save_picker(RenderTexture2D *rt, const Sprites *sprites,
                            StartupChoice *out) {
    SlotSet slots;
    scan_slots(&slots);

    int cursor = 0;
    // Row index = 0..SAVE_SLOT_COUNT-1 for slots, SAVE_SLOT_COUNT for "New".
    int row_count = SAVE_SLOT_COUNT + 1;
    int new_row   = SAVE_SLOT_COUNT;

    // Cursor lands on the first existing slot, else on "New". Saves are
    // physically segregated by pack (<user-data>/openbounty/saves/<pack_id>/),
    // so every slot we see here belongs to the active pack.
    cursor = new_row;
    for (int i = 0; i < SAVE_SLOT_COUNT; i++) {
        if (slots.hdrs[i].exists) { cursor = i; break; }
    }

    while (!WindowShouldClose()) {
        harness_tick();
        // ---- Input ---------------------------------------------------
        if (harness_key_pressed(KEY_ESCAPE)) {
            // ESC on save picker returns to class select, not quit.
            out->action = STARTUP_BACK;
            advance_input_frame();
            return true;
        }
        if (harness_key_pressed(KEY_UP) || harness_key_pressed(KEY_KP_8)) {
            cursor = (cursor - 1 + row_count) % row_count;
        }
        if (harness_key_pressed(KEY_DOWN) || harness_key_pressed(KEY_KP_2)) {
            cursor = (cursor + 1) % row_count;
        }
        if (harness_key_pressed(KEY_ENTER) || harness_key_pressed(KEY_KP_ENTER) ||
            harness_key_pressed(KEY_SPACE)) {
            if (cursor == new_row) {
                // Find first empty slot for the new game; player can
                // overwrite an occupied slot by selecting it directly
                // from the list (handled in the NEW branch below).
                int target = -1;
                for (int i = 0; i < SAVE_SLOT_COUNT; i++) {
                    if (!slots.hdrs[i].exists) { target = i; break; }
                }
                if (target < 0) target = 0;   // all full: default to slot 0
                out->action = STARTUP_NEW;
                out->slot   = target;
                return true;
            }
            if (slots.hdrs[cursor].exists) {
                out->action = STARTUP_LOAD;
                out->slot   = cursor;
                return true;
            } else {
                // Empty slot chosen directly -> new game into that slot.
                out->action = STARTUP_NEW;
                out->slot   = cursor;
                return true;
            }
        }

        // ---- Render --------------------------------------------------
        frame_begin(rt);
        draw_class_picker_backdrop(sprites);
        draw_class_picker_status_hint(resources_current());

        // Layout: pad / header row / body rows / gap / instruction row / pad.
        int pad      = 6;
        int row_h    = GH + 2;
        int header_h = GH + 4;       // "Select game:" plus a little breathing room
        int gap_h    = 4;            // space between last body row and instructions
        int instr_h  = GH;           // "UP/DN select ..." line
        int body_h   = row_count * row_h;
        int w = 280;
        int h = pad + header_h + body_h + gap_h + instr_h + pad;
        int x = (CL_SCREEN_W - w) / 2;
        int y = (CL_SCREEN_H - h) / 2;
        panel(x, y, w, h);

        const Resources *r = resources_current();
        const ResUI *ui = r ? &r->ui : NULL;
        bfont_draw(ui ? ui->startup_save_picker_title : " Select game:",
                   x + pad, y + pad, PAL_CLR(YELLOW));
        int ty = y + pad + header_h;

        const char *empty_lbl = ui ? ui->startup_save_picker_empty : "(empty)";
        for (int i = 0; i < SAVE_SLOT_COUNT; i++) {
            Color fg = (i == cursor) ? PAL_CLR(YELLOW) : PAL_CLR(WHITE);
            char line[80];
            if (slots.hdrs[i].exists) {
                snprintf(line, sizeof(line),
                         "%2d. %-10s  %-9s  %3dd",
                         i + 1,
                         slots.hdrs[i].name,
                         slots.hdrs[i].rank_title,
                         slots.hdrs[i].days_left);
            } else {
                snprintf(line, sizeof(line), "%2d. %s", i + 1, empty_lbl);
            }
            bfont_draw(line, x + pad, ty, fg);
            ty += row_h;
        }
        // "New game" row — uses the SAME slot in the body grid as the
        // (row_count-1)th row. Already drawn above with the other rows? No —
        // the "New game" row is slot index `new_row == SAVE_SLOT_COUNT`, so
        // it's one extra row past the 10 save slots. The loop above draws
        // 10 rows, leaving `ty` at the "New game" row position.
        Color nfg = (cursor == new_row) ? PAL_CLR(YELLOW) : PAL_CLR(WHITE);
        char ng_line[64];
        snprintf(ng_line, sizeof ng_line, "    %s",
                 ui ? ui->startup_save_picker_new_game : "New game");
        bfont_draw(ng_line, x + pad, ty, nfg);

        // Hint fits in the 33-char content width (280 - 2*pad).
        // Source: res.ui.startup_controls_hint (game.json strings.startup).
        const char *hint = ui ? ui->startup_controls_hint
                              : "UP/DN move  ENTER pick  ESC quit";
        bfont_draw(hint, x + pad, y + h - pad - instr_h, PAL_CLR(GREY));

        frame_end(rt);
    }
    out->action = STARTUP_QUIT;
    return false;
}

// ---------------------------------------------------------------------------
// Class selection. Up to 4 classes from the catalog.
// ---------------------------------------------------------------------------

static bool run_class_select(const Resources *res,
                             const Sprites   *sprites,
                             RenderTexture2D *rt,
                             StartupChoice   *out) {
    int n = res->classes_count;
    if (n < 1) n = 1;
    if (n > 4) n = 4;

    // Center the picker image on the screen.
    int pw = sprites && sprites->class_picker.id
             ? sprites->class_picker.width : 288;
    int ph = sprites && sprites->class_picker.id
             ? sprites->class_picker.height : 184;
    int px = (CL_SCREEN_W - pw) / 2;
    int py = (CL_SCREEN_H - ph) / 2;

    while (!WindowShouldClose()) {
        harness_tick();
        if (harness_key_pressed(KEY_ESCAPE)) {
            out->action = STARTUP_QUIT;
            return false;
        }
        // L for Load 
        if (harness_key_pressed(KEY_L)) {
            out->action = STARTUP_LOAD;
            drain_char_queue();   // don't leak the 'L' into name entry
            return true;
        }
        // A/B/C/D pick directly .
        static const int keys[4] = { KEY_A, KEY_B, KEY_C, KEY_D };
        for (int k = 0; k < n; k++) {
            if (harness_key_pressed(keys[k])) {
                const ClassDef *c = class_by_index(k);
                safe_copy(out->class_id, sizeof(out->class_id),
                          c ? c->id : "knight");
                out->action = STARTUP_NEW;
                drain_char_queue();  // don't leak 'A/B/C/D' into name entry
                return true;
            }
        }

        frame_begin(rt);
        // Background: solid black.
        DrawRectangle(0, 0, CL_SCREEN_W, CL_SCREEN_H, PAL_CLR(BLACK));

        // Picker bitmap, centered.
        if (sprites && sprites->class_picker.id) {
            Texture2D t = sprites->class_picker;
            Rectangle src = { 0, 0, (float)t.width, (float)t.height };
            Rectangle dst = { (float)px, (float)py,
                              (float)pw, (float)ph };
            DrawTexturePro(t, src, dst, (Vector2){0,0}, 0.0f, WHITE);
        } else {
            // Fallback: text list if asset missing.
            const char *miss = res ? res->ui.startup_class_picker_missing
                                   : "class picker asset missing";
            bfont_draw(miss, 40, 90, PAL_CLR(YELLOW));
        }

        // Status-bar hint at top ().
        DrawRectangle(0, 0, CL_SCREEN_W, GH + 2, PAL_CLR(DRED));
        const char *hint = res ? res->ui.startup_class_select_hint
                               : "Select Char A-D or L-Load saved game";
        bfont_draw_centered(hint, CL_SCREEN_W / 2, 1, PAL_CLR(WHITE));

        frame_end(rt);
    }
    out->action = STARTUP_QUIT;
    return false;
}

// ---------------------------------------------------------------------------
// Combined name + difficulty entry.
//
// Panel: 30 cols × 12 rows in 8x8 font cells = 240 × 96 px, centered.
// Row layout (1-indexed):
//   row 1: " Knight    Name: [text_input 10 chars at col 18]"
//   row 3: "   Difficulty   Days  Score"
//   row 4: (blank)
//   row 5: "   Easy         900    x.5 "
//   row 6: "   Normal       600     x1 "
//   row 7: "   Hard         400     x2 "
//   row 8: "   Impossible?  200     x4 "
//   row 10 (only when has_name): "^v to select   Ent to Accept"
//   rows 5-8 col 0 (only when has_name): ">" next to the selected row.
// ---------------------------------------------------------------------------

static bool run_create_game(const Resources *res,
                            const Sprites   *sprites,
                            RenderTexture2D *rt,
                            StartupChoice   *out) {
    char name_buf[11] = { 0 };
    int  name_len = 0;
    bool has_name = false;
    int  sel = 1;             // initial difficulty is Normal
    double cursor_blink = 0;

    // Look up class title via out->class_id (set by run_class_select).
    const ClassDef *cls = class_by_id(out->class_id);
    const char *class_title = cls ? cls->name : "Hero";

    // Labels + score-multiplier text from res.ui.difficulty (game.json
    // strings.difficulty); enum order matches the JSON key order.
    struct {
        const char *label;   // %-11s 
        const char *score;   // 3-char right-aligned score multiplier
        Difficulty  diff;
    } rows[4];
    static const Difficulty diff_order[4] = {
        DIFFICULTY_EASY, DIFFICULTY_NORMAL,
        DIFFICULTY_HARD, DIFFICULTY_IMPOSSIBLE,
    };
    for (int i = 0; i < 4; i++) {
        rows[i].label = (res ? res->ui.difficulty[i].label : "");
        rows[i].score = (res ? res->ui.difficulty[i].score_mult : "");
        rows[i].diff  = diff_order[i];
    }
    const int n = 4;

    while (!WindowShouldClose()) {
        harness_tick();
        if (harness_key_pressed(KEY_ESCAPE)) {
            // ESC on new-game screen returns to class select, not quit.
            out->action = STARTUP_BACK;
            advance_input_frame();
            return true;
        }

        if (!has_name) {
            // Name entry phase. Enter confirms; BACKSPACE deletes; alpha/
            // digit/space appends.
            if (harness_key_pressed(KEY_ENTER) || harness_key_pressed(KEY_KP_ENTER)) {
                if (name_len == 0) {
                    // We default to world.default_name on empty name
                    // (from game.json) so the flow always completes.
                    const char *dn = (res && res->world.default_name[0])
                        ? res->world.default_name : "Hero";
                    safe_copy(name_buf, sizeof(name_buf), dn);
                    name_len = (int)strlen(name_buf);
                }
                // name[0] = toupper(name[0])
                if (name_buf[0] >= 'a' && name_buf[0] <= 'z') {
                    name_buf[0] = (char)(name_buf[0] - 'a' + 'A');
                }
                has_name = true;
            } else if (harness_key_pressed(KEY_BACKSPACE) && name_len > 0) {
                name_buf[--name_len] = '\0';
            } else {
                int ch = harness_get_char_pressed();
                while (ch > 0 && name_len < 10) {
                    bool allowed = (ch >= 'A' && ch <= 'Z') ||
                                   (ch >= 'a' && ch <= 'z') ||
                                   (ch >= '0' && ch <= '9') ||
                                   ch == ' ';
                    if (allowed) {
                        name_buf[name_len++] = (char)ch;
                        name_buf[name_len] = '\0';
                    }
                    ch = harness_get_char_pressed();
                }
            }
        } else {
            // Difficulty selection phase. Arrows clamp :
            //   sel--; if (sel < 0) sel = 0;
            //   sel++; if (sel > 3) sel = 3;
            if (harness_key_pressed(KEY_UP) || harness_key_pressed(KEY_KP_8)) {
                if (sel > 0) sel--;
            }
            if (harness_key_pressed(KEY_DOWN) || harness_key_pressed(KEY_KP_2)) {
                if (sel < n - 1) sel++;
            }
            if (harness_key_pressed(KEY_ENTER) || harness_key_pressed(KEY_KP_ENTER)) {
                out->difficulty = rows[sel].diff;
                safe_copy(out->name, sizeof(out->name), name_buf);
                out->action = STARTUP_NEW;
                return true;
            }
        }

        cursor_blink += GetFrameTime();
        bool show_caret = (int)(cursor_blink * 2.0) & 1;

        // ---- Render  ----
        frame_begin(rt);
        draw_class_picker_backdrop(sprites);
        draw_class_picker_status_hint(res);

        int w = 30 * GW;           // 240
        int h = 12 * GH;           //  96
        int x = (CL_SCREEN_W - w) / 2;
        int y = (CL_SCREEN_H - h) / 2;
        panel(x, y, w, h);

        // Row offsets: menu.y + fs->h * N. Row 1 (first inner row)
        // is at fs->h. We mirror that — text drawn at y + GH*N (N = 1..10).
        #define ROW_Y(n) (y + GH * (n))

        // Row 1: " Knight    Name: [cursor]name_buf"
        // Format: " %-9s Name: " using class title.
        // text_input is drawn separately at menu.x + fs->w*18 (column 18).
        {
            char prefix[64];
            snprintf(prefix, sizeof(prefix), " %-9s Name: ", class_title);
            bfont_draw(prefix, x + GW, ROW_Y(1), PAL_CLR(WHITE));
        }
        // Text input at column 18.
        int name_x = x + GW * 18;
        int name_y = ROW_Y(1);
        bfont_draw(name_buf, name_x, name_y, PAL_CLR(WHITE));
        if (!has_name && show_caret && name_len < 10) {
            // Blinking caret after the last typed char.
            int cx = name_x + name_len * GW;
            DrawRectangle(cx, name_y, 1, GH, PAL_CLR(YELLOW));
        }

        // Row 3: difficulty table header.
        bfont_draw(res ? res->ui.startup_new_game_table_header
                       : "   Difficulty   Days  Score",
                   x + GW, ROW_Y(3), PAL_CLR(WHITE));

        // Rows 5-8: difficulty table:
        //   "   Easy         %3d    x.5 "  (11-char label, %3d days, score)
        for (int i = 0; i < n; i++) {
            int days = res ? res->time.days_per_difficulty[i] : 0;
            char line[32];
            snprintf(line, sizeof(line), "   %-11s %3d    %s",
                     rows[i].label, days, rows[i].score);
            bfont_draw(line, x + GW, ROW_Y(5 + i), PAL_CLR(WHITE));
        }

        // After has_name: draw the ">" cursor at col 0 of the selected row,
        // and the "^v to select   Ent to Accept" hint on row 10.
        if (has_name) {
            // ">" cursor — print sel==i ? ">\n" : " \n" at
            //   menu.x + fs->w, menu.y + fs->h * 5  (i.e. col 1, row 5)
            // and walks down 4 rows. The cursor column is the same column as
            // the "   " prefix in the difficulty lines (col 1).
            bfont_draw(">", x + GW, ROW_Y(5 + sel), PAL_CLR(WHITE));

            // Hint on row 10: "\x18\x19 to select   Ent to Accept"
            // — 0x18 and 0x19 are CP437 up/down arrows. Our bfont is ASCII-
            // only, so substitute "^v".
            bfont_draw(res ? res->ui.startup_new_game_select_hint
                           : "^v to select   Ent to Accept",
                       x + GW, ROW_Y(10), PAL_CLR(WHITE));
        }

        #undef ROW_Y
        frame_end(rt);
    }
    out->action = STARTUP_QUIT;
    return false;
}

// ---------------------------------------------------------------------------
// Credits screen — .
// Shown once between the title splash and the class picker. Dismissed
// by any key or after a 6-second timeout. %VERSION% in any line is
// substituted with res->version.
// ---------------------------------------------------------------------------

static void expand_version(char *out, int out_sz, const char *src,
                           int version) {
    if (!out || out_sz <= 0) return;
    out[0] = '\0';
    if (!src) return;
    int o = 0;
    while (*src && o + 1 < out_sz) {
        if (strncmp(src, "%VERSION%", 9) == 0) {
            int n = snprintf(out + o, out_sz - o, "%d", version);
            if (n < 0) break;
            o += n;
            src += 9;
        } else {
            out[o++] = *src++;
        }
    }
    out[o] = '\0';
}

// Last step: a "new game is being
// created" panel over the class-select cartoon. Any key advances; we
// also auto-advance after a short timeout so harness/replay flows
// don't stall.
static bool run_new_game_intro(RenderTexture2D *rt,
                               const Resources *res,
                               const Sprites   *sprites,
                               const StartupChoice *out,
                               const char *name) {
    if (!res || !res->banners.new_game_intro[0]) return true;

    const ClassDef *cls = class_by_id(out->class_id);
    const char *class_title = cls ? cls->name : "Hero";

    char body[RES_BANNER_LEN];
    ResTemplateVar vars[2] = {
        { "NAME",  name && name[0] ? name : "Hero" },
        { "CLASS", class_title },
    };
    resources_format_template(body, sizeof(body),
                              res->banners.new_game_intro, vars, 2);

    // Pre-compute panel size from the body's longest line.
    int line_count = 1;
    int max_chars = 0;
    {
        int cur = 0;
        for (const char *p = body; *p; p++) {
            if (*p == '\n') {
                if (cur > max_chars) max_chars = cur;
                cur = 0;
                line_count++;
            } else {
                cur++;
            }
        }
        if (cur > max_chars) max_chars = cur;
    }
    int line_h = GH;
    int pad    = 8;
    int panel_w = max_chars * GW + pad * 2;
    int panel_h = line_count * line_h + pad * 2;
    int px = (CL_SCREEN_W - panel_w) / 2;
    int py = (CL_SCREEN_H - panel_h) / 2;

    double start = GetTime();
    double timeout = 4.0;
    while (!WindowShouldClose()) {
        harness_tick();
        if (any_key_pressed() || (GetTime() - start) >= timeout) return true;

        frame_begin(rt);

        // Background: same class-select cartoon (sprites->class_picker).
        if (sprites && sprites->class_picker.id) {
            Texture2D t = sprites->class_picker;
            int pw = t.width;
            int ph = t.height;
            int bx = (CL_SCREEN_W - pw) / 2;
            int by = (CL_SCREEN_H - ph) / 2;
            Rectangle src = { 0, 0, (float)pw, (float)ph };
            Rectangle dst = { (float)bx, (float)by, (float)pw, (float)ph };
            DrawTexturePro(t, src, dst, (Vector2){0,0}, 0.0f, WHITE);
        }

        // Status hint at top.
        DrawRectangle(0, 0, CL_SCREEN_W, GH + 2, PAL_CLR(DRED));
        const char *hint = res->ui.startup_class_select_hint[0]
            ? res->ui.startup_class_select_hint
            : "Select Char A-D or L-Load saved game";
        bfont_draw_centered(hint, CL_SCREEN_W / 2, 1, PAL_CLR(WHITE));

        // Panel.
        panel(px, py, panel_w, panel_h);

        int y = py + pad;
        const char *p = body;
        while (*p) {
            const char *e = p;
            while (*e && *e != '\n') e++;
            char line[128];
            int n = (int)(e - p);
            if (n >= (int)sizeof(line)) n = sizeof(line) - 1;
            memcpy(line, p, n);
            line[n] = '\0';
            bfont_draw(line, px + pad, y, PAL_CLR(WHITE));
            y += line_h;
            if (*e == '\n') e++;
            p = e;
        }

        frame_end(rt);
    }
    return false;
}

static bool run_credits(RenderTexture2D *rt, const Resources *res,
                        const Sprites *sprites) {
    if (!res) return true;
    int gn = res->credits.group_count;
    int cn = res->credits.copyright_count;
    if (gn == 0 && cn == 0) return true;

    // Title-page credit layout: a yellow-chrome bordered blue panel
    // sitting over the title splash, with group labels left-aligned,
    // names indented one space, the class-select highlight sprite inset
    // on the right, and copyright lines centered at the bottom.

    // Panel is sized to fit the screen with margin.
    int line_h = GH + 1;
    int pad    = 6;

    // Compute width: longest of all lines (labels, names, copyright)
    // plus padding for the inset image.
    int max_text_chars = 0;
    for (int g = 0; g < gn; g++) {
        int L = (int)strlen(res->credits.groups[g].label);
        if (L > max_text_chars) max_text_chars = L;
        for (int n = 0; n < res->credits.groups[g].name_count; n++) {
            int Ln = (int)strlen(res->credits.groups[g].names[n]) + 2; // 2 spaces indent
            if (Ln > max_text_chars) max_text_chars = Ln;
        }
    }
    for (int c = 0; c < cn; c++) {
        int L = (int)strlen(res->credits.copyright[c]);
        if (L > max_text_chars) max_text_chars = L;
    }

    int image_w = 0, image_h = 0;
    Texture2D inset = (Texture2D){ 0 };
    if (sprites && res->credits.image[0]) {
        // The credit inset re-uses the class-select highlight sprite.
        if (sprites->class_highlight.id) {
            inset = sprites->class_highlight;
            image_w = inset.width;
            image_h = inset.height;
        }
    }

    int text_w  = max_text_chars * GW;
    int gap     = image_w ? 8 : 0;
    int panel_w = pad + text_w + gap + image_w + pad;
    if (panel_w > CL_SCREEN_W - 8) panel_w = CL_SCREEN_W - 8;

    // Compute height: groups (label + names) + spacer + copyright.
    int rows = 0;
    for (int g = 0; g < gn; g++) {
        rows += 1 + res->credits.groups[g].name_count;
        if (g + 1 < gn) rows += 1; // blank line between groups
    }
    int copyright_rows = cn;
    int spacer_before_copyright = (gn && cn) ? 1 : 0;
    int total_rows = rows + spacer_before_copyright + copyright_rows;
    int panel_h = pad * 2 + total_rows * line_h;
    int min_panel_h = pad * 2 + image_h;
    if (panel_h < min_panel_h) panel_h = min_panel_h;

    int px = (CL_SCREEN_W - panel_w) / 2;
    int py = (CL_SCREEN_H - panel_h) / 2;

    double start = GetTime();
    double timeout = 2.5;
    while (!WindowShouldClose()) {
        harness_tick();
        if (any_key_pressed() || (GetTime() - start) >= timeout) return true;

        frame_begin(rt);

        // Backdrop: the class-select cartoon -- credits drape over the
        // character-pick screen.
        draw_class_picker_backdrop(sprites);
        draw_class_picker_status_hint(res);

        panel(px, py, panel_w, panel_h);

        // Text column.
        int tx = px + pad;
        int ty = py + pad;
        for (int g = 0; g < gn; g++) {
            bfont_draw(res->credits.groups[g].label, tx, ty, PAL_CLR(WHITE));
            ty += line_h;
            for (int n = 0; n < res->credits.groups[g].name_count; n++) {
                bfont_draw(res->credits.groups[g].names[n],
                           tx + 2 * GW, ty, PAL_CLR(WHITE));
                ty += line_h;
            }
            if (g + 1 < gn) ty += line_h; // blank line between groups
        }
        if (spacer_before_copyright) ty += line_h;
        for (int c = 0; c < cn; c++) {
            char line[64];
            expand_version(line, sizeof(line),
                           res->credits.copyright[c], res->version);
            int tw = (int)strlen(line) * GW;
            int cx = px + (panel_w - tw) / 2;
            bfont_draw(line, cx, ty, PAL_CLR(WHITE));
            ty += line_h;
        }

        // Inset image (re-using class_select_highlight). Vertically
        // centered against the group block, with a thin green outline.
        if (image_w) {
            int ix = px + panel_w - pad - image_w;
            int iy = py + pad + line_h;
            if (iy + image_h > py + panel_h - pad) {
                iy = py + panel_h - pad - image_h;
            }
            Rectangle src = { 0, 0, (float)image_w, (float)image_h };
            Rectangle dst = { (float)ix, (float)iy,
                              (float)image_w, (float)image_h };
            DrawTexturePro(inset, src, dst, (Vector2){0,0}, 0.0f, WHITE);
            DrawRectangleLines(ix - 1, iy - 1,
                               image_w + 2, image_h + 2,
                               PAL_CLR(DGREEN));
        }

        frame_end(rt);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Top-level flow.
// ---------------------------------------------------------------------------

bool startup_flow(const Resources *res,
                          const Sprites   *sprites,
                          void            *chrome_target,
                          StartupChoice   *out) {
    RenderTexture2D *rt = (RenderTexture2D *)chrome_target;
    memset(out, 0, sizeof(*out));

    // Always show class select first

    // Splash 1: publisher logo on black.
    if (sprites && !run_splash(rt, sprites->splash_logo,
                               (Color){ 0x00, 0x00, 0x00, 0xFF })) {
        out->action = STARTUP_QUIT;
        return false;
    }

    // Splash 2: game title on black .
    if (sprites && !run_splash(rt, sprites->splash_title,
                               (Color){ 0x00, 0x00, 0x00, 0xFF })) {
        out->action = STARTUP_QUIT;
        return false;
    }

    // Credits screen . Drawn over the
    // class-select cartoon so the picker is visible behind. Skipped
    // silently if the game pack doesn't define any credit lines.
    if (!run_credits(rt, res, sprites)) {
        out->action = STARTUP_QUIT;
        return false;
    }

    // Main startup loop: class select is the root. Sub-screens (save
    // picker, new-game) ESC back here; only ESC at class select exits.
    for (;;) {
        if (!run_class_select(res, sprites, rt, out)) return false;

        if (out->action == STARTUP_LOAD) {
            // User pressed L: show save picker. May return STARTUP_LOAD
            // (existing slot), STARTUP_NEW (empty slot / "New game" row),
            // or STARTUP_BACK (ESC → back to class select).
            if (!run_save_picker(rt, sprites, out)) return false;
            if (out->action == STARTUP_BACK) continue;   // back to class select
            if (out->action == STARTUP_LOAD) break;      // done, load path
            // else STARTUP_NEW — fall through to create_game for the slot
        }

        // New-game path . ESC here also returns to
        // class select.
        if (!run_create_game(res, sprites, rt, out)) return false;
        if (out->action == STARTUP_BACK) continue;
        break;   // STARTUP_NEW set by run_create_game on Enter
    }

    // New-game intro screen: "<Name> the <Class>, A new game is
    // being created. Please wait while I perform godlike actions to make
    // this game playable." Auto-advances after a few seconds.
    if (out->action == STARTUP_NEW) {
        run_new_game_intro(rt, res, sprites, out, out->name);
    }

    out->seed = (unsigned long)time(NULL);
    return true;
}
