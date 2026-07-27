#include "nps_tls.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <stdlib.h>

struct nps_tls {
    SSL_CTX *ctx;
    SSL *ssl;
    bool verify;
    char negotiated_proto[32];
    char last_error[256];
};

nps_tls_t *nps_tls_create(bool verify_certs, const char *cacert_path, const char *cert_path, const char *key_path, bool force_tls12, bool force_tls13) {
    // Initialize OpenSSL library
    static bool openssl_initialized = false;
    if (!openssl_initialized) {
        OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
        openssl_initialized = true;
    }

    nps_tls_t *tls = malloc(sizeof(nps_tls_t));
    if (!tls) {
        return NULL;
    }
    tls->ssl = NULL;
    tls->verify = verify_certs;
    tls->negotiated_proto[0] = '\0';
    tls->last_error[0] = '\0';

    // Use modern TLS client method
    const SSL_METHOD *method = TLS_client_method();
    tls->ctx = SSL_CTX_new(method);
    if (!tls->ctx) {
        free(tls);
        return NULL;
    }

    // Offer HTTP/1.1 (http/1.1) protocol
    static const unsigned char alpn_protos[] = "\x08http/1.1";
    SSL_CTX_set_alpn_protos(tls->ctx, alpn_protos, sizeof(alpn_protos) - 1);

    // Enforce TLS versions if requested
    if (force_tls12) {
        SSL_CTX_set_min_proto_version(tls->ctx, TLS1_2_VERSION);
        SSL_CTX_set_max_proto_version(tls->ctx, TLS1_2_VERSION);
    } else if (force_tls13) {
        SSL_CTX_set_min_proto_version(tls->ctx, TLS1_3_VERSION);
        SSL_CTX_set_max_proto_version(tls->ctx, TLS1_3_VERSION);
    }

    // Configure certificate verification
    if (!verify_certs) {
        SSL_CTX_set_verify(tls->ctx, SSL_VERIFY_NONE, NULL);
    } else {
        SSL_CTX_set_verify(tls->ctx, SSL_VERIFY_PEER, NULL);
        if (cacert_path) {
            if (SSL_CTX_load_verify_locations(tls->ctx, cacert_path, NULL) != 1) {
                // Failed to load custom CA cert
                SSL_CTX_free(tls->ctx);
                free(tls);
                return NULL;
            }
        } else {
            // Load standard system paths
            SSL_CTX_set_default_verify_paths(tls->ctx);
        }
    }

    // Configure client certificates if provided
    if (cert_path) {
        if (SSL_CTX_use_certificate_chain_file(tls->ctx, cert_path) != 1) {
            SSL_CTX_free(tls->ctx);
            free(tls);
            return NULL;
        }
    }
    if (key_path) {
        if (SSL_CTX_use_PrivateKey_file(tls->ctx, key_path, SSL_FILETYPE_PEM) != 1) {
            SSL_CTX_free(tls->ctx);
            free(tls);
            return NULL;
        }
        // Verify private key matches certificate
        if (cert_path && SSL_CTX_check_private_key(tls->ctx) != 1) {
            SSL_CTX_free(tls->ctx);
            free(tls);
            return NULL;
        }
    }

    return tls;
}

int nps_tls_handshake(nps_tls_t *tls, int socket_fd, const char *hostname) {
    if (!tls) {
        return -1;
    }

    tls->ssl = SSL_new(tls->ctx);
    if (!tls->ssl) {
        return -1;
    }

    SSL_set_fd(tls->ssl, socket_fd);

    // Set Server Name Indication (SNI) header
    SSL_set_tlsext_host_name(tls->ssl, hostname);

    // Enforce certificate hostname validation if verifying
    if (tls->verify) {
        X509_VERIFY_PARAM *param = SSL_get0_param(tls->ssl);
        X509_VERIFY_PARAM_set1_host(param, hostname, 0);
    }

    int ret = SSL_connect(tls->ssl);
    if (ret != 1) {
        unsigned long ssl_err = ERR_get_error();
        ERR_error_string_n(ssl_err, tls->last_error, sizeof(tls->last_error));
        SSL_free(tls->ssl);
        tls->ssl = NULL;
        return -1;
    }

    // Extract ALPN negotiated protocol
    const unsigned char *alpn = NULL;
    unsigned int alpn_len = 0;
    SSL_get0_alpn_selected(tls->ssl, &alpn, &alpn_len);
    if (alpn && alpn_len > 0) {
        if (alpn_len < sizeof(tls->negotiated_proto)) {
            memcpy(tls->negotiated_proto, alpn, alpn_len);
            tls->negotiated_proto[alpn_len] = '\0';
        }
    } else {
        tls->negotiated_proto[0] = '\0';
    }

    return 0;
}

int nps_tls_read(nps_tls_t *tls, void *buf, int len) {
    if (!tls || !tls->ssl) {
        return -1;
    }
    return SSL_read(tls->ssl, buf, len);
}

int nps_tls_write(nps_tls_t *tls, const void *buf, int len) {
    if (!tls || !tls->ssl) {
        return -1;
    }
    return SSL_write(tls->ssl, buf, len);
}

void nps_tls_close(nps_tls_t *tls) {
    if (tls && tls->ssl) {
        SSL_shutdown(tls->ssl);
        SSL_free(tls->ssl);
        tls->ssl = NULL;
    }
}

void nps_tls_free(nps_tls_t *tls) {
    if (tls) {
        nps_tls_close(tls);
        if (tls->ctx) {
            SSL_CTX_free(tls->ctx);
        }
        free(tls);
    }
}

const char *nps_tls_get_negotiated_proto(nps_tls_t *tls) {
    if (!tls || tls->negotiated_proto[0] == '\0') {
        return NULL;
    }
    return tls->negotiated_proto;
}

const char *nps_tls_last_error(nps_tls_t *tls) {
    if (!tls || tls->last_error[0] == '\0') {
        return NULL;
    }
    return tls->last_error;
}
