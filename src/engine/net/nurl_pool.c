#include "nurl_pool.h"
#include "nurl_net.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

NutConnPool *nurl_pool_create(void) {
    NutConnPool *pool = calloc(1, sizeof(NutConnPool));
    if (!pool) return NULL;
    return pool;
}

void nurl_pool_destroy(NutConnPool *pool) {
    if (!pool) return;
    for (int i = 0; i < NURL_POOL_MAX; i++) {
        if (pool->entries[i].stream) {
            NutStream *s = pool->entries[i].stream;
            if (s->tls) nurl_tls_free(s->tls);
            nurl_net_close(s->fd);
            nurl_stream_free(s);
        }
    }
    free(pool);
}

nurl_err_t nurl_pool_acquire(NutConnPool *pool, const char *host, int port, bool is_tls, const NutRequest *req, NutStream **stream) {
    if (!pool) return NURL_ERR_GENERIC;
    time_t now = time(NULL);

    // 1. Scan for existing warm connection
    for (int i = 0; i < NURL_POOL_MAX; i++) {
        NutPoolEntry *e = &pool->entries[i];
        if (e->stream && !e->in_use && e->port == port && e->is_tls == is_tls && strcmp(e->host, host) == 0) {
            // Idle eviction check (e.g. 60 seconds)
            if (now - e->last_used > 60) {
                if (req->verbose && !req->silent) {
                    fprintf(stderr, "* Connection pool: evicting idle connection to %s:%d (idle %lds)\n", e->host, e->port, (long)(now - e->last_used));
                }
                if (e->stream->tls) nurl_tls_free(e->stream->tls);
                nurl_net_close(e->stream->fd);
                nurl_stream_free(e->stream);
                e->stream = NULL;
            } else if (!nurl_net_is_alive(e->stream->fd)) {
                if (req->verbose && !req->silent) {
                    fprintf(stderr, "* Connection pool: pooled connection to %s:%d died, evicting\n", e->host, e->port);
                }
                if (e->stream->tls) nurl_tls_free(e->stream->tls);
                nurl_net_close(e->stream->fd);
                nurl_stream_free(e->stream);
                e->stream = NULL;
            } else {
                e->in_use = true;
                *stream = e->stream;
                if (req->verbose && !req->silent) {
                    fprintf(stderr, "* Connection pool: reusing warm connection to %s:%d\n", host, port);
                }
                return NURL_OK;
            }
        }
    }

    // 2. Find empty slot or evict oldest unused
    int slot = -1;
    time_t oldest_time = now;
    int oldest_slot = -1;

    for (int i = 0; i < NURL_POOL_MAX; i++) {
        NutPoolEntry *e = &pool->entries[i];
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
            NutPoolEntry *e = &pool->entries[oldest_slot];
            if (req->verbose && !req->silent) {
                fprintf(stderr, "* Connection pool: pool full, evicting oldest connection to %s:%d\n", e->host, e->port);
            }
            if (e->stream->tls) nurl_tls_free(e->stream->tls);
            nurl_net_close(e->stream->fd);
            nurl_stream_free(e->stream);
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
    nurl_err_t conn_err = NURL_OK;
    int fd = nurl_net_connect_proxy_ex(host, port, req->proxy, req->proxy_user, req->no_proxy, req->connect_to, req->connect_timeout_sec, &conn_err);
    if (fd < 0) {
        return conn_err;
    }
    if (req->read_timeout_sec > 0) {
        nurl_net_set_timeout(fd, req->read_timeout_sec);
    }

    nurl_tls_t *t = NULL;
    if (is_tls) {
        t = nurl_tls_create(req->tls_verify, req->cacert, req->cert, req->key, req->tls_version == 12, req->tls_version == 13);
        if (!t) {
            nurl_net_close(fd);
            return NURL_ERR_TLS;
        }

        if (nurl_tls_handshake(t, fd, host) != 0) {
            nurl_tls_free(t);
            nurl_net_close(fd);
            return NURL_ERR_TLS_HANDSHAKE;
        }
    }

    NutStream *s = nurl_stream_new(fd, t);
    if (!s) {
        if (t) nurl_tls_free(t);
        nurl_net_close(fd);
        return NURL_ERR_OOM;
    }

    *stream = s;

    // Save to slot if available
    if (slot >= 0) {
        NutPoolEntry *e = &pool->entries[slot];
        strncpy(e->host, host, sizeof(e->host) - 1);
        e->host[sizeof(e->host) - 1] = '\0';
        e->port = port;
        e->is_tls = is_tls;
        e->stream = s;
        e->in_use = true;
        e->last_used = now;
    }

    return NURL_OK;
}

void nurl_pool_release(NutConnPool *pool, const char *host, int port, NutStream *stream) {
    if (!pool) return;
    (void)host;
    (void)port;
    for (int i = 0; i < NURL_POOL_MAX; i++) {
        NutPoolEntry *e = &pool->entries[i];
        if (e->stream == stream) {
            e->in_use = false;
            e->last_used = time(NULL);
            return;
        }
    }
    // If connection was bypass, close it
    if (stream->tls) nurl_tls_free(stream->tls);
    nurl_net_close(stream->fd);
    nurl_stream_free(stream);
}

void nurl_pool_evict(NutConnPool *pool, NutStream *stream) {
    if (!pool || !stream) return;
    for (int i = 0; i < NURL_POOL_MAX; i++) {
        NutPoolEntry *e = &pool->entries[i];
        if (e->stream == stream) {
            if (e->stream->tls) nurl_tls_free(e->stream->tls);
            nurl_net_close(e->stream->fd);
            nurl_stream_free(e->stream);
            e->stream = NULL;
            e->in_use = false;
            return;
        }
    }
    // Bypass connection not in pool — close it directly
    if (stream->tls) nurl_tls_free(stream->tls);
    nurl_net_close(stream->fd);
    nurl_stream_free(stream);
}
