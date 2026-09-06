---
seq: 0121
crumb-id: ERR-FACTORY
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [SM-G4]
sources:
  - "origin/cargo/0.3.1.0-error-factory"                             # recovery branch with error::new + error::join implementation
  - "src/checker/scope.tks"                                          # builtin_fn additions (new, join)
  - "src/codegen/codegen.tks"                                        # codegen for tk_error_make
---

# 0121 · ERR-FACTORY — `error::new(msg)` and `error::join(a,b)` factory builtins

> Implement error-value factory functions: `error::new(msg: str): error` (create error from message)
> and `error::join(a: error, b: error): error` (combine two errors). Drain the undrained work on
> `origin/cargo/0.3.1.0-error-factory` (adds `new`/`join` builtin branches to `scope.tks` and codegen
> emission of `tk_error_make` in `codegen.tks`).
>
> **Recovery:** Work is undrained; source branch is `origin/cargo/0.3.1.0-error-factory`.

## Goal

`error` is a first-class type (SM-G4 `0009`). This crumb adds its surface-level factory functions:
`error::new(msg: str): error` (create an error value from a human-readable message) and
`error::join(a: error, b: error): error` (combine two error values into one). Both are builtins
(not stdlib) because they map to runtime `tk_error_make` which surfaces the error encoding. This
drives a **fixpoint-rebuild** (codegen change). Work is awaiting drain from the prepared branch.

## Where

- `src/checker/scope.tks` — add `builtin_fn("error::new", …)` and `builtin_fn("error::join", …)`
  with their signatures and lowering call-sites to `tk_error_make`.
- `src/codegen/codegen.tks` — emit the `tk_error_make` call (runtime encoding of error values,
  `teko_rt.h` signature). Both functions compile to a single native/C call + result binding.

## How

1. **`error::new(msg: str): error`** — builtin that accepts a message string and returns an error
   value. Lowers to: `tk_error_make(msg)` → error encoding in the runtime.
2. **`error::join(a: error, b: error): error`** — builtin that folds two errors into one (semantic:
   combine their messages or chain them per runtime semantics). Lowers to: `tk_error_make(combined_msg)`.
   The combination strategy (concatenation, nesting, etc.) is defined by the runtime implementation.
3. **Builtin signatures** — add to `scope.tks`'s `builtin_fn` table with correct type signatures
   (no generics; `error` is a concrete type).
4. **Codegen** — route both to `tk_error_make` emission in `codegen.tks` as a call operation.

```teko
/**
 * error::new — construct an error value from a message string.
 *
 * @param msg  the error message (human-readable)
 * @return     an error value (opaque encoding of msg + metadata)
 * @since 0.3.1
 */
exp fn new(msg: str): error

/**
 * error::join — combine two error values into a single error.
 *
 * @param a  the first error
 * @param b  the second error
 * @return   a new error representing both a and b (chained or concatenated per runtime semantics)
 * @since 0.3.1
 */
exp fn join(a: error, b: error): error
```

## Rulings & laws

- **Teko-only:** `src/checker/*.tks` + `src/codegen/*.tks`; no C twin (runtime `tk_error_make` is
  maintained-C in `teko_rt.c`).
- **Comment convention (W15, owner 2026-08-19):** `/** */` only on `exp` decls; no `//` or `/* */`.
- **Builtin scope:** both are exported `exp` (user-facing error factories); route through builtin
  registry in `scope.tks`.
- **No generics** — `error` is concrete, not a type variable; keep signatures simple.
- **Fork protocol (owner 2026-08-19):** error encoding semantics (what `tk_error_make` does in the
  runtime) are ratified (SM-G4); no fork here — just expose the surface.
- **W15 full Javadoc** on every `exp` decl; flatten; no `//`.
- **Safety:** NEVER `teko test .`; build under `ulimit -v 6815744`; `[fixpoint]` `gen2==gen3`; sweep
  `.tkt` if any signature changes.
- Rests on: SM-G4 (`0009`, error type ratified) + the prepared branch `origin/cargo/0.3.1.0-error-factory`.

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `error_factory_new` | `error::new("test")` creates an error value | `0` |
| `error_factory_join` | `error::join(a, b)` combines two errors | `0` |
| `error_in_try_return` | `try { … } \| error` catches and chains errors via `error::join` | `0` |

## Gate

`[fixpoint]` — `gen2==gen3` byte-identity (codegen change; the emitted error-making code must stabilize).
"Green" = both `error::new` and `error::join` compile, lower to `tk_error_make` correctly, and the
rebuild is byte-identical. Reseed-class: `fixpoint-rebuild`.

## Deps

`SM-G4` (`0009`, error type + runtime encoding). No other crumbs blocked by this.

## Done when

`error::new(msg: str): error` and `error::join(a: error, b: error): error` are callable builtins,
compile to `tk_error_make` invocations, and the fixpoint is byte-identical.
