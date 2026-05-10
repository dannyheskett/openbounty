#ifndef OB_COMBAT_RENDER_H
#define OB_COMBAT_RENDER_H

#include "combat.h"
#include "sprites.h"
#include "resources.h"

// Render one frame of the combat view. Caller is responsible for
// BeginDrawing / EndDrawing and for any post-frame scaling. The
// renderer paints into the same 320x200 design space the rest of
// the chrome uses.
void combat_render_frame(const Combat *c, const Game *g,
                         const Sprites *sprites);

// Push one log line. Format-string flavor for engine-side messages.
void combat_log(Combat *c, const char *fmt, ...);

// Push one log line by expanding a %TOKEN%-style template
// (typically a field of `Resources.combat_log`). Vars and nvars are
// the same shape resources_format_template accepts. NULL or empty
// template is a no-op.
void combat_log_template(Combat *c, const char *template_str,
                         const ResTemplateVar *vars, int nvars);

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
#define CL_COMBAT_X       16
#define CL_COMBAT_Y       22
#define CL_COMBAT_W       288
#define CL_COMBAT_H       170
#define CL_COMBAT_CELL_W  48    // 288 / COMBAT_W (=6)
#define CL_COMBAT_CELL_H  34    // 170 / COMBAT_H (=5)

#endif
