#include "sprites.h"
#include "assets.h"
#include "tables.h"
#include "resources.h"
#include <stdio.h>
#include <string.h>

// Sprite paths in game.json are pack-relative; LoadAssetTexture reads
// them straight from the active pack on the global pack stack.

static Texture2D load_filtered(const char *path) {
    Texture2D t = LoadAssetTexture(path);
    SetTextureFilter(t, TEXTURE_FILTER_POINT);
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

void sprites_load(Sprites *s, const Resources *res) {
    if (!res) return;
    s_res = res;

    // Hero walk + boat frames from the sprite manifest.
    for (int i = 0; i < 4; i++) {
        s->hero_walk[i] = load_rel(res->sprites.hero_walk[i]);
        s->hero_boat[i] = load_rel(res->sprites.hero_boat[i]);
    }

    // Class portraits from the class catalog.
    int nc = classes_count();
    if (nc > 4) nc = 4;
    for (int i = 0; i < nc; i++) {
        const ClassDef *c = class_by_index(i);
        s->class_portrait[i] = c ? load_rel(c->portrait) : (Texture2D){ 0 };
    }

    // Villain portraits from the villain catalog. Load the static
    // portrait (frame 0) PLUS the 4-frame animation strip if sibling
    // files named "<stem>_00.png" .. "<stem>_03.png" exist. Animation
    // is driven by global tick in the sidebar renderer.
    int nv = villains_count();
    if (nv > 17) nv = 17;
    for (int i = 0; i < nv; i++) {
        const VillainDef *v = villain_by_index(i);
        s->villain_portrait[i] = v ? load_rel(v->portrait) : (Texture2D){ 0 };
        if (!v) continue;
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
        for (int f = 0; f < 4; f++) {
            char framepath[160];
            snprintf(framepath, sizeof(framepath), "%s_%02d.png", stem, f);
            s->villain_anim[i][f] = load_rel(framepath);
        }
    }

    // View icons 0..7 from artifact catalog, 8..13 from sprites.view_icons_extra.
    int na = artifacts_count();
    if (na > 8) na = 8;
    for (int i = 0; i < na; i++) {
        const ArtifactDef *a = artifact_by_index(i);
        s->view_icon[i] = a ? load_rel(a->icon) : (Texture2D){ 0 };
    }
    for (int i = 0; i < res->sprites.view_icons_extra_count && i < 14 - 8; i++) {
        s->view_icon[8 + i] = load_rel(res->sprites.view_icons_extra[i]);
    }

    // Troop sprites from the troop catalog.
    int nt = troops_count();
    if (nt > 25) nt = 25;
    for (int i = 0; i < nt; i++) {
        const TroopDef *t = troop_by_index(i);
        s->troop_sprite[i] = t ? load_rel(t->sprite) : (Texture2D){ 0 };
        for (int f = 0; f < 4; f++) {
            s->troop_anim[i][f] =
                (t && t->anim[f][0]) ? load_rel(t->anim[f]) : (Texture2D){ 0 };
        }
    }

    // UI backdrops.
    s->puzzle_cover     = load_rel(res->sprites.puzzle_cover);
    s->town_backdrop    = load_rel(res->sprites.town_backdrop);
    s->castle_backdrop  = load_rel(res->sprites.castle_backdrop);
    s->plains_backdrop  = load_rel(res->sprites.plains_backdrop);
    s->forest_backdrop  = load_rel(res->sprites.forest_backdrop);
    s->hillcave_backdrop= load_rel(res->sprites.hillcave_backdrop);
    s->dungeon_backdrop = load_rel(res->sprites.dungeon_backdrop);
    s->ending_win       = load_rel(res->sprites.ending_win);
    s->ending_lose      = load_rel(res->sprites.ending_lose);

    // HUD panels.
    s->hud_contract_silhouette = load_rel(res->sprites.hud_contract_silhouette);
    s->hud_siege_silhouette    = load_rel(res->sprites.hud_siege_silhouette);
    s->hud_magic_silhouette    = load_rel(res->sprites.hud_magic_silhouette);
    s->hud_puzzle_grid         = load_rel(res->sprites.hud_puzzle_grid);
    s->hud_gold_purse          = load_rel(res->sprites.hud_gold_purse);
    for (int i = 0; i < 4; i++) {
        s->hud_siege_anim[i] = load_rel(res->sprites.hud_siege_animation[i]);
        s->hud_magic_anim[i] = load_rel(res->sprites.hud_magic_animation[i]);
    }
    s->hud_bar_strip = load_rel(res->sprites.hud_bar_strip);
    s->chrome_overworld = load_rel(res->sprites.chrome_overworld);
    s->splash_logo      = load_rel(res->sprites.splash_logo);
    s->splash_title     = load_rel(res->sprites.splash_title);
    s->class_picker     = load_rel(res->sprites.class_picker);
    s->class_highlight  = load_rel(res->sprites.class_highlight);
    s->orb              = load_rel(res->sprites.orb);

    // Victory cartoon tiles.
    s->end_grass  = load_rel(res->ending.grass_tile);
    s->end_carpet = load_rel(res->ending.carpet_tile);
    s->end_hero   = load_rel(res->ending.hero_tile);
    s->end_throne = load_rel(res->ending.throne_backdrop);

    // Combat tileset . Names
    // match the role each frame plays in combat scene composition.
    static const char *combat_names[15] = {
        "art/combat/field_grass.png",
        "art/combat/obstacle_01.png",
        "art/combat/obstacle_02.png",
        "art/combat/obstacle_03.png",
        "art/combat/castle_spike.png",
        "art/combat/castle_wall_01.png",
        "art/combat/castle_wall_02.png",
        "art/combat/castle_wall_03.png",
        "art/combat/castle_wall_04.png",
        "art/combat/castle_wall_05.png",
        "art/combat/castle_wall_06.png",
        "art/combat/cursor_01.png",
        "art/combat/cursor_02.png",
        "art/combat/cursor_03.png",
        "art/combat/cursor_04.png",
    };
    for (int i = 0; i < 15; i++) {
        s->combat_tile[i] = load_rel(combat_names[i]);
    }
}

void sprites_unload(Sprites *s) {
    for (int i = 0; i < 4;  i++) UnloadTexture(s->hero_walk[i]);
    for (int i = 0; i < 4;  i++) UnloadTexture(s->hero_boat[i]);
    for (int i = 0; i < 4;  i++) UnloadTexture(s->class_portrait[i]);
    for (int i = 0; i < 17; i++) {
        UnloadTexture(s->villain_portrait[i]);
        for (int f = 0; f < 4; f++) UnloadTexture(s->villain_anim[i][f]);
    }
    for (int i = 0; i < 14; i++) UnloadTexture(s->view_icon[i]);
    for (int i = 0; i < 25; i++) {
        UnloadTexture(s->troop_sprite[i]);
        for (int f = 0; f < 4; f++) UnloadTexture(s->troop_anim[i][f]);
    }
    for (int i = 0; i < 15; i++) UnloadTexture(s->combat_tile[i]);
    UnloadTexture(s->puzzle_cover);
    UnloadTexture(s->town_backdrop);
    UnloadTexture(s->castle_backdrop);
    UnloadTexture(s->plains_backdrop);
    UnloadTexture(s->forest_backdrop);
    UnloadTexture(s->hillcave_backdrop);
    UnloadTexture(s->dungeon_backdrop);
    UnloadTexture(s->ending_win);
    UnloadTexture(s->ending_lose);
    UnloadTexture(s->hud_contract_silhouette);
    UnloadTexture(s->hud_siege_silhouette);
    UnloadTexture(s->hud_magic_silhouette);
    UnloadTexture(s->hud_puzzle_grid);
    UnloadTexture(s->hud_gold_purse);
    for (int i = 0; i < 4; i++) UnloadTexture(s->hud_siege_anim[i]);
    for (int i = 0; i < 4; i++) UnloadTexture(s->hud_magic_anim[i]);
    UnloadTexture(s->hud_bar_strip);
    UnloadTexture(s->chrome_overworld);
    UnloadTexture(s->splash_logo);
    UnloadTexture(s->splash_title);
    UnloadTexture(s->class_picker);
    UnloadTexture(s->class_highlight);
    UnloadTexture(s->orb);
    UnloadTexture(s->end_grass);
    UnloadTexture(s->end_carpet);
    UnloadTexture(s->end_hero);
    UnloadTexture(s->end_throne);
}
