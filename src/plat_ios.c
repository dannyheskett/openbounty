#include "plat_ios.h"

#if defined(PLATFORM_IOS)

#include "savepath.h"

#include <stdio.h>
#include <stdlib.h>

// Implemented in ios/plat_ios.mm, which is where Foundation lives. Both return
// malloc'd buffers the caller frees; NULL on failure.
extern char          *plat_ios_documents_dir(void);
extern unsigned char *plat_ios_bundle_file(const char *name, int *out_size);

void plat_ios_boot(void) {
    char *docs = plat_ios_documents_dir();
    if (!docs) return;
    // The app's Documents directory: backed up by iCloud, visible in the Files
    // app if the bundle ever declares it, and wiped when the app is deleted --
    // which is what an iOS player expects of save data.
    char dir[1024];
    snprintf(dir, sizeof dir, "%s/saves", docs);
    free(docs);
    SavePathSetDirOverride(dir);
}

Pack *plat_ios_open_pack(void) {
    int size = 0;
    unsigned char *bytes = plat_ios_bundle_file(IOS_PACK_RESOURCE, &size);
    if (!bytes || size <= 0) {
        fprintf(stdout, "ios: cannot read %s from the bundle\n", IOS_PACK_RESOURCE);
        free(bytes);
        return NULL;
    }
    // pack_open_mem copies every entry out, so the bundle blob is done with
    // here -- the same shape as the Android path.
    Pack *p = pack_open_mem(bytes, (size_t)size, IOS_PACK_RESOURCE);
    free(bytes);
    return p;
}

#else   // every other platform

void plat_ios_boot(void) {}

Pack *plat_ios_open_pack(void) { return NULL; }

#endif
