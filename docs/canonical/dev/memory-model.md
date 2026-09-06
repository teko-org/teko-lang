# Memory Model — Arenas, the Spine, and the Reference System

Teko manages memory **without a garbage collector and without a user-facing borrow checker**.
The default path is invisible; increasingly explicit, increasingly risky layers sit on top of
it, each opt-in and each visible at its declaration site — cost proportional to what you use,
never to what the language could theoretically do.

## The containment ladder

```
invisible arena (default)         — compiler-managed region allocation; no annotation needed
        │
        ▼
adopt { }                         — lexical, bulk-drop region for cyclic/long-lived data
        │
        ▼
unsafe (type/function modifier)   — raw allocation, raw pointers, full risk, fully visible
        │
        ▼
RawBuf / Owned<T>                 — malloc/free-shaped raw allocation, explicit and contained
```

Each rung is a strictly larger risk surface than the one above it, and every rung is *named at
its use site* — there is no silent escalation. This is the same "security wins at the edge,
simplicity wins on the common path" posture the rest of the language follows: the 99% path
(arena, references, `error` values) stays simple; the 1% that needs raw control pays for it
explicitly, at the boundary that needs it.

### Arena regions (default)

Allocation and deallocation are compiler-managed: values live in a **region** (an arena), and
the region is dropped as a whole when its owning scope ends — no per-object bookkeeping, no
GC pause, no `free` call in ordinary code. Regions form a tree (`tk_region_new(parent)`), and
concurrency reuses exactly this tree shape rather than inventing a second allocation model —
see "Isolate concurrency" below.

### `adopt { }` (opt-in, lexical)

For **cyclic or long-lived** graphs that don't fit a strict arena lifetime (a doubly-linked
list, a parent↔child object graph), `adopt { }` opens a region that is bulk-dropped at the
block's closing brace, regardless of internal cycles. This is the escape hatch for graph
shapes the ordinary arena discipline cannot express soundly — cyclic structures compile today
via class reference fields; adopting them is opt-in, never mandatory.

### `unsafe` (type/function modifier — not a block)

`unsafe` in Teko is a **modifier on a type or function**, not a block scope like Rust's
`unsafe { }`. Marking a `struct`/`class` `unsafe` addresses every method on it as unsafe and
enables raw pointers (`ptr<T>`) on its fields; marking a bare structural alias `unsafe` is
rejected outright (an alias has no members to address — the capability belongs to the aliased
type, not the alias). If a type doesn't use raw pointers or call unsafe operations, it has no
reason to carry the modifier — the compiler does not require it, but it stays a code-review
signal: an `unsafe` type that turns out to need nothing unsafe is a smell, not an error.

### `RawBuf` / `Owned<T>` (fully explicit)

The bottom rung: `malloc`/`free`-shaped raw buffers and owned pointers, always behind
`unsafe`, always with a matching manual release. This is where FFI-adjacent code and anything
genuinely needing C-shaped ownership lives — contained, never leaking into the safe layers
above it.

## The reference model

Values (structs, primitives, inline variant payloads) are **cheap-to-copy value types**.
Classes are **reference types**. A function that receives a class argument, or a struct field
that stores a borrowed pointer, uses the `ref` modifier — explicit at the parameter/field
declaration, with the receiver of an instance method implicit.

- **`T` is never null.** `NULL` is not "absent" in Teko; absence is spelled `T?`, and only
  `T?` carries it. There is no view-type narrowing a `ref` to `T?` back down to `T` — a stored
  borrow of an optional field is only accepted when the field's and the borrow's scopes match
  in lifetime (bounded by the spine, below).
- **`ref` is the whole surface.** `Ref<T>` is an internal checker representation, never
  written by a user; `&T`/`*T` sigils do not exist in safe code — the only pointer sigils that
  exist at all are `unsafe`-only raw pointers (`ptr<T>`), explicit at the unsafe boundary.
  There is deliberately **no safe immutable borrow**: a safe borrow is always a
  mutable-capable reference (an annotation for immutability may be added later, but
  immutability itself is not treated as a safety property today).
- **Cap-2 invariant.** At most two owning references exist per heap allocation in safe code —
  the spine infers this and the checker validates it; `unsafe` is the explicit, contagious way
  to exceed the cap.
- **Swap is value-swap.** Swapping two `ref`-to-`T` fields swaps the two `T` values (a deep
  copy each way), never the underlying pointers — there is no reference-aliasing side channel
  hiding inside `swap`.

## The spine: bounded points-to / uniqueness inference

The **spine** is the static analysis that makes the reference model sound without asking the
programmer to write lifetime annotations. It runs per function, over a **finite** universe of
cells (every local binding, every one-hop field off a binding, every `Reference` parameter),
tracking three joined axes per cell:

- **points-to** — what a reference cell may refer to,
- **borrowed-from** — the referent a borrow was taken from (single-assignment: a `ref` binding
  is seeded once, at its one syntactic borrow site — never reconciled from two candidate
  origins),
- **uniqueness** — whether a cell is the sole live reference to its allocation or one of
  several.

The lattice has bounded height (≤3 per axis) over a finite universe, so the analysis
terminates by construction — it is a worklist fixpoint over a small, statically-known state
space, not an open-ended alias analysis. The spine sits *beside* the older, coarser
"escaping-names" analysis (which only computes which locals outlive their frame): the spine
certifies a strict subset of what that coarser analysis over-approximates, and the two never
have to agree on more than that projection. What the spine buys, concretely: sound stored
borrows in structs/classes, and a sound manual `mem::free` outside the arena discipline —
without a Rust-shaped borrow checker the programmer writes annotations for.

## Isolate concurrency

The concurrency unit is **`isolate`**, not a suspension model. This distinction is load-bearing:
async/await-style suspension has several logical units share one thread and **share
everything** until an explicit `await` yields; an `isolate` is the opposite — each unit is an
**actual OS thread with its own arena root**, "as if it were a separate program," sharing
*nothing* until an explicit channel moves data across. `corotina`/`coroutine`, `task`, `thread`,
and `actor` were all considered and rejected as names precisely because each one either implies
suspension/shared memory (the wrong model) or names the mechanism instead of the guarantee (a
mechanism the design deliberately keeps free to evolve from 1:1 OS threads to an M:N backing
under the same surface).

The surface is `teko::isolate` — `Isolate`, `spawn`, `join`, `fork_join`,
`hardware_parallelism()` — plus the reserved (not yet built) vocabulary for explicit
cross-isolate sharing: `scope { }`, `chan<T>`, `send`, `recv`. Because every isolate owns its
arena root outright (`tk_region_new(NULL)` at birth, `tk_region_drop` at death), the
process-global arena mark/rewind machinery (`tk_arena_push`/`tk_arena_pop`) that a
shared-root design would need becomes unnecessary rather than merely fixed — the runtime
already provides independent-root regions, threads bottom out in the platform's native thread
primitive, and no additional C is required for this model to hold.

**Grouping isolates under one shared arena domain is deliberately not part of the surface.**
Every named consumer of concurrency in the compiler itself (the test gate, codegen, the
regression runner) is fork-join with disjoint writes and a read-after-barrier — none needs a
shared mutable domain. If a future consumer genuinely needs mutable state shared between
isolates in a shape that doesn't reduce to fork-join, the answer is a **channel**, not a
named group keyed by a bare string (string-keyed grouping without a namespace is exactly the
shape of bug this design has repeatedly paid for elsewhere in the tree).

## Memory diagnostics

Two independent oracles cover memory safety in the shipped toolchain:

- **ASan/UBSan** on the production path, catching classic memory and undefined-behavior bugs.
- **`TEKO_MEM_PARANOID=1`** — Teko's own arena accounting (poison-on-free, never-reuse) is the
  one oracle that can see **arena-level** double-free/use-after-free of *aliased* pointers,
  which vanilla ASan cannot instrument (arena reuse is invisible to it). This mode is
  dev-time-only (it changes allocation behavior) and runs across every published libc/arch
  combination the toolchain ships.
