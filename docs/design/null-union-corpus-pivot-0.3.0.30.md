# 0.3.0.30 — Null-Union CORPUS PIVOT + SUGAR ERADICATION (design)

Owner-directed, decisive. Umbrella `remodel/emit-throughput` (base 62d59a1; null-union C3+C4+C5 merged).
**DESIGN ONLY** — this document is the executable plan. No product code is written here.

Directive, verbatim intent:
1. Move EVERY nullable-sugar `T?` in the whole codebase to explicit `T | null`.
2. Remove ALL sugar entirely — DELETE `T?`, `?.`, `??` from parser/checker/codegen. Eradicate, do not re-target.
3. Rewrite `?.`/`??` USES to explicit match/narrowing BEFORE deleting the sugar.

Hard ordering law: the compiler compiles its own source. So **rewrite the whole corpus off the
sugar first (buildable by the released 0.3.0.29 seed), validate, THEN delete the sugar from the
compiler.** If the parser loses `T?`/`?.`/`??` before the corpus is off them, the compiler can no
longer parse itself.

---

## 0. As-built map (read before touching anything)

Tokens (`src/lexer/token.tks` 100-104, `src/lexer/lexer.tks` 645-678) — `?` is EXCLUSIVE to nullability:
- `TokenKind::Question` — lone `?`, the type suffix `T?` (lexer.tks:678).
- `TokenKind::QDot` — `?.` safe-nav (composite munch, lexer.tks:648).
- `TokenKind::QQ` — `??` coalesce (composite munch, lexer.tks:647).

Parse sites:
- Type suffix `T?`: `src/parser/parse_type.tks` 64-70 (`parse_type_primary`, the postfix `?` loop → `OptionalType`).
- `<T?>` / constraint `?` REJECTION guards: `src/parser/parse_decl.tks` 120-123 and 170-174 (they error on `?`).
- `??`: `src/parser/parse_expr.tks` 402-411 (`parse_coalesce`, top of the expr ladder → `Coalesce`).
- `?.`: `src/parser/parse_expr.tks` 433-450 (in `parse_postfix` → `SafeFieldAccess` / `SafeMethodCall`).

AST (`src/parser/ast.tks` 46-49, 97; `src/parser/type.tks` 8, 11):
- `OptionalType { inner }`  (TypeExpr variant member).
- `SafeFieldAccess { receiver, field }`, `Coalesce { left, right }`, `SafeMethodCall { receiver, method, args }` (ExprKind variant members).

Checker representation (`src/checker/type.tks` 71, 102):
- `type Optional = struct { inner: Type }` and the `Type` variant member `Optional`.
- `T?` → `Optional { inner }` in `resolve_type` (`src/checker/resolve.tks` 1608-1623).
- `T | null` → **`Variant { members = [Null, …] }`** in `resolve_type` (resolve.tks 1565-1587) via
  `union_normalize_null` (resolve.tks 1461-1478). **This is the load-bearing fact of the whole pivot:
  `T | null` and `T?` are DIFFERENT checker types** — the first a `Variant` with a `Null` member, the
  second an `Optional` former. They are compiled by different code paths (`Variant`-with-`Null` is the
  C3/C4/C5 niche/box/ref-local representation; `Optional` is the legacy sugar former).

Type identity/assignability (verified):
- `type_eq(Optional{T}, Variant{[Null,T]})` = **false** (`src/checker/type.tks` 142-149).
- `widens_into` bridges NEITHER direction between `Optional{T}` and `Variant{[Null,T]}` (`src/checker/resolve.tks` 748-814):
  the `to: Optional` arm only present-wraps a `T`; the `to: Variant` arm only accepts a member-widen.
- BUT a **bare `null` literal** widens into BOTH (`from_is_bare_null` + `null_widens_into`, resolve.tks 751),
  a **present `T` value** widens into BOTH, and **`match` narrowing consumes BOTH identically**
  (`check_pattern` Optional arm at match.tks 163-168 mirrors the `Variant`+`Null` arms). ⇒ the ONLY
  place the strict boundary can bite is a **whole-optional VALUE forwarded across an annotation
  boundary without narrowing** (see §3 batch-hazard rule).

Seed-safe narrowing idiom — proven by the pristine-0.3.0.29-seed-building fixture
`examples/regressions/repr_niche/src/probe.tks` (builds + exits 44 with the released seed):

```
let present: Node | null = Node::make(7)
match present {
    null      => a = 100
    Node as n => a = n.value
}
```
and `null | error`:
```
let ok: null | error = null
match ok {
    null  => a = 5
    error => a = 40      // no `as` binder needed when the value is unused
}
```
The seed handles `match x { null => …; T as v => … }` on `T | null`. This is the target form for
every rewrite below. The ONE construct the 0.3.0.29 seed CANNOT build is `Ref<T> | null` LOCAL
narrowing (C5, fixture `ref_null_local`) — see §5; the pivot never needs it.

---

## 1. INVENTORY (grep-driven, comments + string-literals stripped, line-accurate)

Counter: a state-machine stripper removes `/* */`, `//`, and string contents, then counts the three
constructs. `.claude/worktrees`, `gen1out`, `bin`, `init_test_scratch`, `.teko` excluded (agent/build
artifacts). Reproduce with the script in the crumb-review appendix (§6).

### 1.1 Grand totals — the migration surface

| Surface            | `T?` (Optional suffix) | `?.` (QDot) | `??` (QQ) |
|--------------------|-----------------------:|------------:|----------:|
| `src/**/*.tks` (compiler + stdlib) | **161** | 0 | 0 |
| `src/**/*.tkt` (tests)             | **6**   | 0 | 0 |
| `tooling/**`                       | **6**   | 0 | 0 |
| `examples/**` (fixtures)           | **38**  | **26** | **32** |
| **GRAND**                          | **211** | **26** | **32** |

Headline correction to the relayed figure: the "77× `error?`" is really **88** `error?` type-suffix
uses (line-accurate, prose excluded) — the single largest inner type; `error?` = "maybe an error,
`null` = OK". Full `T?` inner-type distribution: `error` 88, generic `T` 18, `i64` 16, `TExpr` 15,
`str` 14, `parser::Function` 7, `Type` 7, `LStructLayout` 6, `JsonValue`/`Counter`/`Box`/`checker::Type` 4
each, then a long 1-3 tail.

### 1.2 The decisive structural fact

**`?.` and `??` are used in exactly SEVEN example fixtures and NOWHERE else — the compiler's own
`src/` has ZERO `?.`/`??` expression uses** (every `src/` hit for those tokens is doc-comment prose or
the parser/checker MACHINERY that implements them). So the `?.`/`??` corpus rewrite touches only
`examples/`; the compiler source rewrite is purely `T?` → `T | null` in type annotations.

The seven `?.`/`??` fixtures (all under `examples/regressions/`):

| Fixture | `?.` | `??` | `T?` | notes |
|---|---:|---:|---:|---|
| `np_oop/`                 | 18 | 17 | 7 | NP-OOP (issue #116): safe method/field, `??` widening, chains |
| `optionals/`              |  2 |  6 | 8 | core optional semantics; `error?` returns |
| `safe_field_access_class/`|  2 |  2 | 2 | class safe-field |
| `lambda_opt_typedef/`     |  2 |  2 | 2 | `?.`/`??` captured in a lambda |
| `selfref_class_optional/` |  2 |  2 | 1 | linked-list walk `cur?.next ?? null` |
| `native_gate_coercions/`  |  0 |  4 | 2 | `[]i64?` element coalesce (see §2 precedence rule) |
| `wasm_panic_hook/`        |  0 |  1 | 1 | `a ?? panic("boom")` (diverging fallback) |

### 1.3 `T?` per-file heat (compiler `src/`, top of list — full list grep-reproducible)

`checker/typer.tks` 37 · `checker/collect.tks` 12 · `codegen/codegen.tks` 12 · `checker/comptime_fold.tks` 11 ·
`lir/lower.tks` 10 · `checker/consteval_form.tks` 7 · `checker/monomorph.tks` 6 · `iter/iter.tks` 6 ·
`checker/resolve.tks` 5 · `checker/check_modules.tks` 5 · `checker/initanalysis.tks` 4 · `checker/di.tks` 4 ·
`checker/type.tks` 3 · `checker/revalidate.tks` 3 · `build/init.tks` 3 · then a 1-2 tail across
`parser/parse_decl`, `checker/match`, `checker/borrow`, `env/env`, `fs/fs`, `io/io`, `io/stream`,
`encoding/json`, `iter/*`, `collections/map`, `math/*`, `build/*`, `emit/*`, `backend/stackify`, etc.

`src/**/*.tkt` `T?` (6): `comptime_fold_test.tkt` 3, `spine_test.tkt` 1, `consteval_test.tkt` 1,
`iter_test.tkt`/`collections/map_test.tkt` (~1). `tooling` `T?` (6): the six editor `host.tks` +
`shared/extract.tks`.

---

## 2. REWRITE RULES (explicit form; every output 0.3.0.29-seed-buildable)

### R1 — `T?` type suffix → `T | null`, **precedence-parenthesized**

`?` binds TIGHTER than `|` and applies to the type PRIMARY. So a `T?` that is NOT the entire type
expression must become `(T | null)`, or grouping changes.

| before | after | why |
|---|---|---|
| `x: error?`                | `x: error \| null`           | whole type — bare form fine |
| `-> Box?`                  | `-> Box \| null`             | whole return |
| `mut e: error? = null`     | `mut e: error \| null = null`| `null` literal widens into the `Variant` |
| `f: []i64?` (= `[](i64?)`) | `f: [](i64 \| null)`         | **`?` bound to the element** — MUST parenthesize inside `[]` |
| `Box<i64?>`                | `Box<i64 \| null>`           | inside generic args — bare form fine |
| `T? \| U` (= `(T?) \| U`)  | `(T \| null) \| U`           | keep the inner group; do not flatten silently |

Seed-safe: `T | null` VALUES build with the pristine 0.3.0.29 seed (fixtures `repr_niche` exit 44,
`repr_box` exit 116). Narrowing at the USE sites is already `match e { error as x => …; null => … }`
(the codebase has 1917 `error as …` arms today) — those arms are byte-identical on `Optional{T}` and
`Variant{[Null,T]}`, so R1 is a pure spelling change at annotation sites and needs NO change at the
narrowing sites.

WATCH: after R1 an `Optional{T}` value must never FLOW into a still-`T?` slot (or vice versa) — see the
batch-hazard rule §3. Within one file this is automatic; across files it drives batch closure.

### R2 — `a ?? b` (coalesce) → explicit `match`

`a` is `T?` (now `T | null`); `b` is the fallback. Lower to a match on the union.

Value fallback (`np_oop:13 (c?.n ?? 0)` after `?.` is also rewritten — see R3):
```
// before:  a ?? b
// after (a already narrowed to a local of type `T | null`):
match a {
    null   => b
    T as v => v
}
```
Widening fallback (`np_oop:42 let picked = noshape ?? circ`, interface/base widen): identical shape —
`T as v => v` in the present arm, `b` in the null arm; `widens_into` still admits `circ` into the
result type exactly as `type_coalesce` did (resolve.tks 802-812 handles the Variant result).

Diverging fallback (`wasm_panic_hook:2 let y = a ?? panic("boom")`):
```
match a {
    null   => panic("boom")
    T as v => v
}
```
`null | error` right-adopts (`x ?? null`): keep the whole union type in both arms.

Seed-safe: `match x { null => …; T as v => … }` is the proven idiom. When `b` is a non-trivial
expression used once, lower inline in the arm (no extra binding); when `a` is itself a compound
expression, bind it first (`let tmp = <a>` of type `T | null`) so the scrutinee is a simple value —
mirrors what `lower_coalesce` did internally.

### R3 — `x?.field` / `x?.method(args)` (safe-nav) → explicit `match`

`x` is `T?` (now `T | null`) of a named struct/class. Null propagates ⇒ the whole expression is
`(field-type) | null` / `(ret) | null`.

```
// before:  x?.field
// after:
match x {
    null   => null
    T as v => v.field
}

// before:  x?.method(args)
// after:
match x {
    null   => null
    T as v => v.method(args)
}
```
Chained `a?.m()?.n` (np_oop:18-20): rewrite inside-out — bind the inner result, then match it:
```
let step = match a { null => null; T as v => v.m() }   // step : (ret) | null
match step { null => null; R as w => w.n }
```
Combined with a trailing `?? d` (the dominant np_oop pattern `(c?.n ?? 0)`): fold the fallback into the
SAME match — no intermediate needed when the receiver is simple:
```
// before:  (c?.n ?? 0)
// after:
match c { null => 0; Counter as v => v.n }
```
`c?.poke()` in statement position with no `??` (np_oop:23-24): the null arm is unit:
```
match c { null => { }; Counter as v => v.poke() }
```
Seed-safe: same `match null / T as v` idiom. Visibility/arity/vtable dispatch behave EXACTLY like the
old desugar because the present arm is an ordinary `.field` / `.method()` on the narrowed value `v`
(this is literally what `safe_nav_match` produced internally — typer.tks 1792-1801).

### R4 — `<T?>` / constraint `?` (already rejected)

No rewrite: `parse_decl` already ERRORS on `?` in a type-param/constraint (parse_decl.tks 120-123,
170-174). Zero corpus uses. The guards themselves are DELETED in Phase B when the token is removed.

---

## 3. ORDERED CRUMB SEQUENCE

**Batch-hazard rule (the ONLY correctness subtlety in Phase A):** because `type_eq`/`widens_into`
bridge NEITHER direction between `Optional{T}` and `Variant{[Null,T]}` (§0), a batch is safe iff it is
**optional-flow-closed**: every `T?` it rewrites whose VALUE is *forwarded un-narrowed* (returned,
passed as an argument, or assigned) into an annotation in ANOTHER file must have that other annotation
rewritten in the SAME batch. Producing a union (`return null` / `return err{…}` / a matched inner) and
consuming it by `match` are BOTH boundary-safe (literals + present values + `match` all bridge). Only
a whole-union *pass-through* can mismatch. Recommended decisive default: rewrite the compiler's own
`src/` corpus **atomically** (one crumb) — the self-hosting error-thread is densely connected, so the
smallest *safe* step for `src/` is the whole component. A leaf-first split is permitted only under the
closure rule; if any gate reports an `Optional`↔`Variant` `type_eq` error, COALESCE the offending
files into the batch and re-gate.

Gen1 keeps FULL sugar support throughout Phase A (deletion is Phase B), so Phase A crumbs accept both
spellings and are independent except that ALL of Phase A must precede ALL of Phase B.

### [RITUAL] gate (every crumb)
```
teko build . --no-verify --release      # 0.3.0.29 seed builds gen1 at -O2 into ./bin
./bin/teko test .                        # gen1 (NOT the seed) runs the unit + regression lanes
```
### [FIXPOINT] (append where PRODUCT code moves — C3, C4, C5, C6, C7)
```
./bin/teko build . -o gen2 --no-verify --release   # gen1 (./bin, seed-built) builds gen2
cmp bin/teko.c gen2/teko.c               # MUST be byte-identical (gen1 == gen2 — no gen3)
```

---

### PHASE A — rewrite the whole corpus off the sugar

**C1 — `?.`/`??`+`T?` in the seven sugar fixtures → explicit forms (R1/R2/R3).**
Files: the seven `examples/regressions/{np_oop,optionals,safe_field_access_class,lambda_opt_typedef,
selfref_class_optional,native_gate_coercions,wasm_panic_hook}/**`. Per-fixture edit; each fixture's
`EXPECT_EXIT` is UNCHANGED (behavior-preserving) — that unchanged exit code is the regression guard.
No product code ⇒ [RITUAL] only (no fixpoint). Watch `native_gate_coercions` `[]i64?` → `[](i64 | null)`
(R1 element parenthesization) and `wasm_panic_hook` diverging fallback (R2). Independently gate-able
(each fixture is its own `.tkb` build unit in the regression lane).

**C2 — remaining `T?`-only fixtures + tooling hosts → `T | null` (R1).**
Files: the `examples/**` fixtures that use `T?` but no `?.`/`??` (e.g. `json_roundtrip`,
`iter_protocol`, `qualified_optional`, `collections_map`, `encodings_roundtrip`,
`unsafe_containment`, `class_slice_element`, `adopt_cyclic_bulk_drop`, `underscore_as_x_rejected`,
`io_file_copy`, `np_oop/src/gadgets.tks`) + `tooling/**` (6 `host.tks`/`extract.tks`). [RITUAL] only.

**C3 — `T?` → `T | null` across `src/**/*.tks` (161 sites, R1). ATOMIC (recommended).**
Files: every compiler/stdlib `.tks` in §1.3. Pure annotation spelling change; NO narrowing-site edits
(the `error as …`/`match` arms already handle both reps). Includes the parenthesization rule R1 for
every slice/union/nested `T?`. This is where PRODUCT code moves ⇒ [RITUAL] + [FIXPOINT].
Flagged sub-risk: NONE are `Ref<T> | null` locals (§5) — the pivot introduces zero C5-only sites.

**C4 — `T?` → `T | null` across `src/**/*.tkt` test fns (6 sites, R1).**
Files: `checker/comptime_fold_test.tkt`, `checker/spine_test.tkt`, `checker/consteval_test.tkt`,
`iter/iter_test.tkt`, `collections/map_test.tkt`. May be folded into C3. [RITUAL] + [FIXPOINT] (tests
compile into the self-test binary). **Note:** the test *values* here are built via `Optional { inner }`
constructors of `checker::Type` (e.g. `cf_var("o", Optional { inner = u64t() })`) — those are internal
former constructions that STAY until Phase B/C7; C4 only rewrites `T?` in ANNOTATION position, not the
`Optional{…}` former constructors (which exercise the machinery that still exists in Phase A).

> End of Phase A invariant: **zero `T?`/`?.`/`??` USES anywhere in the tree.** The sugar MACHINERY
> still exists and is now exercised only by its dedicated `#test` fns (deleted in Phase B).

---

### PHASE B — eradicate the sugar (expression sugar → type sugar → the `Optional` former)

**C5 — delete `?.`/`??` EXPRESSION sugar.** [RITUAL] + [FIXPOINT].
- Lexer: remove the `QQ`/`QDot` composite-munch lines (`src/lexer/lexer.tks` 647-648) and the
  `TokenKind::QQ`/`QDot` enum members (`src/lexer/token.tks` 103-104); drop their `lexer_test.tkt` coverage.
- Parser: delete `parse_coalesce` and re-point the ladder entry (`parse_expr`/`parse_expr_no_struct`
  → `parse_or`, `src/parser/parse_expr.tks` 392-411); delete the `?.` block in `parse_postfix`
  (parse_expr.tks 433-450); delete AST `SafeFieldAccess`, `Coalesce`, `SafeMethodCall`
  (`src/parser/ast.tks` 46-49) and their `ExprKind` variant members (ast.tks 97).
- Checker: delete `type_coalesce`, `type_safe_field_access`, `type_safe_method_call`,
  `safe_nav_receiver`/`safe_nav_match`/`safe_binder_name` (`src/checker/typer.tks` 1779-1883) and the
  three dispatch arms (typer.tks 2599-2601); delete `TSafeFieldAccess`/`TCoalesce` (`src/checker/tast.tks`
  40-41) and their `TExprKind` members (tast.tks 91). Remove every `TSafeFieldAccess`/`TCoalesce`/
  `SafeFieldAccess`/`Coalesce`/`SafeMethodCall` match arm the checker's exhaustiveness now flags:
  `resolve.tks` (2138-2145), `escape.tks` (174-179, 471-472, 670-671), `spine.tks` (1148-1149),
  `monomorph.tks` (635-642), `comptime_fold.tks` (2103-2104, 2225-2244, 3012-3013),
  `consteval.tks` (196-197), `consteval_form.tks` (40-41), `initanalysis.tks` (79-80),
  `revalidate.tks` (103-110), `typer.tks` lam-collect (93-98), `coverage.tks` (122-123).
- Codegen/LIR/emit: delete `emit_safe_field_access`/`emit_coalesce` + dispatch (`src/codegen/codegen.tks`
  3216, 3258, 6141-6142) and the SFA/Coalesce arms in `cg_expr_refs_name`/`cg_expr_calls_self`/
  `cov_collect_expr`/`cg_collect_expr_opts`/`cg_lift_expr` (codegen.tks 683-684, 7138-7139, 7383-7384,
  8235-8236, 9162-9163); delete `lower_coalesce`/`lower_safe_field_access` + dispatch + the
  describe-arms (`src/lir/lower.tks` 611-612, 2804-2870, 4463-4464); delete the tkb tags 14/15
  (`src/emit/tkb_write.tks` 141-144, `src/emit/tkb_read.tks` 331-340, `src/emit/tkb_frame.tks` 171-172)
  and renumber/reserve per the tkb version rule; `build/reachability.tks` 87-88.
- Tests: DELETE the whole `#test` fns (owner ruling — never strip the annotation) that exercise
  `?.`/`??`/SFA/Coalesce/SMC: `parser_test.tkt` (`a?.b`/SMC/chain, 1711-1721),
  `checker_test.tkt` (sfa/co 1019-1024, TCoalesce 1784, sfa+co 1936-1937, np_smc/sfa/co 3314-3434),
  `comptime_fold_test.tkt` (coalesce_use/safe_use 717-725), `spine_test.tkt` (1553-1554),
  `lower_test.tkt` (lwt_coalesce* / lwt_safe_field_access 1737-1795), `tkb_test.tkt` (TCoalesce 134-137),
  `lir_interp_test.tkt` (iwt_coalesce_exit 494), `codegen_test.tkt` (TSafeFieldAccess/TCoalesce 1419-1451).

**C6 — delete `T?` TYPE sugar.** [RITUAL] + [FIXPOINT].
- Lexer: remove `TokenKind::Question` (`src/lexer/token.tks` 102) and its map entry
  (`src/lexer/lexer.tks` 678). A stray `?` now becomes a LEX error (message shifts from parse-level to
  lex-level — acceptable; assert the new message in the negative fixtures, §4).
- Parser: delete the postfix `?` loop in `parse_type_primary` (`src/parser/parse_type.tks` 64-70);
  delete `OptionalType` (`src/parser/type.tks` 8) and its `TypeExpr` variant member (type.tks 11);
  delete the now-dead `?`-rejection guards in `parse_decl` (`src/parser/parse_decl.tks` 120-123, 170-174).
- Checker/codegen/emit: delete the `parser::OptionalType` arms the exhaustiveness flags —
  `resolve.tks` 1608-1623 (the `T?`→Optional producer) + 1895, 2005, 2353; `monomorph.tks` 225, 266;
  `check_modules.tks` 138; `spine.tks` 197; `codegen.tks` 885, 937, 1759, 1825, 1917, 1929, 3105,
  3863-3922, 4845-4856, 8338, 8634; `emit/tkb_write.tks` 299, `emit/tkb_read.tks` 570,
  `emit/tkb_frame.tks` 198 (typeexpr tag 3 — renumber/reserve).
- Tests: DELETE the `#test` fns exercising `OptionalType`: `parser_test.tkt` (98), `codegen_test.tkt`
  (1196-1221), `spine_test.tkt` (568), `checker_test.tkt` (the `OptionalType`/`Reference` niche test
  1371-1379). After C6 the `?` character is fully un-lexable and `parser::OptionalType` no longer exists;
  the `checker::Optional` FORMER is now dead (no producer) but still present — removed in C7.

**C7 — delete the `Optional` FORMER (`checker::Type` variant).** [RITUAL] + [FIXPOINT].
Now that no `OptionalType`/`TSafeFieldAccess`/`TCoalesce` produce it, `Optional` has no producer.
- Delete `type Optional = struct { inner: Type }` and the `Optional` member of the `Type` variant
  (`src/checker/type.tks` 71, 102). The `Type`-exhaustiveness now flags EVERY `Optional as …`/`Optional =>`
  arm to delete: `type.tks` `type_eq` (142-147); `resolve.tks` `widens_into` present-wrap (755-759) +
  `variant_member_admissible` Optional-rejection (1523) + the `Void?`/`T??`-collapse remnants; `match.tks`
  `check_pattern` (163-168) + `present_case_covered` (497); `monomorph.tks` `type_to_texpr` (225);
  `escape`/`spine`/`comptime_fold`/`codegen` Optional-layout+mangle arms (`opt_`/`tk_opt_`,
  `cg_te_reaches_byvalue`, etc.). Delete the `Optional`-former `#test` fns that survive C5/C6
  (e.g. `comptime_fold_test.tkt` cf_var Optional constructions, `checker_test.tkt` Optional-Reference).
- Confirm `T | null` (the `Variant`+`Null` path) is UNTOUCHED: none of the deleted arms sit on the
  `Variant`/`Null` `Type` members; `union_normalize_null`, `variant_member_admissible`'s `Reference`+
  `Null` relaxation (resolve.tks 1524-1536), and the C3/C4/C5 niche/box/ref codegen all key on
  `Variant`/`Null`, never `Optional`. The repr fixtures (`repr_niche` 44, `repr_box` 116, `ref_null_admit`,
  `ref_null_local`) must stay green through C7 — they ARE the guard that the former's deletion did not
  disturb `T | null`.

> Merge note: C6+C7 may be combined only if the implementer is comfortable resolving both
> exhaustiveness cascades in one gate; keeping them separate keeps each step bisectable.

---

## 4. REGRESSION FIXTURES to add (inputs → expected exit, VM + native)

Behavior-preservation (Phase A) is guarded by the SEVEN rewritten fixtures keeping their existing
`EXPECT_EXIT` unchanged (np_oop, optionals, safe_field_access_class, lambda_opt_typedef,
selfref_class_optional, native_gate_coercions, wasm_panic_hook). No new positive fixture is needed for
the rewrite itself — the unchanged exit codes across the rewrite ARE the assertion, run in both the
VM/self-test lane and the native `./bin/teko test .` lane.

Add, after Phase B, to lock in ERADICATION — negative/expect-fail cases (each a minimal `.tkb`
project whose BUILD must fail). Placement: the main gate's `[tests] regression = ["examples/regressions"]`
(teko.tkp) is a POSITIVE-only lane, and `examples/regression-fixture/regressions-fail/` (e.g. `exit_bad`)
is the regression-HARNESS's own meta-fixture, not the compiler's negative surface. So wire these into
whatever expect-fail mechanism the P1/P2/P3 regressive surface exposes (per
`docs/design/regressives-full-stack-0.3.0.30.md`); if none yet accepts a compile-fail assertion,
report that gap up to the integrator rather than inventing a lane. Each fixture:

| new fixture | source (one-liner) | expected | asserts |
|---|---|---|---|
| `sugar_qmark_type_rejected`   | `fn f() -> i64? { 1 }`                | compile FAILS (lex error on `?`) | `T?` eradicated (C6) |
| `sugar_qmark_field_rejected`  | `let y = x?.n`                        | compile FAILS (lex/parse on `?`) | `?.` eradicated (C5) |
| `sugar_coalesce_rejected`     | `let y = a ?? 0`                      | compile FAILS (lex/parse on `?`) | `??` eradicated (C5) |
| `sugar_slice_opt_rejected`    | `fn f(xs: []i64?) -> i64 { 0 }`      | compile FAILS               | element `?` eradicated (C6) |

Each carries an `EXPECT_EXIT` (non-zero compile failure) and, where the fail-lane supports it, an
expected-message substring (`unexpected` / the lexer's stray-`?` message). These run under the same
`./bin/teko test .` gate. Keep them tiny so they never depend on any other language surface.

Also KEEP (do not delete) the positive repr fixtures `repr_niche` (44), `repr_box` (116),
`ref_null_admit`, `ref_null_local` — they guard that `T | null` survives the former's deletion (C7).

---

## 5. RISKS + LAW TENSIONS + HALT flags

**Seed-safety (verified — NO intermediate seed needed for the pivot).** The one construct the
0.3.0.29 seed cannot build is `Ref<T> | null` LOCAL narrowing (C5, `ref_null_local`). The corpus
pivot introduces ZERO such sites: `Ref<T>?` is and always was ILLEGAL (`resolve_type` rejects a
nullable reference, resolve.tks 1616-1619), so no existing `T?` is a ref-optional, so R1 never yields
a `Ref<T> | null` local. Confirmed by grep: no `Ref<…>?`/`Reference?` anywhere in `src/`. **The entire
pivot (C1-C7) stays 0.3.0.29-seed-safe.** No HALT.

**RISK-1 (batch boundary, MITIGATED by design).** `type_eq`/`widens_into` do not bridge `Optional{T}`↔
`Variant{[Null,T]}` (§0). Mitigation: the flow-closure rule §3 + the decisive atomic-`src` default
(C3). Fallback: a gate `type_eq` error names the mismatching file — coalesce and re-gate. Not a law
tension (both reps are already law-blessed; the pivot only removes the legacy one).

**RISK-2 (tkb tag renumbering).** Deleting typeexpr tag 3 (Optional) and expr tags 14/15
(SFA/Coalesce) changes the `.tkb` wire format. This is the compiler's own artifact format; the
fixpoint (gen1==gen2) is the guard — any read/write asymmetry breaks byte-identity at C5/C6. Reserve
(don't reuse) the freed tags per the existing tkb version discipline, or bump the tkb format version
if the header carries one. Report to the integrator; no user-facing compatibility law applies
(bootstrap-internal format).

**RISK-3 (test-value former constructors).** Several `.tkt` build `Optional { inner = … }` /
`TSafeFieldAccess {…}` / `TCoalesce {…}` *values* directly (not via source sugar). These exercise the
machinery and MUST be removed as whole `#test` fns in C5/C6/C7 (owner ruling), not annotation-stripped.
Enumerated in §3 C5/C6/C7. Missing one → a Phase-B gate fails to compile the self-test binary (caught
immediately).

**RISK-4 (`Optional` present-wrap semantics loss).** `widens_into`'s `to: Optional` arm (resolve.tks
755-759) present-wraps a whole `T` into `T?`. The `Variant` path present-wraps via the member-widen
arm (802-812) — VERIFY at C3 that every rewritten site that relied on implicit `T → T?` wrapping now
relies on `T → (T | null)` member-widen (it does: `repr_niche`'s `let present: Node | null =
Node::make(7)` is exactly this, and builds with the seed). No code change needed; this is a
review-checkpoint, not a defect.

**No genuine law tension remains** — the directive is decisive, the Constitution's "one obvious way"
(M.5) FAVORS eradicating the redundant sugar, and seed-sequencing is satisfied. Nothing to HALT on.

Adjacent finding (REPORTED up, not actioned): `type_coalesce`/safe-nav carried NP-OOP interface/base
widening and diverging-fallback handling (typer.tks 1866-1882) that the explicit `match` rewrite
reproduces via ordinary arm typing — no behavior is lost, but the np_oop fixture's widening cases
(`noshape ?? circ`, `s ?? Circle::make(9)`) are the ones to eyeball first when running C1's gate.

---

## 6. Appendix — reproduce the inventory

State-machine counter (strips `/* */`, `//`, string contents; preserves line numbers; excludes
`.claude/worktrees`, `gen1out`, `bin`, `init_test_scratch`, `.teko`). Counts `??`, then `?.` in the
remainder, then a `?` preceded by `[\w>\]\)]` in the remainder (= a `T?` suffix). Script lived at
`scratchpad/final.py` during design; totals in §1.1 are its output. Spot-check commands:
`rg -n 'QDot|QQ|Question' src/parser src/lexer` (parse/lex sites);
`rg -n 'OptionalType|Coalesce|SafeFieldAccess|SafeMethodCall' src` (machinery surface).
