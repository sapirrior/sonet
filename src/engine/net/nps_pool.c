#include "nps_pool.h"
#include "nps_net.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

NpsConnPool *nps_pool_create(void) {
    NpsConnPool *pool = calloc(1, sizeof(NpsConnPool));
    if (!pool) return NULL;
    return pool;
}

void nps_pool_destroy(NpsConnPool *pool) {
    if (!pool) return;
    for (int i = 0; i < NPS_POOL_MAX; i++) {
        if (pool->entries[i].stream) {
            NpsStream *s = pool->entries[i].stream;
            if (s->tls) nps_tls_free(s->tls);
            nps_net_close(s->fd);
            nps_stream_free(s);
        }
    }
    free(pool);
}

nps_err_t nps_pool_acquire(NpsConnPool *pool, const char *host, int port, bool is_tls, const NpsRequest *req, NpsStream **stream) {
    if (!pool) return NPS_ERR_GENERIC;
    time_t now = time(NULL);

    // 1. Scan for existing warm connection
    for (int i = 0; i < NPS_POOL_MAX; i++) {
        NpsPoolEntry *e = &pool->entries[i];
        if (e->stream && !e->in_use && e->port == port && e->is_tls == is_tls && strcmp(e->host, host) == 0) {
            // Idle eviction check (e.g. 60 seconds)
            if (now - e->last_used > NPS_POOL_IDLE_TTL) {
                if (req->verbose && !req->silent) {
                    fprintf(stderr, "* Connection pool: evicting idle connection to %s:%d (idle %lds)\n", e->host, e->port, (long)(now - e->last_used));
                }
                if (e->stream->tls) nps_tls_free(e->stream->tls);
                nps_net_close(e->stream->fd);
                nps_stream_free(e->stream);
                e->stream = NULL;
            } else if (!nps_net_is_alive(e->stream->fd)) {
                if (req->verbose && !req->silent) {
                    fprintf(stderr, "* Connection pool: pooled connection to %s:%d died, evicting\n", e->host, e->port);
                }
                if (e->stream->tls) nps_tls_free(e->stream->tls);
                nps_net_close(e->stream->fd);
                nps_stream_free(e->stream);
                e->stream = NULL;
            } else {
                e->in_use = true;
                *stream = e->stream;
                if (req->verbose && !req->silent) {
                    fprintf(stderr, "* Connection pool: reusing warm connection to %s:%d\n", host, port);
                }
                return NPS_OK;
            }
        }
    }

    // 2. Find empty slot or evict oldest unused
    int slot = -1;
    time_t oldest_time = now;
    int oldest_slot = -1;

    for (int i = 0; i < NPS_POOL_MAX; i++) {
        NpsPoolEntry *e = &pool->entries[i];
        if (!e->stream) {
            slot = i;
            break;
        }
        if (!e->in_use && e->last_used < oldest_time) {
            oldest_time = e->last_used;
            oldest_slot = i;
        }
    }

    if (slot < 0) {
        if (oldest_slot >= 0) {
            NpsPoolEntry *e = &pool->entries[oldest_slot];
            if (req->verbose && !req->silent) {
                fprintf(stderr, "* Connection pool: pool full, evicting oldest connection to %s:%d\n", e->host, e->port);
            }
            if (e->stream->tls) nps_tls_free(e->stream->tls);
            nps_net_close(e->stream->fd);
            nps_stream_free(e->stream);
            e->stream = NULL;
            slot = oldest_slot;
        } else {
            // All slots in use
            if (req->verbose && !req->silent) {
                fprintf(stderr, "* Connection pool: all connections in use, creating bypass connection\n");
            }
        }
    }

    // 3. Connect & handshake
    nps_err_t conn_err = NPS_OK;
    int fd = nps_net_connect_proxy_ex(host, port, req->proxy, req->proxy_user, req->no_proxy, req->connect_to, req->connect_timeout_sec, &conn_err);
    if (fd < 0) {
        return conn_err;
    }
    if (req->read_timeout_sec > 0) {
        nps_net_set_timeout(fd, req->read_timeout_sec);
    }

    nps_tls_t *t = NULL;
    if (is_tls) {
        t = nps_tls_create(req->tls_verify, req->cacert, req->cert, req->key, req->tls_version == 12, req->tls_version == 13);
        if (!t) {
            nps_net_close(fd);
            return NPS_ERR_TLS;
        }

        if (nps_tls_handshake(t, fd, host) != 0) {
            nps_tls_free(t);
            nps_net_close(fd);
            return NPS_ERR_TLS_HANDSHAKE;
        }
    }

    NpsStream *s = nps_stream_new(fd, t);
    if (!s) {
        if (t) nps_tls_free(t);
        nps_net_close(fd);
        return NPS_ERR_OOM;
    }

    *stream = s;

    // Save to slot if available
    if (slot >= 0) {
        NpsPoolEntry *e = &pool->entries[slot];
        strncpy(e->host, host, sizeof(e->host) - 1);
        e->host[sizeof(e->host) - 1] = '\0';
        e->port = port;
        e->is_tls = is_tls;
        e->stream = s;
        e->in_use = true;
        e->last_used = now;
    }

    return NPS_OK;
}

void nps_pool_release(NpsConnPool *pool, const char *host, int port, NpsStream *stream) {
    if (!pool) return;
    (void)host;
    (void)port;
    for (int i = 0; i < NPS_POOL_MAX; i++) {
        NpsPoolEntry *e = &pool->entries[i];
        if (e->stream == stream) {
            e->in_use = false;
            e->last_used = time(NULL);
            return;
        }
    }
    // If connection was bypass, close it
    if (stream->tls) nps_tls_free(stream->tls);
    nps_net_close(stream->fd);
    nps_stream_free(stream);
}

void nps_pool_evict(NpsConnPool *pool, NpsStream *stream) {
    if (!pool || !stream) return;
    for (int i = 0; i < NPS_POOL_MAX; i++) {
        NpsPoolEntry *e = &pool->entries[i];
        if (e->stream == stream) {
            if (e->stream->tls) nps_tls_free(e->stream->tls);
            nps_net_close(e->stream->fd);
            nps_stream_free(e->stream);
            e->stream = NULL;
            e->in_use = false;
            return;
        }
    }
    // Bypass connection not in pool — close it directly
    if (stream->tls) nps_tls_free(stream->tls);
    nps_net_close(stream->fd);
    nps_stream_free(stream);
}
