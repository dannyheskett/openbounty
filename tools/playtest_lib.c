#include "playtest_lib.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// ---- pacing ----------------------------------------------------------
static int s_pause_ms = 80;
void pt_set_pause_ms(int ms) { s_pause_ms = ms; }
int  pt_pause_ms(void)       { return s_pause_ms; }

void pt_zsleep(int ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000 };
    nanosleep(&ts, NULL);
}

void pt_say(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[playtest] ");
    vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap);
}

// ---- run stats -------------------------------------------------------
void rs_pass(RunStats *rs, const char *path, const char *got) {
    if (rs) rs->asserts_passed++;
    fprintf(stderr, "[playtest] ASSERT OK: %s = %s\n", path, got);
}
void rs_fail(RunStats *rs, const char *path,
             const char *expected, const char *got) {
    if (rs) rs->asserts_failed++;
    fprintf(stderr, "[playtest] ASSERT FAIL: %s — expected %s, got %s\n",
            path, expected, got);
}

// ---- transport -------------------------------------------------------
int pt_connect_socket(const char *path, int retries_ms) {
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path);
    for (int i = 0; i < retries_ms / 50; i++) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0) {
            return fd;
        }
        close(fd);
        pt_zsleep(50);
    }
    return -1;
}

int pt_send_cmd(int fd, const char *cmd,
                char *header, size_t cap_h,
                char *status, size_t cap_s) {
    char buf[256];
    int n = snprintf(buf, sizeof buf, "%s\n", cmd);
    if (n < 0 || (size_t)n >= sizeof buf) return -1;
    if (send(fd, buf, (size_t)n, 0) != (ssize_t)n) return -1;
    static char acc[131072];
    size_t off = 0;
    int newlines = 0;
    while (off + 1 < sizeof acc) {
        ssize_t r = recv(fd, acc + off, sizeof acc - 1 - off, 0);
        if (r <= 0) return -1;
        for (ssize_t i = 0; i < r; i++) if (acc[off + i] == '\n') newlines++;
        off += (size_t)r;
        acc[off] = '\0';
        if (newlines >= 2) break;
    }
    char *nl1 = strchr(acc, '\n');
    if (!nl1) return -1;
    *nl1 = '\0';
    char *nl2 = strchr(nl1 + 1, '\n');
    if (nl2) *nl2 = '\0';
    if (header) {
        size_t hlen = strlen(acc);
        if (hlen >= cap_h) hlen = cap_h - 1;
        memcpy(header, acc, hlen); header[hlen] = '\0';
    }
    if (status) {
        const char *s = nl1 + 1;
        size_t slen = strlen(s);
        if (slen >= cap_s) slen = cap_s - 1;
        memcpy(status, s, slen); status[slen] = '\0';
    }
    return 0;
}

bool pt_cmd_ok(int fd, const char *cmd) {
    char h[256];
    if (pt_send_cmd(fd, cmd, h, sizeof h, NULL, 0) != 0) return false;
    return strncmp(h, "OK", 2) == 0;
}

cJSON *pt_fetch_state(int fd) {
    static char status[131072];
    if (pt_send_cmd(fd, "state", NULL, 0, status, sizeof status) != 0) return NULL;
    return cJSON_Parse(status);
}

// ---- input convenience ----------------------------------------------
bool pt_key(int fd, const char *k) {
    char buf[64];
    snprintf(buf, sizeof buf, "key:%s", k);
    bool ok = pt_cmd_ok(fd, buf);
    if (s_pause_ms > 0) pt_zsleep(s_pause_ms);
    return ok;
}

bool pt_frames(int fd, int n) {
    char buf[32];
    snprintf(buf, sizeof buf, "frames:%d", n);
    bool ok = pt_cmd_ok(fd, buf);
    int wait_ms = n * 17;
    if (wait_ms < s_pause_ms) wait_ms = s_pause_ms;
    pt_zsleep(wait_ms);
    return ok;
}

// ---- walkability + BFS ----------------------------------------------
static bool parse_walk_map(const char *payload, WalkMap *out) {
    int w = 0, h = 0;
    const char *p = payload;
    if (sscanf(p, "%d %d", &w, &h) != 2) return false;
    if (w <= 0 || w > PLAYSHOW_MAP_MAX || h <= 0 || h > PLAYSHOW_MAP_MAX) return false;
    out->w = w; out->h = h;
    while (*p && *p != ' ') p++;
    if (*p) p++;
    while (*p && *p != ' ') p++;
    if (*p) p++;
    int nibbles = (w + 3) / 4;
    for (int y = 0; y < h; y++) {
        for (int nb = 0; nb < nibbles; nb++) {
            char c = *p++;
            int v;
            if (c >= '0' && c <= '9') v = c - '0';
            else if (c >= 'a' && c <= 'f') v = 10 + (c - 'a');
            else if (c >= 'A' && c <= 'F') v = 10 + (c - 'A');
            else return false;
            for (int b = 0; b < 4; b++) {
                int x = nb * 4 + b;
                if (x >= w) break;
                out->walk[y][x] = (v >> (3 - b)) & 1;
            }
        }
        if (*p == ' ') p++;
    }
    return true;
}

static const char *header_payload(const char *h) {
    if (strncmp(h, "OK ", 3) == 0)  return h + 3;
    if (strncmp(h, "ERR ", 4) == 0) return h + 4;
    return h;
}

bool pt_fetch_map(int fd, WalkMap *out) {
    char hdr[8192];
    if (pt_send_cmd(fd, "dump_map", hdr, sizeof hdr, NULL, 0) != 0) return false;
    if (strncmp(hdr, "OK ", 3) != 0) {
        pt_say("dump_map: %s", hdr);
        return false;
    }
    return parse_walk_map(header_payload(hdr), out);
}

int pt_bfs_path(const WalkMap *m, int sx, int sy, int tx, int ty,
                char *out, size_t cap) {
    if (tx < 0 || ty < 0 || tx >= m->w || ty >= m->h) return -1;
    if (!m->walk[ty][tx]) return -1;
    static int dist[PLAYSHOW_MAP_MAX][PLAYSHOW_MAP_MAX];
    static int prev[PLAYSHOW_MAP_MAX][PLAYSHOW_MAP_MAX];
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++) { dist[y][x] = -1; prev[y][x] = -1; }
    static int qx[PLAYSHOW_MAP_MAX*PLAYSHOW_MAP_MAX];
    static int qy[PLAYSHOW_MAP_MAX*PLAYSHOW_MAP_MAX];
    int qh = 0, qt = 0;
    qx[qt] = sx; qy[qt] = sy; qt++;
    dist[sy][sx] = 0;
    int dxs[4] = { 0, 0, 1, -1 };
    int dys[4] = { -1, 1, 0, 0 };
    char dch[4] = { 'N', 'S', 'E', 'W' };
    bool found = false;
    while (qh < qt) {
        int cx = qx[qh], cy = qy[qh]; qh++;
        if (cx == tx && cy == ty) { found = true; break; }
        for (int d = 0; d < 4; d++) {
            int nx = cx + dxs[d], ny = cy + dys[d];
            if (nx < 0 || ny < 0 || nx >= m->w || ny >= m->h) continue;
            if (!m->walk[ny][nx]) continue;
            if (dist[ny][nx] >= 0) continue;
            dist[ny][nx] = dist[cy][cx] + 1;
            prev[ny][nx] = d;
            qx[qt] = nx; qy[qt] = ny; qt++;
        }
    }
    if (!found) return -1;
    char tmp[PLAYSHOW_MAP_MAX*PLAYSHOW_MAP_MAX];
    int len = 0;
    int cx = tx, cy = ty;
    while (!(cx == sx && cy == sy)) {
        int d = prev[cy][cx];
        if (d < 0) return -1;
        tmp[len++] = dch[d];
        cx -= dxs[d];
        cy -= dys[d];
    }
    if ((size_t)len + 1 > cap) return -1;
    for (int i = 0; i < len; i++) out[i] = tmp[len - 1 - i];
    out[len] = '\0';
    return len;
}

WalkResult pt_walk_path(int fd, const char *path) {
    for (const char *p = path; *p; p++) {
        const char *k = NULL;
        switch (*p) {
            case 'N': k = "UP"; break;
            case 'S': k = "DOWN"; break;
            case 'E': k = "RIGHT"; break;
            case 'W': k = "LEFT"; break;
            default: continue;
        }
        if (!pt_key(fd, k))    return WALK_FAILED;
        if (!pt_frames(fd, 2)) return WALK_FAILED;
        if (pt_ui_blocking(fd)) return WALK_INTERRUPTED;
    }
    return WALK_DONE;
}

// ---- state lookups ---------------------------------------------------
bool pt_get_hero_xy(int fd, int *x, int *y) {
    cJSON *root = pt_fetch_state(fd);
    if (!root) return false;
    cJSON *p = cJSON_GetObjectItem(root, "position");
    bool ok = false;
    if (cJSON_IsObject(p)) {
        cJSON *jx = cJSON_GetObjectItem(p, "x");
        cJSON *jy = cJSON_GetObjectItem(p, "y");
        if (cJSON_IsNumber(jx) && cJSON_IsNumber(jy)) {
            *x = jx->valueint; *y = jy->valueint; ok = true;
        }
    }
    cJSON_Delete(root);
    return ok;
}

char *pt_find_villain_castle_n(int fd, const char *zone, int skip,
                               int *out_x, int *out_y) {
    cJSON *root = pt_fetch_state(fd);
    if (!root) return NULL;
    cJSON *castles = cJSON_GetObjectItem(root, "castles");
    char *result = NULL;
    int seen = 0;
    if (cJSON_IsArray(castles)) {
        cJSON *c;
        cJSON_ArrayForEach(c, castles) {
            cJSON *jowner = cJSON_GetObjectItem(c, "owner");
            cJSON *jzone  = cJSON_GetObjectItem(c, "zone");
            cJSON *jid    = cJSON_GetObjectItem(c, "id");
            cJSON *jx     = cJSON_GetObjectItem(c, "x");
            cJSON *jy     = cJSON_GetObjectItem(c, "y");
            if (!cJSON_IsString(jowner) || strcmp(jowner->valuestring, "villain") != 0) continue;
            if (!cJSON_IsString(jzone)  || strcmp(jzone->valuestring, zone) != 0) continue;
            if (!cJSON_IsString(jid))    continue;
            if (!cJSON_IsNumber(jx) || !cJSON_IsNumber(jy)) continue;
            if (seen++ < skip) continue;
            *out_x = jx->valueint;
            *out_y = jy->valueint;
            result = strdup(jid->valuestring);
            break;
        }
    }
    cJSON_Delete(root);
    return result;
}

bool pt_find_castle_by_id(int fd, const char *id, int *out_x, int *out_y) {
    cJSON *root = pt_fetch_state(fd);
    if (!root) return false;
    cJSON *castles = cJSON_GetObjectItem(root, "castles");
    bool ok = false;
    if (cJSON_IsArray(castles)) {
        cJSON *c;
        cJSON_ArrayForEach(c, castles) {
            cJSON *jid = cJSON_GetObjectItem(c, "id");
            if (!cJSON_IsString(jid) || strcmp(jid->valuestring, id) != 0) continue;
            cJSON *jx = cJSON_GetObjectItem(c, "x");
            cJSON *jy = cJSON_GetObjectItem(c, "y");
            if (cJSON_IsNumber(jx) && cJSON_IsNumber(jy)) {
                *out_x = jx->valueint; *out_y = jy->valueint; ok = true;
            }
            break;
        }
    }
    cJSON_Delete(root);
    return ok;
}

bool pt_find_town_by_id(int fd, const char *id, int *out_x, int *out_y) {
    cJSON *root = pt_fetch_state(fd);
    if (!root) return false;
    cJSON *towns = cJSON_GetObjectItem(root, "towns");
    bool ok = false;
    if (cJSON_IsArray(towns)) {
        cJSON *t;
        cJSON_ArrayForEach(t, towns) {
            cJSON *jid = cJSON_GetObjectItem(t, "id");
            if (!cJSON_IsString(jid) || strcmp(jid->valuestring, id) != 0) continue;
            cJSON *jx = cJSON_GetObjectItem(t, "x");
            cJSON *jy = cJSON_GetObjectItem(t, "y");
            if (cJSON_IsNumber(jx) && cJSON_IsNumber(jy)) {
                *out_x = jx->valueint; *out_y = jy->valueint; ok = true;
            }
            break;
        }
    }
    cJSON_Delete(root);
    return ok;
}

bool pt_find_dwelling_in(int fd, const char *zone, int skip,
                         int *out_x, int *out_y, char *out_troop, size_t cap) {
    cJSON *root = pt_fetch_state(fd);
    if (!root) return false;
    bool ok = false;
    // First try state.dwellings[] (only populated after first visit).
    cJSON *dw = cJSON_GetObjectItem(root, "dwellings");
    int seen = 0;
    if (cJSON_IsArray(dw)) {
        cJSON *d;
        cJSON_ArrayForEach(d, dw) {
            cJSON *jzone = cJSON_GetObjectItem(d, "zone");
            if (!cJSON_IsString(jzone) || strcmp(jzone->valuestring, zone) != 0) continue;
            if (seen++ < skip) continue;
            cJSON *jx = cJSON_GetObjectItem(d, "x");
            cJSON *jy = cJSON_GetObjectItem(d, "y");
            cJSON *jtroop = cJSON_GetObjectItem(d, "troop");
            if (cJSON_IsNumber(jx) && cJSON_IsNumber(jy)) {
                *out_x = jx->valueint; *out_y = jy->valueint;
                if (out_troop && cap > 0) {
                    const char *s = cJSON_IsString(jtroop) ? jtroop->valuestring : "";
                    snprintf(out_troop, cap, "%s", s);
                }
                ok = true;
            }
            break;
        }
    }
    // Fallback: scan placements[] for dwelling-kind entries (set at salt
    // time, so present from game start before any visit).
    if (!ok) {
        cJSON *pl = cJSON_GetObjectItem(root, "placements");
        seen = 0;
        if (cJSON_IsArray(pl)) {
            cJSON *p;
            cJSON_ArrayForEach(p, pl) {
                cJSON *jzone = cJSON_GetObjectItem(p, "zone");
                cJSON *jkind = cJSON_GetObjectItem(p, "kind");
                if (!cJSON_IsString(jzone) || strcmp(jzone->valuestring, zone) != 0) continue;
                if (!cJSON_IsString(jkind) || strncmp(jkind->valuestring, "dwelling_", 9) != 0) continue;
                if (seen++ < skip) continue;
                cJSON *jx = cJSON_GetObjectItem(p, "x");
                cJSON *jy = cJSON_GetObjectItem(p, "y");
                cJSON *jid = cJSON_GetObjectItem(p, "id");
                if (cJSON_IsNumber(jx) && cJSON_IsNumber(jy)) {
                    *out_x = jx->valueint; *out_y = jy->valueint;
                    if (out_troop && cap > 0) {
                        const char *s = cJSON_IsString(jid) ? jid->valuestring : "";
                        snprintf(out_troop, cap, "%s", s);
                    }
                    ok = true;
                }
                break;
            }
        }
    }
    cJSON_Delete(root);
    return ok;
}

bool pt_find_foe(int fd, const char *placement_id,
                 int *out_x, int *out_y, bool *out_friendly) {
    cJSON *root = pt_fetch_state(fd);
    if (!root) return false;
    cJSON *foes = cJSON_GetObjectItem(root, "foes");
    bool ok = false;
    if (cJSON_IsArray(foes)) {
        cJSON *f;
        cJSON_ArrayForEach(f, foes) {
            cJSON *jid = cJSON_GetObjectItem(f, "placement_id");
            if (!cJSON_IsString(jid) || strcmp(jid->valuestring, placement_id) != 0) continue;
            cJSON *jx = cJSON_GetObjectItem(f, "x");
            cJSON *jy = cJSON_GetObjectItem(f, "y");
            cJSON *jfr = cJSON_GetObjectItem(f, "friendly");
            if (cJSON_IsNumber(jx) && cJSON_IsNumber(jy)) {
                *out_x = jx->valueint; *out_y = jy->valueint;
                if (out_friendly) *out_friendly = cJSON_IsTrue(jfr);
                ok = true;
            }
            break;
        }
    }
    cJSON_Delete(root);
    return ok;
}

// ---- prompt classification ------------------------------------------
bool pt_ui_blocking(int fd) {
    cJSON *root = pt_fetch_state(fd);
    if (!root) return false;
    bool blocked = (cJSON_GetObjectItem(root, "dialog") != NULL) ||
                   (cJSON_GetObjectItem(root, "prompt") != NULL);
    if (!blocked) {
        // Active view (town backdrop, character sheet, world map, ...) also
        // blocks adventure-mode input. view == 0 (VIEW_NONE) means the
        // overworld is interactive.
        cJSON *v = cJSON_GetObjectItem(root, "view");
        if (cJSON_IsNumber(v) && v->valueint != 0) blocked = true;
    }
    cJSON_Delete(root);
    return blocked;
}

int pt_drain_dialogs(int fd, int max_attempts) {
    int n = 0;
    for (int i = 0; i < max_attempts; i++) {
        if (!pt_ui_blocking(fd)) break;
        pt_key(fd, "ENTER");
        pt_frames(fd, 15);
        n++;
    }
    return n;
}

const char *pt_classify_prompt(int fd) {
    static char tag[16];
    tag[0] = '\0';
    cJSON *root = pt_fetch_state(fd);
    if (!root) return "";
    cJSON *p = cJSON_GetObjectItem(root, "prompt");
    cJSON *d = cJSON_GetObjectItem(root, "dialog");
    const char *result = "";
    if (cJSON_IsObject(p)) {
        cJSON *b = cJSON_GetObjectItem(p, "body");
        cJSON *t = cJSON_GetObjectItem(p, "title");
        const char *body  = cJSON_IsString(b) ? b->valuestring : "";
        const char *title = cJSON_IsString(t) ? t->valuestring : "";
        if      (strstr(title, "Foes") || strstr(body, "You encounter")) result = "foe";
        else if (strncmp(title, "Castle", 6) == 0 ||
                 strstr(body, "and army occupy") ||
                 strstr(body, "Garrison:"))                              result = "siege";
        else if (strstr(body, "audience"))                                result = "audience";
        else if (strstr(body, "gold") && strstr(body, "leadership"))      result = "chest";
        else if (strstr(body, "available") && strstr(body, "recruit"))    result = "recruit";
        else if (strstr(body, "wish to join you") ||
                 strstr(body, "want to join you"))                        result = "friendly";
        else                                                               result = "other";
    } else if (cJSON_IsObject(d)) {
        result = "dialog";
    } else {
        // No prompt or dialog — but a view stack entry can still block input
        // (town backdrop, world map, character sheet, …). Tag as "view" so
        // navigators can bail successfully on town/castle entry.
        cJSON *v = cJSON_GetObjectItem(root, "view");
        if (cJSON_IsNumber(v) && v->valueint != 0) result = "view";
    }
    snprintf(tag, sizeof tag, "%s", result);
    cJSON_Delete(root);
    return tag;
}

void pt_describe_prompt(int fd, char *out, size_t cap) {
    cJSON *root = pt_fetch_state(fd);
    out[0] = '\0';
    if (!root) return;
    cJSON *p = cJSON_GetObjectItem(root, "prompt");
    cJSON *d = cJSON_GetObjectItem(root, "dialog");
    if (cJSON_IsObject(p)) {
        cJSON *t = cJSON_GetObjectItem(p, "title");
        cJSON *b = cJSON_GetObjectItem(p, "body");
        snprintf(out, cap, "prompt[title=%s body=%.80s]",
                 cJSON_IsString(t) ? t->valuestring : "?",
                 cJSON_IsString(b) ? b->valuestring : "?");
    } else if (cJSON_IsObject(d)) {
        cJSON *t = cJSON_GetObjectItem(d, "title");
        cJSON *b = cJSON_GetObjectItem(d, "body");
        snprintf(out, cap, "dialog[title=%s body=%.80s]",
                 cJSON_IsString(t) ? t->valuestring : "?",
                 cJSON_IsString(b) ? b->valuestring : "?");
    } else {
        snprintf(out, cap, "(none)");
    }
    cJSON_Delete(root);
}

int pt_resolve_combat(int fd, bool press_y_first) {
    if (press_y_first) { pt_key(fd, "Y"); pt_frames(fd, 30); }
    int dismissed = 0;
    for (int waited = 0; waited < 90; waited++) {
        cJSON *root = pt_fetch_state(fd);
        if (!root) { pt_frames(fd, 30); continue; }
        cJSON *m = cJSON_GetObjectItem(root, "mode");
        cJSON *d = cJSON_GetObjectItem(root, "dialog");
        cJSON *p = cJSON_GetObjectItem(root, "prompt");
        bool in_combat = cJSON_IsString(m) && strcmp(m->valuestring, "combat") == 0;
        bool blocking  = (d != NULL) || (p != NULL);
        cJSON_Delete(root);
        if (blocking) { pt_key(fd, "ENTER"); pt_frames(fd, 15); dismissed++; continue; }
        if (!in_combat) break;
        pt_frames(fd, 30);
    }
    return dismissed;
}

int pt_villains_caught(int fd) {
    cJSON *root = pt_fetch_state(fd);
    if (!root) return -1;
    int n = -1;
    cJSON *contract = cJSON_GetObjectItem(root, "contract");
    if (cJSON_IsObject(contract)) {
        cJSON *arr = cJSON_GetObjectItem(contract, "villains_caught");
        if (cJSON_IsArray(arr)) n = cJSON_GetArraySize(arr);
    }
    cJSON_Delete(root);
    return n;
}

// ---- assertions ------------------------------------------------------
cJSON *pt_json_path(cJSON *root, const char *path) {
    if (!root || !path) return NULL;
    cJSON *node = root;
    char buf[128];
    size_t pi = 0;
    for (size_t i = 0;; i++) {
        char c = path[i];
        if (c == '.' || c == '\0') {
            buf[pi] = '\0';
            if (pi == 0) return NULL;
            bool numeric = true;
            for (size_t k = 0; k < pi; k++) {
                if (buf[k] < '0' || buf[k] > '9') { numeric = false; break; }
            }
            if (numeric && cJSON_IsArray(node)) {
                node = cJSON_GetArrayItem(node, atoi(buf));
            } else {
                node = cJSON_GetObjectItem(node, buf);
            }
            if (!node) return NULL;
            if (c == '\0') return node;
            pi = 0;
        } else {
            if (pi + 1 >= sizeof buf) return NULL;
            buf[pi++] = c;
        }
    }
}

int pt_read_int(int fd, const char *path, int fallback) {
    cJSON *root = pt_fetch_state(fd);
    cJSON *n = pt_json_path(root, path);
    int v = (cJSON_IsNumber(n)) ? n->valueint : fallback;
    cJSON_Delete(root);
    return v;
}

bool pt_read_str(int fd, const char *path, char *buf, size_t cap) {
    cJSON *root = pt_fetch_state(fd);
    cJSON *n = pt_json_path(root, path);
    bool ok = cJSON_IsString(n);
    if (ok && buf && cap > 0) snprintf(buf, cap, "%s", n->valuestring);
    cJSON_Delete(root);
    return ok;
}

bool pt_assert_int_eq(int fd, const char *path, int want, RunStats *rs) {
    cJSON *root = pt_fetch_state(fd);
    cJSON *n = pt_json_path(root, path);
    bool ok = cJSON_IsNumber(n) && n->valueint == want;
    char wantbuf[32], gotbuf[32];
    snprintf(wantbuf, sizeof wantbuf, "%d", want);
    if (cJSON_IsNumber(n)) snprintf(gotbuf, sizeof gotbuf, "%d", n->valueint);
    else                   snprintf(gotbuf, sizeof gotbuf, "(missing)");
    if (ok) rs_pass(rs, path, gotbuf); else rs_fail(rs, path, wantbuf, gotbuf);
    cJSON_Delete(root);
    return ok;
}

bool pt_assert_int_ge(int fd, const char *path, int floor, RunStats *rs) {
    cJSON *root = pt_fetch_state(fd);
    cJSON *n = pt_json_path(root, path);
    bool ok = cJSON_IsNumber(n) && n->valueint >= floor;
    char wantbuf[32], gotbuf[32];
    snprintf(wantbuf, sizeof wantbuf, ">=%d", floor);
    if (cJSON_IsNumber(n)) snprintf(gotbuf, sizeof gotbuf, "%d", n->valueint);
    else snprintf(gotbuf, sizeof gotbuf, "(missing)");
    if (ok) rs_pass(rs, path, gotbuf); else rs_fail(rs, path, wantbuf, gotbuf);
    cJSON_Delete(root);
    return ok;
}

bool pt_assert_int_le(int fd, const char *path, int ceil, RunStats *rs) {
    cJSON *root = pt_fetch_state(fd);
    cJSON *n = pt_json_path(root, path);
    bool ok = cJSON_IsNumber(n) && n->valueint <= ceil;
    char wantbuf[32], gotbuf[32];
    snprintf(wantbuf, sizeof wantbuf, "<=%d", ceil);
    if (cJSON_IsNumber(n)) snprintf(gotbuf, sizeof gotbuf, "%d", n->valueint);
    else snprintf(gotbuf, sizeof gotbuf, "(missing)");
    if (ok) rs_pass(rs, path, gotbuf); else rs_fail(rs, path, wantbuf, gotbuf);
    cJSON_Delete(root);
    return ok;
}

bool pt_assert_str_eq(int fd, const char *path, const char *want, RunStats *rs) {
    cJSON *root = pt_fetch_state(fd);
    cJSON *n = pt_json_path(root, path);
    bool ok = cJSON_IsString(n) && strcmp(n->valuestring, want) == 0;
    const char *got = cJSON_IsString(n) ? n->valuestring : "(missing)";
    if (ok) rs_pass(rs, path, got); else rs_fail(rs, path, want, got);
    cJSON_Delete(root);
    return ok;
}

bool pt_assert_bool_eq(int fd, const char *path, bool want, RunStats *rs) {
    cJSON *root = pt_fetch_state(fd);
    cJSON *n = pt_json_path(root, path);
    bool got = cJSON_IsTrue(n);
    bool present = cJSON_IsBool(n);
    bool ok = present && got == want;
    const char *gotstr = !present ? "(missing)" : (got ? "true" : "false");
    if (ok) rs_pass(rs, path, gotstr); else rs_fail(rs, path, want ? "true" : "false", gotstr);
    cJSON_Delete(root);
    return ok;
}

bool pt_assert_hero_at(int fd, int x, int y, RunStats *rs) {
    int hx, hy;
    bool got = pt_get_hero_xy(fd, &hx, &hy);
    char wantbuf[32], gotbuf[32];
    snprintf(wantbuf, sizeof wantbuf, "(%d,%d)", x, y);
    if (got) snprintf(gotbuf, sizeof gotbuf, "(%d,%d)", hx, hy);
    else     snprintf(gotbuf, sizeof gotbuf, "(missing)");
    bool ok = got && hx == x && hy == y;
    if (ok) rs_pass(rs, "position", gotbuf); else rs_fail(rs, "position", wantbuf, gotbuf);
    return ok;
}

bool pt_assert_array_len(int fd, const char *path,
                         const char *op, int value, RunStats *rs) {
    cJSON *root = pt_fetch_state(fd);
    cJSON *n = pt_json_path(root, path);
    int len = cJSON_IsArray(n) ? cJSON_GetArraySize(n) : -1;
    bool ok = false;
    if (len >= 0) {
        if      (strcmp(op, "==") == 0) ok = (len == value);
        else if (strcmp(op, ">=") == 0) ok = (len >= value);
        else if (strcmp(op, "<=") == 0) ok = (len <= value);
        else if (strcmp(op, ">")  == 0) ok = (len >  value);
        else if (strcmp(op, "<")  == 0) ok = (len <  value);
        else if (strcmp(op, "!=") == 0) ok = (len != value);
    }
    char want[64], got[32];
    snprintf(want, sizeof want, "len%s%d", op, value);
    if (len >= 0) snprintf(got, sizeof got, "len=%d", len);
    else          snprintf(got, sizeof got, "(missing)");
    if (ok) rs_pass(rs, path, got); else rs_fail(rs, path, want, got);
    cJSON_Delete(root);
    return ok;
}

// ---- lifecycle -------------------------------------------------------
int pt_spawn_game(const char *seed_arg, const char *load_path,
                  const char *sock_path, GameProcess *out) {
    (void)load_path;   // load goes through title-screen Load; not exposed via CLI
    unlink(sock_path);
    pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        if (seed_arg && seed_arg[0]) {
            execl("./build/openbounty", "openbounty",
                  "--harness", sock_path,
                  "--seed", seed_arg, (char *)NULL);
        } else {
            execl("./build/openbounty", "openbounty",
                  "--harness", sock_path, (char *)NULL);
        }
        perror("execl"); _exit(127);
    }
    int fd = pt_connect_socket(sock_path, 15000);
    if (fd < 0) { kill(child, SIGTERM); waitpid(child, NULL, 0); return -1; }
    out->child = child;
    out->fd = fd;
    snprintf(out->sock_path, sizeof out->sock_path, "%s", sock_path);
    return 0;
}

void pt_shutdown_game(GameProcess *gp) {
    if (!gp) return;
    if (gp->fd >= 0)   { pt_cmd_ok(gp->fd, "quit"); close(gp->fd); gp->fd = -1; }
    if (gp->child > 0) { waitpid(gp->child, NULL, 0); gp->child = -1; }
    if (gp->sock_path[0]) unlink(gp->sock_path);
}

static bool wait_for_mode(int fd, const char *want_mode, int timeout_frames) {
    char cur[32];
    for (int i = 0; i < timeout_frames; i++) {
        if (pt_read_str(fd, "mode", cur, sizeof cur) &&
            strcmp(cur, want_mode) == 0) return true;
        pt_frames(fd, 5);
    }
    return false;
}

bool pt_run_title_new_game(int fd, const char *class_letter,
                           const char *name, const char *difficulty) {
    // Splash 1 + 2 + credits.
    pt_key(fd, "ENTER"); pt_frames(fd, 30);
    pt_key(fd, "ENTER"); pt_frames(fd, 30);
    pt_key(fd, "ENTER"); pt_frames(fd, 30);
    // Class pick.
    pt_key(fd, class_letter ? class_letter : "A"); pt_frames(fd, 30);
    // Name entry.
    char tcmd[64];
    snprintf(tcmd, sizeof tcmd, "text:%s", name ? name : "Bot");
    pt_cmd_ok(fd, tcmd); pt_frames(fd, 5);
    pt_key(fd, "ENTER"); pt_frames(fd, 30);
    // Difficulty: default Normal (sel=1). easy=0 (UP×1), normal=1, hard=2 (DOWN×1), impossible=3 (DOWN×2).
    int sel = 1;
    if (difficulty && difficulty[0]) {
        if      (strcmp(difficulty, "easy") == 0)       sel = 0;
        else if (strcmp(difficulty, "normal") == 0)     sel = 1;
        else if (strcmp(difficulty, "hard") == 0)       sel = 2;
        else if (strcmp(difficulty, "impossible") == 0) sel = 3;
    }
    int delta = sel - 1;
    while (delta < 0) { pt_key(fd, "UP");   pt_frames(fd, 5); delta++; }
    while (delta > 0) { pt_key(fd, "DOWN"); pt_frames(fd, 5); delta--; }
    pt_key(fd, "ENTER"); pt_frames(fd, 60);
    // Drain post-create dialogs. Keep pressing ENTER until we are in
    // adventure mode AND no dialog/prompt is blocking. The "new game is
    // being created" modal is up the moment mode flips to adventure;
    // breaking the loop on mode alone leaves the dialog blocking input.
    for (int i = 0; i < 12; i++) {
        char m[32];
        bool in_adv = pt_read_str(fd, "mode", m, sizeof m) && strcmp(m, "adventure") == 0;
        if (in_adv && !pt_ui_blocking(fd)) break;
        pt_key(fd, "ENTER"); pt_frames(fd, 30);
    }
    return wait_for_mode(fd, "adventure", 60);
}
