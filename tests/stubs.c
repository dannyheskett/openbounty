// Stubs for symbols that live in src/main.c. Test binary excludes
// main.c (it has its own main), so the few symbols other game code
// references from main need stand-ins here.
//
// Currently empty: every main.c symbol the shell needs has been
// extracted to its own shell_*.{c,h} file and is linked into the
// test binary directly.

// This translation unit is intentionally near-empty. A single extern
// declaration keeps it as a valid TU without producing an unused-var
// warning.
extern int _ob_stubs_placeholder;
int _ob_stubs_placeholder = 0;
