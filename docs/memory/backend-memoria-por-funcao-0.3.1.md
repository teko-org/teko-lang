# Resumo — escopar a memória do backend nativo por função (0.3.1)

Handoff curto do design completo em `docs/design/backend-memoria-por-funcao-0.3.1.md`.

## O achado que reenquadra a tese
O pipeline nativo NÃO é função-a-função — é MÓDULO-a-módulo em três passes em série
(`select_module_x86`→`regalloc_module_x86`→`encode_module_x86`, `project.tks:2640-2646`). Todo o
MInst de todas as funções coexiste no pico (LIR + MModule não-colorida + MModule colorida). A
fronteira segura da tese (*"LIR/MInst de A morre depois que os bytes de A são anexados"*) **ainda não
existe**; o trabalho é CRIÁ-la fundindo os três passes num laço por função. Confirma o issue:
`grep region_new/region_drop src/backend` = vazio.

## A fronteira
- **Scratch por-função (escopar-e-largar):** LIR do corpo (`lower.tks:12819`), `SelCtxX86`/`vinfo`
  (`isel_x86_64.tks:1875`), stream numerado + `IntervalSet` de regalloc (`regalloc_x86.tks:874-880`),
  buffers de encode (`encode_x86_64.tks:2357`).
- **Persistente (raiz/objeto, nunca escopado):** `prog`; tabelas globais de `lower_program`
  (`lower.tks:12998-13005`); `rodata`/`globals`/`layouts` carregados por-referência
  (`minst_x86.tks:810-820`); tabela de símbolos, relocs, buffer de texto do objeto, `.tsym`.

## A prova de não-escape
Só os BYTES de A persistem, copiados para o buffer do objeto. Exceções nomeadas: **E1** `Symbol.name`/
`RelocX86.sym` (`str`) — têm de originar na raiz (`prog`), não re-internados na janela escopada; a
cópia para os acumuladores raiz acontece antes do drop. **E2** rodata/globals/layouts nunca entram no
scratch. **E3** sem inline cross-função no backend hoje. Não relaxa escape geral — é fronteira de
fase.

## O mecanismo
`tk_alloc` bump-aloca SEMPRE de `tk_region_root()` (`teko_rt.h:118-125`) — não há região-corrente
trocável. `arena_push`/`arena_pop` (únicos builtins por-escopo, `scope.tks:740-742`) são uma pilha de
bump na raiz única e têm ARMADILHA: largam um acumulador intercalado. **Recomendação:** adicionar
`tk_region_enter`/`tk_region_leave` (pilha de região-corrente) em `teko_rt.{c,h}` (C MANTIDA,
permitido) reusando as filhas `tk_region_new`/`tk_region_drop` (chunks separados); scratch → filha,
acumulador → raiz (append após leave), drop da filha. Plano-B: `arena_push/pop` + acumulador na região
de PROGRAMA.

## Medição + portão (gen2 NATIVE — correção do dono)
O build nativo inteiro OOMa a 15,8 GB; o pico do self-build completo só fecha num box >16 GB
(nomeado). Mensurável sem ele: `TEKO_ARENA_OBS` sob `TEKO_BACKEND=native` num projeto pequeno —
aceitação "scoped > 0" e "regiões largadas ≈ nº funções" (hoje 0.0% / 11 de 5007). **Portão:** gen2
buildado com `TEKO_BACKEND=native`, `teko test .` verde, **FIXPOINT gen2==gen3 byte-idêntico**, diff
C-vs-own inalterado.

## Ordem (crumbs)
C0 [feito] commit vazio. C1 runtime enter/leave + builtins (aditivo em `lower.tks`/`scope.tks`). C2
wrappers `pub encode_func_x86`/`encode_func` (aditivo em `encode_*`). C3 cauda fundida x86-linux
(RITUAL: fixpoint+gate native). C4 replicar win/arm64/arm64_linux. C5 [limpeza] retirar passes
module-at-a-time. C6 [Tier 2] lowering por-função (freeing LIR de A). Colisões concentradas em
`project.tks`; ficheiros quentes só tocados aditivamente. Coordenar com `phase_begin` do backend-instr
(âncora natural do enter/leave).

## Impacto a jusante (gargalo do caminho crítico)
Este fix destrava gen2-native → destrava o portão de teste native. Os dois sítios de emissão de teste
são 100% rota-C hoje e não leem `m.backend`: `native_gate_build` (`project.tks:3549`) e
`build_regression_cov_exe` (`project.tks:5523`). Rotear por `Native` é trabalho a jusante — nomeado,
não projetado aqui.

## Estado
Sem HALT. Nenhuma tensão de lei genuína. Único pedido ao dono: HARDWARE >16 GB para a prova final do
pico. Design é DESENHO; nenhuma linha de produto tocada.
