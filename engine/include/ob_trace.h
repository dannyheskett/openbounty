// engine/include/ob_trace.h
//
// TEMPORARY observational trace (AP-046: purely observational, never alters
// decisions/state). Compile-time gated behind OB_TRACE_PRES so it is a no-op in
// every normal build. Used to PROVE the visible-autoplay presentation sequence
// (which REQ_VIEW/REQ_MESSAGE get acked, by whom, and when) before fixing it.
//
// Remove (or leave dormant) once the presentation defect is fixed and proven.
#ifndef OB_TRACE_H
#define OB_TRACE_H

#ifdef OB_TRACE_PRES
#include <stdio.h>
#define OB_TRACE(...)                                  \
    do {                                               \
        fprintf(stdout, "OBTRACE " __VA_ARGS__);       \
        fprintf(stdout, "\n");                         \
        fflush(stdout);                                \
    } while (0)
#else
#define OB_TRACE(...) ((void)0)
#endif

#endif // OB_TRACE_H
