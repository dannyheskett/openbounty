#include "chrome.h"
#include "layout.h"
#include "palette.h"
#include "bfont.h"
#include "resources.h"
#include "views.h"
#include "ui.h"
#include <stdbool.h>
#include <stdio.h>

// Defined in main.c; chrome reads it to know whether to render the
// fast-quit prompt in the status bar.
extern bool main_fast_quit_active(void);

// Outerworld chrome: a pixel-exact 320x200 bitmap. The bitmap carries
// the outer frame (left/right 16px, top 8px, bottom 8px) with transparent
// interior. Status bar background, bar strip, and status text are painted
// procedurally on top because they're dynamic (difficulty color, text
// contents, mode).

// Status bar background color, sourced from res->colors.difficulty_*
// (game.json colors.difficulty_bar). Defaults match the canonical
// Easy/Normal/Hard/Impossible bar colors unless a mod overrides.
static Color color_from_packed(unsigned int v) {
    return (Color){
        (unsigned char)((v >> 16) & 0xFF),
        (unsigned char)((v >>  8) & 0xFF),
        (unsigned char)( v        & 0xFF),
        (unsigned char)((v >> 24) & 0xFF),
    };
}

static Color status_bg_for_difficulty(Difficulty d) {
    const Resources *res = resources_current();
    if (res) {
        const ResColors *c = &res->colors;
        switch (d) {
            case DIFFICULTY_EASY:       return color_from_packed(c->difficulty_easy);
            case DIFFICULTY_NORMAL:     return color_from_packed(c->difficulty_normal);
            case DIFFICULTY_HARD:       return color_from_packed(c->difficulty_hard);
            case DIFFICULTY_IMPOSSIBLE: return color_from_packed(c->difficulty_impossible);
        }
    }
    // Fallback (resources not yet loaded).
    switch (d) {
        case DIFFICULTY_EASY:       return (Color){ 0x00, 0xAA, 0xAA, 0xFF };
        case DIFFICULTY_NORMAL:     return (Color){ 0xAA, 0x00, 0x00, 0xFF };
        case DIFFICULTY_HARD:       return (Color){ 0x55, 0x55, 0xFF, 0xFF };
        case DIFFICULTY_IMPOSSIBLE: return (Color){ 0xAA, 0x00, 0xAA, 0xFF };
    }
    return (Color){ 0x00, 0xAA, 0xAA, 0xFF };
}

// Draw chrome shell + a custom status text. Used by combat so the
// title bar reads "Options / <Actor> M<n>" or "<Player> vs <Foe>
// killing <N>" without going through the adventure-mode time-stop /
// days-left paths. Pass status_text=NULL to skip status text.
void chrome_draw_with_status(const Game *g, const Sprites *s,
                                     const char *status_text) {
    // Caller has already painted the inner area (combat field, modal
    // body, etc). Do NOT full-screen black here — that would erase
    // everything below us. The chrome bitmap below has a transparent
    // interior, so the field shows through.
    Color status_bg = status_bg_for_difficulty(
        g ? g->character.difficulty : DIFFICULTY_NORMAL);
    DrawRectangle(CL_STATUS_X, CL_STATUS_Y, CL_STATUS_W, CL_STATUS_H,
                  status_bg);
    if (s && s->hud_bar_strip.id) {
        Rectangle src = { 0, 0,
                          (float)s->hud_bar_strip.width,
                          (float)s->hud_bar_strip.height };
        Rectangle dst = { 0, (float)CL_BAR_Y,
                          (float)CL_SCREEN_W,
                          (float)s->hud_bar_strip.height };
        DrawTexturePro(s->hud_bar_strip, src, dst,
                       (Vector2){ 0, 0 }, 0.0f, WHITE);
    }
    if (s && s->chrome_overworld.id) {
        Rectangle src = { 0, 0,
                          (float)s->chrome_overworld.width,
                          (float)s->chrome_overworld.height };
        Rectangle dst = { 0, 0,
                          (float)CL_SCREEN_W, (float)CL_SCREEN_H };
        DrawTexturePro(s->chrome_overworld, src, dst,
                       (Vector2){ 0, 0 }, 0.0f, WHITE);
    }
    if (status_text && status_text[0]) {
        bfont_draw(status_text, CL_STATUS_X + 1, CL_STATUS_Y + 1,
                   PAL_CLR(WHITE));
    }
}

void chrome_draw(const Game *g, const Sprites *s) {
    // Fill the whole screen black. The chrome bitmap paints the frame on
    // top; map / sidebar / views paint the interior on top.
    DrawRectangle(0, 0, CL_SCREEN_W, CL_SCREEN_H, PAL_CLR(BLACK));

    // Status bar fill (y=8..16, x=16..303) with difficulty color.
    Color status_bg = status_bg_for_difficulty(
        g ? g->character.difficulty : DIFFICULTY_NORMAL);
    DrawRectangle(CL_STATUS_X, CL_STATUS_Y, CL_STATUS_W, CL_STATUS_H,
                  status_bg);

    // Middle bar (bar_strip.png) at y=17, 5px tall. 320 wide; the chrome
    // bitmap's side columns will paint over the outer 16px after this.
    if (s && s->hud_bar_strip.id) {
        Rectangle src = { 0, 0,
                          (float)s->hud_bar_strip.width,
                          (float)s->hud_bar_strip.height };
        Rectangle dst = { 0, (float)CL_BAR_Y,
                          (float)CL_SCREEN_W,
                          (float)s->hud_bar_strip.height };
        DrawTexturePro(s->hud_bar_strip, src, dst,
                       (Vector2){ 0, 0 }, 0.0f, WHITE);
    }

    // Blit the chrome bitmap over everything. Its interior is transparent
    // so the status bar + bar strip drawn above remain visible.
    if (s && s->chrome_overworld.id) {
        Rectangle src = { 0, 0,
                          (float)s->chrome_overworld.width,
                          (float)s->chrome_overworld.height };
        Rectangle dst = { 0, 0,
                          (float)CL_SCREEN_W, (float)CL_SCREEN_H };
        DrawTexturePro(s->chrome_overworld, src, dst,
                       (Vector2){ 0, 0 }, 0.0f, WHITE);
    }

    // Status text (white, on top of the fill). Three modes:
    //   - Fast-quit (Ctrl+Q): "Quit without saving (y/n)". Highest
    //     priority -- a modal status-bar prompt that overrides
    //     everything else until the player answers y/n.
    //   - Special screens (views_wants_exit_hint): centered
    //     "Press 'ESC' to exit".
    //   - Adventure mode (default): "Days Left:N" / "Time Stop:N".
    if (g) {
        const ResUI *ui = (g->res) ? &g->res->ui : NULL;
        if (main_fast_quit_active()) {
            const char *txt = (ui && ui->quit_to_dos_prompt[0])
                              ? ui->quit_to_dos_prompt
                              : " Quit without saving (y/n) ";
            bfont_draw_centered(txt,
                                CL_STATUS_X + CL_STATUS_W / 2,
                                CL_STATUS_Y + 1,
                                PAL_CLR(WHITE));
        } else if (views_wants_exit_hint() || dialog_is_active()) {
            const char *txt = (ui && ui->press_esc_to_exit[0])
                              ? ui->press_esc_to_exit
                              : "Press 'ESC' to exit";
            bfont_draw_centered(txt,
                                CL_STATUS_X + CL_STATUS_W / 2,
                                CL_STATUS_Y + 1,
                                PAL_CLR(WHITE));
        } else {
            char buf[64], nbuf[16];
            const ResBanners *bn = (g->res) ? &g->res->banners : NULL;
            if (g->stats.time_stop > 0) {
                snprintf(nbuf, sizeof nbuf, "%d", g->stats.time_stop);
                ResTemplateVar vars[] = { { "STEPS", nbuf } };
                if (bn) {
                    resources_format_template(buf, sizeof buf,
                                              bn->status_time_stop, vars, 1);
                } else {
                    snprintf(buf, sizeof buf,
                             " Options / Controls / Time Stop:%d ",
                             g->stats.time_stop);
                }
            } else {
                snprintf(nbuf, sizeof nbuf, "%d", g->stats.days_left);
                ResTemplateVar vars[] = { { "DAYS", nbuf } };
                if (bn) {
                    resources_format_template(buf, sizeof buf,
                                              bn->status_days_left, vars, 1);
                } else {
                    snprintf(buf, sizeof buf,
                             " Options / Controls / Days Left:%d ",
                             g->stats.days_left);
                }
            }
            bfont_draw(buf, CL_STATUS_X + 1, CL_STATUS_Y + 1, PAL_CLR(WHITE));
        }
    }
}
