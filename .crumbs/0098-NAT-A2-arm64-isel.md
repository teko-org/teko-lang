---
seq: 0098
crumb-id: NAT-A2
milestone: M4
gate: "[dry]"
reseed-class: "none"
deps: [NAT-A1]
sources:
  - "docs/design/backend-a2-isel-arm64.md:20-70"                       # the A1 shape isel consumes + MInst
  - "docs/design/plano-mestre-0.3.1-implementacao.md:283"              # M4 NAT-A2 row
  - "src/backend/isel_arm64.tks:898"                                   # select_lfunc / select_module
  - "src/backend/minst.tks:9"                                          # MRelocKind / MInst
---

# 0098 · NAT-A2 — arm64 instruction selection (`minst.tks` + `isel_arm64.tks`)

> arm64 instruction selection over A1's complete `LModule`: lower each `LOp` to the `MInst` machine-IR on
> virtual `MReg`s (no register allocation, no bytes), signedness on the opcode, widths on `LType`,
> fat values as two ordinary scalars.

## Goal

The arm64 isel leg: consume A1's full sixteen-case `LOp` set and select each to `MInst` (`minst.tks`) on
virtual registers, over an abstract operand model — `SDIV`/`UDIV` picked by `IDivS`/`IDivU`, signed/
unsigned condition codes by `ICmpLt*`, GPR/FPR class by `LType`. `str`/`slice` are already two independent
scalar VRegs at the LIR level (no fat `LOp`), so isel needs NO fat opcode: each half selects as an
ordinary scalar; the pair surfaces only as two adjacent AAPCS64 argument occupancies at a call or two
`MMov`s at a merge. This is a design-ahead `[dry]` leg landing against A1's DECLARED `LModule` shape;
`select_module`/`select_lfunc` already exist (`isel_arm64.tks:898-904`) — A2 closes their coverage to the
full `LOp` set A1 delivers. **Byte-preserving** on the C route (the isel output feeds only the native
tail, never the emitted C); reseed-class `none` (a `[dry]` leaf — compile + scoped check, no emitted-byte
change).

## Where

- `src/backend/minst.tks` — the `MInst` machine-IR variant (the 29-case set, `minst.tks:787` per the
  A3 doc) + `MReg`/`MRegClass`/`MMem`/`MRelocKind` (`minst.tks:9`) + the printer. Extend any `MInst` case
  the full `LOp` set requires (memory/aggregate/indirect selections).
- `src/backend/isel_arm64.tks:898` `select_lfunc` / `:904` `select_module` — the `LIR → MInst` selector;
  extend the per-`LOp` match to cover A1's memory/rodata/fat/indirect cases with virtual `MReg` operands.

## How

1. **Cover the full `LOp` set** in `select_lfunc` (`isel_arm64.tks:898`): a flat per-op match (signedness
   on the opcode, width on `LType`) selecting each `LOp` to one or more `MInst`. `LAlloca`→stack-slot
   address; `LFieldAddr`→`base + offset` (folded when constant, `MAluImm`); `LLoad`/`LStore`→`MLdr`/`MStr`
   over `MMem`; `LGlobalAddr`/`LFuncAddr`→`ADRP`+`ADD` page-hi/page-lo reloc pair (`adrp_inst`/
   `add_lo_inst`, `minst.tks:345-349`); `LCall`/`LCallIndirect`→`MBl`/`MBlr` with AAPCS64 arg pins.
2. **Fat values as two scalars** (the load-bearing correction): each half of a `str`/`slice` selects as an
   ordinary scalar `MReg`; no fat `MInst`. A fat pair passed to a call occupies two adjacent AAPCS64 arg
   registers; a fat pair crossing a merge is two `MMov`s.
3. **Physical pins at ABI boundaries only**: isel plants `is_phys=true` `MReg`s at call arg/result and at
   `ret` (AAPCS64), leaving every interior value virtual for A3 to color.

```teko
/**
 * select_module — select an arm64 MModule from A1's complete LModule: each LFunc's LOps become MInsts on
 * virtual MRegs (physical pins only at AAPCS64 arg/result/ret boundaries), rodata/globals/layouts carried
 * through unchanged for the encoder. No register allocation (A3) and no machine bytes (A4) — the selected
 * form stays on virtual registers over the abstract operand model.
 *
 * @param m  the lowered LModule (A1's full LOp coverage)
 * @return   the selected MModule on virtual registers, or an error on an uncovered LOp
 * @throws   when an LOp has no arm64 selection (an internal invariant break, not a checker-reachable node)
 * @since 0.3.1
 */
pub fn select_module(m: lir::LModule): MModule | error
```

4. **No isel-over-oracle harness** (the differential oracles are RETIRED, `backend-a2` NOTA): coverage is
   proven by the self-build lowering→selecting its own corpus + the arm64 native CI leg, not a standalone
   oracle.

## Rulings & laws

- **Teko-only:** `src/backend/*.tks`; no C twin. `MInst` is in-memory only (no wire format).
- **W15 full Javadoc** on every `MInst` case, `select_*` fn, and helper; flatten; no `//`.
- **Signedness on opcode, width on `LType`** (`backend-a2:34-42`): a flat per-op match, no re-analysis.
- **No fat opcode** (`backend-a2:44-54`): fat values are two scalars — resolved decision, recorded so a
  reviewer does not flag a missing fat selection.
- **Backend native testing removed (owner 2026-08-18):** the arm64 CI leg exercises isel; no standalone
  backend fixture.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  `[dry]` — no emitted-byte change on the C route; sweep `.tkt` after any `MInst` widening.

## Fixtures

none — the fixpoint self-build exercises this. The compiler selects arm64 `MInst` for its OWN corpus, and
the arm64 native CI leg runs it (owner: "backend native — REMOVER; o CI exercita"). No standalone `.tkr`
reaches the isel surface.

## Gate

`[dry]` — compile + scoped `.tkr` (the surviving corpus) + trivial fixpoint (no emitted-byte change; the
isel output feeds only the native tail). "Green" = `select_module` covers A1's full `LOp` set with virtual
`MReg`s + ABI pins, and the C-route build is byte-identical. Reseed-class: `none`.

## Deps

`NAT-A1` — verbatim from 000-INDEX (isel consumes A1's complete `LModule`).

## Done when

`select_module`/`select_lfunc` cover every `LOp` A1 produces to `MInst` on virtual registers with AAPCS64
pins at boundaries, fat values select as two scalars, and the `[dry]` build is byte-identical.
