// engine/combat_log.c -- combat log line append.
//
// Pure Combat-struct mutation. Engine-side: called from damage/AI/spell
// code. Renderer reads c->log_lines and c->banner to paint the bottom
// strip.

#include "combat.h"
#include "resources.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void combat_log(Combat *c, const char *fmt, ...) {
    if (!c) return;
    char line[COMBAT_LOG_LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (c->log_count < COMBAT_LOG_LINES) {
        snprintf(c->log_lines[c->log_count], COMBAT_LOG_LINE_LEN, "%s", line);
        c->log_count++;
    } else {
        for (int i = 1; i < COMBAT_LOG_LINES; i++) {
            memcpy(c->log_lines[i - 1], c->log_lines[i], COMBAT_LOG_LINE_LEN);
        }
        snprintf(c->log_lines[COMBAT_LOG_LINES - 1], COMBAT_LOG_LINE_LEN,
                 "%s", line);
    }
    snprintf(c->banner, COMBAT_BANNER_LEN, "%s", line);
}

void combat_log_template(Combat *c, const char *template_str,
                         const ResTemplateVar *vars, int nvars) {
    if (!c || !template_str || !template_str[0]) return;
    char line[COMBAT_LOG_LINE_LEN];
    resources_format_template(line, sizeof line, template_str, vars, nvars);
    if (c->log_count < COMBAT_LOG_LINES) {
        snprintf(c->log_lines[c->log_count], COMBAT_LOG_LINE_LEN, "%s", line);
        c->log_count++;
    } else {
        for (int i = 1; i < COMBAT_LOG_LINES; i++) {
            memcpy(c->log_lines[i - 1], c->log_lines[i], COMBAT_LOG_LINE_LEN);
        }
        snprintf(c->log_lines[COMBAT_LOG_LINES - 1], COMBAT_LOG_LINE_LEN,
                 "%s", line);
    }
    snprintf(c->banner, COMBAT_BANNER_LEN, "%s", line);
}
