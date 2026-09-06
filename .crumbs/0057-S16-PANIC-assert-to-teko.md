---
seq: 0057
crumb-id: S16-PANIC
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [S16-IO]
sources:
  - "docs/design/plano-s16-expurgo-libc-completo.md:238"          # FASE5 assert.c→Teko (compare+panic, L0-shaped)
  - "docs/design/plano-s16-expurgo-libc-completo.md:284-288"       # dep graph: FASE4 → FASE5 leaf
  - "docs/design/plano-s16-expurgo-libc-completo.md:388"           # S15 assert_migrated fixture
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:93-99"      # §1.4 the assert seed (assert.{c,h})
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:238"        # #6 assert in the libc-header table (L0-shaped)
---

# 0057 · S16-PANIC — FASE5 `assert.c` → Teko (compare + panic, L0-shaped)

> Migrate the assert seed's value-comparison + panic bodies fully to pure Teko over the already-migrated
> io/panic bottom (S16-IO), so `teko::assert` needs NO C symbol from `assert.c`; a leaf after FASE1-4.

## Goal

`assert.c` (256 lines, `src/assert/assert.c`) is one half of the freeze-exception the §16 sweep exists to
retire (`migracao…` §1.4). Its bodies are **value comparison + panic — entirely L0-shaped** (compare two
i64/u64/f64/str/bytes, and on mismatch `panic("assertion failed: …")`). The Teko twin `src/assert/assert.tks`
(154 lines) already carries almost all of this shape; what still ties the family to C is (a) the `panic`/
string bottom it stands on — closed by S16-IO (FASE2, `SYS_exit_group`+`SYS_write`+ftoa) — and (b) the C
seed's own compare/format bodies still being the ones the host-`cc` native binary links. This crumb makes the
Teko assert family **self-sufficient over the migrated panic**: every `eq_i64`/`ne_i64`/…/`bytes_eq` compares
and panics in pure Teko, and the C `assert.c` compare/format code is dead (no `teko__assert__*` C symbol on
the path). Byte-preserving for existing programs — the emitted `teko.c` for a program that calls
`teko::assert` is guarded byte-identical by the fixpoint (only WHICH object defines the symbol changes, not
the emitted call site or the assert body's lowering). It does NOT reseed teaching; it is a `fixpoint-rebuild`
swap the core consumes. **The physical DELETION of `assert.{c,h}` is NOT this crumb** — that is the FASE9
consolidated sweep (`0096` S16-SWEEP, M3); here the seed goes dead, it is not yet removed.

**BLOCKED (design-ahead, honest).** Like every runtime-migration leaf, FASE5 sits behind the **native fixpoint
closing** (the own backend compiling the compiler; `migracao…` banner + `gate-sem-c` §2.3). This doc is the
design the implementer resumes in minutes when the fixpoint closes; nothing here touches product or reseeds
today. The scenario-name state hooks (`scenario_prefix_rt`/`scenario_set_rt`/`scenario_ok_rt`, extern to
`teko_rt`) are process/task state and migrate with RT-L5 (`0063`) / the harness RT-L6 (`0064`) — they remain
`extern` after this crumb (honest partial), because compare+panic is the L0 leaf, scenario state is not.

## Where

- `src/assert/assert.tks` — the `teko::assert` family (`is_true`/`eq_i64`/`ne_i64`/`lt_i64`/`le_i64`/`gt_i64`/
  `eq_str`/`ne_str`/`str_contains`/`bytes_eq`/`len_eq`/`is_present`/`is_absent`/`is_ok`/`is_error`, …) — confirm
  each body is pure Teko over `panic` + `teko::str`; retarget any that still routed through a `teko_rt` compare.
- `src/assert/assert.c` (`src/assert/assert.c:52` `tk_assert_fail`, `:92-259` the `teko__assert__*` twins) —
  goes DEAD (no path references it); the file is NOT deleted here (that is `0096`).
- `src/build/project.tks:428` `assert_dir` / the seed-linking logic (`build_cc_argv`, the
  `TK_ASSERT_SEED_STRONG` / `program_declares_assert_seed` role-selection described in `assert.c:1-40`) — the
  seed's dual-role (fill-in for programs that CALL but do not DEFINE `teko::assert`) is now served by the
  Teko bodies compiled into the program's own object, exactly as `migracao…` §2.2 proves for the 11 live
  runtime symbols. Confirm the compiler's own build still defines the family exactly once (no double-def).
- NO new module; NO new decl surface — assert bodies already exist in `.tks`.

## How

1. **Confirm the panic bottom is migrated (dep S16-IO).** Every assert failure calls `panic(msg)`; `panic`
   lowers to `SYS_write`(stderr) + `SYS_exit_group` after S16-IO (FASE2). The float family (`eq_f64`) formats
   via the pure-Teko `ftoa`/`%.17g` from S16-IO. So no assert body needs a C symbol for its failure path.
2. **Audit each `assert.tks` body for a residual C compare.** The compares are `i64`/`u64` `==`/`!=`/`<`/`<=`/
   `>`/`>=`, `f64` equality, `str` equality/substring, and `[]byte` `memcmp`-equivalent. The str/bytes ones
   route through `teko::str::eq`/a byte loop (already L0 in `teko_rt.tks`), NOT `tk_slice_eq_bytes`. Retarget
   any that still bind a `teko_rt` extern to the pure-Teko equality.
3. **The seed's dual-role becomes a build fact, not a linker guess** (`assert.c:29-40`). With the family fully
   Teko, a program that DEFINES `teko::assert` (the compiler corpus) compiles its own bodies into its object;
   a program that only CALLS it links the same Teko bodies from the assert unit — no weak-symbol PE/COFF
   breakage (the bug `assert.c:18-27` documents), no C seed. `program_declares_assert_seed` /
   `TK_ASSERT_SEED_STRONG` collapse to "always the Teko unit"; keep the single-definition invariant.
4. **No new surface.** This is a migration + confirmation, not a feature; there is no fn/type to add. The
   assert declarations already carry W15 Javadoc (`assert.tks:20-` — e.g. the `@throws` on `scenario_ok`);
   any body edited keeps its doc-comment intact (no inline `//`).
5. **Fixpoint byte-identity is the detector.** After the swap, `gen2 == gen3` byte-identical proves the
   emitted `teko.c` did not shift for existing assert callers. If it shifts, a residence or lowering changed
   unintentionally — stop and re-examine (`migracao…` R8).

No new W15 signature is introduced by this crumb (bodies pre-exist). The reused surfaces are `panic(msg: str)`
(migrated by S16-IO), `teko::str::concat`/`eq` (L0, `teko_rt.tks`), and the `teko::assert::*` family already
declared in `assert.tks`.

## Rulings & laws

- **Teko-only:** the migration lands in `src/assert/assert.tks`; the C seed is FROZEN and only goes DEAD here
  (deletion is the M3 sweep `0096`). The runtime/assert C exception is the BRIDGE the §16 campaign retires
  (`migracao…` §1.4, `plano-s16-expurgo…`:238) — no tension.
- **W15 full Javadoc** on every touched declaration (pub + private); flatten/extract; no inline `//`. Existing
  doc-comments are preserved verbatim on any edited body.
- **Removals = clean expurgo, NO tombstone:** this crumb does NOT remove surface (the assert names stay). It
  makes the C seed dead; the physical `assert.{c,h}` deletion + `#include "assert.h"` stop is `0096` S16-SWEEP,
  where the expurgo is clean and tombstone-free.
- **Owner ruling (FASE order, `plano-s16-expurgo…`:253-259):** FASE5 is a pure leaf — the sweep is the FINAL
  step, not this one; deleting before every subsystem migrates would prove less.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` (6.5 GiB) cap — a blown guard
  is a root-cause fix, never a raised ceiling; commit each green step; **reseed ONLY at a [RITUAL]** — this is
  `fixpoint-rebuild`, the core CONSUMES the swap and NO teaching seed is harvested; fixpoint `gen2==gen3`
  byte-identical; sweep `.tkt`/`.tkr` after any signature change (none expected here).

## Fixtures

The assert family is heavily exercised by `teko test .`-style harness scenarios, but the harness itself is
NEVER run as `teko test .` by the gate; the migrated compare+panic leaf gets an isolated oracle
(the §16 `S15` fixture, `plano-s16-expurgo…`:388):

| fixture | asserts | expected |
|---|---|---|
| `assert_migrated` | `assert::eq_i64(21+21, 42)` returns without panic; a `!=` path panics (via a probe that catches the exit) | `0` |
| `assert_str_bytes_pure` | `assert::eq_str`/`bytes_eq` compare + pass with NO `teko_rt`/`assert.c` symbol referenced (nm-check the object) | `0` |
| `assert_no_c_seed_link` | a program that CALLS `teko::assert::is_true(true)` but does NOT define it links + runs from the Teko unit alone (no weak-symbol) | `0` |

## Gate

`[fixpoint]` — build gen2 + the scoped fixtures + `gen2==gen3` byte-identity. "Green" = every assert compare
panics/passes in pure Teko over the migrated panic, no `teko__assert__*` C symbol is on the path, the emitted
`teko.c` is byte-identical to before the swap, and `assert_migrated` exits `0`. **Reseed-class:**
`fixpoint-rebuild` (core-consumes the swap; teaches nothing; NO reseed harvested).

## Deps

`S16-IO` (`0051` — FASE2 `SYS_exit_group`+`SYS_write`+ftoa: the panic/format bottom every assert failure
stands on).

## Done when

The `teko::assert` compare+panic family runs pure-Teko over the migrated panic, the C `assert.c` bodies are
dead on every path (nm-verified), a program that calls but does not define `teko::assert` links from the Teko
unit alone, the `assert_migrated` fixture exits `0`, and a `[fixpoint]` build is `gen2==gen3` byte-identical.
