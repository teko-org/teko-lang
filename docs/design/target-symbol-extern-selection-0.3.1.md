# TargetSymbol — per-target foreign-symbol selection for `extern fn` (0.3.1)

- Status: **ARCHITECT PROPOSAL** (2026-08-19) — AWAITING OWNER APPROVAL. Doc-only; no product
  code touched in this change (Teko-only preserved; nothing here introduces C).
- Branch: `arch-targetsymbol` (from `fix/retirement`, local, no push).
- Author role: architect. Deliverable is this plan; the implementer of `0061` (RT-L3) copies the
  worked example verbatim.
- Closes: the "`TargetSymbol` gap" that `.crumbs/0061-RT-L3-runtime-ffi-fs-env.md` flags as the
  ONLY compiler blocker of the fs half of the RT-L3 migration.
- Governing laws in scope: minimal surface (YAGNI); reuse NAT-XL target detection, do not invent a
  parallel one; native-first / 0% C endgame (no reliance on a C preprocessor); W15 (doc-comments
  only on `exp`, never larger than the code); arrays fixed; `#arena_*` pragmas killed.

---

## 0. The headline (law-first conclusion)

**There is no new language surface to build. The `TargetSymbol` gap is a phantom: the exact
capability it names already ships, ratified, in the conditional-compilation surface
(`#os(...)` / `#arch(...)` / `#if (os == "...") … #endif`) applied to `extern fn`.** The
smallest form that closes the gap (YAGNI) is therefore: *use the guard-on-`extern` mechanism that
`src/io/file_stream.tks` already uses in the shipping compiler, and strike the proposed
`TargetSymbol` struct from crumb `0061`.*

The recommendation to the owner is a single ratifiable decision:

> **D-TS1.** The per-target foreign-symbol selection ("TargetSymbol") for the runtime fs/env/time
> migration is expressed by target-guarded `extern fn` declarations (`#os`/`#arch`/`#if`), the
> already-ratified conditional-compilation surface, resolved by `prune_cc` against the NAT-XL
> `native_target`. No new declaration form (`pub type TargetSymbol = struct {…}`) is added. The
> struct sketch in `0061` §How-2 and in `migracao…` §6 is withdrawn.

The rest of this document proves the capability already exists, shows the worked `open` example,
states the resolution rule precisely, and gives the honest-partial fallback if the owner prefers
to defer or wants the struct anyway.

---

## 1. The gap, restated precisely

Migrating fs/env from C to Teko, the SAME logical leaf operation binds to a DIFFERENT foreign
symbol per target:

| logical op | linux | macos | windows |
|---|---|---|---|
| open a file | raw `SYS_openat` syscall | `open` from `System` (libc) | `CreateFileW` from `kernel32` |
| read | raw `SYS_read` | `read` from `System` | `ReadFile` from `kernel32` |
| stat | raw `SYS_newfstatat` | `fstatat` from `System` | `GetFileAttributesExW` from `kernel32` |
| chdir | raw `SYS_chdir` | `chdir` from `System` | `_chdir` from `msvcrt` |

Two facts the naive "one symbol, two names" framing misses, and that the design must honor:

1. **It is at least a THREE-way split, not POSIX-vs-Win32.** Linux reaches the kernel by *raw
   syscall number* (no `extern` symbol at all — see `src/io/file_stream.tks:76-99`), macOS reaches
   it by a *libc symbol* (`open` from `System`), Windows by a *kernel32 symbol*. A binary
   `{posix, win32}` selector cannot express the linux leg.
2. **The whole call site differs, not just the symbol name.** POSIX `open(path, flags, mode) -> fd`
   and Win32 `CreateFileW(name, access, share, sec, disp, flags, tmpl) -> HANDLE` differ in arity,
   argument types, return type, and error convention. `src/io/file_stream.tks` proves this: the
   `#os("macos")` `os_open` body is `os_open_raw(cstr_addr(path), …)` while the `#os("windows")`
   `win_open`/`os_read` bodies marshal wide paths and OVERLAPPED out-params. A struct whose only
   per-target field is the symbol *string* assumes signatures coincide — they do not.

Both facts point the same way: the unit that varies per target is the **whole guarded
declaration** (the `extern` binding AND its Teko wrapper body), not a symbol string inside a shared
declaration. That is exactly what the guard surface already selects.

---

## 2. What already ships (the mechanism, cited)

### 2.1 The guard surface

Two spellings, one `Pred`:

- **Per-declaration attribute:** `#os("linux"|"macos"|"windows")`, `#arch("x86_64"|"arm64")`
  (`src/parser/parse_decl.tks:1120-1134`). Values are validated against the canonical axis set
  (`cc_axis_value_diag`, `src/parser/parse_cc.tks:163`) — a typo like `#os("win32")` is a diagnosed
  compile error with a "did you mean" hint, not a silent miss.
- **Region form:** `#if (os == "windows") … #elseif (…) … #else … #endif`
  (`src/parser/parse_cc.tks:172-226`), supporting `&&`, `||`, `!`, and grouping.

Both lower to a `parser::Pred` (`src/parser/ast.tks:289-307`) attached to each declaration and
carried on `parser::Item.guard`. `Function` additionally carries `os_guard: str`
(`src/parser/ast.tks:186`).

### 2.2 `extern fn` already carries the symbol + library

`extern fn NAME(params): RET = "symbol" from "lib"` parses into `Function.c_symbol` and
`Function.from_lib` (`src/parser/parse_decl.tks:319-338`, `src/parser/ast.tks:184-185`). The
`from "lib"` clause already names the resolving library (`System`, `kernel32`, `c`, `teko_rt`).

### 2.3 Combining them is a SHIPPING pattern, not a proposal

`src/io/file_stream.tks` already binds per-target foreign symbols for exactly the fs family this
migration targets:

- `#os("macos") extern fn os_open_raw(...) = "open" from "System"` (`:30-31`)
- `#os("windows") extern fn CreateFileW(...) = "CreateFileW" from "kernel32"` (`:51-52`)
- linux: no `extern` — `#os("linux") fn os_open(...)` over `teko::sys::syscall4(SYS_OPENAT, …)`
  (`:76-79`).

And the CONSUMER-facing name is unified by defining the SAME Teko name once per target under
disjoint guards: `os_open` is declared three times — `#os("linux")` (`:77`), `#os("macos")`
(`:102`), `#os("windows")` has the `win_open` variant reached through `os_read`/`os_write`/`os_close`
(`:141-167`). Callers write `os_open(...)`; the per-target selection is invisible to them.

### 2.4 The resolution rule (the load-bearing sentence)

`prune_cc` (`src/build/prune.tks:57`) runs in `frontend_check` (`src/build/project.tks:389`)
**before** the checker (`checked_program_of`, `:395`). Its `CcEnv{os, arch}` is
`target_os(manifest)` / `target_arch(manifest)` (`src/build/project.tks:61-76`), which resolve from
`TEKO_TARGET` when set and otherwise from `teko::os()` / `teko::arch()` — i.e. the **NAT-XL
`native_target`** host/target detection (`.crumbs/0104-NAT-XL-host-detect-crosslink.md`). No new
detection is introduced; the selector rides the one that already exists.

The rule has two legs, keyed by `PruneMode.keep_target_arms = c_backend_selected()`
(`src/build/project.tks:389,1252`):

- **Native backend (M4, the endgame) — `keep_target_arms = false`.** `cc_keep_item` keeps an item
  iff `eval_pred(item.guard, env)` is true (`src/build/prune.tks:52-55`). For a family of same-name
  externs under disjoint target guards, **exactly one survives**, and it survives *before the
  checker or native codegen ever runs*. The checker then sees a single `os_open`, bound to the one
  target's symbol; the native backend emits one binding. No C preprocessor is consulted — the
  native-first law holds by construction.
- **C backend (transition route only) — `keep_target_arms = true`.** All arms are kept
  (`cc_target_survives`, `src/build/prune.tks:44-54`) and each is wrapped by `guard_to_c_cond`
  (`src/codegen/codegen.tks:8598`) in `#if defined(__linux__) / __APPLE__ / _WIN32`, deferring the
  final pick to the host `cc`. Windows extern-from-lib decls additionally emit
  `#pragma comment(lib, "<lib>")` (`src/codegen/codegen.tks:8687-8689`). This route exists only
  while the published seed still emits C; it is not part of the 0% C endgame and requires nothing
  new here.

Same-name same-signature declarations under target-**disjoint** guards are legal — the checker
governs disjointness with `preds_target_disjoint` / `pred_is_target_guard`
(`src/checker/consteval.tks:72,90`), and `src/io/file_stream.tks` (three identical-signature
`os_open`s) is the shipping proof.

### 2.5 The arch axis, and syscall numbers per arch — already covered

Raw syscall numbers differ per arch (x86_64 vs arm64). The guard surface's `#arch("...")` axis and
the target-conditional const ladder (`cg_emit_cond_const_ladder`, `src/codegen/codegen.tks:8656`;
disjointness checked by `arms_pairwise_disjoint`, `src/checker/consteval.tks:128`) already express
`SYS_openat` etc. as per-arch constants. The arch half of the "possibly different raw syscall
numbers per arch" concern is thus closed by the same surface — no addition needed.

---

## 3. Why NOT the `TargetSymbol` struct (law-first rejection)

The `0061` / `migracao…` §6 sketch is:

```teko
pub type TargetSymbol = struct { posix: str; win32: str }
```

It is withdrawn for five independent reasons, any one sufficient:

1. **Parallel target detection — forbidden.** The owner's mandate is explicit: reuse NAT-XL
   detection, do not invent a parallel target-detection mechanism. A `TargetSymbol` resolver would
   need its own "am I POSIX or Win32?" test at lowering, a second target oracle beside
   `prune_cc`/`native_target`.
2. **Cannot express the real split.** `{posix, win32}` is two-way; fs is (at least) three-way
   (linux-syscall / macos-libc / win-kernel32) and arch-varying. §1 and `file_stream.tks` prove the
   two-way model is factually wrong for the migration it is meant to serve.
3. **Assumes signatures coincide — they do not.** A per-target symbol *string* inside one
   declaration forces one signature across targets; POSIX `open` and Win32 `CreateFileW` share
   neither arity nor types nor error convention.
4. **YAGNI / minimal surface.** It adds a new `pub type`, its parse path, checker handling, and a
   codegen lowering — for zero capability the guard surface lacks. The smallest form that closes
   the gap is the one that adds nothing.
5. **Not native-first without re-deriving §2.4.** Any new form would still have to resolve through
   `prune_cc` to avoid a C-preprocessor dependency in the native endgame — i.e. it would reduce to
   the guard surface anyway, with extra text.

Passes-all-Laws test: the guard-on-`extern` mechanism satisfies minimal-surface, NAT-XL reuse,
native-first, and Teko-only simultaneously; the struct fails minimal-surface and NAT-XL reuse.
Law-first, the guard mechanism wins — no genuine tension remains, so no HALT.

---

## 4. Worked example — `open` for the RT-L3 fs half

The implementer copies this shape into `src/runtime/teko_rt.tks` (or the fs module). It mirrors the
shipping `file_stream.tks` precedent. W15: doc-comments appear only on `exp` declarations and are
never larger than the code; the guarded per-target leaves carry none.

```teko
#os("linux")
extern fn fs_open_at(dirfd: i32, path: u64, flags: i32, mode: i32): i32 = "__teko_never" from "c"

#os("macos")
extern fn fs_open_libc(path: u64, flags: i32, mode: i32): i32 = "open" from "System"

#os("windows")
extern fn fs_create_file_w(name: u64, access: u32, share: u32, sec: u64, disp: u32, flags: u32, tmpl: u64): u64 = "CreateFileW" from "kernel32"

#os("linux")
fn fs_open(path: str, flags: i64, mode: i64): i64 {
    teko::sys::syscall4(teko::sys::SYS_OPENAT, teko::sys::AT_FDCWD, cstr_addr(path), flags, mode)
}

#os("macos")
fn fs_open(path: str, flags: i64, mode: i64): i64 {
    fs_open_libc(cstr_addr(path) to u64, flags to i32, mode to i32) to i64
}

#os("windows")
fn fs_open(path: str, flags: i64, mode: i64): i64 {
    win_open_handle(path, flags, mode)
}

/**
 * open_file — open `path` for the runtime fs layer, returning a raw OS handle or a negative
 * error. The per-target foreign binding (linux syscall, macOS `open`, Windows `CreateFileW`) is
 * selected at lowering by the build target through the guarded `fs_open` family; this exported
 * entry is target-agnostic.
 *
 * @param path   the filesystem path to open
 * @param flags  the OS open flags for the active target
 * @param mode   the creation mode when the open creates
 * @return       a non-negative OS handle, or a negative OS error code
 * @since 0.3.1
 */
exp fn open_file(path: str, flags: i64, mode: i64): i64 {
    fs_open(path, flags, mode)
}
```

On a native linux build, `prune_cc` keeps only the two `#os("linux")` declarations plus
`open_file`; the macOS/Windows leaves never reach the checker. On a native Windows build, only the
Windows leg survives. `open_file` is written once and dispatches with zero runtime cost.

The linux `fs_open_at` extern is shown for shape-parity only; the linux body reaches the kernel via
`teko::sys::syscall4`, so that `extern` line may be omitted entirely (as `file_stream.tks` omits it
on linux). Prefer omission — YAGNI.

---

## 5. Honest-partial fallback (if the owner defers or insists on the struct)

Two fallbacks, in law-order preference:

- **F-A (recommended, zero-risk).** Approve D-TS1 as-is. RT-L3 proceeds today using the shipping
  guard mechanism; the `0061` blocker on "TargetSymbol ratification" is cleared with no compiler
  change. This is the minimal-surface, native-first, pass-all-Laws outcome.
- **F-B (owner still wants a first-class `TargetSymbol` form).** Then it must be defined as
  *sugar that desugars, pre-checker, into the guard family of §2* — never a parallel resolver. The
  honest scope is: a parser rewrite in `expand_macros_syntactic` (`src/build/project.tks:391`,
  already downstream of `prune_cc`... note: desugar must run BEFORE `prune_cc`, so it belongs in
  `assemble_sel`/parse, not macro-expand) that expands one annotated `extern` into N guarded
  `extern` decls. Even then it cannot model differing signatures (§3.3), so it is strictly weaker
  than writing the guards directly. This is documented as considered-and-not-recommended, not as a
  work item.

If the owner defers the whole decision, RT-L3's fs half stays BLOCKED exactly as `0061` records it
today; nothing regresses, and the design-ahead (this doc + the crumb edit) lets the implementer
resume in minutes on approval.

---

## 6. What this proposal does NOT change

- No change to `parser`, `checker`, `codegen`, `prune`, or `build` product code — the mechanism is
  already present and shipping.
- No change to NAT-XL (`0104`) — it is reused verbatim as the target oracle.
- No new `TEKO_*` env var, no new manifest key, no new CLI flag.
- `win32_compat.h` fs-half orphaning (`0061` §How-4) is unaffected: the guarded Windows `extern`s
  bind `CreateFileW`/`_chdir`/etc. directly, so the `#define chdir _chdir` shims lose their last
  caller exactly as `0061` intends.

---

## 7. Owner decision requested

Ratify **D-TS1** (§0): per-target foreign-symbol selection for RT-L3 is the target-guarded
`extern fn` surface; the `TargetSymbol` struct is withdrawn from `0061`. On approval, `0061` drops
its "`TargetSymbol` ratification" blocker and RT-L3's fs half is unblocked with no compiler change.
