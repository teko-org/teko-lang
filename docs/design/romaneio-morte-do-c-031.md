---
section: romaneio
created: 2026-07-27
source: ruling do owner — "este trem deve remover toda a compilação C"; medição em ci/0.3.1-lanes-e-seeds @ 47f269ab
status: INVENTÁRIO MEDIDO — não é plano de execução, é a lista contra a qual a .31 será julgada
---

# Romaneio da morte do C — o que exatamente precisa sumir na .31

O ruling do owner, na forma final:

> *"Quando digo matar .c e .h versionados, quero dizer que tudo é teko puro, todos estes arquivos
> devem ser expurgados do código fonte majoritariamente, sem remanescentes ou sobressalentes."*
> — inclui `src/runtime/teko_rt.c`.
>
> *"o único backend na versão atual deve ser native, logo, nem mesmo o enumerado se faz mais
> necessário ou mesmo as funções que roteavam para backend em C."*
>
> *"TEKO_BACKEND é outro que não deve mais existir."*

Critério de aceite, também dele: `seed .30 → gen1` **via C, inevitável**; `gen1 → gen2` nativo;
`gen2 → gen3` nativo e **byte-idêntico**. Única exceção que sobrevive: um `.h` **emitido** quando
`abi=c` — que é saída do compilador, não fonte versionada.

Este documento é **medição**, não plano. Ele existe porque "matar o C" é grande demais para caber
na cabeça de quem executa, e um alvo que não é contável não é verificável.

## 1. Fontes C versionadas — 8 arquivos, 3.894 linhas

| linhas | arquivo | o que é | destino |
|---:|---|---|---|
| 2.580 | `src/runtime/teko_rt.c` | o runtime | **reescrever em Teko** (a arena inclusa, sem exceção) |
| 865 | `src/runtime/teko_rt.h` | declarações do runtime | morre junto |
| 172 | `src/win32_compat.h` | compat Win32 | morre junto |
| 55 | `src/assert/assert.c` | seed de assert | morre junto |
| 23 | `src/assert/assert.h` | idem | morre junto |
| 91 | `scripts/region_drop_subtree_test.c` | teste C do arena | **morre com o C** (ruling: testes C morrem com o C) |
| 85 | `scripts/tk_arena_commit_test.c` | teste C do arena | idem |
| 23 | `scripts/ar_link_run_consumer.c` | consumidor do teste de `ar` | idem |

**Zero exceções na tabela.** Não há linha aqui que o ruling permita manter.

## 2. A superfície de runtime é MUITO menor do que o arquivo sugere

Esta é a medição que muda a estimativa do trabalho, e por isso é a mais importante do romaneio:

- `teko_rt.c` **define 156 símbolos** `tk_*`.
- O caminho **nativo** (`src/lir/**` + `src/backend/**`) nomeia **17** símbolos `tk_*`.
- Desses, **10** são de fato definidos pelo runtime:

  `tk_print` · `tk_println` · `tk_write` · `tk_eprint` · `tk_eprintln` · `tk_ewrite` ·
  `tk_str` · `tk_str_concat` · `tk_i64_to_str` · `tk_u64_to_str`

**Logo: 146 dos 156 símbolos existem para servir o EMISSOR DE C, não o backend nativo.** Matar o
emissor não deixa 2.580 linhas de C para portar — deixa **dez símbolos**, todos de I/O e conversão
de inteiro para string, mais o arena.

Corolário para quem executa: **a ordem certa é matar o emissor PRIMEIRO e medir de novo.** Portar
`teko_rt.c` inteiro para Teko antes disso é portar 146 símbolos que estão prestes a ficar órfãos.

### O arena, que é caso à parte

12 funções, e o owner foi explícito de que ela **vai para Teko sem exceção**:

`tk_arena_push` · `tk_arena_pop` · `tk_arena_commit` ·
`tk_region_new` · `tk_region_alloc` · `tk_region_drop` · `tk_region_drop_subtree` ·
`tk_region_lookup` · `tk_region_register` · `tk_region_root` · `tk_region` · `tk_regions_free_all`

O arena não aparece na lista dos 10 porque o caminho nativo não a chama **por nome** — ela é a
disciplina de memória do programa emitido, não uma chamada que o backend escreve. Portá-la é
trabalho de projeto próprio, não de tradução linha-a-linha, e é o único item deste romaneio que não
se resolve deletando alguma coisa.

## 3. Proporções do código Teko

| | linhas |
|---|---:|
| `src/codegen/codegen.tks` — o emissor de C | **10.727** |
| `src/lir/**` — lowering para LIR | 8.474 |
| `src/backend/**` — isel, regalloc, objfile | 33.464 |

O caminho nativo já é **quatro vezes** o emissor de C. O C não é a implementação principal com um
experimento nativo ao lado; é o contrário, há muito tempo. As 10.727 linhas do emissor são a maior
deleção única da .31.

## 4. Superfície de CI e scripts

**Workflows que nomeiam C:** `pr.yml`, `nightly.yml`, `release.yml`, `codeql.yml`.

`codeql.yml` merece nota própria: seu job `c-cpp` fica **sem entrada** no instante em que o emissor
morre. A regra de ordem é a mesma do `ci_gate_coverage.sh` — **remover o nome do ruleset antes de
deletar o job**, nunca o contrário, porque `main` não tem bypass e um check requerido cujo job
sumiu não fica vermelho, fica **PENDENTE PARA SEMPRE**. O owner já sinalizou que ensinar Teko ao
CodeQL é trabalho da .32; até lá o job sai e o oráculo fica em falta — o que é **reposição
pendente**, não polimento, e está registrado como tal.

**Scripts:** 27 `.sh` em `scripts/`, dos quais **19 nomeiam C** de alguma forma (`cc`/`clang`/`gcc`,
`teko_rt`, `TEKO_BACKEND`, ou um `.c`). Nem todos morrem — vários apenas *linkam*, e **o linker
fica**: *"Isso não inclui o linker, pois ainda temos libs a debater até lá."* A triagem
linka-versus-compila é trabalho da carga que fizer a excisão, e este romaneio só afirma o
denominador.

Os três de morte certa, porque seu assunto é o C e não o link:
`scripts/region_drop_subtree_test.sh` · `scripts/tk_arena_commit_test.sh` ·
`scripts/build_gen1_from_c.sh` (este último só depois que o degrau `seed .30 → gen1` deixar de ser
o caminho — isto é, na **.32**, não nesta).

## 5. Os oráculos que morrem junto, e o que fica em falta

O que instrumentava ou lia o C emitido não tem o que ler quando o backend escreve objeto direto.
Já saíram do `pr.yml` nesta versão: `Heavy sanitizer gate (main)` (ASan/UBSan/LSan) e `SAST gate`
(clang-tidy). Sobreviveu `Sanitizer gate`, **com conteúdo novo**: agora agrega `mem-paranoid`, o
único oráculo de memória que sobrevive ao C porque seu assunto é o **arena** (poison-on-free, nunca
reuso) e não a linguagem em que o runtime está escrito. O próprio comentário do runtime em C já
registrava que isso nunca foi redundante: *"Arena reuse is invisible to ASan"*.

Fica em falta, para repor na .32: **CodeQL entendendo Teko** e o diferencial `own == C`
(`scripts/diff_c_own.sh`), que por construção deixa de existir quando um dos dois lados morre. A
perda do diferencial é real e deve ser dita em voz alta: era o oráculo que pegava miscompilação do
backend nativo comparando-o com um segundo backend independente. O que o substitui é o **fixpoint
`gen2 == gen3` byte-idêntico**, que é mais forte em uma dimensão (auto-consistência total sob
self-host) e mais fraco em outra (não tem segunda opinião). Registrar isso é obrigação; fingir que
a troca é neutra, não.

## 5-bis. A DEPENDÊNCIA QUE O ROMANEIO NÃO TINHA — o gate passa pelo emissor

Achado da carga `cargo/20-degrau-native` (2026-07-27), e é o item mais importante deste documento
depois da seção 2, porque **contradiz a ordem que a seção 2 propõe**:

> **O emissor de C não pode morrer antes de `run_native_gate` ter porto.** O `teko test .` inteiro
> passa pelo emissor — `codegen::tk_emit_c_test` + `run_cc` emitem e compilam um `teko-tktest.c`.
> Matar o emissor hoje deixa o projeto **sem gate nenhum**.

Isso não é motivo para não matar; é a dependência que precisa estar escrita **antes** de alguém
tentar. A ordem corrigida fica:

1. portar o harness de teste para o caminho nativo (o gate para de precisar de `run_cc`);
2. **então** deletar o emissor (as 10.727 linhas);
3. **então** medir de novo os 17/10 e portar o que sobrar do runtime.

Fazer (2) antes de (1) é ficar cego no meio da maior deleção da versão.

**Resíduo declarado pela mesma carga, e é coerente com o ruling do linker:** `run_cc` **sobrevive**
à excisão do seletor. São 4 chamadores, todos gates de teste/cobertura, o maior sendo
`run_native_gate`. `build_cc_argv` também fica — é compartilhado com `link_object` (é como o objeto
nativo vira binário) e é ele que põe `teko_rt.c` na linha de link. Ou seja: parte do que a seção 4
conta como "script/função que nomeia C" é **link**, e o link fica por ruling explícito
(*"Isso não inclui o linker"*). O denominador da seção 4 continua correto; o numerador de mortes
é menor do que ele.

## 5-ter. CUSTO DE BUILD REABERTO PELA EXCISÃO — decisão do owner pendente

Também da mesma carga, e vai direto contra o alvo vinculante de **10 builds por host**:

`regr_group_solo` chaveava em `TEKO_BACKEND=native` porque só as linhas que optavam pelo backend
próprio não podiam ter dispatcher. **Sem seletor, toda linha é own → todo grupo vira SOLO.**
Agrupar foi exatamente o que levou o regressor de **126 → 44 builds**; solo devolve esse custo.

A forma de reconquistar o agrupamento é **fechar o honest-stop N2**
`fat-pointer receiver match-expression not yet lowered` — que é o que `teko::env::var` exige —
e **não** relaxar a regra do solo: regra relaxada emite dispatcher que o backend recusa, o que
troca um custo de build por uma falha de compilação.

~~As opções honestas são: fechar o N2 nesta versão, ou aceitar o custo e fechar na .32. Não há
terceira que preserve o alvo.~~ **ERRADO — corrigido pelo owner no mesmo dia. Há uma terceira, e é
a certa.** A frase fica riscada em vez de apagada porque o erro é instrutivo: era a lente, não a
conta.

> *"Sim há, você que está vendo pela lente errada. Tenho certeza absoluta que a grande maioria das
> regressões caberiam em um ou dois projetos no máximo, o que liberaria para ti outros 7 canais
> (projetos de regressão) para utilizar em casos específicos."*

**A distinção que eu não tinha feito, e que é o conteúdo do ruling:**

> **Variação de FONTE não exige projeto separado — exige ARQUIVO separado dentro de um projeto.
> Só variação de CONFIGURAÇÃO DE BUILD exige build separado.**

`regr_group_solo` existia para contornar variação por linha do eixo **backend**. Esse eixo **acabou
de morrer**. Restaurar o agrupamento seria restaurar um mecanismo cujo propósito evaporou — e pior,
pagando por isso com trabalho de lowering (o N2) que nada tem a ver com o problema.

**Payoff imediato: o N2 sai do caminho crítico.** Ele segue dívida por mérito próprio, mas não é
mais o preço do alvo de 10 builds.

**A medição que confirma o ruling** (`regressor.tkr` pós-excisão, 344 linhas, Features R0/F2/F5/F7/F9):
avulsos (`x86_64-windows`, `x86_64-linux`). **Todo o resto varia só em fonte e exit
esperado**, isto é: não precisa de build próprio para nada.

**A convergência que fecha o argumento.** Oito dos nove diretórios de `examples/regressions/` são
testes de **composição cross-namespace**. Juntá-los num projeto só não é apenas economia de build:
**fortalece o teste**, porque põe mais namespaces interagindo no mesmo build — que é exatamente o
arranjo em que os defeitos de compilador aparecem (ver a seção "os defeitos foram tornados
alcançáveis" em `teko-stacked-train-discipline.md`). **Densidade e contagem de build apontam para o
mesmo lugar**, o que é o sinal mais confiável de que o desenho está certo.

Desenho decorrente, despachado em `cargo/20-regressor-canais`:

| canal | hospeda | builds |
|---|---|---:|
| diagnostics | todos os compile-fail, **um** build que falha | 1 |
| cross-ns | os 8 de composição, juntos e por isso mais densos | 1 |
| `cwd_build` | semântica de cwd própria | 1 |
| avulsos | `x86_64-windows` | 1 |

**A lição de método, que vale além deste caso:** quando um mecanismo de otimização quebra porque
seu eixo morreu, a pergunta certa não é *"como restauro o mecanismo?"* — é *"o mecanismo ainda tem
propósito?"*. Restaurar por reflexo custa trabalho real para reconstruir um contorno de um problema
que já não existe.

## 6. Como este romaneio se verifica

Os números acima são reprodutíveis com o repositório em mãos:

```sh
git ls-files '*.c' '*.h' | xargs wc -l           # 8 arquivos, 3894 linhas
grep -o '^[a-z_0-9]* *\**tk_[a-z_0-9]*(' src/runtime/teko_rt.c \
  | grep -o 'tk_[a-z_0-9]*' | sort -u | wc -l     # 156 definidos
grep -rho 'tk_[a-z_0-9]*' src/lir/*.tks src/backend/*.tks | sort -u | wc -l   # 17 nomeados
```

Refaça a terceira medição **depois** que o emissor morrer. Se os 10 não caírem para perto de zero
depois de portar I/O e conversão para Teko, a premissa da seção 2 está errada e o romaneio precisa
ser corrigido em vez de seguido.
