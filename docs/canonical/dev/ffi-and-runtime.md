# FFI, the C Boundary, and `teko_rt`

Teko keeps exactly one piece of hand-maintained C in the tree by design: the execution runtime
linked into every generated program, `src/runtime/teko_rt.{c,h}` (with its `assert.{c,h}`
sibling). This is the FFI seam for native binaries and the intentional exception to
"Teko-only" — every other compiler source file is Teko.

## Why the runtime stays C

`teko_rt` provides the primitives that must exist before any Teko code can run at all —
allocation (`tk_alloc`, `tk_region_new`/`drop`), panics, arithmetic guards (division-by-zero,
overflow in debug), string/number formatting, and platform syscalls (I/O, process, env). It is
allowed to **grow** — new primitives get added to it — under two narrow conditions: (a) the
native backend needs a call shape the runtime doesn't yet offer, or (b) it is a documented
escape hatch for a known bug that would otherwise corrupt state silently and that the native
path cannot yet fix at its proper layer. Growth is always logged with a stated reason at the
site; the intent is for `teko_rt`'s Teko-facing surface (`teko::runtime`) to eventually absorb
what lands here, not for the runtime to become an unaudited dumping ground.

## `extern` — calling out to foreign (C) code

```teko
extern fn host_write(fd: i32, buf: ptr, len: u64): i64 = "write" from "libc"
```

- **Form:** `extern fn name(params): ret = "symbol" from "lib"` — a bodyless `fn`, reusing
  the ordinary function grammar rather than a dedicated block construct. The Teko-facing name
  is decoupled from the C symbol, so the surface stays idiomatic.
- **`from` is optional.** Omitted, the symbol is expected in the implicit host libc — the
  legislated default for AOT-on-host, not an inference. `[extern] freestanding = true` in the
  manifest drops the implicit libc and makes `from` mandatory everywhere.
- **Marshalling is restricted and explicit.** Parameters/returns are limited to the native
  primitives plus `ptr`/`uptr`/`void`. Aggregates (strings, byte slices, structs) never
  auto-convert across the boundary — crossing requires an explicit call
  (`teko::mem::as_ptr(str|[]byte): ptr`, `.len`) so the unsafe seam is always visible at the
  call site, never hidden behind a convenience wrapper.
- **`extern type Name`** is a named, fully opaque foreign handle (`FILE*`, `sqlite3*`, a socket)
  — one pointer-sized word, non-null by default (a maybe-null handle is the ordinary `Name?`),
  with no dereference, field access, arithmetic, or construction available in Teko code. Its
  entire value is that the checker refuses to let a `File` handle and a `Dir` handle mix, even
  though both are "just a pointer" underneath.
- **Variadic `extern` is forbidden** — an unbounded ABI is treated as inherently unsafe; a
  variadic C function is wrapped at a fixed arity instead.
- **Library resolution lives in the manifest, not the source.** `.tks` carries only a logical
  handle (`from "ssl"`); the `.tkp` `[extern.libs]` table maps that handle to the concrete
  link spec per platform, so source stays portable while platform detail stays where it
  belongs.
- **Cross-platform naming differences resolve in the manifest** (a per-OS symbol-name
  override); **shape differences** (the same operation exposed as a genuinely different API
  per OS — POSIX `write` vs. Win32 `WriteFile`) need real per-OS declarations, gated by the
  general `#os("unix")` / `#os("windows")` attribute (not an `extern`-only keyword, because the
  wrapping `exp fn` around it differs too).

## Fail-loud inside, fail-soft at the boundary

Inside safe Teko code, an invariant violation panics — loud, immediate, non-recoverable by
design (M.1: never corrupt silently). At the FFI boundary, the posture inverts: a C-side error
(an errno-style failure, a null return) is lifted into a Teko `error` value in the wrapper that
surrounds the raw `extern`, not the raw `extern` itself — the wrapper is where "the outside
world can fail and that's fine" gets translated into "callers `match` on it like anything
else." This is the same "simple core, contained edge" posture the memory model follows: the
`extern` declaration mirrors the raw ABI 1:1 (nothing is hidden), and the surrounding `exp fn`
is where the boundary becomes an ordinary Teko contract.

## Errors as values, factory-first

A recoverable failure is the native, lowercase `error` case of a variant (`T | error`), never
an exception. The idiomatic way to produce one is the `error` factory rather than a bare
struct literal:

```teko
error::new(message)                                   // a plain error
error::new_pos(message, line, col, file)               // an error with source position
error::join(left, right)                               // two errors flattened into one message
```

The `error { message = ... }` struct-literal form still compiles (it is not removed — a
developer is never forced through the factory), but the factory is the recommended convention:
it hides the `error` struct's layout behind a call, which is what lets the compiler evolve that
layout (richer position info, structured joins) without touching call sites that already went
through the factory. `join` today flattens two errors into one message textually — it does not
build a wrapped/unwrappable chain — an intentional, low-cost deferral rather than a closed
door: because the factory already hides the layout, growing `join` into a true chain later
costs nothing at existing call sites.
