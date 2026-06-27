// src/vm/vm.h   (namespace 'teko::vm')
//
// D1 — the TYPED-TREE VM / interpreter (Stage-1: the test/debug execution engine).
//
// It tree-walks the CHECKED typed tree (tk_tprogram from src/checker/tast.h) DIRECTLY:
// no .tkb serialization, no codegen, no native binary. Tests run on THIS engine over the
// typed tree, BEFORE emit/codegen, then are discarded — they never become .tkb/binary.
//
// ARCHITECTURE: the VM and the C codegen (src/codegen/codegen_c.c) are TWO execution
// paths over the SAME checked tast. This VM MIRRORS codegen's semantics exactly — same
// node coverage, same ÷0 / cast guards, same print/println/assert builtin recognition,
// same VIRTUAL-MAIN return semantics. That makes the VM the differential-correctness
// anchor against the native path. If a node's interpreter semantics would diverge from
// codegen, that is a HALT (report), not a silent choice.
//
// LAWS: M.0 (metal — a tagged-union value model, no boxing tower); M.1 (fail loud —
// panics via the runtime's tk_panic_* so messages MATCH the native path); M.3 (honest —
// any node kind not yet interpreted ABORTS with a clear message, never wrong-silently).
#ifndef TK_VM_H
#define TK_VM_H

#include "../checker/tast.h"   // tk_tprogram (the checked typed tree the VM walks)

// tk_vm_run — execute the VIRTUAL-MAIN (the loose top-level statements) in order,
// MIRRORING codegen_c.c's main() lowering: a `return n` sets the process exit code (an
// early exit, value cast to int); falling off the end yields 0. Top-level functions are
// callable. Returns the exit code (caller hands it to the OS).
//
// The value model (tk_value), the env, and tk_vm_eval_expr / tk_vm_exec_block are
// translation-unit-internal (vm.c) — this is the single public door for the driver.
int tk_vm_run(tk_tprogram prog);

// tk_vm_run_tests — run every `#test` function (D2 test runner; `teko test`). Fail-fast: a
// failed assertion panics from inside the VM. All pass → 0. (Mirrors vm.tks run_tests.)
int tk_vm_run_tests(tk_tprogram prog);
// `record_branches` enables branch recording (the gate's BRANCH floor reads it); `write_xml` writes
// the Cobertura report to `cov_path`. (Mirrors vm.tks run_tests_cov.)
int tk_vm_run_tests_cov(tk_tprogram prog, bool record_branches, bool write_xml, const char *cov_path);
// aggregate LINE / BRANCH coverage % from the last recorded run (D4 line/branch floors). (Mirrors vm.tks.)
uint64_t tk_vm_line_coverage_pct(tk_tprogram prog);
uint64_t tk_vm_branch_coverage_pct(tk_tprogram prog);

// tk_vm_coverage_pct — function-level coverage % from the last run_tests (D3). (Mirrors vm.tks.)
uint64_t tk_vm_coverage_pct(tk_tprogram prog);

#endif // TK_VM_H
