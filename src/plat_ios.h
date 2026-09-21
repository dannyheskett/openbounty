// src/plat_ios.h
//
// The iOS-only parts of startup, the exact counterpart of src/plat_android.h:
// an iOS app has no command line and no writable working directory, and its
// pack lives inside the .app bundle, so the two things every other platform
// gets from argv -- which pack to open and where saves go -- are resolved here.
//
// Everything here is a no-op stub off iOS, so call sites need no #ifdefs.

#ifndef OB_PLAT_IOS_SHELL_H
#define OB_PLAT_IOS_SHELL_H

#include "pack.h"

// The one pack an iOS build ships, as named inside the bundle.
#define IOS_PACK_RESOURCE "glory-of-rome.openbounty"

// Point the save root at the app's Documents directory. Call once, before any
// save slot is read. Off iOS: no-op.
void plat_ios_boot(void);

// Open the bundled pack. Returns NULL off iOS (and on failure), so the caller
// falls through to its normal resolve-and-open path.
Pack *plat_ios_open_pack(void);

#endif // OB_PLAT_IOS_SHELL_H
