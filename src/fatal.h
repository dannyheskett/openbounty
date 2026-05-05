// Platform-aware fatal-error reporter for the bootstrap path, where the
// game window / dialog system isn't up yet. On Windows GUI builds this
// is the only way the user ever learns why the .exe quit silently.
//
// Usage:
//     fatal_user_error("OpenBounty cannot start",
//                      "No game pack found.\n\n"
//                      "...");
//     return 1;
//
// The function returns; it is the caller's job to exit. This way the
// caller controls the exit code and any cleanup.

#ifndef OPENBOUNTY_FATAL_H
#define OPENBOUNTY_FATAL_H

void fatal_user_error(const char *title, const char *body);

#endif
