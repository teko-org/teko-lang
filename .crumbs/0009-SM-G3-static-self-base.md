---
seq: 0009
crumb-id: SM-G3
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: []
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:199-256"  # §5 self/base/static
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:894-913"  # §8 self↔DPS convergence
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1173-1174"# §10 Phase G — G3
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1341-1343"# §14 R2 base contextual
---

# 0009 · SM-G3 — `static` keyword + synthetic `self` receiver + `base` rename

> `static` kw + synthetic `self` receiver + `base` rename (accept both loose + synthetic).

## Goal

Instance-by-default (members may use `self`/`base`); `static` marks class-level members (no `self`);
`base` = parent. This crumb ADDS the `static` reserved keyword and the synthetic-`self` receiver
mechanism while KEEPING the old untyped-first-param receiver so the current seed's `src/` still parses
(additive window). The KEYSTONE: when `allow_receiver && !static`, the parser SYNTHESIZES a first
`Param { name = "self"; has_type = false }` and consumes NO receiver from source — preserving the entire
downstream invariant `params[0].has_type == false ⇔ instance`, so `is_static_method`, `is_instance`,
`method_sig_matches` keep working verbatim. `self` = the caller's instance mutated in place = the same
DPS/caller-arena discipline (§8) — a mutable receiver passed as a caller-arena pointer, exclusivity by
control flow. Byte-preserving (front-end rename; codegen byte-neutral); its seed folds into SM-R1. The
removal of the loose-receiver parse is SM-S3 (`0033`, M2).

## Where

- `src/lexer/lexer.tks:315-364` — add `static` as a RESERVED keyword (`TokenKind::Static`; no identifier
  collision). Keep `self`/`base` CONTEXTUAL (plain `Ident`; meaning only inside a method body). **Do NOT
  reserve `base`** — it is a LIVE production local name (`driver.tks:177`, `resolve.tks:947/1298/1708`,
  `zlib.tks:31`); reserving it breaks production (§14 R2, M.5 austerity).
- `src/parser/parse_decl.tks:291` — `parse_function` — accept optional `static`; add `is_static: bool` to
  `parser::Function`. When `allow_receiver && !static`, synthesize `Param{name="self"; has_type=false}`,
  consume no source receiver; when `static`, synthesize nothing.
- `src/parser/parse_decl.tks:34` — `parse_params` `allow_untyped_first` — KEPT (the additive window
  accepts BOTH old loose receiver AND synthetic `self`); removed in SM-S3.
- `src/checker/typer.tks:3110-3128` — the synthetic `let base: <Base> = <self upcast>` prepend — reused
  for `base`; unconditionally set `base_binding_name = "base"` when `has_base`.
- `src/checker/di.tks:147` — `is_static_method` — reads `m.is_static` (structural fallback kept in the
  window). `typer.tks:745` `is_instance`, `collect.tks:721` `method_sig_matches` — UNCHANGED (invariant
  preserved).
- `src/codegen/synth.tks:199/314/365/418/475` — emits methods — updated to emit `name="self"` +
  `is_static` directly (byte-neutral: `params[0]` still the untyped receiver with `type_ann` rewritten to
  `Named{struct_name}` before codegen).

## How

1. **Lexer:** reserve `static` (`TokenKind::Static`); grep-gate no production identifier `static`
   collision. Keep `self`/`base` contextual (§14 R2 — `base` stays a live local name; reserving it is
   unrelated churn that breaks production).
2. **Parser keystone:** accept optional `static` before a method; add `is_static: bool` to
   `parser::Function`. Synthesize the receiver:

```teko
/**
 * synth_self_param — the receiver keystone: for a NON-static method in a type body, synthesize a first
 * parameter `self` with `has_type = false` and consume NO receiver from source, so the downstream
 * invariant `params[0].has_type == false ⇔ instance method` holds verbatim (`is_static_method`,
 * `is_instance`, `method_sig_matches` keep working). A `static` method synthesizes nothing. During the
 * additive window the parser ALSO still accepts the old untyped-first-param loose receiver; SM-S3 later
 * removes that loose form.
 *
 * @param is_static      whether the method carries the `static` modifier
 * @param allow_receiver whether the enclosing context permits an instance receiver (a type body)
 * @return               the synthetic `self` param to prepend, or null for a static/free function
 * @since 0.3.1
 */
fn synth_self_param(is_static: bool, allow_receiver: bool): parser::Param | null
```

3. **Base rename:** `class Base(binding)` → `class Base`; unconditionally set `base_binding_name = "base"`
   when `has_base`. Reuse the existing synthetic `let base: <Base> = <self upcast>` prepend
   (`typer.tks:3110-3128`). `base` where `!has_base` → clear error.
4. **Checker (comes for free, §5.2):** define the synthetic `Param{name="self"}` into the method env
   exactly as today's convention `self`. New small diagnostics ONLY: reject a user param/local literally
   named `self`/`base` in a method. `is_static_method` reads `m.is_static` (structural fallback in the
   window). Generic-method/constraint dispatch (#254/#294) need ZERO change (synthetic `params[0]`
   preserved).
5. **Codegen (byte-neutral, §5.3):** `params[0]` is still the untyped receiver with `type_ann` rewritten
   to `Named{struct_name}` before codegen — the receiver spelling is invisible to the backend. Update
   `synth.tks` to emit `name="self"` + `is_static` directly. Skip adding `"self"` to `cg_is_c_keyword`
   (C has no `self` keyword).
6. **`self`↔DPS convergence (§8):** no separate lowering — the receiver rides `params[0]` as a pointer
   into the caller's current region (`region_current_vreg`), exactly as `alloc_call_dest` reserves the
   return destination. Note the shared discipline; no new codegen.
7. **Confirm byte-neutrality.** Front-end rename; codegen emits the same bytes. `src/` still uses the
   loose receiver, so a `[dry]` build is byte-identical.

## Rulings & laws

- **Teko-only:** lexer/parser/checker/codegen `.tks`; no C twin.
- **§14 R2 (law-first, M.5 austerity):** `base` stays CONTEXTUAL, NOT reserved — reserving it breaks live
  production locals (`driver.tks:177`, `resolve.tks`, `zlib.tks`). This softens the owner's "reserved" to
  "reserved-in-method-body", resolved law-first, no HALT.
- **W15:** full Javadoc on `synth_self_param` and helpers; no `//`.
- **NÃO DETECTAR O QUE NÃO EXISTE:** the new diagnostics reject only what the surface DOES produce (a
  user param named `self`/`base` in a method, `base` with no base) — no branch for impossible cases.
- **Additive window:** both loose receiver and synthetic `self` accepted; the loose-receiver removal +
  `allow_untyped_first` deletion is SM-S3 (`0033`).
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step.

## Fixtures

The self-build still uses the loose receiver (not yet swept), so the synthetic-`self`/`static`/`base`
accept paths and the new rejects are NOT self-exercised — isolated fixtures required:

| fixture | asserts | expected |
|---|---|---|
| `self_keyword_receiver` | `fn f(): i64 { self.x }` types; no loose receiver param | 0 |
| `base_call_in_override` | `base.g()` in a subclass method resolves | 0 |
| `base_still_a_local_name` | a production `base` local (non-method) still compiles (contextual) | 0 |
| `static_modifier_factory` | `static fn make(): C { }` has 0 params, `is_static` | 0 |
| `self_param_rejected` | a user param named `self` in a method is rejected | EXPECT_COMPILE_FAIL |

## Gate

`[dry]` — compile + the five fixtures + trivial fixpoint (bytes unchanged; `src/` still loose-receiver).
"Green" = `static`/synthetic-`self`/`base` all parse and check, `base` stays a valid local name, the
`self`-param reject fires, `[dry]` build byte-identical. Reseed-class: `(folds R1)`.

## Deps

`—`

## Done when

`static` is reserved, the synthetic-`self` receiver preserves `params[0].has_type == false ⇔ instance`,
`base` is renamed (contextual, still a live local), the accept fixtures are exit `0`, `self_param_rejected`
is `EXPECT_COMPILE_FAIL`, and a `[dry]` build is byte-identical.
