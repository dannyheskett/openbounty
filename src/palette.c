#include "palette.h"
#include "assets.h"
#include <stddef.h>

// Canonical first-16 color fallback.  PAL_* constants
// and is used when palette_init fails to load the 256-color table from
// MCGA.DRV — the chrome still renders correctly with just these.
static const Color FALLBACK_16[16] = {
    {   0,   0,   0, 255 }, {   0,   0, 170, 255 },
    {   0, 170,   0, 255 }, {   0, 170, 170, 255 },
    { 170,   0,   0, 255 }, { 170,   0, 170, 255 },
    { 170,  85,   0, 255 }, { 170, 170, 170, 255 },
    {  85,  85,  85, 255 }, {  85,  85, 255, 255 },
    {  85, 255,  85, 255 }, {  85, 255, 255, 255 },
    { 255,  85,  85, 255 }, { 255,  85, 255, 255 },
    { 255, 255,  85, 255 }, { 255, 255, 255, 255 },
};

Color PAL[PAL_SIZE];

static void install_fallback(void) {
    for (int i = 0; i < 16; i++) PAL[i] = FALLBACK_16[i];
    for (int i = 16; i < PAL_SIZE; i++) PAL[i] = (Color){ 0, 0, 0, 255 };
}

bool palette_init(const char *path) {
    install_fallback();
    if (!path || !*path) return false;

    size_t size = 0;
    const unsigned char *data = LoadAssetBytes(path, &size);
    if (!data) return false;
    if (size < 768) {
        UnloadAssetBytes(data);
        return false;
    }

    // 256 x (R,G,B) already scaled to 8-bit by the extractor.
    for (int i = 0; i < 256; i++) {
        PAL[i].r = data[i * 3 + 0];
        PAL[i].g = data[i * 3 + 1];
        PAL[i].b = data[i * 3 + 2];
        PAL[i].a = 255;
    }
    UnloadAssetBytes(data);
    return true;
}
