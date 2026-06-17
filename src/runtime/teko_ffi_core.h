/*
 * teko_ffi_core.h — Phase-19 FFI-CORE: manifest FFI/deps section, deterministic resolver,
 * lockfile writer, and FFI-boundary marshalling/error-mapping helper.
 *
 * SCOPE (Wave 0, infra-only):
 *   - Data types for the unified .tkp manifest FFI/deps section (§0.3, §0.4 of
 *     docs/PHASE19_NETWORKING.md). Phase 21 owns the full manifest schema; this file
 *     defines ONLY the [deps] section and lockfile. See the comment "UNIFIED MANIFEST"
 *     below.
 *   - Backend hierarchy resolver: system → bundled → native, lazy-by-use.
 *   - Lockfile (.tkp.lock) writer: one line per resolved capability.
 *   - Build-time assertion: FAIL-LOUD with actionable per-OS message when nothing resolves.
 *   - FFI-boundary marshalling helpers: pointer+length safety, NUL-termination, width-correct
 *     casts (MSVC/Windows LLP64 safe), return-code→fail-loud mapping, lib-handle lifetime.
 *
 * WASM: the backend hierarchy (system/bundled/native) is NATIVE-ONLY. On WASM every heavy
 * capability uses host-import / WASI; the resolver short-circuits at compile time when
 * __wasm__ is defined (teko_ffi_resolve returns TEKO_FFI_WASM_ORTHOGONAL without probing).
 *
 * MSVC/Windows portability:
 *   - No C23 auto/nullptr in this header (shared code).
 *   - No POSIX headers unconditionally; <unistd.h>/access() guarded by #if !defined(_WIN32).
 *   - intptr_t/int32_t used for all width-sensitive casts.
 *   - Struct padding handled with standard layout (no TEKO_PACKED needed here — no wire
 *     protocol; all in-memory).
 *
 * Memory safety:
 *   - All allocations use calloc() for zero-init (no uninitialized pointer wild-free, the
 *     lesson from src/parser_ffi.c's ~50% Windows TSan crash).
 *   - Clear ownership: the parser owns manifest_ffi_t and its contents; the caller frees via
 *     teko_ffi_manifest_free(). The resolver writes a static lockfile record (no heap alloc
 *     in the locked record itself). Lib handles are freed via teko_ffi_lib_close().
 *
 * UNIFIED MANIFEST NOTE: .tkp is the unified manifest format shared with Phase-21
 * .teko_meta. Phase 19 defines ONLY the [deps] section here. Phase 21 will extend the
 * schema to cover the full manifest; FFI-CORE's parser must be updated at that point to
 * participate in the unified parse rather than owning its own top-level reader.
 */

#ifndef TEKO_FFI_CORE_H
#define TEKO_FFI_CORE_H

#include <stddef.h>   /* size_t, NULL */
#include <stdint.h>   /* uint32_t, intptr_t */

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * 1. Capability identifiers (the Phase-19 FFI capabilities known to the
 *    resolver).  Each capability has ONE backend hierarchy slot in the
 *    manifest [deps] section.
 * ---------------------------------------------------------------------- */

typedef enum {
    TEKO_CAP_TLS      = 0,  /* tls.* surface — OpenSSL / SChannel / Secure Transport */
    TEKO_CAP_BROTLI   = 1,  /* compress.brotli */
    TEKO_CAP_ZSTD     = 2,  /* compress.zstd */
    TEKO_CAP_LZMA     = 3,  /* compress.lzma */
    TEKO_CAP_DEFLATE  = 4,  /* compress.deflate/gzip/zlib — has a 'native' teko backend */
    TEKO_CAP__COUNT   = 5   /* sentinel — always last */
} TekoCap;

/* Human-readable names (index == TekoCap value). */
extern const char* const TEKO_CAP_NAMES[TEKO_CAP__COUNT];

/* -------------------------------------------------------------------------
 * 2. Backend kind — matches the §0.3 hierarchy (highest preference first).
 * ---------------------------------------------------------------------- */

typedef enum {
    TEKO_BACKEND_SYSTEM   = 0, /* OS library, found by build-time probe */
    TEKO_BACKEND_BUNDLED  = 1, /* teko-designated, pinned+vendored library */
    TEKO_BACKEND_NATIVE   = 2, /* teko's own implementation (optional; DEFLATE today) */
    TEKO_BACKEND__COUNT   = 3
} TekoBackendKind;

/* -------------------------------------------------------------------------
 * 3. Manifest [deps] section: one entry per declared capability.
 *    The parser fills this from the .tkp text; unused capabilities may be
 *    absent (LAZY-BY-USE: absent == not declared, never probed).
 * ---------------------------------------------------------------------- */

#define TEKO_FFI_MAX_CAPS     16   /* room for future capabilities without realloc */
#define TEKO_FFI_MAX_STR      256  /* max bytes for a library name / version / path */

typedef struct {
    TekoCap         cap;                         /* which capability */
    TekoBackendKind order[TEKO_BACKEND__COUNT];  /* preferred backends, in declaration order */
    int             order_count;                 /* 1..3 */
    /* Per-backend optional override fields (may be empty string = "use default"). */
    char            system_lib[TEKO_FFI_MAX_STR];   /* e.g. "ssl" → -lssl probe */
    char            bundled_name[TEKO_FFI_MAX_STR];  /* e.g. "boringssl" */
    char            bundled_ver[TEKO_FFI_MAX_STR];   /* e.g. "2024.01.30" */
    char            native_note[TEKO_FFI_MAX_STR];   /* descriptive; no binary meaning */
} TekoDepsEntry;

typedef struct {
    TekoDepsEntry   entries[TEKO_FFI_MAX_CAPS];
    int             count;  /* number of valid entries */
} TekoDepsManifest;

/* -------------------------------------------------------------------------
 * 4. Resolver result — one record per capability that was USED (not merely
 *    declared).  Written verbatim into the .tkp.lock file.
 * ---------------------------------------------------------------------- */

typedef enum {
    TEKO_FFI_RESOLVED_OK         = 0, /* a backend was found and locked */
    TEKO_FFI_RESOLVED_UNUSED     = 1, /* declared but not used — warning, not error */
    TEKO_FFI_RESOLVED_NOT_FOUND  = 2, /* nothing in the chain resolved — build error */
    TEKO_FFI_WASM_ORTHOGONAL     = 3  /* WASM target: hierarchy does not apply */
} TekoFfiResolution;

typedef struct {
    TekoCap         cap;
    TekoFfiResolution resolution;
    TekoBackendKind winning_backend;           /* valid when RESOLVED_OK */
    char            winning_lib[TEKO_FFI_MAX_STR];    /* lib name as probed/chosen */
    char            winning_ver[TEKO_FFI_MAX_STR];    /* version string, may be "unknown" */
} TekoFfiLockRecord;

typedef struct {
    TekoFfiLockRecord records[TEKO_FFI_MAX_CAPS];
    int               count;
} TekoFfiLockfile;

/* -------------------------------------------------------------------------
 * 5. Manifest parser API.
 *    Input: NUL-terminated .tkp text (attacker-controlled; parser is
 *    bounds-safe and injection-proof).  Allocates with calloc; caller
 *    frees with teko_ffi_manifest_free().
 *
 *    Returns NULL on allocation failure or unrecoverable parse error
 *    (error detail written to errbuf[errbuf_len]).
 * ---------------------------------------------------------------------- */

TekoDepsManifest* teko_ffi_manifest_parse(const char* text, size_t text_len,
                                          char* errbuf, size_t errbuf_len);
void              teko_ffi_manifest_free(TekoDepsManifest* m);

/* -------------------------------------------------------------------------
 * 6. Resolver API.
 *    used_caps: bitmask of TekoCap values that are actually referenced by
 *    the program being compiled (bit 1<<cap is set when cap is used).
 *    On WASM (__wasm__ or target_is_wasm != 0) every entry becomes
 *    TEKO_FFI_WASM_ORTHOGONAL without probing.
 *
 *    Returns a heap-allocated TekoFfiLockfile (calloc).  Caller frees with
 *    teko_ffi_lockfile_free().
 *
 *    If any USED capability resolves to TEKO_FFI_RESOLVED_NOT_FOUND, the
 *    function writes a FAIL-LOUD actionable message to stderr AND sets
 *    *out_fail_count > 0.  The caller should abort the build.
 * ---------------------------------------------------------------------- */

TekoFfiLockfile*  teko_ffi_resolve(const TekoDepsManifest* m,
                                   uint32_t used_caps_mask,
                                   int target_is_wasm,
                                   int* out_fail_count);
void              teko_ffi_lockfile_free(TekoFfiLockfile* lf);

/* Write the lockfile to disk.  path must be a valid writable path; the file
 * is written atomically (write to path.tmp then rename).  Returns 0 on
 * success, -1 on error (errno set). */
int               teko_ffi_lockfile_write(const TekoFfiLockfile* lf, const char* path);

/* Read an existing lockfile from disk (for reproducible re-runs without
 * re-probing).  Returns NULL if the file does not exist or is malformed. */
TekoFfiLockfile*  teko_ffi_lockfile_read(const char* path,
                                         char* errbuf, size_t errbuf_len);

/* -------------------------------------------------------------------------
 * 7. Build-time probe helpers — used by the resolver to check whether a
 *    system library is present.  Returns 1 if found, 0 if absent.
 *    lib_name is the bare library name (e.g. "ssl", "zstd") — the probe
 *    uses pkg-config / CMake find-path logic at build time; at runtime
 *    (unit-test context) it checks for the shared object via dlopen/
 *    LoadLibrary with RTLD_NOLOAD / GET_MODULE_HANDLE.
 * ---------------------------------------------------------------------- */

int teko_ffi_probe_system_lib(const char* lib_name);

/* -------------------------------------------------------------------------
 * 8. FFI-boundary marshalling helpers.
 *    These wrap the thin layer between teko values and system-lib C ABIs.
 *
 *    All pointer+length pairs are validated before use; NUL-termination is
 *    asserted at copy time; width-correct casts use intptr_t/int32_t so
 *    Windows LLP64 cannot silently truncate.  On any invariant violation
 *    the helpers call teko_ffi_panic() which writes to stderr and aborts —
 *    never a silent stub.
 * ---------------------------------------------------------------------- */

/* Opaque handle to a dynamically loaded library (dlopen/LoadLibrary). */
typedef struct TekoDylibHandle TekoDylibHandle;

/* Open a shared library by name (e.g. "libssl.so.3", "ssl", "zstd").
 * Returns NULL on failure; writes diagnostic to errbuf[errbuf_len].
 * Uses calloc for the handle struct. */
TekoDylibHandle*  teko_ffi_lib_open(const char* lib_name,
                                    char* errbuf, size_t errbuf_len);

/* Look up a symbol in a previously opened library.  Returns NULL if not
 * found (non-fatal; caller decides whether to abort). */
void*             teko_ffi_lib_sym(TekoDylibHandle* h, const char* symbol);

/* Close and free a library handle.  Safe to call with NULL. */
void              teko_ffi_lib_close(TekoDylibHandle* h);

/* Copy at most dst_cap-1 bytes from src[0..src_len) into dst, always
 * NUL-terminating.  Panics (abort) if dst is NULL or dst_cap == 0.
 * Returns the number of bytes written (excluding NUL). */
size_t teko_ffi_str_copy(char* dst, size_t dst_cap,
                         const char* src, size_t src_len);

/* Width-correct cast: intptr_t → int32_t with range check.
 * Panics if the value does not fit in int32_t (overflow guard for LLP64). */
int32_t teko_ffi_narrow_i32(intptr_t v);

/* Map a system-library integer return code to a fail-loud error.
 * If rc < 0 (POSIX convention) or rc != ok_value (custom convention):
 *   writes "TEKO FFI error: <context>: rc=<rc>" to stderr and calls abort().
 * Returns rc unchanged when it signals success.
 * context must be a string literal (not user-supplied — no format-string risk). */
int teko_ffi_check_rc(int rc, int ok_value, const char* context);

/* Same but for pointer returns: panics when ptr == NULL. */
void* teko_ffi_check_ptr(void* ptr, const char* context);

/* Explicit panic entry point (called by the helpers above; also available
 * to callers that need a custom message). Writes msg to stderr, flushes,
 * then calls abort().  msg must not contain format specifiers (not a
 * printf-family call — injection-safe). */
_Noreturn void teko_ffi_panic(const char* msg);

#ifdef __cplusplus
}
#endif

#endif /* TEKO_FFI_CORE_H */
