// CC archive parser + LZW decoder.
//
// Each .CC archive begins with `count: u16 LE`, followed by `count`
// directory rows of 8 bytes each:
//   u16 LE   filename hash (per cc_hash() below)
//   u24 LE   absolute offset to compressed entry blob
//   u24 LE   compressed entry size (bytes)
//
// Each compressed blob is `u32 LE` (uncompressed size) followed by the
// LZW stream. LZW geometry: 9..12 bit codes, code 0x100 = reset, code
// 0x101 = end-of-stream. LSB-first bit reader within each byte.
//
// Filename recovery: the archive stores only the 16-bit hash of each
// entry's name. We recover real names by hashing every candidate from
// a known table (graphics bases x suffixes + a few specials) and
// looking up the hash. Any unknown hash gets a synthetic name so we
// don't lose the bytes.

#include "extract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- LZW decoder ----------------------------------------------------

#define LZW_MIN_W   9
#define LZW_MAX_W  12
#define LZW_RESET  0x100
#define LZW_END    0x101
#define LZW_DICT_MAX 4096

typedef struct {
    int          parent;   // index of previous entry (or -1 if a literal byte)
    unsigned char value;   // appended byte
} LzwEntry;

// Read `nbits` (9..12) from `payload` at bit position `*bit_pos`, LSB-first.
// Advances `*bit_pos`. Returns -1 if out of range.
static int lzw_read(const unsigned char *payload, size_t total_bits,
                    int *bit_pos, int nbits) {
    if ((size_t)(*bit_pos + nbits) > total_bits) return -1;
    int v = 0;
    for (int i = 0; i < nbits; i++) {
        int bi = *bit_pos + i;
        v |= ((payload[bi >> 3] >> (bi & 7)) & 1) << i;
    }
    *bit_pos += nbits;
    return v;
}

// Decode one entry's LZW stream. `expected` is the first 4 bytes of
// the blob (uncompressed size). Returns 0 on success and writes the
// decoded bytes into a freshly-malloc'd buffer at *out (caller frees).
static int lzw_decode(const unsigned char *payload, size_t payload_len,
                      size_t expected, unsigned char **out) {
    *out = malloc(expected ? expected : 1);
    if (!*out) return -1;
    unsigned char *dst = *out;
    size_t out_pos = 0;

    static LzwEntry dict[LZW_DICT_MAX];
    int dict_size;
    int width = LZW_MIN_W;
    int prev_code = -1;
    unsigned char prev_first = 0;

    // Pre-seed dictionary with single bytes 0..255 + reset/end placeholders.
    for (int i = 0; i < 256; i++) { dict[i].parent = -1; dict[i].value = (unsigned char)i; }
    dict[256].parent = -1; dict[256].value = 0;   // reset
    dict[257].parent = -1; dict[257].value = 0;   // end
    dict_size = 258;

    int bit_pos = 0;
    size_t total_bits = payload_len * 8;

    unsigned char tmp[LZW_DICT_MAX];   // walk-back buffer

    while (out_pos < expected) {
        int code = lzw_read(payload, total_bits, &bit_pos, width);
        if (code < 0) {
            fprintf(stderr, "extract_lzw: bit-stream truncated\n");
            return -1;
        }
        if (code == LZW_RESET) {
            for (int i = 0; i < 256; i++) { dict[i].parent = -1; dict[i].value = (unsigned char)i; }
            dict[256].parent = -1; dict[257].parent = -1;
            dict_size = 258;
            width = LZW_MIN_W;
            prev_code = -1;
            continue;
        }
        if (code == LZW_END) break;

        // Walk back to assemble the entry's bytes (tmp ends up in reverse).
        // Emission iterates from tmp[n-1] down to tmp[0] to recover forward
        // order. So tmp[0] is the LAST output byte; tmp[n-1] is the FIRST.
        int n = 0;
        if (code < dict_size) {
            int t = code;
            while (t >= 0) {
                tmp[n++] = dict[t].value;
                t = dict[t].parent;
            }
        } else if (code == dict_size && prev_code >= 0) {
            // Special case: code refers to the entry we're about to add =
            // prev_entry + prev_first. Output is `prev_entry_bytes,
            // prev_first` in forward order. Reversed: prev_first goes FIRST
            // (lowest tmp index), then prev_entry walked back.
            tmp[n++] = prev_first;
            int p = prev_code;
            while (p >= 0) {
                tmp[n++] = dict[p].value;
                p = dict[p].parent;
            }
        } else {
            fprintf(stderr, "extract_lzw: invalid code 0x%X at dict_size %d\n", code, dict_size);
            return -1;
        }

        // The first byte of this entry is at tmp[n-1] after the walk-back.
        unsigned char first_byte = tmp[n - 1];

        for (int i = n - 1; i >= 0; i--) {
            if (out_pos < expected) dst[out_pos++] = tmp[i];
        }

        // Add prev + first_byte as a new dictionary entry (when there IS a prev).
        if (prev_code >= 0 && dict_size < LZW_DICT_MAX) {
            dict[dict_size].parent = prev_code;
            dict[dict_size].value  = first_byte;
            dict_size++;
            // Width grows when we hit 2^width entries.
            if (dict_size >= (1 << width) && width < LZW_MAX_W) width++;
        }

        prev_code  = code;
        prev_first = first_byte;
    }

    if (out_pos < expected) {
        fprintf(stderr, "extract_lzw: short decode (%zu of %zu)\n", out_pos, expected);
        return -1;
    }
    return 0;
}

// ---- filename hash + recovery table ---------------------------------

static unsigned short cc_hash(const char *name) {
    unsigned short key = 0;
    for (const char *p = name; *p; p++) {
        int n = (unsigned char)*p & 0x7F;
        if (n >= 0x60) n -= 0x20;       // ASCII case fold
        key = (unsigned short)((key >> 8) | (key << 8));   // byte swap
        key = (unsigned short)((key << 1) | (key >> 15));  // 16-bit rotate left 1
        key = (unsigned short)(key + n);
    }
    return key;
}

// Building the hash
// table on every run is cheap (~80 entries x 3 suffixes + 9 specials).
static const char *GRAPHIC_BASES[] = {
    "peas","spri","mili","wolf","skel","zomb","gnom","orcs","arcr","elfs",
    "pike","noma","dwar","ghos","kght","ogre","brbn","trol","cavl","drui",
    "arcm","vamp","gian","demo","drag",
    "mury","hack","ammi","baro","drea","cane","mora","barr","barg","rina",
    "ragf","mahk","auri","czar","magu","urth","arec",
    "knig","pala","sorc","barb",
    "nwcp","title","select",
    "tileseta","tilesetb","tilesalt",
    "cursor","town","cstl","plai","frst","dngn","cave",
    "comtiles","view","endpic",
    NULL
};
static const char *GRAPHIC_SUFFIXES[] = { ".4", ".16", ".256", NULL };
static const char *OTHER_FILES[] = {
    "KB.CH", "LAND.ORG",
    "TIMER.DRV", "SOUND.DRV",
    "CGA.DRV", "EGA.DRV", "TGA.DRV", "HGA.DRV", "MCGA.DRV",
    NULL
};

typedef struct { unsigned short hash; char name[16]; } HashName;

static int build_hash_table(HashName *out, int cap) {
    int n = 0;
    for (int b = 0; GRAPHIC_BASES[b] && n < cap; b++) {
        for (int s = 0; GRAPHIC_SUFFIXES[s] && n < cap; s++) {
            char name[16];
            snprintf(name, sizeof name, "%s%s", GRAPHIC_BASES[b], GRAPHIC_SUFFIXES[s]);
            out[n].hash = cc_hash(name);
            snprintf(out[n].name, sizeof out[n].name, "%s", name);
            n++;
        }
    }
    for (int i = 0; OTHER_FILES[i] && n < cap; i++) {
        out[n].hash = cc_hash(OTHER_FILES[i]);
        snprintf(out[n].name, sizeof out[n].name, "%s", OTHER_FILES[i]);
        n++;
    }
    return n;
}

static const char *hash_lookup(const HashName *table, int n, unsigned short h) {
    for (int i = 0; i < n; i++) if (table[i].hash == h) return table[i].name;
    return NULL;
}

// ---- public entry point ---------------------------------------------

int ex_cc_load(const uint8_t *cc, size_t cc_len, CcArchive *out) {
    out->entries = NULL;
    out->count = 0;
    if (cc_len < 2) return -1;

    int count = (int)(cc[0] | (cc[1] << 8));
    if (count <= 0 || count > 4096) {
        fprintf(stderr, "extract_lzw: bogus archive count %d\n", count);
        return -1;
    }
    if (cc_len < (size_t)(2 + count * 8)) {
        fprintf(stderr, "extract_lzw: archive directory truncated\n");
        return -1;
    }

    HashName table[256];
    int hn = build_hash_table(table, 256);

    CcEntry *entries = calloc((size_t)count, sizeof(CcEntry));
    if (!entries) return -1;

    int decoded = 0;
    for (int i = 0; i < count; i++) {
        size_t row = 2 + (size_t)i * 8;
        unsigned short h = (unsigned short)(cc[row] | (cc[row+1] << 8));
        size_t eoff  = (size_t)cc[row+2] | ((size_t)cc[row+3] << 8) | ((size_t)cc[row+4] << 16);
        size_t esize = (size_t)cc[row+5] | ((size_t)cc[row+6] << 8) | ((size_t)cc[row+7] << 16);

        const char *name = hash_lookup(table, hn, h);
        if (name) snprintf(entries[i].name, sizeof entries[i].name, "%s", name);
        else      snprintf(entries[i].name, sizeof entries[i].name, "unk_%04X", h);

        if (eoff + esize > cc_len || esize < 4) {
            fprintf(stderr, "extract_lzw: entry %d (%s) out of range\n", i, entries[i].name);
            continue;
        }
        const uint8_t *blob = cc + eoff;
        size_t expected = (size_t)blob[0] | ((size_t)blob[1] << 8) |
                          ((size_t)blob[2] << 16) | ((size_t)blob[3] << 24);
        unsigned char *bytes = NULL;
        if (lzw_decode(blob + 4, esize - 4, expected, &bytes) != 0) {
            free(bytes);
            fprintf(stderr, "extract_lzw: failed to decode entry %d (%s)\n", i, entries[i].name);
            continue;
        }
        entries[i].data = bytes;
        entries[i].size = expected;
        decoded++;
    }

    out->entries = entries;
    out->count = count;
    fprintf(stderr, "extract: %d entries decoded (of %d)\n", decoded, count);
    return decoded > 0 ? 0 : -1;
}
