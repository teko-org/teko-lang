---
seq: 0144
crumb-id: F6-WIN-P1
milestone: M16
gate: "[fixpoint]"
reseed-class: "expurgo"
deps: [F6-WIN-A0]
sources:
  - "docs/design/f6-process-zerolibc-windows.md:20-90"
  - "src/runtime/teko_rt.c:4013-4130"   # tk_rt_pipe/close_fd/fd_wait/fd_fill/take_byte
---

# 0144 · F6-WIN-P1 — Windows pipes + fd staging in Teko

> Rewrite the five `#os("windows")` pipe/fd os_* helpers over kernel32, dropping 5 of the 7 `from "teko_rt"` externs; raw-HANDLE currency; reuse the shared Teko stage buffer.

## Goal

Replace the Windows anonymous-pipe and fd-staging path with a Teko implementation over
`CreatePipe`/`PeekNamedPipe`/`ReadFile`/`CloseHandle`/`DuplicateHandle`/`Sleep`. The
Windows `fd: i64` becomes a raw kernel HANDLE (not a CRT descriptor) — the currency the
already-landed `run` arm and `ProcHandle.raw` doc already speak. **Expurgo:** removes
`win_pipe`/`win_close_fd`/`win_fd_wait_readable`/`win_fd_fill`/`win_fd_take_byte` externs,
making `tk_rt_pipe`/`tk_rt_fd_wait_readable`/`tk_rt_fd_fill`/`tk_rt_fd_take_byte` dead C
(swept in 0147). Emission changes → drives a reseed. The Windows runtime peak must not
grow (ratchet floor D68: rewritten-in-Teko ≤ the C it replaces).

## Where

`src/process/process.tks`, `#os("windows")` region (`:664-892`):

- delete externs `:671 win_pipe`, `:674 win_close_fd`, `:677 win_fd_wait_readable`,
  `:680 win_fd_fill`, `:683 win_fd_take_byte`.
- rewrite `os_pipe` (`:870`), `os_close_fd` (`:875`), `os_fd_wait_readable` (`:880`),
  `os_fd_fill` (`:885`), `os_fd_take_byte` (`:890`) as Teko over kernel32.
- reuse `fd_stage_base` (`:345`, not os-gated) and `stage_take_byte` (`:370`, shared).
- reuse `pack_pipe`/`unpack_read`/`unpack_write` (`:116-128`) unchanged.
- add `#os("windows")` helper `win_peek_ready(h: u64): i32` (the single poll step).

## How

The Windows arm mirrors `teko_rt.c:4013-4130` fact-for-fact, in Teko. All new fns are
`#os("windows")` and `pub`/private → **no doc-comment** (style law; only `exp` gets docs;
the `exp` surface `pipe`/`close_fd`/`fd_*` in `:909-979` is untouched).

1. **os_pipe** — `CreatePipe` with `sa = 0` (both ends non-inheritable, the `_O_NOINHERIT`
   equivalent), two 8-byte out-slots via `buf_ptr`, then pack the low-32 of each HANDLE:

```teko
#os("windows")
fn os_pipe(): i64 {
    var slots = teko::sys::ptr_word(teko::mem::buf_ptr(16)) to u64
    teko::mem::store_u64(slots, 0)
    teko::mem::store_u64(slots + 8, 0)
    if teko::sys::abi::CreatePipe(slots, slots + 8, 0, PIPE_CAPACITY to u32) == 0 { return NO_FD }
    var read_h = teko::mem::load_u64(slots)
    var write_h = teko::mem::load_u64(slots + 8)
    pack_pipe((read_h & 0xffffffff) to i64, (write_h & 0xffffffff) to i64)
}
```

   Rationale for the low-32 pack: Win32 HANDLEs are 32-significant-bit
   (`HandleToLong`/`LongToHandle`) → lossless round-trip; unpack zero-extends back to a
   valid HANDLE. (design doc §2.)

2. **os_close_fd** — `CloseHandle` on the zero-extended HANDLE:

```teko
#os("windows")
fn os_close_fd(fd: i64): i32 {
    if fd < 0 { return NO_FD to i32 }
    if teko::sys::abi::CloseHandle((fd & 0xffffffff) to u64) != 0 { return 0 }
    NO_FD to i32
}
```

3. **win_peek_ready** — one non-blocking read of the pipe: READY when bytes pend OR the
   write end is gone (`ERROR_BROKEN_PIPE` = EOF, must wake a reader), TIMEOUT when neither,
   ERROR otherwise (mirror `tk_rt_win_peek_ready`):

```teko
#os("windows")
fn win_peek_ready(h: u64): i32 {
    var avail = teko::sys::ptr_word(teko::mem::buf_ptr(8)) to u64
    teko::mem::store_u64(avail, 0)
    if teko::sys::abi::PeekNamedPipe(h, 0, 0, 0, avail, 0) == 0 {
        if teko::sys::abi::GetLastError() == teko::sys::ERROR_BROKEN_PIPE { return FD_WAIT_READY }
        return FD_WAIT_ERROR
    }
    if teko::mem::load_u64(avail) > 0 { return FD_WAIT_READY }
    FD_WAIT_TIMEOUT
}
```

4. **os_fd_wait_readable** — the poll loop (no blocking-with-deadline for an anon pipe on
   Win32; poll every `WIN_PIPE_POLL_MS` until the deadline — mirror the C, same measured
   cost):

```teko
#os("windows")
fn os_fd_wait_readable(fd: i64, timeout_ms: i64): i32 {
    if fd < 0 { return FD_WAIT_ERROR }
    var h = (fd & 0xffffffff) to u64
    var waited: i64 = 0
    loop {
        var peek = win_peek_ready(h)
        if peek != FD_WAIT_TIMEOUT { return peek }
        if timeout_ms >= 0 && waited >= timeout_ms { return FD_WAIT_TIMEOUT }
        teko::sys::abi::Sleep(teko::sys::WIN_PIPE_POLL_MS)
        waited = waited + (teko::sys::WIN_PIPE_POLL_MS to i64)
    }
}
```

5. **os_fd_fill** — reuse the shared `fd_stage_base()`; `ReadFile` into `stg +
   STAGE_HDR_BUF` (mirror the Linux/mac `os_fd_fill` at `:356`/`:584`, only the read call
   differs):

```teko
#os("windows")
fn os_fd_fill(fd: i64, timeout_ms: i64): i64 {
    var stg = fd_stage_base()
    teko::mem::store_u64(stg + STAGE_HDR_STAGED, 0)
    teko::mem::store_u64(stg + STAGE_HDR_TAKEN, 0)
    if fd < 0 { return FILL_ERROR }
    var ready = os_fd_wait_readable(fd, timeout_ms)
    if ready == FD_WAIT_TIMEOUT { return FILL_TIMEOUT }
    if ready != FD_WAIT_READY { return FILL_ERROR }
    var got = teko::sys::ptr_word(teko::mem::buf_ptr(8)) to u64
    teko::mem::store_u64(got, 0)
    if teko::sys::abi::ReadFile((fd & 0xffffffff) to u64, (stg + STAGE_HDR_BUF), PIPE_CAPACITY to u32, got, 0) == 0 {
        if teko::sys::abi::GetLastError() == teko::sys::ERROR_BROKEN_PIPE {
            teko::mem::store_u64(stg + STAGE_HDR_STAGED, 0)
            return 0
        }
        return FILL_ERROR
    }
    var n = teko::mem::load_u64(got)
    teko::mem::store_u64(stg + STAGE_HDR_STAGED, n)
    n to i64
}
```

   (`ReadFile` returning 0 bytes / broken pipe = EOF, staged 0 — the reader's end-of-file.)

6. **os_fd_take_byte** — collapse to the shared drain (identical to the mac/linux ones at
   `:381`/`:386`):

```teko
#os("windows")
fn os_fd_take_byte(): i32 {
    stage_take_byte()
}
```

7. Delete the 5 now-unused externs. Leave `win_spawn_redirected`/`win_wait_one` (0145).

## Rulings & laws

- **Teko-only / D90:** rewrite in Teko; `teko_rt.c` untouched; the dead C bodies are swept
  in 0147, not edited here.
- **D106:** kernel32 resolves on the CI Windows leg (precedent: `sync.tks`).
- **NO PUSHES:** the stage buffer is a fixed `PIPE_CAPACITY` region reused per fill; the
  `read_to_eof` accumulator is the pre-existing `exp` path — untouched. Zero dynamic growth
  added.
- **Ratchet D68 / floor:** Windows peak measured on the CI Windows build must not exceed
  the C it replaces (the Linux self-build peak is unaffected — this is `#os("windows")`).
- **exp law:** new fns are `pub`/private, zero doc-comment, zero `//`.
- **D104-T5 (owner carve-out):** the Windows arm is NOT driven by the Linux fixpoint at
  runtime → the Windows CI leg + the pre-existing F5/F6 process-half harness are the sole
  oracle. No new test authored (owner's "sem testes").
- **Fork protocol:** no fork — every step mirrors ratified C behavior; the low-32 pack rests
  on a documented Win32 guarantee (design doc §2).

## Fixtures

`none — the pre-existing F5/F6 process-half harness on the Windows CI leg is the oracle`
(the Linux self-build fixpoint does not drive `#os("windows")`; per owner "sem testes" no
new `.tkr` is authored — Cluster A is validated by the Windows CI ladder).

## Gate

`[fixpoint]`: Linux self-build `gen2==gen3` byte-identical (proves the shared/non-Windows
code and the emit-all-targets monolith still reproduce) + emitted-C for the Windows arm
present; **Windows CI leg green** (spawn/pipe harness). Reseed `bootstrap/teko.c`
(emission changed). Report the Windows-leg peak vs base (floor: not higher).

## Deps

F6-WIN-A0

## Done when

The five Windows pipe/fd os_* helpers are Teko-over-kernel32, the 5 externs are gone, the
Linux fixpoint reseeds byte-identical, and the Windows CI leg passes the process-half
harness.
