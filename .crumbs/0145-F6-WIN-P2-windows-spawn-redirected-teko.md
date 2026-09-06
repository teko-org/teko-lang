---
seq: 0145
crumb-id: F6-WIN-P2
milestone: M16
gate: "[fixpoint]"
reseed-class: "expurgo"
deps: [F6-WIN-P1]
sources:
  - "docs/design/f6-process-zerolibc-windows.md:20-90"
  - "src/win32_compat.h:287-336"          # tk_win32_spawn_redirected / wait_one
  - "src/process/process.tks:803-857"      # landed run/run_quiet raw-buffer idiom
---

# 0145 · F6-WIN-P2 — Windows spawn_redirected + wait_one in Teko

> Rewrite the last two `#os("windows")` process externs over `CreateProcessA` with per-stream inheritable handle dups; raw-buffer STARTUPINFOA (no struct-FFI); drop `win_spawn_redirected`/`win_wait_one`.

## Goal

Replace `tk_rt_spawn_redirected`/`tk_win32_spawn_redirected` and `tk_rt_wait_one`/
`tk_win32_wait_one` with a Teko implementation that reuses the raw-offset-buffer STARTUPINFOA
idiom already landed in `run`/`run_quiet` (D107). The three child streams resolve to
inheritable `DuplicateHandle` copies of caller HANDLEs (pipe ends from 0144) or of file
handles opened via `CreateFileA`, with the same POSIX ownership rule (parent closes only
what it created). **Expurgo:** removes the final 2 process `from "teko_rt"` externs → the
whole `#os("windows")` process arm is C-free; `tk_rt_spawn_redirected`/`tk_rt_wait_one` +
the `tk_win32_*` helpers become dead C (swept 0147). Emission changes → reseed. Windows
peak floor unchanged (D68).

## Where

`src/process/process.tks`, `#os("windows")` region:

- delete externs `:665 win_spawn_redirected`, `:668 win_wait_one`.
- rewrite `os_spawn` (`:860`) and `os_wait_one` (`:865`) as Teko over kernel32.
- extend the raw-buffer STARTUPINFOA builder (generalize `win_startup_info` `:813`, or add
  `win_startup_redirected`) with the three `hStd*` slots + `STARTF_USESTDHANDLES`.
- add `#os("windows")` helpers: `win_dup_inheritable(h: u64): u64`,
  `win_redirect_handle(fd: i64, path: str, which: u32, for_write: bool): u64`,
  `win_env_block(env: []str): u64` (double-NUL block, "" when empty → inherit).
- reuse the STARTUPINFOA/PROCESS_INFORMATION offset consts (`:611-635`) and
  `build_cmdline`/`quote_arg` (`:764-801`), unchanged.

## How

Mirror `win32_compat.h:287-336` fact-for-fact in Teko. All `#os("windows")` `pub`/private
→ no doc-comment. The `exp` `spawn_redirected`/`spawn_redirected_fds`/`wait_one`
(`:925-946`) are untouched (they already dispatch through `os_spawn`/`os_wait_one`).

1. **win_dup_inheritable** — the inheritable duplicate the child receives; `0` when the
   source names nothing (mirror `tk_win32_redirect_handle_of_fd`):

```teko
#os("windows")
fn win_dup_inheritable(h: u64): u64 {
    if h == 0 { return 0 }
    var slot = teko::sys::ptr_word(teko::mem::buf_ptr(8)) to u64
    teko::mem::store_u64(slot, 0)
    var self = teko::sys::abi::GetCurrentProcess()
    if teko::sys::abi::DuplicateHandle(self, h, self, slot, 0, 1, teko::sys::DUPLICATE_SAME_ACCESS) == 0 { return 0 }
    teko::mem::load_u64(slot)
}
```

2. **win_redirect_handle** — a caller HANDLE (a 0144 pipe end, low-32 in `fd`) wins over a
   path; else open the path (`CreateFileA`, `CREATE_ALWAYS` for out/err, `OPEN_EXISTING`
   for in) with an inheritable `SECURITY_ATTRIBUTES`; else inherit the parent's std handle
   (`GetStdHandle` → `win_dup_inheritable`). Mirror `tk_win32_child_handle` +
   `tk_win32_redirect_handle` + `tk_win32_inheritable_std`:

```teko
#os("windows")
fn win_redirect_handle(fd: i64, path: str, which: u32, for_write: bool): u64 {
    if fd >= 0 { return win_dup_inheritable((fd & 0xffffffff) to u64) }
    if path.len == 0 { return win_dup_inheritable(teko::sys::abi::GetStdHandle(which)) }
    var sa = teko::sys::ptr_word(teko::mem::buf_ptr(SECURITY_ATTRIBUTES_SIZE)) to u64
    zero_bytes(sa, SECURITY_ATTRIBUTES_SIZE)
    store_u32_at(sa + SA_NLENGTH, SECURITY_ATTRIBUTES_SIZE to u32)
    store_u32_at(sa + SA_BINHERIT, 1)
    var access: u32 = if for_write { GENERIC_WRITE } else { GENERIC_READ }
    var disp: u32 = if for_write { CREATE_ALWAYS } else { OPEN_EXISTING }
    teko::sys::abi::CreateFileA(cstr_word(path), access, FILE_SHARE_RW, sa, disp, 0, 0)
}
```

   (add `#os("windows")` consts `GENERIC_READ: u32 = 0x80000000`, `CREATE_ALWAYS: u32 = 2`;
   `GENERIC_WRITE`/`FILE_SHARE_RW`/`OPEN_EXISTING`/`STD_*_HANDLE`/`SECURITY_*` already exist
   at `:640-662`. Add `STD_OUTPUT_HANDLE = 0xFFFFFFF5`, `STD_ERROR_HANDLE = 0xFFFFFFF4`.)

3. **win_env_block** — the double-NUL `K=V\0…\0\0` block CreateProcess wants; `0` (inherit)
   when `env` is empty. Build it with the NO-PUSHES idiom — one pass to size, allocate a
   fixed `buf_ptr(total)`, second pass to copy each `K=V` + NUL by index, trailing NUL.
   (Do NOT accumulate with `~` growth for the block bytes; the small `env` count makes a
   two-pass size+fill exact. `teko::env` overlay is already applied by the caller's `env`
   argument — the inherited block is what the child gets by default, so appending `env`
   over an inherited copy matches `spawn_envp`'s overlay semantics; source the inherited
   half from `GetEnvironmentStringsW`→A conversion OR, simplest and matching the landed
   `run` arm which passes `0`, pass only when `env.len > 0` and let non-overlaid vars
   inherit via CreateProcess's own default — but CreateProcess replaces the WHOLE block
   when non-NULL, so the inherited vars MUST be included). Include the inherited block:
   read it via `GetEnvironmentStringsA` (add extern in 0143 if the A-variant is preferred;
   else convert the W block). Size = inherited_len + Σ(len(env[i])+1) + 1.

```teko
#os("windows")
fn win_env_block(env: []str): u64 {
    if env.len == 0 { return 0 }
    var inh = teko::sys::abi::GetEnvironmentStringsA()
    var inh_len = env_block_len(inh)
    var extra: u64 = 0
    var i: u64 = 0
    loop { if i >= env.len { break } extra = extra + env[i].len + 1; i = i + 1 }
    var total = inh_len + extra + 1
    var blk = teko::sys::ptr_word(teko::runtime::region_alloc(teko::runtime::region_program(), total)) to u64
    var w = copy_block(blk, inh, inh_len)
    i = 0
    loop {
        if i >= env.len { break }
        w = copy_bytes(blk + w, env[i]) + w
        teko::mem::store_u8(blk + w, 0); w = w + 1
        i = i + 1
    }
    teko::mem::store_u8(blk + w, 0)
    teko::sys::abi::FreeEnvironmentStringsA(inh)
    blk
}
```

   (`env_block_len`/`copy_block`/`copy_bytes` are small `#os("windows")` byte helpers over
   `load_u8`/`store_u8`; `GetEnvironmentStringsA`/`FreeEnvironmentStringsA` are the A-twins
   of the existing W externs — add to 0143's list.)

4. **os_spawn** — build cmdline + env block + a redirected STARTUPINFOA raw buffer, then
   `CreateProcessA(0, cmdline, 0, 0, /*inherit*/1, 0, envblk, dir?, si, pi)`; close the
   three inheritable dups + the env block; return the child HANDLE (low-32) or SPAWN_FAILED.
   Mirror `tk_win32_spawn_redirected`:

```teko
#os("windows")
fn os_spawn(argv: []str, dir: str, env: []str, in_path: str, out_path: str, err_path: str, in_fd: i64, out_fd: i64, err_fd: i64): i64 {
    if argv.len == 0 { return SPAWN_FAILED to i64 }
    var startup = teko::sys::ptr_word(teko::mem::buf_ptr(STARTUPINFOA_SIZE)) to u64
    zero_bytes(startup, STARTUPINFOA_SIZE)
    teko::mem::store_u64(startup + STARTUPINFOA_CB, STARTUPINFOA_SIZE)
    store_u32_at(startup + STARTUPINFOA_DWFLAGS, STARTF_USESTDHANDLES)
    var hin  = win_redirect_handle(in_fd,  in_path,  STD_INPUT_HANDLE,  false)
    var hout = win_redirect_handle(out_fd, out_path, STD_OUTPUT_HANDLE, true)
    var herr = win_redirect_handle(err_fd, err_path, STD_ERROR_HANDLE,  true)
    teko::mem::store_u64(startup + STARTUPINFOA_HSTDINPUT, hin)
    teko::mem::store_u64(startup + STARTUPINFOA_HSTDOUTPUT, hout)
    teko::mem::store_u64(startup + STARTUPINFOA_HSTDERROR, herr)
    var pinfo = teko::sys::ptr_word(teko::mem::buf_ptr(PROCESS_INFORMATION_SIZE)) to u64
    zero_bytes(pinfo, PROCESS_INFORMATION_SIZE)
    var envblk = win_env_block(env)
    var dirp: u64 = if dir_is_here(dir) { 0 } else { cstr_word(dir) }
    var ok = teko::sys::abi::CreateProcessA(0, cstr_word(build_cmdline(argv)), 0, 0, 1, 0, envblk, dirp, startup, pinfo)
    win_close_if(hin); win_close_if(hout); win_close_if(herr)
    if ok == 0 { return SPAWN_FAILED to i64 }
    var hproc = teko::mem::load_u64(pinfo + PROCINFO_HPROCESS)
    _ = teko::sys::abi::CloseHandle(teko::mem::load_u64(pinfo + PROCINFO_HTHREAD))
    (hproc & 0xffffffff) to i64
}
```

   (`win_close_if(h)` = `if h != 0 { _ = CloseHandle(h) }`. The child HANDLE keeps only its
   low-32 — same 32-significance guarantee as 0144; `wait_one` zero-extends it back.)

5. **os_wait_one** — `WaitForSingleObject` + `GetExitCodeProcess` + `CloseHandle` (mirror
   `tk_win32_wait_one`; the exact shape already lives in `win_process_exit` `:827`, reuse
   it):

```teko
#os("windows")
fn os_wait_one(raw: i64): i32 {
    if raw < 0 { return SPAWN_FAILED }
    var h = (raw & 0xffffffff) to u64
    var code = win_process_exit(h)
    _ = teko::sys::abi::CloseHandle(h)
    code
}
```

6. Delete `win_spawn_redirected`/`win_wait_one` externs. The `#os("windows")`
   `spawn_payload`/`count_prefixed_list`/`fd_token`/`TOKEN_SEP` (`:686-712`) were ONLY the
   C-payload serializer — now dead Teko; remove them too (they fed the deleted extern).

## Rulings & laws

- **Teko-only / D90:** Teko rewrite; `teko_rt.c`/`win32_compat.h` untouched here (dead C
  swept 0147).
- **D107 / D102 follow-up #1:** STARTUPINFOA/PROCESS_INFORMATION via **raw offset buffer**,
  NOT `from "tag"` (windows.h TU collision) — the idiom already in `run`. No struct-FFI
  surface. Confirms the D102 follow-up stays closed.
- **NO PUSHES:** `win_env_block` sizes then fills a fixed region by index; `build_cmdline`
  is the pre-existing `~` path (small argv, untouched). No dynamic array growth added.
- **D68 floor:** Windows-leg peak ≤ the replaced C.
- **exp law:** `pub`/private, no doc-comment, zero `//`.
- **D104-T5:** Windows CI leg + process-half harness = sole oracle; no new test.
- **Fork protocol:** no fork — mirrors ratified `tk_win32_*` behavior.

## Fixtures

`none — the pre-existing F5/F6 process-half harness on the Windows CI leg is the oracle`
(Linux fixpoint does not drive `#os("windows")`; owner "sem testes").

## Gate

`[fixpoint]`: Linux `gen2==gen3` byte-identical + Windows CI leg green (spawn_redirected /
pipe-through-child / wait_one harness). Reseed `bootstrap/teko.c`. Report Windows-leg peak
vs base (floor).

## Deps

F6-WIN-P1

## Done when

`os_spawn`/`os_wait_one` are Teko-over-kernel32, all 7 process `from "teko_rt"` externs are
gone, the Linux fixpoint reseeds byte-identical, and the Windows CI leg passes the harness.
