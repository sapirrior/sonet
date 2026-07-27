#ifndef NPS_H
#define NPS_H

#ifndef NPS_VERSION
#define NPS_VERSION "0.8.1"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    // Auth
    char *user;
    char *bearer;
    char *token;
    bool no_auth;

    // Body
    char *data;
    size_t data_len;
    bool json;

    // TLS
    bool no_verify;
    char *cacert;
    char *cert;
    char *key;
    bool tls12;
    bool tls13;

    // Cookies
    char *cookie;
    char *cookie_jar;
    char *session;

    // Request Control
    char *method;
    bool download;
    bool ping;
    bool resolve;
    bool dry_run;
    unsigned long timeout;
    unsigned long connect_timeout;
    bool location;
    unsigned int max_redirects;
    char *user_agent;
    char *referer;
    char **header;
    size_t header_count;
    bool compressed;
    bool http10;
    char *proxy;
    char *proxy_user;
    char *no_proxy;
    char *connect_to;
    unsigned int retry;
    unsigned long retry_delay;
    unsigned long limit_rate;

    // Output
    char *output;
    bool include;
    bool verbose;
    bool silent;
    char *write_out;
    char *dump_header;
    bool fail;
    bool fail_with_body;
    bool raw;

    // Ping specific
    unsigned int ping_count;
    unsigned long ping_interval;

    // Download & Upload specific
    bool resume;
    bool progress;
    char *upload_file;
    char *upload_name;
    char *upload_mime;
    char **upload_fields;
    size_t upload_fields_count;

    struct {
        uint64_t timeout : 1;
        uint64_t connect_timeout : 1;
        uint64_t location : 1;
        uint64_t user_agent : 1;
        uint64_t max_redirects : 1;
        uint64_t retry : 1;
        uint64_t retry_delay : 1;
        uint64_t limit_rate : 1;
        uint64_t proxy : 1;
    } is_set;
} CommonArgs;

#include "errors/nps_error.h"

#endif /* NPS_H */
