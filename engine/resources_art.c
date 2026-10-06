// engine/resources_art.c -- the art manifest: every art path a pack uses.

#include "resources_internal.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "map.h"
#include "tile.h"
#include "combat.h"     // COMBAT_W / COMBAT_H for the siege grid

// ---- Art manifest ----------------------------------------------------------

static void art_add(ResArtList *out, int cap, int *n, const char *p) {
    (void)cap;
    if (!p || !p[0] || !out) return;
    for (int i = 0; i < out->n; i++)
        if (strcmp(out->path[i], p) == 0) return;    // already listed
    if (out->n >= out->cap) {
        int ncap = out->cap ? out->cap * 2 : 256;
        char (*grown)[RES_PATH_LEN] = realloc(out->path, (size_t)ncap * sizeof *grown);
        if (!grown) return;
        out->path = grown;
        out->cap = ncap;
    }
    res_copy_str(out->path[out->n], RES_PATH_LEN, p);
    out->n++;
    *n = out->n;
}

void resources_art_list_free(ResArtList *list) {
    if (!list) return;
    free(list->path);
    list->path = NULL;
    list->n = list->cap = 0;
}

static void art_add_anim(ResArtList *out, int cap, int *n,
                         const ResAnimSet *a) {
    if (!a) return;
    for (int f = 0; f < OB_FACE_COUNT; f++)
        for (int i = 0; i < a->count[f]; i++)
            art_add(out, cap, n, a->frames[f][i]);
}

void resources_zone_arrival(const ResZone *z, const char *from, int *x, int *y) {
    *x = z->hero_spawn_x;
    *y = z->hero_spawn_y;
    if (!from || !from[0]) return;
    for (int i = 0; i < z->arrival_count; i++)
        if (strcmp(z->arrivals[i].from, from) == 0) {
            *x = z->arrivals[i].x;
            *y = z->arrivals[i].y;
            return;
        }
}

bool resources_tile_from_set(const Resources *res, const char *set, const char *stem) {
    if (!res || !set || !set[0] || !stem) return false;
    for (int zi = 0; zi < res->zone_count; zi++) {
        const ResZone *z = &res->zones[zi];
        if (strcmp(z->tile_set, set) != 0) continue;
        if (z->tile_set_art_count == 0) return true;          // the whole folder
        for (int k = 0; k < z->tile_set_art_count; k++)
            if (strcmp(z->tile_set_arts[k], stem) == 0) return true;
    }
    return false;
}

int resources_art_manifest(const Resources *res, ResArtList *out) {
    int n = 0;
    const int cap = 0;   // unused: the list grows
    if (!out) return 0;
    out->n = 0;
    if (!res) return 0;

    art_add_anim(out, cap, &n, &res->sprites.hero_walk);
    art_add_anim(out, cap, &n, &res->sprites.hero_idle);
    art_add_anim(out, cap, &n, &res->sprites.hero_boat);

    for (int i = 0; i < res->sprites.combat_count; i++) {
        if (i == 0 && resources_combat_ground_is_terrain(res)) continue;  // no field tile shipped
        if (i >= 5 && i <= 10 && res->sprites.siege_grid[0]) continue;    // walls are in the siege grid
        art_add(out, cap, &n, res->sprites.combat[i]);
    }

    art_add(out, cap, &n, res->sprites.font);
    art_add(out, cap, &n, res->font.file);       // no-ops when the pack has no TTF
    art_add(out, cap, &n, res->font.license);
    art_add(out, cap, &n, res->sprites.puzzle_cover);
    art_add(out, cap, &n, res->sprites.town_backdrop);
    art_add(out, cap, &n, res->sprites.castle_backdrop);
    art_add(out, cap, &n, res->sprites.plains_backdrop);
    art_add(out, cap, &n, res->sprites.forest_backdrop);
    art_add(out, cap, &n, res->sprites.hillcave_backdrop);
    art_add(out, cap, &n, res->sprites.dungeon_backdrop);
    art_add(out, cap, &n, res->sprites.alcove_backdrop);
    art_add(out, cap, &n, res->sprites.sail_backdrop);
    // A zone's own town backdrop, and a town's own (REQ-221d).
    for (int i = 0; i < res->zone_count; i++)
        art_add(out, cap, &n, res->zones[i].town_backdrop);
    for (int i = 0; i < res->zone_count; i++)
        art_add(out, cap, &n, res->zones[i].treasure_scene);
    for (int i = 0; i < res->town_count; i++)
        art_add(out, cap, &n, res->towns[i].backdrop);
    art_add(out, cap, &n, res->sprites.palace_welcome);
    art_add(out, cap, &n, res->sprites.palace_barracks);
    art_add(out, cap, &n, res->sprites.palace_throne);
    art_add(out, cap, &n, res->sprites.scene_column_capital);
    art_add(out, cap, &n, res->sprites.scene_column_shaft);
    art_add(out, cap, &n, res->sprites.scene_column_base);
    art_add(out, cap, &n, res->sprites.alcove_figure);
    for (int i = 0; i < res->sprites.alcove_figure_animation_count; i++)
        art_add(out, cap, &n, res->sprites.alcove_figure_animation[i]);
    art_add(out, cap, &n, res->sprites.ending_win);
    art_add(out, cap, &n, res->sprites.ending_lose);
    art_add(out, cap, &n, res->sprites.siege_back_wall);
    art_add(out, cap, &n, res->sprites.siege_back_wall_left);
    art_add(out, cap, &n, res->sprites.siege_back_wall_right);
    for (int y = 0; y <= COMBAT_H; y++)
        for (int x = 0; x < COMBAT_W; x++) {
            char p[RES_PATH_LEN];
            if (resources_siege_grid_path(res, x, y, p, sizeof p))
                art_add(out, cap, &n, p);
        }
    // The open-field grids: the pack's and each zone's own, every cell once.
    for (int zi = -1; zi < res->zone_count; zi++)
        for (int y = 0; y < COMBAT_H; y++)
            for (int x = 0; x < COMBAT_W; x++) {
                char p[RES_PATH_LEN];
                if (resources_field_grid_path(res, zi, x, y, p, sizeof p))
                    art_add(out, cap, &n, p);
            }
    for (int i = 0; i < res->sprites.view_icons_extra_count; i++)
        art_add(out, cap, &n, res->sprites.view_icons_extra[i]);
    art_add(out, cap, &n, res->sprites.hud_contract_silhouette);
    art_add(out, cap, &n, res->sprites.hud_boat_silhouette);
    art_add(out, cap, &n, res->sprites.hud_siege_silhouette);
    for (int i = 0; i < res->sprites.hud_siege_animation_count; i++)
        art_add(out, cap, &n, res->sprites.hud_siege_animation[i]);
    art_add(out, cap, &n, res->sprites.hud_magic_silhouette);
    for (int i = 0; i < res->sprites.hud_magic_animation_count; i++)
        art_add(out, cap, &n, res->sprites.hud_magic_animation[i]);
    art_add(out, cap, &n, res->sprites.hud_puzzle_grid);
    art_add(out, cap, &n, res->sprites.hud_gold_purse);
    art_add(out, cap, &n, res->sprites.hud_days);
    art_add(out, cap, &n, res->sprites.rail_menu);
    art_add(out, cap, &n, res->sprites.rail_map);
    art_add(out, cap, &n, res->sprites.rail_army);
    art_add(out, cap, &n, res->sprites.rail_goto);
    art_add(out, cap, &n, res->sprites.rail_cast);
    art_add(out, cap, &n, res->sprites.combat_shoot);
    art_add(out, cap, &n, res->sprites.combat_wait);
    art_add(out, cap, &n, res->sprites.combat_fly);
    art_add(out, cap, &n, res->sprites.hud_bar_strip);
    art_add(out, cap, &n, res->sprites.chrome_overworld);
    art_add(out, cap, &n, res->sprites.splash_logo);
    art_add(out, cap, &n, res->sprites.splash_title);
    art_add(out, cap, &n, res->sprites.alcove_portrait);
    art_add(out, cap, &n, res->sprites.title_battle);
    art_add(out, cap, &n, res->sprites.title_eagle);
    art_add(out, cap, &n, res->sprites.title_words);
    art_add(out, cap, &n, res->sprites.class_picker);
    art_add(out, cap, &n, res->sprites.class_highlight);
    for (int i = 0; i < res->sprites.class_picker_selected_count; i++)
        art_add(out, cap, &n, res->sprites.class_picker_selected[i]);

    art_add(out, cap, &n, res->ending.grass_tile);
    art_add(out, cap, &n, res->ending.carpet_tile);
    art_add(out, cap, &n, res->ending.hero_tile);

    for (int i = 0; i < res->classes_count; i++) {
        art_add(out, cap, &n, res->classes[i].portrait);
        art_add_anim(out, cap, &n, &res->class_hero[i].walk);
        art_add_anim(out, cap, &n, &res->class_hero[i].idle);
        art_add_anim(out, cap, &n, &res->class_hero[i].boat);
        art_add(out, cap, &n, res->class_hero[i].tile);
        art_add(out, cap, &n, res->class_hero[i].disgraced);
    }

    // One-time vista scenes (game.json `events[].scene`).
    for (int i = 0; i < res->event_scene_count; i++)
        art_add(out, cap, &n, res->event_scenes[i]);

    for (int i = 0; i < res->troops_count; i++) {
        art_add(out, cap, &n, res->troops[i].sprite);
        art_add(out, cap, &n, res->troops[i].portrait);
        for (int f = 0; f < res->troops[i].anim_count; f++)
            art_add(out, cap, &n, res->troops[i].anim[f]);
    }

    for (int i = 0; i < res->portrait_count; i++)
        for (int f = 0; f < res->portraits[i].anim_count; f++)
            art_add(out, cap, &n, res->portraits[i].anim[f]);

    for (int i = 0; i < res->villains_count; i++) {
        const VillainDef *v = &res->villains[i];
        art_add(out, cap, &n, v->portrait);
        // Declared frames, else the <portrait-stem>_NN siblings the shell derives.
        char (*frames)[RES_PATH_LEN] = NULL;
        int nf = res_villain_frame_paths(v, &frames);
        for (int f = 0; f < nf; f++) art_add(out, cap, &n, frames[f]);
        free(frames);
    }

    // The Introduction's backdrops, actors and speakers' faces.
    for (int i = 0; i < res->intro.beat_count; i++) {
        const ResIntroBeat *b = &res->intro.beats[i];
        art_add(out, cap, &n, b->backdrop);
        for (int a = 0; a < b->actor_count; a++)
            for (int f = 0; f < b->actors[a].frame_count; f++)
                art_add(out, cap, &n, b->actors[a].frames[f]);
        for (int f = 0; f < b->face_count; f++) art_add(out, cap, &n, b->face[f]);
    }

    for (int i = 0; i < res->artifacts_count; i++)
        art_add(out, cap, &n, res->artifacts[i].icon);

    // Placed-object art: chosen by the engine's interact kind in map.c, not
    // declared in game.json at all. Asked for rather than duplicated.
    {
        int nobj = 0;
        const char *const *obj = map_object_art_names(&nobj);
        for (int i = 0; i < nobj; i++) {
            char p[RES_PATH_LEN];
            snprintf(p, sizeof p, "art/tiles/%s.png", obj[i]);
            art_add(out, cap, &n, p);
        }
        // Town art is per catalog entry: the shared "town" tile only while
        // some town has no `art` of its own, plus each declared stem once.
        {
            bool shared = (res->town_count == 0);
            for (int i = 0; i < res->town_count; i++) {
                const char *a = res->towns[i].art;
                if (!a[0]) { shared = true; continue; }
                char p[RES_PATH_LEN];
                snprintf(p, sizeof p, "art/tiles/%s.png", a);
                art_add(out, cap, &n, p);
            }
            if (shared) art_add(out, cap, &n, "art/tiles/town.png");
        }
        // Wandering-army art is per zone: the shared tile only while some
        // zone has no `army_art`, plus each declared stem once.
        {
            bool shared = (res->zone_count == 0);
            for (int i = 0; i < res->zone_count; i++) {
                const char *a = res->zones[i].army_art;
                if (!a[0]) { shared = true; continue; }
                char p[RES_PATH_LEN];
                snprintf(p, sizeof p, "art/tiles/%s.png", a);
                art_add(out, cap, &n, p);
            }
            if (shared) art_add(out, cap, &n, "art/tiles/wandering_army.png");
        }
        // The alcove tile is per zone the same way: the hills-dwelling sprite
        // it borrows only while some zone leaves `alcove_art` unset, plus each
        // declared stem once. map_object_art_names already lists the borrowed
        // one, so nothing is added for the fallback here.
        for (int i = 0; i < res->zone_count; i++) {
            const char *a = res->zones[i].alcove_art;
            if (!a[0]) continue;
            char p[RES_PATH_LEN];
            snprintf(p, sizeof p, "art/tiles/%s.png", a);
            art_add(out, cap, &n, p);
        }
        for (int i = 0; i < res->castle_count; i++) {
            if (!res->castles[i].art[0]) continue;
            char p[RES_PATH_LEN];
            snprintf(p, sizeof p, "art/tiles/%s.png", res->castles[i].art);
            art_add(out, cap, &n, p);
        }
        // Castle art follows the footprint (REQ-228): a pack ships the six
        // 3x2 pieces only if some castle stamps 3x2, and the single `castle`
        // tile only if some castle stamps 1x1.
        bool used[2] = { false, false };
        for (int i = 0; i < res->castle_count; i++)
            used[res->castles[i].footprint == RES_CASTLE_FOOTPRINT_1X1] = true;
        for (int fp = 0; fp < 2; fp++) {
            if (!used[fp]) continue;
            int nc = 0;
            const char *const *cn =
                map_castle_art_names((ResCastleFootprint)fp, &nc);
            for (int i = 0; i < nc; i++) {
                char p[RES_PATH_LEN];
                snprintf(p, sizeof p, "art/tiles/%s.png", cn[i]);
                art_add(out, cap, &n, p);
            }
        }
    }

    // Tile art: tile_codes carry a bare name that tile_cache resolves under
    // art/tiles/, or under art/tiles/<tile_set>/ for a zone that declares a
    // set -- only the names the set overrides, when it lists them
    // ("tile_set_arts"), the rest from the master set. List the master set
    // while some zone draws from it, and each declared set once, so a pack
    // ships exactly the terrain it draws.
    bool shared = (res->zone_count == 0);
    for (int zi = 0; zi < res->zone_count; zi++)
        if (!res->zones[zi].tile_set[0] || res->zones[zi].tile_set_art_count > 0) shared = true;
    for (int zi = -1; zi < res->zone_count; zi++) {
        const char *set = NULL;
        if (zi < 0) {
            if (!shared) continue;
        } else {
            set = res->zones[zi].tile_set;
            if (!set[0]) continue;
            bool dup = false;
            for (int k = 0; k < zi; k++)
                if (strcmp(res->zones[k].tile_set, set) == 0) dup = true;
            if (dup) continue;
        }
        for (int i = 0; i < RES_TILE_CODE_COUNT; i++) {
            if (!res->tile_codes[i].present || !res->tile_codes[i].art[0]) continue;
            char p[RES_PATH_LEN];
            if (set) {
                if (resources_tile_from_set(res, set, res->tile_codes[i].art)) {
                    snprintf(p, sizeof p, "art/tiles/%s/%s.png", set, res->tile_codes[i].art);
                    art_add(out, cap, &n, p);
                }
            } else {
                snprintf(p, sizeof p, "art/tiles/%s.png", res->tile_codes[i].art);
                art_add(out, cap, &n, p);
            }
            for (int v = 0; v < res->tile_codes[i].variant_count; v++) {
                if (set) {
                    if (!resources_tile_from_set(res, set, res->tile_codes[i].variants[v])) continue;
                    snprintf(p, sizeof p, "art/tiles/%s/%s.png", set, res->tile_codes[i].variants[v]);
                } else {
                    snprintf(p, sizeof p, "art/tiles/%s.png", res->tile_codes[i].variants[v]);
                }
                art_add(out, cap, &n, p);
            }
        }
    }

    return n;
}

const ResClassHero *resources_class_hero(const Resources *r, const char *class_id) {
    if (!r || !class_id) return NULL;
    for (int i = 0; i < r->classes_count; i++) {
        if (strcmp(r->classes[i].id, class_id) != 0) continue;
        const ResClassHero *h = &r->class_hero[i];
        bool any = h->tile[0] != '\0';
        for (int f = 0; f < OB_FACE_COUNT && !any; f++)
            any = h->walk.count[f] || h->idle.count[f] || h->boat.count[f];
        return any ? h : NULL;
    }
    return NULL;
}

bool resources_siege_grid_path(const Resources *res, int x, int y,
                               char *out, int cap) {
    if (out && cap > 0) out[0] = '\0';
    if (!res || !out || cap <= 0 || !res->sprites.siege_grid[0]) return false;
    if (x < 0 || x >= COMBAT_W || y < 0 || y > COMBAT_H) return false;
    snprintf(out, (size_t)cap, "%s_%d_%d.png", res->sprites.siege_grid, x, y);
    return true;
}

bool resources_field_grid_path(const Resources *res, int zone_index, int x, int y,
                               char *out, int cap) {
    if (out && cap > 0) out[0] = '\0';
    if (!res || !out || cap <= 0) return false;
    if (x < 0 || x >= COMBAT_W || y < 0 || y >= COMBAT_H) return false;
    const char *prefix = res->sprites.field_grid;
    if (zone_index >= 0 && zone_index < res->zone_count && res->zones[zone_index].field_grid[0])
        prefix = res->zones[zone_index].field_grid;
    if (!prefix[0]) return false;
    snprintf(out, (size_t)cap, "%s_%d_%d.png", prefix, x, y);
    return true;
}

bool resources_combat_ground_is_terrain(const Resources *res) {
    return res && strcmp(res->sprites.combat_ground, "terrain") == 0;
}
