---
seq: 0128
crumb-id: NAT-B4
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RT-ENTRY]
sources:
  - "docs/design/native-lowering-cobertura-zero-libc-0.3.1.md:0-260"   # campaign map (eixo A — emissões próprias)
  - "src/codegen/codegen.tks:9606"                                     # cg_emit_thread_clone_helper_text_x86_64 (x86 clone)
  - "src/codegen/codegen.tks:9616"                                     # #error aarch64 thread_clone not yet validated
  - "src/lir/lower.tks:6717"                                           # native_entry_stub (native entry scaffolding)
  - "src/lir/lower.tks:1970"                                           # builtin_cov_symbol (cov mirror DONE)
  - "src/lir/lower.tks:1771,1786"                                      # lower_load_u8_call / lower_store_u8_call (DONE)
---

# 0128 · NAT-B4 — espelho native das emissões próprias da campanha zero-libc

> Escrever o espelho native das emissões que a campanha zero-libc ELA MESMA introduziu — clone aarch64,
> `_start`/`stack_ptr` per-OS, spawn-mmap glue — para que "todo C emitido seja expressável em native".

## Goal

A campanha zero-libc passou a emitir C próprio (threads, entry per-OS, spawn glue) cujo espelho native
precisa EXISTIR (write-only) pela lei "todo C expressável em native" (dono; D106 antecipar-nunca-deferir). O
que JÁ tem espelho native (verificado): `cov_*` (`lower.tks:1970` `builtin_cov_symbol`), `load_u8`/`store_u8`
(`lower.tks:1771`/`:1786`), syscallN/atomics. O que FALTA: (1) `thread_clone` **aarch64** (hoje `#error` em
`codegen.tks:9616`; o x86_64 existe em `codegen.tks:9606`); (2) o intrínseco `stack_ptr` + o naked `_start`
per-OS (o esqueleto native `native_entry_stub` existe em `lower.tks:6717`, o C-side ainda não landou — crumb
0125). Este crumb escreve esses espelhos. Write-only no native (não roda antes de F9); a rota C que ele
completa drives fixpoint-rebuild.

## Where

- `src/codegen/codegen.tks:9616` — o `#error` aarch64 `thread_clone` — SUBSTITUIR pelo asm-inline aarch64
  (espelho do `cg_emit_thread_clone_helper_text_x86_64` em `:9606`, usando `svc #0` + registradores AAPCS64).
- `src/lir/lower.tks:6717` `native_entry_stub` — completar o `_start` LFunc per-OS (o stub existe; wire do
  argc/argv/envp a partir do topo da pilha do SO).
- `src/lir/lower.tks` — NOVO arm de isel para o intrínseco `stack_ptr`: `mov reg,%rsp` (x86_64) /
  `mov reg,sp` (aarch64).
- `src/lir/lower.tks` — spawn-mmap glue native: a chamada `SYS_MMAP` (Linux) / `VirtualAlloc` extern
  (Windows) para a stack da thread — já lowerável como extern-call/syscall; wire ao caminho de spawn.
- (referência, NÃO tocar — já FEITO) `lower.tks:1970` `builtin_cov_symbol`, `lower.tks:1771`/`:1786`
  `lower_load_u8_call`/`lower_store_u8_call`.

## How

1. **Clone aarch64.** Espelhar o helper x86_64 (`codegen.tks:9606`) em aarch64 asm-inline: `svc #0` para o
   syscall `clone`, empurrar `entry`/`arg` na stack da child, `ret`/exit no child. Sob o `#os aarch64 linux`
   split que hoje é `#error` (`:9616`).

```teko
/**
 * cg_emit_thread_clone_helper_text_aarch64 — the aarch64 Linux `tk_thread_clone` helper mirroring the
 * x86_64 one: raw `clone` via `svc #0`, pushing entry/arg onto the child stack and exiting the child.
 *
 * @return  the C text of the aarch64 `tk_thread_clone` inline-asm helper
 * @since 0.3.1
 */
fn cg_emit_thread_clone_helper_text_aarch64(): str
```

2. **`_start` per-OS (native).** Completar `native_entry_stub` (`lower.tks:6717`): emitir o naked `_start`
   que lê argc/argv/envp do topo da pilha (Linux/mac) ou chama o entry Win64, e salta para o `main` do Teko.
   O C-side correspondente é do crumb 0125 (RT-ENTRY) — este crumb entrega o LADO NATIVE.

3. **`stack_ptr` intrínseco.** Novo arm de isel: o intrínseco `stack_ptr` lowera para `mov reg,%rsp`
   (x86_64) / `mov reg,sp` (aarch64) — um único MInst, sem call.

```teko
/**
 * lower_stack_ptr — lower the `stack_ptr` intrinsic to a single move from the stack-pointer register
 * (`%rsp` on x86_64, `sp` on aarch64); no call, one machine instruction.
 *
 * @param ctx  the lowering context (carries the target arch)
 * @param e    the intrinsic call expression
 * @return     the lowered single-move instruction, or an error on an unsupported target
 * @throws     when the target arch has no stack-pointer register mapping
 * @since 0.3.1
 */
exp fn lower_stack_ptr(ctx: LowerCtx, e: checker::TExpr): Lowered | error
```

4. **Spawn-mmap glue.** Wire da alocação de stack da thread: `SYS_MMAP` (Linux) via syscall intrínseco já
   lowerável; `VirtualAlloc` (Windows) via extern-call `from "kernel32"` (D106 antecipa o Windows). Nenhuma
   primitiva nova — reusa o caminho de syscall/extern existente.

## Rulings & laws

- **Teko-only:** `src/codegen/*.tks` + `src/lir/*.tks`; sem C twin novo. Runtime em Teko (sem `from "teko_rt"`).
- **Comment convention (W15, owner 2026-08-19):** `/** */` só em `exp`; sem `//`; doc nunca maior que o código.
- **Fork protocol (owner 2026-08-19):** o Windows é ANTECIPADO (D106) — `VirtualAlloc`/`CreateThread` via
  `from "kernel32"` resolvem AGORA (precedente `sync.tks`/S16-SYNC verde); a thread-create do mac é
  `pthread_create from "System"` (D105 T2 / D101 libSystem-como-ABI). Sem fork aberto → NÃO HALT.
- **W15 full Javadoc** em toda decl nova `exp`; flatten; sem `//`.
- **Safety:** NUNCA `teko test .`; build em subshell `ulimit -v 4718592` (4,5 GiB); estouro é causa-raiz,
  nunca teto maior; commit por passo verde; fixpoint C `gen2.c==gen3.c` byte-idêntico; **native WRITE-ONLY**
  (o clone aarch64 / `_start` / spawn-mmap compilam no self-build, NÃO rodam antes de F9). **Ratchet D68:** o
  runtime native não pode CRESCER o pico vs. o C que espelha.
- Ruling-base: dono "todo C expressável em native" + D106 (antecipar Windows) + campanha
  `native-lowering-cobertura-zero-libc-0.3.1.md` §2 eixo A.

## Fixtures

`none — o self-build fixpoint exercita isto` na rota C (cov/load_u8/store_u8 já FEITOS são rodados pelo
compilador; clone/spawn/entry são CEGOS ao dry-build — o compilador é single-thread e o `_start` só roda no
binário final). As fixtures de plataforma de thread/entry pertencem a F7a/0125 (D105 T5 — surfacear ao dono);
este crumb não adiciona oráculo próprio.

## Gate

`[fixpoint]` — build gen2 (rota C) + `gen2.c==gen3.c` byte-idêntico; o `#error` aarch64 de `thread_clone`
some e o `stack_ptr`/`_start`/spawn-mmap native compilam no self-build. "Green" native (write-only) = os
espelhos entram no gen2 native emitido sem parar o stream. Reseed-class: `fixpoint-rebuild` (rides R#1/F7a
para clone-aarch64+spawn e R#4/entry-native-0125 para `_start`/`stack_ptr`).

## Deps

`RT-ENTRY` (0125) — o `_start`/`stack_ptr` per-OS de que o lado native é espelho; o clone-aarch64 e o
spawn-mmap ridem R#1/F7a. Roda nos reseeds de fase, não abre reseed próprio.

## Done when

O `#error` aarch64 de `thread_clone` sumiu, `_start`/`stack_ptr`/spawn-mmap têm espelho native que compila no
self-build (write-only), e o fixpoint C é byte-idêntico.
