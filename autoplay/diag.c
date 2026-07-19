// autoplay/diag.c -- the --verbose gate (AP-170).

#include "diag.h"

static bool s_verbose = false;
static bool s_quiet = false;

void ob_diag_set_verbose(bool on) { s_verbose = on; }
bool ob_diag_verbose(void) { return s_verbose; }

void ob_diag_set_quiet(bool on) { s_quiet = on; }
bool ob_diag_quiet(void) { return s_quiet; }
