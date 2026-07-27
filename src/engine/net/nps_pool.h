#ifndef NPS_POOL_H
#define NPS_POOL_H

#include "engine/nps_engine_request.h"
#include "nps_stream.h"
#include <time.h>
#include <stdbool.h>

#define NPS_POOL_MAX 8  /* max cached connections */
#define NPS_POOL_IDLE_TTL 30 /* max idle time in seconds for a cached connection */

typedef struct {
    char         host[256];
    int          port;
    bool         is_tls;
    NpsStream  *stream;
    bool         in_use;
    time_t       last_used;   /* for idle eviction */
} NpsPoolEntry;

typedef struct NpsConnPool {
    NpsPoolEntry entries[NPS_POOL_MAX];
} NpsConnPool;

NpsConnPool  *nps_pool_create(void);
void           nps_pool_destroy(NpsConnPool *pool);

/* Acquire a cached connection or open a new one.
 * Returns NPS_OK on success; *stream is populated. */
nps_err_t     nps_pool_acquire(NpsConnPool *pool,
                   const char *host, int port, bool is_tls,
                   const NpsRequest *req,
                   NpsStream **stream);

/* Return a connection to the pool after a successful keep-alive request. */
void           nps_pool_release(NpsConnPool *pool,
                   const char *host, int port,
                   NpsStream *stream);

/* Permanently close and evict a connection (on error or Connection: close). */
void           nps_pool_evict(NpsConnPool *pool, NpsStream *stream);

#endif /* NPS_POOL_H */
