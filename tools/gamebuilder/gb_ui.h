// Shared UI types for GameBuilder's modes.
//
// Kept apart from gb.h so the non-UI layers (workspace, undo, objects,
// validation) stay linkable into the test binary without raylib.

#ifndef OB_GB_UI_H
#define OB_GB_UI_H

#include "gb.h"

#include "raylib.h"

typedef enum {
    GB_TOOL_PAINT = 0, GB_TOOL_FILL, GB_TOOL_RECT, GB_TOOL_PICK, GB_TOOL_COUNT
} GbTool;

// The shared map canvas. Maps and Objects modes both drive this: terrain
// underneath, objects on top, one pan/zoom between them so switching modes
// does not move the view out from under the user.
typedef struct {
    int      zone;              // index into the pack's zones array
    Vector2  origin, size;      // canvas rect on screen
    Vector2  pan;
    float    zoom;
    bool     show_grid;
    bool     show_objects;
    bool     flat_view;         // terrain colours instead of tile art
    GbTool   tool;
    int      brush;             // odd, 1..15
    Terrain  brush_terrain;
    int      selected;          // object index, -1 for none
    bool     dragging;
    Vector2  drag_from;
} GbMapView;

void gb_mapview_init(GbMapView *v);

// One frame of the canvas. `objects_mode` decides whether the mouse edits
// terrain or moves objects.
void gb_mapview_frame(GbMapView *v, MapGrid *g, GbObjectList *objs,
                      GbUndo *undo, bool objects_mode, cJSON *doc,
                      const char *zone_id, char *status, size_t status_sz);

// --- pixel editor (GB-250..255) ----------------------------------------------
//
// Opened from the Art tab on any image. Scoped to the pack: the image's own
// dimensions, the pack palette as the only selectable colours, binary alpha.

bool gb_pixel_open(const char *pack_rel_path);
bool gb_pixel_save(const char *pack_root);
void gb_pixel_close(void);
bool gb_pixel_is_open(void);
bool gb_pixel_dirty(void);
void gb_pixel_frame(int top, const char *pack_root, char *status,
                    size_t status_sz);

// --- art import and palette (GB-241/242, GB-260/262) -------------------------
//
// Import never silently alters the source. Problems are named; fix-up is a
// separate, explicit act on the copy written into the pack.

#define GB_ART_MSG 160
#define GB_ART_MAX_PROBLEMS 4

typedef struct {
    int  w, h;
    int  soft_alpha;      // pixels with partial transparency
    int  off_palette;     // pixels outside the pack palette
    char problem[GB_ART_MAX_PROBLEMS][GB_ART_MSG];
    int  problems;
} GbArtReport;

int  gb_art_check(const char *src_file, const char *pack_rel, GbArtReport *out);
bool gb_art_import(const char *src_file, const char *pack_root,
                   const char *pack_rel, bool fixup, char *err, size_t errsz);

bool gb_palette_save(const char *pack_root, const char *rel);
void gb_palette_set(int index, Color c);

const char *gb_terrain_name(int t);
Color       gb_terrain_color(int t);
Color       gb_object_color(int k);

#endif
