// Native CSPRNG wrapper (Phase 13.3a). Each platform routes to its OS entropy source;
// no external libraries. See the header for the WASM-host-import note.

#include "teko_crypto_random.h"

#if defined(__wasm__)

// WASM target: there is no OS entropy source in the module. Route to the host
// import `env.teko_random(ptr, len)` — the SAME entropy import the emitted Teko
// module already declares for `random.bytes`/`uuid.v4`/`v7` (Node:
// crypto.randomFillSync; browser: crypto.getRandomValues). The import name is
// fixed via attributes so wasm-ld emits it without `--allow-undefined`.
__attribute__((import_module("env"), import_name("teko_random")))
extern void teko_rt_host_random(uint8_t* out, uint32_t len);

int teko_csprng_bytes(uint8_t* out, size_t len) {
    if (len == 0u) return 0;
    if (!out) return -1;
    teko_rt_host_random(out, (uint32_t)len);
    return 0;
}

#elif defined(_WIN32)

#include <windows.h>
#include <bcrypt.h>

int teko_csprng_bytes(uint8_t* out, size_t len) {
    if (len == 0u) return 0;
    if (!out) return -1;
    {
        NTSTATUS status = BCryptGenRandom(NULL, (PUCHAR)out, (ULONG)len,
                                          BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        return BCRYPT_SUCCESS(status) ? 0 : -1;
    }
}

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
      defined(__NetBSD__) || defined(__DragonFly__)

#include <stdlib.h> // arc4random_buf — always seeded, cannot fail

int teko_csprng_bytes(uint8_t* out, size_t len) {
    if (len == 0u) return 0;
    if (!out) return -1;
    arc4random_buf(out, len);
    return 0;
}

#else // Linux and other POSIX

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/random.h> // getrandom
#endif

static int teko_csprng_dev_urandom(uint8_t* out, size_t len) {
    int fd = open("/dev/urandom", O_RDONLY);
    size_t off = 0u;
    if (fd < 0) return -1;
    while (off < len) {
        ssize_t r = read(fd, out + off, len - off);
        if (r <= 0) {
            if (r < 0 && errno == EINTR) continue;
            close(fd);
            return -1;
        }
        off += (size_t)r;
    }
    close(fd);
    return 0;
}

int teko_csprng_bytes(uint8_t* out, size_t len) {
    if (len == 0u) return 0;
    if (!out) return -1;
#if defined(__linux__)
    {
        size_t off = 0u;
        while (off < len) {
            ssize_t r = getrandom(out + off, len - off, 0);
            if (r < 0) {
                if (errno == EINTR) continue;
                break; // fall through to /dev/urandom
            }
            off += (size_t)r;
        }
        if (off == len) return 0;
    }
#endif
    return teko_csprng_dev_urandom(out, len);
}

#endif
