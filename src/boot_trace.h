// src/boot_trace.h
//
// Boot tracing, iOS and Android only. A mobile app has no console: its stdout
// is piped into the system log (ios/plat_ios.mm, the Android activity), and
// these are the only markers between launch and the game's first report of its
// own (the seed, at the title screen). Everywhere else this compiles to nothing.

#ifndef OB_BOOT_TRACE_H
#define OB_BOOT_TRACE_H

#include <stdio.h>

#if defined(PLATFORM_IOS) || defined(PLATFORM_ANDROID)
#define BOOT_TRACE(...) do { fprintf(stdout, __VA_ARGS__); fflush(stdout); } while (0)
#else
#define BOOT_TRACE(...) do { } while (0)
#endif

#endif
