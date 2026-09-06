# tklib Partition Plan Recheck — Scout Pass (2026-08-20)

> **Purpose:** Verify the blind-spot identified by the owner: the previous classification of 5 stdlib modules as "compiler-NOT-used" ignored **planned use in design docs**. Specifically, the owner remembered that **arena IA usar collections**. This recon determines which modules are safe to extract vs. which must be retained due to planned compiler usage.

> **Scope:** Read-only verification (scout pass) of: `sort`, `iter`, `collections`, `cmp`, `threads`.  
> **Sources:** DECISION_LOG.md (D45–D57.1), docs/design/** (arena, memory, COL-F0, concurrency, package architecture), .crumbs/** (0021–0024 COL-F0, execution order, S10), grepped src/ for current refs.

---

## Findings: Current & Planned Use

| Module | Current Refs in src/ | Planned Compiler Use? | Status | Citation |
|---|---|---|---|---|
| **collections** | **2995** (`teko::list::`, `teko::map::`) | **YES — core monolith** | **RETER** | D55:165-172, D56:173-178, tk-data-arch §4 L112 "compiler consumes them"; EXECUTION-ORDER lines 104–105 |
| **cmp** | 0 | No | SEGURO EXTRAIR | Used only in stdlib (Map→StrKey); no refs in checker/codegen/parser/backend |
| **sort** | 0 | No | SEGURO EXTRAIR | stdlib only; no refs in compiler machinery |
| **iter** | 0 | No | SEGURO EXTRAIR | stdlib only; split_lines in process_test.tkt (fixture, not compiler); no machinery refs |
| **threads** | 0 | No — **surface-only** | SEGURO EXTRAIR | S10-SURF (crumb 0115) line 34: "compiler NEVER instantiates §10 surface"; teko::threads stdlib is for user programs, not compiler runtime |

---

## Arena + Collections: The Blind-Spot Resolved

**Owner's memory:** "arena iria usar collections."

**Verdict:** TRUE and CONFIRMED. Three-part binding:

1. **Arena itself** (`src/runtime/arena.tks`) — raw intrusive structures via mmap; does NOT import collections.
2. **COL-F0 + COL-Q bases** (checker/codegen intrinsics + `src/collections/bases/`) — the **primitives** arena and collections both rest on. Monolith.
3. **`teko::collections` (List, Map, Dictionary, Set, HashSet, Queue, etc.)** — **the compiler uses directly** (2995 refs):
   - `Env` in scope.tks holds `base_slots: []ValBinding`, `bindings: []ValBinding` (List-backed).
   - `scope.tks:23` calls `teko::list::push`.
   - Environment, bindings, and symbol tables are List/Map backed throughout checker/codegen/lir/backend.
   - Self-build uses these at compile-time; cannot extract.

**Citation chain:**
- `tk-data-package-architecture-0.3.x.md` line 112: "List, Map, Dictionary, ... **monolith** `teko::collections` | **the compiler consumes them (self-build is their test); cannot be an external package for internal use**"
- `tk-data-package-architecture-0.3.x.md` line 93: "`src/` uses `List`/`Map`/`Set` directly"
- D55 (DECISION_LOG line 165–172) "monolith = só o que o compilador usa; stdlib exposta"
- D56 (line 173–178) arena mechanism confirmed; `place()` region-explicit now
- EXECUTION-ORDER 0077–0078 (M2, COL-Q9/Q10): "List chunkchain" + "Map/Dict/HashSet" migration — fixpoint rebuild triggered by compiler using them

---

## Recheck: Threads (S10) — Compiler Surface ≠ Compiler Runtime

Potential false alarm on threads: **S10 (spawn/chan/await) is TAUGHT, not EXECUTED by the compiler.**

- **S10-SURF** (crumb 0115, line 34): "the compiler NEVER instantiates the §10 surface"
- **S10-RT** (crumb 0116, M2): thread/channel runtime primitives & `teko::threads` stdlib
- The compiler parses `spawn`, `chan<T>`, `await` and checks ref-guards; it does NOT call `spawn()` or interact with channels internally.
- `teko::threads` stdlib is consumed by **user programs** compiled by teko, not by the compiler binary itself.
- **Conclusion:** threads safe to extract; it is an optional feature library, not a compiler dependency.

---

## Final Lists

### Modules SAFE to Extract (Fase A — move-no-unused)

1. **`sort`** — 0 compiler refs; stdlib leaf only
2. **`iter`** — 0 compiler refs; test fixtures & stdlib adapters only
3. **`cmp`** — 0 compiler refs; used in collections (StrKey) but not in compiler itself
4. **`threads`** — 0 compiler refs; S10 surface is parsed/checked, not executed; stdlib feature for user programs

**Verification:** None of these modules appear in `src/checker/`, `src/codegen/`, `src/parser/`, `src/lir/`, `src/backend/` with direct imports or calls.

### Modules RETAINED in Monolith (Fase B onwards)

- **`collections`** (List, Map, Dictionary, Set, HashSet, Queue, Deque, Stack, LinkedList, PriorityQueue, SortedSet, SortedDictionary, Counter, MultiMap, WeakMap, WeakSet, RingBuffer, Table) — **2995 direct refs in compiler machinery**; core binding in `Env`, symbol tables, scope management, checking state.
  - **Sub-deps:** COL-Q bases (ChunkChain, Ring, Hash, Ordered, Heap, BitSet, weak-ref intrinsics) are already monolith primitives.
  - **Reseed tie:** EXECUTION-ORDER 0077–0078 COL-Q9/Q10 trigger fixpoint rebuild (compiler depends).

---

## Execution Timeline (D57 consequence)

**Fase A (Parallel, move-no-unused now):** Extract `sort`, `iter`, `cmp`, `threads` to `./tklib/` as independent packages. **No test added** (D57.1); move is read-only verification.

**Fase B (Post-memory-green):** Begin COL-F0 memory campaign with **collections RETAINED** in monolith. Arena + COL-Q + collections form the unbreakable triad.

**Fase D (Post-native):** Link-time polymorphism of collections only materializes post-native-fixpoint-green and separate-objects-per-namespace (when the `.tkh` trust model & link-time monomorphization can activate).

---

## Blocked Forks: None

All deliberations found on-file:
- D55 (monolith boundary) → collections stays
- D56 (place region) → no fork
- D57 (faseamento conservador) → collections post-memory, not pre-memory
- S10 (surface vs runtime) → threads is surface, not runtime execution
- CoL-F0/Q (bases) → already decided monolith, zero fork

**Conclusion:** Recon identifies ZERO fork points. All 5 modules classified per existing rulings. The owner's memory of arena using collections is **correct and already-reflected in deliberations D55/D56/D57**, even if not explicitly named in the earlier scout.

