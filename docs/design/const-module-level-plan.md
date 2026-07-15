# Module-level `const` — design + crumb plan (#594)

Status: RATIFIED (owner 2026-07-15, with two rulings). Lane:
`fix/issue-594-const-module-level` (base `remodel/backend-build`).
Author: architect. Implementer executes the crumb sequence in order.

> **Owner rulings 2026-07-15 (folded in):**
> - **RULING 1 — aggregates → RODATA at the feature baseline** (not the earlier
>   two-phase inline-then-rodata). This surfaced the data→data relocation
>   constraint (§5.1): Tier A (flat-POD / `[]byte`, the whole ~50) is zero-backend;
>   Tier B (pointer-bearing, e.g. ABI descriptors) needs a data-reloc backend phase
>   (§8 T-B*). Verdict on "backend intocado?": **yes for Tier A / the ~50; no for
>   Tier B.**
> - **RULING 2 — the ~300-literal ISA-encoder magic-value sweep is IN #594**
>   (crumbs S1–S6), sequenced after the feature reaches the seed, each file gated by
>   frozen-byte goldens + fixpoint.

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
6. **Three placements** (owner clarification 2026-07-15, from the issue body): `const`
   is placement-polymorphic — LOCAL, MODULE, and TYPE-MEMBER. See §0.1.

---

## 0.1 Three placements of `const` (owner clarification 2026-07-15)

`const` exists at three sites. All three take `pub` / `exp` / private visibility.
The MODULE and TYPE-MEMBER placements are compile-time constants and **share one
`consteval.tks` (grammar `is_const_expr`, allowlist, cycle/order) and one inliner**;
only the PARSE SITE and the RESOLUTION PATH differ. LOCAL is a distinct, pre-existing
runtime binding and is untouched.

| Placement | Site | Semantics | Resolution | Status |
|---|---|---|---|---|
| **Local** | inside a block/fn body (`BindKind::Const`, `parse_stmt.tks:166`) | a block-scoped **immutable binding** (deep-`let`); MAY bind a runtime value; has a scope/arena | bare name in scope | **EXISTS — unchanged** (§0.2) |
| **Module** | top-level `Decl`/`ItemKind` | a **compile-time** const-expr; no scope, no arena; scalar inlined / aggregate → rodata | bare name (top-level) | NEW — §3, §4.1–4.9 |
| **Type-member** | inside a `struct`/`class` body, peer to fields/methods | **static / type-level** (NOT per-instance, NO layout slot); const-eval identical to module, inlined | `TypeName::NAME` (qualified) | NEW — §3.6, §4.10 |

### 0.2 Reconciliation with the existing LOCAL `const`

The LOCAL `const` (`BindKind::Const`) stays exactly as it is: a block-scoped
immutable binding that may hold a runtime value, treated as non-escaping by
`escape.tks:341,587`. It does **not** flow through `consteval`/the inliner. The
shared keyword is intentional and law-consistent: a local HAS a lexical scope
(hence may evaluate a runtime expression in it), while a module/member const is
comp-time (no scope to evaluate in, so it is restricted to `is_const_expr`).
`parse_decl` (top-level) and the type-body parser dispatch `const` to the NEW
`ConstDecl` path; `parse_stmt` (statement position) keeps dispatching `const` to the
existing local `Binding`. No ambiguity — the three are distinguished purely by parse
position.

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

### D2 · scalar INLINE + aggregate → RODATA at baseline (owner RULING 1, 2026-07-15)

A **scalar** const (any primitive-typed const: the int/float/bool/byte/char
families) is **inlined at every use site** as the equivalent typed literal. No
`LGlobal`, no rodata, no reloc, no arena — a `mov imm`. This is unchanged.

An **immutable aggregate** const (`[]byte`, struct, variant) is **materialized in
rodata directly at the feature baseline** — the owner REJECTED the two-phase
inline-then-rodata plan and wants the end-state now. This requires a **rodata
layout serializer** (serialize a typed aggregate ConstValue into bytes per its
`LStructLayout`) plus a **typed load via `LGlobalAddr`** (read fields with
`LFieldAddr` + `LLoad` off the rodata pointer). Both are lowering-side.

**But whether this stays "zero backend" depends on the aggregate's SHAPE, because
the whole reloc model is `.text`-relative (verified §5.1). Two tiers:**

- **Tier A — self-contained rodata blob (NO internal pointer):** every field is a
  scalar / enum / bool, OR the aggregate is `[]byte` / `str` (bytes in rodata, the
  `{ptr,len}` header built at the USE site exactly as string literals already do).
  The rodata image holds no pointer, so nothing inside rodata must be relocated —
  the only reference is the existing **text→rodata** load. **ZERO backend change.**
  The rodata layout serializer + typed `LFieldAddr`/`LLoad` reads are the new work,
  all in lowering. Covers the ENTIRE owner ~50 inventory (§6). Ships in the feature
  phase.
- **Tier B — pointer/slice-bearing aggregate:** a field is a slice (`[]T`) or a
  pointer, so the rodata image contains a pointer that must hold the address of
  ANOTHER rodata datum → a **data→data relocation whose patch site is INSIDE the
  data section**. That relocation **does not exist** in any writer/encoder/VM
  (§5.1 verdict). This tier BREAKS "zero backend" and becomes a dedicated backend
  phase (§8 crumbs T-B*). The flagship Tier-B consts are the ABI descriptors
  (`sysv64`/`aapcs64`/`riscv64_lp64d`/`win64` — eight `[]u32` slice fields each,
  `abi_aapcs64.tks:14`). **None of the owner's ~50 anemic-const sites are Tier B**
  — they are all Tier A — so the ~50 migrate with zero backend change; Tier B is
  only reached when the pointer-bearing aggregate FACTORIES are also converted.

> Net: scalars have zero use-side cost; Tier-A aggregates lose BOTH the
> definition-side nullary-fn arena AND (via a single shared rodata image + typed
> load) the per-use construction arena, with zero backend change; Tier-B aggregates
> reach the same end-state only after the data-reloc backend crumbs land.

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
  A TYPE-MEMBER const rides along inside the owning `TypeDecl`'s codec (a new
  `consts` list on the struct/class body, §4.10), so a `pub`/`exp` member const
  crosses modules with its type.

### D6 · TYPE-MEMBER const (owner clarification 2026-07-15) — static, `TypeName::NAME`, inlined

A `const` declared inside a `struct`/`class` body, peer to fields and methods, is a
**static (type-level) constant** — NOT a per-instance field. Design, law-first:

- **Grammar (§4.10):** the type-body parser (`parse_fields` / `parse_class_fields`)
  gains a third member branch: after the optional doc + `pub`/`exp` peel, a `const`
  token parses a `ConstDecl` member (the SAME node as a module const, reusing
  `parse_const_decl`). Interleaves freely with fields/methods. `struct`, `class`, and
  `abstract class` bodies accept it; a `trait` accepts it and FOLDS it into each
  deriver like a field; an **`interface` REJECTS it** (interfaces are pure signature
  contracts and carry no statics — `ast.tks:282`, unchanged).
- **Semantics:** const-eval is identical to a module const (`is_const_expr`, same
  tiers, same allowlist). It occupies **NO slot in the struct layout** (`LStructLayout`
  is unchanged; a member const never widens `sizeof(T)`). It is inlined (scalar) or
  rodata-materialized (Tier-A aggregate) exactly like a module const — the owning
  type is only its NAMESPACE for resolution.
- **Access path:** `TypeName::NAME` (qualified, like an enum member `Enum::Member`).
  Anchored in the existing `type_path_expr` (`typer.tks:2283`): today it resolves a
  two-segment path to an ENUM member (`EnumBody`, `:2298`); the new arm — tried
  before the enum arm — asks "does the leading type have a member const named by the
  last segment (walking its base chain)?" and, if so, resolves to that const's
  checked initializer (then the inliner substitutes it). Inside the type's own
  methods, the qualified `TypeName::NAME` (or `Self::NAME` where the `self`/`Self`
  alias from the OOP work exists) is used — bare `NAME` is NOT introduced (avoids
  collision with fields/locals; consistent with enum members always being qualified).
- **Inheritance / OOP (ratified law-first):**
  - **Inherited (by NAME lookup, not by dispatch):** a `pub`/`exp` member const on a
    base IS reachable through a subclass path — `Sub::NAME` resolves by walking from
    `Sub` up its base chain (mirroring `effective_class_methods`) to the base's
    declaration. It is a compile-time, path-qualified lookup convenience, NOT a
    vtable/virtual entry (a member const has no `this`, no runtime identity).
  - **Shadowing = static hiding, ALLOWED:** a subclass MAY redeclare `const NAME`;
    `Sub::NAME` then resolves to the subclass's, `Base::NAME` to the base's. Because
    resolution is by EXPLICIT qualified path and inlined at compile time, there is no
    dispatch ambiguity — each `T::NAME` binds unambiguously to the nearest
    declaration walking from `T`. This is name hiding, NOT `override` (there is
    nothing dynamic to override); the `override` keyword remains method-only and a
    `const` bearing it is a parse error.
  - **Interfaces / abstract:** an interface cannot declare a member const (rejected,
    above). An abstract class CAN (a member const is a concrete value, never an
    abstract requirement). A member const is never part of interface conformance.
  - **Rationale:** this is exactly C#/Java static-const semantics (accessible via the
    subclass name, belongs to the declarer, hideable-not-overridable), reached here
    with ZERO new dispatch machinery because every resolution is a compile-time
    qualified-path lookup that the inliner erases. Passes all laws; smallest safe
    step.
- **Visibility:** `Visibility { Private; Pub; Exp }` (`ast.tks:186`), reusing the
  field/method peel (`parse_class_fields` already peels `pub`/`exp` at `:421`).
  `Private` = usable only within the declaring module; `Pub` = cross-module namespace
  use; `Exp` = exported in the `.tkh` public ABI. Same rules as methods.

### D7 · naming convention (owner 2026-07-15) — `UPPER_SNAKE_CASE`, STYLE not grammar

- **The LANGUAGE imposes NO case.** The parser/checker accept ANY valid identifier
  for a const (module, member, or local) — lowercase, camelCase, anything the normal
  identifier rules allow. There is **no grammar rule and no checker rule** enforcing
  case; `parse_const_decl` / the member branch / the resolver treat a const name as a
  plain identifier. This is a hard constraint on the implementation: do NOT add a
  case check.
- **Our SOURCE convention (W15 style, enforced by the canonicalizer/skill, not the
  compiler):** a real constant is `UPPER_SNAKE_CASE` — `ZIP_METHOD_STORE`,
  `ADLER32_MODULUS`, `BLOCK_TYPE_STORED`, `MASK_ALL_U64`, `SHF_ALLOC`. Applies to
  module consts, member consts, and a LOCAL const that is a genuine compile-time
  constant. A local `const` that merely names a runtime immutable value keeps
  ordinary lowercase (it is not a "constant" in the W15 sense).
- **Enums are `PascalCase` type + `PascalCase` members**, per the repo's existing
  convention (`ast.tks:186` `Visibility { Private; Pub; Exp }`, `minst.tks:11`
  `MRegClass { GPR; FPR }` — acronyms uppercased). So the migrated families are
  `enum BlockType { Stored; Fixed; Dynamic }`, `enum WasmScopeKind { Block; Loop; If }`,
  `enum ElideKind { Normal; Drop; Tee }`, `enum EdgeResult { Stop; Branch; More }`,
  `enum WasmVType { I32; I64; F32; F64 }`, `enum ZipMethod { Store; Deflate }` —
  members are PascalCase, NOT `UPPER_SNAKE`.
- **Flags members** follow the external spec's own names, which for C-ABI flag sets
  are already `UPPER_SNAKE` (`flags ElfSectionFlags { SHF_ALLOC; SHF_EXECINSTR; … }`),
  consistent with the const convention. (No `flags` decl exists in the tree yet — this
  plan introduces the first, so the convention is set here.)

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
The `TypeDecl` codec gains the `consts` list on the struct/class body (§4.10).

### 4.10 Type-member const (parser + typer)

`StructBody` / `ClassBody` / `TraitBody` widen with a `consts: []ConstDecl` list
(peer to `fields`/`methods`); `ParsedStructBody` (the parser's carrier) gains the
same. `parse_fields` / `parse_class_fields` gain the third member branch.

```teko
/**
 * parse_type_member_const — parse a `[pub|exp] const NAME: Type = <const-expr>`
 * member inside a struct/class/trait body, reached from the member loop after the
 * doc + visibility peel when the next token is `const` (#594, owner clarification
 * 2026-07-15). Delegates the `const …` grammar to `parse_const_decl` (the same node
 * a module const uses), so the two placements never diverge. A member const is
 * static/type-level: it takes no layout slot and is resolved by `TypeName::NAME`.
 *
 * @param tokens  the token stream
 * @param pos     the index of the `const` keyword (after the peeled doc/visibility)
 * @param vis     the visibility peeled by the member loop
 * @return        the parsed member ConstDecl and the index past its initializer
 * @throws        a located error on a missing name / `: Type` / `=` / bad initializer
 * @since #594
 */
fn parse_type_member_const(tokens: []lexer::Token, pos: u64, vis: Visibility) -> Parsed<ConstDecl> | error

/**
 * find_member_const — resolve `TypeName::NAME` to its member const's checked
 * initializer, walking `TypeName`'s base chain (a subclass inherits a base's member
 * const by name; a nearer declaration shadows a farther one — static hiding, D6).
 * Returns null when no member const named `seg` exists on the type or its bases (so
 * `type_path_expr` falls through to the enum-member / module-static arms).
 *
 * @param owner  the resolved leading type (struct/class/abstract)
 * @param seg    the last path segment (the member const's name)
 * @param table  the type table (to follow the base chain + read each body's consts)
 * @return       the member const's typed initializer, or null when not found
 * @since #594
 */
fn find_member_const(owner: parser::TypeDecl, seg: str, table: TypeTable) -> TExpr?
```

Wire `find_member_const` into `type_path_expr` (`typer.tks:2283`) as the FIRST arm
for a two-segment path — before the `EnumBody` arm (`:2298`) — so a member const
takes precedence over an equally-named enum member only when the leading path is a
struct/class (they are disjoint type kinds, so no real conflict). Type-checking a
member const's initializer reuses `type_const_decl` (§4.6) with the owning type's
namespace + a `Self`-aware environment; validation reuses `is_const_expr`. An
`interface`/`flags`/`enum`/`alias`/`extern` body carrying a `const` member is a
located parse-or-check error ("a `const` member is only allowed in a struct, class,
or trait body").

---

## 5. Backend impact — Tier A NONE, Tier B REAL (the honest verdict)

### 5.1 Data→data relocation — the load-bearing verdict

**A relocation whose PATCH SITE is inside the data (rodata) section does NOT exist
in this compiler.** Evidence (verified this session):

- **x86_64 (`encode_x86_64.tks:1310`):** `RelocX86.offset` is "relative to this
  function's `.text` base"; `EncodedModuleX86.relocs` are "every relocation,
  `.text`-section-relative". A rodata reference is a **text→rodata** `Pc32`/`Abs64`
  whose patch site is in `.text`, with the rodata byte offset folded into the
  addend. Nothing patches a field inside rodata.
- **ELF writer (`objfile_elf.tks:455`):** the section-header set is exactly
  `["", ".text", ".rodata", ".symtab", ".strtab", ".shstrtab", ".rela.text"]`.
  There is **`.rela.text` but no `.rela.rodata`** — the object cannot carry a
  relocation applied inside `.rodata`.
- **COFF (`objfile_coff.tks:387`):** `coff_apply_rodata_addends` folds each rodata
  reloc's `.rdata` target offset **into the `.text` patch site** — again text→rodata.
- **Mach-O:** same text-section-relative reloc model.
- **wasm (`objfile_wasm.tks:216/640`):** rodata is an **active data segment at a
  fixed, emit-time-known linear-memory offset** — so an intra-data pointer would be
  a compile-time i32 constant (no classic reloc), but the data emitter must
  COMPUTE and WRITE it (a real, if reloc-free, change).

**Consequence:** string literals work because the pointer into rodata is computed
in CODE at the use site (a text→rodata reference) and the rodata blob is flat bytes
with no internal pointer. Any const whose rodata image must itself contain a
pointer (a slice/pointer field) needs a reloc the toolchain cannot emit.

### 5.2 Impact table

| Backend / writer | honest-stop (gates `m.globals` only) | Tier A (scalar + flat-POD + `[]byte`) | Tier B (pointer/slice-bearing) |
|---|---|---|---|
| C backend (`--backend=c`) | — | none (emit `static const` + reference; strings already do) | data-init pointer resolvable in C init, but see VM/native — sequence with them |
| x86_64 (`encode_x86_64.tks`) | `honest_globals_x86` (`:1495`) | none (text→rodata load exists) | **widen `RelocX86` with a patch-site SECTION tag + emit data-section relocs** |
| arm64 (`encode_arm64.tks`) | `honest_globals`/`A4-globals` (`:1858`) | none | **same: data-section reloc emission** |
| riscv64 (`encode_riscv.tks`) | `honest_globals_riscv` (`:1684`) | none | **same** |
| wasm (`stackify.tks`/`objfile_wasm.tks`) | `wasm_honest_globals`/`C1-globals` (`:4458`) | none | **compute+write intra-data i32 offsets in the data segment (no reloc, but new)** |
| ELF writer | — | none | **new `.rela.rodata` section + rela emission** |
| Mach-O writer | — | none | **new rodata-section (local) relocations** |
| COFF writer | — | none | **new `.rdata` relocations (patch site in `.rdata`)** |
| LIR interp (VM) | — | none (typed load off `LGlobalAddr` reuses `LFieldAddr`/`LLoad`; `lir_interp.tks:206/527`) | **resolve a rodata-INTERNAL pointer field to its target rodata base at load** |

**Tier A: no honest-stop is touched (we never populate `m.globals`) and no backend
file changes.** The only Tier-A validation risk is that `LFieldAddr` must accept a
rodata-`LGlobalAddr` base across the 4 encoders + VM — expected to already hold
(it is base+offset), asserted in the Tier-A fixtures.

**Tier B: NOT zero-backend.** It is a dedicated phase touching the 3 native
encoders (patch-site section tag), the ELF/Mach-O/COFF writers (a data-section
relocation), the wasm data emitter (emit-time offsets), and the VM (rodata-internal
pointer resolution). Sequenced in §8 (crumbs T-B1..T-B5), gated by the same
fixpoint + golden bytes. **Only needed to convert pointer-bearing aggregate
factories (ABI descriptors); the owner's ~50 do not require it.**

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

### 6.4 → rodata aggregate — Tier A (feature phase, ZERO backend)

All owner-cited aggregates are **Tier A** (self-contained rodata blob, no internal
pointer): they materialize in `m.rodata` at the feature baseline (D2, RULING 1).

- `src/compress/gzip.tks:16` `gzip_magic -> []byte { [0x1F, 0x8B] }` → the 2 bytes
  live in rodata; the `{ptr,len}` header is built at the use site (the string-literal
  mechanism). Tier A.
- `src/backend/minst.tks:1257` `ret_inst -> MInst { MRet {} }` → an empty variant,
  a zero-size (or tag-only) blob. Tier A.
- `src/backend/isel_x86_64.tks:155` `rax_x86 -> MReg { preg(0, GPR) }` (Tier-5 grammar) →
  `MReg { id: u32; reg_class: enum; is_phys: bool }` is **flat POD** (no pointer) →
  serialized to a fixed byte image in rodata, read via `LFieldAddr`+`LLoad`. Tier A.

### 6.5b → rodata aggregate — Tier B (needs data-reloc backend phase, §8 T-B*)

**Not in the owner's ~50.** Only reached if the pointer-bearing aggregate factories
are converted:

- `src/backend/abi_{sysv64,aapcs64,riscv64,win64}.tks` ABI descriptors — eight
  `[]u32` slice fields (`abi_aapcs64.tks:14`). Full rodata materialization needs a
  data→data reloc (§5.1) → deferred behind crumbs T-B1..T-B5, OR legitimately stay
  `fn` (a genuine pointer-bearing aggregate whose per-call construction is honest
  until data-relocs exist). Recommend: **convert after T-B lands**; until then they
  stay `fn` with a `// #594 Tier-B: awaits data-reloc` doc-note.

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
- The ABI descriptors (`sysv64`, `aapcs64`, `riscv64_lp64d`, `win64`) are
  **pointer-bearing aggregates (Tier B, §6.5b)** — they are NOT in the owner's ~50
  and require the data-reloc backend phase (§8 T-B*) before they can be rodata
  consts. Until then they stay `fn` (honest per-call construction).

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

This is a **program-wide retrofit**. Owner **RULING 2 (2026-07-15): it enters #594
in full** — "em meio ao código também, feito por inteiro aqui." So #594 delivers,
in order: (1) the `const`/`enum`/`flags` feature; (2) the ~50 nullary-fn constants +
the object-writer file-magic/section-flag families (6.3); (3) **a file-by-file sweep
of the ISA encoders** `encode_x86_64.tks` (90 hits), `encode_arm64.tks` (66),
`encode_riscv.tks` (52), `stackify.tks` (109), plus the object writers, turning each
recurring opcode / mask / field constant into `const`/`enum`/`flags` per the W15
rule. The sweep is sequenced AFTER the feature reaches the bootstrap seed (crumbs
1–7) so the encoders may spell `const`/`enum`/`flags`, and each file is its own
crumb with **frozen-byte golden + fixpoint gen2==gen3 as the per-file acceptance
bar** — the emitted machine bytes must not change by a single byte.

Exempted even under RULING 2: a genuinely one-off opcode byte that appears exactly
once in a documented per-instruction encoder table (naming it adds indirection
without removing a magic value); the ≥2×-OR-external-format threshold (§7.1)
decides. The `*_empty()` factories are never touched (they are not constants).

### 7.3 W15 rule text (paste into agent + skill)

> **W15 · No magic values.** Every domain-meaningful literal is named. A single
> scalar → `const UPPER_SNAKE_CASE: T = <const-expr>` (module, member, or
> genuinely-constant local; comp-time, no arena). A closed integer tag family →
> `enum PascalCase { PascalMember; … }` (per the repo's enum convention). A bitmask
> ORed from independent bits → `flags` (members keep the external spec's names, e.g.
> `SHF_ALLOC`). A large immutable aggregate read repeatedly → an aggregate `const`
> (rodata). **Case is a STYLE convention of our source, NOT a language rule — the
> compiler accepts any valid identifier; this rule is enforced by the canonicalizer,
> never by the parser/checker.** Exempt only `0`/`1` identity/step and a one-off
> opcode byte inside a documented ISA-encoder table. A literal that is non-trivial
> AND appears ≥2× OR encodes an external-format constant (file magic, ABI number,
> section flag) MUST be named. Never migrate a per-call *factory* that seeds fresh
> mutable state (`*_empty()`) into a shared const — that aliases state and breaks
> value semantics.

---

## 8. Ordered crumb sequence (each: step · shapes · fixtures · ritual)

Each crumb is independently gate-able. The **ritual point** is where the FULL gate
(both engines + `.tkt` + fixpoint gen2==gen3 + 100% delta coverage) must pass.
Bootstrap-seed rule: `const` must be usable by the corpus only *after* it lands in
the seed — so the FEATURE crumbs (1–8) land and become part of the released seed
BEFORE the MIGRATION crumbs (9+) and the RULING-2 encoder sweep (S*) may use
`const`/`enum`/`flags` in the compiler's own source. The Tier-B backend phase
(T-B*) is a separate, later track needed only for pointer-bearing aggregates.

**Phase map:** Feature = crumbs 1–8 (crumb 7 = TYPE-MEMBER const) · Migration =
9–11 · RULING-2 encoder sweep = S1–S6 · Tier-B backend + ABI-descriptor migration =
T-B1–T-B6.

**Seed-bump points (owner 2026-07-15 — multiple bumps expected; bootstrap is
incremental).** The seed (`teko.tkp`) advances SEVERAL times so later crumbs may use
what earlier crumbs added. The D36 per-merge `-beta` mechanism already tags a seed
per merge; the CAPABILITY-GATING bumps — the ones that unlock a feature for the
corpus's OWN source — are:
- **BUMP #1 — after Crumb 8 (🔑):** the three `const` placements + `enum`/`flags` +
  Tier-A aggregate rodata + the `.tkb` codec are in the seed. This UNLOCKS the
  migration (9–11) and the S* sweep to spell `const`/`enum`/`flags` anywhere in the
  compiler source. The single most important bump.
- **BUMP #2 — rolling, across Crumbs 9–11 + S1–S6:** each migration/sweep merge tags
  its own `-beta` (D36) so the ever-growing use of `const`/`enum`/`flags` in
  `src/` stays self-hosting; these are mechanism bumps, not new capabilities.
- **BUMP #3 — after Crumb T-B5 (🔑):** the data→data relocation capability is in the
  seed. This UNLOCKS Crumb T-B6 (migrating pointer-bearing aggregates — ABI
  descriptors — to rodata consts the compiler's own source then uses). Tier-B may
  itself need >1 intermediate bump (T-B1 reloc model → T-B2/3 writers → T-B4 wasm →
  T-B5 VM) if a later T-B crumb's source uses an earlier one's capability.

Lane→umbrella promotion stays "as early as possible" (owner); the internal seed
bumps are the mechanism for making each increment available to the corpus.

### Crumb 1 — parser: MODULE `ConstDecl` AST + `parse_const_decl` + wiring
- **Shapes:** §4.1 `ConstDecl` type; `Decl`/`ItemKind` widen; §4.2
  `parse_const_decl`; `parse_decl` arm (reuses the EXISTING `pub`/`exp` peel at
  `parse_decl.tks:884–888`, so a module const takes `pub` OR `exp` for free);
  `with_outer_doc` gains a `ConstDecl` arm.
- **Fixtures (parser tests, `.tkt`):** parse `const A: i64 = 1` (ok); `pub const B:
  u8 = ~(0 to u8)` (ok); `exp const C: u32 = 0x78` (ok — `exp` accepted); `const D =
  1` → error (missing type); `const E: i64` → error (missing `=`); `const F: i64 =
  1` at LOCAL scope still parses as the existing local binding (no regression).
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

### Crumb 5 — inliner `inline_consts` (SCALARS) + pipeline placement
- **Shapes:** §4.7 `inline_consts` (scalar arm); call it in the checker pipeline
  after `monomorphize`, before lowering; `lower_item` defensive `TConstDecl` no-op.
- **Fixtures (both-engine):** `const K: i64 = 41; fn main() { print(K + 1) }` prints
  42 on VM AND native; the lowered LIR contains NO reference to `K` and NO `LGlobal`
  (assert `m.globals.len == 0`); nested `const B = A + 1; const A = 1; … B` folds.
- **Ritual:** full gate + `m.globals.len == 0` assertion. Scalar keystone.

### Crumb 6 — Tier-A aggregate → RODATA (owner RULING 1): layout serializer + typed load
- **Shapes:** a new `rodata layout serializer` (serialize a typed flat-POD aggregate
  ConstValue to bytes per its `LStructLayout`; `[]byte`/`str` reuse the string
  intern path); `lower_item` `TConstDecl` arm materializes a Tier-A aggregate via
  `intern_rodata` and rewrites uses to a typed `LGlobalAddr` read (`LFieldAddr` +
  `LLoad`, or a struct-copy from the rodata base into a caller slot when the value
  is passed by value). `inline_consts` routes aggregate references to the rodata
  symbol instead of substituting a literal.
- **Fixtures (both-engine):** `const M: MReg = preg(0, MRegClass::GPR)` referenced
  N times emits **ONE** rodata entry, all uses read identical bytes on VM AND
  native; `gzip_magic: []byte` → 2 bytes in rodata, header at use, byte-identical;
  `ret_inst: MInst = MRet {}` round-trips; assert `m.globals.len == 0` still holds
  (rodata, NOT globals — no honest-stop reachable); assert `LFieldAddr` accepts an
  `LGlobalAddr` base on all 4 encoders + VM (the sole Tier-A backend risk).
- **Ritual:** full gate. **Aggregate keystone** — proves the "→ rodata" end-state
  with zero backend change for Tier A.

### Crumb 7 — TYPE-MEMBER const (owner clarification): type-body parse + `TypeName::NAME` resolution
- **Shapes (§4.10, D6):** widen `StructBody`/`ClassBody`/`TraitBody` + `ParsedStructBody`
  with `consts: []ConstDecl`; `parse_type_member_const` + the third member branch in
  `parse_fields`/`parse_class_fields` (reject `const` in interface/enum/flags/alias/
  extern bodies); `find_member_const` (base-chain walk) wired as the first two-segment
  arm in `type_path_expr` (`typer.tks:2283`); reuse `type_const_decl` + `is_const_expr`
  + the inliner for the member's initializer. NO `LStructLayout` change (a member const
  takes no slot).
- **Fixtures (both-engine):** `type P = struct { x: i64; pub const ORIGIN_X: i64 = 0 }`
  → `P::ORIGIN_X` reads 0, `sizeof(P)` unchanged (assert layout identical to a
  const-less `P`); a subclass `Sub` of a base with `pub const K` resolves `Sub::K` to
  the base value (inherited); `Sub` redeclaring `const K` → `Sub::K` shadows,
  `Base::K` still the base's; `const` in an `interface` body → located error; a
  `private const` used from another module → visibility error; an aggregate member
  const `const R: MReg = preg(0, MRegClass::GPR)` materializes ONE rodata entry.
- **Ritual:** full gate. Layout-invariance + inheritance/shadowing are the acceptance
  bars.

### Crumb 8 — `.tkb` codec (C7.16) `TConstDecl` (module + member) + cross-module `pub`/`exp`
- **Shapes:** §4.9 encode/decode arms (module const as a `TItem`; member const inside
  the `TypeDecl` body's `consts` list); `seed_from_dep` surfaces `pub`/`exp` module +
  member consts.
- **Fixtures:** module `m1` exports `pub const P: u32 = 0x78` and `type T = struct {
  exp const Q: u32 = 9 }`; `m2` uses `m1::P` and `m1::T::Q` → compiles, both engines
  (scalar inlined in `m2`); a `pub const A: MReg = …` aggregate re-materializes ONE
  rodata entry per consuming module; a non-`pub`/`exp` const used cross-module →
  visibility error.
- **Ritual:** full gate. **RITUAL POINT + 🔑 SEED BUMP #1 — end of FEATURE phase.**
  After this passes and the seed is bumped, all three `const` placements (+
  `enum`/`flags` + Tier-A aggregate rodata) are in the bootstrap seed; migration +
  the encoder sweep may now spell them in compiler source.

### Crumb 9 — migrate scalar + Tier-A-aggregate const families (6.1, 6.4)
- **Step:** replace each 6.1 scalar fn and each 6.4 Tier-A aggregate fn with
  `pub const UPPER_SNAKE: T = …`; update call sites `x()` → `X`. Names go
  `UPPER_SNAKE_CASE` per D7/W15 (`u8_max` → `U8_MAX`, `adler32_modulus` →
  `ADLER32_MODULUS`, `gzip_magic` → `GZIP_MAGIC`, `ret_inst` → `RET_INST`, `rax_x86`
  → `RAX_X86`). File-by-file (math, compress, lir, io, isel_arm64 i128 trio, gzip,
  minst, register consts).
- **Fixtures:** existing per-module tests keep their exit codes; a golden asserts the
  migrated value equals the old fn's value (both engines).
- **Ritual:** full gate per file batch (fixpoint gen2==gen3 is the real proof). Each
  merge tags a `-beta` (rolling BUMP #2).

### Crumb 10 — migrate enum families (6.2)
- **Step:** introduce `enum BlockType/WasmScopeKind/ElideKind/EdgeResult/WasmVType/
  ZipMethod`; replace the tag fns; route wire-byte emission through a single
  `match`-driven `_wire` helper so emitted bytes are byte-identical.
- **Fixtures:** the deflate/inflate/wasm golden byte tests are UNCHANGED and pass
  (proves wire compatibility); both engines.
- **Ritual:** full gate. Wire-byte identity is the acceptance bar.

### Crumb 11 — migrate flags families + file magic (6.3)
- **Step:** `flags ElfSectionFlags/ElfSymInfo/MachoSectionAttr`; named file-magic
  consts. Object-writer golden byte tests must be byte-identical.
- **Fixtures:** ELF/Mach-O/COFF/wasm object golden tests unchanged; both engines.
- **Ritual:** full gate.

### Crumbs S1–S6 — RULING-2 ISA-encoder + writer magic-value sweep (file-by-file)
Sequenced AFTER crumb 8 (feature in seed). Each file is one crumb; the acceptance
bar is **frozen machine/object bytes + fixpoint gen2==gen3** — not one emitted byte
may change. Recurring opcode/mask/field literals → `const`/`enum`/`flags` per W15.
- **S1** `src/backend/stackify.tks` (109 hits): wasm opcode `enum`, LEB masks →
  `const`, value-type already via `WasmVType` (crumb 10).
- **S2** `src/backend/encode_x86_64.tks` (90): ModRM/REX field masks → `const`,
  opcode families → an `enum`/named table.
- **S3** `src/backend/encode_arm64.tks` (66): field masks/shifts → `const`.
- **S4** `src/backend/encode_riscv.tks` (52): funct/opcode fields → `enum`/`const`.
- **S5** `src/backend/objfile_{elf,macho,coff,wasm}.tks` residual (header fields,
  alignments) → `const`/`flags` (the file-magic/section-flag families already done
  in crumb 10; S5 mops up the rest).
- **S6** remaining `src/compress/*`, `src/crypto/*`, `src/encoding/*` magic literals.
- **Fixtures (each S*):** the file's existing golden byte tests + a fixpoint run;
  add a golden ONLY if a constant had no prior byte-level coverage.
- **Ritual:** full gate per file. Byte-identity is non-negotiable.

### Crumbs T-B1–T-B6 — Tier-B pointer-bearing aggregate → rodata (data-reloc phase)
A SEPARATE, LATER track (not required by the owner's ~50). Delivers the data→data
relocation absent today (§5.1), then migrates the ABI descriptors.
- **T-B1** widen the reloc model: add a patch-site SECTION tag to `RelocX86` /
  arm64/riscv `Reloc` (today `.text`-only, `encode_x86_64.tks:1310`); the LIR carries
  a data-section relocation entry for a rodata-internal pointer field.
- **T-B2** ELF writer: emit a `.rela.rodata` section (add to the section set at
  `objfile_elf.tks:455`) + its relas.
- **T-B3** Mach-O + COFF writers: emit rodata-section (`.rdata`) local relocations
  whose patch site is inside the data section.
- **T-B4** wasm: compute+write intra-data i32 offsets in the active data segment
  (`objfile_wasm.tks:640`) — no reloc, but new emit-time resolution.
- **T-B5** VM (`lir_interp.tks:527`): resolve a rodata-INTERNAL pointer field to its
  target rodata base at typed load. **🔑 SEED BUMP #3 after T-B5** — the data-reloc
  capability is now in the seed; T-B6's source may use pointer-bearing aggregate
  consts. (T-B1..T-B5 may each tag an intermediate `-beta` if a later T-B crumb's
  source uses an earlier one's capability.)
- **T-B6** migrate the ABI descriptors (`SYSV64`/`AAPCS64`/`RISCV64_LP64D`/`WIN64`,
  `UPPER_SNAKE` per D7) and any other pointer-bearing aggregate to rodata consts.
- **Fixtures:** `const AAPCS64: AbiDescriptor = …` emits ONE rodata blob with data
  relocs to its `[]u32` leaf arrays; both engines read identical register lists; all
  object goldens updated once, then frozen.
- **Ritual:** full gate per crumb; regalloc golden tests (they consume the ABI
  descriptors) must be byte-identical after T-B6.

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
3. **Scope of "no magic values" (owner RULING 2).** The ISA encoders hold ~300
   magic literals over frozen-byte code. *Resolution:* per RULING 2 the sweep is IN
   #594 (crumbs S1–S6), sequenced after the feature reaches the seed, each file
   gated by frozen-byte goldens + fixpoint so not one emitted byte moves. This is the
   dominant regression surface of #594 — the byte-identity bar is the mitigation.
4. **Data→data relocation does not exist (§5.1) — the RULING-1 hard constraint.**
   Materializing a pointer-bearing aggregate (ABI descriptors) fully in rodata needs
   a relocation whose patch site is inside the data section; no writer/encoder/VM
   emits one. *Resolution:* TIER the aggregates. Tier A (flat-POD / `[]byte`, the
   entire owner ~50) ships in the feature phase with **zero backend change**. Tier B
   (pointer-bearing) is a separate backend track (crumbs T-B1–T-B6) that first BUILDS
   the data-reloc across ELF/Mach-O/COFF/wasm/VM, then migrates. This is the honest
   answer to "backend fica intocado ou não": **intocado para Tier A / os ~50;
   TOCADO (3 encoders + 3 writers + wasm emit + VM) para Tier B.**
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
- `src/lir/lower.tks` — `TConstDecl` arm: scalar no-op (inlined upstream) + **Tier-A
  aggregate rodata materialization** (layout serializer + typed `LGlobalAddr` load).
- `.tkb` codec (C7.16) — `TConstDecl` encode/decode.
- `src/parser/ast.tks` + `src/parser/parse_decl.tks` (crumb 7) — widen
  `StructBody`/`ClassBody`/`TraitBody`/`ParsedStructBody` with `consts`;
  `parse_type_member_const` + the member-loop branch.
- `src/checker/typer.tks` (crumb 7) — `find_member_const` + the `type_path_expr`
  two-segment arm for `TypeName::NAME`.
- migration (crumbs 9–11), names `UPPER_SNAKE`: `src/math/*`, `src/compress/*`,
  `src/backend/stackify.tks`, `src/backend/objfile_*.tks`, `src/backend/isel_*.tks`,
  `src/lir/lir_interp.tks`, `src/io/stream.tks`, `src/backend/minst.tks`,
  `src/compress/gzip.tks`.
- RULING-2 sweep (crumbs S1–S6): `encode_x86_64.tks`, `encode_arm64.tks`,
  `encode_riscv.tks`, `stackify.tks`, `objfile_*.tks` — frozen bytes.
- **Feature + Tier A: ZERO backend encoder/writer changes, ZERO C twins.**
- **Tier B ONLY (crumbs T-B1–T-B6, pointer-bearing aggregates):** the 3 native
  encoders (reloc section tag), ELF/Mach-O/COFF writers (data-section relocs), wasm
  data emitter, VM `lir_interp.tks`.

## 11. Note on bootstrap ordering — MULTIPLE seed bumps (owner 2026-07-15)

The seed advances SEVERAL times; a single end-of-phase bump is NOT required. The
corpus must never use a feature not yet in its seed, so the capability-gating bumps
sequence the work:

- Crumbs 1–8 are the FEATURE (three placements + `enum`/`flags` + Tier-A aggregate
  rodata + codec). **🔑 SEED BUMP #1 after crumb 8** lets crumbs 9+ and the S* sweep
  spell `const`/`enum`/`flags` in `src/`.
- Crumbs 9–11 + S1–S6 each tag a rolling `-beta` (D36 per-merge) so the growing use
  of the feature in the compiler's own source stays self-hosting (BUMP #2, rolling).
- The Tier-B track (T-B*) adds a NEW capability (data→data reloc). **🔑 SEED BUMP #3
  after T-B5** lets T-B6 migrate pointer-bearing aggregates the compiler source then
  uses. T-B may need >1 intermediate bump internally.

Tier-B can run in parallel with migration/sweep (it adds capability, not a
dependency on migrated sources). Lane→umbrella promotion stays "as early as
possible" (owner); the internal seed bumps make each increment available to the
corpus incrementally.
