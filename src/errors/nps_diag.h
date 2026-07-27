#ifndef NPS_DIAG_H
#define NPS_DIAG_H

/**
 * Emits a standardized error message to stderr in Unix style.
 * nps: error: <msg>
 */
void nps_diag_err(const char *fmt, ...);

/**
 * Emits a standardized hint message to stderr.
 *       hint: <msg>
 */
void nps_diag_hint(const char *fmt, ...);

/**
 * Emits a standardized warning message to stderr.
 * nps: warning: <msg>
 */
void nps_diag_warn(const char *fmt, ...);

/**
 * Safely emits an out-of-memory error using low-level I/O.
 */
void nps_diag_oom(void);

#endif /* NPS_DIAG_H */
