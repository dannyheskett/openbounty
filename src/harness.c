// OpenBounty control harness — see harness.h.
//
// Per-frame socket loop:
//   1. accept() a pending client (non-blocking).
//   2. recv() one line.
//   3. parse, dispatch, write reply, close.
//
// One client at a time. nc -U -q1 is the canonical client. The game
// loop owns the rendering; the harness only synthesizes input and
// reads game state.

#define OB_HARNESS_INPUT_INTERNAL_H  // we forward to raylib for snap
#include "harness.h"
#include "harness_input.h"
#include "game.h"
#include "combat.h"
#include "combat_loop.h"
#include "tables.h"
#include "map.h"
#include "fog.h"
#include "tile.h"
#include "resources.h"
#include "recorder.h"
#include "adventure.h"
#include "savegame.h"
#include "raylib.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
// Windows: Winsock2 + AF_UNIX support (Windows 10 1803+ has AF_UNIX).
// <afunix.h> provides struct sockaddr_un. ws2_32 must be linked.
//
// Defenses for raylib name collisions: raylib defines `Rectangle`,
// `CloseWindow`, `ShowCursor`, `DrawText` etc. that conflict with the
// Win32 GDI/USER API. WIN32_LEAN_AND_MEAN trims most of windows.h;
// NOGDI / NOUSER exclude the rest. ws2_32 doesn't need them.
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <winsock2.h>
#include <ws2tcpip.h>
// <afunix.h> ships with the Windows 10 SDK (MSVC) but mingw-w64 doesn't
// vendor it. The struct it defines is small and stable; declare it
// inline when the header is missing. Detection: __has_include falls
// back to a build-flag override (-DOPENBOUNTY_HAVE_AFUNIX_H=1) for
// toolchains that ship the header but lack __has_include.
#if defined(__has_include)
#  if __has_include(<afunix.h>)
#    include <afunix.h>
#    define OPENBOUNTY_HAVE_AFUNIX_H 1
#  endif
#endif
#ifndef OPENBOUNTY_HAVE_AFUNIX_H
#  ifndef UNIX_PATH_MAX
#    define UNIX_PATH_MAX 108
#  endif
typedef struct sockaddr_un {
    ADDRESS_FAMILY sun_family;
    char sun_path[UNIX_PATH_MAX];
} SOCKADDR_UN, *PSOCKADDR_UN;
#endif
#include <io.h>
typedef SOCKET socket_t;
#define SOCK_INVALID  INVALID_SOCKET
#define SOCK_CLOSE(s) closesocket(s)
#define SOCK_LASTERR  WSAGetLastError()
#define SOCK_WOULDBLOCK WSAEWOULDBLOCK
typedef int sock_ssize_t;   // recv/send return int on Win
#else
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
typedef int socket_t;
#define SOCK_INVALID  (-1)
#define SOCK_CLOSE(s) close(s)
#define SOCK_LASTERR  errno
#define SOCK_WOULDBLOCK EWOULDBLOCK
typedef ssize_t sock_ssize_t;
#endif

// Helper: set non-blocking. Hides Linux fcntl vs Windows ioctlsocket.
static int set_nonblocking(socket_t fd) {
#ifdef _WIN32
    u_long mode = 1;
    return (ioctlsocket(fd, FIONBIO, &mode) == 0) ? 0 : -1;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

// Helper: remove a socket-file path (used to clean up stale sockets).
static void remove_socket_path(const char *path) {
#ifdef _WIN32
    DeleteFileA(path);
#else
    unlink(path);
#endif
}

// ---- socket state ----------------------------------------------------------

static socket_t s_listen_fd = SOCK_INVALID;
// Persistent client connection. The harness keeps a single client open
// across frames so a long-running script doesn't pay accept/connect
// cost per command. New connect attempts while a client is already
// connected are accepted-then-closed (one driver at a time).
static socket_t s_client_fd = SOCK_INVALID;
// Read buffer for the persistent client. Commands are line-terminated;
// we accumulate bytes here and dispatch each complete line.
static char s_client_inbuf[2048];
static size_t s_client_inlen = 0;
static char s_socket_path[256] = { 0 };
static bool s_should_quit = false;

// Live state pointers, registered by main.c / RunCombat.
static Game             *s_game = NULL;
static const Combat     *s_combat = NULL;
// Map/Fog were const for read-only state queries; save:/load: deliberately
// mutate them, so they're plain pointers now. Read-side handlers still
// only read, so the relaxation is safe.
static Map              *s_map = NULL;
static Fog              *s_fog = NULL;
static RenderTexture2D  *s_target = NULL;

// Pending frame-skip count from a `frames:N` command. main.c's loop
// honors this by suppressing input and just advancing N frames.
static int s_pending_frames = 0;

// Forward decls for the input shim's setter (defined in harness_input.c).
extern void harness_set_active(bool active);

// ---- key name table --------------------------------------------------------

typedef struct { const char *name; int key; } KeyEntry;

static const KeyEntry KEY_TABLE[] = {
    // Letters
    {"a", KEY_A}, {"b", KEY_B}, {"c", KEY_C}, {"d", KEY_D}, {"e", KEY_E},
    {"f", KEY_F}, {"g", KEY_G}, {"h", KEY_H}, {"i", KEY_I}, {"j", KEY_J},
    {"k", KEY_K}, {"l", KEY_L}, {"m", KEY_M}, {"n", KEY_N}, {"o", KEY_O},
    {"p", KEY_P}, {"q", KEY_Q}, {"r", KEY_R}, {"s", KEY_S}, {"t", KEY_T},
    {"u", KEY_U}, {"v", KEY_V}, {"w", KEY_W}, {"x", KEY_X}, {"y", KEY_Y},
    {"z", KEY_Z},
    // Digits
    {"0", KEY_ZERO}, {"1", KEY_ONE}, {"2", KEY_TWO}, {"3", KEY_THREE},
    {"4", KEY_FOUR}, {"5", KEY_FIVE}, {"6", KEY_SIX}, {"7", KEY_SEVEN},
    {"8", KEY_EIGHT}, {"9", KEY_NINE},
    // Arrows + numpad
    {"up", KEY_UP}, {"down", KEY_DOWN}, {"left", KEY_LEFT}, {"right", KEY_RIGHT},
    {"kp_0", KEY_KP_0}, {"kp_1", KEY_KP_1}, {"kp_2", KEY_KP_2},
    {"kp_3", KEY_KP_3}, {"kp_4", KEY_KP_4}, {"kp_5", KEY_KP_5},
    {"kp_6", KEY_KP_6}, {"kp_7", KEY_KP_7}, {"kp_8", KEY_KP_8},
    {"kp_9", KEY_KP_9}, {"kp_enter", KEY_KP_ENTER},
    // Whitespace / control
    {"space", KEY_SPACE}, {"enter", KEY_ENTER}, {"esc", KEY_ESCAPE},
    {"escape", KEY_ESCAPE}, {"backspace", KEY_BACKSPACE},
    {"tab", KEY_TAB},
    // Modifiers
    {"lshift", KEY_LEFT_SHIFT}, {"rshift", KEY_RIGHT_SHIFT},
    {"lctrl", KEY_LEFT_CONTROL}, {"rctrl", KEY_RIGHT_CONTROL},
    {"ctrl", KEY_LEFT_CONTROL},  // alias
    {"lalt", KEY_LEFT_ALT}, {"ralt", KEY_RIGHT_ALT}, {"alt", KEY_LEFT_ALT},
    // Page nav
    {"home", KEY_HOME}, {"end", KEY_END},
    {"page_up", KEY_PAGE_UP}, {"page_down", KEY_PAGE_DOWN},
    {"pgup", KEY_PAGE_UP}, {"pgdn", KEY_PAGE_DOWN},
    // Function keys
    {"f1", KEY_F1}, {"f2", KEY_F2}, {"f3", KEY_F3}, {"f4", KEY_F4},
    {"f5", KEY_F5}, {"f6", KEY_F6}, {"f7", KEY_F7}, {"f8", KEY_F8},
    {"f9", KEY_F9}, {"f10", KEY_F10}, {"f11", KEY_F11}, {"f12", KEY_F12},
    {"grave", KEY_GRAVE},
    {NULL, 0}
};

static int key_lookup(const char *name) {
    if (!name || !*name) return 0;
    // Case-insensitive match.
    char lower[32];
    int i = 0;
    while (name[i] && i < 31) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        lower[i++] = c;
    }
    lower[i] = '\0';
    for (const KeyEntry *e = KEY_TABLE; e->name; e++) {
        if (strcmp(e->name, lower) == 0) return e->key;
    }
    return 0;
}

// ---- attach -----------------------------------------------------------------

void harness_attach_game(Game *g) { s_game = g; }
void harness_attach_combat(const Combat *c) { s_combat = c; }
void harness_attach_map(const Map *m) { s_map = (Map *)m; }
void harness_attach_fog(const Fog *f) { s_fog = (Fog *)f; }
void harness_attach_render_target(void *rt) {
    s_target = (RenderTexture2D *)rt;
}

// ---- helpers ----------------------------------------------------------------

// ---- state JSON ------------------------------------------------------------
//
// The harness no longer maintains its own serializer. The recorder
// captures snapshots on every game-state mutation; we read the most
// recent entry's JSON for `state` queries and the status line on
// every reply.

static unsigned long s_tick = 0;

static void serialize_state(char *buf, size_t cap) {
    const TickRecord *e = recorder_last();
    if (!e || !e->state_json) {
        snprintf(buf, cap,
                 "{\"mode\":\"startup\",\"tick\":%lu}", s_tick);
        return;
    }
    snprintf(buf, cap, "%s", e->state_json);
}


// ---- snap -------------------------------------------------------------------

static bool snap_to(const char *path) {
    if (!s_target) return false;
    Image img = LoadImageFromTexture(s_target->texture);
    ImageFlipVertical(&img);
    bool ok = ExportImage(img, path);
    UnloadImage(img);
    return ok;
}

// ---- command dispatch -------------------------------------------------------
//
// Each handler writes a reply line (without trailing newline; the caller
// adds one). Returns 0 on success, nonzero on error (caller emits ERR).

static int handle_key(const char *arg, char *reply, size_t cap) {
    int k = key_lookup(arg);
    if (k == 0) {
        snprintf(reply, cap, "unknown key '%s'", arg);
        return -1;
    }
    harness_queue_key_pressed(k);
    reply[0] = 0;
    return 0;
}

static int handle_hold(const char *arg, char *reply, size_t cap) {
    int k = key_lookup(arg);
    if (k == 0) { snprintf(reply, cap, "unknown key '%s'", arg); return -1; }
    harness_queue_key_down(k);
    reply[0] = 0;
    return 0;
}

static int handle_release(const char *arg, char *reply, size_t cap) {
    int k = key_lookup(arg);
    if (k == 0) { snprintf(reply, cap, "unknown key '%s'", arg); return -1; }
    harness_queue_key_release(k);
    reply[0] = 0;
    return 0;
}

static int handle_char(const char *arg, char *reply, size_t cap) {
    if (!arg || !*arg) { snprintf(reply, cap, "empty char"); return -1; }
    harness_queue_char((int)(unsigned char)arg[0]);
    reply[0] = 0;
    return 0;
}

static int handle_text(const char *arg, char *reply, size_t cap) {
    if (!arg) { snprintf(reply, cap, "empty text"); return -1; }
    for (const char *p = arg; *p; p++) {
        harness_queue_char((int)(unsigned char)*p);
    }
    reply[0] = 0;
    return 0;
}

static int handle_frames(const char *arg, char *reply, size_t cap) {
    (void)cap;
    int n = (arg && *arg) ? atoi(arg) : 1;
    if (n < 1) n = 1;
    if (n > 1000) n = 1000;
    s_pending_frames += n;
    reply[0] = 0;
    return 0;
}

static int handle_snap(const char *arg, char *reply, size_t cap) {
    if (!arg || !*arg) { snprintf(reply, cap, "missing path"); return -1; }
    if (!snap_to(arg)) { snprintf(reply, cap, "snap failed"); return -1; }
    // Tie the screenshot to the next capture so a recorder dump can
    // reconstruct what the screen looked like at that point.
    recorder_pending_snap(arg);
    snprintf(reply, cap, "%s", arg);
    return 0;
}

static int handle_dump(const char *arg, char *reply, size_t cap) {
    char dir[256];
    if (arg && arg[0]) {
        snprintf(dir, sizeof dir, "%s", arg);
    } else {
        if (!recorder_default_dump_dir(dir, sizeof dir)) {
            snprintf(reply, cap, "dump path overflow");
            return -1;
        }
    }
    int n = recorder_dump_ring(dir);
    if (n < 0) {
        snprintf(reply, cap, "dump failed: %s", dir);
        return -1;
    }
    snprintf(reply, cap, "%d %s", n, dir);
    return 0;
}

// Dump the current zone's walkability bitmap. Reply payload format:
//   <width> <height> <hex-rows-space-separated>
// Each row is ceil(width/4) hex chars; '1' bits mean "walkable on foot".
// Used by playtest to BFS a path on the client side. No fog gating —
// we expose the true map, since the bot is testing reachability not
// vision. Travel-mode aware: in boat, water tiles count walkable too.
static int handle_dump_map(const char *arg, char *reply, size_t cap) {
    (void)arg;
    if (!s_map || s_map->width <= 0 || s_map->height <= 0) {
        snprintf(reply, cap, "no map attached");
        return -1;
    }
    int w = s_map->width, h = s_map->height;
    int nibbles = (w + 3) / 4;
    // Worst case header + h rows of nibbles + h spaces + null
    size_t need = 32 + (size_t)h * (nibbles + 1) + 1;
    if (need > cap) {
        snprintf(reply, cap, "map too large for reply buffer (%zu > %zu)",
                 need, cap);
        return -1;
    }
    bool flying  = (s_game && s_game->character.mount == MOUNT_FLY);
    bool walking = (s_game && s_game->travel_mode == TRAVEL_WALK);
    int n = snprintf(reply, cap, "%d %d ", w, h);
    for (int y = 0; y < h; y++) {
        for (int nb = 0; nb < nibbles; nb++) {
            int v = 0;
            for (int b = 0; b < 4; b++) {
                int x = nb * 4 + b;
                if (x >= w) break;
                const Tile *t = MapGetTile(s_map, x, y);
                bool ok = false;
                if (t) {
                    if      (flying)  ok = adventure_walkable_in_flight(t);
                    else if (walking) ok = adventure_walkable_on_foot(t);
                    else              ok = adventure_walkable_in_boat(t);
                }
                if (ok) v |= (1 << (3 - b));
            }
            char c = (v < 10) ? (char)('0' + v) : (char)('a' + (v - 10));
            if ((size_t)n + 1 >= cap) return -1;
            reply[n++] = c;
        }
        if ((size_t)n + 1 < cap && y + 1 < h) reply[n++] = ' ';
    }
    reply[n] = '\0';
    return 0;
}

static int handle_auto_combat(const char *arg, char *reply, size_t cap) {
    if (!arg || !arg[0]) {
        snprintf(reply, cap, "missing on/off");
        return -1;
    }
    bool on = (strcmp(arg, "on") == 0 || strcmp(arg, "1") == 0 ||
               strcmp(arg, "true") == 0);
    bool off = (strcmp(arg, "off") == 0 || strcmp(arg, "0") == 0 ||
                strcmp(arg, "false") == 0);
    if (!on && !off) {
        snprintf(reply, cap, "expected on|off");
        return -1;
    }
    combat_set_auto_player(on);
    snprintf(reply, cap, "%s", on ? "on" : "off");
    return 0;
}

static int handle_state(const char *arg, char *reply, size_t cap) {
    (void)arg;
    (void)cap;
    // The status line is appended to every reply by harness_poll; the
    // `state` command exists so callers can fetch the status without
    // any side effects. The OK payload is empty.
    reply[0] = '\0';
    return 0;
}

static int handle_save(const char *arg, char *reply, size_t cap) {
    if (!arg || !*arg) { snprintf(reply, cap, "missing path"); return -1; }
    if (!s_game || !s_map || !s_fog) {
        snprintf(reply, cap, "no game attached"); return -1;
    }
    SaveResult r = SaveGameWrite(arg, s_game, s_map, s_fog);
    if (r != SAVE_OK) {
        snprintf(reply, cap, "save failed: %s", SaveResultText(r));
        return -1;
    }
    recorder_capture("harness:save");
    snprintf(reply, cap, "%s", arg);
    return 0;
}

static int handle_load(const char *arg, char *reply, size_t cap) {
    if (!arg || !*arg) { snprintf(reply, cap, "missing path"); return -1; }
    if (!s_game || !s_map || !s_fog) {
        snprintf(reply, cap, "no game attached"); return -1;
    }
    SaveResult r = SaveGameRead(arg, s_game, s_map, s_fog);
    if (r != SAVE_OK) {
        snprintf(reply, cap, "load failed: %s", SaveResultText(r));
        return -1;
    }
    if (s_game->res) {
        const char *zone = s_game->position.zone;
        if (zone[0]) {
            if (!MapLoadZoneWithPlacements(s_map, s_game->res, zone, s_game)) {
                snprintf(reply, cap, "load failed: zone %s", zone);
                return -1;
            }
            GameApplyTileMutations(s_game, s_map, zone);
        }
    }
    // Refresh recorder so the next `state` query reflects the loaded data.
    recorder_capture("harness:load");
    snprintf(reply, cap, "%s", arg);
    return 0;
}

static int handle_quit(const char *arg, char *reply, size_t cap) {
    (void)arg;
    (void)cap;
    s_should_quit = true;
    reply[0] = 0;
    return 0;
}

static void dispatch(const char *line, char *reply, size_t cap, bool *ok) {
    *ok = true;
    // Split "cmd:arg" — colon is the separator. Strip leading whitespace
    // and trailing CR/LF. Sized to the recv buffer in harness_poll so a
    // maximum-length input still fits.
    char buf[2048];
    snprintf(buf, sizeof buf, "%s", line);
    char *end = buf + strlen(buf);
    while (end > buf && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' '))
        *--end = '\0';
    char *cmd = buf;
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    char *arg = strchr(cmd, ':');
    if (arg) { *arg = '\0'; arg++; }

    int rc = 0;
    if      (strcmp(cmd, "key") == 0)     rc = handle_key(arg, reply, cap);
    else if (strcmp(cmd, "hold") == 0)    rc = handle_hold(arg, reply, cap);
    else if (strcmp(cmd, "release") == 0) rc = handle_release(arg, reply, cap);
    else if (strcmp(cmd, "char") == 0)    rc = handle_char(arg, reply, cap);
    else if (strcmp(cmd, "text") == 0)    rc = handle_text(arg, reply, cap);
    else if (strcmp(cmd, "frames") == 0)  rc = handle_frames(arg, reply, cap);
    else if (strcmp(cmd, "snap") == 0)    rc = handle_snap(arg, reply, cap);
    else if (strcmp(cmd, "dump") == 0)    rc = handle_dump(arg, reply, cap);
    else if (strcmp(cmd, "auto_combat") == 0) rc = handle_auto_combat(arg, reply, cap);
    else if (strcmp(cmd, "dump_map") == 0) rc = handle_dump_map(arg, reply, cap);
    else if (strcmp(cmd, "state") == 0)   rc = handle_state(arg, reply, cap);
    else if (strcmp(cmd, "save") == 0)    rc = handle_save(arg, reply, cap);
    else if (strcmp(cmd, "load") == 0)    rc = handle_load(arg, reply, cap);
    else if (strcmp(cmd, "quit") == 0)    rc = handle_quit(arg, reply, cap);
    else if (strcmp(cmd, "ping") == 0)    { snprintf(reply, cap, "pong"); rc = 0; }
    else { snprintf(reply, cap, "unknown command '%s'", cmd); rc = -1; }
    *ok = (rc == 0);
}

// ---- socket lifecycle ------------------------------------------------------

bool harness_init(const char *socket_path) {
    if (!socket_path || !*socket_path) return false;
    if (s_listen_fd != SOCK_INVALID) return true;

#ifdef _WIN32
    // Winsock2 must be initialized once per process. Idempotent in
    // practice — the loader caches the first WSAStartup call.
    static bool s_wsa_started = false;
    if (!s_wsa_started) {
        WSADATA wd;
        if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) {
            fprintf(stderr, "[harness] WSAStartup failed\n");
            return false;
        }
        s_wsa_started = true;
    }
#endif

    snprintf(s_socket_path, sizeof s_socket_path, "%s", socket_path);

    // Remove any stale socket from a prior crashed instance.
    remove_socket_path(s_socket_path);

    socket_t fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == SOCK_INVALID) return false;

    if (set_nonblocking(fd) != 0) {
        SOCK_CLOSE(fd);
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    // sun_path is bounded (typ. 108 bytes on Linux, 108 bytes on
    // Windows AF_UNIX). Reject paths that would overflow rather than
    // silently truncating to a different file.
    if (strlen(s_socket_path) >= sizeof addr.sun_path) {
        fprintf(stderr, "[harness] socket path too long (%zu >= %zu)\n",
                strlen(s_socket_path), sizeof addr.sun_path);
        SOCK_CLOSE(fd);
        return false;
    }
    memcpy(addr.sun_path, s_socket_path, strlen(s_socket_path));

    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        SOCK_CLOSE(fd);
        return false;
    }
    if (listen(fd, 4) < 0) {
        SOCK_CLOSE(fd);
        remove_socket_path(s_socket_path);
        return false;
    }

    s_listen_fd = fd;
    harness_set_active(true);
    fprintf(stderr, "[harness] listening on %s\n", s_socket_path);
    return true;
}

void harness_shutdown(void) {
    if (s_client_fd != SOCK_INVALID) {
        SOCK_CLOSE(s_client_fd);
        s_client_fd = SOCK_INVALID;
        s_client_inlen = 0;
    }
    if (s_listen_fd != SOCK_INVALID) {
        SOCK_CLOSE(s_listen_fd);
        s_listen_fd = SOCK_INVALID;
    }
    if (s_socket_path[0]) {
        remove_socket_path(s_socket_path);
        s_socket_path[0] = '\0';
    }
    harness_set_active(false);
}

bool harness_tick(void) {
    // Order matters: clear stale one-shot keys queued by last frame's
    // poll BEFORE polling again. That way keys queued by THIS poll
    // survive through the loop body's input checks, and only get
    // cleared next time around. Without this, frame_end after poll
    // would clear keys before the input dispatch ever saw them.
    harness_frame_end();
    s_tick++;
    return harness_poll();
}

// Send a reply pair (OK/ERR line + status JSON line) over s_client_fd.
// Closes the client on send failure.
static void send_reply(bool ok, const char *reply_payload) {
    if (s_client_fd == SOCK_INVALID) return;
    // Buffers sized for the worst-case state JSON. Empirically
    // ~17-20KB in adventure with explored fog.
    char status[65536];
    char outbuf[81920];
    status[0] = '\0';
    serialize_state(status, sizeof status);
    snprintf(outbuf, sizeof outbuf, "%s%s%s\n%s\n",
             ok ? "OK" : "ERR",
             (reply_payload && reply_payload[0]) ? " " : "",
             reply_payload ? reply_payload : "",
             status);
    sock_ssize_t want = (sock_ssize_t)strlen(outbuf);
    sock_ssize_t sent = 0;
    while (sent < want) {
        sock_ssize_t r = send(s_client_fd, outbuf + sent,
                              (int)(want - sent), 0);
        if (r <= 0) {
            SOCK_CLOSE(s_client_fd);
            s_client_fd = SOCK_INVALID;
            s_client_inlen = 0;
            return;
        }
        sent += r;
    }
}

// Drain pending complete lines from s_client_inbuf, dispatching each.
static void drain_pending_commands(void) {
    char reply[8192];
    while (s_client_fd != SOCK_INVALID && s_client_inlen > 0) {
        char *nl = (char *)memchr(s_client_inbuf, '\n', s_client_inlen);
        if (!nl) return;            // partial line — wait for more bytes
        *nl = '\0';
        // Strip optional trailing CR.
        if (nl > s_client_inbuf && nl[-1] == '\r') nl[-1] = '\0';

        bool ok = true;
        reply[0] = '\0';
        dispatch(s_client_inbuf, reply, sizeof reply, &ok);
        send_reply(ok, reply);

        // Slide remainder down.
        size_t consumed = (size_t)(nl - s_client_inbuf) + 1;
        size_t remain = s_client_inlen - consumed;
        if (remain > 0) {
            memmove(s_client_inbuf, s_client_inbuf + consumed, remain);
        }
        s_client_inlen = remain;
    }
}

bool harness_poll(void) {
    if (s_listen_fd == SOCK_INVALID) return false;

    // Honor any pending frame-skip from a `frames:N` command. Each call
    // consumes one frame; the game's main loop will tick the frame and
    // call us again. We still drain a partial-line read if any bytes
    // landed since last frame, so the next command after frames:N is
    // ready to dispatch the moment the count drains.
    if (s_pending_frames > 0) {
        s_pending_frames--;
        return s_should_quit;
    }

    // Lazy accept. Single persistent client; if one is already
    // connected we drain its socket. New connection attempts while a
    // client holds the slot are accepted+closed (one driver at a time).
    if (s_client_fd == SOCK_INVALID) {
        socket_t c = accept(s_listen_fd, NULL, NULL);
        if (c != SOCK_INVALID) {
            set_nonblocking(c);
            s_client_fd = c;
            s_client_inlen = 0;
        }
    } else {
        // Reject any second concurrent client so we don't deadlock
        // between drivers. accept() is non-blocking on the listen sock.
        socket_t extra = accept(s_listen_fd, NULL, NULL);
        if (extra != SOCK_INVALID) SOCK_CLOSE(extra);
    }

    if (s_client_fd == SOCK_INVALID) return s_should_quit;

    // Drain any bytes available without blocking.
    while (s_client_inlen < sizeof s_client_inbuf - 1) {
        sock_ssize_t n = recv(s_client_fd,
                              s_client_inbuf + s_client_inlen,
                              (int)(sizeof s_client_inbuf - 1 - s_client_inlen),
                              0);
        if (n > 0) {
            s_client_inlen += (size_t)n;
            continue;
        }
        if (n == 0) {
            // Orderly client close.
            SOCK_CLOSE(s_client_fd);
            s_client_fd = SOCK_INVALID;
            s_client_inlen = 0;
            return s_should_quit;
        }
        // n < 0 — non-blocking would-block is normal; everything else
        // is a hard error.
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) break;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
#endif
        SOCK_CLOSE(s_client_fd);
        s_client_fd = SOCK_INVALID;
        s_client_inlen = 0;
        return s_should_quit;
    }

    drain_pending_commands();
    return s_should_quit;
}
