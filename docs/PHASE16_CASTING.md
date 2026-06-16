# Phase 16 — Casting / Type Conversions & Parsing

Branch `feat/phase-16-casting` (PR #9, draft). Builds the connective tissue between Phase 15's
type model and every surface that serializes or displays a value: **universal, culture-invariant
conversions between types and to/from strings**, plus the **auto-`to_string` on
concatenation / interpolation** that the Phase-15 OOP model left a hook for (`to_string` resolved
by name through the class registry / vtable, dispatched via `OP_CALL_FUNC` — zero runtime
reflection).

Follows the proven Phase-13/14/15 pattern: a **single C runtime is the source of truth**
(`src/runtime/teko_convert.c`, KAT-tested in the Unity suite), lowered to native `teko_rt_*`
wrappers AND compiled into the WASM reactor — every reserved surface gets an executable `.tks`
proof on **both** native and WASM (no dead tokens).

## Owner-defined scope
1. **Conversions between types** — primitive ↔ primitive (int/float/bool/char), and to/from
   complex & user-defined types. Explicit and **checked** — fail loudly, no silent
   truncation/UB.
2. **Parsing to/from strings, WITH or WITHOUT formatting.**
   - **No format → ONE universal, culture-invariant default** that IGNORES the OS locale:
     `.`-decimal, no digit grouping, canonical integer/float/bool grammar — identical bytes on
     every machine/region. Deliberately distinct from Phase 14's `time.format_local` (which is
     OS-locale/DST-aware). The C runtime never calls `setlocale`, so the C library's default
     `"C"` locale formatting **is** the culture-invariant representation.
   - **Explicit format → the developer supplies a spec** (radix, precision, grouped digits,
     custom masks). Only then does output deviate from the universal default.
3. **Universal `to_string` on EVERY type**, auto-invoked when a value is concatenated with /
   interpolated into a string (`"x = " + p`, `"{p}"`). For user-defined types this dispatches to
   the Phase-15 `to_string` method (resolved by name → `OP_CALL_FUNC` for a concrete static
   receiver, vtable slot for an abstract/trait-typed one); when a type defines none, the compiler
   **synthesizes** the culture-invariant default (Go-style per-type generator over the fields,
   compile-time — never a reflective runtime walk). **This auto-call is the core deliverable.**

## Runtime id allocation (OP_CALL_RUNTIME)
Used ids today: 0–12, 15–34, 37–48. Phase 16 conversion block starts at **49** (contiguous):

| id | symbol                  | surface                  | signature                          |
|----|-------------------------|--------------------------|------------------------------------|
| 49 | `teko_rt_int_to_string` | `convert.int_to_str`     | (i32) → decimal str (16.A)         |
| 51 | `teko_rt_bool_to_string`| `convert.bool_to_str`    | (0/1) → `"true"`/`"false"` (16.A)  |
| 52 | `teko_rt_str_concat`    | `convert.str_concat`     | (a, b) → `ab` (16.A; also `+`/interp) |
| 53 | `teko_rt_parse_int`     | `convert.parse_int`      | (str) → i32, checked, fail-loud (16.F) |
| 55 | `teko_rt_parse_bool`    | `convert.parse_bool`     | (str) → 0/1, checked, fail-loud (16.F) |
| 56 | `teko_rt_to_radix`      | `convert.to_radix`       | (i32, radix 2..36) → str (16.E)    |
| 57 | `teko_rt_pad`           | `convert.pad`            | (i32, width) → zero-padded (16.E)  |
| 58 | `teko_rt_group`         | `convert.group`          | (i32) → thousands-grouped (16.E)   |

Value-carrying params are **i32** to match the accumulator/reactor ABI (`$w0` is i32 on WASM;
native truncates the 64-bit register); the full-range i64 *core* is exercised by the Unity KATs.
String args/results cross as NUL-terminated pointers through the shared linear memory on WASM,
exactly like the crypto/time surface. (Reserved-but-unallocated: **50** `float_to_string`, **54**
`parse_float`, and a float `…_fmt` — gated on a frontend float value model; see "Float formatting"
below.)

## Sub-blocks & dependency order
- **16.A — Conversion runtime foundation + primitive `to_string` (explicit-call). ✅ DONE & locally
  green on both targets.** `teko_convert.c` is the source of truth (6 Unity KATs: i64 incl.
  INT64_MIN, bool, str_concat, parse_i64 valid/invalid incl. overflow + grouping rejection,
  parse_bool — suite 223→229). Surface = the dotted-identifier builtins `convert.int_to_str` (id
  49), `convert.bool_to_str` (51), `convert.str_concat` (52, arity 2), lowering through
  OP_CALL_RUNTIME → native `teko_rt_*` + the WASM reactor. Executable proofs
  `runtime/{native,wasm}/samples/convert.tks` → `42 / 1000000 / true / false / "x = 42"`
  (byte-identical across targets, asserting NO digit grouping). ASan+UBSan both dispatch paths +
  16 goldens intact; crypto WASM proofs still green (additive reactor change).
  - **Scope notes (carried forward):** (a) **Float** formatting/parsing is genuinely large in
    freestanding C (shortest round-trip needs a Ryu/Grisu-class algorithm — the wasm32 shim has no
    snprintf/strtod), so it is its OWN later step rather than part of this bounded foundation.
    (b) The **parse** C core (`parse_i64`/`parse_bool`, checked, 1/0 + value-only-on-success) landed
    here as the source of truth + KATs, but its language *surface* is deferred to **16.F** (checked
    inter-type conversions) where fail-loud is the whole point — exposing a silent-failure single-
    value parse on the surface now would be the wrong shape. No dead tokens: only `convert.int_to_str`
    /`bool_to_str`/`str_concat` are surfaced, and all three have executable proofs.
- **16.B — String-concat + auto-`to_string` on `+` for primitives. ✅ DONE & locally green both
  targets (the core deliverable).** Value-type tracking in `frontend_interop.c`: the expression
  evaluators (`eval_primary`/`eval_expr_prec`) now return a value-type (`TEKO_VT_INT`/`VT_STR`);
  string literals and codec/convert/hash calls are `VT_STR` primaries; a `+` with a string operand
  lowers to `to_string` (id 49) on each int operand + `str_concat` (id 52), while all-int `+` and
  every other operator stay integer arithmetic. String-typed named locals are tracked (`g_localstr`,
  seeded from the init value-type, kept in sync on reassignment). Extern call arguments now accept
  expressions (`try_lower_call_arg_expr` — a parenthesized expr, or a primary followed by a binary
  operator — evaluated and spilled to a temp), wired into both the body and top-level call sites.
  Proofs `runtime/{native,wasm}/samples/concat.tks` → `x = 42 / sum = 50 / 42 items / count: 42 /
  n=42` (byte-identical). No new KATs (frontend lowering; the runtime is 16.A's). ASan+UBSan both
  paths + 16 goldens intact; class/traits/generics/eventbus/hello WASM proofs intact.
  - **Scope note:** the auto-converted operand is an *integer* today (id 49). Bool/char/float
    operands auto-convert once their value-types are tracked (bool via id 51 is a small follow-on;
    float waits on the float-formatting step). User-defined-type `to_string` dispatch in a concat is
    **16.D**. Concatenating a *codec/convert call result* directly (`convert.int_to_str(x) + "y"`)
    needs binding to a local first (`let s = …; "y" + s`) — the one-token-lookahead arg detector
    can't see the operator past the call's `)`; documented, not a dead token.
- **16.C — Interpolation auto-`to_string`. ✅ DONE & locally green both targets.** A double-quoted
  literal with a `{expr}` hole interpolates: `strlit_is_interp` flags a brace-bearing literal, and
  `lower_interp_string` builds the result by concatenating literal chunks with per-hole
  `to_string(expr)` — reusing 16.B's `str_concat` (id 52) + `to_string` (id 49). Each hole is
  re-lexed through a sub-`Parser` sharing the lowering `ctx` (so locals/temps resolve), so a hole
  can hold any expression (`"sum = {n + 8}"`). `{{`/`}}` are literal braces. Routed from
  `eval_primary`, the call-arg expression path (bare `emit("{n}")`), and `lower_init_value`
  (`let s = "[{n}]"`). Proofs `runtime/{native,wasm}/samples/interp.tks` → `x = 42 / 42 items, 42
  total / sum = 50 / count: 42 / braces { } kept / [42]` (byte-identical).
  - **Design decision (documented):** the interop frontend treats a brace-bearing double-quoted
    `"…"` as interpolated (matching the owner's `"{p}"` surface) — a lone `{` opens a hole, `{{`/`}}`
    escape a literal brace. The separate full-AST backtick interpolation subsystem
    (`parser_string.c`, `TOKEN_STRING_INTERPOLATED`) is unchanged; this is the interop-frontend
    lowering path. No existing sample has a brace-bearing string literal (no regression).
- **16.D — User-defined-type `to_string` in concat/interpolation. ✅ DONE & locally green both
  targets.** The value-type model gains `TEKO_VT_OBJ_BASE + class_index`; `eval_primary` returns it
  for a class-typed local. `coerce_to_string_in_w0(vt)` centralizes operand→string (int via id 49,
  string unchanged, object via `emit_object_to_string`), used by both the `+` concat branch and the
  interpolation holes. `emit_object_to_string` dispatches `class_method_idx(ci,"to_string")` via
  `OP_CALL_FUNC` (self=arg0, the Phase-15 hook) when the class defines/inherits one, else synthesizes
  the culture-invariant default `ClassName(field0, field1, …)` (each field rendered via the integer
  `to_string`). Proofs `runtime/{native,wasm}/samples/tostring.tks` → `temp is T=25 / [T=25] /
  point = Point(3, 4) / p=Point(3, 4)` (byte-identical).
  - **Scope note:** dispatch is via concrete static `OP_CALL_FUNC` (the common case + proof). A
    *trait-typed* (fat) reference used directly in a concat would dispatch via the runtime vtable
    (`vtable_get(tid_slot, methodid)`); that auto-coerce needs a `VT_TRAIT` encoding carrying the
    tid slot and is a small follow-on — explicit `g.to_string()` already works via the trait-dispatch
    path. Synthesized-default fields are rendered as integers (per-field typing is a follow-on).
- **16.E — Explicit-format spec. ✅ DONE & locally green both targets.** The developer-supplied
  (explicit) integer formats — distinct from the culture-invariant default, still locale-free:
  `convert.to_radix(v, base 2..36)` (id 56 — hex/oct/bin/…), `convert.pad(v, width)` (id 57 —
  zero-pad, sign counts toward width), `convert.group(v)` (id 58 — thousands grouping with `,`).
  `teko_convert.c` source of truth (3 new KATs: INT64_MIN radix, sign-aware padding, negative
  grouping). Native `teko_rt_to_radix/pad/group` + WASM reactor; the `$crypto_<id>` import loop
  bound moved 52→58. Composes with concat/interpolation (a format call is just an expression in a
  hole). Proofs `runtime/{native,wasm}/samples/format.tks` → `ff / 1010 / 100 / 00042 / 1,000,000 /
  "hex = ff"` (byte-identical). Suite 229→232.
  - **Scope note:** integer-format spec (the common explicit cases). Float precision and custom
    date masks ride on the float-formatting step / Phase-14 time surface respectively; a
    custom-grouping-separator variant is a trivial follow-on (`group` fixes `,`).
- **16.F — Checked string→primitive parse, fail-loud. ✅ DONE & locally green both targets.**
  `convert.parse_int` (id 53) / `convert.parse_bool` (id 55) lower to the CHECKED `teko_convert.c`
  core (KAT'd in 16.A). A malformed/overflowing input does NOT silently return 0 — it **fails
  loudly**: native prints a stderr diagnostic + exits 70 (`teko_rt_die`), the wasm32 reactor calls
  `__builtin_trap` (→ a `WebAssembly.RuntimeError`). The happy result is an int (`runtime_result_vt`
  types ids 53/55 as `VT_INT`, so `"n=" + convert.parse_int(s)` concatenates correctly via
  auto-`to_string`). Proofs: `parse.tks` → `n = 123 / neg = -42 / ws = 7 / true / false`
  (byte-identical); `parse_fail.tks` → emits `before` then aborts non-zero (native, `check_fail`) /
  traps (wasm, `run-parse.mjs` asserts the trap). Suite stays 232.
  - **Note on "conversions between types":** for the frontend's value model the meaningful runtime-
    checked conversion *is* string→number (overflow/format-checked here). int↔string is
    `to_string`/`parse`; bool→string is id 51; string→bool is `parse_bool`. There is no silent
    narrowing path to guard — the accumulator value model is uniform register-width.

## Float formatting — the remaining step, and why it is gated
The universal default for **floats** (`.`-decimal shortest round-trip) needs two things this phase
does NOT have: (1) a correct freestanding shortest-round-trip formatter (Ryu/Grisu-class — the
wasm32 reactor has no `snprintf`/`strtod`), and more fundamentally (2) **a float value model in the
frontend**. `frontend_interop.c`'s expression evaluator is integer-only (`$w0` is i32 on WASM /
a GPR on native; literals go through `atoi`); there is no way to carry an `f64` value, so a
`convert.float_to_str` token today would have **no float value to convert — a dead token**, which
the discipline forbids. Float formatting therefore belongs with a **numeric-types expansion**
(float literals → f64 values, float locals/arithmetic, f64 in the accumulator model); the
`teko_convert_f64_to_string` C core + KATs should land *with* that step so it can be exercised
end-to-end. This is the designated next step, not part of Phase 16's casting surface.

## Discipline (unchanged, non-negotiable)
One increment per commit; build + Unity suite; **ASan + UBSan on BOTH dispatch paths + TSan**
clean each commit; the **16 native emitter goldens never regress**; all four CI gates green
(incl. Windows MSVC — guard POSIX/LLP64) before any sub-block is "done"; patient CI watch (≥90s);
**no dead tokens** (executable `.tks` proof per surface, native + WASM); **no merge / force-push**
— the human merges.

## Status
**16.A–16.F DONE** — the full casting/conversion/parsing/`to_string` surface for every type the
frontend value model carries (int, bool, string, user-defined class), on **both targets**, no dead
tokens, each with an executable `.tks` proof native + WASM. The **core deliverable** (auto-`to_string`
on concatenation **and** interpolation, dispatching a user type's `to_string` or synthesizing the
culture-invariant default) is shipped. Suite 223→232 (9 new conversion KATs). The single remaining
owner item — **float** default/formatting — is gated on a frontend float value model (see above) and
is the designated next step. Ready to leave draft once all four gates are green on the final push.
