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
#include "resources.h"

#include <stdbool.h>

#define GB_PATH_MAX     512
#define GB_RECENT_MAX     8
#define GB_STATUS_MAX   256

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
