#ifndef TEKO_SOCKET_H
#define TEKO_SOCKET_H

// Phase 19 (T1a) — client socket C runtime: the single source of truth for the
// net.tcp_connect / net.udp_open / net.send / net.recv / net.close surfaces.
//
// This header is target-agnostic: the implementation body in teko_socket.c is
// guarded #if !defined(__wasm__) (BSD sockets do not exist in the wasm32
// reactor). The WASM build still includes this header so callers compile, but
// every function is a stub returning NULL / TEKO_SOCK_ERR_BADARG.
//
// Pattern: same as teko_duplex.h — opaque pointer, separated state/status enums,
// calloc-allocated handle table, fail-loud bounds, no runtime reflection, no UAF.
//
// OP_CALL_RUNTIME ids 60-69 (net-client) are RESERVED in this wave.
// Emission / frontend surface wiring is NOT done here — it comes in T2 (Wave 1).

#include <stdint.h>

// --- Connection state (what the socket IS) ---
typedef enum {
    TEKO_SOCK_NEW        = 0, // allocated, not yet connected
    TEKO_SOCK_CONNECTING = 1, // connect() in progress (non-blocking)
    TEKO_SOCK_OPEN       = 2, // fully established, ready for I/O
    TEKO_SOCK_CLOSED     = 3, // shut down cleanly
    TEKO_SOCK_ERROR      = 4  // unrecoverable error
} TekoSocketState;

// --- Operation status (what a call returned) ---
typedef enum {
    TEKO_SOCK_OK           = 0, // success
    TEKO_SOCK_ERR_BADARG   = 1, // NULL/invalid argument
    TEKO_SOCK_ERR_RESOLVE  = 2, // hostname resolution failure
    TEKO_SOCK_ERR_CONNECT  = 3, // connect() failure
    TEKO_SOCK_ERR_SEND     = 4, // send() failure
    TEKO_SOCK_ERR_RECV     = 5, // recv() failure / would-block (caller yields/retries)
    TEKO_SOCK_ERR_CLOSED   = 6, // peer closed the connection (EOF)
    TEKO_SOCK_ERR_BOUNDS   = 7  // buffer length exceeds TEKO_SOCKET_MAX_BUF
} TekoSocketStatus;

// Maximum simultaneous live socket handles.
#define TEKO_SOCKET_MAX_HANDLES 256

// Maximum recv buffer size enforced BEFORE every recv() syscall — the
// network-input bounds gate, checked before any data crosses the boundary.
#define TEKO_SOCKET_MAX_BUF 65536u

// Opaque handle — internal layout is in teko_socket.c only.
typedef struct TekoSocket TekoSocket;

// --------------------------------------------------------------------------
// Public API
// All pointer-returning functions return NULL on allocation / connect failure.
// All status-returning functions return TEKO_SOCK_ERR_BADARG on NULL input.
// --------------------------------------------------------------------------

// TCP client: resolve host:port, connect, return an OPEN handle or NULL.
TekoSocket* teko_socket_tcp_connect(const char* host, uint16_t port);

// UDP client: resolve host:port, connect (sets default peer for send/recv).
// Returns an OPEN handle or NULL on failure.
TekoSocket* teko_socket_udp_open(const char* host, uint16_t port);

// Send len bytes from data on an OPEN socket.
// Returns TEKO_SOCK_ERR_BADARG on NULL args; TEKO_SOCK_ERR_CLOSED on closed socket.
TekoSocketStatus teko_socket_send(TekoSocket* s, const char* data, uint32_t len);

// Non-blocking recv into buf (buf_len must be <= TEKO_SOCKET_MAX_BUF).
//   TEKO_SOCK_OK        + *out_received > 0 : data available, read bytes returned.
//   TEKO_SOCK_ERR_RECV  + *out_received = 0 : would block; caller should yield/retry.
//   TEKO_SOCK_ERR_CLOSED                    : EOF / peer closed.
//   TEKO_SOCK_ERR_BOUNDS                    : buf_len > TEKO_SOCKET_MAX_BUF (pre-call guard).
//   TEKO_SOCK_ERR_BADARG                    : NULL pointer or bad state.
TekoSocketStatus teko_socket_recv(TekoSocket* s, char* buf,
                                  uint32_t buf_len, uint32_t* out_received);

// Graceful close: closes the OS file descriptor; state -> CLOSED. Idempotent.
TekoSocketStatus teko_socket_close(TekoSocket* s);

// Close (if not already closed) then free the handle and remove it from the
// internal handle table. Calling teko_socket_free on a NULL pointer is a no-op.
void teko_socket_free(TekoSocket* s);

// Query the current connection state. NULL input -> TEKO_SOCK_ERROR.
TekoSocketState teko_socket_state(const TekoSocket* s);

#endif // TEKO_SOCKET_H
