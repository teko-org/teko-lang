# The `ref` keyword + a COMPLETE raw-pointer model + OWN-BACKEND-FIRST FFI — EXECUTABLE PLAN

> **Status:** DESIGN-AHEAD, doc-only. **NOT implemented — the owner ratifies this plan before code.**
> **Owner-DECIDED:** the safe reference is the **`ref` KEYWORD**; **`Ref<T>` is compiler-internal and
> INVISIBLE**; **`*` / `&` are `unsafe`-only raw-pointer operators.** Accept(.30)→adopt(.31) discipline
> (like null-union): **.30 BUILDS the new base, seed-safe, coexisting**; **.31 SWAPS the corpus**, then
> flips `&`, then removes the old forms.
>
> **OWN-BACKEND-FIRST — the correction that governs Part B/C/D.** The C-transpile backend is being
> **KILLED in .31–.32**; the **own linker lands .33–.34**. Therefore **relying on `cc` for FFI is a
> trap** — anything that assumes a C preprocessor, a C compiler, or the `cc` link driver **regresses
> the moment `cc` is gone.** So **ALL FFI (raw pointers, macros, varargs, callbacks, struct layout,
> reverse-FFI, the `.h`) MUST have a REAL own-backend path — no honest-stop on the own backend, and
> nothing that needs `cc` to exist** (not even "generate a wrapper the `cc` compiles" — there will be
> no `cc`). The C macro problem is solved by a **teko-native macro resolver** (§4.2); varargs by a
> **per-target own-backend ABI** (§4.3); C-library linking by the **own linker** (§4.5). The still-alive
> C backend is a **transitional emitter to be deleted**, never a capability we bank on.
>
> **Semantic model = `docs/design/ref-transparent-model.md`** (executed here). Companions:
> `docs/design/marshall-spec.md`, `docs/design/memory-unsafe-backend-remodel.md` (§4 the VM/backend
> direction — own AOT backend + linker; C dies), `docs/design/own-backend-architecture.md`,
> `docs/design/null-union-c3-c7-0.3.0.30.md` (the accept→adopt seed dance), `docs/design/
> wave-0.3.1-plan.md`. Verified against `src/`.
>
> **GATE-G (every product crumb):** `teko build . --no-verify --release && ./bin/teko test .` +
> self-host fixpoint (`gen1==gen2`, byte-identical — no gen3) + `diff_vm_native`. Design/fixture crumbs skip the fixpoint.

---

## 0. As-built + the BACKEND TIMELINE that governs sequencing

| fact | today | cite |
|---|---|---|
| safe reference | `Ref<T>`→`Reference{inner}`; deref `.value`→`(*p)` | `resolve.tks`, `ast.tks:117` |
| safe borrow | **`&x` = SAFE Borrow** (AL-F1), corpus-wide | `ast.tks:60`, `parse_expr.tks:471` |
| `ref` keyword | **does not exist** (`BindKind=enum{Let;Mut;Const}`) | `ast.tks:101` |
| `*` operator | **NOT an operator — glyph FREE** | `parse_expr.tks:517` |
| raw ptr internals | `Ptr{inner:Type?}`, `Uptr{}`, `unsafe`; `Ptr{inner=null}`≡opaque `ptr<void>` | `type.tks:102`, `resolve.tks:723` |
| `ptr<T>`/`uptr` surface | **partial**; no deref/index/arrow/arith operators | `resolve.tks:1057` |
| runtime rep | `Reference` and `Ptr` both → bare `T*` | `codegen.tks` |

**The timeline (decisive):**

```
.30 (0.3.0.30)   C backend ALIVE   ── BUILD new surface + own-backend FFI codegen (seed-safe, coexist)
.31 (0.3.1)      C backend DYING   ── SWAP corpus to `ref`, flip `&`; own-backend becomes primary
.32              C backend DEAD    ── no cc, no C preprocessor, no cc link driver
.33–.34          OWN LINKER        ── resolve C-lib symbols (.a/.o/.so) with no cc
```

**Consequence:** every FFI capability must be split into (a) an **own-backend codegen** part that is
own-native and ships as early as the own backend can express it, and (b) — only where unavoidable — a
**own-linker** part (resolving external C symbols) that couples with .33–.34. **No part may depend on
`cc`.** The C-backend emitter for FFI is written ONLY as a throwaway bridge for .30/.31 differential
testing and is deleted with kill-C.

---

## 1. The ratified `ref` surface (post-.31 corpus)

- **`Ref<T>` internal + invisible;** no `.value` at the surface.
- **`ref` keyword, 4 positions:** `mut ref r: T` (**`let ref` illegal** — tripartite), `fn a(ref b: T)`,
  `-> ref T`, **`a(ref x)`** (replaces `&x`).
- **Transparent use** (type-directed auto-deref; `r = v` = R4 write-through).
- **`*` / `&` = `unsafe`-only raw-pointer operators** (§2).

---

## 2. The unified pointer model — COMPLETE raw pointer, zero tension (pure codegen → own-backend-native)

Three disjoint worlds, one grafia each; **all pointer operators are machine instructions the own
backend already emits (loads/stores/address arithmetic) — NONE need `cc`:**

| world | grafia | internal | runtime | derefable? | sigils |
|---|---|---|---|---|---|
| SAFE reference | `ref T` (kw) | `Reference{T}` | `T*` | via auto-deref (safe) | none |
| **REAL raw pointer** | **`ptr<T>`** (T concrete) | `Ptr{inner=T}` | `T*` | **yes — full set** | `*` `&` `->` `[]` `+` `-` |
| OPAQUE handle | `ptr<void>` / `uptr` | `Ptr{inner=null}` / `Uptr` | `void*` / word | **no** (cast first) | none until cast |

**The complete raw pointer is `ptr<T>` itself** (no separate `*T` type — dropped). Full `unsafe`-gated
operator set, each a direct own-backend lowering:

| op | meaning | own-backend lowering | phase |
|---|---|---|---|
| `*p` | deref (read/write) | load/store at `p` | **.30** (glyph free) |
| `p[i]` | `*(p+i)` | `load [p + i*sizeof(T)]` | **.30** |
| `p->f` / `(*p).f` | field via pointer | `load [p + offsetof(f)]` | **.30** |
| `p + n` / `p - n` | element arithmetic | `p ± n*sizeof(T)` | **.30** |
| `p - q` | pointer difference | `(p-q)/sizeof(T)` | **.30** |
| address-of lvalue → `ptr<T>` | take address | `lea`/stack-slot address | **capability .30 (named `addr_of`); `&`-glyph .31** (T1) |
| `null<T>()` / `is_null` | null ptr / test (union-nullable, not `?`) | `0` / `cmp` | **.30** |
| `p to ptr<U>` / `to uptr` / `to ptr<T>` | reinterpret casts | no-op / mov | **.30** |

`ptr<void>` deref is a **compile error** (no pointee) — cast to `ptr<T>` first, exactly as C requires
off `void*`. `ptr<ptr<T>>` (`T**`) legal, uncapped. **Tension elimination:** `ref` (no sigils, safe) ≠
`ptr<T>` (`*`/`&`, unsafe) ≠ `ptr<void>` (opaque, same family, `inner=null`); `*`/`&` never appear in
safe code; Marshall (`wrap`/`unwrap`) is the sole crossing. **All of §2 is own-backend-native and
ships in .30.**

---

## 3. Two-phase strategy + the T1 `&` nuance

**.30 BUILD (seed-safe, coexist):** add `ref` (4 positions), the complete `ptr<T>` operators +
`ptr<void>`/`uptr`, Marshall, and the FFI mechanisms of §4 — **all with a real own-backend codegen
path** — while `Ref<T>`/`.value`/safe-`&x` keep working. `src/` adopts nothing new; seed `0.3.0.29`
still builds gen1. **`&` frozen (safe borrow) in .30.**

**.31 SWAP:** migrate `src/` (`Ref<T>`→`ref`, `.value`→transparent, `&x`→`a(ref x)`); hide `Ref<T>`;
remove old acceptance; **last**, flip `&`→unsafe address-of.

**T1:** `&` cannot be safe-borrow AND raw-address-of simultaneously; the corpus uses `&x` as a borrow,
so the `&`-glyph flip is the **last .31 crumb**, after `&x`→`a(ref x)`. In .30 raw address-of is the
named `marshall::addr_of(x)` / `unwrap(r)` — the raw pointer is COMPLETE without touching `&`.

---

## 4. OWN-BACKEND-FIRST FFI — the mechanisms (each with its real own-backend path)

### 4.1 Raw pointers + Marshall — pure own-backend codegen

§2's operators + `addr_of`/`wrap`/`unwrap`/`swap`/`null`/`is_null`/`to_uptr`/`from_uptr` are loads,
stores, address arithmetic, and reinterprets — **the own backend emits them directly.** No `cc`, no
linker dependency. Ships .30, own-native.

### 4.2 C MACROS without a C preprocessor — the teko-native macro RESOLVER (the hard problem)

`extern macro fn N(params) -> R = "MACRO" from header "h.h"` is resolved at build-time by a
**teko-native resolver** the compiler owns — it reads the named header, finds the `#define`, and
resolves it in **tiers**, never invoking `cc`:

```teko
/**
 * Binds a C preprocessor macro as a typed function, resolved by teko's OWN macro resolver
 * (no C preprocessor). The `from header "h.h"` names the header the resolver reads. The dev
 * ASSERTS the signature; the resolver classifies the `#define` into one of four tiers (below)
 * and lowers to own-backend IR — NEVER to a `cc` invocation.
 *
 * @param u32 x  the argument
 * @return u32   the asserted result
 * @since 0.3.1
 */
extern macro fn htonl(x: u32) -> u32 = "htonl" from header "arpa/inet.h"
```

- **Tier 0 — object-like / CONSTANT macro** (`O_RDONLY`, `INT_MAX`, `SOCK_STREAM`, flag bits — the
  MOST COMMON case). The resolver reads the header, extracts `#define NAME <tokens>`, and **evaluates
  the C constant-expression** with a **mini constant evaluator** (integer/char/hex literals, `<< >> | &
  ^ ~ + - * / ( )`, and references to other object-like macros it recursively resolves). The value is
  inlined as a typed Teko constant. **Fully own-native, zero runtime, ships in .30** (no IR needed —
  just a value). This alone covers header CONSTANTS and FLAGS entirely.
- **Tier 1 — symbol-alias macro** (`#define htonl(x) __bswap_32(x)` — the body is a single call to a
  real linkable symbol, params forwarded). The resolver detects the wrapped symbol and **binds the
  `extern macro fn` to that real symbol** — it becomes an ordinary `extern fn` call, resolved by the
  own linker (§4.5). Own-native.
- **Tier 2 — simple-body EXPANSION** (`#define htonl(x) ((((x)&0xff)<<24)|...)` — a pure
  arithmetic/bitwise/shift/ternary expression over the parameters, the real low-level idiom). The
  resolver parses the macro body as a **C expression** and **translates it to own-backend IR** (a
  bounded C-expr→Teko-IR compiler: params, integer literals, Tier-0 constants, the arithmetic/
  bitwise/shift/relational/ternary operators, casts to sized integer types). It is inlined at each
  call and compiled by the own backend. **Own-native, no `cc`.** Covers htonl/htons/bit-twiddling.
- **Tier 3 — arbitrary/complex C macro** (statements, side effects, token-paste `##`, stringize `#`,
  calls into other complex macros, casts to C aggregate types). **HONEST ERROR — and ONLY here:**
  *"this C macro is not mechanically resolvable; provide a Teko equivalent or bind a real linkable
  symbol (`extern fn = \"sym\"`)."* This is the single honest-stop, reserved for genuinely arbitrary C
  — NOT the common constant/flag/bit-twiddle cases.

**Subsystem cost (real, bounded):** a minimal C header tokenizer + `#define` extractor, a C
constant-evaluator (Tier 0), a symbol-detector (Tier 1), and a small C-expression→IR translator (Tier
2). Sizeable but finite, and it makes `extern macro` REAL on the own backend for every case a
low-level dev actually uses. **Sequencing:** Tier 0 ships in .30 (value inlining, no IR dependency);
Tiers 1–2 land as own-backend IR expansion matures (couple with the own backend); Tier 3 is the
always-present honest error.

### 4.3 VARARGS — a per-target own-backend ABI (not delegated to any C compiler)

`extern fn printf(fmt: *byte, ...) -> i32 = "printf"` (`...` = C-ABI variadic tail, extern-only,
trailing; default argument promotions `f32→f64`, sub-`int`→`int`). The **own backend implements the
platform varargs calling convention itself**:

- **SysV AMD64:** integer/pointer args in `RDI,RSI,RDX,RCX,R8,R9` then stack (right-to-left); floats in
  `XMM0–7`; **`AL` = number of vector registers used** for the variadic call; overflow to the stack
  with 16-byte alignment.
- **AArch64 AAPCS64:** `x0–x7`/`v0–v7`, named args in registers, **variadic args passed on the stack**
  (Apple's darwin variant differs — variadics always on stack); honor the platform variant.
- **Windows x64:** `RCX,RDX,R8,R9` + 32-byte shadow space; **each vararg float is duplicated into the
  matching GPR** (the Win64 vararg rule); overflow to stack.

This is a **real own-backend codegen crumb**, sequenced with the own backend's call lowering (it
already lowers fixed-arg calls; varargs adds the promotion + AL/shadow/stack rules). **No `cc`
delegation.** The transitional C-backend emits `printf(...)` verbatim ONLY for .30/.31 differential
oracle; that emitter dies with C.

### 4.4 Struct layout, callbacks, calling conventions, errno — own-backend codegen

- **`#repr("c")` + `extern union`** (G2): the own backend **lays out the aggregate per the target C
  ABI** (field order preserved, natural alignment/padding, no Teko reordering) when `#repr("c")` is
  present; a non-`#repr` aggregate crossing FFI is a compile error. Own-native layout algorithm — no
  `cc`.
- **`cabi` fn-pointer callbacks** (G3): only NON-capturing closures/top-level fns coerce (env-first ABI
  dropped; capturing = reject); the own backend **emits a plain C-ABI function and takes its address**
  — native. User-data is an explicit `ptr<T>`/`uptr` param.
- **`#cconv("stdcall")`** (G4): the own backend emits the named calling convention (stdcall/… callee
  cleanup, name decoration) itself; `teko::ffi::errno()` reads the thread-local `errno` via the
  runtime (`teko_rt` TLS access) — native. Weak/versioning flagged follow-on.

### 4.5 Linking C libraries/objects — the OWN LINKER (.33–.34), surface early / link late

`extern from lib "L" from header "h.h" { … }` (G1, grouped extern-corpus block) and any `extern fn =
"sym"` ultimately need the **external C symbol resolved against a `.a`/`.o`/`.so`**. The **own linker
(.33–.34) does this — NOT `cc`:** it reads archive/object/shared formats (the backend already emits
ELF/Mach-O/COFF objects — `objfile_*`), resolves undefined symbols against the named libs, and
produces the final image. **Split:**
- **.30 (surface + compiler-side reference):** parse the grouped block + `[extern.headers.<os>]` /
  `[extern.libs.<os>]`; emit the undefined-symbol references + the relocation entries in the object.
  This is own-native (object emission already exists).
- **.33–.34 (resolution):** the own linker resolves those references against the C libs without `cc`.
  In the .30–.32 interim the platform linker/`cc`-driver bridges the final link ONLY as throwaway
  scaffolding — the DESIGN targets the own linker, and no capability assumes `cc` compiles anything.

This is the **one FFI capability genuinely coupled to the own-linker epic** (symbol *resolution*);
everything else (codegen) is earlier and own-native.

### 4.6 Reverse-FFI (`abi="c"`, `#[export]`) + the MANDATORY `.h` — own-native

- **`emit_c_header` — the `.h` (MANDATORY, .30):** generating the header is **pure text emission from
  the checked program** — real C prototypes for every `#[export]` fn + **`#define`s for exported
  `const`s** (this is "CREATING C macros callable from C") — **completely backend-independent, needs
  no `cc` and no linker.** Ships in .30; also **closes the `extern type` header honest-stop**
  (`header.tks:91`, **G5**).
- **`[artifact] abi="c"` + `#[export("sym")]` (.30 codegen; artifact packaging couples own linker):**
  the own backend **emits the exported symbol with the C ABI, unmangled** (FFI-safe signature gate: no
  capturing closures; a `ref` is `unwrap`ped to `ptr<T>` at the boundary; aggregates need `#repr("c")`;
  no Teko `params []T` variadic export). Packaging the `.a`/`.so`/`.dylib` uses the own
  archiver/linker (.33–.34); the object + symbol emission is own-native and ships in .30.
- **`teko_rt_init`/`teko_rt_shutdown`** (guarded auto-init; arena process-root) + **panic-must-not-
  unwind-into-C** (a panicking `#[export]` fn returns `… | error`) — own-native runtime, .30.

```teko
/**
 * Exported to C as the stable unmangled symbol `teko_add` (C ABI, no environment), emitted by
 * the OWN backend. The generated `.h` (§4.6) carries `int64_t teko_add(int64_t, int64_t);`.
 * Only FFI-safe scalar / `ptr<T>` / `#repr("c")`-aggregate params and returns are allowed.
 *
 * @param i64 a  first addend
 * @param i64 b  second addend
 * @return i64   the sum
 * @since 0.3.0.30
 */
#[export("teko_add")]
pub fn add(a: i64, b: i64) -> i64 { a + b }
```

---

## 5. Re-sequenced (own-backend-first): what ships when

| capability | own-backend codegen | own-linker part | phase |
|---|---|---|---|
| `ref` keyword + transparent use | reuse `Reference→T*` | — | **.30 build / .31 adopt** |
| complete `ptr<T>` ops (`*`,`[]`,`->`,`+`,`-`, `addr_of`, casts, null) | direct instructions | — | **.30** (own-native) |
| Marshall (`wrap`/`unwrap`/`swap`/bridge) | reinterprets | — | **.30** |
| `extern macro` Tier 0 (constants/flags) | value inline | — | **.30** (resolver, no cc) |
| `extern macro` Tier 1–2 (symbol-alias / body-expand) | IR expansion | Tier 1 needs symbol link | **.31+ as IR matures** (Tier 1 link → .33–.34) |
| `#repr("c")` / `extern union` layout | own layout algo | — | **.30** |
| `cabi` callbacks | emit C-ABI fn + addr | — | **.30** |
| `#cconv` / `errno` | own cconv emit / TLS | — | **.30** (common cases) |
| C-ABI variadics `...` | **per-target vararg ABI** | — | **own-backend crumb** (with own-backend call lowering; .31±) |
| `extern from lib` (grouped block, G1) | undef-symbol refs + relocs | **resolve vs `.a`/`.o`/`.so`** | surface **.30**; resolution **.33–.34** |
| `abi="c"` + `#[export]` | emit unmangled C-ABI symbol | **package `.a`/`.so`** | codegen **.30**; packaging **.33–.34** |
| `emit_c_header` `.h` (MANDATORY, G5) | pure text | — | **.30** (backend-independent) |

**The only FFI parts coupled to the own linker (.33–.34)** are external-C-symbol *resolution* and lib
*packaging*; **the only part coupled to own-backend call-lowering** is the vararg ABI; **everything
else is own-native and ships .30.** The transitional C emitter (for differential testing) is deleted
with kill-C — **no capability regresses when `cc` dies.**

---

## 6. Phase .30 crumbs (BUILD; additive; seed-safe; every FFI crumb has a real own-backend path)

New `src/` modules are written in the OLD surface (seed `0.3.0.29` parses them); they swap in .31.

**Part A — `ref` keyword:** A0 soundness gate (design) · A1 contextual `ref` lexer · A2 parser (4
positions + type-grafia + `by_ref`/`RefArg`) · A3 checker (`ref`→`Reference`, `let ref` reject,
auto-deref, never-null, depth-cap-2, A4, `a(ref x)` borrow-down, C5 niche→pointee) · A4 codegen (reuse
`Reference→T*`) · A5 coexistence with `Ref<T>`/`.value`/`&x`.

**Part B — complete `ptr<T>` (own-native codegen):** B1 `*p` deref · B2 `p[i]`/`p->f`/`(*p).f` · B3
arith `p+n`/`p-n`/`p-q` · B4 `ptr<T>` typing (unconditional `Ptr`/`Uptr` unsafe-carrying; `ptr<void>`
deref-reject; union-nullable; casts; migrate `teko::mem` prims to `unsafe fn`) · B5 `src/marshall/*`
(`addr_of`/`wrap`/`unwrap`/`swap`/`null`/`is_null`/`to_uptr`/`from_uptr`).

**Part C — FFI mechanisms (own-native paths):** C1 **macro resolver Tier 0** (header read + constant
evaluator; `[extern.headers.<os>]` knob) — REAL constants/flags, no cc · C2 grouped `extern from
lib/header { … }` **surface + undef-symbol/reloc emission** (G1; resolution is §8) · C3 `#repr("c")` +
`extern union` **own layout** (G2) · C4 `cabi` callbacks (G3) · C5 `#cconv` + `teko::ffi::errno()`
(G4) · C6 variadic `...` **surface + checker promotions** (the own-backend vararg ABI lowering is §8).

**Part D — reverse-FFI:** D1 `[artifact] abi="c"` + `#[export]` **own-backend C-ABI symbol emission** +
FFI-safe gate · D2 **`emit_c_header` — the `.h`** (MANDATORY; prototypes + `#define` consts; close G5)
· D3 `teko_rt_init`/`shutdown` + panic-not-into-C · D4 fixtures.

GATE-G on every product crumb. A0/D4 skip fixpoint. The .30 merge cuts the `0.3.0.30` seed.

## 7. Phase .31 crumbs (SWAP)

A31.1 corpus `Ref<T>`→`ref` + `.value`→transparent (114 sites; fixpoint+diff per file; self-host
critical) · A31.2 `&x`→`a(ref x)` swap · A31.3 hide `Ref<T>` + remove `.value`/old-`Ref<T>`/safe-`&x`
acceptance · A31.4 **FLIP `&`→unsafe raw address-of** (alias of `addr_of`; LAST, T1) · A31.5 fixtures.

## 8. Own-backend / kill-C / own-linker coupled crumbs (real, no `cc`)

- **KC1 — vararg ABI (own backend):** per-target SysV/AArch64/Win64 varargs lowering (§4.3) — with the
  own-backend call-lowering completion (SW13-adjacent). Powers C6's `...` on the own backend.
- **KC2 — macro resolver Tiers 1–2 (own backend IR):** symbol-alias binding + simple-body→IR expansion
  (§4.2) — as the own-backend IR expansion path is ready. Tier 0 already shipped (.30).
- **KC3 — own linker C-symbol resolution (.33–.34):** resolve `extern`/`extern from lib` undefined
  symbols against `.a`/`.o`/`.so`; package `abi="c"` `.a`/`.so`/`.dylib` (§4.5). Replaces the `cc` link
  bridge; the surface + object emission already shipped (.30).
- **KC4 — delete the transitional C-FFI emitter** with kill-C: remove the throwaway C-backend FFI
  emission once the own-backend paths (KC1–KC3) are green; confirm NO capability regresses (the
  fixtures of §11 must pass on the own backend alone).

Each KC crumb rides GATE-G; KC4 is the ritual proof that the own backend stands alone.

## 9. GATE-G & fixpoint

`teko build . --no-verify --release && ./bin/teko test .` + fixpoint + `diff_vm_native`. In .30 `src/`
is unchanged, so fixpoint holds trivially (additions inert). In .31 each swap crumb holds fixpoint on
the new spelling. **When the own-backend FFI paths land (KC), the differential migrates from
VM/C-backend to own-vs-C-backend and then, at kill-C, to own-backend-only** (remodel §4). A0/D4/A31.5
skip fixpoint.

## 10. Seed-safety (.30 built by seed `0.3.0.29`)

`src/` uses NO new surface in .30 (accept-only). `ref` contextual (no reserved token). `*`-deref uses
a free glyph; `p[i]`/`p->f`/arith are new ops on `ptr` types unused by `src/`. `&` frozen (borrow).
New modules (marshall, ffi, macro-resolver, c-header) written in OLD surface. FFI-prim `unsafe fn`
migration only adds the modifier (seed parses it); callers `rawbuf_*` already unsafe. **The macro
resolver + `.h` emitter are ordinary compiler code (no new surface) → seed-safe.** Never remove an old
form in .30 (that is .31). The .30 merge cuts `0.3.0.30`; .31 dogfoods it.

## 11. Fixtures (own-backend-validated, not cc-validated)

Every FFI fixture must pass **on the own backend** (once its KC crumb lands) — a fixture that only
passes via the C emitter is a regression trap. Native oracle = own backend.

**.30 accept:** `ref_binding_ok`/`ref_param_ok`/`ref_return_ok`/`ref_use_transparent_ok`/
`ref_optional_pointee_ok`; `old_surface_still_ok` (coexistence); `let_ref_rejected`/
`ref_uninit_rejected`/`ref_depth_cap3_rejected` (COMPILE_FAIL). Pointers: `ptr_deref_ok`/
`ptr_index_ok`/`ptr_arrow_ok`/`ptr_arith_ok`/`ptr_addr_of_ok`; `ptr_deref_in_safe_rejected`/
`ptr_void_deref_rejected`/`ptr_optional_question_rejected`. `marshall_swap_values` (VM==native)/
`marshall_wrap_null_panics`. FFI: `macro_const_flag_ok` (**Tier 0, own backend, no cc** — e.g.
`O_RDONLY`), `macro_htonl_expand_ok` (**Tier 2, own backend**), `macro_complex_rejected` (Tier 3
honest error); `repr_c_struct_byval` (own layout), `cabi_callback_noncapturing` (+ `_capturing_rejected`),
`ffi_errno_read`. Reverse: `revffi_c_header_emitted` (the `.h` — pure text, no cc), `revffi_export_add`,
`revffi_rt_init_contract`; `revffi_export_nonffisafe_rejected`.
**Own-backend coupled:** `variadic_printf_ownbackend` (KC1, own-backend vararg ABI),
`macro_symbol_alias_ok` (KC2 Tier 1), `extern_lib_link_ownlinker` (KC3, own linker resolves a C `.a`).
**.31 swap:** `ref_selfhost_ok`; `amp_is_unsafe_addressof_ok`/`amp_in_safe_rejected`;
`refT_surface_rejected`.

## 12. Law / Constitution tensions (resolved law-first)

- **T1 [CRITICAL, ordering] — `&` borrow vs raw address-of.** Freeze `&` in .30 (raw address-of ships
  as named `addr_of`); flip the glyph LAST in .31 (A31.4). Not a HALT.
- **T0 [CRITICAL, corrected] — FFI must be OWN-BACKEND-REAL, not cc-backed.** The prior draft leaned on
  the C backend; **corrected:** every FFI capability has an own-backend path (macro resolver §4.2,
  vararg ABI §4.3, own-linker resolution §4.5, own-native codegen for the rest); **no honest-stop on
  the own backend and nothing that needs `cc` to exist** (the wrapper-symbol-compiled-by-`cc` idea is
  DEAD). The C emitter is throwaway scaffolding deleted at kill-C (KC4). Not a HALT.
- **T2 [HIGH] — corpus swap self-host-critical (A31.1).** File-by-file GATE-G + honest-stop. Not a HALT.
- **T3 [MEDIUM] — auto-deref/depth-cap-2 soundness.** A0 gate before A3. Not a HALT.
- **T4 [MEDIUM] — C5 `Ref<T>|null` niche re-interpretation.** Niche→pointee, flow-narrow disabled
  across ref (ref-model §4.2); additive .30, old niche removed .31. Not a HALT.
- **T5 [MEDIUM] — the C-macro resolver is a real subsystem, bounded.** Risk: Tier 2's C-expr→IR
  translator scope-creeps. **Resolved:** hard-bound Tier 2 to arithmetic/bitwise/shift/relational/
  ternary/cast-to-sized-int over params + literals + Tier-0 constants; anything past that is Tier 3
  (honest error). The common low-level cases (constants, flags, byte-swaps) are covered; arbitrary C
  is honestly refused. Not a HALT.
- **T6 [MEDIUM] — vararg ABI is per-target, real work.** **Resolved:** KC1 implements SysV/AArch64/
  Win64 with the own backend; sequenced with own-backend call lowering; the C emitter bridges only for
  the .30/.31 differential and is deleted at kill-C. Not a HALT.
- **T7 [MEDIUM] — external C-symbol resolution needs the own linker (.33–.34).** **Resolved:** split —
  surface + undef-symbol/reloc emission in .30 (own-native), resolution in the own linker; the `cc`
  link driver is a throwaway interim bridge, never a banked capability. This is the one genuine
  coupling to the linker epic; flagged, not a HALT.
- **T8 [LOW] — reverse-FFI init / panic-into-C.** Explicit `teko_rt_init/shutdown` + `… | error`. Not a HALT.

**No unresolved tension → NO HALT.** REPORTED: the `*T`-type idea is dropped (raw pointer is `ptr<T>`);
marshall signatures spell `ref T`; marshall gains `addr_of`; **the FFI plan is own-backend-first and
carries no `cc` dependency** — the previously-floated "cc-compiles-a-wrapper" macro fallback is removed.

---

## Appendix — file/impact map (absolute paths)

- **.30 `ref`:** `/home/user/teko-lang/src/lexer/lexer.tks`, `.../parser/ast.tks`,
  `.../parser/parse_{decl,stmt,type,expr}.tks`, `.../checker/{resolve,typer}.tks`,
  `.../codegen/codegen.tks`.
- **.30 complete `ptr<T>`:** `.../parser/parse_expr.tks` (`*p`/`p[i]`/`p->f`/`p+n`),
  `.../checker/resolve.tks` (`Ptr`/`Uptr` unsafe; `ptr<void>` deref reject; casts),
  `.../codegen/codegen.tks`, new `/home/user/teko-lang/src/marshall/marshall.tks`.
- **.30 FFI mechanisms:** new `/home/user/teko-lang/src/ffi/macro_resolver.tks` (header tokenizer +
  `#define` extractor + constant evaluator + C-expr→IR translator, Tiers 0–2), new
  `/home/user/teko-lang/src/ffi/ffi.tks` (`errno`), `.../parser/parse_decl.tks` (`extern macro`, `...`,
  grouped `extern`, `#repr`/`extern union`, `cabi`, `#cconv`), `.../build/manifest.tks`
  (`[extern.headers]`), `.../codegen/codegen.tks` (own layout, cabi, cconv, vararg promotions),
  `.../runtime/teko_rt.{c,h}` (maintained-C errno read).
- **.30 reverse-FFI + `.h`:** `.../build/manifest.tks` (`abi="c"`), `.../checker/*` +
  `.../codegen/codegen.tks` (`#[export]` unmangled C-ABI + FFI-safe gate),
  `/home/user/teko-lang/src/emit/header.tks` (**`emit_c_header`**; close `header.tks:91`),
  `.../runtime/teko_rt.{c,h}` (`teko_rt_init/shutdown`).
- **.31 swap:** `/home/user/teko-lang/src/**`; `parse_expr.tks`+`checker`+`codegen` for `&`-flip;
  `resolve.tks` to hide `Ref<T>`.
- **Own-backend / kill-C / linker (KC):** `/home/user/teko-lang/src/backend/*` (per-target vararg ABI;
  own layout; cconv), `/home/user/teko-lang/src/lir/lower.tks` (vararg lowering; macro-body IR),
  `/home/user/teko-lang/src/backend/objfile_{elf,macho,coff}.tks` (undef symbols/relocs; the own linker
  resolution + `abi="c"` packaging).
- **Companions:** `/home/user/teko-lang/docs/design/ref-transparent-model.md`,
  `.../marshall-spec.md` (`ref T` + `addr_of`), `.../memory-unsafe-backend-remodel.md` (§4 C dies),
  `.../own-backend-architecture.md`, `.../null-union-c3-c7-0.3.0.30.md`, `.../wave-0.3.1-plan.md`.

*Design-ahead, doc-only. No product code changed. Own-backend-first; no `cc` dependency. Owner
ratifies before implementation.*
