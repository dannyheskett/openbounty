// The Glory of Rome's troops carry Rome's own ids (#228): none is a King's
// Bounty id, the pack's troop_aliases map each old id to the new one, and a
// save written before the rename loads with every troop under its new id.

#include "greatest.h"
#include "fog.h"
#include "game.h"
#include "map.h"
#include "pack.h"
#include "resources.h"
#include "savegame.h"
#include "tables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ROME_OLD_SAVE "build/test_rome_old_ids.sav"

// The pack's troop ids, copied out so the pack can be closed again.
static int pack_troop_ids(const char *pack_dir, char ids[][RES_ID_LEN], int cap) {
    Pack *p = pack_open(pack_dir);
    if (!p) return -1;
    pack_stack_push(p);
    Resources *r = calloc(1, sizeof *r);
    int n = -1;
    if (r && resources_load(r, "game.json")) {
        n = 0;
        for (int i = 0; i < r->troops_count && n < cap; i++)
            snprintf(ids[n++], RES_ID_LEN, "%s", r->troops[i].id);
    }
    if (r) resources_free(r);
    free(r);
    pack_stack_pop();
    return n;
}

TEST rome_troop_ids_are_romes_own(void) {
    static char kb[64][RES_ID_LEN], rome[64][RES_ID_LEN];
    int nkb = pack_troop_ids("assets/kings-bounty", kb, 64);
    int nrome = pack_troop_ids("assets/glory-of-rome", rome, 64);
    ASSERT(nkb > 0);
    ASSERT(nrome > 0);
    for (int i = 0; i < nrome; i++)
        for (int j = 0; j < nkb; j++)
            ASSERT_FALSEm(rome[i], strcmp(rome[i], kb[j]) == 0);
    PASS();
}

TEST rome_aliases_lead_to_rome_troops(void) {
    Pack *p = pack_open("assets/glory-of-rome");
    ASSERT(p);
    pack_stack_push(p);
    Resources *r = calloc(1, sizeof *r);
    bool ok = r && resources_load(r, "game.json");
    // Read into locals before any assert, so a failure never leaves the Rome
    // pack on the stack for later suites.
    int count = ok ? r->troop_alias_count : 0;
    const char *a = ok ? resources_troop_alias(r, "peasants") : NULL;
    const char *b = ok ? resources_troop_alias(r, "vampires") : NULL;
    const char *c = ok ? resources_troop_alias(r, "elephants") : NULL;
    char got_a[RES_ID_LEN] = "", got_b[RES_ID_LEN] = "", got_c[RES_ID_LEN] = "";
    if (a) snprintf(got_a, sizeof got_a, "%s", a);
    if (b) snprintf(got_b, sizeof got_b, "%s", b);
    if (c) snprintf(got_c, sizeof got_c, "%s", c);
    bool new_id_unaliased = ok && resources_troop_alias(r, "coloni") == NULL;
    bool unknown_unaliased = ok && resources_troop_alias(r, "no_such_troop") == NULL;
    if (r) resources_free(r);
    free(r);
    pack_stack_pop();

    ASSERT(ok);
    ASSERT_EQ(26, count);
    ASSERT_STR_EQ("coloni", got_a);
    ASSERT_STR_EQ("striges", got_b);
    ASSERT_STR_EQ("elephanti", got_c);
    ASSERT(new_id_unaliased);
    ASSERT(unknown_unaliased);
    PASS();
}

// Replace every `from` in buf with `to` (both quoted JSON strings), into out.
static void replace_all(const char *buf, const char *from, const char *to,
                        char *out, size_t cap) {
    size_t fl = strlen(from), tl = strlen(to), n = 0;
    for (const char *s = buf; *s && n + 1 < cap;) {
        if (strncmp(s, from, fl) == 0 && n + tl + 1 < cap) {
            memcpy(out + n, to, tl); n += tl; s += fl;
        } else {
            out[n++] = *s++;
        }
    }
    out[n] = '\0';
}

static char *slurp(const char *path, size_t *len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (buf && fread(buf, 1, (size_t)n, fp) != (size_t)n) { free(buf); buf = NULL; }
    if (buf) buf[n] = '\0';
    fclose(fp);
    if (len) *len = buf ? (size_t)n : 0;
    return buf;
}

TEST rome_old_save_loads_under_new_ids(void) {
    Pack *p = pack_open("assets/glory-of-rome");
    ASSERT(p);
    pack_stack_push(p);
    Resources *res = calloc(1, sizeof *res);
    Game *g1 = calloc(1, sizeof *g1), *g2 = calloc(1, sizeof *g2);
    Map *m1 = calloc(1, sizeof *m1), *m2 = calloc(1, sizeof *m2);
    Fog *f1 = calloc(1, sizeof *f1), *f2 = calloc(1, sizeof *f2);
    bool ok = res && g1 && g2 && m1 && m2 && f1 && f2 &&
              resources_load(res, "game.json");
    bool wrote = false, read = false, has_foe = false;
    char army0[RES_ID_LEN] = "", army1[RES_ID_LEN] = "", gar[RES_ID_LEN] = "";
    char foe0[RES_ID_LEN] = "";
    if (ok) {
        g1->res = res;
        GameInit(g1, "Test", 0, 1, NULL);
        FogInit(f1);
        ok = MapLoadZoneWithPlacements(m1, res, res->world.starting_zone, g1);
    }
    if (ok) {
        // A Rome army, garrison and foe, written under today's ids...
        snprintf(g1->army[0].id, sizeof g1->army[0].id, "coloni");
        g1->army[0].count = 30;
        snprintf(g1->army[1].id, sizeof g1->army[1].id, "striges");
        g1->army[1].count = 4;
        if (g1->castle_count > 0) {
            snprintf(g1->castles[0].garrison[0].id,
                     sizeof g1->castles[0].garrison[0].id, "praetoriani");
            g1->castles[0].garrison[0].count = 9;
        }
        has_foe = g1->foe_count > 0;
        if (has_foe) {
            snprintf(g1->foes[0].garrison[0].id,
                     sizeof g1->foes[0].garrison[0].id, "elephanti");
            g1->foes[0].garrison[0].count = 2;
        }
        wrote = SaveGameWrite(ROME_OLD_SAVE, g1, m1, f1) == SAVE_OK;
    }
    if (wrote) {
        // ...then turned back into a save from before the rename.
        size_t len = 0;
        char *buf = slurp(ROME_OLD_SAVE, &len);
        size_t cap = len * 2 + 64;
        char *t1 = buf ? malloc(cap) : NULL, *t2 = t1 ? malloc(cap) : NULL;
        if (t1 && t2) {
            replace_all(buf, "\"coloni\"", "\"peasants\"", t1, cap);
            replace_all(t1, "\"striges\"", "\"vampires\"", t2, cap);
            replace_all(t2, "\"praetoriani\"", "\"knights\"", t1, cap);
            replace_all(t1, "\"elephanti\"", "\"elephants\"", t2, cap);
            FILE *fp = fopen(ROME_OLD_SAVE, "wb");
            if (fp) { fputs(t2, fp); fclose(fp); }
            g2->res = res;
            GameInit(g2, "Default", 0, 1, NULL);
            FogInit(f2);
            read = SaveGameRead(ROME_OLD_SAVE, g2, m2, f2) == SAVE_OK;
        }
        free(t1); free(t2); free(buf);
    }
    if (read) {
        snprintf(army0, sizeof army0, "%s", g2->army[0].id);
        snprintf(army1, sizeof army1, "%s", g2->army[1].id);
        if (g2->castle_count > 0)
            snprintf(gar, sizeof gar, "%s", g2->castles[0].garrison[0].id);
        if (has_foe && g2->foe_count > 0)
            snprintf(foe0, sizeof foe0, "%s", g2->foes[0].garrison[0].id);
    }
    unlink(ROME_OLD_SAVE);
    if (res) resources_free(res);
    free(res);
    GameFree(g1); free(g1); GameFree(g2); free(g2);
    MapFree(m1); free(m1); MapFree(m2); free(m2);
    FogFree(f1); free(f1); FogFree(f2); free(f2);
    pack_stack_pop();

    ASSERT(ok);
    ASSERT(wrote);
    ASSERT(read);
    ASSERT_STR_EQ("coloni", army0);
    ASSERT_STR_EQ("striges", army1);
    ASSERT_STR_EQ("praetoriani", gar);
    ASSERT(has_foe);
    ASSERT_STR_EQ("elephanti", foe0);
    PASS();
}

// The alcove's troop comes from the pack (sprites.ui.alcove_troop), not a
// hard-coded id: King's Bounty names its gnomes, and the Glory of Rome, which
// draws its own augur there, names none.
static bool alcove_troop_of(const char *pack_dir, char *out, size_t cap,
                            bool *real, bool *has_figure) {
    Pack *p = pack_open(pack_dir);
    if (!p) return false;
    pack_stack_push(p);
    Resources *r = calloc(1, sizeof *r);
    bool ok = r && resources_load(r, "game.json");
    if (ok) {
        snprintf(out, cap, "%s", r->sprites.alcove_troop);
        *real = troop_by_id(r->sprites.alcove_troop) != NULL;
        *has_figure = r->sprites.alcove_figure[0] != '\0';
    }
    if (r) resources_free(r);
    free(r);
    pack_stack_pop();
    return ok;
}

TEST alcove_troop_comes_from_the_pack(void) {
    char kb[RES_ID_LEN] = "", rome[RES_ID_LEN] = "";
    bool kb_real = false, kb_fig = true, rome_real = true, rome_fig = false;
    ASSERT(alcove_troop_of("assets/kings-bounty", kb, sizeof kb, &kb_real, &kb_fig));
    ASSERT(alcove_troop_of("assets/glory-of-rome", rome, sizeof rome, &rome_real, &rome_fig));
    ASSERT_STR_EQ("gnomes", kb);
    ASSERT(kb_real);
    ASSERT_FALSE(kb_fig);
    ASSERT_STR_EQ("", rome);
    ASSERT_FALSE(rome_real);
    ASSERT(rome_fig);
    PASS();
}

SUITE(e2e_rome_troop_ids_suite) {
    RUN_TEST(rome_troop_ids_are_romes_own);
    RUN_TEST(rome_aliases_lead_to_rome_troops);
    RUN_TEST(rome_old_save_loads_under_new_ids);
    RUN_TEST(alcove_troop_comes_from_the_pack);
}
