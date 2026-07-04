// main.c — the C SEED of the executable entry point (`teko`). Co-located at the project
// root next to its Teko original main.tks (the C/H + .tks pair): the seed compiles the
// .tks sources to reproduce the compiler in Teko (self-hosting). Keep this and main.tks
// semantically equivalent.
//
// MONOLITH, PROJECTS ONLY (REBOOT_PLAN §2.6). Teko compiles PROJECTS (`.tkp`), never an
// isolated `.tks` file — a lone file is not a compilation unit. The CLI is project-only:
//   teko build <projdir>   front-end → native codegen (a binary)
//   teko run   <projdir>   front-end → VM (debug profile)
//   teko test  <projdir>   honest stub (the test runner is crumb D2, not yet built)
//   teko <projdir>         ≡ build
// A bare file / `.tks` argument is rejected honestly. main() stays minimal: subcommand
// dispatch + delegate to the driver.
#include "driver.h"   // tk_compile_project, tk_run_project

// (CLI --version) the build version string, from teko_rt.c (TEKO_VERSION_STRING compile-time
// constant fed from teko.tkp). Forward-declared here (not via teko_rt.h) to match driver.c's
// pattern for tk_rt_os — tk_str is layout-identical between text.h (driver.h) and teko_rt.h.
tk_str tk_rt_version(void);

#include <stdio.h>
#include <string.h>     // strlen, strcmp, strrchr, memcpy
#include <signal.h>     // signal — fatal-signal handler (crash stack traces)
#include <stdlib.h>     // _Exit (async-signal-safe)
#ifndef _WIN32
#include <execinfo.h>   // backtrace, backtrace_symbols_fd (POSIX: Linux/macOS only)
#endif

// CRASH STACK TRACE (M.1/M.3 — fail loud, be honest about WHERE). On a fatal signal (a genuine
// internal compiler bug — a NULL/OOB deref, a bad arithmetic), print a C stack trace to stderr and
// exit 128+signo, instead of dying silently with a bare exit code. NOT SIGABRT: abort() is the
// INTENTIONAL honest-barrier path (tk_panic / *_unsupported already printed their message). Uses
// backtrace_symbols_fd (async-signal-safe; no malloc) on POSIX; Windows prints a notice instead.
static void tk_crash_handler(int sig) {
#ifndef _WIN32
    void *frames[64];
    int n = backtrace(frames, 64);
    fputs("\nteko: FATAL signal — internal compiler crash (M.1). C stack trace:\n", stderr);
    backtrace_symbols_fd(frames, n, 2 /* stderr */);
#else
    fputs("\nteko: FATAL signal — internal compiler crash (M.1). Stack trace unavailable on Windows.\n", stderr);
#endif
    _Exit(128 + sig);
}
static void tk_install_crash_handler(void) {
    signal(SIGSEGV, tk_crash_handler);
#ifndef _WIN32
    signal(SIGBUS,  tk_crash_handler);   // not defined on Windows
#endif
    signal(SIGILL,  tk_crash_handler);
    signal(SIGFPE,  tk_crash_handler);
}

// Print `teko <version>` to `stream`. The version is the RAW teko.tkp `version` + `-<suffix>`
// (e.g. "0.0.1.0-bootstrap"), embedded at build time — no runtime file read. Mirrors main.tks.
static void print_version(FILE *stream) {
    tk_str v = tk_rt_version();
    fprintf(stream, "teko %.*s\n", (int)v.len, (const char *)v.ptr);
}

static void usage(void) {
    print_version(stderr);   // version line first (also shown by --help)
    fputs("usage: teko build <projdir>   build the project to a native binary\n"
          "       teko run   <projdir>   run the project on the VM (debug profile)\n"
          "       teko test  <projdir>   run the project's tests\n"
          "       teko fmt   [--check] <path>...   format .tks/.tkt sources (self-hosted binary only)\n"
          "       teko <projdir>         (bare) ≡ build\n"
          "       teko --version | -v    print the version and exit\n"
          "       teko --help | -h       print this help and exit\n"
          "teko compiles projects, not files: pass a project directory or .tkp\n",
          stderr);
}

// A bare file argument ending in ".tks" is NOT a unit (§2.6). Reject it honestly rather
// than pretending to compile it.
static int looks_like_file_arg(const char *arg) {
    size_t n = strlen(arg);
    return n >= 4 && strcmp(arg + n - 4, ".tks") == 0;
}

// Resolve a project argument to its directory: a `.tkp` path resolves to its containing
// directory ("." when it has no path separator); anything else is taken as the dir as-is.
// Returns a pointer into `buf` (or `arg` unchanged). `buf` must be PATH-sized.
static const char *project_dir_of(const char *arg, char *buf, size_t buflen) {
    size_t n = strlen(arg);
    if (!(n >= 4 && strcmp(arg + n - 4, ".tkp") == 0)) return arg;
    const char *slash = strrchr(arg, '/');
    if (slash == NULL) return ".";
    size_t dn = (size_t)(slash - arg);
    if (dn >= buflen) return arg;   // too long — let the driver fail on chdir
    memcpy(buf, arg, dn);
    buf[dn] = '\0';
    return buf;
}

// Reject a file/`.tks` argument with the honest "projects only" message.
static int reject_file_arg(void) {
    fputs("teko: teko compiles projects: pass a project directory or .tkp "
          "(a lone .tks is not a unit)\n", stderr);
    return 2;
}

// Scan argv for `-o <dir>` (the build OUTPUT directory). Returns the dir (or "bin" by default)
// and writes the FIRST non-flag positional after the command into *proj (NULL if none). Keeps the
// CLI flat (no getopt): `-o` may appear anywhere after the subcommand (`teko . -o ./out`).
static const char *parse_out_dir(int argc, char **argv, int start, const char **proj) {
    const char *out_dir = "bin";
    *proj = NULL;
    for (int i = start; i < argc; i += 1) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) { out_dir = argv[i + 1]; i += 1; }   // the dir is the next token
            continue;
        }
        if (strcmp(argv[i], "--no-test") == 0) continue;   // a flag (D4 gate opt-out), not the project
        if (*proj == NULL) *proj = argv[i];   // the first non-flag positional is the project
    }
    return out_dir;
}

// does `--coverage` appear in the args? (write a Cobertura `cobertura.xml` next to the build output.)
static bool has_coverage(int argc, char **argv) {
    for (int i = 1; i < argc; i += 1) if (strcmp(argv[i], "--coverage") == 0) return true;
    return false;
}

int main(int argc, char **argv) {
    tk_install_crash_handler();   // a crash prints a C stack trace, not a silent exit (M.1)

    // --version / -v and --help / -h SHORT-CIRCUIT before the "no args → usage, exit 2" path
    // (which stays unchanged). --version prints the version line to stdout; --help prints the
    // usage banner (which leads with the version line) — both exit 0. Mirrors main.tks.
    if (argc >= 2) {
        const char *a1 = argv[1];
        if (strcmp(a1, "--version") == 0 || strcmp(a1, "-v") == 0) { print_version(stdout); return 0; }
        if (strcmp(a1, "--help") == 0    || strcmp(a1, "-h") == 0) { usage(); return 0; }
    }

    if (argc < 2) { usage(); return 2; }

    const char *cmd = argv[1];

    char buf[4096];

    // `fmt` — DT0 (TEKO_ROADMAP_DEVTOOLS): the canonical formatter is implemented PURE-TEKO
    // in src/fmt/fmt.tks (the teko::regex/teko::fs precedent: corpus source, no C twin), so it
    // exists in every SELF-HOSTED binary this seed builds. MIRRORS main.tks's `fmt` arm: the
    // seed recognizes the subcommand (so `fmt` is never mistaken for a project directory) and
    // stops honestly (M.3) — the seed exists to bootstrap the corpus, not to format it.
    if (strcmp(cmd, "fmt") == 0) {
        fputs("teko: `teko fmt` is implemented by the self-hosted compiler (src/fmt/fmt.tks);\n"
              "this C seed binary only bootstraps (build/run/test). Build the compiler\n"
              "(`teko build . -o bin`) and run `bin/teko fmt` instead.\n", stderr);
        return 2;
    }

    // Explicit subcommands take a project (directory or `.tkp`) + optional `-o <dir>`.
    if (strcmp(cmd, "build") == 0 || strcmp(cmd, "run") == 0 || strcmp(cmd, "test") == 0) {
        const char *proj;
        const char *out_dir = parse_out_dir(argc, argv, 2, &proj);
        if (proj == NULL) { usage(); return 2; }
        if (looks_like_file_arg(proj)) return reject_file_arg();
        const char *dir = project_dir_of(proj, buf, sizeof(buf));
        bool cov = has_coverage(argc, argv);   // `--coverage` → write <out>/cobertura.xml (test: project root)
        if (strcmp(cmd, "build") == 0) return tk_compile_project_g(dir, out_dir, cov);   // D4 gate (ALWAYS; --no-test ignored)
        if (strcmp(cmd, "run") == 0)   return tk_run_project(dir);
        return tk_test_project(dir, cov);   // D2 — run the project's `#test` functions on the VM
    }

    // Bare argument: a project (directory or `.tkp`) ≡ build (also honors `-o <dir>`). A file is rejected.
    const char *proj;
    const char *out_dir = parse_out_dir(argc, argv, 1, &proj);
    if (proj == NULL) { usage(); return 2; }
    if (looks_like_file_arg(proj)) return reject_file_arg();
    return tk_compile_project_g(project_dir_of(proj, buf, sizeof(buf)), out_dir, has_coverage(argc, argv));   // D4 gate (ALWAYS; --no-test ignored)
}
