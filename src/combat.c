#include "combat.h"
#include "combat_render.h"
#include "tables.h"
#include "resources.h"
#include "ui.h"
#include "raylib.h"
#include "harness_input.h"
#include "harness.h"
#include "recorder.h"
#include "audio.h"
#include "bfont.h"
#include "layout.h"
#include "chrome.h"
#include "palette.h"
#include "overlay.h"
#include "screenshot.h"
#include <stdio.h>
#include <string.h>

// Combat module — Phase 1 scaffold. Replaces the old stub. Builds the
// Combat data structure, opens the scouts-have-sighted banner (kept
// from the stub for kb_055 parity), and concedes WIN. Subsequent
// phases fill in prepare_*, reset_match, the turn loop, the renderer,
// AI, spells, and end-of-combat. Reference: docs/COMBAT-PLAN.md.

// Resolve the Resources pointer for combat-log template lookups.
// Player side carries the live Game*; AI-only paths fall back to NULL,
// which the callers already treat as "use defaults". Read-only.
static inline const ResCombatLog *combat_log_strings(const Combat *c) {
    if (c && c->heroes[0] && c->heroes[0]->res) {
        return &c->heroes[0]->res->combat_log;
    }
    return NULL;
}

// Translate the hero's found-artifact set into the COMBAT_POWER_* bit
// field used during combat. Powers that have no in-combat effect
// (DOUBLE_LEADERSHIP, INCREASE_COMMISSION, etc.) are not mapped here;
// they apply at pickup or on the adventure screen.
static unsigned char combat_player_powers(const Game *g) {
    unsigned char p = 0;
    for (int i = 0; i < 8; i++) {
        if (!g->artifacts.found[i]) continue;
        const ArtifactDef *a = artifact_by_index(i);
        if (!a) continue;
        if (a->power == ARTIFACT_POWER_INCREASED_DAMAGE)   p |= COMBAT_POWER_INCREASED_DAMAGE;
        if (a->power == ARTIFACT_POWER_QUARTER_PROTECTION) p |= COMBAT_POWER_QUARTER_PROTECTION;
    }
    return p;
}

// Initial structure init — zero everything, capture mode metadata,
// translate hero powers, pre-fill empty-slot sentinel troop_idx.
// Phases 2–10 add the real prepare/reset/loop logic.
/* exposed for tests */ void combat_init(Combat *c, Game *g, CombatMode mode,
                        const CombatTarget *target) {
    memset(c, 0, sizeof *c);
    c->mode = mode;
    c->castle = (mode == COMBAT_MODE_CASTLE);
    c->heroes[COMBAT_SIDE_PLAYER] = g;
    c->heroes[COMBAT_SIDE_AI]     = NULL;
    c->powers[COMBAT_SIDE_PLAYER] = combat_player_powers(g);
    c->powers[COMBAT_SIDE_AI]     = 0;
    c->result = 0;
    c->villain_id[0] = '\0';
    c->target_filter = 0;
    if (target && target->name && target->name[0]) {
        snprintf(c->target_name, sizeof c->target_name, "%s", target->name);
    } else {
        c->target_name[0] = '\0';
    }
    for (int s = 0; s < COMBAT_SIDES; s++) {
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            c->units[s][i].troop_idx = -1;
        }
    }
}

// ----- RNG -------------------------------------------------------------------
// Per-combat LCG, separate from the world RNG.  / OPENBOUNTY
// : same multiplier and increment so combat random rolls have
// the same statistical shape as world rolls, but the seeded state
// is independent so a battle does not perturb post-combat overworld
// outcomes. Phase 14 verifies determinism with a fixed seed.

/* exposed for tests */ void combat_seed_rng(Combat *c, const Game *g, CombatMode mode,
                            const CombatTarget *target) {
    uint64_t s = g->seed;
    s ^= 0x5DEECE66DULL;
    s ^= (uint64_t)g->stats.followers_killed * 0x9E3779B97F4A7C15ULL;
    s ^= (uint64_t)mode * 0xBF58476D1CE4E5B9ULL;
    if (target && target->name) {
        for (const char *p = target->name; *p; p++) {
            s = s * 131ULL + (unsigned char)*p;
        }
    }
    c->rng_state = s ? s : 1ULL;
}

/* exposed for tests */ int kb_rand(Combat *c, int min, int max) {
    if (min > max) return min;
    if (min == max) return min;
    c->rng_state = c->rng_state * 25214903917ULL + 11ULL;
    unsigned int r = (unsigned int)(c->rng_state >> 32);
    return min + (int)(r % (unsigned int)(max - min + 1));
}

// ----- Distance helpers () --------------------------------------
// Integer square root, fixed-point 16.16. Returns floor(sqrt(h) * 65536).
// Direct transcription of  lines 5733–5752 — the combat
// AI's tie-breaking depends on this exact bit-walk; do not replace
// with sqrtf. Operates on 32-bit values (uint32_t) per spec.
#include <stdint.h>
static unsigned long isqrt32(unsigned long h_in) {
    uint32_t h = (uint32_t)h_in;
    uint32_t x = 0, y = 0;
    for (int i = 0; i < 32; i++) {
        x = (x << 1) | 1;
        if (y < x) x -= 2;
        else       y -= x;
        x++;
        y <<= 1;
        if (h & 0x80000000U) y |= 1;
        h <<= 1;
        y <<= 1;
        if (h & 0x80000000U) y |= 1;
        h <<= 1;
    }
    return x;
}

// Squared-distance × 256 (fixed point).  lines 5766–5772.
// Returns isqrt32((dx*dx + dy*dy) << 16) so callers can compare without
// introducing floating point. Currently unused outside AI (Phase 10) —
// kept here so the helper lands with the rest of the math primitives.
static unsigned long calc_distance(int x1, int y1, int x2, int y2) {
    long dx = x2 - x1;
    long dy = y2 - y1;
    unsigned long d2 = (unsigned long)((dx * dx) + (dy * dy));
    return isqrt32(d2 << 16);
}

// ----- Friendliness () -------------------------------------------
// A unit is "under control" iff its side has a hero AND the hero's
// leadership covers the stack's hp × count. Out-of-control units
// behave as enemies of their own side. Currently used only by
// prepare_player; expanded in Phase 5 (melee) when retaliation
// targeting needs it.
/* exposed for tests */ bool unit_under_control(const Game *g, int troop_idx, int count) {
    if (!g) return false;
    const TroopDef *t = troop_by_index(troop_idx);
    if (!t) return false;
    // : a stack is OOC when
    //   hp * count > leadership (strict).
    // Recruiting up to the displayed max (hp*count == leadership)
    // is the boundary case that DOS King's Bounty allows — still
    // under control. Note: this matches the recruit cap exactly:
    // GameMaxRecruitable returns (leadership - same_troop_hp*count)/hp
    // which permits filling to exactly leadership.
    int leadership = g->stats.leadership_current;
    return (t->hit_points * count) <= leadership;
}

/* exposed for tests */ bool units_are_friendly(const Combat *c, int sA, int iA, int sB, int iB) {
    const CombatUnit *a = &c->units[sA][iA];
    const CombatUnit *b = &c->units[sB][iB];
    if (a->out_of_control || b->out_of_control) return false;
    return sA == sB;
}

// ----- Morale () ---------------------------------------
// Numeric ranks per  lines 560–562: NORMAL=0, LOW=1, HIGH=2.
// Combat-engine attacker side applies morale to final_damage when
// the side has a hero AND the unit is under control.
typedef enum {
    COMBAT_MORALE_NORMAL = 0,
    COMBAT_MORALE_LOW    = 1,
    COMBAT_MORALE_HIGH   = 2,
} CombatMorale;

/* exposed for tests */ int morale_to_rank(char r) {
    if (r == 'L') return COMBAT_MORALE_LOW;
    if (r == 'H') return COMBAT_MORALE_HIGH;
    return COMBAT_MORALE_NORMAL;
}

// Per-unit morale lookup. Mirrors play.c troop_morale exactly,
// including the documented bug ( lines 2531–2545 + ):
// the loop adopts the *lower-ranked* morale via `<`, where the
// converter assigns NORMAL=0, LOW=1, HIGH=2. Net effect: any mixed
// army that doesn't compute HIGH for every pairing degrades to
// NORMAL, never LOW unless LOW is the only computed result. We
// reproduce this faithfully — instruction was "functionally
// identical ".
static CombatMorale troop_morale_for_unit(const Combat *c, int side, int slot) {
    const CombatUnit *self = &c->units[side][slot];
    if (self->troop_idx < 0) return COMBAT_MORALE_NORMAL;
    const TroopDef *ts = troop_by_index(self->troop_idx);
    if (!ts) return COMBAT_MORALE_NORMAL;
    int morale = COMBAT_MORALE_HIGH;
    for (int j = 0; j < COMBAT_SLOTS; j++) {
        const CombatUnit *o = &c->units[side][j];
        if (o->troop_idx < 0 || o->count == 0) continue;
        const TroopDef *to = troop_by_index(o->troop_idx);
        if (!to) continue;
        char nm = morale_result(to->morale_group, ts->morale_group);
        int r = morale_to_rank(nm);
        if (r < morale) morale = r;
    }
    return (CombatMorale)morale;
}

// ----- Battlefield setup (, , ) ----------------------

// Pack a (side, slot) pair into the 1-based UID used by umap. UID 0
// means empty cell. UIDs 1..5 are player slots 0..4; 6..10 are AI
// slots 0..4.  line 6512.
static inline unsigned char pack_uid(int side, int slot) {
    return (unsigned char)(side * COMBAT_SLOTS + slot + 1);
}

// Initialise per-unit state at match start.
/* exposed for tests */ void combat_init_unit(CombatUnit *u, int troop_idx, int count) {
    u->troop_idx  = troop_idx;
    u->count      = count;
    u->max_count  = count;
    u->turn_count = count;
    u->dead       = false;
    u->frame      = 0;
    u->injury     = 0;
    u->acted      = false;
    u->retaliated = false;
    u->moves      = 0;
    u->shots      = 0;
    u->flights    = 0;
    u->frozen     = false;
    u->out_of_control = false;
    u->x = 0;
    u->y = 0;
}

// Player side: read 5 ArmyStacks from g->army, place at column 0,
// rows 0..4. spoils[0] is read for completeness; the player-loss
// path doesn't award spoils to the AI yet.
/* exposed for tests */ void combat_prepare_player(Combat *c, const Game *g) {
    for (int i = 0; i < COMBAT_SLOTS; i++) {
        const ArmyStack *as = &g->army[i];
        if (!as->id[0] || as->count == 0) continue;
        const TroopDef *t = troop_by_id(as->id);
        if (!t) continue;
        CombatUnit *u = &c->units[COMBAT_SIDE_PLAYER][i];
        combat_init_unit(u, t->index, as->count);
        u->x = 0;
        u->y = i;
        u->out_of_control = !unit_under_control(g, t->index, as->count);
        c->spoils[COMBAT_SIDE_PLAYER] += t->spoils_factor * 5 * as->count;
    }
}

// AI side: open-field foe band — limit to first 3 of 5 slots
// ( line 5216). Place at column W-1 = 5, rows 0..2.
// Castles use the full 5 slots (placement happens in reset_match).
/* exposed for tests */ void combat_prepare_foe(Combat *c, const CombatTarget *target) {
    if (!target || !target->garrison) return;
    int placed = 0;
    int slots = target->garrison_slots;
    if (slots > COMBAT_SLOTS) slots = COMBAT_SLOTS;
    const int max_band = 3;
    for (int i = 0; i < slots && placed < max_band; i++) {
        const Unit *src = &target->garrison[i];
        if (!src->id[0] || src->count == 0) continue;
        const TroopDef *t = troop_by_id(src->id);
        if (!t) continue;
        CombatUnit *u = &c->units[COMBAT_SIDE_AI][placed];
        combat_init_unit(u, t->index, src->count);
        u->x = COMBAT_W - 1;
        u->y = placed;
        c->spoils[COMBAT_SIDE_AI] += t->spoils_factor * 5 * src->count;
        placed++;
    }
}

// Castle siege: read up to 5 garrison stacks; positions are stamped
// later by reset_match via the castle_umap table.
/* exposed for tests */ void combat_prepare_castle(Combat *c, const CombatTarget *target) {
    if (!target || !target->garrison) return;
    int slots = target->garrison_slots;
    if (slots > COMBAT_SLOTS) slots = COMBAT_SLOTS;
    int placed = 0;
    for (int i = 0; i < slots && placed < COMBAT_SLOTS; i++) {
        const Unit *src = &target->garrison[i];
        if (!src->id[0] || src->count == 0) continue;
        const TroopDef *t = troop_by_id(src->id);
        if (!t) continue;
        CombatUnit *u = &c->units[COMBAT_SIDE_AI][placed];
        combat_init_unit(u, t->index, src->count);
        // x/y filled by reset_match castle path.
        c->spoils[COMBAT_SIDE_AI] += t->spoils_factor * 5 * src->count;
        placed++;
    }
}

// Castle layout.  lines 5882–5901, decoded literally.
// castle_umap entries are packed UIDs (side*5 + id + 1). The
// castle_omap entries 5..10 are decorative wall tile codes; values
// 1..3 are the random-obstacle codes used in open-field. We keep
// the wall codes ≥ 4 so passability checks can simply "obstacle != 0".
static const unsigned char castle_umap[COMBAT_H][COMBAT_W] = {
    { 0, 8, 6, 7, 9, 0 },
    { 0, 0,10, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0 },
    { 0, 5, 3, 4, 0, 0 },
    { 0, 0, 1, 2, 0, 0 },
};
static const unsigned char castle_omap[COMBAT_H][COMBAT_W] = {
    { 8, 0, 0, 0, 0, 9 },
    { 8, 0, 0, 0, 0, 9 },
    { 8, 0, 0, 0, 0, 9 },
    { 8, 0, 0, 0, 0, 9 },
    { 5, 7, 0, 0,10, 6 },
};

// reset_match: stamp obstacles, place units, refresh per-unit
// per-match counters, prime the turn machinery. 
// lines 5219–5230 +  + .
/* exposed for tests */ void combat_reset_match(Combat *c) {
    memset(c->omap, 0, sizeof c->omap);
    memset(c->umap, 0, sizeof c->umap);

    if (c->castle) {
        // Stamp the fixed castle layout.
        for (int y = 0; y < COMBAT_H; y++) {
            for (int x = 0; x < COMBAT_W; x++) {
                c->omap[y][x] = castle_omap[y][x];
                unsigned char uid = castle_umap[y][x];
                if (uid == 0) continue;
                int side = (uid - 1) / COMBAT_SLOTS;
                int slot = (uid - 1) % COMBAT_SLOTS;
                CombatUnit *u = &c->units[side][slot];
                if (u->troop_idx < 0 || u->count == 0) continue;
                u->x = x;
                u->y = y;
            }
        }
    } else {
        // Open-field random obstacles.  lines 5862–5871.
        // i ∈ {1, 2, 3}; ~10% chance per cell; obstacle code 1..3.
        for (int j = 0; j < COMBAT_H; j++) {
            for (int i = 1; i <= COMBAT_W - 3; i++) {
                if (kb_rand(c, 0, 9) == 0) {
                    c->omap[j][i] = (unsigned char)kb_rand(c, 1, 3);
                }
            }
        }
        // Player at column 0 (rows 0..4 already set in prepare_player).
        // AI at column W-1 (rows 0..2 set in prepare_foe).
    }

    // Stamp umap and refresh per-unit shots/injury/frozen/turn_count.
    for (int s = 0; s < COMBAT_SIDES; s++) {
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            CombatUnit *u = &c->units[s][i];
            if (u->troop_idx < 0 || u->count == 0) continue;
            const TroopDef *t = troop_by_index(u->troop_idx);
            u->shots      = t ? t->ranged_ammo : 0;
            u->injury     = 0;
            u->frozen     = false;
            u->turn_count = u->count;
            c->umap[u->y][u->x] = pack_uid(s, i);
        }
    }

    c->turn  = 0;
    c->phase = 0;
    c->side  = COMBAT_SIDE_PLAYER;
    c->unit_id = -1;
    c->spells_this_round = 0;
    c->first_kill_seen = false;
    c->stacks_destroyed = 0;
    c->log_count = 0;
    c->banner[0] = '\0';
}

// ----- Damage formula () -----------------------------------------
//
// deal_damage is the heart of the combat engine. The implementation
// is a literal transcription of  (lines 5310–5380)
// with each numbered step preserved as a comment. Order of operations
// is load-bearing — every multiplicative step uses integer division
// and floors before the next step, so reordering changes outcomes.
//
// Returns: number of full creatures killed in the target stack.
//          -1 if attack was cancelled (MAGIC vs IMMUNE).

/* exposed for tests */ int combat_deal_damage(Combat *c,
                              int a_side, int a_id,
                              int t_side, int t_id,
                              bool is_ranged,
                              bool is_external,
                              int  external_damage,
                              bool retaliation) {
    CombatUnit *u = (a_side >= 0 && a_id >= 0) ? &c->units[a_side][a_id] : NULL;
    CombatUnit *t = &c->units[t_side][t_id];
    const TroopDef *ut = u ? troop_by_index(u->troop_idx) : NULL;
    const TroopDef *tt = troop_by_index(t->troop_idx);
    if (!tt) return 0;

    int demon_kills = 0;          // Step 1.
    int final_damage = 0;

    if (is_external) {
        // Step 2: spell damage. No attacker unit; final_damage is set
        // directly by the caller.
        u = NULL;
        ut = NULL;
        final_damage = external_damage;
    } else {
        if (!u || !ut) return 0;

        // Step 3a: SCYTHE (Demon special).
        if (ut->abilities & TROOP_ABIL_SCYTHE) {
            if (kb_rand(c, 1, 100) > 89) {
                // ceil(t.count / 2)
                demon_kills = t->count / 2 + ((t->count % 2) ? 1 : 0);
            }
        }

        int dmg = 0;
        if (is_ranged) {
            // Step 3b: ranged. Suppress retaliation, decrement shots.
            retaliation = true;          // ranged never triggers retaliation
            if (u->shots > 0) u->shots--;
            if (ut->abilities & TROOP_ABIL_MAGIC) {
                // MAGIC ranged: fixed damage = ranged_min, cancelled by IMMUNE.
                if (tt->abilities & TROOP_ABIL_IMMUNE) {
                    return -1;           // attack cancelled
                }
                dmg = ut->ranged_min;
            } else {
                dmg = kb_rand(c, ut->ranged_min, ut->ranged_max);
            }
        } else {
            // Step 3c: melee.
            dmg = kb_rand(c, ut->melee_min, ut->melee_max);
        }

        // Step 3d: core formula.
        int total = dmg * u->turn_count;
        int skill_diff = ut->skill_level + 5 - tt->skill_level;
        final_damage = (total * skill_diff) / 10;

        // Step 3e: morale (attacker side, hero present, under control).
        if (c->heroes[a_side] != NULL && !u->out_of_control) {
            CombatMorale m = troop_morale_for_unit(c, a_side, a_id);
            if (m == COMBAT_MORALE_LOW) {
                final_damage /= 2;
            } else if (m == COMBAT_MORALE_HIGH) {
                final_damage += final_damage / 2;
            }
            // NORMAL: unchanged.
        }

        // Step 3f: artifact powers, attacker side.
        if (c->powers[a_side] & COMBAT_POWER_INCREASED_DAMAGE) {
            final_damage += final_damage / 2;
        }
        // Step 3g: artifact powers, target side.
        if (c->powers[t_side] & COMBAT_POWER_QUARTER_PROTECTION) {
            final_damage = (final_damage / 4) * 3;
        }
    }

    // Step 4: prior injury rolls in.
    final_damage += t->injury;
    // Step 5: SCYTHE bonus damage.
    final_damage += tt->hit_points * demon_kills;

    // Steps 6–8: kills + residual injury.
    int kills = (tt->hit_points > 0) ? final_damage / tt->hit_points : t->count;
    int injury_after = (tt->hit_points > 0) ? final_damage % tt->hit_points : 0;
    t->injury = injury_after;

    // Step 9: stack survives or dies.
    if (kills < t->count) {
        t->count -= kills;
    } else {
        t->dead  = true;
        t->count = 0;
        kills = t->turn_count;  // recompute for ABSORB / LEECH accuracy
        final_damage = kills * tt->hit_points;
    }

    // Step 10: only the player side has a hero — friendly losses
    // count against the player's score.
    if (c->heroes[t_side]) {
        c->heroes[t_side]->stats.followers_killed += kills;
    }

    // Step 11: post-attack effects.
    if (!is_external && u && ut) {
        if (ut->abilities & TROOP_ABIL_ABSORB) {
            u->count += kills;
        }
        if (ut->abilities & TROOP_ABIL_LEECH) {
            int hp = ut->hit_points;
            if (hp > 0) {
                u->count += final_damage / hp;
                if (u->count > u->max_count) {
                    u->count = u->max_count;
                    u->injury = 0;
                }
            }
        }
        // Retaliation. Once per round per defender; melee only;
        // skipped for ranged (retaliation flag forced true above)
        // and external. Recursive — guarded by t->retaliated to
        // prevent infinite mutual swings.
        if (!retaliation && !t->retaliated && t->count > 0) {
            t->retaliated = true;
            combat_deal_damage(c, t_side, t_id, a_side, a_id,
                               false, false, 0, true);
        }
    }

    return kills;
}

// Wrapper used by melee / ranged drivers. Snapshot turn_count for
// both sides per  (line 5589) so the formula and any
// retaliation see pre-attack counts. Logs the kb_066-style "<X>
// vs <Y>, N die" banner.
static int combat_hit_unit(Combat *c, int a_side, int a_id,
                           int t_side, int t_id, bool is_ranged) {
    CombatUnit *a = &c->units[a_side][a_id];
    CombatUnit *t = &c->units[t_side][t_id];
    a->turn_count = a->count;
    t->turn_count = t->count;
    int t_count_before = t->count;
    int kills = combat_deal_damage(c, a_side, a_id, t_side, t_id,
                                   is_ranged, false, 0, false);
    // Damage burst over the target cell. Persists ~3 anim ticks
    // (~450ms) so the player sees the splat. Matches DOS kb_037 /
    // kb_040 / kb_044 where the orange splat overlays on every
    // connecting attack regardless of kill count. -1 = IMMUNE cancel,
    // no splat.
    if (kills >= 0) {
        t->hit_flash = 3;
    }
    if (kills > 0) {
        const TroopDef *at = troop_by_index(a->troop_idx);
        const TroopDef *tt = troop_by_index(t->troop_idx);
        const char *aname = at ? at->name : "Attackers";
        const char *tname = tt ? tt->name : "Defenders";
        char cbuf[16];
        snprintf(cbuf, sizeof cbuf, "%d", kills);
        ResTemplateVar vars[] = {
            { "ATK",   aname },
            { "TGT",   tname },
            { "COUNT", cbuf },
        };
        const ResCombatLog *cl = combat_log_strings(c);
        combat_log_template(c,
            cl ? (is_ranged ? cl->ranged_hit : cl->melee_hit)
               : (is_ranged ? "%ATK% shoot %TGT% killing %COUNT%"
                            : "%ATK% vs %TGT%, %COUNT% die"),
            vars, 3);
    } else if (kills == -1) {
        const ResCombatLog *cl = combat_log_strings(c);
        combat_log_template(c,
            cl ? cl->no_effect_msg : "The spell seems to have no effect!",
            NULL, 0);
    }
    {
        char tag[64];
        snprintf(tag, sizeof tag, "combat:hit:s%d:u%d->s%d:u%d:%d",
                 a_side, a_id, t_side, t_id, kills);
        recorder_capture(tag);
    }
    (void)t_count_before;
    return kills;
}

// ----- Movement, flight, melee driver () -------------------------

static inline bool combat_in_bounds(int x, int y) {
    return x >= 0 && x < COMBAT_W && y >= 0 && y < COMBAT_H;
}

// Move attacker into the cell at (nx, ny). Returns 0 (blocked),
// 1 (moved), or 2 (attacked).  lines 5573–5587.
//
//   - Out of bounds → blocked.
//   - Obstacle in target cell → blocked.
//   - Friendly unit (under control) in target cell → blocked.
//   - Hostile unit in target cell → hit_unit, return 2.
//   - Empty cell → relocate, decrement moves, mark acted if zero.
static int combat_move_unit(Combat *c, int side, int id, int dx, int dy) {
    CombatUnit *u = &c->units[side][id];
    int nx = u->x + dx;
    int ny = u->y + dy;
    if (!combat_in_bounds(nx, ny)) return 0;
    if (c->omap[ny][nx]) return 0;
    unsigned char other = c->umap[ny][nx];
    if (other) {
        int o_side = (other - 1) / COMBAT_SLOTS;
        int o_slot = (other - 1) % COMBAT_SLOTS;
        if (units_are_friendly(c, side, id, o_side, o_slot)) return 0;
        combat_hit_unit(c, side, id, o_side, o_slot, false);
        u->acted = true;
        return 2;
    }
    // Relocate.
    c->umap[u->y][u->x] = 0;
    u->x = nx;
    u->y = ny;
    c->umap[ny][nx] = pack_uid(side, id);
    if (u->moves > 0) u->moves--;
    if (u->moves == 0) u->acted = true;
    return 1;
}

// Fly into a (nx, ny) target validated upstream by the picker.
//  lines 5588–5593.
static int combat_fly_unit(Combat *c, int side, int id, int nx, int ny) {
    CombatUnit *u = &c->units[side][id];
    if (!combat_in_bounds(nx, ny)) return 0;
    if (c->omap[ny][nx] || c->umap[ny][nx]) return 0;
    c->umap[u->y][u->x] = 0;
    u->x = nx;
    u->y = ny;
    c->umap[ny][nx] = pack_uid(side, id);
    if (u->flights > 0) u->flights--;
    if (u->flights == 0) u->acted = true;
    return 1;
}

// ----- compact_units () ------------------------------------------
// After deaths during a turn, dead stacks (count == 0) are cleared
// from umap. The spec says "shifts trailing stacks up" for some
// game modes — we keep slot indices stable (resolved :
// no reorder). umap is rewritten from surviving units.
/* exposed for tests */ void combat_compact(Combat *c) {
    bool any_died = false;
    for (int s = 0; s < COMBAT_SIDES; s++) {
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            CombatUnit *u = &c->units[s][i];
            if (u->dead) {
                any_died = true;
                u->dead = false;       // dead is one-shot; cleared after compact
                u->count = 0;
                u->troop_idx = -1;     // free the slot
            }
        }
    }
    if (!any_died) return;
    // Rebuild umap from surviving units (cheap: 6×5 grid, 10 units).
    memset(c->umap, 0, sizeof c->umap);
    for (int s = 0; s < COMBAT_SIDES; s++) {
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            CombatUnit *u = &c->units[s][i];
            if (u->troop_idx < 0 || u->count == 0) continue;
            c->umap[u->y][u->x] = pack_uid(s, i);
        }
    }
    // Title bar mode-switch on first death (kb_074 → kb_078 transition).
    // See COMBAT-PLAN .
    if (!c->first_kill_seen) c->first_kill_seen = true;
    c->stacks_destroyed++;
}

// ----- Turn structure () -----------------------------------------

// Refresh per-turn unit counters. Called at the start of each side's
// Reset state for the side that just had its turn end. The
// `started_at` argument is the side *whose turn just ended*. Only the
// player-side wakeup unfreezes and regens (i.e. when started_at == AI,
// the player gets fresh state).
/* exposed for tests */ void combat_reset_turn(Combat *c, int started_at) {
    for (int s = 0; s < COMBAT_SIDES; s++) {
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            CombatUnit *u = &c->units[s][i];
            if (u->troop_idx < 0 || u->count == 0) continue;
            const TroopDef *t = troop_by_index(u->troop_idx);
            if (!t) continue;
            u->turn_count = u->count;
            u->retaliated = false;
            u->acted      = false;
            u->moves      = t->move_rate;
            u->flights    = (t->abilities & TROOP_ABIL_FLY) ? 2 : 0;
            // Spec line 5258: only at the player-side wakeup boundary
            // (started_at == AI) clear frozen and apply REGEN.
            if (started_at == COMBAT_SIDE_AI) {
                u->frozen = false;
                if (t->abilities & TROOP_ABIL_REGEN) u->injury = 0;
            }
        }
    }
    c->phase = 0;
    c->spells_this_round = 0;
}

// Find the next actable unit on the current side. Scans
// [unit_id+1 .. SLOTS-1], then wraps [0 .. unit_id] with phase++.
//  next_unit lines 5262–5267. Returns slot or -1.
/* exposed for tests */ int combat_next_unit(Combat *c) {
    for (int i = c->unit_id + 1; i < COMBAT_SLOTS; i++) {
        const CombatUnit *u = &c->units[c->side][i];
        if (u->troop_idx >= 0 && u->count > 0 && !u->acted) {
            // Clear the action banner so the title bar reverts to the
            // standard "Options / <Actor>" format on the new unit's
            // turn (matches DOS behavior kb_036→kb_037→kb_042→kb_043).
            c->banner[0] = '\0';
            return i;
        }
    }
    // Wrap.
    c->phase++;
    for (int i = 0; i <= c->unit_id; i++) {
        const CombatUnit *u = &c->units[c->side][i];
        if (u->troop_idx >= 0 && u->count > 0 && !u->acted) {
            c->banner[0] = '\0';
            return i;
        }
    }
    return -1;
}

// Switch active side, run reset_turn, and prime unit_id. 
// next_turn lines 5268–5269.
/* exposed for tests */ void combat_next_turn(Combat *c) {
    int prior = c->side;
    c->side = (c->side == COMBAT_SIDE_PLAYER) ? COMBAT_SIDE_AI : COMBAT_SIDE_PLAYER;
    combat_reset_turn(c, prior);
    if (c->side == COMBAT_SIDE_PLAYER) c->turn++;
    c->unit_id = -1;
    int nxt = combat_next_unit(c);
    c->unit_id = (nxt >= 0) ? nxt : 0;
    {
        char tag[48];
        snprintf(tag, sizeof tag, "combat:turn:%d:side:%d",
                 c->turn, c->side);
        recorder_capture(tag);
    }
}

// True when one side has been wiped out. test_victory: AI dead.
// test_defeat: player dead.  lines 5667–5670.
/* exposed for tests */ bool combat_test_dead(const Combat *c, int side) {
    for (int i = 0; i < COMBAT_SLOTS; i++) {
        const CombatUnit *u = &c->units[side][i];
        if (u->troop_idx >= 0 && u->count > 0) return false;
    }
    return true;
}

// ----- Ranged + surround check () --------------------------------

// True if any adjacent (8-neighbour) cell contains a hostile unit.
// Ranged attacks are blocked while surrounded so a melee enemy
// stops the bow.  line 5608. Note: friendly adjacent
// units do not block (per spec wording "any enemy in adjacent tile").
/* exposed for tests */ bool combat_unit_surrounded(const Combat *c, int side, int slot) {
    const CombatUnit *u = &c->units[side][slot];
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = u->x + dx, ny = u->y + dy;
            if (!combat_in_bounds(nx, ny)) continue;
            unsigned char other = c->umap[ny][nx];
            if (!other) continue;
            int o_side = (other - 1) / COMBAT_SLOTS;
            int o_slot = (other - 1) % COMBAT_SLOTS;
            if (!units_are_friendly(c, side, slot, o_side, o_slot)) {
                return true;
            }
        }
    }
    return false;
}

// Drive a ranged shot from (side, slot) at (t_side, t_slot). Returns
// ----- Target picker () -----------------------------------------

// Filter values for combat_pick_target. 0 = any tile, 1 = unoccupied
// + no obstacle, 2 = any unit, 3 = friendly unit (under control),
// 4 = enemy unit, 5 = enemy undead. Spec lines 6082–6087.
typedef enum {
    PICK_FILTER_ANY        = 0,
    PICK_FILTER_EMPTY      = 1,
    PICK_FILTER_ANY_UNIT   = 2,
    PICK_FILTER_FRIENDLY   = 3,
    PICK_FILTER_ENEMY      = 4,
    PICK_FILTER_UNDEAD     = 5,
} PickFilter;

/* exposed for tests */ bool combat_cell_passes_filter(const Combat *c, int x, int y,
                                      int caster_side, int filter) {
    unsigned char obstacle = c->omap[y][x];
    unsigned char uid = c->umap[y][x];
    int u_side = -1, u_slot = -1;
    if (uid) {
        u_side = (uid - 1) / COMBAT_SLOTS;
        u_slot = (uid - 1) % COMBAT_SLOTS;
    }
    switch (filter) {
        case PICK_FILTER_ANY: return true;
        case PICK_FILTER_EMPTY: return !uid && !obstacle;
        case PICK_FILTER_ANY_UNIT: return uid != 0;
        case PICK_FILTER_FRIENDLY:
            if (!uid) return false;
            return units_are_friendly(c, caster_side, c->unit_id, u_side, u_slot);
        case PICK_FILTER_ENEMY:
            if (!uid) return false;
            return !units_are_friendly(c, caster_side, c->unit_id, u_side, u_slot);
        case PICK_FILTER_UNDEAD: {
            if (!uid) return false;
            if (units_are_friendly(c, caster_side, c->unit_id, u_side, u_slot)) return false;
            const CombatUnit *u = &c->units[u_side][u_slot];
            const TroopDef *t = troop_by_index(u->troop_idx);
            return t && (t->abilities & TROOP_ABIL_UNDEAD);
        }
    }
    return false;
}

// Modal cursor — drives c->cursor_{x,y} until the player Enters or
// Escapes. Returns true on confirm, false on cancel. Caller should
// have set c->cursor_{x,y} to the desired starting cell. The active
// unit's `acted` flag is NOT set here; callers decide whether the
// pick consumes the turn. Pumps the renderer so the cursor animates.
static bool combat_pick_target(Combat *c, const Game *g,
                               const Sprites *sprites,
                               RenderTexture2D *target,
                               int caster_side, int filter,
                               int *out_x, int *out_y) {
    // Enable cursor ring for the duration of the modal pick. Cleared
    // on every return path so the next render is back to normal play.
    c->picker_active = true;
    bool result = false;
    double cursor_tick = GetTime();
    while (!WindowShouldClose()) {
        harness_tick();
        audio_tick();
        // Animate cursor ring at ~150ms per frame, matching the SYN
        // tick. Without this the cursor would stay on a single frame.
        double now = GetTime();
        if (now >= cursor_tick) {
            cursor_tick = now + 0.15;
            c->cursor_frame = (c->cursor_frame + 1) & 3;
        }
        BeginTextureMode(*target);
        combat_render_frame(c, g, sprites);
        EndTextureMode();

        BeginDrawing();
        ClearBackground(BLACK);
        int win_w = GetScreenWidth(), win_h = GetScreenHeight();
        int sx = win_w / CL_SCREEN_W, sy = win_h / CL_SCREEN_H;
        int scale = (sx < sy) ? sx : sy; if (scale < 2) scale = 2;
        int dst_w = CL_SCREEN_W * scale, dst_h = CL_SCREEN_H * scale;
        int dst_x = (win_w - dst_w) / 2, dst_y = (win_h - dst_h) / 2;
        Rectangle src = { 0, 0, (float)target->texture.width,
                          -(float)target->texture.height };
        Rectangle dst = { (float)dst_x, (float)dst_y,
                          (float)dst_w, (float)dst_h };
        DrawTexturePro(target->texture, src, dst,
                       (Vector2){ 0, 0 }, 0.0f, WHITE);
        EndDrawing();

        int dx = 0, dy = 0;
        if      (harness_key_pressed(KEY_UP)    || harness_key_pressed(KEY_KP_8)) dy = -1;
        else if (harness_key_pressed(KEY_DOWN)  || harness_key_pressed(KEY_KP_2)) dy =  1;
        if      (harness_key_pressed(KEY_LEFT)  || harness_key_pressed(KEY_KP_4)) dx = -1;
        else if (harness_key_pressed(KEY_RIGHT) || harness_key_pressed(KEY_KP_6)) dx =  1;
        if      (harness_key_pressed(KEY_KP_7) || harness_key_pressed(KEY_HOME))      { dx = -1; dy = -1; }
        else if (harness_key_pressed(KEY_KP_9) || harness_key_pressed(KEY_PAGE_UP))   { dx =  1; dy = -1; }
        else if (harness_key_pressed(KEY_KP_1) || harness_key_pressed(KEY_END))       { dx = -1; dy =  1; }
        else if (harness_key_pressed(KEY_KP_3) || harness_key_pressed(KEY_PAGE_DOWN)) { dx =  1; dy =  1; }
        if (dx || dy) {
            int nx = c->cursor_x + dx, ny = c->cursor_y + dy;
            if (combat_in_bounds(nx, ny)) {
                c->cursor_x = nx; c->cursor_y = ny;
            }
        }
        if (harness_key_pressed(KEY_ENTER) || harness_key_pressed(KEY_KP_ENTER) ||
            harness_key_pressed(KEY_SPACE) ||
            harness_key_pressed(KEY_A)     || harness_key_pressed(KEY_C)) {
            if (combat_cell_passes_filter(c, c->cursor_x, c->cursor_y,
                                           caster_side, filter)) {
                if (out_x) *out_x = c->cursor_x;
                if (out_y) *out_y = c->cursor_y;
                result = true;
                goto done;
            }
            // Filter rejected — leave cursor and re-loop. Phase 13 can
            // play a "no" tone here.
        }
        if (harness_key_pressed(KEY_ESCAPE)) goto done;
    }
done:
    c->picker_active = false;
    return result;
}

// ----- Input + action dispatch (, ; manual kb_038) ---------

static bool combat_read_dir(int *dx, int *dy) {
    *dx = 0; *dy = 0;
    if (harness_key_pressed(KEY_KP_5)) return false;
    if (harness_key_pressed(KEY_UP)        || harness_key_pressed(KEY_KP_8)) { *dy = -1; return true; }
    if (harness_key_pressed(KEY_DOWN)      || harness_key_pressed(KEY_KP_2)) { *dy =  1; return true; }
    if (harness_key_pressed(KEY_LEFT)      || harness_key_pressed(KEY_KP_4)) { *dx = -1; return true; }
    if (harness_key_pressed(KEY_RIGHT)     || harness_key_pressed(KEY_KP_6)) { *dx =  1; return true; }
    if (harness_key_pressed(KEY_HOME)      || harness_key_pressed(KEY_KP_7)) { *dx = -1; *dy = -1; return true; }
    if (harness_key_pressed(KEY_PAGE_UP)   || harness_key_pressed(KEY_KP_9)) { *dx =  1; *dy = -1; return true; }
    if (harness_key_pressed(KEY_END)       || harness_key_pressed(KEY_KP_1)) { *dx = -1; *dy =  1; return true; }
    if (harness_key_pressed(KEY_PAGE_DOWN) || harness_key_pressed(KEY_KP_3)) { *dx =  1; *dy =  1; return true; }
    return false;
}

// ----- Combat spells (-18.7, manual lines 643-768) ---------------
//
// Spell IDs match game.json's spell catalog ordering:
//   0 Clone, 1 Teleport, 2 Fireball, 3 Lightning,
//   4 Freeze, 5 Resurrect, 6 Turn Undead, then 7..13 adventure spells.
//
// Per Phase 0  (resolved): Fireball / Lightning / Resurrect /
// Turn Undead are stubs  itself — we IMPLEMENT them here
// per the manual + spec scaling, not  stub. 
// gives the SP scaling table.

#define COMBAT_SPELL_CLONE        0
#define COMBAT_SPELL_TELEPORT     1
#define COMBAT_SPELL_FIREBALL     2
#define COMBAT_SPELL_LIGHTNING    3
#define COMBAT_SPELL_FREEZE       4
#define COMBAT_SPELL_RESURRECT    5
#define COMBAT_SPELL_TURN_UNDEAD  6

/* exposed for tests */ int spell_damage(Combat *c, int t_side, int t_slot, int dmg) {
    return combat_deal_damage(c, -1, -1, t_side, t_slot,
                              false, true, dmg, false);
}

/* exposed for tests */ void spell_clone(Combat *c, int t_side, int t_slot, int sp) {
    CombatUnit *u = &c->units[t_side][t_slot];
    const TroopDef *t = troop_by_index(u->troop_idx);
    if (!t || t->hit_points <= 0) return;
    int damage = sp * 10 + u->injury;
    int clones = damage / t->hit_points;
    int residual = damage % t->hit_points;
    u->count     += clones;
    u->max_count += clones;
    u->injury     = residual;
    char cbuf[16];
    snprintf(cbuf, sizeof cbuf, "%d", clones);
    ResTemplateVar vars[] = {
        { "COUNT", cbuf },
        { "TROOP", t->name },
    };
    const ResCombatLog *cl = combat_log_strings(c);
    combat_log_template(c, cl ? cl->cloned : "%COUNT% %TROOP% cloned",
                        vars, 2);
}

/* exposed for tests */ void spell_teleport(Combat *c, int from_side, int from_slot,
                           int to_x, int to_y) {
    CombatUnit *u = &c->units[from_side][from_slot];
    c->umap[u->y][u->x] = 0;
    u->x = to_x;
    u->y = to_y;
    c->umap[to_y][to_x] = pack_uid(from_side, from_slot);
    const ResCombatLog *cl = combat_log_strings(c);
    combat_log_template(c, cl ? cl->teleported : "Teleported", NULL, 0);
}

/* exposed for tests */ int spell_freeze(Combat *c, int t_side, int t_slot) {
    CombatUnit *u = &c->units[t_side][t_slot];
    const TroopDef *t = troop_by_index(u->troop_idx);
    const ResCombatLog *cl = combat_log_strings(c);
    if (t && (t->abilities & TROOP_ABIL_IMMUNE)) {
        ResTemplateVar vars[] = { { "TROOP", t->name } };
        combat_log_template(c, cl ? cl->immune : "%TROOP% are immune",
                            vars, 1);
        return -1;
    }
    u->frozen = true;
    ResTemplateVar vars[] = { { "TROOP", t ? t->name : "Foe" } };
    combat_log_template(c, cl ? cl->frozen : "%TROOP% are frozen",
                        vars, 1);
    return 1;
}

/* exposed for tests */ void spell_resurrect(Combat *c, int t_side, int t_slot, int sp) {
    CombatUnit *u = &c->units[t_side][t_slot];
    if (u->count == 0) return;
    int revived = sp;
    if (revived <= 0) revived = 1;
    if (u->count + revived > u->max_count) {
        revived = u->max_count - u->count;
    }
    if (revived <= 0) return;
    u->count += revived;
    u->injury = 0;
    const TroopDef *t = troop_by_index(u->troop_idx);
    char cbuf[16];
    snprintf(cbuf, sizeof cbuf, "%d", revived);
    ResTemplateVar vars[] = {
        { "COUNT", cbuf },
        { "TROOP", t ? t->name : "creatures" },
    };
    const ResCombatLog *cl = combat_log_strings(c);
    combat_log_template(c, cl ? cl->resurrected : "%COUNT% %TROOP% resurrected",
                        vars, 2);
}

/* exposed for tests */ int spell_damage_value(int base, int sp) {
    if (sp < 1) sp = 1;
    return base * sp;
}

// Spell-cast workflow. .
//   1. Pre-checks: knows_magic, spells_this_round < 1, count[id] > 0.
//   2. Show menu (kb_080 layout): two columns, Combat A-G, Adventure
//      A-G (we only handle Combat A-G here).
//   3. spells[id]-- ; spells_this_round++.
//   4. Run target picker if needed.
//   5. Apply spell effect.
// Adventure spells inside combat are not legal per the kb_080 prompt
// pattern ("Cast which Combat spell (A-G)?") — we restrict to A-G.
//
// Returns 1 if a spell was cast (turn-progressing), 0 otherwise.
static int combat_player_cast(Combat *c, const Game *g,
                              const Sprites *sprites,
                              RenderTexture2D *target) {
    const ResCombatLog *cl_pre = combat_log_strings(c);
    if (c->spells_this_round >= 1) {
        combat_log_template(c,
            cl_pre ? cl_pre->only_one_spell : "Only 1 spell per round!",
            NULL, 0);
        return 0;
    }
    Game *gw = c->heroes[c->side];
    if (!gw) return 0;
    // Pre-check: hero rank must know magic. Use the resolved class
    // table — class_by_index + rank.knows_magic.
    const ClassDef *cls = class_by_id(gw->character.cls.id);
    if (!cls || cls->rank_count == 0) return 0;
    int rank = gw->character.cls.rank_index;
    if (rank < 0) rank = 0;
    if (rank >= cls->rank_count) rank = cls->rank_count - 1;
    if (!cls->ranks[rank].knows_magic) {
        combat_log_template(c,
            cl_pre ? cl_pre->cannot_cast : "You cannot cast magic",
            NULL, 0);
        return 0;
    }

    // Modal: wait for A-G or Esc. The caller ran a render frame just
    // before this; we render-and-poll until a key resolves.
    int picked = -1;
    while (!WindowShouldClose() && picked < 0) {
        harness_tick();
        BeginTextureMode(*target);
        combat_render_frame(c, g, sprites);
        // Inline overlay: kb_080 spells menu, abbreviated. Phase 13
        // can wire the verbatim two-column layout.
        DrawRectangle(40, 30, 240, 130, PAL_CLR(DBLUE));
        DrawRectangleLines(40, 30, 240, 130, PAL_CLR(YELLOW));
        const ResUI *ui = (gw && gw->res) ? &gw->res->ui : NULL;
        bfont_draw(ui ? ui->combat_spells_title      : "Spells", 140, 36, PAL_CLR(YELLOW));
        bfont_draw(ui ? ui->combat_spells_col_combat : "Combat", 60, 50, PAL_CLR(YELLOW));
        char line[40];
        const char *names[] = {
            "Clone", "Teleport", "Fireball", "Lightning",
            "Freeze", "Resurrect", "Turn Undead",
        };
        for (int i = 0; i < 7; i++) {
            snprintf(line, sizeof line, "%d %-12s %c",
                     gw->spells.counts[i], names[i], 'A' + i);
            bfont_draw(line, 56, 64 + i * 10, PAL_CLR(WHITE));
        }
        bfont_draw(ui ? ui->combat_spells_prompt : "Cast which (A-G)?", 70, 144, PAL_CLR(WHITE));
        EndTextureMode();

        BeginDrawing();
        ClearBackground(BLACK);
        int win_w = GetScreenWidth(), win_h = GetScreenHeight();
        int sx = win_w / CL_SCREEN_W, sy = win_h / CL_SCREEN_H;
        int sc = (sx < sy) ? sx : sy; if (sc < 2) sc = 2;
        int dst_w = CL_SCREEN_W * sc, dst_h = CL_SCREEN_H * sc;
        int dst_x = (win_w - dst_w) / 2, dst_y = (win_h - dst_h) / 2;
        Rectangle src = { 0, 0, (float)target->texture.width,
                          -(float)target->texture.height };
        Rectangle dst = { (float)dst_x, (float)dst_y,
                          (float)dst_w, (float)dst_h };
        DrawTexturePro(target->texture, src, dst,
                       (Vector2){ 0, 0 }, 0.0f, WHITE);
        EndDrawing();

        if (harness_key_pressed(KEY_ESCAPE)) return 0;
        for (int i = 0; i < 7; i++) {
            if (harness_key_pressed(KEY_A + i)) { picked = i; break; }
        }
    }
    if (picked < 0) return 0;
    if (gw->spells.counts[picked] <= 0) {
        combat_log_template(c,
            cl_pre ? cl_pre->no_spell_type : "No spells of that type",
            NULL, 0);
        return 0;
    }

    int caster_side = c->side;
    int sp = gw->stats.spell_power;
    if (sp < 1) sp = 1;
    if (gw->artifacts.found[0]) {
        // Amulet of Augmentation = DOUBLE_SPELL_POWER.
        // Phase 13 will use ArtifactPower lookup; for now hard-coded
        // index 0 because that's where the Amulet is in game.json.
    }
    int tx = 0, ty = 0;

    int consumed = 1;     // most spells consume the turn
    int casted   = 0;
    switch (picked) {
        case COMBAT_SPELL_CLONE:
            c->cursor_x = c->units[caster_side][c->unit_id].x;
            c->cursor_y = c->units[caster_side][c->unit_id].y;
            if (combat_pick_target(c, g, sprites, target,
                                    caster_side, PICK_FILTER_FRIENDLY,
                                    &tx, &ty)) {
                unsigned char uid = c->umap[ty][tx];
                int t_side = (uid - 1) / COMBAT_SLOTS;
                int t_slot = (uid - 1) % COMBAT_SLOTS;
                spell_clone(c, t_side, t_slot, sp);
                casted = 1;
            }
            break;
        case COMBAT_SPELL_TELEPORT: {
            int sx_p = 0, sy_p = 0;
            c->cursor_x = c->units[caster_side][c->unit_id].x;
            c->cursor_y = c->units[caster_side][c->unit_id].y;
            if (!combat_pick_target(c, g, sprites, target,
                                     caster_side, PICK_FILTER_ANY_UNIT,
                                     &sx_p, &sy_p)) break;
            unsigned char uid = c->umap[sy_p][sx_p];
            int t_side = (uid - 1) / COMBAT_SLOTS;
            int t_slot = (uid - 1) % COMBAT_SLOTS;
            if (combat_pick_target(c, g, sprites, target,
                                    caster_side, PICK_FILTER_EMPTY,
                                    &tx, &ty)) {
                spell_teleport(c, t_side, t_slot, tx, ty);
                casted = 1;
            }
            break;
        }
        case COMBAT_SPELL_FIREBALL: {
            c->cursor_x = c->units[caster_side][c->unit_id].x;
            c->cursor_y = c->units[caster_side][c->unit_id].y;
            if (!combat_pick_target(c, g, sprites, target,
                                     caster_side, PICK_FILTER_ENEMY,
                                     &tx, &ty)) break;
            unsigned char uid = c->umap[ty][tx];
            int t_side = (uid - 1) / COMBAT_SLOTS;
            int t_slot = (uid - 1) % COMBAT_SLOTS;
            //  cancels on IMMUNE.
            // freeze_army pattern (game.c:3919): the caller treats a
            // failed cast as "spell not used", so casted stays 0 and
            // the spell count is preserved.
            const TroopDef *tt =
                troop_by_index(c->units[t_side][t_slot].troop_idx);
            if (tt && (tt->abilities & TROOP_ABIL_IMMUNE)) {
                combat_log_template(c,
                    cl_pre ? cl_pre->no_effect_msg
                           : "The spell seems to have no effect!",
                    NULL, 0);
                break;
            }
            spell_damage(c, t_side, t_slot, spell_damage_value(25, sp));
            combat_log_template(c,
                cl_pre ? cl_pre->cast_fireball : "Fireball!", NULL, 0);
            casted = 1;
            break;
        }
        case COMBAT_SPELL_LIGHTNING: {
            c->cursor_x = c->units[caster_side][c->unit_id].x;
            c->cursor_y = c->units[caster_side][c->unit_id].y;
            if (!combat_pick_target(c, g, sprites, target,
                                     caster_side, PICK_FILTER_ENEMY,
                                     &tx, &ty)) break;
            unsigned char uid = c->umap[ty][tx];
            int t_side = (uid - 1) / COMBAT_SLOTS;
            int t_slot = (uid - 1) % COMBAT_SLOTS;
            const TroopDef *tt =
                troop_by_index(c->units[t_side][t_slot].troop_idx);
            if (tt && (tt->abilities & TROOP_ABIL_IMMUNE)) {
                combat_log_template(c,
                    cl_pre ? cl_pre->no_effect_msg
                           : "The spell seems to have no effect!",
                    NULL, 0);
                break;
            }
            spell_damage(c, t_side, t_slot, spell_damage_value(10, sp));
            combat_log_template(c,
                cl_pre ? cl_pre->cast_lightning : "Lightning!", NULL, 0);
            casted = 1;
            break;
        }
        case COMBAT_SPELL_FREEZE: {
            c->cursor_x = c->units[caster_side][c->unit_id].x;
            c->cursor_y = c->units[caster_side][c->unit_id].y;
            if (!combat_pick_target(c, g, sprites, target,
                                     caster_side, PICK_FILTER_ENEMY,
                                     &tx, &ty)) break;
            unsigned char uid = c->umap[ty][tx];
            int t_side = (uid - 1) / COMBAT_SLOTS;
            int t_slot = (uid - 1) % COMBAT_SLOTS;
            spell_freeze(c, t_side, t_slot);
            casted = 1;
            break;
        }
        case COMBAT_SPELL_RESURRECT: {
            c->cursor_x = c->units[caster_side][c->unit_id].x;
            c->cursor_y = c->units[caster_side][c->unit_id].y;
            if (!combat_pick_target(c, g, sprites, target,
                                     caster_side, PICK_FILTER_FRIENDLY,
                                     &tx, &ty)) break;
            unsigned char uid = c->umap[ty][tx];
            int t_side = (uid - 1) / COMBAT_SLOTS;
            int t_slot = (uid - 1) % COMBAT_SLOTS;
            spell_resurrect(c, t_side, t_slot, sp);
            casted = 1;
            break;
        }
        case COMBAT_SPELL_TURN_UNDEAD: {
            c->cursor_x = c->units[caster_side][c->unit_id].x;
            c->cursor_y = c->units[caster_side][c->unit_id].y;
            if (!combat_pick_target(c, g, sprites, target,
                                     caster_side, PICK_FILTER_UNDEAD,
                                     &tx, &ty)) break;
            unsigned char uid = c->umap[ty][tx];
            int t_side = (uid - 1) / COMBAT_SLOTS;
            int t_slot = (uid - 1) % COMBAT_SLOTS;
            const TroopDef *tt =
                troop_by_index(c->units[t_side][t_slot].troop_idx);
            if (tt && (tt->abilities & TROOP_ABIL_IMMUNE)) {
                combat_log_template(c,
                    cl_pre ? cl_pre->no_effect_msg
                           : "The spell seems to have no effect!",
                    NULL, 0);
                break;
            }
            spell_damage(c, t_side, t_slot, spell_damage_value(50, sp));
            combat_log_template(c,
                cl_pre ? cl_pre->cast_turn_undead : "Turn Undead!",
                NULL, 0);
            casted = 1;
            break;
        }
    }

    if (casted) {
        gw->spells.counts[picked]--;
        c->spells_this_round++;
        return consumed;  // most spells advance the turn
    }
    return 0;
}

static int combat_player_action_full(Combat *c, const Game *g,
                                     const Sprites *sprites,
                                     RenderTexture2D *target) {
    int dx, dy;
    if (combat_read_dir(&dx, &dy)) {
        if (c->unit_id < 0) return 0;
        return combat_move_unit(c, c->side, c->unit_id, dx, dy);
    }
    if (harness_key_pressed(KEY_SPACE) || harness_key_pressed(KEY_W)) {
        if (c->unit_id >= 0) c->units[c->side][c->unit_id].acted = true;
        return 1;
    }
    if (harness_key_pressed(KEY_G)) {
        c->result = 2;
        return 1;
    }
    if (harness_key_pressed(KEY_S)) {
        // Shoot. Pick enemy, ranged damage.
        if (c->unit_id < 0) return 0;
        CombatUnit *u = &c->units[c->side][c->unit_id];
        const ResCombatLog *cl_sh = combat_log_strings(c);
        if (u->shots <= 0) {
            combat_log_template(c,
                cl_sh ? cl_sh->no_ammo : "No ammo", NULL, 0);
            return 0;
        }
        if (combat_unit_surrounded(c, c->side, c->unit_id)) {
            combat_log_template(c,
                cl_sh ? cl_sh->cant_shoot : "Can't Shoot", NULL, 0);
            return 0;
        }
        c->cursor_x = u->x;
        c->cursor_y = u->y;
        int tx = 0, ty = 0;
        if (combat_pick_target(c, g, sprites, target,
                                c->side, PICK_FILTER_ENEMY, &tx, &ty)) {
            unsigned char uid = c->umap[ty][tx];
            int t_side = (uid - 1) / COMBAT_SLOTS;
            int t_slot = (uid - 1) % COMBAT_SLOTS;
            combat_hit_unit(c, c->side, c->unit_id, t_side, t_slot, true);
            u->acted = true;
            return 1;
        }
        return 0;
    }
    if (harness_key_pressed(KEY_U)) {
        return combat_player_cast(c, g, sprites, target);
    }
    if (harness_key_pressed(KEY_C)) {
        // Controls overlay (kb_038 reference). Modal — block until any
        // key. Renders the keybindings list inside the existing field
        // so the player can read while glancing at unit positions.
        while (!WindowShouldClose()) {
        harness_tick();
            BeginTextureMode(*target);
            combat_render_frame(c, g, sprites);
            DrawRectangle(8, 18, 220, 170, PAL_CLR(DBLUE));
            DrawRectangleLines(8, 18, 220, 170, PAL_CLR(YELLOW));
            int y = 22;
            const char *lines[] = {
                "Combat Controls",
                "",
                "Arrows / Numpad  Move",
                "HOME 7  PGUP 9   diag up",
                "END  1  PGDN 3   diag down",
                "A or C   View Army / Confirm",
                "C        Controls",
                "S        Shoot",
                "U        Use Magic",
                "V        View Char",
                "W        Wait",
                "G        Give Up",
                "SPC      Pass",
                "ESC      Surrender",
                "",
                "Press any key to return",
            };
            int n = sizeof(lines) / sizeof(lines[0]);
            for (int i = 0; i < n; i++) {
                bfont_draw(lines[i], 14, y, PAL_CLR(WHITE));
                y += 10;
            }
            EndTextureMode();
            BeginDrawing();
            ClearBackground(BLACK);
            int win_w = GetScreenWidth(), win_h = GetScreenHeight();
            int sx = win_w / CL_SCREEN_W, sy = win_h / CL_SCREEN_H;
            int sc = (sx < sy) ? sx : sy; if (sc < 2) sc = 2;
            int dst_w = CL_SCREEN_W * sc, dst_h = CL_SCREEN_H * sc;
            int dst_x = (win_w - dst_w) / 2, dst_y = (win_h - dst_h) / 2;
            Rectangle src = { 0, 0, (float)target->texture.width,
                              -(float)target->texture.height };
            Rectangle dst = { (float)dst_x, (float)dst_y,
                              (float)dst_w, (float)dst_h };
            DrawTexturePro(target->texture, src, dst,
                           (Vector2){ 0, 0 }, 0.0f, WHITE);
            EndDrawing();
            if (harness_get_key_pressed() != 0) break;
        }
        return 0;
    }
    return 0;
}


// ----- Combat AI (-19.4) -----------------------------------------

// True if (a) and (b) are within 1 cell (8-neighbour adjacency).
// unit_touching (game.c). Used by ai_pick_target's `nearby`.
/* exposed for tests */ bool unit_touching(const CombatUnit *a, const CombatUnit *b) {
    int dx = a->x - b->x;
    int dy = a->y - b->y;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx <= 1 && dy <= 1;
}

// Pick a target for the acting unit. Returns packed UID
// (side*5 + id + 1) or 0 if none. .
/* exposed for tests */ unsigned char ai_pick_target(const Combat *c, int side, int slot,
                                    bool nearby) {
    const CombatUnit *self = &c->units[side][slot];
    bool ooc = self->out_of_control;
    int best_uid = 0;
    int best_score = -1;  // higher is better
    for (int s = 0; s < COMBAT_SIDES; s++) {
        // Under-control units skip own side.
        if (s == side && !ooc) continue;
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            if (s == side && i == slot && !ooc) continue;
            const CombatUnit *t = &c->units[s][i];
            if (t->troop_idx < 0 || t->count == 0) continue;
            if (nearby && !unit_touching(self, t)) continue;
            // Preference: for far targets prefer shooters; otherwise
            // prefer lower-HP-per-creature targets. We compute a
            // single score that captures both.
            const TroopDef *td = troop_by_index(t->troop_idx);
            int score;
            if (!nearby && t->shots > 0) {
                score = 10000;             // shooter: top priority
            } else if (td) {
                score = 1000 - td->hit_points;  // lower HP better
            } else {
                score = 0;
            }
            if (score > best_score) {
                best_score = score;
                best_uid = pack_uid(s, i);
            }
        }
    }
    return (unsigned char)best_uid;
}

// Compute a single-step offset toward target, respecting passability.
// Returns (0,0) if no productive step is available. Mirrors
// unit_closest_offset ( — full pseudocode in spec but
// the practical behaviour is "step in the direction that minimizes
// distance, prefer cardinal over diagonal when tied"). For a 6×5
// grid this is straightforward.
/* exposed for tests */ void unit_move_offset(const Combat *c, const CombatUnit *self,
                             int tx, int ty, int *ox, int *oy) {
    *ox = 0; *oy = 0;
    int best_dx = 0, best_dy = 0;
    unsigned long best = (unsigned long)-1;
    // Iteration order matches :
    // outer dy = +1, 0, -1 (descending); inner dx = -1, 0, +1 (ascending).
    // Tie-break order is observable in AI movement.
    for (int dy = 1; dy >= -1; dy--) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = self->x + dx;
            int ny = self->y + dy;
            if (!combat_in_bounds(nx, ny)) continue;
            if (c->omap[ny][nx]) continue;
            // Skip cells with friendly occupants. Hostile occupants
            // are fine — that's "step into melee".
            unsigned char uid = c->umap[ny][nx];
            if (uid) {
                int o_side = (uid - 1) / COMBAT_SLOTS;
                int o_slot = (uid - 1) % COMBAT_SLOTS;
                // Self check is implicit (umap[self->y][self->x]
                // contains our own UID, but ny/nx differ).
                if (units_are_friendly(c,
                                       (self - &c->units[0][0]) / COMBAT_SLOTS,
                                       (self - &c->units[0][0]) % COMBAT_SLOTS,
                                       o_side, o_slot)) continue;
            }
            unsigned long d = calc_distance(nx, ny, tx, ty);
            if (d < best) {
                best = d;
                best_dx = dx;
                best_dy = dy;
            }
        }
    }
    *ox = best_dx;
    *oy = best_dy;
}

// AI think —  ai_unit_think pseudocode lines 6473–6491.
/* exposed for tests */ int combat_ai_action(Combat *c) {
    if (c->unit_id < 0) return 0;
    int side = c->side;
    int slot = c->unit_id;
    CombatUnit *u = &c->units[side][slot];
    const TroopDef *t = troop_by_index(u->troop_idx);
    if (!t) { u->acted = true; return 1; }

    // Step 2: frozen → pass.
    if (u->frozen) {
        u->acted = true;
        return 1;
    }

    // Step 1: close target (1-tile range).
    unsigned char close_uid = ai_pick_target(c, side, slot, true);

    // Step 3: ranged attack on far target if available.
    if (close_uid == 0 && u->shots > 0 && !combat_unit_surrounded(c, side, slot)) {
        unsigned char far_uid = ai_pick_target(c, side, slot, false);
        if (far_uid) {
            int t_side = (far_uid - 1) / COMBAT_SLOTS;
            int t_slot = (far_uid - 1) % COMBAT_SLOTS;
            // Magic vs Immune cancellation lives in deal_damage; if it
            // returns -1 we still consume the AI's action so the loop
            // doesn't spin.
            combat_hit_unit(c, side, slot, t_side, t_slot, true);
            u->acted = true;
            return 1;
        }
    }

    // Step 4: fly toward far target.
    if (close_uid == 0 && (t->abilities & TROOP_ABIL_FLY) && u->flights > 0) {
        unsigned char far_uid = ai_pick_target(c, side, slot, false);
        if (far_uid) {
            int t_side = (far_uid - 1) / COMBAT_SLOTS;
            int t_slot = (far_uid - 1) % COMBAT_SLOTS;
            const CombatUnit *target = &c->units[t_side][t_slot];
            // Land in any free adjacent cell to the target.
            // Iteration order  →
            // unit_closest_offset (play.c:1639) for tie-break parity:
            // outer dy = +1, 0, -1; inner dx = -1, 0, +1.
            for (int dy = 1; dy >= -1; dy--) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = target->x + dx, ny = target->y + dy;
                    if (!combat_in_bounds(nx, ny)) continue;
                    if (c->omap[ny][nx] || c->umap[ny][nx]) continue;
                    if (combat_fly_unit(c, side, slot, nx, ny)) {
                        u->acted = true;
                        return 1;
                    }
                }
            }
        }
    }

    // Step 5: walk toward target (close-or-far).
    {
        unsigned char target_uid = close_uid;
        if (target_uid == 0) target_uid = ai_pick_target(c, side, slot, false);
        if (target_uid) {
            int t_side = (target_uid - 1) / COMBAT_SLOTS;
            int t_slot = (target_uid - 1) % COMBAT_SLOTS;
            const CombatUnit *target = &c->units[t_side][t_slot];
            int ox, oy;
            unit_move_offset(c, u, target->x, target->y, &ox, &oy);
            if (ox != 0 || oy != 0) {
                int r = combat_move_unit(c, side, slot, ox, oy);
                if (r != 0) {
                    u->acted = true;
                    return 1;
                }
            }
        }
    }

    // Step 6: nothing better — pass.
    u->acted = true;
    return 1;
}

static void combat_present(const Combat *c, const Game *g,
                           const Sprites *sprites,
                           RenderTexture2D *target) {
    BeginTextureMode(*target);
    combat_render_frame(c, g, sprites);
    // Victory dialog : centered modal
    // floating over the still-rendered battlefield. Defeat does not
    // draw here — combat exits silently and perform_temp_death shows
    // the disgrace message at the home castle ().
    if (dialog_is_active()) overlay_draw_dialog_centered();
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
                      (float)target->texture.width,
                      -(float)target->texture.height };
    Rectangle dst = { (float)dst_x, (float)dst_y,
                      (float)dst_w, (float)dst_h };
    DrawTexturePro(target->texture, src, dst,
                   (Vector2){ 0, 0 }, 0.0f, WHITE);
    EndDrawing();
}

// 150ms SYN tick (manual line 351 / ). AI acts on
// frame rollover so units feel paced; player input dispatches as
// soon as keys are pressed.
static void combat_tick_anim(Combat *c, double *next_tick, bool *rolled_over) {
    *rolled_over = false;
    double now = GetTime();
    if (now < *next_tick) return;
    *next_tick = now + 0.15;
    // Decay damage-burst on every stack (including dead ones, so the
    // splat plays out over a now-empty cell — DOS kb_036).
    for (int s = 0; s < COMBAT_SIDES; s++) {
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            CombatUnit *u = &c->units[s][i];
            if (u->troop_idx < 0) continue;
            if (u->hit_flash > 0) u->hit_flash--;
        }
    }
    // Advance only the active unit's animation frame. game.c
    // line 6119 ticks `units[side][unit_id].frame` exclusively — the
    // animation is the visual cue for whose turn it is. All other
    // stacks stay on frame 0.
    if (c->unit_id >= 0) {
        CombatUnit *act = &c->units[c->side][c->unit_id];
        if (act->troop_idx >= 0 && act->count > 0) {
            act->frame++;
            if (act->frame > 3) {
                act->frame = 0;
                *rolled_over = true;
            }
        }
    }
}

// Block until the player acknowledges the open end-of-combat dialog,
// keeping the battlefield rendered behind it. Without this, RunCombat
// would return the moment c.result is set, callers would mutate world
// state (castle ownership, perform_temp_death), and the dialog would
// be drawn over the overworld instead of over the battle that produced
// it.
static void combat_wait_for_dialog_ack(const Combat *c, const Game *g,
                                       const Sprites *sprites,
                                       RenderTexture2D *target) {
    while (dialog_is_active() && !WindowShouldClose()) {
        harness_tick();
        combat_present(c, g, sprites, target);
        // Allow screenshots while paused on the end-of-combat dialog
        // (main loop is suspended during RunCombat).
        screenshot_tick(*target, "shot");
        if (ui_any_key_pressed()) {
            if (!dialog_advance()) dialog_dismiss();
        }
    }
}

// Auto-combat: when set, RunCombat routes the player's turn through
// combat_ai_action just like the AI side. Used by the harness so a
// driver can step the bot through a full game without authoring the
// combat-modal input sequence per fight. Set via combat_set_auto_player
// (e.g. from harness command "auto_combat:on"). Persists across fights
// until cleared.
static bool s_auto_player = false;

void combat_set_auto_player(bool on) { s_auto_player = on; }
bool combat_auto_player(void)        { return s_auto_player; }

CombatResult RunCombat(Game *g, const Sprites *sprites,
                       void *render_target,
                       CombatMode mode, const CombatTarget *target) {
    Combat c;
    combat_init(&c, g, mode, target);
    // Expose the live combat to the harness so `state` can serialize it.
    // Detached on every return path below.
    harness_attach_combat(&c);
    recorder_attach_combat(&c);
    combat_seed_rng(&c, g, mode, target);
    combat_prepare_player(&c, g);
    if (mode == COMBAT_MODE_CASTLE) combat_prepare_castle(&c, target);
    else                            combat_prepare_foe(&c, target);
    combat_reset_match(&c);
    {
        char tag[64];
        snprintf(tag, sizeof tag, "combat:start:%s:%s",
                 mode == COMBAT_MODE_CASTLE ? "castle" : "foe",
                 (target && target->name) ? target->name : "?");
        recorder_capture(tag);
    }
    audio_set_track(AUDIO_TRACK_COMBAT);

    // Prime the turn machinery: pretend AI just finished, refresh
    // counters, find first actable player unit.
    combat_reset_turn(&c, COMBAT_SIDE_AI);
    c.unit_id = -1;
    int nxt = combat_next_unit(&c);
    if (nxt < 0) {
        harness_attach_combat(NULL);
        recorder_attach_combat(NULL);
        recorder_capture("combat:end:loss");
        audio_play_tune(AUDIO_TUNE_DEFEAT);
        audio_set_track(AUDIO_TRACK_OPENWORLD);
        return COMBAT_RESULT_LOSS;
    }
    c.unit_id = nxt;

    RenderTexture2D *rt = (RenderTexture2D *)render_target;
    double next_tick = GetTime() + 0.15;

    while (c.result == 0 && !WindowShouldClose()) {
        // Pump the harness every frame so external commands keep
        // working through combat. Without this the game appears
        // deadlocked from the harness POV the moment battle starts.
        harness_tick();
        audio_tick();
        combat_present(&c, g, sprites, rt);

        bool frame_rollover;
        combat_tick_anim(&c, &next_tick, &frame_rollover);

        if (c.unit_id >= 0) {
            const CombatUnit *act = &c.units[c.side][c.unit_id];
            if (act->troop_idx >= 0 && act->count > 0) {
                c.cursor_x = act->x;
                c.cursor_y = act->y;
            }
        }

        int acted = 0;
        bool player_turn = (c.side == COMBAT_SIDE_PLAYER &&
                            c.unit_id >= 0 &&
                            !c.units[c.side][c.unit_id].out_of_control);
        if (player_turn && !s_auto_player) {
            acted = combat_player_action_full(&c, g, sprites, rt);
        } else if (frame_rollover) {
            // AI drives this turn — either a real AI unit, an
            // out-of-control player unit, or any player unit while
            // auto_combat is on (harness-driven runs).
            acted = combat_ai_action(&c);
        }

        if (acted) {
            // Park the now-inactive unit on its idle frame so it stops
            // animating (). The next unit picked
            // below will start cycling from frame 0 in combat_tick_anim.
            if (c.unit_id >= 0) {
                CombatUnit *prev = &c.units[c.side][c.unit_id];
                if (prev->troop_idx >= 0) prev->frame = 0;
            }
            combat_compact(&c);
            if (combat_test_dead(&c, COMBAT_SIDE_AI))     { c.result = 1; break; }
            if (combat_test_dead(&c, COMBAT_SIDE_PLAYER)) { c.result = 2; break; }
            int n = combat_next_unit(&c);
            if (n < 0) combat_next_turn(&c);
            else        c.unit_id = n;
        }

        if (harness_key_pressed(KEY_ESCAPE)) { c.result = 2; break; }
    }

    // ----- End-of-combat (Phase 11) -----------------------------------------
    // Victory: gold += AI-side spoils, show kb_066-style banner.
    // Defeat: show kb_098 "disgraced" flavor (the temp_death and
    // re-commission flow lives in main.c's caller and stays there).
    if (c.result == 1) {
        g->stats.gold += c.spoils[COMBAT_SIDE_AI];
        char body[400];
        if (c.target_name[0]) {
            snprintf(body, sizeof body,
                     "Well done %s, you have\n"
                     "successfully vanquished\n"
                     "%s.\n\n"
                     "Spoils of War: %d gold",
                     g->character.name[0] ? g->character.name : "warrior",
                     c.target_name,
                     c.spoils[COMBAT_SIDE_AI]);
        } else {
            snprintf(body, sizeof body,
                     "Well done %s, you have\n"
                     "successfully vanquished\n"
                     "yet another foe.\n\n"
                     "Spoils of War: %d gold",
                     g->character.name[0] ? g->character.name : "warrior",
                     c.spoils[COMBAT_SIDE_AI]);
        }
        open_dialog("Victory!", body);
        combat_wait_for_dialog_ack(&c, g, sprites, rt);
        // Write surviving troops back to g->army so the player keeps
        // their losses. Vacated slots get compacted afterwards so the
        // army view stays contiguous (no gaps where a stack was wiped).
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            CombatUnit *u = &c.units[COMBAT_SIDE_PLAYER][i];
            if (u->troop_idx < 0 || u->count == 0) {
                g->army[i].id[0] = '\0';
                g->army[i].count = 0;
            } else {
                const TroopDef *t = troop_by_index(u->troop_idx);
                if (t) {
                    snprintf(g->army[i].id, sizeof g->army[i].id, "%s", t->id);
                    g->army[i].count = u->count;
                }
            }
        }
        GameCompactArmy(g);
        harness_attach_combat(NULL);
        recorder_attach_combat(NULL);
        recorder_capture("combat:end:win");
        audio_set_track(AUDIO_TRACK_OPENWORLD);
        return COMBAT_RESULT_WIN;
    }
    if (c.result == 2) {
        //  — defeat returns silently. The "After
        // being disgraced..." message is shown by perform_temp_death
        // in main.c after the player has been teleported back to the
        // home castle, drawn over the overworld in the bottom box
        // ( which calls draw_map/draw_sidebar
        // first). No battlefield-side dialog here.
        harness_attach_combat(NULL);
        recorder_attach_combat(NULL);
        recorder_capture("combat:end:loss");
        audio_play_tune(AUDIO_TUNE_DEFEAT);
        audio_set_track(AUDIO_TRACK_OPENWORLD);
        return COMBAT_RESULT_LOSS;
    }
    harness_attach_combat(NULL);
    recorder_attach_combat(NULL);
    recorder_capture("combat:end:flee");
    audio_set_track(AUDIO_TRACK_OPENWORLD);
    return COMBAT_RESULT_FLEE;
}

// ----- Phase 14: determinism harness ---------------------------------------
//
// Pure-formula test entry: assembles a Combat with two stacks (attacker
// at slot 0 player side, defender at slot 0 AI side), no hero on either
// side (so morale doesn't apply), no obstacles, no powers. Then runs
// `rounds` consecutive melee swings and folds the per-step state into
// a single 64-bit digest. The digest must stay stable across builds —
// any change indicates a formula regression.

uint64_t combat_test_digest(uint64_t seed,
                            const char *attacker_id,
                            int attacker_count,
                            const char *defender_id,
                            int defender_count,
                            int rounds) {
    Combat c;
    memset(&c, 0, sizeof c);
    c.rng_state = seed ? seed : 1UL;
    c.heroes[COMBAT_SIDE_PLAYER] = NULL;  // no morale; no followers tally
    c.heroes[COMBAT_SIDE_AI]     = NULL;
    c.powers[COMBAT_SIDE_PLAYER] = 0;
    c.powers[COMBAT_SIDE_AI]     = 0;
    for (int s = 0; s < COMBAT_SIDES; s++) {
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            c.units[s][i].troop_idx = -1;
        }
    }

    const TroopDef *at = troop_by_id(attacker_id);
    const TroopDef *dt = troop_by_id(defender_id);
    if (!at || !dt) return 0;

    combat_init_unit(&c.units[COMBAT_SIDE_PLAYER][0], at->index, attacker_count);
    c.units[COMBAT_SIDE_PLAYER][0].x = 2;
    c.units[COMBAT_SIDE_PLAYER][0].y = 2;
    c.units[COMBAT_SIDE_PLAYER][0].shots = at->ranged_ammo;

    combat_init_unit(&c.units[COMBAT_SIDE_AI][0], dt->index, defender_count);
    c.units[COMBAT_SIDE_AI][0].x = 3;
    c.units[COMBAT_SIDE_AI][0].y = 2;
    c.units[COMBAT_SIDE_AI][0].shots = dt->ranged_ammo;

    uint64_t digest = 0xCBF29CE484222325ULL;  // FNV-64 offset basis
    for (int r = 0; r < rounds; r++) {
        // Snapshot then swing.
        c.units[0][0].turn_count = c.units[0][0].count;
        c.units[1][0].turn_count = c.units[1][0].count;
        int kills = combat_deal_damage(&c, 0, 0, 1, 0, false, false, 0, false);
        // Fold all the load-bearing state into the digest. Each
        // multiply uses the FNV-64 prime so order matters.
        uint64_t fields[6] = {
            (uint64_t)c.units[0][0].count,
            (uint64_t)c.units[0][0].injury,
            (uint64_t)c.units[1][0].count,
            (uint64_t)c.units[1][0].injury,
            (uint64_t)(kills < 0 ? 0xFFFFFFFFu : (unsigned)kills),
            (uint64_t)c.rng_state,
        };
        for (int i = 0; i < 6; i++) {
            digest ^= fields[i];
            digest *= 0x100000001B3ULL;
        }
        // Reset retaliation flag so the next round can swing again.
        c.units[0][0].retaliated = false;
        c.units[1][0].retaliated = false;
        if (c.units[0][0].count == 0 || c.units[1][0].count == 0) break;
    }
    return digest;
}

CombatResult combat_run_headless(Game *g, CombatMode mode,
                                 const CombatTarget *target,
                                 int cap_rounds) {
    if (!g) return COMBAT_RESULT_LOSS;
    Combat c;
    combat_init(&c, g, mode, target);
    combat_seed_rng(&c, g, mode, target);
    combat_prepare_player(&c, g);
    if (mode == COMBAT_MODE_CASTLE) combat_prepare_castle(&c, target);
    else                            combat_prepare_foe(&c, target);
    combat_reset_match(&c);

    combat_reset_turn(&c, COMBAT_SIDE_AI);
    c.unit_id = -1;
    int nxt = combat_next_unit(&c);
    if (nxt < 0) return COMBAT_RESULT_LOSS;
    c.unit_id = nxt;

    int actions = 0;
    int max_actions = cap_rounds > 0 ? cap_rounds * 64 : 4096;
    while (c.result == 0 && actions < max_actions) {
        int acted = combat_ai_action(&c);
        if (acted) {
            if (c.unit_id >= 0) {
                CombatUnit *prev = &c.units[c.side][c.unit_id];
                if (prev->troop_idx >= 0) prev->frame = 0;
            }
            combat_compact(&c);
            if (combat_test_dead(&c, COMBAT_SIDE_AI))     { c.result = 1; break; }
            if (combat_test_dead(&c, COMBAT_SIDE_PLAYER)) { c.result = 2; break; }
            int n = combat_next_unit(&c);
            if (n < 0) combat_next_turn(&c);
            else        c.unit_id = n;
        }
        actions++;
    }

    if (c.result == 1) {
        g->stats.gold += c.spoils[COMBAT_SIDE_AI];
        for (int i = 0; i < COMBAT_SLOTS; i++) {
            CombatUnit *u = &c.units[COMBAT_SIDE_PLAYER][i];
            if (u->troop_idx < 0 || u->count == 0) {
                g->army[i].id[0] = '\0';
                g->army[i].count = 0;
            } else {
                const TroopDef *t = troop_by_index(u->troop_idx);
                if (t) {
                    snprintf(g->army[i].id, sizeof g->army[i].id, "%s", t->id);
                    g->army[i].count = u->count;
                }
            }
        }
        GameCompactArmy(g);
        return COMBAT_RESULT_WIN;
    }
    if (c.result == 2) return COMBAT_RESULT_LOSS;
    return COMBAT_RESULT_FLEE;
}
