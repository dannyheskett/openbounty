#include "scenario.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"

// ---- helpers ----------------------------------------------------------
static const char *jstr(cJSON *o, const char *k, const char *fallback) {
    cJSON *n = cJSON_GetObjectItem(o, k);
    return cJSON_IsString(n) ? n->valuestring : fallback;
}
static int jint(cJSON *o, const char *k, int fallback) {
    cJSON *n = cJSON_GetObjectItem(o, k);
    return cJSON_IsNumber(n) ? n->valueint : fallback;
}
static bool jbool(cJSON *o, const char *k, bool fallback) {
    cJSON *n = cJSON_GetObjectItem(o, k);
    return cJSON_IsBool(n) ? cJSON_IsTrue(n) : fallback;
}

static void copy_str(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = 0; return; }
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t r = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[r] = '\0';
    if (out_len) *out_len = r;
    return buf;
}

// ---- step parsing -----------------------------------------------------
typedef struct { const char *name; StepKind kind; } StepKindEntry;
static const StepKindEntry STEP_KINDS[] = {
    {"key", STEP_KEY},
    {"keys", STEP_KEYS},
    {"text", STEP_TEXT},
    {"frames", STEP_FRAMES},
    {"cheat", STEP_CHEAT},
    {"walk", STEP_WALK},
    {"walk_to", STEP_WALK_TO},
    {"bfs_to", STEP_BFS_TO},
    {"bfs_to_replan", STEP_BFS_TO_REPLAN},
    {"let_villain_castle_in", STEP_LET_VILLAIN_CASTLE_IN},
    {"let_castle", STEP_LET_CASTLE},
    {"let_town", STEP_LET_TOWN},
    {"let_dwelling_in", STEP_LET_DWELLING_IN},
    {"let_foe", STEP_LET_FOE},
    {"expect_prompt", STEP_EXPECT_PROMPT},
    {"expect_dialog", STEP_EXPECT_DIALOG},
    {"drain_dialogs", STEP_DRAIN_DIALOGS},
    {"resolve_combat", STEP_RESOLVE_COMBAT},
    {"auto_combat", STEP_AUTO_COMBAT},
    {"snapshot_baseline", STEP_SNAPSHOT_BASELINE},
    {"assert", STEP_ASSERT},
    {"assert_ge", STEP_ASSERT_GE},
    {"assert_array_len", STEP_ASSERT_ARRAY_LEN},
    {"assert_hero_at", STEP_ASSERT_HERO_AT},
    {"save", STEP_SAVE},
    {"load", STEP_LOAD},
    {"respawn", STEP_RESPAWN},
    {"wait_days", STEP_WAIT_DAYS},
    {"snap", STEP_SNAP},
    {"assert_snap_hash", STEP_ASSERT_SNAP_HASH},
    {"quit", STEP_QUIT},
    {"note", STEP_NOTE},
    {"open_view",        STEP_OPEN_VIEW},
    {"dismiss_view",     STEP_DISMISS_VIEW},
    {"assert_overworld", STEP_ASSERT_OVERWORLD},
    {NULL, 0},
};

static bool parse_kind(const char *name, StepKind *out) {
    for (const StepKindEntry *e = STEP_KINDS; e->name; e++) {
        if (strcmp(e->name, name) == 0) { *out = e->kind; return true; }
    }
    return false;
}

static void parse_value(cJSON *vnode, Value *out) {
    out->kind = VAL_NONE;
    if (!vnode) return;
    if (cJSON_IsBool(vnode))         { out->kind = VAL_BOOL; out->b = cJSON_IsTrue(vnode); }
    else if (cJSON_IsNumber(vnode))  { out->kind = VAL_INT;  out->i = (long long)vnode->valuedouble; }
    else if (cJSON_IsString(vnode))  { out->kind = VAL_STR;  copy_str(out->s, sizeof out->s, vnode->valuestring); }
}

static bool parse_step(cJSON *node, Step *out) {
    memset(out, 0, sizeof *out);
    const char *kname = jstr(node, "step", NULL);
    if (!kname) return false;
    if (!parse_kind(kname, &out->kind)) return false;

    // Pull commonly-used fields lazily — each kind only reads what it uses.
    copy_str(out->a, sizeof out->a, jstr(node, "name", ""));
    if (!out->a[0]) copy_str(out->a, sizeof out->a, jstr(node, "letter", ""));
    if (!out->a[0]) copy_str(out->a, sizeof out->a, jstr(node, "id", ""));
    if (!out->a[0]) copy_str(out->a, sizeof out->a, jstr(node, "ref", ""));
    if (!out->a[0]) copy_str(out->a, sizeof out->a, jstr(node, "value", ""));
    if (!out->a[0]) copy_str(out->a, sizeof out->a, jstr(node, "path", ""));

    copy_str(out->b, sizeof out->b, jstr(node, "as", ""));
    if (!out->b[0]) copy_str(out->b, sizeof out->b, jstr(node, "zone", ""));
    if (!out->b[0]) copy_str(out->b, sizeof out->b, jstr(node, "kind", ""));
    if (!out->b[0]) copy_str(out->b, sizeof out->b, jstr(node, "text", ""));

    copy_str(out->c, sizeof out->c, jstr(node, "on_foe", ""));
    if (!out->c[0]) copy_str(out->c, sizeof out->c, jstr(node, "body_contains", ""));
    if (!out->c[0]) copy_str(out->c, sizeof out->c, jstr(node, "title_eq", ""));
    if (!out->c[0]) copy_str(out->c, sizeof out->c, jstr(node, "field", ""));

    copy_str(out->d, sizeof out->d, jstr(node, "on_chest", ""));
    if (!out->d[0]) copy_str(out->d, sizeof out->d, jstr(node, "on_dialog", ""));

    copy_str(out->ref,  sizeof out->ref,  jstr(node, "ref_baseline", ""));
    copy_str(out->path, sizeof out->path, jstr(node, "path", ""));
    copy_str(out->op,   sizeof out->op,   jstr(node, "op", ""));

    out->i = jint(node, "x", 0);
    out->j = jint(node, "y", 0);
    out->k = jint(node, "n", 0);
    if (out->k == 0) out->k = jint(node, "max", 0);
    if (out->k == 0) out->k = jint(node, "skip", 0);
    if (out->k == 0) out->k = jint(node, "max_hops", 0);
    if (out->k == 0) out->k = jint(node, "delta", 0);
    parse_value(cJSON_GetObjectItem(node, "value"), &out->value);

    // Special: STEP_AUTO_COMBAT uses a bool "on" — coerce.
    if (out->kind == STEP_AUTO_COMBAT) {
        cJSON *on = cJSON_GetObjectItem(node, "on");
        out->value.kind = VAL_BOOL;
        out->value.b = cJSON_IsTrue(on);
    }
    if (out->kind == STEP_RESOLVE_COMBAT) {
        cJSON *py = cJSON_GetObjectItem(node, "press_y");
        out->value.kind = VAL_BOOL;
        out->value.b = py ? cJSON_IsTrue(py) : true;
    }
    return true;
}

// ---- top-level loader -------------------------------------------------
bool scenario_load(const char *path, Scenario *out) {
    memset(out, 0, sizeof *out);
    size_t n = 0;
    char *src = read_file(path, &n);
    if (!src) return false;
    cJSON *root = cJSON_ParseWithLength(src, n);
    free(src);
    if (!root) return false;

    copy_str(out->name, sizeof out->name, jstr(root, "name", "(unnamed)"));
    copy_str(out->description, sizeof out->description, jstr(root, "description", ""));
    cJSON *setup = cJSON_GetObjectItem(root, "setup");
    if (cJSON_IsObject(setup)) {
        cJSON *seedn = cJSON_GetObjectItem(setup, "seed");
        if (cJSON_IsNumber(seedn)) out->seed = (uint64_t)seedn->valuedouble;
        copy_str(out->class_id,  sizeof out->class_id,  jstr(setup, "class", "knight"));
        copy_str(out->char_name, sizeof out->char_name, jstr(setup, "name", "Bot"));
        copy_str(out->difficulty, sizeof out->difficulty, jstr(setup, "difficulty", "easy"));
        out->auto_combat = jbool(setup, "auto_combat", false);
        copy_str(out->load_save, sizeof out->load_save, jstr(setup, "load_save", ""));
    }
    cJSON *tags = cJSON_GetObjectItem(root, "tags");
    if (cJSON_IsArray(tags)) {
        cJSON *t;
        cJSON_ArrayForEach(t, tags) {
            if (out->tag_count >= 8) break;
            if (cJSON_IsString(t)) {
                copy_str(out->tags[out->tag_count], sizeof out->tags[0], t->valuestring);
                out->tag_count++;
            }
        }
    }
    out->max_seconds = jint(root, "max_seconds", 60);

    cJSON *steps = cJSON_GetObjectItem(root, "steps");
    if (cJSON_IsArray(steps)) {
        int n_steps = cJSON_GetArraySize(steps);
        out->steps = calloc((size_t)n_steps, sizeof(Step));
        out->step_count = 0;
        cJSON *si;
        cJSON_ArrayForEach(si, steps) {
            Step st;
            if (!parse_step(si, &st)) {
                pt_say("[%s] WARN: skipped step (unknown kind=%s)",
                       out->name, jstr(si, "step", "?"));
                continue;
            }
            out->steps[out->step_count++] = st;
        }
    }
    cJSON_Delete(root);
    return true;
}

void scenario_free(Scenario *s) {
    if (!s) return;
    free(s->steps);
    s->steps = NULL;
    s->step_count = 0;
}

const char *scenario_class_letter(const Scenario *s) {
    if (!s) return "A";
    if (strcmp(s->class_id, "knight") == 0)    return "A";
    if (strcmp(s->class_id, "paladin") == 0)   return "B";
    if (strcmp(s->class_id, "sorceress") == 0) return "C";
    if (strcmp(s->class_id, "barbarian") == 0) return "D";
    return "A";
}

// ---- bindings ---------------------------------------------------------
static Binding *find_binding(Scenario *s, const char *name) {
    for (int i = 0; i < s->binding_count; i++) {
        if (strcmp(s->bindings[i].name, name) == 0) return &s->bindings[i];
    }
    return NULL;
}

static Binding *push_binding(Scenario *s, const char *name) {
    Binding *b = find_binding(s, name);
    if (b) return b;
    if (s->binding_count >= SCENARIO_MAX_BINDINGS) return NULL;
    b = &s->bindings[s->binding_count++];
    memset(b, 0, sizeof *b);
    copy_str(b->name, sizeof b->name, name);
    return b;
}

// Scan filter against scenario file. Loads just `name` and `tags` to
// avoid running the full parser.
static bool path_basename_eq(const char *path, const char *name) {
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    size_t blen = dot ? (size_t)(dot - base) : strlen(base);
    return strncmp(base, name, blen) == 0 && name[blen] == '\0';
}

bool scenario_matches_filter(const char *file_path, const char *filter) {
    if (!filter || !filter[0]) return true;
    bool invert = false;
    if (filter[0] == '!') { invert = true; filter++; }
    Scenario s = { 0 };
    bool match = false;
    if (scenario_load(file_path, &s)) {
        if (path_basename_eq(file_path, s.name) && strstr(s.name, filter)) match = true;
        for (int i = 0; i < s.tag_count && !match; i++) {
            if (strstr(s.tags[i], filter)) match = true;
        }
        if (!match) {
            // Fall back to substring match against the filename.
            const char *slash = strrchr(file_path, '/');
            const char *base = slash ? slash + 1 : file_path;
            if (strstr(base, filter)) match = true;
        }
        scenario_free(&s);
    }
    return invert ? !match : match;
}

// ---- step interpreter -------------------------------------------------

static bool wait_for_no_block(int fd, int max_frames) {
    for (int i = 0; i < max_frames; i++) {
        if (!pt_ui_blocking(fd)) return true;
        pt_frames(fd, 5);
    }
    return false;
}

// bfs_to_replan: promotes the original playtest.c hop loop into a primitive.
static bool do_bfs_to_replan(int fd, int gx, int gy, int max_hops,
                             const char *on_foe, const char *on_chest,
                             const char *on_dialog, RunStats *rs) {
    int hx = -1, hy = -1;
    if (!pt_get_hero_xy(fd, &hx, &hy)) return false;
    WalkMap m;
    if (!pt_fetch_map(fd, &m)) return false;
    char path[PLAYSHOW_MAP_MAX * PLAYSHOW_MAP_MAX];
    int plen = pt_bfs_path(&m, hx, hy, gx, gy, path, sizeof path);
    if (plen < 0) {
        pt_say("bfs_to_replan: target (%d,%d) unreachable from (%d,%d)", gx, gy, hx, hy);
        return false;
    }
    int hops = 0;
    while (hops++ < max_hops) {
        WalkResult wr = pt_walk_path(fd, path);
        pt_frames(fd, 15);
        if (wr == WALK_FAILED) return false;
        if (wr == WALK_DONE) {
            int curx, cury; pt_get_hero_xy(fd, &curx, &cury);
            if (curx == gx && cury == gy) return true;
            // Some "walk done" lands adjacent (interactive bounce-back).
            // Treat as success if classify_prompt indicates we triggered.
            const char *cls = pt_classify_prompt(fd);
            if (cls[0] && strcmp(cls, "other") != 0) return true;
            return false;
        }
        const char *cls = pt_classify_prompt(fd);
        if (strcmp(cls, "foe") == 0) {
            if (strcmp(on_foe, "auto_combat") == 0) {
                pt_resolve_combat(fd, true);
                if (pt_read_int(fd, "stats.game_over", 0)) {
                    pt_say("bfs_to_replan: hero died in foe combat");
                    if (rs) rs->asserts_failed++;
                    return false;
                }
            } else if (strcmp(on_foe, "skip") == 0) {
                pt_key(fd, "N"); pt_frames(fd, 15);
                pt_drain_dialogs(fd, 4);
            } else {
                return false;
            }
        } else if (strcmp(cls, "chest") == 0) {
            if (strcmp(on_chest, "take_a") == 0) pt_key(fd, "A");
            else                                 pt_key(fd, "B");
            pt_frames(fd, 20);
            pt_drain_dialogs(fd, 4);
        } else if (strcmp(cls, "friendly") == 0) {
            pt_key(fd, "Y"); pt_frames(fd, 20);
            pt_drain_dialogs(fd, 4);
        } else if (strcmp(cls, "dialog") == 0) {
            if (strcmp(on_dialog, "drain") == 0) pt_drain_dialogs(fd, 4);
            else return false;
        } else if (strcmp(cls, "siege") == 0 || strcmp(cls, "audience") == 0 ||
                   strcmp(cls, "view") == 0 || strcmp(cls, "recruit") == 0) {
            return true;
        } else {
            char d[256]; pt_describe_prompt(fd, d, sizeof d);
            pt_say("bfs_to_replan: unhandled prompt: %s", d);
            return false;
        }
        if (!pt_get_hero_xy(fd, &hx, &hy)) return false;
        if (!pt_fetch_map(fd, &m))         return false;
        plen = pt_bfs_path(&m, hx, hy, gx, gy, path, sizeof path);
        if (plen < 0) {
            pt_say("bfs_to_replan: replan failed from (%d,%d)", hx, hy);
            return false;
        }
    }
    return false;
}

// Resolve a `ref` step argument to (x,y). Either a binding name or a literal.
static bool resolve_ref_xy(Scenario *s, const Step *st, int *out_x, int *out_y) {
    if (st->a[0]) {
        Binding *b = find_binding(s, st->a);
        if (b && b->kind == BIND_LOC) { *out_x = b->x; *out_y = b->y; return true; }
    }
    if (st->i || st->j) { *out_x = st->i; *out_y = st->j; return true; }
    return false;
}

static bool eval_assert(int fd, const Step *st, RunStats *rs) {
    const char *p = st->path[0] ? st->path : st->a;
    const char *op = st->op[0] ? st->op : "==";
    if (st->value.kind == VAL_INT) {
        if      (strcmp(op, "==") == 0) return pt_assert_int_eq(fd, p, (int)st->value.i, rs);
        else if (strcmp(op, ">=") == 0) return pt_assert_int_ge(fd, p, (int)st->value.i, rs);
        else if (strcmp(op, "<=") == 0) return pt_assert_int_le(fd, p, (int)st->value.i, rs);
        else if (strcmp(op, ">")  == 0) return pt_assert_int_ge(fd, p, (int)st->value.i + 1, rs);
        else if (strcmp(op, "<")  == 0) return pt_assert_int_le(fd, p, (int)st->value.i - 1, rs);
    } else if (st->value.kind == VAL_BOOL) {
        return pt_assert_bool_eq(fd, p, st->value.b, rs);
    } else if (st->value.kind == VAL_STR) {
        return pt_assert_str_eq(fd, p, st->value.s, rs);
    }
    rs_fail(rs, p, "?", "(no value)");
    return false;
}

static bool eval_assert_ge(Scenario *s, int fd, const Step *st, RunStats *rs) {
    const char *p = st->path[0] ? st->path : st->a;
    int floor = 0;
    if (st->ref[0]) {
        Binding *b = find_binding(s, st->ref);
        if (!b || b->kind != BIND_BASELINE) {
            rs_fail(rs, p, "ref_baseline missing", "(none)");
            return false;
        }
        const char *field = st->c[0] ? st->c : "score";
        if      (strcmp(field, "gold") == 0)        floor = b->gold;
        else if (strcmp(field, "score") == 0)       floor = b->score;
        else if (strcmp(field, "leadership") == 0)  floor = b->leadership;
        else if (strcmp(field, "killed") == 0)      floor = b->killed;
        else if (strcmp(field, "days_left") == 0)   floor = b->days_left;
        floor += st->k;   // optional delta
    } else {
        floor = (int)st->value.i;
    }
    return pt_assert_int_ge(fd, p, floor, rs);
}

bool scenario_run(Scenario *s, int fd, RunStats *rs) {
    if (!s || !s->steps) return true;
    bool ok = true;
    for (int i = 0; i < s->step_count; i++) {
        Step *st = &s->steps[i];
        switch (st->kind) {

        case STEP_KEY:
            pt_key(fd, st->a);
            break;

        case STEP_KEYS:
            // Comma- or space-separated names in `a`.
            for (char *tok = strtok(st->a, ", "); tok; tok = strtok(NULL, ", ")) {
                pt_key(fd, tok);
            }
            break;

        case STEP_TEXT: {
            char buf[160];
            snprintf(buf, sizeof buf, "text:%s", st->b[0] ? st->b : st->a);
            pt_cmd_ok(fd, buf);
            pt_frames(fd, 5);
            break;
        }

        case STEP_FRAMES:
            pt_frames(fd, st->k > 0 ? st->k : 1);
            break;

        case STEP_CHEAT: {
            // F10 → letter → ENTER (dismiss the Debug toast). Then drain
            // any chained dialog (W triggers a cartoon + VIEW_WIN; L
            // pushes VIEW_LOSE). Scenarios no longer need a trailing
            // drain_dialogs after a cheat.
            pt_key(fd, "F10");  pt_frames(fd, 10);
            pt_key(fd, st->a);  pt_frames(fd, 10);
            pt_key(fd, "ENTER"); pt_frames(fd, 15);
            pt_drain_dialogs(fd, 2);
            break;
        }

        case STEP_WALK:
            if (pt_walk_path(fd, st->a) != WALK_DONE) {
                pt_say("step %d (walk): interrupted or failed", i);
                ok = false;
            }
            break;

        case STEP_WALK_TO:
        case STEP_BFS_TO: {
            int gx = 0, gy = 0;
            if (!resolve_ref_xy(s, st, &gx, &gy)) { ok = false; break; }
            int hx, hy; pt_get_hero_xy(fd, &hx, &hy);
            WalkMap m; if (!pt_fetch_map(fd, &m)) { ok = false; break; }
            char path[PLAYSHOW_MAP_MAX * PLAYSHOW_MAP_MAX];
            int plen = pt_bfs_path(&m, hx, hy, gx, gy, path, sizeof path);
            if (plen < 0) { pt_say("step %d: unreachable", i); ok = false; break; }
            WalkResult wr = pt_walk_path(fd, path);
            if (wr != WALK_DONE && wr != WALK_INTERRUPTED) {
                pt_say("step %d: walk failed", i); ok = false;
            }
            break;
        }

        case STEP_BFS_TO_REPLAN: {
            int gx = 0, gy = 0;
            if (!resolve_ref_xy(s, st, &gx, &gy)) { ok = false; break; }
            const char *on_foe    = st->c[0] ? st->c : "auto_combat";
            const char *on_chest  = st->d[0] ? st->d : "take_a";
            const char *on_dialog = "drain";
            int max_hops = st->k > 0 ? st->k : 12;
            if (!do_bfs_to_replan(fd, gx, gy, max_hops, on_foe, on_chest, on_dialog, rs)) {
                pt_say("step %d (bfs_to_replan): did not reach target", i);
                ok = false;
            }
            break;
        }

        case STEP_LET_VILLAIN_CASTLE_IN: {
            // a=as, b=zone, k=skip
            const char *as = st->b;            // we stored "as" into b on parse fallback
            // Re-parse: a held "name"|"id" by accident — for this kind, we
            // use the JSON keys "as" and "zone" explicitly. Re-fall: if
            // st->b empty, st->a is the as.
            const char *zone = NULL, *bind_as = NULL;
            // We need the original keys. Quick fix: re-read both.
            // (parse_step put "as" into b first when the JSON has "as".)
            zone    = st->b[0] ? st->b : "continentia";
            bind_as = st->a[0] ? st->a : "V";
            (void)as;
            int gx = -1, gy = -1;
            char *cid = NULL;
            for (int skip = st->k; skip < st->k + 16; skip++) {
                free(cid);
                cid = pt_find_villain_castle_n(fd, zone, skip, &gx, &gy);
                if (!cid) break;
                WalkMap mm;
                if (!pt_fetch_map(fd, &mm)) break;
                int hx, hy; pt_get_hero_xy(fd, &hx, &hy);
                char tmp[PLAYSHOW_MAP_MAX * PLAYSHOW_MAP_MAX];
                int plen = pt_bfs_path(&mm, hx, hy, gx, gy, tmp, sizeof tmp);
                if (plen >= 0) break;
                free(cid); cid = NULL;
            }
            if (!cid) { pt_say("step %d: no villain castle reachable in %s", i, zone); ok = false; break; }
            Binding *b = push_binding(s, bind_as);
            if (b) { b->kind = BIND_LOC; b->x = gx; b->y = gy; copy_str(b->id, sizeof b->id, cid); }
            pt_say("bound %s -> castle '%s' at (%d,%d)", bind_as, cid, gx, gy);
            free(cid);
            break;
        }

        case STEP_LET_CASTLE: {
            const char *id = st->a;
            const char *bind_as = st->b[0] ? st->b : "C";
            int x, y;
            if (!pt_find_castle_by_id(fd, id, &x, &y)) {
                pt_say("step %d: castle '%s' not found", i, id);
                ok = false; break;
            }
            Binding *b = push_binding(s, bind_as);
            if (b) { b->kind = BIND_LOC; b->x = x; b->y = y; copy_str(b->id, sizeof b->id, id); }
            pt_say("bound %s -> castle '%s' at (%d,%d)", bind_as, id, x, y);
            break;
        }

        case STEP_LET_TOWN: {
            const char *id = st->a;
            const char *bind_as = st->b[0] ? st->b : "T";
            int x, y;
            if (!pt_find_town_by_id(fd, id, &x, &y)) {
                pt_say("step %d: town '%s' not found", i, id);
                ok = false; break;
            }
            Binding *b = push_binding(s, bind_as);
            if (b) { b->kind = BIND_LOC; b->x = x; b->y = y; copy_str(b->id, sizeof b->id, id); }
            pt_say("bound %s -> town '%s' at (%d,%d)", bind_as, id, x, y);
            break;
        }

        case STEP_LET_DWELLING_IN: {
            const char *zone = st->b[0] ? st->b : "continentia";
            const char *bind_as = st->a[0] ? st->a : "D";
            int x, y;
            char troop[32];
            if (!pt_find_dwelling_in(fd, zone, st->k, &x, &y, troop, sizeof troop)) {
                pt_say("step %d: no dwelling in %s (skip=%d)", i, zone, st->k);
                ok = false; break;
            }
            Binding *b = push_binding(s, bind_as);
            if (b) { b->kind = BIND_LOC; b->x = x; b->y = y; copy_str(b->id, sizeof b->id, troop); }
            pt_say("bound %s -> dwelling at (%d,%d) troop=%s", bind_as, x, y, troop);
            break;
        }

        case STEP_LET_FOE: {
            const char *pid = st->a;
            const char *bind_as = st->b[0] ? st->b : "F";
            int x, y; bool friendly;
            if (!pt_find_foe(fd, pid, &x, &y, &friendly)) {
                pt_say("step %d: foe '%s' not found", i, pid);
                ok = false; break;
            }
            Binding *b = push_binding(s, bind_as);
            if (b) { b->kind = BIND_LOC; b->x = x; b->y = y; copy_str(b->id, sizeof b->id, pid); }
            pt_say("bound %s -> foe '%s' (friendly=%d) at (%d,%d)",
                   bind_as, pid, friendly, x, y);
            break;
        }

        case STEP_EXPECT_PROMPT: {
            const char *want = st->b[0] ? st->b : st->a;
            const char *got = pt_classify_prompt(fd);
            if (strcmp(got, want) == 0) {
                rs_pass(rs, "prompt", got);
            } else {
                rs_fail(rs, "prompt", want, got);
                ok = false;
            }
            break;
        }

        case STEP_EXPECT_DIALOG: {
            cJSON *root = pt_fetch_state(fd);
            cJSON *d = cJSON_GetObjectItem(root, "dialog");
            const char *want_title = st->c;
            const char *want_body  = st->d;
            bool present = cJSON_IsObject(d);
            const char *got_title = present ? jstr(d, "title", "") : "(none)";
            const char *got_body  = present ? jstr(d, "body",  "") : "(none)";
            bool pass = present;
            if (pass && want_title[0]) pass = (strcmp(got_title, want_title) == 0);
            if (pass && want_body[0])  pass = (strstr(got_body,  want_body) != NULL);
            if (pass) rs_pass(rs, "dialog", got_title);
            else      { rs_fail(rs, "dialog",
                                want_title[0] ? want_title : "(any)",
                                got_title); ok = false; }
            cJSON_Delete(root);
            break;
        }

        case STEP_DRAIN_DIALOGS: {
            int max = st->k > 0 ? st->k : 4;
            int n = pt_drain_dialogs(fd, max);
            pt_say("drained %d dialog page(s)", n);
            break;
        }

        case STEP_RESOLVE_COMBAT: {
            int n = pt_resolve_combat(fd, st->value.b);
            pt_say("resolve_combat: dismissed %d page(s)", n);
            break;
        }

        case STEP_AUTO_COMBAT:
            pt_cmd_ok(fd, st->value.b ? "auto_combat:on" : "auto_combat:off");
            break;

        case STEP_SNAPSHOT_BASELINE: {
            const char *as = st->b[0] ? st->b : (st->a[0] ? st->a : "base");
            Binding *b = push_binding(s, as);
            if (b) {
                b->kind = BIND_BASELINE;
                b->gold       = pt_read_int(fd, "stats.gold", 0);
                b->score      = pt_read_int(fd, "stats.score", 0);
                b->leadership = pt_read_int(fd, "stats.leadership_current", 0);
                b->killed     = pt_read_int(fd, "stats.followers_killed", 0);
                b->days_left  = pt_read_int(fd, "stats.days_left", 0);
                pt_say("snapshot %s: gold=%d score=%d leadership=%d killed=%d days=%d",
                       as, b->gold, b->score, b->leadership, b->killed, b->days_left);
            }
            break;
        }

        case STEP_ASSERT:
            if (!eval_assert(fd, st, rs)) ok = false;
            break;

        case STEP_ASSERT_GE:
            if (!eval_assert_ge(s, fd, st, rs)) ok = false;
            break;

        case STEP_ASSERT_ARRAY_LEN: {
            const char *p = st->path[0] ? st->path : st->a;
            const char *op = st->op[0] ? st->op : "==";
            if (!pt_assert_array_len(fd, p, op, (int)st->value.i, rs)) ok = false;
            break;
        }

        case STEP_ASSERT_HERO_AT:
            if (!pt_assert_hero_at(fd, st->i, st->j, rs)) ok = false;
            break;

        case STEP_SAVE: {
            char buf[300];
            snprintf(buf, sizeof buf, "save:%s", st->a[0] ? st->a : "/tmp/playtest.save");
            if (!pt_cmd_ok(fd, buf)) { pt_say("step %d (save): harness err", i); ok = false; }
            break;
        }

        case STEP_LOAD: {
            char buf[300];
            snprintf(buf, sizeof buf, "load:%s", st->a[0] ? st->a : "/tmp/playtest.save");
            if (!pt_cmd_ok(fd, buf)) { pt_say("step %d (load): harness err", i); ok = false; }
            break;
        }

        case STEP_RESPAWN:
            // Handled by the runner, not the interpreter — skip here.
            pt_say("step %d (respawn): not implemented in interpreter", i);
            break;

        case STEP_WAIT_DAYS: {
            // Ping-pong walk to advance days. Probes 4 cardinal directions
            // and uses the first pair that successfully toggles the hero's
            // tile. A successful step decrements steps_left_today; the
            // engine rolls over to the next day at 0. Best-effort: if a
            // prompt opens (foe encounter, etc.) we drain and resume.
            int n = st->k > 0 ? st->k : 1;
            int steps_per_day = pt_read_int(fd, "stats.steps_left_today", 40);
            if (steps_per_day < 1) steps_per_day = 40;
            int total_steps = n * steps_per_day + 5;
            int sx, sy; pt_get_hero_xy(fd, &sx, &sy);
            const char *paths[4][2] = {
                {"DOWN","UP"}, {"UP","DOWN"}, {"LEFT","RIGHT"}, {"RIGHT","LEFT"},
            };
            int chosen = -1;
            for (int p = 0; p < 4 && chosen < 0; p++) {
                pt_key(fd, paths[p][0]); pt_frames(fd, 3);
                int nx, ny; pt_get_hero_xy(fd, &nx, &ny);
                if (nx != sx || ny != sy) {
                    pt_key(fd, paths[p][1]); pt_frames(fd, 3);
                    chosen = p;
                }
            }
            if (chosen < 0) { pt_say("wait_days: no walkable direction"); break; }
            for (int s2 = 0; s2 < total_steps; s2++) {
                pt_key(fd, paths[chosen][s2 & 1]);
                pt_frames(fd, 2);
                if (pt_ui_blocking(fd)) pt_drain_dialogs(fd, 8);
            }
            wait_for_no_block(fd, 30);
            break;
        }

        case STEP_SNAP: {
            char buf[300];
            snprintf(buf, sizeof buf, "snap:%s", st->a[0] ? st->a : "/tmp/playtest.png");
            pt_cmd_ok(fd, buf);
            break;
        }

        case STEP_ASSERT_SNAP_HASH:
            pt_say("step %d (assert_snap_hash): [skip] visual hash not implemented", i);
            break;

        case STEP_QUIT:
            pt_cmd_ok(fd, "quit");
            return ok;

        case STEP_NOTE:
            pt_say("note: %s", st->b[0] ? st->b : st->a);
            break;

        case STEP_OPEN_VIEW: {
            // key:LETTER → frames. Default frames=10.
            int n = st->k > 0 ? st->k : 10;
            const char *letter = st->a[0] ? st->a : (st->b[0] ? st->b : "");
            if (!letter[0]) { pt_say("step %d (open_view): missing letter", i); ok = false; break; }
            pt_key(fd, letter);
            pt_frames(fd, n);
            break;
        }

        case STEP_DISMISS_VIEW: {
            // ESC → frames → drain_dialogs. Defaults: frames=15, max=4.
            int n = st->k > 0 ? st->k : 15;
            pt_key(fd, "ESCAPE");
            pt_frames(fd, n);
            pt_drain_dialogs(fd, 4);
            break;
        }

        case STEP_ASSERT_OVERWORLD: {
            if (!pt_assert_str_eq (fd, "mode",            "adventure", rs)) ok = false;
            if (!pt_assert_int_eq (fd, "view",            0,           rs)) ok = false;
            if (!pt_assert_bool_eq(fd, "stats.game_over", false,       rs)) ok = false;
            break;
        }
        }
    }
    return ok;
}
