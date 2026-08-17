#include "chrome.h"
#include "layout.h"
#include "palette.h"
#include "bfont.h"
#include "resources.h"
#include "views.h"
#include "ui.h"
#include <stdbool.h>
#include <stdio.h>

// Defined in src/shell_fastquit.c (stubbed in engine/host_noop.c); chrome
// reads it to know whether to render the fast-quit prompt in the status bar.
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


// The bar strip under the status line. Repeated along its length rather than
// stretched, for the same reason the frame's edge bands are: it is a pattern
// with a pitch, and a screen-wide stretch smears it by however much wider the
// window is than the source. Its height is the band's, CL_BAR_H, not the
// texture's -- drawing at the texture height left a 5px strip in a 10px band
// at ui_scale 2.
static void draw_bar_strip(Texture2D tex) {
    if (tex.id == 0 || tex.width <= 0) return;
    const int h = CL_BAR_H;
    for (int x = 0; x < CL_SCREEN_W; x += tex.width) {
        int run = (x + tex.width > CL_SCREEN_W) ? (CL_SCREEN_W - x) : tex.width;
        Rectangle src = { 0, 0, (float)run, (float)tex.height };
        Rectangle dst = { (float)x, (float)CL_BAR_Y, (float)run, (float)h };
        DrawTexturePro(tex, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
    }
}

static void draw_chrome_frame(Texture2D tex) {
    const int W = CL_SCREEN_W, H = CL_SCREEN_H;
    const int tw = tex.width, th = tex.height;

    if (tw == W && th == H) {
        Rectangle src = { 0, 0, (float)tw, (float)th };
        Rectangle dst = { 0, 0, (float)W, (float)H };
        DrawTexturePro(tex, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
        return;
    }

    const int cw = CL_FRAME_LEFT_W;   // corner / side-band width
    const int ch = CL_FRAME_TOP_H;    // corner / top-band height
    if (tw <= 2 * cw || th <= 2 * ch) return;

    // Corners, 1:1.
    struct { int sx, sy, dx, dy; } corner[4] = {
        { 0,          0,          0,      0      },
        { tw - cw,    0,          W - cw, 0      },
        { 0,          th - ch,    0,      H - ch },
        { tw - cw,    th - ch,    W - cw, H - ch },
    };
    for (int i = 0; i < 4; i++) {
        Rectangle src = { (float)corner[i].sx, (float)corner[i].sy,
                          (float)cw, (float)ch };
        Rectangle dst = { (float)corner[i].dx, (float)corner[i].dy,
                          (float)cw, (float)ch };
        DrawTexturePro(tex, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
    }

    // Top and bottom bands: repeat the source's middle span horizontally.
    int span_w = tw - 2 * cw;
    for (int x = cw; x < W - cw; x += span_w) {
        int run = (x + span_w > W - cw) ? (W - cw - x) : span_w;
        Rectangle stop = { (float)cw, 0.0f, (float)run, (float)ch };
        Rectangle dtop = { (float)x,  0.0f, (float)run, (float)ch };
        DrawTexturePro(tex, stop, dtop, (Vector2){ 0, 0 }, 0.0f, WHITE);
        Rectangle sbot = { (float)cw, (float)(th - ch), (float)run, (float)ch };
        Rectangle dbot = { (float)x,  (float)(H  - ch), (float)run, (float)ch };
        DrawTexturePro(tex, sbot, dbot, (Vector2){ 0, 0 }, 0.0f, WHITE);
    }

    // Left and right bands: repeat the source's middle span vertically.
    int span_h = th - 2 * ch;
    for (int y = ch; y < H - ch; y += span_h) {
        int run = (y + span_h > H - ch) ? (H - ch - y) : span_h;
        Rectangle sl = { 0.0f,             (float)ch, (float)cw, (float)run };
        Rectangle dl = { 0.0f,             (float)y,  (float)cw, (float)run };
        DrawTexturePro(tex, sl, dl, (Vector2){ 0, 0 }, 0.0f, WHITE);
        Rectangle sr = { (float)(tw - cw), (float)ch, (float)cw, (float)run };
        Rectangle dr = { (float)(W  - cw), (float)y,  (float)cw, (float)run };
        DrawTexturePro(tex, sr, dr, (Vector2){ 0, 0 }, 0.0f, WHITE);
    }
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
    // body, etc). Do NOT full-screen black here -- that would erase
    // everything below us. The chrome bitmap below has a transparent
    // interior, so the field shows through.
    Color status_bg = status_bg_for_difficulty(
        g ? g->character.difficulty : DIFFICULTY_NORMAL);
    DrawRectangle(CL_STATUS_X, CL_STATUS_Y, CL_STATUS_W, CL_STATUS_H,
                  status_bg);
    if (s) draw_bar_strip(s->hud_bar_strip);
    if (s && s->chrome_overworld.id) {
        draw_chrome_frame(s->chrome_overworld);
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
    if (s) draw_bar_strip(s->hud_bar_strip);

    // Blit the chrome bitmap over everything. Its interior is transparent
    // so the status bar + bar strip drawn above remain visible.
    if (s && s->chrome_overworld.id) {
        draw_chrome_frame(s->chrome_overworld);
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
            bfont_draw_centered(ui->press_esc_to_exit,
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
