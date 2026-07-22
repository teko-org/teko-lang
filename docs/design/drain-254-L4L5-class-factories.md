# Drain — #254 L4/L5: generic CLASS factories + self-construct inside a generic template

**Status:** DESIGN-AHEAD (architect). No product code changed. Verified at file:line against
`fix/issue-254` (worktree `agent-ada3d9951db03defe`, tip `c45c2c5`).
**Parent design:** `docs/design/drain-onda3-subcluster-A.md` §A/#254 (L4/L5), which the implementer
shipped through L3 (struct methods) and deferred at L4/L5 as a genuine design gap.
**Unblocks:** #163 (collections `Map`/`List`/`Set`/`BTree` are generic CLASSES with factories like
`Map::new()` / `Map<K,V>::new()`).
**Rule:** every snippet is full-Javadoc, `.tks`-only. C twins FROZEN (only `teko_rt.{c,h}` maintained —
none needed here). Ritual per crumb = full gate (gen1 native `#test` + `./bin/teko test .` VM +
FIXPOINT gen1==gen2 byte-identical + `diff_vm_native.sh` + `TEKO_MEM_PARANOID=1` + `//`-audit).

---

## 0. What L1–L3 already shipped (the base this builds on)

Verified in the worktree. The struct-method monomorphization is complete and correct **for methods
whose bodies read `T` / take `T` / return `T`** — never for a body that CONSTRUCTS the owning generic
type. Load-bearing pieces this design reuses unchanged:

- **`method_type_param_table`** (`resolve.tks:631`) — prepends the owner's type-params so a body
  referencing `T` resolves. Wired at `collect.tks:99` + `typer.tks:2976`.
- **Layer-2 body-subst** (`resolve.tks` StructBody/ClassBody arms) — keeps + substitutes methods on
  the stamped instance decl.
- **`register_instance_methods`** (`collect.tks:206`) — registers each stamped instance's methods as
  `Inst::method` callables, keyed by the bare instance name (`Box__g__i64`). Called at
  `typer.tks:3245` + `:3334`.
- **`stamp_all_instance_methods` / `stamp_instance_methods` / `stamp_one_instance_method`**
  (`monomorph.tks:1002 / 968 / 899`) — stamps each carried method as a free `TFunction` keyed by the
  instance name, its typed template body rewritten through **`instance_method_subst`**
  (`monomorph.tks:927`).
- **`instance_method_subst`** (`monomorph.tks:927`) — the instance's `{T → concrete}` Subst
  **EXTENDED** with `canon → Named{inst}` and `base → Named{inst}` remaps (via
  `subst_add_type_remap`, `monomorph.tks:950`), so a `self`-typed receiver / `-> Box<T>` return
  concretizes. **This is the exact hook L4/L5 needs — it is already 90 % of the answer.**

The shipped fixture `examples/regressions/generic_struct_method/` proves L1–L3; it deliberately has NO
self-construct (methods only `get`/`echo`).

---

## 1. Root-cause of each gap (grepped, pinned)

### Gap 1 — self-construct inside a generic template body: the PHANTOM instance

**The failing chain, in order:**

1. `type_struct_methods` (`typer.tks:3121`) types EVERY method body — including the abstract
   template's — via `type_method` (`typer.tks:3169`) against the template's own name (`td.name` =
   `Box`), with `T` registered as a type-param in the table.
2. A self-construct `Box { value = self.value }` in that body reaches `type_struct_lit`
   (`typer.tks:1522`). Since `Box` is generic (`decl.type_params.len > 0`, `typer.tks:1569`) it needs
   an EXPECTED `Named` naming a concrete `Box<…>` instance to retarget to (`typer.tks:1570-1573`).
   In expr position `type_return` supplies none → **error at `typer.tks:1572`**
   ("cannot infer the type arguments of `Box` here — annotate it").
3. Even WITH an expected type, the only candidate the enclosing return type `-> Box<T>` offers is the
   **phantom** `Named{"Box__g__T"}`: `resolve_type`→`resolve_generic_inst` (`resolve.tks:1286`)
   builds `Named{generic_inst_name("Box",[Named{T}])}` = `"Box__g__T"` (`resolve.tks` tail:
   `Named { name = generic_inst_name(name, argtypes) }`). But `instantiate_types` (`resolve.tks`
   ~1420) **SKIPS** an arg that is an unresolved type-param (its own comment: *"an unresolved arg
   … means this site concretizes only when the OUTER generic is instantiated — skip it here"*), so
   **`Box__g__T` is never stamped**. `type_struct_lit`'s instance lookup then fails at
   `typer.tks:1577-1579` ("internal: generic instance `Box__g__T` was not stamped").

   > This is exactly the "PHANTOM `Box__g__T`" the implementer named. Threading `Env.expected_ret`
   > (the reverted L4) fed the phantom as the expected type and hit failure (3) — a dead end that
   > ALSO required a wide `Env {}`-literal churn. **We do not revive it.**

4. Why the mono pass CANNOT fix it after the fact: a `TStructInit` carries **no struct name**
   (`tast.tks:44` — `field_names` + `field_vals` only); the struct identity lives ENTIRELY on the
   node's `TExpr.type` (`Named`). `emit_struct_init_literal` (`codegen.tks:2997`) and
   `emit_struct_init_framed`'s class check (`codegen.tks:2959`) both read `e.type` as the sole source
   of the target name. `mono_texpr` already substitutes it: `nt = subst_type(e.type, s)`
   (`monomorph.tks:433`), and the `TStructInit` arm (`monomorph.tks:547-558`) rebuilds the node with
   `type = nt`. So **IF the body typed to `TStructInit{ type = Named{"Box__g__T"} }`, and the Subst
   mapped `"Box__g__T" → Named{"Box__g__i64"}`, the retarget would be automatic** — no `expected_ret`,
   no new codegen. Two obstacles: (a) the body cannot TYPE to that phantom node today (failure 2/3);
   (b) `instance_method_subst` maps `base`/`canon` but NOT the phantom spelling `"Box__g__T"`.

**Conclusion — the fix is two narrow moves, both on the existing hook:** let the template body TYPE
to a phantom self-init node, and teach `instance_method_subst` to remap that phantom spelling. No
`Env` field.

### Gap 2 — `Type<Args>::static_fn(...)` does not parse

`parse_atom` (`parse_expr.tks:325`) parses the leading path, then:
- `LParen` → a `Call` (`parse_expr.tks:327`). `Cell::make()` works because `parse_path`
  (`parse_path.tks:5`) already consumed `Cell::make` across `::`.
- `Lt` → speculatively `parse_type_args` (`parse_expr.tks:334`), but **commits ONLY when a `{`
  follows** the closed list (`parse_expr.tks:337`). For `Cell<i64>::make()` a `::` follows the `>`,
  not `{`, so the `if` is false, the `<…>` result is discarded, and control falls to
  `parse_expr.tks:348` — the single-segment path `Cell` becomes `Var{name="Cell"}` with
  `next = pp.next` (still pointing AT the `<`). The caller then sees `<i64>::make()` unconsumed →
  parse error. **Verified against the released seed too** (the commit is only additive there).

`parser::Call` (`ast.tks:25`) carries NO `type_args`. But `parser::Call` is **never serialized** —
the `.tkb` codec persists the TYPED `checker::TCall` (`emit/tkb_frame.tks:159`), not the raw AST. So
adding `type_args: []TypeExpr` to `parser::Call` is a NARROW, in-process change: the AST decl, the one
parser construction (`parse_expr.tks:329`), and one synth site (`synth.tks:91`).

---

## 2. Design — self-type-construction retargeting (the load-bearing part)

### 2a. Make the abstract template body TYPE to a phantom self-init node

The template body must type WITHOUT a concrete instance in the table. Add a single well-scoped escape
hatch to `type_struct_lit` (`typer.tks:1569`): **when the generic base being constructed IS the
enclosing method's own owner type (`env.owner_type`), and no concrete instance is available, type the
init against the ABSTRACT template body directly, tagging the node with the phantom `Named{base__g__…paramspelling}`** — the exact name `resolve_type(-> Box<T>)` produces. This keeps the
node self-consistent with the return type and every `let x: Box<T>` local in the body, and it is what
the mono Subst will remap.

Concretely, three touches inside `type_struct_lit`, all gated so the corpus is byte-identical:

```teko
/**
 * Is this struct/class literal a SELF-CONSTRUCT inside its own generic template — i.e. the base
 * being constructed equals the enclosing method's owner type, AND that owner is generic (so no
 * concrete instance is stamped while typing the abstract template)? Only then do we type against the
 * abstract template body and tag the node with the phantom `Base__g__<param>` instance name that the
 * mono pass will later remap to the concrete stamp (#254 L4/L5). A non-generic owner, or a
 * construction of a DIFFERENT generic type, is NOT a self-construct → the ordinary
 * expected-instance path (unchanged) applies.
 *
 * @param base_name    the bare base of the constructed type (`Box`)
 * @param owner_type   the enclosing method's owner (`env.owner_type`, canonical or "")
 * @param decl_tparams the constructed base's type-params (generic iff non-empty)
 * @return             true iff this is a self-construct of the enclosing generic template
 * @since onda-3 (#254 L4/L5)
 */
fn is_self_generic_construct(base_name: str, owner_type: str, decl_tparams: []str) -> bool {
    if decl_tparams.len == 0 { return false }
    if owner_type == "" { return false }
    teko::runtime::str_eq(name_last_segment(owner_type), base_name)
}
```

The phantom instance name is built from the OWNER's own type-params (the template's `T`, in order),
so it matches the `-> Box<T>` return spelling byte-for-byte:

```teko
/**
 * The PHANTOM generic-instance name for a self-construct inside a generic template: the enclosing
 * type's OWN type-params, in order, mangled exactly as `resolve_generic_inst` mangles `-> Box<T>`
 * (`Box__g__T`). The template body's self-init, its `-> Box<T>` return, and any `Box<T>` local all
 * carry this SAME spelling, so the mono pass's single `phantom → concrete` remap retargets them
 * together (#254 L4/L5). Reuses `generic_inst_name` over the owner's params-as-Named so the spelling
 * cannot drift from the resolver's.
 *
 * @param base_name    the bare base (`Box`)
 * @param owner_tparams the enclosing type's type-param names (`["T"]`)
 * @return             the phantom instance name (`Box__g__T`)
 * @since onda-3 (#254 L4/L5)
 */
fn phantom_self_inst_name(base_name: str, owner_tparams: []str) -> str {
    mut args: []Type = teko::list::empty()
    mut i = 0
    loop { if i >= owner_tparams.len { break }; args = teko::list::push(args, Named { name = owner_tparams[i] }); i++ }
    generic_inst_name(base_name, args)
}
```

Wiring inside `type_struct_lit`, at the generic branch (`typer.tks:1569`), BEFORE the "cannot infer"
error at `:1572`: when `is_self_generic_construct(base_name, env.owner_type, decl.type_params)`, do
NOT demand an expected concrete instance — instead take `sb_fields` from the ABSTRACT template decl
(the one already in `decl`, whose fields are `T`-typed), type each field value against its
`subst`-free `T` annotation (the field types resolve via the owner's type-param table already in
scope), and set the result node's `type = Named{ phantom_self_inst_name(base_name, decl.type_params) }`.
The class-literal legality guard (`typer.tks:1598`) already permits this: `env.owner_type`'s last
segment equals `name`'s last segment for a self-construct, so a CLASS self-factory passes the
invariant-safe-construction check unchanged.

> **Note — the enclosing owner's type-params must be in the table while typing the body.** They
> already are: `method_type_param_table` (L1) registered them, and `type_method` types the body under
> that table. So `value: T` resolves; no extra plumbing.

This produces `TStructInit{ field_vals = [ … ]; type = Named{"Box__g__T"} }` — a well-typed phantom
node. **The whole L4 `Env.expected_ret` thread is deleted from the plan.** The retarget rides
`type`, not a return-context.

### 2b. Teach `instance_method_subst` to remap the phantom spelling

`instance_method_subst` (`monomorph.tks:927`) currently adds `canon → inst` and `base → inst`. Add
ONE more remap: `phantom → inst`, where `phantom = phantom_self_inst_name(base, template.type_params)`
(built from the SAME `generic_inst_name` so the spelling matches 2a). Then
`subst_type(Named{"Box__g__T"}, s)` (`resolve.tks:1039`) resolves to `Named{"Box__g__i64"}` and the
`TStructInit` arm at `monomorph.tks:547-558` rewrites `type = nt = Named{"Box__g__i64"}`
automatically — codegen/VM emit the concrete instance.

```teko
/**
 * The instance's method Subst (`instance_type_subst` + base/canon → instance remaps), FURTHER extended
 * with the PHANTOM self-construct spelling `Base__g__<owner-params>` → the concrete instance — so a
 * `Box { … }` self-init inside `Box::dup` (typed against the abstract template to `Named{Box__g__T}`)
 * retargets to `Box__g__i64` in the stamped body (#254 L4/L5). The phantom is built from the
 * template's OWN type-params via the same `generic_inst_name`, so it matches the spelling
 * `type_struct_lit` tagged the self-init with. A no-op when the template has no type-params (never a
 * self-construct) → byte-identical to the L3 Subst for the existing corpus.
 *
 * @param template    the generic template decl
 * @param template_ns the template's declaring namespace
 * @param inst        the stamped instance decl
 * @param table       the folded type table
 * @return            the L3 Subst extended with the phantom → instance remap
 * @since onda-3 (#254 L4/L5)
 */
fn instance_method_subst_l5(template: parser::TypeDecl, template_ns: str, inst: parser::TypeDecl, table: TypeTable) -> Subst {
    let s = instance_method_subst(template, template_ns, inst, table)
    if template.type_params.len == 0 { return s }
    let base = g_instance_base(inst.name)
    let phantom = phantom_self_inst_name(base, template.type_params)
    if teko::runtime::str_eq(phantom, inst.name) { return s }
    subst_add_type_remap(s, phantom, Named { name = inst.name })
}
```

Swap the call at `stamp_instance_methods` (`monomorph.tks:971`) from `instance_method_subst` to
`instance_method_subst_l5`. (Alternatively fold the extra remap directly into `instance_method_subst`
— same effect; a separate fn keeps the L3 helper untouched and the diff auditable.)

### 2c. Locals and params of the owning generic type inside the body

A `let tmp: Box<T> = …` local or a `Box<T>`-typed param inside the method body types to
`Named{"Box__g__T"}` (same phantom) at bind time, and `mono_texpr`/`subst_type` rewrite EVERY
`TExpr.type` through the Subst (`monomorph.tks:433` runs on every node). So the same phantom remap
(2b) fixes locals and params with **zero extra code** — they ride the one remap. The syntactic param
annotation is separately handled by `subst_typeexpr` in `stamp_one_instance_method`
(`monomorph.tks:904`), whose `subst_find_inst` reads the SAME Subst names/types → the phantom is
covered there too (the param annotation `Box<T>` is a `NamedType` with args, but the receiver/`self`
case is the single-segment path already handled; a `Box<T>` param annotation is covered because 2b's
remap is registered in `names`/`types`, which `subst_type` on the resolved param type consumes — the
resolved `TFunction.return_type`/param types are what codegen reads for the C signature).

> **Edge honesty:** a self-construct nested inside a DIFFERENT generic (`Wrap<Box<T>>` built inside
> `Box::foo`) is out of this gap's scope — it needs the outer generic's own instantiation to supply
> the concrete arg, which the existing transitive-inst discovery already handles once the phantom
> remap lands. No new work; if a corpus case ever exercises it, it flows through the same Subst.

### 2d. Static factory (`Cell::make(x)` / `Cell<i64>::make()`) — L5

A static factory is just a method whose body self-constructs and whose return type is the owner
generic — L5 is the union of 2a+2b plus the parser crumb (§3). No receiver, so no `self` remap needed;
the `base → inst` + `phantom → inst` remaps carry the `Cell { v = x }` self-init and the `-> Cell<T>`
return. Arena-per-object ref semantics survive because the stamped factory reuses the SAME
class-lowering (`emit_struct_init_framed`'s class branch keys off `e.type = Named{"Cell__g__i64"}`,
now concrete) — **no new codegen** (verified: `codegen.tks:2959` reads only `e.type`).

---

## 3. Parser crumb — `Type<Args>::static_fn(...)`

Add `type_args: []TypeExpr` to `parser::Call` (`ast.tks:25`); default `teko::list::empty()`
everywhere. In `parse_atom` (`parse_expr.tks:334-343`), after `parse_type_args` closes with
`pending_gt == 0`, add a `::`-continuation branch BEFORE the `{` branch:

```teko
// (#254 L5) `Type<Args>::static_fn(...)` — type-args BEFORE `::`. After the closed `<…>`, a `::`
// (not a `{`) means a static/factory call on a generic instance: re-enter path parsing from the
// `::` to append the factory segment(s), carry the type-args on the Call so the checker retargets
// the callee owner to the mangled instance (`Cell__g__i64::make`). No `{` → not a struct literal.
if allow_struct && is_kind_at(tokens, pp.next, lexer::TokenKind::Lt) {
    match parse_type_args(tokens, pp.next) {
        ParsedTypeArgs as ta => {
            if ta.pending_gt == 0 && is_kind_at(tokens, ta.next, lexer::TokenKind::LBrace) {
                return parse_struct_lit(tokens, ta.next, pp.node, ta.args)
            }
            if ta.pending_gt == 0 && is_kind_at(tokens, ta.next, lexer::TokenKind::ColonColon) {
                let tail = match parse_path(tokens, ta.next + 1) { Parsed<Path> as x => x; error as e => return e }
                let full = path_concat(pp.node, tail.node)   // Cell :: (make) → Cell::make
                if !is_kind_at(tokens, tail.next, lexer::TokenKind::LParen) {
                    return err_at(tokens, tail.next, "expected '(' — a generic static call is `Type<Args>::fn(...)`")
                }
                let ca = match parse_call_args(tokens, tail.next) { ParsedCallArgs as x => x; error as e => return e }
                return Parsed<Expr> { node = Expr { kind = Call { callee = full; args = ca.args; arg_names = ca.arg_names; type_args = ta.args }; line = tokens[pos].line; col = tokens[pos].col }; next = ca.next }
            }
        }
        error => { }   // backtrack: `a < b` or a mid-close → comparison
    }
}
```

`path_concat(Cell, make)` yields the callee `Cell::make`; `type_args = [i64]`. The checker (§3-check)
maps it to the stamped `Cell__g__i64::make`:

```teko
/**
 * Retarget a static-call callee that carries construction-site type-args (`Cell<i64>::make(…)`) to the
 * mangled generic-instance owner (`Cell__g__i64::make`), so `lookup_call` resolves the stamped factory
 * `register_instance_methods` registered (#254 L5). Resolves the type-args to concrete types, mangles
 * the base (the callee's SECOND-TO-LAST segment) via `generic_inst_name`, and rewrites that segment.
 * A call with no type-args passes through unchanged → byte-identical for every existing call.
 *
 * @param c      the parsed static call (carrying type_args)
 * @param table  the folded type table (arg resolution)
 * @param ref_ns the call's enclosing namespace
 * @return       the callee path with the owner segment mangled to the instance, or `c.callee` verbatim
 * @throws       when a type-arg fails to resolve
 * @since onda-3 (#254 L5)
 */
fn retarget_generic_static_callee(c: parser::Call, table: TypeTable, ref_ns: str) -> parser::Path | error {
    if c.type_args.len == 0 || c.callee.segments.len < 2 { return c.callee }
    let owner_idx = c.callee.segments.len - 2
    let base = c.callee.segments[owner_idx].name
    let nt = parser::NamedType { path = single_seg_path(base); args = c.type_args }
    let inst = match resolve_type(nt, table, ref_ns) { Named as n => n.name; Type => return c.callee; error as e => return e }
    rewrite_segment_at(c.callee, owner_idx, inst)
}
```

Call it at the head of `type_call` (`typer.tks:878`) to rewrite `c.callee` before `lookup_call`.
Also extend construction-site discovery: in `collect_expr_insts` (`resolve.tks:1637`) the
`parser::Call` arm must `collect_site_args(single_seg_path(base), call.type_args, out)` so
`Cell<i64>::make()` STAMPS `Cell__g__i64` (mirrors the `StructLit` arm at `resolve.tks:1626`).

Helpers to grep-or-add: `path_concat`, `single_seg_path`, `rewrite_segment_at` (a positional twin of
`rewrite_last_segment` at `monomorph.tks:493`).

> **`Cell::make()` (no type-args) already parses** — it is a plain `Call` with a 2-segment path, and
> `register_instance_methods` registered `Cell__g__i64::make` only under the mangled owner. For the
> bare form to resolve, the concrete instance must be inferable elsewhere (an annotation on the
> `let`, or the args). The type-arg-bearing form `Cell<i64>::make()` is the explicit path #163 needs;
> the bare form remains inference-driven (out of scope, no regression).

---

## 4. Ordered crumb sequence + regression fixtures

Each crumb is independently gate-able; the guard everywhere is: **corpus carries zero generic
self-constructs / generic static factories → every arm is a no-op → gen1==gen2 byte-identical.**

**Crumb L4.1 — self-construct types to a phantom node (checker).**
Add `is_self_generic_construct` + `phantom_self_inst_name` (typer/resolve); wire the self-construct
branch into `type_struct_lit` (`typer.tks:1569`). Gate: no expected-instance demanded ONLY when
`is_self_generic_construct`. **Fixture:** `generic_method_self_construct` (struct) — see below;
type-checks after, still errors before.

**Crumb L4.2 — phantom remap in the mono Subst (monomorph).**
Add `instance_method_subst_l5`; swap the call at `stamp_instance_methods` (`monomorph.tks:971`).
Gate: extra remap only when `template.type_params.len > 0`. **Fixture:** same struct fixture now
RUNS (both engines) → exit code matches.

**Crumb L5.1 — parser: `Type<Args>::fn(...)`.**
Add `parser::Call.type_args` (ast + one parser site + `synth.tks:91`); add the `::`-continuation
branch (`parse_expr.tks`). Gate: default `[]` → every existing `Call` byte-identical.

**Crumb L5.2 — checker: retarget + discover the static-factory instance.**
Add `retarget_generic_static_callee` (call at `type_call` head, `typer.tks:878`); extend the
`parser::Call` arm of `collect_expr_insts` (`resolve.tks:1637`) with `collect_site_args`. Gate:
no-op when `type_args` empty. **Fixture:** `generic_class_factory` runs both engines.

**Crumb L5.3 — corpus `#test`s.**
Add generic-class-factory + self-construct `#test`s to `checker/generics_test.tkt` (VM covers the
mono method path both engines). Gate: full VM gate.

### Fixtures (VM==native unless noted)

- **`examples/regressions/generic_method_self_construct/`** (proves L4). Struct:
  `type Box<T> = struct { value: T; pub fn dup(self) -> Box<T> { Box { value = self.value } };
  pub fn get(self) -> T { self.value } }`. Program:
  `Box<i64>{value=21}.dup().get()` + a SECOND instance `Box<u8>{value=1}.dup().get() to i64`
  → `exit(21 + 21 + …)`; pick constants summing to a distinct exit (e.g. 21+21 → **exit 42**,
  with the `u8` instance proving per-instance retarget, not a shared phantom). Fails to type-check
  today; passes both engines after L4.
- **`examples/regressions/generic_class_factory/`** (proves L5 + L4). Class:
  `type Cell<T> = class { pub v: T; pub fn make(x: T) -> Cell<T> { Cell { v = x } };
  pub fn read(self) -> T { self.v } }`. Program:
  `Cell<i64>::make(7).read()` + `Cell<str>::make("ab").read().len` → **exit (7+2)=9** (two distinct
  instantiations prove the factory stamps per instance; the `str` case proves the parser type-arg
  path + arena-per-object ref semantics on a non-scalar). cc-rejects / VM-panics today; both pass
  after L5. **Second file** with the bare `Cell::make(...)` under a `let x: Cell<i64> = Cell::make(7)`
  annotation is OPTIONAL (bare-form inference is out of scope — include only as an xfail note).
- **`checker/generics_test.tkt`** — VM `#test`s mirroring both above (self-returning method + static
  factory, ≥2 instances) so the mono method path is exercised in the VM gate.

---

## 5. Fixpoint / law notes + scope

- **Fixpoint (the single most important guard):** the compiler's own source has ZERO generic
  self-constructs and ZERO generic static factories. Every new arm is gated on
  `template.type_params.len > 0` / `is_self_generic_construct` / non-empty `type_args`, all FALSE on
  the corpus → the mono append and the typed program stay byte-identical → **gen1==gen2 byte-identical
  MUST hold at each crumb.** Verify at every crumb, not only at the end (the L1–L3 lesson). The
  `any_generic` no-op guard (`monomorph.tks:739`) is unaffected.
- **Laws:** all crumbs are `.tks`-only (Teko-only law); C twins FROZEN, none touched. Full-Javadoc on
  every new fn (snippets above are copy-verbatim). Flattened (early-return guards, no Hadouken). No
  `Env` field is added — the reverted L4 wide-churn is AVOIDED (the retarget rides `TExpr.type` +
  the existing `instance_method_subst` hook, per the implementer's "prefer a narrower thread"
  constraint). This is strictly narrower than the reverted approach and needs no `Env {}`-literal
  sweep.
- **`parser::Call.type_args` is the only new data field, and it is transient** (parser AST, never
  serialized — `.tkb` persists `checker::TCall`), so it adds no codec/`.tkh` surface. That is the
  minimum surface for a call to carry construction-site type-args, symmetric with
  `StructLit.type_args` (`ast.tks:42`).
- **Does this COMPLETE #254?** YES — L1–L3 (shipped) + L4 (self-construct) + L5 (class factory +
  parser) is the whole L1–L5 proposal of the §A/#254 crumbs. No residual sub-scope remains for #254
  itself. The one honestly-deferred edge (a self-construct nested inside a *different* generic,
  §2c note) is NOT part of the #254 proposal and needs no work unless a corpus case appears —
  **report, do not spawn an issue.**
- **Unblocks #163 and #294:** #163's `Map<K,V>::new()` / `List<T>::new()` factories are exactly the
  L5 shape; #294's crumb depends on #254 method stamping (already shipped for its needs). After this
  lands, #163 can proceed on the L5 factory contract.

---

## 6. What remains BLOCKED (design-ahead honesty)

Nothing in this gap is blocked — every root is pinned against the current tree and every crumb
compiles on today's seed (the seed already has generics, classes, and the L1–L3 method machinery this
builds on). The design is fully specified and ready to implement the moment `fix/issue-254` (L1–L3)
merges to the drain branch. No owner HALT: the `expected_ret`-vs-`TExpr.type` fork was resolved
law-first (narrower thread wins; M.5 no-needless-surface + the implementer's explicit constraint),
so there is no genuine unresolved tension.
