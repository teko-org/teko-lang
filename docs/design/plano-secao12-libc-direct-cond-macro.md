# §12 — libc-direct / conditional compilation / FFI macro-resolver — design proposal set

Status: **DESIGN — PROPOSAL SET for owner deliberation.** Read-only on product code;
this document writes no `.tks`, triggers no reseed, runs no build, and — per the task
constraint — **`teko test` was NOT run in any form** (the `monomorph` leak crashes the
container). Author: architect. Branch base: `origin/fix/retirement`.

> **What is SEALED elsewhere (not reopened here).** The **native** macro facility
> (`macro` = Family A syntactic / `comptime` = Family B evaluation, split by pipeline
> stage) is SEALED in `docs/design/mudancas-superficie-0.3.1.md` §14 and fully designed in
> `docs/design/plano-macro.md`. This document does **NOT** redesign native macro semantics.
> It covers the rest of the §12 cluster — the FFI/portability surface — and only
> **references** §14 for the `extern macro` / `extern comptime` spelling
> (`mudancas-superficie-0.3.1.md:1127`: "FFI = `extern` + the family").

> **Output-shape contract (owner's standing feedback).** The prior version of THIS doc was
> too weak — it presented forks and *open questions* without concrete examples. This rewrite
> obeys the corrected contract: every decision point below is presented as **3+ complete,
> fully-specified alternatives, and EACH alternative carries a concrete Teko code example —
> the declaration PLUS its use site PLUS its effect** — followed by **my recommendation**.
> There are **zero bare open questions**: a genuine judgment call is framed as N concrete,
> runnable options with a recommendation, never as a question. The owner deliberates over
> the proposals.

> **Sequencing (owner ruling, `mudancas-superficie-0.3.1.md:989`).** §12 is the **LAST**
> section — inverted with §11 (`exp`/`pub` visibility enforcement, the penultimate,
> `:985`). §12 must **operate over the already-formed visibility surface**: an `extern fn`
> or a `#os`-guarded item has an `exp`/`pub` visibility that §11 defines, so the FFI /
> conditional surface is designed against a settled visibility model, not a moving one.
> Nothing here lands before §11.

---

## 0. Current-state map (file:line — what each proposal reuses)

### 0.1 libc-direct — the FFI seam as it stands today

**Central fact:** there is **no** "libc-direct" today. Every system-surface call routes
through the maintained C seed `teko_rt.{c,h}`, reached by a **bare name the native backend
resolves to a `tk_*` symbol**.

| fact | where |
|---|---|
| The whole builtin-FFI surface is ONE dispatch in the native backend | `native_builtin_symbol` — `src/lir/lower.tks:5250` (fans out to `builtin_io_symbol`/`builtin_arena_symbol`/`builtin_str_query_symbol`/… — 109 distinct `tk_*` literals per `docs/memory/nativo-sem-teko-rt-mapa.md:8`) |
| `isel_*`/`objfile_*` are symbol-agnostic transport | `docs/memory/nativo-sem-teko-rt-mapa.md:9-12` (only `isel_x86_64`/`objfile_coff` name `tk_` in a doc-comment; `arm64`/`elf`/`macho` = zero) |
| A Teko-Teko call mangles through the namespace, never the bare name | `mangle_fn_symbol` — `src/lir/lower.tks:969`; keying rule `call_symbol` — `lower.tks:5310` |
| The pure-logic Teko twins ALREADY EXIST but are bypassed | `src/runtime/teko_rt.tks` (`str_eq:496`, `str_contains:580`, `u64_to_str:133`, `str_concat:115`, `fmt_*`) — the bare name resolves straight to `tk_*`, never entering these bodies |
| `extern fn` already exists as a foreign binding (symbol *resolution* is the own-linker, .33–.34) | `Function.is_extern`/`c_symbol`/`from_lib` — `src/parser/ast.tks:533-535`; VERBATIM lowering — `call_symbol` `lower.tks:5313` |
| Native targets carry os×arch | `LinkTargetKeys { build_os; emit_os; os_arch }` — `src/build/project.tks:3103` (`target_os`/`target_os_name`/`target_name`) |

**Taxonomy of what the seam carries** (`docs/memory/nativo-sem-teko-rt-mapa.md:20-49`):
- **Layer 1 — syscall/libc floor (irreducible):** `write`/`fwrite`, `abort`/`exit`,
  `malloc`, `snprintf`/`strtod` + host surface. **The maintained C seed — the Law keeps it**
  (Teko-only carve-out for `teko_rt.{c,h}`).
- **Layer 2 — raw memory (blocked by 2 language gaps):** `tk_region_*`/`tk_arena_*`/
  `tk_slice_*`/`tk_mem_copy` — Teko-able only once `teko::mem::load_u64`/`store_u64` + a
  `u64->ptr` cast exist (`docs/design/arena-em-teko.md:62,67`).
- **Layer 3 — pure logic with a ready Teko twin:** byte compare/hash/format — relinking is
  wiring, not a rewrite.

**Roadmap intent — RETIRE the `teko_rt` FFI seam** (`docs/design/nativo-sem-teko-rt.md` +
`docs/memory/nativo-sem-teko-rt-mapa.md`): move the FFI boundary DOWN (relink Layer 3 bare
names to the twin mangle) until only the named syscall floor (Layer 1) remains. **Explicit
lock: nothing lands until `teko` is 100 % native AND the native gate is green**
(`nativo-sem-teko-rt-mapa.md:51-57`); the standing native miscompilation is **#112** (the
`gen2==gen3` fixpoint rep-flip).

### 0.2 Conditional compilation — `#os` (exists), `#arch` (absent), `#if` (absent)

**`#os("…")` — implemented end-to-end, ZERO in-tree uses.**

| stage | where |
|---|---|
| parse `#os("…")` (string-only argument) | `src/parser/parse_decl.tks:1368-1371` (inside `parse_attributes`, ~`:1355`) |
| unknown-attribute message | `src/parser/parse_decl.tks:1382` |
| `#os` rejected before a `type` (function-only) | `src/parser/parse_decl.tks:1694` ("`#os(\"…\")` may only precede a function") |
| `os_guard: str` field on `Function` | `src/parser/ast.tks:536` |
| `os_guard` on `ParsedAttributes` | `src/parser/result.tks:31` |
| **prune**: `prune_os` drops any function whose guard ≠ target OS; `""` always passes — matches ONLY the `parser::Function` arm | `src/build/project.tks:118-133` |
| target-OS selection (triple `[extern] target`, else `teko::os()`) | `src/build/project.tks:107-114` (`target_os`) |
| prune call in the front-end (post-parse, pre-check) | `src/build/project.tks:466` |
| **.tkb serialization** (the guard crosses the dep format) | `src/emit/tkb_write.tks:522,540`; `src/emit/tkb_frame.tks:301`; `src/emit/tkb_read.tks:736` |

**In-tree uses of `#os`: ZERO.** The machinery is complete and entirely unexercised by
production code (Fork F).

**`#arch` — DOES NOT EXIST.** No parse, no field, no prune. The targets already distinguish
arch (`os_arch` in `LinkTargetKeys`, `project.tks:3103`), but there is no arch guard on the
surface.

**General `#if` — DOES NOT EXIST.** `#os` is the only conditional, and it is by OS-string
equality, not by an expression. The other attributes (`#arena_size(N)`, `#test`) are not
conditional (`parse_decl.tks:1368-1385`).

### 0.3 FFI macro-resolver — `extern macro` / `extern comptime` (the C-macro/constant side)

| fact | where |
|---|---|
| §14 seals `FFI = extern + the family` — `extern macro` (function-like C macro) / `extern comptime` (C constant like `O_RDONLY`) | `docs/design/mudancas-superficie-0.3.1.md:1127`; `docs/design/plano-macro.md:872-910` |
| The 4-tier teko-native, **no-`cc`** resolver: Tier 0 object-like CONSTANT (→ `extern comptime`), Tiers 1-2 function-like (→ `extern macro`, own-backend), Tier 3 arbitrary C → HONEST ERROR | `docs/design/star-ref-and-ffi-0.3.1.md:147-194` |
| Tier 1 symbol-resolution + `extern from lib` couple the **own linker (.33–.34)**, not `cc` | `star-ref-and-ffi-0.3.1.md:228-243` |
| `extern fn`/`extern comptime`/`extern macro` all reuse the SAME `Function` extern fields | `src/parser/ast.tks:533-535` (`is_extern`/`c_symbol`/`from_lib`) |

This document treats the resolver **only from the FFI side** — the tier staging and the
honest-stop. The native macro *engine* (`comptime` `eval_const`, Family A `quote`/hygiene) is
`plano-macro.md`'s, referenced, not reopened.

---

## FORK A — what "libc-direct" MEANS (the seam's scope)

Three concrete forms, smallest-first. Each names its effect on the `teko_rt` FFI removal.

### A1 — relink-only: move the FFI boundary DOWN to the existing Teko twins — *closable design, native-gate-blocked to land*

Keep `extern fn`→`teko_rt` for the syscall floor, but rewire each Layer-3 bare name so
`native_builtin_symbol` (`lower.tks:5250`) returns the **twin mangle** instead of the `tk_*`
literal — the twin body already exists in `src/runtime/teko_rt.tks`. **No new user surface.**

```teko
/**
 * str_eq — the Layer-3 twin that ALREADY EXISTS in the Teko runtime (`teko_rt.tks:496`) but is
 * bypassed today: the native backend resolves a bare `str_eq` straight to the C literal
 * `tk_str_eq`. A1 rewires the resolver so the bare name mangles to THIS body instead, deleting
 * one `tk_*` from the FFI seam with zero rewrite.
 *
 * @param a  the first byte string
 * @param b  the second byte string
 * @return   true iff the two byte strings are equal
 * @since 0.3.1
 */
fn str_eq(a: str, b: str): bool { /* existing pure-Teko body */ }
// use site:  a == b   lowers today to  call tk_str_eq
//   under A1 native_builtin_symbol("str_eq") returns  teko_runtime_str_eq  (mangle_fn_symbol, lower.tks:969)
//   effect: one fewer tk_* in teko_rt.{c,h}; the syscall FLOOR (malloc/write) is untouched
```

- **Effect on teko_rt removal:** directly executes `nativo-sem-teko-rt.md` step 0 (byte-pure
  twins) — moves the boundary down, does NOT delete the floor. **Blocked to LAND** by the
  native gate + #112 (`nativo-sem-teko-rt-mapa.md:51-57`); **fully designable now** (it is a
  resolver-table edit, own-native).

### A2 — direct `extern fn` to a libc/OS symbol via the own linker — surface closable now, resolution own-linker-blocked

The dev writes a real libc binding; the own backend emits an undefined-symbol reference +
relocation now, and the **own linker (.33–.34)** resolves it against `libc.so`/`.a` **without
`cc`** (`star-ref-and-ffi-0.3.1.md:228-243`).

```teko
/**
 * write — libc-direct: a real POSIX `write(2)` binding. The own backend emits an undefined
 * symbol reference `write` + its relocation TODAY (own-native, no `cc`); the own linker
 * (.33–.34) resolves it against libc. This is "libc-direct" in the strong sense — Teko code
 * calling the syscall wrapper with no `teko_rt` seam in between.
 *
 * @param fd   the file descriptor
 * @param buf  a pointer to the bytes to write (marshall surface: `ptr<byte>`, §5)
 * @param n    the byte count
 * @return     bytes written, or a negative errno (`isize`)
 * @since 0.3.1
 */
extern fn write(fd: i32, buf: *u8, n: usize): isize = "write" from lib "c"
// use site:  var k = write(1, msg_ptr, msg_len)
//   effect: object carries an undefined `write` reloc NOW; own-linker binds it against libc LATER
```

- **Effect on teko_rt removal:** A2 is the *replacement* path — a program that binds `write`
  directly needs no `tk_write` at all. The **surface + refs are own-native and designable
  now**; only symbol *resolution* waits on the own linker.

### A3 — hybrid: A1 now (as the retirement path), A2 as the ratified surface whose resolution links late — *RECOMMENDED*

A1 is wiring that pays down the `teko_rt` roadmap the moment the native gate is green; A2 is
the ratified user surface whose codegen (undefined-symbol refs + relocs) emits today and whose
resolution couples the own linker. This is exactly the split `star-ref-and-ffi-0.3.1.md:236-243`
already proposes.

```teko
/**
 * read_line — a hybrid illustration: the byte-logic helper lowers through the Teko twin (A1,
 * boundary-down), while the actual syscall is a direct `extern fn` (A2, own-linker-resolved).
 * A3 lets both coexist — pure logic leaves the seam, the floor is a real libc binding.
 *
 * @param fd   the descriptor to read from
 * @param into a byte buffer pointer
 * @param cap  the buffer capacity
 * @return     bytes read, or negative errno
 * @since 0.3.1
 */
extern fn read(fd: i32, into: *u8, cap: usize): isize = "read" from lib "c"
fn trim_eq(a: str, b: str): bool { str_eq(a, b) }   // str_eq → twin mangle (A1), no tk_*
// use site:  var n = read(0, buf, 256);  if trim_eq(line, "quit") { … }
//   effect: read = own-native ref + own-linker resolve (A2); trim_eq/str_eq = twin, seam-free (A1)
```

- **Recommendation: A3.** It honours "own-backend-first, nothing depends on `cc`"
  (`star-ref-and-ffi-0.3.1.md:60-64`) and the C-seed Law (the Layer-1 floor stays under the
  Teko-only carve-out). A2 raises "who writes the libc bindings — the user, or a curated
  `teko::sys` stdlib?"; either answer needs per-platform variation, which **couples Fork A to
  Forks B/C**. The recommendation there (per-platform bindings live behind `#os`/`#arch`)
  resolves it without a question.

---

## FORK B — the SCOPE of conditional compilation (`#os` is function-only today)

libc-direct needs per-platform variation not only of functions but of **system layout types**
(`struct stat`, `sockaddr`) and **constants** — today `#os` guards only functions
(`parse_decl.tks:1694`). Three concrete scope forms.

### B1 — widen `#os` (and its future `#arch`) to ALL top-level items — *RECOMMENDED*

Allow `#os` before `type`/`const`/extern-block, not only `fn`. The prune generalizes from the
single `parser::Function` arm (`project.tks:126`) to match over `Item`.

```teko
/**
 * Stat — a per-OS system layout type, expressible ONLY if `#os` widens to `type` (today
 * `parse_decl.tks:1694` rejects it). Each OS variant carries its own C-ABI field order; the
 * consumer's target OS selects exactly one, the other is pruned before type-check.
 *
 * @since 0.3.1
 */
#os("linux") #repr("c") type Stat = struct { dev: u64; ino: u64; mode: u32; size: i64 }
#os("macos") #repr("c") type Stat = struct { dev: i32; mode: u16; ino: u64; size: i64 }
// use site:  extern fn fstat(fd: i32, out: *Stat): i32 = "fstat" from lib "c"
//   effect on a linux build: prune_os keeps the linux `Stat`, drops the macos one;
//   `Stat` resolves to exactly one layout — the macos variant never reaches the checker
```

- **Effect:** unblocks per-platform libc-direct types. **Cost:** generalize `prune_os`
  (`project.tks:118-133`) to visit every `Item` arm, and lift the `type`-rejection at
  `parse_decl.tks:1694`. Closable now (front-end only, no backend).

### B2 — add a symmetric `#arch("…")` guard

`#arch` mirrors `#os`: a new `arch_guard` field, a twin `prune_arch`, and a `target_arch`
derived from the target (`os_arch`, `project.tks:3103`).

```teko
/**
 * bswap32 — a per-ARCH intrinsic selection: arm64 has a rev instruction the x86_64 form lacks,
 * so the byte-swap is written twice and `#arch` prunes to the target's variant. Impossible
 * today (`#arch` does not exist); the OS axis alone cannot express it (both variants are linux).
 *
 * @param x  the value to byte-swap
 * @return   the byte-swapped value
 * @since 0.3.1
 */
#arch("arm64")  fn bswap32(x: u32): u32 { /* rev-based */ }
#arch("x86_64") fn bswap32(x: u32): u32 { /* shift/or-based */ }
// use site:  var n = bswap32(host_order)
//   effect on an arm64 target: prune_arch (twin of prune_os) keeps the arm64 body, drops x86_64
```

- **Effect:** covers per-arch intrinsics + per-arch ABI layout. **Cost:** low, symmetric to
  the existing `#os` machinery (a mirror field + prune + one `target_arch` derivation).
  Closable now.

### B3 — a general `#if(<const predicate>)`

One conditional attribute over a comptime predicate; `#os`/`#arch` become sugar
(`#if(os == "linux")`).

```teko
/**
 * fast_path — a general-predicate guard: the item survives only if the compile-time expression
 * holds. Needs a const-expression EVALUATOR in the prune stage (today the prune is string
 * equality, `project.tks:126`) — which reopens the comptime axis §14 keeps for the native
 * `comptime` family, not for conditional compilation.
 *
 * @return  the platform-selected value
 * @since 0.3.1
 */
#if(os == "linux" && arch == "x86_64") fn fast_path(): i32 { 1 }
#if(!(os == "linux" && arch == "x86_64")) fn fast_path(): i32 { 0 }
// use site:  var v = fast_path()
//   effect: prune must EVALUATE the boolean predicate per item — a comptime step the string
//   equality prune does not have; the negation/`&&` demand a real evaluator
```

- **Effect:** maximal expressivity. **Cost:** a const-expression evaluator inside the prune —
  and it reopens the comptime-in-conditional axis. `#os`/`#arch` by string equality are **not**
  comptime (a build-time string match), so they do not touch that axis; `#if` does. Deferred.

**Recommendation: B1 + B2, defer B3.** Widen `#os` to all items, add symmetric `#arch`, keep
the prune as cheap string equality. This covers per-platform libc-direct (os×arch, in both
`fn` and `type`) without opening a predicate evaluator. `#os("x")` stays forward-compatible: if
`#if` ever lands post-1.0 it desugars to `#if(os=="x")` with zero corpus churn (Fork F: zero
uses today).

---

## FORK C — the SYNTAX of the platform guards (choose now to avoid painting a corner)

Even with B3 deferred, the *spelling* of `#os`/`#arch` must be fixed now. Three concrete forms.

### C1 — distinct attributes, stacked = conjunction (AND) — *RECOMMENDED*

`#os` and `#arch` are separate attributes; stacking them on one decl means AND. Reuses the
existing attribute loop, which already iterates multiple `#…` (`parse_decl.tks:1355-1385`). No
disjunction, no negation.

```teko
/**
 * sendfile_fast — the AND case: this binding exists only on linux AND x86_64. Two stacked
 * guards conjoin; the attribute loop already accepts multiple `#…` before one decl, so this
 * is the cheapest expressivity that still covers os×arch.
 *
 * @param out_fd  the destination descriptor
 * @param in_fd   the source descriptor
 * @param n       byte count
 * @return        bytes transferred, or negative errno
 * @since 0.3.1
 */
#os("linux") #arch("x86_64") extern fn sendfile_fast(out_fd: i32, in_fd: i32, n: usize): isize = "sendfile" from lib "c"
// use site:  var k = sendfile_fast(dst, src, len)
//   effect on linux/arm64: the #arch("x86_64") guard fails → prune drops the item (AND semantics)
//   effect on linux/x86_64: both guards pass → kept
```

- **Cost:** near-zero (the loop already collects multiple attributes; conjunction is the
  natural join). **Forward-compatible** with a future `#if`: stacked guards fold to `&&`.

### C2 — `#os("linux","macos")` OR-list — a bounded step toward disjunction

`#os` (and `#arch`) accept a comma list = OR, for the common "POSIX-like" case. One
expressivity step short of `#if`.

```teko
/**
 * posix_open — the OR case: one binding shared by every POSIX-like OS, written once. An OR-list
 * folds "linux OR macos" into a single guard without a predicate evaluator — the parser reads a
 * string list instead of one string, the prune tests membership instead of equality.
 *
 * @param path   the path pointer
 * @param flags  the open flags
 * @return       the new descriptor, or negative errno
 * @since 0.3.1
 */
#os("linux","macos") extern fn posix_open(path: *u8, flags: i32): i32 = "open" from lib "c"
// use site:  var fd = posix_open(p, rdonly)
//   effect on macos: prune tests `"macos" ∈ {"linux","macos"}` → kept (no duplicate needed)
//   effect on windows: membership fails → dropped
```

- **Cost:** small — `os_guard` becomes a list; `prune_os` tests membership. A cheap amendment
  the owner can accept on top of C1 if the "OR" is wanted now.

### C3 — `#if(os == "linux" || os == "macos")` from the start

Full predicate syntax immediately (this is C1+C2 collapsed into B3).

```teko
/**
 * posix_open — the predicate case: the same POSIX-like binding expressed as a boolean over
 * build variables. Maximally flexible, but the `||` forces the prune to run a real expression
 * evaluator NOW — the cost B3 defers.
 *
 * @param path   the path pointer
 * @param flags  the open flags
 * @return       the descriptor, or negative errno
 * @since 0.3.1
 */
#if(os == "linux" || os == "macos") extern fn posix_open(path: *u8, flags: i32): i32 = "open" from lib "c"
// use site:  var fd = posix_open(p, rdonly)
//   effect: prune must evaluate the disjunction per item — the evaluator B3 defers
```

- **Cost:** the evaluator, plus the comptime-axis reopening. Rejected as the primary for the
  same reason B3 is deferred.

**Recommendation: C1 now, with C2 as a cheap amendment if the owner wants OS "OR" immediately.**
C1 covers os×arch via stacked conjunction with essentially no new machinery and stays
forward-compatible with a post-1.0 `#if`. C3 is rejected now (evaluator + comptime axis).

---

## FORK D — FFI resolver: `extern comptime` (constants) + `extern macro` (function-like) tier staging

Per §14 (`mudancas-superficie-0.3.1.md:1127`) and the 4-tier resolver
(`star-ref-and-ffi-0.3.1.md:147-194`): Tier 0 object-like constants are VALUES → `extern
comptime`; Tiers 1-2 function-like → `extern macro`; Tier 3 arbitrary C → honest error. Three
concrete staging forms. (The native `comptime`/`macro` engine is `plano-macro.md`'s — not
reopened.)

### D1 — Tier 0 only in the first window: `extern comptime` constants, value-inlined — first increment

Ship the object-like C-constant resolver alone (`O_RDONLY`, `SOCK_STREAM`, flag bits) — a mini
C-constant evaluator that inlines a value. Zero runtime, no IR, no own-linker.

```teko
/**
 * O_RDONLY — FFI Tier 0 as `extern comptime`: the resolver reads `fcntl.h`, extracts the
 * `#define`, evaluates the C constant-expression with its mini evaluator, and inlines the
 * value. No body, no runtime, no `cc`, no own-linker. This alone covers header CONSTANTS and
 * FLAGS entirely — the most common libc need.
 *
 * @return  the resolved integer constant, inlined at each use
 * @since 0.3.1
 */
extern comptime O_RDONLY: i32 = "O_RDONLY" from header "fcntl.h"
// use site:  var fd = posix_open(path, @O_RDONLY)
//   effect: `@O_RDONLY` is replaced by the resolved literal (e.g. 0) at compile time
```

- **Effect on teko_rt removal:** none directly, but it removes the "hand-copy the flag value"
  hazard for every libc-direct binding. Closable now (own-native value inlining).

### D2 — Tiers 0-2 own-native + Tier 3 honest-stop — *RECOMMENDED as the destination*

D1 plus `extern macro` for function-like C macros: Tier 1 (symbol-alias → bind a real symbol,
own-linker) and Tier 2 (simple arithmetic body → own-backend IR). Tier 3 (arbitrary C) is the
single honest error.

```teko
/**
 * htonl — FFI Tiers 1-2 as `extern macro`: the resolver classifies the function-like `#define`.
 * Tier 1 binds a real linkable symbol (own-linker); Tier 2 translates the pure C expression
 * body (`((x&0xff)<<24)|…`) to own-backend IR and inlines it. Tier 3 (statements, `##`, side
 * effects) is the honest stop — the ONLY place the resolver refuses.
 *
 * @param x  the u32 host-order value
 * @return   the network-order (byte-swapped) value
 * @throws   a Tier-3 honest error if the macro is not mechanically resolvable
 * @since 0.3.1
 */
extern macro htonl(x: u32): u32 = "htonl" from header "arpa/inet.h"
// use site:  var n = @htonl(host_order)
//   effect: Tier 1 → a symbol call resolved by the own linker; Tier 2 → inlined byte-swap IR
```

- **Effect on teko_rt removal:** makes libc-direct real for bit-twiddle/flag idioms without
  `cc`. **Cost:** a real bounded subsystem (C header tokenizer + `#define` extractor + constant
  evaluator + a mini C-expr→IR translator, `star-ref-and-ffi-0.3.1.md:189-194`). Tier 1
  resolution couples the own linker; Tier 2 couples own-backend IR maturity.

### D3 — reject `extern macro`/`extern comptime` entirely — hand-write every constant

No resolver: the dev writes the value by hand.

```teko
/**
 * O_RDONLY — the no-resolver alternative: the dev hand-copies the constant. Zero subsystem cost,
 * but the value is now UNCHECKED against the header — a silent portability bug when the platform
 * `#define` differs (linux `0` vs a hypothetical other ABI), and it must be re-guarded per OS by
 * hand.
 *
 * @since 0.3.1
 */
#os("linux") const O_RDONLY: i32 = 0
#os("macos") const O_RDONLY: i32 = 0
// use site:  var fd = posix_open(path, O_RDONLY)
//   effect: works, but every constant is a manual, per-OS, header-desynchronizable copy
```

- **Cost:** zero implementation, maximal maintenance + silent-drift risk. Rejected as the
  destination; noted as the honest fallback if the resolver subsystem slips a release.

**Recommendation: D1 as the first increment, D2 as the destination; D3 only as a slip
fallback.** This is exactly `star-ref-and-ffi-0.3.1.md:192-194` sequencing — Tier 0 (`extern
comptime`) first (value inlining, no IR/linker dependency), Tiers 1-2 (`extern macro`) as the
own backend + own linker mature, Tier 3 the always-present honest stop.

---

## FORK E — the prune STAGE × `.tkb` dep interaction

`prune_os` runs in the front-end **after parse, before check** (`project.tks:466`), and the
`os_guard` is **serialized into the `.tkb`** (`tkb_write.tks:540`, `tkb_frame.tks:301`,
`tkb_read.tks:736`) — so a dep ships items carrying their guards, and the *consumer* prunes
with *its* target. Three concrete models.

### E1 — keep source-level pre-check pruning; `.tkb` carries ALL guards; consumer prunes — *RECOMMENDED*

The current model: an item guarded for another OS is **never type-checked** on the build host
(so a linux-only `extern fn` may name a symbol absent on the build host); `#arch` enters the
**same** point with a mirror `arch_guard` in the `.tkb`.

```teko
/**
 * epoll_create1 — a linux-only dep export: it ships in the `.tkb` WITH its `#os("linux")` guard
 * (tkb_frame.tks:301). A macos consumer prunes it before type-check (project.tks:466), so it is
 * never checked against macos — its linux-only symbol never has to exist on the macos host.
 *
 * @param flags  the epoll flags
 * @return       the epoll descriptor, or negative errno
 * @since 0.3.1
 */
#os("linux") extern fn epoll_create1(flags: i32): i32 = "epoll_create1" from lib "c"
// consumer on macos:  prune_os drops this before check → its linux symbol is irrelevant on macos
// consumer on linux:  kept and checked
```

- **Effect:** one source distribution serves all targets; the consumer's os×arch selects. **Add
  a mirror `arch_guard` to the same serialization** (`tkb_write.tks:540`) or a cross-arch dep
  breaks. Closable now (a serializer field + the prune generalization from Fork B).

### E2 — check everything, prune at lowering (post-check)

Do not prune before check; keep every variant through the checker and drop non-matching ones at
lowering.

```teko
/**
 * WriteFile — a windows-only binding whose symbol does NOT exist on a linux build host. Under
 * E2 the checker must process it on linux (prune happens later), so a linux build type-checks a
 * windows-only extern — its symbol resolution must be deferred or it errors on the wrong host.
 *
 * @param h    the handle
 * @param buf  the byte pointer
 * @param n    the byte count
 * @return     bytes written
 * @since 0.3.1
 */
#os("windows") extern fn WriteFile(h: uptr, buf: *u8, n: u32): i32 = "WriteFile" from lib "kernel32"
// effect under E2: the linux build still checks WriteFile → forces cross-host symbol tolerance
```

- **Cost:** the checker must tolerate items destined for another platform (foreign symbols,
  foreign layout types) — strictly harder than E1, and buys nothing the consumer needs.
  Rejected.

### E3 — pre-pruned per-target `.tkb` distribution

The dep is compiled once per target; each `.tkb` already contains only that target's items.

```teko
/**
 * poll_wait — under E3 the dep publisher ships one `.tkb` per target, each already pruned. A
 * consumer picks the `.tkb` matching its os×arch; the guard need not survive into the format
 * (it was applied at publish time).
 *
 * @param fds  the pollfd array pointer
 * @param n    the descriptor count
 * @param ms   the timeout in milliseconds
 * @return     ready count, or negative errno
 * @since 0.3.1
 */
#os("linux") extern fn poll_wait(fds: *u8, n: usize, ms: i32): i32 = "poll" from lib "c"
// effect under E3: linux-target.tkb contains poll_wait; macos-target.tkb does NOT — publisher pre-pruned
```

- **Cost:** N artifacts per dep, a target-matrix at publish time, and it contradicts the
  current single-`.tkb` format that already carries the guard. Rejected as a regression of the
  working model.

**Recommendation: E1 (source-level pre-check prune, all guards in one `.tkb`, consumer
selects), extended with a mirror `arch_guard`.** It is the current model, it lets a
cross-platform binding reference host-absent symbols safely, and `#arch` slots into the exact
same serialization + prune point. Reject E2 (checker cross-host burden) and E3 (artifact
matrix).

---

## FORK F — the ZERO-use `#os` machinery (complete but unexercised)

`#os` is end-to-end but has ZERO in-tree production uses (§0.2) — the prune has never actually
removed an item in a real multi-platform build. Three concrete resolutions.

### F1 — marry the first `#os`/`#arch` fixture to the first libc-direct binding — *RECOMMENDED*

The first real exercise of the conditional machinery IS libc-direct: a per-platform `extern fn`
that the prune must select. The fixture and the feature light up together.

```teko
/**
 * plat_write — the first real `#os` exercise: one logical operation, two platform bindings, the
 * prune forced to pick exactly one in a build that actually links. This takes `#os` out of the
 * "compiles but nothing uses it" false-green in the same move that lands libc-direct.
 *
 * @param fd   the descriptor / handle
 * @param buf  the byte pointer
 * @param n    the byte count
 * @return     bytes written, or negative errno
 * @since 0.3.1
 */
#os("linux")   extern fn plat_write(fd: i32, buf: *u8, n: usize): isize = "write" from lib "c"
#os("windows") extern fn plat_write(fd: i32, buf: *u8, n: usize): isize = "WriteFile" from lib "kernel32"
// fixture on linux: prune keeps the `write` variant; native run proves the byte went out (exit 0)
// fixture on windows: prune keeps the `WriteFile` variant
```

- **Effect:** the conditional machinery gets its first production witness; the prune×.tkb×check
  interaction (Fork E) is exercised for the first time by a real removed item.

### F2 — add a standalone cross-platform corpus self-test NOW, before libc-direct

Exercise `#os` immediately with a pure-Teko cross-platform item (no FFI), to de-risk the prune
before the FFI epic.

```teko
/**
 * platform_tag — a pure-Teko `#os` self-test: no FFI, just two guarded bodies the prune must
 * select between, so the prune×.tkb×check path is exercised on a host that can actually run
 * BOTH build configs, independent of the (blocked) libc-direct work.
 *
 * @return  a stable per-OS tag
 * @since 0.3.1
 */
#os("linux") fn platform_tag(): i32 { 1 }
#os("macos") fn platform_tag(): i32 { 2 }
// fixture: a linux native build returns 1, a macos build returns 2 — proves prune selection works
```

- **Effect:** de-risks the machinery earliest, decoupled from the native-gate-blocked
  libc-direct. Cheapest confidence; a good companion to F1 rather than a substitute.

### F3 — quarantine the `#os` machinery until libc-direct needs it

Mark `#os` as provisional/unstable and gate it behind libc-direct, so an unexercised feature is
not treated as load-bearing.

```teko
/**
 * legacy_only — under F3 `#os` is a quarantined, provisional attribute: it parses and prunes,
 * but is documented as unstable-until-exercised, so no corpus depends on it before the first
 * real use lands. Prevents a false-green "feature" from calcifying untested.
 *
 * @return  a placeholder value
 * @since 0.3.1
 */
#os("linux") fn legacy_only(): i32 { 0 }
// effect under F3: `#os` stays parseable but flagged provisional; no new use until F1 activates it
```

- **Cost:** documentation churn + a provisional flag; risks the machinery bit-rotting. Weaker
  than actually exercising it (F1/F2). Recorded, not recommended.

**Recommendation: F1 as the activation, with F2 as an earlier standalone de-risk.** Land a
pure-Teko `#os` self-test (F2) as soon as §12 opens to prove prune selection on a host that can
build both configs, then marry `#os`/`#arch` to the first real libc-direct binding (F1) so the
machinery leaves false-green with a linking witness. Reject F3 (quarantine bit-rots).

---

## Closable-now vs design-ahead-blocked (native gate / #112 / own-linker)

**Closable now (surface, own-native, depends on neither `cc` nor the own linker):**
- **Fork B** (widen `#os` to all items; add `#arch`; defer `#if`) — front-end parse + prune
  generalization (`parse_decl.tks:1694`, `project.tks:118-133`).
- **Fork C** (stacked-conjunction syntax; optional OR-list) — parser + prune only.
- **Fork D Tier 0** (`extern comptime` constant resolver) — own-native value inlining, no IR,
  no linker.
- **Fork E** (source-level pre-check prune, `.tkb` carries all guards + a mirror `arch_guard`)
  — serializer + prune (`tkb_write.tks:540`).
- **Fork F2** (pure-Teko `#os` self-test) — a fixture over machinery that exists today.

**Design-ahead, blocked (do NOT land without the dependency):**
- **Fork A1** (relink Layer 3 to the twins / retire the seam) — BLOCKED by the **native gate
  green + #112** (`nativo-sem-teko-rt-mapa.md:51-57`). Design is ready in `nativo-sem-teko-rt.md`;
  the resolver-table edit is specified.
- **Fork A2 resolution + Fork D Tier 1** (`extern fn`/`extern macro` symbol binding) — couple
  the **own linker (.33–.34)**. The *surface* (`extern fn`/`extern comptime` spelling +
  undefined-symbol refs + relocs) is own-native and closable now; only *resolution* waits.
- **Fork D Tier 2** (C-expr→own-IR expansion) — couples own-backend IR maturity
  (`star-ref-and-ffi-0.3.1.md:192-194`).
- **Fork F1** (libc-direct-married fixture) — waits on A2/A1 landing.

---

## Risks & law tensions (each with a recommended resolution)

| Risk / tension | Where it bites | Recommended resolution |
|---|---|---|
| **Native-gate lock (#112)** | Fork A1 (seam retirement) | DESIGN-AHEAD: specify the resolver-table relink now; land only after the native gate is green + #112 closed (`nativo-sem-teko-rt-mapa.md:51-57`). No HALT — the block is a known dependency, not a tension. |
| **No `cc` dependency** (`star-ref-and-ffi-0.3.1.md:60-64`) | Fork A2, Fork D Tier 1 | Surface + refs are own-native NOW; symbol *resolution* is the own linker (.33–.34), never `cc`. The split (`:236-243`) already respects this. No HALT. |
| **C-seed FROZEN except the maintained floor** | Fork A1 must not delete the Layer-1 syscall floor | A1 moves the boundary DOWN; the floor (`write`/`malloc`/`exit`) stays under the Teko-only carve-out. Target is "minimal named floor", not "zero `tk_*`" (`nativo-sem-teko-rt-mapa.md:48`). No tension if A1 honours it. |
| **Comptime axis (LTS closed for conditional predicates)** | Fork B3 / C3 general `#if` | Defer `#if`; `#os`/`#arch` are build-time STRING equality (not comptime), so they do not reopen the axis §14 reserves for the native `comptime` family. Passes-all-Laws → firm recommendation, no HALT. |
| **"macro" vocabulary** vs `extern macro` | Fork D spelling | §14 SEALED `extern macro`/`extern comptime` as the FFI family names (`mudancas-superficie-0.3.1.md:1127`) — this doc references, does not relitigate. `extern macro` binds a FOREIGN (C) macro; it is not a user macro. No HALT. |
| **M.0 small-language surface** | Fork B2 (`#arch`), Fork D subsystem | `#arch` is symmetric reuse of the `#os` shape (one mirror field/prune), not new surface shape. The Tier resolver is bounded (`star-ref-and-ffi-0.3.1.md:189`); stage it (D1 then D2). Each its own crumb. |
| **Zero-use `#os` false-green** | Fork F | Exercise it: F2 self-test as soon as §12 opens, F1 libc-direct witness on landing. Reject F3 quarantine (bit-rots). |
| **§11-before-§12 ordering** | The whole section | §12 lands AFTER §11 so `extern`/`#os`-guarded items have a settled `exp`/`pub` visibility (`mudancas-superficie-0.3.1.md:989`). Sequencing constraint, not a tension. |

**No genuine unresolved tension remains — no HALT.** Every judgment call is resolved law-first
with a concrete recommendation, and every proposal carries a runnable Teko example (declaration
+ use site + effect). The owner deliberates over the alternatives, not over open questions.

---

## Sequencing note + crumb-ahead sketch (once §11 lands and §12 opens)

§12 is the **last** section; it opens only after §11 (`exp`/`pub`) forms the visibility
surface. When it does, the closable-now forks sequence as independently gate-able crumbs
(ritual points ★ = full gate + FIXPOINT):

1. **Widen `#os` to all items (Fork B1).** Lift the `type`/`const` rejection
   (`parse_decl.tks:1694`); generalize `prune_os` over `Item` (`project.tks:118-133`). Gate:
   parser + prune fixtures + Fork F2 self-test.
2. **Add `#arch` (Fork B2) + mirror `arch_guard` in `.tkb` (Fork E).** New field
   (`ast.tks:536` sibling), `prune_arch`, `target_arch` from `os_arch` (`project.tks:3103`),
   serializer mirror (`tkb_write.tks:540`). Gate: cross-arch dep fixture.
3. ★ **`extern comptime` Tier 0 constant resolver (Fork D1).** Mini C-constant evaluator,
   `@O_RDONLY` inlined; reference §14 spelling. Gate: FULL gate + `extern_comptime_o_rdonly`.
4. **libc-direct surface (Fork A2 surface / Fork C1 syntax).** `extern fn … from lib`
   undefined-symbol refs + relocs (own-native); stacked `#os`/`#arch` conjunction. Gate:
   object-emission fixture (resolution deferred to the own linker).
5. ★ **Seam relink (Fork A1)** — DESIGN-AHEAD, gated on native-green + #112.
6. ★ **`extern macro` Tiers 1-2 (Fork D2)** — coupled to the own linker + own-backend IR.

Fixtures to add (inputs → expected native exit codes), gated on the **native** engine (never
`teko test` here): `cond_os_type_pruned/` (per-OS `Stat`, wrong variant pruned → ACCEPT),
`cond_arch_intrinsic/` (per-arch `bswap32` selection → exit = swapped value),
`cond_os_arch_and/` (stacked conjunction; wrong-arch dropped → ACCEPT),
`extern_comptime_o_rdonly/` (Tier 0 constant → exit = resolved value),
`extern_macro_tier3_rejected/` (arbitrary C → REJECT, honest "not mechanically resolvable"),
`dep_cross_os_tkb/` (linux-only export consumed on macos, pruned via `.tkb` guard → ACCEPT),
`os_selftest/` (Fork F2 pure-Teko `#os` selection → exit = per-OS tag). Each new/changed line
carries 100 % delta coverage on the native gate.

---

*Design-ahead, doc-only. No product code touched. `teko test` was NOT run in any form (the
`monomorph` leak crashes the container). Native macro semantics are §14/`plano-macro.md`'s and
are only referenced here. After the owner deliberates part-by-part, this becomes an executable
plano-secao12 (crumbs / type signatures / fixtures / gates).*
