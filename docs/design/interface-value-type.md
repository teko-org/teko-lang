# Interface as value-type + dynamic dispatch — design-ahead (keystone 0.3.0.29)

> STATUS: **design-ahead, architect.** Task: "Interface como value-type +
> dispatch dinâmico". Branch `design/interface-value-type` off
> `origin/remodel/emit-throughput`. No product `.tks`/`.tkt` changed and no
> version bumped BY THIS DOCUMENT — it is the plan; the crumbs (§5) do the code
> changes under their own gates. All file:line citations are against
> `origin/remodel/emit-throughput` (the branch base) unless marked otherwise.

---

## 0. TL;DR — the premise is stale; measure before you build

The task states the gap as: *"an interface cannot be used as a VALUE TYPE yet —
`fn pull(r: teko::io::Reader, into: Buf)` → checker error `unknown type: Reader`."*

**Reproduced against the released seed (`bin/teko` 0.3.0.28) — that symptom is no
longer real.** The interface-value keystone is **already shipped for the
reference-semantics case (classes)**, C backend, end to end:

- `Reader` **resolves as a value type** in every position (param / field / return
  / local / slice element). `resolve_named` keeps a user interface nominal, so
  `Named{name = "…::Reader"}` is produced, not `unknown type`
  (`resolve.tks:669` `resolve_named`; the InterfaceBody decl falls through the
  alias split at the `_ =>` arm and is returned as a bare `Named`).
- A **class that conforms to an interface upcasts into it** —
  `widens_into_at` at `resolve.tks:794`:
  `is_class_name(from) && (is_interface_name(to) || is_trait_name(to)) &&
  type_conforms_to(from, to) → true`.
- The upcast **boxes into the fat pointer** `{ data, vtable }` at
  `codegen.tks:4412` (emit_as CONTRACT slot): object pointer → `.data`, the
  static per-`(class, iface)` vtable → `.vtable = tk_vt_<Class>_<Iface>`.
- A **dynamic call through the interface value** lowers to a vtable indirect —
  `typer.tks:940` `type_contract_dispatch` sets `is_iface_dispatch` + `iface_slot`;
  `codegen.tks:2179` `emit_iface_call` emits `recv.vtable[slot](recv.data, …)`.
- **Heterogeneous interface slices + covariant slice upcast already work** —
  proven by the in-tree regression `examples/regressions/class_slices` (#102):
  `total_area(shapes: []Shape)` (interface as slice-element AND param),
  `let shapes: []Shape = circles` (covariant slice upcast),
  `let mixed: []Shape = [Circle::make(1), Square::make(3)]` (heterogeneous,
  per-element upcast), `shapes[i].area()` (dynamic dispatch).
- **Stateful dynamic dispatch works**: a `class & Counter` whose `tick(self)`
  mutates `self.n`, called three times through a `Counter`-typed param, returns
  `3` on the C backend (reproduced this session — the class R1 arena-mutation
  exception, `ref-transparent-model.md` §6 R1, carries the state across
  dispatched calls).

**Evidence (reproduced this session, C backend, `teko build … -o bin`):**

| program shape | result |
|---|---|
| `fn pull(r: Reader, n)` param + `r.read(n)` body, no call site | builds clean (signature collects, body dispatches) |
| `class & Reader` passed to `pull(r: Reader, …)` | builds, runs, **exit 7** (correct) |
| stateful `class & Counter` driven 3× through a `Counter` param | **exit 3** (state persists) |
| `[]Shape` heterogeneous + covariant `[]Circle → []Shape` | `class_slices` gate passes |
| `struct Reader` passed to `pull(r: Reader, …)` | **rejected** — `argument type mismatch` |
| `let r: Reader = <struct value>` | **rejected** — `value type does not match annotation` |
| generic `fn pull_g<T: Reader>(r: T, …)` with a **struct** arg | builds, runs, **exit 7** (static/mono dispatch) |

So the keystone's **class half is DONE**. What actually remains is three
distinct, much smaller things (§1). The design below corrects the premise
(M.3 — state the gap, do not paper over), specifies the remaining pieces
law-first, and delivers the IO1 payoff on the half that ships **today**.

---

## 1. The real remaining gap (three pieces, ranked)

1. **STRUCT → interface value upcast.** Deliberately excluded at
   `resolve.tks:785-786`: *"a STRUCT is pure value data — no stable address to
   dispatch on — so it does NOT widen (its conformance still serves `<T: I>`)."*
   A struct conforms (usable as a `<T: I>` constraint, static/mono dispatch — the
   generic-arg row above), but cannot be boxed into a fat pointer because boxing
   needs an **addressable, longer-lived backing** than a value struct has. **This
   is the memory-model keystone piece (§4) and it is spine-gated (L1).**
2. **Interface as a union/variant member** (`Reader | error`, `Reader | null`).
   Honest-stopped at `resolve.tks:1397`: *"an interface cannot be a variant
   member yet — bind the interface value on its own."* Overlaps the in-flight
   null-union wave (§6) at the exact same validation site.
3. **The OWN/native backend** (`--backend=native`, `TEKO_BACKEND=native`) has
   **no class/interface layout at all** — it is the N1 reference subset
   (`lir/lower.tks:5,13`, exit(n)/integer/control only). A `class & Reader`
   build on the own backend fails `native backend N1: no layout registered for
   struct …` (reproduced). This is the emit-throughput remodel's own multi-wave
   track, **NOT this keystone** — see §3 (VM-is-dead → the differential is C-vs-own,
   `scripts/diff_c_own.sh`, and its corpus is exit(n)-scoped by design).

**Verdict up front:** the keystone ships its user-visible value (dynamic
interface dispatch, heterogeneous collections, the IO1 re-typing) **on classes,
on the C backend, with NO new language feature** (piece 1 is deferred, piece 2 is
coordinated with null-union, piece 3 is a different track). The one genuinely
new-capability piece (struct→interface) is **design-drafted here but its escaping
form HALTs on the unbuilt spine** — scoped in §4 to the non-escaping subset that
is soundly expressible today.

---

## 2. Type-system — interface (and bodyless-trait) in value positions

### 2.1 What resolves (already)

`resolve_type` (`resolve.tks`) → `resolve_named` returns `Named{name}` for an
interface/trait exactly as for a struct/class; nominal, by name (`type.tks:72`).
So an interface name is legal in **every** value-type position the grammar allows
a `NamedType`: parameter, field, local binding, return, slice element
(`SliceType` recurses through `resolve_type`), function-type param/return. No
change needed. The bodyless-`trait` path is symmetric: `is_trait_name`
(`resolve.tks`) is already OR'd into the upcast gate (`resolve.tks:794`) and into
`type_method_call`'s contract-dispatch branch (`typer.tks:827` synthesizes an
`InterfaceBody` from the trait's methods), so a trait used as a value type
dispatches through the SAME machinery.

### 2.2 What is (correctly) rejected today, and stays rejected

- **Field access on an interface value** — `typer.tks:1537`: *"an interface value
  exposes no fields — only its contract methods."* Correct and permanent: a fat
  pointer has no user fields. KEEP.
- **Interface as a variant member** — `resolve.tks:1397`. See §6 (null-union
  coordination); this is piece 2, deferred/coordinated, not part of the class
  half.
- **A struct value up-casting to an interface** — `resolve.tks:785`. This is
  piece 1 (§4). Today it fails with the caller-facing `argument type mismatch` /
  `value type does not match annotation`. **Crumb S1 (§5) replaces that generic
  message with a NAMED honest-stop** so a user who writes `struct … & Reader` and
  passes it as a `Reader` learns exactly why and what to do (use a `class`, or a
  generic `<T: Reader>`), rather than a bare type-mismatch.

### 2.3 The `Interface | null` and `Ref<Interface>` questions

- **`Ref<Interface>`** — an interface value is itself a two-word fat pointer whose
  `.data` is a raw address. `Reference{Named{Iface}}` (a `ref` to a fat pointer)
  is representable (a pointer to the two-word struct) but adds nothing a bare
  interface value lacks (the interface value already carries an address). The
  never-null-`Ref` rule (`ref-transparent-model.md` R2/R3) is unaffected: the
  `Reference` inner is the fat-pointer struct, which is never null; the *pointee's
  `.data`* being null is a **class-null** concern (below), not a `Ref` concern.
  **Recommendation:** allow `ref x: Iface` only where a `ref x: <class>` would be
  allowed (borrow-down of a live class local); no new rule.
- **`Interface | null`** — an interface fat pointer is niche-able: `.data == NULL`
  is a spare bit-pattern, so `Iface | null` can be represented as the bare fat
  pointer with `.data = NULL` meaning `null`, **no tag word**. This is EXACTLY the
  niche class that null-union Crumb 3 defines (`cg_union_niche_member`,
  null-as-union-type.md §8-C3: "class/ptr/`Ref` pointer … emit `X` bare, NULL =
  `null`, no tag"). **The interface fat pointer joins that niche family verbatim
  — do NOT invent a second niche path.** Until null-union lands, `Iface | null`
  falls under the piece-2 honest-stop (`resolve.tks:1397`).

**Sites map (all `resolve.tks`/`typer.tks`, target branch):** resolution =
`resolve_named` (669); value-widen gate = `widens_into_at` (783-800); field
reject = `typer.tks:1537`; dispatch typing = `type_contract_dispatch` (940);
union-member gate = `resolve.tks:1397`.

---

## 3. Representation & dispatch (the fat pointer) — already built, documented here

### 3.1 The fat pointer

An interface- (or polymorphic-base-) typed value lowers to a two-word C struct
typedef `{ void *data; <fnptr> *vtable; }` (`codegen.tks:548-595`). `data` is the
concrete object pointer; `vtable` is a **static, per-`(concreteType, contract)`
table** of the contract's methods in `iface_slot` order. A companion base-class
form `tk_base_<name>` (#98) uses the **same** shape, so Sub→Base and
concrete→interface share one lowering.

### 3.2 Where the vtable is built

One static vtable per conformance, keyed `tk_vt_<Class>_<Iface>`
(`codegen.tks` vtable-thunk emit, the `CgVtableEmit` result at `codegen.tks:73-81`;
mono-time re-key at `monomorph.tks:503 rekey_iface_dispatch` handles the
constraint-bound case). The slot index is the method's position in the
interface's **effective** (extends-transitive) method list —
`method_owner_interface` (`resolve.tks:518`) + `iface_method_slot` pick the owner
interface when a call keys off a wider/narrower contract.

### 3.3 Up-cast boxing

`emit_as` (`codegen.tks:4412`): when `expected` is an interface/trait and the
value's type is a **class**, emit `(tk_t_<Iface>){ .data = (void*)(<v>),
.vtable = tk_vt_<Class>_<Iface> }`. If `<v>` is already a base fat pointer, take
`(<v>).data` instead of casting the whole struct (`codegen.tks:4429-4434`). A
value already of the interface typedef passes through unwrapped.

### 3.4 Dispatch lowering

`emit_iface_call` (`codegen.tks:2179`): bind the receiver once, then
`((R(*)(void*,A…))recv.vtable[iface_slot])(recv.data, args…)`. The TAST carries it
on `TCall` (`tast.tks:31`): `is_iface_dispatch`, `iface_slot`, `callee_type` = the
resolved interface-method `Func`. **No LIR/own-backend path exists yet** (§1 piece
3); the own backend N1 subset (`lir/lower.tks`) does not lower `Named`/class/slice,
so interface dispatch is C-backend-only until the emit-throughput remodel reaches
aggregates. **The differential harness `scripts/diff_c_own.sh` is exit(n)-scoped
by design (§9.2 of that script) and does NOT cover interface programs** — so this
keystone introduces no C-vs-own divergence (there is nothing to diverge against
yet). The VM is retired (issue #524, `scripts/positive_regressions.sh:26`), so
"VM-is-dead" here means the only oracle is C-native, and the keystone's gate is the
positive/native (cc) regression lane.

---

## 4. The Ref / memory-model interaction — the hard part (piece 1)

### 4.1 Why the class half is already sound (no new hole)

A class object lives in an arena and has **reference semantics** (it owns its
arena — `ref-transparent-model.md` §6 R1 class exception;
`oop-this-base-static.md:210` "`this` is an arena-backed" object). The fat
pointer's `.data` is that stable object pointer. Storing/returning/collecting an
interface value therefore extends **exactly the class object's** lifetime, and the
existing class escape/arena analysis (`escape.tks`) already governs it — an
interface value is, for lifetime purposes, the class reference it wraps plus a
static (immortal) vtable pointer. **This is why `class_slices` (#102) is sound and
shipped: no boxing, no copy, the `.data` outlives the fat pointer by construction.**

### 4.2 Why the struct half needs the spine

A struct is pure value data with no address (`resolve.tks:785`). To make a struct
value an interface value you must **materialize it somewhere addressable** and put
that address in `.data`. That is the ref-transparent-model's forbidden direction:

> **"Ir em direção ao valor (descascar) é sempre seguro; afastar-se dele
> (embrulhar +1 nível) exige um lastro mais-longevo que um local não tem."**
> (`ref-transparent-model.md` §8, the summary law; A1 = transitive
> copy-on-attach + escape.)

Boxing a struct into a fat pointer that carries a pointer to it is precisely
"embrulhar +1 nível". If that fat pointer **escapes** the struct's arena scope
(stored in a field, a `[]Reader` that outlives the struct, a return, a closure
capture), the `.data` dangles the moment the frame drops — the exact UAF class
`ref-transparent-model.md` §7 catalogs (struct-field-ref, collection-return,
closure-capture). The current `escape.tks` is one-depth and **cannot prove the
transitive non-escape** that A1 requires. **A1 IS the spine (L1), and it is
unbuilt.**

### 4.3 The soundly-expressible-without-the-spine subset (what a crumb MAY do)

The borrow-down shape (`ref-transparent-model.md` R7 + A3): a struct **lvalue**
whose storage **outlives the call** may be boxed into a fat pointer that is
**consumed within the callee frame** and provably does not escape it. Concretely,
an interface value derived from a struct is sound **iff** it is used only in
**non-escaping positions**: passed to a fn param that (a) does not store it in a
field/collection/return/escaping-closure and (b) whose own body the one-depth
escape check can clear. `read_all(r: Reader) → Buf` fits: it loops `r.read(…)` and
returns a `Buf`, never `r`. This is the R7 "storage survives the call" guarantee,
enforced conservatively (honest-stop everything the one-depth check cannot prove
non-escaping — `ref-transparent-model.md` §9 reject-what-you-can't-prove).

**But a second wall makes the struct subset near-worthless for streams
specifically:** a *stateful* stream needs **mutable self across calls**. A value
struct's `read(self)` **cannot** advance a cursor — value-functional Teko has no
`self.field =` on a value receiver (B.4/B.21; `ref-transparent-model.md` R1 (i):
"métodos de struct que escrevem `this.field` são barrados"). That is the ORIGINAL
reason `stream.tks:20-39` uses the `ReadFn`/`WriteFn` + `Ref`-capture closure seam
— NOT the absence of interface values. So even a fully-built struct→interface
boxing would not let a value struct be a stateful stream.

### 4.4 Recommended resolution (law-first)

**RESOLVED, no HALT:** the stateful-stream contract wants **reference semantics**;
the language already provides that soundly via **classes** (R1 arena-mutation).
Therefore:

- **The IO1 payoff re-types the stream implementers as CLASSES** (`class & Reader`,
  `class & Writer`, …) and the combinators onto interface values (`r: Reader`).
  This ships today on the C backend, sound under existing class-arena rules (§4.1),
  no spine needed. (§5 crumb S3.)
- **Struct→interface is deferred to the spine (L1)** as piece 1. Crumb S1 turns the
  generic mismatch into a NAMED honest-stop that points the user at the two things
  that DO work (a `class`, or a generic `<T: I>`); the value-position boxing itself
  is **not built** here (it would need A1). This is the honest-stop discipline, not
  a capability regression — a struct that needs contract-polymorphism uses `<T: I>`
  static dispatch (works today) or becomes a class.

This keeps every Law: M.1 (fail loud, named), M.3 (gap stated not papered),
ref-transparent-model §8/§9 (no unsound boxing; reject-what-you-can't-prove), and
delivers the whole user-visible keystone (dynamic dispatch + IO1) without waiting
on L1.

---

## 5. Crumb sequence (S/M/L, each gate-able, ritual points)

Ordering guarantees no intermediate build regresses: S1 is a message-only
honest-stop (no behavior change on any passing program); S2 adds regression
fixtures that PIN the already-working class behavior (so a future refactor cannot
silently break it); S3 is the IO1 re-typing (the payoff, a `.tks` change the
implementer makes — NOT this doc); S4 drafts piece-2 coordination. Piece 1
(struct boxing) and piece 3 (own backend) are **not** crumbs here — they are
explicitly deferred (§1, §4.4).

**RITUAL types:** *full-gate* = the complete `teko test .` + positive/native
regression lane + fixpoint (gen1==gen2); *fixture-gate* = the new regression's
own C-native exit assertion.

### Crumb S1 — named honest-stop for struct→interface. **S. Message-only, behavior-preserving. RITUAL: full-gate.**

Replace the generic `argument type mismatch` / `value type does not match
annotation` that a `struct … & Iface` used as an `Iface` value produces, with a
located, named honest-stop. Touch: `typer.tks` (the assignable/arg-check sites
that call `widens_into` — `typer.tks:963`, `1196`, and the annotated-binding path
`typer.tks:1334`), gated behind a new predicate in `resolve.tks`.

```teko
/**
 * struct_conforms_but_not_class — does `from` name a STRUCT that nominally
 * conforms to the interface/trait `to`, yet cannot up-cast to it as a value
 * (a struct has no stable address to back a fat pointer, resolve.tks:785)? True
 * only for the exact "struct implements contract, used as a contract VALUE" case
 * — the one an honest-stop should name. A class `from` (which DOES widen) and a
 * non-conforming `from` both answer false, so the caller's generic mismatch
 * message stands for them.
 *
 * @param from   the source type at a widen/assign/arg site
 * @param to     the destination (expected) type
 * @param table  the program's type table (conformance + kind probes)
 * @return       true iff `from` is a struct conforming to contract `to`
 * @see          widens_into_at — the value-widen gate this diagnoses the gap of
 * @since 0.3.0.29 interface-value keystone
 */
pub fn struct_conforms_but_not_class(from: Type, to: Type, table: TypeTable) -> bool {
    match from {
        Named as fn_ => match to {
            Named as tn => is_struct_name(fn_.name, table)
                && (is_interface_name(tn.name, table) || is_trait_name(tn.name, table))
                && type_conforms_to(fn_.name, tn.name, table)
            _ => false
        }
        _ => false
    }
}
```

The typer sites, on a failed `widens_into`, consult this predicate FIRST and, when
true, return (via `teko::error::err_typed`):

```
"a `struct` value cannot be used as the interface `<Iface>` — a struct has no
 stable address to dispatch through. Make the implementer a `class` (`type T =
 class & <Iface>`), or take it by a generic contract-bound parameter
 (`fn f<T: <Iface>>(x: T)`), which dispatches statically. (interface-value
 boxing of a struct is deferred to the safety spine — docs/design/
 interface-value-type.md §4)"
```

`is_struct_name` mirror (add to `resolve.tks` beside `is_class_name`
`resolve.tks:830` / `is_interface_name`):

```teko
/**
 * is_struct_name — is `name` declared as a `struct` (a value type, no vtable)
 * in the folded type table? The value-type twin of is_class_name/
 * is_interface_name, used by the struct→interface honest-stop and by any
 * value-vs-reference dispatch decision.
 *
 * @param name   a resolved (canonical or bare) type name
 * @param table  the program's type table
 * @return       true iff `name`'s TypeDecl body is a StructBody
 * @see          is_class_name, is_interface_name
 * @since 0.3.0.29 interface-value keystone
 */
pub fn is_struct_name(name: str, table: TypeTable) -> bool {
    match type_table_find(table, name, "") {
        parser::TypeDecl as td => match td.body { parser::StructBody => true; _ => false }
        error => false
    }
}
```

### Crumb S2 — pin the class interface-value surface with regressions. **M. Fixture-only. RITUAL: fixture-gate (both fixtures) + full-gate.**

Add regressions that lock the ALREADY-WORKING class behavior so it cannot silently
regress under the null-union / own-backend waves that touch the same files. See §7
for the fixture table. No product code changes.

### Crumb S3 — IO1: re-type the stream combinators onto interface values. **L. `.tks` product change (implementer, NOT this doc). RITUAL: full-gate + the stream module's own `.tkt`.**

Re-type `src/io/stream.tks` implementers as **classes** and the combinators onto
interface values. Function shapes the implementer copies (full Javadoc), replacing
the `ReadFn`/`WriteFn` closure seam (`stream.tks:236-257`, `277-460`):

```teko
/**
 * read_all — drain a stream to end-of-stream, concatenating every chunk. The
 * post-keystone shape: a dynamic `Reader` value replaces the `ReadFn` closure
 * seam (stream.tks:277) now that a class implementer up-casts into the contract
 * and dispatches through its vtable (docs/design/interface-value-type.md §3).
 * EOF is a zero-length read (§5.2), never an error; a genuine failure returns it.
 *
 * @param r  the source stream, taken as a dynamic interface value (a conforming
 *           `class & Reader`); NON-ESCAPING — `r` is dispatched in the loop and
 *           never stored past this frame (§4.3 borrow-down soundness)
 * @return   the fully-drained bytes, or the first read error encountered
 * @throws   the `error` a `read` call returns, propagated verbatim
 * @see      copy, read_exact
 * @since IO1 (interface-value keystone)
 */
pub fn read_all(r: Reader) -> Buf | error {
    mut collected = Buf::empty()
    loop {
        let chunk = match read_chunk(r, DEFAULT_CHUNK_SIZE) { Buf as b => b; error as e => return e }
        if chunk.len() == 0 { break }
        collected = collected.concat(chunk)
    }
    collected
}
```

`read_chunk`/`write_chunk` are thin adapters that call the contract method
(`r.read(into)`/`w.write(from)`) — needed only while `Buf` mutation-in-place is
still an N-KEYSTONE gap (`stream.tks:29-39`); if the stream `read(self, into: Buf)`
contract is honored directly, the combinator calls `r.read(buf)` inline and
`read_chunk` collapses away. `copy`, `read_exact`, `write_all`, the buffering
wrappers and `tee_write_fn` re-type the same way (`WriteFn → Writer`, `ReadFn →
Reader`). `NullStream`/`MemReader`/`MemWriter` become `class & …`. **This is a
`.tks` change; this doc only specifies it — the implementer executes it under S3's
gate. It is sequenced AFTER S1/S2 and after the seed carries class-interface
dispatch (it does — 0.3.0.28), so the corpus never uses a feature absent from its
seed.**

### Crumb S4 — piece-2 coordination stub (interface-in-union). **S. Doc + fixture-stub. RITUAL: none (design-only).** 

Do NOT relax `resolve.tks:1397` here. Record the contract (§6) and add a
compile-fail fixture that PINS the current honest-stop (so null-union's Crumb 2,
which edits that exact validation, must consciously decide the interface-in-union
arm rather than silently flip it). See §6/§7.

**Crumb count: 4 (S1 small, S2 medium, S3 large, S4 small). Deferred, not
crumbed: struct→interface boxing (spine/L1), own-backend interface lowering
(emit-throughput track).**

---

## 6. Merge-order & conflict notes (vs null-union C1-C2 and CF4)

**Files in contention:** `type.tks`, `resolve.tks`, `typer.tks`, `codegen.tks` —
the same set the ratified null-union wave (`docs/design/null-as-union-type.md`,
branch `feat/0.3.0.29-null-union-c1c2`) and CF4 (`feat/0.3.0.29-cf4-index-fold`)
touch. Verified: `origin/remodel/emit-throughput` still carries the OLD `Optional`
Type case (`type.tks:93`, `… | Optional | …`, no `Null`) — **null-union has NOT
yet landed on the branch base**, so ordering is a live decision.

### 6.1 The exhaustive-`Type`-match arms both waves add

Null-union Crumb 1 adds a `Null` case to `pub type Type = variant …`
(`type.tks:93`) and an arm to **every** exhaustive `match` over `checker::Type`
(type_eq / subst_type / type_mangle / codegen / backends — null-as-union-type.md
§8-C1). This keystone adds **no new `Type` case** (an interface is a `Named`, not a
new variant) — so **the two waves do NOT both edit the `Type` variant declaration**;
only null-union does. That removes the worst class of conflict.

### 6.2 The one true overlap: `resolve.tks` union-member validation

Both waves edit the SAME function — the variant-member gate at `resolve.tks:1387`
(the block containing the interface-in-union reject at `1397`):

- **Null-union Crumb 2** admits a `Null` member and **relaxes the `Reference`
  rejection** to the two-member `Ref<T> | null` shape (null-as-union-type.md §8-C2).
- **This keystone (piece 2 / Crumb S4)** would, later, relax the **interface**
  rejection at `1397` to admit `Iface | null` (niche) and eventually `Iface |
  error`.

**MERGE-ORDER CONTRACT (recommended): null-union BASE first, this keystone rebases
on top.** Rationale, law-first (passes-all-Laws wins):

1. Null-union is **ratified** (owner, 2026-07-19) and structural (it deletes
   `Optional`, redefines nullability). This keystone is **additive on classes** and
   defers its only overlapping piece (interface-in-union). The larger, ratified,
   structural wave is the stable base; the additive one rebases — minimizes total
   churn and keeps one canonical union-member gate.
2. `Iface | null` **is a null-union niche member** (§2.3): its representation is
   defined by null-union Crumb 3's `cg_union_niche_member`, not by this keystone.
   Building interface-in-union BEFORE null-union would force a throwaway second
   niche path. After null-union, admitting the interface arm is a one-line addition
   to `cg_union_niche_member`'s eligibility (class/ptr/Ref → **+ interface fat
   pointer**) plus dropping the `1397` reject.
3. **This keystone's SHIPPING half (S1/S2/S3) does NOT touch `resolve.tks:1387`
   at all** — S1 adds a NEW predicate + edits typer arg/annotation sites; S2/S3 are
   fixtures/`.tks`. So the class half is **interoperable in either order** with
   null-union; only S4 (deferred, design-only) has the ordering dependency.

**Concretely:** land S1+S2 anytime (they conflict with neither wave — S1's typer
edits are at `963/1196/1334`, disjoint from null-union's `Optional`-removal edits;
CF4 is index-const-folding in `codegen.tks`/consteval, disjoint from all interface
sites). Land S3 (IO1) after S1+S2. Hold S4 until null-union's Crumb 2/3 are in,
then fold the interface arm into the unified `Variant`/niche path.

### 6.3 vs CF4

CF4 (`feat/0.3.0.29-cf4-index-fold`) folds constant array-index reads — it lives in
consteval/comptime-fold + a codegen emit path, with **no interface/class/union
touch**. **No conflict.** Order-independent.

### 6.4 Downstream consumer: the embed VFS

`docs/design/embed-vfs.md` converges (owner ruling 3) on a dedicated read-only
`FileSystem` type with `read`/`exists`/`list`. The owner's **segregated
read/write FS interfaces** ruling sits directly on THIS keystone: the read side is
a `Reader`-shaped contract, the write side a `Writer`-shaped contract, and a
read-only VFS conforms to **only** the read contract — enforced exactly by nominal
conformance (`type_conforms_to`, `resolve.tks`) + interface-value parameters
(`fn f(r: ReadableFS)`). **No new mechanism** — the VFS is a class conforming to a
read-only interface, passed as an interface value. Note it as the first
post-IO1 consumer; it needs S3's class-interface-value pattern, nothing more.

---

## 7. Regression fixtures (inputs → expected C-native exit)

VM is retired (issue #524); all exits are **C-native** (`teko build <dir> -o bin`
then run) — the positive/native regression lane. Own-backend (`--backend=native`)
is NOT asserted for these (interface layout is unbuilt there, §1 piece 3); each
fixture's `.tkp` header states C-native-only, mirroring the `NATIVE_ONLY`/honest-
skip convention (`class_slices.tkp`).

**Crumb S2 — pin the working class surface (positive):**

| fixture | shape | expected |
|---|---|---|
| `iface_value_param_dispatch` | `class & Reader` passed to `fn pull(r: Reader, n)`, body `r.read(n)`; sum of two reads | **exit 7** |
| `iface_value_stateful_class` | `class & Counter` with mutating `tick(self)`, driven 3× through a `Counter` param | **exit 3** |
| `iface_value_hetero_slice` | `[]Shape` = `[Circle, Square]`, iterate + dispatch `area()` (already the `class_slices` #5 case, extracted as its own pinned fixture) | **exit 0** |
| `iface_value_return` | `fn pick(b: bool) -> Reader` returning one of two conforming classes; caller dispatches | **exit 0** |
| `iface_value_field` | a struct field `r: Reader` holding a class-derived interface value (class object outlives the struct — §4.1 sound), dispatched | **exit 0** |

**Crumb S1 — the struct honest-stop (compile-fail):**

| fixture | shape | expected |
|---|---|---|
| `iface_value_struct_rejected` | `struct … & Reader` passed to `fn pull(r: Reader, …)` | **compile error**, message contains "a `struct` value cannot be used as the interface" + "Make the implementer a `class`" (compile_fail lane, `scripts/compile_fail_regressions.sh`) |
| `iface_constraint_struct_ok` | the SAME struct passed to `fn pull_g<T: Reader>(r: T, …)` (proves the honest-stop names a REAL alternative that works) | **exit 7** |

**Crumb S4 — pin the interface-in-union honest-stop (compile-fail, coordination
pin):**

| fixture | shape | expected |
|---|---|---|
| `iface_union_member_rejected` | `fn f() -> Reader \| error` (interface as a union member) | **compile error** containing "an interface cannot be a variant member yet" (pins `resolve.tks:1397` so null-union Crumb 2 must consciously handle the interface arm) |

**Crumb S3 — IO1 (the stream module's own `.tkt`):** the re-typed combinators
keep the existing `src/io/*` test coverage; add a `class & Reader`/`class & Writer`
round-trip: a `MemReader` class drained by `read_all(r: Reader)` and copied into a
`MemWriter` class via `copy(w: Writer, r: Reader)`, asserting byte-identical
content and total-bytes-moved. (Executed under S3, authored by the implementer.)

---

## 8. Report (summary for the integrator)

- **Branch created:** `design/interface-value-type`, off
  `origin/remodel/emit-throughput` (worktree; the main tree's uncommitted
  null-union change was left untouched). **Commit:** this doc only — no `.tks`/
  `.tkt`/product file touched (READ-ONLY honored). No version bump.
- **Premise correction (the headline):** the stated symptom (`unknown type:
  Reader`, "interface cannot be a value type") is **stale**. Interface-as-value +
  dynamic dispatch is **already shipped for CLASSES** on the C backend — resolved
  as a value type, up-cast (`resolve.tks:794`), fat-pointer boxed
  (`codegen.tks:4412`), vtable-dispatched (`codegen.tks:2179`), heterogeneous
  slices and covariant upcast proven by `class_slices` (#102), stateful dispatch
  reproduced (exit 3). Evidence table in §0.
- **Crumb count/sizes:** 4 crumbs — S1 (S, named honest-stop), S2 (M, pin
  fixtures), S3 (L, IO1 re-typing = the payoff), S4 (S, coordination pin). Two
  further pieces are **deferred, not crumbed**: struct→interface boxing (spine) and
  own-backend interface lowering (emit-throughput track).
- **Spine-dependency verdict:** the **shipping keystone does NOT halt on L1.** The
  full user-visible value (dynamic dispatch + heterogeneous collections + IO1
  re-typing) rides **classes**, sound under existing class-arena rules (§4.1), no
  spine. The ONE piece that needs the spine — **struct→interface value boxing** —
  is genuinely blocked on A1/L1 (transitive escape) for its escaping form; its
  non-escaping subset is near-worthless for streams anyway (value structs cannot
  hold mutable stream state, §4.3). It is **deferred with a named honest-stop
  (S1)**, not built. **No HALT** — the tension resolves law-first
  (`ref-transparent-model.md` §8/§9: stateful → reference semantics → class; box
  nothing you cannot prove non-escaping).
- **Merge-order contract:** **null-union BASE first, this keystone rebases on
  top.** The class half (S1/S2/S3) is **interoperable in EITHER order** (its edits
  are disjoint from null-union's `Optional`-removal and CF4's index-fold). The only
  overlap is the deferred S4 (interface-in-union at `resolve.tks:1397`), which MUST
  land AFTER null-union so `Iface | null` reuses null-union's niche path
  (`cg_union_niche_member`) instead of a throwaway second one. **CF4 is
  order-independent** (no interface/class/union touch). The embed VFS
  (`embed-vfs.md`) is the first post-IO1 consumer (segregated read/write FS =
  read-only interface value), needing S3's pattern and nothing new.
- **Adjacent finding (REPORTED, not actioned):** the OWN/native backend
  (`--backend=native`) has no class/interface/struct layout yet (N1 subset,
  `lir/lower.tks`); `class & Reader` fails there with `no layout registered`. That
  is the emit-throughput remodel's own aggregate-lowering track, out of this
  keystone's scope; flagged so the roadmap owner sequences interface-value native
  lowering with the rest of the aggregate work, not as a surprise.

---

*Grounding: reproduced against `bin/teko` 0.3.0.28 this session (evidence table
§0); all file:line against `origin/remodel/emit-throughput`. Companion designs:
`ref-transparent-model.md` (memory model / spine), `null-as-union-type.md`
(the co-located wave), `oop-this-base-static.md` (class model), `embed-vfs.md`
(downstream consumer).*
