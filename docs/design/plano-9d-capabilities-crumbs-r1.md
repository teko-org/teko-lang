# §9.D — Reseed 1: the crumb sequence for the THREE inline-union capabilities (implementer-ready)

> **Status:** DESIGN. Read-only on product code — NO `.tks` edited, NO build, NO reseed, NO `teko test`
> in any form (the `monomorph` leak crashes the container). This document IS the artefact; the only
> commit of this crumb is itself. Isolated worktree off `origin/fix/retirement` (the main checkout and
> the reseed worktrees are edited by other agents — UNTOUCHED). Branch `design/9d-capabilities-crumbs`.
>
> **What this doc is.** It transforms the design of `plano-9d-capabilities-inline-uniao-v1.md` §1–§2 into
> an ORDERED, gate-able crumb sequence for **Reseed 1** — the three additive compiler capabilities
> (a) grouped-type parser, (b) union-mangle byte-agreement, (c) anon group-bind pattern — each with the
> Teko/C shapes to add, the existing fns touched, the regression fixtures (input → native exit code), and
> the ritual point. Every `file:line` below was RE-VERIFIED on this worktree; §1 lists the drift I found
> vs the plan's cited anchors.
>
> **Scope fence (binding).** ONLY the three capabilities. The **~2000-site SWEEP is OUT OF SCOPE** — it
> is blocked on §14's `@Type()` (recursive ADTs cannot be inlined by hand; `plano-9d…-v1.md` §4) and is
> sequenced AFTER §14. This doc designs nothing that needs `@Type()`. The three capabilities are additive
> and **inert on today's corpus** (zero `(A|B)` grouped types, zero `[](…|…)`, zero `(…|…) as v` exist),
> so they combine into ONE additive, byte-identical reseed (R1).
>
> **Sealed law drawn around (not reopened):** §9.2b/§9.D — `type X = variant` BANNED, `type X = A | B`
> named-union BANNED; the `|` union survives ONLY in var/param/return/field/constraint position
> (`docs/design/mudancas-superficie-0.3.1.md`). Owner ruling: inline-por-extenso, segmented capability-PRs,
> combine where inert. Teko-only (the C twins in `teko_rt.*` stay frozen; only the Teko-side
> parse/mangle/match gain arms), W15/Javadoc (every snippet below is copy-ready full-Javadoc).

---

## 1. Anchor re-verification (drift found vs `plano-9d-capabilities-inline-uniao-v1.md`)

I read each cited site on `origin/fix/retirement`. Corrected anchors (use THESE):

| capability | symbol | plan said | ACTUAL (verified) |
|---|---|---|---|
| (a) | `parse_type_primary` | `parse_type.tks:148` | `parse_type.tks:148` ✓ — insertion point is the `head` if/else chain, between the `LBracket`→`parse_slice` arm (`:160-161`) and the `is_delegate_kw_at` arm (`:162`) |
| (b) | `cg_opt_mangle` | `codegen.tks:1809` | `:1809` ✓ — tail is `_ => error "codegen: optional/slice inner type not yet supported"` (`:1835`) |
| (b) | `cg_opt_mangle_str` | `:1845` | `:1845` ✓ (str twin, tail error `:1868`) |
| (b) | `cg_variant_typename` | `:1963` | `:1963` ✓ — emits `tk_u` + per-member `_<key>` in **SOURCE ORDER** (`:1966-1971`); does NOT itself normalize |
| (b) | `cg_variant_has_null` | `:1983` | `:1983` ✓ |
| (b) | `cg_union_normalize_null` | `~1994` | **`:2003`** (drift −9) — the ONE canonicalizer; **applied at `cg_te_to_type_ns:10771`** (`checker::Variant { members = cg_union_normalize_null(mem) }`) |
| (b) | `cg_opt_mangle_texpr_str` | `:2508` | `:2508` ✓ — tail `_ => error "codegen: optional inner type-expr not yet supported"` (`:2512`); sig `(te)` only, **no `prog`/`ns`** |
| (b) | `cg_opt_mangle_texpr` (buffer twin) | — (unnamed) | **`:2673`** — sig `(buf, te)` only, no `prog`/`ns` |
| (b) | `cg_member_key_texpr_str` | `:2518` | `:2518` ✓ |
| (b) | `cg_texpr_union_has_null` | `:2531` | `:2531` ✓ |
| (b) | `cg_variant_typename_texpr` | (idiom) | **`:2548`** — emits `tk_u` + keys SOURCE ORDER from a raw `parser::UnionType` |
| (b) | `cg_te_to_type_ns` | `:11050` | DEF **`:10734`**; the union-resolution USE the plan meant is **`:11049`** (`cg_te_to_type_ns(prog, owner_ns, UnionType{…})`) and **`:5373`** |
| (b) | **top-level union emit** | (not cited) | **`emit_type_expr:2593`, `UnionType` arm `:2652-2663`** — see §2.B, the decisive finding |
| (c) | `Pattern` union | `pattern.tks:32` | `:32` ✓ (`LiteralPattern \| RangePattern \| AltPattern \| BindPattern \| FieldPattern \| WildcardPattern \| NullPattern`) |
| (c) | `AltPattern` shape | — | `pattern.tks:8` (`struct { options: []Pattern }`) |
| (c) | pattern parse entry | — | `parse_pattern.tks`: `parse_pattern_primary:7` (a `(` there falls to `err_at "expected a pattern":85` today → inert), `parse_pattern:103` (the `\|` alt level) |
| (c) | `check_pattern` | `match.tks:230` | **`:272`** (drift +42) |
| (c) | `AltPattern` "cannot bind" gate | `:383` | `:383-384` ✓ (and the twin in `check_enum_pattern` at `:35`) |
| (c) | `arm_case_names` | `:428` | `:428` ✓ |
| (c) | `variant_covered` | `:435/:649` | **`:523`** (single def; `some_arm_names:528`, `some_arm_is_null:529`) |
| (c) | `is_direct_case_of` | `:66` | `:66` ✓ |

**Decisive finding (strengthens capability b, Option 1).** The plan's recommended "resolve-then-mangle"
is NOT new plumbing — it is the EXACT idiom the top-level `UnionType` arm of `emit_type_expr` already
runs at `codegen.tks:2652-2663`: a **null-FREE** union takes the source-order fast path
(`cg_variant_typename_texpr`, `:2653`); a **null-BEARING** union routes through `cg_te_to_type_ns(prog,
ref_ns, te)` (`:2654`) to obtain the already-null-normalized `checker::Variant` and then emits from it.
Capability (b) is simply extending that SAME two-branch idiom from the top-level position to the
slice/optional INNER position, which today dead-ends at `_ => error`. This makes byte-agreement a
COROLLARY (one canonicalizer, `cg_union_normalize_null:2003`, applied once at build time), not a promise.

---

## 2. The crumb sequence (ordered; each independently compiles/self-hosts; full gate at R1)

Dependency order: **A1** (parser, standalone) ‖ **B1 → B2** (resolved mangle before syntactic) ‖
**C1 → C2** (parser node before checker typing). The three tracks are mutually independent; within B and
C the second crumb needs the first. All five land in ONE reseed (R1) because all are inert on the seed.

### Crumb A1 — parser GROUPED union `(A | B)` / `[](A | B)` (capability a)

**Goal.** Let a `|` union sit in a type-PRIMARY position (slice element, generic arg, wider-union member):
`[](A | B)`, `List<(A | B)>`, `(A | B) | C`. A single-member group `(A)` collapses to `A`.

**Where.** `src/parser/parse_type.tks`. Add `parse_grouped_type` beside `parse_slice`; add ONE `else if`
arm to the `head` chain of `parse_type_primary` (`:148`), **between** the `LBracket`→`parse_slice` arm
(`:160-161`) and the `is_delegate_kw_at` arm (`:162`). The arm yields a `ParsedType` exactly like its
siblings (shape `ParsedType { node; next; pending_gt }`, per `:159`/`:176`).

```teko
/**
 * parse_grouped_type — a PARENTHESIZED type group `(A | B | …)`, the only spelling that places a `|`
 * union in a type-PRIMARY position (a slice element `[](A | B)`, a generic argument `List<(A | B)>`, or
 * a member of a wider union `(A | B) | C`). `pos` is at the `(` (the caller matched `LParen`). The body
 * is a FULL union-level type (`parse_type`), so an inner `|` binds inside the parens exactly as a
 * top-level union does; a single-member group `(A)` collapses to `A` (no needless `UnionType`, mirroring
 * `parse_type`'s M.5 one-member rule). A pending compound-`>` close (a nested generic mid-close) cannot
 * be followed by `)`, so it is a parse error here rather than a silently swallowed token.
 *
 * The type-PRIMARY twin of the pattern-side grouped bind `(B | C) as v` (capability c): the two grouped
 * spellings — one in a type position, one in a pattern position — are what let an inlined anonymous union
 * nest inside a slice and be bound as a whole after §9.D retires the named ADTs. Inert on today's corpus:
 * a `(` in a type position is presently ALWAYS an unexpected-token error (the delegate rework retired
 * `(A, B): R`), so this arm cannot alter any accepted program.
 *
 * @param tokens  the token stream
 * @param pos     the index of the opening `(`
 * @return        the parsed inner union (or its sole member) as a primary, consuming through the `)`
 * @throws        when the group is not closed by `)`, or a nested generic `>>` close reaches the `)`
 * @since         §9.D capability (a)
 */
fn parse_grouped_type(tokens: []lexer::Token, pos: u64): ParsedType | error {
    var inner = match parse_type(tokens, pos + 1) { ParsedType as x => x; error as e => return e }
    if inner.pending_gt > 0 {
        return err_at(tokens, inner.next, "a grouped type '(A | B)' cannot close inside a generic's '>>' — parenthesize the inner type separately")
    }
    var q = skip_seps(tokens, inner.next)
    if !is_kind_at(tokens, q, lexer::TokenKind::RParen) {
        return err_at(tokens, q, "expected ')' to close a grouped type '(A | B)'")
    }
    ParsedType { node = inner.node; next = q + 1; pending_gt = 0 }
}
```

The one arm added to `parse_type_primary`'s `head` chain (after the `LBracket` arm at `:161`):

```teko
    } else if is_kind_at(tokens, pos, lexer::TokenKind::LParen) {
        match parse_grouped_type(tokens, pos) { ParsedType as x => x; error as e => return e }
```

**Existing fns touched:** `parse_type_primary` (`:148`, one new `else-if` arm) — nothing else.
`parse_type` / `parse_slice` / `skip_seps` / `err_at` / `is_kind_at` reused unchanged.
**Inertness:** a `(` in type position is always an error today → the arm changes no accepted program.
**Gate-able alone:** self-hosts (the plan reports the implementer already validated this branch,
3572 MB); it accepts new syntax the corpus never writes.

### Crumb B1 — RESOLVED-side variant mangle arm (capability b, half 1 — the prerequisite)

**Goal.** Make a resolved `checker::Variant` mangle as an INNER suffix `u_<keys>` (so `Slice{Variant}` →
`tk_slice_u_<keys>`), the resolved half both paths converge on. Today `cg_opt_mangle` (`:1809`) and its
str twin (`:1845`) dead-end at `_ => error` for a `Variant` inner.

**Where.** `src/codegen/codegen.tks`. Add a `checker::Variant` arm to `cg_opt_mangle` (`:1809`) and to
`cg_opt_mangle_str` (`:1845`). The members reaching here are ALREADY null-first normalized at build
(`cg_te_to_type_ns:10771`; the checker's own `union_normalize_null` for checker-resolved types), so
SOURCE-ORDER emission is byte-correct and matches `cg_variant_typename` (`:1963`), which also emits
source-order over already-normalized members. Reuse `cg_member_key` (`:1947`).

```teko
/**
 * cg_opt_mangle — … existing arms (Prim/Byte/Char/Str/Error/Null/Named/Slice) … PLUS a nested anonymous
 * union inner, which mangles to the INNER suffix `u_<member keys>` — the same key sequence
 * `cg_variant_typename` (:1963) emits for a top-level inline `A | B` minus the `tk_` prefix — so a
 * `[](A | B)` slice typedef is `tk_slice_u_<keys>` and every use agrees with the collected decl. The
 * members are already null-first normalized at build (cg_te_to_type_ns:10771), so source-order emission
 * is byte-identical to the resolved typedef; there is no second normalization to drift.
 *
 * @param buf    the C-symbol byte buffer to append to
 * @param inner  the resolved inner type (now including a `Variant`)
 * @return       the extended buffer
 * @throws       when a member's key cannot be mangled (untyped empty slice/optional element)
 * @since        §9.D capability (b)
 */
//   checker::Variant as va => {
//       var out = cb(buf, "u")
//       var i = 0
//       loop {
//           if i >= va.members.len { break }
//           out = cb(out, "_")
//           out = match cg_member_key(out, va.members[i]) { []byte as o => o; error as e => return e }
//           i++
//       }
//       out
//   }
```

The `cg_opt_mangle_str` (`:1845`) twin arm composes the same suffix with `teko::str::concat` (mirroring
its existing `Slice` arm at `:1866`), calling `cg_opt_mangle_str` per member.

**Existing fns touched:** `cg_opt_mangle` (`:1809`, add `Variant` arm), `cg_opt_mangle_str` (`:1845`,
twin arm). `cg_member_key` (`:1947`), `cg_variant_typename` (`:1963`) reused unchanged. No engine change:
tag numbering (`store_variant_tag`) and rep split (`cg_variant_has_null:1983`) untouched.
**Inertness:** fires only on a `Variant` inner of a slice/optional, which the corpus lacks → inert.

### Crumb B2 — SYNTACTIC-side slice/optional-of-union, via resolution (capability b, half 2)

**Goal.** `[](A | B)` in a signature/field position must emit the IDENTICAL `tk_slice_u_<keys>`. Today
`emit_type_expr`'s `SliceType` arm (`:2649`) calls `cg_opt_mangle_texpr(cb(buf,"tk_slice_"), st.element)`
which, for a `UnionType` element, dead-ends at `cg_opt_mangle_texpr:2673`'s `_ => error` (the exact "…not
yet supported" the plan's fixture 4 targets).

**Where — recommended (no threading).** Fix it IN `emit_type_expr` (`:2593`), which already holds
`prog` and `ref_ns` (it calls `cg_te_to_type_ns(prog, ref_ns, te)` at `:2654`). Change the `SliceType`
arm (`:2649`) so a `UnionType` element routes through resolution and reuses the B1 resolved path
(`cg_slice_typename:1791` → `cg_opt_mangle:1809`+B1 arm), exactly mirroring the top-level `UnionType`
idiom at `:2652-2663`:

```teko
    // SLICE `[]T`. A plain element keeps the fast texpr suffix; a UNION element (`[](A | B)`) resolves
    // to the null-normalized checker::Variant and reuses the resolved slice typedef (cg_slice_typename →
    // cg_opt_mangle's §9.D Variant arm), so a []union USE names the same tk_slice_u_<keys> the DECL
    // collected. One canonicalizer (cg_union_normalize_null:2003) → byte-agreement by construction.
    parser::SliceType as st => match st.element {
        parser::UnionType => match cg_te_to_type_ns(prog, ref_ns, st) {
            checker::Slice as sl => cg_slice_typename(buf, sl.element)
            _ => cg_opt_mangle_texpr(cb(buf, "tk_slice_"), st.element)   // unreachable; honest fallback
        }
        _ => cg_opt_mangle_texpr(cb(buf, "tk_slice_"), st.element)
    }
```

**Existing fns touched:** `emit_type_expr` (`:2593`, `SliceType` arm at `:2649`). `cg_te_to_type_ns`
(`:10734`), `cg_slice_typename` (`:1791`), `cg_opt_mangle` (`:1809`, via B1) reused. NO signature change,
NO threading — this is the minimal, byte-safe locus for R1 and the fixtures 4/5 (which exercise `[](A|B)`
in signature/field position). **Optional`?(A|B)` note:** the optional-inner arm (the `cg_opt_mangle`
niche/box rail, `:1663`/`:1683`) already resolves via the checker `Type`; B1's `Variant` arm covers it.

**Deferred (flag, do NOT action at R1): the `_str` member-key callers.** `cg_opt_mangle_texpr_str`
(`:2508`) and `cg_member_key_texpr_str` (`:2518`) take `(te)` with no `prog`/`ns`; their callers are
`:2511`/`:2519` (self-recursion), `:5612`, `:6873`, `:10432`, `:10455`. These fire only when a
slice-of-union is itself a MEMBER of ANOTHER union (nested `A | [](B|C)`), which no R1 fixture and no
current corpus site produces. Fixing them needs EITHER threading `(prog, ref_ns)` through those two fns
(and confirming each of the four call sites holds a resolution context — `:5612`/`:10432`/`:10455` are
in typedef-collection loops that carry `prog`; `:6873` is in a member-emit path — verify at implement
time) OR mirroring `:2653`'s null-free/`:2654` null-bearing split locally. **Recommendation:** leave the
`_str` path on its honest `_ => error` for R1; open it only when the sweep produces the nested shape.
This is a scope note, not a tension — R1's capability is "`[](A|B)` in a type position", which B2 delivers.

### Crumb C1 — parser GROUP-BIND pattern `(B | C) as v` (capability c, half 1)

**Goal.** A parenthesized anonymous-union case that BINDS as a whole. Its bound type is the anonymous
sub-union over its options. Distinct from `AltPattern` (`pattern.tks:8`, value-axis alternatives that by
the settled axis rule cannot bind — `check_pattern:383`); the parens + `as v` are the discriminator, so a
bare `b | c` is untouched.

**Where.** `src/parser/pattern.tks`: add the node and put it in the `Pattern` union (`:32`).
`src/parser/parse_pattern.tks`: add a `LParen` branch to `parse_pattern_primary` (`:7`). A `(` in pattern
position today falls straight to `err_at "expected a pattern"` (`:85`) → inert. The branch parses
`( <primary> ( | <primary> )* ) as <name>`; the `as <name>` is REQUIRED (a parenthesized group without a
binder is not a supported form → keep the bare-alt semantics — reject with a guiding message).

```teko
/**
 * GroupBindPattern — a parenthesized anonymous-union case that BINDS as a whole: `(B | C | …) as v`. It
 * names no single nominal case; its bound type is the anonymous `Variant` over its options, a SUB-UNION
 * of the subject. The pattern-side twin of the type-side grouped union `(A | B)` (capability a): the only
 * spelling that matches AND binds a nested anonymous union member left behind when §9.D inlines a
 * union-of-unions (the `Statement` group inside `ItemKind`, the `TStatement` group inside `TItem`).
 * Distinct from `AltPattern` (pattern.tks:8, value-axis alternatives that never bind): the parens plus
 * the `as v` are its discriminator, so a bare `b | c` alternative is untouched.
 *
 * @field options  the anonymous sub-union's member patterns, source order (each a bare case name)
 * @field binding  the bound identifier `v`
 * @since §9.D capability (c)
 */
pub type GroupBindPattern = struct { options: []Pattern; binding: str }

// The Pattern union (pattern.tks:32) gains the new node:
//   pub type Pattern = variant LiteralPattern | RangePattern | AltPattern | GroupBindPattern
//                    | BindPattern | FieldPattern | WildcardPattern | NullPattern
```

The `LParen` branch in `parse_pattern_primary` (add near `:39`, the `LBracket` slice branch, same shape):
parse a `(`; loop `parse_pattern_primary` separated by `Pipe`; require `RParen`; require `As` + a name
(`is_name_at`, since a binding name may be a contextual keyword, as at `:50`/`:74`); build
`GroupBindPattern { options; binding }`. A `(…)` NOT followed by `as <name>` →
`err_at(… , "a parenthesized group must bind: write '(B | C) as v'")`.

**Existing fns touched (not recreated):** `pattern.tks` (add `GroupBindPattern`, extend the `Pattern`
union at `:32`); `parse_pattern.tks` (`parse_pattern_primary:7`, one `LParen` branch). `parse_pattern`
(`:103`, the alt level) is unchanged — a `GroupBindPattern` is a single primary, so it composes as one
alt option too. **Inertness:** a `(` in pattern position is always an error today → inert.

### Crumb C2 — checker TYPING + exhaustiveness for the group bind (capability c, half 2)

**Goal.** Type/bind `(B | C) as v` to the anonymous sub-`Variant`, and make exhaustiveness count the
group's members together. The discriminant stays the SUBJECT tag; no tag/exhaustiveness ENGINE change.

**Where.** `src/checker/match.tks`. Add `group_bind_subunion` (below), a `GroupBindPattern` arm to
`check_pattern` (`:272`), and teach `arm_case_names` (`:428`) + `variant_covered` (`:523`) that a group
contributes ALL its option names.

```teko
/**
 * group_bind_subunion — resolve a `GroupBindPattern`'s bound TYPE: the anonymous `Variant` over the
 * group's option case-names, as a SUB-UNION of the match subject. [CORRECTED 2026-08-14 — NESTED, per
 * the shipped impl `fd750b33`; the earlier "flat / direct case" phrasing was self-contradictory.] The
 * group's options collectively identify ONE NESTED union member `(… | …)` the subject already carries at
 * a tag slot, matched by TYPE-EQUALITY of that nested `Variant` member (NOT each option as a separate
 * direct case of the subject — the flat reading is retired; `resolve_type` keeps nested unions nested).
 * The result reuses the nested member's own resolved types in SOURCE ORDER, so the bound `v` is
 * byte-identical to the boxed nested variant at that tag slot — no re-tag, no re-box (§9.D nested-group
 * byte-identity).
 *
 * @param gp        the grouped bind pattern
 * @param subject   the match subject's resolved type
 * @param table     the program type table
 * @param ref_ns    the match arm's referencing namespace
 * @return          the anonymous sub-union `Variant` bound to the pattern's identifier
 * @throws          when the group's options do not match one nested union member of the subject
 * @since           §9.D capability (c)
 */
fn group_bind_subunion(gp: GroupBindPattern, subject: Type, table: TypeTable, ref_ns: str): Type | error
```

- **`check_pattern` (`:272`) — new `GroupBindPattern` arm:** call `group_bind_subunion` to get the bound
  `Variant`; bind `gp.binding` to it in the returned `Env` (the same way the `BindPattern` arm binds `as
  x`). Reuse the `AltPattern` "cannot bind" gate UNCHANGED (`:383`) — a bare alt still errors; only the
  parenthesized+`as` group binds.
- **`arm_case_names` (`:428`) — a `GroupBindPattern` contributes ALL its option names** (so a sibling arm
  cannot also claim one of them without an overlap error), mirroring the existing `AltPattern` case
  (`:435`).
- **`variant_covered` (`:523`) — unchanged engine; a group's members are covered together:** since
  `variant_covered` already asks, per subject member, whether `some_arm_names` it (`:528`), a
  `GroupBindPattern` listing `B` and `C` covers exactly `B` and `C` once `arm_case_names` reports them.
  A subject `A | (B | C)` whose only non-`_` arm is `(B | C) as v` therefore leaves `A` UNCOVERED →
  correctly REJECTED (fixture 7).

**Existing fns touched (not recreated):** `match.tks` — `check_pattern:272` (new arm), `arm_case_names:428`
(group contributes its option set), plus reuse of `is_direct_case_of:66`, `some_arm_names:528`. No change
to `variant_covered`'s loop or to the tag/box lowering — the discriminant remains the subject tag.
**RULING (from the plan, confirmed byte-identical):** `ItemKind`/`TItem` use this NESTED group-match, NOT
flatten — flatten renumbers tags and forces a `.tkb` wire bump; the nested form keeps the tag layout and
the wire stable. This is the byte-identical option the owner tends toward. No HALT.

---

## 3. Type signatures / function shapes summary (what the implementer adds, and what they touch)

**New declarations (Teko, full-Javadoc above):**
- `parse_grouped_type(tokens, pos): ParsedType | error` — `src/parser/parse_type.tks` (Crumb A1).
- `GroupBindPattern = struct { options: []Pattern; binding: str }` — `src/parser/pattern.tks` (C1).
- `group_bind_subunion(gp, subject, table, ref_ns): Type | error` — `src/checker/match.tks` (C2).
- (b) two mangle ARMS (not new fns): a `checker::Variant` arm in `cg_opt_mangle` (`:1809`) and
  `cg_opt_mangle_str` (`:1845`).

**Existing fns touched (extend, do NOT recreate):**
- `parse_type_primary` (`parse_type.tks:148`) — one `LParen` `else-if` arm.
- `parse_pattern_primary` (`parse_pattern.tks:7`) — one `LParen` branch; `Pattern` union (`pattern.tks:32`).
- `emit_type_expr` (`codegen.tks:2593`) — `SliceType` arm (`:2649`) routes a union element through
  `cg_te_to_type_ns` + `cg_slice_typename`.
- `check_pattern` (`match.tks:272`) + `arm_case_names` (`:428`) — a `GroupBindPattern` arm / option set.

---

## 4. Regression fixtures (input → expected native exit code; ACCEPT/REJECT)

Run-fixtures under `examples/regressions/own_native` (exit = `main`'s observable value); reject-fixtures
under an `EXPECT_COMPILE_FAIL` sentinel dir with a `.tkr` diagnostic scenario (format:
`examples/regressions/block_expr_reject/`). All must pass AT R1 with the corpus still on `variant`.

| # | capability | fixture | shape | expected |
|---|---|---|---|---|
| 1 | a | `grouped_type_slice` | `var xs: [](Circ \| Sq) = …; match xs[0] { Circ as c => 1; Sq as q => 2 }` | exit `1` vs `2` per arm |
| 2 | a | `grouped_type_nested` | `(A \| B) \| C` and `List<(A \| B)>` parse; a value of each flows | distinct nonzero exit proves the value crossed |
| 3 | a | `grouped_single_collapse` | `(A)` types exactly as `A` (no `UnionType`); both spellings in one program | SAME exit |
| 4 | b | `slice_union_byteagree` | a `[](A \| B)` built in fn F, consumed through a signature in fn G (USE typedef name == DECL typedef name) | nonzero exit (value crossed the seam) — the fixture that catches the old "…not yet supported" |
| 5 | b | `union_null_order_mangle` | `[](X \| null)` and `[](null \| X)` mangle to the SAME typedef (both resolve-normalize) | both compile + interoperate; equal exit |
| 6 | c | `grouped_bind` | subject with a nested union member; `(B \| C) as v => use(v)` binds the whole sub-union | exit distinguishes the group arm from a sibling arm |
| 7 | c | `grouped_bind_exhaustive` (REJECT) | `match … { (B \| C) as v => … }` over subject `A \| (B \| C)`, `A` uncovered | REJECT: `variant_covered` counts `A` uncovered |
| 8 | c | `bare_alt_still_rejects` (REJECT) | `B \| C as v` (no parens) | REJECT: "an alternative (`\|`) cannot bind; use a separate arm" — proves (c) did not loosen the bare-alt rule |

Fixtures 1–3 gate A1; 4–5 gate B1+B2; 6–8 gate C1+C2. Each `exit`/diagnostic token encodes WHICH branch
ran (axis-law: assert the value, never an incidental effect).

---

## 5. Ritual point (full gate)

**R1 — the single ritual for this doc.** After all five crumbs land (the three capabilities, `variant`
still alive), reseed and run fixtures 1–8. The gate is **self-reproduce byte-identical** (bin-a == bin-b:
the capabilities are inert on the seed, so the fixpoint closes trivially) PLUS a green surface. NO fixture
may require the new shapes to exist in the SEED (seed-safe: the corpus uses none of them). Each crumb is
individually compile-able/self-hosting and may be reviewed alone, but the FULL gate is R1.

Downstream (OUT OF SCOPE here, listed for continuity): §14 `@Type()` rituals (per
`section14-macro-comptime-impl-v1.md`); then R2…Rn, the leaves→roots SWEEP, each its own gate — all
BLOCKED on §14 and NOT part of this crumb.

---

## 6. Risks + law tensions (HALT check)

1. **[resolved, corollary] (b) byte-agreement.** ONE canonicalizer (`cg_union_normalize_null:2003`,
   applied at `cg_te_to_type_ns:10771`); both paths converge on the resolved `Variant` via the idiom
   already at `emit_type_expr:2652-2663`. No second normalization to drift. **No HALT.**
2. **[scope note, not a tension] (b) `_str` member-key path.** `cg_opt_mangle_texpr_str:2508` /
   `cg_member_key_texpr_str:2518` lack a `(prog, ns)` seam; they fire only for a slice-of-union nested as
   a MEMBER of another union — absent from R1 fixtures and today's corpus. Left on honest `_ => error` for
   R1; threaded (verify call sites `:5612`/`:6873`/`:10432`/`:10455` hold `prog`) only when the sweep
   produces the nested shape. Cost/deferral, not a law tension.
3. **[resolved, ruling] (c) `ItemKind`/`TItem` nested vs flatten.** Nested group-match (byte-identical,
   wire-stable) chosen; flatten rejected (renumbers tags, forces a `.tkb` bump). Owner tends
   byte-identical. **No HALT.**
4. **[out of scope, relay] §14 TYPE-position splice OPEN** (`section14 §5.1`). The SWEEP needs `@Type()`
   to yield a `TypeExpr`; a §14 decision, not a §9.D capability. Flagged for the §14 owner; the three R1
   capabilities do not depend on it. **Not a §9.D tension.**
5. **[verify at implement] enum-pure / sweep concerns** (arm-name reuse, `.tkb` stability) belong to the
   SWEEP (R2…Rn), out of scope here.

**No genuine law tension survives → NO HALT.** The three capabilities pass every standing law: Teko-only
(grouped union, group-bind pattern, mangle arms are all Teko-side; the C twins in `teko_rt.*` stay
frozen); W15/Javadoc (all snippets full-Javadoc, copy-ready); law-first (byte-agreement resolves by the
single-canonicalizer finding; the `ItemKind`/`TItem` question resolves by byte-identity); seed-safe
(all inert → one additive reseed R1). The only item RELAYED up (not decided here) is the §14 TYPE-position
splice (risk 4), which gates the out-of-scope sweep, not R1.

---

## 7. What remains BLOCKED (design-ahead honesty)

- **R1 (the three capabilities) is fully UNBLOCKED** and specified above; the parser patch (Crumb A1) is
  copy-ready today, the mangle arms (B1/B2) reuse the seam at `codegen.tks:2652-2663`/`:10734`, the
  group-bind (C1/C2) reuses the tag/exhaustiveness engine unchanged.
- **The ~2000-site SWEEP is BLOCKED on §14's `@Type()`** (recursive ADTs — `plano-9d…-v1.md` §4) and is
  NOT designed here. It resumes within minutes of §14's TYPE-position splice landing (risk 4).
- **The (b) `_str` nested-member path** (risk 2) is deferred to sweep-time, when the nested shape first
  appears; its threading is a mechanical follow-up, not a capability of R1.

*Grounding: every `file:line` re-verified on `origin/fix/retirement` this session (§1). Companion docs:
`plano-9d-capabilities-inline-uniao-v1.md` (the design this sequences), `plano-9d-migracao-variant.md`
(the still-valid per-ADT spine), `section14-macro-comptime-impl-v1.md` (`@Type()`, the sweep's gate),
`mudancas-superficie-0.3.1.md` §9.2b/§9.D (the sealed ban).*
