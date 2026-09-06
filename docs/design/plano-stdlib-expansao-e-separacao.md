---
section: design
created: 2026-08-13
updated: 2026-08-13
status: DELIBERAÇÃO-PREP — o MECANISMO de separação está SELADO (self-`.tkh` do monólito, §11 Sequência,
        commit d45c72f0); o que resta em aberto é CONTEÚDO (o catálogo de expansão e a triagem dos ~1620
        `pub`), apresentado abaixo como forks COM EXEMPLOS, nunca perguntas nuas. NÃO é plano de crumbs,
        NÃO houve ratificação do conteúdo, NENHUM build/`teko test` foi executado.
source: §11 Sequência (self-`.tkh`, SELADO), §12 (decisões fechadas), §10 (journal/threads/Intent),
        §9.2b (constraints), §13 (value-struct), docs/memory/0.3.1-item13-monomorph-leak-investigation.md
scope: mapear a stdlib atual, RESTATE a separação para o self-`.tkh` do monólito (substitui o antigo fork
       P2 de pacote-separado), manter P1 (`exp`/`pub`) REFORÇADO, propor a EXPANSÃO como catálogo com forks,
       e RESTATE o corte de RSS do item 13 como redução-de-entrada por link-contra-o-monólito. READ-ONLY
       sobre código de produto.
---

# Stdlib — expansão (catálogo) + separação por self-`.tkh` do monólito, e o corte de RSS do item 13

> **Papel desta fase** (dono, §11 Sequência): (1) **expandir** a stdlib — o **catálogo** de novos itens (um
> passo focado próprio: *aumentar os recursos do dev*); (2) a **separação** que permite o **self-link com o
> programa final** — **SELADA** como o **self-`.tkh` do monólito**: Teko continua monolítico, **não se extrai
> pacote**; o compilador **exporta o `.tkh` de si mesmo** e o programa final **linka contra o monólito** por
> esse header. Só o que é **`exp`** entra no self-`.tkh` → **reforça** `exp` como a superfície visível ao dev;
> (3) o bônus de memória — linkar contra o monólito pré-compilado **em vez de re-arrastar a stdlib como fonte**
> **corta parte do RSS super-linear do item 13**. Esta fase **povoa** o `exp`/`pub` que a §11 depois
> **formaliza** e **enforce**; a §12 (libc-direct/`#if`/`#os`) opera **sobre** a superfície resultante.
>
> **O que mudou (SELADO — não reabrir).** O antigo **fork P2 (granularidade — um pacote stdlib separado)** foi
> **OVERRIDDEN** pelo ruling do dono em `mudancas-superficie-0.3.1.md` §11 (commit `d45c72f0`): **nada de
> pacote extraído**. A separação é o **self-`.tkh`** (§2). **P1** (`exp` = ABI-no-`.tkh`) **fica e é
> reforçado**. **P3** (corte do item 13) é **restated** como *link-contra-o-monólito = redução de entrada*.
>
> Deliberação-prep do **conteúdo**: cada candidato de expansão e cada critério de triagem abre **forks + 3+
> alternativas com exemplo** (a recomendada primeiro), para o dono martelar parte a parte. **Zero pergunta
> nua.** O mecanismo (§2) NÃO é fork — está selado.

---

## 0. TL;DR — o que está selado, o que resta decidir

| # | Item | Estado | Resolução |
|---|------|--------|-----------|
| P1 | Fronteira de separação (`exp`/`pub`/private) | **SELADO + REFORÇADO** | `exp` = ABI no self-`.tkh` (visível ao programa); `pub` = interno inter-namespace do monólito; private = ficheiro. É a legislação Visibility (`ast.tks:517`). O self-`.tkh` **reforça** `exp`: só ele atravessa. |
| ~~P2~~ | ~~Granularidade de link (pacote separado)~~ | **OVERRIDDEN (§2)** | Substituído pelo **self-`.tkh` do monólito**: sem pacote, o programa linka contra o monólito pré-compilado via o header que o compilador exporta de si. |
| P3 | Corte do item 13 | **RESTATED (§2.3)** | Não é fix do `monomorphize`; é **redução de entrada**: o programa **linka** contra o monólito em vez de re-clonar/re-monomorfizar a stdlib como fonte. Complementar ao AL5, não substituto. |
| C (catálogo) | O que entra na expansão AGORA | **FORK — §3** | Monomórfico-buildável-hoje + tipos forward-compatible do §10 (Intent/threads); coleções genéricas atrás do #254. Cada candidato com 3+ opções e exemplo. |
| T (triagem) | Regra de ouro p/ triar ~1620 `pub` | **FORK — §6.1** | Recomendada: `exp` **sse** um programa de usuário chama direto; na dúvida `pub`. 3+ critérios alternativos com exemplo. |
| §11/§12 | Interação | **SELADO (§4)** | Esta fase **escreve** os marcadores (forward-compatible); §11 **liga** o enforcement; §12 usa a superfície `exp` já fixa. |

**Bloqueios honestos (design-ahead).** O *enforcement* de `exp`/`pub` é a §11 (penúltima): hoje `pub` e `exp`
são **ambos** cross-namespace (`check_modules.tks:143` — só `Private` erra cross-ns), então a separação é
**escrita agora, ativada na §11**. As **coleções genéricas** dependem do **#254** (métodos em tipo genérico,
OPEN). A **emissão** do self-`.tkh` e do objeto pré-compilado por-unidade depende do backend nativo (a via
`.tkl`/`.tkb` já existe, `project.tks:136`; o self-`.tkh` é o header dessa mesma emissão apontado para o
próprio monólito). Tudo que **não** depende disso está desenhado abaixo.

---

## 1. Mapa do estado atual — a stdlib de hoje

### 1.1 Como namespace e link funcionam hoje (o ponto de partida da separação)

- **Um projeto só.** `teko.tkp` declara `name = "teko"`, `source = "src"`. **A stdlib mora DENTRO da árvore de
  fontes do compilador** — `teko::list`, `teko::fmt`, `teko::math` são compilados JUNTO com `teko::checker`,
  `teko::lir`, etc. **Não existe artefato de stdlib separado** hoje — e, pelo ruling selado, **nunca haverá um
  pacote extraído**: o monólito **é** o artefato; o que muda é ele **exportar um header de si** (§2). O
  namespace de cada símbolo é o **caminho do ficheiro relativo a `src/`** (`src/io/io.tks` → `teko::io`;
  `src/encoding/json/json.tks` → `teko::encoding::json`).
- **`use` é só alias** (`language-guide.md:223`): nunca muda visibilidade, nunca declara dependência, sem
  wildcard, sem re-export. `check_modules.tks` faz o enforcement de namespace (ref bare cross-ns = erro; ref
  qualificada a private = erro). **É `use` que o self-`.tkh` vai satisfazer**: `use teko::math` num programa de
  usuário resolve contra o header exportado, não contra a fonte da stdlib (§2.1).
- **Visibilidade (legislação, `ast.tks:517`):** `pub type Visibility = enum { Private; Pub; Exp }`. `Private`
  (default, sem keyword) = só o próprio namespace; `Pub` = **através dos namespaces do projeto, NÃO no header
  do artefato**; `Exp` = **exportado no `.tkh` (a ABI pública real do artefato)**, `pub` por definição. Ordem
  crescente de alcance: `Private < Pub < Exp`. **Esta é exatamente a fronteira que o self-`.tkh` materializa** —
  o header carrega os `Exp`, esconde os `Pub`/`Private`.
- **Empacotamento tipado já existe** (`project.tks:136`, "C7.10 — DEP LOADING"): um dep é um **`.tkl`** (ZIP)
  contendo um **`.tkb`** (itens tipados serializados), procurado em `packages/<dep>-*.tkl` e *prepended* antes
  do type-check. É o **mesmo veículo de serialização** que o self-`.tkh` reusa — o header do monólito é a
  projeção `exp`-only dessa serialização, apontada para o próprio compilador (§2.1).
- **`teko::list` e `teko::str` são BUILTINS injetados** — `teko::list::{empty,push,…}` é free-fn builtin
  (`type_list_builtin`); a superfície de string é resolvida por último-segmento em `scope.tks:1054`
  (`teko::str::concat`/`teko::string::concat`). Não têm ficheiro-classe. `str`/`byte`/`error` são **tipos
  predefinidos injetados** (`core.tks`). **Builtins não entram no self-`.tkh`** (são seam de codegen, já no
  binário) — só stdlib `.tks` é triada.

### 1.2 Inventário — namespaces da stdlib (user-facing) e seus ficheiros

| Namespace | Ficheiros | ~Linhas | `exp` hoje | Genérico? | Natureza |
|---|---|---:|---:|---|---|
| `teko::list` | builtin (`src/list/list.tks` = shim `grow`) | 19 | 0 | **sim** (builtin `<T>`) | free-fn builtin sobre a intrínseca de alocação |
| `teko::collections` | `collections.tks`, `list.tks` (`List<T>`), `map.tks` (`Map<V>`) | 313 | 0 | **sim** | classes-coleção — **bloqueado #254** |
| `teko::runtime` | `teko_rt.tks` (+ `.c`/`.h` MANTIDOS) | 789 | 47 | não | seam de runtime; C twin congelado exceto rt |
| `teko::assert` | `assert.tks` (+ `.c`/`.h` seed MANTIDO) | 375 | 24 | não | seed de assert |
| `teko::journal` | `journal.tks`, `summary.tks` | 1995 | 0 | parcial | journaling §10.4 (sink = service singleton) |
| `teko::env` | `env.tks` | 49 | 0 | não | FFI host (args/vars) |
| `teko::fmt` | `fmt.tks` | 878 | 0 | não | formatter DT0 |
| `teko::text` | `text.tks` | 106 | 0 | não | utilidades de `str`/UTF-8 |
| `teko::iter` | `iter.tks`, `int_iter/int_terminals/byte_iter/str_iter` | 837 | 0 | parcial | protocolo de iteração (quer `Iter<T>`) |
| `teko::io` | `io.tks` + 3 | 938 | 0 | parcial | FFI host + streams; Reader/Writer quer round-3 |
| `teko::fs` | `fs.tks` | 34 | 0 | não | FFI host de ficheiros |
| `teko::math` | `math.tks`, `checked.tks` | 2523 | 0 | parcial (`min/max/clamp`) | M0/M1 |
| `teko::sort` | 2 ficheiros | 345 | 0 | parcial | ordenação |
| `teko::time` | `time.tks` | 684 | 0 | não | data/hora (drop-128, campos ≤64b) |
| `teko::process` | `process.tks` | 600 | 0 | não | FFI host fork/exec |
| `teko::regex` | `regex.tks` | 585 | 0 | não | regex sobre `str`/`[]byte` |
| `teko::crypto` | 3 ficheiros | 699 | 0 | não | hash/CSPRNG sobre `[]byte` |
| `teko::compress` | 5 ficheiros | 1514 | 0 | não | DEFLATE/gzip/zlib sobre `[]byte` |
| `teko::encoding::{base64,csv,json,url}` | 4 ficheiros | — | 0 | não | codecs sobre `str`/`[]byte` |
| `teko::numeric::{bigint,dec}` | 2 ficheiros | — | 0 | não | aritmética de struct concreta |
| `teko::casting` | `casting.tks` | 182 | 0 | não | conversões |
| `teko::names` | `names.tks` | 237 | 0 | não | utilidades de identificador |
| `teko::coverage` | `coverage.tks` | 1138 | 19 | não | cobertura (ferramenta) |
| `teko::test` | `test.tks` | 109 | 0 | não | scaffolding de teste |
| `teko::str`, `teko::char` | **builtin** (`scope.tks:1054`) | — | n/a | não | superfície de string injetada (não vai ao self-`.tkh`) |

**Total user-facing ≈ 15 k linhas** de `.tks` (sem contar o compilador: `lexer/parser/checker/lir/codegen/
backend/emit/build/lsp` — ~100 ficheiros — que **NÃO** são superfície do programa do usuário e **não** entram
no self-`.tkh`).

### 1.3 Observação-chave que motiva a fase

Hoje **quase tudo é `pub`** (medido: **~1619 `pub` vs ~202 `exp` vs ~55 `intern`**), e os únicos `exp` estão em
`runtime`/`assert`/`coverage`. Isso porque, sendo **um projeto só**, `pub` **basta** (cross-namespace intra-
projeto — `check_modules.tks:143` trata `Pub` e `Exp` como iguais). **A distinção `exp` vs `pub` só passa a
morder quando o monólito exporta um `.tkh`** — e é exatamente o self-`.tkh` desta fase. **Logo: a fase é, na
prática, decidir para CADA item da stdlib se ele é `exp` (entra no self-`.tkh`, visível ao programa) ou `pub`
(interno do monólito, invisível ao programa).**

---

## 2. A SEPARAÇÃO — SELADA como self-`.tkh` do monólito (substitui o antigo P2)

**Problema.** Hoje, compilar um programa do usuário arrastaria a stdlib inteira **como fonte**, e o
`monomorphize` clonaria/carimbaria cada corpo dela na região raiz do programa (item 13, §3). **Objetivo:** que
o programa final consuma a **superfície `exp`** do monólito **sem re-compilar a stdlib como fonte** e **sem
re-clonar/re-monomorfizar** o que não usa.

**Resolução SELADA (dono, §11, commit `d45c72f0`).** **Teko é monolítico e continua monolítico — NÃO se extrai
pacote nenhum.** O mecanismo é dois artefatos que o compilador emite **de si próprio**:
1. **o self-`.tkh`** — o **header do monólito**: a ABI serializada de **tudo que é `exp`** em **todo**
   namespace da stdlib (a projeção `exp`-only da mesma serialização `.tkb` que `project.tks:136` já usa);
2. **o monólito pré-compilado** — o objeto/arquivo nativo do próprio `teko`, contra o qual o programa **linka**.

O programa do usuário **type-checa contra o self-`.tkh`** (as assinaturas `exp`, não a fonte) e **linka contra
o monólito** (os símbolos já compilados). O `pub`/`private` **não estão no header** — o programa nem os vê, nem
os arrasta.

### 2.1 O mecanismo, com exemplo concreto (um programa `use`-ando a stdlib)

```teko
// ---- app.tks — programa do usuário (fora da árvore do compilador) ----
use teko::math                     // alias; NÃO é dependência de fonte — resolve contra o self-`.tkh`

fn main(): i32 {
    var r = teko::math::sqrt(2.0)  // `sqrt` é `exp` → está no self-`.tkh`, símbolo no monólito
    var g = teko::math::gcd(12, 8) // idem; a IMPLEMENTAÇÃO de gcd (helpers `pub`) NÃO vem para app
    return 0
}
```

**Pipeline (o que o compilador faz):**
1. **Ao construir o `teko`**, o backend emite, além do binário, o **self-`.tkh`**: para cada item marcado
   `exp` em `src/**/*.tks`, serializa a **assinatura tipada** (`teko::math::sqrt(f64): f64`,
   `teko::math::gcd(i64, i64): i64`, …). Corpos `pub`/`private` **não** entram — só a ABI.
2. **Ao compilar `app.tks`**, o front-end **prepende o self-`.tkh`** (a mesma rota do `load_dep_package`,
   `project.tks:136`, mas o "dep" é o próprio monólito e a projeção é `exp`-only). `teko::math::sqrt` resolve
   **por assinatura**, sem tocar `src/math/math.tks`.
3. **No link**, `app.o` resolve o símbolo `teko::math::sqrt` **contra o monólito pré-compilado** — o corpo
   **não é re-emitido** em `app`.
4. **O `monomorphize` de `app`** varre **só** o código de `app` + os **templates genéricos `exp` que `app`
   instancia** (nenhum aqui: `sqrt`/`gcd` são não-genéricos). A stdlib **não-genérica não entra no `items`**.

**Por que "self"-`.tkh`.** O header é do **próprio monólito** — o compilador **faz dogfooding** da sua própria
emissão de `.tkh`: a mesma máquina que emitiria o header de qualquer artefato é apontada para `teko` mesmo. Não
há um segundo projeto, um segundo `.tkp`, nem um loader de pacote novo — só a projeção `exp` do que já existe.

### 2.2 P1 — a FRONTEIRA (o que é `exp` vs `pub`) — REFORÇADO pelo self-`.tkh`

O self-`.tkh` **dá dentes** ao par `exp`/`pub` que hoje é decorativo. A fronteira já é lei (`ast.tks:517`); o
header é quem a **materializa**:

| Opção | `exp` (entra no self-`.tkh`) | `pub` (fica no monólito) | private | Veredito |
|---|---|---|---|---|
| **A — ABI-no-self-`.tkh` (SELADA)** | superfície user-facing; assinatura no header, símbolo linkado | helper inter-namespace do monólito, **fora** do header — invisível ao programa | ficheiro | **É a legislação já escrita**, agora com veículo (o self-`.tkh`). Zero construto novo. |
| B — tudo `exp` (sem fronteira) | todo `pub` vira `exp` | — | ficheiro | rejeitar: infla o header e re-expõe tudo; não corta memória além do que o link já corta; ABI gigante |
| C — 3º eixo `pkg` (visível-no-monólito-não-no-header) | novo marcador | `pub` | private | rejeitar: **cria exceção** (o dono: "já temos muitas") — `Pub` **já é** exatamente "visível no monólito, fora do header" |

**Recomendação (alinhada ao selado): A.** A stdlib expõe uma **API `exp` curada** no self-`.tkh`; todo o resto
é `pub` (helper compartilhado entre módulos — ex. um buffer de bytes que `crypto` e `encoding` partilham) ou
private (ficheiro). **Nenhum construto novo** — só **reescrever `pub`→`exp`** onde é API pública e **manter
`pub`** onde é interno. É o ato central da fase. O eixo C é **redundante**: `Pub` **é** o "3º eixo" que a opção
C inventaria (`ast.tks:517` já diz "across the project's namespaces, **not in the binary header**").

### 2.3 P3 — POR QUE o self-`.tkh` corta o RSS do item 13 (RESTATED: link, não loader)

O item 13 (`monomorph.tks`) tem dois amplificadores estruturais (doc do item 13, §3a/§3b). O antigo P3 falava
em "loader por-namespace + arquivo pré-compilado"; **restate**: o corte vem de o programa **linkar contra o
monólito pré-compilado em vez de re-arrastar a stdlib como fonte**. Ataca a **ENTRADA** de ambos os
amplificadores (sem tocar o pass — o fix do pass é AL5/achatamento, ortogonal):

- **§3a — PHASE-1 clona cada corpo não-genérico na raiz do programa.** Hoje isso incluiria **todo** corpo `pub`
  da stdlib arrastado como fonte. Com o self-`.tkh`, o não-genérico `exp` é **símbolo linkado** (não
  re-clonado) e o `pub`/`private` **nem entra no `items` do programa** (não está no header). → o volume clonado
  da PHASE-1 do programa cai para "só o código do usuário + templates genéricos que ele instancia".
- **§3b — `stamp_all_instance_methods` é O(instâncias × métodos × `items`), SEM teto.** O custo é linear no
  tamanho de `items`. **Encolher `items`** (o programa só vê os `exp` que usa + o que ele mesmo define; nada de
  `pub`/`private` da stdlib) **encolhe multiplicativamente** esse hot-spot. → menos `items`, menos scan.
- **Genéricos ainda monomorfizam no uso** (não dá pra pré-compilar um `List<T>` sem `T`), **mas** o self-`.tkh`
  só carrega os **templates `exp`**; uma superfície `exp` **enxuta** limita **quais** templates são alcançáveis,
  e o link limita **quanto** código não-genérico entra.

**Honestidade (complementa AL5, não substitui).** Isto **não conserta** o `monomorphize` — o teto/região-por-
fase é AL5, outra onda. É **redução de entrada**: encolhe o `n` de um pass super-linear, mudando a compilação
do programa de "re-arrastar a stdlib como fonte" para "linkar contra o monólito". O ganho é real e mensurável
(RSS-vs-superfície-`exp`-usada), mas **complementar** ao AL5. *(Medição segura pertence a VM isolada — NUNCA
`teko test`, NUNCA este container; ver §6a do doc do item 13.)*

### 2.4 A tensão central da separação (assimetria genérico vs não-genérico)

**Genérico não se pré-compila.** Um `exp` genérico (`List<T>`, `Iter<T>`, `svc<T>`, `chan<T>`) é **template**: o
self-`.tkh` carrega a **assinatura + o corpo tipado do template**, mas a monomorfização acontece **no programa
do usuário**, por `T` concreto. Então o corte é **assimétrico**:
- **Não-genérico `exp`** (math sobre `f64`, crypto/compress/encoding sobre `[]byte`, io/fs/env/process FFI):
  corte **total** — vira símbolo linkado do monólito.
- **Genérico `exp`** (coleções, iter, DI, canais): corte **parcial** — sai o `pub`/private e o não-usado, mas o
  template instanciado ainda monomorfiza no programa.

Isso **guia a política de expansão (§3)**: o **não-genérico** é o alvo prioritário da separação (maior ganho), e
o **genérico** deve ter superfície `exp` deliberadamente **mínima** (expor a classe, esconder os helpers de
rebuild em `pub` — como `collections.tks` já faz com as free-fns de replace-at/drop-at).

### 2.5 O único fork de MECANISMO que resta (o resto está selado): granularidade do dead-strip

O **mecanismo** (self-`.tkh` + link contra o monólito) está selado. Resta **uma** decisão de engenharia:
**como o link exclui o não-genérico `exp` que o programa NÃO chama** (o header lista tudo que é `exp`; um
programa que só usa `teko::math::sqrt` não deve pagar o `.text` de `teko::regex`). Três alternativas, com
exemplo:

| Opção | Forma | Exemplo | Custo/veredito |
|---|---|---|---|
| **A — dead-strip do linker (recomendada)** | monólito emitido com `-ffunction-sections`/`-fdata-sections`; `app` linka com `--gc-sections`. O linker mantém só os símbolos `exp` **alcançáveis** a partir de `main`. | `app` chama `teko::math::sqrt` → o linker puxa `sqrt` (+ o `pub` `gcd_impl` que `sqrt` referencia, por alcance de símbolo), **descarta** `teko::regex::*` inteiro. | zero-desenho-novo; já é como o backend nativo linka libc. **Recomendada.** |
| B — arquivo por-símbolo (`.a` de membros por-função) | o monólito vira um `ar` com um membro-objeto por símbolo `exp`; o linker puxa só os membros referenciados | `app` referencia `sqrt.o`; `regex_match.o` nunca é extraído do `.a` | granularidade idêntica à A na prática, mas exige o backend particionar a emissão por-símbolo — construto novo de emissão |
| C — self-`.tkh` particionado por-namespace + N objetos | o header e o objeto são fatiados por namespace; `use teko::X` puxa o objeto de `X` inteiro | `use teko::math` puxa `math.o` (todo o namespace), mesmo que `app` só chame `sqrt` | granularidade **grossa** (namespace, não símbolo); reintroduz sabor de "pacote" que o selado rejeitou — **evitar** |

**Recomendação: A (dead-strip do linker).** É o mecanismo mais próximo do que o backend nativo **já faz** com a
libc, dá granularidade **por-símbolo** sem particionar a emissão, e **não** reintroduz a noção de pacote (o
monólito é UM objeto; o linker é que poda). B é equivalente em efeito mas pede emissão por-símbolo nova
(reportar como possível otimização futura, não fazer agora). C recai no sabor de pacote-por-namespace que o
dono selou **fora**. **Esta escolha é ortogonal ao self-`.tkh`** — o header é o mesmo nas três; muda só como o
objeto é podado no link.

---

## 3. A EXPANSÃO — o CATÁLOGO (passo focado próprio) com forks example-bearing

O dono separou explicitamente (§11): o cerne é **aumentar os recursos do dev** — um **catálogo** de novos itens,
**passo próprio**, distinto de "como separa" (§2). Regra de sequência: **entra agora** o que é
**monomórfico-buildável-hoje** ou **forward-compatible** (os tipos do §10 que já têm grafia `exp`/`pub`
definida); **fica atrás do #254** o que precisa de método em tipo genérico. Cada bloco é um fork com **3+
opções e exemplo**.

### 3.1 Fork E1 — completar `teko::collections` (BLOQUEADO #254)

O catálogo **quer** as coleções, mas elas travam no #254 (métodos em tipo genérico) e no link nativo de método-
irmão genérico (report do #163 na própria `collections.tks`). **Fork: quanto antecipar agora.**

| Opção | O que se faz nesta fase | Exemplo | Veredito |
|---|---|---|---|
| **A — só a fronteira `exp`/`pub` (recomendada)** | desenhar a **assinatura `exp`** de cada classe + `pub` nos internos, implementar após #254 | `exp class Set<T>` no header; buckets `pub` no corpo (stub honesto até #254) | forward-compatible; `List<T>`/`Map<V>` já servem de precedente |
| B — antecipar UMA coleção completa | escolher a de maior demanda (`Set<T>`) e implementá-la já, contornando #254 à mão | `Set<T>` sobre `Map<T, unit>` com métodos livres em vez de método-em-genérico | contorna o bloqueio, mas cria dívida de estilo (free-fn onde deveria ser método); risco de link-fail (#163) |
| C — não catalogar coleções nesta fase | adiar E1 inteiro para a onda pós-#254 | — | perde a chance de fixar namespace/`exp` cedo; retrabalho na §11 |

Candidatos e a **fronteira `exp`/`pub`** proposta (opção A):

| Candidato | Forma | Superfície `exp`/`pub` | Entra? |
|---|---|---|---|
| `Set<T>` | class sobre `Map<T,unit>`/hash | `exp class Set<T>`; helpers de bucket `pub` | após #254 |
| `Deque<T>` | class ring-buffer | `exp class Deque<T>`; o rebuild `pub` | após #254 |
| `BTreeMap<K,V>` | class ordenada (precisa `IOrd`/`__lt`, §9.4) | `exp class`; nós `pub` | após #254 **e** interface-operador |
| `LinkedList<T>` | class encadeada | `exp class`; nó `pub` | após #254 |

**Recomendação: A** — desenhar **só a fronteira** agora (classe `exp`, internos `pub`), implementar quando #254
fechar. `List<T>`/`Map<V>` já existem e são o precedente. Não antecipar B (dívida de estilo + link-fail #163).

### 3.2 Fork E2 — tipos de concorrência do §10 (FORWARD-COMPATIBLE, entram cedo)

O §10.3/§10.4 **já definem** a grafia `exp`/`pub` de `Intent`/`Intent<T>` (backing `_x` private, `exp get`,
`pub set`, `pub static fn new` — verbatim em `mudancas-superficie-0.3.1.md:780`). Esta fase **cataloga-os como
itens de stdlib** e **fixa o namespace**. **Fork: onde os canais/journal moram.**

| Opção | Namespace dos tipos de concorrência | Exemplo | Veredito |
|---|---|---|---|
| **A — `teko::threads` (recomendada, o §10.5 reserva o nome)** | Intent/Ctx/Rx/Tx/chan/IChannelKind em `teko::threads`; journal em `teko::journal` | `svc<Rx<i32>>("nums")` resolve `teko::threads::Rx` | alinha ao §10.5 ("namespace: `teko::threads`"); zero surpresa |
| B — `teko::async` para Intent, `teko::threads` para canais | separa o eixo `await`/Intent do eixo `spawn`/chan | `use teko::async` p/ Intent; `use teko::threads` p/ chan | 2 namespaces onde o §10 trata como um modelo só; churn de `use` |
| C — tudo em `teko::sync` | um guarda-chuva único | `use teko::sync` p/ tudo | contradiz o §10.5 (nome reservado é `teko::threads`) |

**Recomendação: A.** Fixar **`teko::threads`** agora e escrever os `exp`/`pub` **exatamente como o §10 grafou**
(forward-compatible, sem enforcement até §11). É pura população de superfície — **entra**. Catálogo:

| Candidato | Namespace | Superfície | Depende de |
|---|---|---|---|
| `Intent` / `Intent<T>` | `teko::threads` | `exp type` + `exp get`/`pub set` (§10.3, verbatim) | item 14 (value-struct mut), §9 (props) |
| `Ctx`, `Rx<T>`, `Tx<T>` | `teko::threads` | handles: `exp` métodos `send`/`pop`/`add`/`done`/`close` | DI por chave (§7), #254 |
| `IChannelKind<T>`, `OsChan<T>`, `MemChan<T>` | `teko::threads` | `exp interface` + built-ins; sink concreto `pub` | conformidade estática de interface |
| `Roll`, `Record`, `IJournalKind`, `FileJournal` | `teko::journal` | `exp` a interface/enum/Record; `FileJournal` internals `pub` | §10.4 (já grafado) |

### 3.3 Fork E3 — consolidação de string (`teko::str` builtin → módulo?)

Hoje `teko::str::*` é **builtin injetado** (`scope.tks:1054`) e `teko::text` é um módulo separado (106 linhas).
**Fork: como o catálogo trata a superfície de string.**

| Opção | Forma | Exemplo | Veredito |
|---|---|---|---|
| **A — manter o split, fixar `exp` de `teko::text` (recomendada)** | `str` = builtins mínimos (seam de codegen, **fora** do self-`.tkh`); `teko::text` = utilidades ricas, triadas `exp`/`pub` | `teko::str::concat` (builtin) coexiste com `exp teko::text::str_from_utf8` | menor churn; o builtin é seam, não stdlib pura |
| B — fundir tudo em `teko::str` módulo | mover `text` para `teko::str`, aposentar o builtin | `teko::str::str_from_utf8` substitui `teko::text::…` | alto churn no corpus (`teko::str::concat` está em toda parte); risco de reseed |
| C — novo `teko::string` módulo, deprecar `teko::str` | criar `teko::string` rico, `teko::str` vira alias legado | `teko::string::concat` (já legislado, `scope.tks:1058`) como API `exp` | duplica a superfície; confunde qual é a canônica |

**Recomendação: A.** Manter o split, **decidir a fronteira `exp` de `teko::text`** (o que é API vs helper). Não
mexer no builtin `str` (é seam de codegen; **não entra no self-`.tkh`**). B/C mexem no que o corpus usa em toda
parte — risco de reseed sem ganho de catálogo.

### 3.4 Fork E4 — itens monomórficos prontos (do drain fase-3, agora com fronteira `exp`)

Estes são **monomórfico-buildável-hoje** (o drain doc os lista sobre `f64`/`str`/`[]byte`) — o **alvo
prioritário** do corte do self-`.tkh` (§2.4: não-genérico = corte total). **Fork: quanto o catálogo faz agora.**

| Opção | O que a fase faz | Exemplo | Veredito |
|---|---|---|---|
| **A — admitir + fixar namespace/`exp`, implementar por issue própria (recomendada)** | catalogar cada item, escrever sua fronteira `exp`/`pub`, deixar a implementação para a issue do drain | `teko::math::real` fixado: `exp fn sin/cos/…`, `extern` `pub` | herda a ordenação do `drain-fase3-stdlib-order.md`; não retrabalha na §11 |
| B — implementar `teko::math::real` já (libm FFI, `-lm` já linkado) | antecipar o item mais barato (só FFI infalível) | `exp fn teko::math::real::sqrt(f64): f64` sobre `extern fn sqrt` | tentador, mas é escopo de issue própria; a fase é **superfície**, não implementação |
| C — não catalogar E4 | deixar tudo para o drain | — | perde a fixação de `exp` cedo; a §11 re-triaria do zero |

Candidatos (opção A): `teko::math::real` (libm FFI sobre `f64`); `teko::log` (facade `exp` + writers `pub`);
`teko::config` (leitura `exp` + parser `pub`); completar `teko::encoding` (TOML/XML/YAML/binários) e
`teko::numeric` (complex/rational/stats).

**Recomendação: A.** **Admitir como candidatos** e **fixar namespace + `exp`**, mas cada um é **issue própria**
(o drain doc já os ordena) — a fase **não os implementa**. **Reportar ao integrador** (não abrir issues) que E4
herda a ordenação do `drain-fase3-stdlib-order.md`.

---

## 4. Interação com §11 (`exp`/`pub`) e §12

### 4.1 Como esta fase POVOA a §11

A §11 (penúltima) **liga o enforcement** de `exp`/`pub`: hoje `check_modules.tks:143` trata `Pub` e `Exp` como
**iguais** (só `Private` erra cross-ns). Quando a §11 entrar, `pub` deixa de atravessar o self-`.tkh` e só `exp`
chega ao programa do usuário. **Esta fase escreve os marcadores que a §11 vai enforce**:

1. Para **cada item `pub` da stdlib** (~1619), esta fase decide: **continua `pub`** (interno do monólito) **ou
   vira `exp`** (entra no self-`.tkh`). É o trabalho de maior volume.
2. A grafia é **forward-compatible** (o §10.3 já a usa na Intent; `mudancas-superficie-0.3.1.md:776`): hoje tudo
   lê como visível; a §11 **auto-corrige** sem refactor. Logo esta fase pode ser escrita **sem esperar** a §11.
3. A **regra de ouro de triagem** é um fork de conteúdo — ver **§6.1** (3+ critérios com exemplo).

### 4.2 Como a §12 usa a superfície resultante

A §12 (última — libc-direct/`#if`/`#os`/macro) **opera sobre a §11 já formada**: decide **o que** `#os`/libc-
direct pode substituir, e isso só é seguro se a **fronteira `exp` já está fixa** (uma substituição `#os` de um
corpo `pub` interno não vaza pelo self-`.tkh`; de um `exp` sim — mudaria a ABI do header). Logo a ordem do dono
(`exp`/`pub` penúltima, libc-direct última) é **necessária**: a §12 precisa saber o que é ABI (`exp`) antes de
mexer nos corpos.

### 4.3 O corte de memória como pré-condição, não consequência

O §11 Sequência coloca o bônus de memória explícito: a separação **"já reduz parte dos problemas de memória"**.
Registrar que o corte (§2.3) **não** depende do enforcement da §11: o **self-`.tkh` + o link contra o monólito**
cortam `items`/PHASE-1 **assim que existirem**, mesmo antes de `pub` deixar de atravessar. A §11 só **tranca** a
fronteira que a separação já usa (impede um `pub` de vazar para o header por engano).

---

## 5. Riscos e tensões de lei (com resolução recomendada)

| Risco / tensão | Lei em jogo | Resolução recomendada |
|---|---|---|
| Genérico `exp` não pré-compila → corte assimétrico | item 13 / AL5 | **Aceitar**: separação corta não-genérico total, genérico parcial; complementa AL5 (§2.3–2.4) |
| Reseed: o self-`.tkh` usa feature ausente do seed | bootstrap seed | **Sequenciar**: a serialização `.tkl`/`.tkb` já existe (`project.tks:136`); o self-`.tkh` é a projeção `exp`-only dela — **nenhum construto novo de linguagem**, só reescrever `pub`→`exp`. Seguro no seed atual |
| Sweep silencioso de `.tkt`/`.tkr` ao restringir visibilidade | owner-profile (sweep obrigatório) | Reescrita `pub`→`exp` **alarga** (mais visível), não quebra; a restrição real (`pub` sumir do header) é a §11 — **esta fase só escreve**, não enforce, então não quebra hoje |
| `exp` a mais infla o self-`.tkh` e a memória | §2.2 | Regra de ouro §6.1: na dúvida `pub`; `exp` só o que o usuário chama direto |
| Dead-strip não poda o não-usado → header enxuto mas binário gordo | §2.5 | Opção A (`--gc-sections`): o linker poda por-símbolo, como já faz com a libc. Reportar B (emissão por-símbolo) como otimização futura, não fazer agora |
| `teko::str`/`list` builtins parecem stdlib | core.tks (injetado/reservado) | **Não** entram no self-`.tkh` (seam de codegen, já no binário) — só stdlib `.tks` é triada (§1.1, §3.3) |
| Coleções genéricas parecem "prontas" mas travam no link nativo | #254 + report do #163 (`collections.tks`) | **Não antecipar** E1 (opção A); a própria `collections.tks` documenta o link-fail de método-irmão genérico |
| Reabrir o fork P2 (pacote separado) | ruling selado (§11, d45c72f0) | **HALT-guard**: P2 está OVERRIDDEN; qualquer proposta de "extrair pacote stdlib" contradiz o selado — **não reabrir** nesta fase |

**Nenhuma tensão genuína de lei exige HALT.** O mecanismo está **selado** (self-`.tkh`); a fronteira `exp`/`pub`
já é lei (`ast.tks:517`); a serialização já existe; a grafia é forward-compatible. O que resta é **decisão de
conteúdo** (o catálogo §3 e a triagem §6.1), apresentada abaixo como forks com exemplo — **sem perguntas nuas**.

---

## 6. FORKS DE CONTEÚDO — propostas com exemplo (o que o dono martela; zero pergunta nua)

O **mecanismo** (§2) está selado — nada a decidir ali. O que resta é **conteúdo**, e cada item abaixo traz **a
recomendação primeiro + 3+ alternativas com exemplo**. Não há pergunta aberta.

### 6.1 Fork T — a regra de ouro para triar os ~1619 `pub`

O critério que decide quanto da stdlib some do programa (entra ou não no self-`.tkh`). Três critérios
alternativos, com o mesmo item de exemplo (`teko::compress` — `inflate` público, `huffman_build` helper):

| Opção | Critério | Aplicado ao exemplo | Veredito |
|---|---|---|---|
| **A — "chamada direta pelo usuário" (recomendada)** | `exp` **sse** um programa de usuário o chama direto; helper inter-módulo = `pub`; detalhe de ficheiro = private. **Na dúvida, `pub`.** | `exp fn inflate(src: []byte): []byte`; `pub fn huffman_build(...)`; tabelas static = private | custo de esquecer um `exp` = erro honesto na §11; custo de um `exp` a mais = header inflado. Assimetria favorece `pub` |
| B — "tudo que já é `pub` vira `exp`" | migração mecânica, sem triagem | `inflate` **e** `huffman_build` viram `exp` | rejeitar: header gigante, re-expõe internos, não corta memória (§2.2-B) |
| C — "só o que tem teste/doc user-facing vira `exp`" | `exp` guiado por evidência de uso (fixture/exemplo em `docs`) | `inflate` tem exemplo em `docs` → `exp`; `huffman_build` não → `pub` | mais preciso, mas depende de cobertura de docs existir para cada item; muitos itens legítimos sem doc ficariam `pub` por engano |

**Recomendação: A.** É o critério que casa com a assimetria selada (§2.4) e com o custo-de-erro (esquecer `exp`
= erro visível; `exp` a mais = memória). B re-expõe tudo (anti-objetivo). C é bom como **desempate** dentro de A
(se um item tem doc user-facing, é forte sinal de `exp`), mas não como regra primária.

### 6.2 Fork O — ordem de triagem dos namespaces

Por onde começar a reescrita `pub`→`exp` (maximizar ganho de memória cedo). Três ordens, com exemplo:

| Opção | Ordem | Exemplo do 1º lote | Veredito |
|---|---|---|---|
| **A — não-genérico primeiro (recomendada)** | math/crypto/compress/encoding/io/fs/env/process/time/regex → depois genéricos (collections/iter/threads) | 1º lote: `teko::math`, `teko::crypto` — corte **total** por serem não-genéricos (§2.4) | maior ganho de memória cedo; os genéricos (menor ganho, mais risco #254) por último |
| B — por tamanho de linha | do maior (`teko::math` 2523) ao menor | 1º: `teko::math`; 2º: `teko::journal` 1995 | correlaciona mal com ganho (linha ≠ memória); journal é parcial-genérico |
| C — por frequência de uso no corpus | do mais chamado ao menos | 1º: o namespace mais referenciado em `docs`/testes | precisa medir uso primeiro; útil como desempate, não como eixo |

**Recomendação: A.** Não-genérico primeiro maximiza o corte total do self-`.tkh` (§2.4) e adia o risco #254 dos
genéricos. C é bom desempate **dentro** de A.

### 6.3 Fork D — granularidade do dead-strip (mecanismo residual, §2.5)

Já resolvido em §2.5 (recomendação: **A — dead-strip do linker** com `--gc-sections`). Repetido aqui só para o
dono confirmar por parte: A (linker poda por-símbolo, como a libc) vs B (arquivo por-símbolo, emissão nova) vs C
(fatiar por-namespace, sabor de pacote que o selado rejeitou). **Recomendada: A.**

### 6.4 Escopo da fase (confirmação, não pergunta)

Esta fase **escreve os marcadores** (`pub`→`exp` triados por §6.1, na ordem §6.2) **e** desenha o self-`.tkh`
(§2) — mas **não liga** enforcement (isso é §11). O corte de memória vem do **self-`.tkh` + link contra o
monólito** (§2.3), não do enforcement (§4.3). O catálogo de expansão (§3) **fixa namespace + `exp`** mas
implementa por issue própria (E4 herda o drain; E1 espera #254; E2 é forward-compatible e entra).

---

## 7. Confirmação de segurança

`teko test` **NÃO** foi executado em forma alguma durante esta revisão — nem `teko test .`, nem subconjunto,
nem guardado. Nenhum build foi rodado, nenhum seed construído, nenhum código de produto editado. O trabalho foi
**leitura estática + raciocínio** sobre `teko.tkp`, `src/core.tks`, `src/parser/ast.tks` (`Visibility` em
`:517`), `src/checker/{check_modules.tks:143, collect.tks, scope.tks:1054}`, `src/build/project.tks:136`, os
cabeçalhos dos módulos da stdlib, `docs/design/mudancas-superficie-0.3.1.md` (§10–§13, o self-`.tkh` selado no
§11, commit `d45c72f0`), `docs/design/drain-fase3-stdlib-order.md`, `docs/canonical/product/language-guide.md` e
`docs/memory/0.3.1-item13-monomorph-leak-investigation.md` (§3a/§3b, o ângulo de memória). **Uma única edição de
documento — este ficheiro.** Nenhuma edição de código de produto, nenhum reseed, nenhum build.
