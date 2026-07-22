# Onda-3 — the monomorphization + VM + 128-bit keystone cluster

**Status:** DESIGN-AHEAD (architect). No product code changed. Ready to implement the instant #300 merges.
**Seed:** `0.0.1.37-alpha` (teko.tkp).
**Author role:** architect. Implementers copy the crumb snippets verbatim (full-Javadoc, `.tks`-only, W15-from-now).

Onda-3 is the fix that unblocks the GENERIC stdlib layer — the analog, for the *type/generics* layer,
of what C7.19 byte-buffer transport is for the transport layer. Onda-2 (iterators/io/traits/checked-math/
format, shipped by #300) is landing *monomorphic-first*: every generic surface is parked behind exactly
the six bugs mapped here.

---

## 0. TL;DR — the cluster is TWO independent sub-clusters

Treating all six as one cluster is only half right. The shared machinery binds four of them; two are
orthogonal 128-bit arithmetic bugs that share nothing with the mono pass and can land in parallel by a
different implementer.

- **Sub-cluster A — the monomorphization / dispatch family (SHARED ROOT).**
  `#254` (generic methods), `#294` (struct-through-constraint dispatch), `#301` (closure-in-Ref/optional
  round-trip), `#290` (same-bare cross-ns method dispatch). These share the mono pass
  (`src/checker/monomorph.tks`), the method→free-fn lowering, and the codegen mangle/dispatch seam.
- **Sub-cluster B — the 128-bit arithmetic family (INDEPENDENT).**
  `#296` (VM `norm_int` u128 trapping) and `#299` (codegen `cb_i128` negative `Number` literal). Both are
  self-contained two's-complement-reinterpret bugs — one VM-only, one native-only — touching neither the
  mono pass nor dispatch. They gate the u128/i128 math family (#185/#187/#194), not the generic stdlib.

**Recommendation:** run B in parallel with A from day one (two implementers, two branches). B is small,
low-risk, and unblocks the 128-bit math family without waiting on A. A is the real keystone.

---

## 1. Root-cause map (grounded, file:line)

### Sub-cluster B (independent, do these first — they are small and unblock 128-bit math)

#### #296 — VM `norm_int` reinterprets u128 via a TRAPPING `raw to u128`
- **Root:** `src/vm/vm.tks:1598` — inside `fn norm_int(raw: i128, signed: bool, width: u8)` the unsigned
  128-bit arm is `else { raw to u128 }`. `raw` is the `i128` carrier; a u128 with the high bit set is
  stored as a *negative* i128, and `i128 to u128` is a **checked** cast that PANICS on a negative source
  ("impossible conversion"). So `u128::MAX`, `2^127`, any high-bit value → VM panic; native lowers it as a
  plain two's-complement `(unsigned __int128)` and is correct → **VM≠native divergence.**
- **Layer:** none of the five mono layers — this is the **VM value-normalization** layer (the `.tks`
  stand-in for the C `tag` switch), entirely inside the interpreter.
- **Kind:** VM-only.
- **The idiom already exists** three functions up: `wrap_hole_to_u64` (`vm.tks:1332`) does the non-trapping
  reinterpret as `(raw & mask) to u64`. #296 is the u128-width sibling: there is no 128-bit-wide
  non-trapping cast primitive, so the fix builds the u128 from its two masked 64-bit halves.

#### #299 — codegen `cb_i128` panics on a DIRECT negative `Number{value=-1}`
- **Root:** `src/codegen/codegen.tks:149` — `fn cb_i128(buf, v: i128)`; the `v < 0` arm computes the
  magnitude as `(0 to u128) - (v to u128)`. That inner `v to u128` on a negative `i128` trips the same
  checked-cast guard → panic ("impossible conversion"). It never fired historically because a negative in
  source parses as `Unary(Minus, Number)`, not `Number{-1}`; TR3 (#177) synthesizes `-1` via
  `mk_neg_int` (unary-minus) precisely to dodge it. A direct `Number{value=-1}` reaching `cb_i128` (a
  future synthesizer, or a checked-math constant fold) trips F3.
- **Layer:** the **codegen constant-emission** layer (leaf helper, `cb_*` byte-buffer family).
- **Kind:** native-only.
- **Fix:** derive the magnitude via a non-trapping two's-complement reinterpret, mirroring #185's ≤64-bit
  handling — `magnitude = (~ (v to-reinterpret u128)) + 1` computed on the bit pattern, never a checked
  negate. Same idiom family as #296.

**Shared root of B:** both are the *checked/trapping* `to u128` cast standing where a *non-trapping
two's-complement reinterpret* is meant. The language has no 128-bit non-trapping cast op; the pattern is
"mask/rebuild through 64-bit halves" (VM) and "reinterpret the bit pattern" (codegen). Neither touches the
mono pass. A single helper in each engine closes both.

### Sub-cluster A (the keystone — shared mono/dispatch machinery)

#### #254 — generic struct/class METHODS do not exist (the 5 confirmed layers)
Grounded against the current tree:
1. **collect/typer type-param table (layer: type-param resolution, feeds `stamp`).**
   `collect.tks:63 method_func_type` and `typer.tks:2933 type_method` build the type-param table from
   `f.type_params` ONLY (`type_param_table(f.type_params, …)` at `collect.tks:64`, `typer.tks:2938`),
   never prepending the ENCLOSING type's params. So a method of `List<T>` referencing `T` fails with
   `unknown type: T`. The 4+2 call-site fix (prepend the owner's `type_params`) was written, validated
   (662 tests green) and reverted — **crumb 1 is ready.**
2. **resolve body-substitution DROPS methods (layer: stamp — type-decl body).**
   `resolve.tks:1398 subst_body_names` (StructBody arm) and `resolve.tks:1689 normalize_inst_body`
   (StructBody arm) both emit `methods = teko::list::empty()` and have NO `ClassBody` arm at all. So even
   if the fields stamp correctly, the concrete instance `List__g__i64` carries zero methods.
3. **no method-stamping pass (layer: worklist + stamp).**
   `monomorph.tks:726 monomorphize` PHASE-2 stamps only free generic **TFunctions** (`find_generic_fn`
   filters `type_params.len > 0` TFunctions, `monomorph.tks:289`). There is no discovery of, nor stamping
   for, a generic type's METHOD bodies per instantiation. `table_generic_instances` (appended at
   `monomorph.tks:814`) emits the concrete type-decls whose bodies were method-stripped by layer 2.
4. **no return-type-as-expected threading (layer: TExpr.types / typer).**
   `type_return`/`type_block` do not propagate the declared return type as an expected context, so
   `fn box_make<T>(v: T) -> Box<T> { Box { value = v } }` fails (`cannot infer` / `Box__g__T` not stamped).
   The annotation-driven struct-init retarget exists ONLY for `let`-bindings (`type_binding` →
   `type_struct_lit(expected)`, see S4 memory), not for a return position.
5. **classes need a static factory (layer: all of 1–4 + auto-construction).**
   A class instance is built by a static factory whose body constructs `Box<T>{…}` — blocked by 1–3 AND by
   4 (the factory's return type IS the generic instance).
- **SHARED ROOT:** the mono pass models a program as *free functions + method-stripped type-decls*. Methods
  are lowered to namespaced free functions elsewhere (`typer.tks:3110`, method-ns = `ns::Class`), but that
  lowering runs BEFORE mono and does NOT clone-per-instantiation. The whole gap is: **a generic type's
  methods are never carried into, nor stamped by, the mono worklist.**

#### #294 — a struct under `<T: Contract>` does not dispatch through the vtable (only classes upcast)
- **Root:** dispatch through a constraint reaches `rekey_iface_dispatch` (`monomorph.tks:404`) and, at
  emit, the class-only vtable machinery. `type_conforms_to` (`resolve.tks:852`) accepts BOTH structs and
  classes nominally (so the struct *satisfies* the constraint — `constraint_atom_satisfied`,
  `monomorph.tks:72`), but the codegen upcast + `tk_vt_<T>_<iface>` vtable are gated on
  `cg_is_class_named` (`codegen.tks:441`, and every `tk_vt_`/`tk_base_` site). A struct has no fat-pointer
  `{data, vtable}` rep, so a `x.measure()` on a constraint-bound struct either panics on the VM (no vtable
  to name-dispatch through as a value) or emits an upcast to a never-generated symbol natively.
- **Layer:** straddles **stamp/rewrite-calls** (`rekey_iface_dispatch`) and **codegen** (vtable emission).
- **Kind:** both engines (VM panic; native cc-reject / miscompile).
- **Design fork (see §4):** structs are value types with no vtable slot. There are two law-clean
  resolutions; one is a genuine scope decision → **HALT candidate** (resolved below, but flagged).

#### #301 — a function-typed value in a `Ref` cell / optional-slice field does not round-trip
- **Native root:** `src/codegen/codegen.tks:969` and `:1008` — `cg_opt_mangle` / `cg_opt_mangle_str` (the
  deterministic mangle SUFFIX for an optional/slice inner type) have arms for Prim/Byte/Char/Str/Error/
  Named/Optional/Slice but **NO `checker::Func` arm** → the `_ =>` falls to
  `"codegen: optional/slice inner type not yet supported"`. So `[]Fn`, `Fn?`, `Ref<Fn>`-adjacent
  optional/slice inner never gets a `tk_opt_func` / `tk_slice_func` typedef. The by-value edge (`emit_type`,
  `codegen.tks:849`) and the annotation path (`codegen.tks:1214`) DO know `checker::Func => "tk_closure"` —
  the mangle family is the sole gap. Also `cg_opt_mangle_texpr_str` (`codegen.tks:1086`) has no
  `FunctionType` arm (its `_` is the syntactic twin of the same gap).
- **VM root:** a closure (`FuncVal`) re-seated into a `Ref` cell (`cell_set`) is dropped across a call
  boundary. The cell store threads back correctly for scalars (`with_cells(caller, fe.env.cells)`,
  `vm.tks:3223`), so the drop is in the closure-value *capture-vs-cell* path: a `FuncVal` carries its
  `cap_vals` by SNAPSHOT (`eval_lambda_call`, `vm.tks:2933`), so a closure written into a cell after
  capture is not observed by an already-captured copy. Minimal repro (from #301): `reseat(cell: Ref<IntFn>, n)`
  returns wrong values on BOTH engines.
- **Layer:** native = **codegen** (the mangle leaf, layer-5); VM = the interpreter's ref-cell/closure
  layer (not a mono layer). Same *conceptual* root as #294/#254: a function type is a first-class value the
  aggregate-lowering seams do not fully cover.
- **Kind:** both engines. Gate of `flat_map` (ITER0 #184, parked) and of `ReadFn | error` union arms
  (a function-typed union member hits the SAME mangle gap → forced `file_read_fn`/`zip_reader` to return a
  bare `ReadFn`).

#### #290 — instance-method dispatch on same-bare classes across namespaces fails
- **Root:** `src/checker/scope.tks:130 lookup_call` (and its twin `call_ns`, `scope.tks:166`). The
  method-call desugar builds a **2-segment** callee path `[BareClass, method]` (`typer.tks:786-789`, using
  `name_last_segment` for the class segment). A 2-segment path is *qualified* (`qualified = true`,
  `scope.tks:132`), so `qual = "BareClass"` and the resolver matches on
  `ns_last_seg(b.ns) == qual` (`scope.tks:143`). A method-ns is `"ns::Class"` (`typer.tks:3110`), so
  `ns_last_seg` yields `Class` — which matches for BOTH `left::Svc` and `right::Svc`. The reverse scan
  returns the FIRST match → the WRONG namespace's method type → `argument type mismatch`. **Field** access
  works because it resolves off the receiver's `Named{n.name}` (canonical, fully-qualified) via the type
  table, never through `lookup_call`'s ends-with rule.
- **Layer:** the **call/name-resolution** layer (feeds rewrite-calls + codegen mangling). This is the
  #109-family (bare-name) bug on the method-dispatch path — same family as #294/#254's dispatch key.
- **Kind:** checker (surfaces both engines identically).

**SHARED ROOTS of A:**
1. **Method→free-fn lowering + mono coverage.** #254 (methods never stamped) and #290 (method-ns
   disambiguation) both live in how a method becomes a namespaced callable and how that callable is keyed.
   The mono pass and `lookup_call` are the two chokepoints.
2. **Dispatch key / vtable at instantiation.** #294 (struct through constraint) and #290 (same-bare) both
   miskey the dispatch target — `rekey_iface_dispatch` + `cg_is_class_named` (#294), `lookup_call`
   ends-with (#290).
3. **First-class function-type coverage in the aggregate-lowering seams.** #301 is the mangle-family gap;
   it is the sibling that proves the function type is under-covered wherever a value is boxed.

---

## 2. Dependency DAG + recommended ordering

```
  Sub-cluster B (128-bit, INDEPENDENT — parallel branch, different implementer):
    #296 ──┐
           ├──> unblocks u128/i128 math family (#185 residual, #187 bigint, #194 crypto)
    #299 ──┘   (both leaf, no interdependency; land in either order or together)

  Sub-cluster A (mono/dispatch — SEQUENCED, one implementer):
    #290 ─────────────────────────┐   (name-resolution fix; smallest; unblocks method dispatch keying)
                                   ├──> #254 ──> unblocks #163 (collections classes) C2+
    #301 ─────────────────────────┤   (mangle Func arm; independent of #290, but shares codegen seam)
                                   │
    #294 ──(DECIDES scope)────────┘   (struct-through-constraint; depends on the §4 ruling)
```

- **True prerequisites:**
  - `#290 → #254`: generic METHODS lower to the same namespaced-callable path #290 fixes. Landing #254's
    method-stamping on top of a mis-keying resolver would bake same-bare bugs into every stamped generic
    method. Fix the keying first.
  - `#254 → #163`: generic collections classes (List/Set/Map) are the first real customer of generic
    methods. #163 C2+ is blocked on #254 (memory `teko-generic-methods-gap`).
- **Independent-but-related:**
  - `#301` shares the codegen mangle seam with #254 (both add arms to the same `cg_opt_mangle*` family) but
    has no ordering dependency on #290/#254. It can land first (it is smaller and self-contained) or
    alongside. Recommend landing #301 EARLY in A because its mangle-Func arm is a prerequisite for a
    generic `Iterator<T>` whose element is a closure (the ITER0 generic layer).
  - `#294` depends on the §4 scope ruling, not on code. Once ruled, it slots after #254 (it reuses the
    stamped-method machinery for the struct case).
- **VM-only:** #296. **Native-only:** #299. **Both engines:** #254, #294, #301, #290.

**Recommended landing order (A):** `#290` → `#301` → `#254` → `#294`. **(B) in parallel:** `#296` + `#299`.

Rationale: #290 removes the mis-keying foundation; #301 closes the function-type mangle gap (small, high
leverage, unblocks the generic-iterator element type); #254 is the big method-stamping pass built on a
clean keying + mangle base; #294 is the ruled scope decision layered on the stamped-method machinery.

---

## 3. Per-issue ordered crumb sequences (first 3 in the A order + both of B)

> All snippets are full-Javadoc, `.tks`-only. C twins are FROZEN — do NOT touch `.c/.h` except the
> maintained runtime seam (`teko_rt.{c,h}`), which none of these need. Ritual = the full gate
> (`teko-verify-both-with-test-gate`): gen1 `teko . -o bin` (native #test gate) + `./bin/teko test .`
> (VM) + FIXPOINT `gen1==gen2` byte-identical + `diff_vm_native.sh` + `TEKO_MEM_PARANOID=1` + `//`-audit.

### B/#296 — VM u128 non-trapping reinterpret (VM-only, ~1 crumb)

**File:** `src/vm/vm.tks`. **Touches:** `norm_int` (1586). **Adds:** one helper.

**Crumb 1 — a non-trapping u128 reinterpret helper + rewire the 128 arm.**
Replace `else { raw to u128 }` at `vm.tks:1598` with a call to a new helper that rebuilds the u128 from
two masked 64-bit halves (no 128-bit-wide checked cast exists, so we go through halves like
`wrap_hole_to_u64` does for 64-bit):

```teko
/**
 * Reinterpret an i128 carrier's raw BITS as a u128, two's-complement, never trapping.
 *
 * The VM carries every integer Value in an i128; a u128 with the high bit set is stored as a
 * NEGATIVE i128, and a checked `i128 to u128` PANICS on it ("impossible conversion") — a VM≠native
 * divergence (native lowers a u128 as a plain `(unsigned __int128)` reinterpret). There is no
 * 128-bit-wide non-trapping cast op, so we rebuild the value from its two 64-bit halves: the low
 * half is masked out non-trapping (`wrap_hole_to_u64`), the high half is the arithmetic shift's low
 * 64 bits, and they are recomposed in u128 space where every sub-op is total.
 *
 * @param raw  the integer Value's raw bits as an i128 carrier
 * @return     the same bit pattern as a u128 (two's-complement reinterpret, never panics)
 * @see wrap_hole_to_u64 the 64-bit sibling reinterpret (vm.tks)
 * @since onda-3 (#296)
 */
fn reinterpret_i128_to_u128(raw: i128) -> u128 {
    let lo = wrap_hole_to_u64(raw) to u128
    let hi = wrap_hole_to_u64(raw >> 64) to u128
    (hi << 64) | lo
}
```
Then `vm.tks:1598` becomes `else { reinterpret_i128_to_u128(raw) }`.

- **Fixtures:** `examples/regressions/u128_high_bit/` (VM==native, exit-code protocol).
  Program: `let m: u128 = 340282366920938463463374607431768211455  // u128::MAX`; assert
  `m >> 120 == 255` → `exit((m >> 120) to i64 as u8)` = **exit 255**; plus `2^127` high-bit
  (`let h: u128 = 170141183460469231731687303715884105728`; `if h >> 127 != 1 { exit(1) }`) → exit 255.
  The .tkp mirrors the `iter_protocol` shape (`kind = "binary"`). This fixture PANICS on the VM today
  (proving the bug) and passes on both after the fix.
- **Ritual point:** full gate. Extra vigilance on FIXPOINT (norm_int runs in the self-host's own literal
  lowering — but the corpus carries no u128 high-bit literal, so gen1==gen2 must stay byte-identical;
  the no-op-on-existing-corpus property is the guard).

### B/#299 — codegen cb_i128 negative literal (native-only, ~1 crumb)

**File:** `src/codegen/codegen.tks`. **Touches:** `cb_i128` (149).

**Crumb 1 — derive the magnitude via a non-trapping reinterpret.**
Rewrite the `v < 0` arm so the magnitude never goes through a checked `to u128`:

```teko
/**
 * Append a signed 128-bit value as a C constant EXPRESSION (sign + magnitude via the shift form).
 *
 * A negative `i128` must NOT compute its magnitude via `(0 to u128) - (v to u128)`: the inner
 * `v to u128` on a negative source trips the checked-cast guard F3 ("impossible conversion"). This
 * historically never fired (a source negative parses as `Unary(Minus, Number)`, and TR3's `mk_neg_int`
 * synthesizes `-1` the same way), but a DIRECT `Number{value=-1}` (a future synthesizer or a
 * checked-math fold) reaches here. Instead we reinterpret `v`'s bit pattern into u128
 * two's-complement (never trapping) and take the negation's magnitude as `~bits + 1`, mirroring the
 * ≤64-bit handling in #185.
 *
 * @param buf  the output byte buffer
 * @param v    the signed 128-bit value
 * @return     `buf` with the C constant expression appended
 * @since onda-3 (#299)
 */
fn cb_i128(buf: []byte, v: i128) -> []byte {
    if v < 0 {
        let mag = (~ reinterpret_i128_bits_u128(v)) + (1 to u128)   // two's-complement magnitude, non-trapping
        mut b = cb(buf, "-(")
        b = cb_u128_c_expr(b, mag)
        return cb(b, ")")
    }
    cb_u128_c_expr(buf, reinterpret_i128_bits_u128(v))
}
```
where `reinterpret_i128_bits_u128` is the codegen-side twin of #296's helper (same
mask-through-halves construction, placed local to codegen's `cb_*` family since codegen must not depend
on vm.tks). If a shared home is preferred, both can call a single `teko::runtime`-level reinterpret — but
the FROZEN-C-twin rule makes a small per-module helper the lower-risk choice (no cross-module surface
churn, no `.c` twin to touch). **DECISION (law-first, M.3 honest-simple):** duplicate the tiny helper
per engine; do NOT introduce a new runtime symbol for a 6-line bit trick.

- **Fixtures:** `examples/regressions/i128_neg_literal/` (native-only, like `time_types`). A synthesizer
  path is hard to trigger from source directly (that is WHY it never fired). Fixture builds a project whose
  checked-math constant fold produces a direct negative `Number` (e.g. `math::checked::sub_i128` of a fold
  that yields `-1` without a unary-minus AST) → `cc` must accept the emitted `-((unsigned __int128)1ULL)`
  and the binary must exit with the low byte of the value. If no source construct reaches `cb_i128` with a
  direct negative `Number` post-#177, this fixture is a **unit assertion in `codegen_test.tkt`** instead
  (call `cb_i128` on a synthesized `Number{value=-1}` and assert the emitted bytes == `-(...)`), which is
  the honest coverage — reported as such in the PR body.
- **Ritual point:** full gate + a targeted `codegen_test.tkt` unit that feeds `cb_i128` a direct negative.

### A/#290 — same-bare cross-ns method dispatch (checker, ~2 crumbs)

**Files:** `src/checker/typer.tks` (the desugar, ~786), `src/checker/scope.tks` (`lookup_call`/`call_ns`).
**Root:** the method desugar hands `lookup_call` a 2-segment `[BareClass, method]` path whose `qual`
matches ANY namespace ending in that bare class.

**Crumb 1 — carry the receiver's CANONICAL class name into the desugar's callee path.**
At `typer.tks:786`, the receiver's type is already `Named{struct_name}` where `struct_name` is the
canonical (fully-qualified) name (`recv_t` at `typer.tks:687`). The desugar currently bares it. Instead,
build a callee whose leading segments carry the FULL canonical qualifier so `lookup_call`'s `qual` is the
full `ns::…::Class` prefix, not the last segment. The minimal, targeted form: thread the resolved method-ns
alongside the path so `type_call` resolves against the exact namespace rather than the ends-with rule.

```teko
/**
 * Build the method-dispatch callee as a FULLY-QUALIFIED path from the receiver's canonical class
 * name, so name resolution keys off the exact namespace and never collides with a same-bare class in
 * a sibling namespace (#290, #109-family).
 *
 * `struct_name` is canonical (`ns::…::Class`) from the receiver's `Named`. A bare `[Class, method]`
 * path is *qualified* to `lookup_call`, whose `ns_last_seg(b.ns) == qual` rule then matches EVERY
 * namespace ending in `Class` — the reverse scan returns the first, i.e. the wrong one. Emitting the
 * canonical qualifier as leading segments makes `qual` the full class-owning namespace segment, so the
 * match is exact. Field access already resolves off the canonical `Named` via the type table, which is
 * why it never regressed.
 *
 * @param struct_name  the receiver's canonical class name (`ns::…::Class`)
 * @param method       the dispatched method's bare name
 * @return             the fully-qualified `[..ns.., Class, method]` callee path
 * @since onda-3 (#290)
 */
fn method_dispatch_callee(struct_name: str, method: str) -> parser::Path {
    mut segs: []parser::Segment = teko::list::empty()
    let qual = name_qualifier(struct_name)   // "" for a root type, else the owning ns
    if qual.len > 0 {
        // split the canonical qualifier into its `::` segments so lookup_call's `qual` is the class-owning ns segment
        let parts = split_ns_segments(qual)
        mut i = 0
        loop { if i >= parts.len { break }; segs = teko::list::push(segs, parser::Segment { name = parts[i] }); i++ }
    }
    segs = teko::list::push(segs, parser::Segment { name = name_last_segment(struct_name) })
    segs = teko::list::push(segs, parser::Segment { name = method })
    parser::Path { segments = segs }
}
```

> **NOTE (grounding gap the implementer must confirm first):** `lookup_call` today matches on
> `ns_last_seg(b.ns) == qual` — a SINGLE segment compare. Simply lengthening the path does not help unless
> `lookup_call` also compares the class-owning segment (the segment BEFORE `method`) against the binding's
> ns *tail*. The real fix is crumb 2 — make the resolver disambiguate the CLASS segment against the method
> binding's ns, not just the last-but-one. Crumb 1 alone is insufficient; keep both.

**Crumb 2 — disambiguate the class segment in `lookup_call`/`call_ns` against the binding's OWNING ns.**
A method binding's ns is `ns::Class` (`typer.tks:3110`). When the callee path's second-to-last segment is
the CLASS and there is a leading qualifier, require the binding's ns to END WITH `<leadingQual>::<Class>`
(not merely `ns_last_seg == Class`). Guarded so ordinary 2-segment `ns::fn` calls (no class) are unchanged.

```teko
/**
 * Does binding `b`'s namespace satisfy a method-dispatch callee whose leading segments name the
 * receiver's canonical class-owning namespace? For a class method the binding's ns is `owner::Class`;
 * the callee path carries `[..owner.., Class, method]`. Matching the FULL `owner::Class` tail (not just
 * the `Class` last segment) disambiguates two same-bare classes across namespaces (#290).
 *
 * @param b_ns      the candidate binding's namespace
 * @param callee    the dispatch callee path
 * @return          true iff `b_ns` ends with the callee's `<owner-segments>::<Class>` tail
 * @since onda-3 (#290)
 */
fn ns_matches_qualified_class(b_ns: str, callee: parser::Path) -> bool {
    // reconstruct the `owner::…::Class` tail from every segment EXCEPT the trailing method
    let want = join_ns(path_prefix_segments(callee))   // all but the last segment, `::`-joined
    b_ns == want || ends_with_ns(b_ns, want)
}
```
Wire it into `lookup_call` (`scope.tks:143`) and `call_ns` (`scope.tks:179`) as the qualified-match
predicate when the path has ≥2 leading segments (a class-qualified dispatch); keep the existing
`ns_last_seg(b.ns) == qual` for the plain 2-segment `ns::fn` case (guard on "the second-to-last segment
names a class in the table" vs "names a namespace"). This is the exact #109-family disambiguation already
proven for optionals/tags (`qualified_optional` fixture).

- **Fixtures:** `examples/regressions/same_bare_method_dispatch/` (VM==native), modeled on
  `di_same_name_cross_ns` (which reads a FIELD precisely because method dispatch was broken — #290's own
  report). Two namespaces `left`/`right`, each `type Svc = class { pub tag: i64; pub fn make(x) -> Svc; pub
  fn tag_of(self) -> i64 { self.tag } }`; `left::Svc::make(3).tag_of()` == 3 and `right::Svc::make(7).tag_of()`
  == 7; `exit(l + r)` = **exit 10**. Fails to type-check today (`argument type mismatch`), passes on both
  after. Also **flip** `di_same_name_cross_ns` to CALL the method (not read the field) once #290 lands —
  that fixture's field-read workaround becomes obsolete (report it, do not silently rewrite unrelated
  fixtures; fold the flip into #290's PR since it IS the regression closure).
- **Ritual points:** full gate. The self-host compiler is 100% free-function with same-bare classes only in
  the `#109` type surface, so gen1==gen2 must stay byte-identical (the guard: the qualified-class predicate
  is a no-op for every non-class-dispatch call). Verify the `di_same_name_cross_ns` flip in the same run.

### A/#254 — generic type methods (the big one; SEQUENCED after #290)

This is a full pass, delivered as ordered crumbs (each independently gate-able). The full step-by-step is
below; #290 must be green first (methods lower through the keying #290 fixes).

**Crumb 1 (READY — pre-written, reverted): enclosing type-params in the method type-param table.**
Re-apply the 4 `collect.tks` + 2 `typer.tks` call-sites: at `method_func_type` (`collect.tks:63`) and
`type_method` (`typer.tks:2933`) PREPEND the owner type-decl's `type_params`/`type_constraints` to the
method's own before `type_param_table`. Signature stays; the table gains `owner_tps ++ method_tps`.
```teko
// collect.tks method_func_type — was: type_param_table(f.type_params, f.type_constraints, "", table)
let tbl = type_param_table(concat_tps(owner_type_params, f.type_params), concat_tcs(owner_type_constraints, f.type_constraints), "", table)
```
(662 tests were green with this exact change; it is the validated crumb.)

**Crumb 2: stop dropping methods in body-substitution + add the ClassBody arm.**
`resolve.tks:1398 subst_body_names` and `resolve.tks:1689 normalize_inst_body` — replace
`methods = teko::list::empty()` with a per-method substitution, and add a `ClassBody` arm (today absent):
```teko
/**
 * Substitute the enclosing type's parameters through a struct/class BODY, KEEPING and rewriting the
 * methods (previously dropped — #254 layer 2). Each method's param annotations, return type and body
 * type-references are subst'd so a stamped `List__g__i64` carries a concrete method set. A `ClassBody`
 * is handled symmetrically (it was entirely unhandled before).
 * @param body    the generic type's body
 * @param params  the enclosing type's parameter names
 * @param args    the concrete type-args at this instantiation
 * @return        the body with fields AND methods substituted to concrete types
 * @since onda-3 (#254)
 */
```
For each method: `subst_texpr_names` the param annotations + return type, and recurse the body statements
(a syntactic subst analogous to `subst_body_names`' field walk). NOTE: method bodies at this stage are
still SYNTACTIC (parser statements) — the substitution is name-level (`T` → concrete TypeExpr), the same
tool `subst_texpr_names` already provides.

**Crumb 3: stamp generic-type METHODS in the mono worklist.**
`monomorph.tks` — when a concrete type-instance is stamped (currently appended via
`table_generic_instances`, `:814`), ALSO emit each of its (now-carried, crumb-2) methods as a stamped free
TFunction keyed by the instance name, with its body run through `mono_block` under the instance's Subst.
The method-ns of a stamped method is the instance name (`List__g__i64`), so dispatch keys off the concrete
type. This reuses the existing `mono_block`/`subst_type` machinery — the method body walk is IDENTICAL to a
free generic fn's. The discovery seed: a constructor / method call on a generic-type instance
(`list.push(x)` where `list: List<i64>`) enqueues the method inst exactly as a free generic call does.
```teko
/**
 * Stamp every method of a concrete generic-type instance as a monomorphic free function keyed by the
 * instance name, its body rewritten through the instance's Subst. Mirrors free-fn stamping
 * (monomorphize PHASE-2) but the callable's owner is the stamped type, so a `list.push(x)` on a
 * `List<i64>` dispatches to `List__g__i64::push`. Reuses mono_block/subst_type unchanged.
 * @param inst   the type instance (name + Subst)
 * @param gtd    the generic type-decl template (carrying methods after crumb 2)
 * @param items  the program items (for transitive generic-call discovery)
 * @param table  the folded type table
 * @return       the stamped method TFunctions + any transitively-discovered insts
 * @since onda-3 (#254)
 */
```

**Crumb 4: return-type-as-expected threading (#254 layer 4).**
`type_return` — thread the enclosing fn's declared `return_type` as the expected context into the returned
expression's typing, so `fn box_make<T>(v: T) -> Box<T> { Box { value = v } }` retargets the struct-init to
the concrete instance exactly as `type_binding` already does for `let`. Reuse `type_struct_lit(expected)`
(the annotation-driven retarget) — pass the fn's return type as the expected type at the return position.
```teko
// type_return: pass the enclosing return_type as the struct-init expected context (like type_binding does)
```

**Crumb 5: classes — generic static factory + auto-construction (#254 layer 5).**
With crumbs 1–4, a generic class's static factory whose body constructs `Self<T>{…}` and whose return type
is the generic instance now type-checks and stamps. Add the class-body method carrier to crumb 2/3 (already
covered by the ClassBody arm) and verify the arena-per-object ref semantics survive stamping (the stamped
method set reuses the same class-lowering; no new codegen).

- **Fixtures (VM==native unless noted):**
  - `examples/regressions/generic_struct_method/` — `type Box<T> = struct { value: T; pub fn get(self) -> T
    { self.value } }`; `Box<i64>{value=42}.get()` → **exit 42**; a second instantiation `Box<u8>` proves
    per-instance stamping.
  - `examples/regressions/generic_class_factory/` — `type Cell<T> = class { pub v: T; pub fn make(x: T) ->
    Cell<T> { Cell { v = x } }; pub fn read(self) -> T { self.v } }`; `Cell<i64>::make(7).read()` → exit 7.
  - `examples/regressions/generic_method_self_construct/` — a method that CONSTRUCTS its own type
    (`fn dup(self) -> Box<T> { Box { value = self.value } }`) — proves the return-type-as-expected thread.
  - `examples/regressions/generic_method_trait_fold/` — a `<K: Hashable & Eq>` chain method (the #163 Map
    key path) — proves the constraint gate + trait fold survive method stamping.
  - Plus corpus `#test`s in `generics_test.tkt` (run on the VM → cover the mono method path both engines).
- **Ritual points:** full gate at EACH crumb (they are independently gate-able). The no-op-on-non-generic
  guard (`monomorphize` `any_generic`, `:739`) MUST still hold → gen1==gen2 byte-identical (the compiler
  itself has zero generic methods). This is the single most important fixpoint guard in the cluster.

### A/#294 and A/#301 — see §4 (ruling) and §1 (root); crumbs below.

**#301 crumb 1 — the `Func` arm in the mangle family (native).**
Add a `checker::Func` arm to `cg_opt_mangle` (`codegen.tks:942`), `cg_opt_mangle_str` (`:979`), and a
`parser::FunctionType` arm to `cg_opt_mangle_texpr_str` (`:1086`), each yielding the suffix `"func"` (the
SAME fragment `emit_type`/`type_mangle` already use — `codegen.tks:1134`). Then `Fn?` → `tk_opt_func`,
`[]Fn` → `tk_slice_func`, both carrying the `tk_closure` two-word rep. The generated typedef reuses the
existing `tk_closure` value type; only the mangle key was missing.
```teko
// cg_opt_mangle: add before the `_ =>` fallthrough
checker::Func => cb(buf, "func")
// cg_opt_mangle_str: add before the `_ =>` fallthrough
checker::Func => "func"
// cg_opt_mangle_texpr_str: add a FunctionType arm
parser::FunctionType => "func"
```

**#301 crumb 2 — the VM ref-cell closure round-trip.**
The VM drops a `FuncVal` re-seated into a cell across a call boundary because `eval_lambda_call`
(`vm.tks:2918`) binds captures by SNAPSHOT (`fv.cap_vals`, `:2933`). Design: a closure that must observe a
cell WRITE performed after its capture must capture the CELL INDEX (a `RefVal`), not the value — which is
already the sound pattern (#300's "make_counter returns 1,1,1" proof). For the `reseat(cell: Ref<IntFn>, n)`
repro, the fix is to ensure a `Ref<Fn>` cell's `.value` write (`cell_set`) and read (`cell_get`) carry the
`FuncVal` through unchanged (they are `Value`-generic, so this should already hold) — the actual drop is in
the callee→caller cell merge when the written value is a FuncVal whose OWN captured cells were grown in the
callee frame. **Implementer probe first:** minimal repro under `TEKO_TRACE`; confirm whether the drop is
(a) the merge, (b) the snapshot, or (c) a coerce_to on a Func value. The design lands whichever the probe
proves; the mangle arm (crumb 1) is the load-bearing native half and unblocks the union-arm case regardless.

- **Fixtures:** `examples/regressions/closure_in_ref_roundtrip/` (VM==native) — the `reseat` repro;
  `examples/regressions/closure_optional_field/` (native — the `tk_opt_func` typedef must cc-compile);
  a `flat_map`-shaped `iter_test.tkt` once round-trip holds (unparks the ITER0 gap).

**#294 crumb (post-ruling):** see §4. If ruled "structs dispatch via a synthesized value-vtable", the crumb
adds a struct arm to the fat-pointer wrap in codegen (mirroring `cg_is_class_named` sites) + a VM
name-dispatch for struct receivers under a constraint. If ruled "constraint-bound structs stay
monomorphic-direct" (no vtable), the crumb makes `rekey_iface_dispatch` + emit dispatch a constraint-bound
struct call DIRECTLY to its stamped concrete method (no upcast) — which is strictly simpler and needs no
new rep.

---

## 4. Risk / law notes (fixpoint, VM==native parity, constitutional tensions)

### Fixpoint (gen-2 == gen-3 byte-identity)
- **Highest risk: #254.** The mono pass runs on the self-host compiler itself. The compiler is 100%
  free-function with ZERO generic methods, so the `any_generic` no-op guard (`monomorph.tks:739`) MUST keep
  the non-generic corpus byte-identical. Every crumb of #254 must preserve "no generic method in the
  program → output byte-identical". This is the non-negotiable fixpoint guard — verify gen1==gen2 at EACH
  crumb, not just at the end.
- **#290** touches `lookup_call`, which the compiler exercises on every call. The qualified-class predicate
  MUST be a no-op for every non-class-dispatch call (the guard: only fire when the second-to-last segment
  names a class in the table). Same-bare classes appear in the compiler's own type surface (#109), so a
  regression here would break the fixpoint loudly — good early signal.
- **#296/#299** are guarded by "no u128 high-bit / direct-negative-`Number` literal in the corpus" → no-op
  on the self-host, fixpoint trivially preserved.

### VM == native parity (native is authoritative — `teko-no-gc-vm-role`)
- **#296 is a parity FIX** (VM was wrong, native correct) — closes a divergence.
- **#299 is native-only** — the VM already handles negative literals; the fixture is native-only or a
  codegen unit. No new parity surface.
- **#301** must land BOTH engines together (the mangle arm is native, the cell round-trip is VM). Ship them
  in the same PR so `diff_vm_native.sh` never sees a half-fixed state.
- **#254/#294** — every fixture is VM==native (exit-code protocol). The stamped method set must produce
  identical dispatch on both. Native is authoritative on any divergence.

### Constitutional tensions (evolution phase — passes-all-Laws wins; log in DECISION_LOG.md)

1. **#294 — struct-through-constraint dispatch (GENUINE FORK).**
   Two law-clean resolutions:
   - **(a) Constraint-bound structs dispatch DIRECTLY to their stamped concrete method (no vtable).** Under
     monomorphization, a `<T: Contract>` body with `T` bound to a concrete struct is fully known at the
     instantiation — the `x.measure()` call can lower to a DIRECT call to `Struct::measure` (the stamped
     concrete method), never an upcast. No new rep, no value-vtable. Passes M.1 (no honest stop needed),
     M.3 (simplest), and matches the existing "structs satisfy a constraint nominally but are not contract
     VALUES" rule (`teko-oop-w10b-design`: interface-as-value is a class-only fat pointer). **This is the
     recommended, ratifiable-law-first resolution.**
   - **(b) Synthesize a value-vtable for constraint-bound structs.** Heavier: a struct gains a fat-pointer
     rep when used through a constraint. Contradicts "a struct cannot become a contract value"
     (`monomorph.tks:34` doc) and adds a rep the memory model does not want (no-GC, value semantics).
   - **RESOLUTION (law-first, applied):** **(a).** A constraint is a *monomorphization gate*, not a
     dynamic-dispatch promotion. When `T` binds to a struct, dispatch is static-direct to the stamped
     method. Dynamic dispatch (fat pointer) stays class-only, exactly as today. This needs `#254`'s
     method-stamping to exist first (the "stamped concrete method" is what #254 produces) — hence #294
     lands AFTER #254. **No owner HALT required** — (a) passes all Laws and matches settled OOP design.
     Logged for the LTS v1.0.0.0 review per `teko-decision-autonomy-and-log`.
     - *Residual flag for the owner (informational, not blocking):* if a FUTURE issue wants a struct value
       stored behind a `Contract`-typed slot (a heterogeneous collection of structs-as-contract-values),
       resolution (a) does NOT provide it — that would require (b) or boxing. That is a NEW capability, out
       of #294's scope (#294 is only "operate on `T` through the constraint inside a generic"). Report, do
       not expand.

2. **#299 helper placement (minor, resolved).** M.3 (honest-simple) + FROZEN-C-twin → duplicate the 6-line
   bit-reinterpret helper per engine rather than mint a new `teko::runtime` symbol (which would need a `.c`
   twin on the frozen surface). Logged.

3. **#290 — is the fix name-resolution or a deeper #109 canonicalization?** The #109 family has repeatedly
   shown that ANY bare-name matching with a fallback is suspect (`teko-arena-lifetime-observability`). The
   law-clean move is to make the method-dispatch callee CANONICAL end-to-end (crumb 1+2), consistent with
   how field-access already works. No tension — this is applying the established #109 W3 canonical rule to
   the one path (method dispatch) that was missed. Passes M.1 (exact, no guessing).

### Sequencing risk vs #162 (S6)
Memory `teko-generic-methods-gap` sequences #254 AFTER #162 (S6) to avoid heavy conflict in the shared
machinery (monomorph/typer/resolve/collect). Confirm #162's merge state before starting #254; if #162 is
still open, #254's crumbs 1–2 (collect/resolve) are the conflict-prone ones — coordinate or rebase. #290,
#296, #299, #301 do NOT conflict with #162 and can proceed regardless.

---

## 5. What remains BLOCKED (design-ahead honesty)

- **#301 VM crumb 2** needs a `TEKO_TRACE` probe to pin the exact drop (merge vs snapshot vs coerce). The
  DESIGN is complete (capture-the-cell-index, not the value); the one-line site is probe-gated. The native
  mangle arm (crumb 1) is fully specified and unblocks the union-arm/`ReadFn | error` case immediately.
- **#294** is gated on #254 landing (needs the stamped concrete method). Ruling (a) is applied and
  law-clean; implementation is a small dispatch-lowering change once #254's method-stamping exists.
- Everything else (#296, #299, #290, #254 crumbs 1–5) is fully specified against the current tree and can
  start the instant #300 merges.
