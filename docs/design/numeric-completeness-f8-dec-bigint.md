# Numeric Completeness — `f8`, `dec`, `bigint`

> **Status:** DESIGN-AHEAD, doc-only. Owner-ordered 2026-07-22, rulings 2026-07-23.
> Architect plan (crumbs + contracts + fixtures + sequencing + law tensions). **No product
> code.** The owner ratifies the f8 format and the dec/bigint surface before any crumb opens.
>
> **Anchors (read for this plan; every contract below is against these exact shapes):**
> - `src/checker/type.tks` — `PrimKind` enum (`:11`), `prim_is_float` (`:23`), `prim_is_int`
>   (`:28`), `prim_is_signed` (`:39`), `prim_width` (`:47`), `Prim`/`Type` (`:69`,`:102`),
>   `type_eq` (`:119`).
> - `src/emit/tkb_write.tks` — `prim_byte` (`:9`, `k to byte` = enum ordinal), `write_type` (`:16`).
> - `src/emit/tkb_read.tks` — `prim_of` (`:77`, explicit `if b == N` chain), `read_type` (`:110`).
> - `src/lir/lir.tks` — `LType` enum (`:18`), `LUnOp` incl. `FToF`/`IToF`/`FToI` (`:41`),
>   `LConstFloat` (`:47`), `ltype_size` (`:533`), `ltype_align` (`:555`).
> - `src/lir/lower.tks` — `ltype_of_prim` (`:183`), `ltype_of_int_prim` (`:196`),
>   `ltype_of_prim_name` (`:224`).
> - `src/codegen/codegen.tks` — prim→C type (`:330`), prim spelling (`:1122`,`:1160`,`:1857`),
>   `cg_prim_is_float` (`:2009`), `prim_float_tag` (`:2101`), float `/` and `%` rulings (`:2144`).
> - `src/backend/isel_x86_64.tks` (and `isel_arm64.tks`, `isel_riscv.tks`) — the **`B1-fp`
>   float honest-stop family**, uniformly deferred to **0.3.1** (`:197`,`:325`,`:492`,`:522`,…).
> - `src/runtime/teko_rt.{c,h}` — maintained-C runtime seed: `tk_alloc`/arena (`teko_rt.h:124`,
>   `:155`), `__int128` alignment discipline (`:113`), `tk_str`/`tk_slice_*` immutable-buffer
>   pattern, `tk_div`/`tk_rem`/`tk_int_to_float` (`:551`).
> - `docs/design/wave-0.3.1-plan.md` — the seed-chain rule §1 (`:48`: *no sub-wave may use, in
>   `src/`, a surface the previous seed cannot parse*); `own-backend-architecture.md`;
>   `literal-context-typing.md`.

---

## 0. Executive summary (path + the two decisions the owner ratifies)

1. **f8 — RECOMMEND: two distinct nominal prims `f8e4m3` and `f8e5m2`, NO bare `f8`.** Land
   **`f8e4m3` first** (OCP OFP8 / "ML" encoding: 1-4-3, bias 7, subnormals, **no infinities**,
   single NaN, max ±448); **`f8e5m2` as the immediate follow** (1-5-2, bias 15, IEEE-754-shaped:
   ±inf + NaN, max ±57344). Tradeoff in §2. The two encodings are non-interconvertible bit
   layouts; a bare `f8` would force a silent, lossy default exactly at the driver register
   boundary where the encoding is load-bearing. Distinct types make a format mismatch a
   **compile error** — the property a low-level driver wants.
2. **f8 as a TYPE (storage + conversions + software-emulated arithmetic via the C backend)
   lands EARLY** (own crumb train, can start in .30). **f8 arithmetic own-native rides the
   `B1-fp` float/FPR epic (0.3.1)** — but note §3.6: f8 scalar arithmetic is *always* f32-substrate
   software rounding, so f8 adds **no new FPR arithmetic** beyond f32; f8 storage/convert is
   integer-domain and needs the integer isel only.
3. **`dec` and `bigint` — RECOMMEND: native surface types (`dec`, `bigint`) with literals +
   operators, backed by a PURE-TEKO runtime** (`teko::math::bigint`, `teko::math::dec` over
   `[]u64` limbs), **not** by new `teko_rt.c` intrinsics — so they survive kill-c. They are
   **runtime-backed, not `Prim` scalars** (they are NOT in `PrimKind`). Larger features;
   sequenced **after** the f8 train and after the arena/region foundation (SW1 of 0.3.1). §5.
4. **f128 — DROPPED per ruling. Not designed here.**

---

## 1. The current numeric model (what exists, verbatim from the code)

`PrimKind` (`type.tks:11`) is the closed scalar set, ordinals fixed by declaration order and
serialized as that ordinal (`tkb_write.tks:9` `k to byte`; `tkb_read.tks:77` inverse):

| ordinal | 0..4 | 5..9 | 10 | 11 | 12 | 13 |
|---|---|---|---|---|---|---|
| kind | `U8 U16 U32 U64 U128` | `I8 I16 I32 I64 I128` | `F16` | `F32` | `F64` | `Bool` |

Family membership is by **`match`**, never by ordinal (`prim_is_float:23`, `prim_is_int:28`,
`prim_is_signed:39`) — so declaration position is purely a `.tkb` serialization concern.
`prim_width:47` returns 8/16/32/64/128 (ints), 16/32/64 (floats), 1 (bool). The float `/`
lowers to `tk_div_f{16,32,64}` (`codegen.tks:2152`, panic on ÷0), float `%` is rejected
(`:2144`). `LType` (`lir.tks:18`) mirrors this: `I8..I128; F16 F32 F64; Ptr; Void`. The
own-backend isel **stops honestly on every float case** with the `B1-fp` tag, deferred to
**0.3.1** — that is the "float/FPR epic" f8 arithmetic couples to.

`dec`/`bigint` are named as **deferred, runtime-backed** in `type.tks:6`. They have **no**
`PrimKind`, no `Type` case, no lowering today.

---

## 2. f8 — format research, recommendation, and the explicit tradeoff

### 2.1 The two incompatible 8-bit floats

| | **E4M3** (recommend #1) | **E5M2** (recommend #2) |
|---|---|---|
| layout | 1 sign · 4 exp · 3 mant | 1 sign · 5 exp · 2 mant |
| exponent bias | 7 | 15 |
| infinities | **none** (range-extended) | ±inf (IEEE-754-shaped) |
| NaN encodings | 1 (`S.1111.111`) | 2-bit mantissa NaNs + inf |
| max finite (OCP OFP8) | **±448** | **±57344** |
| smallest subnormal | 2⁻⁹ | 2⁻¹⁶ |
| relative precision | **higher** (3 mant bits) | lower (2 mant bits) |
| dynamic range | narrower | **wider** |
| shipping HW role | weights / activations / inference | gradients / training / wide-range transport |

**Two lineages, genuinely incompatible (the owner's "IEEE-P3109 vs ML" note):** the **OCP OFP8**
("ML") spec — implemented by NVIDIA Hopper/Blackwell, AMD, Intel Gaudi tensor units — and the
draft **IEEE P3109** family diverge on NaN/inf/signed-zero encoding. A driver must byte-match the
device register, so the encoding is not a free choice.

### 2.2 Recommendation: `f8e4m3` + `f8e5m2` as distinct prims; adopt **OCP OFP8** semantics

- **Distinct types, not a bare `f8`.** In a systems language for an OS/drivers, the exact 8-bit
  encoding is load-bearing at the register boundary. A bare `f8` (defaulting to one format) makes
  every DMA/interchange to a device of the *other* format silently wrong, and cripples one HW
  class. Two nominal prims make a cross-format store a **compile error** (via `type_eq:119`,
  which already discriminates `Prim` by `kind`).
- **Adopt OCP OFP8, not P3109-draft.** A driver talks to *shipping* silicon; OCP OFP8 is what
  ships. Flag for owner: if a target device implements P3109 encoding instead, that is a *third*
  type later — do not conflate. **← owner ratifies OFP8 vs P3109 here.**
- **Land order E4M3 → E5M2.** If forced to one first: **E4M3** — an OS/kernel more often
  *originates* precision-bound payloads (activations, compact reals) than training gradients;
  its extra mantissa bit gives materially better relative precision and ±448 covers most
  non-training data. E5M2 follows for the range-hungry / gradient / transport path.

### 2.3 The tradeoff, stated plainly

- **Cost of "both distinct types":** two `PrimKind`s, two `LType`s, a **doubled conversion
  matrix** (each of {f8e4m3,f8e5m2} ↔ {f16,f32,f64} + cross-f8), two literal-typing paths, two
  software-emulation runtime families, ~2× the fixtures.
- **Rejected — bare `f8` = E4M3 only:** smallest surface, but silently wrong at the boundary and
  blocks range-hungry devices.
- **Rejected — `f8` aliasing E4M3 + a separate `f8e5m2`:** asymmetric; invites readers to treat
  `f8` as "the generic 8-bit float" in a domain where there is no generic 8-bit float.
- **Chosen — symmetric distinct pair:** costs surface, buys a compile-time format firewall and
  honest interop. For a language that will host an OS, correctness at the register wins.

---

## 3. f8 — the full cascade contract (against the anchored shapes)

### 3.1 `PrimKind` placement — the ordinal tension, resolved law-first

**Tension.** The B.38 rubric says "append within family, **Bool stays LAST**, perturb no existing
ordinal." You cannot simultaneously (a) keep the floats contiguous, (b) keep Bool last, and (c)
keep every existing ordinal (incl. Bool=13) stable — inserting f8 into the float block shifts
Bool 13→15, and Bool=13 is baked into the **seed's** `.tkb` codec.

**Resolution (law-first — `.tkb`/seed stability beats source aesthetics; `tkb_read.tks:76`
"old tags stable").** **Append the f8 kinds AFTER `Bool`**, ordinals **14/15**. Every existing
ordinal — including `Bool=13` — is preserved, so old `.tkb` and the seed reader stay valid. The
"Bool last / floats contiguous" wording is a *source-ordering* aesthetic that ordinal stability
overrides; family membership stays correct because the predicates are `match`-based, not
ordinal-based. **← owner ratifies this departure from the B.38 wording.**

```teko
/**
 * The injected scalar primitives. Native numeric set (B.38 + NUMERIC-COMPLETENESS):
 * the f8 float prims are APPENDED AFTER `Bool` (ordinals 14/15) — NOT inserted into the
 * float block — so every existing ordinal (U8..F64 = 0..12, Bool = 13) is preserved and the
 * seed's `.tkb` codec stays valid. Family membership is by `match` (prim_is_float), never by
 * ordinal, so the trailing position is purely a serialization concern. `dec`/`bigint` are NOT
 * here — they are runtime-backed library types (see numeric-completeness doc §4/§5).
 *
 * @since NUMERIC-COMPLETENESS
 */
pub type PrimKind = enum {
    U8; U16; U32; U64; U128
    I8; I16; I32; I64; I128
    F16; F32; F64
    Bool
    F8E4M3; F8E5M2
}
```

### 3.2 Predicates + width

```teko
/**
 * True for the IEEE / OCP-OFP8 float prims (F16/F32/F64 and the 8-bit F8E4M3/F8E5M2).
 * f8 IS a float family member despite its trailing ordinal — membership is by match.
 *
 * @param k  the primitive kind to test
 * @return true iff k is a floating-point prim
 * @since NUMERIC-COMPLETENESS
 */
fn prim_is_float(k: PrimKind) -> bool {
    match k { F16 => true; F32 => true; F64 => true; F8E4M3 => true; F8E5M2 => true; _ => false }
}

/**
 * True iff the float prim is an 8-bit OCP-OFP8 format. Used by codegen (no native C type →
 * software emulation) and by isel (f8 arithmetic routes through the f32 substrate, never a
 * native f8 FPR op).
 *
 * @param k  the primitive kind to test
 * @return true iff k is F8E4M3 or F8E5M2
 * @since NUMERIC-COMPLETENESS
 */
fn prim_is_f8(k: PrimKind) -> bool {
    match k { F8E4M3 => true; F8E5M2 => true; _ => false }
}
```

`prim_width` (`type.tks:47`) — add `F8E4M3 => 8; F8E5M2 => 8`. `prim_is_int`/`prim_is_signed`
are **unchanged** (f8 is neither). `type_eq` (`:119`) needs **no** change — `Prim` already
compares by `kind`, so `f8e4m3 != f8e5m2 != f16` falls out for free.

### 3.3 `.tkb` serialization

- **Write side (`tkb_write.tks:9`) — NO change.** `prim_byte(k) = k to byte` emits the ordinal;
  14/15 serialize automatically once the enum has them.
- **Read side (`tkb_read.tks:77` `prim_of`) — add explicit tags.** `prim_of` currently returns
  `Bool` as the *implicit else*; with 14/15 present, byte 13 must become an explicit `Bool` case
  or 14/15 misread as `Bool`:

```teko
/**
 * Inverse of prim_byte (PrimKind ↔ its enum ordinal). Ordinals: U8..U128 0..4, I8..I128 5..9,
 * F16/F32/F64 10..12, Bool 13, F8E4M3/F8E5M2 14/15 (appended-after-Bool; old tags stable).
 * Byte 13 is now EXPLICIT (was the implicit else) so the trailing f8 tags do not misread as Bool.
 *
 * @param b  the ordinal byte read from the .tkb stream
 * @return the decoded PrimKind (falls back to Bool on an unknown byte — forward-compat guard)
 * @since NUMERIC-COMPLETENESS
 */
fn prim_of(b: byte) -> checker::PrimKind {
    // … existing 0..12 …
    if b == 13 { return checker::PrimKind::Bool }
    if b == 14 { return checker::PrimKind::F8E4M3 }
    if b == 15 { return checker::PrimKind::F8E5M2 }
    checker::PrimKind::Bool
}
```

### 3.4 Lexer + literals + literal-context typing

- **No new suffix** (matches `lexer.tks:202`: default `f64`, annotation picks the width). A f8
  literal is a normal float token typed by its annotated slot: `let w: f8e4m3 = 0.375`.
- **Names.** `f8e4m3` / `f8e5m2` are new primitive spellings recognized wherever `f16`/`f32`/`f64`
  are (`codegen.tks:1857`, `lower.tks:224` `ltype_of_prim_name`, plus the checker's type-name
  resolver / `scope.tks`).
- **Constant rounding (comptime).** Recommend a **pure-Teko** `f8e4m3_from_f64` /
  `f8e5m2_from_f64` (round-to-nearest-even, saturate/NaN per format) in the checker's
  comptime-fold path (`comptime_fold.tks`), so a literal in a typed f8 slot folds to the **exact
  rounded 8-bit value** and the LIR carries a settled constant (no emit-time surprise). This is
  the same "fold at comptime, don't defer to C" discipline the u128 fold already follows.

### 3.5 Conversions + promotion rank

- **Rank:** `f8{e4m3,e5m2} < f16 < f32 < f64`. f8→{f16,f32,f64} widen **losslessly**;
  {f16,f32,f64}→f8 and f8↔f8-cross **round** (may lose / saturate / NaN). Every f8 conversion is
  **explicit `to`** (no implicit narrowing), consistent with the existing float→int `to` guard
  posture (`codegen.tks:2382`).
- **`prim_float_tag` (`codegen.tks:2101`)** — extend so f8 can be a cast source/dest tag:
  add `F8E4M3 => "f8e4m3"; F8E5M2 => "f8e5m2"`, driving the existing
  `tk_to_<dst>_from_<fsrc>` naming convention.
- **Arithmetic model (the key simplification):** f8 has **no scalar ALU on any ISA**. Every f8
  binary op is defined as **promote→f32, compute in f32, round→f8**. So `f8 + f8` lowers to
  `f8→f32`, `FAdd` (f32), `f32→f8`. This is both the C-backend and own-backend semantics; it means
  f8 introduces **no new float opcode** — only the two convert directions.

### 3.6 CODEGEN — C backend (software emulation; f8 is not a C type)

- **C type mapping (`codegen.tks:330`).** No standard C `f8`. Map both to a 1-byte carrier
  `typedef uint8_t tk_f8e4m3;` / `tk_f8e5m2;` (distinct typedefs for readability;
  `ltype_size`/`align` = 1). Add the `F8E4M3 => "tk_f8e4m3"` etc. arms at `:330`, `:1122`,
  `:1160`, `:1857`.
- **Runtime helpers — maintained-C seed (`teko_rt.c`, the allowed exception).** A small
  branch-free family, round-to-nearest-even, format-correct saturation/NaN:
  `tk_f32_to_f8e4m3(float) -> tk_f8e4m3`, `tk_f8e4m3_to_f32(tk_f8e4m3) -> float`, the e5m2 pair,
  and `tk_f8e4m3_to_f8e5m2` / reverse. f8 arithmetic in emitted C is `to_f32 → C float op →
  from_f32` (no `tk_f8_add` needed; `%` stays rejected per `:2144`, `/` panics on ÷0 via the f32
  substrate). **Owner note:** these C helpers are the *reference* semantics; §3.7 mirrors them
  pure-Teko for kill-c.
- **Determinism.** Round-to-nearest-ties-to-even, fixed saturation, single canonical NaN
  (e4m3) — pinned so VM and native agree byte-for-byte (the round-trip fixtures gate this).

### 3.7 CODEGEN — own-backend (isel) — couples to the `B1-fp` float epic

- **Storage + conversion = integer-domain, needs NO float epic.** An f8 value is a byte:
  load/store, move, and the *bit-manipulation* of `f8↔f32` conversion are integer/GPR ops the
  isel already handles. Add `LType::F8E4M3`/`F8E5M2` (`lir.tks:18`), `ltype_size`/`align` = 1
  (`:533`), and `ltype_of_prim` arms (`lower.tks:183`). Represent f8↔f32 as `LUnOp::FToF`
  (`lir.tks:41`) parameterized by the src/dst `LType` widths (already the model for f16↔f64).
- **Arithmetic = the f32 substrate = the epic.** Because f8 arithmetic *is* f32 arithmetic
  (§3.5), f8 own-native arithmetic needs exactly the **`B1-fp` float/FPR epic (0.3.1)** — the SSE
  / NEON / RVF register class the isel honest-stops on today (`isel_x86_64.tks:197`,`:325`,…).
  **f8 adds nothing to that epic beyond f32 + the two software convert routines.** The convert
  routine itself, own-native, is a pure-GPR round (a Teko `f8e4m3_from_f32` compiled normally),
  so it does **not** depend on the C runtime — kill-c-safe.
- **Sequencing hook.** f8 arithmetic own-native is a *rider* on the float-isel epic in the
  `kill-c-pull-forward` / 0.3.1 float work: when `B1-fp` closes for f32, add two f8-round fixtures
  and f8 arithmetic own-native closes with it. No separate epic.

---

## 4. `dec` and `bigint` — surface, representation, runtime, own-backend

### 4.1 Surface decision

**RECOMMEND native surface types `dec` and `bigint`** (compiler-known names with literals and
operators), **runtime-backed**, **not** `PrimKind` scalars and **not** plain stdlib structs.
Rationale: (a) the owner's ruling ("toda linguagem séria tem") wants ergonomic literals/operators,
which a bare stdlib struct cannot get without operator-overloading (which Teko does not have); (b)
they are **variable-width heap-backed**, so they cannot be `Prim` (fixed-width machine scalars).
The clean fit is the **existing `str`/`Slice` precedent**: a compiler-known type name with a
runtime-backed, **immutable, value-semantic** representation.

Add two new `Type` cases (`type.tks:102`), mirroring `Str`/`Byte` (markers, runtime layout in the
runtime), **not** new `PrimKind`s:

```teko
/** Arbitrary-precision signed integer. Runtime-backed (immutable, arena/region-allocated
 *  limb vector); value-semantic — every op returns a fresh value. NOT a Prim scalar.
 *  @since NUMERIC-COMPLETENESS */
pub type BigInt = struct { }

/** Arbitrary-precision base-10 fixed/decimal (a signed coefficient BigInt + a scale). Runtime-
 *  backed, immutable, value-semantic. Exact decimal arithmetic (money, no binary rounding).
 *  NOT a Prim scalar. @since NUMERIC-COMPLETENESS */
pub type Dec = struct { }
```
`Type` variant (`:102`) gains `| BigInt | Dec`; `type_eq` (`:119`) gains
`BigInt => match b { BigInt => true; _ => false }` and likewise `Dec` (same shape as `Str`/`Error`).
`.tkb` `write_type`/`read_type` gain two new **type tags** (next free tags after the current 12;
NOT prim ordinals) — additive, old tags stable.

### 4.2 Representation (arena-friendly, no-GC)

- **`bigint`:** `{ sign: i8, len: u64, limbs: *u64 }` — a sign-magnitude little-endian `u64`
  limb vector, **immutable**, allocated via `tk_alloc` in the **current region** (never bare
  `malloc`-and-leak), exactly the `tk_str`/`tk_slice_*` retained-buffer model. `__int128`
  alignment discipline already in the arena (`teko_rt.h:113`) covers `u64` limbs trivially.
- **`dec`:** `{ coeff: bigint, scale: i32 }` — value = `coeff × 10^(-scale)`. Reuses the bigint
  limb machinery; scale is a plain field. Normalization (trailing-zero trim) is explicit, not
  automatic, so equality is well-defined.
- **Value semantics + no-GC:** every operation returns a **fresh** immutable value (copy-on-op),
  identical to `tk_str_concat`'s "fresh buffer the result owns" contract (`teko_rt.c:549`). This
  is what makes it no-GC-safe: no mutation, no shared-ownership graph, lifetimes = the arena
  region. See §6.1 for the loop-accumulation tension and its resolution.

### 4.3 Operations (surface)

Operators `+ - * / % **` (bigint), `+ - * /` with explicit rounding-mode (dec); comparisons;
`to` conversions bigint↔{i64,i128,u64,u128} (guarded, may overflow-error), dec↔bigint (scale),
dec↔f64 (lossy, explicit); parse/format from/to `str`. Division defines quotient+remainder
(bigint) and a **required rounding mode** (dec — half-even default). No implicit int↔bigint
widening (explicit `to bigint`), matching the language's no-implicit-narrowing posture.

### 4.4 Runtime backing — **pure Teko**, not `teko_rt.c`

**RECOMMEND the digit engine live in pure-Teko stdlib modules** `teko::math::bigint` and
`teko::math::dec`, operating on `[]u64` limbs via existing slice ops — **not** as new
`teko_rt.c` intrinsics. Reason (law-first, §6.2): `teko_rt.c` is frozen-except-maintained; the
own-backend end-state is **no C**. A pure-Teko engine compiles under both backends and **survives
kill-c** with zero porting. The compiler-known `bigint`/`dec` names desugar their literals/operators
to calls into these modules (the same way `str` methods lower to `tk_str_*` today, but here the
target is Teko, not C). `teko_rt.c` gets **at most** a thin optional fast-path intrinsic later
(e.g. `__int128` mul-add for the inner limb loop) *if* profiling demands — never on the
correctness path.

### 4.5 Own-backend interaction

Because the engine is pure Teko over `[]u64`, `bigint`/`dec` own-native = "compile the stdlib
module with the own-backend" — **no float, no new isel, no C dependency**. It rides whatever
integer-isel maturity the own-backend already has. The only backend-specific surface is the two
new `.tkb` type tags (§4.1) and struct-layout lowering for the `{sign,len,limbs}` /
`{coeff,scale}` records — both already-supported LIR shapes (`LStructLayout`, `lir.tks:524`).

---

## 5. Sequencing (waves) — what enters .30 vs after

**Seed-safety governs everything (`wave-0.3.1-plan.md` §1:48):** these are **additive** surfaces
the compiler's own `src/` does **not** adopt, so no grafia-adoption seed hop is required — only
the additive introduction, gated by the normal per-merge seed cut.

| Train | Scope | Wave | Depends on |
|---|---|---|---|
| **f8-A** | `f8e4m3` type: PrimKind append, predicates, width, `.tkb` r/w, names, literal-context typing, comptime round-fold, C-backend typedef + `tk_f32↔f8e4m3` runtime helpers, C-backend arithmetic (f32-substrate) | **.30-eligible** (early; pure-additive prim, integer-domain storage) | nothing blocked |
| **f8-B** | `f8e5m2` type: the symmetric second format (all of f8-A for e5m2) + cross-f8 convert | **immediately after f8-A** (same wave if capacity) | f8-A landed |
| **f8-C** | f8 **arithmetic own-native** (2 round fixtures riding the f32 substrate) | **0.3.1**, folded into the **`B1-fp` float/FPR isel epic** | HARD-DEP: `B1-fp` epic (float isel) |
| **bigint** | native `bigint` surface + pure-Teko `teko::math::bigint` engine + 2 new `.tkb` type tags | **after 0.3.1 SW1 (arena/region foundation)** | arena/region vocabulary (SW1) |
| **dec** | native `dec` surface + `teko::math::dec` (reuses bigint) | **after `bigint`** | bigint landed |

**Rationale for the split:** f8-as-storage is a small, self-contained additive prim (integer
domain) → early. f8 *arithmetic* own-native is inseparable from the f32 FPR epic → rides it, no
duplicate work. `bigint`/`dec` are the big features: variable-width heap types whose no-GC safety
leans on the region/arena model, so they wait for the SW1 arena foundation and land as their own
train, bigint before dec (dec reuses bigint). **f128 is not sequenced (dropped).**

**Fixpoint discipline:** every train's merge is **gen1 == gen2** (the compiler builds a compiler
that builds an identical compiler) — **no gen3**. Each train is a full-gate ritual point (§7).

---

## 6. Law tensions + recommended resolutions

### 6.1 Memory-model no-GC vs unbounded bigint/dec intermediates (TENSION → resolved)

A `bigint`/`dec` accumulator in a loop (`for … { acc = acc + x }`) allocates a fresh immutable
value each iteration; with the arena, nothing is reclaimed until the region pops → monotonic
growth. **This is not a NEW tension** — it is *exactly* the `str`-concat-in-a-loop behavior the
language already accepts (`teko_rt.c:549`, fresh-owned buffer). **Resolution:** value-semantic +
region-scoped allocation; the **already-staged Boundary-A rewind** (`tk_arena_push`/`pop`/`commit`,
`teko_rt.h:155`) reclaims per-scope. A future optimization may allocate a `mut` accumulator's
limbs in a rewindable sub-region so loop iterations reuse storage — **out of scope now**, noted so
the representation (§4.2, immutable + region ptr) does not foreclose it. **No owner action needed;
no law violated.**

### 6.2 Frozen-C vs runtime backing (TENSION → resolved by pure-Teko engine)

Putting the bigint/dec engine in `teko_rt.c` is *permitted* (maintained-C exception) but pulls
against the own-backend "C dies" end-state. **Resolution (§4.4):** implement the engine in **pure
Teko** so it needs no C and survives kill-c; `teko_rt.c` stays out of the correctness path.
**No law violated; recommended, not a HALT.**

### 6.3 Seed-safety (no tension — stated for completeness)

All three surfaces are additive and **unused by the compiler's `src/`**, so no seed cannot-parse
hazard (`wave-0.3.1-plan.md` §1:48). The f8 `.tkb` ordinal choice (§3.1, append-after-Bool)
specifically **preserves** the seed's codec. Introduce each in the additive sub-wave; adoption in
`src/` is never required.

### 6.4 f8 format ratification (OWNER DECISION, not a HALT)

The one genuine open choice is **which f8 encoding** (§2.2): OCP-OFP8 (recommended) vs P3109-draft,
and confirmation of the `f8e4m3`+`f8e5m2` distinct-pair surface. This is a **ratifiable design
decision, not a law tension** — the recommendation passes all Laws (additive, seed-safe, kill-c-safe).
**Owner ratifies; no HALT.**

---

## 7. Ritual points (full gate must pass)

1. **End of f8-A** (e4m3 storage+convert+C-arith) — seed cut.
2. **End of f8-B** (e5m2) — seed cut.
3. **Within the `B1-fp` epic close** (f8-C arithmetic own-native) — the float-epic ritual absorbs it.
4. **End of `bigint` train** — seed cut.
5. **End of `dec` train** — seed cut.

Each is gen1==gen2 fixpoint + 100%-new-code coverage + independent review (the standard
per-sub-wave merge gate).

---

## 8. Regression fixtures (inputs → expected exit code; VM **and** native must agree)

> Convention: a program that computes and `exit(n)`s an expected byte; a mismatch/panic path
> `exit`s a distinct code. Every fixture runs under both the VM/C backend and (where the train
> reaches it) the own-backend; the two must agree byte-for-byte.

**f8 round-trip / arithmetic (f8-A, f8-B):**
- `f8e4m3` exact-representable round-trip: `0.5`, `1.0`, `448.0` (max), `0.375`, smallest
  subnormal → `to f32 to f8e4m3` identity → `exit 0`.
- `f8e4m3` rounding: `0.1 to f8e4m3 to f32` within one ULP of the format → `exit 0`, else `exit 1`.
- `f8e4m3` saturation: `1000.0 to f8e4m3` → max `448.0` (OFP8 has no inf) → assert → `exit 0`.
- `f8e4m3` NaN: canonical single-NaN encoding preserved through `to f32 to f8e4m3` → `exit 0`.
- `f8e4m3` arithmetic (f32-substrate): `(0.5:f8e4m3) + (0.25:f8e4m3) == 0.75:f8e4m3` → `exit 0`.
- `f8e5m2` range: `57344.0` round-trips; `+inf`/`-inf` present and preserved → `exit 0`.
- `f8e5m2` saturation/inf contrast: `1000.0 to f8e5m2` is finite (range 57344), NOT saturated
  like e4m3 — asserts the two formats differ → `exit 0`.
- **cross-format is a type error:** a fixture storing `f8e5m2` into an `f8e4m3` slot **fails to
  compile** (checker fixture, expected non-zero compile exit / diagnostic) — proves the firewall.
- `.tkb` round-trip: a module exporting f8e4m3/f8e5m2-typed fns writes+reads `.tkb`, ordinals
  14/15 survive, `Bool`(13) unaffected → `exit 0`.

**bigint:**
- `2 ** 128` exact (exceeds `i128`) formats to the known decimal string → `exit 0`.
- factorial(50) exact decimal string → `exit 0`.
- `bigint(-7) % bigint(3)` sign convention matches `tk_rem` (`teko_rt.h:552`) → `exit 0`.
- guarded `to i64` overflow: a bigint > i64::MAX `to i64` → error path → distinct exit.
- loop-accumulation (no-GC stress): sum 1..10000 as bigint, region-scoped, correct + no OOM → `exit 0`.

**dec:**
- `dec("0.1") + dec("0.2") == dec("0.3")` **exactly** (the binary-float trap) → `exit 0`.
- half-even rounding: `dec("2.5") / dec("1")` at scale 0 → `2` (ties-to-even) → `exit 0`.
- scale preservation: `dec("1.00") * dec("1.0")` scale/normalization contract → `exit 0`.
- dec↔bigint↔str round-trip → `exit 0`.

---

## 9. What remains blocked (design-ahead honesty)

- **f8-C (arithmetic own-native)** is BLOCKED on the **`B1-fp` float/FPR isel epic (0.3.1)**.
  Everything f8-C needs *around* the epic is designed here (the two round fixtures, the pure-GPR
  convert routine, the "no new opcode" proof); it resumes in minutes when `B1-fp` closes.
- **`bigint`/`dec`** are BLOCKED on the **0.3.1 SW1 arena/region foundation** for their no-GC
  loop-reuse story (§6.1). The surface, representation, engine module skeletons, `.tkb` tags, and
  fixtures are fully specified against today's `tk_alloc`/arena API — they compile against the
  *declared* arena shape now; only the rewind-reuse optimization waits.
- **Owner ratification pending:** (1) f8 encoding = OCP-OFP8 + distinct `f8e4m3`/`f8e5m2` pair
  (§2.2); (2) the append-after-Bool ordinal placement departing from B.38 wording (§3.1); (3) the
  native-surface (vs stdlib-struct) choice for `dec`/`bigint` (§4.1). None is a law tension; all
  pass the Laws. No HALT.
