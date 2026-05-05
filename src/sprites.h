#ifndef SPRITES_H
#define SPRITES_H

#include "raylib.h"

// Bundle of all non-tile textures used across the game. One instance is
// loaded at startup and passed (const) to drawing modules. Arrays are
// keyed by the same numeric indices as the tables module:
//   - class_portrait[4]        matches CLASSES[]
//   - villain_portrait[17]     matches VILLAINS[]
//   - troop_sprite[25]         matches TROOPS[]
//   - view_icon[14]            0-7 artifacts, 8-11 maps, 12 empty, 13 empty-map
typedef struct {
    Texture2D hero_walk[4];
    Texture2D hero_boat[4];

    Texture2D class_portrait[4];
    // villain_portrait[i] = frame 0 (still image, kept for compatibility).
    // villain_anim[i][0..3] = 4-frame animation strip .
    Texture2D villain_portrait[17];
    Texture2D villain_anim[17][4];
    Texture2D view_icon[14];
    Texture2D troop_sprite[25];
    Texture2D troop_anim[25][4];        // 4-frame idle animation (troop.anim[])
    Texture2D puzzle_cover;
    // Location backdrops (240x102), used by location-screen views
    // (VIEW_TOWN, VIEW_HOME_CASTLE, VIEW_DWELLING, VIEW_ALCOVE, ...).
    // GR_LOCATION sub_ids 0..5 → six images.
    Texture2D town_backdrop;
    Texture2D castle_backdrop;
    Texture2D plains_backdrop;
    Texture2D forest_backdrop;
    Texture2D hillcave_backdrop;
    Texture2D dungeon_backdrop;
    // End-game images .
    Texture2D ending_win;
    Texture2D ending_lose;

    Texture2D hud_contract_silhouette;
    Texture2D hud_siege_silhouette;
    Texture2D hud_magic_silhouette;
    Texture2D hud_puzzle_grid;
    Texture2D hud_gold_purse;
    Texture2D hud_siege_anim[4];
    Texture2D hud_magic_anim[4];
    Texture2D hud_bar_strip;             // 320x5 horizontal middle bar
    Texture2D chrome_overworld;          // 320x200 chrome frame (transparent interior)
    Texture2D splash_logo;                // 320x84 publisher logo
    Texture2D splash_title;               // 320x200 game title
    Texture2D class_picker;               // 288x184 class portraits (A-D)
    Texture2D class_highlight;            // 42x44 cursor glow over current pick
    Texture2D orb;                        // orb of power tile overlay

    // Victory cartoon .
    Texture2D end_grass;
    Texture2D end_carpet;
    Texture2D end_hero;
    Texture2D end_throne;

    // Combat tileset .
    //   [0]      grass field background
    //   [1..3]   random obstacles (boulder, tree-cluster, mound)
    //   [4]      decorative castle item
    //   [5..10]  castle wall pieces (used by castle_omap codes 5-10)
    //   [11..14] cursor sprites (active ring, target ring, arrow ring,
    //            small animation frame)
    Texture2D combat_tile[15];
} Sprites;

#include "resources.h"
void sprites_load(Sprites *s, const Resources *res);
void sprites_unload(Sprites *s);

#endif
