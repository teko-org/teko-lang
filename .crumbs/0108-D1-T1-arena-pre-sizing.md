---
seq: 0108
crumb-id: D1-T1
milestone: M5
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RM-C16]
sources:
  - "docs/design/arena-especificacao-unica-0.3.1.md:132-163"           # §3 arena floor / pre-sizing
  - "docs/design/plano-mestre-0.3.1-implementacao.md:301"              # M5 D1-T1 row
  - "src/codegen/codegen.tks:9832"                                     # #arena_size presize (profiler)
  - "src/codegen/codegen.tks:3167"                                     # emit_slice_of_len
---

# 0108 · D1-T1 — arena pre-sizing / static floor

> Seed each region's first chunk from the AST-proved static floor (Σ of the fixed-width allocations a
> scope provably makes) instead of the fixed 64 KiB default; a union member's slot takes the LARGEST
> alternative. Optimization of the already-correct arena — no capability, a perf/binary-size fixpoint.

## Goal

Doc-1 Idea 1: the AST proves a LOWER BOUND on a region's first-chunk demand — the sum of the fixed-width
allocations a scope provably makes (struct literals of known layout, array literals of known count, box
sites). This crumb seeds `region_new` with `need + header` instead of the fixed 64 KiB default, so a small
scope no longer pays a 64 KiB minimum (the profiler measured a 200-byte block costing 64 KiB — a 300× loss).
For a union-typed slot, the floor takes the LARGEST alternative's slot (a union → largest member wins). It
is the LOWEST-priority of the three Doc-1 ideas and folds into the profiler's existing presize
(`#arena_size`, `codegen.tks:9832`) — NOT a separate analysis. The floor is a lower bound: the region still
grows beyond it by chunk-list (never a UAF from sub-floor — overflow is structurally impossible), and
over-floor is leak-safe (reserved-not-used). It delivers tens of MB (tail-waste ~26 MB / 21k chunks) +
`posix_memalign` throughput, NOT the 93% push win (that was Eixo A). It is a **byte-mover** on the native
route (the emitted object's presize constants change) driving a **fixpoint-rebuild** (byte-identity: the
floor is a deterministic function of the AST).

## Where

- `src/codegen/codegen.tks:9832` — the `#arena_size` presize (the profiler's `Confidence::Thin` seed) —
  FOLD the static floor into it: the AST-computed `need` seeds the presize, refined by the profiler p99.9
  when a sample exists.
- `src/codegen/codegen.tks:3167` `emit_slice_of_len` + the region-open emission — pass `need + header` to
  the sized region-new (`tk_region_new_sized_u(parent, need + header)` per §3.1).
- `src/runtime/arena.tks` — `region_new` / the sized region-new accepting the computed floor (the arena is
  already Teko post-RM-C9).
- A new `arena_floor(scope)` static analysis over the scope's statements (the Σ of provable fixed-width
  allocations; union → largest member).

## How

1. **Compute the floor** (`arena_floor`): walk the scope's statements, summing the byte size of each
   PROVABLE fixed-width allocation (struct literal of known layout via `layout_of`, array literal of known
   count, box site). A union-typed slot contributes its LARGEST alternative's size. Dynamic-size sites
   contribute nothing to the floor (they grow by chunk-list).

```teko
/**
 * arena_floor — the AST-proved LOWER BOUND on a scope's first-chunk demand: the sum of the fixed-width
 * allocations the scope provably makes (known-layout struct literals, known-count array literals, box
 * sites); a union-typed slot takes its LARGEST alternative (union → largest member wins). Seeds
 * region_new with need+header instead of the fixed 64 KiB default. A lower bound only — the region still
 * grows beyond it by chunk-list, so a sub-floor is never a UAF, and over-floor is leak-safe.
 *
 * @param body   the scope's statement block
 * @param table  the checker type table (for layout_of sizes)
 * @return       the proved byte floor (0 when the scope allocates nothing — the elision case, D1-T3)
 * @since 0.3.1
 */
fn arena_floor(body: []checker::TStatement, table: checker::TypeTable): u64
```

2. **Seed the region** with `need + header` (`codegen.tks:3167` region-open): replace the fixed 64 KiB
   default with the computed floor; `open_frame_region`/`open_native_region` pass it to the sized
   region-new.
3. **Fold into the profiler presize** (`codegen.tks:9832`): the floor seeds `Confidence::Thin`; the
   profiler p99.9 refines when a runtime sample exists. Do NOT build a separate analysis (§3.3 priority).
4. **Union → largest slot**: a union member's floor contribution is the max over alternatives (composes
   with 9D-T1's inline unions).
5. **Fixpoint**: the floor is deterministic per AST; the native object reproduces; `gen2==gen3`.

## Rulings & laws

- **Teko-only:** `src/codegen/*.tks` + `src/runtime/arena.tks`; the arena is Teko (post-RM-C9).
- **W15 full Javadoc** on `arena_floor` + the sized region-open; flatten; no `//`.
- **Floor is a LOWER bound (arena-espec §3.2):** the region grows beyond by chunk-list; sub-floor is never
  UAF (overflow structurally impossible); over-floor is leak-safe.
- **Fold into the profiler presize, do NOT build a separate analysis (§3.3):** lowest priority of the three
  ideas.
- **Optimization only — no capability:** the arena is already correct; this is a perf/binary-size tune.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  `[fixpoint]` — native-object-reproducible `gen2==gen3` (the presize constants are deterministic); sweep
  `.tkt` after the region-open signature change.

## Fixtures

none — the fixpoint self-build exercises this. The compiler opens thousands of scoped regions compiling
itself; the floor is measured by the peak-memory + binary-size delta the native fixpoint tracks, and the
determinism is proven by native-object reproduction. No isolated `.tkr` reaches the presize surface.

## Gate

`[fixpoint]` — build gen2 + scoped regression + `gen2==gen3` native-object byte-identity. "Green" = each
region seeds from its AST floor (`need + header`, not 64 KiB), a union slot takes its largest member, the
peak-memory/tail-waste drops, and the native object reproduces. Reseed-class: `fixpoint-rebuild`.

## Deps

`RM-C16` — verbatim from 000-INDEX (the native route the arena tune targets).

## Done when

Each region's first chunk is seeded from the AST-proved static floor (`need + header`, union → largest
member) folded into the profiler presize, tail-waste drops, and the native-object `gen2==gen3` fixpoint
reproduces.
