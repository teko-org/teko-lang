---
seq: 0172
crumb-id: INTR-C1
milestone: M5
gate: "[dry]"
reseed-class: "none"
deps: []
sources:
  - "docs/design/embed-vfs-sweep-integration-0.3.1.md:6"        # §6 intrinsics wave
  - "DECISION_LOG.md:1151,1155"                                  # D134 3 naturezas + censo
  - "src/checker/typer.tks:888,928"                             # wrap/unwrap ALREADY landed (drift)
  - "src/sys/marshall.tks:8,16"                                 # ptr/uptr newtypes landed
---

# 0172 · INTR-C1 — census classify (3 naturezas) + the ONE raw-syscall surface primitive; confirm wrap/unwrap landed

> Open the intrinsics→surface wave (D134) — its OWN wave, NOT tangled in the region byte-mover. Classify
> every backend-recognized intrinsic into the 3 naturezas (codegen 90 names/92 sites, lower 32, overlap
> zero) and land the ONE irreducible floor: a raw `syscall` surface primitive. CONFIRM `wrap`/`unwrap` are
> already surface intrinsics (drift — the census assumed they were pending). Inventory + one primitive →
> inert on emitted bytes.

## Goal

Deliver the wave's map + floor (annex §6): (1) a classified census — nature 1 (`tk_*` calls = dep-C →
expunge), nature 2 (C-inline magic-of-name: `floor`/`memcpy`/`__atomic_*`/reinterpret → surface), nature
3 (`syscall`/raw-emit = irreducible SO floor → ONE raw surface primitive). (2) Ensure the nature-3 floor
is a single exposed `syscall` primitive of surface (it already is raw-emit; formalize it as the one
irreducible surface op). (3) CONFIRM `wrap`/`unwrap` are landed (`typer.tks:888` `type_ptr_unwrap`, `:928`
`type_ptr_wrap`) → the mass USE (reball) is W6/this wave, the MACHINERY is done (correct the D132/W6
expectation).

## Where

- The census document (this crumb's artifact — a classified list, into the annex or a scratch inventory):
  each codegen/lower intrinsic → nature 1/2/3 + target disposition.
- `src/codegen/codegen.tks` / `src/lir/lower.tks` — the `syscall`/raw-emit floor: confirm it is one
  surface primitive; no new magic.
- `src/checker/typer.tks:888,928` — CONFIRM wrap/unwrap intrinsics (no change; drift note).

## How

1. Enumerate the 90 codegen + 32 lower recognized names; tag each nature 1/2/3.
2. Nature 3: identify the raw `syscall` primitive; ensure it is the single irreducible surface op (per-OS
   raw-emit) — the floor that does NOT disappear.
3. Confirm wrap/unwrap landed; record that W6 + INTR-C2/C3 are USE/expunge, not machinery.
4. NO code migration here — inventory + the floor formalization only (inert on emitted bytes).

## Rulings & laws

- **Teko-only.**
- **D134 Opção-3:** floor = ABI/syscall/linker, zero libc, zero magic; nature-3 `syscall` is the one
  irreducible surface primitive; codegen-C is the crutch (F9 finalizes native).
- **Own wave, NOT tangled with region byte-mover.**
- **W15 full Javadoc; no `//`.**
- **Safety:** never `teko test .`; subshell `ulimit -v 4718592`; inventory + floor → full gate byte-
  identical.

## Fixtures

`none — inventory + a formalized existing primitive; nothing new to reject`.

## Gate

`[dry]` — compiles; the census is a doc + the floor is the existing primitive → full gate byte-identical.
"Green" = every intrinsic classified 1/2/3, the raw `syscall` floor named as the one surface primitive,
wrap/unwrap confirmed landed. Reseed-class: `none`.

## Deps

`—`

## Done when

The intrinsic census is classified into the 3 naturezas, the single raw-syscall surface primitive is
formalized, wrap/unwrap is confirmed already-surface, and the self-build is byte-identical.
