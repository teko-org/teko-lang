# Fix: cross-namespace collision of `T | error` / `T | null` union C symbols

Owner ruling 2026-08-16: a REAL compiler bug, to be RESOLVED (not worked around). The `ini`
`KeyStep` rename must become unnecessary.

Status: DESIGN (architect). No product code edited by this document. Validation is COMPILE-only
(`cc` the emitted C, run the fixture natives, check exit codes) — **NEVER `teko test .`** (owner
rule: local test = OOM; CI only).

---

## 0. The bug in one line

Two user structs with the SAME bare name in DIFFERENT namespaces
(`teko::encoding::toml::KeyStep`, `teko::encoding::ini::KeyStep`), each used in a `T | error`
(or `T | null`) union, emit the IDENTICAL C tag constant, tag-enum, `tk_u_` typedef and `.as.`
field, so they collide at `cc` with `incompatible types` — although both namespaces type-check in
isolation.

---

## 1. Root-cause trace — WHERE the namespace identity is lost (file:line evidence)

The owner's diagnostic criterion is correct and, followed to its end, PROVES the loss is in
codegen, not the resolver/dedup:

> "A real type collision would be a duplicate-type error. There is none — the table holds both as
> distinct registrations, so the compiler KNOWS they are distinct."

Following the identity from "the table knows it" down to "codegen emits the C symbol":

1. **Resolution of a union member reference PRESERVES the full canonical identity.**
   `resolve_type` for a union (`src/checker/resolve.tks:2167`) resolves each member with
   `resolve_type` → `resolve_named` (`resolve.tks:2155`). For a user type `resolve_named` returns
   (`resolve.tks:1339`):
   ```
   Named { name = qualify(type_ref_ns(path, table, ref_ns), name) }
   ```
   `qualify(ns, name)` (`resolve.tks:282`) = `ns ~ "::" ~ name`; `type_ref_ns` (`resolve.tks:896`)
   returns the WINNING namespace. So `KeyStep` written bare inside `toml` resolves to
   `checker::Named { name = "teko::encoding::toml::KeyStep" }` — the **FULL canonical name**. The
   bare `TypeReg.name` at `resolve.tks:526` is only the table's storage/`by_last` index key;
   `resolve_named` RECONSTRUCTS the canonical name from `type_ref_ns` + the segment, so the
   distinction survives into `Named.name`.

2. **`union_dedup` / `type_eq` do NOT strip and do NOT wrongly merge.**
   `type_eq` (`src/checker/type.tks:205`) compares `na.name == nb.name` over the WHOLE name. The two
   members carry `"…toml::KeyStep"` vs `"…ini::KeyStep"` → unequal → never deduped. The checker is
   correct. (This is exactly why there is no duplicate-type error: the identities are distinct all
   the way through typing.)

3. **Codegen's RESOLVED-type mangle path RECEIVES the full name and DELIBERATELY STRIPS it.**
   `cg_opt_mangle_str` Named arm (`src/codegen/codegen.tks:2036`) and its buffer twin
   `cg_opt_mangle` Named arm (`codegen.tks:1990`):
   ```
   checker::Named as nm => checker::name_last_segment(nm.name)   // "(#109 W3) BARE key"
   ```
   `nm.name` is `"teko::encoding::toml::KeyStep"`; `name_last_segment` throws the namespace away and
   returns `"KeyStep"`. **This is the identity-loss step for the resolved path.** Codegen strips a
   full name it already holds.

4. **Codegen's SYNTACTIC and PATTERN mangle paths never look the identity up.**
   `cg_opt_mangle_texpr_str` (`codegen.tks:2737`) and `cg_opt_mangle_texpr` (`codegen.tks:2969`)
   read `nt.path.segments[last].name` — the **source-written** spelling, bare `"KeyStep"`, straight
   off the parser AST, never consulting the table. Likewise the match-arm case key
   `cg_emit_case_key_str` (`codegen.tks:7353/7355`) uses `cg_path_last(bp.type_name)` — again the
   bare source spelling. For these paths the identity was never handed in; codegen must re-derive it
   from the table (it already has the seam — see §3).

**Verdict: the fix LANDS IN CODEGEN.** The resolver and the dedup are correct and are left
untouched — changing `type_eq`/`union_dedup` would be wrong (they already keep the types distinct).
The two mangle paths were deliberately kept "bare-matching-bare" so a declaration-position
reference (syntactic) and an expression-position reference (semantic) would agree; the repair is to
make BOTH — and the pattern path — use the FULL canonical name and produce the SAME bytes.

Why the resolved path was made to strip: `name_last_segment` keeps `tk_u_…` / `TK_TAG_…` a valid C
identifier and was assumed to match the syntactic path. But `mangle_type_name` (`codegen.tks:533`)
already proves the namespace-qualified form IS a valid C identifier — it emits the collision-FREE
`tk_t_teko__encoding__toml__KeyStep` for the STRUCT via `cb_tysym`'s `::`→`__` collapse
(`codegen.tks:483`). The union key must simply follow the same scheme.

---

## 2. All-sites audit — every emitter of a union key / tag / field / destructure / typedef

`M` below = a union member's C key. Sites are grouped by the mangle FAMILY that produces `M`.

### A. RESOLVED-type family — routes through `cg_member_key` / `cg_member_key_str`
All of these are repaired at once by fixing the single Named leaf they share (`cg_opt_mangle` /
`cg_opt_mangle_str`, §1 step 3):

| site (`codegen.tks`) | what it emits | via |
|---|---|---|
| `cg_variant_typename:2161` | `tk_u_<M…>` typedef NAME | `cg_member_key` |
| `cg_union_mangle_str:2058` | `u_<M…>` suffix | `cg_member_key_str` |
| `cg_emit_inline_variant_typedef:11410` | tag ENUM def + struct def + `.as.<M>` FIELDS | `cg_member_key`/`_str` |
| `cg_wrap_open:6205` (Variant arm) | `TK_TAG_U_<M…>_<M>` const + `.as.<M>` | `cg_member_key_str` |
| `cg_emit_tag_prefix:7318` (Variant arm) | `TK_TAG_U_<M…>_` prefix | `cg_member_key_str` |
| destructure `codegen.tks:3753/3759` | `.as.<M>` on a resolved member | `cg_member_key_str`/`cg_member_key` |
| `cg_var_mkey:6056` (Variant arm) | member key for reachability | `cg_member_key_str` |

### B. SYNTACTIC type-expr family — routes through `cg_member_key_texpr_str` (bare source path)
| site (`codegen.tks`) | what it emits |
|---|---|
| `emit_type_expr:2946` / `:2955` (UnionType arm) | the `tk_u_<M…>` C TYPE in a field/param/return/variant-member position, via `cg_variant_typename_texpr:2776` |
| `cg_texpr_union_ready:11621` / `:11624` | the readiness/emitted-set KEY for a null-free inline union |
| `cg_var_mkey:6063` (Named-variant arm) | a NAMED variant's member key |
| named-variant decl `11005` / `11028` | a NAMED variant's tag-enum member key + `.as.<M>` field name |
| `cg_emit_case_key_str:7347` (slice arm) | a `[]T as x` slice-pattern case key |

### C. PATTERN / destructure family — bare source spelling
| site (`codegen.tks`) | what it emits |
|---|---|
| `cg_emit_case_key_str:7353/7355` | `.as.<case>` + `TK_TAG_…_<CASE>` from `cg_path_last(pattern)` — consumed at `7461/7469/7478/7498/7731/7747/7763` |
| `cg_group_case_key:7356` | nested-union grouped case key |

### D. NAMED-VARIANT TAG STEM — the "second instance" (`type X = A | B` cross-ns)
The `tk_tag_<X>` enum TYPE name is already qualified (`cb_tysym` at `11000/11014`), but the
CONSTANTS inside are bare and collide for two same-bare named variants in different namespaces:
| site (`codegen.tks`) | emits | current (BARE) |
|---|---|---|
| `cg_wrap_open:6229` (Named arm) | `TK_TAG_<X>_<M>` const | `name_last_segment(nm.name)` |
| `cg_emit_tag_prefix:7333` (Named arm) | `TK_TAG_<X>_` prefix | `name_last_segment(nm.name)` |
| named-variant tag-enum def `11007` | `TK_TAG_<X>_<M>` def | `name_last_segment(d.name)` |

### E. DO-NOT-TOUCH — bare COMPARISON sites (correctness depends on staying bare)
`codegen.tks:7447` and `:7864` compare `cg_path_last(pattern) == name_last_segment(nm.name)` to MATCH
a pattern to a subject member. These are BARE-vs-BARE *matching* predicates (the #109/#290
bare-vs-canonical split) and MUST stay bare — only the EMITTED key changes. Changing these would
break pattern selection.

### F. OUT OF SCOPE — adjacent latent collisions (REPORT up, do not fix here)
- **Generic-instance names** `Base__g__<arg>` (`cg_texpr_mangle:2800` / `cg_union_texpr_inst_mangle:2821`
  and the checker's `generic_inst_name`/`type_mangle`, `resolve.tks:~2313`). Same bare-name hazard
  (`Box<toml::KeyStep>` vs `Box<ini::KeyStep>`), but a DIFFERENT C-symbol family that stays
  internally consistent (checker stamp == codegen use, both bare) — fixing it requires a lockstep
  checker+codegen change and its own reseed. Left bare here (no regression); reported as an adjacent
  finding.
- **Enum constants** `TK_E_<E>_<M>` (`codegen.tks:11048`, uses `7455`/`8420`) — two same-bare `enum`
  decls in different namespaces collide the same way. Separate symbol family; adjacent finding.

Note: standalone `tk_opt_<T>` / `tk_slice_<T>` typedef names ARE inside scope — they share the
`cg_opt_mangle` / `cg_opt_mangle_texpr` leaves with the union path, so §3's fix hardens them too
(a welcome consequence, and required for internal consistency once a union member is a `[]T`/`T?`).

---

## 3. The qualified-key scheme (the invariant)

**One rule, applied at every site in families A–D:** a union member/element/stem key for a user
`Named` type is the type's FULL CANONICAL name with each `::` collapsed to `__`
(`cb_tysym`/`mangle_type_name`'s existing scheme). Builtins, `error`, `null`, `prim`, `str`, `byte`,
`char`, and the `slice_`/`opt_`/`u_` structural prefixes are UNCHANGED.

Add one leaf helper (str twin of `cb_tysym`):
```
/**
 * cg_ns_c_key — a canonical type name as a C-identifier fragment: each `::` collapsed to `__`
 * (the str twin of `cb_tysym`), so `"teko::encoding::toml::KeyStep"` becomes the valid, namespace-
 * unique fragment `"teko__encoding__toml__KeyStep"`. A name with no `::` (a root-namespace or
 * builtin name) is returned byte-for-byte unchanged, which is why every existing root-namespace
 * union keeps its historic C symbol.
 *
 * @param name  a canonical (possibly `::`-qualified) type name
 * @return      the name with every `::` rewritten to `__`
 */
fn cg_ns_c_key(name: str): str { str(cb_tysym(teko::list::empty(), name)) }
```

**Byte-identity across the three paths** (the whole reason bare was originally chosen):
- RESOLVED path emits `cg_ns_c_key(nm.name)`.
- SYNTACTIC path emits `cg_ns_c_key(cg_canon_name_ns(prog, nt.path, ref_ns))`.
- PATTERN path emits `cg_ns_c_key(cg_canon_name_ns(prog, bp.type_name, ref_ns))` (or, preferred,
  reuses the resolved subject member's key — §4 C3b).

`cg_canon_name_ns` (`codegen.tks:1010`) already resolves a syntactic path to its canonical decl
name in `ref_ns` using the type table (it is the codegen seam for re-deriving the identity the
parser AST does not carry). All three therefore feed `cg_ns_c_key` the SAME canonical string
(`resolve_named`, `cg_canon_name_ns` and `cg_find_decl_ns` all read the same table registration) →
byte-identical output.

**Root-namespace / builtin invariant (bounds the reseed):** for a root type `Foo`,
`nm.name == "Foo"` (no `::`) → `cg_ns_c_key("Foo") == "Foo"` == today's `name_last_segment`. For a
builtin `error`/`i64`/`str`, the Prim/Error/Null/Str/Byte arms are untouched. So `Foo | error` and
`i64 | error` in the ROOT namespace mangle **byte-identically to today**; only user types that
actually carry a namespace change. (In THIS corpus most user types ARE namespaced, so the emitted-C
diff is nonetheless broad — see §6.)

---

## 4. Ordered crumb sequence

Because a union's typedef, tag-enum, tag-constant use, `.as.` field and match-destructure must ALL
agree for `cc` to accept the translation unit, a half-flipped union does not compile. The crumbs are
sequenced for reviewability, but **C1+C2+C3 form ONE atomic ritual unit** (they go green only
together). C0 and C4 are independently green.

### Crumb 0 — add `cg_ns_c_key`
- Edit: `codegen.tks` (near `cb_tysym`, ~497). Add the helper from §3.
- Gate: `teko` builds; no behavior change (helper unused yet). Independently green.

### Crumb 1 — RESOLVED member-key qualification (repairs the identity-loss step)
- `cg_opt_mangle:1990` Named arm: `cb_str(buf, checker::name_last_segment(nm.name))` →
  `cb_tysym(buf, nm.name)`.
- `cg_opt_mangle_str:2036` Named arm: `checker::name_last_segment(nm.name)` → `cg_ns_c_key(nm.name)`.
- `cg_member_key_is_c_keyword:2127` Named arm: `cg_is_c_keyword(checker::name_last_segment(nm.name))`
  → `cg_is_c_keyword(cg_ns_c_key(nm.name))` (a namespaced key contains `__` and is never a keyword;
  a root key is unchanged — this keeps `cg_member_key`'s buffer escape byte-identical to
  `cg_member_key_str`'s).
- Before/after C symbols for `teko::encoding::toml::KeyStep | error`:
  - typedef `tk_u_keystep_error` → `tk_u_teko__encoding__toml__KeyStep_error`
  - tag `TK_TAG_U_KEYSTEP_ERROR_KEYSTEP` → `TK_TAG_U_TEKO__ENCODING__TOML__KEYSTEP_ERROR_TEKO__ENCODING__TOML__KEYSTEP`
  - field `.as.keystep` → `.as.teko__encoding__toml__KeyStep`
- Repairs every family-A site (§2.A) at once.
- Gate: NOT independently green (family-B/C references still bare). Gate at the C3 ritual.

### Crumb 2 — SYNTACTIC type-expr member-key qualification
Thread `(prog, ref_ns)` into the syntactic mangle chain and canonicalize the source path:
- `cg_opt_mangle_texpr_str:2737` NamedType arm →
  `cg_ns_c_key(cg_canon_name_ns(prog, nt.path, ref_ns))`.
- `cg_opt_mangle_texpr:2969` NamedType arm → `cb_tysym(buf, cg_canon_name_ns(prog, nt.path, ref_ns))`.
- Add `(prog, ref_ns)` params to the callers in the chain: `cg_member_key_texpr_str:2746`,
  `cg_union_texpr_mangle_str:2718`, `cg_variant_typename_texpr:2776`.
- Update the callers, all of which already hold `prog` + a namespace:
  `emit_type_expr:2946/2955` (`prog`, `ref_ns`); `cg_texpr_union_ready:11621/11624`
  (`prog`, `owner_ns`); `cg_var_mkey:6063` (`prog`; `ref_ns = checker::name_qualifier(nm.name)`);
  named-variant decl `11005/11028` (`prog`; `ref_ns = checker::name_qualifier(d.name)`).
- Builtins/`null`/slice-prefix arms unchanged (only the NamedType arm resolves+qualifies).
- **Owner-preferred variant for C2 ONLY (Layer A, no codegen threading):** extend the resolver's
  existing AST-normalization `normalize_inst_texpr` (`resolve.tks:3428` — it ALREADY rewrites a
  `NamedType.path` for generic instances) to also rewrite a plain user-type `NamedType.path` to the
  canonical multi-segment path via `name_to_path(decl.name)` (`resolve.tks:857`). Then
  `cg_opt_mangle_texpr_str` reads an already-canonical path and only needs to JOIN segments with
  `__`. NamedType has no dedicated slot and needs none — the EXISTING `path` field is rewritten,
  exactly as the generic-instance branch already does; NO new field, NO constructor churn.
  Limitation: `normalize_inst_texpr` walks type-decl bodies + fn/method SIGNATURES only, NOT match-arm
  patterns, so it CANNOT cover Crumb 3. Since C3 must live in codegen regardless, this design
  recommends keeping C2 in codegen too (one uniform mechanism). Layer A is documented as the
  owner's alternative for the type-expr sub-path if the AST-rewrite is preferred over threading.
- Gate at the C3 ritual.

### Crumb 3 — PATTERN / destructure case-key qualification
The emitted `.as.<case>` / `TK_TAG_…_<CASE>` in a match arm must equal the (now qualified) union
field/tag. Two options:
- **C3a (canonicalize the pattern path):** thread `(prog, ref_ns)` into `cg_emit_case_key_str:7341`
  (and `cg_case_name_escaped`, `cg_group_case_key`), and for the Named case emit
  `cg_ns_c_key(cg_canon_name_ns(prog, bp.type_name, ref_ns))`; for the slice case, canonicalize the
  element. Ripples `(prog, ref_ns)` to the 7 consumers (`7461/7469/7478/7498/7731/7747/7763`).
- **C3b (RECOMMENDED — derive from the resolved subject member):** the arm already matches a pattern
  to a subject `Variant` member by bare compare (`7447/7864`). Emit the case key from that matched
  member's RESOLVED type via `cg_member_key_str(member_type)` (the §-C1-fixed semantic key), instead
  of `cg_emit_case_key_str(pattern)`. Single source of truth, reuses C1, no pattern-path threading;
  requires the arm to carry the matched member's `@Type()` (already reachable from the subject).
- Keep the bare COMPARE at `7447/7864` UNCHANGED (§2.E).
- Gate (RITUAL): full green `teko` self-build + the C1–C3 regression fixtures (§5) compile and run
  with expected exit codes + the fixpoint (§6). This is the first point the tree compiles.

### Crumb 4 — NAMED-VARIANT tag-stem qualification (independent, self-consistent)
- `cg_wrap_open:6229`: `cb_upper(out, checker::name_last_segment(nm.name))` →
  `cb_upper(out, cg_ns_c_key(nm.name))`.
- `cg_emit_tag_prefix:7333`: same rewrite.
- named-variant tag-enum def `codegen.tks:11007`: `cb_upper(out, checker::name_last_segment(d.name))`
  → `cb_upper(out, cg_ns_c_key(d.name))`.
- `d.name`/`nm.name` are already canonical, so this is pure codegen (no resolver seam). The
  `tk_tag_<X>` TYPE name is already qualified; this makes its CONSTANTS unique too.
- Before/after for a named variant `X` declared in `teko::a` vs `teko::b`:
  `TK_TAG_X_…` → `TK_TAG_TEKO__A__X_…` vs `TK_TAG_TEKO__B__X_…`.
- Gate: def + both uses flip together → independently green; run its fixture case (§5, `named_variant_ns`).

### Ritual points
- After Crumb 3 (the C1+C2+C3 unit) — FULL gate + fixpoint + all §5 fixtures.
- After Crumb 4 — FULL gate + fixpoint + `named_variant_ns` fixture.
- Final — the reseed ritual (§6).

---

## 5. Regression fixtures (COMPILE-and-run; assert native exit codes)

New dir `examples/regressions/union_ns_collision/`, laid out like
`examples/regressions/recursive_union/` (a `.tkp` with `kind = "binary"`, a `.tkr` naming each
scenario, `src/` with the namespaced modules, and a `main.tks` that calls each probe and returns an
exit code). Each probe returns a distinct integer so an ALIASED field (the bug) would return the
wrong value and fail the exit-code assertion.

| case | shape (the exact thing that broke) | expected native exit |
|---|---|---|
| `cross_ns_named` | `mod::a::KeyStep` and `mod::b::KeyStep` (same bare name, different ns), each returned from a fn as `KeyStep \| error`; `a` carries payload 11, `b` carries 22; `main` reads both and returns `a_val*10 + b_val_ok` | `111` (proves distinct `tk_u_`/tag/field, no aliasing) |
| `cross_ns_null` | same two structs, each in `KeyStep \| null`; a present-value probe and a `null` probe per namespace | `0` on all-ok, non-zero on mismatch |
| `cross_ns_destructure` | `match` over `a::KeyStep \| error` binding `KeyStep as x`, reads `x`'s field | field value (proves the destructure case key == the qualified field) |
| `named_variant_ns` | `type Step = A \| B` declared in BOTH `mod::a` and `mod::b`; construct + `match` both | distinct per-namespace values (proves Crumb 4) |
| `root_unchanged` | a ROOT-namespace `Foo \| error` and `Bar \| null` | unchanged behavior; its C symbols are asserted byte-identical to pre-fix by the fixpoint diff (§6) |
| `builtin_union` | `i64 \| error`, `str \| null` | unchanged (proves builtins/error/null keys untouched) |

The `cross_ns_named` case is the precise reproduction of finding #4 and must FAIL on the pre-fix
compiler (with the `ini` rename reverted) and PASS after. `root_unchanged` + `builtin_union` guard
against regressions to the unchanged mangling.

---

## 6. Fixpoint + reseed plan (COMPILE-only)

The fix renames the mangled name of every union/opt/slice that carries a NAMESPACED user type →
`teko.c` diverges broadly (root-namespace and builtin unions stay byte-identical, but this corpus is
heavily namespaced, so expect a wide diff). Codegen is deterministic, so the fixpoint still holds.

1. Build a `gen1` compiler from the FIXED source using the current release seed
   (`bootstrap/teko.c`; the fix adds no language feature, so the seed compiles it) via the ladder
   (`scripts/build_with_seed_fallback.sh` / `scripts/degrau.sh`).
2. `gen1` (native) builds `gen2` and emits `gen2`'s `teko.c`; `gen2` builds `gen3` and emits
   `gen3`'s `teko.c`.
3. **Fixpoint gate** (`scripts/fixpoint_gate.sh`): assert `gen2.c == gen3.c` byte-identical.
4. **Reseed** `bootstrap/teko.c := gen2.c` by hand-harvest (`run: hand-harvest`), refresh
   `bootstrap/PROVENANCE` / `bootstrap/DEGRAU`, and pass `scripts/provenance_gate.sh`.
5. Validation is COMPILE-only: `cc` the emitted C, run the §5 fixture natives, assert exit codes.
   **NEVER `teko test .`.**

Bounding note for the reviewer: diffing `gen2.c` against the pre-fix `teko.c` should show changes
ONLY in `tk_u_*` / `tk_tag_*` / `TK_TAG_*` / `.as.*` tokens for NAMESPACED types, plus `tk_opt_*` /
`tk_slice_*` for namespaced element types; every root-namespace and builtin union token must be
untouched. Any change outside that set is a bug in the fix.

---

## 7. Risks + law tensions

- **Identifier length.** Deeply-namespaced keys (`tk_u_teko__encoding__toml__KeyStep_error`, and the
  UPPERCASE tag doubling the stem) are long, but `mangle_type_name` already emits equally-long
  `tk_t_teko__encoding__toml__KeyStep` for the STRUCT, so no NEW length regime is introduced; modern
  `cc` (gcc/clang) impose no practical limit. Low risk.
- **New ambiguity from `::`→`__`.** The collapse is the SAME scheme `cb_tysym`/`mangle_type_name`
  already use tree-wide (proven collision-free in this corpus: `x::a__b` → `x__a__b` vs `x::b::a` →
  `x__b__a` stay distinct). No new ambiguity. Low risk.
- **Bare-vs-canonical split (#109/#290).** Only EMITTED keys change; the bare *matching* predicates
  at `7447/7864` (pattern-vs-member) stay bare and MUST NOT change — called out in §2.E. Getting
  this wrong would silently break pattern selection; the fixture `cross_ns_destructure` guards it.
- **Coverage completeness.** A missed union-symbol site emits a qualified/​bare mismatch → `cc`
  `unknown type name` or `incompatible types`. The §2 audit table is the guard; the full self-build
  is the gate (a mismatch cannot pass `cc`).
- **Reseed breadth.** Broad `teko.c` diff (see §6). The fixpoint (`gen2==gen3`) proves the new
  mangling is deterministic and self-reproducing regardless of diff size.
- **Law tension (resolver-vs-codegen layer) — RESOLVED, no HALT.** The owner's hypothesis placed the
  loss in the resolver/dedup. The trace (§1, `resolve.tks:1339` + `type.tks:205` + `codegen.tks:2036`)
  shows the resolved `Named.name` reaches codegen FULLY canonical and codegen strips it — so the loss
  is in codegen, and `type_eq`/`union_dedup` must be left ALONE (they correctly keep the types
  distinct). The owner's own criterion ("no duplicate-type error ⇒ the table knows them distinct")
  is what proves `Named.name` is already distinct, i.e. the strip is downstream in the mangle. The
  owner explicitly directed landing "at the exact identity-loss step, on the evidence"; that step is
  `codegen.tks:2036/1990` (resolved) plus the un-resolved syntactic/pattern paths
  (`2737/2969/7353`). No genuine unresolved tension remains → no HALT.
- **Adjacent findings REPORTED up (not fixed here):** generic-instance `Base__g__<same-bare-arg>`
  collision (§2.F, needs lockstep checker+codegen + own reseed) and enum `TK_E_<same-bare-enum>`
  collision (§2.F). Both are the same bare-name hazard in SEPARATE C-symbol families and are outside
  this issue's proposal.
