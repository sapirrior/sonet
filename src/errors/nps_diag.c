#include "nps_diag.h"
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

#ifdef _WIN32
#include <io.h>
#endif

void nps_diag_err(const char *fmt, ...) {
    fprintf(stderr, "nps: error: ");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

void nps_diag_hint(const char *fmt, ...) {
    fprintf(stderr, "      hint: ");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

void nps_diag_warn(const char *fmt, ...) {
    fprintf(stderr, "nps: warning: ");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

static const char oom_msg[] = "nps: error: out of memory\n";

void nps_diag_oom(void) {
#ifdef _WIN32
    _write(2, oom_msg, sizeof(oom_msg) - 1);
#else
    write(STDERR_FILENO, oom_msg, sizeof(oom_msg) - 1);
#endif
}
