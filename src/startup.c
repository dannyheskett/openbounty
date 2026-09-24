#include "frame_host.h"
#include "gfx.h"
#include "input_host.h"
#include "startup.h"
#include "touch.h"
#include "uitouch.h"
#include "layout.h"
#include "modern/mlayout.h"
#include "modern/mlist.h"
#include "modern/uikit.h"
#include "modern/page.h"
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
    // The pre-game screens have no frame: their art is full-bleed, and a page
    // over it measures from the screen's edges.
    page_bare();
}

// Draw the class-select cartoon as the screen backdrop, so dialog
// panels (credits, save picker, new-game name/difficulty, new-game
// intro) sit visually over the character-selection screen.
static void draw_class_picker_backdrop(const Sprites *sprites) {
    if (!sprites || !sprites->class_picker.id) return;
    Texture2D t = sprites->class_picker;
    if (CL_IS_MODERN) { page_art(t, NULL); return; }
    int fs = ui_fit_scale(t.width, t.height, CL_SCREEN_W, CL_SCREEN_H);
    int pw = t.width  * fs;
    int ph = t.height * fs;
    ui_blit(t, (CL_SCREEN_W - pw) / 2, (CL_SCREEN_H - ph) / 2, pw, ph);
}

// The band every pre-game screen wears. It is also the way back out of the
// screen for a finger: these screens are not views, so the game's own band
// (chrome.c) is not drawn and nothing else here answers a tap. The title menu
// does NOT call this -- Escape there leaves the menu, and a stray tap must
// not do that.
static void startup_bar(const char *text) {
    gfx_rect(0, 0, CL_SCREEN_W, GH + 2, PAL_CLR(DRED));
    bfont_draw_centered(text, CL_SCREEN_W / 2, 1, PAL_CLR(WHITE));
    ui_bar(0, 0, CL_SCREEN_W, GH + 2, KEY_ESCAPE);
}

static void draw_class_picker_status_hint(const Resources *res) {
    startup_bar(res->ui.startup_class_select_hint);
}

static void frame_end(RenderTexture2D *rt) {
    present_end();

    present_scaled(*rt);
    frame_host_end_frame();

    // Backtick captures a screenshot of the target we just drew.
    screenshot_tick(*rt, "shot");
}

// Legacy's panel: blue, with its yellow frame. Modern's are pages.
static void panel(int x, int y, int w, int h) {
    gfx_rect(x, y, w, h, PAL_CLR(DBLUE));
    legacy_window_frame(x, y, w, h, PAL_CLR(YELLOW));
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

// The purple the title sequence opens on, before the battle fades in.
static Color title_purple(void) { return (Color){ 65, 9, 104, 255 }; }

static void draw_title_sequence(const Sprites *s, double t) {
    Texture2D b = s->title_battle, e = s->title_eagle;
    int fs = ui_fit_scale(b.width, b.height, CL_SCREEN_W, CL_SCREEN_H);
    int pw = b.width * fs, ph = b.height * fs;
    int ox = (CL_SCREEN_W - pw) / 2, oy = (CL_SCREEN_H - ph) / 2;
    // Full-bleed art, on black where the screen is larger.
    gfx_rect(ox, oy, pw, ph, title_purple());
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
    page_art(sprites->splash_title, NULL);
}

// Any key but a modifier, or a tap: the one any-key check (src/ui.c).
static bool any_key_pressed(void) { return ui_any_key_pressed(); }

// A full-screen splash: the texture centred on `bg_color`.
static void draw_splash(Texture2D tex, Color bg_color) {
    gfx_clear(bg_color);
    // Modern: full-bleed art at the largest whole multiple, on black.
    if (CL_IS_MODERN) { page_art(tex, NULL); return; }
    // Splash art is authored in the 320x200 design space; legacy draws it at 1x.
    int fs = ui_fit_scale(tex.width, tex.height, CL_SCREEN_W, CL_SCREEN_H);
    int iw = tex.width  * fs;
    int ih = tex.height * fs;
    ui_blit(tex, (CL_SCREEN_W - iw) / 2, (CL_SCREEN_H - ih) / 2, iw, ih);
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
        draw_splash(tex, bg_color);
        frame_end(rt);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Save-slot picker. Shows 10 rows with each slot's character name +
// class + zone + days-left summary. Slot 10 acts as "New Game".
// Returns: action=LOAD with slot set, or action=NEW with slot set.
// ---------------------------------------------------------------------------

// The slots and their rows: src/modern/saveslots.c (the in-game menu uses them too).

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

typedef struct { const DiffCtx *d; const char *cancel; } DiffRowsCtx;

static bool difficulty_or_cancel_row(void *ctx, int i, char *label, char *right, int cap) {
    const DiffRowsCtx *c = (const DiffRowsCtx *)ctx;
    if (i >= 4) { ml_exit_hint(right); snprintf(label, (size_t)cap, "%s", c->cancel); return true; }
    return difficulty_row((void *)c->d, i, label, right, cap);
}

// Modern: the slot rows, then Back.
typedef struct { const GmPage *page; const SlotSet *slots; } TitleSlotsCtx;

static bool title_slot_row(void *ctx, int i, char *label, char *right, int cap) {
    const TitleSlotsCtx *c = (const TitleSlotsCtx *)ctx;
    const GmPage *p = c->page;
    if (i < MODERN_SAVE_SLOTS) {
        saveslots_row((void *)c->slots, i, label, right, cap);
        return p->item[i].enabled;
    }
    return gm_row((void *)p, i, label, right, cap);
}

// Modern: the title's Load Saved Game is the in-game Load page -- built by
// the same function, so its rows and their words are the same -- with "Load
// Saved Game" for its path.
static void load_page_build(GmPage *p, const Resources *r, const SlotSet *slots) {
    gm_load_page(p, slots, r->ui.title_load_adventure);
}

// The cursor the Load page opens on: its first row that can be chosen.
static int load_page_cursor(const SlotSet *slots) {
    const Resources *r = resources_current();
    GmPage p;
    if (!r) return 0;
    load_page_build(&p, r, slots);
    return gm_first_enabled(&p);
}

static void draw_save_picker_modern(const Sprites *sprites, const GmPage *p,
                                    int cursor, TitleSlotsCtx *ctx) {
    draw_title_backdrop(sprites);
    // The in-game Load page, over the title art.
    page_menu(p, p->title, NULL, cursor, TOUCH_LIST_STARTUP, title_slot_row, ctx);
}

static bool run_save_picker_modern(RenderTexture2D *rt, const Sprites *sprites,
                                   StartupChoice *out, const SlotSet *slots) {
    const Resources *r = resources_current();
    if (!r) { out->action = STARTUP_BACK; return true; }
    GmPage p;
    load_page_build(&p, r, slots);
    TitleSlotsCtx ctx = { &p, slots };
    int cursor = load_page_cursor(slots);

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
        draw_save_picker_modern(sprites, &p, cursor, &ctx);
        frame_end(rt);
    }
    out->action = STARTUP_QUIT;
    return false;
}

// Legacy: ten slot rows and New game, over the class-select cartoon.
static void draw_save_picker_legacy(const Sprites *sprites, const SlotSet *slots,
                                    int cursor, int new_row) {
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
        if (slots->hdrs[i].exists) {
            snprintf(line, sizeof(line),
                     "%2d. %-10s  %-9s  %3dd",
                     i + 1,
                     slots->hdrs[i].name,
                     slots->hdrs[i].rank_title,
                     slots->hdrs[i].days_left);
        } else {
            snprintf(line, sizeof(line), "%2d. %s", i + 1, empty_lbl);
        }
        bfont_draw(line, x + pad, ty, fg);
        ui_tile_row(x, ty, w, row_h, TOUCH_LIST_STARTUP, i);
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
    bfont_draw(ng_line, x + pad, ty, nfg);
    ui_tile_row(x, ty, w, row_h, TOUCH_LIST_STARTUP, new_row);

    // Hint fits in the 33-char content width (280 - 2*pad).
    // Source: res.ui.startup_controls_hint (game.json strings.startup).
    const char *hint = ui->startup_controls_hint;
    bfont_draw(hint, x + pad, y + h - pad - instr_h, PAL_CLR(GREY));
}

static bool run_save_picker(RenderTexture2D *rt, const Sprites *sprites,
                            StartupChoice *out) {
    SlotSet slots;
    saveslots_scan(&slots);
    screen_open();
    if (CL_IS_MODERN) return run_save_picker_modern(rt, sprites, out, &slots);

    // Legacy. Row index = 0..SAVE_SLOT_COUNT-1 for slots, SAVE_SLOT_COUNT
    // for "New".
    int row_count = SAVE_SLOT_COUNT + 1;
    int new_row   = SAVE_SLOT_COUNT;

    // Cursor lands on the first existing slot, else on "New". Saves are
    // physically segregated by pack (<user-data>/openbounty/saves/<pack_id>/),
    // so every slot we see here belongs to the active pack.
    int cursor = new_row;
    for (int i = 0; i < SAVE_SLOT_COUNT; i++) {
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
            }
            // Empty slot chosen directly -> new game into that slot.
            out->action = STARTUP_NEW;
            out->slot   = cursor;
            return true;
        }

        // ---- Render --------------------------------------------------
        frame_begin(rt);
        draw_save_picker_legacy(sprites, &slots, cursor, new_row);
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

// The title menu's rows over the title art; touch_list 0 registers no tap
// regions. Nothing is under it to go back to, so a tap off it does nothing.
static void draw_title_menu(const Sprites *sprites, const char **labels, int count,
                            int cursor, int touch_list) {
    (void)sprites;
    page_title_menu(labels, count, cursor, touch_list);
}

// The class picker's confirm row. Hardcoded like the other shell-owned touch
// labels (the letter selector's DEL / SPC / OK): it belongs to the shell's
// flow, not to a pack's content. One row: there is nothing to cancel to.
static bool class_confirm_row(void *ctx, int i, char *label, char *right, int cap) {
    (void)ctx; (void)i;
    right[0] = '\0';
    snprintf(label, (size_t)cap, "Continue");
    return true;
}

// The title menu's rows. No Exit on a phone: iOS has no notion of quitting an
// app and Apple rejects a control that claims otherwise, and on Android the
// system handles it. Everywhere else the row stays exactly where it was.
#if defined(PLATFORM_IOS) || defined(PLATFORM_ANDROID)
enum { ROW_NEW, ROW_LOAD, ROW_CREDITS, ROW_COUNT };
#else
enum { ROW_NEW, ROW_LOAD, ROW_CREDITS, ROW_EXIT, ROW_COUNT };
#endif

static void title_menu_labels(const ResUI *ui, const char *labels[ROW_COUNT]) {
    labels[ROW_NEW]     = ui->title_new_adventure;
    labels[ROW_LOAD]    = ui->title_load_adventure;
    labels[ROW_CREDITS] = ui->title_credits;
#if !defined(PLATFORM_IOS) && !defined(PLATFORM_ANDROID)
    labels[ROW_EXIT]    = ui->menu_exit;
#endif
}

static bool run_title_menu(const Resources *res, const Sprites *sprites,
                           RenderTexture2D *rt, StartupChoice *out) {
    const char *labels[ROW_COUNT];
    title_menu_labels(&res->ui, labels);

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
            // Drawn at the screen's zoom, as the screen is, so its words are
            // as sharp fading in as they are once it is up.
            float a = title_phase(t, TITLE_MENU_IN, TITLE_END);
            int z = present_get_zoom();
            if (menu_rt.id && menu_rt.texture.width != CL_SCREEN_W * z) {
                gfx_target_free(menu_rt);
                menu_rt = (RenderTexture2D){ 0 };
            }
            if (a > 0 && !menu_rt.id) menu_rt = gfx_target_create(CL_SCREEN_W * z, CL_SCREEN_H * z);
            if (a > 0 && menu_rt.id) {
                gfx_target_begin(menu_rt);
                gfx_clear(BLANK);
                if (z > 1) gfx_zoom_begin((float)z);
                page_frame_begin();
                page_bare();
                draw_title_menu(sprites, labels, ROW_COUNT, l.cursor, 0);
                if (z > 1) gfx_zoom_end();
                gfx_target_end();
            }
            frame_begin(rt);
            draw_title_sequence(sprites, t);
            if (a > 0 && menu_rt.id)
                gfx_texture_draw(menu_rt.texture,
                                 (Rectangle){ 0, 0, (float)menu_rt.texture.width, -(float)menu_rt.texture.height },
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

// The class picker: the painting, the picked figure ringed and captioned.
// class_cursor < 0: modern, before anyone is picked -- the whole painting.
static void draw_class_select(const Resources *res, const Sprites *sprites,
                              int n, int class_cursor) {
    // Background: solid black.
    gfx_rect(0, 0, CL_SCREEN_W, CL_SCREEN_H, PAL_CLR(BLACK));

    // Picker bitmap, centered. Sized per frame: present_refit can change
    // the screen out from under us when the window is resized.
    int pw = 288 * CL_UI, ph = 184 * CL_UI;
    int px = 0, py = 0;
    bool picker_shown = false;
    if (sprites && sprites->class_picker.id && CL_IS_MODERN) {
        // Modern: the carousel frame for the picked figure, pre-rendered
        // with the others dimmed and the figure ringed in gold
        // (tools/classpicker.py); the whole painting before anyone is
        // picked. Full-bleed art, at the largest whole multiple.
        bool picked = class_cursor >= 0;
        bool carousel = picked && class_cursor < sprites->class_picker_selected_count &&
                        sprites->class_picker_selected[class_cursor].id;
        ML_Rect a = page_art(carousel ? sprites->class_picker_selected[class_cursor]
                                      : sprites->class_picker, NULL);
        px = a.x; py = a.y; pw = a.w; ph = a.h;
        picker_shown = true;
        // A pack without carousel frames dims by column instead.
        if (picked && !carousel) {
            int cw = pw / n;
            for (int k = 0; k < n; k++)
                if (k != class_cursor) gfx_rect(px + k * cw, py, cw, ph, uk_shade());
            int cx = px + class_cursor * cw;
            for (int t = 0; t < 3; t++)
                gfx_rect_lines(cx + t, py + t, cw - 2 * t, ph - 2 * t, PAL_CLR(YELLOW));
        }
    } else if (sprites && sprites->class_picker.id) {
        int fs = ui_fit_scale(sprites->class_picker.width,
                              sprites->class_picker.height,
                              CL_SCREEN_W, CL_SCREEN_H);
        pw = sprites->class_picker.width  * fs;
        ph = sprites->class_picker.height * fs;
        px = (CL_SCREEN_W - pw) / 2;
        py = (CL_SCREEN_H - ph) / 2;
        picker_shown = true;
        ui_blit(sprites->class_picker, px, py, pw, ph);
    } else {
        // Fallback: text list if asset missing.
        bfont_draw(res->ui.startup_class_picker_missing,
                   40 * CL_UI, 90 * CL_UI, PAL_CLR(YELLOW));
    }

    if (!CL_IS_MODERN) {
        // Touch: the picker art shows the classes side by side, one column
        // each; tapping a column picks that class (A-D).
        if (picker_shown)
            for (int k = 0; k < n; k++)
                ui_tile(px + k * (pw / n), py, pw / n, ph, KEY_A + k);
        startup_bar(res->ui.startup_class_select_hint);
        return;
    }

    // Modern: a caption along the painting's foot -- "Choose your class" until
    // a figure is picked, then its class, what it is like, and Continue, the
    // one way on. Its strip carries Back, to the title. The same row whatever
    // the input: a tap, a key or a pad lands on it, so no route confirms
    // invisibly. Picking another figure is the way to change class.
    const int tw = page_msg_w() - 2 * UK_INSET;
    const char *back = res->ui.gm_back;
    int top = CL_SCREEN_H;
    if (class_cursor >= 0) {
        const ClassDef *pc = class_by_index(class_cursor);
        const ResClassHero *ch = pc ? resources_class_hero(res, pc->id) : NULL;
        const char *desc = ch ? ch->desc : "";
        int lines = uk_lines(desc, tw);
        int chh = uk_title_h() + UK_BAND + 2 * UK_INSET + lines * uk_line_h()
                + UK_BAND + ml_list_height(1);
        Page cp = page_caption(chh, pc ? pc->name : "", back, KEY_ENTER);
        ML_Rect r = cp.r;
        top = cp.outer.y;
        int ty = r.y + uk_title_h() + UK_BAND;
        uk_flow(r.x + UK_INSET, ty + UK_INSET, tw, r.x, 0, r.y + r.h, desc, PAL_CLR(WHITE));
        int ry = r.y + r.h - ml_list_height(1);
        lattice_band_h(r.x, ry - UK_BAND, r.w, UK_BAND);
        ml_list_draw(r.x, ry, r.w, ml_list_height(1), 1, 0,
                     class_confirm_row, NULL, TOUCH_LIST_CLASS_CONFIRM,
                     uk_ink());
    } else {
        Page cp = page_caption(uk_title_h() + UK_BAND, res->ui.startup_class_select_hint, back, 0);
        top = cp.outer.y;
    }

    // Touch: each figure is a column of the painting, down to the caption; a
    // tap on one picks it.
    if (picker_shown) {
        int fh = (top < py + ph ? top : py + ph) - py;
        for (int k = 0; k < n; k++)
            ui_tile_row(px + k * (pw / n), py, pw / n, fh, TOUCH_LIST_CLASS, k);
    }
}

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

            // Choosing a class is two steps, the same two for every input:
            // pick a figure (it takes the gold outline and its description
            // comes up), then Continue -- so the choice is on screen before
            // it is made.
            int tapped = touch_tapped_row(TOUCH_LIST_CLASS);
            if (tapped >= 0 && tapped < n) class_cursor = tapped;
            if (class_cursor >= 0) {
                // A tap on Continue is the confirmation; Enter is the same
                // row from a keyboard.
                int crow = touch_tapped_row(TOUCH_LIST_CLASS_CONFIRM);
                enter = (crow == 0) || enter;
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
        draw_class_select(res, sprites, n, class_cursor);

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

// ---------------------------------------------------------------------------
// Modern new game: one question a screen -- class, then difficulty, then the
// name -- each a step back from the next: Back on the name goes to the
// difficulty, on the difficulty to the class, on the class to the title.
// ---------------------------------------------------------------------------

// The backdrop both screens wear: the chosen figure's frame of the painting.
static void new_game_backdrop(const Sprites *sprites, const ClassDef *cls) {
    Texture2D bg = sprites ? sprites->class_picker : (Texture2D){ 0 };
    if (sprites && cls && cls->index >= 0 && cls->index < sprites->class_picker_selected_count &&
        sprites->class_picker_selected[cls->index].id)
        bg = sprites->class_picker_selected[cls->index];
    if (bg.id) page_art(bg, NULL);
}

// Difficulty: a menu page over the painting -- its class as the title, the
// column words over the rows, the four with their days and scores, then Back
// on the foot.
static void draw_difficulty_modern(const Resources *res, const Sprites *sprites,
                                   const ClassDef *cls, int sel) {
    DiffCtx dc = { res, { 0 }, { 0 }, true };
    for (int i = 0; i < 4; i++) {
        dc.labels[i] = res ? res->ui.difficulty[i].label : "";
        dc.scores[i] = res ? res->ui.difficulty[i].score_mult : "";
    }
    const int n = 4;
    new_game_backdrop(sprites, cls);
    ML_Rect b = page_menu_body(cls ? cls->name : "", NULL, 0);
    int ty = b.y + UK_INSET;
    // The pack's three column words over the rows they head.
    static const int col[3] = { 0, 13, 21 };
    char hdr[64];
    snprintf(hdr, sizeof hdr, "%s", res->ui.startup_new_game_table_header);
    char *hp = hdr;
    for (int k = 0; k < 3 && *hp; k++) {
        while (*hp == ' ') hp++;
        char *tok = hp;
        while (*hp && *hp != ' ') hp++;
        if (*hp) *hp++ = '\0';
        int hx = (k == 2) ? b.x + b.w - UK_INSET - bfont_text_width(tok)
                          : b.x + UK_INSET + col[k] * GW;
        if (*tok) bfont_draw(tok, hx, ty, PAL_CLR(YELLOW));
    }
    ty += uk_line_h() + UK_INSET;
    lattice_band_h(b.x, ty - UK_BAND, b.w, UK_BAND);
    DiffRowsCtx rc = { &dc, res ? res->ui.gm_back : "Back" };
    ml_rows_draw((ML_Rect){ b.x, ty, b.w, b.y + b.h - ty }, n + 1, 1, sel, difficulty_or_cancel_row,
                 &rc, TOUCH_LIST_STARTUP);
}

// Difficulty: four rows and Back, live from the moment the screen opens.
static bool run_difficulty_modern(const Resources *res, const Sprites *sprites,
                                  RenderTexture2D *rt, StartupChoice *out) {
    screen_open();
    const ClassDef *cls = class_by_id(out->class_id);
    static const Difficulty diff_order[4] = {
        DIFFICULTY_EASY, DIFFICULTY_NORMAL, DIFFICULTY_HARD, DIFFICULTY_IMPOSSIBLE,
    };
    const int n = 4;
    int sel = 1;   // Normal
    for (int i = 0; i < n; i++) if (diff_order[i] == out->difficulty && out->name[0]) sel = i;

    while (!frame_host_should_close()) {
        MlList l = { n + 1, sel, NULL, NULL };  // the four, then Back
        int row = -1;
        MlEvent ev = ml_list_input(&l, TOUCH_LIST_STARTUP, &row);
        sel = l.cursor;
        if (ev == ML_EV_BACK || (ev == ML_EV_ACT && row == n)) {
            out->action = STARTUP_BACK;      // to the class
            advance_input_frame();
            return true;
        }
        if (ev == ML_EV_ACT && row >= 0 && row < n) {
            out->difficulty = diff_order[row];
            out->action = STARTUP_NEW;
            advance_input_frame();
            drain_char_queue();
            return true;
        }

        frame_begin(rt);
        draw_difficulty_modern(res, sprites, cls, sel);
        frame_end(rt);
    }
    out->action = STARTUP_QUIT;
    return false;
}

// The name page's Back row.
static bool name_back_row(void *ctx, int i, char *label, char *right, int cap) {
    (void)i;
    snprintf(label, (size_t)cap, "%s", (const char *)ctx);
    ml_exit_hint(right);
    return true;
}

// Name: a menu page over the painting -- the field, the letter grid while a
// finger or a pad is what the player last used, and Back on the foot.
static void draw_name_modern(const Resources *res, const Sprites *sprites,
                             const ClassDef *cls, const char *name_buf, int name_len,
                             bool caret, bool selector, const TextSel *ts, bool on_back) {
    new_game_backdrop(sprites, cls);
    ML_Rect b = page_menu_body(cls ? cls->name : "", NULL, 0);
    const int lh = uk_line_h();
    int cx0 = b.x + UK_INSET, ty = b.y + UK_INSET;
    const char *label = res->ui.hero_name_label;
    bfont_draw(label, cx0, ty, PAL_CLR(YELLOW));
    int fx = cx0 + bfont_text_width(label) + GW;
    if (name_len > 0) bfont_draw(name_buf, fx, ty, PAL_CLR(WHITE));
    else              bfont_draw(res->world.default_name, fx, ty, PAL_CLR(DGREY));
    if (caret && name_len < 10)
        gfx_rect(fx + bfont_text_width(name_buf), ty + GH - 2, GW, 2, PAL_CLR(YELLOW));
    ty += lh + UK_INSET;
    lattice_band_h(b.x, ty - UK_BAND, b.w, UK_BAND);

    int foot_y = b.y + b.h - ml_list_height(1);
    if (selector) {
        // The letter grid takes what the page leaves it.
        int room = foot_y - UK_BAND - ty - UK_INSET;
        textsel_layout(false, b.w - 2 * UK_INSET, room);
        int cw = textsel_cell_w(), chh = textsel_cell_h();
        int gx = b.x + (b.w - textsel_w(false, cw)) / 2;
        TextSel shown = *ts;
        if (on_back) shown.cursor = -1;
        textsel_draw(&shown, gx, ty + UK_INSET / 2, cw, chh,
                     PAL_CLR(YELLOW), uk_ink(), TOUCH_LIST_TEXTSEL);
    } else {
        // Typing: what the keys do, in the prompt's own words.
        uk_flow(cx0, ty + UK_INSET / 2, b.w - 2 * UK_INSET, cx0, 0, foot_y - UK_BAND,
                res->ui.prompt_text_hint, PAL_CLR(WHITE));
    }
    lattice_band_h(b.x, foot_y - UK_BAND, b.w, UK_BAND);
    ml_list_draw(b.x, foot_y, b.w, ml_list_height(1), 1, on_back ? 0 : -1, name_back_row,
                 (void *)(res ? res->ui.gm_back : "Back"), TOUCH_LIST_STARTUP, uk_ink());
}

// Name: the field, and the way to type into it. Typed letters are taken
// whatever was used last; the grid shows while a finger or a pad was.
static bool run_name_modern(const Resources *res, const Sprites *sprites,
                            RenderTexture2D *rt, StartupChoice *out) {
    screen_open();
    const ClassDef *cls = class_by_id(out->class_id);
    char name_buf[11] = { 0 };
    int  name_len = 0;
    double blink = 0;
    TextSel ts = { 0, false };
    bool on_back = false;       // the grid's cursor has gone down to Back

    while (!frame_host_should_close()) {
        bool selector = input_last_device() != INPUT_DEV_KEYS;
        bool done = false;
        int back = touch_tapped_row(TOUCH_LIST_STARTUP);
        if (input_key_pressed(KEY_ESCAPE) || back == 0 ||
            (on_back && (input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER)))) {
            out->action = STARTUP_BACK;      // to the difficulty
            advance_input_frame();
            return true;
        }
        if (selector && on_back) {
            // Back is under the grid: Up returns to the grid's last row.
            if (input_key_pressed(KEY_UP) || input_key_pressed(KEY_KP_8)) on_back = false;
        } else if (selector) {
            // Down from the grid's last row reaches Back.
            int cols = textsel_cols(false), n = textsel_count(false);
            bool last_row = ts.cursor / cols == (n - 1) / cols;
            if (last_row && (input_key_pressed(KEY_DOWN) || input_key_pressed(KEY_KP_2))) on_back = true;
            else done = textsel_input(&ts, name_buf, &name_len, (int)sizeof name_buf,
                                      TOUCH_LIST_TEXTSEL, name_char_allowed);
        }
        if (!selector && (input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER))) done = true;
        if (done) {
            if (name_len == 0) {
                safe_copy(name_buf, sizeof name_buf, res->world.default_name);
                name_len = (int)strlen(name_buf);
            }
            if (name_buf[0] >= 'a' && name_buf[0] <= 'z')
                name_buf[0] = (char)(name_buf[0] - 'a' + 'A');
            safe_copy(out->name, sizeof(out->name), name_buf);
            out->action = STARTUP_NEW;
            advance_input_frame();
            drain_char_queue();
            return true;
        }
        // Typing is taken whatever the player used last.
        if (input_key_pressed(KEY_BACKSPACE) && name_len > 0 && !selector) {
            name_buf[--name_len] = '\0';
        } else {
            int ch = input_get_char_pressed();
            while (ch > 0) {
                if (name_char_allowed(name_buf, name_len, ch)) {
                    name_buf[name_len++] = (char)ch;
                    name_buf[name_len] = '\0';
                }
                ch = input_get_char_pressed();
            }
        }

        blink += (float)frame_host_delta();
        bool caret = (int)(blink * 2.0) & 1;

        frame_begin(rt);
        draw_name_modern(res, sprites, cls, name_buf, name_len, caret, selector, &ts, on_back);
        frame_end(rt);
    }
    out->action = STARTUP_QUIT;
    return false;
}

// Legacy: name and difficulty on one panel, over the class-select cartoon.
// The name is typed first; the difficulty rows take the keys once it is in.
static void draw_create_game_legacy(const Resources *res, const Sprites *sprites,
                                    const char *class_title, const char *name_buf,
                                    bool has_name, bool show_caret, int sel) {
    int name_len = (int)strlen(name_buf);
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

    // Labels + score-multiplier text from res.ui.difficulty (game.json
    // strings.difficulty).
    struct { const char *label, *score; } rows[4];
    for (int i = 0; i < 4; i++) {
        rows[i].label = (res ? res->ui.difficulty[i].label : "");
        rows[i].score = (res ? res->ui.difficulty[i].score_mult : "");
    }

    // Rows 5-8: difficulty table:
    //   "   Easy         %3d    x.5 "  (11-char label, %3d days, score)
    for (int i = 0; i < 4; i++) {
        int days = res ? res->time.days_per_difficulty[i] : 0;
        char line[32];
        snprintf(line, sizeof(line), "   %-11s %3d    %s",
                 rows[i].label, days, rows[i].score);
        bfont_draw(line, x + GW, ROW_Y(5 + i), PAL_CLR(WHITE));
        if (has_name)
            ui_tile_row(x, ROW_Y(5 + i), w, GH,
                             TOUCH_LIST_STARTUP, i);
    }

    // After has_name: draw the ">" cursor at col 0 of the selected row,
    // and the "^v to select   Ent to Accept" hint on row 10.
    if (has_name) {
        // ">" cursor -- print sel==i ? ">\n" : " \n" at
        //   menu.x + fs->w, menu.y + fs->h * 5  (i.e. col 1, row 5)
        // and walks down 4 rows. The cursor column is the same column as
        // the "   " prefix in the difficulty lines (col 1).
        bfont_draw(">", x + GW, ROW_Y(5 + sel), PAL_CLR(WHITE));

        // Hint on row 10: "\x18\x19 to select   Ent to Accept"
        // -- 0x18 and 0x19 are CP437 up/down arrows. Our bfont is ASCII-
        // only, so substitute "^v".
        bfont_draw(res->ui.startup_new_game_select_hint,
                   x + GW, ROW_Y(10), PAL_CLR(WHITE));
    }
    #undef ROW_Y
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
    screen_open();

    // Look up class title via out->class_id (set by run_class_select).
    const ClassDef *cls = class_by_id(out->class_id);
    const char *class_title = cls->name;

    // Difficulty per row, in the order of res.ui.difficulty (game.json
    // strings.difficulty); enum order matches the JSON key order.
    static const Difficulty diff_order[4] = {
        DIFFICULTY_EASY, DIFFICULTY_NORMAL,
        DIFFICULTY_HARD, DIFFICULTY_IMPOSSIBLE,
    };
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
            // The window keyboard takes the name on a touch screen.
            touch_request(TOUCH_CHROME_KEYBOARD);
            // Name entry phase. Enter confirms; BACKSPACE deletes; alpha/
            // digit/space appends.
            if (input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER)) {
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
            } else if (input_key_pressed(KEY_BACKSPACE) && name_len > 0) {
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
                out->difficulty = diff_order[sel];
                safe_copy(out->name, sizeof(out->name), name_buf);
                out->action = STARTUP_NEW;
                return true;
            }
        }

        cursor_blink += (float)frame_host_delta();
        bool show_caret = (int)(cursor_blink * 2.0) & 1;

        frame_begin(rt);
        draw_create_game_legacy(res, sprites, class_title, name_buf, has_name,
                                show_caret, sel);
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

// The body the intro shows: "<Name> the <Class>, a new game is being created".
static void new_game_intro_body(const Resources *res, const char *class_id,
                                const char *name, char *body, size_t cap) {
    const ClassDef *cls = class_by_id(class_id);
    const char *class_title = cls->name;
    ResTemplateVar vars[2] = {
        { "NAME",  name && name[0] ? name : res->world.default_name },
        { "CLASS", class_title },
    };
    resources_format_template(body, cap, res->banners.new_game_intro, vars, 2);
}

static void draw_new_game_intro(const Resources *res, const Sprites *sprites,
                                const char *body) {
    // Panel size from the body's longest line.
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

    // Background: same class-select cartoon (sprites->class_picker).
    draw_class_picker_backdrop(sprites);

    // Status hint at top.
    startup_bar(res->ui.startup_class_select_hint);

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

    char body[RES_BANNER_LEN];
    new_game_intro_body(res, out->class_id, name, body, sizeof body);

    screen_open();
    double start = frame_host_time();
    double timeout = 4.0;
    while (!frame_host_should_close()) {
        if (any_key_pressed() || (frame_host_time() - start) >= timeout) return true;

        frame_begin(rt);
        draw_new_game_intro(res, sprites, body);
        frame_end(rt);
    }
    return false;
}

// The credits: group labels and names, the copyright lines, the class-select
// highlight inset at the right. Modern adds Back along the foot.
static void draw_credits(const Resources *res, const Sprites *sprites) {
    int gn = res->credits.group_count;
    int cn = res->credits.copyright_count;
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

    int px, py;
    if (CL_IS_MODERN) {
        // A page to read over the title art: a menu page's size, its title
        // strip with Close, and a tap anywhere closes it. Its lines are every
        // page's lines apart.
        pad = UK_INSET;
        line_h = uk_line_h();
        draw_title_backdrop(sprites);
        ML_Rect r = page_sheet_small(res->ui.title_credits, NULL);
        px = r.x;
        py = r.y;
        panel_w = r.w;
        panel_h = r.h;
    } else {
        // Legacy drapes the credits over the character-pick screen.
        px = (CL_SCREEN_W - panel_w) / 2;
        py = (CL_SCREEN_H - panel_h) / 2;
        draw_class_picker_backdrop(sprites);
        draw_class_picker_status_hint(res);
        panel(px, py, panel_w, panel_h);
    }

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

}

static bool run_credits(RenderTexture2D *rt, const Resources *res,
                        const Sprites *sprites) {
    if (!res) return true;
    if (res->credits.group_count == 0 && res->credits.copyright_count == 0) return true;

    screen_open();
    double start = frame_host_time();
    double timeout = 2.5;
    while (!frame_host_should_close()) {
        // Modern opens the credits from the title menu, so they stay up until
        // a key; legacy runs them once at startup on a timer.
        if (any_key_pressed() || (!CL_IS_MODERN && (frame_host_time() - start) >= timeout))
            return true;

        frame_begin(rt);
        draw_credits(res, sprites);
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
        // New Game: class, then difficulty, then the name -- one question a
        // screen, Back on each going one step back: from the name to the
        // difficulty, from the difficulty to the class, from the class to the
        // title menu.
        int step = 0;
        while (step >= 0 && step < 3) {
            bool ok = step == 0 ? run_class_select(res, sprites, rt, out)
                    : step == 1 ? run_difficulty_modern(res, sprites, rt, out)
                                : run_name_modern(res, sprites, rt, out);
            if (!ok) return false;
            step += out->action == STARTUP_BACK ? -1 : 1;
        }
        if (step < 0) continue;
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

// ---------------------------------------------------------------------------
// --gallery: one pre-game screen, drawn as its loop draws it, no input read.
// ---------------------------------------------------------------------------

bool startup_gallery_draw(StartupShot shot, const Resources *res,
                          const Sprites *sprites, void *render_target) {
    RenderTexture2D *rt = (RenderTexture2D *)render_target;
    if (!res || !sprites || !rt) return false;
    // The new-game screens show the second class, as a player who stepped
    // one figure along would see them.
    const ClassDef *cls = class_by_index(res->classes_count > 1 ? 1 : 0);
    int n = res->classes_count < 1 ? 1 : res->classes_count;
    if (!CL_IS_MODERN && n > 4) n = 4;
    SlotSet none;
    memset(&none, 0, sizeof none);
    const Color black = { 0x00, 0x00, 0x00, 0xFF };

    switch (shot) {
    case STARTUP_SHOT_LOGO:
        if (!sprites->splash_logo.id) return false;
        frame_begin(rt);
        draw_splash(sprites->splash_logo, black);
        break;
    case STARTUP_SHOT_TITLE:
        if (CL_IS_MODERN) {
            const char *labels[ROW_COUNT];
            title_menu_labels(&res->ui, labels);
            frame_begin(rt);
            draw_title_backdrop(sprites);
            draw_title_menu(sprites, labels, ROW_COUNT, 0, TOUCH_LIST_STARTUP);
        } else {
            if (!sprites->splash_title.id) return false;
            frame_begin(rt);
            draw_splash(sprites->splash_title, black);
        }
        break;
    case STARTUP_SHOT_CREDITS:
        if (res->credits.group_count == 0 && res->credits.copyright_count == 0) return false;
        frame_begin(rt);
        draw_credits(res, sprites);
        break;
    case STARTUP_SHOT_LOAD:
        frame_begin(rt);
        if (CL_IS_MODERN) {
            GmPage p;
            load_page_build(&p, res, &none);
            TitleSlotsCtx ctx = { &p, &none };
            draw_save_picker_modern(sprites, &p, load_page_cursor(&none), &ctx);
        } else {
            draw_save_picker_legacy(sprites, &none, SAVE_SLOT_COUNT, SAVE_SLOT_COUNT);
        }
        break;
    case STARTUP_SHOT_CLASS:
        frame_begin(rt);
        draw_class_select(res, sprites, n, CL_IS_MODERN ? -1 : 0);
        break;
    case STARTUP_SHOT_CLASS_PICKED:
        if (!CL_IS_MODERN) return false;      // legacy picks on a key, at once
        frame_begin(rt);
        draw_class_select(res, sprites, n, cls ? cls->index : 0);
        break;
    case STARTUP_SHOT_DIFFICULTY:
        frame_begin(rt);
        if (CL_IS_MODERN) draw_difficulty_modern(res, sprites, cls, 1);
        else draw_create_game_legacy(res, sprites, cls ? cls->name : "", "Dan", true, false, 1);
        break;
    case STARTUP_SHOT_NAME:
        frame_begin(rt);
        if (CL_IS_MODERN) {
            bool selector = input_last_device() != INPUT_DEV_KEYS;
            TextSel ts = { 0, false };
            draw_name_modern(res, sprites, cls, "Dan", 3, true, selector, &ts, false);
        } else {
            draw_create_game_legacy(res, sprites, cls ? cls->name : "", "Dan", false, true, 1);
        }
        break;
    case STARTUP_SHOT_INTRO: {
        if (CL_IS_MODERN || !res->banners.new_game_intro[0]) return false;
        char body[RES_BANNER_LEN];
        new_game_intro_body(res, cls ? cls->id : "", "Dan", body, sizeof body);
        frame_begin(rt);
        draw_new_game_intro(res, sprites, body);
        break;
    }
    default:
        return false;
    }
    frame_end(rt);
    return true;
}
