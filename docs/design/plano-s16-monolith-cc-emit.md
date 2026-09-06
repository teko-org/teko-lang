# KEYSTONE §16 — Monolith C emission of `#os`/`#arch`-guarded values

Status: architecture plan (design-only). Author: architect. Date: 2026-08-17.
Scope: fix the root cause behind `bootstrap/teko.c` being a SINGLE-arch seed while the
language contract requires ONE monolith source that cross-compiles on every (arch, os).

---

## 1. The law being satisfied

Owner (PT-BR): *"o emit de teko.c não está carregando todas as arquiteturas e SOs em si...
para emitir C, precisa emitir na íntegra. Tem que lembrar que teko é um monólito e precisa
ser capaz de crosscompilar. Somente quando vier native espero um executável por arquitetura
e por sistema, mas que ainda faça crosscompiling."*

Two backends, two contracts:

- **C backend (`TEKO_BACKEND=c`, the seed):** ONE `bootstrap/teko.c` compiled by `cc` on every
  host. It must contain EVERY target's target-varying values, C-preprocessor-gated, so the one
  file cross-compiles everywhere. "Emit na íntegra."
- **Native backend (future):** one executable per (arch, os); it still cross-compiles by
  *selecting* a target triple, so it may collapse to the single selected target.

Standing law: NO WORKAROUNDS — fix the root cause.

## 2. The verified bug (root cause)

- `src/sys/sys.tks` declares target-varying syscall NUMBERS as `#os`/`#arch`-guarded `const`s
  (e.g. `SYS_MUNMAP = 11` under `#os("linux") #arch("x86_64")`, `= 215` under `#arch("arm64")`).
- `src/build/project.tks:482` `frontend_check` runs `prune_cc(...)` (`src/build/prune.tks`)
  BEFORE the checker. On an x86_64 host it keeps only the `x86_64` arm and DROPS the `arm64` arm.
- Scalar module consts are then INLINED at every use and dropped: `src/checker/consteval.tks`
  `inline_consts` → `inline_rw_var` substitutes each `TVar` reference with the const's literal
  initializer; `src/codegen/codegen.tks:14113` and `:14212` emit NOTHING for a `TConstDecl`.
- Net effect verified in the committed seed (`bootstrap/teko.c` ~line 326052):
  `tk_syscall2(((int64_t)11ULL), ...)` — the x86_64 number, hard-baked, NO `#if`, NO `215`.
  The x86_64-emitted seed cannot run correctly on arm64.
- The syscall INSTRUCTION half already emits proper C-`#if`-gated per-arch text
  (`cg_emit_syscall_helpers`, codegen.tks:13897, with `#if defined(__linux__) && (defined(__x86_64__)...)`
  `#elif ... aarch64 ... svc #0 ... #else <stub> #endif`). The syscall NUMBERS are the missing companion.
- Silent-prune hazard: `pred_axis_eq` (prune.tks:52) compares `env.arch == e.value` raw. A
  misspelled guard value (the in-flight `#arch("aarch64")` bug — never equals canonical `arm64`)
  prunes SILENTLY to nothing, with no diagnostic. That is why the class passed all x86_64-local
  validations. The canonical spellings are `arm64`/`x86_64`/`linux`/`macos`/`windows`
  (`src/runtime/teko_rt.c` `tk_rt_arch`; `target_arch`/`target_os`, project.tks:106/124).

Corpus fact (grep `^#(os|arch)\(` over `src/`): the ONLY declarations carrying `#os`/`#arch`
guards anywhere in the corpus are the `teko::sys` **consts**. No fn or type is target-guarded.
This is decisive for scope (see §4).

## 3. Recommendation

**Adopt mechanism (A), "target-conditional const", in its lightest faithful form (call it A′):**

- A′ carries the item's prune predicate onto the typed const node (`checker::TConstDecl` gains a
  `guard: parser::Pred` field) rather than minting a brand-new `@TItem()` variant.
- A same-named family of consts under **pairwise-disjoint target guards** (≥2 arms) is a
  *target-conditional const*: it is **symbol-backed** (not folded), and the C backend emits a
  `#if` ladder of `#define`s plus an `#else` stub, lowering every reference to the SYMBOL.
- A lone guarded const (one arm, e.g. `PROT_READ` under `#os("linux")`) is NOT conditional: its
  single value is correct wherever its guard holds and dead-but-harmless where it does not, so it
  keeps today's inline-fold behavior. This confines the blast radius to exactly the divergent
  families (`SYS_*` over arch; `CLOCK_*` over os).

### Why A over B

- **Generality / "emit na íntegra".** B (syscall-number codegen intrinsic) is syscall-specific.
  §16 will keep adding os/arch-varying VALUES that are not syscall numbers: `PROT_*`/`MAP_*`
  (already present), `errno` values, `struct` field layouts/offsets, `O_*`/`SEEK_*`. A single
  uniform mechanism over `const` is the monolith-faithful answer; B would need re-inventing per
  family. B is rejected.
- **Fidelity.** A′ makes the const the unit of per-target divergence, exactly mirroring the
  existing syscall-helper precedent (per-arch C-`#if` text with an `#else` stub). Same C shape,
  same guard→macro mapping, now for the *numbers* as well as the *instruction*.

### Why A′ (guard-on-`TConstDecl`) over the heavyweight A (new `TCondConst` `@TItem()` node)

- A new `@TItem()` variant taxes EVERY exhaustive match (tast union, `codegen` pass-1 + main,
  `lir/lower`, `emit/tkb_{read,write}` + `header`, `checker/{metrics,warnings,consteval,comptime_*}`).
- A′ reuses the existing `TConstDecl` plumbing (already handled — as a no-op or rodata — in all
  those sites). The only *new* behavior lives in three seams: consteval (exclude from the fold
  map), prune (keep the arms), codegen (emit the ladder + lower the reference).
- Cross-module `.tkb` export of a target-conditional const is **not needed**: the compiler build
  MERGES all `src/*.tks` into ONE program (`assemble_sel`, project.tks:462), so `teko::sys` and
  its consumer `teko::arena` are the same program — no `.tkb` boundary. The `.tkb` writer therefore
  takes an **honest-stop** (error) if it ever meets a non-`PTrue` target-guarded const, and the
  `.tkb` reader defaults the guard to `pred_true()`. No `Pred` serialization is added. (When a real
  dependency package needs to export target-conditional consts, that is a future, REPORTED issue.)
- The NATIVE backend prunes to the single selected target (see §6), so it NEVER forms a
  target-conditional const — `lir/lower` and the native path are untouched.

### Fold in the loud-parse-validation hardening: YES

Make an unrecognized `#os`/`#arch`/`#if(axis==…)` value a LOUD parse-time error with `line:col` +
"did you mean" for known aliases. It is independent, low-risk, and it is the guardrail that would
have caught the original `aarch64` typo. It lands FIRST (crumb 0) so the rest of the work cannot
silently regress.

## 4. Scope (KISS / YAGNI)

- **Consts only.** No fn/type is target-guarded anywhere in the corpus; do NOT build machinery for
  guarded fns/types. If one ever appears, it is a separate issue (REPORTED up, not invented here).
- **Multi-arm only is symbol-backed.** Single-arm guarded consts keep inline-fold.
- **Target axes only.** A target-conditional const's arms may differ only on `os`/`arch`. A
  `#if`-flag-bearing guard on such a const is an honest-stop in `guard_to_c_cond` (YAGNI: no such
  const exists; flags are build-config, resolved by prune, not C-preprocessor gated).
- **No `.tkb` crossing** (honest-stop), **no native changes**, **no new `@TItem()` node**.

## 5. Key definitions the crumbs share

- **target guard**: a `parser::Pred` whose atoms are all `PEq` on axis `"os"` or `"arch"`
  (composed with `PAnd`/`POr`/`PNot`); no `PFlag`; not `PTrue`.
- **target-conditional const**: a `(namespace, name)` for which ≥2 `TConstDecl`s exist whose guards
  are target guards and are **pairwise disjoint** (no target satisfies two arms at once).
- **guard→C condition** (`guard_to_c_cond`), mirroring `cg_emit_syscall_helpers` (codegen.tks:13899):
  - `PEq os=="linux"` → `defined(__linux__)`
  - `PEq os=="macos"` → `defined(__APPLE__)`
  - `PEq os=="windows"` → `defined(_WIN32)`
  - `PEq arch=="x86_64"` → `(defined(__x86_64__) || defined(__amd64__))`
  - `PEq arch=="arm64"` → `(defined(__aarch64__) || defined(__arm64__))`
  - `PAnd` → `((L) && (R))`, `POr` → `((L) || (R))`, `PNot` → `(!(X))`
  - `PTrue` → `1` (total; unreachable for a conditional const)
  - `PFlag` / unknown → honest-stop `error`
- **symbol name** (`cond_const_c_symbol`): reuse codegen's existing global-name mangling over
  `(namespace, name)`, prefixed `tk_const_` (e.g. `tk_const_teko__sys__SYS_MUNMAP`). The
  implementer wires this to the same mangler codegen already uses for module globals.
- **`#else` stub**: every ladder ends with `#else\n#define <sym> 0 /* dead on this target */\n#endif`
  so the monolith COMPILES on every host even where no arm applies (macOS/Windows today), exactly as
  the syscall-helper stub does. The value is dead — the syscall stub returns `-ENOSYS`.

## 6. Prune backend-awareness (the switch-over)

`src/build/prune.tks`:

```
/**
 * PruneMode — which backend the prune serves, deciding whether target-varying arms are kept.
 *
 * @since §16
 */
pub type PruneMode = struct {
    /** true = C-monolith backend (keep target-conditional const arms for a `#if` ladder);
        false = native single-target backend (collapse to the one selected arm). */
    keep_target_arms: bool
}
```

- `prune_cc(program, env, mode)` gains `mode: PruneMode`.
- **Native (`keep_target_arms=false`)**: byte-identical to today — full `eval_pred(item.guard, env)`;
  the one matching arm survives; folds and cross-compiles by choosing the triple.
- **C monolith (`keep_target_arms=true`)**: an item that is a `const` whose guard is a *target guard*
  is KEPT regardless of `os`/`arch` (its arm decision is deferred to `cc`'s preprocessor); every
  other item (and every flag atom, via `eval_pred`) is pruned exactly as before. Concretely a new
  `cc_keep_item(it, env, mode)` guards the loop instead of the bare `eval_pred`:

```
/**
 * cc_keep_item — the per-item keep test the prune applies. In native mode this is exactly
 * `eval_pred(it.guard, env)`. In C-monolith mode a target-guarded CONST is kept unconditionally
 * (its arm is chosen later by the emitted `#if` ladder), while every other item — and every
 * build-flag atom — is still resolved by `eval_pred`.
 *
 * @param it    the pre-check item
 * @param env   the build-time constant environment
 * @param mode  the backend's prune mode
 * @return      true iff `it` survives the prune on this backend
 * @since §16
 */
fn cc_keep_item(it: parser::Item, env: CcEnv, mode: PruneMode): bool
```

- `src/build/project.tks:482` `frontend_check` selects the mode from the resolved backend
  (`TEKO_BACKEND`): `PruneMode { keep_target_arms = backend_is_c(pf.manifest) }`. A single small
  reader `backend_is_c` (env/manifest) is added; default = C (the seed path).

This is the L0→L1 switch-over: until this crumb, prune still drops arms and the whole §5/§7
machinery is dormant (compiles, fixpoint byte-stable). Flipping it activates the fix.

## 7. Checker collision + consteval exclusion

Keeping both arms means the checker sees two `TConstDecl`s named `SYS_MUNMAP`.

- **Binding.** `define_const` (scope.tks:233) merely pushes a `ValBinding`; `lookup_binding`
  resolves the largest-position match. Both arms share the same declared type (`i64`), so a
  reference type-checks identically whichever arm wins the lookup. No fatal collision at binding.
- **Duplicate diagnostic.** Locate any const redeclaration diagnostic on the collect/typer path
  (the type-level analogue is `check_no_duplicate_types`, collect.tks:2325; the const analogue, if
  present, is the seam to touch). EXEMPT a same-`(ns,name)` pair whose guards are pairwise-disjoint
  target guards — they are legitimately distinct arms, not a redeclaration.
- **Const dependency order.** `const_dep_order` (consteval_order.tks) keys by `(ns,name)`; duplicates
  would collide. AVOID the collision by EXCLUDING target-conditional-const names from the const set
  fed to it (next bullet), so `const_dep_order` never sees the duplicates.
- **Fold exclusion.** In `inline_consts` (consteval.tks:532) the shared helper set (§5) removes
  target-conditional-const names from `collect_module_consts`/`build_scalar_map`, so:
  - their `TVar` references are NOT inlined (they survive as `TVar` for codegen to lower), and
  - their `TConstDecl` items are NOT dropped (they survive to codegen as the ladder source).
  Everything else (single-arm guarded consts, unguarded consts, aggregates) is unchanged.

## 8. Ordered crumb sequence

Every crumb is compiler-touching (it lives in `src/`). Per-crumb gate = `TEKO_BACKEND=c` build of
gen1 COMPILES + the fixpoint property `gen2 == gen3` holds (validation is COMPILE-only + fixpoint;
tests run in CI only — `teko test .` OOMs locally; use `ulimit -v 6291456`). Crumbs 1–5 are
DORMANT scaffolding: they compile and keep the fixpoint trivially because no target-conditional
const flows until the crumb-6 switch. Reseed of `bootstrap/teko.c` happens at the two RITUAL points.

### Crumb 0 — Loud parse-time validation of `#os`/`#arch`/axis values  (independent guardrail)
- Seams: `src/parser/parse_decl.tks` (the `#os`/`#arch` attribute arms, ~1642–1654) and
  `src/parser/parse_cc.tks` `parse_pred_axis_eq` (~186).
- Add a shared validator:

```
/**
 * cc_axis_value_diag — validate a `#os`/`#arch`/`#if(axis==…)` VALUE against the canonical
 * vocabulary, returning a located "did you mean" error for a misspelling (e.g. `aarch64`→`arm64`,
 * `darwin`→`macos`) and `null` for a recognized value. Prevents the SILENT prune-to-nothing that
 * hid the §16 `#arch("aarch64")` bug from every x86_64-local validation.
 *
 * @param axis   the axis being compared — `"os"` | `"arch"`
 * @param value  the quoted value the source supplied
 * @param line   the value token's 1-based line (for the located diagnostic)
 * @param col    the value token's 1-based column
 * @return       null when `value` is canonical, else a located parse error naming the nearest alias
 * @since §16
 */
fn cc_axis_value_diag(axis: str, value: str, line: u32, col: u32): null | error
```

  Canonical sets: os ∈ {linux, macos, windows}; arch ∈ {x86_64, arm64}. Alias hints:
  aarch64→arm64, amd64/x64→x86_64, darwin/apple→macos, win/win32/mingw→windows.
- Fixtures (CI negative): `#arch("aarch64") pub const X: i64 = 1` → error `unknown arch "aarch64"
  (did you mean "arm64"?)` at the value's line:col; `#os("linex")` → suggests `linux`. Expected
  native exit code of the compiler on these inputs: non-zero (compile failure), stderr contains the
  message.
- Reseed: yes, at **RITUAL A** (below). Fixpoint holds (valid programs parse identically).

### Crumb 1 — `TConstDecl` carries its prune guard
- Seams: `src/checker/tast.tks:302` add `guard: parser::Pred` to `TConstDecl`; set it at every
  construction site (typer.tks:9584, 9626, 9658 from the enclosing `parser::Item.guard`;
  collect.tks:3016 dep path and any member-const hoist use `parser::pred_true()`).
- `.tkb` codecs (`emit/tkb_write.tks:596` `write_tconstdecl`, `emit/tkb_read.tks:923`
  `read_tconstdecl`): the WRITER honest-stops if `c.guard` is a non-`PTrue` target guard
  (`error { message = "a target-conditional const cannot cross a module boundary yet (§16)" }`);
  otherwise writes as today (no `Pred` bytes added). The READER sets `guard = parser::pred_true()`.
- Dormant: nothing reads `guard` yet. Fixpoint byte-stable.

### Crumb 2 — Shared target-guard predicates (pure, unused yet)
- Seam: `src/checker/consteval.tks` (co-located with the fold, both consteval and codegen reference
  it via `checker::`). Add, with unique tree-wide names:

```
/** pred_is_target_guard — true iff `p`'s atoms are all os/arch `PEq` (no flag) and `p` is not `PTrue`. */
fn pred_is_target_guard(p: parser::Pred): bool
/** preds_target_disjoint — true iff no (os,arch) target satisfies both `a` and `b`. */
fn preds_target_disjoint(a: parser::Pred, b: parser::Pred): bool
/** cond_const_names — the (ns,name)s with ≥2 pairwise-disjoint target-guard arms among `items`. */
fn cond_const_names(items: []checker::TConstDecl): []ConstQName
/** is_cond_const — membership of `(ns,name)` in a precomputed `cond_const_names` set. */
fn is_cond_const(ns: str, name: str, set: []ConstQName): bool
```
  `preds_target_disjoint` is decidable over the finite {os}×{arch} grid; a simple satisfiability
  scan over the canonical vocabulary is sufficient and KISS.
- Dormant. Fixpoint byte-stable.

### Crumb 3 — consteval excludes target-conditional consts from the fold
- Seam: `src/checker/consteval.tks:532` `inline_consts` → `collect_module_consts` (78) and
  `build_scalar_map` (103): filter out `is_cond_const` names so their references are NOT inlined and
  their `TConstDecl`s are NOT dropped. `const_dep_order` never sees the duplicates.
- Dormant (no cond const flows until crumb 6). Fixpoint byte-stable.

### Crumb 4 — checker tolerates disjoint-guard same-name const arms
- Seam: the const redeclaration diagnostic on the collect/typer path (analogue of
  `check_no_duplicate_types`, collect.tks:2325). EXEMPT a same-`(ns,name)` pair whose guards are
  pairwise-disjoint target guards (via `preds_target_disjoint`).
- Dormant (prune still drops arms; no duplicate reaches the checker yet). Fixpoint byte-stable.

### Crumb 5 — codegen: `#if` ladder + reference lowering (dead code until crumb 6)
- Seams: `src/codegen/codegen.tks:14113` (`TConstDecl` arm in the item pass) and the `TVar`
  emission path; add near `cg_emit_syscall_helpers` (13897):

```
/**
 * guard_to_c_cond — render a target `Pred` as a C-preprocessor condition (§5 mapping), mirroring
 * the per-arch macros `cg_emit_syscall_helpers` already emits. Honest-stops on a `PFlag`/unknown
 * atom (a target-conditional const may not depend on a build flag).
 *
 * @param p  the arm's target guard
 * @return   the `#if`/`#elif` condition text, or an error for a non-target atom
 * @since §16
 */
fn guard_to_c_cond(p: parser::Pred): str | error

/**
 * cg_emit_cond_const_ladder — emit, ONCE per target-conditional-const family, the
 * `#if <cond0>\n#define <sym> <v0>\n#elif <cond1>\n#define <sym> <v1>\n…\n#else\n#define <sym> 0
 * /* dead on this target *​/\n#endif` block. Deduplicated by `(ns,name)` so the family's N arms
 * yield ONE ladder. Reuses `cond_const_c_symbol` for `<sym>` and `guard_to_c_cond` for each `<cond>`.
 *
 * @param buf   the emission buffer
 * @param prog  the codegen program (its `TConstDecl` items are the arm source)
 * @return      `buf` with every family's ladder appended, or an error
 * @since §16
 */
fn cg_emit_cond_const_ladder(buf: []byte, prog: CgProg): []byte | error
```

  Placement: emit all ladders in the preamble region (alongside/after the syscall helpers) so the
  `#define`s precede every reference. The `TConstDecl` item-pass arm stays a no-op (the ladder owns
  emission). The `TVar` path gains: if `(ns,name)` is a cond const, emit `cond_const_c_symbol(...)`
  (codegen wraps it with the expr's cast exactly as it wrapped `((int64_t)11ULL)`).
- Dead code until crumb 6 (no `TVar`-to-cond-const survives yet, no cond const item exists yet).
  Fixpoint byte-stable.

### Crumb 6 — SWITCH-OVER: prune backend-awareness (§6)  ← activates the fix
- Seams: `src/build/prune.tks` (`PruneMode`, `cc_keep_item`, `prune_cc` signature) and
  `src/build/project.tks:482` `frontend_check` (`backend_is_c` → `PruneMode`). All callers of
  `prune_cc` pass a mode; every non-`frontend_check` caller uses native mode (single-target).
- Now the two `SYS_*` arms flow through the checker (tolerated, crumb 4), survive consteval
  (crumb 3), and codegen emits the ladder + symbol reference (crumb 5). `bootstrap/teko.c` changes:
  the bare `((int64_t)11ULL)` becomes `((int64_t)tk_const_teko__sys__SYS_MUNMAP)` with a preceding
  `#if defined(__x86_64__…) #define … 11 #elif defined(__aarch64__…) #define … 215 #else … 0 #endif`.
- Fixtures:
  - **F-emit (compile+grep, local):** build the compiler with `TEKO_BACKEND=c`; the emitted C for a
    program that `use teko::sys` + calls `arena` mmap/munmap contains BOTH `11` and `215` for
    `SYS_MUNMAP`, gated by `#if`, and NO bare `((int64_t)11ULL)` for that use.
  - **F-arena (CI, native exit code, cross-arch matrix):** a program that mmaps one page, writes a
    sentinel, reads it back, munmaps, exits 0. Expected native exit code = 0 on BOTH
    linux/x86_64 AND linux/arm64 (the single seed compiled by each host's `cc`). A single-arch seed
    would call the wrong syscall number on arm64 → non-zero/crash.
  - **F-clock (CI, os ladder):** if/when a `CLOCK_*`-using fixture lands, its ladder is os-gated
    (`defined(__linux__)` vs `defined(__APPLE__)`), proving the mechanism generalizes past arch.
- Reseed at **RITUAL B**.

## 9. Ritual points, fixpoint, reseed

- **RITUAL A — after crumb 0.** Full gate: gen1 compiles, `gen2 == gen3`, negative parse fixtures
  pass in CI, reseed `bootstrap/teko.c`. (Crumb 0 is independent and self-contained.)
- **RITUAL B — after crumb 6.** Crumbs 1–5 accumulate as dormant scaffolding (each individually
  compiles and keeps the fixpoint), and crumb 6 activates them. Full gate: gen1 compiles;
  **fixpoint `gen2 == gen3`** (the compiler's self-emission is deterministic, so the ladder emits
  identically across generations); F-emit grep passes locally; F-arena/F-clock pass in the CI
  cross-arch matrix; then **reseed `bootstrap/teko.c`**. This is the compiler-touching reseed the
  issue anticipates.

Reseed mechanics unchanged: gen1 (built from the current seed) emits the new `teko.c`; verify
`gen2 == gen3`; commit the regenerated `bootstrap/teko.c`. The corpus uses no feature absent from
the seed (guards, consts, and codegen `#if` text all predate this work), so no seed-ordering
constraint is violated.

## 10. Risks and law tensions

- **R1 — reference lowering path for a residual module-const `TVar`.** Today NO scalar module-const
  `TVar` reaches codegen (all inlined). Crumb 5 adds the first such path; if codegen's `TVar` arm
  has no fall-through for a module const it could honest-stop. Mitigation: crumb 5 lands the `TVar`
  cond-const branch BEFORE crumb 6 makes one flow, so the path exists when first exercised.
- **R2 — `#else` stub correctness.** Without the stub, a macOS/Windows monolith fails to compile
  (undefined symbol). The stub (`#define <sym> 0 /* dead */`) mirrors the syscall-helper stub and is
  mandatory. Law tension with "emit na íntegra": resolved — the stub IS the full emission for a
  target with no written arm; when the macOS arm is authored it slots into the ladder.
- **R3 — disjointness assumption.** Two arms that are NOT disjoint (e.g. two `#os("linux")` arms of
  the same name with different values) are a genuine authoring error. `preds_target_disjoint`
  returning false must surface as a LOUD checker error (not a silent last-writer-wins), reusing the
  redeclaration diagnostic crumb 4 otherwise exempts. This closes the silent-prune class entirely.
- **R4 — `.tkb` cross-module export.** Deferred by honest-stop (crumb 1). Sound because the compiler
  build is one merged program. A future dependency needing this is REPORTED up, not solved here.
- **R5 — fixpoint churn.** The change rewrites many `teko.c` lines (every `SYS_*` use). This is
  expected and deterministic; `gen2 == gen3` still holds. No law tension.

No unresolved law tension remains; nothing HALTs. The design is ratifiable: it obeys Teko-only,
W15 (doc-comments-only, flatten/extract, unique helper names, no index-assign), KISS/YAGNI (consts
only, multi-arm only, target axes only), and arena/no-GC (no allocation model change).

## 11. Files touched (summary, all `.tks`)

- `src/parser/parse_decl.tks`, `src/parser/parse_cc.tks` — crumb 0 (loud validation).
- `src/checker/tast.tks` — crumb 1 (`TConstDecl.guard`).
- `src/checker/typer.tks`, `src/checker/collect.tks` — crumb 1 (thread guard), crumb 4 (dup exempt).
- `src/emit/tkb_write.tks`, `src/emit/tkb_read.tks` — crumb 1 (honest-stop write / default read).
- `src/checker/consteval.tks` (+ `consteval_order.tks` awareness) — crumbs 2, 3 (helpers + exclusion).
- `src/codegen/codegen.tks` — crumb 5 (ladder + reference lowering).
- `src/build/prune.tks`, `src/build/project.tks` — crumb 6 (backend-aware prune switch).
- `bootstrap/teko.c` — regenerated at RITUAL A and RITUAL B (reseed only, not hand-edited).
