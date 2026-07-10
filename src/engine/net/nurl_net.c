#include "nurl_net.h"
#include "nurl_utils.h"
#include "nurl_buf.h"
#include "compat/nurl_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #define socket_close(fd) closesocket(fd)
  #define socket_read(fd, buf, len) recv(fd, (char *)(buf), (int)(len), 0)
  #define socket_write(fd, buf, len) send(fd, (const char *)(buf), (int)(len), 0)
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netdb.h>
  #include <fcntl.h>
  #include <errno.h>
  #include <poll.h>
  #define socket_close(fd) close(fd)
  #define socket_read(fd, buf, len) read(fd, buf, len)
  #define socket_write(fd, buf, len) write(fd, buf, len)
#endif

int nurl_net_init(void) {
#ifdef _WIN32
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData);
#else
    return 0;
#endif
}

void nurl_net_cleanup(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

int nurl_net_connect_ex(const char *hostname, int port, unsigned int connect_timeout_sec, nurl_err_t *out_err) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints;
    struct addrinfo *result, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname, port_str, &hints, &result) != 0) {
        if (out_err) *out_err = NURL_ERR_RESOLVE;
        return -1;
    }

    int socket_fd = -1;
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        socket_fd = (int)(intptr_t)socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (socket_fd == -1) continue;

#ifndef _WIN32
        if (connect_timeout_sec > 0) {
            int flags = fcntl(socket_fd, F_GETFL, 0);
            fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);

            if (connect(socket_fd, rp->ai_addr, rp->ai_addrlen) == -1) {
                if (errno != EINPROGRESS) {
                    socket_close(socket_fd);
                    socket_fd = -1;
                    continue;
                }

                struct pollfd pfd;
                pfd.fd = socket_fd;
                pfd.events = POLLOUT;
                int res = poll(&pfd, 1, (int)(connect_timeout_sec * 1000));
                if (res <= 0) {
                    socket_close(socket_fd);
                    socket_fd = -1;
                    continue;
                }

                int err;
                socklen_t len = sizeof(err);
                if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
                    socket_close(socket_fd);
                    socket_fd = -1;
                    continue;
                }
            }
            fcntl(socket_fd, F_SETFL, flags);
            break; /* Success */
        } else {
            if (connect(socket_fd, rp->ai_addr, rp->ai_addrlen) != -1) break;
        }
#else
        if (connect(socket_fd, rp->ai_addr, (int)rp->ai_addrlen) != -1) break;
#endif
        socket_close(socket_fd);
        socket_fd = -1;
    }

    freeaddrinfo(result);
    if (socket_fd == -1) {
        if (out_err) *out_err = NURL_ERR_CONNECT;
        return -1;
    }

    return socket_fd;
}

int nurl_net_set_timeout(int socket_fd, unsigned long seconds) {
#ifdef _WIN32
    DWORD timeout = (DWORD)(seconds * 1000);
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    return 0;
}

bool nurl_net_is_alive(int socket_fd) {
    char buf;
#ifdef _WIN32
    int res = recv(socket_fd, &buf, 1, MSG_PEEK);
    if (res == 0) return false;
    if (res < 0) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) return false;
    }
#else
    int res = (int)recv(socket_fd, &buf, 1, MSG_PEEK | MSG_DONTWAIT);
    if (res == 0) return false;
    if (res < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return false;
#endif
    return true;
}

void nurl_net_close(int socket_fd) {
    socket_close(socket_fd);
}

int nurl_net_connect_proxy_ex(
    const char *host, int port,
    const char *proxy, const char *proxy_user, const char *no_proxy,
    const char *connect_to,
    unsigned int connect_timeout_sec,
    nurl_err_t *out_err
) {
    const char *target_host = host;
    int target_port = port;
    char *allocated_target_host = NULL;

    if (connect_to) {
        // Format: host:port:target_host:target_port
        char *ct = strdup(connect_to);
        char *h = ct;
        char *p = strchr(h, ':');
        if (p) {
            *p = '\0';
            char *th = strchr(p + 1, ':');
            if (th) {
                *th = '\0';
                char *tp = strchr(th + 1, ':');
                if (tp) {
                    *tp = '\0';
                    int p_val = atoi(p + 1);
                    if (nurl_strcasecmp(h, host) == 0 && p_val == port) {
                        allocated_target_host = strdup(th + 1);
                        target_host = allocated_target_host;
                        target_port = atoi(tp + 1);
                    }
                }
            }
        }
        free(ct);
    }

    bool use_proxy = (proxy != NULL);
    if (use_proxy && no_proxy) {
        char *np_copy = strdup(no_proxy);
        if (np_copy) {
            char *trimmed_np = nurl_utils_trim(np_copy);
            char *tok = trimmed_np;
            while (tok) {
                char *comma = strchr(tok, ',');
                if (comma) *comma = '\0';
                char *item = nurl_utils_trim(tok);
                if (item) {
                    size_t hlen = strlen(host);
                    size_t ilen = strlen(item);
                    if (nurl_strcasecmp(host, item) == 0) {
                        use_proxy = false;
                        break;
                    }
                    if (item[0] == '.' && hlen > ilen && nurl_strcasecmp(host + hlen - ilen, item) == 0) {
                        use_proxy = false;
                        break;
                    }
                }
                if (comma) tok = comma + 1;
                else tok = NULL;
            }
            free(np_copy);
        }
    }

    if (!use_proxy) {
        int res = nurl_net_connect_ex(target_host, target_port, connect_timeout_sec, out_err);
        if (allocated_target_host) free(allocated_target_host);
        return res;
    }

    char *proxy_scheme = NULL, *proxy_host = NULL, *proxy_path = NULL;
    int proxy_port = 0;
    if (nurl_utils_parse_url(proxy, &proxy_scheme, &proxy_host, &proxy_port, &proxy_path) != 0) {
        if (out_err) *out_err = NURL_ERR_URL;
        if (allocated_target_host) free(allocated_target_host);
        return -1;
    }
    free(proxy_scheme);
    free(proxy_path);

    if (!proxy_host) {
        if (out_err) *out_err = NURL_ERR_URL;
        if (allocated_target_host) free(allocated_target_host);
        return -1;
    }

    int socket_fd = nurl_net_connect_ex(proxy_host, proxy_port, connect_timeout_sec, out_err);
    free(proxy_host);

    if (socket_fd < 0) {
        if (allocated_target_host) free(allocated_target_host);
        return -1;
    }

    // HTTP CONNECT Tunneling
    NutBuf connect_req;
    nurl_buf_init(&connect_req);
    nurl_buf_printf(&connect_req, "CONNECT %s:%d HTTP/1.1\r\nHost: %s:%d\r\n", target_host, target_port, target_host, target_port);
    if (allocated_target_host) free(allocated_target_host);

    if (proxy_user) {
        char *auth_b64 = nurl_utils_base64_encode((const unsigned char *)proxy_user, strlen(proxy_user));
        if (auth_b64) {
            nurl_buf_printf(&connect_req, "Proxy-Authorization: Basic %s\r\n", auth_b64);
            free(auth_b64);
        }
    }

    nurl_buf_append(&connect_req, "\r\n", 2);

    char *req_data = connect_req.data;
    size_t req_len = connect_req.len;

    if (socket_write(socket_fd, req_data, req_len) <= 0) {
        nurl_buf_free(&connect_req);
        if (out_err) *out_err = NURL_ERR_NETWORK;
        socket_close(socket_fd);
        return -1;
    }
    nurl_buf_free(&connect_req);

    // Read Response Header (buffered)
    char resp_buf[4096];
    int resp_len = 0;
    while (resp_len < (int)sizeof(resp_buf) - 1) {
        int n = socket_read(socket_fd, resp_buf + resp_len, sizeof(resp_buf) - resp_len - 1);
        if (n <= 0) break;
        resp_len += n;
        resp_buf[resp_len] = '\0';
        if (strstr(resp_buf, "\r\n\r\n") != NULL) {
            break;
        }
    }

    // Verify 2xx response
    if (strstr(resp_buf, " 200") == NULL) {
        if (out_err) *out_err = NURL_ERR_PROXY;
        socket_close(socket_fd);
        return -1;
    }

    return socket_fd;
}
