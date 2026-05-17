// Surface a fatal error to the user via the most-visible mechanism
// available on each platform. Used by the bootstrap path (no pack
// found, pack corrupt, --pack arg unresolved) where the in-game
// dialog system isn't running yet.
//
// Always prints to stderr too — for terminal/CI runs that want the
// raw text, and as a fallback when the GUI helper fails.

#include "fatal.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
// Windows: pop a MessageBoxA from user32. The release build uses
// -mwindows so stderr is invisible; the message box is the only thing
// the user actually sees. We deliberately don't gate this on "is a
// console attached" — even if there's a console, the dialog is the
// right UX for a fatal startup error.
#  define WIN32_LEAN_AND_MEAN
#  define NOGDI
#  include <windows.h>
#endif

void fatal_user_error(const char *title, const char *body) {
    if (!title) title = "OpenBounty";
    if (!body)  body  = "Fatal error.";

    // Always log to stderr first.
    fprintf(stderr, "%s\n\n%s\n", title, body);

#ifdef _WIN32
    MessageBoxA(NULL, body, title, MB_OK | MB_ICONERROR);
#elif defined(__APPLE__)
    // osascript ships with every macOS install. Escape double-quotes
    // and backslashes so the AppleScript literal stays valid.
    char cmd[8192];
    char esc_title[1024];
    char esc_body[6144];
    size_t ti = 0, bi = 0;
    for (const char *p = title; *p && ti + 2 < sizeof esc_title; p++) {
        if (*p == '"' || *p == '\\') {
            if (ti + 3 >= sizeof esc_title) break;
            esc_title[ti++] = '\\';
        }
        esc_title[ti++] = *p;
    }
    esc_title[ti] = '\0';
    for (const char *p = body; *p && bi + 2 < sizeof esc_body; p++) {
        if (*p == '"' || *p == '\\') {
            if (bi + 3 >= sizeof esc_body) break;
            esc_body[bi++] = '\\';
        } else if (*p == '\n') {
            // AppleScript uses "return" (\r) inside string literals
            // for newline; the simplest portable rewrite is to inline
            // the linefeed escape.
            if (bi + 4 >= sizeof esc_body) break;
            esc_body[bi++] = '\\';
            esc_body[bi++] = 'n';
            continue;
        }
        esc_body[bi++] = *p;
    }
    esc_body[bi] = '\0';
    snprintf(cmd, sizeof cmd,
             "osascript -e 'display dialog \"%s\" "
             "with title \"%s\" buttons {\"OK\"} default button 1 "
             "with icon stop' >/dev/null 2>&1",
             esc_body, esc_title);
    int rc = system(cmd);
    (void)rc;
#else
    // Linux: try the common dialog tools in order. zenity (GNOME),
    // kdialog (KDE), then xmessage (every X11 install). Each is a
    // best-effort system() — failure is silent, stderr already had
    // the message.
    char cmd[8192];
    snprintf(cmd, sizeof cmd,
             "zenity --error --title=\"OpenBounty\" --text=\"%s\\n\\n%s\" >/dev/null 2>&1",
             title, body);
    if (system(cmd) == 0) return;
    snprintf(cmd, sizeof cmd,
             "kdialog --error \"%s\\n\\n%s\" --title \"OpenBounty\" >/dev/null 2>&1",
             title, body);
    if (system(cmd) == 0) return;
    snprintf(cmd, sizeof cmd,
             "xmessage -center \"%s\\n\\n%s\" >/dev/null 2>&1",
             title, body);
    int rc = system(cmd);
    (void)rc;
#endif
}
