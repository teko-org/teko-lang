# Drop the 128-bit family (i128 / u128) + f16 from the Teko language — carrier-detox then surface-removal (crumb plan)

> **[NOTA]** — este documento descreve oráculos diferenciais ativos durante o bring-up do backend nativo. Ambos os oráculos foram desde então **retirados** (#524 e seguintes); o restante deste documento é registro histórico do método usado, não descreve o estado atual do projeto.

**Status:** DESIGN (doc-only; owner ratifies before any code). Proposal of the owner, 2026-07-23.
Two scope messages fold in: (1) drop i128/u128 from the language (f128 never existed); (2) also drop
f16. Branch `design/drop-128-family`, base conceptually **`origin/main` AFTER the null-union pivot
(C6-C7), the corpus-wide `ref` adoption, and KP16 objfile land** — this wave engages *after the ref*.
Every file:line below is cited against `origin/main` as of this writing; **offsets WILL move** once the
`ref` adoption rewrites the corpus, so the implementer re-greps the named symbol, not the line number.

> **Owner ruling (proposed 2026-07-23).** Do not adopt i128/u128. Callers who need wider integers use
> `teko::numeric` BigInt / Decimal (landed in .30) or roll their own. Remove NOW the whole topology and
> backend for the 128-bit cases, and also f16, limiting the language to **integers 8–64 bit
> (u8..u64 / i8..i64) + floats f32/f64**. Motivation: the kill-C critical path becomes *float-only* —
> it deletes register-pair materialization, 128-bit ABI classification across the 4 ABIs, the legacy engine
> 128 arms, and the isel honest-stops. **Final surface: 8/16/32/64-bit ints + f32/f64.**

---

## 0. TL;DR — and the one thing the owner's fact-list under-counts

The removal is real and worth doing, but the framing "**6 internal load-bearing uses**" is optimistic.
Grounded against `origin/main`, **i128/u128 is the compiler's pervasive internal "universal integer
carrier"** — the widest type that holds *any* i64 **or** u64 value (a u64 with its high bit set needs 65
signed bits) without loss. It is not 6 spot-edits; it is **~11 carrier subsystems**, four of them large:

| Carrier subsystem | `origin/main` site | i128/u128 hits | Detox size |
|---|---|---|---|
| `parser::Number.value` + `checker::tast::TNumber.value` (literal) | `src/parser/ast.tks:10`, `src/checker/tast.tks:20` | few + wire | **M** |
| `checker::comptime_fold::CVInt.bits: u128` (const-fold value) | `src/checker/comptime_fold.tks:34` | **127** | **L** |
| `checker::typer::value_fits(v: i128, …)` + `tast::TPathExpr.value: u128` | `src/checker/typer.tks:1566`, `tast.tks:72` | 11 | M |
| `lir::LConstInt.val: i128` (LIR constant) | `src/lir/lir.tks:45` | — | S |
| `lir::lir_oracle` value carrier (`RegFile.values: []i128`, `IResult.value: i128`) | `src/lir/lir_oracle.tks:23,48` | **84** | **L** |
| `backend::minst_oracle` value carrier (mirror of the above) | `src/backend/minst_oracle.tks` | **85** | **L** |
| `lir::lower_const` const-fold + `lir::lir_print::i128_dec` | `src/lir/lower_const.tks:76+`, `lir_print.tks:34` | ~15 | M |
| `codegen` `cb_u128_digits` (emits decimal of u64-range values) | `src/codegen/codegen.tks:113+` | **75** | M |
| `numeric::bigint` `mag128: i128` (abs of `i64::MIN`) | `src/numeric/bigint/bigint.tks:454` | 2 | S |
| `parser::FlagsBody.values: []u128` (flags bit-values) | `src/parser/ast.tks:290` | 1 + json | M |
| **timestamps**: `teko::time` ticks + `teko_rt.c` datetime externs + `build::progress`/`project` | `src/time/time.tks:15-19`, `src/runtime/teko_rt.c:2445`, `src/build/progress.tks:85`, `src/build/project.tks` (11 fns) | ~20 | M |

**The two legacy engines (169 combined hits) and `comptime_fold` (127) are the real weight**, and the
legacy engines carry a *behavioral* choice (§9-g). f16, by contrast, is **zero-use** and a **pure Wave-B
deletion** (§8), so it costs nothing to fold in.

**Structure — TWO waves, each crumb independently gate-able (GATE-G = build seed + `test` + fixpoint
`gen1==gen2`):**

- **Wave A — CARRIER DETOX (A1–A7).** Rewrite the ~11 internal carriers so **no compiler-internal
  declaration is typed `i128`/`u128`**, while the **surface still accepts** i128/u128/f16. Immediate,
  bankable win the instant Wave A closes: **native self-host stops needing 128-bit isel** (the corpus it
  compiles no longer contains an i128 value), so the isel honest-stops become unreachable.
- **Wave B — SURFACE + TOPOLOGY REMOVAL (B1–B6).** Checker rejects `i128`/`u128`/`f16` with an honest
  diagnostic (pointing at `bigint`/`dec`/`f32`); rejection tests; delete the backend topology
  (isel routes, 128-bit ABI classification, legacy engine width-128 arms, `lower`, `PrimKind::{I128,U128,
  F16}` and its whole match cascade, `LType::{I128,F16}`); sweep corpus/tests/fixtures; amend B.38; docs.

Wave A must land **first** and **whole** (a half-detoxed corpus still forces 128-bit isel). Wave B is
mechanical once Wave A holds.

> **Three owner proposals were validated (2026-07-23).** Plan B (swap internal carriers to
> `teko::numeric::BigInt`) — **rejected** (§0.9: arena blow-up in the hot fold/oracle paths, overhead
> everywhere else, impossible across the `teko_rt` C ABI). Plan C (accept i128/u128 syntax but silently
> undersize to 64) — its *mechanism* is sound but its "compile em falso" carries an M.3 honesty debt, and
> its f16→f32 "existing precedent" is **false** (§0.95: f16 is a real distinct `_Float16` type).
> **The owner's REFRAME is decisive (§0.99): 128 is surface-SUPPORT, not intrinsic compiler use** — the
> only real producers of >64-bit values are the 128 *tests*. Delete those first and the narrowing is a
> mechanical rename whose correctness is proven **empirically by the gate** (fixpoint + suite + own==C),
> collapsing Wave A from 7 crumbs (4×L) to **1 mechanical narrow + 2 point-fixes**, and letting the whole
> issue — narrow + surface **rejection** + topology deletion + f16 + sweeps + B.38 — land in **.31**.
> **RECOMMENDED ROUTE + collapsed 4-crumb sequence: §0.99 / §3.1.** (The per-carrier detail in §3/§4 is
> retained as the implementer's site map.)

---

## 0.9 Owner's BigInt proposal — validation, per-subsystem, VERDICT

Grounded against the shipped `src/numeric/bigint/bigint.tks` (`origin/main`). The BigInt API is: `BigInt
= struct { neg: bool; limbs: []u64 }` — **sign-magnitude, base-2³² limbs** (a 64-bit value is TWO
limbs); public ops `of(i64)`, `of_lit(str)`, `from_str`, `to_str`, `add/sub/mul/div/rem/neg/abs/cmp/eq/
is_neg/is_zero`. Three facts decide everything below:

- **(F1) NO public bitwise/shift surface.** There is no `and`/`or`/`xor`/`not`/`shl`/`shr`. Bit access is
  private (`mag_bit`, `mag_setbit`, `mag_shl1`). BigInt is sign-magnitude **arbitrary precision**, so it
  has **no two's-complement model** — `~x`, `x << k`, wrap, and width-masking are undefined on it.
- **(F2) NO `of_u64` / `to_u64` / low-limb extraction in the public API.** `of` takes **i64** — a
  high-bit u64 literal (e.g. `u64::MAX`) cannot even be *constructed* without new API or `of_lit(decimal)`.
- **(F3) Every op ALLOCATES.** `mag_add`/`mag_mul`/… return fresh `[]u64`; `from_str` allocates per digit.
  Under the M.0 no-free arena, every BigInt value and intermediate is a permanent allocation.

### Per-subsystem: is BigInt-swap cheaper than the proven u64-narrow?

| Carrier | BigInt-swap verdict | Why |
|---|---|---|
| `comptime_fold::CVInt.bits` (**127 hits**) | **TRAP — much worse** | Does two's-complement `&`/`\|`/`^`/`~`/`<<`/`>>` + width-mask. BigInt has none (F1) and is sign-magnitude — you'd **re-implement a two's-complement layer on top of BigInt**. The u64-narrow is a mechanical `u128→u64` (all widths ≤64; existing u64 bit-ops just work). |
| `lir_oracle` + `minst_oracle` (**169 hits**) | **TRAP — worse + slower** (point 2) | The oracle must MATCH native's 64-bit wrap ⇒ `rem 2^64` (an allocation) after *every* op. i64-wrapping IS the native model, zero alloc. See §9-g. |
| `parser::Number.value` / `TNumber.value` (literal) | **Pure overhead** (point 4) | Once the surface is removed, a literal's range is **already ≤ u64::MAX** by construction (see below) — a BigInt to hold a guaranteed-u64 value, needing a new `of_u64` (F2) and per-literal alloc (F3). `{neg,mag:u64}` is strictly cheaper and sufficient. Also feeds bitwise literal fold (`~5`, `1<<40`) ⇒ needs the width layer BigInt lacks (F1). |
| `lir::LConstInt.val` + `lower_const` + `lir_print` | **TRAP — nonsensical** | The backend materializes a **machine constant** into a register; a limb-vector cannot be a `mov imm`. Must be i64/u64. |
| `codegen::cb_u128_digits` (75 hits) | **Overhead** | Prints decimal of *u64-range* mark values; `to_str(BigInt)` allocates to print a u64. `cb_u64_digits` is trivial. |
| timestamps (`teko::time` + `teko_rt` C ABI) | **IMPOSSIBLE** | `tk_rt_datetime_*` marshals a struct **by value across the C ABI**; a BigInt `{neg, []u64}` cannot cross it. Must be `i64` ns. |
| `typer::value_fits` | **Neutral** (slight win) | With BigInt it is `cmp(v, min)>=0 && cmp(v, max)<=0` — clean. With `{neg,mag}` it is a sign-split compare — also clean. Not a differentiator. |
| `TPathExpr.value` (u128, enum ordinal) | **Overhead** | Small non-negative ordinal; u64 is trivially cheaper. |
| `FlagsBody.values` (u128) | **Neutral / one real capability** | BigInt is the ONLY place it *adds* something: flags could exceed 64 members. But a flags mask must be a **machine-word** at runtime (native `&`), so a BigInt mask breaks the runtime model. The 64-cap + u64 is the pragmatic answer; >64-member flags is a separate feature, out of scope. |
| `numeric::bigint::mag128` | **N/A** | It is inside BigInt itself; must be u64-narrow regardless (A7). |

**Conclusion (point 1):** BigInt-swap is *mechanical and safe* in **essentially zero** of the carriers
that matter. It is a **trap** wherever two's-complement/bitwise/wrap semantics live (const-fold, oracles,
LIR/backend), **pure overhead** where the value is already u64-bounded (literal, ordinal, codegen digits),
and **impossible** where a C ABI or a machine register is crossed (timestamps, LConstInt). The one place
it adds capability (flags > 64) is out of scope and breaks the runtime mask model. **The "hybrid where
BigInt is mechanical" collapses to "u64-narrow almost everywhere"** — there is no carrier where BigInt is
*both* cheaper *and* safe.

**Point 2 (legacy engines) — CONFIRMED.** i64-wrapping is better *and* more faithful; BigInt+mask reimposes
the wrap BigInt was supposed to abstract, at an allocation per op. Keep decision §9-g (i64-wrapping).

**Point 3 (M.0 / arena blow-up) — REAL, and disqualifying for the hot paths.** `comptime_fold` runs over
the WHOLE corpus at compile time; the legacy engines run the whole `.tkt` gate — both are long, no-free
executions. Swapping their O(1) scalar carriers to BigInt turns every constant/fold/register-write into a
**permanent limb-vector allocation** (F3) on an arena that already peaks ~780 MB during self-host. The
blow-up is unbounded-in-principle (proportional to fold/oracle op count, which scales with corpus size)
and would land as a *self-inflicted* transitional regression that .32 then has to *undo*. **Not acceptable
even transitionally** for `comptime_fold` and the legacy engines. (For a handful of literal values it would
be tolerable — but those are exactly the places BigInt buys nothing.)

**Point 4 (fixpoint / tkb / literal range) — resolved, and it removes BigInt's supposed advantage.**
- *Determinism:* BigInt is pure deterministic Teko, so `gen1==gen2` would still hold — but the swap
  changes the emitted C, and the .32 narrow changes it *back*, so Plan B pays **two re-baselines per
  carrier** where Plan A pays one. Strictly worse for the fixpoint budget.
- *Literal range — the key question:* "with surface removal in .31, what literal range is accepted?"
  **Once the type `i128`/`u128` is deleted, there is no type to hold a value in `(u64::MAX, i128::MAX]`.**
  A literal is therefore typed as one of `u8..u64` (≤ `u64::MAX`) or is a **`bi` BigInt literal** (a
  *separate* node lowering to `of_lit`, NOT a `Number.value`). So `Number.value`'s range is pinned to
  `[i64::MIN, u64::MAX]` **by the surface removal itself** — the `{neg,mag:u64}` contract of A1 stands
  **either way**. BigInt does **not** let us "defer" the range decision; the decision is made the instant
  the surface types go. Therefore a BigInt `Number.value` is arbitrary precision holding a
  provably-≤-u64 value — pure overhead (F2/F3). **`{neg,mag:u64}` remains the A1 contract (§9-a).**
- *tkb wire:* unchanged from §7 — the `{neg,mag}` → hi:lo derivation is compat, no bump. (A BigInt carrier
  would serialize its low 128 bits from limbs and honest-stop past 128 — strictly more code for the same
  wire, and moot given the ≤u64 range above.)

**Point 6 (seed30) — both plans compile under seed30.** BigInt is .30 corpus (Plan B *would* build). But
Plan A needs **no capability seed30 lacks**: `{neg,mag:u64}` is a plain struct; the sign→hi:lo wire uses
`u64` `0 - mag` + bit-ops; `numint_fits` uses sign-split compares — all pervasive in today's corpus.
**No crumb (either plan) requires a post-seed30 feature.** Confirmed.

### VERDICT (Plan B)

**Reject Plan B (BigInt carrier swap)** as the internal carrier mechanism. BigInt is the right tool for
the *user-facing* escape hatch (`123bi`) and stays exactly that; it is the wrong tool for the compiler's
internal machine-integer carriers. See §0.95 for Plan C (undersizing) and §0.99 for the owner's reframe,
which supersedes the carrier debate and yields the single recommended route.

---

## 0.95 Plan C — "undersizing" (accept i128/u128 syntax, silently narrow to 64) — validation

The owner's fallback: keep the front-end *accepting* i128/u128/f16 syntax but map them to 64-bit/f32
("compilar em falso"), delete the whole 128 border (emitters, gates, tests, backend topology), and clean
the residue in .32 — on the claim that "a lógica já existe e os emissores também" (the f16→f32 undersize
supposedly already exists).

**Point 1 — the f16→f32 precedent DOES NOT EXIST (premise is imprecise).** Verified against
`origin/main`: f16 is a **fully-realized distinct type**, never an f32 alias —
- `codegen.tks:6109-6111` emits `(_Float16)(…)` for an f16 literal (a real half, not a promoted float);
- `lower.tks:184` maps `PrimKind::F16 → LType::F16` (its **own** LType, not F32);
- `lir.tks:540` sizes `F16 => 2` (a real 2-byte half, not 4).
There is **no** f16→f32 promote/widen anywhere in lower/codegen/fold/typer. Symmetrically, there is **no**
i128→i64 undersizing precedent — i128/u128 have real register-pair lowering (the topology we are
deleting). So the claim "the emitters already exist" is **backwards**: the existing emitters produce the
128/f16 code being removed. The undersizing map is **net-new** (≈3 resolver arms:
`"i128"→I64`, `"u128"→U64`, `"f16"→F32`), cheap but not a reused precedent.

**Point 2 — catalog of internal sites that are NOT 64-safe under a blind narrow.** A blanket "i128→i64"
map silently breaks wherever the compiler's OWN source depends on the wide carrier:
- **(a) `Number.value`** — `parse_lit::lit_int` accumulates in i128; positive **u64-range literals
  `2^63 .. 2^64-1`** do not fit i64. These occur in the compiler ITSELF — e.g. the mask
  `0xFFFFFFFFFFFFFFFF` at `tkb_write.tks:100-101,178,601`, the `0x8000000000000000` sign boundaries in
  `minst_oracle`. A blind i64 narrow makes `value_fits`'s unsigned gate (`v >= 0`) reject them and the
  fold read them wrong → **the compiler miscompiles itself → fixpoint break.** This is NOT a transitional
  wart; it is disqualifying. **Requires the sign+65-bit carrier fix (A1-lite) regardless** — Plan C does
  NOT avoid A1.
- **(b) `bigint` `abs(i64::MIN)`** — needs 65 bits transiently (A7 two's-complement u64 trick).
- **(c) `comptime_fold`** — `width_mask(w)`/`msb_value(w)`/`twos_neg(x,w)` (`comptime_fold.tks:148-215`)
  already special-case the **top width** (`1<<128` "cannot be spelled", so all-ones is hard-coded).
  Narrowing moves that special-case threshold **128 → 64** (the value `2^64-1` fits u64; only the
  intermediate `1<<64` needs the guard). This is a **mechanical adjustment of the existing guard**, not
  new math — the fold ops *mirror surface widths*, so with widths ≤64 they are 64-safe once the guard
  moves.
- **(d) legacy engines** — a 64-bit **wrap** is acceptable and becomes decision §9-g (auto-acquired).
- **(e) timestamps / flags / `TPathExpr`** — 64-safe under the proposed rulings.
So the non-64-safe set is exactly **(a), (b), (c)** — the same two intrinsic fixes as Plan A (A1, A7)
**plus** the mechanical `comptime_fold` top-width guard move. Everything else narrows mechanically.

**Point 3 — the M.3 honesty tension of "compilar em falso".** Accepting `let x: u128 = …` and silently
delivering 64 bits is anti-honesty by definition (M.3). The owner may suspend a Law in a cleanup wave,
but the doc must record the exception, its deadline, and its mitigation:
- **Deadline:** .32 closes the door with surface rejection.
- **Mitigation (recommended):** **reject any integer literal > `u64::MAX` even in undersized mode** —
  this removes the *worst* false-success (a literal that visibly needs 128 bits fails loudly), leaving
  only the benign case (a 128-typed variable holding a ≤64-bit value). Optionally emit a **WARN
  diagnostic** "i128 treated as i64 — transitional (0.3.1), removed in 0.3.2" instead of pure silence.
- **BUT** — see §0.99: the owner's own reframe makes surface **rejection** affordable in .31, so the
  honesty exception need **not** be taken at all. Plan C's "em falso" is a fallback, not the end state.

**Point 6 (seed30):** the undersize map + the three point-fixes use only seed30 features. Fine.

---

## 0.99 Owner's REFRAME — the decisive lens: 128 is surface-support, not intrinsic use

> *"Tirando os testes, não temos uso de 128 bits — o compilador não usa senão para compilar [código do
> usuário]."* — owner, 2026-07-23.

**This is correct and it collapses Wave A.** The ~11 internal carriers exist to **carry user 128-bit
values through the pipeline**, not because the compiler intrinsically computes in >64 bits:
`Number.value` is i128 to hold a *user's* 128-bit literal; `CVInt.bits` is u128 to fold a *user's* 128
constant; the legacy engines carry i128 to execute a *user's* 128-bit op; `flags []u128` to allow a
*user's* 128-member flags. **The only real producers of >64-bit values in an accepted program are the 128
tests/fixtures.** Delete those first, and **no value above 64 bits flows through any carrier** — so the
narrowing is mechanical and any >64-bit semantic divergence is **unobservable**.

**Consequence 1 — the preservation proof becomes EMPIRICAL, not analytic.** Order: **sweep the 128
tests/fixtures FIRST**, then narrow. The gate itself is the demonstration: **byte-identical fixpoint
(`gen1==gen2`) + the full `.tkt` suite + the own==C differential.** The fixpoint is an especially strong
net *here* because the compiler self-hosts — it exercises its OWN fold/oracle/carrier paths on its OWN
source (e.g. a wrong `width_mask(64)` would miscompile the compiler and break the fixpoint). No
site-by-site analytic proof is required; the three point-fixes (§0.95-a/b/c) are the only spots that
touch a genuinely-wide value, and each is guarded by a targeted fixture.

**Consequence 2 — the intrinsic exceptions are exactly TWO (+ one mechanical guard move):**
`Number.value` needs sign+u64-magnitude for a u64 literal (A1-lite, `{neg,mag:u64}`); `bigint`
`abs(i64::MIN)` needs the two's-complement u64 trick (A7). `comptime_fold`'s top-width special-case moves
128→64 (mechanical, part of the narrow). **Confirmed: no `CVInt` mask genuinely needs >64** — the mask
ops parametrize on the *surface* width, and the surface dies.

**Consequence 3 — re-estimate DOWN; the L's dehydrate.** Under the empirical lens the per-carrier
analytic rewrites of §3 (A2/A3/A4/A5/A6, sized L/M for a *proven* rewrite) **collapse into ONE mechanical
narrowing crumb** (rename the type names i128→i64 / u128→u64 across the 64-safe carriers; the values
already fit) **plus** the two point-fixes. `comptime_fold`'s 127 hits and the legacy engines' 169 hits are
now **mechanical renames**, not reasoned rewrites — M, not L.

**Consequence 4 — with the cost collapsed, EVERYTHING fits in .31.** Narrowing + surface **rejection**
(B1) + topology deletion (B2) + f16 kill + sweeps + B.38 all land now; .32 holds only residual polish.
Because the narrowing removes every 128 *producer*, `PrimKind::{I128,U128,F16}` and `LType::{I128,F16}`
have no producers → their members **and** the register-pair/ABI/oracle topology are dead → deletable in
.31 (no need to defer to .32). **The honesty exception of Plan C (§0.95-3) is therefore NOT taken** — we
reject the surface outright in .31 rather than "compile em falso," so M.3 holds.

### FINAL COMPARISON & SINGLE RECOMMENDATION

| | Plan A (analytic u64-narrow) | Plan B (BigInt swap) | Plan C (undersize "em falso") | **Recommended (C-mechanism + A-honesty, empirical)** |
|---|---|---|---|---|
| Carrier work | 7 crumbs, 4×L (proven site-by-site) | swap to BigInt | ≈3 resolver arms + 2 fixes, surface stays | **1 mechanical narrow crumb + 2 point-fixes (A1-lite, A7) + guard move** |
| Surface | reject in .31 | reject in .31 | accept-but-truncate (M.3 debt) | **reject in .31 (honest, M.3 holds)** |
| Topology (isel/ABI/oracle-128) | .32 defer | .32 | .31 possible | **delete in .31 (no producers left)** |
| Fixpoint risk | low (proven) | 2× re-baseline/carrier, arena blow-up | low IF fixes applied | **low — fixpoint+suite+own==C is the empirical proof** |
| Arena / M.0 | fine | **blows up (disqualifying)** | fine | **fine** |
| Dies in .31 | surface + carriers | surface + carriers | border + tests | **MAXIMUM — carriers + surface + topology + tests + f16 + spec** |

**RECOMMENDATION: adopt the reframe route** — the *mechanical narrowing* insight of Plan C (128 is
surface-support, so narrowing is a rename proven empirically by the gate) married to the *honest surface
rejection* of Plan A (no "compile em falso"), doing the **maximum in .31**. This is what the owner's
criterion (maximize what dies now, minimize fixpoint/gate risk) selects. Reject Plan B (arena blow-up).
The collapsed, re-ordered crumb sequence is **§3.1** below; the detailed per-carrier view of §3/§4 is
retained as the implementer's site map and fallback.

### §3.1 Recommended crumb sequence (collapsed, empirical lens) — all .31

| Crumb | What | Size | Re-baseline |
|---|---|---|---|
| **R0** | **Sweep 128 tests/fixtures FIRST** — delete `u128_high_bit/`, convert `lit_i128_if_assign`/`lit_arg_if_over_i64` → u64 (§6), prune isel `_test.tkt` 128/F16 cases. *(Removes every >64 producer so the narrow is observably safe.)* | **S** | none (fixtures only) |
| **R1** | **Mechanical narrow + 2 point-fixes** — rename internal `i128→i64` / `u128→u64` across the 64-safe carriers (`comptime_fold` 127, oracles 169, `LConstInt`, `codegen` digits, timestamps, flags, `TPathExpr`, LIR/print); **A1-lite** `Number.value → {neg,mag:u64}` + sign-aware `value_fits`/parse + reject `>u64::MAX` literal; **A7** bigint abs; move `comptime_fold` top-width guard 128→64. Wire stays compat (§7). | **M** | 1 |
| **R2** | **B1 reject surface + topology delete** — `scope.tks` honest-error arms for i128/u128/f16 (§4 B1); delete `PrimKind::{I128,U128,F16}` + `LType::{I128,F16}` + the match cascade + isel register-pair routes + ABI size-16 legs + oracle width-128 arms (§4 B7/B8). Rejection fixtures born (§6). | **M–L** | 1 |
| **R3** | **Tidies** — ffi prose (B4), `checked.tks` (B3), **B.38 amendment** + DECISION_LOG + docs (B6). | **S** | none (docs) |

**Total: 2 re-baselines, all .31-feasible.** (R1 and R2 may merge into one re-baseline if reviewed
together; kept separate so a surface-rejection regression cannot mask a narrowing regression.)

**What (if anything) does NOT fit in .31 — and why.** Structurally, **nothing** — the collapsed cost is
2 re-baselines. The only constraint is the shared re-baseline WINDOW: R1/R2 must land **after the `ref`
adoption** (offsets shift) and must not **overlap** another in-flight re-baseline (KP16, float-slice,
#524). If that window is oversubscribed, the honest fallback is: **R0+R1 (the enabling narrow) is the
priority into .31**; R2's topology deletion is pure dead-code removal and can slip to early .32 at zero
correctness cost. But by design intent the owner's "maximum death now" is achievable — push R0→R3 into
.31 and leave .32 empty of this issue.

---

## 1. Ground truth (verified against `origin/main`)

**Surface (Wave-B targets):**
- `src/checker/scope.tks:317` (`u128` → `PrimKind::U128`), `:322` (`i128` → `PrimKind::I128`), `:323`
  (`f16` → `PrimKind::F16`), all tagged `native set (B.38)`.
- `src/checker/resolve.tks:1631-1633` — `prim_name` renders `U128/I128` and `F16` (line 1633 the floats).
- `src/checker/type.tks:11-16` — `PrimKind = enum { …U128; …I128; F16; F32; F64; Bool }`; predicates
  `prim_is_int`/`prim_is_signed`/`prim_is_float` (`type.tks:18-34`) enumerate the 128 + F16 members.
- **f128 does not exist** — floats are exactly F16/F32/F64.
- **FFI export already refuses** u128/i128/f16: `src/codegen/ffi_export.tks:73` (prose), `:118-119`
  (the `U128/I128` arms in the surface-name map), `:126-127` (the reject rationale). This is the
  **precedent** the Wave-B checker diagnostic mirrors.

**f16 is nominal only (verified):**
- Registered `scope.tks:323`; C spelling `_Float16` at `codegen.tks:330` and `:1861` (plus `f16`
  surface spellings at `codegen.tks:1122,1160,2089,2107,6109`); own-backend type plumbing only —
  `lir.tks:20` (`F16` LType member), `:540` (`F16 => 2` in `ltype_size`), `lower.tks:184,371`.
- **Zero uses** in the compiler corpus / `.tkt` / `examples`, EXCEPT: the three isel `_test.tkt`
  (``) exercise the F16 *type plumbing* (Wave-B deletes those cases),
  and `examples/regressions/enum_member_shadows_primkind/src/kinds.tks` uses `F16` as a **user enum
  member name** mirroring PrimKind's shape (issue #263) — that is an identifier, NOT the builtin `f16`
  type, and **must survive** the sweep. The f16 sweep keys on the builtin type `f16`, never the token.

**`math/checked.tks:25-31`** — the 128-bit `checked_*` family is *deliberately not shipped*. Wave B
**formalizes** this: the prose about conditional future extension of the 128-bit family is deleted
(there is no longer any 128 to extend to).

**`.tkb` wire (frozen codec).** `parser::Number.value` (an i128) is serialized as **two u64 halves —
hi = `(value >> 64)`, lo = `(value & 0xFFFF…FFFF)`** at `src/emit/tkb_write.tks:100-101` (general
expr), `:178` (`TPathExpr.value`, a u128), `:601` (pattern-expr `write_pexpr`). The reader reconstructs
`(hi<<64)|lo` (`src/emit/tkb_read.tks` `read_u64` at `:36-38`). See §7 for the wire-compat proof.

---

## 2. The carrier replacements (the load-bearing design)

Every carrier below holds a value that, after this issue, ranges over **`[i64::MIN, u64::MAX]`** — the
union of the signed and unsigned 64-bit ranges (65 bits of information, sign + 64-bit magnitude). Two
carrier shapes cover all cases; both use ONLY types in the stable seed today (`bool`, `u64`, `i64`):

### 2.1 Signed-magnitude pair — for *literal* carriers (Number, const-fold value, LConstInt)

```teko
/**
 * A resolved integer-literal value, carrier-detoxed off i128 (drop-128, 2026-07-23). Holds any value
 * in the language's post-drop integer range `[i64::MIN, u64::MAX]` as a sign flag plus a 64-bit
 * magnitude — `neg=false` covers `0..=u64::MAX`, `neg=true` covers `i64::MIN..=-1`. Replaces the old
 * `i128` carrier, whose only reason to exist was to hold a high-bit u64 losslessly.
 *
 * @field neg  true iff the value is strictly negative (only ever set for a signed target)
 * @field mag  the magnitude: the value itself when `neg=false`, else its absolute value. `i64::MIN`
 *             is representable as `{ neg = true; mag = 0x8000000000000000 }` (|MIN| = 2^63 fits u64)
 * @since 0.3.1 (drop-128-family)
 */
pub type NumInt = struct { neg: bool; mag: u64 }
```

`parser::Number` and `checker::tast::TNumber` change `value: i128` → `value: NumInt`. All arithmetic on
the carrier (const-fold `+`/`-`/`~`/unary-`-`, `value_fits`) becomes sign-magnitude arithmetic on
`{neg,mag}`. Helper shapes the implementer adds alongside `NumInt` (all full-Javadoc):

```teko
/**
 * numint_fits — does the signed-magnitude literal `v` fit the target integer prim `k`? Replaces
 * `typer::value_fits(v: i128, k)` (typer.tks:1566). Unsigned targets reject `v.neg` first, then gate on
 * `v.mag <= <unsigned-max(k)>`; signed targets gate on `v.mag <= <signed-max(k)>` (or `== 2^63` when
 * `neg` and `k==I64`, the `i64::MIN` corner). There is no U128/I128 arm (they no longer exist).
 *
 * @param v  the resolved literal carrier
 * @param k  the target integer prim kind (U8..U64 / I8..I64)
 * @return   true iff `v` is representable in `k`
 */
fn numint_fits(v: NumInt, k: PrimKind): bool
```

### 2.2 Raw 64-bit wrapping carrier — for the *legacy engine register files*

The two differential legacy engines (`lir_oracle`, `minst_oracle`) hold **`RegFile.values: []i128`** and
`IResult.value: i128` as an *unmasked* carrier (`lir_oracle.tks:11` "Values are the i128 carrier; a
numeric `to` cast masks to the target"). After the drop, the carrier becomes a **64-bit two's-complement
register value held in `i64`**, with per-op width/sign reinterpretation retained (maintaining
historical semantics). See §9-g for the **behavioral decision** this forces (unmasked-i128 → 64-bit-wrapping),
which is a *ratification item*, not a default.

```teko
/**
 * IResult — the legacy engine step result, carrier-detoxed off i128 (drop-128). `value` is now the raw
 * 64-bit two's-complement register value (was an unmasked i128 carrier). Arithmetic wraps mod 2^64 —
 * the same wrap the native backend produces — and per-op reinterpretation (sign/zero-extend for
 * compares/shifts/div) is applied at the op, exactly as `minst_oracle` must mirror it.
 */
pub type IResult = struct { tag: u32; value: i64; message: str }
```

`RegFile.values: []i128` → `[]i64`; `two_pow_64`/`decode_signed64` and the `two_pow_32` helpers
(`minst_oracle.tks:28+`) collapse (a 64-bit carrier needs no widening to 128 to decode a 64-bit
pattern). **Both legacy engines change in lockstep** — the oracle-vs-oracle equivalence check is
preserved because both apply the identical model; the oracle-vs-native check *improves* (a 64-bit
wrapping register models a real machine register more faithfully than an unmasked i128).

### 2.3 Clean narrowings — no new shape needed

- `comptime_fold::CVInt.bits: u128` → **`u64`** (`comptime_fold.tks:34`). The field is "raw
  two's-complement bits, zero-extended into u128" — the zero-extension to 128 existed *only* to admit
  128-bit widths. With all widths ≤ 64, the two's-complement bits of every value fit `u64`. This is a
  mechanical `u128`→`u64` narrowing across all 127 hits (the value-set is unchanged for ≤64-bit widths).
- `tast::TPathExpr.value: u128` → **`u64`** (`tast.tks:72`). Enum ordinals / flags bit-values are always
  non-negative and ≤ `1<<63` after the flags cap (§2.4), so they fit `u64`.
- `lir::LConstInt.val: i128` → **`i64`** (`lir.tks:45`); `select_const_int_x86`'s `fits_i64` honest-stop
  (`isel_x86_64.tks:178-181`, "A2's i128 route") becomes **unreachable** (Wave A) then **deleted**
  (Wave B). `lir_print::i128_dec` → `i64_dec` (fold into the existing `i64_to_str`).
- `codegen::cb_u128_digits` → **`cb_u64_digits`** (`codegen.tks:113+`). Its inputs (`fn_idx`, `line`,
  `col`, `outcome`) are all u64-range coverage-mark values that were merely widened to u128 to reuse the
  u128 digit printer.
- `numeric::bigint` `mag128: i128` (`bigint.tks:454`, abs of `i64::MIN`) → compute the magnitude in
  `u64` directly: `if neg { (0 - (n + 1) to u64) + 1 } else { n to u64 }` (avoids the `i64::MIN` overflow
  without a wider type). The implementer verifies the `one_limb` call still receives the same `u64`.

### 2.4 Flags — 64-member cap

`parser::FlagsBody.values: []u128` → **`[]u64`** (`ast.tks:290`; member `i` → `1 << i`, `json.tks:34`
documents the parallel-slice shape). A `u64` bit-value caps a flags declaration at **64 members**
(`1 << 63` is the last representable). The 65th member (`1 << 64`) is now a **hard checker error**
(honest, named): today `u128` allowed up to 128 members. See §9-b (ratify the cap + the diagnostic).

### 2.5 Timestamps — i64 nanoseconds, root-to-leaf

The i128 timestamp chain is **deeper than `progress`/`project`** (the fact-list's item 3). It is rooted
in the runtime C and the `teko::time` module:

- `src/runtime/teko_rt.c:2445` `tk_rt_datetime_to_unix_ns` returns `__int128`; `tk_datetime.ticks` is
  `__int128` (`:2349`). **`teko_rt.{c,h}` is MAINTAINED C** (the standing exception), so these
  signatures may be narrowed to `int64_t` under this issue.
- `src/time/time.tks:15-19` — `DateTime.ticks: i128`, `TimeSpan.ticks: i128`, `DateTimeOffset.ticks:
  i128`; extern decls `datetime_now`/`datetime_to_unix_ns` (`:24,45`) mirror the C `__int128`.
- `src/build/progress.tks:85` `Phase.start: i128`, `:98` `now_ns(): i128` (chains
  `datetime_to_unix_ns(datetime_now())`), `:145` `elapsed_str`.
- `src/build/project.tks` — 11 functions thread `start: i128` (`:1568,1611,1626,1813,1841,1872,1879`).

**Decision (§9-c):** narrow the whole chain to **`i64` nanoseconds since the Unix epoch** (i64 ns
saturates ~year 2262 — ample). This touches the maintained-C runtime (allowed), `teko::time`,
`build::progress`, `build::project`, and `crypto::rand` (`rand.tks:8-12` mirrors the clock accessor's
honest-stop; a comment-only touch). **`teko::time`'s `DateTime`/`TimeSpan`/`DateTimeOffset` are public
stdlib types** — narrowing their tick field is an observable stdlib change (a 292-year range instead of
an astronomical one). Ratify with (c).

> Note on `teko_rt.c`'s *arithmetic* helpers `tk_div`/`tk_rem`/`tk_int_to_float` (`__int128` params):
> this doc called them the runtime's INTERNAL wide-arith and put them **out of scope**, on the reasoning
> that the language may stop *exposing* i128 while the runtime keeps a wider internal type.
> **SUPERSEDED by the owner ruling of 2026-07-30** (*"é pra remover suporte de 128 bits como primitivas
> (inteiros e flutuantes), para isto foram criados os arbitrários bigint e dec"*): they are REMOVED,
> together with the whole `*_i128`/`*_u128` helper family, the `tk_to_{u,i}128*` casts and the 128-bit
> cast CARRIER (now `int64_t`/`uint64_t`). Two facts closed it: (1) nothing ever called the trio —
> integer `/` and `%` route through the per-width `tk_div_<tag>`/`tk_mod_<tag>` helpers and an int->float
> cast is a plain C cast; (2) being the runtime's only NON-static `__int128` functions, they were the
> sole reason `teko_rt.o` referenced the libgcc 128-bit builtins `__divti3`/`__udivti3`/`__modti3`/
> `__umodti3`/`__floattidf`/`__floatuntidf`, which MSVC's `link.exe` cannot resolve — six LNK2019s that
> reddened `artifact / windows-x86_64` the moment the Windows leg moved off mingw. "Internal wide-arith
> the surface cannot reach" was still a cost, and it was being paid at the link.

---

## 3. Wave A — CARRIER DETOX (crumbs A1–A7)

> **NOTE (route):** §3–§4 are the **granular, analytic** view — the implementer's per-carrier SITE MAP
> and the fallback if the owner prefers site-by-site rewrites. The **RECOMMENDED** route is the collapsed
> 4-crumb **§3.1** (empirical lens, §0.99): A1–A7 fold into the single mechanical crumb **R1**, B1/B7/B8
> into **R2**. Read §3/§4 for *where* each edit lands; execute per §3.1.

Ordering rule: each crumb ends at **GATE-G** (build the previous released seed, run `teko test .`,
verify `gen1==gen2`). Only ONE self-host re-baseline is in flight at a time. Surface acceptance of
i128/u128/f16 is UNCHANGED throughout Wave A (the checker still resolves them). Wave A is **complete**
only when a `grep -nE '\b(i128|u128)\b' src/**/*.tks` returns nothing outside a doc-comment.

### A1 — literal carrier `Number/TNumber.value: i128 → NumInt` (+ wire, typer, synth, parse)  — **L**
**Targets:** `src/parser/ast.tks:10` (+ the `ast.tks:4,6` doc-comment prose), `src/checker/tast.tks:20`
(+ `:19` prose), `src/parser/parse_lit.tks:8-46` (`lit_int`/hex/bin/dec producers → build `NumInt`, add
the **over-`u64::MAX` overflow guard**, §9-a), `src/checker/typer.tks:1566-1577` (`value_fits` →
`numint_fits`, delete the `U128`/`I128` arms), `src/checker/synth.tks:100,133` (`mk_int`/`mk_neg_int`
take/emit `NumInt`), `src/emit/tkb_write.tks:100-101,601` + `src/emit/tkb_read.tks` (wire, §7).
**New shapes:** `NumInt` (§2.1), `numint_fits` (§2.1). **Risk:** the wire compat proof (§7) is the
gate — the fixpoint's `.tkb` round-trip must stay byte-identical. **Ritual:** GATE-G; additionally
diff a `.tkb` emitted by the seed vs. the detoxed compiler on a literal-heavy module — must be
byte-identical (proves §7).

### A2 — const-fold carrier `comptime_fold::CVInt.bits: u128 → u64`  — **L**
**Targets:** `src/checker/comptime_fold.tks:34` (`CVInt.bits`), `:93` (`cv_int`), and all 127 hits
(mechanical `u128`→`u64` narrowing; the `zero-extended into u128` prose at `:30,88` → `u64`). **Risk:**
the largest single edit; a stray `to u128` cast left behind reintroduces the type. Grep-gate:
`grep -n u128 src/checker/comptime_fold.tks` returns only doc prose after. **Ritual:** GATE-G.

### A3 — LIR constant + print + codegen digits  — **M**
**Targets:** `src/lir/lir.tks:45` (`LConstInt.val: i128 → i64`), `src/lir/lower_const.tks:72-115`
(`serialize_le`/`const_fold_unary` carriers → `i64`/`u64`), `src/lir/lir_print.tks:31-51`
(`i128_dec`/`u128_of_neg_i128` → `i64_dec`, reuse `i64_to_str`), `src/codegen/codegen.tks:113-137+`
(`cb_u128_digits` → `cb_u64_digits`; the 75 hits, mostly coverage-mark digit emission). Note: leave the
isel `fits_i64` honest-stop text in place (it is now unreachable but still valid Teko) — Wave B deletes
it. **Ritual:** GATE-G.

### A4 — legacy engine carriers `lir_oracle` + `minst_oracle` `i128 → i64` (LOCKSTEP)  — **L**
**Targets:** `src/lir/lir_oracle.tks` (`IResult.value`, `RegFile.values`, `ires_*`, `eval_bin`/`eval_un`/
`eval_div` arms — 84 hits) and `src/backend/minst_oracle.tks` (mirror — 85 hits;
`two_pow_32/64`/`decode_signed{32,64}` collapse). **Carries the §9-g behavioral decision** — resolve it
BEFORE this crumb. **Risk:** the two must stay bit-equivalent; any asymmetry breaks the oracle-equiv
oracle in `{lir_oracle,minst_oracle}_test.tkt`. Do both files in ONE crumb. **Ritual:** GATE-G +
`isel_*_test.tkt` (the differential harness) green.

### A5 — flags + enum-value carriers  — **M**
**Targets:** `src/parser/ast.tks:290` (`FlagsBody.values: []u128 → []u64`), `src/checker/tast.tks:72`
(`TPathExpr.value: u128 → u64`) + `typer.tks:2861` (the `j to u128` producer → `to u64`), the flags
member-assignment site (`C8.3`, `1<<i`) gains the **65th-member honest error** (§9-b),
`src/encoding/json/json.tks:34` (doc-comment update only). **Ritual:** GATE-G + a flags-with-64-members
fixture (max) and a flags-with-65-members rejection fixture (§6).

### A6 — timestamps `i128 → i64` (runtime C + `teko::time` + build)  — **M**
**Targets (maintained-C exception):** `src/runtime/teko_rt.{c,h}` datetime signatures
(`tk_datetime.ticks`, `tk_rt_datetime_to_unix_ns`, `tk_rt_datetime_add/sub/diff`, `tk_timespan`) →
`int64_t`. **Teko:** `src/time/time.tks:15-19,24-45` (tick fields + extern decls → `i64`),
`src/build/progress.tks:85,98,145` (`Phase.start`, `now_ns`, `elapsed_str` → `i64`),
`src/build/project.tks` 11 `start: i128 → i64` sites, `crypto/rand.tks:8-12` (comment). **Risk:** the C
signature and the Teko extern decl must agree byte-for-byte at the ABI (they marshal a struct-by-value);
a mismatch is a silent miscompile. **Ritual:** GATE-G + a `now_ns()` smoke (a build prints a plausible
elapsed time).

### A7 — bigint abs  — **S**
**Targets:** `src/numeric/bigint/bigint.tks:446,454-455` (`mag128: i128` → u64 magnitude, §2.3).
**Risk:** the `i64::MIN` corner (|MIN| overflows i64) — the u64 form in §2.3 handles it; a unit fixture
over `i64::MIN → bigint` guards it. **Ritual:** GATE-G.

**End of Wave A — bankable result:** no compiler-internal declaration is `i128`/`u128`; the corpus the
native backend self-compiles contains no i128 value; the isel 128 honest-stops are unreachable;
the surface still resolves i128/u128/f16 (Wave B removes that). f16 is untouched by Wave A (zero use).

---

## 4. Wave B — SPLIT: .31 = SURFACE REMOVAL, .32 = TOPOLOGY DEAD-CODE CLEANUP

This is the owner's "pay in .31, clean in .32" split, on the Plan-A carrier (§0.9). The enabling
observation: **surface removal only needs to remove the resolve arms** (`scope.tks::builtin_type`), so
the names `i128`/`u128`/`f16` stop resolving and are honestly rejected. The `PrimKind::{U128,I128,F16}`
and `LType::{I128,F16}` enum members can **stay** (dead — nothing produces them, no surface name maps to
them). Every `match` over them stays exhaustive and compiles. **Deleting the enum members forces the
whole match-arm cascade** (codegen/lower/resolve/predicates) + the backend register-pair/ABI topology —
that is the mechanical dead-code sweep deferred to **.32**. So the giant cascade does NOT block the .31
surface removal.

### .31 crumbs (surface)

#### B1(.31) — checker rejects i128/u128/f16 at the resolve boundary (honest diagnostic)  — **M**
Precondition: **all of Wave A landed** (removing `scope.tks`'s `i128` arm also stops the compiler's OWN
`value: i128` annotations from resolving — so no internal i128 may remain). **Targets:**
`src/checker/scope.tks:317,322,323` (`builtin_type`) — replace the `u128`/`i128`/`f16` arms with explicit
**honest-error arms** (do NOT let them fall through to the generic "unknown type"):
- `i128`/`u128` → `"type 'i128'/'u128' was removed (0.3.1): the language's widest integer is 64-bit
  (i64/u64). For larger integers use teko::numeric::bigint::BigInt (write '123bi') or Decimal."`
- `f16` → `"type 'f16' was removed (0.3.1): the language's floats are f32/f64. Use f32."`
**Leave `PrimKind::{U128,I128,F16}` and every match arm in place** (dead). **Rejection tests born (§6).**
**Ritual:** GATE-G + the new rejection fixtures. f16's arm has **no Wave-A precondition** (zero internal
use) — it can even ship as its own tiny crumb ahead of the i128/u128 arm if the detox slips (§10).

#### B3(.31) — `math/checked.tks` — formalize the removal  — **S**
Delete the `checked.tks:24-31` prose about conditional future extension of 128-bit support;
restate the width scope as **"exactly u8..u64 / i8..i64 — the language's full integer set"**. No code
change (the family was never shipped). **Ritual:** GATE-G.

#### B4(.31) — FFI export prose tidy  — **S**
`ffi_export.tks:73,126-127` — the reject rationale prose is tidied to drop the "128-bit ints and f16 are
rejected" clause (after B1 they are not types; the `U128/I128` surface-name arms at `:118-119` are dead
but stay until the .32 cascade). **Ritual:** GATE-G.

#### B5(.31) — corpus / fixtures sweep  — **M**
The dying + surviving fixtures (§6 details the exit codes):
- **Delete:** `examples/regressions/u128_high_bit/` (whole project is `u128::MAX >> 120`; no i64/u64
  analogue — it *is* the u128 semantics test).
- **Convert (not delete):** `examples/regressions/lit_i128_if_assign/` and `lit_arg_if_over_i64/` test a
  literal *just over `i64::MAX`* (`2^63 = 9223372036854775808`), which **fits `u64`**. Under the new
  rules the over-i64 literal widens to **u64** instead of i128; retype the fixture (rename to
  `lit_over_i64_u64`) so the equality holds under u64 inference — **EXPECT_EXIT stays 7**. (If the owner
  prefers, delete instead; convert is recommended — it preserves the "literal over i64" regression.)
- **Grep-sweep** `examples/**` and `**/*.tkt` for the builtin `i128`/`u128` (NOT the
  `enum_member_shadows_primkind` user `F16` identifier, which stays). The isel `_test.tkt` **F16 cases
  stay green in .31** (LType::F16 still exists) and are pruned in .32 with the LType deletion.

#### B6(.31) — spec amendment + docs  — **S**
Amend **B.38 "native set"** (the ratified type surface) — in `tooling/shared/src/spec_json.tks` and any
Constitution/spec mirror — to **u8..u64 / i8..i64 + f32/f64** (drop U128/I128/F16; note f128 never
existed). DECISION_LOG entry (D-number, base = this ruling). MASTER_PLAN mark. Update the `ast.tks`/
`tast.tks`/`lir_oracle` doc-comment prose that still says "any width incl u128/i128" / "canonical Teko
uses i128". **Ritual:** the closing .31 gate — `gen1==gen2`.

### .32 crumbs (topology dead-code cleanup — safe to defer; the code is unreachable after .31)

#### B7(.32) — delete `PrimKind::{U128,I128,F16}` + `LType::{I128,F16}` + the match cascade  — **L**
Now that no surface produces them and no internal carrier is 128-bit, delete the enum members and every
now-dead arm in the SAME crumb (or the matches go non-exhaustive): `type.tks:11-34` (members +
`prim_is_int`/`prim_is_signed`/`prim_is_float`), `resolve.tks:1631-1633` (`prim_name`),
`codegen.tks:330,1122,1160,2089,2107,6109` (C spellings + float-literal arm), `lower.tks:184,236,251-258,
371`, `ffi_export.tks:118-119` (dead surface-name arms), `expr.tks:10`/`revalidate.tks` predicates,
`lir.tks:20,527,540` (`LType` members + `ltype_size` `16`/`F16 => 2`). **Ritual:** GATE-G.

#### B8(.32) — delete the backend register-pair / ABI-128 / oracle-width-128 topology  — **L**
The isel i128 routes + honest-stops — `isel_x86_64.tks` (~11 incl. `select_const_int_x86`'s register-pair
stop, now unreachable), `isel_arm64.tks` (~35), um isel de backend (~29); the 128-bit ABI classification —
literal `128`; key on the size-16/two-register path, not a grep); `lower.tks` residual
`C1-i128` stop; the `if width == 128` arms remaining in `lir_oracle`/`minst_oracle` (the carrier already
went in A4); `lir_print` residual. Prune the isel `_test.tkt` i128/F16 cases. **Ritual:** GATE-G +
own==C differential (`diff_c_own.sh`) with the i128 **KNOWN-STOP guards removed** (§6) — the differential
runs with no 128 skip. **This is the closing ritual of the whole issue.**

---

## 5. Ritual points (where the full GATE-G must pass)

**Recommended route (§3.1) — the gate IS the empirical proof (§0.99):**

| After | Ritual | Proves |
|---|---|---|
| **R0** | GATE-G (fixtures only; no re-baseline) | the 128 producers are gone; suite still green |
| **R1** | GATE-G + `.tkb` byte-identity (literal-heavy module) + isel differential (`_test.tkt`) + `now_ns` smoke | the mechanical narrow preserves the fixpoint (the empirical proof); wire compat (§7); oracles agree under 64-wrap (§9-g); the `i64::MIN`/`u64::MAX`/`width_mask(64)` point-fixes hold |
| **R2** | GATE-G + rejection fixtures + own==C differential (i128 KNOWN-STOP guards removed, §6) | i128/u128/f16 honestly refused; topology deleted; differential green with no 128 skip |
| **R3** | **FULL closing gate** + final `gen1==gen2` | spec/B.38/docs match reality; whole issue green in .31 |

The single load-bearing ritual is **R1's byte-identical fixpoint**: because the compiler self-hosts, a
wrong narrow (e.g. a mis-moved `width_mask` guard) miscompiles the compiler and breaks `gen1==gen2` — so
the gate catches exactly the divergences the analytic proof would have. (The §3/§4 per-crumb A/B rituals
remain valid if the owner prefers the granular route.)

Per the CI-perf model: one native test-gate on the primary platform per sub-PR, the rest compile-only;
wait for aggregate AllGreen + a fresh seed before the next crumb (each Wave-A/B `src/`-touching crumb
carries exactly ONE re-baseline).

---

## 6. Regression fixtures — who dies, who is born, differential impact

**Existing fixtures affected:**

| Fixture | Today | After | Action |
|---|---|---|---|
| `examples/regressions/u128_high_bit/` | exit 255 (`u128::MAX >> 120`) | u128 removed | **DELETE** (no i64/u64 analogue) |
| `examples/regressions/lit_i128_if_assign/` | exit 7 (over-i64 literal → i128) | over-i64 → **u64** | **CONVERT** → `lit_over_i64_u64`, EXPECT_EXIT stays 7 |
| `examples/regressions/lit_arg_if_over_i64/` | exit 7 (`2^63` round-trip) | `2^63` fits u64 | **CONVERT** (retype to u64), EXPECT_EXIT stays 7 |
| `` | exercise i128 + F16 plumbing | removed | **PRUNE** the 128/F16 cases |
| `enum_member_shadows_primkind/src/kinds.tks` | user enum member `F16` | unaffected | **KEEP** (identifier, not the builtin) |

**Rejection fixtures born (native exit codes — updated for native-only, per the legacy engine retirement):**

| New fixture | Input | Expected |
|---|---|---|
| `examples/regressions/reject_i128/` | `let x: i128 = 1` | checker error, exit 2 (compile-fail); stderr names `bigint`/`123bi` |
| `examples/regressions/reject_u128/` | `let x: u128 = 1` | checker error, exit 2; stderr names `bigint` |
| `examples/regressions/reject_f16/` | `let x: f16 = 1.0` | checker error, exit 2; stderr names `f32` |
| `examples/regressions/reject_lit_over_u64/` | a literal `> u64::MAX` (e.g. `18446744073709551616`) | checker error, exit 2; stderr: "use '…bi'" (§9-a) |
| `examples/regressions/flags_64_members/` | a flags with exactly 64 members | builds + runs, asserts `1<<63` member value |
| `examples/regressions/reject_flags_65/` | a flags with 65 members | checker error, exit 2; stderr names the 64-member cap (§9-b) |
| unit (in `bigint`'s `.tkt`) | `i64::MIN → BigInt` | magnitude correct (A7 corner) |

**Differential (`diff_c_own.sh`) impact:** the i128 entries in the **KNOWN-STOP list** (`diff_c_own.sh`
~§6/`:210+`) — the guards that assert a *reproducible i128 stop* — are **deleted** in B2. After the drop
there is no i128 to stop on, so those known-stop assertions would themselves fail (they expect a stop
that no longer happens). Removing them makes the differential run the affected fixtures for real. This is
a positive: fewer skips, more real cross-checking.

---

## 7. `.tkb` wire — COMPAT, no version bump (proof)

The frozen codec serializes `Number.value` (i128) as **hi:lo u64 halves** (`tkb_write.tks:100-101,178,
601`), reader reconstructs `(hi<<64)|lo`. With the new `NumInt {neg, mag}` carrier, the writer derives
the halves from the sign — **the high half is a pure function of the sign**:

- `lo = if neg { (0 - mag) }` (the 64-bit two's-complement of the negative value) `else { mag }`.
- `hi = if neg { 0xFFFFFFFFFFFFFFFF }` (sign-extension) `else { 0 }`.

**Claim:** for every value in `[i64::MIN, u64::MAX]`, this produces the byte-identical hi:lo the old i128
path produced. Proof by the two cases:
- **Non-negative** `v ∈ [0, u64::MAX]`: old i128 of `v` has hi=0, lo=`v`. New: `neg=false` → hi=0,
  lo=`mag`=`v`. Identical.
- **Negative** `v ∈ [i64::MIN, -1]`: old i128 (a two's-complement 128-bit) has hi=`0xFFFF…FFFF`
  (sign-extension), lo = the 64-bit two's-complement of `v`. New: `neg=true` → hi=`0xFFFF…FFFF`,
  lo=`0 - mag` = the 64-bit two's-complement of `-mag` = of `v`. Identical. (`i64::MIN`: mag=`2^63`,
  `0 - 2^63` = `0x8000000000000000` = the correct low half.)

The **only** i128 values that break compat are those with `hi ∉ {0, 0xFFFF…FFFF}` — i.e. magnitudes
**beyond the i64/u64 range**. Those are exactly what the §9-a literal-limit guard now **rejects at the
checker**, so they never reach emit. Therefore: **wire-compat holds for the entire post-drop value range;
no codec version bump.** The A1 ritual (`.tkb` byte-identity diff) is the empirical confirmation.

`TPathExpr.value` (u128 → u64, ordinals/flags): always non-negative, always hi=0 → trivially compat.

> Ratify (§9-d): **compat, no bump.** The alternative (bump the `.tkb` version and drop the high half)
> is a larger, breaking change with no benefit — recommended against.

---

## 8. f16 — pure Wave-B deletion (no detox)

f16 has **zero corpus use**, so it needs **no Wave-A carrier work**. It is deleted wholesale in Wave B
alongside the 128 topology:
- `scope.tks:323` (surface resolve arm) → honest error → `f32` (B1).
- `type.tks` `PrimKind::F16` + `prim_is_float` arm; `resolve.tks:1633` `prim_name`; `codegen.tks:330`
  (`_Float16`), `:1122,1160,1861,2089,2107,6109` (spellings + float-literal arm); `lir.tks:20,540`
  (`LType::F16`, `ltype_size`); `lower.tks:184,371` (mapping + kind) — all in B1/B2's PrimKind cascade.
- `ffi_export.tks:73` already refuses f16 — the prose is tidied in B4.
- isel `_test.tkt` F16 cases pruned (B5).

**Future cost avoided (owner's rationale):** half-precision arithmetic is not native on most CPUs
(convert-compute-convert via F16C / AVX512-FP16), has bespoke AAPCS64 rules and `__extendhfsf2`/
`__truncsfhf2` libcalls, and `_Float16` is non-portable in C (MSVC lacks it). Removing the nominal type
now costs nothing (no user depends on it) and deletes that future liability. **The isel float family
still covers f32/f64 unchanged** — f16's removal does not touch f32/f64 semantics.

---

## 9. Decisions to ratify (owner) + risks / law tensions

**No genuine unresolved law tension → NO HALT.** The ruling is law-first coherent (Teko-only obeyed —
all edits are `.tks` except the maintained-C `teko_rt`; "issues are 100%" — the whole family goes in one
wave). The items below are **choices the owner must ratify** before code, plus the honest scope finding.

**(0) ROUTE — the top-level decision (supersedes the carrier debate).** RECOMMEND the **reframe route**
(§0.99 / §3.1): mechanical narrowing (128 is surface-support; proven empirically by the fixpoint+suite+
own==C gate) + honest surface **rejection** in .31 + full topology deletion in .31. **Reject Plan B**
(BigInt swap — arena blow-up, §0.9). **Do not take Plan C's "compile em falso"** (M.3 debt, §0.95-3) —
the reframe makes rejection affordable now, so honesty is preserved. Ratify the route and that the
**empirical gate is the accepted preservation proof** (no site-by-site analytic proof required, given R0
deletes every >64 producer first).

**(a) Literal carrier + over-u64 limit.** RECOMMEND `NumInt {neg, mag: u64}` (§2.1). A literal whose
magnitude exceeds `u64::MAX` is a **hard checker error** naming the escape hatch: *"integer literal
exceeds u64::MAX — the language's widest integer is 64-bit; write it as a BigInt: `123…bi`"*. (Confirm
the `bi` suffix is the ratified BigInt-literal spelling from the .30 numeric land; the message must
match it.) *Alternative rejected:* keep an i128-range literal accepted-but-truncated — silent lossy,
against M.1.

**(b) Flags 64-member cap.** RECOMMEND `[]u64`, hard error on the 65th member: *"a flags type may declare
at most 64 members (bit 63 is the last); this declares N"*. *Risk:* narrows the max from 128 to 64; a
>64-member flags is exotic and can decompose into two flags. Ratify the cap.

**(c) Timestamps → i64 ns.** RECOMMEND narrowing the WHOLE chain (runtime C `teko_rt` datetime externs,
`teko::time` public tick fields, `build::progress`/`project`) to `i64` nanoseconds (§2.5). *Risk /
observable:* `teko::time`'s public `DateTime`/`TimeSpan`/`DateTimeOffset` lose astronomical range for a
~year-2262 ceiling; the maintained-C `teko_rt` ABI changes. Ratify that `teko::time`'s public surface may
narrow under this issue (it is the only path off i128 for the compiler's own build clock).

**(d) `.tkb` wire.** RECOMMEND **compat, no bump** — proven in §7 (high half derived from sign; only
out-of-range magnitudes, which (a) rejects, would differ). Ratify no-bump.

**(e) f16.** RECOMMEND pure deletion (§8), diagnostic → `f32`. No f32/f64 semantic change. Ratify.

**(f) const-fold carrier `CVInt.bits: u128 → u64`.** RECOMMEND the clean narrowing (§2.3) — value-set
unchanged for ≤64-bit widths. Under the reframe lens (§0.99) the 127 hits are a **mechanical rename**, not
a reasoned rewrite; the ONE thought-spot is moving `width_mask`/`msb_value`'s top-width special-case
128→64 (`comptime_fold.tks:148-215`). Confirmed: no fold mask genuinely needs >64 (ops mirror the dying
surface widths). Folded into crumb R1.

**(g) Legacy-engine carrier BEHAVIOR — the one substantive semantic choice.** The differential legacy engines
hold an **unmasked i128** carrier today (`lir_oracle.tks:11`); moving to a **64-bit wrapping `i64`**
carrier (§2.2) changes intermediate-overflow behavior: an i64/u64 arithmetic result that momentarily
exceeded 64 bits used to be held wide and only masked at a `to` cast — it now **wraps mod 2^64 at the
op**. RECOMMEND the wrapping carrier: it is **more faithful to the native backend** (real 64-bit
registers wrap), and both legacy engines change in lockstep so the oracle-equiv oracle is preserved. *Risk:*
a corpus test whose exit code depended on the unmasked-then-masked intermediate could shift; the A4
ritual (isel differential) surfaces any such case. **The legacy engines follow the "reinterpret at the
boundary" model the `checked.tks:22` note describes.**
Ratify the wrapping model (or, alternatively, keep an unmasked model on a `{neg,mag}` pair — heavier, and
it re-introduces a non-machine value model; not recommended).

**(h) SCOPE — reconciled under the reframe.** The ~11 carrier subsystems (§0 table) are real, but the
owner's reframe (§0.99) reclassifies them correctly: they are **surface-SUPPORT**, not intrinsic use, so
narrowing them is **mechanical** (one rename crumb R1), not 7 reasoned crumbs. The legacy engines (169 hits)
and `comptime_fold` (127) are the bulk of the rename but carry only ONE thought-spot each (oracle wrap =
§9-g; fold guard = §9-f). The two genuinely-intrinsic wide uses are `Number.value` (§9-a) and `bigint`
`abs(i64::MIN)` (A7). **Net: the empirical lens collapses the cost from L-heavy to M — this is the finding
that makes "everything in .31" affordable.**

**Risks already covered by rituals (recommended route):** the mechanical narrow's correctness = R1's
byte-identical fixpoint (self-host is the oracle); wire round-trip = R1 `.tkb` diff; oracle wrap = R1 isel
differential; timestamp C-ABI marshal = R1 `now_ns` smoke; surface rejection + topology exhaustiveness =
R2; own==C without the 128 KNOWN-STOP = R2.

---

## 10. Train fit (.31, after the ref) + what remains blocked

- **Depends on / sequences after:** the null-union pivot (C6-C7), the corpus-wide `ref` adoption, and
  KP16 objfile — all in flight. This wave **engages after the ref lands** (it rewrites the corpus; every
  line:offset here shifts — re-grep the symbol). Cite offsets as *approximate*.
- **Interaction with the legacy engine detox:** the LIR legacy engine (`lir_oracle`) and machine-code
  legacy engine (`minst_oracle`, in `src/lir`/`src/backend`) are orthogonal to the native backend (A4) and
  needed regardless. If the legacy engine detox (#524) lands first, one fewer coverage interaction to reason about;
  no ordering hard-dependency either way. `checked.tks:26` references the `norm_int` constant — B3's prose
  cleanup should also review this reference to ensure it aligns with the current integer model.
- **Interaction with KP16 (objfile) + the float-slice work:** R2's ABI edits (size-16/eightbyte-pair
  classification) touch the same `abi_*.tks` files the float-slice work touches — sequence R2 to NOT
  overlap a float-slice re-baseline (one re-baseline in flight). The float isel family (f32/f64) is
  **untouched**; f16's removal only *shrinks* the float set the isel must handle, easing float-slice.
- **Re-baseline budget (recommended route, §3.1):** **2 re-baselines total** — R1 (mechanical narrow +
  fixes) and R2 (surface rejection + topology). R0 (fixtures) and R3 (docs) carry none. This is the
  collapse the reframe buys: down from the 7 (Wave A) + 4 (Wave B) of the analytic route. Land as
  `fix/drop-128-*` sub-PRs; do NOT overlap another issue's re-baseline (ref, KP16, float-slice, #524).
  **Everything fits in .31**; the only slip-risk is re-baseline-window contention (§0.99 Consequence 4) —
  if oversubscribed, R0+R1 is the priority and R2's dead-code topology delete can slip to early .32 at
  zero correctness cost.

**What remains blocked / open (nothing hard-blocks the DESIGN; these are confirmations before code):**
1. The **`bi` BigInt-literal suffix spelling** — the (a) diagnostic must name the exact ratified spelling
   from the .30 numeric land. Confirm before B1's message text.
2. The **B.38 canonical home** — the spec amendment (B6) must edit the authoritative source; confirm it
   is `tooling/shared/src/spec_json.tks` (grep found the B.38 tag there) vs. a Constitution doc.
3. Post-`ref` offsets — all cited lines re-grep on the actual base.

Everything else (carrier shapes, crumb order, wire proof, fixtures, rejection messages) is designable and
committed here today; the implementer resumes in minutes once (1)–(2) are confirmed and the ref is in.
