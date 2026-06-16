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

Sub-block **17.C (Ryu shortest-round-trip f64->string C core) is DONE** (locally green) — the
single C source of truth, KAT-tested, with **NO language surface** (the `convert.float_to_str`
surface = id 50 + auto-`to_string` is 17.D; like the parse core that landed in 16.A — so no `.tks`
proof here). Owner-APPROVED algorithm = **Ryu** (Ulf Adams, PLDI 2018).
- **Runtime:** `char* teko_convert_f64_to_string(double)` in new `src/runtime/teko_convert_f64.c`
  (+ the power-of-5 split tables in `src/runtime/teko_convert_f64_tables.h`), declared in
  `teko_convert.h`. The shortest-decimal core (`d2d`/`d2d_small_int` + `umul128`/`shiftright128`/
  `mulShift64`/`mulShiftAll64`/`pow5Factor`/`multipleOfPowerOf5`/log helpers/`div*`) is transcribed
  from the reference impl (github.com/ulfjack/ryu `d2s.c`/`d2s_intrinsics.h`/`common.h`) with an
  **Apache-2.0 / Boost-1.0 attribution header** (NOT claimed public-domain). The two
  `DOUBLE_POW5_INV_SPLIT` (342) / `DOUBLE_POW5_SPLIT` (326) tables are reproduced byte-for-byte.
- **Freestanding / portability (HARD constraints, all met):** no `math.h`/`snprintf`/`strtod`/
  `setlocale` — only `malloc`/`memcpy`/`strlen` (verified: the wasm32 object's only undefined
  symbols are exactly those three). **NO `__int128` and NO 128-bit libcall** — always Ryu's portable
  `!HAS_UINT128 && !HAS_64_BIT_INTRINSICS` 64-bit-halves path (verified: `llvm-nm` shows no
  `__multi3`/`__udivti3`/`__umodti3`/`__divti3`). MSVC/Windows-safe portable C, no `auto`/`nullptr`,
  no VLAs, pure/deterministic (native == WASM byte-identical).
- **Renderer policy (custom, NOT Ryu's `to_chars`; documented in the file header):** plain
  culture-invariant `.`-decimal; ALWAYS ≥1 fractional digit (`1.0`/`42.0`/`100.0`); specials
  `NaN`/`Infinity`/`-Infinity`/`0.0`/`-0.0`; **scientific (lowercase `e`, explicit sign, no
  leading-zero exponent) only when `e10 < -4` or `e10 >= 21`** (mirrors ECMAScript
  `Number.toString`), else plain decimal; shortest digits, never padded to 17. Catalog confirms:
  `5e-324`→`5e-324`, `1.7976931348623157e308`→`1.7976931348623157e+308`, `1e20`→plain,
  `1e21`→`1e+21`, `0.1`→exactly `0.1`.
- **Build wiring (proves wasm32 compat NOW, no surface):** added to `CMakeLists.txt` CORE_SOURCES
  + the `teko_rt` archive, and to `runtime/wasm/crypto/build-crypto-reactor.sh` `SRCS`
  (compile-only, NOT EXPORTS — no surface until 17.D). Confirmed it compiles + the reactor links
  under `--target=wasm32 -ffreestanding -nostdlib`.
- **KATs (new `tests/runtime/test_convert_f64.c`, 3 tests):** (1) exact-string catalog (zeros/
  small wholes/fractions/pi/2^53/powers-of-ten across the e-threshold/`5e-324`/DBL_MAX/specials);
  (2) round-trip property over ~30,265 doubles (39-value catalog + a 10,230-double binary-exponent
  sweep + a 19,996-iter fixed-seed LCG over raw bit patterns, skipping NaN/Inf) — format then
  re-parse with the **HOST libc `strtod`** and assert bit-for-bit equal; (3) shortest spot-checks
  (`0.1`/`0.3`/`1.0` emit no padded digits).

Verification: suite 232→235/235 (the 3 new f64 KATs); ASan/UBSan (both dispatch paths) + TSan clean;
the 16 native goldens + all prior native/WASM proofs intact (17.C is additive runtime + tests only —
no emitter/frontend file touched). Implementation continues at **17.D** (the `convert.float_to_str`
surface = id 50 + auto-`to_string` for floats, with `.tks` proofs on both targets).

Sub-block **17.D (`convert.float_to_str` surface + auto-`to_string` for floats) is DONE** on both
targets (locally green) — id 50 is now LIVE, with an executable `.tks` proof emitting BYTE-IDENTICAL
output native + WASM. The crux: **id 50 is the ONLY OP_CALL_RUNTIME id whose value argument is a
`double`** — the float rides the PARALLEL float accumulator `$f0` (xmm0/d0 native; `(local $f0 f64)`
WASM), NOT the integer `$w0`; the result is a `char*` in `$w0` (VT_STR), like the other to_string ids.
- **Runtime:** `char* teko_rt_float_to_string(double)` (`runtime/native/teko_rt.c`/`.h`) → the 17.C
  Ryu shortest-round-trip `teko_convert_f64_to_string`. Added to the reactor `EXPORTS`
  (`build-crypto-reactor.sh`) — `teko_convert_f64.c` was already a reactor SRC from 17.C.
- **Native (`emit_native_hosted.c`):** symbol table gains `case 50` (arity 1); OP_CALL_RUNTIME
  **special-cases id 50** to a BARE call (`emit_call_noarg`) — the double is already in xmm0/d0 (=
  `$f0`) by the SysV/AAPCS FP-arg convention, so the normal `$w0`→GP-arg marshal must be skipped.
- **WASM (`emit_wasm.c`):** id 50 added to `wasm_is_crypto_ext_id`; the import is declared
  `(func $crypto_50 (param f64) (result i32))` — the ONLY non-i32 reactor import — and **gated on
  `wasm_emit_float`** so a float-free crypto-ext module (convert.tks etc.) stays byte-identical; the
  call lowers `local.get $f0 / call $crypto_50 / local.set $w0`. (`codegen_li_emit_call_runtime`
  sets `uses_float` for id 50 defensively so `$f0` is declared.)
- **Frontend (`frontend_interop.c`):** `coerce_to_string_in_w0(VT_FLOAT)` now emits id 50 (reads
  `$f0`) — replacing the 17.A no-op. The `+` concat path reloads a LEFT-float operand via
  `FLOAD_LOCAL` (not the integer `load_local`) before coercing; a RIGHT-float and interpolation float
  holes are already in `$f0` at the coerce point. `convert.float_to_str(<expr>)` is surfaced through
  the SAME `is_floatcast_head`/`lower_floatcast` machinery as 17.B's `to_int`/`to_float` (promotes an
  int arg via OP_I2F, emits id 50, returns VT_STR) — so every call site (eval_primary,
  lower_init_value, the top-level call-arg path) picks it up; the call-arg path accepts a bare
  `convert.float_to_str(…)` (its char* result spills like `to_int`'s i32).
- **Proof:** `runtime/{native,wasm}/samples/floatstr.tks` → `f = 3.14` / `0.1` / `pi~ 3.14, dbl 6.28`
  / `2.5` / `3.14 = pi` (RIGHT-float concat, explicit `convert.float_to_str(0.1)`, interpolation with
  a float hole + a float-EXPRESSION hole `{f * 2.0}`, a format bound to a string local, LEFT-float
  concat). Native (`run-native.sh`) and WASM (`run-floatstr.mjs`, reactor-backed) are BYTE-FOR-BYTE
  identical (the same 17.C Ryu C runs both sides). Wired into `run-native.sh`, the wasm harness, and
  `wasm.yml`.

Verification: suite 235/235 (17.D adds no runtime C → no new KATs); ASan/UBSan (both dispatch paths)
+ TSan clean; the 16 native goldens + all float-free native/WASM output byte-identical (id 50 only
fires when a float is formatted; the `crypto_50` import is `wasm_emit_float`-gated).

Sub-block **17.E (`convert.parse_float`, id 54, checked fail-loud) is DONE** on both targets —
**the f64 core (17.A–17.E) is COMPLETE.** id 54 is the INVERSE of id 50's ABI: a **string** arg
(ptr in `$w0`) → a **double** result in the float accumulator `$f0` (VT_FLOAT). Native: the normal
`emit_call` marshals the string `$w0`→arg-reg and the `double` return lands in xmm0/d0 = `$f0`
automatically (no special emission). WASM: import `(func $crypto_54 (param i32) (result f64))`
(`wasm_emit_float`-gated, the second non-i32 reactor import after id 50) + call
`local.get $w0 / call $crypto_54 / local.set $f0`.
- **Parser core** `teko_convert_parse_f64` (in `teko_convert_f64.c`, +373 LOC) — freestanding (no
  `strtod`/`math.h`/`setlocale`, no `__int128`), **correctly rounded** (round-to-nearest-even) via
  **simple decimal conversion** (decimal-digit buffer → binary mantissa by repeated multiply/divide-
  by-2), so `parse(format(d)) == d` bit-for-bit. CHECKED/**fail-loud**: malformed input + overflow
  (→±Inf) return 0 → `teko_rt_parse_float` calls the SAME `teko_rt_die` 16.F uses (native exit 70 +
  stderr / wasm reactor `__builtin_trap`); underflow-to-subnormal/0 is representable (not an error).
- **Surface** via the codec path (`codec_id_for` → 54; `runtime_result_vt(54)` → VT_FLOAT) so
  `convert.parse_float(s) + 1.0` is float arithmetic and `"x=" + convert.parse_float(s)`
  auto-`to_string`'s via id 50.
- **KATs (+3 → 238/238):** parse-exact catalog (incl. subnormal `5e-324`, DBL_MAX, `2.5e-3`, `-0.0`),
  the **format→parse→identity round-trip** over the 17.C vector (proves correct rounding), and reject
  cases (`""`/`"abc"`/`"1.2.3"`/`"1e"`/overflow → 0).
- **Proofs** `runtime/{native,wasm}/samples/parsefloat.tks` → `3.14 / got 0.5 / 3.0` (parse→format
  round-trip, parse-result auto-`to_string`, float arithmetic on two parses) **byte-identical native
  vs WASM**; `parsefloat_fail.tks` → emits `before` then native-aborts exit 70 / WASM-traps on
  `"notafloat"`. `run-parsefloat.mjs` + run-native.sh + wasm.yml wired.

Verification: suite 238/238; ASan/UBSan (both dispatch paths) + TSan clean; 16 native goldens + all
float-free native/WASM output byte-identical. **The Phase-16 float gate is CLOSED** — floats now have
a value model, arithmetic/compares, checked casts, culture-invariant shortest-round-trip
formatting/parsing, and auto-`to_string` in concat/interpolation, on native AND WASM, no dead tokens.
Next is **17.F** (the owner-approved exact base-10 `decimal`) — a sub-block plan + diff estimate +
own-PR recommendation is reported to the owner BEFORE implementing.

## Phase 17.F — exact base-10 `decimal` type (owner-APPROVED; implemented AFTER 17.A–17.E)
**Owner decision: APPROVED — a FIXED-WIDTH 256-BYTE base-10 value type** (NOT C# `System.Decimal`;
the earlier "128-bit/16-byte" note was superseded by the owner's envelope update). **256 bytes /
2048 bits**, no heap, no dynamic bignum, no `__int128`/libc. Target layout: **~8 bytes metadata**
(sign + decimal scale/exponent) + **~248 bytes base-10 coefficient** → **~590 significant digits**,
with the fractional part bounded to **~128 bits ≈ ~38 decimal places**. **Banker's rounding
(round-half-to-even) by default.** Self-contained arithmetic over **64-bit limb arrays** (31×u64
coefficient; no `__int128`), a decimal formatter + parser reusing the 17.C/17.E patterns, checked
fail-loud casts, ONE KAT-tested C runtime → native + WASM reactor (byte-identical), `.tks` proofs on
both targets. Because 256 bytes does NOT fit a register, decimal flows via the **memory-slot
`$d0/$d1` + `teko_rt_decimal_*` runtime-call model** (the channel/object family pattern, NOT the f64
register accumulator). **Sequenced AFTER the f64 core (17.A–17.E) closes;** recommended as its **own
PR** (large — est. ~2,500–3,500 LOC). KAT oracle = Python `decimal` at high precision with
`ROUND_HALF_EVEN` (a committed generator script emits the reference vectors + 256-byte encodings).
The opcodes/value-type reserved below **go live in 17.F**. The reservation (made in 17.A) so the
approval never renumbers anything:
- **Value-type slot** `TEKO_VT_DECIMAL = (1 << 21)` (its own high sentinel, in `frontend_interop.c`).
- **Opcode byte range `0x83–0x96`** reserved CONTIGUOUSLY right after the float ops (`codegen_li.h`),
  mirroring the float layout: `OP_DCONST` (0x83), `OP_DADD/DSUB/DMUL/DDIV/DMOD` (0x84–0x88),
  `OP_DEQ..DGE` (0x89–0x8E), `OP_DSTORE/DLOAD/DSTORE_LOCAL/DLOAD_LOCAL` (0x8F–0x92), and the
  conversions `OP_I2D/D2I/F2D/D2F` (0x93–0x96). Next free opcode range starts at `0x97`.
These are claimed **only as documentation/comments** (exactly like the `0x76`/`0x82` float
reservations) — **no enum constants, no emitted opcodes, no live token** (no dead-token gate trip),
**no orphan reference**.

### Status — 17.F.1 (the 256-byte exact base-10 `decimal` runtime CORE) is DONE
Runtime + KATs only (NO language surface / opcodes / frontend — those are 17.F.3/.4; this mirrors
the parse/format cores that landed without surface in 16.A/17.C). Additive: NO emitter/frontend/
opcode file was touched, so the 16 native goldens and all prior native/WASM proofs are byte-identical.
- **Value type** `src/runtime/teko_decimal.h` — the owner-LOCKED 256-byte `teko_decimal`
  (`uint8_t sign,scale,flags,_pad[5]; uint64_t limb[31]`), value = (-1)^sign·COEFF·10^(-scale),
  COEFF = LE unsigned 1984-bit integer, scale 0–38, always finite. `_Static_assert(sizeof==256)` +
  `_Static_assert(_Alignof==8)` (hold on Apple/Linux clang AND `--target=wasm32 -ffreestanding
  -nostdlib`). ABI = ALWAYS by pointer.
- **Runtime** `src/runtime/teko_decimal.c` — self-contained 64-bit-limb arithmetic with banker's
  rounding: portable `umul64` (no `__int128`), `bn_add/sub/cmp/mul`, `bn_mul_pow10`, `bn_divmod`
  (**Knuth Algorithm D**, base 2^32 half-limbs), `bn_round_drop_decimals` (round-half-to-even). All
  temps are fixed-size stack arrays (≤64 limbs); NO `__int128`/libm/`snprintf`/`malloc`. Public API
  (by pointer, int 1=ok/0=fail-loud): `teko_decimal_add/sub/mul/div/mod`, `teko_decimal_cmp`,
  `teko_decimal_zero`, and the test helper `teko_decimal_from_components`.
- **Semantics (pinned; the file's header is the canonical statement):** each op computes the EXACT
  result then (1) banker's-rounds to scale 38 only if it needs >38 frac digits, (2) fails-loud on
  coeff ≥ 2^1984 or div/mod-by-zero. Per-op: add/sub align to max scale; mul scale = sa+sb (cap 38);
  div to scale 38 (guard-digit + sticky banker's); mod = `a - trunc(a/b)·b` (Python
  `Decimal.__mod__`, `//` toward zero), result scale = max(a.scale,b.scale); cmp aligns in a wide
  temp, ±0 equal. Results stored UN-NORMALIZED at the natural scale. **Owner-confirmation note:**
  the one place the C and the Python oracle initially disagreed was the *scale of a zero `mod`
  remainder* — Python's `Decimal` subtraction picks its own zero-exponent; the C's exact `a−t·b`
  lands at `max(a.scale,b.scale)`. Resolved by pinning the oracle to the C's deterministic rule
  (the well-defined choice). No other ambiguity surfaced.
- **KATs** `tools/gen_decimal_kats.py` (committed generator, fixed-seed LCG, Python `decimal`
  oracle: prec≥600, `ROUND_HALF_EVEN`) emits `tests/runtime/teko_decimal_kat_vectors.h` (committed):
  **1326 vectors** (1311 ok / 15 expect_fail), each operand+result in the exact 256-byte LE encoding.
  Covers all ops, scales 0–38, negatives/±0, banker's ties, non-terminating div (1/3, 2/7, n/{3,7,9,
  11,13,17,23} at scale 38 — the Knuth-division safety net), near-590-digit operands, coeff overflow
  & div/mod-by-zero → expect_fail. `tests/runtime/test_decimal.c` asserts the ok-flag AND (on ok)
  `memcmp(out,expected,256)==0` byte-for-byte; registered in `tests/test_main.c`.
- **Build wiring:** `teko_decimal.c` added to CMake `CORE_SOURCES` + the `teko_rt` archive, and to
  `build-crypto-reactor.sh` SRCS (compile-only, NOT EXPORTS — no surface yet; the reactor still links).
- **Verification:** suite **241/241** (238 + struct-layout + from-components + the 1326-vector KAT);
  ASan+UBSan clean on BOTH dispatch paths; TSan clean; wasm32 freestanding compile + `_Static_assert`s
  confirmed; 16 native goldens + all prior proofs byte-identical; no `__int128`/libm/`snprintf`/`malloc`
  in `teko_decimal.c`.

Next: **17.F.2** (decimal parse/format core, reusing the 17.C/17.E patterns) → **17.F.3/.4** (the
opcodes + language surface + checked casts). 17.F.1 is the source of truth those build on.
