# Phase 17 — Floating-Point & Numeric Types Expansion

Branch `feat/phase-17-floating-point` (PR #10, draft). Closes the **float gap Phase 16
deliberately gated**: `convert.float_to_str` / `convert.parse_float` (reserved runtime ids **50**
and **54**) plus auto-`to_string` for floats need a real **f64 value model** in the frontend
first. Today `frontend_interop.c`'s expression evaluator is integer-only (`$w0` is i32 on WASM /
a GPR on native; literals via `atoi`); there is no way to carry an `f64`, so a float-format token
would be a **dead token** — which the discipline forbids. Phase 17 carries floating-point values
end to end, then surfaces the formatting/parsing.

Follows the proven Phase 13/14/15/16 pattern: a **single C runtime is the source of truth**
(`src/runtime/teko_convert.c` + a new freestanding shortest-round-trip formatter), KAT-tested in
the Unity suite, lowered to native `teko_rt_*` wrappers AND compiled into the WASM reactor — every
reserved surface gets an executable `.tks` proof on **both** native and WASM (no dead tokens).

## Scope (owner-defined, from `docs/plan.md` Phase 17)
1. **f64 value model.** Float literals (`3.14`, `1.0`, `1e9`, `2.5e-3`), float locals, float
   arithmetic (`+ - * / %`) and comparisons, and checked int↔float casts — carried through the
   frontend value model alongside the existing integer path.
2. **Float formatting + parsing (closes the gate).** `convert.float_to_str` (id 50, shortest
   round-trip culture-invariant `.`-decimal) and checked `convert.parse_float` (id 54, fail-loud),
   wired into auto-`to_string` on `+` / interpolation, on native AND WASM.

## The f64 value model — design on both backends

The integer path stays **byte-identical** — float support is purely additive (new opcodes, a
parallel accumulator, a new value-type). The 16 native goldens are integer/concurrency programs;
they emit no float opcode, so their byte streams do not change.

### Frontend (`src/frontend_interop.c`)
- New value-type `TEKO_VT_FLOAT` (3) alongside `VT_INT`/`VT_STR`/`VT_OBJ_BASE`.
- `eval_primary` recognizes a **float literal** (a numeric token containing `.`, `e`/`E`, or a
  trailing `f`) and lowers `OP_FCONST`; an integer-only numeric token stays `OP_ICONST` (`atoi`).
  The literal's f64 bit pattern is encoded in the IL (see opcodes below).
- `eval_expr_prec` propagates the value-type: a binary op with **either** operand `VT_FLOAT`
  promotes the other (int→float via `OP_I2F`) and emits the **float** arithmetic/compare opcode;
  all-int stays integer. A `VT_STR` operand still wins (string concat), now auto-converting a
  float operand via `to_string` id 50 (16.B/16.D path, extended).
- Float-typed named locals tracked parallel to `g_localstr` (a `g_localflt` registry); load/store
  of a float local routes through the float accumulator.
- Checked int↔float casts are explicit conversion builtins (fail-loud, consistent with 16.F).

### Native accumulator (`src/codegen/emit_native_hosted.c`)
- A **parallel float accumulator**: `$fw0`/`$fw1` mapped to `xmm0`/`xmm1` (x86_64 SysV) and
  `d0`/`d1` (arm64 AAPCS). Float locals spill to the existing frame (8-byte slots, reused via
  `movsd`/`str d`).
- New opcodes lower to scalar-double instructions: `OP_FCONST` → load f64 immediate (via `.rodata`
  constant or `movabs`+`movq` to xmm); `OP_FADD/FSUB/FMUL/FDIV` → `addsd/subsd/mulsd/divsd`
  (`fadd/fsub/fmul/fdiv` on arm64); `OP_FMOD` → call to a runtime `fmod` helper (freestanding);
  compares → `ucomisd`+set / `fcmp`+cset; `OP_I2F`/`OP_F2I` → `cvtsi2sd`/`cvttsd2si` (`scvtf`/
  `fcvtzs`). A float result feeding a runtime call (id 50) is moved into the ABI's first FP arg
  register.
- `teko_native_runtime_symbol` gains ids 50 (`teko_rt_float_to_string`) / 54
  (`teko_rt_parse_float`).

### WASM accumulator (`src/codegen/bare_metal/emit_wasm.c`)
- Declare `(local $f0 f64) (local $f1 f64)` next to `$w0/$w1/$cp` (the module already declares one
  i64 local `$tdl`, so a non-i32 local is precedent — every op stays stack-neutral).
- `OP_FCONST` → `f64.const` → `local.set $f0`; arithmetic → `f64.add/sub/mul/div`, `OP_FMOD` →
  reactor helper; compares → `f64.eq/ne/lt/le/gt/ge` (result i32 → `$w0`); `OP_I2F` →
  `f64.convert_i32_s`, `OP_F2I` → `i32.trunc_f64_s` (checked variant traps on overflow — matches
  fail-loud). Float args to a reactor call cross as f64 (the reactor entry takes `double`); the
  string result is a pointer in shared memory exactly like the rest of Phase 16.
- ids 50/54 added to the reactor-import set (`wasm_is_crypto_ext_id`) and the reactor export list.

### IL opcodes (new, additive — free slots `0x71`+ per `codegen_li.h`)
`OP_FCONST` (carries the 8-byte f64 pattern as an IL arg), `OP_FADD/FSUB/FMUL/FDIV/FMOD`,
`OP_FEQ/FNE/FLT/FLE/FGT/FGE`, `OP_I2F`, `OP_F2I`. Final byte assignments fixed in the value-model
commit; the `codegen_metal.c` arg reader is extended for the f64-immediate width.

## Runtime ids (OP_CALL_RUNTIME) — the two reserved float ids
| id | symbol                    | surface                | signature                              |
|----|---------------------------|------------------------|----------------------------------------|
| 50 | `teko_rt_float_to_string` | `convert.float_to_str` | (f64) → shortest round-trip `.`-decimal |
| 54 | `teko_rt_parse_float`     | `convert.parse_float`  | (str) → f64, checked, fail-loud         |

The C core lands in `teko_convert.c` (or a dedicated `teko_convert_f64.c` if the formatter is
large): `teko_convert_f64_to_string(double)` and `teko_convert_parse_f64(const char*, double*)`,
both freestanding-safe (no `snprintf`/`strtod`/`setlocale`).

## Sub-blocks & dependency order (value model BEFORE formatting — no dead tokens)
- **17.A — f64 value model: literals, locals, arithmetic, compares.** The prerequisite. New
  value-type + opcodes + parallel accumulator on both backends; integer path and 16 goldens
  byte-identical. Proof: a `.tks` computing float arithmetic and **storing to an int** (via checked
  `F2I`) so the result is observable through the existing integer print path (no float formatting
  yet). Bounded-but-real (touches frontend evaluator + both emitters + IL arg reader).
- **17.B — checked int↔float casts.** `OP_I2F`/`OP_F2I` surfaced as explicit fail-loud conversion
  builtins; overflow traps/aborts. Small, builds on 17.A. Proof via the cast `.tks`.
- **17.C — `teko_convert_f64_to_string` C core + KATs (no surface yet).** Shortest-round-trip
  formatter as the source of truth, KAT-tested. **Formatter = Ryu (owner-APPROVED).** A carefully
  transcribed public-domain Ryu reference (with attribution), **portable `umul128` via 64-bit halves
  (NO `__int128`/`__multi3`)**, a static power-of-5 table, freestanding-safe (no `math.h`/`snprintf`/
  `strtod` — only the reactor shim's `mem*`/`malloc`). Render culture-invariant `.`-decimal, falling
  to `e`-notation only at extreme exponents (documented threshold). Heavy KAT vectors: `0.0`, `-0.0`,
  `1.0`, integers-as-float, `0.1`, halfway/boundary values, subnormal `5e-324`, max
  `1.7976931348623157e308`, powers of 10. ONE C source compiled identically to native (id 50, 17.D)
  and the WASM reactor → byte-identical. **Large** — the formatter is the hard part. Lands as runtime
  + KATs only (no surface; like the parse core in 16.A).
- **17.D — `convert.float_to_str` surface + auto-`to_string` for floats.** Wire id 50 on both
  targets; extend the concat/interpolation coercion (16.B/16.D) so a `VT_FLOAT` operand
  auto-converts. Proof `.tks` → byte-identical float output native + WASM.
- **17.E — `teko_convert_parse_f64` + `convert.parse_float` (checked, fail-loud).** id 54; KATs
  (valid/invalid/overflow) + `parse.tks`/`parse_fail.tks`-style proofs (happy + trap/abort), as
  16.F did for ints.

Order: **17.A → 17.B → 17.C → 17.D → 17.E**. The formatting/parsing surface (17.D/17.E) is only
introduced once a float value exists to feed it.

## Bounded vs. large
- **Bounded:** the value-model plumbing (17.A/17.B) — additive opcodes + a parallel accumulator,
  mirroring the existing integer lowering; the surface wiring (17.D/17.E) reuses the established
  reactor-import + coercion machinery.
- **Large:** **17.C, the freestanding shortest-round-trip formatter** (Ryu/Grisu-class) — correct,
  table-driven, `snprintf`-free, and identical on native + the wasm32 reactor. This is the real
  engineering risk and is sequenced as its own sub-block with KATs before any surface depends on it.

## Where it starts
17.A — the f64 value model. First commit: the new `TEKO_VT_FLOAT` + `OP_FCONST`/float-arith
opcodes + the parallel accumulator on both backends, with a float-arithmetic `.tks` proof that
funnels its result back through the integer observation path (checked `F2I`), so the model is
exercised end-to-end **before** any float-formatting runtime exists.

## Discipline (unchanged, non-negotiable)
One increment per commit; build + Unity suite; **ASan + UBSan on BOTH dispatch paths + TSan** clean
each commit; the **16 native emitter goldens never regress** (re-verify byte-identical after the
accumulator change); all four CI gates green (incl. Windows MSVC — guard POSIX/LLP64) before any
sub-block is "done"; patient CI watch (≥90s); **no dead tokens** (executable `.tks` proof per
surface, native + WASM); **no merge / force-push** — the human merges.

## Status
**Phase opened.** Plan/scope above; branch + draft PR up; `docs/plan.md` + `CLAUDE.md` renumbered
(Phase 17 inserted; old 17–22 → 18–23).

Sub-block **17.A (f64 value model) is DONE** on both targets (locally green). Float literals
(`3.14`/`2.0`/`0.5` — the lexer emits `TOKEN_LIT_FLOAT` only for a `<digits>.<digits>` fraction;
`e`/`E` exponents and the `f` suffix are not lexed as floats, so they are out of scope here),
float locals, float arithmetic (`+ - * /`), float comparisons (`== != < <= > >=`), and mixed
int→float promotion (`OP_I2F`) are carried end to end through a PARALLEL float accumulator,
purely additive to the integer path:
- **IL opcodes** `OP_FCONST` (0x71, 4-byte float-pool index — NOT a 64-bit immediate) + `OP_FADD/
  FSUB/FMUL/FDIV` + `OP_FEQ..FGE` + `OP_FSTORE/FLOAD/FSTORE_LOCAL/FLOAD_LOCAL` + `OP_I2F`
  (0x72–0x81; `0x76`=FMOD / `0x82`=F2I reserved for 17.B, NOT emitted). A **float-constant pool**
  (`BytecodeBuffer.floats`, dedup by bit-equality) mirrors the string pool; `uses_float` gates the
  WASM float locals.
- **Native (`emit_native_hosted.c`):** `$f0/$f1` = `xmm0/xmm1` (x86_64) / `d0/d1` (arm64); float
  locals reuse the integer frame slots (`movsd` / `str d`/`ldr d`); FCONST scratch is `r11`/`x9`
  (never `$w0`/`$w1`); compares via `ucomisd`+`set{a,ae,e,ne}` (NaN-correct with `setnp`/`setp`) /
  `fcmp`+`cset`.
- **WASM (`emit_wasm.c`):** `(local $f0 f64)/(local $f1 f64)` + a `$fvN` float local file declared
  at every function open, gated by `wasm_emit_float` (float-free modules byte-identical); ops lower
  to `f64.const/add/sub/mul/div`, `f64.{eq,ne,lt,le,gt,ge}` (→ `$w0`), `f64.convert_i32_s`. FCONST
  prints `%.17g` so the WAT round-trips the exact double.
- **Frontend (`frontend_interop.c`):** `TEKO_VT_FLOAT` (a high sentinel `1<<20`, chosen so it never
  collides with `TEKO_VT_OBJ_BASE + class_index` — every existing `>= OBJ_BASE` site is untouched);
  a `g_localflt` registry parallel to `g_localstr`; `eval_expr_prec` promotes/spills per operand
  type and emits the float op; `let`/reassignment store floats via `FSTORE_LOCAL`. A `VT_FLOAT`
  operand reaching a string concat is a deliberate no-op (float→string is 17.D).
- **Proof:** `runtime/{native,wasm}/samples/float.tks` → `a = 0` / `b = 1` / `c = 1` (float
  arithmetic + mixed promotion + comparisons, each 0/1 result observed via `convert.int_to_str`
  id 49 — no float formatter yet). Native (`run-native.sh`) and WASM (`run-float.mjs`,
  reactor-backed) outputs are BYTE-FOR-BYTE identical.

Verification: suite 232/232; ASan/UBSan (both dispatch paths) + TSan clean; the 16 native goldens
+ all float-free native/WASM output byte-identical (integer path untouched). **All four CI gates
GREEN** on PR #10 (incl. Windows MSVC x86_64 **and** arm64 — the f64/ABI concern is cleared; the
hosted `xmm`/`movabsq` path is not compiled on the freestanding Windows runner, and the MSVC-compiled
compiler C — float pool + `memcpy`'d bit pattern, no aliasing UB — is clean). Implementation
continues at **17.B** (checked int↔float casts).

Sub-block **17.B (checked int↔float casts + float modulo) is DONE** on both targets (locally green).
The two opcodes 17.A reserved are now LIVE, surfaced as explicit conversion builtins, with executable
`.tks` proofs on the happy AND fail-loud paths:
- **OP_FMOD (0x76)** — float `%`. Native is libc-hosted, so it reuses `fmod` (args already in
  xmm0/xmm1 = d0/d1 by the SysV/AAPCS convention; `call fmod` / `bl fmod`, result in $f0); WASM has
  no `f64.rem`, so it inlines the IEEE remainder toward zero `$f0 - trunc($f0/$f1)*$f1`
  (stack-neutral). `p12_tok_fop(TOKEN_MOD)` now maps `MOD → OP_FMOD`, so `7.5 % 2.0` lowers through
  the existing float-arith branch.
- **OP_F2I (0x82)** — CHECKED float→int (truncate toward zero), the **core deliverable**: it must
  **FAIL LOUD**, never silently truncate/wrap/UB. WASM lowers to `i32.trunc_f64_s`, which TRAPS on
  NaN/±Inf/out-of-i32-range automatically (a `WebAssembly.RuntimeError`, like 16.F's
  `__builtin_trap`). `cvttsd2si`/`fcvtzs` do NOT trap, so the hosted emitter emits an explicit
  inline guard — NaN test (`ucomisd %xmm0,%xmm0` + `jp` / `fcmp d0,d0` + `b.vs`) plus an i32-range
  check **matched to `i32.trunc_f64_s`'s valid open interval `-2147483649.0 < x < 2147483648.0`**
  (empirically confirmed) — that `call`s the new exported `teko_rt_f2i_fail` (the SAME exit-70 +
  stderr fail-loud path the 16.F parsers use). So a value that traps on WASM (tested with
  `3000000000.0`, over INT32_MAX) **aborts non-zero on native too** — identical behavior on both
  targets.
- **Surface** (`frontend_interop.c`): a NEW dotted-head path `convert.to_int` / `convert.to_float`
  (`is_floatcast_head` / `lower_floatcast`), claimed BEFORE the codec check at every `is_codec_head`
  site (eval_primary, lower_init_value — recording `g_last_init_vt` so a cast-initialized local reads
  back as the right type, and the top-level call-arg path). The argument is a full EXPRESSION
  (eval'd via `eval_expr_prec`), NOT a codec literal/local — so it bypasses `codec_id_for` /
  `lower_base_codec`. `to_int(float)` emits OP_F2I (`codegen_li_emit_f2i`); `to_float(int)` emits
  OP_I2F; an already-correct-type arg is a no-op.
- **codegen_metal.c:** OP_FMOD added to the float-op integer-CSE barrier set (writes $f0 only);
  OP_F2I added to BOTH the barrier set AND the $w0-clobber invalidation set (it writes $w0 with a
  non-constant int). Both single-byte — no 4-byte-arg-walker change.
- **Proofs:** `runtime/{native,wasm}/samples/cast.tks` → `a = 1 / n = 7 / m = 1` (byte-for-byte
  identical native vs WASM — `to_float`, checked `to_int`, `fmod`) and `cast_fail.tks` (emits
  `before`, then native aborts non-zero with `convert.to_int: float out of i32 range…` / WASM traps
  — `run-cast.mjs` mirrors `run-parse.mjs`'s happy+trap structure). Wired into `run-native.sh`, the
  wasm harness, and `wasm.yml`.

Verification: suite 232/232 (17.B adds no runtime C → no new KATs); ASan/UBSan (both dispatch paths)
+ TSan clean; the 16 native goldens + all float-free native/WASM output byte-identical (the integer
path and float-free modules emit zero float opcodes — purely additive). Implementation continues at
**17.C** (the freestanding shortest-round-trip `teko_convert_f64_to_string` formatter + KATs).

## Phase 17.F — exact base-10 `decimal` type (owner-APPROVED; implemented AFTER 17.A–17.E)
**Owner decision: APPROVED, C#-`System.Decimal` semantics.** An EXACT base-10 `decimal`, **128-bit /
16 bytes** = a **96-bit coefficient + sign + scale 0–28** (base-10), distinct from the binary f64 —
exact for money. **Banker's rounding (round-half-to-even) by default**, matching C# `System.Decimal`.
Self-contained 128-bit base-10 arithmetic (**no libc, no `__int128` — 64-bit limb pairs**), a decimal
formatter + parser reusing the 17.C/17.E infra, checked fail-loud casts, ONE KAT-tested C runtime →
native + WASM reactor (byte-identical), `.tks` proofs on both targets. **Sequenced AFTER the f64 core
(17.A–17.E) closes.** Because it is large, before implementing 17.F a sub-block plan + diff-size
estimate is reported to the owner, with a recommendation on whether it warrants its own PR (same
phase). The opcodes/value-type reserved below **go live in 17.F**. The reservation (made in 17.A) so
an approval never renumbers anything:
- **Value-type slot** `TEKO_VT_DECIMAL = (1 << 21)` (its own high sentinel, in `frontend_interop.c`).
- **Opcode byte range `0x83–0x96`** reserved CONTIGUOUSLY right after the float ops (`codegen_li.h`),
  mirroring the float layout: `OP_DCONST` (0x83), `OP_DADD/DSUB/DMUL/DDIV/DMOD` (0x84–0x88),
  `OP_DEQ..DGE` (0x89–0x8E), `OP_DSTORE/DLOAD/DSTORE_LOCAL/DLOAD_LOCAL` (0x8F–0x92), and the
  conversions `OP_I2D/D2I/F2D/D2F` (0x93–0x96). Next free opcode range starts at `0x97`.
These are claimed **only as documentation/comments** (exactly like the `0x76`/`0x82` float
reservations) — **no enum constants, no emitted opcodes, no live token** (no dead-token gate trip),
**no orphan reference**.
