// autoplay/report.c
//
// Decision log + objective checklist. Phase 2 scope.

#include "report.h"

#include <stdarg.h>
#include <stdio.h>

void report_init(Report *r) {
    if (!r) return;
    r->log_lines = 0;
}

void report_log(Report *r, const char *fmt, ...) {
    if (!r) return;
    r->log_lines++;
    va_list ap;
    va_start(ap, fmt);
    fputs("autoplay: ", stdout);
    vprintf(fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
}

void report_checklist(const Report *r, const PlanStepSet *set, const Game *g) {
    (void)r;
    if (!set) return;
    int done = 0, total = 0;
    planstepset_progress(set, g, &done, &total);
    printf("autoplay: objective checklist (%d/%d done)\n", done, total);
    for (int i = 0; i < set->count; i++) {
        const PlanStep *gl = &set->steps[i];
        bool d = planstep_is_done(gl, g);
        printf("autoplay:   [%c] %s\n", d ? 'x' : ' ', gl->label);
    }
}
