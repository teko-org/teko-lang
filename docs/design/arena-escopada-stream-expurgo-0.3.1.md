---
section: design
created: 2026-08-19
status: DESIGN — no product line. The three-part 0.3.1 SCOPED-MEMORY correction
        (owner spec 2026-08-19): (#1) scoped arena by FINE scope, (#2) native stream at all io
        points with buffers reclaimed by the scope arena, (#3) total purge of
        ::push/::empty/grow_inplace/with_cap. Extends the canonical arena spec; supersedes it only
        where the owner's new fine-scope + purge decree diverges (§1).
        REMOUNT 2026-08-19b: §9's five open forks (F1–F5) are RULED by the owner and propagated
        through the body. F1 — arrays are FIXED-SIZE; growth = allocate a NEW fixed array and DROP
        the old (the standing NO-PUSHES law + the four conversion natures); NO push/grow_inplace/
        with_cap AND no growable method survives; Tier B surface removal HAPPENS this round. F2 —
        the object arena is a DEDICATED per-object region (not the shared enclosing arena). F3 —
        the push re-point stays native-first (C1 after A3). F4 — KILL both #arena_depth (#476) and
        #arena_size (the profiler presize); the compiler measures the slot size STATICALLY (the
        AST floor) as a minimum materialization support floor. F5 — the `.h`→`.tks` FFI migration
        is IN SCOPE (link §16). Arena-model invariant reaffirmed (owner 2026-08-10, restated): the
        arena is DYNAMIC with a static FLOOR (lower bound), NO ceiling — it grows past the floor by
        chunk-list (never copying bytes); F1 (fixed arrays) is the SLICE layer, the arena is the
        REGION layer, and killing the sizing pragmas does NOT make the arena static (§2.5).
source: owner spec 2026-08-19 (scoped-memory correction) reconciled with
        docs/design/arena-especificacao-unica-0.3.1.md (source of truth), io-streaming-0.3.1.md
        (io decree 2026-08-19), modelo-de-memoria-por-escopo-0.3.1.md, arena-por-escopo-0.3.1.md,
        backend-memoria-por-funcao-0.3.1.md, lang-evolution-0.3.1-memory-and-surface.md (S0/S1/S2).
frozen: bootstrap/teko.c + the C checker/codegen/build twins are OUTPUT/FROZEN; the only editable C
        is src/runtime/teko_rt.{c,h} (runtime exception). New product work is `.tks` only. All work
        drains DIRECT into fix/retirement (no PR) — owner ruling 2026-08-15.
---

# Scoped arena, native stream, and the growable-primitive purge (0.3.1)

Architect, 2026-08-19. Base: `origin/fix/retirement` @ `9eba3d75` (the commit that created this doc;
REMOUNT after the owner ruled §9's five forks). DESIGN document — no product line. This is the owner's
three-part SCOPED-MEMORY correction, sequenced #1 (foundation) → #2 (stream buffers hang off #1) → #3
(purge, because the arena reclaims the transient of the removed growable primitives). §9's F1–F5 are now
RULED and propagated through the body (see §9 for the verbatim rulings; §0 for the reconciliation).

---

## 0. Reconciliation — what already exists (do NOT reinvent)

This correction does not start from zero. It EXTENDS the canonical model and SUPERSEDES it only at the
two points the owner's new decree moves. Cited, with the divergence made explicit.

| existing doc | what it owns | this doc's relation |
|---|---|---|
| `docs/design/arena-especificacao-unica-0.3.1.md` (owner ruling 2026-08-10) | THE source of truth: region mechanics (§1), the region tree lifecycle (§2), the AST **floor/piso** (§3), **elision** `need==0` (§4), **DPS** move-on-return (§5), the **four escape boundaries** (§6), concurrency/F1/F2 (§7), DI-as-arena (§8) | **EXTENDS.** #1 here is the *fine-scope* refinement of its §2/§4/§6 the owner now decrees; #3 here removes the growable primitives its §1.2 named (`grow_inplace`/`with_cap`) AND `push`/`empty` AND any growable method (owner F1). Its §3 **couples** the static floor to the profiler's `#arena_size` for the dynamic-need case (`arena-especificacao-unica-0.3.1.md:147-149`) — owner F4 **SEVERS** that coupling: the floor stays static, the region stays DYNAMIC by chunk-list, the profiler refinement is killed (§2.5). Its §7.8 F2 per-entry free-list stays (for the immortal-F2 chan/journal case) and COEXISTS with F2's dedicated per-object regions (§2.4). Where the two disagree on `grow_inplace`/the sizing pragmas, **THIS wins** (§1, §2.5, §5). |
| `docs/design/modelo-de-memoria-por-escopo-0.3.1.md` (2026-08-02) | the per-scope LANGUAGE model: the 5 lexical scopes, `ResidencePlan`, the `residence_plan` oracle, `region_enter`/`region_leave` | **REUSES verbatim.** The `ResidencePlan`/`residence_plan` oracle and `region_enter`/`leave` primitive are the plumbing #1 lowers onto. #1 adds a 6th scope (the OBJECT arena) and tightens the reclaim rule to the depth check (§3 here). |
| `docs/design/io-streaming-0.3.1.md` (owner decree 2026-08-19) | the native-stream io SURFACE (`FileStream`, `open_*`, `stream_read/write/seek/close`, `write_stream`/`read_stream`, `byte_ptr`), the syscall-per-OS map, the io-site migration census | **REUSES verbatim and BINDS to #1.** #2 here is NOT a second io surface — it is that surface with its 1024 B scratch buffer bound to the enclosing scope arena of #1 (§7 here). It also names its own §9 co-dependency ("closes together with the arena") — this doc is that arena. |
| `docs/design/arena-por-escopo-0.3.1.md` (2026-08-07, M0–M3) | the compiler CROSS-PHASE retention axis (`clone_tprogram`, front-region drop, `tk_str_concat_len_r`) | **ORTHOGONAL, complementary.** That axis frees the frontend overhang BETWEEN phases; #1 here frees dead scratch WITHIN each fine scope. Both compose; neither touches the other's hot file at its core. `tk_str_concat_len_r` (M2 there) is reused by #2 here for stream-adjacent str. |
| `docs/design/backend-memoria-por-funcao-0.3.1.md` | the compiler-scratch per-function region + the shared `tk_region_enter`/`leave` primitive | **SHARES the primitive.** One runtime `enter`/`leave`, two consumers (compiler scratch there; program-generated scope arenas here). Coordinate, do not duplicate. |
| `docs/design/lang-evolution-0.3.1-memory-and-surface.md` §5 (S0/S1/S2) | the allocation-seam evolution vocabulary | **THE seam this ties into** (§2 here). |

**The divergences to state loudly (all now RULED, §9):**

1. **`grow_inplace` retired (was the canonical §1.2/§6 AL3 cure).** `arena-especificacao-unica` §1.2/§6
   kept `tk_slice_grow_inplace` (AL3/Model A) as the self-append cure. The owner retires it entirely (#3,
   owner F1): #1 makes abandoning FREE — the abandoned copy-grow halves die at arm exit in the scope
   arena — so the in-place mutation primitive (3-word `{ptr,len,cap}` header the native backend cannot
   represent, `lower.tks:4339`, `typer.tks:835`) has no reason to exist. The self-append boundary
   canonical §6 assigned to AL3 is reassigned to the scope arena (#1).

2. **`push`/`empty`/`with_cap` and every growable METHOD retired (owner F1, this round).** Arrays are
   FIXED-SIZE. There is no in-place append and no growable method; to "grow" you allocate a NEW fixed
   array (sized exactly at its own allocation) and DROP the old — the standing NO-PUSHES law and its four
   conversion natures (map = presize-to-source + index-assign; parse/scan = two-pass count-then-fill;
   filter = widen-to-upper-bound + cut; output-buffer = literals + interpolation). Tier B surface removal
   HAPPENS this round (§5.3), not as a fast-follow. `of_len<T>(n): []T` (zero-fill) + index-assign + the
   `count`/watermark idiom is the replacement, NOT a re-sized slice primitive.

3. **The arena stays DYNAMIC; only the sizing pragmas die (owner F4).** `arena-especificacao-unica` §3
   couples the static floor to the profiler's `#arena_size` presize for dynamic need
   (`arena-especificacao-unica-0.3.1.md:147-149`). The owner KILLS `#arena_size` (`codegen.tks` presize,
   `cg_emit_arena_presize` at `codegen.tks:6934`; canonical §3 cites the drifted `:9832`) and `#arena_depth`
   (#476). The floor remains the static AST lower bound; dynamic need is served by the region growing past
   the floor via chunk-list (`arena-especificacao-unica-0.3.1.md:64-72`, never copying bytes) — the arena
   is NOT made static (§2.5).

4. **The object arena is DEDICATED per-object (owner F2).** A shared/enclosing arena is rejected as
   breaking object visibility+security; every object gets its own region (§2.3/§2.4).

5. **The `.h`→`.tks` FFI migration is IN SCOPE (owner F5).** Every C `.h` include becomes `.tks` over the
   native ABI or syscall, without gcc — tied into §16 (`plano-s16-expurgo-libc-completo.md`) and the
   runtime migration (`migracao-runtime-c-para-teko-0.3.1.md`), §5A.

Everything else in the canonical spec stands.

---

## 1. The S0/S1/S2 seam this rides on (the allocation flow today)

The evolution names three allocation seams (`teko_rt.h:144-188`, `lang-evolution §5`):

- **S0 — the COMPILER-internal seam** in `core.h`: static-inline `tk_alloc`/`tk_realloc0`/`tk_free0`/
  `tk_alloc_copy`, internal linkage, over libc. Every allocation the compiler-as-a-C-program makes
  flows through this one point (`teko_rt.h:154-157`). It stays on libc until the list migration threads
  old-size through the header — out of this doc's scope; noted only as the frozen floor.
- **S1 — the RUNTIME seam** `teko_rt.c::tk_alloc` (`teko_rt.h:144-158`): now bump-allocates from the
  process ROOT region instead of `malloc`. Behavior-preserving because root is never dropped =
  today's leak (M.5). **This is the seam #1 re-points**: the default allocation target moves from
  "root, fixed" to "the CURRENT region" (`tk_region_current()`, `teko_rt.h:371-375`) — which under #1
  is the enclosing fine-scope arena, not root.
- **S2 — the REGION model** (`teko_rt.h:175-188`, `362-375`): `tk_region_new(parent)` /
  `tk_region_enter(child)` / `tk_region_leave()` / `tk_region_drop(child)` (bulk-free, O(1)), the
  per-task current-region stack, and the region-aware slice primitives (`tk_slice_push_r`, etc.).
  **#1 is the S2 keystone applied at FINE granularity.**

The flow #1 installs: **every default allocation lands in `tk_region_current()`; each fine scope makes
that current region its own child on entry and drops it on the arm's exit edge.** S0 is untouched
(frozen); S1's default target becomes current-region; S2's tree gains one child per executed fine scope.

---

## 2. Part #1 — the scoped arena, by FINE scope

### 2.1 The scope taxonomy — exactly which scopes get an arena

The canonical spec (`modelo-de-memoria-por-escopo §4`) enumerates FIVE lexical scopes. The owner's
2026-08-19 correction refines this to the FINE-scope taxonomy: not "the whole `if`" but **each arm**
separately, plus the object arena. The exact set that materializes an arena:

| # | fine scope | arena birth | arena death (arm exit edge) | notes |
|---|---|---|---|---|
| 1 | **bare block** `{ … }` | on entry, if `need>0` | on the block's exit edge | the base case |
| 2 | **loop arm** (body of `loop`/`loop while`/`for`) | on entry of EACH iteration | on EACH iteration's exit edge (back-edge) | per-iteration reclaim; NEVER flattened across the back-edge (`modelo §7` carve-out) |
| 3 | **`if` then-arm** | on arm entry, if `need>0` | on the then-arm exit edge | each arm is its OWN scope (the refinement) |
| 4 | **`elseif` arm** | on arm entry, if `need>0` | on the elseif-arm exit edge | distinct from the `if` arm |
| 5 | **`else` arm** | on arm entry, if `need>0` | on the else-arm exit edge | distinct arm |
| 6 | **`match`/`when` arm** | on arm entry, if `need>0` | on the matched arm's exit edge | one arena per arm, not per match |
| 7 | **object arena** (per instance) | when the instance is constructed | when the instance dies (its owner scope's exit edge, transitively) | the NON-lexical scope; §2.4 |
| — | **fn body** (frame) | on call entry | on the return edge | the canonical frame region; DPS (§5 canonical) routes the RETURN value up, not this |

**The elision rule is unchanged and governs 1–6:** a fine scope with `need==0` (no routable allocation
site inside it — `scope_touches_arena` false, `arena-especificacao-unica §4`) opens NO arena; the
`if x { return a }` leaf, a bare comparison arm, a guard block cost nothing. **Doubt → do not elide**
(conservative, leak-safe, never UAF). This is the same skip the native backend already proves with
`bracket_depth > 0` (`lower.tks:1637`).

**The floor governs the SIZE (1–7):** a scope that does allocate is born sized to its proven need +
header via `tk_region_new_sized_u(parent, need + header)` (`arena-especificacao-unica §3`), not the
64 KiB default. Elision is the `need==0` limit of the floor.

### 2.2 The arena lifecycle per fine scope

Uniform across 1–6 (the loop arm's "exit edge" is per-iteration; everything else once):

```
// on the arm's ENTER edge, when need>0:
child = region_new_sized(region_current(), floor(scope) + header)   // sized by the AST floor
region_enter(child)                                                 // tk_alloc now bumps into `child`
// ... arm body: every default alloc, every dropped-old fixed array (new+drop growth), every str concat lands in `child` ...
// on the arm's EXIT edge:
region_leave()                                                      // current returns to the parent arena
region_drop(child)                                                  // bulk-free ALL of the arm's dead scratch — O(1)
```

- The `region_enter`/`region_leave`/`region_drop`/`region_new_sized` primitives already exist
  (`teko_rt.h:362-375`, `arena-especificacao-unica §2.1`, `modelo §14`). #1 adds NO runtime primitive;
  it emits the enter/leave/drop pair at each fine-scope boundary in the two lowerings.
- **What lands in the arm arena (reclaimed):** every local born-and-dead within the arm — the scratch
  of §0's dominant term: the dropped-old fixed arrays of new+drop growth (the transient previous version
  a variable held before reassignment), the concatenated `str` (`tk_str_concat_len_r(child)`), the boxed
  aggregate elements, the interpolation buffers, the io scratch buffer (#2). ALL of it dies at the arm's
  exit edge instead of leaking to root.
- **What does NOT land here (escapes → stays):** anything the escape check (§3) proves is referenced
  from a SHALLOWER scope. It is routed to that shallower region at birth (the LUB), never reclaimed
  here. The return value goes up by DPS (canonical §5), untouched by the arm drop.

### 2.3 The object arena (scope #7) — a DEDICATED per-object region (owner F2, RULED)

The owner's list adds "PLUS an object arena." An object (struct/class instance) that owns interior
allocations — its `[]T`/`str` payloads, boxed fields — needs those interiors to live exactly as long as
the object, no longer. **Owner F2 ruling:** *"pq compartilhada? Isso quebra a visibilidade e segurança
de um objeto por definição."* The object arena is therefore a **DEDICATED per-object region** — NOT the
shared enclosing fine-scope arena. Sharing an arm's arena would commingle the object's interiors with
unrelated arm scratch, breaking the object's visibility and security boundary by definition. Each object
gets its OWN region, even a stack/scope-local one:

- **Birth:** when the instance is constructed, a dedicated child region is opened (nested in the region
  current at construction) into which the instance's owned interior allocations bump. It is the object's
  private span — no other binding's scratch lands in it. It is born sized to the object's own AST floor
  (§2.5), so a dedicated region is NOT a 64 KiB tax: it is exactly the object's proven interior need +
  header, growing past it by chunk-list only if the object's interiors do (dynamic, no ceiling).
- **Death:** the dedicated region drops when the object dies — the exit edge of whatever scope the
  object's residence resolves to (the LUB of the object's uses, §3). Reclaiming the object reclaims its
  interiors in one O(1) bulk drop; the object's private region is the unit of reclaim.
- **Why it is a fine scope, not a special case:** it obeys the exact same birth/floor/escape/drop
  discipline as 1–6 (§2.5); its "arm exit edge" is the object's death instead of a lexical `}`. The one
  difference from 1–6 is that it is DEDICATED (one region per object instance) rather than one region per
  lexical arm — that is precisely what preserves the object's isolation.

**Boundary with DPS (canonical §5):** DPS decides WHERE a returned aggregate is BORN. Under F2 the
returned object is born in ITS OWN dedicated region, allocated by the caller (the DPS destination is the
object's region handle, sized by the object's floor), nested in the caller's current region. DPS places
the object's region; the object arena reclaims it at the object's death. No conflict: the returned
aggregate is the object, and the object owns its region.

### 2.4 Dedicated per-object regions COEXIST with the §7.8 free-list (no conflict)

F2 makes the object arena dedicated. This must be reconciled with the canonical **§7.8 per-entry
free-list** (`arena-especificacao-unica-0.3.1.md:542-548`), which gives the immortal F2 program region a
free-list/slab so `chan`/journal service entries can be reclaimed INDIVIDUALLY. The two are orthogonal
mechanisms for two different lifetime classes, and neither subsumes the other:

- **A dedicated per-object region** is a bulk-droppable unit: the object dies as a whole, so its region
  is reclaimed with one O(1) `region_drop`. No free-list is needed — the object IS the reclaim granule.
  This is the RULE for ordinary objects (owner F2).
- **The §7.8 free-list** exists precisely for entries that CANNOT be bulk-dropped: a `chan`/journal
  service lives in the immortal F2 program region (shared across tasks, never bulk-dropped), so its entry
  must be freed individually when the `ctx` drops (a directed free, not `mem::free`). This is the ONLY
  new arena capability §7.8 requires, and it is untouched by F2.

So: an object's interiors → its dedicated region (bulk drop); an F2-resident chan/journal entry → the
§7.8 free-list (per-entry free). A dedicated per-object region NEVER needs the free-list, because it is
never wedged inside the immortal region — it is its own droppable child. The `is_unique_at`
cross-thread gate (canonical §7) still governs an object that escapes to another task by copy.

### 2.5 The arena floor is STATIC; the arena is DYNAMIC (owner F4, RULED — kill both pragmas)

Owner F4: *"vamos matar #arena_depth e #arena_size, já tem ruling para isso, o compilador agora deve
medir estaticamente o tamanho do slot para quando a arena ser materializada ter um piso de apoio
mínimo."* Both `#arena_depth` (#476) and `#arena_size` (the profiler presize) DIE. The compiler measures
the slot size STATICALLY (the AST floor, `arena-especificacao-unica §3`) so that when a region
materializes it has a **minimum support floor**. State the model precisely, because it is easy to
misread:

**The arena is DYNAMIC with a static FLOOR (a lower bound), and NO ceiling.** (owner 2026-08-10,
restated). A region is a chunk LIST (`arena-especificacao-unica-0.3.1.md:62-72`): it bumps within the
head chunk and, when a request does not fit, PREPENDS another chunk — **growing a region never copies its
bytes**, and a chunk never overflows (overflow at the region level is structurally impossible). The
static floor only sets how big the FIRST chunk is born (`tk_region_new_sized_u(parent, need + header)`,
not the 64 KiB default); the region grows past it, O(1) amortized, whenever the arm allocates more than
the proven floor. **Killing `#arena_size` does NOT make the arena static or fixed-size — the arena stays
dynamic; only the profiler-driven presize refinement leaves.**

**The two layers must not be conflated (this is the F1↔F4 boundary):**

| layer | what F-ruling governs it | growth model |
|---|---|---|
| **SLICE / array** (`[]T`, the value) | **F1** — FIXED size; grow = allocate a NEW fixed array + drop old | each array is EXACT-sized at its own allocation (`of_len<T>(n)`); no in-place growth exists |
| **REGION / arena** (the chunk list) | **F4** — static floor (lower bound), dynamic growth by chunk-list | grows past the floor by prepending chunks, never copying bytes, no ceiling |

The static floor targets the REGION's first-chunk demand; the 1.8 GB peak lives at the SLICE layer, not
the region (`arena-especificacao-unica-0.3.1.md:74-86`). F1 kills the slice-layer copy-grow (the halves
are reclaimed at arm exit; §5.1); F4 keeps the region dynamic and only removes the pragma refinement.

**How dynamic need is served WITHOUT the profiler (the coupling §3 canonical severed).** Canonical §3
(`arena-especificacao-unica-0.3.1.md:147-149`) routed the dynamic-need case — "the arm allocates, but the
size is a runtime fact" — through the profiler's `#arena_size` `Confidence::Thin` seed + p99.9 refinement.
F4 removes that path. Dynamic need is served instead by the region simply growing past its static floor
by chunk-list (`:64-72`): the floor is a MINIMUM support, and when the arm allocates more, the region
prepends another chunk. **The profiler was only presize REFINEMENT — a way to reduce the number of chunk
prepends — never a correctness requirement:** region reallocation is O(1) with NO byte copy either way, so
the worst case of dropping `#arena_size` is a few more chained chunks, not a slower or wrong build.
- **No UAF, no correctness regression from killing `#arena_size`.** The floor is a lower bound
  (`arena-especificacao-unica-0.3.1.md:153`): a region can never run out of space — it links another
  chunk — so an under-sized floor never strands or corrupts a write. Over-floor is leak-safe
  (reserved-not-used). A wrong presize could only ever waste a little space or add a prepend; it can never
  cause a use-after-free. FIXPOINT gen2==gen3 is unaffected because chunk sizing does not change emitted
  bytes (`ast-computed-arena-assessment-0.3.1.md:159-166`).

**The ast-computed-arena-assessment warning DISSOLVES (and where a residual sliver remains).**
`ast-computed-arena-assessment-0.3.1.md:23,133-142` warned that the AST floor is *only a PARTIAL lower
bound for DYNAMIC collections* — the AST proves final capacity only for literal-count collections, never
for a dynamic accumulation loop, so it argued the dynamic `#arena_size`/p99.9 path "sizes better." Under
F1+F4 that warning dissolves, for two independent reasons:
1. **A partial lower bound is EXACTLY what a materialization floor is meant to be.** The floor was never
   supposed to predict the final REGION size; it is a minimum support the region grows past by chunk-list.
   "Partial" is not a defect here — it is the definition. The thing the profiler's p99.9 tried to
   pre-buy (fewer chunk prepends) is worthless once we accept that a prepend is O(1) no-copy.
2. **F1 removes the dynamic-collection object the warning was about.** The warning's "dynamic accumulation
   loop" was a slice doubling to an unknown final size. Under F1 there is no such object: a collection
   grows by allocating a NEW fixed array of an EXACT size known at THAT allocation (`old_len + k`, or the
   two-pass counted `n`) and dropping the old. Each individual array is exact-at-alloc; the region merely
   absorbs the transient old copies and reclaims them at the arm exit. There is no slice whose final size
   the floor must guess.

**The residual sliver (stated honestly):** the region's static floor still cannot statically know the
NUMBER of new+drop iterations a runtime-bounded loop performs, so it cannot pre-size the region to hold
all transient old arrays at once. This is a non-issue: (a) it is exactly the "partial lower bound" the
floor is designed to be, absorbed by chunk-list growth; and (b) the transient old arrays are reclaimed at
the arm exit edge (§2.2), so they never accumulate past the arm anyway. The only place an EXACT region
floor is provable is a literal-count or pre-computed-count arm; everywhere else the floor is a seed and
the chunk-list is the truth. That is the intended, sound design — not a gap.

### 3.1 The rule (what is reclaimable vs escaping, per scope)

The evolution already names this check. `concorrencia-adiantada-s8.md:44`: *"S2 scope regions + escape
check — per-fn done, block-arm not — the escape check BY DEPTH COMPARISON exists; the fine granularity
does not."* And `onde-esta-a-memoria-do-compilador.md:158` marks the same: `per-fn checked, block-arm
open`. **#1 is exactly the promotion of that depth-comparison escape check from per-function to
per-fine-scope.** There is ONE check, and it is a depth comparison:

> **The region-depth escape check (the single rule):** assign every fine scope a monotone region
> DEPTH along the current-region stack — the fn frame is depth 0, and each nested fine scope that opens
> an arena is depth+1 (the loop arm, the if-arm, the block, the object arena). An allocation BORN at
> depth `d` is **reclaimable at that scope's exit edge** if, and ONLY if, **every live reference to it
> is at depth ≥ d** (deeper-or-equal — i.e. dies with, or inside, the birth scope). If ANY live use is
> at depth `< d` (a SHALLOWER, longer-lived scope), the allocation **ESCAPES**: it is born instead in
> the shallowest such scope's region (the LUB of its uses), and depth `d`'s drop never touches it.

This is the `pt_join` LUB of `arena-especificacao-unica §1`/`modelo §1` expressed as an integer depth
comparison: the residence region = the shallowest (smallest-depth) scope that dominates all uses.
`spine.tks::pt_join` (`spine.tks:495`) already computes the LUB; the depth is its rank. The check is a
single `min` over use-depths, compared against the birth depth.

### 3.2 Why it is sound (never UAF — the preserved theorem)

`modelo §1` proves it and #1 inherits the proof verbatim: the residence = LUB(uses) DOMINATES every
use by definition of least-upper-bound, so every use occurs within the residence's lifetime; a UAF
requires a use AFTER the residence dies, impossible when the residence dominates all uses. The
depth-comparison is the same theorem: born-at-`d` and dropped-at-`d`'s-exit is safe iff no use outlives
depth `d`, i.e. no use at depth `<d`. **Doubt → escape to the shallower region (leak-safe, never UAF).**
This is the M.1 contract (`escape.tks:9-12`): "when the analysis cannot PROVE frame-local, treat as
escaping — a leak is safe, a use-after-free is a vulnerability."

### 3.3 The three concrete escape channels, per fine scope

Mapped onto the canonical four boundaries (`arena-especificacao-unica §6`), specialized to a fine scope:

| channel | example (inside an arm at depth d) | verdict | routing |
|---|---|---|---|
| **arm-local scratch** | `var t = concat(a, b)` used only inside the arm | reclaimable | born in `child`, dropped at arm exit |
| **returned up** (DPS) | `return Point{…}` in a tail arm | escapes to caller | born in caller's current arena (DPS, canonical §5); arm drop skips it |
| **stored into a shallower binding** | `outer = <new fixed array of outer + x>` where `outer` is at depth `<d` | escapes to `outer`'s depth | the NEW array is born in `outer`'s region (the depth-`<d` arena); arm drop skips it; the dropped-old is arm-local |
| **sent cross-thread** | `chan.send(x)` | escapes to program (F2) | born in `tk_region_program()`; `is_unique_at` gate (canonical §7) |

The dominant win (§0's ~1.8 GB copy-grow) is the FIRST row: the dropped-old fixed arrays of a new+drop
growth whose result never leaves the arm are pure arm-local scratch, reclaimed at arm exit. That is why
#3's purge is safe — the peak-inflation the removed growable primitives created is exactly this
reclaimable transient scratch, now reclaimed at the arm exit edge.

### 3.4 What the checker produces (reuse the oracle)

No new analysis engine. `residence_plan(f)` (`modelo §14`, the `ResidencePlan` artifact) is EXTENDED to
emit, per binding and per allocation site, the fine-scope depth of its residence (the min-use-depth),
not just a coarse tier. Both lowerings consume the SAME plan (the invariant that C and native never
disagree, `escape.tks:405`). The extension is: the scope index the plan already carries becomes a
fine-scope index (arm-granular), and the tier gains the depth integer.

```teko
/**
 * ResidenceDepth — the region DEPTH at which a value resides: 0 is the fn frame, and each nested
 * fine scope that opens an arena is one deeper. A value is reclaimable at the exit edge of the
 * scope whose depth EQUALS its residence depth; a residence depth SHALLOWER than the birth scope
 * means the value escapes to that shallower arena (the LUB of its uses). This is the integer form
 * of `spine::pt_join`'s rank — the single region-depth escape check.
 *
 * @since 0.3.1
 */
pub type ResidenceDepth = u32

/**
 * fine_scope_residence — the region-depth escape check at FINE granularity. For each local binding
 * and each routable allocation site in `f`, computes the min-use-depth across the fine-scope tree
 * (arm-granular: each if/elseif/else/match arm, loop arm, bare block, and object arena is its own
 * depth). The site is reclaimable at the exit edge of the scope whose depth equals the returned
 * depth; a returned depth shallower than the birth scope routes the birth to that shallower arena.
 * Uses `spine::pt_join` (the transitive LUB, `spine.tks:495`) as the precise source and `escape.tks`
 * as the conservative fast path. Doubt resolves to the shallower depth (leak-safe, never UAF).
 *
 * @param f  the typed function to analyse
 * @return   the residence plan, each entry carrying its fine-scope residence depth
 * @since 0.3.1
 */
pub fn fine_scope_residence(f: checker::TFunction): checker::ResidencePlan

/**
 * scope_touches_arena — the elision predicate at fine granularity: true iff `body` contains at least
 * one routable allocation site (push/box/struct-init/array-lit/str-concat/tk_alloc). A fine scope for
 * which this is false opens NO arena (the `need==0` limit of the floor). Conservative: doubt returns
 * true (open the arena; leak-safe, never a redirected allocation). Mirrors the existing
 * `bracket_depth > 0` skip (`lower.tks:1637`).
 *
 * @param body  the arm/block body to test
 * @return      true when the scope must open an arena, false when it may be elided
 * @since 0.3.1
 */
pub fn scope_touches_arena(body: []@checker::TStatement()): bool
```

Existing functions touched: `escape.tks` `fn_escaping_vars`/`binding_is_block_local` (feed the plan),
`spine.tks` `pt_join`/`is_unique_at` (LUB + cross-thread gate), `modelo`'s `residence_plan` (extended
to carry depth), the two lowering entry/exit-of-arm emitters (§6).

---

## 4. Part #2 — native stream at ALL io points, buffers reclaimed by #1

### 4.1 The surface is already designed — bind it to the arena

`docs/design/io-streaming-0.3.1.md` (owner decree, same day) fully specifies the native-stream io
surface: `FileStream`, `open_read`/`open_write`/`open_append`, `stream_read`/`stream_write`/
`stream_seek`/`stream_close`, the easy helpers `write_stream`/`append_stream`/`read_stream`, the TOTAL
forms re-based on stream, the per-OS syscall map (Linux `syscallN`, macOS `from "System"`, Windows
`from "kernel32"`), and the single new builtin `teko::mem::byte_ptr`. **#2 does not redesign that
surface — it BINDS each stream's buffer to the enclosing fine-scope arena of #1.** Every io point
becomes a native stream (that doc's decree); this doc makes each stream's buffer die with its scope.

### 4.2 The buffer↔arena binding (the one thing #2 adds over io-streaming)

The io-streaming doc's read loop uses a 1024 B scratch from the arena (`io-streaming §4`):

```teko
var scratch: []byte = teko::mem::bytes_from_ptr(teko::mem::buf_ptr(CHUNK), CHUNK)
```

Today `buf_ptr(CHUNK)` bumps from `tk_region_current()`. Under #1, `tk_region_current()` inside a fine
scope IS that scope's arena — so the scratch buffer is BORN in the arm arena and RECLAIMED at the arm
exit edge, with zero code change to the io surface. The binding is structural: the buffer lifetime =
the scope lifetime, because the buffer allocates through the same current-region seam #1 re-points.

The rules that make this sound (all satisfied by construction):

1. **The scratch is written-in-place, never reassigned** (`io-streaming §9.2`): `stream_read` writes
   into `scratch` and returns a count; the buffer is not grown, so no copy-grow half is abandoned and
   the purge-on-reassign ownership semantics do not fire. The buffer is pure arm-local scratch —
   row 1 of §3.3, reclaimable.
2. **No `region_drop` occurs mid-drain** (`io-streaming §9.2`): the read loop runs entirely within one
   fine scope; the scope's arena is stable for the whole loop and drops only after the loop's exit
   edge. #1's per-arm discipline guarantees this — the drop is emitted at the arm boundary, never
   inside it.
3. **The FileStream handle is a scalar `i64`** (`io-streaming §2.3`): it holds no arena pointer, so a
   stream value that escapes the arm (returned, stored) carries no dangling buffer reference — the
   buffer is arm-local, the handle is a value. The escape of the handle is a scalar move (trivial),
   independent of the buffer's reclaim.

### 4.3 The write path — no root accumulation

`stream_write(ref s, data: ref []byte)` slices `data` by index and syscalls each ≤ CHUNK piece
(`io-streaming §4`); nothing accumulates, so there is no growing output buffer to leak. Where the
compiler builds output (the 22 MB `teko.c`), the end-game is codegen emitting DIRECT into the writer
(`io-streaming §9`) instead of the `csrc` copy-grow `str` — which is precisely a #3 purge target
(`tk_append_bytes_fo`/the `cb` builder). **#2 delivers the sink; #3 removes the accumulator; #1
reclaims whatever transient remains.** The three close together on the codegen tail, exactly as
`io-streaming §9` and `arena-especificacao-unica §1.2` both foretell.

### 4.4 str concat adjacent to streams

A `str` built for an io payload inside an arm (a path, a header line) routes through
`tk_str_concat_len_r(child, …)` (the `_r` twin from `arena-por-escopo` M2, `teko_rt.h`), landing in the
arm arena and dying at arm exit — not the root-leaking `tk_str_concat_len`. This is the §3.3 arm-local
row applied to io-adjacent strings; it removes the 66 MB unroutable-str term for the io path.

---

## 5. Part #3 — total purge of ::push / ::empty / grow_inplace / with_cap

### 5.1 Why they can go once #1+#2 land

The owner: *"these primitives are the source of the peak-inflation (transient over-allocation) the
arena is meant to reclaim, so once #1+#2 land they have no reason to exist."* The mechanism, made
precise:

- `with_cap` existed to PRE-SIZE a fresh slice and skip the 1→2→4→8 doubling ladder. Under owner F1 the
  slice IS fixed-size and sized EXACTLY at its own allocation (`of_len<T>(n)` / `[n]T` zero-fill +
  index-assign) — slice pre-sizing is the fixed-array idiom, so `with_cap` is redundant. (Note the layer:
  this is the SLICE layer, NOT the arena floor — the arena floor sizes the REGION's chunk, §2.5; do not
  conflate them.)
- `grow_inplace` existed to append WITHOUT abandoning (dodge the O(n²) abandoned halves). #1 makes
  abandoning FREE — the halves die at arm exit in the scope arena — so the in-place primitive (which needs
  an F1 exclusive-borrow proof and a 3-word header the native backend cannot represent, `lower.tks:4339`)
  is unnecessary. It is also a banned workaround under standing law (arrays are immutable; growth is
  new+drop only).
- `push` (the root-leaking value form `tk_slice_push`) leaked every grown buffer to root. Under owner F1
  there is no `push` at all: growth is "allocate a new fixed array of exact size, drop the old." The
  transient old array is born in `tk_region_current()` = the arm arena and reclaimed at arm exit; the
  root-leaking primitive is removed, not re-pointed.
- `empty` seeded a zero-cap slice that forced the doubling from empty. Under owner F1 an array is created
  at its known size (`of_len<T>(n)`; the `count`/watermark idiom covers "not yet filled"); `need==0`
  elision (§3) covers an arm that allocates nothing. There is no zero-cap seed to grow from.

### 5.2 The purge table

| primitive | today | arena-backed replacement | call-site reality (latest tree) |
|---|---|---|---|
| **`teko::list::with_cap`** (`typer.tks:824`, `codegen.tks:2531`/`emit_list_with_cap`, `lower.tks:4340`; runtime `tk_slice_with_cap`/`_r` `teko_rt.h:1439-1449`) | pre-size a fresh len-0 buffer | **AST floor** — the scope arena is born at `need+header`; a presized `[]T` written by index needs no explicit cap. Remove the surface fn + the runtime twins. | staged-off; **no real user call sites** (only typer/codegen/lower plumbing) — cheap, mechanical |
| **`teko::list::grow_inplace`** (`typer.tks:835`, `codegen.tks:2562`/`emit_list_grow_inplace`, `lower.tks:4339`; runtime `tk_slice_grow_inplace` `teko_rt.h:1450-1456`) | in-place append under exclusive `ref` | **scope-arena reclaim** — abandon freely; the arm arena reclaims the halves at exit. Remove the surface fn + `tk_slice_grow_inplace` + the F1-borrow requirement it carried. | staged-off; **no real user call sites** (native has no lowering at all) — cheap, mechanical |
| **`teko::list::push`** (checker builtin `typer.tks:808`; runtime `tk_slice_push`/`_push_fo`/`_push_r`) | copy-grow, root-leaking value form | **new-fixed-array + drop-old** (owner F1) — the four conversion natures: map = presize-to-source + index-assign; parse/scan = two-pass count-then-fill; filter = widen-to-upper-bound + cut; output = literals + interpolation. The transient old array dies in the arm arena. Surface REMOVED this round; the C slice-grow machinery is REMOVED (not patched), the compiler ENUMERATES survivors on removal. | **pervasive: ~2698 sites.** Removed this round (§5.3) |
| **`teko::list::empty`** (checker builtin `typer.tks:805`) | zero-cap seed | **`of_len<T>(n)` fixed create** — arrays are created at a known size; the `count`/watermark idiom covers "not yet filled"; `need==0` elision covers alloc-free arms. No zero-cap seed. Surface REMOVED this round. | **pervasive: ~2202 sites.** Removed this round (§5.3) |
| **growable METHOD** — `teko::list::grow<T>(ref x: []T, v)` (`src/list/list.tks:1`, the `ref`-mutable wrapper over `push`) and any `List<T>::push` collection method | in-place-ish append via `ref []T` reassign | **REMOVED entirely** (owner F1: *"não é pra manter o método"*). `ref []T` is a position-pointer only (no grow, no whole-array reassign); a collection grows by building a new fixed backing and returning it (DPS). | leaf choke point + the collections re-spec (§5.3, R9) |
| adjacent: `tk_append_bytes_fo`, `tk_free_block` | the linear-`cb` byte accumulator + explicit park | subsumed by #2 stream-write (no accumulator) + arm-arena drop | codegen `cb` chain; REMOVED with the codegen-emit-direct of #2/§4.3 |

### 5.3 The migration order — total removal this round (owner F1 RULED)

**Owner F1 rules the replacement idiom AND that Tier B happens this round.** There is no fork-gated
deferral: *"nossos arrays DEVEM SER DE TAMANHO FIXO E SEM POSSIBILIDADE DE PUSH / GROW / ou qualquer
artifício de expandir um array. Quer expandir? Cria um novo e dropa o antigo."* The replacement idiom is
the standing NO-PUSHES law's four conversion natures (already ruled surface, not new design):
- **map** (one output per source element) → presize `of_len<T>(source.len)` + `loop i { xs[i] = f(src[i]) }`.
- **parse/scan** (`n` emerges from scanning) → two passes: count `n`, then presize `of_len<T>(n)` + fill by index.
- **filter** (conditional subset) → widen to the upper bound (`source.len`), write the fits into a `count`, cut `slice[0..count]`.
- **output buffer** (the codegen `cb`, the 93 % term) → literals + interpolation (`$"…"`) / literal byte arrays, ZERO growing buffer.

`grow_inplace`/`with_cap` are staged-off (no real user call sites); `push`/`empty`/`grow` are pervasive
(~4900 combined sites). This is NOT a hand-edit-per-site campaign — it is the EXPURGO methodology
(standing law): BUILD the fixed-array machinery first, seed, then remove the roots and let the
self-compiling compiler ENUMERATE the surviving references as raw errors (each error is one site to
convert). Iterate ENSINA → SEED → SWEEP → SEED to a green fixpoint (gen2==gen3).

**Step 1 — build the fixed-array machinery (additive, coexists with the old).** `of_len<T>(n)` zero-fill,
the `[n]T` runtime-sized type syntax, index-assign (`typer.tks type_index_assign`), the `count`/watermark
idiom, the null-deref guard, routed through the arm arena (§2). Seed so gen0 understands the new form.
This is where the peak-inflation is actually cut: growth is now new-fixed-array + drop-old, and the
transient old array dies in `tk_region_current()` = the arm arena at the arm exit edge.

**Step 2 — remove `grow_inplace` + `with_cap` roots.** The typer arms (`typer.tks:824,835`), the codegen
emitters + dispatch, the native honest-stops (`lower.tks:4339,4340`), and the C slice-grow machinery
(`tk_slice_with_cap`/`_r`/`tk_slice_grow_inplace`) — **REMOVED, not patched** (standing law: the expurgo
does NOT go through `teko_rt.c`; the slice-grow machine is dead code deleted). No real caller breaks.

**Step 3 — remove `push`/`empty`/`grow` roots (the surface).** Delete the `push`/`empty` checker builtins
(`typer.tks:808,805`), `teko::list::grow` (`src/list/list.tks:1`, the single leaf choke point — the doc's
earlier `list.tks:15` `List<T>::push` cite is stale; the real wrapper is `grow<T>` at `list.tks:1`), and
the C forms `tk_slice_push`/`_push_fo`/`_push_r`. Seed; the compiler tries to self-compile and ERRS at
every surviving reference — that error list IS the sweep checklist. Convert each to the natures above,
re-seed, repeat.

**Step 4 — re-spec the growable collections (R9, cross-doc).** `plano-collections-genericas-e-concorrentes-0.3.1.md`
and `collections-generics-fase1b-crumbs.md` define growable methods (`List<T>::push`, `Dictionary::insert`'s
internal `teko::list::push` at `:233-235`, `SortedSet`, `PriorityQueue::enqueue` `:333`, `ConcurrentStack::push`
`:616`) built on the now-removed primitives. Under F1 those growable methods must be re-expressed as
new-fixed-backing + return (DPS), not in-place grow. This is a REPORTED cross-doc consequence (R9): the
collections plan needs a follow-up re-spec pass. It does NOT block the compiler-core expurgo (the core
does not depend on the growable-collection surface), and it is not a new issue I open — it is flagged up
for the owner/coordinator to schedule.

Sequence the sweep leaf-first (`src/collections/*`, `src/list/`) then the hot compiler cores (checker
~1615, lir ~877, build ~652, backend ~633, parser ~293, codegen ~163), each behind its own fixpoint gate,
with iterative reseed (the layout of the element does not change — zero-fill, no tag — so the transition
is staged-green: fixpoint gen2==gen3 at each harvest).

---

## 5A. The `.h` → `.tks` FFI migration is IN SCOPE (owner F5, RULED)

Owner F5 overrules the earlier "leave frozen / out-of-scope" recommendation: *"Não é fora de escopo, todo
'.h' feito include no C deve virar código em tks para usar FFI para a ABI nativa ou syscall sem depender
do gcc (já cobre o native)."* Every C `.h` include becomes `.tks` code using FFI for the native ABI or a
raw syscall, WITHOUT depending on gcc. This binds this correction to the §16 libc-expurgo axis; it is NOT
a new axis I invent, it is the tie-in the owner rules.

**The scope this pulls in (and where it lives).** The `.h` includes are two populations
(`plano-s16-expurgo-libc-completo.md:34-48`):
1. **The C EMITTED** (`bootstrap/teko.c` + every generated program): codegen emits `#include <stdint.h>`,
   `<stdbool.h>`, `<stdlib.h>`, `<math.h>`, `<string.h>`, `"assert.h"`
   (`plano-s16-expurgo-libc-completo.md:37-42`). Each becomes: native types of its own / a self-emitted
   fixed-width `typedef` / a Teko intrinsic or raw syscall — never a C header dependency.
2. **The hand-written RUNTIME** (`teko_rt.c` + `teko_rt.h` + `win32_compat.h` + `assert.c`): 28 POSIX/C
   headers plus `win32_compat.h` (`plano-s16-expurgo-libc-completo.md:47-48`). These migrate per the
   camada-2 roadmap (`migracao-runtime-c-para-teko-0.3.1.md`), leg-by-platform: Linux raw-syscall / macOS
   libSystem FFI / Windows kernel32-ntdll FFI (`plano-s16-expurgo-libc-completo.md:54-58`).

**The S0 `core.h` list migration — now IN SCOPE (was §1's "frozen floor").** §1 of this doc called the S0
compiler-internal seam (`tk_alloc`/`tk_realloc0`/`tk_free0`/`tk_alloc_copy`) out-of-scope, "the frozen
floor," pending the list-header old-size threading. Owner F5 pulls it IN: the S0 header allocations become
`.tks` over the syscall/FFI allocation path (the S16 `plano-s16-arena-mmap.md` mmap-backed region, the
`plano-s16-syscall-intrinsic.md` raw-syscall intrinsic, the `plano-s16-fundacao-crumbs.md` FFI
foundation). The S0 seam is the arena's own bottom, so bringing it into `.tks` is the natural completion
of "the arena in Teko."

**The law tension to state (frozen-C vs migrate-C), and its resolution.** The standing frozen-runtime law
names `src/runtime/teko_rt.{c,h}` + the assert seed as MAINTAINED C (the runtime exception). Bringing
`.h`→`.tks` into scope appears to collide with that. It does not, law-first: the migration doc resolves it
explicitly — *"essa exceção é a PONTE, não o destino… a camada-2 é exatamente o trabalho de RETIRAR a
exceção"* (`migracao-runtime-c-para-teko-0.3.1.md:22-28`). The exception covers the bridge while it
exists; F5 is the work that removes the bridge. There is no tension: maintain-C-now and migrate-C-to-Teko
are the same law at two times.

**What is BLOCKED (design-ahead honesty).** The migration doc's hard precondition: NO camada-2 phase can
LAND until the native fixpoint closes (the own backend self-compiles the compiler —
`migracao-runtime-c-para-teko-0.3.1.md:3-8`; today gen1 native stops at ~53 honest-stops, the float family
inside). So F5's LANDING is gated on the native fixpoint + the §16 foundation crumbs (mmap region, syscall
intrinsic, FFI foundation). What is UNBLOCKED now is the DESIGN tie-in (this section) and the contract
already carried by §16. This correction does NOT re-derive §16; it points at it and states the S0/core.h
inclusion the owner just ruled in.

Each crumb is the smallest independently gate-able step. Gate legend: **[dry]** = compiles + `teko test`
scoped-run green + trivial fixpoint (no emit consumers yet); **[RITUAL]** = full gate: build gen2
`TEKO_BACKEND=native`, scoped regression green, **FIXPOINT gen2==gen3 byte-identical**, `TEKO_ARENA_OBS`
signal. Reseed only at a [RITUAL], never mid-crumb. Bootstrap-safe: no crumb teaches the compiler an
idiom its own seed lacks; the previous released `teko` builds gen1 unchanged. Native-first: every memory
number is the NATIVE peak (the wall lives there).

> NOTE (harness): do NOT run `teko test .` whole (the monomorph leak crashes the container). All
> regression fixtures below are `.tkr`, run ISOLATED, asserting native exit code / stdout — never the
> full-tree test.

### Part #1 — the foundation (crumbs A1–A5)

**A1 — extend the residence oracle to fine-scope depth.** New/extended `src/checker/residence.tks` (or
`spine.tks` extension): `fine_scope_residence(f)` + `scope_touches_arena(body)` + `ResidenceDepth` (§3.4).
Emits, per binding/site, the fine-scope depth. No consumer yet. Prefer a NEW file importing `spine` to
minimize collision with spine agents. **Type shapes:** the three signatures in §3.4. **Fixtures:** none
yet (oracle unconsumed). **Gate: [dry]** — trivial fixpoint. Ritual: NO.

**A2 — emit the fine-scope enter/drop in the C route.** `codegen.tks` reads the A1 plan and, for each
fine scope 1–6 with `need>0`, emits `tk_region_new_sized`/`enter` on the arm ENTER edge and
`leave`/`drop` on the arm EXIT edge (each `if`/`elseif`/`else`/`match` arm, loop arm, bare block —
generalizing the existing `_tkbr` value-arm drop `codegen.tks:5462` to ALL fine scopes). Default
allocation and slice growth target the arm region (not root). **Touches:** `emit_stmt`/`emit_branch_value`/
`emit_loop_while`, the `RegionFrame` stack (`codegen.tks:7955`), `cg_enclosing_region_expr` (`:7963`).
**Fixtures (`.tkr`, native exit code):**

| fixture | proves | expected exit |
|---|---|---|
| `arena_arm_block_dies` | a local used only in a bare block dies at block exit (churn N cycles; corruption = wrong value, not just leak) | 0 |
| `arena_if_arms_distinct` | a local in the then-arm and one in the else-arm each die at their OWN arm exit | 0 |
| `arena_elseif_arm` | a value in an `elseif` arm reclaimed at that arm's exit | 0 |
| `arena_match_arm` | one arena per match arm; the non-taken arms open none | 0 |
| `arena_loop_per_iter` | loop arena reclaimed EACH iteration; live-arena count ≈ 1 over 1M iters (not 1M) | 0 |
| `arena_elision_leaf` | `if x { return a }` leaf opens NO arena (need==0) | 0 |

**Gate: [RITUAL]** — gen2 native, scoped fixtures green, FIXPOINT gen2==gen3, `TEKO_ARENA_OBS`:
`scoped>0`, `reclaim>0`, live-arenas ≈ nesting depth. Ritual: YES.

**A3 — native route inherits the fine-scope lifecycle.** `lower.tks` reads the SAME A1 plan and emits
`region_enter(region_new_sized(current))` on each fine-scope enter edge, `region_leave`+`region_drop` on
each exit edge; per-iteration for the loop arm (never flattened across the back-edge). `buf_ptr` and the
default `tk_alloc` target `tk_region_current()` instead of the fixed `tk_region_root()`
(`lower.tks:3390-3437`, the literal "routes through tk_region_root()" the doc must fix). **Touches:**
`lower.tks` (very hot — coordinate with `backend-memoria`, the native-map agents). **Fixtures:** the A2
set re-run under native (same `.tkr`, native backend). **Gate: [RITUAL]** — FIXPOINT gen2==gen3
byte-identical; `TEKO_ARENA_OBS` scoped>0. Ritual: YES.

**A4 — the object arena (scope #7), DEDICATED per-object (owner F2 RULED).** Thread the object's
residence depth (A1) so an instance's owned interiors bump into the instance's OWN dedicated region — one
region per object instance, born at construction (sized by the object's AST floor, §2.5), dropped at the
object's death (LUB of uses). NOT the shared enclosing arm arena (owner F2: sharing breaks the object's
visibility+security). The §7.8 free-list is NOT needed for a dedicated per-object region (it is
bulk-droppable); it stays reserved for the immortal-F2 chan/journal case only (§2.4). **Fixtures:**
`arena_object_interiors_die` (an object's `[]T` member reclaimed with the object, in its own region),
`arena_object_dedicated_isolation` (two same-arm objects do NOT share a region — one's drop leaves the
other's interiors intact), `arena_object_moved` (an object returned by DPS carries its dedicated region to
the caller — used after return; if the interiors stayed in the callee arm it would be UAF/corruption).
**Gate: [RITUAL].** Ritual: YES.

**A5 — `TEKO_ARENA_OBS` fine-scope attribution (measurement).** Extend the existing obs counters
(`teko_rt.c` obs) to break `scoped`/`reclaim` down per fine-scope depth, so A2–A4 are gate-able by
"where the peak fell." Runtime-read-only, additive. **Gate: [dry].** Ritual: NO.

### Part #2 — native stream, buffers on the arena (crumbs B1–B4)

Depends on: #1 A3 landed (so `tk_region_current()` inside an arm IS the arm arena) and the
io-streaming doc's crumbs 1–5 (surface). Design-ahead: B-crumbs can be WRITTEN against the io-streaming
declared shapes today; they land as io-streaming's surface lands.

**B1 — confirm buf_ptr binds to current region.** Verify (and fixture) that `teko::mem::buf_ptr(CHUNK)`
inside a fine scope allocates in the arm arena and is reclaimed at arm exit (the §4.2 binding). No new
code if A3 re-pointed `buf_ptr`'s target; else a one-line re-point. **Fixture:** `io_scratch_arm_reclaim`
(open a stream in a block, drain, exit block; the 1024 B scratch is gone — obs shows the arm drop
reclaimed it). **Gate: [dry]/[RITUAL]** per whether it moves bytes.

**B2 — route io-adjacent str through `tk_str_concat_len_r(current)`.** The 3 native concat/interp sites
(`lower.tks:11030,12920,12922`, from `arena-por-escopo` M2) pass the current region; an io path/header
built in an arm dies with the arm. **Gate: [RITUAL]** — FIXPOINT; obs `str unroutable` drops on the io
path. Ritual: YES.

**B3 — migrate the compiler's io READS to stream, arm-scoped scratch.** Per io-streaming crumb 7
(`assemble.tks` source read, `fmt.tks`, `project.tks`, `regression.tks`) — the read scratch is arm-local
(#1). Preserving. **Gate: [RITUAL]** (fixpoint). Ritual: YES.

**B4 — migrate the compiler's io WRITES to stream; codegen-emit-direct.** Per io-streaming crumb 8 +
§4.3: the 22 MB `teko.c` writer emits direct (no `csrc` copy-grow), the accumulator being a #3 target.
**Gate: [RITUAL]** — `teko.c` gen2==gen3 byte-identical. Ritual: YES.

### Part #3 — the total purge (crumbs C1–C6; all RULED, none fork-gated)

Owner F1 rules total removal this round. The expurgo methodology (build-first, compiler-enumerates,
iterative reseed) governs the ordering; C1 builds the fixed-array machinery, C2–C4 remove roots, C5 sweeps
the surface, C6 kills the sizing pragmas.

**C1 — build the fixed-array machinery + arm-arena routing (expurgo step 1).** `of_len<T>(n)` zero-fill,
`[n]T` runtime-sized type, index-assign, `count`/watermark, null-deref guard, growth = new-fixed-array +
drop-old with the transient old born in `tk_region_current()` = the arm arena (§2). Additive, coexists
with the old surface; seed so gen0 understands it. This is where the ~1.8 GB copy-grow peak is reclaimed
(the old array dies at arm exit). **Fixture:** `purge_grow_newdrop_reclaimed` (a grow loop whose result
stays local: obs shows each old fixed array reclaimed at arm exit, peak flat). **Gate: [RITUAL]** — FIXPOINT
gen2==gen3 (bytes identical; only the buffer's REGION changed, the `al4a` structural dedup proof,
`arena-por-escopo §4.4`); obs `reclaim>0`. Ritual: YES.

**C2 — remove `grow_inplace` (expurgo step 2a).** Delete the typer arm (`typer.tks:835`), codegen emitter
+ dispatch, native honest-stop (`lower.tks:4339`), and REMOVE (not patch) the C `tk_slice_grow_inplace`
(standing law: the slice-grow machine is dead code deleted, the expurgo does not go through `teko_rt.c`).
No real caller breaks (staged-off). **Fixture:** `purge_grow_inplace_gone` (REJECT — the old surface fails
to compile with a clear diagnostic; the message NEVER names the removed construct, standing law). **Gate:
[RITUAL]** — FIXPOINT. Ritual: YES.

**C3 — remove `with_cap` (expurgo step 2b).** Delete the typer arm (`typer.tks:824`), codegen emitter +
dispatch + allow-list, native honest-stop (`lower.tks:4340`), and REMOVE the C `tk_slice_with_cap`/`_r`.
**Fixture:** `purge_with_cap_gone` (REJECT). **Gate: [RITUAL]** — FIXPOINT. Ritual: YES.

**C4 — remove the C slice-grow machinery (expurgo step 2c).** REMOVE `tk_slice_push`/`_push_fo`/`_push_r`,
`tk_append_bytes_fo`, `tk_free_block` — dead code once C1's fixed-array path is the emit and B4's
codegen-emit-direct lands. NOT a collapse-into-`_r` (the old plan); a deletion. **Gate: [RITUAL]** —
FIXPOINT; obs shows no slice-grow form remaining. Ritual: YES.

**C5 — remove `push`/`empty`/`grow` surface roots + sweep (expurgo step 3, RULED — was fork-gated).**
Delete the `push`/`empty` checker builtins (`typer.tks:808,805`) and `teko::list::grow` (`list.tks:1`, the
leaf choke point). Seed; the self-compiling compiler ERRS at every survivor — that list IS the sweep
checklist. Convert each site to the four natures (§5.3), leaf-first then hot cores, iterative reseed to
green fixpoint. **Fixture:** `purge_push_form_rejected` (REJECT — `teko::list::push` no longer resolves).
**Gate: [RITUAL]** per module. Ritual: YES. (No longer blocked — F1 ruled the idiom.)

**C6 — kill `#arena_size` and `#arena_depth` (owner F4).** Remove the `#arena_size` presize path
(`cg_emit_arena_presize` `codegen.tks:6934`, the `has_arena_size`/`arena_size` fields on `TFunction`
`tast.tks:89-90`, the `merge.tks:326-327` equality, the `monomorph.tks` threading) and the `#arena_depth`
(#476) override. The static AST floor (§2.5) is the ONLY sizing input; the region stays dynamic by
chunk-list. **Fixtures:** `arena_no_presize_pragma` (the pragma no longer parses/has effect — REJECT/no-op),
`arena_dynamic_grows_past_floor` (an arm allocating past its static floor still succeeds — the region
prepends chunks, exit code 0). **Gate: [RITUAL]** — FIXPOINT gen2==gen3 (sizing does not change emitted
bytes). Ritual: YES.

### Dependency order (the mandatory spine)

```
#1: A1 (oracle) → A2 (C route) → A3 (native route) → A4 (object arena, DEDICATED) → A5 (obs)
                                        │
#2: (io-streaming surface crumbs 1-5) ─┴─> B1 → B2 → B3 → B4        [needs A3: current==arm arena]
                                        │
#3: A3 landed ─> C1 (fixed-array machinery) → C2 (grow_inplace) → C3 (with_cap) → C4 (C slice-grow) →
                                              C5 (surface removal + sweep) → C6 (kill #arena_size/#depth)

F5: [BLOCKED on native fixpoint + §16 foundation] .h→.tks FFI migration (design tie-in §5A)
```

A1 before everything (no plan, no gate). A2/A3 before any #3 crumb (the arm arena must exist). #2 needs
A3. C1 (build) before C2–C5 (remove roots) — expurgo build-first law. C6 can land any time after A2/A3
(independent of the slice expurgo). F5 lands only when the native fixpoint closes.

---

## 7. Risks and law tensions

| risk | produced by | resolution (law-first) |
|---|---|---|
| **R1 — a fine-scope drop with a live alias = UAF** (the arena-por-escopo class the canonical §2.2 warns of) | dropping an arm arena while a shallower binding still references into it | the depth check (§3) is the guard: born-at-`d` is dropped-at-`d` ONLY when no use is at depth `<d`; doubt → escape to the shallower arena. Never drop with a live shallower alias. FIXPOINT gen2==gen3 is the detector: a wrong reclaim corrupts a value → bytes diverge → fails BEFORE any UAF ships. |
| **R2 — bytes move region, leak into emitted output** | if codegen dedup depended on pointer identity | measured NOT to: dedup/mangle is STRUCTURAL (`cg_opt_key ==`, `al4a:39,110`; ids node-carried "would not survive copies", `al4a:50`). Region changes address, not the emitted value. Detector: fixpoint. |
| **R3 — loop-arm flattening accumulates** (the peak the owner fears) | previously the `#arena_depth(N>1)` flattening override | MOOT under owner F4: `#arena_depth` is KILLED (C6), so depth is always 1 — each fine scope is ALWAYS its own materialization boundary, and a loop arm is reclaimed every iteration (`modelo §7` carve-out). No flattening exists to accumulate. Fixture `arena_loop_per_iter` is the detector. |
| **R4 — removing grow_inplace/with_cap breaks a caller** | a hidden real user of the staged-off primitives | measured: no real call sites (only typer/codegen/lower plumbing + runtime). The self-compile ENUMERATES any survivor on removal (expurgo). REJECT fixtures pin the new diagnostic. |
| **R5 — total push/empty removal (F1) with no idiom** | ~4900 sites need a replacement form | RESOLVED by owner F1: the idiom is the standing NO-PUSHES four natures (§5.3); the expurgo (build-first, compiler-enumerates, iterative reseed) executes it safely, not a hand-edit campaign. No HALT — the idiom is ruled. |
| **R6 — collision in hot files** | `codegen.tks`/`lower.tks`/`typer.tks` | oracle in a NEW file; A2 (C) and A3 (native) separated; coordinate with `backend-memoria`, `arena-escopo-local`, the native-map agents. #3's runtime edits are REMOVALS staged behind the additive C1 build. |
| **R7 — teko_rt.{c,h} touched** | removing the C slice-grow machinery (C2–C4), obs extension | standing law: the expurgo REMOVES the slice-grow machine (dead code), it does NOT patch `teko_rt.c`; the obs extension is read-only. The runtime exception is a bridge being retired (F5), not a permanent patch. |
| **R8 — dedicated per-object region cost/kind (owner F2)** | scope #7 as a DEDICATED region per object | ruled dedicated (F2). Cost is bounded: each object region is born at the object's static floor (§2.5), not 64 KiB, and bulk-dropped O(1) at object death. It does NOT need the §7.8 free-list (that is only for immortal-F2 chan/journal entries). Risk of many small regions is mitigated by static-floor sizing + O(1) bulk drop + `need==0` elision for interior-free objects. |
| **R9 — F1 collides with the growable-collection API** (cross-doc) | `plano-collections-genericas-e-concorrentes-0.3.1.md` / `collections-generics-fase1b-crumbs.md` define growable methods (`List::push`, `Dictionary::insert` push `:233-235`, `SortedSet`, `PriorityQueue::enqueue` `:333`, `ConcurrentStack::push` `:616`) on the removed primitives | FLAGGED, not silently overridden. Owner F1 rules arrays fixed + no growable method, so those collections must be re-expressed as new-fixed-backing + return (DPS). This is a cross-doc re-spec REPORTED up (§5.3 step 4); it does NOT block the compiler-core expurgo (the core does not use the growable-collection surface). Not a new issue I open — a scheduling flag for the owner/coordinator. |
| **R10 — F5 (.h→.tks) vs the frozen-runtime law** | bringing `teko_rt.{c,h}` / core.h into `.tks` scope | RESOLVED law-first: the runtime-C exception is "a PONTE, não o destino" (`migracao-runtime-c-para-teko-0.3.1.md:22-28`); F5 is the camada-2 work that retires the bridge. Maintain-now and migrate-later are the same law at two times. Landing is gated on the native fixpoint + §16 foundation (§5A, §8). |

**Residual law tension forcing a HALT: NONE.** Every fork is now RULED; the consequences are propagated,
not deferred. Teko-only honored (product in `.tks`; the C slice-grow machine is REMOVED not patched; the
runtime exception is a retiring bridge). W15 full-Javadoc on every snippet. Issue-100%: #1+#2+#3
(including the F1 total removal and the F4 pragma kill) deliver the whole correction this round. FIXPOINT
gen2==gen3 byte-identical is the inviolable gate and the detector of every reclaim risk. The one cross-doc
consequence (R9, the growable-collection re-spec) is REPORTED up, not turned into an issue by me.

---

## 8. What remains BLOCKED (design-ahead honesty)

- **F5 (the `.h`→`.tks` FFI migration, §5A)** is blocked on the native fixpoint closing + the §16
  foundation crumbs (mmap region, syscall intrinsic, FFI foundation) — the camada-2 hard precondition
  (`migracao-runtime-c-para-teko-0.3.1.md:3-8`). Its DESIGN tie-in (§5A) and the §16 contract are ready
  now; only the landing waits.
- **#2 B1–B4** depend on the io-streaming surface crumbs 1–5 landing; those are themselves unblocked
  (`io-streaming §10` — leaf-new, compiles today). The buffer↔arena binding (B1) needs #1 A3.
- **R9 — the growable-collection re-spec** (`plano-collections-genericas-…`) is a cross-doc follow-up the
  owner/coordinator schedules; it does not block the compiler-core expurgo.

Everything else — the oracle (A1), the C and native fine-scope lowering (A2/A3), the DEDICATED object
arena (A4), the obs (A5), the fixed-array machinery + total push/empty/grow_inplace/with_cap removal
(C1–C5), and the `#arena_size`/`#arena_depth` kill (C6) — compiles against today's tree and needs no
blocked API. C5's idiom is ruled (F1), so it is no longer fork-gated.

---

## 9. The five forks — RULED by the owner (closed) and propagated

All five §9 forks are RULED. Each entry: the owner's verbatim ruling, the recommendation it overruled (or
confirmed), and where the consequence is propagated in the body.

**F1 — replacement idiom for `push`/`empty` (was: gates #3 Tier B). RULED.**
Overruled recommendation: (c) keep the `List<T>::push` METHOD arena-routed. Owner REJECTS (c):
*"não é pra manter o método, nossos arrays DEVEM SER DE TAMANHO FIXO E SEM POSSIBILIDADE DE PUSH / GROW /
ou qualquer artifício de expandir um array. Quer expandir? Cria um novo e dropa o antigo."*
Ruling: arrays are FIXED-SIZE. There is NO `push`/`grow_inplace`/`with_cap`/in-place append AND no
growable method. The idiom to grow is: allocate a NEW fixed array (exact-sized at its own allocation) and
DROP the old — closest to option (b) presized-index builder, but strictly no growth primitive survives.
Tier B (surface removal of `push`/`empty` AND the `grow`/method) HAPPENS this round; the §5 purge is
total. Idiom = the standing NO-PUSHES four natures. **Propagated:** §0 divergence #2, §5.1, §5.2 table,
§5.3, crumbs C1/C5, R5, R9.

**F2 — object arena: shared vs dedicated. RULED.**
Overruled recommendation: (a) always the enclosing fine-scope arena (shared). Owner REJECTS shared:
*"pq compartilhada? Isso quebra a visibilidade e segurança de um objeto por definição."*
Ruling: the object arena is a DEDICATED per-object region (option b) as the RULE, not just the decoupled
case. A shared/enclosing arena breaks the object's visibility+security by definition. **Propagated:**
§2.3 (reworked to dedicated-per-object), §2.4 (coexistence with the §7.8 free-list), crumb A4, R8.

**F3 — Tier A push re-point: native-first or C-route-first. RULED (confirmed).**
Owner: "ok" → confirm native-first: C1 stays after A3; NO C-route-first partial. **Propagated:** §6
dependency order (unchanged — C-series after A3), R-none (already the plan's spine).

**F4 — arena sizing pragmas. RULED (further than recommended).**
Overruled recommendation: fast-follow deferral of `#arena_depth(N>1)`. Owner goes further:
*"vamos matar #arena_depth e #arena_size, já tem ruling para isso, o compilador agora deve medir
estaticamente o tamanho do slot para quando a arena ser materializada ter um piso de apoio mínimo."*
Ruling: KILL BOTH `#arena_depth` (#476) AND `#arena_size` (the profiler presize, `cg_emit_arena_presize`
`codegen.tks:6934`; canonical §3 cites the drifted `:9832`). The compiler measures the slot size
STATICALLY (the AST floor) so the arena has a minimum support floor at materialization. No profiler-driven
dynamic sizing, no depth-override pragma. **Crucial:** this does NOT make the arena static — the arena
stays DYNAMIC with a static FLOOR (lower bound), no ceiling, growing by chunk-list (§2.5). This severs the
canonical §3 floor↔`#arena_size` coupling (`arena-especificacao-unica-0.3.1.md:147-149`); dynamic need is
served by chunk-list growth, and killing `#arena_size` introduces no UAF and no correctness regression
(at worst a few more chained chunks). **Propagated:** §0 divergence #3, §2.5 (the full model + proof),
crumb C6, R3.

**F5 — `.h` migration. RULED (overruled the out-of-scope recommendation).**
Overruled recommendation: leave frozen / out-of-scope. Owner OVERRULES:
*"Não é fora de escopo, todo '.h' feito include no C deve virar código em tks para usar FFI para a ABI
nativa ou syscall sem depender do gcc (já cobre o native)."*
Ruling: IN SCOPE. Every C `.h` include becomes `.tks` using FFI for the native ABI or a syscall, without
gcc. The S0 `core.h` list migration is in scope; tie into §16 (`plano-s16-expurgo-libc-completo.md`) and
the runtime migration (`migracao-runtime-c-para-teko-0.3.1.md`). **Propagated:** §0 divergence #5, the new
§5A (scope + law-tension resolution + what is blocked), §6 dependency graph (F5 leg), R10, §8.
