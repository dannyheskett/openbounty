// Art import and validation (GB-241/242), and palette editing (GB-260/262).
//
// Import never silently alters the source. A file that is the wrong size or
// carries soft alpha is REPORTED, with the specific problem named, and any
// fix-up is a separate, explicit act. Quietly resizing a commissioned artist's
// work and shipping the result is how a pack ends up subtly wrong in ways
// nobody can trace.

#include "gb_ui.h"

#include <stdio.h>
#include <string.h>

extern Color PAL[];

// Expected dimensions per category, from docs/ART-SPEC.md. Anything not listed
// has no fixed size (UI chrome varies), so only the format is checked.
typedef struct { const char *dir; int w, h; } ArtSpec;
static const ArtSpec ART_SPEC[] = {
    { "tiles",    48,  34 },
    { "troops",   48,  34 },
    { "villains", 48,  34 },
    { "sprites",  48,  34 },
    { "combat",   48,  34 },
    { "classes",  96, 102 },
};

static bool spec_for(const char *pack_rel, int *w, int *h) {
    for (unsigned i = 0; i < sizeof ART_SPEC / sizeof *ART_SPEC; i++) {
        char frag[64];
        snprintf(frag, sizeof frag, "art/%s/", ART_SPEC[i].dir);
        if (strstr(pack_rel, frag)) {
            *w = ART_SPEC[i].w;
            *h = ART_SPEC[i].h;
            return true;
        }
    }
    return false;
}

int gb_art_check(const char *src_file, const char *pack_rel,
                 GbArtReport *out) {
    memset(out, 0, sizeof *out);
    Image img = LoadImage(src_file);
    if (!img.data) {
        snprintf(out->problem[out->problems++], GB_ART_MSG,
                 "Not a readable image. PNG is what the engine loads.");
        return out->problems;
    }
    out->w = img.width;
    out->h = img.height;

    int ew, eh;
    if (spec_for(pack_rel, &ew, &eh) && (img.width != ew || img.height != eh)) {
        snprintf(out->problem[out->problems++], GB_ART_MSG,
                 "Is %dx%d; this category needs exactly %dx%d.",
                 img.width, img.height, ew, eh);
    }

    // Soft alpha reads as dirt at 48x34 and the renderer treats alpha as
    // binary anyway, so a feathered edge is a defect, not a style.
    int soft = 0, offpal = 0;
    for (int y = 0; y < img.height; y++) {
        for (int x = 0; x < img.width; x++) {
            Color c = GetImageColor(img, x, y);
            if (c.a > 0 && c.a < 255) soft++;
            if (c.a < 128) continue;
            bool found = false;
            for (int i = 0; i < 256 && !found; i++)
                if (PAL[i].r == c.r && PAL[i].g == c.g && PAL[i].b == c.b)
                    found = true;
            if (!found) offpal++;
        }
    }
    out->soft_alpha = soft;
    out->off_palette = offpal;
    if (soft)
        snprintf(out->problem[out->problems++], GB_ART_MSG,
                 "%d pixel(s) are partly transparent. Alpha is treated as "
                 "on or off, so these will harden and may look ragged.", soft);
    if (offpal)
        snprintf(out->problem[out->problems++], GB_ART_MSG,
                 "%d pixel(s) use colours outside the pack palette. They will "
                 "still draw, but the pack will not read as one set.", offpal);

    UnloadImage(img);
    return out->problems;
}

bool gb_art_import(const char *src_file, const char *pack_root,
                   const char *pack_rel, bool fixup, char *err, size_t errsz) {
    Image img = LoadImage(src_file);
    if (!img.data) {
        snprintf(err, errsz, "Could not read:\n%s", src_file);
        return false;
    }
    if (fixup) {
        // Explicit, opt-in, and only ever on the copy being written into the
        // pack -- the source file is never touched.
        int ew, eh;
        if (spec_for(pack_rel, &ew, &eh) &&
            (img.width != ew || img.height != eh))
            ImageResizeNN(&img, ew, eh);
        for (int y = 0; y < img.height; y++)
            for (int x = 0; x < img.width; x++) {
                Color c = GetImageColor(img, x, y);
                c.a = c.a >= 128 ? 255 : 0;
                ImageDrawPixel(&img, x, y, c);
            }
    }
    char out[GB_PATH_MAX * 2];
    snprintf(out, sizeof out, "%s/%s", pack_root, pack_rel);
    bool ok = ExportImage(img, out);
    UnloadImage(img);
    if (!ok) {
        snprintf(err, errsz, "Could not write:\n%s\n\nIs the pack writable?",
                 out);
        return false;
    }
    return true;
}

// --- palette -------------------------------------------------------------------

bool gb_palette_save(const char *pack_root, const char *rel) {
    char path[GB_PATH_MAX * 2];
    snprintf(path, sizeof path, "%s/%s", pack_root, rel);
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    // 256 x RGB, exactly 768 bytes -- the engine rejects anything shorter.
    for (int i = 0; i < 256; i++) {
        fputc(PAL[i].r, f);
        fputc(PAL[i].g, f);
        fputc(PAL[i].b, f);
    }
    fclose(f);
    return true;
}

void gb_palette_set(int index, Color c) {
    if (index < 0 || index > 255) return;
    PAL[index] = (Color){ c.r, c.g, c.b, 255 };
}
