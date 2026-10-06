// src/cli_flags.h
//
// The command-line flags, listed once. `--help` prints this table, and a test
// (tests/unit/test_cli_flags.c) checks that README §3's flag table and
// OPENBOUNTY-SPEC REQ-480 name exactly these flags. The parser in src/main.c
// handles each one.

#ifndef OB_CLI_FLAGS_H
#define OB_CLI_FLAGS_H

#include <stdio.h>

typedef struct {
    const char *flag;    // "--seed"
    const char *alias;   // "-v", or NULL
    const char *arg;     // "<0-255>", or NULL for none
    const char *help;    // one line for --help
} CliFlag;

extern const CliFlag cli_flags[];
extern const int     cli_flag_count;

// Print the build line and every flag, one to a line, to `out`.
void cli_flags_print_help(FILE *out, const char *version);

#endif
