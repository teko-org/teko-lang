// src/win32_compat.h — POSIX-to-Win32 compatibility shims used by teko.
//
// Include this header (inside a #ifdef _WIN32 guard at the call site, or
// unconditionally here) instead of <unistd.h>, <spawn.h>, <sys/wait.h>,
// <dirent.h>, and <sys/stat.h> on Windows.  POSIX platforms keep their
// native headers unchanged.
//
// Surfaces provided:
//   chdir       → _chdir (<direct.h>)
//   mkdir(p,m)  → _mkdir(p) (<direct.h>; mode ignored — not supported on Windows)
//   opendir / readdir / closedir / DIR / dirent  → FindFirstFileA shim
//   tk_win32_spawnvp(file, argv) → _spawnvp(_P_WAIT, …) — synchronous subprocess
//
// All inline/static so this header can be included in multiple TUs.

#ifndef TK_WIN32_COMPAT_H
#define TK_WIN32_COMPAT_H

#ifdef _WIN32

// Silence MSVC/CRT deprecation warnings for POSIX-name functions (fopen, getenv, …).
// We intentionally use the portable names; the "safe" _s variants have different semantics.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <windows.h>   // FindFirstFileA / FindNextFileA / FindClose
#include <direct.h>    // _chdir, _mkdir, _getcwd
#include <process.h>   // _spawnvp, _P_WAIT
#include <stdlib.h>    // malloc, free, _putenv_s
#include <string.h>    // strlen, memcpy
#include <errno.h>     // EEXIST (available on Windows CRT)

// --- directory change / creation -----------------------------------------
#define chdir(p)           _chdir(p)
#define mkdir(path, mode)  _mkdir(path)   // mode is not supported on Windows
#define getcwd(buf, size)  _getcwd(buf, (int)(size))

// --- environment ---------------------------------------------------------
// setenv(name, value, overwrite) → _putenv_s(name, value)
// Both return 0 on success and non-zero on error; caller checks `!= 0`.
#define setenv(n, v, o)    _putenv_s(n, v)

// --- dirent shim ---------------------------------------------------------
// Minimal opendir/readdir/closedir over Win32 FindFirstFileA/FindNextFileA.
// Each TK_DIR carries its own tk_dirent so recursive calls are safe.

typedef struct tk_dirent { char d_name[MAX_PATH]; } tk_dirent;

typedef struct {
    HANDLE           hFind;
    WIN32_FIND_DATAA wfd;
    bool             first;
    tk_dirent        dent;
} TK_DIR;

static inline TK_DIR *tk_opendir(const char *path) {
    size_t plen = strlen(path);
    if (plen + 3 > MAX_PATH) return NULL;
    char pattern[MAX_PATH];
    memcpy(pattern, path, plen);
    pattern[plen] = '\\'; pattern[plen + 1] = '*'; pattern[plen + 2] = '\0';
    TK_DIR *d = (TK_DIR *)malloc(sizeof *d);
    if (!d) return NULL;
    d->hFind = FindFirstFileA(pattern, &d->wfd);
    if (d->hFind == INVALID_HANDLE_VALUE) { free(d); return NULL; }
    d->first = true;
    return d;
}

static inline tk_dirent *tk_readdir(TK_DIR *d) {
    if (d->first) {
        d->first = false;
    } else if (!FindNextFileA(d->hFind, &d->wfd)) {
        return NULL;
    }
    size_t n = strlen(d->wfd.cFileName);
    if (n >= MAX_PATH) n = MAX_PATH - 1;
    memcpy(d->dent.d_name, d->wfd.cFileName, n);
    d->dent.d_name[n] = '\0';
    return &d->dent;
}

static inline void tk_closedir(TK_DIR *d) {
    if (d->hFind != INVALID_HANDLE_VALUE) FindClose(d->hFind);
    free(d);
}

#define DIR      TK_DIR
#define dirent   tk_dirent
#define opendir  tk_opendir
#define readdir  tk_readdir
#define closedir tk_closedir

// --- synchronous subprocess ----------------------------------------------
// Replacement for posix_spawnp+waitpid / fork+execvp+waitpid.
// Returns the child's exit code, or -1 on spawn error.
//
// QUOTING (the bug this shim now fixes): the CRT's _spawnvp joins argv with
// spaces UNQUOTED into the single CreateProcess command line, so an element
// containing spaces splits apart when the child's CRT re-parses that line.
// Concretely, `sh -c "<cmd with spaces>"` reached sh as many separate words:
// sh took only the first word after -c as the script (running the compiler
// child argument-less, its usage leaking to the inherited stdout) and the
// redirect tokens were never interpreted — the regression runner's captured
// stderr stayed empty on Windows. Each element is therefore pre-quoted per
// the MSVC command-line rules ("Parsing C++ command-line arguments") so the
// child re-parses the exact argv. PATH resolution uses `file` (first param),
// which _spawnvp does NOT take from the quoted vector — resolution is
// unaffected by the quoting.

// tk_win32_quote_arg — one argv element quoted per the MSVC re-parse rules:
// unchanged when it has no space/tab/quote (and is non-empty); otherwise
// wrapped in double quotes with N backslashes before a quote emitted as
// 2N+1 backslashes + the quote, and N trailing backslashes emitted as 2N.
// Returns a malloc'd copy (caller frees), or NULL on allocation failure.
static inline char *tk_win32_quote_arg(const char *a) {
    size_t n = strlen(a);
    bool need = (n == 0);
    for (size_t i = 0; i < n && !need; i++)
        if (a[i] == ' ' || a[i] == '\t' || a[i] == '"') need = true;
    if (!need) {
        char *c = (char *)malloc(n + 1);
        if (!c) return NULL;
        memcpy(c, a, n + 1);
        return c;
    }
    // Worst case doubles every char and adds the two wrapping quotes + NUL.
    char *q = (char *)malloc(2 * n + 3);
    if (!q) return NULL;
    size_t w = 0, bs = 0;
    q[w++] = '"';
    for (size_t i = 0; i < n; i++) {
        char c = a[i];
        if (c == '\\') { bs++; continue; }
        if (c == '"') {
            for (size_t k = 0; k < 2 * bs + 1; k++) q[w++] = '\\';
            q[w++] = '"';
            bs = 0;
            continue;
        }
        for (size_t k = 0; k < bs; k++) q[w++] = '\\';
        bs = 0;
        q[w++] = c;
    }
    for (size_t k = 0; k < 2 * bs; k++) q[w++] = '\\';
    q[w++] = '"';
    q[w] = '\0';
    return q;
}

static inline int tk_win32_spawnvp(const char *file, char *const *argv) {
    size_t argc = 0;
    while (argv[argc]) argc++;
    char **qv = (char **)malloc((argc + 1) * sizeof(char *));
    if (!qv) return -1;
    int rc = -1;
    size_t made = 0;
    for (; made < argc; made++) {
        qv[made] = tk_win32_quote_arg(argv[made]);
        if (!qv[made]) goto tk_spawn_done;
    }
    qv[argc] = NULL;
    rc = _spawnvp(_P_WAIT, file, (const char *const *)qv);
tk_spawn_done:
    for (size_t i = 0; i < made; i++) free(qv[i]);
    free(qv);
    return rc;
}

#endif  // _WIN32
#endif  // TK_WIN32_COMPAT_H
