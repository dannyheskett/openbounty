// Shared types and helpers for the extract.c pipeline. Each pipeline
// stage lives in its own TU (extract_unpack.c, extract_lzw.c,
// extract_vga.c, extract_png.c) and pulls only what it needs from
// here.

#ifndef EXTRACT_H
#define EXTRACT_H

#include <stddef.h>
#include <stdint.h>

// One entry from a CC archive after LZW decode.
typedef struct {
    char           name[16];   // original DOS filename ("MCGA.DRV", "cane.256", …)
    uint8_t       *data;       // malloc'd, NUL-terminated for safety
    size_t         size;
} CcEntry;

typedef struct {
    CcEntry *entries;
    int      count;
} CcArchive;

// Read an entire file into a fresh malloc'd buffer. Returns NULL on
// error. *out_size is set to the byte count (excluding the NUL we
// always append).
uint8_t *ex_read_file(const char *path, size_t *out_size);

// mkdir -p `path`. Returns 0 on success.
int ex_mkdir_p(const char *path);

// Write `len` bytes to `path`, creating parent dirs as needed.
int ex_write_file(const char *path, const uint8_t *data, size_t len);

// Find a CC entry by case-insensitive name match. Returns NULL if not found.
const CcEntry *ex_cc_find(const CcArchive *cc, const char *name);

// Free a CC archive (each entry's data + the entries array).
void ex_cc_free(CcArchive *cc);

// ---- pipeline stage entry points ------------------------------------
// Each stage prints a one-line progress message on success and returns
// 0; nonzero on error (also prints to stderr).

// (1) Unpack KB.EXE (EXEPACK or COMPRESSOR) to a fresh buffer.
int ex_unpack_kb_exe(const uint8_t *in, size_t in_len,
                     uint8_t **out, size_t *out_len);

// (2) LZW-decompress a CC archive into an in-memory file table.
int ex_cc_load(const uint8_t *cc_bytes, size_t cc_len, CcArchive *out);

// (3-7) Pipeline stages — each takes the prepared inputs and writes to
// out_dir/<subpath>. Implementations live in extract.c (small ones) or
// dedicated TUs (graphics).
int ex_emit_palette  (const CcArchive *cc, const char *out_dir);
int ex_emit_font     (const CcArchive *cc, const char *out_dir);
int ex_emit_maps     (const CcArchive *cc, const char *out_dir,
                      const char *game_json_path);
int ex_emit_wavs     (const uint8_t *kb_unpacked, size_t kb_len,
                      const char *out_dir);
int ex_emit_graphics (const CcArchive *cc, const CcArchive *cc416,
                      const char *out_dir);

// (8) Synthesize port-only assets that have no DOS source: the 9x6
// puzzle_cover tile (palette-aware: black border + red interior, mirrors
// OpenKB's GR_PIECE in src/lib/dos-data.c:1079).
int ex_emit_synth    (const CcArchive *cc, const char *out_dir);

// (9) Cleanroom procedural chrome_overworld.png. Lives in extract_chrome.c.
// 320x200 yellow-rope frame with transparent interior, generated entirely
// from the VGA palette + a deterministic LCG. No pixels copied from
// the legacy port asset.
int ex_emit_chrome   (const CcArchive *cc, const char *out_dir);

// PNG writer (extract_png.c). RGBA8 only; deterministic-ish (relies on
// miniz's deflate output, which is stable for a given input + level).
int ex_write_png_rgba(const char *path, int w, int h, const uint8_t *rgba);

// VGA renderer (extract_vga.c). Decodes one frame from the .256 file
// at `frame_idx`. Writes RGBA8 to *out_rgba (caller frees), *out_w/h.
// `apply_color_key` non-zero applies the auto-detected transparent
// color (used for sprites with a clear background); zero treats every
// pixel as opaque (used for tile sprites, which the openbounty
// golden ships fully opaque).
int ex_vga_render_frame(const uint8_t *file_data, size_t file_len,
                        int frame_idx,
                        const uint8_t *palette_768,
                        int apply_color_key,
                        uint8_t **out_rgba, int *out_w, int *out_h);

// Returns the number of frames in a .256 graphics file.
int ex_vga_frame_count(const uint8_t *file_data, size_t file_len);

// game.json synthesis — see tools/extract_gamejson.c. Returns a cJSON
// tree (caller takes ownership and must cJSON_Delete it). Forward-
// declared to avoid pulling cJSON.h into extract.h.
//
// `cc` is the loaded 256.CC archive (provides MCGA.DRV, KB.CH, LAND.ORG).
// `kb` is the unpacked KB.EXE bytes (or NULL); some entries (notably
// town-gate Y for entry 8) are read directly from KB.EXE because OpenKB
// has a transcription bug there. Pass NULL to fall back to constants.
struct cJSON;
struct cJSON *ex_gamejson_synthesize(const CcArchive *cc,
                                     const uint8_t *kb, size_t kb_len);

// Top-level entry point. Reads KB.EXE + 256.CC (+ optional 416.CC) from
// `in_dir` and writes the loose asset tree to `out_dir`. Returns 0 on
// success, non-zero on failure. Pass NULL for either argument to use
// the legacy defaults (legacy/bin and assets/kings-bounty).
int extract_run(const char *in_dir, const char *out_dir);

#endif
