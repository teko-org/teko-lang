---
seq: 0144
crumb-id: RM-C9d
milestone: M3
gate: "[RITUAL]"
reseed-class: "expurgo"
deps: [RM-C9c]
sources:
  - "docs/design/arena-terminal-zero-libc-0.3.1.md:52-90"
  - "src/build/project.tks:1639-1669"
  - ".crumbs/0095-RM-C9-terminal-c-root-removal.md:43-44"
---

# 0144 · RM-C9d — repoint the u64-handle arena twins off `teko_rt`

> The native fused-emit path still calls the arena through six `tk_region_*_u from "teko_rt"` twins;
> repoint them to the Teko arena so the WHOLE arena surface is teko_rt-free.

## Goal

`project.tks:1639-1649` declares `region_root`/`region_new`/`region_drop`/`region_enter`/`region_leave`/
`region_current` as `extern … = "tk_region_*_u"/"tk_region_leave" from "teko_rt"` — u64-handle twins the
native fused emit uses to drain per-LFunc (`fold_lfunc_scoped_x86` and siblings, `:1658/1911/1930/
2043`). These are the last arena-path `from "teko_rt"` references. The Teko arena exposes ptr-based fns
(`region_root(): ptr`, `region_new(parent: ptr): ptr`, …); this crumb adds thin u64-twin wrappers to
`arena.tks` (bridging via `ptr_word`/`word_ptr`) and repoints `project.tks` to them, deleting the six
externs. Native-path (D52-gated) but compiled into the self-image, so the repoint is required to zero
`from "teko_rt"` even before the native leg runs.

## Where

- `src/runtime/arena.tks` — NEW `exp` u64-twin wrappers over the ptr fns:
  `region_root_u`/`region_new_u`/`region_drop_u`/`region_enter_u`/`region_current_u` (and
  `region_leave` already takes no ptr). Mirror the C `_u` ABI (`uint64_t` in/out).
- `src/build/project.tks:1639-1649` — the six `extern … from "teko_rt"` — REPLACE the `from "teko_rt"`
  bindings with calls to the `teko::runtime::region_*_u` twins (drop the externs; use the module fns).
- `src/runtime/teko_rt.c` — `tk_region_root_u`/`_new_u`/`_drop_u`/`_enter_u`/`_current_u`/`tk_region_
  leave` — dead after the repoint; REMOVED in 0146.

## How

1. Add the u64 twins to `arena.tks` (each bridges the handle through the existing ptr fns — no new
   arena logic):

```teko
/**
 * region_new_u — u64-handle twin of `region_new`, for the native fused-emit path that carries region
 * handles as words. Bridges `parent` and the result through `ptr_word`/`word_ptr`.
 *
 * @param parent  the parent region handle as a word (0 for a rootless region)
 * @return        the fresh region handle as a word
 * @since 0.3.1
 */
exp fn region_new_u(parent: u64): u64 {
    teko::sys::ptr_word(region_new(teko::sys::word_ptr(parent to i64))) to u64
}
```

   …and likewise `region_root_u`/`region_drop_u`/`region_enter_u`/`region_current_u`. `region_leave`
   already has no handle → the native path calls `teko::runtime::region_leave` directly.

2. In `project.tks:1639-1649`, delete the six `extern … from "teko_rt"` and route the call-sites
   (`:1658/1911/1930/2043`) to `teko::runtime::region_*_u` / `region_leave`.

3. Reseed iteratively to `gen2==gen3`. The C-leg self-build does not exercise the native fused path,
   so the twins are compiled-but-inert on the C leg (proven live only when the native leg runs, D52);
   the repoint is byte-faithful for the C-leg corpus.

## Rulings & laws

- **Teko-only + expurgo:** the twins move C→Teko; the `tk_region_*_u` C bodies are REMOVED in 0146, not
  patched (D90).
- **exp (D111):** the u64 twins are consumable arena surface (the native path and any handle-carrying
  consumer) → `exp` with W15 doc.
- **NATIVE gated (D52):** repoint now (it compiles into the self-image), RUN on the native leg later —
  writing `lower.tks`-visible surface, not running it (owner "escreve em Teko, emite em C").
- **Safety:** NEVER `teko test .`; subshell `ulimit -v 4718592`; reseed ONLY at this [RITUAL];
  `gen2==gen3`; leave gen2/gen3 in scratchpad.

## Fixtures

none — the fixpoint self-build compiles the twins; their RUN path is native (D52), validated when the
native leg closes, not by a C-leg fixture.

## Gate

`[RITUAL]` — full native ladder + expurgo reseed. "Green" = `project.tks` binds no `tk_region_*_u`
from C, the arena path is 100% `from "teko_rt"`-free, and `gen2==gen3` byte-identical. Measure
dry-build `peak`: NÃO-CRESCER (D68). Reseed-class: `expurgo`.

## Deps

`RM-C9c`.

## Done when

The six u64-handle arena externs are gone from `project.tks`, replaced by `teko::runtime::region_*_u`
twins, the arena path has zero `from "teko_rt"`, and `gen2==gen3`.
