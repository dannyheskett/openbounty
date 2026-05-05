// extract — single C utility that produces the openbounty King's Bounty
// asset pack from a user's own copy of the original DOS distribution.
//
// Inputs (default --input legacy/bin/):
//   KB.EXE   — original game binary (EXEPACK-compressed); supplies sign
//              text and PC-speaker tune note tables (after unpack).
//   256.CC   — LZW archive holding MCGA.DRV (palette), KB.CH (bitmap
//              font), LAND.ORG (4 continent tile maps), and all the
//              VGA-mode graphics (.256 files).
//
// Outputs (default --out assets/kings-bounty/):
//   audio/tune_{walk,bump,chest,defeat}.wav
//   maps/{continentia,forestria,archipelia,saharia}.dat
//   palettes/palette.bin
//   art/font/kb-font.png
//   art/{classes,combat,sprites,tiles,troops,ui,villains}/<name>.png
//
// We do NOT generate game.json or the OGG music — those ship with
// openbounty. The OGGs are modern recordings, not derivable from
// KB.EXE; game.json is hand-curated. A future iteration of this tool
// could emit a starter game.json by reading catalogs / strings out of
// KB.EXE; the OutputRow tables make that a pure addition.
//
// Port-only assets (extractor does NOT emit these; shipped with the
// pack):
//   art/ui/chrome_overworld.png  — screenshot composite (320x200), no
//                                   DOS source. Hand-edited at port time.
//   art/tiles/tile-map.json      — JSON manifest, not a raw asset.
//   audio/{openworld,combat}.ogg — modern recordings, not derivable.
//
// Sourcing notes for the assets we DO emit but that don't sit in the
// obvious place:
//   end_{win,lose}_screen.png    — endpic.256 in 416.CC (NOT 256.CC).
//                                   Frame 0 = won, frame 1 = lost.
//   puzzle_cover.png             — synthesized procedurally (ex_emit_synth);
//                                   9x6 black border + palette-color-4
//                                   interior, mirroring OpenKB GR_PIECE.
//   throne_backdrop              — game.json points throne_backdrop at
//                                   end_lose_screen.png (the original DOS
//                                   game has no separate throne art and
//                                   the openbounty port had used a
//                                   pixel-identical copy).
//
// Build:  gcc -std=c99 -O2 -Ithird_party/miniz tools/extract*.c
//             third_party/miniz/miniz.c -o build/extract
// Run:    ./build/extract [--input legacy/bin] [--out assets/kings-bounty]

#define _POSIX_C_SOURCE 200809L
#include "extract.h"
#include "cJSON.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// File-IO helpers (ex_read_file, ex_mkdir_p, ex_write_file, ex_cc_find,
// ex_cc_free) live in extract_io.c so test drivers can link a subset.

// ---- entry point -----------------------------------------------------
//
// extract_run is the library-style entry point used by the engine
// binary's `--extract` flag. It writes a loose asset tree under
// `out_dir`. The standalone tools/extract binary has been removed;
// callers that previously ran `./build/extract` should run
// `./build/openbounty --extract --out-dir <dir>` instead.

int extract_run(const char *in_dir, const char *out_dir) {
    if (!in_dir)  in_dir  = "legacy/bin";
    if (!out_dir) out_dir = "assets/kings-bounty";

    // Stage (1): read + unpack KB.EXE.
    char path[512];
    snprintf(path, sizeof path, "%s/KB.EXE", in_dir);
    size_t exe_len = 0;
    uint8_t *exe_packed = ex_read_file(path, &exe_len);
    if (!exe_packed) return 1;

    uint8_t *exe_unpacked = NULL;
    size_t   exe_unpacked_len = 0;
    if (ex_unpack_kb_exe(exe_packed, exe_len, &exe_unpacked, &exe_unpacked_len) != 0) {
        free(exe_packed);
        return 1;
    }
    free(exe_packed);
    fprintf(stderr, "extract: unpacked KB.EXE (%zu -> %zu bytes)\n",
            exe_len, exe_unpacked_len);

    // Stage (2): LZW-decompress 256.CC (the VGA archive).
    snprintf(path, sizeof path, "%s/256.CC", in_dir);
    size_t cc_len = 0;
    uint8_t *cc_bytes = ex_read_file(path, &cc_len);
    if (!cc_bytes) { free(exe_unpacked); return 1; }

    CcArchive cc = { 0 };
    if (ex_cc_load(cc_bytes, cc_len, &cc) != 0) {
        free(cc_bytes); free(exe_unpacked); return 1;
    }
    free(cc_bytes);
    fprintf(stderr, "extract: 256.CC -> %d entries\n", cc.count);

    // Stage (2b): LZW-decompress 416.CC (mostly EGA siblings, but it
    // also holds endpic.256 — the win/lose ending art that 256.CC
    // doesn't carry). Optional: if the user only has 256.CC, the
    // endpic frames are skipped with a warning.
    snprintf(path, sizeof path, "%s/416.CC", in_dir);
    size_t cc416_len = 0;
    uint8_t *cc416_bytes = ex_read_file(path, &cc416_len);
    CcArchive cc416 = { 0 };
    int have_416 = 0;
    if (cc416_bytes) {
        if (ex_cc_load(cc416_bytes, cc416_len, &cc416) == 0) {
            have_416 = 1;
            fprintf(stderr, "extract: 416.CC -> %d entries\n", cc416.count);
        }
        free(cc416_bytes);
    } else {
        fprintf(stderr, "extract: 416.CC missing — endpic frames will be skipped\n");
    }

    // game.json is the shipped manifest at the OUT location (openbounty
    // ships it; we write everything else around it). For the maps stage
    // we read tile_codes from this file. If it's missing, the user's
    // pack is incomplete; emit a warning and skip the maps stage rather
    // than failing the whole run.
    char game_json[512];
    snprintf(game_json, sizeof game_json, "%s/game.json", out_dir);

    // Emit game.json FIRST so that downstream stages (maps) can read its
    // tile_codes block. The synthesizer reads LAND.ORG positions from
    // `cc` directly.
    {
        cJSON *gj = ex_gamejson_synthesize(&cc, exe_unpacked, exe_unpacked_len);
        if (gj) {
            char *text = cJSON_Print(gj);
            cJSON_Delete(gj);
            if (text) {
                if (ex_write_file(game_json,
                                  (const uint8_t *)text, strlen(text)) != 0) {
                    fprintf(stderr, "extract: cannot write %s\n", game_json);
                    free(text);
                    return 1;
                }
                fprintf(stderr, "extract: wrote %s (%zu bytes)\n",
                        game_json, strlen(text));
                free(text);
            }
        }
    }

    int rc = 0;
    rc |= ex_emit_palette (&cc, out_dir);
    rc |= ex_emit_font    (&cc, out_dir);
    rc |= ex_emit_maps    (&cc, out_dir, game_json);
    rc |= ex_emit_wavs    (exe_unpacked, exe_unpacked_len, out_dir);
    rc |= ex_emit_graphics(&cc, have_416 ? &cc416 : NULL, out_dir);
    rc |= ex_emit_synth   (&cc, out_dir);
    rc |= ex_emit_chrome  (&cc, out_dir);

    ex_cc_free(&cc);
    if (have_416) ex_cc_free(&cc416);
    free(exe_unpacked);

    if (rc != 0) {
        fprintf(stderr, "extract: one or more stages failed\n");
        return 1;
    }
    fprintf(stderr, "extract: done -> %s\n", out_dir);
    return 0;
}

// ---- temporary stubs for stages whose TU isn't written yet ----------
//
// We compile the scaffold today so the dispatch flow is verifiable as
// each TU lands. Each stub prints a "[skip] not implemented" line and
// returns 0. As the real implementations land, the stub here gets
// removed and the symbol is provided by the dedicated TU.

#define STUB(name) \
    int name() { fprintf(stderr, "extract: [skip] " #name " — not implemented\n"); return 0; }


// ---- Stage (3): palette ---------------------------------------------
//
// MCGA.DRV @ 0x032D, 768 bytes (256 × 6-bit RGB). The engine expands
// 6→8 bits at load time.

int ex_emit_palette(const CcArchive *cc, const char *out_dir) {
    const CcEntry *mcga = ex_cc_find(cc, "MCGA.DRV");
    if (!mcga) { fprintf(stderr, "extract: MCGA.DRV not found\n"); return -1; }
    if (mcga->size < 0x032D + 768) {
        fprintf(stderr, "extract: MCGA.DRV too small (%zu)\n", mcga->size);
        return -1;
    }
    // 6-bit VGA triples expanded to 8-bit via DOS_ReadPalette_RW formula
    // (OpenKB src/lib/dos-data.c:183): out = in * 255 / 63. The engine
    // (src/classic/palette.c:38) reads 8-bit values directly.
    unsigned char out[768];
    const unsigned char *src = mcga->data + 0x032D;
    for (int i = 0; i < 768; i++) out[i] = (unsigned char)((src[i] * 255) / 63);
    char path[512];
    snprintf(path, sizeof path, "%s/palettes/palette.bin", out_dir);
    if (ex_write_file(path, out, 768) != 0) return -1;
    fprintf(stderr, "extract: wrote palette (768 bytes)\n");
    return 0;
}

// ---- Stage (4): font ------------------------------------------------
//
// KB.CH = 1024 bytes. 128 glyphs × 8 rows × 1 byte (1bpp MSB-first).
// Output: 1024×8 RGBA PNG. White-opaque on transparent so bfont.c can
// tint with a multiply.

int ex_emit_font(const CcArchive *cc, const char *out_dir) {
    const CcEntry *kbch = ex_cc_find(cc, "KB.CH");
    if (!kbch) { fprintf(stderr, "extract: KB.CH not found\n"); return -1; }
    if (kbch->size != 1024) {
        fprintf(stderr, "extract: KB.CH size %zu (expected 1024)\n", kbch->size);
        return -1;
    }
    int W = 128 * 8, H = 8;
    uint8_t *rgba = calloc((size_t)W * H * 4, 1);
    if (!rgba) return -1;
    for (int g = 0; g < 128; g++) {
        int base_x = g * 8;
        for (int row = 0; row < 8; row++) {
            unsigned b = kbch->data[g * 8 + row];
            for (int col = 0; col < 8; col++) {
                if (b & (0x80u >> col)) {
                    size_t off = ((size_t)row * W + base_x + col) * 4;
                    rgba[off + 0] = 255;
                    rgba[off + 1] = 255;
                    rgba[off + 2] = 255;
                    rgba[off + 3] = 255;
                }
            }
        }
    }
    char path[512];
    snprintf(path, sizeof path, "%s/art/font/kb-font.png", out_dir);
    int rc = ex_write_png_rgba(path, W, H, rgba);
    free(rgba);
    if (rc == 0) fprintf(stderr, "extract: wrote font %dx%d\n", W, H);
    return rc;
}

// ---- Stage (5): maps ------------------------------------------------
//
// LAND.ORG: 16384 bytes = 4 × 64×64 continents in KB Y-up storage. We
// flip Y to display Y-down and translate each byte through a TILE_MAP
// table to get an art name, then through game.json's tile_codes
// inverse to get an ASCII char. Output: 4 .dat files with a 1-line
// header + 64 rows.
//
// TILE_MAP comes from install_named_tiles.py:20-105 baked here as a
// static array. tile_codes inverse is built from game.json at runtime
// via cJSON.

#include "cJSON.h"

typedef struct { unsigned char b; const char *art; } TileMapEntry;

// Mirror of install_named_tiles.py:20-105. We ONLY need the byte→art
// mapping for terrain rendering; the TILE_MAP rows that map to
// interactive sprites (chest/sign/town/dwelling/etc.) are placed by
// the engine at MapLoadZone time from the JSON, not stamped into the
// .dat. So those rows fall through to '.'.
static const TileMapEntry TILE_MAP[] = {
    { 0x00, "grass" },
    { 0x01, "grass_variant" },
    { 0x02, "castle_tl" },
    { 0x03, "castle_ml" },
    { 0x04, "castle_br" },
    { 0x05, "castle_gate" },
    { 0x06, "castle_tr" },
    { 0x07, "castle_mr" },
    { 0x08, "bridge_h" },
    { 0x09, "bridge_v" },
    { 0x0A, "town" },
    { 0x0B, "chest" },
    { 0x0C, "dwelling_plains" },
    { 0x0D, "dwelling_forest" },
    { 0x0E, "dwelling_hills" },
    { 0x0F, "dwelling_dungeon" },
    { 0x10, "sign" },
    { 0x11, "wandering_army" },
    { 0x12, "artifact_ring" },
    { 0x13, "artifact_chest" },
    // Water variants 0x14..0x20.
    { 0x14, "water_edge_00" }, { 0x15, "water_edge_01" }, { 0x16, "water_edge_02" },
    { 0x17, "water_edge_03" }, { 0x18, "water_edge_04" }, { 0x19, "water_edge_05" },
    { 0x1A, "water_edge_06" }, { 0x1B, "water_edge_07" }, { 0x1C, "water_edge_08" },
    { 0x1D, "water_edge_09" }, { 0x1E, "water_edge_10" }, { 0x1F, "water_edge_11" },
    { 0x20, "water" },
    // Forest: anchor at 0x2D, edges 0x21..0x2C (matches install_named_tiles.py).
    { 0x21, "forest_edge_01" }, { 0x22, "forest_edge_02" }, { 0x23, "forest_edge_03" },
    { 0x24, "forest_edge_04" }, { 0x25, "forest_edge_05" }, { 0x26, "forest_edge_06" },
    { 0x27, "forest_edge_07" }, { 0x28, "forest_edge_08" }, { 0x29, "forest_edge_09" },
    { 0x2A, "forest_edge_10" }, { 0x2B, "forest_edge_11" }, { 0x2C, "forest_edge_12" },
    { 0x2D, "forest" },
    // Desert: anchor at 0x3A, edges 0x2E..0x39.
    { 0x2E, "desert_edge_01" }, { 0x2F, "desert_edge_02" }, { 0x30, "desert_edge_03" },
    { 0x31, "desert_edge_04" }, { 0x32, "desert_edge_05" }, { 0x33, "desert_edge_06" },
    { 0x34, "desert_edge_07" }, { 0x35, "desert_edge_08" }, { 0x36, "desert_edge_09" },
    { 0x37, "desert_edge_10" }, { 0x38, "desert_edge_11" }, { 0x39, "desert_edge_12" },
    { 0x3A, "desert" },
    // Mountain: anchor at 0x47, edges 0x3B..0x46.
    { 0x3B, "mountain_edge_01" }, { 0x3C, "mountain_edge_02" }, { 0x3D, "mountain_edge_03" },
    { 0x3E, "mountain_edge_04" }, { 0x3F, "mountain_edge_05" }, { 0x40, "mountain_edge_06" },
    { 0x41, "mountain_edge_07" }, { 0x42, "mountain_edge_08" }, { 0x43, "mountain_edge_09" },
    { 0x44, "mountain_edge_10" }, { 0x45, "mountain_edge_11" }, { 0x46, "mountain_edge_12" },
    { 0x47, "mountain" },
};
#define TILE_MAP_N (sizeof TILE_MAP / sizeof TILE_MAP[0])

static const char *byte_to_art(unsigned char b) {
    for (size_t i = 0; i < TILE_MAP_N; i++) if (TILE_MAP[i].b == b) return TILE_MAP[i].art;
    return NULL;
}

// Build art_name → ASCII char map by reading game.json tile_codes.
typedef struct { char art[40]; char ch; } ArtChar;

static int load_art_to_char(const char *game_json_path,
                            ArtChar *out, int cap) {
    size_t n = 0;
    char *text = (char *)ex_read_file(game_json_path, &n);
    if (!text) return -1;
    cJSON *root = cJSON_ParseWithLength(text, n);
    free(text);
    if (!root) { fprintf(stderr, "extract: cannot parse %s\n", game_json_path); return -1; }

    cJSON *tc = cJSON_GetObjectItem(root, "tile_codes");
    int built = 0;
    if (cJSON_IsObject(tc)) {
        cJSON *child;
        cJSON_ArrayForEach(child, tc) {
            const char *ch_str = child->string;
            if (!ch_str || !ch_str[0]) continue;
            cJSON *art = cJSON_GetObjectItem(child, "art");
            if (!cJSON_IsString(art)) continue;
            // First-seen wins (matches Python comment: "Multiple chars can
            // map to the same art (e.g. several grass variants); we take
            // the FIRST char so output is deterministic.")
            int dup = 0;
            for (int i = 0; i < built; i++) {
                if (strcmp(out[i].art, art->valuestring) == 0) { dup = 1; break; }
            }
            if (dup) continue;
            if (built >= cap) break;
            snprintf(out[built].art, sizeof out[built].art, "%s", art->valuestring);
            out[built].ch = ch_str[0];
            built++;
        }
    }
    cJSON_Delete(root);
    return built;
}

static char art_to_char(const ArtChar *table, int n, const char *art) {
    if (!art) return '.';
    for (int i = 0; i < n; i++) if (strcmp(table[i].art, art) == 0) return table[i].ch;
    return '.';
}

static char land_byte_to_char(unsigned char b, const ArtChar *table, int n) {
    if (b == 0xFF) b = 0x20;       // continent-end → water
    unsigned char idx = b & 0x7F;
    if (idx > 0x47) return '.';
    return art_to_char(table, n, byte_to_art(idx));
}

int ex_emit_maps(const CcArchive *cc, const char *out_dir, const char *game_json) {
    const CcEntry *land = ex_cc_find(cc, "LAND.ORG");
    if (!land) { fprintf(stderr, "extract: LAND.ORG not found\n"); return -1; }
    if (land->size != 4 * 64 * 64) {
        fprintf(stderr, "extract: LAND.ORG size %zu (expected 16384)\n", land->size);
        return -1;
    }

    ArtChar table[128];
    int tn = load_art_to_char(game_json, table, 128);
    if (tn <= 0) {
        fprintf(stderr, "extract: cannot load tile_codes from %s\n", game_json);
        return -1;
    }

    static const struct { const char *name; size_t off; } continents[4] = {
        { "continentia", 0x0000 },
        { "forestria",   0x1000 },
        { "archipelia",  0x2000 },
        { "saharia",     0x3000 },
    };

    char path[512];
    char buf[5 * 1024];
    for (int c = 0; c < 4; c++) {
        const uint8_t *raw = land->data + continents[c].off;
        int len = snprintf(buf, sizeof buf, "# %s 64x64\n", continents[c].name);
        for (int disp_y = 0; disp_y < 64; disp_y++) {
            int kb_y = 63 - disp_y;
            for (int kb_x = 0; kb_x < 64; kb_x++) {
                buf[len++] = land_byte_to_char(raw[kb_y * 64 + kb_x], table, tn);
            }
            buf[len++] = '\n';
        }
        snprintf(path, sizeof path, "%s/maps/%s.dat", out_dir, continents[c].name);
        if (ex_write_file(path, (const uint8_t *)buf, (size_t)len) != 0) return -1;
        fprintf(stderr, "extract: wrote %s (%d bytes)\n", path, len);
    }
    return 0;
}

// ---- Stage (6): WAVs ------------------------------------------------
//
// Synthesize 4 PC-speaker tunes from the unpacked KB.EXE.
//
// Layout in the unpacked binary (relative to whichever base the
// unpacker chose — in our output, around 0x16800):
//
//   freq palette   88 × u16 LE   (Hz; index 0 = silence/marker)
//   delay palette  16 × u16 LE   (ms; first 8 = power-of-two-ish)
//   tune ptr table 10 × u16 LE   (offsets relative to DATA_SEGMENT 0x15690)
//   tune note streams (slots 0/1/5/7), each terminated by 0xFF.
//
// We don't hardcode offsets. The delay palette has a distinctive
// header (2000, 1000, 500, 250, 125, 62, 31, 15) so we scan for it
// and derive every other offset from there.
//
// Synthesis: 22050 Hz mono s16 square wave, ±0x4000 amplitude, phase
// resets between notes. freq_idx==0 emits silence.

static int find_delay_palette(const uint8_t *kb, size_t klen) {
    // 16 LE u16: 2000, 1000, 500, 250, 125, 62, 31, 15, then 2000, 1500, 750, 375, ...
    // First 8 bytes = d0 07 e8 03 f4 01 fa 00.
    static const uint8_t sig[16] = {
        0xd0, 0x07,   // 2000
        0xe8, 0x03,   // 1000
        0xf4, 0x01,   // 500
        0xfa, 0x00,   // 250
        0x7d, 0x00,   // 125
        0x3e, 0x00,   // 62
        0x1f, 0x00,   // 31
        0x0f, 0x00,   // 15
    };
    for (size_t i = 0; i + sizeof sig < klen; i++) {
        if (memcmp(kb + i, sig, sizeof sig) == 0) return (int)i;
    }
    return -1;
}

// Append `samples` samples of a square wave at `freq_hz` to `out`.
// samples = ms * sample_rate / 1000. Phase starts at 0.
static void append_square(int16_t *out, size_t *pos, int sample_rate,
                          int freq_hz, int ms, int amp) {
    long n = ((long)sample_rate * ms + 500) / 1000;   // round to nearest sample
    if (freq_hz <= 0 || ms <= 0) {
        // Silence (freq 0 or zero-length).
        for (long i = 0; i < n; i++) out[(*pos)++] = 0;
        return;
    }
    // Half-period in samples (square wave toggles every half period).
    long half_period = (long)((double)sample_rate / (2.0 * freq_hz) + 0.5);
    if (half_period < 1) half_period = 1;
    long phase = 0;
    int16_t level = (int16_t)amp;
    for (long i = 0; i < n; i++) {
        out[(*pos)++] = level;
        phase++;
        if (phase >= half_period) {
            phase = 0;
            level = (int16_t)(-level);
        }
    }
}

static int write_wav(const char *path, int sample_rate,
                     const int16_t *pcm, size_t n_samples) {
    size_t bytes = n_samples * sizeof(int16_t);
    unsigned char hdr[44];
    // RIFF chunk
    memcpy(hdr,    "RIFF", 4);
    unsigned long riff_size = 36 + bytes;
    hdr[4] = (unsigned char)(riff_size); hdr[5] = (unsigned char)(riff_size >> 8);
    hdr[6] = (unsigned char)(riff_size >> 16); hdr[7] = (unsigned char)(riff_size >> 24);
    memcpy(hdr+8,  "WAVE", 4);
    // fmt subchunk
    memcpy(hdr+12, "fmt ", 4);
    hdr[16]=16; hdr[17]=0; hdr[18]=0; hdr[19]=0;        // subchunk size = 16
    hdr[20]=1;  hdr[21]=0;                                // audio fmt = PCM
    hdr[22]=1;  hdr[23]=0;                                // num_channels = 1
    unsigned long sr = (unsigned long)sample_rate;
    hdr[24] = (unsigned char)sr;        hdr[25] = (unsigned char)(sr >> 8);
    hdr[26] = (unsigned char)(sr >> 16); hdr[27] = (unsigned char)(sr >> 24);
    unsigned long byte_rate = sr * 2;
    hdr[28] = (unsigned char)byte_rate;        hdr[29] = (unsigned char)(byte_rate >> 8);
    hdr[30] = (unsigned char)(byte_rate >> 16); hdr[31] = (unsigned char)(byte_rate >> 24);
    hdr[32]=2;  hdr[33]=0;                                // block align = 2
    hdr[34]=16; hdr[35]=0;                                // bits per sample = 16
    // data subchunk
    memcpy(hdr+36, "data", 4);
    hdr[40] = (unsigned char)bytes;        hdr[41] = (unsigned char)(bytes >> 8);
    hdr[42] = (unsigned char)(bytes >> 16); hdr[43] = (unsigned char)(bytes >> 24);

    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (fwrite(hdr, 1, 44, f) != 44) { fclose(f); return -1; }
    if (bytes > 0 && fwrite(pcm, 1, bytes, f) != bytes) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

#define KB_DATA_SEGMENT     0x15690u
#define FREQ_PALETTE_N      88
#define DELAY_PALETTE_N     16
#define TUNE_PTR_N          10

int ex_emit_wavs(const uint8_t *kb, size_t klen, const char *out_dir) {
    int delay_off = find_delay_palette(kb, klen);
    if (delay_off < 0) {
        fprintf(stderr, "extract: cannot locate delay palette in unpacked EXE\n");
        return -1;
    }
    int freq_off = delay_off - FREQ_PALETTE_N * 2;
    int tune_ptr_off = delay_off + DELAY_PALETTE_N * 2;
    if (freq_off < 0 || tune_ptr_off + TUNE_PTR_N * 2 > (int)klen) {
        fprintf(stderr, "extract: palette layout overflows file\n");
        return -1;
    }

    uint16_t freqs[FREQ_PALETTE_N];
    for (int i = 0; i < FREQ_PALETTE_N; i++) {
        freqs[i] = (uint16_t)(kb[freq_off + 2*i] | (kb[freq_off + 2*i + 1] << 8));
    }
    uint16_t delays[DELAY_PALETTE_N];
    for (int i = 0; i < DELAY_PALETTE_N; i++) {
        delays[i] = (uint16_t)(kb[delay_off + 2*i] | (kb[delay_off + 2*i + 1] << 8));
    }
    uint16_t tune_ptrs[TUNE_PTR_N];
    for (int i = 0; i < TUNE_PTR_N; i++) {
        tune_ptrs[i] = (uint16_t)(kb[tune_ptr_off + 2*i] | (kb[tune_ptr_off + 2*i + 1] << 8));
    }

    // The tune_ptr values are offsets relative to DATA_SEGMENT 0x15690 in
    // the ORIGINAL packed-from layout. In our unpacked output the data
    // base is shifted; we recover the shift by computing where the freq
    // palette landed in our file vs. where it lived in the original.
    // Original freq palette was at 0x189D1; our freq_off is wherever we
    // found it. Delta = freq_off - 0x189D1.
    int delta = freq_off - 0x189D1;

    static const struct { int slot; const char *name; } tunes[4] = {
        { 0, "walk"   },
        { 1, "bump"   },
        { 5, "chest"  },
        { 7, "defeat" },
    };

    const int SR = 22050;
    const int AMP = 0x4000;

    for (int t = 0; t < 4; t++) {
        long file_off = (long)KB_DATA_SEGMENT + tune_ptrs[tunes[t].slot] + delta;
        if (file_off < 0 || file_off >= (long)klen) {
            fprintf(stderr, "extract: tune %s offset 0x%lx out of range\n",
                    tunes[t].name, file_off);
            return -1;
        }
        // Read note pairs until 0xFF terminator.
        int notes[256][2];
        int nn = 0;
        for (long p = file_off; p < (long)klen && nn < 256;) {
            unsigned f = kb[p++];
            if (f == 0xFF) break;
            if (p >= (long)klen) break;
            unsigned d = kb[p++];
            notes[nn][0] = (int)f;
            notes[nn][1] = (int)d;
            nn++;
        }
        if (nn == 0) {
            fprintf(stderr, "extract: tune %s empty\n", tunes[t].name);
            continue;
        }
        // Total sample count.
        size_t total = 0;
        for (int i = 0; i < nn; i++) {
            int ms = (notes[i][1] < DELAY_PALETTE_N) ? delays[notes[i][1]] : 0;
            total += ((size_t)SR * ms + 500) / 1000;
        }
        int16_t *pcm = calloc(total + 8, sizeof(int16_t));
        if (!pcm) return -1;
        size_t pos = 0;
        for (int i = 0; i < nn; i++) {
            int fi = notes[i][0];
            int di = notes[i][1];
            int hz = (fi < FREQ_PALETTE_N) ? freqs[fi] : 0;
            int ms = (di < DELAY_PALETTE_N) ? delays[di] : 0;
            // KB's freq table mixes some non-musical values at low indices;
            // they're used as guard rails. Treat any > 8000 Hz as silence
            // to match the engine's behavior on those notes.
            if (hz > 8000 || hz <= 30) hz = 0;
            append_square(pcm, &pos, SR, hz, ms, AMP);
        }
        char path[512];
        snprintf(path, sizeof path, "%s/audio/tune_%s.wav", out_dir, tunes[t].name);
        // Make sure parent dir exists.
        char dir[512];
        snprintf(dir, sizeof dir, "%s/audio", out_dir);
        ex_mkdir_p(dir);
        if (write_wav(path, SR, pcm, pos) != 0) {
            free(pcm);
            fprintf(stderr, "extract: cannot write %s\n", path);
            return -1;
        }
        fprintf(stderr, "extract: wrote %s (%d notes, %zu samples)\n",
                path, nn, pos);
        free(pcm);
    }
    return 0;
}
// ---- Stage (7): graphics --------------------------------------------
//
// The OutputRow table maps (raw .256 entry, frame index) → output path
// inside the pack. Entries were derived once during implementation by
// rendering every frame of every .256 file with the VGA renderer,
// SHA-256-ing the decoded RGBA buffer, then matching against the
// SHA of the pre-existing golden assets/kings-bounty/art tree.
//
// Tile entries (tileseta/tilesetb/tilesalt) render with no color-key
// because golden tile PNGs are fully opaque (a hand-edit applied
// during the original port).
//
// Frames not listed here are silently skipped — they're variants the
// engine doesn't reference (alternate tilesets, sub-cursors, etc.).
// 14 view.256 hud-icon frames are not in the table because the
// raw→hud filename mapping was applied by hand in the original port
// and we haven't recovered it; they're left as a TODO. The user can
// manually copy from the rendered intermediates if they need exact
// hud art parity, or substitute their own.

typedef struct {
    const char *raw;       // CC entry name ("arcr.256")
    int         frame;     // 0-based frame index
    const char *out_rel;   // pack-relative output path
} OutputRow;

static const OutputRow OUTPUT_MAP[] = {
#include "extract_output_map.inc"
};
#define OUTPUT_MAP_N (sizeof OUTPUT_MAP / sizeof OUTPUT_MAP[0])

static int is_tile_entry(const char *raw) {
    return strncmp(raw, "tileset", 7) == 0 || strncmp(raw, "tilesalt", 8) == 0;
}

int ex_emit_graphics(const CcArchive *cc, const CcArchive *cc416,
                     const char *out_dir) {
    const CcEntry *mcga = ex_cc_find(cc, "MCGA.DRV");
    if (!mcga) { fprintf(stderr, "extract: MCGA.DRV missing for graphics\n"); return -1; }
    const uint8_t *pal = mcga->data + 0x032D;

    int written = 0;
    for (size_t i = 0; i < OUTPUT_MAP_N; i++) {
        const OutputRow *r = &OUTPUT_MAP[i];
        // Rows whose raw name starts with "416:" come from 416.CC; the
        // suffix after the colon is the entry name in that archive.
        const char *raw = r->raw;
        const CcArchive *src = cc;
        if (strncmp(raw, "416:", 4) == 0) {
            if (!cc416) {
                fprintf(stderr, "extract: skip %s (416.CC unavailable)\n", raw);
                continue;
            }
            raw = r->raw + 4;
            src = cc416;
        }
        const CcEntry *e = ex_cc_find(src, raw);
        if (!e) {
            fprintf(stderr, "extract: warning: %s not in archive (row %zu)\n",
                    r->raw, i);
            continue;
        }
        uint8_t *rgba = NULL; int w = 0, h = 0;
        int color_key = is_tile_entry(raw) ? 0 : 1;
        if (ex_vga_render_frame(e->data, e->size, r->frame, pal,
                                color_key, &rgba, &w, &h) != 0) {
            fprintf(stderr, "extract: render fail for %s frame %d\n",
                    r->raw, r->frame);
            continue;
        }
        char path[512];
        snprintf(path, sizeof path, "%s/%s", out_dir, r->out_rel);
        if (ex_write_png_rgba(path, w, h, rgba) == 0) written++;
        free(rgba);
    }
    fprintf(stderr, "extract: wrote %d sprite PNGs\n", written);
    return 0;
}

// ---- Stage (8): synthesized port-only assets ------------------------
//
// puzzle_cover.png — 9x6 black-bordered red fill. OpenKB's GR_PIECE
// generates the same surface procedurally (src/lib/dos-data.c:1079):
// 9x6 palette surface, black (color 0) outline, interior filled with
// palette color 4 (red in 256-color mode, magenta in CGA). We use the
// 256-color value: palette index 4.

int ex_emit_synth(const CcArchive *cc, const char *out_dir) {
    const CcEntry *mcga = ex_cc_find(cc, "MCGA.DRV");
    if (!mcga) { fprintf(stderr, "extract: MCGA.DRV missing for synth\n"); return -1; }
    const uint8_t *pal = mcga->data + 0x032D;

    // Bit-replicate 6→8 (matches the VGA renderer).
    #define EXPAND6(v) (((v) << 2) | ((v) >> 4))
    uint8_t r0 = EXPAND6(pal[0 * 3 + 0] & 0x3F);
    uint8_t g0 = EXPAND6(pal[0 * 3 + 1] & 0x3F);
    uint8_t b0 = EXPAND6(pal[0 * 3 + 2] & 0x3F);
    uint8_t r4 = EXPAND6(pal[4 * 3 + 0] & 0x3F);
    uint8_t g4 = EXPAND6(pal[4 * 3 + 1] & 0x3F);
    uint8_t b4 = EXPAND6(pal[4 * 3 + 2] & 0x3F);
    #undef EXPAND6

    const int W = 9, H = 6;
    uint8_t rgba[W * H * 4];
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int o = (y * W + x) * 4;
            int border = (x == W - 1 || y == H - 1);
            rgba[o + 0] = border ? r0 : r4;
            rgba[o + 1] = border ? g0 : g4;
            rgba[o + 2] = border ? b0 : b4;
            rgba[o + 3] = 255;
        }
    }
    char path[512];
    snprintf(path, sizeof path, "%s/art/ui/puzzle_cover.png", out_dir);
    if (ex_write_png_rgba(path, W, H, rgba) != 0) return -1;
    fprintf(stderr, "extract: wrote puzzle_cover (9x6 synthesized)\n");
    return 0;
}
