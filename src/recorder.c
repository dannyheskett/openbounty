// _POSIX_C_SOURCE: clock_gettime + localtime_r need POSIX 199309 +
// 500 respectively. 200809L covers both.
#define _POSIX_C_SOURCE 200809L

#include "recorder.h"
#include "state_serialize.h"
#include "cJSON.h"
#include "raylib.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir((p), 0755)
#endif

#define DEFAULT_CAP_ENTRIES 16
#define DEFAULT_CAP_BYTES   (8u * 1024u * 1024u)

// Linked list keeps eviction logic simple (FIFO with byte cap on top
// of count cap). Oldest at s_head, newest at s_tail. The total entry
// count is small (default 16), so there's no point in micro-optimizing
// with a flat ring buffer.
typedef struct TickNode {
    TickRecord       rec;
    struct TickNode *next;
} TickNode;

static TickNode *s_head = NULL;
static TickNode *s_tail = NULL;
static int       s_count = 0;
static size_t    s_bytes = 0;

static int       s_cap_entries = 0;
static size_t    s_cap_bytes   = 0;

static uint64_t  s_total = 0;
static struct timespec s_start_ts = { 0, 0 };

static char     *s_pending_snap = NULL;
static char     *s_record_dir   = NULL;

// Attached pointers.
static Game        *s_attached_game   = NULL;
static const Map   *s_attached_map    = NULL;
static const Fog   *s_attached_fog    = NULL;
static const Combat *s_attached_combat = NULL;
static RenderTexture2D *s_target = NULL;

// ---------- helpers ---------------------------------------------------------

static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s, n + 1);
    return out;
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
    uint64_t base = (uint64_t)s_start_ts.tv_sec * 1000u +
                    (uint64_t)s_start_ts.tv_nsec / 1000000u;
    return now - base;
}

static size_t record_bytes(const TickRecord *r) {
    size_t n = 0;
    if (r->trigger)    n += strlen(r->trigger) + 1;
    if (r->snap_path)  n += strlen(r->snap_path) + 1;
    if (r->state_json) n += strlen(r->state_json) + 1;
    if (r->frame_png)  n += (size_t)r->frame_png_len;
    return n;
}

static void free_record(TickRecord *r) {
    if (!r) return;
    free(r->trigger);    r->trigger = NULL;
    free(r->snap_path);  r->snap_path = NULL;
    free(r->state_json); r->state_json = NULL;
    if (r->frame_png) {
        // ExportImageToMemory allocates with raylib's MemAlloc; free with
        // MemFree to match.
        MemFree(r->frame_png);
        r->frame_png = NULL;
    }
    r->frame_png_len = 0;
    r->seq = 0;
    r->ms = 0;
}

static void evict_oldest(void) {
    if (!s_head) return;
    TickNode *n = s_head;
    s_head = n->next;
    if (!s_head) s_tail = NULL;
    s_bytes -= record_bytes(&n->rec);
    s_count--;
    free_record(&n->rec);
    free(n);
}

static void enforce_caps(void) {
    while (s_count > s_cap_entries) evict_oldest();
    while (s_bytes > s_cap_bytes && s_count > 0) evict_oldest();
}

// ---------- disk dump helpers -----------------------------------------------

static bool write_file(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    return w == len;
}

static bool ensure_dir(const char *dir) {
    if (!dir || !dir[0]) return false;
    struct stat st;
    if (stat(dir, &st) == 0) return S_ISDIR(st.st_mode);
    if (MKDIR(dir) == 0) return true;
    return errno == EEXIST;
}

// Append one manifest entry. The manifest is fsynced lazily; we
// fopen("ab") + fclose per entry so a crashed session leaves a usable
// timeline. Volume is low (one append per logical tick).
static void append_manifest(const char *dir, const TickRecord *r,
                            const char *json_name, const char *png_name) {
    char path[512];
    int n = snprintf(path, sizeof path, "%s/manifest.ndjson", dir);
    if (n < 0 || (size_t)n >= sizeof path) return;
    FILE *f = fopen(path, "ab");
    if (!f) return;
    cJSON *obj = cJSON_CreateObject();
    if (!obj) { fclose(f); return; }
    cJSON_AddNumberToObject(obj, "seq",     (double)r->seq);
    cJSON_AddNumberToObject(obj, "ms",      (double)r->ms);
    cJSON_AddStringToObject(obj, "trigger", r->trigger ? r->trigger : "");
    cJSON_AddStringToObject(obj, "json",    json_name);
    cJSON_AddStringToObject(obj, "png",     png_name ? png_name : "");
    if (r->snap_path && r->snap_path[0])
        cJSON_AddStringToObject(obj, "snap", r->snap_path);
    char *line = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (line) {
        fputs(line, f);
        fputc('\n', f);
        free(line);
    }
    fclose(f);
}

static void write_to_disk(const char *dir, const TickRecord *r) {
    if (!ensure_dir(dir)) return;
    char json_name[64];
    char png_name[64];
    snprintf(json_name, sizeof json_name, "tick_%06llu.json",
             (unsigned long long)r->seq);
    snprintf(png_name,  sizeof png_name,  "tick_%06llu.png",
             (unsigned long long)r->seq);

    char path[512];
    if (r->state_json) {
        snprintf(path, sizeof path, "%s/%s", dir, json_name);
        write_file(path, r->state_json, strlen(r->state_json));
    }
    if (r->frame_png && r->frame_png_len > 0) {
        snprintf(path, sizeof path, "%s/%s", dir, png_name);
        write_file(path, r->frame_png, (size_t)r->frame_png_len);
    }
    append_manifest(dir, r, json_name, r->frame_png ? png_name : "");
}

// ---------- framebuffer capture --------------------------------------------

static unsigned char *capture_frame_png(int *out_len) {
    *out_len = 0;
    if (!s_target) return NULL;
    Image img = LoadImageFromTexture(s_target->texture);
    if (img.data == NULL) return NULL;
    ImageFlipVertical(&img);
    int len = 0;
    unsigned char *png = ExportImageToMemory(img, ".png", &len);
    UnloadImage(img);
    if (!png || len <= 0) {
        if (png) MemFree(png);
        return NULL;
    }
    *out_len = len;
    return png;
}

// ---------- public API ------------------------------------------------------

void recorder_init(int cap_entries, size_t cap_bytes) {
    if (s_cap_entries) return;   // already initialized
    s_cap_entries = (cap_entries > 0) ? cap_entries : DEFAULT_CAP_ENTRIES;
    s_cap_bytes   = (cap_bytes   > 0) ? cap_bytes   : DEFAULT_CAP_BYTES;
    clock_gettime(CLOCK_MONOTONIC, &s_start_ts);
}

void recorder_shutdown(void) {
    while (s_head) evict_oldest();
    free(s_pending_snap); s_pending_snap = NULL;
    free(s_record_dir);   s_record_dir = NULL;
    s_cap_entries = 0;
    s_cap_bytes = 0;
    s_total = 0;
}

bool recorder_active(void) { return s_cap_entries > 0; }

void recorder_attach_state(Game *g, const Map *map, const Fog *fog) {
    s_attached_game = g;
    s_attached_map  = map;
    s_attached_fog  = fog;
}

void recorder_attach_combat(const Combat *c) {
    s_attached_combat = c;
}

void recorder_attach_render_target(void *rt) {
    s_target = (RenderTexture2D *)rt;
}

void recorder_set_record_dir(const char *dir) {
    free(s_record_dir);
    s_record_dir = (dir && dir[0]) ? xstrdup(dir) : NULL;
    if (s_record_dir) ensure_dir(s_record_dir);
}

void recorder_pending_snap(const char *path) {
    free(s_pending_snap);
    s_pending_snap = (path && path[0]) ? xstrdup(path) : NULL;
}

void recorder_capture(const char *trigger) {
    if (!s_cap_entries) return;     // not initialized
    if (!s_attached_game) return;   // pre-game; silently drop

    s_total++;
    uint64_t seq = s_total;
    uint64_t ms  = now_ms();
    const char *snap = s_pending_snap ? s_pending_snap : "";

    cJSON *root = state_build_snapshot(s_attached_game,
                                       s_attached_combat,
                                       s_attached_map,
                                       s_attached_fog,
                                       trigger, snap, seq, ms);
    if (!root) { s_total--; return; }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) { s_total--; return; }

    int png_len = 0;
    unsigned char *png = capture_frame_png(&png_len);

    TickNode *node = (TickNode *)calloc(1, sizeof(TickNode));
    if (!node) {
        free(json);
        if (png) MemFree(png);
        s_total--;
        return;
    }
    node->rec.seq           = seq;
    node->rec.ms            = ms;
    node->rec.trigger       = xstrdup(trigger ? trigger : "");
    node->rec.snap_path     = xstrdup(snap);
    node->rec.state_json    = json;
    node->rec.frame_png     = png;
    node->rec.frame_png_len = png_len;

    if (s_tail) s_tail->next = node;
    else        s_head = node;
    s_tail = node;
    s_count++;
    s_bytes += record_bytes(&node->rec);

    enforce_caps();

    if (s_record_dir) write_to_disk(s_record_dir, &node->rec);

    if (s_pending_snap) { free(s_pending_snap); s_pending_snap = NULL; }
}

const TickRecord *recorder_last(void) {
    return s_tail ? &s_tail->rec : NULL;
}

int recorder_count(void) { return s_count; }
uint64_t recorder_total(void) { return s_total; }

int recorder_dump_ring(const char *dir) {
    if (!dir || !dir[0]) return -1;
    if (!ensure_dir(dir)) return -1;
    int n = 0;
    for (TickNode *node = s_head; node; node = node->next) {
        write_to_disk(dir, &node->rec);
        n++;
    }
    return n;
}

char *recorder_default_dump_dir(char *buf, size_t cap) {
    if (!buf || cap == 0) return NULL;
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    int n = snprintf(buf, cap,
                     "screenshots/record_%04d%02d%02d_%02d%02d%02d_%llu",
                     tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                     tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
                     (unsigned long long)s_total);
    if (n < 0 || (size_t)n >= cap) return NULL;
    return buf;
}
