// Autoplay core: the runner glue and shared building blocks every
// villain module reuses (BFS pathing, combat scripting, the
// before-frame combat hook, the common-prompt handler, the
// checkpoint save, and the top-level shell_run dispatcher).
//
// Per-villain logic — the phase enum and the per_frame state
// machine that drives one villain's capture — lives in a separate
// module file: src/autoplay/murray.c, src/autoplay/hack.c, etc.
// Each villain's terminal phase transitions in-memory into the
// next villain's initial phase; there is no save/load between
// villains. One --autoplay invocation runs the whole game from
// fresh start to the last wired-up villain.

#include "autoplay/core.h"
#include "autoplay/internal.h"

#include "input_host.h"
#include "frame_host.h"
#include "shell_run.h"
#include "startup.h"
#include "tables.h"
#include "combat.h"
#include "tile.h"
#include "raylib.h"
#include "ui.h"
#include "views.h"
#include "prompt.h"
#include "savegame.h"
#include "savepath.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =========================================================================
// Shared helpers
// =========================================================================

void ap_queue_standard_startup(void) {
    input_host_queue_key(KEY_A);     // class select: A = knight
    input_host_queue_key(KEY_ENTER); // name entry: empty -> "Hero"
    input_host_queue_key(KEY_ENTER); // difficulty: Normal (default)
}

void ap_queue_recruit_count(int n) {
    char buf[16];
    snprintf(buf, sizeof buf, "%d", n);
    for (const char *p = buf; *p; p++) {
        input_host_queue_key(KEY_ZERO + (*p - '0'));
    }
    input_host_queue_key(KEY_ENTER);
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

bool ap_save_checkpoint(const Game *g, const Map *m, const Fog *f) {
    const char *pack_id = (g->res && g->res->pack_id[0])
        ? g->res->pack_id : NULL;
    int slot = SAVE_SLOT_COUNT - 1;
    char path[512];
    if (!SavePathGetSlot(pack_id, slot, path, sizeof path)) {
        AP_LOG("checkpoint: SavePathGetSlot failed");
        return false;
    }
    SaveResult r = SaveGameWrite(path, g, m, f);
    if (r != SAVE_OK) {
        AP_LOG("checkpoint: SaveGameWrite failed (%s)",
               SaveResultText(r));
        return false;
    }
    AP_LOG("checkpoint: saved to slot %d (%s)", slot + 1, path);
    char msg[64];
    snprintf(msg, sizeof msg, "AutoPlay checkpoint saved (slot %d)",
             slot + 1);
    toast_show(msg);
    return true;
}

// =========================================================================
// Path-finding (BFS over MapWalkable + boat-aware extension)
// =========================================================================

int ap_bfs(const Map *m, int sx, int sy, int gx, int gy,
           ApTravel mode, ApPoint *out, int out_cap) {
    if (!m || !MapInBounds(m, sx, sy) || !MapInBounds(m, gx, gy)) return 0;
    int W = m->width;
    int H = m->height;
    short *parent = (short *)calloc((size_t)W * H, sizeof(short));
    if (!parent) return 0;

    ApPoint *queue = (ApPoint *)malloc(sizeof(ApPoint) * (size_t)W * H);
    if (!queue) { free(parent); return 0; }
    int qh = 0, qt = 0;
    queue[qt++] = (ApPoint){ sx, sy };
    parent[sy * W + sx] = 1;

    bool found = (sx == gx && sy == gy);

    static const int dxs[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
    static const int dys[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };

    while (!found && qh < qt) {
        ApPoint p = queue[qh++];
        for (int k = 0; k < 8; k++) {
            int nx = p.x + dxs[k];
            int ny = p.y + dys[k];
            if (!MapInBounds(m, nx, ny)) continue;
            if (parent[ny * W + nx]) continue;
            const Tile *t = MapGetTile(m, nx, ny);
            if (!t) continue;
            bool ok = false;
            if (mode == AP_TRAVEL_WALK) {
                ok = TerrainWalkable(t->terrain) && !t->blocks_foot;
                if (ok && t->interactive != INTERACT_NONE &&
                    t->interactive != INTERACT_TREASURE_CHEST &&
                    t->interactive != INTERACT_ARTIFACT) {
                    ok = false;
                }
            } else {
                ok = (t->terrain == TERRAIN_WATER) || t->is_bridge;
                // Coastal towns sit on water tiles with INTERACT_TOWN
                // stamped on top — sailing into one opens VIEW_TOWN and
                // wedges the autoplay. Same goes for any other
                // interactive that happens to share a water tile.
                // Routes around it; goal-override below still lets us
                // land on a non-water destination tile to disembark.
                if (ok && t->interactive != INTERACT_NONE) ok = false;
            }
            // Always allow the goal even if mode-blocked.
            if (!ok && (nx != gx || ny != gy)) continue;
            parent[ny * W + nx] = (short)((dys[k] + 1) * 3 + (dxs[k] + 1) + 1);
            queue[qt++] = (ApPoint){ nx, ny };
            if (nx == gx && ny == gy) { found = true; break; }
        }
    }

    int n = 0;
    if (found) {
        // Walk parent chain back to start. The chain can exceed
        // AP_PATH_MAX on long traversals; we still walk it to
        // termination but only keep the last (out_cap - 1) tiles +
        // the start. (This means out[] holds the *tail* of the
        // path, ending at the goal; if the path is too long the
        // start tile gets dropped but the goal is always present.)
        // We use a heap buffer so the chain length isn't bounded by
        // stack size — the WORLD bound is W*H tiles.
        ApPoint *chain = (ApPoint *)malloc(sizeof(ApPoint) * (size_t)(W * H));
        if (chain) {
            int sp = 0;
            int cx = gx, cy = gy;
            int chain_cap = W * H;
            while (sp < chain_cap) {
                chain[sp++] = (ApPoint){ cx, cy };
                if (cx == sx && cy == sy) break;
                short enc = parent[cy * W + cx];
                int dir = (int)enc - 1;
                int dy = dir / 3 - 1;
                int dx = dir % 3 - 1;
                cx -= dx;
                cy -= dy;
            }
            n = (sp <= out_cap) ? sp : out_cap;
            for (int i = 0; i < n; i++) {
                out[i] = chain[sp - 1 - i];
            }
            free(chain);
        }
    }
    free(queue);
    free(parent);
    return n;
}

int ap_dir_key(int cx, int cy, int nx, int ny) {
    int dx = nx - cx;
    int dy = ny - cy;
    if (dx == 0  && dy == -1) return KEY_UP;
    if (dx == 0  && dy ==  1) return KEY_DOWN;
    if (dx == -1 && dy ==  0) return KEY_LEFT;
    if (dx == 1  && dy ==  0) return KEY_RIGHT;
    if (dx == -1 && dy == -1) return KEY_HOME;
    if (dx == 1  && dy == -1) return KEY_PAGE_UP;
    if (dx == -1 && dy ==  1) return KEY_END;
    if (dx == 1  && dy ==  1) return KEY_PAGE_DOWN;
    return 0;
}

bool ap_ensure_path(AutoplayState *st, const Map *m,
                    int cx, int cy, int tx, int ty,
                    ApTravel mode) {
    bool target_changed = (st->path_target_x != tx) ||
                          (st->path_target_y != ty);
    if (st->path_len > 0 && !target_changed) {
        for (int delta = 0; delta < st->path_len; delta++) {
            int i = st->path_idx + delta;
            if (i < st->path_len &&
                st->path[i].x == cx && st->path[i].y == cy) {
                st->path_idx = i;
                return true;
            }
            i = st->path_idx - delta;
            if (i >= 0 &&
                st->path[i].x == cx && st->path[i].y == cy) {
                st->path_idx = i;
                return true;
            }
        }
    }
    st->path_len = ap_bfs(m, cx, cy, tx, ty, mode, st->path, AP_PATH_MAX);
    st->path_idx = 0;
    st->path_target_x = tx;
    st->path_target_y = ty;
    return st->path_len > 0;
}

bool ap_step_along_path(AutoplayState *st, int cx, int cy) {
    if (st->path_len == 0 || st->path_idx >= st->path_len) return false;
    while (st->path_idx < st->path_len &&
           st->path[st->path_idx].x == cx &&
           st->path[st->path_idx].y == cy) {
        st->path_idx++;
    }
    if (st->path_idx >= st->path_len) return true;  // arrived
    ApPoint next = st->path[st->path_idx];
    int k = ap_dir_key(cx, cy, next.x, next.y);
    if (k == 0) {
        AP_LOG("ap_dir_key 0: cur=(%d,%d) want=(%d,%d) idx=%d",
               cx, cy, next.x, next.y, st->path_idx);
        return false;
    }
    if (input_host_queue_depth() > 0) return true;
    input_host_queue_key(k);
    return true;
}

// =========================================================================
// Combat decision helpers
// =========================================================================

int ap_closest_enemy(const Combat *c, int ux, int uy, int *out_dist) {
    int best = -1, best_d = 1 << 30;
    for (int i = 0; i < COMBAT_SLOTS; i++) {
        const CombatUnit *e = &c->units[COMBAT_SIDE_AI][i];
        if (e->troop_idx < 0 || e->count <= 0) continue;
        int adx = e->x - ux; if (adx < 0) adx = -adx;
        int ady = e->y - uy; if (ady < 0) ady = -ady;
        int d = (adx > ady) ? adx : ady;  // chebyshev (8-direction)
        if (d < best_d) { best_d = d; best = i; }
    }
    if (out_dist) *out_dist = best_d;
    return best;
}

// Highest-total-HP enemy stack (count * hit_points), tiebroken by
// proximity. Used for ranged target selection so shooters focus-fire
// the biggest threat (e.g. Nomads at 150 HP) instead of pinging the
// nearest peasant stack. Returns -1 if no enemies remain.
int ap_highest_threat_enemy(const Combat *c, int ux, int uy,
                            int *out_dist) {
    int best = -1, best_hp = -1, best_d = 1 << 30;
    for (int i = 0; i < COMBAT_SLOTS; i++) {
        const CombatUnit *e = &c->units[COMBAT_SIDE_AI][i];
        if (e->troop_idx < 0 || e->count <= 0) continue;
        const TroopDef *td = troop_by_index(e->troop_idx);
        int hp_each = td ? td->hit_points : 1;
        int total_hp = e->count * hp_each;
        int adx = e->x - ux; if (adx < 0) adx = -adx;
        int ady = e->y - uy; if (ady < 0) ady = -ady;
        int d = (adx > ady) ? adx : ady;
        // Strict total-HP comparison; tiebreak on proximity so equal
        // threats prefer the closer one (less likely to take retal
        // damage moving to engage).
        bool better = (total_hp > best_hp) ||
                      (total_hp == best_hp && d < best_d);
        if (better) {
            best_hp = total_hp;
            best_d = d;
            best = i;
        }
    }
    if (out_dist) *out_dist = best_d;
    return best;
}

int ap_queue_picker(int cx, int cy, int tx, int ty) {
    int queued = 0;
    while (cx != tx || cy != ty) {
        int dx = 0, dy = 0;
        if (tx > cx) dx = 1; else if (tx < cx) dx = -1;
        if (ty > cy) dy = 1; else if (ty < cy) dy = -1;
        int k = ap_dir_key(cx, cy, cx + dx, cy + dy);
        if (k == 0) break;
        input_host_queue_key(k);
        queued++;
        cx += dx;
        cy += dy;
        if (queued > 32) break;
    }
    input_host_queue_key(KEY_ENTER);
    queued++;
    return queued;
}

// =========================================================================
// Common prompt / combat handler
// =========================================================================
//
// Modules call ap_handle_common_prompts() near the top of their
// per_frame. If it returns true, the module should return CONTINUE
// without touching its switch — input has already been queued (or
// the engine is mid-something and we should wait).

// Phases where a dialog/prompt is EXPECTED and the common handler
// must NOT auto-dismiss it (the module's own switch case wants
// first crack). Modules whose phase enum maps to one of these
// "expect dialog" intents should add their phase here.
static bool ap_phase_expects_dialog(AutoplayPhase p) {
    return p == AP_MURRAY_DISMISS_INTRO ||
           p == AP_MURRAY_TAKE_CONTRACT ||
           p == AP_MURRAY_POST_COMBAT_DIALOG ||
           p == AP_HACK_TAKE_CONTRACT ||
           p == AP_HACK_POST_COMBAT_DIALOG;
}

static bool ap_phase_expects_prompt(AutoplayPhase p) {
    return p == AP_MURRAY_SIEGE_PROMPT ||
           p == AP_MURRAY_COMBAT ||
           p == AP_HACK_SIEGE_PROMPT ||
           p == AP_HACK_COMBAT ||
           // Dwelling phases expect a text-input "how many to recruit?"
           // prompt — common-prompts handler would ESC it before we
           // could enter the count.
           p == AP_HACK_WALK_TO_DWELLING ||
           p == AP_HACK_RECRUIT_AT_DWELLING ||
           p == AP_HACK_WALK_FROM_LAND_TO_DWELLING;
}

bool ap_handle_common_prompts(const AutoplayState *st) {
    // Stray dialog (e.g. weekend astrology) — dismiss with SPACE.
    if (dialog_is_active() && !ap_phase_expects_dialog(st->phase)) {
        if (input_host_queue_depth() == 0) {
            input_host_queue_key(KEY_SPACE);
        }
        return true;
    }
    // Stray prompts:
    //   - "Foes!" / "Siege" / "Castle ..." → press Y (attack).
    //   - A/B picker (chest gold-or-leadership) → A (gold).
    //   - everything else → N (decline).
    if (prompt_is_active() && !ap_phase_expects_prompt(st->phase)) {
        if (input_host_queue_depth() == 0) {
            const char *hdr = prompt_header_text();
            const char *kind = prompt_kind_str();
            int key = KEY_N;
            const char *what = "decline";
            // Text-input prompts (dwelling recruit, hero-name) need
            // ESC to dismiss — N is ignored and the prompt spins forever.
            // Numeric prompts (dismiss-army slot picker) also take ESC.
            if (kind && (strcmp(kind, "text") == 0 ||
                         strcmp(kind, "numeric") == 0)) {
                key = KEY_ESCAPE; what = "ESC (dismiss text/numeric)";
            } else if (hdr && (strstr(hdr, "Foe") || strstr(hdr, "Siege") ||
                               strstr(hdr, "Castle"))) {
                key = KEY_Y; what = "ATTACK";
            } else if (kind && strcmp(kind, "ab") == 0) {
                key = KEY_A; what = "A (gold)";
            }
            AP_LOG("prompt kind=%s header='%s' → %s",
                   kind, hdr, what);
            // Castle siege diagnostic: dump army vs garrison.
            if (hdr && strstr(hdr, "Castle") && key == KEY_Y) {
                extern Game *g_active_game_for_diag;
                Game *gd = g_active_game_for_diag;
                if (gd) {
                    AP_LOG("siege MATCHUP — our army HP=%d gold=%d "
                           "leadership=%d:",
                           ap_army_total_hp(gd), gd->stats.gold,
                           gd->stats.leadership_current);
                    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
                        const ArmyStack *u = &gd->army[i];
                        if (!u->id[0] || u->count <= 0) continue;
                        const TroopDef *td = troop_by_id(u->id);
                        fprintf(stderr, "[autoplay]   ours[%d]: %d × %s "
                                "(%d HP each)\n", i, u->count,
                                td ? td->name : u->id,
                                td ? td->hit_points : 0);
                    }
                    for (int ci = 0; ci < GAME_CASTLES; ci++) {
                        const CastleRecord *cr = &gd->castles[ci];
                        if (!cr->id[0]) continue;
                        if (cr->owner_kind != CASTLE_OWNER_VILLAIN &&
                            cr->owner_kind != CASTLE_OWNER_MONSTERS) continue;
                        // Pick whichever castle the hero is adjacent to.
                        const ResCastle *rc = resources_castle_by_id(
                            gd->res, cr->id);
                        if (!rc) continue;
                        int dx = rc->x - gd->position.x;
                        int dy = rc->y - gd->position.y;
                        if (dx < 0) dx = -dx;
                        if (dy < 0) dy = -dy;
                        if (dx > 1 || dy > 1) continue;
                        AP_LOG("siege MATCHUP — garrison at %s (villain=%s):",
                               cr->id, cr->villain_id);
                        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
                            const Unit *u = &cr->garrison[i];
                            if (!u->id[0] || u->count <= 0) continue;
                            const TroopDef *td = troop_by_id(u->id);
                            fprintf(stderr, "[autoplay]   their[%d]: %d × %s "
                                    "(%d HP each)\n", i, u->count,
                                    td ? td->name : u->id,
                                    td ? td->hit_points : 0);
                        }
                        break;
                    }
                }
            }
            input_host_queue_key(key);
        }
        return true;
    }
    return false;
}

// =========================================================================
// Frame-host before-frame hook (combat input scripting)
// =========================================================================
//
// per_frame fires from the overworld main loop only. During
// RunCombat we drop into a different loop owned by combat_loop.c —
// so we wire a before-frame callback on the frame_host that fires
// every iteration of THAT loop too, and use it to script combat
// input. Outside of combat the callback is a no-op.

static AutoplayState *g_active_autoplay_state = NULL;
int                   g_autoplay_frame_counter = 0;

static void autoplay_before_frame(void *user) {
    (void)user;
    AutoplayState *st = g_active_autoplay_state;
    if (!st) return;
    int frame_no = g_autoplay_frame_counter++;
    Combat *c = combat_current_rendered;
    if (!c) return;

    // Dismiss any post-combat dialog that pops up inside RunCombat.
    if (dialog_is_active()) {
        if (input_host_queue_depth() == 0) {
            input_host_queue_key(KEY_SPACE);
        }
        return;
    }

    // Combat over? Wait for RunCombat to return.
    if (c->result != 0) return;
    if (c->picker_active) return;
    if (c->side != COMBAT_SIDE_PLAYER) return;
    if (c->unit_id < 0) return;
    if (input_host_queue_depth() > 0) return;

    CombatUnit *u = &c->units[c->side][c->unit_id];
    if (u->acted || u->troop_idx < 0 || u->count <= 0) return;
    if (c->unit_id == st->combat_unit_id_last_acted &&
        frame_no < st->combat_frame_last_action + 6) {
        return;
    }
    // Two enemy choices: closest (for melee approach) and highest-
    // threat (for ranged target). Ranged shots ignore the "must be
    // closer than 1 tile" check against the target — we just need ANY
    // enemy more than 1 tile away to be allowed to shoot.
    int enemy_d = 0;
    int enemy_slot = ap_closest_enemy(c, u->x, u->y, &enemy_d);
    if (enemy_slot < 0) return;
    const CombatUnit *enemy = &c->units[COMBAT_SIDE_AI][enemy_slot];
    bool can_shoot = (u->shots > 0 && enemy_d > 1 &&
                      !combat_unit_surrounded(c, c->side, c->unit_id));
    if (can_shoot) {
        int tgt_d = 0;
        int tgt_slot = ap_highest_threat_enemy(c, u->x, u->y, &tgt_d);
        if (tgt_slot < 0) tgt_slot = enemy_slot;
        const CombatUnit *target = &c->units[COMBAT_SIDE_AI][tgt_slot];
        input_host_queue_key(KEY_S);
        ap_queue_picker(u->x, u->y, target->x, target->y);
    } else {
        static const int dxs[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
        static const int dys[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
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
                best_k = ap_dir_key(u->x, u->y, nx, ny);
            }
        }
        if (best_k != 0) {
            input_host_queue_key(best_k);
        } else {
            input_host_queue_key(KEY_SPACE);
        }
    }
    st->combat_unit_id_last_acted = c->unit_id;
    st->combat_frame_last_action = frame_no;
}

// =========================================================================
// Top-level dispatcher
// =========================================================================
//
// Routes per_frame to the right module based on which contiguous
// block the current phase falls in.

static void autoplay_before_startup(ShellRunHooks *self) {
    (void)self;
    ap_queue_standard_startup();
}

Game *g_active_game_for_diag = NULL;

static ShellRunVerdict autoplay_per_frame(ShellRunHooks *self,
                                          Game *g, Map *m, Fog *f,
                                          Resources *res, int frame_no) {
    AutoplayState *st = (AutoplayState *)self->user;
    g_active_game_for_diag = g;

    // Shared input handling: dismiss strays, auto-attack stray foes,
    // drive combat. If this fires, the module's switch is skipped.
    if (ap_handle_common_prompts(st)) return SHELL_RUN_CONTINUE;

    // Mirror the combat-driver here so that combat triggered from
    // the main loop's prompt-dispatch (which yields immediately to
    // RunCombat) is steered even on the frame the prompt resolves.
    // RunCombat itself runs autoplay_before_frame each combat frame.
    Combat *c_active = combat_current_rendered;
    if (c_active && c_active->result == 0 && !c_active->picker_active &&
        c_active->side == COMBAT_SIDE_PLAYER && c_active->unit_id >= 0 &&
        input_host_queue_depth() == 0) {
        CombatUnit *u = &c_active->units[c_active->side][c_active->unit_id];
        if (!u->acted && !(u->troop_idx < 0 || u->count <= 0)) {
            if (c_active->unit_id != st->combat_unit_id_last_acted ||
                frame_no >= st->combat_frame_last_action + 6) {
                int enemy_d = 0;
                int enemy_slot = ap_closest_enemy(c_active, u->x, u->y,
                                                  &enemy_d);
                if (enemy_slot >= 0) {
                    const CombatUnit *enemy =
                        &c_active->units[COMBAT_SIDE_AI][enemy_slot];
                    bool can_shoot = (u->shots > 0 && enemy_d > 1 &&
                                      !combat_unit_surrounded(c_active,
                                          c_active->side,
                                          c_active->unit_id));
                    if (can_shoot) {
                        input_host_queue_key(KEY_S);
                        ap_queue_picker(u->x, u->y, enemy->x, enemy->y);
                    } else {
                        int dx = 0, dy = 0;
                        if (enemy->x > u->x) dx = 1;
                        else if (enemy->x < u->x) dx = -1;
                        if (enemy->y > u->y) dy = 1;
                        else if (enemy->y < u->y) dy = -1;
                        int k = ap_dir_key(u->x, u->y,
                                           u->x + dx, u->y + dy);
                        if (k == 0) input_host_queue_key(KEY_SPACE);
                        else        input_host_queue_key(k);
                    }
                } else {
                    input_host_queue_key(KEY_SPACE);
                }
                st->combat_unit_id_last_acted = c_active->unit_id;
                st->combat_frame_last_action = frame_no;
            }
        }
    }

    // Dispatch to the module that owns this phase. New villains
    // plug in by adding their (FIRST..LAST) range here.
    if (st->phase >= AP_MURRAY_FIRST && st->phase <= AP_MURRAY_LAST) {
        return ap_murray_per_frame(g, m, f, res, frame_no, st);
    }
    if (st->phase >= AP_HACK_FIRST && st->phase <= AP_HACK_LAST) {
        return ap_hack_per_frame(g, m, f, res, frame_no, st);
    }
    if (st->phase == AP_ALL_DONE) {
        printf("autoplay: all wired-up villains captured\n");
        return SHELL_RUN_EXIT_PASS;
    }
    AP_LOG("dispatcher: unknown phase %d", (int)st->phase);
    return SHELL_RUN_EXIT_FAIL;
}

// =========================================================================
// Public entry
// =========================================================================

int autoplay_run(int argc, char **argv) {
    input_host_use_queue();
    frame_host_use_test();
    startup_skip_intros = true;

    AutoplayState state = { 0 };
    state.phase = AP_MURRAY_FIRST;
    state.combat_unit_id_last_acted = -1;
    g_active_autoplay_state = &state;
    g_autoplay_frame_counter = 0;
    frame_host_set_before_frame(autoplay_before_frame, NULL);

    ShellRunHooks hooks = {
        .before_startup = autoplay_before_startup,
        .per_frame      = autoplay_per_frame,
        .user           = &state,
    };
    int rc = shell_run_game(argc, argv, &hooks);

    frame_host_set_before_frame(NULL, NULL);
    g_active_autoplay_state = NULL;
    return rc;
}
