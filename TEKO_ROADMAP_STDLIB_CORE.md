# TEKO — ROADMAP: standard-library core (`teko::io` · `try` · `teko::iter`)

> **Status:** DESIGN (no code yet) · **Created:** 2026-07-02 · **Branch:** `feat/net-connectors` (off `chore/reboot`)
>
> Three **foundational** additions that multiply everything built on top of them. They are listed FIRST
> because they change the *shape* of the net/crypto/db/encoding/compress surfaces — landing them before
> those roadmaps are sliced to agents avoids a rewrite (streaming vs whole-buffer; `try` vs hand-written
> `match … return e`; lazy iteration vs eager allocation).
>
> Same work-distribution contract as the sibling roadmaps: each ▪ unit is one agent task with deps, files,
> and a verify bar. Governed by the Laws (M.0–M.5); SUPREME RULE + verify-both gate apply.

---

## 0. Why these three, first

- **`teko::io`** gives one contract for moving bytes — so `tcp`, `tls`, `file`, `gzip`, `crypto` all
  compose (`gzip(tls(tcp))`) instead of each inventing its own read/write. It also enables **streaming**
  (process data as it arrives) instead of the whole-buffer-in-memory shape the net/crypto units would
  otherwise bake in.
- **`try`** removes the `match … { X as v => v; error as e => return e }` boilerplate that `T | error`
  forces on every fallible call. It is **checker-desugar-only** (same class of change as `~`, `in`,
  `params`): zero codegen/VM work.
- **`teko::iter`** gives lazy `map`/`filter`/`fold`/… over one iteration protocol, on top of the closures
  (W10) and generics (S4) that already exist — expressiveness without intermediate allocations.

---

## 1. `try` — error-propagation operator

### ▪ CORE-TRY — the `try` operator
**Deps:** none. **Files:** `src/lexer/lexer.{tks,c}` (keyword), `src/parser/parse_expr.{tks,c}` (prefix
form), `src/checker/typer.{tks,c}` + `expr.{tks,c}` (desugar + validation), `.tkt` + a regression example.

**Semantics.** `try expr` where `expr : T | error`:
- evaluates `expr` once;
- if it is the **error** arm → `return` that error from the enclosing function;
- otherwise → the expression yields the **`T`** value.

**Rules (checker-enforced):**
- Valid ONLY inside a function whose return type is `… | error` (or `error?`); otherwise a clear
  compile error ("`try` requires the enclosing function to return `… | error`").
- `expr` must be `T | error`; the type of `try expr` is `T`.
- Scope `defer`s still fire on the error return ([[teko-defer-per-scope]]).
- **Desugar-only**: `try e` ⇒ `match e { T as v => v; error as x => return x }` — the exact code a
  developer writes today. NO codegen/VM change; the lexer gains `try` (contextual at expression start to
  avoid breaking any identifier named `try`), the parser a prefix node, the checker the desugar+validation.

**Example.**
```teko
fn fetch(host: str, req: []byte) -> []byte | error {
    let conn = try teko::net::tcp::connect(host)   // TcpStream, or propagate the error
    try conn.write(req)                             // propagate a write error
    try conn.read_all()                             // []byte, or propagate
}
```

**Verify:** `.tkt` — `try` on both arms (success threads the value; error returns it); a compile-error
test for `try` in a non-`error` function; VM == native; both twins byte-identical; gen-2 == gen-3.

**Open decision (ratify):** ship only `try` (pure propagate) now, leaving inline error handling to
`match` — vs also adding `try expr else |e| { … }` (map/handle). *(rec: `try` only; keep the surface
minimal, `match` already handles the rest.)*

---

## 2. `teko::io` — the byte-stream contract

### ▪ IO0 — core interfaces + errors
**Deps:** interfaces (W10b.IF ✅). **Files:** `src/io/stream.tks` (namespace `teko::io`).
```teko
type Reader = interface { fn read(self, into: Buf)  -> u64 | error }   // bytes read (0 = EOF)
type Writer = interface { fn write(self, from: Buf) -> u64 | error }   // bytes written
type Seeker = interface { fn seek(self, off: i64, whence: Whence) -> u64 | error }
type Closer = interface { fn close(self) -> error? }
```
Plus combinators (pure Teko): `read_all(r) -> Buf | error`, `read_exact`, `copy(dst, src) -> u64 | error`,
`BufReader`/`BufWriter` (buffering wrappers), `LimitReader`, a bytes/`str` in-memory `Reader`/`Writer`.
`Buf` is the same arena-backed byte region the net keystone defines (shared). **Verify:** `.tkt` over
in-memory reader/writer + copy + buffering (all pure Teko).

### ▪ IO1 — wire the existing surfaces to the interfaces
**Deps:** IO0. Make `teko::io` file/console fns (already extern-backed) and — as they land — net
`TcpStream`/`TlsStream`, compress streams, and crypto stream ciphers **implement `Reader`/`Writer`**.
This is the payoff: `teko::io::copy(gzip_writer, tcp_reader)` composes across roadmaps. **Verify:** native
example piping file → gzip → file via the generic `copy`.

> **Retro-impact:** the net/crypto/compress roadmaps should express their stream types in terms of these
> interfaces from the start. This unit's existence is why they can offer streaming, not just
> whole-buffer calls.

---

## 3. `teko::iter` — lazy iteration protocol

### ▪ ITER0 — the protocol + adapters
**Deps:** closures (W10 ✅), generics (S4 ✅), interfaces (W10b.IF ✅). **Files:** `src/iter/iter.tks`.
```teko
type Iterator<T> = interface { fn next(self) -> T? }   // null = exhausted
```
Lazy adapters as pure-Teko functions/closures (no intermediate allocation): `map`, `filter`, `take`,
`skip`, `zip`, `enumerate`, `chain`, `flat_map`; eager terminals: `fold`, `reduce`, `count`, `sum`,
`any`, `all`, `find`, `collect → []T`. Iterators over `array`/slices and (later) over `teko::collections`
and `teko::io` lines/bytes. **Verify:** `.tkt` — laziness (a `map` whose closure is never called past a
`take`), correctness of each adapter/terminal; VM == native.

> **Deliberately NOT added** (Laws): no `for`/`foreach` sugar — iteration is `loop { match it.next() {
> null => break; T as v => … } }` or the terminals above ([[teko-only-loop]]). `iter` is a library, not
> new control-flow syntax.

---

## 4. Dependency graph + order

```
CORE-TRY        (independent — land first, it cleans up all later code)
IO0 ── IO1      (IO1 wires net/compress/crypto as they arrive)
ITER0           (independent; composes with IO0 for line/byte iteration)
```

**Order:** `CORE-TRY` first (pure desugar, immediately simplifies the compiler corpus itself and every
subsequent unit). `IO0` before slicing net/crypto/compress to agents (their stream types should implement
`Reader`/`Writer`). `ITER0` any time after closures. All are independent of the net KEYSTONE.

## 5. Open decisions (ratify with the sibling roadmaps in PR #80)

1. **`try` scope**: pure-propagate only vs also `try … else |e|` mapping. *(rec: pure only)*
2. **`try` keyword**: reserved vs contextual-at-expression-start. *(rec: contextual, so `try` stays usable as an identifier elsewhere)*
3. **`io` error model**: `read`/`write` return `u64 | error` with EOF as `0` (rec, Go-style) vs a distinct `Eof` error case.
4. **`Buf`**: confirm `teko::io` and `teko::net` share the ONE arena-backed byte region (from the net keystone), not two.
5. **`iter` protocol**: `next() -> T?` (rec — reuses the optional model) vs a `(bool, T)` pair or a `Done|Item<T>` variant.
