// Phase 19 (T1b, Wave 0) — Unity KATs for teko_socket_server.
//
// Tests cover:
//   1. handle table alloc / free / NULL safety
//   2. connection cap enforcement (FULL state)
//   3. accept-loop cooperative state (AGAIN when nothing is pending)
//   4. loopback accept <-> connect round-trip (accept a real connection within the same
//      test process; send/recv a small payload)
//
// NATIVE-ONLY: the entire test file is wrapped in #if !defined(__wasm__) so it compiles
// clean when WASM targets are probed at build time.
//
// Windows notes:
//   - Winsock is initialised inside teko_server_open (WSAStartup call); no extra setup.
//   - connect() on Windows takes const struct sockaddr*; the code is identical.
//   - SOCKET (UINT_PTR) is handled by the intptr_t ABI in teko_socket_server.c.

#include "unity.h"

#if !defined(__wasm__)

#include "../../src/runtime/teko_socket_server.h"

// --- platform: connect a client socket to a port ---------------------------------

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
typedef SOCKET test_sock_t;
#  define TEST_INVALID_SOCK INVALID_SOCKET
static int test_wsa_ready = 0;
static void test_wsa_init(void) {
    if (!test_wsa_ready) {
        WSADATA w; WSAStartup(MAKEWORD(2,2), &w); test_wsa_ready = 1;
    }
}
static test_sock_t test_connect(uint16_t port) {
    test_wsa_init();
    test_sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == TEST_INVALID_SOCK) return TEST_INVALID_SOCK;
    struct sockaddr_in addr;
    {unsigned char* p=(unsigned char*)&addr; for(int i=0;i<(int)sizeof(addr);i++) p[i]=0;}
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, (struct sockaddr*)&addr, (int)sizeof(addr)) != 0) {
        closesocket(s); return TEST_INVALID_SOCK;
    }
    return s;
}
static void test_close_sock(test_sock_t s) { if (s != TEST_INVALID_SOCK) closesocket(s); }
static void test_send_all(test_sock_t s, const char* buf, int len) {
    int sent = 0;
    while (sent < len) {
        int r = send(s, buf + sent, len - sent, 0);
        if (r <= 0) break;
        sent += r;
    }
}
#else // POSIX
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <errno.h>
typedef int test_sock_t;
#  define TEST_INVALID_SOCK (-1)
static void test_wsa_init(void) {}
static test_sock_t test_connect(uint16_t port) {
    test_sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return TEST_INVALID_SOCK;
    struct sockaddr_in addr;
    {unsigned char* p=(unsigned char*)&addr; for(int i=0;i<(int)sizeof(addr);i++) p[i]=0;}
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, (struct sockaddr*)&addr, (socklen_t)sizeof(addr)) != 0) {
        close(s); return TEST_INVALID_SOCK;
    }
    return s;
}
static void test_close_sock(test_sock_t s) { if (s >= 0) close(s); }
static void test_send_all(test_sock_t s, const char* buf, int len) {
    int sent = 0;
    while (sent < len) {
        int r = (int)send(s, buf + sent, (size_t)(len - sent), 0);
        if (r <= 0) break;
        sent += r;
    }
}
#endif // platform

// A loopback port unlikely to collide on a CI runner. Tests that need a listening socket
// pick ephemeral-ish values starting here; each test uses a unique port to avoid
// TIME_WAIT collisions when tests run back-to-back with SO_REUSEADDR.
#define TEST_PORT_BASE 51700u

// --- KAT 1: alloc / free / NULL safety -------------------------------------------

void test_teko_server_alloc_free(void) {
    // A server on a high loopback port: bind, listen, then free immediately.
    TekoServer* s = teko_server_open((uint16_t)(TEST_PORT_BASE + 0), 4, 8);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(TEST_PORT_BASE + 0), teko_server_port(s));
    TEST_ASSERT_EQUAL_UINT32(0u, teko_server_conn_count(s));
    teko_server_free(s);
    // NULL free must be a safe no-op.
    teko_server_free(NULL);
}

// --- KAT 2: NULL/badarg guards ---------------------------------------------------

void test_teko_server_badarg(void) {
    // All public functions must handle NULL without crashing.
    TEST_ASSERT_EQUAL_INT(TEKO_SRV_BADARG, teko_server_accept(NULL, NULL));
    intptr_t fd = -1;
    TEST_ASSERT_EQUAL_INT(TEKO_SRV_BADARG, teko_server_accept(NULL, &fd));

    // recv: NULL buf or zero len -> BADARG.
    uint32_t n = 0;
    TEST_ASSERT_EQUAL_INT(TEKO_SRV_BADARG, teko_server_recv(-1, NULL, 16, &n));
    TEST_ASSERT_EQUAL_INT(TEKO_SRV_BADARG, teko_server_recv(-1, &n, 0, &n));

    // send: NULL buf or zero len -> BADARG.
    TEST_ASSERT_EQUAL_INT(TEKO_SRV_BADARG, teko_server_send(-1, NULL, 16));
    TEST_ASSERT_EQUAL_INT(TEKO_SRV_BADARG, teko_server_send(-1, &n, 0));

    // conn_count / port on NULL -> 0.
    TEST_ASSERT_EQUAL_UINT32(0u, teko_server_conn_count(NULL));
    TEST_ASSERT_EQUAL_UINT16(0u, teko_server_port(NULL));

    // conn_close on -1 fd -> no-op.
    teko_server_conn_close(NULL, -1);
}

// --- KAT 3: accept-loop AGAIN state (non-blocking, no client) --------------------

void test_teko_server_accept_again_when_idle(void) {
    // Bind a server but do NOT connect a client. teko_server_accept must return
    // TEKO_SRV_AGAIN immediately (non-blocking cooperative posture).
    TekoServer* s = teko_server_open((uint16_t)(TEST_PORT_BASE + 1), 4, 8);
    TEST_ASSERT_NOT_NULL(s);

    intptr_t conn_fd = -1;
    TekoServerStatus st = teko_server_accept(s, &conn_fd);
    TEST_ASSERT_EQUAL_INT(TEKO_SRV_AGAIN, st);
    TEST_ASSERT_EQUAL_INT(-1, (int)conn_fd); // must be -1 on non-OK

    teko_server_free(s);
}

// --- KAT 4: connection cap enforcement -------------------------------------------

void test_teko_server_cap_enforcement(void) {
    // Open a server with max_conns = 1.
    TekoServer* s = teko_server_open((uint16_t)(TEST_PORT_BASE + 2), 4, 1);
    TEST_ASSERT_NOT_NULL(s);

    // Connect one client and accept it.
    test_wsa_init();
    test_sock_t cli1 = test_connect((uint16_t)(TEST_PORT_BASE + 2));
    TEST_ASSERT_NOT_EQUAL(TEST_INVALID_SOCK, cli1);

    intptr_t conn1 = -1;
    // Retry a few times to let the kernel deliver the connection.
    TekoServerStatus st = TEKO_SRV_AGAIN;
    for (int i = 0; i < 1000 && st == TEKO_SRV_AGAIN; i++) st = teko_server_accept(s, &conn1);
    TEST_ASSERT_EQUAL_INT(TEKO_SRV_OK, st);
    TEST_ASSERT_NOT_EQUAL(-1, (int)conn1);
    TEST_ASSERT_EQUAL_UINT32(1u, teko_server_conn_count(s));

    // Now attempt a second accept. Cap is 1, so we expect TEKO_SRV_FULL
    // (NOT TEKO_SRV_AGAIN — the cap check fires before the accept syscall).
    test_sock_t cli2 = test_connect((uint16_t)(TEST_PORT_BASE + 2));
    // cli2 may succeed at the TCP level (kernel accept queue), but teko_server_accept
    // should return FULL because our cap is reached.
    intptr_t conn2 = -1;
    st = teko_server_accept(s, &conn2);
    TEST_ASSERT_EQUAL_INT(TEKO_SRV_FULL, st);
    TEST_ASSERT_EQUAL_INT(-1, (int)conn2);

    // Cleanup.
    teko_server_conn_close(s, conn1);
    TEST_ASSERT_EQUAL_UINT32(0u, teko_server_conn_count(s));
    test_close_sock(cli1);
    test_close_sock(cli2);
    teko_server_free(s);
}

// --- KAT 5: loopback accept + send/recv round-trip --------------------------------

void test_teko_server_loopback_roundtrip(void) {
    // Open server, connect a client, accept, send from client, recv on server side,
    // send a response, recv on client side. Verifies the full cooperative I/O path.
    TekoServer* s = teko_server_open((uint16_t)(TEST_PORT_BASE + 3), 4, 8);
    TEST_ASSERT_NOT_NULL(s);

    test_wsa_init();
    test_sock_t cli = test_connect((uint16_t)(TEST_PORT_BASE + 3));
    TEST_ASSERT_NOT_EQUAL(TEST_INVALID_SOCK, cli);

    // Accept the connection (retry loop to handle kernel timing).
    intptr_t conn = -1;
    TekoServerStatus st = TEKO_SRV_AGAIN;
    for (int i = 0; i < 1000 && st == TEKO_SRV_AGAIN; i++) st = teko_server_accept(s, &conn);
    TEST_ASSERT_EQUAL_INT(TEKO_SRV_OK, st);
    TEST_ASSERT_NOT_EQUAL(-1, (int)conn);

    // Client sends "PING".
    const char* ping = "PING";
    test_send_all(cli, ping, 4);

    // Server receives "PING" (retry loop for cooperative non-blocking posture).
    char buf[16];
    {unsigned char* p=(unsigned char*)buf; for(int i=0;i<16;i++) p[i]=0;}
    uint32_t nr = 0;
    st = TEKO_SRV_AGAIN;
    for (int i = 0; i < 10000 && st == TEKO_SRV_AGAIN; i++)
        st = teko_server_recv(conn, buf, 4, &nr);
    TEST_ASSERT_EQUAL_INT(TEKO_SRV_OK, st);
    TEST_ASSERT_EQUAL_UINT32(4u, nr);
    TEST_ASSERT_EQUAL_MEMORY("PING", buf, 4);

    // Server sends "PONG".
    st = teko_server_send(conn, "PONG", 4);
    TEST_ASSERT_EQUAL_INT(TEKO_SRV_OK, st);

    // Client receives "PONG".
    char rbuf[8];
    {unsigned char* p=(unsigned char*)rbuf; for(int i=0;i<8;i++) p[i]=0;}
    int r = 0;
    for (int i = 0; i < 10000 && r <= 0; i++) {
#if defined(_WIN32)
        r = recv(cli, rbuf, 4, 0);
        if (r < 0 && WSAGetLastError() == WSAEWOULDBLOCK) { r = 0; continue; }
#else
        r = (int)recv(cli, rbuf, 4, 0);
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { r = 0; continue; }
#endif
    }
    TEST_ASSERT_EQUAL_INT(4, r);
    TEST_ASSERT_EQUAL_MEMORY("PONG", rbuf, 4);

    // Cleanup.
    teko_server_conn_close(s, conn);
    test_close_sock(cli);
    teko_server_free(s);
}

#endif // !defined(__wasm__)
