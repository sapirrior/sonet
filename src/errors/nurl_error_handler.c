#include "nurl_error_handler.h"
#include "nurl_diag.h"
#include "engine/tls/nurl_tls.h"
#include "engine/net/nurl_stream.h"
#include <stdio.h>
#include <string.h>

void nurl_handle_request_error(nurl_err_t err, const NutRequest *req, const char *target_url) {
    if (err == NURL_OK) return;

    switch (err) {
        case NURL_ERR_URL:
            nurl_diag_err("malformed URL '%s' provided.", target_url);
            nurl_diag_hint("ensure the URL uses a supported scheme like 'http://' or 'https://' and has a valid hostname.");
            break;

        case NURL_ERR_NETWORK:
            nurl_diag_err("network connection reset or interrupted during the request to '%s'.", target_url);
            if (req && req->out && req->out != stdout) {
                nurl_diag_hint("since you are downloading a file to disk, you can attempt to pick up where you left off by adding the --resume flag.");
            } else {
                nurl_diag_hint("check your internet connection or verify if the server is reachable.");
            }
            break;

        case NURL_ERR_RESOLVE:
            nurl_diag_err("could not resolve hostname '%s'.", target_url);
            nurl_diag_hint("check your spelling or DNS configuration.");
            break;

        case NURL_ERR_CONNECT:
            nurl_diag_err("failed to connect to host '%s'.", target_url);
            nurl_diag_hint("verify the host is up and reachable on the specified port.");
            break;

        case NURL_ERR_PROXY:
            nurl_diag_err("proxy handshake failed while connecting to '%s'.", target_url);
            nurl_diag_hint("verify your proxy settings and credentials.");
            break;

        case NURL_ERR_TLS:
        case NURL_ERR_TLS_HANDSHAKE: {
            const char *tls_err = (req && req->stream) ? nurl_tls_last_error(req->stream->tls) : NULL;
            if (!tls_err && req && req->last_tls_error[0] != '\0') {
                tls_err = req->last_tls_error;
            }
            if (tls_err) {
                nurl_diag_err("TLS failure for '%s': %s", target_url, tls_err);
            } else {
                nurl_diag_err("TLS handshake or certificate verification failed for '%s'.", target_url);
            }
            nurl_diag_hint("if you trust this host and want to bypass verification for local testing, use the -k or --no-verify flag.");
            break;
        }

        case NURL_ERR_OOM:
            nurl_diag_oom();
            break;

        case NURL_ERR_IO:
            nurl_diag_err("local I/O error occurred while reading or writing data for '%s'.", target_url);
            nurl_diag_hint("ensure you have proper read/write permissions for the target file paths.");
            break;

        case NURL_ERR_HTTP_4XX:
            nurl_diag_err("the server returned a 4xx Client Error for the request to '%s'.", target_url);
            nurl_diag_hint("this usually indicates a problem with the request parameters, authentication, or the resource path.");
            break;

        case NURL_ERR_HTTP_5XX:
            nurl_diag_err("the server returned a 5xx Server Error for the request to '%s'.", target_url);
            nurl_diag_hint("the remote server is currently experiencing issues. You might want to try again later.");
            break;

        default:
            nurl_diag_err("an unexpected error (code %d) occurred while requesting '%s'.", err, target_url);
            break;
    }
}
