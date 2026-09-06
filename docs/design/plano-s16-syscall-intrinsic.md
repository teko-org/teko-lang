# Plano §16 — SYSCALL INTRINSIC keystone (Linux raw-syscall via codegen intrinsic)

Status: DESIGN (architect). Base: `origin/fix/retirement` HEAD `c366d8be`.
Supersedes the libc-FFI keystone of §16 per the owner ruling **"VIRADA §16 — libc está FORA"**
(`docs/design/mudancas-superficie-0.3.1.md` §11.2, 2026-08-16). This document designs the ONE
keystone the whole per-OS surface rides on: the **Linux raw-syscall codegen intrinsic**, mirroring
the `f64_bits` "intrínseco de codegen" precedent. macOS (libSystem) and Windows (kernel32/ntdll)
native-lib binding are DESIGN-AHEAD notes only (§9); env-over-`environ` pure-Teko is out of scope.

This crumb is READ-AND-DESIGN. No product code is written here.

---

## §1 — The Teko surface

### §1.1 The fixed-arity family

Recommendation: a fixed-arity family, exactly the shape musl/Go/Zig expose, injected as compiler
builtins (like `f64_bits`, NOT declared in any `.tks`):

```
teko::sys::syscall0(nr: i64)                                           : i64
teko::sys::syscall1(nr: i64, a0: i64)                                  : i64
teko::sys::syscall2(nr: i64, a0: i64, a1: i64)                         : i64
teko::sys::syscall3(nr: i64, a0: i64, a1: i64, a2: i64)                : i64
teko::sys::syscall4(nr: i64, a0: i64, a1: i64, a2: i64, a3: i64)       : i64
teko::sys::syscall5(nr: i64, a0..a4: i64)                              : i64
teko::sys::syscall6(nr: i64, a0..a5: i64)                              : i64
```

- **All args are `i64`.** Not `uptr`: `uptr` is the OPAQUE transport type (`ptr_opaque_error`) and
  rejects both arithmetic AND `to`-casts (`src/checker/typer.tks` `cast_check` → `cast_kind` returns
  `ptr_opaque_error` for `Ptr`/`Uptr`), so a `uptr`-typed param could not accept a literal `nr`, an
  `fd`, a `len` or a flag word. `i64` is the machine word every argument register is, and the register
  is agnostic to the datum's Teko type. This keeps the checker signature TRIVIAL (fixed `i64` params,
  no generics, no ref-plumbing).
- **Return is a raw `i64`.** A Linux syscall returns the result in `rax`, and on error returns the
  NEGATED errno in `[-4095, -1]`. We surface that raw: the caller checks `r < 0` and reads `-r` as the
  errno. NO `i64 | error` lift at the intrinsic — the intrinsic is the thinnest possible primitive;
  the ergonomic `Result`-shaped wrappers (`teko::io::write`, `teko::time::now`, …) are ordinary Teko
  `.tks` fns built ON TOP (§7), and THEY do the `< 0 → error` translation with a per-call errno name.
  This is the Go/`unix.Syscall` model (returns `(r1, r2, errno)`; the typed wrappers add meaning).

Arity set 0..6 is the full Linux ceiling (no Linux syscall takes >6 register args). Ship all seven —
they are inert until used (§6), so unused arities cost nothing in the emitted self-image.

### §1.2 How a POINTER argument crosses into an `i64` slot

Two argument shapes carry an address, and the opaque-`ptr` law forbids `ptr to i64`. Each shape gets
its OWN tiny bridge primitive whose job is exactly the sanctioned reinterpret — the direct analogue
of `f64_bits` being the sanctioned float→bits reinterpret (a reinterpret the language otherwise
forbids, lowered by the compiler, never a `to` cast). Both are NEEDED ONLY by the SECOND crumb
onward — the FIRST proof (§5) uses NEITHER.

1. **An existing pointer value** (the `ptr` returned by `teko::mem::as_cstr(s)`,
   `teko::mem::as_ptr(s)`, or `teko::mem::buf_ptr(n)`):

   ```
   teko::sys::ptr_word(p: ptr): i64
   ```

   Signature `(ptr): i64`. The opaque `ptr` accepts any typed `ptr<T>` via `ptr_widens_to_opaque`
   (`src/checker/resolve.tks:1409`), exactly as `bytes_from_ptr` already does. Lowers to
   `(int64_t)(uintptr_t)(<p>)`. This is the ONLY place a `ptr` becomes an integer — a compiler
   intrinsic, not a user `to` cast, so the opaque-ptr law is intact.

2. **The address of an `extern type = struct` local** (a `Timespec`, `Stat`, … — the C1 deliverable),
   for the `SYS_clock_gettime`/`SYS_stat` family that write THROUGH a struct pointer:

   ```
   teko::sys::ref_word(x: T): i64          // T is any addressable local (extern-struct or prim)
   ```

   This reuses the C1 `ref T` → `&local` address path EXACTLY. C1 established (codegen.tks ~`:5194`,
   the `pn == "Ref"` arm) that a reference crossing emits `&x` over a `mut` lvalue the checker has
   proven addressable. `ref_word` is dispatched as a SPECIAL codegen emitter (alongside `as_cstr` at
   `codegen.tks:5020`), reads `c.args[0]` — an addressable local lvalue — and emits
   `((int64_t)(uintptr_t)&(<local>))`. Its checker gate reuses the `__unwrap<T>`/`peak_rss` addressable
   -lvalue proof already in `typer.tks:1637-1644` (a `var`/`mut` lvalue; a non-lvalue is rejected).
   The struct's C typedef is byte-identical to the foreign `struct` (C1's `emit_extern_struct_typedef`
   drops only `tk_struct_hdr __hdr`), so `&local` is a correct kernel-ABI pointer with no marshalling.

   > IMPLEMENTER NOTE: `ref_word`'s checker signature is the one place that wants a generic/lvalue
   > shape rather than a fixed `Func`. If plumbing a generic `ref T` builtin param proves heavy, ship
   > `ref_word` as an lvalue-checked SPECIAL form (mirror `__unwrap<T>` in `typer.tks`, which already
   > takes "an addressable `var`" and returns the opaque word) — do NOT invent a new `Func` shape.

Naming: `ptr_word`/`ref_word` live in `teko::sys` (next to the syscall family and the numbers),
because they exist SOLELY to feed a syscall register. `f64_bits` set the precedent that a
reinterpret intrinsic need not sit in `teko::mem`.

### §1.3 Why fixed-arity and not a variadic

A variadic `syscall(nr, ..args)` would need a `params []i64` slice, which lowers to a `tk_slice_i64`
that the inline-asm helper cannot consume without unpacking at the call site — defeating the
"helper in the preamble, not inline at every call" mandate. Fixed arity keeps each call a plain
`tk_syscallN(...)` C call whose args land straight in the argument registers.

---

## §2 — The codegen intrinsic, C leg (the build target)

### §2.1 The preamble helpers (x86_64 Linux — PRIMARY, the current build arch)

Mirror the `f64_bits` model: the asm lives in a `static inline` helper emitted ONCE into the preamble,
never inline at each call site. The `syscallN` builtin call then lowers to a plain `tk_syscallN(...)`
C call. Exact text for x86_64 (musl-convention: `rax`=nr+ret, args `rdi rsi rdx r10 r8 r9`, clobber
`rcx r11 memory`; `r10/r8/r9` have no constraint letter so they use register-asm locals):

```c
/* --- teko §16 raw-syscall intrinsic (x86_64 Linux); emitted only for arities the program uses --- */
static inline long tk_syscall0(long n) {
    long ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
    return ret;
}
static inline long tk_syscall1(long n, long a1) {
    long ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}
static inline long tk_syscall2(long n, long a1, long a2) {
    long ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return ret;
}
static inline long tk_syscall3(long n, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile ("syscall"
        : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}
static inline long tk_syscall4(long n, long a1, long a2, long a3, long a4) {
    long ret; register long r10 __asm__("r10") = a4;
    __asm__ volatile ("syscall"
        : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10) : "rcx", "r11", "memory");
    return ret;
}
static inline long tk_syscall5(long n, long a1, long a2, long a3, long a4, long a5) {
    long ret; register long r10 __asm__("r10") = a4; register long r8 __asm__("r8") = a5;
    __asm__ volatile ("syscall"
        : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8) : "rcx", "r11", "memory");
    return ret;
}
static inline long tk_syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6) {
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;
    __asm__ volatile ("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return ret;
}
```

Notes:
- `"memory"` clobber is mandatory — a syscall (write/read/mmap) mutates memory the optimiser cannot
  see, and the buffer store MUST be ordered before the instruction. This is the same discipline the
  `f64_bits` ruling demanded (pun in MEMORY, never a register round-trip).
- No `#include` is added. The helper is self-contained `long` arithmetic + one `__asm__ volatile`.
  The generated TU already sets `#pragma GCC diagnostic` and targets gcc/clang, the only supported
  toolchain, so GNU register-asm locals are in-bounds.
- `long` == `int64_t` on the LP64 x86_64 target; the `i64` args pass straight through.

### §2.2 Where the helpers are emitted (and the USE-GATE — the fixpoint hinge)

Emit into the PREAMBLE slot in `tk_emit_c_mode` (`codegen.tks`), immediately AFTER the
`#pragma GCC diagnostic` block (`codegen.tks:13340`) and BEFORE the forward-typedef pass. Emit ONLY
the arities the program actually calls, discovered by a one-shot pre-scan of the typed tree:

```
b = cb(b, "...#pragma...\n\n")                         // existing, :13337-13340
var sc_used = cg_scan_syscall_arities(prog)            // NEW: bit-set over 0..6 (+ ptr_word/ref_word)
b = cg_emit_syscall_helpers(b, sc_used)                // NEW: emits only the used tk_syscallN helpers
// ... existing forward-typedef pass continues at :13342
```

New helper fns (both in `codegen.tks`, full Javadoc):

```
/**
 * cg_scan_syscall_arities — walk the typed program once and record which raw-syscall arities
 * (`teko::sys::syscall0..syscall6`) any call site names, so the preamble emits EXACTLY the
 * `static inline tk_syscallN` helpers the program uses and no others. A program with zero syscall
 * calls (the compiler's OWN self-image) yields the empty set, so its emitted preamble is
 * byte-identical to a build that never knew the intrinsic — the property that keeps the reseed a
 * clean fixpoint (§6). Resolution mirrors the builtin dispatch: a call whose last path segment is
 * `syscallN` and whose qualifier is bare or `teko`-rooted (`builtin_qualifier_ok`).
 *
 * @param prog  the lowered/typed program being emitted
 * @return      a `SyscallUse` bit-set naming the arities (0..6) reached by some call
 * @since       §16
 */
fn cg_scan_syscall_arities(prog: CgProg): SyscallUse { ... }

/**
 * cg_emit_syscall_helpers — append the x86_64-Linux `static inline long tk_syscallN(...)` raw-syscall
 * helpers named by `used` to the preamble buffer, in ascending arity, each carrying the
 * `asm volatile("syscall" ...)` with the SysV-Linux register convention (args `rdi rsi rdx r10 r8 r9`,
 * nr+ret in `rax`, clobber `rcx r11 memory`). Emits NOTHING when `used` is empty. aarch64-Linux
 * (args `x0-x5`, nr in `x8`, `svc #0`) is guarded behind `#arch` and honest-stops here until the
 * build host can validate it (§2.4).
 *
 * @param buf   the preamble byte buffer
 * @param used  the arity bit-set from `cg_scan_syscall_arities`
 * @return      `buf` with the requested helpers appended
 * @since       §16
 */
fn cg_emit_syscall_helpers(buf: []byte, used: SyscallUse): []byte { ... }
```

### §2.3 The builtin dispatch arm

In `emit_call`'s builtin name-substitution ladder (`codegen.tks`, next to `f64_bits` at `:5047`):

```
else if last == "syscall0" { builtin = "tk_syscall0"; has_builtin = true }   // (nr) -> i64
else if last == "syscall1" { builtin = "tk_syscall1"; has_builtin = true }   // (nr,a0) -> i64
else if last == "syscall2" { builtin = "tk_syscall2"; has_builtin = true }   // (nr,a0,a1) -> i64
else if last == "syscall3" { builtin = "tk_syscall3"; has_builtin = true }   // (nr,a0,a1,a2) -> i64
else if last == "syscall4" { builtin = "tk_syscall4"; has_builtin = true }
else if last == "syscall5" { builtin = "tk_syscall5"; has_builtin = true }
else if last == "syscall6" { builtin = "tk_syscall6"; has_builtin = true }
```

Args emit plainly (each is already an `i64` C expression), so `teko::sys::syscall1(SYS, 42)` lowers to
`tk_syscall1((int64_t)SYS_value, (int64_t)42)`. `ptr_word` is one more arm in the same ladder
(`builtin = "..."` — but it needs a `(int64_t)(uintptr_t)` cast, so route it to a mini special emitter
like `as_cstr` rather than a bare name-substitution). `ref_word` is a SPECIAL emitter in the
`as_cstr`/`buf_ptr` block (`codegen.tks:5019-5022`).

### §2.4 The checker registration

In `src/checker/scope.tks` `builtin_fn` (next to `f64_bits` at `:1102`), full Javadoc on a helper that
builds each fixed `i64^(N+1) -> i64` signature:

```
if name == "syscall0" { return syscallN_signature(0) }
...
if name == "syscall6" { return syscallN_signature(6) }
if name == "ptr_word" { return Func { params = <[ptr]>; ret = <i64>; ... } }
// ref_word: lvalue-checked special form (mirror __unwrap<T>, typer.tks:1637), NOT a plain Func
```

`builtin_qualifier_ok` (`typer.tks:3346`) ALREADY admits the two spellings codegen can lower — bare
`syscall1(...)` or `teko`-rooted `teko::sys::syscall1(...)` at any depth. No qualifier change needed.

### §2.5 aarch64-Linux — DESIGN-AHEAD

Same helper shape under an `#arch("aarch64")` guard: args in `x0-x5`, nr in `x8`, instruction
`svc #0`, ret in `x0`, no `rcx/r11` clobber (clobber `memory` only):

```c
static inline long tk_syscall3(long n, long a1, long a2, long a3) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a1;
    register long x1 __asm__("x1") = a2;
    register long x2 __asm__("x2") = a3;
    __asm__ volatile ("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}
```

Since the build host is x86_64, `cg_emit_syscall_helpers` HONEST-STOPS on the aarch64 arm (a named
"§16 aarch64 raw-syscall helper not yet validated on this host" error) until a build host can prove
it — the exact discipline C1 used for its deferred native lower. The x86_64 arm is the target that
MUST build+run for this crumb.

---

## §3 — The codegen intrinsic, native leg (Doc-2 terminal phase)

HONEST-STOP now. A `syscallN`/`ptr_word`/`ref_word` builtin call is NOT in the native subset
(`lower_call` in `src/lir/lower.tks` lowers only Teko→Teko calls plus `exit`/`panic`), so it falls
straight into the terminal `_ => error { "native backend N1: <kind> not yet lowered (N2)" }`
(`lower.tks:1853`) with NO new code — exactly as C1's `ExternStructBody` fell into its `_ =>` arm. The
eventual native lowering emits the `syscall`/`svc` instruction directly (no helper indirection), and
is deferred to Doc-2's terminal native phase. The C leg is this crumb's build+run target.

---

## §4 — `teko::sys` syscall numbers

Extend `src/sys/sys.tks` with `#os("linux")`-guarded, `#arch`-differentiated numeric consts,
transcribed from the kernel ABI (never a C header/macro), typed `i64` to match the `nr`/`a*` params
(no cast at the call). Minimal first set (x86_64 Linux `arch/x86/entry/syscalls/syscall_64.tbl`):

```
/**
 * SYS_write — the x86_64 Linux syscall number for `write(fd, buf, count)`. Part of `teko::sys`, the
 * curated per-`#os`/`#arch` syscall-number module the §16 raw-syscall intrinsic reads: every value is
 * a literal Teko `const` transcribed from the kernel ABI table (never a C header/macro), `#os`/`#arch`
 * guarded so the §17 prune keeps exactly the target's definition before the checker sees the others.
 * x86_64 value: 1. aarch64 (`unistd.h` generic table) differs — 64.
 * @since §16
 */
#os("linux") #arch("x86_64")
pub const SYS_WRITE: i64 = 1

/**
 * SYS_exit_group — the x86_64 Linux syscall number for `exit_group(status)`, which terminates ALL
 * threads of the process with `status` (the correct process-exit primitive; plain `exit`/60 ends only
 * the calling thread). x86_64 value: 231. aarch64 value: 94. The FIRST-proof intrinsic (§5).
 * @since §16
 */
#os("linux") #arch("x86_64")
pub const SYS_EXIT_GROUP: i64 = 231
```

Add the `#os("linux") #arch("aarch64")` twins (`SYS_WRITE = 64`, `SYS_EXIT_GROUP = 94`) as their own
prunable blocks — same self-contained-per-target discipline the existing `CLOCK_*` pairs use. The
next subsystems (§7) add `SYS_clock_gettime` (228 / aarch64 113), `SYS_getrandom` (318 / 278),
`SYS_mmap` (9 / 222), `SYS_read` (0 / 63) as they land — each a leaf edit to `sys.tks`.

> Confirm whether `sys.tks` already supports a combined `#os #arch` guard on one const or whether the
> §17 prune wants nested/separate directives — the existing consts are `#os`-only. If `#arch` stacking
> is not yet a prune input, that is a PREREQUISITE leaf (extend `src/build/prune.tks`) and must
> sequence before this — flagged, not silently assumed.

---

## §5 — FIRST PROOF CRUMB (the first thing to implement)

**`SYS_exit_group(code)` via `syscall1` — the smallest verifiable syscall.** Chosen over `SYS_write`
because it needs ZERO buffer/pointer surface: no `ptr_word`, no `as_cstr`, no struct. It exercises the
entire keystone — the `syscall1` builtin (checker + dispatch), the preamble helper emit, the use-gated
fixpoint, and a `teko::sys` number surviving the §17 prune — and is verified by the harness EXIT CODE
alone (`exit_group(42)` terminates the process at exactly 42, bypassing `main`'s return and the
`tk_exit_status` normalizer).

### Fixture: `examples/regressions/sys_exit_group/`

Layout mirrors `sys_clock` (local `src/sys/` mirror, since an external project cannot see the
compiler-internal `teko::sys` — the `generic_sort`/`sys_clock` precedent). The syscall NUMBER comes
from the local mirror (`sys::SYS_EXIT_GROUP`, bare `sys::`); the syscall INTRINSIC comes from the
`teko`-rooted builtin spelling (`teko::sys::syscall1`, resolved by `builtin_qualifier_ok` regardless
of the mirror).

`main.tks`:
```
/** Raise the process exit code to 42 through the raw x86_64 Linux `exit_group` syscall, proving the
 *  §16 syscall1 codegen intrinsic issues the instruction and the kernel honours it. `syscall1` never
 *  returns (the process dies); the trailing `exit(0)` is dead but keeps the tree well-typed. */
teko::sys::syscall1(sys::SYS_EXIT_GROUP, 42)
exit(0)
```

`src/sys/sys.tks`: the local mirror of the `SYS_EXIT_GROUP` `#os("linux")`/`#arch` const pair
(byte-for-byte structural mirror of the real block, per §4).

`sys_exit_group.tkp`: `name = "sys_exit_group"`, `source = "src"`, `[artifact] kind = "binary"`.

`sys_exit_group.tkr`:
```
Feature: sys_exit_group — the §16 raw-syscall intrinsic issues x86_64 Linux exit_group(42)

  Scenario: raw_exit_group_sets_exit_code — the process exits 42 via the syscall instruction
    When the program is built and run
    Then it exits 42
```

**Expected native exit code: 42.**

Validation = COMPILE only (`--no-verify --release`, `TEKO_BACKEND=c`, `ulimit -v 6291456`), then run
the built binary and read `$?`. NEVER `teko test .`.

### Second crumb (immediately after): `SYS_write` proof
`teko::sys::syscall3(sys::SYS_WRITE, 1, teko::sys::ptr_word(teko::mem::as_cstr("hi\n")), 3)` then
`exit(0)` — exercises `ptr_word` + a buffer address; expected exit 0 with `hi` on stdout. This is the
first exercise of the §1.2 pointer bridge.

---

## §6 — Reseed / ladder verdict

**CLEAN FIXPOINT, single reseed, intrinsic inert in the corpus until first use.**

Adding the dispatch arm + `cg_scan_syscall_arities`/`cg_emit_syscall_helpers` to `codegen.tks` and the
signatures to `scope.tks` is compiler-touching → reseed per Finding A (function-add). BUT because the
helpers are USE-GATED (§2.2) and the compiler's own corpus calls ZERO syscalls, the pre-scan returns
the empty set for the self-image, no helper is emitted, and the dispatch arm never fires. So:

- gen0 (seed, no intrinsic) emits tc1's `teko.c` with no syscall helpers.
- tc1 (has the intrinsic in source) emits gen1's `teko.c` — the pre-scan sees no syscall call in the
  compiler corpus, emits no helpers ⇒ gen1.c == tc1.c.
- tc2, tc3 likewise ⇒ **tc1 == tc2 == tc3, byte-identical, NO ladder** — the same clean-fixpoint
  result C1 got ("corpus declares zero `extern type = struct` → codegen arm inert in self-image").

The reseed is therefore a simple one-shot swap (new seed = tc1 = tc2 = tc3), gated by the coordinator's
real 3-gen build + `MEM_PARANOID` tree exit 0 + the `sys_exit_group` fixture exit 42 +
`provenance_gate.sh` PASS.

> Contrast — the AVOIDED degradation: if the implementer instead emits the helpers UNCONDITIONALLY
> (every `teko.c`, not use-gated), then tc1.c (made by gen0, no helpers) ≠ gen1.c (made by tc1, HAS
> helpers) ⇒ a ONE-STEP LADDER tc1 ≠ tc2 == tc3 — still acceptable (it is exactly the `f64_bits`
> intrinsic's shape, per §11.2), but strictly worse than the use-gated clean fixpoint. RECOMMEND
> use-gated. If the pre-scan proves awkward against the linear preamble emission, the one-step ladder
> is the sanctioned fallback, not a blocker.

---

## §7 — Ordered next crumbs (after the intrinsic lands)

Each migrates a `teko_rt.c` subsystem off its C implementation onto the raw syscall, smallest first.
Ordering follows least-surface → most:

1. **`SYS_exit_group` — process exit** (FIRST PROOF, §5; also retires `tk_exit`'s libc `exit`). Leaf
   use of the intrinsic; reseed already done by the keystone.
2. **`SYS_write` — `tk_write`/`tk_print`/stdout+stderr** (second crumb, §5). First `ptr_word` user.
   The print path is the highest-traffic subsystem; migrating it proves the buffer-address bridge.
3. **`SYS_read` — `tk_rt_read_line`/`read_stdin`.** `ptr_word` + a stack/region buffer; still no
   struct.
4. **`SYS_clock_gettime` — `tk_rt_monotonic_ns`.** FIRST `ref_word` + C1 `extern type = struct Timespec`
   user (Timespec already delivered by C1's `extern_type_struct` fixture). Reads `CLOCK_MONOTONIC`
   (already in `teko::sys`) into a `Timespec` local, `ref_word(ts)` as the pointer arg.
5. **`SYS_getrandom` — `tk_rt_secure_bytes`.** `ptr_word(buf)` + `len` + flags; a leaf once write/read
   land (same shape).
6. **ARENA `mmap`/`munmap` — the big one.** `SYS_mmap`/`SYS_munmap` replace the arena's libc `mmap`.
   This is §16-core (the keystone the whole runtime memory model rides; Doc-1 improves the arena
   after). Retains the most C; sequence LAST because a regression here OOMs the whole gate. Its own
   fixpoint check (the arena is used by the compiler itself, so this one is NOT inert — expect a real
   reseed with a genuine emit delta, unlike steps 1-5 which are corpus-only until the runtime rewrite
   consumes them).

Steps 1-5 are leaf-or-single-reseed (they touch `teko_rt`/`.tks` runtime callers, not `codegen.tks`
emit logic); step 6 is the load-bearing reseed. Each keeps the deleted C symbol (`tk_rt_*`) removal
to a SEPARATE two-legs gate crumb (the C7-style "symbol deletion after reseed" discipline), never
folded into the migration crumb.

---

## §8 — Risks + law tensions

- **`ptr_word`/`ref_word` are new opaque→integer reinterprets.** Tension with the opaque-ptr law
  (`ptr_opaque_error`: no ptr arithmetic/casts). RESOLUTION (law-first): they are COMPILER INTRINSICS,
  not user `to` casts — the identical carve-out the owner already ratified for `f64_bits` (a
  reinterpret the language forbids in surface syntax, lowered by the compiler). They live in
  `teko::sys` (syscall-only) and are `unsafe by contract` in the `as_cstr`/`load_u64` sense
  (`capability_audit.md §4`). No new law needed; this rides the `f64_bits` precedent.
- **`"memory"` clobber correctness.** A missing `memory` clobber would let the optimiser reorder a
  buffer store after the `write` syscall — silent data corruption. MITIGATION: it is in every helper
  above (mandatory), and the §5-second-crumb `SYS_write` fixture (stdout content check) catches a
  regression.
- **`#os #arch` combined prune.** `sys.tks` today is `#os`-only; the numbers need `#arch`
  discrimination. If `src/build/prune.tks` does not yet take `#arch` as a prune input, that is a
  PREREQUISITE leaf (§4 note) — sequence it FIRST. Not a HALT, but a real ordering constraint the
  implementer must confirm before the numbers land.
- **Errno convention surfaced raw.** Returning raw `i64` (`< 0` = `-errno`) pushes the error-decode
  onto the typed wrappers (§7). This is deliberate (thinnest primitive), but every wrapper MUST check
  `< 0` — a wrapper that forgets treats `-errno` as a huge unsigned length. MITIGATION: the wrappers
  are ordinary `.tks` with their own fixtures; the intrinsic's contract is documented as "raw kernel
  return, caller checks sign".

No genuine unresolved tension → NO HALT. The design is fully ratifiable law-first on the `f64_bits`
intrinsic precedent the owner already set.

---

## §9 — Design-ahead (out of THIS crumb, noted for the implementer)

- **macOS** → bind `libSystem.dylib` symbols via the C1 `extern fn` + native-lib linker path (Apple
  gives no stable syscall ABI); NOT a raw-syscall intrinsic. The `extern fn`/`extern struct` machine
  (C1) is reusable verbatim.
- **Windows** → bind `kernel32.dll`/`ntdll.dll` the same way.
- **env over `environ`** → pure Teko, no syscall/lib; separate crumb.
- **`ptr_word`/`ref_word`** are also the bridge the macOS/Windows native-lib binding will reuse to
  hand addresses to a bound symbol — designed once here.

---

## Section index

- §1 The Teko surface (syscall0..6 signatures; `ptr_word`/`ref_word` pointer bridges)
- §2 Codegen intrinsic, C leg (x86_64 helper text; use-gated preamble emit; dispatch; checker; aarch64 design-ahead)
- §3 Codegen intrinsic, native leg (honest-stop, Doc-2 terminal)
- §4 `teko::sys` syscall numbers (`SYS_WRITE`/`SYS_EXIT_GROUP`; `#os`/`#arch` prune)
- §5 FIRST PROOF crumb (`sys_exit_group`, exit 42) + second crumb (`SYS_write`)
- §6 Reseed/ladder verdict (clean fixpoint, single reseed)
- §7 Ordered next crumbs (exit → write → read → clock_gettime → getrandom → mmap)
- §8 Risks + law tensions (no HALT)
- §9 Design-ahead (macOS/Windows/env)
