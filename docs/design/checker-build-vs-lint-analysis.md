# Estudo de arquitetura — CHECKER: build-essencial vs. análise RELOCADA-E-REPROPOSITADA (lint / linker-DCE / PGO)

**Escopo.** Análise profunda, fundamentada e arquitetada de `src/checker/**` e do driver que o
sequencia (`src/build/project.tks` → `frontend_body`; `src/checker/typer.tks` →
`type_program_with_deps` / `type_program`). Fonte da verdade: `origin/remodel/backend-build` HEAD
`26c7981` (fetch feito nesta sessão).

**Enquadramento (reframe do dono, 2026-07-14, verbatim intent).** "Os casos de unused: vamos
relaxar. Quando o linker próprio chegar, ele mesmo conseguirá fazer essa limpeza. Acredito que muito
do checker será reutilizado, não apenas como linter, mas na otimização do linker e no profiler
(PGO)." Isto transforma o corte de "build vs lint" em **build-essencial (E) vs. RELOCADO-E-
REPROPOSITADO (R)**: os passes advisory não são *descartados*, são *realocados* e passam a alimentar
TRÊS consumidores distintos da MESMA análise de alcançabilidade — (i) `teko lint` (feedback ao dev,
sob demanda), (ii) o linker próprio (DCE silencioso → binário menor), (iii) o profiler PGO (grafo de
chamadas / atribuição de hotness). Este documento é ANÁLISE — não aciona interruptor, não altera o
repo.

> **E** = BUILD-ESSENTIAL (o código emitido ou a solidez depende do produto). **R** = RELOCADO-E-
> REPROPOSITADO (advisory hoje; o binário é byte-idêntico com ou sem; o produto vira INPUT de
> lint/linker-DCE/PGO). "Grey" = zona cinzenta explicitada.

---

## 0. Correções, confirmações e o ruling datado

**0.1 — NÃO existe passe de "definite-assignment / use-before-init". É estrutural.**
`src/checker/initanalysis.tks:5-13` (LAW-FIRST): Teko não tem binding não inicializado
(`parser::Binding.value` obrigatório; valor tipado no env PRÉ-binding em `typer.tks type_binding`),
logo use-before-init é *impossível por construção* — não um passe. `TEKO_MASTER_PLAN.md:145`
confirma ("use-before-init is STRUCTURAL"). **Consequência:** `initanalysis.tks` entrega
EXCLUSIVAMENTE dois diagnósticos — unused-LOCAL e unused-PRIVATE — ambos puramente consultivos. Não
há aqui garantia de solidez a proteger; o hotspot medido é 100% análise advisory.

**0.2 — O hotspot medido e a parte E do checker são disjuntos (alinhamento afortunado).**
A inferência de tipos propriamente dita (`type_block`/unify/`type_join`) é E mas barata (<5% C-path,
~11% native-path; `type_join`=0.00s). O caro (init-analysis, ~76% do front-end no native-path) é
exatamente o material R. Relocar o caro NÃO custa solidez.

**0.3 — Dois passes já estão DESLIGADOS (não-cabeados):**
- `src/checker/revalidate.tks` — re-prova do TAST; `validate_texpr`/`validate_program` só em
  COMENTÁRIOS (`typer.tks:1307`, `check.tks:19`). **Zero call sites.**
- `src/checker/borrow.tks` — sumário borrow whole-program (`fn_borrow_summary`, `borrow.tks:913`).
  **Zero consumidores** fora do próprio arquivo.
Ambos já são "trabalho preservado, não deletado" — o modelo exato de repropositar (Seção 5.5).

**0.4 — RULING DATADO (registrar como supersedência atribuída, não mudança silenciosa).**
> **Owner 2026-07-14 — RELAXAR os casos de unused.** `unused-local`, hoje um **ERROR** de build
> (`initanalysis.tks:193-194`), é **demovido**: deixa de falhar o build. Isto **SUPERSEDE** a regra
> deliberada de `TEKO_MASTER_PLAN.md:186/189` ("unused local = error"). A limpeza de código
> não-usado é propriamente trabalho do **linker próprio (DCE)**; a análise de alcançabilidade é
> REPROPOSITADA para lint + linker-DCE + PGO. A supersedência é DATADA e ATRIBUÍDA (owner
> 2026-07-14), não assumida.

**0.5 — Precedente já em árvore: o linker JÁ reusa a análise do checker.**
`src/build/reachability.tks` (namespace `teko::build`) declara explicitamente que "reuses the
CONSERVATIVE call-graph shape already proven in teko::checker::initanalysis (the unused-private-fn
pass)" (`reachability.tks:6-9`). Ele generaliza `occurs_expr` de "o nome ocorre?" para "colete todo
callee alcançável" (`collect_expr`, `reachability.tks:42`), roda um fecho worklist a partir das
raízes (virtual-main + fns `exp`) (`entry_roots`/`close_reachable`/`grow_reachable_pass`,
`reachability.tks:193/215/226`) e ALIMENTA O LINKER: decide quais `[extern.libs]` linkar
(`append_reachable_link_flags`, `project.tks:366`). **Ou seja, a visão do dono já tem uma
âncora funcionando: a mesma máquina de alcançabilidade do checker já é INPUT de otimização de
link.** Symbol-level DCE e o grafo-de-chamadas do PGO são a generalização natural do que
`reachability.tks` já faz para libs externas.

---

## 1. Inventário de responsabilidades do checker

Driver real: `type_program_with_deps` (`typer.tks:4271`); `type_program` (`typer.tks:4367`) é o
mesmo pipeline sem seed de deps. `frontend_body` (`project.tks:240`) chama SÓ
`checker::type_program_with_deps` (`project.tks:294`). Sequência canônica:

| # | Responsabilidade | Entrada | Produto | Consumidor a jusante | Sítio |
|---|---|---|---|---|---|
| 1 | **fold_traits** (fold de derivações) | `parser::Program` | Program c/ corpos derivados | todos + codegen | `typer.tks:4276` |
| 2 | **canon_class_bases** (qualifica bases) | Program | Program canônico | collect, typing, codegen (mangle) | `typer.tks:4277`, `collect.tks:332` |
| 3 | **seed_from_dep** (.tkb dep-seed merge) | TProgram da dep (desserializado do `.tkb`) | `Collected` semeado | collect_with_seed | `typer.tks:4280` |
| 4 | **collect / collect_with_seed** (pass 1) | Program + seed | `TypeTable` + `Env` (assinaturas) | TODOS + codegen (`type_table_of`) | `typer.tks:4285`, `collect.tks:182` |
| 5 | **check_modules** (visibilidade/privacidade) | Program + TypeTable | `error?` | — | `typer.tks:4288`, `check_modules.tks:162` |
| 6 | **check_di** (valida overlays `#inject`) | Program + TypeTable | `error?` | — | `typer.tks:4289`, `di.tks:222` |
| 7 | **build_di_registry** (grafo DI) | Program + TypeTable | `DiRegistry` | typing (`di_lower_use`) + codegen | `typer.tks:4290`, `di.tks:57` |
| 8 | **instantiate_types** (estampa genéricas concretas) | Program + TypeTable | TypeTable estendida | typing + codegen (layout) | `typer.tks:4292` |
| 9 | **register_instance_methods** | Env + TypeTable | Env c/ métodos de instância | typing de chamadas | `typer.tks:4293`, `collect.tks:253` |
| 10 | **typing per-item** (inferência, unificação, resolução de valor, conformance, exhaustividade) | itens + Env + TypeTable | **TAST** (`.type` em cada nó) | codegen, VM, `.tkb`, monomorph | `typer.tks:4314-4340` |
| 10a | ↳ check_returns | corpo + tipo retorno | `error?` | — (solidez) | `typer.tks:3375` |
| 10b | ↳ check_trailing_value | idem | `error?` | — | `typer.tks:3548` |
| 10c | ↳ check_labels | corpo + escopo labels | `error?` | — | `typer.tks:3562+`, `4343` |
| 10d | ↳ exhaustive | arms + subject | `bool` | — (codegen assume) | `match.tks:454`, `typer.tks:2919` |
| 10e | ↳ conformance nominal | tipos + TypeTable | Type/erro | codegen (dispatch), monomorph | `resolve.tks:816/1138` |
| 10f | ↳ check_ref_storability (spine gate #331) | TFunction | `error?` (rejeita ref-escape/aliased-free) | — (solidez MEM) | `typer.tks:3829/3845`, `spine.tks:1406` |
| 11 | **DI lowering** (`di_lower_use` em `type_var`) | uso de `#inject` | TExpr → chamada de materializer | codegen (`di_materializer_name`) | `typer.tks:46`, `di.tks:270`, codegen `6532/6551` |
| 12 | **analyze_program** (unused-local + unused-private) | TProgram inteiro | `error?` (+ `eprintln`) | **NENHUM** (só veredito) | `typer.tks:4361/4451`, `initanalysis.tks:271` |
| 13 | **monomorphize** | TProgram + TypeTable | TProgram só concreto | codegen, VM, tests | `typer.tks:4363/4456`, `monomorph.tks:1147` |
| 14 | **fn_escaping_vars** (escape — frame region) | TFunction | conjunto de escapes | **codegen C** (`_tkfr`) | `escape.tks:320`, `codegen.tks:6645` |

Infra compartilhada (`scope.tks`, `type.tks`, `resolve.tks`, `tast.tks`, `synth.tks`, `match.tks`)
segue a classe do maior consumidor (essencial). A maquinaria puramente-R é `occurs_expr`/
`occurs_stmt`/`occurs_block` (`initanalysis.tks:34/139/161`) — e é exatamente o hotspot (`occurs_expr`
~22% self C-path) — E É A MESMA que `reachability.tks::collect_expr` generaliza para o linker.

---

## 2. Classificação build-essencial (E) vs. relocado-repropositado (R)

### 2.1 — Núcleo inequivocamente E (o binário depende do produto)

| Passe | Classe | Justificativa (file:line) |
|---|---|---|
| fold_traits (1) | **E** | reescreve corpos emitidos. `typer.tks:4276` |
| canon_class_bases (2) | **E** | qualifica nomes → mangle C. `collect.tks:332` |
| seed_from_dep (3) | **E** | sem seed, chamadas a deps não resolvem. `typer.tks:4280` |
| collect (4) | **E** | `TypeTable`+`Env` = substrato de tudo + codegen (`collect.tks:296`). |
| instantiate_types (8) | **E** | layout das genéricas concretas. `typer.tks:4292` |
| register_instance_methods (9) | **E** | dispatch de método. `collect.tks:253` |
| typing/inferência (10) | **E** | TAST c/ `.type` que o codegen lê (`tast.tks:1-4`). |
| check_returns (10a) | **E (solidez)** | caminho sem retorno = valor lixo; codegen só emite trap sob condição de tipo (`codegen.tks:6687`). |
| check_trailing_value (10b) | **E** | retorno implícito tipado + wrap de slot. |
| exhaustive (10d) | **E (solidez, grey leve)** | ver §2.3. |
| conformance (10e) | **E** | correção de vtable/dispatch + gates `<T: I>`. |
| check_ref_storability/spine (10f) | **E (solidez MEM)** | fecha UAF por aliased-free (`typer.tks:3885-3891`) e ref-escape (`spine.tks:1438`). |
| DI (6/7/11) | **E** | `di_lower_use` emite chamada de materializer (`codegen.tks:6532/6551`). |
| monomorphize (13) | **E** | codegen/VM só veem concreto (`monomorph.tks:1-11`). |
| fn_escaping_vars/escape (14) | **E (solidez MEM, backend C)** | frame region `_tkfr` (`codegen.tks:6640-6648`); só roda no backend C (grep `src/lir` = vazio). |

### 2.2 — Inequivocamente R (binário byte-idêntico; vira input de otimização)

| Passe | Classe | Justificativa (file:line) |
|---|---|---|
| **unused-PRIVATE fn** | **R** | já advisory: `warn_unused_fn`→`eprintln`, nunca falha (`initanalysis.tks:250-251`). Passe quadrático `used_in_program` O(privates×programa) (`initanalysis.tks:236`). Produto (conjunto de nomes chamados) é EXATAMENTE o que o linker-DCE e o PGO consomem. |
| **unused-LOCAL** | **R (por ruling 0.4)** | sem consumidor de dado — `analyze_program` retorna só `error?` (`typer.tks:4361`); binário byte-idêntico. Era ERROR por POLÍTICA (MASTER_PLAN:186), agora RELAXADO (owner 2026-07-14). Quadrático `occurs_block`-por-local (`initanalysis.tks:192`). |

### 2.3 — Zona cinzenta: exhaustive (10d)
Falha o typing num `match` sem `_` nem cobertura total (`typer.tks:2919`). Se um `match` incompleto
chegasse ao codegen, não haveria arm default → cai-através (indefinido). Como o build está
estruturado, **exhaustive é E-solidez** — barreira que impede o codegen ver match não coberto (só
emite `unreachable` sob condição de tipo de retorno, `codegen.tks:6687`). Migrar exigiria trap de
arm-default no backend. Custo de mantê-lo ~0 (não é hotspot). **Recomendação: manter E.**

### 2.4 — Zona cinzenta: check_modules (5) — privacidade
`check_modules` (`check_modules.tks:162`) impõe qualificação e privacidade cross-ns; retorna `error?`
sem produto. O codegen mangla pelo nome canônico já resolvido em collect/typing → **o binário é
byte-idêntico com ou sem a checagem de PRIVACIDADE (regra B.9): é encapsulamento/contrato, não
solidez → R-elegível em princípio.** Caveats: (a) a regra "bare ref a outra ns" pode ser
pré-requisito de RESOLUÇÃO (um bare que resolve à ns errada mudaria o binário) — **INFERÊNCIA,
requer auditoria em resolve.tks**; (b) privacidade é CONTRATO que usuários esperam. **Recomendação:
tratar como R-PARCIAL** — a privacidade (b) é candidata a lint; qualificação/alias (a/c) ficam E até
auditoria; não é hotspot, payoff de perf ~nulo → prioridade baixa (ver §7 Q4).

### 2.5 — Resumo do corte
- **E-núcleo:** 1,2,3,4,8,9,10(+10a/b/c),10e,10f,6,7,11,13,14.
- **E-solidez c/ nota:** 10d exhaustive.
- **R puro:** unused-private, unused-local (relaxado 0.4).
- **R-parcial/grey:** check_modules privacidade-(b).
- **Fora do build (dormentes, residência R):** revalidate.tks, borrow.tks.

---

## 3. Uma análise, TRÊS consumidores (o coração do reframe)

A análise de alcançabilidade (`occurs_*`/`used_in_program`, hoje presa dentro de `analyze_program`
retornando `error?`) é a MESMA informação que três consumidores distintos querem. O ponto central: o
`error?` de veredito é a forma MENOS reusável possível; o produto NATURAL é um **grafo de referência
whole-program** (símbolo → é-alcançado-de-uma-raiz? + arestas de chamada), que os três consumidores
projetam de formas diferentes.

### 3.1 — Os três consumidores da mesma alcançabilidade

| Consumidor | O que faz | Quando | Feedback ao dev? | O que projeta do grafo |
|---|---|---|---|---|
| **teko lint** | "unused local `x`", "unused private `helper`" | sob demanda (`teko lint`) | SIM (warning) | símbolos declarados MENOS os alcançados = conjunto morto → renderiza como diagnóstico com caret |
| **linker-DCE** | strip de símbolos não alcançados → binário menor | link time (silencioso) | NÃO (otimização) | fecho de alcançabilidade a partir das raízes = conjunto VIVO → mantém só ele; descarta o resto |
| **PGO profiler** | atribui hotness / seed do grafo de chamadas p/ profile-guided opt | build instrumentado + rebuild | NÃO (otimização) | arestas de chamada (grafo estático) = esqueleto que os contadores de perfil populam |

**Insight que fortalece a visão (torná-lo explícito):** *relaxar o gate de build NÃO tira o sinal do
dev.* O mesmo fato "`x` nunca é lido" migra de um **build error** para (i) um **lint** que ele roda
quando quer E (ii) uma **DCE silenciosa** que o linker faz sozinho. Lint e DCE são DOIS consumidores
da MESMA análise: um fala com o humano, o outro encolhe o binário. O dev não perde nada — ganha
escolha de quando ver o sinal, e o binário fica menor sem intervenção.

### 3.2 — A estrutura de dados a expor (contrato para os três)
Hoje `analyze_program` retorna `error?` (`initanalysis.tks:271`) — descarta o grafo que constrói
implicitamente. `reachability.tks` já constrói o fecho VIVO (`close_reachable` devolve `[]str` de
nomes alcançados, `reachability.tks:215`) mas só para libs externas. A generalização que serve os
três:

```
// esboço de contrato (NÃO é proposta de implementação — só a forma do dado)
// (INFERÊNCIA arquitetural, rotulada)
type UseGraph = struct {
    roots:      []str            // entry points: virtual-main + fns `exp`/`pub` de biblioteca + #test
    reached:    []str            // fecho worklist a partir de roots (o conjunto VIVO)
    declared:   []str            // todo símbolo top-level declarado
    call_edges: []CallEdge       // (caller, callee-last-segment) — o esqueleto p/ PGO
    local_uses: []LocalUse       // (fn, local, lido?) — o material per-função do unused-local
}
```

- **lint** deriva `dead = declared \ reached` (unused-private) e filtra `local_uses` por `lido==false`
  (unused-local) → warnings.
- **linker-DCE** consome `reached` diretamente (já é o que `reachability.tks` faz para libs; a
  generalização é símbolo-a-símbolo em vez de lib-a-lib).
- **PGO** consome `call_edges` como grafo estático; os contadores de instrumentação anexam hotness
  por aresta/nó.

Isto realiza literalmente "muito do checker será reutilizado... na otimização do linker e no PGO": a
alcançabilidade sai de `analyze_program` (onde vira `error?` e morre) e passa a um `UseGraph`
consumido por três clientes. `reachability.tks` é a prova-de-conceito já em produção.

### 3.3 — Conservadorismo unidirecional é COMPATÍVEL com os três
`occurs_*` sobre-aproxima "usado" (nome-ocorrência), o que `initanalysis.tks:19-24` e
`reachability.tks:10-15` documentam como falso-NEGATIVO seguro (perde um unused, nunca inventa um).
Para os três consumidores isso é a direção SEGURA: lint que às vezes não avisa (nunca avisa errado);
DCE que às vezes mantém um símbolo morto (nunca strip-a um vivo → sem link error); PGO que às vezes
tem uma aresta a mais (grafo super-conjunto → hotness conservadora). O mesmo `margin` serve os três —
é por isso que UMA análise basta.

---

## 4. O que o `teko build` ainda garante (e o que para de garantir)

Cenário do ruling: unused relaxado; alcançabilidade realocada para lint/linker-DCE/PGO.

**Continua garantindo (inalterado):** solidez de tipos (TAST, `tast.tks`); solidez de memória
(spine 10f `typer.tks:3885`; escape 14 no backend C); exaustividade (10d mantido E); todo caminho
retorna + labels (10a/c); DI resolvido (6/7/11); monomorfização (13). **O binário é tão sólido
quanto hoje** — nenhum bug de runtime novo alcança o executável.

**Para de garantir (custo honesto):**
- **unused-local não falha mais o build** (ruling 0.4). Um `let x = caro()` nunca lido compila. Custo:
  bug de lógica silencioso escapa o BUILD — mas é RECUPERADO por dois canais (§3): `teko lint` (dev
  vê quando roda) e linker-DCE (o `x` morto é stripado do binário). O sinal migra, não some.
- **unused-private** deixa o build (já era só warning) → aparece em `teko lint`; o símbolo morto é
  removido pelo linker-DCE.
- **(se privacidade-(b) realocada — decisão SEPARADA, §7 Q4)** um símbolo privado cross-ns
  compilaria; recuperado só via lint (não há canal linker). Por isso privacidade fica FORA do ruling
  de unused — é contrato, não higiene.

**A cruz:** nenhuma perda é de solidez de RUNTIME; a de unused é higiene RECUPERADA por lint+DCE (o
dev escolhe a latência). A troca: latência-de-detecção (build→lint) + otimização automática (DCE) em
troca de velocidade-de-build (remove ~76% do front-end no native-path).

---

## 5. Levers de custo, incremental, e os dormentes

### 5.1 — A dimensão quadrática: relocar vs. de-quadratizar (por passe)
O hotspot é 100% R e 100% quadrático. Duas alavancas ortogonais:
- **unused-private** `used_in_program` O(P·N) (`initanalysis.tks:236`): (A) relocar p/ lint+DCE (build
  não paga); (B) de-quadratizar — UMA passada coletando nomes-chamados num set, cada private é lookup
  O(1) → O(N+P) (é literalmente o que `close_reachable` já faz, `reachability.tks:215`); (C) escopar
  per-arquivo (§5.2). **Com o ruling, (A) é o caminho — e o linker-DCE já quer o set.**
- **unused-local** `occurs_block`-por-local O(locals·corpo) (`initanalysis.tks:192`): (A) relocar; (B)
  UMA passada por fn coletando nomes-lidos num set. **Com o ruling relaxando, (A).**

Nota: a alavanca (B) permanece valiosa PARA O LINT/DCE — quando a análise rodar (no lint ou no
linker), ela deve rodar linear, não quadrática. Ou seja, de-quadratizar não some do mapa: muda de
"otimizar o build" para "otimizar o consumidor realocado".

### 5.2 — Realocação e o incremental / content-signed .tkb (a outra ideia do dono)
**Sim — tirar a liveness whole-program do build torna o cache per-módulo LIMPO, e este é um dos
maiores payoffs arquiteturais do reframe.** `analyze_program` roda sobre o TProgram MERGE inteiro
(`typer.tks:4361`) — é intrinsecamente whole-program. Enquanto estiver DENTRO do build, ele é uma
função do programa TODO: um build que assina `.tkb`/`.o`/`.a` e pula módulos não-alterados não pode,
em sã consciência, pular a liveness, porque adicionar um caller no módulo B poderia mudar o veredito
de "unused" no módulo A — forçando invalidação conservadora. **Removida a liveness do build, cada
módulo vira `parse→type→emit` puro** (função só do próprio módulo + assinaturas de dep do `.tkb`), e
o content-signing pode pular corretamente módulos inalterados. A alcançabilidade whole-program roda
DEPOIS, no linker (DCE) e no lint — que são naturalmente whole-program e não estão no caminho crítico
per-módulo. Isto também casa com o parallel front-end (TEKO_FRONTEND_THREADS): a liveness serializada
era um ponto de sincronização; removê-la do build remove a barreira. **Observação fina:** um
`private` é file-local (`check_modules` garante que ninguém cross-ns o referencia), então
unused-private é PER-ARQUIVO-decidível — a varredura whole-program é over-approximação da
implementação, não necessidade (INFERÊNCIA de `check_modules.tks:1-12` + a definição de `private`).
Isto reforça que a análise sai limpa do caminho per-módulo.

### 5.3 — Os dormentes como análise repropositada
`revalidate.tks` (re-prova TAST) → `teko lint --audit-tast` (auto-checagem de corrupção, opt-in).
`borrow.tks` (sumário borrow whole-program) → lint de aliasing mais rico que o spine gate mínimo, E
candidato a input do linker-DCE/PGO (aliasing informa quais frees/otimizações são seguras). Realiza
"não perder trabalho" sem custo de build (já desligados).

---

## 6. Superfície (protocolo counter-argue) — unused RELAXADO é a direção DECIDIDA

### 6.1 — unused-LOCAL (DECIDIDO: relaxar — ruling 0.4)

```teko
fn compute(n: i64) -> i64 {
    let scratch = expensive(n)   // nunca lido depois
    n * 2
}
```

- **ANTES (build falhava — status pré-2026-07-14):**
  ```
  compute.tks:2:9: error: unused local `scratch` — it is declared but never read
      let scratch = expensive(n)
          ^
  ```
  (`initanalysis.tks:193-194`.)
- **DEPOIS do ruling (build compila; sinal recuperado por dois canais):**
  ```
  $ teko build              # compila sem menção a scratch
  $ teko lint               # canal 1 — feedback ao dev, sob demanda
  compute.tks:2:9: warning: unused local `scratch` — declared but never read
      let scratch = expensive(n)
          ^
  # canal 2 — linker-DCE (silencioso): a store de `scratch` e a chamada expensive(n),
  # se sem efeito observável, são elimináveis; o símbolo morto não entra no binário.
  ```
  Honestidade: o build para de pegar o bug de LÓGICA (o dev pretendia usar `scratch`). Isto é
  RECUPERADO pelo lint (quando rodado). É supersedência DATADA de MASTER_PLAN:186 (registrada 0.4),
  não silenciosa.

### 6.2 — unused-PRIVATE fn (relaxar do build; realocar)

```teko
fn helper() -> i64 { 42 }   // private, nunca chamada
pub fn api() -> i64 { 1 }
```

- **ANTES (build imprimia warning, não falhava):**
  `lib.tks:1: warning: unused private function `helper`` (`initanalysis.tks:251`).
- **DEPOIS:** `teko build` silencioso; `teko lint` emite o mesmo warning; o linker-DCE strip-a
  `helper` do binário (é o mesmo fecho de `reachability.tks`, generalizado de lib p/ símbolo).
- Trade-off: remove o maior quadrático do build; o dev vê no lint; binário menor de graça.

### 6.3 — privacidade cross-ns (NÃO coberta pelo ruling — decisão separada)

```teko
// ns a
type Secret = struct { x: i64 }   // privado
// ns b
let s: a::Secret = a::Secret { x = 1 }
```
- **Atual (build falha):** `b.tks:1: error: `a::Secret` is private to namespace `a`` (B.9).
- **Recomendação: MANTER E no build.** É contrato, não higiene; não é hotspot; não há canal
  linker-DCE que recupere o sinal (privacidade não é alcançabilidade). Relocá-la seria o pior
  trade-off. Fora do escopo do ruling 0.4 (que é sobre *unused*, não *privacidade*).

---

## 7. Decisões — TODAS RATIFICADAS (owner 2026-07-14)

> **Convergência fechada (owner 2026-07-14).** As oito questões abaixo foram todas ratificadas pelo
> dono nesta data; a recomendação do integrador foi aceita em cada uma (as duas que ele problematizou
> — Q3 e Q4 — foram fechadas após a rodada de contra-argumento). Registro datado/atribuído; nenhuma
> reaberta. Isto FECHA o desenho no nível de ANÁLISE — a implementação ainda depende de aprovação
> explícita do dono (este documento não altera código).
>
> - **Q1 — unused-local: RELAXAR** (ruling 0.4). Não falha mais o build; supersede MASTER_PLAN:186.
> - **Q2 — `UseGraph` único** servindo lint/linker-DCE/PGO. **RATIFICADO.** Sem risco de perf: sai do
>   caminho quente do build (relaxado) → custo no build = **zero**; o grafo é pago pelos consumidores.
> - **Q3 — duas portas sobre UM motor. RATIFICADO.** LSP (inner loop, incremental por-arquivo) = a
>   superfície MAIS performática pro dev interativo; CLI/`.tkb` (outer loop, CI/auditoria) = batch.
>   Sequência: motor + porta **CLI in-process primeiro** (barato, reusa o typed-tree do build),
>   desenhado incremental-friendly; **LSP** como a superfície interativa definitiva depois.
> - **Q4 — MANTER privacidade ESSENCIAL (E). RATIFICADO** ("ok em manter a segurança de
>   encapsulamento"). Privacidade ≠ unused: é acesso-ilegal (aresta VIVA) que o DCE NÃO recupera —
>   o DCE só mata o não-referenciado. Payoff de perf nulo, contrato real preservado. Ressalva load-
>   bearing: auditar em `resolve.tks` se "ref bare a outra ns" é pré-requisito de RESOLUÇÃO (por
>   corretude, não perf) antes de qualquer toque.
> - **Q5 — SIM**: tirar a liveness whole-program do build é pré-requisito do incremental content-signed.
> - **Q6 — SIM**: dormentes (`revalidate`, `borrow`) → lint opt-in; `borrow` também input de DCE/PGO.
> - **Q7 — SIM**: `exhaustive` permanece E enquanto o codegen não emitir trap de arm-default.
> - **Q8 — JUNTO**: de-quadratizar no consumidor (lint/linker), não no build (que deixa de rodá-la).

O texto abaixo é a fundamentação de cada recomendação, agora ACEITA (não mais aberta):

**Q2. A alcançabilidade deve ser refatorada de `error?` para um `UseGraph` (§3.2) servindo os três
consumidores, ou cada consumidor reimplementa seu próprio walk?**
Rec.: expor um `UseGraph` único (generalizando `reachability.tks::close_reachable`, que já prova a
forma). Trade-off: um contrato de dado a manter, mas evita TRÊS walks divergentes (lint/linker/PGO) e
capitaliza o precedente já em árvore; é o que concretiza "muito do checker reutilizado". Custo:
definir a estrutura antes de linker/PGO existirem (design-ahead contra a forma declarada).

**Q3. `teko lint` in-process (reusa `analyze_program`/`UseGraph` sobre o `Frontend.prog`,
`project.tks:296`) ou via `.tkb`?**
Rec.: começar in-process (zero código novo, reusa o passe verbatim); migrar p/ `.tkb` quando o
incremental (Q5) amadurecer. Trade-off: in-process re-tipa a cada lint; via-`.tkb` é incremental mas
o `.tkb` hoje não é fixpoint-artifact nem carrega borrow.

**Q4. privacidade cross-ns: manter E ou realocar a regra-(b) p/ lint?**
Rec.: manter E. Trade-off: contrato, não higiene; não é hotspot; sem canal DCE de recuperação;
payoff de perf ~nulo. As regras (a)/(c) merecem auditoria de corretude-de-resolução — por solidez,
não perf. FORA do ruling de unused.

**Q5. Tirar a liveness whole-program do build é pré-requisito do incremental content-signed?**
Rec.: SIM, tratar como pré-requisito (§5.2). Com o ruling relaxando unused, isto sai naturalmente: a
liveness deixa o caminho per-módulo, o build vira função pura por-módulo, o content-signing pula
inalterados corretamente, e a alcançabilidade whole-program roda no linker/lint. Trade-off: exige que
o linker-DCE assuma a limpeza (o que o dono já designou); casa com o parallel front-end.

**Q6. Os dormentes (revalidate.tks, borrow.tks) migram p/ `teko lint` (+ borrow → input de
DCE/PGO)?**
Rec.: sim, registrá-los como verificações opt-in do lint; borrow adicionalmente como input de
aliasing p/ o linker-DCE/PGO. Trade-off: dá residência e propósito a trabalho construído, sem custo
de build (já desligados). Nenhuma decisão de solidez.

**Q7. exhaustive permanece E?**
Rec.: sim, enquanto o codegen não emitir trap de arm-default (§2.3). Custo de manter ~0; mover sem a
compensação no backend reintroduz bug de solidez. NÃO é candidato a relocação.

**Q8. De-quadratizar a alcançabilidade agora (§5.1-B) ou junto da realocação?**
Rec.: junto da realocação, no consumidor (lint/linker), não no build (que deixa de rodá-la). O
linear O(N+P) do fecho worklist já existe em `reachability.tks:215` — o linker-DCE herda-o de graça.
Trade-off: se o dono quiser ganho de perf ANTES do linker chegar, um de-quadratize in-place temporário
(mantendo unused-local como warning-não-fatal no build) é uma ponte — mas o ruling 0.4 já tira a
urgência, pois relaxar remove o passe do caminho crítico.

---

## Apêndice — mapa de dependência de produtos (quem lê o quê)

- **TAST (`.type`)** ← typing (10) → codegen, VM, `.tkb`, monomorph. **E.**
- **TypeTable** ← collect (4) → todos + codegen. **E.**
- **DiRegistry** ← build_di_registry (7) → typing + codegen. **E.**
- **frame-escape set** ← escape (14) → codegen C. **E (backend C).**
- **spine facts** ← fn_spine (10f) → gate MEM. **E (solidez).**
- **`error?` de analyze_program (12)** → NINGUÉM hoje; sob o reframe vira **`UseGraph` → lint +
  linker-DCE + PGO**. **R.**
- **`error?` de check_modules (5)** → NINGUÉM; privacidade R-parcial (só lint, sem canal DCE).
- **reachable-libs (`reachability.tks`)** ← generaliza `occurs_expr` → **linker (link flags)** HOJE;
  a symbol-DCE é a extensão natural. É a âncora do reframe.
- **borrow summary / revalidate** → NINGUÉM (desligados) → candidatos a lint / input DCE-PGO.

**Achado-síntese:** todo produto de DADO consumido pelo codegen/VM vem de passes E; os passes cujo
único produto é `error?`-sem-dado (analyze_program; check_modules-privacidade) são exatamente os
R-elegíveis — e o mais caro (analyze_program) é o hotspot. O corte E/R coincide com o corte
tem-dado / só-veredito E com o corte barato / caro. Sob o reframe do dono, o `error?` descartado de
`analyze_program` é reinterpretado como um `UseGraph` que UMA análise entrega a TRÊS consumidores
(lint, linker-DCE, PGO) — e `src/build/reachability.tks` já é a prova viva de que a máquina de
alcançabilidade do checker se reutiliza fora dele. A relocação não corta nenhuma aresta de DADO do
grafo de compilação; ela promove uma aresta hoje descartada (o veredito de liveness) a uma
estrutura de primeira classe compartilhada.
