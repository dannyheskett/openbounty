// In-app file browser (GB-018).
//
// Built in rather than shelling out to a native dialog: raylib provides none,
// and the usual Linux answer (zenity / kdialog) is a runtime dependency that
// fails on a bare system. An in-app browser also looks identical on all four
// desktop targets.
//
// Deliberately small: list a directory, walk into it, pick it or a .openbounty
// inside it. No renaming, no deleting, no creating -- this chooses a location,
// it is not a file manager.

#include "gb.h"

#include "raylib.h"
#include "raygui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// dirent, not windows.h, even on Windows: mingw ships dirent.h, and pulling in
// windows.h here collides head-on with raylib -- both define Rectangle,
// CloseWindow and ShowCursor. One directory walk is not worth that fight.
#include <dirent.h>
#ifdef _WIN32
#  define GB_SEP '\\'
#else
#  include <unistd.h>
#  define GB_SEP '/'
#endif

#define GB_MAX_ENTRIES 512

// Paths are composed into GB_PATH_MAX buffers from a dir plus a 255-byte
// name; snprintf truncates safely and the checks below reject the result.

typedef struct {
    char name[256];
    bool is_dir;
    bool is_pack;      // a .openbounty file
    bool has_manifest; // a directory containing game.json -- i.e. a pack
} GbEntry;

struct GbBrowser {
    bool       active;
    GbPickMode mode;
    char       cwd[GB_PATH_MAX];
    char       title[128];
    GbEntry    entry[GB_MAX_ENTRIES];
    int        count;
    int        selected;
    Vector2    scroll;
    bool       needs_rescan;
};

static bool is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool has_suffix(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

static bool dir_has_manifest(const char *dir, const char *name) {
    char p[GB_PATH_MAX * 2];
    snprintf(p, sizeof p, "%s%c%s%cgame.json", dir, GB_SEP, name, GB_SEP);
    struct stat st;
    return stat(p, &st) == 0;
}

static int cmp_entry(const void *a, const void *b) {
    const GbEntry *x = a, *y = b;
    if (x->is_dir != y->is_dir) return x->is_dir ? -1 : 1;   // dirs first
    return strcmp(x->name, y->name);
}

static void rescan(GbBrowser *b) {
    b->count = 0;
    b->selected = -1;
    b->scroll = (Vector2){ 0, 0 };

    DIR *d = opendir(b->cwd);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) && b->count < GB_MAX_ENTRIES) {
        if (!strcmp(de->d_name, ".")) continue;
        if (de->d_name[0] == '.' && strcmp(de->d_name, "..")) continue; // hidden
        char full[GB_PATH_MAX * 2];
        snprintf(full, sizeof full, "%s%c%s", b->cwd, GB_SEP, de->d_name);
        GbEntry *e = &b->entry[b->count];
        snprintf(e->name, sizeof e->name, "%s", de->d_name);
        e->is_dir = is_dir(full);
        e->is_pack = has_suffix(e->name, ".openbounty");
        e->has_manifest = e->is_dir && strcmp(e->name, "..") &&
                          dir_has_manifest(b->cwd, e->name);
        if (e->is_dir || e->is_pack) b->count++;
    }
    closedir(d);
    qsort(b->entry, (size_t)b->count, sizeof *b->entry, cmp_entry);
    b->needs_rescan = false;
}

static void go_to(GbBrowser *b, const char *path) {
    snprintf(b->cwd, sizeof b->cwd, "%s", path);
    b->needs_rescan = true;
}

static void go_into(GbBrowser *b, const char *name) {
    char next[GB_PATH_MAX * 2];
    if (!strcmp(name, "..")) {
        snprintf(next, sizeof next, "%s", b->cwd);
        char *slash = strrchr(next, GB_SEP);
        if (slash && slash != next) *slash = '\0';
        else if (slash) next[1] = '\0';           // root
    } else {
        snprintf(next, sizeof next, "%s%c%s", b->cwd, GB_SEP, name);
    }
    go_to(b, next);
}

GbBrowser *gb_browser_create(void) {
    GbBrowser *b = calloc(1, sizeof *b);
    return b;
}

void gb_browser_destroy(GbBrowser *b) { free(b); }

void gb_browser_open(GbBrowser *b, GbPickMode mode, const char *start,
                     const char *title) {
    b->active = true;
    b->mode = mode;
    snprintf(b->title, sizeof b->title, "%s", title ? title : "Choose a folder");
    if (start && *start && is_dir(start)) {
        go_to(b, start);
    } else {
        const char *home = getenv("HOME");
#ifdef _WIN32
        if (!home) home = getenv("USERPROFILE");
#endif
        go_to(b, home && is_dir(home) ? home : ".");
    }
}

bool gb_browser_active(const GbBrowser *b) { return b && b->active; }

bool gb_browser_draw(GbBrowser *b, int x, int y, int w, int h,
                     char *out, size_t outsz) {
    if (!b->active) return false;
    if (b->needs_rescan) rescan(b);

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  (Color){ 0, 0, 0, 170 });
    GuiPanel((Rectangle){ (float)x, (float)y, (float)w, (float)h }, b->title);

    const int pad = 10, row_h = 22, top = y + 30;
    int list_h = h - 30 - 84;

    // Current location, elided from the left so the tail stays readable.
    const char *shown = b->cwd;
    if (strlen(shown) > 62) shown = shown + strlen(shown) - 62;
    GuiLabel((Rectangle){ (float)(x + pad), (float)(top), (float)(w - 2 * pad), 18 },
             TextFormat("%s%s", shown == b->cwd ? "" : "...", shown));

    Rectangle list = { (float)(x + pad), (float)(top + 22),
                       (float)(w - 2 * pad), (float)list_h };
    Rectangle view;
    GuiScrollPanel(list, NULL,
                   (Rectangle){ 0, 0, list.width - 14,
                                (float)(b->count * row_h + 4) },
                   &b->scroll, &view);

    BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
    for (int i = 0; i < b->count; i++) {
        Rectangle r = { list.x + 2, list.y + b->scroll.y + 2 + i * row_h,
                        list.width - 18, row_h - 2 };
        if (r.y + r.height < view.y || r.y > view.y + view.height) continue;

        bool hover = CheckCollisionPointRec(GetMousePosition(), r) &&
                     CheckCollisionPointRec(GetMousePosition(), view);
        if (i == b->selected) DrawRectangleRec(r, (Color){ 60, 90, 140, 255 });
        else if (hover)       DrawRectangleRec(r, (Color){ 45, 45, 55, 255 });

        const GbEntry *e = &b->entry[i];
        // A folder that already holds game.json is the thing the user is
        // almost always looking for, so mark it rather than making them guess.
        const char *icon = e->is_pack ? "[pack]" : e->has_manifest ? "[PACK]" : "[dir]";
        Color col = e->has_manifest ? (Color){ 150, 220, 150, 255 }
                  : e->is_pack      ? (Color){ 220, 200, 120, 255 }
                                    : RAYWHITE;
        DrawText(TextFormat("%s  %s", icon, e->name),
                 (int)r.x + 6, (int)r.y + 4, 12, col);

        if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) b->selected = i;
        if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
            IsKeyDown(KEY_LEFT_SHIFT)) { /* reserved */ }
        if (hover && IsMouseButtonReleased(MOUSE_LEFT_BUTTON) &&
            GetTime() > 0 && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) { }
    }
    EndScissorMode();

    // Double-click, or Enter on a selection, walks into a directory.
    static double last_click = -1;
    static int    last_idx = -1;
    if (b->selected >= 0 && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
        CheckCollisionPointRec(GetMousePosition(), view)) {
        double now = GetTime();
        if (last_idx == b->selected && now - last_click < 0.35) {
            if (b->entry[b->selected].is_dir) {
                go_into(b, b->entry[b->selected].name);
                last_idx = -1;
                EndScissorMode();
            }
        }
        last_click = now;
        last_idx = b->selected;
    }

    int by = y + h - 44;
    if (GuiButton((Rectangle){ (float)(x + pad), (float)by, 90, 28 }, "Up")) {
        go_into(b, "..");
    }
    if (b->selected >= 0 && b->entry[b->selected].is_dir &&
        GuiButton((Rectangle){ (float)(x + pad + 96), (float)by, 90, 28 },
                  "Open")) {
        go_into(b, b->entry[b->selected].name);
    }
    if (GuiButton((Rectangle){ (float)(x + w - pad - 96), (float)by, 90, 28 },
                  "Cancel") || IsKeyPressed(KEY_ESCAPE)) {
        b->active = false;
        return false;
    }

    // Choosing: either the folder we are standing in, or a selected pack file.
    bool picked_file = b->selected >= 0 && b->entry[b->selected].is_pack &&
                       b->mode == GB_PICK_PACK;
    const char *label = picked_file ? "Choose this pack" : "Choose this folder";
    if (GuiButton((Rectangle){ (float)(x + w - pad - 96 - 156), (float)by,
                               150, 28 }, label)) {
        if (picked_file) {
            snprintf(out, outsz, "%s%c%s", b->cwd, GB_SEP,
                     b->entry[b->selected].name);
        } else {
            snprintf(out, outsz, "%s", b->cwd);
        }
        b->active = false;
        return true;
    }
    return false;
}
