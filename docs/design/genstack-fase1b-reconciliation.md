# genstack ↔ fase1b reconciliation — the STALE generic-stack design vs the current `fix/retirement` lane

> **Status:** DESIGN RECONCILIATION (architect). Read-only on product code; this doc is the sole edit.
> No build, no reseed, `teko test` NOT run. Every claim grounded at `file:line` on `fix/retirement`
> **HEAD `43814b58`** (`git rev-parse HEAD`). Reconciles the stale `generic-stack-completion.md`
> (designed pre-retirement, five gaps) against the live 9-ops + #254 machinery and the current
> `collections-generics-fase1b-crumbs.md`.

---

## 0. Verified environment (HEAD 43814b58)

| fact | verification |
|---|---|
| structural-trait synthesis RETIRED | `src/checker/synth.tks` ABSENT (`ls` → no such file); `git grep is_structural_trait\|synthesize_structural` over `*.tks` → **zero live hits** (only doc-comment prose in `src/collections/map.tks:9-13`). Retirement commits `1aae1145` + `6ebae869`. |
| 9-ops interface capability corpus LANDED | `src/cmp/cmp.tks` EXISTS (5656 B): `pub type IEq` `:10`, `pub type IOrd` `:38`, `NeByEq` `:86`, `GtByLt` `:104`, `LeByLt` `:121`, `GeByLt` `:138`. |
| 9-ops generic operator dispatch LIVE | `constraint_op_owner` `typer.tks:1102`; `build_constraint_operator_call` `:1137`; `dispatch_constraint_binop` `:1161`; entry from `type_binary` type-param arm `:1254-1256`. |
| #254 method-over-`T` dispatch LIVE | `type_method_call` `typer.tks:2908` → type-param `InterfaceBody` arm `:2921` → `type_contract_dispatch` `:3056`; `method_owner_interface` used `:1107,:2424`; `iface_methods_by_name` `:1108,:2418,:2428,:2443`. |
| mono concretization of constraint dispatch | `rekey_struct_constraint_dispatch` `monomorph.tks:594`; `rekey_iface_dispatch` `:625` (calls the struct-direct rekey `:626`); wired in the stamp walk `:882`. |
| runtime hash/cmp seeds | `exp fn str_hash` `teko_rt.tks:529` (FNV-1a); `str_cmp` `teko_rt.tks:538`. |
| OOP hard-cut (D27) LANDED | commits `6d6473a5` (reserve self/base/static + synthetic receiver + reseed R1), `7947a692` (strip explicit self receiver + reseed R3), `0362dbcc` (migrate `.tkt` fixtures). Live proof: `list.tks:26` `pub static fn make(): List<T> { .{ items = … } }`; instance methods declare NO `self` param and read the synthetic `self` (`list.tks` `pub fn len(): u64 { self.items.len }`). |
| seed can compile 9-ops code | reseed `e98fd5b0` already contains `constraint_op_owner` (`git grep -c` → 5 hits in `e98fd5b0:src/checker/typer.tks`); later reseed `1f86ee64` (gap #3) inherits it. The cmp corpus `b778b7d1` was a **leaf** (`22912a8e`: "no reseed — leaf corpus"). |
| `IHash` NOT yet declared | `git grep IHash` over `*.tks` → only a doc-comment mention in `cmp.tks:6`. It is net-new (G1's job). |

---

## 1. Gap #1 (structural-trait dispatch on `K: Hashable & Eq`) — **OBSOLETE, confirmed**

**Its premise is deleted, not blocked.** Gap #1's four root sites (per `generic-stack-completion.md:27,109-114`)
were `atom_surface` structural arm, `constraint_interfaces` structural arm, `type_param_reg` ExternBody,
and the `==` honest stop — ALL predicated on `is_structural_trait` + `synthesize_structural_methods` in
`src/checker/synth.tks`. That file is gone; the recognizers are gone (§0). Gap #1's crumbs 1.1–1.4 all
*build on* `synthesize_structural_methods` ("the synthesis already exists", `generic-stack-completion.md:114,141,168`)
— they cannot be implemented and MUST NOT be (un-retiring synth reverses the owner's `1aae1145`/`6ebae869`,
out of bounds).

**The capability gap #1 targeted is fully covered by the 9-ops + #254 interface model — for an
INTERFACE-constrained param.** `Map<K: IEq & IHash, V>` is achievable with zero structural synthesis:

- **`k == k2` on `K: IEq`** lowers through the 9-ops path. `type_binary` (`typer.tks:1235`) computes
  `op_dn = "__eq"`; since neither operand is a value-type with a native `__eq` owner but `K` is a
  type-param (`operand_is_type_param`, `typer.tks:1079`), it takes the arm at `typer.tks:1254-1256` →
  `dispatch_constraint_binop` (`:1161`). That calls `constraint_op_owner(K, "__eq")` (`:1102`), which
  reads `K`'s constraint `InterfaceBody.extends` (`:1106`), finds `IEq` via `method_owner_interface`
  (`:1107`), and confirms `IEq` declares `operator __eq` (`cmp.tks:18`). It then
  `build_constraint_operator_call` (`:1137`) synthesizes the method-call `k.__eq(k2)` and routes it
  through `type_method_call` → `type_contract_dispatch` (the #254 seam). At mono, `rekey_iface_dispatch`
  (`monomorph.tks:625`) concretizes it to the bound type's own `operator __eq`
  (`rekey_struct_constraint_dispatch` for a struct receiver; the class vtable otherwise). **This is the
  real dispatch the retired structural `Eq` never had** (`cmp.tks:4-6` states exactly this).

- **`k.hash()` on `K: IHash`** lowers through the #254 constraint-method path. `type_method_call`
  (`typer.tks:2908`) types the receiver to `Named{K}`, finds `K`'s type-param decl whose body is an
  `InterfaceBody` (the constraint), and takes the `InterfaceBody` arm at `typer.tks:2921` →
  `type_contract_dispatch` (`:3056`), resolving `hash` via `method_owner_interface(ib.extends, "hash")`
  against `IHash`. `IHash` is an ordinary METHOD interface (`fn hash(): u64`) — no operator token needed
  — so it needs ONLY #254, which is DONE. Mono concretizes to the bound type's `hash` (e.g.
  `StrKey.hash()` → `teko::runtime::str_hash`, `teko_rt.tks:529`).

Therefore `Map<K: IEq & IHash, V>` = interface constraints + `constraint_op_owner` (crumb-4 dispatch) +
`type_contract_dispatch` (#254), with **NO structural synthesis**. Gap #1 is superseded by design, not
merely unreachable.

**Residual capability gap (named precisely).** The 9-ops model covers everything gap #1 covered **except
implicit auto-derivation**: structural `Hashable`/`Eq` auto-synthesized `hash`/`eq` onto ANY struct with
no author action, so a bare `Point` struct was usable as a key for free. The interface model requires a
key type to EXPLICITLY conform (write `operator __eq` / `fn hash()`). This is a deliberate consequence of
the owner's retirement, not a machinery hole. **The interface-model way to close it — already in fase1b —
is (a) the `StrKey`-style wrapper struct that writes `operator __eq` + `fn hash()` over `str_hash`, and
(b) the `NeByEq`/`GtByLt`/… reflection mixins (`cmp.tks:86-147`) that supply the counter-part dunders so a
conformer hand-writes only `__eq`/`__lt` + `hash`.** No resurrection of synth; a future ergonomic
`derive`-style sugar (if ever wanted) is a separate owner decision, explicitly NOT proposed here.

**Verdict: Gap #1 OBSOLETE — remove it from the live backlog. No residual machinery gap; the only lost
affordance is implicit auto-derivation, closed by explicit conformance + mixins + wrappers.**

---

## 2. Gaps #2 / #4 / #5 — relevance to the CURRENT fase1b lane

All three ROOT-NS root sites still exist on HEAD (re-anchored below), but fase1b as designed (G1-G6)
**trips none of them**. Classification: **all three LATENT-but-not-triggered-by-fase1b.**

### Gap #2 — nested generic construct in a generic method — **LATENT (dodged)**

- **Root, re-anchored:** the resolver refactored since the stale doc (old `typer.tks:1587`). Today
  `type_struct_lit` (`typer.tks:4780`) delegates the prefix to `construct_target` (`:4752`) →
  `bare_construct_target` (`:4726`) which resolves via `resolve_named(sl.type_path, table, env.cur_ns, 0)`
  (`typer.tks:4727`) — the arity-0 lookup in the USING ns; the phantom path is `explicit_inst_target`
  (`:4703-4711`, `name_is_phantom_instance`). The own-type `.{ }` self-construct is now `dot_construct_target`.
  The failure mode gap #2 named (a DIFFERENT generic base parameterized by the owner's type-param, e.g.
  `Cell<V>` inside `Holder<V>`) is still not phantom-stamped at abstract-typing time. Root PRESENT.
- **Does fase1b trip it?** NO. Every fase1b collection (G3 `Dictionary`, G4 `HashSet`, G5
  `SortedSet`/`PriorityQueue`/`SortedDictionary`) uses the **parallel-array representation**
  (`keys: []K` / `hashes: []u64` / `vals: []V`, or `items: []T`) built purely with
  `teko::list::empty()` / `teko::list::push`. There is NO nested generic-class construct
  parameterized by the owner's type-param — this is the exact `Map<V>` shape (`map.tks:30-44`) that ships
  today. G6 `to_dictionary` constructs `StrKey { key }`, but `StrKey` is a CONCRETE (arity-0) struct, not
  a generic parameterized by `V` — so it takes the plain `bare_construct_target` path, not the nested-
  generic one. **#2 stays latent.** (The fase1b doc's own note at its §G3 confirms the parallel-array
  choice is deliberately to avoid the nested `Entry<K,V>`.)

### Gap #4 — generic instance as a free-fn param (`m: Map<i64>` value-vs-pointer) — **LATENT (not triggered)**

- **Root, re-anchored + STILL PRESENT:** `emit_type_expr` (`codegen.tks:3051`) — the `<args>` early return
  is now at **`codegen.tks:3090`**: `if nt.args.len > 0 { return mangle_type_name(buf, "", cg_texpr_inst_name(last, nt.args)) }`,
  which returns BEFORE the `ClassBody`-pointer arm at `codegen.tks:3095-3097`
  (`parser::ClassBody => return cb(mangle_type_name(buf, "", cdname), " *")`). So a param a human spells
  `Dictionary<i64>` (args>0) still emits by-value while the bare stamped `Dictionary__g__i64` (args==0)
  reaches the pointer arm — the value/pointer divergence is unchanged. Root PRESENT.
- **Does fase1b trip it?** NO. Every fase1b FREE function takes ARRAYS or scalars, never a collection
  class by the `<args>` spelling: `dict_find_index<K: IEq & IHash>(items: []K, hashes: []u64, h: u64, k: K)`,
  `sorted_insert<T: IOrd>(ref items: []T, x: T)`, `heap_sift_up<T: IOrd>(heap: []T)`, `arr_*<T>(xs: []T, …)`.
  No free fn has a `Dictionary<…>` / `HashSet<…>` parameter. The collections themselves are passed only as
  method receivers (the synthetic `self`), which codegen emits by the RESOLVED-`Named` path
  (`emit_type`, `codegen.tks:1821`, the correct class-pointer twin), not the syntactic `<args>` path.
  **#4 stays latent.**

### Gap #5 — cross-ns generic factory (`ns::List<i64>::make()`) — **LATENT (not triggered)**

- **Root, re-anchored + STILL PRESENT:** `retarget_generic_static_callee` (`typer.tks:3139`) still builds
  `NamedType { path = single_seg_path(owner_name); args = c.owner_type_args }` (`typer.tks:3143`), dropping
  every leading qualifier, then `resolve_type(nt, table, ref_ns)` in the CALLER's ns (`:3144`). A
  fully-qualified cross-ns factory `ns::Coll<i64>::make()` would still resolve `Coll` bare in the caller's
  ns. Root PRESENT. Early-out guard: `c.callee.segments.len < 2` returns the callee verbatim
  (`typer.tks:3140`).
- **Does fase1b trip it?** NO. The `.tkt` gate pattern (inherited, `list_test.tkt`) calls the factory by
  its **bare single-segment base**: `var xs = List<i64>::make()`. A single-segment callee hits the
  `segments.len < 2` early-out (`typer.tks:3140`) → never reaches the qualifier-dropping line. fase1b's
  `dictionary_test.tkt` / `hashset_test.tkt` / etc. mirror this exact pattern
  (`Dictionary<StrKey, i64>::make()` bare). No fase1b crumb calls a collection's `static make()` by a
  multi-segment `ns::…::Coll<T>::make()` spelling from another namespace (G6 `to_dictionary` constructs the
  `Dictionary` in-ns via its own `make`). `teko::env` consumes the str-keyed `Map<V>` (already shipped),
  not a fase1b generic factory cross-ns. **#5 stays latent.**

**Summary:** none of #2/#4/#5 is a REAL-PREREQ of fase1b. All three are **LATENT-but-not-triggered** —
report them as open ROOT-NS follow-ups (they gate a FUTURE ergonomic form: nested-`Entry`, collection-typed
free-fn params, cross-ns factory), NOT fase1b. No crumb port required.

---

## 3. fase1b GREEN-LIGHT with fresh anchors

### 3.1 Dependency gate — SATISFIED NOW (no pending 9-ops reseed)

The fase1b doc's gate prose ("FASE-1b may enter only a seed that already contains 9-ops … 9-ops reseed →
FASE-1b corpus reseed") is **stale on the reseed premise**. The correct reading (and the deliverable's
note) holds: the gate is that the **seed's COMPILER can compile 9-ops-using source**, which is TRUE — the
9-ops checker machinery (`constraint_op_owner` et al.) has been in the seed since reseed **`e98fd5b0`**
(verified: 5 occurrences in `e98fd5b0:src/checker/typer.tks`) and the parser handles `operator` decls (the
`cmp.tks` leaf `b778b7d1` compiled against that seed). The cmp TYPEDEFS (`IEq`/`IOrd`) are a **leaf in the
SOURCE corpus**, not in the seed — and that is FINE: fase1b's corpus will `use` them and compile them from
source in the SAME build, so no reseed of the cmp typedefs is needed. **There is no pending "9-ops reseed"
blocking fase1b — it can start immediately on the current seed (post `1f86ee64`).**

### 3.2 Stale-anchor corrections to `collections-generics-fase1b-crumbs.md`

| fase1b doc claim | CORRECT (HEAD 43814b58) |
|---|---|
| "on `origin/fix/retirement`, HEAD `5f0d701c`" | HEAD is now `43814b58` (`5f0d701c` verified ancestor). Re-anchor. |
| "`src/cmp/` does NOT exist (dropped in `07588731`)" | **`src/cmp/cmp.tks` EXISTS** (re-added `b778b7d1`): `IEq:10`, `IOrd:38`, `NeByEq:86`, `GtByLt:104`, `LeByLt:121`, `GeByLt:138`. §6.1 coordination point is **RESOLVED**: `IHash` co-locates in `src/cmp/cmp.tks`. |
| "9-ops `IEq`/`IOrd` sealed decls (plano :341-357) — the DECLARED shape the corpus references" | Now **LANDED as real code**, not a declared shape: contract against `src/cmp/cmp.tks:10/38`, not the plano prose. |
| "#254 dispatch cited `typer.tks:2197`" | Mechanism live at `type_method_call:2908` → `type_contract_dispatch:3056`; `method_owner_interface:1107/2424`; `iface_methods_by_name:1108/2418/2428/2443`. |
| "9-ops generic operator dispatch — crumb 4" | LANDED: `type_binary` type-param arm `typer.tks:1254-1256` → `dispatch_constraint_binop:1161` → `constraint_op_owner:1102` → `build_constraint_operator_call:1137`. |
| `str_cmp` "doc said :536 (drift −2)" | `teko_rt.tks:538` (matches the fase1b doc's corrected value). `str_hash` `exp fn` at `:529`. |
| `Iterator<T>` "`iter.tks:28`" | `src/iter/iter.tks:27` (drift −1). |
| `Map<V>` class/`map_find_index` | `pub type Map<V>` `map.tks:30`; `pub static fn make` `:43`; free `map_find_index(keys:[]str,…)` `:143`. |
| `arr_*` combinators | `arr_replace_at<T>` `collections.tks:29`, `arr_drop_at<T>` `:49`, `arr_drop_last<T>` `:67` (matches). |
| `sort_str`/`sort_i64` concrete | `sort.tks:81` / `:157` (matches — generic `sort<T: IOrd>` is adjacent, unblocked, NOT built here). |

### 3.3 Ordered implementable crumb sequence (green-lit, current lane)

Dependency spine unchanged: **G1 → G3/G4**, **G2 → G5**, **G3 → G6**. What CHANGED: the whole sequence is
buildable NOW (no reseed wait); tag each crumb by which machinery it needs.

| order | crumb | needs | buildable on current seed? |
|---|---|---|---|
| **1 (FIRST)** | **G1a — `IHash` interface** in `src/cmp/cmp.tks` (`type IHash = interface { fn hash(): u64 }`) | **#254 method-interface-over-`T` only** (DONE) | **YES — start here** |
| 2 | G2a — un-gated combinators `arr_insert_at`/`arr_swap`/`arr_reverse`/`arr_slice` in `collections.tks` | #254 generics only | YES |
| 3 | G1b — `StrKey` conformer (`struct IEq & IHash & NeByEq { key: str }`, `hash()`=`str_hash`, `operator __eq`=byte-eq) + prim adapters | 9-ops `operator __eq` dispatch (LIVE) + G1a | YES (machinery seeded) |
| 4 | G2b — IOrd-gated helpers `sorted_insert<T: IOrd>`, `heap_sift_up`/`heap_pop_min` | 9-ops `<` over `T: IOrd` (LIVE) | YES |
| 5 ★ | **G3 — `Dictionary<K: IEq & IHash, V>` + `dict_find_index<K: IEq & IHash>`** (`src/collections/dictionary.tks`) | generic `==` over `K: IEq` (crumb-4, LIVE) + G1 | YES |
| 6 ★ | G4 — `HashSet<T: IEq & IHash>` (reuses `dict_find_index`) | G3 | YES |
| 7 ★ | G5 — `SortedSet<T: IOrd>` / `PriorityQueue<T: IOrd>` / `SortedDictionary<K: IOrd, V>` | 9-ops IOrd + G2b | YES |
| 8 | G6 — additive `Map<V>.to_dictionary(): Dictionary<StrKey, V>` (Map STAYS) | G3 | YES |
| — | REJECT fixture `diagnostics/dict_key_no_ihash/` — `Dictionary<Plain,V>` where `Plain: IEq` but not `IHash` → `IHash` constraint diagnostic | G1+G3 | YES |

**First implementable crumb: G1a — declare `type IHash = interface { fn hash(): u64 }` in
`src/cmp/cmp.tks`** (co-located with `IEq`/`IOrd`), needing only the shipped #254 method-over-`T` path.
Because the 9-ops machinery is already seeded, the old two-stage "9-ops reseed then fase1b reseed" collapses
to a **single additive fase1b reseed** (G1-G6 + fixtures). Blast radius of MACHINERY = zero (no
checker/codegen/parser edit); the compiler's own corpus instantiates none of these generics, so the
`any_generic` no-op guard holds and gen1==gen2 stays byte-identical.

---

## 4. OOP hard-cut (D27) — snippet modernization caveat

The hard-cut has LANDED (§0). Current instance-method spelling: **no explicit `self` parameter** (the
receiver is synthetic — `list.tks` `pub fn len(): u64 { self.items.len }`); factories are **`pub static fn
make()`**; self-construction is **`.{ … }`**; `Name { … }` is legal ONLY inside the class's own methods.

**fase1b copy-verbatim source is ALREADY compliant.** Both the fase1b crumbs doc's inline snippets (G4
`HashSet`, G5 `SortedDictionary`, G1 `IHash`) AND the plano-collections §2 snippets they point implementers
to use the post-hard-cut form: `pub static fn make(): T { .{ … } }` (`plano-…-0.3.1.md:218-219,293,326`),
instance methods with NO `self` param (`add(x: T)`, `insert(k: K, v: V)`, `get(k: K)`), `self.field` reads
inside. **No modernization needed for any fase1b snippet.**

**PRE-hard-cut snippets that need modernization — all confined to the OBSOLETE
`generic-stack-completion.md`** (do NOT implement as written; listed so no one copies stale syntax):

| location | stale spelling | post-hard-cut spelling |
|---|---|---|
| gap #3 fixture (`:99`) `Ctr<T>` | `pub fn make(): Ctr<T> { Ctr { n = 0 } }`; `pub fn count(self): i64`; `pub fn twice(self): i64` | `pub static fn make(): Ctr<T> { .{ n = 0 } }`; `pub fn count(): i64`; `pub fn twice(): i64` (drop `self` param; `self.count()` stays) |
| gap #1 fixtures (`:215-217`) `Map`/`Point`/`same`/`h` | `pub fn insert(self, k: K, v: V)`; `k.hash()` on `<K: Hashable & Eq>` | OBSOLETE (gap #1 retired). If ever re-cast: drop `self` param, constrain `<K: IEq & IHash>`, `pub static fn make()`. |
| gap #4 fixture (`:247`) `Bag<T>` | `pub fn make(): Bag<T> { Bag { xs = … } }`; `pub fn add(self, x: T)` | `pub static fn make(): Bag<T> { .{ xs = … } }`; `pub fn add(x: T)` |
| gap #5 fixture (`:288`) `Stack<T>` | `pub fn make(): Stack<T> { … }` (non-static, brace-construct) | `pub static fn make(): Stack<T> { .{ … } }` |
| gap #2 fixture (`:323`) `Cell<T>`/`Holder<T>` | `pub fn make(x: T): Cell<T> { Cell { v = x } }`; `Cell<T>::make(x)` | `pub static fn make(x: T): Cell<T> { .{ v = x } }` |

These fixtures matter only if the LATENT #2/#4/#5 roots are ever picked up as a separate follow-up; at that
time they must be re-spelled per this table.

---

## 5. Verdict

1. **Gap #1 OBSOLETE — confirmed.** Its synth premise is deleted (`1aae1145`/`6ebae869`), zero live refs;
   un-retiring is out of bounds. `Map<K: IEq & IHash, V>` is fully achievable via interfaces: `==`→
   `constraint_op_owner`/`dispatch_constraint_binop` (`typer.tks:1102/1161/1254`), `hash()`→
   `type_contract_dispatch` (#254, `typer.tks:3056`), mono-concretized (`monomorph.tks:625`). Only residual
   loss = implicit auto-derivation; closed by `StrKey` wrapper + `NeByEq`/`GtByLt` mixins (`cmp.tks:86-147`),
   NOT by resurrecting synth.
2. **#2 / #4 / #5 = LATENT-but-not-triggered-by-fase1b.** Roots still present (#2 `typer.tks:4727`; #4
   `codegen.tks:3090`; #5 `typer.tks:3143`) but fase1b's parallel-array representation (#2), array-typed
   free-fn params (#4), and bare same-ns factory calls (#5, `segments.len<2` early-out `typer.tks:3140`)
   dodge all three. No crumb port required; report them as future ROOT-NS follow-ups.
3. **fase1b GREEN-LIT on the current seed — no reseed wait.** 9-ops checker machinery seeded since
   `e98fd5b0`; cmp typedefs are a source leaf compiled with the corpus (gate satisfied). First implementable
   crumb: **G1a — `type IHash = interface { fn hash(): u64 }` in `src/cmp/cmp.tks`** (needs #254 only).
   Ordered sequence: G1a → G2a → G1b → G2b → G3★ → G4★ → G5★ → G6 (+ reject fixture), single additive reseed.
4. **Hard-cut modernization: none needed in fase1b** (its snippets and the plano §2 source are already
   post-hard-cut). Only the OBSOLETE `generic-stack-completion.md` fixtures use pre-hard-cut syntax (§4
   table) — do not copy them.

*Grounding: every `file:line` re-verified on `fix/retirement` HEAD `43814b58`. No product `.tks` edited.*
