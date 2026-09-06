---
seq: 0040
crumb-id: RM-C6
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RM-C5]
sources:
  - "docs/design/reducao-memoria-arrays-0.3.1.md:257-260"   # §6 crumb C6
  - "docs/design/reducao-memoria-arrays-0.3.1.md:184-198"   # §5.3 arena-por-escopo (the route)
  - "docs/design/reducao-memoria-arrays-0.3.1.md:56-83"     # §2 foundation already in df63f88c
  - "docs/design/reducao-memoria-arrays-0.3.1.md:107-129"   # §4 mem_model — C6 residual drop
---

# 0040 · RM-C6 — arena-per-scope at boundaries (`region_enter/leave/drop_subtree` + `arena_push/pop/commit`)

> Emit `region_enter`/`region_leave` + `region_drop_subtree` per function and
> `arena_push`/`arena_pop`/`arena_commit` per file, so the transient scratch that today leaks into the
> never-freed `root` arena is reclaimed EN MASSE at each scope boundary — no block-by-block free.

## Goal

Eixo B reclaims the residual (~1.2 GB non-push): the scratch that is born and dies inside a scope but
today falls into `root` (which is never freed until process exit). The arena machinery is ALREADY landed
(`src/runtime/arena.tks` exposes `region_enter`/`region_leave`/`region_new`/`region_current`/
`region_drop_subtree` and `arena_push`/`arena_pop`/`arena_commit`; the codegen already routes allocation
through `CgArenaSym` and `emit_slice_of_len` already allocates into the enclosing region via
`cg_enclosing_region_expr`). What remains is to CLOSE THE LOOP: emit the enter/leave (and mark/rewind)
at the RIGHT boundaries. This crumb makes codegen emit a per-function region prologue/epilogue and a
per-file mark/rewind, so a function's `region_alloc` scratch is dropped in bulk on return and a file's
parse/lower scratch is rewound after that file compiles. Byte-mover for the emitted `teko.c`? YES — it
INSERTS the enter/leave/drop and push/pop/commit calls into the emitted C, so the emitted text changes
(a real emit delta) → `fixpoint-rebuild` reseed. Expected drop: -0.2 to -0.4 GB (mem_model C6), taking
the peak to ~0.9–1.1 GB.

## Where

- `src/codegen/codegen.tks:5747` — `cg_enclosing_region_expr` / `:5752` `cg_enclosing_region_expr_framed`
  — the region an alloc lands in; unchanged in shape, but now a per-function child region is pushed so
  the enclosing region for a function body is a FRESH subtree.
- `src/codegen/codegen.tks` function-emit prologue/epilogue — NEW: emit
  `region_enter(region_new(region_current()))` at the emitted function prologue and
  `region_leave()` + `region_drop_subtree(<that region>)` at every emitted return/epilogue path.
- `src/codegen/codegen.tks` file/unit-emit boundary — NEW: emit `arena_push()` before a file's scratch
  is built and `arena_pop()` after; `arena_commit()` where a result must SURVIVE to the next stage (the
  AST that continues).
- `src/codegen/codegen.tks:250` `CgArenaSym` — reuse the existing `RegionEnter`/`RegionLeave`/`RegionNew`/
  `RegionDropSubtree`/`ArenaPush`/`ArenaPop`/`ArenaCommit` kinds (all already routed by
  `cg_arena_teko_sym`/`cg_arena_c_sym`); no new symbol kind.

## How

1. **Per-function region (scratch of codegen/lir per function).** In the emitted function prologue,
   emit `region_enter(region_new(region_current()))`; in EVERY epilogue path (each `return`, the natural
   fall-off, and the DPS/`ret_dest` paths), emit `region_leave()` then `region_drop_subtree(<the region
   handle captured at prologue>)`. Every `region_alloc` inside the body (which `emit_slice_of_len`
   already routes to the current region) lands in this subtree and is dropped in bulk on return. Capture
   the region handle in a prologue local so the epilogue drops exactly the region it entered.

```teko
/**
 * cg_emit_fn_region_prologue — emit the per-function scratch region entry: a fresh child of the
 * current region entered for the duration of the function body, so all `region_alloc` scratch the
 * body makes is bulk-dropped by the matching epilogue (RM-C6, `reducao §5.3`). Pairs 1:1 with
 * `cg_emit_fn_region_epilogue`; the returned handle-expression is the region the epilogue drops.
 *
 * @param buf   the emit buffer
 * @param regions  the region-frame stack (the epilogue reads the pushed frame back)
 * @return      `buf` with `region_enter(region_new(region_current()))` emitted, or an emit error
 * @throws      when the region-frame stack is malformed (mismatched enter/leave)
 * @since 0.3.1
 */
fn cg_emit_fn_region_prologue(buf: Cb, regions: []RegionFrame): Cb | error
```

2. **Per-file mark/rewind (parse/lower scratch).** Around compiling one file, emit `arena_push()`
   before and `arena_pop()` after (rewind the `root` bump to the mark, returning ALL that file's
   scratch), and `arena_commit()` at the points where the produced AST must outlive the file (survive to
   the next stage). This is the `mark/rewind` checkpoint (`arena_push`/`arena_pop`/`arena_commit`) the
   arena already exposes.
3. **Move-on-return safety.** The per-function drop MUST NOT reclaim a value that ESCAPES via return —
   the existing `frame_escape_guard`/escape analysis already promotes escaping values out of the frame
   region before `region_leave`; RM-C6 emits the drop only for the frame's own scratch, never the moved
   return value. Reuse the landed escape machinery; do not weaken it.
4. **Fixpoint is a real delta.** Unlike RM-C5 (byte-identical), the emitted `teko.c` now CONTAINS the
   enter/leave/drop/push/pop/commit calls — a genuine emit change. Expect `gen1 ≠ gen0` on first emit,
   then `gen2 == gen3` (the ladder converges once the seed knows the new emit). Gate HARD on
   `gen2 == gen3`.
5. **Peak proof.** Under `ulimit -v 6815744`, the peak drops ~0.2–0.4 GB (residual reclaimed). If a
   per-function drop reclaims a still-live value the build CRASHES (UAF) rather than merely peaking — a
   root-cause escape-analysis bug, HALT and fix, never widen the drop conservatively into silence.

## Rulings & laws

- **Teko-only:** codegen emit logic in `.tks`; the arena runtime (`src/runtime/arena.tks`) is Teko over
  raw mmap, not a C twin.
- **W15 full Javadoc** on the new prologue/epilogue emitters; flatten; no inline `//`.
- **Owner (`reducao §3`, `§5.3`):** arena-per-scope with bulk drop DISPENSES individual free for the
  transient — the preferred route (R1); block-by-block free (`region_free`, RM-C8) is the backstop for
  the reassigned-accumulator case only.
- **Escape soundness (R2 family):** the per-function drop must never reclaim an escaping/returned value;
  it rides the landed `frame_escape_guard`. On the smallest doubt of liveness, the value is promoted
  (moved), not dropped.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` — a blown guard is a
  root-cause fix; commit each boundary (per-function, then per-file) as its own green step; reseed ONLY
  at the fixpoint; `gen2==gen3` byte-identical; sweep `.tkt`/`.tkr` after the emit-shape change.

## Fixtures

none — the fixpoint self-build exercises this. The compiler emitting its OWN function/file boundaries
drives every enter/leave/drop and push/pop/commit; `gen2==gen3` plus the falling peak-guard and a
crash-free MEM_PARANOID tree ARE the regression. (A UAF from an over-eager drop surfaces as a build
crash, not a missed assertion, so no isolated `.tkr` is needed.)

## Gate

`[fixpoint]` — build gen2 (`--no-verify --release`, `TEKO_BACKEND=c`, `TEKO_CC=clang`,
`ulimit -v 6815744`) + scoped regression + `gen2==gen3` byte-identity. "Green" = the emitted `teko.c`
now carries the per-function region prologue/epilogue and per-file mark/rewind, the 3-gen ladder
converges (`gen2==gen3`), MEM_PARANOID exits 0, and the peak falls ~0.2–0.4 GB to ~0.9–1.1 GB.
Reseed-class: `fixpoint-rebuild`.

## Deps

`RM-C5`.

## Done when

Codegen emits `region_enter(region_new(region_current()))`/`region_leave()`+`region_drop_subtree` per
function and `arena_push`/`arena_pop`/`arena_commit` per file, no escaping value is reclaimed
(MEM_PARANOID clean), `gen2==gen3` holds, and the build peak has dropped into the ~0.9–1.1 GB band.
