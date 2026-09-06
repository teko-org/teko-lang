---
seq: 0113
crumb-id: NAT-N1
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [NAT-A1]
sources:
  - "docs/design/native-lowering-cobertura-zero-libc-0.3.1.md:0-260"  # campaign map — pull-forward + write-only native gate
  - "docs/design/recon-native-n1n2-gaps-strategy.md:72-85"            # the family taxonomy (union/nullable = LARGEST)
  - "src/lir/lower.tks:909"                                           # `<type>` has no single PrimKind, asked by <site>
  - "src/lir/lower.tks:1141"                                          # comparing a tagged union to a non-null non-numeric member
  - "docs/design/backend-a1-lir-lowering.md:46-238"                  # lowering frontier
  - "docs/design/plano-mestre-0.3.1-implementacao.md:282"           # M4 NAT-A1 row (this crumb extends it)
---

# 0113 · NAT-N1 — union / nullable native-lowering family (the largest honest-stop cluster)

> Close the single LARGEST native-lowering honest-stop family: `T | null` / `str | error` and every
> tagged-union shape that today raises "has no single PrimKind" (`lower.tks:909`) or "comparing a
> tagged union to a non-null, non-numeric member is not yet lowered" (`lower.tks:1141`) in cast /
> field / match-subject / comparison position.

## Goal

This crumb closes the frontier that NAT-A1 (`0097`) left implicit. `0097` names control-flow,
aggregates, rodata/fat-pointers, `match` (loaded-tag variant) and dispatch, but it does NOT enumerate
the **union / nullable** family, which `recon-native-n1n2-gaps-strategy.md:72-85` measures as the
LARGEST cluster (the `str`-PrimKind stop alone fires ×17 in the bulk corpus) and the one that is core
language surface. A `T | null` / `str | error` value has no single machine word, so every site that
asks `prim_kind_of` a union type honest-stops. This crumb teaches the native lowering to represent a
tagged union as `{tag, payload}` and lower its cast, field-access, comparison and match-subject uses.
It is **byte-preserving on the C route** (native-only lowering; the emitted C artifact does not move)
and drives a **fixpoint-rebuild**. It is a completeness crumb, not a capability one; the checker
already types these unions.

## Where

- `src/lir/lower.tks:909` — the `has no single PrimKind` honest-stop (`msg = ... asked by the <site>`).
  Its callers in cast/field/comparison must route a union operand to the new union-lowering path
  instead of asking for a single PrimKind.
- `src/lir/lower.tks:1141` — `lower_union_compare` honest-stop: extend the tagged-union comparison to
  cover non-null, non-numeric members (compare tag first, then dispatch the member compare on the
  active arm).
- `src/lir/lower.tks` — new arms: `lower_union_value` (materialize `{tag, payload}`), `lower_union_cast`
  (widen/narrow a union, `A → A | B`), `lower_union_field` (project a field common to all arms),
  `lower_union_match_subject` (tag-load + per-arm payload bind for a `match` on a union subject).
- `src/lir/lir.tks` — a `LUnionLayout { tag_offset; tag_width; payload_offset; payload_size; arms }`
  registered in `LModule.layouts` (the `{funcs; rodata; globals; layouts}` shape NAT-A1 introduced).
- `src/lir/lir_print.tks` — one print case for the union layout header and any new `LOp`.

## How

Follow the family order in `recon:105-110` — union/nullable FIRST because it clears the most corpus
items per crumb.

1. **Union machine model.** A `T | null` / `A | B` / `str | error` lowers to a two-slot aggregate
   `{tag: u32, payload: <max-arm>}` where `tag` selects the active arm and `payload` is sized to the
   largest arm (the union layout rule already ratified in Doc-2 per-type memory model, 1614-1669;
   union → largest slot wins, mirrored by `D1-T1` arena pre-sizing).

```teko
/**
 * union_layout_of — the resolved native layout of a tagged union: the tag slot, the payload slot
 * sized to the widest arm, and the per-arm machine descriptor. Keyed by the union's canonical
 * arm-set so `T | null` and `null | T` resolve identically.
 *
 * @param arms   the checker's resolved arm types, in canonical order
 * @param table  the checker type table supplying each arm's width/layout
 * @return       the tag/payload layout, or an error if an arm type has no known machine width
 * @throws       when an arm type has no registered layout or PrimKind
 * @since 0.3.1
 */
exp fn union_layout_of(arms: []checker::Type, table: checker::TypeTable): LUnionLayout | error
```

2. **`null` as the zero-tag arm.** A nullable `T | null` uses `tag == 0` for `null`, `tag == 1` for
   `T`; the payload for the `null` arm is undefined (never read). This is the common shape and must be
   the fast path — a `x == null` test lowers to `ICmpEq(tag, 0)`, no payload traffic.

3. **Cast (`lower_union_cast`).** Widening `A → A | B` sets `tag` to `A`'s ordinal and stores `A` into
   the payload slot; narrowing (post-`match`/guard) reads the payload at the arm's machine type. NO
   run-time check on widen (the checker proved the arm); the narrow is only reached on a proven arm.

4. **Field projection (`lower_union_field`).** A field common to all arms (e.g. a shared struct
   header) projects at its offset within the active arm's payload; a field NOT common to all arms is a
   checker error, never reached here.

5. **Comparison (`lower.tks:1141` extension).** `lower_union_compare` compares `tag` first; on equal
   tags it dispatches the member comparison for the active arm (numeric → `ICmp*`; `str`/`[]T` →
   the existing runtime compare; nested union → recurse). This removes the "non-null, non-numeric
   member" honest-stop.

6. **Match subject (`lower_union_match_subject`).** A `match` whose subject is a union loads `tag`
   once, builds the per-arm test chain against arm ordinals, and binds each arm's pattern variable to
   the payload read at that arm's machine type — reusing NAT-A1's per-arm merge for the result.

7. **Confirm the two stops are unreachable** by a checker-produced node: `lower.tks:909` and `:1141`
   remain only as internal-invariant guards. A final review, plus the self-build reaching further.

## Rulings & laws

- **Teko-only:** `src/lir/*.tks`; no C twin. LIR is in-memory only — no `.tkb`/wire concern.
- **Comment convention (W15, owner 2026-08-19):** `/** */` only on `exp` decls; no `//` or `/* */`; a
  doc-comment never larger than the code — enforced by the W15 reviewer, not the compiler.
- **Fork protocol (owner 2026-08-19):** the union machine model (largest-arm payload, tag slot) is
  already ratified (Doc-2 per-type memory model 1614-1669; `D1-T1` union → largest slot wins) — no
  undecided fork; do NOT HALT.
- **W15 full Javadoc** on every new `exp` `LOp`/type/fn; flatten with early-return guards; no `//`.
- **`LOp`/`LModule`/`layouts` growth is SANCTIONED** (the N2 growth) — the frozen closure is
  `LType`/`LBinOp`/`LUnOp`, untouched here.
- **Layout parity:** the union `{tag, payload}` layout must equal what the C route emits for the same
  union (tag width, payload size, offsets) so the C-vs-native fixpoint agrees by construction.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit each closed
  sub-family; `[fixpoint]` — `gen2==gen3` byte-identical (no emitted artifact moves); sweep `.tkt`
  after each `LModule`/layout widening. **Conditional (owner 2026-08-19):** once a DRY build peaks
  ≤ 1.5 GB the full `teko build .` unlocks — measure and report peak when crossing it.
- The Doc-2 ruling this rests on: the per-type memory model union layout (Doc-2:1614-1669) + the
  recon family taxonomy (`recon-native-n1n2-gaps-strategy.md:72-85`).

## Fixtures

The self-build fixpoint exercises MOST of this (the compiler's own `src/` is dense with `T | null` and
`str | error`). Add ONLY the boundary shapes the corpus under-covers:

| fixture | asserts | expected |
|---|---|---|
| `native_union_str_error_match` | native `match` on a `str \| error` subject binds both arms and returns | `0` |
| `native_nullable_compare` | native `x == null` on a `T \| null` lowers to a tag test (no payload read) | `0` |
| `native_union_ordered_member` | native comparison of a tagged union whose active arm is a non-numeric member lowers (was `:1141`) | `0` |

## Gate

`[fixpoint]` — build gen2 (C route) + scoped regression + **C `gen2.c==gen3.c` byte-identity**
(native-only lowering; the emitted C artifact does not change). The NATIVE metric is **write-only**:
`gen1` emits gen2 native (`item N/TOTAL`) further into the corpus than the union/nullable frontier once
this family lowers — the `gen2==gen3` **native** rebuild is NOT a gate here (gen2 native does not run
until post-F9; that migration is `0106`/RM-C16). "Green" = neither `lower.tks:909` nor `:1141` is
reachable by a checker-produced node, the full `src/` union/nullable surface lowers to LIR, and the C
rebuild is byte-identical. Reseed-class: `fixpoint-rebuild` (rides R#1, F7a — pulled forward from M4 per
D106, `native-lowering-cobertura-zero-libc-0.3.1.md` §4).

## Deps

`NAT-A1` — verbatim from 000-INDEX. **Re-sequenced (D106, campaign §4):** pulled forward from M4 to run
in the tail-§16 R#1 reseed (F7a) — it does NOT wait on the memory milestone nor `RM-C15`/`0105`; the
native lowering is WRITE-ONLY and rides the phase reseed.

## Done when

`T | null`, `str | error` and every tagged-union cast / field / comparison / match-subject in `src/`
lowers to LIR with no reachable "has no single PrimKind" / "tagged union comparison" honest-stop, `gen1`
emits gen2 native past this frontier (write-only), and the C `gen2.c==gen3.c` rebuild is byte-identical.
