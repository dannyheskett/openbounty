#include "step.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Engine headers
#include "adventure.h"
#include "tile.h"
#include "tables.h"
#include "flows.h"
#include "pending.h"

// Shell callbacks invoked by step_try (audio events, recorder pings,
// prompt/dialog/screen openers). The host implements these; engine
// just emits via the signatures declared in ui_host.h.
#include "ui_host.h"

bool step_try(Game *game, Map *map, Fog *fog,
              const Resources *res, int dx, int dy) {
    if (dx == 0 && dy == 0) return false;
    int nx = game->position.x + dx;
    int ny = game->position.y + dy;
    const Tile *nt = MapGetTile(map, nx, ny);
    if (!nt) {
        // Off-map: emit a blocked trace so the harness can distinguish
        // "no move attempted" from "move attempted and blocked".
        char tag[48];
        snprintf(tag, sizeof tag, "step:blocked:%+d%+d:edge", dx, dy);
        recorder_capture(tag);
        audio_play_tune(AUDIO_TUNE_BUMP);
        return false;
    }

    int prev_x = game->position.x;
    int prev_y = game->position.y;
    // Snapshot state that may be mutated before we know whether the
    // target tile will bounce the hero back (towns / castles). If the
    // interact result sets bounce_back, we need to roll all of this
    // back, not just x/y — otherwise boarding a boat → entering a town
    // strands the hero "walking on water" with the boat under them.
    TravelMode prev_travel_mode = game->travel_mode;
    int prev_boat_x = game->boat.x;
    int prev_boat_y = game->boat.y;
    bool walking = (game->travel_mode == TRAVEL_WALK);
    bool in_boat = (game->travel_mode == TRAVEL_BOAT);
    bool flying = (game->character.mount == MOUNT_FLY);
    bool will_board = false;
    bool can_move = false;

    if (flying) {
        // Flight bypasses ground restrictions ().
        can_move = adventure_walkable_in_flight(nt);
    } else if (walking && game->boat.has_boat &&
        nx == game->boat.x && ny == game->boat.y) {
        will_board = true;
        can_move = true;
    } else if (walking) {
        can_move = adventure_walkable_on_foot(nt);
    } else {
        can_move = adventure_walkable_in_boat(nt);
    }
    if (!can_move) {
        // Tile rejected by walkable check. Tag with the dx,dy and the
        // terrain so the harness consumer can see WHY blocked at a glance.
        char tag[64];
        snprintf(tag, sizeof tag, "step:blocked:%+d%+d:%s", dx, dy,
                 TerrainName(nt->terrain));
        recorder_capture(tag);
        audio_play_tune(AUDIO_TUNE_BUMP);
        return false;
    }

    game->position.last_x = prev_x;
    game->position.last_y = prev_y;
    if (dx < 0) game->position.facing_left = true;
    else if (dx > 0) game->position.facing_left = false;
    game->position.x = nx;
    game->position.y = ny;

    if (will_board) {
        game->travel_mode = TRAVEL_BOAT;
    } else if (in_boat) {
        bool still_water = (nt->terrain == TERRAIN_WATER) || nt->is_bridge;
        if (still_water) {
            game->boat.x = nx;
            game->boat.y = ny;
        } else {
            game->travel_mode = TRAVEL_WALK;
            game->boat.x = prev_x;
            game->boat.y = prev_y;
        }
    }

    FogReveal(fog, map, nx, ny, res->world.fog_sight);

    bool bounced = false;
    // (game.c:6522): interactive tiles do not fire while flying.
    // The hero passes over castles/towns/signs without triggering them.
    if (!flying && nt->interactive != INTERACT_NONE) {
        InteractResult ir = adventure_handle_interact(nt, game->position.zone);
        if (ir.entered_town) {
            GameTouchTown(game, ir.town_id);
            // Look up by id — the stepped-onto tile may be the town's
            // gate coords, not its home (x,y), so coord-keyed lookup can
            // miss. ir.town_id comes from the map tile metadata (which
            // was stamped from the ResTown record), so it's authoritative.
            const ResTown *rt = resources_town_by_id(res, ir.town_id);
            const char *disp = (rt && rt->name[0]) ? rt->name : ir.town_id;
            views_open_town(disp, ir.town_id, ir.town_boat_x, ir.town_boat_y);
        }
        if (ir.artifact_idx >= 0) {
            const ArtifactDef *a = artifact_by_index(ir.artifact_idx);
            if (a && GameClaimArtifact(game, ir.artifact_idx)) {
                MapClearInteractive(map, nx, ny);
                GameAddConsumed(game, game->position.zone, nx, ny);
                // : header holds the
                // full artifact flavor paragraph (our `effect`), body is
                // the "map to the scepter" footer. Matches KB_BottomBox
                // (header + two-line body).
                char header[256];
                snprintf(header, sizeof(header),
                         "You have found %s!\n%s",
                         a->name, a->effect);
                open_dialog(header,
                    "...and a piece of the map to\n"
                    "the stolen scepter.");
            }
        }
        if (ir.opened_castle) {
            // Mark visited on any castle entry — audience, own, or
            // hostile — so the Castle Gate spell can list it. Per
            // OpenKB spec §15: castle_visited[i] is set on first
            // visit regardless of outcome.
            CastleRecord *cr_mut = GameFindCastle(game, ir.castle_id);
            if (cr_mut) cr_mut->visited = true;
            const CastleRecord *cr = GameFindCastleConst(game, ir.castle_id);
            const ResCastle *rc = resources_castle_by_id(res, ir.castle_id);
            char header[64];
            char body[768];
            bool audience = (rc && strcmp(rc->special.flow, "audience") == 0);

            if (audience) {
                size_t k = 0;
                while (k + 1 < sizeof(pending_castle_id) && ir.castle_id[k]) {
                    pending_castle_id[k] = ir.castle_id[k]; k++;
                }
                pending_castle_id[k] = '\0';
                screen_home_castle_open(game);
                header[0] = '\0';
                body[0]   = '\0';
            } else if (cr && cr->owner_kind == CASTLE_OWNER_PLAYER) {
                screen_own_castle_open(game, ir.castle_id);
                header[0] = '\0';
                body[0]   = '\0';
            } else if (cr && cr->owner_kind == CASTLE_OWNER_MONSTERS &&
                       !game->stats.siege_weapons) {
                // Original DOS KB: stepping onto a hostile castle gate
                // without siege weapons silently bounces the hero back.
                // The bounce_back flag is already set by adventure.c.
                // openkb left this unenforced (spec §34.14); we restore
                // the original behavior so castles actually gate the
                // purchase.
                header[0] = '\0';
                body[0]   = '\0';
            } else if (cr && cr->owner_kind == CASTLE_OWNER_MONSTERS) {
                size_t k = 0;
                while (k + 1 < sizeof(pending_castle_id) && ir.castle_id[k]) {
                    pending_castle_id[k] = ir.castle_id[k]; k++;
                }
                pending_castle_id[k] = '\0';
                char prompt_body[320];
                int po = 0;
                po += snprintf(prompt_body + po, sizeof(prompt_body) - po,
                    "Various groups of monsters\n"
                    "occupy this castle.\n\n"
                    "No one's rule.\n\n"
                    "Garrison:\n");
                int stacks_shown = 0;
                for (int i = 0; i < GAME_ARMY_SLOTS &&
                     po + 1 < (int)sizeof(prompt_body); i++) {
                    const Unit *u = &cr->garrison[i];
                    if (!u->id[0] || u->count == 0) continue;
                    const TroopDef *td = troop_by_id(u->id);
                    const char *tname = (td && td->name[0]) ? td->name : u->id;
                    const char *count_label = GameNumberName(game, u->count);
                    if (count_label[0]) {
                        po += snprintf(prompt_body + po,
                                       sizeof(prompt_body) - po,
                                       "  %s %s\n", count_label, tname);
                    } else {
                        po += snprintf(prompt_body + po,
                                       sizeof(prompt_body) - po,
                                       "  %d %s\n", u->count, tname);
                    }
                    stacks_shown++;
                }
                if (stacks_shown == 0) {
                    po += snprintf(prompt_body + po,
                                   sizeof(prompt_body) - po,
                                   "  (none)\n");
                }
                char prompt_header[64];
                snprintf(prompt_header, sizeof(prompt_header), "Castle %s",
                         rc && rc->name[0] ? rc->name : ir.castle_id);
                pending_flow = FLOW_SIEGE_MONSTER;
                prompt_yes_no_open(prompt_header, prompt_body);
                header[0] = '\0';
                body[0]   = '\0';
            } else if (cr && cr->owner_kind == CASTLE_OWNER_VILLAIN &&
                       !game->stats.siege_weapons) {
                // See CASTLE_OWNER_MONSTERS branch above — silent
                // bounce-back without siege weapons (DOS KB faithful).
                header[0] = '\0';
                body[0]   = '\0';
            } else if (cr && cr->owner_kind == CASTLE_OWNER_VILLAIN) {
                const VillainDef *v = villain_by_id(cr->villain_id);
                size_t k = 0;
                while (k + 1 < sizeof(pending_castle_id) && ir.castle_id[k]) {
                    pending_castle_id[k] = ir.castle_id[k]; k++;
                }
                pending_castle_id[k] = '\0';
                char prompt_body[320];
                int po = 0;
                po += snprintf(prompt_body + po, sizeof(prompt_body) - po,
                    "%s and army occupy\n"
                    "this castle.\n\n"
                    "Garrison:\n",
                    v ? v->name : cr->villain_id);
                int stacks_shown = 0;
                for (int i = 0; i < GAME_ARMY_SLOTS &&
                     po + 1 < (int)sizeof(prompt_body); i++) {
                    const Unit *u = &cr->garrison[i];
                    if (!u->id[0] || u->count == 0) continue;
                    const TroopDef *td = troop_by_id(u->id);
                    const char *tname = (td && td->name[0]) ? td->name : u->id;
                    const char *count_label = GameNumberName(game, u->count);
                    if (count_label[0]) {
                        po += snprintf(prompt_body + po,
                                       sizeof(prompt_body) - po,
                                       "  %s %s\n", count_label, tname);
                    } else {
                        po += snprintf(prompt_body + po,
                                       sizeof(prompt_body) - po,
                                       "  %d %s\n", u->count, tname);
                    }
                    stacks_shown++;
                }
                if (stacks_shown == 0) {
                    po += snprintf(prompt_body + po,
                                   sizeof(prompt_body) - po,
                                   "  (none)\n");
                }
                char prompt_header[64];
                snprintf(prompt_header, sizeof(prompt_header), "Castle %s",
                         rc && rc->name[0] ? rc->name : ir.castle_id);
                pending_flow = FLOW_SIEGE_VILLAIN;
                prompt_yes_no_open(prompt_header, prompt_body);
                header[0] = '\0';
                body[0]   = '\0';
            } else {
                snprintf(header, sizeof(header), "Castle");
                snprintf(body, sizeof(body), "An uncharted castle.");
            }
            if (header[0] || body[0]) {
                open_dialog(header, body);
            }
        }
        if (ir.opened_alcove) {
            if (!game->stats.knows_magic) {
                char body[256], cbuf[16];
                snprintf(cbuf, sizeof cbuf, "%d", res->economy.alcove_cost);
                ResTemplateVar vars[] = { { "COST", cbuf } };
                resources_format_template(body, sizeof body,
                                          res->banners.alcove_offer,
                                          vars, 1);
                screen_alcove_open(game);
                pending_flow = FLOW_ALCOVE;
                prompt_yes_no_open(res->ui.dt_alcove_offer, body);
            } else {
                char body[RES_BANNER_LEN];
                resources_format_template(body, sizeof body,
                                          res->banners.alcove_already,
                                          NULL, 0);
                open_dialog(NULL, body);
            }
            if (ir.bounce_back) {
                game->position.x = prev_x;
                game->position.y = prev_y;
                game->travel_mode = prev_travel_mode;
                game->boat.x = prev_boat_x;
                game->boat.y = prev_boat_y;
                bounced = true;
            }
            goto after_interact;
        }
        if (ir.opened_dwelling) {
            const char *kind = "plains";
            switch (ir.dwelling_kind) {
                case INTERACT_DWELLING_PLAINS:  kind = "plains";  break;
                case INTERACT_DWELLING_FOREST:  kind = "forest";  break;
                case INTERACT_DWELLING_HILLS:   kind = "hill";    break;
                case INTERACT_DWELLING_DUNGEON: kind = "dungeon"; break;
                default: break;
            }
            DwellingState *d = GameTouchDwelling(game, game->position.zone,
                                                 nx, ny, kind);
            const TroopDef *t = (d && d->troop_id[0])
                ? troop_by_id(d->troop_id) : NULL;
            if (t && d && d->count > 0) {
                {
                    size_t k = 0;
                    while (k + 1 < sizeof(pending_dwelling_troop) && t->id[k]) {
                        pending_dwelling_troop[k] = t->id[k]; k++;
                    }
                    pending_dwelling_troop[k] = '\0';
                }
                {
                    size_t k = 0;
                    while (k + 1 < sizeof(pending_dwelling_zone) &&
                           game->position.zone[k]) {
                        pending_dwelling_zone[k] = game->position.zone[k]; k++;
                    }
                    pending_dwelling_zone[k] = '\0';
                }
                pending_dwelling_x = nx;
                pending_dwelling_y = ny;
                int max = GameMaxRecruitable(game, t->id);
                if (max < 0) max = 0;
                int affordable = (t->recruit_cost > 0)
                    ? (game->stats.gold / t->recruit_cost) : 0;
                int cap = (max < affordable) ? max : affordable;
                if (cap > d->count) cap = d->count;
                char header[48];
                char body[192];
                snprintf(header, sizeof(header), "%s", t->name);
                char cnt_buf[16], cost_buf[16], gold_buf[16], cap_buf[16];
                snprintf(cnt_buf,  sizeof cnt_buf,  "%d", d->count);
                snprintf(cost_buf, sizeof cost_buf, "%d", t->recruit_cost);
                snprintf(gold_buf, sizeof gold_buf, "%d", game->stats.gold);
                snprintf(cap_buf,  sizeof cap_buf,  "%d", cap);
                ResTemplateVar vars[] = {
                    { "COUNT", cnt_buf },
                    { "TROOP", t->name },
                    { "COST",  cost_buf },
                    { "GOLD",  gold_buf },
                    { "CAP",   cap_buf },
                };
                resources_format_template(body, sizeof body,
                                          res->banners.dwelling_recruit_prompt,
                                          vars, 5);
                DwellingKind dk = DWELLING_KIND_PLAINS;
                switch (ir.dwelling_kind) {
                    case INTERACT_DWELLING_PLAINS:  dk = DWELLING_KIND_PLAINS;  break;
                    case INTERACT_DWELLING_FOREST:  dk = DWELLING_KIND_FOREST;  break;
                    case INTERACT_DWELLING_HILLS:   dk = DWELLING_KIND_HILL;    break;
                    case INTERACT_DWELLING_DUNGEON: dk = DWELLING_KIND_DUNGEON; break;
                    default: break;
                }
                screen_dwelling_open(game, dk, t->id,
                                     d->count, t->recruit_cost,
                                     game->stats.gold, cap);
                pending_flow = FLOW_RECRUIT;
                prompt_text_input_open(header, body, 4,
                                       cap > 0 ? cap : 0);
            } else if (t) {
                char msg[RES_BANNER_LEN];
                resources_format_template(msg, sizeof msg,
                                          res->banners.dwelling_none_this_week,
                                          NULL, 0);
                open_dialog(NULL, msg);
            } else {
                char msg[RES_BANNER_LEN];
                resources_format_template(msg, sizeof msg,
                                          res->banners.dwelling_empty,
                                          NULL, 0);
                open_dialog(NULL, msg);
            }
        }
        if (ir.opened_chest) {
            audio_play_tune(AUDIO_TUNE_CHEST);
            int zone_index = 0;
            if (res) {
                for (int i = 0; i < res->zone_count; i++) {
                    if (strcmp(res->zones[i].id, game->position.zone) == 0) {
                        zone_index = i;
                        break;
                    }
                }
            }
            char body[320];
            ChestPending cp = { 0, 0 };
            ChestOutcome outcome = GameRollChest(game, zone_index, nx, ny,
                                                 body, sizeof(body), &cp);

            MapClearInteractive(map, nx, ny);
            GameAddConsumed(game, game->position.zone, nx, ny);

            if (outcome == CHEST_OUTCOME_GOLD && cp.pending_gold > 0) {
                pending_chest_gold       = cp.pending_gold;
                pending_chest_leadership = cp.pending_leadership;
                pending_flow = FLOW_CHEST_CHOICE;
                prompt_ab_open("", body);
            } else {
                open_dialog(NULL, body);
            }
        }
        if (ir.opened_telecave) {
            int found_self = -1;
            int pair_target = -1;
            int seq = 0;
            int self_seq = -1;
            for (int i = 0; i < game->placement_count; i++) {
                const SaltedPlacement *p = &game->placements[i];
                if (strcmp(p->zone, game->position.zone) != 0) continue;
                if (p->kind != INTERACT_TELECAVE) continue;
                if (p->x == nx && p->y == ny) {
                    found_self = i;
                    self_seq = seq;
                }
                seq++;
            }
            if (found_self >= 0 && self_seq >= 0) {
                int target_seq = (self_seq % 2 == 0)
                    ? self_seq + 1 : self_seq - 1;
                int s = 0;
                for (int i = 0; i < game->placement_count; i++) {
                    const SaltedPlacement *p = &game->placements[i];
                    if (strcmp(p->zone, game->position.zone) != 0) continue;
                    if (p->kind != INTERACT_TELECAVE) continue;
                    if (s == target_seq) { pair_target = i; break; }
                    s++;
                }
            }
            char tmsg[RES_BANNER_LEN];
            if (pair_target >= 0) {
                const SaltedPlacement *dst = &game->placements[pair_target];
                game->position.x = dst->x;
                game->position.y = dst->y;
                game->position.last_x = dst->x;
                game->position.last_y = dst->y;
                FogReveal(fog, map, dst->x, dst->y, res->world.fog_sight);
                resources_format_template(tmsg, sizeof tmsg,
                                          res->banners.telecave_teleport,
                                          NULL, 0);
                open_dialog(res->ui.dt_teleport_cave, tmsg);
            } else {
                resources_format_template(tmsg, sizeof tmsg,
                                          res->banners.telecave_inert,
                                          NULL, 0);
                open_dialog(res->ui.dt_teleport_cave, tmsg);
            }
        }
        if (ir.opened_navmap) {
            int target_zone = -1;
            const char *p = ir.navmap_id;
            const char *digit = p;
            while (*digit && (*digit < '0' || *digit > '9')) digit++;
            if (*digit) target_zone = atoi(digit);
            int cur_zone = -1;
            for (int i = 0; i < res->zone_count; i++) {
                if (strcmp(res->zones[i].id, game->position.zone) == 0) {
                    cur_zone = i;
                    break;
                }
            }
            if (target_zone < 0 || target_zone >= res->zone_count ||
                target_zone == cur_zone) {
                target_zone = -1;
                for (int i = 0; i < res->zone_count; i++) {
                    if (i != cur_zone && !game->world.zones_discovered[i]) {
                        target_zone = i;
                        break;
                    }
                }
            }
            char body[128];
            if (target_zone >= 0 && target_zone < GAME_CONTINENTS) {
                game->world.zones_discovered[target_zone] = true;
                MapClearInteractive(map, nx, ny);
                GameAddConsumed(game, game->position.zone, nx, ny);
                const char *zn = res->zones[target_zone].name[0]
                    ? res->zones[target_zone].name
                    : res->zones[target_zone].id;
                ResTemplateVar vars[] = { { "ZONE", zn } };
                resources_format_template(body, sizeof body,
                                          res->banners.navmap_pickup,
                                          vars, 1);
                open_dialog(NULL, body);
            } else {
                // All zones already discovered: consume the navmap
                // silently. No port-authored fallback dialog.
                MapClearInteractive(map, nx, ny);
                GameAddConsumed(game, game->position.zone, nx, ny);
            }
        }
        if (ir.opened_orb) {
            int zone_index = -1;
            for (int i = 0; i < res->zone_count; i++) {
                if (strcmp(res->zones[i].id, game->position.zone) == 0) {
                    zone_index = i;
                    break;
                }
            }
            if (zone_index >= 0 && zone_index < GAME_CONTINENTS) {
                game->world.orbs_found[zone_index] = true;
            }
            MapClearInteractive(map, nx, ny);
            GameAddConsumed(game, game->position.zone, nx, ny);
            char body[128];
            int zi = (zone_index >= 0) ? zone_index : 0;
            ResTemplateVar vars[] = { { "ZONE", res->zones[zi].name } };
            resources_format_template(body, sizeof body,
                                      res->banners.crystal_ball_pickup,
                                      vars, 1);
            open_dialog(res->ui.dt_crystal_ball, body);
        }
        if (ir.opened_foe) {
            const FoeState *f = GameFindFoeConst(game, ir.foe_id);
            bool friendly = (f && f->friendly);
            if (friendly) {
                start_foe_friendly_flow(game, map, res, ir.foe_id, nx, ny);
            } else {
                start_foe_hostile_flow(game, ir.foe_id, nx, ny);
                ir.bounce_back = true;
            }
        }
        after_interact:
        if (ir.bounce_back) {
            game->position.x = prev_x;
            game->position.y = prev_y;
            game->travel_mode = prev_travel_mode;
            game->boat.x = prev_boat_x;
            game->boat.y = prev_boat_y;
            bounced = true;
        }
    }

    if (!bounced) {
        bool day_end = false, week_end = false;
        int  paid = 0;
        GameOnStep(game, !flying && nt->terrain == TERRAIN_DESERT,
                   &day_end, &week_end, &paid);
        (void)day_end;
        int collided_idx = GameFoesFollow(game, map);
        if (collided_idx >= 0) {
            const FoeState *cf = &game->foes[collided_idx];
            char fid[32];
            size_t k = 0;
            while (k + 1 < sizeof(fid) && cf->placement_id[k]) {
                fid[k] = cf->placement_id[k]; k++;
            }
            fid[k] = '\0';
            if (cf->friendly) {
                start_foe_friendly_flow(game, map, res, fid,
                                        game->position.x, game->position.y);
            } else {
                start_foe_hostile_flow(game, fid,
                                       game->position.x, game->position.y);
            }
        }
        if (week_end) {
            schedule_week_end(game, paid);
        }
        if (game->stats.game_over) {
            show_lose_game(game, res);
        }
    }
    return !bounced;
}
