# Generic-stack completion — the four (+1) residual generic-class gaps (#163 follow-up)

**Status:** DESIGN-AHEAD (architect). No product code changed. Re-verified at file:line against the current tree.
**Parents:** `docs/design/onda3-monomorphization-cluster.md`, `docs/design/drain-onda3-subcluster-A.md` (K-A: #290→#301→#254→#294, all landed). This is the K-A follow-on cluster — the same mono-pass + codegen machinery, one layer deeper.
**Trigger:** #163 (collections) shipped `List<T>` + a `str`-keyed `Map<V>` (PR #321) by WORKING AROUND these gaps (each documented verbatim in `src/collections/{list,map,collections}.tks`). The generic stack under-delivers the collections ruling (`Map<K: Hashable & Eq, V>`, `teko::env = Map<str, str?>` generically, nested generics) until these land.
**Seed:** `teko.tkp` current. **Rule:** every snippet is full-Javadoc, `.tks`-only. C twins FROZEN (only `teko_rt.{c,h}` maintained — none needed here). **Size:** **L** — a multi-PR round, the twin of onda-3 sub-cluster A.
**Ritual (every crumb):** full gate — gen1 `teko . -o bin` (native `#test`) + `./bin/teko test .` (VM) + FIXPOINT gen1==gen2 byte-identical + `diff_vm_native.sh` + `TEKO_MEM_PARANOID=1` + `//`-audit. **The `any_generic` no-op guard (`monomorph.tks` PHASE-1/PHASE-2 gate) MUST hold — the compiler corpus has ZERO generic instances, so gen1==gen2 stays byte-identical; verify at EACH crumb, not only at the end.**

---

## HEADLINE — the shared root (the whole point of the cluster)

All five gaps are **one family: a generic type's OWNING NAMESPACE is lost the moment the stamp/dispatch machinery touches it, and a structural-trait capability is invisible on a type param.** Two shared roots:

1. **ROOT-NS (gaps #2, #4-value-form, #5):** the bare-vs-canonical name split (`#109`/`#290` family). A stamped generic instance name (`List__g__i64`) is BARE (`mono_type_mangle` mangles only the last segment, `monomorph.tks:154`). Every site that re-resolves a generic USE by its bare base — the nested-construct resolver (`resolve_named` in `type_struct_lit`, `typer.tks:1587`), the cross-ns static-factory retarget (`retarget_generic_static_callee`, `typer.tks:932`), and the syntactic type-arg emit (`emit_type_expr`, `codegen.tks:1277`) — resolves it against the WRONG namespace (the caller's `ref_ns`, or drops the qualifier entirely). This is the *identical* failure mode #290 fixed for method dispatch, now hitting construction + factory + param-emit.

2. **ROOT-DISPATCH (gaps #1, #3):** a call keyed against the ABSTRACT template (`call_ns = ns::Holder`, or a structural-trait atom that never became an `extends` interface) is never re-keyed to the concrete stamp. `mono_texpr`'s TCall arm preserves `call_ns` verbatim (`monomorph.tks:568,571`) — a sibling `self.count()` stays keyed on `Holder`, not `Holder__g__i64` (#3); and a structural trait (`Hashable`/`Eq`) never enters `constraint_interfaces` (`resolve.tks:487-492`) so it produces an EMPTY `extends` → opaque `K`, no method surface (#1).

**Flagship = Gap #1** (structural-trait dispatch on a type param): it unblocks generic-`K` `Map<K: Hashable & Eq, V>` and `teko::env`. It is CHECKER-only (plus a mono re-key crumb that #3 also needs), sits on the D22-ratified "constraint = monomorphization gate, not dynamic vtable" law, and is the single highest-leverage fix. **Gap #3** shares its mono re-key crumb (the ROOT-DISPATCH fix), so #1+#3 are naturally co-delivered.

---

## VERIFIED ROOT-CAUSE MAP (grepped, not theorized)

| Gap | One-line root | Layer | file:line | Shared root |
|-----|---------------|-------|-----------|-------------|
| **#1** structural-trait on type param | `Hashable`/`Eq` are name-set-only (`is_structural_trait`, not a TypeDecl) → `is_trait_name`/`is_interface_name` false → `constraint_interfaces` yields EMPTY `extends` → `type_param_reg` builds an `ExternBody` (opaque) not an `InterfaceBody` → `k.hash()`/`k.eq()` unauthorized; `==` on `K` stays an honest stop | checker (atom_surface / constraint_interfaces / type_param_reg) + mono re-key | `resolve.tks:327` (`atom_surface` structural→empty), `resolve.tks:487-492` (`constraint_interfaces` no structural arm), `resolve.tks:568-569` (`type_param_reg` ExternBody), `typer.tks:263-268` (`==`/op honest stop) | ROOT-DISPATCH |
| **#2** nested generic construct in generic method | `Cell<V>{…}` inside `Holder<V>::m` → `resolve_named(sl.type_path, table, env.cur_ns)` resolves `Cell` in the USING ns; the self-construct phantom (`is_self_generic_construct`) only covers the OWN type, so a DIFFERENT generic base falls to the annotation branch with no stamped `Cell__g__V` phantom → "type 'Cell' is not visible bare from namespace" | checker (type_struct_lit) + mono (subst self-construct extension) | `typer.tks:1587` (`resolve_named` in cur_ns), `typer.tks:1634-1651` (only OWN-type phantom), `resolve.tks:217` (the "not visible bare" error), `monomorph.tks:1043-1050` (`instance_method_subst_l5` phantom is own-type-only) | ROOT-NS |
| **#3** sibling instance method on `self` | stamped `Holder__g__i64::twice` body calls `self.count()` typed against abstract `Holder` → `call_ns = ns::Holder`; `mono_texpr` TCall arm keeps `call_ns` verbatim → codegen `cb_fn_name(ns=Holder-canon, "count")` ≠ emitted `Holder__g__i64__count` → "undeclared identifier" | mono (call_ns re-key) + codegen (symbol mismatch is the SYMPTOM) | `monomorph.tks:568` & `:571` (`call_ns = cl.call_ns` un-rewritten), `codegen.tks:364` (`cb_fn_name` mangles the stale ns), `monomorph.tks:972-984` (`stamp_one_instance_method` — body walked but call_ns not remapped) | ROOT-DISPATCH |
| **#4** generic instance as free-fn param | free-fn param written `m: Map<i64>` → `emit_type_expr` hits the `nt.args.len > 0` early-return (`codegen.tks:1277`) emitting `tk_t_Map__g__i64` BY VALUE, skipping the ClassBody-pointer check (`:1283-1284`); the bare stamped form `Map__g__i64` DOES reach it → `Map__g__i64 *` (pointer). Value-vs-pointer mismatch | codegen (emit_type_expr) | `codegen.tks:1277` (args>0 early-return, no class check), `codegen.tks:1283-1284` (the class-pointer arm it skips), `codegen.tks:926` (`emit_type` semantic twin — correct) | ROOT-NS (bare-vs-canonical, class-ness lost on the `<args>` form) |
| **#5** cross-ns generic factory | `ns::List<i64>::make()` from another ns → `retarget_generic_static_callee` builds `NamedType{path=single_seg_path(base=List), args}` DROPPING the `ns::` qualifier, resolves `List` in the CALLER's `ref_ns` → "not visible bare" | checker (retarget_generic_static_callee) | `typer.tks:932` (`single_seg_path(base)` drops qualifier), `typer.tks:933` (`resolve_type` in caller ref_ns) | ROOT-NS |

**Cross-checks confirming the map:**
- `is_structural_trait` (`resolve.tks:887-890`) is a pure closed name-set (`Eq`/`Ord`/`Hash`/`Clone`/`Default` + synonyms `Hashable`/`Comparable`) — NO type-table entry. `synthesize_structural_methods` (`synth.tks:629`) already produces per-deriver `eq`/`hash`/`compare`/`clone`/`default` methods on each CONCRETE struct/class. So the concrete stamp ALREADY HAS the method; only the type-param dispatch to it is missing (#1 is a routing fix, not a synthesis fix).
- The stamped instance TypeDecl DOES preserve `ClassBody` (`subst_body_names` ClassBody arm, `resolve.tks:1594-1598`), so `cg_is_class_named("Map__g__i64")` is TRUE — which is exactly why #4's value-form (that skips the check) diverges from the pointer-form (that honors it).
- D22 (DECISION_LOG.md:210) ratified "constraint = monomorphization gate, not dynamic vtable" for #294 structs; #1 must obey the SAME law for structural traits (see Law notes).

---

## DEPENDENCY DAG + ORDERING

```
                 ROOT-DISPATCH (mono call_ns re-key)      ROOT-NS (owning-ns preservation)
                 ├── Gap #3 (sibling self.method)         ├── Gap #2 (nested generic construct)
                 └── Gap #1 (structural-trait on K) ──┐    ├── Gap #4 (free-fn param emit)
                     (needs the re-key AND the         │    └── Gap #5 (cross-ns factory)
                      structural→interface synthesis)  │
                                                       ▼
   FLAGSHIP CHAIN:  #3  →  #1  ──unblocks──▶  generic-K Map<K: Hashable & Eq, V> + teko::env = Map<str, str?>
   INDEPENDENT:     #4  (codegen-only, 1 arm)    #5 (checker-only, 1 fn)     #2 (checker + mono)
```

**Recommended order (each independently gate-able):**
1. **#3 first** (mono `call_ns` re-key) — smallest ROOT-DISPATCH fix, unblocks `List`/`Map` shedding the free-function workarounds (`map_find_index`, `arr_*` can become private instance methods), and is the substrate #1 reuses.
2. **#1 second — THE FLAGSHIP** (structural-trait → synthetic interface surface + reuse #3's re-key) — unblocks generic-`K` Map. CHECKER-heavy.
3. **#4 + #5 in parallel** (both single-site, orthogonal, one codegen / one checker) — unblock generic collections as free-fn params and cross-ns factories.
4. **#2 last** (nested generic construct) — the widest (checker resolve + mono subst extension), lowest #163 urgency (parallel-array representation is a standard hash-map shape; the nested-`Entry<V>` form is a nicety, not a blocker).

**Checker-only:** #1 (+ shared mono re-key), #5. **Codegen-only:** #4. **Both:** #2, #3.
**Blocking relation:** #1 is BLOCKED by #3's re-key (a structural method call on `K` bound to a concrete type re-keys through the same path). Everything else is independent. **None blocks the OOP hard-cut** (D27-owner) — that is a pure front-end rename (see Law notes).

---

## GAP #3 — sibling instance method on `self` (mono call_ns re-key) — **codegen symptom, mono root**

**Root (confirmed):** `stamp_one_instance_method` (`monomorph.tks:972`) walks the template method body through `mono_block`/`mono_texpr`, but `mono_texpr`'s TCall arm (`monomorph.tks:568,571`) carries `call_ns = cl.call_ns` UNCHANGED. A sibling call `self.count()` in `Holder`'s template was typed to `call_ns = <canon Holder>` (the direct-dispatch callee, `typer.tks:691`); the stamp never rewrites it to `Holder__g__i64`. The emitted method is `cb_fn_name(namespace = inst_name = "Holder__g__i64", "count")` (`monomorph.tks:983` sets `namespace = inst_name`) → C symbol `Holder__g__i64__count`, but the call emits `cb_fn_name("<canon Holder>", "count")` → a different symbol → "undeclared identifier".

**Crumb 3.1 — re-key a stamped instance-method body's OWN-TYPE calls to the instance.**
The instance-method Subst (`instance_method_subst`, `monomorph.tks:1000`) already binds `canon Holder → Named{Holder__g__i64}` and `bare Holder → Named{…}` for the RECEIVER/return-type remap. The fix threads that SAME remap into the TCall's `call_ns` string during `mono_texpr`. Add a Subst-driven `call_ns` rewrite: when `call_ns` (or the callee's qualifier segment) names a type the Subst remaps to a `Named`, rewrite it to that `Named`'s name.

```teko
/**
 * Rewrite a stamped body's TCall namespace key through the instance Subst — a sibling instance-method
 * call (`self.count()` inside `Holder__g__i64::twice`) was typed against the ABSTRACT template, so its
 * `call_ns` still names the template owner (`ns::Holder`); the stamped `count` is emitted under the
 * instance namespace (`Holder__g__i64`), so the call must key off the instance too or the C symbol
 * mismatches ("undeclared identifier"). The Subst already carries `ns::Holder -> Named{Holder__g__i64}`
 * (instance_method_subst's receiver remap); this reuses it for the call key. A `call_ns` the Subst does
 * not remap passes through UNCHANGED, so every free-fn / cross-type call is byte-identical.
 *
 * @param call_ns  the TCall's current namespace key (the abstract owner for a sibling call)
 * @param s        the instance-method Subst (carries `owner-canon -> Named{instance}`)
 * @return         the instance namespace when `call_ns` is the remapped owner, else `call_ns` unchanged
 * @since generic-stack-completion (#163 follow-up, gap #3)
 */
fn mono_rekey_call_ns(call_ns: str, s: Subst) -> str {
    if call_ns.len == 0 { return call_ns }
    match subst_find_inst(s, call_ns) {
        Named as n => n.name
        _ => call_ns
    }
}
```
Wire in BOTH TCall result arms of `mono_texpr` (`monomorph.tks:568` and `:571`): replace `call_ns = cl.call_ns` with `call_ns = mono_rekey_call_ns(cl.call_ns, s)`, AND rewrite the callee's qualifier segment (the second-to-last, when present) via the same lookup so `cb_fn_name`'s ns and the callee path agree. `subst_find_inst` (`monomorph.tks:282`) already resolves a name → its remapped `Type` over the Subst's `names`/`types`; the receiver remap registered `ns::Holder` in both `params` and `names` (`subst_add_type_remap`, `monomorph.tks:1023`), so the lookup hits.

> **GROUNDING GAP (implementer confirms FIRST):** verify the sibling call's `call_ns` is the CANONICAL owner (`typer.tks:691` `method_dispatch_callee` places the canonical `struct_name` as the qualifier). If typing instead keys the sibling call on the BARE owner, add the bare remap to the lookup (the Subst already carries the bare spelling too, `monomorph.tks:1006-1007`). Probe: `TEKO_TRACE` a 2-method generic class, read the pre-mono `call_ns` on the sibling TCall.

**Fixtures (VM==native):**
- `examples/regressions/generic_sibling_method/` — `type Ctr<T> = class { intern n: i64; pub fn make() -> Ctr<T> { Ctr { n = 0 } }; pub fn count(self) -> i64 { self.n }; pub fn twice(self) -> i64 { self.count() + self.count() } }`; `Ctr<i64>::make().twice()` → **exit 0** (or seed `n=3` via a setter for exit 6). Fails to LINK natively today (VM passes), passes both after.
- Fold-in note (report in PR body, do NOT silently rewrite): after #3, flip `src/collections/{list,collections}.tks`'s `arr_replace_at`/`arr_drop_at`/`arr_drop_last` from free functions back to private instance methods IF it keeps the corpus building — that IS the #163-workaround closure. Deferred to the #163-follow-up PR that adopts these fixes; keep the free-fn form until the fix is green (do not gate #3 on the collections rewrite).

**Ritual:** full gate. `mono_rekey_call_ns` is a no-op for every `call_ns` the Subst does not remap (every existing call) → gen1==gen2 byte-identical.

---

## GAP #1 — structural-trait dispatch on a type param (THE FLAGSHIP) — **checker + #3's re-key**

**Root (confirmed, 4 sites):**
- `atom_surface` (`resolve.tks:327`) — `is_structural_trait(name)` → returns an EMPTY surface (opaque-by-design, TR3 v1).
- `constraint_interfaces` (`resolve.tks:487-492`) — a structural atom matches neither `is_trait_name` nor `is_interface_name` → returns `no_interfaces` (empty) → the type-param's `extends` is empty.
- `type_param_reg` (`resolve.tks:568-569`) — empty `extends` → `ExternBody` (opaque honest stop), never an `InterfaceBody` → `type_method_call` (`typer.tks:722`) hits `type_param_call_error` / the "not guaranteed by the constraint" stop.
- `type_binary` `==` (`typer.tks:263-268`, `type_compare` `typer.tks:324`) — a `K == K2` on a type-param operand falls through to the "operands must be the same type" / opaque-operand honest stop; there is no structural-`Eq` authorization.

**The synthesis already exists** (`synth.tks:629` `synthesize_structural_methods`): every concrete deriver of `Eq`/`Hash`/… ALREADY carries a real `eq`/`hash`/`compare`/`clone`/`default` method. So #1 is a ROUTING fix: make `<K: Hashable & Eq>` authorize `k.hash()`/`k.eq(k2)` on `K`, and lower `k == k2` to `k.eq(k2)`; the mono pass (with #3's re-key) then dispatches to the concrete stamp's `eq`/`hash`.

**Crumb 1.1 — a structural-trait atom contributes a SYNTHETIC method surface (checker authorizes the call).**
Give `atom_surface` a structural arm that returns the trait's provided-method SIGNATURE (`eq(self, other: Self) -> bool`, `hash(self) -> u64`, `compare(self, other: Self) -> i64`, `clone(self) -> Self`, `default() -> Self`) BEFORE the opaque `is_structural_trait` fallthrough. These are synthetic `parser::Function` sigs (no body — the surface is a CONTRACT the checker authorizes; the concrete method is dispatched at mono).

```teko
/**
 * (gap #1) The method SURFACE a compiler-known structural trait authorizes on a constrained type param
 * — the same provided-method signatures `synthesize_structural_methods` stamps on each concrete deriver
 * (`Eq`→`eq(self, other) -> bool`, `Hash`→`hash(self) -> u64`, `Ord`→`compare(self, other) -> i64`,
 * `Clone`→`clone(self) -> Self`, `Default`→`default() -> Self`), but as BODYLESS contract signatures so a
 * `<K: Hashable & Eq>` body may call `k.hash()`/`k.eq(k2)` on `K`. The concrete method already exists on
 * every deriver (synth.tks); this only opens the type-param surface, so the mono re-key (gap #3) can
 * dispatch the call to the stamped concrete method. NOT a dynamic vtable — a monomorphization gate (D22).
 *
 * @param name   the structural-trait atom name (a synonym is canonicalized)
 * @param table  the folded type table (Self-type construction)
 * @return       the trait's provided-method signatures as a bodyless surface
 * @since generic-stack-completion (#163 follow-up, gap #1)
 */
fn structural_trait_surface(name: str, table: TypeTable) -> []parser::Function {
    let canon = structural_trait_canonical(name)
    mut out: []parser::Function = teko::list::empty()
    out = teko::list::push(out, structural_sig(canon))
    out
}
```
`structural_sig(canon)` builds the bodyless `parser::Function` for the one provided method (`structural_method_name(canon)`, `synth.tks:658`), receiver `self` + the canonical param/return types. Reuse the exact param/return shapes `synthesize_one` (`synth.tks:593`) already constructs so the surface and the stamp agree by construction. Wire at `resolve.tks:327`: replace the empty-surface line with `if is_structural_trait(name) { return structural_trait_surface(name, table) }`.

**Crumb 1.2 — a structural-trait atom contributes a SYNTHETIC INTERFACE to `extends` (so dispatch resolves).**
`constraint_interfaces` (`resolve.tks:487`) must, for a structural atom, contribute a synthetic contract name so `type_param_reg` builds an `InterfaceBody` (not `ExternBody`) and `resolve_iface_dispatch`/`method_owner_interface` find the method. Two viable shapes, RATIFIABLE below:

- **(A) Register the 5 structural traits as real synthetic `InterfaceBody` TypeDecls in the folded table** (at the fold, `collect.tks`), so `is_interface_name`/`iface_methods_by_name` light up for `Eq`/`Hash`/… automatically and NO new arm is needed in `constraint_interfaces`/`atom_surface`/`resolve_iface_dispatch` (they already handle interfaces). The provided-method sigs come from crumb 1.1's builder. Cleanest — the structural traits become first-class contract atoms in the type surface, and `method_owner_interface` keys the (already-class-only, D22) dispatch off them.
- **(B) A dedicated structural arm in each of `constraint_interfaces`/`atom_surface`/`resolve_iface_dispatch`** carrying the synthetic surface without a table entry. More surgical, but duplicates the interface plumbing across three sites and risks drift.

**RATIFIABLE DECISION — pick (A), law-first.** (A) makes the structural trait a proper contract atom (M.2 explicit, M.3 honest — the surface IS the contract), reuses the entire interface-dispatch path unchanged (fewer new sites → less drift, the #296 lesson), and keeps the constraint a **monomorphization gate** exactly as D22 mandates: the synthetic interface is NEVER emitted as a runtime vtable for a struct/primitive `K` (the mono re-key rewrites `k.hash()` to the concrete `str::hash`/`Point::hash` DIRECT stamped call — see crumb 1.4). Dynamic vtable stays class-only. **No owner HALT** — this is the same ruling D22 already ratified, extended from user-declared contracts to compiler-known structural ones. Logged for LTS review.

```teko
/**
 * (gap #1, decision A) Fold the five compiler-known structural traits into the type table as synthetic
 * bodyless-method INTERFACE decls (`Eq`/`Ord`/`Hash`/`Clone`/`Default`), so a `<K: Hashable & Eq>`
 * constraint atom resolves through the EXISTING interface-dispatch path (constraint_interfaces →
 * type_param_reg InterfaceBody → resolve_iface_dispatch) with no new per-site arm. Idempotent — a
 * program that never constrains on a structural trait is unaffected (the decls are inert until a
 * constraint names one). The provided-method surface matches synthesize_structural_methods exactly, so
 * the type-param contract and the concrete deriver's stamp agree. The synonyms (`Hashable`/`Comparable`)
 * are registered as ALIASES to their canonical decl.
 *
 * @param table  the folded type table so far
 * @return       the table extended with the five synthetic structural-trait interface decls
 * @since generic-stack-completion (#163 follow-up, gap #1)
 */
pub fn register_structural_trait_interfaces(table: TypeTable) -> TypeTable { /* … */ }
```
Called once during the fold (`collect.tks`, alongside the existing type-table build). With (A), `constraint_interfaces`' `is_interface_name(a.name, table)` arm (`resolve.tks:489`) NOW matches a structural atom → contributes it to `extends` → `type_param_reg` builds the `InterfaceBody` → `type_method_call` dispatches. The `atom_surface` structural arm (crumb 1.1) becomes redundant with (A) but keep it as the single source of the provided-method sigs the registration reads (DRY).

**Crumb 1.3 — lower `k == k2` on a structural-`Eq`-constrained type param to `k.eq(k2)`.**
`type_binary`'s `==`/`!=` path (and `type_compare` `typer.tks:324` for `<`/`>` under `Ord`) must, when BOTH operands are the SAME type-param `Named{K}` and `K`'s constraint authorizes `Eq` (its synthetic surface has `eq`), desugar to a method call `K::eq(k, k2)` (a `MethodCall`/direct `Call`) instead of the "opaque operand" honest stop. Mirror how `~` desugars to `teko::string::concat` WHOLESALE before the operand-typing (`typer.tks:270`) — the desugar produces an ordinary `Call` the mono pass already handles.

```teko
/**
 * (gap #1) When both operands of `==`/`!=` are the SAME structural-`Eq`-constrained type param `K`,
 * desugar to `k.eq(k2)` (negated for `!=`) — a constraint authorizes the structural comparison exactly
 * as an interface constraint authorizes a method call. Reuses the interface-dispatch desugar (the call
 * lowers to `K::eq` on the type-param's synthetic InterfaceBody, then the mono re-key dispatches it to
 * the concrete deriver's `eq`). A no-op when either operand is not an `Eq`-constrained type param — every
 * primitive/native `==` (str, i64, …) keeps its existing lowering, so the corpus is byte-identical.
 *
 * @param b      the `==`/`!=` binary node
 * @param env    the typing environment (type-param constraints in scope)
 * @param table  the folded type table
 * @return       the typed `k.eq(k2)` desugar, or null when the operands are not an Eq-constrained param
 * @since generic-stack-completion (#163 follow-up, gap #1)
 */
fn type_eq_via_structural_trait(b: parser::Binary, env: Env, table: TypeTable) -> TExpr? | error { /* … */ }
```
Call at the head of `type_binary`'s equality path; on a non-match (`null`) fall through to the existing native `==`. Guard tightly: BOTH operands must type to the SAME `Named{K}` whose synthetic surface contains `eq` — a mixed or non-type-param operand is untouched.

**Crumb 1.4 — mono re-keys `k.hash()`/`k.eq()` on a CONCRETE-bound `K` to the deriver's stamped method.**
This is where D22 is enforced: when `K` is bound to a concrete type (e.g. `str`, or a derived `Point` struct), `rekey_iface_dispatch` (`monomorph.tks:481`) already rewrites a constraint-bound STRUCT dispatch to a DIRECT stamped-method call (`rekey_struct_constraint_dispatch`, `monomorph.tks:450` — #294's crumb). Extend it so a `K` bound to a PRIMITIVE (`str`/`i64`/…) whose structural method is a runtime builtin (`str::hash` → `teko::runtime::str_hash`, `str == str` → `tk_str_eq`) re-keys to that builtin, and a `K` bound to a struct/class deriver re-keys to its synthesized `Point::eq`/`Point::hash` stamp. NO fat-pointer vtable is ever emitted for `K` — the constraint is a gate.

```teko
/**
 * (gap #1, D22) Re-key a structural-trait method call on a constraint-bound type param to the CONCRETE
 * dispatch target — a builtin for a primitive `K` (`str.hash()` → `teko::runtime::str_hash`, `str.eq()`
 * → `tk_str_eq`) or the synthesized method stamp for a struct/class deriver (`Point::eq`). A constraint
 * is a monomorphization GATE, not a dynamic-dispatch promotion (D22): no `tk_vt_<K>_<Trait>` vtable is
 * ever emitted for a structural-trait-constrained param. Extends rekey_struct_constraint_dispatch's
 * struct path with the primitive-builtin path. A no-op for a non-structural interface dispatch.
 *
 * @param cl     the interface-dispatch TCall (structural-trait method, args rewritten to concrete)
 * @param args   the rewritten args (args[0] is the receiver, concrete type)
 * @param table  the folded type table (classifies K as primitive vs struct vs class)
 * @return       a DIRECT builtin/stamped-method TCall for a concrete K, else `cl`
 * @since generic-stack-completion (#163 follow-up, gap #1)
 */
fn rekey_structural_trait_dispatch(cl: TCall, args: []TExpr, table: TypeTable) -> TCall { /* … */ }
```
Call it in `rekey_iface_dispatch` alongside `rekey_struct_constraint_dispatch`. For a primitive `K`, map the (canonical trait, method) pair to its runtime builtin symbol; for a struct/class deriver, reuse the struct-direct path (the concrete `eq`/`hash` was synthesized onto the deriver by `synth.tks` and stamped by #254). **Depends on #3's `mono_rekey_call_ns` for the sibling case inside a chained structural method.**

**Fixtures (VM==native):**
- `examples/regressions/generic_key_map/` — the PAYOFF: `type Map<K: Hashable & Eq, V> = class { … pub fn insert(self, k: K, v: V) { let h = k.hash(); … if k.eq(existing) { … } } }`; a `Map<str, i64>` and a `Map<Point, i64>` (a struct deriving `Hashable & Eq`); insert/get round-trip → **exit** the summed values. Rejects/diverges today (map.tks's own workaround comment is the witness); passes both after.
- `examples/regressions/type_param_eq/` — `fn same<K: Eq>(a: K, b: K) -> bool { a == b }`; `same(3, 3)` and `same("x", "x")` → exit 1; `same(3, 4)` → exit 0 (proves crumb 1.3 + 1.4 primitive path).
- `examples/regressions/type_param_hash_struct/` — a struct `Point` deriving `Hashable & Eq`, `fn h<K: Hashable>(k: K) -> u64 { k.hash() }`, `h(Point{x=1;y=2})` (proves crumb 1.4 deriver path).
- Fold-in note (report in PR body): after #1, `teko::env` (`src/env/`) can adopt `Map<str, str?>` generically ([[teko-env-as-map]] unblocked) — DO NOT rewrite env in this PR; report the unblock.

**Ritual:** full gate at EACH crumb (1.1→1.4 independently gate-able). The structural-trait interface registration is inert unless a constraint names one → gen1==gen2 byte-identical (the compiler corpus has structural DERIVERS from #177 but no structural-CONSTRAINED type params). Verify the no-op at 1.2 specifically (the widest table change).

---

## GAP #4 — generic instance as a free-fn param (codegen, 1 arm) — **INDEPENDENT**

**Root (confirmed, 1 site):** `emit_type_expr` (`codegen.tks:1277`): `if nt.args.len > 0 { return mangle_type_name(buf, "", cg_texpr_inst_name(last, nt.args)) }` emits `tk_t_Map__g__i64` BY VALUE and returns BEFORE the ClassBody-pointer arm (`:1283-1284`). The bare stamped form `Map__g__i64` (`nt.args.len == 0`, what `subst_typeexpr` produces for a substituted param) DOES reach `:1283-1284` → `Map__g__i64 *`. So a param a HUMAN writes as `Map<i64>` (args>0) mismatches a param the mono pass produced (bare) — value vs pointer.

**Crumb 4.1 — the `<args>` generic-instance form must honor class-ness (emit a pointer for a class instance).**
After computing the mangled instance name, apply the SAME ClassBody-pointer check the bare arm applies, so a generic CLASS instance param is a pointer and a generic STRUCT instance stays by-value.

```teko
// emit_type_expr (codegen.tks:1277) — replace the args>0 early-return with a class-aware emit:
if nt.args.len > 0 {
    let inst = cg_texpr_inst_name(last, nt.args)
    // (gap #4) a generic CLASS instance is a REFERENCE type → a pointer, exactly like the bare
    // stamped form (`Map__g__i64 *`); a generic STRUCT instance stays a by-value aggregate. Without
    // this the two spellings of the same instance (`Map<i64>` vs `Map__g__i64`) disagree on ptr-vs-value.
    if cg_is_class_named(prog, inst) { return cb(mangle_type_name(buf, "", inst), " *") }
    return mangle_type_name(buf, "", inst)
}
```
`cg_is_class_named` already resolves the bare stamped name against the appended instance TypeDecl (`monomorph.tks:1240`, body kind preserved by `subst_body_names`, `resolve.tks:1594`). No new lookup.

> **GROUNDING GAP (implementer confirms FIRST):** the stamped instance decl must be in `prog` BEFORE `emit_type_expr` runs (it is — mono appends it, `monomorph.tks:1233-1241`). Verify the `emit_type` SEMANTIC twin (`codegen.tks:926`) is already correct for the resolved `Named{Map__g__i64}` (it is — it goes straight to the class check); #4 is purely the SYNTACTIC path.

**Fixtures (VM==native):**
- `examples/regressions/generic_class_param/` — `type Bag<T> = class { intern xs: []T; pub fn make() -> Bag<T> { Bag { xs = teko::list::empty() } }; pub fn add(self, x: T) { self.xs = teko::list::push(self.xs, x) } }` + a FREE fn `fn size(b: Bag<i64>) -> u64 { b.xs.len }`; `let g = Bag<i64>::make(); g.add(5); size(g)` → **exit 1**. cc-rejects today (ptr/value mismatch), passes both after. Include a generic STRUCT param variant to prove structs stay by-value (no regression).

**Ritual:** full gate. The compiler corpus has NO generic-instance params → the new class-pointer arm is a no-op there; gen1==gen2 byte-identical.

---

## GAP #5 — cross-namespace generic factory (checker, 1 fn) — **INDEPENDENT**

**Root (confirmed, 1 site):** `retarget_generic_static_callee` (`typer.tks:932`) builds `NamedType { path = single_seg_path(base); args = c.type_args }` using ONLY the second-to-last segment's bare name (`List`), DROPPING every leading qualifier segment, then `resolve_type(..., ref_ns)` (`:933`) resolves it in the CALLER's namespace → `ns::List<i64>::make()` from another ns fails with "not visible bare".

**Crumb 5.1 — preserve the full owner qualifier when resolving the factory's generic instance.**
Build the `NamedType` path from ALL segments up to and including `owner_idx` (the full `ns::…::List`), not just the bare base. The rest of the fn is unchanged.

```teko
/**
 * (gap #5) Retarget a static generic-factory callee (`ns::List<i64>::make()`) to its mangled instance
 * owner, PRESERVING the full namespace qualifier so a cross-namespace factory resolves against the
 * generic's OWN namespace (not the caller's). The prior form dropped every segment before the base,
 * so `ns::List<i64>::make()` from another namespace failed "not visible bare" (#109/#290 family). A call
 * with no type-args or a single-segment callee passes through unchanged (byte-identical to today).
 *
 * @param c      the parsed static call (carrying type_args)
 * @param table  the folded type table
 * @param ref_ns the call's enclosing namespace (only the tie-breaker; the qualifier now drives resolution)
 * @return       the callee with the owner segment mangled to the instance, or the callee verbatim
 * @throws       when a type-arg fails to resolve
 * @since generic-stack-completion (#163 follow-up, gap #5)
 */
fn retarget_generic_static_callee(c: parser::Call, table: TypeTable, ref_ns: str) -> parser::Path | error {
    if c.type_args.len == 0 || c.callee.segments.len < 2 { return c.callee }
    let owner_idx = c.callee.segments.len - 2
    let owner_path = path_prefix_through(c.callee, owner_idx)   // segments[0 ..= owner_idx] — the FULL ns::…::Base
    let nt = parser::NamedType { path = owner_path; args = c.type_args }
    let rt = match resolve_type(nt, table, ref_ns) { Type as t => t; error as e => return e }
    let inst = match rt { Named as n => n.name; _ => return c.callee }
    rewrite_segment_at(c.callee, owner_idx, inst)
}
```
`path_prefix_through(p, idx)` = a `parser::Path` of `p.segments[0..=idx]` (trivial slice-build; add if absent). `resolve_type` on the QUALIFIED `NamedType` resolves in the owner's ns via the existing `#109 W1` qualified path (`resolve.tks` resolve_named honors the qualifier), so `ref_ns` becomes a mere tie-breaker.

**Fixtures (VM==native):**
- `examples/regressions/cross_ns_generic_factory/` — namespace `coll` declares `type Stack<T> = class { … pub fn make() -> Stack<T> { … } }`; a DIFFERENT namespace calls `coll::Stack<i64>::make()` → **exit 0** (or push/len for a nonzero). Fails resolution today, passes both after.

**Ritual:** full gate. Corpus factories are single-ns today → the qualified form is byte-identical for a bare-base call (the `len < 2` / qualifier-equals-ref_ns paths). gen1==gen2 preserved.

---

## GAP #2 — nested generic construct in a generic method (checker + mono) — **WIDEST, LAST**

**Root (confirmed):** `type_struct_lit` (`typer.tks:1587`) resolves `Cell` via `resolve_named(sl.type_path, table, env.cur_ns)` — the USING ns. The self-construct phantom (`is_self_generic_construct`, `typer.tks:1635`) ONLY fires for the OWN type (`base_name == owner_type`), so a DIFFERENT generic base (`Cell` inside `Holder<V>::m`) falls to the annotation branch (`typer.tks:1638-1651`) which needs a stamped `Cell__g__V` phantom that does not exist at abstract-typing time → the `resolve_named` (or the `type_table_find` at `:1646`) fails "not visible bare". The mono self-construct extension (`instance_method_subst_l5`, `monomorph.tks:1043`) likewise builds ONLY the OWN-type phantom.

**Crumb 2.1 — resolve a nested generic construct in the OWNER's namespace + phantom-stamp it under the owner's params.**
Two coordinated edits (checker authorizes the abstract form; mono retargets the phantom to concrete):
- **Checker:** in `type_struct_lit`, when the target is a generic type OTHER than the owner AND its type-args mention the owner's OWN type-params (`Cell<V>` where `V` is `Holder`'s param), build a phantom instance name `Cell__g__V` (the same `phantom_self_inst_name` shape, over the NESTED type's base and the owner's param spelling) and type against it — instead of requiring an already-stamped concrete instance. Resolve `Cell`'s decl via the OWNER's ns (or the nested type's own ns), not `env.cur_ns` alone.
- **Mono:** extend `instance_method_subst_l5` (`monomorph.tks:1043`) to ALSO remap every NESTED-generic phantom the method body constructs (`Cell__g__V → Cell__g__<concrete>`) — currently it only adds the OWN-type phantom. Discovery: the nested construct must ALSO enqueue `Cell__g__<concrete>` as a MonoInst so its type-decl + methods are stamped (mirrors the free-generic-call discovery in `mono_texpr`).

```teko
/**
 * (gap #2) The phantom instance name for a NESTED generic construct inside a generic method — `Cell<V>`
 * built inside `Holder<V>::wrap` tags the literal `Cell__g__V` (the nested base + the OWNER's param
 * spelling), so the abstract method body type-checks before any concrete stamp exists, exactly as the
 * OWN-type self-construct already does. At mono the L5 Subst remaps `Cell__g__V -> Cell__g__<concrete>`
 * and enqueues that instance for stamping. A no-op when the construct is the owner's OWN type (handled
 * by the existing self-construct phantom) or names no owner type-param.
 *
 * @param nested_base   the nested generic type's base name (`Cell`)
 * @param owner_tparams the enclosing method's owner type-params (`[V]`)
 * @return              the phantom instance name (`Cell__g__V`)
 * @since generic-stack-completion (#163 follow-up, gap #2)
 */
fn nested_phantom_inst_name(nested_base: str, owner_tparams: []str) -> str { /* reuse generic_inst_name */ }
```

> **GROUNDING GAP (implementer probes FIRST):** confirm the mono phantom-remap covers the DISCOVERY side — the nested construct must enqueue `Cell__g__<concrete>` (its type-decl AND its methods) or the stamp is missing at emit. Reuse `collect_body_insts` (`resolve.tks:1424`, extended in #254 L2 to walk method bodies) so the nested construct inside a method body is discovered. If the nested type's ns differs from the owner's, the `resolve_named` ref_ns must be the NESTED type's own ns (via its qualifier), the #5 fix's sibling.

**Fixtures (VM==native):**
- `examples/regressions/nested_generic_construct/` — `type Cell<T> = class { intern v: T; pub fn make(x: T) -> Cell<T> { Cell { v = x } }; pub fn read(self) -> T { self.v } }` + `type Holder<T> = class { intern c: Cell<T>; pub fn make(x: T) -> Holder<T> { Holder { c = Cell<T>::make(x) } }; pub fn get(self) -> T { self.c.read() } }`; `Holder<i64>::make(9).get()` → **exit 9**. "not visible bare" today, passes both after.
- Fold-in note (report in PR body): after #2, `Map<V>` MAY adopt a `[]Entry<V>` single-array store (map.tks's parallel-array workaround comment is the witness) — DO NOT rewrite Map here; report the unblock. Parallel arrays remain a valid representation, so this is a nicety, not required.

**Ritual:** full gate. Corpus has no nested generic constructs → the nested-phantom path is a no-op; gen1==gen2 byte-identical. This is the WIDEST edit (checker resolve + mono subst + discovery) — gate it LAST and independently.

---

## LAW / RISK NOTES

- **D22 (constraint = monomorphization gate, NOT dynamic vtable) — the load-bearing law for #1.** The structural-trait synthetic interface (crumb 1.2, option A) must NEVER lower to a runtime `tk_vt_<K>_<Trait>` for a struct/primitive `K`; crumb 1.4 rewrites every `k.hash()`/`k.eq()` on a concrete-bound `K` to a DIRECT builtin/stamped-method call at mono. This is the SAME ruling D22 ratified for #294 structs, extended from user contracts to compiler-known structural traits — **no new owner HALT** (logged for LTS review as an extension of D22). Dynamic vtable dispatch stays class-only, unchanged.
- **No-GC / native-authoritative (`teko-no-gc-vm-role`):** every fix is a compile-time stamp/re-key/emit change; no runtime metadata, no allocation-model change. Native is authoritative on divergence; every fixture is VM==native.
- **OOP hard-cut interaction (D27-owner, RATIFIED 2026-07-06):** the `this`/`base`/`static` hard-cut is a PURE FRONT-END RENAME (receiver = `params[0]` positional; codegen/VM read positionally, never by name) → codegen/VM byte-identical → **orthogonal to and fixpoint-safe against every fix here** (#254/#294 needed zero change; so do these). BUT the hard-cut's mechanical codemod WILL rewrite generic-class method bodies (rename `self`→`this`). **Sequencing:** these five fixes are SEMANTIC (dispatch/emit/resolve); the hard-cut is SYNTACTIC (rename). Land the flagship chain (#3→#1) and #4/#5 on the OLD syntax (as #163 did), then let the hard-cut codemod rewrite the corpus atomically (D27-owner's plan). If the hard-cut lands FIRST, these snippets' `self` receivers become `this` — a trivial rename in the fixtures, no logic change. **No law tension.** Recommend: hard-cut BEFORE #2 (the widest, latest), so #2's new fixtures are written in the new syntax once.
- **Risk — the structural-trait table registration (crumb 1.2) is the widest table change.** Mitigation: the five synthetic decls are INERT unless a constraint names one; the `any_generic` guard + gen1==gen2 is the loud tripwire (the compiler corpus derives structural traits from #177 but never CONSTRAINS a type param on one). Verify the no-op at crumb 1.2 in isolation before 1.3/1.4.
- **Risk — `#254 L4 Env.expected_ret` churn** (DECISION_LOG.md:245, "alta rotatividade") is ADJACENT: #2's return-position nested construct may lean on the same expected-type thread. If L4's `Env` field is still open, #2's checker crumb must set `env.expected_ret` for the nested construct's return-typed field — sequence #2 AFTER L4 is confirmed stable. Reported, not turned into a new issue.
- **Risk — the #296 lesson (count sites, do not trust "1 crumb").** Verified counts are in the root-cause table; every fix here is 1–4 sites, re-verified at file:line. The one to eyeball hardest is #1 crumb 1.2 (three interface-plumbing sites collapse to ZERO new arms under option A — confirm `is_interface_name`/`iface_methods_by_name`/`resolve_iface_dispatch` all light up for the registered synthetic decls).

## WHAT REMAINS BLOCKED (design-ahead honesty)

- **Nothing external blocks this cluster** — K-A (#290/#301/#254/#294) is fully landed; #163 shipped. All five fixes are specified against the current tree and startable now, in the recommended order.
- **#1 crumb 1.4 primitive-builtin map is PROBE-GATED:** confirm the exact runtime symbols for each (structural-trait, primitive) pair (`str.hash()` → `teko::runtime::str_hash` is confirmed used by map.tks; verify `i64`/`u8`/… have a structural `hash`/`eq` builtin or synthesize one). If a primitive lacks a builtin structural method, the pre-alpha scope may restrict structural-`K` to `str` + struct/class derivers first (map.tks already proves `str`), deferring numeric-`K` to a follow-up — REPORT which primitives are covered, do not silently narrow.
- **#2 gated on #254 L4 `Env.expected_ret` stability** (see risk above) — confirm before the checker crumb.
