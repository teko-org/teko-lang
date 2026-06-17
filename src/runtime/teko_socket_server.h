#ifndef TEKO_SOCKET_SERVER_H
#define TEKO_SOCKET_SERVER_H

// Phase 19 (T1b, Wave 0) — server socket runtime: bind / listen / accept loop.
//
// NATIVE-ONLY: this entire translation unit is wrapped in #if !defined(__wasm__).
// The WASM reactor must NEVER compile it; the browser has no listening sockets and
// the WASI path is handled separately. Exclude this file from
// runtime/wasm/crypto/build-crypto-reactor.sh SRCS and EXPORTS.
//
// SEPARATE TU (same pattern as teko_rt_sched.c): kept in its own object file so the
// linker pulls it ONLY when a produced binary references teko_server_open / _free.
// Programs that do not use a listening server never link this TU and stay byte-identical
// to before Phase 19.
//
// OP_CALL_RUNTIME id range 70-79 is PRE-ALLOCATED for net-server ops.
// NO opcodes are emitted this wave — emission is deferred to Track T2 (Wave 1).
//
// Cooperative C1 posture (Phase-14 scheduler compatible):
//   teko_server_accept and teko_server_recv are NON-BLOCKING: they return
//   TEKO_SRV_AGAIN immediately when no connection / data is pending so the
//   Phase-14 cooperative scheduler loop can call teko_rt_run() and retry without
//   ever blocking the OS thread in the kernel.
//
// Hardening (SAST-mandated for attacker-controlled accept surface):
//   - Connection cap (TEKO_SERVER_MAX_CONNS) prevents fd exhaustion.
//   - Per-connection recv buf is caller-supplied with an explicit length — no
//     implicit copies, no format-string paths, no path traversal.
//   - teko_server_send retries internally on EAGAIN/EWOULDBLOCK (bounded) so
//     the caller never issues a partial write.
//   - LLP64 correctness: socket handles are SOCKET (UINT_PTR, 64-bit on Windows
//     x64); intptr_t holds them without truncation on every ABI.

#if !defined(__wasm__)

#include <stdint.h>

// OP_CALL_RUNTIME net-server id range: 70-79 (reserved; emission deferred to T2).

#define TEKO_SERVER_MAX_CONNS   256u  // hard cap on simultaneous open connections
#define TEKO_SERVER_MAX_BACKLOG 128u  // listen() backlog cap

// Status codes returned by server operations. Values are stable (they will map to
// OP_CALL_RUNTIME surface codes in T2). Zero = success; positive = structured
// non-error status (cooperative yield, cap reached, peer-close); 5 = hard OS error.
typedef enum {
    TEKO_SRV_OK      = 0, // success (accept gave a connection; recv returned data; send complete)
    TEKO_SRV_AGAIN   = 1, // no connection / no data available yet; caller should yield and retry
    TEKO_SRV_FULL    = 2, // max_conns cap reached; caller should yield and retry later
    TEKO_SRV_CLOSED  = 3, // server has been closed (accept), or peer closed connection (recv/send)
    TEKO_SRV_BADARG  = 4, // NULL server pointer or other invalid argument
    TEKO_SRV_ERR     = 5  // hard OS error (bind/listen/accept/recv/send syscall failure)
} TekoServerStatus;

// Opaque server instance. Allocate with teko_server_open; free with teko_server_free.
// The definition lives in teko_socket_server.c — callers hold a pointer only.
typedef struct TekoServer TekoServer;

// --- lifecycle -------------------------------------------------------------------

// Allocate, bind to `port` on all interfaces (INADDR_ANY / in6addr_any), listen, and
// return a ready-to-accept server handle. `backlog` is clamped to
// TEKO_SERVER_MAX_BACKLOG. `max_conns` is clamped to TEKO_SERVER_MAX_CONNS.
// The listening socket is set NON-BLOCKING so teko_server_accept never blocks.
// Returns NULL on allocation failure or OS error (bind/listen failure); the caller
// should treat NULL as TEKO_SRV_ERR and emit a diagnostic.
TekoServer* teko_server_open(uint16_t port, uint32_t backlog, uint32_t max_conns);

// Close the listening socket, close all tracked open connection fds, and free the
// server struct. Safe to call on NULL (no-op). After this call the pointer is invalid.
void teko_server_free(TekoServer* s);

// --- connection accept -----------------------------------------------------------

// Non-blocking try-accept. If a connection is pending, sets *out_conn_fd to the new
// accepted socket fd (as intptr_t) and returns TEKO_SRV_OK. The accepted socket is
// also set NON-BLOCKING. If no connection is pending at this instant, returns
// TEKO_SRV_AGAIN (the caller should run the Phase-14 scheduler and retry). Returns
// TEKO_SRV_FULL when the internal connection count >= max_conns (no accept attempted;
// caller should yield). Returns TEKO_SRV_BADARG on NULL s or NULL out_conn_fd.
// Returns TEKO_SRV_CLOSED if the server has been closed. Returns TEKO_SRV_ERR on a
// hard OS error. On every non-OK return *out_conn_fd is set to -1.
TekoServerStatus teko_server_accept(TekoServer* s, intptr_t* out_conn_fd);

// --- per-connection I/O ---------------------------------------------------------

// Non-blocking recv from an accepted connection fd. Reads up to `len` bytes into
// buf[0..len). On success sets *out_n to the number of bytes read and returns
// TEKO_SRV_OK. Returns TEKO_SRV_AGAIN if no data is available yet (cooperative:
// caller yields and retries). Returns TEKO_SRV_CLOSED when the peer has closed the
// connection (zero-byte read = EOF). Returns TEKO_SRV_BADARG if buf is NULL or
// len == 0. Returns TEKO_SRV_ERR on hard OS error. *out_n is set to 0 on any
// non-OK return. The fd is NOT closed by this function.
TekoServerStatus teko_server_recv(intptr_t conn_fd, void* buf, uint32_t len, uint32_t* out_n);

// Blocking-ish send of all `len` bytes in buf[0..len). Internally retries on
// EAGAIN/EWOULDBLOCK (bounded: up to TEKO_SERVER_MAX_CONNS send iterations) so
// the caller never has to handle a partial write. Returns TEKO_SRV_OK when all
// bytes are sent. Returns TEKO_SRV_CLOSED if the peer closed the connection during
// the send. Returns TEKO_SRV_BADARG if buf is NULL or len == 0. Returns
// TEKO_SRV_ERR on a hard OS error. The fd is NOT closed by this function.
TekoServerStatus teko_server_send(intptr_t conn_fd, const void* buf, uint32_t len);

// Close one accepted connection fd. Safe to call with -1 (no-op). Decrements the
// server's internal open-connection counter so new connections can be accepted.
// Pass the TekoServer* so the counter is updated; pass NULL only for raw fd close
// (counter NOT decremented in that case — prefer always passing the server ptr).
void teko_server_conn_close(TekoServer* s, intptr_t conn_fd);

// --- state queries ---------------------------------------------------------------

// Current number of open (accepted, not yet closed) connections. Returns 0 if s is NULL.
uint32_t teko_server_conn_count(const TekoServer* s);

// Listening port (as passed to teko_server_open, not the OS-assigned ephemeral port
// if 0 was passed). Returns 0 if s is NULL.
uint16_t teko_server_port(const TekoServer* s);

#endif // !defined(__wasm__)

#endif // TEKO_SOCKET_SERVER_H
