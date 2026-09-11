#include "tilevar.h"
#include "resources.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    char stem[RES_TILE_ART_LEN];
    int  count;
    char names[RES_TILE_VARIANTS][RES_TILE_ART_LEN];
} Entry;

static Entry    s_ent[RES_TILE_CODE_COUNT];
static int      s_n = 0;
static unsigned s_seed = 0;

void tilevar_init(const struct Resources *res, unsigned seed) {
    const Resources *r = (const Resources *)res;
    s_n = 0;
    s_seed = seed;
    if (!r) return;
    for (int i = 0; i < RES_TILE_CODE_COUNT; i++) {
        const ResTileCode *tc = &r->tile_codes[i];
        if (!tc->present || tc->variant_count <= 0 || !tc->art[0]) continue;
        // one entry per distinct stem: two codes with the same art share it
        int k;
        for (k = 0; k < s_n; k++) if (strcmp(s_ent[k].stem, tc->art) == 0) break;
        if (k < s_n) continue;
        Entry *e = &s_ent[s_n++];
        snprintf(e->stem, sizeof e->stem, "%s", tc->art);
        e->count = tc->variant_count;
        for (int v = 0; v < tc->variant_count; v++)
            snprintf(e->names[v], sizeof e->names[v], "%s", tc->variants[v]);
    }
}

int tilevar_pick(unsigned seed, int x, int y, int n) {
    if (n <= 1) return 0;
    // a small integer hash: the cell and the seed mixed so neighbours differ
    unsigned h = seed ^ 0x9E3779B9u;
    h ^= (unsigned)x * 0x85EBCA6Bu;
    h ^= (h >> 13);
    h ^= (unsigned)y * 0xC2B2AE35u;
    h ^= (h >> 16);
    h *= 0x27D4EB2Fu;
    h ^= (h >> 15);
    return (int)(h % (unsigned)n);
}

const char *tilevar_art(const char *art, int x, int y, char *out, size_t cap) {
    if (!art || s_n == 0 || !out || cap == 0) return art;
    const char *slash = strrchr(art, '/');
    const char *stem = slash ? slash + 1 : art;
    for (int k = 0; k < s_n; k++) {
        const Entry *e = &s_ent[k];
        if (strcmp(e->stem, stem) != 0) continue;
        int pick = tilevar_pick(s_seed, x, y, e->count + 1);   // 0 = the base art
        if (pick == 0) return art;
        if (slash)
            snprintf(out, cap, "%.*s/%s", (int)(slash - art), art, e->names[pick - 1]);
        else
            snprintf(out, cap, "%s", e->names[pick - 1]);
        return out;
    }
    return art;
}
