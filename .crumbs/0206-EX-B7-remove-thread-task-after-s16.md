---
seq: 0206
crumb-id: EX-B7
milestone: M3
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [EX-A0, S10-RT]
sources:
  - "docs/design/expurgo-intrinsecos-inlines-0.3.1.md:2"
  - ".crumbs/0116-S10-RT-threads-channels-intent-runtime.md:0"
---

# 0206 · EX-B7 — remover name-detect de thread_clone/task_reset após S10-RT/§16

> `thread_clone` (C `tk_thread_clone`, codegen 3778) e `task_reset` (C `tk_task_reset`, 3800). Grupo B =
> threads runtime (S10-RT 0116 + §16 threads). `thread_clone` é `clone`-syscall-classe → o corpo Teko é
> `syscall`-backed (carve-classe) ou superfície sobre syscall. **BLOQUEADO no S10-RT.**

## Goal (design-ahead)

Quando o S10-RT landar o runtime de threads (região-por-thread, `thread.tks` já tem stack_new/free), o
`thread_clone` vira chamada à superfície sobre `syscall` (`thread.tks`); `task_reset` idem. Remover os
name-detects C.

## Where

- `src/codegen/codegen.tks:3778` (`thread_clone`), `3800` (`task_reset`).
- `src/lir/lower.tks` — thread/task paths em `native_builtin_symbol`.
- Corpo: `src/runtime/thread.tks` (stack já lá; clone via syscall no S10-RT).

## How

1. **Bloqueado:** aguardar S10-RT (thread runtime) + §16 clone-via-syscall.
2. Remover name-detect das duas rotas; rotear à superfície.
3. Fixpoint + ASan + reseed.

## Rulings & laws

- **Teko-only. D161. S10-RT / §16 threads. D130 (região-por-thread = param, não TLS). D148.**
- **W15; sem `//`.**
- **Safety:** `ulimit -v 4718592`; `gen2==gen3` + ASan+UBSan (D166); ratchet D68; reseed (D164).

## Fixtures

`none — the fixpoint self-build exercises this` (spawn/task do compilador; corpo é do S10-RT).

## Gate

`[fixpoint]` — `gen2==gen3` + ASan+UBSan limpo. Reseed-class: `fixpoint-rebuild`.

## Deps

`EX-A0`, **S10-RT (0116) + §16 clone** — HARD BLOCK.

## Done when

Nenhum `tk_thread_clone|tk_task_reset` name-detect em codegen/lower e `gen2==gen3`.
</content>
