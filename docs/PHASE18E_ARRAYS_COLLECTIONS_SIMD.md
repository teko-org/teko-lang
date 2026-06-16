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

### 18.E.1 — `array` substrate (fixed, contiguous, checked, O(1) len)
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

### 18.E.2 — `list` (dynamic, non-contiguous, chunked) + `for` iteration
- C runtime `src/runtime/teko_list.c`: chunked/segmented list — `new()`, `append(h,v)`, `get(h,i)`,
  `set(h,i,v)`, `len(h)` (O(1) metadata). Opcodes `OP_LIST_NEW/APPEND/GET/SET/LEN` → native + reactor.
- `for x in a { }` iteration over both `array` and `list` (add `in` as a contextual keyword — there
  is no TOKEN_IN today; `for` is reserved). Lowers to an index loop over `.len` (reuses the Phase-14
  control-flow foundation: LOOP_BEGIN/IF/BREAK).
- Proof `list.tks` + `foreach.tks` (native + WASM, byte-identical): append/grow, index, len, iterate-sum.

### 18.E.3 — AoS↔SoA layout for arrays-of-struct
- `soa Class[N]` (TOKEN_SOA): a structure-of-arrays collection = **k field-arrays of N** (one
  contiguous `array` per class field) vs AoS (default: an `array` of object handles). `s[i].f` →
  `field_array[f]` at `i`; `s[i].f = v` writes it. The k contiguous field runs are what 18.E.4
  vectorizes. Builds on 18.E.1 + the Phase-15 class field model. Runtime-backed → byte-identical.
- Proof `soa.tks` (native + WASM, byte-identical): build a `soa Point[N]`, set/get fields, sum a field
  across elements, demonstrate the field run is contiguous.

### 18.E.4 — Real SIMD emission over SoA field runs
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
