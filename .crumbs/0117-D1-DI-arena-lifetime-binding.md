---
seq: 0117
crumb-id: D1-DI
milestone: M5
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [S10-RT, RM-C16]
sources:
  - "docs/design/arena-especificacao-unica-0.3.1.md:728-768"       # Doc-1 §8 DI = arena lifetimes + per-thread sub-root
  - "docs/design/mudancas-superficie-0.3.1.md:252-300"            # Doc-2 §7 DI service/svc (Part A banked)
  - "docs/design/estado-doc2-campanha-limpeza-0.3.1.md:44"        # svc_scope_expr seam frozen (Part B left to Doc-1 §8)
---

# 0117 · D1-DI — DI Part B: bind service lifetimes to arena regions (fill the `svc_scope_expr` seam)

> Fill the frozen `svc_scope_expr(lifetime, regions)` seam (banked at DI Part A, `0a246dfe`) with the
> actual arena binding: `singleton`→thread-root once-guard, `scoped`→arena-ancestry walk,
> `transient`→current region; per-thread sub-root resolution (Doc-1 §8).

## Goal

Doc-2 §7 DI **Part A** is banked (SM-G6 `0012` taught the `service`/`svc` taint + `IService` gate; the
`svc_scope_expr(lifetime, regions)` costura was FROZEN at the program-root, `estado-doc2:44`). **Part
B** — the substance that is arena (Doc-1 §8) — was explicitly LEFT for Doc-1. This crumb realizes it:
`svc<T>(key)` resolves inline at comp-time to per-lifetime code binding the service instance to the
right arena region (singleton→root once-guard, scoped→ancestry walk to the call-site scope-arena,
transient→current region), and under threads each lane resolves against its OWN sub-root (Doc-1 §8: DI
is the thread's, not the program's — no cross-thread lock), with the channel-transport service as the
F2 program-root exception (Doc-1 §7.8). It belongs in **M5** because its substance is arena and it
depends on the per-thread regions `S10-RT` (`0116`) establishes and the native fixpoint `RM-C16`.

## Where

- `src/di/` (or wherever `svc_scope_expr` lives, per DI Part A) — replace the frozen program-root stub
  with the three-lifetime lowering + the per-thread sub-root walk.
- `src/checker/` — keep the Part A escape rule (a service value is never stored/passed/returned in user
  code, backend-exempt-but-arena-bounded, Doc-1 §8) and add the channel-transport F2 exception.
- `src/lir/lower.tks` / `src/codegen/codegen.tks` — the per-lifetime inline expansion (no runtime
  service-locator; the constant key + type decide everything at comp-time, Doc-1 §7.8).

## How

1. **singleton** → a once-guarded slot in the lane's ROOT region (thread-root under threads, Doc-1 §8);
   one instance per lane, no cross-thread share/lock.
2. **scoped** → walk the arena ancestry from the call-site to the enclosing scope-arena; resolve the
   instance there (the point Part A left open).
3. **transient** → fresh alloc in the current region each resolution.
4. **channel-transport exception** → `make<K: service singleton & IChannelKind<T>>` binds K in the F2
   PROGRAM root under the channel's constant key (Doc-1 §7.8), not the thread root — because it is the
   inter-task primitive. Verify the `S10-RT` channel layer resolves `svc<Tx/Rx>("key")` against F2.

```teko
/**
 * svc_bind — the comp-time binding of a service resolution to its arena region, by lifetime. Inlined
 * at the `svc<T>(key)` call-site; NO runtime service-locator. Under threads, `singleton`/`scoped`
 * resolve against the lane's sub-root (Doc-1 §8), the channel transport against the F2 program root.
 *
 * @param lifetime  transient | scoped | singleton (the `ServiceLifetime` banked at Part A)
 * @param key       the optional constant key (by-type when null; by-name when present)
 * @param regions   the live arena-region tree at the call-site
 * @return          the region + slot the instance binds to
 * @since 0.3.1
 */
exp fn svc_bind(lifetime: ServiceLifetime, key: str | null, regions: RegionTree): SvcSlot
```

## Rulings & laws

- **Teko-only:** `.tks`; no C twin.
- **Comment convention (W15, owner 2026-08-19):** `/** */` only on `exp` decls; no `//` or `/* */`.
- **Fork protocol (owner 2026-08-19):** the three-lifetime→region mapping and per-thread sub-root are
  RATIFIED (Doc-1 §8 table, owner 2026-08-10) — no undecided fork; do NOT HALT.
- **W15 full Javadoc** on every `exp` decl; flatten; no `//`.
- **Escape rule (Part A, preserved):** a service value is never stored/passed/returned in user code;
  backend-exempt but arena-bounded (Doc-1 §8).
- **Safety:** NEVER `teko test .`; build under `ulimit -v 6815744`; `[fixpoint]` `gen2==gen3`; sweep
  `.tkt` after any signature change.
- Rests on: Doc-1 §8 (728-768) + Doc-2 §7 + the frozen seam (`estado-doc2:44`).

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `svc_scoped_ancestry` | a `scoped` service resolves to the call-site scope-arena instance, dropped with it | `0` |
| `svc_singleton_per_thread` | two lanes each get their OWN `singleton` (no share) | `0` |
| `svc_channel_f2_root` | a channel transport service lives in the F2 program root across tasks | `0` |
| `svc_escape_reject` | `return svc<S>()` in user code is rejected (Part A rule preserved) | `EXPECT_COMPILE_FAIL` |

## Gate

`[fixpoint]` — `gen2==gen3` byte-identity. "Green" = the three lifetimes bind to the correct region,
per-thread sub-root resolution holds, the channel F2 exception holds, and the rebuild is byte-identical.
Reseed-class: `fixpoint-rebuild`.

## Deps

`S10-RT` (`0116`, per-thread regions) + `RM-C16` (`0106`, native fixpoint). Part A (`SM-G6` `0012`) is
banked.

## Done when

`svc<T>(key)` binds `singleton`/`scoped`/`transient` to the correct arena region, resolves per-thread
against the lane sub-root, keeps the channel transport in F2, preserves the escape rule, and rebuilds
byte-identical.
