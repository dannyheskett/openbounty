#include "game.h"
#include "map.h"
#include "adventure.h"
#include "savegame.h"
#include "fatal.h"
#include "ui_host.h"   // recorder_capture, audio_play_tune + AudioTuneId
#include "game_internal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>

// Seeded random helper for deterministic scepter placement.
// Uses uint64_t so the LCG behaves identically on 32-bit and 64-bit
// platforms (unsigned long is 32-bit on Windows and breaks the >>32
// shift).
static uint64_t game_rng_state = 0;

void game_rng_seed(uint64_t seed) {
    game_rng_state = seed ^ 0x5DEECE66DULL;  // Linear congruential generator seed
}

// 8-bit world catalog: expand a 0..255 index into a full-width world seed.
// The expansion is not decoration. Consumers read the seed at three widths --
// flows.c spawns use (seed >> 8), chest_rand / the dwelling pick / the weekly
// salt truncate to (unsigned), and the LCG above takes all 64 bits -- so a raw
// 0..255 seed would hand (seed >> 8) the value 0 for every world and leave the
// rest with 8 bits of entropy. Avalanching first keeps all 256 worlds distinct
// through every one of those paths.
//
// This is the catalog's identity: changing these constants, or the order of the
// game_rng_next() calls below, re-maps every world and invalidates any recorded
// per-world result.
uint64_t GameSeedFromIndex(unsigned char index) {
    uint64_t z = (uint64_t)index + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

int game_rng_next(int min, int max) {
    if (min > max) return min;
    if (min == max) return min;
    game_rng_state = game_rng_state * 25214903917ULL + 11ULL;
    unsigned int result = (unsigned int)(game_rng_state >> 32);
    return min + (result % (max - min + 1));
}

// Snapshot/restore the process-global world RNG. The autoplay planner
// drives the real engine on game COPIES to validate objectives, but some
// engine ops (e.g. GameRollChest) advance this global state; the planner
// snapshots before and restores after so the live game's RNG sequence is
// unperturbed by planning.
uint64_t GameRngSnapshot(void) { return game_rng_state; }
void     GameRngRestore(uint64_t s) { game_rng_state = s; }

void game_copy_id(char *dst, size_t dst_sz, const char *src) {
    if (!src) { dst[0] = '\0'; return; }
    size_t i = 0;
    while (i + 1 < dst_sz && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

void GameAddPlacement(Game *g, const char *zone, int x, int y, int kind, const char *id) {
    if (!g || !zone) return;
    if (!GameReservePlacements(g, g->placement_count + 1)) return;
    SaltedPlacement *p = &g->placements[g->placement_count++];
    game_copy_id(p->zone, sizeof(p->zone), zone);
    p->x = x;
    p->y = y;
    p->kind = kind;
    game_copy_id(p->id, sizeof(p->id), id);
}

// The cast formulas, exposed as read-only queries so planning layers never
// duplicate them (game.h). The casts below apply exactly these values.
// Both are the bare original formulas: NO minimum. A class whose current rank
// has spell_power 0 (Barbarian, first rank) gets nothing from either spell --
// that is the intended original behavior, not a degenerate case to clamp away.
int GameTimeStopStepsPerCast(const Game *g) {
    return (g ? g->stats.spell_power : 0) * 10;
}

int GameRaiseControlAmount(const Game *g) {
    return (g ? g->stats.spell_power : 0) * 100;
}

bool GameTerrainCostsFullDay(int terrain) {
    return terrain == TERRAIN_DESERT;
}

// time_stop: game->time_stop += spell_power * 10.
void GameCastTimeStop(Game *g) {
    if (!g) return;
    int idx = spell_index_by_adventure_effect(ADV_EFFECT_TIME_STOP);
    if (idx < 0 || g->spells.counts[idx] <= 0) return;
    g->spells.counts[idx]--;
    g->stats.time_stop += GameTimeStopStepsPerCast(g);
}

// raise_control: leadership_current += spell_power*100 (the field's only lever
// besides rank-ups), capped at GAME_LEADERSHIP_MAX. TEMPORARY: leadership_current
// resets to leadership_base at the next week boundary (GameOnStep), so callers
// that need the boost for a fight must use it the same tick. Consumes one
// charge; no-op (nothing spent) if none owned. Note the charge IS spent even at
// spell_power 0, where the amount is 0 -- the cast happens, it just does nothing.
void GameCastRaiseControl(Game *g) {
    if (!g) return;
    int idx = spell_index_by_adventure_effect(ADV_EFFECT_RAISE_CONTROL);
    if (idx < 0 || g->spells.counts[idx] <= 0) return;
    g->spells.counts[idx]--;
    g->stats.leadership_current += GameRaiseControlAmount(g);
    if (g->stats.leadership_current > GAME_LEADERSHIP_MAX)
        g->stats.leadership_current = GAME_LEADERSHIP_MAX;
}

// find_villain: scan castles for the one held by the active contract's
// villain, mark it known so its location shows on the world map and
// the intel dialog reflects it.
void GameCastFindVillain(Game *g) {
    if (!g) return;
    int idx = spell_index_by_adventure_effect(ADV_EFFECT_FIND_VILLAIN);
    if (idx < 0 || g->spells.counts[idx] <= 0) return;
    if (!g->contract.active_id[0]) return;
    g->spells.counts[idx]--;
    for (int i = 0; i < g->castle_count; i++) {
        if (!g->castles[i].id[0]) continue;
        if (g->castles[i].owner_kind != CASTLE_OWNER_VILLAIN) continue;
        if (strcmp(g->castles[i].villain_id, g->contract.active_id) != 0)
            continue;
        g->castles[i].known = true;
        return;
    }
}

// Weekly-economy prediction queries -- the SAME quantities the week boundary below
// applies (end_day's week block). Planning layers read these to decide whether
// waiting a week accrues or bleeds gold; the formulas exist only here.
int GameStackWeeklyUpkeep(const char *troop_id, int count) {
    // Weekly upkeep is count * (recruit_cost/10), not full recruit_cost.
    if (!troop_id || !troop_id[0] || count <= 0) return 0;
    const TroopDef *t = troop_by_id(troop_id);
    return t ? count * (t->recruit_cost / 10) : 0;
}

int GameArmyWeeklyUpkeep(const Game *g) {
    if (!g) return 0;
    int upkeep = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++)
        upkeep += GameStackWeeklyUpkeep(g->army[i].id, g->army[i].count);
    return upkeep;
}

// What the week-end takes for the army from a wallet of `gold` (after the
// commission). Where unpaid troops leave (#141), each stack in slot order is
// paid in full or not at all, and `left` (may be NULL) marks the slots that
// go; otherwise the whole upkeep is charged and the gold floor absorbs any
// shortfall.
static int week_army_charge(const Game *g, int gold, bool left[GAME_ARMY_SLOTS]) {
    if (left) for (int i = 0; i < GAME_ARMY_SLOTS; i++) left[i] = false;
    if (!g->res || !g->res->economy.unpaid_troops_leave) return GameArmyWeeklyUpkeep(g);
    int paid = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        int cost = GameStackWeeklyUpkeep(g->army[i].id, g->army[i].count);
        if (!g->army[i].id[0] || g->army[i].count <= 0) continue;
        if (cost <= gold - paid) paid += cost;
        else if (left) left[i] = true;
    }
    return paid;
}

int GameWeeklyNetGold(const Game *g) {
    if (!g) return 0;
    int wallet = g->stats.gold + g->stats.commission_weekly;
    int net = g->stats.commission_weekly - week_army_charge(g, wallet > 0 ? wallet : 0, NULL);
    // The boat's fare is charged whatever the purse holds (end_day floors the
    // gold at 0 afterwards).
    if (g->boat.has_boat) net -= GameBoatCost(g);
    return net;
}

int GameWeeklyOutgoings(const Game *g) {
    // end_week's gross weekly debit (upkeep + boat fare). Unlike
    // GameWeeklyNetGold, the fare is counted unconditionally: this is what
    // the week CHARGES, not what the post-credit wallet can afford.
    if (!g) return 0;
    return GameArmyWeeklyUpkeep(g) + (g->boat.has_boat ? GameBoatCost(g) : 0);
}

int GameWeekId(const Game *g) {
    // Weeks elapsed since start -- the key GamePickAstrologyCreature is fed
    // at each week boundary (end_day below uses this same function).
    if (!g) return 0;
    int start_days = g->res->time.days_per_difficulty[0];
    if ((int)g->character.difficulty >= 0 &&
        (int)g->character.difficulty < 4) {
        start_days = g->res->time.days_per_difficulty[
            (int)g->character.difficulty];
    }
    int passed = start_days - g->stats.days_left;
    int wk = g->res->time.week_days;
    return (wk > 0) ? (passed / wk) : 0;
}

static void end_day(Game *g, bool *week_ended, int *commission_paid) {
    if (week_ended) *week_ended = false;
    if (commission_paid) *commission_paid = 0;

    if (g->stats.days_left > 0) g->stats.days_left--;
    g->stats.steps_left_today = g->res->time.day_steps;
    g->stats.time_stop = 0;

    if (g->stats.days_left == 0) {
        g->stats.game_over = true;
        return;
    }
    int wk = g->res->time.week_days;
    if (wk > 0 && g->stats.days_left % wk == 0) {
        // Week boundary. end_week order:
        //   reset time_stop, leadership
        //   roll astrology creature
        //   credit commission, debit upkeep + boat
        //   ghosts -> peasants on astrology=peasants week
        //   repopulate matching dwellings
        g->stats.time_stop = 0;
        g->stats.leadership_current = g->stats.leadership_base;

        // Week id: weeks elapsed since start.
        int week_id = GameWeekId(g);
        int astrology = GamePickAstrologyCreature(g, week_id);
        g->stats.last_astrology_troop = astrology;

        g->stats.last_week_on_hand = g->stats.gold;
        g->stats.gold += g->stats.commission_weekly;
        g->stats.last_commission = g->stats.commission_weekly;

        // The gold floor below means a short wallet pays only what it holds;
        // where unpaid troops leave, the stacks it cannot pay go instead.
        int wallet = g->stats.gold > 0 ? g->stats.gold : 0;
        bool left[GAME_ARMY_SLOTS];
        int upkeep = week_army_charge(g, wallet, left);
        memset(g->stats.last_week_left, 0, sizeof g->stats.last_week_left);
        bool any_left = false;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (!left[i]) continue;
            g->stats.last_week_left[i] = g->army[i];
            g->army[i].id[0] = '\0';
            g->army[i].count = 0;
            any_left = true;
        }
        if (any_left) GameCompactArmy(g);
        g->stats.last_week_army = upkeep < wallet ? upkeep : wallet;
        g->stats.last_week_boat = 0;
        g->stats.gold -= upkeep;

        // The boat's fare is charged from what the purse holds and the boat
        // is kept, as King's Bounty's end_week does (#199): a short purse
        // pays what it has, never taking the boat from under a sailing hero.
        if (g->boat.has_boat) {
            int boat_cost = GameBoatCost(g);
            int purse = g->stats.gold > 0 ? g->stats.gold : 0;
            int paid = boat_cost < purse ? boat_cost : purse;
            g->stats.gold -= boat_cost;
            g->stats.last_week_boat = paid;
        }
        if (g->stats.gold < 0) g->stats.gold = 0;

        // Astrology: full repopulate of matching dwellings; grow others;
        // ghosts -> peasants when creature == peasants.
        GameApplyAstrology(g, astrology);

        // Where the pack renews magic, one learned spell is filled (#157).
        g->stats.last_renewed_spell = GamePickRenewedSpell(g, week_id);
        GameRenewSpell(g, g->stats.last_renewed_spell);

        // A castle the hero left without a garrison falls back to the
        // monsters (OPENKB-SPEC section 16.11): a fresh monster garrison at the
        // castle's difficulty, and it must be besieged again. The original
        // tested stack 0 alone and kept the owner byte; here the whole
        // garrison must be empty and the owner changes, so a garrisoned
        // castle is never overwritten and a retaken one is the monsters' (#112).
        // Before the growth below, as in the original, so it grows this week.
        for (int i = 0; i < g->castle_count; i++) {
            CastleRecord *cr = &g->castles[i];
            if (!cr->id[0] || cr->owner_kind != CASTLE_OWNER_PLAYER) continue;
            bool empty = true;
            for (int s = 0; s < GAME_ARMY_SLOTS; s++)
                if (cr->garrison[s].count > 0) { empty = false; break; }
            if (!empty) continue;
            cr->owner_kind = CASTLE_OWNER_MONSTERS;
            cr->villain_id[0] = '\0';
            repopulate_castle(g, i);
            if (strcmp(g->position.own_castle, cr->id) == 0)
                g->position.own_castle[0] = '\0';
        }

        // weekly astrology growth.
        // For every non-player-owned castle, stacks whose troop matches
        // the astrology creature grow by troop.growth_per_week. Hostile
        // foes on the overworld grow by the same rule (play.c:1028-1031).
        const TroopDef *astro = troop_by_index(astrology);
        if (astro && astro->id[0] && astro->growth_per_week > 0) {
            for (int i = 0; i < g->castle_count; i++) {
                if (!g->castles[i].id[0]) continue;
                if (g->castles[i].owner_kind == CASTLE_OWNER_PLAYER ||
                    g->castles[i].owner_kind == CASTLE_OWNER_SPECIAL) continue;
                for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
                    if (g->castles[i].garrison[s].count == 0) continue;
                    if (strcmp(g->castles[i].garrison[s].id, astro->id) != 0)
                        continue;
                    g->castles[i].garrison[s].count += astro->growth_per_week;
                }
            }
            for (int i = 0; i < g->foe_count; i++) {
                FoeState *f = &g->foes[i];
                if (!f->alive) continue;
                for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
                    if (f->garrison[s].count == 0) continue;
                    if (strcmp(f->garrison[s].id, astro->id) != 0) continue;
                    f->garrison[s].count += astro->growth_per_week;
                }
            }
        }

        if (week_ended) *week_ended = true;
        if (commission_paid) *commission_paid = g->stats.commission_weekly;
    }
}

void GameOnStep(Game *g, bool terrain_is_desert,
                bool *day_ended, bool *week_ended, int *commission_paid) {
    if (day_ended)        *day_ended = false;
    if (week_ended)       *week_ended = false;
    if (commission_paid)  *commission_paid = 0;

    if (g->stats.game_over) return;

    // time_stop absorbs a step without advancing the day.
    if (g->stats.time_stop > 0) {
        g->stats.time_stop--;
        return;
    }

    if (terrain_is_desert) {
        g->stats.steps_left_today = 0;
    } else if (g->stats.steps_left_today > 0) {
        g->stats.steps_left_today--;
    }

    // Handle any day rollovers. Desert only zeroes once, but the loop
    // tolerates effects that push steps further negative.
    while (g->stats.steps_left_today <= 0 && !g->stats.game_over) {
        bool we = false;
        int  paid = 0;
        end_day(g, &we, &paid);
        if (day_ended) *day_ended = true;
        if (we)        { if (week_ended) *week_ended = true; }
        if (paid > 0)  { if (commission_paid) *commission_paid = paid; }
    }

    // State trace hook. Fires once per step regardless of day/week
    // rollover; the snapshot reflects the post-step state including
    // rolled-over day count.
    recorder_capture("step");
    audio_play_tune(AUDIO_TUNE_WALK);
}

bool GameIsOver(const Game *g) {
    return g->stats.game_over || g->stats.days_left == 0;
}

int GameSpendDays(Game *g, int days, int *total_commission) {
    if (total_commission) *total_commission = 0;
    if (!g) return 0;
    if (days > g->stats.days_left) days = g->stats.days_left;
    int weeks = 0;
    for (int i = 0; i < days && !g->stats.game_over; i++) {
        bool we = false; int paid = 0;
        end_day(g, &we, &paid);
        if (we) weeks++;
        if (paid > 0 && total_commission) *total_commission += paid;
    }
    return weeks;
}

int GameSpendWeek(Game *g, int *total_commission) {
    if (!g) return 0;
    int week_days = g->res ? g->res->time.week_days : 5;
    if (week_days < 1) week_days = 5;
    // Days remaining in the current week: how many more end_day() calls
    // land on a multiple of week_days. end_week_days =
    // WEEK_DAYS - (passed_days % WEEK_DAYS).
    // passed_days isn't tracked directly; derive from days_left and the
    // starting count.
    int start_days = g->res ? g->res->time.days_per_difficulty[0] : 900;
    if ((int)g->character.difficulty >= 0 &&
        (int)g->character.difficulty < 4 && g->res) {
        start_days = g->res->time.days_per_difficulty[(int)g->character.difficulty];
    }
    int passed = start_days - g->stats.days_left;
    int into_week = passed % week_days;
    int to_spend = week_days - into_week;
    if (to_spend <= 0) to_spend = week_days;
    return GameSpendDays(g, to_spend, total_commission);
}

bool GameClaimArtifact(Game *g, int idx) {
    if (idx < 0 || idx >= g->artifacts.count) return false;
    if (g->artifacts.found[idx]) return false;
    g->artifacts.found[idx] = true;

    const ArtifactDef *a = artifact_by_index(idx);
    if (!a) return true;
    switch (a->power) {
        case ARTIFACT_POWER_DOUBLE_LEADERSHIP:
            g->stats.leadership_base    *= 2;
            g->stats.leadership_current  = g->stats.leadership_base;
            break;
        case ARTIFACT_POWER_INCREASE_COMMISSION:
            g->stats.commission_weekly += 2000;
            break;
        case ARTIFACT_POWER_DOUBLE_SPELL_POWER:
            g->stats.spell_power *= 2;
            break;
        case ARTIFACT_POWER_DOUBLE_MAX_SPELLS:
            g->stats.max_spells *= 2;
            break;
        // Damage/protection apply in combat; cheaper boats is a passive query;
        // unknown is a no-op.
        default: break;
    }
    {
        char tag[64];
        snprintf(tag, sizeof tag, "artifact:%s", a->id);
        recorder_capture(tag);
    }
    return true;
}

bool GameHasPower(const Game *g, ArtifactPower power) {
    for (int i = 0; i < g->artifacts.count; i++) {
        const ArtifactDef *a = artifact_by_index(i);
        if (g->artifacts.found[i] && a && a->power == power) return true;
    }
    return false;
}

void GameAddConsumed(Game *g, const char *zone, int x, int y) {
    if (!g || !zone) return;
    for (int i = 0; i < g->consumed_count; i++) {
        if (g->consumed[i].x == x && g->consumed[i].y == y &&
            strcmp(g->consumed[i].zone, zone) == 0) return;
    }
    if (!GameReserveConsumed(g, g->consumed_count + 1)) return;
    TileMutation *m = &g->consumed[g->consumed_count++];
    game_copy_id(m->zone, sizeof(m->zone), zone);
    m->x = x;
    m->y = y;
}

void GameAddBridge(Game *g, const char *zone, int x, int y, bool vertical) {
    if (!g || !zone) return;
    for (int i = 0; i < g->bridge_count; i++) {
        if (g->bridges[i].x == x && g->bridges[i].y == y &&
            strcmp(g->bridges[i].zone, zone) == 0) return;
    }
    if (!GameReserveBridges(g, g->bridge_count + 1)) return;
    BuiltBridge *b = &g->bridges[g->bridge_count++];
    game_copy_id(b->zone, sizeof(b->zone), zone);
    b->x = x;
    b->y = y;
    b->vertical = vertical ? 1 : 0;
}

void GameApplyTileMutations(const Game *g, Map *map, const char *zone) {
    if (!g || !map || !zone) return;
    for (int i = 0; i < g->consumed_count; i++) {
        const TileMutation *m = &g->consumed[i];
        if (strcmp(m->zone, zone) != 0) continue;
        MapClearInteractive(map, m->x, m->y);
    }
    // Bridges the spell laid: the fresh map has the water back under each.
    for (int i = 0; i < g->bridge_count; i++) {
        const BuiltBridge *b = &g->bridges[i];
        if (strcmp(b->zone, zone) != 0) continue;
        MapLayBridge(map, b->x, b->y, b->vertical != 0);
    }
    // A vista that has played changed the map for good (its bridge, its cleared
    // pass): re-apply those tiles every time the zone loads.
    const ResZone *z = g->res ? resources_zone_by_id(g->res, zone) : NULL;
    for (int i = 0; z && i < g->events_done_count; i++) {
        if (strcmp(g->events_done[i].zone, zone) != 0) continue;
        for (int k = 0; k < z->event_count; k++) {
            const ResZoneEvent *ev = &z->events[k];
            if (strcmp(ev->id, g->events_done[i].id) != 0) continue;
            for (int e = 0; e < ev->effect_count; e++) {
                if (ev->effects[e].kind != RES_EVENT_FX_TILE) continue;   // the fog is saved
                MapSetTileFromCode(map, g->res, ev->effects[e].x,
                                   ev->effects[e].y, ev->effects[e].code);
            }
        }
    }
}

bool GameReloadZoneMap(const Game *g, Map *map, const char *zone) {
    if (!g || !map || !zone || !zone[0]) return false;
    if (!MapLoadZoneWithPlacements(map, g->res, zone, g)) return false;
    // Re-apply consumed tiles so picked-up artifacts / chests stay gone, and
    // lay the spell's bridges again.
    GameApplyTileMutations(g, map, zone);
    return true;
}

bool GameFoeBarsHero(const Game *g, const FoeState *f) {
    if (!g || !f || !f->requires_troop[0]) return false;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++)
        if (g->army[i].count > 0 && strcmp(g->army[i].id, f->requires_troop) == 0)
            return false;
    return true;
}

bool GameEventFired(const Game *g, const char *zone, const char *id) {
    if (!g || !zone || !id) return false;
    for (int i = 0; i < g->events_done_count; i++)
        if (strcmp(g->events_done[i].zone, zone) == 0 &&
            strcmp(g->events_done[i].id, id) == 0) return true;
    return false;
}

// How much of one precondition the hero holds right now.
static int event_req_held(const Game *g, const ResEventReq *rq) {
    switch (rq->kind) {
    case RES_EVENT_REQ_SPELL: {
        int idx = spell_index_by_id(rq->id);
        return (idx >= 0 && idx < g->spells.count) ? g->spells.counts[idx] : 0;
    }
    case RES_EVENT_REQ_TROOP: {
        int n = 0;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++)
            if (strcmp(g->army[i].id, rq->id) == 0) n += g->army[i].count;
        return n;
    }
    case RES_EVENT_REQ_GOLD:
        return g->stats.gold;
    case RES_EVENT_REQ_ARTIFACT: {
        const ArtifactDef *a = artifact_by_id(rq->id);
        return (a && a->index >= 0 && a->index < g->artifacts.count &&
                g->artifacts.found[a->index]) ? 1 : 0;
    }
    }
    return 0;
}

// Spend what a precondition says it consumes (troops and artifacts are held,
// never spent: a vista asks the hero to bring them, not to give them up).
static void event_req_spend(Game *g, const ResEventReq *rq) {
    if (!rq->consume) return;
    switch (rq->kind) {
    case RES_EVENT_REQ_SPELL: {
        int idx = spell_index_by_id(rq->id);
        if (idx >= 0 && idx < g->spells.count) {
            g->spells.counts[idx] -= rq->count;
            if (g->spells.counts[idx] < 0) g->spells.counts[idx] = 0;
        }
        break;
    }
    case RES_EVENT_REQ_GOLD:
        g->stats.gold -= rq->count;
        if (g->stats.gold < 0) g->stats.gold = 0;
        break;
    case RES_EVENT_REQ_TROOP:
    case RES_EVENT_REQ_ARTIFACT:
        break;
    }
}

bool GameTryFireEvent(Game *g, Map *map, Fog *fog, int x, int y) {
    if (!g || !map || !g->res || !g->position.zone[0]) return false;
    const ResZone *z = resources_zone_by_id(g->res, g->position.zone);
    if (!z) return false;
    for (int k = 0; k < z->event_count; k++) {
        const ResZoneEvent *ev = &z->events[k];
        if (ev->x != x || ev->y != y) continue;
        if (GameEventFired(g, g->position.zone, ev->id)) return false;
        for (int q = 0; q < ev->req_count; q++)
            if (event_req_held(g, &ev->reqs[q]) < ev->reqs[q].count) {
                // Not yet: the place says so, when the pack gave it words.
                if (ev->hint[0]) player_io_note(g, ev->title[0] ? ev->title : NULL, ev->hint);
                return false;
            }
        if (!GameReserveEventsDone(g, g->events_done_count + 1)) return false;
        for (int q = 0; q < ev->req_count; q++) event_req_spend(g, &ev->reqs[q]);
        for (int e = 0; e < ev->effect_count; e++) {
            if (ev->effects[e].kind == RES_EVENT_FX_REVEAL) {
                // The whole zone, seen from the lighthouse.
                if (fog) FogRevealRect(fog, map, map->width / 2, map->height / 2,
                                       map->width, map->height);
                continue;
            }
            MapSetTileFromCode(map, g->res, ev->effects[e].x, ev->effects[e].y,
                               ev->effects[e].code);
        }
        EventFired *done = &g->events_done[g->events_done_count++];
        game_copy_id(done->zone, sizeof done->zone, g->position.zone);
        game_copy_id(done->id, sizeof done->id, ev->id);
        player_io_note_scene_event(g, ev->title, ev->body, ev->scene_index);
        return true;
    }
    return false;
}

int GameKnownSpells(const Game *g) {
    int total = 0;
    for (int i = 0; i < g->spells.count; i++) total += g->spells.counts[i];
    return total;
}

int GameSpellRoom(const Game *g, int spell_idx) {
    if (!g || spell_idx < 0 || spell_idx >= g->spells.count) return 0;
    bool per_spell = g->res && g->res->economy.spell_limit_per_spell;
    int room = g->stats.max_spells - (per_spell ? g->spells.counts[spell_idx] : GameKnownSpells(g));
    return room > 0 ? room : 0;
}

int GameBoatCost(const Game *g) {
    if (GameHasPower(g, ARTIFACT_POWER_CHEAPER_BOATS))
        return g->res->economy.boat_cost_cheap;
    return g->res->economy.boat_cost_normal;
}

BoatActionResult GameRentBoat(Game *g, int boat_x, int boat_y,
                              const char *zone_id) {
    if (!g) return BOAT_RENT_NO_GOLD;
    // Boats are rented ONLY at a town (the town menu's Boat row). Reject an
    // off-map rental so no caller can conjure a boat without visiting a town.
    if (!g->position.in_town[0]) return BOAT_RENT_NO_GOLD;
    int cost = GameBoatCost(g);
    // KB: `if (gold <= boat_cost)` -- exact-match also fails (no free boat
    // when gold equals the cost).
    if (g->stats.gold <= cost) return BOAT_RENT_NO_GOLD;
    g->stats.gold -= cost;
    // Zero the WHOLE BoatState first (padding + the zone[] tail past the terminator): writing only
    // the live fields leaves stale bytes from a previous boat, which are meaningless but part of the
    // serialized/fingerprinted byte image -- two runs that reach the same logical boat via different
    // histories would then differ byte-for-byte (a determinism regression).
    memset(&g->boat, 0, sizeof g->boat);
    g->boat.has_boat = true;
    g->boat.x = boat_x;
    g->boat.y = boat_y;
    size_t m = 0;
    if (zone_id) {
        while (m + 1 < sizeof(g->boat.zone) && zone_id[m]) {
            g->boat.zone[m] = zone_id[m]; m++;
        }
    }
    g->boat.zone[m] = '\0';
    return BOAT_RENT_OK;
}

BoatActionResult GameCancelBoat(Game *g) {
    if (!g) return BOAT_CANCEL_OK;
    // Cancelling a rental is a town-menu action too (the Boat row toggles).
    if (!g->position.in_town[0]) return BOAT_CANCEL_OK;
    // KB: refuses cancellation while sailing; must disembark first.
    if (g->travel_mode == TRAVEL_BOAT) return BOAT_CANCEL_AT_SEA;
    memset(&g->boat, 0, sizeof g->boat);   // whole BoatState (padding + zone tail) -> canonical no-boat image
    g->boat.has_boat = false;
    g->boat.x = -1;
    g->boat.y = -1;
    return BOAT_CANCEL_OK;
}

SiegeBuyResult GameBuySiege(Game *g) {
    if (!g) return SIEGE_BUY_NO_GOLD;
    // Siege weapons are bought ONLY at a town (the town menu's Siege row). Reject
    // an off-map call so no caller can acquire them without visiting a town.
    if (!g->position.in_town[0]) return SIEGE_BUY_NO_GOLD;
    if (g->stats.siege_weapons) return SIEGE_BUY_ALREADY;
    int cost = g->res->economy.siege_cost;
    if (g->stats.gold <= cost) return SIEGE_BUY_NO_GOLD;  // KB: <= fails
    g->stats.gold -= cost;
    g->stats.siege_weapons = 1;
    return SIEGE_BUY_OK;
}

bool GameFoeCanEvade(const Game *g, const Map *map) {
    if (!g || !map) return false;
    if (!g->res || !g->res->economy.evade_needs_free_square) return true;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dy) continue;
            int x = g->position.x + dx, y = g->position.y + dy;
            const Tile *t = MapGetTile(map, x, y);
            if (!t || t->interactive != INTERACT_NONE) continue;
            // The hero's own boat, parked beside them in this zone, is a way
            // out for a hero on foot: stepping onto it boards it.
            bool boat_here = g->travel_mode == TRAVEL_WALK && g->character.mount != MOUNT_FLY &&
                             g->boat.has_boat && g->boat.x == x && g->boat.y == y &&
                             (g->boat.zone[0] == '\0' || strcmp(g->boat.zone, g->position.zone) == 0);
            bool ok = boat_here ? true
                    : (g->character.mount == MOUNT_FLY) ? adventure_walkable_in_flight(t)
                    : (g->travel_mode == TRAVEL_BOAT)   ? (t->terrain == TERRAIN_WATER || t->is_bridge)
                    :                                     adventure_walkable_on_foot(t);
            if (!ok) continue;
            bool foe_here = false;
            for (int i = 0; i < g->foe_count && !foe_here; i++) {
                const FoeState *f = &g->foes[i];
                foe_here = f->alive && f->x == x && f->y == y &&
                           strcmp(f->zone, g->position.zone) == 0;
            }
            if (!foe_here) return true;
        }
    }
    return false;
}

int GameAlcoveCost(const Game *g, const char *zone_id) {
    if (!g || !g->res) return 0;
    const ResZone *z = zone_id ? resources_zone_by_id(g->res, zone_id) : NULL;
    return (z && z->alcove_cost >= 0) ? z->alcove_cost : g->res->economy.alcove_cost;
}

bool GameHasRites(const Game *g, const char *zone_id) {
    if (!g) return false;
    if (!g->res || !g->res->economy.rites_per_zone) return g->stats.knows_magic;
    int zi = zone_id ? resources_zone_index(g->res, zone_id) : -1;
    return zi >= 0 && zi < g->world.zone_count && g->world.zone_rites[zi];
}

bool GameTownHasRites(const Game *g, const char *town_id) {
    if (!g || !g->res) return false;
    if (!g->res->economy.rites_per_zone) return true;   // one magic: towns sell to anyone
    const ResTown *t = town_id ? resources_town_by_id(g->res, town_id) : NULL;
    return t && GameHasRites(g, t->zone);
}

SpellBuyResult GameBuySpell(Game *g, const char *town_id) {
    if (!g || !town_id || !town_id[0]) return SPELL_BUY_NO_SPELL;
    // A spell is bought ONLY at the town you are visiting (the town menu's Spell
    // row). Require the hero to be AT this town -- reject an off-map call or one
    // naming a town the hero is not in.
    if (strcmp(g->position.in_town, town_id) != 0) return SPELL_BUY_NO_SPELL;
    const TownRecord *t = NULL;
    for (int i = 0; i < g->town_count; i++)
        if (strcmp(g->towns[i].id, town_id) == 0) { t = &g->towns[i]; break; }
    const SpellDef *sp =
        (t && t->spell_for_sale[0]) ? spell_by_id(t->spell_for_sale) : NULL;
    if (!sp) return SPELL_BUY_NO_SPELL;
    if (g->res && g->res->economy.rites_per_zone && !GameTownHasRites(g, town_id))
        return SPELL_BUY_NO_RITES;
    if (GameSpellRoom(g, sp->index) <= 0) return SPELL_BUY_AT_CAP;
    if (g->stats.gold <= sp->cost) return SPELL_BUY_NO_GOLD;  // KB: <= fails
    g->spells.counts[sp->index]++;
    g->spells.learned[sp->index] = true;   // a temple teaches it (#157)
    g->stats.gold -= sp->cost;
    return SPELL_BUY_OK;
}

TownRecord *GameTouchTown(Game *g, const char *town_id) {
    if (!town_id || !town_id[0]) return NULL;
    // Existing slot? Touching a known town marks it visited (the Town
    // Gate spell filters on this flag); first-visit also gets it set
    // below via the fresh-slot path.
    for (int i = 0; i < g->town_count; i++) {
        if (strcmp(g->towns[i].id, town_id) == 0) {
            g->towns[i].visited = true;
            return &g->towns[i];
        }
    }
    // Allocate a fresh slot.
    for (int i = 0; i < g->town_count; i++) {
        if (g->towns[i].id[0]) continue;
        TownRecord *t = &g->towns[i];
        game_copy_id(t->id, sizeof(t->id), town_id);
        t->visited = true;
        // Deterministic spell: seed xor town slot index, modulo spells_count().
        // We don't
        // carry that hardcoded pairing; any spell is fair game.
        // uint64_t, not unsigned long: the latter is 32-bit on Windows and
        // 64-bit elsewhere, which gave the same world different town spells
        // per platform once the seed carried entropy above bit 31.
        int idx = (int)((g->seed ^ (uint64_t)(i + 1)) % (uint64_t)spells_count());
        const SpellDef *sp = spell_by_index(idx);
        if (sp) game_copy_id(t->spell_for_sale, sizeof(t->spell_for_sale), sp->id);
        else    t->spell_for_sale[0] = '\0';
        return t;
    }
    return NULL;
}

bool GameVillainContractObtainable(const Game *g, const char *villain_id) {
    // A villain's contract can be taken right now iff its id is currently in one
    // of the active contract-cycle slots (or is already the active contract). A
    // villain that has not yet rotated into the cycle is PREDICTABLY unobtainable
    // until an earlier villain is captured (GameFulfillContract refills its slot)
    // -- a deterministic, O(cycle_length) check with no simulation. The planner
    // uses this to skip such villains INSTANTLY instead of probing them.
    if (!g || !villain_id || !villain_id[0]) return false;
    if (g->contract.active_id[0] &&
        strcmp(g->contract.active_id, villain_id) == 0) return true;
    int n = g->res ? g->res->contract.cycle_length : 0;
    if (n > g->contract.cycle_count) n = g->contract.cycle_count;
    for (int i = 0; i < n; i++)
        if (strcmp(g->contract.cycle[i], villain_id) == 0) return true;
    return false;
}

const char *GameTakeNextContract(Game *g) {
    // Contracts are taken ONLY at a town (the player's only path is the town
    // menu's "Get New Contract" row). The engine enforces it so no caller --
    // autoplay included -- can take a contract from the open map.
    if (!g || !g->position.in_town[0]) return NULL;
    int n = g->contract.cycle_count;
    if (n < 1) return NULL;
    g->contract.last_contract++;
    if (g->contract.last_contract > n - 1) g->contract.last_contract = 0;
    const char *vid = g->contract.cycle[g->contract.last_contract];
    if (!vid[0]) return NULL;
    game_copy_id(g->contract.active_id, sizeof(g->contract.active_id), vid);
    {
        char tag[64];
        snprintf(tag, sizeof tag, "contract:new:%s", vid);
        recorder_capture(tag);
    }
    return g->contract.active_id;
}

const char *GameTakeContractAt(Game *g, int slot) {
    if (!g || !g->position.in_town[0]) return NULL;
    int n = g->contract.cycle_count;
    if (slot < 0 || slot >= n) return NULL;
    const char *vid = g->contract.cycle[slot];
    if (!vid[0]) return NULL;
    g->contract.last_contract = slot;
    game_copy_id(g->contract.active_id, sizeof(g->contract.active_id), vid);
    {
        char tag[64];
        snprintf(tag, sizeof tag, "contract:new:%s", vid);
        recorder_capture(tag);
    }
    return g->contract.active_id;
}

bool GameFulfillContract(Game *g, const char *villain_id) {
    if (!g || !villain_id || !villain_id[0]) return false;
    const VillainDef *v = villain_by_id(villain_id);
    if (!v) return false;

    g->stats.gold += v->reward;
    if (v->index >= 0 && v->index < g->contract.villain_count) {
        g->contract.villains_caught[v->index] = true;
    }

    int cycle_len = g->contract.cycle_count;

    int slot = -1;
    for (int i = 0; i < cycle_len; i++) {
        if (strcmp(g->contract.cycle[i], villain_id) == 0) { slot = i; break; }
    }

    if (g->contract.active_id[0] &&
        strcmp(g->contract.active_id, villain_id) == 0) {
        g->contract.active_id[0] = '\0';
    }

    if (slot < 0) return false;

    g->contract.cycle[slot][0] = '\0';

    int total = villains_count();
    for (int i = g->contract.max_contract; i < total; i++) {
        const VillainDef *cand = villain_by_index(i);
        if (!cand) continue;
        if (cand->index >= 0 && cand->index < g->contract.villain_count &&
            g->contract.villains_caught[cand->index]) continue;
        game_copy_id(g->contract.cycle[slot],
                sizeof(g->contract.cycle[slot]), cand->id);
        break;
    }
    g->contract.max_contract++;
    {
        char tag[64];
        snprintf(tag, sizeof tag, "contract:done:%s", villain_id);
        recorder_capture(tag);
    }
    return true;
}

bool GameMaybeRankUp(Game *g) {
    const ClassDef *cls = class_by_id(g->character.cls.id);
    if (!cls) return false;
    int caught = GameVillainsCaught(g);
    int r = g->character.cls.rank_index;
    // Promote while caught count meets the next rank's threshold.
    bool changed = false;
    while (r < 3 && caught >= cls->ranks[r + 1].villains_needed) {
        r++;
        changed = true;
    }
    if (!changed) return false;
    int old_r = g->character.cls.rank_index;
    g->character.cls.rank_index = r;
    game_copy_id(g->character.cls.rank_id, sizeof(g->character.cls.rank_id),
            cls->ranks[r].id);
    game_copy_id(g->character.cls.rank_title, sizeof(g->character.cls.rank_title),
            cls->ranks[r].name);
    // Promotion applies the new ranks' bonuses ADDITIVELY (openkb: each stat
    // is an accumulator -- "base_leadership never decreases", spell_power /
    // max_spells / commission "increment from class ranks"). Assigning the
    // class-table cumulative here instead WIPED every chest-accumulated
    // bonus (leadership, spell power, max spells, commission) at each
    // promotion -- a fidelity bug that starved late-game heroes.
    int lead0 = 0, maxsp0 = 0, spp0 = 0, comm0 = 0;
    int lead1 = 0, maxsp1 = 0, spp1 = 0, comm1 = 0;
    class_stats_at_rank(cls, old_r, &lead0, &maxsp0, &spp0, &comm0);
    class_stats_at_rank(cls, r,     &lead1, &maxsp1, &spp1, &comm1);
    g->stats.leadership_base   += lead1 - lead0;
    g->stats.commission_weekly += comm1 - comm0;
    g->stats.max_spells        += maxsp1 - maxsp0;
    g->stats.spell_power       += spp1 - spp0;
    // The current cap gains the same delta (it re-syncs to base at end_week).
    g->stats.leadership_current += lead1 - lead0;
    {
        char tag[64];
        snprintf(tag, sizeof tag, "rank:%s", g->character.cls.rank_id);
        recorder_capture(tag);
    }
    return true;
}

GameAudienceOutcome GameAudienceWithKing(Game *g, int *out_needed) {
    if (out_needed) *out_needed = 0;
    const ClassDef *cls = class_by_id(g->character.cls.id);
    int rank = g->character.cls.rank_index;
    if (!cls || rank + 1 >= cls->rank_count) return GAME_AUDIENCE_FINAL_RANK;
    int needed = cls->ranks[rank + 1].villains_needed - GameVillainsCaught(g);
    if (needed > 0) {
        if (out_needed) *out_needed = needed;
        return GAME_AUDIENCE_MORE_NEEDED;
    }
    GameMaybeRankUp(g);
    return GAME_AUDIENCE_PROMOTED;
}

// pct of v, at least 1 when v is above 0.
static int pct_gain(int v, int pct) {
    if (v <= 0 || pct <= 0) return 0;
    int d = (int)((long long)v * pct / 100);
    return d < 1 ? 1 : d;
}

GameBlessingOutcome GameSeekBlessing(Game *g, int *out_needed, GameAudienceGain *gain) {
    if (out_needed) *out_needed = 0;
    if (gain) memset(gain, 0, sizeof *gain);
    if (g->stats.blessed) return GAME_BLESSING_ALREADY;
    int total = g->artifacts.count;
    int missing = total - GameArtifactsFound(g);
    if (missing > 0) {
        if (out_needed) *out_needed = missing;
        return GAME_BLESSING_NEED_ARTIFACTS;
    }
    int d = pct_gain(g->stats.leadership_base, g->res->economy.blessing_leadership_pct);
    g->stats.leadership_base += d;
    g->stats.leadership_current += d;   // re-syncs to base at end_week
    g->stats.blessed = true;
    if (gain) gain->leadership = d;
    return GAME_BLESSING_GRANTED;
}

bool GamePayTribute(Game *g, int *out_needed, GameAudienceGain *gain) {
    const ResEconomy *ec = &g->res->economy;
    if (out_needed) *out_needed = 0;
    if (gain) memset(gain, 0, sizeof *gain);
    if (g->stats.gold < ec->tribute_cost) {
        if (out_needed) *out_needed = ec->tribute_cost - g->stats.gold;
        return false;
    }
    g->stats.gold -= ec->tribute_cost;
    int dl = pct_gain(g->stats.leadership_base, ec->tribute_leadership_pct);
    int dp = pct_gain(g->stats.spell_power, ec->tribute_magic_pct);
    int dm = pct_gain(g->stats.max_spells, ec->tribute_magic_pct);
    g->stats.leadership_base += dl;
    g->stats.leadership_current += dl;
    g->stats.spell_power += dp;
    g->stats.max_spells += dm;
    g->stats.tributes++;
    if (gain) { gain->leadership = dl; gain->spell_power = dp; gain->max_spells = dm; }
    return true;
}

int GameArmyTotalLeadership(const Game *g) {
    int total = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (!g->army[i].id[0]) continue;
        const TroopDef *t = troop_by_id(g->army[i].id);
        if (!t) continue;
        total += t->hit_points * g->army[i].count;
    }
    return total;
}

int GameArmyStackCount(const Game *g) {
    int n = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++)
        if (g->army[i].id[0]) n++;
    return n;
}

int GameComputeScore(const Game *g) {
    if (!g) return 0;
    const ResScoring *sc = &g->res->economy.scoring;
    int score = sc->per_villain  * GameVillainsCaught(g)
              + sc->per_artifact * GameArtifactsFound(g)
              + sc->per_castle   * GameCastlesOwned(g)
              - sc->kill_penalty * g->stats.followers_killed;
    int d = (int)g->character.difficulty;
    if (sc->easy_halves && d == DIFFICULTY_EASY) {
        score /= 2;
    } else if (d >= 1 && d < 5) {
        score *= sc->difficulty_multiplier[d];
    }
    if (score < 0) score = 0;
    return score;
}

int GameVillainsCaught(const Game *g) {
    // villains_caught[] is keyed by VillainDef.index, which load validation
    // constrains to [0, villains_count) and uniqueness.
    int n = 0;
    for (int i = 0; i < g->contract.villain_count; i++)
        if (g->contract.villains_caught[i]) n++;
    return n;
}

int GameArtifactsFound(const Game *g) {
    int n = 0;
    for (int i = 0; i < g->artifacts.count; i++)
        if (g->artifacts.found[i]) n++;
    return n;
}

// A town whose informant reports on the sacred artifacts rather than on a
// castle: which one he names, and where it lies. Candidates are the artifacts
// not yet found -- salt-placed ones from g->placements[], JSON-authored ones
// from the zone catalog -- and the choice is a pure hash of the seed and the
// town, never a draw from the game RNG, so a given seed always answers alike.
// As artifacts are claimed the list shrinks and the town names another.
bool GameTownArtifactIntel(const Game *g, const char *town_id,
                           char *out_zone, int zone_cap, int *out_x, int *out_y) {
    if (!g || !g->res || !town_id) return false;
    const Resources *r = g->res;

    // Gather the unfound ones, in a fixed order (placements, then catalog).
    int cand_cap = g->placement_count;
    for (int z = 0; z < r->zone_count; z++) cand_cap += r->zones[z].artifact_count;
    if (cand_cap <= 0) return false;
    struct IntelCand { const char *zone; int x, y; } *cand = malloc((size_t)cand_cap * sizeof *cand);
    if (!cand) return false;
    int n = 0;
    for (int i = 0; i < g->placement_count && n < cand_cap; i++) {
        const SaltedPlacement *p = &g->placements[i];
        if (p->kind != INTERACT_ARTIFACT) continue;
        const ArtifactDef *a = artifact_by_id(p->id);
        if (!a || g->artifacts.found[a->index]) continue;
        cand[n].zone = p->zone; cand[n].x = p->x; cand[n].y = p->y; n++;
    }
    for (int z = 0; z < r->zone_count && n < cand_cap; z++) {
        const ResZone *rz = &r->zones[z];
        for (int i = 0; i < rz->artifact_count && n < cand_cap; i++) {
            const ArtifactDef *a = artifact_by_id(rz->artifacts[i].id);
            if (!a || g->artifacts.found[a->index]) continue;
            cand[n].zone = rz->id; cand[n].x = rz->artifacts[i].x; cand[n].y = rz->artifacts[i].y; n++;
        }
    }
    if (n == 0) { free(cand); return false; }

    // A pure hash of the seed and the town's id: stable, and different towns
    // name different chests.
    uint64_t h = g->seed * 1099511628211ull;
    for (const char *p = town_id; *p; p++) h = (h ^ (unsigned char)*p) * 1099511628211ull;
    h ^= h >> 29;
    int pick = (int)(h % (uint64_t)n);

    if (out_zone && zone_cap > 0) snprintf(out_zone, (size_t)zone_cap, "%s", cand[pick].zone);
    if (out_x) *out_x = cand[pick].x;
    if (out_y) *out_y = cand[pick].y;
    free(cand);
    return true;
}

int GameCastlesOwned(const Game *g) {
    int n = 0;
    for (int i = 0; i < g->castle_count; i++) {
        if (!g->castles[i].id[0]) continue;
        if (g->castles[i].owner_kind == CASTLE_OWNER_PLAYER) n++;
    }
    return n;
}

// ---- Treasure chest rolls ---------------
// Curves and value ranges live in res->economy.chest (game.json economy.chest).
// Defaults match ..503 when game.json omits the block.

// Deterministic pseudo-random per (seed, x, y) so the same chest gives the
// same outcome on replay.
static unsigned chest_rand(const Game *g, int x, int y, unsigned salt) {
    unsigned h = (unsigned)g->seed ^ 0xC0FFEEu;
    h ^= (unsigned)(x * 131u);
    h ^= (unsigned)(y * 97u);
    h ^= salt;
    h = h * 1664525u + 1013904223u;
    return h;
}

// The gold chest: a purse the player takes as gold or as leadership. The
// caller runs the choice and calls GameAcceptChestGold or
// GameAcceptChestLeadership.
static ChestOutcome chest_gold(const Game *g, int x, int y, int zi,
                               ChestPending *out_pending,
                               char *out_body, size_t out_sz) {
    const ResChest *ch = &g->res->economy.chest;
    int points = (int)(chest_rand(g, x, y, 2) %
                       (unsigned)(ch->gold_max[zi] > 0 ? ch->gold_max[zi] : 1)) + 1;
    points += ch->gold_min[zi];
    int gold = points * 100;
    int leadership = gold / 50;
    if (GameHasPower(g, ARTIFACT_POWER_DOUBLE_LEADERSHIP)) leadership *= 2;
    if (out_pending) {
        out_pending->pending_gold = gold;
        out_pending->pending_leadership = leadership;
    }
    char gbuf[16], lbuf[16];
    snprintf(gbuf, sizeof gbuf, "%d", gold);
    snprintf(lbuf, sizeof lbuf, "%d", leadership);
    ResTemplateVar vars[] = {
        { "GOLD", gbuf }, { "LEADERSHIP", lbuf },
    };
    resources_format_template(out_body, out_sz, g->res->banners.chest_gold,
                              vars, (int)(sizeof vars / sizeof vars[0]));
    return CHEST_OUTCOME_GOLD;
}

ChestOutcome GameRollChest(Game *g, int zone_index, int x, int y,
                           char *out_body, int out_sz,
                           ChestPending *out_pending) {
    if (out_pending) {
        out_pending->pending_gold = 0;
        out_pending->pending_leadership = 0;
    }
    int zi = (zone_index >= 0 && zone_index < 4) ? zone_index : 0;
    const ResChest *ch = &g->res->economy.chest;
    const ResBanners *bn = &g->res->banners;
    int chance = (int)(chest_rand(g, x, y, 1) % 100u) + 1;   // 1..100

    // A chest the pack pinned a purse to ("gold": N) always holds exactly that,
    // rolling nothing: the reward a vista or a guarded place is worth is the
    // pack's to decide (REQ-230d).
    int pinned = 0;
    if (zone_index >= 0 && zone_index < g->res->zone_count) {
        const ResZone *z = &g->res->zones[zone_index];
        for (int i = 0; i < z->chest_count; i++)
            if (z->chests[i].x == x && z->chests[i].y == y && z->chests[i].gold > 0)
                pinned = z->chests[i].gold;
    }
    if (pinned > 0) {
        int leadership = pinned / 50;
        if (GameHasPower(g, ARTIFACT_POWER_DOUBLE_LEADERSHIP)) leadership *= 2;
        if (out_pending) {
            out_pending->pending_gold = pinned;
            out_pending->pending_leadership = leadership;
        }
        char gbuf[16], lbuf[16];
        snprintf(gbuf, sizeof gbuf, "%d", pinned);
        snprintf(lbuf, sizeof lbuf, "%d", leadership);
        ResTemplateVar vars[] = { { "GOLD", gbuf }, { "LEADERSHIP", lbuf } };
        resources_format_template(out_body, out_sz, bn->chest_gold, vars, 2);
        return CHEST_OUTCOME_GOLD;
    }

    if (chance < ch->chance_gold[zi])
        return chest_gold(g, x, y, zi, out_pending, out_body, out_sz);
    if (chance < ch->chance_commission[zi]) {
        int points = (int)(chest_rand(g, x, y, 3) %
                           (unsigned)(ch->commission_max[zi] > 0 ? ch->commission_max[zi] : 1)) + 1;
        points += ch->commission_min[zi];
        g->stats.commission_weekly += points;
        char pbuf[16];
        snprintf(pbuf, sizeof pbuf, "%d", points);
        ResTemplateVar vars[] = { { "POINTS", pbuf } };
        resources_format_template(out_body, out_sz, bn->chest_commission,
                                  vars, 1);
        return CHEST_OUTCOME_COMMISSION;
    }
    if (chance < ch->chance_spell_power[zi]) {
        int points = 1;
        g->stats.spell_power += points;
        char pbuf[16];
        snprintf(pbuf, sizeof pbuf, "%d", points);
        ResTemplateVar vars[] = { { "POINTS", pbuf } };
        resources_format_template(out_body, out_sz, bn->chest_spell_power,
                                  vars, 1);
        return CHEST_OUTCOME_SPELL_POWER;
    }
    if (chance < ch->chance_max_spells[zi]) {
        int points = ch->max_spells_base[zi];
        if (GameHasPower(g, ARTIFACT_POWER_DOUBLE_MAX_SPELLS)) points *= 2;
        g->stats.max_spells += points;
        char pbuf[16];
        snprintf(pbuf, sizeof pbuf, "%d", points);
        ResTemplateVar vars[] = { { "POINTS", pbuf } };
        resources_format_template(out_body, out_sz, bn->chest_max_spells,
                                  vars, 1);
        return CHEST_OUTCOME_MAX_SPELLS;
    }
    if (chance < ch->chance_new_spell[zi]) {
        int sc = spells_count();
        if (sc <= 0) {
            resources_format_template(out_body, out_sz, bn->chest_empty, NULL, 0);
            return CHEST_OUTCOME_EMPTY;
        }
        int spell_type = (int)(chest_rand(g, x, y, 4) % (unsigned)sc);
        int spell_num  = (int)(chest_rand(g, x, y, 5) % (unsigned)(zi + 1)) + 1;
        // Charges, not learning: only a temple teaches a spell (#157). Where
        // each spell is capped, the chest gives no more than there is room for.
        // A spell already at its cap makes it a gold chest.
        if (g->res && g->res->economy.spell_limit_per_spell) {
            int room = GameSpellRoom(g, spell_type);
            if (room <= 0)
                return chest_gold(g, x, y, zi, out_pending, out_body, out_sz);
            if (spell_num > room) spell_num = room;
        }
        g->spells.counts[spell_type] += spell_num;
        const SpellDef *sp = spell_by_index(spell_type);
        char cbuf[16];
        snprintf(cbuf, sizeof cbuf, "%d", spell_num);
        ResTemplateVar vars[] = {
            { "COUNT", cbuf },
            { "SPELL", sp ? sp->name : "(unknown)" },
        };
        resources_format_template(out_body, out_sz, bn->chest_new_spell,
                                  vars, 2);
        return CHEST_OUTCOME_NEW_SPELL;
    }

    resources_format_template(out_body, out_sz, bn->chest_empty, NULL, 0);
    return CHEST_OUTCOME_EMPTY;
}

void GameAcceptChestGold(Game *g, int gold) {
    if (!g || gold <= 0) return;
    g->stats.gold += gold;
    {
        char tag[48];
        snprintf(tag, sizeof tag, "chest:gold:%d", gold);
        recorder_capture(tag);
    }
    // Note: no tune here -- the chest tune already played on chest
    // entry, matching take_chest's
    // "play happy music first, regardless of outcome" model.
}

void GameAcceptChestLeadership(Game *g, int leadership) {
    if (!g || leadership <= 0) return;
    g->stats.leadership_base += leadership;
    g->stats.leadership_current += leadership;
    {
        char tag[48];
        snprintf(tag, sizeof tag, "chest:lead:%d", leadership);
        recorder_capture(tag);
    }
    // See note above re: tune already played on chest entry.
}

// ---- Dwelling helpers ----------------------------------------------------

const TroopDef *GameDwellingTroopAt(const Game *g, const char *dwelling_kind,
                                    int x, int y) {
    if (!g || !dwelling_kind || !dwelling_kind[0]) return NULL;
    // Collect all troops whose `dwelling` field matches.
    int total = troops_count();
    if (total <= 0) return NULL;
    int *cand = malloc((size_t)total * sizeof *cand);
    if (!cand) return NULL;
    int n = 0;
    for (int i = 0; i < total; i++) {
        const TroopDef *t = troop_by_index(i);
        if (!t) continue;
        if (strcmp(t->dwelling, dwelling_kind) == 0) cand[n++] = i;
    }
    if (n == 0) { free(cand); return NULL; }
    // Deterministic pick: (seed, x, y) -> index into cand[].
    unsigned h = (unsigned)g->seed;
    h ^= (unsigned)(x * 131u);
    h ^= (unsigned)(y * 97u);
    h = h * 1664525u + 1013904223u;
    int pick = (int)(h % (unsigned)n);
    const TroopDef *out = troop_by_index(cand[pick]);
    free(cand);
    return out;
}

//  verbatim:
//
//   free_leadership = game->leadership
//   for i in 0..4:
//     if player_troops[i] == troop_id:
//       free_leadership -= troops[troop_id].hit_points * player_numbers[i]
//       break
//   return free_leadership
//
// Only the slot already holding `troop_id` is subtracted; other troops
// are ignored. The recruit ceiling is `free_leadership / hp`.
int GameMaxRecruitable(const Game *g, const char *troop_id) {
    if (!g || !troop_id) return 0;
    const TroopDef *t = troop_by_id(troop_id);
    if (!t || t->hit_points <= 0) return 0;
    // Recruit cap: (leadership_current - same_troop_leadership) / hp
    // Crucially, *other* troop slots do NOT reduce the cap. This is
    // why a fresh Knight on Easy can recruit 30 Militia + 8 Archers
    // + 10 Pikemen all at once even though the totals exceed leadership
    // 100 -- each troop type is checked independently.
    int same_troop_consumed = 0;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (strcmp(g->army[i].id, troop_id) == 0) {
            same_troop_consumed += g->army[i].count * t->hit_points;
        }
    }
    int free_leadership = g->stats.leadership_current - same_troop_consumed;
    if (free_leadership < 0) return 0;
    return free_leadership / t->hit_points;
}

// A zone's kind ("hills") as the troop catalog names it ("hill"), so
// GameDwellingTroopAt's strcmp against troop->dwelling matches.
static const char *dwelling_kind_normalize(const char *kind) {
    const char *k = DwellingCatalogKind(DwellingInteractFromKind(kind));
    return k ? k : (kind ? kind : "");
}

// enforce_dwelling: eagerly materialize a
// DwellingState row for one tile: pick troop by kind+seed and set
// count = max_population. Idempotent -- returns the existing row on
// re-touch without overwriting it.
DwellingState *game_enforce_dwelling(Game *g, const char *zone, int x, int y,
                                     const char *dwelling_kind) {
    if (!g || !zone || !zone[0]) return NULL;
    // Find existing.
    for (int i = 0; i < g->dwelling_count; i++) {
        if (g->dwellings[i].x == x && g->dwellings[i].y == y &&
            strcmp(g->dwellings[i].zone, zone) == 0) {
            return &g->dwellings[i];
        }
    }
    // Create new.
    if (!GameReserveDwellings(g, g->dwelling_count + 1)) return NULL;
    DwellingState *d = &g->dwellings[g->dwelling_count++];
    memset(d, 0, sizeof(*d));
    game_copy_id(d->zone, sizeof(d->zone), zone);
    d->x = x; d->y = y;
    const TroopDef *t = GameDwellingTroopAt(g,
                                            dwelling_kind_normalize(dwelling_kind),
                                            x, y);
    if (t) {
        game_copy_id(d->troop_id, sizeof(d->troop_id), t->id);
        d->max_population = t->max_population;
        // Dwellings start at full population.
        d->count = t->max_population;
    }
    return d;
}

// Variant that pins the troop directly (populate_dwelling: troop
// is decided up-front, kind is derived from troops[id].dwells). Used by
// salt_continent so per-zone preferred-troop lists / dwelling_range work.
DwellingState *game_enforce_dwelling_pinned(Game *g, const char *zone,
                                            int x, int y,
                                            const char *troop_id) {
    if (!g || !zone || !zone[0] || !troop_id || !troop_id[0]) return NULL;
    for (int i = 0; i < g->dwelling_count; i++) {
        if (g->dwellings[i].x == x && g->dwellings[i].y == y &&
            strcmp(g->dwellings[i].zone, zone) == 0) {
            return &g->dwellings[i];
        }
    }
    const TroopDef *t = troop_by_id(troop_id);
    if (t && !GameReserveDwellings(g, g->dwelling_count + 1)) return NULL;
    if (!t) return NULL;
    DwellingState *d = &g->dwellings[g->dwelling_count++];
    memset(d, 0, sizeof(*d));
    game_copy_id(d->zone, sizeof(d->zone), zone);
    d->x = x; d->y = y;
    game_copy_id(d->troop_id, sizeof(d->troop_id), t->id);
    d->max_population = t->max_population;
    d->count = t->max_population;
    return d;
}

// Public accessor -- kept for the visit-handler path (main.c). Eager
// init in GameInit means the row almost always exists already; this
// just looks it up.
DwellingState *GameTouchDwelling(Game *g, const char *zone, int x, int y,
                                 const char *dwelling_kind) {
    return game_enforce_dwelling(g, zone, x, y, dwelling_kind);
}

void GameTempDeath(Game *g, Map *map, Fog *fog, const Resources *res) {
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        g->army[i].id[0] = '\0';
        g->army[i].count = 0;
    }
    g->stats.siege_weapons = 0;

    // The consolation stack (pack knob; fizzles to an empty army when the
    // configured troop id is absent from the catalog).
    const TroopDef *t = troop_by_id(res->tuning.temp_death_troop);
    if (t) {
        size_t k = 0;
        while (k + 1 < sizeof(g->army[0].id) && t->id[k]) {
            g->army[0].id[k] = t->id[k]; k++;
        }
        g->army[0].id[k] = '\0';
        g->army[0].count = res->tuning.temp_death_count;
    }

    // Forfeit the boat rental on defeat. Without this, GameSwitchZone
    // below would move the boat to the home spawn, magically materializing
    // it on land at the King's castle. Boat is rental property; losing
    // it matches the rest of temp_death's "forfeit everything" semantics
    // (army wiped, siege weapons revoked).
    g->boat.has_boat = false;
    g->boat.x = -1;
    g->boat.y = -1;
    g->boat.zone[0] = '\0';

    for (int zi = 0; zi < res->zone_count; zi++) {
        if (!res->zones[zi].is_home) continue;
        GameSwitchZone(g, map, fog, res->zones[zi].id);
        g->position.x = res->zones[zi].home_spawn_x;
        g->position.y = res->zones[zi].home_spawn_y;
        g->position.last_x = g->position.x;
        g->position.last_y = g->position.y;
        break;
    }
    g->character.mount = MOUNT_RIDE;
    g->travel_mode = TRAVEL_WALK;
}

bool GameSwitchZone(Game *g, Map *map, Fog *fog, const char *zone_id) {
    if (!g || !map || !fog || !zone_id) return false;
    const ResZone *z = resources_zone_by_id(g->res, zone_id);
    if (!z) return false;

    // Snapshot the outgoing zone's fog into per-continent storage so
    // we can restore it later ([continent][y][x]).
    {
        const ResZone *old_z = (g->position.zone[0])
            ? resources_zone_by_id(g->res, g->position.zone) : NULL;
        if (old_z) {
            int old_zi = (int)(old_z - g->res->zones);
            if (old_zi >= 0 && old_zi < g->world.zone_count) {
                FogCopy(&g->world.continent_fog[old_zi], fog);
            }
        }
    }

    // The zone sailed from picks the landing ("arrivals"), else the spawn.
    char from[sizeof g->position.zone];
    game_copy_id(from, sizeof from, g->position.zone);

    if (!GameReloadZoneMap(g, map, zone_id)) return false;
    // Move hero to the arrival point.
    game_copy_id(g->position.zone, sizeof(g->position.zone), zone_id);
    g->position.x = map->hero_spawn_x;
    g->position.y = map->hero_spawn_y;
    if (strcmp(from, zone_id) != 0)
        resources_zone_arrival(z, from, &g->position.x, &g->position.y);
    g->position.last_x = g->position.x;
    g->position.last_y = g->position.y;

    // Match the hero's travel mode to the spawn terrain. Cross-continent
    // arrivals can land on water (open ocean entry) or land (the home
    // continent's hero_spawn is on grass). Without this, sailing back
    // to Continentia would leave the hero in a boat on land.
    {
        const Tile *spawn = MapGetTile(map, g->position.x, g->position.y);
        bool on_water = spawn && spawn->terrain == TERRAIN_WATER &&
                        !spawn->is_bridge;
        if (on_water) {
            g->travel_mode = TRAVEL_BOAT;
            g->boat.has_boat = true;
            g->boat.x = g->position.x;
            g->boat.y = g->position.y;
            memset(g->boat.zone, 0, sizeof g->boat.zone);   // zero the tail before copy (canonical image)
            game_copy_id(g->boat.zone, sizeof(g->boat.zone), zone_id);
        } else {
            g->travel_mode = TRAVEL_WALK;
            // Keep the boat; place it at the spawn so the player can
            // re-board to sail away. The renderer hides the boat tile
            // when the hero is standing on it.
            if (g->boat.has_boat) {
                g->boat.x = g->position.x;
                g->boat.y = g->position.y;
                memset(g->boat.zone, 0, sizeof g->boat.zone);   // zero the tail before copy (canonical image)
                game_copy_id(g->boat.zone, sizeof(g->boat.zone), zone_id);
            }
        }
    }

    // Restore fog for the incoming zone. Continents revisited by boat or
    // teleport keep whatever the player had previously revealed; first
    // visits start blank and reveal around spawn.
    int zi = (int)(z - g->res->zones);
    if (zi >= 0 && zi < g->world.zone_count && g->world.zones_discovered[zi]) {
        FogCopy(fog, &g->world.continent_fog[zi]);
    } else {
        FogInit(fog);
    }
    FogRevealFor(g->res, fog, map, g->position.x, g->position.y);
    // Mark discovered.
    if (zi >= 0 && zi < g->world.zone_count) g->world.zones_discovered[zi] = true;
    {
        char tag[64];
        snprintf(tag, sizeof tag, "zone:%s", zone_id);
        recorder_capture(tag);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Gate spells (Town Gate / Castle Gate): destination lists + teleport.
//
// The engine owns visited-eligibility and the boat semantics so the shell UI
// and the autoplay responder share one source of truth.
// ---------------------------------------------------------------------------

// Case-insensitive name compare (alphabetical), then zone for a stable tie.
static int gate_name_icmp(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
static int gate_dest_cmp(const void *a, const void *b) {
    const GateDestination *da = a, *db = b;
    int c = gate_name_icmp(da->name, db->name);
    return c ? c : strcmp(da->zone, db->zone);
}

int GameGateDestinations(const Game *g, GateDestKind kind,
                         GateDestination *out, int cap) {
    if (!g || !out || cap <= 0) return 0;
    int n = 0;
    if (kind == GATE_DEST_CASTLE) {
        int cn = g->castle_count;
        for (int i = 0; i < cn && n < cap; i++) {
            if (!g->castles[i].visited) continue;
            const ResCastle *rc = &g->res->castles[i];
            if (resources_castle_is_home(rc)) continue;
            if (!rc->zone[0]) continue;
            snprintf(out[n].name, sizeof out[n].name, "%s",
                     rc->name[0] ? rc->name : rc->id);
            snprintf(out[n].zone, sizeof out[n].zone, "%s", rc->zone);
            out[n].x = rc->gate_x >= 0 ? rc->gate_x : rc->x;
            out[n].y = rc->gate_y >= 0 ? rc->gate_y : rc->y;
            n++;
        }
    } else {
        int tn = g->town_count;
        for (int i = 0; i < tn && n < cap; i++) {
            if (!g->towns[i].visited) continue;
            const ResTown *rt = &g->res->towns[i];
            if (!rt->zone[0]) continue;
            snprintf(out[n].name, sizeof out[n].name, "%s",
                     rt->name[0] ? rt->name : rt->id);
            snprintf(out[n].zone, sizeof out[n].zone, "%s", rt->zone);
            out[n].x = rt->gate_x >= 0 ? rt->gate_x : rt->x;
            out[n].y = rt->gate_y >= 0 ? rt->gate_y : rt->y;
            n++;
        }
    }
    if (n > 1) qsort(out, (size_t)n, sizeof out[0], gate_dest_cmp);
    return n;
}

bool GameGateTeleport(Game *g, Map *map, Fog *fog,
                      const GateDestination *dest, const char *spell_id) {
    if (!g || !map || !fog || !dest || !dest->zone[0]) return false;

    // Leave any current boat behind at the cast tile (origin zone). This must
    // happen BEFORE the zone switch, which would otherwise carry the boat to
    // the destination spawn. We snapshot the "left behind" boat and restore it
    // after the switch so the boat's zone stays the origin zone.
    BoatState left_behind;
    memset(&left_behind, 0, sizeof left_behind);   // zero padding + zone tail: an uninitialized local
                                                   // copies stack garbage into g->boat below, which the
                                                   // serialized/fingerprinted byte image would then carry
                                                   // (a determinism regression across otherwise-equal runs).
    bool have_left = false;
    if (g->travel_mode == TRAVEL_BOAT) {
        left_behind.has_boat = true;
        left_behind.x = g->position.x;
        left_behind.y = g->position.y;
        game_copy_id(left_behind.zone, sizeof left_behind.zone, g->position.zone);
        have_left = true;
    } else if (g->boat.has_boat) {
        // A docked boat (hero on foot) also stays where it is.
        left_behind = g->boat;
        have_left = true;
    }

    if (!GameSwitchZone(g, map, fog, dest->zone)) return false;

    // Always arrive on foot at the gate landing tile.
    if (dest->x >= 0 && dest->y >= 0) {
        g->position.x = dest->x;
        g->position.y = dest->y;
        g->position.last_x = dest->x;
        g->position.last_y = dest->y;
    }
    g->travel_mode = TRAVEL_WALK;

    // Restore the boat to where it was left (origin zone), overriding the
    // re-attach GameSwitchZone performed at the destination spawn.
    if (have_left) {
        g->boat = left_behind;
    }

    int sidx = spell_index_by_id(spell_id);
    if (sidx >= 0 && g->spells.counts[sidx] > 0) g->spells.counts[sidx]--;
    return true;
}

int GamePickAstrologyCreature(const Game *g, int week_id) {
    if (!g) return 0;
    int tc = troops_count();
    if (tc < 1) tc = 1;
    // Every 4th week is peasants `).
    if ((week_id & 3) == 0) return 0;
    unsigned h = (unsigned)g->seed ^ (unsigned)week_id;
    h = h * 1664525u + 1013904223u;
    int idx = 1 + (int)(h % (unsigned)(tc > 1 ? tc - 1 : 1));
    if (idx >= tc) idx = 0;
    return idx;
}

int GamePickRenewedSpell(const Game *g, int week_id) {
    if (!g || !g->res || !g->res->economy.spell_weekly_renewal) return -1;
    int n = 0;
    for (int i = 0; i < g->spells.count; i++) n += g->spells.learned[i];
    if (n == 0) return -1;
    // The astrology's seed and week, on a draw of its own.
    unsigned h = ((unsigned)g->seed ^ (unsigned)week_id) ^ 0x5bd1e995u;
    h = h * 1664525u + 1013904223u;
    h = h * 1664525u + 1013904223u;
    int k = (int)((h >> 8) % (unsigned)n);
    for (int i = 0; i < g->spells.count; i++)
        if (g->spells.learned[i] && k-- == 0) return i;
    return -1;
}

void GameRenewSpell(Game *g, int idx) {
    if (!g || idx < 0 || idx >= g->spells.count) return;
    if (g->spells.counts[idx] < g->stats.max_spells) g->spells.counts[idx] = g->stats.max_spells;
}

const char *GameApplyAstrology(Game *g, int troop_idx) {
    if (!g) return "";
    const TroopDef *at = troop_by_index(troop_idx);
    if (!at) return "";

    // Repopulate dwellings whose troop == astrology creature (
    // play.c:1024-1026). Non-matching dwellings carry their current
    // population over unchanged -- population only refills on the troop's
    // astrology week.
    for (int i = 0; i < g->dwelling_count; i++) {
        DwellingState *d = &g->dwellings[i];
        if (!d->troop_id[0]) continue;
        if (strcmp(d->troop_id, at->id) == 0) {
            d->count = d->max_population;
        }
    }

    // Week of the Peasants (index 0) converts absorb-ability troops in
    // the player's army to peasants (ghosts -> peasants trick;
    // play.c:1007-1011).
    if (troop_idx == 0) {
        for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
            if (!g->army[s].id[0] || g->army[s].count == 0) continue;
            const TroopDef *t = troop_by_id(g->army[s].id);
            if (!t) continue;
            if (t->abilities & TROOP_ABIL_ABSORB) {
                game_copy_id(g->army[s].id, sizeof(g->army[s].id), at->id);
            }
        }
    }

    return at->id;
}

bool GameTroopFlies(const TroopDef *t) {
    return t && (t->abilities & TROOP_ABIL_FLY) && t->skill_level >= 2;
}

bool GamePlayerCanFly(const Game *g) {
    if (!g) return false;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (!g->army[i].id[0] || g->army[i].count == 0) continue;
        if (!GameTroopFlies(troop_by_id(g->army[i].id))) return false;
    }
    return true;
}

// Mount transitions -- the ONE copy of the legality the shell's FLY/LAND keys
// apply (shell_actions.c) and autoplay's fly-leg rides on.
bool GameMountFly(Game *g) {
    if (!g || g->character.mount != MOUNT_RIDE || !GamePlayerCanFly(g)) return false;
    g->character.mount = MOUNT_FLY;
    return true;
}

bool GameCanLandAt(const Game *g, const Map *map, int x, int y) {
    (void)g;
    const Tile *t = map ? MapGetTile(map, x, y) : NULL;
    return t && t->terrain == TERRAIN_GRASS &&
           t->interactive == INTERACT_NONE && !t->blocks_foot;
}

bool GameLandHere(Game *g, const Map *map) {
    if (!g || g->character.mount != MOUNT_FLY) return false;
    if (!GameCanLandAt(g, map, g->position.x, g->position.y)) return false;
    g->character.mount = MOUNT_RIDE;
    return true;
}

const char *GameNumberName(const Game *g, int count) {
    if (!g || count < 1) return "";
    const Resources *r = g->res;
    if (!r || r->number_name_count <= 0) return "";
    // Thresholds listed high-to-low; pick first label whose min <= count.
    for (int i = 0; i < r->number_name_count; i++) {
        if (count >= r->number_name_thresholds[i])
            return r->number_name_labels[i];
    }
    return r->number_name_labels[r->number_name_count - 1];
}

// Append `frag` to `buf` at *off, never past `cap`.
static void report_append(char *buf, size_t cap, size_t *off, const char *frag) {
    if (*off + 1 >= cap) return;
    int n = snprintf(buf + *off, cap - *off, "%s", frag);
    if (n < 0) return;
    if ((size_t)n >= cap - *off) { *off = cap - 1; return; }
    *off += (size_t)n;
}

// at_gate: the castle names itself in the page's title, so the report opens
// with castle_gate_owner ("Under %OWNER%'s rule.") instead of naming it.
static bool castle_report(const Game *g, const char *castle_id, char *out, size_t cap,
                          bool at_gate) {
    if (!out || cap == 0) return false;
    out[0] = '\0';
    if (!g || !g->res || !castle_id) return false;
    const ResBanners *bn = &g->res->banners;
    const ResCastle *rc = resources_castle_by_id(g->res, castle_id);
    const CastleRecord *cr = GameFindCastleConst(g, castle_id);
    if (!rc || !cr) return false;

    char buf[512];
    size_t off = 0;
    char tmp[256];
    buf[0] = '\0';
    bool short_form = at_gate && bn->castle_gate_owner[0];
    if (!short_form) {
        ResTemplateVar vars[] = { { "NAME", rc->name[0] ? rc->name : cr->id } };
        resources_format_template(tmp, sizeof tmp, bn->town_intel_castle_under, vars, 1);
        report_append(buf, sizeof buf, &off, tmp);
    }
    const char *owner = bn->town_intel_owner_none;
    switch (cr->owner_kind) {
        case CASTLE_OWNER_PLAYER:   owner = bn->town_intel_owner_player; break;
        case CASTLE_OWNER_MONSTERS: owner = bn->town_intel_owner_none;   break;
        case CASTLE_OWNER_VILLAIN: {
            const VillainDef *v = villain_by_id(cr->villain_id);
            owner = (v && v->name[0]) ? v->name : cr->villain_id;
            break;
        }
        case CASTLE_OWNER_SPECIAL:  owner = bn->town_intel_owner_king;   break;
    }
    {
        ResTemplateVar vars[] = { { "OWNER", owner } };
        resources_format_template(tmp, sizeof tmp,
                                  short_form ? bn->castle_gate_owner : bn->town_intel_owner_rule,
                                  vars, 1);
        report_append(buf, sizeof buf, &off, tmp);
    }
    // A pack that reports at the gate (#71) gives each troop one line, its
    // stacks' counts summed (#139); the original listed every slot, so two
    // stacks of one troop read twice, and King's Bounty keeps that.
    Unit lines[GAME_ARMY_SLOTS];
    int n_lines = 0;
    bool merge = g->res->world.castle_gate_report;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        const Unit *u = &cr->garrison[i];
        if (!u->id[0] || u->count == 0) continue;
        int k = 0;
        if (merge)
            while (k < n_lines && strcmp(lines[k].id, u->id) != 0) k++;
        else
            k = n_lines;
        if (k == n_lines) lines[n_lines++] = *u;
        else              lines[k].count += u->count;
    }
    int stacks_shown = 0;
    for (int i = 0; i < n_lines && off + 1 < sizeof(buf); i++) {
        const Unit *u = &lines[i];
        const TroopDef *t = troop_by_id(u->id);
        const char *tname = (t && t->name[0]) ? t->name : u->id;
        const char *count_label = GameNumberName(g, u->count);
        if (count_label[0]) {
            ResTemplateVar vars[] = { { "LABEL", count_label }, { "TROOP", tname } };
            resources_format_template(tmp, sizeof tmp, bn->town_intel_count_named, vars, 2);
        } else {
            char cbuf[16];
            snprintf(cbuf, sizeof cbuf, "%d", u->count);
            ResTemplateVar vars[] = { { "COUNT", cbuf }, { "TROOP", tname } };
            resources_format_template(tmp, sizeof tmp, bn->town_intel_count_numeric, vars, 2);
        }
        report_append(buf, sizeof buf, &off, tmp);
        stacks_shown++;
    }
    if (!stacks_shown && off + 1 < sizeof(buf)) {
        // No garrison rolled: a monster castle reads as "various groups".
        const char *src = (cr->owner_kind == CASTLE_OWNER_MONSTERS)
            ? bn->town_intel_monsters_generic
            : bn->town_intel_no_garrison;
        resources_format_template(tmp, sizeof tmp, src, NULL, 0);
        report_append(buf, sizeof buf, &off, tmp);
    }
    snprintf(out, cap, "%s", buf);
    return true;
}

bool GameCastleReport(const Game *g, const char *castle_id, char *out, size_t cap) {
    return castle_report(g, castle_id, out, cap, false);
}

bool GameCastleGateReport(const Game *g, const char *castle_id, char *out, size_t cap) {
    return castle_report(g, castle_id, out, cap, true);
}

CastleRecord *GameFindCastle(Game *g, const char *castle_id) {
    if (!g || !castle_id || !castle_id[0]) return NULL;
    for (int i = 0; i < g->castle_count; i++) {
        if (strcmp(g->castles[i].id, castle_id) == 0) return &g->castles[i];
    }
    return NULL;
}

const CastleRecord *GameFindCastleConst(const Game *g, const char *castle_id) {
    return GameFindCastle((Game *)g, castle_id);
}

FoeState *GameFindFoe(Game *g, const char *placement_id) {
    if (!g || !placement_id || !placement_id[0]) return NULL;
    // Placement IDs (wandering_army_NNN) are NOT unique across zones --
    // the same numbering restarts in every zone's `wandering_armies[]`.
    // Scope the match to the hero's current zone so defeating a foe on
    // one continent doesn't silently mark a same-named foe on another
    // continent as dead, leaving the one you actually fought alive and
    // pursuing forever.
    const char *zone = g->position.zone;
    for (int i = 0; i < g->foe_count; i++) {
        if (strcmp(g->foes[i].placement_id, placement_id) != 0) continue;
        if (zone[0] && strcmp(g->foes[i].zone, zone) != 0) continue;
        return &g->foes[i];
    }
    return NULL;
}

const FoeState *GameFindFoeConst(const Game *g, const char *placement_id) {
    return GameFindFoe((Game *)g, placement_id);
}

bool GameFriendlyFoeOriginAt(const Game *g, const char *zone, int x, int y) {
    if (!g || !zone) return false;
    // Match regardless of `alive`: a friendly foe's origin chest slot is spent
    // the moment the game is salted -- it stays suppressed after the foe is
    // recruited or killed so the slot never reverts to an openable chest.
    for (int i = 0; i < g->foe_count; i++) {
        const FoeState *f = &g->foes[i];
        if (!f->friendly) continue;
        if (f->origin_x != x || f->origin_y != y) continue;
        if (strcmp(f->zone, zone) != 0) continue;
        return true;
    }
    return false;
}

// Port of foe_closest_offset (OpenKB's play.c:1738).
// Walk each on-screen foe within 2 tiles of the hero's last
// position one step toward it. The step is picked by evaluating all 9
// cells of the foe's 3x3 neighborhood (including stay-put) and choosing
// the cell with the minimum Euclidean distance to the target. Non-center
// cells that aren't walkable get bumped to a max-distance sentinel so
// they're never picked. The center cell always keeps its real distance,
// which lets foes stay put when every move would be worse than waiting.
//
// Target is hero's `last_*` (the tile the hero just vacated), not current
// position. Combat on
// collision is handled by the step-on-foe interact path, not here; this
// function never steps onto the hero's current tile.
