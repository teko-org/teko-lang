---
seq: 0099
crumb-id: NAT-A3
milestone: M4
gate: "[dry]"
reseed-class: "none"
deps: [NAT-A2]
sources:
  - "docs/design/backend-a3-regalloc.md:24-60"                         # the A2 shape the allocator consumes
  - "docs/design/plano-mestre-0.3.1-implementacao.md:284"              # M4 NAT-A3 row
  - "src/backend/regalloc.tks:1105"                                    # regalloc_module
  - "src/backend/abi_aapcs64.tks:1"                                    # AbiDescriptor + allocatable_pool
---

# 0099 · NAT-A3 — linear-scan regalloc + AAPCS64 descriptor (`regalloc.tks`, `abi_aapcs64.tks`)

> Target-independent linear-scan register allocation over A2's `MFunc`, parameterized by the AAPCS64 ABI
> descriptor: rewrite every virtual `MReg` to a physical register (or a spill slot with load/store),
> honoring the isel's physical pins, no two overlapping live ranges sharing a register.

## Goal

The regalloc leg: consume A2's `MFunc` (every value an `MReg`, `minst.tks:22`; `is_phys=false` virtual,
`is_phys=true` an ABI pin) and produce an `MFunc` in which every virtual `MReg` is colored to a physical
register or spilled, the pins honored, no overlap sharing a register. The allocator is
target-INDEPENDENT: it reads the per-`MInst` def/use table (`backend-a3:39-60`) and the AAPCS64
`AbiDescriptor` (`abi_aapcs64.tks:1` — `allocatable_pool`/`is_caller_saved`/`is_callee_saved`/
`spill_scratch`) and nothing else about instruction semantics. `regalloc_module`/`regalloc_func` already
exist (`regalloc.tks:1105`); A3 closes coverage to A2's full `MInst` set + wires the AAPCS64 descriptor.
It emits NO machine bytes (A4) and NO prologue/epilogue (deferred). **Byte-preserving** on the C route;
reseed-class `none` (a `[dry]` leaf).

## Where

- `src/backend/regalloc.tks:1105` `regalloc_module` — the linear-scan allocator; close coverage to A2's
  full `MInst` def/use set (the `MMSub` 3-use case sizes the scratch pool; `MMovK` is read-modify-write —
  `dst` is both use and def; `MCmp`/`MFCmp` write flags, a non-register def).
- `src/backend/abi_aapcs64.tks:1` `AbiDescriptor` + `:59` `arg_reg` + `:67` `allocatable_pool` + `:71/:76`
  caller/callee-saved + `:83` `spill_scratch` — the AAPCS64 descriptor parameterizing the allocator.

## How

1. **Build liveness from the def/use table** (`backend-a3:39-60`): each `MInst` case has a fixed def/use
   set — the allocator's ONLY input about semantics. Compute live intervals over the linearized `MFunc`.
2. **Linear-scan color** honoring pins: a virtual `MReg` whose interval overlaps an ABI pin cannot take
   that physical register while the pin is live; spill on pressure to a stack slot with a
   `spill_scratch`-fed load/store (`abi_aapcs64.tks:83`). The scratch pool is sized by the max simultaneous
   scratch demand (the `MMSub` 3-use case is the sizing constraint).
3. **Honor the special cases**: `MMovK` (`dst` use+def) keeps its interval joined; flag-writing
   `MCmp`/`MFCmp` produce a non-register def the allocator does not color but must sequence.
4. **Parameterize by `AbiDescriptor`**: `regalloc_module` takes the descriptor so the SAME allocator serves
   SysV64 (NAT-B1) and Win64 (NAT-B3) by swapping the descriptor — the allocator body is target-independent.

```teko
/**
 * regalloc_module — linear-scan register allocation over an MModule, parameterized by the ABI descriptor
 * (AAPCS64 here; SysV64/Win64 reuse the same body). Rewrites every virtual MReg to a physical register or
 * a spill slot (load/store fed by the descriptor's spill scratch), honoring the isel's physical pins, so
 * no two overlapping live ranges share a physical register. Emits no machine bytes and no prologue/epilogue.
 *
 * @param abi  the ABI descriptor (allocatable pools, caller/callee-saved sets, spill scratch)
 * @param m    the selected MModule on virtual registers
 * @return     an MModule where every MReg is physical (colored or spilled), or an error on a pin conflict
 * @throws     when a pin cannot be honored (an internal invariant break)
 * @since 0.3.1
 */
pub fn regalloc_module(abi: AbiDescriptor, m: MModule): MModule | error
```

5. **No post-alloc oracle** (differential oracles RETIRED): the no-overlap invariant + AAPCS64 correctness
   are proven by the self-build + the arm64 native CI leg.

## Rulings & laws

- **Teko-only:** `src/backend/*.tks`; no C twin.
- **W15 full Javadoc** on `regalloc_module` + the descriptor accessors + helpers; flatten; no `//`.
- **Target-independent allocator + ABI descriptor** (`backend-a3:12-20`): the same body serves all three
  ABIs — record so B1/B3 reuse it rather than fork.
- **Backend native testing removed (owner 2026-08-18):** the arm64 CI leg exercises regalloc.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  `[dry]` — no emitted-byte change; sweep `.tkt` after any descriptor/signature change.

## Fixtures

none — the fixpoint self-build exercises this. The compiler allocates registers for its OWN selected
corpus and the arm64 native CI leg runs it. No standalone `.tkr` reaches the regalloc surface.

## Gate

`[dry]` — compile + scoped `.tkr` + trivial fixpoint (no emitted-byte change; the allocator output feeds
only the native tail). "Green" = every virtual `MReg` is colored or spilled, pins honored, no overlap
sharing a register, and the C-route build is byte-identical. Reseed-class: `none`.

## Deps

`NAT-A2` — verbatim from 000-INDEX (the allocator consumes A2's `MFunc`).

## Done when

`regalloc_module` rewrites every virtual `MReg` to physical (colored or spilled) over the full A2 `MInst`
set, honors the AAPCS64 descriptor + isel pins with no overlap, and the `[dry]` build is byte-identical.
