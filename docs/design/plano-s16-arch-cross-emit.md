# KEYSTONE §16 — `#arch` cross-emit: one Linux `teko.c`, every Linux arch via C `#if`

Status: architecture plan (design-only). Author: architect. Date: 2026-08-17.
Scope: fix the ROOT CAUSE of the arm64 first-alloc SIGSEGV (M.1) — the C seed hard-bakes the
x86_64 syscall NUMBERS, so the one `bootstrap/teko.c` cross-compiled to arm64 calls the wrong
kernel entry (`SYS_MMAP` 9 = `lgetxattr` on aarch64).

This doc REFINES the sibling `docs/design/plano-s16-monolith-cc-emit.md` on ONE load-bearing
point (the prune becomes AXIS-AWARE: `#os` still prunes per-target, `#arch` is deferred to the C
preprocessor). It REUSES that doc's crumbs 0–5 verbatim (the target-conditional-const mechanism
A′) and REPLACES only its §6 prune switch. It COORDINATES with
`docs/design/plano-s16-sync-cross-plataforma.md` (the `#os("macos")`/`#os("windows")` consts there
are UNAFFECTED — they remain os-pruned; only the `#arch` axis of the Linux teko.c changes).

---

## 1. The law being satisfied, and the DISTINCTION the fix turns on

Monolith law (CLAUDE.md): *"Teko é um monólito e precisa cross-compilar. A perna C emite UM
`teko.c` que compila em toda arquitetura/SO via `#if` do C — tem que emitir tudo (todos os alvos),
não podar para o host."* No workarounds — fix the root cause.

The two conditional-compile axes are NOT symmetric, and conflating them is exactly what the
current prune gets wrong:

- **`#arch` MUST NOT prune.** Within ONE OS, the arch variants of a value are just DIFFERENT
  NUMBERS fed to the SAME mechanism (the raw-syscall intrinsic). The identical `teko.c` cross-
  compiles linux-x86_64 → linux-arm64 through `aarch64-linux-gnu-gcc`; the C preprocessor's
  `#if defined(__aarch64__)` picks the right number at `cc` time. The syscall INSTRUCTION half
  already ships this way (`cg_emit_syscall_helpers`, codegen.tks:13974 — a per-arch `#if`
  ladder). The syscall NUMBERS are the missing companion. → the arch arms must ALL reach the
  emitter to become one `#if` ladder.
- **`#os` MUST keep pruning per-target.** Across OSes the mechanism itself diverges: Linux uses
  raw `svc`/`syscall`; macOS uses libSystem FFI; Windows uses kernel32/ntdll FFI (§16 R1–R5). The
  symbols, the toolchains, and the FUNCTION bodies differ, so the C backend emits ONE `teko.c`
  PER OS (the committed `bootstrap/teko.c` is the LINUX seed; a macOS build emits its own). The
  os axis is resolved to the target BEFORE emission — a linux teko.c must never carry a macOS
  arm's value or symbol, or (once the sync sibling lands) a macOS FFI function body.

So the monolith law, read honestly against the syscall-vs-FFI reality §16 already ratified, is:
**one `teko.c` per OS, cross-compiling every arch of that OS.** The arm64 crash is precisely the
"every arch of that OS" half failing. That is the fix's whole surface.

## 2. The verified bug (mechanism only — cause is confirmed upstream)

- `src/sys/sys.tks` declares the syscall NUMBERS as `#os("linux") #arch("x86_64")` /
  `#arch("arm64")` const pairs (e.g. `SYS_MMAP = 9` vs `= 222`).
- `frontend_check` (project.tks:630) runs `prune_cc(program, CcEnv{os=target_os, arch=target_arch,
  …})` BEFORE the checker. `pred_axis_eq` (prune.tks:52) resolves BOTH `os` and `arch` against the
  host env, so on an x86_64 host the `arm64` arm is DROPPED.
- The surviving scalar const is then INLINED at every use and its decl dropped (consteval
  `inline_consts`; codegen `TConstDecl` arm is a no-op — codegen.tks:14276, 14375).
- Net: `bootstrap/teko.c` emits `tk_syscall6(((int64_t)9ULL), …)` — the x86_64 number hard-baked,
  NO `#if`, NO `222`. Cross-compiled to arm64, `9` is `lgetxattr` → SIGSEGV at the arena's first
  `ar_mmap`. Reproduced under `qemu-aarch64-static` (`-strace` shows `lgetxattr(NULL,0x10000,3,34)`
  — the mmap args, wrong number).

## 3. Decision — (a), axis-aware NON-prune of `#arch`, with justification

The issue frames two options:

- **(a)** Do NOT prune `#arch`; teach the emitter to group same-name arch-variants and emit the
  C `#if`. Keep the `#os` prune.
- **(b)** Keep the prune as-is, but have the emitter materialize the `#if` from all arch variants
  of a symbol.

**Recommend (a).** Justification — **(b) is unimplementable as stated:** the prune runs BEFORE
the checker and drops all-but-one arch arm; by the time the emitter runs, the other arch's value
is GONE from the program. The emitter cannot "materialize the `#if` from all arch variants" when
only one variant survived. Any faithful fix MUST let every arch arm survive the prune to reach the
emitter — that IS option (a). (b) can only be made to work by first not-pruning, at which point it
collapses into (a). So (a) it is.

The mechanism reuses the sibling doc's **A′ "target-conditional const"** wholesale (symbol-backed
const + C `#if` ladder of `#define`s, references lowered to the symbol). The ONLY thing this doc
changes relative to the sibling is the PRUNE's keep-test: it becomes **axis-aware** rather than
keep-all-target-arms.

### 3.1 Why axis-aware, not the sibling's keep-ALL-target-arms

The sibling doc's `prune_cc` C-monolith mode keeps a const "whose guard is a target guard
regardless of os/arch". On a linux host that also keeps the `#os("macos")` `CLOCK_MONOTONIC`
arm and ladders it (`#if __linux__ … #elif __APPLE__ …`) INTO the Linux teko.c. That is:

- **wrong per this issue's mandate** (`#os` must prune — the macOS value has no business in the
  Linux seed), and
- **a latent break** once `plano-s16-sync-cross-plataforma.md` adds os-guarded macOS/Windows FFI
  FUNCTIONS: keep-all-os-arms would pull macOS libSystem code into a Linux raw-syscall teko.c,
  which cannot compile. os-prune is the only sound rule for the function case, and consts must
  follow the same axis discipline.

Axis-aware keep = resolve the **os** atoms against the host (a teko.c is per-OS), DEFER the
**arch** atoms to the emitted C `#if` (one teko.c, all arches). This yields exactly the desired
behavior with zero blast radius on the os axis:

| const family              | guard shape                         | linux-host prune result        | emit           |
|---------------------------|-------------------------------------|--------------------------------|----------------|
| `SYS_*` (11 families)     | `#os(linux) #arch(x86_64|arm64)`    | BOTH arch arms survive         | `#if` arch ladder |
| `CLOCK_*`                 | `#os(linux)` vs `#os(macos)`        | only linux arm survives (1 arm)| folded literal (unchanged) |
| `PROT_*`/`MAP_*`/`CLONE_*`/`FUTEX_*` | single `#os(linux)`      | survives (1 arm)               | folded literal (unchanged) |

## 3.2 Supported (os, arch) matrix — FIXED, closed today (owner ruling 2026-08-17)

The target space is a CLOSED whitelist of EXACTLY four combinations. Everything else is
UNSUPPORTED and must be rejected — the build "nem tenta fazer" (never even starts):

| OS       | supported arch(es)   |
|----------|----------------------|
| Windows  | `x86_64` ONLY        |
| Linux    | `x86_64` AND `arm64` |
| macOS    | `arm64` ONLY (Apple Silicon) |

Valid pairs: `windows-x86_64`, `linux-x86_64`, `linux-arm64`, `macos-arm64`. Unsupported (rejected)
includes `windows-arm64`, `macos-x86_64`, `linux-riscv`, and any other combination. The matrix is
CLOSED today ("futuramente talvez, mas não agora"); the design keeps it in ONE place, trivially
extensible later.

Note the consequence for `ARCH_VOCAB` and the arch ladders: the arch axis is only ever multi-arm
on **Linux** (x86_64 + arm64). Windows is single-arch (x86_64), macOS is single-arch (arm64) — so
their per-OS teko.c never needs an arch ladder at all (the `SYS_*` families are `#os("linux")`, so
the os-prune drops them entirely on the macOS/Windows teko.c; their FFI runtime supplies the
equivalents). The arch ladder is therefore a LINUX-only shape in practice, and `cc_arch_vocab`'s
`{x86_64, arm64}` is exactly the Linux arch span.

### 3.3 Fail-fast rejection of an unsupported target (layer a)

Reject an invalid `(os, arch)` BEFORE any prune/emit/`cc` — the owner's "nem tenta fazer o build".
The single choke point is `frontend_check` (project.tks:629–630), where `target_os(pf.manifest)`
and `target_arch(pf.manifest)` are already computed and the function already returns
`Frontend | error`. Insert the validation immediately, BEFORE `prune_cc`:

```
/**
 * SUPPORTED_TARGETS — the CLOSED whitelist of `(os, arch)` the toolchain builds for (owner ruling
 * 2026-08-17): `windows-x86_64`, `linux-x86_64`, `linux-arm64`, `macos-arm64`. The SINGLE point of
 * truth — the fail-fast validator (`cc_validate_target`), `cc_arch_vocab`, and any future matrix
 * extension all read it here, so widening the matrix is a one-line edit. Closed today; do NOT
 * expand without an owner ruling.
 *
 * @return  the supported `(os, arch)` pairs, each as an `"<os>-<arch>"` key
 * @since §16
 */
fn cc_supported_targets(): []str

/**
 * cc_validate_target — FAIL-FAST rejection of an unsupported `(os, arch)` combination BEFORE the
 * build does any work (prune/emit/`cc`), per the owner ruling: a non-whitelisted target "nem tenta
 * fazer o build". Called at the top of `frontend_check`, before `prune_cc`. Returns the located
 * honest-stop naming the offending pair and the whole supported set; returns `null` for a
 * whitelisted pair. This is layer (a) of the two-layer defense — layer (b) is the ladders'
 * `#else #error` in the emitted C (§4.2).
 *
 * @param os    the resolved target OS (`target_os(manifest)`)
 * @param arch  the resolved target arch (`target_arch(manifest)`)
 * @return      null when `(os, arch)` is supported, else the honest-stop error
 * @since §16
 */
fn cc_validate_target(os: str, arch: str): null | error
```

The error message (owner-specified shape):

```
combinação SO-ARCH não suportada: <os>-<arch>; suportadas: windows-x86_64, linux-x86_64, linux-arm64, macos-arm64
```

Wiring in `frontend_check`:

```
match cc_validate_target(target_os(pf.manifest), target_arch(pf.manifest)) { error as e => return e; null => { } }
var selected = prune_cc(pf.parsed, CcEnv { … }, mode)
```

This ALSO subsumes the older per-arch/per-os "unknown" leaks: `target_arch`/`target_os` return
`"unknown"` for an unrecognized triple token (project.tks:112/129); an `"unknown"`-bearing pair is
not in the whitelist, so it is rejected here with a clear message rather than silently pruning
everything to nothing. (This is complementary to crumb 0's loud parse-time axis-value validation,
which catches a misspelled `#arch("aarch64")` in SOURCE; `cc_validate_target` catches a bad BUILD
TARGET.)

## 4. The intervention point (exact)

### 4.1 Prune — `src/build/prune.tks` (the load-bearing change)

Replace the bare `eval_pred(it.guard, env)` keep with an axis-aware test used ONLY for the C
backend. The os axis is resolved against the host; the arch axis is existentially deferred over
the finite arch vocabulary (correct under `PNot`/`POr`, unlike a naive "treat arch atom as true"
substitution).

```
/**
 * ARCH_VOCAB — the canonical arch spellings the C monolith cross-compiles across within one OS
 * (`target_arch`'s output space: `x86_64`, `arm64`). The arch-defer keep-test scans this finite
 * set; the emitted `#if` ladder has one arm per member a family declares.
 *
 * @return  the canonical arch names
 * @since §16
 */
fn cc_arch_vocab(): []str

/**
 * cc_arch_deferred_survives — the C-monolith prune keep-test for a target-guarded item: true iff
 * SOME arch in `cc_arch_vocab()` satisfies `p` once the OS axis is fixed to `env.os`. This resolves
 * the `#os` atoms against the emission host (a `teko.c` is emitted per-OS — a linux-host teko.c
 * must not carry a macos/windows arm) while DEFERRING every `#arch` atom to the emitted C `#if`
 * ladder (the one teko.c cross-compiles across every arch of its OS). The existential scan is
 * correct under `PNot`/`POr`, where a substitution of "arch atom = true" would not be.
 *
 * @param p    the item's guard predicate
 * @param env  the build-time constant environment (its `os` fixes the OS axis; its `arch` is ignored)
 * @return     true iff some arch of `env.os` satisfies `p`
 * @since §16
 */
fn cc_arch_deferred_survives(p: parser::Pred, env: CcEnv): bool

/**
 * cc_keep_item — the per-item keep test the prune applies under each backend. NATIVE mode is
 * byte-identical to today: `eval_pred(it.guard, env)` (the single target's arm survives; folds and
 * cross-compiles by choosing the triple). C-MONOLITH mode defers the arch axis: a TARGET-
 * CONDITIONAL CONST (a const whose guard is a target guard, §5) is kept iff `cc_arch_deferred_
 * survives` — so BOTH linux arch arms of `SYS_MMAP` survive while a macos arm is dropped. Every
 * OTHER item is still resolved by the full `eval_pred` (os-pruned as before). An item whose guard
 * MENTIONS `#arch` but is NOT a target-conditional const has no `#if` emitter yet and HONEST-STOPS
 * loudly (see §7 R-gen) rather than silently collapsing to the host arch.
 *
 * @param it    the pre-check item
 * @param env   the build-time constant environment
 * @param mode  the backend's prune mode
 * @return      true iff `it` survives the prune on this backend, or an error for an unemittable
 *              arch-guarded non-const item
 * @since §16
 */
fn cc_keep_item(it: parser::Item, env: CcEnv, mode: PruneMode): bool | error
```

`PruneMode`, its threading through `prune_cc(program, env, mode)`, and the `backend_is_c`
selection in `frontend_check` are AS SPECIFIED in the sibling doc §6 — the only delta is that
`cc_keep_item`'s C-monolith branch calls `cc_arch_deferred_survives` (os-resolved / arch-deferred)
instead of the sibling's "kept regardless of os/arch".

### 4.2 Emitter — `src/codegen/codegen.tks` (reused from sibling crumb 5)

`guard_to_c_cond` (sibling §5) renders the SURVIVING guard as a C-preprocessor condition. Because
the os axis is already host-fixed by the prune, the os atom in a surviving guard is a
true-constant the preprocessor folds; rendering the FULL guard is therefore both correct and
simplest, and keeps the mapping uniform with the sibling. `cg_emit_cond_const_ladder` emits, once
per `(ns,name)` family, the ladder of `#define`s; the `TVar` path lowers a reference to
`cond_const_c_symbol(ns,name)` wrapped by the same cast codegen already applied (`((int64_t)…)`).

Macro-arch mapping (IDENTICAL to `cg_emit_syscall_helpers`, codegen.tks:13976/13979 — the number
ladder and the instruction ladder MUST agree byte-for-byte on the macro spelling):

- `#arch("x86_64")` → `defined(__x86_64__) || defined(__amd64__)`
- `#arch("arm64")`  → `defined(__aarch64__) || defined(__arm64__)`
- (os atoms, folded-true on the per-OS teko.c) `#os("linux")` → `defined(__linux__)`,
  `#os("macos")` → `defined(__APPLE__)`, `#os("windows")` → `defined(_WIN32)`

**`#else` = `#error`, NOT a `0` stub, for an arch-varying const — this is DEFENSE-IN-DEPTH layer
(b).** The sibling doc ends its ladder with `#else #define <sym> 0 /* dead */` so a macOS/Windows
monolith still compiles. That rationale belongs to the OS axis, which os-prune removes from this
ladder's scope. For the ARCH axis on a given OS, an unknown/unsupported arch is a rejected build
(§3.2), and the syscall INSTRUCTION helper already `#error`s on it (codegen.tks:13982–13983). The
NUMBER ladder must MATCH — `#else #error "unsupported (os,arch) …"` — so even if some future edit
bypassed the layer-(a) fail-fast (§3.3), the emitted C still refuses to compile for an
off-whitelist arch rather than silently baking a wrong number. The two layers are redundant on
purpose (owner ruling). Concretely for `SYS_MMAP` (a `#os("linux")` family — the ladder covers the
two Linux arches only; on the macOS/Windows teko.c the family is os-pruned and never referenced):

```
#if defined(__x86_64__) || defined(__amd64__)
#define tk_const_teko__sys__SYS_MMAP 9
#elif defined(__aarch64__) || defined(__arm64__)
#define tk_const_teko__sys__SYS_MMAP 222
#else
#error "teko section 16: unsupported (os,arch) — SYS_MMAP has no syscall number for this arch (supported: linux-x86_64, linux-arm64; see docs/design/plano-s16-arch-cross-emit.md section 3.2)"
#endif
```

The `#define` body is the arm's folded value rendered by the SAME `cb_numint` path codegen uses
for any integer literal, so the emitted digits match everywhere.

## 5. Affected consts (the arch-varying `SYS_*` families — ALL of `src/sys/sys.tks`)

Eleven families, each `#os("linux")` with a two-arm `#arch` split (x86_64 / arm64), each becoming
ONE `#if` ladder; consumers in arena/thread/sync/time/io:

| const               | x86_64 | arm64 | consumer                         |
|---------------------|-------:|------:|----------------------------------|
| `SYS_MMAP`          |      9 |   222 | arena backing store (the crash)  |
| `SYS_MUNMAP`        |     11 |   215 | arena / thread-stack free        |
| `SYS_MPROTECT`      |     10 |   226 | thread-stack guard page          |
| `SYS_EXIT_GROUP`    |    231 |    94 | process exit                     |
| `SYS_EXIT`          |     60 |    93 | thread-only exit (trampoline)    |
| `SYS_WRITE`         |      1 |    64 | io                               |
| `SYS_CLOCK_GETTIME` |    228 |   113 | time                             |
| `SYS_GETRANDOM`     |    318 |   278 | rng                              |
| `SYS_FUTEX`         |    202 |    98 | sync (lock/condvar)              |
| `SYS_GETTID`        |    186 |   178 | per-thread state key             |
| `SYS_CLONE`         |     56 |   220 | thread spawn (`thread_clone`)    |

NOT affected (single-arm `#os("linux")`, no arch split → stay folded literals): `PROT_*`,
`MAP_*`, `CLONE_*` flag family, `FUTEX_*` op family. NOT affected (os axis, os-pruned): `CLOCK_
REALTIME`/`CLOCK_MONOTONIC` (linux vs macos) and every `#os("macos")`/`#os("windows")` const the
sync sibling owns.

Generalization (per the issue's mandate, KISS/YAGNI-bounded): the mechanism is over the `const`
ITEM, not over syscalls — any future `#arch`-varying VALUE (errno numbers, `O_*`/`SEEK_*`, struct
offsets) rides the same ladder for free. A future `#arch`-varying `fn`/`type` has NO `#if` emitter
today; `cc_keep_item` HONEST-STOPS on it (§7 R-gen) rather than silently emitting the host arch —
building that emitter is a separate, REPORTED issue, not invented here.

## 6. Crumb sequence (each independently gate-able)

Crumbs 0–5 are the sibling doc's, adopted verbatim (dormant scaffolding: they compile and keep the
fixpoint trivially because no arch arm flows until the crumb-6 switch). Crumb 6 is REFINED here
(axis-aware) and carries the new arm64-under-qemu gate. Per-crumb LOCAL gate (unless noted):
`TEKO_BACKEND=c` build of gen1 COMPILES (`--no-verify --release`, `ulimit -v 6291456`) + fixpoint
`gen2 == gen3` byte-identical. Tests run in CI only.

- **Crumb 0 — loud `#os`/`#arch`/axis-value validation** (sibling crumb 0). `cc_axis_value_diag`
  in `src/parser/parse_decl.tks` + `parse_cc.tks`: an unrecognized axis value is a located parse
  error with a "did you mean" hint (`aarch64`→`arm64`, `darwin`→`macos`). This is the guardrail
  that would have caught the original `#arch("aarch64")` silent-prune-to-nothing. Lands FIRST.
  Gate: local build+fixpoint; CI negative fixtures (§ below). → RITUAL A (reseed).
- **Crumb 0b — target whitelist fail-fast** (this doc, §3.2/§3.3). `cc_supported_targets`,
  `cc_validate_target` in `src/build/project.tks`; call it at the top of `frontend_check` before
  `prune_cc`. Independent guardrail; changes NO emitted C (the self-build host is `linux-x86_64`,
  whitelisted), so fixpoint holds trivially. Gate: local build+fixpoint; CI negative fixtures
  (F-target below). Rides RITUAL A (reseed unchanged — behavior differs only for invalid targets
  that never occur in the self-build). Lands with crumb 0.
- **Crumb 1 — `TConstDecl` carries its guard** (sibling crumb 1): add `guard: parser::Pred` to
  `checker::TConstDecl` (tast.tks), set at every construction site, `.tkb` writer honest-stops on
  a non-`PTrue` target guard / reader defaults `pred_true()`. Dormant. Gate: build+fixpoint.
- **Crumb 2 — shared target-guard predicates** (sibling crumb 2): `pred_is_target_guard`,
  `preds_target_disjoint`, `cond_const_names`, `is_cond_const` in `src/checker/consteval.tks`,
  plus (this doc) `cc_arch_vocab`/`cc_arch_deferred_survives` in `src/build/prune.tks`. Pure,
  unused yet. Dormant. Gate: build+fixpoint.
- **Crumb 3 — consteval excludes cond consts from the fold** (sibling crumb 3): their `TVar`s are
  not inlined and their `TConstDecl`s not dropped; `const_dep_order` never sees the duplicates.
  Dormant. Gate: build+fixpoint.
- **Crumb 4 — checker tolerates disjoint-guard same-name arms** (sibling crumb 4): exempt a
  same-`(ns,name)` pair whose guards are pairwise-disjoint target guards from the const-
  redeclaration diagnostic; a NON-disjoint pair (two `#os(linux)` same-name arms) stays a LOUD
  error (§7 R3). Dormant. Gate: build+fixpoint.
- **Crumb 5 — codegen `#if` ladder + reference lowering** (sibling crumb 5, with §4.2's `#else
  #error` for the arch ladder): `guard_to_c_cond`, `cg_emit_cond_const_ladder`,
  `cond_const_c_symbol`, and the `TVar`→symbol path. Emitted in the preamble alongside the syscall
  helpers so the `#define`s precede every reference. Dead code until crumb 6. Gate: build+fixpoint.
- **Crumb 6 — SWITCH-OVER: axis-aware prune** (REFINED). `PruneMode`, `cc_keep_item` (calling
  `cc_arch_deferred_survives`), `prune_cc` signature, and `backend_is_c` in `frontend_check`. Now
  BOTH `SYS_*` arch arms flow through the checker (tolerated, crumb 4), survive consteval (crumb
  3), and codegen emits the ladder + symbol (crumb 5). `bootstrap/teko.c` changes for EVERY target:
  the bare `((int64_t)9ULL)` becomes `((int64_t)tk_const_teko__sys__SYS_MMAP)` preceded by the
  arch `#if` ladder. Gate: full ritual B below (build + fixpoint + the qemu-arm64 gate + reseed).

## 7. Regression fixtures (inputs → expected native exit codes)

- **F0 (CI, negative, crumb 0):** `#arch("aarch64") pub const X: i64 = 1` → compiler exits
  NON-ZERO, stderr `unknown arch "aarch64" (did you mean "arm64"?)` at the value's `line:col`.
  `#os("linex") …` → suggests `linux`. Guards the silent-prune class.
- **F-target (CI, negative, crumb 0b):** building with an off-whitelist target (e.g.
  `TEKO_TARGET`/manifest triple resolving to `macos-x86_64` or `windows-arm64`, or an `"unknown"`
  arch/os token) → compiler exits NON-ZERO, stderr `combinação SO-ARCH não suportada:
  <os>-<arch>; suportadas: windows-x86_64, linux-x86_64, linux-arm64, macos-arm64`, and NO
  `teko.c` / no `cc` invocation (the abort precedes emission — "nem tenta fazer o build"). The
  four whitelisted targets each proceed past the check.
- **F-emit (LOCAL, compile+grep, crumb 6):** build the compiler with `TEKO_BACKEND=c`; the emitted
  `teko.c` for a program that `use teko::sys` + arena-mmaps contains BOTH `9` and `222` for
  `SYS_MMAP`, gated by `#if defined(__x86_64__)…`/`#elif defined(__aarch64__)…`, and NO bare
  `((int64_t)9ULL)` for that use. Confirms the ladder materialized.
- **F-arena-x86_64 (CI native, crumb 6):** a program that mmaps one page, writes a sentinel, reads
  it back, munmaps, exits 0. Native exit code **0** on linux/x86_64 (the seed compiled by the host
  `cc`).
- **F-arena-arm64 (CI native cross, crumb 6 — THE regression that reproduces M.1):** the SAME
  program, the SAME single `teko.c`, cross-compiled and run under qemu (§8). Native exit code
  **0** on linux/arm64. Before the fix this crashes (SIGSEGV) at the first `ar_mmap` because
  `SYS_MMAP` = 9 = `lgetxattr`. This fixture IS the crash's gravestone.
- **F-clock (CI, os-axis witness):** a `CLOCK_MONOTONIC`-using fixture stays os-pruned — on
  linux it folds to `1` with NO ladder (proving `#os` still prunes and the sync sibling's os
  consts are untouched).

## 8. The arm64 validation gate (per crumb 6, PROVEN method + installed tools)

After emitting the fixed `teko.c`, cross-compile and run under qemu — the crash M.1 MUST vanish:

```
aarch64-linux-gnu-gcc -std=c2x -w -O2 -static -pthread \
  -I src/runtime -I src/assert \
  teko.c src/runtime/teko_rt.c src/assert/assert.c -lm -o teko-arm64
TK_RT_DIR=$PWD/src/runtime TEKO_BACKEND=c qemu-aarch64-static \
  ./teko-arm64 . -o /tmp/x --no-verify --release
```

Expected: the arena's first `ar_mmap` now issues `SYS_MMAP` = 222 (selected by
`#if defined(__aarch64__)`) — no `lgetxattr`, no SIGSEGV; the compiler proceeds. `-strace` (if
re-run) shows `mmap(...)`, not `lgetxattr(...)`. This gate is BLOCKING for crumb 6 / RITUAL B.

## 9. Ritual points, fixpoint, reseed (COMPILER change → full reseed REQUIRED)

This changes the C EMITTED FOR EVERY TARGET (x86_64 too — its bare `((int64_t)9ULL)` becomes the
`#if`-guarded `tk_const_…` symbol). It is unambiguously a compiler-touching change, so it demands a
FULL reseed of `bootstrap/teko.c`, at two ritual points:

- **RITUAL A — after crumb 0.** Full gate: gen1 compiles; `gen2 == gen3` byte-identical; CI
  negative parse fixtures (F0) pass; PROVENANCE + provenance_gate PASS; MEM_PARANOID clean; reseed
  `bootstrap/teko.c`. (Crumb 0 is independent and self-contained.)
- **RITUAL B — after crumb 6.** Crumbs 1–5 accumulate as dormant scaffolding (each individually
  compiles and holds the fixpoint); crumb 6 activates them. Full gate:
  1. gen1 (built from the RITUAL-A seed) compiles under `TEKO_BACKEND=c`.
  2. **fixpoint `gen2 == gen3` byte-identical** — the ladder is emitted deterministically
     (dedup by `(ns,name)`, arms in source order, symbol a pure function of `(ns,name)`), so
     self-emission is stable across generations despite the many rewritten `SYS_*` lines.
  3. **F-emit** grep passes locally (both `9` and `222`, `#if`-gated).
  4. **F-arena-arm64 under qemu (§8) exits 0** — the M.1 gate. Plus F-arena-x86_64 exits 0.
  5. PROVENANCE regenerated + provenance_gate PASS; MEM_PARANOID clean.
  6. reseed `bootstrap/teko.c` (the x86_64-harvested seed, now carrying the arch ladders, is the
     one that cross-compiles to arm64 correctly).

Reseed mechanics unchanged; the corpus uses no feature absent from the current seed (guards,
consts, and the codegen `#if` text all predate this work), so no seed-ordering constraint is
violated.

## 10. Risks + law tensions (recommended resolution)

- **R1 — residual module-const `TVar` lowering.** Today no scalar module-const `TVar` reaches
  codegen (all inlined). Crumb 5 adds the first such path; land it BEFORE crumb 6 makes one flow,
  so the branch exists when first exercised. (Sibling R1.)
- **R2 — `#else` policy divergence from the sibling.** This doc uses `#else #error` for the ARCH
  ladder (vs the sibling's `#define 0` stub). Resolution: the stub's reason (a macOS/Windows
  monolith must still compile) is an OS-axis concern that os-prune removes from this ladder; the
  arch axis mirrors the syscall INSTRUCTION helper, which already `#error`s on an unknown arch —
  one consistent story, no silent wrong number. No law tension.
- **R3 — non-disjoint same-name arms.** Two arms that are NOT target-disjoint (e.g. two
  `#os("linux")` `SYS_X` with different values) are an authoring error; `preds_target_disjoint`
  false must be a LOUD checker error (crumb 4 exempts ONLY disjoint pairs). Closes the silent
  last-writer-wins class. (Sibling R3.)
- **R4 — `.tkb` cross-module export of a cond const.** Honest-stopped (crumb 1). Sound: the
  compiler build MERGES all `src/*.tks` into ONE program, so `teko::sys` and its consumers share a
  program — no `.tkb` boundary. A future dependency needing this is REPORTED up.
- **R-gen — arch-guarded NON-const item.** No fn/type is arch-guarded in the corpus today.
  `cc_keep_item` HONEST-STOPS on one rather than silently emitting the host arch (there is no `#if`
  emitter for a fn body yet). Building that emitter is a separate REPORTED issue, not scope creep
  here. This is the faithful "generalize correctly" boundary the issue asks for.
- **R-matrix — closed whitelist reduces the arch ladder to a Linux shape.** Per §3.2 the arch axis
  is multi-arm only on Linux; Windows (x86_64) and macOS (arm64) are single-arch, so their teko.c
  emit NO arch ladder and the `SYS_*` families are os-pruned off them entirely. The layer-(a)
  fail-fast (§3.3) and the layer-(b) `#else #error` (§4.2) are redundant by owner ruling — an
  off-whitelist build is refused both before emission and at `cc`. Widening the matrix later is a
  one-line edit to `cc_supported_targets` plus (if a new OS gains a second arch) authoring that
  OS's arch arms; no mechanism change. No law tension.
- **TENSION (monolith law vs os-prune) — RESOLVED law-first.** "Emit todos os alvos, incl. SO"
  could be read as ONE teko.c for every OS too. Resolution: the syscall-vs-FFI divergence §16
  R1–R5 already ratified makes per-OS FUNCTION bodies unavoidable, so the C backend necessarily
  emits one `teko.c` PER OS; the monolith law's operative content for the C seed is FULL ARCH
  cross-compile within that OS — exactly the failing case this fix closes. The committed
  `bootstrap/teko.c` being the Linux seed (os already resolved) confirms the architecture is
  already per-OS. No unresolved tension; nothing HALTs.

## 11. Coordination with the sibling docs

- `docs/design/plano-s16-monolith-cc-emit.md`: this doc REUSES its A′ mechanism and crumbs 0–5,
  and SUPERSEDES its §6 prune (keep-all-target-arms) with the axis-aware `cc_keep_item`
  (os-resolved / arch-deferred). The implementer should treat §4/§6 HERE as the authoritative
  prune spec and that doc's §5/§7 (checker/consteval/codegen mechanism) as the authoritative
  emitter spec.
- `docs/design/plano-s16-sync-cross-plataforma.md`: its `#os("macos")`/`#os("windows")` consts are
  UNAFFECTED — they remain os-pruned (dropped from the Linux teko.c) exactly as today. This fix
  touches ONLY the `#arch` axis of the Linux teko.c. The §3.2 matrix bounds that sibling too: its
  macOS surface targets `arm64` only and its Windows surface `x86_64` only — so neither ever needs
  an arch ladder (each is single-arch), and the `cc_validate_target` fail-fast (§3.3) guards every
  build path both docs share. The one shared invariant: the SAME emitted
  `teko.c` must have BOTH axes right for its target — os pruned to the target, arch laddered — and
  the two mechanisms compose (os-prune first fixes the OS, the arch ladder then covers that OS's
  arches).

## 12. Files touched (all `.tks`; summary)

- `src/parser/parse_decl.tks`, `src/parser/parse_cc.tks` — crumb 0 (loud axis validation).
- `src/checker/tast.tks`, `src/checker/typer.tks`, `src/checker/collect.tks` — crumb 1 (guard),
  crumb 4 (disjoint-arm exempt).
- `src/emit/tkb_write.tks`, `src/emit/tkb_read.tks` — crumb 1 (honest-stop write / default read).
- `src/checker/consteval.tks` (+ `consteval_order.tks` awareness) — crumbs 2, 3.
- `src/build/prune.tks` — crumbs 2 (`cc_arch_vocab`/`cc_arch_deferred_survives`) + 6 (`PruneMode`,
  `cc_keep_item`, `prune_cc` signature). **The load-bearing axis-aware change.**
- `src/build/project.tks` — crumb 0b (`cc_supported_targets`, `cc_validate_target`, wired at the
  top of `frontend_check` — the fail-fast whitelist) + crumb 6 (`backend_is_c` → `PruneMode`).
- `src/codegen/codegen.tks` — crumb 5 (`guard_to_c_cond`, `cg_emit_cond_const_ladder`,
  `cond_const_c_symbol`, `TVar`→symbol; arch ladder `#else #error`).
- `bootstrap/teko.c` — regenerated at RITUAL A and RITUAL B (reseed only, never hand-edited).
