# §10 Concurrency — the CHANNELS batch (C0b·C0c·C1·C2·C3·C4·C5) at implementation depth

**Status:** DESIGN-AHEAD (architect). No product `.tks`/`.c` changed by this doc.
**Ground truth:** HEAD `2d084b3d` (`git rev-parse HEAD` → `2d084b3d307292c6a9da9eaa539eff3760d98457`).
**Builds on (LANDED):** the spawn batch — C0a `tk_thread_spawn` + join twin (`teko_rt.c`, `#include <pthread.h>`
already present at `teko_rt.c:43`, `-pthread` in the ladder), S1–S3 surface. The generics stack (9-ops `IEq`/
`IOrd`, #254 methods-on-`T`, fase1b `Dictionary`/`HashSet`, F3 phantom-owner factory, `sort<T:IOrd>`). The §7
DI/`service singleton`/`svc` machinery.
**Spec:** `docs/design/mudancas-superficie-0.3.1.md` §10 (~L669–1052, sealed). Prior design:
`plano-s10-concorrencia-crumbs.md` (spine), `plano-s10-spawn-batch-detalhe.md` (the style/depth this doc matches),
`concorrencia-isolate-spawn-chan-0.3.1.md`.

**Scope of THIS doc:** the channel crumbs **C0b, C0c, C1, C2, C3, C4, C5** only. `await` (A1–A4), `cancel` (CN1),
and `teko::journal` (J1) are LATER batches and OUT of scope. Channels are **model-INDEPENDENT of the D2/await
ruling** (they build only on the landed spawn runtime).

**Corpus-leaf invariant (holds for EVERY crumb here):** the compiler NEVER instantiates `chan`/`Rx`/`Tx`/`Ctx` in
its own `src/` (axis-2 parallel codegen uses the SEPARATE internal `fork_join`, §10.2 L757). `grep -rn '\bchan\b'
src/ --include=*.tks` finds only a local variable `chan` in `regression.tks:206` (an expression-position `str`, not
a type) — no collision with a `chan<T>` TYPE resolved in type position. So §10 stays a **LEAF**: stdlib crumbs
(C1/C2/C3, the Teko half of C5) are byte-identical-ish leaf reseeds; C0b/C0c are maintained-C (runtime rebuild, no
codegen reseed — like C0a); only **C4** is genuinely compiler-touching (fixpoint + reseed), and even its emitted
`teko.c` for the compiler's OWN self-image is byte-identical because the corpus never calls `chan::make`/`svc<Rx>`.

---

## 0. The THREE generic-machinery gap verdicts (they gate C4's classification) — grounded on HEAD `2d084b3d`

### Gap 1 — a method type-param `K` whose constraint `IChannelKind<T>` references the owner's `T`

**Verdict: the SPEC'S LITERAL spelling does NOT parse today (real, parser-level) — but the capability the design
needs is CLOSED-BY-DESIGN with zero compiler crumb, because conformance is purely NOMINAL and the owner's `T` is
never load-bearing in the K-constraint. C4 does NOT flip to `[C]` on Gap 1's account.**

Grounding:
- `parse_constraint_atom` (`parse_decl.tks:119-141`) reads only a **bare** `name = tokens[pos].text` (`:137`) and
  builds `ConstraintAtom { name = name }` (`:141`, whose type is `ConstraintAtom = struct { name: str }` —
  `ast.tks:568`, **no type-argument field**). It never consumes a following `<...>`.
- So for `<K: service singleton & IChannelKind<T>>`, after the atom `IChannelKind` is parsed, `p` sits on the `<`.
  `parse_type_params` (`parse_decl.tks:203-205`) then requires `,` or `>` and **errors**: `"expected ',' or '>' in
  a type-parameter list"`. The parameterized constraint atom `IChannelKind<T>` is unrepresentable in the constraint
  grammar today.
- **But the conformance gate is nominal and arg-blind.** `constraint_atom_satisfied` (`monomorph.tks:53`), for an
  `InterfaceBody` atom, returns `n.name == atom_name || type_conforms_to(n.name, atom_name, table)` (`:74`).
  `type_conforms_to` (`resolve.tks:1701`) walks the conformer's `implements` list matching by **bare last-segment
  name** (`:1726`), ignoring type arguments entirely. So a conformer declared `struct MemChan<T> … IChannelKind<T>`
  satisfies `type_conforms_to("MemChan","IChannelKind")` with **no reference to `T`**.
- **The T-bearing methods are never dispatched through the constraint.** `constraint_surface` (`resolve.tks:1003`)
  over `service singleton & IChannelKind` (a `ConstraintAnd`) unions the form-word surface (EMPTY — `:1017/:1122`)
  with `iface_methods_by_name("IChannelKind")` = `[init, send, recv, end]`. Inside `make`, the ONLY K-method
  invoked is `init(key: str)` (and optionally `end()`) — **neither references `T`**. The `T`-bearing ops
  (`send(v:T)`/`recv():T`) are called through the **concrete conformer's OWN generic context**
  (`MemChan<T>::send`, where `T` is `MemChan`'s own param), reached via the typed `Tx<T>`/`Rx<T>` handle — never
  through the K constraint. So mono is NEVER asked to substitute the owner's `T` into `K`'s constraint.

**Resolution (adopted): spell the constraint as the bare atom `K: service singleton & IChannelKind` (drop the
`<T>`).** It parses, the nominal gate accepts every correct transport, and no owner-`T`-into-`K` substitution is
required. The owner's `T` flows through `chan<T>`'s owner param and the `Rx<T>`/`Tx<T>` wrappers. **No parser/checker
crumb.** (If the owner ever wants the literal `IChannelKind<T>` spelling for doc parity, that is a *separate,
optional* parser enhancement — parameterized constraint atoms — explicitly NOT required for correctness and reported
up, not built here.)

### Gap 2 — `Rx<T>.pop(): T | Closed` — a union member over a type-param `T`

**Verdict: CLOSED-pending-a-de-risk-fixture. Precedent proves the generic-union-return stamps; the only unproven
delta is a NOMINAL (non-`null`) union member, which the fixture pins.**

Grounding:
- Precedent SHIPS: `Dictionary<K,V>.get(k: K): V | null` (`dictionary.tks:86`) — a generic method whose return is a
  union member over a type-param. It stamps (`Dictionary__g__StrKey__i64`) and its two-level match works today
  (fase1b landed). `sorted_dictionary.tks:76` and `map.tks:98` are further live instances.
- The monomorph substitution handles union members: `subst_typeexpr` rewrites a `UnionType`'s members
  (`monomorph.tks:345`), so `Rx<T>.pop(): T | Closed` stamps to `Rx__g__i32.pop(): i32 | Closed`.
- **Delta:** every shipped precedent has `null` as the second member; `Closed` is a NOMINAL member. This composes the
  proven `V | null` generic-union-return with the (separately proven) nominal-variant two-level match. No shipped
  generic returns `T | <nominal-non-null>`, so THIS composition is the only unproven point.

**Resolution (adopted): `Closed` is a nominal error struct in `teko::threads` (`type Closed = struct { }`), and C1
carries a REGRESSION FIXTURE `rx_pop_closed` that stamps `Rx__g__i32` and matches `i32 | Closed` on both arms.** If
that fixture reveals the two-level nominal match fails to stamp for a generic instance, a small codegen crumb
mirroring `native-variant-match-root-map-0.3.1.md` closes it — but the precedent above makes closed the expected
outcome. Grounded verdict: **closed, fixture-gated.**

### Gap 3 — `K: service singleton` as a mono-gate constraint (non-singleton = compile error)

**Verdict: CLOSED. Already enforced at the instantiation point. No crumb.**

Grounding:
- `service singleton` parses to `ConstraintForm { form = "service"; has_lifetime = true; lifetime =
  ServiceLifetime::Singleton }` (`parse_decl.tks:127-132`, via `parse_service_lifetime` `:1135`).
- `constraint_form_satisfied` (`monomorph.tks:115-129`) for `form == "service"`: requires `is_service_name(name)`
  (`resolve.tks:1569`) AND, because `has_lifetime`, `lifetime_eq(service_lifetime_of(name), Singleton)`
  (`resolve.tks:1584/1598`). A non-singleton (or non-service) conformer **fails** here, and `check_constraints`
  (`monomorph.tks:167`) turns that into a compile error at the `make<K>` instantiation. Exactly the spec's
  "transporte não-singleton **falha na compilação** no `make`" (§10.2 L813).
- REJECT fixture `chan_make_nonsingleton` (under C4) locks this.

**Net effect on C4's classification:** C4 is `[C]` **only** because of the `svc<Rx<T>>/svc<Tx<T>>` DI-by-key
extension + the `chan::make` lowering (§4 below), NOT because of any of the three gaps. Gaps 1 and 3 are closed with
no crumb; Gap 2 is fixture-gated in the leaf C1.

---

## Crumb C0b — `tk_memchan_*` (maintained-C) — the FIRST implementable crumb

**Files:** `src/runtime/teko_rt.{c,h}` (maintained-C, Teko-only-law-exempt). **Zero new deps** — builds on the
landed `tk_region_program` (F2 singleton region, `teko_rt.c:2321` / `teko_rt.h:248`), `tk_region_register`/
`tk_region_lookup` (`teko_rt.h:292/295`), and `pthread.h` (already included, `teko_rt.c:43`). **No fixpoint reseed**
— runtime rebuild only, exactly like C0a.

### C0b.1 — signatures (add to `teko_rt.h`, near the region decls at `:288-296`)

```c
/* tk_memchan — an in-process MPSC FIFO for opaque fixed-width elements, living in the F2 program region
 * (tk_region_program) so it OUTLIVES the producing task's tk_task_end (a channel is cross-task by construction).
 * Type-erased: it moves `elem_size` raw bytes per element (the Teko layer marshals T<->bytes). Bounded by a slot
 * count; `bounds == 0` means UNBOUNDED (grows). All send/recv are blocking (a bounded send blocks while full, a
 * recv blocks while empty), released by the close signal. §10 C0b. */
typedef struct tk_memchan tk_memchan;

/* tk_memchan_make — allocate a channel of `bounds` slots of `elem_size` bytes each. The STRUCT SHELL lives in the
 * F2 program region (stable address for the registry); the ring BUFFER and the pthread mutex/cond are heap/OS
 * resources that tk_memchan_end releases (F2 has no per-entry free yet — see C5 teardown note). bounds==0 =>
 * unbounded (buffer doubles on demand). OOM => tk_panic. */
tk_memchan *tk_memchan_make(uint64_t elem_size, uint64_t bounds);

/* tk_memchan_send — copy `elem_size` bytes from `elem` into the FIFO tail. Blocks while a BOUNDED channel is full;
 * returns immediately once space exists or the channel is closed (a send on a closed channel is a no-op — the
 * producer contract closes exactly once). */
void tk_memchan_send(tk_memchan *ch, const void *elem);

/* tk_memchan_recv — copy the FIFO head into `out` (`elem_size` bytes). Blocks while empty. Returns 1 when an
 * element was delivered, 0 when the channel is CLOSED AND DRAINED (the `Closed` signal the Teko Rx.pop surfaces).
 * A single reader (MPSC) is assumed; two concurrent readers are a program error, not guarded here. */
int tk_memchan_recv(tk_memchan *ch, void *out);

/* tk_memchan_close — mark closed and wake every blocked sender/receiver. Idempotent (the producer may close once;
 * ctx coordinates N producers). After drain, recv returns 0. */
void tk_memchan_close(tk_memchan *ch);

/* tk_memchan_end — close + release the ring buffer and destroy the mutex/cond (the drop-cascade's transport
 * teardown). Idempotent. The F2 struct shell is reclaimed at process exit (tk_regions program-region free) until
 * the Doc 1 §7.8 per-entry F2 reclamation lands (C5 note). */
void tk_memchan_end(tk_memchan *ch);

/* tk_memchan_selftest — C-owned probe body reached through ONE plain-scalar extern (the tk_thread_spawn_selftest
 * pattern, teko_rt.c C0a). Spawns a producer that sends 0..n-1 (i64), the caller thread recv's them, asserts
 * order+sum, then close+end. Returns 0 on success, a non-zero failure code otherwise. Under TEKO_MEM_PARANOID=1
 * the arena live-count is asserted balanced by the caller. */
uint64_t tk_memchan_selftest(int64_t n);
```

### C0b.2 — the bounded-buffer struct + blocking semantics (add to `teko_rt.c`)

```c
struct tk_memchan {
    unsigned char *buf;        /* ring: cap*elem_size bytes (malloc — freeable at end; NOT arena) */
    uint64_t elem_size;
    uint64_t cap;              /* slot count (== bounds for bounded; current capacity when unbounded) */
    uint64_t head, tail, count;
    int closed;                /* guarded by lock */
    int unbounded;             /* bounds == 0 */
    pthread_mutex_t lock;
    pthread_cond_t  not_empty; /* recv waits here */
    pthread_cond_t  not_full;  /* bounded send waits here */
};
```

- **Residence (the whole point):** `tk_memchan_make` allocates the SHELL via `tk_region_alloc(tk_region_program(),
  sizeof(struct tk_memchan))` so the struct address is stable and survives any producing task's `tk_task_end`
  (`tk_region_program` is owned by no task — `teko_rt.c:2308`). The ring `buf` and the pthread objects are the
  releasable resources `tk_memchan_end` frees.
- **Blocking vs `bounds`:** bounded (`cap>0`, `!unbounded`) — `send` does `while (count==cap && !closed)
  pthread_cond_wait(&not_full,&lock)`; `recv` does `while (count==0 && !closed) pthread_cond_wait(&not_empty,
  &lock)`. Unbounded (`bounds==0`) — `send` never blocks (doubles `buf` via `malloc`+`memcpy` on a full ring, since
  the arena has no `realloc`); `recv` still blocks while empty. Every mutation signals the paired cond.
- **Close/drain:** `recv` returns 0 only when `count==0 && closed`; while data remains it drains first, so `Closed`
  is delivered AFTER the last real element (spec: "encerrado E drenado").

**Why pthread mutex/cond, not the existing `__atomic` spinlock (`tk_lock_acquire`/`tk_lock_release`,
`teko_rt.c:1591/1598`):** the spinlock busy-waits, which is fine for the microscopic region-singleton critical
sections it guards but pathological for a channel where a reader may block for milliseconds. `pthread.h` is already
in the file and `-pthread` already in the ladder (C0a), so cond-vars cost nothing new. (The §16 rewrite may swap to a
futex; that is INSIDE this file, zero surface impact.)

### C0b.3 — fixture `memchan_pingpong` (drives the primitive BEFORE the surface exists)

New probe `examples/probes/memchan/` — Teko side reaches `tk_memchan_selftest` through one scalar extern
(the C0a `thread_spawn_selftest` idiom):

```
/**
 * memchan_pingpong — drive the maintained-C tk_memchan primitive before the `chan` surface exists: a producer
 * thread sends 0..n-1, this thread recv's them, and the C selftest asserts order + sum + a balanced arena
 * live-count. The success sentinel is 42 (the chan_dgram/thread_spawn convention); any drift or mis-order exits 1.
 *
 * @return process exit 42 on success, 1 on any failure
 * @since 0.3.1 §10 C0b
 */
fn main() {
    var rc = memchan_selftest(10000)
    if rc != 0 { teko::process::exit(1) }
    teko::process::exit(42)
}
```

Driven `TEKO_MEM_PARANOID=1 ./…/memchan` → **42**; mis-order/drift → **1**. No `cabi fn` needed (scalar extern only).

---

## Crumb C0c — `tk_oschan_*` (maintained-C) — lifted from the `chan_dgram` probe

**Files:** `src/runtime/teko_rt.{c,h}`. Lifts the proven syscalls of `examples/probes/chan_dgram/src/chan_dgram.tks`
(`socket`/`bind`/`sendto`/`recvfrom`, `AF_UNIX=1`, `SOCK_DGRAM=2`, record framing). **No fixpoint reseed** — runtime
rebuild. **Linux-first** (the probe is `#os("linux")`; macOS/Windows mailslot is a later port, §10.2 L808).

### C0c.1 — signatures (add to `teko_rt.h`)

```c
/* tk_oschan — an AF_UNIX SOCK_DGRAM MPSC transport: ONE reader socket bound to a key-derived ABSTRACT-namespace
 * name (sun_path[0]==0, Linux — auto-removed on close, no filesystem cleanup), N writers each sendto that name.
 * Datagram boundaries make each `push` atomic for free (no torn frame). Lives in F2 (tk_region_program). Fixed-
 * width elements only in v1: `elem_size` raw little-endian bytes per datagram (the same portable byte layout
 * tkb_buf.tks uses). Fat T (str/slice) across the socket = a length-prefixed frame, deferred (mirrors the .tkj
 * frame; honest-stopped by C3's checker gate — see C3). §10 C0c. */
typedef struct tk_oschan tk_oschan;

/* tk_oschan_make — create+bind the reader socket to the abstract name derived from `key`; set SO_RCVBUF from
 * `bounds` (bounds==0 => the OS default, effectively unbounded). Returns the F2-resident handle, or tk_panic on a
 * socket/bind failure (the surface maps this to `make`'s `error`). */
tk_oschan *tk_oschan_make(uint64_t elem_size, uint64_t bounds, const char *key, uint64_t key_len);

/* tk_oschan_send — sendto the reader's bound name (a writer socket is lazily opened per calling thread and cached
 * thread-local). Returns 0 on accept, -1 when the receive queue is full (EAGAIN under MSG_DONTWAIT for a bounded
 * channel — the observable back-pressure the probe proved); a blocking (bounds==0) send omits MSG_DONTWAIT. */
int tk_oschan_send(tk_oschan *ch, const void *elem);

/* tk_oschan_recv — recvfrom the reader socket (blocking). Returns 1 with `out` filled (elem_size bytes), or 0 on
 * the CLOSED sentinel (a zero-length control datagram the producer's close sends, distinguishable because a valid
 * record is exactly elem_size>0 bytes). */
int tk_oschan_recv(tk_oschan *ch, void *out);

/* tk_oschan_close — send the zero-length CLOSED sentinel so a blocked reader wakes and drains; idempotent. */
void tk_oschan_close(tk_oschan *ch);

/* tk_oschan_end — close + close(2) the reader fd (abstract name vanishes) + close cached writer fds. Idempotent. */
void tk_oschan_end(tk_oschan *ch);

/* tk_oschan_selftest — the fan-in proof as a C-owned body reached by one scalar extern: `w` writer threads each
 * send `r` records carrying (writer_id, seq, pattern=f(id,seq)); the reader verifies all three per record (proof by
 * CONTENT not count — the probe's own warning). Returns 0 on success. */
uint64_t tk_oschan_selftest(int64_t w, int64_t r);
```

### C0c.2 — socket lifecycle + T-across-the-boundary

- **Bind name from key:** `sun_path[0] = 0` then the bytes `"tkchan\0" + key` (abstract namespace, Linux). No
  filesystem node, so a crashed process leaks nothing and two runs never collide on a stale path. `addrlen =
  offsetof(sun_path) + 1 + prefix_len + key_len`.
- **One reader, N writers (MPSC):** `make` (called once, by the creator) creates+binds the reader fd, stored in the
  F2 handle. Each writer thread opens its OWN unbound `AF_UNIX/SOCK_DGRAM` socket on first `send` (cached in a
  `_Thread_local` slot keyed by the channel pointer) and `sendto`s the reader's abstract name. No `accept`, no
  per-writer handler (the probe's exact shape).
- **T serialization:** the Teko layer hands `elem_size` contiguous bytes; `send` copies them into a stack record and
  `sendto`s the whole frame; `recv` `recvfrom`s into `out`. Because a datagram preserves boundaries, one record = one
  frame, boundary-safe, little-endian-portable (an arm64 CI datagram reads on x86_64). **v1 restricts T to
  fixed-width POD** (`elem_size` known, no interior pointers) — `ref` is already forbidden across the boundary (§10.6)
  and fat `str`/slice marshalling is the deferred length-prefixed extension gated in C3.
- **Closed:** `close` sends one zero-length datagram; `recv` returns 0 on a zero-length frame (valid records are
  `elem_size > 0`), delivering `Closed` after the real records drained through the OS queue.

### C0c.3 — fixture `oschan_fanin`

New probe `examples/probes/oschan/` (or extend `chan_dgram`): `tk_oschan_selftest(4, 500)` → **42** on all
`4*500` records verified by content; **50** if the flood phase never observes `EAGAIN` (the bounded refusal
regressed); **1** on any content mismatch. Reuses the probe's `writer_count()=4`/`records_per_writer()=500`/
`flood`/`SO_RCVBUF` methodology verbatim.

---

## Crumb C1 — `IChannelKind<T>` / `Ctx` / `Rx<T>` / `Tx<T>` (stdlib, NEW `teko::threads`) — `[L]` leaf

**Files:** new `src/threads/threads.tks` (namespace `teko::threads`, §10.5 L1039) + its registration in the module
graph. `[L]` leaf: the compiler never instantiates these, so the reseed is byte-identical/mechanical (the module is
compiled into the seed's stdlib but exercised only by user programs + the C1 fixture). Carries the **Gap-2 de-risk
fixture**.

### C1.1 — the interface (bare atom — Gap 1 resolution)

```
/**
 * IChannelKind — the extensible transport contract for a `chan<T>` (§10.2). A conformer is a `service singleton`
 * that binds a key to some medium (in-process FIFO, AF_UNIX datagram, Kafka, …) and moves values of `T` across it.
 * `init` is the prior step that ties the transport to the constant channel key; `end` closes and drains. The
 * constraint that references this interface is spelled BARE (`K: service singleton & IChannelKind`) — the nominal
 * conformance gate needs no type argument (Gap 1), and `T` flows through the owner `chan<T>` and the Rx/Tx wrappers.
 *
 * @param T the element type carried across the channel (copied, never referenced — §10.6)
 * @since 0.3.1 §10 C1
 */
type IChannelKind<T> = interface {
    /**
     * init — bind this transport to the channel's constant key (open the FIFO/socket/topic).
     * @param key the channel's constant key
     */
    fn init(key: str)
    /**
     * send — move one value across the transport (blocking per the channel's bounds).
     * @param v the value to send (by copy)
     */
    fn send(v: T)
    /**
     * recv — receive one value from the transport (blocking).
     * @return the next value
     */
    fn recv(): T
    /**
     * end — close and drain the transport.
     */
    fn end()
}
```

### C1.2 — `Closed` and the handle/context layouts

```
/**
 * Closed — the specific `Rx.pop` sentinel meaning "the channel is closed AND drained" (§10.2 L711), distinct from a
 * transport `error`. A nominal empty struct so `T | Closed` is a two-level nominal-variant union the reader matches.
 *
 * @since 0.3.1 §10 C1
 */
type Closed = struct { }

/**
 * Rx — the single-reader handle of a `chan<T>`, resolved by key via `svc<Rx<T>>(key)` (§10.2). Opaque: it carries
 * only the F2 channel registry id; every op is a monomorphized direct call into the concrete transport (constant
 * key) or a function-pointer indirection (variable key, C4). Never constructed by hand — the compiler registers it
 * and `make` materializes it.
 *
 * @param T the element type
 * @field _id the channel's F2 registry type-id (svc_type_id of the derived channel-key symbol)
 * @since 0.3.1 §10 C1
 */
pub type Rx<T> = struct {
    _id: u64
    /**
     * pop — receive the next value, or `Closed` once the channel is closed and drained.
     * @return the next `T`, or `Closed`
     */
    pub fn pop(): T | Closed { .{ }  /* C4 lowers this to tk_*_recv + decode; body is a phantom until C4 */ }
    /**
     * add — register one consumer on the channel's WaitGroup by this handle (the ctx is transient — §10.2 L713).
     */
    pub fn add() { }
    /**
     * done — signal this consumer's completion on the WaitGroup by this handle.
     */
    pub fn done() { }
}

/**
 * Tx — a writer handle of a `chan<T>` (N per channel), resolved by `svc<Tx<T>>(key)`. The PRODUCER closes (§10.2
 * L710). `closed` observes drain; the reader observes `Closed`.
 *
 * @param T the element type
 * @field _id the channel's F2 registry type-id
 * @field _closed set by `close`; read via `closed`
 * @since 0.3.1 §10 C1
 */
pub type Tx<T> = struct {
    _id: u64
    _closed: bool
    /**
     * send — send one value (by copy). Returns null; `closed` reports drain.
     * @param v the value to send
     * @return null
     */
    pub fn send(v: T): null { null }
    /**
     * close — the producer closes: invoke the transport `end` (close + drain); idempotent.
     */
    pub fn close() { }
    /**
     * add — register one producer on the WaitGroup by this handle.
     */
    pub fn add() { }
    /**
     * done — signal this producer's completion on the WaitGroup by this handle.
     */
    pub fn done() { }
    /**
     * closed — has the channel been closed (and is it draining/drained)?
     * @return true once `close` (or the transport `end`) has fired
     */
    pub get closed(): bool { self._closed }
}

/**
 * Ctx — the channel's OWNER handle (§10.2 L789): a MANUAL WaitGroup plus the channel-lifetime teardown. Transient,
 * resident in the creator's arena; when it drops it cascades `end` -> deregister key -> free the F2 singletons.
 * `make` returns it; the creator alone holds it (workers reach add/done through their tx/rx handles).
 *
 * @field _id the channel's F2 registry type-id (identifies the WaitGroup + transport to tear down)
 * @since 0.3.1 §10 C1
 */
pub type Ctx = struct {
    _id: u64
    /**
     * add — register `n` awaited tasks on the WaitGroup (call BEFORE spawn for the race-free path — §10.2 L718).
     * @param n how many tasks to await
     */
    pub fn add(n: usize) { }
    /**
     * wait — block until the WaitGroup counter reaches zero.
     */
    pub fn wait() { }
    /**
     * close — the reserve close of the channel (invoke transport `end`); idempotent with the producer's close.
     */
    pub fn close() { }
}
```

> The method bodies above are DELIBERATELY inert scaffolding that compiles today; **C4/C5 replace them** with the
> real lowering (a constant-key channel monomorphizes `pop`/`send`/`wait` to direct `tk_memchan_*`/`tk_oschan_*`/
> `tk_waitgroup_*` calls). Shipping C1 as compiling scaffolding lets C2–C5 land against a stable surface.

### C1.3 — fixture `rx_pop_closed` (the Gap-2 stamp de-risk) — `[L]`

A regression `examples/regressions/rx_pop_closed/`: declare a local `Rx<i32>` over a trivial in-module fake channel
(or, once C2 lands, a real `MemChan<i32>`), send two `i32`, close, and match:

```
loop {
    var v = rx.pop()               // v : i32 | Closed
    match v {
        i32    as x => sum = sum + x,
        Closed      => break
    }
}
if sum != 3 { teko::process::exit(1) }   // 1 + 2
teko::process::exit(0)
```

Expected native exit: **0** (both arms of `i32 | Closed` stamp and match on `Rx__g__i32`), **1** on a wrong sum. This
is the concrete gate that flips Gap 2 from "expected-closed" to "proven-closed"; a stamping failure here is the
only trigger for the contingency codegen crumb.

---

## Crumb C2 — `MemChan<T>` (stdlib) — the `IChannelKind` conformer over `tk_memchan_*` — `[L]`

**File:** `src/threads/threads.tks` (or `memchan.tks` in the same ns). A `service singleton` struct conforming to
`IChannelKind`, wrapping the C0b handle.

```
/**
 * MemChan — the built-in in-process FIFO transport (§10.2 L808): no syscall, an F2-resident bounded/unbounded
 * queue. A `service singleton` conforming to `IChannelKind` (bare-atom conformance — Gap 1). Moves `T` as
 * `size_of<T>` raw bytes through tk_memchan_*.
 *
 * @param T the element type (fixed-width or fat — MemChan copies exactly size_of<T> bytes; str/slice are copied by
 *          their (ptr,len) header into the same F2 arena via the deep-copy the send lowering emits, C4)
 * @field _h the C0b channel handle (u64)
 * @since 0.3.1 §10 C2
 */
service MemChan<T> singleton & IChannelKind {
    _h: u64

    /**
     * init — open the F2 FIFO for this channel key with the bounds the compiler recorded at `make` (§10.2 L698).
     * @param key the channel's constant key
     */
    fn init(key: str) { self._h = memchan_make(size_of<T>(), self._bounds_for(key)) }

    /**
     * send — copy one `T` into the FIFO tail (blocks while a bounded channel is full).
     * @param v the value to send
     */
    fn send(v: T) { memchan_send(self._h, addr_of(v)) }

    /**
     * recv — receive the next `T` (blocks while empty; the Closed edge is surfaced by Rx.pop, not here).
     * @return the next value
     */
    fn recv(): T { var out: T = zeroed<T>(); memchan_recv(self._h, addr_of(out)); out }

    /**
     * end — close and drain the FIFO.
     */
    fn end() { memchan_end(self._h) }
}
```

- `memchan_make`/`memchan_send`/`memchan_recv`/`memchan_end` are `extern fn … from "teko_rt"` thin binders over
  C0b (declared in `teko::threads`). `size_of<T>()`/`addr_of`/`zeroed<T>` are the existing marshalling intrinsics
  (§5 marshall, `plano-secao5-marshall.md`); if `addr_of`/`zeroed<T>` are not both present today, C2 honest-stops
  the missing one with a message pointing at the marshall crumb — but the `str`-keyed `Map<V>` already round-trips
  fat values through F2, so the machinery exists.
- **Bounds:** the compiler knows `bounds` from the constant-key `make` call; C4 threads it into `init` (a
  monomorphized constant), so `_bounds_for` is a C4-provided detail, not a runtime lookup.
- Fixture `memchan_roundtrip` (`[L]` regression): `MemChan<i32>` send 1..100, recv, assert order+sum → exit **0**;
  mismatch → **1**.

---

## Crumb C3 — `OsChan<T>` (stdlib) — the conformer over `tk_oschan_*` — `[L]`

**File:** `src/threads/threads.tks`. Identical shape to C2 over the C0c binders, plus the **fixed-width gate**.

```
/**
 * OsChan — the built-in AF_UNIX SOCK_DGRAM transport (§10.2 L808, the default): cross-task via the kernel, one
 * reader / N writers, datagram-atomic. A `service singleton & IChannelKind`. Linux-first (C0c); v1 carries only
 * FIXED-WIDTH `T` (a compile gate rejects fat `T` with the length-prefixed-frame honest stop).
 *
 * @param T the element type (fixed-width POD in v1)
 * @field _h the C0c channel handle (u64)
 * @since 0.3.1 §10 C3
 */
service OsChan<T> singleton & IChannelKind {
    _h: u64

    /**
     * init — create+bind the reader socket for this channel key (abstract namespace), sized by the compiler's bounds.
     * @param key the channel's constant key
     */
    fn init(key: str) { self._h = oschan_make(size_of<T>(), self._bounds_for(key), key, key.len) }

    /**
     * send — sendto one `T` as a fixed-width datagram (a bounded-full send blocks or reports back-pressure per C0c).
     * @param v the value to send
     */
    fn send(v: T) { oschan_send(self._h, addr_of(v)) }

    /**
     * recv — recvfrom the next `T` (blocking).
     * @return the next value
     */
    fn recv(): T { var out: T = zeroed<T>(); oschan_recv(self._h, addr_of(out)); out }

    /**
     * end — close + tear down the sockets.
     */
    fn end() { oschan_end(self._h) }
}
```

- **Fixed-width gate (v1 scope line):** if `T` is fat (`str`/`[]U`/an interior-pointer struct), `OsChan<T>`
  instantiation honest-stops: `"OsChan<T> v1 carries only fixed-width values across the datagram boundary; T = <T>
  needs the length-prefixed-frame marshalling (a later crumb — see the .tkj frame). Use MemChan<T> for fat T
  in-process, or wrap T in a fixed record."` MemChan has no such restriction (it copies through F2). This is the same
  gated-scope discipline S2 used for spawn's copyable-arg boundary — reported here, not a new issue.
- Fixture `oschan_roundtrip` (`[L]`): `OsChan<i64>` fan-in 4 producers × 250, reader verifies content → exit **0**;
  mismatch → **1**. Plus REJECT `oschan_fat_T` (`OsChan<str>`) → the fixed-width honest stop.

---

## Crumb C4 — `chan<T>::make<K>` + `svc<Rx/Tx>` DI-by-key (compiler + stdlib) — `[C]` fixpoint + reseed

This is the ONE compiler-touching channel crumb. Two compiler seams + the `chan<T>` stdlib type. It reuses the **F3
phantom-owner factory** (`retarget_generic_static_callee` `typer.tks:3159`, `phantom_owner_subst` `:3216`, the
routing at `:2920`) — the `Dictionary<StrKey,V>::make` analogue — for the owner-generic static factory.

### C4.1 — `chan<T>` as a real stdlib type with a phantom-factory `make<K>`

```
/**
 * chan — the channel FACTORY namespace-type (§10.2). Never instantiated as a value; it exists only to host the
 * static `make<K>` factory (like a `Dictionary<K,V>` hosts `make`). `make` creates the transport K under the
 * constant key, registers Rx/Tx/WaitGroup into F2, and returns the owning Ctx.
 *
 * @param T the element type carried by channels this factory makes
 * @since 0.3.1 §10 C4
 */
pub type chan<T> = struct {
    /**
     * make — create a `chan<T>` served by transport `K` under the constant `key` with `bounds` capacity (§10.2
     * L700). Calls K.init(key), registers the transport + a synthesized Rx<T>/Tx<T> + a WaitGroup into the F2
     * program region keyed by svc_type_id(derived key symbol), and returns the owning Ctx. Errors on a key
     * conflict (variable-key runtime path) or a K.init failure (opening the medium).
     *
     * @param key    the channel's CONSTANT key (a literal/const; comptime — §10.2 L776)
     * @param bounds capacity: 1 = bounded-1 (default), N = bounded-N, 0 = unbounded
     * @return the owning Ctx, or an error (key conflict / transport open failure)
     * @throws when the key conflicts (variable key) or K.init fails
     * @since 0.3.1 §10 C4
     */
    pub static fn make<K: service singleton & IChannelKind>(key: str, bounds: usize = 1): Ctx | error {
        .{ }   /* compiler-lowered; see C4.3 */
    }
}
```

- The constraint is the **bare atom** `service singleton & IChannelKind` (Gap 1 resolution). `service singleton`
  fires the Gap-3 form-word gate (non-singleton K = compile error). `IChannelKind` is the nominal conformance atom.
- `chan<i32>::make<MemChan<i32>>("nums", 64)` resolves through `retarget_generic_static_callee`: `owner_type_args =
  [i32]` mangles `chan` -> `chan__g__i32`; the method type-arg `MemChan<i32>` is K, bound downstream via
  `callee_type_args` (the `Map<str,i64>::fold<Acc>()` path, `typer.tks:3142`). Because the corpus never calls this,
  the phantom/mono machinery is exercised only by user programs + fixtures.

### C4.2 — `svc<Rx<T>>(key)` / `svc<Tx<T>>(key)` — the DI-by-key extension (the compiler seam)

`Rx`/`Tx`/`Ctx` are **not services** (§10.2 L771) — so they must NOT flow through `svc_providers`/`type_satisfies_
iservice` (which would reject them at `type_svc` `typer.tks:2011` with `svc_not_iservice_error`). Add a branch in
`type_svc` (`typer.tks:2005`), BEFORE the IService gate at `:2011`:

```
/**
 * type_svc_channel — resolve svc<Rx<T>>(key) / svc<Tx<T>>(key) against the F2 CHANNEL registry rather than the
 * §7 service-provider registry (Rx/Tx are compiler-registered handles, not user services — §10.2 L771). For a
 * CONSTANT key the compiler knows the concrete transport K it was made with, monomorphizes the handle + its
 * pop/send ops to direct tk_*_recv/tk_*_send calls, and emits a load of the F2-registered handle by
 * svc_type_id(derived key). For a VARIABLE key it emits a runtime tk_region_lookup + a function-pointer table
 * indirection (§10.2 L782), and a miss panics (svc is infallible in the type — guard with has_svc first).
 *
 * @param c     the raw svc<…>(key) call
 * @param tname the target name ("Rx" or "Tx" in teko::threads)
 * @param env   the typing environment
 * @param table the folded type table
 * @return the typed handle-load expression, NotSvcOp when tname is not Rx/Tx, or an error
 * @since 0.3.1 §10 C4
 */
fn type_svc_channel(c: parser::Call, tname: str, env: Env, table: TypeTable): TExpr | NotSvcOp | error { … }
```

- Wire it as the first thing after `svc_target_name` in `type_svc`: `match type_svc_channel(c, tname, env, table) {
  TExpr as te => return te; error as e => return e; NotSvcOp => { } }` — falls through to the ordinary service path
  for every non-channel `T`, so §7 DI is byte-identical.
- The **constant-key** path pairs each `svc<Rx<T>>("k")` to the `chan<T>::make<K>("k", …)` of the SAME constant (the
  spec's static/inline registration, §10.2 L774), so the concrete K and bounds are known at compile time — no runtime
  lookup, the ops monomorphize to direct transport calls. The **variable-key** path uses `svc_type_id` +
  `tk_region_lookup(tk_region_program(), id)` and a function-pointer op table (NOT an interface vtable — §10.2 L783).
- `svc_type_id` (`di.tks:10`, FNV-1a) is reused unchanged to derive the F2 registry key from the channel key symbol.

### C4.3 — the `make` lowering (reusing the F3 phantom fix)

For a **constant key**, codegen lowers `chan<T>::make<K>(key, bounds)` to:
1. materialize K (the `service singleton`) — construct + `K.init(key)` (K.init dispatches through the bare
   `IChannelKind` surface via #254; `init(key:str)` does not reference `T`, so no owner-T substitution — Gap 1);
2. `tk_region_register(tk_region_program(), svc_type_id(derived_key), transport_handle)` — the transport into F2;
3. register a synthesized `Rx<T>` and `Tx<T>` (each just `_id = svc_type_id(derived_key)`) into F2 under sibling ids
   (`derived_key ++ ".rx"` / `".tx"`), so `svc<Rx<T>>/svc<Tx<T>>` load them;
4. create the WaitGroup (`tk_waitgroup_make`, C5) into F2 under `derived_key ++ ".wg"`;
5. return `Ctx { _id = svc_type_id(derived_key) }`.

The F3 phantom-owner factory carries the abstract `chan<T>::make` through abstract typing and defers the concrete
stamp to mono (`phantom_owner_subst` `typer.tks:3216`) — the exact `Dictionary<StrKey,V>::make` mechanism, now with
an owner type-arg `T` and a method type-param `K` (the `fold<Acc>` shape, `typer.tks:3142`).

### C4.4 — fixtures

- `chan_make_svc` (regression, C backend): the §10.2 L728–751 mini-flow with a **constant key** and `MemChan<i32>`
  (`produz` sends 0..99 via `svc<Tx<i32>>("nums")`, `tx.close()`; `main` makes the channel, `ctx.add(1)`, `spawn
  produz()`, reads via `svc<Rx<i32>>("nums")` until `Closed`, `ctx.wait()`). Expected exit **0** (all 100 read, sum
  correct); **1** on a lost/duplicated/corrupt value.
- REJECT `chan_make_nonsingleton`: `chan<i32>::make<NotSingletonChan<i32>>(…)` where the transport is a
  non-`singleton` service → the Gap-3 form-word compile error (compiler exits nonzero; compile-must-fail fixture).
- REJECT `svc_rx_unmade_key`: `svc<Rx<i32>>("never_made")` with a constant key that pairs no `make` → a compile
  error (constant-key miss, §10.2 L775 "conflito … erro de compilação" twin for a missing pair).

---

## Crumb C5 — WaitGroup `Ctx.add/wait/close` + the drop-cascade (stdlib + maintained-C) — `[L]` + `[C-rt]`

**Files:** `teko_rt.{c,h}` (the counter + the block) + `src/threads/threads.tks` (the Ctx/handle wiring). Runtime
rebuild for the C part; leaf reseed for the Teko wrappers.

### C5.1 — the maintained-C WaitGroup (add to `teko_rt.{c,h}`)

```c
/* tk_waitgroup — a MANUAL counter with a blocking wait, F2-resident (survives producing tasks). add(n)/done()
 * mutate the count under a mutex; wait() blocks on a cond until the count reaches zero. §10 C5. */
typedef struct tk_waitgroup tk_waitgroup;
tk_waitgroup *tk_waitgroup_make(void);              /* count=0, in tk_region_program */
void  tk_waitgroup_add(tk_waitgroup *wg, int64_t n);/* count += n (call before spawn = race-free) */
void  tk_waitgroup_done(tk_waitgroup *wg);          /* count -= 1; broadcast not-busy at zero */
void  tk_waitgroup_wait(tk_waitgroup *wg);          /* block while count > 0 */
void  tk_waitgroup_end(tk_waitgroup *wg);           /* destroy mutex/cond (drop-cascade) */
uint64_t tk_waitgroup_selftest(int64_t n);          /* n threads each done() after an add(n); wait returns 0 */
```

```c
struct tk_waitgroup { int64_t count; pthread_mutex_t lock; pthread_cond_t zero; };
```

Same pthread-mutex/cond idiom as C0b (justified there). `add` before `spawn` is the race-free path; `done` via the
handle after the worker finishes; `wait` on the creator's `ctx`.

### C5.2 — the drop-cascade teardown + the `tk_region_deregister` addition

The `ctx` OWNS the lifetime (§10.2 L789): its teardown runs `end -> deregister key -> free F2 singletons`. Two
pieces:

1. **Transport + WaitGroup `end`:** `tk_memchan_end`/`tk_oschan_end`/`tk_waitgroup_end` release the heavy resources
   (ring buffer, sockets, pthread objects). These EXIST after C0b/C0c/C5.1.
2. **Key deregistration (new `[C-rt]`):** add `void tk_region_deregister(tk_region *r, uint64_t type_id)` to
   `teko_rt.{c,h}` — binds `type_id -> NULL` in `r`'s own table so a post-teardown `tk_region_lookup` MISSES. This is
   what makes `svc<Rx<T>>("k")` FAIL (not dangle) after teardown (§10.2 L794 "resolução por chave falha em vez de
   pendurar"). It is the arg-flip twin of `tk_region_register` (`teko_rt.c` region table).

**Where the cascade fires:** the `ctx` is transient in the creator's arena. Two triggers, in priority order:
- **`ctx.close()` (works today):** the explicit reserve close (§10.2 L800) invokes the full cascade — the shippable
  v1 path. Deterministic, no arena hook needed.
- **Automatic drop at scope end (dependency-noted):** the fully-automatic "ctx cai -> cascateia" (§10.2 L790) rides
  the §128 drop family (`drop-128-family-0.3.1.md`) + the NEW per-entry F2 reclamation (Doc 1 §7.8 — "a única
  capacidade nova de arena que isto exige"). Until BOTH are wired for `Ctx`, C5 emits the cascade from `ctx.close()`
  and, if a drop hook for `Ctx` is not yet available, HONEST-STOPS the implicit-drop path with:
  `"Ctx auto-teardown-on-drop needs the §128 drop hook + F2 per-entry reclaim; call ctx.close() explicitly for now
  (§10.2)"`. The `ctx.wait()` barrier already guarantees no worker outlives the region, so `close()`-driven teardown
  is UAF-safe by construction (§10.2 L792). **The small F2 struct SHELLS (memchan/oschan/waitgroup) are reclaimed at
  process exit via the program-region free until the per-entry F2 free lands — no leak of the heavy payload (buffer/
  socket/pthread are freed by `end`), a bounded shell residue only.** Reported as a scope line, not a new issue.

### C5.3 — the Ctx/handle wiring (stdlib)

`Ctx.add(n)` -> `tk_waitgroup_add(wg, n)`; `Ctx.wait()` -> `tk_waitgroup_wait(wg)`; `Ctx.close()` -> the cascade
(C5.2). `Tx.add/done` and `Rx.add/done` -> the SAME `wg` reached by `_id` (the handle carries the F2 id, so a worker
that only holds its `tx`/`rx` still reaches the WaitGroup — §10.2 L713). `wg` is loaded from F2 by `_id ++ ".wg"`.

### C5.4 — fixtures

- `waitgroup_barrier` (regression): `ctx.add(8)`; `spawn worker(i)` × 8 where each worker `svc<Tx<i32>>("k")`, sends,
  `tx.done()`; `ctx.wait()` must return only after all 8 `done` → exit **0**; a premature return (broken counter) is
  caught by a post-wait invariant check → **1**.
- Runtime probe `tk_waitgroup_selftest(1000)` under `TEKO_MEM_PARANOID=1` → **42**/**1** (like C0b).
- REJECT (optional) `ctx_implicit_drop` — if the drop hook is not wired, asserts the honest stop fires rather than a
  silent leak.

---

## Ritual per crumb — reseed table (HEAD `2d084b3d`)

| Crumb | Kind | Reseed | Corpus byte-identical? |
|-------|------|--------|------------------------|
| **C0b** `tk_memchan_*` | `[C-rt]` maintained-C | **runtime rebuild only; NO fixpoint reseed** | YES — emitted `teko.c` unchanged (new C symbol, never emitted) |
| **C0c** `tk_oschan_*` | `[C-rt]` maintained-C | **runtime rebuild only; NO fixpoint reseed** | YES — same reasoning |
| **C1** `teko::threads` types | `[L]` stdlib leaf | **additive leaf reseed** | YES — corpus never instantiates Rx/Tx/Ctx |
| **C2** `MemChan<T>` | `[L]` stdlib leaf | **additive leaf reseed** | YES |
| **C3** `OsChan<T>` | `[L]` stdlib leaf | **additive leaf reseed** | YES |
| **C4** `chan::make` + `svc<Rx/Tx>` | `[C]` compiler (typer + codegen) + stdlib | **fixpoint + reseed** | YES — compiler's own image never calls `chan::make`/`svc<Rx>`, so gen1==gen2 |
| **C5** WaitGroup + deregister | `[C-rt]` (`teko_rt` + `tk_region_deregister`) + `[L]` stdlib | **runtime rebuild + additive leaf reseed** | YES |

**Ritual points (full gate MUST pass):** after **C4** (the sole compiler-touching crumb) — fixpoint byte-identity +
full regression. C0b/C0c/C5-runtime gate via their `TEKO_MEM_PARANOID=1` probes + a clean runtime rebuild (no
compiler fixpoint). C1/C2/C3/C5-stdlib gate via the additive leaf reseed + their regression fixtures. **NEVER run
`teko test .` (OOM)** — gate via the sharded/regression runners.

---

## Recommended ORDER + the single first implementable crumb

Dependency spine: **C0b, C0c** (runtime, mutually independent) → **C1** (types; needs nothing but the surface) →
**C2** (needs C0b + C1) · **C3** (needs C0c + C1) → **C4** (needs C1 + C2/C3 + the F3 phantom fix + `svc` seam) →
**C5** (needs C4 for the Ctx wiring; the C-rt WaitGroup can land alongside C0b/C0c).

Ordered, each independently gate-able:
1. **C0b** `tk_memchan_*` — pure maintained-C on the landed `tk_region_program` + pthread; no reseed. **← FIRST
   implementable crumb** (fully unblocked, smallest, no sockets).
2. **C0c** `tk_oschan_*` — maintained-C, lift from `chan_dgram`; no reseed. (Parallel with C0b.)
3. **C1** `teko::threads` (`IChannelKind`/`Closed`/`Rx`/`Tx`/`Ctx` scaffolding) + the **Gap-2 `rx_pop_closed`
   fixture**. Additive leaf reseed.
4. **C2** `MemChan<T>` conformer (+ `memchan_roundtrip`). Leaf reseed.
5. **C3** `OsChan<T>` conformer + fixed-width gate (+ `oschan_roundtrip`, `oschan_fat_T` reject). Leaf reseed.
6. **C4** `chan::make<K>` + `svc<Rx/Tx>`-by-key (+ `chan_make_svc`, `chan_make_nonsingleton` reject,
   `svc_rx_unmade_key` reject). **Fixpoint + reseed.** The one ritual point.
7. **C5** WaitGroup + `tk_region_deregister` + Ctx wiring (+ `waitgroup_barrier`, `tk_waitgroup_selftest`). Runtime
   rebuild + leaf reseed.

**First implementable crumb: C0b (`tk_memchan_*`)** — maintained-C, zero new deps, no reseed, drives the whole
channel spine; the `memchan_pingpong` probe under `TEKO_MEM_PARANOID=1` gates it to exit **42**.

---

## Risks + law tensions (recommended resolutions)

- **R1 — Gap 1 literal-spelling parse failure (spec vs grammar).** The spec writes `IChannelKind<T>` in the
  constraint; the constraint grammar cannot parse a parameterized atom. RESOLUTION: the bare atom
  `service singleton & IChannelKind` is semantically sufficient (nominal conformance, §0 Gap 1) — adopt it; no
  crumb. The literal `<T>` spelling is an OPTIONAL doc-parity parser enhancement, reported up, NOT built. No law
  tension (the surface's OBSERVABLE — which transports `make` accepts — is unchanged).
- **R2 — Gap 2 nominal-union stamp is the one unproven composition.** RESOLUTION: the `rx_pop_closed` fixture in C1
  proves `Rx__g__i32.pop(): i32 | Closed` stamps + matches before C2 depends on it; contingency is a small
  root-map codegen crumb (`native-variant-match-root-map`). Not a law tension — a fixture-gated de-risk.
- **R3 — OsChan v1 fixed-width-only.** Fat `T` across the datagram needs the length-prefixed frame (the `.tkj`
  mechanism), deferred. RESOLUTION: C3 honest-stops fat `T` with a message naming MemChan as the in-process
  alternative — the same gated-scope discipline spawn used (S2). Reported, not a new issue.
- **R4 — automatic ctx-drop teardown depends on §128 drop hooks + Doc 1 §7.8 per-entry F2 free.** Neither is a
  channel-batch deliverable. RESOLUTION: ship the cascade on `ctx.close()` (works today, deterministic, UAF-safe via
  the `ctx.wait()` barrier); honest-stop the implicit-drop path if no `Ctx` drop hook exists yet; the small F2
  shells reclaim at process exit (heavy payload freed by `end`). No law tension — an explicit, gated scope line.
- **R5 — pthread mutex/cond vs the atomic spinlock.** Adding cond-var blocking is new runtime behaviour.
  RESOLUTION: `pthread.h` + `-pthread` are already landed (C0a); cond-vars are the right tool for a
  millisecond-scale channel wait (the spinlock is for microscopic region critical sections). §16 may swap to a
  futex inside `teko_rt.c` with zero surface impact. Not a tension.
- **R6 — `chan` as a lowercase type name.** `chan<T>` sits in type position (`chan<i32>::make`), while the compiler's
  own `var chan` (`regression.tks:206`) is an expression-position local — no collision (context-separated, and the
  corpus never writes `chan<T>::make`). Not a tension.

**No unresolved tension — nothing HALTs in this batch.** (D2/await is the only genuine owner-fork and is OUT of
scope; channels are model-independent of it.)

---

## Final anchors (file:line on HEAD `2d084b3d`)

- **Runtime:** `teko_rt.c:2321` `tk_region_program`, `:2308` its "owned by no task" note, `:2497` `tk_task_begin`,
  `:2505` `tk_task_end`, `:2458` `tk_names_live_count`, `:1591/:1598` `tk_lock_acquire`/`release` (atomic spinlock —
  the fast-path idiom, NOT the channel wait), `:1387` `_Thread_local tk_g_current_task`, `:43` `#include <pthread.h>`
  (C0a). Header: `teko_rt.h:248` `tk_region_program`, `:292` `tk_region_register`, `:295` `tk_region_lookup`,
  `:215/216` `tk_task_begin`/`tk_task_end`.
- **DI:** `di.tks:10` `svc_type_id` (FNV-1a — the F2 registry key derivation). `typer.tks:2005` `type_svc` (the
  `svc<Rx/Tx>` extension point, add `type_svc_channel` before the IService gate at `:2011`), `:1762`
  `svc_target_name`, `:1795` `svc_providers`, `:1826` `svc_service_implements`, `:2031` `rtype = Named{tname}`.
- **Constraint gaps:** `parse_decl.tks:119-141` `parse_constraint_atom` (bare name, NO type args — Gap 1),
  `:183/:203-205` `parse_type_params` (the `,`/`>` terminator that rejects `IChannelKind<T>`), `:127-132`
  `service singleton` -> `ConstraintForm`, `:1135` `parse_service_lifetime`. `ast.tks:568` `ConstraintAtom{name}`,
  `:589` `ConstraintForm{form,has_lifetime,lifetime}`, `:799` `ServiceLifetime`. `monomorph.tks:53`
  `constraint_atom_satisfied` (`:74` nominal InterfaceBody arm), `:93` `constraint_satisfied`, `:115-129`
  `constraint_form_satisfied` (`:121` the `has_lifetime` singleton gate — Gap 3), `:167` `check_constraints`, `:345`
  `subst_typeexpr` union-member subst (Gap 2). `resolve.tks:1701` `type_conforms_to` (bare-name nominal — Gap 1),
  `:949/1003` `atom_surface`/`constraint_surface`, `:1569` `is_service_name`, `:1584` `service_lifetime_of`, `:1598`
  `lifetime_eq`. `collect.tks:1406` `iface_methods_by_name`.
- **Gap 2 precedent:** `dictionary.tks:86` `get(k:K): V | null` (generic union-member return, stamps today);
  `sorted_dictionary.tks:76`, `map.tks:98` further instances.
- **F3 phantom factory (C4):** `typer.tks:3159` `retarget_generic_static_callee`, `:3142` "method type-args bind
  downstream" (K), `:2481` `name_is_phantom_instance`, `:2920` phantom routing, `:3216` `phantom_owner_subst`.
- **Transport probe (C0c):** `examples/probes/chan_dgram/src/chan_dgram.tks` — `af_unix()=1`, `sock_dgram()=2`,
  `sol_socket()=1`, `so_rcvbuf()=8`, `msg_dontwait()=64`, `eagain()=11`, `record_bytes()=24`,
  `sockaddr_un_bytes()=110`, `sun_path_offset()=2`, `writer_count()=4`, `records_per_writer()=500`; externs
  `c_socket`/`c_bind`/`c_sendto`/`c_recvfrom` (`#os("linux")`).
- **Spawn precedent (for the probe/selftest pattern):** `plano-s10-spawn-batch-detalhe.md` C0a — `tk_thread_spawn`,
  `tk_thread_spawn_selftest`, the `TEKO_MEM_PARANOID=1` → exit-42 probe idiom the C0b/C0c/C5 selftests copy.
</content>
