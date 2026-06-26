// src/checker/initanalysis.h — Phase 5: definite-assignment / init analysis.
//
// C-BOOTSTRAP REALIZATION of initanalysis.tks. Teko has NO uninitialized binding form
// (every let/mut/const carries a value, typed in the pre-binding env), so use-before-init is
// structural and needs no check. This pass delivers the remaining Phase-5 diagnostics over the
// fully typed program:
//   * unused LOCAL  (a let/mut/const simple-name never read in its function) -> ERROR (error?)
//   * unused PRIVATE function (declared private, never called anywhere)      -> WARNING (stderr)
// Warnings print to stderr (the warnings channel — Teko has no module-mutable accumulator) and
// never fail; the first unused-local is returned as a located error (tk_error). On success the
// returned error has a NULL message (the `error?` ok convention — tk_error_make(NULL)).
#ifndef TK_INITANALYSIS_H
#define TK_INITANALYSIS_H

#include "tast.h"   // tk_tprogram, tk_tfunction, tk_tstatement, tk_texpr

tk_error tk_analyze_program(tk_tprogram prog);

#endif // TK_INITANALYSIS_H
