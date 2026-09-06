---
seq: 0174
crumb-id: INTR-C3
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [INTR-C2]
sources:
  - "docs/design/embed-vfs-sweep-integration-0.3.1.md:6"        # §6 nature 2
  - "DECISION_LOG.md:1155"                                       # D134 nature 2 (C-inline magic)
  - "src/checker/typer.tks:888,928"                             # wrap/unwrap (the reinterpret surface, landed)
---

# 0174 · INTR-C3 — nature-2 (C-inline magic-of-name) → surface primitives

> Nature 2 of the wave (D134): the C-inline "magic" recognized by name (`floor`/`memcpy`/`__atomic_*`/
> reinterpret) that is NOT libc but IS magic-of-name → turn each into a surface primitive. Reinterpret is
> already surface (`wrap`/`unwrap`, landed) → those sites become USE, not new machinery. Byte-mover →
> RITUAL; ratchet DOWN.

## Goal

For each nature-2 intrinsic (INTR-C1 census): expose it as a surface primitive so the backend recognizes a
SURFACE op, not an unnamed magic. `floor`/`memcpy`/`__atomic_*` become named surface primitives (raw, dev-
owns-safety); reinterpret is already `wrap`/`unwrap` (typer.tks:888/928) → migrate its sites to the
surface op (the W6 reball's cousin — but here for the intrinsic sites, not the region reball). After this,
the only backend magic left is the nature-3 raw `syscall` floor (INTR-C1). Codegen-C stays a crutch;
lower.tks is the permanent seat.

## Where

- `src/codegen/codegen.tks` / `src/lir/lower.tks` — the nature-2 recognized names → surface-primitive
  recognition (a `teko::…` op with a raw lowering), not an unnamed inline.
- The surface-primitive decls (in `teko::sys`/`teko::runtime`) — `exp` raw primitives (floor/memcpy/
  atomics), dev-owns-safety (consistent with §6 aposentar-`unsafe`).
- Reinterpret sites → `wrap`/`unwrap` (already landed).

## How

1. Per nature-2 name, add the `exp` surface primitive + reroute the backend recognition to it.
2. Reinterpret: migrate to `wrap`/`unwrap` at the intrinsic sites.
3. Both routes (codegen-C exercised, lower.tks written).
4. Confirm the residual magic set = ONLY the nature-3 `syscall` floor (the one irreducible surface op).

## Rulings & laws

- **Teko-only.**
- **D134 Opção-3 + D131:** nature-2 becomes surface primitives, `exp`, exposed (no gate, dev-owns-safety);
  the only remaining floor is the raw `syscall`.
- **Own wave, not tangled with region byte-mover.**
- **W15 full Javadoc; no `//`.**
- **Safety:** never `teko test .`; subshell `ulimit -v 4718592`; **RITUAL** — full ladder, `gen2==gen3`,
  MEM_PARANOID. Ratchet: DOWN strict.

## Fixtures

`none — the primitives are exercised by the self-build; the fixpoint proves behavior-preservation`.

## Gate

`[RITUAL]` — full ladder; nature-2 intrinsics are surface primitives, only the raw `syscall` floor
remains as backend-recognized, `gen2==gen3`, peak down. "Green" = zero unnamed C-inline magic left (only
the one surface `syscall` floor), `gen2==gen3`. Reseed-class: `fixpoint-rebuild`.

## Deps

`INTR-C2`

## Done when

Every nature-2 C-inline magic is a surface primitive (reinterpret via wrap/unwrap), only the raw
`syscall` floor remains backend-recognized, and the RITUAL gate is green with peak down.
