// src/runtime/teko_rt.h   (namespace 'teko::runtime')
// libteko_rt: runtime for GENERATED Teko programs (M.1 fail-loud).
// Distinct from the compiler's own src/core.h; self-contained, libc-only.
#ifndef TEKO_RT_H
#define TEKO_RT_H

#include <stdint.h>   // uint8_t, int64_t
#include <stddef.h>   // size_t
#include <stdbool.h>  // bool
#include <stdlib.h>   // realloc, free — needed by TK_RT_LIST


// ─── C7.14: concrete list types (TK_RT_LIST) ────────────────────────────────
// TK_RT_LIST(T, Name) — the runtime analogue of core.h's TK_LIST. Generates a
// typed growable array struct and three static-inline helpers with ZERO overhead
// at the call site (all inlined). Growth uses realloc/free directly (stdlib.h)
// because this header is self-contained and must not include core.h (which
// re-declares tk_alloc as static-inline, conflicting with the extern below).
// OOM panics via abort() — same M.1 fail-loud contract as tk_alloc itself.
// `push` CONSUMES and RETURNS (xs = Name##_push(xs, item)); the caller holds the
// one live copy. `free` releases the backing buffer without touching elements.
#define TK_RT_LIST(T, Name)                                                   \
    typedef struct { T *ptr; size_t len; size_t cap; } Name;                  \
    static inline Name Name##_empty(void) {                                   \
        return (Name){ .ptr = NULL, .len = 0, .cap = 0 };                    \
    }                                                                         \
    static inline Name Name##_push(Name xs, T item) {                        \
        if (xs.len == xs.cap) {                                               \
            size_t ncap = (xs.cap == 0) ? 8 : (xs.cap * 2);                  \
            void *p = realloc(xs.ptr, ncap * sizeof(T));                      \
            if (p == NULL) { abort(); }                                       \
            xs.ptr = (T *)p; xs.cap = ncap;                                   \
        }                                                                     \
        xs.ptr[xs.len] = item;                                                \
        xs.len = xs.len + 1;                                                  \
        return xs;                                                            \
    }                                                                         \
    __attribute__((unused)) static inline void Name##_free(Name xs) { free(xs.ptr); }   /* (#151) many instantiations never free (arena-era callers) — not a warning */

// byte — one octet (mirrors src/text/text.h's tk_byte; same rep).
typedef uint8_t tk_byte;

// str — a VIEW into UTF-8 bytes; len is in BYTES, NOT NUL-terminated.
// Identical shape to src/text/text.h's tk_str.
typedef struct {
    const tk_byte *ptr;   // the bytes
    size_t         len;   // length in BYTES
} tk_str;

// char — a UTF-8 codepoint (1–4 bytes). DISTINCT type (its own checker tag), but its runtime
// layout is the SAME byte-slice shape the codegen lifts a `[]byte` to (`{uint8_t*,uint64_t}` ==
// tk_slice_byte). A `c'…'` literal lowers to a static byte array + this view. Decode to the scalar
// codepoint via tk_char_to_u32 (the explicit `char to u32`/u64/i64 cast).
typedef struct {
    uint8_t  *ptr;        // the codepoint's UTF-8 bytes
    uint64_t  len;        // length in BYTES (1–4)
} tk_char;

// tk_slice_byte — the runtime layout of a `[]byte` slice. Same {ptr,len} shape as tk_char
// (both are byte arrays). Pre-declared here so runtime helpers can name it without depending
// on the generated program's per-file typedef.
typedef struct {
    tk_byte  *ptr;
    uint64_t  len;
} tk_slice_byte;

// ─── item-14 §13.2: the FAT value-struct header ─────────────────────────────
// tk_struct_hdr — the fat value-struct header: ONE self-pointer word that makes `self` a ref, so an
// in-place mutation on a struct method STICKS, WITHOUT turning the struct into an object. It is
// deliberately SMALLER than an object header — no vtable, no identity region — because a struct stays
// a VALUE: it is copied on assignment/pass, has no heap identity, and costs no arena.
//
// The value semantics are preserved by RE-MATERIALIZATION: every copy gets a FRESH header pointing at
// the copy itself (tk_struct_hdr_remat), so mutating a copy can never reach through to the original.
// A struct whose header still pointed at the source would alias it — that is the one bug this type
// exists to make impossible.
//
// Every value-struct C typedef embeds this as its FIRST member (`__hdr`), so a field's offset is its
// declared offset plus one word, identically in both backends.
typedef struct { uintptr_t self_ptr; } tk_struct_hdr;

// tk_struct_hdr_of — materialize the header of the struct living at `p`: the self-pointer word is the
// address of the materialization itself. Called at CONSTRUCTION (`Tipo { … }` / `.{ … }`).
//
// `p` is void* rather than tk_struct_hdr* because the caller holds a pointer to the whole generated
// struct type (whose first member is the header) and C guarantees a pointer to a struct equals a
// pointer to its first member — so no per-type helper is needed.
static inline void tk_struct_hdr_of(void *p) {
    ((tk_struct_hdr *)p)->self_ptr = (uintptr_t)p;
}

// tk_struct_hdr_remat — RE-materialize the header after a value COPY: `dst` holds a bitwise copy of
// some source struct, so its header still carries the SOURCE's self-pointer; point it at `dst`. This
// is what keeps value semantics honest — without it a mutation through the copy's `self` would land
// on the original. Called on assignment, by-value argument passing, and by-value return.
static inline void tk_struct_hdr_remat(void *dst) {
    ((tk_struct_hdr *)dst)->self_ptr = (uintptr_t)dst;
}

// closure / function VALUE (W10a). The uniform runtime representation of a value of a function
// type `(A, B) -> R`: a code pointer plus a captured-environment pointer. For a NAMED fn used as a
// value (W10a) `env` is NULL and `fn` is the C function's address; calls cast `fn` back to the
// statically-known C signature `R (*)(A, B)` and invoke it. The `env` field is reserved for W10b
// capturing closures (so the struct shape never changes when captures land).
typedef struct {
    void *fn;     // the code pointer (a C function address)
    void *env;    // captured environment (NULL for a named fn / non-capturing closure)
} tk_closure;

// error — the Teko built-in error-as-value (E2-NATIVE). Mirrors core.h's tk_error so that
// generated programs carry full diagnostic adornments in native.
// A message-only `error { message = "…" }` literal leaves file/line/col/expected/actual
// at their zero/NULL defaults (C1.3 additive — all existing sites are unchanged).
typedef struct {
    tk_str     message;    // the error message (required)
    tk_str     file;       // source file (empty = unknown)
    uint32_t   line;       // 1-based line (0 = unknown)
    uint32_t   col;        // 1-based column (0 = unknown)
    tk_str     expected;   // rendered expected type (empty = n/a)
    tk_str     actual;     // rendered actual type   (empty = n/a)
} tk_error;

// Constructors — message-only (minimal) and the two diagnostic adornment helpers that
// mirror core.h's tk_error_loc / tk_error_types (E2).
static inline tk_error tk_error_make(tk_str message) {
    tk_error e; e.message = message; e.file = (tk_str){0}; e.line = 0; e.col = 0;
    e.expected = (tk_str){0}; e.actual = (tk_str){0}; return e;
}
static inline tk_error tk_error_loc(tk_error e, uint32_t line, uint32_t col) {
    e.line = line; e.col = col; return e;
}
static inline tk_error tk_error_types(tk_error e, tk_str expected, tk_str actual) {
    e.expected = expected; e.actual = actual; return e;
}

// C7.14: concrete list type instantiations (must come AFTER tk_byte/tk_str are defined).
// Generated programs and the compiler itself use these typed lists. The Teko-surface
// equivalents are `[]str`, `[]byte`, `[]i64` with `teko::list::push` / `teko::list::empty`
// (see teko_rt.tks § C7.14 comment block).
TK_RT_LIST(tk_byte,   tk_byte_list)    // []byte  — byte builder, str of bytes, etc.
TK_RT_LIST(tk_str,    tk_str_list)     // []str   — string accumulator lists (argv, paths, …)
TK_RT_LIST(int64_t,   tk_i64_list)     // []i64   — integer accumulator lists

// tk_alloc — the allocation seam (S0→S1). Hands back a fresh, uniquely-addressable block of
// ≥ n usable bytes (n→1 when 0 so the result is unique), aligned to the arena's alignment
// (TK_ARENA_ALIGN in teko_rt.c: max_align_t's alignment floored at 16, the strongest alignment
// any supported target demands, so every node is correctly aligned even where max_align_t's
// alignment is only 8);
// tk_panic on OOM (M.1, never NULL). Generated code allocates through this: slice
// copy-append AND the auto-boxed recursive-value-type back-edges (tk_alloc(sizeof *p)).
// (S1) The body now bump-allocates from the process ROOT region (tk_region_root) instead
// of calling malloc directly — the swap is mechanical, the contract is unchanged, and the
// root is never dropped so the leak profile is identical to malloc-everywhere (M.5).
// LINKAGE NOTE: this is the EXTERN runtime tk_alloc, distinct from core.h's static-inline
// tk_alloc (internal linkage, used by the compiler TUs over libc). The two never merge;
// do NOT give core.h's tk_alloc external linkage nor #include this header into a compiler-internal
// TU — that would route those allocations to the arena while their tk_free0 stays libc (corruption).
void *tk_alloc(size_t n);

// tk_slice_variant_widen — build a `[]T` (T a variant; the own backend holds a variant slice
// BY-ADDRESS, i.e. `len` 8-byte pointers each to a boxed 24-byte tag+payload wrapper) from a `[]U`
// whose element U is a DIRECT member of T, wrapping every element as member `tag`. This is the own
// backend's analog of the C route's `emit_as` covariant-slice rebuild (codegen.tks, the
// malloc + per-element `cg_wrap_elem_str` loop): the own backend's `lower_value_into_type` widened a
// scalar member into a variant but left a `[]U` -> `[]T` binding un-widened, so the slice's elements
// were read at the wrong width. `mode` selects how the wrapper's payload (offset 8) is filled from
// the `[]U` element at `src_stride` bytes: 0 = REGISTER (copy `pcopy` payload bytes inline), 1 = FAT
// (copy the 16-byte {ptr,len} pair), 2 = AGGREGATE (the element holds an address; box `pcopy` bytes
// read through it and store the box pointer), 3 = NULL (tag only). Every allocation follows
// tk_region_current(), exactly like tk_slice_push, so the widened slice lives where its `[]U` source
// did. Returns the `[]T` data pointer (len is unchanged, kept by the caller).
void *tk_slice_variant_widen(const void *src, uint64_t len, uint64_t src_stride,
                             int64_t tag, int32_t mode, uint64_t pcopy);

// ── Arena allocation (S1 — TEKO_EVOLUTION_DESIGN §5.2: arena primitive + root region) ──
// A bump-allocator REGION: a chunk-list of aligned-malloc'd blocks, sub-allocated by a bump
// offset. No per-object metadata, no free-list (M.0 metal/no-GC). region_alloc results are
// TK_ARENA_ALIGN-aligned (an over-aligned chunk payload, NOT merely malloc's guarantee — see
// teko_rt.c), so every type that was malloc-stored — including the most strongly-aligned nodes —
// stays correctly aligned. OOM panics (M.1, never NULL). region_drop bulk-frees the whole span in
// one pass (the S2 keystone). The process ROOT region (tk_region_root) is never dropped in
// S1 → its memory lives for the whole process = today's malloc-everywhere leak (M.5
// leak-tolerant), so routing tk_alloc through it is behavior-preserving (the only divergence
// is the OOM boundary, which shifts by the per-chunk header — still fail-loud, M.1).
// S1 reroutes ONLY this runtime seam; core.h's compiler seam stays on libc until S2's
// realloc-aware list migration (the arena has no per-block size header, so a drop-in realloc
// needs old-size threaded through TK_LIST — an S2-scope change).
#define TK_REGION_DEFAULT_CHUNK (64u * 1024u)   // default chunk payload (bytes)
typedef struct tk_region tk_region;             // opaque — full struct lives in teko_rt.c
// (S2 — arena PARENT-PTR TREE + per-arena type→instance registry) tk_region_new now takes the
// enclosing region as its PARENT (NULL for a parentless/root region). Codegen threads the
// currently-active region variable (the function frame, or the innermost block region) at every
// call site, so the tree mirrors the LEXICAL nesting that used to be implicit in C scoping only.
// This is the prerequisite for a future DI `#scoped` lifetime: tk_region_lookup walks UP the
// parent chain to find an already-materialized ancestor instance before tk_region_register makes
// a new one in the CURRENT arena (never an ancestor's — registration is always local).
tk_region *tk_region_new(tk_region *parent);    // a fresh empty region (default chunk size), child of `parent` (NULL = no parent)
void      *tk_region_alloc(tk_region *r, size_t n);  // bump-allocate n (n→1), aligned; OOM→panic
void       tk_region_drop(tk_region *r);        // bulk-free every chunk + the region (NULL-tolerant; idempotent on a re-walk — head is cleared before free; callers MUST null their handle after, as the freed region must not be reused)
void       tk_region_drop_subtree(tk_region *root);  // (#337) the `adopt` bulk-drop: drop `root` AND every live region whose ->parent chain reaches it, in one sweep (cycles among objects irrelevant); NULL-tolerant; callers MUST null their handle after
tk_region *tk_region_root(void);                // the CURRENT TASK's root region (lazy; parent = NULL — the tree root). Since F1 there is one per task, not one per process.

// --- (F1) THE TASK — the seat of the memory discipline -------------------------------------
// Before F1 the arena mark stack, the root region and the free list were process-global, so one
// flow of control's tk_arena_pop rewound the root past allocations another flow was still using.
// Each of those families now lives on a tk_task, and every allocator entry point above reads
// tk_task_current() internally — the PROTOTYPES ARE UNCHANGED, so no call site moved.
//
// tk_task_current is the whole seam, and it has exactly two encarnations: a _Thread_local pointer
// under the C seed (one %fs-relative load), or pthread_getspecific/TlsGetValue behind an
// `extern fn` in the native Teko runtime — an ordinary call to an external C symbol, which asks
// the own backend for no new capability. Swapping one for the other touches ONE function.
typedef struct tk_task tk_task;
tk_task   *tk_task_current(void);               // the task owning this flow's memory discipline; never NULL (defaults to the main task)
tk_task   *tk_task_begin(void);                 // create + install a fresh task with an EMPTY discipline; returns the task that was current
void       tk_task_end(tk_task *previous);      // free the current task's regions and reinstall `previous`

// (§10 C0a) tk_thread_spawn — fire-and-forget: start a DETACHED OS thread running `entry(ctx_blob)`
// on its OWN sub-root arena. `entry` is the codegen trampoline (one per spawn-site, the sole caller
// of the target fn); `ctx_blob` is a single self-contained malloc'd block holding a deep COPY of every
// argument (packed by the spawning thread before the call). Ownership of `ctx_blob` passes to the
// spawned thread, which frees it after `entry` returns. Never blocks the caller. Panics (never returns
// an error) if the OS refuses the thread — a spawn that cannot start is program-fatal, matching
// tk_task_begin's OOM panic. §16 will retire libc: pthread_create is the TRANSITIONAL mechanism;
// clone(2)/CreateThread selection is an internal detail deferred to §16. Nothing outside this file may
// assume pthreads.
void       tk_thread_spawn(void (*entry)(void *), void *ctx_blob);
// (§10 AWAIT-BATCH) tk_thread_join_spawn — the JOINABLE twin. Same contract as tk_thread_spawn but
// returns an opaque handle for a later tk_thread_join to block on until the thread's `entry` returns.
// UNUSED by the spawn surface (spawn is detached); declared now so the await lowering resumes quickly.
// Do NOT wire a Teko surface to it in this batch.
// (§10 AWAIT-BATCH) tk_thread_join — block until the thread named by `handle` has returned, then free
// the handle. UNUSED by the spawn surface. §16 will retire libc.
// (§10 C0a) tk_thread_spawn_selftest — the C-owned probe body the `thread_spawn_paranoid` fixture
// reaches through ONE ordinary extern call (the tk_test_capture_probe pattern — a Teko test cannot
// pass a `void(*)(void*)`). Spawns `n` threads, each doing tk_task_begin/alloc/tk_task_end, JOINS
// them all (deterministic — detached threads give the test no finish edge), then returns the
// tk_names_live_count delta from the pre-spawn baseline (MUST be 0 under TEKO_MEM_PARANOID=1).

// (§10 C0b) tk_memchan — an in-process MPSC FIFO for opaque fixed-width elements, living in the F2
// program region (tk_region_program) so it OUTLIVES the producing task's tk_task_end (a channel is
// cross-task by construction). Type-erased: it moves `elem_size` raw bytes per element (the Teko
// layer marshals T<->bytes). Bounded by a slot count; `bounds == 0` means UNBOUNDED (grows). All
// send/recv are blocking (a bounded send blocks while full, a recv blocks while empty), released by
// the close signal.
typedef struct tk_memchan tk_memchan;
// tk_memchan_make — allocate a channel of `bounds` slots of `elem_size` bytes each. The STRUCT SHELL
// lives in the F2 program region (stable address for the registry); the ring BUFFER and the pthread
// mutex/cond are heap/OS resources that tk_memchan_end releases (F2 has no per-entry free yet — see
// the C5 teardown note). bounds==0 => unbounded (buffer doubles on demand). OOM => tk_panic.
// tk_memchan_send — copy `elem_size` bytes from `elem` into the FIFO tail. Blocks while a BOUNDED
// channel is full; returns immediately once space exists or the channel is closed (a send on a
// closed channel is a no-op — the producer contract closes exactly once).
// tk_memchan_recv — copy the FIFO head into `out` (`elem_size` bytes). Blocks while empty. Returns 1
// when an element was delivered, 0 when the channel is CLOSED AND DRAINED (the `Closed` signal the
// Teko Rx.pop surfaces). A single reader (MPSC) is assumed.
// tk_memchan_close — mark closed and wake every blocked sender/receiver. Idempotent.
// tk_memchan_end — close + release the ring buffer and destroy the mutex/cond (the drop-cascade's
// transport teardown). Idempotent. The F2 struct shell is reclaimed at process exit.
// tk_memchan_selftest — C-owned probe body reached through ONE plain-scalar extern (the
// tk_thread_spawn_selftest pattern). Spawns a producer that sends 0..n-1 (i64), the caller thread
// recv's them, asserts order+sum, then close+end. Returns 0 on success, a non-zero failure code
// otherwise. Under TEKO_MEM_PARANOID=1 the arena live-count is asserted balanced by the caller.
// The u64-HANDLE ABI the Teko `extern fn` surface binds to (the tk_region_new_u convention): a
// tk_memchan* travels through Teko as a `u64`, and the element buffer as a `u64` address, so these
// thin twins take/return that width and cast at the boundary (no int<->pointer conversion warning).

// (§10 C0c) tk_oschan — an AF_UNIX SOCK_DGRAM MPSC transport: ONE reader socket bound to a
// key-derived ABSTRACT-namespace name (sun_path[0]==0, Linux — auto-removed on close, no filesystem
// cleanup), N writers each sendto that name. Datagram boundaries make each `push` atomic for free (no
// torn frame). Lives in F2 (tk_region_program). Fixed-width elements only in v1: `elem_size` raw
// little-endian bytes per datagram. Fat T (str/slice) across the socket is a later frame — gated by
// C3's checker.
typedef struct tk_oschan tk_oschan;
// tk_oschan_make — create+bind the reader socket to the abstract name derived from `key`; set
// SO_RCVBUF from `bounds` (bounds==0 => the OS default, effectively unbounded). Returns the
// F2-resident handle, or tk_panic on a socket/bind failure (the surface maps this to `make`'s error).
// tk_oschan_send — sendto the reader's bound name (a writer socket is lazily opened per calling
// thread and cached thread-local). Returns 0 on accept, -1 when the receive queue is full (EAGAIN
// under MSG_DONTWAIT for a bounded channel); a blocking (bounds==0) send omits MSG_DONTWAIT.
// tk_oschan_recv — recvfrom the reader socket (blocking). Returns 1 with `out` filled (elem_size
// bytes), or 0 on the CLOSED sentinel (a zero-length control datagram the producer's close sends).
// tk_oschan_close — send the zero-length CLOSED sentinel so a blocked reader wakes and drains;
// idempotent.
// tk_oschan_end — close + close(2) the reader fd (abstract name vanishes) + close cached writer fds.
// Idempotent.
// tk_oschan_selftest — the fan-in proof as a C-owned body reached by one scalar extern: `w` writer
// threads each send `r` records carrying (writer_id, seq, pattern=f(id,seq)); the reader verifies all
// three per record (proof by CONTENT not count). Returns 0 on success.
// The u64-HANDLE ABI the Teko `extern fn` surface binds to (the tk_region_new_u convention). The key
// bytes travel as a u64 address + a length so a Teko `str` reaches the C `const char *`.
// tk_oschan_make_str — the same make with the key delivered as a Teko `str` fat pointer (the natural
// binder for a `str` channel key), so a Teko caller needs no manual byte-address plumbing.

// (§10 C5) tk_waitgroup — a MANUAL counter with a blocking wait, F2-resident (survives producing
// tasks). add(n)/done() mutate the count under a mutex; wait() blocks on a cond until the count
// reaches zero.
typedef struct tk_waitgroup tk_waitgroup;
// The u64-HANDLE ABI the Teko `extern fn` surface binds to (the tk_region_new_u convention).
// (§10 C5) tk_region_deregister — bind `type_id` -> NULL in `r`'s OWN table so a post-teardown
// tk_region_lookup MISSES (the drop-cascade key removal). The arg-flip twin of tk_region_register.

// (F2) tk_region_program — the PROGRAM region: one per process, owned by NO task, so an object in
// it survives both a task's tk_arena_pop and that task's exit. F1 leaves the runtime with N task
// roots and nothing else, so the singletons the owner's ruling places "in the program arena"
// (channels, and whatever else must outlive the task that created it) need this seat. Freed at
// process termination by tk_regions_free_all, so the program stays leak-clean.
tk_region *tk_region_program(void);
// (#109 test-gate memory) checkpoint/rewind the ROOT region's bump position, bulk-freeing everything
// it allocated in between. Balanced push/pop; used by the test-gate runner to bound per-test memory.
void       tk_arena_push(void);                 // save the root region's current bump position
void       tk_arena_pop(void);                  // free every root-region chunk allocated since the matching push
// (enabling primitive — staged off; no compiler source calls this yet) tk_arena_commit — discard
// the top mark WITHOUT rewinding, so every allocation made since the matching tk_arena_push is
// folded permanently into the (now-current) mark below it. The Boundary-A counterpart to
// tk_arena_pop: a scope that turned out to ESCAPE commits its allocations instead of losing them.
void       tk_arena_commit(void);               // discard the top mark, keeping its allocations
// (E1-C2) tk_task_reset — return the current task to the ephemeral-clean state of "before any test":
// the ONE place that clears every per-task singleton a `#test` can dirty (intern table, coverage
// counts, cast positions, tally, scope/scenario labels, capture channels, stdin/fd staging), buffers
// preserved. The `task_reset` builtin lowers to it; resolves the task through tk_task_current().
void       tk_task_reset(void);                 // clear the current task's ephemeral per-test state
// (0.3.1 backend-memoria C1) tk_region_enter / tk_region_leave — the SWAPPABLE CURRENT REGION. A
// per-task stack of "current region": tk_alloc bump-allocates from the stack TOP instead of a fixed
// tk_region_root(). An EMPTY stack ⇒ the root, so a program that never enters is byte-for-byte the
// program it was (behaviour-identical). Reuses the tk_region_new/tk_region_drop child tree (separate
// chunk lists — NONE of tk_arena_push's interleaved-bump trap), letting the native backend route its
// per-function scratch into a child region and drop it wholesale after copying the emitted bytes to
// the root object accumulators. Balanced enter/leave; an over-deep enter is counted but not stored
// (leave then no-ops), keeping enter/leave balanced without ever reading past the fixed stack.
void       tk_region_enter(tk_region *child);   // push `child` as the current region for tk_alloc
void       tk_region_leave(void);               // pop the current region (back to the enclosing one, else root)
// (0.3.1 move-on-return A1) tk_region_current — the region tk_alloc bump-allocates from RIGHT NOW: the
// current-region stack TOP, else the root (empty/over-deep degrades to root, never garbage). Made
// PUBLIC (was static) so the C and native codegens can emit the accessor to capture R_ret — the
// caller's chosen result region — at function entry (the move-on-return conveyance channel).
tk_region *tk_region_current(void);             // the current region for tk_alloc (root when the stack is empty)
// (0.3.1 backend-memoria C1) the u64-HANDLE ABI the Teko `extern fn` surface binds to: a tk_region*
// travels through Teko as a `u64` (uintptr_t), so these thin twins take/return that width and cast
// at the boundary, matching the extern prototypes byte-for-byte (no int↔pointer conversion warning
// in the C route). The native backend passes the same 64-bit word untouched. tk_region_leave has no
// pointer in its signature, so the Teko surface binds it directly (no twin needed).
uint64_t   tk_region_new_u(uint64_t parent);    // tk_region_new((tk_region*)parent) as a handle
uint64_t   tk_region_root_u(void);              // tk_region_root() as a handle
uint64_t   tk_region_current_u(void);           // tk_region_current() as a handle (native captures R_ret at fn entry)
void       tk_region_drop_u(uint64_t region);   // tk_region_drop((tk_region*)region)
void       tk_region_enter_u(uint64_t child);   // tk_region_enter((tk_region*)child)
// (§16 crumb D) the arena-over-mmap P2 seam — the ONE mutable process word (per-task, _Thread_local)
// the Teko arena core recovers its CONTROL block through, plus the cached TEKO_MEM_PARANOID probe it
// reads its poison oracle from. See the seam block in teko_rt.c and docs/design/plano-s16-arena-mmap.md
// §1.3. ADD-ALONGSIDE at crumb D (the arena core is compiled but unwired; no caller yet).
uint64_t   tk_arena_control_get(void);          // this task's arena CONTROL address, 0 until first init
void       tk_arena_control_set(uint64_t addr); // install this task's arena CONTROL address (once, lazily)
uint64_t   tk_arena_paranoid(void);             // cached TEKO_MEM_PARANOID probe (1 set/non-empty, else 0)
// tk_region_register — bind `type_id` → `instance` in `r`'s OWN table (never an ancestor's; a
// second registration of the same type_id in the same region OVERWRITES — the compiler is
// expected to enforce true duplicate-registration errors at a higher DI layer; this is just the
// storage primitive).
// tk_region_lookup — find `type_id`'s instance in `r`, else its parent, else its parent's parent,
// … until found or the chain ends (NULL). The #scoped walk-up primitive.
// (W9.3b) tk_regions_free_all — free every still-live region of the CURRENT TASK (its root + its
// live scoped frame/block regions) AND the (F2) program region, emptying both registries. Called at
// the termination choke points (tk_panic*, tk_exit, and an atexit hook registered lazily by whichever
// of tk_region_root / tk_region_program runs first) so that an abnormal exit/panic does not leak the
// stack-local scoped regions that a diverging path skips dropping. Idempotent: after it runs both
// registries are empty, so a second call (e.g. atexit after an explicit panic/exit call) is a no-op.
// A task other than the caller's owns its own regions; tk_task_end is what frees those.
void       tk_regions_free_all(void);

// --- (F3) THE NAME REGISTRY — a u64 NAME resolves to a resource, and a stale name is an ERROR ---
//
// WHY THIS IS RUNTIME AND NOT TEKO. Teko has no module-level mutable state (`mut REG = …` outside a
// function does not parse — "expected a declaration") and check_ref_return_passdown only admits
// returning one of a function's own `ref` parameters, so a stored field cannot escape. There is
// nowhere in Teko to PUT a process-wide table today, so the table lives here and Teko reaches it
// through `extern fn` (src/names/names.tks).
//
// WHY A NAME AND NOT A POINTER. A stale pointer is undefined behaviour; a stale NAME is a reportable
// error. Every entry point below answers a dead name with a status code, never with another
// resource's bytes. That is the whole reason this layer exists between a task and a shared resource.
//
// WHERE IT LIVES. In the (F2) PROGRAM region: the slot array must outlive both the tk_arena_pop of
// whatever task grew it and the exit of that task, and the program region is the only seat with that
// lifetime. tk_regions_free_all frees the program region, so it also forgets this table (a name
// issued before termination is UNKNOWN after it, never a dangling read).
//
// THE PACKING, AND WHY RESURRECTION IS IMPOSSIBLE RATHER THAN MERELY UNLIKELY. A name is
// `(generation << 32) | slot`: the low half indexes the slot array directly (O(1), no hashing), the
// high half is that slot's lifetime stamp. tk_names_close bumps the stamp, so every name ever issued
// for a slot is strictly older than the live one and compares unequal — a recycled slot cannot
// answer to its predecessor's name. A slot whose stamp would reach TK_NAMES_GEN_MAX is RETIRED
// (never returned to the free list), so the counter cannot wrap and hand a second resource the name
// of a first: the guarantee is arithmetic, not statistical. Its price is one slot (24 bytes on LP64)
// withheld per 4 294 967 293 open/close cycles of that same slot.
//
// NOT the per-region `tk_region_register`/`tk_region_lookup` pair above, deliberately: that one is
// keyed by a compile-time type_id, is PER REGION with a parent walk, is a linear scan, and its
// duplicate rule is OVERWRITE — i.e. exactly the silent-resurrection behaviour this table exists to
// make impossible. Nothing here touches it and the DI surface is unchanged.
#define TK_NAMES_KIND_CELL      1u          // the F3-owned resource kind: one int64_t in the program region
#define TK_NAMES_OK             0           // the name is live (and, for a kinded query, of that kind)
#define TK_NAMES_ERR_UNKNOWN  (-1)          // no such name was ever issued (includes the 0 name)
#define TK_NAMES_ERR_CLOSED   (-2)          // the name WAS issued and has since been closed — the stale case
#define TK_NAMES_ERR_KIND     (-3)          // the name is live, but names another kind of resource
// The two codes below classify a REFUSED open — an open answers with the name 0 and the caller is
// what tells the two apart, because only the caller's own argument distinguishes them.
#define TK_NAMES_ERR_FULL     (-4)          // no slot could be issued (the index space is exhausted)
#define TK_NAMES_ERR_RESERVED (-5)          // a cell was asked to hold TK_NAMES_NO_VALUE
#define TK_NAMES_NO_VALUE     INT64_MIN     // the reserved payload tk_names_cell_read returns for a dead name
// tk_names_open — register `resource` under a fresh name of `kind`. The registry never allocates,
// frees or dereferences the resource: it only hands back a name for it. 0 on refusal (index space
// exhausted). THIS IS THE F4 SEAM: a `tk_chan *` enters here under its own kind.
// tk_names_lookup — the resource `name` stands for, or NULL when the name is dead, was never issued,
// or names a resource of another kind. The kind argument is what stops one kind's name from ever
// resolving to another kind's bytes.
void      *tk_names_lookup(uint64_t name, uint32_t kind);
// tk_names_status — TK_NAMES_OK, or which of the failure modes `name` is in (kind-agnostic).
int64_t    tk_names_status(uint64_t name);
// tk_names_close — retire `name`, bumping its slot's stamp so the name can never resolve again.
// TK_NAMES_OK, or the same status a lookup would have failed with (closing twice is an ERROR).
int64_t    tk_names_close(uint64_t name);
// tk_names_live_count — how many names are live right now (the F3 leak probe: open+close returns it).
uint64_t   tk_names_live_count(void);
// tk_names_capacity — how many slots the array currently holds (its byte cost is capacity * 24).
uint64_t   tk_names_capacity(void);
// tk_names_slot_of — the slot half of a name. A PURE decode: defined for a dead name too, which is
// what lets a test show that the slot really is recycled while the name is not.
uint64_t   tk_names_slot_of(uint64_t name);
// tk_names_generation_of — the stamp half of a name. Pure decode, same reason.
uint64_t   tk_names_generation_of(uint64_t name);
// tk_names_cell_open — the first resource kind, and the one Teko can hold today: a single int64_t
// allocated in the program region. 0 on refusal (TK_NAMES_NO_VALUE is not storable).
uint64_t   tk_names_cell_open(int64_t value);
// tk_names_cell_read — the value the cell named by `name` holds, or TK_NAMES_NO_VALUE when the name
// does not resolve to a live cell.
int64_t    tk_names_cell_read(uint64_t name);
// tk_names_cell_status — TK_NAMES_OK when `name` is a live cell, else the failure mode (so a caller
// can report WHY the read failed instead of only that it did).
int64_t    tk_names_cell_status(uint64_t name);

// --- the PER-TEST CHANNEL (owner ruling: "para os que rodam em processo, passar um canal proprio
// pra stdin, out e err"). ---------------------------------------------------------------------
//
// A `#test` runs IN PROCESS, in the same address space and on the same two FILE* streams as every
// other test of the run. That made the run's output UNATTRIBUTABLE: the harness printed
// `test <label> ... `, the test's own body then printed whatever it prints, and the closing `ok`
// landed on the NEXT line — a suite count taken by grepping `... ok` under-reported the run, and a
// failing test's report showed a NEIGHBOUR's bytes. Under parallelism that is not confusing, it is
// unusable.
//
// So a test gets its OWN out/err channel: between tk_test_begin and tk_test_end every tk_print /
// tk_println / tk_write / tk_eprint / tk_ewrite / tk_eprintln byte is appended to a per-test buffer
// instead of the shared stream, and tk_test_end then writes ONE deterministic block: the verdict
// line first (so `test <label> ... ok` is ATOMIC and countable), then that test's own bytes, each
// line prefixed and therefore attributed. tk_panic/tk_panic_str do the same for a FAILING test
// before they abort, which is what makes a failure report show what THAT test wrote.
//
// The channel is a process-wide singleton, not a stack: tests do not nest.
//
// TK_TEST_LABEL_MAX — the label buffer, sized so no qualified `namespace::name` in this corpus is
// truncated; a longer one is clipped rather than growing an allocation on the panic path.
#define TK_TEST_LABEL_MAX 512
// TK_TEST_CHAN_MIN_CAP — the first allocation a captured channel makes. Most tests print nothing,
// so the buffers stay NULL for the whole run and the capture costs one branch per print.
#define TK_TEST_CHAN_MIN_CAP 256
// TK_TEST_OUT_PREFIX / TK_TEST_ERR_PREFIX — what opens each attributed line of a test's own output.
// Neither may begin with `test ` : a suite count greps `^test <label> ... ` and a captured line that
// could impersonate a verdict line would recreate the miscount this channel exists to end.
#define TK_TEST_OUT_PREFIX "     out| "
#define TK_TEST_ERR_PREFIX "     err| "
// TK_TEST_SHARD_ENV — the env key selecting `<i>/<n>` sharding of an in-process suite.
#define TK_TEST_SHARD_ENV "TEKO_TEST_SHARD"
// --- the SCENARIO NAME (what `teko::assert` addresses a case BY) ------------------------------
//
// A scenario used to be addressed by the exit code its failure produced. That space holds 255 values,
// it collides with itself long before a corpus is exhausted, and `exit(260)` TRUNCATES to 4 in
// silence — a fixture with 119 cases had ten of them aliasing cases 5..13. A NAME has none of those
// properties, so the name is what an assertion reports and what a passing case announces.
//
// tk_assert_scenario_set — name the case every later assertion failure is prefixed with.
void tk_assert_scenario_set(tk_str name);
// tk_assert_scenario_prefix — `"<name>: "`, or an empty str when no case is named (so an assertion
// outside a scenario keeps its historic message byte-for-byte).
tk_str tk_assert_scenario_prefix(void);
// TK_SCENARIO_OK_PREFIX — the fixed word that opens a passing case's announcement, so a `.tkr` row
// can demand `scenario <name>: ok` and NOT be satisfied by a different case whose name merely ENDS
// with this one's. Measured on the corpus: `loop_tail_no_break` is a suffix of
// `fat_loop_tail_no_break`, so without the prefix the first row would pass on the second's line.
#define TK_SCENARIO_OK_PREFIX "scenario "
// tk_assert_scenario_ok — announce `scenario <name>: ok` on the CURRENT channel (a test's own channel
// when one is open, the real stdout otherwise). Panics when no case is named: a silent "ok" for an
// unnamed scenario is the reporting hole this whole mechanism exists to close.
void tk_assert_scenario_ok(void);
// tk_test_begin — open `label`'s channel; every subsequent print is captured, not written.
void tk_test_begin(tk_str label);
// tk_test_end — close the open channel and emit the `ok` verdict block (verdict line, then the
// captured stdout/stderr lines, prefixed). A no-op when no channel is open.
// TK_TEST_SCOPE_MAX — the scope token buffer. A token is `<shard>-<sanitised label>`, so it is bounded
// by the label bound plus the shard digits and a separator.
#define TK_TEST_SCOPE_MAX (TK_TEST_LABEL_MAX + 24)
// tk_test_scope — a filesystem-safe token identifying the test running RIGHT NOW, or "" when none is.
//
// WHY THIS EXISTS (owner, 2026-07-30, on the parallelism landed in this same change: "implementou-se
// o paralelismo mas faltou o controle de concorrência, aí o arquivo acaba sofrendo sobrescrita").
// The `.tkcov` dump was fixed by ISOLATION — one file per shard, then merged — and that is the right
// answer, but it was applied to ONE resource. Every other fixed scratch path a test writes has the
// same shape, and the round-robin shard split makes it WORSE: neighbouring tests, which are exactly
// the ones sharing a path because they live in the same file and family, land in DIFFERENT shards.
//
// So the discipline is uniform instead of per-resource: a scratch path DERIVES from the identity of
// the test that writes it. The identity is already here — the per-test channel records the label —
// and the shard index disambiguates the (impossible today, cheap to exclude) case of one label being
// run by two processes.
//
// EMPTY OUTSIDE A TEST, deliberately: the same composing code runs in PRODUCTION (the regression
// runner asks the host toolchain the same questions the unit tests do), and there the path must stay
// exactly what it was. Scoping applies only where the concurrency it guards against exists.
tk_str tk_test_scope(void);
// tk_test_shard_take — the SHARD filter: count this test and answer whether THIS process owns it.
// With no shard selected (`TEKO_TEST_SHARD` unset/malformed) every test is owned, so the emitted
// harness is behaviourally identical to the unsharded one. See teko_rt.c for the `i/n` protocol.
// --- the CAPTURE (owner ruling 2026-07-30, §14 of docs/design/journaling-de-corrida-0.3.1.md) ------
//
// "E PARA CAPTURAR E SAIR ELEGANTE ANTES SEM ENVIAR SYSCALL DE SAIDA QUANDO COMPILAR TESTES... Uma
// coisa e um aborto externo, de fora do programa, outra coisa e o proprio programa sair, e para sair
// ele tem que ser deterministico."
//
// A `#test` that panics or calls exit() used to END THE PROCESS, so every test after it was never
// reported and the run's tally had to be RECONSTRUCTED from what the death left behind. The program
// leaving on its own terms is OURS to control; only an EXTERNAL abort (SIGKILL, the OOM killer, a
// power cut) is not. So the self-inflicted half is captured and the suite continues.
//
// THREE RESULTS, AND NONE OF THEM IS A STATE OF THE PROCESS. That is the whole distinction: `exit(7)`
// inside a `#test` becomes a FACT ABOUT THAT TEST which the harness reports and moves past; `exit(7)`
// in a program is still the program's contract with whoever invoked it, and stays a real syscall.
#define TK_TEST_OK        0   /* the body returned */
#define TK_TEST_PANICKED  1   /* panic / failed assertion / implicit panic (div0, oob, cast) */
#define TK_TEST_EXITED    2   /* the body called exit(n); `code` carries the n */
// tk_test_outcome — how one `#test` body ended, and with which value when it ended by exiting.
//
// NAMED `outcome` AND NOT `end`, and the reason is measured rather than stylistic: `tk_test_end` is
// already a FUNCTION in this same header (the channel closer), and a typedef of that spelling would
// collide with it in C's ordinary identifier namespace. The design text used `tk_test_end` for both.
typedef struct { int32_t how; int32_t code; } tk_test_outcome;
// tk_test_run — run ONE `#test` body, capturing any exit the body itself provokes.
//
// THE JUMP LIVES HERE, and that is why generated code never has to know it exists: the emitted C (and
// any future own-backend harness) only has to take the ADDRESS of a top-level function and pass it.
// setjmp has requirements no general code generator wants to honour — the frame that called it must
// stay alive, and a local modified between the setjmp and the longjmp is indeterminate unless it is
// `volatile`. `body` is `volatile` for exactly that reason: it is the one setjmp requirement that
// cannot be delegated.
// tk_test_report — write ONE test's verdict block and count it into the run's tally.
//
// Replaces the bare `tk_test_end()` of the passing path and the abort of the failing one: `ok` closes
// the channel as before, a PANICKED body closes it with FAILED (its panic line already sits in THIS
// test's captured stderr, never on the shared stream), and an EXITED body closes it with FAILED plus
// the code it tried to leave with.
// tk_test_summary — the run's one closing block: how many tests ran, passed, failed, and how many of
// the failures were an `exit` rather than a panic. A COUNT, not a reconstruction — the harness now
// always reaches the end, so the numbers are sums.
// tk_test_any_failed — did any `#test` of this run end other than `ok`? The gate binary's exit status.
// --- the GUARD's way in ---------------------------------------------------------------------------
//
// §14.3 of the design gives the capture a Teko surface, `run_capturing(body: cabi fn())`. MEASURED:
// that surface does not exist and cannot be written today — `cabi` is not a token this lexer mints
// and `fn() -> T` in parameter position does not parse. C0 does not need it (the `#test` harness is
// emitted C, and `&<symbol>` asks the language for nothing), but the GUARD does need a way in, and a
// guard that cannot fail is decoration.
//
// So the runtime owns the probe bodies and a `#test` reaches them through one ordinary extern call.
// Each `TK_TEST_PROBE_*` selector names a body whose ending is known, so the guard can assert that
// the body RAN and that the ending HAPPENED — not merely that the process is still alive, which a
// test that never ran a body would satisfy too.
#define TK_TEST_PROBE_RETURNS 0   /* a body that returns normally      -> TK_TEST_OK */
#define TK_TEST_PROBE_PANICS  1   /* a body that panics explicitly     -> TK_TEST_PANICKED */
#define TK_TEST_PROBE_EXITS   2   /* a body that calls exit()          -> TK_TEST_EXITED */
#define TK_TEST_PROBE_DIV0    3   /* a body that divides by zero       -> TK_TEST_PANICKED */
// TK_TEST_PROBE_EXIT_CODE — the value TK_TEST_PROBE_EXITS tries to leave with, and which the capture
// must hand back in `code`. Nonzero and not 1, so it cannot be confused with a status the harness
// itself produces.
#define TK_TEST_PROBE_EXIT_CODE 7
// TK_TEST_PROBE_UNKNOWN — the `how` of a selector that names no body. Distinct from all three real
// results so an out-of-range selector is a reported failure rather than a silent pass.
#define TK_TEST_PROBE_UNKNOWN 3
// tk_test_capture_probe — run the body `which` names under the capture and answer HOW it ended (one
// of the `TK_TEST_*` results, or TK_TEST_PROBE_UNKNOWN when `which` names no body).
//
// IT ANSWERS A SCALAR AND NOT THE `tk_test_outcome` PAIR, and that is measured rather than chosen: a
// `from "teko_rt"` extern cannot return a user struct by value on this tree. The typer allows a
// `Named` return for teko_rt externs, but codegen emits NO prototype for them — it relies on this
// header — and the header's C struct type is not the mangled Teko one, so the generated call site is
// an `invalid initializer`. The exit CODE is therefore a second read.
// tk_test_capture_last_code — the `code` recorded by the most recent tk_test_capture_probe.
// tk_print — write exactly s.len bytes from s.ptr to stdout; no newline, no NUL.
void tk_print(tk_str s);
// tk_println — tk_print(s) then a single '\n' (0x0A).
void tk_println(tk_str s);
/* Flush stdout now, so a name printed before a crash is not lost in the block buffer.
   See teko_rt.c for the misdiagnosis that motivated it. */
// Host output FFI bottoms (scope.c write/ewrite/eprint/eprintln) — s.len bytes, NUL-tolerant.
// write → stdout; ewrite/eprint → stderr; eprintln → stderr + '\n'.
void tk_write(tk_str s);
void tk_ewrite(tk_str s);
void tk_eprint(tk_str s);
void tk_eprintln(tk_str s);
// teko::float::parse(str) -> f64 (strtod over a NUL-terminated copy; non-numeric → 0.0).
double tk_float_parse(tk_str s);
// Native backend bridge: tk_float_parse returning IEEE-754 bits as u64 (to work around LCall
// result-class limitation). The native lowering intercepts teko::float::parse and routes through
// this, then bitcasts the u64 back to f64 on the assignment side.
uint64_t tk_rt_float_parse_bits(tk_str s);

// --- string interpolation `$"…{expr}…"` builders (self-host parity) ---
// These are EXTERN (linked from teko_rt.c), NOT static inline — the generated C calls them
// via a forward declaration (like tk_print) rather than including this header. Leak-tolerant
// (M.5 — the results are short-lived process-lifetime buffers).
//
// tk_str_concat — a fresh str holding a's bytes then b's bytes; the result OWNS the buffer.
tk_str tk_str_concat(tk_str a, tk_str b);
// (0.3.1 modelo-de-memoria §9) tk_str_concat_r — tk_str_concat with the result buffer bump-allocated
// in `r` (tk_region_alloc) instead of malloc'd: the str lives in `r` and dies when `r` is dropped, so
// a str produced inside a `{}` can participate in death-by-scope. `r == tk_region_root()`/program (or
// NULL) reproduces the leak-tolerant malloc behaviour of tk_str_concat.
// (C7.1a) FFI marshalling: a fresh NUL-terminated C copy of a str (teko::mem::as_cstr), and a
// copy of a NUL-terminated foreign C string back into a fresh str (teko::mem::str_from_cstr).
void *tk_cstr_dup(tk_str s);
// (tk_bytes_from_ptr is declared after tk_ffi_bytes, below — its return type is defined there.)
// tk_i64_to_str / tk_u64_to_str — the integer's DECIMAL text in a fresh str. The interp
// lowering widens every signed int hole to i64 and every unsigned hole to u64 (EVERY Teko
// integer prim fits — 64 bits is the language's widest integer; the checker scopes holes to
// what the corpus needs).

// tk_str_concat_len / tk_i64_to_str_len / tk_u64_to_str_len — the SAME three builders above, with
// their result's length handed back through an OUT-PARAMETER instead of `tk_str`'s second field.
// A genuine C caller never needs these (a `tk_str` return already carries both halves); they exist
// for the native backend's own `LCall`, whose result capture is ONE register
// (`select_call_result_x86`/`select_call_result_arm64`) — the same width a plain scalar/pointer
// call answers with, one short of the two-eightbyte SysV/AAPCS64 register pair a `tk_str`-by-value
// return actually occupies. Mirrors `tk_slice_push`'s own `(ptr, len, &item, esz, &out_len)`
// shape: the pointer half rides the ordinary return value, the length rides `*out_len`. Thin
// wrappers — each still calls its own two-word twin above and owns no logic of its own
// (0.3.1.0 degrau 9).
const tk_byte *tk_str_concat_len(const tk_byte *a_ptr, uint64_t a_len, const tk_byte *b_ptr, uint64_t b_len, uint64_t *out_len);

// --- Phase 3 str/byte stdlib (the four recognized-but-not-yet-lowered builtins) ---
// Same contract as tk_str_concat: a fresh malloc'd buffer the result OWNS, tk_panic on OOM
// (M.1), leak-tolerant (M.5 — short-lived).
//
// tk_str_of_bytes — a fresh str COPYING the bytes of a []byte slice. A []byte slice has the
// SAME {ptr,len} shape as tk_str, so codegen passes the slice value directly; this copies it
// into a fresh owned buffer (the `str_of_bytes` builtin; the `str` builtin aliases it).
tk_str tk_str_of_bytes(tk_str bytes);
// tk_str_of_bytes_len — the out-parameter-length twin of `tk_str_of_bytes` (mirrors
// `tk_str_concat_len`'s own doc): the native backend's `LCall` reads exactly one result
// register, never the true 2-eightbyte SysV/AAPCS64 struct `tk_str_of_bytes` returns by value,
// so the copied buffer's pointer rides the return register and its length rides `*out_len`.
const tk_byte *tk_str_of_bytes_len(const tk_byte *ptr, uint64_t len, uint64_t *out_len);
// tk_one_byte — a fresh 1-byte str holding c.
// tk_one_byte_len — the out-parameter-length twin of `tk_one_byte` (mirrors `tk_str_of_bytes_len`'s
// own doc): the native backend's `LCall` reads exactly one result register, never the true
// 2-eightbyte SysV/AAPCS64 struct `tk_one_byte` returns by value, so the fresh buffer's pointer
// rides the return register and its length (always 1) rides `*out_len`. The byte travels in a
// FULL-WIDTH parameter — the same shape every other `_len` twin uses for its scalar arguments
// (`tk_i64_to_str_len`, `tk_str_slice_len`) — because SysV/AAPCS64 leave the bits ABOVE a
// sub-register-width argument unspecified, and this backend has no argument-narrowing pass
// (`apply_native_c_return_narrow` narrows RETURNS only); the low 8 bits are taken here instead
// (0.3.1.0 degrau 32).
// tk_char_to_u32 — decode a `char` (its 1–4 UTF-8 bytes) to the scalar codepoint value. The bytes
// are valid UTF-8 by construction (the lexer validated the literal), so this is a pure decode.
uint32_t tk_char_to_u32(tk_char c);
// tk_slice_char — the runtime layout of a `[]char` slice (the generated C holds char arrays here).
typedef struct { tk_char *ptr; uint64_t len; } tk_slice_char;
// tk_str_concat3 REMOVED (2026-07-01) — superseded by `concat(params pieces: []str)`, bridged
// at the call site (codegen.c/.tks) by folding N pieces via tk_str_concat; no runtime symbol needed.
// --- Phase 3 str query/slice builtins (the checker types these via tk_builtin_fn) ---
// The query helpers (eq/ends_with/contains/len) do NO allocation; the slice helpers return a
// FRESH owned buffer (same ownership as tk_str_concat), tk_panic on OOM/out-of-range (M.1).
//
// tk_str_eq — true iff a and b have the same length and the same bytes (memcmp; embedded NUL
// tolerated). No allocation.
bool tk_str_eq(tk_str a, tk_str b);
// tk_char_eq — true iff a and b decode the SAME codepoint. Compares by BYTES (same length and
// same bytes, memcmp), not by decoded scalar: the lexer only ever produces the ONE valid UTF-8
// encoding for a given codepoint, so byte-equality and codepoint-equality coincide, and comparing
// bytes needs no decode. (0.3.1.0 — `char ==`/`!=`, the language-hole fix: the C route rejected
// `==` on the raw {ptr,len} struct outright, and the native route had no PrimKind for `char`.)
bool tk_char_eq(tk_char a, tk_char b);
// tk_slice_eq_bytes — `[]T == []T` VALUE equality (mirrors Go's slice-by-value comparison, per
// the owner's 2026-07-29 four-references ruling) for a POD element `T` with no interior pointer
// (an integer/bool/byte prim): same length AND `memcmp` of the packed backing array. Safe for
// these kinds specifically because a homogeneous C array of a fixed-width scalar has no padding
// between elements and no representation for one value that isn't also byte-identical to every
// other encoding of that SAME value. NOT used for `float`/`char`/`str` elements — see their own
// dedicated helpers below (a float's bit pattern is not its value equality, `-0.0`/`NaN`; a
// char/str element's `{ptr,len}` pair differs by BACKING ADDRESS even when the two elements hold
// the same content).
bool tk_slice_eq_bytes(const void *a_ptr, uint64_t a_len, const void *b_ptr, uint64_t b_len, uint64_t elem_size);
// tk_slice_f32_eq / tk_slice_f64_eq — `[]f32 == []f32` / `[]f64 == []f64`: same length AND every
// element equal by the IEEE `==` (NOT memcmp — `-0.0 == 0.0` are value-equal but bit-distinct,
// and `NaN != NaN` even though its bits equal themselves).
bool tk_slice_f32_eq(const float *a_ptr, uint64_t a_len, const float *b_ptr, uint64_t b_len);
bool tk_slice_f64_eq(const double *a_ptr, uint64_t a_len, const double *b_ptr, uint64_t b_len);
// tk_slice_char_eq — `[]char == []char`: same length AND every element `tk_char_eq`. A `[]char`
// element is byte-for-byte a `tk_char` (both the `{uint8_t*,uint64_t}` shape), so the backing
// array IS an array of `tk_char` and needs no bridging.
bool tk_slice_char_eq(const tk_char *a_ptr, uint64_t a_len, const tk_char *b_ptr, uint64_t b_len);
// tk_slice_str_eq — `[]str == []str`: same length AND every element `tk_str_eq`. A `[]str`
// element is byte-for-byte a `tk_str`, for the same reason `tk_slice_char_eq` needs no bridging.
bool tk_slice_str_eq(const tk_str *a_ptr, uint64_t a_len, const tk_str *b_ptr, uint64_t b_len);
// (TR3) tk_str_hash — FNV-1a over the str's bytes (offset basis 14695981039346656037, prime
// 1099511628211, u64 wraparound); an empty str hashes to the offset basis. No allocation.
uint64_t tk_str_hash(tk_str s);
// (TR3) tk_str_cmp — lexicographic byte compare (unsigned): -1 (a < b) / 0 (equal) / 1 (a > b);
// the shorter str is the lesser when one is a prefix of the other. No allocation.
int64_t tk_str_cmp(tk_str a, tk_str b);
// (enabling primitive — staged off; no compiler source calls this yet) a HEAP-BACKED (never
// arena-backed) string intern table, keyed and valued by content (not by identity): a memoization
// cache for codegen's repeated string-builders (e.g. a variant's C type name, rebuilt identically
// at every use site today). Heap-only BY DESIGN — an arena-backed cache would dangle across the
// coming Boundary-A rewind (tk_arena_pop / tk_arena_commit); a libc block never does.
//
// tk_intern_get — a FRESH, INDEPENDENT copy of the value cached for `key` (never a view into the
// table's own entry — a view would dangle the instant a later tk_intern_put/tk_intern_reset
// overwrites or frees that entry, breaking Teko's str value-semantics contract), or the empty str
// ({NULL,0}) on a miss. Safe as a miss sentinel because every real cache value here (a
// synthesized type name) is non-empty.
tk_str tk_intern_get(tk_str key);
// tk_intern_put — cache `value` under `key` (both copied into fresh heap blocks the table owns),
// overwriting any prior entry for an equal key; returns `value` UNCHANGED so a call composes as
// `tk_intern_put(key, computed)` inline. OOM panics (M.1).
tk_str tk_intern_put(tk_str key, tk_str value);
// tk_intern_reset — free every cached entry. The per-pass boundary: call before a fresh
// compilation pass that must not observe a PRIOR pass's cached strings.
void   tk_intern_reset(void);
// tk_str_slice — the substring bytes [start, end) as a fresh owned str. An out-of-range slice
// (start > end, or end > s.len) PANICS (M.1, fail-loud on out-of-bounds). The empty
// slice (start == end) is a valid empty str (1-byte buffer so ptr is never NULL+stale len).
tk_str tk_str_slice(tk_str s, uint64_t start, uint64_t end);
// tk_str_slice_to — tk_str_slice(s, 0, end).
tk_str tk_str_slice_to(tk_str s, uint64_t end);
// tk_str_slice_from — tk_str_slice(s, start, s.len).
tk_str tk_str_slice_from(tk_str s, uint64_t start);

// tk_str_slice_len / tk_str_slice_to_len / tk_str_slice_from_len — the SAME three slice builders
// above, with their result's length handed back through an OUT-PARAMETER instead of `tk_str`'s
// second field. Same reason and shape as `tk_str_concat_len` (above): the native backend's own
// `LCall` result capture is ONE register, one short of the two-eightbyte SysV/AAPCS64 register
// pair a `tk_str`-by-value return actually occupies. Thin wrappers — each still calls its own
// two-word twin above and owns no logic of its own (0.3.1.0 degrau 11).
const tk_byte *tk_str_slice_len(const tk_byte *s_ptr, uint64_t s_len, uint64_t start, uint64_t end, uint64_t *out_len);
const tk_byte *tk_str_slice_to_len(const tk_byte *s_ptr, uint64_t s_len, uint64_t end, uint64_t *out_len);
const tk_byte *tk_str_slice_from_len(const tk_byte *s_ptr, uint64_t s_len, uint64_t start, uint64_t *out_len);
// tk_str_len — s.len (the byte length). No allocation.
uint64_t tk_str_len(tk_str s);
// tk_str_ends_with — true iff s ends with suffix (suffix.len <= s.len and the tail bytes
// match). No allocation.
bool tk_str_ends_with(tk_str s, tk_str suffix);
// tk_str_contains — true iff needle occurs in s (naive byte search; an empty needle → true).
// No allocation.
bool tk_str_contains(tk_str s, tk_str needle);

// =========================================================================
// Host-FFI + arithmetic bottoms (Phase 7 / scope.c builtin_fn surface).
//
// THE LIFTING SEAM. The host I/O builtins return Teko `T | error` / `error?`
// sums and `[]str`. Those are GENERATED struct types (tk_u_str_error,
// tk_opt_error, tk_slice_str, …) defined in the emitted C, AFTER this header is
// #included — so the runtime CANNOT name them. Instead each primitive returns a
// FIXED-ABI result struct over header-knowable types only (tk_str + scalars +
// tk_str*), and codegen lifts that into the program's result type (the
// emit_host_ffi statement-expression). `error` lowers to its message tk_str.
// =========================================================================

// str | error  (read_file, var): ok → value; !ok → err (the message).
typedef struct { bool ok; tk_str value; tk_str err; } tk_ffi_sres;
// error?  (write_file, chdir): ok → success (no error); !ok → err present.
typedef struct { bool ok; tk_str err; } tk_ffi_ures;
// []str | error  (list_dir): ok → {ptr,len} entries; !ok → err.
typedef struct { bool ok; tk_str *ptr; uint64_t len; tk_str err; } tk_ffi_slres;
// u64 | error  (last_index_of): ok → value; !ok → not found.
typedef struct { bool ok; uint64_t value; } tk_ffi_u64res;
// []byte  (bytes_from_ptr): a {ptr,len} the codegen lifts to the generated tk_slice_byte (C7.1a).
typedef struct { tk_byte *ptr; uint64_t len; } tk_ffi_bytes;
// (C7.1a) copy n octets from a foreign pointer into a fresh []byte (teko::mem::bytes_from_ptr).
tk_ffi_bytes tk_bytes_from_ptr(const void *p, uint64_t n);

// str_from_utf8(bytes) — the validated bytes -> str constructor (ROUND 0 / B.36). ok → a fresh
// str COPYING the bytes; !ok → err "invalid UTF-8". Reuses tk_ffi_sres (same {ok,value,err}
// shape as read_file/getenv). Takes ptr+len (the []byte ABI), mirroring write_file_bytes's arg.
tk_ffi_sres tk_rt_str_from_utf8(const tk_byte *ptr, uint64_t len);
// tk_rt_str_from_utf8_ok — the native backend's OWN-REGISTER twin of `tk_rt_str_from_utf8`
// (mirrors `tk_rt_last_index_of_ok`'s own doc, 0.3.1.0 degrau 21): the real `tk_ffi_sres` ABI
// (three eightbytes) is wider than the ONE result register this backend's `LCall` captures, so
// the found/failed flag travels back as this function's OWN single-register `bool` return, and
// the ACTIVE case's (ptr, len) pair — the decoded str on success, the "invalid UTF-8" message on
// failure — travels back through the SAME two out-parameters either way; the caller's own `bool`
// decides which meaning to read (`lower_str_from_utf8_call`'s doc, lir/lower.tks).
bool tk_rt_str_from_utf8_ok(const tk_byte *ptr, uint64_t len, const tk_byte **out_ptr, uint64_t *out_len);

// teko::io::read_file(path) — slurp the whole file as UTF-8 bytes (owned copy).
tk_ffi_sres tk_rt_read_file(tk_str path);
// (DT3) teko::io::read_line() — read one LINE from stdin (the trailing '\n'/"\r\n" stripped),
// an owned copy — empty when stdin has no more input (check tk_rt_stdin_eof() to tell that
// apart from a genuine blank line). A DIRECT `str` return (no {ok,value,err} lift) so this
// brand-new primitive stays lowerable by every codegen generation, including the released
// bootstrap seed's frozen codegen.c, which can only special-case ALREADY-KNOWN {ok,value,err}
// shapes by name (mirrors tk_rt_version's already-working plain-str shape).
tk_str tk_rt_read_line(void);
// (DT3) teko::io::stdin_eof() — did the LAST read_line() hit real EOF (stdin fully exhausted)?
bool tk_rt_stdin_eof(void);
// (#229) teko::io::read_stdin() — slurp all of stdin until EOF (owned copy), for `teko fmt -`.
// A bare tk_str (no error union — see tk_rt_read_stdin's definition for why); panics on a
// genuine read failure.
tk_str tk_rt_read_stdin(void);
// (0.3.1.0 LSP crumb 0) read EXACTLY `n` bytes from stdin into an arena-owned []byte slice,
// blocking until `n` bytes have arrived or stdin hits EOF. The RETURNED slice's length is the
// count actually read: `len == n` on a full body, `len < n` on EOF mid-body (a truncated frame,
// which the LSP transport surfaces as an honest error rather than a silent accept). Needed
// because the JSON-RPC / LSP base-protocol body is `Content-Length`-framed with NO trailing
// newline, so the line reader (`tk_rt_read_line`) cannot bound it — the caller knows the exact
// byte count from the header and reads precisely that many. A `tk_slice_byte` return (the same
// {ptr,len} ABI `tk_rt_secure_bytes` already uses) so a brand-new primitive stays lowerable by
// the released bootstrap seed's frozen codegen (the generic user-`extern` path emits the raw C
// symbol directly — no per-name lift, which only `str | error`-shaped externs would need).
// teko::env::var(name) — the environment value, or error when unset.
tk_ffi_sres tk_rt_getenv(tk_str name);
// teko::io::write_file(path, content) — (over)write the file; error on failure.
tk_ffi_ures tk_rt_write_file(tk_str path, tk_str content);
// (0.3.1.0) teko::io::append_file(path, content) — APPEND `content` to the file at `path`, creating
// it when absent. 0 on success, TK_RT_APPEND_FAILED when the file could not be opened or the bytes
// could not all be written.
//
// WHY A PRIMITIVE AND NOT A READ-MODIFY-WRITE. `teko::process::verdict_emit` used to append by
// reading the WHOLE file back, concatenating, and rewriting it. Two arena allocations the size of
// the file per record, and the arena frees ONCE, at process exit (`tk_regions_free_all` — "free
// EVERY still-live region"), so the bytes allocated over N records are the SUM of 1..N file sizes:
// QUADRATIC, and never reclaimed. Measured on the regression tier: 8.4 GB anon-rss on the base.
// One `open(O_APPEND)`+`write` per record makes that linear.
//
// A SCALAR RETURN AND NOT `tk_ffi_ures`, for the reason `teko_rt.h` already records for
// `tk_rt_read_line`: a brand-new `error`-shaped extern needs a per-name lift in codegen that the
// RELEASED bootstrap seed's frozen codegen cannot learn. `teko::io::append_file` is a plain Teko
// wrapper over this scalar (src/io/io.tks), so the caller still sees `error | null`.
//
// THE RACE NARROWS, AND ONLY AS FAR AS THE HOST ALLOWS. POSIX specifies that a `write` to a
// descriptor opened `O_APPEND` seeks to end and writes ATOMICALLY with respect to other writers, so
// two processes appending small records to one path can no longer overwrite each other — the window
// the old read-modify-write left wide open. NOT AN UNLIMITED PROMISE: a `write` may still be
// PARTIAL above some size, and this function then loops to finish it, which is no longer one atomic
// act. WHERE THAT SIZE LIES IS NOT MEASURED HERE. And the MSVC CRT's `_O_APPEND` is documented as a
// seek-to-end followed by a write, not one atomic operation, so the Windows arm keeps the race it
// always had; naming it is the honest move, hiding it behind the POSIX guarantee is not.
#define TK_RT_APPEND_FAILED (-1)
int32_t tk_rt_append_file(tk_str path, tk_str content);
// teko::io::write_file_bytes(path, data) — write a raw []byte slice to the file; error on failure.
// Takes the byte list as a ptr+len pair (the C7.14 tk_byte_list ABI). Shares the same
// write-path as tk_rt_write_file; accepts binary data (not UTF-8-restricted).
tk_ffi_ures tk_rt_write_file_bytes(tk_str path, const tk_byte *ptr, uint64_t len);
// teko::env::chdir(path) — change the process working directory; error on failure.
tk_ffi_ures tk_rt_chdir(tk_str path);
// teko::fs::mkdir(path) — create a directory (mode 0755); SUCCESS if it already exists.
tk_ffi_ures tk_rt_mkdir(tk_str path);
// (issue #79) teko::fs::remove_file(path) — delete a file (libc remove); SUCCESS if already absent.
tk_ffi_ures tk_rt_remove_file(tk_str path);
// teko::env::cwd() — the current working directory as an owned absolute path, or error.
tk_ffi_sres tk_rt_getcwd(void);
// teko::env::set_var(name, value) — set an environment variable; error on failure.
tk_ffi_ures tk_rt_setenv(tk_str name, tk_str value);
// teko::fs::list_dir(path) — the directory entries (excluding "." / ".."), or error.
tk_ffi_slres tk_rt_list_dir(tk_str path);
// teko::str::last_index_of(hay, needle) — byte index of the LAST occurrence, or not-found.
tk_ffi_u64res tk_rt_last_index_of(tk_str hay, tk_str needle);
// tk_rt_last_index_of_ok — the SAME search, in the single-register-return + out-parameter shape
// the native (non-C) backend's own `LCall` can capture (it reads exactly one result register,
// never the true 2-eightbyte SysV/AAPCS64 struct pair `tk_ffi_u64res` returns by value): the
// found/not-found flag rides the ordinary `bool` return, the found index rides `*out_index`
// (untouched when not found). A thin wrapper over `tk_rt_last_index_of` — no scan logic is
// duplicated (0.3.1.0 degrau 15).
bool tk_rt_last_index_of_ok(tk_str hay, tk_str needle, uint64_t *out_index);
// --- native-backend own-register twins for the aggregate-returning host FFI (0.3.1.0 degrau 34) ---
// The native (non-C) backend's `LCall` captures exactly ONE result register, so a host primitive
// whose true ABI returns a struct wider than a register (`tk_ffi_sres` is 40 bytes → SysV MEMORY
// class → SRET hidden pointer; `tk_ffi_ures`/`tk_ffi_slres` likewise) cannot be received on that
// path — an un-set-up SRET pointer makes the callee write its result over garbage and the caller
// SIGSEGVs (measured: gen2 crashed in `tk_rt_getenv` at startup, `c_backend_selected`'s
// TEKO_BACKEND check). Each twin below carries the ok/err flag as its OWN single-register `bool`
// return and the active case's (ptr, len) through two shared out-parameters — the SAME convention
// `tk_rt_str_from_utf8_ok`/`tk_rt_last_index_of_ok` already use, extended to the whole host surface
// the self-host build walks (env/io/fs). Args are taken as ptr+len scalar pairs (never `tk_str`
// by value), so the pair IS their C ABI on every target and the backend passes them in registers.
//
// str | error family — ok → the value's (ptr, len) in the out-slots; !ok → the error message's
// (ptr, len) in the SAME out-slots (the caller's `bool` decides which meaning to read).
bool tk_rt_getenv_ok(const tk_byte *name_ptr, uint64_t name_len, const tk_byte **out_ptr, uint64_t *out_len);
bool tk_rt_getcwd_ok(const tk_byte **out_ptr, uint64_t *out_len);
// []str | error — ok → the tk_str[] entries' (base, count) in the out-slots (a `[]str` slice's
// element layout IS an array of `tk_str`, so the backend stores that pair as the fat payload
// unchanged); !ok → the error message's (ptr, len) in the SAME out-slots.
bool tk_rt_list_dir_ok(const tk_byte *path_ptr, uint64_t path_len, const tk_byte **out_ptr, uint64_t *out_len);
// error | null family — the `bool` return IS "succeeded (no error)"; on failure the error message's
// (ptr, len) travels through the two out-slots (untouched on success).
bool tk_rt_setenv_ok(const tk_byte *name_ptr, uint64_t name_len, const tk_byte *value_ptr, uint64_t value_len, const tk_byte **out_err_ptr, uint64_t *out_err_len);
bool tk_rt_chdir_ok(const tk_byte *path_ptr, uint64_t path_len, const tk_byte **out_err_ptr, uint64_t *out_err_len);
bool tk_rt_mkdir_ok(const tk_byte *path_ptr, uint64_t path_len, const tk_byte **out_err_ptr, uint64_t *out_err_len);
bool tk_rt_remove_file_ok(const tk_byte *path_ptr, uint64_t path_len, const tk_byte **out_err_ptr, uint64_t *out_err_len);
// TK_RT_SPAWN_FAILED — "the runtime could not start a child at all", as distinct from any status a
// child could report. It is NEGATIVE on purpose: a real child's code is 0..255 (128+signo for a
// signalled one, so <= 191), which makes a negative provably outside that space and therefore
// unambiguous. This replaces the old sentinel 127, which collided with THREE other meanings at
// once — the POSIX exec-failed convention, `sh`'s own "command not found", and a child that simply
// chose to exit 127 — so a CI failure reading `exit 127` could not be attributed to any of them.
// What is STILL ambiguous, honestly: the POSIX child arm must `_exit(127)` after a failed execvp,
// because the exec'd-image failure has no other channel; that one case remains indistinguishable
// from a child exiting 127. Everything the PARENT can see is now separated.
#define TK_RT_SPAWN_FAILED (-1)
// teko::process::run(argv) — fork/exec argv[0] with argv, wait, return its exit status
// (TK_RT_SPAWN_FAILED when argv is empty or the child could not be started at all). Takes the
// slice as ptr+len (no generated type).
int32_t tk_rt_run(const tk_str *argv, uint64_t n);
// teko::process::run_quiet(argv) — same contract as tk_rt_run, but the child's stdout/stderr
// are redirected to the null device (issue #73: the cc flag-family probe uses this so a
// deliberately-rejected compiler flag doesn't print to the user's build output).
int32_t tk_rt_run_quiet(const tk_str *argv, uint64_t n);
// (0.3.1.2 — process-half regression harness) teko::process::spawn_redirected(argv, dir, env,
// in_path, out_path, err_path) — launch `argv` WITHOUT waiting for it, with its three standard
// streams redirected to the given paths ("" inherits the parent's own descriptor) and its working
// directory set to `dir` ("" or "." inherits the parent's). The returned handle is opaque (POSIX:
// the pid; Windows: the process HANDLE cast to intptr_t) and negative (TK_RT_SPAWN_FAILED) only
// when no child ever started — the same sentinel tk_rt_run uses, for the same reason (a real
// handle can never be negative).
//
// ONE `str` PARAMETER, NOT SIX — measured (process.tks: TOKEN_SEP/spawn_payload): six `str`
// parameters checked cleanly but the native x86-64 backend does not yet spill a call argument past
// its six-register window (`isel x86-64: B1-args`), and each `str` is a two-word fat pointer — six
// of them need twelve registers where the ABI offers six. `payload` is `spawn_payload`'s
// self-describing flattening of all six logical arguments (a decimal element count precedes each
// variable-length list): `<argc>` NUL `argv[0]` … `argv[argc-1]` NUL `<envc>` NUL `env[0]` …
// `env[envc-1]` NUL `dir` NUL `in_path` NUL `out_path` NUL `err_path`. Safe because none of an argv
// element, an environment token, or a path may itself carry a NUL byte — every OS API underneath
// (execvp, CreateProcess, open) is NUL-terminated already. `env` tokens are "K=V" and are ADDED to
// the child's inherited environment; the parent's own environment is never touched.
//
// (0.3.1.0 F5) THREE MORE TOKENS AT THE END, AND THE PATH ROUTE IS UNCHANGED: `<in_fd>` NUL
// `<out_fd>` NUL `<err_fd>`, each a decimal DESCRIPTOR the caller already owns (a `tk_rt_pipe` end)
// or EMPTY for "this slot names no descriptor". A descriptor WINS over the path in the same slot.
// Appending is safe for a payload written by an older producer — the splitter yields an empty token
// past the payload's end, and an empty token reads as TK_RT_FD_NONE — which is what lets the frozen
// bootstrap twin, which emits the seven-token shape, keep working against this parser unchanged.
//
// OWNERSHIP SPLITS ON WHO OPENED IT. A descriptor opened HERE from a path is closed here in the
// parent right after the fork, as it always was. A descriptor the CALLER passed in is never closed
// here — the caller's `tk_rt_close_fd` is the only thing that closes it, because the parent's copy
// of a pipe's write end is precisely what has to go away for the read end to see end-of-file, and a
// runtime that closed it behind the caller's back would break the caller sharing one write end
// across several children.
//
// EVERY REDIRECTION TARGET IS OPENED BY THE PARENT, BEFORE THE CHILD'S WORKING DIRECTORY CHANGES —
// a relative in/out/err path is therefore resolved against the PARENT's cwd, never the child's
// `dir`, matching the "the parent NAMES the descriptor, the argv's own cwd is the child's separate
// concern" contract callers rely on.
int64_t tk_rt_spawn_redirected(tk_str payload);
// (0.3.1.2) teko::process::wait_one(raw) — block until the child `raw` (a `spawn_redirected`
// handle) exits, and return its status: the same 0..255 / 128+signo reading `tk_rt_run` already
// documents. TK_RT_SPAWN_FAILED when `raw` never identified a real child (it was already negative,
// or the wait itself could not observe one). A handle may be waited on at most once — POSIX reaps
// the pid and Windows closes the HANDLE, so a second wait on the same value is undefined on both.
int32_t tk_rt_wait_one(int64_t raw);

// =========================================================================
// (0.3.1.0 F5) ANONYMOUS PIPES — the per-child descriptor pair the concurrency design wants where
// `spawn_redirected` today can only name a FILE PATH. Counted before this block was written:
// `pipe(` 0 occurrences in the tree, `fork(` 3, `dup2(` 7 — fork and dup2 were already here and the
// pipe was the one missing piece, which is exactly why every capture in this repository goes
// through a scratch file (`.regr-pool-*`, `.toolquery.{in,out,err}`) instead of a descriptor.
//
// TK_RT_FD_NONE — "this is not a descriptor". Serves both roles, and they are the same claim: a
// redirection slot that names no descriptor, and a call that produced none (a failed `pipe`, a
// failed `close`). NEGATIVE for the reason TK_RT_SPAWN_FAILED is: a real descriptor is >= 0 on both
// hosts, so the failure can never be read as a descriptor.
#define TK_RT_FD_NONE (-1)
// The three readings of tk_rt_fd_wait_readable, on their own axis (they are not descriptors):
// READY means "a read will not block" — which on a pipe covers BOTH pending bytes and end-of-file,
// exactly as `poll` reports it; TIMEOUT means the deadline expired with neither; ERROR means the
// descriptor could not be observed at all.
#define TK_RT_FD_WAIT_ERROR   (-1)
#define TK_RT_FD_WAIT_TIMEOUT (0)
#define TK_RT_FD_WAIT_READY   (1)
// TK_RT_PIPE_CAPACITY — the kernel buffer requested for a pipe. POSIX `pipe()` takes no size (the
// host picks; Linux 64 KiB), so this number is only ever READ on Windows, whose `_pipe` demands
// one; it is spelled to match the Linux default so a child that fills the buffer blocks at the same
// point on both hosts rather than at two silently different ones.
#define TK_RT_PIPE_CAPACITY (65536)
// teko::process::pipe() — create an anonymous unidirectional pipe and return BOTH descriptors
// PACKED into one i64: the read end in the low 32 bits, the write end in the high 32 bits.
// TK_RT_FD_NONE when the host refused.
//
// PACKED AND NOT TWO OUT-PARAMETERS, for the reason `tk_rt_spawn_redirected` takes one flattened
// `str` and not six: the native backend's `LCall` captures exactly ONE result register, and a pipe
// is created ATOMICALLY — two calls could not return the two halves of the same pipe. Two 32-bit
// halves fit one register with room to spare (a descriptor is a small non-negative `int` on both
// hosts), and the packed value stays non-negative, so it never collides with TK_RT_FD_NONE.
//
// NEITHER END IS INHERITED BY ACCIDENT. POSIX sets FD_CLOEXEC on both and Windows opens the pair
// `_O_NOINHERIT`, so an UNRELATED child launched between the pipe's creation and its use never gets
// a copy — which is what keeps the end-of-file reading meaningful (one stray inherited write end in
// any other process holds the pipe open forever). The child that IS meant to have it gets it
// through `tk_rt_spawn_redirected`, whose POSIX arm `dup2`s it onto a standard stream (dup2 clears
// FD_CLOEXEC on the target) and whose Windows arm duplicates it inheritable for CreateProcess.
int64_t tk_rt_pipe(void);
// tk_rt_pipe_read_fd / tk_rt_pipe_write_fd — the two halves of a `tk_rt_pipe` packing, unpacked.
// TK_RT_FD_NONE in, TK_RT_FD_NONE out, so a failed `pipe` propagates through both accessors instead
// of decoding into the descriptor pair (-1, -1) minus the sign.
// teko::process::close_fd(fd) — close one descriptor. 0 on success, TK_RT_FD_NONE on failure or
// when `fd` was never a descriptor.
//
// THE CALLER CLOSES, AND THAT IS THE WHOLE CONTRACT. `tk_rt_spawn_redirected` never closes a
// descriptor the caller handed it (it closes only the ones IT opened from a path) — the parent owns
// its copy of the write end and must close it, or the read end never reaches end-of-file. That is
// the classic pipe defect, and it is named here rather than hidden because the alternative — the
// runtime closing a descriptor it does not own — would break the caller that wants ONE write end
// shared by several children.
int32_t tk_rt_close_fd(int64_t fd);
// teko::process::fd_wait_readable(fd, timeout_ms) — block until reading `fd` would not block, or
// until `timeout_ms` elapses. TK_RT_FD_WAIT_READY / _TIMEOUT / _ERROR (above). A negative
// `timeout_ms` waits without a deadline.
//
// A DEADLINE IS NOT A CONVENIENCE HERE. A read on a pipe whose write end is still open anywhere
// blocks FOREVER, and a test that blocks forever does not go red — it hangs, and a hung suite never
// reports at all. Every reader in this repository's tests goes through this call so the failure is
// a value that can be asserted on.
//
// THE TWO HOSTS ARE NOT SYMMETRIC HERE, AND THE ASYMMETRY IS REAL. POSIX blocks inside `poll` with
// the exact deadline: it wakes on the event, and the deadline is the kernel's. Windows anonymous
// pipes (`_pipe`/`CreatePipe`) are NOT waitable objects and cannot be opened overlapped, so MSVC
// offers no blocking-with-deadline call for them at all; the Windows arm therefore POLLS with
// `PeekNamedPipe` every TK_RT_PIPE_POLL_INTERVAL_MS. Consequence, stated as a number rather than
// hidden: on Windows a wakeup is late by up to one interval (2 ms) and a deadline can overshoot by
// the same, where POSIX overshoots by the scheduler's granularity only. Closing that gap needs a
// NAMED pipe opened with FILE_FLAG_OVERLAPPED — a different primitive with a different lifetime,
// not a flag on this one. UNVERIFIED ON A REAL WINDOWS HOST: this repository has no Windows runner,
// so the Windows arm is reasoned and compiled, never measured — same standing as the
// `tk_win32_spawn_redirected` block it sits beside.
#define TK_RT_PIPE_POLL_INTERVAL_MS (2)
int32_t tk_rt_fd_wait_readable(int64_t fd, int64_t timeout_ms);
// teko::process::fd_fill(fd, timeout_ms) — wait for `fd` under the deadline, then read one buffer's
// worth of it into a runtime-owned staging buffer, and return HOW MANY bytes landed there:
//   n > 0                    n bytes are staged; drain them with tk_rt_fd_take_byte
//   0                        END-OF-FILE — the last write end is gone and nothing more will arrive
//   TK_RT_FD_FILL_TIMEOUT    the deadline expired with neither bytes nor end-of-file
//   TK_RT_FD_FILL_ERROR      the descriptor could not be waited on or read at all
//
// TWO SCALAR CALLS AND NOT ONE `str` RETURN, AND THAT IS MEASURED, NOT TASTE. The native backend
// cannot lower ANY fat return through a general `extern fn`: its `LCall` captures exactly one result
// register, where a `str` and a `[]byte` are both two-word values. Measured with none of this
// block's code in the picture — a throwaway project binding the untouched, pre-existing `tk_rt_version`
// as `extern fn host_ver() -> str` exits 42 under `TEKO_BACKEND=c` and 1 on the own backend (the
// received `str` arriving with `len == 0`), and the same project binding `tk_rt_secure_bytes` as
// `-> []byte` reads `len == 0` there too. The fat-returning host FFI that DOES work natively
// (`tk_rt_read_file`, `tk_rt_getcwd`) works per-NAME, through a lift the released seed's frozen
// codegen already carries and cannot learn a new entry for. A pair of scalar calls needs no lift on
// either backend, which is the only shape a brand-new primitive can take today.
//
// ONE STAGING BUFFER FOR THE WHOLE PROCESS, and the constraint is stated rather than hidden: a fill
// discards whatever a previous fill left unread, so a caller must drain one fill completely before
// starting the next, and two interleaved drains of two descriptors would consume each other's
// bytes. That is the same single-slot sticky-state shape `tk_rt_stdin_eof_flag` already uses. It is
// enough for one drain at a time, which is what a caller reading one child's stream does; a reader
// multiplexing several children needs a buffer per descriptor, and that belongs with the channel
// this change deliberately does not build.
#define TK_RT_FD_FILL_TIMEOUT (-1)
#define TK_RT_FD_FILL_ERROR   (-2)
int64_t tk_rt_fd_fill(int64_t fd, int64_t timeout_ms);
// tk_rt_fd_take_byte() — the next staged byte as 0..255, or -1 once the last fill is exhausted.
// Scalar in and scalar out, for the reason tk_rt_fd_fill is.
int32_t tk_rt_fd_take_byte(void);
// =========================================================================

// teko::env::args() — the captured process argv as owned tk_str's; *n receives the count.
// tk_set_args must run first (the generated `main` calls it before the virtual-main body).
void    tk_set_args(int argc, char **argv);
tk_str *tk_rt_args(uint64_t *n);
// (CLI --version) the build's version string — the RAW project-manifest `version` + `-<suffix>`
// (e.g. "0.0.1.0-bootstrap"). Compiled from the TEKO_VERSION_STRING define injected by both build
// paths (CMake for the bootstrap, run_cc for self-host), never a runtime file read. (teko::env::version)
tk_str tk_rt_version(void);
// tk_rt_version_len — the out-parameter-length twin of the zero-argument version string above
// (mirrors `tk_str_concat_len`'s own doc): the native backend's `LCall` reads exactly one result
// register, never the true 2-eightbyte SysV/AAPCS64 struct the plain twin returns by value, so the
// COMPILE-TIME-CONSTANT string's pointer rides the return register and its length rides `*out_len`.
const tk_byte *tk_rt_version_len(uint64_t *out_len);
// tk_rt_read_line_len / tk_rt_read_stdin_len / tk_assert_scenario_prefix_len / tk_test_scope_len —
// the out-parameter-length twins of the remaining plain-`tk_str`-returning host primitives the
// self-host reaches through an `extern fn` declaration (io::read_line/read_stdin, assert's scenario
// prefix, the test scope). SAME reason as the host-info trio above: the native backend's `LCall`
// captures ONE result register, so a `tk_str`-by-value return has its length (the second eightbyte)
// dropped — the length rides `*out_len` here instead (0.3.1.0 degrau 35). Thin wrappers; no logic
// is duplicated.
const tk_byte *tk_rt_read_line_len(uint64_t *out_len);
const tk_byte *tk_rt_read_stdin_len(uint64_t *out_len);
const tk_byte *tk_assert_scenario_prefix_len(uint64_t *out_len);
const tk_byte *tk_test_scope_len(uint64_t *out_len);
// (#148) the process peak RSS in bytes (0 = unavailable) — teko::mem::peak_rss.
uint64_t tk_peak_rss(void);
// (arena-por-escopo M0) the process CURRENT RSS in bytes (0 = unavailable) — the twin of
// tk_peak_rss, for per-phase peak attribution. Linux-only (/proc/self/statm); 0 elsewhere.
// (E1-C6) the OS-granted online processor count (>= 1) — the default and cap for the test/regression
// job pools, so a run never hard-codes how parallel a machine may be. Bound in the build source
// through an `extern fn ... from "teko_rt"` (regression.tks `os_max`), the seed-lowerable route the
// name registry's `tk_names_live_count` uses; a new injected builtin would need a release to seed it.
uint64_t tk_rt_nproc(void);

// (#194 C6) teko::crypto::rand::secure_bytes(n) — n cryptographically-secure random bytes
// from the host CSPRNG (getrandom(2)/getentropy(3) on POSIX, rand_s (ucrt) on Windows).
// Returns a fresh owned buffer of EXACTLY n bytes (n == 0 -> a valid empty slice, ptr may be
// NULL); tk_panic on a genuine host entropy failure (M.1 — silently returning weak/short
// output is a security defect, never a soft error here).
tk_slice_byte tk_rt_secure_bytes(uint64_t n);

// ---- teko::time (drop-128 A6 redesign, owner-ratified table 2026-07-24) ----
// Carrier structs, EVERY field <= 64 bits (no 128-bit carrier anywhere in this surface):
//   date      — i32 days since 1970-01-01 (Gregorian proleptic, +-5.88M years)
//   time      — u64 ns since midnight, domain 0..86_399_999_999_999 (23:59:59.999999999)
//   span      — i64 ns signed duration/delta (+-292 years); also the monotonic-clock reading
//   datetime  — {days: i32; ticks: u64} — civil date+time-of-day, no timezone
//   dto       — datetime + offset_minutes: i16 — civil with a FIXED UTC offset (a geographic
//               zone id is Tier 2, deferred to .32+)
// timestamp (i64 UNIX seconds) is a REPRESENTATION, not a carrier struct.
//
// An extern fn's struct param/return uses THIS header's typedef, never the mangled typedef
// codegen generates for the SAME-shaped Teko type — the two are layout-identical but nominally
// distinct C types, so a Teko `let`/field can never hold a value fresh off an extern call (a
// compile error, not a runtime corruption). So EVERY host-dependent read below returns a bare
// SCALAR (never one of the carrier structs) — teko::time's pure Teko constructors then compose
// the scalar into the ergonomic struct, entirely on the Teko side, fully storable/composable
// from that point on (src/time/time.tks). `tk_rt_date_from_days`/`tk_rt_date_year`/
// `tk_rt_date_month`/`tk_rt_date_day_of_month` are the ONE exception, kept ONLY for the
// pre-existing `examples/regressions/time_types` fixture's own direct extern re-declaration
// (its ORIGINAL extern-chained calling shape, unchanged) — teko::time's own module no longer
// calls them (civil_from_days, already pure Teko, does the same calendar math).
typedef struct { int32_t  days;                          } tk_date;
typedef struct { uint64_t ticks;                         } tk_time;
typedef struct { int64_t  ticks;                         } tk_span;


// --- host clock reads (SCALAR-only, see above) ---
int16_t  tk_rt_wall_offset_minutes(void);  // host local UTC offset, in minutes

// tk_nproc — logical processors the OS grants this process now. POSIX: sysconf(_SC_NPROCESSORS_ONLN).
// Windows: GetActiveProcessorCount(ALL_PROCESSOR_GROUPS). Floor 1 (never 0). No allocation. The
// default degree of build parallelism (teko::env::nproc → build_jobs, TEST_JOBS default).
uint64_t tk_nproc(void);
// ===== journal runtime funds (design journaling-de-corrida-0.3.1 §2.3 / §6) — BEGIN =====
// A DELIMITED, DISJOINT BLOCK of NEW symbols (the J1 crumb). Kept apart from tk_region_enter/leave
// (the M2 territory) and tk_slice_push* (the 5th-gap territory) so a drain rebase is trivial: nothing
// here edits a function either of those touches. The str-taking entries are ALSO transcribed into
// lir/lower.tks's by-value-tk_str arity tables, per that file's "the table mirrors the header" law.
//
// tk_journal_open — open/create `path` in O_WRONLY|O_APPEND|O_CREAT and return the RAW descriptor
// (>= 0), or -1 on failure. NO stdio: a FILE*'s user-space buffer dies with the process, which is the
// one moment this file had value. Also STASHES the descriptor as the target a later async-signal-safe
// tk_journal_note writes to (the polite-signal arm, §6).
int64_t tk_journal_open(tk_str path);
// tk_journal_append — write `rec` whole to descriptor `seg`, finishing a short write in a loop.
// Returns 0, or the errno. `rec` leads (a by-value tk_str is only ABI-describable when leading; see
// lir/lower.tks::takes_one_str_by_value) so the whole record lands in the kernel as one O_APPEND write.
int32_t tk_journal_append(tk_str rec, int64_t seg);
// tk_journal_arm — remember `prefix` (a pre-shaped "run\twriter\tstop\t" record head) as the bytes a
// later tk_journal_note prepends before the signal number. Formatting the stamp HERE, at open time,
// is what lets the note itself allocate and format nothing — the async-signal-safety requirement (§6).
void tk_journal_arm(tk_str prefix);
// tk_journal_note — the ASYNC-SIGNAL-SAFE arm (§6): write the armed prefix, the decimal `sig`, and a
// newline to the stashed descriptor, using only write(2) over pre-built bytes (no malloc, no stdio).
// A no-op when no segment was opened. WIRED into the signal handlers by the J5 crumb; defined here so
// J5 is pure handler installation.
void tk_journal_note(int sig);
// tk_rt_rename — rename `from` to `to` atomically within one directory (rename(2) / MoveFileExW with
// MOVEFILE_REPLACE_EXISTING). The second half of the discipline (§2.3): what is append goes by append,
// what is a whole artefact writes to a temporary and publishes here, so no reader sees a half file.
int32_t tk_rt_rename(tk_str from, tk_str to);
// tk_rt_pid — this process's id (getpid / GetCurrentProcessId). One half of a run's identity stamp,
// disambiguating two runs that mint the same monotonic nanosecond.
int64_t tk_rt_pid(void);
// tk_rt_pid_alive — whether a process with id `pid` is currently alive (kill(pid,0) / OpenProcess).
// The liveness `sweep` reads from a run root's lock before reclaiming it: a live sibling is spared.
bool tk_rt_pid_alive(int64_t pid);
// ===== journal runtime funds — END =====

// (#148 R2) bulk byte-append with free-old-on-grow BY DECREE (the linear cb emitter chain) — one
// memcpy per fragment; the old buffer parks for reuse the moment a grow replaces it.
void *tk_append_bytes_fo(const void *ptr, uint64_t len, const void *src, uint64_t n, uint64_t *out_len);
// (mem::free) tk_free_block — park an explicitly freed root-arena block for same-size REUSE
// (the `teko::mem::free` []T-arm lowering: `tk_free_block(s.ptr, s.len * sizeof(elem))`).
void tk_free_block(void *p, uint64_t bytes);

// (0.3.1.0 degrau 4 — NATIVE-AGG-SLICE-BY-ADDRESS) tk_slice_elem_box — per-EXECUTION storage for one
// AGGREGATE element pushed into a slice. The native backend holds a `[]struct` as a slice of
// ADDRESSES, and its value for a struct/class instance is the address of an `alloca` — ONE frame slot
// per INSTRUCTION, so `loop { xs = push(xs, T { … }) }` would store the SAME address every iteration.
// The lowering copies the aggregate here first and pushes the address this returns instead.
// LIFETIME: the copy is bump-allocated in the ROOT arena, through tk_alloc — exactly where
// tk_slice_push allocates the buffer that will hold the address. An element copy therefore lives at
// least as long as every buffer that can name it, and both are reclaimed by the same tk_arena_pop /
// tk_regions_free_all. Should a region-scoped push (tk_slice_push_r) ever become emittable, its
// elements must be boxed in the SAME region, not here.
void *tk_slice_elem_box(const void *elem, uint64_t esz);

// (0.3.1.0 degrau 18 — reference deref-assignment) tk_mem_copy — copy `n` bytes from `src` into
// `dst`. The plain byte-copy the LIR has no instruction for: `r.value = <aggregate>` through a
// `Ref<T>` handle writes the new value's bytes DIRECTLY into the handle's pointee storage — unlike
// a container field's BOXED pointer slot (this backend's uniform "a struct value is its address"
// model), `Ref<T>` names `T`'s memory directly (the C route's bare `<T> *`), so storing the new
// value's address there would overwrite the pointee's own first bytes with a pointer instead of
// its data. Same "the LIR has no allocation/copy op" reasoning `tk_slice_elem_box` already used.
void tk_mem_copy(void *dst, const void *src, uint64_t n);

// --- float division + float bit-patterns ---
// fdiv: checked f64 division (panics on a zero divisor, M.1). The sign-aware `tk_div`/`tk_rem`/
// `tk_int_to_float` trio that used to sit here rode a 128-bit carrier and is REMOVED (128-bit
// primitives, integer and float, are gone from the language — owner ruling 2026-07-30; use
// `teko::numeric::bigint`/`dec` for wider arithmetic). Nothing emitted them: integer `/` and `%`
// route through the per-width `tk_div_<tag>`/`tk_mod_<tag>` helpers below, and an int→float cast
// is a plain C cast. Their only remaining effect was to drag the libgcc 128-bit builtins
// (`__divti3`/`__udivti3`/`__modti3`/`__umodti3`/`__floattidf`/`__floatuntidf`) into every link.
double   tk_fdiv(double a, double b);
// f64 ↔ raw IEEE-754 bit pattern (the .tkb float (de)serialization edge).
uint64_t tk_f64_bits(double x);
double   tk_f64_from_bits(uint64_t bits);

// TK_PANIC_MARKER — the canonical opening of EVERY deliberate panic line: the word
// "deliberate" is the whole point. A panic is the program's OWN guard firing (M.1); a
// crash is the host killing it (tk_rt_crash_handler prints "teko: FATAL signal" instead),
// and the two must never be confused by whoever reads the log.
//
// Why the marker exists at all, and why it is TEXT: a regressor scenario used to prove
// "this panicked, on purpose, for this reason" by asserting `exit = 134`. That number was
// never Teko's — it is 128+SIGABRT, SYNTHESISED by a POSIX shell out of the child's wait
// status, and Windows has no such convention (PR #94's `test / windows` reported exit 127
// on a run whose stderr plainly carried the panic). An 8-bit exit status is also a channel
// the child SHARES with the shell and the OS, so it can be forged by either. A line on
// stderr can be written by nothing but the program itself, which makes it the only evidence
// that means the same thing on every platform.
//
// The line is `TK_PANIC_MARKER <msg>` and NOTHING else: no source position (a positioned
// guard prints its own "line:col: " ahead of it) and no backtrace
// (that follows on its own lines, and degrades to a one-line notice where execinfo is
// absent). So marker+reason always sit adjacent on one line, and a regressor pattern can
// assert both at once without depending on either of the parts that move per platform.
//
// STABLE: `.tkr` scenarios spell this text literally. Changing it breaks them — deliberately.
#define TK_PANIC_MARKER "teko: deliberate panic: "

// tk_panic — fail loud (M.1): "teko: deliberate panic: <msg>\n" to stderr, a backtrace,
// then a non-zero exit (SIGABRT). See TK_PANIC_MARKER above for the line's contract.
_Noreturn void tk_panic(const char *msg);
// the Teko-level globals `panic(str)` / `exit(<int>)` (legislator's ruling — no `never` type).
// tk_panic_str takes a tk_str (ptr+len, tolerates embedded NUL); tk_exit ends with a status code.
_Noreturn void tk_panic_str(tk_str msg);
// TK_EXIT_STATUS_MASK — the low byte of a process status, the ONLY part of an exit code every
// supported platform reports IDENTICALLY. POSIX exit(3)/wait(2) expose `status & 0377`; Windows
// hands back all 32 bits of ExitProcess, so the same program reported 20203 there and 235 here.
// Owner ruling 2026-07-30: the mask is the LANGUAGE's job ("pode fazer isso dentro do próprio
// exit (para equalizar a saída)") — never asked of the programmer, never tolerated by a test
// comparator. tk_exit_status is the single implementation on the C side; the own-backend process
// entry carries its own (teko::lir::EXIT_STATUS_MASK) because it never links this file.
#define TK_EXIT_STATUS_MASK 0xFF
// tk_exit_status — the platform-uniform process status for a raw Teko exit code: its low byte,
// which is also what a NEGATIVE code already yielded on POSIX (-1 -> 255). Called by tk_exit and
// by the C route's `main` return (codegen emits `return tk_exit_status((int)(<expr>));`).
_Noreturn void tk_panic_div0(void);       // "division by zero"
_Noreturn void tk_panic_overflow(void);   // "integer overflow"

// teko::assert (the injected testing assertions) lives in its own C seed —
// src/assert/assert.{c,h} (canonical: src/assert/assert.tks). Generated programs that
// call them get the symbols because driver.c::run_cc compiles src/assert/assert.c
// alongside this runtime; the compiler lib links the same source via CMake.

// =========================================================================
// F3 runtime guards (M.1 — fail loud, never silent corruption / metal hazard):
// the backend emits calls to these in place of raw C `/`, `%`, `+`, `-`, `*`
// (integer only), and narrowing casts. All are SINGLE-EVALUATION (each argument
// is computed once by the caller and passed by value), so codegen never
// duplicates an operand subtree.
//
// COVERAGE RULE (M.3): conversion/division possibility is validated at RUNTIME;
// impossibility -> PANIC. Constants out of range are the checker's job (compile
// error) and are out of scope here.
//
// C7.15 — OVERFLOW GUARDS for +, -, * on INTEGER types:
// When TEKO_OVERFLOW_DEBUG is defined the helpers use __builtin_*_overflow
// (GCC/Clang) and call tk_panic_overflow() if overflow is detected.
// Without the flag they reduce to the plain C operation — zero overhead,
// identical to the old bare-operator path. Float +,-,* are NOT guarded
// (float overflow is not a Teko panic; only int arithmetic is guarded here).
// =========================================================================

// --- checked integer division / modulo: panic on a zero divisor (no UB / SIGFPE) ---
// One helper per signed/unsigned width (u8..u64, i8..i64 — the language's WHOLE integer set);
// codegen selects by the binary node's prim.
static inline uint8_t  tk_div_u8 (uint8_t  a, uint8_t  b){ if (b == 0) tk_panic_div0(); return (uint8_t )(a / b); }
static inline uint16_t tk_div_u16(uint16_t a, uint16_t b){ if (b == 0) tk_panic_div0(); return (uint16_t)(a / b); }
static inline uint32_t tk_div_u32(uint32_t a, uint32_t b){ if (b == 0) tk_panic_div0(); return a / b; }
static inline uint64_t tk_div_u64(uint64_t a, uint64_t b){ if (b == 0) tk_panic_div0(); return a / b; }
static inline int8_t   tk_div_i8 (int8_t   a, int8_t   b){ if (b == 0) tk_panic_div0(); return (int8_t  )(a / b); }
static inline int16_t  tk_div_i16(int16_t  a, int16_t  b){ if (b == 0) tk_panic_div0(); return (int16_t )(a / b); }
static inline int32_t  tk_div_i32(int32_t  a, int32_t  b){ if (b == 0) tk_panic_div0(); return a / b; }
static inline int64_t  tk_div_i64(int64_t  a, int64_t  b){ if (b == 0) tk_panic_div0(); return a / b; }

static inline uint8_t  tk_mod_u8 (uint8_t  a, uint8_t  b){ if (b == 0) tk_panic_div0(); return (uint8_t )(a % b); }
static inline uint16_t tk_mod_u16(uint16_t a, uint16_t b){ if (b == 0) tk_panic_div0(); return (uint16_t)(a % b); }
static inline uint32_t tk_mod_u32(uint32_t a, uint32_t b){ if (b == 0) tk_panic_div0(); return a % b; }
static inline uint64_t tk_mod_u64(uint64_t a, uint64_t b){ if (b == 0) tk_panic_div0(); return a % b; }
static inline int8_t   tk_mod_i8 (int8_t   a, int8_t   b){ if (b == 0) tk_panic_div0(); return (int8_t  )(a % b); }
static inline int16_t  tk_mod_i16(int16_t  a, int16_t  b){ if (b == 0) tk_panic_div0(); return (int16_t )(a % b); }
static inline int32_t  tk_mod_i32(int32_t  a, int32_t  b){ if (b == 0) tk_panic_div0(); return a % b; }
static inline int64_t  tk_mod_i64(int64_t  a, int64_t  b){ if (b == 0) tk_panic_div0(); return a % b; }

// --- #49: width-masked shift <<, >> — defined result for an OUT-OF-RANGE count -------------
// Plain C `<<`/`>>` is UB when the shift count is >= the operand's bit-width (or negative); a
// Teko program that computes a large/negative-looking `u32`/`i64`/… count (e.g. from user input
// or arithmetic) must not hit native UB. Ruling: mask the count by (bit-width - 1) — C#/Java
// semantics — so `(1 as i32) << 40` == `1 << (40 & 31)` == `1 << 8` == 256. In-range counts
// are unaffected (the
// mask is a no-op when count < width). One helper per signed/unsigned width (u8..u64, i8..i64);
// codegen selects by the binary node's result prim (same tag as tk_div_*/tk_add_*). Signed `>>`
// keeps its existing sign-preserving (arithmetic) behavior — only the COUNT is fixed here.
static inline uint8_t  tk_shl_u8 (uint8_t  a, uint8_t  b){ return (uint8_t )(a << (b & 7));   }
static inline uint16_t tk_shl_u16(uint16_t a, uint16_t b){ return (uint16_t)(a << (b & 15));  }
static inline uint32_t tk_shl_u32(uint32_t a, uint32_t b){ return a << (b & 31);              }
static inline uint64_t tk_shl_u64(uint64_t a, uint64_t b){ return a << (b & 63);              }
static inline int8_t   tk_shl_i8 (int8_t   a, int8_t   b){ return (int8_t )((uint8_t )a << ((uint8_t )b & 7));   }
static inline int16_t  tk_shl_i16(int16_t  a, int16_t  b){ return (int16_t)((uint16_t)a << ((uint16_t)b & 15));  }
static inline int32_t  tk_shl_i32(int32_t  a, int32_t  b){ return (int32_t)((uint32_t)a << ((uint32_t)b & 31));  }
static inline int64_t  tk_shl_i64(int64_t  a, int64_t  b){ return (int64_t)((uint64_t)a << ((uint64_t)b & 63));  }

static inline uint8_t  tk_shr_u8 (uint8_t  a, uint8_t  b){ return (uint8_t )(a >> (b & 7));   }
static inline uint16_t tk_shr_u16(uint16_t a, uint16_t b){ return (uint16_t)(a >> (b & 15));  }
static inline uint32_t tk_shr_u32(uint32_t a, uint32_t b){ return a >> (b & 31);              }
static inline uint64_t tk_shr_u64(uint64_t a, uint64_t b){ return a >> (b & 63);              }
static inline int8_t   tk_shr_i8 (int8_t   a, int8_t   b){ return (int8_t )(a >> (b & 7));   }
static inline int16_t  tk_shr_i16(int16_t  a, int16_t  b){ return (int16_t)(a >> (b & 15));  }
static inline int32_t  tk_shr_i32(int32_t  a, int32_t  b){ return a >> (b & 31);              }
static inline int64_t  tk_shr_i64(int64_t  a, int64_t  b){ return a >> (b & 63);              }

// --- C7.15 overflow-guarded integer +, -, *: panic when TEKO_OVERFLOW_DEBUG is set ---
// One helper per signed/unsigned width (u8..u64, i8..i64). Float +,-,* are NOT here —
// float overflow is not a Teko error. Bool is not an arithmetic target.
// __builtin_add/sub/mul_overflow work on any integer type in GCC/Clang; the generic form
// `__builtin_add_overflow(a, b, &r)` selects the right overflow semantics for the type.
#ifdef TEKO_OVERFLOW_DEBUG
static inline uint8_t  tk_add_u8 (uint8_t  a, uint8_t  b){ uint8_t  r; if (__builtin_add_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline uint16_t tk_add_u16(uint16_t a, uint16_t b){ uint16_t r; if (__builtin_add_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline uint32_t tk_add_u32(uint32_t a, uint32_t b){ uint32_t r; if (__builtin_add_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline uint64_t tk_add_u64(uint64_t a, uint64_t b){ uint64_t r; if (__builtin_add_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int8_t   tk_add_i8 (int8_t   a, int8_t   b){ int8_t   r; if (__builtin_add_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int16_t  tk_add_i16(int16_t  a, int16_t  b){ int16_t  r; if (__builtin_add_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int32_t  tk_add_i32(int32_t  a, int32_t  b){ int32_t  r; if (__builtin_add_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int64_t  tk_add_i64(int64_t  a, int64_t  b){ int64_t  r; if (__builtin_add_overflow(a,b,&r)) tk_panic_overflow(); return r; }

static inline uint8_t  tk_sub_u8 (uint8_t  a, uint8_t  b){ uint8_t  r; if (__builtin_sub_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline uint16_t tk_sub_u16(uint16_t a, uint16_t b){ uint16_t r; if (__builtin_sub_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline uint32_t tk_sub_u32(uint32_t a, uint32_t b){ uint32_t r; if (__builtin_sub_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline uint64_t tk_sub_u64(uint64_t a, uint64_t b){ uint64_t r; if (__builtin_sub_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int8_t   tk_sub_i8 (int8_t   a, int8_t   b){ int8_t   r; if (__builtin_sub_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int16_t  tk_sub_i16(int16_t  a, int16_t  b){ int16_t  r; if (__builtin_sub_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int32_t  tk_sub_i32(int32_t  a, int32_t  b){ int32_t  r; if (__builtin_sub_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int64_t  tk_sub_i64(int64_t  a, int64_t  b){ int64_t  r; if (__builtin_sub_overflow(a,b,&r)) tk_panic_overflow(); return r; }

static inline uint8_t  tk_mul_u8 (uint8_t  a, uint8_t  b){ uint8_t  r; if (__builtin_mul_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline uint16_t tk_mul_u16(uint16_t a, uint16_t b){ uint16_t r; if (__builtin_mul_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline uint32_t tk_mul_u32(uint32_t a, uint32_t b){ uint32_t r; if (__builtin_mul_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline uint64_t tk_mul_u64(uint64_t a, uint64_t b){ uint64_t r; if (__builtin_mul_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int8_t   tk_mul_i8 (int8_t   a, int8_t   b){ int8_t   r; if (__builtin_mul_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int16_t  tk_mul_i16(int16_t  a, int16_t  b){ int16_t  r; if (__builtin_mul_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int32_t  tk_mul_i32(int32_t  a, int32_t  b){ int32_t  r; if (__builtin_mul_overflow(a,b,&r)) tk_panic_overflow(); return r; }
static inline int64_t  tk_mul_i64(int64_t  a, int64_t  b){ int64_t  r; if (__builtin_mul_overflow(a,b,&r)) tk_panic_overflow(); return r; }
#else  // !TEKO_OVERFLOW_DEBUG — plain C, zero overhead, identical to old bare-operator path
static inline uint8_t  tk_add_u8 (uint8_t  a, uint8_t  b){ return (uint8_t )(a + b); }
static inline uint16_t tk_add_u16(uint16_t a, uint16_t b){ return (uint16_t)(a + b); }
static inline uint32_t tk_add_u32(uint32_t a, uint32_t b){ return a + b; }
static inline uint64_t tk_add_u64(uint64_t a, uint64_t b){ return a + b; }
static inline int8_t   tk_add_i8 (int8_t   a, int8_t   b){ return (int8_t )(a + b); }
static inline int16_t  tk_add_i16(int16_t  a, int16_t  b){ return (int16_t)(a + b); }
static inline int32_t  tk_add_i32(int32_t  a, int32_t  b){ return a + b; }
static inline int64_t  tk_add_i64(int64_t  a, int64_t  b){ return a + b; }

static inline uint8_t  tk_sub_u8 (uint8_t  a, uint8_t  b){ return (uint8_t )(a - b); }
static inline uint16_t tk_sub_u16(uint16_t a, uint16_t b){ return (uint16_t)(a - b); }
static inline uint32_t tk_sub_u32(uint32_t a, uint32_t b){ return a - b; }
static inline uint64_t tk_sub_u64(uint64_t a, uint64_t b){ return a - b; }
static inline int8_t   tk_sub_i8 (int8_t   a, int8_t   b){ return (int8_t )(a - b); }
static inline int16_t  tk_sub_i16(int16_t  a, int16_t  b){ return (int16_t)(a - b); }
static inline int32_t  tk_sub_i32(int32_t  a, int32_t  b){ return a - b; }
static inline int64_t  tk_sub_i64(int64_t  a, int64_t  b){ return a - b; }

static inline uint8_t  tk_mul_u8 (uint8_t  a, uint8_t  b){ return (uint8_t )(a * b); }
static inline uint16_t tk_mul_u16(uint16_t a, uint16_t b){ return (uint16_t)((unsigned)a * (unsigned)b); }
static inline uint32_t tk_mul_u32(uint32_t a, uint32_t b){ return a * b; }
static inline uint64_t tk_mul_u64(uint64_t a, uint64_t b){ return a * b; }
static inline int8_t   tk_mul_i8 (int8_t   a, int8_t   b){ return (int8_t )(a * b); }
static inline int16_t  tk_mul_i16(int16_t  a, int16_t  b){ return (int16_t)(a * b); }
static inline int32_t  tk_mul_i32(int32_t  a, int32_t  b){ return a * b; }
static inline int64_t  tk_mul_i64(int64_t  a, int64_t  b){ return a * b; }
#endif // TEKO_OVERFLOW_DEBUG

// --- checked FLOAT division: ruling (§5) — float ÷0 PANICS (parity with int, M.1) ---
// `%` on floats is invalid (the backend rejects it); only `/` rides these. Single-eval.
static inline double     tk_div_f64(double     a, double     b){ if (b == 0.0)     tk_panic_div0(); return a / b; }
static inline float      tk_div_f32(float      a, float      b){ if (b == 0.0f)    tk_panic_div0(); return a / b; }
static inline _Float16   tk_div_f16(_Float16   a, _Float16   b){ if (b == (_Float16)0) tk_panic_div0(); return a / b; }


#endif // TEKO_RT_H
