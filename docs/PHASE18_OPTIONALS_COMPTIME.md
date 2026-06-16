# Phase 18 — Zero-Overhead Optionals & Compile-Time Metaprogramming

Branch `feat/phase-18-optionals-comptime`, **PR #11** (draft — the human merges). Follows Phase 17
(Floating-Point & Numeric Types, merged via PR #10). This phase expands the **language surface** on
top of the now-validated backend/runtime: nullability, compile-time execution, scope-exit
registration, and a SIMD-friendly data layout — every token reserved in Phase 12's keyword matrix
(`comptime`, `defer`, `soa`, `null`) plus the optional operators (`?`, `??`, `?.`) goes **LIVE**
here, each with an executable `.tks` proof on native AND WASM (no dead tokens).

## Scope (from `docs/plan.md` §Phase 18 + the owner memorandum §"ZERO-OVERHEAD OPTIONALS…")
1. **`?T` nullability via compacted value types.** The Elvis operator `??` compiles directly to a
   hardware conditional (`je`/`cbz`). Plus `null` and safe navigation `?.`.
2. **`comptime`** — code execution at build time; metaprogramming during compilation.
3. **`soa`** (Structure of Arrays) — reorganizes array-of-struct layouts in RAM to enable native
   SIMD vectorization invisibly.
4. **`defer`** — syntactic registration of mandatory scope-closing routines (LIFO at scope exit).

## Tokens already reserved (Phase 12) — made LIVE here
Lexer (`src/lexer.{c,h}`) already tokenizes them; Phase 18 wires the frontend + lowering:
`TOKEN_QUESTION` (`?`), `TOKEN_ELVIS` (`??`), `TOKEN_SAFE_DOT` (`?.`), `TOKEN_NULL` (`null`),
`TOKEN_DEFER` (`defer`), `TOKEN_COMPTIME` (`comptime`), `TOKEN_SOA` (`soa`). **No lexer change is
expected** beyond, at most, recognizing `?` in a `let x: ?int` type position (the annotation parse
in `lower_let_stmt` currently only captures a leading *identifier*).

## Architecture grounding (read before implementing)
- The frontend (`src/frontend_interop.c`) is the real `.tks → IL` compiler for the supported subset.
  Value-typed locals are tracked by **parallel name-set registries** — `g_localcls` (object/class),
  `g_localstr` (string), `g_localflt` (float), `g_localdec` (decimal); a local absent from all of
  them is a plain int in `$w0`. The established way to add a typed local is **a new parallel
  registry** + a value-type bit (`TEKO_VT_*`). Optionals follow this exactly.
- The IL has structured control flow (`OP_IF_BEGIN/END` — *no else*), loops, and **local
  reassignment** (the Phase-14 control-flow foundation). `OP_IF_BEGIN` lowers to a conditional
  branch (native `je`/arm64 `cbz`; WASM `(if …)`) — so an Elvis lowered through it IS the
  hardware-conditional the memorandum asks for, honestly.
- The Phase-15B **fat-local precedent** (`g_traitlocal`: an instance-handle slot + a hidden
  compile-time-constant companion slot, object layout unchanged) is the template for the optional's
  hidden **present** companion.
- **There is no array / struct-field-layout substrate in the frontend today** (no `[]`, no
  array-of-struct). `soa` (Structure-of-Arrays) therefore has nothing to reorganize yet — it is the
  one sub-block that needs a new substrate. See **18.E**: it ships a **bounded MVP** (a minimal
  fixed-size homogeneous aggregate whose layout flips AoS→SoA, proven by contiguous-field access);
  full SIMD *emission* is documented as future optimization (same posture as AES-NI / hardware
  accel — correctness of the layout is the deliverable, not vector instructions).

## Sub-blocks (dependency order — one increment per commit, report at each close with CI green)

### 18.A — Optionals value model + `null` + Elvis `??`  *(headline)*  — ✅ DONE
**Status (DONE, locally green both targets).** Implemented in `src/frontend_interop.c`: a new
`g_localopt` registry (name → {present_slot, base_vt}); the `null` literal + an optional-local read
in `eval_primary` (exposing the present descriptor via `g_prim_present_slot`: -1 plain / -2 literal
null / ≥0 companion slot); Elvis `??` lowered in `eval_expr_prec` as a lowest-precedence,
right-associative `default → conditional-overwrite` over `OP_IF_BEGIN/END` + load/store-local +
iconst (**no new opcode/runtime**); `let x: ?T` detection + the present companion (`x#opt`) emitted
in `lower_let_stmt`, kept in sync by `lower_reassign`. Proof `optionals.tks` (native + WASM,
byte-identical) → `b = 7`, `d = 5`, `e = 5` (incl. a right-assoc `c ?? a ?? 9` chain). Suite
246/246; ASan/UBSan both dispatch paths + TSan clean; 16 goldens intact; optional-free output
byte-identical (every new path is gated on a `null`/`?`/`??` actually appearing). MVP payload =
int (string works incidentally via `$w0`); float/decimal optional payloads + runtime-null are 18.B+.


- **Value model (compacted, zero-overhead).** An optional local `?T` = the payload in its
  natural slot (`$w0`/`$vN` for int, the string slot, etc.) **+ a hidden 1-byte/word "present"
  companion local** (0 = null, 1 = present), exactly the 15.B fat-local pattern. No heap, no boxing.
  New registry `g_localopt` (name → {base_vt, present_slot}); reset alongside the others. New
  value-type bit `TEKO_VT_OPT` (next free bit, `1 << 22`) carried OR'd with the base VT through the
  evaluators.
- **`let x: ?int = null;`** — detect the leading `?` in the `let` annotation, mark the local
  optional, store payload=0/present=0. **`let x: ?int = 5;`** — present=1, payload=5.
- **`null` literal** — an expression of `TEKO_VT_OPT` with present=0.
- **Elvis `a ?? b`** — lowers to: `result := b` (default); `if (present(a)) { result := payload(a) }`
  — reusing `OP_IF_BEGIN/END` + reassignment (→ `je`/`cbz`). Result value-type = base of `a` /
  type of `b`. **No new opcode** (frontend-only lowering, like 15.C/15.D).
- **Proof** `optionals.tks` (native + WASM, byte-identical): `let a: ?int = null; let b = a ?? 7;`
  → `7`; `let c: ?int = 5; let d = c ?? 7;` → `5`; observed via `emit("… " + convert.int_to_str(…))`.
- Risk: low (frontend-only, reuses control flow). Covers int first; string-optional folds in if cheap.

### 18.B — Safe navigation `?.` + optional propagation  — ✅ DONE
**Status (DONE, locally green both targets).** `lower_safe_nav` in `src/frontend_interop.c`: claims
the `IDENTIFIER ?. member` form in `eval_primary` (before the optional-local read); guards the member
access (`OP_OBJ_GET` field / `OP_CALL_FUNC` method, reusing the 15.A emission) behind `OP_IF_BEGIN`
on the receiver's present flag — present → the access runs, null → SKIPPED (no deref of a null
handle); the result is itself optional (payload + a present-flag temp exposed via
`g_prim_present_slot`), so a trailing Elvis `?? d` defaults on null. An optional object records its
class from the `?Class` annotation (so a `null` receiver still resolves the field/method statically).
Proof `safenav.tks` (native + WASM, byte-identical) → `a=21` (field via `?.`), `b=42` (method+arg via
`?.`), `c=-1`/`d=-1` (null short-circuit). Suite 246/246; ASan/UBSan both paths + TSan clean; 16
goldens intact. **Pre-existing bug found + filed as tech debt** (NOT 18.B): a method returning a bare
`self.field` (terminal `OP_OBJ_GET`) yields 0 — `safenav.tks` uses an operator-return method
(`self.v * k`) to avoid it; the bug is orthogonal to safe-nav (affects plain method calls too).


- **`obj?.field` / `obj?.method()`** over Phase-15 objects: if the object optional is present,
  evaluate the member access; else short-circuit to `null` (present=0). Lowers as a guarded
  `OP_IF_BEGIN` around the existing `OP_OBJ_GET` / `OP_CALL_FUNC` member lowering, writing the
  optional result + present companion. Chains (`a?.b?.c`) propagate null.
- Extends the optional model to object-typed payloads (the present companion + the existing
  class/handle slot — again a fat local).
- **Proof** `safenav.tks` (both targets): a present object → field value; a null object → the `??`
  fallback after a `?.` chain. Byte-identical.
- Risk: low–medium (builds on 18.A + the 15.A/B object surface).

### 18.C — `defer` (LIFO scope-exit)  — ✅ DONE
**Status (DONE, locally green both targets).** `defer <stmt>;` makes `TOKEN_DEFER` LIVE. The
top-level loop captures each deferred statement's SOURCE (`defer_capture` rebuilds it from its
tokens — re-lexable, like a 16.C interpolation hole — into a LIFO stack `g_defer`); at `$main` close
the stack is DRAINED IN REVERSE through the normal dispatcher (`lower_one_stmt`) just before
`OP_HALT`. Deferred statements may reference locals (still live at scope exit) incl. auto-`to_string`
concat. No new opcode/runtime — the statements lower to ordinary IL, relocated to scope end;
defer-free programs are byte-identical. Proof `defer.tks` (native + WASM, byte-identical) →
`start`/`middle`/`end` (immediate), then LIFO `last registered`, `deferred n = 42` (captures `n`).
Suite 246/246; ASan/UBSan both paths + TSan clean; 16 goldens intact. **MVP scope = `$main`
(program-exit) defers** — `defer` inside a `fn`/method/block body (scope-local drain) is a documented
follow-up.


- **`defer <stmt>;`** registers a statement to run when the enclosing scope closes, in **reverse
  registration order** (LIFO). MVP scope = the `$main` body and `fn`/method/routine bodies (the
  block-body dispatcher already exists). Frontend collects deferred statement spans per scope and
  re-lowers them at the scope's close before its terminator (`emit`/`return`/HALT).
- Frontend-only (re-uses the body dispatcher); **no new opcode**.
- **Proof** `defer.tks` (both targets): `defer emit("3"); defer emit("2"); emit("1");` →
  `1`,`2`,`3` (registration emits `1`, then deferred `2` then `3` in LIFO). Byte-identical.
- Risk: low (frontend statement-buffering + replay at scope close).

### 18.D — `comptime` (compile-time execution)  — ✅ DONE
**Status (DONE, locally green both targets).** `comptime let NAME = <const-expr>;` makes
`TOKEN_COMPTIME` LIVE. A precedence-climbing compile-time evaluator (`comptime_eval`: int literals,
other comptime constants, `( )`, unary minus, `+ - * / %`) folds the expression AT COMPILE TIME and
binds NAME in the `g_comptime` registry — **no IL arithmetic is emitted** for the expression; a read
of NAME (in `eval_primary` and the codec-arg path `lower_codec_value`) lowers to a single
`iconst(value)`, so the module carries only the folded constant. Comptime constants compose (`B = A +
8`). No new opcode/runtime — comptime-free programs byte-identical. Proof `comptime.tks` (native +
WASM, byte-identical) → `A=42` (`6*7`), `B=50` (`A+8`), `C=10` (`(A-2)/4`), `D=2` (`A%5`). Suite
246/246; ASan/UBSan both paths + TSan clean; 16 goldens intact. **MVP = compile-time-constant integer
folding** (the metaprogramming foundation); `comptime { … }` blocks / comptime-generated code are a
documented follow-up.


- **`comptime <expr>` / `comptime { … }`** — evaluated by the frontend at compile time and folded
  to a constant emitted into the IL (a literal `OP_ICONST`/`OP_FCONST`/string-pool entry). MVP:
  constant-foldable integer/float/decimal/string arithmetic over literals and other `comptime`
  bindings (`comptime let K = 6 * 7;`). The folded value participates in normal lowering downstream.
- Frontend-only (a small constant evaluator over the existing expression grammar); the IL/backends
  are **unchanged** → comptime-free output stays byte-identical automatically.
- **Proof** `comptime.tks` (both targets): `comptime let K = 6 * 7; emit("K = " + convert.int_to_str(K));`
  → `K = 42`, with the IL carrying the *folded* constant (no runtime multiply). Byte-identical.
- Risk: low–medium (a compile-time evaluator, but bounded to constant expressions for the MVP;
  deeper metaprogramming — comptime-generated code — documented as a follow-up).

### 18.E — `soa` (Structure of Arrays)  *(riskiest — needs a new substrate; bounded MVP)*
- **Substrate (new):** a minimal fixed-size homogeneous aggregate the frontend can lay out — an
  array of a small struct with named fields. **AoS** (default) interleaves fields per element;
  **`soa T[N]`** flips the layout so each field is a contiguous run (field-major), which is what
  enables SIMD over a single field.
- **MVP deliverable:** the `soa` keyword changes the layout and field access is correct under it,
  proven by summing one field across all elements (the SoA layout makes that field's elements
  contiguous in memory). **Actual SIMD *instruction* emission is future work** (documented like
  AES-NI hardware accel) — the deliverable is the *layout transform* + correct access, not vector
  ops, so the proof is layout-correctness, not a perf number.
- **Proof** `soa.tks` (both targets): a `soa Point[3]` with `.x/.y`, write then sum `.x` across the
  3 elements → a known total. Byte-identical native vs WASM.
- **⚠ Scope fork (the one item needing owner confirmation):** because the frontend has no array
  substrate, 18.E must build a minimal one. The bounded MVP above (fixed-size aggregate + layout
  flip + field-sum proof, SIMD emission deferred) is the proposed scope. If the owner wants full
  array semantics or real SIMD emission in this phase, that enlarges 18.E materially.

## Per-sub-block pattern (same as Phases 14–17)
For surfaces that need a runtime: C source-of-truth → opcode → native `teko_rt_*` + WASM reactor
import → executable `.tks` proof on both targets. **18.A–18.D are frontend-only** (no new runtime,
reuse existing opcodes) — the pattern collapses to: frontend lowering → `.tks` proof both targets.
18.E adds the minimal aggregate substrate (frontend layout + access lowering; no new runtime call
expected for the MVP).

## Discipline (every commit)
1 increment/commit; build + suite; **ASan+UBSan BOTH dispatch paths + TSan clean**; **16 native
goldens + all optional/comptime/defer/soa-free native+WASM output byte-identical** (gate every new
emission behind a `uses_*`/presence check; these surfaces are purely additive); **4 CI gates green
incl. Windows MSVC**; executable `.tks` proof native+WASM byte-identical per surface; **no dead
tokens**; **no merge/force-push** (human merges); patient CI watch (≥90s between polls). Report to
the owner at each sub-block close with CI green; pause only on a NEW scope fork (the 18.E array
substrate is the pre-flagged one).

## Order
18.A (optionals + `null` + `??`) → 18.B (`?.`) → 18.C (`defer`) → 18.D (`comptime`) →
18.E (`soa`, bounded MVP, scope-confirm).
