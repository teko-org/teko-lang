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

**★ SELF-HOST CHECK GREEN + NATIVE CODEGEN COMPILING DEEP (2026-06-26):** `teko build .` →
*"project assembled (853 items) and type-checked OK"*; the native backend now generates C that compiles
through almost the entire corpus, with a short tail of remaining walls. SIX milestones committed this
session (each mirrored to `.tks`, regressions VM==native 5/6, bootstrap clean):
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

**Remaining native walls** (short tail): host-FFI builtins (`write`/`ewrite`/`parse` + Phase-7
file/process/env runtime), duplicate same-namespace helpers (`is_bool`/… in two checker files), one
bind-whole-variant pattern. See memory `teko-collections-rulings.md` for the exact next-step list.

---

## THE SEQUENCE

> **Status legend:** ✅ done · 🔶 in progress / partial · ⬜ not started. Updated 2026-06-26 (commit `8ae56d5`).

| # | Phase | Status | Why here |
|---|-------|--------|----------|
| 1 | Diagnostics axis | 🔶 E1/snippet+caret/expected-actual/E2 ✅; E3/E4 `.tsym`+stack-trace ⬜ | Highest ROI; makes every later phase debuggable |
| 2 | `in` operator | ✅ done (lexer→parser→checker→codegen→VM→tkb→.tks, single-eval) | Build the tool the DRY sweep will use (feature only) |
| 3 | str/byte stdlib as real mirrored fns | ✅ done (`teko::str::*` + host-FFI surface in scope.c/.tks) | Close a half-implemented layer; unblocks self-host CHECK |
| 4 | C↔.tks mirroring audit | 🔶 ongoing (closed many gaps during the CHECK march; no dedicated sweep yet) | Pay down mirror debt before more code lands |
| 5 | Definite-assignment / init analysis | ⬜ not started | Real checker gap; lands the warnings channel |
| 6 | Finish self-host → working `teko .` | 🔶 **CHECK ✅ GREEN** (853 items); **native codegen ✅ compiles deep** (auto-box, ns-mangling, case→variant wrap, slice rebuild — 6 commits); short tail (host-FFI builtins, dup helpers, 1 pattern) + VM-run ⬜ | The gating milestone for everything downstream |
| 7 | Host independence | 🔶 FFI builtins typed in checker; native runtime: str surface + `tk_alloc` ✅; host I/O (`write`/`ewrite`/`parse`/file/process/env) runtime impls ⬜ | FFI + host surfaces + VM test gate + project/output/pack |
| 8 | FLAGS | ⬜ not started | Bitflag enums (spec frozen) |
| 9 | SEC | ⬜ not started | SAST + capability audit, after corrections |
| 10 | Evolution S1–S9 | ⬜ not started | Post-self-host campaign (arenas→…→concurrency) |
| 11 | **DRY sweep + comment hygiene** | ⬜ **LAST** | refactor the settled corpus + doc-comments-only |

---

### Phase 1 — Diagnostics axis  *(§A.1 ∪ INDEPENDENCE Eixo E ∪ CORRECTION_PLAN §10 column-granularity)*  🔶 PARTIAL
**Status:** ✅ E1 (file:line:col threaded through tokens→AST→tast) · ✅ source snippet + caret · ✅ expected-vs-actual on every mismatch (type/arg/return/assign/field/struct-lit) · ✅ E2 (error fields/`err_loc`/`err_typed`; native degraded). ⬜ E3 `.tsym` symbol map · ⬜ E4 stack-trace · ⬜ cc-failure surfacing · ⬜ warnings channel (shared w/ Phase 5).
**Goal:** compile-time messages stop being poor. Errors point at the failing **expression**, not the enclosing function.
**Work:**
- **E1** — thread `{file, line, col}` through the whole pipeline: lexer → tokens → parser → AST → `tast` (every node knows its origin). Root cause today: AST exprs carry no position; only decls do.
- **Source snippet + caret** — print the offending line with a `^` under the column (clang/rust style); driver holds source text at report time.
- **Expected-vs-actual** types in every mismatch error (type / arg / return / assign / field).
- **cc-failure surfacing** — relate generated-C `cc` errors back to Teko.
- **E2** — `teko::Error` / panics carry file+line (VM and native). ✅ DONE (VM full; native degraded). The Teko `error` value gained accessible diagnostic fields (message/file/line/col/expected/actual) + `err_loc`/`err_typed` builtins (mirroring C `tk_error_loc`/`tk_error_types`); the `error { message }` literal stayed special-cased (NO 434-site ripple). Checker types them; VM carries them in full (`err_loc(...).line` == 7); the `.tks` checker wiring (typer.tks/check_modules.tks) prefers the expr position. **Native is DEGRADED** (error is still lowered to its message `tk_str`: `err_loc`/`err_typed` are no-ops, `error.line/col`→0, `error.file/expected/actual`→empty) so native compiles + self-host stays unblocked, but native diagnostics fall back to the item position. **Phase-6 follow-on:** represent native `error` as a struct (tk_str fields + runtime helpers) so native matches VM. (4-track workflow `e2-error-position`; codegen track HALTed on the error→tk_str representation, resolved via the degraded path.)
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

### Phase 4 — C↔.tks mirroring audit  *(§A.3)*
**Goal:** the legislator "smells" incomplete mirroring; confirm and close it.
**Work:** audit every `.c`/`.h` against its `.tks` twin (e.g. `typer.tks` was missing `builtin_fn`; `expr.c` bool+out vs typer.tks marker variant). Reconcile any lingering W5-cf-2 / corpus `.tks` mirror debt.
**Exit:** a mechanical diff finds zero behavioral divergence between any C file and its `.tks`.

### Phase 5 — Definite-assignment / init analysis  *(CORRECTION_PLAN W3b — REAL GAP, absent)*
**Goal:** mandatory initialization; use-before-init is an error; unused local = error, unused private = warning.
**Work:** init-analysis pass in the checker; wire the unused-* diagnostics into the Phase-1 warnings channel.
**Exit:** use-before-init and unused bindings are reported with Phase-1-quality locations.

### Phase 6 — Finish self-host → working `teko .`  *(tasks #55/#57; CORRECTION_PLAN §15.1; #40/#41/#19)*
**Goal:** the bootstrap compiles its own `src/` corpus to a native binary. The gating milestone.
**Status (2026-06-26, commit `048b4af`):**
- ✅ **CHECK GREEN** — `teko build .` type-checks the whole corpus (796 items, 0 errors). Delivered: the widening lattice (`tk_widens_into`/`tk_type_join`), enum-subject `match`, pervasive literal adoption, **E7 enum↔int casts**, the host-FFI builtin surface, the VM `.tks` value+eval cluster, `src/build/project.tks`. Regressions VM==native 5/6.
- 🔶 **#41 partial** — namespace-aware *call* resolution (`tk_env_lookup_call`) ✅; namespace-aware *type* resolution (a `Named` carrying its namespace) ⬜ still deferred (sidestepped via the `vm::Env/Return`→`Venv/VmReturn` rename).
- 🔶 **Native codegen** — generates C that compiles DEEP into the corpus (6 commits this session, all mirrored to `.tks`, regressions VM==native 5/6): slice value-layer, inline-union lowering, topological type-decl emission, **auto-boxing of recursive value types** (`tk_alloc` back-edge pointers, S0 seam — cycle fully broken), **namespace-qualified function mangling** (`teko::checker::type_eq`→`teko__checker__type_eq`, fixed #41/#49 collisions incl. libc `div`), **case→variant wrapping** at every site (bindings/struct-fields/call-args/returns — DIRECT, TRANSITIVE `Named`→`Type`→`Type|error`, and covariant `[]case`→`[]variant` element-wise slice rebuild), enum-subject match lowering, C-keyword escaping (`bool`/`signed`), alias-in-field resolution, `str==`→`tk_str_eq`, str builtin call-map, **function prototypes**. Native runtime: str surface (`tk_str_eq/slice/len/ends_with/contains/…`) + `tk_alloc` seam. **Remaining short tail**: host-FFI builtins (`write`/`ewrite`/`parse` — easy map+runtime; then Phase-7 file/process/env + `last_index_of`, variant returns); duplicate same-ns helpers (`is_bool`/… in expr.tks + revalidate.tks — structural dedup); one bind-whole-variant pattern (`TypeExpr as t` over optional); value-form-`if` diverging returns.
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

### Phase 7 — Host independence  *(INDEPENDENCE Eixos A/C/D + BINARY cleanup)*
**Goal:** the compiler reaches the host without C-side scaffolding; tests gate emission.
**Work (in order):**
1. **C1.0 LEGISLATE `extern`/FFI form** (TRIBUNAL — blocks all of FFI) → **C1.1** extern primitive (single opcode → platform convention).
2. **Host surfaces over FFI:** C2a `teko::env::args` + `teko::exit`; C2b `teko::io` slurp (read/write/write_err); C2c `teko::fs` `list_dir` (feeds discovery); C2d `teko::process` exec (invoke `cc`).
3. **VM test gate:** D2 `#test` runner in VM (`teko test`; makes the 646 asserts executable) → D3 per-line coverage (`.tkt` outside denominator) → D4 pre-emit gate (tests + coverage BEFORE codegen; floor 10%, default 80%; release bars on fail).
4. **Project/output:** A4 main-rule from the manifest artifact; A6 packages + pre-linker (load deps' `.tkh`+`.tkb`, merge typed trees pre-codegen); **A7 (DECISION)** output as a directory + `-o <dir>` (default `target/`, not `build/`); **A8 (DECISION, deferred)** `teko pack` → `.tkh`+`.tkb` (+metadata/checksum/.tsym/license/dep-lock discussion); CORRECTION_PLAN §11 package system.
5. **BINARY cleanup:** B0d promote `tk_str_eq` to `text.h`; B3a `TK_LIST` runtime list; B3b overflow-debug panic guard wiring; `.tkb` statement/program codec (today only `TExpr` serializes).
6. Crumb **M2** `driver.tks` falls out once C2* + A5 exist (future C4 deliverable).
**Exit:** `teko build`/`run`/`test` operate via Teko host surfaces; the test+coverage gate runs before any emission.

### Phase 8 — FLAGS (bitflag enums)  *(§A.7)*
**Goal:** a flags type — "an enum with another keyword."
**Spec (frozen):**
- Distinct **keyword** (e.g. `flags`); members are names like an enum.
- Member values **auto-computed at compile time** as powers of two: member n = `1<<n` (1, 2, 4, 8, …). No manual values.
- **Compile-time size identifier**; total bit-span capped at **u128** (overflow = compile error).
- **Bitwise ops supported** (`&` `|` `^` `~`/`not`); **NO shift operators** exposed (shifts are an internal compile-time detail).
- **Dev-friendly helpers** for non-bitwise devs: `has` / `all` / `any` / `none` / add / remove.
**Work:** full pipeline — `flags` keyword (lexer) + parser + AST + checker (power-of-two assignment, u128 size guard, bitwise-op typing, helper resolution) + codegen (emit as the u128-fitting unsigned int + helpers) + VM + `.tkb` + ALL `.tks` mirrors.
**Exit:** a `flags` decl auto-assigns powers of two, rejects >u128 span, supports bitwise + helpers, VM==native.

### Phase 9 — SEC  *(task #53 = CORRECTION_PLAN §14 = INDEPENDENCE C5)*
**Goal:** security/guard evaluation after corrections close.
**Work:** SAST gate + capability/sandboxing audit of the `exp` / `extern` / syscall surface.
**Exit:** the security gate runs in CI; the FFI/syscall surface is audited.

### Phase 10 — EVOLUTION campaign S1–S9  *(EVOLUTION_DESIGN/JUSTIFICATION; POST-SELF-HOST; R1/R2 ratified)*
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
**Deferred ceilings (keep deferred):** borrow-solver / lifetime-variable system; region-polymorphism; async/await (conditional); M:N green-thread scheduler. **Tribunal micro-decision:** implicit copy-out of a very small escaping value (default lean: NO implicit copy).

### Phase 11 — DRY sweep + comment hygiene  *(§A.5 — LAST)*
**Goal:** kill repeated identical calls with no variance across the WHOLE codebase (C AND Teko), e.g. `if at(source, p) == b'e' || at(source, p) == b'E'`.
**Work:** whole-corpus refactor using the tools now in place — the `in` operator (Phase 2), hoisting (`let ch = at(source, p)`), or `match`.
**COMMENT HYGIENE (legislator, 2026-06-26):** as part of this final settling pass, normalize ALL comments across the corpus (C AND Teko):
  - **Doc comments only** — every retained comment becomes a `/** … */` doc comment (block form), attached to the declaration/section it documents.
  - **Drop inline comments** — trailing/inline `// …` and `/* … */` mid-code comments are REMOVED; if a line genuinely needs explanation, the logic is hoisted/renamed so it reads without one, or the rationale moves into the preceding `/** … */`.
  - **Why last:** comments churn with the code through every prior phase; normalizing earlier would re-touch the same lines repeatedly. Do it once, on settled code.
**Why last:** it is a settling pass; running it earlier would refactor code that later phases still churn.
**Exit:** no remaining no-variance repeated-call patterns; every comment is a `/** … */` doc comment with NO inline comments remaining; corpus still green + VM==native.

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
| 11 | session directive (DRY) |

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

## Phase 1 — Diagnostics axis

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
| **C1.7** native panics carry line:col | `rt`, `cg` (+ `.tks`) | C1.3 ✅ | ✅ DONE (OOB) — `tk_panic_oob_at(line,col)` (teko_rt) + codegen emits the index node's position; native index-OOB now prints `line:col: teko: panic: index out of bounds`, **identical to the VM**. *Deferred:* native CAST positioning (48 inline `tk_to_*` helpers — needs a global panic-loc) and DIV0 (codegen guard unwired, B3b) — a later "native panic positioning" pass. |
| **C1.9** native stack traces (+ `.tsym` per-frame later) | `rt` (+`.tks` note) | C1-POS | ✅ DONE (core) — teko_rt prints a `backtrace` on panic + installs a fatal-signal handler (SIGSEGV/BUS/ILL/FPE) for generated programs; a native crash/panic shows the Teko call stack (frames `g`, `f`, …). main.c's own handler still wins in the bootstrap. *Deferred:* per-frame Teko `file:line` via `.tsym` emission (Eixo E3) — backtrace_symbols gives C symbols now; some frames mis-symbolize at +0 without it. |

**Revised rounds:** R1.1 `{C1.2, C1.3}` ✅ → **R1.2 `{C1-POS, C1.7}`** (C1-POS dominates; C1.7 parallel on `rt`) → R1.3 `{C1.6, C1.8, C1.9}` (w3, after C1-POS).

## Phase 2 — `in` operator

| Crumb | Owns | Dep |
|-------|------|-----|
| **C2.1** lexer `in` keyword/token | `L` | — |
| **C2.2** membership AST node + parse | `ast`, `P` | C2.1 |
| **C2.3** checker: `x in [..]` → bool (value & options comparable) | `chk` | C2.2 |
| **C2.4** codegen single-eval lowering `({T _v=x; _v==a||_v==b;})` | `cg` | C2.2 |
| **C2.5** VM eval | `vm` | C2.2 |
| **C2.6** `.tkb` tag serialize/deserialize | `tkb` | C2.2 |

**Rounds:** R2.1 `{C2.1}` (w1) → R2.2 `{C2.2}` (w1) → R2.3 `{C2.3, C2.4, C2.5, C2.6}` (w4). ✅ **DONE** (workflow `p2-in-operator`: frontend then parallel checker/codegen/vm/tkb + adversarial review). `x in [a,b,…]`→bool, single-eval LHS (GNU stmt-expr), comparison precedence, special `[…]` membership-set (no array literal). VM==native: `3 in [1,2,3]`→true, `9 in […]`/`x in []`→false, `b'e' in [b'e',b'E']`→true. **3 reviewer fixes applied:** (1) self-host regression — `in` added to `is_name_at` (cursor) AND pattern bind-names (parse_pattern) so the corpus's `in` identifiers/bindings still parse (the `to` precedent); (2) empty-set VM==native — codegen now evaluates the LHS once for `[]` too; (3) `revalidate` gained the `TK_TEXPR_IN`/`TInExpr` case (-Wswitch + non-exhaustive `.tks` match fixed). Build clean; regressions 5/6; self-host wall restored to `lexer.tks:461` (CHECK).

## Phase 3 — str/byte stdlib as real mirrored fns

| Crumb | Owns | Dep |
|-------|------|-----|
| **C3.1** real C impls (text.h) + teko_rt twins for slice/str/str_of_bytes/one_byte/str_concat[3]/i64_to_str/u64_to_str/ftoa | `rt` | — |
| **C3.4** checker signature reconcile + mirror (scope/typer) | `scope`, `chk` | — |
| **C3.2** codegen lowering of each builtin → runtime call | `cg` | C3.1 |
| **C3.3** VM execution of each builtin | `vm` | C3.1 |

**Rounds:** R3.1 `{C3.1, C3.4}` (w2) → R3.2 `{C3.2, C3.3}` (w2). ✅ **DONE** (workflow `p3-stdlib`): wired the 4 missing builtins — `str_of_bytes`/`str` (`[]byte`→str), `one_byte` (byte→str), `str_concat3`, `ftoa` (f64→str) — runtime (teko_rt) + codegen + VM + mirrors. **Reviewer fix:** `str`/`str_of_bytes` codegen bridges the `[]byte` value (which lowers to the struct `tk_slice_byte`, a DISTINCT C type from `tk_str`) to a `tk_str` at the call site (single-eval) before `tk_str_of_bytes` copies it — was a latent cc error. Verified VM==native: `str_concat3("ab","cd","ef").len`=6, `ftoa(1.5).len`=3, `one_byte`/1-elem `str_of_bytes`=1. Build clean; regressions 5/6; parses all 64. **⚠ Surfaced a PRE-EXISTING bug (task #57, slice value-layer): `teko::list::push` does NOT accumulate past one element** (`push(push(empty,a),b).len`==1, should be 2) in BOTH VM and native — a major Phase-6/self-host blocker (the corpus builds strings via `push` everywhere). NOT a Phase-3 regression (Phase 3 didn't touch `emit_list_push`/VM push).

## Phase 4 — C↔.tks mirroring audit *(read-mostly; partitioned by directory → fully parallel)*

| Crumb | Owns | Dep |
|-------|------|-----|
| **C4.1** audit + reconcile lexer + parser pairs | `L`, `ast`, `P` | — |
| **C4.2** audit + reconcile checker pairs | `chk`, `match`, `collect`, `res`, `scope`, `tast` | — |
| **C4.3** audit + reconcile codegen + vm pairs | `cg`, `vm` | — |
| **C4.4** audit + reconcile emit + runtime + build + assert + main | `tkb`, `rt`, `build`, `assert`, `main` | — |

**Rounds:** R4.1 `{C4.1, C4.2, C4.3, C4.4}` (w4).

## Phase 5 — Definite-assignment / init analysis *(single subsystem → mostly serial)*

| Crumb | Owns | Dep |
|-------|------|-----|
| **C5.1** definite-assignment pass (mandatory init, use-before-init error) | `chk` (new flow module) | C1.1 (positions) |
| **C5.2** unused-local error / unused-private warning → warnings channel | same flow module | C5.1, C1.3 |

**Rounds:** R5.1 `{C5.1}` (w1) → R5.2 `{C5.2}` (w1).

## Phase 6 — Finish self-host → working `teko .` *(milestone; `cg`/`vm` serialize across rounds)*

| Crumb | Owns | Dep |
|-------|------|-----|
| **C6.1** function parameters in codegen | `cg` | — |
| **C6.2** function parameters in VM | `vm` | — |
| **C6.3** slice `[]T as x` pattern — parser | `P` (+pattern.h) | — |
| **C6.10** #41 value-level cross-ns enforce (calls + enum paths) | `res` | — |
| **C6.4** slice pattern checker + `[]T \| error` return | `match`, `chk` | C6.3 |
| **C6.5** slice pattern codegen (variant-member) | `cg` | C6.3 |
| **C6.6** slice pattern VM | `vm` | C6.3 |
| **C6.7** copy-append collections codegen | `cg` | C6.5 |
| **C6.8** copy-append collections VM | `vm` | C6.6 |
| **C6.11** #19 X5 justification-header sweep (headers only) | *(all headers — solo)* | all above |

**Rounds:** R6.1 `{C6.1, C6.2, C6.3, C6.10}` (w4) → R6.2 `{C6.4, C6.5, C6.6}` (w3) → R6.3 `{C6.7, C6.8}` (w2) → R6.4 `{C6.11}` (w1) → **attempt `teko .`**.

**Self-host wall (2026-06-26): `lexer.tks:461` = TASK #41 (cross-namespace call resolution).** The C1.8 message reads `argument type mismatch — expected Reader, found str` at `read_str(source, pos)`. Root cause: `type_call` (expr.c) resolves a call by BARE name in the flat env (`tk_env_lookup(env, name)`), with no namespace scoping — so `teko::lexer::read_str(str,u64)` binds to `teko::emit::read_str(Reader,…)` (tkh.tks). There are **12 such cross-ns function-name collisions** in the corpus (read_str, define, is_bool/float/integer/numeric, one_byte, str_of_bytes, prim_width, run, cast_may_lose, write_u64), so a rename is whack-a-mole — the fix is **#41**: a namespace-tagged function table (like the type table) + resolve a bare call in the CALLER's namespace (thread `current_ns` into `type_call`); qualified `ns::name` resolves the named ns + visibility. Moderate core-checker refactor + `.tks` mirror. This is the next self-host gate (before the slice-`empty()` binding half of #57, which is a codegen-time issue behind this CHECK wall).

**Self-host facts uncovered during C1-POS** (both behind the wall, both pre-existing — the typed `TExpr` already relies on them; C1-POS's `Expr` struct-wrapper just adds more instances):
- **Variant-widening in a struct-literal field is NOT yet supported by the checker.** `S { kind = A {} }` where `kind: K` and `K = variant A | B` → "field value does not match the field's declared type". The ENTIRE struct-wrapper AST (`TExpr { kind = TNumber{…} }`, and now `Expr { kind = Number{…} }`) needs this. A real Phase-6 checker feature.
- **`match` convention over a struct-wrapper is unsettled.** typer.tks now uses `match e.kind { Case }` (matching the variant field — safe under any model); codegen.tks/vm.tks use bare `match e { Case }` over the struct `TExpr` (pre-existing). Phase 6 must confirm which the checker accepts and unify (prefer `.kind` — sound under both).

## Phase 7 — Host independence *(gated by the C7.0 tribunal decision; Phase 6 done)*

> **C7.0 — LEGISLATE `extern`/FFI form (TRIBUNAL DECISION, not an agent crumb).** Blocks the FFI track (C7.1*, C7.2–C7.5, C7.17). The other tracks (test-gate, project, cleanup) do NOT wait on it.

| Crumb | Owns | Dep |
|-------|------|-----|
| **C7.1a** `extern` decl — lexer + parser + AST | `L`, `ast`, `P` | C7.0 |
| **C7.1b** `extern` checker typing/marshalling | `chk` | C7.1a |
| **C7.1c** `extern` codegen emit | `cg` | C7.1a |
| **C7.1d** `extern` VM (dlopen/libffi boundary) | `vm` | C7.1a |
| **C7.2** `teko::env::args` + `teko::exit` | *(new ns files)* | C7.1b–d |
| **C7.3** `teko::io` slurp (read/write/write_err) | *(new ns files)* | C7.1b–d |
| **C7.4** `teko::fs` `list_dir` | *(new ns files)* | C7.1b–d |
| **C7.5** `teko::process` exec (invoke `cc`) | *(new ns files)* | C7.1b–d |
| **C7.6** D2 `#test` runner in VM (`teko test`) | `vm`, *(runner)*, `main` | — |
| **C7.7** D3 per-line coverage in VM | `vm` | C7.6 |
| **C7.8** D4 pre-emit gate (tests+coverage before codegen) | `main`, `build` | C7.7 |
| **C7.9** A4 main-rule from manifest artifact | `build` | — |
| **C7.10** A6 packages + pre-linker (.tkh/.tkb merge pre-codegen) | `tkb`, `build`, `res` | C7.16 |
| **C7.11** A7 output directory + `-o <dir>` *(decision then impl)* | `main` | — |
| **C7.12** A8 `teko pack` → .tkh+.tkb *(decision then impl)* | `build`, `tkb` | C7.16 |
| **C7.13** B0d promote `tk_str_eq`→text.h (+panic wrapper) | `rt`(text.h), `chk` | — |
| **C7.14** B3a `TK_LIST` runtime list | `rt` | — |
| **C7.15** B3b overflow-debug panic guard wiring | `cg` | — |
| **C7.16** `.tkb` statement/program codec (today only `TExpr`) | `tkb` | — |
| **C7.17** M2 `driver.tks` materialized | *(driver)* | C7.2–C7.5 |

**Rounds:**
- R7.1 `{C7.1a, C7.6, C7.9, C7.13, C7.14, C7.16}` (w6) — FFI-front + test-runner + project + cleanup tracks in parallel (all disjoint).
- R7.2 `{C7.1b, C7.1c, C7.1d}` (w3) — extern across checker/codegen/vm.
- R7.3 `{C7.2, C7.3, C7.4, C7.5, C7.7, C7.15}` (w6) — host surfaces (new files) + vm coverage + codegen overflow guard.
- R7.4 `{C7.8, C7.10, C7.11, C7.12, C7.17}` (w5) — gate + packages + output/pack + driver.

## Phase 8 — FLAGS

| Crumb | Owns | Dep |
|-------|------|-----|
| **C8.1** `flags` keyword | `L` | — |
| **C8.2** flags decl + AST | `ast`, `P` | C8.1 |
| **C8.3** checker: power-of-2 auto-assign + u128 size guard + bitwise typing + helper resolve | `chk`, `collect`, `res` | C8.2 |
| **C8.4** codegen: emit u128-fitting uint + bitwise (no shift) + helpers | `cg` | C8.2 |
| **C8.5** VM: flags values + bitwise + helpers | `vm` | C8.2 |
| **C8.6** `.tkb` serialize flags | `tkb` | C8.2 |

**Rounds:** R8.1 `{C8.1}` (w1) → R8.2 `{C8.2}` (w1) → R8.3 `{C8.3, C8.4, C8.5, C8.6}` (w4).

## Phase 9 — SEC

| Crumb | Owns | Dep |
|-------|------|-----|
| **C9.1** SAST gate (CI checks) | *(CI/build scripts)* | — |
| **C9.2** capability/sandbox audit of `exp`/`extern`/syscall surface | `res`/`scope` + report | — |

**Rounds:** R9.1 `{C9.1, C9.2}` (w2).

## Phase 10 — Evolution S1–S9 *(POST-SELF-HOST; each S-stage is its own sub-project — detailed crumbs are drafted AT stage start against real data, M.4)*

Stage-level dependency waves (the hard-ordering invariant). Generics (S4) runs parallel to arenas (S1–S3).

| Stage-crumb | Dep |
|-------------|-----|
| **C10.S1** arena primitive + root region | S0 seam (done) |
| **C10.S4** generics (unconstrained, monomorphization) | — |
| **C10.S2 ★** scope regions + escape check *(tribunal ratifies one-depth-compare)* | S1 |
| **C10.S3** `ref` (mutable-target only, R2) | S2 |
| **C10.S5** DI lifetimes → arenas | S2 (+S3) |
| **C10.S6** constraints (positive; `!` only prims/sealed) | S4 + real Map need |
| **C10.S7** dynamic collections (append, Map, Set; out-region param) | S4, S6, S2/S3 |
| **C10.S8** concurrency capstone (scope/spawn/channel/send/recv) | S2, S3 |
| **C10.S9** LTS cleanup (Parsed×14→Result<T>; unify parse/to_string) | S4 |

**Waves:** W10.A `{S1, S4}` → W10.B `{S2 ★}` → W10.C `{S3, S5}` → W10.D `{S6 → S7}` → W10.E `{S8, S9}`.
*(Keep deferred ceilings deferred: borrow-solver, region-poly, async, M:N. Implicit-copy-on-escape micro-decision → tribunal.)*

## Phase 11 — DRY sweep + comment hygiene *(LAST; read-mostly; partitioned by directory → fully parallel)*

Each crumb does BOTH on its partition: (a) DRY (kill no-variance repeated calls), and (b) **comment hygiene** — convert every retained comment to a `/** … */` doc comment and DROP all inline `//` / mid-code `/* */` comments (hoist/rename so the line reads without one, or fold the rationale into the preceding doc comment). Partitioned by directory → fully parallel; do (a) then (b) per file so comments settle on the already-DRY'd code.

| Crumb | Owns | Dep |
|-------|------|-----|
| **C11.1** DRY + comment-hygiene lexer + parser | `L`, `ast`, `P` | — |
| **C11.2** DRY + comment-hygiene checker | `chk`, `match`, `collect`, `res`, `scope`, `tast` | — |
| **C11.3** DRY + comment-hygiene codegen + vm | `cg`, `vm` | — |
| **C11.4** DRY + comment-hygiene emit + runtime + build + assert + main | `tkb`, `rt`, `build`, `assert`, `main` | — |

**Rounds:** R11.1 `{C11.1, C11.2, C11.3, C11.4}` (w4). Exit: no no-variance repeats; every comment is `/** … */`; zero inline comments; corpus green + VM==native.

---

## Round-by-round agent matrix (max concurrency width)

| Phase | Rounds → width | Peak agents | Notes |
|-------|----------------|-------------|-------|
| 1 Diagnostics | R1.1=2 ✅, R1.2=2, R1.3=3 | **3** | C1-POS (R1.2) is a serial foundation — exact-fields + Expr reshape; width resumes at R1.3 |
| 2 `in` | R2.1=1, R2.2=1, R2.3=4 | **4** | fan-out after the AST node lands |
| 3 stdlib | R3.1=2, R3.2=2 | **2** | runtime-first, then cg/vm |
| 4 mirror audit | R4.1=4 | **4** | one agent per directory cluster |
| 5 init-analysis | R5.1=1, R5.2=1 | **1** | single subsystem, serial |
| 6 self-host | R6.1=4, R6.2=3, R6.3=2, R6.4=1 | **4** | `cg`/`vm` serialize R6.1→R6.2→R6.3 |
| 7 host-independence | R7.1=6, R7.2=3, R7.3=6, R7.4=5 | **6** | widest phase; 4 independent tracks |
| 8 FLAGS | R8.1=1, R8.2=1, R8.3=4 | **4** | fan-out after the decl AST lands |
| 9 SEC | R9.1=2 | **2** | CI + audit in parallel |
| 10 evolution | 5 stage-waves (≤2 each) | **2** | per-stage crumbs expand at stage start |
| 11 DRY | R11.1=4 | **4** | one agent per directory cluster |

**Bottlenecks to respect:** `cg` and `vm` are single-owner files — they cap real concurrency whenever a
phase touches the backends (1, 6, 7, 8). New-file work (host surfaces, `diag`, `tsym`, namespace stdlib)
and per-directory audits/sweeps are where width is cheapest. **Cross-phase parallel option:** Phase 2
(`in`), Phase 3 (stdlib), and Phase 5 (init-analysis) are mutually independent and *could* run as one
combined fan-out if agent budget allows — but Phase 1 must land first (everything reports through it) and
Phase 6 must not start until 1–5 are integrated (it builds on settled front-end + diagnostics).
