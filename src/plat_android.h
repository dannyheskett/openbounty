// src/plat_android.h
//
// The Android-only parts of startup. An Android app has no command line and
// no writable working directory, and its data lives inside the APK, so the two
// things every other platform gets from argv -- which pack to open and where
// saves go -- are resolved here instead.
//
// Everything in this header is a no-op stub off Android, so callers do not
// need their own #ifdefs around the call sites.

#ifndef OB_PLAT_ANDROID_H
#define OB_PLAT_ANDROID_H

#include "pack.h"

// The one pack an Android build ships, as named inside the APK's assets/.
#define ANDROID_PACK_ASSET "glory-of-rome.openbounty"

// Point the save root at the app's private storage. Call once, before any
// save slot is read. Off Android: no-op.
void plat_android_boot(void);

// Open the bundled pack out of the APK. Returns NULL off Android (and on
// failure), so the caller falls through to its normal resolve-and-open path.
Pack *plat_android_open_pack(void);

#endif // OB_PLAT_ANDROID_H
