// Reading and writing a zone's .dat, through the pack's own tile_codes table.
//
// The editor never hard-codes a tile byte. Every byte in a .dat is resolved
// through res->tile_codes[], and every byte written back is looked up the same
// way, so a pack that renames or renumbers its codes keeps working and the
// editor cannot drift from what the engine loads.

#include "mapedit.h"

#include "assets_bytes.h"
#include "pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TERRAIN_STEM[TERRAIN_COUNT] = {
    "grass", "forest", "mountain", "water", "desert"
};

const char *mapedit_art_for(Terrain t, int variant) {
    static char buf[RES_TILE_ART_LEN];
    if (t < 0 || t >= TERRAIN_COUNT) return "grass";
    if (variant < 0) {
        snprintf(buf, sizeof buf, "%s", TERRAIN_STEM[t]);
    } else {
        snprintf(buf, sizeof buf, "%s_edge_%02d", TERRAIN_STEM[t], variant);
    }
    return buf;
}

char mapedit_code_for(const Resources *res, Terrain t, int variant) {
    const char *want = mapedit_art_for(t, variant);
    for (int i = 0; i < RES_TILE_CODE_COUNT; i++) {
        if (!res->tile_codes[i].present) continue;
        if (strcmp(res->tile_codes[i].art, want) == 0) return (char)i;
    }
    return 0;
}

// The plain-terrain code, used when a variant the pack lacks is requested.
static char plain_code(const Resources *res, Terrain t) {
    char c = mapedit_code_for(res, t, -1);
    return c ? c : '.';
}

// Split "water_edge_10" into terrain + variant. A stem with no _edge_ suffix
// is the plain tile.
static void art_to_cell(const Resources *res, unsigned char code,
                        MapCell *out) {
    const char *art = res->tile_codes[code].art;
    out->terrain = (Terrain)res->tile_codes[code].terrain;
    out->variant = -1;
    out->decor   = 0;
    const char *e = strstr(art, "_edge_");
    if (e && e[6]) {
        out->variant = (int)strtol(e + 6, NULL, 10);
        return;
    }
    // Neither an edge variant nor the plain tile for its terrain: a
    // decorative alternate. Preserve the byte verbatim.
    if (strcmp(art, mapedit_art_for(out->terrain, -1)) != 0) {
        out->decor = (char)code;
    }
}

bool mapedit_load(MapGrid *m, const Resources *res, const char *zone_id) {
    const ResZone *z = NULL;
    for (int i = 0; i < res->zone_count; i++) {
        if (strcmp(res->zones[i].id, zone_id) == 0) { z = &res->zones[i]; break; }
    }
    if (!z) {
        fprintf(stderr, "mapedit: no zone '%s' in this pack\n", zone_id);
        return false;
    }
    if (z->width > MAPEDIT_MAX_W || z->height > MAPEDIT_MAX_H) {
        fprintf(stderr, "mapedit: zone '%s' is %dx%d, over the %dx%d ceiling "
                        "(MAP_MAX_W/H in engine/include/map.h)\n",
                zone_id, z->width, z->height, MAPEDIT_MAX_W, MAPEDIT_MAX_H);
        return false;
    }

    size_t sz = 0;
    const unsigned char *bytes = LoadAssetBytes(z->map_path, &sz);
    if (!bytes) {
        fprintf(stderr, "mapedit: cannot read %s\n", z->map_path);
        return false;
    }

    memset(m, 0, sizeof *m);
    m->w = z->width;
    m->h = z->height;
    snprintf(m->zone_id, sizeof m->zone_id, "%s", zone_id);
    snprintf(m->tile_set, sizeof m->tile_set, "%s", z->tile_set);
    snprintf(m->dat_path, sizeof m->dat_path, "%s", z->map_path);
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++)
            m->cell[y][x] = (MapCell){ TERRAIN_GRASS, -1, 0 };

    // One byte per tile, row-major. Blank lines and '#' comments are skipped;
    // short rows pad with grass, matching engine/map.c.
    const char *p = (const char *)bytes, *end = p + sz;
    int y = 0;
    while (p < end && y < m->h) {
        while (p < end && (*p == '\n' || *p == '\r')) p++;
        if (p < end && *p == '#') {
            while (p < end && *p != '\n') p++;
            continue;
        }
        int x = 0;
        while (p < end && *p != '\n' && *p != '\r' && x < m->w) {
            unsigned char c = (unsigned char)*p++;
            if (c < RES_TILE_CODE_COUNT && res->tile_codes[c].present) {
                art_to_cell(res, c, &m->cell[y][x]);
            } else {
                fprintf(stderr, "mapedit: %s:%d:%d unknown tile code 0x%02x\n",
                        z->map_path, y + 1, x + 1, c);
            }
            x++;
        }
        while (p < end && *p != '\n') p++;
        y++;
    }
    UnloadAssetBytes(bytes);
    m->dirty = false;
    return true;
}

bool mapedit_save(MapGrid *m, const Resources *res) {
    int speckles = mapedit_despeckle(m);
    int unresolved = 0;
    int furnished = mapedit_furnish(m, &unresolved);

    // The .dat is written to the pack directory on disk. An archive pack
    // (.openbounty) has no loose tree to write into -- pack_hash() is set for
    // archives and empty for directory packs, which is how we tell them apart.
    const Pack *pk = pack_stack_top();
    const char *root = pack_path(pk);
    if (!root || !*root || (pack_hash(pk) && *pack_hash(pk))) {
        fprintf(stderr, "mapedit: this pack is a .openbounty archive; open a "
                        "loose directory with --pack <dir> to save\n");
        return false;
    }
    char full[512];
    snprintf(full, sizeof full, "%s/%s", root, m->dat_path);
    FILE *f = fopen(full, "wb");
    if (!f) {
        fprintf(stderr, "mapedit: cannot write %s\n", full);
        return false;
    }

    fprintf(f, "# %s -- %dx%d. Written by openbounty-mapedit.\n"
               "# Fully rendered: edge variants are baked in (REQ-229). The\n"
               "# engine computes nothing about appearance at load.\n",
            m->zone_id, m->w, m->h);
    for (int y = 0; y < m->h; y++) {
        for (int x = 0; x < m->w; x++) {
            const MapCell *cell = &m->cell[y][x];
            char c;
            if (cell->decor && cell->variant < 0) {
                c = cell->decor;          // decorative alternate, kept as-is
            } else {
                c = mapedit_code_for(res, cell->terrain, cell->variant);
                if (!c) c = plain_code(res, cell->terrain);
            }
            fputc(c, f);
        }
        fputc('\n', f);
    }
    fclose(f);
    m->dirty = false;

    fprintf(stdout, "saved %s (%dx%d): %d despeckled, %d furnished",
            full, m->w, m->h, speckles, furnished);
    if (unresolved) fprintf(stdout, ", %d UNRESOLVED", unresolved);
    fprintf(stdout, "\n");
    return true;
}
