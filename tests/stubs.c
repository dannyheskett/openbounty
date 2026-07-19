// Link stand-ins for the test binary, which supplies its own main() and so
// cannot link src/main.c.
//
// No stubs are currently needed: every symbol the shell shares lives in its
// own shell_*.{c,h} translation unit and links directly. The file is kept as
// the place such a stub would go. The declaration below keeps this a valid,
// warning-free translation unit.
extern int _ob_stubs_placeholder;
int _ob_stubs_placeholder = 0;
