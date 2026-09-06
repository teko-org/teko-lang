# Serialization tags → implementer-ready crumb sequence (the serialization-OWNED half of §14's keystone)

> **Status:** DESIGN — crumb sequence. Read-only on product code; this file is the SOLE edit. NO build,
> NO reseed, `teko test` NOT run in any form (the `monomorph` leak crashes the container). Isolated
> worktree off `origin/fix/retirement`, branch `design/serial-crumbs`; the main checkout and the reseed
> worktrees (edited by other agents) are UNTOUCHED.
>
> **What this doc is.** It turns `serial-tags-comptime-field-reflection-0.3.1.md` (the SEALED design) into
> an ORDERED, gate-able crumb sequence, contracting against the §14 PRODUCER's DECLARED shapes
> (`section14-macro-comptime-impl-v1.md`). §14 is being implemented NOW; this is its serialization consumer,
> designed-ahead so the implementer resumes in minutes when §14 lands.
>
> **The producer/consumer split (the load-bearing boundary — §1).** §14 OWNS the comptime engine,
> `@fields<T>()` v1 (descriptors WITHOUT `.tag`), the `synthesize_serializers` machinery, and its UNTAGGED
> path (field-name keys + `X|null` nullability). SERIALIZATION (this doc) OWNS: the backtick tag layer
> (lexer token + `Field.tag`/`StructBody.type_tag` AST + codec + parse), the per-format tag GRAMMAR +
> comptime tag parsers, the `.tag` extension of `@fields`, the TAGGED synthesis branches (key rename /
> omitempty / skip / catchall / `,string`), schema-first (type-level tag + schema readers + conformance +
> validate/ugly + XML URI resolution + `.tkp [schema]`), and the stdlib `encode`/`decode` surface.
>
> **Sealed law (not reopened):** owner rulings — Go-style backticks orient the compiler to SYNTHESIZE one
> per-type routine in comptime, used at runtime, zero runtime reflection (M.0); STRUCT-ONLY DTO;
> nullability from the field TYPE (`T|null`, §9.D); schema-first via type-level tag; aliases are DYNAMIC
> (never trusted); XML namespace by URI (never prefix); validate vs ugly modes; catch-all for schemaless
> JSON. Teko-only (the C twins stay frozen; only Teko-side lexer/parser/checker/emit gain arms),
> W15/Javadoc (every snippet copy-ready).

---

## 1. Anchor re-verification + the §14 contract (verified on `origin/fix/retirement` this session)

**Serialization-side anchors (corrected drift vs the design doc in parentheses):**

| what | ACTUAL (verified) | design doc said |
|---|---|---|
| `Field` node | `src/parser/ast.tks:643` — `struct { name; type_ann: TypeExpr; vis; is_intern; has_doc; doc; is_readonly }` (NO tag slot) | `:643` ✓ |
| `StructBody` | `ast.tks:618` — `struct { fields; methods; implements; consts; is_readonly }` (no type_tag) | `:618` ✓ |
| lexer unexpected-char fallthrough (backtick lands here) | `read_symbol` `_ => scan_err_at` at **`lexer.tks:703`**; `next_token` dispatch `:710`, the `@` marker branch `:720` | `:699`/`:719` (−4) |
| verbatim-string template to copy | `read_string`/`read_string_body` reached from `next_token:719`; `@"…"` at `lexer.tks:429-453` (per §14 recon) | `:453-551` |
| field-build site #1 (struct body) | **`parse_decl.tks:917`** (Field built after `parse_type` returns `ty:916`, before `p = ty.next:918`) | `:905` (+12) |
| field-build site #2 (class body) | `parse_class_fields` **`:951`**, Field built **`:990`** | `:939`/`:974` |
| field-build site #3 | **`:1245`** | `:1219` |
| `freeze_all_fields` (readonly rebuild) | def **`:1269`**, rebuild **`:1275`** — must copy any new `tag` | `:1243-1249` |
| `.tkb` writer — `[]Field` | **`tkb_write.tks:367-382`** (name+type_ann+vis+is_intern+has_doc+doc+is_readonly) | (uncited) |
| `.tkb` reader — `Field` | **`tkb_read.tks:630`** | (uncited) |
| `.tkb` writer/reader — `StructBody` | `write_struct_body` **`tkb_write.tks:421`**; reader **`tkb_read.tks:766`** | `tkb_write.tks:446` (approx) |
| `.tkb` version constants | `tkb_frame.tks` (`TKB_*_VERSION`) | ✓ |
| `FieldView` (the reflection substrate) | `collect.tks:1620`; `deriver_field_view` **`:1675`** | `:1618`/`:1673` (+2) |
| `JsonValue` (decode target) | `src/encoding/json/json.tks:73` | ✓ |

**§14 PRODUCER contract — the DECLARED shapes I build against (from `section14-macro-comptime-impl-v1.md`,
NOT this doc's to change):**

| §14 shape | declared form | crumb | status in §14 |
|---|---|---|---|
| `FieldInfo` | `exp type FieldInfo = struct { name: str; is_nullable: bool; vis: parser::Visibility }` — **NO `.tag` in v1** (B5, §6, §5.3-Opt-1) | §14 B5 | `.tag` is EXPLICITLY serialization-owned (§14 §6, tension 5) |
| `@fields<T>()` | `exp global comptime fields<T>(): []FieldInfo` — projects `deriver_field_view` | §14 B5 | BLOCKED on Extent-3 (recommended-ratify) |
| `expand_comptime` | `fn expand_comptime(prog: TProgram, table: TypeTable): TProgram \| error` at `project.tks:367` slot | §14 B3 | greenfield |
| `literal_of` (agg) | extended `literal_of(v, ty, line, col): TExpr \| null` (CVAgg/CVBytes) | §14 B2 | greenfield |
| `synthesize_serializers` | `fn synthesize_serializers(prog: TProgram, table: TypeTable): TProgram \| error` — monomorph-style, post-mono slot; **UNTAGGED path is §14-owned**, TAGGED path serialization-owned | §14 B6 | untagged buildable on `@fields` v1; **tagged BLOCKED on `Field.tag`** |

**Confirmed absent in the tree** (`expand_comptime`, `comptime for`, `comptime if`, `T.fields` — grep
finds none): the whole comptime facility is genuinely §14-blocked. **The clean seam:** everything on the
serialization side that does NOT need the comptime engine is BUILDABLE NOW (crumbs S1–S3, S6a, S7-skel);
the comptime-consuming half (S4/S5/S6b bodies) resumes when §14 B2/B3/B5/B6 land.

---

## 2. The crumb sequence (ordered; each independently gate-able; ★ = ritual point)

Dependency spine: **S1 → S2** (lexer token then AST member — additive, behind a reseed) is the ONLY part
touching the seed surface, so it leads. **S3** (tag grammar) is pure library, parallel. **S4/S5** (the
`@fields.tag` extension + tagged synthesis) contract against §14 B5/B6 and land when those land. **S6**
(schema-first) layers on S2/S3. **S7** (stdlib surface) is honest-stop skeletons from day one.

### S1 — lexer: a `Tag` token + `read_backtick_tag` (BUILDABLE NOW)

**Goal.** Lex a Go-style backtick run `` `json:"id" xml:"@id"` `` to ONE `Tag` token whose body is the raw
bytes between backticks (no escapes — a raw run, like `@"…"`).

**Where.** `src/lexer/token.tks` — append a `Tag` kind LAST (never a stored operator; the `kind_byte`
ordinal-stability rule, §14 recon `token.tks:95-102`). `src/lexer/lexer.tks` — add `read_backtick_tag`
(a stripped `read_string_body` with delimiter `` ` `` (0x60), no escape processing, error on EOF before
the closing backtick), dispatched in `next_token` (`:710`) for a leading `` ` `` **before** falling to
`read_symbol` (whose `_ => scan_err_at:703` is today's backtick error).

```teko
/**
 * read_backtick_tag — scan a Go-style raw backtick tag run `` `…` ``: the opening backtick at `pos`, then
 * every byte verbatim (NO escapes, NO interpolation — a raw run) up to the closing backtick, yielding one
 * `Tag` token whose text is the body between the backticks. An EOF before the close is a located lex error
 * (mirrors an unterminated string). A backtick is a FREE lexical slot today (read_symbol rejects it at
 * lexer.tks:703), so this is additive-inert: no corpus contains a backtick.
 *
 * @param source  the source text
 * @param pos     the index of the opening backtick
 * @return        a `Tag` scan spanning through the closing backtick, or an unterminated-tag error
 * @since 0.3.1 (serialization tags)
 */
fn read_backtick_tag(source: str, pos: u64): Scan | error { /* stripped read_string_body, delim 0x60 */ }
```

**Existing fns touched:** `next_token` (`lexer.tks:710`, one leading-backtick branch); `token.tks` (append
`Tag`). **Inertness:** a backtick is an error today → byte-identical corpus lexing. **Gate-able alone:**
`.tkt` — a backtick run lexes to one `Tag`; body bytes verbatim; unterminated → lex error.

### S2 ★ — AST: `Field.tag` + `StructBody.type_tag`, codec, parse (BUILDABLE NOW; the reseed crumb)

**Goal.** Carry the tag from source to the comptime input. `Field` gains `tag: str` (default `""`);
`StructBody` (and the type-level position) gains `type_tag: str` (default `""`) for the schema-first tag
(§3.5 of the design). Both default `""` so an untagged corpus is inert.

**Where + the four Field-build sites that MUST thread `tag` (or a readonly/frozen struct loses it):**
`parse_decl.tks:917` (struct body), `:990` (class body), `:1245` (third push), `:1275`
(`freeze_all_fields` rebuild). Plus the `.tkb` codec: `tkb_write.tks:367-382` (`[]Field` writer),
`tkb_read.tks:630` (`Field` reader), `write_struct_body` `tkb_write.tks:421` + reader `tkb_read.tks:766`
(`StructBody`), and a `TKB_*_VERSION` bump in `tkb_frame.tks` at the ONE crossing.

```teko
// ast.tks:643 — Field gains a trailing tag (default "" ⇒ additive-inert; the fixpoint argument):
//   pub type Field = struct { name: str; type_ann: TypeExpr; vis: Visibility; is_intern: bool;
//                             has_doc: bool; doc: str; is_readonly: bool; tag: str }
// ast.tks:618 — StructBody gains the type-level schema tag (default ""):
//   pub type StructBody = struct { fields: []Field; methods: []Function; implements: []str;
//                                  consts: []ConstDecl; is_readonly: bool; type_tag: str }
```

**Parse.** In `parse_fields`/`parse_class_fields`, AFTER `parse_type` returns `ty` (`parse_decl.tks:916`)
and BEFORE building the `Field`, optionally consume a trailing `Tag` token into `tag` (else `""`). For the
type-level tag, consume a `Tag` between the struct head and the `{` into `type_tag`.

**The byte-identity argument (two distinct claims, kept separate):**
1. **Self-host fixpoint (bin-a == bin-b):** holds — the change is deterministic; both generations built by
   the same compiler write the `tag` field identically. An untagged corpus produces `tag == ""` everywhere.
2. **`.tkb` cross-SEED wire:** adding a member shifts the on-disk `Field` layout, so a prior-seed artifact
   is NOT bit-compatible. Resolve with a `TKB_*_VERSION` bump at the single reseed crossing (the seed that
   first understands the member); the corpus may not WRITE a tag until that seed ships. This is the
   standard additive → reseed → sweep discipline; ★ ritual gates the fixpoint here.

**Existing fns touched:** `ast.tks` (`Field:643`, `StructBody:618`); `parse_decl.tks` (`:917`/`:990`/
`:1245`/`freeze_all_fields:1275`, + the type-tag consume + every `StructBody { … }` build site); `.tkb`
codec (`tkb_write.tks:377`/`:421`, `tkb_read.tks:630`/`:766`, `tkb_frame.tks` version). **Gate (★, full +
fixpoint):** `.tkt` — a trailing tag stores on `Field.tag`; untagged ⇒ `tag == ""`; a `readonly struct`
preserves the tag through `freeze_all_fields`; a `.tkb` round-trip of a tagged `Field`/`StructBody` equals
the original; an untagged corpus reseeds byte-identically.

### S3 — the per-format tag GRAMMAR + comptime tag parsers (grammar NOW; bodies §14-blocked)

**Goal.** Each format owns a value mini-grammar, parsed in comptime (the lexer/parser keep the tag an
opaque raw `str` — §3.4-c of the design, law-first: the language knows nothing of JSON). Grammar (design
§3.1–§3.3, §4.7.5): JSON `name|-|'' (,omitempty|,catchall|,string)*`; XML `(@name|>path|ns:local)
(,attr|,chardata|,cdata|,omitempty)*`; CSV `(col|idx|-) (,omitempty)*`; `fixed` `@offset+width
(,pad=…|align=…)*`. Field-level uses `format:` (name/option mapping); type-level uses `format=` (schema
ref) + `mode=` (§3.5) — the separator disambiguates.

```teko
/**
 * JsonSpec — the parsed field-level `json:"…"` tag: the JSON key (defaulting to the field name), and the
 * options that steer the synthesized routine. A COMPTIME aggregate consumed by the synthesis pass (§S5);
 * no JsonSpec, no tag string, and no tag parser reach runtime (the synthesis consumed them — M.0).
 *
 * @field name       the JSON key (the field name when the tag's name is empty)
 * @field skip       `json:"-"` — the field is omitted from encode and ignored on decode
 * @field omitempty  omit a nullable field when it is `null` (§9.D; no-op on a non-null field)
 * @field catchall   the empty-name catch-all sink (type must be `str | null` or `Json`); at most one per type
 * @field as_string  `,string` — encode a number/bool as a JSON string
 * @since 0.3.1 (serialization tags)
 */
exp type JsonSpec = struct { name: str; skip: bool; omitempty: bool; catchall: bool; as_string: bool }

/**
 * parse_json_tag — Family B comptime tag parser: split the raw backtick body, pick the `json:"…"` spec,
 * fold it to a comptime `JsonSpec`. Runs in `eval_const` (§14 B3); the tag is a compile-time literal, so
 * NO tag string and NO parser reach runtime. A malformed spec is a comptime diagnostic. Body BLOCKED on
 * §14's comptime engine (`eval_const` string walk); the SIGNATURE + grammar are fixed NOW.
 *
 * @param tag       the raw backtick body ("" when untagged)
 * @param fallback  the field's declared name (used when the json name is empty)
 * @return          the parsed json spec, as a comptime aggregate
 * @throws          a comptime diagnostic on a malformed spec
 * @since 0.3.1 (serialization tags)
 */
exp global comptime parse_json_tag(tag: str, fallback: str): JsonSpec { /* honest-stop until §14 B3 */ }
// siblings: parse_xml_tag → XmlSpec, parse_csv_tag → CsvSpec, parse_fixed_tag → FixedSpec (same shape)
```

**Existing fns touched:** none — new library module (`src/encoding/tags/` or per-format under
`src/encoding/{json,xml,csv,fixed}/`). **BUILDABLE NOW:** the `*Spec` types + the parser SIGNATURES +
`.tkt` grammar fixtures that assert the GRAMMAR (a hand-written parser test, not the comptime one).
**BLOCKED on §14 B3:** the comptime parser BODIES (need `eval_const`). **Gate:** `.tkt` —
`parse_json_tag("email,omitempty","email")` → `{name="email", omitempty=true}`; `"-"` → skip;
`",catchall"` → catchall; a bad option → error. (Runs as a plain unit test on the grammar until the
comptime engine wires the intrinsic.)

### S4 — extend `@fields<T>()` with `.tag` (contract against §14 B5; lands when B5 + S2 land)

**Goal.** §14 ships `FieldInfo` WITHOUT `.tag` in v1 and EXPLICITLY defers `.tag` to serialization (§14 §6,
tension 5). This crumb adds the `.tag` member and teaches the `@fields` projection to read `f.tag` off the
(S2-extended) `parser::Field` — §14's own recon says this is "no machinery change".

```teko
/**
 * FieldInfo (serialization extension) — §14's FieldInfo (name/is_nullable/vis) PLUS the raw backtick tag.
 * The added member is read directly off the S2-extended `parser::Field.tag` by the `@fields<T>()`
 * projection; §14's `deriver_field_view` walk is unchanged (§14 §6). Compile-time-only, M.0.
 *
 * @field name        the field's declared name (from §14)
 * @field is_nullable true iff the declared type is `X | null` (§9.D) (from §14)
 * @field vis         the field's visibility (from §14)
 * @field tag         the raw backtick tag body ("" when untagged) — this extension
 * @since 0.3.1 (serialization tags)
 */
exp type FieldInfo = struct { name: str; is_nullable: bool; vis: parser::Visibility; tag: str }
```

**Existing fns touched:** §14's `FieldInfo` decl (add `tag`) + the `@fields<T>()` projection point (read
`f.tag`). **BLOCKED on:** §14 B5 (`@fields` intrinsic) + S2 (`Field.tag`). **Gate:** `.tkt` —
`@fields<Tagged>()` returns each field's `tag` verbatim; an untagged field returns `tag == ""`.
**Coordination note (report up, do not action):** §14 must ship `FieldInfo` at a spot serialization can
extend additively; recommend §14 leave the `tag` member's ordinal LAST so this extension is append-only.

### S5 ★ — the TAGGED synthesis branches in `synthesize_serializers` (contract against §14 B6)

**Goal.** §14 B6 ships `synthesize_serializers` + its UNTAGGED path (field-name keys + `X|null`
nullability). This crumb adds, per field, the TAG-DRIVEN branches the synthesized `serialize_T`/`parse_T`
bake in — key rename, `omitempty`, `json:"-"`, `,catchall`, `,string`, XML attr/element/nil, CSV column/
index, fixed offset/width. Each is a COMPILE-TIME-decided branch (from the parsed `*Spec` + `f.is_nullable`
off §9.D); the emitted routine is straight-line direct field access — zero reflection (design §4.6, M.0).

Encode content (design §4.3), decode content (§4.4), catch-all (§4.5) — the decision tables the pass bakes:

| field shape | tag | `serialize_T` bakes (JSON) | `parse_T` bakes (JSON) |
|---|---|---|---|
| `T` non-null | — | always `"key": render(v.f)` | key absent/`null` → **decode error** (required) |
| `T\|null`, no `omitempty` | — | `v.f==null ? "key":null : …` | absent → bind `null`; present → coerce |
| `T\|null`, `,omitempty` | `omitempty` | omit the pair when `v.f==null` | absent → `null` |
| any | `json:"-"` | nothing | ignored |
| `str\|null`/`Json` | `,catchall` | append the field's OWN entries (a value loop, M.0 intact) | collect unmatched keys into it |

The `v.f == null` test is a direct union-tag read on the `X|null` fat descriptor (`{tag@0;ptr@8;len@16}`,
§9.D) — not reflection. Exactly one catch-all per type (a second is a compile error).

```teko
/**
 * synthesize_field_fragment — SYNTHESIS-internal (this crumb): given one `FieldInfo` (with its parsed
 * `*Spec`) and the target format, return the baked serialize/parse fragment for that field — the
 * compile-time-decided branch (key rename / omitempty / skip / catchall / string / xml attr-vs-element-vs
 * -nil / csv col-vs-idx / fixed offset+width). Appended, per field, to the ONE `serialize_T`/`parse_T`
 * `TFunction` §14 B6 assembles. `f.is_nullable` (§9.D) + the parsed spec decide the branch at compile
 * time, so the finished routine is straight-line, holding no spec, no tag, no reflection.
 *
 * @param f       the field descriptor (name/is_nullable/vis/tag)
 * @param spec    the field's parsed per-format spec (JsonSpec/XmlSpec/…)
 * @param fmt     the target format
 * @return        the baked serialize+parse fragment for this field, or a diagnostic (e.g. a second catch-all)
 * @throws        a compile diagnostic on an ill-typed catch-all or a duplicate catch-all
 * @since 0.3.1 (serialization tags)
 */
fn synthesize_field_fragment(f: FieldInfo, spec: FormatSpec, fmt: Format): FieldFragment | error { /* §14 B6 host loop */ }
```

**Existing fns touched:** §14's `synthesize_serializers` (the per-field loop calls this for the tagged
branch). **BLOCKED on:** §14 B6 (the pass exists) + S3 (the `*Spec` parsers) + S4 (`@fields.tag`).
**Gate (★, full, native exit codes):** `enc_omitempty` (a `,omitempty` null field → key absent),
`dec_missing_required` (non-null field absent → decode error), `json_skip` (`json:"-"` field never
appears), `catchall` (extra keys captured + round-trip), `one_routine_per_type` (two call-sites share ONE
`serialize_User` symbol — the owner's per-type-not-per-callsite ruling), `no_runtime_reflection` (no
`FieldInfo`/tag/type-metadata symbol survives — the M.0 assertion).

### S6 — schema-first (type-level tag → schema readers, validate/ugly, XML URI, manifest)

Two sub-crumbs: **S6a (BUILDABLE NOW)** — the pure parse/manifest layer; **S6b (§14-blocked)** — the
comptime schema readers.

**S6a — `type_tag` grammar + `.tkp [schema]` manifest + URI→path rule (NOW).** The type-level tag rides on
`StructBody.type_tag` (S2); its grammar is `format=("schema.json;schema2.json")` + `mode=("validate"|
"ugly")` (design §3.5). Add a `.tkp` `[schema]` table `URI → local path` and the deterministic,
hermetic-safe resolution order (design §4.7.4): (1) manifest map (authoritative), (2) vendored-path
convention `schemas/<sanitized-uri>.xsd`, (3) public fetch ONLY if the manifest opts in AND pins a
content hash, (4) unresolved → honest compile error naming the URI + searched paths. **Aliases are
DYNAMIC — never trusted:** a schema reference resolves by its literal path/URI, never by a document-local
alias/prefix (owner ruling; the XML side resolves by URI in S6b).

```
# .tkp
[schema]
"http://example.com/a" = "schemas/a.xsd"     # 1. manifest map — vendored, reproducible
# (2) else schemas/http_example_com_a.xsd by convention
# (3) remote fetch ONLY if:  allow-remote = true  AND a pinned sha256 is present   (default: no network)
```

**S6b — comptime schema readers + validate/ugly + XML URI (§14-blocked bodies).** The type-level schema
feeds the SAME synthesis pass (§S5). Recommended reconciliation (design §4.7.1 Opt-A): the Teko struct is
the source of shape; the compiler parses the JSON-Schema/XSD at compile time and ERRORS on structural
divergence (M.3); schema value-facets become checks baked into `parse_T` ONLY under `mode="validate"`
(elided entirely under `mode="ugly"` via a `comptime if` — zero runtime cost). `mode` governs VALIDATION,
never CONSTRUCTION (a non-null required field absent is still a decode error under `ugly`). XML namespaces
resolve by URI (design §4.7.3): a comptime URI→handler table + a runtime per-document prefix→URI remap;
the document prefix is arbitrary and never trusted.

```teko
/**
 * check_schema_conformance — SYNTHESIS-internal (S6b, Opt-A): at compile time parse the bound schema and
 * assert `@fields<T>()` conform (required↔non-null, type-compatible, XSD element order == declaration
 * order). A structural divergence is a COMPILE error naming the field + schema clause (M.3). Returns the
 * schema's value-facets as a comptime constraint list the parser bakes in under `mode="validate"`. Body
 * BLOCKED on §14's comptime engine; the SIGNATURE + the Opt-A ruling are fixed now.
 *
 * @param schema  the compile-time-parsed schema (JSON-Schema doc / XSD)
 * @param T       the type being reconciled
 * @return        the comptime value-constraint list, or a conformance error
 * @throws        a compile-time diagnostic when the struct diverges from the schema shape
 * @since 0.3.1 (serialization tags — schema-first)
 */
comptime check_schema_conformance<T>(schema: Schema): []Constraint | error { /* honest-stop until §14 B3 */ }
```

**Existing fns touched:** `.tkp` parser (add `[schema]` table); the S5 synthesis loop (consume the
type-level spec). **BUILDABLE NOW (S6a):** `type_tag` parse (rides S2), the `[schema]` manifest key, the
URI→path resolution rule + its `.tkt`. **BLOCKED (S6b):** the schema readers + conformance + validate
prologue (need §14 comptime). **Gate:** `schema_conformance` (required-without-non-null-field → compile
error), `mode_validate` (facet violation → decode error under validate; `0` under ugly),
`xml_ns_prefix_remap` (two docs, different prefixes, same URI → identical decode).

### S7 — the visible stdlib surface: `encode`/`decode` (honest-stop skeletons NOW)

**Goal.** The VISIBLE, explicitly-called trigger (never an invisible auto-derive — the no-shadow law, design
§2.3). Free generic functions ⇒ they ride `monomorph`; no #254 (a `user.encode()` method form would need
#254 — deferred).

```teko
/**
 * encode — serialize `v` to the format's text via the compiler-synthesized `serialize_T` (§S5). A VISIBLE
 * stdlib generic: the synthesis pass replaces this call with the per-type routine it built from `v`'s
 * visible field types + tags (the type IS the schema). Zero runtime reflection (M.0). One shared routine
 * per (T, format), memoized — not an inline per call-site (owner ruling). Honest-stop until §14 B6 wires
 * the synthesis; the SIGNATURE is the stable contract every call-site binds to.
 *
 * @param v  the struct value to serialize (STRUCT-ONLY DTO — a class is converted by the dev)
 * @return   the serialized text, or an error
 * @since 0.3.1 (serialization tags)
 */
exp fn encode<T>(v: T): str | error { /* honest-stop: replaced by serialize_T at synthesis (§14 B6) */ }

/**
 * decode — parse `s` into a `T` via the compiler-synthesized `parse_T` (§S5): required (non-null) fields
 * absent → decode error, `T | null` fields optional (§9.D). Visible trigger; one shared `parse_T` per
 * (T, format). Honest-stop until §14 B6.
 *
 * @param s  the input text
 * @return   the parsed value, or a decode error
 * @since 0.3.1 (serialization tags)
 */
exp fn decode<T>(s: str): T | error { /* honest-stop: replaced by parse_T at synthesis (§14 B6) */ }
// surface: teko::encoding::{json,xml,csv,fixed}::{encode,decode}
```

**Existing fns touched:** none — new stdlib modules. **BUILDABLE NOW** as honest-stop skeletons that
compile (the implementer fills the synthesis retarget when §14 B6 lands). **Gate:** compiles today;
end-to-end native fixtures gate at S5/§14 B6.

---

## 3. Fixtures (input → native exit code)

**`.tkt` (unit, pure — buildable now, no comptime):**
- `tag_lex.tkt` — backtick run → one `Tag`; body verbatim; unterminated → lex error. (S1)
- `tag_parse.tkt` — trailing tag → `Field.tag`; untagged → `""`; readonly struct preserves through
  `freeze_all_fields`; `.tkb` round-trip. (S2)
- `json_tag_grammar.tkt` / `xml_tag_grammar.tkt` / `csv_fixed_tag_grammar.tkt` — grammar folds (S3).
- `schema_manifest.tkt` — `.tkp [schema]` URI→path resolution order; unresolved → error (S6a).

**`.tkr` (native exit code — end-to-end; blocked until §14 B6):**

| fixture | input | exit |
|---|---|---|
| `enc_roundtrip` | `decode<User>(encode(u)) == u` (fully populated) | `0` |
| `enc_omitempty` | encode `User{email=null}` `,omitempty` → key absent | `0` |
| `dec_missing_required` | `decode<User>("{}")`, `id:u64` non-null-required | non-zero (decode error) |
| `dec_optional_absent` | doc missing a `T\|null` field → binds `null` | `0` |
| `json_skip` | a `json:"-"` field never appears in output | `0` |
| `xml_nil_presence` | null nullable → element omitted (or `xsi:nil`); absent → null | `0` |
| `catchall` | 2 extra keys → captured into a `Json` catch-all; re-encode round-trips | `0` |
| `one_routine_per_type` | two `json::encode(User)` sites → ONE `serialize_User` symbol (shared) | `0` |
| `no_runtime_reflection` | inspect `serialize_User` rodata/symbols → no FieldInfo/tag/type-metadata | `0` |
| `schema_conformance` | schema required prop with no matching non-null field → COMPILE error | non-zero (compile-fail) |
| `mode_validate` | facet violation under `validate` → decode error; same input under `ugly` → `0` | validate: non-zero · ugly: `0` |
| `xml_ns_prefix_remap` | same doc, two xmlns prefixes for the expected URI → identical decode | `0` |
| `csv_fixed_roundtrip` | `Order` (CSV) + `Record` (fixed-width) round-trip | `0` |

Each exit/token encodes WHICH branch ran (axis-law: assert the value, never an incidental effect).

---

## 4. Ritual points (full gate + fixpoint)

- **S2 ★** — the AST-member + codec + reseed crumb: full C+self-host+native gate AND `.tkb` fixpoint
  (untagged corpus byte-identical; the `TKB_*_VERSION` bump is the one crossing). No corpus writes a tag
  until this seed ships.
- **S5 ★** — the tagged synthesis: full gate with the end-to-end + M.0 fixtures (`one_routine_per_type`,
  `no_runtime_reflection`). Gates on §14 B6 already reseeded.
- **§14 rituals (upstream, out of scope here):** A0–B6 + seed bump S per `section14-…-v1.md §7`; the
  serialization crumbs S4/S5/S6b resume only after §14's `@fields`/`synthesize_serializers`/comptime
  engine land.

---

## 5. Law tensions + HALT check

Inherits the design doc's §6 resolutions (all law-first, no HALT): M.0 (reflection is a comptime VALUE;
the runtime routine holds no metadata); no-shadow (a VISIBLE `encode<T>` trigger, fully determined by
visible tags+types — not an invisible auto-derive); §14 two-family SEAL (the synthesis is a monomorph-style
pass, not a third macro class); `.tkb` byte-identity (default-`""` members, additive→reseed); hermetic
build (schema read local-first, remote opt-in+pinned); schema divergence (compile error, struct-is-truth);
`ugly` honesty (mode governs validation, not construction). **No genuine unresolved tension → NO HALT.**

**Flagged for owner CONFIRMATION (not invented — reported, do not action):**
1. **Reflection Extent-3 ratification** — `@fields<T>()` needs `T.fields` (guarded to comptime descriptors
   only). This is §14's confirmation (already recommended in `section14-…-v1.md §8`); the serialization
   crumbs S4+ inherit it. Named so the fold-in tracks the dependency, not a new question from me.
2. **`.tkp [schema]` remote-fetch policy (S6a)** — the design resolves it hermetically (local-first;
   remote only opt-in + content-pinned; default no network). This adds a NEW manifest capability + a
   build-network policy; recommend the owner CONFIRM the opt-in+pin default before the fetch path is built
   (the local-first path needs no confirmation and is buildable now).
3. **Method-form `user.encode()`** — deferred (needs #254). The free-function `json::encode(u)` ships
   first; confirm whether the method sugar is wanted for a later wave (design §4.2). Not a blocker.

---

## 6. What is BUILDABLE NOW vs BLOCKED (design-ahead honesty)

**BUILDABLE NOW (no §14 dep):** S1 (lexer `Tag` token), S2 (`Field.tag`/`StructBody.type_tag` AST + codec
+ parse + reseed), S3 grammar (the `*Spec` types + parser signatures + grammar `.tkt`), S6a (`type_tag`
parse + `.tkp [schema]` manifest + URI→path rule), S7 (`encode`/`decode` honest-stop skeletons). These land
additively behind the S2 reseed and perturb nothing (all `""`-default, all inert).

**BLOCKED on §14 (resumes in minutes when the named producer crumb lands):**
- S3 comptime parser BODIES → §14 B3 (`expand_comptime`/`eval_const`).
- S4 (`@fields.tag`) → §14 B5 (`@fields<T>()`) + S2.
- S5 (tagged synthesis) → §14 B6 (`synthesize_serializers`) + S3 + S4.
- S6b (schema readers + validate/ugly + XML URI) → §14 B3 comptime engine.

The implementer resumes by: landing S1+S2 behind the reseed, keeping S3/S6b parser+reader bodies and
S5/S7 as honest-stops, and wiring them the moment §14 B2/B3/B5/B6 close — each against the DECLARED §14
shape this doc contracted to (§1).

*Grounding: every `file:line` re-verified on `origin/fix/retirement` this session (§1). Producer contract:
`section14-macro-comptime-impl-v1.md` (B2/B3/B5/B6, the declared shapes). Design source:
`serial-tags-comptime-field-reflection-0.3.1.md` (the SEALED mechanics this sequences). Companion seals:
`plano-macro.md` (two-family), `mudancas-superficie-0.3.1.md` §9.D/§14.*
