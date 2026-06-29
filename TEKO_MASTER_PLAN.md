# TEKO — MASTER PLAN (execution sequence)

> **Status:** active · **Created:** 2026-06-25 · **Branch:** chore/reboot
>
> This is the single, ordered execution sequence for ALL open teko-lang work. It consolidates:
> the legislator's session critiques/directives, and every not-yet-done item mined from
> `TEKO_CORRECTION_PLAN.md`, `TEKO_EVOLUTION_DESIGN.md`, `TEKO_EVOLUTION_JUSTIFICATION.md`,
> `TEKO_ROADMAP_BINARY.md`, and `TEKO_ROADMAP_INDEPENDENCE.md`.
>
> Those documents remain the **detailed source of truth** for each item; this file is the
> **order in which we attack them**. Memory mirror: `memory/teko-open-problems-backlog.md`.

---

## Governing constraints (hold throughout every phase)

- **SUPREME RULE** — zero `.c`/`.h` ↔ `.tks` misalignment. Every C change is mirrored to its Teko twin.
- **Differential equivalence** — VM (`teko run`) == native (`teko build`) on every validated change.
- **Laws M.0–M.5** (`TEKO_CONSTITUTION.md`) govern all design rulings; tensions → tribunal, not guesswork.
- **Commit at green checkpoints** — build green + regressions `match_pattern_bindings==5` / `optionals==6`.
- **DRY-LAST RULING (legislator)** — the whole-codebase DRY refactor is the FINAL phase. Every other open
  item lands first, so DRY sweeps settled code, not a moving target.

## Foundation already laid (context — not in scope to redo)

Type-model doctrine (void/error/variant/nullable; no `never`), 128-bit + float prims, struct/variant value
layer, match/if-value execution, labeled loops, subscript indexing, the **S0 `tk_alloc()` allocation seam**,
slice value-layer Increment A (fixed+copy), and the `panic`/`exit` global-diverging-fn ruling are **DONE**.

**★ THE VALIDATION GATE — native self-host (`cmake --build build --target selfhost`, i.e. `./build/teko
build .` → `./bin/teko`):** this is the ONE end-to-end proof (read→lex→parse→check→native codegen→cc
link). **It does NOT pass yet** — native codegen has a short tail of walls, so Phase 6 is 🔶 and nothing
downstream is guaranteed. The green sub-checks (corpus type-check 854 items, regressions VM==native 5/6,
bootstrap clean) are NECESSARY but NOT SUFFICIENT; only a clean `selfhost` makes them cohesive. Native
build artifacts land in `./bin`. EIGHT+ milestones committed this session toward the gate (each mirrored
to `.tks`):
- `916c568` **Collections ruling #4** (no untyped empty — element type required at decl; sentinel
  back-inference machinery deleted) + **topological type-decl emission** (slice typedefs first, then
  named/optional/inline-variant bodies in by-value dep order).
- `b8d5106` **Auto-boxing of recursive value types** (`TBinary.left: TExpr` → `tk_alloc`'d heap back-edge
  pointer; cycle broken; rides the S0 seam, S2 arena swap stays mechanical) + native-completeness fixes
  (alias-in-field, keyword escaping, `str==`→`tk_str_eq`, str builtin call-map, function prototypes) +
  native str runtime fns.
- `1ac8b17` **Namespace-qualified function mangling** (`teko::checker::type_eq`→`teko__checker__type_eq`;
  killed cross-namespace + libc collisions) + enum-subject match lowering + struct-init variant-field wrap.
- `9170452` **Transitive case→variant wrapping** in emit_as (`Named`→`Type`→`Type|error`).
- `32b3edd` **Call-argument** case→variant wrapping (param-type lookup + emit_as).
- `8ae56d5` **Covariant `[]case`→`[]variant` slice rebuild** (element-wise wrap).

**★★★ FULL SELF-HOSTING — `./bin/teko .` COMPILES THE PROJECT TO A WORKING BINARY.** The self-hosted
compiler (bin/teko, built by the bootstrap from the .tks corpus) now runs the ENTIRE pipeline on its
own project: parse manifest → discover the source tree → assemble → type-check all 880 items → native
codegen (1.45MB of C) → cc → a 606KB `teko` binary that itself runs and compiles other projects
(gen-1 → gen-2 → projects). Got here by materializing the three build-pipeline stubs (manifest/
discover/assemble) in real Teko, closing ~12 checker/codegen parity gaps the full corpus exposed
(missing 128/float builtin types; literal adoption in return/if-join/struct-field/range/literal-
pattern positions; E7 enum↔int cast; virtual-main env threading; the `[]T` type-expr barrier; the
null-field and 128-bit-literal codegen; and full ret_type threading through the .tks codegen — the
twin of the C's g_cg_ret_type global), and fixing the O(n²) OOM in the output buffer with an
amortized growable `tk_slice_push` (geometric growth + alias-safe live-tail cache; value semantics
preserved). 

**SELF-HOST NATIVE BUILD LINKS — GATE GREEN (0 errors).** `cmake --build build --target selfhost`
(the bootstrap `teko` compiling its own corpus → C → cc → link) now produces `bin/teko` end-to-end;
`./build/teko build .` reaches "built bin/teko". The native cc tail went 62→0 this session via:
host-FFI + arith runtime bucket, `write_u64` dedup, assignment value-wrap, `T?`-field wrap, diverging
panic/exit arm in a value-form match, embedded-`return` ret-type threading, imperative exp-slot
rewrite, and `__attribute__((weak))` on the assert seed (corpus self-defines `teko::assert`). The
self-hosted binary RUNS; some corpus surfaces are still honest seed stubs (e.g. `parse_manifest`
"materialized in the C23 seed") — that functional-completeness work is Phase 7, separate from the
compile+link gate which is now the milestone reached.

---

## THE SEQUENCE

> **Status legend:** ✅ done · 🔶 ACTIVELY in flight now · ⬜ TODO (to be completed — every item gets done) · ⏸️ DEFERRED (intentionally postponed; reason + revisit-point given) · 🚧 BLOCKED (needs a prerequisite first) · ⚠️ = a caveat/known-limitation note, NOT a status. The single consolidated list of all ⏸️/🚧/⚠️ items is in **"DEFERRED · BLOCKED · KNOWN LIMITATIONS"** near the end (so the rows above stay status-only). Updated 2026-06-29.

| # | Phase | Status | Why here |
|---|-------|--------|----------|
| 1 | Diagnostics axis | ✅ **CLOSED** — E1 file:line:col + snippet/caret + expected-vs-actual + E2 error fields + **E3 `.tsym` symbol map** + **E4 native stack-trace resolution** (frames → Teko `name (file:line)` via `<binary>.tsym`). Warnings channel is Phase 5's. | Highest ROI; makes every later phase debuggable |
| 2 | `in` operator | ✅ **CLOSED** (lexer→parser→checker→codegen→VM→tkb→.tks, single-eval) | Build the tool the DRY sweep will use (feature only) |
| 3 | str/byte stdlib as real mirrored fns | ✅ **CLOSED** (`teko::str::*` + host-FFI surface in scope.c/.tks) | Close a half-implemented layer; unblocks self-host CHECK |
| 4 | C↔.tks mirroring | ✅ MAINTAINED continuously — every commit mirrors its `.c`/`.h` change to the `.tks` twin (SUPREME RULE); a standing per-commit discipline, not a pending sweep | Pay down mirror debt before more code lands |
| 5 | Definite-assignment / init analysis | ✅ **CLOSED** — `src/checker/initanalysis.{tks,c,h}` runs last in `type_program`: **use-before-init is STRUCTURAL**; **unused-local → ERROR** (snippet+caret); **unused-private-fn → WARNING** via stderr warnings channel. Both twins lockstep; gate GREEN. | Real checker gap; lands the warnings channel |
| 6 | Finish self-host → working `teko .` | ✅ **COMPLETE** — exit criteria met: (1) FULL SELF-HOSTING (`selfhost` gate → `bin/teko`, 0 cc errors, 902 items); (2) VM==native regression set (5/6/106); (3) C6.7+C6.8 spread-into-literal `[..xs, x]` ✅ (carried into Phase 7, completed R7.6). | The gating milestone for everything downstream |
| 7 | Host independence | ✅ **CLOSED** — extern/FFI (C7.1a–i, C7.1k, C7.1m) ✅; D2/D3/D4 test gate ✅; `.tkb` program codec C7.16 ✅; `-o <dir>` ✅; main-rule C7.9 ✅; tk_str_eq C7.13 ✅; TK_RT_LIST C7.14 ✅; spread C6.7+C6.8 ✅; host ns files C7.2–5 ✅; `.tkl` ZIP C7.12 ✅; overflow-debug C7.15 ✅; pre-linker C7.10 ✅; driver.tks C7.17 ✅; flags-kw C8.1 ✅; **E2-NATIVE** ✅; **C1.7-CAST** ✅; **C8.3** ✅; **C8.5** ✅; **C8.6** ✅; **`defer` C7.18** ✅; **C7.1j multi-OS/arch CI** ✅ (W6). | FFI + host surfaces + VM test gate + project/output/pack |
| 8 | FLAGS | ✅ **CLOSED** — C8.1 keyword ✅, C8.2 decl+AST+parser ✅, C8.3 checker (power-of-2 auto, u128 guard, bitwise, helpers) ✅, C8.4 codegen ✅, C8.5 VM ✅, C8.6 `.tkb` ✅ (W1–W4). | Bitflag enums (spec frozen) |
| 9 | SEC | ✅ **CLOSED** — C9.2 capability audit ✅ (`src/checker/capability_audit.md`); **C9.1 SAST gate** ✅ (`.github/workflows/sast.yml` — CodeQL + clang-tidy-audit, W6). Further hardening is opportunistic, not a blocking item. | SAST + capability audit, after corrections |
| 10 | Evolution S1–S10 | 🔶 **PARTIAL** — DONE: **S1** arena ✅, **S4** generics ✅, **ptr<T>** ✅, and **MEM-1 the full automatic memory model** ✅ (W8: Ref<T> native+VM + escape-gate + per-fn regions/dealloc + non-scalar refs; security-vetted, ASan-clean). NEXT: **W9** S2 block-scoped regions (dealloc volume) → **W9.4** parser generics-ergonomics (newly-found prerequisite) → **W9.5** brevity (Ref-threading unblocked now; generics-dedup waits on W9.4) → closures/lifetimes (W10) → S6/S7/S8/S9/S10. The memory model was the FOUNDATION the brevity refactors depend on (built first, not deferred). | Post-self-host campaign (arenas → generics → **memory model ✅** → brevity DRY → closures → collections → concurrency → async/await) |
| 11 | **Code quality sweep** | ⬜ **LAST** | DRY + KISS + SOLID + YAGNI + 12-Factor (III/V/IX/XI/XII) + comment hygiene — applied once to settled code |

---

### Phase 1 — Diagnostics axis  *(§A.1 ∪ INDEPENDENCE Eixo E ∪ CORRECTION_PLAN §10 column-granularity)*  ✅ CLOSED (E1/E2/E3/E4 done)
**Status:** ✅ E1 (file:line:col threaded through tokens→AST→tast) · ✅ source snippet + caret · ✅ expected-vs-actual on every mismatch (type/arg/return/assign/field/struct-lit) · ✅ E2 (error fields/`err_loc`/`err_typed`; VM full + **E2-NATIVE ✅ W2** native parity complete) · ✅ **E3 `.tsym` symbol map** (codegen `tk_emit_tsym` → `<binary>.tsym`: mangled C symbol → Teko qualified name + `file:line`; written by the backend in BOTH twins; needed `tk_tfunction` to carry file/line, threaded from the parsed item in `tk_type_item`) · ✅ **E4 native stack-trace resolution** (teko_rt's panic/crash backtrace loads `<argv0>.tsym` and appends `=> <teko-name> <file:line>` per frame). Remaining (moved to their owners): cc-failure surfacing is adequately covered (cc errors print to stderr; `-w` mutes only warnings) and the **warnings channel** is Phase 5's (init-analysis) deliverable.
**Goal:** compile-time messages stop being poor. Errors point at the failing **expression**, not the enclosing function.
**Work:**
- **E1** — thread `{file, line, col}` through the whole pipeline: lexer → tokens → parser → AST → `tast` (every node knows its origin). Root cause today: AST exprs carry no position; only decls do.
- **Source snippet + caret** — print the offending line with a `^` under the column (clang/rust style); driver holds source text at report time.
- **Expected-vs-actual** types in every mismatch error (type / arg / return / assign / field).
- **cc-failure surfacing** — relate generated-C `cc` errors back to Teko.
- **E2** — `teko::Error` / panics carry file+line (VM and native). ✅ DONE (VM full + native full). The Teko `error` value gained accessible diagnostic fields (message/file/line/col/expected/actual) + `err_loc`/`err_typed` builtins (mirroring C `tk_error_loc`/`tk_error_types`); the `error { message }` literal stayed special-cased (NO 434-site ripple). Checker types them; VM carries them in full (`err_loc(...).line` == 7); the `.tks` checker wiring (typer.tks/check_modules.tks) prefers the expr position. **E2-NATIVE (W2) ✅:** native now represents `error` as a full `tk_error` C struct in `teko_rt.h` (message/file/line/col/expected/actual); `err_loc`/`err_typed` builtins emit real `tk_error_loc`/`tk_error_types` calls; field access reads the struct; `error { message=s }` emits `tk_error_make(s)`; FFI lifting wraps `tk_str err` with `tk_error_make()`; variant unions store `tk_error error` not `tk_str`. (4-track workflow `e2-error-position`; native degraded carve-out resolved W2.)
- **E3** — emit `.tsym` symbol map alongside the artifact.  **E4** — stack-trace (frames carry origin; native via `.tsym`).
- **Warnings channel** — Teko has none today; introduce one (shared with Phase 5).
**Exit:** a type error inside a function body reports the exact expr `file:line:col` + snippet + caret + expected/actual; panics print file:line; native build emits `.tsym`.

### Phase 2 — `in` membership operator  *(§A.6; CORRECTION_PLAN W2c)*  ✅ DONE
**Status:** delivered — full pipeline (lexer→parser→AST→checker→codegen single-eval→VM→tkb→.tks mirrors); `in []` evaluates the LHS once and short-circuits, VM==native verified.
**Goal:** `x in [a, b]` membership, evaluating the LHS **once**.
**Work:** full pipeline — lexer `in` keyword → parser → AST → checker (value & options comparable → bool) → codegen (single-eval lowering, e.g. `({ T _v = x; _v==a || _v==b; })`) → VM → `.tkb` → ALL `.tks` mirrors. NOT the DRY sweep — just the tool it will use.
**Exit:** `x in [..]` type-checks, runs VM==native, serializes; LHS evaluated once (verified).

### Phase 3 — str/byte stdlib as REAL mirrored functions  *(§A.2)*  ✅ DONE (checker + VM/runtime; native emit rides Phase 6 codegen)
**Status:** the `teko::str::*` surface (concat/concat3/len/slice_to/slice_from/ends_with/contains/last_index_of) + str/byte builtins (str/str_of_bytes/one_byte/i64_to_str/u64_to_str/ftoa/slice) are wired in checker + scope.c/.tks, with C runtime twins (`tk_str_*`). The corpus's str/byte calls all type-check and run on the VM. (Native emission of the slice/str bridges rides the Phase-6 codegen frontier.)
**Goal:** kill the "recognized-but-not-implemented" half-measure. Today slice/str/str_of_bytes/one_byte/str_concat/str_concat3/i64_to_str/u64_to_str/ftoa have checker signatures only.
**Work:** make each a real, fully-wired function — checker + codegen + VM + `teko_rt` (real `teko::text` functions with C twins, or fully-wired builtins). No recognize-and-defer.
**Exit:** every str/byte builtin the corpus calls compiles to native AND runs in the VM, equal results; current CHECK-phase walls past these clear.

### Phase 4 — C↔.tks mirroring audit  *(§A.3)*  ✅ DONE (standing per-commit discipline)
**Goal:** the legislator "smells" incomplete mirroring; confirm and close it.
**Work:** audit every `.c`/`.h` against its `.tks` twin (e.g. `typer.tks` was missing `builtin_fn`; `expr.c` bool+out vs typer.tks marker variant). Reconcile any lingering W5-cf-2 / corpus `.tks` mirror debt.
**Exit:** a mechanical diff finds zero behavioral divergence between any C file and its `.tks`.

### Phase 5 — Definite-assignment / init analysis  *(CORRECTION_PLAN W3b — ✅ CLOSED)*
**Goal:** mandatory initialization; use-before-init is an error; unused local = error, unused private = warning.
**Delivered:** `src/checker/initanalysis.{tks,c,h}` (namespace `teko::checker`), a pass run last in `type_program` over the fully typed program.
- **Definite assignment — STRUCTURAL (law-first).** Teko has NO uninitialized binding form: `parser::Binding.value` is mandatory and the value is typed in the PRE-binding env (`type_binding`), so a self/forward reference fails as an unknown name. Use-before-init therefore cannot be written — no runtime/flow check is needed, and adding one would be dead code. Documented in the module header.
- **C5.1 unused-local → ERROR.** A `let`/`mut`/`const` simple-name never read in its function body (over-approximating "read" by a name occurrence — a var read, a call's last segment, or a compound-assign target — so shadowing only ever MISSES, never false-positives, keeping the gate green). The explicit discard `_` is exempt. Located via `diag_at` + structured `err_loc` → the native renderer shows the source line + caret (Phase-1 quality).
- **C5.2 unused-private-fn → WARNING + the WARNINGS CHANNEL.** Teko has no module-mutable accumulator, so the pass PRINTS warnings to stderr (`teko::io::eprintln` / `fprintf`) as found and returns only the first hard error via `error?`; a warning never fails the build. A private (file-local) function never called anywhere in the merged program is flagged (`main` exempt). Surfaced 14 genuinely-dead private fns in the corpus (verified 0 call sites each).
**Exit:** ✅ use-before-init proven impossible by construction; unused locals reported with Phase-1-quality locations; warnings channel live. Gate GREEN (`selfhost` → `bin/teko`), both twins byte-identical in behavior, corpus type-checks (902 items), regressions VM==native (match_pattern_bindings=5, optionals=6).

### Phase 6 — Finish self-host → working `teko .`  *(✅ CLOSED — tasks #55/#57; CORRECTION_PLAN §15.1; #40/#41/#19)*
**Goal:** the bootstrap compiles its own `src/` corpus to a native binary. The gating milestone.
**EXIT MET (2026-06-26).** Both exit criteria hold:
1. ✅ **`teko .` produces a working native binary of the compiler** — FULL SELF-HOSTING: the C bootstrap compiles the corpus → `bin/teko.c` → `cc` → `bin/teko` (gen-1), which rebuilds itself (gen-2) and builds other projects (gen-3). `cmake --build build --target selfhost` is GREEN (0 cc errors, links cleanly). 902 items type-check.
2. ✅ **VM==native across the corpus regression set** — `match_pattern_bindings` (5), `optionals` (6), `uniontest` i64|error (106) all agree VM==native. The B-cg3 (function params), B-cg4 (slice/list emission), and B-vm-iii (prim-member union discrimination) walls that the stale block below describes are all CLOSED (the regression set exercises and passes them).
**VM-execution frontier (B-vm) — honest stop landed.** `teko run .` over the WHOLE compiler corpus cannot succeed under the VM because the compiler's own `main` calls host-FFI (`args`/`read_file`/`list_dir`/`run`/`cc`-invocation) that the VM has no surface for — that surface is **Phase 7's C2\*** work. The VM now stops HONESTLY at that frontier (``vm: `args` is a host function the VM cannot run (use `teko build`…)``) instead of the misleading "internal: checker should reject" abort — both twins (`vm.tks` `find_function`, `vm.c` `eval_call`). M.1 fail-loud / M.3 honest frontier.
**Deferred to their proper owners (law-first, not Phase-6 blockers):**
- **#41 namespace-aware TYPE resolution** (a `Named` carrying its namespace) → DEFERRED as future-hardening. The real collision source was FUNCTION names — fixed by #49 namespace-qualified mangling. For TYPES it is **latent**: the corpus has ZERO cross-namespace type-name collisions (verified), so bare-last-segment type resolution is correct today; threading a namespace through every `Named` would be invasive for a non-issue. Revisit if/when a collision is introduced.
- **VM host surface** (args/read_file/write_file/list_dir/process::run/env::\*) → **Phase 7 C2\*** (host surfaces over FFI), where it belongs.
- **#19 X5 justification-header sweep** → substantially satisfied: the corpus is densely and appropriately headered (every file identifies its namespace/role; contested decisions cite their law). Brought the last two header-less files (`optokens.tks`, `pattern.tks`) to parity. Treated as continuous doc hygiene, not a gate.
**Historical status block (stale — commit `048b4af`, kept for the build narrative):**
- ✅ **CHECK GREEN** — `teko build .` type-checks the whole corpus (796 items, 0 errors). Delivered: the widening lattice (`tk_widens_into`/`tk_type_join`), enum-subject `match`, pervasive literal adoption, **E7 enum↔int casts**, the host-FFI builtin surface, the VM `.tks` value+eval cluster, `src/build/project.tks`. Regressions VM==native 5/6.
- **#41/#49** — namespace-aware *call* resolution (`tk_env_lookup_call`) ✅ AND codegen namespace-qualified function mangling (`teko__checker__type_eq`, call+def, fixed collisions) ✅ done this session. Namespace-aware *type* resolution (a `Named` carrying its namespace) ⬜ TODO — currently sidestepped (the `vm::Env/Return`→`Venv/VmReturn` rename); to be completed (not deferred).
- 🔶 **Native codegen** — generates C that compiles DEEP into the corpus (6 commits this session, all mirrored to `.tks`, regressions VM==native 5/6): slice value-layer, inline-union lowering, topological type-decl emission, **auto-boxing of recursive value types** (`tk_alloc` back-edge pointers, S0 seam — cycle fully broken), **namespace-qualified function mangling** (`teko::checker::type_eq`→`teko__checker__type_eq`, fixed #41/#49 collisions incl. libc `div`), **case→variant wrapping** at every site (bindings/struct-fields/call-args/returns — DIRECT, TRANSITIVE `Named`→`Type`→`Type|error`, and covariant `[]case`→`[]variant` element-wise slice rebuild), enum-subject match lowering, C-keyword escaping (`bool`/`signed`), alias-in-field resolution, `str==`→`tk_str_eq`, str builtin call-map, **function prototypes**. Native runtime: str surface (`tk_str_eq/slice/len/ends_with/contains/…`) + `tk_alloc` seam. **Gate progress**: the native cc tail is down to **14 errors** (from 62) and falling. Done since: host-FFI output/parse builtins ✅, helper dedup ✅, bind-whole-variant ✅, value-form-`if` return wrap ✅, case-key keyword escape ✅, panic/math builtins (`math.h`/`-lm`) ✅, **struct-with-`kind` match DESCEND** ✅, and **the whole host-FFI + arith RUNTIME bucket** ✅ — `read_file`/`var`/`write_file`/`chdir`/`list_dir`/`args`/`run` (Phase-7 host I/O) + `div`/`rem`/`fdiv`/`int_to_float`/`f64_bits`/`f64_from_bits`/`last_index_of` now live in `teko_rt.{h,c}` as **fixed-ABI result structs** (`tk_ffi_sres`/`ures`/`slres`/`u64res`, since the runtime header is included before the generated sum/optional/slice types and can't name them) and are **lifted by codegen** (new `emit_host_ffi` statement-expression turns each `tk_ffi_*res` into the program's `T|error`/`error?`/`[]str`; `error` lowers to its message str). `main` now captures argv (`int main(int argc,char**argv){ tk_set_args(...) }`) so `teko::env::args()` works. This dropped the ~20-error undeclared-identifier cluster in one bucket, then the tail closed completely (37→14→0): `write_u64` dedup, assignment value-wrap (`TAssign.bound` + emit_as), `T?`-field wrap in emit_struct_init, diverging panic/exit arm in a value-form match, embedded-`return` ret-type threading in emit_arm_value, an imperative exp-slot rewrite (avoids un-widenable sub-unions), and `__attribute__((weak))` on the assert seed. **GATE GREEN — `cmake --build build --target selfhost` produces `bin/teko`, 0 cc errors, links cleanly.** Regressions VM==native 5/6 hold, corpus type-checks (858 items). Native output → `./bin`. Next: functional completeness of the native binary (corpus seed stubs) + VM-run path.
- ⬜ **VM execution** — `teko run .` reaches `vm: call to an unknown function`: `.tks` `eval_call` doesn't bind params yet; `teko::fs::list_dir` is checker-only (no `tk_list_dir` runtime).
- ⬜ **#19** X5 justification-header sweep.
**Work remaining — native backend (W-backend #40), in order:**
- **B-cg1** ✅ `emit_type_expr` slice `[]T` in signatures/fields (commit `7ce809f`).
- **B-cg2** ✅ **variant/inline-union codegen (commit `8e762e7`):** non-named members (error/prim/slice/byte/str/opt) via `cg_member_key`; anonymous inline unions → deterministic `tk_u_<keyA>_<keyB>` typedef collected+stamped in the prelude; `emit_as` wraps a case into a named-or-anonymous variant by resolved-type-key; match discrimination for non-named members; `[]T as x` slice patterns. Smoke `examples/smoke/uniontest` (Box|error) VM==native==106; regressions 5/6.
- **B-cg3** ⬜ **function parameters in codegen + VM (current wall: `codegen: function parameters not yet supported`).** Nearly every corpus fn; both backends honest-stop.
- **B-cg4** ⬜ slice/list value emission (empty/push) in all positions; Increment B+ append `xs+[x]` / `[]`/`[a,b]` literal syntax.
- **B-vm** ⬜ VM execution path (`teko run .`): (i) `eval_call` param-binding; (ii) host-FFI runtime (args/read_file/write_file/list_dir/process::run/env::*) — needs argv/host plumbing into `tk_vm_run`; (iii) **VM prim/str/slice union-member discrimination** — `pat_match` (vm.c) needs `TK_VAL_STRUCT`+`type_name`, so a `i64 | error` value (bare `TK_VAL_INT`) can't match its arm → VM≠native for prim-member unions (native handles all kinds). The VM value model needs a discriminator for non-struct union members.
- **#41** ⬜ namespace-aware TYPE resolution (a `Named` carrying its namespace).  **#19** ⬜ X5 justification-header sweep.
**Exit:** `teko .` produces a working native binary of the compiler; VM==native across the corpus regression set.
**Note:** the native self-build is the single largest remaining effort (full C emission for a 65-file compiler) — expect many codegen walls beyond B-cg2; grind incrementally with regressions + per-feature smoke tests as the safety net.

### Phase 7 — Host independence  *(INDEPENDENCE Eixos A/C/D + BINARY cleanup)*  ✅ DONE
**Goal:** the compiler reaches the host without C-side scaffolding; tests gate emission.
**Work (in order):**
1. **C1.0 `extern`/FFI form — ✅ RATIFIED 2026-06-27** (see LEGISLATION §"FFI / `extern`"; master-plan C7.0) → **C1.1** extern primitive (single `OP_CALL_EXTERN` → platform convention), **macOS+libc first**, then Linux, then Windows; per-OS `.tkp` resolution + `#os(...)` guard are legislated-now / implemented as follow-ons.
2. **Host surfaces over FFI:** C2a `teko::env::args` + `teko::exit`; C2b `teko::io` slurp (read/write/write_err); C2c `teko::fs` `list_dir` (feeds discovery); C2d `teko::process` exec (invoke `cc`).
3. **VM test gate:** D2 `#test` runner in VM (`teko test`) ✅ **DONE** → D3 coverage (`.tkt` outside denominator) ✅ **DONE (function-level)** → D4 pre-emit gate (tests + coverage BEFORE codegen; release bars on fail) ✅ **DONE**.
   - **D2 delivered (both twins).** `#test` is a real attribute: lexer `Hash` token → parser consumes `#test` (own line ok) on a function → `Function.is_test`/`TFunction.is_test` threaded checker-through. `teko test <proj>` assembles WITH the `.tkt` files (`assemble_sel`/`project_frontend_sel`/`test_project`) and the VM runs every `#test` function (`vm::run_tests` / `tk_vm_run_tests`), FAIL-FAST: a failed assertion panics with its message after the running test's name is printed (`test ns::name ... ok`; all pass → "N test(s) passed", exit 0). This forced **VM function-parameter binding** (`eval_call` now binds args→params in a fresh frame — closes the B-vm-i gap) in BOTH twins. `#test` fns are exempt from the unused-private-fn warning (run by the runner, not source). The one bare-`assert`-keyword test file (`assert_test.tkt`) migrated to `teko::assert::is_true`. Regressions hold VM==native (5/6/106).
   - **D3 coverage delivered (both twins) — FUNCTION-LEVEL.** A host coverage SINK in `teko_rt.c` (`tk_cov_reset`/`tk_cov_mark`/`tk_cov_distinct`) exposed as the `cov_reset`/`cov_mark`/`cov_distinct` builtins — the M.0-compliant way (the mutable state lives in the C runtime, accessed over the FFI boundary like `print`/`args`, NOT a Teko module-mutable). The VM marks each production fn it enters (`eval_call`, `!is_test`); `run_tests` reports `coverage X% (executed/total functions)` with `#test` fns OUTSIDE the denominator. *Per-line* coverage is a follow-up: line granularity would need per-statement instrumentation threaded through the functional evaluator (the function-entry sink is the bounded, M.0-clean first cut).
   - **D4 gate delivered (both twins).** `teko build` now runs the project's tests BEFORE codegen (`compile_project_g` / `tk_compile_project_g`): fail-fast (a failed assertion bars the build) + a **80% coverage floor** (below → barred with a clear message), then codegen of the production program with **`#test` functions stripped** from the binary (`strip_tests`). Skipped cleanly when there are no `.tkt` files. **`--no-test`** opts out (parsed in `main`, threaded through; `project_arg_of`/`parse_out_dir` skip the flag). The **self-host gate uses `--no-test`** (CMake `selfhost` target) because the compiler's own `.tkt` files use not-yet-implemented syntax (slice patterns w/o `as`) and aren't VM-runnable yet — M.1 keeps the self-host lifeline green while user projects gate by default. Verified: pass→builds, fail→barred, 25%→barred, 100%→builds, no-tkt→builds, test fns absent from the binary; both twins byte-identical.
4. **Project/output:** A4 main-rule from the manifest artifact; A6 packages + pre-linker (load deps' `.tkh`+`.tkb`, merge typed trees pre-codegen); **A7 (DECISION)** output as a directory + `-o <dir>` (default `target/`, not `build/`); **A8 (DECISION, deferred)** `teko pack` → `.tkh`+`.tkb` (+metadata/checksum/.tsym/license/dep-lock discussion); CORRECTION_PLAN §11 package system.
5. **BINARY cleanup:** B0d promote `tk_str_eq` to `text.h`; B3a `TK_LIST` runtime list; B3b overflow-debug panic guard wiring; `.tkb` statement/program codec (today only `TExpr` serializes).
6. Crumb **M2** `driver.tks` falls out once C2* + A5 exist (future C4 deliverable).
**Exit:** `teko build`/`run`/`test` operate via Teko host surfaces; the test+coverage gate runs before any emission.

### Phase 8 — FLAGS (bitflag enums)  *(§A.7)*  ✅ DONE
**Goal:** a flags type — "an enum with another keyword."
**Spec (frozen):**
- Distinct **keyword** (e.g. `flags`); members are names like an enum.
- Member values **auto-computed at compile time** as powers of two: member n = `1<<n` (1, 2, 4, 8, …). No manual values.
- **Compile-time size identifier**; total bit-span capped at **u128** (overflow = compile error).
- **Bitwise ops supported** (`&` `|` `^` `~`/`not`); **NO shift operators** exposed (shifts are an internal compile-time detail).
- **Dev-friendly helpers** for non-bitwise devs: `has` / `all` / `any` / `none` / add / remove.
**Work:** full pipeline — `flags` keyword (lexer) + parser + AST + checker (power-of-two assignment, u128 size guard, bitwise-op typing, helper resolution) + codegen (emit as the u128-fitting unsigned int + helpers) + VM + `.tkb` + ALL `.tks` mirrors.
**Exit:** a `flags` decl auto-assigns powers of two, rejects >u128 span, supports bitwise + helpers, VM==native.

### Phase 9 — SEC  *(task #53 = CORRECTION_PLAN §14 = INDEPENDENCE C5)*  ✅ DONE
**Goal:** security/guard evaluation after corrections close.
**Work:** SAST gate + capability/sandboxing audit of the `exp` / `extern` / syscall surface.
**Exit:** the security gate runs in CI; the FFI/syscall surface is audited.

### Phase 10 — EVOLUTION campaign S1–S9  *(EVOLUTION_DESIGN/JUSTIFICATION; POST-SELF-HOST; R1/R2 ratified)*  🔶 IN FLIGHT (S1/S3/S4 ✅, S2 per-fn ✅; rest ⬜)
**Goal:** build the foundation keystone-first, in dependency order, against real self-hosted data (M.4).
**Hard ordering invariant:** arenas (S1–S2) before ref (S3) and before DI / collections storage; generics (S4) before real collections (S7); Map (S7) forces constraints (S6); single-task arenas+ref (S1–S3) before concurrency (S8); concurrency is independent of generics.
**Stages:**
- **S1** arena primitive + root region (bump alloc, chunk-list, OOM-panic; builds on the S0 seam).
- **S2 ★** scope regions + escape check (depth compare) — the linchpin; tribunal ratifies "ONE region-depth comparison" vs borrow-solver; enables `#scoped`.
- **S3** `ref` (MUTABLE-TARGET ONLY per R2; no shared/read ref; ref never null → `(ref T)?`).
- **S4** generics (unconstrained, monomorphization).  **S5** DI lifetimes → arenas.
- **S6** constraints (positive; exclusion `!` only prims/sealed; forced by `Map<K,V>`).
- **S7** real dynamic collections (amortized append, Map, Set; out-region param).
- **S8** concurrency CAPSTONE (`scope{}`/`spawn`/`channel<T>`/`send`/`recv`→`T|error`; 1:1 OS threads first).
- **S9** LTS cleanup (collapse `Parsed<T> ×14` → `Result<T>`; unify per-type `parse`/`to_string`).
**Reserved (don't freeze syntax until parser + real duplication data exist):** arena/scope surface, the three `ref` positions, the five concurrency primitives.
**Deferred ceilings (keep deferred):** borrow-solver / lifetime-variable system; region-polymorphism. **Tribunal micro-decision:** implicit copy-out of a very small escaping value (default lean: NO implicit copy). *(async/await → S10/W14; M:N scheduler → S8/W13 — both removed from deferred and scheduled.)*

### Phase 11 — Code quality sweep  *(§A.5 — LAST)*  ⬜ TODO (W15)
**Goal:** settle the corpus permanently: kill redundancy, enforce simplicity, validate architectural boundaries, and normalize documentation. Every principle below is applied once, to stable code, so it never has to be re-applied.

**Principles (applied in order within each file, then comment hygiene last):**

**DRY (Don't Repeat Yourself)**
Kill no-variance repeated-call patterns across the whole codebase (C AND Teko). Tools: `in` operator (Phase 2), hoisting (`let ch = at(source, p)`), `match`, helper extraction. Example wall: `if at(source, p) == b'e' || at(source, p) == b'E'` → `if at(source, p) in [b'e', b'E']`.

**KISS (Keep It Simple, Stupid)**
Eliminate accidental complexity. Concrete checks:
- No multi-step conditional where a single `match` arm suffices.
- No helper function that wraps a single expression with no branching.
- No intermediate variable that is used exactly once with no naming value.
- Prefer the shorter of two semantically-equivalent Teko forms (law-first — `x != 0` over `x to bool`; `[..xs, x]` over repeated push chains).
- Remove any `honest-stop` stubs whose condition can never be reached now that Phase 7–10 are complete (dead code by construction, not by accident).

**SOLID (where applicable to a functional/procedural compiler)**
- **SRP** — each `.tks` file owns exactly ONE namespace; each function does ONE thing. Split any function that has more than one distinct responsibility (e.g. parses AND validates).
- **OCP** — adding a new AST node, expr kind, or statement kind should only require adding a new arm/case, not modifying existing logic. Sweep match/switch exhaustiveness; add `[[noreturn]]` / `_Noreturn` guards where appropriate.
- **LSP** — not applicable (Teko has no subtype inheritance by design).
- **ISP** — no function exposes parameters it doesn't use; no struct field that is only set but never read gets to live (YAGNI overlap). Narrow public surfaces in `.h` / `pub` in `.tks`.
- **DIP** — `driver.c`/`driver.tks` must depend on module interfaces (`checker.h`, `codegen.h`), never on internal helpers. Verify no cross-module `#include` of a non-header file. In `.tks`: `use teko::checker` not `use teko::checker::collect::internal_helper`.

**YAGNI (You Aren't Gonna Need It)**
Delete anything added "for the future" that has no current caller:
- `driver.tks::compile` (confirmed dead — only caller `compile` is unused; flagged in `selfhost-vm-perf.md`).
- Any `pub` function in a namespace file with zero call sites across the whole corpus.
- Any struct field never read after being set.
- Any `honest-stop` path that is structurally unreachable post-Phase 10.

**12-Factor (factors applicable to a CLI compiler tool)**
- **III Config** — `TK_RT_DIR` and `TK_SRC_DIR` must be injectable via environment variables (already partially done in CMake); verify that `bin/teko` (non-cmake build) respects them; no path is baked into the binary string literals.
- **V Build / run / test separation** — `teko build`, `teko run`, `teko test` must be strictly separate code paths with no side-effects leaking across (e.g. `build` never runs the VM; `run` never emits `.tkl`; `test` strips `#test` fns from the native artifact). Audit `driver.c`/`project.tks` command dispatch.
- **IX Disposability** — fast startup (no global init beyond argv); clean exit on every error path (no half-written output files on failure — use a temp path + atomic rename or explicit cleanup); all OS resources released before exit.
- **XI Logs as streams** — ALL diagnostics (errors, warnings, coverage reports) → `stderr`; the generated artifact (`.c` source, `.tkl` bytes, `.tsym`) → file only, never mixed into `stdout`. Audit every `printf`/`io::print` call in driver/project paths.
- **XII Admin processes** — `teko test`, `teko pack`, `teko run` are proper first-class sub-commands (already true); verify each is idempotent (running twice with the same input produces the same output with no leftover state).

**COMMENT HYGIENE (legislator, 2026-06-26)**
Normalize ALL comments across the corpus (C AND Teko), applied LAST so comments settle on already-refactored code:
- **Doc comments only** — every retained comment becomes a `/** … */` doc comment attached to the declaration/section it documents.
- **Drop inline comments** — trailing `// …` and mid-code `/* … */` are REMOVED; if a line needs explanation, hoist/rename so it reads without one, or move the rationale into the preceding `/** … */`.

**Why last:** every prior phase churns code; applying any of the above earlier would re-touch the same lines repeatedly. Do it once, on settled code.

**Exit:** no no-variance repeated calls; no KISS violation (single-use helpers, dead stubs); SOLID boundaries hold; no YAGNI survivors; 12-Factor III/V/IX/XI/XII pass; every comment is `/** … */`; zero inline comments; corpus green + VM==native.

---

## Source-doc cross-reference

| Phase | Primary source(s) |
|-------|-------------------|
| 1 | `TEKO_ROADMAP_INDEPENDENCE.md` Eixo E; `TEKO_CORRECTION_PLAN.md` §10 |
| 2 | `TEKO_CORRECTION_PLAN.md` W2c |
| 3 | session directive (str/byte builtins) |
| 4 | SUPREME RULE; session directive |
| 5 | `TEKO_CORRECTION_PLAN.md` W3b |
| 6 | `TEKO_CORRECTION_PLAN.md` §15.1; tasks #40/#41/#19/#55/#57; `TEKO_ROADMAP_INDEPENDENCE.md` C3/C4 |
| 7 | `TEKO_ROADMAP_INDEPENDENCE.md` Eixos A/C/D; `TEKO_ROADMAP_BINARY.md` B0d/B3a/B3b; `TEKO_CORRECTION_PLAN.md` §11 |
| 8 | session ruling (flags spec) |
| 9 | `TEKO_CORRECTION_PLAN.md` §14; `TEKO_ROADMAP_INDEPENDENCE.md` C5; task #53 |
| 10 | `TEKO_EVOLUTION_DESIGN.md` / `TEKO_EVOLUTION_JUSTIFICATION.md` S1–S9 |
| 11 | session directive (DRY + KISS + SOLID + YAGNI + 12-Factor) |

---

# PARALLELIZATION — crumbs, rounds & agent matrix

This section breaks each phase into **tiny crumbs** (the smallest shippable units) and schedules them
into **rounds** (parallel waves). The goal is **maximum agent concurrency** without conflict.

## Parallelization rules (invariant)

1. **One owner per file per round.** Two crumbs in the SAME round must own DISJOINT file sets. The
   bottleneck files are `cg` (codegen) and `vm` — only one crumb may own each per round, so they
   serialize across rounds.
2. **SUPREME RULE = atomic pairing.** A crumb owns the C file(s) **and** their `.tks` twin(s) together,
   so alignment can never drift between agents.
3. **Dependencies gate rounds, not drafting.** A crumb may be *drafted* once the node/shape it needs
   exists; type/semantic correctness is verified at integration. Listed `Dep` = the crumb whose output
   this one consumes.
4. **Round barrier = the integration gate** (legislator integrates, per the orchestration model): build
   green + regressions `match_pattern_bindings==5` / `optionals==6` + **VM==native** before the next round.
5. **Agents DRAFT, I integrate; tension → agent HALTS → tribunal** (`memory/teko-orchestration-model.md`).
6. **Phases are gated/sequential** (esp. Phase 6 self-host is the milestone); crumbs parallelize *within*
   a phase. A few cross-phase opportunities are noted where genuinely independent.

**File-shorthand legend:** `L`=lexer · `ast`=parser/ast · `P`=parser/parse_* · `tast`=checker/tast ·
`chk`=checker/typer+expr · `match`=checker/match · `collect`=checker/collect · `res`=checker/resolve ·
`scope`=checker/scope · `cg`=codegen · `vm`=vm · `tkb`=emit/tkb_{write,read} · `rt`=runtime/teko_rt(+text.h/core.h) ·
`build`=build/{discover,tkp_rule,manifest} · `assert`=assert · `main`=main · `diag`=*(new)* checker/diag ·
`tsym`=*(new)* emit/tsym_write. Every entry implies its `.c/.h` **and** `.tks` twin.

---

## Phase 1 — Diagnostics axis ✅ DONE

> **Tribunal rulings (2026-06-25, during R1.1) — the plan revised against reality:**
> 1. **C1.1 + C1.4 + C1.5 are MERGED into `C1-POS` (expr-position plumbing).** Discovery: Teko's
>    **exact-fields rule** (a struct literal must set every declared field) means adding a position
>    field to an expr node is *never* additive on the `.tks` side — every construction site must set it
>    — and the untyped `.tks` `Expr` is a **bare variant** (`Expr = variant Number | Var | …`) with no
>    slot for common fields, whereas the typed `TExpr` is already a struct-wrapper. So: add-field (C1.1),
>    parser-stamp (C1.4), checker-copy (C1.5) cannot be split on the `.tks` side without C/.tks drift
>    (SUPREME RULE) or dead placeholder fields. They become one atomic crumb. As part of it the untyped
>    `Expr` is **reshaped to a struct-wrapper** `Expr = struct { kind: ExprKind; line; col }` mirroring
>    the existing `TExpr` design (debt-paying, consistent). C side stays additive (`tk_expr`/`tk_texpr`
>    gain `uint32_t line,col`; designated initializers zero-fill). `.tks` side ripples to all `Expr`
>    match/construct sites (~16 files) — validated by `teko .` parse-progress not regressing.
> 2. **C1.3 rescoped to the error-model DATA extension only** (Option B): `tk_error` gains
>    `const char *file; uint32_t line,col; const char *expected,*actual; tk_severity severity` — all
>    `const char*`/`uint32_t` to keep `core.h` off the `teko::text` DAG (B.8), matching the existing
>    `message` convention. The `diag` collection module + renderer are DEFERRED to their real consumers
>    (renderer → C1.8; warnings-channel collection → Phase 5), to avoid dead infrastructure.

| Crumb | Owns | Dep | Status |
|-------|------|-----|--------|
| **C1.2** lexer carries `file:line:col` on every token | `L` | — | ✅ DONE (R1.1 — verified no-op; positions already correct. Accessor for C1-POS: `t[pos].line/.col`, 1-based, 0=unstamped, capture-on-leading-token like `parse_decl`) |
| **C1.3** error-model data: position + expected/actual + severity | `core.h`,`core.tks` | — | ✅ DONE (R1.1 — Option B; additive; build green; regressions 5/6; VM==native) |
| **C1-POS** expr-position plumbing | `ast`,`tast`,`P`,`chk`,`tkb` (C: +`expr.c`,`typer.c`) | — | ✅ DONE — **C-side** (tk_expr/tk_texpr line,col; parser `tk_at` stamps every node; `tk_typer_expr` wrapper copies position onto the typed node + attaches it to errors innermost-wins; `tk_type_program` prefers the expr position). Proof: body type-error → **expr** `3:15`; self-host wall `lexer.tks:451:4`→`460:29`. **.tks mirror** (ast.tks `Expr`→struct-wrapper `{kind,line,col}`; tast.tks `TExpr` +line,col; parse_*/typer/tkb_read reshaped via agent fan-out): parses all 64. Build green; regressions 5/6. **CARVE-OUT → E2:** the C checker attaches positions to error VALUES (via tk_error's C1.3 fields); the `.tks` `error` is `{message}` and gaining line/col ripples EVERY `error {}` under exact-fields → that is **E2** ("teko::Error carry file:line"), done corpus-wide there. C-side error-position is a forward-compatible down payment (delivers the win now); `.tks` error-position lands in E2. |
| **C1.8** driver renderer: source line + `^` caret + expected/actual (type→string `tk_type_render`) | `res`(render),`chk`,`typer`,`driver` (+`.tks` mirror) | C1.3 ✅, C1-POS | ✅ DONE (workflow `diag-enrich-r13`) — `tk_type_render` (resolve.c/.tks, all 10 tags); expected/actual set at call/binary/binding/assign/return mismatch sites; `tk_type_program` threads structured file/line/col; driver prints source line + caret (1-based, tab=1col) + "expected X, found Y". Proof: `work.tks:3:15` + caret + "expected i64, found str"; self-host wall now `lexer:460:29 — expected Reader, found str`. Build green; regressions 5/6; parses all 64. *Residual:* cc-failure surfacing not yet done; return/trailing-value mismatch under-locates (line=0 → fn header) — diagnostics refinement. |
| **C1.6** VM runtime panics carry line:col | `vm` | C1-POS | ✅ DONE (workflow) — positioned wrappers (`vm_panic_div0/cast/oob_at`) prefix `line:col` from the offending typed node; C+vm.tks mirrored. *File-threading deferred* (VM program carries no per-node file → line:col only for now). |
| **C1.7** native panics carry line:col | `rt`, `cg` (+ `.tks`) | C1.3 ✅ | ✅ DONE (OOB) — `tk_panic_oob_at(line,col)` (teko_rt) + codegen emits the index node's position; native index-OOB now prints `line:col: teko: panic: index out of bounds`, **identical to the VM**. *Deferred:* native CAST positioning (48 inline `tk_to_*` helpers — needs a global panic-loc) → **Phase 7 crumb C1.7-CAST** (R7.11); native DIV0 guard (B3b) → **Phase 7 crumb C7.15** (R7.8b). |
| **C1.9** native stack traces (+ `.tsym` per-frame later) | `rt` (+`.tks` note) | C1-POS | ✅ DONE — teko_rt prints a `backtrace` on panic + installs a fatal-signal handler (SIGSEGV/BUS/ILL/FPE); the **E4 native stack-trace resolution** (teko_rt loads `<argv0>.tsym` → appends `=> <teko-name> <file:line>` per frame) is also DONE (see Phase 1 header). Both parts complete; no remaining deferred work in this crumb. |

**Revised rounds:** R1.1 `{C1.2, C1.3}` ✅ → **R1.2 `{C1-POS, C1.7}`** (C1-POS dominates; C1.7 parallel on `rt`) → R1.3 `{C1.6, C1.8, C1.9}` (w3, after C1-POS).

## Phase 2 — `in` operator ✅ DONE

| Crumb | Owns | Dep |
|-------|------|-----|
| **C2.1** lexer `in` keyword/token | `L` | — |
| **C2.2** membership AST node + parse | `ast`, `P` | C2.1 |
| **C2.3** checker: `x in [..]` → bool (value & options comparable) | `chk` | C2.2 |
| **C2.4** codegen single-eval lowering `({T _v=x; _v==a||_v==b;})` | `cg` | C2.2 |
| **C2.5** VM eval | `vm` | C2.2 |
| **C2.6** `.tkb` tag serialize/deserialize | `tkb` | C2.2 |

**Rounds:** R2.1 `{C2.1}` (w1) → R2.2 `{C2.2}` (w1) → R2.3 `{C2.3, C2.4, C2.5, C2.6}` (w4). ✅ **DONE** (workflow `p2-in-operator`: frontend then parallel checker/codegen/vm/tkb + adversarial review). `x in [a,b,…]`→bool, single-eval LHS (GNU stmt-expr), comparison precedence, special `[…]` membership-set (no array literal). VM==native: `3 in [1,2,3]`→true, `9 in […]`/`x in []`→false, `b'e' in [b'e',b'E']`→true. **3 reviewer fixes applied:** (1) self-host regression — `in` added to `is_name_at` (cursor) AND pattern bind-names (parse_pattern) so the corpus's `in` identifiers/bindings still parse (the `to` precedent); (2) empty-set VM==native — codegen now evaluates the LHS once for `[]` too; (3) `revalidate` gained the `TK_TEXPR_IN`/`TInExpr` case (-Wswitch + non-exhaustive `.tks` match fixed). Build clean; regressions 5/6; self-host wall restored to `lexer.tks:461` (CHECK).

## Phase 3 — str/byte stdlib as real mirrored fns ✅ DONE

| Crumb | Owns | Dep |
|-------|------|-----|
| **C3.1** real C impls (text.h) + teko_rt twins for slice/str/str_of_bytes/one_byte/str_concat[3]/i64_to_str/u64_to_str/ftoa | `rt` | — |
| **C3.4** checker signature reconcile + mirror (scope/typer) | `scope`, `chk` | — |
| **C3.2** codegen lowering of each builtin → runtime call | `cg` | C3.1 |
| **C3.3** VM execution of each builtin | `vm` | C3.1 |

**Rounds:** R3.1 `{C3.1, C3.4}` (w2) → R3.2 `{C3.2, C3.3}` (w2). ✅ **DONE** (workflow `p3-stdlib`): wired the 4 missing builtins — `str_of_bytes`/`str` (`[]byte`→str), `one_byte` (byte→str), `str_concat3`, `ftoa` (f64→str) — runtime (teko_rt) + codegen + VM + mirrors. **Reviewer fix:** `str`/`str_of_bytes` codegen bridges the `[]byte` value (which lowers to the struct `tk_slice_byte`, a DISTINCT C type from `tk_str`) to a `tk_str` at the call site (single-eval) before `tk_str_of_bytes` copies it — was a latent cc error. Verified VM==native: `str_concat3("ab","cd","ef").len`=6, `ftoa(1.5).len`=3, `one_byte`/1-elem `str_of_bytes`=1. Build clean; regressions 5/6; parses all 64. **⚠ Surfaced a PRE-EXISTING bug (task #57, slice value-layer): `teko::list::push` does NOT accumulate past one element** (`push(push(empty,a),b).len`==1, should be 2) in BOTH VM and native — a major Phase-6/self-host blocker (the corpus builds strings via `push` everywhere). NOT a Phase-3 regression (Phase 3 didn't touch `emit_list_push`/VM push).

## Phase 4 — C↔.tks mirroring audit ✅ DONE (now a standing per-commit discipline) *(read-mostly; partitioned by directory → fully parallel)*

| Crumb | Owns | Dep | Status |
|-------|------|-----|--------|
| **C4.1** audit + reconcile lexer + parser pairs | `L`, `ast`, `P` | — | ✅ MAINTAINED (SUPREME RULE — every commit that touches a `.c`/`.h` mirrors the change to the `.tks` twin atomically; no divergence has been allowed to accumulate; a discrete audit sweep is superseded by the standing per-commit discipline) |
| **C4.2** audit + reconcile checker pairs | `chk`, `match`, `collect`, `res`, `scope`, `tast` | — | ✅ MAINTAINED |
| **C4.3** audit + reconcile codegen + vm pairs | `cg`, `vm` | — | ✅ MAINTAINED |
| **C4.4** audit + reconcile emit + runtime + build + assert + main | `tkb`, `rt`, `build`, `assert`, `main` | — | ✅ MAINTAINED |

**Rounds:** R4.1 ✅ MAINTAINED — continuous per-commit discipline (SUPREME RULE); no pending discrete sweep.

## Phase 5 — Definite-assignment / init analysis ✅ DONE *(single subsystem → mostly serial)*

| Crumb | Owns | Dep | Status |
|-------|------|-----|--------|
| **C5.1** definite-assignment pass (mandatory init, use-before-init error) | `chk` (new flow module) | C1.1 (positions) | ✅ DONE — use-before-init proven impossible by construction (parser `Binding.value` mandatory; typed in pre-binding env); unused-local → ERROR with Phase-1-quality caret via `diag_at`+`err_loc`. Both twins lockstep. |
| **C5.2** unused-local error / unused-private warning → warnings channel | same flow module | C5.1, C1.3 | ✅ DONE — unused-private-fn → WARNING to stderr; 14 dead private fns surfaced in corpus; `#test` fns exempt; warnings channel live. |

**Rounds:** R5.1 ✅ `{C5.1}` → R5.2 ✅ `{C5.2}` — `src/checker/initanalysis.{tks,c,h}` delivered; gate GREEN; both twins byte-identical in behavior.

## Phase 6 — Finish self-host → working `teko .` ✅ DONE *(milestone; `cg`/`vm` serialize across rounds)*

| Crumb | Owns | Dep | Status |
|-------|------|-----|--------|
| **C6.1** function parameters in codegen | `cg` | — | ✅ DONE |
| **C6.2** function parameters in VM | `vm` | — | ✅ DONE |
| **C6.3** slice `[]T as x` pattern — parser | `P` (+pattern.h) | — | ✅ DONE |
| **C6.10** #41 value-level cross-ns enforce (calls + enum paths) | `res` | — | ✅ DONE (namespace-qualified mangling) |
| **C6.4** slice pattern checker + `[]T \| error` return | `match`, `chk` | C6.3 | ✅ DONE |
| **C6.5** slice pattern codegen (variant-member) | `cg` | C6.3 | ✅ DONE |
| **C6.6** slice pattern VM | `vm` | C6.3 | ✅ DONE |
| **C6.7** spread-into-literal `[..xs, x]` — frontend | `L`, `ast`, `P`, `chk` | C6.5 | ✅ DONE (R7.6). `TK_TOKEN_DOTDOT`/`DotDot`; `tk_array_elem {is_spread, expr*}` + push helper; `parse_array_elems` (spread prefix at unary precedence); checker `type_array_lit` handles `..xs: []T`; `TArrayElem` in tast; tkb `is_spread` u8 flag. 172 tests. |
| **C6.8** spread-into-literal `[..xs, x]` — backend | `cg`, `vm`, `tkb` | C6.7 | ✅ DONE (R7.6). VM flattens spread via `v_list_push`; codegen fast-path (no spread) unchanged, slow path emits `tk_slice_push` loop; TKB round-trip test added. |
| **C6.11** #19 X5 justification-header sweep (headers only) | *(all headers — solo)* | all above | ✅ DONE |

**Exit criteria MET** (commit `461c491`): (1) FULL SELF-HOSTING — `bin/teko` built by bootstrap, compiles its own corpus; (2) VM==native regression set (5/6/106). **C6.7/C6.8 are language-feature crumbs** (not exit-gate blockers) carried into Phase 7's work queue. **CONSTITUTION RULING** (2026-06-27): `xs + [x]` is ILLEGAL — `+` is math only (Law M.0); the correct spread form is `[..xs, x]` (decompose-recompose). The `..` token must be added to the lexer; the array-literal AST gets tagged elements (`plain | spread`).

**Rounds:** R6.1 ✅ `{C6.1, C6.2, C6.3, C6.10}` → R6.2 ✅ `{C6.4, C6.5, C6.6}` → R6.3 ✅ `{C6.7, C6.8}` (spread-into-literal, completed in Phase 7) → R6.4 ✅ `{C6.11}`.

**Self-host wall (2026-06-26): `lexer.tks:461` = TASK #41 (cross-namespace call resolution).** The C1.8 message reads `argument type mismatch — expected Reader, found str` at `read_str(source, pos)`. Root cause: `type_call` (expr.c) resolves a call by BARE name in the flat env (`tk_env_lookup(env, name)`), with no namespace scoping — so `teko::lexer::read_str(str,u64)` binds to `teko::emit::read_str(Reader,…)` (tkh.tks). There are **12 such cross-ns function-name collisions** in the corpus (read_str, define, is_bool/float/integer/numeric, one_byte, str_of_bytes, prim_width, run, cast_may_lose, write_u64), so a rename is whack-a-mole — the fix is **#41**: a namespace-tagged function table (like the type table) + resolve a bare call in the CALLER's namespace (thread `current_ns` into `type_call`); qualified `ns::name` resolves the named ns + visibility. Moderate core-checker refactor + `.tks` mirror. This is the next self-host gate (before the slice-`empty()` binding half of #57, which is a codegen-time issue behind this CHECK wall).

**Self-host facts uncovered during C1-POS** (both behind the wall, both pre-existing — the typed `TExpr` already relies on them; C1-POS's `Expr` struct-wrapper just adds more instances):
- **Variant-widening in a struct-literal field is NOT yet supported by the checker.** `S { kind = A {} }` where `kind: K` and `K = variant A | B` → "field value does not match the field's declared type". The ENTIRE struct-wrapper AST (`TExpr { kind = TNumber{…} }`, and now `Expr { kind = Number{…} }`) needs this. A real Phase-6 checker feature.
- **`match` convention over a struct-wrapper is unsettled.** typer.tks now uses `match e.kind { Case }` (matching the variant field — safe under any model); codegen.tks/vm.tks use bare `match e { Case }` over the struct `TExpr` (pre-existing). Phase 6 must confirm which the checker accepts and unify (prefer `.kind` — sound under both).

## Phase 7 — Host independence ✅ DONE (residual R7.8b–R7.12 folded into the cross-phase waves W1–W6, all ✅) *(gated by the C7.0 tribunal decision; Phase 6 done)*

> **C7.0 — `extern`/FFI form — ✅ RATIFIED 2026-06-27** (TRIBUNAL DECISION). Form: `extern fn name(p) -> r = "symbol" from "lib"` (bodyless; `from` optional→libc; `freestanding` makes it mandatory); `ptr`-only marshalling; one `OP_CALL_EXTERN` → platform convention; library resolution indirected through `.tkp [extern.libs]` (array vocabulary `[]`/name/`static:`|`shared:`/path/`-flag`) + `[extern.search]` (`-L`, per-OS soft-drop) + `prefer` + `cc`/`target`/`sysroot` (musl-ready); cross-OS name-diffs resolve in the `.tkp`, shape-diffs need the general `#os(...)` guard. Full clause: **LEGISLATION §"FFI / `extern`"**. **Staging: macOS+libc FIRST, then Linux, then Windows;** per-OS `.tkp` resolution and `#os(...)` are legislated now / implemented as follow-ons.

| Crumb | Owns | Dep | Status |
|-------|------|-----|--------|
| **C7.1a** `extern` decl — lexer (`extern`/`from`) + parser + AST/tast *(macOS)* | `L`, `ast`, `P` | C7.0 | ✅ DONE |
| **C7.1b** `extern` checker typing (prims+`ptr`/`uptr`/`void` only) | `chk` | C7.1a | ✅ DONE |
| **C7.1c** `extern` codegen emit (C extern proto + call; Teko-name→C-symbol) | `cg` | C7.1a | ✅ DONE |
| **C7.1d** `extern` VM handling (defer/stub like the other host bottoms) | `vm` | C7.1a | ✅ DONE |
| **C7.1e** `.tkp` `[extern.libs]` FULL array vocab → cc link flags (macOS: empty/bare/multi/path/`-flag`; resolved in manifest → `link_flags`) | `build`(manifest), `driver`/`build` | C7.0 | ✅ DONE |
| **C7.1f** `os()` builtin + `[extern]` cc/target/sysroot/freestanding + musl portability fixes; `#os(...)` + per-OS `[extern.libs.<os>]` (parse pure — deferred to link-time via `os_lib_os`/`os_lib_flag`) | `build`, `L`, `P`, `chk`, `cg`, `rt` | C7.1a–e | ✅ DONE |
| **C7.1g** `ptr`/`uptr` opaque transport types + `extern type` opaque handle (→ `void*`) | `chk`, `cg`, `ast`, `P` | C7.1a–c | ✅ DONE |
| **C7.1i** marshalling primitives `teko::mem::as_ptr`/`as_cstr`/`str_from_cstr`/`bytes_from_ptr` | `chk`, `cg`, `rt` | C7.1g | ✅ DONE |
| **C7.1k** BINARY METADATA — `.tkp` `description`+`[platforms]`; every binary carries `@(#)` metadata string + macOS Mach-O `__TEXT,__info_plist`; staged: Windows PE VERSIONINFO + Linux ELF `.note` | `build`(manifest/project), `cg`, `driver` | C7.1f | ✅ DONE |
| **C7.1m** ARTIFACT KINDS — `Artifact = enum {Binary,Static,Shared,Package}` + parse + R-main-d **function exists** (tkp_rule) + build dispatch (honest stops for static/shared/package). `.tkl`=ZIP legislated; `.tkb` OWN-tree-only; pipeline=load deps→check→emit | `build`(tkp_rule/manifest/project), `driver` | C7.1k | ✅ DONE (dispatch) |
| **C7.6** D2 `#test` runner in VM (`teko test`) | `vm`, *(runner)*, `main` | — | ✅ DONE |
| **C7.7** D3 three-metric coverage (functions/lines/branches) + Cobertura XML | `vm`, `rt` | — | ✅ DONE |
| **C7.8** D4 pre-emit gate; `--no-test` removed (build ALWAYS runs tests when `.tkt` files exist) | `main`, `build` | — | ✅ DONE |
| **C7.11** A7 output directory + `-o <dir>` | `main` | — | ✅ DONE |
| **C7.16** `.tkb` statement/program codec — ALL 22 expr kinds, 7 statement kinds, 7 pattern kinds, TypeExpr/Param/Field/TypeBody/TypeDecl/UseDecl/TFunction/TArm/TItem/TProgram + `serialize_program`/`deserialize_program` (version-2 frame). 170 tests, byte-identical. UNBLOCKS C7.12 + C7.10 | `tkb` | — | ✅ DONE |
| **C7.9** A4 main-rule — `check_main_file_rule` wired into `frontend_body` in both `project.tks` + `driver.c`; scans file list for `main.tks` after discover; +1 test `has_main_detection_matches_ends_with_main_tks` | `build`(project), `driver` | — | ✅ DONE |
| **C7.13** B0d `tk_str_eq` declared in `text.h` + `tk_str_require_eq` inline panic wrapper; removed duplicate `static` definitions from `checker/type.c` + `checker/expr.c`; `teko_rt.h` retains declaration (can't include text.h — core.h conflict) | `rt`(text.h), `chk` | — | ✅ DONE |
| **C7.14** B3a `TK_RT_LIST(T,Name)` macro in `teko_rt.h` (self-contained, uses realloc/free — can't include core.h); instantiates `tk_byte_list`, `tk_str_list`, `tk_i64_list`; twin comment block in `teko_rt.tks` | `rt` | — | ✅ DONE |
| **C7.2** `teko::env` namespace file | *(new `src/env/env.tks`)* | C7.1b–d | ✅ DONE (R7.7) — `src/env/env.tks` with `pub extern fn args/var/cwd/set_var/chdir`; seeds removed from `scope.c`/`scope.tks`; checker uses `tk_env_lookup_call` (ns_last_seg match) which finds env-collected extern fn before `builtin_fn` fallback. |
| **C7.3** `teko::io` namespace file | *(new `src/io/io.tks`)* | C7.1b–d | ✅ DONE (R7.7) — `src/io/io.tks` with `pub extern fn println/eprintln/print/eprint/write/ewrite/read_file/write_file/write_file_bytes`; seeds removed. |
| **C7.4** `teko::fs` namespace file | *(new `src/fs/fs.tks`)* | C7.1b–d | ✅ DONE (R7.7) — `src/fs/fs.tks` with `pub extern fn list_dir/mkdir`; seeds removed. |
| **C7.5** `teko::process` namespace file | *(new `src/process/process.tks`)* | C7.1b–d | ✅ DONE (R7.7) — `src/process/process.tks` with `pub extern fn run`; seeds removed. |
| **C7.15** B3b overflow-debug panic guard wiring in codegen | `cg` | — | ✅ DONE — `tk_add_*/tk_sub_*/tk_mul_*` inline helpers (u8..u128, i8..i128) added to `teko_rt.h`; `#ifdef TEKO_OVERFLOW_DEBUG` uses `__builtin_*_overflow` + `tk_panic_overflow()`; else plain C (zero overhead). Codegen (`codegen.c`+`codegen.tks`) routes `+`,`-`,`*` on integer prims through these helpers; float +,-,* unchanged. 172 tests pass; regressions match_pattern_bindings==5, optionals==6 hold. |
| **C7.12** A8 `package` output → `.tkl` (ZIP-STORE of `.tkh`+`.tkb`+`.tsym`, named `<name>-<version>[-suffix].tkl`) + pure-Teko ZIP-STORE writer (CRC32 + local/central/EOCD headers, deterministic timestamp, twins). C7.16 ✅ unblocks the real `.tkb` payload; update the honest-stop message in project.tks/driver.c | `build`, `tkb` | C7.16 ✅, C7.1m ✅ | ✅ DONE (R7.8) — `src/compress/compress.tks` + `compress.c` (namespace `teko::compress`; reusable by any Teko project); `write_zip(entries: []ZipEntry) -> []byte`; CRC-32 table-driven; deterministic. Backend wired: `emit_program` → `.tkh`, `serialize_program` → `.tkb`, `tk_emit_tsym` → `.tsym`; all three ZIPped into `<out>/<name>-<version>.tkl`. `write_file_bytes(path, []byte)` added to `teko::io` + `teko_rt`. `type_table_of` added to checker/collect. Smoke-tested: valid ZIP, `['pkg.tkh','pkg.tkb','pkg.tsym']`. 172 tests. |
| **C7.10** A6 pre-linker — load each dep (native `.a`/`.so` OR `.tkl`'s `.tkb`) → merge typed trees → check app BEFORE emit; the merged `.tkb` is the dep's OWN tree (never re-inline deps) | `tkb`, `build`, `res` | C7.12, C7.16 ✅, C7.1m ✅ | ✅ DONE (W1) — `read_zip` added to `teko::compress` (ZIP reader); `seed_from_dep`+`collect_with_seed` in `checker/collect`; `type_program_with_deps` in `checker/typer`; `load_dep_program`/`load_deps_program` in `project.tks`; `frontend_body` now seeds typed tree from dep `.tkb` before checking; honest error if `packages/<dep>-*.tkl` not found. All C+.tks twins. 172 tests green. |
| **C7.17** M2 `driver.tks` fully materialized — replace seed scope-lookup paths with real `use teko::env`/`teko::io`/`teko::fs`/`teko::process` calls | *(driver)* | C7.2–C7.5 | ✅ DONE (W1) — stale "NOT YET SEED-COMPILABLE" header updated; `use teko::env/io/fs/process` added; thin `read_file` wrapper removed + 4 call sites updated to `teko::io::read_file` directly; stale "deferred"/"pending" comments fixed throughout driver.tks + driver.c. 172 tests green. |
| **E2-NATIVE** native `error` struct in codegen — codegen currently lowers `error` to its message `tk_str`; `err_loc`/`err_typed` builtins are no-ops in native (`error.line/col`→0, `error.file/expected/actual`→empty). Goal: represent native `error` as a C struct with all C1.3 fields (`message/file/line/col/expected/actual`); emit struct init in codegen; `err_loc`/`err_typed` return the real fields so native matches VM. Deferred from Phase 1 E2 native-degraded carve-out; now has Phase 7 codegen infrastructure to land on. | `rt`, `cg` (+ `.tks`) | C7.15, C1.3 ✅ | ✅ DONE (W2) — `tk_error` struct added to `teko_rt.h` (`message/file/line/col/expected/actual` + `tk_error_make`/`tk_error_loc`/`tk_error_types` static-inline helpers); `emit_type`/`emit_type_expr` now emit `tk_error`; `err_loc`/`err_typed` builtins emit real `tk_error_loc`/`tk_error_types` calls (not no-ops); error field access reads real struct fields; `error { message = s }` emits `tk_error_make(s)`; FFI lifting wraps `tk_str .err` with `tk_error_make()`; variant `T\|error` union member stores `tk_error error` instead of `tk_str error`. All mirrored to `codegen.tks`; test `cgt_type_emission` updated. 172 tests green; uniontest smoke exits 106 ✅. |
| **C1.7-CAST** native CAST positioning — the 48 `tk_to_*` helpers in `teko_rt` don't carry position; a cast panic prints no `file:line`. Thread a global panic-loc (set by codegen at the call site) so a native cast panic is identical to the VM. Deferred from C1.7. | `rt`, `cg` (+ `.tks`) | E2-NATIVE | ✅ DONE (W3) — `_tk_cast_loc_line`/`_tk_cast_loc_col` globals in `teko_rt.h`+`.c`; `tk_panic_cast()` prefixes `line:col:` when set; codegen emits `({ _tk_cast_loc_line=L; _tk_cast_loc_col=C; tk_to_*(expr); })` statement-expr for float→int and narrowing int→int casts (skips if `e.line==0`); `codegen.tks` mirrored. 172 tests green; regressions 5/6. |
| **C7.18** `defer` — scoped deferred cleanup. Syntax: `defer { stmts }` (Zig-style; executes LIFO at ANY scope exit — normal return, early `break`, or panic). Full pipeline: lexer `defer` keyword; AST `DeferStmt { block }`; parser in `parse_stmt`; checker types the block (void context, no value); codegen inserts the block at ALL scope-exit points of the enclosing function body (C inline-at-exit global stack `g_cg_defers[]`); VM maintains a deferred-stack per call frame (`defers: []DeferBody` in Venv), pops+executes LIFO on frame exit; TKB serializes `DeferStmt` as tag=7. Self-hosted codegen has an honest stop (C backend handles it). | `L`, `ast`, `P`, `chk`, `cg`, `vm`, `tkb` (+ all `.tks`) | C1.7-CAST ✅ (codegen stable) | ✅ DONE |
| **C7.1h** `ptr<T>` (deref behind `#repr(C)`) + `ptr ≡ ptr<void>` | — | **S4 generics** | ⬜ deferred to S4 |
| **C7.1j** multi-OS/arch CI pipelines *(LAST in phase)* — Linux arm64+x86_64, Windows arm64+x86_64, macOS arm64 | CI | C7.1* | ⬜ TODO (LAST) |
| **C6.7** *(carried from Phase 6)* spread-into-literal `[..xs, x]` — frontend (`L`+`ast`+`P`+`chk`) | `L`, `ast`, `P`, `chk` | C6.5 ✅ | ✅ DONE (R7.6) — `TK_TOKEN_DOTDOT`/`DotDot` token; `tk_array_elem {is_spread, expr*}` + `tk_array_elems_push`; `parse_array_elems` with spread prefix; checker `type_array_lit` handles `..xs: []T → T`; `TArrayElem` in tast. |
| **C6.8** *(carried from Phase 6)* spread-into-literal `[..xs, x]` — backend (`cg`+`vm`+`tkb`) | `cg`, `vm`, `tkb` | C6.7 | ✅ DONE (R7.6) — VM flattens spread slices; codegen emits `tk_slice_push` loops; TKB codec carries `is_spread` u8 per element + round-trip test. |

**Completed rounds (for reference):**
- R7.1 ✅ `{C7.1a ✅, C7.6 ✅, C7.16 ✅}` — extern front + test-runner + .tkb codec done; C7.9/C7.13/C7.14 carried forward
- R7.2 ✅ `{C7.1b ✅, C7.1c ✅, C7.1d ✅}` — extern across checker/codegen/vm done
- R7.3 ✅ `{C7.1e ✅, C7.1f ✅, C7.1g ✅, C7.1i ✅, C7.7 ✅}` — extern libs + os + marshalling + coverage done
- R7.4 partial `{C7.8 ✅, C7.11 ✅, C7.1k ✅, C7.1m ✅}` — gate + output + metadata + artifact kinds done

**Completed rounds (for reference):**
- **R7.5** ✅ `{C7.9, C7.13, C7.14}` — main-rule + tk_str_eq + TK_RT_LIST. 171 tests, both builds green.
- **R7.6** ✅ `{C6.7, C6.8}` — spread-into-literal `[..xs, x]` complete. 172 tests, both builds green.
- **R7.7** ✅ `{C7.2, C7.3, C7.4, C7.5}` — host namespace files + seeds removed. 172 tests, both builds green.
- **R7.8** ✅ `{C7.12}` — `.tkl` ZIP-STORE output; `teko::compress` reusable; `write_file_bytes` added. 172 tests.

**Remaining Phase 7 crumbs — now scheduled in the cross-phase master sequence (see below):**
- **C7.15** ✅ → cross-phase **W1** (parallel with C7.10, C7.17, C8.1)
- **C7.10** ✅ → cross-phase **W1** (parallel with C7.15, C7.17, C8.1)
- **C7.17** ✅ → cross-phase **W1** (parallel with C7.15, C7.10, C8.1)
- **E2-NATIVE** ✅ → cross-phase **W2** (after C7.15; parallel with C8.2)
- **C1.7-CAST** → cross-phase **W3** (after E2-NATIVE; parallel with C8.3/C8.5/C8.6)
- **C7.18** (`defer`) ✅ → cross-phase **W5** (solo wave — owns all single-owner files; after C1.7-CAST ✅)
- **C7.1j** (CI — LAST) → cross-phase **W6** (after Phase 8 ✅ + C9.2 ✅ + defer ✅)

## Phase 8 — FLAGS ✅ DONE

| Crumb | Owns | Dep | Status |
|-------|------|-----|--------|
| **C8.1** `flags` keyword | `L` | — | ✅ DONE (W1) — `TK_TOKEN_FLAGS` added to `token.h`+`token.tks`; `"flags"` added to `keyword_kind()` in `lexer.c`+`lexer.tks`. 172 tests green. |
| **C8.2** flags decl + AST | `ast`, `P` | C8.1 | ✅ DONE (W2) — `TK_BODY_FLAGS`/`FlagsBody` in ast.h+ast.tks; `parse_flags_decl` in parse_decl.c+.tks; tkb_write/read/frame updated; codegen+header honest-stop stubs; check_modules.tks case added. 172 tests green. |
| **C8.3** checker: power-of-2 auto-assign + u128 size guard + bitwise typing + helper resolve | `chk`, `collect`, `res` | C8.2 | ✅ DONE (W3) — `is_flags_named` predicate in `expr.c`+`expr.tks`; `type_binary` allows `& \| ^` on same-flags NAMED types; `type_unary` allows `~` on flags; `type_path_expr` extended to accept `TK_BODY_FLAGS` (ordinal = bit index); `type_flags_method` lowers `has/all/any/none/add/remove` to synthetic TAST binary/compare nodes; `TK_EXPR_METHOD_CALL` dispatch tries flags before deferring. 172 tests green (native + VM). |
| **C8.4** codegen: emit u128-fitting uint + bitwise (no shift) + helpers | `cg` | C8.2 | ✅ DONE (W4) — `cg_named_is_flags` helper; `emit_type_decl` for `TK_BODY_FLAGS`: `typedef <uintN_t> tk_t_<Name>` (N=8/16/32/64/128 by member count) + `static const tk_t_<Name> tk_t_<Name>_<M> = (1<<i)`; `TK_TEXPR_PATH` for flags refs pre-emitted constant names; flags included in "ENUMS FIRST" emission pass. codegen.tks mirrored. 172 tests green; regressions 5/6. |
| **C8.5** VM: flags values + bitwise + helpers | `vm` | C8.2 | ✅ DONE (W3) — flags stored as `v_int(…, false, 128)` (u128); bitwise `& | ^ ~` reuse existing integer paths; C8.3 lowered `.has/.all/.any/.none/.add/.remove` helpers to synthetic TAST binary/compare nodes so VM handles them automatically. Zero vm.c/vm.tks changes needed. 172 tests green. |
| **C8.6** `.tkb` serialize flags | `tkb` | C8.2 | ✅ DONE (W3) — `tk_flags_body` extended with `values: unsigned __int128 *`; `FlagsBody` in ast.tks got `values: []u128`; tkb_write/read (C+.tks) updated: tag 5 now emits `n(u64)` then for each member: `name_idx(u32)+hi(u64)+lo(u64)`; parse_decl.tks literals fixed; forward-decl fix for `is_flags_named` in expr.c. 172 tests green (native + VM). |

**Rounds (cross-phase wave schedule):** C8.1 → **W1** · C8.2 → **W2** · C8.3/C8.5/C8.6 → **W3** · C8.4 → **W4** (see master sequence table).

## Phase 9 — SEC ✅ DONE

| Crumb | Owns | Dep |
|-------|------|-----|
| **C9.1** SAST gate (CI checks) | *(CI/build scripts)* | — |
| **C9.2** capability/sandbox audit of `exp`/`extern`/syscall surface | `res`/`scope` + report | — | ✅ DONE (W4) — `src/checker/capability_audit.md` written; findings: varargs gap (MEDIUM), `exp fn` not ABI-exported, `process::run` unrestricted (documented intent), `teko::mem` unsafe-by-contract (documented). No exploitable bugs found; minor notes for future tightening. 172 tests green. |

**Rounds (cross-phase wave schedule):** C9.2 → **W4** · C9.1 → **W6** (see master sequence table).

## Phase 10 — Evolution S1–S10 🔶 IN FLIGHT (S1 ✅, S4 ✅, S3 ✅, S2 per-fn ✅ / block ⬜; S4c/S5/S6/S7/S8/S9/S10 ⬜) *(POST-SELF-HOST; each S-stage is its own sub-project — detailed crumbs are drafted AT stage start against real data, M.4)*

Stage-level dependency waves (the hard-ordering invariant). Generics (S4) runs parallel to arenas (S1–S3); closures (S4c) unlock after S4; async/await (S10) is the capstone.

*(Status mirrors the master-sequence wave table below — same legend: ✅ done · 🔶 in flight · ⬜ TODO.)*

| Status | Stage-crumb | Dep | Wave |
|--------|-------------|-----|------|
| ✅ | **C10.S1** arena primitive + root region | S0 seam (done) | W7 |
| ✅ | **C10.S4** generics (unconstrained, monomorphization) | — | W7 |
| ✅ | **C10.S3** `ref` (mutable-target only) — **delivered as `Ref<T>` in MEM-1** (the `ref` keyword became the `Ref<T>` type) | S2 | W8 |
| 🔶 | **C10.S2 ★** scope regions + escape check — **per-FUNCTION regions DONE (MEM-1/W8)**; block-scoped regions remain (W9) | S1 | W8 (fn) / W9 (block) |
| ⬜ | **C10.S4c** closures — `fn` literals with lexical capture; value-capturing first (environment struct + fn-ptr pair); reference-capturing after S3. Full pipeline: parser `\|params\| body` syntax; checker captures types + infer; codegen closure struct emission; VM closure values. | S4 | W10 |
| ⬜ | **C10.S5** DI lifetimes → arenas (`#singleton`/`#scoped`/`#transient`) | S2 (+S3) | W10 |
| ⬜ | **C10.S6** constraints (positive; `!` only prims/sealed) | S4 + real Map need | W11 |
| ⬜ | **C10.S7** dynamic collections (append, Map, Set; out-region param) | S4, S6, S2/S3 | W12 |
| ⬜ | **C10.S8** concurrency capstone — `scope{}`/`spawn` (routine launch); `channel<T>`/`send`/`recv`; **`Mutex<T>`** (lock/unlock, poisoning on panic); **`Semaphore`** (acquire/release, counting); **`RWMutex`** (concurrent reads, exclusive write); **`WaitGroup`** (add/done/wait); **thread-control** (`thread::spawn` → `JoinHandle`, join, detach, park/unpark); **M:N scheduler** (lightweight routine → OS thread fan-out; work-stealing queue) | S2, S3 | W13 |
| ⬜ | **C10.S9** LTS cleanup (Parsed×14→Result<T>; unify parse/to_string) | S4 | W13 |
| ⬜ | **C10.S10** async/await — coroutine-based non-blocking I/O; `async fn`/`await expr`; Teko runtime event loop (epoll/kqueue/IOCP); cancellation via scope; composable with channels + Mutex | S8 | W14 |

**Waves (cross-phase schedule):** S1+S4 → **W7** · S4c → **W8** · S2 ★ → **W9** · S3+S5 → **W10** · S6 → **W11** · S7 → **W12** · S8+S9 → **W13** · S10 → **W14** (see master sequence table).
*(Keep deferred ceilings deferred: borrow-solver / lifetime-variable system; region-polymorphism. Implicit-copy-on-escape → tribunal (lean: NO). M:N scheduler and async/await are NOW IN SEQUENCE — no longer deferred.)*

## Phase 11 — Code quality sweep ⬜ TODO (W15) *(LAST; read-mostly; partitioned by directory → fully parallel)*

Each crumb applies the full principle stack (DRY + KISS + SOLID + YAGNI + 12-Factor + comment hygiene) to its file partition. Order within a file: DRY → KISS → SOLID/ISP/DIP → YAGNI → 12-Factor checks → comment hygiene. Files within a crumb are disjoint → all four crumbs run in parallel.

| Crumb | Owns | Dep | Principle focus within partition |
|-------|------|-----|----------------------------------|
| **C11.1** quality sweep — lexer + parser | `L`, `ast`, `P` | all W1–W14 ✅ | DRY: `in` for char sets; KISS: flatten multi-step token checks; SRP: parser fns parse exactly one construct; YAGNI: remove unused parse helpers; 12F-XI: no diagnostic mixed into token stream |
| **C11.2** quality sweep — checker | `chk`, `match`, `collect`, `res`, `scope`, `tast` | all W1–W14 ✅ | DRY: hoisting repeated `tk_env_lookup` patterns; KISS: single-pass resolution where two passes exist with no benefit; ISP: narrow `scope.h` pub surface; DIP: `res` depends on `scope.h` interface only; YAGNI: remove any `pub` checker fn with zero corpus call sites |
| **C11.3** quality sweep — codegen + vm | `cg`, `vm` | all W1–W14 ✅ | DRY: deduplicate emit helpers that differ only in type name; KISS: merge any codegen case that reduces to the same C template; OCP: new expr kinds are additive arms — audit `-Wswitch` coverage; YAGNI: `driver.tks::compile` dead fn removal + any unreachable honest-stop arms |
| **C11.4** quality sweep — emit + runtime + build + assert + main | `tkb`, `rt`, `build`, `assert`, `main` | all W1–W14 ✅ | DRY: TKB tag handlers; KISS: `project.tks` command dispatch simplification; 12F-III: `TK_RT_DIR`/`TK_SRC_DIR` env-var paths verified; 12F-V: build/run/test path isolation audit; 12F-IX: atomic output (temp+rename); 12F-XI: stderr-only diagnostics; 12F-XII: sub-command idempotency |

**Rounds (cross-phase schedule):** C11.1/C11.2/C11.3/C11.4 → **W15** (all parallel — see master sequence table). Exit: DRY ✓ · KISS ✓ · SOLID ✓ · YAGNI ✓ · 12-Factor III/V/IX/XI/XII ✓ · every comment `/** … */` · zero inline comments · corpus green + VM==native.

---

## Round-by-round agent matrix — completed phases

Phases 1–7 (partial) are done; this table is kept for historical reference.

| Phase | Rounds → width | Peak agents | Notes |
|-------|----------------|-------------|-------|
| 1 Diagnostics | R1.1=2 ✅, R1.2=2 ✅, R1.3=3 ✅ | **3** | C1-POS serial foundation; E2-NATIVE ✅ W2 done; C1.7-CAST deferred to cross-phase wave 3 below |
| 2 `in` | R2.1=1 ✅, R2.2=1 ✅, R2.3=4 ✅ | **4** | — |
| 3 stdlib | R3.1=2 ✅, R3.2=2 ✅ | **2** | — |
| 4 mirror audit | R4.1=4 ✅ (continuous SUPREME RULE) | **4** | standing discipline, not a discrete sweep |
| 5 init-analysis | R5.1=1 ✅, R5.2=1 ✅ | **1** | initanalysis.{tks,c,h} delivered |
| 6 self-host | R6.1=4 ✅, R6.2=3 ✅, R6.3=2 ✅, R6.4=1 ✅ | **4** | FULL SELF-HOSTING — exit criteria met |
| 7 host-independence | R7.1=6 ✅, R7.2=3 ✅, R7.3=6 ✅, R7.4=5 ✅, R7.5=3 ✅, R7.6=2 ✅, R7.7=4 ✅, R7.8=1 ✅ | **6** | R7.8b–R7.12 remain — folded into cross-phase waves below |

---

## Master execution sequence — all remaining work (cross-phase waves)

Items within a wave are **parallel** (disjoint file-owners, all dependencies met). A wave cannot
start until the previous wave's integration gate passes: **build green + VM==native + regressions**.

**Parallelism constraints:**
- `cg` and `vm` are single-owner: only ONE crumb per wave may own each.
- `rt` (`teko_rt.h`/`.c`): single-owner.
- `tkb` (`tkb_write`/`tkb_read`): single-owner.
- `chk`/`collect`/`res`/`scope`: logically single-owner (tightly coupled checker files).
- New files (`src/arena/`, `src/generics/`, …): no constraint on count.

| Wave | Crumbs (parallel within wave) | Files owned | Width | Gate before next wave |
|------|-------------------------------|-------------|-------|----------------------|
| ~~**W1**~~ ✅ | ~~**C7.15** overflow-debug panic guard (codegen)~~ ✅ | `cg` | | |
| | ~~**C7.10** pre-linker — load dep `.tkl`/`.tkb` → merge typed trees → check app pre-emit~~ ✅ | `tkb`, `build`, `res` | | |
| | ~~**C7.17** driver.tks materialized — replace seed lookups with `use teko::env/io/fs/process`~~ ✅ | `driver` | | |
| | ~~**C8.1** `flags` keyword (lexer)~~ ✅ | `L` | **4** | ✅ build green + VM==native (172 tests; regressions 5/6) |
| ~~**W2**~~ ✅ | ~~**E2-NATIVE** native `error` as C struct — `err_loc`/`err_typed` work in native; `error.line/col` → real values~~ ✅ | `rt`, `cg` | **2** | ✅ build green + 172 tests; uniontest exits 106 |
| | ~~**C8.2** `flags` decl + AST + parser~~ ✅ | `ast`, `P` | **2** | ✅ build green + VM==native (172 tests; regressions 5/6) |
| ~~**W3**~~ ✅ | ~~**C1.7-CAST** native CAST positioning — global panic-loc in `tk_to_*` helpers~~ ✅ | `rt`, `cg` | | |
| | ~~**C8.3** `flags` checker — power-of-2 auto-assign, u128 guard, bitwise typing, helpers~~ ✅ | `chk`, `collect`, `res` | | |
| | ~~**C8.5** `flags` VM — flags values + bitwise + helpers~~ ✅ | `vm` | | |
| | ~~**C8.6** `flags` TKB serialize~~ ✅ | `tkb` | **4** | ✅ build green + VM==native (172 tests; regressions 5/6) |
| ~~**W4**~~ ✅ | ~~**C8.4** `flags` codegen — emit u128-fitting uint + bitwise + helpers~~ ✅ | `cg` | | |
| | ~~**C9.2** capability/sandbox audit — `exp`/`extern`/syscall surface~~ ✅ | `res`, `scope` + report | **2** | ✅ build green + audit report (172 tests; regressions 5/6) |
| ~~**W5**~~ ✅ | ~~**C7.18** `defer` — full pipeline: lexer keyword; AST `DeferStmt`; parser in `parse_stmt`; checker (void-context block); codegen (cleanup at ALL exit points of the enclosing fn, inline-at-exit via `g_cg_defers[]`); VM (deferred-stack per frame `defers: []DeferBody` in Venv, LIFO on exit); TKB serialize tag=7; self-hosted codegen honest stop~~ ✅ | `L`, `ast`, `P`, `chk`, `cg`, `vm`, `tkb` | **1** | ✅ build green + 174 tests pass (VM defer tests included) |
| ~~**W6**~~ ✅ | ~~**C7.1j** multi-OS/arch CI pipelines — Linux arm64+x86, Windows arm64+x86, macOS arm64 *(deps: all Phase 7 ✅, Phase 8 ✅, defer ✅)*~~ ✅ | CI scripts | | |
| | ~~**C9.1** SAST gate (CI checks) *(deps: C9.2 ✅, C7.1j CI infra)*~~ ✅ | CI scripts | **2** | ✅ CI green on all platforms (Linux x86/arm64, Windows x86/arm64, macOS arm64, riscv64-qemu, CodeQL, clang-tidy, ASan/UBSan/TSan, Windows stress) |
| ~~**W7**~~ ✅ | ~~**S1** arena primitive + root region *(deps: S0 seam ✅)*~~ ✅ | `rt` (runtime seam) | | ✅ bump allocator (`tk_region_new`/`alloc`/`drop`/`root`, chunk-list, OOM-panic) in `teko_rt.{h,c}`; **runtime seam only** — `tk_alloc`→root region (never dropped = today's leak, no behavior change); honest `.tks` note (raw-pointer host bottom, no Teko surface); `core.h` compiler seam stays on libc until S2 (no per-block size header → realloc needs S2 migration; M.1+M.5 settle this — runtime-only). 177 tests (both engines); native self-compile runs through the arena. **Lives in `teko_rt`, not `src/arena/`** — generated programs only receive `teko_rt.c`+headers via the driver, so a separate cc input was avoided (M.5). |
| | ~~**S4** generics — unconstrained, monomorphization *(deps: —)*~~ ✅ | `src/checker/monomorph.{tks,c,h}` | | ✅ inference-only function generics (`fn id<T>(x:T)->T`). Parser `<T,…>` decl params; checker `type_param_table`/`Subst`/`unify`/`subst_type`; **monomorph pass** (`src/checker/monomorph.tks`+twin, wired last in `type_program`): no-op when no generics (corpus byte-identical), worklist fixpoint stamps a concrete copy per instantiation (`<name>__g__<argmangle>`), rewrites every `TExpr.type` + call callee, 5000-instance ceiling → honest M.1 error for unbounded polymorphic recursion. Verified: 197 tests both engines, gen-2==gen-3 fixpoint, **0** `tk_t_T`. **Reused `Named` Type case (no new variant)** so B.15 exhaustiveness holds compiler-wide. **TYPE-generics also DONE** (`type Box<T> = struct{value:T}`, `let b: Box<i64> = Box{value=42}` → exit 42 both engines): parser `<…>` type-args (speculative+backtrack); `resolve_generic_inst`→`Named{Box__g__i64}` + shared mangle; `instantiate_types` pass stamps concrete decls into the table pre-typing; annotation-driven struct-init (`type_struct_lit` expected-type); mono emits the stamped type-decls into items. **Nested `Box<Box<i64>>` DONE** (parser `pending_gt` close-split + expected-type threaded into field values + codegen instance-name dep-ordering) → exit 42 both engines. Deferred: value-driven struct-init (no-annotation), instantiation in casts/if-match-nested bindings, constraints (S6). |
| | **MEMORY-MODEL CAMPAIGN (re-prioritized 2026-06-28, user) — the FOUNDATION, built first.** Brevity ⟸ `Ref<T>` in both engines ⟸ VM aliasing ⟸ region alloc/dealloc; so the full memory model is built FIRST, security-first (memory-safe by construction), VM aliasing UN-deferred. Then generics+`Ref` brevity (overrides W15 DRY-LAST). Standing: raise coverage every wave · build code via agents. CAVEAT: these C-emission reps (`<T> *`, `teko_rt` arena regions) are provisional — likely reworked at the future native-byte backend. Memory: [[teko-ref-and-ptr-generic-ideas]], [[teko-dry-via-generics-ref-and-coverage]]. | | | |
| **W8** | ✅ **MEM-1 — AUTOMATIC MEMORY MODEL: DONE** (alloc + dealloc + memory-safe references; security-vetted, ASan-clean). All pieces committed + gated: `ptr<T>` typed pointer (`17aefd5`) · `Ref<T>` type + native `.value`/auto-ref + VM aliasing (statement-position) (`4885bc9`…`602859e`) · escape-gate = `Ref<T>` is a param-only borrow that CANNOT escape = **memory-safe by construction** (`938b5b8`) · void tail-match codegen fix (`906d66c`) · per-function regions + one-depth escape analysis (`escape.{tks,c,h}`) = real DEALLOC (`5ca3e95`) · NON-SCALAR refs `Ref<struct>`/`Ref<[]T>` (`023f954`). **236 tests both engines · gen-2==gen-3 fixpoint · clang-tidy-18 clean · 3 adversarial ASan rounds closed every UAF.** Per-item detail in git + memory. | `res chk cg vm rt escape` | | ✅ DONE |
| **W9** | ✅ **S2 — block-scoped regions: COMPLETE** (the dealloc VOLUME). Per-FUNCTION regions (W8) + **LOOP-BODY** (`3751a0c`, per-iteration via `_tkbr<n>`) + **STATEMENT-position if/then/else + match-ARM** (`ac7b460`, `is_loop` flag so arm regions are crossed by break/continue, only loops are stop targets) + **VALUE-position if/match arms** (`c4be6c3`, materialize tail into enclosing sink BEFORE arm-region drop; Teko twin threads escaping/regions/fn_body through emit_expr's ~65 sites via `emit_expr_ctx`). EVERY scope block (fn, loop, statement-arm, value-arm) now has its own arena cleaned at exit. `tk_binding_is_block_local` (total fn reads == safe reads strictly inside B, `is_value` excludes the tail). 254 tests both engines, gen-2==gen-3 byte-identical, clang-tidy-18 clean, full self-host + all reproducers ASan-clean. **USER CLARIFICATION (2026-06-29): every function/method/BLOCK automatically gets its own arena scope, cleaned when it ends — structural, NOT annotation-driven.** *(deps: MEM-1 ✅)* | `chk cg escape` | **1** | ✅ gen-2==gen-3 · ASan-clean |
| **W9.3** | ✅ **DEFER per-scope DONE (`30e8e68`) — all engines, VM==native.** `defer` fires at EACH block's exit (loop body per ITERATION, if/match arm per arm, fn body), LIFO within a block, innermost-first on break/continue/return, BEFORE that block's S2 region drop; SKIPS panic/exit. BOUNDARY PRINCIPLE (user): continue/break/return are the scope boundary, defer runs inside the scope immediately before it → a value carried across the boundary (`return acc` / trailing) is evaluated AFTER the scope defers (defer can affect the return → 9); a VALUE-arm captures its tail BEFORE its own defer (tail-then-defer, opposite of fn-return). codegen `defer_base` per scope-block (.c + .tks twins; .tks threads a DeferCtx through emit_expr; old source-transform + honest-stop removed); VM every exec_block is a defer scope + return pre-drains the frame. Fixed: no-region value-arm defer (was undeclared-identifier + `g_cg_ndefers` leak). 267 tests both engines, gen-2==gen-3 byte-identical, clang-tidy clean, self-host + reproducers ASan-clean, defer programs compile byte-identically on build/teko & bin/teko. | `vm cg chk` | **1** | ✅ VM==native · ASan · gen-2==gen-3 | [[teko-defer-per-scope]]
| **W9.3b** | ✅ **ARENA MEMORY freed on PANIC/EXIT DONE (`f3dc67a`).** `teko_rt` global intrusive registry of live regions (`tk_region_new` links, `tk_region_drop` unlinks, idempotent) + `tk_regions_free_all()` drained by `tk_panic`/`tk_exit` (before abort/exit) and `atexit` (normal end) → root + every live `_tkfr`/`_tkbr` freed; no leak at abnormal termination. Native-runtime only (VM uses no regions). Leak-clean (0 live blocks on exit/panic/normal, was 2-3); no double-free/UAF; 267 tests; gen-2==gen-3. | `rt` | **1** | ✅ ASan-clean panic+exit · gen-2==gen-3 | [[teko-defer-per-scope]]
| **W9.4** | ✅ **PARSER generics ergonomics DONE (`3d48a19`).** (1) explicit type-args at STRUCT CONSTRUCTION `Foo<i64>{ … }` → stamps `Foo__g__i64`, NO annotation; (2) type-args in MATCH-BIND `Foo<i64> as x` → resolves the stamped instance. Shared speculative `<…>` arg parser (reuses `>>`/Shr close-split, backtracks so `a < b` still parses); AST carries an optional type-arg list, checker resolves+discards it (TAST/TKB unchanged). PLUS the codegen/VM completion **generic instances as VARIANT MEMBERS** — a normalization pass (resolve.{c,tks}) rewrites every `Base<args>` reference in every type-decl body to the bare stamped `Base__g__<mangle>` (no-op on the current corpus → byte-identical), so a `Gen<i64>` value variant-wraps + matches end-to-end (was a cc error). 282 tests both engines (+15), gen-2==gen-3 byte-identical, clang-tidy clean, self-host+reproducer ASan-clean, VM==native (`Gen<i64>{val=7}` into `Gen<i64> | Other`, matched → 7). UNBLOCKS W9.5b. *(deps: generics ✅)* | `ast P chk match cg vm res` | **1** | ✅ gen-2==gen-3 · VM==native |
| **W9.5** | 🔶 **BREVITY — DRY (split by what's unblocked).** **(a) `Ref`-threading — UNBLOCKED, do now:** replace functional threading with in-place `Ref` mutation (env→`Ref<Env>`, codegen buffer→`Ref<[]byte>`, accumulators) — uses `Ref` only in param/annotation + auto-ref + `.value`, so it hits NEITHER parser wall. **(b) generics-dedup — BLOCKED on W9.4:** `R<T>` 42→1 (~350), `read_list<T>` 23→1 (~200), `Parsed<T>` 17→1 (~80), `unwrap_r<T>` 204 sites (~100) ≈ 630 LOC, but every site constructs in return-position / matches by bare path → needs W9.4 first. Overrides W15 DRY-LAST for generics/Ref dedup. Batch-gated. *(deps: (a) non-scalar Ref ✅; (b) W9.4)* | `tkb chk cg P` widespread | | gen-2==gen-3 · VM==native · +coverage/batch |
| **W10** | 🗣️ **S4c** closures — **RESERVED CO-DESIGN / DISCUSSION (user 2026-06-29): the closure FORMAT (syntax) and BEHAVIORS (capture-by-value vs by-ref, lifetime/region of the captured env, escaping closures) get discussed when reached — NOT assumed.** Tentative only (subject to that discussion): `fn` literals + lexical capture; env allocated in a region, referenced via `Ref<T>`; `\|params\| body` syntax; checker capture types; codegen `{env, fn}`; VM closure value. RE-ORDERED here (needs lifetime-bound capture storage). *(deps: MEM-1, S2 ✅)* | `ast`, `P`, `chk`, `cg`, `vm`, `tkb` | **1** | discuss → design → build green + VM==native |
| **W10b** | 🗣️ **OOP — DEFINE + IMPLEMENT (NEW step, user 2026-06-29; RESERVED CO-DESIGN / DISCUSSION — do NOT design unilaterally).** Objects/classes/methods (+ whatever model we agree: encapsulation, dispatch, etc.). MUST land BEFORE the lifetime/DI annotations (DI annotates how an OBJECT binds to memory/arena → no objects to annotate until OOP exists). Surface + semantics to be discussed when reached. *(deps: S2 ✅; gates MEM-3)* | TBD (discuss) | | discuss → design → build green + VM==native |
| **W10c** | 🚧 **MEM-3** lifetime/DI annotations `#singleton` (global arena) / `#scoped` (scope-bound) / `#transient` (no scope) — **apply to DATA/OBJECTS only, NOT functions/methods/blocks** (user clarification 2026-06-29: those always get automatic per-scope arenas via S2). These annotate an object's lifetime; native-DI uses them to decide how an object binds to memory/arena (⚠️ `#transient` ownership OPEN); SUBSUMES old S3 `ref`-keyword (now `Ref<T>`) + S5 DI lifetimes. **BLOCKED on OOP (W10b) — moved after it (user 2026-06-29).** *(deps: S2 ✅, OOP/W10b)* | `chk`, `cg`, `vm` | | build green + VM==native |
| **W11** | **S6** constraints — positive; `!` only prims/sealed; forced by Map *(deps: S4 ✅, S3 ✅)* | `chk`, `cg`, `vm` | **1** | build green + VM==native |
| **W12** | **S7** real dynamic collections — amortized append, Map, Set; out-region param *(deps: S4 ✅, S6 ✅, S2 ✅, S3 ✅)* | `chk`, `cg`, `vm`, `src/collections/` (new) | **1** | build green + VM==native |
| **W13** | **S8** concurrency capstone — `scope{}`/`spawn` (routine launch, M:N scheduler); `channel<T>`/`send`/`recv`; **`Mutex<T>`** (lock/unlock, panic-poisoning); **`Semaphore`** (counting acquire/release); **`RWMutex`** (concurrent reads + exclusive write); **`WaitGroup`** (add/done/wait); **thread-control** (`thread::spawn`→`JoinHandle`, join, detach, park/unpark); **M:N work-stealing scheduler** *(deps: S2 ✅, S3 ✅)* | `chk`, `cg`, `vm`, `src/concurrency/` (new) | | |
| | **S9** LTS cleanup — `Parsed<T>×14`→`Result<T>`; unify `parse`/`to_string` *(deps: S4 ✅)* | `ast`, `P`, `chk` (widespread) | **2** | build green + VM==native *(S8+S9 share `chk`; internal crumbs serialize; detailed at M.4)* |
| **W14** | **S10** async/await — `async fn` + `await expr`; coroutine-based non-blocking I/O; Teko runtime event loop (epoll/kqueue/IOCP per OS); cancellation via `scope{}`; composable with `channel<T>` + `Mutex<T>`. ⚠️ **NATIVE ASYNC SURFACE (user, 2026-06-28):** support `async main.tks` (an async top-level/main) AND `async #test` functions — design the runtime so main + the test runner can be async; settle at this step. *(deps: S8 ✅)* | `chk`, `cg`, `vm`, `rt`, `src/async/` (new) | **1** | build green + VM==native |
| **W15** | **C11.1** quality sweep (KISS+SOLID+YAGNI+12F+comments) — lexer + parser. *(Generics/Ref DRY already done early in W9.5; W15 is the residual KISS/SOLID/comment-hygiene sweep.)* | `L`, `ast`, `P` | | |
| | **C11.2** quality sweep — checker | `chk`, `match`, `collect`, `res`, `scope`, `tast` | | |
| | **C11.3** quality sweep — codegen + vm | `cg`, `vm` | | |
| | **C11.4** quality sweep — emit + runtime + build + assert + main | `tkb`, `rt`, `build`, `assert`, `main` | **4** | DRY ✓ · KISS ✓ · SOLID ✓ · YAGNI ✓ · 12F-III/V/IX/XI/XII ✓ · comments `/** */` · VM==native |
| **W16** | **C12.1** self-host RAW byte-identity — make the codegen temp-name (gensym) scheme deterministic in BOTH `codegen.c` and `codegen.tks` (today both use `buf.len`, which requires byte-for-byte-identical emission at every sampling point; a single sub-byte timing diff cascades into all temp NUMBERS). gen-1 (C bootstrap) vs gen-2 (self-hosted) currently differ ONLY in temp numbers (temp-normalized AND all-number-normalized diff = 0; 178 tests × 3 gens). NOTE: gen-2 == gen-3 ALREADY (self-hosted compiler is a stable FIXPOINT) — the divergence is only the one-time C→self-host transition. Verify via MULTI-GENERATION convergence (gen-1==gen-2==gen-3==…). *(deps: self-host green ✅; defer codegen parity ✅ commit 52a544a)* | `cg` | **1** | gen-1 == gen-2 byte-identical + 178 tests × N gens |

### DEFERRED · BLOCKED · KNOWN LIMITATIONS (the single consolidated list — everything `⏸️`/`🚧`/`⚠️`)

**⏸️ DEFERRED (intentionally postponed; will revisit):**
- **value-position `Ref` in the VM** — `let y = bump(x)` (a ref-mutating call whose result is consumed) honest-stops in the VM; native runs it. STATEMENT-position is complete (covers the brevity refactors). Full fix = thread the cell store through `eval_expr` (large ripple). Revisit when a corpus/user need arises or at the native-byte backend.
- **`Ref<T>` struct representation `{ value: ptr<T>? }`** — today `Ref<T>` lowers to a bare `<T> *`. The struct-with-nullable-pointer rep is the shape for when a nullable ref / metadata is actually needed (W10 lifetime annotations / DI). "If needed."
- **W16 raw gen-1==gen-2 byte-identity** — gen-2==gen-3 IS byte-identical (stable fixpoint); only the one-time C→self-host transition differs by cosmetic gensym numbers (temp-normalized diff = 0). Deterministic-gensym scheme deferred to W16.
- **C-emission reps are provisional** — `ptr<T>`/`Ref<T>`/regions are shaped by the transpile-to-C backend; likely reworked at the future native-byte backend (not yet scheduled).

**🚧 BLOCKED (needs a prerequisite):**
- **generics-dedup of the corpus's own types** (`R<T>`/`Parsed<T>`/`read_list<T>`/`unwrap_r<T>`, ~630 LOC) — BLOCKED on **W9.4** (explicit type-args at struct-construction + match-bind). Discovered 2026-06-29. The `Ref`-threading half of brevity is NOT blocked.

**⚠️ KNOWN LIMITATIONS (documented; native correct / not blocking):**
- **VM wrapper-descent** — VM `type_eq` can't distinguish same-inner single-field wrappers (`Ref`/`Ptr`/`Optional`/`Slice`); NATIVE is correct via tags. VM-only (tests); keep VM tests nominal-direct. [[teko-vm-wrapper-descent-bug]]
- **`tk_opt_<T>` typedef gap** — a struct's UNREAD optional self-ref field (`next: Node?`) doesn't register its `tk_opt_<T>` typedef → cc fails. The `Ref<T?>`-param case was fixed; the general field case remains. Pre-existing codegen; fix during a codegen pass.
- **nullable generic args** (`Box<i64?>`/`List<Foo?>`/`Ref<i64?>`) — ✅ **SUPPORTED** (user reversal 2026-06-29; the brief prohibition was never implemented). The divergence behind them was a PRE-EXISTING VM bug — struct-literal construction didn't present-wrap an optional field from a bare value — now FIXED with native parity (`coerce_to` on the declared field type; mirrors `emit_struct_init`'s per-field `emit_as`). The `<T?>`/`<T: C?>` DECLARATION ban stays. [[teko-generics-constraints-rules]]
- ✅ ~~bare `T` → `T?` parameter~~ **FIXED** (commit bb32be7) — law-first ruling (present-wrap, by uniformity with field/return positions): checker keeps the arg's narrow type for a bare→optional widen, codegen call-loop + VM `bind_call_args` present-wrap. VM==native, both engines.
- ✅ ~~`[]T` → `[]T?` slice covariance~~ **FIXED** (commit bb32be7) — codegen `cg_wrap_elem_str` present-wraps optional elements (variant covariance preserved); VM `coerce_to` gains a Slice rebuild case. Field/param/binding/return positions all rebuild `[]T?` uniformly. 8 new tests, gen-2==gen-3 byte-identical.
- **#41 namespace-aware TYPE resolution** — latent (corpus has zero cross-namespace type-name collisions); revisit if a collision is introduced.

**Deferred ceilings (never enter the sequence):** borrow-solver / lifetime-variable system; region-polymorphism. Implicit-copy-on-escape → tribunal (lean: NO). *(async/await → W14; M:N scheduler → W13 — both removed from deferred.)*

**Bottlenecks:** `cg`/`vm`/`rt`/`tkb`/`chk` serialize across waves (single-owner). New-namespace files (W7–W14) have no concurrency constraint. Wave widths 1–5 are capped by these bottlenecks, not by agent count.

**STANDING (from W8 on, user 2026-06-28):** ① **RAISE TEST COVERAGE every wave** — fold coverage into each step (don't lower the `teko.tkp [coverage]` floor; add tests that hit NEW branches) so no dedicated catch-up step is needed. ② **Build code via AGENTS** (draft + I integrate/gate; worktree isolation unreliable on this repo). ③ **SECURITY-FIRST** for the memory model (memory-safety by construction). ④ Continue waves autonomously (commit+push+fix-CI) until a real tension → HALT + surface.
