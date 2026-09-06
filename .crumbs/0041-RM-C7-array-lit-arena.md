---
seq: 0041
crumb-id: RM-C7
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RM-C6]
sources:
  - "docs/design/reducao-memoria-arrays-0.3.1.md:262-265"   # §6 crumb C7
  - "docs/design/reducao-memoria-arrays-0.3.1.md:56-83"     # §2 foundation — emit_slice_of_len routes RegionAlloc
  - "docs/design/reducao-memoria-arrays-0.3.1.md:113-118"   # §4 mem_model — C7 drop
---

# 0041 · RM-C7 — array literal via arena (`emit_array_lit` non-spread → `region_alloc`, kill raw `malloc`)

> Route the non-spread branch of `emit_array_lit` through `region_alloc` on the enclosing scope region —
> exactly as `emit_slice_of_len` already does — instead of the raw `malloc(...)` it emits today, closing
> a never-freed `malloc` leak.

## Goal

`emit_array_lit` (`src/codegen/codegen.tks:3380`), in its non-spread branch, emits a direct `malloc(...)`
for the array literal backing — a `malloc` that is NEVER freed, so every array literal the compiler
emits leaks its backing for the life of the process. `emit_slice_of_len` (`:3339`) already solved the
identical problem for `[n]T = []` by allocating via `RegionAlloc` into the enclosing scope region
(`cg_enclosing_region_expr`). This crumb makes `emit_array_lit`'s non-spread branch do the SAME:
allocate the literal's backing with `region_alloc` on the current scope region, so RM-C6's per-scope
drop reclaims it in bulk. Byte-mover for the emitted `teko.c`? YES — the emitted allocation call changes
from `malloc(...)` to the arena symbol (`region_alloc`, routed through `CgArenaSym::RegionAlloc`) → a
real emit delta → `fixpoint-rebuild` reseed. Expected drop: -0.1 to -0.2 GB (mem_model C7), holding the
peak near ~0.9 GB.

## Where

- `src/codegen/codegen.tks:3380` — `emit_array_lit` — the non-spread branch: replace the raw
  `malloc(<bytes>)` backing allocation with `region_alloc(<enclosing region>, <bytes>)`.
- `src/codegen/codegen.tks:5747` — `cg_enclosing_region_expr` — reused verbatim to name the region the
  literal allocates into (the same expression `emit_slice_of_len` passes to `RegionAlloc`).
- `src/codegen/codegen.tks:250` — `CgArenaSym::RegionAlloc` — reused; no new symbol kind.

## How

1. **Mirror `emit_slice_of_len`.** In `emit_array_lit`'s non-spread branch, compute the backing byte
   total exactly as today, then emit the allocation through the arena symbol table
   (`cg_arena_sym(CgArenaSym::RegionAlloc)`) applied to `cg_enclosing_region_expr(regions)` and the byte
   count — NOT `malloc`. The element writes-by-index that follow are unchanged (the backing address is
   the same shape).

```teko
/**
 * emit_array_lit — emit an array literal. Non-spread branch (RM-C7): the backing is allocated via
 * `region_alloc` on the enclosing scope region (`cg_enclosing_region_expr`), mirroring
 * `emit_slice_of_len`, so the per-scope drop (RM-C6) reclaims it — replacing the never-freed raw
 * `malloc(...)` this branch emitted before. The spread branch (RM-C3's `b"…"`+`..str` idiom) is
 * unchanged. Layout of the emitted backing is identical (`{ptr,len}`, zero-init preserved); only the
 * allocation call symbol changes, so the emit delta is exactly `malloc(...)` → `region_alloc(...)`.
 *
 * @param buf       the emit buffer
 * @param prog      the codegen program context
 * @param e         the typed expression (source span, for temp naming)
 * @param a         the typed array-literal node
 * @param escaping  the set of names that escape the current frame (governs region choice)
 * @param regions   the enclosing region-frame stack
 * @param fn_body   the function body (for defer/return context)
 * @param dctx      the defer context
 * @return          `buf` with the literal emitted over `region_alloc`, or an emit error
 * @throws          when the enclosing region cannot be resolved or the byte total overflows
 * @since 0.3.1
 */
fn emit_array_lit(buf: Cb, prog: CgProg, e: checker::TExpr, a: checker::TArrayLit, escaping: []str, regions: []RegionFrame, fn_body: []@TStatement(), dctx: DeferCtx): Cb | error
```

2. **Escape-aware region choice.** If the literal ESCAPES the current frame (its name is in
   `escaping`), it must allocate in the region it is moved to, not the frame-local region that RM-C6
   drops — reuse the SAME `escaping`/`regions` logic `emit_slice_of_len` uses to pick the region. Do not
   drop an escaping literal; promote it.
3. **Kill the raw `malloc`.** After the flip, the non-spread branch emits ZERO `malloc(` for the
   literal backing. (The `malloc`/`abort` C-root itself is not removed here — that is the S16/RM-C9
   expurgo; this crumb only stops EMITTING it for array literals.)
4. **Fixpoint delta.** The emitted `teko.c` changes (allocation symbol) → `gen1 ≠ gen0`, then converge
   `gen2 == gen3`. Gate HARD on `gen2 == gen3` + MEM_PARANOID clean (an escaping literal wrongly dropped
   would crash, not merely leak).

## Rulings & laws

- **Teko-only:** codegen emit logic in `.tks`; arena runtime is Teko over mmap.
- **W15 full Javadoc** on `emit_array_lit`; flatten; no inline `//`.
- **Owner (`reducao §6 C7`):** the non-spread array literal is a never-freed `malloc` leak; route it
  through the scope region like `emit_slice_of_len` — the arena-per-scope drop (RM-C6) then reclaims it.
- **Escape soundness:** an escaping literal allocates in its destination region and is moved, never
  dropped — rides the landed escape machinery, identical to `emit_slice_of_len`.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  reseed ONLY at the fixpoint; `gen2==gen3` byte-identical; sweep `.tkt`/`.tkr` after the emit-shape change.

## Fixtures

none — the fixpoint self-build exercises this. The compiler emits array literals throughout its own
codegen, so the `region_alloc` route is driven by the self-build; `gen2==gen3`, a crash-free
MEM_PARANOID tree, and the falling peak-guard ARE the regression.

## Gate

`[fixpoint]` — build gen2 (`--no-verify --release`, `TEKO_BACKEND=c`, `TEKO_CC=clang`,
`ulimit -v 6815744`) + scoped regression + `gen2==gen3` byte-identity. "Green" = the emitted `teko.c`
non-spread array-literal backing is `region_alloc(...)` (zero `malloc(` for literals), the 3-gen ladder
converges, MEM_PARANOID exits 0, and the peak holds near ~0.9 GB. Reseed-class: `fixpoint-rebuild`.

## Deps

`RM-C6`.

## Done when

`emit_array_lit`'s non-spread branch allocates the literal backing via `region_alloc` on the scope
region (no raw `malloc`), escaping literals are promoted not dropped, `gen2==gen3` holds, MEM_PARANOID is
clean, and the never-freed literal leak is closed.
