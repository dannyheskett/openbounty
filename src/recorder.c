// _POSIX_C_SOURCE: clock_gettime + localtime_r + mkdtemp.
#define _POSIX_C_SOURCE 200809L

#include "recorder.h"
#include "state_serialize.h"
#include "cJSON.h"
#include "raylib.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir((p), 0755)
#endif

// ---- state ----------------------------------------------------------------

static bool         s_active     = false;
static char        *s_out_path   = NULL;  // final .mp4 output
static char        *s_tmp_dir    = NULL;  // intermediate frame dump
static uint64_t     s_seq        = 0;     // monotonic tick id
static struct timespec s_start_ts = { 0, 0 };

static Game        *s_attached_game = NULL;
static const Map   *s_attached_map  = NULL;
static const Fog   *s_attached_fog  = NULL;
static RenderTexture2D *s_target    = NULL;

// ---- helpers --------------------------------------------------------------

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

static bool ensure_dir(const char *dir) {
    if (!dir || !dir[0]) return false;
    struct stat st;
    if (stat(dir, &st) == 0) return S_ISDIR(st.st_mode);
    if (MKDIR(dir) == 0) return true;
    return errno == EEXIST;
}

// Build a per-process temp directory under the system tmp dir.
// Resulting string is heap-allocated; caller owns.
static char *make_temp_dir(void) {
    const char *base = getenv("TMPDIR");
    if (!base || !base[0]) base = "/tmp";
    char buf[512];
    int n = snprintf(buf, sizeof buf, "%s/openbounty-movie-%ld",
                     base, (long)getpid());
    if (n < 0 || (size_t)n >= sizeof buf) return NULL;
    if (!ensure_dir(buf)) return NULL;
    return xstrdup(buf);
}

// Recursively delete a directory. Best-effort; failures are logged
// only to the manifest's success/fail status, not propagated.
static void rmtree(const char *path) {
    if (!path) return;
    DIR *d = opendir(path);
    if (!d) { remove(path); return; }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        char child[1024];
        snprintf(child, sizeof child, "%s/%s", path, e->d_name);
        struct stat st;
        if (stat(child, &st) == 0 && S_ISDIR(st.st_mode)) rmtree(child);
        else remove(child);
    }
    closedir(d);
    rmdir(path);
}

static bool write_file(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    return w == len;
}

static void append_manifest(uint64_t seq, uint64_t ms,
                            const char *trigger,
                            const char *json_name,
                            const char *png_name) {
    char path[512];
    int n = snprintf(path, sizeof path, "%s/manifest.ndjson", s_tmp_dir);
    if (n < 0 || (size_t)n >= sizeof path) return;
    FILE *f = fopen(path, "ab");
    if (!f) return;
    cJSON *obj = cJSON_CreateObject();
    if (!obj) { fclose(f); return; }
    cJSON_AddNumberToObject(obj, "seq",     (double)seq);
    cJSON_AddNumberToObject(obj, "ms",      (double)ms);
    cJSON_AddStringToObject(obj, "trigger", trigger ? trigger : "");
    cJSON_AddStringToObject(obj, "json",    json_name);
    cJSON_AddStringToObject(obj, "png",     png_name ? png_name : "");
    char *line = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (line) {
        fputs(line, f);
        fputc('\n', f);
        free(line);
    }
    fclose(f);
}

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

// ---- public API -----------------------------------------------------------

void recorder_init(const char *out_mp4_path) {
    if (s_active) return;
    if (!out_mp4_path || !out_mp4_path[0]) return;
    s_tmp_dir = make_temp_dir();
    if (!s_tmp_dir) return;
    s_out_path = xstrdup(out_mp4_path);
    if (!s_out_path) {
        rmtree(s_tmp_dir);
        free(s_tmp_dir);
        s_tmp_dir = NULL;
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &s_start_ts);
    s_seq = 0;
    s_active = true;
}

bool recorder_shutdown(void) {
    // The shell renders the encode-progress dialog and calls into
    // mp4_encode_dir(s_tmp_dir, ...) before us. Our job here is to
    // clean up: free state, drop the temp dir. We return whether we
    // had captured at least one frame (the caller decides whether to
    // bother running the encoder).
    bool had_frames = (s_active && s_seq > 0);
    if (s_tmp_dir) {
        rmtree(s_tmp_dir);
        free(s_tmp_dir);
        s_tmp_dir = NULL;
    }
    free(s_out_path); s_out_path = NULL;
    s_active = false;
    s_seq = 0;
    return had_frames;
}

bool recorder_active(void) { return s_active; }

void recorder_attach_state(Game *g, const Map *map, const Fog *fog) {
    s_attached_game = g;
    s_attached_map  = map;
    s_attached_fog  = fog;
}

void recorder_attach_render_target(void *rt) {
    s_target = (RenderTexture2D *)rt;
}

void recorder_capture(const char *trigger) {
    if (!s_active) return;            // recording disabled
    if (!s_attached_game) return;     // pre-game; silently drop

    s_seq++;
    uint64_t seq = s_seq;
    uint64_t ms  = now_ms();

    cJSON *root = state_build_snapshot(s_attached_game, NULL,
                                       s_attached_map, s_attached_fog,
                                       trigger, "", seq, ms);
    if (!root) { s_seq--; return; }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) { s_seq--; return; }

    int png_len = 0;
    unsigned char *png = capture_frame_png(&png_len);

    char json_name[64], png_name[64];
    snprintf(json_name, sizeof json_name, "tick_%06llu.json",
             (unsigned long long)seq);
    snprintf(png_name,  sizeof png_name,  "tick_%06llu.png",
             (unsigned long long)seq);

    char path[1024];
    snprintf(path, sizeof path, "%s/%s", s_tmp_dir, json_name);
    write_file(path, json, strlen(json));
    free(json);

    if (png && png_len > 0) {
        snprintf(path, sizeof path, "%s/%s", s_tmp_dir, png_name);
        write_file(path, png, (size_t)png_len);
        MemFree(png);
        append_manifest(seq, ms, trigger, json_name, png_name);
    } else {
        append_manifest(seq, ms, trigger, json_name, "");
    }
}

const char *recorder_temp_dir(void)    { return s_tmp_dir; }
const char *recorder_output_path(void) { return s_out_path; }
