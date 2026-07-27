#ifndef NPS_NET_H
#define NPS_NET_H

#include "errors/nps_error.h"
#include <stdbool.h>

/**
 * Advanced connect with detailed error reporting.
 */
int nps_net_connect_ex(const char *hostname, int port, unsigned int connect_timeout_sec, nps_err_t *out_err);

/**
 * Advanced proxy connect with detailed error reporting.
 */
int nps_net_connect_proxy_ex(const char *host, int port, const char *proxy, const char *proxy_user, const char *no_proxy, const char *connect_to, unsigned int connect_timeout_sec, nps_err_t *out_err);


/**
 * Sets send and receive timeouts on the socket descriptor.
 * Returns 0 on success, or -1 on failure.
 */
int nps_net_set_timeout(int socket_fd, unsigned long seconds);

/**
 * Checks if a socket connection is still alive and healthy.
 * Returns true if alive, false if closed or errored.
 */
bool nps_net_is_alive(int socket_fd);

/**
 * Closes the given socket file descriptor.
 */
void nps_net_close(int socket_fd);

/**
 * Initializes platform socket subsystems (e.g. Winsock WSAStartup).
 * Returns 0 on success, or non-zero on failure.
 */
int nps_net_init(void);

/**
 * Cleans up platform socket subsystems (e.g. Winsock WSACleanup).
 */
void nps_net_cleanup(void);

#endif /* NPS_NET_H */
