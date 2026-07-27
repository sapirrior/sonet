#include "nps_dispatch.h"
#include "utils/nps_utils.h"
#include "compat/nps_compat.h"
#include "errors/nps_diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int nps_mode_resolve(const char *url_or_host, const CommonArgs *common) {
    (void)common;
    char *scheme = NULL, *host = NULL, *path = NULL, *target_host = NULL;
    int port = 0;
    if (strstr(url_or_host, "://")) {
        if (nps_utils_parse_url(url_or_host, &scheme, &host, &port, &path) == 0) target_host = strdup(host);
    }
    if (!target_host) target_host = strdup(url_or_host);
    if (!target_host) { free(scheme); free(host); free(path); return 2; }

    struct addrinfo hints, *result, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(target_host, NULL, &hints, &result) != 0) {
        nps_diag_err("DNS resolution failed for '%s'.", target_host);
        nps_diag_hint("check your internet connection or verify the hostname is correct.");
        free(target_host); free(scheme); free(host); free(path); return 2;
    }

    bool found = false;
    char ip_str[INET6_ADDRSTRLEN];
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        found = true;
        void *addr;
        const char *record_type;
        if (rp->ai_family == AF_INET) {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)rp->ai_addr;
            addr = &(ipv4->sin_addr); record_type = "A";
        } else {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)rp->ai_addr;
            addr = &(ipv6->sin6_addr); record_type = "AAAA";
        }
        inet_ntop(rp->ai_family, addr, ip_str, sizeof(ip_str));
        if (!common->silent) printf("%s\t%s\t%s\n", target_host, ip_str, record_type);
    }
    freeaddrinfo(result); free(target_host); free(scheme); free(host); free(path);
    return found ? 0 : 2;
}
