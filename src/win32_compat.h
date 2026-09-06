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
// ESTE INCLUDE FALTAVA, e a forma da falha diz porque um cabecalho tem de carregar as SUAS
// proprias dependencias em vez de as pedir emprestadas a quem o inclui: o `teko_rt.c` inclui
// `<io.h>` DUAS LINHAS DEPOIS de incluir este ficheiro (`:29` contra `:31`) — e duas linhas tarde
// e tarde na mesma. A perna `artifact / windows-x86_64` morreu com `win32_compat.h:236: use of
// undeclared identifier '_get_osfhandle'` ao fim de 75,5 s de `cc`, e nao morreu em mais lado
// nenhum porque o bloco e so do Windows: nem o Linux nem o macOS o compilam.
#include <io.h>        // _get_osfhandle — descritor CRT → HANDLE, em tk_win32_redirect_handle_of_fd (F5)

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

// --- asynchronous subprocess with per-stream redirection (0.3.1.2) --------
// tk_rt_spawn_redirected (teko_rt.c) marshals its tk_str arguments to plain C strings and calls
// these; kept beside tk_win32_spawnvp because both share tk_win32_quote_arg and the same "one
// command line, MSVC-quoted" launch shape. UNVALIDATED ON A REAL WINDOWS HOST — this repository's
// CI has no Windows runner reachable from this change; it is compiled (never RUN) against
// mingw-w64's headers as the only check available here. Named so nobody mistakes "compiles" for
// "measured".

// tk_win32_join_cmdline — one CreateProcess command-line string built from a NULL-terminated argv,
// each element quoted per the MSVC re-parse rules (`tk_win32_quote_arg`). Caller frees.
static inline char *tk_win32_join_cmdline(char *const *argv) {
    size_t argc = 0;
    while (argv[argc]) argc++;
    char **qv = (char **)malloc(argc * sizeof(char *));
    size_t total = 1;
    for (size_t i = 0; i < argc; i++) {
        qv[i] = tk_win32_quote_arg(argv[i]);
        total += strlen(qv[i]) + 1;
    }
    char *line = (char *)malloc(total);
    line[0] = '\0';
    for (size_t i = 0; i < argc; i++) {
        if (i) strcat(line, " ");
        strcat(line, qv[i]);
        free(qv[i]);
    }
    free(qv);
    return line;
}

// tk_win32_inheritable_std — an INHERITABLE duplicate of one of this process's own standard
// handles (used when a redirection path is left empty, i.e. "inherit the parent's stream"). A bare
// `GetStdHandle` result is not guaranteed inheritable, so this always duplicates it explicitly.
static inline HANDLE tk_win32_inheritable_std(DWORD which) {
    HANDLE self = GetCurrentProcess();
    HANDLE src = GetStdHandle(which);
    if (src == NULL || src == INVALID_HANDLE_VALUE) return NULL;
    HANDLE dup = NULL;
    if (!DuplicateHandle(self, src, self, &dup, 0, TRUE, DUPLICATE_SAME_ACCESS)) return NULL;
    return dup;
}

// tk_win32_redirect_handle — the inheritable HANDLE for one child stream: a fresh file opened at
// `path` when non-empty (`CREATE_ALWAYS` for an output stream, `OPEN_EXISTING` for the input one),
// else an inheritable duplicate of the parent's own standard handle (`tk_win32_inheritable_std`).
static inline HANDLE tk_win32_redirect_handle(const char *path, DWORD which, bool for_write) {
    if (!path || !path[0]) return tk_win32_inheritable_std(which);
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    DWORD access = for_write ? GENERIC_WRITE : GENERIC_READ;
    DWORD disposition = for_write ? CREATE_ALWAYS : OPEN_EXISTING;
    return CreateFileA(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, disposition, FILE_ATTRIBUTE_NORMAL, NULL);
}

// (0.3.1.0 F5) tk_win32_redirect_handle_of_fd — the INHERITABLE HANDLE for one child stream backed
// by a CRT DESCRIPTOR the caller already owns (a `tk_rt_pipe` end). The caller's own descriptor is
// left untouched: what CreateProcess receives is an explicit inheritable DUPLICATE, closed by
// `tk_win32_spawn_redirected` right after the launch exactly like the path-opened handles beside
// it. That split is the Windows spelling of the same ownership rule the POSIX arm follows — the
// parent's copy of a pipe's write end dies only when the CALLER closes it, because that close is
// what lets the read end reach end-of-file.
//
// NULL when `fd` names nothing (< 0, or not an open CRT descriptor), which the caller reads as
// "fall back to the path, or inherit".
static inline HANDLE tk_win32_redirect_handle_of_fd(int fd) {
    if (fd < 0) return NULL;
    HANDLE src = (HANDLE)_get_osfhandle(fd);
    if (src == INVALID_HANDLE_VALUE) return NULL;
    HANDLE self = GetCurrentProcess();
    HANDLE dup = NULL;
    if (!DuplicateHandle(self, src, self, &dup, 0, TRUE, DUPLICATE_SAME_ACCESS)) return NULL;
    return dup;
}

// (0.3.1.0 F5) tk_win32_child_handle — the inheritable HANDLE one child stream resolves to, with
// the DESCRIPTOR winning over the path when both are given — the same precedence the POSIX arm's
// `tk_rt_open_redirect` applies, so a caller reads one rule and not two.
static inline HANDLE tk_win32_child_handle(int fd, const char *path, DWORD which, bool for_write) {
    HANDLE from_fd = tk_win32_redirect_handle_of_fd(fd);
    if (from_fd) return from_fd;
    return tk_win32_redirect_handle(path, which, for_write);
}

// tk_win32_build_envblock — the CreateProcess environment block: the process's own inherited
// environment with `extra` (`K=V` tokens) appended, `"K=V\0…\0\0"`-shaped. Returns NULL ("inherit
// unchanged") when `extra_n` is zero — the common case, and the one that needs no block at all.
// Caller frees the non-NULL result with `free`.
static inline char *tk_win32_build_envblock(char *const *extra, size_t extra_n) {
    if (extra_n == 0) return NULL;
    char *inherited = GetEnvironmentStringsA();
    size_t inherited_len = 0;
    if (inherited) {
        const char *p = inherited;
        while (*p) { size_t l = strlen(p); inherited_len += l + 1; p += l + 1; }
    }
    size_t total = inherited_len + 1;
    for (size_t i = 0; i < extra_n; i++) total += strlen(extra[i]) + 1;
    char *block = (char *)malloc(total);
    size_t w = 0;
    if (inherited) { memcpy(block, inherited, inherited_len); w = inherited_len; FreeEnvironmentStringsA(inherited); }
    for (size_t i = 0; i < extra_n; i++) {
        size_t l = strlen(extra[i]);
        memcpy(block + w, extra[i], l);
        w += l;
        block[w++] = '\0';
    }
    block[w] = '\0';
    return block;
}

// tk_win32_spawn_redirected — CreateProcess `argv` without waiting, its three standard streams
// redirected per `in_fd`/`out_fd`/`err_fd` (a CRT descriptor the caller owns, else < 0) falling
// back to `in_path`/`out_path`/`err_path` (NULL/"" = inherit), and its working directory set to
// `dir` (NULL/"" = inherit), with `extra` environment tokens appended to the inherited block.
// Returns the child's HANDLE cast to an opaque i64 (closed once, by `tk_win32_wait_one`), or -1 on
// failure.
//
// (0.3.1.0 F5) THE DESCRIPTOR SLOTS ARE WHY THE CAPTURE NEED NOT BE A FILE. A caller passing a
// `tk_rt_pipe` write end here gets the child's stream on the pipe instead of on a scratch file, and
// keeps its own copy of that end — this function closes only the three handles IT created, which
// for a descriptor slot is the inheritable duplicate and never the caller's descriptor.
static inline int64_t tk_win32_spawn_redirected(char *const *argv, const char *dir,
                                                 char *const *extra, size_t extra_n,
                                                 const char *in_path, const char *out_path, const char *err_path,
                                                 int in_fd, int out_fd, int err_fd) {
    char *cmdline = tk_win32_join_cmdline(argv);
    char *envblock = tk_win32_build_envblock(extra, extra_n);
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = tk_win32_child_handle(in_fd,  in_path,  STD_INPUT_HANDLE,  false);
    si.hStdOutput = tk_win32_child_handle(out_fd, out_path, STD_OUTPUT_HANDLE, true);
    si.hStdError  = tk_win32_child_handle(err_fd, err_path, STD_ERROR_HANDLE,  true);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof pi);
    BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0,
                              envblock, (dir && dir[0]) ? dir : NULL, &si, &pi);
    free(cmdline);
    free(envblock);
    if (si.hStdInput  && si.hStdInput  != INVALID_HANDLE_VALUE) CloseHandle(si.hStdInput);
    if (si.hStdOutput && si.hStdOutput != INVALID_HANDLE_VALUE) CloseHandle(si.hStdOutput);
    if (si.hStdError  && si.hStdError  != INVALID_HANDLE_VALUE) CloseHandle(si.hStdError);
    if (!ok) return -1;
    CloseHandle(pi.hThread);
    return (int64_t)(intptr_t)pi.hProcess;
}

// tk_win32_wait_one — WaitForSingleObject + GetExitCodeProcess on the HANDLE `raw` encodes, then
// closes it — a handle is collected at most once, matching the `wait_one` contract in teko_rt.h.
// -1 when `raw` never held a real handle or the exit code could not be read.
static inline int32_t tk_win32_wait_one(int64_t raw) {
    if (raw < 0) return -1;
    HANDLE h = (HANDLE)(intptr_t)raw;
    WaitForSingleObject(h, INFINITE);
    DWORD code = 0;
    BOOL ok = GetExitCodeProcess(h, &code);
    CloseHandle(h);
    return ok ? (int32_t)code : -1;
}

#endif  // _WIN32
#endif  // TK_WIN32_COMPAT_H
