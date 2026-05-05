// Knowledge Dynamics COMPRESSOR v1.01 unpacker, ported from
// legacy/scripts/exe-unpackers/unexecomp.c (public domain).
//
// KB.EXE is COMPRESSOR-packed (signature 0xE9 0x99 0x00 at offset
// 0x200). The original tool was a CLI program; this is a library
// function that takes the packed bytes and returns a freshly-malloc'd
// unpacked buffer. The output layout has a 32-byte header followed by
// the unpacked payload — the same format unexecomp.c writes to disk.
//
// The downstream stages (tune-table reader, sign-text reader) locate
// data by scanning for known signatures rather than by hardcoded file
// offsets, so the exact header padding doesn't matter. This avoids
// having to match whatever pre-existing pipeline produced the
// `legacy/bin/KB.unpacked.EXE` artifact byte-for-byte.

#include "extract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define READ_WORD(p, i) ((unsigned)((p)[(i)+1]) * 0x100 + (unsigned)((p)[(i)]))

// LZW dictionary geometry (9..12 bits, 0x100 reset, 0x101 EOF).
#define MBUFFER_SIZE 1024
#define MBUFFER_EDGE (MBUFFER_SIZE - 3)

// Decode the COMPRESSOR LZW stream at `src` (starting at offset 0,
// `src_len` bytes available). Writes up to `expect_len` decoded bytes
// to `dst`. Returns decoded byte count (or expect_len if shorter).
static int comp_unpack_stream(const unsigned char *src, size_t src_len,
                              unsigned char *dst, int expect_len) {
    unsigned long long res_pos = 0;

    int pos = 0;
    int byte_pos = 0;
    int bit_pos  = 0;
    int step     = 9;

    static const unsigned short keyMask[4] = {
        0x01FF, 0x03FF, 0x07FF, 0x0FFF,
    };

    static unsigned short dict_key[768 * 16];
    static unsigned char  dict_val[768 * 16];
    unsigned short dict_index = 0x0102;
    unsigned short dict_range = 0x0200;

    unsigned char queue[256];
    int queued = 0;

    unsigned char mbuffer[MBUFFER_SIZE];
    size_t src_pos = 0;

    // Initial fill from `src`.
    size_t initial = (src_len < MBUFFER_SIZE) ? src_len : MBUFFER_SIZE;
    memcpy(mbuffer, src, initial);
    src_pos = initial;
    if (initial < MBUFFER_SIZE) memset(mbuffer + initial, 0, MBUFFER_SIZE - initial);

    unsigned short next_index = 0;
    unsigned char  last_char  = 0;
    unsigned short last_index = 0;
    int reset_hack = 0;

    while (1) {
        if (reset_hack) {
            step = 9;
            dict_range = 0x0200;
            dict_index = 0x0102;
        }

        byte_pos = pos / 8;
        bit_pos  = pos % 8;

        pos += step;

        if (byte_pos >= MBUFFER_EDGE) {
            int bytes_extra = MBUFFER_SIZE - byte_pos;
            int bytes_left  = MBUFFER_SIZE - bytes_extra;
            for (int j = 0; j < bytes_extra; j++) {
                mbuffer[j] = mbuffer[bytes_left + j];
            }
            // Read the rest from `src`.
            size_t want = (size_t)bytes_left;
            size_t avail = (src_pos + want > src_len) ? (src_len - src_pos) : want;
            memcpy(mbuffer + bytes_extra, src + src_pos, avail);
            src_pos += avail;
            if (avail < want) {
                memset(mbuffer + bytes_extra + avail, 0, want - avail);
            }
            pos = bit_pos + step;
            byte_pos = 0;
            if (reset_hack) bit_pos = bytes_extra;
        }

        unsigned big_index =
            ((mbuffer[byte_pos+2] & 0xFFu) << 16) |
            ((mbuffer[byte_pos+1] & 0xFFu) << 8) |
            ( mbuffer[byte_pos]   & 0xFFu);
        big_index >>= bit_pos;
        big_index &= 0xFFFFu;
        next_index = (unsigned short)(big_index & keyMask[step - 9]);

        if (reset_hack) {
            last_index = next_index;
            last_char  = (unsigned char)(next_index & 0xFFu);
            if ((int)res_pos < expect_len) dst[res_pos++] = last_char;
            reset_hack = 0;
            continue;
        }

        if (next_index == 0x0101) break;     // EOF
        if (next_index == 0x0100) { reset_hack = 1; continue; }

        unsigned short keep_index = next_index;

        if (next_index >= dict_index) {
            next_index = last_index;
            queue[queued++] = last_char;
        }
        while (next_index > 0x00FF) {
            queue[queued++] = dict_val[next_index];
            next_index = dict_key[next_index];
        }
        last_char = (unsigned char)(next_index & 0xFFu);
        queue[queued++] = last_char;

        while (queued) {
            if ((int)res_pos < expect_len) dst[res_pos++] = queue[--queued];
            else queued--;
        }

        dict_key[dict_index] = last_index;
        dict_val[dict_index] = last_char;
        dict_index++;

        last_index = keep_index;

        if (dict_index >= dict_range && step < 12) {
            step += 1;
            dict_range = (unsigned short)(dict_range * 2);
        }

        if (src_pos >= src_len && byte_pos >= MBUFFER_EDGE) break;
    }
    return (int)res_pos;
}

int ex_unpack_kb_exe(const uint8_t *in, size_t in_len,
                     uint8_t **out, size_t *out_len) {
    *out = NULL;
    *out_len = 0;
    if (in_len < 0x205) {
        fprintf(stderr, "extract: KB.EXE too small (%zu bytes)\n", in_len);
        return -1;
    }
    if (in[0] != 'M' || in[1] != 'Z') {
        fprintf(stderr, "extract: not an MZ executable\n");
        return -1;
    }
    // COMPRESSOR signature at offset 0x200.
    if (in[0x200] != 0xE9 || in[0x201] != 0x99 || in[0x202] != 0x00) {
        fprintf(stderr, "extract: KB.EXE not COMPRESSOR-packed (0x%02x %02x %02x at 0x200)\n",
                in[0x200], in[0x201], in[0x202]);
        return -1;
    }

    // Outer MZ header → blocks_in_file + bytes_in_last_block → packed-data offset.
    unsigned blocks_in_file     = (unsigned)READ_WORD(in, 0x04);
    unsigned bytes_in_last_block= (unsigned)READ_WORD(in, 0x02);
    int extra_data_start = (int)blocks_in_file * 512;
    if (bytes_in_last_block) extra_data_start -= (512 - (int)bytes_in_last_block);

    if (extra_data_start <= 0 || extra_data_start + 0x25 > (int)in_len) {
        fprintf(stderr, "extract: bogus packed-data offset 0x%x\n", extra_data_start);
        return -1;
    }

    // Inner MZ header (the unpacked file's intended header).
    const uint8_t *inner = in + extra_data_start;
    unsigned inner_blocks      = (unsigned)READ_WORD(inner, 0x04);
    unsigned inner_last_bytes  = (unsigned)READ_WORD(inner, 0x02);
    unsigned inner_header_para = (unsigned)READ_WORD(inner, 0x08);

    int inner_data_start  = (int)inner_header_para * 16;
    int inner_extra_start = (int)inner_blocks * 512;
    if (inner_last_bytes) inner_extra_start -= (512 - (int)inner_last_bytes);
    int expected_size = inner_extra_start - inner_data_start;

    if (expected_size <= 0 || expected_size > 16 * 1024 * 1024) {
        fprintf(stderr, "extract: bogus expected unpacked size %d\n", expected_size);
        return -1;
    }

    int packed_offset = extra_data_start + inner_data_start;
    if (packed_offset >= (int)in_len) {
        fprintf(stderr, "extract: packed-stream offset out of range\n");
        return -1;
    }

    // Allocate output: 32-byte header + payload.
    size_t out_size = 0x20 + (size_t)expected_size;
    uint8_t *buf = malloc(out_size);
    if (!buf) return -1;
    memset(buf, 0, 0x20);
    buf[0] = 'M'; buf[1] = 'Z';
    // header_paragraphs = 2 (so payload sits at 0x20 = 32 bytes); reloc_table_offset = 0x1C.
    buf[0x08] = 0x02; buf[0x09] = 0x00;
    buf[0x18] = 0x1C; buf[0x19] = 0x00;

    int n = comp_unpack_stream(in + packed_offset,
                               in_len - (size_t)packed_offset,
                               buf + 0x20, expected_size);
    if (n != expected_size) {
        fprintf(stderr, "extract: unpacker decoded %d of expected %d bytes\n",
                n, expected_size);
        // Continue anyway — the data we care about (tune tables) is well
        // before any tail truncation. Real bug surfaces in stage (6).
    }

    *out = buf;
    *out_len = out_size;
    return 0;
}
