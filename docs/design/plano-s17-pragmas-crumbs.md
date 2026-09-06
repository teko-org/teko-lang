# §17 — Conditional Compilation (`#if`/`#elseif`/`#else`/`#endif` + `#os`/`#arch`) — Implementation Plan

Architect plan. HEAD `deb76e5a`. Read-only survey + this design doc. No product edits.

Owner sequencing: "17 antes de 16" — §17 is the prerequisite for §16 (per-target FFI) and the
§10-(c) reactor (per-`#os` selection of ucontext/Fibers, io_uring/epoll/kqueue/IOCP). This plan
EXTENDS the existing `#os` machinery; it does not duplicate it.

Standing-law note: `comptime`/§14 is NOT reopened. The predicate evaluator defined here is a
**boolean-over-constants prune evaluator** (os/arch/flag string equality composed with `&&`/`||`/`!`),
evaluated at the prune step, computing NO value and generating NO code. Owner ruling: this is the
accepted mechanism of the `#if` family.

---

## 1. The existing `#os` machinery — exact map (file:line on HEAD `deb76e5a`)

The pieces that already exist and that the new work extends:

- **Attribute parse** — `src/parser/parse_decl.tks:1608-1613` (inside `parse_decl_attributes`,
  `src/parser/parse_decl.tks:1593`). `#os("…")` is recognized token-by-token: `Hash Ident("os")
  LParen Str RParen`. The quoted OS string lands in `ParsedAttributes.os_guard`.
  - `ParsedAttributes` shape — `src/parser/result.tks:43`:
    `struct { is_test: bool; os_guard: str; has_arena_size: bool; arena_size: u64; next: u64 }`.
- **Where the guard rides** — `Function.os_guard: str` — `src/parser/ast.tks:626`
  (`"" = all OSes`). It is threaded into the parsed `Function` by `parse_function`
  (`src/parser/parse_decl.tks:418`, param `os_guard`) via `parse_fn_decl_with_attrs`
  (`src/parser/parse_decl.tks:2066-2068`).
- **Function-only restriction** — `reject_fn_only_attrs_on_type`
  (`src/parser/parse_decl.tks:2083-2089`): `#os` on a `type`/`flags`/`const` is a parse error
  (`src/parser/parse_decl.tks:2086`). THIS is the gate the "widen to all items" crumb removes.
- **The build-time OS value** — `target_os(m: Manifest)` — `src/build/project.tks:106-113`:
  derived from the `[extern] target` triple (`teko::str::contains` on `linux`/`darwin`/`macos`/
  `apple`/`windows`/`mingw`/`w64`) else the host `teko::os()`.
  - `teko::os()` lowers to `tk_rt_os()` — `src/runtime/teko_rt.c:4393-4404`: value set
    `{"linux","macos","windows","unknown"}`.
- **The PRUNE itself** — `prune_os(program, tos)` — `src/build/project.tks:117-132`: walks
  `program.items`, and for each `Item` whose `content` is a `Function` with
  `os_guard != "" && os_guard != tos`, sets `keep = false`; unguarded items always pass. Rebuilds a
  fresh `parser::Program` of survivors.
- **Where the prune sits in the pipeline** — `frontend_check`
  (`src/build/project.tks:468-483`), line **470**:
  ```
  prune_os(pf.parsed, target_os(m))   // 470  ← PRUNE
    → expand_macros_syntactic          // 472
    → load_deps_program                // 478
    → checked_program_of (type-check)  // 480
  ```
  The prune runs BEFORE macro expansion, dep loading, name-resolution and type-check. This is
  exactly the ordering §17 requires (crumb-5): a `#os("windows")`-only item that references a
  Windows-only extern is DROPPED on Linux before the checker ever sees it, so it cannot error.
- **The flattened item model the prune consumes** — `parser::Item`
  (`src/parser/ast.tks:1010`): `struct { content: @ItemKind(); namespace: str; file: str }`;
  `parser::Program` (`src/parser/ast.tks:1011`): `struct { items: []Item }`.
  `@ItemKind()` (`src/parser/ast.tks:998`):
  `UseDecl | @Statement() | Function | TypeDecl | ConstDecl | MacroDecl | ComptimeDecl`.
- **Flatten site (Decl → Item)** — `src/build/assemble.tks`: `asm_module_items`
  (`src/build/assemble.tks:55-76`) builds `Item{}` literals for each `Module.decl`;
  `asm_main_items` (`:37-52`) for a `MainFile`.
- **Arch already exists at build time** — `teko::arch()` → `tk_rt_arch()`
  (`src/runtime/teko_rt.c:4419-4428`): value set `{"x86_64","arm64","unknown"}` (canonical
  spellings, NOT `aarch64`/`amd64`). Consumed today by `host_target_for_os`
  (`src/build/project.tks:2734-2740`) and `host_default_target`
  (`src/build/project.tks:2755`). A triple-token canonicalizer already exists:
  `arch_token_canonical` (`src/build/regression.tks:872-876`: `aarch64→arm64`,
  `amd64`/`x64`→`x86_64`).

**Serialization boundary (correctness anchor).** The prune runs on `parser::Program` BEFORE
`checked_program_of`. `.tkb` serializes the CHECKED `TProgram` (`src/emit/tkb_write.tks:546` writes
`Function.os_guard`), never `parser::Item`. So any prune-only field added to `Item` is ERASED by the
prune before checking and never reaches `.tkb`. `Function.os_guard` still flows to `.tkb`
(tkb_write:546, tkb_read:749, tkb_frame:305) and to `merge` dedup (`src/checker/merge.tks:650`) on
the SURVIVING function exactly as today — see the no-regression constraint in §4.

---

## 2. The build-time predicate evaluator (the core new machinery)

A boolean-over-constants AST + evaluator, mirroring the `Expr` struct-wrapper recursion pattern
(`src/parser/ast.tks:310-311`: `macro ExprKind()` inline union + `Expr = struct { kind }`). New,
lives in `src/parser/ast.tks` (the AST) and a new prune module `src/build/prune.tks` (the eval).

### 2.1 Predicate AST (add to `src/parser/ast.tks`)

```teko
/**
 * PredKind — the build-time conditional-compile predicate node kind (§17). A boolean-over-constants
 * tree over the `os`/`arch` axes and build flags, composed with `!`/`&&`/`||`. It is a PRUNE
 * predicate ONLY: evaluated at the build-time prune (`eval_pred`), it computes no value and reopens
 * no `comptime`/§14 machinery. Recursive arms (`PNot`/`PAnd`/`POr`) box their `@Pred()` operands
 * under CR1, exactly as `@ExprKind()` boxes its recursive members.
 *
 * @since §17
 */
macro PredKind() { lowering { parser::PTrue | parser::PEq | parser::PFlag | parser::PNot | parser::PAnd | parser::POr } }

/**
 * Pred — a conditional-compile predicate: the struct wrapper over `@PredKind()`, mirroring `Expr`
 * over `@ExprKind()`. An UNGUARDED item carries `Pred { kind = PTrue { } }` (always kept), so the
 * prune has a single uniform shape to evaluate for every item.
 *
 * @field kind  the predicate node (`@PredKind()` inline union)
 * @since §17
 */
pub type Pred = struct { kind: @PredKind() }

/**
 * PTrue — the always-true predicate: an UNGUARDED top-level item (the `#os`-absent default). The
 * prune keeps it on every target. Also the identity that `#if`-arm composition ANDs against.
 * @since §17
 */
pub type PTrue = struct { }

/**
 * PEq — an axis-equality atom: `os == "linux"` (from `#os("linux")` or `#if(os == "linux")`) or
 * `arch == "arm64"` (from `#arch("arm64")` or `#if(arch == "arm64")`). `axis` is `"os"` or `"arch"`;
 * any other axis evaluates false (an unknown axis never matches).
 *
 * @field axis   the constant axis being compared — `"os"` | `"arch"`
 * @field value  the string literal the axis must equal
 * @since §17
 */
pub type PEq = struct { axis: str; value: str }

/**
 * PFlag — a build-flag atom: a bare identifier in `#if(<flag>)` that is true iff the flag is present
 * in the build's flag set (`CcEnv.flags`). Enables `#if(debug)` / `#if(feature_x && os == "linux")`.
 *
 * @field name  the build-flag identifier
 * @since §17
 */
pub type PFlag = struct { name: str }

/** PNot — logical negation `!<pred>` (boxed operand). @field inner the negated predicate. @since §17 */
pub type PNot = struct { inner: @Pred() }
/** PAnd — logical conjunction `<l> && <r>` (boxed operands, short-circuit in eval). @since §17 */
pub type PAnd = struct { left: @Pred(); right: @Pred() }
/** POr — logical disjunction `<l> || <r>` (boxed operands, short-circuit in eval). @since §17 */
pub type POr = struct { left: @Pred(); right: @Pred() }
```

### 2.2 The constant environment

```teko
/**
 * CcEnv — the conditional-compilation constant environment: the fixed build-time facts a §17
 * predicate is evaluated against. `os`/`arch` come from the resolved target (the `[extern] target`
 * triple, else the host); `flags` is the build's flag set (manifest/CLI — see crumb F). It is a pure
 * data snapshot: constructed once per build, read-only during the prune.
 *
 * @field os     the target OS — `target_os(m)` ("linux"|"macos"|"windows"|"unknown")
 * @field arch   the target arch — `target_arch(m)` ("x86_64"|"arm64"|"unknown")
 * @field flags  the set build flags (empty until crumb F lands the flag source)
 * @since §17
 */
pub type CcEnv = struct { os: str; arch: str; flags: []str }
```

### 2.3 The evaluator (new `src/build/prune.tks`)

```teko
/**
 * eval_pred — evaluate a §17 conditional-compile predicate against the build's constant
 * environment, yielding whether the guarded item SURVIVES the prune. Pure, total, boolean-only: it
 * reads `env` and the predicate tree and returns a `bool`; it computes no value and touches no
 * checker/`comptime` state. `&&`/`||` short-circuit. An unknown axis or absent flag is simply false
 * (a predicate nobody's target satisfies prunes the item, never errors).
 *
 * @param p    the predicate (an item's `guard`, or a `#if`-arm's composed predicate)
 * @param env  the build-time constant environment (os/arch/flags)
 * @return     true iff the guarded item survives on this target
 * @since §17
 */
fn eval_pred(p: @Pred(), env: CcEnv): bool {
    match p.kind {
        parser::PTrue => true
        parser::PEq as e => pred_axis_eq(e, env)
        parser::PFlag as f => str_list_contains(env.flags, f.name)
        parser::PNot as n => !eval_pred(n.inner, env)
        parser::PAnd as a => eval_pred(a.left, env) && eval_pred(a.right, env)
        parser::POr as o => eval_pred(o.left, env) || eval_pred(o.right, env)
    }
}

/**
 * pred_axis_eq — resolve a `PEq` axis atom against the environment. `"os"` compares `env.os`,
 * `"arch"` compares `env.arch`; any other axis name is false (unknown axes never match).
 *
 * @param e    the axis-equality atom
 * @param env  the build-time constant environment
 * @return     true iff the named axis equals `e.value`
 * @since §17
 */
fn pred_axis_eq(e: parser::PEq, env: CcEnv): bool {
    if e.axis == "os" { return env.os == e.value }
    if e.axis == "arch" { return env.arch == e.value }
    false
}
```

(`str_list_contains` is a trivial `[]str` membership helper; use `teko::list`/`teko::str` if an
existing one is found, else a 5-line local with full Javadoc.)

### 2.4 `target_arch` — the build-time arch value (sibling of `target_os`)

```teko
/**
 * target_arch — the TARGET arch for §17 `#arch`/`arch ==` guard selection, the exact sibling of
 * `target_os` (`src/build/project.tks:106`): derived from the `[extern] target` triple's arch token
 * when set (canonicalized `aarch64→arm64`, `amd64`/`x64`→`x86_64` — the same folding
 * `arch_token_canonical` already applies), else the host `teko::arch()`.
 *
 * @param m  the resolved manifest (its `[extern] target` triple, if any)
 * @return   "x86_64" | "arm64" | "unknown"
 * @since §17
 */
fn target_arch(m: Manifest): str {
    if m.target.len > 0 {
        if teko::str::contains(m.target, "aarch64") || teko::str::contains(m.target, "arm64") { return "arm64" }
        if teko::str::contains(m.target, "x86_64") || teko::str::contains(m.target, "amd64") || teko::str::contains(m.target, "x64") { return "x86_64" }
    }
    teko::arch()
}
```

---

## 3. Where the guard rides — `parser::Item.guard` (the load-bearing design choice)

The guard is stored on the **flattened `parser::Item`**, NOT on each `Decl` struct.

Rationale (law-first, no-regression):
- The prune already consumes `parser::Item`s at a SINGLE site — one uniform `item.guard` read
  replaces the `Function`-only `match` and instantly covers `type`/`const`/`extern`.
- It touches ZERO `Function`/`TypeDecl`/`ConstDecl` literals (there are many across
  codegen/merge/monomorph). Adding a field to `Function` would churn every literal in the tree
  (with_outer_doc, codegen lambda-lift, etc.). Adding it to `Item` touches only the ~6 `Item{}`
  literals in `assemble.tks`/`driver.tks`.
- The guard is a PRUNE-TIME-ONLY fact. Because prune runs before check/serialize, `Item.guard` is
  erased before `.tkb` — nothing downstream needs to learn about it.

```teko
pub type Item = struct { content: @ItemKind(); namespace: str; file: str; guard: @Pred() }
```

Population path (parser → assemble): a `Module`/`MainFile` gains a `[]@Pred()` PARALLEL to its
decl list (the codebase's established "PARALLEL to" idiom, e.g. `type_params`/`type_constraints`,
`args`/`arg_names`). `asm_module_items` zips `decls[i]` with `guards[i]` into
`Item { …; guard = guards[i] }`. Unguarded decls get `Pred { kind = PTrue { } }`.

```teko
pub type Module = struct { uses: []UseDecl; decls: []@Decl(); decl_guards: []@Pred() }
```

`decl_guards` is PARALLEL to `decls` (same length); every existing `Module { … }` literal appends
`decl_guards = teko::list::empty()` and, where a `Module` is built with decls, one `PTrue` per decl
(additive; the parser is the only real producer).

---

## 4. Crumb sequence (ordered, each independently gate-able)

Each crumb below names: the type/fn shapes to add, the existing fns it touches, its fixtures, and
its reseed flag.

### Crumb A — Pred AST + evaluator + const env  *(FIRST IMPLEMENTABLE)*
- **Add:** `PredKind`/`Pred`/`PTrue`/`PEq`/`PFlag`/`PNot`/`PAnd`/`POr` (`src/parser/ast.tks`);
  `CcEnv`, `eval_pred`, `pred_axis_eq`, `str_list_contains` (new `src/build/prune.tks`).
- **Touches:** nothing existing (pure additive island — no wiring yet).
- **Fixtures (unit, native exit-code):** a `#test`-style probe under `examples/regressions/` that
  builds Preds by hand and asserts `eval_pred`:
  - `PTrue` → true; `PEq("os","linux")` vs `CcEnv{os="linux"}` → true, vs `{os="macos"}` → false;
    `PEq("arch","arm64")` axis switch; unknown axis `PEq("cpu","x")` → false;
    `PFlag("debug")` present/absent; `PNot`/`PAnd`/`POr` truth tables incl. short-circuit;
    a composed tree `os=="linux" && (arch=="arm64" || debug)`.
  - Encode as a native binary whose exit code is a bitfield of the assertions (0 = all pass; each
    failing check ORs a distinct bit) so the harness reads a single exit int.
- **Reseed:** compiler-code additive island; no parser/prune behavior change yet. Gate = build gen1
  + self-compile **fixpoint** (byte-identical `teko.c`, trivially — no call sites changed).

### Crumb B — `Item.guard` + generalized prune (`prune_cc`) + `target_arch`
- **Add:** `Item.guard` field (§3); `Module.decl_guards`; `target_arch` (§2.4);
  `prune_cc(program, env)` replacing `prune_os`.
  ```teko
  /**
   * prune_cc — §17 conditional-compilation PRUNE: keep exactly the top-level items whose `guard`
   * predicate is satisfied by the build's constant environment, dropping the rest BEFORE macro
   * expansion / name-resolution / type-check (so a pruned item referencing an unavailable
   * per-platform symbol never reaches the checker). Generalizes `prune_os` from a `Function`-only
   * `os_guard` string to a per-`Item` `@Pred()` over os/arch/flags.
   *
   * @param program  the assembled, pre-check program
   * @param env      the build-time constant environment (`CcEnv`)
   * @return         a fresh `Program` of the surviving items, in source order
   * @since §17
   */
  fn prune_cc(program: parser::Program, env: CcEnv): parser::Program { … eval_pred(program.items[i].guard, env) … }
  ```
- **Touches:** `frontend_check` (`src/build/project.tks:470`) — replace
  `prune_os(pf.parsed, target_os(m))` with
  `prune_cc(pf.parsed, CcEnv { os = target_os(m); arch = target_arch(m); flags = build_flags(m) })`;
  `asm_module_items`/`asm_main_items` (`src/build/assemble.tks`) — set `guard` on every `Item{}`
  (default `PTrue`); `parse_module` — populate `decl_guards` (all `PTrue` for now).
  `prune_os` is retired (its single caller moves to `prune_cc`).
- **`#os`-on-function compat:** in this crumb, `#os` still ALSO sets `Function.os_guard` (unchanged),
  AND now emits `Item.guard = Pred{PEq("os", X)}`. `prune_cc` reads `Item.guard` exclusively; the
  surviving function still carries `os_guard` into `.tkb`/`merge` byte-identically. Net prune result
  for `#os` is unchanged (PEq("os",X) prunes iff `os_guard != tos`).
- **Fixtures:** the existing `#os("linux")` behavior must be preserved — a regression that a
  linux-guarded item survives on a linux target and is dropped on a `[extern] target` macos build
  (native exit codes: present→runs the guarded path; absent→the fallback). Reuse the
  `examples/probes/chan_dgram` `#os("linux")` shape as the model input.
- **Reseed:** parser + prune behavior change → fixpoint + **reseed**. Corpus impact NIL (src/ has no
  guards — see §6), so survivors are unchanged on every platform and emitted `teko.c` is
  byte-identical per platform.

### Crumb C — `#arch("…")` shortcut
- **Add:** in `parse_decl_attributes` (`src/parser/parse_decl.tks:1608`), an `else if attr ==
  "arch"` arm mirroring the `"os"` arm (`Hash Ident("arch") LParen Str RParen`), producing an
  `arch` guard. Extend `ParsedAttributes` (`src/parser/result.tks:43`) with a
  `guard: @Pred()` field that the attribute arms build:
  `#os("linux")` → `PEq("os","linux")`, `#arch("arm64")` → `PEq("arch","arm64")`. When both `#os`
  and `#arch` precede one item, AND them (`PAnd`).
- **Touches:** `parse_decl_attributes`; `ParsedAttributes`; the callers that read
  `attrs.os_guard`/`attrs.guard` (`parse_fn_decl_with_attrs` `:2066`, and the region/decl loop in
  `parse_module` that records `decl_guards`).
- **Spec typo caught:** the sealed spec writes `#arch("arm64") ≡ #if(arch == "arch64")`; `"arch64"`
  is a typo. The canonical arch value is `"arm64"` (`tk_rt_arch`, `src/runtime/teko_rt.c:4421`).
  Implement against `"arm64"`. REPORTED, not silently reinterpreted.
- **Fixtures:** `#arch("arm64")`/`#arch("x86_64")` item kept vs dropped under a
  cross `[extern] target` triple; a fn carrying BOTH `#os("linux")` and `#arch("arm64")` survives
  only on linux-arm64.
- **Reseed:** parser change → fixpoint + reseed. Corpus impact NIL.

### Crumb D — widen `#os`/`#arch` to ALL top-level items
- **Change:** `reject_fn_only_attrs_on_type` (`src/parser/parse_decl.tks:2083-2089`) must STOP
  rejecting an os/arch guard on a `type`/`flags`/`const` (keep rejecting `#test`/`#arena_size`,
  which stay function-only). The guard flows to the item's `Item.guard` via `decl_guards`, so
  `parse_type_decl_with_attrs` (`:2072`)/`parse_flags_decl_with_attrs` (`:2078`)/the const path
  (`:2041-2045`) must thread `attrs.guard` into the module's `decl_guards` slot instead of dropping
  it. `extern fn` (a `Function` with `is_extern`) and `extern type` (a `TypeDecl` with `ExternBody`,
  `src/parser/ast.tks:786`) are already `Function`/`TypeDecl`, so they are covered — there is NO
  separate "extern block" node (confirmed: `extern` is a per-decl modifier, not a region).
- **Touches:** `reject_fn_only_attrs_on_type`; `parse_type_decl_with_attrs`;
  `parse_flags_decl_with_attrs`; the const arm at `parse_decl.tks:2041`.
- **Fixtures:** an `extern type` guarded `#os("windows")` referencing a Windows-only opaque handle
  DROPS on linux without a checker error (the ordering guarantee — crumb-5); a `const` guarded
  `#arch("arm64")` present only on arm64; a guarded `type` used by a guarded `fn` (both prune
  together on the off-target).
- **Reseed:** parser change → fixpoint + reseed. Corpus impact NIL (src/ still has no guarded
  types/consts).

### Crumb E — `#if`/`#elseif`/`#else`/`#endif` regions + composite predicate parser
- **Add (predicate parser)** — a small recursive-descent parser over the token stream inside a
  `#if(<pred>)` / `#elseif(<pred>)` paren group, producing a `@Pred()`. Grammar (precedence low→high):
  ```
  pred    := or
  or      := and   ( "||" and )*        → POr
  and     := unary ( "&&" unary )*      → PAnd
  unary   := "!" unary | primary        → PNot
  primary := "(" pred ")"
           | ("os" | "arch") "==" STRING   → PEq(axis, value)
           | IDENT                          → PFlag(name)
  ```
  New fns (full Javadoc): `parse_pred`, `parse_pred_or`, `parse_pred_and`, `parse_pred_unary`,
  `parse_pred_primary`, each `(tokens, pos) -> Parsed<@Pred()> | error`, in
  `src/parser/parse_cc.tks` (new file, `teko::parser`).
- **Add (region parser)** — in `parse_module` (and `parse_main_file` if main files carry top-level
  decls — see risk R3), a decl-list-level recognizer: on `#if(<pred>)`, collect arms until
  `#endif`. Each arm is a run of decls (each itself parsed by the ordinary `parse_decl`, so a decl
  INSIDE a region may still carry its own `#os`/`#arch`). The parser does NOT evaluate os/arch (it
  has no target); it COMPOSES the arm predicate structurally and stamps it into `decl_guards`:
  - arm 1 (`#if P1`): each decl guard = `P1`
  - arm k (`#elseif Pk`): `Pk && !P1 && … && !P(k-1)`  (first-true-wins, encoded as AND-of-NOTs)
  - `#else` arm: `!P1 && … && !Pn`
  - a decl's own inner `#os`/`#arch` guard G: `arm_guard && G`
  The prune then keeps at most one arm's decls — whichever arm predicate `eval_pred`s true first,
  structurally. The `#os`/`#arch` shortcuts are exactly the single-item sugar (`#os("linux")` ≡
  the attribute form of `#if(os == "linux")` over one decl), so they share the `@Pred()` producer.
- **Add helper:** `pred_and(a, b)` / `pred_not(a)` constructors (full Javadoc) to keep the
  composition readable and boxed correctly.
- **Touches:** `parse_module` (region loop); `Module.decl_guards` population; reuses crumb-A/B/C/D.
- **Fixtures:**
  - `#if(os == "linux") … #elseif(os == "macos") … #else … #endif` — exactly one arm's fn survives
    per target (linux/macos/`unknown`→else); native exit code identifies which arm ran.
  - composite: `#if(os == "linux" && arch == "x86_64") … #endif` present only on that pair.
  - `!`/`||`/grouping: `#if(!(os == "windows")) … #endif` dropped only on windows.
  - a region containing a `type` + a `const` + a `fn` (widen interaction): all three prune together.
  - a decl with an inner `#os` NESTED in a region arm (AND composition).
  - malformed: unbalanced `#if` without `#endif`, `#elseif` after `#else`, empty pred `#if()` →
    honest parse errors (exit-code distinct).
- **Reseed:** parser change → fixpoint + reseed. Corpus impact NIL.

### Crumb F — build flags in the const env  *(DESIGN-AHEAD; may be blocked on flag-source decision)*
- **Blocked-on:** the SOURCE of build flags (manifest `[build] flags = […]` vs CLI `--flag=…`) is a
  §17-adjacent decision not pinned by the sealed spec. Do NOT invent it.
- **Design-ahead (compiles today):** `CcEnv.flags` already exists (crumb A). Wire `build_flags(m):
  []str` as an **honest-stop** returning `teko::list::empty()` (no flags) with a Javadoc
  `@since`/TODO naming the pending flag-source decision. `PFlag` then always evaluates false until
  the source lands — a conservative, sound default (a flag nobody sets prunes its `#if` off). Every
  other crumb (os/arch/regions) ships fully without it.
- **Resumes in minutes** when the owner pins the flag source: `build_flags` reads it; no other code
  changes (the `PFlag`/`CcEnv.flags`/`eval_pred` contract is already in place).

### Crumb sequence summary
```
A (Pred+eval, island)  →  B (Item.guard + prune_cc + target_arch)  →  C (#arch)
   →  D (widen to all items)  →  E (#if regions + composite parser)  →  F (flags, design-ahead)
```
Dependencies: B needs A; C needs B; D needs C; E needs B+C+D; F is parallel-safe after A (its
consumer `build_flags` is wired in B).

**First implementable crumb: A** — the Pred AST + `eval_pred` + `CcEnv`, a pure additive island
with a self-contained truth-table fixture, gate-able with no pipeline change.

---

## 5. Ritual / reseed points

- **Ritual (full gate) after each of B, C, D, E** — each mutates the PARSER and/or the PRUNE, i.e.
  the compiler binary. Law = fixpoint + reseed: build gen1, self-compile to a fixpoint (byte-
  identical `teko.c`), reseed the bootstrap binary. Crumb A is compiler code too, so it also gates,
  but as an inert island the fixpoint is trivial.
- **Bootstrap-seed sequencing:** every new construct (union `Pred`, struct field `Item.guard`,
  `Module.decl_guards`, the predicate/region parser) is ORDINARY Teko already in the seed's language
  (unions, struct fields, recursive `@X()` boxing, recursive-descent). The corpus does NOT adopt any
  `#if`/`#arch`/`#os` SYNTAX in `src/` (§6), so no "corpus uses a feature not yet in seed" hazard
  arises — the sequence is safe as ordered.

---

## 6. Does the compiler's OWN corpus use `#os`? — reseed impact (grounded)

**No.** Grep for the `#os(`/`#arch(`/`#if(` ATTRIBUTE application across `*.tks`:
- The ONLY `.tks` applying `#os(` is `examples/probes/chan_dgram/src/chan_dgram.tks`
  (lines 171,182,198,213,227,242,251,262,272) — a standalone PROBE, not part of the self-host
  compiler build. `src/**` has ZERO `#os`/`#arch`/`#if` applications (every `#os` hit in `src/` is a
  doc-comment or the machinery itself: `parse_decl.tks`, `ast.tks`, `project.tks`).

**Consequence for reseed (byte-identity):**
- Because `src/` has no guarded items, EVERY compiler item's `guard` is `PTrue`, so `prune_cc` keeps
  the entire program on EVERY platform → the set of items handed to the checker/codegen is
  unchanged → the emitted `teko.c` is **byte-identical on a given platform** before and after §17.
  The reseed is a normal fixpoint with no per-platform divergence.
- **Cross-platform seed subtlety (flagged, not yet live):** the released `teko.c` seed is emitted on
  ONE platform. TODAY the prune is a no-op over `src/`, so the seed is platform-independent w.r.t.
  §17. IF the compiler corpus LATER adopts `#os`/`#arch` (a plausible future — `src/win32_compat.h`
  hints at per-OS compiler code, and §10-(c)'s reactor will want per-`#os` reactor backends), THEN
  the emitted `teko.c` would differ per platform (different survivors), and the checked-in seed would
  only reproduce on its emit platform. When that day comes, the reseed law needs a per-platform seed
  or a canonical emit host. This is NOT introduced by §17 itself; §17 only makes it POSSIBLE. Report
  up; do not act.

---

## 7. Risks + law tensions (with recommended resolutions)

- **R1 — `Function.os_guard` vs `Item.guard` dual-track (no-regression).** `os_guard` flows into
  `.tkb` (tkb_write:546) and `merge` dedup (merge:650). *Resolution:* KEEP `os_guard` set by `#os`
  on functions (frozen surface, byte-identical `.tkb`/merge) AND drive the prune from `Item.guard`.
  `os_guard` becomes prune-inert but ABI-live; `#arch`/`#if` and non-function items use `Item.guard`
  only. No `.tkb`/merge behavior changes. (Recommended; passes all laws — no frozen-C twin touched,
  additive-inert.)
- **R2 — `Item.guard` must never reach `.tkb`.** *Resolution:* it can't — the prune runs on
  `parser::Program` at `project.tks:470`, strictly before `checked_program_of`
  (`:480`)/serialization (`.tkb` serializes the checked `TProgram`, not `parser::Item`). Verified by
  the pipeline order. Add a Javadoc invariant on `Item.guard` stating "prune-time only; erased before
  check".
- **R3 — main.tks top-level items.** `asm_main_items` (`assemble.tks:37-52`) flattens only `uses` +
  loose `body` statements; module files (`asm_module_items`) are where `fn`/`type`/`const`/`extern`
  live. §17 targets top-level ITEMS, which are a module-file concept. *Resolution:* scope
  `decl_guards`/regions to `Module` (module files). If `main.tks` is later allowed top-level guarded
  decls, mirror the parallel-array onto `MainFile` — but that is out of the sealed §17 surface;
  do not widen speculatively. (No tension; flagged for the implementer.)
- **R4 — spec typo `arch == "arch64"`.** *Resolution:* implement `"arm64"` (canonical `tk_rt_arch`).
  Reported here; not a HALT.
- **R5 — the predicate evaluator vs the `comptime` law.** The evaluator is boolean-over-constants,
  returns `bool`, computes no value, generates no code, and lives in the prune (`src/build/prune.tks`),
  entirely separate from the §14 `comptime` fold. This matches the owner's sealed ruling ("the prune
  is the mechanism of this family; it does NOT reopen §14"). No tension.

No genuine unresolved law tension remains — **no HALT**. Every decision above is resolvable law-first
and no-regression; the plan delivers the whole §17 surface (composite `#if`/`#elseif`/`#else`/`#endif`
+ `#os`/`#arch` shortcuts, widened to all top-level items, pruned before type-check).

---

## 8. Deliverable pointers

- This doc: `/home/user/teko-lang/docs/design/plano-s17-pragmas-crumbs.md`
- Existing machinery to extend: `src/parser/parse_decl.tks:1593-1626` (attrs),
  `src/parser/ast.tks:626` (`os_guard`), `src/build/project.tks:106-132` (`target_os`/`prune_os`),
  `src/build/project.tks:468-483` (`frontend_check` pipeline), `src/build/assemble.tks:37-76`
  (flatten), `src/runtime/teko_rt.c:4393-4428` (`tk_rt_os`/`tk_rt_arch` — FROZEN C, read-only).
- New files to create: `src/build/prune.tks` (env + `eval_pred` + `prune_cc` + `target_arch`),
  `src/parser/parse_cc.tks` (predicate + region parser).
