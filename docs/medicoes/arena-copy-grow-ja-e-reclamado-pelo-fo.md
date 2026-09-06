# O copy-grow do compilador NAO e um vazamento — ja e reclamado pelo free-old

Resultado da investigacao de `arena-copy-grow-abandona-o-bump-top.md`. Pico de RSS medido com um
wrapper `wait4`/`ru_maxrss` sobre `TEKO_BACKEND=c teko . -o OUT --no-verify` (o compilador
compilando a si mesmo). Caixa de 16 GB, 4 CPUs. O pico do teko independe do nivel de otimizacao
do `cc` final (medido: `--release` 1648,5 MB vs `--opt=0` 1634,7 MB).

## A prova de uma linha: desligar o free-old custa +0,66 GB

```
baseline (free-old LIGADO):                 1634,7 MB de pico
TEKO_FO_MAX=0 (free-old DESLIGADO):         2295,9 MB de pico   (+661 MB)
```

`tk_slice_push_fo` (a variante free-old, emitida pelo codegen para toda slice que a analise de
escape prova LINEAR — nascida de `list::empty()`, so self-append, sem captura) PARKEIA o buffer
antigo na free-list a cada copy-grow, e o proximo allocador do mesmo tamanho o REUSA. Desligar
isso (`TEKO_FO_MAX=0`) sobe o pico em 661 MB. Ou seja: **o copy-grow dominante ja e reclamado.**

O ofensor #1, `checker::variant_siblings` (946 MB / 1,25 M copy-grows), lowerisa exatamente para
`tk_slice_push_fo` — confirmado no C emitido:

```c
tk_slice_push_fo(_sb.ptr, _sb.len, &_si, sizeof(tk_t_teko__checker__Type), &_sl);
```

## Por que o "reclaim ratio: 0,0%" enganava

A metrica do `TEKO_ARENA_OBS` somava so `region drops + test-gate rewinds` no numerador — nunca
o REUSO da free-list (que, na corrida instrumentada, reciclou ~548 MB em ~1,27 M takes). Num
compilador cuja reclamacao e quase toda free-old + free-list, isso lia "0,0% reclamado" enquanto
centenas de MB eram de fato reciclados. Esta PR corrige a metrica: o reuso da free-list entra no
numerador (`teko_rt.c`, `tk_obs_dump`), e uma linha nova reporta os bytes reusados.

## O grow-in-place seguro foi implementado, medido e REJEITADO

O conserto proposto (estender o buffer no topo do bump, sem copia — seguro por construcao,
`[0,len)` intacto) foi implementado e medido em tres variantes:

```
baseline:                                   1634,7 MB
grow-in-place (todos os slices):            1946,6 MB   (+312 MB — REGRIDE)
grow-in-place + gate por free_peek:         1947,4 MB   (+313 MB — REGRIDE)
grow-in-place so no caminho plain (nao-FO): 1628,9 MB   (-6 MB — ruido)
```

O grow-in-place cego regride porque ROUBA os consumidores da free-list: cada grow-in-place e um
copy-grow a menos que teria REUSADO um bloco parkeado, entao ~417 K blocos parkeados ficam ociosos
(+110 MB parkeado) e ~293 MB vem de chunks NOVOS em vez de reuso. Restrito ao caminho `plain`
(slices sem prova de linearidade, que hoje abandonam sem parkear), o ganho e ~6 MB (ruido): slices
plain raramente estao no topo do bump (e por interleave de outras alocacoes que a analise de escape
os marca nao-lineares). Nenhuma variante justifica o custo/risco. **Nao embarcado.**

## O que SOBRA de reduzivel exige o que esta carga nao pode fazer

- As chunks da raiz (1532 MB, malloc'd) sao o WORKING SET do compilador: ele segura AST + tipos +
  IR do programa INTEIRO na regiao raiz e larga 11 de 5127 regioes durante a compilacao. Reduzir
  isso e escopar por-funcao/por-arquivo e dropar — o que exige PROVAR que nada de A escapa para B
  (analise de escape). O dono carvou isso para fora desta carga ("se um fix exigir provar
  nao-escape, ele NAO e desta carga — reporte, nao arrisque").
- A gap #2 do mandato (codegen NATIVO a ~15,8 GB) e a mesma coisa no backend nativo: `src/backend`
  nao usa NENHUMA regiao escopada (`grep region_new/region_drop` = vazio) — tudo acumula na raiz.
  O conserto e um drop por-funcao no pipeline LIR→MInst→encode, cuja seguranca exige provar que
  nenhum ponteiro de LIR/MInst e retido no objeto final. Alem de ser raciocinio de escape, ele NAO
  e PROVAVEL nesta caixa: o build nativo estoura os 16 GB (OOM), entao nao ha como medir o pico
  antes/depois que o proprio mandato exige como prova.

## Conclusao

O copy-grow, o alocador que o mandato aponta como "a brecha #1", ja e reclamado pelo free-old +
free-list (prova: +661 MB ao desligar). O grow-in-place seguro regride. A memoria que sobra e
working set, reduzivel so por escopo-com-drop provado por analise de escape — explicitamente fora
desta carga. Esta PR entrega a metrica honesta (que era a origem do alarme "0,0%") e este registro;
a reducao real precisa da onda de analise de escape sobre o backend nativo (gap #2).
