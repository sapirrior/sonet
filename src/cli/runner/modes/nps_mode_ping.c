#include "nps_dispatch.h"
#include "compat/nps_compat.h"
#include "net/nps_net.h"
#include "tls/nps_tls.h"
#include "net/nps_stream.h"
#include "utils/nps_utils.h"
#include "errors/nps_diag.h"
#include "engine/nps_engine.h"
#include "engine/nps_engine_request.h"
#include "engine/http/nps_http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

int nps_mode_ping(const char *url, const CommonArgs *common) {
    char *scheme = NULL, *host = NULL, *path = NULL;
    int port = 0;
    if (nps_utils_parse_url(url, &scheme, &host, &port, &path) != 0) return NPS_ERR_URL;

    unsigned int count = common->ping_count > 0 ? common->ping_count : 1;
    unsigned long interval = common->ping_interval > 0 ? common->ping_interval : 1000;

    nps_err_t err = NPS_OK;
    int fd = nps_net_connect_proxy_ex(host, port, common->proxy, common->proxy_user, common->no_proxy, common->connect_to, (unsigned int)common->connect_timeout, &err);
    if (fd < 0) { free(scheme); free(host); free(path); return NPS_ERR_CONNECT; }

    bool use_tls = (nps_strcasecmp(scheme, "https") == 0);
    nps_tls_t *t = NULL;
    if (use_tls) {
        t = nps_tls_create(!common->no_verify, common->cacert, common->cert, common->key, common->tls12, common->tls13);
        if (!t) { nps_net_close(fd); free(scheme); free(host); free(path); return NPS_ERR_TLS; }
        if (nps_tls_handshake(t, fd, host) != 0) { nps_tls_free(t); nps_net_close(fd); free(scheme); free(host); free(path); return NPS_ERR_TLS_HANDSHAKE; }
    }
    NpsStream *stream = nps_stream_new(fd, t);

    if (common->verbose && !common->silent) {
        if (use_tls) fprintf(stderr, "* Connected to %s:%d (TLS warm)\n", host, port);
        else fprintf(stderr, "* Connected to %s:%d (plaintext)\n", host, port);
    }

    unsigned long *latencies = malloc(sizeof(unsigned long) * count);
    for (unsigned int i = 0; i < count; i++) {
        double start = nps_utils_get_time_sec();
        nps_http_response_t *res = NULL;
        NpsHttpParams p = { .method = "HEAD", .path = path, .hostname = host, .extra_headers = "Connection: keep-alive\r\n" };
        err = nps_http_request(stream, &p, &res);

        if (err != NPS_OK) {
            nps_diag_err("ping: request %u failed (error %d)", i + 1, err);
            latencies[i] = 0;
        } else {
            unsigned long diff = (unsigned long)((nps_utils_get_time_sec() - start) * 1000.0);
            latencies[i] = diff;
            if (!common->silent) printf("ping %s: seq=%u status=%d time=%lu ms\n", host, i + 1, res->status_code, diff);
            nps_http_response_free(res);
        }
        if (i < count - 1) nps_sleep_ms(interval);
    }

    unsigned long min = ULONG_MAX, max = 0, sum = 0, success = 0;
    for (unsigned int i = 0; i < count; i++) {
        if (latencies[i] > 0) {
            if (latencies[i] < min) min = latencies[i];
            if (latencies[i] > max) max = latencies[i];
            sum += latencies[i]; success++;
        }
    }
    if (success > 0 && !common->silent) {
        printf("\n--- %s ping statistics ---\n%u requests, %u success, %u%% packet loss\nround-trip min/avg/max = %lu/%lu/%lu ms\n", host, count, (unsigned int)success, (unsigned int)((count - success) * 100 / count), min, sum / success, max);
    } else if (!common->silent) printf("\n--- %s ping statistics ---\n%u requests, 0 success, 100%% packet loss\n", host, count);

    free(latencies);
    if (t) nps_tls_free(t);
    nps_net_close(fd);
    nps_stream_free(stream);
    free(scheme); free(host); free(path);
    return 0;
}
