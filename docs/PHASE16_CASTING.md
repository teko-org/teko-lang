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

| id | symbol                       | signature                         |
|----|------------------------------|-----------------------------------|
| 49 | `teko_rt_int_to_string`      | (i64 as str) → decimal str        |
| 50 | `teko_rt_float_to_string`    | (f64 as str) → canonical decimal  |
| 51 | `teko_rt_bool_to_string`     | (0/1 as str) → `"true"`/`"false"` |
| 52 | `teko_rt_str_concat`         | (a, b) → `ab`                     |
| 53 | `teko_rt_parse_int`          | (str) → i64 (checked)             |
| 54 | `teko_rt_parse_float`        | (str) → f64 (checked)             |
| 55 | `teko_rt_parse_bool`         | (str) → 0/1 (checked)             |
| 56 | `teko_rt_int_to_string_radix`| (i64, radix) → str (explicit fmt) |
| 57 | `teko_rt_float_to_string_fmt`| (f64, spec) → str (explicit fmt)  |

(ids extended as sub-blocks land; the table here is the contract.) All string args/results cross
as NUL-terminated pointers through the shared linear memory on WASM, exactly like the crypto/time
surface; integer/float values are marshalled as their canonical string form into the same ABI
(keeps the uniform pointer-passing convention — the runtime parses/formats).

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
- **16.D — User-defined-type `to_string` in concat/interpolation.** Concrete receiver →
  `OP_CALL_FUNC` to the `to_string` slot; abstract/trait receiver → vtable slot. When absent,
  synthesize the culture-invariant default (per-type field-walk generator). **MEDIUM** (reuses the
  Phase-15 method/vtable resolution; the hook is already in place).
- **16.E — Explicit-format spec.** Radix (hex/oct/bin), float precision, grouped digits, custom
  masks via a format string (ids 56/57+). **BOUNDED–MEDIUM.**
- **16.F — Checked inter-type conversions.** Primitive casts that fail loudly (narrowing range
  checks, float→int truncation policy), complex/user-defined casts. **MEDIUM.**

## Discipline (unchanged, non-negotiable)
One increment per commit; build + Unity suite; **ASan + UBSan on BOTH dispatch paths + TSan**
clean each commit; the **16 native emitter goldens never regress**; all four CI gates green
(incl. Windows MSVC — guard POSIX/LLP64) before any sub-block is "done"; patient CI watch (≥90s);
**no dead tokens** (executable `.tks` proof per surface, native + WASM); **no merge / force-push**
— the human merges.
