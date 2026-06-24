// src/main.c — the Teko bootstrap driver entry (`tekoc`).
//
// F1 (path-to-first-binary): the pipeline is WIRED. `tekoc <file.tks>` runs
// read → lex → parse → check end-to-end (see src/driver.c) and reports the verdict.
// main() stays minimal: argument handling + delegate to tk_compile.
#include "driver.h"   // tk_compile

#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fputs("usage: teko <file.tks>\n", stderr);
        return 2;
    }
    return tk_compile(argv[1]);
}
