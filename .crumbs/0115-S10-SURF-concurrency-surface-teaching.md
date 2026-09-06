---
seq: 0115
crumb-id: S10-SURF
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: []
sources:
  - "docs/design/plano-s10-concorrencia-crumbs.md:44-61"            # the ordered crumb spine (S1/S2/A2/A3 front-end)
  - "docs/design/mudancas-superficie-0.3.1.md:669-1052"            # Doc-2 §10 concurrency surface (sealed)
  - "docs/design/arena-especificacao-unica-0.3.1.md:359-701"      # Doc-1 §7.6-7.9 arena-under-concurrency
  - "src/parser/parse_stmt.tks:78-141"                            # existing parse_spawn (partial)
  - "src/threads/threads.tks:1-68"                               # existing IChannelKind/Rx/Tx/Ctx type stubs
---

# 0115 · S10-SURF — §10 concurrency SURFACE teaching (spawn / chan / await→Intent accept)

> **RESEED-SET CHANGE — LOUD FLAG.** This is a MISSING M1 teaching. It teaches the compiler to ACCEPT
> the §10 concurrency surface (`spawn`, `chan<T>`, `await`→`Intent<T>`, the cross-boundary ref-guard).
> Per the M1 consolidation law it must be captured by the single teaching reseed **SM-R1 (`0030`)** —
> so `0030`'s captured surface ENLARGES and `0030` gains a dep on this crumb. See the INDEX reseed-
> budget note and the proposed M1 renumber.

## Goal

Doc-2 §10 AND Doc-1 §7 (the arena under isolation/parallelism) specify a concurrency surface that is
**entirely absent from the 112-crumb plan** — only the `S16-SYNC` substrate (`0056`) and the
`COL-Q19` BlockingCollection (`0087`) exist, plus a partial `parse_spawn` and skeleton `teko::threads`
type stubs in `src/`. The owner ruling is unambiguous: **§10 must be 100% before Doc-1** (Doc-2:1267,
Doc-1 §7). This crumb delivers the COMPILER-FACING half — the additive, **feature-gated-inert** surface
acceptance (s10 spine S1/S2/A2/A3 + the accept side of C4) authored in old spelling so today's seed
parses it, folding into SM-R1. The RUNTIME half (thread/channel primitives, `teko::threads` stdlib,
await lowering) is `S10-RT` (`0116`) in M2. This crumb is **byte-preserving / feature-gated-inert**:
the compiler NEVER instantiates the §10 surface (the leaf rule, s10:84-88), so the reseed is mechanical
byte-identity — but it ENLARGES what SM-R1 must parse/know.

## Where

- `src/parser/parse_stmt.tks:78-141` — `parse_spawn`/`is_spawn_head` exist (partial). Complete the
  `Spawn` AST node (s10 S1) and add contextual recognition so `spawn worker(...)` parses everywhere a
  statement/expr is legal, not only at statement head.
- `src/parser/ast.tks` — add `Intent<T>` as a recognized generic type name; add the `awaited` flag on
  `Binding`/`MultiBind` (s10 A2) for `await`-prefixed bindings.
- `src/parser/parse_expr.tks` — `await`-prefix expression parser (s10 A2): `await <call>` sets the
  `awaited` flag; contextual (no new keyword token — §10.1 "tudo contextual", Doc-2:675).
- `src/checker/` — s10 S2 + A3: the cross-boundary **ref-guard** (a `ref T` / `<ref T>` may NOT cross a
  spawn/channel boundary — Doc-1 §7.7 NAMES-not-pointers, §7.6/§10.6 UAF rule) and the `await` return-
  widening (`await f(): T` widens the binding to `Intent<T>`), plus arg-copy-at-boundary.
- `src/threads/threads.tks:1-68` — the `IChannelKind<T>`/`Rx<T>`/`Tx<T>`/`Ctx`/`Closed` type surface
  already exists as stubs; this crumb only makes the compiler ACCEPT `chan<T>::make<K>` + `svc<Rx<T>>`/
  `svc<Tx<T>>` DI-by-key resolution (s10 C4-accept). The bodies stay stubs until `S10-RT`.

## How

Order per the s10 spine (`plano-s10-concorrencia-crumbs.md:44-61`), front-end only:

1. **`Spawn` node (S1).** Finish the `parse_spawn` partial into a full `Spawn` AST node carrying the
   callee + copied args; contextual `spawn` (no keyword token). Inert: no lowering here (that is
   `S10-RT` S3).
2. **Spawn ref-guard + arg-copy (S2).** The checker rejects a `ref`/`<ref T>` argument crossing the
   spawn boundary and marks the args copy-at-boundary. Doc-1 §7.7: only NAMES cross a task boundary.

```teko
/**
 * IntentOf — the checker's return-widening for an `await`-prefixed call: `await f(): T` binds a value
 * of `Intent<T>` (the awaitable handle), never a bare `T`. `Intent<null>`/`Intent` (void) are the
 * degenerate forms. Model-independent of the D2 suspension choice (thread-per-await vs reactor).
 *
 * @param inner  the awaited call's declared return type `T`
 * @return       the widened binding type `Intent<T>`
 * @since 0.3.1
 */
exp fn intent_of(inner: checker::Type): checker::Type
```

3. **`Intent<T>` type + `await` parser (A1/A2).** Recognize `Intent<T>`/`Intent` as generic protected
   struct names and the `awaited` binding flag; `await` is a contextual prefix, not a keyword.
4. **`await` checker (A3).** Widen the awaited binding to `Intent<T>` via `intent_of`; apply the same
   ref-guard (an awaited result may not carry a boundary-crossing `ref`).
5. **`chan<T>` / `svc<Rx/Tx>` accept (C4-accept).** The compiler accepts `chan<T>::make<K>()` and the
   `svc<Rx<T>>(key)`/`svc<Tx<T>>(key)` DI-by-key resolution against the existing `threads.tks` types.
   Verify the residual generic gaps (s10:73-80): the method type-param `K: IChannelKind<T>` referencing
   the owner's `T`, and `Intent<T>._value: T | null` monomorphized over `T` — if mono does not
   substitute `T`, this arm grows into a checker fix (still inert, still folds R1).
6. **Keep §10 a LEAF (s10:84-88).** The compiler's own `src/` must NEVER adopt `spawn`/`chan`/`await`
   (the parallel-codegen axis uses a separate internal `fork_join`, not this surface). This keeps every
   reseed byte-identical/mechanical and keeps the D2 fork non-load-bearing for the compiler's build.

## Rulings & laws

- **Teko-only:** `src/parser/*.tks` + `src/checker/*.tks` + `src/threads/threads.tks`; additive-inert.
- **Comment convention (W15, owner 2026-08-19):** `/** */` only on `exp` decls; no `//` or `/* */`;
  never larger than the code (W15 reviewer).
- **Fork protocol (owner 2026-08-19):** the D2 fork (await suspension model — thread-per-await vs
  reactor) is a GENUINE owner fork already flagged by the s10 architect (`plano-s10-…:27-40`) with the
  recommendation **thread-per-await for v1**. It blocks ONLY `await` LOWERING (`S10-RT` A4) + the
  suspension half of `cancel`; ALL of THIS crumb's front-end (Intent type, parser, checker widening,
  ref-guard) is **model-independent and proceeds regardless**. This crumb does NOT HALT; `S10-RT`
  relays the D2 decision request.
- **W15 full Javadoc** on every new `exp` type/fn; flatten; no `//`.
- **Leaf discipline (owner):** §10 stays a stdlib leaf; the compiler never instantiates it → reseed is
  mechanical byte-identity, folded into SM-R1.
- **Safety:** NEVER `teko test .`; build under `ulimit -v 6815744`; `[dry]` — compile + scoped `.tkr` +
  trivial fixpoint (no emitted-byte change; the surface is inert until `S10-RT` uses it); its SEED is
  harvested once at SM-R1 (`0030`). Sweep `.tkt` after the AST/`Binding` widening.
- Rests on: Doc-2 §10 (669-1052) + Doc-1 §7.6-7.9 (359-701) + s10 spine S1/S2/A2/A3.

## Fixtures

Isolated `EXPECT_COMPILE_FAIL` rejects the fixpoint cannot exercise (the compiler never adopts §10):

| fixture | asserts | expected |
|---|---|---|
| `spawn_ref_across_boundary` | a `ref T` argument to `spawn` is rejected (Doc-1 §7.7) | `EXPECT_COMPILE_FAIL` |
| `await_widens_to_intent` | `var x = await f()` where `f(): i32` gives `x: Intent<i32>` (accept, not run) | `0` |
| `chan_make_svc_resolve` | `chan<i32>::make<MemChan<i32>>()` + `svc<Tx<i32>>(key)` type-checks | `0` |

## Gate

`[dry]` — compile + scoped `.tkr` + trivial fixpoint (the surface is feature-gated-inert; no emitted
byte changes). "Green" = the §10 surface parses/type-checks (accept + reject), the compiler emits no
different bytes, and the trivial fixpoint holds. Its reseed is harvested at **SM-R1 (`0030`)** — this
crumb is a dep of `0030`. Reseed-class: `(folds R1)`.

## Deps

`—` (additive; authored in old spelling). **Execution-order:** M1, BEFORE `SM-R1` (`0030`). `0030`
gains a dep on `S10-SURF` (targeted edit, flagged in the gap report + INDEX). The append seq `0115` is
the filename ordinal only; the topological position is fixed by the `0030`←`S10-SURF` dep edge, NOT by
the seq number. Proposed formal renumber: insert as a new M1 row before `0030` and shift `0030`-`0114`
by +1 (see gap report).

## Done when

The compiler accepts and type-checks the full §10 surface (`spawn` + ref-guard, `await`→`Intent<T>`,
`chan<T>::make<K>` + `svc<Rx/Tx>` by key), rejects a boundary-crossing `ref`, emits byte-identical
output (leaf-inert), and its surface is captured by the SM-R1 reseed.
