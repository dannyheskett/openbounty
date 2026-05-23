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

#if 0  // === offline flow-field generator — set AP_PRINT_FLOW=1 to run ===

#include <stdlib.h>

static int dir_to_key(int dx, int dy) {
    if (dx == 0  && dy == -1) return 265; // UP
    if (dx == 0  && dy ==  1) return 264; // DOWN
    if (dx == -1 && dy ==  0) return 263; // LEFT
    if (dx == 1  && dy ==  0) return 262; // RIGHT
    if (dx == -1 && dy == -1) return 268; // HOME
    if (dx == 1  && dy == -1) return 266; // PAGE_UP
    if (dx == -1 && dy ==  1) return 269; // END
    if (dx == 1  && dy ==  1) return 267; // PAGE_DOWN
    return 0;
}

// BFS shortest path from (sx,sy) to (gx,gy) over foot-walkable tiles.
// Goal can be an interactive tile; intermediate tiles must NOT be
// (we don't want to walk through dwellings or onto chests we haven't
// claimed yet — except possibly already-stepped-on ones, but for path
// gen we forbid all interactives in between).
static int bfs_keys(const Map *m, int sx, int sy, int gx, int gy,
                    int *out_keys, int cap) {
    if (sx == gx && sy == gy) return 0;
    int W = m->width, H = m->height;
    short *par = (short *)calloc((size_t)W * H, sizeof(short));
    if (!par) return 0;
    int *qx = (int *)malloc(sizeof(int) * (size_t)W * H);
    int *qy = (int *)malloc(sizeof(int) * (size_t)W * H);
    if (!qx || !qy) { free(par); free(qx); free(qy); return 0; }
    int qh = 0, qt = 0;
    qx[qt] = sx; qy[qt] = sy; qt++;
    par[sy * W + sx] = 1;
    static const int dxs[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
    static const int dys[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };
    bool found = false;
    while (!found && qh < qt) {
        int cx = qx[qh], cy = qy[qh]; qh++;
        for (int k = 0; k < 8; k++) {
            int nx = cx + dxs[k], ny = cy + dys[k];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
            if (par[ny * W + nx]) continue;
            const Tile *t = &m->tiles[ny][nx];
            bool is_goal = (nx == gx && ny == gy);
            bool walkable = !t->blocks_foot && TerrainWalkable(t->terrain);
            if (!is_goal) {
                if (!walkable) continue;
                if (t->interactive != INTERACT_NONE) continue;
            }
            par[ny * W + nx] = (short)((dys[k] + 1) * 3 + (dxs[k] + 1) + 1);
            qx[qt] = nx; qy[qt] = ny; qt++;
            if (is_goal) { found = true; break; }
        }
    }
    int n = 0;
    if (found) {
        // Walk back recording dx,dy.
        int cx = gx, cy = gy;
        int tmp_dx[1024], tmp_dy[1024], cnt = 0;
        while (!(cx == sx && cy == sy) && cnt < 1024) {
            short enc = par[cy * W + cx];
            int dir = (int)enc - 1;
            int dy = dir / 3 - 1;
            int dx = dir % 3 - 1;
            tmp_dx[cnt] = dx;
            tmp_dy[cnt] = dy;
            cx -= dx; cy -= dy;
            cnt++;
        }
        // Reverse and convert to keys.
        for (int i = cnt - 1; i >= 0 && n < cap; i--) {
            int key = dir_to_key(tmp_dx[i], tmp_dy[i]);
            if (key) out_keys[n++] = key;
        }
    }
    free(par); free(qx); free(qy);
    return n;
}

// For each target (chest or foe), do reverse-BFS from the target
// and emit a flow-field of next-step direction keys: for every
// landmass tile, which direction should the hero step toward to
// reach this target by shortest path? Output is a giant C array
// pasted into flow.c. Runtime: hero at (x,y) targeting chest T
// → press flow_keys[T][y][x]. No runtime BFS.
//
// Foes are treated as BLOCKERS in the flow field (so paths route
// around them — we never step into a foe by accident en route to a
// chest). The final foe leg uses a separate flow field that ALLOWS
// the foe tile as the goal.
static void dump_zone_interactives(const Map *m, const Game *g) {
    if (!getenv("AP_PRINT_FLOW")) return;

    AP_LOG("=== AP_PRINT_FLOW: generating flow-field table ===");
    AP_LOG("hero start: (%d,%d)", g->position.x, g->position.y);

    // 1. Flood-fill to identify the start landmass.
    bool land[64][64] = { { false } };
    {
        int qx[64*64], qy[64*64], qh=0, qt=0;
        qx[qt]=g->position.x; qy[qt]=g->position.y; qt++;
        land[g->position.y][g->position.x] = true;
        static const int dxs[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
        static const int dys[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
        while (qh < qt) {
            int cx=qx[qh], cy=qy[qh]; qh++;
            for (int k=0; k<8; k++) {
                int nx=cx+dxs[k], ny=cy+dys[k];
                if (nx<0||ny<0||nx>=m->width||ny>=m->height) continue;
                if (land[ny][nx]) continue;
                const Tile *t = &m->tiles[ny][nx];
                bool walkable = !t->blocks_foot && TerrainWalkable(t->terrain);
                bool terminal = t->interactive == INTERACT_TREASURE_CHEST ||
                                t->interactive == INTERACT_FOE ||
                                t->interactive == INTERACT_ARTIFACT ||
                                t->interactive == INTERACT_TOWN;
                if (!walkable && !terminal) continue;
                if (t->interactive != INTERACT_NONE && !terminal) continue;
                land[ny][nx] = true;
                if (!terminal) { qx[qt]=nx; qy[qt]=ny; qt++; }
            }
        }
    }

    // 2. Collect reachable chests, foes, and towns.
    typedef struct { int x, y; const char *id; } Target;
    Target chests[32]; int n_chests = 0;
    Target foes[32];   int n_foes = 0;
    Target towns[8];   int n_towns = 0;
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            if (!land[y][x]) continue;
            const Tile *t = &m->tiles[y][x];
            if (t->interactive == INTERACT_TREASURE_CHEST && n_chests < 32) {
                chests[n_chests++] = (Target){x, y, t->id};
            } else if (t->interactive == INTERACT_FOE && n_foes < 32) {
                foes[n_foes++] = (Target){x, y, t->id};
            } else if (t->interactive == INTERACT_TOWN && n_towns < 8) {
                towns[n_towns++] = (Target){x, y, t->id};
            }
        }
    }

    AP_LOG("// landmass chests: %d  foes: %d", n_chests, n_foes);
    fprintf(stderr, "// Targets in declaration order:\n");
    for (int i = 0; i < n_chests; i++) {
        fprintf(stderr, "//   chest[%d] = (%d,%d) %s\n",
                i, chests[i].x, chests[i].y, chests[i].id);
    }
    int foe_idx = -1;
    if (n_foes > 0) {
        // Pick the nearest foe to start.
        int best = 0, best_d = 1 << 30;
        for (int j = 0; j < n_foes; j++) {
            int adx = foes[j].x - g->position.x; if (adx<0) adx=-adx;
            int ady = foes[j].y - g->position.y; if (ady<0) ady=-ady;
            int d = (adx > ady) ? adx : ady;
            if (d < best_d) { best_d = d; best = j; }
        }
        foe_idx = best;
        fprintf(stderr, "//   foe       = (%d,%d) %s\n",
                foes[foe_idx].x, foes[foe_idx].y, foes[foe_idx].id);
    }
    int town_idx = -1;
    if (n_towns > 0) {
        // Pick the nearest town to start (siege-weapon purchase point).
        int best = 0, best_d = 1 << 30;
        for (int j = 0; j < n_towns; j++) {
            int adx = towns[j].x - g->position.x; if (adx<0) adx=-adx;
            int ady = towns[j].y - g->position.y; if (ady<0) ady=-ady;
            int d = (adx > ady) ? adx : ady;
            if (d < best_d) { best_d = d; best = j; }
        }
        town_idx = best;
        fprintf(stderr, "//   town      = (%d,%d) %s\n",
                towns[town_idx].x, towns[town_idx].y, towns[town_idx].id);
    }

    // 3. For each target (chest, then foe), emit a flow-field:
    //    flow[y][x] = direction key from (x,y) toward target, or 0.
    //
    // Build by reverse-BFS from the target tile. Allowed intermediate
    // tiles: walkable, no interactive (foes are blockers!), in the
    // start landmass. The target tile itself is allowed as goal.
    typedef struct { int x, y; } P;
    static const int dxs[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
    static const int dys[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };

    int n_total_targets = n_chests + (n_foes > 0 ? 1 : 0) + (n_towns > 0 ? 1 : 0);
    fprintf(stderr,
        "// === BEGIN FLOW-FIELD TABLE (target idx, then 64x64 keys) ===\n");
    fprintf(stderr, "static const int N_TARGETS = %d;\n", n_total_targets);
    fprintf(stderr, "static const int TARGET_X[%d] = {", n_total_targets);
    for (int i = 0; i < n_chests; i++) fprintf(stderr, "%d,", chests[i].x);
    if (foe_idx >= 0)  fprintf(stderr, "%d,", foes[foe_idx].x);
    if (town_idx >= 0) fprintf(stderr, "%d,", towns[town_idx].x);
    fprintf(stderr, "};\n");
    fprintf(stderr, "static const int TARGET_Y[%d] = {", n_total_targets);
    for (int i = 0; i < n_chests; i++) fprintf(stderr, "%d,", chests[i].y);
    if (foe_idx >= 0)  fprintf(stderr, "%d,", foes[foe_idx].y);
    if (town_idx >= 0) fprintf(stderr, "%d,", towns[town_idx].y);
    fprintf(stderr, "};\n");
    fprintf(stderr, "static const char TARGET_KIND[%d] = {", n_total_targets);
    for (int i = 0; i < n_chests; i++) fprintf(stderr, "'C',");
    if (foe_idx >= 0)  fprintf(stderr, "'F',");
    if (town_idx >= 0) fprintf(stderr, "'T',");
    fprintf(stderr, "};\n");

    fprintf(stderr, "static const int FLOW[%d][64][64] = {\n", n_total_targets);
    for (int ti = 0; ti < n_total_targets; ti++) {
        int tx, ty;
        if (ti < n_chests) {
            tx = chests[ti].x; ty = chests[ti].y;
        } else if (ti == n_chests && foe_idx >= 0) {
            tx = foes[foe_idx].x; ty = foes[foe_idx].y;
        } else {
            tx = towns[town_idx].x; ty = towns[town_idx].y;
        }
        // Reverse-BFS from (tx,ty). Record parent direction (the
        // direction you'd press FROM (x,y) to step toward the target).
        short par[64][64];
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) par[y][x] = 0;
        // Mark goal with sentinel.
        par[ty][tx] = -1;
        P queue[64*64]; int qh=0, qt=0;
        queue[qt++] = (P){tx, ty};
        while (qh < qt) {
            P p = queue[qh++];
            for (int k = 0; k < 8; k++) {
                int nx = p.x + dxs[k], ny = p.y + dys[k];
                if (nx<0||ny<0||nx>=m->width||ny>=m->height) continue;
                if (par[ny][nx] != 0) continue;
                if (!land[ny][nx]) continue;
                const Tile *t = &m->tiles[ny][nx];
                // Intermediate (non-target) tiles: walkable AND
                // not a town/castle/dwelling (those bounce-back and
                // open a view we don't want to enter unintentionally).
                // Chests and foes are OK as intermediates — chests
                // give a one-tick A press to claim, foes give one
                // combat to clear, and then the tile is passable.
                bool is_target_tile = (nx == tx && ny == ty);
                if (!is_target_tile) {
                    if (t->interactive == INTERACT_TOWN) continue;
                    if (t->interactive == INTERACT_CASTLE_GATE) continue;
                    if (t->blocks_foot) continue;
                    if (!TerrainWalkable(t->terrain)) continue;
                }
                // Encode: the direction the hero at (nx,ny) should
                // press to step toward (px,py) = (nx - dx, ny - dy)
                // — which is the REVERSE of how we got here.
                int hero_dx = -dxs[k];
                int hero_dy = -dys[k];
                int key = dir_to_key(hero_dx, hero_dy);
                par[ny][nx] = (short)key;
                queue[qt++] = (P){nx, ny};
            }
        }
        // Emit the 64x64 grid.
        fprintf(stderr, "    /* target %d (%d,%d) */ {\n", ti, tx, ty);
        for (int y = 0; y < 64; y++) {
            fprintf(stderr, "        {");
            for (int x = 0; x < 64; x++) {
                int v = par[y][x];
                if (v < 0) v = -1;  // goal sentinel — runtime uses this
                                     // to detect "we are AT target."
                fprintf(stderr, "%d,", v);
            }
            fprintf(stderr, "},\n");
        }
        fprintf(stderr, "    },\n");
    }
    fprintf(stderr, "};\n");

    // 4. Boat-mode flow fields. Collect all chests on the WHOLE map
    //    that AREN'T on the start landmass. For each, reverse-BFS
    //    using mixed water/land passability: water tiles are
    //    boat-traversable; land tiles are foot-traversable; engine
    //    auto-switches travel mode on water↔land transitions.
    //    The hero starts the boat phase in TRAVEL_BOAT on a water
    //    tile; the flow steers boat across water until it lands on
    //    coast, then walks to the chest. Same flow handles both.
    Target boat_chests[64]; int n_boat_chests = 0;
    for (int y = 0; y < m->height && n_boat_chests < 64; y++) {
        for (int x = 0; x < m->width && n_boat_chests < 64; x++) {
            const Tile *t = &m->tiles[y][x];
            if (t->interactive != INTERACT_TREASURE_CHEST) continue;
            if (land[y][x]) continue;  // skip landmass chests (already in LAND flow)
            boat_chests[n_boat_chests++] = (Target){x, y, t->id};
        }
    }
    AP_LOG("// off-landmass chests: %d", n_boat_chests);
    for (int i = 0; i < n_boat_chests; i++) {
        fprintf(stderr, "//   boat_chest[%d] = (%d,%d) %s\n",
                i, boat_chests[i].x, boat_chests[i].y, boat_chests[i].id);
    }

    fprintf(stderr, "static const int N_BOAT_TARGETS = %d;\n", n_boat_chests);
    fprintf(stderr, "static const int BOAT_TARGET_X[%d] = {",
            n_boat_chests > 0 ? n_boat_chests : 1);
    for (int i = 0; i < n_boat_chests; i++)
        fprintf(stderr, "%d,", boat_chests[i].x);
    if (n_boat_chests == 0) fprintf(stderr, "0");
    fprintf(stderr, "};\n");
    fprintf(stderr, "static const int BOAT_TARGET_Y[%d] = {",
            n_boat_chests > 0 ? n_boat_chests : 1);
    for (int i = 0; i < n_boat_chests; i++)
        fprintf(stderr, "%d,", boat_chests[i].y);
    if (n_boat_chests == 0) fprintf(stderr, "0");
    fprintf(stderr, "};\n");

    fprintf(stderr, "static const int BOAT_FLOW[%d][64][64] = {\n",
            n_boat_chests > 0 ? n_boat_chests : 1);
    if (n_boat_chests == 0) {
        // Stub so the array isn't zero-sized.
        fprintf(stderr, "    /* (no boat targets) */ {{0}}\n");
    }
    for (int ti = 0; ti < n_boat_chests; ti++) {
        int tx = boat_chests[ti].x, ty = boat_chests[ti].y;
        short par[64][64];
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) par[y][x] = 0;
        par[ty][tx] = -1;
        P queue[64*64]; int qh=0, qt=0;
        queue[qt++] = (P){tx, ty};
        while (qh < qt) {
            P p = queue[qh++];
            for (int k = 0; k < 8; k++) {
                int nx = p.x + dxs[k], ny = p.y + dys[k];
                if (nx<0||ny<0||nx>=m->width||ny>=m->height) continue;
                if (par[ny][nx] != 0) continue;
                const Tile *t = &m->tiles[ny][nx];
                bool is_target_tile = (nx == tx && ny == ty);
                if (!is_target_tile) {
                    // Skip view-opening interactives (town, castle,
                    // dwelling). Chests and foes are OK as
                    // intermediates same as the land flow.
                    if (t->interactive == INTERACT_TOWN) continue;
                    if (t->interactive == INTERACT_CASTLE_GATE) continue;
                    // Allow: water (boat) OR walkable land (foot).
                    bool water = (t->terrain == TERRAIN_WATER) || t->is_bridge;
                    bool walkable = !t->blocks_foot &&
                                    TerrainWalkable(t->terrain);
                    if (!water && !walkable) continue;
                }
                int hero_dx = -dxs[k];
                int hero_dy = -dys[k];
                int key = dir_to_key(hero_dx, hero_dy);
                par[ny][nx] = (short)key;
                queue[qt++] = (P){nx, ny};
            }
        }
        fprintf(stderr, "    /* boat target %d (%d,%d) */ {\n", ti, tx, ty);
        for (int y = 0; y < 64; y++) {
            fprintf(stderr, "        {");
            for (int x = 0; x < 64; x++) {
                int v = par[y][x];
                if (v < 0) v = -1;
                fprintf(stderr, "%d,", v);
            }
            fprintf(stderr, "},\n");
        }
        fprintf(stderr, "    },\n");
    }
    fprintf(stderr, "};\n");

    fprintf(stderr,
        "// === END FLOW-FIELD TABLE ===\n");
}

#else
static void dump_zone_interactives(const Map *m, const Game *g) {
    (void)m; (void)g;
}
#endif // offline flow-field generator

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
    ApCmd cmd = ap_flow_phase(g, m, st, &phase_done, &next_phase);

    // 5. Set the live key for this tick (or clear).
    if (cmd.key) ap_set_key(cmd.key);
    else         ap_clear_key();

    // 6. Stash for next-tick assertion.
    st->pending = cmd;
    st->pending_active = true;

    // 7. Transition if signaled.
    if (phase_done) st->phase = next_phase;

    AP_LOG("tick=%d phase=%d cmd='%s' key=%d",
           st->tick, (int)st->phase,
           cmd.name ? cmd.name : "(unnamed)", cmd.key);

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
