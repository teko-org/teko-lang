---
seq: 0076
crumb-id: COL-Q8
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [COL-F0d]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:536-573"   # Q8 weak wrappers
  - "docs/design/plano-mestre-0.3.1-implementacao.md:236"                # M2 collections row COL-Q8
  - "docs/design/mudancas-superficie-0.3.1.md:1619-1623"                 # Doc-2/§16 wrap-refcount arena capability
---

# 0076 · COL-Q8 — Weak wrappers (`Weak<T>`)

> Weak wrappers: `Weak<T>` — a non-retaining reference over the FASE-0 weak-ref hook; `get` upgrades iff the
> wrap-refcount is still > 0. The cycle-breaker for the `wrapped` kind. Pure `.tks`, teaches nothing.

## Goal

Deliver `Weak<T>` — a non-retaining reference to a WRAPPED object. It does NOT increment the wrap-refcount, so
it never keeps its target alive; `get()` upgrades to a live (momentarily retained) reference iff the refcount is
still > 0, else the empty variant. It is the cycle-breaker for the wrap-refcount reclamation, meaningful ONLY
for the `wrapped` kind (a value has no identity; a plain `class` is region-drop, not refcount). It is **pure
`.tks` over the FASE-0 weak-ref hook (COL-F0d, item 5) seeded at SM-R1** — teaches nothing, `[dry]`. The
compiler core uses no wrapped objects, so this whole path is fixture-covered. Byte-preserving: additive leaf,
no core consumer.

## Where

- `src/collections/weak.tks` — NEW module. Pure `.tks` over the FASE-0 weak-ref hook (the `addr → count` root
  arena dict; the non-retaining read that does NOT bump the count, and the count>0 upgrade).
- No EXISTING fn modified; additive leaf. Base for `WeakMap`/`WeakSet` (COL-Q18, `0086`).

## How

1. **Create `src/collections/weak.tks`.** Copy the W15 API shape verbatim from the source doc
   (`colecoes-memoria-fila-implementacao-0.3.1.md:544-565`).
2. **`of(strong)`** records the raw (non-retaining) address of the target in the wrap-refcount table — it does
   NOT call `retain`, so constructing a `Weak` never keeps the target alive.
3. **`get()`** reads the wrap-refcount for `addr`: if count > 0 it upgrades (momentarily retains) and returns
   the live reference; if count == 0 (target freed) it returns `null`. This is the cycle-breaker: a `Weak` edge
   in a cycle lets the strong count reach zero and the object free.
4. **Restrict to `wrapped`.** `Weak<T>` is only well-formed for a `wrapped` `T` (the FASE-0 refcount kind); a
   `value`/plain-`class` `T` has no refcount table entry — the checker's kind classification (seeded) already
   distinguishes these, so `Weak<value>`/`Weak<class>` is a normal type error, not a new diagnostic.

```teko
/**
 * Weak<T> — a non-retaining reference to a WRAPPED object: it does NOT increment the wrap-refcount, so it
 * never keeps the target alive. `get` upgrades to a live (momentarily retained) reference iff the refcount is
 * still > 0, else the empty variant. The cycle-breaker for wrap-refcount; meaningful ONLY for `wrapped` (value
 * has no identity; a plain class is region-drop, not refcount).
 *
 * @since 0.3.1
 */
exp type Weak<T> = class {
    /** The raw (non-retaining) address of the target in the wrap-refcount table. */
    intern addr: u64

    /** Build a weak reference from a strong wrapped reference (does NOT retain). */
    pub static fn of(strong: T): Weak<T>
    /**
     * Upgrade to a strong reference if the target is still alive.
     *
     * @return the live reference (retained), or null if the target is gone
     */
    pub fn get(): T | null
}
```

## Rulings & laws

- **Teko-only:** `.tks` leaf; no C twin; the refcount table is the FASE-0 arena capability (Doc-2/§16
  `mudancas-superficie-0.3.1.md:1619-1623`), already seeded — no checker/codegen edit here.
- **W15 full Javadoc** on the type + both members; no inline `//`.
- **Weak semantics (record F0 item 5):** `of` does NOT retain; `get` upgrades iff count > 0 — a `Weak` never
  extends a lifetime and breaks reference cycles.
- **Teach-once (owner 2026-08-19):** no new surface — stands on the SM-R1 weak hook; `[dry]`, zero reseed.
- **Byte-preserving:** additive leaf, no core consumer.
- **Safety:** NEVER `teko test .` (run fixtures ISOLATED); build in a subshell with `ulimit -v 6815744`; commit
  the green step.

## Fixtures

The FASE-0 crumb (COL-F0d) already carries `weak_dead`/`weak_cycle_break` (the hook itself). This wrapper adds
only the alive path (the wrapper over a strongly-held target):

| fixture | asserts | expected |
|---|---|---|
| `weak_alive` | target strongly held: `Weak::of(x).get()` returns it (upgrade succeeds, count > 0) | `0` |

(No duplicate `weak_dead`/`weak_cycle_break` — those live in COL-F0d and cover the hook.)

## Gate

`[dry]` — compiles + `weak_alive` green + trivial fixpoint (byte-identical; additive leaf). "Green" = a weak ref
to a live wrapped target upgrades; the dead/cycle paths are covered by COL-F0d. **Reseed-class: none.**

## Deps

`COL-F0d` (the weak-ref hook + `wrapped` refcount, seeded at SM-R1).

## Done when

`src/collections/weak.tks` compiles, `weak_alive` passes, and a `[dry]` build is byte-identical — a
non-retaining `Weak<T>` whose `get` upgrades only while the wrap-refcount is alive.
