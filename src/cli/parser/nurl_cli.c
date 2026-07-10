#include "nurl_cli.h"
#include "errors/nurl_error.h"
#include "errors/nurl_diag.h"
#include "compat/nurl_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h>

void nurl_cli_init_args(CommonArgs *args) {
    memset(args, 0, sizeof(CommonArgs));
    args->timeout = 30;
    args->connect_timeout = 10;
    args->ping_count = 1;
    args->ping_interval = 1000;
}

static bool has_scheme(const char *url) {
    const char *p = url;
    while (*p && (isalpha((unsigned char)*p) || *p == '+' || *p == '-' || *p == '.')) p++;
    return (*p == ':' && *(p+1) == '/' && *(p+2) == '/');
}

static char *nurl_normalize_url(const char *raw) {
    if (!raw) return NULL;
    if (has_scheme(raw)) return strdup(raw);
    size_t len = strlen(raw);
    char *normalized = malloc(len + 9);
    if (!normalized) return NULL;
    snprintf(normalized, len + 9, "https://%s", raw);
    return normalized;
}

static void nurl_cli_infer_method(CommonArgs *args) {
    if (args->method) return;
    if (args->upload_file) args->method = strdup("POST");
    else if (args->data || args->json) args->method = strdup("POST");
    else args->method = strdup("GET");
}

static int set_arg_str(char **dest, const char *val, const char *name) {
    if (val[0] == '\0') { nurl_diag_err("--%s value cannot be empty.", name); return -1; }
    if (*dest) free(*dest);
    if (!(*dest = strdup(val))) { nurl_diag_err("out of memory while processing --%s.", name); return -1; }
    return 0;
}

static int append_arg_str(char ***array, size_t *count, const char *val, const char *name) {
    char **temp = realloc(*array, sizeof(char *) * (*count + 1));
    if (!temp || !(temp[*count] = strdup(val))) { nurl_diag_err("out of memory while processing --%s.", name); if (temp) *array = temp; return -1; }
    *array = temp; (*count)++; return 0;
}

static int parse_non_negative_int(const char *optarg, const char *name) {
    char *end;
    unsigned long v = strtoul(optarg, &end, 10);
    if (*end != '\0' || v > 2147483647) {
        nurl_diag_err("--%s must be a valid non-negative integer.", name);
        return -1;
    }
    return (int)v;
}

int nurl_cli_parse(int argc, char **argv, CommonArgs *args, char **url) {
    nurl_cli_init_args(args);
    static struct option long_options[] = {
        {"user", 1, 0, 'u'}, {"bearer", 1, 0, 1}, {"token", 1, 0, 2}, {"no-auth", 0, 0, 3},
        {"data", 1, 0, 'd'}, {"json", 0, 0, 'j'}, {"insecure", 0, 0, 'k'}, {"cacert", 1, 0, 4},
        {"timeout", 1, 0, 't'}, {"follow", 0, 0, 'L'}, {"header", 1, 0, 'H'}, {"output", 1, 0, 'o'},
        {"include", 0, 0, 'i'}, {"verbose", 0, 0, 'v'}, {"silent", 0, 0, 's'}, {"raw", 0, 0, 5},
        {"count", 1, 0, 6}, {"interval", 1, 0, 7}, {"resume", 0, 0, 8}, {"progress", 0, 0, 9},
        {"mime", 1, 0, 10}, {"name", 1, 0, 11}, {"field", 1, 0, 12}, {"cookie", 1, 0, 'b'},
        {"cookie-jar", 1, 0, 'c'}, {"session", 1, 0, 13}, {"format", 1, 0, 'w'}, {"version", 0, 0, 'V'},
        {"help", 0, 0, 'h'}, {"cert", 1, 0, 14}, {"key", 1, 0, 15}, {"proxy", 1, 0, 'x'},
        {"proxy-user", 1, 0, 16}, {"no-proxy", 1, 0, 17}, {"user-agent", 1, 0, 'A'}, {"gzip", 0, 0, 18},
        {"retry", 1, 0, 19}, {"retry-delay", 1, 0, 20}, {"referer", 1, 0, 'e'}, {"fail", 0, 0, 'f'},
        {"tls1.2", 0, 0, 21}, {"tls1.3", 0, 0, 22}, {"method", 1, 0, 'X'}, {"upload", 1, 0, 23},
        {"connect-timeout", 1, 0, 24}, {"download", 0, 0, 'D'}, {"ping", 0, 0, 25}, {"resolve", 0, 0, 26},
        {"max-redirects", 1, 0, 28}, {"max-redirs", 1, 0, 28}, {"fail-with-body", 0, 0, 29}, {"head", 0, 0, 'I'}, {"http1.0", 0, 0, 30}, {"dump-header", 1, 0, 31}, {"connect-to", 1, 0, 32}, {"limit-rate", 1, 0, 33}, {"dry-run", 0, 0, 27}, {0, 0, 0, 0}
        };

    int opt; opterr = 0;
    while ((opt = getopt_long(argc, argv, "u:d:jt:LH:o:ivshkb:c:w:Vx:A:e:fX:DI", long_options, NULL)) != -1) {
        switch (opt) {
            case 'u': if (set_arg_str(&args->user, optarg, "user")) return -1; break;
            case 1:   if (set_arg_str(&args->bearer, optarg, "bearer")) return -1; break;
            case 2:   if (set_arg_str(&args->token, optarg, "token")) return -1; break;
            case 3:   args->no_auth = true; break;
            case 'd': if (set_arg_str(&args->data, optarg, "data")) return -1; args->data_len = strlen(optarg); break;
            case 'j': args->json = true; break;
            case 'k': args->no_verify = true; break;
            case 4:   if (set_arg_str(&args->cacert, optarg, "cacert")) return -1; break;
            case 't': { int v = parse_non_negative_int(optarg, "timeout"); if (v < 0) return -1; args->timeout = (unsigned long)v; args->is_set.timeout = 1; break; }
            case 'L': args->location = true; args->is_set.location = 1; break;
            case 'H': {
                const char *colon = strchr(optarg, ':');
                if (!colon) { nurl_diag_err("invalid header '%s'", optarg); return -1; }
                size_t key_len = colon - optarg;
                if (key_len == 0) { nurl_diag_err("header key cannot be empty."); return -1; }
                for (size_t i = 0; i < key_len; i++) {
                    if (isspace((unsigned char)optarg[i]) || iscntrl((unsigned char)optarg[i])) { nurl_diag_err("header key contains invalid characters."); return -1; }
                }
                if (append_arg_str(&args->header, &args->header_count, optarg, "header")) return -1; break;
            }
            case 'o': if (set_arg_str(&args->output, optarg, "output")) return -1; break;
            case 'i': args->include = true; break;
            case 'v': args->verbose = true; break;
            case 's': args->silent = true; break;
            case 5:   args->raw = true; break;
            case 6:   { int v = parse_non_negative_int(optarg, "count"); if (v < 0) return -1; args->ping_count = (unsigned int)v; break; }
            case 7:   { int v = parse_non_negative_int(optarg, "interval"); if (v < 0) return -1; args->ping_interval = (unsigned long)v; break; }
            case 8:   args->resume = true; break;
            case 9:   args->progress = true; break;
            case 10:  if (set_arg_str(&args->upload_mime, optarg, "mime")) return -1; break;
            case 11:  if (set_arg_str(&args->upload_name, optarg, "name")) return -1; break;
            case 12:  if (append_arg_str(&args->upload_fields, &args->upload_fields_count, optarg, "field")) return -1; break;
            case 'b': if (set_arg_str(&args->cookie, optarg, "cookie")) return -1; break;
            case 'c': if (set_arg_str(&args->cookie_jar, optarg, "cookie-jar")) return -1; break;
            case 13:  if (set_arg_str(&args->session, optarg, "session")) return -1; break;
            case 'w': if (set_arg_str(&args->write_out, optarg, "format")) return -1; break;
            case 14:  if (set_arg_str(&args->cert, optarg, "cert")) return -1; break;
            case 15:  if (set_arg_str(&args->key, optarg, "key")) return -1; break;
            case 'x': if (set_arg_str(&args->proxy, optarg, "proxy")) return -1; break;
            case 16:  if (set_arg_str(&args->proxy_user, optarg, "proxy-user")) return -1; break;
            case 17:  if (set_arg_str(&args->no_proxy, optarg, "no-proxy")) return -1; break;
            case 'A': if (set_arg_str(&args->user_agent, optarg, "user-agent")) return -1; args->is_set.user_agent = 1; break;
            case 18:  args->compressed = true; break;
            case 19:  { int v = parse_non_negative_int(optarg, "retry"); if (v < 0) return -1; args->retry = (unsigned int)v; break; }
            case 20:  { int v = parse_non_negative_int(optarg, "retry-delay"); if (v < 0) return -1; args->retry_delay = (unsigned long)v; break; }
            case 'e': if (set_arg_str(&args->referer, optarg, "referer")) return -1; break;
            case 'f': args->fail = true; break;
            case 21:  args->tls12 = true; break;
            case 22:  args->tls13 = true; break;
            case 'X': if (set_arg_str(&args->method, optarg, "method")) return -1; break;
            case 23:  if (set_arg_str(&args->upload_file, optarg, "upload")) return -1; break;
            case 24:  { int v = parse_non_negative_int(optarg, "connect-timeout"); if (v < 0) return -1; args->connect_timeout = (unsigned long)v; args->is_set.connect_timeout = 1; break; }
            case 'D': args->download = true; break;
            case 25:  args->ping = true; break;
            case 26:  args->resolve = true; break;
            case 27:  args->dry_run = true; break;
            case 28:  { int v = parse_non_negative_int(optarg, "max-redirects"); if (v < 0) return -1; args->max_redirects = (unsigned int)v; args->is_set.max_redirects = 1; break; }
            case 29:  args->fail_with_body = true; break;
            case 30:  args->http10 = true; break;
            case 31:  if (set_arg_str(&args->dump_header, optarg, "dump-header")) return -1; break;
            case 32:  if (set_arg_str(&args->connect_to, optarg, "connect-to")) return -1; break;
            case 33:  {
                char *end;
                unsigned long v = strtoul(optarg, &end, 10);
                if (*end == 'k' || *end == 'K') { v *= 1024; }
                else if (*end == 'm' || *end == 'M') { v *= 1024 * 1024; }
                args->limit_rate = v;
                break;
            }
            case 'I': if (args->method) free(args->method); args->method = strdup("HEAD"); args->include = true; break;
            case 'V': printf("nurl %s\n", NURL_VERSION); exit(0);
            case 'h': return -1; // -1 indicates explicit help request
            default:  if (optind > 0 && optind <= argc) {
                          nurl_diag_err("option '%s' unrecognized or invalid.", argv[optind - 1]);
                      } else {
                          nurl_diag_err("option unrecognized or invalid.");
                      }
                      nurl_diag_hint("run 'nurl --help' for usage."); return NURL_ERR_ARG;
        }
    }
 
    if (args->tls12 && args->tls13) { nurl_diag_err("conflicting TLS versions"); return NURL_ERR_ARG; }
    if (args->ping && args->dry_run) { nurl_diag_err("--ping and --dry-run are mutually exclusive"); return NURL_ERR_ARG; }
    if (args->ping && args->download) { nurl_diag_err("--ping and --download are mutually exclusive"); return NURL_ERR_ARG; }
    if (args->resolve && args->download) { nurl_diag_err("--resolve and --download are mutually exclusive"); return NURL_ERR_ARG; }
    if (args->no_verify && args->cacert) { nurl_diag_warn("--insecure and --cacert used together; --cacert takes precedence."); }
    if (args->resume && !args->download && !args->output) { nurl_diag_err("--resume requires --download or -o"); return NURL_ERR_ARG; }
 
    int rem = argc - optind;
    if (rem <= 0) { nurl_diag_err("no URL specified!"); return NURL_ERR_ARG; }
    if (rem > 1) { nurl_diag_err("only a single URL target is supported. Multiple targets specified."); return NURL_ERR_ARG; }
 
    *url = nurl_normalize_url(argv[optind]);
    nurl_cli_infer_method(args);

    if (args->data && (nurl_strcasecmp(args->method, "GET") == 0 || nurl_strcasecmp(args->method, "HEAD") == 0)) {
        nurl_diag_warn("sending body with %s is non-standard.", args->method);
    }
    if (nurl_strcasecmp(args->method, "HEAD") == 0) args->include = true;

    return 0;
}

void nurl_cli_free_args(CommonArgs *args) {
    if (!args) return;
    free(args->method); free(args->user); free(args->bearer); free(args->token);
    free(args->data); free(args->cacert); free(args->output); free(args->upload_file);
    free(args->upload_name); free(args->upload_mime); free(args->cookie);
    free(args->cookie_jar); free(args->session); free(args->write_out); free(args->dump_header);
    free(args->cert); free(args->key); free(args->proxy); free(args->proxy_user); free(args->connect_to);
    free(args->no_proxy); free(args->user_agent); free(args->referer);
    if (args->upload_fields) {
        for (size_t i = 0; i < args->upload_fields_count; i++) free(args->upload_fields[i]);
        free(args->upload_fields);
    }
    if (args->header) {
        for (size_t i = 0; i < args->header_count; i++) free(args->header[i]);
        free(args->header);
    }
    memset(args, 0, sizeof(CommonArgs));
}
