#ifndef NURL_POOL_H
#define NURL_POOL_H

#include "engine/nurl_engine_request.h"
#include "nurl_stream.h"
#include <time.h>
#include <stdbool.h>

#define NURL_POOL_MAX 8  /* max cached connections */

typedef struct {
    char         host[256];
    int          port;
    bool         is_tls;
    NutStream  *stream;
    bool         in_use;
    time_t       last_used;   /* for idle eviction */
} NutPoolEntry;

typedef struct NutConnPool {
    NutPoolEntry entries[NURL_POOL_MAX];
} NutConnPool;

NutConnPool  *nurl_pool_create(void);
void           nurl_pool_destroy(NutConnPool *pool);

/* Acquire a cached connection or open a new one.
 * Returns NURL_OK on success; *stream is populated. */
nurl_err_t     nurl_pool_acquire(NutConnPool *pool,
                   const char *host, int port, bool is_tls,
                   const NutRequest *req,
                   NutStream **stream);

/* Return a connection to the pool after a successful keep-alive request. */
void           nurl_pool_release(NutConnPool *pool,
                   const char *host, int port,
                   NutStream *stream);

/* Permanently close and evict a connection (on error or Connection: close). */
void           nurl_pool_evict(NutConnPool *pool, NutStream *stream);

#endif /* NURL_POOL_H */
