#include "sprites.h"
#include "gfx.h"
#include "assets.h"
#include "tables.h"
#include "resources.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Sprite paths in game.json are pack-relative; LoadAssetTexture reads
// them straight from the active pack on the global pack stack.

static Texture2D load_filtered(const char *path) {
    Texture2D t = LoadAssetTexture(path);
    gfx_texture_point(t);
    return t;
}

// Stashed for the duration of sprites_load so the per-call helpers don't
// each need to take Resources. Reset on every sprites_load entry.
static const Resources *s_res = NULL;

static Texture2D load_rel(const char *rel) {
    if (!rel || !rel[0]) return (Texture2D){ 0 };
    char p[256];
    resources_resolve_path(s_res, rel, p, sizeof p);
    return load_filtered(p);
}

// Load every declared frame of an animation set, preserving which facings the
// pack actually authored so the renderer knows whether to mirror.
static void load_anim_set(SpriteAnim *dst, const ResAnimSet *src) {
    if (!dst || !src) return;
    dst->directional = src->directional;
    for (int f = 0; f < OB_FACE_COUNT; f++) {
        int n = src->count[f];
        dst->tex[f] = n > 0 ? calloc((size_t)n, sizeof *dst->tex[f]) : NULL;
        dst->frames[f] = dst->tex[f] ? n : 0;
        for (int i = 0; i < dst->frames[f]; i++)
            dst->tex[f][i] = load_rel(src->frames[f][i]);
    }
}

static void unload_anim_set(SpriteAnim *a) {
    if (!a) return;
    for (int f = 0; f < OB_FACE_COUNT; f++) {
        for (int i = 0; i < a->frames[f]; i++) gfx_texture_free(a->tex[f][i]);
        free(a->tex[f]);
        a->tex[f] = NULL;
        a->frames[f] = 0;
    }
}

// A frame strip of `n` textures from `paths`, or NULL for none.
static Texture2D *load_strip(const char (*paths)[RES_PATH_LEN], int n) {
    if (n <= 0 || !paths) return NULL;
    Texture2D *t = calloc((size_t)n, sizeof *t);
    if (!t) return NULL;
    for (int i = 0; i < n; i++) t[i] = load_rel(paths[i]);
    return t;
}

static void unload_strip(Texture2D **t, int *n) {
    for (int i = 0; *t && i < *n; i++) gfx_texture_free((*t)[i]);
    free(*t);
    *t = NULL;
    *n = 0;
}

void sprites_load(Sprites *s, const Resources *res) {
    if (!s || !res) return;
    // Animation arrays are now filled only up to their declared count, so the
    // unused tail has to start zeroed -- sprites_unload walks the whole array
    // and UnloadTexture is a no-op on a zero id.
    memset(s, 0, sizeof *s);
    s_res = res;

    // Hero walk / idle / boat from the sprite manifest. The declared array
    // length is the cycle; a pack shipping six walk frames animates over six.
    load_anim_set(&s->hero_walk, &res->sprites.hero_walk);
    load_anim_set(&s->hero_idle, &res->sprites.hero_idle);
    load_anim_set(&s->hero_boat, &res->sprites.hero_boat);

    // Class portraits from the class catalog.
    int nc = classes_count();
    s->class_count     = nc;
    s->class_hero_walk = nc ? calloc((size_t)nc, sizeof *s->class_hero_walk) : NULL;
    s->class_hero_idle = nc ? calloc((size_t)nc, sizeof *s->class_hero_idle) : NULL;
    s->class_hero_boat = nc ? calloc((size_t)nc, sizeof *s->class_hero_boat) : NULL;
    s->class_end_hero  = nc ? calloc((size_t)nc, sizeof *s->class_end_hero) : NULL;
    s->class_portrait  = nc ? calloc((size_t)nc, sizeof *s->class_portrait) : NULL;
    s->class_disgraced = nc ? calloc((size_t)nc, sizeof *s->class_disgraced) : NULL;
    if (nc && (!s->class_hero_walk || !s->class_hero_idle || !s->class_hero_boat ||
               !s->class_end_hero || !s->class_portrait || !s->class_disgraced))
        s->class_count = nc = 0;
    for (int i = 0; i < nc; i++) {
        const ResClassHero *h = &res->class_hero[i];
        load_anim_set(&s->class_hero_walk[i], &h->walk);
        load_anim_set(&s->class_hero_idle[i], &h->idle);
        load_anim_set(&s->class_hero_boat[i], &h->boat);
        if (h->tile[0]) s->class_end_hero[i] = load_rel(h->tile);
    }
    for (int i = 0; i < nc; i++) {
        const ClassDef *c = class_by_index(i);
        s->class_portrait[i] = c ? load_rel(c->portrait) : (Texture2D){ 0 };
        s->class_disgraced[i] = load_rel(res->class_hero[i].disgraced);
    }

    // One-time vista scenes, in the pack's own order.
    int ne = res->event_scene_count;
    s->event_scene = ne ? calloc((size_t)ne, sizeof *s->event_scene) : NULL;
    s->event_scene_count = s->event_scene ? ne : 0;
    for (int i = 0; i < s->event_scene_count; i++)
        s->event_scene[i] = load_rel(res->event_scenes[i]);

    // Villain portraits from the villain catalog. Load the static portrait
    // (frame 0) plus the animation strip. A pack may declare the frames
    // explicitly as villains[].anim, exactly like a troop does; when it
    // doesn't, fall back to deriving "<stem>_00.png" .. "<stem>_03.png"
    // siblings, which is how kings-bounty addresses them. Animation is
    // driven by a global tick in the sidebar renderer.
    int nv = villains_count();
    s->villain_count       = nv;
    s->villain_portrait    = nv ? calloc((size_t)nv, sizeof *s->villain_portrait) : NULL;
    s->villain_anim_frames = nv ? calloc((size_t)nv, sizeof *s->villain_anim_frames) : NULL;
    s->villain_anim        = nv ? calloc((size_t)nv, sizeof *s->villain_anim) : NULL;
    if (nv && (!s->villain_portrait || !s->villain_anim_frames || !s->villain_anim))
        s->villain_count = nv = 0;
    for (int i = 0; i < nv; i++) {
        const VillainDef *v = villain_by_index(i);
        s->villain_portrait[i] = v ? load_rel(v->portrait) : (Texture2D){ 0 };
        if (!v) continue;
        if (v->anim_count > 0) {
            s->villain_anim[i] = load_strip((const char (*)[RES_PATH_LEN])v->anim, v->anim_count);
            s->villain_anim_frames[i] = s->villain_anim[i] ? v->anim_count : 0;
            continue;
        }
        // Strip the ".png" suffix from the portrait path to build the
        // per-frame file names.
        char stem[128];
        const char *p = v->portrait;
        int slen = 0;
        while (p[slen] && slen + 1 < (int)sizeof(stem)) {
            stem[slen] = p[slen]; slen++;
        }
        stem[slen] = '\0';
        // Portrait paths point at the frame-0 file (e.g. murray_00.png);
        // strip the "_NN.png" suffix to recover the stem so per-frame
        // siblings resolve as <stem>_00.png ... <stem>_03.png.
        if (slen >= 7 && stem[slen - 7] == '_' && stem[slen - 4] == '.') {
            stem[slen - 7] = '\0';
        } else if (slen >= 4 && stem[slen - 4] == '.') {
            stem[slen - 4] = '\0';
        }
        s->villain_anim[i] = calloc(OB_ANIM_FRAMES_DEFAULT, sizeof *s->villain_anim[i]);
        s->villain_anim_frames[i] = s->villain_anim[i] ? OB_ANIM_FRAMES_DEFAULT : 0;
        for (int f = 0; f < s->villain_anim_frames[i]; f++) {
            char framepath[160];
            snprintf(framepath, sizeof(framepath), "%s_%02d.png", stem, f);
            s->villain_anim[i][f] = load_rel(framepath);
        }
    }

    s->portrait_count = res->portrait_count;
    s->portrait_frames = s->portrait_count ? calloc((size_t)s->portrait_count, sizeof *s->portrait_frames) : NULL;
    s->portrait_anim   = s->portrait_count ? calloc((size_t)s->portrait_count, sizeof *s->portrait_anim) : NULL;
    if (!s->portrait_frames || !s->portrait_anim) s->portrait_count = 0;
    for (int i = 0; i < s->portrait_count; i++) {
        s->portrait_anim[i] = load_strip((const char (*)[RES_PATH_LEN])res->portraits[i].anim,
                                         res->portraits[i].anim_count);
        s->portrait_frames[i] = s->portrait_anim[i] ? res->portraits[i].anim_count : 0;
    }

    // View icons 0..7 from artifact catalog, 8..13 from sprites.view_icons_extra.
    int na = artifacts_count();
    s->view_icon_extra_base = na > 8 ? na : 8;
    s->view_icon_count = s->view_icon_extra_base + res->sprites.view_icons_extra_count;
    s->view_icon = calloc((size_t)s->view_icon_count, sizeof *s->view_icon);
    if (!s->view_icon) s->view_icon_count = 0;
    for (int i = 0; s->view_icon && i < na; i++) {
        const ArtifactDef *a = artifact_by_index(i);
        s->view_icon[i] = a ? load_rel(a->icon) : (Texture2D){ 0 };
    }
    for (int i = 0; s->view_icon && i < res->sprites.view_icons_extra_count; i++) {
        s->view_icon[s->view_icon_extra_base + i] = load_rel(res->sprites.view_icons_extra[i]);
    }

    // Troop sprites from the troop catalog.
    int nt = troops_count();
    s->troop_count       = nt;
    s->troop_sprite      = nt ? calloc((size_t)nt, sizeof *s->troop_sprite) : NULL;
    s->troop_portrait    = nt ? calloc((size_t)nt, sizeof *s->troop_portrait) : NULL;
    s->troop_anim_frames = nt ? calloc((size_t)nt, sizeof *s->troop_anim_frames) : NULL;
    s->troop_anim        = nt ? calloc((size_t)nt, sizeof *s->troop_anim) : NULL;
    if (nt && (!s->troop_sprite || !s->troop_portrait || !s->troop_anim_frames || !s->troop_anim)) {
        fprintf(stderr, "sprites: out of memory for %d troops\n", nt);
        s->troop_count = nt = 0;
    }
    for (int i = 0; i < nt; i++) {
        const TroopDef *t = troop_by_index(i);
        s->troop_sprite[i] = t ? load_rel(t->sprite) : (Texture2D){ 0 };
        s->troop_portrait[i] = t ? load_rel(t->portrait) : (Texture2D){ 0 };
        s->troop_anim[i] = t ? load_strip((const char (*)[RES_PATH_LEN])t->anim, t->anim_count) : NULL;
        s->troop_anim_frames[i] = s->troop_anim[i] ? t->anim_count : 0;
    }

    // UI backdrops.
    s->puzzle_cover     = load_rel(res->sprites.puzzle_cover);
    s->town_backdrop    = load_rel(res->sprites.town_backdrop);
    s->castle_backdrop  = load_rel(res->sprites.castle_backdrop);
    s->plains_backdrop  = load_rel(res->sprites.plains_backdrop);
    s->forest_backdrop  = load_rel(res->sprites.forest_backdrop);
    s->hillcave_backdrop= load_rel(res->sprites.hillcave_backdrop);
    s->dungeon_backdrop = load_rel(res->sprites.dungeon_backdrop);
    s->alcove_backdrop  = load_rel(res->sprites.alcove_backdrop);
    s->sail_backdrop    = load_rel(res->sprites.sail_backdrop);
    {
        int nz = res->zone_count;
        s->zone_town_backdrop = nz ? calloc((size_t)nz, sizeof *s->zone_town_backdrop) : NULL;
        s->zone_town_backdrop_count = s->zone_town_backdrop ? nz : 0;
        for (int i = 0; i < s->zone_town_backdrop_count; i++)
            s->zone_town_backdrop[i] = load_rel(res->zones[i].town_backdrop);
        int nt = res->town_count;
        s->town_backdrop_own = nt ? calloc((size_t)nt, sizeof *s->town_backdrop_own) : NULL;
        s->town_backdrop_count = s->town_backdrop_own ? nt : 0;
        for (int i = 0; i < s->town_backdrop_count; i++)
            s->town_backdrop_own[i] = load_rel(res->towns[i].backdrop);
    }
    s->palace[0]        = load_rel(res->sprites.palace_welcome);
    s->palace[1]        = load_rel(res->sprites.palace_barracks);
    s->palace[2]        = load_rel(res->sprites.palace_throne);
    s->scene_column[0]  = load_rel(res->sprites.scene_column_capital);
    s->scene_column[1]  = load_rel(res->sprites.scene_column_shaft);
    s->scene_column[2]  = load_rel(res->sprites.scene_column_base);
    s->alcove_figure    = load_rel(res->sprites.alcove_figure);
    s->alcove_figure_anim = load_strip((const char (*)[RES_PATH_LEN])res->sprites.alcove_figure_animation,
                                       res->sprites.alcove_figure_animation_count);
    s->alcove_figure_frames = s->alcove_figure_anim ? res->sprites.alcove_figure_animation_count : 0;
    s->ending_win       = load_rel(res->sprites.ending_win);
    s->ending_lose      = load_rel(res->sprites.ending_lose);

    // HUD panels.
    s->hud_contract_silhouette = load_rel(res->sprites.hud_contract_silhouette);
    if (res->sprites.hud_boat_silhouette[0])
        s->hud_boat_silhouette = load_rel(res->sprites.hud_boat_silhouette);
    s->hud_siege_silhouette    = load_rel(res->sprites.hud_siege_silhouette);
    s->hud_magic_silhouette    = load_rel(res->sprites.hud_magic_silhouette);
    s->hud_puzzle_grid         = load_rel(res->sprites.hud_puzzle_grid);
    s->hud_gold_purse          = load_rel(res->sprites.hud_gold_purse);
    s->hud_siege_anim = load_strip((const char (*)[RES_PATH_LEN])res->sprites.hud_siege_animation,
                                   res->sprites.hud_siege_animation_count);
    s->hud_siege_anim_frames = s->hud_siege_anim ? res->sprites.hud_siege_animation_count : 0;
    s->hud_magic_anim = load_strip((const char (*)[RES_PATH_LEN])res->sprites.hud_magic_animation,
                                   res->sprites.hud_magic_animation_count);
    s->hud_magic_anim_frames = s->hud_magic_anim ? res->sprites.hud_magic_animation_count : 0;
    s->hud_bar_strip = load_rel(res->sprites.hud_bar_strip);
    s->chrome_overworld = load_rel(res->sprites.chrome_overworld);
    s->splash_logo      = load_rel(res->sprites.splash_logo);
    s->splash_title     = load_rel(res->sprites.splash_title);
    s->title_battle     = load_rel(res->sprites.title_battle);
    s->alcove_portrait  = load_rel(res->sprites.alcove_portrait);
    s->title_eagle      = load_rel(res->sprites.title_eagle);
    s->title_words      = load_rel(res->sprites.title_words);
    s->class_picker     = load_rel(res->sprites.class_picker);
    s->class_highlight  = load_rel(res->sprites.class_highlight);
    s->class_picker_selected = load_strip((const char (*)[RES_PATH_LEN])res->sprites.class_picker_selected,
                                          res->sprites.class_picker_selected_count);
    s->class_picker_selected_count = s->class_picker_selected ? res->sprites.class_picker_selected_count : 0;
    s->orb              = load_rel(res->sprites.orb);

    // Victory cartoon tiles.
    s->end_grass  = load_rel(res->ending.grass_tile);
    s->end_carpet = load_rel(res->ending.carpet_tile);
    s->end_hero   = load_rel(res->ending.hero_tile);
    if (res->sprites.siege_back_wall[0])
        s->siege_back_wall = load_rel(res->sprites.siege_back_wall);
    if (res->sprites.siege_back_wall_left[0])
        s->siege_back_wall_end[0] = load_rel(res->sprites.siege_back_wall_left);
    if (res->sprites.siege_back_wall_right[0])
        s->siege_back_wall_end[1] = load_rel(res->sprites.siege_back_wall_right);
    s->end_throne = load_rel(res->ending.throne_backdrop);
    // Siege grid: all or nothing, so a half-loaded grid never mixes with the
    // per-code walls on the same board.
    s->siege_grid_ok = res->sprites.siege_grid[0] != '\0';
    for (int y = 0; y <= COMBAT_H; y++)
        for (int x = 0; x < COMBAT_W; x++) {
            char p[RES_PATH_LEN];
            if (resources_siege_grid_path(res, x, y, p, sizeof p))
                s->siege_grid[y][x] = load_rel(p);
            if (s->siege_grid[y][x].id == 0) s->siege_grid_ok = false;
        }

    // Combat tileset, in the role order the renderer indexes by. The list
    // lives in the manifest now (res->sprites.combat) rather than here, so a
    // pack can name its own battle art.
    // The same two rules the manifest applies (REQ-165c/d): no field tile
    // when the hero's terrain is the ground, no wall pieces under a siege grid.
    for (int i = 0; i < res->sprites.combat_count && i < 15; i++) {
        if (i == 0 && resources_combat_ground_is_terrain(res)) continue;
        if (i >= 5 && i <= 10 && res->sprites.siege_grid[0]) continue;
        s->combat_tile[i] = load_rel(res->sprites.combat[i]);
    }
}

void sprites_unload(Sprites *s) {
    unload_anim_set(&s->hero_walk);
    unload_anim_set(&s->hero_idle);
    unload_anim_set(&s->hero_boat);
    for (int i = 0; i < s->class_count; i++) {
        gfx_texture_free(s->class_portrait[i]);
        gfx_texture_free(s->class_disgraced[i]);
        gfx_texture_free(s->class_end_hero[i]);
        unload_anim_set(&s->class_hero_walk[i]);
        unload_anim_set(&s->class_hero_idle[i]);
        unload_anim_set(&s->class_hero_boat[i]);
    }
    free(s->class_portrait);  s->class_portrait = NULL;
    free(s->class_disgraced); s->class_disgraced = NULL;
    for (int i = 0; i < s->event_scene_count; i++) gfx_texture_free(s->event_scene[i]);
    free(s->event_scene); s->event_scene = NULL; s->event_scene_count = 0;
    free(s->class_end_hero);  s->class_end_hero = NULL;
    free(s->class_hero_walk); s->class_hero_walk = NULL;
    free(s->class_hero_idle); s->class_hero_idle = NULL;
    free(s->class_hero_boat); s->class_hero_boat = NULL;
    s->class_count = 0;
    for (int i = 0; i < s->villain_count; i++) {
        gfx_texture_free(s->villain_portrait[i]);
        unload_strip(&s->villain_anim[i], &s->villain_anim_frames[i]);
    }
    free(s->villain_portrait);    s->villain_portrait = NULL;
    free(s->villain_anim_frames); s->villain_anim_frames = NULL;
    free(s->villain_anim);        s->villain_anim = NULL;
    s->villain_count = 0;
    for (int i = 0; i < s->portrait_count; i++)
        unload_strip(&s->portrait_anim[i], &s->portrait_frames[i]);
    free(s->portrait_frames);
    free(s->portrait_anim);
    s->portrait_frames = NULL;
    s->portrait_anim = NULL;
    s->portrait_count = 0;
    for (int i = 0; i < s->view_icon_count; i++) gfx_texture_free(s->view_icon[i]);
    free(s->view_icon);
    s->view_icon = NULL;
    s->view_icon_count = 0;
    for (int i = 0; i < s->troop_count; i++) {
        gfx_texture_free(s->troop_sprite[i]);
        gfx_texture_free(s->troop_portrait[i]);
        unload_strip(&s->troop_anim[i], &s->troop_anim_frames[i]);
    }
    free(s->troop_sprite);      s->troop_sprite = NULL;
    free(s->troop_portrait);    s->troop_portrait = NULL;
    free(s->troop_anim_frames); s->troop_anim_frames = NULL;
    free(s->troop_anim);        s->troop_anim = NULL;
    s->troop_count = 0;
    for (int i = 0; i < 15; i++) gfx_texture_free(s->combat_tile[i]);
    gfx_texture_free(s->puzzle_cover);
    gfx_texture_free(s->town_backdrop);
    gfx_texture_free(s->castle_backdrop);
    gfx_texture_free(s->plains_backdrop);
    gfx_texture_free(s->forest_backdrop);
    gfx_texture_free(s->hillcave_backdrop);
    gfx_texture_free(s->dungeon_backdrop);
    gfx_texture_free(s->alcove_backdrop);
    gfx_texture_free(s->sail_backdrop);
    for (int i = 0; i < s->zone_town_backdrop_count; i++) gfx_texture_free(s->zone_town_backdrop[i]);
    free(s->zone_town_backdrop); s->zone_town_backdrop = NULL; s->zone_town_backdrop_count = 0;
    for (int i = 0; i < s->town_backdrop_count; i++) gfx_texture_free(s->town_backdrop_own[i]);
    free(s->town_backdrop_own); s->town_backdrop_own = NULL; s->town_backdrop_count = 0;
    for (int i = 0; i < 3; i++) gfx_texture_free(s->scene_column[i]);
    for (int i = 0; i < 3; i++) gfx_texture_free(s->palace[i]);
    gfx_texture_free(s->alcove_figure);
    unload_strip(&s->alcove_figure_anim, &s->alcove_figure_frames);
    gfx_texture_free(s->ending_win);
    gfx_texture_free(s->ending_lose);
    gfx_texture_free(s->hud_contract_silhouette);
    gfx_texture_free(s->hud_boat_silhouette);
    gfx_texture_free(s->hud_siege_silhouette);
    gfx_texture_free(s->hud_magic_silhouette);
    gfx_texture_free(s->hud_puzzle_grid);
    gfx_texture_free(s->hud_gold_purse);
    unload_strip(&s->hud_siege_anim, &s->hud_siege_anim_frames);
    unload_strip(&s->hud_magic_anim, &s->hud_magic_anim_frames);
    gfx_texture_free(s->hud_bar_strip);
    gfx_texture_free(s->chrome_overworld);
    gfx_texture_free(s->splash_logo);
    gfx_texture_free(s->splash_title);
    gfx_texture_free(s->title_battle);
    gfx_texture_free(s->alcove_portrait);
    gfx_texture_free(s->title_eagle);
    gfx_texture_free(s->title_words);
    gfx_texture_free(s->class_picker);
    gfx_texture_free(s->class_highlight);
    unload_strip(&s->class_picker_selected, &s->class_picker_selected_count);
    gfx_texture_free(s->orb);
    gfx_texture_free(s->end_grass);
    gfx_texture_free(s->end_carpet);
    gfx_texture_free(s->end_hero);
    gfx_texture_free(s->siege_back_wall);
    gfx_texture_free(s->siege_back_wall_end[0]);
    gfx_texture_free(s->siege_back_wall_end[1]);
    for (int y = 0; y <= COMBAT_H; y++)
        for (int x = 0; x < COMBAT_W; x++) gfx_texture_free(s->siege_grid[y][x]);
    gfx_texture_free(s->end_throne);
}

static int class_slot(const Sprites *s, const char *class_id) {
    if (!class_id || !class_id[0]) return -1;
    const ClassDef *c = class_by_id(class_id);
    return (c && c->index >= 0 && c->index < s->class_count) ? c->index : -1;
}

const SpriteAnim *sprites_hero_anim(const Sprites *s, const char *class_id, int kind) {
    const SpriteAnim *global = kind == 1 ? &s->hero_idle
                             : kind == 2 ? &s->hero_boat : &s->hero_walk;
    int i = class_slot(s, class_id);
    if (i < 0) return global;
    const SpriteAnim *own = kind == 1 ? &s->class_hero_idle[i]
                          : kind == 2 ? &s->class_hero_boat[i] : &s->class_hero_walk[i];
    return sprites_anim_present(own) ? own : global;
}

Texture2D sprites_end_hero(const Sprites *s, const char *class_id) {
    int i = class_slot(s, class_id);
    if (i >= 0 && s->class_end_hero[i].id) return s->class_end_hero[i];
    return s->end_hero;
}
