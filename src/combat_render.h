#ifndef OB_COMBAT_RENDER_H
#define OB_COMBAT_RENDER_H

#include "combat.h"
#include "layout.h"
#include "sprites.h"
#include "resources.h"

// Render one frame of the combat view. Caller is responsible for
// BeginDrawing / EndDrawing and for any post-frame scaling. The
// renderer paints into the same 320x200 design space the rest of
// the chrome uses.
// Ground drawn under every combat cell when set (sprites.ui.combat_ground
// "terrain": the map tile the hero stands on); a zero texture means the
// pack's field tile, sprites.combat[0]. Set by the shell before RunCombat.
void combat_render_set_ground(Texture2D ground);

// Modern: the troop on `side` standing at (x, y) is drawn at `frame` of its
// strip -- an attack playing out. frame < 0 clears it. Legacy never sets it.
void combat_render_set_attack(int side, int x, int y, int frame);

// Modern: whether the playing blow has LANDED. Before impact a unit the blow
// has already damaged (engine-side) is drawn with its pre-blow count and no
// splat -- the number falls, and the splat appears, on the swing's last
// frame, not when the engine did the arithmetic (REQ-398).
void combat_render_set_impact(bool landed);

// A troop's count on the foot of its cell (x, y, w, h): the one badge on the
// field.
void combat_count_badge(int x, int y, int w, int h, int count);

void combat_render_frame(const Combat *c, const Game *g,
                         const Sprites *sprites);

// combat_log / combat_log_template now live in engine/include/combat.h
// (engine/combat_log.c implements them; they're pure data + string
// substitution, no rendering).

// Build the title-bar string.
// Pre-first-kill: "Options / <Actor> M<n>"
// Post-first-kill: "<Player> vs <Foe> killing <N>"
// Buf must be at least COMBAT_BANNER_LEN.
void combat_format_title(const Combat *c, const Game *g, char *buf, int cap);

// Combat-field pixel layout (320x200 design space). Combat occupies
// the full inner area inside the chrome -- no sidebar; the field
// extends to the right border.
//
//   x range: 16 .. 303  -> width 288
//   y range: 22 .. 191  -> height 170
//   cells:   6 x 5      -> 48 x 34 per cell, matches 48-wide sprites
//
// 22 = CL_FRAME_TOP_H (8) + CL_STATUS_H (9) + CL_BAR_H (5)
// 192 = CL_BOTTOM_Y; bottom 8 is the chrome strip.
// The 6x5 grid is a rule of the game and never changes. What changes is the
// cell, which is one map tile, so combat sprites are the size the pack authored
// them at. The field is centred in the chrome interior. In legacy that
// arithmetic lands on exactly the historic numbers -- the grid is 288x170,
// the interior is 288 wide and the map band 170 tall, so both offsets are
// zero and the field sits at 16,22 as it always has.
#define CL_COMBAT_CELL_W  CL_TILE_W
#define CL_COMBAT_CELL_H  CL_TILE_H
#define CL_COMBAT_W       (CL_COMBAT_CELL_W * COMBAT_W)
#define CL_COMBAT_H       (CL_COMBAT_CELL_H * COMBAT_H)
// Modern: the field is where page_combat put it (set each frame by
// combat_render_set_field) -- the interior left of the battle's one column.
int combat_field_x(void);
int combat_field_y(void);
#define CL_COMBAT_X       (CL_IS_MODERN ? combat_field_x() : \
                           (CL_FRAME_LEFT_W + \
                            ((CL_SCREEN_W - CL_FRAME_LEFT_W - CL_FRAME_RIGHT_W) \
                             - CL_COMBAT_W) / 2))
#define CL_COMBAT_Y       (CL_IS_MODERN ? combat_field_y() : \
                           (CL_MAP_Y + (CL_MAP_H - CL_COMBAT_H) / 2))

// Modern: the field's top-left cell, and whether the castle's back wall
// shows above it (a siege the screen has room for).
void combat_render_set_field(int x, int y, bool wall);

#endif
