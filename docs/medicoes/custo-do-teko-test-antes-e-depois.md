# O custo do `teko test`, antes e depois — pico do pai, tempo do tier, e o `-O2` do regrcov

Medições de 2026-07-31, ramo `cargo/0.3.1.0-teste-por-processo`. Caixa partilhada: 4 CPUs, 16 GB,
quatro agentes a trabalhar em paralelo. Onde a carga da caixa muda o número, está dito na linha.

Método do pico: `/proc/<pid>/status:VmHWM` **do processo pai isolado**, amostrado a 1 Hz. Não é a
árvore — a árvore inclui os filhos e não responde à pergunta "o pai continua a segurar o que
compilou?".

---

## 1. O resumo

| grandeza | antes | depois |
|---|---|---|
| pico do processo PAI durante o tier de regressão | **3 105 140 KB (2,96 GiB)**, constante | **40 336 KB (39,4 MB)** |
| pico do PAI na corrida inteira | **12 472 332 KB (11,9 GiB)** → OOM-kill (137) | **40 336 KB** |
| a corrida `./out/teko test .` | **morreu** aos ~25 min sem chegar ao fim | **767 s (12m47), completa** |
| tier de regressão (13 regressores, 63 builds) | nunca terminou | **227,4 s** |
| portão unitário | 1161 testes | **1161 testes, 0 falhas** |

O antes é a mesma árvore sem as duas mudanças; o depois é `out5/teko`, construído pela rota C.

---

## 2. MUDANÇA A — o `teko-regrcov` passa de `-O0` a `-O2`

`build_regression_cov_exe` (`src/build/project.tks:5505`) compilava o compilador instrumentado sem
nenhuma bandeira `-O`; passa a compilá-lo com `REGRCOV_CC_OPT` (`src/build/project.tks:5527`), que
vale 2. Os dois lados da troca, medidos na caixa **livre**:

| lado | `-O0` (antes) | `-O2` (depois) | delta |
|---|---|---|---|
| `cc` do regrcov (TU de 17,95 MB) | **28,0 s** | **102,9 s** | **+74,9 s**, uma vez por corrida |
| compilar o `examples/regressions/own_native` com ele | **1163 s (19m23)** | **180,5 s** | **−982,5 s** |

Paga-se +74,9 s de `cc` uma vez e recupera-se ~982 s num único cenário; o tier compila treze
regressores, não um. **Saldo largamente positivo.** Na caixa carregada (três outros agentes com
`teko test` simultâneo) o mesmo `cc` a `-O2` levou **400,8 s** — o custo é real e é sensível à carga,
e mesmo aí o saldo se mantém, porque o `-O0` correspondente subiria na mesma proporção.

A primeira tentativa de medir o `-O0` foi **abortada no tecto de 10 minutos**; o número de 1163 s é
de uma segunda corrida, sem tecto, e está aqui como medida e não como estimativa.

### O que a medição também mostrou, e é maior do que o nível de `cc`

O mesmo `own_native` compilado pelo binário de **release** — o mesmo compilador, sem instrumentação
de cobertura — leva **1,0 s de CPU** (4,0 s de relógio). Ou seja:

| binário | compila o `own_native` em |
|---|---|
| release (`-O2`, sem cobertura) | **1,0 s de CPU** |
| regrcov `-O2` (com cobertura) | **180,5 s** |
| regrcov `-O0` (com cobertura) | **1163 s** |

**A instrumentação de cobertura (`CgMode::ProgramCov`) custa ~180× ao próprio compilador**; o nível
de `cc` explica apenas um factor ~6 disso. O gargalo do tier não é a bandeira `-O` — é a
instrumentação. Fica **reportado, não corrigido** aqui: é uma decisão de desenho do canal de
cobertura, não desta carga.

### Os outros três portões que passam `0` ao `run_cc` NÃO foram mexidos

Por ordem expressa: medir antes de estender. O binário do portão de testes
(`GATE_CC_OPT`, `src/build/project.tks:3539`) é a forma **oposta** do regrcov — 1195 testes CURTOS
sobre uma TU de 18,37 MB, do tamanho do compilador — e é o lado do `cc` que ali é caro: o `cc -O0`
do portão levou 34,6 s na corrida medida, e a razão `-O2/-O0` medida no regrcov (3,7×) põe o `-O2` do
portão acima dos 100 s para recuperar, no máximo, o que 1161 testes de milissegundos podem devolver.
**Não estendido, e o valor fica nomeado ao lado do outro para que a comparação se faça de relance.**
O mesmo para o analisador (`:4499`) e a cobertura por teste (`:4700`), que só correm sob bandeira de
dev — não medidos, e por isso não mexidos.

---

## 3. MUDANÇA B — o passo 1 do `teko test` ganha fronteira de processo

Dos cinco passos de uma corrida (compilar o projecto + `.tkt`, `cc` do portão, correr o portão,
compilar cada `.tkr`, correr cada `.tkr`), quatro já corriam em filhos e devolviam a memória ao sair.
O passo 1 corria no próprio processo e, como o alocador de regiões só liberta na terminação, ficava
residente através de tudo o resto.

`test_project` (`src/build/project.tks:5614`) passa a despachar três passos, cada um no seu processo,
com o pai a re-invocar-se a si próprio — o padrão já provado do `compile_regressive`:

| passo | onde | função |
|---|---|---|
| `build` | filho | `test_stage_build`, `src/build/project.tks:5707` |
| driver | o processo do utilizador | `test_drive`, `src/build/project.tks:5648` |
| `report` | filho | `test_stage_report`, `src/build/project.tks:5743` |

**O pico do PAI, medido isoladamente:**

| momento da corrida | antes | depois |
|---|---|---|
| enquanto o passo 1 compila | 3,10 GB (é ele que compila) | **2 296 KB** (espera por um filho) |
| durante o portão unitário | 3,10 GB | 7 416 KB |
| durante o tier de regressão | **3 105 140 KB**, plano | **40 336 KB** |

**≈77× menos no patamar do tier** — e o patamar de antes não era um pico momentâneo: era o que o pai
segurava através dos onze builds e das onze execuções.

O passo `report` existe porque a cobertura só se lê contra o programa TIPADO e só existe depois das
execuções: o passo 1 já saiu quando os despejos aparecem. O relatório re-verifica a fonte — uma
passagem de front-end, **35,0 s** na corrida medida, num processo que sai logo a seguir
(`recheck_frontend`, `src/build/project.tks:5785`).

---

## 4. O balão de 12 GB não era o passo 1 — era o motor de regex

A corrida de referência morreu por OOM com o pai a **12,4 GB**. Com o passo 1 já fora do pai, a
corrida seguinte **também** subiu — de 9 MB para 3,7 GB em ~300 s — ao chegar ao `own_native.tkr`.
Logo, o balão era outra coisa.

Reprodução sintética (um projecto-regressor com N cenários `Then stdout pattern` contra a saída do
mesmo binário), com o pai medido isoladamente:

| forma | HWM do pai |
|---|---|
| 130 cenários, saída de 130 linhas, `Then stdout pattern` | **1 987 456 KB (1,9 GB)** |
| 130 cenários, saída de **1** linha, `Then stdout pattern` | 9 236 KB |
| 130 cenários, saída de 130 linhas, `Then exit = 0` | 7 876 KB |
| **10** cenários, saída de 130 linhas, `Then stdout pattern` | 3 136 KB |
| **depois do arranjo** — 130 cenários, 130 linhas, pattern | **14 260 KB** |

O custo era do casador: a continuação era um `[]RegexNode` que cada passo RECONSTRUÍA — o `m_cont`
copiava a cauda para deixar cair a cabeça, e uma concatenação copiava a corrida de irmãos inteira por
cima da continuação do chamador — **em cada deslocamento inicial da busca**. Nada liberta dentro de
uma corrida, e o corredor de regressões julga a saída de **cada** cenário através do `is_match`.

Com célula cons (`MCont`, `src/regex/regex.tks:316`), a cauda é partilhada em vez de copiada, e a
cadeia de topo da busca é construída UMA vez para todos os deslocamentos (`is_match`,
`src/regex/regex.tks:527`): um padrão literal deixa de alocar por deslocamento. **1,9 GB → 13,9 MB,
139×.** Mesma linguagem, mesma ordem de retrocesso, mesmos veredictos — os 18 testes do módulo e os
1161 do portão passam.

Isto explica os OOM-kills que dois verificadores já tinham relatado sem os conseguir atribuir: o
custo estava no PAI, no julgamento da saída, não no compilador nem no cenário.

---

## 5. A corrida completa, depois (rota C, árvore fundida com a lane)

`./outB/teko test .` — **rc=0, 432 s (7m12)**, caixa livre:

```
lexer      224/224 files    0.3s     checker    8595/8595 items  18.7s
monomorph  43/43 instances 11.4s     consteval  543/543 consts    0.4s
emit test  17.5 MB          7.9s     cc test    bin/teko-tktest  28.3s
emit rcov  17.1 MB          7.5s     cc rcov    bin/teko-regrcov 102.1s
portão     1161 testes, 0 falhas, 4 shards
tier       13 regressores, 0 falhas, 64 builds, 211,4 s
recheck    31,4 s
total      432 s;  HWM do processo PAI 41 316 KB (40,3 MB)
```

Contra a corrida de referência da mesma árvore sem as mudanças: **morreu por OOM aos ~25 min sem
chegar ao fim do tier, com o pai a 12,4 GB**.

Uma corrida anterior, na mesma árvore mas **antes** de trazer a lane e com três outros agentes a
compilar em simultâneo, deu 767 s com o mesmo veredicto de portão (1161 verdes) e uma falha de
manifesto (M.3) que não era do código: o `teko.tkp:57` listava o `const_slice_of_str.tkr` que ainda
vivia noutro ramo. Depois do merge da lane, essa falha desapareceu — 13 regressores, 0 falhas. A
diferença de 767 s para 432 s é quase toda o `cc` do regrcov sob carga (400,8 s contra 102,1 s).

FIXPOINT verificado **na rota C** (`TEKO_BACKEND=c`), cadeia de três gerações sobre a árvore
fundida: `outB/teko.c` e `outC/teko.c` byte-idênticos, **10 572 426 bytes**. Os três builds da cadeia
saíram sem um único aviso. **Não prova nada sobre a rota nativa**, que não foi exercitada aqui.
