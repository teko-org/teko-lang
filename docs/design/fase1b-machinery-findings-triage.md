# fase1b machinery-findings triage — F1..F4 (post-G5 drain)

> **Status:** TRIAGE + FIX-DESIGN (architect). Read-only on product `.tks`; this doc is the sole edit.
> No build, no reseed, `teko test` NOT run. Every claim grounded at `file:line` on `fix/retirement`
> **HEAD `f177be09`** (`git rev-parse HEAD` — fase1b G1-G5 drained). Triages the four compiler-machinery
> findings the fase1b implementer surfaced (F1 union-of-constrained-`T`, F2 mono re-key misfire, F3
> factory-owner-type-param, F4 class-instantiation constraint check). OOP hard-cut is LANDED (`self`
> reserved, `static fn` factories, `.{ }` construct); every snippet below is post-hard-cut + full-Javadoc.

---

## Verdict table (read this first)

| # | one-line root | root `file:line` (HEAD f177be09) | classification | compiler-touching? | reseed? | owner-ruling? |
|---|---|---|---|---|---|---|
| **F1** | a constrained type-param `T: I` is registered with an `InterfaceBody`, so `is_interface_name("T")` is TRUE and the union-member gate rejects `T \| null` as "an interface" | `resolve.tks:2132-2134` (gate) ← `resolve.tks:1200-1202` (`type_param_reg` builds `InterfaceBody`) ← `resolve.tks:1527-1531` (`is_interface_name` body-kind probe) | **IN-SCOPE BUG FIX** (a false-positive narrowing; NOT the deferred #28 interface-in-union feature) | YES (checker) | YES | **NO** |
| **F2** | (as reported) same free generic instantiated at owner-`T` and concrete `[]u64` in one instance method mis-stamps the concrete arm | **NOT reproducible by static read on HEAD** — the free-generic stamp path is correct (see §F2) | **ACCEPTABLE-WITH-WORKAROUND-DEFER** + LIVE-PROBE-REQUIRED | probe-gated | probe-gated | NO |
| **F3** | a static factory `Dict<StrKey,V>::make()` retargets to the PHANTOM owner `Dictionary__g__StrKey__V`, whose `make` was never registered → `unknown function: make`; mono never concretizes the phantom qualifier | `typer.tks:3143-3146` (retarget builds phantom) → `typer.tks:3486`/`:3494` (`lookup_call` fails → "unknown function"); mono gap `monomorph.tks:428-435`/`:407-413` (re-key uses exact `subst_find_inst`, not `subst_instance_name`) | **IN-SCOPE BUG FIX** — the real Doc-2 "prepare the terrain" blocker, **G6 depends on it** | YES (checker + mono) | YES | NO |
| **F4** | only FREE-generic fn instantiations are constraint-checked (`monomorph.tks:1534`); generic CLASS instantiations (`instantiate_types`) skip it → `Dictionary<Plain,i64>.hash()` LINK-fails (`undefined reference to Plain__hash`) instead of a clean compile-time stop | `resolve.tks:3320-3331` (`stamp_inst_site` stamps a class instance, no constraint check) vs `monomorph.tks:1534` (the free-generic check that DOES fire) | **IN-SCOPE BUG FIX** (M.1 fail-loud: a compile-time constraint escapes to link time) | YES (checker) | YES | NO |

**None of the four is owner-ruling.** F1 reads as a deferred feature because of the `yet` in the message,
but the deferred feature (#28 S4, real interface VALUES in a union) is a *different* code path that this
gate happens to also guard; a constrained type-param erases to a concrete member at mono and is not that
feature (see §F1). F2 does not reproduce statically. F3/F4 are the same monomorphization-cluster family
already ratified under D22/#254 — extensions, not new law.

---

## F1 — interface-constrained type-param cannot be a union member

### Root (re-verified)
`variant_member_admissible` (`resolve.tks:2123`) rejects any union member whose resolved `Named.name` is
`is_interface_name(nm.name, table)` (`resolve.tks:2132-2134`). The bug: a **constrained** type-param is
registered by `type_param_reg` (`resolve.tks:1199`) with `body = parser::InterfaceBody { extends = ifaces }`
whenever its constraint contributes ≥1 interface (`resolve.tks:1200-1202`). `is_interface_name`
(`resolve.tks:1527-1531`) is a pure body-kind probe — it returns **true for that synthetic decl**. So
resolving the return type `T | null` of `first_or_null<T: IOrd>(): T | null` resolves the member `T` to
`Named{"T"}`, `is_interface_name("T")` is true, and the gate fires **"an interface cannot be a variant
member yet"**. An *unconstrained* `T` gets an `ExternBody` (`resolve.tks:1201`) → not an interface → would
pass; it is exactly the constraint (`T: IOrd`) that trips it.

### Why this is NOT the deferred #28 feature
The doc-comment at `resolve.tks:2112-2114` defers **interface VALUES in a union** (`I | error`, the #28 S4
carve-out that "rebases on top of null-union"). A constrained type-param is categorically different: at
monomorphization `T` is substituted to the concrete bound type (`subst_type`), so `T | null` becomes e.g.
`StrKey | null` — an ordinary nominal-variant null-union that the C3 machinery already ships (and that the
guard/`is_empty()` workaround proves is otherwise fine). The fix does not relax the interface-value carve-out
at all; it stops a **false positive** where a type-param is mis-identified as a real interface.

### Classification: IN-SCOPE BUG FIX (checker, reseed). NOT owner-ruling.
High leverage / low risk: unblocks the ubiquitous generic idiom `find/pop/peek/dequeue(): T | null` —
`heap_pop_min<T: IOrd>(): T | null`, `PriorityQueue.dequeue(): T | null`, `first_or_null<T: IOrd>(): T | null`
— letting fase1b drop the `is_empty()`-guarded bare-`T` workaround.

### Crumb F1.1 — admit a constrained type-param as a union member
A discriminator for "this `Named` is a type-param synthetic decl, not a real interface" already exists as
`is_type_param_decl` (`typer.tks:2460`: `decl.type_params.len == 0 && decl.type_constraints.len == 1`), but
that lives in `typer.tks` (ABOVE `resolve.tks` in the module DAG). Add the twin predicate in `resolve.tks`
and consult it in the gate.

```teko
/**
 * Is `name` a generic TYPE-PARAMETER's synthetic registration (a `type_param_table` decl) rather than a
 * genuine user interface? A constrained type-param `T: IOrd` is registered by `type_param_reg` with an
 * `InterfaceBody` (so `constraint_op_owner`/`type_contract_dispatch` can authorize calls on `T`), which
 * makes `is_interface_name` report it as an interface. The synthetic decl is uniquely shaped — no own
 * `type_params`, exactly one carried `type_constraints` entry (the param's own constraint) — where a real
 * interface decl carries none. Used to tell a constrained type-param apart from an interface at the
 * union-member gate, so a `T | null` return concretizes at mono instead of being rejected as "an interface".
 *
 * @param name   the resolved member name to classify
 * @param table  the folded type table (with the enclosing generic's type-params registered)
 * @return       true iff `name` resolves to a type-param's synthetic InterfaceBody decl
 * @since fase1b-machinery (F1)
 */
fn is_type_param_named(name: str, table: TypeTable): bool {
    match type_table_find(table, name, "") {
        parser::TypeDecl as td => td.type_params.len == 0 && td.type_constraints.len == 1
        error => false
    }
}
```

Wire it into `variant_member_admissible` (`resolve.tks:2132-2135`): a `Named` that is a type-param is
admissible (it erases to a concrete member at mono), so it must NOT hit the interface stop.

```teko
        Named as nm => {
            if is_interface_name(nm.name, table) && !is_type_param_named(nm.name, table) {
                return error { message = "an interface cannot be a variant member yet — bind the interface value on its own (no `I | error` unions)" }
            }
            null
        }
```

> **GROUNDING NOTE (implementer confirms):** the synthetic-decl shape check is the same one `typer.tks:2460`
> relies on; confirm no genuine user interface is authored with exactly one `type_constraints` entry (a
> generic interface `type I<A> = interface {…}` has non-empty `type_params`, so `is_type_param_named` stays
> false for it — the two are disjoint). The `_ => null` value-member arm and the `Reference`/`Void`/`Null`
> arms are untouched.

### Fixtures (inputs → native exit code)
- `tests/regressions/type_param_union_return/` — `first_or_null<T: IOrd>(xs: []T): T | null { if xs.len == 0 { return null }; xs[0] }`; a two-level match over `first_or_null([7,3])` returning the present value → **exit 7**; over `first_or_null<i64>(empty)` taking the `null` arm → **exit 0**. Rejects "an interface cannot be a variant member yet" today.
- `tests/regressions/heap_pop_min_option/` — the fase1b `heap_pop_min<T: IOrd>(heap: []T): T | null` shape restored from the guard-workaround; pop-min of `[5,2,9]` → **exit 2**; empty → the `null` arm.
- **Keep** a NON-type-param reject fixture (`diagnostics/iface_in_union/`: `var x: IOrd | null`) → still errors, proving the carve-out is only narrowed for type-params, not lifted.

### Ritual: full gate. The compiler corpus has no `T:Constraint | null` return today → the narrowed gate is a no-op there; gen1==gen2 byte-identical. Additive fase1b reseed once the collections adopt the `T | null` form.

---

## F2 — "mono re-key misfire" on same-free-generic-at-owner-`T`-and-concrete

### Re-verification: NOT reproducible by static analysis on HEAD f177be09
The workaround witness is `arr_drop_u64_at` (`collections.tks:53`, comment `:44-46`) standing in for
`arr_drop_at<u64>` in `HashSet.remove` (`hashset.tks:74-75`). I traced the exact path for two `arr_drop_at`
calls inside `HashSet__g__StrKey::remove` (one on `self.items: []T`→StrKey, one on `self.hashes: []u64`):

1. `stamp_one_instance_method` (`monomorph.tks:1287`) walks the body with the instance Subst `s`
   (`instance_method_subst_l5`, `monomorph.tks:1358`), which binds owner `T → StrKey` (recovered by
   field-unification in `instance_type_subst`, `monomorph.tks:1197`).
2. Each `arr_drop_at` call reaches the free-fn arm of `mono_texpr`'s `TCall` (`monomorph.tks:850-878`).
   The args are rewritten FIRST: `self.hashes`→`[]u64`, `self.items`→`[]StrKey`.
3. `mono_call_subst` (`monomorph.tks:696`) takes the **INFERRED** path (the source has no explicit `<…>`,
   so `cl.type_args` is empty — `typer.tks:3605` stores an empty list on the inferred generic call). The
   inferred path builds `infs` **fresh** (`monomorph.tks:718`, `params = gf.type_params`, empty names/types)
   and `unify`s each param pattern against the already-concrete arg (`monomorph.tks:723`, `unify` at
   `resolve.tks:1955`). It **does not read the outer `s`** — so the u64 call binds `T→u64`, the StrKey call
   binds `T→StrKey`, independently.
4. `mono_mangle_name` (`monomorph.tks:233`) yields `arr_drop_at__g__u64` and `arr_drop_at__g__StrKey` —
   distinct. The gap-#3 re-key on the callee (`mono_rekey_callee_qualifier`, `monomorph.tks:428`) and the
   ns (`mono_rekey_call_ns`, `:407`) are **no-ops for a single-segment free-fn callee** (`:429` `< 2`
   early-out; `:408` empty-`call_ns` early-out). PHASE-2 dedup keys on `(mangled, owner_ns)`
   (`mono_inst_seen`, `:262`) — the two mangles differ, no collision.

Every step is correct for the described case. The `T`-name collision (both `HashSet<T>` and `arr_drop_at<T>`
use `T`) does NOT manifest because the inferred Subst is fresh, not seeded from `s`. The reported error
`argument type mismatch` originates ONLY in `coerce_argument_into` (`typer.tks:7437`), called ONLY from the
typer (`:2995/:3076/:3638`) on the ABSTRACT body — where both calls type independently and correctly.

**Most likely truth:** the misfire was real at an INTERMEDIATE fase1b commit and was closed by the gap-#3
re-key landing (`mono_rekey_callee_qualifier`/`mono_rekey_call_ns`, `monomorph.tks:407/428`, added during
the G-drain), the workaround left in place. The current machinery handles owner-`T` + concrete correctly.

### Classification: ACCEPTABLE-WITH-WORKAROUND-DEFER + LIVE-PROBE-REQUIRED. NOT owner-ruling.
The `arr_drop_u64_at` workaround is harmless (a correct concrete twin) and can stay. Do NOT design a mono
edit against a non-reproducing symptom — that risks perturbing the byte-identical fixpoint for no gain.

### Crumb F2.0 (probe, gates any fix) — reproduce or retire
Build the minimal repro as a leaf `.tkt` and run it natively (single file, no `teko test .`):
```teko
type Box<T: IEq & IHash> = class {
    intern items: []T
    intern tags: []u64
    pub static fn make(): Box<T> { .{ items = teko::list::empty(); tags = teko::list::empty() } }
    pub fn drop_at(at: u64) {
        self.tags  = arr_drop_at(self.tags, at)   // concrete u64 arm
        self.items = arr_drop_at(self.items, at)  // owner-T arm (StrKey)
    }
}
```
`Box<StrKey>::make().drop_at(0)` over a 2-element seed → assert the surviving element → **exit that value**.
- If it **passes**: F2 is CLOSED by the landed re-key; report and DELETE `arr_drop_u64_at`, restore
  `arr_drop_at` at `hashset.tks:74`, re-gate. No compiler edit; no reseed.
- If it **reproduces**: instrument `mono_call_subst` (`monomorph.tks:696`) and `mono_mangle_name`
  (`monomorph.tks:233`) via `TEKO_TRACE` to capture the u64-arm `infs` and `mangled`; the fix is then a
  localized inference/mangle correction (checker; reseed) — but NOT designed here until the probe pins it.

### Ritual: probe first. No fixture beyond the probe until the root is confirmed on HEAD.

---

## F3 — factory-call owner-type-param → `unknown function: make` (G6 BLOCKER)

### Root (re-verified — the FACTORY-call form, gap-#5 cousin, NOT the struct-literal gap #2)
`Map<V>.to_dictionary(): Dictionary<StrKey, V>` bodies a static factory `Dictionary<StrKey, V>::make()`.
The call carries `owner_type_args = [StrKey, V]` where `V` is the owner's (or a free fn's) type-param.

1. `type_call` sees `owner_type_args.len > 0` and calls `retarget_generic_static_callee`
   (`typer.tks:3437` → def `:3139`). It resolves `NamedType{ path = Dictionary; args = [StrKey, V] }` via
   `resolve_type` (`:3144`) → `resolve_generic_inst` (`resolve.tks:2503`), which — because `V` resolves to
   an in-scope type-param `Named{V}` — returns the **PHANTOM** `Named{"Dictionary__g__StrKey__V"}`
   (`resolve.tks:2534-2535`, `generic_inst_name` mangling an abstract token; no error, no stamp).
2. `retarget` rewrites the owner segment to that phantom (`typer.tks:3145-3146`), giving callee
   `Dictionary__g__StrKey__V::make`.
3. `lookup_call` (`typer.tks:3486`) finds nothing under the phantom namespace (only CONCRETE instances get
   their `make` registered by `register_instance_methods`, `collect.tks:442`), `builtin_fn("make")` also
   misses, and it errors **`unknown function: make`** (`typer.tks:3494`).

Both-concrete `<StrKey, i64>` works because the phantom is then a **real** stamped instance
`Dictionary__g__StrKey__i64` whose `make` IS registered. There is no gap-#2-free spelling of the bridge:
the free-generic form fails identically (`fn to_dict<V>(…): Dictionary<StrKey, V>` hits the SAME phantom).
This is the STATIC-FACTORY analogue of the self-construct phantom that `type_struct_lit` already tolerates
(`typer.tks:2469`, `name_is_phantom_instance` `:2480`) and that mono re-mangles via `subst_instance_name`
(`resolve.tks:1866`) — the factory-call path never got the matching tolerate + re-key.

### Classification: IN-SCOPE BUG FIX (checker + mono, reseed). NOT owner-ruling. **G6 depends on it.**
This is the genuine Doc-2 "prepare the terrain" blocker of the four.

### Crumb F3.1 (checker) — TOLERATE a phantom-owner factory, dispatching to the abstract template
In `retarget_generic_static_callee` (`typer.tks:3139`), when the resolved instance owner is a phantom,
DO NOT rewrite to the un-registered phantom segment; leave the callee on the base so `lookup_call` finds the
TEMPLATE factory (`Dictionary::make`), and type the result as the phantom instance (mono concretizes it).

```teko
/**
 * Retarget a static generic-factory callee to its mangled instance owner — EXTENDED (F3) to tolerate a
 * PHANTOM owner. When an owner type-arg is an in-scope abstract type-param (`Dictionary<StrKey, V>::make()`
 * inside `Map<V>::to_dictionary`, or a free `fn f<V>()`), the resolved instance is a phantom
 * (`Dictionary__g__StrKey__V`) that no `instantiate_types` site stamps and whose `make` is unregistered.
 * Rewriting to it would fail `lookup_call` ("unknown function: make"); instead the callee is left on the
 * abstract base so `lookup_call` resolves the TEMPLATE factory, exactly as `type_struct_lit` tolerates a
 * phantom self-construct (typer.tks:2469). The mono pass re-mangles the phantom owner through the bound
 * `V` (`subst_instance_name`) and enqueues the concrete instance for stamping. A fully-concrete owner
 * (`<StrKey, i64>`) still rewrites to the real stamped instance — byte-identical to today.
 *
 * @param c      the parsed static call (carrying owner_type_args)
 * @param table  the folded type table (arg resolution + phantom classification)
 * @param ref_ns the call's enclosing namespace
 * @return       the callee mangled to a CONCRETE instance owner, or the callee verbatim for a phantom owner
 * @throws       when an owner type-arg fails to resolve
 * @since fase1b-machinery (F3), extending onda-3 (#254 L5)
 */
fn retarget_generic_static_callee(c: parser::Call, table: TypeTable, ref_ns: str): parser::Path | error {
    if c.owner_type_args.len == 0 || c.callee.segments.len < 2 { return c.callee }
    var owner_idx = c.callee.segments.len - 2
    var owner_name = c.callee.segments[owner_idx].name
    var nt = parser::NamedType { path = single_seg_path(owner_name); args = c.owner_type_args }
    var rt = match resolve_type(nt, table, ref_ns) { (@Type()) as t => t; error as e => return e }
    var inst = match rt { Named as n => n.name; _ => return c.callee }
    if name_is_phantom_instance(inst, table) { return c.callee }   // (F3) defer the stamp to mono; template factory resolves
    rewrite_segment_at(c.callee, owner_idx, inst)
}
```

**Result typing (same fn, at the call site `typer.tks:3504-…`):** when the owner was a phantom, the factory
resolves to the template `make` whose return type is `Dictionary<StrKey, V>` → its abstract-typed return
already carries the phantom `Named{"Dictionary__g__StrKey__V"}` (the template return resolves against the
in-scope `V`). No extra work — the phantom rides `TExpr.type` exactly as a self-return phantom does, so
`subst_type`/`subst_instance_name` retarget it at mono. Confirm the abstract template factory's return
resolves to that phantom (probe below); if the template return is spelled `Self`/`Dictionary<StrKey,V>` the
existing `self_inst_spelling`/`generic_inst_name` path already produces the matching phantom token.

### Crumb F3.2 (mono) — concretize a phantom factory owner in the call re-key
`mono_rekey_call_ns` (`monomorph.tks:407`) and `mono_rekey_callee_qualifier` (`monomorph.tks:428`) currently
match the owner name via **exact** `subst_find_inst` (`:409`/`:431`), which misses a phantom whole-name
(`Dictionary__g__StrKey__V` is not a Subst key). Fall the exact miss through to `subst_instance_name`
(`resolve.tks:1866`), which substitutes the phantom's embedded `V` token → `Dictionary__g__StrKey__i64`.

```teko
/**
 * Rewrite a stamped body's TCall namespace key through the instance Subst (F3-extended). The exact-match
 * path re-keys a sibling instance-method call (owner name in the Subst). When the key is instead a PHANTOM
 * generic-instance name — a static factory `Dictionary__g__StrKey__V::make()` whose owner embeds the bound
 * `V` — the exact match misses, so it falls through to `subst_instance_name`, which concretizes the phantom
 * token to `Dictionary__g__StrKey__i64`. A `call_ns` that is neither a Subst key nor a phantom passes
 * through UNCHANGED (byte-identical for every free-fn / cross-type call).
 *
 * @param call_ns  the TCall's current namespace key (an abstract owner or a phantom factory owner)
 * @param s        the instance/free-fn Subst (owner remaps + bound type-params)
 * @return         the concrete namespace key, else `call_ns` unchanged
 * @since fase1b-machinery (F3), extending generic-stack gap #3
 */
fn mono_rekey_call_ns(call_ns: str, s: Subst): str {
    if call_ns.len == 0 { return call_ns }
    match subst_find_inst(s, call_ns) {
        (@Type()) as ct => match ct { Named as n => n.name; _ => subst_instance_name(call_ns, s) }
        null => subst_instance_name(call_ns, s)   // (F3) phantom-owner factory qualifier
    }
}
```
Apply the identical exact-miss → `subst_instance_name(qual, s)` fallthrough in `mono_rekey_callee_qualifier`
(`monomorph.tks:428-435`) for the callee's second-to-last segment, so the emitted C symbol's ns and the
callee path both name `Dictionary__g__StrKey__i64`. `subst_instance_name` is a strict no-op for a non-phantom
name (`resolve.tks:1867` early-out), so the existing corpus stays byte-identical.

### Crumb F3.3 (mono discovery, PROBE-GATED) — enqueue the concrete factory instance
The concrete `Dictionary__g__StrKey__i64` (its type-decl + `make` + methods) must exist for the re-keyed call
to link. The instantiation fixpoint (`stamp_inst_site`, `resolve.tks:3320`) re-scans a freshly-stamped body's
inst-sites (`collect_body_insts`, `:3330`). Confirm that when `Map__g__i64` is stamped, the re-scan of
`to_dictionary`'s body substitutes `V→i64` in the `Dictionary<StrKey, V>` inst-site so it enqueues
`Dictionary<StrKey, i64>` (this is the same discovery caveat the reconciliation doc flagged for gap #2's
mono side). If the re-scan leaves `V` abstract, the site must be substituted through the instance's owner
Subst before `generic_inst_name` — the minimal extension to `stamp_inst_site` at `resolve.tks:3325-3326`.

> **GROUNDING NOTE (implementer probes FIRST, before F3.2/F3.3):** with `TEKO_TRACE`, dump (a) the abstract
> `to_dictionary` factory-call's `call_ns` + callee after F3.1 (is it the phantom, or the base?), and (b)
> whether `Dictionary__g__StrKey__i64` is already in `table_generic_instances` after `Map<i64>` stamps. (a)
> tells you whether the mono re-key operates on `call_ns`, the qualifier, or both; (b) tells you whether
> F3.3 is needed or the fixpoint already discovers it.

### Fixtures (inputs → native exit code)
- `tests/regressions/factory_owner_type_param/` — the G6 payload: `type Map<V> = class { … pub fn to_dictionary(): Dictionary<StrKey, V> { … Dictionary<StrKey, V>::make() … } }`; build a `Map<i64>`, bridge, read a key back → **exit** the stored value. Errors "unknown function: make" today.
- `tests/regressions/free_fn_owner_type_param/` — the free-generic form `fn to_dict<V>(v: V): Dictionary<StrKey, V> { var d = Dictionary<StrKey, V>::make(); d.insert(StrKey.make("k"), v); d }`; `to_dict<i64>(9).get(StrKey.make("k"))` → **exit 9**. Proves the fix is not class-specific.
- Two-concrete guard `tests/regressions/factory_two_concrete/` — `Dictionary<StrKey, i64>::make()` — must stay **exit 0** (byte-identical; proves no regression to the working path).

### Ritual: full gate at EACH crumb (F3.1 checker-only gate-able; F3.2 mono; F3.3 discovery). `any_generic` no-op guard holds (compiler corpus builds no owner-type-param factory). Compiler-touching → FIXPOINT gen1==gen2 + additive reseed. **G6 unblocks once F3 is green.**

---

## F4 — class-instantiation constraints not checked at monomorph

### Root (re-verified)
Free-generic FN instantiations ARE constraint-checked in PHASE-2 of `monomorphize`
(`monomorph.tks:1534`: `check_constraints(gf.type_params, gf.type_constraints, inst.s, table)`, def `:167`)
— this is why `dict_find_index<Plain>` yields the clean `does not satisfy its constraint` diagnostic. Generic
CLASS/struct instantiations are stamped by a DIFFERENT pass — `instantiate_types` (`resolve.tks:3288`) →
`stamp_inst_site` (`:3320-3331`) — which resolves `argtypes` (`:3325`) and stamps the instance decl
(`:3328-3329`) but **never calls `check_constraints`**. Both `stamp_inst_site` and `instantiate_types` are
total (no error channel), so the constraint violation is silently stamped; `Dictionary<Plain, i64>`'s method
`.hash()` then lowers to a call on `Plain__hash`, which was never synthesized (Plain lacks `IHash`) →
**link error `undefined reference to Plain__hash`** instead of a compile-time stop.

### Classification: IN-SCOPE BUG FIX (checker, reseed). NOT owner-ruling.
An M.1 fail-loud gap: a constraint that IS a compile-time gate escapes to the linker. Medium priority
(diagnostic correctness; it does not block a feature, but it makes the fase1b REJECT fixture dishonest —
today `diagnostics/dict_key_no_ihash/` drives the gate through the free-generic helper `dict_find_index`,
not the `Dictionary<Plain,…>` class instantiation).

### Crumb F4.1 — check every stamped class instance's constraints in `monomorphize`
`monomorphize` (`monomorph.tks:1463`) HAS an error channel (`TProgram | error`) and already walks
`table_generic_instances(table)` (`:1558`, def `resolve.tks:3406`). Add a constraint-check loop there,
reusing the SAME `check_constraints` the free-generic path uses and recovering each instance's owner Subst
via `instance_type_subst` (`monomorph.tks:1197`, the field-unification trick).

```teko
/**
 * (F4) Gate every stamped generic CLASS/struct instance against its template's type-param constraints,
 * mirroring the free-generic-fn check (monomorph.tks:1534). `instantiate_types` stamps a class instance
 * without an error channel, so a `Dictionary<Plain, i64>` where `Plain` lacks `IHash` reaches codegen and
 * link-fails on the missing `Plain__hash`; this raises the clean compile-time stop instead. The owner
 * `{K → Plain, V → i64}` Subst is recovered from the instance's stamped fields (`instance_type_subst`),
 * then `check_constraints` verifies each param. A no-op for an instance whose template has no constraints,
 * and for the whole non-generic corpus (no stamped instances) → gen1==gen2 byte-identical.
 *
 * @param table  the folded type table (source of stamped instances + their templates)
 * @return       null when every class instance satisfies its constraints; the first violation otherwise
 * @since fase1b-machinery (F4)
 */
fn check_instance_constraints(table: TypeTable): error | null {
    var insts = table_generic_instances(table)
    var i = 0
    loop {
        if i >= insts.len { break }
        var inst = insts[i]
        var stem = g_instance_base(inst.name)
        match find_generic_template(table, stem) {
            TemplateReg as tr => {
                if tr.decl.type_constraints.len > 0 {
                    var s = instance_type_subst(tr.decl, inst, table)
                    match check_constraints(tr.decl.type_params, tr.decl.type_constraints, s, table) { error as e => return e; null => { } }
                }
            }
            null => { }
        }
        i++
    }
    null
}
```
Wire it early in `monomorphize`, right after the `any_generic` guard (`monomorph.tks:1476`) so the diagnostic
fires before any body work: `match check_instance_constraints(table) { error as e => return e; null => { } }`.

> **GROUNDING NOTE:** `check_constraints`' message (`monomorph.tks:179`) is generic ("type parameter 'K'
> does not satisfy its constraint at this instantiation"); it is the SAME message the free-generic path
> emits, so the diagnostic is consistent across both. Enriching it to name the owner type + the concrete arg
> is optional polish (report, do not gate). Confirm `instance_type_subst` recovers the owner Subst for a
> CLASS instance (it field-unifies template vs instance annotations — a class with `intern keys: []K` yields
> `K→Plain`); a class with zero fields mentioning a param cannot be constraint-relevant, so an empty Subst
> there is benign (`check_constraints` then hits its own internal-bind guard only if a constraint exists on
> an unused param, which the corpus never authors — verify).

### Fixtures (inputs → native exit code)
- `tests/regressions/diagnostics/class_key_no_ihash/` — `type Plain = struct { n: i64 }` (NO `IHash`) + `Dictionary<Plain, i64>` with a `.hash()`-using method → **compile REJECT** with "does not satisfy its constraint" (was a link error). This REPLACES the current helper-driven reject so the gate runs through the class instantiation.
- Keep the free-generic reject `dict_find_index<Plain>` fixture (already clean via `monomorph.tks:1534`) — the two now agree.
- Positive guard `tests/regressions/class_key_ihash_ok/` — `Dictionary<StrKey, i64>` (StrKey HAS IHash) compiles and round-trips → **exit** the stored value (proves the check does not over-reject).

### Ritual: full gate. No-op for the constraint-free corpus → gen1==gen2 byte-identical. Compiler-touching → reseed (the class-instance reject fixture needs the seed to emit the diagnostic).

---

## Ordered fix plan (dependencies + priority)

**Doc-2 "prepare the terrain" blockers:** F3 (blocks G6) and F1 (blocks the `T | null` generic-return idiom
the whole collection surface wants). **Polish/correctness:** F4 (diagnostic honesty). **Non-issue pending
probe:** F2.

```
F1  ──(independent, checker)──────────────┐
F4  ──(independent, checker, reuses check_constraints)──┐
F3  ──(checker + mono; shares the re-key family with the — non-reproducing — F2)──▶ unblocks G6
F2  ──(PROBE first; likely already closed by the landed gap-#3 re-key)
```

**Recommended sequence (each independently gate-able; batch reseeds to minimize fixpoint runs):**

1. **F1 FIRST** — smallest, isolated, one predicate + one guard edit (`resolve.tks:2132` + new
   `is_type_param_named`). Unblocks `heap_pop_min`/`dequeue`/`first_or_null` returning `T | null`. Reseed.
2. **F4 SECOND** — isolated, reuses `check_constraints` + `instance_type_subst` verbatim; one new fn wired
   into `monomorphize` (`monomorph.tks:1476`). Makes the reject fixture honest. **Batch its reseed with F1**
   (both are low-blast-radius checker additions, both no-op the corpus).
3. **F3 THIRD** — the G6 blocker; widest (checker tolerate F3.1 + mono re-key F3.2 + discovery F3.3),
   highest blast radius (mono re-key touches the fixpoint). Land it on its OWN reseed, gate F3.1→F3.2→F3.3
   independently, then adopt **G6 `Map.to_dictionary`** on top (G6 corpus reseed follows).
4. **F2 LAST — PROBE ONLY.** Run crumb F2.0. If it passes (expected), delete `arr_drop_u64_at`, restore
   `arr_drop_at` at `hashset.tks:74`, re-gate — no compiler edit. If it reproduces, escalate with the
   `TEKO_TRACE` capture; do not design blind.

**Shared machinery:** F3's mono re-key (`mono_rekey_call_ns`/`mono_rekey_callee_qualifier`,
`monomorph.tks:407/428`) is the same family the (non-reproducing) F2 named; F3's `subst_instance_name`
fallthrough is also the mechanism gap #2's nested-generic discovery needs — landing F3 pre-positions that
follow-up. F1 and F4 share nothing with F3 and can proceed in parallel.

**Law/risk notes:**
- No law tension in any of the four. F1 narrows a false positive (does NOT lift the deferred #28
  interface-value-in-union carve-out — verify the reject fixture stays red). F3/F4 extend the D22/#254
  monomorphization-gate machinery already ratified. F4's diagnostic is the M.1 fail-loud the surface mandates.
- Every fix is a compile-time stamp/gate/resolve change — no runtime metadata, no allocation-model change,
  native backend authoritative. Each no-ops the compiler's own corpus → `any_generic` guard holds,
  gen1==gen2 stays byte-identical; verify at EACH crumb, not only at the end.
- **Adjacent, REPORTED not actioned:** gap #2 (nested generic construct, struct-literal form) remains latent
  — F3 closes only the FACTORY-call form. The `subst_instance_name` re-key F3 adds is the same hook gap #2's
  mono discovery will reuse. Do not fold gap #2 into this triage.

*Grounding: every `file:line` re-verified on `fix/retirement` HEAD `f177be09`. No product `.tks` edited.*
