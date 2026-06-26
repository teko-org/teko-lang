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

#include <stdio.h>
#include <string.h>     // strlen, strcmp, strrchr, memcpy
#include <signal.h>     // signal — fatal-signal handler (crash stack traces)
#include <execinfo.h>   // backtrace, backtrace_symbols_fd
#include <stdlib.h>     // _Exit (async-signal-safe)

// CRASH STACK TRACE (M.1/M.3 — fail loud, be honest about WHERE). On a fatal signal (a genuine
// internal compiler bug — a NULL/OOB deref, a bad arithmetic), print a C stack trace to stderr and
// exit 128+signo, instead of dying silently with a bare exit code. NOT SIGABRT: abort() is the
// INTENTIONAL honest-barrier path (tk_panic / *_unsupported already printed their message). Uses
// backtrace_symbols_fd (async-signal-safe; no malloc).
static void tk_crash_handler(int sig) {
    void *frames[64];
    int n = backtrace(frames, 64);
    fputs("\nteko: FATAL signal — internal compiler crash (M.1). C stack trace:\n", stderr);
    backtrace_symbols_fd(frames, n, 2 /* stderr */);
    _Exit(128 + sig);
}
static void tk_install_crash_handler(void) {
    signal(SIGSEGV, tk_crash_handler);
    signal(SIGBUS,  tk_crash_handler);
    signal(SIGILL,  tk_crash_handler);
    signal(SIGFPE,  tk_crash_handler);
}

static void usage(void) {
    fputs("usage: teko build <projdir>   build the project to a native binary\n"
          "       teko run   <projdir>   run the project on the VM (debug profile)\n"
          "       teko test  <projdir>   run the project's tests\n"
          "       teko <projdir>         (bare) ≡ build\n"
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

// does `--no-test` appear in the args? (D4 — skip the build-time test gate; the bootstrap
// self-build uses it because the corpus's own tests are not yet VM-runnable.)
static bool has_no_test(int argc, char **argv) {
    for (int i = 1; i < argc; i += 1) if (strcmp(argv[i], "--no-test") == 0) return true;
    return false;
}

int main(int argc, char **argv) {
    tk_install_crash_handler();   // a crash prints a C stack trace, not a silent exit (M.1)
    if (argc < 2) { usage(); return 2; }

    const char *cmd = argv[1];

    char buf[4096];

    // Explicit subcommands take a project (directory or `.tkp`) + optional `-o <dir>`.
    if (strcmp(cmd, "build") == 0 || strcmp(cmd, "run") == 0 || strcmp(cmd, "test") == 0) {
        const char *proj;
        const char *out_dir = parse_out_dir(argc, argv, 2, &proj);
        if (proj == NULL) { usage(); return 2; }
        if (looks_like_file_arg(proj)) return reject_file_arg();
        const char *dir = project_dir_of(proj, buf, sizeof(buf));
        if (strcmp(cmd, "build") == 0) return tk_compile_project_g(dir, out_dir, !has_no_test(argc, argv));   // D4 gate (unless --no-test)
        if (strcmp(cmd, "run") == 0)   return tk_run_project(dir);
        return tk_test_project(dir);   // D2 — run the project's `#test` functions on the VM
    }

    // Bare argument: a project (directory or `.tkp`) ≡ build (also honors `-o <dir>`). A file is rejected.
    const char *proj;
    const char *out_dir = parse_out_dir(argc, argv, 1, &proj);
    if (proj == NULL) { usage(); return 2; }
    if (looks_like_file_arg(proj)) return reject_file_arg();
    return tk_compile_project_g(project_dir_of(proj, buf, sizeof(buf)), out_dir, !has_no_test(argc, argv));   // D4 gate (unless --no-test)
}
