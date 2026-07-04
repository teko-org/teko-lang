# TEKO_EVOLUTION_JUSTIFICATION — expected improvements, in NUMBER and in CODE QUALITY

> **Purpose.** This is the deliverable the legislator asked for: for each evolution feature in
> `TEKO_EVOLUTION_DESIGN.md`, **(1)** the expected improvement *in numbers* (grounded in or projected
> from the real corpus) and **(2)** the *code-quality* rationale (clarity, safety, maintainability,
> doctrine-fit against the Laws M.0–M.5). It does not legislate; it justifies. It ends with a per-feature
> ROI ranking and a one-paragraph "build-first" recommendation.
>
> **Method / honesty note (M.3).** Every count below was measured against the live tree on branch
> the reboot line (now `main`). Where a number is a *projection* (e.g. a compile-time speedup) it is labelled
> **[projected]** and the model is shown. The corpus measured: **49 `.tks` source units**, **40 `.c`
> compilation units**, **49 `.h`**, **14 `.tkt` tests** — **63 VM-run units (`.tks`+`.tkt`)**, **138
> `.c`/`.h`/`.tks` source files**, across **11 subsystems** under `src/`. Source LOC: **`.tks` ≈ 9,312**,
> **`.c` ≈ 8,698**, **`.h` ≈ 1,947**.

---

## Corpus baseline (the denominator for everything below)

| Metric | Count | Where measured |
|---|---:|---|
| `.tks` source units | 49 | `src/**/*.tks` |
| `.c` compilation units | 40 | `src/**/*.c` |
| `.h` headers | 49 | `src/**/*.h` |
| `.tkt` VM tests | 14 | `src/**/*.tkt` |
| VM-run units (`.tks`+`.tkt`) | 63 | the "≈64 corpus files" |
| subsystems | 11 | `src/{lexer,parser,checker,codegen,vm,emit,text,build,runtime,assert,…}` |
| `teko::list::push` calls (`.tks`) | **120** | `grep list::push` |
| `teko::list::empty` calls (`.tks`) | **103** | `grep list::empty` |
| total `teko::list::*` calls (`.tks`) | **212** | combined |
| `[]T as x` slice patterns (`.tks`) | **98** | `grep '\[\]…as '` |
| `.len` reads (`.tks`) | **307** | `grep '\.len'` |
| index `x[i]` (`.tks`, approx) | **305** | indexing heuristic |
| loop-accumulator sites (`.tks`) | **78** | `mut … (list::empty\|[])` |
| files touching `teko::list` | 27 / 49 | 55% of source units |
| `TK_LIST(...)` distinct instantiations (C) | **16** | hand-stamped list types |
| `Parsed*` result-struct duplications (C) | 3 types / 5 sites | `ParsedUse`, `ParsedUses`, `ParsedMainFile` |
| `malloc` sites (C seed) | **68** | leak/lifetime surface |
| `realloc` sites (C seed) | **19** | resize/aliasing surface |
| `free` sites (C seed) | **65** | double-free surface |
| `abort` sites (C seed) | **87** | M.1 fail-loud points |
| `tk_alloc` seam sites | **0** | the swap point — not yet introduced |
| `tk_type`-by-value param/copy sites (C) | **123** | ref candidate |
| file-read / I/O sites (C) | 13 | `driver.c`, `build/discover.c`, `build/assemble.c` |

These are the load-bearing denominators. "Improvement" below is always stated *relative to one of these*.

---

## 1. Collections (copy-append + read-side)

### Improvement in NUMBERS

- **Self-host unblock — the headline.** **212** `teko::list` calls + **98** `[]T as x` patterns + **307**
  `.len` + **305** index reads are spread across **27 of 49** source units (55%). The front-end **cannot
  self-compile** until the collection read-side + COPY-append are lowered through codegen/VM. Per
  `TEKO_CORRECTION_PLAN` (§15, gap #14), the *current* self-host wall is literally
  `parse_expr.tks:115` — a `[]lexer::Token as ts` slice pattern. So collections is the gate on **~55% of
  the corpus** and on self-host itself; nothing else on this list unblocks anything until it lands.
- **LOC saved vs. the alternatives.** The seed expresses accumulation as **78 loop-accumulator sites**
  (`let mut xs = list::empty; … xs = list::push(xs, x)`). The two avoidance strategies the language would
  otherwise force are: (a) **two-pass** (count, then fill a fixed `[]T`) — roughly *doubles* each of the
  78 loops (~+150–230 LOC of duplicated traversal + a count variable each); (b) **manual realloc
  bookkeeping** in user code — the seed already shows this costs ~3 lines per growth site (`realloc` +
  null-check + `abort`), i.e. the **19** C `realloc` sites are exactly the boilerplate copy-append
  *removes from the language surface*. Copy-append collapses each accumulator to **1 expression**.
- **Performance, stated honestly (M.3).** COPY-append is **O(n) per push → O(n²) per fully-built list**;
  the real (arena-backed, amortized) collection in S7 is **O(1) amortized per push**. For the bootstrap's
  list sizes (token streams, AST node lists — hundreds, not millions) the O(n²) is *correct and
  invisible* (M.5). The design's promise is the key number here: the migration to amortized append is
  **0 signature changes** — `alloc(len+1)` becomes `region_alloc`, same surface (Design §2).

### Code-quality rationale

- **Immutability by default (M.1/M.2).** COPY-append returns a *fresh* list; the input is never mutated.
  This is the same discipline that fixed the heap bug this session (see §3): the checker reuses one `env`
  value **non-linearly** across sibling match arms, and the *functional* copy-on-extend is what makes that
  safe. Copy-append generalizes that safety to all 212 list sites.
- **Forward-compatible signature.** The single most valuable quality property: the surface (`xs + [x]`,
  `push`, `empty`, `[]T as x`, `.len`) is **frozen now and survives the arena swap unchanged**. 212 call
  sites written today need **zero** edits when the runtime becomes amortized/arena-backed. That is the
  cheapest possible migration story for the largest single feature.
- **Doctrine-fit:** M.0 (no GC — copy + bump-alloc), M.5 (O(n²) is *correct* for batch sizes; pay for the
  amortized version only when long-running processes need it), M.4 (read-side + append is exactly and only
  what self-host needs — nothing speculative ships).

---

## 2. Generics (monomorphization)

### Improvement in NUMBERS

- **Duplication that collapses to one definition.** The C seed hand-stamps **16 distinct `TK_LIST(...)`
  instantiations** (`tk_tokens`, `tk_strs`, `tk_texpr_list`, `tk_env`, `tk_type_table`, …) — each is a
  manually-named copy of one list shape. Add **3 `Parsed*` result-struct duplications**
  (`ParsedUse`/`ParsedUses`/`ParsedMainFile`) and the per-type `to_string`/`parse` family the design
  flags for collapse (`Parsed<T> ×14` and unified `parse`/`to_string`, Design §S9). **Conservative total
  of hand-specialized type duplications eliminable by one generic definition each: ~16 list + ~14 Parsed +
  the per-type parse/print family ≈ 30+ duplications → ~3 generic definitions** (`List<T>`, `Result<T>`,
  `Box<T>`).
- **LOC / maintenance-surface reduction.** The `TK_LIST` macro body is ~1 definition expanded **16×** at
  compile time; replaced by `type List<T>` written **once**. The maintenance win is the multiplier: a
  change to list semantics today is **16 edit sites** (one per instantiation's surrounding code) +
  whatever the macro can't capture; with monomorphization it is **1**. Same 14→1 for `Parsed<T>`.
- **Cost is paid only for use (M.5).** Monomorphization stamps native code **per instantiation actually
  used** — the *same* 16 concrete list types exist in the binary, but in **source** they are 1. Net source
  reduction with no binary-size or speed regression.

### Code-quality rationale

- **One definition vs. N copies.** The defining quality property: today the 16 list instantiations can
  *drift* (a fix applied to 15 of 16). One generic definition makes drift structurally impossible.
- **No boxing, no dispatch (M.0).** The design **rejects dictionaries/Option-B** explicitly — generics are
  monomorphization, producing code *identical to the hand-written per-type code* already in the seed. Zero
  vtables, zero `any`, zero erasure. The generic version is a *strictly cleaner spelling of what the seed
  already does by hand* — which is the strongest possible doctrine-fit argument.
- **Doctrine-fit:** M.0 (no runtime dispatch/boxing), M.3 (no `any` black box, no erasure — honest), M.4
  (designed against the *real* `Map<K,V>` need that forces constraints, not speculatively), M.5 (code-size
  only for instantiations used).

---

## 3. Arenas + memory control

### Improvement in NUMBERS

- **Defect-class eliminated — with a real, this-session example.** The seed has **68 `malloc` + 19
  `realloc` + 65 `free` + 87 `abort`** sites = a **152-site manual-lifetime surface**, and **0
  `tk_alloc`** seams (not yet introduced). The realloc surface is the dangerous one: this session a
  **real heap-corruption bug** lived in `tk_env_define` (`src/checker/scope.c:16`). The original used a
  linear `TK_LIST` push that **realloc'd a buffer in place** while the checker held **multiple live
  aliases** to it (sibling match arms and sibling functions each extend the *same* base env). After a
  realloc, every other holder's `.ptr` dangled → *"pointer being freed was not allocated" / abort*. The
  fix (now in the tree, comment at `scope.c:17-24`) is **copy-on-extend**. That single bug **is** the
  defect class arenas erase wholesale:
  - **use-after-realloc** (the exact bug): impossible when allocation is bump-only and lifetime is a
    region span, not an aliased buffer.
  - **double-free**: the 65 `free` sites become **~1 `region_drop` per scope** — you free *regions, not
    objects*, so there is nothing to free twice.
  - **leak**: the 68 malloc sites' leak surface collapses to "the region you forgot to drop" — and the
    root region is reclaimed at `exit` regardless (the seed's current correct-for-batch behavior is
    *preserved*, not regressed).
  - **OOB / dangling on resize**: removed at the source — bump allocation never moves a live object.
- **The single seam.** Routing the **68 malloc** (+19 realloc) sites through **one `tk_alloc(size)`** seam
  is a **no-cost, mechanical** change *now* (Design §5.1) that turns the later malloc→`region_alloc` swap
  from a 68-site edit into a **1-site edit**. This is the highest-leverage zero-risk action available.
- **Deterministic bulk-free.** A per-scope `region_drop` is a **pointer reset / chunk-list walk** — O(1)
  amortized in objects-freed, vs. 65 individually-tracked `free`s. For a long-running process (a future
  language server / watch mode) this converts an unbounded leak into bounded per-request memory.

### Code-quality rationale

- **Explicit lifetimes, no GC (M.0).** Lifetime becomes **visible scope structure** — a block, a function,
  the program — not per-object bookkeeping. A bump allocator is a pointer increment; bulk-free is a
  pointer reset. No managed heap, no per-object header, no collector thread.
- **Exclusion-by-construction (M.1).** The bug above was *possible* because manual realloc + manual
  aliasing is possible. Arenas make the whole **152-site** manual surface unreachable: you can't
  use-after-realloc what you never realloc; you can't double-free what you free by region. This is the
  doctrine's strongest form — not "fail loud at runtime" but "the failure mode cannot be expressed."
- **One concept, three lifetimes (M.5).** One region mechanism covers root/scope/local *and* the DI
  `#singleton/#scoped/#transient` directives — no separate machinery. The escape check that makes it safe
  is **one integer depth comparison**, not a borrow solver.
- **Doctrine-fit:** M.0 (metal, no GC), M.1 (defect classes excluded by construction; OOM panics never
  returns NULL — matches the 87 existing `abort`s), M.5 (one region concept, single-integer check).

---

## 4. ref / pointers

### Improvement in NUMBERS

- **Wasteful pass-by-copy today.** `tk_type` is a tagged-union struct copied **by value at 123 sites** in
  the C seed (`tk_type t/r/s/f/…` params). It is the hottest value in the checker (the 43 `tk_type t`
  params alone). Every one of those 123 is a full struct copy where a `ref tk_type` would pass **one
  machine word**. Other large value structs copied the same way: the parser AST nodes
  (`tk_expr`/`tk_function`/`tk_file` — `parser/ast.h` defines **60** struct types) and the typed-AST
  nodes (`tast.h`, 72 declared members). `ref`/`ref mut` converts the **123 tk_type copies** (plus the
  AST/TAST copy traffic) from O(sizeof struct) to O(1) word.
- **Defect-class: null-deref eliminated.** A `ref` is **never null** by construction (it always denotes a
  live arena slot). "Maybe a reference" is the existing `(ref T)?`. So the entire null-deref class on
  internal references is **structurally removed** — not checked, *excluded*. The current opaque
  `ptr`/`uptr` stay FFI-only and are explicitly *not* merged with `ref` (Design §3.5).
- **Soundness cost: zero new analysis.** `ref` adds **0** new checks — its soundness *is* the arena escape
  check already built in S2 (one depth comparison). The only addition is the per-call
  aliasing-XOR-mutation rider (a coarse per-call exclusivity set, not a borrow graph).

### Code-quality rationale

- **Explicit aliasing (M.2/M.3).** `ref` / `ref mut` makes aliasing *visible at the call site* — three
  declared positions, no hidden references, no coloring. The reader sees exactly where mutation-through
  can happen. This is the opposite of an implicit pointer.
- **Escape-checked, never-null (M.1).** A `ref` cannot outlive its region (escape check) and cannot be
  null (construction). Two whole bug families — use-after-free *and* null-deref — are excluded by the same
  machinery, at no extra type-system cost.
- **Honest separation from `ptr` (M.3).** `ref` is safe internal arena aliasing; `ptr`/`uptr` remain
  opaque transport-only FFI values. They never merge — no pretend guarantee.
- **Doctrine-fit:** M.0 (a `ref` is a bare address / `{arena,ptr}` tag — no indirection tower), M.1
  (never-null + escape-checked by construction), M.2/M.3 (explicit, visible, no overclaim), M.4 (waits for
  arenas — soundness depends on the escape check existing first).

---

## 5. Concurrency — the full spectrum (async/await kept ON the table)

The design doc *rejects* async/await. The legislator asked for it to be **presented fairly and
quantified**, not vetoed. So here is each model on its merits, then the comparison table, then a
recommendation that still leaves async on the table for the legislator's call.

### 5a. async/await (cooperative state-machine)

- **Where it helps, in numbers.** I/O-bound concurrency. The seed has **13 file-read/I/O sites**
  (`driver.c`, `build/discover.c`, `build/assemble.c`). Today these are sequential blocking reads. The
  *future* package-fetch story (REBOOT §11, deferred) is many independent network round-trips — the
  canonical async win: **N** concurrent fetches overlap their latency instead of summing it. For a
  language server / watch mode, async would let a single thread interleave I/O waits without N threads.
- **Honest cost (why the design leans against it).** **Function coloring** — every caller of an `async`
  function must itself be `async` or block; this is *viral* and would re-color a large fraction of the 13
  I/O call chains' callers. Plus a **hidden executor / scheduler** (anti-M.0 — the doctrine forbids a
  hidden runtime) and a **disguised state machine** at each await point (anti-M.3 — the cost is not
  visible at the call site). The numeric trade: async buys overlap on **~13 I/O sites + future fetches**
  at the cost of a viral type-coloring discipline across their transitive callers and one always-present
  executor. For a **batch compiler that is CPU-bound, not I/O-bound**, the denominator it optimizes
  (I/O wait) is small and the tax it imposes (coloring + executor) is global.

### 5b. routines (coroutines / green threads / goroutine-style)

- **Numbers.** M:N green threads give cheap spawn (thousands of tasks, ~KB stacks) vs. 1:1 OS threads
  (~MB stack, syscall to clone). For the compiler's actual parallel unit — **49 `.tks` / 63 VM-run
  units** — you need *tens*, not thousands, of tasks, so the M:N spawn-cost advantage is **near-zero in
  this workload**. The structured-scope ergonomics (a `scope{}` that joins all children) are the real win
  and survive into the chosen model.

### 5c. concurrency (interleaving + shared-state safety without a GC)

- **The safety number: data races excluded by construction (0, not "fewer").** `move-on-send` ⇒ no two
  live owners of a value ⇒ **no data races, structurally**. Combined with the arena "task scope" axis on
  the escape check, a `ref` may cross into a child task **iff** its region provably outlives the task —
  and structured concurrency guarantees the parent always outlives children, so the common case is *legal
  with zero annotation*. This reuses the **same single depth-compare** from S2; concurrency invents almost
  no new analysis.

### 5d. parallelism (true multi-core — where the biggest NUMBER lives)

- **The compile-time speedup [projected].** The checker/lowering passes over the **49 `.tks` source
  units** (or 63 VM-run units) are *embarrassingly parallel per file* up to the cross-file resolve. Model:
  let *S* = serial fraction (cross-module resolve, link), *P* = parallelizable fraction (lex+parse+check
  per file). Amdahl on *N* cores gives speedup `1 / (S + P/N)`.
  - If per-file work is **P = 0.8** (lex/parse/check dominate; resolve+emit serial):
    - 4 cores → **~2.5×**; 8 cores → **~3.3×**; 16 cores → **~4.0×**.
  - If the pipeline is made more data-parallel (**P = 0.9**):
    - 4 cores → **~3.1×**; 8 cores → **~4.7×**; 16 cores → **~6.4×**.
  - The ceiling is the resolve/link serial tail; work-stealing across the 49 files keeps cores busy on the
    P fraction. **This is the single largest quantified number in the whole evolution** — a multi-× drop
    in self-compile wall-clock, growing with core count.
- **Cost.** 1:1 OS threads (the design's first cut) — visible cost, no hidden scheduler. Data-parallel
  passes need the per-file work to be arena-isolated (each file checks into its own scope region) — which
  the arena layer already provides. M:N work-stealing is a later, *swappable* optimization (Design §3.4).

### Comparison table

| Model | Latency (I/O) | Throughput (multi-core) | Safety story | Runtime cost | Code-quality / ergonomics | Doctrine-fit |
|---|---|---|---|---|---|---|
| **async/await** | **Best** for the 13 I/O sites + future fetches | Poor (cooperative, single-thread by default) | Races still possible if shared state | **Hidden executor** (always present) | Viral **function coloring**; disguised state machine | **Weak** — hidden runtime (¬M.0), invisible cost (¬M.3) |
| **routines (M:N green)** | Good | Good, cheap spawn | Depends on memory model | Hidden scheduler / green-thread runtime | Cheap spawn; structured scope ergonomics | Mixed — M:N runtime tension with M.0 |
| **concurrency (CSP + move + scope)** | Adequate | Good | **Data-race-free by construction** (move-on-send) | Channels (futex/mutex), no executor | `scope{}`/`spawn`/`channel` — visible at call site | **Strong** — M.0/M.1/M.3 all satisfied |
| **parallelism (1:1 threads + work-stealing)** | n/a | **Best** — the multi-× compile speedup | Same as CSP (shares the model) | OS thread per spawn (visible) | Data-parallel passes; explicit | **Strong** — visible cost, reuses escape check |

### Recommendation (leaving async on the table)

The **CSP + structured-scope + parallelism** model (Design's S8) is the recommended substrate: it delivers
the **0-data-race-by-construction** safety number and the **multi-× compile-time** throughput number while
reusing the *same single depth-compare* the rest of the evolution already pays for — its marginal analysis
cost is near zero. **async/await is not dismissed:** it owns the I/O-latency column and is the right tool if
teko-lang's future centers on **I/O-bound workloads** (a network-heavy package client, a long-lived
server). The honest framing for the legislator: async optimizes a denominator (I/O wait) that is **small
for a batch compiler** (~13 sites) at a **global, viral cost** (coloring + executor that wounds M.0/M.3),
whereas CSP+parallelism optimizes the denominator that is **large** (CPU work over 49–63 units) at **near-
zero doctrine cost**. If the roadmap's center of gravity shifts to I/O, revisit async as a *bounded,
`teko::io`-style swappable boundary* — never as the global default surface.

---

## 6. Per-feature ROI ranking

ROI = (improvement magnitude) ÷ (implementation cost) , adjusted by dependency position.

| Rank | Feature | Improvement magnitude | Impl. cost | Dependencies | ROI verdict |
|---|---|---|---|---|---|
| **1** | **`tk_alloc()` seam** (sub-step of Arenas) | Turns a future 68-site swap into a 1-site swap | **~0 (mechanical, now)** | none | **Highest** — do it immediately, zero risk |
| **2** | **Collections (copy-append + read-side)** | Unblocks self-host of **~55% of corpus**; 212 calls / 98 patterns / 307 `.len` lowered; frozen forward-compatible surface | Medium-High (codegen/VM slice subsystem) | self-host backend only | **Top** — gates everything; mandatory |
| **3** | **Arenas + escape check** | Erases the **152-site** manual-lifetime surface + the real use-after-realloc class; deterministic free | Medium (bump allocator + 1 depth check) | seam | **High** — the keystone; everything safe reuses it |
| **4** | **Generics (mono)** | ~30+ hand-specializations → ~3 definitions; 16 `TK_LIST` → 1; no boxing | Medium (mono pass) | self-host (M.4 data); parallel with arenas | **High** — big dedup, independent mechanism |
| **5** | **ref / ref mut** | 123 `tk_type` copies → O(1); null-deref class excluded; **0 new analysis** | Low-Medium (rides the escape check) | arenas (S2) | **High per unit cost** — cheap, big safety |
| **6** | **Real dynamic collections (S7)** | O(n²)→O(1) amortized; Map/Set; **0 signature change** | High (generics+arena+ref-mut+constraints) | S4+S6+S2/S3 | **Medium** — high value, deepest dependency stack |
| **7** | **Concurrency: parallelism (CSP+scope)** | **multi-× compile speedup** (2.5–6.4× projected); 0 data races | High (threads, channels, task axis) | arenas+ref | **Medium** — biggest single number, but last & costly |
| **8** | **Concurrency: async/await** | Overlaps ~13 I/O sites + future fetches | High (executor + viral coloring) | — | **Low for a batch compiler** — kept on the table for an I/O-future |

## What to build first for the biggest quality/number win

**Introduce the `tk_alloc()` allocation seam now (zero cost, turns a 68-site future swap into one site),
then drive the collections read-side + COPY-append through codegen/VM to finish self-host.** Collections is
the only feature that is simultaneously (a) the gate on **~55% of the corpus** and on self-host itself —
the current wall is a real slice pattern at `parse_expr.tks:115` — and (b) shippable with a **frozen,
forward-compatible surface**, so all **212 list calls / 98 patterns / 307 `.len` / 305 indexes** written
today survive the later arena/amortized swap with **zero edits**. The seam costs nothing and de-risks the
single most dangerous future migration (the same realloc-aliasing class that produced this session's
`tk_env_define` heap corruption). After self-host, build **arenas + the one escape check** next: it is the
keystone whose single integer comparison simultaneously buys use-after-free safety, `ref` soundness, DI
lifetimes, and — with one added axis — concurrency's data-race freedom. Build that one check once;
everything safe downstream is free.
