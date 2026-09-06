---
section: design
version: 0.3.1.0-serial-tags-r2
created: 2026-08-14
status: DESIGN — mechanics proposal for the SEALED serialization-tags feature. Read-only over
        product code; this file is the SOLE edit. NO build, NO reseed, `teko test` NOT run in any
        form (the `monomorph` leak crashes the container). Work happened in an isolated git worktree
        off `origin/fix/retirement`; the main checkout was NOT touched.
role: architect — turn the owner's SEAL (catalog §D) + the owner's PRECISION (per-type synthesized
      routine, below) into a fully-specified, ratifiable mechanic set with 3+ concrete alternatives +
      code for every OPEN point, and a recommendation each.
owner-precision (2026-08-14, centre the design on this): the backtick tags **orient the compiler to
      CONSTRUCT / SYNTHESIZE a dedicated per-TYPE parser+serializer ROUTINE in the AST** at compile
      time — a generated `parse_T` / `serialize_T` — **used at runtime**. It is NOT runtime reflection
      (the compiler already built the parser), and NOT inline expansion scattered across call-sites: it
      is **ONE generated routine per type, embedded in the AST**, running with zero reflection (M.0).
      Pipeline: **INPUT (comptime field reflection: iterate fields+tags) → SYNTHESIS (the compiler
      assembles the per-type routine into the AST) → PRODUCT (the runtime routine every call-site
      calls).**
owner-amplification (2026-08-14, catalog §D commit 01b0612e): beyond **type-first** (field↔tag; the
      type is the schema) there is a **schema-first** at the TYPE LEVEL — a type-level backtick tag
      referencing external schema(s), multi via `;` (`json="a.json;b.json" xml="a.xsd;b.xsd"`). The
      compiler READS the schemas at compile time and reflects them into the SAME per-type synthesis
      (schema validates/drives the shape; field tags map names/options — the two COMPOSE). XML
      namespace prefixes are DYNAMIC + UNTRUSTED: the synthesized parser resolves by namespace **URI,
      never by prefix**. A namespace URI is a logical id: public/resolvable, else the compiler infers a
      **local path**. See §3.5 (grammar) + §4.7 (read/reconcile/URI-resolution).
seals-consumed: catalog §D (HEAD 9ce8fca3) · §9.D union-com-null (o TIPO É o schema) · §14.1 macro /
      §14.2 comptime (plano-macro.md two-family SEAL — §14.2 is the comptime LAW) · §4.1 `.{}` · exp/pub.
---

# Serialization tags (Go-style backticks) → a per-type synthesized parser/serializer

> **The shape, precisely (owner precision).** A backtick tag on a field is a **marking spec** the compiler
> reads at compile time to **SYNTHESIZE a dedicated routine per type** — `serialize_User(u): str` and
> `parse_User(s): User | error` — **materialized as real functions in the AST** and **called at runtime**
> by every `json::encode(u)` / `json::decode<User>(s)` site. There is **zero runtime reflection** (Law
> M.0): the compiler has *already built* the parser; runtime just calls it. There is **no inline expansion
> scattered per call-site**: there is ONE routine per (type, format), memoized, that all call-sites share.
>
> **The three stages, kept distinct throughout this doc:**
>
> | stage | what | mechanism | where it lives |
> |---|---|---|---|
> | **INPUT** | iterate `T`'s fields → `{ name, type, tag }` at compile time | **comptime field reflection** (§2) | compile-time value, never survives |
> | **SYNTHESIS** | assemble `serialize_T` / `parse_T` from the field view + tags | a compiler pass (§4 — the open question) | rewrites the AST once per (T, format) |
> | **PRODUCT** | the generated routine every call-site invokes | an ordinary runtime `fn` in the AST | emitted like any function |
>
> **Nullability comes from the field TYPE** (§9.D `T | null`) — the type IS the schema; JSON gets a
> `,catchall`; XML maps null → element/attribute presence + `xsi:nil`.
>
> **What this is NOT.** Not a crumb plan. It is **DESIGN-AHEAD** — blocked on the macro/comptime facility
> (`plano-macro.md`) being *implemented* and on `T.fields` reflection (that doc's Extent-3, owner-pending).
> §5.4 marks exactly what is blocked vs designable now.

---

## 0. How to read this doc

Every OPEN mechanic is presented as **3+ complete, fully-specified alternatives, each with a Teko example
(declaration + call site + what it achieves)**, then a **recommendation**. SEALED points are decisions, not
reopened. Zero bare open questions: the genuine owner-ratifiable extension (§4.2, §6) is framed as staged
options with a recommendation and a law-first resolution, never a HALT.

---

## 1. RECON — the current state (file:line)

### 1.1 How a struct field is stored today (name / type / visibility)

- **The field node.** `pub type Field = struct { name: str; type_ann: TypeExpr; vis: Visibility;
  is_intern: bool; has_doc: bool; doc: str; is_readonly: bool }` — `src/parser/ast.tks:643`. It carries the
  source `TypeExpr` (unresolved), visibility, readonly bit, doc. **No slot for a tag today.**
  `StructBody = struct { fields: []Field; methods; implements; consts; is_readonly }` — `ast.tks:618`.
- **Where fields are parsed** (three `Field { … }` sites + one rebuild — the tag-parse blast radius):
  - `parse_fields` (struct body) — `src/parser/parse_decl.tks:873-917`; field built at **`:905`**, right
    after `parse_type` returns `ty`, before the separator check (`p = ty.next`). **The insertion point for
    an optional trailing tag.**
  - `parse_class_fields` (class body, default-private) — `:939-976`; field built at **`:974`**, same spot.
  - a third field-push at **`:1219`**.
  - `freeze_all_fields` — `:1243-1249`: rebuilds every `Field { … }` for `readonly struct`; **must copy any
    new `tag`** or a readonly struct loses it.
- **The resolved field enumeration ALREADY EXISTS (the INPUT keystone to reuse).** `pub type FieldView =
  struct { name: str; type: Type }` — `src/checker/collect.tks:1618`; `resolve_field_view` (`:1631`),
  `resolve_field_views` (`:1648`), `pub fn deriver_field_view(type_name: str, table: TypeTable):
  []FieldView | error` (`:1673-1687`) already walk a struct/class's fields (a class through its full base
  chain via `effective_class_fields`; a struct's trait-flattened body) and resolve each annotation to a
  **real `Type`**. **The synthesis INPUT (§2) is a thin projection over this M.0-honest machinery.**

### 1.2 Where the backtick enters the lexer/parser

- **The backtick is a FREE lexical slot (confirmed).** `read_symbol` (`src/lexer/lexer.tks:634-702`) has no
  arm for `` ` `` (0x60): it falls to `_ => return scan_err_at(source, pos, "unexpected character")` at
  **`:699`**. So a backtick is an error today — no corpus uses it → adding it is **additive-inert**.
  `next_token` (`:706-720`) dispatches; a backtick check slots **before** `read_symbol` at `:719`.
- **Token enum** — `src/lexer/token.tks`. New kinds are APPENDED LAST (ordinal of a stored operator must
  never shift — `kind_byte` only serializes true operators; `:95-102` shows `To`/`Dot`/`Semicolon`/
  `Variant`/`Pub` all appended last for this reason). A `Tag` token is appended last — never a stored op.
- **Verbatim-string precedent to copy.** The `@"…"` raw-string scanner (`read_string_body`,
  `lexer.tks:453-551`, `has_verbatim` path) is the template for a no-escape raw run — a backtick tag is
  "raw bytes to the closing backtick, no escapes," i.e. a stripped `read_string_body` with delimiter `` ` ``.

### 1.3 The reflection state today (Law M.0 — zero reflection)

- **Runtime reflection = zero, by law.** `collect.tks:1608-1612` states it on `FieldView`: "no surface
  syntax reaches this shape, and it carries no runtime value or metadata (**Law M.0 — zero reflection**)."
  The retired **structural traits** (`Eq`/`Ord`/… — synthesized bodies the dev never saw) were removed for
  being a compiler shadow; **no-shadow** is standing law (surface doc §9.4). §6 tension #2 reconciles the
  owner's *deliberate* per-type synthesis with no-shadow.
- **Type reflection that exists is comptime-only + scalar.** `@sizeof<T>()`/`@typename<T>()` reflect
  `T.size`/`T.name` (plano-macro.md §B.2, SEALED). Structural `T.fields` is that doc's **Extent-3 —
  owner-pending** (its one owner-pending sub-decision), guarded so `.fields` "may only feed comptime VALUES
  … never synthesize a runtime read of a field VALUE." **This doc is the concrete driver that ratifies
  Extent-3, and shows the guardrail holds** (§2.4, §6).

### 1.4 How comptime evaluates + inlines today (§14.2, Family B — the comptime LAW)

- **Two-family macro SEAL** (`plano-macro.md`): **Family A `macro`** expands the *untyped* AST **before**
  type-check (`lowering`/`${}`, stable-mangle); **Family B `comptime`** runs **after** type-check, args are
  typed values, result **inlined as a literal** (`literal_of`). Both `@`-called; a bare call is a runtime fn.
- **Family B engine (mandated).** `eval_const(e, table, env, agg): ConstValue | error` —
  `comptime_fold.tks:306`; `literal_of(v, ty, line, col): TExpr | null` — `:1997`; `ConstValue`
  (`CVInt|CVFloat|CVBool|CVBytes|CVAgg`) — `:18-36`. Expand slot: `expand_comptime(prog: TProgram):
  TProgram | error` at **`project.tks:367`**, after `monomorph` (`:353-354`), before `inline_consts`.
- **The precedent for SYNTHESIS.** `monomorph` (`project.tks:353-354`) already **synthesizes new named
  functions** (specialized generic instances) into the TProgram post-type-check and memoizes them — exactly
  the pattern the per-type serializer synthesis follows (§4.1). `#os` pruning (`project.tks:116-126`) is the
  program-transform-pass precedent.

---

## 2. INPUT — comptime field reflection (feeds the synthesis)

The synthesis needs one compile-time capability: *iterate `T`'s fields* → `{ name, type, tag, vis }`. This
is the **input**, never the product. Below are **three concrete forms**, then the recommendation. (How the
iteration's result becomes a routine in the AST is §4 — kept separate on purpose.)

### 2.1 Form A — `@fields<T>()` comptime intrinsic (a comptime list of descriptors) — **RECOMMENDED input**

```teko
/**
 * FieldInfo — one field of a type's COMPTIME field view: declared name, raw backtick tag ("" when
 * absent), whether the declared type is a `T | null` union (§9.D), and visibility. Compile-time-only:
 * it carries NO runtime value and never survives to the emitted program (Law M.0). It is the bounded
 * projection of the checker's existing `FieldView` (`collect.tks:1618`), extended with the tag and the
 * nullability predicate the synthesis needs.
 *
 * @field name        the field's declared name
 * @field tag         the raw backtick tag body ("" when untagged)
 * @field is_nullable true iff the declared type is a `X | null` union (§9.D)
 * @field vis         the field's visibility
 * @since 0.3.1 (serialization tags)
 */
exp type FieldInfo = struct { name: str; tag: str; is_nullable: bool; vis: parser::Visibility }

/**
 * fields — Family B comptime reflection: the compile-time field view of `T`, one `FieldInfo` per field
 * in declaration order. A comptime VALUE (descriptors only) — it never reads any field's runtime VALUE,
 * so M.0's guardrail holds. Reuses `deriver_field_view` (`collect.tks:1673`); `is_nullable` is read off
 * the already-resolved `Type`. It is the INPUT the synthesis pass (§4) consumes — never emitted itself.
 *
 * @return  the field descriptors of `T`, as a comptime aggregate
 * @since 0.3.1 (serialization tags)
 */
exp global comptime fields<T>(): []FieldInfo { T.fields }
// consumed by the synthesis pass:  @fields<User>()  →  [ {name="id", tag="json:\"id\" xml:\"@id\"", …}, … ]
```

- **Unlocks:** the whole synthesis — the pass reads the descriptors, parses each tag (§3), and assembles
  the routine (§4). **Cost:** one comptime intrinsic (the honest minimum) over existing checker machinery.
  **Recommended input** because it is a pure, bounded, comptime VALUE — the synthesis is a *reader* of it.

### 2.2 Form B — `comptime for f in @fields<T>() { … }` (an unrolling iterator the SYNTHESIS uses internally)

A `comptime for` unrolls a comptime list at compile time. **It is not the output model** (the owner rejected
inline-per-call-site); it is the **loop the synthesis pass runs internally** to assemble the ONE routine's
body — the compiler walks each field once and appends that field's fragment to the routine it is building.

```teko
/**
 * build_serialize_body — SYNTHESIS-internal: the compiler walks `@fields<T>()` with a `comptime for` to
 * ASSEMBLE the body of the single `serialize_T` routine (NOT to inline at a call-site). Each iteration
 * appends one field's rendering fragment to the routine under construction; nullability (§9.D) and tag
 * options are decided per-field at compile time, so the finished routine is branch-baked straight-line.
 * Shown as the mechanism the §4 pass uses; the dev does not write this.
 *
 * @param T  the type whose serializer body is assembled
 * @since 0.3.1 (serialization tags)
 */
comptime for f in @fields<T>() {                      // the COMPILER's loop, once per field, at synthesis
    const spec = @parse_json_tag(f.tag, f.name)        // comptime tag parse (§3)
    comptime if !spec.skip {
        comptime if f.is_nullable && spec.omitempty {
            emit { if v.<f.name> != null { out = out ++ sep ++ quote(spec.name) ++ ":" ++ render(v.<f.name>) } }
        } else {
            emit { out = out ++ sep ++ quote(spec.name) ++ ":" ++ render(v.<f.name>) }
        }
    }
}
// result: the fragments accumulate into ONE  serialize_User(v)  routine (§4.3), not N inline expansions
```

- **Unlocks:** the visible, hand-readable assembly the synthesis performs. **Cost:** the `comptime for` /
  `comptime if` control (post-type-check, so `f.is_nullable` is real — §9.D). **Used by** §4's pass; not the
  output model on its own.

### 2.3 Form C — `#derive`-style compiler visitor with NO visible driver

The compiler synthesizes `serialize`/`parse` purely from an attribute, with no visible field-walk anywhere.

```teko
/**
 * User — Form C: a `#derive` asks the compiler to synthesize serialize/parse from tags with NO visible
 * driver at all (not even a library `encode<T>`). REJECTED-as-sole-surface: the SYNTHESIS is fine (the
 * owner wants it), but the *trigger* must be a visible, explicitly-called stdlib `encode<T>`/`decode<T>`
 * — an invisible auto-derive is the retired structural-trait shadow (surface doc §9.4).
 *
 * @since 0.3.1 (serialization tags) — REJECTED as the trigger; the synthesis itself is kept (§4)
 */
#derive(json)
exp type User = struct { id: u64 `json:"id"` }
// REJECTED trigger:  user.encode()  auto-appears with no visible call surface
```

- **Why REJECTED (as the *surface*):** an invisible auto-derive re-creates the no-shadow violation the
  project retired. **Kept:** the compiler DOES synthesize the routine — but triggered by a **visible,
  explicitly-called** `json::encode<T>`/`decode<T>` (§4), whose result is fully determined by the visible
  tags+types. That is the owner's per-type synthesis without the shadow.

### 2.4 Recommendation + how the input honors M.0

**Recommend Form A (`@fields<T>()`) as the INPUT, consumed via Form B's `comptime for` inside the synthesis
pass (§4); reject Form C's invisible auto-derive as the surface.** The invariant: **reflection is a COMPTIME
value (names/types/tags); the synthesized routine reads only field VALUES by direct static access.** The
`@fields` list, the `comptime for`, and every `v.<name>` resolution evaporate at synthesis — the emitted
routine holds no `FieldInfo`, no tag string, no type metadata, no reflective dispatch. This is
`@sizeof`/`@typename` extended from scalars to the field list.

---

## 3. Per-format tag grammar (Go-style, parsed in comptime)

The tag is a raw backtick run of space-separated, format-namespaced specs; each format owns its value
mini-grammar. The tag body is a compile-time `str` (`Field.tag`), so every parse runs in `eval_const` —
**the runtime program contains no tag string and no tag parser** (the synthesis consumed them).

```
tag        := '`' spec (WS spec)* '`'
spec       := format ':' '"' body '"'
format     := 'json' | 'xml' | 'csv'
```

### 3.1 JSON body
```
json_body  := ( name | '-' | '' ) ( ',' opt )*
opt        := 'omitempty' | 'catchall' | 'string'
```
- `name` — the key (defaults to the field name when empty). `-` — skip entirely. `,omitempty` — omit a
  **nullable** field that is `null` (§9.D; no-op on non-null). `,catchall` (empty name) — the catch-all sink
  (§4.5): the field's type must be `str | null` or `Json`. `,string` — encode a number/bool as a JSON string.

### 3.2 XML body
```
xml_body   := ( '@' name | '>' path | name ) ( ',' xopt )*
xopt       := 'attr' | 'chardata' | 'cdata' | 'omitempty'
name        := ( ns ':' )? local
```
- `name` — element; `@name` — attribute; `ns:local` — namespaced; `>path` — nested path; `,chardata`/`,cdata`
  — the element's text/CDATA. **Nullability → presence:** a null nullable field is **omitted** (element/
  attribute absent), or emitted as `xsi:nil="true"` when the parent opts into nil-signalling. **Order** =
  field declaration order.

### 3.3 CSV body
```
csv_body   := ( col | idx | '-' ) ( ',' copt )*
copt       := 'omitempty'
```
- `col` — header cell name; `idx` — fixed 0-based column index; `-` — skip. Column **order** = declaration
  order unless `idx` overrides. Null nullable → empty cell; non-null with empty cell on decode → error.

### 3.4 The comptime tag parser
```teko
/**
 * parse_json_tag — Family B comptime tag parser: split the raw backtick body, pick the `json:"…"` spec,
 * fold it to a comptime `JsonSpec` (name, skip, omitempty, catchall, string). Runs in `eval_const`; the
 * tag is a compile-time literal, so NO tag string and NO parser reach runtime — the SYNTHESIS consumes it.
 *
 * @param tag       the raw backtick body ("" when untagged)
 * @param fallback  the field's declared name (used when the json name is empty)
 * @return          the parsed json spec, as a comptime aggregate
 * @throws          a comptime diagnostic on a malformed spec
 * @since 0.3.1 (serialization tags)
 */
exp global comptime parse_json_tag(tag: str, fallback: str): JsonSpec { /* eval_const string walk */ }
// used at synthesis:  @parse_json_tag("json:\"email,omitempty\"", "email")  →  {name="email", omitempty=true}
```
Three placements for *when* the tag is parsed: **(a)** at lex time (rejected — bakes format grammars into
the lexer); **(b)** at parse time onto `Field` (rejected — same coupling); **(c) RECOMMENDED** — lexer/parser
keep the tag an **opaque raw `str`**, each format parses its own spec in **comptime**. (c) keeps the language
format-agnostic (M.0-small), lets libraries define new formats' tag grammar, and parses at compile time.

### 3.5 The TYPE-LEVEL tag — schema-first (owner amplification)

A backtick tag may also sit **at the type level**, referencing external schema(s) and a validation mode. The
two positions are disambiguated by the separator — **field-level uses `format:` (name/option mapping),
type-level uses `format=` (schema reference)**:

```
type_tag   := '`' tspec (WS tspec)* '`'
tspec       := format ('=' '"' schemaref '"' | 'mode' '=' '"' mode '"')
schemaref   := path (';' path)*          // multi-schema, ';'-separated
mode        := 'validate' | 'ugly'       // opt-in validation (§4.7.2); default = ugly (zero-cost)
```

```teko
/**
 * A — schema-first: the TYPE-LEVEL tag binds external schema(s) (JSON-Schema for `json=`, XSD for
 * `xml=`, multi via `;`) plus a validation `mode`. The compiler READS the schemas at compile time and
 * reflects them into the SAME per-type synthesis (§4.7): the schema validates/drives the shape, the
 * FIELD-level tags map names/options — the two COMPOSE. `mode="validate"` bakes conformance checks into
 * the synthesized parser; `mode="ugly"` (default) omits them entirely (zero runtime cost).
 *
 * @since 0.3.1 (serialization tags — schema-first)
 */
type A = struct `json="schema.json;schema2.json" xml="a.xsd;b.xsd" mode="validate"` {
    id:   u64        `json:"id" xml:"@id"`
    name: str        `json:"name"`
}
// type-level `format=` binds a schema;  field-level `format:` maps names/options — they COMPOSE at synthesis
```

The type-level tag rides on `StructBody` (or `TypeDecl`) as an opaque raw `str`, parsed in comptime by the
same per-format comptime helpers (§3.4-c). It composes with field tags: field tags always map names/options;
the schema, when present, adds validation + drives XML namespace/order (§4.7).

---

## 4. SYNTHESIS → PRODUCT — how the per-type routine is materialized in the AST

This is the centre of the owner's precision: the compiler **builds one `serialize_T`/`parse_T` routine per
(type, format), inserts it into the AST, memoizes it, and points every call-site at it**. §4.1 answers the
open question — *how the synthesis materializes the routine* — with three options and a recommendation;
§4.3–§4.5 fix the routine's content (nullability, XML presence, catch-all).

### 4.1 The open question — where/how the routine is synthesized (3 options)

The tags need the field's **resolved TYPE** for nullability (`T | null`, §9.D). Resolved types exist **only
after type-check**. That single fact orders the options.

#### Option (a) — Family A `macro` that enlarges the AST with the routine via `lowering { … }`

```teko
/**
 * serialize (Option a) — a Family-A `macro` PASTES a `serialize_T` routine into the AST via `lowering`.
 * REJECTED: Family A runs BEFORE type-check (plano-macro §A.0), so the field TYPES are unresolved — the
 * macro cannot tell `str` from `str | null`, so it cannot honor nullability-from-type (§9.D). It also
 * pastes per expansion site rather than emitting one shared routine. Shown to be ruled out on §14 grounds.
 *
 * @since 0.3.1 (serialization tags) — REJECTED (pre-type-check: no types → no nullability)
 */
macro serialize(T) { lowering { fn serialize_${T}(v: ${T}): str { /* cannot see T|null here */ } } }
```
- **Verdict: REJECTED.** Pre-type-check ⇒ no resolved types ⇒ cannot implement `T | null` nullability, the
  schema itself. Decisive under §14.1.

#### Option (b) — Family B `comptime` that generates and inlines the routine body per type

```teko
/**
 * encode (Option b) — a `comptime`-driven generic whose body is generated per monomorphized `T` from
 * `@fields<T>()`. Post-type-check, so nullability is real. VIABLE but drifts: if it inlines at each
 * call-site it violates the owner's "not inline per call-site"; if it emits a MEMOIZED shared body it
 * has become Option (c). Kept as the "comptime family owns it" framing; collapses toward (c) when memoized.
 *
 * @param v  the value to serialize
 * @return   the serialized text, or an error
 * @since 0.3.1 (serialization tags)
 */
exp fn encode<T>(v: T): str | error { comptime for f in @fields<T>() { /* assemble */ } }
// call site:  json::encode(user)   →   body generated post-type-check; MUST be one shared routine, not N inlines
```
- **Verdict: viable but under-specified for "one routine per type."** Family B as sealed produces an
  inlined *value/body*; to yield ONE shared named routine it needs the memoized-synthesis logic of (c). It
  is the right *stage* (post-type-check) but the wrong *granularity* unless it delegates to (c).

#### Option (c) — a dedicated post-type-check SYNTHESIS pass, monomorph-style — **RECOMMENDED**

```teko
/**
 * synthesize_serializers — RECOMMENDED: a dedicated program-transform pass at the monomorph/comptime
 * slot (`project.tks:353-367`). For every reached `json::encode<T>` / `decode<T>` instantiation it (1)
 * reads `@fields<T>()` + parses each tag (§3), (2) ASSEMBLES one `serialize_T`/`parse_T` `TFunction` via
 * the `comptime for` of §2.2, (3) INSERTS it into the `TProgram` ONCE, memoized by (T, format), and (4)
 * rewrites the call-site to target the synthesized routine. This mirrors how `monomorph` already
 * synthesizes + memoizes specialized generic instances (`project.tks:353-354`) — the established
 * precedent — and runs AFTER type-check, so nullability (§9.D) is available. Zero runtime reflection: the
 * routine is a plain `TFunction` of direct field accesses; `@fields`/tags/the loop all evaporate.
 *
 * @param prog  the type-checked, monomorphized program
 * @return      the program with the per-(type,format) serializer routines synthesized in, or an error
 * @throws      a diagnostic when a type is not field-shaped, or a tag is malformed
 * @since 0.3.1 (serialization tags)
 */
fn synthesize_serializers(prog: TProgram): TProgram | error { /* §5.2 crumb — reuses @fields + comptime for */ }
// call sites (all share the ONE routine):
//   json::encode(user)          →   serialize_User(user)      // memoized: synthesized once
//   json::decode<User>(text)    →   parse_User(text)          // one routine, called everywhere
```
- **Verdict: RECOMMENDED.** It is the only option that delivers **exactly** the owner's model — ONE
  per-type routine in the AST, shared by all call-sites, no inline scatter — at the correct stage
  (post-type-check, nullability available), reusing the existing monomorph synthesis+memoization precedent,
  and staying M.0-honest. It uses §2's `@fields<T>()` as INPUT and §2.2's `comptime for` as its internal
  assembly loop; it is a thin *driver* over the mandated comptime engine, not a new macro family (§6 #3).

**Recommendation: Option (c) — a dedicated post-type-check synthesis pass at `project.tks:353-367`,
monomorph-style (synthesize + memoize per (T, format)), driven by `@fields<T>()` + `comptime for`.** Reject
(a) (pre-type-check: no nullability); treat (b) as (c) once memoized.

### 4.2 What the surface looks like (the visible trigger)

The trigger is a **visible, explicitly-called** stdlib generic — never an invisible auto-derive (§2.3):
```teko
exp fn encode<T>(v: T): str | error          // teko::encoding::{json,xml,csv}::encode  → calls the synthesized serialize_T
exp fn decode<T>(s: str): T | error          // teko::encoding::{json,xml,csv}::decode  → calls the synthesized parse_T
```
`encode`/`decode` are the visible surface; the synthesis pass (§4.1c) replaces their call with the
per-type routine it built. Free generic functions ⇒ they ride `monomorph`; **no #254 needed** (a
`user.encode()` *method* form would need #254 — deferred; the free-function form ships first).

### 4.3 The synthesized routine — encode content (`T | null` drives null/omitempty)

The pass bakes, per field (using `f.is_nullable` from §9.D + the parsed tag), a fixed branch into the ONE
`serialize_T` body:

| field shape | tag | `serialize_T` emits (JSON) | (XML) |
|---|---|---|---|
| `T` (non-null) | — | always `"key": render(v.f)` | always `<key>…</key>` |
| `T \| null`, no `omitempty` | — | `v.f==null ? "key":null : "key":render(v.f)` | null → `<key xsi:nil="true"/>`, else element |
| `T \| null`, `,omitempty` | `omitempty` | omit the pair when `v.f==null` | omit the element when `v.f==null` |
| any | `json:"-"` | nothing | nothing |

The `v.f == null` test is a **direct union-tag read** on the `X | null` fat descriptor (`{tag@0; ptr@8;
len@16}`, §9.D) — not reflection. Everything else is compile-time-decided in the synthesized body.

### 4.4 The synthesized routine — decode content (`T` required→error, `T | null` optional→accept)

`parse_T` unrolls a per-field extractor over the parsed document (`JsonValue`, `json.tks:73`):

| field shape | key absent / JSON `null` | key present |
|---|---|---|
| `T` (non-null, required) | **decode error** ("missing required field `f`") | coerce to `T`, or type error |
| `T \| null` (optional) | bind `null` | coerce to `T` |

Non-null-required is the schema contract (§9.D "o TIPO diz"): the absence of `omitempty`/`| null` makes a
field mandatory — no external schema. XML maps it onto element/attribute presence; JSON onto value
absence/`null`. Nested struct fields are constructed via target-typed `.{ … }` (§4.1 surface doc — the
field's declared type is the target).

### 4.5 Catch-all

A field tagged `json:",catchall"`, typed `str | null` or `Json`:
- **decode:** `parse_T` ends with a collector — every JSON key not matched by a named field goes into the
  catch-all field (a `Json` value, or stringified for `str | null`).
- **encode:** `serialize_T` iterates the catch-all field's **own entries** (its key/value data) and appends
  each — a loop over the field's *value* (legitimate runtime data), not over type metadata (M.0 intact).
- Exactly one catch-all field per type (a second is a compile error).

### 4.6 What survives to runtime (the M.0 proof)

After synthesis, `serialize_User` is a plain `TFunction`: string appends interleaved with `render(u.id)`,
`render(u.name)`, one `if u.email != null` guard, etc. **No `FieldInfo`, no tag, no `@fields`, no loop, no
reflection** — indistinguishable from a hand-written serializer, and shared by every call-site.

### 4.7 Schema-first — reading + reconciling external schemas at compile time

The type-level schema tag (§3.5) feeds the **same** synthesis pass (§4.1c): the compiler reads the
schema(s) at compile time and reflects them into `parse_T`/`serialize_T`. Three sub-questions, each with
options + a recommendation.

#### 4.7.1 How the external schema is read + reconciled with the Teko struct

- **Opt-A — VALIDATE-and-error-on-structural-divergence (the struct is the truth) — RECOMMENDED.** The
  Teko struct + field tags remain the source of shape (the type IS the schema, §9.D). The compiler parses
  the JSON-Schema/XSD at compile time and **checks the struct conforms** — every required schema property
  has a matching non-null field, types are compatible, XSD element order matches declaration order. A
  **structural divergence is a COMPILE error** (M.3 honest boundary). The schema additionally contributes
  *value* constraints the type cannot express (JSON-Schema `pattern`/`minimum`/`enum`, XSD facets) which
  become checks in `parse_T` under `mode="validate"` (§4.7.2).

```teko
/**
 * check_schema_conformance — SYNTHESIS-internal (Opt-A): at compile time, parse the bound schema and
 * assert the Teko struct's `@fields<T>()` conform (required↔non-null, type-compatible, XSD order). A
 * divergence is a compile error naming the field and the schema clause. The schema's value facets are
 * returned as a comptime constraint list the parser bakes in when `mode="validate"`.
 *
 * @param schema  the compile-time-parsed schema (JSON-Schema doc / XSD)
 * @param T       the type being reconciled
 * @return        the comptime value-constraint list, or a conformance error
 * @throws        a compile-time diagnostic when the struct diverges from the schema shape
 * @since 0.3.1 (serialization tags — schema-first)
 */
comptime check_schema_conformance<T>(schema: Schema): []Constraint | error { /* §5.2 crumb */ }
```

- **Opt-B — GENERATE the struct from the schema (the schema is the truth).** The compiler emits the Teko
  type from the XSD/JSON-Schema (xsd.exe-style). **Deferred/rejected as in-scope:** it inverts "the type IS
  the schema," and type *generation from a schema* is a separate tooling roadmap (a `teko schema-gen`
  command), not the parser-synthesis feature. Recorded, not built here.
- **Opt-C — schema DRIVES value-validation only, struct is nominal (no structural check).** The compiler
  ignores structural mismatch and only bakes the schema's value facets into `parse_T`. Rejected as the base
  (a silent structural mismatch is a latent bug — M.3); its facet-extraction is folded into Opt-A instead.

**Recommendation: Opt-A** — struct-is-truth, compile-time structural conformance check (error on
divergence), schema value-facets folded into the parser under `validate`. Opt-B is a separate schema-gen
tool; Opt-C's facet layer lives inside Opt-A.

#### 4.7.2 Validation is OPT-IN — `mode="validate"` vs `mode="ugly"` (owner ruling)

The schema binding (or field tags) carries a **mode** that decides whether the synthesized parser includes
a validation step:

- **`mode="validate"` (strict):** `parse_T` includes a **validation step** — schema value-facets
  (pattern/range/enum/XSD facets), strictness (reject unknown elements/keys unless a catch-all exists),
  and XSD element-order checks. A violation → **decode error**.
- **`mode="ugly"` (tolerant, DEFAULT):** the synthesis **omits the validation step entirely** — `parse_T`
  is best-effort: it reads the fields it recognizes, ignores the rest, runs no facet/strictness checks.
  **Zero runtime cost** because the checks are *never emitted* (not merely skipped at runtime).

**Where it enters + how it is elided.** `mode` is a comptime-known tag option, so the synthesis wraps the
validation prologue in a `comptime if`:

```teko
/**
 * (SYNTHESIS-internal) the validation step is emitted into parse_T ONLY under `mode="validate"`. Under
 * `mode="ugly"` the `comptime if` is false, so NO validation node exists in the synthesized routine —
 * zero cost, not a runtime skip. The TYPE contract (a non-null required field absent → error) is NOT
 * governed by mode: it is CONSTRUCTION (you cannot build a `T` without it), not validation.
 */
comptime if mode == Mode::Validate {
    emit { validate_against_schema(doc, SCHEMA_CONSTRAINTS) or return decode_error }   // baked only when strict
}
// serialize side is unaffected by mode (output always conforms by construction)
```

The line the design draws: **`mode` governs VALIDATION** (schema facets + strictness + XSD order);
**it does NOT govern CONSTRUCTION** — a non-null required field that is absent is still a decode error even
under `ugly`, because the storage `T` cannot be built without it (§4.4). That keeps `ugly` honest: it relaxes
*checking*, never the type's own feasibility.

#### 4.7.3 XML namespace resolution — by URI, never by prefix (owner ruling)

XML namespace prefixes are **document-local, arbitrary, untrusted** (`x:sch=…` in one doc, `s:sch=…` in
another, same meaning). The synthesized `parse_T` **resolves by namespace URI, never by the prefix**:

- **Opt-A — URI-keyed dispatch table baked at synthesis — RECOMMENDED.** The synthesis bakes a comptime
  table `URI → field-handler` into `parse_T`. At parse, `parse_T` first reads the document's `xmlns:`
  declarations to build a runtime map `doc-prefix → URI`, then dispatches **every element/attribute by its
  resolved URI**, remapping whatever prefix the document happens to use. An unexpected URI → skip (ugly) or
  error (validate).

```teko
/**
 * (synthesized parse_T, XML) — resolve by URI, never by prefix. Read the doc's xmlns bindings into a
 * prefix→URI map, then dispatch each node by its URI against the comptime-baked expected-URI table. The
 * document's prefix is arbitrary and NEVER trusted — only the URI it binds to matters.
 */
var ns = read_xmlns_bindings(doc)                 // doc-prefix → URI, runtime, per document
match resolve_uri(ns, node.prefix) {              // remap the arbitrary prefix to its URI
    "http://example.com/a" => parse_field_id(node)          // expected URI → the baked handler
    "http://example.com/b" => parse_field_name(node)
    _ => skip_or_error(mode)                        // unknown namespace → ugly skips, validate errors
}
```

- **Opt-B — prefix-normalization pre-pass** (rewrite all prefixes to canonical before dispatch): same
  result, an extra tree rewrite; reserve if a streaming parser makes the inline map awkward.
- **Opt-C — trust the prefix:** REJECTED — prefixes are arbitrary; trusting them is a correctness bug.

**Recommendation: Opt-A** — a comptime URI→handler table + a runtime prefix→URI remap per document. Prefix
is data, URI is identity.

#### 4.7.4 Namespace URI → local path resolution (public vs private)

A namespace URI is a **logical identifier**, often not resolvable over the network (private/internal
schemas). Resolution order (deterministic, **hermetic-build-safe** — §6 tension #7):

1. **Manifest map (authoritative).** The project `.tkp` carries a `[schema]` table `URI → local path`; if
   the URI is listed, use that vendored file. Explicit, reproducible.
2. **Vendored-path convention (fallback).** Else look for a conventional local file (e.g.
   `schemas/<sanitized-uri>.xsd` under the project). Common XML practice.
3. **Public fetch — OPT-IN + PINNED only.** A remote fetch happens **only** when the manifest explicitly
   enables it AND pins a content hash (reproducibility). **Never a silent build-time network call** — the
   default is no network (M.3 honest boundary; byte-identical fixpoint forbids nondeterministic fetch).
4. **Unresolved → honest compile error** naming the URI and the searched paths (never a silent skip).

```
# .tkp
[schema]
"http://example.com/a" = "schemas/a.xsd"          # 1. manifest map — vendored, reproducible
# (2) else schemas/http_example_com_a.xsd by convention
# (3) fetch only if:  allow-remote = true  AND  a pinned sha256 is present
```

#### 4.7.5 Teko-native schemas for formats WITHOUT a standard schema language (owner ruling)

CSV and fixed-width files have **no standard schema language**, so the Teko-native schema **IS the type +
field tags** (the type-is-the-schema principle, no new file format needed) — and it feeds the *same*
synthesis:

- **CSV — a column schema via field tags.** Each field's `csv:"…"` tag gives column name / index / order,
  plus a type from the field's Teko type; a type-level `csv=` may carry header/delimiter options.

```teko
/**
 * Order — CSV column schema expressed as field tags (Teko-native; no external schema language). Column
 * name/index/order come from the tags, the cell type from the Teko field type; the type-level tag sets
 * header/delimiter. Feeds the SAME synthesis → serialize_Order/parse_Order.
 *
 * @since 0.3.1 (serialization tags — Teko-native CSV schema)
 */
type Order = struct `csv="header=true;delim=,"` {
    id:    u64        `csv:"id,0"`                 // column "id", index 0
    total: f64        `csv:"total,2"`              // column "total", index 2
    note:  str | null `csv:"note,3,omitempty"`    // nullable → empty cell
}
```

- **`teko::encoding::fixed` — a fixed-position schema via field tags.** For pointer-files / fixed-width
  (banking / mainframe / legacy), each field's `fixed:"…"` tag gives **offset + width** (+ padding /
  alignment / numeric format). `parse_T` slices `s[offset .. offset+width]`, trims padding, coerces;
  `serialize_T` pads each field to its width at its offset.

```teko
/**
 * Record — fixed-width (pointer-file) schema as field tags (Teko-native): field → offset+width (+pad/
 * align). The synthesized parse_Record slices by offset/width and trims padding; serialize_Record pads
 * to width at offset. Same synthesis pass, a different per-format tag grammar + render/extract helper.
 *
 * @since 0.3.1 (serialization tags — teko::encoding::fixed)
 */
type Record = struct `fixed="len=64"` {
    acct: str  `fixed:"@0+12,pad=space,align=left"`     // bytes 0..12, space-padded, left-aligned
    bal:  i64  `fixed:"@12+10,pad=zero,align=right"`    // bytes 12..22, zero-padded, right-aligned
}
```

Both are ordinary formats plugged into §3.4-c: a per-format comptime tag grammar (`csv`/`fixed`) + a
render/extract helper, driving the SAME `synthesize_serializers` pass. No new synthesis machinery — a new
format is a new comptime tag parser + scalar codec, nothing more.

---

## 5. Composition, fixpoint order, blast-radius, fixtures, dependencies

### 5.1 Composition with the sealed surface

- **§9.D (union-com-null) — the schema source.** `@fields<T>()`'s `is_nullable` comes off the resolved
  `Type`; the synthesized routine's null-branch is a union-tag read on the fat descriptor. encode/decode are
  *readers* of §9.D, adding no nullability concept.
- **§14.2 (comptime — the LAW) — the host.** `@fields<T>()` = Extent-3 reflection (INPUT); `comptime for`/
  `comptime if` = the synthesis's internal assembly loop; the synthesis pass sits at `project.tks:353-367`.
  These extend Family B's stage; **they add no third macro class** (§6 #3) — the pass is a monomorph-style
  driver, and encode/decode are ordinary runtime functions.
- **9-ops / #254.** `encode<T>`/`decode<T>` are **free generic functions** ⇒ ride `monomorph`; **no #254**.
  A method form (`user.encode()`) would need #254 — deferred.
- **Lexer (backtick).** The tag rides pre-type-check as an opaque `str` on `Field.tag`, inert until the
  synthesis consumes it in comptime (§3.4).
- **monomorph precedent.** The synthesis reuses monomorph's synthesize+memoize shape (`project.tks:353-354`).

### 5.2 Fixpoint order (leaves → roots) + blast-radius

The `.tkb` codec serializes `Field`; adding a member shifts its on-disk layout ⇒ **byte-identity fixpoint**
concern, sequenced additive-first:

1. **Lexer** — add the `Tag` token + `read_backtick_tag` (stripped `read_string_body`, delimiter `` ` ``),
   dispatched before `read_symbol` (`lexer.tks:719`). **Additive-inert.** *Narrow blast.*
2. **AST** — `Field` gains `tag: str` (default `""`); `StructBody` (and `TypeDecl`) gains a `type_tag: str`
   (default `""`) for the schema-first type-level tag (§3.5). Thread `Field.tag` through the **four** sites
   (`parse_decl.tks:905/974/1219` + `freeze_all_fields:1249`) + the `.tkb` read/write of `Field`/`StructBody`.
   **Widest blast.** Both default `""` ⇒ an untagged corpus lowers **byte-identically** (the fixpoint argument).
3. **Parser** — after `parse_type` (the `p = ty.next` points), optionally consume a trailing `Tag` into
   `Field.tag`; between the type head and the `{`, optionally consume a type-level `Tag` into `type_tag`.
   Additive.
4. **Checker** — extend `FieldView` (or a sibling projection) with `tag`+`vis`; implement `@fields<T>()`
   over `deriver_field_view` (`collect.tks:1673`) + the `is_nullable` predicate.
5. **Comptime + synthesis** — `T.fields` (Extent-3), `comptime for`/`comptime if`, and the
   `synthesize_serializers` pass at `project.tks:353-367`.
6. **Stdlib** — `teko::encoding::{json,xml,csv}::{encode,decode}` surface + per-format comptime tag parsers.

**Reseed hazard.** Steps 1–2 change the seed's surface (new token + new `Field` member): sequence
additive → **reseed** → sweep; only then may the corpus write a tag. Steps 4–6 use only already-seeded
features once the macro/comptime facility is seeded (§5.4).

### 5.3 Fixtures (`.tkt` pure logic + `.tkr` native exit codes)

**`.tkt` (unit, pure — buildable for the non-synthesis parts):**
- `tag_lex.tkt` — a backtick run lexes to one `Tag` token; body bytes verbatim; unterminated backtick → lex error.
- `tag_parse.tkt` — a trailing tag is stored on `Field.tag`; untagged ⇒ `tag == ""`; a `readonly struct`
  preserves the tag through `freeze_all_fields`.
- `json_tag_grammar.tkt` — `@parse_json_tag` folds `"json:\"email,omitempty\""` → `{name="email",
  omitempty=true}`; `"json:\"-\""` → skip; `"json:\",catchall\""` → catchall; a bad option → comptime error.

**`.tkr` (Gherkin regressor, native exit code — end-to-end; blocked until §5.4):**

| fixture | input | expected native exit code |
|---|---|---|
| `enc_roundtrip.tkr` | `decode<User>(encode(u))` equals `u` (fully-populated `User`) | `0` |
| `enc_omitempty.tkr` | encode `User{email=null}` with `,omitempty` → `email` key absent; assert | `0` |
| `dec_missing_required.tkr` | `decode<User>("{}")` where `id: u64` is non-null-required | non-zero (decode error path); assert the specific error |
| `dec_optional_absent.tkr` | `decode<User>` of a doc missing a `T\|null` field → binds `null` | `0` |
| `xml_nil_presence.tkr` | encode a null nullable field → element omitted (or `xsi:nil`); decode absent → null | `0` |
| `catchall.tkr` | decode a doc with 2 extra keys into a `,catchall` `Json` field → both captured; re-encode round-trips | `0` |
| `one_routine_per_type.tkr` | two call-sites of `json::encode(u: User)` → the `.tsym` shows **ONE** synthesized `serialize_User` symbol (shared, not two inline bodies) | `0` |
| `no_runtime_reflection.tkr` | inspect `serialize_User`'s rodata/`.tsym` → **no `FieldInfo`/tag/type-metadata** symbol (the M.0 assertion) | `0` |
| `schema_conformance.tkr` | a type bound to a schema whose required property has no matching non-null field → **compile** error (§4.7.1) | non-zero (compile-fail) |
| `mode_validate.tkr` | `mode="validate"` + an input violating a schema facet (bad pattern) → decode error; the same input under `mode="ugly"` → `0` (no validation emitted) | validate: non-zero · ugly: `0` |
| `xml_ns_prefix_remap.tkr` | the same doc with two different xmlns prefixes for the expected URI → both decode identically (resolve by URI, §4.7.3) | `0` |
| `csv_fixed_roundtrip.tkr` | `Order` (CSV column schema) and `Record` (fixed-width) round-trip encode→decode (§4.7.5) | `0` |

### 5.4 Dependencies — what is BLOCKED vs designable NOW

**BLOCKED (needs a dep closed first):**
- **The macro/comptime facility must be IMPLEMENTED.** `plano-macro.md` is DESIGN. The synthesis needs
  Family B's `expand_comptime`/`eval_const`/`literal_of` wired, PLUS two extensions this doc drives:
  **`T.fields` reflection (Extent-3, owner-pending)** and the **`comptime for`/`comptime if`** the synthesis
  loop uses.
- **The `synthesize_serializers` pass (§4.1c)** — a new post-type-check synthesis pass; must be ratified +
  implemented (it is the owner-precision's core) before the serializers exist.

**DESIGNABLE / buildable NOW (independent of the blocked comptime):**
- The **lexer `Tag` token + `read_backtick_tag`** (§5.2 step 1).
- The **`Field.tag` AST member + four parse sites + `freeze_all_fields`** (steps 2–3).
- The **tag grammar** (§3) + the `.tkt` lex/parse/grammar fixtures (§5.3) — no comptime needed.
- The **`FieldInfo`/`@fields<T>()`/`encode`/`decode` contracts** as doc-commented **honest-stop** skeletons
  in the stdlib against the declared shapes, compiling today (empty bodies) — the implementer fills them the
  minute Extent-3 + `comptime for` + the synthesis pass close.
- The **type-level `type_tag` AST member + parse** (§3.5) and the **`.tkp [schema]` manifest key** + the
  URI→local-path resolution rule (§4.7.4) — pure parse/manifest work, no comptime needed.
- The **JSON-Schema / XSD / CSV-column / fixed-position comptime tag-parser + schema-reader contracts**
  (§3.4, §4.7.1, §4.7.5) as honest-stop comptime helpers against declared shapes — blocked only for their
  bodies (which need the comptime engine), designable as signatures now.

The implementer resumes in minutes: land the token + `Field.tag` additively behind a reseed, keep the
`encode`/`decode` + `synthesize_serializers` skeletons as honest-stops, wire the synthesis when the comptime
capability lands.

---

## 6. Law tensions — resolved law-first

| # | Tension | Law(s) | Resolution |
|---|---|---|---|
| 1 | **M.0 zero-reflection vs consuming tags/fields.** | M.0 | **Resolved.** Reflection is a **comptime VALUE** (names/types/tags), consumed by the synthesis to **build one routine of direct static accesses**; the runtime routine holds no metadata, no reflective dispatch (§4.6). The compiler *already built the parser* — runtime just calls it. Same family as `@sizeof`/`@typename`, extended to the field list. |
| 2 | **Owner-precision "the compiler SYNTHESIZES the routine" vs the no-shadow law** (retired structural traits synthesized invisible bodies). | no-shadow (surface §9.4) | **Resolved.** The synthesis is (a) **triggered by a visible, explicitly-called** `encode<T>`/`decode<T>` — never an invisible auto-derive (§2.3 rejected as surface); and (b) **fully determined by the VISIBLE tags + types** the dev wrote (the type IS the schema). A synthesized `serialize_User` is an implementation detail of an explicitly-called library generic — exactly like a `monomorph`-specialized instance the compiler already generates from visible source. The shadow the project retired was an *invisible interface-satisfaction*; this is a *visible, explicitly-invoked* codegen. No-shadow holds. |
| 3 | **A dedicated synthesis pass / "comptime emits a routine" vs "NO third macro class".** | §14.1 SEAL | **Resolved (owner-ratifiable, recommended — no HALT).** The `synthesize_serializers` pass introduces **no new `@`-declaration family and no new call surface**; it is a **monomorph-style program-transform** (the established `project.tks:353-354` precedent) that *drives* the sealed Family-B comptime engine (`@fields` + `comptime for`) to emit a **named function**. It is not a third macro — it is codegen, like monomorph. Framed as the minimal mechanism the owner-precision *requires*; recommend ratifying it as the Extent-3 companion. |
| 4 | **macro (Family A) is insufficient** — the tags need field TYPES (nullability) which do not exist pre-type-check. | §14.1/§14.2 | **Resolved.** Synthesis MUST run **post-type-check** (§4.1a rejected). This is why the recommended pass sits at `project.tks:353-367`, in Family B's stage — the §14 law itself forces the choice. |
| 5 | **Format grammar coupling to the language.** | M.0 (small) | **Resolved.** Lexer/parser keep the tag an **opaque raw `str`**; each format parses its own spec in comptime (§3.4-c). The language knows nothing of JSON — formats are library-defined and extensible. |
| 6 | **`.tkb` byte-identity** when `Field`/`StructBody` gain a member. | bootstrap fixpoint | **Resolved.** `tag`/`type_tag` default `""`; an untagged corpus serializes byte-identically (inert empty strings); live only when written, behind additive → reseed → sweep (§5.2). |
| 7 | **Compile-time schema READ vs hermetic / byte-identical builds** — a remote schema fetch is nondeterministic. | M.3 (honest boundary) · bootstrap fixpoint | **Resolved.** A namespace URI resolves **local-first** — manifest `[schema]` map, then vendored convention (§4.7.4); a public fetch is **opt-in AND content-pinned** only, never a silent build-time network call. Default = no network ⇒ the schema read is deterministic and the fixpoint holds. Unresolved URI → honest compile error. |
| 8 | **Schema-struct divergence** — schema says required, struct field is nullable (or missing). | M.3 | **Resolved.** Opt-A (§4.7.1): the compiler checks conformance at compile time and **errors on structural divergence** (named field + schema clause), rather than silently trusting either side. The struct stays the source of shape; the schema adds value-facets under `validate`. |
| 9 | **`ugly` mode dropping the type contract** — could "no validation" skip a required non-null field? | M.1 (fail-loud) vs tolerance | **Resolved.** `mode` governs **validation** (facets/strictness/order), **not construction**: a non-null required field absent is still a decode error under `ugly` (§4.7.2), because a `T` cannot be built without it. Tolerance relaxes checking, never the type's feasibility. |

**No genuine unresolved tension requires a HALT.** The owner's SEAL + precision *mandate* a compiler-
synthesized, per-type, zero-reflection routine driven by type-as-schema tags; this doc supplies the only
mechanism satisfying M.0 + no-shadow + the two-family SEAL together (a visible, explicitly-called generic
whose per-type routine a monomorph-style pass synthesizes post-type-check), and names the single
owner-ratifiable extension (the synthesis pass, tension #3) with a recommendation — not a question.

---

## 7. Safety confirmation

`teko test` was **NOT** run in any form (not `teko test .`, not a subset, not staged) — the `monomorph`
leak that crashes the container was never risked. **No build, no reseed, no product-code edit.** All work
was static reading + reasoning over `src/parser/ast.tks` (`:643` `Field`, `:618` `StructBody`),
`src/parser/parse_decl.tks` (`:873-917`/`:939-976`/`:1219`/`:1243-1249`), `src/lexer/lexer.tks`
(`:453-551`, `:634-720`), `src/lexer/token.tks`, `src/checker/collect.tks` (`:1608-1687`, M.0 anchor
`:1612`), `src/checker/comptime_fold.tks` (`:18-36`/`:306`/`:1997`), `src/build/project.tks`
(`:116-126`/`:353-367` — the monomorph synthesis + pass slots), `src/encoding/json/json.tks` (`:66-73`
`JsonValue`), `docs/design/plano-stdlib-catalogo-expansao.md` (§D), `docs/design/mudancas-superficie-0.3.1.md`
(§9.D, §14), `docs/design/plano-macro.md` (two-family SEAL, §A.0/§B.1/§B.2), `docs/design/tkr-regression-format.md`.
**The main `/home/user/teko-lang` checkout was NOT touched** — all work happened in an isolated worktree off
`origin/fix/retirement`. This file is the sole edit.
