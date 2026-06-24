// src/main.c — the Teko bootstrap driver entry (`tekoc`).
//
// F-1: a STANDALONE build-smoke and the seed of the F1 pipeline driver. It deliberately
// does NOT yet #include the (still-incomplete) C mirror headers — F0 makes the mirror
// compile, then F1 grows this into the real driver (read → lex → parse → check → emit-C).
// Its only job today is to prove the toolchain + CMake build/link/run end-to-end.

#include <stdio.h>

int main(int argc, char **argv) {
    (void)argv;
    fputs("tekoc — Teko bootstrap compiler\n", stdout);
    fputs("build: OK (F-1 smoke).  pipeline: not wired yet "
          "(F0 compiles the C mirror; F1 wires read->lex->parse->check).\n", stdout);
    if (argc > 1)
        fputs("note: input files are ignored until the pipeline is wired (F1).\n", stdout);
    return 0;
}
