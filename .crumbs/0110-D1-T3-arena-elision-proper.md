---
seq: 0110
crumb-id: D1-T3
milestone: M5
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [SM-A4, RM-C16]
sources:
  - "docs/design/arena-especificacao-unica-0.3.1.md:166-197"           # §4 elision — need == 0
  - "docs/design/plano-mestre-0.3.1-implementacao.md:303"              # M5 D1-T3 row
  - "src/lir/lower.tks:1637"                                           # the bracket_depth skip pattern
---

# 0110 · D1-T3 — arena elision proper (no region where nothing allocates)

> Realize arena elision fully (SM-A4 planted the `scope_touches_arena` guard): a scope that provably
> allocates nothing opens NO region — skip `new`/`enter`/`leave`/`drop` entirely (forwarders, const
> returns, comparison arms, guard blocks). The `need == 0` limit case of the pre-sizing floor.

## Goal

Doc-1 Idea 2, made complete: a predicate `scope_touches_arena(body)` is TRUE iff the scope contains at
least one routable allocation site (`push`-era sites are gone; now struct-init / array-lit / str-concat /
box / `tk_alloc`). When FALSE, `open_native_region`/`open_frame_region` SKIP `region_new`/`enter` (and the
paired `drop`) — exactly as the existing `bracket_depth > 0` skip already does (`lower.tks:1637`), a proven
pattern in the same function. SM-A4 (M1) planted the guard; this crumb realizes the physical skip on the
native route: a leaf scope that allocates nothing (a forwarder, a `const` return, a comparison arm, a guard
block) pays ZERO region machinery — no header, no `new`/`enter`/`leave`/`drop`. It is the `need == 0` limit
case of D1-T1's floor (no need → no region). The rule is CONSERVATIVE: doubt → do NOT elide (an
undetected routable site routes its allocation to the OUTER region, leak-safe, NEVER a UAF — because
elision only removes an EMPTY region wrapper, never redirects an existing allocation). It is a **byte-mover**
on the native route driving a **fixpoint-rebuild**.

## Where

- `src/lir/lower.tks:1637` — the `bracket_depth > 0` region-skip (the proven pattern) — GENERALIZE: also
  skip when `scope_touches_arena(body)` is false, keeping the region stack balanced by the same symmetry
  (skip `new`+`enter` AND the paired `leave`+`drop`).
- `src/lir/lower.tks` `scope_touches_arena` (SM-A4's guard) — confirm the routable-site set is current
  (post-COL-F2: struct-init / array-lit / str-concat / box / `tk_alloc`; the removed `push` is gone).
- `src/lir/lower.tks` `open_native_region` / `open_frame_region` — consult the guard and skip the region
  wrapper when false.

## How

1. **Confirm the routable-site set** (`scope_touches_arena`, SM-A4): a site is routable if it allocates in
   a region — struct-init of known layout, array literal, str-concat, box site, `tk_alloc`. The removed
   `push` family is NOT in the set (COL-F2 deleted it). Doubt → treat as routable (conservative).
2. **Skip the region wrapper when false** (`lower.tks:1637` generalization): `open_native_region`/
   `open_frame_region` skip `region_new`+`enter` when the guard is false; the paired `leave`+`drop` skip by
   the same symmetry (the stack stays balanced). This mirrors the `bracket_depth` skip exactly.

```teko
/**
 * scope_touches_arena — TRUE iff the scope contains at least one routable allocation site (known-layout
 * struct-init, array literal, str-concat, box, tk_alloc — the push family is gone post-COL-F2). When
 * FALSE, the region wrapper (new/enter/leave/drop) is elided — the need == 0 limit case of the pre-sizing
 * floor. CONSERVATIVE: doubt → returns TRUE (an undetected site routes to the outer region, leak-safe,
 * never a UAF, because elision only removes an EMPTY wrapper — it never redirects an existing allocation).
 *
 * @param body  the scope's statement block
 * @return true when the scope allocates (open a region); false when it is provably allocation-free (elide)
 * @since 0.3.1
 */
fn scope_touches_arena(body: []checker::TStatement): bool
```

3. **Compose with D1-T2 DPS**: an allocation-free scope conveys nothing, so the DPS conveyance brackets
   never fire inside it — clean composition.
4. **Conservative direction**: elision only removes an EMPTY wrapper; it NEVER redirects an existing
   allocation earlier or drops a region with a live alias (the arena-por-escopo failure) — doubt → keep the
   region.
5. **Fixpoint**: the guard is a deterministic AST predicate; the native object reproduces; `gen2==gen3`.

## Rulings & laws

- **Teko-only:** `src/lir/*.tks`; no C twin.
- **W15 full Javadoc** on `scope_touches_arena` + the region-open skip; flatten; no `//`.
- **Conservative — doubt → NOT elide (arena-espec §4.3):** an undetected site is leak-safe to the outer
  region, never UAF; elision removes only the wrapper.
- **`need == 0` limit case of the floor (§4.2):** composes with D1-T1; with the floor sized by need, elision
  removes even the header.
- **Never drop early with a live alias (§2.2):** elision removes a wrapper, it does not drop a live region.
- **Optimization only — no capability:** SM-A4 planted the guard; this realizes the physical skip.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  `[fixpoint]` — native-object-reproducible `gen2==gen3`; sweep `.tkt` after the region-open change.

## Fixtures

none — the fixpoint self-build exercises this. The compiler has countless allocation-free leaf scopes
(forwarders, comparison arms, guards) it compiles itself; the elision is exercised at scale, its
correctness proven by the native fixpoint reproducing + the CI legs running the binary. A wrongful elision
surfaces as a UAF/wrong exit on a native leg, not a missed fixture.

## Gate

`[fixpoint]` — build gen2 + scoped regression + `gen2==gen3` native-object byte-identity. "Green" = an
allocation-free scope opens no region (no `new`/`enter`/`leave`/`drop`, no header), the region stack stays
balanced, the peak memory / hot-path machinery drops, and the native object reproduces. Reseed-class:
`fixpoint-rebuild`.

## Deps

`SM-A4, RM-C16` — verbatim from 000-INDEX (the elision guard planted at SM-A4; the native route it realizes
on).

## Done when

A provably allocation-free scope opens NO region (region wrapper fully skipped, stack balanced), the guard
stays conservative (doubt → keep the region, leak-safe never UAF), the hot-path region machinery drops, and
the native-object `gen2==gen3` fixpoint reproduces.
