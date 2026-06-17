#include "teko_socket.h"

// Phase 19 (T1a) — client socket C runtime implementation.
//
// The entire implementation is guarded by #if !defined(__wasm__) so this TU
// compiles into both teko_core (tests) and teko_rt (native binaries) without
// introducing POSIX/Winsock headers into the wasm32 reactor build. On WASM,
// every exported function becomes a stub that returns NULL / TEKO_SOCK_ERR_BADARG
// so a caller can still include the header and the linker resolves the symbols.
//
// SAST posture (per §7 of PHASE19_NETWORKING.md):
//   - recv buffer: buf_len > TEKO_SOCKET_MAX_BUF is checked BEFORE the syscall.
//   - handle table: alloc_handle bounds-checked against TEKO_SOCKET_MAX_HANDLES.
//   - All allocations via calloc (zero-init — matches parser_ffi.c wild-free lesson).
//   - SOCKET on Win64 is unsigned __int64; stored in teko_fd_t (= SOCKET on Windows).
//   - No format-string, no path-traversal: host is passed directly to getaddrinfo.
//   - No C23 auto/nullptr; no computed-goto; no __attribute__((packed)).

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#if !defined(__wasm__)

// --------------------------------------------------------------------------
// Platform-specific: socket descriptor type + headers
// --------------------------------------------------------------------------
#if defined(_WIN32)
// Winsock must be included before <windows.h> to avoid macro collisions.
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
// Tell MSVC to link ws2_32.lib without a separate CMake target_link_libraries
// entry for the test executable (teko_rt already links it).
#  pragma comment(lib, "ws2_32.lib")
typedef SOCKET teko_fd_t;
#  define TEKO_INVALID_FD INVALID_SOCKET
// Windows uses closesocket(), not close().
#  define teko_closesocket(fd) closesocket(fd)
// recv/send return int on both platforms; the cast below is safe.
typedef int teko_socklen_t;
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netdb.h>
#  include <fcntl.h>
#  include <unistd.h>
#  include <errno.h>
typedef int    teko_fd_t;
#  define TEKO_INVALID_FD (-1)
#  define teko_closesocket(fd) close(fd)
typedef socklen_t teko_socklen_t;
#endif // _WIN32

// --------------------------------------------------------------------------
// Internal struct (not exposed in the header)
// --------------------------------------------------------------------------
struct TekoSocket {
    teko_fd_t      fd;
    TekoSocketState state;
    int            is_udp;
    uint32_t       _pad;   // alignment padding
};

// --------------------------------------------------------------------------
// Handle table (static, zero-initialised, TEKO_SOCKET_MAX_HANDLES slots)
// --------------------------------------------------------------------------
static TekoSocket* g_handles[TEKO_SOCKET_MAX_HANDLES]; // NULL = free slot
static int         g_winsock_inited = 0;

// Reserve a free slot in the handle table and calloc a new TekoSocket into it.
// Returns NULL if the table is full (bounds: index < TEKO_SOCKET_MAX_HANDLES).
static TekoSocket* alloc_handle(void) {
    for (int i = 0; i < TEKO_SOCKET_MAX_HANDLES; i++) {
        if (g_handles[i] == NULL) {
            TekoSocket* s = (TekoSocket*)calloc(1, sizeof(TekoSocket));
            if (!s) return NULL;
            // calloc gives zero-init: fd=0/INVALID (platform), state=NEW, is_udp=0.
            // On POSIX fd=0 is stdin — mark it as INVALID explicitly.
            s->fd    = TEKO_INVALID_FD;
            s->state = TEKO_SOCK_NEW;
            g_handles[i] = s;
            return s;
        }
    }
    return NULL; // table full (TEKO_SOCK_ERR_BOUNDS at the caller)
}

// Remove the handle from the table (does NOT free — teko_socket_free does that).
static void release_handle(TekoSocket* s) {
    if (!s) return;
    for (int i = 0; i < TEKO_SOCKET_MAX_HANDLES; i++) {
        if (g_handles[i] == s) {
            g_handles[i] = NULL;
            return;
        }
    }
}

// --------------------------------------------------------------------------
// Winsock initialisation (POSIX: no-op)
// --------------------------------------------------------------------------
static void ensure_platform_init(void) {
#if defined(_WIN32)
    if (!g_winsock_inited) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        g_winsock_inited = 1;
    }
#else
    (void)g_winsock_inited; // silence unused-variable warning on POSIX
#endif
}

// --------------------------------------------------------------------------
// Set a socket to non-blocking mode
// --------------------------------------------------------------------------
static int set_nonblocking(teko_fd_t fd) {
#if defined(_WIN32)
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 ? 0 : -1;
#endif
}

// --------------------------------------------------------------------------
// Connect helper: shared logic for TCP and UDP
// --------------------------------------------------------------------------
static TekoSocket* do_connect(const char* host, uint16_t port, int is_udp) {
    if (!host || port == 0) return NULL;
    ensure_platform_init();

    char port_str[8];
    // Portable uint16_t -> string without snprintf (freestanding-safe).
    // Maximum port value is 65535 — 5 digits + NUL fits in 8 bytes.
    uint16_t p = port;
    int idx = 6;
    port_str[7] = '\0';
    if (p == 0) { port_str[6] = '0'; idx = 6; }
    else {
        port_str[7] = '\0';
        idx = 7;
        while (p > 0 && idx > 0) {
            port_str[--idx] = (char)('0' + (p % 10));
            p = (uint16_t)(p / 10);
        }
    }

    struct addrinfo hints, *res = NULL, *rp = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = is_udp ? SOCK_DGRAM : SOCK_STREAM;

    if (getaddrinfo(host, port_str + idx, &hints, &res) != 0) return NULL;

    teko_fd_t fd = TEKO_INVALID_FD;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = (teko_fd_t)socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == TEKO_INVALID_FD) continue;

        if (set_nonblocking(fd) != 0) {
            teko_closesocket(fd);
            fd = TEKO_INVALID_FD;
            continue;
        }

        int rc = connect(fd, rp->ai_addr, (teko_socklen_t)rp->ai_addrlen);
        if (rc == 0) break; // connected immediately (UDP or loopback)

#if defined(_WIN32)
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) {
            // Non-blocking connect in progress: use select with 5 s timeout.
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(fd, &wfds);
            struct timeval tv = {5, 0};
            if (select(0, NULL, &wfds, NULL, &tv) > 0) break; // connected
        }
#else
        if (errno == EINPROGRESS) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(fd, &wfds);
            struct timeval tv;
            tv.tv_sec = 5; tv.tv_usec = 0;
            if (select((int)fd + 1, NULL, &wfds, NULL, &tv) > 0) {
                int so_err = 0;
                teko_socklen_t so_len = (teko_socklen_t)sizeof(so_err);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &so_len);
                if (so_err == 0) break; // connected
            }
        }
#endif
        teko_closesocket(fd);
        fd = TEKO_INVALID_FD;
    }
    freeaddrinfo(res);

    if (fd == TEKO_INVALID_FD) return NULL;

    TekoSocket* s = alloc_handle();
    if (!s) {
        teko_closesocket(fd);
        return NULL;
    }
    s->fd     = fd;
    s->state  = TEKO_SOCK_OPEN;
    s->is_udp = is_udp ? 1 : 0;
    return s;
}

// --------------------------------------------------------------------------
// Public API implementation
// --------------------------------------------------------------------------

TekoSocket* teko_socket_tcp_connect(const char* host, uint16_t port) {
    return do_connect(host, port, 0);
}

TekoSocket* teko_socket_udp_open(const char* host, uint16_t port) {
    return do_connect(host, port, 1);
}

TekoSocketStatus teko_socket_send(TekoSocket* s, const char* data, uint32_t len) {
    if (!s || !data) return TEKO_SOCK_ERR_BADARG;
    if (s->state == TEKO_SOCK_CLOSED) return TEKO_SOCK_ERR_CLOSED;
    if (s->state != TEKO_SOCK_OPEN)   return TEKO_SOCK_ERR_BADARG;
    if (len == 0) return TEKO_SOCK_OK; // nothing to send

    uint32_t sent = 0;
    int retries = 0;
    while (sent < len && retries < 8) {
#if defined(_WIN32)
        int r = send(s->fd, data + sent, (int)(len - sent), 0);
        if (r > 0) {
            sent += (uint32_t)r;
            retries = 0;
            continue;
        }
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) { retries++; continue; }
#else
        ssize_t r = send(s->fd, data + sent, (size_t)(len - sent), 0);
        if (r > 0) {
            sent += (uint32_t)r;
            retries = 0;
            continue;
        }
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { retries++; continue; }
#endif
        s->state = TEKO_SOCK_ERROR;
        return TEKO_SOCK_ERR_SEND;
    }
    return (sent == len) ? TEKO_SOCK_OK : TEKO_SOCK_ERR_SEND;
}

TekoSocketStatus teko_socket_recv(TekoSocket* s, char* buf,
                                   uint32_t buf_len, uint32_t* out_received) {
    if (!s || !buf || !out_received) return TEKO_SOCK_ERR_BADARG;
    *out_received = 0;
    // Bounds gate: checked BEFORE the syscall — network-input size validation.
    if (buf_len > TEKO_SOCKET_MAX_BUF) return TEKO_SOCK_ERR_BOUNDS;
    if (s->state == TEKO_SOCK_CLOSED) return TEKO_SOCK_ERR_CLOSED;
    if (s->state != TEKO_SOCK_OPEN)   return TEKO_SOCK_ERR_BADARG;

#if defined(_WIN32)
    int r = recv(s->fd, buf, (int)buf_len, 0);
    if (r > 0) {
        *out_received = (uint32_t)r;
        return TEKO_SOCK_OK;
    }
    if (r == 0) { s->state = TEKO_SOCK_CLOSED; return TEKO_SOCK_ERR_CLOSED; }
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) return TEKO_SOCK_ERR_RECV; // yield/retry
#else
    ssize_t r = recv(s->fd, buf, (size_t)buf_len, 0);
    if (r > 0) {
        *out_received = (uint32_t)r;
        return TEKO_SOCK_OK;
    }
    if (r == 0) { s->state = TEKO_SOCK_CLOSED; return TEKO_SOCK_ERR_CLOSED; }
    if (errno == EAGAIN || errno == EWOULDBLOCK) return TEKO_SOCK_ERR_RECV; // yield
#endif
    s->state = TEKO_SOCK_ERROR;
    return TEKO_SOCK_ERR_RECV;
}

TekoSocketStatus teko_socket_close(TekoSocket* s) {
    if (!s) return TEKO_SOCK_ERR_BADARG;
    if (s->state == TEKO_SOCK_CLOSED) return TEKO_SOCK_OK; // idempotent
    if (s->fd != TEKO_INVALID_FD) {
        teko_closesocket(s->fd);
        s->fd = TEKO_INVALID_FD;
    }
    s->state = TEKO_SOCK_CLOSED;
    return TEKO_SOCK_OK;
}

void teko_socket_free(TekoSocket* s) {
    if (!s) return;
    teko_socket_close(s);
    release_handle(s);
    free(s);
}

TekoSocketState teko_socket_state(const TekoSocket* s) {
    if (!s) return TEKO_SOCK_ERROR;
    return s->state;
}

#else // __wasm__ — freestanding stubs so the header resolves on WASM builds

TekoSocket* teko_socket_tcp_connect(const char* host, uint16_t port) {
    (void)host; (void)port; return NULL;
}
TekoSocket* teko_socket_udp_open(const char* host, uint16_t port) {
    (void)host; (void)port; return NULL;
}
TekoSocketStatus teko_socket_send(TekoSocket* s, const char* data, uint32_t len) {
    (void)s; (void)data; (void)len; return TEKO_SOCK_ERR_BADARG;
}
TekoSocketStatus teko_socket_recv(TekoSocket* s, char* buf,
                                   uint32_t buf_len, uint32_t* out_received) {
    (void)s; (void)buf; (void)buf_len;
    if (out_received) *out_received = 0;
    return TEKO_SOCK_ERR_BADARG;
}
TekoSocketStatus teko_socket_close(TekoSocket* s) {
    (void)s; return TEKO_SOCK_ERR_BADARG;
}
void teko_socket_free(TekoSocket* s) { (void)s; }
TekoSocketState  teko_socket_state(const TekoSocket* s) {
    (void)s; return TEKO_SOCK_ERROR;
}

#endif // !__wasm__
