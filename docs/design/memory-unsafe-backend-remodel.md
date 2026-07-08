# Remodel: Memory Model + `unsafe` + Own Backend — Design Base

**Status:** RATIFIED by the owner 2026-07-06. This document is the base for a **parallel
remodel branch**. It records the decisions from the memory-model / VM / backend discussion so
the branch starts from a settled design and does not re-litigate.

Teko is a **functional, no-GC, systems-capable** language whose apps come **after the compiler is
finished** (which is near). Everything below is scoped to that: the compiler itself is fine today;
the model is for the user programs Teko will host.

---

## 0. The honest framing — what "the memory problem" is, and is not

- **The 1.5 GB was the VM, not the arena.** Isolation (measured): pure codegen ≈ 366 MB; the old
  VM test-gate ≈ 1566 MB (the VM's functional-env O(n) rebuild interpreting the whole `#test`
  corpus in-process adds ~1.2 GB). `#324` flipped the gate VM→native → ~937 MB, VM out of the
  per-PR gate. The compiler's own arena is ~366 MB, a **safe batch leak-to-root** (it exits; the OS
  reclaims). It is NOT a runaway.
- **The 8.5 GB → 293 MB self-host win was already banked** by the arena machinery (free-list reuse +
  first-rung right-sizing + free-old-on-grow, `#148`), **NOT** by scope reclamation — automatic
  region drops free *little* because the return-heavy corpus promotes almost everything to root.
- **So the memory model is for USER apps** (long-lived, functional, eventually concurrent), not the
  batch compiler. The one genuinely-real native gap it must close is the `mem::free` aliased UAF
  (see §5), which is caught by **neither static analysis NOR ASan** (only `TEKO_MEM_PARANOID`).
- For the compiler itself the leak-to-root only bites when compiling a **very large** input (holds
  all ASTs+TASTs); for self-host (moderate) it is fine. The cheap lever there is arena right-sizing
  (366→~300), independent of this remodel.

---

## 1. The recommended hybrid memory model

**`arena` (default) + inferred `spine` + `adopt` (cycles) + `unsafe` (floor). No GC.** One tool per
kind of memory pressure, and the ~95% you actually write stays annotation-free.

| Layer | Role | Build status | Cost |
|---|---|---|---|
| **L0 — bump-arena region tree** | the invisible default for ~95% of code; per-scope, per-request region is the server hatch | **BUILT** | none (leak-to-root is sound) |
| **L1 — the inferred spine** | a bounded per-function points-to/uniqueness fact (upgrade `escape.tks`); makes stored borrows, `mem::free`, and channel-send *sound*; types the `unsafe` boundary on the safe side | **UNBUILT, research-grade** | one hard analysis; **the whole safety bet** |
| **L2 — three static gates** | Ref-storability · affine `free(x)` · channel-send-move, each a local check reading the spine | UNBUILT (gated on L1) | modest; over-rejection is the tax |
| **L3 — reaps + SlotMap idiom** | recycle freed blocks into the bump path (C4 plateau); generational-index collection for graphs | SCAFFOLDING (free-list ships) | small per-alloc scan |
| **L4 — `adopt` region** | bulk-reclaim a cyclic knot on drop, reusing the arena parent/child tree (the C1 closer) | SCAFFOLDING (tree exists; owner-tag new) | 1 word/object + 2 compares/lifetime |
| **L5 — `unsafe` (see §2)** | raw manual allocation, contained by TYPE | SCAFFOLDING | manual safety inside the unsafe surface |

### The honest ceiling
- **Built today: 0 cliffs closed.** The safety story hangs on **one** unbuilt analysis (the spine).
  If the spine can be made sound over the name-based escape pass, it unlocks four guarantees at
  once; if it cannot, it unlocks none. Pretending the fact is free is the only dishonest option.
- **C1 (cyclic reclamation) decision:** accept **leak-to-region by default + `adopt` opt-in
  bulk-drop** as the C1 closer. It is the least-invasive, least-dependent option (owner's criterion).
- **No GC — ruled out.** A "minimal region-GC" does **not** stay minimal: it needs program-wide
  write-barriers (violates no-GC), precise stack maps portable C cannot emit, and its only *sound*
  mode is bulk-drop — which **is** `adopt`. It would cost the hot path and portability and buy
  nothing `adopt` doesn't already give.
- **RC / generational-refs / full borrow-checker** also rejected: RC *is* a GC (per Bacon–Cheng–Rajan)
  and leaks the cyclic cache; gen-refs tax every deref and only *mitigate*; a whole-program borrow
  checker taxes the 95% that has no memory problem (violates "small language").

### The single first move (when the branch starts)
**Audit `escape.tks`** for the confinement direction — can a bounded points-to fact **layer over**
the name-based pass, or must it **replace** it? (`escape.tks:14` "no points-to graph — DEFERRED";
`:284` "no second fixpoint pass".) That decision gates the whole spine, hence L1/L2. `unsafe` (§2,
nominal) and `adopt` (arena-tree) do **not** depend on the spine and can proceed in parallel.

---

## 2. `unsafe` — by TYPE, not block (owner decision)

**`unsafe` is a declaration MODIFIER, never a block.** It applies to:
- a **type** declaration (`unsafe type M = struct | class`), and
- a **namespace function** (`[pub|exp|…] unsafe fn …`) — NOT methods (a method of an `unsafe`
  type inherits the type's unsafe-ness; you don't mark methods individually).

**Rationale (owner):** *if the dev uses unsafe, they assume the risk COMPLETELY, not for a small
isolated "block".* A lexical `unsafe { }` lies ("only these lines are unsafe") while the unsafe data
leaks out of the block; a type/function says the truth — *this whole thing is unsafe, you own it
entirely.* The unsafe-ness is a **grepable, total, assumed property of the structure** (fits law M.3
"honest"), not a hidden trapdoor.

### The rule (contagion on DATA, not on call sites)
- An `unsafe`-typed value may appear **only** in the signature, body, or field of an `unsafe fn` (or
  in another `unsafe type`). **Safe code cannot name an unsafe type** — not as a param, return,
  field, or local.
- **Contagion propagates through COMPOSITION:** a `struct`/`class` that embeds an `unsafe`-typed
  field must itself be `unsafe`. (Otherwise a safe wrapper would leak unsafe data.)
- **Calls are UNCOLORED:** a safe fn may call an `unsafe fn` and vice-versa freely — because only
  **safe types cross the boundary** (a safe caller cannot construct/hold an unsafe argument, so an
  `unsafe fn` exposed to safe code must take/return only safe types; the raw work stays inside).
- **What crosses out of the unsafe surface:** only a by-value safe copy (e.g. `[]byte`) or a
  move-only `Owned<T>` handle — never a raw `unsafe`-typed value.

### The big win: `unsafe` is DECOUPLED from the spine
Because containment is a **nominal type check** (does the type carry the `unsafe` marker? does it
propagate through composition? does a safe context name it?), it needs **no** flow/reachability
analysis. So `unsafe` is the keystone that **ships FIRST**, independent of the (hard, unbuilt) spine.
(This corrects the earlier belief that `unsafe` containment was "a slice of the spine" — that was
true for a *lexical block*, not for a *type marker*.)

### 2c. The `unsafe #must_free` Arena — a dev-controlled manual region (=#358, owner 2026-07-06)

A **manual, non-lexical, unsafe-only** region: the dev creates it, allocates pointers *into* it, and
**bulk-frees the whole region** at a point of their choosing. It is the unsafe, non-lexical complement
to the (lexical, safe) `adopt { }`.

```teko
unsafe #must_free type Arena = ...        // both markers coexist on the TypeDecl (U1 is_unsafe + S2 must_free)
unsafe fn build() -> i64 {
    mut a = Arena::new()                  // tk_region_new (fresh child region)
    let p = a.alloc<Node>(...)            // ptr<Node> attached to a's lifetime
    mem::free(a)                          // bulk-free the whole region (the #must_free consume)
    0
}                                         // dropping `a` without mem::free(a) => COMPILE ERROR
```

The elegance: it **composes the wave's two features, each guarding the half it can**.
- **`#must_free` bars the region leak** — dropping the `Arena` without `mem::free(a)` is a compile
  error (the §5a dataflow). You cannot leak the whole region.
- **`unsafe` contains the residual** — after the bulk-free the pointers into the region are dangling
  (the aliased-UAF the spine cannot track). The dev assumes *exactly* that risk, and only that.

Zero grammar (stdlib type; the markers already parse). `Arena::new/alloc/free` map onto the existing
`tk_region_new/alloc/free` arena tree (the same `adopt` reuses) — expose it, don't build a new allocator.

**The full memory ladder (auto → raw):**

| tool | lifetime | safety |
|---|---|---|
| `arena` default | per-scope (invisible) | safe |
| `#must_free` / `mem::free` / `defer` / `adopt` | dev-controlled release | safe (the "C# `using`/`IDisposable`" tier — **stronger**: `#must_free` *forces* the release, C# only warns) |
| **`unsafe #must_free` Arena** | dev-controlled region, bulk-free | unsafe (leak barred by `#must_free`; dangling owned by `unsafe`) |
| `RawBuf` / `Owned<T>` | raw buffer | unsafe (malloc/free) |

---

## 3. Surface syntax additions (lexer + parser)

**Lexer: ≈ 0 new tokens.** New keywords are **contextual** (Teko already does this for
`from`/`params`/`trait`/`type`/`to`/`in`/`self`/`base`, via `is_name_at`). This is what keeps a
namespace segment named `unsafe`/`raw` legal (a reserved keyword could not be a path segment).

**Parser additions (small):**
1. **Grouped import** — `use <path>::[ A, B, C ]`. A LIST, so `[]` + `,` (Teko invariant: `{}`
   bodies are `;`-separated — struct fields, match arms; `[]` collections are `,`-separated). NOT
   `{ }`. Unambiguous: `::[` is a new sequence (distinct from index `x[i]` — no `::` — and array type
   `[]T` — a prefix). Ergonomics win, independent of memory.
2. **`unsafe`** — a declaration **modifier** joining `pub`/`exp`/`static`/`extern` before `fn`/`type`.
   **No block production.**
3. **`adopt { … }`** — a contextual block, same shape as the existing `defer { }`.
4. **`#must_free`** — a decorator at **declaration** position (`#must_free type DbConn`), reusing the
   existing `#`-decorator machinery (`#singleton`/`#inject`/`#scoped`/`#test`). Never on a `let`.
5. *(optional)* `region <name> { }` — the resettable per-request region, if not folded into `#scoped`.

**NOT surface (checker + stdlib):** `Owned<T>`, `RawBuf` are stdlib generic types (imported via
`use`, zero grammar); the spine is checker inference (zero surface); `Ref<T>`, `mem::free`, `defer`
already exist.

**Naming note:** if the unsafe types live at `teko::mem::unsafe`, `unsafe` must stay contextual (a
valid segment), OR name the namespace `teko::mem::raw` and reserve `unsafe` as a pure modifier.

---

## 4. VM & backend direction

- **Build a new own AOT backend + linker**, to leave C codegen + external `cc`/linker. North-star:
  toolchain independence, compile speed, the bare-metal/OS vision.
- **The VM retires** (eventually). Its roles and their fates:
  - `teko run` (interpret, `driver.tks:207`) — **unused today**; the own-backend's fast AOT replaces it.
  - **REPL** (`teko repl`, `vm::exec_stmt`) — the **one role to decide**: an interactive REPL is
    naturally an interpreter; on an AOT backend it becomes compile-and-run-per-line, or keep a minimal
    tree-walker just for it. **Decide consciously.**
  - **Differential (`diff_vm_native`)** — the VM was "often the wrong side," so the oracle is noisy.
    It **migrates** to **C-backend vs own-backend** (both native, C-backend trusted/self-hosting) — a
    *stronger* oracle that validates the new backend exactly when it is born. Then retire the C-backend.
  - **LSan with per-`#test` arena rewind** — the VM gate's rewind (`tk_arena_push/pop`) is what makes
    leak-detection *meaningful* (LSan sees leaks beyond the rewound set). The native gate runs
    without rewind → the arena holds everything to exit → LSan cannot tell a deliberate hold from a
    real leak (this broke `#327`'s native lane; fixed by `detect_leaks=0` there). **LSan's meaningful
    home is a per-test-rewound run — the nightly VM lane**, if leak coverage is wanted.
  - **No comptime / const-eval dependency** — VERIFIED (`escape`/`fold` in the checker are type-table
    folds, not VM evaluation). This is what **de-risks** retiring the VM: killing it does not break
    *compilation*.
- **The native-ASan gate is a rolling UB audit** of the production native path — it has already
  driven root fixes for the `#291` trait-vtable function-pointer UB, `tk_mul_u16` overflow, and the
  systemic arena `__int128` under-alignment. This hardening is the right prep for a VM-less future
  where native is the sole path.
- **Sequence:** keep C-transpile shipping; build the own-backend validated against it; flip when
  trusted; then retire the VM (or its noisy differential earlier). Do **not** retire the VM
  differential *before* C-vs-own is in place — unless the VM's noise already exceeds its signal.

---

## 5. Pre-branch gate (ratified sequence)

1. **`#327` landed** (arena `__int128` alignment + `tk_mul_u16`) → clean foundation. Release
   `0.0.1.49-alpha` (captures `#324`+`#325`+`#326`+`#327`).
2. **Two de-risk fixes** on main (the last work before the branch): (a) correct the one dishonest
   doc claim — "use-after-free is a static error" is FALSE for `mem::free` (it is caught by neither
   static analysis nor ASan; only `TEKO_MEM_PARANOID`); (b) a **reserved-name guard** so a user fn
   named `run`/`args`/`cwd`/`read_file` is not hijacked by `emit_host_ffi`'s bare-name sniffing
   (a real native-codegen crash — and a real landmine for the own-backend's new code).
3. **Open the parallel remodel branch** off the de-risked main.

**Tracked, NOT pre-branch:**
- **`#301`** (function-typed value in `Ref`/optional/slice does not round-trip; the mangle has no
  `Func` arm for optional/slice inner) — a codegen keystone that gates `flat_map`/function-unions and
  **parks `#184`**. It is **pre-apps** (functional apps store functions), not pre-branch (the
  compiler's own code avoids it). The own-backend's codegen must handle this case.
- **`#283`** (latent `-O2` UB in generated `teko.c`) — **obsoleted by the own-backend** (no C, no
  `zig cc -O2`); the native-ASan rolling audit is also draining this family.
- **`#184` (#300)** — parked, blocked by `#301`; finalizes when `#301` lands.

---

## 5a. S2 `#must_free` — the LOCAL consume-or-fail dataflow's honest first-cut bounds

`#must_free` (issue #336) is a **local, per-binding** dataflow — no points-to/uniqueness fact, so
(mirroring the `mem::free` aliased-UAF note in §5) it is powerless against the ALIASED-FREE
use-after-free (`y = x; free(x); use(y)`), which still needs the spine (`#331`). Implementation
surfaced FOUR further first-cut bounds, recorded here so they are documented, not silently
assumed:

- **Loop bodies are NOT walked.** A `#must_free` binding declared inside a `loop { }` body is not
  checked at all — a leak there compiles silently today. The dataflow only walks straight-line
  blocks and `if`/`match` branches reached from a tracked binding's own declaration point; it
  never recurses into a `TLoopStmt`'s body. Closing this is a scope WIDENING (loop-body-aware
  walking), not a bug fix — deferred, tracked alongside the spine work.
- **A `#must_free` PARAM can only be consumed by `return h`.** A parameter is always immutable
  (B.21), and `teko::mem::free` requires a `mut` target — so a received handle can never reach
  `mem::free` directly; only a move-out via `return` consumes it. Consuming a param by moving it
  into a callee (rather than returning it) needs general move-tracking, which is the spine's job
  (`#331`), not this local check's.
- **Reassigning a `mut #must_free` local leaks the overwritten value.** `mut h = make(); h =
  make()` (an unfreed `h` overwritten before any free) is NOT caught — the dataflow tracks
  whether `h`'s NAME is eventually consumed somewhere in its scope, not whether every VALUE ever
  bound to it was. General move/assignment tracking is again the spine's job (`#331`).
- **`must_free` is not yet serialized in the `.tkb` codec** (it mirrors the pre-existing
  un-serialized `di_kind` gap there) — so the constraint is currently MODULE-LOCAL: a `.tkb`-read
  `TypeDecl` always carries `must_free = false`. A `.tkl` package boundary must not yet rely on
  `#must_free` surviving a `.tkb` round-trip; wiring the codec is a follow-up.

---

## 6. The one-paragraph summary (for whoever opens the branch)

Keep the arena as the invisible default; do not add a GC. Build the **spine** (a bounded inferred
points-to fact over `escape.tks`) as the load-bearing safety bet — but **audit `escape.tks` first**
to learn whether it layers or replaces. `unsafe` is a **type/function modifier** (full risk
ownership, nominal containment, contagious by composition) and ships **first**, independent of the
spine. `adopt` is the opt-in cyclic-reclamation closer, reusing the arena tree. The surface cost is
tiny (contextual keywords + a few declaration modifiers + `use path::[…]`). In parallel, build the
**own AOT backend + linker**; validate it against the trusted C-backend (the differential migrates
there from the VM); retire the VM when its remaining roles (REPL, LSan-with-rewind) are decided.
