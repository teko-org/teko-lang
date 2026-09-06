# Cast & Width Hygiene — definitive fix (0.3.1)

> **Status:** DECISIONS CLOSED — ruling-level, owner 2026-07-24 (counter-round cumprido),
> **with D1 REVERSED on 2026-07-27: the `redundant cast` diagnostic is a WARNING, not an error**
> (§4, §9). D2–D5 stand as ratified.
> **Implementation progress (branch `feat/0.3.1-cast-width-a`, base `327d50d4`):** **C1
> (W-RULE in the checker) — DONE.** **C2 (backend no-op confirmation) — DONE.** **C6
> (`teko::casting` stdlib module) — DONE.**
> **C3 (signature sweep, branch `feat/0.3.1-cast-width-c3c5`) — DONE.** Widened ~30
> frame-slot/count declarations (`MFrameAddr`/`X86.slot`, `Spilled`/`AssignLookup.slot`,
> `next_slot`, `frame_base`, `count_spills`/`append_spill_slots`,
> `emit_gpr_reload`/`emit_gpr_store` + `rewrite_inst`/`block`/`func` × 3 archs,
> `slot_offset`/``, `frame_slot_addr`, `block_pos`→`block_rpo_pos`) `u32`→`u64`,
> deleting the `.len to u32`/`.len to u64` casts they existed only to satisfy.
> **C4 (corpus cast sweep + the D1 `redundant cast` diagnostic, "varre → liga") — DONE.**
> **Since 2026-07-27 the diagnostic is a WARNING, not an error** (§4/§9 — the owner reversed the
> 2026-07-24 ruling because as an error it was unsatisfiable across the seed/gen1 boundary; D4's
> ≤2% gate carries the enforcement now). `type_cast`
> flags a same-type cast (class 1); `type_binary`/`type_compare` flag a redundant
> lossless-widen (class 3) — guarded against a literal partner (adoption depends on the
> partner's PRE-cast type) and against a partner that is ITSELF an equally-removable `to`
> (two mutually-"redundant" casts can jointly establish the real computed width — found and
> fixed during the sweep itself, um backend encoder). Swept ~200+
> corpus-wide redundant casts (the bulk "bare literal to i64/f64" pattern + individually
> verified class-3 sites); D1 flipped on in the SAME commit, corpus never red between wagons.
> **C5 (ARITH-CAST-RATE probe + CI flag) — DONE**, `--arith-cast-gate` wired and unit-tested;
> current corpus reads CAST-DENSITY 23.3/KLOC (was 25.7/KLOC pre-C3/C4) and ARITH-CAST-RATE
> 3.40% — ABOVE the D4 ≤2% ceiling, so the flag is NOT yet wired as a blocking CI step (would
> red every future PR against the current corpus). Closing that gap needs either a follow-on
> signature-sweep wagon over the remaining cast-dense `*_consts.tks`/`objfile_*.tks` wire-format
> files (C3's own explicitly out-of-scope "highest blast radius" tier) or an owner
> re-ratification of the threshold/scope — an open item for the integrator, not silently
> deferred. §9 records the closed rulings; the plan below is aligned to them (D1 direct-error,
> D2 rejected → no new cast syntax, D3 peer-type, D4 ≤2% gate, D5 = swap declarant types to u64) — with D1 REVERSED to a warning on 2026-07-27, which makes the D4 gap above the only remaining enforcement of the class and therefore no longer a deferrable item.
>
> **Owner rulings recorded verbatim (dated 2026-07-24):**
> 1. *"Casting deve ser feito com muita cautela e em casos excepcionais (raríssimos); o
>    compilador deve ter uma base totalmente (>=98%) determinística."*
> 2. The three failures below "foram pedidos AINDA NA ERA C do backend/frontend e nunca
>    honrados" — this doc recovers the original order and closes it.
>
> **Reading order (context):** `docs/design/literal-context-typing.md` (the .29 delivery this
> builds on — context-typed literals), `docs/design/drop-128-family-0.3.1.md` (the R1 u64-narrow
> that removes i128/u128 BEFORE this sweep runs), `docs/design/wave-0.3.1-plan.md` (the seed
> chain + `ref` linchpin these corpus-wide sweeps engage AFTER), `TEKO_LEGISLATION.md` (B.22,
> B.38), `TEKO_HISTORY.md` §B.22/§B.38.

---

## 0. The recovered original order (C era)

The owner's memory is correct. Two C-era artifacts frame the intent:

- **`docs/PHASE16_CASTING.md`** (commit `5872bd51`, 2026-06-16; later removed from tree). Scope
  item 1: *"Conversions between types … Explicit and **checked** — fail loudly, no silent
  truncation/UB."* Sub-block **16.F — Checked inter-type conversions** ("narrowing range checks,
  float→int truncation policy"). The C era named the checked-cast machinery but **never legislated
  a mixed-width arithmetic rule** — it left arithmetic same-type-only and pushed every width change
  onto an explicit `to`. That is the gap.

- **The B.22 comparison sign-check** (`TEKO_HISTORY.md` §B.22, lines 96–97; seed codegen). This is
  the load-bearing precedent. Comparison was given a **sign-check-first strategy with NO promotion
  and NO ceiling**: *"if the signed side < 0 it's less than any unsigned, else compare at original
  width"* — `u256 vs i256` works. **Mixed-width integer comparison ALREADY works today, with zero
  casts.** The asymmetry the owner is pointing at:

  | regime | today | file |
  |--------|-------|------|
  | **comparison** `< > == …` | mixed width/sign **works** (sign-check, no cast) | `expr.tks:19-23 is_comparable` |
  | **arithmetic** `+ - * / % & \| ^` | **same-type only** (`type_eq`), cross-width needs `to` | `typer.tks:286-299 type_binary` |

  Arithmetic was left behind. The current honest-stop text even says so:
  `"operands must be the same type (no promotion, no mixed int/float — B.22; use `to`)"`
  (`typer.tks:292`). **The original C-era intent — checked, fail-loud, no silent truncation —
  wanted the width change to be automatic-and-safe where it is lossless, and explicit only where it
  can lose.** That never landed. This doc lands it.

---

## 1. The three failures — evidence & counts

Baseline probe (whole corpus, `src/**/*.tks`, 136 files, **100,236 lines**):

```
total numeric casts  `x to <numtype>`   : 2649        (26.4 casts / KLOC)
  to u32   1314   |  to u64   438  |  to byte 368  |  to i64 218
  to i128   136   |  to u128   71  |  to i32   36   |  to u8   33
  to u16     13   |  to i8      9  |  to i16    9   |  to f64   4
`.len to <T>`        : 146   (90×→u32, 31×→i64, 15×→u64)
`x to u32)` (narrow-into-arg/field, closing paren) : 306
```

Densest files (numeric casts): `codegen.tks` **117**,
`encode_arm64_consts.tks` **103**, um backend encoder **98**,
`objfile_macho.tks` **97**, `objfile_elf.tks` **86**, `encode_x86_consts.tks` **86**,
`math/checked.tks` **77**, `emit/tkb_write.tks` **77**. The backend/emit tier is where the manual
equalizations concentrate.

### Failure (1) — pointless casts (the cast changes nothing)

`x to T` where the operand is already `T`, or where the target is a width the rule would already
reach. Concrete:

- **`.len to u64`** (15 sites): `.len` is **already `u64`** (`typer.tks:1527-1528` types
  `str`/slice `.len` as `Prim{U64}`). `x.len to u64` is a literal no-op.
- **Redundant equalize-in-comparison** — a `(i to u32) == id` where `i` is `i64` and `id` is
  `u32`; **comparison already allows mixed width** (§0), so the `to u32` is dead weight today,
  before any rule change.
- **Literal shift/const casts** — a `bits >> (32 to u64)`. Under context-typed
  literals (.29, `literal-context-typing.md`) `32` already adopts the operand width; the `to u64`
  is redundant.

### Failure (2) — wrong expected type forces a lossy narrow

`.len to u32` (**90 sites**) is the emblematic case. `.len` is `u64`; a field/param/index that is
declared `u32` forces `x.len to u32`, a **u64→u32 narrowing that PANICS on overflow** (the checked
cast is fail-loud — `TEKO_LEGISLATION.md` conversions block). Real sites:
`encode_arm64.tks:1311,1757,1955,2053`, um backend encoder — every one is
`<something>.len to u32` feeding a `u32` offset/index/count. **If the offset/count fields were
their natural `u64` (or a deliberate `usize`-style width), the cast — and its panic edge —
vanishes.** The `x to u32)` count (306) is the upper bound of this class: casts jammed against a
call/field boundary purely to satisfy a too-narrow declared type.

### Failure (3) — manual width equalization, up AND down

Because arithmetic is same-type-only, mixed-width math is hand-equalized in the corpus. Both
directions exist:

- **DOWN** (narrow both/one to the smaller): a `((scopes.len - 1) - i) to u32` or an
  `order.len to u32` — compute wide, then chop to the consumer's narrow width.
- **UP** (widen both to a common wide type): the `to u64`/`to i64`/`to u128` family (438 + 218 +
  71 sites) — e.g. `encode_arm64.tks:1955 acc.base + ((ef.words.len to u32) * ARM64_WORD_BYTES)`
  mixes a `u32` base with widened terms by hand.

These equalizations are **corpus source, not a backend algorithm** — there is no separate
"backend equalization pass" to delete. The backend faithfully lowers whatever `to` nodes the
source writes (`lower.tks:326 cast_int_unop_of` → `SExt`/`ZExt`/`Trunc`;
`codegen.tks:2114 cast_may_lose`). So "what the backend stops doing" = **the sweep deletes the
manual `to` casts from the backend's own `.tks` files** (the 169/117/114… hot files), and the
checker's new width rule synthesizes the *lossless* widen internally instead.

---

## 2. The definitive Width Rule (RECOMMENDATION — ratifiable)

**Recommended rule (law-first; mirrors the B.22 comparison sign-check into arithmetic):**

> **W-RULE.** In a binary arithmetic/bitwise op over two **integers**:
> 1. **Same signedness, different width** → the result type is the **larger width**; the narrower
>    operand is implicitly widened (`ZExt` if unsigned, `SExt` if signed). Always lossless.
> 2. **Mixed signedness** → *only* lossless when a fixed-width type holds **all** values of both:
>    - signed side **strictly wider** than the unsigned side (e.g. `u32 + i64`) → widen both to the
>      **signed wider** type (`ZExt` the unsigned). Lossless (every `uN` fits in `i>N`). Result =
>      signed wider.
>    - **otherwise** (equal width mixed sign — `u32 + i32`; or unsigned strictly wider — `u64 + i32`)
>      → **no lossless common fixed-width type exists** → **compile error**, honest-stop demanding an
>      explicit `to` naming the developer's chosen target (which then carries the fail-loud runtime
>      guard). **No silent sign reinterpretation, ever.**
> 3. **Float × float, different width** → larger width (widen the narrower). Same lossless logic.
> 4. **Int × float** → unchanged: **compile error**, needs explicit `to` (B.38 — no implicit
>    int↔float).
> 5. **Literals** sit BELOW the rule: a fitting numeric literal operand adopts the other's type
>    first (context-typing, .29 `literal_adopts` at `typer.tks:279-281`), so `n + 1`, `len % 10`
>    never even reach the width join.

**Why this rule, law-first (no HALT):**

- It is the **exact sibling of the comparison sign-check** the seed already shipped (§0) — one
  regime was done, this finishes the set. Zero new doctrine; it *completes* B.22.
- It is **totally deterministic and platform-independent**: widths are fixed (`u8…u64`/`i8…i64`
  after drop-128), the join is a pure function of the two static widths+signs, never a
  platform word size. Honors owner ruling #1.
- It **never lies**: the only mixed-sign case it accepts is the one where the wider signed type is
  a *superset* of both value ranges. Every genuinely-lossy or ambiguous case still stops and asks
  for an explicit `to` — casting stays "cautela, casos raríssimos."

### 2.6 IMPLICIT WIDENING AND THE NARROWING GUARD (owner ruling 2026-07-29 — W-RULE §6/§7)

The W-RULE above governs a binary OPERATOR's two operands. The 2026-07-29 ruling extends the same
lossless set to every place a value meets a DECLARED type, and fixes what a narrowing costs.

> Owner, literal: *"Tamanhos menores devem caber em tamanhos maiores e de mesma aridade [...] o mesmo
> vale para floats e para bigint e decimal. Fazendo pânico somente se ocorrer (em runtime) overflow"*
> and *"Uma rule para cast (implícito ou explícito), não pode truncar, no caso de explícito de tipo
> maior que outro, deve ser feito em runtime (e panicar se overflow), por isso teremos na stdlib um
> local apropriado para checked (casts seguros)"*.

**§6 — implicit widening.** A value whose type widens LOSSLESSLY into its target reaches that target
with no `to` written. The admitted set is exactly `numeric_widens_implicitly` (`checker/expr.tks`),
which delegates the integer half to `cast_is_lossless_widen` — **the same predicate the arithmetic
W-RULE already uses**, so an expression and an assignment can never disagree about which widths mix.
`bigint`/`dec` receive every machine integer through their `of`/`of_u64` constructors. Widening cannot
overflow, so this direction never panics. Targets served: annotated binding, plain/compound/field/
index assignment, write-through-reference, call argument, and `return`/trailing value.

**Language mirrored, and the divergence made on purpose.** This is the **C#** shape — implicit
widening numeric conversions plus a checked narrowing that raises on overflow. It is a **deliberate
divergence from Rust**, the project's surface reference, which has NO implicit numeric widening and
demands `as` on every width change (§2's own precedent table records that Rust's strictness *"rejects
the owner's ask"*). The divergence is the owner's call; it is named here and in the doc-comment rather
than dressed up as parity.

**§7 — no cast truncates, ever.** A narrowing `to` verifies the fit at run time and PANICS when the
value does not fit (`tk_panic_cast`). There is no truncating path, documented or fallback. The
recoverable form is `teko::casting::<src>_to_<dst>` (§10), which returns `T | error` instead.

**Float narrowing (`f64 to f32`) — DECIDED here, and it is NOT guarded.** It follows IEEE-754
round-to-nearest: exact when representable, correctly rounded when not, ±∞ on overflow, ±0 on
underflow; it never panics. An integer narrow that overflows answers with a finite number bearing no
relation to its input (300 becoming 44) and that is the wrong answer §7 outlaws; a float narrow never
does — overflow yields ±∞, which IS an `f32` value and which nobody mistakes for the input. Guarding
only the overflow end would also be incoherent, since the identical range argument covers ordinary
precision loss and underflow (`1.0e-300 to f32` is `0.0`) and no language panics on those. C# agrees:
its `checked` context covers integral types only and `(float)1e300` is `+∞`, not an exception. Pinned
executably by `f64_to_f32_follows_ieee_and_never_panics` (`src/checker/checker_test.tkt`).

### Precedents (owner asked to cite Zig/Rust/Go and recommend ONE)

| lang | mixed-width int arithmetic | mixed-sign | verdict for Teko |
|------|---------------------------|-----------|------------------|
| **Rust** | **no** implicit widening — `u32 + u64` is a compile error, need `as`/`.into()` | error | too strict; would keep the corpus full of casts — rejects the owner's ask |
| **Go** | **no** implicit conversion between numeric types at all (even `int`/`int64`) | error | same — rejects the ask |
| **Zig** | **peer type resolution**: widens to a type that can represent **all values of both** operands; narrowing stays explicit (`@intCast`) | mixed-sign resolves only when a type holds both, else explicit | **ADOPT** — this is exactly W-RULE |

**Recommendation: adopt the Zig "peer type resolution / widen-to-lossless-common" model** — it is
the only one of the three that grants the owner's implicit-widening ask while preserving
fail-loud/no-lie. Teko's twist vs Zig: Teko's fixed set has no `i128` backstop after drop-128, so
the equal-width mixed-sign case (`u64 + i64`) has **no** lossless common type and correctly errors
(Zig would reach for a wider int; Teko honestly stops). This is *more* conservative than Zig, which
suits ruling #1.

### Where the checker applies it

`typer.tks:286-299 type_binary`, the `op_is_arith` and `op_is_arith_bitwise` arms. Replace the
`type_eq` gate with the width join. **Implementation shape (implementer copies verbatim, full
Javadoc):**

```teko
/**
 * int_arith_join — the result type of a mixed-width integer binary op (W-RULE §2).
 *
 * Returns the smallest fixed-width integer prim that losslessly holds BOTH operand
 * types, or `error` when no such type exists in the fixed set (equal-width mixed sign,
 * or unsigned strictly wider than a signed operand) — the caller then emits the
 * "needs an explicit `to`" honest-stop. Floats and int×float are NOT handled here
 * (see `float_arith_join` / the B.38 int×float reject).
 *
 * @param  a      the left operand's resolved integer PrimKind
 * @param  b      the right operand's resolved integer PrimKind
 * @return PrimKind  the widened common integer kind (W-RULE §2.1/§2.2)
 * @throws error  when a and b share no lossless common fixed-width type (W-RULE §2.2 fallthrough)
 * @see    is_comparable — the comparison sibling this mirrors (expr.tks:19)
 * @since  0.3.1
 */
fn int_arith_join(a: PrimKind, b: PrimKind): PrimKind | error { /* … */ }
```

```teko
/**
 * widen_operand — wrap a checked operand in a synthetic widening TCast to `target`,
 * or return it unchanged when it already IS `target`. The backend lowers the TCast to
 * ZExt/SExt (lower.tks:326 cast_int_unop_of) with NO backend change — the width rule
 * emits the exact same node the corpus used to write by hand, so codegen is untouched.
 *
 * @param  operand  the already-checked TExpr (its .type is an integer Prim)
 * @param  target   the common integer PrimKind int_arith_join chose
 * @return TExpr    operand, or operand wrapped in a synthetic TCast{target} (never lossy)
 * @since  0.3.1
 */
fn widen_operand(operand: TExpr, target: PrimKind): TExpr { /* … */ }
```

`type_binary` then becomes: type both sides → literal-adopt (unchanged) → if `type_eq` keep the
fast path → else `int_arith_join(l,r)` (or `float_arith_join`); on `error` return the honest-stop
with rendered types; on success wrap the narrow side with `widen_operand` and set the result type
to the join. `float_arith_join` is the symmetric §2.3 helper (larger float width; distinct
signature so int/float never cross). **Touched fns:** `type_binary` (typer.tks:271),
`type_unary`/`type_compare` untouched (unary is single-operand; compare already sign-checks). New
helpers live in `expr.tks` beside `is_comparable`/`is_numeric` (the B.22 shared core).

### What the backend/codegen stops doing

**Nothing algorithmic is deleted** — `cast_int_unop_of`, `cast_may_lose`, the `SExt`/`ZExt`/`Trunc`
lowering all stay (they still serve the surviving explicit `to`). What changes: the **corpus sweep
(crumb 4) deletes the manual equalizations** from the backend/emit `.tks` files — the 169/117/114…
cast clusters shrink to the handful of deliberate ABI-boundary narrows. The checker now synthesizes
the widen the source used to spell.

---

## 3. `.len` canonical type & policy

- **Canonical today:** `str`/slice `.len` is **`u64`** — `typer.tks:1527-1528`
  (`type = Prim { kind = PrimKind::U64 }`); the injected `str.len` builtin is likewise
  `-> u64` (`scope.tks:561`). Confirmed single source of truth.

- **Policy:** **`.len` is `u64`; never narrow it below 64 bits without an explicit, guarded
  `to`.** A length is a machine-word count — narrowing to `u32`/`i32` reintroduces the overflow
  panic edge for no gain. The 90 `.len to u32` + 31 `.len to i64` + 15 `.len to u64` sites are the
  sweep target of Failure (2).

- **Signature sweep (crumb 3):** the 90 `.len to u32` sites exist because a **declared field/param
  is `u32`** (a block offset, a saved-reg count). Sweep those declarations to the
  natural width:
  - DWARF/ELF/Mach-O/COFF offset & index fields that are genuinely 32-bit **on the wire** stay
    `u32` **but** the narrow moves to **one guarded boundary cast at serialization**, not sprinkled
    at every `.len` read. (The wire format's u32 IS the deliberate exceptional cast — §5, kept and
    documented.)
  - internal count/length params that are `u32`/`i32` for no wire reason → **widen the declaration
    to `u64`**; the `.len to u32` at the call sites then evaporates under the width rule.
  - the 15 `.len to u64` → pure deletion (no-op, Failure 1).
  Inventory of the exact declarations to touch is produced mechanically at crumb open (grep
  `to u32\b` in the encode_*/objfile_* files, then read each declaring signature). Expect
  ~30–50 declaration edits driving ~120 call-site cast deletions.

---

## 4. Sweep of pointless `x to T` — mechanical detection + anti-regression diagnostic

**Detect mechanically (three classes, all grep-able + confirmable by a small probe pass):**

1. **cast-to-same-type** — `x to T` where `typeof(x) == T`. The checker already computes this: at
   `typer.tks:1301`/`resolve.tks (type_eq(from,to))` the cast is flagged `null` ("same type — a
   no-op"). A probe that walks `TCast` nodes and reports `type_eq(inner.type, target)` finds every
   one deterministically (this is a CF6-style compiler probe, not a grep guess).
2. **redundant-with-literal-context** — the operand is a numeric literal that already adopts the
   target via context-typing (.29, `literal-context-typing.md` task #13/#15). `5 to u32` under
   `let x: u32 =` is redundant (that doc, lines 115–116, 303–306, already ratified it stays *legal*
   but *unnecessary*).
3. **redundant-under-W-RULE** — a `to` whose only purpose was manual equalization the width rule
   now performs, or a widen-in-comparison the sign-check already allowed. Detected by: re-check the
   corpus with W-RULE ON and a "strip this cast" trial — if the expression still types identically,
   the cast is a no-op. The probe removes each candidate `TCast`, re-runs `type_binary`, and keeps
   the removal iff the result type is unchanged.

**Anti-regression diagnostic (D1 — REVERSED TO A WARNING, owner 2026-07-27; was a direct ERROR,
owner 2026-07-24):** a `x to T` that is provably a no-op (class 1 or 3 above) emits a **`redundant
cast` WARNING on stderr** and compiles. Owner, literal: *"o cast, nestes casos, não deveria ser
falha, deveria ser warning, o que não nos exime de não ter warnings no próprio compilador (lembra?
<= 2%)."* The superseded ruling read: *"prefiro que falhe para evitarmos de cair no mesmo erro."*

**Why the reversal, in one paragraph, because "we softened a rule" is the wrong reading.**
D1-as-error made a redundant cast *unspellable*, and a bootstrap chain has **two compilers that
disagree about which casts are redundant**. The released seed predates the W-RULE: it applies B.22
(*"operands must be the same type — no promotion"*) and therefore **requires** the cast. A
locally-built gen1 has `widen_int_binop` and therefore **proves that same cast a no-op**. Under
D1-as-error no single source text satisfies both — measured on wagon 20: of the 18 casts the seed
demands, **17 make gen1 reject the tree outright** (`teko test .` aborts, 0/1008). That
contradiction is what the seed-fallback ladder exists to route around. As a warning, one text
compiles under both, and the seed's B.22 debt becomes payable.

**What holds the line instead: D4, unchanged and still hard.** The ARITH-CAST-RATE ≤ 2% gate
(§5, `metrics.tks`, `run_arith_cast_gate`) **fails the build**. The owner named it in the same
breath as the reversal for exactly that reason — a warning does not exempt the compiler's own source
from being ≥98% cast-free. One consequence in the implementation: the same-type site keeps the
`TCast` node in the typed tree instead of folding it away, because `expr_has_conversion` counts
`TCast` nodes; folding it would have quietly disabled the ceiling along with the error.

**Sequencing note (SUPERSEDED).** The 2026-07-24 ruling required the error to be enabled **in the
SAME wagon as the C4 sweep** ("varre → liga") so the corpus was never red between wagons. With D1 a
warning there is nothing to sequence: a redundant cast never reds a build, so the sweep lands
independently of the diagnostic.

**Lossy-cast form (D2 — REJECTED, owner 2026-07-24).** Owner, literal: *"não gostei."* **NO new
cast surface** — `to!` and `narrow()` are **dead**; **`to` remains the one and only cast form**.
Rationale accepted: with **D1**, every surviving `to` is **flagged if it is not intentional** (a
redundant one warns, and D4 caps how many may accumulate), so a distinct lossy form buys nothing.
The rejection was originally argued from *D1-as-error* — "a redundant one won't compile" — and the
2026-07-27 reversal to a warning does **not** reopen it: the argument was that a distinguished lossy
spelling adds nothing on top of a diagnostic that already names every no-op cast, and a warning
names them just as precisely. **The protection against
silent truncation stays exactly the PHASE16 one — a *checked* cast that fails loud at runtime** —
which is **already fully implemented today** (see §4.1); there is **no implementation gap**, so
nothing here becomes a crumb and nothing becomes new syntax.

### 4.1 Where the checked-narrowing guard lives today

> **CORRECTED 2026-07-30 — this section used to be headed "confirmed — no gap" and its own
> "Gap assessment" below read *"the CHECK logic is complete and consistent across all execution
> paths — no real implementation gap."* That was FALSE, and it was false in the worst available
> way: a guard attributed to a ruling, asserted present, and absent from one of the two routes, so
> nobody suspected it. Measured 2026-07-30 on `teko 0.3.0.31-beta`, native route:
> `300 to u8` answered **300**, `2^40 to i32` answered **0**, `2^40 to u64` from an `i64` answered
> **0**, `-5 to i8` answered **4294967291**, `-1 to u32` answered **4294967295**. Every one of those
> five was a native-vs-C divergence — the C route panicked or kept the value correctly on all five —
> so by the standing oracle rule each was a native bug.
>
> The cause: the audit below inspected the C emitter and the runtime, and read "all execution paths"
> off a list that never contained the native backend. `lower_cast` (`src/lir/lower.tks`) emitted a
> bare `Trunc`, and `Trunc` selects a 32-BIT move on both isels — so it did not even truncate to the
> destination's width, let alone check the fit. Closed by `lower_cast_fit_guard` (0.3.1 aridade
> numérica): the destination's bounds, an `icmp` per end that the source can actually leave, and a
> trap block calling the SAME `tk_panic_cast` the C route's helpers call. After the guard proves the
> fit, NO conversion instruction is emitted at all — a value representable in both types already
> carries one shared 64-bit two's-complement pattern — which is why neither isel needed a new case.
>
> The one width table both routes now ask is `checker::cast_is_lossless_widen`;
> `codegen::cast_may_lose` is its literal complement. It used to be a second, independent copy, and
> the native route had no copy at all. **When auditing a guard, enumerate the EMITTERS, not the
> helpers** — a helper that is never called is indistinguishable from a helper that does not exist.

The PHASE16 "checked, fail-loud, no silent truncation" protection on the C route is live, and D2's
rejection rests on it, so it is recorded here against the source:

- **Runtime (source of truth):** `src/runtime/teko_rt.h:752-769` — `tk_to_u8_s`/`tk_to_u32_s`/
  `tk_to_u64_u`/`tk_to_i8_s`… range-check the value and call `tk_panic_cast()` when it doesn't fit;
  mirrored in the Teko twin `src/runtime/teko_rt.tks:685+` (`panic_cast` at `:652`, "the `x to T`
  guard — B.36 / M.1"). Positioned panic: `_tk_cast_loc_line`/`_tk_cast_loc_col`
  (`teko_rt.h:577-578`) print `line:col:` (C1.7-CAST), for positioned panics.
- **Codegen (native):** `src/codegen/codegen.tks:2353-2431` `emit_cast` routes a narrowing int→int
  cast (`cast_may_lose`, `codegen.tks:2114-2120`) and every float→int cast through
  `tk_to_<dst>_<carrier>` inside a statement-expression that sets the cast position. Widening /
  same-type casts emit a bare C cast (no guard needed).

**Gap assessment (SUPERSEDED, kept as written so the error is legible rather than tidied away —
see the correction at the head of this section):** the CHECK logic is complete and consistent across all execution paths — **no real
implementation gap.** One adjacent note (NOT a gap in this issue): the runtime helpers currently
carry values in `__int128`/`unsigned __int128` (`teko_rt.h:752+`); **drop-128 R1** narrows those
carriers to `u64`/`i64` as part of its own plan — this doc's C1–C5 run *after* that narrow and
inherit the u64 carrier. No action here.

---

## 5. The determinism metric (owner ruling #1: ">=98% determinístico")

**Chosen metric (most auditable, cheapest — computable by one grep + `wc`):**

> **CAST-DENSITY** = `count( x to <numtype> in src/**/*.tks ) / KLOC`.
> **Baseline (2026-07-24):** 2649 / 100.236 = **26.4 casts/KLOC.**

**Secondary headline metric (the "≥98% sem cast" the owner named):**

> **CAST-FREE-LINE-RATE** = `1 − ( lines-containing-a-numeric-cast / total-nonblank-lines )`.
> Baseline: ~2,300 cast-bearing lines / 100,236 = **~97.7% cast-free today** already at the *line*
> level — so the ≥98% target is measured on the **arithmetic/assignment surface**, not raw lines,
> where the concentration is real.

**The target metric that actually bites (recommended as THE gate):**

> **ARITH-CAST-RATE** = `conversions in an arithmetic/assignment expression /
> total arithmetic+assignment expressions`. Target: **≤ 2%** (i.e. ≥98% of arithmetic/assignment
> flows carry NO conversion). **A "conversion" is a bare `to` node OR a `teko::casting::*` call**
> (D5 refinement, owner 2026-07-24) — both are counted together, since both are the raríssima
> exception the gate bounds; routing an inevitable narrow through `casting::*` to dodge the panic
> does NOT dodge the metric. Measured by the same probe that powers the D1 error (it visits every
> `TBinary`/`TAssign`/`TBinding` and tags whether a `TCast` OR a `casting::*` `TCall` appears in the
> operand tree). This is the honest expression of "≥98% determinístico" and is computed for free as
> a byproduct of crumb C5's probe.

**Projected post-sweep:** deleting the ~120 `.len` narrows + the ~306 `to u32)` boundary narrows
that move to a single guarded site + the class-1/class-3 no-ops (conservatively 1,600–2,000 of the
2,649) lands CAST-DENSITY near **6–10/KLOC**, dominated by the genuine ABI/serialization boundary
casts that are the deliberate exceptions. Report exact before/after in the crumb-5 gate.

**Every surviving conversion is a documented exception** — a bare `to` (panics, positioned) OR a
`teko::casting::*` call (returns `T | error`, no panic). Each falls in one of: (a) wire/ABI width
(ELF/Mach-O/COFF/DWARF fixed 32-bit fields — one converter at the serialization boundary);
(b) intentional truncation with a guard (hash folding, masking); (c) int↔float / float width change
(B.38 explicit). The **choice between `to` and `casting::*` is the §10.2 policy**. Every surviving
site should carry a Javadoc `@see` or one-line rationale so the audit is self-describing. Both forms
are raríssima by mandate — owner: *"deve ser usado tão raramente quanto o `to`."*

---

## 6. Crumb sequence (ordered, estimated, chain-fitted)

**Chain fit.** These sweeps are **corpus-wide** and therefore engage **after the `ref` linchpin
(SW3/SW4) and after drop-128's R1 u64-narrow** — touching the corpus before `ref` adoption or
while i128/u128 still exist would churn the same files twice and break the seed chain
(`wave-0.3.1-plan.md` §1 stable-seed discipline; `drop-128-family-0.3.1.md` R1). Slot: **after
SW4 (ref adoption complete) and after drop-128 surface removal**, as its own sub-wave cutting a
`0.3.1.N-beta` seed. The checker *rule* (crumb C1) is additive and may land earlier (it only
*widens* what's accepted — no corpus depends on the old rejection), but the *sweeps* (C3/C4) wait
for the clean base.

| # | crumb | size | gate | depends on |
|---|-------|------|------|-----------|
| **C1 — DONE** | **Width rule in the checker.** `int_arith_join`/`float_arith_join`/`widen_operand` in `expr.tks`; rewire `type_binary` arith + arith_bitwise arms; honest-stop for the no-lossless-common case; synthetic widening `TCast`. Additive (only widens acceptance). | **M** | fixpoint gen2==gen3 (checker self-hosts); rota C e backend nativo on a new arith fixture matrix; existing corpus still compiles (the manual casts are now redundant but still legal — no-ops) | drop-128 R1 (fixed set is `u8…u64`/`i8…i64`) — landed WITHOUT waiting for drop-128: `int_arith_join`/`float_arith_join` are generic over `prim_width`/`prim_is_signed`, so they are correct for the CURRENT fixed set (this base still includes `i128`/`u128`) and need no rework once drop-128 lands |
| **C2 — DONE** | **Backend confirm = no-op.** Verify the synthetic widen lowers through `cast_int_unop_of`/`emit_cast` byte-identically to a hand-written `to`; add a differential fixture (`a:u32 + b:u64` with & without the manual cast → identical binary). **No backend code change.** | **S** | native gate; emit goldens re-baselined ONLY where a genuine widen now differs (expected — §7) | C1 — confirmed by inspection (`widen_operand` emits the SAME `TCast{expr;type}` shape `type_cast` builds for a manual `to`, so `lir/lower.tks::cast_int_unop_of`/`codegen.tks::emit_cast` are untouched) plus the `width_rule_same_sign_widen`/`width_rule_mixed_sign_peer_ok` rota C e backend nativo fixtures |
| **C6 — DONE** | **`teko::casting` stdlib module (D5 refinement — no-panic checked converters).** New module `src/casting/casting.tks` (namespace `teko::casting`); per-source→dest checked converters returning `T \| error` (§10 surface). Additive; **built and SEEDED before C3/C4 so the inevitable narrows they meet can route to `casting::*` (error) instead of a bare `to` (panic).** Family derived from the *surviving-narrow* inventory, NOT the cartesian product. | **M** | full gate; rota C e backend nativo on every converter's round-trip + at-boundary reject proof; **100% coverage (W15/D39)**; each converter has an executable `.tks` proof | C1 (fixed set) — shipped 8 converters (`u64_to_u32`, `i64_to_u32`, `u64_to_u8`, `u32_to_u8`, `u64_to_u16`, `u32_to_u16`, `i64_to_i32`, `u32_to_i32`), each with an in-range + out-of-range `#test` (`src/casting/casting_test.tkt`) plus the `casting_native_roundtrip` rota C e backend nativo regression fixture |
| **C3** | **Signature sweep (`.len` etc.).** Widen internal count/length/offset params from `u32`/`i32` to `u64` where no wire reason; the genuine wire narrow becomes **one `casting::*` call (recoverable flow) or one guarded `to` (internal invariant) at the serialization boundary** (§10.2 policy); delete the 15 `.len to u64` no-ops. ~30–50 decl edits → ~120 call-site cast deletions. | **L** | full gate; per-file fixpoint; the 90 `.len to u32` panic edges gone (assert via an overflow fixture that used to panic) | C1, C2, **C6**, ref adoption (SW4) |
| **C4** | **Cast sweep + `redundant cast` ERROR in ONE wagon (D1, "varre → liga").** Delete class-1 (same-type) + class-3 (W-RULE-redundant) + class-2 (literal-context) casts, densest in `codegen/encode_*`; route any *inevitable* narrow uncovered here to `casting::*` or a guarded `to` per §10.2; **the SAME commit turned the `redundant cast` diagnostic ON as a hard ERROR** (no warn phase — owner 2026-07-24) so the corpus was **never red between wagons**. **That diagnostic is a WARNING since the 2026-07-27 reversal** (§4/§9) — the sweep this crumb performed still stands, only its enforcement moved to D4's ≤2% gate. Driven by the crumb-5 probe's candidate list, each removal fixpoint-verified; the probe must report **zero** candidates before the error flips on inside the wagon. | **L** | full gate; gen2==gen3 after every file batch; rota C e backend nativo unchanged; a seeded redundant cast is diagnosed (as an ERROR when this crumb landed, as a WARNING since 2026-07-27); CAST-DENSITY reported | C3, C6 |
| **C5** | **Metric gate + probe.** Ship the ARITH-CAST-RATE probe and wire it into CI (≤2%, D4) so the class cannot return. **The probe counts a bare `to` AND a `teko::casting::*` call as the SAME "conversion" unit** (§5) — both are the raríssima exception the gate bounds. **No D2 surface work** — removed by the owner's ruling; the anti-regression diagnostic already landed inside C4 (as an error then, as a warning since 2026-07-27), which is precisely why THIS gate is the one that has to bite. | **M** | full gate; CI metric gate green (≤2% counting `to` + `casting::*`); the D1 diagnostic stays silent on a genuine boundary cast, fires on a seeded no-op | C4 |

**Ritual points (full gate must pass):** end of **C1** (rule cemented — the seed everything else
dogfoods), end of **C6** (the `casting` module is a seeded stdlib surface the sweeps depend on —
100% coverage), end of **C3** (`.len`/signature shift — highest blast radius), end of **C4** (corpus
clean **and** the D1 error live — the "varre → liga" wagon, the single most sensitive gate since it
both deletes and forbids in one commit), end of **C5** (metric gate guards the door). C2 is an
internal confirm, not a ritual.

---

## 7. Regression fixtures (inputs → expected exit, rota C e backend nativo)

Add under `examples/regressions/` (or the .tkt suite per `tkb-regression-format.md`). Each runs on
rota C e backend nativo; exit codes must match.

| fixture | source shape | C rota exit | native exit | proves |
|---------|-------------|---------|-------------|--------|
| `width_same_sign_widen` | `let a: u32 = 300; let b: u64 = 7; return (a + b) to i64 … ` (result u64, value 307) | 307 | 307 | W-RULE §2.1 (u32+u64→u64, no cast) |
| `width_mixed_sign_ok` | `let a: u32 = 5; let b: i64 = -2; return a + b` (→ i64, value 3) | 3 | 3 | W-RULE §2.2 lossless (unsigned into wider signed) |
| `width_mixed_sign_reject` | `let a: u32 = 5; let b: i32 = -2; return a + b` (equal-width mixed sign) | **compile error (exit 1)** | compile error | W-RULE §2.2 honest-stop (no lossless common type); rota C e backend nativo identical |
| `width_int_float_reject` | `let a: i32 = 5; let b: f64 = 1.0; return a + b` | **compile error** | compile error | §2.4 unchanged (B.38 int×float); rota C e backend nativo identical |
| `width_float_widen` | `let a: f32 = 1.5; let b: f64 = 2.0; return (a + b) …` | ok | ok | §2.3 float widen |
| `width_literal_adopt` | `let n: u32 = 10; return n % 3` (no cast) | 1 | 1 | §2.5 literal sits below the rule (regression of .29) |
| `len_is_u64_nocast` | `let s = "abc"; let n: u64 = s.len; return n to i64 …` (no cast on .len) | 3 | 3 | `.len` u64 policy — no widen needed |
| `len_narrow_needs_guard` | `.len to u32` on a >4GiB-conceptual value (or the smallest fixture that exercises the guard path) | panic (nonzero) | panic (same) | Failure-2 guard still fires where a real narrow survives |
| `redundant_cast_same_type` | `let x: u32 = 5; return (x to u32) to i64 …` | **compile error (exit 1)** | compile error | D1 as **direct error** on class-1 (post-C4 — no warn phase) |
| `redundant_cast_legal_before_C4` | `let x: u32 = 5 to u32; return x` (the .29 fixture `lit_redundant_cast_still_ok`) | 5 (pre-C4) | 5 | the redundant cast compiles UNTIL its C4 wagon deletes it + flips the error — proves the "never red between wagons" ordering |

**Preserve** the existing `literal-context-typing.md` fixtures (`lit_redundant_cast_still_ok` et al.)
and the `own_arith_exit`/`own_sub_exit` arithmetic goldens — the width rule must not regress them.
Note: after C4, `lit_redundant_cast_still_ok`'s body is rewritten to a non-redundant form (or moved
to expect the D1 error) — it must not both survive and remain a no-op once the error is live.

---

## 8. Risks & law tensions (honest)

1. **Emit goldens re-baseline (EXPECTED break).** Where a manual DOWN-equalize (`… to u32`) is
   deleted and the width rule now computes at a **wider** type, the emitted bytes change (different
   register width, no `Trunc`). This **breaks fixpoint/goldens by design** on those exact sites —
   crumb C2/C4 must **re-baseline** and prove the new bytes are correct (value-equal, wider
   register). Mitigation: sweep file-by-file under `gen2==gen3` + rota C e backend nativo so a wrong widen is
   caught immediately; never batch the re-baseline blind.

2. **Signed×unsigned comparison semantics.** The width rule for *arithmetic* is new, but comparison
   already sign-checks. Risk: a site that today writes `(i to u32) == id` (Failure 1) is deleted to
   `i == id` (i64 vs u32) — the sign-check compares by value, which is **more correct** than the
   truncating cast it replaces, but if any site *relied* on the truncation wraparound as behavior
   (rather than a mistake), deleting it changes semantics. Mitigation: crumb C4 removes each cast
   under fixpoint+rota C e backend nativo; a behavioral change surfaces as a fixture/gate diff, not silently.

3. **`.len` declaration widening ripples (C3, highest blast radius).** Widening a `u32` count param
   to `u64` touches every caller and can cascade into the backend's own width math. Mitigation: C3
   is gated as a ritual; do it AFTER C1/C2 cement the rule so cascaded mixed-width math just works
   instead of demanding new casts.

4. **Genuine wire-format narrows must survive.** ELF/Mach-O/COFF/DWARF have real 32-bit
   on-wire fields; the sweep must NOT delete those narrows — it must **relocate** them to one
   guarded boundary cast (§3, §5). Risk: over-eager sweep corrupts an object file. Mitigation: the
   probe (crumb 5) classifies a cast as boundary (feeds a serialization write) vs internal; only
   internal ones are auto-deleted; boundary ones are hand-reviewed and documented.

5. **Diagnostic-as-error timing (D1, owner-ruled DIRECT ERROR — no warn phase).** Turning the
   `redundant cast` error on while the corpus still has a straggler would strand the seed chain
   (corpus red between wagons). Mitigation is the owner's **"varre → liga" ordering**: the C4 wagon
   deletes every candidate AND flips the error on in the **same commit**, gated on the probe
   reporting **zero** candidates first — so the error never precedes a clean corpus and the corpus
   is never red between wagons.

**No unresolved law tension → no HALT.** The width rule is a law-first *completion* of B.22's
comparison sign-check into the arithmetic regime; it strengthens M.1 (fail-loud, no silent loss —
the mixed-sign lossy case still stops) and honors M.0/M.3 (fixed widths, no platform dependence).

---

## 9. Decisions — CLOSED (ruling-level, owner 2026-07-24, counter-round cumprido)

All five are ratified by the owner; none remains open. Attribution `owner 2026-07-24`; literal
owner text quoted where given. **One has since been REVERSED — D1, on 2026-07-27 — and it is
recorded below as the reversal it is, not rewritten to look like the original ruling.**

- **D1 — `redundant cast` = WARNING. RATIFIED 2026-07-27, REVERSING the 2026-07-24 DIRECT ERROR.**
  Owner, literal (2026-07-27): *"o cast, nestes casos, não deveria ser falha, deveria ser warning, o
  que não nos exime de não ter warnings no próprio compilador (lembra? <= 2%)."* Superseded ruling
  (2026-07-24), literal: *"prefiro que falhe para evitarmos de cair no mesmo erro."*
  **Why:** as an error the diagnostic was unsatisfiable across the bootstrap chain — the seed's B.22
  demands the cast, gen1's W-RULE proves it redundant, and 17 of 18 measured cases made gen1 reject
  the tree. **What replaces the enforcement:** D4's ARITH-CAST-RATE ≤ 2%, which still fails the
  build and is untouched. **Sequencing:** the "varre → liga" note is void — a warning never reds a
  build, so nothing needs to land in the same wagon as the C4 sweep. (§4, crumb C4.)
- **D2 — lossy-cast distinct form. REJECTED.** Owner, literal: *"não gostei."* **No new cast
  syntax** — `to!`/`narrow()` are dead; **`to` remains the only cast form.** Every no-op `to` is
  named by D1 (as a warning since 2026-07-27) and bounded by D4, so a distinguished lossy spelling
  adds nothing. Silent-truncation protection stays the **PHASE16
  checked cast (fail-loud at runtime)**, confirmed already implemented in §4.1
  (`teko_rt.h:752-769` / `teko_rt.tks:685+` / `codegen.tks:2353-2431 emit_cast`) — **no
  implementation gap, so nothing here becomes a crumb or new grammar.**
- **D3 — signed×unsigned = peer-type (Zig). RATIFIED.** Implicit widen only when a fixed-width type
  is a superset of both operands' value ranges; otherwise honest-stop for an explicit `to`. (§2,
  W-RULE.)
- **D4 — determinism metric = ARITH-CAST-RATE ≤ 2%. RATIFIED** as the CI gate (CAST-DENSITY
  reported as the headline number). Per the D5 refinement, the gate counts **`to` + `casting::*`
  calls together** as conversions. (§5, crumb C5.)
- **D5 — `.len`/count width policy. RATIFIED** in the owner's reading (*"tudo que vi seria
  resolvido trocando o tipo dos outros itens"*): the rule is to **SWAP THE DECLARANT TYPES to
  `u64`** (length/count params & fields), **not** to sprinkle casts. Narrowing survives **only at a
  single serialization site**, where the wire format genuinely requires it. (§3, crumb C3.)
- **D5-refinement — `teko::casting` no-panic checked converters. RATIFIED (owner 2026-07-24).**
  Owner, literal: *"prefira uma conversão checked em casos que não há escapatória, adicione um
  teko::casting::* para conversores checados que validam o valor e retornam a conversão ou erro,
  para evitar pânico, mas, novamente, é exceção e não regra, deve ser usado tão raramente quanto o
  `to`."* → new stdlib module `teko::casting` whose converters return **`T | error`**
  (errors-as-values, M.3) — the **no-panic** alternative for an inevitable narrow. The bare `to`
  (positioned panic) and `casting::*` (returns error) are **BOTH raríssima exceptions**, **both
  counted by the D4 gate**. Surface, family-derivation and the to-vs-casting choice policy are §10;
  it is crumb **C6**, seeded before C3/C4. This is a **stdlib surface, not new grammar** — fully
  consistent with D2's rejection of new cast *syntax*.

---

## 10. `teko::casting` — checked no-panic converters (D5 refinement, owner 2026-07-24)

New stdlib module `src/casting/casting.tks` (namespace `teko::casting`). Every converter **validates
the value and returns the conversion OR an `error`** — the no-panic sibling of the bare `to` (which
still panics, positioned). Mirrors the proven `teko::math::checked_*` convention
(`src/math/checked.tks` — `-> T | error`, per-type-suffixed names because Teko has **no overload by
param type**, so the name carries source AND destination).

### 10.1 Proposed surface (naming: `<src>_to_<dst>`)

```teko
/**
 * u64_to_u32 — checked narrowing of a u64 to u32; returns the value or an `error`
 * (NO panic — the errors-as-values sibling of `v to u32`). Use when the caller can
 * RECOVER from an out-of-range length/offset (§10.2); use the bare `to` only when an
 * internal invariant guarantees the fit and a violation should abort (positioned panic).
 *
 * @param  v  the source value
 * @return u32    v, when v <= U32_MAX
 * @throws error  when v > U32_MAX (does not fit the destination)
 * @see    teko::math::checked_add_u32 — the checked-arithmetic sibling this mirrors
 * @since  0.3.1
 */
pub fn u64_to_u32(v: u64): u32 | error { /* if v > U32_MAX { return error {…} }  v to u32 */ }
```

**Family derived from the SURVIVING-narrow inventory (NOT the cartesian product — owner mandate).**
The final set is fixed at crumb-C6 open from the C5 probe's tag of every *inevitable* narrow's
`src→dst`; the corpus's real narrowing directions today (targets < 64-bit + `byte`) are:

| dst | seen count (pre-sweep) | expected surviving sources | proposed converters |
|-----|-----------------------|----------------------------|---------------------|
| `u32` | 1314 (mostly equalizations that DELETE) | `u64` (wire offsets/indices, `.len`), `i64` | `u64_to_u32`, `i64_to_u32` |
| `byte`/`u8` | 370 + 33 | `u64`, `u32` | `u64_to_u8`, `u32_to_u8` (`byte` = `u8`, B.36) |
| `u16` | 13 | `u64`, `u32` | `u64_to_u16`, `u32_to_u16` |
| `i32` | 36 | `i64`, `u32` | `i64_to_i32`, `u32_to_i32` |
| `i8`/`i16` | 9 + 9 | `i64` | `i64_to_i8`, `i64_to_i16` (only if a survivor exists) |

Rule for inclusion: **a converter exists iff the surviving-narrow inventory has ≥1 site for that
`src→dst`.** No speculative pairs. Signed↔unsigned same-or-narrower (e.g. `i64_to_u32`,
`u64_to_i32`) are added only if the inventory demands them — each validates BOTH range ends. Expect
~6–12 converters, not the ~40 of a full product. **100% coverage (W15/D39):** every declared
converter carries an executable `.tks` proof (in-range round-trip + out-of-range → `error`) on rota C e backend nativo, or it does not ship.

### 10.2 Policy — which form to reach for (documented, D5 refinement)

| situation | form | why |
|-----------|------|-----|
| the flow can **recover** from a bad value (parse a length from untrusted input, a user-supplied size, a value that *might* not fit) | **`teko::casting::<src>_to_<dst>`** → `match` the `T \| error` | errors-as-values (M.3); no panic; the caller decides |
| an **internal invariant** guarantees the fit (a `.len` known ≤ a wire cap by construction, a masked value) and a violation is a **bug that should abort** | **bare `to`** (positioned panic) | fail-loud at the exact line:col; a violated invariant is not recoverable |
| widening / same-type | **neither** — the W-RULE handles it implicitly (§2) | no conversion node at all |

Both rows 1–2 are **raríssima by mandate** and **both count against the D4 ≤2% gate** — choosing
`casting::*` to dodge the panic does not dodge the metric. The choice is *panic-vs-recover*, never
*count-vs-not-count*.

### 10.3 Fixtures (add to §7, rota C e backend nativo)

| fixture | shape | C rota | native | proves |
|---------|-------|----|--------|--------|
| `casting_u64_to_u32_ok` | `match teko::casting::u64_to_u32(300) { u32 as v => v to i64 …; error => 99 }` | 300 | 300 | in-range returns the value |
| `casting_u64_to_u32_err` | `match teko::casting::u64_to_u32(0x1_0000_0000) { u32 => 0; error => 7 }` | 7 | 7 | out-of-range returns `error`, **no panic** |
| `casting_vs_to_parity` | same value through `casting::u64_to_u8` (ok arm) and `v to u8` | equal | equal | the converter's success path == the bare cast |
