# Phase 18.E — Arrays, Collections (`list`) & real SIMD (full scope)

Owner approved **full scope** (not the bounded MVP): a real array/collection substrate + real SoA
layout + **real per-backend SIMD instruction emission**. Design-first; this doc captures the
owner-LOCKED fork decisions and the dependency-ordered sub-blocks. Same branch
`feat/phase-18-optionals-comptime`, PR #11 (draft; human merges). Follows 18.A–18.D (all CI-green).

## Owner-LOCKED decisions (forks resolved — do not relitigate)
1. **Bounds-checking.** `array` access (`a[i]` read/write) is **CHECKED, fail-loud** (out-of-range →
   native exit 70 / WASM trap), consistent with the parse_int/cast/decimal posture. (Dynamism is NOT
   achieved by unchecked arrays — it is a separate `list` type.)
2. **Two collection types.**
   - **`array`** — FIXED size, CONTIGUOUS, checked, SIMD-able. The numeric/SoA substrate.
   - **`list`** — DYNAMIC, fully growable, **non-contiguous**; the general collection substrate other
     collections build on. Slower than `array` (non-contiguous), that is the accepted trade.
   - **Every collection carries length METADATA** (O(1) count — no scan), even at a memory cost.
3. **SIMD targets (maximal, no-conflict union of opt1+opt2).** Real vectorization on: x86_64 **SSE2**
   baseline (+ **AVX2** opportunistic), arm64 **NEON**, WASM **simd128**, and **riscv64 RVV
   best-effort**. Everything else (the 16 freestanding emitters, any target without a vector unit, or
   riscv where QEMU's CPU lacks RVV) falls back to **scalar**, honestly marked (the AES-NI posture:
   "native-accelerated vs scalar-emulated"). The 16 freestanding goldens stay scalar → byte-identical
   preserved. CI vector coverage: NEON (macOS arm64), SSE/AVX (Linux x86_64), simd128 (Node/wasmtime),
   RVV (Linux riscv64 under QEMU, non-blocking).
4. **SIMD correctness proof = output-identity + scalar self-check.** SIMD instruction streams DIFFER
   per ISA, so `.s`/`.wat` byte-identity is impossible for vectorized code. Instead prove: (1) stdout
   IDENTICAL native vs WASM, and (2) in the same program, the vectorized result == a scalar reference
   loop. Byte-identity of emitter output STILL HOLDS where there is NO SIMD (18.E.1–.3 are
   runtime-C-backed → byte-identical native+WASM as usual; only 18.E.4's vectorized numeric output is
   proven by value, not by bytes).

## Design decision (mine, per owner delegation): the `list` structure
`list` = a **chunked/segmented list** (an unrolled list of fixed-size blocks): a growable index of
block pointers, each block a small fixed array. Gives O(1) length metadata, amortized O(1) append,
O(1) random access by (block, offset), and far better cache behavior than a node-per-element linked
list — while being genuinely non-contiguous (satisfies the owner's "não contíguo, totalmente
dinâmico, base de outras coleções"). The single C runtime is the source of truth (native + WASM
reactor), like every prior runtime.

## Sub-blocks (dependency order — one increment per commit, report at each close with CI green)

### 18.E.1 — `array` substrate (fixed, contiguous, checked, O(1) len)  — ✅ DONE
**Status (DONE, locally green both targets).** `src/runtime/teko_array.{c,h}` (handle store, O(1)
len metadata in the header, `intptr_t` cells — Windows LLP64-safe; bounds-RETURNING get/set);
`teko_rt_array_*` wrappers fail-loud on OOB (native exit 70 + stderr `array: index out of bounds`,
WASM `__builtin_trap`); opcodes `OP_ARR_NEW/GET/SET/LEN` (0x97–0x9A) mirroring `OP_OBJ_*` (both CSE
sets, native `call teko_rt_array_*`, WASM reactor import gated on `wasm_emit_array`); reactor
SRCS+EXPORTS; CMake; frontend `g_localarr` + array literal `[…]` + index r/w `a[i]` (eval_primary +
lower_codec_value + statement dispatch) + `a.len`. Proof `arrays.tks` (native + WASM byte-identical)
→ `a[1] = 20`, `a[0] = 99`, `len = 3`; `arrays_fail.tks` → fail-loud (native exit 70 / WASM trap).
Suite 246/246; ASan/UBSan both paths + TSan clean; native 50 OK/0 FAIL; 16 goldens byte-identical;
existing WASM proofs (decimal/class/optionals) intact after the reactor rebuild. Element cells are
register-width i64 (ints/handles); typed numeric element arrays for SIMD land in 18.E.2/.4.


- C runtime `src/runtime/teko_array.c` (handle store, `teko_object.c` pattern): `alloc(n)→handle`,
  `get(h,i)`, `set(h,i,v)`, `len(h)` (O(1) metadata). **Bounds-checked**: get/set out-of-range →
  fail (wrapper `teko_rt_die` native exit 70 / WASM trap). Register-width i64 cells for the MVP
  (ints/handles); typed numeric element arrays land in 18.E.2 for SIMD.
- Opcodes `OP_ARR_NEW (0x97) / OP_ARR_GET (0x98) / OP_ARR_SET (0x99) / OP_ARR_LEN (0x9A)` → native
  `teko_rt_array_*` + WASM reactor import (add `teko_array.c` to the reactor SRCS+EXPORTS). Gated on
  `uses_array`. codegen_li emit helpers + codegen_metal dispatch (runtime-call family, like OP_OBJ_*).
- Frontend: array literal `[e0,e1,…]` → `ARR_NEW(n)` + per-element `SET`; indexing `a[i]` read
  (postfix `[` in eval_primary) + write (`a[i] = v` statement); `a.len`. New `g_localarr` registry.
- Proof `arrays.tks` (native + WASM, BYTE-IDENTICAL): build, index r/w, `.len`, + a fail-loud
  out-of-bounds case (`arrays_fail.tks`).

### 18.E.2 — `for … in` iteration + typed `i32[]` packed arrays  — ✅ DONE
**(Owner reordered: 18.E.2 is `for…in` + typed numeric arrays; the dynamic `list` type is DEFERRED
off the critical path to SIMD — it can land as a later sub-block.)**
**Status (DONE, locally green both targets).** (A) `for NAME in ARR { }` iteration over any array
(`lower_for`, contextual `in` keyword, hidden index local, `< .len` guard via the control-flow
foundation, binds the loop var to `ARR[i]`; wired into the block dispatcher + top-level loop). (B) a
typed **`i32[]` packed numeric array** (the SIMD substrate): `src/runtime/teko_iarray.{c,h}` (packed
`int32_t` cells, O(1) len, bounds-RETURNING get/set), `teko_rt_iarray_*` fail-loud, opcodes
`OP_IARR_NEW/GET/SET/LEN` (0x9B–0x9E) mirroring `OP_ARR_*`, reactor import gated on `wasm_emit_iarray`;
frontend `g_localiarr` + the `: i32[]` annotation → typed literal, with index r/w / `.len` / `for…in`
dispatching the IARR op family (plain `[…]` stays the i64 array, byte-identical). Proofs `foreach.tks`
(sum 60), `iarray.tks` (typed i32: a[2]=6/len=3/sum=15/a[0]=40), `iarray_fail.tks` (fail-loud) — all
byte-identical native+WASM. Suite 246/246; ASan/UBSan both paths + TSan clean; native 55 OK/0 FAIL;
16 goldens byte-identical. **f64-typed arrays deferred** (avoids float-accumulator plumbing here;
i32x4 is a complete SIMD demo for 18.E.4). **BUNDLED FIX (disclosed):** the implementing agent also
fixed a PRE-EXISTING Phase-15 bug (resolves the filed tech-debt) — a class method / trait dispatch
used as an expression ARGUMENT or arithmetic operand, and a method returning a bare `self.<field>`,
were mis-lowered to iconst 0; now they lower to `OP_CALL_FUNC` / `vtable_get`+`OP_CALL_FUNC`. Entangled
in frontend_interop.c with the 18.E.2 changes (clean split unsafe), so committed together with
regression proofs `method_arg.tks` (42/42/47/142) + `trait_arg.tks` (12/112/25), byte-identical
native+WASM.


- C runtime `src/runtime/teko_list.c`: chunked/segmented list — `new()`, `append(h,v)`, `get(h,i)`,
  `set(h,i,v)`, `len(h)` (O(1) metadata). Opcodes `OP_LIST_NEW/APPEND/GET/SET/LEN` → native + reactor.
- `for x in a { }` iteration over both `array` and `list` (add `in` as a contextual keyword — there
  is no TOKEN_IN today; `for` is reserved). Lowers to an index loop over `.len` (reuses the Phase-14
  control-flow foundation: LOOP_BEGIN/IF/BREAK).
- Proof `list.tks` + `foreach.tks` (native + WASM, byte-identical): append/grow, index, len, iterate-sum.

### 18.E.3 — AoS↔SoA layout for arrays-of-struct  — ✅ DONE
**Status (DONE, locally green both targets).** Frontend-only (reuses the iarray + object runtimes;
NO new opcode/runtime/SIMD → SoA/AoS-free output byte-identical). **SoA** `let s = soa Class[N];`
allocates k contiguous typed-i32 arrays (`OP_IARR_NEW` ×k), each field handle parked in a hidden
local `s#f<idx>` (`g_localsoa` registry); `s[i].field` r/w → `OP_IARR_GET/SET` on the field run;
`s.len` → N; **`s.field` whole-run accessor** (no index) → the contiguous i32[] handle, and
`let col = s.field;` records `col` as a real iarray local aliasing the SAME run (write-through, `.len`,
`[i]`, `for…in` all work) — **this is the 18.E.4 SIMD hook**. **AoS** `let a = [Point(),…];` (i64
array of object handles) + `a[i].field` index-then-member (`OP_ARR_GET`→`OP_OBJ_GET`) r/w (`g_localaos`).
Supporting fix (additive, verified): `eval_primary` gained an `is_instantiation_head` branch so
`ClassName()` works in expression position (needed for the array literal). Proofs `soa.tks`
(`s[1].x=20`/`s[2].y=3`/`len=3`/`sum_x=60`/`col.len=3`/`col[1]=20`) + `aos.tks` (`a[1].x=20`/`a[2].y=3`/
`len=3`/`sum_x=60` — same logical result, AoS layout), byte-identical native+WASM. Suite 246/246;
ASan/UBSan both paths + TSan clean; native 57 OK/0 FAIL; 16 goldens byte-identical; instantiation-path
proofs (class/generics/traits/method_arg) intact.


- `soa Class[N]` (TOKEN_SOA): a structure-of-arrays collection = **k field-arrays of N** (one
  contiguous `array` per class field) vs AoS (default: an `array` of object handles). `s[i].f` →
  `field_array[f]` at `i`; `s[i].f = v` writes it. The k contiguous field runs are what 18.E.4
  vectorizes. Builds on 18.E.1 + the Phase-15 class field model. Runtime-backed → byte-identical.
- Proof `soa.tks` (native + WASM, byte-identical): build a `soa Point[N]`, set/get fields, sum a field
  across elements, demonstrate the field run is contiguous.

### 18.E.4 — Real SIMD emission over SoA field runs  — ✅ DONE
**Status (DONE, locally green both targets).** `simd.sum(run)` reduces a contiguous `i32[]` run (an
iarray local or the SoA `s.field` whole-run from 18.E.3) to its scalar sum via REAL per-backend vector
instructions. The backend emits ONE kernel function `teko_simd_sum_i32` per module (gated on
`uses_simd`/`wasm_emit_simd`); `OP_SIMD_SUM` (0x9F) lowers to: get data ptr (`teko_rt_iarray_data`) +
len → call the kernel. Kernel = 4-wide vector accumulate → 4-lane collapse → scalar tail. **REAL
emission confirmed:** x86_64 **SSE2** (`movdqu`/`paddd`, AT&T, store-collapse), arm64 **NEON**
(`ld1 v.4s`/`add v.4s`/`addv` — verified in the native `.s`), WASM **simd128** (`v128.load`/`i32x4.add`/
`extract_lane` — verified in the `.wat`); **scalar fallback** for riscv64 + the 16 freestanding
emitters (honest, gated → byte-identical preserved). Proof `simd.tks`: a 10-element `i32[]`
(`simd.sum`=55) + a `soa Point[6]` field run (`simd.sum(s.x)`=210), each with an IN-PROGRAM scalar
self-check (`for`/`while` reference) — **vectorized == scalar must hold on every target** (a
mis-emitted kernel diverges and fails the proof), N=10/6 exercise the scalar tail. **Output-identical
native==WASM** (the byte-identity rule is relaxed to value-identity only for the vectorized op, as
agreed). NEON + simd128 self-checks pass locally; x86_64 SSE2 validated by CI Linux x86_64 + the
self-check. Suite 246/246; ASan/UBSan both paths + TSan clean; native 58 OK/0 FAIL; 16 goldens
byte-identical; non-simd output byte-identical. riscv64 RVV remains a documented opportunistic
follow-up (scalar is the honest fallback there).

## PHASE 18.E COMPLETE (18.E.1–.4); PHASE 18 COMPLETE
Arrays (fixed/contiguous/checked) + typed `i32[]` + `for…in` + AoS↔SoA + REAL per-ISA SIMD, all
CI-green, no dead tokens, byte-identical (output-identical for the SIMD op). **Deferred by owner
reorder (off the SIMD critical path):** the dynamic non-contiguous `list` type, f64-typed numeric
arrays (float SIMD), AVX2/RVV opportunistic upgrades — all documented future sub-blocks.
- A vectorized op family over a contiguous SoA field run (numeric `array`): a reduction (`simd.sum`)
  and an elementwise map/add (`simd.add(a,b)→c`). Per-backend emission:
  - x86_64: SSE2 (`movdqu`/`paddd` int, `addps` f32) baseline; AVX2 (`vpaddd`/`vaddps`) opportunistic.
  - arm64: NEON (`ld1`/`add v.4s`/`fadd v.4s`).
  - WASM: simd128 (`v128.load` + `i32x4.add`/`f32x4.add`), reduction via lane extract.
  - riscv64: RVV best-effort (`vsetvli`/`vle32`/`vadd.vv`); scalar fallback if the QEMU CPU lacks RVV.
  - Scalar fallback elsewhere (16 freestanding emitters, non-vector targets) — honestly marked.
  - **Tail handling**: N not a multiple of the vector width → a scalar remainder loop.
- Proof `simd.tks`: (1) the vectorized reduction result is printed and must be IDENTICAL native vs
  WASM; (2) the program ALSO computes a scalar reference and asserts vectorized == scalar (self-check),
  so a mis-emitted vector op fails loudly on every target. Document which targets ran vectorized vs
  scalar (the harness logs the path).

## Discipline (every commit)
1 increment/commit; build + suite; ASan+UBSan BOTH dispatch paths + TSan clean; **16 native goldens
byte-identical** (arrays/SIMD are additive + gated — the freestanding emitters never see the new
opcodes); 4 CI gates green incl. Windows MSVC; executable `.tks` proof per surface — **byte-identical
native+WASM for 18.E.1–.3, output-identical + scalar-self-check for 18.E.4's vectorized output**; no
dead tokens; no merge/force-push (human merges); patient CI watch. Report at each sub-block close.

## Order
18.E.1 (`array`) → 18.E.2 (`list` + `for`) → 18.E.3 (SoA) → 18.E.4 (SIMD).
