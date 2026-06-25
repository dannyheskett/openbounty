// Tests for src/pack.c — open dir + zip, lookup, stack ordering.

#include "greatest.h"
#include "pack.h"
#include "miniz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// ---------------------------------------------------------------------------
// Loose directory mode: open assets/kings-bounty/ and read a known file.
// ---------------------------------------------------------------------------

TEST pack_open_dir_reads_known_entry(void) {
    Pack *p = pack_open("assets/kings-bounty");
    ASSERT(p != NULL);

    size_t sz = 0;
    const unsigned char *bytes = pack_read(p, "game.json", &sz);
    ASSERT(bytes != NULL);
    ASSERT(sz > 100);  // game.json is many KB; sanity check

    // Identity is parsed from game.json. May be empty; accept either way.
    ASSERT(pack_id(p) != NULL);

    pack_close(p);
    PASS();
}

TEST pack_lookup_missing_returns_null(void) {
    Pack *p = pack_open("assets/kings-bounty");
    ASSERT(p != NULL);
    size_t sz = 999;
    const unsigned char *bytes = pack_read(p, "no-such-entry.txt", &sz);
    ASSERT(bytes == NULL);
    ASSERT_EQ(0, (int)sz);
    pack_close(p);
    PASS();
}

// ---------------------------------------------------------------------------
// ZIP mode: build a tiny in-memory zip on the fly, write to a temp file,
// open via pack_open.
// ---------------------------------------------------------------------------

static const char *make_tiny_zip(void) {
    static char path[256];
    snprintf(path, sizeof path, "build/test-tiny.openbounty");
    mz_zip_archive z = {0};
    if (!mz_zip_writer_init_file(&z, path, 0)) return NULL;
    const char *gj =
        "{\"pack_id\":\"tiny\",\"pack_name\":\"Tiny\",\"pack_kind\":\"base\"}";
    if (!mz_zip_writer_add_mem(&z, "game.json", gj, strlen(gj),
                               MZ_DEFAULT_COMPRESSION)) return NULL;
    const char hello[] = "hello-from-zip";
    if (!mz_zip_writer_add_mem(&z, "art/hello.txt", hello, sizeof hello - 1,
                               MZ_DEFAULT_COMPRESSION)) return NULL;
    if (!mz_zip_writer_finalize_archive(&z)) return NULL;
    if (!mz_zip_writer_end(&z)) return NULL;
    return path;
}

TEST pack_open_zip_reads_entries_and_identity(void) {
    const char *zip_path = make_tiny_zip();
    ASSERT(zip_path != NULL);

    Pack *p = pack_open(zip_path);
    ASSERT(p != NULL);

    size_t sz = 0;
    const unsigned char *bytes = pack_read(p, "art/hello.txt", &sz);
    ASSERT(bytes != NULL);
    ASSERT_EQ(14, (int)sz);
    ASSERT(memcmp(bytes, "hello-from-zip", 14) == 0);

    ASSERT_STR_EQ("tiny", pack_id(p));
    ASSERT_STR_EQ("Tiny", pack_name(p));
    ASSERT_STR_EQ("base", pack_kind(p));

    pack_close(p);
    remove(zip_path);
    PASS();
}

// ---------------------------------------------------------------------------
// Stack: push two packs, top wins on overlap.
// ---------------------------------------------------------------------------

static const char *make_overlay_zip(void) {
    static char path[256];
    snprintf(path, sizeof path, "build/test-overlay.openbounty");
    mz_zip_archive z = {0};
    if (!mz_zip_writer_init_file(&z, path, 0)) return NULL;
    const char *gj = "{\"pack_id\":\"over\",\"pack_kind\":\"dlc\"}";
    if (!mz_zip_writer_add_mem(&z, "game.json", gj, strlen(gj),
                               MZ_DEFAULT_COMPRESSION)) return NULL;
    const char hello[] = "hello-from-overlay";
    if (!mz_zip_writer_add_mem(&z, "art/hello.txt", hello, sizeof hello - 1,
                               MZ_DEFAULT_COMPRESSION)) return NULL;
    if (!mz_zip_writer_finalize_archive(&z)) return NULL;
    if (!mz_zip_writer_end(&z)) return NULL;
    return path;
}

TEST pack_stack_top_wins(void) {
    const char *base = make_tiny_zip();
    const char *over = make_overlay_zip();
    ASSERT(base && over);

    pack_stack_clear();

    Pack *pb = pack_open(base);
    Pack *po = pack_open(over);
    ASSERT(pb && po);
    pack_stack_push(pb);
    pack_stack_push(po);

    size_t sz = 0;
    const unsigned char *bytes = pack_stack_read("art/hello.txt", &sz);
    ASSERT(bytes != NULL);
    ASSERT_EQ(18, (int)sz);
    ASSERT(memcmp(bytes, "hello-from-overlay", 18) == 0);

    // After popping the overlay, the base pack's bytes are exposed again.
    pack_stack_pop();   // closes po
    bytes = pack_stack_read("art/hello.txt", &sz);
    ASSERT(bytes != NULL);
    ASSERT_EQ(14, (int)sz);
    ASSERT(memcmp(bytes, "hello-from-zip", 14) == 0);

    pack_stack_clear();
    remove(base);
    remove(over);
    PASS();
}

SUITE(unit_pack_suite) {
    RUN_TEST(pack_open_dir_reads_known_entry);
    RUN_TEST(pack_lookup_missing_returns_null);
    RUN_TEST(pack_open_zip_reads_entries_and_identity);
    RUN_TEST(pack_stack_top_wins);
}
