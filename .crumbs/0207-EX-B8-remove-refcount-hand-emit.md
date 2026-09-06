---
seq: 0207
crumb-id: EX-B8
milestone: M3
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [EX-A0, COL-F0c, COL-F0d]
sources:
  - "docs/design/expurgo-intrinsecos-inlines-0.3.1.md:2"
  - ".crumbs/0023-COL-F0c-wrapped-retain-release.md:0"
---

# 0207 · EX-B8 — surfaciar/remover hand-emit de refcount (retain/release/weak_get/deep_copy)

> `retain`/`release`/`weak_get`/`deep_copy` são **hand-emit inline** (codegen 3742–3745,
> `emit_retain`/`emit_release`/`emit_weak_get`/`emit_deep_copy`) — SEM superfície Teko. Grupo B, mas
> **NÃO é §16-libc**: é o runtime de refcount da COL-F0c/F0d. **BLOQUEADO no COL-F0c/F0d.**

## Goal (design-ahead)

Quando a COL-F0c (wrap-refcount table, redesign 0123) e COL-F0d (weak/deepcopy/CAS) definirem os corpos
de superfície `retain`/`release`/`weak_get`/`deep_copy`, substituir o hand-emit por chamada genérica e
remover os espelhos native. Elimina C-hand-emit escondido (endgame D161).

## Where

- `src/codegen/codegen.tks:3742-3745` — `emit_retain`/`emit_release`/`emit_weak_get`/`emit_deep_copy`.
- `src/lir/lower.tks` — refcount paths.
- Corpo: a superfície de refcount (COL-F0c redesign 0123 / COL-F0d 0024).

## How

1. **Bloqueado:** aguardar COL-F0c/F0d corpo de superfície de refcount.
2. Trocar hand-emit por resolução genérica; remover native.
3. Fixpoint + ASan + reseed.

## Rulings & laws

- **Teko-only. D161 (hand-emit → superfície). COL-F0c/F0d. D148.**
- **W15; sem `//`.**
- **Safety:** `ulimit -v 4718592`; `gen2==gen3` + ASan+UBSan (D166); ratchet D68; reseed (D164).

## Fixtures

`none — the fixpoint self-build exercises this` (o compilador usa wrap/weak/deepcopy; oráculos, se houver,
são da COL-F0c/F0d).

## Gate

`[fixpoint]` — `gen2==gen3` + ASan+UBSan limpo. Reseed-class: `fixpoint-rebuild`.

## Deps

`EX-A0`, **COL-F0c (0023/0123) + COL-F0d (0024)** — HARD BLOCK.

## Done when

Zero hand-emit `emit_retain`/`emit_release`/`emit_weak_get`/`emit_deep_copy` (viram call genérico) e
`gen2==gen3`.
</content>
