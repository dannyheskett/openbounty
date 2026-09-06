#ifndef OB_SPRITES_H
#define OB_SPRITES_H

#include "raylib.h"
#include "resources.h"   // OB_ANIM_FRAMES_MAX (via tables.h)

// Bundle of all non-tile textures used across the game. One instance is
// loaded at startup and passed (const) to drawing modules. Arrays are
// keyed by the same numeric indices as the tables module:
//   - class_portrait[4]        matches CLASSES[]
//   - villain_portrait[17]     matches VILLAINS[]
//   - troop_sprite[25]         matches TROOPS[]
//   - view_icon[14]            0-7 artifacts, 8-11 maps, 12 empty, 13 empty-map
// Every *_frames field is the animation's cycle length as the pack declared
// it. Index an animation with sprites_frame(counter, count) rather than a
// hardcoded mask -- the cycle is pack data now, not a fixed four.
// One loaded animation: either a single strip the renderer mirrors, or four
// authored facings it selects between. Mirrors ResAnimSet in the manifest.
typedef struct {
    bool      directional;
    int       frames[OB_FACE_COUNT];
    Texture2D tex[OB_FACE_COUNT][OB_ANIM_FRAMES_MAX];
} SpriteAnim;

typedef struct {
    SpriteAnim hero_walk;
    SpriteAnim hero_idle;
    SpriteAnim hero_boat;
    // Per-class hero art, parallel to the class catalog; empty sets and a zero
    // texture where a class declared none. Read through sprites_hero_anim /
    // sprites_end_hero, which fall back to the pack-wide sets above.
    SpriteAnim class_hero_walk[4];
    SpriteAnim class_hero_idle[4];
    SpriteAnim class_hero_boat[4];
    Texture2D  class_end_hero[4];

    Texture2D class_portrait[4];
    // villain_portrait[i] = frame 0 (still image, kept for compatibility).
    // villain_anim[i][0..villain_anim_frames[i]-1] = the animation strip.
    Texture2D villain_portrait[17];
    int       villain_anim_frames[17];
    Texture2D villain_anim[17][OB_ANIM_FRAMES_MAX];
    Texture2D view_icon[14];
    Texture2D troop_sprite[25];
    int       troop_anim_frames[25];    // 0 = no animation, use troop_sprite
    Texture2D troop_anim[25][OB_ANIM_FRAMES_MAX];   // idle animation (troop.anim[])
    Texture2D puzzle_cover;
    // Location backdrops (240x102), used by location-screen views
    // (VIEW_TOWN, VIEW_HOME_CASTLE, VIEW_DWELLING, VIEW_ALCOVE, ...).
    // GR_LOCATION sub_ids 0..5 -> six images.
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
    int       hud_siege_anim_frames;
    Texture2D hud_siege_anim[OB_ANIM_FRAMES_MAX];
    int       hud_magic_anim_frames;
    Texture2D hud_magic_anim[OB_ANIM_FRAMES_MAX];
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

// Map a free-running animation counter onto a cycle of `count` frames.
// `count` <= 0 means the pack declared no frames, so this yields 0 and the
// caller draws whatever still image it has. Every animated draw site goes
// through this instead of masking the counter against a fixed frame count.
static inline int sprites_frame(int counter, int count) {
    if (count <= 0) return 0;
    if (counter < 0) counter = -counter;
    return counter % count;
}

// Pick the texture for `facing` at animation tick `counter`, and report
// through *out_mirror whether the caller must flip it horizontally.
//
// A single-strip animation always returns its one strip and asks to be
// mirrored when facing west, which is exactly what the game did before
// facings existed. A directional animation returns the authored facing and
// never asks for a mirror; if that facing was left undeclared it falls back
// to south rather than drawing nothing.
static inline Texture2D sprites_anim_tex(const SpriteAnim *a, int facing,
                                         int counter, bool *out_mirror) {
    Texture2D none = (Texture2D){ 0 };
    if (out_mirror) *out_mirror = false;
    if (!a) return none;
    if (facing < 0 || facing >= OB_FACE_COUNT) facing = OB_FACE_SOUTH;
    if (!a->directional) {
        if (out_mirror) *out_mirror = (facing == OB_FACE_WEST);
        return a->tex[OB_FACE_SOUTH][sprites_frame(counter,
                                                   a->frames[OB_FACE_SOUTH])];
    }
    if (a->frames[facing] <= 0) facing = OB_FACE_SOUTH;
    return a->tex[facing][sprites_frame(counter, a->frames[facing])];
}

// True when the pack declared any frames for this animation at all.
static inline bool sprites_anim_present(const SpriteAnim *a) {
    if (!a) return false;
    for (int f = 0; f < OB_FACE_COUNT; f++)
        if (a->frames[f] > 0) return true;
    return false;
}

void sprites_load(Sprites *s, const Resources *res);

// The hero animation set for a class: 0 = walk, 1 = idle, 2 = boat. The
// class's own set when the pack declared one, else the pack-wide set.
const SpriteAnim *sprites_hero_anim(const Sprites *s, const char *class_id, int kind);
// The win-cartoon hero tile for a class, else the pack-wide ending tile.
Texture2D sprites_end_hero(const Sprites *s, const char *class_id);
void sprites_unload(Sprites *s);

#endif
