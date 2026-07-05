# TEKO_EVOLUTION_DESIGN — the unified evolution design (collections · generics · memory/arenas · ref · concurrency)

> **⚖️ RATIFIED (2026-06-25) — two legislator rulings move this from study to committed work:**
>
> **R1 — Evolution is NOT deferred; it is the committed next phase.** *"Without it, we can't evolve more,
> the codebase is too 'locked' without these things."* Generics + memory control (arenas/`ref`) +
> real collections + concurrency are **foundational, not optional** — a metal language without them cannot
> grow a stdlib, run long-lived processes, or escape hand-duplicated containers. They are therefore to be
> **BUILT** along the S1–S9 sequence (§5.2), as the **immediate campaign after self-host**, *not* "someday."
> The sequencing nuance (integrated/legislator-accepted): minimal copy-append **now** (forward-compatible,
> locks nothing) to **reach self-host**, then build the foundation in Teko (cheaper than twice-in-C),
> **keystone first** (S1→S2 arena + escape check). The "lock" is real; the unlock is reaching self-host
> cheaply, then building the foundation immediately — not indefinitely deferring it.
>
> **R2 — `ref` is mutable-only.** *"`ref` only mutable values can be referred, not immutable."* There is
> **no immutable/shared `ref`**. A `ref` may target **only a mutable value** and always carries mutation
> intent; read-only sharing is **copy** (copy-by-default, M.0). This *collapses* the `ref`/`ref mut`
> distinction to a single `ref` (mutable), **removes** the shared-XOR-mutable "aliasing rider" (§3.1) —
> strengthening the "one escape check, **no borrow-solver**" invariant — and is folded into S3 below.
> Accepted trade-off: read-only aliasing of a *large* value must **copy** (or await a future narrow
> read-view); fine while values are small (the corpus norm). See §1 / §3.1 / §5.2-S3 for the folded form.
>
> **Status:** design synthesis with the two rulings above now binding. This document unifies five forward
> studies — the already-decided **collections** mechanism plus four design memos (**generics**,
> **arenas/memory**, **ref/pointers**, **concurrency**) — into ONE coherent evolution design for teko-lang.
> It states the integrated feature set, how the areas depend on one another, a single recommended build
> sequence (self-host **now** vs. the staged evolution), and the doctrine-fit against the Laws M.0–M.5.
>
> **Scope discipline (M.4):** "decision-ready" here means *semantics and sequencing* are ready; exact
> **surface syntax** for arenas, `ref`, and concurrency is deliberately **reserved** until the parser and
> real self-hosted-duplication data exist. Freezing syntax now would be building on the incomplete.
>
> **What it does NOT change:** the seed. Today's model — fixed-size arrays + COPY, value semantics, no
> `ref`, no generics, malloc-everywhere/leak-tolerant, single-threaded — is **correct** for self-host
> under M.5 and is the floor every stage below builds *on top of*, never inside.

---

## 0. The one-paragraph thesis

teko-lang's evolution is **one mechanism wearing five faces**. The metal value model is *copy by default*;
the memory model is *a tree of bump-allocated arenas, bulk-freed by lexical span, with a single
compile-time escape check (a region-depth comparison)*; `ref` is *a bare address into an arena, made safe
by that same escape check*; generics are *monomorphization — the user writing what the compiler already
does internally for arrays, lists, and `parse`*; collections are *generics' first real customer over
arena storage*; and concurrency is *the capstone — channels move owned data, structured `scope`s bound
task lifetimes, and the escape check gains one axis ("task scope") to deliver data-race freedom for free*.
**The arena escape check is the single load-bearing invariant** under `ref`, deterministic free, real
collections, DI lifetimes, and data-race freedom. Build it once; everything safe downstream reuses it.

---

## 1. The integrated feature set (what the full language gains)

> **⚠️ Concurrency row note (2026-07-04):** the Concurrency row shows the OLD design (scope/spawn/channel,
> async rejected). This was **SUPERSEDED 2026-06-30** by the ratified design: Intent<T> + async/await.
> See issue #164 for the new design. The rest of this table (Collections, Generics, Memory, ref) remains current.

| Area | Seed (today) | Evolution adds | Mechanism | New surface |
|---|---|---|---|---|
| **Collections** | fixed `[]T` + COPY-append (`xs+[x]`, `teko::list::push`); read-side (`[]T as x`, `.len`, indexing) | real dynamic `[]T` (amortized append), `Map<K,V>`, `Set<T>` | special-cased now → monomorphized + arena-backed later | none now; `Map`/`Set` types later |
| **Generics** | none (per-call special-casing: `print`, `list`, `parse`, `assert`) | `fn f<T>`, `type Box<T>`, `Map<K,V>` | **monomorphization** (compile-time stamping); never dictionaries | `<T>` type-param lists (lexer/parser addition) |
| **Memory/arenas** | malloc-everywhere, leak-tolerant (M.5-correct for batch) | root/scope/local arenas; deterministic bulk free; `#singleton`/`#scoped`/`#transient` wired to arenas | bump-allocator tree + **escape check (depth compare)** | `#`-directives exist; explicit-scope + out-region reserved |
| **ref/pointers** | none (value-only; bare `self` + `mut`-local close the door by construction); `ptr`/`uptr` opaque FFI-only | `ref T` — **mutable target only (R2)**; read-only sharing = copy | arena-scoped reference; soundness = the escape check, **no lifetime variables, no shared/unique split** | `ref`, three positions (reserved) |
| **Concurrency** ⚠️ **SUPERSEDED** | none (single-threaded, thread-ready: no global mutable state, no magic closures) | `scope{}`, `spawn`, `channel<T>`, `send`, `recv` **(OLD)** | CSP channels + structured concurrency; move-on-send; 1:1 OS threads | five primitives (reserved) |

The full language is therefore: *a metal, no-GC, copy-by-default systems language whose escapes from copy
(`ref`), whose storage discipline (arenas), whose abstraction (monomorphized generics), whose containers
(real collections/maps/sets), and whose parallelism (CSP + structured scopes) are all governed by one
lexical-region/escape discipline* — Rust's guarantees without Rust's lifetime calculus, bought instead
with copy-default + arena spans + structured scopes.

---

## 2. How the five areas interact and depend on each other

```
                                  ┌──────────────────────────────┐
                                  │  ARENAS  (region tree +       │   ← the keystone
                                  │  ESCAPE CHECK = depth compare)│
                                  └───────┬───────────────┬───────┘
                                          │               │
                       defines soundness  │               │  storage substrate
                                          ▼               ▼
                                    ┌──────────┐     ┌───────────────────┐
                                    │   ref    │     │ DI lifetimes       │
                                    │ ref mut  │     │ #singleton/#scoped/│
                                    └────┬─────┘     │ #transient         │
                            mutate-through│          └───────────────────┘
                                          ▼
   ┌──────────┐  first customer   ┌───────────────────────┐
   │ GENERICS │◄──────────────────┤ real dynamic collections│  needs: generics + arenas + ref-mut
   │ (mono)   │   Map forces       │ Map<K,V> / Set<T>       │
   └────┬─────┘   constraints      └───────────────────────┘
        │ K: Hashable+Eq                       
        │                          ┌───────────────────────────────────────────────────────────┐
        └─ retrofit (optional) ──► │ ⚠️ CONCURRENCY  scope{} / spawn / channel<T>  (SUPERSEDED) │  capstone
                                   │  move-on-send + escape-check("task scope")  (OLD DESIGN)   │  needs: arenas + ref
                                   │  NEW (2026-06-30): Intent<T> + async/await → issue #164  │
                                   └───────────────────────────────────────────────────────────┘
```

**The dependency edges, each doctrine-stated (not invented):**

- **Arenas → ref.** `ref`'s soundness *is* the arena escape check (a `ref` may not outlive its region).
  The door is shut today by construction (bare `self`, `mut`-local); arenas + the escape check are the
  compile-time lock that lets it open safely. *(Constitution M.1 exclusion-by-construction; Legislation
  "ref waits for arenas"; HISTORY §B.10 "safer arenas → ref(&)".)*
- **Arenas → DI lifetimes.** `#singleton/#scoped/#transient` *are* arena spans (root/scope/local). The
  same escape check catches "inject a `#transient` into a `#singleton`". *(Legislation DI-lifetimes;
  HISTORY §B.10.)*
- **Arenas → real collections (storage).** COPY-append already promised "only the runtime changes when
  capacity/arena arrive." Arenas are that runtime change: copy-append's `alloc len+1` becomes
  `region_alloc`, **no signature change**. Real growable vectors are region-resident buffers that
  bump-grow; maps/sets likewise.
- **ref → real (mutate-through) collections.** A `push` that mutates in place needs `ref mut` to the
  backing store. Copy-append (seed) needs no ref; the *real* dynamic collections do.
- **Generics → real collections, Map, Set.** Containers are inherently parametric; `Map<K,V>` /
  `Set<T>` cannot be expressed without a type variable. They are generics' **first real customers**.
- **Map → constraints.** `Map<K,V>` needs `K: Hashable + Eq` — the concrete forcing-function for the
  constraint system. Design constraints *here*, with this real need (M.4), never speculatively.
- **Arenas + ref → concurrency.** A `scope{}` *is* a concurrency arena; the escape check gains one axis
  ("a `ref` may not cross a task boundary unless it outlives the task") and move-on-send is the
  single-owner rule applied to channels. Concurrency invents almost nothing — it reuses the memory layer.
- **Generics ⟂ concurrency.** `channel<T>` is special-cased per element type (print-style) until generics
  arrive, then retrofitted with **no surface change**. Concurrency must **not** be the feature that forces
  generics.

**The collapse of seeming-independent areas into one invariant:** the arena *escape rule* (a single
integer depth comparison) is simultaneously (a) the no-use-after-free guarantee for arenas, (b) the
soundness of `ref`, (c) the lifetime check for DI, and (d) with a "task" axis, the data-race-freedom
guarantee for concurrency. This is why the build sequence is not five projects but one spine with
branches.

---

## 3. Conflicts between the memos, resolved

The five studies were written independently. They are mutually consistent on the big calls; the few seams
are resolved here.

**3.1 What "the escape check" is — unified.** The arena memo specifies it as a **lexical region-depth
comparison** (`region_depth(target) <= region_depth(source)`), *not* a flow-sensitive borrow graph. The
ref memo describes "C#-style escape analysis" plus "aliasing-XOR-mutation per call." The concurrency memo
adds "task scope as a lifetime region." **Resolution:** there is ONE escape check — the depth comparison —
and the additional rules are *small riders on it*, not new analyses:
- Base rider (arenas/ref): a reference/view may be stored/returned only into an equal-or-outer region.
- ~~ref rider (aliasing): aliasing-XOR-mutation per call~~ — **REMOVED by R2.** With `ref` mutable-only
  and **no read/shared `ref`**, there is no shared-vs-unique distinction to police: read-only sharing is
  copy, so the shared-XOR-mutable exclusivity set is *unnecessary*. The escape check reduces to the base
  depth rider plus the concurrency task rider. (This is the simplification R2 buys.)
- concurrency rider (tasks): a region's depth axis gains a "task" dimension; a `ref` may cross into a
  child task iff its region provably outlives the task (structured concurrency makes the parent always
  outlive children, so the common case is legal). 
None of these is a lifetime-variable solver. The borrow-solver / lifetime polymorphism is the **explicitly
deferred ceiling** (the generics-constraint-system tax, B.11), approached only if region discipline is
*proven* insufficient by real self-hosted code (M.4). All three memos agree on this; it is stated once.

**3.2 Out-region parameter vs. region-polymorphism.** The arena memo adopts the single "out-region
parameter" pattern (`fn build(into: region) -> []T`) from Cyclone-style regions but rejects full
region-polymorphism (it *is* generics-over-regions → reintroduces the constraint tax). The ref memo's
"return a `ref` into an equal-or-outer arena" is the same pattern viewed from the callee. **Resolution:**
adopt **only** the out-region parameter as the sanctioned no-copy-return mechanism; reject region
polymorphism. Consistent across both memos.

**3.3 Implicit copy-out on escape — leaning NO.** The arena memo raises whether a small escaping value may
be *silently* copied out. **Resolution (M.3):** **no implicit copy.** A silent copy hides a cost; the
compiler **errors** ("this reference would outlive its region") and the programmer writes the copy or
supplies an out-region. (This is a genuinely open micro-decision for *very small* values, flagged for the
tribunal, but the default lean is explicit.)

**3.4 OS-thread mapping.** The concurrency memo maps `spawn` 1:1 to an OS thread in the first cut (no
hidden M:N scheduler — metal ethos, visible cost), with M:N work-stealing later as a *swappable*
`teko::io`-style boundary. No conflict with other memos; recorded as the chosen substrate.

**3.5 `ref` ≠ `ptr`, permanently.** All memos agree and it is doctrine: `ptr`/`uptr` stay opaque,
transport-only, FFI-unsafe (B.6); `ref` is safe-internal arena aliasing. They never merge (M.3 — no
pretend guarantee). Restated once so no later stage conflates them.

**3.6 Nullability stays orthogonal.** A `ref` is **never null** (exclusion-by-construction — it always
denotes a live arena slot). "Maybe-a-reference" is `(ref T)?` using the existing `?` type-former. Do not
invent nullable references. Disjoint failure domains preserved (null → `?.`/`??`; error → `match`).

---

## 4. Doctrine-fit (M.0 metal/no-GC · M.1 fail-loud/exclusion · M.2/M.3 honest · M.4 build-order · M.5 minimal)

**M.0 — metal, no GC, no hidden runtime, no boxing.**
- Arenas: a bump allocator is a pointer increment; bulk-free is a pointer reset / chunk-list walk. No GC,
  no per-object bookkeeping, no managed heap — "direct arena allocation."
- Generics: monomorphization stamps concrete native code per instantiation — zero runtime dispatch, zero
  boxing, no vtable. Identical to hand-written per-type code. Dictionaries (Option B) are **rejected** —
  they are the "caixa-preta aberta-em-runtime" the doctrine forbids (boxing + fn-pointer dispatch).
- ref: a `ref` is a bare address into a region (native) / a `{arena, ptr}` value tag (VM). No indirection
  tower.
- Concurrency: 1:1 OS threads (clone syscall) + channels (futex/mutex) — no hidden executor, no green-
  thread runtime. async/await is **rejected** (hidden executor = anti-M.0).

**M.1 — fail early/loud; never corrupt in silence; exclusion-by-construction where possible.**
- Arenas/ref: the escape check is **compile-time, exclusion-by-construction** — no use-after-free (a ref
  can't outlive its region), no double-free (you free regions, not objects). OOM panics, never NULL
  (matches `core.h`'s `abort`).
- Concurrency: move-on-send ⇒ no two live owners ⇒ **no data races by construction**; structured `scope`
  ⇒ no leaked/orphaned tasks. async (function-coloring lie) and raw shared-mem+locks (silent races) are
  **rejected** as the surface.
- The hazard floor stays: malloc-leak is **correct** for the batch compiler (M.5), not a corruption —
  the OS reclaims at `exit`.

**M.2 / M.3 — explicit, honest; no overclaim.**
- Lifetimes are **visible scope structure** + `#` directives, not inferred per-pointer. The pitch is
  "region safety," **never** "Rust's borrow checker without the borrow checker" (M.3 — never promise a
  safety you didn't earn). Generics have no erasure / no `any` black box. Every channel hand-off and
  every `scope` is visible at the call site (no coloring, no disguised state machine).

**M.4 — build with real data, in dependency order.**
- Nothing is built before its prerequisite (arenas before ref; ref before mutate-through collections;
  single-task before concurrency). Constraints are designed against the **real** `Map` need, not ahead of
  it. Surface syntax for arenas/ref/concurrency is **reserved** until the parser exists.

**M.5 — minimal weight, pay only for use.**
- **One** region concept covers root/scope/local; it attaches to spans the code already has (a block, a
  function, the program). The escape check is a single integer comparison — dramatically lighter than a
  borrow solver. Generics cost code-size only for instantiations you use. Concurrency is five primitives,
  not a framework. The borrow-solver and region-polymorphism — the two heavy type-system layers — are
  **deferred indefinitely** (the B.11 constraint-system tax), exactly as generics' constraints are.

---

## 5. Self-host NOW vs. the staged evolution

### 5.1 NOW (self-host) — what is needed

| Area | Needed for self-host? | What ships now |
|---|---|---|
| Collections | **YES (read + append)** | COPY-append (`xs+[x]`, `teko::list::push`/`empty`, immutable result) + read-side (`[]T as x`, `.len`, indexing), all special-cased |
| Generics | **NO** | per-call special-casing (`print`, `list`, `parse`/`to_string` per-type, `assert`); user writes zero generics (B.11) |
| Memory/arenas | **NO** | malloc-everywhere + copy-on-extend (M.5-correct); **one no-cost action**: route allocs through a thin `tk_alloc(size)` **seam** so the later malloc→`region_alloc` swap is mechanical |
| ref/pointers | **NO** | value-only; bare `self` + `mut`-local hold the door shut; `ptr`/`uptr` opaque FFI-only |
| Concurrency | **NO** | nothing — seed is single-threaded, short-lived, already thread-ready |

**The actual self-host frontier is none of the five evolution areas.** It is the **slice-value layer +
function params in codegen/VM + optionals** (`TEKO_CORRECTION_PLAN`). Finishing that — *not* generics,
arenas, ref, or concurrency — completes self-host. The only evolution-adjacent action justified now is the
**allocation seam** (a no-cost forward-compatibility discipline), which also honors the collections study's
"forward-compatible signature" promise for the memory layer.

### 5.2 The recommended BUILD SEQUENCE (post-self-host)

```
SEED / NOW (no evolution features):
  [done]  print/interp · list COPY-append · parse/to_string per-type · []T read-side
  [next]  slice-value layer + fn params in codegen/VM + optionals   ← FINISHES SELF-HOST (not evolution)
  [now, no-cost]  tk_alloc() allocation SEAM + str/slice stay {ptr,len} views   ← the swap point
        │
        ▼  ──────────────────────────── self-host complete; M.4: design with real duplication data
EVOLUTION (staged, dependency-ordered):

  S1  ARENA PRIMITIVE + root region
        bump allocator (region/region_alloc/region_drop), chunk-list, OOM-panics.
        Wire the process into one root region = today's leak (no behavior change) → proves the primitive.
        deps: allocation seam.

  S2  SCOPE REGIONS + ESCAPE CHECK (depth compare)        ★ the linchpin
        first real safety win; enables #scoped (deterministic per-scope free → long-running processes).
        deps: S1.   ── everything safe downstream reuses this check.
        │
        ├── S3  ref  (MUTABLE-TARGET ONLY — R2; three C# positions; escape-checked; NO shared/read ref,
        │         NO aliasing rider; read-only sharing = copy; ref never null → (ref T)?; ref ≠ ptr).
        │         Concurrency send/sync substrate SPECIFIED here.  deps: S2.
        │
        └── S5  DI LIFETIMES wired to arenas (#singleton/#scoped/#transient → root/scope/local);
                  inject + binding → (interfaces → OOP).   deps: S2 (+ S3 for inject-by-ref / use(&x)).

  S4  GENERICS (unconstrained, monomorphization)          can proceed in parallel with S1–S3
        fn f<T>, type Box<T>, Map<K,V> shape; monomorphization pass: checker → expand → codegen/VM.
        first customers: Result<T> / T|error family (thin, one type var — early validation);
        value List<T>; Box<T> (once ref exists).
        deps: self-host (M.4 data). Independent of arenas for the MECHANISM; ref-based instances need S3.

  S6  CONSTRAINTS (positive, C#-style; exclusion `!` only prims/sealed)
        forced by Map<K,V> needing K: Hashable + Eq. Design HERE, against real data — never speculatively.
        deps: S4 + the real Map need.

  S7  REAL DYNAMIC COLLECTIONS  (amortized append, Map<K,V>, Set<T>)
        the generic payload behind the already-decided surface — "only the runtime changes."
        out-region parameter for no-copy build. deps: S4 (+ S6 for Map) + S2/S3 (arena + ref mut) .

  S8  CONCURRENCY  (capstone — LAST)
        ⚠️ **SUPERSEDED (2026-06-30) → issue #164** — ratified design: Intent<T> + async/await.
        See issue #164 for the new design (Intent<T> union, async/await keywords, threading/channels/sync/Context).
        This section describes the OLD model (scope/spawn/channel, async rejected) kept for historical reference.
        ────────────────────────────────────────────────────────────────────────────────
        [HISTORIC MODEL]
        scope{} / spawn / channel<T> / send / recv -> T|error.  1:1 OS threads (M:N later, swappable).
        data-race freedom = move-on-send + escape check with "task scope" axis (reuses S2/S3).
        independent of generics (channel<T> special-cased now, retrofit later — never forces generics).
        deps: S2 (arenas) + S3 (ref/escape).   then C-scaling (M:N, SIMD) optional.

  S9  LTS CLEANUP (refinement, not capability): collapse Parsed<T> ×14, unify parse/to_string.
        deps: S4.
```

**Hard ordering invariants (each doctrine-stated):**
1. Arenas (S1–S2) **before** ref (S3) — soundness of ref *is* the escape check.
2. Arenas (S1–S2) **before** DI lifetimes (S5) and real collections' *storage* (S7).
3. ref (S3) **before** mutate-through collections (S7).
4. Generics (S4) **before** real collections / Map / Set (S7); Map (S7) **forces** constraints (S6).
5. Single-task arenas + ref (S1–S3) **before** concurrency (S8) — M.4.
6. Concurrency (S8) is **independent of** generics (retrofit, never a prerequisite).
7. The borrow-solver / lifetime polymorphism / region polymorphism is the **deferred ceiling** —
   approached only if region discipline is proven insufficient by real code.

**Parallelism in the plan:** after self-host, **arena/ref (S1→S2→S3)** and **unconstrained generics
(S4)** can proceed *in parallel* — generics' mechanism needs neither arenas nor ref. They converge at S7
(real collections need both) and S8 (concurrency needs arena+ref, not generics).

---

## 6. The single most important decision the legislator must make

> **Ratify the arena escape rule as ONE lexical region-depth check — and with it, the standing refusal to
> build a borrow-solver / lifetime-variable system — as the load-bearing safety invariant for the entire
> evolution (arenas, `ref`, DI lifetimes, real collections, and concurrency's data-race freedom).**

Everything else in this design is downstream of, or independent of, that one call. The arena memo, the ref
memo, and the concurrency memo each independently arrive at it; the generics memo (B.11) supplies its
justification (the constraint/lifetime *solver*, not the mechanism, is where the doctrine can be wounded).
If the legislator ratifies "the region is the lifetime; soundness is a depth comparison; no lifetime
variables until real code proves them necessary," then ref, DI, collections-storage, and data-race freedom
**all fall out of the same machinery** and the build sequence above holds. If instead the legislator wants
Rust-grade expressiveness (self-referential cross-region structures, lifetime polymorphism), that pulls in
the borrow-solver — the exact constraint-system illegibility tax B.11 deferred — and re-shapes ref,
arenas, and concurrency together. **This is the keystone ruling; make it first, because every other
evolution decision rests on it.**
