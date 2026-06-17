/*
 * teko_ffi_core.c — Phase-19 FFI-CORE implementation.
 *
 * Implements:
 *   1. Manifest [deps] parser (bounds-checked text scanner; attacker-controlled input).
 *   2. Deterministic resolver: system → bundled → native; lazy-by-use; WASM short-circuit.
 *   3. Lockfile writer (.tkp.lock) and reader.
 *   4. Build-time system-library probe (dlopen/LoadLibrary with RTLD_NOLOAD).
 *   5. FFI-boundary marshalling helpers: str_copy, narrow_i32, check_rc, check_ptr,
 *      lib_open/sym/close, panic.
 *
 * Memory safety invariants (enforced here, not assumed):
 *   - calloc() for every heap allocation; no malloc().
 *   - Every pointer written into a struct is either NULL (calloc-zeroed) or owned.
 *   - Clear ownership: teko_ffi_manifest_free() and teko_ffi_lockfile_free() are the
 *     ONLY places that free manifest/lockfile objects.
 *   - teko_ffi_lib_close() is safe to call with NULL.
 *
 * MSVC / Windows portability:
 *   - <unistd.h> and dlopen/dlfcn.h are guarded by #if !defined(_WIN32).
 *   - LoadLibrary / GetProcAddress used on Windows.
 *   - intptr_t / int32_t for all width-sensitive values.
 *   - snprintf used for all string formatting (no sprintf).
 */

#include "teko_ffi_core.h"

#include <stdio.h>    /* fprintf, stderr, snprintf */
#include <stdlib.h>   /* calloc, free, abort */
#include <string.h>   /* memcpy, memset, strncmp, strlen, strchr */
#include <stdint.h>   /* uint32_t, intptr_t, int32_t */
#include <stddef.h>   /* size_t, NULL */
#include <errno.h>    /* errno */

#if !defined(_WIN32)
#  include <unistd.h>    /* access() for file-exists check */
#  include <dlfcn.h>     /* dlopen, dlsym, dlclose */
#endif

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>   /* LoadLibraryA, GetProcAddress, FreeLibrary, GetModuleHandleA */
#endif

/* -------------------------------------------------------------------------
 * Capability name table (public, matches the TekoCap enum order).
 * ---------------------------------------------------------------------- */

const char* const TEKO_CAP_NAMES[TEKO_CAP__COUNT] = {
    "tls",
    "brotli",
    "zstd",
    "lzma",
    "deflate"
};

/* -------------------------------------------------------------------------
 * Internal helpers: bounded string scanning (no strcpy/strcat without
 * length; no strtok/sscanf on attacker-controlled input).
 * ---------------------------------------------------------------------- */

/* Skip ASCII whitespace (space, tab, CR, LF).  Returns pointer past whitespace. */
static const char* skip_ws(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
        ++p;
    return p;
}

/* Skip INLINE whitespace only (space + tab; not CR/LF).
 * Used inside backend-list parsing so that newlines terminate the list. */
static const char* skip_inline_ws(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t'))
        ++p;
    return p;
}

/* Skip to end of line (does not consume the newline). */
static const char* skip_to_eol(const char* p, const char* end) {
    while (p < end && *p != '\n' && *p != '\r')
        ++p;
    return p;
}

/* Copy a bare token (alphanumeric + '.' + '-' + '_') into dst[dst_cap].
 * Returns length of token, 0 if empty.  Never writes beyond dst_cap-1.
 * This is not strcpy — it copies from a bounded, non-NUL-terminated buffer. */
static size_t copy_token(char* dst, size_t dst_cap,
                         const char* p, const char* end) {
    size_t n = 0;
    while (p < end && n + 1 < dst_cap) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_') {
            dst[n++] = c;
            ++p;
        } else {
            break;
        }
    }
    dst[n] = '\0';
    return n;
}

/* Write to errbuf safely (snprintf, always NUL-terminates). */
static void set_err(char* errbuf, size_t errbuf_len, const char* msg) {
    if (!errbuf || errbuf_len == 0) return;
    /* Use snprintf to avoid format-string risk — msg is a literal from our code. */
    (void)snprintf(errbuf, errbuf_len, "%s", msg);
}

/* -------------------------------------------------------------------------
 * Manifest parser — parses the [deps] section of a .tkp text file.
 *
 * Expected format (line-oriented, subset):
 *
 *   [deps]
 *   tls      = system, bundled:boringssl@2024.01.30
 *   brotli   = bundled:brotli@1.1.0
 *   deflate  = system, bundled:zlib-ng@2.2.1, native
 *   # comment line
 *
 * Grammar (informal):
 *   deps_section := '[deps]' NL entry*
 *   entry        := cap_name WS* '=' WS* backend_list NL
 *   backend_list := backend (',' WS* backend)*
 *   backend      := 'system' (':' lib_name)?
 *                 | 'bundled' ':' lib_name ('@' version)?
 *                 | 'native'
 *   cap_name     := ALPHA_IDENT
 *
 * Parsing is deliberately strict on bounds and lenient on unknown cap names
 * (unknown caps are skipped with a warning to errbuf — not an error — so
 * future Phase-21 capabilities don't break a Phase-19 build).
 *
 * Security properties:
 *   - text_len is always checked before every byte read (no OOB).
 *   - All string copies go through copy_token (bounded; no null needed from src).
 *   - Rejects entries that would exceed TEKO_FFI_MAX_CAPS (truncation attack guard).
 *   - Integer arithmetic: entry count is checked against TEKO_FFI_MAX_CAPS before
 *     incrementing — no overflow possible (TEKO_FFI_MAX_CAPS = 16, fits in int).
 * ---------------------------------------------------------------------- */

TekoDepsManifest* teko_ffi_manifest_parse(const char* text, size_t text_len,
                                          char* errbuf, size_t errbuf_len) {
    if (!text || text_len == 0) {
        set_err(errbuf, errbuf_len, "teko_ffi_manifest_parse: null/empty input");
        return NULL;
    }

    TekoDepsManifest* m = (TekoDepsManifest*)calloc(1, sizeof(TekoDepsManifest));
    if (!m) {
        set_err(errbuf, errbuf_len, "teko_ffi_manifest_parse: calloc failed");
        return NULL;
    }

    const char* p   = text;
    const char* end = text + text_len;

    /* Scan forward to the [deps] section header. */
    int in_deps = 0;
    while (p < end) {
        p = skip_ws(p, end);
        if (p >= end) break;
        /* Comment? */
        if (*p == '#') { p = skip_to_eol(p, end); continue; }
        /* Section header? */
        if (*p == '[') {
            const char* hdr = p + 1;
            /* Find the closing ']'. */
            const char* rbr = hdr;
            while (rbr < end && *rbr != ']' && *rbr != '\n') ++rbr;
            if (rbr >= end || *rbr != ']') {
                p = skip_to_eol(p, end);
                continue;
            }
            /* Is it [deps]? */
            size_t hlen = (size_t)(rbr - hdr);
            if (hlen == 4 && strncmp(hdr, "deps", 4) == 0) {
                in_deps = 1;
            } else {
                in_deps = 0;  /* entering a different section */
            }
            p = rbr + 1;  /* past ']' */
            p = skip_to_eol(p, end);
            continue;
        }
        if (!in_deps) {
            p = skip_to_eol(p, end);
            continue;
        }
        /* Entry line: cap_name = backend_list */
        char cap_name[64];
        size_t cap_len = copy_token(cap_name, sizeof(cap_name), p, end);
        if (cap_len == 0) { p = skip_to_eol(p, end); continue; }
        p += cap_len;
        p = skip_ws(p, end);
        if (p >= end || *p != '=') { p = skip_to_eol(p, end); continue; }
        ++p; /* skip '=' */
        p = skip_ws(p, end);

        /* Map cap_name → TekoCap. Skip unknown caps (future-proof). */
        TekoCap cap_id = TEKO_CAP__COUNT; /* sentinel = unknown */
        for (int i = 0; i < TEKO_CAP__COUNT; ++i) {
            if (strcmp(cap_name, TEKO_CAP_NAMES[i]) == 0) {
                cap_id = (TekoCap)i;
                break;
            }
        }
        if (cap_id == TEKO_CAP__COUNT) {
            /* Unknown capability — skip the line (future Phase-21 cap). */
            p = skip_to_eol(p, end);
            continue;
        }
        if (m->count >= TEKO_FFI_MAX_CAPS) {
            set_err(errbuf, errbuf_len,
                    "teko_ffi_manifest_parse: too many dep entries");
            /* Non-fatal truncation: stop adding entries but return what we have. */
            break;
        }

        TekoDepsEntry* e = &m->entries[m->count];
        e->cap = cap_id;

        /* Parse the backend list (comma-separated).
         * skip_inline_ws (space/tab only) is used INSIDE the list so that a
         * newline terminates the entry rather than running into the next line. */
        while (p < end && *p != '\n' && *p != '\r') {
            p = skip_inline_ws(p, end);
            if (p >= end || *p == '\n' || *p == '\r' || *p == '#') break;

            char bknd[64];
            size_t blen = copy_token(bknd, sizeof(bknd), p, end);
            if (blen == 0) break;
            p += blen;

            TekoBackendKind bk = TEKO_BACKEND__COUNT; /* unknown */
            if (strcmp(bknd, "system")  == 0) bk = TEKO_BACKEND_SYSTEM;
            else if (strcmp(bknd, "bundled") == 0) bk = TEKO_BACKEND_BUNDLED;
            else if (strcmp(bknd, "native")  == 0) bk = TEKO_BACKEND_NATIVE;

            if (bk != TEKO_BACKEND__COUNT && e->order_count < TEKO_BACKEND__COUNT) {
                e->order[e->order_count++] = bk;
            }

            /* Optional ':' qualifier after backend keyword. */
            if (p < end && *p == ':') {
                ++p; /* skip ':' */
                /* lib_name[@version] — copy into the appropriate field. */
                char qual[TEKO_FFI_MAX_STR];
                /* qual may contain alphanumeric + '.' + '-' + '_' + '@' */
                size_t qn = 0;
                while (p < end && qn + 1 < sizeof(qual)) {
                    char c = *p;
                    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '-' ||
                        c == '_' || c == '@') {
                        qual[qn++] = c;
                        ++p;
                    } else break;
                }
                qual[qn] = '\0';

                /* Split on '@' for name vs version. */
                char lib_part[TEKO_FFI_MAX_STR];
                char ver_part[TEKO_FFI_MAX_STR];
                const char* at = strchr(qual, '@');
                if (at) {
                    size_t name_len = (size_t)(at - qual);
                    if (name_len >= TEKO_FFI_MAX_STR) name_len = TEKO_FFI_MAX_STR - 1;
                    memcpy(lib_part, qual, name_len);
                    lib_part[name_len] = '\0';
                    /* Version: after '@'; bounded copy. */
                    size_t ver_len = strlen(at + 1);
                    if (ver_len >= TEKO_FFI_MAX_STR) ver_len = TEKO_FFI_MAX_STR - 1;
                    memcpy(ver_part, at + 1, ver_len);
                    ver_part[ver_len] = '\0';
                } else {
                    /* No version: copy the whole qualifier as the lib name. */
                    size_t llen = strlen(qual);
                    if (llen >= TEKO_FFI_MAX_STR) llen = TEKO_FFI_MAX_STR - 1;
                    memcpy(lib_part, qual, llen);
                    lib_part[llen] = '\0';
                    ver_part[0] = '\0';
                }

                /* Store into the appropriate entry field. */
                if (bk == TEKO_BACKEND_SYSTEM) {
                    (void)snprintf(e->system_lib, TEKO_FFI_MAX_STR, "%s", lib_part);
                } else if (bk == TEKO_BACKEND_BUNDLED) {
                    (void)snprintf(e->bundled_name, TEKO_FFI_MAX_STR, "%s", lib_part);
                    (void)snprintf(e->bundled_ver,  TEKO_FFI_MAX_STR, "%s", ver_part);
                } else if (bk == TEKO_BACKEND_NATIVE) {
                    (void)snprintf(e->native_note, TEKO_FFI_MAX_STR, "%s", lib_part);
                }
            }

            /* Advance past optional comma (inline whitespace only — do not
             * cross a newline, which would incorrectly merge the next entry). */
            p = skip_inline_ws(p, end);
            if (p < end && *p == ',') { ++p; }
        }

        /* Only add the entry if at least one valid backend was parsed. */
        if (e->order_count > 0) {
            m->count++;
        }

        p = skip_to_eol(p, end);
    }

    return m;
}

void teko_ffi_manifest_free(TekoDepsManifest* m) {
    if (!m) return;
    free(m);
}

/* -------------------------------------------------------------------------
 * Resolver + lockfile.
 *
 * The resolver walks each TekoDepsEntry in the manifest and, for each
 * capability that is in used_caps_mask, tries the backends in declaration
 * order.  The first one that probes successfully wins.
 *
 * For TEKO_BACKEND_SYSTEM, teko_ffi_probe_system_lib() is called.
 * For TEKO_BACKEND_BUNDLED, the bundled library is considered always
 * available (teko ships it vendored; if it is missing the build system
 * would have failed earlier).
 * For TEKO_BACKEND_NATIVE, DEFLATE is always available (built into teko);
 * other capabilities have no native backend and are skipped.
 *
 * FAIL-LOUD: if a USED capability has no resolving backend, the resolver
 * emits an actionable stderr message and increments *out_fail_count.
 *
 * Determinism: the resolver always processes capabilities in the order they
 * appear in the manifest (no hash/map non-determinism).
 * ---------------------------------------------------------------------- */

/* Designated bundled defaults (per §0.3). */
static const char* BUNDLED_DEFAULT[TEKO_CAP__COUNT] = {
    "boringssl",   /* TLS */
    "brotli",      /* BROTLI */
    "libzstd",     /* ZSTD */
    "liblzma",     /* LZMA */
    "zlib-ng"      /* DEFLATE */
};

static const char* BUNDLED_DEFAULT_VER[TEKO_CAP__COUNT] = {
    "bundled",
    "bundled",
    "bundled",
    "bundled",
    "bundled"
};

/* System library names to probe for each capability (POSIX/Windows conventions). */
static const char* SYSTEM_LIB_DEFAULT[TEKO_CAP__COUNT] = {
#if defined(_WIN32)
    "ssleay32",   /* TLS on Windows — try OpenSSL/SChannel */
    "brotlidec",  /* brotli */
    "zstd",       /* zstd */
    "lzma",       /* lzma */
    "zlib"        /* deflate/zlib */
#else
    "ssl",        /* TLS */
    "brotlidec",  /* brotli */
    "zstd",       /* zstd */
    "lzma",       /* lzma */
    "z"           /* deflate/zlib */
#endif
};

/* Fail-loud message template per capability. */
static void print_not_found_msg(TekoCap cap) {
    fprintf(stderr,
        "TEKO FFI error: capability '%s' has no available backend.\n"
        "  To resolve, choose one of:\n"
        "    - Install the system library (see per-OS notes below).\n"
        "    - Add 'bundled' to the backend list in your .tkp [deps] section.\n"
        "    - For deflate/gzip: add 'native' to use teko's built-in DEFLATE.\n"
        "\n"
        "  Linux (apt):  sudo apt-get install lib%s-dev\n"
        "  macOS:        brew install %s\n"
        "  Windows:      vcpkg install %s or add 'bundled' in .tkp\n"
        "\n",
        TEKO_CAP_NAMES[cap],
        TEKO_CAP_NAMES[cap],
        TEKO_CAP_NAMES[cap],
        TEKO_CAP_NAMES[cap]
    );
}

TekoFfiLockfile* teko_ffi_resolve(const TekoDepsManifest* m,
                                  uint32_t used_caps_mask,
                                  int target_is_wasm,
                                  int* out_fail_count) {
    TekoFfiLockfile* lf = (TekoFfiLockfile*)calloc(1, sizeof(TekoFfiLockfile));
    if (!lf) {
        fprintf(stderr, "teko_ffi_resolve: calloc failed\n");
        abort();
    }
    if (out_fail_count) *out_fail_count = 0;

    if (!m) return lf;

    for (int i = 0; i < m->count; ++i) {
        const TekoDepsEntry* e = &m->entries[i];
        if (lf->count >= TEKO_FFI_MAX_CAPS) break; /* safety cap */

        TekoFfiLockRecord* rec = &lf->records[lf->count];
        rec->cap = e->cap;

        /* WASM: hierarchy does not apply. */
        if (target_is_wasm) {
            rec->resolution = TEKO_FFI_WASM_ORTHOGONAL;
            lf->count++;
            continue;
        }

        /* Is this capability used? */
        uint32_t cap_bit = (uint32_t)1 << (uint32_t)e->cap;
        if (!(used_caps_mask & cap_bit)) {
            rec->resolution = TEKO_FFI_RESOLVED_UNUSED;
            /* Warn (not error) — declared-but-unused. */
            fprintf(stderr,
                "TEKO FFI warning: capability '%s' declared but not used by this program.\n",
                TEKO_CAP_NAMES[e->cap]);
            lf->count++;
            continue;
        }

        /* Try backends in declaration order. */
        int resolved = 0;
        for (int b = 0; b < e->order_count && !resolved; ++b) {
            TekoBackendKind bk = e->order[b];
            switch (bk) {
                case TEKO_BACKEND_SYSTEM: {
                    /* Use the declared system_lib name, or fall back to the default. */
                    const char* lib_name =
                        (e->system_lib[0] != '\0')
                            ? e->system_lib
                            : SYSTEM_LIB_DEFAULT[e->cap];
                    if (teko_ffi_probe_system_lib(lib_name)) {
                        rec->resolution    = TEKO_FFI_RESOLVED_OK;
                        rec->winning_backend = TEKO_BACKEND_SYSTEM;
                        (void)snprintf(rec->winning_lib, TEKO_FFI_MAX_STR,
                                       "%s", lib_name);
                        (void)snprintf(rec->winning_ver, TEKO_FFI_MAX_STR,
                                       "system");
                        resolved = 1;
                    }
                    break;
                }
                case TEKO_BACKEND_BUNDLED: {
                    /* Bundled = always available (teko ships it vendored). */
                    const char* bname =
                        (e->bundled_name[0] != '\0')
                            ? e->bundled_name
                            : BUNDLED_DEFAULT[e->cap];
                    const char* bver =
                        (e->bundled_ver[0] != '\0')
                            ? e->bundled_ver
                            : BUNDLED_DEFAULT_VER[e->cap];
                    rec->resolution      = TEKO_FFI_RESOLVED_OK;
                    rec->winning_backend = TEKO_BACKEND_BUNDLED;
                    (void)snprintf(rec->winning_lib, TEKO_FFI_MAX_STR, "%s", bname);
                    (void)snprintf(rec->winning_ver, TEKO_FFI_MAX_STR, "%s", bver);
                    resolved = 1;
                    break;
                }
                case TEKO_BACKEND_NATIVE: {
                    /* Native only available for DEFLATE today. */
                    if (e->cap == TEKO_CAP_DEFLATE) {
                        rec->resolution      = TEKO_FFI_RESOLVED_OK;
                        rec->winning_backend = TEKO_BACKEND_NATIVE;
                        (void)snprintf(rec->winning_lib, TEKO_FFI_MAX_STR,
                                       "teko-deflate-native");
                        (void)snprintf(rec->winning_ver, TEKO_FFI_MAX_STR,
                                       "native");
                        resolved = 1;
                    }
                    /* For other caps, native is not available; try next. */
                    break;
                }
                default:
                    break;
            }
        }

        if (!resolved) {
            rec->resolution = TEKO_FFI_RESOLVED_NOT_FOUND;
            print_not_found_msg(e->cap);
            if (out_fail_count) (*out_fail_count)++;
        }
        lf->count++;
    }

    return lf;
}

void teko_ffi_lockfile_free(TekoFfiLockfile* lf) {
    if (!lf) return;
    free(lf);
}

/* -------------------------------------------------------------------------
 * Lockfile I/O.
 *
 * Format (line-oriented, machine-readable):
 *   # teko ffi lockfile v1
 *   <cap_name> <resolution> <backend_kind> <lib> <version>
 *
 * Fields are space-separated; lib and version are URL-safe identifiers
 * (no spaces; the parser only reads them back as tokens).
 *
 * Write uses a .tmp-then-rename pattern for atomicity.
 * Read is strict (lines that do not match the expected format are ignored).
 *
 * Security: path is caller-supplied but never interpreted as a shell command
 * — we use fopen/rename only (no system/exec).  Path traversal is the
 * caller's responsibility (the manifest parser restricts what can appear in
 * the dep names so a dep name cannot escape to a path).
 * ---------------------------------------------------------------------- */

static const char* RESOLUTION_NAMES[] = {
    "ok", "unused", "not_found", "wasm_orthogonal"
};

static const char* BACKEND_KIND_NAMES[] = {
    "system", "bundled", "native"
};

int teko_ffi_lockfile_write(const TekoFfiLockfile* lf, const char* path) {
    if (!lf || !path) { errno = EINVAL; return -1; }

    /* Build temp path: path + ".tmp".  Check for overflow. */
    size_t plen = strlen(path);
    /* 4 bytes for ".tmp" + NUL = 5; total must not overflow size_t. */
    if (plen > 4096) { errno = ENAMETOOLONG; return -1; }
    /* Stack buffer is large enough; plen + 5 <= 4101 < 4096+5 ≤ SIZE_MAX. */
    char tmp_path[4096 + 5];
    /* snprintf with explicit size prevents OOB write. */
    int n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(tmp_path)) { errno = ENAMETOOLONG; return -1; }

    /* Open in binary mode on all platforms for deterministic byte output
     * (no CRLF translation on Windows). */
    FILE* f = fopen(tmp_path, "wb");
    if (!f) return -1;

    fprintf(f, "# teko ffi lockfile v1\n");
    for (int i = 0; i < lf->count; ++i) {
        const TekoFfiLockRecord* r = &lf->records[i];
        if ((unsigned)r->cap >= (unsigned)TEKO_CAP__COUNT) continue;
        if ((unsigned)r->resolution > (unsigned)TEKO_FFI_WASM_ORTHOGONAL) continue;

        const char* res_str = RESOLUTION_NAMES[r->resolution];
        const char* bk_str  = (r->resolution == TEKO_FFI_RESOLVED_OK &&
                                (unsigned)r->winning_backend < (unsigned)TEKO_BACKEND__COUNT)
                              ? BACKEND_KIND_NAMES[r->winning_backend]
                              : "-";
        /* winning_lib / winning_ver may be empty string. */
        const char* lib = (r->winning_lib[0] != '\0') ? r->winning_lib : "-";
        const char* ver = (r->winning_ver[0] != '\0') ? r->winning_ver : "-";

        fprintf(f, "%s %s %s %s %s\n",
                TEKO_CAP_NAMES[r->cap], res_str, bk_str, lib, ver);
    }

    if (fclose(f) != 0) {
        /* Attempt cleanup; ignore secondary error. */
        (void)remove(tmp_path);
        return -1;
    }

    /* Atomic rename. */
#if defined(_WIN32)
    /* On Windows, rename fails if the destination exists; use MoveFileExA. */
    if (!MoveFileExA(tmp_path, path, MOVEFILE_REPLACE_EXISTING)) {
        (void)remove(tmp_path);
        errno = EIO;
        return -1;
    }
#else
    if (rename(tmp_path, path) != 0) {
        (void)remove(tmp_path);
        return -1;
    }
#endif
    return 0;
}

TekoFfiLockfile* teko_ffi_lockfile_read(const char* path,
                                        char* errbuf, size_t errbuf_len) {
    if (!path) {
        set_err(errbuf, errbuf_len, "teko_ffi_lockfile_read: null path");
        return NULL;
    }

    /* Open in binary mode on all platforms for consistent byte reading
     * (no CRLF translation on Windows). */
    FILE* f = fopen(path, "rb");
    if (!f) return NULL; /* File does not exist: not an error. */

    TekoFfiLockfile* lf = (TekoFfiLockfile*)calloc(1, sizeof(TekoFfiLockfile));
    if (!lf) {
        fclose(f);
        set_err(errbuf, errbuf_len, "teko_ffi_lockfile_read: calloc failed");
        return NULL;
    }

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        /* Skip comment / header lines. */
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        if (lf->count >= TEKO_FFI_MAX_CAPS) break;

        char cap_s[64], res_s[64], bk_s[64], lib_s[TEKO_FFI_MAX_STR], ver_s[TEKO_FFI_MAX_STR];
        /* sscanf with bounded widths — safe against overlong tokens. */
        /* %63s reads at most 63 chars; %255s reads at most 255 chars. */
        int matched = sscanf(line, "%63s %63s %63s %255s %255s",
                             cap_s, res_s, bk_s, lib_s, ver_s);
        if (matched < 2) continue;

        TekoFfiLockRecord* r = &lf->records[lf->count];

        /* Map cap name back to TekoCap. */
        r->cap = TEKO_CAP__COUNT;
        for (int i = 0; i < TEKO_CAP__COUNT; ++i) {
            if (strcmp(cap_s, TEKO_CAP_NAMES[i]) == 0) {
                r->cap = (TekoCap)i;
                break;
            }
        }
        if (r->cap == TEKO_CAP__COUNT) continue; /* unknown — skip */

        /* Map resolution string. */
        r->resolution = TEKO_FFI_RESOLVED_NOT_FOUND; /* default */
        for (int j = 0; j <= (int)TEKO_FFI_WASM_ORTHOGONAL; ++j) {
            if (strcmp(res_s, RESOLUTION_NAMES[j]) == 0) {
                r->resolution = (TekoFfiResolution)j;
                break;
            }
        }

        /* Map backend kind string. */
        if (matched >= 3) {
            r->winning_backend = TEKO_BACKEND__COUNT;
            for (int k = 0; k < TEKO_BACKEND__COUNT; ++k) {
                if (strcmp(bk_s, BACKEND_KIND_NAMES[k]) == 0) {
                    r->winning_backend = (TekoBackendKind)k;
                    break;
                }
            }
        }

        if (matched >= 4 && strcmp(lib_s, "-") != 0) {
            (void)snprintf(r->winning_lib, TEKO_FFI_MAX_STR, "%s", lib_s);
        }
        if (matched >= 5 && strcmp(ver_s, "-") != 0) {
            (void)snprintf(r->winning_ver, TEKO_FFI_MAX_STR, "%s", ver_s);
        }

        lf->count++;
    }

    fclose(f);
    return lf;
}

/* -------------------------------------------------------------------------
 * System library probe.
 *
 * At runtime (unit-test / build-time tool context):
 *   - POSIX: dlopen with RTLD_NOLOAD | RTLD_NOW to check for presence
 *     without side-effecting the process.  If RTLD_NOLOAD fails, try
 *     a plain dlopen to handle static-link or LD_PRELOAD cases.
 *   - Windows: GetModuleHandleA (already-loaded check) then LoadLibraryA.
 *
 * This is NOT used at Teko-compiled-program runtime — it is a build-time
 * tool (the resolver runs during `teko build`, not in produced binaries).
 * ---------------------------------------------------------------------- */

int teko_ffi_probe_system_lib(const char* lib_name) {
    if (!lib_name || lib_name[0] == '\0') return 0;

#if defined(_WIN32)
    /* Try the module as-is (e.g. "ssl.dll", "zstd.dll", "ssl"). */
    if (GetModuleHandleA(lib_name)) return 1;
    /* Try with .dll suffix if not already present. */
    {
        char dll_name[TEKO_FFI_MAX_STR];
        size_t n = strlen(lib_name);
        if (n >= 4 && strcmp(lib_name + n - 4, ".dll") == 0) {
            /* Already has .dll. */
            HMODULE h = LoadLibraryA(lib_name);
            if (h) { FreeLibrary(h); return 1; }
        } else {
            int r = snprintf(dll_name, sizeof(dll_name), "%s.dll", lib_name);
            if (r > 0 && (size_t)r < sizeof(dll_name)) {
                HMODULE h = LoadLibraryA(dll_name);
                if (h) { FreeLibrary(h); return 1; }
            }
            /* Also try lib<name>.dll (MinGW convention). */
            r = snprintf(dll_name, sizeof(dll_name), "lib%s.dll", lib_name);
            if (r > 0 && (size_t)r < sizeof(dll_name)) {
                HMODULE h = LoadLibraryA(dll_name);
                if (h) { FreeLibrary(h); return 1; }
            }
        }
    }
    return 0;
#elif defined(__APPLE__)
    /* macOS: try lib<name>.dylib variants. */
    {
        char so[TEKO_FFI_MAX_STR];
        /* Try as-is first. */
        void* h = dlopen(lib_name, RTLD_NOLOAD | RTLD_NOW);
        if (h) { dlclose(h); return 1; }
        /* Try lib<name>.dylib. */
        int r = snprintf(so, sizeof(so), "lib%s.dylib", lib_name);
        if (r > 0 && (size_t)r < sizeof(so)) {
            h = dlopen(so, RTLD_NOLOAD | RTLD_NOW);
            if (h) { dlclose(h); return 1; }
            /* Without NOLOAD — accepts fresh loads too. */
            h = dlopen(so, RTLD_NOW | RTLD_LOCAL);
            if (h) { dlclose(h); return 1; }
        }
    }
    return 0;
#else
    /* Linux / other POSIX: try lib<name>.so and lib<name>.so.N variants. */
    {
        char so[TEKO_FFI_MAX_STR];
        /* RTLD_NOLOAD check. */
        void* h = dlopen(lib_name, RTLD_NOLOAD | RTLD_NOW);
        if (h) { dlclose(h); return 1; }
        /* lib<name>.so */
        int r = snprintf(so, sizeof(so), "lib%s.so", lib_name);
        if (r > 0 && (size_t)r < sizeof(so)) {
            h = dlopen(so, RTLD_NOLOAD | RTLD_NOW);
            if (h) { dlclose(h); return 1; }
            h = dlopen(so, RTLD_NOW | RTLD_LOCAL);
            if (h) { dlclose(h); return 1; }
        }
    }
    return 0;
#endif
}

/* -------------------------------------------------------------------------
 * FFI-boundary marshalling helpers.
 * ---------------------------------------------------------------------- */

/* Opaque handle struct. */
struct TekoDylibHandle {
#if defined(_WIN32)
    HMODULE hmod;
#else
    void*   dlh;
#endif
};

TekoDylibHandle* teko_ffi_lib_open(const char* lib_name,
                                   char* errbuf, size_t errbuf_len) {
    if (!lib_name || lib_name[0] == '\0') {
        set_err(errbuf, errbuf_len, "teko_ffi_lib_open: null/empty lib_name");
        return NULL;
    }
    TekoDylibHandle* h = (TekoDylibHandle*)calloc(1, sizeof(TekoDylibHandle));
    if (!h) {
        set_err(errbuf, errbuf_len, "teko_ffi_lib_open: calloc failed");
        return NULL;
    }
#if defined(_WIN32)
    h->hmod = LoadLibraryA(lib_name);
    if (!h->hmod) {
        DWORD err = GetLastError();
        (void)snprintf(errbuf ? errbuf : (char*)(void*)&errbuf,
                       errbuf ? errbuf_len : 0,
                       "teko_ffi_lib_open: LoadLibraryA('%s') failed, error=%lu",
                       lib_name, (unsigned long)err);
        if (errbuf) errbuf[errbuf_len - 1] = '\0';
        free(h);
        return NULL;
    }
#else
    h->dlh = dlopen(lib_name, RTLD_NOW | RTLD_LOCAL);
    if (!h->dlh) {
        const char* dl_err = dlerror();
        (void)snprintf(errbuf ? errbuf : (char*)(void*)&errbuf,
                       errbuf ? errbuf_len : 0,
                       "teko_ffi_lib_open: dlopen('%s') failed: %s",
                       lib_name, dl_err ? dl_err : "(unknown)");
        if (errbuf) errbuf[errbuf_len - 1] = '\0';
        free(h);
        return NULL;
    }
#endif
    return h;
}

void* teko_ffi_lib_sym(TekoDylibHandle* h, const char* symbol) {
    if (!h || !symbol) return NULL;
#if defined(_WIN32)
    return (void*)(intptr_t)GetProcAddress(h->hmod, symbol);
#else
    return dlsym(h->dlh, symbol);
#endif
}

void teko_ffi_lib_close(TekoDylibHandle* h) {
    if (!h) return;
#if defined(_WIN32)
    if (h->hmod) FreeLibrary(h->hmod);
#else
    if (h->dlh)  dlclose(h->dlh);
#endif
    free(h);
}

size_t teko_ffi_str_copy(char* dst, size_t dst_cap,
                         const char* src, size_t src_len) {
    if (!dst || dst_cap == 0) teko_ffi_panic("teko_ffi_str_copy: null dst or zero cap");
    if (!src) {
        dst[0] = '\0';
        return 0;
    }
    size_t copy_n = src_len;
    if (copy_n >= dst_cap) copy_n = dst_cap - 1;
    memcpy(dst, src, copy_n);
    dst[copy_n] = '\0';
    return copy_n;
}

int32_t teko_ffi_narrow_i32(intptr_t v) {
    if (v < (intptr_t)(-2147483647 - 1) || v > (intptr_t)2147483647) {
        /* This is a hard invariant violation (overflow guard for LLP64).
         * The message is a string literal — no format-string injection. */
        teko_ffi_panic("teko_ffi_narrow_i32: value out of int32_t range (LLP64 overflow guard)");
    }
    return (int32_t)v;
}

int teko_ffi_check_rc(int rc, int ok_value, const char* context) {
    if (rc != ok_value) {
        /* context is a caller-supplied string literal; we use %s (not direct embedding)
         * to prevent accidental format-string injection if a caller is sloppy. */
        fprintf(stderr, "TEKO FFI error: %s: rc=%d\n",
                context ? context : "(unknown)", rc);
        fflush(stderr);
        abort();
    }
    return rc;
}

void* teko_ffi_check_ptr(void* ptr, const char* context) {
    if (!ptr) {
        fprintf(stderr, "TEKO FFI error: %s: unexpected NULL\n",
                context ? context : "(unknown)");
        fflush(stderr);
        abort();
    }
    return ptr;
}

_Noreturn void teko_ffi_panic(const char* msg) {
    /* msg is expected to be a string literal or a pre-formatted fixed string.
     * We use fputs (not fprintf) — no format-string interpretation. */
    fputs("TEKO FFI panic: ", stderr);
    fputs(msg ? msg : "(null message)", stderr);
    fputs("\n", stderr);
    fflush(stderr);
    abort();
}
