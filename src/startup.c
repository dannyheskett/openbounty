#include "frame_host.h"
#include "gfx.h"
#include "input_host.h"
#include "startup.h"
#include "touch.h"
#include "layout.h"
#include "modern/mlayout.h"
#include "modern/mlist.h"
#include "modern/uikit.h"
#include "modern/saveslots.h"
#include "modern/gamemenu.h"
#include "lattice.h"
#include "present.h"
#include "palette.h"
#include "chrome.h"
#include "bfont.h"
#include "savegame.h"
#include "screenshot.h"
#include "ui.h"
#include "select.h"
#include "textsel.h"
#include "tables.h"
#include "resources.h"
#include "ob_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GW BFONT_GLYPH_W
#define GH BFONT_GLYPH_H

// ---------------------------------------------------------------------------
// Local frame boilerplate: begin the 320x200 render texture, clear to
// black, and end after the caller has drawn into it. We don't blit the
// chrome bitmap -- the pre-game screens are full-panel modal dialogs.
// ---------------------------------------------------------------------------

static void safe_copy(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = 0;
    while (n + 1 < dst_sz && src[n]) { dst[n] = src[n]; n++; }
    dst[n] = '\0';
}

static void frame_begin(RenderTexture2D *rt) {
    // The startup loop runs before the main loop and owns the same target, so
    // it has to re-fit too. Without this, resizing the window during character
    // select leaves the buffer at its launch size and present_scaled crops it.
    present_refit(rt);
    present_begin(rt);
    gfx_clear(PAL_CLR(BLACK));
    ml_set_area(ML_AREA_SCREEN);   // startup panels centre on the whole screen
}

// Draw the class-select cartoon as the screen backdrop, so dialog
// panels (credits, save picker, new-game name/difficulty, new-game
// intro) sit visually over the character-selection screen.
static void draw_class_picker_backdrop(const Sprites *sprites) {
    if (!sprites || !sprites->class_picker.id) return;
    Texture2D t = sprites->class_picker;
    int fs = ui_fit_scale(t.width, t.height, CL_SCREEN_W, CL_SCREEN_H);
    int pw = t.width  * fs;
    int ph = t.height * fs;
    ui_blit(t, (CL_SCREEN_W - pw) / 2, (CL_SCREEN_H - ph) / 2, pw, ph);
}

static void draw_class_picker_status_hint(const Resources *res) {
    gfx_rect(0, 0, CL_SCREEN_W, GH + 2, PAL_CLR(DRED));
    bfont_draw_centered(res->ui.startup_class_select_hint,
                        CL_SCREEN_W / 2, 1, PAL_CLR(WHITE));
}

static void frame_end(RenderTexture2D *rt) {
    present_end();

    present_scaled(*rt);
    frame_host_end_frame();

    // Backtick captures a screenshot of the target we just drew.
    screenshot_tick(*rt, "shot");
}

static void panel(int x, int y, int w, int h) {
    if (CL_IS_MODERN) { uk_panel(x, y, w, h); return; }
    gfx_rect(x, y, w, h, PAL_CLR(DBLUE));
    ui_window_frame(x, y, w, h, PAL_CLR(YELLOW));
}

// Drain any queued typed characters from raylib's input queue. Call this
// on exit from a screen that commits on a letter key (e.g. class select,
// save-picker shortcuts) so the next screen's name/text input doesn't
// receive the committing keystroke.
static void drain_char_queue(void) {
    while (input_get_char_pressed() != 0) { /* discard */ }
}

// Cycle raylib's key-edge state to the next frame. input_key_pressed() compares
// currentKeyState against previousKeyState; both are refreshed on
// PollInputEvents(). Without this call, a key handled in one while-loop
// would still register as "pressed" in the next screen's first iteration
// -- e.g. ESC on the name screen would immediately re-fire on the class
// select screen and exit the program.
static void advance_input_frame(void) {
    frame_host_poll_events();
    // Injected (touch) keys have the same one-frame-edge hazard: an ESC
    // injected for this screen must not re-fire in the next screen's loop.
    input_host_clear_injected();
}

// Every startup screen opens with this: keys pressed during the screen before,
// or while loading, must not answer this one.
#define SCREEN_GUARD 0.25
static void screen_open(void) { input_host_flush(SCREEN_GUARD); }

// Modern title sequence (REQ-430p), in the 256x164 art's own pixels: the
// title words and eagle standard on purple, the battle fades in behind them,
// then the eagle slides left and the menu appears. Played once per run; any
// key or tap skips to the end, and coming back to the title shows the end.
#define TITLE_EAGLE_X0   80
#define TITLE_EAGLE_X1    1    // left wing (art column 2) three pixels in from the edge
#define TITLE_EAGLE_Y    18
#define TITLE_HOLD      1.0    // seconds on purple
#define TITLE_FADED     2.5    // battle fully in
#define TITLE_END       3.5    // eagle in place, menu up
#define TITLE_MENU_IN   3.0    // the menu fades in from here to TITLE_END
static bool s_title_played;

static bool title_sequence_ok(const Sprites *s) {
    return CL_IS_MODERN && s && s->title_battle.id && s->title_eagle.id && s->title_words.id;
}

static float title_phase(double t, double from, double to) {
    float f = (float)((t - from) / (to - from));
    return f < 0 ? 0 : f > 1 ? 1 : f;
}

static void draw_title_sequence(const Sprites *s, double t) {
    Texture2D b = s->title_battle, e = s->title_eagle;
    int fs = ui_fit_scale(b.width, b.height, CL_SCREEN_W, CL_SCREEN_H);
    int pw = b.width * fs, ph = b.height * fs;
    int ox = (CL_SCREEN_W - pw) / 2, oy = (CL_SCREEN_H - ph) / 2;
    gfx_rect(ox, oy, pw, ph, (Color){ 65, 9, 104, 255 });
    unsigned char a = (unsigned char)(255 * title_phase(t, TITLE_HOLD, TITLE_FADED));
    gfx_texture_draw(b, (Rectangle){ 0, 0, (float)b.width, (float)b.height },
                   (Rectangle){ (float)ox, (float)oy, (float)pw, (float)ph }, (Color){ 255, 255, 255, a });
    // Eased slide, whole art pixels so the eagle stays on the art's grid.
    float k = title_phase(t, TITLE_FADED, TITLE_END);
    k = k * k * (3 - 2 * k);
    int ex = TITLE_EAGLE_X0 + (int)((TITLE_EAGLE_X1 - TITLE_EAGLE_X0) * k - 0.5f);
    int z = present_get_zoom();
    gfx_clip_begin(ox * z, oy * z, pw * z, ph * z);   // the eagle leaves the art's edge
    ui_blit(e, ox + ex * fs, oy + TITLE_EAGLE_Y * fs, e.width * fs, e.height * fs);
    gfx_clip_end();
    ui_blit(s->title_words, ox, oy, pw, ph);
}

// The title art as the backdrop (modern: title menu, credits, save picker).
static void draw_title_backdrop(const Sprites *sprites) {
    if (title_sequence_ok(sprites)) { draw_title_sequence(sprites, TITLE_END); return; }
    if (!sprites || !sprites->splash_title.id) return;
    Texture2D t = sprites->splash_title;
    int fs = ui_fit_scale(t.width, t.height, CL_SCREEN_W, CL_SCREEN_H);
    int pw = t.width * fs, ph = t.height * fs;
    ui_blit(t, (CL_SCREEN_W - pw) / 2, (CL_SCREEN_H - ph) / 2, pw, ph);
}

// Helper: true if any key was pressed this frame (other than pure
// modifier keys).  behavior.
static bool any_key_pressed(void) {
    touch_region_any(KEY_ENTER);   // a tap counts as any key
    int k = input_get_key_pressed();
    while (k != 0) {
        if (k != KEY_LEFT_SHIFT && k != KEY_RIGHT_SHIFT &&
            k != KEY_LEFT_CONTROL && k != KEY_RIGHT_CONTROL &&
            k != KEY_LEFT_ALT && k != KEY_RIGHT_ALT &&
            k != KEY_LEFT_SUPER && k != KEY_RIGHT_SUPER &&
            k != KEY_CAPS_LOCK && k != KEY_NUM_LOCK && k != KEY_SCROLL_LOCK) {
            return true;
        }
        k = input_get_key_pressed();
    }
    return false;
}

// Show a full-screen splash (texture centered on `bg_color`) for 2 seconds,
// or until the player presses any key. Returns false if the window is closed.
static bool run_splash(RenderTexture2D *rt,
                       Texture2D tex,
                       Color bg_color) {
    if (!tex.id) return true;   // Missing asset: skip silently.
    screen_open();
    double start_time = frame_host_time();
    double timeout = 2.5;
    while (!frame_host_should_close()) {
        // Auto-advance after 2 seconds or on any key press
        if (frame_host_time() - start_time >= timeout || any_key_pressed()) return true;

        frame_begin(rt);
        gfx_clear(bg_color);
        // Splash art is authored in the 320x200 design space; legacy draws
        // it at 1x, modern at the largest whole scale the buffer holds.
        int fs = ui_fit_scale(tex.width, tex.height, CL_SCREEN_W, CL_SCREEN_H);
        int iw = tex.width  * fs;
        int ih = tex.height * fs;
        ui_blit(tex, (CL_SCREEN_W - iw) / 2, (CL_SCREEN_H - ih) / 2, iw, ih);
        present_end();

        present_scaled(*rt);
        frame_host_end_frame();

        screenshot_tick(*rt, "shot");
    }
    return false;
}

// ---------------------------------------------------------------------------
// Save-slot picker. Shows 10 rows with each slot's character name +
// class + zone + days-left summary. Slot 10 acts as "New Game".
// Returns: action=LOAD with slot set, or action=NEW with slot set.
// ---------------------------------------------------------------------------

// The slots and their rows: src/modern/saveslots.c (the in-game menu uses them too).

static bool title_row(void *ctx, int i, char *label, char *right, int cap) {
    const char **labels = (const char **)ctx;
    right[0] = '\0';
    snprintf(label, (size_t)cap, "%s", labels[i]);
    return true;
}

typedef struct {
    const Resources *res;
    const char *labels[4];
    const char *scores[4];
    bool has_name;
} DiffCtx;

static bool difficulty_row(void *ctx, int i, char *label, char *right, int cap) {
    const DiffCtx *d = (const DiffCtx *)ctx;
    char sc[8];
    snprintf(sc, sizeof sc, "%s", d->scores[i]);
    char *p = sc;
    while (*p == ' ') p++;
    snprintf(label, (size_t)cap, "%-12.12s %4d", d->labels[i],
             d->res->time.days_per_difficulty[i]);
    snprintf(right, 48, "%s", p);
    return true;          // readable before a name; chosen only after one
}

// Modern: the slot rows, then Back.
typedef struct { const GmPage *page; const SlotSet *slots; } TitleSlotsCtx;

static bool title_slot_row(void *ctx, int i, char *label, char *right, int cap) {
    const TitleSlotsCtx *c = (const TitleSlotsCtx *)ctx;
    const GmPage *p = c->page;
    if (i < MODERN_SAVE_SLOTS) {
        saveslots_row((void *)c->slots, i, label, right, cap);
    } else {
        snprintf(label, (size_t)cap, "%s", p->item[i].label ? p->item[i].label : "");
        right[0] = '\0';
    }
    return p->item[i].enabled;
}

// Modern: the title's Load Saved Game is the in-game Load page -- the same
// panel, width and rows (gm_draw_page), with "Load Saved Game" for its path.
static bool run_save_picker_modern(RenderTexture2D *rt, const Sprites *sprites,
                                   StartupChoice *out, const SlotSet *slots) {
    const Resources *r = resources_current();
    if (!r) { out->action = STARTUP_BACK; return true; }
    const ResUI *ui = &r->ui;
    const ResBanners *bn = &r->banners;
    GmPage p;
    memset(&p, 0, sizeof p);
    p.title = ui->title_load_adventure;
    for (int i = 0; i < MODERN_SAVE_SLOTS; i++)
        p.item[p.n++] = (GmItem){ "", bn->gmd_load, "", GM_ACT_USER + i,
                                  slots->hdrs[i].exists };
    p.item[p.n++] = (GmItem){ ui->gm_back, bn->gmd_back_up, "", GM_ACT_BACK, true };

    TitleSlotsCtx ctx = { &p, slots };
    int cursor = MODERN_SAVE_SLOTS;   // Back, unless a save exists
    for (int i = 0; i < MODERN_SAVE_SLOTS; i++)
        if (slots->hdrs[i].exists) { cursor = i; break; }

    while (!frame_host_should_close()) {
        GmEvent ev = gm_page_input(&p, &cursor, TOUCH_LIST_STARTUP);
        if (ev == GM_EV_BACK || (ev == GM_EV_ACT && p.item[cursor].key == GM_ACT_BACK)) {
            out->action = STARTUP_BACK;
            advance_input_frame();
            return true;
        }
        if (ev == GM_EV_ACT) {
            out->action = STARTUP_LOAD;
            out->slot   = cursor;
            return true;
        }
        frame_begin(rt);
        draw_title_backdrop(sprites);
        gfx_rect(0, 0, CL_SCREEN_W, CL_SCREEN_H, (Color){ 0, 0, 0, 110 });
        // The title screen has no chrome around the pane, so the page centres
        // on the screen; its width and rows are the menu's (gm_draw_page).
        ml_set_area(ML_AREA_SCREEN);
        gm_draw_page(&p, p.title, NULL, cursor, TOUCH_LIST_STARTUP, title_slot_row, &ctx);
        frame_end(rt);
    }
    out->action = STARTUP_QUIT;
    return false;
}

static bool run_save_picker(RenderTexture2D *rt, const Sprites *sprites,
                            StartupChoice *out) {
    SlotSet slots;
    saveslots_scan(&slots);
    screen_open();
    if (CL_IS_MODERN) return run_save_picker_modern(rt, sprites, out, &slots);

    int cursor = 0;
    // Row index = 0..SAVE_SLOT_COUNT-1 for slots, SAVE_SLOT_COUNT for "New".
    // Modern has no "New game" row: New Game lives on the title menu.
    int nslots    = CL_IS_MODERN ? MODERN_SAVE_SLOTS : SAVE_SLOT_COUNT;
    int row_count = nslots + (CL_IS_MODERN ? 0 : 1);
    int new_row   = CL_IS_MODERN ? -1 : SAVE_SLOT_COUNT;

    // Cursor lands on the first existing slot, else on "New". Saves are
    // physically segregated by pack (<user-data>/openbounty/saves/<pack_id>/),
    // so every slot we see here belongs to the active pack.
    cursor = CL_IS_MODERN ? 0 : new_row;
    for (int i = 0; i < nslots; i++) {
        if (slots.hdrs[i].exists) { cursor = i; break; }
    }

    while (!frame_host_should_close()) {
        // ---- Input ---------------------------------------------------
        touch_request(TOUCH_CHROME_BACK);
        // Touch: tapping a row selects and confirms it in one go.
        int tapped = touch_tapped_row(TOUCH_LIST_STARTUP);
        if (tapped >= 0 && tapped < row_count) cursor = tapped;
        if (input_key_pressed(KEY_ESCAPE)) {
            // ESC on save picker returns to class select, not quit.
            out->action = STARTUP_BACK;
            advance_input_frame();
            return true;
        }
        if (input_key_pressed(KEY_UP) || input_key_pressed(KEY_KP_8)) {
            cursor = (cursor - 1 + row_count) % row_count;
        }
        if (input_key_pressed(KEY_DOWN) || input_key_pressed(KEY_KP_2)) {
            cursor = (cursor + 1) % row_count;
        }
        if (tapped >= 0 ||
            input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER) ||
            input_key_pressed(KEY_SPACE)) {
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
            } else if (!CL_IS_MODERN) {
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
        int pad      = 6 * CL_UI;
        int row_h    = GH + 2 * CL_UI;
        int header_h = GH + 4 * CL_UI;  // title plus a little breathing room
        int gap_h    = 4 * CL_UI;       // last body row to instructions
        int instr_h  = GH;              // "UP/DN select ..." line
        int body_h   = (SAVE_SLOT_COUNT + 1) * row_h;
        int w = 280 * CL_UI;
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
            if (CL_IS_MODERN) {
                sel_row(x, ty, w, row_h, x + pad, line, i == cursor, fg, PAL_CLR(DBLUE),
                        TOUCH_LIST_STARTUP, i);
            } else {
                bfont_draw(line, x + pad, ty, fg);
                touch_region_row(x, ty, w, row_h, TOUCH_LIST_STARTUP, i);
            }
            ty += row_h;
        }
        // "New game" row -- slot index `new_row == SAVE_SLOT_COUNT`, one row
        // past the save slots. The loop above draws exactly SAVE_SLOT_COUNT
        // rows and leaves `ty` sitting on this one, so it needs no further
        // advance.
        Color nfg = (cursor == new_row) ? PAL_CLR(YELLOW) : PAL_CLR(WHITE);
        char ng_line[64];
        snprintf(ng_line, sizeof ng_line, "    %s",
                 ui->startup_save_picker_new_game);
        if (!CL_IS_MODERN) {
            bfont_draw(ng_line, x + pad, ty, nfg);
            touch_region_row(x, ty, w, row_h, TOUCH_LIST_STARTUP, new_row);
        }

        // Hint fits in the 33-char content width (280 - 2*pad).
        // Source: res.ui.startup_controls_hint (game.json strings.startup).
        const char *hint = ui->startup_controls_hint;
        bfont_draw(hint, x + pad, y + h - pad - instr_h, PAL_CLR(GREY));

        frame_end(rt);
    }
    out->action = STARTUP_QUIT;
    return false;
}

// ---------------------------------------------------------------------------
// Title menu (modern), on the title art: New Game, Load Saved Game,
// Credits, Exit. Sets out->action to STARTUP_NEW (go to class
// select) or STARTUP_BACK (go to the save picker); Credits shows the credits
// and comes back. Returns false on Exit, Escape or a closed window.
// ---------------------------------------------------------------------------

static bool run_credits(RenderTexture2D *rt, const Resources *res,
                        const Sprites *sprites);

// The title menu's panel and rows; touch_list 0 registers no tap regions.
static void draw_title_menu(const Sprites *sprites, const char **labels, int count,
                            int cursor, int touch_list) {
    // Standard select rows (REQ-430n) in a panel sized to them.
    int w = 0;
    for (int i = 0; i < count; i++) {
        int tw = bfont_text_width(labels[i]);
        if (tw > w) w = tw;
    }
    w += 2 * ML_PAD + 64;
    int h = ml_list_height(count);
    int x = (CL_SCREEN_W - w) / 2;
    int y = CL_SCREEN_H / 2 + (CL_SCREEN_H / 2 - h) / 2 - CL_SCREEN_H / 16;
    if (title_sequence_ok(sprites)) y -= 40;   // clear of the subtitle at the art's foot
    panel(x, y, w, h);
    ml_list_draw(x, y, w, h, count, cursor, title_row, (void *)labels,
                 touch_list, uk_ink());
}

// The class picker's two confirm rows. Hardcoded like the other shell-owned
// touch labels (the letter selector's DEL / SPC / OK): they belong to the
// shell's flow, not to a pack's content.
static bool class_confirm_row(void *ctx, int i, char *label, char *right, int cap) {
    (void)ctx;
    right[0] = '\0';
    snprintf(label, (size_t)cap, "%s", i == 0 ? "Continue" : "Cancel");
    return true;
}

static bool run_title_menu(const Resources *res, const Sprites *sprites,
                           RenderTexture2D *rt, StartupChoice *out) {
    // No Exit on a phone: iOS has no notion of quitting an app and Apple
    // rejects a control that claims otherwise, and on Android the system
    // handles it. Everywhere else the row stays exactly where it was.
#if defined(PLATFORM_IOS) || defined(PLATFORM_ANDROID)
    enum { ROW_NEW, ROW_LOAD, ROW_CREDITS, ROW_COUNT };
    const ResUI *ui = &res->ui;
    const char *labels[ROW_COUNT] = {
        ui->title_new_adventure, ui->title_load_adventure, ui->title_credits,
    };
#else
    enum { ROW_NEW, ROW_LOAD, ROW_CREDITS, ROW_EXIT, ROW_COUNT };
    const ResUI *ui = &res->ui;
    const char *labels[ROW_COUNT] = {
        ui->title_new_adventure, ui->title_load_adventure, ui->title_credits, ui->menu_exit,
    };
#endif

    screen_open();
    SelList l = { ROW_COUNT, 0 };
    bool playing = title_sequence_ok(sprites) && !s_title_played;
    double started = frame_host_time();
    RenderTexture2D menu_rt = { 0 };
    while (!frame_host_should_close()) {
        // The sequence takes no menu input: a key or tap only skips it.
        double t = TITLE_END;
        if (playing) {
            t = frame_host_time() - started;
            if (any_key_pressed() || t >= TITLE_END) {
                playing = false;
                s_title_played = true;
                t = TITLE_END;
                screen_open();
            }
        }
        if (playing) {
            // The fading menu is drawn once into its own texture, then onto
            // the scene at the fade's alpha; it takes no taps until it is up.
            float a = title_phase(t, TITLE_MENU_IN, TITLE_END);
            if (a > 0 && !menu_rt.id) menu_rt = gfx_target_create(CL_SCREEN_W, CL_SCREEN_H);
            if (a > 0 && menu_rt.id) {
                gfx_target_begin(menu_rt);
                gfx_clear(BLANK);
                draw_title_menu(sprites, labels, ROW_COUNT, l.cursor, 0);
                gfx_target_end();
            }
            frame_begin(rt);
            draw_title_sequence(sprites, t);
            if (a > 0 && menu_rt.id)
                gfx_texture_draw(menu_rt.texture,
                                 (Rectangle){ 0, 0, (float)CL_SCREEN_W, -(float)CL_SCREEN_H },
                                 (Rectangle){ 0, 0, (float)CL_SCREEN_W, (float)CL_SCREEN_H },
                                 (Color){ 255, 255, 255, (unsigned char)(255 * a) });
            frame_end(rt);
            continue;
        }
        if (menu_rt.id) { gfx_target_free(menu_rt); menu_rt = (RenderTexture2D){ 0 }; }
        if (input_key_pressed(KEY_ESCAPE)) break;
        int row = -1;
        if (sel_input(&l, TOUCH_LIST_STARTUP, 0, &row) == SEL_CONFIRM && row >= 0) {
            advance_input_frame();
            drain_char_queue();
            switch (row) {
            case ROW_NEW:  out->action = STARTUP_NEW;  return true;
            case ROW_LOAD: out->action = STARTUP_BACK; return true;
            case ROW_CREDITS:
                if (!run_credits(rt, res, sprites)) { out->action = STARTUP_QUIT; return false; }
                screen_open();
                continue;
            default:       out->action = STARTUP_QUIT; return false;
            }
        }

        // The menu sits on the title page, over the lower half of the art.
        frame_begin(rt);
        draw_title_backdrop(sprites);
        draw_title_menu(sprites, labels, ROW_COUNT, l.cursor, TOUCH_LIST_STARTUP);
        frame_end(rt);
    }
    out->action = STARTUP_QUIT;
    return false;
}

// ---------------------------------------------------------------------------
// Class selection: every class in the catalog (legacy: the first four).
// ---------------------------------------------------------------------------

static bool run_class_select(const Resources *res,
                             const Sprites   *sprites,
                             RenderTexture2D *rt,
                             StartupChoice   *out) {
    int n = res->classes_count;
    if (n < 1) n = 1;
    if (!CL_IS_MODERN && n > 4) n = 4;   // legacy: the painting's four figures, keys A-D
    screen_open();
    // Modern: the carousel starts on the whole painting with no one picked;
    // Left/Right step through the figures, Enter picks.
    int class_cursor = CL_IS_MODERN ? -1 : 0;
    int confirm_row = 0;        // 0 Continue, 1 Cancel, once a class is picked
    double opened = frame_host_time();   // modern: nothing picked after 2 s -> the first class

    while (!frame_host_should_close()) {
        touch_request(TOUCH_CHROME_BACK);
        if (input_key_pressed(KEY_ESCAPE)) {
            // Modern: back to the title menu. Legacy: class select is the root.
            if (CL_IS_MODERN) {
                out->action = STARTUP_BACK;
                advance_input_frame();
                return true;
            }
            out->action = STARTUP_QUIT;
            return false;
        }
        if (CL_IS_MODERN) {
            if (class_cursor < 0 && frame_host_time() - opened >= 2.0) class_cursor = 0;
            // Any other key during that wait picks out the first figure at
            // once; it does not also choose it.
            bool woke = false;
            if (class_cursor < 0 && !input_key_pressed(KEY_LEFT) && !input_key_pressed(KEY_RIGHT) &&
                !input_key_pressed(KEY_ESCAPE)) {
                for (int k = input_get_key_pressed(); k != 0; k = input_get_key_pressed())
                    woke = true;
                if (woke) class_cursor = 0;
            }
            if (input_key_pressed(KEY_LEFT)) {
                class_cursor = class_cursor < 0 ? n - 1 : sel_wrap(class_cursor, -1, n);
            }
            if (input_key_pressed(KEY_RIGHT)) {
                class_cursor = class_cursor < 0 ? 0 : sel_wrap(class_cursor, 1, n);
            }
            bool enter = !woke && (input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER));

            // Choosing a class is TWO steps, the same two for every input:
            // pick a figure (it takes the gold outline and its description
            // comes up), then Continue or Cancel. A tap used to do both at
            // once, so the choice was never on screen; and the keyboard used
            // to confirm on Enter with no way back.
            int tapped = touch_tapped_row(TOUCH_LIST_CLASS);
            if (tapped >= 0 && tapped < n) {
                class_cursor = tapped;
                confirm_row = 0;
            }
            if (class_cursor >= 0) {
                if (input_key_pressed(KEY_UP) || input_key_pressed(KEY_KP_8) ||
                    input_key_pressed(KEY_DOWN) || input_key_pressed(KEY_KP_2))
                    confirm_row = !confirm_row;
                int crow = touch_tapped_row(TOUCH_LIST_CLASS_CONFIRM);
                if (crow >= 0) confirm_row = crow;
                bool go = (crow == 0) || (enter && confirm_row == 0);
                bool back = (crow == 1) || (enter && confirm_row == 1);
                if (back) {
                    class_cursor = -1;      // the painting, nothing picked
                    confirm_row = 0;
                    enter = false;
                    go = false;
                }
                // A tap on Continue IS the confirmation. The accept below
                // tested `enter` alone, so on a touch screen Continue set
                // `go` and nothing read it: the picker could be reached, and
                // never left.
                enter = go;
            }
            if (enter && class_cursor >= 0) {
                const ClassDef *c = class_by_index(class_cursor);
                safe_copy(out->class_id, sizeof(out->class_id), c ? c->id : "knight");
                out->action = STARTUP_NEW;
                drain_char_queue();
                return true;
            }
        }
        // L for Load (legacy; modern loads from the title menu)
        if (!CL_IS_MODERN && input_key_pressed(KEY_L)) {
            out->action = STARTUP_LOAD;
            drain_char_queue();   // don't leak the 'L' into name entry
            return true;
        }
        // A/B/C/D pick directly .
        static const int keys[4] = { KEY_A, KEY_B, KEY_C, KEY_D };
        for (int k = 0; k < n && !CL_IS_MODERN; k++) {
            if (input_key_pressed(keys[k])) {
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
        gfx_rect(0, 0, CL_SCREEN_W, CL_SCREEN_H, PAL_CLR(BLACK));

        // Picker bitmap, centered. Sized per frame: present_refit can change
        // the screen out from under us when the window is resized.
        int pw = 288 * CL_UI, ph = 184 * CL_UI;
        int px = 0, py = 0;
        bool picker_shown = false;
        if (sprites && sprites->class_picker.id) {
            int fs = ui_fit_scale(sprites->class_picker.width,
                                  sprites->class_picker.height,
                                  CL_SCREEN_W, CL_SCREEN_H);
            pw = sprites->class_picker.width  * fs;
            ph = sprites->class_picker.height * fs;
            px = (CL_SCREEN_W - pw) / 2;
            py = (CL_SCREEN_H - ph) / 2;
            picker_shown = true;
            // Modern: the carousel frame for the picked figure, pre-rendered
            // with the others dimmed and the figure ringed in gold
            // (tools/classpicker.py); the whole painting before anyone is
            // picked.
            bool picked = CL_IS_MODERN && class_cursor >= 0;
            bool carousel = picked && class_cursor < sprites->class_picker_selected_count &&
                            sprites->class_picker_selected[class_cursor].id;
            ui_blit(carousel ? sprites->class_picker_selected[class_cursor]
                             : sprites->class_picker, px, py, pw, ph);
            // Modern: the red header names the picked figure. A pack without
            // carousel frames dims by column instead.
            if (CL_IS_MODERN) {
                if (picked && !carousel) {
                    int cw = pw / n;
                    for (int k = 0; k < n; k++)
                        if (k != class_cursor)
                            gfx_rect(px + k * cw, py, cw, ph, (Color){ 0, 0, 0, 150 });
                    int cx = px + class_cursor * cw;
                    for (int t = 0; t < 3; t++)
                        gfx_rect_lines(cx + t, py + t, cw - 2 * t, ph - 2 * t, PAL_CLR(YELLOW));
                }
            }
        } else {
            // Fallback: text list if asset missing.
            bfont_draw(res->ui.startup_class_picker_missing,
                       40 * CL_UI, 90 * CL_UI, PAL_CLR(YELLOW));
        }

        // Modern: the picked figure's caption along the foot -- its class and
        // what it is like.
        if (CL_IS_MODERN && class_cursor >= 0) {
            const ClassDef *pc = class_by_index(class_cursor);
            const ResBanners *bn = &res->banners;
            const char *desc = !pc ? "" : strcmp(pc->id, "knight") == 0 ? bn->class_desc_knight
                             : strcmp(pc->id, "paladin") == 0 ? bn->class_desc_paladin
                             : strcmp(pc->id, "sorceress") == 0 ? bn->class_desc_sorceress
                             : strcmp(pc->id, "barbarian") == 0 ? bn->class_desc_barbarian : "";
            int cw = 700, tw = cw - 2 * UK_INSET;
            int lines = uk_lines(desc, tw);
            int chh = 2 * UK_INSET + (1 + lines) * uk_line_h();
            // Two rows under the description, and they are the ONLY way on:
            // Continue takes the class, Cancel puts the painting back. The
            // same two rows whatever the input -- a tap, an arrow key or a
            // pad all land on them, so no one route confirms invisibly.
            chh += ML_ROW_RULE + 2 * ml_row_h();
            int cx = (CL_SCREEN_W - cw) / 2, cy = CL_SCREEN_H - chh - 16;
            panel(cx, cy, cw, chh);
            bfont_draw(pc ? pc->name : "", cx + UK_INSET, cy + UK_INSET, PAL_CLR(YELLOW));
            uk_flow(cx + UK_INSET, cy + UK_INSET + uk_line_h(), tw, cx, 0, cy + chh, desc, PAL_CLR(WHITE));

            int ry = cy + 2 * UK_INSET + (1 + lines) * uk_line_h();
            lattice_band_h(cx, ry, cw, ML_ROW_RULE);
            ry += ML_ROW_RULE;
            ml_list_draw(cx, ry, cw, ml_list_height(2), 2, confirm_row,
                         class_confirm_row, NULL, TOUCH_LIST_CLASS_CONFIRM,
                         uk_ink());   // cursor: 0 Continue, 1 Cancel
        }

        // Touch: the picker art shows the classes side by side, one column
        // each; tapping a column picks that class (A-D). Registered AFTER the
        // description panel, because the panel sits INSIDE the painting and a
        // tap takes the first region that contains it (`resolve_tap`,
        // src/touch.c): registered first, the columns swallowed every tap on
        // Continue and Cancel, and touch had no way off this screen (REQ-532).
        if (picker_shown) {
            for (int k = 0; k < n; k++) {
                if (CL_IS_MODERN) touch_region_row(px + k * (pw / n), py, pw / n, ph, TOUCH_LIST_CLASS, k);
                else              touch_region(px + k * (pw / n), py, pw / n, ph, KEY_A + k);
            }
        }

        // Status-bar hint at top (). Modern: the picked figure's class.
        gfx_rect(0, 0, CL_SCREEN_W, GH + 2, PAL_CLR(DRED));
        const char *hint = res->ui.startup_class_select_hint;
        if (CL_IS_MODERN && class_cursor >= 0) {
            const ClassDef *pc = class_by_index(class_cursor);
            if (pc && pc->name[0]) hint = pc->name;
        }
        bfont_draw_centered(hint, CL_SCREEN_W / 2, 1, PAL_CLR(WHITE));

        frame_end(rt);
    }
    out->action = STARTUP_QUIT;
    return false;
}

// ---------------------------------------------------------------------------
// Combined name + difficulty entry.
//
// Panel: 30 cols x 12 rows in 8x8 font cells = 240 x 96 px, centered.
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

// The name field takes letters, digits and spaces, ten characters.
static bool name_char_allowed(const char *buf, int len, int ch) {
    (void)buf;
    if (len >= 10) return false;
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') || ch == ' ';
}

static bool run_create_game(const Resources *res,
                            const Sprites   *sprites,
                            RenderTexture2D *rt,
                            StartupChoice   *out) {
    char name_buf[11] = { 0 };
    int  name_len = 0;
    bool has_name = false;
    int  sel = 1;             // initial difficulty is Normal
    double cursor_blink = 0;
    TextSel ts = { 0, false };   // modern: the letter selector when there is no keyboard
    bool selector = false;
    screen_open();

    // Look up class title via out->class_id (set by run_class_select).
    const ClassDef *cls = class_by_id(out->class_id);
    const char *class_title = cls->name;

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

    while (!frame_host_should_close()) {
        touch_request(TOUCH_CHROME_BACK);
        if (input_key_pressed(KEY_ESCAPE)) {
            // ESC on new-game screen returns to class select, not quit.
            out->action = STARTUP_BACK;
            advance_input_frame();
            return true;
        }

        int tapped = touch_tapped_row(TOUCH_LIST_STARTUP);
        if (!has_name) {
            // Modern: without a keyboard, or once a pad or touch has been
            // used, the in-game letter selector takes the field; typing
            // still works alongside it. Legacy keeps the window keyboard.
            // A finger gets the on-screen KEYBOARD, not the in-buffer letter
            // grid: the grid is laid out in the pack's design pixels, which
            // on a phone is a 21x13pt key, while the chrome keyboard is drawn
            // in window pixels and can be a proper size. The grid stays for a
            // gamepad and for a desktop with no keyboard.
            selector = CL_IS_MODERN && !input_touch_active() &&
                       (input_text_mode() == TEXT_MODE_SELECTOR || input_pad_or_touch_seen());
            if (!selector) touch_request(TOUCH_CHROME_KEYBOARD);
            bool done = false;
            if (selector) {
                done = textsel_input(&ts, name_buf, &name_len, (int)sizeof name_buf,
                                     TOUCH_LIST_TEXTSEL, name_char_allowed);
            }
            // Name entry phase. Enter confirms; BACKSPACE deletes; alpha/
            // digit/space appends. With the selector up, Enter picks a cell
            // instead, and OK on the grid is the confirm.
            if (done || (!selector && (input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER)))) {
                if (name_len == 0) {
                    // We default to world.default_name on empty name
                    // (from game.json) so the flow always completes.
                    const char *dn = res->world.default_name;
                    safe_copy(name_buf, sizeof(name_buf), dn);
                    name_len = (int)strlen(name_buf);
                }
                // name[0] = toupper(name[0])
                if (name_buf[0] >= 'a' && name_buf[0] <= 'z') {
                    name_buf[0] = (char)(name_buf[0] - 'a' + 'A');
                }
                has_name = true;
            } else if (!selector && input_key_pressed(KEY_BACKSPACE) && name_len > 0) {
                name_buf[--name_len] = '\0';
            } else {
                int ch = input_get_char_pressed();
                while (ch > 0 && name_len < 10) {
                    bool allowed = (ch >= 'A' && ch <= 'Z') ||
                                   (ch >= 'a' && ch <= 'z') ||
                                   (ch >= '0' && ch <= '9') ||
                                   ch == ' ';
                    if (allowed) {
                        name_buf[name_len++] = (char)ch;
                        name_buf[name_len] = '\0';
                    }
                    ch = input_get_char_pressed();
                }
            }
        } else {
            // Difficulty selection phase. Arrows clamp :
            //   sel--; if (sel < 0) sel = 0;
            //   sel++; if (sel > 3) sel = 3;
            // Touch: tapping a difficulty row selects and accepts it.
            if (tapped >= 0 && tapped < n) sel = tapped;
            if (input_key_pressed(KEY_UP) || input_key_pressed(KEY_KP_8)) {
                if (sel > 0) sel--;
            }
            if (input_key_pressed(KEY_DOWN) || input_key_pressed(KEY_KP_2)) {
                if (sel < n - 1) sel++;
            }
            if (tapped >= 0 ||
                input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER)) {
                out->difficulty = rows[sel].diff;
                safe_copy(out->name, sizeof(out->name), name_buf);
                out->action = STARTUP_NEW;
                return true;
            }
        }

        cursor_blink += (float)frame_host_delta();
        bool show_caret = (int)(cursor_blink * 2.0) & 1;

        // ---- Render  ----
        frame_begin(rt);
        if (CL_IS_MODERN) {
            // Modern: the chosen figure's carousel frame behind, its class in
            // the red header, and a panel sized to its text: a Name field
            // (the default name in grey until something is typed), then the
            // difficulty rows.
            Texture2D bg = sprites ? sprites->class_picker : (Texture2D){ 0 };
            if (sprites && cls && cls->index >= 0 && cls->index < sprites->class_picker_selected_count &&
                sprites->class_picker_selected[cls->index].id)
                bg = sprites->class_picker_selected[cls->index];
            if (bg.id) {
                int fs = ui_fit_scale(bg.width, bg.height, CL_SCREEN_W, CL_SCREEN_H);
                ui_blit(bg, (CL_SCREEN_W - bg.width * fs) / 2,
                        (CL_SCREEN_H - bg.height * fs) / 2, bg.width * fs, bg.height * fs);
            }
            gfx_rect(0, 0, CL_SCREEN_W, GH + 2, PAL_CLR(DRED));
            bfont_draw_centered(class_title, CL_SCREEN_W / 2, 1, PAL_CLR(WHITE));

            // An in-lay as tall as what it holds: the name, the difficulty
            // table, and the letter selector while it takes the name.
            int mrow = GH + 4;
            int sel_h = (!has_name && selector) ? textsel_h(false, GH + 4) + mrow : 0;
            int mh = UK_INSET + 3 * mrow + UK_BAND + ml_list_height(n) + sel_h;
            int mw = UK_INLAY_W;
            int mx = (CL_SCREEN_W - mw) / 2, my = (CL_SCREEN_H - mh) / 2;
            if (my < GH + 8) my = GH + 8;
            gfx_rect(0, GH + 2, CL_SCREEN_W, CL_SCREEN_H, (Color){ 0, 0, 0, 110 });
            panel(mx, my, mw, mh);
            int cx0 = mx + UK_INSET;
            int ty = my + UK_INSET;

            // "Hero Name: " then the name as plain text, the default in grey
            // until something is typed, and a caret while typing.
            const char *label = res->ui.hero_name_label;
            bfont_draw(label, cx0, ty, PAL_CLR(YELLOW));
            int fx = cx0 + bfont_text_width(label) + GW;
            if (name_len > 0 || has_name) {
                bfont_draw(name_buf, fx, ty, PAL_CLR(WHITE));
            } else if (!has_name) {
                bfont_draw(res->world.default_name, fx, ty, PAL_CLR(GREY));
            }
            if (!has_name && show_caret && name_len < 10) {
                int cx = fx + bfont_text_width(name_buf);
                gfx_rect(cx, ty + GH - 2, GW, 2, PAL_CLR(YELLOW));
            }
            ty += 2 * mrow;

            // The pack's header is three words; each sits over its column.
            {
                static const int col[3] = { 0, 13, 21 };
                char hdr[64];
                snprintf(hdr, sizeof hdr, "%s", res->ui.startup_new_game_table_header);
                char *p = hdr;
                for (int k = 0; k < 3 && *p; k++) {
                    while (*p == ' ') p++;
                    char *tok = p;
                    while (*p && *p != ' ') p++;
                    if (*p) *p++ = '\0';
                    // The last column sits over the scores, right-aligned in the rows.
                    int hx = (k == 2) ? mx + mw - ML_PAD - bfont_text_width(tok)
                                      : cx0 + col[k] * GW;
                    if (*tok) bfont_draw(tok, hx, ty,
                                         PAL_CLR(YELLOW));
                }
            }
            ty += mrow;
            // The difficulties as standard select rows (REQ-430n); grey and
            // not tappable until a name is entered.
            DiffCtx dc = { res, { 0 }, { 0 }, has_name };
            for (int k = 0; k < n; k++) { dc.labels[k] = rows[k].label; dc.scores[k] = rows[k].score; }
            lattice_band_h(mx, ty, mw, 4);
            ty += 4;
            ml_list_draw(mx, ty, mw, ml_list_height(n), n, has_name ? sel : -1,
                         difficulty_row, &dc, has_name ? TOUCH_LIST_STARTUP : 0,
                         uk_ink());
            ty += ml_list_height(n);

            if (!has_name && selector) {
                int cw = textsel_min_cell_w(), chh = GH + 6;
                int gx = mx + (mw - textsel_w(false, cw)) / 2;
                int gy = ty + mrow / 2;
                textsel_draw(&ts, gx, gy, cw, chh, PAL_CLR(YELLOW), uk_ink(),
                             TOUCH_LIST_TEXTSEL);
            }
            frame_end(rt);
            continue;
        }
        draw_class_picker_backdrop(sprites);
        draw_class_picker_status_hint(res);

        int w = 30 * GW;           // 240
        int h = 12 * GH;           //  96
        int x = (CL_SCREEN_W - w) / 2;
        int y = (CL_SCREEN_H - h) / 2;
        panel(x, y, w, h);

        // Row offsets: menu.y + fs->h * N. Row 1 (first inner row)
        // is at fs->h. We mirror that -- text drawn at y + GH*N (N = 1..10).
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
            int cx = name_x + bfont_text_width(name_buf);
            gfx_rect(cx, name_y, 1, GH, PAL_CLR(YELLOW));
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
            if (CL_IS_MODERN && has_name) {
                sel_row(x, ROW_Y(5 + i), w, GH, x + GW, line, sel == i,
                        PAL_CLR(WHITE), PAL_CLR(DBLUE), TOUCH_LIST_STARTUP, i);
            } else {
                bfont_draw(line, x + GW, ROW_Y(5 + i), PAL_CLR(WHITE));
                if (has_name)
                    touch_region_row(x, ROW_Y(5 + i), w, GH,
                                     TOUCH_LIST_STARTUP, i);
            }
        }

        // After has_name: draw the ">" cursor at col 0 of the selected row,
        // and the "^v to select   Ent to Accept" hint on row 10.
        if (has_name) {
            // ">" cursor -- print sel==i ? ">\n" : " \n" at
            //   menu.x + fs->w, menu.y + fs->h * 5  (i.e. col 1, row 5)
            // and walks down 4 rows. The cursor column is the same column as
            // the "   " prefix in the difficulty lines (col 1).
            if (!CL_IS_MODERN) bfont_draw(">", x + GW, ROW_Y(5 + sel), PAL_CLR(WHITE));

            // Hint on row 10: "\x18\x19 to select   Ent to Accept"
            // -- 0x18 and 0x19 are CP437 up/down arrows. Our bfont is ASCII-
            // only, so substitute "^v".
            bfont_draw(res->ui.startup_new_game_select_hint,
                       x + GW, ROW_Y(10), PAL_CLR(WHITE));
        }

        if (!has_name && selector) {
            int cw = textsel_min_cell_w(), chh = GH + 6 * CL_UI;
            int gx = x + (w - textsel_w(false, cw)) / 2;
            int gy = y + h + 4 * CL_UI;
            gfx_rect(gx - 2 * CL_UI, gy - 2 * CL_UI,
                          textsel_w(false, cw) + 4 * CL_UI, textsel_h(false, chh) + 4 * CL_UI,
                          PAL_CLR(DBLUE));
            textsel_draw(&ts, gx, gy, cw, chh, PAL_CLR(YELLOW), PAL_CLR(DBLUE), TOUCH_LIST_TEXTSEL);
        }
        #undef ROW_Y
        frame_end(rt);
    }
    out->action = STARTUP_QUIT;
    return false;
}

// ---------------------------------------------------------------------------
// Credits screen -- .
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
    const char *class_title = cls->name;

    char body[RES_BANNER_LEN];
    ResTemplateVar vars[2] = {
        { "NAME",  name && name[0] ? name : res->world.default_name },
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

    screen_open();
    double start = frame_host_time();
    double timeout = 4.0;
    while (!frame_host_should_close()) {
        if (any_key_pressed() || (frame_host_time() - start) >= timeout) return true;

        frame_begin(rt);

        // Background: same class-select cartoon (sprites->class_picker).
        draw_class_picker_backdrop(sprites);

        // Status hint at top.
        gfx_rect(0, 0, CL_SCREEN_W, GH + 2, PAL_CLR(DRED));
        bfont_draw_centered(res->ui.startup_class_select_hint,
                            CL_SCREEN_W / 2, 1, PAL_CLR(WHITE));

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
            image_w = inset.width  * CL_UI;
            image_h = inset.height * CL_UI;
        }
    }

    int text_w  = max_text_chars * GW;
    int gap     = image_w ? 8 * CL_UI : 0;
    int panel_w = pad + text_w + gap + image_w + pad;
    if (panel_w > CL_SCREEN_W - 8 * CL_UI) panel_w = CL_SCREEN_W - 8 * CL_UI;

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

    // Modern: a Back row along the foot (any key or tap still closes them).
    if (CL_IS_MODERN) {
        pad = UK_INSET;
        panel_h += 2 * (UK_INSET - 6) + UK_BAND + ml_list_height(1);
        panel_w += 2 * (UK_INSET - 6);
    }
    int px = (CL_SCREEN_W - panel_w) / 2;
    int py = (CL_SCREEN_H - panel_h) / 2;

    screen_open();
    double start = frame_host_time();
    double timeout = 2.5;
    while (!frame_host_should_close()) {
        // Modern opens the credits from the title menu, so they stay up until
        // a key; legacy runs them once at startup on a timer.
        if (any_key_pressed() || (!CL_IS_MODERN && (frame_host_time() - start) >= timeout))
            return true;

        frame_begin(rt);

        // Backdrop: legacy drapes the credits over the character-pick screen;
        // modern over the title art, since class select is not next.
        if (CL_IS_MODERN) {
            draw_title_backdrop(sprites);
        } else {
            draw_class_picker_backdrop(sprites);
            draw_class_picker_status_hint(res);
        }

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
            int tw = bfont_text_width(line);
            int cx = px + (panel_w - tw) / 2;
            bfont_draw(line, cx, ty, PAL_CLR(WHITE));
            ty += line_h;
        }

        if (CL_IS_MODERN) {
            const char *back[1] = { res->ui.gm_back };
            int ry = py + panel_h - ml_list_height(1);
            lattice_band_h(px, ry - UK_BAND, panel_w, UK_BAND);
            ml_list_draw(px, ry, panel_w, ml_list_height(1), 1, 0, title_row, (void *)back,
                         TOUCH_LIST_STARTUP, uk_ink());
        }

        // Inset image (re-using class_select_highlight). Vertically
        // centered against the group block, with a thin green outline.
        if (image_w) {
            int ix = px + panel_w - pad - image_w;
            int iy = py + pad + line_h;
            if (iy + image_h > py + panel_h - pad) {
                iy = py + panel_h - pad - image_h;
            }
            ui_blit(inset, ix, iy, image_w, image_h);
            gfx_rect_lines(ix - CL_UI, iy - CL_UI,
                               image_w + 2 * CL_UI, image_h + 2 * CL_UI,
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
                          StartupChoice   *out,
                          bool             skip_intro) {
    RenderTexture2D *rt = (RenderTexture2D *)chrome_target;
    memset(out, 0, sizeof(*out));

    // Splash 1: publisher logo on black.
    if (!skip_intro && sprites && !run_splash(rt, sprites->splash_logo,
                               (Color){ 0x00, 0x00, 0x00, 0xFF })) {
        out->action = STARTUP_QUIT;
        return false;
    }

    // Splash 2: game title on black. Modern skips it: the title menu is
    // drawn on the title art.
    if (!skip_intro && !CL_IS_MODERN && sprites && !run_splash(rt, sprites->splash_title,
                               (Color){ 0x00, 0x00, 0x00, 0xFF })) {
        out->action = STARTUP_QUIT;
        return false;
    }

    // Credits screen . Drawn over the
    // class-select cartoon so the picker is visible behind. Skipped
    // silently if the game pack doesn't define any credit lines.
    if (!skip_intro && !CL_IS_MODERN && !run_credits(rt, res, sprites)) {
        out->action = STARTUP_QUIT;
        return false;
    }

    // Modern: the title menu is the root. Load Saved Game opens the
    // save picker; New Game runs class select and then name and
    // difficulty; Credits are shown from it. Escape on each screen goes back one step, and Escape
    // (or Exit) on the title menu quits.
    for (; CL_IS_MODERN;) {
        if (!run_title_menu(res, sprites, rt, out)) return false;
        if (out->action == STARTUP_BACK) {                         // Load
            if (!run_save_picker(rt, sprites, out)) return false;
            if (out->action == STARTUP_LOAD) break;
            continue;
        }
        bool to_title = false;                                     // New Game
        for (;;) {
            if (!run_class_select(res, sprites, rt, out)) return false;
            if (out->action == STARTUP_BACK) { to_title = true; break; }
            if (!run_create_game(res, sprites, rt, out)) return false;
            if (out->action == STARTUP_BACK) continue;
            break;
        }
        if (to_title) continue;
        break;
    }

    // Legacy: class select is the root. Sub-screens (save picker, new-game)
    // ESC back here; only ESC at class select exits.
    for (; !CL_IS_MODERN;) {
        if (!run_class_select(res, sprites, rt, out)) return false;

        if (out->action == STARTUP_LOAD) {
            // User pressed L: show save picker. May return STARTUP_LOAD
            // (existing slot), STARTUP_NEW (empty slot / "New game" row),
            // or STARTUP_BACK (ESC -> back to class select).
            if (!run_save_picker(rt, sprites, out)) return false;
            if (out->action == STARTUP_BACK) continue;   // back to class select
            if (out->action == STARTUP_LOAD) break;      // done, load path
            // else STARTUP_NEW -- fall through to create_game for the slot
        }

        // New-game path. ESC here also returns to class select.
        if (!run_create_game(res, sprites, rt, out)) return false;
        if (out->action == STARTUP_BACK) continue;
        break;   // STARTUP_NEW set by run_create_game on Enter
    }

    // New-game intro screen: "<Name> the <Class>, A new game is
    // being created. Please wait while I perform godlike actions to make
    // this game playable." Auto-advances after a few seconds.
    // Modern goes straight into the game.
    if (out->action == STARTUP_NEW && !CL_IS_MODERN) {
        run_new_game_intro(rt, res, sprites, out, out->name);
    }

    return true;
}
