# Plano §10 — MULTITHREADING NATIVO (retire pthread on the OS's own raw facility, both legs)

Status: DESIGN (architect) — R1 RATIFIED (owner 2026-08-17, DECIDED = B; §5 / §10). Base:
`origin/fix/retirement`. Design-doc only — NO product code is written here. This is the crumb spine an
implementer resumes from; M0 (the leaf `teko::sys` numbers + flags) is landed.

Owner ruling that forces this pass: *"pthread is a dependency the NATIVE backend cannot rely on / cannot
link, so it must be built BY HAND on the OS's own raw facility (can be inline asm); the OS itself already
has the primitive. Whenever you touch C codegen you must think how it works in native."* The §16 goal is
to remove ALL C/library deps (`teko_rt.c/.h` + `win32_compat`) in favour of Teko + FFI-to-the-OS.

This doc REPLACES the pthread substrate under §10 concurrency: thread spawn/detach/join
(`teko_rt.c` `(§10 C0a)` block, ~L2565-2638) and the `pthread_mutex_t`/`pthread_cond_t` that back
channels (`tk_memchan`, ~L2640-2788) and waitgroups (`tk_waitgroup`, ~L2790-2851). It supersedes §10
Decision **D1** ("a maintained-C `tk_thread_spawn` on pthread, deferred to §16/§17") — this IS that §16/§17
rewrite. It leaves §10 Decision **D2** (thread-per-await vs reactor) untouched: this pass changes only the
SUBSTRATE `spawn`/`join` ride on, not the await model; thread-per-await (the D2 recommendation) sits on the
raw substrate identically to how it sat on pthread.

It mirrors the §16 keystones already landed and running: the raw-syscall codegen intrinsic
(`docs/design/plano-s16-syscall-intrinsic.md`, `teko::sys::syscall0..6`), the `teko::sys` per-`#os`/`#arch`
number module (`src/sys/sys.tks`), the `ptr_word`/`word_ref`/`word_ptr` bridges, and the pure-Teko arena on
raw `mmap`/`munmap`/`exit_group` (`src/runtime/arena.tks`). The arena proves the whole thesis: a load-bearing
runtime subsystem, authored in a restricted Teko dialect over `teko::sys::syscallN`, C-leg-first, native leg
honest-stopped until Doc-2's terminal native phase. Threads follow the same road.

---

## §0 — What is being retired, and the substrate that replaces it

| pthread usage today (`teko_rt.c`) | raw OS facility that replaces it |
|---|---|
| `pthread_create` / `pthread_detach` (spawn) | Linux `clone(2)`; macOS `bsdthread_create` (libSystem); Windows `CreateThread`/`NtCreateThreadEx` |
| `pthread_join` (join twin, await batch) | Linux `CLONE_CHILD_CLEARTID` tid word + `futex(FUTEX_WAIT)`; macOS/Windows join via bound lib call |
| `pthread_mutex_t` (memchan, waitgroup) | a 1-word lock over `futex` (Linux) / `os_sync_wait_on_address` (macOS) / `WaitOnAddress` (Windows) |
| `pthread_cond_t` (memchan not_empty/not_full, waitgroup zero) | a sequence-word condvar over the SAME wait/wake primitive |
| `-pthread` link flag (`src/build/project.tks:1213` `append_pthread_flag`) | REMOVED — the retirement's final proof |

The substrate is the same shape the arena uses: raw integer/address arithmetic over a small word block in
program-region memory (already Teko-arena), state read/written with `teko::mem::load_u64`/`store_u64`, and OS
calls through `teko::sys::syscallN` (Linux) or `extern fn … from "<lib>"` (macOS/Windows). ONE new codegen
intrinsic is required — spawn on Linux `clone`, whose child-branch is not expressible as a plain `syscallN`
(§3). Everything else is leaf `.tks` over machinery that exists on HEAD.

---

## §1 — The per-platform primitive matrix (spawn + sync)

| Target | SPAWN | JOIN | SYNC wait/wake (mutex + condvar) |
|---|---|---|---|
| **Linux x86_64 / aarch64** | `clone` syscall (56 / 220) via the NEW `thread_clone` codegen intrinsic (§3) — child-branch asm, new stack, child entry trampoline, thread exit via `SYS_exit` (60 / 93, NOT `exit_group`) | `CLONE_CHILD_CLEARTID` on a ctid word + `futex(ctid, FUTEX_WAIT)` until the kernel clears it on thread exit | `futex` syscall (202 / 98), `FUTEX_WAIT_PRIVATE`/`FUTEX_WAKE_PRIVATE` over the `syscall*` intrinsic |
| **macOS arm64** (design-ahead) | `__bsdthread_create` (+ one-time `bsdthread_register`) bound via `extern fn … from "System"` — Apple gives no stable syscall ABI, so libSystem is the raw facility (§9.1) | thread-exit join word via `__ulock_wait`, or bound `pthread_join`-free scheme (§9.1) | `os_sync_wait_on_address` / `os_sync_wake_by_address_any` (macOS 14.4+, PUBLIC) — fallback `__ulock_wait`/`__ulock_wake` — via `extern fn … from "System"` |
| **Windows x86_64 / arm64** (design-ahead) | `CreateThread` (`extern fn … from "kernel32"`), or `NtCreateThreadEx` (`… from "ntdll"`) for the no-CRT path (§9.2) | `WaitForSingleObject` on the thread handle, or the ctid-word scheme via `WaitOnAddress` | `WaitOnAddress` / `WakeByAddressSingle` (`… from "kernel32"`) — fallback `SRWLOCK` + `CONDITION_VARIABLE` |

Rationale for the split: Linux exposes a stable raw SYSCALL ABI, so it rides the existing `syscallN` intrinsic
(spawn excepted — §3). macOS and Windows do NOT publish a stable syscall ABI, so their "raw facility" is the
always-present OS library (libSystem is unavoidable on macOS; kernel32/ntdll on Windows) reached by the C1
`extern fn … from "<lib>"` machinery — the exact posture recorded in the syscall-intrinsic doc §9. This is
NOT the pthread dependency returning: pthread is a SEPARATE user library the native backend refuses to link;
libSystem/kernel32 are the OS itself, the floor every process already stands on.

---

## §2 — `teko::sys` numbers + flags to add (leaf, no reseed)

Extend `src/sys/sys.tks` with `#os`/`#arch`-guarded consts, transcribed from the kernel ABI (never a C
header), same self-contained-per-target discipline the existing `SYS_MMAP`/`CLOCK_*` blocks use. Full Javadoc
on each, modelled on the `SYS_EXIT_GROUP` doc already in the file.

```
/**
 * SYS_CLONE — the Linux `clone(flags, stack, ptid, ctid, tls)` syscall number, the raw thread-creation
 * facility the §10 native substrate spawns on (replacing pthread_create). Consumed ONLY by the
 * `thread_clone` codegen intrinsic (§3), never a plain `syscallN` — clone's child returns on a NEW stack
 * and cannot flow back through the generic inline helper. x86_64 value: 56. aarch64 value: 220.
 * WARNING: the aarch64 argument order swaps `ctid` and `tls` relative to x86_64 (§3.4).
 * @since §10-native
 */
#os("linux") #arch("x86_64") pub const SYS_CLONE: i64 = 56
#os("linux") #arch("arm64")  pub const SYS_CLONE: i64 = 220

/** SYS_FUTEX — Linux `futex(uaddr, op, val, timeout, uaddr2, val3)`, the wait/wake primitive under every
 *  §10 lock and condvar. x86_64: 202. aarch64: 98. @since §10-native */
#os("linux") #arch("x86_64") pub const SYS_FUTEX: i64 = 202
#os("linux") #arch("arm64")  pub const SYS_FUTEX: i64 = 98

/** SYS_GETTID — Linux `gettid()`, the kernel thread id (used to key the per-thread state table, §5).
 *  x86_64: 186. aarch64: 178. @since §10-native */
#os("linux") #arch("x86_64") pub const SYS_GETTID: i64 = 186
#os("linux") #arch("arm64")  pub const SYS_GETTID: i64 = 178

/** SYS_EXIT — Linux `exit(status)`, terminating THIS THREAD only (NOT the process — that is exit_group).
 *  The last instruction a spawned thread's trampoline runs. x86_64: 60. aarch64: 93. @since §10-native */
#os("linux") #arch("x86_64") pub const SYS_EXIT: i64 = 60
#os("linux") #arch("arm64")  pub const SYS_EXIT: i64 = 93

/** CLONE_VM (0x100), CLONE_FS (0x200), CLONE_FILES (0x400), CLONE_SIGHAND (0x800), CLONE_THREAD (0x10000),
 *  CLONE_SYSVSEM (0x40000), CLONE_SETTLS (0x80000), CLONE_PARENT_SETTID (0x100000),
 *  CLONE_CHILD_CLEARTID (0x200000) — the thread-flavour clone flag word. Arch-invariant on Linux, so ONE
 *  `#os("linux")` block each, no `#arch` split (like the PROT_*/MAP_* family). @since §10-native */
#os("linux") pub const CLONE_VM: i64 = 0x100
#os("linux") pub const CLONE_FS: i64 = 0x200
#os("linux") pub const CLONE_FILES: i64 = 0x400
#os("linux") pub const CLONE_SIGHAND: i64 = 0x800
#os("linux") pub const CLONE_THREAD: i64 = 0x10000
#os("linux") pub const CLONE_SYSVSEM: i64 = 0x40000
#os("linux") pub const CLONE_SETTLS: i64 = 0x80000
#os("linux") pub const CLONE_PARENT_SETTID: i64 = 0x100000
#os("linux") pub const CLONE_CHILD_CLEARTID: i64 = 0x200000

/** FUTEX_WAIT (0), FUTEX_WAKE (1), FUTEX_PRIVATE_FLAG (128). The §10 locks are process-private, so they
 *  use the _PRIVATE variants (WAIT|128 = 128, WAKE|128 = 129) — cheaper (no shared-hash path). Arch-
 *  invariant. @since §10-native */
#os("linux") pub const FUTEX_WAIT: i64 = 0
#os("linux") pub const FUTEX_WAKE: i64 = 1
#os("linux") pub const FUTEX_PRIVATE_FLAG: i64 = 128
```

macOS/Windows design-ahead consts (`__ULOCK_OP_COMPARE_AND_WAIT`, `os_sync` flag words; `INFINITE`,
`CREATE_SUSPENDED`) are catalogued as their own `#os("macos")`/`#os("windows")` blocks when those legs land.
`SYS_MMAP`/`SYS_MUNMAP` (thread stacks) and `PROT_*`/`MAP_*` are already in `sys.tks`.

Reseed: `teko::sys` is a stdlib leaf the compiler does not consume → **no reseed** (owner's "leaf modules
don't require a reseed"). Gated only on the `#os #arch` prune already in the seed.

---

## §3 — The `thread_clone` codegen intrinsic (the ONE compiler-touching keystone)

### §3.1 Why spawn needs a NEW intrinsic and is NOT a `syscallN` call

`clone` is the one syscall that cannot ride the generic `tk_syscallN` inline helper. Unlike every other
syscall, `clone`'s child returns from the `syscall` instruction on a DIFFERENT (freshly supplied) stack, with
`rax == 0`. If issued through `tk_syscall5(...)`, the child would try to `ret` out of the `static inline`
helper's frame — but its `rsp` now points at the new thread stack, which holds no such return address.
Undefined execution. The child branch MUST be handled in the same asm block that issues `clone`: test the
return, and on the child side JUMP to the entry function (never `ret`). This is exactly why musl/glibc/Go/Zig
all hand-write `__clone` in assembly. So spawn is a bespoke codegen intrinsic that emits that asm helper —
the direct analogue of the `syscallN` helpers, one notch higher because of the child branch.

### §3.2 The Teko surface

A compiler builtin (like `syscallN`/`ptr_word`/`word_ptr` — NOT declared in any `.tks`):

```
teko::sys::thread_clone(entry: i64, stack_top: i64, arg: i64, ctid_addr: i64, flags: i64): i64
```

- `entry`  — the child entry function address (a C-ABI `void(*)(void*)` symbol; the spawn SPECIAL emitter
  supplies the per-site trampoline symbol directly — §4 — so no first-class function-pointer VALUE is
  needed, sidestepping the "address of a Teko fn" gap).
- `stack_top` — highest address of a caller-mapped, 16-aligned thread stack (mmap'd by the spawn wrapper).
- `arg` — the ctx-blob pointer word passed to `entry` in the child.
- `ctid_addr` — the `CLONE_CHILD_CLEARTID` word address (the join word, §6); 0 for a fire-and-forget spawn.
- `flags` — the clone flag word (§2). The parent's return is the child's tid (or `-errno`).

Return convention is the raw-syscall one (`< 0` = `-errno`), consistent with `syscallN`.

### §3.3 The C-leg preamble helper (x86_64 Linux — the build target)

Emitted ONCE into the preamble, use-gated exactly like the `tk_syscallN` helpers (§3.5), next to them.
Conceptual shape (implementer transcribes the exact asm; this is the classic musl `__clone` child-branch):

```c
/* --- teko §10 raw thread-spawn (x86_64 Linux); emitted only if the program spawns --- */
static long tk_thread_clone(void (*entry)(void*), void *stack_top, void *arg,
                            int *ctid, long flags) {
    /* pre-store entry+arg at the top of the child stack so the child can retrieve them
       AFTER clone (registers are not guaranteed across the child's first instruction). */
    void **sp = (void **)stack_top;
    *(--sp) = arg;        /* [sp+0] on child */
    *(--sp) = (void*)entry;
    long ret;
    __asm__ volatile (
        "syscall\n\t"                 /* clone(flags, sp, 0, ctid, 0) — SYS_clone in rax */
        "test %%rax, %%rax\n\t"
        "jnz  1f\n\t"                 /* parent: rax = child tid, fall through to return */
        /* ---- child: on the new stack ---- */
        "pop  %%rax\n\t"              /* entry */
        "pop  %%rdi\n\t"              /* arg   */
        "call *%%rax\n\t"             /* entry(arg) — the §4 trampoline; runs task_begin/target/task_end */
        "mov  $60, %%rax\n\t"         /* SYS_exit (thread-only, NOT exit_group) */
        "xor  %%rdi, %%rdi\n\t"
        "syscall\n\t"
        "1:\n\t"
        : "=a"(ret)
        : "a"(56 /*SYS_clone*/), "D"(flags), "S"(sp), "d"(0), "r"(/*r10=*/ctid), "r"(/*r8=*/0)
        : "rcx", "r11", "memory", "cc");
    return ret;
}
```

The `r10`/`r8` operands use register-asm locals (as the `tk_syscall4/5/6` helpers already do). The clone raw
arg order is `(flags=rdi, stack=rsi, ptid=rdx, ctid=r10, tls=r8)`; here `ptid=0`, `tls=0` (TLS deferred to
the §5 seam decision), `ctid` carries `CLONE_CHILD_CLEARTID`'s join word. `SYS_exit` (60), never `exit_group`
— the process must survive one thread's end.

### §3.4 aarch64-Linux — DESIGN-AHEAD (honest-stop on the build host)

Same helper under `#arch("arm64")`: `clone` = 220, `svc #0`, args `x0-x4`, ret `x0`, child branch on
`cbnz x0, parent`. **The aarch64 clone argument order swaps `ctid` and `tls`**: `(flags, stack, ptid, tls,
ctid)` — `ctid` is `x4`, not `x3`. The emitter HONEST-STOPS on the aarch64 arm ("§10 aarch64 thread_clone
not yet validated on this host") until an aarch64 build host proves it — the exact discipline the syscall
intrinsic used for its aarch64 helper.

### §3.5 Emit gating + dispatch + native leg (mirror the syscall intrinsic exactly)

- **Use-gate.** Extend `cg_scan_syscall_arities` (or a sibling `cg_scan_spawns` — the `spawn_sites` list
  `_lr.spawns` already computed at `codegen.tks:13909` is the ready-made signal): emit `tk_thread_clone`
  ONLY when `spawn_sites.len > 0`. The compiler's own corpus never spawns (§10 dogfood law — the parallel
  codegen uses a private `fork_join`, NOT the `spawn` surface), so the self-image emits ZERO thread helpers
  ⇒ **clean fixpoint, single reseed, inert in corpus** — the identical property the syscall intrinsic and the
  `extern type = struct` keystone both got. `tc1 == tc2 == tc3`, no ladder.
- **Placement.** Emit the helper next to the `tk_syscallN` helpers, after the `#pragma` block
  (`codegen.tks:13927-13928` already calls `cg_emit_syscall_helpers`); the `#include <string.h>` for the
  spawn packers is already conditional on `spawn_sites.len > 0` (`codegen.tks:13916`).
- **Checker.** Register `thread_clone` in `scope.tks::builtin_fn` next to `syscall*`/`word_ptr`
  (`scope.tks:1153-1157`) as `Func { params = <[i64,i64,i64,i64,i64]>; ret = <i64> }`. No qualifier change
  (`builtin_qualifier_ok` already admits the `teko::sys::` spelling).
- **Native leg.** HONEST-STOP now — `thread_clone` is not in the native subset, so it falls into
  `lower.tks:1853`'s terminal `_ => "… not yet lowered (N2)"`, exactly as `syscallN`/`ptr_word`/`word_ptr` do.
  The native lowering emits the `clone`/`svc` + child-branch directly (no C helper indirection) in Doc-2's
  terminal native phase. **The C leg is this pass's build+run target.**

This is the SOLE compiler-touching crumb in the whole pass (M1). Everything else is leaf `.tks`.

---

## §4 — The entry trampoline, the arena-per-thread bracket, and `tk_g_current_task`

Today `tk_thread_start` (C, `teko_rt.c:2583`) is the pthread start-routine that (a) copies+frees the call
record, (b) `tk_task_begin()` — installs a FRESH per-thread arena, (c) `entry(blob)` — the codegen
trampoline that unpacks args and runs the target, (d) `tk_task_end()` — frees the whole thread arena, (e)
returns (pthread reaps). The raw transition preserves every step; only WHO calls them moves.

**The bracket moves INTO the emitted per-site trampoline.** `emit_spawn`/`emit_spawn_thunks`
(`codegen.tks:10678-10710`, `:10621-10632`) already emit a per-site `tk_spawn_tramp_k(void *_raw)` that
unpacks the ctx blob and calls the target. The rewrite:

1. `emit_spawn`'s tail changes from `tk_thread_spawn(tk_spawn_tramp_k, _sc)` to: map a thread stack
   (`teko::runtime::thread_stack_new()` — a leaf Teko fn over `SYS_mmap`, guard page + fixed size), then
   `teko::sys::thread_clone(&tk_spawn_tramp_k, stack_top, _sc, ctid, FLAGS)`. The `&tk_spawn_tramp_k` is
   emitted as the bare C symbol by the special emitter (it KNOWS the site's trampoline name) — no
   function-pointer value crosses Teko.
2. `tk_spawn_tramp_k` gains the bracket around its existing body:
   `task_begin(); <unpack + target call>; task_end(); free(_raw); /* return → SYS_exit via §3.3 asm */`.
   `task_begin`/`task_end` become Teko fns (`teko::runtime::task_begin/end`) that install/tear-down a fresh
   arena CONTROL block for THIS thread through the P2 seam — the arena core already keys everything off the
   per-thread `tk_arena_control_get/set` accessor (`arena.tks:9-18`), so a thread that begins with control==0
   maps its own control block on first `tk_region_alloc`, and `task_end` `munmap`s that thread's arena and
   resets the seam word. The child's `tk_arena_pop`/`task_end` can NEVER reach another thread's allocations —
   the copy-into-blob-before-handoff isolation (already in the packers, `emit_spawn_pack_field`) is unchanged.
3. **`tk_g_current_task` / arena control per-thread survival** — THE crux, §5.

The join word (`ctid`) and the stack base are recorded in the spawn HANDLE (§6) for the joinable twin; the
detached path passes `ctid=0` and accepts a bounded stack leak (§8 R3).

---

## §5 — The `_Thread_local` seam under raw `clone` (the headline design decision)

`tk_g_current_task` (`teko_rt.c:1389`) and `tk_g_arena_control` (`teko_rt.c:2320`) are `_Thread_local`. On
glibc, `_Thread_local` compiles to a `%fs`-relative (x86_64) / `tpidr_el0` (aarch64) load against a TLS block
that **glibc/pthread sets up at thread creation**. A raw `clone` thread has NO such block unless we set it up:
`_Thread_local` on a raw-clone thread reads an unrelated `%fs.base` and CORRUPTS or FAULTS. This is the single
thing that must be resolved before implementation, because it decides the shape of `task_begin`/`task_end` and
the arena control seam.

**OWNER RULING (ratified 2026-08-17) — DECIDED = B.** The per-thread-state seam question (R1 /
§5, the glibc-TLS-unavailable-on-raw-clone fork) is RESOLVED: the owner ratified **Option B — the
`SYS_gettid`-keyed per-thread state table in program-region memory** (libc-independent,
native-leg-identical). This is no longer an open A/B/A-then-B fork: the seam migrates to the tid-keyed
table. Option A (`CLONE_SETTLS` + hand-laid glibc TCB) is REJECTED as both primary and transitional —
the implementation goes to B directly. The two options below are retained only as the record of what was
weighed; **B is the ruling.**

Two law-first options (both remove the pthread/glibc-TLS dependency; owner ruled **B**):

- **Option A — hand-lay a TLS block + `CLONE_SETTLS`.** The spawn wrapper allocates a per-thread TCB, points
  `%fs.base`/`tpidr_el0` at it via `CLONE_SETTLS` (tls arg in §3.3), and lays out the ABI-required TCB
  self-pointer so `_Thread_local` keeps working unchanged. REJECTED as the primary: the TCB layout is
  glibc-internal and version-coupled (variant-II on x86_64, the dtv, the stack-guard slot) — exactly the
  library coupling §16 is removing. It re-imports glibc's ABI through the back door.

- **Option B (RECOMMENDED) — replace the `_Thread_local` seam with a raw per-thread table keyed by kernel
  tid.** The arena control seam is ALREADY "one function, two incarnations" (`arena-mmap.md` §0, the F1 seam
  doc). Swap the incarnation: `tk_arena_control_get/set` (and the task-current seam) look up a small
  open-addressed table `tid → { task, control }`, `tid = SYS_gettid` (cheap, cached in a register-free leaf).
  The table lives in program-region memory (one mmap, grows by doubling under its OWN futex lock — §7). This
  is fully libc-independent, works BYTE-IDENTICALLY in the native leg (just `gettid` + `load_u64`/`store_u64`
  + the table), and needs no glibc ABI knowledge. Cost: one hashed table probe per seam read vs one `%fs`
  load — negligible (the seam is not on the allocator's hot inner loop; the arena caches `control` per call).

**Posture under the ruling (DECIDED = B).** With B ratified, the seam is the `SYS_gettid`-keyed per-thread
table from the start: `tk_arena_control_get/set` (and the task-current seam) resolve `tid → { task, control }`
through the program-region table, `tid = SYS_gettid`. No `CLONE_SETTLS`, no borrowed glibc TCB, no
`_Thread_local` on a spawned thread — the table is the only per-thread-state mechanism on both legs, so the C
leg and the native leg are byte-identical here. The A-transitional path is NOT taken. This was the one genuine
fork; it is now closed.

---

## §6 — Join, and the futex mutex + condvar (leaf `.tks`)

### §6.1 Join (Linux)

Spawn with `CLONE_CHILD_CLEARTID` and a ctid word `w` (an `i32` in a heap/arena handle). When the thread
exits, the kernel zeroes `w` and does a `FUTEX_WAKE` on it. Join =

```
fn thread_join(w_addr: u64) {
    loop {
        var tid = teko::mem::load_u64(w_addr)      // read the ctid word (low 32 bits)
        if (tid & 0xffffffff) == 0 { break }       // kernel cleared it → thread gone
        _ = teko::sys::syscall6(teko::sys::SYS_FUTEX, w_addr to i64,
              teko::sys::FUTEX_WAIT | teko::sys::FUTEX_PRIVATE_FLAG, tid to i64, 0, 0, 0)
    }
}
```

After join the stack mapping is `munmap`'d (the joiner owns it now). This is the deterministic finish edge the
`tk_thread_spawn_selftest`/`tk_memchan_selftest`/`tk_waitgroup_selftest` anchors depend on.

### §6.2 The futex mutex (3-state, Drepper) and condvar — the pthread_mutex/cond replacement

A mutex is ONE `i32` word (0 = free, 1 = locked-no-waiters, 2 = locked-maybe-waiters) in the channel/waitgroup
struct (already program-region memory). Lock: CAS 0→1; on contention set to 2 and `FUTEX_WAIT` while != 0.
Unlock: if the word was 2, store 0 and `FUTEX_WAKE(1)`. A condvar is a monotonically-incrementing sequence
word: `wait(cv, mtx)` snapshots the seq, unlocks `mtx`, `FUTEX_WAIT(cv, seq)`, relocks `mtx`; `signal`
increments seq and `FUTEX_WAKE(1)`; `broadcast` increments seq and `FUTEX_WAKE(INT_MAX)`. All pure Teko over
`teko::sys::syscall*` + `load_u64`/`store_u64` + the atomic CAS (see §8 R5 for the atomicity requirement).
Type shapes (leaf `teko::runtime::sync` module):

```
/** A one-word futex mutex. `state`: 0 free / 1 locked / 2 locked-with-waiters. @since §10-native */
fn mtx_lock(state_addr: u64)                              // CAS + FUTEX_WAIT loop
fn mtx_unlock(state_addr: u64)                            // store 0 + conditional FUTEX_WAKE(1)
/** A one-word sequence condvar paired with a futex mutex. @since §10-native */
fn cv_wait(seq_addr: u64, mtx_state_addr: u64)            // snapshot seq, drop mtx, FUTEX_WAIT, relock
fn cv_signal(seq_addr: u64)                               // seq++ ; FUTEX_WAKE(1)
fn cv_broadcast(seq_addr: u64)                            // seq++ ; FUTEX_WAKE(INT_MAX)
```

### §6.3 Channels + waitgroups rewritten over §6.2

`tk_memchan` (ring + `not_empty`/`not_full` conds), `tk_waitgroup` (counter + `zero` cond) rewrite one-to-one:
the `pthread_mutex_t`→`mtx_*`, each `pthread_cond_t`→a seq word + `cv_*`. The ring buffer, close flag, counter
all stay as words in the program-region struct. `tk_oschan` (AF_UNIX DGRAM) is ALREADY syscall-shaped (the
`chan_dgram` probe) and needs only its `socket`/`bind`/`sendto`/`recvfrom` moved to `teko::sys::syscall*`
(no pthread in it at all) — a separate, independent leaf.

---

## §7 — Ordered crumb sequence

`[C]` = compiler-touching → fixpoint + reseed · `[L]` = leaf `.tks` · `[Del]` = two-legs C-symbol deletion.
C-leg-first throughout; the native leg inherits each intrinsic in Doc-2's terminal phase.

1. **M0 `teko::sys` numbers + flags** — `[L]` — SYS_CLONE/FUTEX/GETTID/EXIT + CLONE_*/FUTEX_* (§2). No
   reseed. FIRST implementable crumb — fully unblocked, zero deps, unblocks everything below.
2. **M1 `thread_clone` codegen intrinsic** — `[C]` — the x86_64 clone child-branch helper, use-gated on
   `spawn_sites`, checker registration, aarch64 honest-stop, native honest-stop (§3). Clean fixpoint (inert
   in corpus). **The keystone reseed.** Fixture `thread_clone_smoke` (§ below).
3. **M2 futex sync module `teko::runtime::sync`** — `[L]` — `mtx_*`/`cv_*` over `SYS_FUTEX` (§6.2). No
   reseed (runtime leaf the compiler does not consume). Fixture `futex_mutex_counter`.
4. **M3 spawn wrapper + trampoline bracket** — `[C]` (touches `emit_spawn`/`emit_spawn_thunks`) — rewire the
   spawn tail from `tk_thread_spawn` to `thread_stack_new` + `thread_clone`; move `task_begin`/`task_end` into
   the emitted trampoline; wire the ctid join word (§4, §6.1). Reseed (still inert in corpus — the compiler
   does not spawn). Fixtures `thread_spawn_raw`, `thread_join_raw`. **May batch with M1** to avoid a double
   reseed (both are spawn-codegen, both inert-in-corpus) — RECOMMEND batching M1+M3 into one reseed.
5. **M4 per-thread seam → tid-table (Option B, §5)** — `[C]` if it re-shapes `tk_arena_control_get/set` in
   `teko_rt.c` + the seam accessor, `[L]` for the Teko table. The §5 owner ruling is DECIDED = B, so this is
   the tid-keyed table directly (no A-transitional TLS). Sequenced with M1/M3 so spawned threads have the
   table seam from first spawn — there is no interim glibc-TLS coupling to remove.
   Fixture `thread_local_state_isolation` (two threads, disjoint arena control, no cross-hit).
6. **M5 channels + waitgroups over §6.2** — `[L]` — rewrite `tk_memchan`/`tk_waitgroup` bodies in
   `teko::threads` (`.tks`) over `mtx_*`/`cv_*`; `tk_oschan` sockets over `syscall*`. No reseed. Fixtures
   `memchan_fifo`, `waitgroup_barrier`, `oschan_dgram`.
7. **M6 macOS spawn+sync (libSystem FFI)** — `[L]` design-ahead (§9.1). Honest-stop until a macOS host.
8. **M7 Windows spawn+sync (kernel32 FFI)** — `[L]` design-ahead (§9.2). Honest-stop until a Windows host.
9. **M8 C-symbol DELETION sweep + `-pthread` removal** — `[Del]` — remove `tk_thread_spawn`/
   `tk_thread_join_spawn`/`tk_thread_join`, `tk_memchan_*`, `tk_waitgroup_*`, `tk_oschan_*` from
   `teko_rt.c/.h`, and delete `append_pthread_flag` + `PTHREAD_FLAG` from `project.tks` (§0). Each only after
   ALL callers (compiler + corpus) are migrated. Full two-legs gate per deletion. **The retirement's proof:
   a spawn/channel/waitgroup fixture that builds and runs with NO `-pthread` and no pthread symbol.**

FIRST implementable crumb: **M0** (leaf, unblocked). First LOAD-BEARING crumb: **M1(+M3 batched)**.

---

## §8 — Regression fixtures (inputs → expected native exit codes)

All compile `--no-verify --release`, `TEKO_BACKEND=c`, under the memory `ulimit`; the harness reads `$?`.
NEVER `teko test .`. Behavioral anchors: `tk_thread_spawn_selftest` (balance to baseline),
`tk_waitgroup_selftest` (barrier), `tk_memchan_selftest` (FIFO sum).

| # | fixture (`examples/regressions/`) | body | exit |
|---|---|---|---|
| T1 | `thread_clone_smoke` | `thread_clone` a trampoline that writes 42 to a shared word, join on ctid, read it | `42` |
| T2 | `thread_spawn_raw` | spawn N=100 JOINABLE threads, each `task_begin`/32 allocs/`task_end` under `TEKO_MEM_PARANOID=1`; join all; assert live-count == baseline; exit 0 else 1 (mirrors `tk_thread_spawn_selftest`) | `0` |
| T3 | `futex_mutex_counter` | M=8 threads each `mtx_lock`/`counter++`/`mtx_unlock` K=10000 times; join; exit 0 iff counter == M*K | `0` |
| T4 | `waitgroup_barrier` | `wg.add(N)`; N workers each `wg.done()`; `wg.wait()` returns only after all; exit 0 (mirrors `tk_waitgroup_selftest`) | `0` |
| T5 | `memchan_fifo` | producer sends 0..n-1 (bounded-16, exercises blocking send); consumer sums; exit 0 iff sum == n(n-1)/2 and count == n (mirrors `tk_memchan_selftest`) | `0` |
| T6 | `thread_local_state_isolation` | two threads each map their own arena control, cross-check disjoint; a child `arena_pop` cannot reach the other's block; exit 0/1 | `0` |
| T7 | `pthread_free_link` | build+run a spawn+channel program with `-pthread` REMOVED and no pthread symbol referenced; exit 0 — the retirement proof (M8) | `0` |

T2/T4/T5 reproduce the three C selftests' observables on the raw substrate. T1 is the intrinsic keystone
proof. T7 is the "dependency is actually gone" gate.

---

## §9 — Design-ahead: macOS + Windows (out of the C-leg build target, contracts drafted)

### §9.1 macOS arm64 (libSystem FFI — the `extern fn … from "System"` path)

Apple ships NO stable syscall ABI; libSystem is the mandatory floor. So macOS spawn/sync are C1
`extern fn … from "System"` bindings, NOT a raw-syscall intrinsic — the exact posture the syscall-intrinsic
doc §9 recorded. Contracts against the DECLARED shapes:

- **Spawn:** `__bsdthread_create(func, arg, stack, pthread_obj, flags)` — requires a one-time
  `bsdthread_register(...)` at process start. Both are libSystem symbols. Because the raw `bsdthread_*` ABI is
  itself semi-private, the pragmatic fallback is `os_workgroup`-free `pthread_create_from_mach_thread` OR,
  simplest and stable, bind `pthread_create` **from libSystem directly** (libSystem is NOT the pthread
  library the native backend refuses to link — it is the one library macOS cannot run without). Recommend the
  latter for v1 macOS; note `bsdthread_create` as the pure-raw refinement.
- **Sync:** `os_sync_wait_on_address(addr, value, size, flags)` / `os_sync_wake_by_address_any(...)` (macOS
  14.4+, PUBLIC, futex-equivalent) via `extern fn … from "System"`; fallback `__ulock_wait`/`__ulock_wake`.
  The §6.2 `mtx_*`/`cv_*` word algorithms are IDENTICAL — only the wait/wake leaf swaps.

### §9.2 Windows x86_64/arm64 (kernel32/ntdll FFI)

- **Spawn:** `CreateThread(NULL, stack, start, arg, flags, &tid)` via `extern fn … from "kernel32"` — start
  routine is `DWORD(*)(LPVOID)`; the §4 trampoline shape carries over. `NtCreateThreadEx` (`… from "ntdll"`)
  is the no-CRT variant if kernel32 must be avoided.
- **Join:** `WaitForSingleObject(handle, INFINITE)` + `CloseHandle`.
- **Sync:** `WaitOnAddress(addr, compare, size, ms)` / `WakeByAddressSingle(addr)` (`… from "kernel32"`) —
  again the §6.2 algorithms are unchanged; fallback `SRWLOCK` + `CONDITION_VARIABLE`. `win32_compat.h`
  (a §16 retirement target) is NOT reused.

The `mtx_*`/`cv_*`/channel/waitgroup Teko algorithms are written ONCE (§6) and are platform-agnostic above a
3-symbol wait/wake/spawn leaf — the design's leverage: only the leaf is per-OS.

---

## §10 — Risks + law tensions (one genuine fork → HALT for owner)

- **R1 — RESOLVED (owner ruling 2026-08-17, DECIDED = B): the `_Thread_local` seam has no backing on a raw
  `clone` thread.** glibc TLS (`%fs`/`tpidr`) is not set up by raw `clone`, so `tk_g_current_task` / arena
  control would corrupt or fault on a spawned thread (§5). The owner ratified **Option B — replace the seam
  with a `SYS_gettid`-keyed per-thread table in program-region memory** (fully libc-independent,
  native-leg-identical). Option A (hand-laid TCB + `CLONE_SETTLS`, re-imports glibc ABI) is REJECTED, and the
  A-transitional path is NOT taken. This was the single ruling needed before implementation; it is now made,
  and it shapes `task_begin`/`task_end` and the arena seam (§5). No open fork remains.
- **R2 — `clone` child branch cannot be a plain `syscallN`.** Resolved: the `thread_clone` codegen intrinsic
  emits the bespoke child-branch asm (§3), the direct analogue of the `syscallN` helpers. Rides the
  `f64_bits`/`syscallN` codegen-intrinsic precedent; no new law. `"memory"`+`"cc"` clobbers mandatory.
- **R3 — detached-thread stack lifetime.** A detached thread cannot `munmap` its own running stack. v1: the
  joinable twin (which the selftests use) frees the stack after join; the detached path accepts a bounded
  stack leak (O(live detached threads)) OR uses the `munmap`-then-`SYS_exit` tail-asm (`__unmap_self`) as a
  refinement. Not a blocker; noted.
- **R4 — aarch64 clone `ctid`/`tls` arg-order swap** (§3.4) and per-arch syscall numbers. Mitigated by the
  honest-stop on the aarch64 arm until a validating host, and by the `#arch`-guarded numbers (§2).
- **R5 — atomicity of the futex word CAS.** `mtx_lock`'s 0→1 transition needs a real atomic
  compare-and-swap, not `load_u64`+`store_u64` (which race). The runtime needs an atomic-CAS primitive: reuse
  the C11 `__atomic_*` seam the current channels already rely on, exposed as a `teko::sys` intrinsic
  (`atomic_cas_u32(addr, expected, desired): i32`) lowered to `__atomic_compare_exchange` in the C leg / `LL`
  /`SC` (aarch64) or `lock cmpxchg` (x86_64) in the native leg. This is a SECOND small intrinsic the sync
  module needs — flag it as a co-requisite of M2 (a leaf-sized codegen add, not a redesign). Without it the
  mutex is unsound. RECOMMEND landing `atomic_cas_u32` (+ `atomic_add`/`atomic_store` as needed) alongside M2.
- **R6 — double-reseed churn.** M1 and M3 are both spawn-codegen and both inert-in-corpus → batch into one
  reseed (§7 note). M4 (seam) is a later, separate reseed.

No tension remains unresolved. R1 was a genuine architecture fork (not a law violation); the owner ruled it
**DECIDED = B** (2026-08-17, §5) — the `SYS_gettid`-keyed per-thread table in program-region memory. Every
crumb up to and including the M0 numbers, the M1 intrinsic, and the M4 seam is now design-complete and
law-clean; the implementer lands M0 immediately and builds M1/M3/M4 against the tid-keyed table seam, with no
pending ruling.

---

## Section index

- §0 What is retired + the substrate
- §1 Per-platform primitive matrix (spawn + sync)
- §2 `teko::sys` numbers + flags (leaf)
- §3 `thread_clone` codegen intrinsic (the one keystone; C-leg helper, gating, dispatch, native honest-stop)
- §4 Entry trampoline + arena-per-thread bracket + `tk_g_current_task`
- §5 The `_Thread_local` seam under raw clone (the headline decision)
- §6 Join + futex mutex/condvar + channels/waitgroups (leaf)
- §7 Ordered crumb sequence (M0..M8)
- §8 Regression fixtures (T1..T7)
- §9 Design-ahead macOS + Windows
- §10 Risks + law tensions (R1 HALT)
