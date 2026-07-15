# Module-level `const` — design + crumb plan (#594)

Status: PROPOSED (architect plan; owner ruling 2026-07-15). Lane: `remodel/backend-build`.
Author: architect. Implementer executes the crumb sequence in order.

> GitHub access was disabled for this session, so the issue #594 body could not be
> read directly. This plan is written against the **owner brief as relayed by the
> coordinator** (the definitive scope quoted below) plus a full read of the current
> source. If the issue body carries any constraint not reflected here, treat the
> issue as authoritative and reconcile.

---

## 0. Scope (owner, definitive)

1. `const NAME: Type = <const-expr>` at **module** level must exist as a real
   language feature (parse → check → usable-as-value → cross-module `pub const`).
   "Defined since the start, neglected by agents; do it now."
2. Migrate **ALL** ~50 `fn X() -> T { <const> }` nullary constant-returning
   functions to named constants. Scope is "Tudo, não apenas o máximo": the
   const-eval frontier must reach every site; the architect classifies each.
3. **No magic values** (new W15 convention, owner 2026-07-15): the retrofit is not
   limited to anemic constant-returning functions — inline magic literals *in the
   middle of code* (conditions, indices, offsets, masks, sizes, file magic, section
   flags) must also become named `const` / `enum` / `flags`.
4. Some families become `enum` or `flags`, not scalar `const`.
5. Behavior-preserving, proven by **fixpoint gen2==gen3** + both-engine (VM +
   native) tests + 100% coverage of the delta.

---

## 1. The real rationale: arenas, not just CALL/RET (owner 2026-07-15)

The dominant cost of a zero-arg `fn X() -> T { <const> }` is **not** the CALL/RET
pair. It is that **every call opens an arena (a lexical region)** — Teko's memory
model is arena-per-scope (ref-transparent model, R11: "sem GC; arenas lexicais").
A nullary function that returns a constant still enters, allocates/owns a region
for its body scope, materializes its result into the *caller's* arena on return
(R6 copy-on-return), and tears the region down. For a value read thousands of times
in a hot encoder loop, that is thousands of region open/close cycles for a value
that never changes.

A **`const` is compile-time**: it has no scope, opens no arena, allocates nothing
at run time. This is the central justification for the whole feature and for the
lowering decision below:

- **Scalar const → INLINE the folded literal at the use site.** Zero arena, zero
  global, zero relocation, zero backend change. The value is a `mov imm` (or an
  immediate operand) exactly where it is used.
- **Immutable aggregate const → materialize once in rodata** (the read-only data
  path that string literals already use end-to-end) and reference it. One static
  image, no per-use arena, no per-use construction.

Both eliminate the per-call arena. That is the win the owner is buying.

---

## 2. Grounding — what exists today (read before implementing)

| Concern | Location | State |
|---|---|---|
| Top-level decl parse (`fn`/`type`/`flags` only) | `src/parser/parse_decl.tks:878` (`parse_decl`), `:970` (`parse_module`) | must gain a `const` arm |
| `Decl` / `Item` / `ItemKind` AST | `src/parser/ast.tks:351,361,362` | `Decl = variant Function \| TypeDecl`; `ItemKind = variant UseDecl \| Statement \| Function \| TypeDecl` — both widen |
| Local `const` (`BindKind::Const`) | `src/parser/parse_stmt.tks:166` | exists as a LOCAL binding only; module-level does not exist |
| Pass-1 name collection | `src/checker/collect.tks:9` (`collect_types`), env seeding | must collect const names + types |
| Pass-2 typing per item | `src/checker/typer.tks:4394` (`type_item`) | must type a const item + validate const-expr |
| Escape/borrow already knows const | `src/checker/escape.tks:341,587` | `BindKind::Const` treated as non-escaping — reuse |
| Typed AST item | `src/checker/tast.tks:148` (`TItem`), `:149` (`TProgram`) | `TItem` widens with `TConstDecl` |
| Lowering per item | `src/lir/lower.tks:4529` (`lower_program`), `:4828` (`lower_item`) | must handle a const item (mostly a no-op — see §5) |
| LIR rodata (WORKS) | `src/lir/lir.tks:143` (`LRodata`), `:262` (`LGlobalAddr`), `lower.tks:3759` (`intern_rodata`) | string literals already intern rodata + reference it |
| LIR global (STUB) | `src/lir/lir.tks:148` (`LGlobal`) | "top-level consts are a later construct" — we DO NOT use this |
| Backend honest-stops | `encode_x86_64.tks:1489/1495`, `encode_arm64.tks:1858`, `encode_riscv.tks:1684`, `stackify.tks:4458` | ALL gate on `m.globals.len > 0`, **never** on `m.rodata` |
| rodata emission (WORKS) | `encode_*.tks encode_rodata`, `objfile_{elf,macho,coff,wasm}.tks`, `lir_interp.tks:206/527` | `LRodata`→bytes+symbol+reloc already emitted on every backend AND interpreted in the VM |

### 2.1 The critical insight (validated)

The honest-stops that block "top-level data" fire only on **`m.globals`** (the
`LGlobal` table), never on **`m.rodata`**. String literals already flow read-only
data + a `Pc32`/`Abs64` relocation through **every** backend (C, x86_64, arm64,
riscv64, wasm) and both object writers (ELF/Mach-O/COFF/wasm) **and** the LIR
interpreter (`rodata_base_of` / `interp_global_addr`). Therefore:

- **Scalars never touch data at all** (inlined literals).
- **Aggregates reuse the rodata path** — no `LGlobal`, so no honest-stop is
  tripped, and **no backend file is modified**.

This is the load-bearing simplification: #594 is a **front-end + lowering** change.
The backends and the frozen C twins are untouched.

---

## 3. Ratified design decisions (law-first)

The owner listed five design-open questions. Ratifications below; a genuine tension
would HALT — none does.

### D1 · const-expr grammar (the frontier)

A const initializer is a restricted expression grammar, tiered by what the site
needs. "const means const": we REJECT anything not on this grammar rather than
silently inlining an arbitrary call (least-surprise, law-first).

- **Tier 0 — literal:** int / float / bool / byte / char / string literal.
  (`i8_max { 127 }`, `pi { 3.14159… }`, `zip_method_store { 0 }`.)
- **Tier 1 — cast:** `<const-expr> to T` (`u8_min { 0 to u8 }`,
  `gzip_cm_deflate { 8 to byte }`).
- **Tier 2 — unary / binary / bitwise over const-exprs:** `~`, unary `-`, `+ - * /
  % & | ^ << >>` where both operands are const-exprs (`u8_max { ~(0 to u8) }`,
  `mask_all_u64 { ~(0 to u64) }`, `min_i64_i128 { -(max_i64_i128) - (1 to i128) }`).
- **Tier 3 — reference to another const / enum member / flags member:** a bare
  name (or `ns::name`) resolving to a module-level const, an `enum` member, or a
  `flags` member (`min_i64_i128` references `max_i64_i128`).
- **Tier 4 — aggregate literal of const-exprs:** array/slice literal `[a, b, …]`
  and struct/variant literal `T { f = <const-expr>; … }` / `V { }` whose every
  field is a const-expr (`gzip_magic { [0x1F to byte, 0x8B to byte] }`,
  `ret_inst { MRet {} }`).
- **Tier 5 — call to an allowlisted pure constructor:** a call whose callee is on a
  small, explicit allowlist of side-effect-free constructors, with const-expr args.
  The closed allowlist for the current corpus is exactly:
  - `teko::f64_from_bits` / `teko::f32_from_bits` (builtins; `inf`, `nan`),
  - `preg` (`src/backend/minst.tks:886` — body is a single `MReg { … }` literal;
    `rax_x86`, `rcx_x86`, `riscv_zero_reg`, …).

  Rationale for an explicit allowlist over a general purity analyzer: the set is
  tiny and closed, deterministic, and reversible; a transitive-purity pass is a
  much larger, higher-risk feature out of scope for #594. Documented in
  DECISION_LOG as reversible. A future `const fn` marker can supersede it.

Everything above is validated by a single predicate `is_const_expr(TExpr) -> error?`
in the new `consteval` module (§3.5). Anything else in a const initializer is a
type error with a located message.

### D2 · scalar INLINE vs `LGlobal` → **INLINE (owner-aligned)**

A **scalar** const (any primitive-typed const: the int/float/bool/byte/char
families) is **inlined at every use site** as the equivalent typed literal. No
`LGlobal`, no rodata, no reloc, no arena — a `mov imm`. This is the owner's
explicit lean and eliminates all backend risk.

An **immutable aggregate** const (`[]byte`, struct, variant) is materialized. Two
sub-options were considered:

- **(a) rodata materialization** (owner's stated "agregados imutáveis → rodata"):
  a single static image + `LGlobalAddr`, reusing the string-literal path. One copy,
  no per-use arena/construction — the true optimization.
- **(b) inline the aggregate literal** at each use site (re-construct in place,
  exactly what the old nullary fn did per call).

**Ratification:** ship **(b) inline** as the #594 baseline for aggregates, and
schedule **(a) rodata** as a fast-follow optimization crumb (§6, C-agg-2) *gated by
the same fixpoint proof*. Reason (law-first, smallest-safe-step): (b) is strictly
behavior-preserving (an aggregate const inlined == the old fn's per-call
construction, byte-for-byte, so gen2==gen3 is trivially provable) and touches **zero
backend files**. (a) is the better end-state (kills the per-use construction arena
too) but requires a rodata layout serializer for non-`[]byte` structs and a
`LGlobalAddr`-typed-value load path; doing it under the same PR risks the fixpoint.
The owner's "→ rodata" intent is honored as the very next crumb, not dropped.

> Net: **all** consts eliminate the *definition-side* nullary-fn arena immediately.
> Aggregates additionally lose their *use-side* construction arena when C-agg-2
> lands. Scalars have no use-side cost at all from crumb one.

### D3 · evaluation order + cycle detection

Consts form a dependency DAG via Tier-3 references. Resolution:

- Build a dependency edge `B → A` for every const `A` referenced in `B`'s
  initializer (walk the typed initializer collecting `TVar`/`TCall` that resolve to
  a module-level const).
- **Topologically order** consts before inlining/lowering. A back-edge is a
  **cycle** → a located `error` ("constant cycle: A → B → A") from a visiting-set
  DFS. No fixpoint iteration, no numeric evaluation needed (inlining is AST
  substitution; the backend computes `~(0 to u64)` etc. exactly as the old fn did).
- Cross-module: a `pub const` referenced from another module is resolved through
  the already-typed dep `TProgram` (`collect.tks:1719 seed_from_dep`); its
  initializer is available (serialized in the `.tkb`, §3.6), so the same
  substitution applies.

### D4 · `pub const` + cross-module visibility

`const` follows the exact visibility rules of `fn`: default = module-private,
`pub` = exported. Parse reuses the existing `pub`/`exp` peel in `parse_decl`
(`:884`). A `pub const`'s **typed initializer is serialized in the `.tkb`** so a
consumer inlines it identically (no cross-module data symbol, no linkage — the
consumer re-materializes the value in its own translation unit, preserving the
inline/no-arena property). A non-`pub` const referenced cross-module is the usual
visibility error.

### D5 · where const-eval lives + serialization

- **New module `src/checker/consteval.tks`** (namespace `teko::checker`). It owns:
  `is_const_expr` (grammar validator), the dependency-graph builder, cycle
  detection, and the topological ordering. It does NOT do numeric folding (not
  needed; see D3). Kept out of `typer.tks` to bound cyclomatic complexity and to
  give the const feature one obvious home.
- **`.tkb` codec (C7.16):** `TConstDecl` (§4) is a new `TItem` case. Its fields are
  `name: str`, `namespace: str`, `ty: Type`, `init: TExpr`, `vis`, doc, line/col —
  every component (`Type`, `TExpr`) already has a codec, so the new case is a
  struct-of-existing-codecs. Add encode/decode arms mirroring the `TFunction` case.

---

## 4. Type / function shapes the implementer adds (full-Javadoc, copy verbatim)

### 4.1 Parser AST (`src/parser/ast.tks`)

```teko
/**
 * ConstDecl — a module-level named compile-time constant `const NAME: Type = init`
 * (#594). Distinct from the local `BindKind::Const` binding (`parse_stmt.tks`): a
 * ConstDecl is a top-level Decl with visibility, a mandatory type annotation, and a
 * const-expr initializer (validated by `teko::checker::is_const_expr`). It carries
 * no scope and opens no arena — the checker inlines it (scalars) or the backend
 * materializes it in rodata (aggregates, C-agg-2).
 *
 * @param name    the constant's identifier
 * @param ty      the mandatory declared type annotation
 * @param init    the const-expr initializer
 * @param vis     Pub/Exp/private, mirroring a function's
 * @param has_doc whether a doc-comment was attached
 * @param doc     the attached doc-comment text (valid iff has_doc)
 * @param line    the `const` keyword's source line
 * @param col     the `const` keyword's source column
 * @since #594
 */
pub type ConstDecl = struct { name: str; ty: TypeExpr; init: Expr; vis: Visibility; has_doc: bool; doc: str; line: u32; col: u32 }
```

`Decl` widens: `variant Function | TypeDecl | ConstDecl`.
`ItemKind` widens: `variant UseDecl | Statement | Function | TypeDecl | ConstDecl`.

### 4.2 Parser (`src/parser/parse_decl.tks`)

```teko
/**
 * parse_const_decl — parse a module-level `const NAME: Type = <const-expr>` (#594),
 * reached from `parse_decl` after the optional doc/`pub`/`exp` peel. The type
 * annotation is MANDATORY at module scope (unlike a local `const`, whose type may
 * infer): a public constant's type is part of its contract. The initializer is
 * parsed as a full expression here and validated as a const-expr later, in the
 * checker (`is_const_expr`), so parse errors stay syntactic.
 *
 * @param tokens  the full token stream
 * @param start   the index of the `const` keyword
 * @param vis     the visibility peeled by `parse_decl`
 * @return        the parsed ConstDecl and the index just past its initializer
 * @throws        a located error on a missing name, a missing `: Type`, a missing
 *                `=`, or an ill-formed initializer expression
 * @since #594
 */
fn parse_const_decl(tokens: []lexer::Token, start: u64, vis: Visibility) -> Parsed<Decl> | error
```

Wire into `parse_decl` (`:895`-style arm, after the `fn`/`type`/`flags` checks):
`if is_kind_at(tokens, k, lexer::TokenKind::Const) { … return parse_const_decl(…) }`.
`parse_module` needs no change (it already loops `parse_decl`).

### 4.3 Checker collect (`src/checker/collect.tks`)

```teko
/**
 * collect_const_sig — resolve a module-level ConstDecl's declared type and bind its
 * name into the pass-1 environment so a forward reference (another const, or a fn
 * body) resolves before pass-2 types the initializer (#594). Mirrors `func_type`'s
 * role for functions: signature-only, no initializer evaluation here.
 *
 * @param cd      the parsed const declaration
 * @param table   the program's type table (to resolve `cd.ty`)
 * @param ref_ns  the namespace the const is declared in
 * @return        the const's resolved value type, or a located error
 * @since #594
 */
fn collect_const_sig(cd: parser::ConstDecl, table: TypeTable, ref_ns: str) -> Type | error
```

Bind `cd.name` → resolved type into `Env` (a new `EnvBinding` kind, or reuse the
existing value-binding path with an `is_const` marker — reuse is cheaper).

### 4.4 New module (`src/checker/consteval.tks`, namespace `teko::checker`)

```teko
/**
 * is_const_expr — the D1 grammar gate: return null iff `e` is a legal const
 * initializer (Tiers 0–5), else a located error naming the offending form (#594).
 * Recurses structurally; a call is legal only when its callee is on the closed
 * pure-constructor allowlist (`is_const_allowlisted_callee`).
 *
 * @param e      the typed initializer expression
 * @param table  the type table (to classify a callee / a referenced name)
 * @param env    the environment (to resolve a referenced name to a const/enum/flags)
 * @return       null when const, else a located error
 * @since #594
 */
fn is_const_expr(e: TExpr, table: TypeTable, env: Env) -> error?

/**
 * const_dep_order — topologically order the program's module-level consts by their
 * Tier-3 references so an inlining pass never substitutes an unresolved const, and
 * a reference cycle is reported as a located error (#594). DFS with a visiting-set;
 * a re-entered node on the current path is the cycle.
 *
 * @param consts  every module-level TConstDecl in the program
 * @return        the consts in dependency order (deps first), or a cycle error
 * @since #594
 */
fn const_dep_order(consts: []TConstDecl) -> []TConstDecl | error

/**
 * is_const_allowlisted_callee — true iff a call to `path` is permitted in a const
 * initializer (Tier 5): the closed set `teko::f64_from_bits`, `teko::f32_from_bits`,
 * and the `preg` register constructor. Deliberately explicit, not a purity
 * analysis (#594, DECISION_LOG — reversible; a future `const fn` supersedes it).
 *
 * @param path  the resolved callee path
 * @return      whether the callee may appear in a const initializer
 * @since #594
 */
fn is_const_allowlisted_callee(path: parser::Path) -> bool
```

### 4.5 Typed AST (`src/checker/tast.tks`)

```teko
/**
 * TConstDecl — a typed module-level constant (#594): the resolved value type and
 * the typed, const-validated initializer. Lowering inlines a scalar TConstDecl at
 * each use site and drops the declaration; an aggregate is inlined (baseline) or
 * interned to rodata (C-agg-2). Serialized in the `.tkb` (C7.16) so a `pub const`
 * inlines identically across modules.
 *
 * @param name       the constant's identifier
 * @param namespace  its declaring namespace (for cross-module resolution)
 * @param ty         the resolved value type
 * @param init       the typed, const-validated initializer
 * @param vis        its visibility
 * @param has_doc    whether a doc-comment is attached
 * @param doc        the doc-comment text (valid iff has_doc)
 * @param line       source line
 * @param col        source column
 * @since #594
 */
pub type TConstDecl = struct { name: str; namespace: str; ty: Type; init: TExpr; vis: parser::Visibility; has_doc: bool; doc: str; line: u32; col: u32 }
```

`TItem` widens: `variant TFunction | parser::TypeDecl | parser::UseDecl | TStatement | TConstDecl`.

### 4.6 Typer (`src/checker/typer.tks`)

`type_item` (`:4394`) gains an arm:

```teko
        parser::ConstDecl as cd => {
            let ti = match type_const_decl(cd, env, table) { TConstDecl as x => x; error as e => return e }
            TConstDecl { name = ti.name; namespace = item.namespace; ty = ti.ty; init = ti.init; vis = ti.vis; has_doc = ti.has_doc; doc = ti.doc; line = cd.line; col = cd.col }
        }
```

```teko
/**
 * type_const_decl — pass-2 typing for a module-level constant (#594): type the
 * initializer, check it against (and coerce to) the declared `cd.ty`, then gate it
 * through `is_const_expr`. Produces a TConstDecl whose `init` is the checked
 * expression the inliner substitutes at each use site.
 *
 * @param cd     the parsed const declaration
 * @param env    the pass-2 environment (top-level names bound)
 * @param table  the program's type table
 * @return       the typed constant, or a located error (type mismatch or non-const)
 * @since #594
 */
fn type_const_decl(cd: parser::ConstDecl, env: Env, table: TypeTable) -> TConstDecl | error
```

### 4.7 Inliner (const-use substitution)

A use of a const is a `TVar { name; is_func = false }` (it was `X()` — a `TCall` —
before migration; after migration each call site becomes a bare `X`). Substitution
runs as a typed-AST rewrite AFTER `const_dep_order`, replacing every `TVar`
resolving to a **scalar** const with a clone of its (already inlined) initializer,
and — at baseline — every `TVar` resolving to an **aggregate** const likewise.

```teko
/**
 * inline_consts — rewrite the typed program, replacing every reference to a
 * module-level constant with a clone of that constant's checked initializer, in
 * dependency order so nested const references collapse fully (#594). Scalars always
 * inline; aggregates inline at baseline (C-agg-2 later routes aggregates to rodata
 * instead). The TConstDecl items are then dropped from the program (they emit no
 * code — no fn body, no arena).
 *
 * @param prog  the fully typed, monomorphized program
 * @return      the program with const references substituted and const decls removed
 * @throws      a located error if a const reference cannot be resolved
 * @since #594
 */
pub fn inline_consts(prog: TProgram) -> TProgram | error
```

> Placement: runs in the checker pipeline after `monomorphize`
> (`monomorph.tks:1147`) and before lowering, so lowering sees no const references
> and `lower_item` needs only to *skip* a residual `TConstDecl` (defensive no-op).

### 4.8 Lowering (`src/lir/lower.tks`)

`lower_item` (`:4828`) gains a defensive no-op arm for `TConstDecl` (post-inlining
there should be none, but a `pub const` kept for the `.tkb` may survive): produce
no `LFunc`, no `LGlobal`. For C-agg-2, this arm is where an aggregate const interns
rodata via the existing `intern_rodata` (`:3759`) and records a symbol the inliner
rewrites uses to (`LGlobalAddr`).

### 4.9 `.tkb` codec (C7.16)

Add `TConstDecl` encode/decode arms alongside the `TFunction` case (a
struct-of-existing-codecs: `str`, `Type`, `TExpr`, `Visibility`, `bool`, `u32`).

---

## 5. Backend impact — NONE (the whole point)

| Backend / writer | honest-stop | changes for #594 baseline | changes for C-agg-2 |
|---|---|---|---|
| C twin (frozen) | — | none | none |
| x86_64 (`encode_x86_64.tks`) | `honest_globals_x86` (`:1495`, gates `m.globals`) | none (scalars inline; aggregates inline) | none (uses `m.rodata`, already emitted) |
| arm64 (`encode_arm64.tks`) | `honest_globals`/`A4-globals` (`:1858`) | none | none |
| riscv64 (`encode_riscv.tks`) | `honest_globals_riscv` (`:1684`) | none | none |
| wasm (`stackify.tks`) | `wasm_honest_globals`/`C1-globals` (`:4458`) | none | none |
| ELF/Mach-O/COFF/wasm writers | — | none | none |
| LIR interp (VM) | — | none | none (rodata read path exists: `lir_interp.tks:206/527`) |

No honest-stop is touched, because we never populate `m.globals`. `m.rodata`
(already flowing through every backend + the VM) is the only data path used, and
only in the C-agg-2 follow-up.

---

## 6. Classified inventory (const / enum / flags / rodata / stays-fn)

Grep the exact list before starting (`rg -n "fn [a-z_0-9]+\(\)\s*->" src`); the
classification below is by **family**, per the owner's enum/flags directive. "≈50
consts" becomes **N consts + M enums + K flags**.

### 6.1 → scalar `const` (INLINE) — the bulk

- `src/math/checked.tks`: `u8_min/u8_max/u16_min/u16_max/u32_min/u32_max/u64_min/
  u64_max` (Tier 1–2: `0 to uN`, `~(0 to uN)`), `i8_max/i16_max/i32_max/i64_max`
  (Tier 0). **Consider a shared pattern**, but keep as individual `pub const`
  (their names are the API). ~12 consts.
- `src/math/math.tks`: `pi/e/sqrt2/ln2/ln10` (Tier 0 f64), `exponent_mask/
  mantissa_mask/sign_mask` (Tier 0 u64 hex). ~8 consts.
- `src/lir/lir_interp.tks:359` `block_step_budget` (Tier 0). `src/io/stream.tks:262`
  `default_chunk_size` (Tier 0). ~2 consts.
- `src/compress/*`: `zlib_cmf`, `adler32_modulus`, `zip_fixed_dos_date`,
  `max_huffman_bits`, `max_stored_block_len`, `gzip_cm_deflate` (Tier 0/1). Some of
  these belong to enum/flags families below — see 6.2/6.3.
- `src/backend/isel_arm64.tks:285/297/335…` `max_i64_i128` (Tier 1),
  `min_i64_i128` (Tier 2+3, references `max_i64_i128`), `mask_all_u64` (Tier 2).
  These reference each other → exercises `const_dep_order`. ~a handful consts.

### 6.2 → `enum` (a related integer family, owner directive #3)

- `src/compress/inflate.tks:655/662/669` `block_type_stored/fixed/dynamic (0/1/2)`
  → `pub type BlockType = enum { Stored; Fixed; Dynamic }`. Update the `u32`
  comparisons at the deflate/inflate use sites to the enum. **This is the model
  case** — a closed set of tags used in a `match`/comparison.
- `src/backend/stackify.tks:195/205/216` `wasm_scope_kind_block/loop/if` →
  `enum WasmScopeKind`.
- `src/backend/stackify.tks:562/574/587` `elide_kind_normal/drop/tee` →
  `enum ElideKind`.
- `src/backend/stackify.tks:3975/3986/3997` `edge_result_stop/branch/more` →
  `enum EdgeResult`.
- `src/backend/stackify.tks:10/19/28/37` `wasm_vt_i32/i64/f32/f64` → `enum WasmVType`
  (the wasm value-type tag byte; keep a `to u8` at the single emit site).
- `src/compress/compress.tks:177/184` `zip_method_store/deflate (0/8)` →
  `enum ZipMethod`.

> Migration note for enums: the tag's **numeric wire value** must be preserved
> where it is serialized to bytes (wasm value-type byte 0x7F.., ZIP method u16).
> Where Teko enums do not (yet) pin discriminant values, keep a small
> `fn <enum>_wire(v) -> u8/u32` at the **single** emit site and drive it by `match`.
> This keeps the wire bytes byte-identical (fixpoint-safe) while the *logic* uses
> the enum. Do NOT change the emitted bytes.

### 6.3 → `flags` (a bitmask family, owner directive #3)

- ELF `sh_flags` / `st_info` (`src/backend/objfile_elf.tks:146/171/195/220` uses
  `0x00/0x12/0x10/0x03`): ST_INFO = `(bind<<4)|type` and SHF_* section flags →
  `flags ElfSymInfo` / `flags ElfSectionFlags` (`SHF_ALLOC | SHF_EXECINSTR`, …).
- Mach-O section attributes (`objfile_macho.tks:426` `0x80000400`, ntype
  `0x0E/0x0F`) → `flags MachoSectionAttr` / an `enum MachoSymType`.
- **File magic** (`0xFEEDFACF`, ELF `\x7fELF`, wasm `\0asm`, gzip `0x1F8B`) →
  named `const` (a magic number is a single named value, not a bitmask).

> As with enums: the emitted bytes MUST stay identical. `flags` migration changes
> the *source spelling* (`SHF_ALLOC | SHF_EXECINSTR` instead of `0x6`), not the
> lowered bytes. Prove with fixpoint + the object-writer golden tests.

### 6.4 → rodata aggregate (C-agg-2)

- `src/compress/gzip.tks:16` `gzip_magic -> []byte { [0x1F, 0x8B] }` → aggregate
  const; baseline inlines the 2-byte array literal, C-agg-2 interns it in rodata.
- `src/backend/minst.tks:1257` `ret_inst -> MInst { MRet {} }`,
  `src/backend/isel_x86_64.tks:155` `rax_x86 -> MReg { preg(0, GPR) }` (Tier 5) →
  aggregate/struct consts; baseline inlines the literal/constructor.

### 6.5 → STAYS `fn` (justified exceptions)

- Any nullary fn whose body **allocates a fresh mutable/heap value per call** and
  whose call sites RELY on getting a distinct instance — e.g. the `*_empty()`
  seed factories (`empty_module`, `env_empty`, `di_registry_empty`,
  `memstore_new`, `mrf_new`, `borrow_summary_env_empty`, …) that return
  `teko::list::empty()`-backed structures. These are **not constants**: they are
  identity/seed factories; converting them to a shared const would alias mutable
  state across call sites and is NOT behavior-preserving. **Justification:** the
  owner's "convert TUDO" targets *constant* returns; a factory that seeds fresh
  mutable state is a different category and correctly stays a fn.
- `nan()`/`inf()` are Tier-5 consts (allowlisted `f64_from_bits`) → migrate.
- The ABI descriptors (`sysv64`, `aapcs64`, `riscv64_lp64d`, `win64`) are large
  immutable structs read many times → **excellent aggregate-const candidates**
  (C-agg-2 rodata), but MAY stay fn at baseline if their struct literal is large;
  classify per implementer judgement, defaulting to const.

> Deliverable for the implementer: before crumb 1, produce the FROZEN exact list
> (grep output) annotated with the 6.1–6.5 bucket per line, and get it into the PR
> description. The counts above are representative, not final.

---

## 7. "No magic values" — the W15 rule + inline-literal inventory

### 7.1 Policy (owner directive #2)

A **magic value** is a bare literal carrying domain meaning at its use site — a
mask, offset, size, index bound, tag, file magic, opcode, or section flag. Under
W15 these become named:

- a single semantic scalar → **`const`** (e.g. `const ADLER32_MODULUS: u32 = 65521`);
- a related integer family used as tags → **`enum`**;
- a bitmask ORed from independent bits → **`flags`**;
- a large immutable aggregate read repeatedly → **aggregate `const`** (rodata).

Exemptions (stay inline, documented): `0`/`1` used as identity/step in arithmetic;
loop bounds that are the obvious `len`; a one-off opcode byte inside a documented
per-instruction ISA encoder table (these are inherently local and already
doc-commented — but a *reused* opcode/mask crosses the ≥2-uses threshold below).

**Threshold heuristic:** a literal that (a) is non-trivial (not `0`/`1`), AND
(b) appears ≥2 times OR encodes an external-format constant (file magic, ABI
number, section flag) MUST be named.

### 7.2 Inline-literal hotspots (grep sampling)

Hex-literal-with-cast density (proxy for magic values), by file:

- `src/backend/stackify.tks` — 109 hits (wasm opcodes, LEB masks, value-type
  bytes). Highest priority: opcode/section families → enum/flags/const table.
- `src/backend/encode_x86_64.tks` — 90; `encode_arm64.tks` — 66;
  `encode_riscv.tks` — 52 (ISA opcode/ModRM/immediate masks). Recurring masks
  (`0xFF`, `0x1F`, field shifts) → named `const`; opcode families → an enum/table.
- `src/backend/objfile_{wasm,macho,elf,coff}.tks` — 24/20/14/9 (file magic,
  section flags, symbol info) → `const`/`flags`/`enum` per 6.3.
- `src/compress/compress.tks` — 12 (ZIP/adler constants) → per 6.1/6.2.

This is a **program-wide retrofit**, far larger than #594's ~50 fns. Recommendation
(law-first, smallest-safe-step): **#594 delivers the `const`/`enum`/`flags` feature
+ migrates the ~50 nullary-fn constants + the object-writer file-magic/section-flag
families (6.3), which are the highest-value, most external-contract-sensitive magic
values.** The ISA-opcode retrofit (encoders) is REPORTED UP as a large follow-on
retrofit for the w15-retrofit skill to sweep file-by-file under the new rule — it
is out of #594's proposal boundary and must not be smuggled in (regression surface
on the frozen-bytes encoders). This keeps #594 at 100% of its stated scope while
honoring the new rule for the parts that belong to it.

### 7.3 W15 rule text (paste into agent + skill)

> **W15 · No magic values.** Every domain-meaningful literal is named. A single
> scalar → `const NAME: T = <const-expr>` (module-level, comp-time, no arena). A
> closed integer tag family → `enum`. A bitmask ORed from independent bits →
> `flags`. A large immutable aggregate read repeatedly → an aggregate `const`
> (rodata). Exempt only `0`/`1` identity/step and a one-off opcode byte inside a
> documented ISA-encoder table. A literal that is non-trivial AND appears ≥2× OR
> encodes an external-format constant (file magic, ABI number, section flag) MUST
> be named. Never migrate a per-call *factory* that seeds fresh mutable state
> (`*_empty()`) into a shared const — that aliases state and breaks value semantics.

---

## 8. Ordered crumb sequence (each: step · shapes · fixtures · ritual)

Each crumb is independently gate-able. The **ritual point** is where the FULL gate
(both engines + `.tkt` + fixpoint gen2==gen3 + 100% delta coverage) must pass.
Bootstrap-seed rule: `const` must be usable by the corpus only *after* it lands in
the seed — so the FEATURE crumbs (1–6) land and become part of the released seed
BEFORE the MIGRATION crumbs (7+) may use `const`/`enum`/`flags` in the compiler's
own source.

### Crumb 1 — parser: `ConstDecl` AST + `parse_const_decl` + wiring
- **Shapes:** §4.1 `ConstDecl` type; `Decl`/`ItemKind` widen; §4.2
  `parse_const_decl`; `parse_decl` arm; `with_outer_doc` gains a `ConstDecl` arm.
- **Fixtures (parser tests, `.tkt`):** parse `const A: i64 = 1` (ok); `pub const B:
  u8 = ~(0 to u8)` (ok); `const C = 1` → error (missing type); `const D: i64` →
  error (missing `=`); `const E: i64 = 1` at LOCAL scope still parses as the
  existing local binding (no regression).
- **Ritual:** full gate. Exit codes: valid programs compile-clean; each malformed
  case exits with the parser's error code (VM and native identical — parse is
  pre-backend so both engines share it).

### Crumb 2 — checker collect: `collect_const_sig` + env binding
- **Shapes:** §4.3 `collect_const_sig`; extend the pass-1 loop in `collect.tks` to
  bind const names/types.
- **Fixtures:** a fn body that references a `const` declared later in the file
  type-checks (forward ref); a const with an unknown type → located error.
- **Ritual:** full gate.

### Crumb 3 — new module `consteval.tks`: `is_const_expr` + allowlist + cycle/order
- **Shapes:** §4.4 all three fns.
- **Fixtures (checker `.tkt`):** each Tier 0–5 form accepted; a `mut`-referencing or
  I/O-calling initializer rejected with a located message; `const A = B` / `const B
  = A` → cycle error; `const B = A + 1; const A = 1` orders A before B.
- **Ritual:** full gate. 100% coverage of every Tier arm and the cycle path.

### Crumb 4 — typer: `type_const_decl` + `type_item` arm + `TConstDecl`/`TItem`
- **Shapes:** §4.5 `TConstDecl` + `TItem` widen; §4.6 `type_const_decl` + arm.
- **Fixtures:** `const A: u8 = 300` → type/range error; `const A: i64 = 1` typed ok
  and usable as a value in an expression; declared-type coercion (`const A: i128 =
  0 to i128`).
- **Ritual:** full gate.

### Crumb 5 — inliner `inline_consts` + pipeline placement
- **Shapes:** §4.7 `inline_consts`; call it in the checker pipeline after
  `monomorphize`, before lowering; `lower_item` defensive `TConstDecl` no-op (§4.8).
- **Fixtures (both-engine):** a program `const K: i64 = 41; fn main() { print(K + 1)
  }` prints 42 on VM AND native; the lowered LIR contains NO reference to `K` and NO
  `LGlobal` (assert `m.globals.len == 0`); nested `const B = A + 1; const A = 1;
  … B` folds fully.
- **Ritual:** full gate + explicit `m.globals.len == 0` assertion (proves no
  honest-stop is reachable). **This is the behavior-preservation keystone.**

### Crumb 6 — `.tkb` codec (C7.16) `TConstDecl` + cross-module `pub const`
- **Shapes:** §4.9 encode/decode arms; `seed_from_dep` surfaces `pub const`s.
- **Fixtures:** module `m1` exports `pub const P: u32 = 0x78`; `m2` uses `m1::P` →
  compiles, both engines, inlined in `m2` (assert no cross-module data symbol); a
  non-`pub` const used cross-module → visibility error.
- **Ritual:** full gate. **RITUAL POINT — end of FEATURE phase.** After this passes
  and is released, `const`/`enum`/`flags` are in the bootstrap seed; migration
  crumbs may now use them in compiler source.

### Crumb 7 — migrate scalar-const families (6.1)
- **Step:** replace each 6.1 nullary fn with `pub const …`; update call sites `X()`
  → `X`. Do it file-by-file (math, compress, lir, io, isel_arm64 i128 trio).
- **Fixtures:** existing tests for each module keep their exit codes; add a golden
  that the migrated value equals the old fn's value (both engines).
- **Ritual:** full gate per file batch (fixpoint gen2==gen3 is the real proof).

### Crumb 8 — migrate enum families (6.2)
- **Step:** introduce `enum BlockType/WasmScopeKind/ElideKind/EdgeResult/WasmVType/
  ZipMethod`; replace the tag fns; route wire-byte emission through a single
  `match`-driven `_wire` helper so emitted bytes are byte-identical.
- **Fixtures:** the deflate/inflate/wasm golden byte tests are UNCHANGED and pass
  (proves wire compatibility); both engines.
- **Ritual:** full gate. Wire-byte identity is the acceptance bar.

### Crumb 9 — migrate flags families + file magic (6.3)
- **Step:** `flags ElfSectionFlags/ElfSymInfo/MachoSectionAttr`; named file-magic
  consts. Object-writer golden byte tests must be byte-identical.
- **Fixtures:** ELF/Mach-O/COFF/wasm object golden tests unchanged; both engines.
- **Ritual:** full gate.

### Crumb 10 (C-agg-2, optional fast-follow) — aggregate consts → rodata
- **Step:** the `lower_item` `TConstDecl` arm interns aggregate consts into
  `m.rodata` (reuse `intern_rodata`) and rewrites uses to `LGlobalAddr`; `[]byte`
  first (`gzip_magic`), then struct consts (needs a rodata layout serializer +
  a typed-value load off `LGlobalAddr`).
- **Fixtures:** `gzip_magic` emits ONE rodata entry, referenced N times; both
  engines read identical bytes; `m.globals.len == 0` still holds (rodata, not
  globals).
- **Ritual:** full gate. Separable PR if it risks the fixpoint.

---

## 9. Risks + law tensions (with resolution)

1. **Fixpoint risk on wire-byte migrations (enum/flags).** A discriminant or OR
   spelling that changes an emitted byte breaks gen2==gen3 and object goldens.
   *Resolution:* route every serialized tag through a single `match`-driven `_wire`
   helper; assert byte-identity against the pre-migration goldens BEFORE changing
   logic. Never let the enum's *internal* discriminant leak to the wire.
2. **Tier-5 allowlist vs "const means const" (a soft tension, not a HALT).** An
   explicit allowlist is less principled than a purity analyzer. *Resolution:*
   ship the allowlist (closed, tiny, reversible; DECISION_LOG); a future `const fn`
   marker supersedes it. Passes all laws; smallest safe step.
3. **Scope creep from "no magic values" (owner #2).** The full program has 1000s of
   magic literals (encoders alone: ~300). *Resolution (law-first, issues-are-100%):*
   #594 delivers the feature + the ~50 nullary consts + object-writer families
   (6.3); the ISA-encoder sweep is REPORTED UP as a separate w15-retrofit lane, not
   folded into #594. This keeps #594 at exactly its proposal boundary.
4. **Aggregate baseline (inline) is not the owner's stated end-state (rodata).**
   *Resolution:* not a tension — baseline inline is behavior-preserving and
   backend-free; C-agg-2 delivers the rodata end-state next, gated by the same
   fixpoint. The owner's "→ rodata" is honored, sequenced for safety.
5. **`*_empty()` factory misclassification.** Converting a fresh-mutable-seed
   factory to a shared const aliases state → NOT behavior-preserving. *Resolution:*
   6.5 explicitly excludes them; the W15 rule text names the trap.

No genuine unresolved tension → no HALT.

---

## 10. Files the implementer will touch (summary)

- `src/parser/ast.tks` — `ConstDecl`, widen `Decl`/`ItemKind`.
- `src/parser/parse_decl.tks` — `parse_const_decl` + arm + `with_outer_doc` arm.
- `src/checker/collect.tks` — `collect_const_sig` + pass-1 binding + `seed_from_dep`.
- `src/checker/consteval.tks` — NEW: `is_const_expr`, `const_dep_order`,
  `is_const_allowlisted_callee`.
- `src/checker/typer.tks` — `type_const_decl` + `type_item` arm.
- `src/checker/tast.tks` — `TConstDecl`, widen `TItem`.
- checker pipeline driver — call `inline_consts` after `monomorphize`.
- `src/lir/lower.tks` — defensive `TConstDecl` no-op arm (+ C-agg-2 rodata later).
- `.tkb` codec (C7.16) — `TConstDecl` encode/decode.
- migration: `src/math/*`, `src/compress/*`, `src/backend/stackify.tks`,
  `src/backend/objfile_*.tks`, `src/backend/isel_*.tks`, `src/lir/lir_interp.tks`,
  `src/io/stream.tks` (crumbs 7–9).
- ZERO backend encoder files, ZERO C twins.

## 11. Note on bootstrap ordering

Crumbs 1–6 are the FEATURE and must be **released into the bootstrap seed** before
crumbs 7+ may spell `const`/`enum`/`flags` in the compiler's own `.tks` sources
(the corpus must not use a feature not yet in its seed). Sequence the PRs so the
feature ships and the seed advances, then migrate.
