# Drain — Onda-3 sub-cluster A (re-verified implementation crumbs)

**Status:** DESIGN-AHEAD (architect). No product code changed. Re-verified at file:line against the current tree.
**Parent design:** `docs/design/onda3-monomorphization-cluster.md`.
**Order (A):** `#290` → `#301` → `#254` → `#294`. **Seed:** `teko.tkp` current (`.37-alpha`).
**Rule:** every snippet is full-Javadoc, `.tks`-only. C twins FROZEN (only `teko_rt.{c,h}` is maintained — none needed here).
**Ritual (all):** full gate — gen1 `teko . -o bin` (native #test) + `./bin/teko test .` (VM) + FIXPOINT gen1==gen2 byte-identical + `diff_vm_native.sh` + `TEKO_MEM_PARANOID=1` + `//`-audit.

## RE-VERIFICATION HEADLINE (the #296 lesson: count sites, do not trust "1 crumb")

The original cluster doc under-described three of the four roots. Verified counts below; **every added site is flagged** with `[+SITE MISSED]`.

| Issue | doc estimate | **verified** | added sites the doc missed |
|-------|-------------|--------------|-----------------------------|
| #290 | 2 crumbs (typer desugar + scope predicate) | **verified 3 sites** | sibling desugar at `typer.tks:774-778` (base-vtable path) uses CANONICAL `struct_name` directly — MUST stay canonical or it regresses into the same bug from the other side |
| #301 | 3 mangle arms | **verified 5 native sites + 1 VM probe** | `cg_member_key_str:1039` + `cg_variant_typename_str:1057` auto-cover the `ReadFn\|error` variant-member case (delegation) — but the present-wrap at `cg_wrap_elem_str:3459` and the typedef-body emitter at `codegen.tks:6894` are the load-bearing downstream consumers to eyeball; `cg_type_ready` already Func-ready |
| #254 | "4 collect + 2 typer" call-sites (layer 1) | **verified 2 fn-body edits, layer 2 has 3 sites not 2, layer 4 needs env-plumbing** | layer-2 `ClassBody` is NOT dropped (it survives UN-substituted via the `_` arm) — a *different* bug than StructBody's drop; `collect_body_insts:1424` has no method-body walk (transitive insts from method bodies never discovered); layer-4 `type_return` has NO return-type in scope → needs an `env` field, not a one-liner |
| #294 | 1 post-ruling crumb | **verified 2 sites** | `rekey_iface_dispatch:404` early-returns `cl` unchanged when the receiver conforms (line 410) — a STRUCT receiver keeps `is_iface_dispatch=true` and hits the class-only vtable at `codegen.tks:1881→emit_iface_call`; the fix must REWRITE to a direct call, and `emit_iface_call` must never see a struct receiver |

---

## A/#290 — same-bare cross-ns method dispatch — **verified 3 sites**

**Roots (confirmed):**
- `typer.tks:786` — `type_method_call` desugars the direct instance call to a callee whose class segment is `name_last_segment(struct_name)` (BARE). This is the bug: `lookup_call` then matches ANY ns ending in that bare class.
- `scope.tks:130 lookup_call` + `scope.tks:166 call_ns` — for a 2-segment path (`qualified = true`, `scope.tks:132`), `qual = segments[len-2].name` and the match is `ns_last_seg(b.ns) == qual` (`scope.tks:143/156/179/192` — **4 identical compare sites**, 2 per fn × bindings/base). A method-ns is `ns::Class` (`typer.tks:3110`), so `ns_last_seg` = `Class` → matches `left::Svc` AND `right::Svc`; reverse scan returns the first → wrong ns → `argument type mismatch`.
- `[+SITE MISSED] typer.tks:774-778` — the `is_polymorphic_base` virtual-dispatch desugar builds `bseg_base = struct_name` **CANONICAL** (not bared) and emits `is_iface_dispatch=true`. This path is CORRECT today (keys off the full name via the vtable). The #290 fix must NOT bare this path; both desugars must converge on canonical. **Confirm this sibling stays canonical when you touch line 786.**

**Crumb 1 — carry the receiver's CANONICAL class into the direct-dispatch callee.**
Change `typer.tks:786`: build the callee from the canonical qualifier's segments, not the bare last segment. The receiver's `struct_name` (from `recv_t` = `Named{n.name}`, `typer.tks:694`) is already canonical.

```teko
/**
 * Build the direct-method-dispatch callee as a FULLY-QUALIFIED segment path from the receiver's
 * canonical class name, so `lookup_call` keys off the exact owning namespace and never collides
 * with a same-bare class in a sibling namespace (#290, #109-family). Mirrors how the polymorphic-base
 * desugar (typer.tks:774) already keeps `struct_name` canonical, and how FIELD access already resolves
 * off the canonical `Named` via the type table (which is why fields never regressed).
 *
 * @param struct_name  the receiver's canonical class name (`ns::…::Class`)
 * @param method       the dispatched method's bare name
 * @return             the `[..ns.., Class, method]` callee path (all leading qualifier segments + Class + method)
 * @since onda-3 (#290)
 */
fn method_dispatch_callee(struct_name: str, method: str) -> parser::Path {
    mut segs: []parser::Segment = teko::list::empty()
    let qual = name_qualifier(struct_name)
    if qual.len > 0 {
        let parts = split_ns_segments(qual)
        mut i = 0
        loop { if i >= parts.len { break }; segs = teko::list::push(segs, parser::Segment { name = parts[i] }); i++ }
    }
    segs = teko::list::push(segs, parser::Segment { name = name_last_segment(struct_name) })
    segs = teko::list::push(segs, parser::Segment { name = method })
    parser::Path { segments = segs }
}
```
Wire at `typer.tks:788` — replace the `seg_struct`/`segs` build with `let segs = method_dispatch_callee(struct_name, mc.method).segments`.

> **GROUNDING GAP (implementer confirms FIRST):** lengthening the path alone does NOT fix `lookup_call`, whose `qual = segments[len-2].name` is still a SINGLE segment (`scope.tks:133/169`) — it would compare only the `Class` segment. **Crumb 2 is mandatory.** Verify `name_qualifier`/`split_ns_segments` exist (grep: `name_qualifier` is used at `codegen.tks:6682`; add `split_ns_segments` if absent as a `::`-splitter helper in `scope.tks`).

**Crumb 2 — disambiguate the class-owning tail in `lookup_call` + `call_ns`.**
When the callee path has ≥3 segments AND the second-to-last names a class in the table (a class-qualified dispatch), require `b.ns` to end with the FULL `owner::…::Class` tail, not merely `ns_last_seg(b.ns) == qual`. Keep the existing single-segment rule for a plain `ns::fn` call (guard: second-to-last names a NAMESPACE, not a class).

```teko
/**
 * Does binding `b`'s namespace satisfy a class-qualified method-dispatch callee? For a class method the
 * binding's ns is `owner::…::Class` (typer.tks:3110); the callee carries `[..owner.., Class, method]`.
 * Matching the FULL `owner::…::Class` tail (not just the `Class` last segment) disambiguates two
 * same-bare classes across namespaces (#290). A no-op for a plain `ns::fn` call (that path has no
 * class-owning segment, so the caller keeps the existing `ns_last_seg == qual` rule).
 *
 * @param b_ns    the candidate binding's namespace
 * @param callee  the dispatch callee path (≥3 segments for a class-qualified dispatch)
 * @return        true iff `b_ns` == or ends-with the callee's `<owner-segments>::<Class>` tail
 * @since onda-3 (#290)
 */
fn ns_matches_qualified_class(b_ns: str, callee: parser::Path) -> bool {
    let want = join_ns(path_prefix_segments(callee))
    b_ns == want || ends_with_ns(b_ns, want)
}
```
Apply at ALL **4** compare sites (`scope.tks:143` + `:156` in `lookup_call`, `:179` + `:192` in `call_ns`). Guard: only substitute the predicate when `callee.segments.len >= 3 && second_to_last names a class`; else keep `ns_last_seg(b.ns) == qual`. Helpers `join_ns`/`path_prefix_segments`/`ends_with_ns` — grep first; `ends_with_ns` likely already exists (`find_class_method` at `vm.tks:2998` uses an `ends_with(tf.namespace, "::" ~ class_name)` idiom — reuse that exact tail-compare).

**Fixtures:** `examples/regressions/same_bare_method_dispatch/` (VM==native), modeled on `di_same_name_cross_ns`. Two ns `left`/`right`, each `type Svc = class { pub tag: i64; pub fn make(x: i64) -> Svc; pub fn tag_of(self) -> i64 { self.tag } }`; `left::Svc::make(3).tag_of() == 3`, `right::Svc::make(7).tag_of() == 7`; `exit(l + r)` = **exit 10**. Fails to type-check today, passes both after. **Fold in:** flip `di_same_name_cross_ns` to CALL the method (its field-read is the #290 workaround) IN THIS PR — it IS the regression closure (report the flip in the PR body; do not silently rewrite).
**Ritual:** full gate; the compiler's own #109 type surface has same-bare classes, so gen1==gen2 is a loud guard. The qualified-class predicate MUST be a no-op for every non-class-dispatch call.

---

## A/#301 — closure-in-Ref/optional round-trip — **verified 5 native sites + 1 VM probe**

**Native roots (confirmed):** the mangle family drops `checker::Func`. The three PRIMARY arms:
- `cg_opt_mangle` (`codegen.tks:942`) — `_` fallthrough at `:969` = "optional/slice inner type not yet supported".
- `cg_opt_mangle_str` (`codegen.tks:979`) — `_` fallthrough at `:1008`.
- `cg_opt_mangle_texpr_str` (`codegen.tks:1086`) — `_` fallthrough at `:1091` (the syntactic twin; `FunctionType` unhandled).

`[+SITE MISSED]` verified downstream that AUTO-COVER once the 3 arms land (no separate edit, but MUST be eyeballed to confirm valid C):
- `cg_member_key_str:1039` + `cg_variant_typename_str:1057` DELEGATE to `cg_opt_mangle_str` → the `ReadFn | error` **variant-member** case (#301's explicit union-arm blocker) is fixed transitively. Confirm `cg_member_key_str` yields `"func"` (not a keyword collision — `func` is not a C keyword; safe).
- typedef-collection: `cg_opt_key:6196` calls `cg_opt_mangle`; today Func → `""` → the type is SKIPPED (`cg_opt_set_add:6202`). After the arm, `cg_opt_key(Func)="func"` → the inner/slice is REGISTERED → the prelude emitter MUST emit a valid body:
  - inner body `codegen.tks:6894-6900`: `typedef struct tk_opt_func { bool present; <emit_type(Func)> value; } tk_opt_func;` — and `emit_type` Func arm (`codegen.tks:849`) = `tk_closure` → `{ bool present; tk_closure value; }` VALID.
  - slice body `codegen.tks:6846-6850`: `typedef struct { <emit_type(Func)> *ptr; uint64_t len; } tk_slice_func;` → `{ tk_closure *ptr; uint64_t len; }` VALID.
  - `cg_type_ready` (`codegen.tks:6644` `_` arm + `:6677` `parser::FunctionType => true`) is ALREADY Func-ready — **no edit.**
  - present-wrap `cg_wrap_elem_str:3459` recurses `cg_opt_typename(opt.inner=Func)` → `tk_opt_func`, then the inner wrap falls to the bare `_` emit — **no edit** (auto-covered).

**Crumb 1 (native, load-bearing) — the three `Func` mangle arms.**
```teko
// cg_opt_mangle (codegen.tks:942): add BEFORE the `_ =>` at :969
checker::Func => cb(buf, "func")
// cg_opt_mangle_str (codegen.tks:979): add BEFORE the `_ =>` at :1008
checker::Func => "func"
// cg_opt_mangle_texpr_str (codegen.tks:1086): add BEFORE the `_ =>` at :1091
parser::FunctionType => "func"
```
The fragment `"func"` MATCHES `cg_texpr_mangle`'s existing `parser::FunctionType => "func"` (`codegen.tks:1134`) — one canonical spelling. No new typedef code needed (the collection/emit paths above light up automatically).

**Crumb 2 (VM) — the ref-cell closure round-trip — PROBE-GATED (design complete, one line probe-pinned).**
Verified VM facts: `eval_lambda_call` (`vm.tks:2962`) DOES merge callee cells back (`with_cells(caller, fe.env.cells)`, `:2981`); the deref-read `cell_get` (`vm.tks:1236`) and cells-merge for a user fn (`:3267`) are `Value`-generic, so a `FuncVal` threads through structurally. Two verified suspects:
1. **snapshot capture** — `fv.cap_vals` bound by value at `eval_lambda_call:2977` (from the `TLambda` snapshot at `vm.tks:2170-2182`): a closure that must observe a cell WRITE made after its capture needs to capture the CELL INDEX (a `RefVal`), not the value. This is already the sound pattern (#300's `make_counter` proof).
2. **`coerce_to` on a Func return** — `vm.tks:3270` runs `coerce_to(rv, f.return_type)` on the returned value; if `f.return_type` is a `Ref<Fn>`/`Fn?` the coerce may not carry a FuncVal through unchanged. **Verify `coerce_to` has a Func/Ref-of-Func passthrough.**

**Implementer probe FIRST:** run the `reseat(cell: Ref<IntFn>, n)` repro under `TEKO_TRACE`; confirm whether the drop is (1) the snapshot, (2) `coerce_to`, or (3) the merge. Land whichever the probe proves. **The native crumb 1 is the load-bearing half** and unblocks the `ReadFn | error` union-arm case regardless.

**Fixtures:**
- `examples/regressions/closure_in_ref_roundtrip/` (VM==native) — the `reseat` repro; exit-code protocol.
- `examples/regressions/closure_optional_field/` (native — `tk_opt_func` must cc-compile): a struct with a `Fn?` field and a `[]Fn` field, constructed + read.
- `flat_map`-shaped `iter_test.tkt` once round-trip holds (unparks ITER0 #184).
**Ritual:** ship BOTH engines in ONE PR (mangle=native, cell=VM) so `diff_vm_native.sh` never sees a half-fixed state. Corpus carries no `Fn?`/`[]Fn` today → gen1==gen2 trivially preserved (guard: the new arms are a no-op unless a Func inner appears).

---

## A/#254 — generic type methods — **verified 2 fn-body edits (L1); L2 has 3 sites; L4 needs env-plumbing**

`#290` MUST be green first (methods lower through the keying #290 fixes).

### Layer 1 — enclosing type-params in the method table. **verified: 2 fn-body edits (NOT 6 call-sites).**
The reverted patch edited 6 CALL-SITES; the CLEAN fix is 2 FUNCTION BODIES, each looking up the owner decl by `struct_name` from `table` (covering all callers at once):
- `method_func_type` (`collect.tks:63`, body builds `tbl` at `:64` from `f.type_params` only) — called from `collect.tks:120,138,1475,1491` + `typer.tks:746,820`.
- `type_method` (`typer.tks:2933`, `tbl` at `:2938`) — called from `typer.tks:3131`.
Free-fn sites `func_type` (`collect.tks:27`) and `type_function` (`typer.tks:3029`) are NOT methods → **leave unchanged.** `check_modules.tks:175`, `collect.tks:1329`, `monomorph.tks:476` are not method-body typers → **leave.**

```teko
/**
 * Build a method's type-param table PREPENDED with the enclosing type's own type-params/constraints,
 * so a method of `List<T>` referencing `T` resolves against the OWNER's `T` instead of failing with
 * "unknown type: T" (#254 layer 1). The owner decl is looked up by its canonical name; a non-generic
 * owner (or a free fn) contributes an empty prefix → byte-identical no-op for the existing corpus.
 *
 * @param owner_name  the enclosing type's canonical name (`struct_name`)
 * @param mtps        the method's own type-params
 * @param mtcs        the method's own type-constraints
 * @param table       the folded type table (source of the owner's params)
 * @return            a type-param table over `owner_tps ++ mtps` / `owner_tcs ++ mtcs`
 * @since onda-3 (#254)
 */
fn method_type_param_table(owner_name: str, mtps: []str, mtcs: []parser::ConstraintExpr, table: TypeTable) -> TypeTable {
    match type_table_find(table, owner_name, "") {
        parser::TypeDecl as td => type_param_table(concat_tps(td.type_params, mtps), concat_tcs(td.type_constraints, mtcs), "", table)
        error => type_param_table(mtps, mtcs, "", table)
    }
}
```
At `collect.tks:64` replace with `method_type_param_table(struct_name, f.type_params, f.type_constraints, table)`. At `typer.tks:2938` replace with `method_type_param_table(struct_name, f.type_params, f.type_constraints, table)`. (Provide `concat_tps`/`concat_tcs` as trivial two-list appends if absent.) **NOTE:** the reverted 4+2 version was 662-tests-green — this refactor is equivalent, fewer touch points.

### Layer 2 — stop dropping/under-substituting methods. **verified: 3 sites (doc said 2).**
- `subst_body_names` StructBody arm `resolve.tks:1398` — drops methods (`teko::list::empty()`).
- `normalize_inst_body` StructBody arm `resolve.tks:1689` — drops methods.
- `[+SITE MISSED]` BOTH `_` default arms (`resolve.tks:1402`, `:1693`) return `body` UNCHANGED → a `ClassBody` currently PASSES THROUGH with methods KEPT-but-UN-SUBSTITUTED (a *different* bug: `T` in a class method body is never replaced). Add an explicit `ClassBody` arm to each (subst fields AND methods), and rewrite the StructBody arm to KEEP+subst methods.
- `[+SITE MISSED]` `collect_body_insts` (`resolve.tks:1424`) has StructBody/Variant/Alias arms but NO method-body walk and NO ClassBody arm → transitive generic-instances reachable only through a method body are never discovered. Add a method-body walk (both StructBody and a new ClassBody arm) so `list.push(Box<i64>{…})` inside a method enqueues `Box__g__i64`.

```teko
/**
 * Substitute the enclosing type's parameters through a struct/class BODY, KEEPING and rewriting the
 * methods (previously dropped for a struct, previously un-substituted for a class — #254 layer 2).
 * Each method's param annotations, return type, and body type-references are subst'd (`subst_texpr_names`
 * at the type level; the body is still SYNTACTIC at this stage, so this is a name-level rewrite). A
 * `ClassBody` is handled symmetrically (it was hit by the `_` passthrough before).
 * @param body    the generic type's body
 * @param params  the enclosing type's parameter names
 * @param args    the concrete type-args at this instantiation
 * @return        the body with fields AND methods substituted to concrete types
 * @since onda-3 (#254)
 */
```
Method walk per method: `subst_texpr_names` each `params[i].type_ann` + `return_type`, and recurse the body statements' type-annotations (the same syntactic name-subst `subst_body_names` applies to fields; reuse `subst_texpr_names`).

### Layer 3 — stamp generic-type METHODS in the mono worklist. **verified: new discovery + new stamp.**
`monomorph.tks:289 find_generic_fn` filters only free `type_params.len > 0` TFunctions; `monomorphize` PHASE-2 (`:775-809`) stamps only those. `table_generic_instances` (`:814`) appends the concrete TYPE-decls whose bodies were method-carried by layer 2 — but their methods are never emitted as callable stamped free fns. Add: when a concrete instance is appended (`monomorph.tks:814-822`), ALSO emit each carried method as a stamped free `TFunction` named `Inst::method` keyed by the instance name, its body run through `mono_block` (`:703`) under the instance's Subst — IDENTICAL to the free-fn stamp at `:795-808`. Discovery seed: a method call on a generic-type instance (`list.push(x)` where `list: List<i64>`) enqueues the method inst exactly as a free generic call does at `mono_texpr` (`:426`).

```teko
/**
 * Stamp every method of a concrete generic-type instance as a monomorphic free TFunction keyed by the
 * instance name, its body rewritten through the instance's Subst — mirroring the free-fn stamp
 * (monomorphize PHASE-2, monomorph.tks:795) but the callable's owner is the stamped type, so
 * `list.push(x)` on a `List<i64>` dispatches to `List__g__i64::push`. Reuses mono_block/subst_type
 * unchanged. A NO-OP when the instance carries no methods (the existing corpus) → gen1==gen2 preserved.
 * @param inst   the type instance (mangled name + Subst)
 * @param gtd    the generic type-decl template (carrying methods after layer 2)
 * @param items  the program items (transitive generic-call discovery)
 * @param table  the folded type table
 * @return       the stamped method TFunctions + any transitively-discovered insts
 * @since onda-3 (#254)
 */
```

### Layer 4 — return-type-as-expected threading. **verified: needs an `env` field, NOT a one-liner.**
`type_return` (`typer.tks:2482`) currently does `type_expr(r.value, env, table)` with NO expected context — and `env` does NOT carry a return type today (grep confirmed: no `env.ret_type`). So the doc's "pass the fn's return type" requires PLUMBING:
1. Add an `expected_ret: Type` (or `Type?`) field to `Env`; set it in `type_function` (`typer.tks:3027`) and `type_method` (`typer.tks:2933`) via a `with_ret(env, resolved_ret)` before typing the body (analogous to `with_owner` at `typer.tks:2937`).
2. In `type_return`, when `env.expected_ret` is a concrete type, type the value with that expected context by reusing `type_value_expected`/`type_struct_lit(expected)` (the annotation-driven retarget already used for `let` at `type_binding`, which routes through `type_struct_lit` at `typer.tks:1499` / `1606`). This retargets `Box { value = v }` to `Box__g__T` at a return position exactly as `let x: Box<T> = …` does.

```teko
// type_return (typer.tks:2482) — thread the enclosing return type as the struct-init expected context:
fn type_return(r: parser::Return, env: Env, table: TypeTable) -> TypedStmt | error {
    if !r.has_value {
        return TypedStmt { node = TReturn { has_value = false; value = void_texpr() }; env = env }
    }
    let v = match type_expr_expected(r.value, env.expected_ret, env, table) { TExpr as te => te; error as e => return e }
    TypedStmt { node = TReturn { has_value = true; value = v }; env = env }
}
```
(Where `type_expr_expected` is the expected-threading entry `type_binding` already uses — confirm its exact name; `type_value_expected` at `typer.tks:933` is the arg-position twin.) **Adding an `Env` field is a wide change — every `Env { … }` literal must set it.** Grep `Env {` and default the new field to a `Void`/`null` sentinel to keep every non-return path byte-identical.

### Layer 5 — generic class static factory.
With L1-L4, a generic class's static factory (body constructs `Self<T>{…}`, return type is the generic instance) type-checks and stamps. The ClassBody method carrier (L2/L3) covers it. Verify arena-per-object ref semantics survive stamping (the stamped method set reuses the same class-lowering; no new codegen).

**Fixtures (VM==native unless noted):**
- `examples/regressions/generic_struct_method/` — `type Box<T> = struct { value: T; pub fn get(self) -> T { self.value } }`; `Box<i64>{value=42}.get()` → **exit 42**; a second `Box<u8>` inst proves per-instance stamping.
- `examples/regressions/generic_class_factory/` — `type Cell<T> = class { pub v: T; pub fn make(x: T) -> Cell<T> { Cell { v = x } }; pub fn read(self) -> T { self.v } }`; `Cell<i64>::make(7).read()` → exit 7 (proves L5 + L4).
- `examples/regressions/generic_method_self_construct/` — `fn dup(self) -> Box<T> { Box { value = self.value } }` (proves the L4 return-type thread).
- `examples/regressions/generic_method_trait_fold/` — a `<K: Hashable & Eq>` chain method (the #163 Map-key path — proves the constraint gate + trait fold survive stamping).
- Corpus `#test`s in `generics_test.tkt` (VM → cover the mono method path both engines).
**Ritual:** full gate at EACH layer (independently gate-able). The `any_generic` no-op guard (`monomorph.tks:739`) MUST hold — the compiler has ZERO generic methods → gen1==gen2 byte-identical is the single most important fixpoint guard in the cluster. Verify at each layer, not only at the end.
**Sequencing vs #162 (S6):** memory `teko-generic-methods-gap` sequences #254 AFTER #162 to avoid conflict in monomorph/typer/resolve/collect. Confirm #162's merge state; layers 1-2 (collect/resolve) are the conflict-prone ones — coordinate or rebase.

---

## A/#294 — struct-through-constraint dispatch — **verified 2 sites; ruling (a) applied; needs #254 first**

**Roots (confirmed):**
- `rekey_iface_dispatch` (`monomorph.tks:404`) — when the receiver conforms (`type_conforms_to`, `:410`) it returns `cl` UNCHANGED, keeping `is_iface_dispatch=true`. `type_conforms_to` accepts structs AND classes nominally, so a constraint-bound STRUCT reaches here with `is_iface_dispatch=true`.
- `[+SITE MISSED]` codegen: `emit_call` routes `c.is_iface_dispatch` → `emit_iface_call` (`codegen.tks:1881`), which emits the class-only fat-pointer upcast + `tk_vt_<T>_<iface>` (every `tk_vt_`/`cg_is_class_named` site: `codegen.tks:441,3488,3863,3888`). A struct has no `{data,vtable}` rep → cc-reject / miscompile natively; VM has no vtable value to name-dispatch through → panic. So there are TWO sites: the rekey (checker) AND the emit (codegen) — the fix must ensure a struct receiver NEVER reaches `emit_iface_call`.

**RULING (a) — applied, law-first (M.1/M.3, matches `teko-oop-w10b-design`): a constraint is a monomorphization GATE, not a dynamic-dispatch promotion.** A `<T: Contract>` body with `T` bound to a concrete struct is fully known at instantiation → lower `x.measure()` to a DIRECT call to the stamped `Struct::measure` (produced by #254), never an upcast. No value-vtable, no new rep. Dynamic dispatch (fat pointer) stays class-only. **No owner HALT.** Logged for LTS v1.0.0.0 review.
*Residual flag (informational, do NOT expand):* a future "struct value stored behind a `Contract`-typed slot" (heterogeneous struct-as-contract collection) is NOT provided by (a) — that needs boxing/(b), a NEW capability out of #294's scope. Report, do not turn into an issue.

**Crumb (post-#254) — rewrite a constraint-bound STRUCT dispatch to a direct stamped-method call.**
In `rekey_iface_dispatch` (`monomorph.tks:404`), BEFORE the `type_conforms_to` early-return at `:410`, add: if the receiver `args[0].type` is a `Named` naming a STRUCT (not a class/interface), rewrite the TCall to a DIRECT (non-iface) call to the concrete stamped method `Struct::method` (the same name #254 stamps), clearing `is_iface_dispatch` and `iface_slot`. This keeps `emit_iface_call` class-only.

```teko
/**
 * When a constraint-dispatch receiver is a concrete STRUCT (value type, no vtable), rewrite the call to
 * a DIRECT call to its stamped concrete method — a constraint is a monomorphization gate, not a
 * dynamic-dispatch promotion (#294, ruling (a)). Dynamic dispatch (the fat pointer) stays class-only,
 * exactly as today. Requires #254's per-instance method stamping (the direct target must exist). A
 * no-op for a class/interface receiver (they keep is_iface_dispatch=true) → no regression to the
 * class dispatch path.
 * @param cl    the interface-dispatch TCall (is_iface_dispatch true; args already rewritten)
 * @param args  the rewritten args (args[0] is the receiver, carrying the concrete resolved type)
 * @param table the folded type table (to classify the receiver as struct vs class vs interface)
 * @return      a DIRECT TCall keyed on the stamped struct method when the receiver is a struct; else `cl`
 * @since onda-3 (#294)
 */
fn rekey_struct_constraint_dispatch(cl: TCall, args: []TExpr, table: TypeTable) -> TCall {
    if !cl.is_iface_dispatch || args.len == 0 { return cl }
    let recv = match args[0].type { Named as n => n.name; _ => return cl }
    if !is_struct_named(table, recv) { return cl }   // class/interface → leave for the vtable path
    let method = cl.callee.segments[cl.callee.segments.len - 1].name
    let direct = method_dispatch_callee(recv, method)   // reuse #290's canonical callee builder
    TCall { callee = direct; args = cl.args; call_ns = recv; is_closure_call = false; callee_type = cl.callee_type; is_iface_dispatch = false; iface_slot = 0 }
}
```
Call it at the head of `rekey_iface_dispatch` (return early on a struct rewrite) so `emit_iface_call` never receives a struct. Provide `is_struct_named(table, name)` (a struct-decl-body probe; the checker twin of `cg_is_class_named`). The VM side needs no new dispatch — a direct call resolves through `find_function_ns` like any stamped method.

**Fixtures (VM==native):**
- `examples/regressions/struct_through_constraint/` — `type P = struct { pub w: i64; pub fn measure(self) -> i64 { self.w } }` + `fn total<T: Measurable>(x: T) -> i64 { x.measure() }`; `total(P{w=5})` → **exit 5**. Panics on VM / cc-rejects natively today; both pass after.
- `examples/regressions/struct_vs_class_constraint/` — same constraint satisfied by a STRUCT and by a CLASS in the same program; the struct dispatches direct, the class through the vtable — both correct (proves the no-op-for-class guard).
**Ritual:** full gate; gated on #254 green (the stamped concrete method is the direct target). gen1==gen2: the compiler uses no constraint-bound struct dispatch → the new arm is a no-op there.

---

## What remains BLOCKED (design-ahead honesty)
- **#301 VM crumb 2** — probe-gated: `TEKO_TRACE` the `reseat` repro to pin snapshot (`vm.tks:2977`) vs `coerce_to` (`vm.tks:3270`) vs merge. Design complete (capture-the-cell-index). Native crumb 1 (mangle) is fully specified and unblocks the `ReadFn | error` union-arm case immediately.
- **#294** — gated on #254's method stamping (the direct target). Ruling (a) applied, law-clean; the crumb is a small dispatch-rewrite once #254 lands.
- **#254 layer 4** — the `Env` field addition is wide; confirm `type_expr_expected`/`type_value_expected`'s exact entry name before editing (both exist, name-verify).
- Everything else (#290, #301 native, #254 layers 1-3/5) is fully specified against the current tree and can start once #300 is merged and #290→#254 ordering is honored.
