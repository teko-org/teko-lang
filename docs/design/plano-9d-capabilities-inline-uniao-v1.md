# §9.D — capability-segmented migration to the INLINE `|` union (v1, supersedes the Solution-A body of `plano-9d-migracao-variant.md`)

> **Status:** DESIGN. Read-only on product code — NO `.tks` edited, NO build, NO reseed, NO `teko test`
> in any form (the `monomorph` leak crashes the container). This document IS the artefact; the only
> commit of this crumb is itself. Isolated worktree off `origin/fix/retirement` (the main checkout and
> the reseed worktrees are being edited by other agents — UNTOUCHED).
>
> **What this doc changes vs `plano-9d-migracao-variant.md`.** That doc's spine (per-ADT
> classification, leaves→roots fixpoint, blast-radius, `.tkb` wire, the value→reference crux
> dissolution) STAYS VALID and is cited, not repeated. Its RECOMMENDATION — "Solution A /
> newtype-tagged-value-union wrapper `struct { case: … }`" — is **retired by owner ruling**: the target
> form is the **inline `|` union written out por-extenso** in each field/param/return, no wrapper, no
> alias. A BUILD by the implementer proved the OLD premise FALSE: the bare union `A | B` works, but the
> *shape the migration requires* does not yet parse / codegen / match. This doc re-plans §9.D as
> **segmented capability-PRs** that add exactly the three missing compiler capabilities FIRST (each an
> additive, byte-identical, inert reseed), then run the sweep as its own reseed — sequenced AFTER §14.
>
> **Sealed law drawn around (not reopened):** §9.2b / §9.D — `type X = variant` BANNED, `type X = A | B`
> named-union BANNED; the `|` union survives ONLY in var/param/return/field/constraint position
> (`docs/design/mudancas-superficie-0.3.1.md`). Owner rulings on THIS re-plan (do not reopen): (1)
> inline-por-extenso continues (simpler than a wrapper); (2) segment into capability-PRs, each its own
> additive reseed, byte-identical and inert until the corpus uses it, THEN the sweep as its own reseed;
> (3) combine capabilities into one additive reseed where possible (fewer reseeds), but each capability
> ships byte-identical (inert).

---

## 0. The count correction: it is **29**, not 28

The old plan (and `plano-match-universal`) counted 28. The authoritative grep on `origin/fix/retirement`
finds **29** — the missing one is **`MergeDisposition`** (`src/checker/merge.tks:31`,
`variant Absorb | Overload | Conflict`, all three arms `struct { }` → an **enum-pure** candidate, same
class as `Unique`/`ResidenceTier`).

```
grep -rnE '^\s*(pub )?(readonly )?type [A-Za-z0-9_]+ = variant ' src --include=*.tks | grep -v _test
```

The 29 (file:line, arm count):

| # | ADT | file:line | # | ADT | file:line |
|---|---|---|---|---|---|
| 1 | `Pattern` | `src/parser/pattern.tks:32` | 16 | `TStatement` | `src/checker/tast.tks:220` |
| 2 | `FSpecKind` | `src/parser/ast.tks:253` | 17 | `TItem` | `src/checker/tast.tks:290` |
| 3 | `ExprKind` | `src/parser/ast.tks:292` | 18 | `PointsTo` | `src/checker/spine.tks:98` |
| 4 | `BindTarget` | `src/parser/ast.tks:299` | 19 | `BorrowedFrom` | `src/checker/spine.tks:146` |
| 5 | `BindElem` | `src/parser/ast.tks:448` | 20 | `Unique` | `src/checker/spine.tks:165` |
| 6 | `Statement` | `src/parser/ast.tks:490` | 21 | `Type` | `src/checker/type.tks:166` |
| 7 | `ConstraintExpr` | `src/parser/ast.tks:508` | 22 | **`MergeDisposition`** | `src/checker/merge.tks:31` |
| 8 | `TypeBody` | `src/parser/ast.tks:768` | 23 | `ResidenceTier` | `src/checker/residence.tks:106` |
| 9 | `Decl` | `src/parser/ast.tks:826` | 24 | `RegAssignment` | `src/backend/regalloc.tks:1052` |
| 10 | `File` | `src/parser/ast.tks:829` | 25 | `MInstX86` | `src/backend/minst_x86.tks:746` |
| 11 | `ItemKind` | `src/parser/ast.tks:843` | 26 | `MInst` | `src/backend/minst.tks:924` |
| 12 | `TypeExpr` | `src/parser/type.tks:10` | 27 | `JsonValue` | `src/encoding/json/json.tks:73` |
| 13 | `ConstValueKind` | `src/checker/comptime_fold.tks:25` | 28 | `LOp` | `src/lir/lir.tks:208` |
| 14 | `TFSpecKind` | `src/checker/tast.tks:89` | 29 | `RegexNode` | `src/regex/regex.tks:86` |
| 15 | `TExprKind` | `src/checker/tast.tks:148` | | | |

**Sweep scale (why the sweep is not a hand edit).** The named ADTs are used as bare `: Type` /
`[]TStatement` / etc. in **thousands** of sites (grep on `origin/fix/retirement`, whole-word, product
code only): `Type` ~1055, `TStatement` ~399, `Statement` ~163, `MInst` ~145, plus `LOp`/`MInstX86`/….
Under inline-por-extenso each `: Type` becomes a **14-member recursive** union; each `[]TStatement` a
slice-of-a-10-member union. §4 shows why this is impossible by hand and must ride §14's `@Type()`.

---

## 1. The three missing capabilities (BUILD-verified by the implementer)

The bare union `A | B` in a plain var/param/return works today. The migration additionally needs three
shapes that currently do **not** parse / codegen / match:

- **(a) Parser — GROUPED union** `(A | B)`, `[](A | B)`, nested. Needed because a slice/optional/generic
  element is a type-PRIMARY, and a bare `|` binds at the union level: `[]A | B` parses as `([]A) | B`,
  never `[](A | B)`. A grouped primary is the only way to write a slice/optional of an anonymous union —
  which is exactly what `[]@Type()` (§14) and every `[]Type`/`[]TStatement` sweep site expands into.
- **(b) Codegen — mangle of a SYNTACTIC union nested in a slice/optional.** `cg_opt_mangle_texpr_str` /
  `cg_member_key_texpr_str` (`src/codegen/codegen.tks:2508,2518`) handle `NamedType`/`SliceType` but NOT
  `UnionType`; the resolved twin `cg_opt_mangle` (`:1809`) likewise stops at `_ => error` for a `Variant`
  inner. So `[](A | B)` today fails codegen ("optional inner type-expr not yet supported"). The challenge
  is **byte-agreement**: the syntactic-position name and the resolved-position typedef name of the SAME
  anonymous union must be the identical C identifier (§3).
- **(c) Checker — anon GROUP-union match** `(B | C) as v => …`. Today `B | C as v` reads as an
  `AltPattern` (value-axis alternatives) that CANNOT bind. Needed only where a member of an inlined union
  is itself a multi-member union that a site binds as a whole — which is **exactly and only**
  `ItemKind` (member `Statement`) and `TItem` (member `TStatement`).

All three are **additive and inert**: the current corpus contains **zero** `(A | B)` grouped types, zero
`[](…|…)`, zero `(…|…) as v` (grep confirms). They change compiler behaviour ONLY on syntax that today
is always a hard error, so the self-host is byte-identical → they combine into **one** additive reseed
(Reseed 1) without perturbing the seed. `(` in a type position is presently always an unexpected-token
error (the delegate rework retired `(A, B): R`), so branch (a) cannot alter any accepted program.

### 1.1 Capability (a) — the READY parser patch (reproduced; +1 fn, ~27 lines, self-hosts)

The implementer already implemented and validated this (branch work: `LParen` branch in
`parse_type_primary` + a `parse_grouped_type`, `src/parser/parse_type.tks`; self-hosts, 3572 MB). It is
reproduced here verbatim-ready so the implementer of Reseed 1 copies it. Insert the branch into
`parse_type_primary` (`src/parser/parse_type.tks:148`) between the `LBracket` (slice) branch and the
`is_delegate_kw_at` branch, and add `parse_grouped_type` beside `parse_slice`.

```teko
/**
 * parse_grouped_type — a PARENTHESIZED type group `(A | B | …)`, the only spelling that puts a `|`
 * union in a type-PRIMARY position (a slice element `[](A | B)`, a generic argument `List<(A | B)>`,
 * or a member of a wider union `(A | B) | C`). `pos` is at the `(` (the caller checked `LParen`). The
 * body is a FULL union-level type (`parse_type`), so an inner `|` binds inside the parens exactly as a
 * top-level union does; a single-member group `(A)` collapses to `A` (no needless `UnionType`, mirroring
 * `parse_type`'s M.5 rule). Separators may lead the closing `)` so a wide group may wrap across lines,
 * like a multi-line `variant` used to. A pending compound-`>` close (a nested generic mid-close) cannot
 * be followed by `)`, so it is a parse error here rather than a silently swallowed token.
 *
 * This is the type-PRIMARY twin of the pattern-side grouped bind `(B | C) as v` (capability c): the two
 * grouped spellings — one in a type position, one in a pattern position — are what let an inlined
 * anonymous union nest inside a slice and be bound as a whole after §9.D retires the named ADTs.
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

The one-line branch added to `parse_type_primary`'s `head` chain (after the `LBracket` arm):

```teko
    } else if is_kind_at(tokens, pos, lexer::TokenKind::LParen) {
        match parse_grouped_type(tokens, pos) { ParsedType as x => x; error as e => return e }
```

**Existing fns touched:** `parse_type_primary` (`parse_type.tks:148`, one new `else-if` arm) — nothing
else. `parse_type`/`parse_slice`/`skip_seps`/`err_at` are reused unchanged. Because a `(` in a type
position is presently always an error, the added arm is inert on the corpus.

### 1.2 Capability (c) — the checker anon group-union match, and the `ItemKind`/`TItem` ruling

**The shape.** After inline-por-extenso, `ItemKind`'s member `Statement` (itself a 11-arm union) is
written as a NESTED anonymous group: `UseDecl | (Binding | … | MultiBind) | Function | TypeDecl |
ConstDecl`. A site that used to write `Statement as s => …` must now bind that whole inner group. The
surface is the pattern-side twin of capability (a): a **parenthesized grouped bind** `(Binding | … |
MultiBind) as s => …`, whose bound type is the anonymous inner `Variant`.

**Why parenthesized (not the bare `B | C as v`).** A bare `B | C as v` is ambiguous with — and today IS
— an `AltPattern` (value-axis alternatives, which by the settled axis rule cannot bind,
`src/checker/match.tks:383`). Requiring the parens disambiguates by construction and mirrors the
type-side `(A | B)` of capability (a); zero change to the meaning of the existing bare-alt pattern, so
inert.

**The shapes the implementer adds (Teko, full Javadoc), against the declared AST:**

```teko
/**
 * GroupBindPattern — a parenthesized anonymous-union case that BINDS as a whole: `(B | C | …) as v`.
 * It names no single nominal case; its bound type is the anonymous `Variant` over its options, a
 * SUB-UNION of the subject. This is the pattern-side twin of the type-side grouped union `(A | B)`
 * (capability a): it is the only spelling that matches, and binds, a nested anonymous union member left
 * behind when §9.D inlines a union-of-unions (the `Statement` group inside `ItemKind`, the `TStatement`
 * group inside `TItem`). Distinct from `AltPattern` (value-axis alternatives that never bind): the parens
 * plus the `as v` are its discriminator, so a bare `b | c` alternative is untouched.
 *
 * @field options  the anonymous sub-union's member patterns, source order (each a bare case name)
 * @field binding  the bound identifier `v`
 * @since §9.D capability (c)
 */
pub type GroupBindPattern = struct { options: []Pattern; binding: str }

/**
 * group_bind_subunion — resolve a `GroupBindPattern`'s bound TYPE: the anonymous `Variant` over the
 * group's option case-names, as a SUB-UNION of the match subject. Every option must be a direct case of
 * the subject's variant view (`is_direct_case_of`, `match.tks:66`); the result reuses the members' own
 * resolved types in SOURCE ORDER, so the bound `v` is byte-identical to the boxed nested variant the
 * subject already carries at that tag slot — no re-tag, no re-box (the ruling below relies on this).
 *
 * @param gp        the grouped bind pattern
 * @param subject   the match subject's resolved type
 * @param table     the program type table
 * @param ref_ns    the match arm's referencing namespace
 * @return          the anonymous sub-union `Variant` bound to the pattern's identifier
 * @throws          when an option is not a direct case of the subject
 * @since           §9.D capability (c)
 */
fn group_bind_subunion(gp: GroupBindPattern, subject: Type, table: TypeTable, ref_ns: str): Type | error
```

**Existing fns touched (not recreated):** `src/parser/pattern.tks` — add `GroupBindPattern` to the
`Pattern` union and a parse branch (a `(` in pattern position followed by `… ) as name`);
`src/checker/match.tks` — `check_pattern` (`:230`, a new arm typing/binding the group), `arm_case_names`
(`:428`, a group contributes ALL its option names), `variant_covered` (`:435`/`:649`, a group covers
exactly its member set); no change to the tag/exhaustiveness engine — the discriminant is still the
subject tag, `variant_covered` still runs over the subject's member list.

**RULING RECOMMENDED — `ItemKind` / `TItem`: NESTED anon-group-match (capability c), NOT flatten.**

| axis | (c) NESTED anon-group `UseDecl \| (Binding\|…) \| …` | FLATTEN `UseDecl \| Binding \| … \| ConstDecl` |
|---|---|---|
| tag layout | Statement stays ONE boxed member (one tag slot) — **byte-identical** to today's `variant UseDecl \| Statement \| …` | Statement's 11 arms lift into ItemKind → tag renumbering, box→inline → **NOT byte-identical** |
| `.tkb` wire | member numbering unchanged → wire stable | numbering changes → forced `TKB_*_VERSION` bump |
| "bind the Statement group" | expressible (`(Binding\|…) as s`) | inexpressible — no whole-group case remains |
| fixpoint | additive/inert reseed | breaks byte-identity, larger blast-radius |
| cost | +1 pattern node, +3 checker touch-points | touches every `ItemKind`/`TItem` construction + match site |

**Recommendation: capability (c), the nested byte-identical form** — it preserves the tag layout and the
`.tkb` wire, keeps the sweep an additive reseed, and is the only form that can bind a whole sub-union.
The owner tends byte-identical; this is the byte-identical option. Flatten is rejected (breaks byte
identity, forces a wire bump, and loses the whole-group case). This ruling closes the only open question
capability (c) carries; no HALT.

---

## 2. Capability (b) — byte-agreement of the SYNTACTIC vs RESOLVED union mangle (3+ options + rec.)

### 2.1 The exact gap

For `[](A | B)` the compiler mangles the SAME anonymous union along two paths that must agree to the byte:

- **resolved path** (the collected typedef DECLARATION): `Slice{ element: Variant{A,B} }` →
  `cg_slice_typename` → `cg_opt_mangle(buf, Variant)` (`codegen.tks:1809`). Today the `Variant` inner hits
  `_ => error`. The intended suffix, mirroring how a top-level inline `A | B` is named
  `tk_u_<keys>` by `cg_variant_typename` (`:1963`), is `u_<keys>` — i.e. `[](A|B)` → `tk_slice_u_a_b`.
- **syntactic path** (a signature/field USE position): `SliceType{ element: UnionType{A,B} }` →
  `cg_opt_mangle_texpr_str` (`:2508`). Today the `UnionType` inner hits `_ => error`. It must produce the
  IDENTICAL `u_<keys>` suffix.

The two paths already agree on a bare member key (`nt.path.segments[last].name` at `:2510` ==
`name_last_segment(nm.name)` at `:1862`). The HARD part is the **union-level canonicalization** the
RESOLVED path applies but the syntactic type-expr has not seen:

1. **null-first normalization** — `cg_union_normalize_null` (`:1994`) moves a `null` member to index 0
   and collapses duplicates, so a resolved `X | null` mangles from member order `null, X`; the syntactic
   `UnionType` still holds source order `X, null`. Naively concatenating keys gives `u_x_null` (syntactic)
   vs `u_null_x` (resolved) → **mismatch**.
2. **member dedup / C3 rep split** — a null-bearing union takes the niche rail (`cg_variant_has_null`,
   `:1983`; `cg_texpr_union_has_null`, `:2531`); the null-FREE union keeps the legacy enum tag. The rep
   split must be decided identically from both sides.
3. **nested unions** — a group member that is itself a union (the `ItemKind`/`TItem` case) recurses; the
   two paths must recurse in lockstep.

"The syntactic side lacks resolved member-info" (aliases, canonical `ns::Name`, normalization) — hence
the three options for how to make them byte-agree.

### 2.2 The options

**Option 1 — RESOLVE-then-mangle (single mangle path). RECOMMENDED.**
At the syntactic mangle site, do NOT mangle the `UnionType` directly. Resolve it to a `checker::Variant`
via the codegen-local `cg_te_to_type_ns` (already used for exactly this at `codegen.tks:11050`), then feed
the resolved `Variant` to the ONE resolved path (`cg_variant_typename` / `cg_opt_mangle`). Add a `Variant`
arm to `cg_opt_mangle` (`:1809`) and `cg_opt_mangle_str` (`:1845`) that emits `u_` + the member keys via
the existing `cg_member_key`; the syntactic `cg_opt_mangle_texpr_str` `UnionType` arm becomes a
one-liner: resolve, then call the resolved suffix.

```teko
/**
 * cg_opt_mangle — … existing arms … PLUS: a nested anonymous union inner mangles to `u_<member keys>`,
 * the SAME suffix `cg_variant_typename` emits for a top-level inline `A | B` (minus the `tk_` prefix), so
 * a `[](A | B)` slice typedef is `tk_slice_u_<keys>` and every use agrees with the collected decl.
 */
//   checker::Variant as va => cg_variant_suffix(cb(buf, "u"), va)   // '_' + key per member, source order post-normalize
```

- *Byte-agreement:* GUARANTEED — there is literally one canonicalization (the resolved one), applied
  once; null-first / dedup / rep-split / nested recursion cannot drift because the syntactic side no
  longer owns a copy of them.
- *Cost:* the syntactic mangle site must have a `(ns, TypeTable)` to resolve in. That seam already exists
  and is already exercised at `:11050` (`cg_te_to_type_ns(... ut ...)`), so no new plumbing.
- *Inertness:* fires only on a `UnionType`/`Variant` inner, which the corpus lacks today → inert.

**Option 2 — TWIN-MIRROR the normalization on the type-expr.**
Add a `UnionType` arm to `cg_opt_mangle_texpr_str` that re-implements null-first + dedup on the type-expr
(reusing `cg_texpr_union_has_null`, `:2531`) and then emits `u_` + `cg_member_key_texpr_str` per member,
kept byte-identical to the resolved `cg_variant_typename` by inspection — exactly the resolved/syntactic
twin idiom the file already lives by (`cg_opt_mangle_str` ↔ `cg_opt_mangle_texpr_str`,
`cg_variant_typename` ↔ `cg_variant_typename_texpr`).

- *Byte-agreement:* by construction-mirroring (two twins hand-kept in lockstep).
- *Cost:* a SECOND normalization implementation that must track the resolved one forever; any future rep
  tweak (a new niche rule, a member-order change) must touch both or the C definition and its use diverge
  silently. Highest maintenance; the twin-drift is precisely the failure mode this migration cannot risk
  at ~2000 sites.

**Option 3 — CANONICALIZE the union type-expr once, upstream of both paths.**
Run a `canonicalize_union_texpr` (null-first, dedup) immediately after `parse_type` builds a `UnionType`,
so BOTH paths see an already-canonical member order and the naive `u_<keys>` concat agrees without either
path re-normalizing.

- *Byte-agreement:* both paths start from the same order → naive concat agrees.
- *Cost:* mutates AST member order globally; any consumer that relies on SOURCE order (diagnostics,
  `union_normalize_null` in the checker, `.tkb` member emission) must be audited; broadest blast-radius.
  The checker's own `union_normalize_null` would become a near-no-op yet must stay for safety → two
  sources of truth again. Cleanest concept, riskiest reach.

**Option 4 (noted, not recommended) — content-hash typedef name** `tk_u_<hash-of-sorted-members>`:
byte-agreement reduces to "both sides produce the same sorted member-name list", but it loses the
human-readable C identifier (debuggability) and needs a collision-free hash — over-engineered for a
finite corpus.

### 2.3 Recommendation

**Option 1 (resolve-then-mangle).** One canonicalization, zero twin-drift, reuses the `cg_te_to_type_ns`
seam already present at `codegen.tks:11050`, byte-identical by construction. Fall back to Option 2's
twin-mirror ONLY if a syntactic mangle position is found where resolution is genuinely unavailable — none
exists in the IR corpus (every slice/optional/generic element over a union is a concrete resolved type at
codegen). Add the `Variant` arm to `cg_opt_mangle` / `cg_opt_mangle_str` and route
`cg_opt_mangle_texpr_str`'s new `UnionType` arm through resolution.

**Existing fns touched:** `cg_opt_mangle` (`:1809`, add `Variant` arm), `cg_opt_mangle_str` (`:1845`,
twin arm for the str composition), `cg_opt_mangle_texpr_str` (`:2508`, `UnionType` arm → resolve →
resolved suffix), `cg_member_key`/`cg_member_key_texpr_str` (reused unchanged), `cg_te_to_type_ns`
(reused). No engine change; the tag numbering (`store_variant_tag`) and rep split (`cg_variant_has_null`)
are untouched.

---

## 3. Reseed grouping (minimize reseeds; each capability byte-identical)

| Reseed | Contents | Additive? | Byte-identical self-host? | Gate |
|---|---|---|---|---|
| **R1 — capabilities** | (a) grouped-type parser + (b) union mangle byte-agreement + (c) anon group-bind pattern, ALL together | yes | yes (corpus uses none of the new shapes → inert) | R1 ritual (§7): reseed + capability unit fixtures |
| **§14 track** | `@Type()` etc. per `section14-macro-comptime-impl-v1.md` (its own A0–A4 reseeds) | yes | yes (source-only consumer) | per §14 plan |
| **R2…Rn — the sweep** | leaves→roots, grouped as §5; each group des-teaches its `variant` name → rewrites decls+sites (via `@Type()` after §14) → reseed → cleanup → reseed | representation-preserving | byte-identical (nested groups, §1.2) or shrinks (enum-pure) | per-group ritual |

The three capabilities are ONE reseed (R1) because all three are inert on the seed — the owner's
"combine where you can" applies maximally here. The sweep is deliberately NOT combined with R1: it is the
first reseed that actually EXERCISES the new shapes, so it must gate on the already-reseeded capability
compiler.

---

## 4. The §14 / `@Type()` dependency — the sweep MUST follow §14 (recursive ADTs cannot be inlined by hand)

**Ergonomic reason:** ~2000+ sites (`Type` ~1055, `TStatement` ~399, `Statement` ~163, `MInst` ~145, …),
each `: Type` expanding to a 14-member union, is not a survivable hand edit.

**HARD reason (not merely ergonomic):** the core ADTs are **RECURSIVE** — `Type` contains `[]Type`
(through `Slice`, `Variant.members`, `Func`, `Reference`, `Ptr`); `Statement`/`TStatement` contain `Expr`
trees that transitively contain statements. An inline `|` union of a recursive type is **not expressible
in finite source** without a name or a macro: writing `(Prim | … | Variant | … | Null)` requires, inside
`Variant`, another `[]Type` — i.e. the union must refer to itself. §9.2b bans re-naming it as a `type`.
The ONLY §9.D-legal way to close that reference is §14's **`@Type()` macro** — a comptime abbreviation
(NOT a `type`, so the ban is honored) whose `lowering` is the inline union and which recurses through
`pub type Variant = struct { members: []@Type() }` (`section14-macro-comptime-impl-v1.md:180-181,211-215`;
the box the union emits breaks the DATA recursion, `@Type()` breaks the SOURCE recursion). The `[]@Type()`
splice is a slice-of-a-grouped-union — i.e. it consumes capability (a)+(b) directly.

**Sequencing recommendation:** run the sweep AFTER §14 lands `@Type()` (and the TYPE-position splice,
`section14 §5.1`, currently OPEN there). Then the sweep of a recursive ADT is `: Type` → `: @Type()` (a
7-char macro call), and `[]Type` → `[]@Type()` (needs capability a+b). The non-recursive, small ADTs
(enum-pure `Unique`/`ResidenceTier`/`MergeDisposition`; `File`; `BindTarget`; `BindElem`) may sweep early
by hand within R2, but there is no reason to split the track — folding them into the post-§14 sweep keeps
one mechanism.

**Cross-doc note (report up, do not action here):** `section14 §5.1` leaves the TYPE-position `lowering`
splice OPEN (Opt-1 context-polymorphic re-parse vs Opt-3 decl-site production kind). The §9.D sweep needs
Opt-1-or-equivalent so `@Type()` yields a `TypeExpr`. This is a §14 decision; flagged for the §14 owner,
not decided here.

---

## 5. Fixpoint order (leaves → roots), and the sweep grouping

After R1 + §14, sweep in dependency order (each: des-teach the `variant` name → rewrite decl + sites →
reseed → cleanup → reseed):

1. **Enum-pure** (representation SHRINKS tagged→int; `match`-over-enum already exists): `Unique`
   (`spine.tks:165`), `ResidenceTier` (`residence.tks:106`), `MergeDisposition` (`merge.tks:31`). Confirm
   by grep that each arm name (e.g. `ResidenceTier`'s generic `Scope`/`Frame`/`Root`) is used ONLY as a
   member of its variant before converting; else keep it an inline union.
2. **Already-wrapped, byte-identical inline into the existing `.kind` field**: `ExprKind`→`Expr.kind`
   (`ast.tks:292/293`), `TExprKind`→`TExpr.kind` (`tast.tks:148/10`), `FSpecKind`→`FSpec.kind`,
   `TFSpecKind`→`TFSpec.kind`. Same tag/order → byte-identical.
3. **Leaf payload unions (own site set, no recursion into the big trees)**: `RegAssignment`, `LOp`,
   `MInst`, `MInstX86`, `RegexNode` (stdlib), `JsonValue` (stdlib), `PointsTo`, `BorrowedFrom`,
   `RegexNode`, `ConstValueKind`, `ConstraintExpr`, `TypeExpr`, `BindElem`, `BindTarget`, `File`,
   `TypeBody` (the `.tkb` pilot — round-trips the wire codec, `tkb_write.tks:446`).
4. **Recursive trees (ride `@Type()`-style macros)**: `Pattern`, `Statement`, `TStatement`, then
   `Decl`, `ItemKind`, `TItem` (the two union-of-unions that need capability (c)'s grouped bind), and
   **`Type` LAST** — the most entangled; its `Variant` arm STAYS as the internal carrier of anonymous
   `A | B` (§9.D preserves the anonymous union), so migrating `Type` removes the NAME `Type`, not the
   notion of `Variant`.
5. **Retire the FORM** `type X = variant …` from the parser/checker (the `type X = A | B` rejection
   already exists). Final reseed.

**Blast-radius (representative):**

| ADT | ~sites | shape after sweep | byte-identical? | needs |
|---|---|---|---|---|
| `Type` | ~1055 | `@Type()` (recursive union macro) | yes (same tags) | a+b+§14 |
| `TStatement` | ~399 | `[]@TStatement()` / `@TStatement()` | yes | a+b+§14 |
| `Statement` | ~163 | inline union / `@Statement()` | yes | a+b+§14 |
| `MInst` | ~145 | inline 31-union / macro | yes | a+b (+§14 ergo) |
| `ItemKind` | tens | `UseDecl \| (Statement group) \| …` | yes | c |
| `TItem` | tens | `TFunction \| (TStatement group) \| …` | yes | c |
| enum-pure ×3 | few | `enum` | shrinks | — |

---

## 6. Fixtures (`.tkt` unit / `.tkr` regression → native exit codes)

New shapes go under `examples/regressions/own_native` (run-fixtures, exit = `main`'s observable value)
and the parser/checker `.tkt` suites; reject-fixtures use an `EXPECT_COMPILE_FAIL` sentinel dir with a
`.tkr` diagnostic scenario (format: `examples/regressions/block_expr_reject/`).

**Capability R1 (must pass at R1, corpus still on `variant`):**

1. **(a) grouped type parses & runs** — `var xs: [](Circ | Sq) = …; match xs[0] { Circ as c => 1; Sq as q
   => 2 }` → distinct exit per arm. (`own_native`.)
2. **(a) nested group** — `(A | B) | C` and `List<(A | B)>` parse; a value of each flows → exit proves it.
3. **(a) single-member group collapses** — `(A)` types exactly as `A` (no `UnionType`); a program using
   both spellings gives the same exit.
4. **(b) `[](A | B)` codegen byte-agreement** — a slice-of-union built in one function and consumed
   through a signature in another compiles (the USE typedef name == the DECL typedef name) and runs →
   nonzero exit proves a value crossed the seam. This is the fixture that would have caught the old
   "optional inner type-expr not yet supported".
5. **(b) `X | null` order-agnostic mangle** — a `[](X | null)` and a `[](null | X)` mangle to the same
   typedef (Option 1: both resolve-normalize) → both compile and interoperate.
6. **(c) grouped bind** — subject with a nested union member; `(B | C) as v => use(v)` binds the whole
   sub-union → exit distinguishes the group arm from a sibling arm.
7. **(c) exhaustiveness with a group** — a match whose only non-`_` arm is `(B | C) as v` over a subject
   `A | (B | C)` WITHOUT covering `A` → REJECTED (`variant_covered` counts the group's members as covered
   together, `A` as uncovered). (`EXPECT_COMPILE_FAIL`.)
8. **(c) parens required** — `B | C as v` (no parens) still REJECTED "an alternative (`|`) cannot bind;
   use a separate arm" → proves capability (c) did not loosen the bare-alt rule. (`EXPECT_COMPILE_FAIL`.)

**Sweep fixtures (per group, from R2):**

9. **enum-pure** — `Unique`/`ResidenceTier`/`MergeDisposition` as `enum`; `match` per member → exit.
10. **already-wrapped byte-parity** — a program building/matching `Expr.kind` gives the SAME exit before
    and after inlining `ExprKind` (fixes group-2 byte-identity).
11. **`.tkb` round-trip** — serialize/deserialize a migrated `TypeBody`; equality holds; a prior-version
    artefact rejected iff a version bump was forced.
12. **`type X = variant …` now a parse error** — guided message toward the inline union / enum / macro.
    (`EXPECT_COMPILE_FAIL`, after step 5 of §5.)

---

## 7. Ritual points (full gate required)

- **R1** — after the three capabilities (additive, `variant` still alive): reseed + fixtures 1–8. The
  gate is self-reproduce (byte-identical) + green surface; no fixture may require the new shapes to exist
  in the seed.
- **§14 rituals** — per `section14-macro-comptime-impl-v1.md` (out of this doc's scope; its `@Type()`
  A4 unlock is the gate this sweep waits on).
- **R2…Rn** — after EACH sweep group (des-teach → rewrite → reseed → cleanup → reseed); the `TypeBody`
  pilot has its own gate with fixture 11; the enum-pure group with fixture 9; the already-wrapped group
  with fixture 10.
- **Final** — after retiring the `variant` FORM: reseed + fixtures 12 + full suite.
- A `.tkb`/`.tkh` wire bump, if any group forces one, lands at a SINGLE crossing with a dedicated gate
  (`TKB_EXPR_VERSION`/`TKB_PROGRAM_VERSION`/`TKH_VERSION`).

---

## 8. Risks + law tensions (HALT check)

1. **[resolved, law-first] recursive union not hand-inlinable (§4).** Inline-por-extenso of a recursive
   ADT is impossible in finite source; §14's `@Type()` (a macro, not a named `type`) is the §9.D-legal
   mechanism. Resolution: sequence the sweep AFTER §14. No law tension — a macro abbreviation is neither
   `type X = variant` nor `type X = A | B`, so §9.2b holds. **No HALT.**
2. **[resolved, recommended] `ItemKind`/`TItem` — nested group vs flatten (§1.2).** Recommend capability
   (c) nested (byte-identical); flatten rejected (breaks byte identity + wire). Owner tends byte-identical.
   **No HALT.**
3. **[recommended, single path] `[](A|B)` mangle byte-agreement (§2).** Recommend Option 1
   (resolve-then-mangle) — one canonicalization, no twin-drift. **No HALT.**
4. **[report up, do not action] §14 TYPE-position splice OPEN** (`section14 §5.1`). The sweep needs
   `@Type()` to yield a `TypeExpr`; that is a §14 ruling. Flagged for the §14 owner. Not a §9.D tension.
5. **[verify at implement time] enum-pure arm-name reuse.** `ResidenceTier`'s arms (`Scope`/`Frame`/
   `Root`) are generic names; confirm by grep each is used only as a member of its variant before
   converting to `enum`, else keep an inline union. Mechanical, not a tension.
6. **[verify] `.tkb` wire stability.** The sweep preserves member ORDER (nested groups, §1.2), so the
   wire should stay stable; if any serialized layout shifts, one version bump at one crossing (§7).

**No genuine law tension survives → NO HALT.** The plan passes every standing law: Teko-only (grouped
union, group-bind pattern, enum, macro are all Teko/§14 forms — the C twins stay frozen, only their
Teko-side mangle/parse/match gain arms); W15/Javadoc (all snippets above are full-Javadoc, copy-ready);
law-first (the recursive-inline crux resolves by §14 sequencing, the two rulings resolve by byte-identity);
issue-100% (all 29 ADTs sequenced, count corrected); seed-safe (capabilities inert → one additive reseed,
sweep leaves→roots byte-identical). The only item RELAYED up (not decided here) is the §14 TYPE-position
splice (risk 4).

---

## 9. What remains BLOCKED (design-ahead honesty)

- **The sweep itself** is blocked on §14's `@Type()` (recursive ADTs — §4). Everything up to and including
  Reseed 1 (the three capabilities) is UNBLOCKED and fully specified above; the parser patch (§1.1) is
  copy-ready today.
- **Capability (b)'s Option 1** assumes `cg_te_to_type_ns` is reachable at the syntactic mangle site; it
  is exercised at `codegen.tks:11050` today, so this is validated, not assumed — but the implementer
  should confirm the resolution context is threaded to the specific `cg_opt_mangle_texpr_str` call chain
  before committing to Option 1 over the Option 2 twin-mirror fallback.
- **The §14 TYPE-position splice** (risk 4) is owned by the §14 plan; the §9.D sweep resumes within
  minutes of that landing.

*Grounding: all citations are real `file:line` on `origin/fix/retirement`. Companion docs:
`plano-9d-migracao-variant.md` (per-ADT classification + the value→reference crux, still valid spine),
`plano-match-universal-e-migracao-variant.md`, `plano-item14-value-struct-mutavel.md`,
`section14-macro-comptime-impl-v1.md` (`@Type()` producer), `mudancas-superficie-0.3.1.md` §9.2b/§9.D
(the sealed ban).*
