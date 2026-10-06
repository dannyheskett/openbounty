// Where saves go (engine/savepath.c): flat under --save-dir, else
// <user-data>/saves/<pack_id>/save_<N>.dat, slots 0..SAVE_SLOT_COUNT-1.

#define _POSIX_C_SOURCE 200112L   // setenv, unsetenv under -std=c99

#include "greatest.h"
#include "savepath.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

TEST save_dir_override_is_flat(void) {
    system("rm -rf build/ob_savepath");
    SavePathSetDirOverride("build/ob_savepath");
    char path[512];
    bool ok = SavePathGetSlot("glory-of-rome", 3, path, sizeof path);
    SavePathSetDirOverride(NULL);
    ASSERT(ok);
    ASSERT(strstr(path, "build/ob_savepath") == path);
    ASSERT(strstr(path, "save_3.dat") != NULL);
    ASSERT(strstr(path, "glory-of-rome") == NULL);   // no per-pack folder
    ASSERT(is_dir("build/ob_savepath"));               // made on demand
    PASS();
}

TEST slots_outside_the_range_are_refused(void) {
    SavePathSetDirOverride("build/ob_savepath");
    char path[512];
    bool lo = SavePathGetSlot("p", -1, path, sizeof path);
    bool hi = SavePathGetSlot("p", SAVE_SLOT_COUNT, path, sizeof path);
    bool last = SavePathGetSlot("p", SAVE_SLOT_COUNT - 1, path, sizeof path);
    SavePathSetDirOverride(NULL);
    ASSERT_FALSE(lo);
    ASSERT_FALSE(hi);
    ASSERT(last);
    PASS();
}

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
TEST the_default_layout_is_per_pack(void) {
    system("rm -rf build/ob_xdg && mkdir -p build/ob_xdg");
    const char *old = getenv("XDG_DATA_HOME");
    char saved[512] = "";
    if (old) snprintf(saved, sizeof saved, "%s", old);
    setenv("XDG_DATA_HOME", "build/ob_xdg", 1);
    char path[512];
    bool ok = SavePathGetSlot("kings-bounty", 0, path, sizeof path);
    if (old) setenv("XDG_DATA_HOME", saved, 1); else unsetenv("XDG_DATA_HOME");
    ASSERT(ok);
    ASSERT_STR_EQ("build/ob_xdg/openbounty/saves/kings-bounty/save_0.dat", path);
    ASSERT(is_dir("build/ob_xdg/openbounty/saves/kings-bounty"));
    PASS();
}
#endif

SUITE(unit_savepath_suite) {
    RUN_TEST(save_dir_override_is_flat);
    RUN_TEST(slots_outside_the_range_are_refused);
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    RUN_TEST(the_default_layout_is_per_pack);
#endif
}
