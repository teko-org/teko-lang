---
section: process-law
created: 2026-07-25
source: owner rulings 2026-07-23/24/25 (fila serial → trem empilhado → dreno LIFO)
---

# Trem empilhado — disciplina de vagões (0.3.1+)

O modelo de entrega deixou de ser "PRs paralelos na main" e passou a ser um **trem**: cada
vagão é um PR cuja **base é a branch do vagão anterior**, não a main. Trabalhar sobre a
superfície do vagão de baixo é deliberado — expõe problema e solução cedo.

## O modelo

**Empilhamento (owner 2026-07-23):** nada em paralelo; cada vagão só abre baseando-se no
último vagão engatado. Equalização LOCAL antes de existir na origin (merge/cherry-pick).

**Dreno LIFO (owner 2026-07-24):** o trem drena de **cima para baixo** — o último vagão
engatado mergeia **dentro** do vagão de baixo, que mergeia no de baixo, até o vagão de base
entrar na main **uma única vez** carregando o trem inteiro integrado. Não há retarget e não
há cascata durante o dreno.

**Auto-correção do trem (owner 2026-07-25) — A REGRA QUE MAIS ECONOMIZA TRABALHO:** se um
vagão está **vermelho** e o vagão que **deriva dele** está **verde**, o de baixo **fica
verde quando o de cima desagua nele**. Consequências, todas obrigatórias:

- **NUNCA cascatear um fix para baixo.** Propagar manualmente o que o dreno propaga de graça
  queima runners, tempo e contexto. (Violado 9× em 2026-07-24 — o erro que originou esta nota.)
- **Correção pequena/pontual → entra no ÚLTIMO VAGÃO ENGATADO** (o topo).
- **Correção grande → vagão NOVO no topo.** O trem se corrige sozinho no dreno.
- **GATE ÚNICO QUE IMPORTA: o topo do trem verde.** Vagões de baixo NÃO precisam estar verdes
  isoladamente; não os dirija ao verde um por um.
- **Vagão só se reabre quando um merge dá erro.** Fora disso, vagão fechado não se toca.
- **Nunca pushar em vagão já fechado/verde** (o owner aperta as rules de branch se repetir).

**Achado de auditoria nunca edita vagão existente (owner 2026-07-24):** o romaneio produz um
**vagão novo** de correções, não edições retroativas. Assim o vagão auditado permanece
exatamente como foi validado e cada achado tem diff próprio.

## O dreno é UM merge, não N (owner 2026-07-25)

A branch do topo **contém**, por construção, todo o histórico de cada vagão abaixo (cada um é
baseado no anterior). Logo, mergear o topo na main produz **a mesma árvore** que N merges
encadeados — o dreno vagão-a-vagão é trabalho redundante. A receita:

1. contra-máquina verde no topo;
2. **retarget da base dela para `main`** (o diff do PR passa a ser o trem inteiro, e o CI roda
   contra a main de verdade — o "run completo" no vagão que aterrissa);
3. squash-merge: a main recebe **um** commit com tudo, incluindo o bump;
4. os demais PRs **se fecham**, não se mergeiam;
5. apagar as branches dos vagões.

**Como os demais PRs realmente fecham — depende da estratégia:**

- **squash** (o nosso): os commits dos vagões NUNCA ficam alcançáveis a partir da main (o squash
  cria um commit novo), então o GitHub **não** marca os outros como mergeados por alcançabilidade.
  O que os fecha é **apagar as branches** — o GitHub fecha o PR quando a base ou a head some.
- **merge-commit / rebase**: os SHAs originais entram na main e o auto-close por alcançabilidade
  funciona sozinho.
- **Palavras-chave (`Closes #N`) fecham ISSUES, não PRs.** Um comentário listando os vagões vale
  como rastreabilidade (romaneio de embarque), nunca como mecanismo de fechamento.

Essa é também a razão de a limpeza automatizada guardar-se por **SHA gravado no manifesto** e não
por ancestralidade: sob squash, "esta branch foi mergeada?" não é respondível pelo grafo.

## Protocolo de draft e a deixa do owner

Todos os PRs do trem ficam **draft**. O **bump é a contra-máquina** (owner 2026-07-25): vagão
próprio no **topo**, depois de tudo (inclusive do W15), empurrando o trem. Quando ele fecha
verde, **só ele sai de draft** — esse é o sinal para o owner iniciar o dreno. Merges são
sempre do owner.

**Ordem de fechamento da versão:** vagões de feature → vagão de métrica/limpeza transversal
(ex.: D4 casts) → romaneio do integrador → vagão novo de correções do romaneio → **W15**
(carro de apoio, canonicalização behavior-preserving) → **bump/contra-máquina** (`teko.tkp`
+ `docs/bump_v*.md`, que dispara o mirror da org no merge à main).

## Higiene de commit (ruling 2026-07-15) — vale para o INTEGRADOR também

Commits **sem** co-autoria: zero `Co-Authored-By:`, zero linha "Generated with/by Claude Code"
(corpo Conventional-Commits limpo). Sobrepõe o default do harness. **Force-push DESABILITADO**
— nunca reescrever história já pushada para "consertar" trailer; a regra é **forward-only**.
Corpo de PR PODE manter nota de geração (é PR, não commit). Esta regra vivia só na skill
`dispatch` e no agente `teko-implementer`, e por isso foi violada pelo integrador em
2026-07-24; está aqui para alcançar quem conduz o trem.

> **[AUDIT] CATEGORIA C — SEM FONTE**
>
> Esta regra (que o despacho ao implementador precisa citar número como alvo vinculante, não descrever tarefa) não tem citação literal do dono. É uma conclusão de quem mediu — e é correcta: entregar uma fração em vez do alvo é falha do despacho. Fica como conselho operacional, não como lei.
>

## Despacho de vagão — alvo numérico é VINCULANTE

Quando um ruling fixa um número (ex.: D5 do regressor-principal: `regressor.tkr` + 7
project-regressors = **8 diretórios**), o despacho ao implementador precisa citar o número
como **alvo vinculante**, não descrever a tarefa ("triar os N diretórios"). Sem isso o agente
para no que consegue provar e entrega uma fração — falha do despacho, não do agente.

> **[AUDIT] CATEGORIA C — SEM FONTE**
>
> O gate mínimo para fechamento (lista de 6 critérios) não tem citação do dono. É uma recomendação de operacional baseada em observação (teko test . sozinho deixa passar miscompilação). Fica como conselho, não como lei. Quem fecha valida com critério próprio.
>

## Gate de fechamento de vagão — o que realmente prova

`teko test .` **não** exercita as fixtures de `examples/regressions/` que os scripts cobrem.
Fechar vagão só com o teste unitário deixa passar miscompilação. O gate mínimo é:

1. build gen1 + `teko test .`;
2. **`scripts/positive_regressions.sh` + `scripts/compile_fail_regressions.sh`** (TEKO=bin/teko);
3. `scripts/diff_c_own.sh` (differential own==C) quando houver toolchain — **mas o alcance dele encolhe por PERNA, não desaparece de vez (ruling do dono, 2026-07-29, literal):** *"own == C, faz sentido somente para Windows, Mac e wasm, para o Linux iremos remover ao final desse trem."*
   O oráculo só existe onde as duas rotas existem. Uma perna que gera NATIVO não tem C com que se comparar, portanto exigir-lho seria exigir o impossível — e um gate que não pode passar bloqueia mais do que protege (lei de 2026-07-25, neste mesmo ficheiro). Na 0.3.1.0: as quatro pernas Linux perdem-no **no fim deste trem**; `windows-x86_64`, `macos-arm64` e wasm mantêm-no enquanto viverem na rota C. O que substitui o oráculo nas pernas nativas é o que já as gateia: `gen2 == gen3` byte-idêntico, o corpus `own_native`, e o `TEKO_MEM_PARANOID`.
   **A remover ao fechar o trem, não antes:** enquanto as pernas Linux ainda param num degrau, o diferencial continua a ser evidência útil.
4. fixpoint gen2 == gen3 **byte-idêntico**;
5. `TEKO_MEM_PARANOID=1` no fechamento;
6. auditoria W15 do delta (zero `//` inline; D39).

> **[AUDIT] CATEGORIA C — SEM FONTE**
>
> Os invariantes do bootstrap escalonado (sondagem por descoberta, pinagem de TK_RT_DIR, etc.) não têm citação literal. São conclusões arquiteturais documentadas aqui. Ficam como princípios de implementação.
>

## Bootstrap escalonado do CI (seed-fallback)

O seed liberado só precisa construir a **linhagem da base**, não o tip (ruling 2026-07-24). Num
trem empilhado há mais de um salto de capacidade, então o fallback é uma **escada iterativa**:
`repete { se o compilador corrente constrói o tip, fim; degrau := ancestral first-parent MAIS
NOVO que o corrente consegue construir; corrente := gen(degrau) }`, com guarda de
não-progresso. Invariantes aprendidos na marra:

- **O degrau é DESCOBERTO por sondagem, nunca assumido.** Um vagão pode ADICIONAR capacidade e
  em seguida DELETAR o corpus que a dispensava (ex.: W-RULE + varredura de casts) — sua própria
  cabeça exige compilador novo. O degrau construível é o vagão que já tem a capacidade mas
  ainda não o corpus que depende dela.
- **`TK_RT_DIR` é pinado por estágio.** O compilador resolve `teko_rt.{h,c}` relativo ao
  próprio binário (argv[0]); sem pinar, mistura eras de runtime e o link falha.
- **A saída da sondagem mora DENTRO do worktree sondado** (mesma razão: era do runtime).
- **O clean preserva os diretórios que guardam os compiladores da escada.**
- Build intermediário é seco (`--no-verify`): medida **transitória da .31**, a desfazer na .32.

## Seeds commitados — REMOÇÃO AGENDADA para o primeiro vagão da .32

Ruling do owner (2026-07-25): *"versionar binário (nightlys do fork) será somente para o trem
atual, no próximo devemos remover isso"*. O que fica permanente é o **nightly + promoção**; o que
sai é o blob no repo. Sem esta lista escrita a gambiarra vira permanente, que é exatamente o modo
como gambiarra sempre vira permanente.

**O que sai, item a item:**

| o quê | onde |
|---|---|
| os cinco blobs + o manifesto | `bootstrap/seeds/` (diretório inteiro) |
| a regra binária | linha `bootstrap/seeds/*.xz binary` em `.gitattributes` |
| o consumo | `seed_from_committed`, `host_seed_label`, `unxz`, o ramo `TEKO_SEED_PREFER_COMMITTED` e o fallback pós-loop em `scripts/ci_provision_teko.sh` |
| a produção | job `seeds` + `scripts/cross_compile_seeds.sh` |
| o consumo em CI | job `seed-debut` (`native.yml`) |
| a escada pinada | `scripts/build_with_seed_fallback.sh` inteiro (já agendado no cabeçalho dele) |

**Por que o registro está aqui e não no `<!-- train-manifest -->` do bump, como o briefing do
vagão 20 pedia:** aquele bloco mora em `docs/bump_v<versão>.md`, e `mirror-pr-to-org.yml` dispara
em `push` à `main` com `paths: ['teko.tkp', 'docs/bump_v*.md']`. Criar o arquivo do bump num vagão
que **não é** o bump abriria o PR de promoção na org antes da hora — ou, com o nome de versão
ainda não bumpado, falharia com `bump doc não encontrado` numa `main` verde. **O vagão da
contra-máquina copia esta tabela para o `<!-- train-manifest -->` que ele cria**; é lá que ela
termina, mas não é lá que ela pode nascer.

## Lane que estreia na aterrissagem → vagão próprio de correção (owner 2026-07-25)

O split light/full do CI (`base_ref == main` ⇒ full) tem um efeito de segunda ordem: as lanes
full-only rodam **pela primeira vez** no vagão que aterrissa. Na .31 isso concentrou OITO estreias
self-host, `test / macos`, três `ar validation`, cross-smoke — e duas delas (ASan e mem-paranoid)
**nunca haviam rodado na história do projeto**, porque gateavam em `github.ref == 'refs/heads/main'`,
condição que nunca casa num evento `pull_request`.

Duas saídas foram propostas ao owner e **as duas foram rejeitadas**: aceitar o risco e resolver na
contra-máquina; ou gastar um PR descartável apontado para `main` só para forçar um run completo.

**Ruling: vagão NOVO de correção, antes do W15.** Ordem de fechamento passa a ser:

> features → métrica/limpeza transversal (D4) → **vagão de correção de estreia** → W15 → contra-máquina

O run completo sai de graça, sem PR de mentira: o vagão de correção recebe **retarget da base para
`main`** — a mesma mecânica já ratificada para o dreno ("o CI roda contra a main de verdade") — o
full roda contra a árvore quase-final, as correções entram **nesse mesmo vagão**, e depois a base
volta para o vagão anterior para o W15 empilhar em cima.

Por que é melhor que as alternativas: um PR descartável mede uma árvore que ainda vai mudar e o
trabalho de correção não tem onde morar; aceitar o risco trava o owner no último passo. O vagão
resolve os dois — é lugar de trabalho **e** é o gatilho do run.

**Corolário: pré-carregue o vagão em vez de reagir ao vermelho.** As classes de falha de cada lane
são estaticamente caçáveis. Prova empírica da .31: a lane do Windows, ao ser ligada, entregou dois
defeitos reais em duas horas — um seed de assert morto em PE/COFF (weak cruzando unidade de tradução
não existe nesse formato) e um `"/tmp/..."` hardcoded num fixture. O segundo era achável com um
`grep`. Uma varredura por classe antes do run faz o vagão nascer com lista.

### O que a estreia realmente encontrou (medido, 2026-07-26)

O ruling acima foi tomado sobre uma previsão. A estreia aconteceu no vagão 20, com a base
retargetada para `main`, e o resultado confirma a previsão com folga. Registrado por CAUSA, não por
lane, porque uma causa aparecia em várias lanes e isso escondia a contagem:

| causa | achada por | o que era |
|---|---|---|
| `dladdr` fora da `libc` | `artifact / linux-*-glibc` | mora em `libdl` até a glibc 2.33; o 2.39 do runner escondia a dependência que o piso de 2.28 exige. **Só apareceu porque o zig morreu** e o build passou a acontecer no piso. |
| ABI do Win64 | `test / windows-x86_64` **e** `` | `tk_str` (16 bytes) era passado em par de registradores. A MS x64 só passa agregado em registrador nos tamanhos 1/2/4/8; acima disso vai por referência. O callee lia os 16 primeiros bytes da string como `{ptr; len}`. O objeto PE/COFF sempre esteve correto — a hipótese "é do COFF" era minha e estava errada. |
| `memcmp(NULL, …)` | `ASan+UBSan+LSan` | UB em `tk_str_eq` e `tk_str_ends_with`. **Primeira execução na história do projeto** desta lane (o J2). |
| sem alvo ELF/aarch64 | `test / linux-arm64-{glibc,musl}` | os assets arm64 eram publicados sem que `teko test .` jamais tivesse rodado naquele hardware. |

**A lição que generaliza** é a última linha da tabela, e ela vale para toda lane nova: uma sonda que
verifica o NOME de uma ferramenta em vez do FATO que ela deve produzir passa verde e a falha reaparece
depois, disfarçada de regressão real. O mesmo padrão apareceu neste vagão em três lugares diferentes
(a sonda do qemu, o gatilho com `paths:` que não disparava, e o agregador que aceitava `skipped` como
aprovação). É a mesma família do "portão que não gateia".

**Corolário para a contagem de risco:** duas das cinco causas (o `dladdr` e a ABI do Win64) eram
invisíveis por construção antes deste vagão — a primeira porque cross-compilar escondia o piso, a
segunda porque nenhum fixture do corpus tinha uma chamada de runtime que RETORNA e leva agregado.
Nenhuma varredura estática as teria achado. O "pré-carregue o vagão" acima continua certo, mas não
substitui a estreia: ele reduz a lista, não a zera.

> **[AUDIT] CATEGORIA C — SEM FONTE**
>
> A observação sobre comentários errados ser pior que nenhum, e o padrão diagnóstico das funções que guardam edge cases vs. as que afirmam, não têm citação literal do dono. Fica como aprendizado de revisão.
>

### Um comentário errado é pior que nenhum

Duas das causas acima estavam documentadas como SEGURAS por comentários que argumentavam mal:

- `tk_str_eq`: *"a zero-length pair compares equal without touching ptr (memcmp of 0 bytes is
  well-defined)"* — verdade sobre a leitura, falso sobre a chamada (`nonnull`).
- `is_str_arg_builtin`: achatar `tk_str` em `(ptr, len)` *"reproduz a verdadeira ABI C sem nenhuma
  mudança de isel/regalloc/stackify"* — verdade em SysV/AAPCS64/LP64D, falso em Win64.

Nos dois casos o comentário afirmava a conclusão certa para o caso comum e foi ele que tornou o
defeito invisível à revisão. Padrão diagnóstico útil: nas mesmas famílias, as funções que GUARDAM o
caso de borda não fazem a afirmação; as que afirmam não guardam. **Ao corrigir, corrija o comentário
junto** — senão o próximo leitor reintroduz o defeito com a bênção da documentação.

## O DEGRAU que aposenta o C (owner 2026-07-26)

> *"este trem deve remover toda a compilação C para no trem seguinte ser 100% nativo. Isso não
> inclui o linker, pois ainda temos libs a debater até lá."*
> *"como faremos degrau aqui, gen1 != gen2 quando usar native, logo o degrau corrige e aposenta C"*

A .31 mata a compilação C; a .32 nasce 100% nativa. O **linker fica de fora** — as bibliotecas de
plataforma ainda estão em debate (inventário fechado é a tarefa pré-linker).

**O critério de aceite NÃO é `gen1 == gen2`.** É o mesmo que o bootstrap original já usa, e que o
`docs/BUILDING.md` já enuncia para a transição C→self-host: *"only the one-time C→self-host
transition differs cosmetically. Gen-2 vs gen-3 must be byte-identical."*

```
gen1 = seed (backend C) compila a árvore        → compilador que emite C
gen2 = gen1 sob TEKO_BACKEND=native             → compilador nativo   ← O DEGRAU: difere de gen1
gen3 = gen2 compila a árvore                    → BYTE-IDÊNTICO a gen2
```

`gen1 != gen2` é a definição do degrau, não um defeito. Quando `gen2 == gen3` valer, o seed do trem
seguinte passa a ser um binário nativo e o C sai do caminho por consequência — ninguém precisa
apagá-lo à mão.

### Três coisas que decorrem disso e que já mordem hoje

1. **Determinismo virou pré-requisito, não higiene.** O degrau exige igualdade byte-a-byte entre
   duas execuções. A ordem de descoberta de fontes vinha do `readdir` (ver a armadilha abaixo), o
   que tornaria `gen2 == gen3` falso por construção — e a falha apareceria como "o degrau não
   fecha", sem causa visível. Corrigido em `tk_rt_list_dir` antes de o degrau começar, por sorte.

2. **O portão `nightly === gen1` precisa de um QUARTO antecedente: a geração.** Ele mede seed,
   toolchain e árvore, e afirma que o mesmo commit dá os mesmos bytes. No commit em que o degrau
   acontece, o binário publicado muda de geração e o portão vai acusar não-reprodutibilidade num
   commit são. Corrigir junto do degrau, não depois.

3. **Teste unitário e teste de C morrem com o C** (ruling do owner na mesma conversa: *"não deve
   focar esforços neles"*). Isso reescreve o corte de testes: não é "medir cobertura e podar", é
   consequência da morte do C. O que sobrevive é o corpus de regressão, que roda o binário.

### O que morre JUNTO com o C — quatro oráculos, e sete lanes

Levantado a pedido do owner (*"não esqueça de lanes de CI que exigem C"*), 2026-07-26. A conta é
maior que ajustar YAML: metade da verificação do projeto existe **porque** o caminho passa por C.

| lane de `pr.yml` | por que depende do C |
|---|---|
| `artifact / <producer>` (7 legs) | `native_linux_asset.sh` compila `teko.c` + `teko_rt.c` + `assert.c` com gcc — a produção de asset INTEIRA é compilação C |
| `TSan` | baixa o artefato `teko-c` e linka gen1 instrumentado |
| `ASan+UBSan smoke` | idem |
| `ASan+UBSan+LSan / default dispatch` | idem — a auditoria pesada |
| `clang-tidy audit` | varre `src/runtime/teko_rt.c src/assert/assert.c` |
| `Memory paranoid (native self-host)` | `TEKO_MEM_PARANOID` é um botão do `teko_rt.c` |
| `codeql.yml` (`c-cpp`) | analisa o C |

**Os quatro oráculos que somem:**

1. **O diferencial `own == C`.** Era o que provava o backend nativo correto por igualdade contra um
   caminho maduro. Sem C não há contra o que diferir; 31 linhas `| c |` do `regressor.tkr` perdem
   o sentido.
2. **Os sanitizadores.** ASan/UBSan/TSan/LSan são o clang instrumentando o C emitido. Um backend
   que emite objeto direto não tem clang para instrumentá-lo.
3. **O SAST (`clang-tidy`).**
4. **A análise CodeQL `c-cpp`.**

**O que sobra provando o compilador:** o valor que cada cenário do corpus afirma por si (exit,
stdout, trap), o fixpoint `gen2 == gen3`, e o `TEKO_MEM_PARANOID`.

### O oráculo que NÃO morre — e é o que cobria o que os outros não cobriam

Owner, 2026-07-26: *"a arena vai para teko, não há exceções de manter algo em C"*. Consequência
que melhora a conta acima em vez de piorá-la.

`TEKO_MEM_PARANOID` são **três linhas** em `tk_free_take`: ler o env uma vez e, quando ligado,
`memset(p, 0xDD, usable); return;` — envenena o bloco e nunca o devolve ao pool. Isso é
propriedade da **arena**, não do C, e migra com ela sem perder nada.

E o comentário que já estava ao lado dele é o ponto:

> *"Arena reuse is invisible to ASan, so a wrong linearity proof would corrupt silently; with
> poison, any read-after-park yields 0xDD garbage and the gate/diff harness fails LOUDLY."*

Ou seja: **o ASan nunca cobriu reuso de arena.** O oráculo que cobre o modelo de memória mais
distintivo do projeto é exatamente o que sobrevive, e ele nunca foi redundante com os
sanitizadores. Dos quatro que se perdem, nenhum cobria a arena.

### Os *San's: teacháveis, mas não pela .31 (owner 2026-07-26)

> *"quanto aos *San's, se não houver como ensiná-los (não vejo motivos se compararmos com outras
> linguagens que fazem a mesma abordagem), não tem pq mantê-los."*

O precedente é real e vale registrar o MECANISMO, porque ele não é o que a intuição sugere: Rust e
Go não pedem ao clang que instrumente — **o próprio compilador emite** a instrumentação (chamadas
`__asan_*` em torno de load/store; `__tsan_read`/`__tsan_write` em cada acesso) e linka a runtime
de sanitizador da plataforma. Como o linker do sistema continua nosso até a .33, o caminho existe
para nós também.

Mas é trabalho de CODEGEN, não de configuração. Ruling aplicado: **saem com o C**, e voltam se e
quando o backend nativo souber emiti-las. Não seguram a .31.

**Armadilha imediata, e é a da família "portão que não gateia":** se o C sumir e o job `c-cpp` do
`codeql.yml` continuar no lugar, ele passa **verde analisando nada**. Um gate vazio é pior que
gate nenhum, porque parece cobertura. Ou o job sai junto com o C, ou entende Teko — e é por isso
que *ensinar Teko ao CodeQL na .32* (owner, mesma conversa) é **reposição**, não polimento.

### O método: escada medida, não inventário de comentário

Contar `honest_stop` em comentário dá 332 e não significa nada. O número que significa é onde o
compilador para ao compilar A SI PRÓPRIO pelo backend nativo. Medido em 2026-07-26, na árvore do
vagão 20: passa lexer, parser, **checker (6139 itens)**, monomorph e consteval (591 consts), e para
em

```
const struct: initializer is not a struct literal (Tier-A follow-up) (#594)
```

Esse é o degrau 1. O método é o laço: fechar o stop que ele nomeia, rodar de novo, ler o próximo.
Cada volta é medível e o progresso é o número de fases vencidas, não a contagem de TODOs.

## PUSH FREQUENTE, verde ou não — e a carga é o destino que não acorda o CI (owner 2026-07-26)

> *"Não interessa se está verde ou não, é por isso também de usar as cargas caso queira evitar
> disparar CI, mas é importante sempre exercitar pushes frequentes."*
>
> *"Não é só em vermelho, enquanto estiver trabalhando/produzindo, busque usar as cargas, isso
> diminui o número de alertas de cancelamento."*

A `cargo/**` tem DUAS funções, e usar só a primeira é o erro que esta nota registra:

1. **paralelizar a produção** (o que a seção da esteira já dizia), e
2. **ser o destino de push do trabalho em voo** — `cargo/**` não tem PR e os cinco portões são
   `pull_request`-only, então empurrar para lá **não dispara CI nenhum**. É push de graça.

**O ambiente é efêmero.** Worktree, branch local e commit local vivem no container; quando ele é
reciclado, somem. "Guardar para empurrar quando fechar verde" não protege nada — protege um verde
que nem sempre existe — e arrisca tudo.

**O erro cometido em 2026-07-26, para não repetir:** o integrador segurou o push do vagão
"até o gate fechar", enquanto o vagão estava vermelho POR DECISÃO do owner (a inversão do backend
para nativo). Resultado do inventário quando o owner perguntou: **18 commits** não empurrados no
vagão e **13 branches `cargo/*` sem remoto nenhum** — incluindo o fecho do `B2-bigimm`, a correção
de ordem da monomorfização, o desdobramento inteiro e 551 linhas de excisão de CI. A carga viva
tinha mais 2 commits pesados soltos (a excisão do enum `Backend` e o `extern` mirando o símbolo C).
Tudo a um crash de distância de sumir.

**A regra invertida é a certa:** vagão vermelho por decisão do owner é vagão que se empurra MAIS
cedo, não menos. O vermelho já é o estado desejado; não há o que preservar segurando.

### O destino default é a carga — não só no vermelho (owner 2026-07-26, segunda ordem)

A primeira leitura desta regra foi estreita demais: *"enquanto o vagão está vermelho eu empurro
para as cargas"*. O owner corrigiu o escopo — **não é o vermelho que escolhe o destino, é o estar
produzindo**. Enquanto há trabalho em voo, a carga é o destino; o vagão recebe **marcos**, não
fatias.

**O motivo é o alerta de cancelamento, e ele é mecânico.** O `pr.yml` declara

```yaml
concurrency:
  group: ${{ github.workflow }}-${{ github.event.pull_request.number }}
  cancel-in-progress: true
```

que é o certo — sem ele cada push do trem empilhado deixa uma corrida órfã queimando runner. Mas
o preço é que **todo push no vagão com PR aberto cancela a corrida anterior**, e cada
cancelamento vira notificação para o owner. Empurrar cinco fatias em uma hora não produz cinco
medições: produz **uma** medição e **quatro alertas de cancelamento**. O sinal que o owner recebe
fica pior quanto mais frequente é o push — exatamente o oposto do que a regra de push frequente
quer.

A carga desmancha o conflito porque **as duas metades da regra vivem em ramos diferentes**:

| | frequência | acorda CI? | gera cancelamento? |
|---|---|---|---|
| `cargo/**` (sem PR) | **por fatia** | não — os portões são `pull_request`-only | não |
| vagão (com PR) | **por marco** | sim | só quando há o que medir |

**Marco** é um estado que vale medir, não um relógio: um degrau da escada fechado, uma carga
drenada para o vagão, o gate local passando, ou o owner pedindo. Fora disso, `cargo/**`.

### QUANDO empurrar o marco — a corrida em voo tem valor, e o vermelho é o que o zera (owner 2026-07-27)

> *"E procure empurrar para o vagão apenas quando o CI estiver terminado ou haver sinal em
> vermelho e sua carga estiver pronta, se nenhum sinal verde tiver sido emitido, aguarde o
> término. Ou melhor, se nenhum sinal vermelho tiver sido emitido, aguarde o término."*

Ter um marco pronto **não autoriza** o push por si só. Antes de empurrar no vagão, olhe a corrida
em voo e decida por ela:

| estado da corrida no HEAD do vagão | ação |
|---|---|
| **terminada** (qualquer conclusão) | **empurre** — não há corrida para cancelar, o push é limpo |
| **em voo, já com pelo menos um check vermelho** | **empurre** se a carga estiver pronta |
| **em voo, sem nenhum vermelho ainda** | **AGUARDE o término** |

**O princípio, que é o que vale guardar:** o valor de uma corrida em voo é a informação que ela
**ainda não entregou**. Cancelá-la antes do primeiro vermelho destrói uma medição inteira que
estava a caminho — inclusive um verde possível, que é a informação mais cara de produzir neste
projeto. Depois do primeiro vermelho, a corrida já entregou o que importava (a resposta é "não
passa"), e o que resta dela é marginal: cancelar custa pouco e o push novo mede o estado novo, que
é mais interessante que terminar de medir um estado já reprovado.

Note que a auto-correção do owner inverte o teste: **não é "espere se ainda não houve verde", é
"espere se ainda não houve vermelho"**. A diferença importa porque uma corrida longa emite verdes
parciais o tempo todo (cada lane que fecha) — se o gatilho fosse a ausência de verde, quase nunca
se esperaria. A ausência de VERMELHO é a condição certa: ela diz "esta corrida ainda pode terminar
verde", e é exatamente isso que não se joga fora.

**Como conferir, sem adivinhar:** `pull_request_read` com `method: get_check_runs` no PR do vagão.
Procure `conclusion` em `failure`/`timed_out`/`cancelled` entre os já `completed`; se não houver
nenhum e houver algum `in_progress`, a resposta é aguardar. **Não infira o estado do relógio nem
do "já deve ter acabado"** — esta é a mesma armadilha que em 2026-07-26 fez o integrador anunciar
que uma correção de gatilho não tinha funcionado quando a corrida existia e estava `in_progress`:
ele mediu o proxy em vez do fato.

**Enquanto aguarda, não pare** — continue produzindo nas cargas. A espera é do PUSH DO VAGÃO, não
do trabalho. É a mesma regra da seção anterior vista de outro ângulo: se a carga é o destino
default, aguardar o término da corrida não custa nada, porque não há nada represado esperando o
vagão.

**Operacional:** commit por fatia, push por fatia — **para a carga**. Uma carga que acumula
trabalho no worktree para "reportar no fim" está guardando o trabalho no lugar mais frágil que
existe. O briefing de carga deve exigir push, não só commit. E o integrador que está ele próprio
produzindo (não só drenando) abre uma carga para si — **não existe trabalho em voo que pertença
ao vagão**; o vagão é onde o trabalho pousa.

## POR QUE OS DEFEITOS ESTÃO APARECENDO AGORA — as duas decisões que os tornaram alcançáveis (owner 2026-07-27)

> *"se continuasse em C, veríamos isso muito mais tarde, e pior, se ainda estivesse com centenas de
> regressores que não possuem concorrência de nome em namespaces diferentes, tbm nunca teria pego
> até que acontecesse."*

Registrado porque a leitura ingênua da .31 é *"o backend nativo está cheio de bugs"*, e essa
leitura é falsa e desmotivadora. O que está acontecendo é o **oposto**: os defeitos não foram
introduzidos, foram **tornados alcançáveis**. Duas decisões independentes fizeram isso, cada uma
por um mecanismo distinto.

### Decisão 1 — matar o C. O C não escondia os defeitos: ele FAZIA O TRABALHO por nós

O caso do `no layout registered` é a demonstração limpa, e a medição está no repositório:

| | menções a `offset`/`size_of`/`align` |
|---|---:|
| `src/codegen/codegen.tks` — o emissor de C, **10.727 linhas** | **15** |
| `src/lir/lower.tks` + `src/backend/stackify.tks` — só dois arquivos | **220** |

O emissor de C escreve `typedef struct tk_t_<M> { <Ctype> <f>; … }` e **para por aí**: quem calcula
offset, tamanho e alinhamento é o `cc`. O backend nativo tem que calcular tudo — por isso 220
contra 15.

Consequência exata, e ela é mais forte do que "veríamos mais tarde": **enquanto o C carregava a
emissão, o defeito de `ClassBody` não contribuir layout não podia se manifestar — não porque
estivesse escondido, mas porque a responsabilidade era de outro.** Não havia bug para encontrar;
havia uma pergunta que nunca tínhamos precisado responder.

**A regra geral, que vale para muito além deste caso:** toda responsabilidade delegada ao compilador
C — layout, ABI, convenção de chamada, alinhamento, promoção de inteiro — é uma responsabilidade
sobre a qual **nunca precisamos estar corretos**. Matar o C não cria esses defeitos; ele **revela
que nunca os resolvemos**. Por isso a contagem de honest-stops subindo na .31 é a **medição
melhorando**, não o código piorando. Um honest-stop nomeado é uma pergunta que agora sabemos que
existe.

Corolário para o julgamento da versão: a .31 será avaliada por **quantas dessas perguntas foram
respondidas**, não por quantas apareceram. Uma versão que descobre 40 e responde 40 é melhor que
uma que descobre 5 — e a que descobre 5 é a que ainda está delegando.

### Decisão 2 — consolidar o regressor. 200+ projetos eram grandes e RASOS

O outro mecanismo é de configuração, não de responsabilidade.

Enquanto eram **200+ projetos**, cada regressivo era um projeto minúsculo compilado sozinho: **uma
namespace por build, sem vizinhos**. Nessa configuração, todo defeito que vive na *interação entre
unidades de compilação* é **inalcançável por construção** — não é que os testes não o pegavam, é
que não existia arranjo em que ele pudesse acontecer.

A consolidação em **9 diretórios** colocou muitas namespaces **no mesmo build**. Foi isso, e só
isso, que criou as condições para o defeito de monomorfização aparecer: `q006::Box` (não-genérica)
e `q084::Box<T>` só podem se atropelar se estiverem **na mesma tabela de tipos**, e antes nunca
estavam.

**E a configuração antiga é a que NÃO corresponde à realidade.** Um programa de usuário de verdade
tem muitas namespaces num build só. O regressor de 200+ projetos testava, em massa, um arranjo que
nenhum usuário habita. Daí a formulação que o owner usou — *"nunca teria pego até que acontecesse"*
— sendo "acontecesse" o pior lugar possível: na mão de quem usa.

**A lição, e é contraintuitiva o bastante para merecer estar escrita:** cobertura **não é
contagem**, é a variedade de interações alcançáveis. Ir de 200+ para 9 **diminuiu o número de
testes e aumentou o que eles pegam**, porque os defeitos que importam num compilador vivem entre
unidades, não dentro delas.

### O custo da densidade, que é real e tem antídoto

Ambiente denso **encontra** defeitos e ao mesmo tempo **confunde a atribuição** deles — e este
documento não seria honesto se omitisse que isso já custou caro aqui.

O integrador olhou os três `no layout registered`, viu que as fixtures envolvidas tinham nomes
homônimos em namespaces diferentes, e formulou a hipótese de que fossem parentes do defeito de
monomorfização. **Estava errado.** Duas namespaces com `struct Svc` homônimo compilam e rodam; uma
classe única, uma namespace, zero homônimos, dá o stop. As fixtures eram homônimas **por
coincidência** — eram simplesmente as que usavam `class`. A densidade fez duas características
co-ocorrerem, e a co-ocorrência passou por causa.

O antídoto é o que a carga fez para derrubar a hipótese: **repro mínimo**. Isolar uma classe, uma
namespace, e ver o stop aparecer mesmo assim.

**A dupla certa, nesta ordem:** **densidade para ENCONTRAR, isolamento para ATRIBUIR.** Um ambiente
denso sem a disciplina do repro mínimo produz diagnósticos plausíveis e errados — que é a única
coisa pior que não achar o defeito, porque manda o conserto para o lugar errado.

> **[AUDIT] CATEGORIA C — SEM FONTE**
>
> As armadilhas listadas são conclusões de quem mediu — cada uma documenta um incidente real (git stash de 2026-07-25, auditoria de 2026-07-26, etc.). Fica como conselho prático, não como lei.
>

## Armadilhas do worktree compartilhado

- **NUNCA `git stash` em worktree de vagão.** O `.git` é compartilhado entre todos os worktrees e a
  **pilha de stash é global**. Um `git stash -q` numa árvore já limpa não cria entrada (sai 0 em
  silêncio), e o `git stash pop -q` emparelhado vai então buscar o topo da pilha **de outro agente** —
  em 2026-07-25 isso deletou um design doc alheio ao vagão. Para comparar antes/depois use
  `git archive` ou copie para `/tmp`.
- **Um achado de auditoria vale no snapshot em que foi feito, não no topo.** Dois achados de revisores
  na .31 eram verdadeiros no worktree auditado e **já corrigidos num vagão acima** (a guarda de nome
  de membro do `ar`, que chegou no commit `d1aab7ba`). Reverificar no TOPO antes de virar trabalho não
  é desconfiança do revisor — é o passo que evita um vagão de correção inútil.
- **Ramo de erro inalcançável não é bug.** Um `error => ""` cuja condição de entrada é a negação exata
  da condição de erro do chamado não pode disparar. Vira higiene (ramo morto que finge ser possível),
  não MEDIUM de corrupção.
- **Intercalação de stdout com stderr não é causalidade.** O nome do teste vai para stdout, que é
  block-buffered fora de tty; o pânico, o `abort()` e o segfault vão para stderr, sem buffer. Um crash
  se atribui ao último nome que passou por um flush — em 2026-07-26 isso custou uma investigação
  inteira e fez uma carga reportar um defeito de compilador FABRICADO ("acrescentar campo a um struct
  quebra um teste de spine não relacionado"); o teste real estava ~66 nomes adiante. Medido depois:
  numa suíte pequena a perda é TOTAL — 41 testes, zero linhas no arquivo. Corrigido na raiz
  (`tk_flush_out` antes do corpo de cada teste, `709d41c1`), mas a disciplina fica: **antes de tratar
  uma atribuição de crash como fato, reproduza com `stdbuf -o0`.** Vale para qualquer saída em que
  dois descritores com políticas de buffer diferentes contam a mesma história.

## LEAD TIME É O CUSTO, CYCLE TIME NÃO É (owner 2026-07-27)

> *"Não, ela custou 2h08m19s, o tempo de build conta, pois precede a ocorrência. É a diferença de
> lead time e cicle time."*

Correção de **medição**, não de opinião, e por isso fica gravada: o integrador reportou o **cycle
time** de uma falha e o apresentou como o custo dela.

O episódio: a lane `cli surface` foi vermelha porque afirmava a prosa de um ruling já superado.
O integrador mediu as duas lanes que falharam — **23 segundos** — e concluiu que "a falha custou 23
segundos, o caro é a re-execução". O owner refez a conta e ela **fecha exata**:

```
2h07m56s   o bloco de artefato — A FILA
+     23s   as duas lanes cli surface — O TOQUE
= 2h08m19s
```

**O build não é despesa alheia à falha: é a fila em que a falha esteve parada.** Ele *precede* a
ocorrência, então entra no relógio. Cycle time é o tempo de toque no item; **lead time é o relógio
desde o instante em que o defeito ficou descobrível até ser descoberto**, e é ele que se paga.

**O que essa distinção reordena, imediatamente:**

- **Otimizar cycle time aqui não vale nada.** Fazer o `cli surface` rodar em 5s em vez de 23s
  economiza 18 segundos de um lead time de duas horas.
- **A lacuna de fail-fast passa a ser defeito de LEAD TIME.** Medido no mesmo run: `artifact /
  macos-arm64` terminou 02:56:19; `cli surface / macos-arm64` só pôde começar 03:33:08, porque
  `needs: artifact` numa matriz espera **todas** as pernas e `` levou
  38m48s. Uma assertion de 9 segundos esperou **36m49s** por um binário que ela nem testa.
- **A investigação de tempo de build muda de categoria.** Não é fatura de runner: enquanto a perna
  mais lenta levar 38m48s, esse é o **piso de lead time de todo defeito** que o repositório é capaz
  de detectar depois do build. É o tempo que qualquer erro fica invisível.

**A regra operacional que sai disso:** ao relatar o custo de uma falha de CI, relate o **lead
time** — da descoberta possível à descoberta real — e diga qual parte é fila e qual é toque.
Relatar só o toque faz uma falha cara parecer barata, que é exatamente o erro que esta seção
registra.

**E o corolário desconfortável, que é o motivo de a nota existir:** o defeito era detectável por
`grep -rn TEKO_BACKEND scripts/` **antes** do merge, em segundos. O integrador drenou a excisão da
env var sem varrer os scripts que afirmavam sobre a mensagem antiga. Lead time de 2h08m19s para um
defeito de lead time potencial de dois segundos — **a varredura de consumidores de uma string que
se está removendo faz parte da remoção**, não é zelo opcional.

### Drenar tudo de uma vez — CONSELHO SITUACIONAL, não lei (escopo corrigido pelo dono, 2026-07-29)

> **AVISO DE ESCOPO.** A citação abaixo é real, mas a LEI que a rodeava não era dele. O dono
> corrigiu-o em 2026-07-29: *"Esse de mergear tudo e empurrar foi a sessão anterior quem escreveu,
> não eu, apenas pedi uma vez pra fazer isso pq ela estava com acúmulo e conflitos."*
>
> Ou seja: pediu-o UMA VEZ, para um estado concreto de acumulação e conflito. Uma sessão anterior
> generalizou-o em regra permanente e deu-lhe título de lei. **Vale como conselho quando há
> acumulação; não vale como proibição de drenar um marco isolado.**
>
> E repara que a secção SEGUINTE deste mesmo ficheiro — a janela de um minuto, essa sim marcada
> como corrigida pelo próprio dono — é mais permissiva: *"interromper um CI no início não é
> problema"*. Quando as duas discordam, manda a janela: o critério é o TEMPO DECORRIDO da corrida,
> não a contagem de cargas.
>
> O custo medido continua verdadeiro e continua a valer a pena evitar — mas é uma OBSERVAÇÃO de
> quem mediu, não um ruling.

### O texto original, tal como a sessão anterior o escreveu

> *"quanto a ordem, deveria drenar tudo de uma vez"*

O integrador drenou carga a carga, cada uma com seu merge e seu push. **Custo medido num único
dia: cinco pushes no vagão → cinco corridas de CI e quatro cancelamentos.** Drenando tudo junto
seria **uma** corrida. Cada corrida custa da ordem de duas horas de runner e cada cancelamento vira
alerta para o owner.

**O erro conceitual:** tratar cada CARGA como um marco. A carga não é marco — ela já está protegida
no seu próprio branch, que não acorda CI. **O marco é o DRENO**, e um dreno é um evento, não uma
sequência.

**Operacional:** acumule as cargas prontas, faça TODOS os merges **localmente**, e empurre **uma
vez**. Se uma delas conflitar, ela sai do lote e vira carga de reconciliação — o lote não espera
por ela, e nem por isso vira lote de um.

Isto é a mesma lei da seção anterior vista do outro lado: se a carga é o destino default de push,
o vagão recebe marcos; e drenar N cargas é **um** marco, não N.


## A JANELA DE PUSH — UM MINUTO (dono, 2026-07-27, corrigido pelo proprio dono)

Refina a regra anterior ("empurrar so com o CI terminado ou sinal vermelho"), que na pratica me
fazia segurar carga por medo de cancelar. O criterio real e de TEMPO DECORRIDO:

> *"interromper um CI no início não é problema, problema é fazer isso depois de uns 5 min do CI
> ainda em execução."* — e, logo em seguida, apertando: *"considere 1min ao invés dos 5min que
> mencionei."*

**A JANELA E DE UM MINUTO.** Passado isso, a corrida ja esta trabalhando e cancelar joga fora
medicao real. Na pratica isso quase elimina o meio-termo: ou se empurra IMEDIATAMENTE, ou se espera
a corrida fechar.

Por que um minuto e nao cinco, medido nesta lane: o `artifact` e o gargalo e ele comeca quase
imediato — os quatro produtores entram em ~40s do inicio do run. Aos 3 minutos eles ja estao no meio
da escada de bootstrap. "Recem-comecado" e "produtor no meio do trabalho" praticamente coincidem, e
por isso a folga de cinco minutos era generosa demais para valer como criterio.

**O erro que isso corrige, e ele foi meu:** eu empurrei um commit so-de-documentacao sobre uma
corrida madura e destrui o veredito do degrau 4 por inteiro — `Memory paranoid`, `test /`,
`ar validation`, `regressor` e `cross-arch` foram todos CANCELADOS sem terem comecado, e os portoes
"falharam" apenas por recusarem tratar cancelado como passe. O degrau 4 so foi medido no ciclo
seguinte.

**Corolario operacional:** documentacao acumula e sai junto com o proximo dreno de produto. Um
commit de doc nunca justifica cancelar uma corrida madura — ele nao tem pressa nenhuma e a corrida
tem.

## Faixas de códigos de saída, e porque a regra nasceu (2026-07-29)

O corpus de `examples/regressions/own_native` identifica cada fixture por um código de saída único
(`main.tks`, `bad = N`). Com vários agentes a acrescentar fixtures em paralelo, **dois pediram o
mesmo 53** no mesmo dia — o degrau 20 e a lane das igualdades `char`/`[]T`. Só foi apanhado no
dreno, e a resolução foi renumerar um deles à mão.

**Falha de sequenciamento do integrador, não dos agentes**: cada um recebeu "usa o próximo livre" e
ambos leram o mesmo estado da árvore, porque foram despachados sobre a mesma base.

**A regra: quando houver mais de um agente vivo a poder tocar no corpus, atribuir FAIXAS disjuntas
no briefing, não "o próximo livre".** Exemplo do dia em que a regra nasceu:

| agente | faixa |
|---|---|
| degrau 22 | 57–59 |
| dissolver `bulk` | 60–69 |
| degrau 23 | 70 em diante |

A faixa vai no briefing com a razão explícita ("a faixa X está reservada a outro agente que corre em
paralelo"), para o agente não a alargar por iniciativa própria ao ver códigos livres.

**Sintoma correlato da mesma causa**: a mesma sessão viu dois agentes generalizarem a MESMA função
de dispatch de comparação sem se verem, produzindo conflito semântico no dreno. **Antes de despachar
trabalho concorrente, verificar se os âmbitos partilham um ponto de dispatch ou um recurso numerado
globalmente** — códigos de saída, faixas de teste, tabelas de registo de builtins.

## Autonomia do integrador — ruling do dono (2026-07-29)

Literal: *"Se está pronto, não precisa me perguntar, despache, se está feito, drene."*

**Não perguntar para despachar o degrau seguinte, nem para drenar trabalho concluído.** O dono decide
desenho, quebra de superfície, tensão lei-contra-lei e promoção. A escada de degraus e o dreno são
execução, e a execução é minha.

O que CONTINUA a precisar dele, e não se dilui com esta autonomia:

- **promover / fundir o PR** — nunca;
- **tirar um vagão de draft** — nunca;
- **quebra de superfície** ou semântica de linguagem (foi ele que cravou `.len`, a fábrica de
  `error`, `join` achatado, o corte a fazer barulho);
- **desligar ou abrandar uma medição que ele pediu** — a régua do `fixpoint_backend` é o caso vivo:
  reduzi-la de quatro para duas pernas foi ordem dele, não iniciativa minha;
- **alargar o âmbito de uma lane que está a fechar** — o `bulk` e o array-literal foram ambos
  perguntados antes, e ambos aprovados; a pergunta era legítima porque era âmbito, não execução.

### A correcção que a mesma sessão obrigou a fazer duas vezes

**Uma faixa aberta não é uma faixa.** Atribuí "60 em diante" a um agente e "70 em diante" a outro; o
segundo colidiu com o primeiro no código 70, e antes disso dois agentes tinham pedido o mesmo 53.
**Faixas de códigos de saída do corpus atribuem-se FECHADAS nos dois lados** (`100 a 109`), com a
instrução de pedir mais se não chegarem.

### O contorno legítimo do force-push, para agentes presos

As worktrees deste repositório partilham o object store (`/home/user/teko-lang/.git`). Um agente que
rebaseie fica com a branch impublicável (force-push bloqueado para toda a gente, dono incluído) —
mas **não precisa de publicar**: commita local, diz o SHA, e o integrador drena do objecto
partilhado. Nenhuma lei é revogada e nada se perde. Aconteceu duas vezes nesta sessão, e nas duas o
agente parou correctamente em vez de forçar.

## `theory/**` é o campo de provas do agente — e eu não o usei (dono, 2026-07-30)

Eu relatei que o agente das relocações arm64 *"honestamente não pôde provar sem hardware arm64"* e
que ficava à espera do `test / macos-arm64` do vagão. O dono cortou:

> Como não? É pra isso que DEVE usar uma 'theory/**'.

Ele está certo, e a falha é de **despacho**, não do agente: todos os briefs daquele dia diziam
*"você não tem esse host, então diga honestamente o que não conseguiu verificar"* — quando a
instrução correta era *"empurre para `theory/**` e prove no host real"*.

### O que a fast-lane realmente oferece (medido, não suposto)

`.github/workflows/agent-fast-lane.yml`, gatilho `push: branches: ['theory/**']` (exclusivo, por
ruling do dono no mesmo dia):

- **O host é escolhível**: `runs-on: ${{ github.event.inputs.runner || 'macos-latest' }}`, opções
  `macos-latest` · `ubuntu-latest` · `ubuntu-24.04-arm` · `windows-latest`. **Um push simples corre
  em macOS-arm64**, que era exatamente o host de que o agente precisava.
- **Não é smoke test**: gen1 pela rota C, **o ponto de fixo (gen2 == gen3)**, as sondas, provisiona
  **mingw e wasmtime**, corre a **suíte inteira**, e acaba no portão de **no-skips**.
- O cabeçalho manda **TROCAR** o host, nunca acrescentar uma segunda perna — uma de cada vez, de
  propósito.

Ou seja: um push dava a prova do link real em arm64. O agente entregou meia prova por omissão minha.

### A regra, para todo brief futuro

Um agente que não tem o host **não declara a limitação e passa** — ele **empurra `theory/<nome>`**
com o mesmo conteúdo da sua `cargo/**` (a `cargo/**` é a rede, a `theory/**` é o campo de provas) e
**reporta o nome da branch**. Trocar o host precisa de `workflow_dispatch`, e os agentes batem em
403 na API do GitHub — logo **o dispatch é do integrador**. O agente empurra e reporta; eu troco o
runner.

**A limitação honestamente declarada continua a valer como último recurso**, não como primeira
resposta. "Não consegui verificar" só é aceitável depois de a fast-lane não servir, não em vez dela.

### E uma recomendação minha que estava errada duas vezes

Quando o dono quis travar a fast-lane em `theory/**`, eu recomendei **não** travar. Ele travou. Só
ao medir a lane percebi porquê: é a exclusividade que a torna um campo de provas previsível e
barato para os agentes. Recomendei mal, e o erro só apareceu quando precisei da ferramenta que a
minha própria recomendação teria diluído.

### O agente dispara o próprio CI — mas nunca espera por ele (dono, 2026-07-30)

> Em theory, prefira por CI de push, isso resolve o problema e até o agente consegue disparar, o
> problema é que ele vai ficar idle aguardando resposta.

**CI de push, não de dispatch.** Duas razões, e a segunda foi medida:

1. Um push a `theory/**` já dispara a `agent-fast-lane.yml` — o agente aciona a validação completa
   sozinho, sem pedir nada ao integrador.
2. **`workflow_dispatch` não existe para um workflow que só vive numa `theory/**`.** Medido: a API
   devolve **404**. O GitHub só o expõe quando o ficheiro está no **branch default**. (Cuidado com a
   distinção que eu próprio conflacionei: a **fast-lane VIVE no default**, logo ela É dispatchável
   pelo integrador, e com escolha de `runner`. O que não é dispatchável é uma sonda criada só na
   theory.)

**E o modo de falha que o dono nomeou: o agente fica ocioso à espera.** Pior — ele fica ocioso **e
cego**, porque não tem acesso à API do GitHub (403 medido em vários agentes hoje). Esperar queima
tempo de parede sem forma nenhuma de ler o resultado.

**A disciplina, para todo brief:**

1. **Empurre a theory no momento em que aparecer uma pergunta que só um host responde** — não ao
   fim. Se a resposta pode mudar o desenho, quer-se a resposta antes de construir sobre suposição.
2. **Siga imediatamente.** Nunca bloqueie no CI.
3. **Reporte o nome da branch E a pergunta feita.** O integrador lê o CI e devolve a medição; a
   conversa do agente é retomável por `SendMessage`, logo ele continua de onde estava com o dado na
   mão, sem ter esperado.

O corolário para o integrador: **ler o CI de theory é trabalho meu, não do agente.** Se eu não o
fizer, o agente entrega sobre suposição — e foi exatamente o que aconteceu com a relocação arm64,
onde eu aceitei "não pude verificar" como resposta em vez de ter mandado medir.

### O CI de theory é do INTEGRADOR, nunca do agente — e a razão é contaminação (dono, 2026-07-30)

> Logo, melhor tu mesmo criar o CI e não o agente, para evitar que ele faça um cherry-pick da theory
> para a branch de trabalho e colha o CI restrito junto

**Nenhum agente cria ou edita ficheiro sob `.github/workflows/`, em NENHUMA branch, incluindo
`theory/**`.**

**ISTO NÃO É HIPÓTESE — JÁ ACONTECEU.** O dono, ao ler a regra: *"foi a falta dessa guarda que fez a
fast-lane ir para a main na outra sessão"*. A `agent-fast-lane.yml` chegou à `main` exatamente por
este caminho, numa sessão anterior. A regra tem cicatriz, não suposição, e deve ser lida assim.

**E a ironia, registada honestamente:** o artefacto desse acidente é hoje **load-bearing**. Um
workflow de `push` dispara da branch onde está, logo a fast-lane funcionaria vivendo só na theory —
mas `workflow_dispatch` só existe se o ficheiro estiver no **branch default**. Ou seja: é por a
fast-lane ter ido para a main por engano que o integrador consegue **escolher o runner** e pedir um
host específico. O acidente produziu a capacidade que hoje se usa. Isso não absolve o mecanismo; diz
apenas que o resultado de uma contaminação pode ser útil e continuar a ser contaminação — e que a
próxima pode não ter a mesma sorte.

O mecanismo do risco, que não é óbvio: se o workflow de theory estiver num commit **do agente**, ele
entra no histórico dele. No momento em que o agente faz cherry-pick ou merge da theory de volta para
a sua `cargo/**` — o movimento natural, e o que ele vai querer fazer — **o CI restrito viaja com o
conteúdo** e chega ao vagão. O isolamento que o dono exige (*"desde que a theory não entre no
vagão"*) quebra sem ninguém notar, porque nada no dreno olha para ficheiros de workflow vindos de
uma `cargo/**`.

**E não custa nada obedecer, porque o agente não precisa de autorar workflow nenhum:** a
`agent-fast-lane.yml` já vive no **branch default** e dispara em todo push a `theory/**`. O agente
ganha a validação completa sem escrever um ficheiro. Sonda específica que a fast-lane não dê é
**pedida ao integrador**, que a monta na **sua própria** branch de theory (ex.:
`theory/sonda-toolchain`), que nunca se cruza com a do agente.

**Corolário para o agente:** a branch de theory dele é um **espelho** da `cargo/**` — mesmo
conteúdo, nada commitado só nela. Se nada existe apenas na theory, nada pode viajar de volta.

**Corolário para o integrador:** a minha própria branch de sonda **nunca** é merjada em nada. Ela
existe para ser lida e esquecida.


### O agente que precisa de esperar CI está TERMINADO — e faz handoff (dono, 2026-07-30)

> vamos manter o pace, 4 agentes no máximo, se algum precisar parar para executar uma teoria,
> considere-o terminado, e ele precisará te fazer um Handoff, assim consegue liberar a vaga
> (trabalhar assíncrono)

Isto resolve o problema que o próprio dono nomeou antes — o agente que empurra a `theory/**` e fica
**ocioso e cego** à espera do CI, porque não tem acesso à API do GitHub.

**A regra: esperar não é um estado permitido.** Um agente que chegou a uma pergunta que só o CI
responde está **terminado**. Ele:

1. escreve um **handoff** no relatório final — o que fez, o que empurrou (branch + SHA), **qual
   pergunta ficou pendente no CI**, e o que fazer com cada resposta possível;
2. termina, **libertando a vaga**;
3. o integrador lê o CI e, com o handoff na mão, **retoma** (por `SendMessage`, que preserva o
   contexto) ou **despacha novo agente** a partir do handoff.

O trabalho passa a ser **assíncrono por construção**: a vaga nunca fica presa a um agente que não está
a produzir. Com teto de **4**, uma vaga ocupada por espera é 25% da capacidade parada.

**Corolário:** o handoff do agente é um entregável, não um resumo de cortesia. Um relatório final que
não diz qual pergunta ficou no CI e o que fazer com cada resposta **não liberta a vaga de verdade** —
obriga o integrador a reconstruir o contexto.

### Escreveu? Comita e empurra. Na branch onde estiver (dono, 2026-07-30)

> Lição para os agentes: escreveu? Comitar e empurrar para a branch que estiver trabalhando
> "cargo ou theory".

Não é "empurre quando estiver pronto" nem "empurre ao fim do marco" — é **empurre ao escrever**. Vale
para `cargo/**` e para `theory/**` igualmente.

Custa ZERO: uma `cargo/**` não dispara CI, e uma `theory/**` dispara a fast-lane, que é precisamente o
que se quer quando há algo para medir.

**Já se pagou duas vezes num dia.** No primeiro reinício de container perderam-se sete commits meus
mais trabalho de dois agentes, porque eu segurava commits para um "marco". No segundo reinício, com a
regra em vigor, **as cinco branches de agente estavam integralmente empurradas e não se perdeu nada** —
verificado uma por uma, zero commits à frente do remoto.

### O handoff fica SEMPRE vivo (dono, 2026-07-30)

> mantenha o Handoff vivo SEMPRE, se algo acontecer, tenho como recuperar a sessão

`docs/memory/handoff-0.3.1.0.md` não é um documento de fim de sessão — é **estado corrente**.
Atualizar depois de cada dreno, cada despacho, cada ruling novo, e empurrar. Um handoff desatualizado
é pior que nenhum: ele parece autoridade e mente. (Aconteceu no mesmo dia em que foi escrito: listava
cinco branches como "por drenar" depois de eu as ter drenado, e a secção "decisões do dono" dizia
"nada" enquanto havia uma.)
