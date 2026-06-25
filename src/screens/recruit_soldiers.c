// Recruit-soldiers screen.
//
// Structure:
//   - home_troops[5] = troops with dwells == DWELLING_CASTLE
//   - twirl glyphs cycle on SYN tick
//   - state variable `whom`: 0 = idle (twirl on right), 1..5 = letter
//     picked, count entry on right
//   - max recomputed on letter pick AND after each successful purchase
//   - inline text input at right column when whom != 0
//   - silent clamp `if (number > max) number = max`
//   - errors use bottom-banner pause boxes
//
// The screen owns its own input handling. main.c just calls
// screen_recruit_soldiers_update() each frame and the screen returns
// SCREEN_RESULT_DISMISS when ESC is pressed at idle.

#include "input_host.h"
#include "recruit_soldiers.h"
#include "layout.h"
#include "palette.h"
#include "bfont.h"
#include "views.h"
#include "tables.h"
#include "resources.h"
#include "game.h"
#include "ui.h"
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Backdrop accessor exposed by overlay.c.
extern void screens_draw_location_backdrop(const Game *g, const Sprites *s,
                                           int loc_kind, int troop_idx,
                                           int troop_frame);
#define SCREEN_LOC_CASTLE 1

// home_troops filtered by dwelling=="castle".
#define RECRUIT_SLOTS 5
static int s_pool[RECRUIT_SLOTS];   // catalog indices, in pool-order
static int s_pool_count = 0;

// Animated troop, chosen once at home-castle entry. Stays stable for
// the whole visit; the recruit screen inherits it.
static int s_anim_troop_idx = -1;

// `whom` state:
//   0    = idle, listening for letters, twirl on right
//   1..5 = letter pressed, count entry on right
static int s_whom = 0;

// Max recruitable for the currently selected troop. Recomputed on
// letter pick and on each successful purchase.
static int s_max = 9999;

// troop_frame: 0..3 unit-anim frame.
// twirl_pos:   0..3 cursor twirl.
// troop_delay throttles troop_frame to 1/3 the rate of twirl_pos
//   (twirl advances 3x faster than the troop frame).
static int s_troop_frame = 3;
static int s_twirl_pos   = 0;
static int s_troop_delay = 0;
static double s_last_tick = 0.0;

// Inline text-input buffer, used when s_whom != 0.
// 6 digits max, numeric only.
#define INPUT_MAX_LEN 6
static char s_input_buf[INPUT_MAX_LEN + 1] = { 0 };
static int  s_input_len = 0;

// Error banners are synchronous: they block until any key is pressed.
// We render the error inline by temporarily replacing the panel
// content, then any-key clears it. Empty string = no error pending.
static char s_error_msg[64] = { 0 };
// One-shot edge-detect: don't dismiss the error on the same frame
// it's set (the key that triggered it would also clear it instantly).
static bool s_error_just_set = false;

void screen_recruit_soldiers_open(Game *g) {
    if (!g) return;

    // Build home_troops[5]: iterate the troop catalog and pick all
    // dwelling="castle" entries. The recruit list is rendered in
    // ascending recruit_cost order (Militia 50 -> Archers 250 ->
    // Pikemen 300 -> Cavalry 800 -> Knights 1000), so we sort here
    // since catalog order does not guarantee that.
    s_pool_count = 0;
    int total = troops_count();
    for (int i = 0; i < total && s_pool_count < RECRUIT_SLOTS; i++) {
        const TroopDef *t = troop_by_index(i);
        if (!t) continue;
        if (strcmp(t->dwelling, "castle") == 0) {
            s_pool[s_pool_count++] = i;
        }
    }
    // Insertion sort by recruit_cost ascending -- n <= 5, trivial.
    for (int i = 1; i < s_pool_count; i++) {
        int key = s_pool[i];
        const TroopDef *kt = troop_by_index(key);
        int kcost = kt ? kt->recruit_cost : 0;
        int j = i - 1;
        while (j >= 0) {
            const TroopDef *jt = troop_by_index(s_pool[j]);
            int jcost = jt ? jt->recruit_cost : 0;
            if (jcost <= kcost) break;
            s_pool[j + 1] = s_pool[j];
            j--;
        }
        s_pool[j + 1] = key;
    }

    // Pick a deterministic random troop from the pool, seeded from the
    // game seed + days-left so revisiting the same castle on the same
    // day produces the same animated troop.
    if (s_pool_count > 0) {
        unsigned long h = (unsigned long)g->seed ^ 0xC451E11Au;
        h ^= (unsigned long)g->stats.days_left;
        h = h * 2654435761u + 0x9E3779B9u;
        s_anim_troop_idx = s_pool[h % (unsigned long)s_pool_count];
    } else {
        s_anim_troop_idx = -1;
    }

    s_whom = 0;
    s_max  = 9999;
    s_troop_frame = 3;
    s_twirl_pos   = 0;
    s_troop_delay = 0;
    s_input_buf[0] = '\0';
    s_input_len = 0;

    views_push(VIEW_RECRUIT_SOLDIERS);
}

// max = min(leadership_cap, (gold - reserve) / recruit_cost). The
// leadership cap (GameMaxRecruitable) governs how many your army
// can hold; the gold cap governs what you can afford. We reserve a
// small buffer (100g, enough for the cheapest boat rental) so the
// player isn't left totally cashless after a max-out recruit.
// The commit path's silent-clamp uses this s_max, so by bounding
// here we prevent the "no gold" error that fires when entering a
// leadership-allowed count that exceeds the wallet.
// Buffer left after a max-out recruit. Must cover several weeks of
// boat upkeep (500/week) plus the recruit's own week-end deduction,
// so the engine doesn't silently repossess the boat on the next
// week boundary.
#define RECRUIT_GOLD_RESERVE 1000
static int recompute_max(const Game *g, int slot) {
    if (slot < 0 || slot >= s_pool_count) return 0;
    const TroopDef *t = troop_by_index(s_pool[slot]);
    if (!t) return 0;
    int m = GameMaxRecruitable(g, t->id);
    if (m < 0) m = 0;
    if (t->recruit_cost > 0) {
        int budget = g->stats.gold - RECRUIT_GOLD_RESERVE;
        if (budget < 0) budget = 0;
        int affordable = budget / t->recruit_cost;
        if (affordable < m) m = affordable;
    }
    return m;
}

// Idle-state input poll. Returns:
//   1..5 letter pressed, A=1
//   6    SYN tick (animation only)
//   -1   ESC
//   0    nothing
static int poll_idle_input(void) {
    if (input_key_pressed(KEY_ESCAPE)) return -1;
    for (int i = 0; i < 5; i++) {
        if (input_key_pressed(KEY_A + i)) return i + 1;
    }
    // SYN tick at 90ms cadence drives the twirl animation.
    double now = GetTime();
    if (now - s_last_tick >= 0.090) {
        s_last_tick = now;
        return 6;
    }
    return 0;
}

// Numeric text-input poll, inline at right column.
// Accepts digits, backspace, ENTER (commit), ESC (cancel).
// Returns:
//   1  ENTER pressed: digits in s_input_buf are the value
//   2  ESC pressed: cancel (whom -> 0)
//   0  still entering / nothing this frame
static int poll_count_input(void) {
    if (input_key_pressed(KEY_ESCAPE)) return 2;
    if (input_key_pressed(KEY_ENTER) || input_key_pressed(KEY_KP_ENTER)) return 1;
    if (input_key_pressed(KEY_BACKSPACE) && s_input_len > 0) {
        s_input_len--;
        s_input_buf[s_input_len] = '\0';
    }
    for (int d = 0; d < 10; d++) {
        if ((input_key_pressed(KEY_ZERO + d) || input_key_pressed(KEY_KP_0 + d))
            && s_input_len < INPUT_MAX_LEN) {
            s_input_buf[s_input_len++] = (char)('0' + d);
            s_input_buf[s_input_len] = '\0';
        }
    }
    return 0;
}

// The recruit-commit implementation used by the ENTER path
// (screen_recruit_soldiers_update). Clamps `count` to the per-slot max
// (recompute_max) and performs the REAL transaction (GameBuyTroop). Returns
// the GameBuyTroop result code (0 ok, 1 no gold, 2 no slots), or -1 if the
// slot/count is a no-op. `set_err` controls whether a gold/slots failure
// raises the screen's error popup.
static int recruit_commit(Game *g, int pool_slot, int count, bool set_err);

static void set_error(const char *msg) {
    if (!msg) { s_error_msg[0] = '\0'; return; }
    size_t n = 0;
    while (n + 1 < sizeof(s_error_msg) && msg[n]) {
        s_error_msg[n] = msg[n]; n++;
    }
    s_error_msg[n] = '\0';
    s_error_just_set = true;
}

// Called by main.c each frame while VIEW_RECRUIT_SOLDIERS is on top.
// Returns true when the screen should be dismissed (player pressed ESC
// while idle).
bool screen_recruit_soldiers_update(Game *g) {
    if (!g) return true;

    // if an error popup is up, swallow input until
    // the player presses any key. Don't consume the key on the same
    // frame the popup opened.
    if (s_error_msg[0]) {
        if (s_error_just_set) {
            s_error_just_set = false;
        } else if (input_get_key_pressed() != 0) {
            s_error_msg[0] = '\0';
        }
        return false;   // don't dismiss the screen, just wait
    }

    if (s_whom == 0) {
        // listen for letter or SYN.
        int key = poll_idle_input();
        if (key == -1) return true;             // ESC: dismiss
        if (key >= 1 && key <= 5) {
            // pick letter, recompute max.
            // Slot must exist (s_pool_count covers it). 
            // (extended) gates on the same `total_leadership < hp*6`
            // rule the row uses to print "n/a" — unreachable rows
            // must not be selectable, otherwise the player would type
            // a count for a "n/a" troop and recruit it anyway.
            if (key - 1 < s_pool_count) {
                const TroopDef *t = troop_by_index(s_pool[key - 1]);
                bool unreachable = !t ||
                                   t->hit_points <= 0 ||
                                   g->stats.leadership_current
                                       < t->hit_points * 6;
                if (!unreachable) {
                    s_whom = key;
                    s_max = recompute_max(g, s_whom - 1);
                    s_input_buf[0] = '\0';
                    s_input_len = 0;
                }
            }
        } else if (key == 6) {
            // advance twirl every tick, troop_frame
            // every 3 ticks.
            s_twirl_pos++;
            if (s_twirl_pos > 3) s_twirl_pos = 0;
            s_troop_delay++;
            if (s_troop_delay > 2) {
                s_troop_delay = 0;
                s_troop_frame++;
                if (s_troop_frame > 3) s_troop_frame = 0;
            }
        }
    } else {
        // inline numeric input.
        int r = poll_count_input();
        if (r == 2) {
            // ESC: cancel back to idle (source: enter == NULL).
            s_whom = 0;
            s_input_buf[0] = '\0';
            s_input_len = 0;
        } else if (r == 1) {
            // ENTER: commit purchase (atoi → shared clamp+buy). The error
            // popup is raised inside recruit_commit on a gold/slots failure.
            int number = (s_input_len > 0) ? atoi(s_input_buf) : 0;
            recruit_commit(g, s_whom - 1, number, /*set_err=*/true);
            // recompute max after purchase.
            s_max = recompute_max(g, s_whom - 1);
            // UX deviation from source: after committing a count, return
            // to the letter-select state instead of staying in count
            // entry for the same troop. The state machine keeps `whom`
            // set so the user types another count for the same troop;
            // we reset to idle so the player picks a fresh letter.
            s_whom = 0;
            s_input_buf[0] = '\0';
            s_input_len = 0;
        }
    }
    return false;
}

void screen_recruit_soldiers_draw(const Game *g, const Sprites *s) {
    // backdrop with animated troop (random_troop, frame).
    screens_draw_location_backdrop(g, s, SCREEN_LOC_CASTLE,
                                   s_anim_troop_idx, s_troop_frame);

    // Bottom panel — same fixed rect every dialog/screen uses.
    int x = CL_PANEL_X;
    int y = CL_PANEL_Y;
    int w = CL_PANEL_W;
    int h = CL_PANEL_H;
    DrawRectangle(x, y, w, h, PAL_CLR(DBLUE));
    DrawRectangleLines(x, y, w, h, PAL_CLR(YELLOW));

    int pad = 4;
    int row_h = BFONT_GLYPH_H + 1;
    int tx = x + pad;
    int ty = y + pad;

    // when an error popup is up the panel content is
    // replaced by the error text (KB_BottomBox MSG_PAUSE semantics).
    // Body is rendered with 3 leading blank rows ( padding) for
    // the gold error; the slots error fills the panel as-is.
    if (s_error_msg[0]) {
        // 3 blank lines of padding (matches "\n\n\nYou don't have
        // enough gold!" ). Slots error renders without
        // extra padding ().
        int e_ty = ty + 3 * row_h;
        bfont_draw_centered(s_error_msg, x + w / 2, e_ty, PAL_CLR(WHITE));
        return;
    }

    // ---- LEFT SIDE ----------------------------------------------------
    const ResUI *ui = (g && g->res) ? &g->res->ui : NULL;
    bfont_draw(ui ? ui->recruit_soldiers_title : "Recruit Soldiers",
               tx, ty, PAL_CLR(WHITE));

    // 5 troop rows starting at text->y + fs->h/4
    // (one row gap after the title), formatted "%c) %-11s%d".
    //
    // Show "n/a" when the player's total leadership can't fit at least
    // 6 of this troop:
    //   total_leadership < hp * 6  ->  "n/a"
    // Otherwise show the cost (even after the player has bought all
    // they can with current leadership). On fresh starts: Knight 100 /
    // Paladin 80 / Sorceress 60 leadership all flag Cavalry HP=20 and
    // Knights HP=35 as "n/a"; Pikemen HP=10 and smaller always show
    // their cost.
    int troop_ty = ty + row_h + 1;
    int total_lead = g->stats.leadership_current;
    for (int i = 0; i < 5; i++) {
        if (i >= s_pool_count || s_pool[i] < 0) continue;
        const TroopDef *t = troop_by_index(s_pool[i]);
        if (!t) continue;
        bool unreachable = (t->hit_points <= 0) ||
                           (total_lead < t->hit_points * 6);
        char line[64];
        if (unreachable) {
            snprintf(line, sizeof(line), "%c) %-11sn/a",
                     'A' + i, t->name);
        } else {
            snprintf(line, sizeof(line), "%c) %-11s%d",
                     'A' + i, t->name, t->recruit_cost);
        }
        bfont_draw(line, tx, troop_ty + i * row_h, PAL_CLR(WHITE));
    }

    // ---- RIGHT SIDE ---------------------------------------------------
    // right column at text->x + fs->w * 20.
    int rx = tx + 20 * BFONT_GLYPH_W;

    // GP=NK header at the same y as title.
    char gp[32];
    snprintf(gp, sizeof(gp), "GP=%dK", (int)(g->stats.gold / 1000));
    bfont_draw(gp, rx, ty, PAL_CLR(WHITE));

    // body starts at text->y + fs->h/4 with a leading
    // "\n\n" → the (A-C) row is 2 rows below the GP=NK header.
    int rby = ty + row_h + 1 + row_h;   // GP row + 1 gap + 1 blank line

    // Literal "(A-C) " hint string, rendered verbatim.
    bfont_draw("(A-C) ", rx, rby, PAL_CLR(WHITE));
    int after_hint_x = rx + 6 * BFONT_GLYPH_W;   // after "(A-C) "

    if (s_whom == 0) {
        // twirl[] = "\x1D\x05\x1F\x1C" — bitmap-font codepoints for
        // | / - \. bfont_init copies the printable glyphs into those
        // slots so the spinner renders correctly.
        const char twirl[] = "\x1D\x05\x1F\x1C";
        char tw[2] = { twirl[s_twirl_pos & 3], '\0' };
        bfont_draw(tw, after_hint_x, rby, PAL_CLR(WHITE));
        // 3 blank padded lines — nothing more to render.
    } else {
        // letter, Max=N, How Many, blank.
        char letter[2] = { (char)('A' + s_whom - 1), '\0' };
        bfont_draw(letter, after_hint_x, rby, PAL_CLR(WHITE));

        char maxbuf[24];
        snprintf(maxbuf, sizeof(maxbuf), "Max=%d", s_max);
        bfont_draw(maxbuf, rx, rby + row_h, PAL_CLR(WHITE));

        bfont_draw(ui ? ui->recruit_soldiers_how_many : "How Many",
                   rx, rby + 2 * row_h, PAL_CLR(WHITE));

        // text_input cursor inline at right column,
        // 4 rows below the start of the body (right at the "blank
        // line reserved).
        char buf_with_cursor[INPUT_MAX_LEN + 4];
        snprintf(buf_with_cursor, sizeof(buf_with_cursor), "%s_",
                 s_input_buf);
        bfont_draw(buf_with_cursor, rx, rby + 3 * row_h, PAL_CLR(WHITE));
    }
}

static int recruit_commit(Game *g, int pool_slot, int count, bool set_err) {
    if (!g || pool_slot < 0 || pool_slot >= s_pool_count) return -1;
    const TroopDef *t = troop_by_index(s_pool[pool_slot]);
    if (!t) return -1;
    int max = recompute_max(g, pool_slot);   // silent clamp (gold + leadership)
    if (count > max) count = max;
    if (count <= 0) return -1;
    int rc = GameBuyTroop(g, t->id, count);  // the REAL transaction
    if (set_err) {
        if (rc == 1) {
            const char *m = (g->res && g->res->banners.town_no_gold[0])
                ? g->res->banners.town_no_gold : "You don't have enough gold!";
            set_error(m);
        } else if (rc == 2) {
            const char *m = (g->res && g->res->banners.no_troop_slots[0])
                ? g->res->banners.no_troop_slots : "No troop slots left!";
            set_error(m);
        }
    }
    return rc;
}
