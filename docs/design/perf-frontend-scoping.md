# Front-end arena scoping — safe per-pass / cross-pass rewind design

Status: DESIGN (no product code changed). Author: architect. Date: 2026-07-14.
Base: `origin/remodel/backend-build` (tip `b9006f9`).
Companion record: `docs/design/perf-phase.md`.

## 1. The problem, restated

The front-end bump-allocates **everything** into the process-lifetime root region
(`tk_alloc` -> `tk_region_alloc(tk_region_root(), n)`, `src/runtime/teko_rt.c:1228`).
That region is **never dropped** (M.5 leak-by-design): 471 MB, 0.0% reclaim, 5007
chunks, no per-file / per-pass rewind. The only scoping that exists is
`tk_arena_push` / `tk_arena_pop` (`teko_rt.c:1255` / `:1265`) — a LIFO
checkpoint/rewind of the **root region's bump position**, used today only by:

* the **native test-gate** per-`#test` wrapper (#469), emitted into the *generated
  test binary's* C (`src/codegen/codegen.tks:8562` / `:8573`) — runs in the CHILD
  process, not in this compiler; and
* (historically) the retired in-VM per-test rewind (#109).

`tk_arena_push`/`pop` are already reachable from Teko as the host builtins
`teko::arena_push()` / `teko::arena_pop()` (checker signature
`src/checker/scope.tks:312-313`; lowering `src/codegen/codegen.tks:2323-2324`).

## 2. Lifetime map (Q1)

Everything below lives in ONE root region, interleaved chunk-by-chunk. The
distinction that matters is **live-to-codegen** vs **transient**, and — for the
transient set — whether it is *separately reclaimable* (contiguous / in its own
region) or *interleaved* (cannot be freed without also freeing live neighbours).

### LIVE-to-codegen (must NOT be rewound while a codegen of the same pass is pending)

| Allocation | Evidence | Why live |
|---|---|---|
| Source string(s) (`read_file` result) | `Token.text: str` (`src/lexer/token.tks:186`) is a fat-pointer slice of the source; AST name/text fields are `str` copied from `Token.text` and still point at the source buffer | AST names alias the source bytes; codegen reads them |
| Untyped parser AST | `TItem = variant TFunction \| parser::TypeDecl \| parser::UseDecl \| TStatement` (`src/checker/tast.tks:148`); `TCall.callee: parser::Path` (`:31`); `TArm.pattern: parser::Pattern` (`:35`); `TFunction.params: []parser::Param`, `type_constraints: []parser::ConstraintExpr`, `vis: parser::Visibility` (`:132-136`); `TBinding.kind/target` (`:86`) | The typed tree **embeds** parser nodes verbatim; freeing the AST frees pieces of the typed tree |
| Typed tree (`TProgram`/`TItem`/`TExpr`/…) | `TProgram { items: []TItem }` (`tast.tks:149`), consumed by `codegen::tk_emit_c` | The codegen input |
| `Type` nodes / type table | referenced across items (a type in file A referenced by file B) — the M.1 whole-program merge | Cross-file references; monomorph + codegen read them |
| Symbol / namespace maps | built by check, read by codegen (`.tsym`, mangling) | Codegen reads them |

### TRANSIENT (dead after the pass that made them)

| Allocation | Dead after | Separately reclaimable? |
|---|---|---|
| `[]Token` spine (array of `Token` structs) | parse | NO — interleaved with AST in root; and its `.text` slices point at the LIVE source (not at the Token structs), so a naive token rewind is safe for the spine but the spine is not contiguous |
| Pass-internal work-lists (monomorph worklists, checker scope/env scratch, codegen intermediate byte buffers before the final `str`) | pass end | NO — interleaved with live tree nodes in root |
| **The entire first (with-tests) front-end product + test-gate codegen output** | after the child test run + coverage read, before `release_build_of` | **YES — a single contiguous LIFO span of root** |

Conclusion: **within a single front-end pass, there is no clean, high-payoff,
separately-reclaimable transient** — the token spine and per-pass scratch are
interleaved with live tree nodes, so a bump-checkpoint rewind of root would free
live data (use-after-free). The only clean, contiguous, safe boundary is the
**cross-pass** one: the whole throwaway gate pass.

## 3. Safe scope boundaries, ranked (Q2/Q3)

### Boundary A — WHOLESALE REWIND OF THE THROWAWAY GATE PASS  (SAFEST, biggest payoff)

The verify build (`compile_project_g`, `src/build/project.tks:1404`) runs the
front-end **twice**:

1. `frontend_body(true, false)` — assemble WITH `.tkt` tests, check -> `fe`
   (`project.tks:1410`). `fe.prog` is used to codegen the test TU
   (`run_native_gate`, `:1303`), which is compiled and run **in a child
   process**; then coverage is read from `fe.prog`
   (`report_native_coverage` / `coverage_gate_result` / `cov_cobertura`).
2. `release_build_of` (`:1335`) then runs `frontend_body(false, true)` **fresh**
   -> `rel.prog`, and codegens the release binary. It **never references `fe`.**

So at the instant `release_build_of` is entered, the **entire pass-1 product**
(`fe.prog`: AST + typed tree + type table + symbol maps) AND the test-gate
codegen `str` are **fully dead**. They form one contiguous LIFO span of the root
region (nothing from before pass 1 is interleaved after the checkpoint).

**Use-after-free hazards ruled out:**

* The test binary runs in a CHILD process (`run_native_gate` -> `teko::process::run`,
  `project.tks:1318`) — it holds no pointer into this compiler's arena.
* Coverage sinks are **libc-heap**, not arena (`tk_cov_ids`/`tk_covb_ids` are
  `malloc`/`realloc`, `teko_rt.c:1766-1781`, `:1795`), and are merged from the
  child's dumped `.tkcov` (`cov_merge`) — arena-independent, survive any rewind.
* All reads of `fe.prog` (`report_native_coverage`, `coverage_gate_result`,
  `cov_cobertura`) happen **before** `release_build_of` — none after the rewind.
* `tk_arena_pop` already purges the two root-scoped caches that could hold stale
  addresses across a rewind: the free-list (`tk_free_purge`) and the live-tail
  witness / push cache (`tk_push_cache_purge`, `teko_rt.c:1269-1270`, the #148
  safety).

**Payoff:** cuts the *verify-path* compiler peak from
`pass1 + test-codegen + release-pass` (piled on a never-dropped root) to
`max(pass1 + test-codegen, release-pass)` — roughly halving the front-end's own
root-region share on the full-gate path (~1.77 GB today). It does **not** help
`--no-verify` (a single pass, no gate) — that path is addressed only by the
concurrent list/string pre-sizing + (deferred) Boundary B.

**One control-flow wrinkle** (the `!has_tests` case): `frontend_body(true, …)`
is built *before* we know whether tests exist, and on the no-tests path `fe.prog`
**IS** the release program (`build_no_tests_present`, `project.tks:1386`) — it must
NOT be rewound. Because the checkpoint must precede `frontend_body(true, …)` to
capture `fe`, we need a way to *discard the checkpoint without rewinding* on the
keep-`fe` path. Two options:

* **(A1, recommended) add a trivial `tk_arena_commit()` primitive** — pop the top
  mark WITHOUT freeing (commit the allocations to the enclosing scope). Three
  lines in the maintained runtime seed; keeps the mark stack balanced. Exposed as
  the builtin `teko::arena_commit()`.
* **(A2, primitive-free fallback)** on the no-tests path, `tk_arena_pop()` (rewind)
  and re-run `frontend_body(false, …)` fresh for the release — trading a second
  front-end pass to avoid a new primitive. Wasteful for the common small-project
  no-tests case; only choose it if the owner wants zero runtime additions.

This design uses **A1**.

### Boundary B — PER-PASS SCRATCH SUB-REGIONS  (deferred; needs a targeted proof)

Genuinely transient per-pass work-lists (monomorph worklists, checker scratch,
codegen pre-`str` byte buffers) are dead at pass end but **interleaved** with live
tree nodes in root, so they are NOT reclaimable by a root bump-checkpoint. To
reclaim them a pass must allocate its scratch into its **own child region**
(`tk_region_new(root)` -> `tk_region_alloc` -> `tk_region_drop`), which requires
**region-parameterised list/string builders** threaded through the pass — an
invasive change that touches exactly the list (`teko::list::push`) and string
(`str::concat`) layers the concurrent implementer is currently pre-sizing and
interning. **Defer** until that work settles, then re-scope. Flagged
**needs-a-targeted-proof**: every list/buffer moved into a scratch region must be
proven not to leak a pointer into the returned tree (an aliasing audit per pass).

### Boundary C — TOKEN-SPINE / UNTYPED-AST REWIND  (RULED OUT for now)

Superficially attractive ("free tokens after parse; free the AST after check"),
but **BLOCKED by aliasing**: the typed tree embeds parser nodes (§2 evidence) and
all `str` name fields alias the source buffer. Rewinding the untyped AST or the
source after check is a **use-after-free**. Only becomes possible with Boundary B
sub-region threading PLUS interning names/source *out of* the rewound span (the
concurrent interning work is a prerequisite, not a sufficient condition). Do NOT
attempt before B lands and an aliasing proof exists.

## 4. Runtime primitive needed (Q4)

* **Boundary A** needs NO scratch region — only a **`tk_arena_commit()`** companion
  to the existing `tk_arena_push`/`pop` (discard the top mark without rewinding).
* **Boundary B** (deferred) needs the existing `tk_region_new` / `tk_region_alloc`
  / `tk_region_drop` (the S2 arena-tree the `Arena` feature already uses,
  `teko_rt.c:1036` / `:1076` / `:1111`), threaded through the passes via
  region-parameterised builders. No NEW allocator — reuse the tree.

Proposed C seed (maintained runtime, `teko_rt.c`, beside `tk_arena_pop`):

```c
/* Discard the top checkpoint WITHOUT rewinding — commit its allocations to the
 * enclosing scope. The bump position and every chunk stay; only the mark is
 * popped. Balances a tk_arena_push that must be KEPT (the no-tests gate path,
 * where the first front-end product IS the release program). Under-flow tolerant. */
void tk_arena_commit(void) {
    if (tk_arena_msp <= 0) return;
    tk_arena_msp -= 1;
}
```

Checker signature (`src/checker/scope.tks`, mirroring `:312-313`):

```teko
if name == "arena_commit" { return Func { params = teko::list::empty(); ret = void_t; variadic = false; param_names = []; n_required = 0; defaults = [] } }
```

Codegen lowering (`src/codegen/codegen.tks`, mirroring `:2323-2324`):

```teko
else if last == "arena_commit" { builtin = "tk_arena_commit"; has_builtin = true }   // () -> void
```

Header declaration (`src/runtime/teko_rt.h`, beside `tk_arena_pop`): `void tk_arena_commit(void);`.

## 5. Ordered crumb sequence (smallest-safest first)

Each crumb is independently gate-able (the full self-host gate must stay green).
Assume the concurrent list-pre-sizing / string-interning layer may shift the
list/string call shapes slightly — none of the crumbs below depend on their
internals, only on `teko::list`/`teko::str` surface.

**Crumb 1 — add the `arena_commit` primitive (enabler, no behaviour change).**
* `src/runtime/teko_rt.c`: add `tk_arena_commit` (above) with full-Javadoc doc-comment.
* `src/runtime/teko_rt.h`: declare it beside `tk_arena_pop`.
* `src/checker/scope.tks`: register the `() -> void` signature.
* `src/codegen/codegen.tks`: lower `arena_commit -> tk_arena_commit`.
* Regression fixture (native): `examples/regressions/arena_commit_keeps` — push,
  allocate a list, `arena_commit()`, then READ the list -> exit 0 (data survives);
  and `arena_pop_frees` twin under `TEKO_ARENA_OBS` asserting the pop reclaims the
  span. See `docs/design/perf-phase.md` §5 for the fixture inputs -> exit codes.
* Gate: full self-host gate green + the two fixtures. No caller yet -> zero
  behaviour change (ritual point R1).

**Crumb 2 — wrap the throwaway gate pass (Boundary A).** Only after Crumb 1 gates.
* `src/build/project.tks::compile_project_g` — insert `teko::arena_push()`
  immediately before `frontend_body(true, false)` (`:1410`); on the `!has_tests`
  branch call `teko::arena_commit()` before `build_no_tests_present`; on the
  `error` arm call `teko::arena_commit()` before `fail`.
* Split `run_gate_native` (`:1355`) so the rewind sits between "gate + coverage
  done (all `fe` reads complete)" and `release_build_of`: call
  `teko::arena_pop()` immediately before `release_build_of(dir, out_dir)`
  (`:1367`). On a gate-failure early `return` the mark is left un-popped — harmless
  (the process exits non-zero right after; `atexit` frees the arena). Document that
  in the fn doc-comment.
* Update the touched fns' Javadoc doc-comments to record the checkpoint contract.
* Gate: full self-host gate green; **verify-path peak drops** (measure via
  `report_peak` / `TEKO_ARENA_OBS`). Ritual point R2 = the FULL gate under ASan
  (`ci/heavy-sanitizers-on-main-only`) to prove no use-after-free across the pop.

**Crumb 3 (DEFERRED, needs-proof) — per-pass scratch sub-regions (Boundary B).**
Do NOT start until the list-pre-sizing / interning work lands and an aliasing
audit per pass is written. Region-parameterise the chosen pass's work-list
builders, allocate scratch into a `tk_region_new(root)` child, `tk_region_drop` at
pass end. One pass at a time, each behind its own aliasing proof + ASan gate.

## 6. Risks + law tensions

* **TOP RISK — interaction with the concurrent string-interning work.** If the
  interner stores interned bytes **in the arena (root) within the rewound span**
  during pass 1, and the release front-end (pass 2, after the rewind) looks them
  up and gets a hit into freed memory, that is a use-after-free. **Resolution
  (coordinate):** the interner's byte storage MUST be OUTSIDE the rewound span —
  either (a) libc-heap (survives the rewind; a bonus, pass 2 re-uses the same
  interned bytes), or (b) purged inside `tk_arena_pop` via a new `tk_intern_purge()`
  hook alongside `tk_push_cache_purge` (pass 2 re-interns from scratch). Option (a)
  is preferred. This must be settled with the interning implementer BEFORE Crumb 2
  lands, and proven by the ASan full-gate run (R2).
* **Mark-stack balance across functions.** The push (`compile_project_g`) and pop
  (`run_gate_native`) live in different functions; error paths inside the gate
  `return` without popping. Balanced on the success path; un-popped on error is
  harmless (imminent non-zero exit + `atexit`). No correctness issue; documented.
* **Law tension — none.** Teko-only: all edits are `.tks` except the maintained
  runtime seed `teko_rt.{c,h}` (explicitly exempt). Full-Javadoc: every new/edited
  declaration carries a doc-comment (snippets above are written gate-ready).
  Whole-program M.1 is preserved — Boundary A rewinds a WHOLE dead pass, never a
  live cross-file reference; Boundaries B/C, which could touch live references, are
  deferred behind proofs. No genuine unresolved tension -> no HALT.
