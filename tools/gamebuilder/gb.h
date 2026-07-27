// GameBuilder -- OpenBounty's game-pack editor.
//
// See docs/GAMEBUILDER-SPEC.md for the requirement set (GB- namespace).
//
// Model note (GB-114 / GB-121): the workspace's source of truth is the cJSON
// DOM of game.json, NOT the engine's parsed Resources. Resources is a lossy
// read-only projection -- fixed array caps, unknown keys dropped -- so saving
// from it would silently damage a pack that uses engine features this build
// does not know about. Resources is derived from the DOM for preview only.

#ifndef OB_GB_H
#define OB_GB_H

#include "cJSON.h"
#include "mapedit.h"
#include "resources.h"

#include <stdarg.h>
#include <stdbool.h>

#define GB_PATH_MAX     512
#define GB_RECENT_MAX     8
#define GB_STATUS_MAX   256
#define GB_UNDO_MAX     256
#define GB_MAX_ZONES    RES_MAX_ZONES

// --- workspace ---------------------------------------------------------------

typedef struct {
    bool   open;
    char   root[GB_PATH_MAX];      // loose directory being edited
    char   from_archive[GB_PATH_MAX];  // source .openbounty, "" if a directory
    cJSON *doc;                    // game.json DOM: the source of truth
    bool   dirty;

    // Derived, for preview. Rebuilt from the DOM after a commit; never the
    // thing that gets written back.
    Resources res;
    bool      res_valid;

    char base_pack[GB_PATH_MAX];   // explicitly chosen base, "" if none (GB-112)
} GbWorkspace;

// Open a pack directory (or an extracted archive) as the workspace. Returns
// false and leaves ws untouched on failure, writing the reason to `err`.
bool gb_workspace_open(GbWorkspace *ws, const char *path,
                       char *err, size_t errsz);

// Write game.json back. Only game.json: other files are written by the modes
// that own them. Output is stable (GB-114) -- an unchanged workspace saves
// byte-identically.
bool gb_workspace_save(GbWorkspace *ws, char *err, size_t errsz);

void gb_workspace_close(GbWorkspace *ws);

// Rebuild ws->res from the DOM so previews reflect uncommitted edits.
bool gb_workspace_reproject(GbWorkspace *ws);

// --- undo / redo (GB-102) ----------------------------------------------------
//
// One history across every mode. Command-based: entries record what changed,
// not a copy of the world, because a pack is a JSON DOM plus up to eight map
// grids plus images and snapshotting that per brush stroke is not viable.

typedef struct GbUndo GbUndo;

// The history does not know how Maps mode stores its grids, so the caller
// supplies a lookup and an after-change hook (used to re-furnish).
typedef struct {
    void    *user;
    MapGrid *(*grid_for)(void *user, int zone);
    void     (*after_tiles)(void *user, int zone);
} GbUndoApply;

GbUndo *gb_undo_create(void);
void    gb_undo_destroy(GbUndo *u);
void    gb_undo_clear(GbUndo *u);

// Record a replaced DOM node. `index` is -1 for object members. `before` or
// `after` may be NULL to mean absent.
void gb_undo_push_json(GbUndo *u, const char *label, cJSON *parent,
                       const char *key, int index,
                       const cJSON *before, const cJSON *after);

// Record a rectangle of terrain before and after an edit.
void gb_undo_push_tiles(GbUndo *u, const char *label, int zone,
                        int x, int y, int w, int h,
                        const MapCell *before, const MapCell *after);

bool        gb_undo_can_undo(const GbUndo *u);
bool        gb_undo_can_redo(const GbUndo *u);
const char *gb_undo_label(const GbUndo *u, bool redo);
bool        gb_undo_undo(GbUndo *u, GbUndoApply *ctx);
bool        gb_undo_redo(GbUndo *u, GbUndoApply *ctx);
int         gb_undo_depth(const GbUndo *u);

// --- zone objects (GB-210..215) ----------------------------------------------

#define GB_MAX_OBJECTS 1024

typedef enum {
    GB_OBJ_TOWN = 0, GB_OBJ_CASTLE, GB_OBJ_CHEST, GB_OBJ_SIGN,
    GB_OBJ_DWELLING, GB_OBJ_ARMY,
    GB_OBJ_HERO_SPAWN, GB_OBJ_HOME_SPAWN, GB_OBJ_ALCOVE,
    GB_OBJ_KINDS
} GbObjKind;

extern const char *const GB_OBJ_NAME[GB_OBJ_KINDS];

// A placement, pointing INTO the DOM. `node` is not owned; editing it edits
// the pack. `owner` is the containing array (NULL for the zone's singular
// points, which cannot be deleted) and `index` its position in that array.
typedef struct {
    GbObjKind kind;
    cJSON    *node;
    cJSON    *owner;
    int       index;
    int       x, y;
    char      label[64];
} GbObject;

typedef struct {
    GbObject item[GB_MAX_OBJECTS];
    int      count;
} GbObjectList;

void   gb_objects_collect(GbObjectList *L, cJSON *doc, const char *zone_id);
bool   gb_object_move(GbObject *o, int x, int y);
bool   gb_object_delete(GbObject *o);
cJSON *gb_object_create(cJSON *doc, const char *zone_id, GbObjKind kind,
                        int x, int y);
void   gb_castle_footprint(int gate_x, int gate_y, int *x0, int *y0,
                           int *w, int *h);

int         gb_zone_count(cJSON *doc);
const char *gb_zone_id_at(cJSON *doc, int index);

// --- validation (GB-300..305) ------------------------------------------------

#define GB_MAX_FINDINGS 512

typedef enum {
    GB_TIER_STRUCTURAL = 0, GB_TIER_REFERENTIAL, GB_TIER_SPATIAL, GB_TIER_COUNT
} GbTier;

typedef struct {
    GbTier tier;
    char   where[64];     // file, zone or entry id
    char   what[256];     // what is wrong AND what to do about it
} GbFinding;

typedef struct {
    GbFinding item[GB_MAX_FINDINGS];
    int       count;
    int       by_tier[GB_TIER_COUNT];
    int       overflow;
} GbFindings;

// All findings are advisory (GB-320): nothing here blocks packaging.
void        gb_validate(GbFindings *F, GbWorkspace *ws,
                        MapGrid *const *grids, const bool *loaded, int nzones);
const char *gb_tier_name(GbTier t);

// --- packaging (GB-320..324) -------------------------------------------------

// Zip the workspace into `out_zip`. Reports what it wrote; never refuses on
// validation findings.
bool gb_package(GbWorkspace *ws, const char *out_zip, char *err, size_t errsz);

// --- new pack (GB-400) -------------------------------------------------------

// Write a minimal pack that loads and runs into an empty directory. Generated,
// never copied, so it carries no copyright-restricted content (GB-401).
bool gb_newpack_create(const char *dir, char *err, size_t errsz);

// --- modes drawn elsewhere ---------------------------------------------------

void gb_mode_draw(int mode, GbWorkspace *ws, GbUndo *undo, int top,
                  char *status, size_t status_sz);

// --- archives and autosave (GB-111 / GB-103) ---------------------------------

// Extract a .openbounty into a working directory under the user data folder
// and return its path. The archive is never edited in place.
bool gb_archive_extract(const char *archive, char *out_dir, size_t out_sz,
                        char *err, size_t errsz);

// Autosave writes only the manifest, beside the pack, never into it.
bool gb_autosave_write(const GbWorkspace *ws);
bool gb_autosave_exists(const char *root);
bool gb_autosave_recover(GbWorkspace *ws, char *err, size_t errsz);
void gb_autosave_discard(const GbWorkspace *ws);

// --- recent packs (GB-017) ---------------------------------------------------

typedef struct {
    char path[GB_RECENT_MAX][GB_PATH_MAX];
    int  count;
} GbRecent;

void gb_recent_load(GbRecent *r);
void gb_recent_add(GbRecent *r, const char *path);
void gb_recent_save(const GbRecent *r);

// --- file browser (GB-018) ---------------------------------------------------
//
// In-app, not a native dialog: raylib provides none, and shelling out to
// zenity/kdialog adds a runtime dependency that fails on a bare system.

typedef enum {
    GB_PICK_DIR,        // choose a directory (a loose pack)
    GB_PICK_PACK,       // choose a directory or a .openbounty file
} GbPickMode;

typedef struct GbBrowser GbBrowser;

GbBrowser *gb_browser_create(void);
void       gb_browser_destroy(GbBrowser *b);

// Open the browser at `start` (NULL = home). Non-blocking: call gb_browser_draw
// each frame while active.
void gb_browser_open(GbBrowser *b, GbPickMode mode, const char *start,
                     const char *title);
bool gb_browser_active(const GbBrowser *b);

// Draw and step one frame. Returns true when the user committed a choice,
// writing it to `out`. Cancelling closes the browser and returns false.
bool gb_browser_draw(GbBrowser *b, int x, int y, int w, int h,
                     char *out, size_t outsz);

#endif
