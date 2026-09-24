// src/shell_menu.c

#include "shell_menu.h"
#include "layout.h"

#include "savegame.h"
#include "savepath.h"
#include "ui.h"

// Format `template` with %REASON% substituted, then fire a toast.
static void toast_with_reason(const char *template_, const char *reason) {
    char msg[128];
    ResTemplateVar vars[] = { { "REASON", reason ? reason : "" } };
    resources_format_template(msg, sizeof msg, template_, vars, 1);
    toast_show(msg);
}

bool menu_save(void *ud) {
    MenuCtx *c = (MenuCtx *)ud;
    char path[1024];
    const char *pid = c->res->pack_id[0] ? c->res->pack_id : NULL;
    if (!SavePathGetSlot(pid, c->slot, path, sizeof(path))) {
        toast_show(c->res->ui.toast_save_cancelled);
        return true;
    }
    SaveResult r = SaveGameWrite(path, c->game, c->map, c->fog);
    SavePathFlush();
    if (r == SAVE_OK) {
        // Modern says it in a message (src/main.c); legacy keeps the toast.
        if (!CL_IS_MODERN) toast_show(c->res->ui.toast_save_ok);
    }
    else toast_with_reason(c->res->ui.toast_save_failed, SaveResultText(r));
    return true;
}

bool menu_load(void *ud) {
    MenuCtx *c = (MenuCtx *)ud;
    char path[1024];
    const char *pid = c->res->pack_id[0] ? c->res->pack_id : NULL;
    if (!SavePathGetSlot(pid, c->slot, path, sizeof(path))) {
        toast_show(c->res->ui.toast_load_cancelled);
        return false;
    }
    SaveResult r = SaveGameRead(path, c->game, c->map, c->fog);
    if (r == SAVE_OK) toast_show(c->res->ui.toast_load_ok);
    else toast_with_reason(c->res->ui.toast_load_failed, SaveResultText(r));
    return r == SAVE_OK;
}

bool menu_new(void *ud) {
    MenuCtx *c = (MenuCtx *)ud;
    // Legacy's New Game. Modern's asks in the game menu's own page and goes
    // back to the title menu (src/main.c, GM_DO_NEW).
    c->game->position.x = c->spawn_x;
    c->game->position.y = c->spawn_y;
    c->game->travel_mode = TRAVEL_WALK;
    c->game->boat.has_boat = false;
    c->game->boat.x = -1;
    c->game->boat.y = -1;
    FogInit(c->fog);
    FogRevealFor(c->res, c->fog, c->map, c->game->position.x, c->game->position.y);
    toast_show(c->res->ui.toast_new_game);
    return true;
}

bool menu_quit(void *ud) {
    MenuCtx *c = (MenuCtx *)ud;
    *c->quit_flag = true;
    return true;
}
