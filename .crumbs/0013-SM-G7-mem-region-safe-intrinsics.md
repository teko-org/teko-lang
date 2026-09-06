---
seq: 0013
crumb-id: SM-G7
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: ["SM-G5"]
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:395-404"  # §6.5.2 C/D covered as safe
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:449-451"  # §6.5.4 step 1
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1181-1182"# §10 Phase G — G7
---

# 0013 · SM-G7 — reclassify `teko::mem` + region primitives as SAFE intrinsics

> Reclassify `teko::mem` + region primitives as SAFE intrinsics (`__wrap` supplies check).

## Goal

The `teko::mem` FFI/marshalling builtins (`as_ptr`/`as_cstr`/`str_from_c`/`bytes_from_ptr`/`store_u64`/
`load`/`buf_ptr`/`region_buf`) and the arena/region primitives (`region_alloc`/`region_new`/
`region_drop`) are "unsafe by contract" today ONLY because the CHECKER cannot statically prove liveness
— which is exactly what SM-G5's `__wrap<T>()` DYNAMIC check now provides. This crumb DROPS their
`unsafe`-by-contract status and re-exposes them as SAFE intrinsics: `buf_ptr`/`as_cstr` bump-allocate
INTO an arena region (arena-backed, arena-bounded), `bytes_from_ptr` reads back a bounded `[]byte` copy,
and the region primitives are TRUSTED backend intrinsics (a region alloc into a live region is safe by
construction). `unsafe` STILL PARSES during this window (a no-op modifier) so the seed builds. It is the
first step of the `unsafe` retirement (§6.5.4 step 1); depends on SM-G5 (the `__wrap` check that makes
the reclassification sound). Byte-preserving; its seed folds into SM-R1.

## Where

- `src/checker/scope.tks:1012-1046` and `:533-551` — the `teko::mem` builtins' "UNSAFE BY CONTRACT"
  classification (`scope.tks:549-551` "cannot prove `addr` names live, aligned, in-bounds memory") — DROP
  the unsafe-by-contract status; re-expose as SAFE intrinsics that yield/consume the opaque `ptr`.
- The arena/region primitives (`region_alloc`/`region_new`/`region_drop`, `scope.tks` + `teko_rt`
  `tk_region_*`) — reclassify as SAFE trusted intrinsics (the exempt backend zone, §7.4 / §6.5.2 D).
- `src/checker/*` — leave the `unsafe` keyword PARSING (no-op window); its DELETION is SM-S5 (`0092`, M3).

## How

1. **Drop the unsafe-by-contract flag on C-class builtins** (`scope.tks:1012-1046`, `:533-551`): the
   `teko::mem` marshalling builtins are re-exposed as SAFE intrinsics. `__wrap` (SM-G5) supplies the
   dynamic null + arena-liveness + tag check the checker cannot do statically — so a `buf_ptr`→C→read-back
   round-trip in a NON-`unsafe` fn now compiles. `buf_ptr`/`as_cstr` results are arena-backed and
   arena-bounded (safe on the Teko side by §0); `bytes_from_ptr` reads a bounded `[]byte` copy.
2. **Reclassify the region primitives as trusted intrinsics** (§6.5.2 D): `region_alloc`/`region_new`/
   `region_drop` ARE the arena machinery that IMPLEMENTS safety — the same trusted-backend zone the DI
   resolver lives in. A region alloc into a live region is safe by construction; expose them as safe
   intrinsics, no user `unsafe`.
3. **Keep `unsafe` parsing (no-op window).** Do NOT delete the `unsafe` keyword here — steps 2-5 of the
   retirement (SM-G8, SM-S5) land while `unsafe` still parses as a no-op modifier, so the current seed
   keeps building (bootstrap-additive, like the `mut`→`var` soft-deprecation).
4. **Confirm byte-neutrality.** Reclassification changes only WHICH fns the checker admits in a
   non-`unsafe` context; the emitted bytes of already-compiling code are unchanged. `[dry]` build
   byte-identical.

No new `fn`/`type` — this is a classification change on existing builtins; `__wrap` (SM-G5) is the check
it leans on.

## Rulings & laws

- **Teko-only:** checker `.tks`; no C twin (the runtime primitives are unchanged — only their checker
  classification moves).
- **§6.5.2 C/D + §6.5.4 step 1:** C-class marshalling builtins COVERED (safe via arena + `__wrap`);
  D-class region primitives COVERED (trusted intrinsics). `unsafe` stays parsing until SM-S5.
- **W15:** touched declarations keep Javadoc; no `//`.
- **Depends on SM-G5:** `__wrap`'s dynamic check is what makes the reclassification sound — sequence after it.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step.

## Fixtures

The self-build's FFI still runs through the old raw path (migrated in SM-S4), so the NON-`unsafe`
round-trip accept is not self-exercised — one isolated accept fixture:

| fixture | asserts | expected |
|---|---|---|
| `bufptr_ffi_in_safe_fn` | a `buf_ptr`→C→read-back round-trip in a NON-`unsafe` fn compiles | 0 |

## Gate

`[dry]` — compile + `bufptr_ffi_in_safe_fn` + fixpoint (byte-identical). "Green" = the `teko::mem` +
region primitives are SAFE intrinsics (no `unsafe` needed), `unsafe` still parses (no-op), `[dry]` build
byte-identical. Reseed-class: `(folds R1)`.

## Deps

`SM-G5` (the `__wrap` dynamic check the reclassification relies on).

## Done when

The `teko::mem` marshalling builtins and the region primitives are reclassified as SAFE intrinsics
(usable in a non-`unsafe` fn, `__wrap` supplying the dynamic check), `unsafe` still parses as a no-op,
`bufptr_ffi_in_safe_fn` is exit `0`, and a `[dry]` build is byte-identical.
