---
seq: 0077
crumb-id: COL-Q9
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [COL-Q1]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:575-633"   # Q9 List → chunk-chain
  - "docs/design/plano-mestre-0.3.1-implementacao.md:237"                # M2 collections row COL-Q9
  - "docs/design/colecoes-remodelagem-backing-fixo-0.3.1.md"             # the remodel (§3.5 presize combinators)
---

# 0077 · COL-Q9 — convert `List<T>` to chunk-chain + TS + three-category (build side)

> Convert `List<T>` → chunk-chain + TS + three-category memory (build side). Re-back `List<T>` on
> `ChunkChain<T>`, replacing the value copy-grow `push` (the v1 leak). The old root removal is FASE 2 (COL-F2).

## Goal

Re-back `List<T>` on the `ChunkChain<T>` base (COL-Q1) with the three-category element model, replacing the
value copy-grow `push` at `src/collections/list.tks:15` (`self.items = teko::list::push(self.items, x)` — the
grow-leak choke point). This is **build-first**: we BUILD the replacement and prove it green here; the REMOVAL
of the old `teko::list::push`/`grow` root is FASE 2 (COL-F2, `0093`). `List` is core-consumed across the
compiler, so the swap rides a `[fixpoint]` rebuild — the compiler is rebuilt on the new body but the language
is UNCHANGED (pure `.tks` over the SM-R1-taught surface): teaches nothing, byte-identity `gen2==gen3` is the
gate. Elements are three-category: value copied once + bucket; class held by pointer, freed by region-drop via
escape-analysis (the list is a holder raising residence — the conservative GATE-1 default); wrapped
retained/released.

## Where

- `src/collections/list.tks:1-15` — rewrite the `List<T>` type body over `ChunkChain<T>` (COL-Q1). The current
  `intern items: []T` + `teko::list::push` growable-array field becomes `intern items: ChunkChain<T>`; every
  method delegates to the chain.
- `src/collections/collections.tks:2-92` — the `arr_*` combinators become presize-exact (remodel §3.5) as a
  sub-step (allocate the exact final `of_len`, fill, no copy-grow). Still CALLED here; their REMOVAL is COL-F2.
- `src/list/list.tks:1-2` (`teko::list::grow`/`push`) — left in place for FASE 2 (build-first; still the old
  root until COL-F2 removes it). NOT edited here beyond ceasing to call it from `List`.

## How

1. **Rewrite the `List<T>` body** over `ChunkChain<T>`. Copy the settled W15 shape verbatim from the source doc
   (`colecoes-memoria-fila-implementacao-0.3.1.md:587-620`). `make()` builds a `ChunkChain<T>::make(64,
   TsMode::CasAppend)`; `push`/`get`/`pop`/`len`/`is_empty` delegate to the chain.
2. **`set(i, x)`** overwrites in place (O(1)); a no-op if `i >= len()`. **`remove_at(i)`** shifts to preserve
   order (O(n)). **`to_array()`** materializes a FRESH `[]T` snapshot — never a view over the chain (a view
   would dangle at a later reclaim; the R9 to_array correction).
3. **Presize the `arr_*` combinators** (`collections.tks:2-92`): each allocates the exact final length via
   `of_len` and fills — no intermediate copy-grow. This is the sub-step that removes `List`'s reliance on the
   growable `push` while KEEPING the old helpers callable until COL-F2.
4. **Three-category reclamation** rides the chain's `push`/`pop`: value = place once + bucket; class = pointer
   stored, region-drop at the list's end (conservative GATE-1 default — leak-safe, never UAF); wrapped =
   retain on push / release on pop.
5. **Build-first:** the old `teko::list::push`/`grow` stay defined (still the root); `List` simply stops calling
   them. COL-F2 removes them once EVERY caller is migrated.

```teko
/**
 * List<T> — a growable, reference-semantic sequence backed by a ChunkChain<T> (TS-by-default). Append +
 * iteration is the dominant use, so the cache-friendly chunk-chain wins; push/pop at the end are amortized
 * O(1). Positional remove/insert in the middle is O(n) (an explicit trade; use LinkedList for middle churn).
 * Elements are three-category: value copied once + bucket; class held by pointer, freed by region-drop via
 * escape-analysis (the list is a holder raising residence); wrapped retained/released. `to_array` is a fresh
 * snapshot, never a view over the chain.
 *
 * @since 0.3.1
 */
exp type List<T> = class {
    /** The chunk-chain backing (growable + thread-safe). */
    intern items: ChunkChain<T>

    /** Build an empty list. */
    pub static fn make(): List<T> { .{ items = ChunkChain<T>::make(64, TsMode::CasAppend) } }
    /** The live element count. */
    pub fn len(): u64 { self.items.len() }
    /** Append `x` (amortized O(1), TS by default). */
    pub fn push(x: T) { self.items.push(x) }
    /** Read the element at `i` (value copy / object reference). */
    pub fn get(i: u64): T { self.items.get(i) }
    /** Overwrite the element at `i` (O(1)); a no-op if `i >= len()`. */
    pub fn set(i: u64, x: T)
    /** Remove the last element (O(1)); value bucket / class region-drop / wrapped release. */
    pub fn pop() { self.items.pop() }
    /** Remove the element at `i`, preserving order (O(n) shift). */
    pub fn remove_at(i: u64)
    /** A fresh `[]T` snapshot of the live elements (never a view over the chain). */
    pub fn to_array(): []T
}
```

## Rulings & laws

- **Teko-only:** `.tks` only; the old `teko::list::push`/`grow` C-frozen? No — they are `.tks` (`src/list/`),
  removed cleanly at COL-F2. No C twin touched.
- **W15 full Javadoc** on the rewritten type + every member; flatten; no inline `//`.
- **Build-first (owner):** BUILD + prove the replacement green BEFORE FASE 2 removes the old root — the old
  `push`/`grow` stay defined; `List` just stops calling them.
- **Fixed-backing (F1):** growth LINKS a fixed chunk (ChunkChain), NEVER a whole-backing swap — the v1 grow-leak
  is closed by construction; the banned growable-array-by-swap does not reappear.
- **GATE-1 (class lifetime):** the conservative region-drop-via-escape holder default ships now (leak-safe,
  never UAF); promote-to-wrapped is an additive `[dry]` follow-up when GATE-1 closes — NOT this crumb.
- **Teaches nothing:** pure `.tks` over the SM-R1 surface; the `[fixpoint]` rebuild advances the blessed binary
  only to carry the new body, no new seed language.
- **Safety:** NEVER `teko test .`; build gen2 in a subshell with `ulimit -v 6815744` (a chunk-chain monomorph
  that inflates the build is a root-cause fix — chunk_cap / Doc-1 presizing — NEVER a raised ceiling); FIXPOINT
  `gen2==gen3` byte-identical; sweep `.tkt`/`.tkr` after the signature change; commit the green step.

## Fixtures

`List` is core-consumed → the self-build exercises push/get/pop/to_array the moment the compiler's own `List`
builds on the new body (happy path is fixpoint-covered). Write ONLY the non-self-build path (the GATE-1 class
boundary the compiler may not reach); concurrency is already covered by `chunkchain_*` in COL-Q1 — no duplicate.

| fixture | asserts | expected |
|---|---|---|
| `list_class_region_drop` | a `class` held ONLY by the list, removed early: freed by region-drop at the list's drop (conservative GATE-1 default); no premature free, no leak | `0` |

## Gate

`[fixpoint]` — build gen2 (`TEKO_BACKEND=native`), `list_class_region_drop` green, FIXPOINT `gen2==gen3`
byte-identical. "Green" = the compiler rebuilds on the chunk-chain-backed `List`, the class-region-drop boundary
holds, and gen2==gen3. Teaches nothing (pure `.tks`). **Reseed-class: fixpoint-rebuild.**

## Deps

`COL-Q1` (the ChunkChain base + `of_len`, seeded at SM-R1).

## Done when

`List<T>` is backed by `ChunkChain<T>` with the three-category element model, the copy-grow `push` is no longer
CALLED from `List`, `list_class_region_drop` passes, and the compiler self-build fixpoint `gen2==gen3` holds.
