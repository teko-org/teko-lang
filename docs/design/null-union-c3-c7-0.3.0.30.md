# Null-union PROGRESSION — Crumbs C3..C7 (wave 0.3.0.30)

> STATUS: **DESIGN — ordered, gate-able crumb sequence.** Author: architect
> (design-ahead). Umbrella: `remodel/emit-throughput` (34a515c). Follows the
> RATIFIED base `docs/design/null-as-union-type.md` (owner, 2026-07-19) and the
> AS-BUILT 0.3.0.29 BASE (PR #23). No product `.tks` is changed BY THIS DOCUMENT
> — it is the implementation plan; each crumb below changes code under its own
> gate. Every code snippet is written in full-Javadoc (W15) and is copy-paste
> ready for the implementer.
>
> This progression is BYTES-CHANGING. The load-bearing proof at every crumb is
> the **self-host FIXPOINT** (`gen1==gen2` byte-identical) plus the **size
> probe** (the emitted-C `sizeof`/heap assertion). These are NOT optional — a
> crumb that changes representation and does not carry both rituals is incomplete.

---

## 0. What 0.3.0.29 actually shipped (the BASE — read from the tree, not the plan)

The ratified base plan `null-as-union-type.md` §8 lists 8 crumbs. The as-built
.29 BASE landed **doc-Crumbs 1-2 plus partial bridge scaffolding**. Confirmed by
reading the tree at authoring time:

- **`Null` type case exists.** `checker::Type` gained `… | Reference | Null`
  (`src/checker/type.tks:99,102`), `pub type Null = struct { }`. Arms wired in
  `type_eq` (`type.tks:157`), `type_mangle` → `"null"` (`type.tks:1627`),
  `type_render` → `"null"` (`type.tks:1781`); `Null` is a leaf in `subst_type`.
- **`null` literal parses** as a single-segment `NamedType "null"` (reserved
  keyword) → `resolve_type` maps it to `checker::Null` (`parser/parse_type.tks:36,52`).
- **Union accepts a `null` member + canonical null-first order.**
  `union_normalize_null` (`resolve.tks:1461`) moves `Null` to index 0, called in
  the variant-resolve fold (`resolve.tks:1534`). `null | error` ≡ `error | null`
  now resolve to one type + one mangle.
- **Widening + match.** `null_widens_into` (`resolve.tks:834`), `is_bare_null_value`
  (`resolve.tks:818`), `variant_has_null` / `some_arm_is_null` + `Null`
  exhaustiveness (`checker/match.tks:149,425,450`).
- **`checker::Null` → `uint8_t`** in codegen (`codegen/codegen.tks:1027`);
  `cg_opt_mangle`/`_str` carry a `Null → "null"` arm (`codegen.tks:1123,1161`).

**What the base LEFT as a bridge / placeholder (this is the C3..C7 work):**

1. **No real `T | null` representation.** A `T | null` union today emits through
   the NAIVE inline-variant path `tk_u_<keys>` whose tag is a C `enum tk_tag_<M>`
   — **4 bytes** on LP64 (`codegen.tks:7291-7315`). Per base §6.2 this REGRESSES
   every small-scalar and pointer optional (`i8|null` 2→8, `ClassRef|null`
   8-niche→16). No niche, no box-in-arena, no `uint8` discriminant exists yet.
2. **Legacy `Optional` former still IS the representation of `T?`.** `tk_opt_<inner>`
   = `{ bool present; <inner> value; }` (`codegen.tks:1095-1109`,
   `emit_type_decl` ~3857). The `Optional` checker case (`type.tks:71`) is intact.
3. **The `null` literal is still born as the legacy sentinel.** `type_nulllit`
   (`typer.tks:42`) types a bare `null` as `Optional { inner = Void }`; only when
   an EXPECTED union is present does `typer.tks:3215` retype it to `Null{}`. The
   structural pivot (born-as-`Null`) is NOT done.
4. **No flow-narrowing.** There is no `narrow_on_eq_guard` — `if x == null` /
   `if x != null` do not narrow. Only `match` narrows (via existing variant arms).
5. **R2 NOT relaxed.** The variant-member gate (`resolve.tks:1519`) still rejects
   `Reference` outright — `Ref<T> | null` does NOT resolve yet. (base §3d/D3 puts
   this in doc-Crumb 2; it did not land, so C3 carries it as a prerequisite.)
6. **All sugar present.** `SafeFieldAccess` / `Coalesce` / `SafeMethodCall`
   (`parser/ast.tks:46-49`), postfix `OptionalType` (`parse_type.tks:64-68`),
   `lower_coalesce` / `lower_safe_field_access` (`lir/lower.tks:611-612,2816,2838`),
   and the 77 `-> error?` signatures are all live.
7. **Interface-in-union honest-stop is intact** (`resolve.tks:1523`), pinned by
   the #28 "S4" carve-out (`docs/design/interface-value-type.md` §5-S4/§6).

### 0.1 Crumb-number mapping (this doc's C3..C7 vs base §8's Crumbs 3-8)

| This doc | base §8 crumb(s) | one-line scope |
|---|---|---|
| **C3** | Crumb 3 (+ deferred Crumb-2 R2 relax) | codegen niche + `uint8` inline-tag + `tk_null`; relax R2 for `Ref<T>\|null` |
| **C4** | Crumb 4 | box-in-arena ≥16B + the general `#inline` directive |
| **C5** | Crumb 5 | structural pivot: `null`→`Null`, universal narrowing, DESUGAR bridge |
| **C6** | Crumb 6 | mechanical corpus + stdlib rewrite to canonical spelling |
| **C7** | Crumb 7 **+** Crumb 8 | DELETE sugar/`Optional` former + sentinel cleanup + final full gate |

Five crumbs, each buildable by the prior seed. The order is the base's ratified
order and guarantees **no intermediate build regresses layout or breaks the
fixpoint**: representation (C3/C4) lands before any corpus value depends on it;
the desugar bridge (C5) makes `T?` ≡ `T | null` before the corpus is rewritten
(C6); the forms are deleted only once provably dead (C7).

### 0.2 Ritual vocabulary (used per crumb below)

- **[RITUAL: fixpoint]** — the self-host fixpoint: the seed builds `src/` → gen2
  compiler; gen2 builds `src/` → gen2 compiler; **gen1==gen2 byte-identical**.
  This is the load-bearing proof for every bytes-changing crumb.
- **[RITUAL: size probe]** — a native throughput gate: an emitted-C fixture under
  `examples/regressions/` that `EXPECT_EXIT`s on `sizeof`/heap assertions matching
  the base §6.2/§6.5 targets. Any crumb changing representation carries one.
- **[RITUAL: dual-engine]** — the fixture runs on BOTH engines: the LIR-interp
  oracle (`src/lir/lir_interp.tks`, the semantic oracle that replaced the retired
  VM — #524) AND native (the C backend; and, where in the differential corpus, the
  own AOT backend via `scripts/diff_c_own.sh`). "VM and native" in this plan means
  exactly this oracle + native pair.
- **[RITUAL: full gate]** — the whole `teko test .` suite + all `scripts/*_regressions.sh`
  legs `completed + success`. Reserved for C7 (the ratified end-state).

**Seed-sequencing law.** Each crumb's `src/` must build under the PREVIOUS
released seed. C3/C4 add representation the seed does not emit yet — they are
gated so `src/` itself uses NO `T | null` value until C5; the C3/C4 code is
reachable only by the new fixtures, never by the compiler's own corpus. C5 flips
`src/`'s own null usage onto the unified rail. This is why representation precedes
the pivot.

---

## C3 — codegen niche + `uint8` inline-tag classes; `tk_null`; relax R2. **L. BYTES-CHANGING.**

**[RITUAL: fixpoint] + [RITUAL: size probe] + [RITUAL: dual-engine].**

**Goal.** Give `T | null` a NON-REGRESSING representation for the two
zero-indirection classes, and admit `Ref<T> | null` at the resolve gate. No
corpus value uses these shapes yet (C5 does), so the compiler's own emitted bytes
are UNCHANGED — the crux fixpoint proof is that adding the machinery, dormant,
leaves `gen1==gen2`, and the size probe proves the new shapes hit target.

### Representation decision (the rule, applied here)

For a resolved `Variant` whose members are exactly `{ null, X }` (a two-member
absence union), classify `X`:

- **NICHE (1 word, 0 tag, 0 indirection)** — when `X` carries a spare NULL
  bit-pattern: a class handle (`Named` class → `C*`), `Ptr` (`ptr<T>` → `T*`),
  `Reference` (`Ref<T>` → `T*`), or `error`/`Str` (the `{ptr,len}` fat value whose
  `ptr` is never NULL for a live value). Emit `X` bare; `NULL`/`{ptr=NULL}` MEANS
  `null`; NO tag word. Niche-safety is proven in base §6.2: M.1 allocation never
  returns NULL (`teko_rt.h:115`); a live `error`/`str` `.ptr` is never NULL
  (`teko_rt.h:299`). Wins: `ClassRef|null` 16→8, `ptr<T>|null` new-8,
  `null|error` **24→16**.
- **INLINE `uint8`-TAG (`{ uint8_t tag; <X> payload; }`)** — when `X` is a
  saturating scalar ≤ 8 bytes (`i8..i64`, `bool`, `char`, `byte`, `f32/f64`): no
  free niche, but a **1-byte** discriminant (the owner's `0b00000000`) instead of
  the 4-byte C-enum tag. Restores parity with today's `bool present`:
  `i8|null` = 2, `i32|null` = 8, `i64|null` = 16 — no regression.
- **(defer to C4)** saturating payload ≥ 16 bytes → box-in-arena.

`> 2` members, or a saturating ≥16B member, fall through to C4 / the existing
tagged path (with the `uint8` tag, once C3 lands, for any null-bearing union).

### Files touched

- `src/codegen/codegen.tks` — the union emitter. Add `cg_union_niche_member`
  (below); route a null-bearing `Variant` through it; when it returns a member,
  emit `X` bare (niche); else emit the `{ uint8_t tag; … }` inline-tag struct
  (replacing the 4-byte `enum tk_tag_` for null-bearing unions — the `enum` stays
  for null-free `A|B|C` to keep those mangles byte-stable, base §6.3). Touch
  `emit_type_decl` (~`codegen.tks:7285-7320`), the wrap/read helpers
  (`cg_wrap_open` ~3737, the match-arm tag compare ~4700), and `tk_null` is already
  `uint8_t` (`codegen.tks:1027`) — confirm the standalone `Null` value emits `0`.
- `src/checker/resolve.tks` — **relax R2** at the variant-member gate
  (`resolve.tks:1519`): admit a `Reference` member ONLY in the two-member
  `Ref<T> | null` shape (a `Reference` alongside exactly one `Null`); keep
  rejecting a bare stored `Reference` in any other union/struct/collection (R4
  storability is a separate concern — base §3d). Replace the flat
  `Reference => return error{…}` arm with the shape-aware helper below.
- `src/backend/*` / `src/lir/lower.tks` — the LIR/own-backend value model learns
  the niche load (a bare pointer whose 0 is `null`) and the `uint8`-tag load. The
  LIR-interp oracle carries `Null` as a zero word already (`lower.tks` null lit).

### New / changed function shapes (copy-paste, full Javadoc)

```teko
/**
 * cg_union_niche_member — for a two-member absence union `{ null, X }`, return
 * `X` when it carries a spare NULL bit-pattern so codegen may emit `X` BARE with
 * `NULL` (or a `{ptr=NULL}` fat value) meaning `null` and NO tag word; return
 * `null` when the union must be tagged (a saturating payload, or more than two
 * members). The niche classes are: a `Named` class handle, `Ptr`, `Reference`,
 * and `error`/`Str` (fat `{ptr,len}` whose `ptr` is never NULL for a live value).
 *
 * @param v  the resolved union type (its members already null-first normalized)
 * @return   the niche-carrying member `X`, or `null` if the union must be tagged
 * @throws   error on an internally malformed member type
 * @see      cg_union_tag_ctype (the inline `uint8`-tag path this gates)
 * @since 0.3.0.30 (null-union C3)
 */
fn cg_union_niche_member(v: checker::Variant) -> checker::Type | null | error

/**
 * variant_member_admissible — the C3 replacement for the flat `Reference`
 * rejection at the variant-member gate. A `Reference` member is admitted ONLY as
 * the never-null-handle half of a two-member `Ref<T> | null` union (R2 relaxed,
 * base §3d/D3); every other stored `Reference` is still rejected (R4). `void` and
 * a bare interface `Named` remain rejected (the #28 S4 honest-stop is untouched
 * here — see §Interactions). `Null` and ordinary value members are admitted.
 *
 * @param member    the resolved candidate member type
 * @param siblings  the other resolved members already collected for this union
 * @param table     the type table (interface-name resolution)
 * @return          null when `member` is admissible; an error naming why not
 * @since 0.3.0.30 (null-union C3)
 */
fn variant_member_admissible(member: checker::Type, siblings: []checker::Type, table: TypeTable) -> null | error

/**
 * cg_union_tag_ctype — the C type of a null-bearing tagged union's discriminant:
 * a `uint8_t` (the owner's 1-byte `0b00000000`), REPLACING the legacy 4-byte
 * `enum tk_tag_<keys>` for any union carrying a `null` member. A null-FREE
 * `A | B | C` keeps its existing enum tag (mangle byte-stability, base §6.3).
 *
 * @param buf  the emit buffer
 * @param v    the resolved null-bearing union
 * @return     `buf` with `uint8_t` appended, or an error
 * @since 0.3.0.30 (null-union C3)
 */
fn cg_union_tag_ctype(buf: []byte, v: checker::Variant) -> []byte | error
```

### Fixtures this crumb adds

- `examples/regressions/repr_niche/` — **[size probe]** emits and runs a program
  that `EXPECT_EXIT`s on: `sizeof(ClassRef|null)==8`, `sizeof(ptr<T>|null)==8`,
  `sizeof(Ref<T>|null)==8`, `sizeof(null|error)==16`, `sizeof(i8|null)==2`,
  `sizeof(i32|null)==8`, `sizeof(i64|null)==16`. Each MUST show no regression vs
  the recorded legacy `tk_opt_*`. Exit 0 = all asserts hold.
- `examples/regressions/ref_null_admit/` — `let r: Ref<Node> | null = null` and
  `= some_ref` both RESOLVE and build (R2 relaxed); a bare stored `Reference`
  field still `EXPECT_COMPILE_FAIL`s (R4 intact).
- `t/repr_niche_roundtrip.tks` (checker/codegen unit) — a niche `ClassRef|null`
  and a tagged `i32|null` each round-trip null↔present with identical observable
  behavior on the LIR-interp oracle and native.

### Ritual gate

FIXPOINT is the crux: the machinery is dormant (no `src/` value uses it yet), so
`gen1==gen2` MUST hold byte-identically — proving C3 added the representation
without disturbing existing emission. Size probe proves the targets. Dual-engine
proves oracle==native on the new shapes.

---

## C4 — box-in-arena (saturating ≥16B) + the general `#inline` directive. **L. BYTES-CHANGING (opt-in).**

**[RITUAL: fixpoint] + [RITUAL: size probe] + [RITUAL: dual-engine].**

**Goal.** Complete the three-class representation: default-box the saturating
≥16B sums (8-byte handle, `NULL`=null=zero heap, arena bump-alloc on non-null),
and add the `#inline` binding/field attribute that flips a boxed-by-default sum
back to inline-tag. Still dormant for the corpus (C5 flips usage), so fixpoint
again proves the additive machinery is byte-neutral for existing emission.

### Representation decision (the rule, applied here)

- **Saturating payload ≥ 16 bytes (`u128`/`i128`, large by-value struct) →
  BOX-IN-ARENA (default).** The slot is one pointer (8 bytes); `null` = `NULL` =
  zero heap; non-null = one arena cell (`tk_region_alloc`) holding the payload.
  Variant-sized in the HEAP dimension (base §6.4/6.5, owner D8 = "A + hatch").
- **`#inline` escape hatch** forces the INLINE-TAG (`{ uint8_t tag; payload }`,
  in-frame, max-size, zero-indirection) on a proven-hot binding/field. GENERAL
  over any eligible `Variant` (`T|null`, `T|error`, `A|B|C`), not null-only
  (owner D10). Eligible = a `Variant` with a SATURATING max payload ≥ 16 bytes.

### `#inline` is a NEW parse site (precision — not just a name in the decl list)

The existing `#` channel (`parse_decl_attributes`, `parser/parse_decl.tks:760`)
is DECL-level (`#test`/`#os`/`#inject`/`#singleton`/`#must_free`). `#inline`
attaches to a **local binding** (`#inline mut hot: u128 | null = null`) and a
**struct field** (`#inline pos: Vec4 | error`) — two positions the decl-attribute
parser does not cover. This crumb adds a binding/field attribute channel:

- `src/parser/parse_stmt.tks` — accept a leading `#inline` before a `let`/`mut`
  binding; stamp a `is_inline_repr: bool` (or an attribute set) on the binding AST.
- the struct-field parser (`parser/parse_decl.tks` field loop) — accept `#inline`
  before a field decl; stamp the same flag on the field.
- `src/parser/parse_decl.tks:800` — extend the "unknown attribute" message to list
  `#inline` and its LEGAL POSITIONS (binding/field), so a mis-placed `#inline`
  (e.g. before `fn`) is a precise error, never a silent accept.

### Files touched

- `src/parser/parse_stmt.tks`, `src/parser/parse_decl.tks`, `src/parser/ast.tks`
  — the `#inline` binding/field attribute (new AST flag + parse sites + message).
- `src/checker/typer.tks` (or `resolve.tks`) — `inline_attr_eligible` (below),
  run at the binding/field where `#inline` appears; a bad application is the exact
  base §6.7 compile error.
- `src/codegen/codegen.tks` — the member-representation switch: default
  box-in-arena for saturating ≥16B (emit the 8-byte handle; alloc a cell on
  non-null construct; load-through on read); honor `#inline` by flipping THAT
  binding/field to the inline-tag struct. One switch serves local + field.
- `src/lir/lower.tks` + `src/backend/*` — the box handle's alloc/load in the LIR
  and own backend; the LIR-interp oracle models the handle as a boxed word.

### New / changed function shapes (copy-paste, full Javadoc)

```teko
/**
 * inline_attr_eligible — validate a `#inline` attribute on a binding or field.
 * Legal ONLY when `t` is a boxed-by-default sum: a `Variant` whose SATURATING max
 * payload is ≥ 16 bytes (e.g. `u128 | null`, `BigVal | error`). Rejects, each
 * with a distinct reason (never a silent no-op): a non-sum type; a niche-able sum
 * (ptr/ref/class handle — already optimal at one word); a self-recursive sum
 * (MUST box — an inline self-recursive sum has no finite size); and a sum whose
 * payload is < 16 bytes (already inline). General scope, owner-ratified (D10).
 *
 * @param t      the resolved type the attribute is applied to
 * @param table  the type table (recursion detection + size resolution)
 * @return       null when `#inline` is legal; an error naming the ineligible class
 * @since 0.3.0.30 (null-union C4)
 */
fn inline_attr_eligible(t: checker::Type, table: TypeTable) -> null | error

/**
 * union_repr_class — classify how a resolved union is stored: `Niche` (bare, C3),
 * `InlineTag` (`{uint8;payload}`, saturating ≤ 8B or `#inline`-forced), or
 * `BoxInArena` (8-byte handle, saturating ≥ 16B default). The single source of
 * truth both `emit_type` and the LIR lowering consult so the two engines agree.
 *
 * @param v         the resolved union type
 * @param forced_inline  true iff a `#inline` attribute rides this binding/field
 * @param table     the type table (size + recursion resolution)
 * @return          the representation class, or an error on a malformed member
 * @since 0.3.0.30 (null-union C4)
 */
fn union_repr_class(v: checker::Variant, forced_inline: bool, table: TypeTable) -> UnionRepr | error

/**
 * The physical storage class chosen for a resolved union type. Orthogonal to the
 * union's SEMANTIC category (reference-category per the ref-model digest) — this
 * is the layout dial only (base §6.4 Fact 4).
 *
 * @since 0.3.0.30 (null-union C4)
 */
pub type UnionRepr = enum { Niche; InlineTag; BoxInArena }
```

### Fixtures this crumb adds

- `examples/regressions/repr_box/` — **[size probe]** `sizeof(u128|null)==8`
  (boxed) with ZERO heap bytes allocated for the `null` case (a heap-probe
  counter); a `#inline`-annotated `u128|null` slot `==32` (inline); a `u128`
  value round-trips through the boxed handle. Exit 0 = all hold.
- `examples/regressions/inline_attr_reject/` — four `EXPECT_COMPILE_FAIL`
  fixtures, one per ineligible class (`i32|null` = already inline; `Node|null` =
  niche-able; a self-recursive `Tree|null` = must box; `mut n: i32` = not a sum),
  each asserting the exact §6.7 message text.
- `t/inline_attr_positions.tks` — `#inline` on a local AND on a struct field both
  parse and flip the layout; `#inline` before `fn` is the precise placement error.

### Ritual gate

Fixpoint (dormant machinery ⇒ byte-neutral) + size probe (box ==8, `#inline`
==32, zero heap when null) + dual-engine (the boxed handle behaves identically on
oracle and native).

---

## C5 — STRUCTURAL PIVOT: `null`→`Null`, universal narrowing, DESUGAR bridge. **L. BYTES-CHANGING for the corpus, BEHAVIOR-PRESERVING.**

**[RITUAL: fixpoint] + [RITUAL: size probe] + [RITUAL: dual-engine].**

**Goal.** Flip the compiler's OWN corpus onto the unified rail with UNCHANGED
source and UNCHANGED observable behavior. The bytes change (former `error?`
niche-fills 24→16, etc.) but semantics are identical — the fixpoint here proves
the pivot did not perturb behavior. This is the crumb where C3/C4's dormant
representation goes LIVE.

### Three moves

**(a) `null` literal born as `Null`.** Retire the `Optional{Void}` sentinel for
`NullLit`: `type_nulllit` (`typer.tks:42`) types a bare `null` as `Null{}`, not
`Optional{inner=Void}`. `null` then widens by ordinary set-membership
(`null_widens_into`, already present, `resolve.tks:834`). Keep `is_bare_null_value`
recognizing BOTH forms during the bridge (C7 deletes the `Optional{Void}` arm).

**(b) Universal narrowing invariant (base §3f).** Using a `T | null` (any union)
payload AS `T` — deref / field-access / method-call / value-use — is REJECTED
until narrowed, the SAME diagnostic family as an unmatched `T | error`. TWO
narrowing forms: exhaustive `match` (already works — `checker/match.tks` binds
each arm to its member) and the equality-guard flow-narrower (the ONE genuinely
new piece). The `Ref<T> | null` deref case (base §3d) is one instance: `r.value`
is rejected until `r` is narrowed to bare `Ref<T>`.

**(c) DESUGAR bridge — the transitional lowering that C6/C7 later retire.** So the
entire existing corpus runs on the unified rail without a source rewrite yet:
- at RESOLVE: `parser::OptionalType` `T?` → `Variant { members = [Null, T] }`
  (i.e. `T | null`); `error?` → `null | error`. (Replace the current
  `OptionalType → Optional{inner}` mapping at `resolve.tks:1556-1569` with the
  union lowering, GATED behind a bridge flag so C7 can delete cleanly.)
- at LOWER: `TCoalesce` (`x ?? y`) → `match x { null => y; T as t => t }`;
  `TSafeFieldAccess` (`x?.f`) → `match x { null => null; T as t => t.f }`;
  `TSafeMethodCall` → the method analog. (Route `lower_coalesce` /
  `lower_safe_field_access` / the method path — `lower.tks:611-612,2816,2838` —
  through the desugar instead of the legacy `tk_opt` sentinel branch.)

The bridge makes `T?` and `T | null` DENOTE THE SAME TYPE and emit the SAME
bytes, which is the precondition for C6's byte-identical corpus rewrite.

### Files touched

- `src/checker/typer.tks` — `type_nulllit` → `Null{}`; the narrowing invariant
  (reuse the existing sum-not-a-member guard, add `narrow_on_eq_guard`);
  `null_widens_into` already present.
- `src/checker/resolve.tks` — `T?`/`error?` desugar to `T|null`/`null|error` at
  the `OptionalType` resolve site (bridge-flagged).
- `src/lir/lower.tks` — `?.`/`??`/`?.m()` desugar to `match`/`if` (bridge-flagged).
- `src/checker/match.tks` — confirm the `Null` arm binds and exhausts (already
  present, `match.tks:425`).

### New / changed function shapes (copy-paste, full Javadoc)

```teko
/**
 * narrow_on_eq_guard — the one genuinely new checker piece (base §3f). If `cond`
 * is `x == null` or `x != null`, return the branch-scoped type overrides for `x`:
 * in the branch where `x` is statically known non-null, `x`'s type is the union
 * MINUS `null` (a bare member if only one remains); the other branch keeps the
 * full union. Any other condition returns the environment unchanged. The second
 * narrowing form (`match` arms) reuses existing variant machinery unchanged.
 *
 * @param cond  the resolved `if` condition expression
 * @param env   the current flow type-environment
 * @return      the (then-env, else-env) overrides, or the unchanged env
 * @see         match narrowing (checker/match.tks arm binding)
 * @since 0.3.0.30 (null-union C5)
 */
fn narrow_on_eq_guard(cond: checker::TExpr, env: FlowEnv) -> BranchEnvs

/**
 * desugar_optional_type — the transitional BRIDGE lowering (retired in C7): map a
 * parser `OptionalType` `T?` to the resolved union `T | null` (`error?` →
 * `null | error`), so the legacy sugar denotes exactly the unified rail. Gated by
 * `bridge_active` so C7 deletes the whole path once the corpus is rewritten (C6).
 *
 * @param inner  the resolved inner type of the `T?`
 * @return       the resolved `Variant { members = [Null, inner] }` (null-first)
 * @deprecated   transitional; deleted in C7 once no source uses `T?`
 * @since 0.3.0.30 (null-union C5)
 */
fn desugar_optional_type(inner: checker::Type) -> checker::Type

/**
 * require_narrowed — reject a use of a `T | null` (any union) payload AS a bare
 * member before it has been narrowed, with the SAME diagnostic family as an
 * unmatched `T | error`. Reuses the existing sum-not-a-member guard; the only new
 * behavior is recognizing the two accepted narrowing scopes (a `match` arm, and
 * the flow-narrowed branch of an equality guard).
 *
 * @param use_site  the resolved expression using the value
 * @param val_type  the resolved (possibly still-union) type of the value
 * @param narrowed  true iff `use_site` is inside a narrowing scope for this value
 * @return          null when the use is legal; an error demanding narrowing
 * @since 0.3.0.30 (null-union C5)
 */
fn require_narrowed(use_site: checker::TExpr, val_type: checker::Type, narrowed: bool) -> null | error
```

### Fixtures this crumb adds

- `t/null_narrow_match.tks` — bare payload use rejected; `match`-narrowed use
  compiles and runs (dual-engine).
- `t/null_narrow_ifguard.tks` — `if x != null { use(x) }` compiles (flow-narrowed);
  using `x` outside the guard is rejected.
- `t/ref_null_narrow.tks` — `r.value` on `Ref<T> | null` rejected until narrowed;
  narrowed deref compiles (base §3d safety-leak fence).
- `t/desugar_equiv.tks` — a `T?` fn and its hand-written `T | null` twin emit
  BYTE-IDENTICAL C (the precondition for C6).
- `examples/regressions/error_union_pivot/` — **[size probe]** a former `error?`
  site now `sizeof == 16` (niche), round-trips success (`null`) and failure
  (`error`) on both engines. Exit 0.

### Ritual gate

Fixpoint (the whole corpus now on the unified rail; `gen1==gen2` proves the
pivot is behavior-preserving) + size probe (former `error?` = 16B) + dual-engine.

---

## C6 — mechanical corpus + stdlib source rewrite. **L (the biggest churn). BEHAVIOR-PRESERVING + BYTE-IDENTICAL.**

**[RITUAL: fixpoint] + [RITUAL: byte-identical corpus rebuild vs C5].**

**Goal.** Rewrite every `src/` site from the legacy spelling to the canonical
`T | null` spelling. Because C5 made the forms SEMANTICALLY IDENTICAL and
BYTE-IDENTICAL in emission (the desugar bridge), this rewrite changes SOURCE
SPELLING ONLY — the emitted bytes are unchanged, so the corpus rebuild must be
byte-identical to its C5 output. That equality is the safety proof of the churn.

### Which spellings change (mechanical, ordered by risk)

| Legacy spelling | Canonical rewrite | Count / locus |
|---|---|---|
| `-> error?` | `-> null \| error` | **77** signatures across `src/` (e.g. `typer.tks:1276`, `collect.tks:911`) |
| field/param `T?` | `T \| null` | every `OptionalType` position |
| `x ?? y` | `match x { null => y; T as t => t }` (or `if x != null { … }`) | every `Coalesce` |
| `x?.f` | `match x { null => null; T as t => t.f }` | every `SafeFieldAccess` |
| `x?.m(a)` | `match x { null => null; T as t => t.m(a) }` | every `SafeMethodCall` |

Canonical member order is **`null` first** (`null | error`, `null | i32`) —
enforced already by `union_normalize_null` (`resolve.tks:1461`), so a rewrite that
writes either order still resolves to one canonical type + one mangle. Write
`null | error` for source-review consistency.

### How to do it safely (mechanical + fixpoint-gated)

1. Rewrite in DEPENDENCY ORDER (leaves first: stdlib `io`/`fs`/`text` before
   `checker`/`codegen`), so each sub-rewrite rebuilds green under the C5 seed.
2. After EACH file (or small batch), rebuild and assert **byte-identical to the
   C5-output** artifact (a recorded gen2 hash). A byte difference means the
   rewrite changed semantics (a bug) — STOP and inspect, never accept a diff.
3. The `?.`/`??` → `match`/`if` rewrites are the ONLY ones that change AST shape;
   do those last and per-site, each behind its own byte-identity check. A `?.f`
   that discards the `null` case (propagates `null`) must map to the `null => null`
   arm EXACTLY — the desugar in C5 is the reference semantics.
4. `#inline` is NOT added by this rewrite (representation is unchanged); only
   spelling changes. Any hot-path `#inline` is a SEPARATE, later, opt-in decision.

After C6, NO `src/` source uses `T?` / `error?` / `?.` / `??` / `?.m()`.

### Files touched

Broadly `src/` (all 77 `error?` sites + every `T?`/`?.`/`??` site). Concentrated
in `src/checker/*` (the `error?`-heavy fallible signatures) and the stdlib
(`src/io`, `src/fs`, `src/text`, `src/encoding`, `src/collections`).

### Fixtures this crumb adds

- `examples/regressions/error_union_migration/` — a representative `null | error`
  fn round-trips success (`null`) and failure (`error`) on BOTH engines,
  `EXPECT_EXIT` distinguishing the two paths.
- The **byte-identical corpus rebuild** is itself the primary gate: gen2(C6) ==
  gen2(C5) for the whole `src/`, plus `gen1==gen2` fixpoint on the C6 tree.

### Ritual gate

Fixpoint + byte-identical-vs-C5. If the whole-corpus rebuild is not byte-identical
to C5, the rewrite introduced a semantic change — a hard FAIL.

---

## C7 — DELETE the sugar + the `Optional` former; sentinel cleanup; final gate. **M (net deletion). BEHAVIOR-PRESERVING + BYTE-IDENTICAL.**

**[RITUAL: fixpoint] + [RITUAL: full gate].**

**Goal.** With no user remaining after C6, delete the superseded forms and the
desugar bridge, retire the legacy sentinel, and run the whole-suite ratified gate.
This is REMOVED code that OFFSETS the C1/C3 additions (base §7).

### What gets deleted (each has zero users after C6)

- `parser/ast.tks:46-49` — `SafeFieldAccess`, `Coalesce`, `SafeMethodCall` from
  `ExprKind` (`ast.tks:97`). Keep `NullLit` + `NullPattern` (union atoms).
- `parser/parse_type.tks:64-68` — the postfix `?` / `OptionalType` former (whole).
- `parser/parse_expr.tks` — the `?.` / `??` / `?.m()` parse paths.
- `lir/lower.tks:611-612,2816,2838` — `lower_coalesce`, `lower_safe_field_access`,
  the safe-method path, AND the C5 desugar bridge (`desugar_optional_type`,
  `bridge_active`).
- `checker/type.tks:71,102` — the `Optional` case from `pub type Type = variant …`
  and its arms in `type_eq` (`type.tks:142`), `subst_type` (`type.tks:1359`),
  `type_contains_ref` (`type.tks:183`), `type_mangle`/`type_render`
  (`type.tks:1619,1775`), `unify`/`collect_sig_type_params`
  (`resolve.tks:1414,1430`), `unsafe_carrying_at` (`resolve.tks:1055`).
- `checker/resolve.tks` — the `Optional`-over-`Reference` reject (the old
  `resolve.tks:1519` arm, now that R2 is the shape-aware C3 helper), and the
  `Optional{inner}` mapping (`resolve.tks:1556-1569`).
- `codegen/codegen.tks` — the `tk_opt_*` former paths (`cg_opt_typename` ~1095,
  the `emit_type_decl` optional branch ~3857, the `TEXPR_NULL` present-wrap ~3357)
  once the unified `Variant` path is the sole representation. NOTE: `cg_opt_mangle`
  / `cg_opt_key` are ALSO the SLICE/union key-mangler (`codegen.tks:1108,1203`) —
  do NOT delete those; only the OPTIONAL-specific `tk_opt_<inner>` STRUCT emission.

### Sentinel cleanup (base §5, base Crumb 8)

- Delete the `Optional{inner=Void}` bare-null sentinel arm from `is_bare_null_value`
  (`resolve.tks:827-828`), `type_has_sentinel` (`resolve.tks:1277`),
  `type_has_void_sentinel`, and `type_nulllit`'s legacy path (already flipped in
  C5). Confirm the SEPARATE empty-collection `Slice{Void}` sentinel
  (`list::empty()` inference) is UNTOUCHED — it is a different mechanism (base §5).

### Fixtures this crumb adds

- `examples/regressions/no_optional_former/` — grep-gate + `EXPECT_COMPILE_FAIL`:
  a postfix `T?`, a `?.`, a `??` each no longer parse (each is the base §4 parse
  error message); the corpus still builds.
- `t/empty_slice_sentinel.tks` — `list::empty()` still infers into a typed slice
  (proves the `Slice{Void}` sentinel survived the `Optional{Void}` deletion).
- W15 Javadoc audit: every declaration touched across C3-C7 carries a full
  Javadoc doc-comment (fn/type/member, pub + private).

### Ritual gate

Fixpoint (final ratified end-state; `gen1==gen2`) + FULL GATE (whole `teko test .`
+ every `scripts/*_regressions.sh` leg `completed + success`, VM-oracle + native).

---

## Interaction notes with adjacent .30 work

### Interface-in-union (#28 "S4" carve-out) — `docs/design/interface-value-type.md`

- **The interface fat pointer is a null-union NICHE MEMBER, not a second niche
  path.** `Iface | null` niche-fills to the bare fat pointer with `.data == NULL`
  meaning `null` (interface-value-type.md §2.3). C3's `cg_union_niche_member` is
  the SINGLE niche authority — the interface family must join it verbatim; the
  keystone implementer MUST NOT invent a parallel niche. Recommend C3's niche
  classifier be written to recognize an interface `Named` fat-pointer shape as a
  niche member ALREADY (dormant until interface-in-union relaxes the resolve gate),
  so the keystone only relaxes the GATE, never adds a rep path.
- **The one true overlap is the variant-member gate.** C3's
  `variant_member_admissible` REPLACES the flat rejections at `resolve.tks:1519-1523`.
  It relaxes `Reference` (R2) but MUST LEAVE the interface honest-stop
  (`resolve.tks:1523`) INTACT — the S4 carve-out (interface-value-type.md §5-S4)
  deliberately pinned a compile-fail fixture there so null-union "must consciously
  decide the interface-in-union arm rather than silently flip it." C3's decision:
  keep rejecting `Iface | …` here; interface-in-union rebases ON TOP of the
  null-union base and relaxes that arm itself (merge-order contract,
  interface-value-type.md §6: null-union base first, keystone rebases). Write
  `variant_member_admissible` so adding the interface arm later is a one-line
  relaxation, not a rewrite.
- **`Ref<Interface>` / class-null** are unaffected by null-union (the `Reference`
  inner is the never-null fat pointer; a null `.data` is a class-null concern,
  interface-value-type.md §2.3).

### Regressives full-stack (sibling architect, same .30 wave)

- Null-union is a REPRESENTATION change to the most pervasive fallible signature
  (`error?` → `null | error`, 77 sites) and to every pointer/class optional. The
  regressives full-stack MUST cover the new shapes so no future wave silently
  regresses layout. Concretely, feed the sibling these fixtures for the full-stack
  regression corpus: `repr_niche`, `repr_box`, `error_union_migration`,
  `ref_null_admit`, `no_optional_former` (all authored here). The size-probe
  fixtures in particular are the throughput-doctrine tripwire (base §6.3 is a HARD
  gate) and belong in the standing regression set, not just this wave's gates.
- Merge-order: null-union C3-C7 and the regressives full-stack touch overlapping
  gate infrastructure (`scripts/*_regressions.sh`, `examples/regressions/`). The
  fixtures authored here are ADDITIVE directories; coordinate the sibling to
  INCLUDE (not re-author) them, and to run the size probes on every backend lane
  (`scripts/native_regressions.sh` + `scripts/diff_c_own.sh`).

---

## Risks / law tensions

**No unresolved HALT.** The base design (`null-as-union-type.md` §9) is
owner-ratified and converged (D2-D10). This progression is the sequenced
implementation of that ratified plan; it introduces no new design question. The
risks below are all resolved law-first or by sequencing.

- **R-1 — niche vs the arena / value-semantics model (the flagged risk).** A niche
  `X | null` where `NULL` means `null` interacts with the ref-model tripartite
  categories (ref-model digest: optionals are "reference category,
  capability-passing"). RESOLUTION (base §6.4 Fact 4): representation
  (niche/inline/box) is a PURELY PHYSICAL dial, ORTHOGONAL to the semantic
  category. A niche `ClassRef|null` is still reference-category semantically; the
  niche only removes the tag word. The arena is untouched: niche allocates nothing
  (the word is the value's own), box-in-arena uses the EXISTING `tk_region_alloc`
  bump path (mem-model-empirical: leak-to-root batch-safe). Niche-safety is proven
  (base §6.2: M.1 never returns NULL; live `str`/`error` `.ptr` never NULL). NO
  tension — but the size-probe rituals (C3/C4/C5) are the HARD gate that keeps it
  honest: any build shipping the 4-byte-enum-tag intermediate is FORBIDDEN
  (throughput doctrine, base §6.3).
- **R-2 — R2 (references never null) relaxation lands in C3, not the .29 base.**
  The as-built base did NOT relax the `Reference` variant-member gate (it still
  rejects at `resolve.tks:1519`). C3 carries it (owner D3 ratified the relaxation).
  RESOLUTION: the UNIVERSAL NARROWING INVARIANT (C5, base §3d safety-leak proof)
  is the fence — `Ref<T> | null` cannot be dereffed until narrowed to bare
  `Ref<T>`, so the bare-ref R2 promise stays sound. C3 admits the SHAPE; C5 adds
  the fence. Sequencing note: `ref_null_admit` (C3) tests resolution only;
  `ref_null_narrow` (C5) tests the fence. Both required.
- **R-3 — `#inline` is a new parse site, not a decl-attribute add.** Under-scoping
  it as "another name in `parse_decl_attributes`" would miss the binding/field
  positions (C4 §"NEW parse site"). RESOLUTION: C4 explicitly adds the
  binding/field attribute channel + the precise mis-placement error. Flagged so
  the implementer does not shortcut it.
- **R-4 — C6 corpus rewrite risk (77+ sites).** RESOLUTION: the rewrite is
  BYTE-IDENTITY-GATED against C5 output — any semantic drift shows as a byte diff
  and hard-fails. Mechanical, dependency-ordered, per-batch verified. Churn, not
  risk (base §7).
- **R-5 — `cg_opt_mangle`/`cg_opt_key` double duty.** These mangle SLICE and union
  keys too, not just optionals (`codegen.tks:1108,1203`). C7 must delete ONLY the
  `tk_opt_<inner>` STRUCT emission, NOT the shared key-mangler. Flagged in C7 so a
  broad delete does not break slice/union mangling.

RATIFIED base, sequenced implementation — no HALT. §C3-C7 is the build order.
