# Terminal native — `.tkbl`/`.tkhl` por-namespace + linker interno (0.3.1)

> **Convenção de nomes (sufixo `l` = link/intermediário).** O **par intermediário**,
> por-namespace, tempo de link, **pré-mono** = **`.tkbl`** (objeto) + **`.tkhl`** (header de
> símbolos). O **par publicado**, final, entregue = **`.tkb`** (objeto/pacote) + **`.tkh`**
> (header). Onde este doc dizia "`.tkb` intermediário", leia **`.tkbl`**; o `.tkb` sem sufixo
> é sempre o artefato FINAL publicado.

Base de leitura: `docs/design/reducao-memoria-arrays-0.3.1.md` (§6bis Eixo C e os parágrafos
do "terminal native") + crumbs `RM-C10`..`RM-C17`. Autor: arquiteto (SÓ design; não toca
código de produto). Este documento **dá forma** ao terminal native que o dono descreveu,
**amarrado ao Eixo C já ratificado** — não é um fork nem uma re-deliberação: é o
preenchimento do terminal de um pipeline que já está desenhado. Onde o Eixo C já decidiu,
aqui só se **cita e estende**.

> **DESIGN-AHEAD (a regra "adiantar o que der").** A rota native é a **última perna** do
> programa 0.3.1 — está **gated no marco de memória** (Eixos A/B entregam a meta ≤1,5 GB nas
> DUAS rotas; a rota native é o *endgame* arquitetural, não o que fecha a meta). Os crumbs
> `RM-C15`/`C16`/`C17` são **M4** e dependem das pernas NAT (encoders ELF/Mach-O/COFF,
> aarch64, resolução de target) já enfileiradas. Portanto este doc é **desenho adiantado**:
> fixa contratos, sabores de artefato, mapeamentos e pontos de decisão HOJE, para o
> implementer retomar em minutos quando as pernas NAT e o Eixo C (C10–C13) fecharem verde. O
> que permanece BLOQUEADO está marcado em §11.

---

## 1. Visão — o arbusto vira per-namespace `.tkbl`+`.tkhl` + linker interno

Hoje a build é whole-program e termina em UM `teko.c` que o `cc` compila. O dono corrige o
endgame: **`teko.c` é muleta**; a linguagem tem que emitir seu **próprio objeto linkável**.
A rota native é a **única a se beneficiar de `.tkbl`** — porque native é compilação separada
clássica, e `.tkbl`/`.tkhl` são exatamente o par "objeto intermediário + header de símbolos"
que a compilação separada precisa.

O "arbusto" (o programa inteiro como uma árvore densa de namespaces com recursão mútua) é
**podado em galhos independentes**: ao ir para native, o compilador emite, **POR NAMESPACE**,
um `.tkbl` intermediário (o typed/lowered daquela unidade, **pré-mono**) + o seu `.tkhl` de
link (a interface de símbolos daquela unidade). Um **linker próprio** — que conhece SÓ os
NOSSOS objetos, como um IR — associa os símbolos entre unidades (a FFI interna de `RM-C11`) e
roda os passos de pipeline que faltam **na ponta** (monomorfização + isel/regalloc/encode do
alvo). A saída final depende do destino (package/tool/lib/binary — §5).

Este arranjo dá **o mesmo ganho que C já tem** (gera `.o`, depois linka) — só que **o linker
é nosso**, opera sobre o nosso IR/`.tkbl`, e conhece a semântica Teko (visibilidade,
monomorfização pendente, ABI). É esse conhecimento que habilita os dois grandes ganhos
seguintes: **paralelização do build** (§6) e **build incremental** (§7) — no qual o `.tkbl`
é a própria **unidade de cache** (§7).

O terminal native NÃO é um novo pipeline: é a **realização native da saída-de-unidade
abstrata de `RM-C12`** (`UnitOutput`). Na rota C, a saída-de-unidade é um pedaço de texto
concatenado num `teko.c`; na rota native, é um `.tkbl`/`.o` no disco. Mesmo laço de streaming,
dois back-ends de emissão (`reducao…` 311-320, 546-559).

---

## 2. O pipeline — parse-per-unit → LINK/FFI-interna → check/lower/emit por namespace → linker interno → saída por destino

O pipeline do Eixo C já está desenhado (`reducao…` §6bis, crumbs C10–C13). O terminal native
**pluga na saída-de-unidade** sem re-estruturar o laço. Sequência:

```
1. parse-per-unit  (RM-C11)  → AST INCOMPLETA por namespace
                               (decls exp+pub: tipos/assinaturas/const + refs pendentes;
                                CORPOS não residentes)
2. LINK (barreira global, RM-C11) → FFI INTERNA (exp+pub: tipo Teko + símbolo + ABI)
                               resolve as refs pendentes; alimenta o checker de cada unidade;
                               TRANSITÓRIA (some após o streaming)
3. check+lower+emit POR NAMESPACE (RM-C12, fundidos; despejo = arena-drop / disco RM-C13)
     por unidade, em ordem determinística:
       (re)carrega corpos → checa contra a FFI interna → lower → EMITE a saída-de-unidade
       → derruba a região da unidade (o despejo)
     saída-de-unidade abstrata:
       • rota C      → pedaço de texto (concatena no teko.c)          [muleta]
       • rota native → .tkbl intermediário PRÉ-mono + .tkhl de link    [endgame, RM-C15]
                       POR NAMESPACE
4. LINKER INTERNO (o "linker próprio") — SEQUENCIADO POR DEPENDÊNCIA, memory-bounded (§2bis)
       • ordena as unidades em ORDEM TOPOLÓGICA (grafo vindo da FFI interna / barreira LINK)
       • por unidade (ou SCC — ver §2bis), em streaming:
           carrega o .tkbl → mono-na-ponta (lib/binary/tool-no-install) → isel/regalloc/encode
           → PODA (descarta o que a unidade não precisa mais) → EMITE direto pro .tkb/.o/.tkh
           final → derruba a unidade
       • associa símbolos entre unidades (a FFI interna no plano de linkagem;
         tabela de símbolos do .o = exp+pub global, privado = local)
       • junta via ld do SO (ou objfile_ar) no artefato final
       • segura SÓ a unidade/SCC corrente + a FFI interna (compacta)
5. SAÍDA POR DESTINO (§5): package | tool | lib (shared/static) | binary
```

**Ponto-chave do desenho:** os estágios 3 e 4 são **separados de propósito**. O estágio 3
emite artefatos **PRÉ-monomorfização, por namespace** (genéricos crus + pedidos de
monomorfização que cruzam unidades). O estágio 4 (o linker interno) é onde a monomorfização
**mora** — porque só na ponta se conhece o conjunto FECHADO de instanciações que cruzam todas
as unidades (§4). Isto casa com o modelo de pico do Eixo C: cada unidade emitida é despejada
(região derrubada), e o linker interno segura só a FFI interna (compacta) + a unidade
corrente.

---

## 2bis. O linker interno é memory-bounded — ordem topológica + poda ao carregar + SCC

O linker interno **NÃO carrega tudo em memória**. Ele é o mesmo princípio de despejo do Eixo C
aplicado à PONTA: processa as unidades em **streaming, em ordem topológica de dependência**, e
descarta cada uma assim que emite. O grafo de dependência vem **da FFI interna / barreira de
LINK** (`RM-C11`) — a mesma tabela que resolve os símbolos cross-namespace já descreve *quem
depende de quem*.

**O laço (memory-bounded por construção):**

1. **Ordena topologicamente** as unidades pelo grafo da FFI interna.
2. Para cada unidade (ou SCC — ver abaixo), em ordem: **carrega** o `.tkbl` → roda os passos da
   ponta (**mono-na-ponta** para lib/binary e para tool-no-install; isel/regalloc/encode) →
   **PODA** (a "jardinagem" aplicada no linker: descarta tudo o que a unidade não precisa mais
   — corpos já encodados, instanciações já materializadas, scratch) → **emite DIRETO** para o
   `.tkb`/`.o`/`.tkh` final → **derruba a unidade** (região de arena).
3. Segura só a **unidade/SCC corrente + a FFI interna** (compacta). O pico do linker é o MÁXIMO
   de uma unidade, não a soma — o mesmo teto do Eixo C, agora na etapa de link.

**CUIDADO DE DESIGN a registrar — o grafo TEM CICLOS (não é DAG).** A linguagem é um monólito
com **recursão mútua entre namespaces** (o checker resolve nomes/tipos cruzando namespaces,
`reducao…` 428-435). Logo o grafo de dependência **não é acíclico** — há ciclos. O linker
interno agrupa os **componentes fortemente conexos (SCC)**: namespaces mutuamente recursivos
são processados **JUNTOS, como uma única unidade de link**. A ordenação topológica é sobre o
**grafo condensado de SCCs** (que É um DAG). Um SCC vira a granularidade real de despejo do
linker — o que casa exatamente com a granularidade de unidade do Eixo C (namespace, ou um
grupo de namespaces mutuamente recursivos quando o ciclo existe; `reducao…` R5: se um
namespace/SCC sozinho piquear acima do guard, subdivide-se por REGIÃO de escopo interno, não
por arquivo). Assim o linker é **memory-bounded por construção**, sem exceção de ciclo.

**Nota de determinismo:** a ordem topológica + a ordem interna de cada SCC têm que ser
**canônicas** (chaves ordenadas, não ordem de iteração de `map`/`hashset`) — é a mesma âncora
de determinismo do fixpoint (R6, transitivamente `RM-C10`). Um SCC processado em ordem
não-determinística divergiria o `.o` e quebraria `gen2.o==gen3.o` (§8).

---

## 3. Os dois `.tkh` — header de LINK (por-namespace) × header PUBLICADO (final)

O `.tkh` de hoje é **um só**: a superfície `exp` agregada (`src/emit/tkh.tks:217`
`emit_tkh`), interface do usuário. O redesign native precisa **distinguir dois sabores**,
porque o `.tkh` passa a servir a dois tempos diferentes (link interno × FFI externa).
Nomeados e distintos:

### 3.1 `.tkhl` — o header de LINK, por-namespace (tempo de LINK interno)

- **O que é:** a interface de **símbolos** de UMA unidade no tempo de link — a materialização
  **em disco** da **FFI interna** de `RM-C11` restrita àquele namespace: `exp`+`pub`, cada
  entrada com **tipo Teko + símbolo de linkagem + ABI**. Par com o `.tkbl` (o objeto
  intermediário da mesma unidade).
- **Sabor:** **PRÉ-monomorfização** (assinaturas genéricas cruas). Companheiro do `.tkbl`
  intermediário por-namespace.
- **Tempo de vida:** **TRANSITÓRIO** — vive o link, some após o streaming; **NUNCA** é
  embarcado. É a "FFI interna no plano de linkagem" (`reducao…` 421-426, R8).
- **Nome + política:** `.tkhl` (`tkh`-link, sufixo `l`) para não colidir com o `.tkh`
  publicado; par com `.tkbl`. **Política DECIDIDA (dono 2026-08-24, §13.3 Opção 3A): `.tkhl`
  vai SEMPRE DIRETO EM DISCO** (par do `.tkbl`), reusando o serializer determinístico de
  `RM-C13`. **Não** há rota memória-primeiro — a tese da campanha é despejo-em-disco para bound
  de memória; segurar o `.tkhl` em memória contradiria o objetivo. O IO por unidade é o *preço
  aceito* do despejo (o cache incremental `RM-C14` reusa o `.tkhl` do disco direto).

### 3.2 `.tkh` — o header PUBLICADO, final (FFI externa / IDE)

- **O que é:** a superfície `exp` **agregada** do artefato inteiro — a interface do USUÁRIO
  (`src/emit/tkh.tks`, `RM-C17`). SÓ `exp`; `pub` jamais aparece (lei de visibilidade
  `tast.tks` M.4 + R8).
- **Sabor depende do destino (§5) — DUAS políticas de mono:**
  - **package** e **tool** → **PRÉ-monomorph, portátil** (poli; genéricos preservados). O
    package instancia no consumidor; o tool instancia no INSTALL (mono adiada). Ambos
    distribuem o `.tkb` pré-mono.
  - **lib (shared/static)** e **binary** → **MONOMÓRFICO** no build (para FFI com outras
    linguagens — C/Rust/… precisam de assinaturas concretas, sem genéricos; binary fecha a
    partir do `main`).
- **Tempo de vida:** **EMBARCADO** — entregue junto do artefato (`RM-C17`).

**A tabela de distinção (registrar) — headers E objetos, o par espelhado:**

| Artefato | Tempo | Escopo | Visibilidade | Mono? | Embarcado? | Papel |
|---|---|---|---|---|---|---|
| **`.tkhl`** (header link) | LINK interno | 1 namespace/SCC | `exp`+`pub` | PRÉ-mono | **não** (transitório) | associar símbolos entre unidades |
| **`.tkbl`** (objeto link) | LINK interno | 1 namespace/SCC | — (corpos) | PRÉ-mono | **não** (transitório) | objeto intermediário + **unidade de cache incremental** (§7) |
| **`.tkh`** (header publicado) | pós-build | agregado | só `exp` | package/tool: PRÉ · lib/binary: MONO | **sim** | FFI externa + IDE/intellisense |
| **`.tkb`** (objeto publicado) | pós-build | agregado | — | package/tool: PRÉ · lib/binary: MONO | **sim** | pacote distribuível (`.tkl`) / objeto final |

Convenção: sufixo **`l`** = **link/intermediário** (transitório, pré-mono); sem sufixo =
**final publicado**. Par intermediário = `.tkbl`+`.tkhl`; par publicado = `.tkb`+`.tkh`.

Os dois planos são **ortogonais** (R8): `.tkhl`/`.tkbl` carregam `pub` para o link;
`.tkh`/`.tkb` publicados **nunca** vazam `pub`. Reusar a projeção do `.tkb` estendida com `pub`
(`RM-C11`) para montar o `.tkhl` é correto — mas o emissor de `.tkh` (`emit_tkh`) tem que
continuar **filtrando só `exp`**; embarcar o `.tkhl`/`.tkbl` no par publicado por descuido é o
erro que R8 nomeia.

---

## 4. Onde a monomorfização mora — na PONTA (target), nunca no `.tkbl` intermediário

**Estado de hoje (verificado):** `src/build/project.tks:204-256` (`checked_program_of`) roda
`checker::monomorphize` (`src/checker/monomorph.tks:946`) **antes** de qualquer emissão, e o
`.tkb` de package (`:1145` `emit::serialize_program(prog)`) serializa o programa **POST-mono**.
Ou seja: hoje o `.tkb` carrega o monomorfizado. **O redesign inverte isto.**

**Regra do redesign (`reducao…` + descrição do dono):** o `.tkbl` intermediário por-namespace
é **PRÉ-monomorph** — genéricos crus + **pedidos de monomorfização** que cruzam unidades
(`reducao…` 441: "Pedidos de monomorfização que cruzam unidades" é o que a FFI interna
retém). A monomorfização roda **na ponta**, porque:

1. **Fecho global.** A monomorfização precisa do conjunto FECHADO de instanciações alcançáveis
   a partir do `main`/exports. Sob emissão por-namespace, esse fecho só é conhecido depois que
   TODAS as unidades foram vistas — logo, na ponta.
2. **Portabilidade do package.** Um package pré-mono é **reinstanciável** pelo consumidor
   (que combina os genéricos do package com os seus próprios tipos). Monomorfizar no package
   **congelaria** as instanciações e quebraria a composição.
3. **Bounded memory.** O `.tkbl` pré-mono é MENOR (uma cópia do genérico, não N cópias
   monomorfizadas), o que reforça o teto de pico do Eixo C.

**"Na ponta" tem DOIS momentos (a mono é adiada, mas para pontos diferentes):**

- Para **lib** e **binary**, "a ponta" é o **linker interno no BUILD** (estágio 4): monomorfiza
  no build, entrega binário/`.tkh` monomórficos.
- Para **tool**, "a ponta" é o **INSTALL**: o tool é distribuído `.tkb` pré-mono (igual
  package) e só monomorfiza quando instalado, virando executável standalone (§5.2).
- Para **package**, a mono **nunca** roda no produtor — roda no **consumidor** que instancia os
  genéricos (§5.1).

Em todos os casos o `.tkbl`/`.tkb` distribuído é **pré-mono**; só muda ONDE a mono acontece.

**Consequência de sequenciamento:** o passo `checker::monomorphize` sai do meio do frontend
(`checked_program_of`) e migra para o **linker interno** (estágio 4, para lib/binary) ou para o
**instalador** (tool). Para package, **nunca** roda no produtor — o `.tkb` final agrega todos
os namespaces ainda poli (§5.1). Ver **[DECISÃO DO DONO] 2**.

---

## 5. Saída por destino — QUATRO destinos, DUAS políticas de mono

O `Artifact` já existe (`src/build/init.tks`: `binary`/`static`/`shared`/`package`; enum
`Artifact::{Binary,Tool,Static,Shared,Package}` em `project.tks` — **`Binary` e `Tool` já são
separados**; SÓ a semântica muda). O terminal native especializa a saída. Resumo:

| Destino | Distribui | Mono? | Onde a mono roda | Linka em outro binário Teko? |
|---|---|---|---|---|
| **package** | `.tkb`+`.tkh` pré-mono/poli | PRÉ | no CONSUMIDOR (ao instanciar) | **sim** (biblioteca/dependência) |
| **tool** | `.tkb`+`.tkh` pré-mono/poli | PRÉ | no INSTALL (vira executável) | **não** (standalone) |
| **lib** (shared/static) | binário + `.tkh` monomórfico | MONO | no BUILD (linker interno) | via FFI (C ABI) |
| **binary** | executável + `.tkh` monomórfico | MONO | no BUILD (fecho do `main`) | **não** |

### 5.1 `package` → `.tkb`+`.tkh` pré-mono/poli, LINKA em outro binário Teko

- **`.tkb` final:** agrega os `.tkbl` intermediários de TODOS os namespaces num único artefato,
  mantido **PRÉ-monomorfização** (poli). É o `.tkb` reinstanciável que o **consumidor combina**
  com os próprios tipos — a mono roda quando o consumidor instancia.
- **`.tkh` final:** um único header, **só `exp`**, **PRÉ-mono/portátil** (§3.2).
- **Sem monomorfização, sem isel/encode:** package NÃO produz binário — entrega o par
  `.tkb`+`.tkh` (dentro do `.tkl`, como hoje `src/build/project.tks:1137-1168`), agora
  pré-mono em vez de post-mono. Este é o único ponto onde o `.tkb` de HOJE muda de sabor.

### 5.2 `tool` → `.tkb`+`.tkh` pré-mono (IGUAL package), mono no INSTALL → executável standalone

- **Distribuição:** um tool é um **helper/acessório de CLI distribuído como pacote** — sai
  `.tkb` **pré-mono, exatamente como package** (poli, portátil). A monomorfização é **adiada
  para a INSTALAÇÃO**.
- **No install:** o `.tkb` pré-mono é **compilado ao executável** (mono no install → isel/encode
  → binário standalone). Vira um executável de CLI autônomo.
- **NÃO linka em outro binário:** diferente de package (que é biblioteca/dependência), o tool
  **não** é linkado em outro binário Teko — é acessório standalone.
- É por isso que `Tool` e `Binary` são enums separados: mesma forma final (executável), mas
  **política de distribuição/mono oposta** (tool distribui pré-mono e monomorfiza no install;
  binary monomorfiza no build).

### 5.3 `lib` (shared/static) → binário MONOMORFIZADO no build + `.tkh` MONOMÓRFICO

- **Binário:** o linker interno **monomorfiza no BUILD** (fecho de instanciações da lib),
  isel/regalloc/encode por unidade, e junta num `.a` (static, `objfile_ar`) ou `.so`/`.dylib`/
  `.dll` (shared — hoje `project.tks:1134-1136` ainda é honest-stop "not yet implemented";
  ver §11).
- **`.tkh` monomórfico:** a interface `exp` já monomorfizada — **para FFI com outras
  linguagens** (C/Rust chamam símbolos concretos, sem genéricos).

### 5.4 `binary` → executável MONOMORFIZADO no build + `.tkh` monomórfico

- **Executável:** monomorfização no build (fecho a partir do `main`) → isel/encode por
  unidade → `ld` do SO junta os `.o` + os externals do runtime (`reducao…` 320) → executável.
- **`.tkh` monomórfico:** emitido junto (`RM-C17`), para linkar/estender o compilador e
  intellisense na IDE.

**Nota de ABI:** para `abi = "c"`, o checker de export FFI (`codegen::check_ffi_export`,
`project.tks:1175`) e o header C (`ffi_write_c_header`) continuam válidos — o `.tkh`
monomórfico é o análogo Teko-nativo do header C.

---

## 6. Paralelização do build

O linker interno + a emissão por-namespace habilitam **paralelizar o build por unidade** — o
próximo grande ganho de tempo (depois de memória). O mecanismo é **exatamente o de C**: o
compilador "miscompila" cada namespace **confiando no `.tkhl`/FFI-interna** (como o `cc`
compila cada `.c` confiando nos headers), sem precisar dos corpos das outras unidades.

**Pré-condição arquitetural (já garantida pelo Eixo C):**

1. A **barreira de LINK** (`RM-C11`) resolve TODAS as pendências cross-unit ANTES do estágio
   3. Depois dela, cada unidade tem tudo o que precisa (a FFI interna) para ser checada/emitida
   **em isolamento** → o estágio 3 é **embaraçosamente paralelo** por namespace.
2. O **determinismo** (`RM-C10` gensym + R6 frame) garante que a ordem de execução NÃO afeta o
   byte de saída — condição necessária para paralelizar sem quebrar o fixpoint.
3. O **despejo por unidade** (região de arena por namespace, `RM-C6`/`C12`) dá **isolamento de
   memória** entre workers: cada unidade numa região própria, derrubada ao fim.

**Sequenciamento:** paralelização é **posterior** a `RM-C12`/`C15` (a emissão por-unidade
serial primeiro, verde e byte-idêntica). Só então introduzir workers. NÃO é um crumb desta
onda — é o ganho que o desenho HABILITA (registrar como linha de roadmap, não como gate de
0.3.1). O fixpoint continua serial-determinístico; a paralela tem que reproduzir o mesmo byte.

---

## 7. Build incremental (RM-C14)

Já desenhado em `RM-C14` (`.crumbs/0069`): cache do **`.tkbl`** typed por-unidade em disco,
chaveado por `hash(unidade) + hash(fatia da FFI interna de que ela depende)`; recompila só o
que mudou (fonte ou assinatura `pub` de dependência). É o **caso persistente** do
despejo-em-disco de `RM-C13` — o mesmo `.tkbl` que bounded o pico serve de cache entre builds.

**Amarração ao terminal native — o `.tkbl` É a unidade de cache.** O `.tkbl` intermediário
por-namespace do terminal native é **exatamente a unidade de cache** do incremental: ele
**persiste em disco chaveado por hash**, e o **`.tkhl` da mesma unidade alimenta o grafo de
link junto** (a fatia da FFI interna de que a unidade depende — é o que o hash de dependência
captura). Um namespace cuja fonte e cujas dependências (via `.tkhl`) não mudaram **reusa o
`.tkbl`/`.o` cacheado** — não re-checa, não re-emite, não re-encoda. Como o linker interno
conhece o grafo de dependência (a FFI interna / os `.tkhl`), a invalidação é precisa: mudar uma
assinatura `pub` num `.tkhl` invalida os dependentes.

**Lei (RM-C14):** o incremental é **DESLIGADO no self-build/fixpoint** — um build limpo tem
que produzir o artefato byte-idêntico. É otimização de **tempo de dev**, não reduz pico, e
**nunca** influencia o byte do fixpoint. É `[dry]`, reseed-class `none`. O `.tkbl` cacheado
tem que ser byte-determinístico (mesmo frame de R6) para que uma reutilização de cache produza
o mesmo `.o` que uma recompilação limpa.

---

## 8. Migração do fixpoint (RM-C16) — de `teko.c` idêntico para objeto native reproduzível

Hoje o fixpoint é `gen2.c == gen3.c` (o `teko.c` emitido byte-idêntico). No endgame o critério
migra para **objeto native reproduzível** (`RM-C16`, `.crumbs/0106`):

- **Determinismo do `.o` (pré-condição):** sem timestamp, ordem estável de símbolos e seções,
  sem path absoluto embutido, relocations em ordem canônica, padding zerado. Auditar TODO
  `src/backend/objfile_*` + `objfile_ar` (o `objfile_ar` já ordena símbolos —
  `coff_lib_sorted_symbols`/`bsd_sorted_symbols`; o `objfile_elf` já separa
  `STB_LOCAL`/`STB_GLOBAL` deterministicamente, `objfile_elf.tks:36-127`).
- **Critério novo:** o harness/CI compara `gen2.o == gen3.o` (por unidade + binário juntado)
  em vez dos dois `teko.c`. Uma divergência é **causa-raiz no writer do objfile**, NUNCA um
  critério relaxado.
- **Remoção da muleta C (gated):** SÓ quando as 4 pernas native do CI (x86_64-linux,
  arm64-linux, arm64-macos, x86_64-windows) fecham verde E o objeto reproduz — aí `teko.c` +
  `cc` são REMOVIDOS, as 2 pernas C do CI viram native, e o **reseed do bootstrap passa a ser
  o objeto/binário native**. Requer também `S16-SWEEP` (a morte da dependência de runtime C):
  §16 tira o runtime C, C16 tira o compile C — ambos fecham para "no C" ser verdade.

O **mapeamento visibilidade → tabela de símbolos do `.o`** é a peça load-bearing que o
terminal native introduz e que o fixpoint native exercita: `exp`+`pub` = símbolo GLOBAL
(`!local`, visível ao `ld`/link cross-unit); privado = `static`/local (nem entra na tabela). A
tabela de símbolos do `.o` **É** a FFI interna no plano de linkagem — `pub` alcança o `ld` sem
vazar para o `.tkh` (R8). O `Symbol.local` do backend (`objfile_elf.tks`) é o slot exato desse
mapeamento.

---

## 9. Relação explícita com cada crumb RM-C10..C17

O terminal native NÃO cria crumbs novos — ele **é** a segunda metade da cadeia RM já
enfileirada. Amarração um-a-um:

| Crumb | Papel no terminal native | Sítio |
|---|---|---|
| **RM-C10** (`0065`) determinizar gensym | **PRÉ-REQUISITO** do per-unit native: gensym derivado de `buf.len` global quebra a reprodutibilidade sob emissão por-namespace (o buffer passa a ser por-unidade). Bloqueia C11. Ver **[DECISÃO DO DONO] 4**. | `codegen.tks:3840,3932` + `next_temp()` |
| **RM-C11** (`0066`) parse-per-unit → LINK / FFI interna | A **FFI interna** (`exp`+`pub`: tipo Teko + símbolo + ABI) — a base do `.tkhl` (§3.1) e a tabela que o **linker interno** usa para associar símbolos. É o "linker próprio" no plano lógico. | `project.tks:352,272` + novo `src/build/link.tks` |
| **RM-C12** (`0067`) check+lower+emit fundidos por unidade | O laço de streaming cuja **saída-de-unidade abstrata** (`UnitOutput`) o terminal native realiza como `.tkbl`+`.tkhl`+`.o`. Despejo = região derrubada por namespace. | `project.tks:1165,2426` + `UnitOutput` |
| **RM-C13** (`0068`) dump typed `.tkbl` por unidade | O `.tkbl` intermediário **por-namespace** (agora PRÉ-mono, §4) — despejo em disco na fronteira do LINK que não funde. `serialize_unit`/`deserialize_unit`. | `tkb_frame.tks:369`, `tkb_read.tks:930` |
| **RM-C14** (`0069`) build incremental | Cache do `.tkbl`/`.o` por-unidade (§7) — o `.tkbl` É a unidade de cache; o `.tkhl` alimenta o grafo de link. DESLIGADO no fixpoint. | novo `src/build/incremental.tks` |
| **RM-C15** (`0105`) terminal native `.o` por unidade | **O coração deste doc:** cada namespace → `lower→isel→regalloc→encode→emit_elf/macho/coff` → `.o` no disco; região derrubada (o `.o` É o despejo). Visibilidade → tabela de símbolos. | `project.tks:1598,1615` + `objfile_*` |
| **RM-C16** (`0106`) fixpoint de objeto native + retirar muleta C | Migração do fixpoint (§8); remoção de `teko.c`+`cc` gated nas 4 pernas verdes + `S16-SWEEP`. | `objfile_*`, harness/CI, `codegen.tks` |
| **RM-C17** (`0107`) emitir + empacotar `.tkh` | O `.tkh` **publicado** (§3.2), monomórfico (lib/binary) ou pré-mono (package/tool). Só `exp`; FFI interna nunca embarca. | `emit/tkh.tks:217`, `header.tks:144` |

**A monomorfização-na-ponta (§4) e os dois `.tkh` (§3) são a extensão que este doc acrescenta
ao desenho de C15/C17** — não os re-delibera; concretiza *onde* a mono roda (linker interno,
estágio 4) e *qual* header é qual (link `.tkhl` × publicado `.tkh`).

---

## 10. Restrição SQLite / no-C (LEI do doc — registrar)

**Restrição firme do dono, com força de lei neste projeto:** qualquer integração SQLite (por
exemplo, se o `.tsym`/debug-info virar consultável) tem que ser **construção NATIVA em Teko**
— **ABI/syscall direto OU driver próprio na stdlib**. **NUNCA** linkar `libsqlite3` C.

- Isto é coerente com o `TEKO_ROADMAP_DB.md` (Princípio 0: "Native over FFI"; SQLite é a
  exceção FFI que só entra sob a keystone de link sob-demanda `DB-KEYSTONE`/`C7.20`). Mas para
  **o compilador/toolchain** a regra é MAIS forte: **zero `libsqlite3`** — não é "linkar sob
  demanda", é "não linkar C, ponto". Um SQLite consumível pelo toolchain seria
  **SQLite-em-Teko** (ABI/syscall ou driver puro-Teko), linha de **roadmap DB separada**
  (`teko::db::*`), **NÃO um gate de 0.3.1**.
- **O `.tsym` fica como blob binário Teko puro por ora.** O `.tsym` (sidecar de posições,
  projeto "poda/jardinagem") permanece um **blob binário Teko puro** — hoje é até um stub
  (`codegen.tks:9685` `tk_emit_tsym` emite um comentário "disabled (debug-only, native
  backend)"). Torná-lo consultável via SQLite-em-Teko é **linha de roadmap DB separada, não
  gate** do terminal native. O terminal native NÃO depende de, nem introduz, qualquer
  dependência de DB.
- **Coerência com C16 "no C":** todo o endgame é *remover* dependência de C (compile: `teko.c`+
  `cc` saem em C16; runtime: `S16-SWEEP`). Introduzir `libsqlite3` C seria andar para trás.
  A restrição SQLite/no-C é a mesma lei "no-C" aplicada ao futuro do `.tsym`.

---

## 11. O que permanece BLOQUEADO (DESIGN-AHEAD — §11)

Este doc é desenho adiantado. O que já é **executável hoje** vs. o que **espera dependência**:

**Executável agora (não bloqueado):**
- Todo o design deste doc (contratos, sabores de artefato, mapeamentos, os 4 pontos).
- `RM-C10` (determinizar gensym) — dep `RM-C5` já pousou; é rename mecânico byte-preservante.
- `RM-C11`/`C12`/`C13` — deps na própria onda RM; a FFI interna, o laço por-unidade e o dump
  `.tkbl` por-namespace são construíveis contra a forma DECLARADA do `.tkb` de hoje.
- Scaffolding que compila hoje: skeletons de `src/build/link.tks` (a barreira LINK +
  `InternalFfi`/`FfiEntry`/`link_units`), `src/build/incremental.tks` (`incremental_enabled`/
  `cache_key`), com doc-comments W15 e honest-stops onde a lógica ainda depende de C15.

**Bloqueado (aguarda dependência):**
- **`RM-C15`/`C16`/`C17` (M4):** dependem das pernas **NAT** (encoders `NAT-A4` Mach-O, `NAT-B1`
  ELF, `NAT-B3` COFF, `NAT-AARCH` aarch64-ELF, `NAT-XL` resolução de target) E de `RM-C12`
  verde na rota C. O `.o` por-unidade, o fixpoint de objeto e o `.tkh` publicado só fecham
  quando essas pernas fecham.
- **Monomorfização-na-ponta (§4):** a MOVIMENTAÇÃO física do `checker::monomorphize`
  (`monomorph.tks:946`) do frontend para o linker interno depende do estágio 4 (linker
  interno) existir — ou seja, gated em `RM-C15`. Até lá, o desenho fica registrado; a mono
  continua no frontend (whole-program) e o `.tkb` de package continua post-mono. **O flip
  para pré-mono é uma mudança de sabor de artefato → exige reseed** (superfície do `.tkb`).
- **`shared` lib (`.so`/`.dylib`/`.dll`):** `project.tks:1134-1136` é honest-stop hoje ("not
  yet implemented"). O `.tkh` monomórfico de lib (§5.2) só é entregável quando o shared-link
  fechar. **Permanece honest-stop até então.**
- **Paralelização (§6) e incremental-ON (§7):** posteriores a C12/C15 verdes e seriais; não
  são gate de 0.3.1.

---

## 12. Riscos e tensões de lei

Os riscos R4/R6/R7/R8 do Eixo C (`reducao…` §8) valem integralmente aqui. Específicos do
terminal native:

- **T-mono (§4) — flip pré-mono é mudança de superfície.** Mover a mono para a ponta muda o
  sabor do `.tkb` de package (post-mono → pré-mono). Isso quebra a **compatibilidade do `.tkl`
  publicado** (um consumidor 0.3.0 leria um `.tkb` post-mono; um 0.3.1 lê pré-mono). Mitigação:
  o flip é gated em `RM-C15` e **exige reseed** (mudança de superfície/ABI de artefato); marcar
  o `.tkb` com um **byte de versão de sabor** (poli vs mono) para o deserializador rejeitar o
  sabor errado com erro claro. **Não é tensão de lei** — é sequenciamento + versionamento.
- **T-tkhl (§3.1) — vazamento de `pub` para o `.tkh` (R8).** Reusar a projeção `.tkb`+`pub`
  para o `.tkhl` e, por descuido, alimentá-la em `emit_tkh`. Guarda: `emit_tkh` filtra só
  `exp`; o `.tkhl` é um artefato de disco TRANSITÓRIO (par do `.tkbl`, §13.3 Opção 3A),
  consumido só pelo link/incremental e nunca lido pelo emissor de `.tkh` — e apagado ao fim do
  build. Já coberto por R8 — reforçado aqui pela existência de DOIS headers.
- **T-det (§8) — não-determinismo do encoder.** O fixpoint native (`gen2.o==gen3.o`) exige
  determinismo TOTAL do `.o`. Já há ordenação de símbolos, mas o audit de `RM-C16` tem que
  cobrir toda fonte de não-determinismo (timestamps, paths, ordem de relocations). Causa-raiz
  no writer, nunca critério relaxado.
- **Sem tensão de lei não resolvida — nada HALTa.** Todos os pontos abaixo têm recomendação
  law-first; são **confirmações do dono**, não impasses.

---

## 13. Pontos de decisão do dono

> **Ruling do dono sobre a apresentação (registrar):** cada `[DECISÃO DO DONO]` traz **≥3
> propostas concretas**, cada uma com **exemplo** (código Teko / forma de artefato / mecanismo)
> e **prós/contras** medidos em quatro eixos — **memória**, **complexidade**, **conformidade
> com as leis/R8** e **risco de reprodutibilidade** (fixpoint). O dono decide **comparando o
> leque**; a recomendação vem SÓ no fim, com o "por que as outras perdem". Nenhum ponto HALTa
> (não há tensão de lei não resolvida) — são escolhas de engenharia sobre um piso law-first.

### [DECISÃO DO DONO] 1 — Duas tabelas (FFI interna × `.tkh`) — ✅ RATIFICADA: Opção 1A (dono 2026-08-24)

> **✅ RATIFICADA (dono 2026-08-24) — Opção 1A** (duas tabelas totalmente separadas: `.tkhl`
> `exp`+`pub` transitório × `.tkh` só `exp`). Bate com a recomendação do coordenador. Registro
> no `DECISION_LOG.md` D67. O leque abaixo fica como memória de deliberação.

**Problema.** A compilação separada cross-namespace DENTRO do programa precisa ver `exp`+`pub`
(o compilador chama MUITO símbolo `pub` interno). Mas o `.tkh` publicado é só `exp` (lei de
visibilidade `tast.tks` M.4). Como carregar `pub` para o link sem vazá-lo para o header (R8)?

#### Opção 1A — Duas tabelas totalmente separadas (`.tkhl` exp+pub transitório × `.tkh` exp)

O LINK monta uma tabela `.tkhl` (`exp`+`pub`, transitória, some após o streaming); o
`emit_tkh` monta uma tabela `.tkh` (só `exp`, embarcada) num passe independente.

```teko
/**
 * InternalFfi — tabela de link TRANSITÓRIA (exp+pub); NUNCA embarcada. Vive o link, some após.
 * @since 0.3.1
 */
type InternalFfi = struct { entries: []FfiEntry }

/**
 * Header — a superfície publicada, montada SEPARADAMENTE, SÓ exp.
 * @since 0.3.1
 */
type Header = struct { exports: []ExpDecl }
```

- **Prós.** Vazamento R8 **impossível por construção** (o `.tkh` nunca vê uma entrada `pub` —
  são tipos diferentes). Header enxuto (só `exp`). O `.tkhl` é derrubado antes do emit final →
  memória do header publicado é mínima. Clareza máxima: dois artefatos, dois papéis.
- **Contras.** Duas projeções da mesma decl (a assinatura `exp` aparece em ambas) → um pouco de
  duplicação de código de serialização. Pico transitório: `.tkhl` (exp+pub) coexiste com a
  fase de link (mas é compacto — assinaturas, não corpos).
- **Eixos.** Memória: **ótima** (transitório compacto, derrubado). Complexidade: média (duas
  projeções). Leis/R8: **máxima** (separação física). Reprodutibilidade: alta (duas tabelas
  determinísticas independentes).

#### Opção 1B — Uma tabela única com flag de visibilidade; o emissor filtra `exp` no `.tkh`

Uma só `LinkTable` com `exp`+`pub`, cada entrada com um campo `vis`. O link usa tudo; o
`emit_tkh` **filtra** `vis == Exp` ao escrever o header.

```teko
/**
 * LinkTable — tabela ÚNICA; cada entrada carrega a visibilidade. O link consome tudo; o
 * emissor de .tkh filtra exp. O RISCO R8 vira uma responsabilidade de UM filtro.
 * @since 0.3.1
 */
type LinkEntry = struct { symbol: str; sig: @Type(); vis: Visibility }

/**
 * emit_tkh_filtered — escreve o .tkh SÓ com as entradas exp (o filtro que segura o R8).
 * @param t a tabela única (exp+pub)
 * @return os bytes do .tkh (só exp)
 * @since 0.3.1
 */
fn emit_tkh_filtered(t: LinkTable): []byte | error {
    var exps = filter_exp(t.entries)
    write_header(exps)
}
```

- **Prós.** Uma projeção só (sem duplicação). Menos código. A tabela já existe (é a
  `InternalFfi` de `RM-C11`) — o header vira uma **vista filtrada** dela.
- **Contras.** **R8 depende de um filtro correr certo em runtime** — um bug no `filter_exp` (ou
  um caminho de emit que esquece de filtrar) vaza `pub` para o `.tkh`. É a exata falha que R8
  nomeia. Precisa de fixture-guarda dedicada (`tkhl_excludes_from_tkh`, §14) + revisão. A
  tabela única `exp`+`pub` fica viva mais tempo (não pode ser derrubada antes do emit do header,
  pois o header lê dela).
- **Eixos.** Memória: **pior** (tabela grande viva até o emit do header). Complexidade: baixa
  (uma projeção). Leis/R8: **frágil** (garantia procedural, não estrutural). Reprodutibilidade:
  alta (uma tabela determinística).

#### Opção 1C — `.tkh` "gordo" = exp+pub com `pub` marcado interno (consumidor ignora)

Um único `.tkh` embarcado que INCLUI `pub`, cada `pub` marcado `internal`; o consumidor
**ignora** as entradas `internal` ao resolver a API.

```teko
/**
 * FatHeader — .tkh "gordo": exp visível + pub marcado internal. O consumidor lê só o que NÃO
 * é internal. UM artefato serve link E publicação.
 * @since 0.3.1
 */
type FatEntry = struct { symbol: str; sig: @Type(); internal: bool }
```

- **Prós.** Um único artefato (nem `.tkhl` nem passe separado). O link e a publicação leem o
  mesmo arquivo. Cache incremental trivial (um blob por unidade).
- **Contras.** **VIOLA R8 diretamente** — `pub` FICA no header embarcado (mesmo "marcado
  interno"): incha o header, expõe a superfície interna do compilador ao mundo, e viola a lei
  de visibilidade `tast.tks` M.4 ("só `exp` alcança o `.tkh`"). Qualquer consumidor
  malicioso/curioso lê os `pub`. Muda a semântica do `.tkl` publicado. **Isto é a mudança que
  R8 proíbe.**
- **Eixos.** Memória: ruim (header embarcado inflado). Complexidade: baixa. Leis/R8:
  **reprovado** (viola R8 e M.4). Reprodutibilidade: alta, mas irrelevante — reprova na lei.

#### Recomendação → **✅ RATIFICADA: Opção 1A (duas tabelas totalmente separadas)** — dono 2026-08-24

1A é a única que torna o vazamento R8 **estruturalmente impossível** (tipos distintos, o `.tkh`
nunca segura um `pub`) e derruba o `.tkhl` cedo (melhor memória). **1C está fora** — viola R8/M.4
frontalmente (é exatamente o erro proibido). **1B perde** por transformar uma garantia
estrutural (R8) numa garantia procedural (um filtro que pode falhar) e por manter a tabela
grande viva mais tempo; a economia de uma projeção não paga o risco de vazamento de superfície.
A duplicação de projeção de 1A é mitigável reusando a projeção de decl do `.tkb` (`RM-C11`)
como base comum, filtrando `exp`+`pub` para `.tkhl` e `exp` para `.tkh` na saída. **O dono
ratificou 1A em 2026-08-24 (DECISION_LOG D67).**

### [DECISÃO DO DONO] 2 — Onde a monomorfização mora — ✅ RATIFICADA: Opção 2A (dono 2026-08-24)

> **✅ RATIFICADA (dono 2026-08-24) — Opção 2A** (100% no linker; `.tkbl` sempre pré-mono; mono
> só na ponta, com as 2 políticas de destino do §5). Bate com a recomendação do coordenador.
> Registro no `DECISION_LOG.md` D67.

**Problema.** Hoje `checker::monomorphize` (`monomorph.tks:946`) roda no frontend e o `.tkb`
sai post-mono. Sob emissão por-namespace, onde a mono deve rodar?

#### Opção 2A — 100% no linker (todo `.tkbl` pré-mono; mono só na ponta para lib/binary)

Nenhuma unidade monomorfiza; o `.tkbl` é sempre pré-mono. O linker interno (estágio 4)
monomorfiza no fecho global, só para lib/binary; package/tool ficam poli.

```teko
/**
 * emit_unit_object — emite o .tkbl PRÉ-mono da unidade (genéricos crus + pedidos de mono).
 * A monomorfização é adiada para link_and_monomorphize (estágio 4).
 * @since 0.3.1
 */
fn emit_unit_object(unit: LUnit, target: NativeTarget, out_dir: str): str | error

/**
 * link_and_monomorphize — no fecho global (todas as unidades vistas), instancia os genéricos
 * alcançáveis e encoda. Só para lib/binary; package/tool NÃO chamam.
 * @since 0.3.1
 */
fn link_and_monomorphize(units: []LUnit, ffi: InternalFfi, dest: Artifact): []byte | error
```

- **Prós.** **Fecho global correto** (o conjunto fechado de instanciações só é conhecido na
  ponta). `.tkbl` menor (uma cópia do genérico, não N) → melhor pico (Eixo C). Package/tool
  portáteis (pré-mono reinstanciável/instalável). Um único ponto de mono → fácil auditar.
- **Contras.** O linker faz MAIS trabalho (mono + isel + encode na ponta) → o estágio 4 é mais
  pesado por unidade (mitigado pelo streaming por SCC do §2bis). Inverte o comportamento de
  hoje → **exige reseed** + byte de versão de sabor (T-mono).
- **Eixos.** Memória: **ótima** (`.tkbl` compacto). Complexidade: média (mono migra de sítio).
  Leis: neutra. Reprodutibilidade: alta se o fecho for iterado em ordem canônica (R6).

#### Opção 2B — Split por destino (binary/lib mono no build; package/tool pré-mono)

O que 2A faz, MAS a mono de binary/lib roda **durante** o stream por-unidade (não num passe de
linker separado): cada unidade de binary/lig já sai monomorfizada.

```teko
/**
 * emit_unit — se o destino monomorfiza no build (binary/lib), instancia JÁ os genéricos desta
 * unidade; senão (package/tool) emite pré-mono. A decisão é por-destino, dentro do laço.
 * @since 0.3.1
 */
fn emit_unit(unit: Unit, ffi: InternalFfi, dest: Artifact): UnitOutput | error
```

- **Prós.** Sem passe de linker-mono separado. Para binary/lib, a unidade já sai pronta.
- **Contras.** **Fecho global quebrado para instanciações cross-unit.** Uma instanciação
  `List<Foo>` pedida na unidade A mas cujo genérico mora em B: quando A é processada, B pode não
  ter sido — a mono por-unidade não conhece o fecho. Precisa de um pré-passe que colete os
  pedidos cross-unit ANTES (que é... o linker de 2A). Ou seja, 2B **colapsa em 2A** para
  qualquer genérico cross-namespace — e o compilador é cheio deles. Duplicação de N instâncias
  do mesmo genérico entre unidades (cada unidade monomorfiza o seu) → mais memória e `.o`
  maiores. Risco de reprodutibilidade: mesma instância emitida em duas unidades → símbolo
  duplicado no link.
- **Eixos.** Memória: **pior** (instâncias duplicadas). Complexidade: alta (coleta cross-unit
  vira um pré-passe = 2A disfarçado). Leis: neutra. Reprodutibilidade: **frágil** (duplicação
  de símbolo).

#### Opção 2C — Mono lazy/on-demand por instanciação (cache incremental)

Cada instanciação `(genérico, args)` é materializada **sob demanda** e cacheada num mapa
global; a segunda vez reusa. Ao emitir uma chamada, força a instância.

```teko
/**
 * request_mono — materializa (ou reusa do cache) a instância (fn genérica, args de tipo).
 * On-demand: a primeira chamada instancia; as seguintes reusam a instância cacheada.
 * @param key  (símbolo genérico, substituição de tipos)
 * @return o símbolo mangled da instância monomorfizada
 * @since 0.3.1
 */
fn request_mono(key: MonoKey, cache: ref MonoCache): str | error
```

- **Prós.** Instancia SÓ o alcançável de fato (nada especulativo). Casa com o incremental
  (`RM-C14`): o cache de instâncias persiste entre builds. Potencialmente o menor volume de
  código emitido.
- **Contras.** **Cache global mutável = risco de reprodutibilidade #1.** A ordem de
  materialização (quem pede primeiro) tem que ser canônica ou o `.o` diverge (`gen2.o!=gen3.o`).
  Complexidade alta (gerir cache, invalidação, ordem determinística de flush). Estado global
  compartilhado colide com a paralelização (§6) — dois workers pedindo a mesma instância. É o
  oposto do "despejo por unidade" (o cache VIVE entre unidades, não é derrubado).
- **Eixos.** Memória: boa em volume, mas o cache é estado vivo cross-unit (não despejável).
  Complexidade: **alta**. Leis: neutra. Reprodutibilidade: **a pior** (cache global mutável +
  ordem de flush + tensão com paralela).

#### Recomendação → **✅ RATIFICADA: Opção 2A (100% no linker, `.tkbl` sempre pré-mono)** — dono 2026-08-24

2A dá o fecho global correto (a razão técnica dura: instanciações cruzam namespaces), o menor
`.tkbl` (melhor pico) e a portabilidade de package/tool — tudo law-first. **2B perde** porque
colapsa em 2A assim que há genérico cross-namespace (e há muitos), duplicando instâncias e
símbolos no meio do caminho. **2C perde** pelo cache global mutável, que é o maior risco de
`gen2.o!=gen3.o` e briga com a paralelização — o ganho de volume não paga o risco de
reprodutibilidade. Sobre 2A, a política por-destino do §5 se aplica na PONTA: **binary/lib**
monomorfizam no build (linker), **tool** monomorfiza no INSTALL, **package** no consumidor — o
`.tkbl` distribuído é sempre pré-mono. Gated em `RM-C15`, com reseed + byte de versão de sabor
(T-mono, §12). `Tool` e `Binary` já são enums separados (`project.tks`); só a semântica muda.
**O dono ratificou 2A em 2026-08-24 (DECISION_LOG D67).**

### [DECISÃO DO DONO] 3 — Dois `.tkh`: nome + política disco-vs-memória — ✅ RATIFICADA: Opção 3A (dono 2026-08-24, INVERTE a recomendação)

> **✅ RATIFICADA (dono 2026-08-24) — Opção 3A** (`.tkhl` **DIRETO EM DISCO**, par do `.tkbl`),
> nome `.tkhl`. **O dono INVERTEU a recomendação do coordenador (era 3B, memória-primeiro).**
> Ver a **retratação formal** ao fim da sub-seção. Registro no `DECISION_LOG.md` D67.

**Problema.** O header de LINK (`exp`+`pub`, tempo de link) vs. o header PUBLICADO (só `exp`).
Como nomear e onde materializar o de link?

#### Opção 3A — `.tkhl` em disco + `.tkh`

O header de link é um arquivo `.tkhl` no disco (par do `.tkbl`), ao lado do publicado `.tkh`.

```
build/obj/teko__str.tkbl     # objeto de link (pré-mono)
build/obj/teko__str.tkhl     # header de link (exp+pub) — em disco
dist/teko.tkh                # header publicado (só exp)
```

- **Prós.** O cache incremental (`RM-C14`) reusa o `.tkhl` do disco direto (grafo de link
  persistido). Simetria total com `.tkbl`. Auditável (dá pra inspecionar o arquivo). A fronteira
  do LINK que não funde (`RM-C13`) já escreve em disco — o `.tkhl` acompanha.
- **Contras.** IO por unidade mesmo quando o estágio 3→4 funde em memória (desnecessário no
  build limpo). Mais arquivos temporários a limpar.
- **Eixos.** Memória: ótima (nada retido; está no disco). Complexidade: baixa. Leis/R8: ok
  (transitório, não embarcado). Reprodutibilidade: alta (frame determinístico R6). IO: **alto**.

#### Opção 3B — `.tkhl` só em memória (materializa em disco só na fronteira que não funde)

O header de link vive na arena (parte da `InternalFfi`); só é serializado para `.tkhl` quando o
estágio não funde (a barreira do LINK global) ou quando o incremental quer cachear.

```teko
/**
 * link_header_of — projeta o header de link (exp+pub) da unidade EM MEMÓRIA. Só
 * serialize_link_header materializa em disco, e só na fronteira que não funde / no cache.
 * @since 0.3.1
 */
fn link_header_of(unit: IncompleteUnit): InternalFfiSlice
fn serialize_link_header(s: InternalFfiSlice): []byte | error
```

- **Prós.** **Zero IO no build limpo fundido** (o caso comum). O `.tkhl` só toca disco onde é
  inevitável (LINK global / cache). Melhor tempo de build limpo.
- **Contras.** Duas rotas (memória × disco) → mais código condicional. O incremental precisa da
  rota de serialização de qualquer forma (então 3B ainda constrói o `.tkhl` de 3A — é 3A + um
  fast-path em memória). Menos auditável no build fundido (não há arquivo para inspecionar).
- **Eixos.** Memória: boa (slice na arena, derrubada por unidade). Complexidade: **média-alta**
  (duas rotas). Leis/R8: ok. Reprodutibilidade: alta. IO: **mínimo**.

#### Opção 3C — Formato único de `.tkh` com seção/flag link-vs-publicado

Um só arquivo `.tkh` com DUAS seções: uma seção `[link]` (exp+pub) e uma `[public]` (só exp); o
consumidor lê `[public]`, o linker lê `[link]`.

```
# teko.tkh
[public]   exp fn parse(...): ...
[link]     pub fn parse_inner(...): ...   # NÃO embarcado no dist
```

- **Prós.** Um artefato só (menos arquivos). Simetria simples.
- **Contras.** **Risco R8 de novo** — se o `.tkh` distribuído carrega a seção `[link]`, `pub`
  vaza (é 1C sob outro nome). Para não vazar, é preciso STRIP da seção `[link]` ao empacotar →
  que é ter dois artefatos de novo, com um passo extra de strip (mais frágil que separá-los na
  origem). Cache incremental confuso (a unidade de cache mistura dois sabores).
- **Eixos.** Memória: neutra. Complexidade: média (strip no empacotamento). Leis/R8: **frágil**
  (depende do strip; se falhar, vaza). Reprodutibilidade: alta. IO: médio.

**Alternativas de NOME.** `.tkhl` (`tkh`-link, sufixo `l`, espelha `.tkbl`) × `.tkhi`
(`tkh`-internal, deixa "interno" explícito) × `.tkh.link`/`.linktkh` (composto). Prós de
`.tkhl`: simetria direta com `.tkbl` (o par intermediário todo usa sufixo `l`), curto, ordena
junto do `.tkh` no diretório. Contra: `.tkhl` × `.tkh` diferem por uma letra (erro de digitação
fácil). `.tkhi` deixa "interno" mais óbvio mas quebra a simetria com `.tkbl`.

#### Recomendação (original) → **Opção 3B (memória-primeiro)** — ⛔ RETRATADA; ver decisão do dono

3B era 3A com um fast-path: zero IO no build limpo fundido (o caso quente), materializando
`.tkhl` só onde é inevitável (LINK global não-fundido + cache incremental). **3C perde** —
reintroduz o risco R8 (é 1C com seções) e troca a separação estrutural por um strip frágil no
empacotamento. O coordenador havia recomendado 3B por ganhar tempo de build limpo isolando a
rota condicional em `serialize_link_header`.

#### ✅ RATIFICADA: **Opção 3A (`.tkhl` DIRETO EM DISCO)**, nome `.tkhl` — dono 2026-08-24

> **RETRATAÇÃO FORMAL (counter-argue do dono).** A **recomendação 3B (memória-primeiro) fica
> RETRATADA** — o dono escolheu **3A** (`.tkhl` direto em disco). **Razão do dono:** a tese da
> campanha inteira é **despejo-em-disco para bound de memória**; manter o `.tkhl` em memória
> (3B) **contradiz o objetivo** de tirar coisa da memória para o disco. Além disso, **3A reusa o
> serializer de copy-grow já escrito** (o mesmo framing `.tkb`/`RM-C13`), sem inventar a rota
> condicional memória-vs-disco de 3B. Ou seja: 3B otimizava um tempo de build às custas de
> reintroduzir estado em memória — exatamente o que o Eixo C existe para eliminar. **3A é a
> decisão.** O contra-argumento do coordenador (IO no build fundido) é subordinado à tese de
> memória: o custo de IO é o *preço aceito* do despejo, não um defeito a evitar.

**Consequência (política decidida):** o `.tkhl` é **sempre materializado em disco**, par do
`.tkbl`, reusando o serializer determinístico (`RM-C13`, frame R6). Não há rota memória-primeiro
nem `serialize_link_header` condicional — há um único caminho: emitir `.tkbl`+`.tkhl` em disco
por unidade, derrubar a região. O cache incremental (`RM-C14`) lê o `.tkhl` do disco direto.
**Nome: `.tkhl`** (simetria com `.tkbl`; `.tkhi` era o fallback, não escolhido).

### [DECISÃO DO DONO] 4 — RM-C10: COMO determinizar o gensym — ✅ RATIFICADA: Opção 4A (dono 2026-08-24)

> **✅ RATIFICADA (dono 2026-08-24) — Opção 4A** (contador global monotônico substitui
> `buf.len`). Bate com a recomendação do coordenador para o `RM-C10` AGORA. **Razão do dono:**
> "se é naming, já existe maquinaria, não tem nada para inventar" — 4A é a mudança MÍNIMA que
> **reusa a maquinaria de gensym existente** (só troca `buf.len` por um contador), byte-
> preservante; B/C inventavam esquemas novos. **4B fica registrada como migração futura, só se a
> paralelização (§6) exigir.** Registro no `DECISION_LOG.md` D67.

**Problema (R4).** Hoje nomes temporários derivam de `buf.len` (ex.: `$"_oln{buf.len}"`). Sob
emissão por-namespace o buffer é por-unidade → o MESMO corpo gera nome DIFERENTE conforme a
ordem/fronteira das unidades → `teko.c`/`.o` divergem → fixpoint quebra. `RM-C10` é
pré-requisito de C11. COMO gerar o nome de forma estável?

Cenário-exemplo comum aos três: a 2ª variável temporária de `parse()` no namespace `teko::str`.

#### Opção 4A — Contador global monotônico (substitui `buf.len`)

Um contador de processo, resetado no início do codegen, incrementado a cada draw.

```teko
/**
 * next_temp — próximo ordinal temporário do contador global do run de codegen. Substitui a
 * derivação por buf.len. Reproduz a MESMA sequência de nomes de hoje SE a ordem de emissão for
 * determinística (é — namespaces + itens iteram em ordem fixa).
 * @return o próximo ordinal, monotônico no run
 * @since 0.3.1
 */
fn next_temp(): i64
```

Nome gerado no exemplo: **`_t41`** (o 41º temporário do run inteiro).

- **Prós.** Simplíssimo (um inteiro). Reproduz byte-a-byte a sequência de HOJE (requisito de
  `RM-C10`: `teko.c` byte-idêntico após o rename). C emitido curto e legível.
- **Contras.** **Depende da ORDEM GLOBAL de emissão.** Reordenar unidades (ou paralelizar, §6)
  muda todos os números a partir do ponto de divergência → `.o` diverge. É determinístico HOJE
  (ordem fixa), mas é a opção **mais frágil sob paralelização futura**. O número não diz de qual
  fn/namespace veio (debug mais difícil).
- **Eixos.** Reprodutibilidade por-unidade: **frágil** (acopla à ordem global). Colisão: nenhuma
  (monotônico). Legibilidade do C: **ótima** (`_t41`). Estabilidade sob paralela: **ruim**.

#### Opção 4B — Tupla `(namespace, índice-de-fn, seq)`

O nome deriva de coordenadas ESTÁVEIS: o namespace, o índice da fn dentro do namespace, e um
contador local resetado por-fn.

```teko
/**
 * next_temp_scoped — ordinal temporário derivado de (namespace, fn_idx, seq-local-da-fn). O
 * nome NÃO depende da ordem global de emissão — só das coordenadas da própria fn — logo é
 * estável sob reordenação de unidades E sob paralelização (§6).
 * @param ns namespace da fn corrente
 * @param fn_idx índice da fn no namespace
 * @return o ordinal local, monotônico DENTRO da fn
 * @since 0.3.1
 */
fn next_temp_scoped(ns: str, fn_idx: u32): i64
```

Nome gerado no exemplo: **`_str_parse_1`** (ou mangled: `_t_str__parse__1`) — namespace `str`,
fn `parse`, 2º temporário (seq 1).

- **Prós.** **Estável por-unidade por construção** — o nome de um temporário depende só da SUA
  fn, não do que veio antes globalmente. Sobrevive a reordenação de unidades E a paralelização
  (§6) sem divergir. C legível e auto-descritivo (dá pra ver de onde veio). Casa com o
  incremental (a unidade recompila igual, mesmos nomes).
- **Contras.** Nomes mais longos (mais bytes no `.o`/`.c`). Precisa carregar (ns, fn_idx) até o
  ponto de gensym (um pouco de plumbing). **NÃO reproduz a sequência de HOJE byte-a-byte** — os
  nomes MUDAM (de `_oln{n}` para `_str_parse_1`) → o `teko.c` muda → é um reseed real, não um
  rename byte-preservante. (Contraste com `RM-C10` que hoje pede byte-idêntico — 4B trocaria isso
  por "byte-idêntico dali em diante".)
- **Eixos.** Reprodutibilidade por-unidade: **máxima** (coordenadas locais). Colisão: nenhuma
  (a tripla é única). Legibilidade do C: boa (verboso mas informativo). Estabilidade sob
  paralela: **ótima**.

#### Opção 4C — Nome por hash-de-conteúdo do nó da AST

O nome deriva de um hash do subárvore/nó (tipo + posição estrutural + conteúdo).

```teko
/**
 * temp_of_node — nome temporário derivado de um hash estável do nó da AST (estrutura+tipo). NÃO
 * depende de contador nem de ordem — o MESMO nó gera o MESMO nome sempre, em qualquer ordem.
 * @param node o nó que precisa do temporário
 * @return o nome (prefixo + hash truncado)
 * @since 0.3.1
 */
fn temp_of_node(node: @TExpr()): str
```

Nome gerado no exemplo: **`_t_9f3a1c`** (hash truncado do nó).

- **Prós.** Independente de ordem E de coordenadas — content-addressed puro. Estável sob
  qualquer reordenação/paralela. Dois nós idênticos... (ver contra).
- **Contras.** **Colisão real.** Dois temporários com o MESMO hash (nós estruturalmente iguais
  na mesma fn — ex.: dois `x+1` idênticos) colidem → precisa de desambiguação (sufixo de
  ocorrência), o que **reintroduz um contador** (volta a 4A/4B). Hash truncado agrava a colisão;
  hash cheio incha o nome. C ilegível (`_t_9f3a1c` não diz nada). Custo de hashing por nó. **Não
  reproduz a sequência de hoje** (reseed). O hash tem que ser 100% determinístico (sem endereço
  de ponteiro, sem ordem de `map`) — mais uma superfície de não-determinismo a auditar.
- **Eixos.** Reprodutibilidade por-unidade: alta SE o hash for puro, mas a **colisão** força um
  desambiguador ordinal (regride). Colisão: **problema real**. Legibilidade do C: **ruim**.
  Estabilidade sob paralela: ótima (se resolver colisão de forma determinística).

#### Recomendação → **✅ RATIFICADA: Opção 4A para `RM-C10`; 4B como migração futura** — dono 2026-08-24

`RM-C10` (o crumb `0065`) exige explicitamente **`teko.c` byte-idêntico após o rename** (rename
mecânico, validado byte-a-byte) — SÓ **4A** satisfaz isso (reproduz a sequência de `buf.len` de
hoje). É o passo mínimo, seguro e byte-preservante que desbloqueia C11 sem um reseed de
conteúdo. **4C está fora** — a colisão força um desambiguador ordinal (regredindo a um
contador) e produz C ilegível, com uma nova superfície de não-determinismo (o hash) a auditar; o
custo não paga. **4B é tecnicamente superior a 4A** para o futuro (estável sob reordenação E
paralelização, §6) e é a evolução natural — MAS trocar os nomes AGORA quebra o byte-idêntico que
`RM-C10` pede, virando um reseed de conteúdo prematuro. Portanto: **4A no `RM-C10`** (byte-
preservante, desbloqueia o Eixo C), e registrar **4B como a migração** a fazer JUNTO com a
paralelização (§6), onde a estabilidade por-coordenada deixa de ser luxo e vira requisito (o
contador global de 4A não sobrevive a workers). Até lá, 4A + ordem de emissão determinística
(garantida por `RM-C11`/`C12`) mantém `gen2==gen3`. **Marcar `RM-C10` como bloqueador do
per-unit native** segue valendo (já nos crumbs: `RM-C10` "BLOCKS C11"). **O dono ratificou 4A em
2026-08-24 (DECISION_LOG D67), razão: "se é naming, já existe maquinaria, não tem nada para
inventar" — 4A reusa o gensym existente; 4B fica como migração futura, só se a paralelização
exigir.**

---

## 14. Fixtures de regressão (o que o implementer adiciona)

O Eixo C é **exercitado pelo self-build/fixpoint** (o compilador É o monólito multi-namespace)
— por isso C10–C13/C15–C17 têm `Fixtures: none` e o fixpoint É a regressão. As fixtures
isoladas ficam onde o self-build NÃO exercita o caminho:

| Fixture | Entrada → asserção | Exit esperado |
|---|---|---|
| `tkbl_flavor_premono_roundtrip` | serializar um namespace genérico pré-mono (`.tkbl`) e deserializar → recupera os genéricos crus + pedidos de mono, sem instanciar (sabor poli preservado) | `0` |
| `tkb_flavor_rejects_wrong` | deserializar um `.tkb` publicado de sabor mono onde se espera poli (byte de versão de sabor errado) → erro claro, não crash | `0` |
| `tkhl_excludes_from_tkh` | montar `.tkhl` (`exp`+`pub`) e emitir `.tkh` do mesmo namespace → `pub` NÃO aparece no `.tkh`; só `exp` | `0` |
| `visibility_symbol_binding` | um namespace com `exp`/`pub`/privado → no `.o`, `exp`+`pub` são GLOBAL (`!local`), privado é LOCAL/`static` (nem entra na tabela) | `0` |
| `linker_scc_mutual_recursion` | dois namespaces mutuamente recursivos → o linker os agrupa num único SCC/unidade de link e processa juntos; a ordem topológica dos SCCs é canônica (determinística) | `0` |
| `package_tkb_is_poly` | package com genérico exportado → o `.tkb`+`.tkh` publicados preservam o genérico (pré-mono/portátil) | `0` |
| `tool_tkb_is_poly` | tool com genérico → o `.tkb` distribuído é pré-mono (IGUAL package); a mono NÃO roda no build (adiada pro install) | `0` |
| `lib_tkh_is_mono` | lib com `exp` genérico instanciado → o `.tkh` publicado é monomórfico (assinaturas concretas, para FFI) | `0` |
| `binary_is_mono_at_build` | binary com genérico alcançável do `main` → monomorfizado no build (fecho do `main`); nenhum genérico cru no `.o` | `0` |
| `incremental_off_self_build` (já em RM-C14) | `self_build=true` → cache bypassed, saída byte-idêntica a um build limpo C12/C13 | `0` |

As fixtures de `RM-C14` (`incremental_hit_reuses`, `incremental_dep_invalidates`,
`incremental_off_self_build`) já estão no crumb `0069` e permanecem. As de sabor de
`.tkbl`/`.tkb`, de header, de política de mono por destino (tool/binary) e do SCC do linker são
**novas deste desenho** (acompanham o flip pré-mono do §4, os dois pares de artefato do §3, os
4 destinos do §5 e o linker sequenciado do §2bis), e só ganham gate quando `RM-C15`/`C17`
saírem do bloqueio (§11).

---

## 15. Pontos de ritual (onde o gate cheio tem que passar)

- **RM-C10..C14:** cada um é `[fixpoint]` (exceto C14 `[dry]`) — `gen2==gen3` byte-idêntico +
  `ulimit -v 6815744`. São `fixpoint-rebuild` (o core consome; sem teaching seed).
- **RM-C15:** `[fixpoint]` — ainda na rota C (`gen2.c==gen3.c`), MAIS as 4 pernas native
  emitindo `.o` por-unidade que o `ld`/`objfile_ar` juntam num binário executável nas 4
  pernas. **RITUAL:** a muleta C fica; C15 só prova que o `.o` roda.
- **RM-C16:** **`[RITUAL]`** — a escada native completa + reseed genuíno (migração do seed
  native). É AQUI que o gate cheio migra: `gen2.o==gen3.o` byte-idêntico nas 4 pernas, `teko.c`+
  `cc` REMOVIDOS, as 2 pernas C viram native, seed do bootstrap = objeto native. Requer
  `S16-SWEEP` fechado. **Este é o ritual maior do terminal native.**
- **RM-C17:** `[fixpoint]` — `gen2==gen3` do objeto native + o `.tkh` publicado reproduzido
  byte-a-byte.

---

## 16. Resumo do amarre

Este doc **não re-delibera** o Eixo C — ele preenche o terminal native com o que faltava
concretizar: (a) o **arbusto → `.tkbl`+`.tkhl` por-namespace + linker interno** (§1–2), com o
par intermediário (`.tkbl`+`.tkhl`, transitório/pré-mono) distinto do par publicado
(`.tkb`+`.tkh`, final); (b) o **linker interno memory-bounded** — ordem topológica de
dependência + poda ao carregar + agrupamento de **SCCs** (recursão mútua ⇒ grafo com ciclos)
(§2bis); (c) os **dois sabores de header/objeto** (`.tkhl`/`.tkbl` de link × `.tkh`/`.tkb`
publicados, §3); (d) **onde a monomorfização mora** (na ponta, `.tkbl` sempre pré-mono; mono
adiada — build p/ lib/binary, install p/ tool, consumidor p/ package, §4); (e) a **saída por
destino** — **QUATRO destinos, DUAS políticas de mono**: package (poli, linka em outro binário)
· tool (poli, mono no install, standalone) · lib (mono no build, FFI) · binary (mono no build)
(§5); (f) **paralelização** (§6) e **incremental** (§7, o `.tkbl` É a unidade de cache) como os
ganhos que o desenho habilita; (g) a **migração do fixpoint** para objeto native (§8); (h) a
**restrição SQLite/no-C** como lei (§10). Tudo gated no marco de memória e nas pernas NAT (§11)
— é **design-ahead**. Os 4 pontos do dono (§13) têm recomendação law-first e **nenhum HALT**:
são confirmações, não impasses.
