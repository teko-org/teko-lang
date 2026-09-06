---
seq: 0149
crumb-id: MEM-E0b
milestone: M5
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [MEM-E0a]
sources:
  - ".crumbs/0011-SM-G5-marshall-opaque-ptr.md"                       # the recovered design (opaque ptr + wrap/unwrap)
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:259-357"  # §6 Marshall opaque ptr
  - "DECISION_LOG.md:1166"                                            # D130 addendum — ptr/uptr opaque + wrap/unwrap
  - "src/parser/ast.tks:208"                                          # NewtypeBody { backing; methods } — the capability EXISTS
  - "src/parser/parse_decl.tks:998"                                   # newtype parse (landed)
  - "src/checker/typer.tks:5256-5318"                                 # scalar-newtype checking (landed)
  - "src/emit/header.tks:73-74"                                       # GAP: value-type newtype header export errors
  - "src/runtime/teko_rt.tks:64-69"                                   # str_of_bytes copy-loop (the flagship target)
  - "src/checker/di.tks:373"                                          # di_type_id (the one project hash)
---

# 0149 · MEM-E0b — `ptr`/`uptr` as Teko surface NEWTYPES over `isize`/`usize` + `wrap`/`unwrap`

> Owner (D130, deepened): `ptr`/`uptr` are NOT compiler-hardcoded primitives — they are SURFACE newtypes
> defined in Teko's own base:
> ```
> global exp type ptr  = isize { … wrap/unwrap }
> global exp type uptr = usize { … wrap/unwrap }
> ```
> a newtype over a scalar base (the arch-word `isize`/`usize`, E0a) WITH a method block, like Go's
> `type X Underlying` + methods. **VERIFIED: the capability EXISTS** — `NewtypeBody { backing; methods }`
> is parsed (`parse_decl.tks:998`), checked (`typer.tks:5256-5318`, explicit scalar-newtype path),
> collected, borrow-checked, tkb-serialized. So this is USE, not a new capability — the ONE prerequisite
> gap is header-export of a value-type newtype (`emit/header.tks:73` errors), lifted here because
> `ptr`/`uptr` are `global exp`. The flagship consumer is the ZERO-COPY `str`↔`[]byte` interop. Recover
> 0011's design (opaque, no arithmetic, tag layer); the surface is the newtype, not `PrimKind`.

## Goal

Define `ptr`/`uptr` as `global exp` newtypes over `isize`/`usize` in the language base (a prelude-injected
module), RETIRING the hardcoded `PrimKind::Ptr`/`Uptr` and the generic `Ptr{inner}` as the USER surface —
the machine representation IS the backing arch-word, so lowering flows through the backing (byte-preserving
on 64-bit: `ptr`/`uptr` == the word). Opacity comes from the newtype being NOMINAL over the word with NO
arithmetic operators declared (so `p+n`/`p[n]` do not type-check — no element width). The two CANONICAL
methods (owner, VERBATIM) live on the newtype, identical on both `ptr` and `uptr`:

- **`ptr::unwrap<T>(ref T): ptr`** — STATIC: `ref T` → the opaque `ptr` of that reference.
- **`ptr.wrap<T>(): T`** — INSTANCE: the opaque `ptr` → the `T` at it (reinterpret).

These are the DIRECT same-base conversion — zero-copy, zero-cast, no extra computation. **Flagship:**
`[]str` and `[]byte` share the `{ptr,len}`-of-bytes base, so `ptr::unwrap<[]str>(ref xs).wrap<[]byte>()`
yields `[]byte` with zero copy — the implicit same-rep cast the expurgo wants (CLAUDE.md), which KILLS the
`str_of_bytes` copy-loop (`teko_rt.tks:64-69`). Byte-preserving on 64-bit → `[fixpoint]`; the mass
migration of raw `u64`/`i64` pointers → the newtype is the reball (`0091 SM-S4` / `MEM-W6`).

## Where

- `src/emit/header.tks:73-74` — LIFT the "exporting a value-type newtype in the header is not yet
  supported" error: emit the scalar-newtype (backing + method signatures) to the `.tkh` (the ONE
  prerequisite — `ptr`/`uptr` are `global exp`, must reach the header).
- a language-base module (prelude-injected, e.g. `src/sys/` or `src/runtime/`) — DEFINE
  `global exp type ptr = isize { … }` and `global exp type uptr = usize { … }` with the two methods.
- `src/checker/type.tks:51-54` — retire the user-facing `PrimKind::Ptr`/`Uptr` surface (the generic
  `Ptr{inner}` goes; the machine-word backing is `isize`/`usize`). Keep an internal lowering target only
  if the backend needs one; the newtype lowers via its backing.
- `src/checker/scope.tks`/`src/lir/lower.tks` — `wrap`/`unwrap` as the newtype's methods; lower to a bare
  reinterpret (zero instructions beyond the type change).
- `src/checker/typer.tks` — accept the same-rep `str`↔`[]byte` cast as IMPLICIT (identity reinterpret).
- `src/runtime/teko_rt.tks:64-69` — `str_of_bytes`/`bytes_of_str` become identity reinterprets (adopted
  in the reball, kept inert here).
- `src/checker/di.tks:373` — REUSE `di_type_id` for the OPTIONAL FFI-checked tag layer (one hash).

## How

1. Lift the header-export gap for value-type newtypes (`header.tks:73`).
2. Define the `ptr`/`uptr` newtypes over `isize`/`usize` with the two canonical methods:

```teko
/**
 * unwrap — STATIC method on the `ptr` newtype (`ptr::unwrap<T>(ref T): ptr`): take a reference to a `T`
 * and return the OPAQUE pointer of that reference (typed-ref → opaque pointer). The OUT half of the
 * direct same-base conversion — hand a `ref []str` to get its opaque `ptr`, then `.wrap<[]byte>()`
 * reinterprets it as `[]byte`, zero copy, zero cast. An identical static method lives on `uptr`.
 *
 * @param r  a reference to the typed value whose address is taken
 * @return   the opaque pointer of `r`
 * @since 0.3.1
 */
static fn unwrap<T>(r: ref T): ptr

/**
 * wrap — INSTANCE method on a `ptr` value (`p.wrap<T>(): T`): return the `T` at that pointer — reinterpret
 * the opaque pointer AS a `T`. The IN half of the direct same-base conversion, zero-copy zero-cast, no
 * extra computation (sound when `T` shares the pointee's base representation). Identical on `uptr`.
 *
 * @return  the `T` value at this opaque pointer
 * @since 0.3.1
 */
fn wrap<T>(): T
```

3. Retire the hardcoded `PrimKind::Ptr`/`Uptr` user surface; the name `ptr`/`uptr` resolves to the
   newtype; lowering flows through the backing arch-word (byte-preserving on 64-bit).
4. Lower `wrap`/`unwrap` to a bare reinterpret; the opaque newtype forbids arithmetic; `wrap`/`unwrap`
   bridge same-base types (compile-time safety). The 0011 arena-liveness+tag stays as the OPTIONAL
   FFI-checked layer (foreign/dead-arena) via `region_lookup`/`di_type_id`.
5. Accept `str`↔`[]byte` as an implicit same-rep cast; keep `str_of_bytes` adoption for the reball.

## Rulings & laws

- **Teko-only, newtype-in-the-base:** `ptr`/`uptr` are `global exp` Teko newtypes over `isize`/`usize`,
  NOT `PrimKind` — the capability (`NewtypeBody`) is landed; only header-export needed lifting.
- **Recover 0011 signatures VERBATIM:** the two canonical methods; do NOT invent a variation.
- **Opacity = nominal newtype + no arithmetic operators** (structural, not a keyword); same-base
  `wrap`/`unwrap` is compile-time-safe; the tag layer is optional for FFI.
- **`global` law (D129):** `global` on a TYPE decl (`global exp type`) is the module-type form the owner
  specified — NOT a `global var` (which stays forbidden). NOTE: `global` today means "un-namespaced
  global NAME" (`check_modules.tks:174-232`), NOT "privileged base" — see the FORK below.
- **PRIVILEGED-BASE ENFORCEMENT GATE — FORKED, design-ahead-blocked (see plan §8):** the proposed gate
  is name-collision ("type already defined") barring user redefinition, with the `global` base permitted
  to (re)define/enrich. Mapped to code: the BARRING half is a natural extension of the existing duplicate
  check (`collect.tks:1366` / `check_modules.tks:227`); the PERMITTING half ("base redefines/enriches")
  is NOT deliberated — there is NO type-reopen/enrich mechanic (only trait member-absorb `merge.tks:340`),
  and the current `global`-collision REJECTS a second same-name `global` (the opposite of "base may
  redefine"). Real FORK (HALT): is the permitted case a one-shot single definition (nothing new needed)
  or a genuine reopen (undeliberated merge rules + what supersedes the `global`-collision)? This crumb
  DEFINES the capability + the compiler-base one-shot definition (VALID under any resolution — the
  compiler IS the base); do NOT implement the base-reopen/enrich or the user-vs-base asymmetry here.
- **Byte-preserving on 64-bit (§7b.5):** the newtype lowers as its arch-word backing → `gen2==gen3`.
- **arena-is-Teko (D128):** the optional tag twin `region_alloc_tagged` lands in `arena.tks` (Teko) — the
  old C-vs-Teko fork (DECISION_LOG:680) is DEAD.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`.

## Fixtures

Isolated oracles (a rejection oracle allowed; the mass reball is later):

| fixture | asserts | expected |
|---|---|---|
| `newtype_scalar_methods` | `type X = isize { fn m(): i64 }` type-checks + a method call works (the base capability) | 0 |
| `newtype_header_export` | a `global exp` scalar newtype reaches the `.tkh` (the lifted gap) | 0 |
| `marshall_unwrap_wrap_roundtrip` | `ptr::unwrap<T>(ref x)` then `.wrap<T>()` yields the original; SAME address (no copy) | 0 |
| `marshall_str_bytes_zerocopy` | `ptr::unwrap<[]str>(ref xs).wrap<[]byte>()` shares the SAME base ptr — the flagship | 0 |
| `marshall_uptr_wrap_unwrap` | the identical pair works on `uptr` | 0 |
| `opaque_ptr_no_arithmetic` | `p + 1` / `p[0]` on `ptr` rejected (no arithmetic operators) | EXPECT_COMPILE_FAIL |

## Gate

`[fixpoint]` — compile + the oracles + `gen2==gen3` (the newtype lowers as its arch-word backing →
byte-preserving on 64-bit; retiring the prim surface changes resolution deterministically). "Green" =
`ptr`/`uptr` are Teko newtypes over `isize`/`usize`, `wrap`/`unwrap` type + lower to a bare reinterpret,
the `str`↔`[]byte` flagship is zero-copy, header-export works, arithmetic rejected, `gen2==gen3`.
Reseed-class: `fixpoint-rebuild` (folds into RESEED-1 of `MEM-E5`).

## Deps

`MEM-E0a` (the `isize`/`usize` backing types).

## Done when

`ptr`/`uptr` are `global exp` Teko newtypes over `isize`/`usize` (NOT `PrimKind`), the value-type-newtype
header-export gap is lifted, `ptr::unwrap<T>(ref T): ptr` and `ptr.wrap<T>(): T` (and the `uptr` pair) are
the canonical direct same-base conversion lowering to a bare reinterpret, the `str`↔`[]byte` flagship is
zero-copy, arithmetic is rejected, and the `[fixpoint]` build is `gen2==gen3`.
