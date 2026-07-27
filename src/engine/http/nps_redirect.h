#ifndef NPS_REDIRECT_H
#define NPS_REDIRECT_H

/**
 * Resolves a redirect Location header against the current target URL.
 * Supports absolute URLs, root-relative paths, and relative paths.
 *
 * @param current_url The current URL of the request.
 * @param location The Location header value received from the server.
 * @return Dynamically allocated resolved absolute URL, or NULL on failure.
 *         The caller is responsible for freeing the returned pointer.
 */
char *nps_resolve_redirect(const char *current_url, const char *location);

#endif /* NPS_REDIRECT_H */
