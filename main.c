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

// `teko test <projdir>` — honest stub. The project test runner is crumb D2 (not yet
// built); wire the subcommand so it exists and fails loudly rather than silently.
static int test_stub(const char *dir) {
    fprintf(stderr,
            "teko: %s: test not yet — the project test runner is crumb D2 (not built)\n",
            dir);
    return 1;
}

// Reject a file/`.tks` argument with the honest "projects only" message.
static int reject_file_arg(void) {
    fputs("teko: teko compiles projects: pass a project directory or .tkp "
          "(a lone .tks is not a unit)\n", stderr);
    return 2;
}

int main(int argc, char **argv) {
    tk_install_crash_handler();   // a crash prints a C stack trace, not a silent exit (M.1)
    if (argc < 2) { usage(); return 2; }

    const char *cmd = argv[1];

    char buf[4096];

    // Explicit subcommands take a project (directory or `.tkp`).
    if (strcmp(cmd, "build") == 0 || strcmp(cmd, "run") == 0 || strcmp(cmd, "test") == 0) {
        if (argc < 3) { usage(); return 2; }
        if (looks_like_file_arg(argv[2])) return reject_file_arg();
        const char *dir = project_dir_of(argv[2], buf, sizeof(buf));
        if (strcmp(cmd, "build") == 0) return tk_compile_project(dir);
        if (strcmp(cmd, "run") == 0)   return tk_run_project(dir);
        return test_stub(dir);
    }

    // Bare argument: a project (directory or `.tkp`) ≡ build. A file/`.tks` is rejected.
    if (looks_like_file_arg(cmd)) return reject_file_arg();
    return tk_compile_project(project_dir_of(cmd, buf, sizeof(buf)));
}
