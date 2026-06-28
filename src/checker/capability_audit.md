# Capability Audit — C9.2

**Scope:** `extern fn` FFI surface, `exp` export surface, host namespace declarations,
`ptr`/`uptr` marshalling primitives, and ABI safety rules as implemented in the checker
and runtime.  Conducted 2026-06-27 against branch `chore/reboot`.

---

## 1. `extern fn` scope (typer.c `extern_type_ok` / `teko_rt_type_ok`)

### How extern is gated

`tk_type_function` (typer.c:345) enforces a two-tier rule at check time:

* **Generic extern** (`from "<anything other than teko_rt">`):
  params and return must each pass `extern_type_ok` — only primitives, `byte`, `ptr`,
  `uptr`, and opaque `extern type` handles are allowed.  Aggregate Teko types (struct,
  enum, variant, optional, slice, str) are **rejected** at the checker level (typer.c:351).

* **`teko_rt` bypass** (`from "teko_rt"`):
  params and return must pass `teko_rt_type_ok`, which additionally permits `str`,
  `[]T`, `T | U` (variant), and `T?` (optional) — the internal ABI shapes the runtime
  understands.  This bypass is hard-coded to the exact string `"teko_rt"` (typer.c:347).

### Risks / findings

| # | Finding | Severity |
|---|---------|----------|
| 1a | **No allowlist for symbol names.** Any Teko program that can write an `extern fn` block can name *any* C symbol — `system`, `dlopen`, `mmap`, etc. — and the checker will validate it. The only constraint is the type signature. | KNOWN / DESIGN |
| 1b | **`teko_rt` bypass is a single-string gate, not a namespace check.** A library named `"teko_rt"` by a third-party package would gain the same relaxed ABI rules.  The `from` clause is currently informational only (not wired to the link step) so this is moot today, but it should be documented as a future risk if `from` drives linking. | LOW |
| 1c | **No varargs guard.** The parser and checker have no concept of variadic C functions.  A Teko `extern fn` declared as a fixed-arity function can silently bind to a C varargs function (e.g. `printf`).  Calling it from Teko with the declared arity produces a well-formed C call, but the C ABI for varargs on some platforms (notably x86-64 SysV with XMM registers in `al`) may produce UB.  **No call site guard exists.** | MEDIUM |
| 1d | **`ptr` is untyped.** The `ptr` type is an opaque `void *`; the checker knows nothing about what it points to.  Any `ptr` value can be passed to any `extern fn` accepting `ptr` without a cast.  This is by design (opaque handle) but means type confusion across `ptr`-typed FFI functions is invisible to the checker. | KNOWN / DESIGN |

### Confirmed safe

* Aggregate-by-value FFI is blocked: passing a Teko struct/enum/variant directly to a
  non-`teko_rt` extern is a type error at check time.
* `void` is correctly rejected for parameter position and permitted only as a return type.

---

## 2. `exp fn` surface (emit/header.c, codegen.c)

### How `exp` works

`TK_VIS_EXP` is the highest visibility level (parser/ast.h:184).  At codegen time
(`emit_function_sig`, codegen.c:2958) there is **no special treatment** — an `exp` fn
emits the same mangled C symbol as any other function.  The header builder (emit/header.c)
collects `exp` items into the `.tkh` for downstream packages.

There is **no** `__attribute__((visibility("default")))` or `__declspec(dllexport)` added
by the current codegen.

### Risks / findings

| # | Finding | Severity |
|---|---------|----------|
| 2a | **`exp fn` does not currently surface as a C-visible symbol beyond the `.tkh`.** For a binary build the mangled symbol is internal to the final executable.  For future shared-library or plugin builds this would matter, but it is a non-issue today. | INFO |
| 2b | **No checked constraint that `exp fn` parameters/returns are FFI-safe types.** An `exp fn` can expose Teko-internal types (`str`, slices, variants) across the `.tkh` boundary.  A consumer package that reads the `.tkh` gets the full Teko types, which is correct; but if the intent ever evolves toward cross-language C-ABI exports, `exp` would need the same `extern_type_ok` gate that `extern fn` applies to imports.  No such gate exists today. | LOW (future risk) |
| 2c | **Memory safety of `exp fn` bodies:** since an `exp fn` is ordinary Teko, the type system applies in full. An external C caller that invokes the mangled symbol directly would bypass all Teko type checks, but that is a C-interop concern outside Teko's control. | KNOWN / DESIGN |

---

## 3. Host namespace surface (`teko::env`, `teko::io`, `teko::fs`, `teko::process`)

### Declarations reviewed

| Namespace | Functions |
|-----------|-----------|
| `teko::env` | `args`, `var`, `cwd`, `set_var`, `chdir` |
| `teko::io`  | `read_file`, `write_file`, `write_file_bytes`, `write`, `ewrite`, `print`, `println`, `eprint`, `eprintln` |
| `teko::fs`  | `list_dir`, `mkdir` |
| `teko::process` | `run` |

### Findings

| # | Finding | Severity |
|---|---------|----------|
| 3a | **`teko::env::set_var` / `chdir`** mutate global process state (environment and working directory).  Any Teko code that imports `teko::env` can change environment variables or the CWD for the whole process, including later calls to `teko::process::run`.  No capability gating or sandboxing exists. | KNOWN / DESIGN |
| 3b | **`teko::process::run`** executes an arbitrary command.  `args[0]` is passed directly to `execvp` with no sanitization, path restriction, or allowlist.  This is a full-capability ambient OS call: any Teko program that can reach `teko::process::run` can run any executable visible on `PATH`.  **This is a known, intentional design choice; the capability is documented here as required by C9.2.** | KNOWN / HIGH CAPABILITY |
| 3c | **`teko::io::read_file` / `write_file` / `write_file_bytes`** accept an arbitrary path string.  There is no path restriction (e.g. no chroot or path prefix guard).  A Teko program can read or overwrite any file the process has permission to access. | KNOWN / DESIGN |
| 3d | **`teko::fs::mkdir`** creates directories at any path.  Mode is hardcoded 0755; no guard. | KNOWN / DESIGN |
| 3e | All four host namespaces are `all-or-nothing`: importing any function from `teko::process` makes `run` available; there is no per-symbol capability gate.  Future work could split `teko::process::run` into a separate `teko::process::unsafe_run` or gate it behind a build-level flag. | RECOMMENDATION |

---

## 4. `ptr`/`uptr` marshalling (`teko::mem`)

### Functions

| Teko name | C impl | Behaviour |
|-----------|--------|-----------|
| `as_ptr(str) -> ptr` | `tk_as_ptr` | Returns `s.ptr` as `void *` — a **borrow** of the str's buffer. Not NUL-terminated. |
| `as_cstr(str) -> ptr` | `tk_cstr_dup` | `malloc`s a NUL-terminated copy.  Caller must ensure the `ptr` outlives its use; Teko has no destructor to free it. |
| `str_from_cstr(ptr) -> str` | `tk_str_from_cstr` | Copies a C string into a fresh Teko str.  NULL-safe (yields empty str). |
| `bytes_from_ptr(ptr, u64) -> []byte` | `tk_bytes_from_ptr` | Copies `n` bytes from a foreign pointer into a fresh slice. NULL-safe. |

### Findings

| # | Finding | Severity |
|---|---------|----------|
| 4a | **`as_ptr` borrows without a lifetime.** The returned `ptr` is valid only while the original `str` value is alive.  Teko has no borrow checker or lifetime tracking.  A Teko program can retain the `ptr` after the `str` goes out of scope (e.g., a local `str` in a function that returns a `ptr`) and pass it to a later `extern fn`, producing a use-after-free.  The checker cannot detect this. | HIGH / UNSOUND |
| 4b | **`as_cstr` leaks.** The `malloc`'d NUL-terminated copy is never freed.  For short-lived use (passing to an `extern fn` that does not store the pointer) this is benign under the arena/leak-tolerant model (M.5), but callers should not rely on the buffer being freed. | KNOWN / LOW (M.5 model) |
| 4c | **`bytes_from_ptr` trusts the caller-supplied length.** If `n` exceeds the actual allocation behind `p`, a buffer over-read occurs at the `memcpy` in `tk_bytes_from_ptr`.  There is no bounds check possible at the C level given an opaque `ptr`. | HIGH / UNSAFE BY CONTRACT |
| 4d | **`str_from_cstr` trusts the pointer.** A non-NULL `ptr` that is not a valid NUL-terminated string will cause `strlen` to scan past the end of the allocation (undefined behaviour). | HIGH / UNSAFE BY CONTRACT |
| 4e | **No taint tracking.** A `ptr` value obtained via `as_ptr` is type-identical to a `ptr` obtained via any `extern fn` that returns a `ptr` (e.g., `malloc`).  The checker cannot distinguish "borrowed Teko str pointer" from "heap pointer" from "NULL" at type-check time. | KNOWN / DESIGN |

### Confirmed safe

* `bytes_from_ptr` and `str_from_cstr` make fresh copies — the resulting Teko slice/str
  does not alias the foreign pointer, so mutations on the Teko side do not affect the
  foreign buffer.
* `NULL` is handled gracefully by both `str_from_cstr` and `bytes_from_ptr`.

---

## 5. FFI ABI rules — summary of gaps

| Gap | Status |
|-----|--------|
| Varargs: no guard against binding a C varargs fn via a fixed-arity `extern fn` | UNGUARDED (finding 1c) |
| Wrong-size `ptr`: a `ptr` from `as_ptr(str)` can be passed to an extern expecting a pointer to a different layout | UNGUARDED BY DESIGN (opaque ptr) |
| Lifetime of `as_ptr` result | UNSOUND (finding 4a) |
| Caller-supplied length in `bytes_from_ptr` | UNSAFE BY CONTRACT (finding 4c) |
| `exp fn` does not gate FFI-safe types for cross-language export | LOW / FUTURE (finding 2b) |

---

## 6. Summary of recommended follow-up items

1. **Document `as_ptr` lifetime contract prominently** in `teko::mem` docs and in a
   compiler warning when `as_ptr` result is stored into a binding that outlives the source
   `str`.  (Full enforcement requires a borrow checker — deferred beyond current roadmap.)

2. **Add a varargs guard** to the `extern fn` checker: parse a `...` sentinel in the
   `extern fn` parameter list (or reject any symbol known to be variadic).  Until then,
   binding a C varargs function via a fixed-arity `extern fn` silently compiles.

3. **Consider splitting `teko::process::run`** into a clearly-named "unsafe" surface
   (e.g. a build-level `[capabilities] process_exec = true` flag in `.tkp`) so library
   authors cannot accidentally expose arbitrary command execution to their consumers.

4. **`teko_rt` bypass label risk**: if `from "<lib>"` ever drives the link step, the
   `teko_rt` string should be a reserved keyword or built-in, not a user-writable string
   that grants relaxed ABI rules.

---

*Audited files:* `src/checker/scope.c`, `src/checker/resolve.c`, `src/checker/typer.c`,
`src/emit/header.c`, `src/codegen/codegen.c`, `src/runtime/teko_rt.c`,
`src/env/env.tks`, `src/io/io.tks`, `src/fs/fs.tks`, `src/process/process.tks`.

*No code was changed as part of this audit (read-only crumb as specified).*
