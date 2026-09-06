# Plano §16 — ARENA-OVER-MMAP keystone (the hand-written C arena → Teko over raw mmap/munmap)

Status: DESIGN (architect). Read-and-design ONLY — no product code written here.
Base: `origin/fix/retirement` HEAD `08e61e4b`, seed `1b61761d`.
Owner mandate (`mudancas-superficie-0.3.1.md` §11.2, "VIRADA §16" + "TROCAR TODAS as deps C,
inclusive a arena, por Teko+syscall"): the arena allocator (`tk_region_*`/`tk_alloc`/`malloc`)
becomes Teko over raw `SYS_mmap`/`SYS_munmap`. This is §16-core — the deepest compiler-touching
change in the project, because it swaps the compiler's OWN runtime memory management.

Builds on the LANDED syscall keystone (`fb0ec8c7`/`1a03a68e`, doc `plano-s16-syscall-intrinsic.md`):
`teko::sys::syscall0..6`, `ptr_word`, `ref_word`; `SYS_WRITE`/`SYS_EXIT_GROUP`/`SYS_CLOCK_GETTIME`/
`SYS_GETRANDOM`. Supersedes the **libc** framing of `docs/design/arena-em-teko.md` (the vagão-20
cargo): that doc designed the arena data-structures + the 12-function map + a FULL reference impl
(`examples/probes/arena_teko`, native-leg) sitting on `c_aligned_alloc`/`c_free` (libc). The VIRADA
ruling BANNED `from "c"`. This doc REUSES that data-structure design verbatim and swaps the libc
"fundo" for `mmap`/`munmap` syscalls, then designs the part `arena-em-teko.md` explicitly did NOT
do: the actual landing into `src/` and the switch-over of the emitted compiler image.

---

## §0 — What the current C arena IS (the thing we replace)

`src/runtime/teko_rt.c` `Arena allocation (S1)` block (~:1219-3100). A bump allocator over a
per-region LIFO chunk list; the ONLY libc dependency underneath is the CHUNK BACKING STORE and the
REGION/ENTRIES header allocation:

- `tk_chunk_alloc(bytes)` → `posix_memalign(16)` / `_aligned_malloc` (the sole page source).
- `tk_chunk_free(c)` → `free` / `_aligned_free`.
- `tk_region_new_on` → `malloc(sizeof struct tk_region)` (a 64-ish-byte header).
- `tk_region_register`/entries growth → `malloc`/`realloc`/`free` of the `(type_id, instance)` array.
- Region header struct: `struct tk_region { head; reg_next; parent; entries; nentries; entries_cap;
  gen }`. Chunk header: `struct tk_chunk { next; cap; used; _Alignas(16) data[] }`, `offsetof(data)=32`.

The emitted C **call surface** codegen depends on (the symbols the switch-over must preserve or
retarget — ~80 literal call-sites in `codegen.tks`, ~262 in `teko_rt.c`):

| symbol | shape | role |
|---|---|---|
| `tk_region_alloc(r, n)` | `(tk_region*, size_t) -> void*` | THE bump allocation (hottest) |
| `tk_alloc(n)` | `(size_t) -> void*` | root-region convenience (`tk_region_alloc(current, n)`) |
| `tk_region_new(parent)` | `(tk_region*) -> tk_region*` | fresh region |
| `tk_region_drop(r)` | `(tk_region*) -> void` | bulk-free a region |
| `tk_region_drop_subtree(root)` | `(tk_region*) -> void` | `adopt` bulk-drop over `parent` tree |
| `tk_region_root()` / `tk_region_current()` / `tk_region_program()` | `() -> tk_region*` | region getters |
| `tk_region_enter(c)` / `tk_region_leave()` | current-region stack | move-on-return |
| `tk_arena_push()` / `tk_arena_pop()` / `tk_arena_commit()` | checkpoint stack | test-gate rewind |
| `tk_region_register(r,tid,inst)` / `tk_region_lookup(r,tid)` | DI registry | `svc` scopes |
| `tk_regions_free_all()` / `tk_region_*_u(...)` | teardown + u64-handle twins | epilogue / native |

Per-task seat (`struct tk_task`, `_Thread_local tk_g_current_task`, `tk_task_current()`): F1 already
moved the arena roots (`regs`, `root`, `arena_marks`, `free_bins`, `cur_regions`, `push_cache`, gen
counter) INTO the task, reached through ONE seam. The F1 doc-comment already declares that seam is
the sole thing the native runtime re-encarnates (pthread_getspecific behind an `extern fn`). **We
exploit that: the seam is the maintained-C anchor that solves P2 (below) without new language surface.**

---

## §1 — THE BOOTSTRAP CIRCULARITY (solved first)

The emitted C calls `tk_region_alloc()` for EVERY Teko allocation. If the arena is reimplemented in
Teko, that Teko code — once compiled to C by our own backend — is itself in `teko.c`, and any Teko
construct in it that lowers to an allocation would call the arena to allocate ITSELF. Infinite
regress / undefined bootstrap. The break is the same one `arena-em-teko.md` §2 identified and the
same the C twin uses: **the arena keeps its entire state INSIDE the raw memory it administers**, read
and written as words at computed addresses, allocating NOTHING through the arena.

### 1.1 The allocation-free arena core — the discipline

The arena core is authored in a **restricted Teko dialect**. Everything is `u64`/`i64` addresses +
integer arithmetic + three raw primitives + syscalls. Concretely:

- **Backing store**: `SYS_mmap` returns a page-aligned address as `i64`; `SYS_munmap` releases it.
  This is the ONLY memory source — it replaces `posix_memalign`/`malloc`/`free` wholesale.
- **State access**: every header field (chunk `next`/`cap`/`used`; region `head`/`reg_next`/`parent`/
  `entries`/`nentries`/`entries_cap`/`gen`; the CONTROL block; free-list nodes; marks; bins) lives at
  a computed address and is read/written with `teko::mem::load_u64(addr)` / `store_u64(addr, v)` —
  ONE `LLoad`/`LStore` each, provably NON-allocating (arena-em-teko.md §2 P1).
- **Address carrier**: `u64` (NOT `uptr`; the checker gives `u64` bump arithmetic and `u64 == void*`
  on 64-bit — `arena-em-teko.md` §2.1). The incoming `ptr` region-handle is bridged to `u64` with
  `ptr_word` (landed); the outgoing allocation address is bridged `u64 -> ptr` with `word_ptr` (NEW,
  §2). Alignment rounding, chunk-fit, free-bin index — all plain `u64` arithmetic.

### 1.2 FORBIDDEN inside the arena core (anything that lowers to an allocation)

The implementer MUST NOT use, anywhere in the arena core module:

- `str` construction / interpolation / `$"…"` / concatenation (each bump-allocates a buffer).
- `[]T` slice literals or slice growth (`teko::list::push`, index-append — `tk_slice_*` / `tk_alloc`).
- `[]byte`/`Buf` built via `teko::mem` (`buf_ptr`/`bytes_from_ptr` — the latter allocates its return).
- struct-init that auto-boxes a recursive value-type back-edge, or class instantiation (arena-per-
  object → `tk_region_alloc`), or closures (env box → `tk_region_alloc`).
- `match` producing a boxed/fat value; anything returning `str | error` (the error path allocates).
- `panic(msg)` with a computed `str` (allocates + native-leg gap). Failures are RAW: an OOM in the
  arena calls the maintained-C `tk_panic("out of memory")` seam (M.1) via an `extern fn`, OR — cleaner
  — issues `SYS_write` of a static message + `SYS_exit_group`. RECOMMEND the latter (fully syscall).

ALLOWED: `u64`/`i64`/`bool` locals and params, integer/bitwise/compare ops, `if`/`loop`/early-return,
plain fn calls WITHIN the core, and the primitives `syscallN` / `ptr_word` / `word_ptr` / `load_u64` /
`store_u64`. This is exactly the dialect the `arena_teko` probe already proved compiles and runs
(native leg) — the design is validated; only the "fundo" (libc→mmap) and the leg (native→C) change.

### 1.3 P2 — the one mutable process word (the CONTROL anchor)

`tk_alloc` is called from generated code carrying NO state, yet the arena needs process roots
(`g_root`, `g_regs`, gen counter, mark stack, free bins). `arena-em-teko.md` §2 P2 collapses this to
**exactly ONE word**: a `CONTROL: u64` slot holding the address of a control block; everything else
lives inside that block (layout §3.4 of that doc: magic, g_regs, g_root, g_region_gen, arena_msp,
paranoid, free_large, marks[64], free_bins[4096] — one ~33 KB block, mmap'd once on first touch).

**RESOLUTION (law-first, SAFEST): reuse the existing F1 seam — no new language surface.** `teko_rt.c`
is the maintained-C exception (standing law). The F1 doc-comment already names `tk_task_current()` as
"the whole seam — one function, two encarnations." Add ONE field `uint64_t arena_control;` to
`struct tk_task` (or a `_Thread_local uint64_t`) and expose two trivial maintained-C accessors bound
as `extern fn`:

```
/**
 * tk_arena_control_get — read this task's arena CONTROL-block address (0 until first init). The
 * ONE mutable process word P2 requires (arena-em-teko.md §2), kept in the maintained-C F1 seam
 * (`tk_task`) so the Teko arena needs NO module-mutable-state language surface. Allocation-free.
 * @return  the CONTROL address, or 0 if the arena is not yet initialized on this task
 */
pub extern fn tk_arena_control_get(): u64 = "tk_arena_control_get" from "teko_rt"
/**
 * tk_arena_control_set — install this task's arena CONTROL-block address (once, lazily, by the Teko
 * arena's first-touch path). The write half of the P2 seam. Allocation-free.
 * @param addr  the mmap'd CONTROL-block address to record
 */
pub extern fn tk_arena_control_set(addr: u64) = "tk_arena_control_set" from "teko_rt"
```

This rides the maintained-C exception exactly as F1 designed, sidesteps the module-mutable-word
language feature (`LGlobalAddr`/`.bss`, a real surface addition — DEFERRED to its own future crumb,
not on this critical path), and preserves per-task correctness (F1 soundness) for free. The purist
"fully-Teko module-mutable word" path is noted as a later Doc-1/surface improvement, NOT a blocker.

---

## §2 — THE ptr↔i64 INTRINSIC GAP (word_ptr + the C-leg load/store gap)

### 2.1 `word_ptr` — the INVERSE of `ptr_word` (NEW intrinsic)

`ptr_word(p: ptr): i64` (landed) bridges an existing pointer → integer. The arena bumps a cursor in
`u64` and must hand each allocation back AS a `ptr` (so codegen's existing `(uint8_t *)tk_region_alloc(
…)` cast and the region-handle ABI keep compiling). It needs the inverse:

```
teko::sys::word_ptr(w: i64): ptr          // (or u64 param — see below)
```

- **Lowering (C leg)**, mirroring `emit_ptr_word` (`codegen.tks:4575`): a new `emit_word_ptr` emitting
  `((void *)(uintptr_t)(<w>))`. Special emitter in the `as_cstr`/`ptr_word` block (`codegen.tks:5071`),
  NOT a bare name-substitution (it needs the cast).
- **Checker**: register in `scope.tks::builtin_fn` next to `ptr_word` — `Func { params = <[i64]>;
  ret = Ptr { inner = null } }`. The returned opaque `ptr` widens to any `ptr<T>` at the use-site via
  the existing `ptr_widens_to_opaque` path, exactly as `buf_ptr`'s result does.
- **`cast_check` won't fight it**: like `ptr_word`/`ref_word`/`f64_bits`, it is a COMPILER INTRINSIC,
  not a user `to` cast — the sanctioned reinterpret carve-out. `cast_kind` is never consulted (there is
  no `to`); the value is produced by the emitter directly. The opaque-ptr law (`ptr_opaque_error`)
  stays intact (no surface `u64 -> ptr` cast is introduced — only the intrinsic).
- **Native leg**: HONEST-STOP now (falls into `lower_call`'s terminal `_ =>` "not yet lowered (N2)"),
  exactly as `ptr_word`/`ref_word` do; the native lower is Doc-2 terminal.
- Param type: RECOMMEND `i64` to mirror `ptr_word`'s `i64` return symmetrically (round-trips
  `word_ptr(ptr_word(p)) == p`). The arena does its arithmetic in `u64` and converts `u64 -> i64` (a
  sanctioned `Prim` cast) at the boundary; or ship the param as `u64` if that reads cleaner against the
  `load_u64`/`store_u64` family — either is fine, pick one and be consistent. `teko::sys` placement
  (next to `ptr_word`) per the `f64_bits`-precedent (a reinterpret intrinsic need not live in `mem`).

### 2.2 THE C-LEG LOAD/STORE GAP (the true first blocker — flag)

`teko::mem::load_u64`/`store_u64` (P1) are registered in `scope.tks::builtin_fn` AND lowered in
`src/lir/lower.tks` (`is_load_u64_call`/`is_store_u64_call` → `LLoad`/`LStore`) — **but ONLY on the
native leg.** `codegen.tks` (the C leg) has NO emitter for them (verified: zero matches in
`codegen.tks`). Because the arena core is compiled by the **C backend** into `teko.c` (self-image +
local validation are `TEKO_BACKEND=c`), the arena core cannot even COMPILE until the C leg lowers
load/store. This is a HARD PREREQUISITE — the `arena_teko` reference impl ran native-only, which is
why the gap was invisible.

Fix (its own sub-crumb, §7 crumb B), mirroring `ptr_word` exactly:
```
emit_load_u64:  ((uint64_t)*(volatile uint64_t *)(uintptr_t)(<addr>))
emit_store_u64: (*(volatile uint64_t *)(uintptr_t)(<addr>) = (uint64_t)(<v>))    // as a void-expr stmt
```
`volatile` for the same reason the syscall helpers clobber `"memory"`: the arena reads back words a
syscall (mmap) or a sibling store just wrote; the optimiser must not elide/reorder them across the raw
address it cannot alias-analyze. Register as special emitters in the `teko::mem` block (`codegen.tks
:5067` neighbourhood), dispatched by bare last segment `load_u64`/`store_u64`.

### 2.3 The mmap syscall numbers + flags (`teko::sys`)

`mmap(addr, length, prot, flags, fd, offset)` = `syscall6`; `munmap(addr, length)` = `syscall2`.
Numbers are `#arch`-differentiated (as the existing `SYS_*` are); PROT_*/MAP_* flags are the Linux
asm-generic values, identical on x86_64 and aarch64 (`asm-generic/mman-common.h`), so `#os("linux")`
without `#arch`. Add to `src/sys/sys.tks` (const-only — LEAF per ACHADO A, no reseed):

| const | x86_64 | aarch64 | note |
|---|---|---|---|
| `SYS_MMAP: i64` | 9 | 222 | `#arch`-split blocks |
| `SYS_MUNMAP: i64` | 11 | 215 | `#arch`-split blocks |
| `PROT_READ: i64` | 1 | 1 | `#os("linux")` |
| `PROT_WRITE: i64` | 2 | 2 | `#os("linux")` |
| `PROT_NONE: i64` | 0 | 0 | (completeness) |
| `MAP_PRIVATE: i64` | 2 | 2 | `#os("linux")` |
| `MAP_ANONYMOUS: i64` | 0x20 (32) | 0x20 (32) | generic value both arches |
| `MAP_FIXED: i64` | 0x10 (16) | 0x10 (16) | (for the P2-fallback / future) |

Anonymous private mapping = `flags = MAP_PRIVATE | MAP_ANONYMOUS = 0x22`; `prot = PROT_READ |
PROT_WRITE = 3`; `fd = -1`; `offset = 0`. mmap returns the address (page-aligned, so ≥16-aligned — it
SUBSUMES the `TK_ARENA_ALIGN=16` guarantee posix_memalign gave, no explicit align needed) or a value
in the error band `[-4095, -1]` (= `-errno`) on failure. munmap returns `0` / `-errno`.

---

## §3 — THE META-POOL (the mmap-specific memory-correctness keystone — NOT in arena-em-teko)

**This is the one place the mmap swap is NOT a mechanical substitution of `arena-em-teko.md`, and it
is a correctness/footprint keystone.** That doc allocated each region header (64 B) and each entries
array via a separate `c_aligned_alloc`. `malloc` sub-allocates fine at 64 B. **`mmap` cannot**: the
kernel's minimum mapping is one page (4096 B), so one mmap-per-64 B-header is a **64× footprint
blow-up** — fatal against the 6 GB cap (self-build region churn is large; MEM_PARANOID already peaks
~5.6 GB). Chunks are fine (65536 B = 16 pages), but headers/entries/CONTROL/free-nodes are NOT.

Design: a **META-POOL** — a small-object bump-sub-allocator over mmap pages, dedicated to the
fixed-size metadata (region headers = 64 B; entries arrays; the CONTROL block; the arena's own
internal bookkeeping). It:
- mmaps a slab (e.g. 64 KB) and bump-allocates aligned 64 B (and doubling-array) cells from it,
  growing by another mmap slab when full (a slab free-list threads the slabs);
- maintains a fixed-size **free-list of 64 B header cells** (a single-linked list threaded through the
  freed header's first word) so `tk_region_drop` returns the header for reuse — mirroring `malloc/free`
  footprint behavior so the flip does NOT grow steady-state memory. Entries arrays (rare, small)
  likewise park into size-binned free slots or leak-bounded (they are already O(regions) tiny).

This keeps the metadata footprint bounded to ~the malloc baseline. Chunk payload continues to come
from its own `mmap` (one mapping per chunk, munmap'd on drop). The volume gate (§6, promoted from
`arena_teko`'s `volume_gate`) must be extended to assert META-POOL reuse (a drop-then-new region
reuses the SAME header address) — otherwise a header leak silently bloats the self-build past the cap.

---

## §4 — THE SWITCH-OVER

Two questions: (a) how codegen STOPS emitting the C `tk_region_alloc` and starts routing to the Teko
arena; (b) the naming.

### 4.1 ABI shape — keep the emitted call-sites UNCHANGED

The Teko arena fns are authored to **signature-mirror the current C ABI** using `ptr` + `u64`:
```
region_alloc(r: ptr, n: u64): ptr          // C: void* tk_region_alloc(void*, uint64_t)
region_new(parent: ptr): ptr
region_drop(r: ptr)
region_root(): ptr ; region_current(): ptr ; region_program(): ptr
arena_push() ; arena_pop() ; arena_commit()
region_register(r: ptr, tid: u64, inst: ptr) ; region_lookup(r: ptr, tid: u64): ptr
```
`ptr` lowers to `void*`; `u64` to `uint64_t`. `void*` is ABI-compatible with the existing call-site
literals both ways (`tk_region *`↔`void*`, `uint8_t *`↔`void*` implicit in C). So the ~80 emitted-C
literals in `codegen.tks` need NO textual change at the call site — only the DEFINITION provider flips.
Inside each fn body: `ptr_word(r)` → the `u64` the arithmetic uses; `word_ptr(addr)` → the `ptr`
returned. `size_t n` becomes `u64 n` (identical on LP64).

### 4.2 Naming / provider flip — RECOMMEND codegen-retarget (task option b)

The emitted symbol from a Teko fn is its MANGLED name (e.g. `tk_t_teko__rt__arena__region_alloc`),
NOT `tk_region_alloc`. Two ways to make the emitted call resolve to the Teko body:

- **(a) symbol-alias**: give the Teko arena fns the exact C export names `tk_region_alloc` etc. Fragile
  — Teko has an `extern fn … = "sym"` for CALL binding but no ratified DEFINITION-export-name surface;
  and Defect #4 of `arena-em-teko.md` (a user fn named like an injected builtin — `arena_push`/
  `arena_pop`/`arena_commit` — is SILENTLY replaced by the builtin, `builtin_fn` matching on last path
  segment) makes those three names actively MINED. REJECT.
- **(b) codegen-retarget (RECOMMENDED)**: the ~15 emitted symbol literals live behind a SINGLE codegen
  constant table (`cg_arena_sym(kind)` returning `"tk_region_alloc"` today). Flip that table to emit
  the Teko arena fns' mangled symbols. One localized change; no export-name surface; sidesteps Defect
  #4 (the Teko fns get compiler-unique namespaced names like `teko::rt::arena::region_alloc`, which do
  NOT collide with the `arena_push`/… builtin last-segments IF namespaced away from those bare names —
  and the switch also RETIRES the `arena_push`/`arena_pop`/`arena_commit`/… builtin injections, closing
  Defect #4 permanently). RECOMMEND: introduce the sym-table indirection in a PRIOR no-op crumb (emit
  the SAME `tk_*` strings — byte-identical, pure refactor, provable), then the flip is a one-line table
  change.

### 4.3 The circularity, re-confirmed at the switch

Once compiled, the Teko arena fns ARE in `teko.c` — but their BODIES are the §1 allocation-free
dialect (only mmap/load/store/word_ptr/ptr_word), so NONE of them re-enters the arena. They bootstrap:
`region_root()` lazily mmaps the CONTROL block + first region via the META-POOL, all through syscalls
and raw word writes. No `tk_region_alloc` appears in the arena's own emitted body. Verified-shape by
the `arena_teko` probe (same dialect, native leg).

---

## §5 — THE FIXPOINT / RESEED SHAPE (the scary part) — a deliberate MULTI-STEP LADDER

The arena backs the compiler's OWN runtime, so the flip changes how the compiler manages memory while
it compiles itself. NOT a clean single-shot. Design the SAFEST landing as an add-alongside → prove →
flip → delete ladder, each step its own reseed + full ritual gate:

- **L0 (refactor, no behavior)**: introduce the `cg_arena_sym(kind)` indirection emitting the SAME
  `tk_*` strings. Byte-identical corpus emit? No — ACHADO A: adding the fn shifts temp-var IDs →
  RESEED, but behavior-inert (emitted `teko.c` for the corpus is textually identical modulo the ID
  shift; the arena still C). Clean reseed.
- **L1 (add-alongside)**: land `src/runtime/arena.tks` (the Teko arena + META-POOL) as NEW functions,
  the mmap consts, `word_ptr`, and the C-leg load/store emitters. Codegen STILL calls the C
  `tk_region_alloc`. The Teko arena is COMPILED into the self-image but UNUSED by the corpus (proven
  instead by the promoted `arena_teko`/`arena_mmap` regression on the C leg). RESEED (new fns), but the
  compiler still runs on the C arena → memory behavior unchanged, low risk.
- **L2 (FLIP)**: `cg_arena_sym` retargets to the Teko arena symbols; retire the `arena_push`/… builtin
  injections. NOW the compiler's own runtime IS the Teko-over-mmap arena. Load-bearing reseed.
  - **Fixpoint expectation: CLEAN 3-gen (tc1==tc2==tc3).** Emitted TEXT depends only on compiler LOGIC,
    not on which correct allocator backs it (allocation is behavior-transparent: same distinct-pointer,
    same bump/rewind semantics — the `arena_teko` volume gate proves chunk-packing identity vs the C
    rule). So gen0 (C-arena binary) emits tc1.c (routes-to-Teko-arena); tc1 (Teko-arena binary) emits
    tc2.c; if the Teko arena is byte-behavior-identical, tc1.c == tc2.c == tc3.c. Gate HARD on this.
  - If a subtle divergence makes tc1 ≠ tc2 (a real behavior delta in the arena), that is a BUG in the
    arena, surfaced by the fixpoint — NOT an acceptable ladder. HALT and fix; do not reseed a
    non-converged arena (a subtly-wrong arena corrupts every emitted program incl. the compiler —
    `arena-em-teko.md` §6 risk).
- **L3 (C-symbol deletion)**: §6, separate crumb.

### 5.1 MEMORY RISK (flag, top severity)

MEM_PARANOID already peaks ~5.6 GB against the 6 GB cap (the syscall-keystone landing noted a first-run
"out of memory (str concat)" that passed on re-run — pressure at the cap edge). The flip's risks:
1. **META-POOL correctness (§3)** — a header leak or one-mmap-per-header blows past 6 GB → gate OOM.
   The volume gate MUST assert header reuse. THIS is the single most likely failure.
2. **mmap page granularity** — chunk payloads round to 4 KB pages; a region that made many <4 KB chunks
   under malloc now consumes whole pages. Default chunk is 64 KB (16 pages) so shared bump-fill
   dominates and this is negligible, BUT a pathological many-tiny-region pattern could grow RSS. The
   volume gate's footprint comparison vs the C rule catches it.
3. **Probe-build OOM** — the 21 MB `cc` OOMs with `-g` at this cap. Per the standing law: pure
   read/design here; if the implementer probe-builds, drop `-g`, kill strays, one build at a time. Do
   NOT run MEM_PARANOID and a parallel build together at L2.

---

## §6 — C-SYMBOL DELETION (C7, two-legs) — L3, its OWN crumb

After L2 lands and the 3-gen fixpoint + MEM_PARANOID + full tree are green, DELETE from `teko_rt.c`:
`tk_region_alloc`, `tk_alloc`, `tk_region_new`/`_on`, `tk_region_drop`/`_subtree`, `tk_region_root`/
`_current`/`_program`, `tk_region_enter`/`_leave`, `tk_arena_push`/`_pop`/`_commit`,
`tk_region_register`/`_lookup`, `tk_regions_free_all`, `tk_chunk_*`, and the `malloc`/`posix_memalign`/
`free` they used — PLUS the now-dead free-bin/mark/canary machinery. KEEP (maintained-C exception): the
`tk_task`/`tk_task_current` seam, the new `tk_arena_control_get/set` anchor, `tk_panic`, and the `_u`
handle twins IF the native leg still needs them. This is the C7 "symbol deletion after reseed"
discipline — NEVER folded into the migration crumb (the two-legs rule): a separate reseed proves the
symbols are genuinely unreferenced. Do NOT delete before L2 is proven; the C arena is the fallback if
L2's fixpoint fails.

---

## §7 — SUB-CRUMB DECOMPOSITION (ordered)

Each is independently gate-able. Verdict = leaf / reseed. Proof = the fixture that asserts behavior.

- **A — `teko::sys` mmap surface (LEAF, no reseed).** `SYS_MMAP`/`SYS_MUNMAP` (`#arch`) + `PROT_*`/
  `MAP_*` (`#os`) consts in `sys.tks` (const-only → LEAF per ACHADO A). Proof: fixture
  `examples/regressions/sys_mmap/` — mmap one anon page, check the return is NOT in `[-4095,-1]`,
  munmap it, check `0`; exit 0. Uses ONLY the landed `syscall6`/`syscall2` (no load/store/word_ptr
  needed yet), so it is fully C-leg-validatable TODAY. **← implementer-ready spec below.**
- **B — C-leg `load_u64`/`store_u64` emitters (RESEED, inert).** `emit_load_u64`/`emit_store_u64` in
  `codegen.tks` (§2.2), `volatile`. Extends fixture A to store a word into the mmap'd page and load it
  back identical before munmap (exit 0). Corpus doesn't call them → behavior-inert reseed.
- **C — `word_ptr` intrinsic (RESEED, inert).** `emit_word_ptr` + `scope.tks` signature (§2.1). Native
  honest-stop. Proof: a fixture round-trips `word_ptr(ptr_word(as_cstr("x"))) `→ writes 'x' via
  SYS_write through the recovered ptr (exit 0). Inert for corpus.
- **D — the Teko arena core + META-POOL (RESEED, add-alongside = L1).** `src/runtime/arena.tks` (the 12
  fns + free-list + META-POOL §3), authored in the §1 dialect over mmap. NOT wired. Proof: promote
  `examples/probes/arena_teko` to `examples/regressions/arena_mmap/` RE-TARGETED to the C leg + mmap
  fundo, replaying its group-0..5 behavior asserts (distinct 16-aligned ptrs; >64 KB exclusive chunk;
  drop idempotent; subtree drop; register/lookup; free-list ceil/floor; MEM_PARANOID poison) PLUS the
  new META-POOL header-reuse assert. Exit 42 on all-pass.
- **E — the switch-over (RESEED, load-bearing = L0 then L2).** L0 sym-table refactor (inert reseed),
  then L2 flip + retire `arena_push`/… builtins. RITUAL POINT: full 3-gen fixpoint (expect
  tc1==tc2==tc3) + MEM_PARANOID exit 0 + full tree + `provenance_gate.sh`.
- **F — C-symbol deletion (RESEED, C7 two-legs = L3).** §6.
- **wrap-refcount (Doc-2 escape-hatch) — DEFER to its OWN sub-crumb AFTER E.** Per §11.2 it is Doc-2
  terrain (deep-copy is the default; wrap-refcount the user escape-hatch, a `Dictionary<addr,count>`
  in the ROOT region — new machinery, NOT reused). The arena keystone is its FOUNDATION (root-region +
  drop semantics), so it FOLDS IN only after the arena is Teko. State: DEFERRED, named, not on this
  ladder's critical path. Its checker generalizes the landed `escape.tks` (frame-local vs promoted) to
  cross-arena (refcounted) — same family, confirming the meta-principle; but it is a separate crumb.

### 7.1 IMPLEMENTER-READY SPEC — sub-crumb A (`sys_mmap`)

Add to `src/sys/sys.tks`, each a full-Javadoc `pub const` transcribed from the kernel ABI (never a C
header), matching the existing `SYS_*` block style: `SYS_MMAP` (`#os("linux") #arch("x86_64")` = 9;
`#arch("aarch64")` = 222), `SYS_MUNMAP` (x86_64 = 11; aarch64 = 215), and `#os("linux")` flag consts
`PROT_READ=1`, `PROT_WRITE=2`, `PROT_NONE=0`, `MAP_PRIVATE=2`, `MAP_ANONYMOUS=0x20`, `MAP_FIXED=0x10`,
all typed `i64`. Create `examples/regressions/sys_mmap/` mirroring `sys_exit_group` layout: a LOCAL
`src/sys/sys.tks` mirror of the mmap block (an external project cannot see compiler-internal
`teko::sys`), `sys_mmap.tkp` (`kind = "binary"`), and `sys_mmap.tkr`
(`Then it exits 0`). `main.tks` (full doc-comment on the entry expr):
```
teko::sys::syscall6(sys::SYS_MMAP, 0, 4096, sys::PROT_READ | sys::PROT_WRITE,
                    sys::MAP_PRIVATE | sys::MAP_ANONYMOUS, 0 - 1, 0)   // -> addr or -errno
```
bind to a `var a`; guard `if a >= (0 - 4095) && a <= (0 - 1) { exit(1) }` (error band → fail); then
`teko::sys::syscall2(sys::SYS_MUNMAP, a, 4096)` → `var u`; `if u != 0 { exit(2) }`; `exit(0)`. The
syscall INTRINSIC is the `teko`-rooted builtin (`teko::sys::syscall6`, resolved by
`builtin_qualifier_ok`); the NUMBERS/flags come from the bare local mirror (`sys::SYS_MMAP`, …).
**Expected native exit code: 0.** Validation = COMPILE only (`--no-verify --release`,
`TEKO_BACKEND=c`, `ulimit -v 6291456`), then run + read `$?`. NEVER `teko test .`. This crumb is a
true LEAF (const-only + fixture) — no reseed — and is the safest possible first step.

---

## §8 — RISKS + LAW TENSIONS

- **META-POOL / mmap page granularity (§3) — TOP RISK.** One-mmap-per-header = 64× blow-up → 6 GB-cap
  OOM. RESOLUTION: the META-POOL sub-allocator + header free-list (§3); the volume gate asserts reuse.
  Not a law tension — an engineering keystone that MUST land with the arena, flagged loudest.
- **C-leg load/store gap (§2.2).** P1 landed native-only; the arena can't compile on the C leg without
  it. RESOLUTION: crumb B, sequenced before D. Verified empirically (zero `load_u64` in `codegen.tks`).
- **`word_ptr` is a new opaque→ptr reinterpret.** Tension with the opaque-ptr law. RESOLUTION
  (law-first): COMPILER INTRINSIC, not a `to` cast — the ratified `f64_bits`/`ptr_word`/`ref_word`
  carve-out. No new law. Rides precedent. NO HALT.
- **P2 mutable process state.** RESOLUTION: reuse the maintained-C F1 seam (`tk_task` + two accessors),
  the standing-law `teko_rt.c` exception — NO new module-mutable-word surface on the critical path.
- **Defect #4 (builtin-name capture).** `arena_push`/`arena_pop`/`arena_commit` are mined last-segments
  (`arena-em-teko.md` §8.4). RESOLUTION: codegen-retarget (§4.2 option b) + RETIRE those builtin
  injections at L2; the Teko arena fns live under a namespaced path that does not collide.
- **Fixpoint non-convergence at L2.** A subtly-wrong arena passes fixtures yet corrupts emit → tc1≠tc2.
  RESOLUTION: the 3-gen fixpoint IS the detector; HALT + fix, never reseed a non-converged arena. The C
  arena stays as fallback until L2 is proven (deletion is L3).
- **atexit / leak-clean teardown** (`arena-em-teko.md` §6). The C uses `atexit(tk_regions_free_all)`;
  Teko can't pass a fn ptr to atexit without new surface. RESOLUTION: the generated `main` epilogue +
  the `exit`/`panic` termination points call `regions_free_all` (already choke-points in `teko_rt.c`);
  or keep the `atexit` registration in the maintained-C seam. Note, not a blocker.

No genuine UNRESOLVED tension → **NO HALT.** Every decision is ratifiable law-first on the `f64_bits`/
syscall-intrinsic precedent + the maintained-C `teko_rt.c` exception + the ACHADO-A reseed rule. The
design is fully drafted; what remains genuinely BLOCKED is nothing — crumb A is buildable today; B/C/D
depend only on landed intrinsics + this design; the wrap-refcount escape-hatch is DEFERRED by ruling
(Doc-2, own crumb), not blocked.

---

## Section index

- §0 The current C arena (call surface + F1 seat we replace)
- §1 The bootstrap circularity (allocation-free core; forbidden constructs; P2 via the F1 seam)
- §2 The ptr↔i64 gap (`word_ptr` NEW; the C-leg load/store gap; mmap numbers/flags)
- §3 The META-POOL (the mmap-specific footprint keystone — new vs arena-em-teko)
- §4 The switch-over (ptr/u64 ABI-mirror; codegen-retarget; circularity re-confirmed)
- §5 The fixpoint/reseed ladder (L0 refactor → L1 add → L2 flip → L3 delete; memory risk)
- §6 C-symbol deletion (C7 two-legs, L3)
- §7 Sub-crumb decomposition A–F + wrap-refcount defer; implementer-ready spec for A
- §8 Risks + law tensions (no HALT)
