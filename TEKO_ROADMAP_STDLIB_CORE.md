# TEKO — ROADMAP: standard-library core (`teko::io` · `teko::iter`)

> **Status:** DESIGN (no code yet) · **Created:** 2026-07-02 · **Updated:** 2026-07-02 (`try` REJECTED — see §1) · **Branch:** `main`
>
> Two **foundational** additions that multiply everything built on top of them. They are listed FIRST
> because they change the *shape* of the net/crypto/db/encoding/compress surfaces — landing them before
> those roadmaps are sliced to agents avoids a rewrite (streaming vs whole-buffer; lazy iteration vs eager
> allocation).
>
> Same work-distribution contract as the sibling roadmaps: each ▪ unit is one agent task with deps, files,
> and a verify bar. Governed by the Laws (M.0–M.5); SUPREME RULE + verify-both gate apply.

---

## 0. Why these two, first

- **`teko::io`** gives one contract for moving bytes — so `tcp`, `tls`, `file`, `gzip`, `crypto` all
  compose (`gzip(tls(tcp))`) instead of each inventing its own read/write. It also enables **streaming**
  (process data as it arrives) instead of the whole-buffer-in-memory shape the net/crypto units would
  otherwise bake in.
- **`teko::iter`** gives lazy `map`/`filter`/`fold`/… over one iteration protocol, on top of the closures
  (W10) and generics (S4) that already exist — expressiveness without intermediate allocations.

Error handling stays with **`match`**, and optional handling with **`?.`/`??`** (which already exist).
No error-propagation operator — see §1 for why.

---

## 1. Error handling — `match` stays; `try` is REJECTED

**RULING 2026-07-02 (user):** do **NOT** add a `try`/`?`-style error-propagation operator. Keep `match`
as the way to handle `T | error`, and keep the existing null-propagation operators `?.` and `??` for
optionals.

**Why (the reasoning):** a `try` operator presumes the value is exactly `T | error` and that the *only*
thing you ever want is "unwrap the `T` or propagate the `error`." But Teko's union values are richer than
that — a result can be `A | B | error`, or a `T | error` where the `T` itself still needs validation, or a
multi-arm variant where several non-error cases each need handling. Assuming "it's `T`, otherwise it's an
error" is **presumptuous** and papers over cases that genuinely require inspection. The explicit `match`
forces the author to confront every arm — it is **more verbose, and that verbosity IS the safety**
(M.1 fail-loud, no hidden control flow). So:

- `T | error` (and any wider union) → **`match`**, always:
  ```teko
  match teko::io::read_file(path) {
      str as s   => use(s)
      error as e => return e      // or handle e here — the author decides, explicitly
  }
  ```
- optional `T?` → the existing **`?.`** (safe navigation) and **`??`** (coalesce):
  ```teko
  let name = user?.profile?.name ?? "anonymous"
  ```

`?.`/`??` already exist and are the sanctioned ergonomic shortcut — but only for **absence** (`null`),
never for **error**, because absence is a single well-defined case whereas an error union may not be.

**Consequence for the sibling roadmaps:** their examples and dependency lines that mentioned `try` are
corrected to use `match`; none of them depend on a `try` unit anymore.

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
`Buf` is the same arena-backed byte region the net keystone defines (shared). Callers handle the
`… | error` results with `match`. **Verify:** `.tkt` over in-memory reader/writer + copy + buffering (all
pure Teko).

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
IO0 ── IO1      (IO1 wires net/compress/crypto as they arrive)
ITER0           (independent; composes with IO0 for line/byte iteration)
```

**Order:** `IO0` before slicing net/crypto/compress to agents (their stream types should implement
`Reader`/`Writer`). `ITER0` any time after closures. Both are independent of the net KEYSTONE.
(Error handling needs no unit — `match` + `?.`/`??` already exist.)

## 5. Open decisions (ratify with the sibling roadmaps in PR #80)

1. ~~`try` operator~~ — **DECIDED (rejected)**, §1. `match` stays; `?.`/`??` stay for optionals.
2. **`io` error model**: `read`/`write` return `u64 | error` with EOF as `0` (rec, Go-style) vs a distinct `Eof` error case.
3. **`Buf`**: confirm `teko::io` and `teko::net` share the ONE arena-backed byte region (from the net keystone), not two.
4. **`iter` protocol**: `next() -> T?` (rec — reuses the optional model) vs a `(bool, T)` pair or a `Done|Item<T>` variant.
