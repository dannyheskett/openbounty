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
    if (!SavePathGetSlot(pid, 0, path, sizeof(path))) {
        toast_show(c->res->ui.toast_save_cancelled);
        return true;
    }
    SaveResult r = SaveGameWrite(path, c->game, c->map, c->fog);
    SavePathFlush();
    if (r == SAVE_OK) toast_show(c->res->ui.toast_save_ok);
    else toast_with_reason(c->res->ui.toast_save_failed, SaveResultText(r));
    return true;
}

bool menu_load(void *ud) {
    MenuCtx *c = (MenuCtx *)ud;
    char path[1024];
    const char *pid = c->res->pack_id[0] ? c->res->pack_id : NULL;
    if (!SavePathGetSlot(pid, 0, path, sizeof(path))) {
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
    // Modern: a real new game, through the title menu. main.c asks first.
    if (CL_IS_MODERN && c->new_game_flag) {
        *c->new_game_flag = true;
        return true;
    }
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

bool menu_key_available(int key, void *ud) {
    MenuCtx *c = (MenuCtx *)ud;
    if (!c || !c->game) return true;
    int mount = c->game->character.mount;
    if (key == KEY_F) return mount != MOUNT_FLY;
    if (key == KEY_L) return mount == MOUNT_FLY;
    if (key == KEY_N) return mount == MOUNT_SAIL;
    if (key == KEY_D) {                  // the key path needs a troop to dismiss
        for (int i = 0; i < GAME_ARMY_SLOTS; i++)
            if (c->game->army[i].id[0] && c->game->army[i].count > 0) return true;
        return false;
    }
    return true;
}
