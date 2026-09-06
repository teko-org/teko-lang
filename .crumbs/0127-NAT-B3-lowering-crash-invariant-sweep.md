---
seq: 0127
crumb-id: NAT-B3
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [NAT-A1]
sources:
  - "docs/design/native-lowering-cobertura-zero-libc-0.3.1.md:0-260"   # campaign map (métrica, classe b, raiz retorno-agregado/sret)
  - "src/backend/objfile_elf.tks:398"                                  # emit_elf_shdr — fat []byte return
  - "src/backend/objfile_elf.tks:425"                                  # emit_elf_shdrs — chains fat []byte (x86@933 hang/OOM)
  - "src/backend/dwarf.tks:508"                                        # debug_info_locals returns DwLocalsInfo (arm64@90 stack-smash)
  - "src/build/assemble.tks:28"                                        # asm_module_items returns []parser::Item (win64@1145 sret)
  - "src/lir/lower.tks:3595"                                           # variant_member_index_of — the (internal) offender
  - "src/lir/lower.tks:3605"                                           # variant_member_type_matches — _ => false at :3613 drops Null/Func/Void/agg
  - "src/lir/lower.tks:3602,3819"                                      # the false "(internal)" error sites
---

# 0127 · NAT-B3 — classe (b): retorno-agregado/sret por-ABI + invariante-errado + sweep

> Corrigir os bugs de lowering INTERNOS que matam gen2 cedo — a raiz única de RETORNO DE AGREGADO/FAT
> exposta em 3 ABIs (x86 `[]byte` cadeia, arm64 struct sret, win64 slice sret) + o invariante-errado de
> membro-de-variante — e varrer os `(internal)` restantes por fronteira.

## Goal

O CI #1135 mostra que as três pernas que compilam morrem no MESMO ponto de design — **retorno de
agregado/fat** — cada uma numa ABI: `emit_elf_shdrs` (`objfile_elf.tks:425`) encadeia `[]byte`-return e dá
HANG/OOM (x86@933); `debug_info_locals` (`dwarf.tks:508`) retorna o struct `DwLocalsInfo` por sret e dá
stack-smash (arm64@90); `asm_module_items` (`assemble.tks:28`) retorna slice `[]parser::Item` por sret e dá
VERDICT FAILED (win64@1145). MAIS o invariante-errado de `variant_member_index_of` (`lower.tks:3595`), que
emite `(internal)` falso porque `variant_member_type_matches` (`:3605`, `_ => false` em `:3613`) não casa
membros `Null`/`Func`/`Void`/agregado. Este crumb impõe uma disciplina única de **dest-passing (DPS)** para
agregados/fat por-ABI e conserta o invariante — destravando o stream native. É a classe (b): NÃO grepável por
`not yet lowered` (são `(internal)`/hang/crash). Byte-preserving na rota C (native-only lowering); drives
fixpoint-rebuild.

## Where

- `src/backend/objfile_elf.tks:398` `emit_elf_shdr` / `:425` `emit_elf_shdrs` — o alvo do HANG/OOM x86@933:
  o threading `b = emit_elf_shdr(b, …)` (retorno `[]byte`) deve lowerar por DPS sem realloc/cópia por chamada.
- `src/backend/dwarf.tks:508` `debug_info_locals` (retorna `DwLocalsInfo`, struct em `:426`) — alvo do
  stack-smash arm64@90: retorno-agregado por sret AAPCS64 (`x8`), tamanho exato, guard de overrun.
- `src/build/assemble.tks:28` `asm_module_items` (retorna `[]parser::Item`) — alvo do VERDICT FAILED win64@1145:
  retorno slice-agregado por sret Win64 com shadow-space correto.
- `src/lir/lower.tks:3605` `variant_member_type_matches` (`_ => false` em `:3613`) — ESTENDER para casar
  `Null`, `Func`, `Void` e agregado.
- `src/lir/lower.tks:3595` `variant_member_index_of` + os sítios `:3602`/`:3819` — o `(internal)` deixa de
  disparar quando o match acima passa a cobrir os membros faltantes.
- `src/lir/lower.tks` / `src/backend/abi_*.tks` — a disciplina DPS de retorno-agregado/fat (o caller aloca o
  slot destino na sua arena e passa o ponteiro; o callee escreve NO destino). Espelha por-ABI: SysV, AAPCS64
  (x8/sret), Win64 (sret+shadow-space). Cobre os honest-stops "fat-typed lambda return" (`lower.tks:2824`/
  `:2859`/`:6020`).

## How

1. **DPS de retorno-agregado/fat (a raiz).** Definir a regra única: toda fn cujo retorno é agregado
   (struct/slice `[]T`/fat `{ptr,len}`) recebe um ponteiro-destino oculto (arena do caller) e escreve o
   resultado NELE; NÃO aloca nem copia por chamada. Encadeamento `b = f(b, …)` reusa o MESMO destino.

```teko
/**
 * lower_aggregate_return — lower a function whose result is an aggregate/fat value via dest-passing:
 * the caller's destination slot is passed as a hidden pointer and written in place, so a chained
 * `b = f(b, …)` reuses one destination with no per-call realloc or copy.
 *
 * @param ctx   the lowering context (carries the caller arena + ABI descriptor)
 * @param fn    the callee whose aggregate/fat result is being lowered
 * @param dest  the caller-owned destination slot the callee writes into
 * @return      the lowered call with the hidden dest pointer wired, or an error if the ABI has no
 *              aggregate-return descriptor for this target
 * @throws      when the target ABI descriptor is missing a sret/x8/shadow rule
 * @since 0.3.1
 */
exp fn lower_aggregate_return(ctx: LowerCtx, fn: checker::TFunc, dest: LDest): Lowered | error
```

2. **Por-ABI (materialização).**
   - **SysV x86_64:** agregado ≤16 bytes em pares de registrador; >16 via ponteiro-destino. O `[]byte` de
     `emit_elf_shdrs` é `{ptr,len}` → dest-passing, sem realloc → mata o HANG@933.
   - **AAPCS64 (arm64):** agregado grande via `x8` (indirect result). `DwLocalsInfo` escreve no slot de `x8`
     com tamanho EXATO + guard de overrun no encoder → mata o stack-smash@90.
   - **Win64:** agregado >8 bytes via ponteiro-destino oculto (sret) com shadow-space de 32 bytes reservado →
     `[]parser::Item` retorna correto → mata o VERDICT FAILED@1145.

3. **Invariante de membro-de-variante.** Estender `variant_member_type_matches` (`:3605`):

```teko
/**
 * variant_member_type_matches — whether a declared variant member type matches a value's type,
 * now covering Null (null-as-union), Func (closure), Void and aggregate members that the prior
 * `_ => false` dropped, so `variant_member_index_of` no longer raises a false `(internal)`.
 *
 * @param member    the declared arm type from the variant definition
 * @param value_ty  the value's resolved type being placed into the union
 * @param variants  the registered variant definitions for named-arm resolution
 * @return          true when the value type belongs to this arm
 * @since 0.3.1
 */
fn variant_member_type_matches(member: @Type(), value_ty: @Type(), variants: []LVariantDef): bool
```

Adicionar arms: `checker::Null => match value_ty { checker::Null => true; _ => false }`;
`checker::Func => match value_ty { checker::Func => true; _ => false }`;
`checker::Void => match value_ty { checker::Void => true; _ => false }`; agregado por igualdade estrutural.

4. **Sweep frontier-driven.** Após P1.1/P1.2 destravarem o stream, RE-RODAR a emissão gen2 native por-arch;
   o próximo `(internal)` que o `item N/TOTAL` alcançar é o próximo alvo. Repetir corrigindo cada
   primeiro-ofensor por-arch até o stream avançar bem além dos ~1145 (os `(internal)` restantes se acumulam no
   mesmo reseed de fase — native não roda, sem pressa de validar isolado).

## Rulings & laws

- **Teko-only:** `src/lir/*.tks` + `src/backend/*.tks`; sem C twin. LIR/backend in-memory.
- **Comment convention (W15, owner 2026-08-19):** `/** */` só em `exp`; sem `//`; doc nunca maior que o código.
- **Fork protocol (owner 2026-08-19):** a raiz retorno-agregado/sret e o invariante-membro são BUGS medidos
  (CI #1135), não forks — corrigir, NÃO HALT. A regra de layout de agregado/união já está ratificada (Doc-2
  per-type memory model; D1-T1).
- **W15 full Javadoc** em toda decl nova `exp`; flatten com early-return; sem `//`.
- **NÃO detectar o inexistente (dono):** o `(internal)` de `variant_member_index_of` era detecção de
  caso-impossível ERRADA — corrigir a cobertura do match é a causa-raiz, não reword da mensagem.
- **Safety:** NUNCA `teko test .`; build em subshell `ulimit -v 4718592` (4,5 GiB); estouro é causa-raiz,
  nunca teto maior; commit por ofensor corrigido; fixpoint C `gen2.c==gen3.c` byte-idêntico (native-only
  lowering, o artefato C não muda); sweep após mudança de layout. **Native é WRITE-ONLY** — o gen2 native é
  emitido (item→TOTAL), NÃO executado.
- Ruling-base: campanha `native-lowering-cobertura-zero-libc-0.3.1.md` §1-§3 + CI #1135 (medição empírica).

## Fixtures

`none — o self-build fixpoint exercita isto`. Os três ofensores (`emit_elf_shdrs`, `debug_info_locals`,
`asm_module_items`) e `variant_member_index_of` são funções que o PRÓPRIO compilador roda ao se compilar (o
fixpoint C os exercita); o gate native é o `item N/TOTAL` chegar a TOTAL, não um oráculo `.tkr`. Nenhum
fixture novo.

## Gate

`[fixpoint]` — build gen2 (rota C) + `gen2.c==gen3.c` byte-idêntico. "Green" native (write-only) = `gen1`
emite gen2 native COMPLETO (`item == TOTAL`) nas 4 pernas, sem HANG/OOM/stack-smash/VERDICT-FAILED e sem
`(internal)` falso; o `emit_elf_shdrs`/`debug_info_locals`/`asm_module_items` passam. Reseed-class:
`fixpoint-rebuild` (rides R#1/destrava e R#3/resíduo).

## Deps

`NAT-A1` (0097) — a fronteira de lowering LIR/ABI que este crumb estende. Roda em R#1 (F7a threads) para
destravar antes de tudo; o resíduo frontier-driven cai em R#3 (F7b canais).

## Done when

As 4 pernas do CI emitem gen2 native até `item == TOTAL` (write-only) — nenhum retorno-agregado/fat trava ou
corrompe, e `variant_member_index_of` não dispara `(internal)` falso — com o fixpoint C byte-idêntico.
