// Phase 19 (T1b, Wave 0) — server socket runtime implementation.
//
// NATIVE-ONLY: compiled only when !defined(__wasm__). Do NOT add this file to
// runtime/wasm/crypto/build-crypto-reactor.sh SRCS or EXPORTS.
//
// SEPARATE TU: linked only when a produced binary references teko_server_open/free.
// Programs without a listening server never pull this object in.
//
// Portability:
//   POSIX  — Linux, macOS, FreeBSD, other UNIX.
//   Winsock2 — Windows (_WIN32 / MSVC). We use WSAStartup / closesocket / ioctlsocket
//              rather than POSIX equivalents; SOCKET is UINT_PTR (64-bit on x64).
//
// Cooperative C1 posture: accept/recv are NON-BLOCKING; they return TEKO_SRV_AGAIN
// immediately when no connection/data is pending so the Phase-14 scheduler can run
// other tasks (teko_rt_run) then retry — the OS thread is never parked in the kernel.
//
// Hardening (attacker-controlled accept surface — §7 SAST from PHASE19_NETWORKING.md):
//   - Connection cap enforced before every accept.
//   - send uses bounded retry on EAGAIN/EWOULDBLOCK — no infinite spin.
//   - All size arithmetic is uint32_t; length before every copy; no format-string/
//     path-traversal paths; buf pointer checked before dereferencing.
//   - calloc zero-initialises the TekoServer struct; ownership is clear
//     (teko_server_free is the sole destructor).

#if !defined(__wasm__)

#include "teko_socket_server.h"

// --- platform detection and socket layer includes --------------------------------

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")

// Windows type aliases to unify the code below.
typedef SOCKET    teko_sock_t;
#  define TEKO_INVALID_SOCK  INVALID_SOCKET
#  define TEKO_SOCK_ERR      SOCKET_ERROR
#  define teko_closesocket   closesocket

// Set a socket to non-blocking mode (Windows: ioctlsocket).
static int teko_set_nonblocking(teko_sock_t fd) {
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode);
}
// Predicate: would the last socket op block?
static int teko_would_block(void) {
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK;
}
// Predicate: was the last accept() error "would block"?
static int teko_accept_would_block(void) { return teko_would_block(); }

#else // POSIX
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <unistd.h>
#  include <errno.h>
#  include <string.h>

typedef int       teko_sock_t;
#  define TEKO_INVALID_SOCK  (-1)
#  define TEKO_SOCK_ERR      (-1)
#  define teko_closesocket   close

static int teko_set_nonblocking(teko_sock_t fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
static int teko_would_block(void) {
    return errno == EAGAIN || errno == EWOULDBLOCK;
}
static int teko_accept_would_block(void) {
#  if defined(EAGAIN) && defined(EWOULDBLOCK) && (EAGAIN != EWOULDBLOCK)
    return errno == EAGAIN || errno == EWOULDBLOCK;
#  else
    return errno == EAGAIN;
#  endif
}
#endif // platform

#include <stdlib.h>

// --- TekoServer definition -------------------------------------------------------

struct TekoServer {
    teko_sock_t  listen_fd;   // the bound + listening socket
    uint16_t     port;        // port passed to teko_server_open
    uint32_t     max_conns;   // clamped max simultaneous connections
    uint32_t     conn_count;  // current open accepted connections
};

// --- WSA init helper (Windows only) ---------------------------------------------

#if defined(_WIN32)
static int teko_wsa_init(void) {
    WSADATA wsa;
    // WSAStartup is idempotent for the same version; repeated calls just bump the
    // internal reference count. We request Winsock 2.2.
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0 ? 0 : -1;
}
#else
static int teko_wsa_init(void) { return 0; } // no-op on POSIX
#endif

// --- teko_server_open -----------------------------------------------------------

TekoServer* teko_server_open(uint16_t port, uint32_t backlog, uint32_t max_conns) {
    if (teko_wsa_init() != 0) return NULL;

    // Clamp caps (hard safety bounds — never exceed the defines).
    if (backlog  == 0 || backlog  > TEKO_SERVER_MAX_BACKLOG) backlog  = TEKO_SERVER_MAX_BACKLOG;
    if (max_conns == 0 || max_conns > TEKO_SERVER_MAX_CONNS)  max_conns = TEKO_SERVER_MAX_CONNS;

    teko_sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == TEKO_INVALID_SOCK) return NULL;

    // SO_REUSEADDR: allow quick server restart without TIME_WAIT delay.
    int opt = 1;
#if defined(_WIN32)
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, (int)sizeof(opt));
#else
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, (socklen_t)sizeof(opt));
#endif

    struct sockaddr_in addr;
    // Zero-init the full addr struct before filling fields (C portability: padding bytes).
    {
        unsigned char* p = (unsigned char*)&addr;
        for (int i = 0; i < (int)sizeof(addr); i++) p[i] = 0;
    }
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr*)&addr, (int)sizeof(addr)) == TEKO_SOCK_ERR) {
        teko_closesocket(fd);
        return NULL;
    }

    if (listen(fd, (int)backlog) == TEKO_SOCK_ERR) {
        teko_closesocket(fd);
        return NULL;
    }

    // Set the listening socket non-blocking so teko_server_accept never blocks.
    if (teko_set_nonblocking(fd) != 0) {
        teko_closesocket(fd);
        return NULL;
    }

    // calloc zero-initialises all fields — no UAF risk from unset pointers.
    TekoServer* s = (TekoServer*)calloc(1, sizeof(TekoServer));
    if (!s) { teko_closesocket(fd); return NULL; }

    s->listen_fd  = fd;
    s->port       = port;
    s->max_conns  = max_conns;
    s->conn_count = 0;
    return s;
}

// --- teko_server_free -----------------------------------------------------------

void teko_server_free(TekoServer* s) {
    if (!s) return;
    if (s->listen_fd != TEKO_INVALID_SOCK) {
        teko_closesocket(s->listen_fd);
        s->listen_fd = TEKO_INVALID_SOCK;
    }
    free(s);
}

// --- teko_server_accept ---------------------------------------------------------

TekoServerStatus teko_server_accept(TekoServer* s, intptr_t* out_conn_fd) {
    if (out_conn_fd) *out_conn_fd = -1;
    if (!s || !out_conn_fd)       return TEKO_SRV_BADARG;
    if (s->listen_fd == TEKO_INVALID_SOCK) return TEKO_SRV_CLOSED;

    // Enforce connection cap BEFORE calling accept() — avoids burning an fd slot just
    // to immediately close it (fd-exhaustion / slowloris hardening).
    if (s->conn_count >= s->max_conns) return TEKO_SRV_FULL;

    struct sockaddr_in peer;
    // Zero-init peer storage (portability: avoid reading uninitialised padding).
    {
        unsigned char* p = (unsigned char*)&peer;
        for (int i = 0; i < (int)sizeof(peer); i++) p[i] = 0;
    }
#if defined(_WIN32)
    int peer_len = (int)sizeof(peer);
    teko_sock_t conn = accept(s->listen_fd, (struct sockaddr*)&peer, &peer_len);
#else
    socklen_t peer_len = (socklen_t)sizeof(peer);
    teko_sock_t conn = accept(s->listen_fd, (struct sockaddr*)&peer, &peer_len);
#endif

    if (conn == TEKO_INVALID_SOCK) {
        if (teko_accept_would_block()) return TEKO_SRV_AGAIN;
        return TEKO_SRV_ERR;
    }

    // Set the accepted connection non-blocking (cooperative recv posture).
    if (teko_set_nonblocking(conn) != 0) {
        teko_closesocket(conn);
        return TEKO_SRV_ERR;
    }

    s->conn_count++;
    // LLP64 correctness: cast SOCKET (UINT_PTR, up to 64-bit) through intptr_t.
    // On Windows x64 SOCKET fits in intptr_t (both 64-bit). On POSIX int fits trivially.
    *out_conn_fd = (intptr_t)conn;
    return TEKO_SRV_OK;
}

// --- teko_server_recv -----------------------------------------------------------

TekoServerStatus teko_server_recv(intptr_t conn_fd, void* buf, uint32_t len, uint32_t* out_n) {
    if (out_n) *out_n = 0;
    if (!buf || len == 0 || !out_n) return TEKO_SRV_BADARG;

#if defined(_WIN32)
    teko_sock_t fd = (teko_sock_t)(UINT_PTR)conn_fd; // LLP64: safe widening intptr_t -> UINT_PTR
    int r = recv(fd, (char*)buf, (int)len, 0);
#else
    teko_sock_t fd = (teko_sock_t)conn_fd;
    int r = (int)recv(fd, buf, (size_t)len, 0);
#endif

    if (r < 0) {
        if (teko_would_block()) return TEKO_SRV_AGAIN;
        return TEKO_SRV_ERR;
    }
    if (r == 0) return TEKO_SRV_CLOSED; // peer closed connection (EOF)

    // r > 0: bytes received. Checked cast: recv returns at most `len` bytes, so
    // r fits in uint32_t since len <= UINT32_MAX.
    *out_n = (uint32_t)r;
    return TEKO_SRV_OK;
}

// --- teko_server_send -----------------------------------------------------------

TekoServerStatus teko_server_send(intptr_t conn_fd, const void* buf, uint32_t len) {
    if (!buf || len == 0) return TEKO_SRV_BADARG;

#if defined(_WIN32)
    teko_sock_t fd = (teko_sock_t)(UINT_PTR)conn_fd;
#else
    teko_sock_t fd = (teko_sock_t)conn_fd;
#endif

    uint32_t sent = 0;
    // Bounded retry loop: at most TEKO_SERVER_MAX_CONNS iterations (256) prevents an
    // infinite spin on a persistently-congested socket (slowloris-style send-fill).
    for (uint32_t iter = 0; iter < TEKO_SERVER_MAX_CONNS && sent < len; iter++) {
        uint32_t remaining = len - sent;
        const char* ptr = (const char*)buf + sent;

#if defined(_WIN32)
        int r = send(fd, ptr, (int)remaining, 0);
#else
        int r = (int)send(fd, ptr, (size_t)remaining, 0);
#endif

        if (r < 0) {
            if (teko_would_block()) continue; // brief stall: retry within the loop
            return TEKO_SRV_ERR;
        }
        if (r == 0) return TEKO_SRV_CLOSED; // peer closed
        // r > 0 and r <= remaining (by send semantics), so addition is safe.
        sent += (uint32_t)r;
    }

    return (sent == len) ? TEKO_SRV_OK : TEKO_SRV_ERR; // ERR if cap exhausted before full send
}

// --- teko_server_conn_close -----------------------------------------------------

void teko_server_conn_close(TekoServer* s, intptr_t conn_fd) {
    if (conn_fd == -1) return;

#if defined(_WIN32)
    teko_closesocket((teko_sock_t)(UINT_PTR)conn_fd);
#else
    teko_closesocket((teko_sock_t)conn_fd);
#endif

    // Decrement the counter so new connections can be accepted (cap enforcement).
    if (s && s->conn_count > 0) s->conn_count--;
}

// --- teko_server_conn_count / teko_server_port ----------------------------------

uint32_t teko_server_conn_count(const TekoServer* s) {
    return s ? s->conn_count : 0u;
}

uint16_t teko_server_port(const TekoServer* s) {
    return s ? s->port : 0u;
}

#endif // !defined(__wasm__)
