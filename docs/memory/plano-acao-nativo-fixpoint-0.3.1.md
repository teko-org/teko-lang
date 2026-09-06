# Plano de ação — fechar o self-host NATIVO (gen1 + gen3) e finalizar os drenos pendentes

Data: 2026-08-04. Autor: sessão autônoma (Claude). Vagão: `fix/union` → `remodel/0.3.1.0-linux-native-2` (PR #107).

Este plano cobre TODO o trabalho pendente do vagão `fix/union`, incluindo os drenos que
entraram mas não foram finalizados. Meta primária do dono: **gen1 e gen3 nativos resolvidos**
(o fixpoint nativo gen2==gen3 fechando). Modo de trabalho: **autônomo**, coleta de seed **via CI**,
reprodução de build permitida **dentro de agente** (nunca no loop principal).

## Estado atual (fatos provados)

- `fix/union` está reconstruída LINEAR a partir de `a5f66e92` (último commit do dono), tip `9b3d8483`
  (53 commits, 0 merges) + docs de journal. `origin/fix/union` = `9b3d8483`.
- O **fat-field fix (raiz A)** está CORRETO e alcança **gen2 nativo (exit 0)** — provado pela receita
  do implementer (laddering scratchpad rota-C): seed ancião (gen1 oráculo de `theory/reseed-tip`,
  blob `20f5bcf2`) → `TEKO_BACKEND=c gen1 build .` (fonte com fix) → `gen1_fix` → `setarch -R
  TEKO_BACKEND=native gen1_fix build .` → **gen2 nativo exit 0**. O crash do fat-field (~5083,
  `emit_native_x86`/`tk_region_alloc`) SUMIU.
- O **residual que trava o fixpoint** é gen2→gen3: `setarch -R TEKO_BACKEND=native gen2 build .`
  crasha M.1 em **`checker::collect_stmt_insts`** (`src/checker/resolve.tks:2573`), logo no início do
  checker do gen3. É um bug do BACKEND NATIVO distinto do fat-field (o `gen1_fix` nativo-constrói
  `examples/regressions/own_native` limpo; reprodutores sintéticos pequenos monomorfizam certo em
  ambos backends). Precisa do caminho de inferência do self-host completo.
- `setarch -R` (ASLR off) é OBRIGATÓRIO pra reproduzir o M.1 determinístico (é Heisenbug: com
  `TEKO_NATIVE_TRACE_ITEMS`/gdb some). A theory CT atual NÃO usa `setarch -R` → dispara em sítio
  diferente (ex.: native-lowering item ~1936); precisa ser corrigida pra replicar o ambiente.
- O REPL (`src/repl/repl.tks` + fiação em `main.tks`) foi DRENADO por engano — foi decidido-removido
  (nunca esteve na `remodel`), e revelou um bug de codegen (`c == c'X'` → C inválido `(tk_char){...}`).

## Frente A — NATIVO (gen1 + gen3): a meta primária

- **A1. Reproduzir a cadeia do implementer (agente, local + `setarch -R`):** gen1 oráculo →
  `gen1_fix` (rota C) → gen2 nativo (exit 0) → gen3 nativo → cravar o crash `collect_stmt_insts`.
  Confirmar o sítio via `nm -n`/`addr2line` sobre os offsets.
- **A2. Diagnosticar o residual `collect_stmt_insts`** (`src/checker/resolve.tks:2573`): é uma
  miscompilação do backend nativo (não do checker em si — a rota C passa). Desk-check estilo raiz-A:
  qual construto de `collect_stmt_insts`/`resolve.tks` o backend nativo lower/emit errado. Suspeitos:
  walk pesado de match/variant/list, mesma família de arena/lifetime, ou outro layout escalar-vs-fat.
- **A3. Corrigir** o bug em Teko (`.tks`), single-source. Provar por: `gen1_fix` → gen2 → gen3 nativo
  exit 0 E gen2==gen3 byte-idêntico (o fixpoint fecha).
- **A4. Corrigir a theory harness** pra usar `setarch -R` na perna nativa (determinismo) e re-provar
  via CI (coleta via CI). Só então a lane fecha VERDE nas duas pernas.
- **A5. Regressão:** `.tkt` do molde que dispara `collect_stmt_insts` (bare-`T` no caminho de inst).

## Frente B — corrigir os drenos pendentes na `fix/union`

- **B1. Journal `summary.tks`** — APLICAR a solução do especialista
  (`docs/design/reconciliacao-journal-0.3.1-fixunion.md` §3): portar da journal-impl o struct privado
  `WriterStat` + `fold_writer_record`, `cov_of_payload`, `cov_add`, `is_dead`
  (`has_incomplete || (has_plan && !has_end)`), `nth_field`/`parse_u64`, `stat_of`, `collect_findings`
  sobre a casca/render CANÔNICA; rejeitar duplicatas; NÃO re-fiar `project.tks` (RunSummary idêntico);
  os 2 testes net-novos vão pro `journal_test.tkt` (não criar `summary_test.tkt`). `journal-impl`
  fica ABANDONADA (sem drain).
- **B2. Remover REPL + remanescente do motor legado** (mantendo **teko doc** e **teko lint**, que apoiam o LSP):
  refazer a reconstrução linear EXCLUINDO os commits do REPL (`devtools-repl`: `repl.tks`,
  `repl_cli_test.sh`, a fiação `teko::repl::run_cli` em `main.tks`, entradas de help do REPL) e
  qualquer código remanescente do motor legado. NÃO por revert à mão — pelo redo do dreno na ordem certa.
- **B3.** Após B2, a `fix/union` linear deve compilar a rota C sem o `c == c'X'` (o bug do REPL some
  com a remoção). Reconfirmar via theory.

## Frente C — higiene do vagão / PR

- **C1.** `fix/union` linear, sem REPL/motor legado, com journal reconciliado e o fix nativo → PR #107
  (`fix/union → remodel`) verde. Coleta do seed pós-fixpoint via CI, com PROVENANCE.
- **C2.** Drenos já consolidados (keystone, fat-field, native-diag, tests-native-no-c, docs/tooling):
  verificar que seguem íntegros após B2.

## Frente D — memória de arena (alvo <1.5G, teto 2G) — pode convergir com A2

- **D1.** O pico 2293 MB do self-host completo + a família de lifetime de arena
  (`collect_stmt_insts`/`prune_os`) são provavelmente o mesmo território do residual A2. Ao fechar
  A2, medir o pico e, se >1.5G, atacar a retenção de região (liberar no fim do escopo dono).

## Frente E — superfície de linguagem (DIFERIDO, pós-fixpoint)

- **E1.** `sizeof<T>(): u64` (comptime) + `teko::mem::size<T>(ref value: T): u64 | error`
  (runtime; null→0; error p/ incalculável/corrupção) — salvaguardas de memória manual/unsafe.
- **E2.** Parâmetros default em funções (vagão futuro).

## Ordem de execução autônoma

1. **A (nativo)** é a meta da noite — prioridade máxima. Agente reproduz + diagnostica + corrige o
   `collect_stmt_insts`, prova gen2→gen3, corrige a theory pra `setarch -R`, coleta via CI.
2. **B (journal + REPL/motor legado)** em paralelo — agente aplica a solução do journal e refaz o dreno sem
   REPL/motor legado.
3. **C/D** ao fechar A/B. **E** diferido.

## Invariantes (leis do dono)

- Coleta de seed SEMPRE via CI (nunca seed privado local não rastreado — foi o que quebrou antes).
- Reprodução de build só DENTRO de agente, com `setarch -R` pra determinismo.
- História do vagão LINEAR (sem merges); drenos por precedência de data.
- REPL e remanescente do motor legado FORA; teko doc e teko lint FICAM.
- Nada de enquete no loop principal; reportar avanços, não cada iteração.
