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

// Per-turn snapshot — called once per acting player unit before we
// pick a move. Dumps the live grid (units + obstacles) and both
// sides' rosters so we can see what the picker is reacting to.
static void log_combat_turn(const Combat *c) {
    AP_LOG("[fight] turn=%d acting=player slot=%d at (%d,%d) "
           "troop=%d count=%d shots=%d moves=%d",
           c->turn, c->unit_id,
           c->units[c->side][c->unit_id].x,
           c->units[c->side][c->unit_id].y,
           c->units[c->side][c->unit_id].troop_idx,
           c->units[c->side][c->unit_id].count,
           c->units[c->side][c->unit_id].shots,
           c->units[c->side][c->unit_id].moves);
    // Roster (both sides).
    for (int s = 0; s < COMBAT_SIDES; s++) {
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            const CombatUnit *u = &c->units[s][i];
            if (u->troop_idx < 0 || u->count <= 0) continue;
            const TroopDef *t = troop_by_index(u->troop_idx);
            AP_LOG("[fight]   %s[%d]: %d x %s at (%d,%d) hp_each=%d",
                   s == COMBAT_SIDE_PLAYER ? "ply" : "ai ",
                   i, u->count, t ? t->id : "?",
                   u->x, u->y, t ? t->hit_points : -1);
        }
    }
    // Grid: each cell is one of U=player unit, A=ai unit, #=obstacle,
    // .=open. Y is row 0..H-1 top to bottom.
    for (int y = 0; y < COMBAT_H; y++) {
        char row[64];
        int n = 0;
        n += snprintf(row + n, sizeof row - n, "  y%d: ", y);
        for (int x = 0; x < COMBAT_W; x++) {
            char ch = '.';
            if (c->omap[y][x]) ch = '#';
            unsigned char u = c->umap[y][x];
            if (u) {
                int side = (u - 1) / COMBAT_SLOTS;
                ch = (side == COMBAT_SIDE_PLAYER) ? 'U' : 'A';
            }
            row[n++] = ch;
            row[n++] = ' ';
        }
        row[n] = '\0';
        AP_LOG("[fight]%s", row);
    }
}

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
    // Picker early-exit. The spell-target picker is the only one
    // we drive from this hook; the shoot/fly pickers are resolved
    // by keys already queued via queue_picker_path from the SHOOT
    // / MELEE branches below, so we must NOT re-evaluate combat
    // strategy while one of those is mid-resolution.
    if (c->picker_active &&
        c->pick_reason != COMBAT_PICK_REASON_SPELL_TARGET) {
        return;
    }
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

    // One log line per acting player unit so we can see what the
    // picker is reacting to. Only emit when this is a different
    // (unit_id, turn) tuple than the last we logged, to avoid
    // spamming repeated frames.
    {
        static int last_logged_turn = -1;
        static int last_logged_slot = -1;
        if (c->turn != last_logged_turn || c->unit_id != last_logged_slot) {
            log_combat_turn(c);
            last_logged_turn = c->turn;
            last_logged_slot = c->unit_id;
        }
    }

    int enemy_d = 0;
    int enemy_slot = ap_closest_enemy(c, u->x, u->y, &enemy_d);
    if (enemy_slot < 0) return;

    // Cast fireball through the engine state machine. While the
    // cast is in flight we drive one transition per tick by reading
    // c->cast_phase + c->picker_active — no key queue, no race.
    //
    //   Idle  : if we own a fireball and the heaviest enemy stack
    //           is hp_each >= 25, press U to enter PICK_SPELL.
    //   PICK_SPELL : press C (Fireball is catalog letter C).
    //   PICK_TARGET (reason=SPELL_TARGET) : walk the cursor one
    //           step toward the cached target, or press A to
    //           confirm when on it.
    //   APPLY  : engine resolves this frame; nothing for us to do.
    //
    // module_scratch[6] stores the target cell as (y<<8)|x while a
    // cast is in flight; cleared once spells_this_round flips.
    Game *gw = c->heroes[c->side];
    if (c->cast_phase == COMBAT_CAST_PICK_SPELL) {
        ap_set_key(KEY_C);
        return;
    }
    if (c->picker_active &&
        c->pick_reason == COMBAT_PICK_REASON_SPELL_TARGET) {
        int tx = st->module_scratch[6] & 0xff;
        int ty = (st->module_scratch[6] >> 8) & 0xff;
        if (c->cursor_x == tx && c->cursor_y == ty) {
            ap_set_key(KEY_A);
        } else {
            int dx = (tx > c->cursor_x) ? 1 : (tx < c->cursor_x ? -1 : 0);
            int dy = (ty > c->cursor_y) ? 1 : (ty < c->cursor_y ? -1 : 0);
            int k = 0;
            if      (dx ==  0 && dy == -1) k = KEY_UP;
            else if (dx ==  0 && dy ==  1) k = KEY_DOWN;
            else if (dx == -1 && dy ==  0) k = KEY_LEFT;
            else if (dx ==  1 && dy ==  0) k = KEY_RIGHT;
            else if (dx == -1 && dy == -1) k = KEY_HOME;
            else if (dx ==  1 && dy == -1) k = KEY_PAGE_UP;
            else if (dx == -1 && dy ==  1) k = KEY_END;
            else if (dx ==  1 && dy ==  1) k = KEY_PAGE_DOWN;
            if (k) ap_set_key(k);
        }
        return;
    }
    if (c->cast_phase == COMBAT_CAST_NONE && !c->picker_active &&
        gw && gw->stats.knows_magic && gw->spells.counts[2] > 0 &&
        c->spells_this_round == 0) {
        int tgt_slot = ap_highest_threat_enemy(c, u->x, u->y, NULL);
        if (tgt_slot >= 0) {
            const CombatUnit *target = &c->units[COMBAT_SIDE_AI][tgt_slot];
            const TroopDef *tt = troop_by_index(target->troop_idx);
            int e_hp_each = tt ? tt->hit_points : 0;
            if (e_hp_each >= 25) {
                AP_LOG("[fight]   action: CAST FIREBALL at slot %d "
                       "(%d,%d) e_hp_each=%d",
                       tgt_slot, target->x, target->y, e_hp_each);
                st->module_scratch[6] =
                    ((target->y & 0xff) << 8) | (target->x & 0xff);
                ap_set_key(KEY_U);
                return;
            }
        }
    }

    bool can_shoot = (u->shots > 0 && enemy_d > 1 &&
                      !combat_unit_surrounded(c, c->side, c->unit_id));
    if (can_shoot) {
        int tgt_slot = ap_highest_threat_enemy(c, u->x, u->y, NULL);
        if (tgt_slot < 0) tgt_slot = enemy_slot;
        const CombatUnit *target = &c->units[COMBAT_SIDE_AI][tgt_slot];
        AP_LOG("[fight]   action: SHOOT at slot %d (%d,%d)",
               tgt_slot, target->x, target->y);
        input_host_queue_key(KEY_S);
        queue_picker_path(u->x, u->y, target->x, target->y);
    } else {
        // Don't suicide-rush heavy enemies. If the closest enemy has
        // high per-unit HP (heavy hitter), melee units stay back and
        // let archers do the work. Threshold = 25 HP/unit (covers
        // trolls=50, giants=60, ogres, etc; excludes peasants=1,
        // militia=2, sprites=1, zombies=5, archers=10, pikemen=10).
        {
            const CombatUnit *e = &c->units[COMBAT_SIDE_AI][enemy_slot];
            const TroopDef *t = troop_by_index(e->troop_idx);
            int e_hp_each = t ? t->hit_points : 0;
            const TroopDef *mt = troop_by_index(u->troop_idx);
            int my_hp_each = mt ? mt->hit_points : 0;
            // Heavy = enemy hp/unit at least 3x my hp/unit, AND
            // enemy ≥ 25 hp/unit. That keeps pikemen (10) from
            // suiciding into trolls (50), but doesn't make pikemen
            // afraid of zombies (5).
            bool heavy = (e_hp_each >= 25) &&
                         (my_hp_each > 0 && e_hp_each >= my_hp_each * 3);
            if (heavy) {
                AP_LOG("[fight]   action: HOLD (heavy enemy slot %d "
                       "hp_each=%d > my hp_each=%d)",
                       enemy_slot, e_hp_each, my_hp_each);
                ap_set_key(KEY_SPACE);
                return;
            }
        }
        // Melee: If we're already adjacent to an enemy, attack (step
        // INTO that tile — engine treats it as melee). Otherwise BFS
        // over the combat grid to find the shortest 8-direction path
        // that ends on a tile adjacent (Chebyshev=1) to ANY enemy.
        // Then take the first step on that path. The BFS handles
        // rocks/obstacles that block the direct line-of-sight.
        static const int dxs[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
        static const int dys[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
        static const int keys[8] = {
            KEY_HOME,   KEY_UP,   KEY_PAGE_UP,
            KEY_LEFT,             KEY_RIGHT,
            KEY_END,    KEY_DOWN, KEY_PAGE_DOWN
        };
        // First: if adjacent to an enemy, pick the direction toward
        // the highest-threat adjacent enemy and attack.
        {
            int best_e = -1;
            int best_e_hp = -1;
            for (int i = 0; i < COMBAT_SLOTS; i++) {
                const CombatUnit *e = &c->units[COMBAT_SIDE_AI][i];
                if (e->troop_idx < 0 || e->count <= 0) continue;
                int adx = e->x - u->x; if (adx < 0) adx = -adx;
                int ady = e->y - u->y; if (ady < 0) ady = -ady;
                if (adx <= 1 && ady <= 1 && (adx + ady) > 0) {
                    const TroopDef *t = troop_by_index(e->troop_idx);
                    int hp = t ? e->count * t->hit_points : 0;
                    if (hp > best_e_hp) {
                        best_e_hp = hp;
                        best_e = i;
                    }
                }
            }
            if (best_e >= 0) {
                const CombatUnit *e = &c->units[COMBAT_SIDE_AI][best_e];
                int dx = e->x - u->x;
                int dy = e->y - u->y;
                // Map (dx,dy) to one of the 8 keys.
                int k = 0;
                if (dx == 0  && dy == -1) k = KEY_UP;
                else if (dx == 0  && dy ==  1) k = KEY_DOWN;
                else if (dx == -1 && dy ==  0) k = KEY_LEFT;
                else if (dx == 1  && dy ==  0) k = KEY_RIGHT;
                else if (dx == -1 && dy == -1) k = KEY_HOME;
                else if (dx == 1  && dy == -1) k = KEY_PAGE_UP;
                else if (dx == -1 && dy ==  1) k = KEY_END;
                else if (dx == 1  && dy ==  1) k = KEY_PAGE_DOWN;
                if (k) {
                    AP_LOG("[fight]   action: ATTACK adjacent slot %d "
                           "(%d,%d) dx=%d dy=%d",
                           best_e, e->x, e->y, dx, dy);
                    ap_set_key(k);
                    return;
                }
            }
        }
        int W = COMBAT_W, H = COMBAT_H;
        // visited[y*W + x] = step-count from u, or -1 if unvisited.
        // first_dir[y*W + x] = the 0..7 dxs/dys index of the first
        // move on the shortest path to (x,y) from the unit's tile.
        int dist[6 * 5];
        int first_dir[6 * 5];
        for (int i = 0; i < W * H; i++) { dist[i] = -1; first_dir[i] = -1; }
        int q[6 * 5];
        int qh = 0, qt = 0;
        int s_idx = u->y * W + u->x;
        dist[s_idx] = 0;
        q[qt++] = s_idx;
        int best_path_dir = -1;
        int best_path_len = 1 << 30;
        while (qh < qt) {
            int cur = q[qh++];
            int cy = cur / W;
            int cx = cur % W;
            int d_here = dist[cur];
            if (d_here >= best_path_len) continue;
            // Goal test: this tile is adjacent to an enemy.
            for (int i = 0; i < COMBAT_SLOTS; i++) {
                const CombatUnit *e = &c->units[COMBAT_SIDE_AI][i];
                if (e->troop_idx < 0 || e->count <= 0) continue;
                int adx = e->x - cx; if (adx < 0) adx = -adx;
                int ady = e->y - cy; if (ady < 0) ady = -ady;
                int che = (adx > ady) ? adx : ady;
                if (che <= 1 && d_here > 0) {
                    if (d_here < best_path_len) {
                        best_path_len = d_here;
                        best_path_dir = first_dir[cur];
                    }
                    break;
                }
            }
            // Expand 8 neighbors.
            for (int i = 0; i < 8; i++) {
                int nx = cx + dxs[i];
                int ny = cy + dys[i];
                if (!combat_in_bounds(nx, ny)) continue;
                if (c->omap[ny][nx]) continue;
                unsigned char other = c->umap[ny][nx];
                if (other) {
                    int o_side = (other - 1) / COMBAT_SLOTS;
                    int o_slot = (other - 1) % COMBAT_SLOTS;
                    // Friendly units block; enemy units block walking
                    // through but a tile adjacent to them is the goal.
                    if (units_are_friendly(c, c->side, c->unit_id,
                                           o_side, o_slot)) continue;
                    if (o_side == COMBAT_SIDE_AI) continue;  // can't stand on enemy
                }
                int n_idx = ny * W + nx;
                if (dist[n_idx] >= 0) continue;
                dist[n_idx] = d_here + 1;
                // first_dir on a node = either the dir from the source
                // (if cur is the source) or inherited from cur.
                if (cur == s_idx) first_dir[n_idx] = i;
                else              first_dir[n_idx] = first_dir[cur];
                q[qt++] = n_idx;
            }
        }
        if (best_path_dir >= 0) {
            AP_LOG("[fight]   action: MELEE step dir=%d len=%d",
                   best_path_dir, best_path_len);
            ap_set_key(keys[best_path_dir]);
        } else {
            AP_LOG("[fight]   action: WAIT (no path)");
            ap_set_key(KEY_SPACE);  // truly nowhere to go
        }
    }
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
