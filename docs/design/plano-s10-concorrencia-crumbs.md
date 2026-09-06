# §10 Concurrency — implementer-ready crumb sequence (reconstructed from the architect pass)

**Status:** DESIGN-AHEAD (architect, 2026). No product code changed. Grounded on `fix/retirement` HEAD `7721a1d7`.
**Spec:** `docs/design/mudancas-superficie-0.3.1.md` §10 (~L669-1052, sealed). Prior design:
`concorrencia-isolate-spawn-chan-0.3.1.md` (spawn=library-fn Ruling B), `plano-collections-genericas-e-concorrentes`
§3 (concurrent collections depend on §10).
**Runtime today:** `tk_task`/`tk_task_current`/`tk_task_begin` (`teko_rt.c:2496`)/`tk_task_end` (`:2507`) — arena/task
state; `tk_region_program` (F2 singleton region surviving task exit); C11 `__atomic_*`. **NO OS thread-creation
primitive, NO scheduler/executor.** AF_UNIX SOCK_DGRAM transport proven by `examples/probes/chan_dgram/`.

> NOTE: this doc was reconstructed after a filesystem-snapshot rewind lost the architect's original file. It
> captures the architect's decisions + crumb spine faithfully; regenerate the full-detail version if needed.

---

## Decision D1 — how `spawn` creates an OS thread: **CONSTRAINT-FORCED (not owner-ruling)**

Resolution: a **new maintained-C primitive `tk_thread_spawn` in `teko_rt.{c,h}`** (detached; joinable twin for
D2-option-a). NOT a Teko `extern fn` to `pthread_create`, NOT raw `clone(2)`. Forced because the arena discipline
requires `tk_task_begin()` as the new thread's FIRST instruction and `tk_task_end()` as its LAST — a bracket Teko
cannot express, so it lives in the C trampoline (same reason `tk_test_run` owns its `setjmp`, `teko_rt.h:461`).
`tk_task_begin/end` already exist with nothing to create the thread they bracket — this primitive is the sibling
they were waiting for. pthread-vs-clone-vs-CreateThread is an implementation detail INSIDE the maintained runtime,
correctly deferred to §16/§17 (which rewrites all of `teko_rt.c`, including this, to Teko/FFI). The rejected
`extern fn` form does not even compile today (`cabi fn(T…): R` is not a token this lexer mints).

## Decision D2 — the `await` suspension model: **OWNER-RULING (genuine architecture fork) — HALT**

Constraints do NOT settle it. Sealed §10.3 (L939) asks for a REACTOR ("cede, nunca bloqueia") for I/O, but
NO-VM + no-`async`-keyword makes continuation-reification a large AOT/arena transform with zero scaffolding today.
**The one owner question:**

> Ship v1 as **thread-per-await** (Option a — structurally `spawn`+`join`; simple, one OS thread per outstanding
> await, awaiting thread blocks on join, `Intent` semantics identical) and defer the reactor; OR invest now in
> state-machine lowering to honour "never blocks the thread"?

**Architect recommendation: (a) thread-per-await for v1** — the only model buildable on the runtime that exists
once D1 lands, preserving every observable of §10.3. **Only crumb A4 (await lowering) + the suspension-aware half
of CN1 (cancel) are blocked on this ruling; ALL await front-end (Intent<T> type, parser, checker widening, cancel
recognition) is model-independent and proceeds regardless.**

---

## Ordered crumb spine ([C]=compiler→fixpoint+reseed · [C-rt]=maintained-C runtime · [L]=stdlib leaf)

- **C0a** `tk_thread_spawn` + join twin — **[C-rt]** ← FIRST implementable crumb (unblocked, no reseed by itself)
- **C0b** `tk_memchan_*` (F2 FIFO + futex lock) — **[C-rt]**
- **C0c** `tk_oschan_*` (AF_UNIX DGRAM, from the probe) — **[C-rt]**
- **S1** parser: `Spawn` AST node + contextual `spawn` recognition — **[C]**
- **S2** checker: ref-guard (no `ref` across boundary) + arg-copy — **[C]**
- **S3** codegen: ctx-blob + `cabi` trampoline + `tk_thread_spawn` — **[C]** (hardest spawn crumb)
- **C1** `IChannelKind<T>`/`Ctx`/`Rx<T>`/`Tx<T>` in new `teko::threads` — **[L]**
- **C2** `MemChan<T>` — **[L]** · **C3** `OsChan<T>` — **[L]**
- **C4** `chan<T>::make<K>` + `svc<Rx/Tx>` DI-by-key — **[C? may flip from L — see generic-gaps]**
- **C5** WaitGroup (`ctx.add/wait`, handle add/done) — **[L]+[C-rt]**
- **A1** `Intent<T>`/`Intent` protected structs — **[L] UNBLOCKED**
- **A2** `await`-prefix parser (`awaited` flag on Binding/MultiBind) — **[C] UNBLOCKED**
- **A3** `await` checker (widen return `T`→`Intent<T>`, ref-guard) — **[C] UNBLOCKED**
- **A4** `await` lowering — **[C] BLOCKED ON D2**
- **CN1** `cancel()` (panic-outside UNBLOCKED; under-await BLOCKED on D2) — **[C]+[C-rt]**
- **J1** `teko::journal` (mirrors chan DI-by-key/F2 model) — **[L]**

**First implementable crumb: C0a** — `tk_thread_spawn` in `teko_rt.{c,h}`. Fully unblocked, maintained-C only,
zero deps, unblocks the whole spine. Fixture `thread_spawn_paranoid`: 100 threads × task_begin/alloc/task_end under
`TEKO_MEM_PARANOID=1`, assert `tk_names_live_count` returns to baseline.

## §16 boundary — CONFIRMED: built-in transports + spawn land on the CURRENT runtime

`tk_thread_spawn`, `MemChan` (pure F2 FIFO, no syscall), `OsChan` (AF_UNIX DGRAM via proven `chan_dgram` syscalls)
are all maintained-C seam — NOT §16. Only **user-pluggable transports** (Kafka/Rabbit/WS — "link dinâmico FFI a
lib de sistema", §10.2 L809) need dynamic-FFI (`extern fn … from lib "c"`, per-OS) — the §16→§17-gated extension.

## Residual generic-machinery gaps to verify before assuming C4/A1 are leaves

1. Method type-param `K` whose constraint `IChannelKind<T>` references the owner's `T` — beyond the delivered
   cross-ns factory; if mono doesn't substitute `T` into `K`'s constraint, **C4 flips [L]→[C]**.
2. `Intent<T>._value: T | null` — union member monomorphized over `T` (composes with the two-level nominal-variant
   match trap); verify `Intent__g__i32` stamps at `resolve.tks:1594`.
3. `K: service singleton` as a mono-gate constraint (non-singleton = compile error) — verify `di.tks`/
   `svc_service_implements` enforces it; if not, C4 grows a checker crumb.

## Dogfood / self-image

Keep §10 a **LEAF** — the parallel-codegen axis-2 uses a SEPARATE internal `fork_join` (§10.2 L757), NOT the
`spawn`/`chan` surface. As long as `src/` never instantiates §10, the `any_generic` no-op guard holds and every
reseed is byte-identical/mechanical. Do NOT let the compiler adopt the §10 surface during this issue — it would put
trampolines/`chan<T>` monomorphizations into the compiler binary, break fixpoint byte-identity, and make the D2
choice load-bearing for the compiler's own build.
