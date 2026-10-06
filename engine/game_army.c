// engine/game_army.c -- the hero's army: recruiting, adding and compacting
// stacks, and moving them in and out of castle garrisons.

#include "game.h"
#include "game_internal.h"
#include "map.h"
#include "adventure.h"
#include "ui_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Recruit-location guard (mirrors the in_town town-core guards): a troop may only
// be bought where a player can buy it. Home-pool troops (catalog dwelling=="castle")
// require the hero on the home-castle gate (position.home_castle set). Any other
// troop is a dwelling troop: it requires the hero standing on a dwelling tile whose
// offered troop matches AND that still has population. Returns the legal cap (>=0)
// for the troop, or -1 when the hero is at no legal source for it.
static int recruit_location_cap(const Game *g, const char *troop_id,
                                const TroopDef *t) {
    // A dwelling the hero is STANDING ON that offers this troop sells it,
    // whatever the troop's catalog class: the salt roll can seed castle-class
    // troops at dwellings (salt_pick_dwelling_troop draws from the full
    // range), and the old castle-first early-return refused the very shop
    // the world spawned -- the prompt opened, the buy silently failed.
    if (g->position.dwelling_troop[0] &&
        strcmp(g->position.dwelling_troop, troop_id) == 0) {
        for (int i = 0; i < g->dwelling_count; i++) {
            const DwellingState *d = &g->dwellings[i];
            if (d->x == g->position.dwelling_x && d->y == g->position.dwelling_y &&
                strcmp(d->zone, g->position.zone) == 0 &&
                strcmp(d->troop_id, troop_id) == 0) {
                return d->count;
            }
        }
        return -1;
    }
    if (strcmp(t->dwelling, "castle") == 0) {
        // Home pool: unlimited (the castle never runs dry); gated by being at
        // the home castle and by the castle offering the troop.
        return g->position.home_castle[0] && GameCastleOffersTroop(g, t)
                   ? (1 << 28) : -1;
    }
    // Dwelling troop: the hero must be on the dwelling that offers this troop.
    if (!g->position.dwelling_troop[0] ||
        strcmp(g->position.dwelling_troop, troop_id) != 0) {
        return -1;
    }
    for (int i = 0; i < g->dwelling_count; i++) {
        const DwellingState *d = &g->dwellings[i];
        if (d->x == g->position.dwelling_x && d->y == g->position.dwelling_y &&
            strcmp(d->zone, g->position.zone) == 0 &&
            strcmp(d->troop_id, troop_id) == 0) {
            return d->count;
        }
    }
    return -1;
}

char GameArmySlotMorale(const Game *g, int slot) {
    if (!g || slot < 0 || slot >= GAME_ARMY_SLOTS) return 'N';
    const TroopDef *me = troop_by_id(g->army[slot].id);
    if (!me) return 'N';
    int others = 0, low = 0, high = 0;
    for (int j = 0; j < GAME_ARMY_SLOTS; j++) {
        if (j == slot) continue;
        if (!g->army[j].id[0] || g->army[j].count == 0) continue;
        const TroopDef *o = troop_by_id(g->army[j].id);
        if (!o) continue;
        others++;
        char r = morale_result(me->morale_group, o->morale_group);
        if (r == 'L') low++;
        else if (r == 'H') high++;
    }
    if (others == 0)    return 'H';
    if (low > 0)        return 'L';
    if (high == others) return 'H';
    return 'N';
}

bool GameCastleOffersTroop(const Game *g, const TroopDef *t) {
    return g && t && t->hit_points > 0 &&
           g->stats.leadership_current >= t->hit_points * 6;
}

int GameRecruitLocationCap(const Game *g, const char *troop_id) {
    if (!g || !troop_id) return -1;
    const TroopDef *t = troop_by_id(troop_id);
    if (!t) return -1;
    return recruit_location_cap(g, troop_id, t);
}

int GameBuyTroop(Game *g, const char *troop_id, int count) {
    if (!g || !troop_id || count <= 0) return 2;
    const TroopDef *t = troop_by_id(troop_id);
    if (!t) return 2;
    // Location guard: refuse a recruit the hero cannot legally make from here.
    // Same contract as the town transaction cores (GameBuySpell etc.) -- the engine,
    // not the caller, is the legality boundary, so autoplay is held to it too.
    int loc_cap = recruit_location_cap(g, troop_id, t);
    if (loc_cap < 0 || count > loc_cap) {
        return 4;   // illegal location / over the dwelling population
    }
    int total_cost = t->recruit_cost * count;
    if (g->stats.gold < total_cost) return 1;
    if (count > GameMaxRecruitable(g, troop_id)) return 3;

    // Find matching stack or an empty slot.
    int slot = -1;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (strcmp(g->army[i].id, troop_id) == 0) { slot = i; break; }
    }
    if (slot < 0) {
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (!g->army[i].id[0]) { slot = i; break; }
        }
    }
    if (slot < 0) return 2;
    game_copy_id(g->army[slot].id, sizeof(g->army[slot].id), troop_id);
    g->army[slot].count += count;
    g->stats.gold -= total_cost;
    {
        char tag[64];
        snprintf(tag, sizeof tag, "buy:%s:%d", troop_id, count);
        recorder_capture(tag);
    }
    return 0;
}

int GameAddTroop(Game *g, const char *troop_id, int count) {
    if (!g || !troop_id || count <= 0) return 1;
    int slot = -1;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (strcmp(g->army[i].id, troop_id) == 0 && g->army[i].count > 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (!g->army[i].id[0] || g->army[i].count == 0) { slot = i; break; }
        }
    }
    if (slot < 0) return 1;
    game_copy_id(g->army[slot].id, sizeof(g->army[slot].id), troop_id);
    g->army[slot].count += count;
    {
        char tag[64];
        snprintf(tag, sizeof tag, "add:%s:%d", troop_id, count);
        recorder_capture(tag);
    }
    return 0;
}

void GameCompactArmy(Game *g) {
    if (!g) return;
    int dst = 0;
    for (int src = 0; src < GAME_ARMY_SLOTS; src++) {
        if (!g->army[src].id[0] || g->army[src].count == 0) continue;
        if (dst != src) {
            g->army[dst] = g->army[src];
        }
        dst++;
    }
    for (int i = dst; i < GAME_ARMY_SLOTS; i++) {
        g->army[i].id[0] = '\0';
        g->army[i].count = 0;
    }
}

// Garrison is only legal at an owned castle the hero is standing at -- the
// own-castle screen (or the moment a siege win takes the castle). The engine, not
// the caller, enforces it: position.own_castle must name THIS castle, and the
// castle must be player-owned. Same contract as the in_town town cores.
static bool garrison_location_ok(const Game *g, const char *castle_id) {
    if (!castle_id || !castle_id[0]) return false;
    if (strcmp(g->position.own_castle, castle_id) != 0) return false;
    const CastleRecord *cr = GameFindCastleConst(g, castle_id);
    return cr && cr->owner_kind == CASTLE_OWNER_PLAYER;
}

//  garrison_troop, generalised to part of a stack. Moving the whole stack is
// exactly the original: refused when it is the hero's last, and the army is
// compacted. Moving part of it always leaves the stack in the army, so it is
// never the last-army refusal.
int GameGarrisonTroopCount(Game *g, const char *castle_id, int slot, int count) {
    if (!g || slot < 0 || slot >= GAME_ARMY_SLOTS) return 1;
    if (!garrison_location_ok(g, castle_id)) return 1;
    const ArmyStack *src = &g->army[slot];
    if (!src->id[0] || src->count == 0) return 1;
    if (count <= 0 || count > src->count) return 1;
    bool whole = (count == src->count);

    // Refuse if this would leave the player with no army (
    // game->player_troops[1]; we count non-empty slots other than `slot`).
    if (whole) {
        int remaining = 0;
        for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
            if (i == slot) continue;
            if (g->army[i].id[0] && g->army[i].count > 0) { remaining++; break; }
        }
        if (remaining == 0) return 2;
    }

    CastleRecord *cr = GameFindCastle(g, castle_id);
    if (!cr) return 1;

    // Find matching troop in garrison, or first empty slot.
    int dst = -1;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (strcmp(cr->garrison[i].id, src->id) == 0 && cr->garrison[i].count > 0) {
            dst = i;
            break;
        }
        if (!cr->garrison[i].id[0] || cr->garrison[i].count == 0) {
            if (dst < 0) dst = i;   // remember first empty
        }
    }
    if (dst < 0) return 1;

    game_copy_id(cr->garrison[dst].id, sizeof(cr->garrison[dst].id), src->id);
    cr->garrison[dst].count += count;

    if (!whole) {
        g->army[slot].count -= count;
        return 0;
    }
    // Remove from player. dismiss_troop zeroes the stack and
    // leaves a gap; openbounty compacts so the filled slots stay
    // contiguous (matches the visible UI expectation that A/B/C/...
    // are dense).
    g->army[slot].id[0] = '\0';
    g->army[slot].count = 0;
    GameCompactArmy(g);
    return 0;
}

int GameGarrisonTroop(Game *g, const char *castle_id, int slot) {
    if (!g || slot < 0 || slot >= GAME_ARMY_SLOTS) return 1;
    return GameGarrisonTroopCount(g, castle_id, slot, g->army[slot].count);
}

//  ungarrison_troop, generalised to part of a stack. The whole stack is the
// original move (the garrison is compacted); part of it leaves the rest there.
int GameUngarrisonTroopCount(Game *g, const char *castle_id, int slot, int count) {
    if (!g || slot < 0 || slot >= GAME_ARMY_SLOTS) return 1;
    if (!garrison_location_ok(g, castle_id)) return 1;
    CastleRecord *cr = GameFindCastle(g, castle_id);
    if (!cr) return 1;
    const Unit *src = &cr->garrison[slot];
    if (!src->id[0] || src->count == 0) return 1;
    if (count <= 0 || count > src->count) return 1;
    bool whole = (count == src->count);

    // Find matching army stack or empty slot.
    int dst = -1;
    for (int i = 0; i < GAME_ARMY_SLOTS; i++) {
        if (strcmp(g->army[i].id, src->id) == 0 && g->army[i].count > 0) {
            dst = i;
            break;
        }
        if (!g->army[i].id[0] || g->army[i].count == 0) {
            if (dst < 0) dst = i;
        }
    }
    if (dst < 0) return 1;

    game_copy_id(g->army[dst].id, sizeof(g->army[dst].id), src->id);
    g->army[dst].count += count;

    if (!whole) {
        cr->garrison[slot].count -= count;
        return 0;
    }
    // Compact the garrison .
    for (int i = slot; i < GAME_ARMY_SLOTS - 1; i++) {
        game_copy_id(cr->garrison[i].id, sizeof(cr->garrison[i].id),
                cr->garrison[i + 1].id);
        cr->garrison[i].count = cr->garrison[i + 1].count;
    }
    cr->garrison[GAME_ARMY_SLOTS - 1].id[0] = '\0';
    cr->garrison[GAME_ARMY_SLOTS - 1].count = 0;
    return 0;
}

int GameUngarrisonTroop(Game *g, const char *castle_id, int slot) {
    if (!g || slot < 0 || slot >= GAME_ARMY_SLOTS) return 1;
    const CastleRecord *cr = GameFindCastleConst(g, castle_id);
    if (!cr) return 1;
    return GameUngarrisonTroopCount(g, castle_id, slot, cr->garrison[slot].count);
}
