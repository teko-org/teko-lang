# FASE-1b — implementer-ready crumb sequence: generic collections + `IHash` (gated on the 9-ops reseed)

> **Status:** DESIGN — crumb sequence. Read-only on product code; this file is the SOLE edit. NO build,
> NO reseed, `teko test` NOT run in any form. Isolated worktree off `origin/fix/retirement`, branch
> `design/collections-generics-crumbs`; the main checkout + other agents' worktrees UNTOUCHED.
>
> **What this doc is.** It turns the SEALED generic section (§1–§2) of
> `plano-collections-genericas-e-concorrentes-0.3.1.md` into an ORDERED, gate-able crumb sequence for
> **FASE-1b** — the capability-constrained generic collections — contracting against the DECLARED shapes
> of its two dependencies: **9-ops** (`IEq`/`IOrd` interface-operators + generic operator dispatch, crumb
> 4/6 of `plano-9ops-interface-operador-e-overload-composicao-0.3.1.md`, LANDING now) and **#254**
> (generic methods + static factories, DONE). The CONCURRENT section (§3, Option B) is out of scope — it
> is re-planned separately (`design/collections-optionB`). Every `file:line` below is re-verified on this
> worktree; §1 lists the drift.
>
> **The GATE (seed discipline — load-bearing).** The FASE-1b corpus uses `T: IEq`/`T: IOrd`,
> `operator __eq`, and `==`/`<` over a constrained `T` — features that exist ONLY AFTER the 9-ops reseed.
> The bootstrap seed must never USE a feature absent from itself, so **FASE-1b may enter only a seed that
> already contains 9-ops.** Sequence: 9-ops reseed → FASE-1b corpus reseed. (Two crumbs — G1's `IHash`
> interface and G2's `arr_*` combinators — need only #254/DONE and could enter the current seed, but there
> is no reason to split the track; fold them into the post-9-ops FASE-1b reseed for one mechanism.)
>
> **Sealed law drawn around (not reopened):** collections-as-class + reference semantics (`map.tks:1-6`);
> the sibling-link rule → shared logic is FREE generic functions, not private methods
> (`collections.tks:9-15`); `Map<V>` stays str-keyed and is NOT removed (`teko::env` depends). Teko-only,
> W15/Javadoc (every snippet copy-ready), two-instantiation `.tkt` per type (`list_test.tkt`/
> `map_test.tkt` pattern).

---

## 1. Anchor re-verification (on `origin/fix/retirement`, HEAD `5f0d701c`)

| what | ACTUAL (verified) | design doc / note |
|---|---|---|
| pure array combinators (fold base) | `src/collections/collections.tks` — `arr_replace_at<T>:29`, `arr_drop_at<T>:49`, `arr_drop_last<T>:67` (all free generic, value-functional) | ✓ the extension template |
| `List<T>` class + `make()` factory | `src/collections/list.tks` (`pub type List<T> = class`, `make()` #254 L5) | ✓ molde |
| `Map<V>` str-keyed class + `map_find_index` | `src/collections/map.tks:30` (class), free `map_find_index` `:143`; the `str`-not-`K` ruling `:9-18` | ✓ the shape `Dictionary` generalises |
| two-instantiation `.tkt` pattern | `src/collections/list_test.tkt`, `map_test.tkt` | ✓ inherited |
| `teko::list::push`/`empty` (array primitive) | checker builtins (`type_list_builtin`); `grow<T>` at `src/list/list.tks:17` | ✓ |
| `Iterator<T>` contract | `src/iter/iter.tks:28` — `pub type Iterator<T> = interface { fn next(): T | null }` | the `iter()` door |
| `str_hash` (StrKey ref body) | `src/runtime/teko_rt.tks:529` — `exp fn str_hash(s: str): u64` (FNV-1a) | ✓ |
| `str_cmp` (IOrd-str ref body) | `src/runtime/teko_rt.tks:538` (doc-comment; the Teko twin of `tk_str_cmp`) | doc said `:536` (drift −2) |
| #254 method-over-`T` dispatch (IHash's path) | `iface_methods_by_name` + `constraint_interfaces` — used at `src/checker/typer.tks:1107`, `:2417`, `:2427`, `:2442` | doc cited `typer.tks:2197` — cite the MECHANISM, the exact line drifted |
| 9-ops `IEq`/`IOrd` sealed decls | `plano-9ops-…-0.3.1.md:341-357` (`IEq { __eq; __ne }`, `IOrd { __lt; __gt; __le; __ge }`), `NeByEq:367`, `GtByLt:377` | the DECLARED shape the corpus references |
| 9-ops generic operator dispatch (`==`/`<` over `T`) | that doc's **crumb 4** (RITUAL); reuses the #254 method-over-`T` seam | the key FASE-1b unblock |
| host module for `IEq`/`IOrd`/`IHash` | `src/cmp/` does NOT exist (its `cmp.tks` was dropped in `07588731`, "outran the seed"); `src/core.tks` EXISTS | **coordination point — §6.1** |

**Confirmed:** `sort_str`/`sort_i64` are CONCRETE (`src/sort/sort.tks:81`/`:157`), not `sort<T: IOrd>` —
the generic sort is ADJACENT (unblocked by 9-ops, reported not built here).

---

## 2. The crumb sequence (ordered; each gate-able; ★ = ritual after the 9-ops reseed)

Dependency spine: **G1 → G3/G4** (capabilities before the hash collections), **G2 → G5** (combinators
before the ordered collections), **G3 → G6** (Dictionary before the Map migration). G1's `IHash`
interface and G2's `arr_*` need only #254; everything else needs the 9-ops reseed.

### G1 — the capability corpus: `IHash` + `StrKey` + prim adapters

**Goal.** Supply the hashing half that 9-ops does NOT (it delivers `IEq`/`IOrd` operators only). `IHash`
is a **METHOD interface** (`fn hash(): u64`) — NOT an operator; it dispatches through the ordinary
generic-method-on-`T` path (#254, `iface_methods_by_name`/`constraint_interfaces`), which is DONE. So the
`IHash` interface itself compiles on the CURRENT seed; `StrKey` (which writes `operator __eq`) gates on
9-ops.

**Where.** Co-locate `IHash` with `IEq`/`IOrd` — the three capabilities must cohabit so a key constraint
`K: IEq & IHash` resolves both from one namespace. That module is 9-ops crumb 6's to pin (`src/cmp/` when
re-created, or `src/core.tks`); **§6.1 flags this coordination point.** The sealed full-Javadoc shapes are
in the design doc §2.2 (`IHash`, `StrKey`) — copy verbatim. Reproduced here for the crumb record:

```teko
/**
 * IHash — the hashing capability, a METHOD interface (not an operator: there is no `operator hash`
 * token). A type conforms by writing `fn hash(): u64`, dispatched through the generic-method-on-`T`
 * path (#254, iface_methods_by_name/constraint_interfaces) — the half 9-ops's interface-operator layer
 * does NOT deliver. A hash-keyed collection constrains its key `K: IEq & IHash`: `IHash.hash()` buckets
 * a key (compared as a cheap `u64` before the full key), `IEq.__eq` disambiguates a collision. CONTRACT:
 * two keys that compare `__eq`-equal MUST return the same `hash()` (hash-eq consistency) — violating it
 * silently corrupts lookup, so a conformer's `hash` and `__eq` must read the same fields.
 *
 * @return a 64-bit hash of the receiver, stable within one process run
 * @see IEq (the equality half a hash-keyed collection also requires)
 * @since 0.3.1
 */
type IHash = interface {
    fn hash(): u64
}
```

`StrKey` (design doc §2.2, `struct IEq & IHash & NeByEq { key: str }`, `hash()` = `str_hash(self.key)`,
`operator __eq` = byte-equal) — reuses the exact FNV-1a (`teko_rt.tks:529`) + `str` `==` (`tk_str_eq`)
that shipped `Map<V>` proves. **Gates on 9-ops** (`operator __eq` needs crumbs 1-2 parse + crumb 4
dispatch). Prim adapters (`i64`/`u64` keys) follow the same wrapper molde.

**Existing fns touched:** none — new corpus in the IEq/IOrd module + `str_hash` reused. **Gate (`.tkt`):**
`ihash_strkey_test.tkt` — two `StrKey`s with equal `key` return equal `hash()` AND compare `__eq`-equal
(the consistency law); distinct keys differ; a `StrKey` round-trips as a dict key.

### G2 — combinator extension in `collections.tks` (arr_* now; ordered helpers with 9-ops)

**Goal.** Extend the free-generic combinator vault the collections fold over. Two tiers by gate.

**Tier A — un-gated (only #254, buildable NOW), molde `arr_replace_at`:**

```teko
/**
 * arr_insert_at — a copy of `xs` with `v` inserted BEFORE index `at` (elements at `at…` shift up by
 * one); `at >= xs.len` appends. Value-functional (Teko arrays are snapshots); a FREE generic function,
 * not a method, per the sibling-link rule (collections.tks:9-15). The backing shape a `Deque`
 * front-insert and a `SortedSet` ordered-insert both reassign a field to.
 *
 * @param xs  the source array
 * @param at  the index to insert before
 * @param v   the element to insert
 * @return    a fresh array with `v` at position `min(at, xs.len)`
 */
pub fn arr_insert_at<T>(xs: []T, at: u64, v: T): []T { /* loop: push xs[i], emit v at i==at, else append */ }

/** arr_swap<T>(xs, i, j): []T — a copy with positions `i` and `j` exchanged (out-of-range → unchanged copy). */
/** arr_reverse<T>(xs): []T — a copy in reverse order. */
/** arr_slice<T>(xs, from, to): []T — a copy of the half-open range `[from, min(to, xs.len))` (from>=to → empty). */
```

**Tier B — IOrd-gated (with the 9-ops reseed), the ordered/heap helpers G5 consumes:**

```teko
/** sorted_insert<T: IOrd>(ref items: []T, x: T): bool — binary-search `x`'s sorted position over `<`,
 *  insert if absent (via arr_insert_at), returning whether newly inserted; a present element
 *  (`!(x<e) && !(e<x)`) is a no-op. FREE generic (ref-threaded), the SortedSet/SortedDictionary backer. */
fn sorted_insert<T: IOrd>(ref items: []T, x: T): bool

/** heap_sift_up<T: IOrd>(heap: []T): []T — restore the min-heap property upward after a push. */
/** heap_pop_min<T: IOrd>(ref heap: []T): T | null — remove+return the minimum (sift down), or null if empty. */
```

**Existing fns touched:** none — additive free functions in `collections.tks`. **Gate (`.tkt`):**
`arr_combinators_test.tkt` (Tier A, i64+str: insert/swap/reverse/slice edge cases) NOW; `ordered_helpers`
folded into the G5 fixtures.

### G3 ★ — `Dictionary<K: IEq & IHash, V>` + `dict_find_index<K: IEq & IHash>`

**Goal.** Generalise the shipped str-keyed `Map<V>` (`map.tks:30`) to an ARBITRARY key type. Same
three-parallel-arrays representation (`keys`/`hashes`/`vals`), `Map`'s proven shape (a nested `Entry<K,V>`
still fails to resolve on the generic stack, `map.tks:20-25`). The sealed full-Javadoc `Dictionary` +
`dict_find_index` are in the design doc §2.3 — copy verbatim. `dict_find_index` is the generalisation of
`map_find_index` (`map.tks:143`) from `str` to `K: IEq` (the `u64` hash rejects a non-match before an
`__eq` call; an equal hash falls through to `keys[i] == k` — the generic `==` dispatch, 9-ops crumb 4).

**Gate:** 9-ops (generic `==` over `K: IEq`, crumb 4) + `IHash` (G1). **Existing fns touched:** none — new
`src/collections/dictionary.tks`; reuses `arr_replace_at`, `teko::list::push`. `Map<V>` UNCHANGED.
**Gate (`.tkt`, ★, two-instantiation):** `dictionary_test.tkt` — `Dictionary<StrKey,i64>` matches
`Map<i64>` behaviour (round-trip, update-not-grow, collision distinctness, remove present/absent) + a
second instance `<StrKey,str>`.

### G4 ★ — `HashSet<T: IEq & IHash>`

**Goal.** The unordered O(1)-membership set: parallel `hashes`/`items`, the `dict_find_index` molde
without a value array. Not fully spelled in the doc; the shape:

```teko
/**
 * HashSet<T: IEq & IHash> — a growable, reference-semantic hash set. Parallel owned `hashes`/`items`
 * arrays re-grown per mutation (the `Dictionary` representation minus `vals`); `IHash.hash()` buckets,
 * `IEq.__eq` disambiguates. `add` is idempotent (a present element is a no-op). Reference semantics.
 *
 * GATE: 9-ops (generic `==` over `T: IEq`, crumb 4) + `IHash` (G1).
 * @since 0.3.1
 */
pub type HashSet<T: IEq & IHash> = class {
    /** Each element's cached `hash()`, parallel to `items` — compared before the full element. */
    intern hashes: []u64
    /** The elements, insertion order, parallel to `hashes`. */
    intern items: []T

    /** Build an empty `HashSet<T>`. @return a fresh empty set at the caller's concrete `T` */
    pub static fn make(): HashSet<T> { .{ hashes = teko::list::empty(); items = teko::list::empty() } }

    /**
     * Add `x` if absent (idempotent); reference semantics.
     * @param x the element to add
     * @return `true` iff `x` was newly inserted (was absent)
     */
    pub fn add(x: T): bool {
        var h = x.hash()
        if dict_find_index<T>(self.items, self.hashes, h, x) < self.items.len { return false }
        self.hashes = teko::list::push(self.hashes, h)
        self.items = teko::list::push(self.items, x)
        true
    }

    /** Whether `x` is a member. @param x the element @return true iff present */
    pub fn contains(x: T): bool { dict_find_index<T>(self.items, self.hashes, x.hash(), x) < self.items.len }
}
```

(Reuses `dict_find_index<K>` from G3 — the same free fn keyed on `IEq & IHash`.) **Gate (`.tkt`, ★):**
`hashset_test.tkt` — add/contains/remove; duplicate add = no-op (`false`); two instances
(`<StrKey>` + a prim-adapter instance).

### G5 ★ — `SortedSet<T: IOrd>`, `PriorityQueue<T: IOrd>`, `SortedDictionary<K: IOrd, V>`

**Goal.** The ordered family over `<` (9-ops IOrd). `SortedSet`/`PriorityQueue` sealed full-Javadoc in the
design doc §2.3 (binary-search insert / binary heap; `add`/`enqueue`/`dequeue` over `sorted_insert`/
`heap_*` from G2 Tier B). `SortedDictionary` not fully spelled; the shape:

```teko
/**
 * SortedDictionary<K: IOrd, V> — a growable map kept in ascending `K` order (parallel `keys`/`vals`),
 * so iteration is sorted and lookup is O(log n) binary search over `<` (no hashing — the ordered dual of
 * `Dictionary`, which needs `IEq & IHash`). Reference-semantic class.
 *
 * GATE: 9-ops generic `<` over `K: IOrd` (crumb 4).
 * @since 0.3.1
 */
pub type SortedDictionary<K: IOrd, V> = class {
    /** The keys, strictly ascending by `<` — binary-searchable; parallel to `vals`. */
    intern keys: []K
    /** The values, parallel to `keys`. */
    intern vals: []V
    /** Build an empty `SortedDictionary<K, V>`. @return a fresh empty ordered map */
    pub static fn make(): SortedDictionary<K, V> { .{ keys = teko::list::empty(); vals = teko::list::empty() } }
    /**
     * Insert or update `v` at `k`'s sorted position (binary search over `<`). Reference semantics.
     * @param k the key @param v the value
     */
    pub fn insert(k: K, v: V) { /* binary-search k; arr_replace_at on hit, else arr_insert_at both arrays */ }
    /** The value at `k`, or null when absent (disjoint-domain null path). @param k the key @return value or null */
    pub fn get(k: K): V | null { /* binary search over `<`; null when not found */ }
}
```

**Gate:** 9-ops IOrd (crumb 4) + G2 Tier B helpers. **Existing fns touched:** none — new
`src/collections/{sorted_set,priority_queue,sorted_dictionary}.tks`. **Gate (`.tkt`, ★, two-instantiation
each):** `sortedset_test.tkt` (out-of-order inserts emerge sorted; duplicate `add` = `false`),
`priorityqueue_test.tkt` (`dequeue` returns the minimum in ascending order), `sorteddict_test.tkt`.

### G6 — the ADDITIVE `Map<V>` → `Dictionary<StrKey, V>` migration (Map STAYS)

**Goal.** `Dictionary<StrKey, V>` is the generic successor; `Map<V>` REMAINS as the documented str
shortcut. **No removal** — `teko::env = Map<str, str?>` and the `.tkt` suites depend on `Map`; removing it
is a regression. The migration is purely additive documentation + an optional bridge:

```teko
/**
 * to_dictionary — a `Dictionary<StrKey, V>` holding this map's current entries — the additive door from
 * the str-keyed `Map<V>` to the generic successor. `Map<V>` is NOT deprecated (teko::env depends on it);
 * this is a convenience for code migrating to the generic form. Copies (snapshot); later `Map` mutations
 * do not change the returned dictionary.
 *
 * @return a `Dictionary<StrKey, V>` with each of this map's `(key, value)` pairs
 * @since 0.3.1
 */
pub fn to_dictionary(): Dictionary<StrKey, V> { /* loop self.keys(): dict.insert(StrKey{key}, get(k)) */ }
```

**Existing fns touched:** `Map<V>` gains ONE additive method (`to_dictionary`) — no field/behaviour change,
byte-identical for every existing `Map` use (the method is only stamped when called). **Gate (`.tkt`):**
`map_to_dictionary_test.tkt` — `Map<i64>` → `Dictionary<StrKey,i64>` preserves every entry; the original
`Map` still passes its existing suite unchanged.

### REJECT fixture — the gate proves it holds

`examples/regressions/diagnostics/dict_key_no_ihash/` — `Dictionary<Plain, V>` where `Plain` conforms
`IEq` but NOT `IHash` → `Then diagnostic` "constraint `IHash` not satisfied" (proves the key gate is a
real constraint, not documentation).

---

## 3. Fixtures summary (input → native exit / diagnostic)

Two-instantiation `.tkt` per type (the `list_test.tkt`/`map_test.tkt` pattern), plus project `.tkr` where
end-to-end exit is clearer. Each `exit`/token encodes WHICH branch ran (axis-law).

| # | crumb | fixture | proves |
|---|---|---|---|
| 1 | G1 | `ihash_strkey_test.tkt` | hash-eq consistency (equal keys → equal hash + `__eq`); distinct differ |
| 2 | G2A | `arr_combinators_test.tkt` | insert/swap/reverse/slice, i64+str, edge cases (now, un-gated) |
| 3 | G3 | `dictionary_test.tkt` | `<StrKey,i64>` matches `Map<i64>` + `<StrKey,str>` (two instances) |
| 4 | G4 | `hashset_test.tkt` | add/contains/remove; dup add = no-op; two instances |
| 5 | G5 | `sortedset_test.tkt` / `priorityqueue_test.tkt` / `sorteddict_test.tkt` | ordered insert/min-pop; two instances each |
| 6 | G6 | `map_to_dictionary_test.tkt` | additive bridge; `Map` suite unchanged |
| 7 | gate | `diagnostics/dict_key_no_ihash/` | REJECT — key without `IHash` → constraint diagnostic |

---

## 4. Ritual points (full gate + fixpoint)

- **Pre-req (NOT this doc's ritual):** the **9-ops reseed** must land first (`IEq`/`IOrd` + crumb-4
  generic operator dispatch in the SEED). FASE-1b corpus cannot compile before it (seed discipline, §GATE).
- **FASE-1b ★** — after the 9-ops reseed, the FASE-1b corpus (G1 StrKey, G2 Tier B, G3, G4, G5, G6) enters
  as ONE additive reseed: full C+self-host+native gate + fixtures 1-7 + byte-identity (the corpus is new
  stdlib; the compiler's own corpus instantiates none of it → additive-inert for the compiler fixpoint,
  `bin-a == bin-b` closes trivially). Blast-radius of MACHINERY = zero (no checker/codegen/parser edit).
- **Buildable-early (optional, current seed):** G1's `IHash` interface + G2 Tier A `arr_*` need only #254;
  they may pre-land, but folding them into the FASE-1b reseed keeps one mechanism (recommended).

---

## 5. HALT check — no HALT

All tensions resolve law-first (inherited from the sealed design doc §5, T-1…T-5):
- **`IHash` as a method interface vs D18's structural `Hashable`** — `IHash` dispatches over `T` (via
  #254), the retired structural trait could not (`map.tks:9-18`). Coherent with the post-9-ops direction;
  D18 becomes HISTÓRICO. Report up: mark D18 superseded when `IHash` lands (NOT an issue I open). No HALT.
- **`Map<V>` stays vs removed** — STAYS (additive; `teko::env` depends). No HALT.
- **Seed discipline** — FASE-1b sequenced strictly after the 9-ops reseed. No HALT.
- **`.tkb`/codec** — `Dictionary`/`HashSet`/etc. are ordinary generic classes; `IHash` an ordinary method
  interface; `StrKey`'s `operator __eq` already round-trips since §9. Zero new codec surface. No HALT.

**No genuine law tension → NO HALT.**

---

## 6. Points flagged for owner / integrator (not invented, not actioned)

1. **Host module for `IEq`/`IOrd`/`IHash` (coordination with 9-ops crumb 6).** `src/cmp/` does NOT exist
   (its `cmp.tks` was dropped in `07588731`, "its interface operators outrun the seed"); `src/core.tks`
   EXISTS. The three capabilities MUST cohabit (a `K: IEq & IHash` constraint resolves both from one
   namespace). The exact path is **9-ops crumb 6's to pin** (it ships `IEq`/`IOrd`); `IHash` follows into
   the same module. Recommend confirming the module (re-created `src/cmp/` post-9-ops, or `src/core.tks`)
   once 9-ops crumb 6 lands — a coordination dependency, not a design open. This is where the design doc
   (§2.2) says "align with where 9-ops puts IEq/IOrd".
2. **`StrKey` wrapper vs direct-`str` key capability (design doc §2.2 / 9-ops T-1).** A `struct { key: str }
   IEq & IHash` conformer works (9-ops §9.4: a struct may implement an interface). Whether a PRIMITIVE
   `str` could carry the capability DIRECTLY (no wrapper) depends on 9-ops permitting capability on a
   value-type prim-backed type — DEFERRED there. Recommend shipping the `StrKey` wrapper (safe, proven);
   the direct-`str` form is an additive follow-on IF 9-ops T-1 authorises it. Not a HALT.
3. **Adjacent (reported, NOT built here):** `sort<T: IOrd>` generic (unblocked by 9-ops, `sort.tks:81/157`
   are concrete today); `LinkedList<T>` by-pointer (the self-referential node still stamps wrong on the
   generic stack, `map.tks:20-25` — the free-list form is the safe shape); `PriorityQueue<E, P: IOrd>`
   with a separate priority key.

*Grounding: every `file:line` re-verified on `origin/fix/retirement` (§1). Dependencies contracted against
the DECLARED shapes: `plano-9ops-…-0.3.1.md` (IEq/IOrd :341-357, crumb 4/6), #254 (DONE). Design source:
`plano-collections-genericas-e-concorrentes-0.3.1.md` §1-§2 (SEALED — the full-Javadoc snippets this
sequences). Concurrent §3 is out of scope (design/collections-optionB).*
