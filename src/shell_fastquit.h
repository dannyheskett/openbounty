// src/shell_fastquit.h
//
// Ctrl+Q "Quit without saving (y/n)" status-bar prompt. The string is
// rendered into the top status bar by chrome.c (which calls
// main_fast_quit_active() to gate the substitution), NOT a bottom
// dialog. While active, all other input is swallowed.
//
// Y -> set quit; N / ESC / cancel -> dismiss.

#ifndef OB_SHELL_FASTQUIT_H
#define OB_SHELL_FASTQUIT_H

#include <stdbool.h>

void fast_quit_open(void);
bool fast_quit_is_active(void);

// Per-frame pump. Returns true if a quit was confirmed (caller should
// break the main loop). When returning false, caller should still
// check fast_quit_is_active() to know whether to swallow other input.
bool fast_quit_tick(void);

#endif
