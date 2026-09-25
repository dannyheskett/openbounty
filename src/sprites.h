#ifndef OB_SPRITES_H
#define OB_SPRITES_H

#include "ob_types.h"
#include "resources.h"
#include "combat.h"   // COMBAT_W / COMBAT_H

// Bundle of all non-tile textures used across the game. One instance is
// loaded at startup and passed (const) to drawing modules. Arrays are
// keyed by the same numeric indices as the tables module:
//   - class_*[class_count]     matches the pack's classes[]
//   - villain_*[villain_count] matches the pack's villains[]
//   - troop_*[troop_count]     matches the pack's troops[]
//   - view_icon[view_icon_count]: the artifact icons from 0, the pack's extra
//     icons (continent maps, empty slots) from view_icon_extra_base -- 8 when a
//     pack has eight artifacts or fewer, so the legacy layout is unchanged.
// Every table is heap, sized to what the pack declares.
// Every *_frames field is the animation's cycle length as the pack declared
// it. Index an animation with sprites_frame(counter, count) rather than a
// hardcoded mask -- the cycle is pack data now, not a fixed four.
// One loaded animation: either a single strip the renderer mirrors, or four
// authored facings it selects between. Mirrors ResAnimSet in the manifest.
typedef struct {
    bool       directional;
    int        frames[OB_FACE_COUNT];
    Texture2D *tex[OB_FACE_COUNT];      // heap, frames[f] per facing
} SpriteAnim;

typedef struct {
    SpriteAnim hero_walk;
    SpriteAnim hero_idle;
    SpriteAnim hero_boat;
    // Per-class hero art, parallel to the class catalog; empty sets and a zero
    // texture where a class declared none. Read through sprites_hero_anim /
    // sprites_end_hero, which fall back to the pack-wide sets above.
    int         class_count;
    SpriteAnim *class_hero_walk;
    SpriteAnim *class_hero_idle;
    SpriteAnim *class_hero_boat;
    Texture2D  *class_end_hero;

    Texture2D  *class_portrait;
    Texture2D  *class_disgraced;   // modern: the temporary-death scene, when the pack has one
    // One-time vista scenes, parallel to res->event_scenes.
    int         event_scene_count;
    Texture2D  *event_scene;
    // villain_portrait[i] = frame 0 (still image, kept for compatibility).
    // villain_anim[i][0..villain_anim_frames[i]-1] = the animation strip.
    int         villain_count;
    Texture2D  *villain_portrait;
    int        *villain_anim_frames;
    Texture2D **villain_anim;
    // portraits[] (town informants, priests), parallel to res->portraits.
    int         portrait_count;
    int        *portrait_frames;
    Texture2D **portrait_anim;
    int         view_icon_count;
    int         view_icon_extra_base;
    Texture2D  *view_icon;
    // Heap, troop_count entries: one per troop the pack declares.
    int        troop_count;
    Texture2D *troop_sprite;
    Texture2D *troop_portrait;                         // modern: still portrait, id 0 = none
    int       *troop_anim_frames;                      // 0 = no animation, use troop_sprite
    Texture2D **troop_anim;                            // idle animation (troop.anim[])
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
    Texture2D hud_boat_silhouette;
    Texture2D hud_siege_silhouette;
    Texture2D hud_magic_silhouette;
    Texture2D hud_puzzle_grid;
    Texture2D hud_gold_purse;
    Texture2D hud_days;
    // The left rail's five tiles, in rail order.
    Texture2D rail_menu;
    Texture2D rail_map;
    Texture2D rail_army;
    Texture2D rail_search;
    Texture2D rail_cast;
    // The combat command panel; Cast reuses rail_cast.
    Texture2D combat_shoot;
    Texture2D combat_wait;
    Texture2D combat_fly;
    int        hud_siege_anim_frames;
    Texture2D *hud_siege_anim;
    int        hud_magic_anim_frames;
    Texture2D *hud_magic_anim;
    // The magic alcove's own backdrop and the figure who keeps it. Both are
    // optional; id 0 means the pack declared none and the alcove falls back to
    // the hill cave's backdrop and to animating a troop.
    Texture2D alcove_backdrop;
    Texture2D sail_backdrop;       // the sail-to scene, when the pack ships one
    // Town screen backdrops a pack declares per zone and per town (REQ-221d);
    // an empty texture means "fall back".
    int        zone_town_backdrop_count;
    Texture2D *zone_town_backdrop;
    int        town_backdrop_count;
    Texture2D *town_backdrop_own;
    Texture2D scene_column[3];   // modern: capital, shaft, base (id 0: the lattice)
    Texture2D palace[3];         // the Emperor's castle: welcome, barracks, throne
    Texture2D alcove_figure;
    int        alcove_figure_frames;
    Texture2D *alcove_figure_anim;
    Texture2D hud_bar_strip;             // 320x5 horizontal middle bar
    Texture2D chrome_overworld;          // 320x200 chrome frame (transparent interior)
    Texture2D splash_logo;                // 320x84 publisher logo
    Texture2D splash_title;               // 320x200 game title
    Texture2D title_battle, title_eagle, title_words;   // modern title sequence layers
    Texture2D alcove_portrait;            // modern temple: the keeper's portrait
    Texture2D class_picker;               // 288x184 class portraits (A-D)
    Texture2D class_highlight;            // 42x44 cursor glow over current pick
    int        class_picker_selected_count;
    Texture2D *class_picker_selected;      // modern: the picker with one figure picked out
    Texture2D orb;                        // orb of power tile overlay

    // Victory cartoon .
    Texture2D end_grass;
    Texture2D end_carpet;
    Texture2D end_hero;
    Texture2D end_throne;
    Texture2D siege_back_wall;            // optional band above the siege board
    Texture2D siege_back_wall_end[2];     // its end cells, left and right
    // Optional full siege grid (sprites.ui.siege_grid): row 0 the band above
    // the board, rows 1..COMBAT_H the board. siege_grid_ok only when every
    // tile loaded; then the renderer draws these instead of the per-code walls.
    bool      siege_grid_ok;
    Texture2D siege_grid[COMBAT_H + 1][COMBAT_W];
    // Optional open-field grids, one per zone (a zone's field_grid, else the
    // pack's): field_grid_ok[z] only when every cell of zone z loaded; then
    // the renderer draws them as the ground of an open fight there.
    int        field_grid_zones;
    bool      *field_grid_ok;                       // heap, field_grid_zones
    Texture2D *field_grid;                          // heap, zones x COMBAT_H x COMBAT_W

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

// The frame of a heap strip at `counter`, or a zero texture when the strip
// has no frames.
static inline Texture2D sprites_strip(const Texture2D *strip, int count, int counter) {
    if (!strip || count <= 0) return (Texture2D){ 0 };
    return strip[sprites_frame(counter, count)];
}

// Modern: a standing figure rocks between frames 0 and 1 instead of playing its
// whole strip, which reads as an attack. Pass the result as the counter.
static inline int sprites_stand(int counter) {
    return (counter < 0 ? -counter : counter) % 2;
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
        if (a->frames[OB_FACE_SOUTH] <= 0 || !a->tex[OB_FACE_SOUTH]) return none;
        return a->tex[OB_FACE_SOUTH][sprites_frame(counter,
                                                   a->frames[OB_FACE_SOUTH])];
    }
    if (a->frames[facing] <= 0) facing = OB_FACE_SOUTH;
    if (a->frames[facing] <= 0 || !a->tex[facing]) return none;
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
// The open-field ground cell (x, y) of zone z, when sprites_field_grid_ok(s, z).
bool      sprites_field_grid_ok(const Sprites *s, int z);
Texture2D sprites_field_cell(const Sprites *s, int z, int x, int y);
void sprites_unload(Sprites *s);

#endif
