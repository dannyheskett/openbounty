// Autoplay core: dispatcher, shared helpers, and the combat input
// driver. Per-phase logic lives in src/autoplay/flow.c.

#include "autoplay/core.h"
#include "autoplay/internal.h"

#include "input_host.h"
#include "frame_host.h"
#include "shell_run.h"
#include "startup.h"
#include "tables.h"
#include "tile.h"
#include "combat.h"
#include "raylib.h"
#include "ui.h"
#include "views.h"
#include "prompt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =========================================================================
// Pre-tick snapshot (read by predicate functions)
// =========================================================================

int ap_pre_gold    = 0;
int ap_pre_army_hp = 0;
int ap_pre_pos_x   = 0;
int ap_pre_pos_y   = 0;

// =========================================================================
// Shared helpers
// =========================================================================

void ap_queue_standard_startup(void) {
    input_host_queue_key(KEY_A);     // class select: A = knight
    input_host_queue_key(KEY_ENTER); // name entry: empty -> "Hero"
    input_host_queue_key(KEY_ENTER); // difficulty: Normal (default)
}

int ap_army_total_hp(const Game *g) {
    int hp = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (!g->army[i].id[0]) continue;
        const TroopDef *t = troop_by_id(g->army[i].id);
        if (t) hp += t->hit_points * g->army[i].count;
    }
    return hp;
}

// =========================================================================
// Combat decision helpers (used by the per-inner-tick combat driver)
// =========================================================================

int ap_closest_enemy(const Combat *c, int ux, int uy, int *out_dist) {
    int best = -1, best_d = 1 << 30;
    for (int i = 0; i < COMBAT_SLOTS; i++) {
        const CombatUnit *e = &c->units[COMBAT_SIDE_AI][i];
        if (e->troop_idx < 0 || e->count <= 0) continue;
        int adx = e->x - ux; if (adx < 0) adx = -adx;
        int ady = e->y - uy; if (ady < 0) ady = -ady;
        int d = (adx > ady) ? adx : ady;
        if (d < best_d) { best_d = d; best = i; }
    }
    if (out_dist) *out_dist = best_d;
    return best;
}

int ap_highest_threat_enemy(const Combat *c, int ux, int uy,
                            int *out_dist) {
    int best = -1, best_hp = -1, best_d = 1 << 30;
    for (int i = 0; i < COMBAT_SLOTS; i++) {
        const CombatUnit *e = &c->units[COMBAT_SIDE_AI][i];
        if (e->troop_idx < 0 || e->count <= 0) continue;
        const TroopDef *t = troop_by_index(e->troop_idx);
        if (!t) continue;
        int stack_hp = e->count * t->hit_points;
        int adx = e->x - ux; if (adx < 0) adx = -adx;
        int ady = e->y - uy; if (ady < 0) ady = -ady;
        int d = (adx > ady) ? adx : ady;
        if (stack_hp > best_hp || (stack_hp == best_hp && d < best_d)) {
            best_hp = stack_hp;
            best_d  = d;
            best    = i;
        }
    }
    if (out_dist) *out_dist = best_d;
    return best;
}

// =========================================================================
// State dump
// =========================================================================

void ap_dump_state(const char *why, const Game *g, const AutoplayState *st) {
    AP_LOG("STATE DUMP: %s", why ? why : "(no reason)");
    AP_LOG("  tick=%d phase=%d", st->tick, (int)st->phase);
    AP_LOG("  pos=(%d,%d) zone=%s travel_mode=%d",
           g->position.x, g->position.y, g->position.zone,
           (int)g->travel_mode);
    AP_LOG("  pre: pos=(%d,%d) gold=%d army_hp=%d",
           ap_pre_pos_x, ap_pre_pos_y, ap_pre_gold, ap_pre_army_hp);
    AP_LOG("  now: gold=%d army_hp=%d",
           g->stats.gold, ap_army_total_hp(g));
    AP_LOG("  views=%d dialog=%d prompt=%d",
           (int)views_active(),
           (int)dialog_is_active(),
           (int)prompt_is_active());
    if (prompt_is_active()) {
        const char *hdr = prompt_header_text();
        const char *kind = prompt_kind_str();
        AP_LOG("  prompt header='%s' kind=%s",
               hdr ? hdr : "(null)", kind ? kind : "(null)");
    }
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (!g->army[i].id[0] || g->army[i].count <= 0) continue;
        AP_LOG("  army[%d]: %d x %s", i, g->army[i].count, g->army[i].id);
    }
}

// =========================================================================
// Combat driver — runs from inside RunCombat's inner loop
// =========================================================================
//
// Wired via frame_host_set_before_frame. Fires each inner tick. The
// combat picker is a multi-key modal sequence (KEY_S, direction keys,
// KEY_A confirm) so for that case we use the tiny FIFO; everything
// else uses ap_set_key directly.

static AutoplayState *g_active_state = NULL;

static void queue_picker_path(int cx, int cy, int tx, int ty) {
    int max_iters = COMBAT_W + COMBAT_H + 4;
    int n = 0;
    while ((cx != tx || cy != ty) && n < max_iters) {
        int dx = 0, dy = 0;
        if (tx > cx) dx = 1; else if (tx < cx) dx = -1;
        if (ty > cy) dy = 1; else if (ty < cy) dy = -1;
        int k = 0;
        if (dx == 0  && dy == -1) k = KEY_UP;
        else if (dx == 0  && dy ==  1) k = KEY_DOWN;
        else if (dx == -1 && dy ==  0) k = KEY_LEFT;
        else if (dx == 1  && dy ==  0) k = KEY_RIGHT;
        else if (dx == -1 && dy == -1) k = KEY_HOME;
        else if (dx == 1  && dy == -1) k = KEY_PAGE_UP;
        else if (dx == -1 && dy ==  1) k = KEY_END;
        else if (dx == 1  && dy ==  1) k = KEY_PAGE_DOWN;
        if (k == 0) break;
        input_host_queue_key(k);
        cx += dx; cy += dy;
        n++;
    }
    input_host_queue_key(KEY_A);  // confirm
}

static int combat_unit_id_last_acted = -1;
static int combat_tick_last_action   = 0;

static void autoplay_before_frame(void *user) {
    (void)user;
    AutoplayState *st = g_active_state;
    if (!st) return;
    Combat *c = combat_current_rendered;
    if (!c) return;
    if (c->result != 0) {
        // Combat ended (1=win, 2=loss). RunCombat shows a victory or
        // defeat dialog before returning. SPACE dismisses it.
        if (dialog_is_active()) ap_set_key(KEY_SPACE);
        return;
    }
    if (c->picker_active) return;
    if (c->side != COMBAT_SIDE_PLAYER) return;
    if (c->unit_id < 0) return;
    if (dialog_is_active()) {
        ap_set_key(KEY_SPACE);
        return;
    }
    if (input_host_queue_depth() > 0) return;

    CombatUnit *u = &c->units[c->side][c->unit_id];
    if (u->acted || u->troop_idx < 0 || u->count <= 0) return;
    // The engine's u->acted flag already guarantees one decision per
    // unit per turn — we don't need an additional rate-limit here.
    (void)combat_unit_id_last_acted;
    (void)combat_tick_last_action;

    int enemy_d = 0;
    int enemy_slot = ap_closest_enemy(c, u->x, u->y, &enemy_d);
    if (enemy_slot < 0) return;
    bool can_shoot = (u->shots > 0 && enemy_d > 1 &&
                      !combat_unit_surrounded(c, c->side, c->unit_id));
    if (can_shoot) {
        int tgt_slot = ap_highest_threat_enemy(c, u->x, u->y, NULL);
        if (tgt_slot < 0) tgt_slot = enemy_slot;
        const CombatUnit *target = &c->units[COMBAT_SIDE_AI][tgt_slot];
        input_host_queue_key(KEY_S);
        queue_picker_path(u->x, u->y, target->x, target->y);
    } else {
        // Melee: pick the 8-direction step that closes Chebyshev
        // distance to the closest enemy the most.
        static const int dxs[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
        static const int dys[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
        const CombatUnit *enemy = &c->units[COMBAT_SIDE_AI][enemy_slot];
        int best_k = 0;
        int best_d = enemy_d;
        for (int i = 0; i < 8; i++) {
            int nx = u->x + dxs[i];
            int ny = u->y + dys[i];
            if (!combat_in_bounds(nx, ny)) continue;
            if (c->omap[ny][nx]) continue;
            unsigned char other = c->umap[ny][nx];
            if (other) {
                int o_side = (other - 1) / COMBAT_SLOTS;
                int o_slot = (other - 1) % COMBAT_SLOTS;
                if (units_are_friendly(c, c->side, c->unit_id,
                                       o_side, o_slot)) continue;
            }
            int adx = enemy->x - nx; if (adx < 0) adx = -adx;
            int ady = enemy->y - ny; if (ady < 0) ady = -ady;
            int nd = (adx > ady) ? adx : ady;
            if (nd < best_d) {
                best_d = nd;
                int k = 0;
                if (dxs[i] == 0  && dys[i] == -1) k = KEY_UP;
                else if (dxs[i] == 0  && dys[i] ==  1) k = KEY_DOWN;
                else if (dxs[i] == -1 && dys[i] ==  0) k = KEY_LEFT;
                else if (dxs[i] == 1  && dys[i] ==  0) k = KEY_RIGHT;
                else if (dxs[i] == -1 && dys[i] == -1) k = KEY_HOME;
                else if (dxs[i] == 1  && dys[i] == -1) k = KEY_PAGE_UP;
                else if (dxs[i] == -1 && dys[i] ==  1) k = KEY_END;
                else if (dxs[i] == 1  && dys[i] ==  1) k = KEY_PAGE_DOWN;
                best_k = k;
            }
        }
        if (best_k != 0) ap_set_key(best_k);
        else             ap_set_key(KEY_SPACE);  // wait
    }
    combat_unit_id_last_acted = c->unit_id;
    combat_tick_last_action = st->tick;
}

// =========================================================================
// Dispatcher
// =========================================================================
//
// 1 command, 1 tick, 1 assertion. Hard fail on assertion failure.

static void autoplay_before_startup(ShellRunHooks *self) {
    (void)self;
    ap_queue_standard_startup();
}

// Map-scan dump. With env var AP_DUMP_MAP=1, prints the whole zone
// as ASCII (water=~, blocked=#, walkable=., chest=C, foe=F, town=T,
// castle=K, hero=@). Used once by hand to author the step lists for
// each chest leg. Otherwise no-op.
#include "tile.h"
#include <stdlib.h>
static void dump_zone_interactives(const Map *m, const Game *g) {
    if (!getenv("AP_DUMP_MAP")) return;
    AP_LOG("=== zone map (zone=%s, %dx%d) ===",
           g->position.zone, m->width, m->height);
    AP_LOG("  hero at (%d,%d)", g->position.x, g->position.y);
    for (int y = 0; y < m->height; y++) {
        char row[160];
        int n = 0;
        n += snprintf(row + n, sizeof row - n, "  %2d: ", y);
        for (int x = 0; x < m->width; x++) {
            const Tile *t = &m->tiles[y][x];
            char c = '?';
            if (x == g->position.x && y == g->position.y) c = '@';
            else if (t->interactive == INTERACT_TREASURE_CHEST) c = 'C';
            else if (t->interactive == INTERACT_FOE) c = 'F';
            else if (t->interactive == INTERACT_TOWN) c = 'T';
            else if (t->interactive == INTERACT_CASTLE_GATE) c = 'K';
            else if (t->interactive == INTERACT_ARTIFACT) c = 'A';
            else if (t->interactive == INTERACT_DWELLING_PLAINS ||
                     t->interactive == INTERACT_DWELLING_FOREST ||
                     t->interactive == INTERACT_DWELLING_HILLS ||
                     t->interactive == INTERACT_DWELLING_DUNGEON) c = 'D';
            else if (t->interactive != INTERACT_NONE) c = '*';
            else if (t->terrain == TERRAIN_WATER) c = '~';
            else if (t->blocks_foot) c = '#';
            else if (TerrainWalkable(t->terrain)) c = '.';
            else c = '?';
            row[n++] = c;
        }
        row[n] = '\0';
        AP_LOG("%s", row);
    }
    AP_LOG("=== scan done ===");
}


static ShellRunVerdict autoplay_per_tick(ShellRunHooks *self,
                                         Game *g, Map *m, Fog *f,
                                         Resources *res, int frame_no) {
    (void)f; (void)res; (void)frame_no;
    AutoplayState *st = (AutoplayState *)self->user;
    st->tick++;

    // One-shot map dump on first tick.
    if (st->tick == 1) dump_zone_interactives(m, g);

    // 1. Assert the previous command's post-state expectation.
    if (st->pending_active) {
        bool ok = (st->pending.assert_post == NULL) ||
                  st->pending.assert_post(g);
        if (!ok) {
            AP_LOG("cmd '%s' assertion failed at tick %d",
                   st->pending.name ? st->pending.name : "(unnamed)",
                   st->tick);
            ap_dump_state("assertion failed", g, st);
            return SHELL_RUN_EXIT_FAIL;
        }
        st->pending_active = false;
    }

    // 2. Terminal phase: report and exit PASS.
    if (st->phase == AP_ALL_DONE) {
        printf("autoplay: complete (tick=%d)\n", st->tick);
        return SHELL_RUN_EXIT_PASS;
    }

    // 3. Snapshot pre-tick state for delta-based assertions.
    st->pre_gold    = g->stats.gold;
    st->pre_army_hp = ap_army_total_hp(g);
    st->pre_pos_x   = g->position.x;
    st->pre_pos_y   = g->position.y;
    ap_pre_gold    = st->pre_gold;
    ap_pre_army_hp = st->pre_army_hp;
    ap_pre_pos_x   = st->pre_pos_x;
    ap_pre_pos_y   = st->pre_pos_y;

    // 4. Ask the active phase for the next command.
    bool phase_done = false;
    AutoplayPhase next_phase = st->phase;
    AutoplayPhase phase_in = st->phase;
    ApCmd cmd = ap_flow_phase(g, m, st, &phase_done, &next_phase);
    (void)phase_in;

    // 5. Set the live key for this tick (or clear).
    if (cmd.key) ap_set_key(cmd.key);
    else         ap_clear_key();

    // 6. Stash for next-tick assertion.
    st->pending = cmd;
    st->pending_active = true;

    // 7. Transition if signaled.
    if (phase_done) st->phase = next_phase;

    AP_LOG("tick=%d phase_in=%d phase_out=%d cmd='%s' key=%d done=%d "
           "pos=(%d,%d) gold=%d hp=%d",
           st->tick, (int)phase_in, (int)st->phase,
           cmd.name ? cmd.name : "(unnamed)", cmd.key, (int)phase_done,
           g->position.x, g->position.y, g->stats.gold,
           ap_army_total_hp(g));

    return SHELL_RUN_CONTINUE;
}

// =========================================================================
// Public entry
// =========================================================================

int autoplay_run(int argc, char **argv) {
    input_host_use_queue();
    frame_host_use_test();
    startup_skip_intros = true;

    AutoplayState state = { 0 };
    state.phase = AP_FLOW_FIRST;
    for (size_t i = 0; i < sizeof(state.module_scratch)/sizeof(state.module_scratch[0]); i++) {
        state.module_scratch[i] = -1;
    }
    g_active_state = &state;
    combat_unit_id_last_acted = -1;
    combat_tick_last_action   = 0;
    frame_host_set_before_frame(autoplay_before_frame, NULL);

    ShellRunHooks hooks = {
        .before_startup = autoplay_before_startup,
        .per_frame      = autoplay_per_tick,
        .user           = &state,
    };
    int rc = shell_run_game(argc, argv, &hooks);

    frame_host_set_before_frame(NULL, NULL);
    g_active_state = NULL;
    return rc;
}
