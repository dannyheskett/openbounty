// autoplay/diag.h
//
// The one diagnostics gate for every autoplay channel (AP-170). Set once from
// the --verbose CLI flag before any autoplay path runs; read through
// ob_diag_verbose() at each emission site. The project takes NOTHING from
// environment variables.

#ifndef OB_AUTOPLAY_DIAG_H
#define OB_AUTOPLAY_DIAG_H

#include <stdbool.h>

void ob_diag_set_verbose(bool on);
bool ob_diag_verbose(void);

// Quiet gate: silences the operational [AUTOPLAY]/[SEARCH]/[VERDICT] lines so
// a caller that renders its own report (--validate-pack) sees a clean stream.
// Off by default; --autoplay --headless keeps its normal output.
void ob_diag_set_quiet(bool on);
bool ob_diag_quiet(void);

#endif
