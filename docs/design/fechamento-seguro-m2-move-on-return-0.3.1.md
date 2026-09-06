# FECHAMENTO SEGURO DE M2 — o ponto de ruptura do move-on-return (0.3.1)

> **MATERIAL DE DECISÃO — aguarda ruling do dono. NADA IMPLEMENTADO.**
> Nenhuma linha de produto foi tocada. Nenhum bump, nenhum PR. Este documento analisa os DOIS
> mecanismos candidatos que o agente da rota C apontou para fechar M2 sem UAF, prova cada um contra
> os padrões reais do compilador, e RECOMENDA. A escolha entre os mecanismos, e o sinal verde para
> implementar, são do dono.

Arquiteto, 2026-08-02. Ramo `fix/union`. Complementa
`docs/design/transicao-move-on-return-e-seletor-n-niveis-0.3.1.md` (a transição A1–A7; o §3b é o
contrato de conveyance cuja lacuna este documento fecha) e
`docs/design/modelo-de-memoria-por-escopo-0.3.1.md` (§1 prova não-UAF por LUB).

---

## 0. O ponto de ruptura, medido com arquivo:linha

A rota C entregou A1–A4/M1 + A6 byte-idênticos (`cargo/0.3.1.0-move-on-return-impl`, commits
`cea900cc`/`efb7165a`/`8267cecb`). **M2 — o flip da conveyance — PAROU** com UAF provado por ASan no
self-host. A causa exata:

**O contrato §3b só prova o RE-RETORNO DIRETO.** O §3b (`transicao-…-0.3.1.md:184-202`) faz o callee
depositar o resultado escapante na região-corrente-de-entrada (`R_ret`), e prova a composição
transitiva para `a() { return b() }`: `a` faz `tk_region_leave()` ANTES de avaliar o `return`, então
`b` nasce com `current == R_ret_a`, e o valor bolha direto ao consumidor de topo. **Mas o padrão
DOMINANTE do compilador não é re-retorno direto — é valor escapante RETIDO num local, e só um CAMPO
dele embrulhado num struct de retorno.**

O regressor é `parser::parse_function` (`src/parser/parse_decl.tks:410-411`):

```teko
let blk = match parse_block(tokens, p) { ParsedList<Statement> as x => x; error as e => return e }
Parsed<Decl> { node = Function { … body = blk.items … }; next = blk.next }
```

Sob M2, `parse_function` faz `tk_region_enter(_tkfr)` na entrada, então `current == _tkfr` durante o
CORPO. A chamada `parse_block(...)` executa com `current == _tkfr` → o seu `R_ret` é `_tkfr` → o
`ParsedList` (e o seu `items: []Statement`) materializa em `_tkfr`. O binding local `blk` RETÉM esse
valor. O `return` constrói `Function { body = blk.items }`: o struct-embrulho é `Caller`-tier e
materializa em `_tkrr` (a região do caller), mas `blk.items` é só um PONTEIRO de slice copiado para
dentro do struct — **o buffer apontado continua em `_tkfr`**. No `return`, `tk_region_drop(_tkfr)`
liberta esse buffer. O checker lê a AST depois → **heap-use-after-free em
`checker::collect_item_insts ← parser::parse_function → tk_region_drop`.**

**A lacuna nomeada, exatamente:** o `tk_region_leave()`-antes-do-return de M2 só corrige a posição
de retorno DIRETA (a expressão avaliada após o leave). Um valor escapante produzido por uma chamada
ANTES do return, retido num local e embrulhado, foi depositado em `_tkfr` no momento da chamada —
cedo demais — e o leave-antes-do-return chega tarde. O buffer transitivamente retido NÃO é movido.

---

## 1. A raiz comum aos dois mecanismos

Os dois candidatos atacam a MESMA pergunta: **como é que uma chamada-callee cujo resultado escapa
deposita esse resultado na região de DESTINO do escape (`_tkrr` = a região do caller) em vez da
região-corrente-de-frame (`_tkfr`)?**

- A análise de escape JÁ marca `blk` como escapante: uma leitura de `blk` (`blk.items`) ocorre numa
  posição escapante (campo de struct-init de um valor retornado), e `escape.tks:15-21` propaga o
  contexto escapante para baixo → `blk ∈ fn_escaping_vars`. Conservadora por construção
  (`escape.tks:9-12`: na dúvida → escapante). Portanto o conjunto de bindings escapantes é um
  SUPERCONJUNTO do verdadeiro — sobre-marca, nunca sub-marca.
- O que falta é o canal de CONVEYANCE do resultado da chamada retida: fazer `parse_block` depositar
  em `_tkrr`, não em `_tkfr`.

Os dois mecanismos diferem só no CANAL:

1. **Brackets `leave/enter` por-chamada-escapante:** manipula a pilha de região-corrente (o MESMO
   canal que §3b já usa) para que, durante a chamada escapante, `current == _tkrr`.
2. **ABI de return-slot:** um canal SEPARADO — o caller passa a região/slot de destino ao callee, e o
   `return` do callee materializa lá, independente de `current`.

---

## 2. Mecanismo 1 — Brackets `leave/enter` por-chamada-escapante

**A ideia, precisa.** Envolver a AVALIAÇÃO da RHS de todo binding/assign cujo nome ∈
`fn_escaping_vars` num par `tk_region_leave() … tk_region_enter(_tkfr)`. Como sob M2 a pilha é
`[… , _tkrr, _tkfr]` (o caller escolheu `_tkrr = current`, esta função empurrou `_tkfr` por cima), o
`leave()` expõe `_tkrr` como corrente. A chamada escapante nasce então com `R_ret == _tkrr` e o seu
resultado materializa na região do caller. O `enter(_tkfr)` restaura a corrente-de-frame para o resto
do corpo.

Isto é, literalmente, **estender a disciplina leave-antes-do-return de M2 (que já cobre a expressão
de retorno) para toda RHS escapante retida num local.** Mesmo primitivo, mesma prova, mesma pilha.

### 2a. Prova de segurança-UAF contra `parse_function`

`blk ∈ fn_escaping_vars`. A RHS de `blk` (`match parse_block(...) { … }`) é bracketada: `leave()` →
`current == _tkrr`; `parse_block` executa com `R_ret == _tkrr`; o `ParsedList` e o seu `items`
materializam em `_tkrr`; `enter(_tkfr)`. No `return`, o leave-antes-do-return (M2) expõe `_tkrr`;
`Function { body = blk.items }` constrói em `_tkrr`; **`blk.items` já reside em `_tkrr`** →
`tk_region_drop(_tkfr)` não toca no buffer. O checker lê a AST em `_tkrr`, viva enquanto o caller
(que a consome) viver. `R_caller = _tkrr ⊒ todos os usos` (o LUB do §1). **Sem UAF.** ∎

O único primitivo novo por-sítio é o par leave/enter — símbolos que já existem e ambas as rotas já
emitem (`tk_region_enter`/`tk_region_leave`, `teko_rt.c:1855`/`:1860`).

### 2b. Prova contra os outros padrões

1. **Re-retorno direto (`return b()`):** a expressão de retorno já é bracketada pelo leave-antes-do-
   return de M2. Nenhum bracket adicional. §3b inalterado. **Seguro.** ✓
2. **Escapante em slice/lista (`xs = push(xs, faz_algo())`, `xs` escapa):** `xs ∈ escaping` → a RHS
   inteira é bracketada → o `push` E o elemento produzido por `faz_algo()` nascem em `_tkrr`. O
   acumulador escapante e os seus elementos co-residem em `_tkrr`, morrem com o caller.
   Complementarmente, se `xs` NÃO escapa mas é frame-local (`_tkfr`) e o elemento é uma chamada bare,
   o elemento nasce em `current == _tkfr` (mesma região do acumulador) → morrem juntos → **seguro**
   (ambos frame-local; consistente com o modelo). ✓
3. **Escapante em closure:** o env capturante é alocado em `tk_region_alloc(tk_region_root(), …)`
   (`codegen.tks:8013`). Uma closure que escapa tem de ter o env a viver tanto quanto ela. Hoje →
   root (leak-seguro). No modelo move, o env de uma closure escapante deve → `_tkrr`, e uma variável
   CAPTURADA que seja resultado-de-chamada tem de ser conduzida a `_tkrr` também. **Este é o único
   sítio ADICIONAL que os brackets não cobrem sozinhos** — a alocação do env é um sítio de alocação
   nomeável (como as de M1), a rotear a `_tkrr` quando a closure ∈ escaping. Marcado como ponto de
   verificação (fixture `closure_escape`, §5). Sob o fallback conservador (env → root) é leak-seguro
   até esse roteamento aterrar — **nunca UAF**. ✓

### 2c. A aresta afiada: brackets que cruzam control-flow (retorno-em-erro embutido)

A RHS de um binding no compilador é tipicamente um `match call() { X as x => x; error as e => return e }` —
o padrão de propagação de erro. Um `leave()` ingénuo antes da RHS deixa a pilha desbalanceada no braço
`error => return e`: o valor `e` (escapante) nasce corretamente em `_tkrr` (bom), mas o `leave()` do
bracket e o `leave()`-antes-do-return da aresta de retorno pilham DUAS vezes.

**Resolução (precedente existente).** O `leave()` do bracket tem de ser DESFEITO em cada aresta de
saída da RHS, exatamente como os defers e os `tk_region_drop` já são desfeitos por aresta
(`DeferCtx`, `codegen.tks:8299`; drops em return/break/continue/fall-through, `:9123`). O contexto de
codegen ganha um contador de "brackets abertos" (profundidade de leave pendente), e cada aresta de
retorno emite os `enter` de reequilíbrio antes do seu próprio leave+drop — a MESMA máquina de
unwind-por-aresta. `tk_region_leave` é auto-guardado contra underflow (`teko_rt.c:1861`), o que dá
uma rede de segurança, mas o balanceamento explícito é o contrato. **Esta é a complexidade real do
Mecanismo 1** — contida, precedente, gate-ável.

Nota de simplificação: usar `enter(_tkrr); rhs; leave()` (empurrar `_tkrr` por cima em vez de pilhar
`_tkfr`) dá o mesmo `current == _tkrr` durante a RHS e transforma o par num enter/leave NORMAL
(empurra-depois-pilha), idêntico à disciplina de região de bloco que o codegen já desfaz por aresta.
Recomendo esta forma: reusa a máquina de unwind de região-de-bloco sem inversão de sinal.

### 2d. Impacto no fixpoint

FIXPOINT é gen2==gen3 (auto-consistência determinística), não gen2==gen1 — qualquer mudança
determinística preserva-o (`transicao-…:399`, R7). O detetor real é `TEKO_MEM_PARANOID` + `teko test
.`. O Mecanismo 1 encaixa no LADDER de degraus M-literais tal como M1 fez (§4): um degrau intermédio
com `_tkfr` ALIASED a root e `_tkrr = root` exercita todo o TEXTO de brackets/leaves com comportamento
de runtime IDÊNTICO (tudo em root, nada morre cedo) — prova de balanceamento SEM libertação precoce.
Só depois o `_tkfr` vira child real e o `_tkrr` vira `current`. **Encaixe limpo no ladder.**

### 2e. Superfície no runtime — o achado adjacente

O achado: `tk_slice_push_r`/`tk_slice_with_cap_r` recebem a região explícita (OK), mas
`tk_slice_push_fo` (`teko_rt.c:3929`), `tk_append_bytes_fo` (`:3850`) e os builders de str/byte
alocam via `tk_region_current()` e devolvem buffers escapantes. **O Mecanismo 1 lida com isto SEM
NENHUM símbolo novo:** essas helpers seguem a corrente; bracketar a RHS escapante põe `current ==
_tkrr` e elas materializam em `_tkrr` automaticamente. As helpers bare-que-seguem-a-corrente Just
Work. **É o ponto decisivo:** a superfície de runtime a mexer é ZERO (além do acessor público
`tk_region_current` que A1 já expôs).

Cuidado pontual do `_fo` (free-old): num self-append escapante bracketado, o buffer OLD pode estar em
`_tkfr` e o NEW em `_tkrr`. O `tk_free_block` já guarda `if (tk_cur_rsp != 0) return`
(`teko_rt.c:4005`) — dentro de uma janela scoped não parqueia no free-list de root, evitando o dangle.
Com a forma `enter(_tkrr); …; leave()`, durante a RHS `cur_rsp > 0` → o guard ativa → seguro. A guarda
de geração do push-cache (`region_gen`, `:3782`) impede in-place false-hits entre regiões. **Coberto
pelos invariantes existentes.**

### 2f. Custo/complexidade e risco de UAF-em-caso-esquecido

- **Custo:** um par enter/leave por binding/assign escapante retido. Barato (dois writes na pilha
  thread-local). O conjunto escapante é pequeno (locais de uma função).
- **Complexidade:** o unwind-por-aresta dos brackets (§2c) — precedente na máquina de defers/drops.
- **UAF-em-caso-esquecido:** a ÚNICA via de UAF é uma RHS escapante NÃO bracketada. Fecha-se com uma
  regra CEGA: **bracketar a RHS de TODO binding/assign cujo nome ∈ `fn_escaping_vars`** — sem detetar
  "contém uma chamada" (bracketar uma RHS pura é inócuo). Assim, se escapa, é bracketado. A única via
  residual é a INSANIDADE da análise de escape (sub-marcação) — a MESMA precondição load-bearing que
  M2 já assume (`transicao-…:230-233`). **O Mecanismo 1 NÃO adiciona superfície de UAF nova** além da
  soundness de escape já load-bearing.

### 2g. Aplicabilidade C + nativo

`tk_region_enter`/`leave` são primitivos de runtime que AMBAS as rotas já emitem (C como texto,
nativo como `call_inst`; `lower.tks` já lowera `region_new`/`region_alloc`, `:3487-3506`). O modelo
de pilha-corrente é PARTILHADO (`teko_rt.c:1846-1863`, único por-task). A rota nativa emite os mesmos
brackets nas mesmas arestas, derivados do MESMO `fn_escaping_vars`. **Espelho exato, zero divergência
possível** — é o mesmo primitivo no mesmo runtime.

---

## 3. Mecanismo 2 — ABI de return-slot

**A ideia.** O caller passa a região-destino (ou um slot de storage) ao callee; o `return` do callee
materializa o valor escapante diretamente lá.

Há DUAS realizações, e ambas falham ou degeneram:

### 3a. Return-slot como PONTEIRO (estilo RVO C++)

O caller aloca o storage do struct de retorno e passa `&dst`; o callee constrói o resultado
in-place. **Não resolve o problema transitivo.** Em `parse_function`, o RVO construiria `Function`
no slot do caller, mas `Function.body = blk.items` continua a ser um PONTEIRO para um buffer em
`_tkfr`. O buffer transitivamente retido NÃO é movido pelo slot do struct externo. **UAF permanece.**
Só o embrulho exterior muda de sítio; o interior retido não. Descartado.

### 3b. Return-slot como REGIÃO (o caller passa `_tkrr` ao callee)

Para de facto mover o buffer transitivo, o callee tem de alocar TODOS os seus valores `Caller`-tier
na região passada. Isto é: passar a região-destino e usá-la como o `R_ret` do callee. **Mas isto é
EXATAMENTE o que a pilha-corrente já faz** (`R_ret = current-na-entrada`). Um canal-de-região
separado é um SEGUNDO canal-corrente redundante, com custo estrito adicional:

- **ABI muda.** Ou (i) um parâmetro novo em cada assinatura → quebra a ABI C com extern fns, o runtime
  escrito à mão, o FFI (viola R4, `transicao-…:396`: "ABI intocada"); ou (ii) um slot TLS
  "região-de-retorno-pendente" que o caller escreve antes da chamada e o callee lê à entrada — o que
  é FUNCIONALMENTE idêntico à pilha-corrente, só que com um segundo mecanismo a manter sincronizado.
- **Superfície de runtime explode.** Para materializar valores escapantes construídos incrementalmente
  (slices, strings, bytes) na região-slot, TODAS as helpers que hoje seguem a corrente
  (`tk_slice_push_fo`, `tk_append_bytes_fo`, concat/builders de str/byte) precisam de gémeos `_r`/slot
  threaded. É a EXATA superfície que o Mecanismo 1 evita (§2e). Grande, e a rota nativa tem de espelhar
  cada gémeo na sua convenção de chamada → risco de divergência C-vs-nativo.
- **Risco de UAF-em-caso-esquecido MAIOR.** Cada chamada que possa conduzir um valor escapante tem de
  receber o slot. Uma chamada esquecida → retorna via corrente (que pode ser `_tkfr`) → UAF. A
  superfície de "esquecer" é toda chamada-que-conduz-escape, não só bindings escapantes retidos.

### 3c. Fixpoint / C+nativo do Mecanismo 2

Um canal-slot não estagia como um degrau comportamento-idêntico tão limpo como o M-literal-root: o
canal existe ou não. Mais disruptivo ao ladder. E o espelhamento nativo da ABI de slot é trabalho
por-rota com detalhes de convenção que podem divergir — o oposto da unificação C4
(`transicao-…:253-278`).

**Veredicto do Mecanismo 2:** a variante ponteiro NÃO fecha o UAF transitivo; a variante região
DEGENERA num segundo canal-corrente que replica a pilha existente com ABI alterada, superfície de
runtime muito maior, maior risco de esquecimento, e pior unificação C/nativo. Sem ganho de segurança
sobre o Mecanismo 1.

---

## 4. Comparação e RECOMENDAÇÃO

| eixo | Mec. 1 (brackets leave/enter) | Mec. 2 (return-slot ABI) |
|---|---|---|
| fecha o UAF transitivo de `parse_function` | **Sim** (§2a) | ponteiro: **Não**; região: sim mas por 2º canal |
| ABI | **intocada** (R4) | ponteiro/param: quebra; TLS: 2º canal redundante |
| superfície de runtime nova | **zero** (helpers-corrente Just Work) | gémeos `_r`/slot para todas as helpers de escape |
| canal | reusa a pilha-corrente que §3b já usa | canal separado a manter sincronizado |
| fixpoint / degrau M-literal | encaixa (alias-root → child → move) | disruptivo; sem degrau limpo |
| C + nativo | espelho exato (mesmo primitivo) | ABI de slot por-rota; risco de divergência |
| UAF-em-caso-esquecido | só sub-marcação de escape (precondição já load-bearing) | toda chamada-condutora esquecida = UAF |
| complexidade | unwind-por-aresta dos brackets (precedente defers/drops) | ABI + gémeos + threading do slot |

**RECOMENDAÇÃO: Mecanismo 1 — brackets `leave/enter` por RHS escapante retida, enquadrados como a
extensão da disciplina leave-antes-do-return de M2 do tail para toda RHS escapante.** Fecha o UAF
transitivo real, sem tocar a ABI (R4), sem UM único símbolo de runtime novo (decisivo dado o achado
`_fo`/str/byte), com espelho C-nativo exato, encaixando no ladder de degraus M-literais, e sem
adicionar superfície de UAF além da soundness de escape que M2 já pressupõe. A única aresta afiada
(brackets a cruzar retorno-em-erro embutido) reusa a máquina de unwind-por-aresta já existente.

---

## 5. Sequência ORDENADA de crumbs (M2a0 → M2a → M2b) — o mecanismo recomendado

Estado de partida (aterrado, byte-idêntico): M1 = nomear escapante → `_tkrr = tk_region_root()`; SEM
enter; `current == root` sempre; tudo vaza para root. Cada crumb é gate-ável em separado.

> **REGRA DO DONO (obrigatória) — SEM `.tkp` NOVO.** Cada `.tkp` = +1 build; a contagem tem de CAIR,
> não subir. Toda prova de regressão abaixo REUTILIZA um projeto de regressão JÁ EXISTENTE da família
> `mem_*` — adiciona um `.tkr` (cenário) e/ou um entrypoint ao `src` de um `.tkp` que já existe,
> consolidando idealmente a família num único projeto `mem` com vários `.tkr`. **Zero `.tkp` novo.**
> Um `.tkr` suporta múltiplos `Feature`/`Scenario` partilhando UM build (`regression.tks:2132`).

### M2a0 — PLUMBING, BYTE/COMPORTAMENTO-IDÊNTICO (não é o flip perigoso)

Emitir o TEXTO completo da conveyance mas NEUTRALIZADO em root:
- `tk_region_enter(_tkfr)` na entrada; brackets `enter(_tkrr) … leave()` à volta de cada RHS de
  binding/assign com nome ∈ `fn_escaping_vars`; `tk_region_leave()` antes de cada expr de retorno e
  antes de cada `tk_region_drop(_tkfr)`; balanceado em TODAS as arestas (return/break/continue/
  fall-through) via o contador de brackets abertos no contexto de codegen.
- MAS abrir `_tkfr = tk_region_root()` (ALIAS de root) e `_tkrr = tk_region_root()`, e SUPRIMIR o
  `tk_region_drop(_tkfr)` enquanto aliased (nunca dropar root).
- **Runtime:** tudo aloca em root; leaves expõem root; nada morre cedo. **Comportamento == M1.**
- **O que prova:** o BALANCEAMENTO da pilha em todas as arestas (enters==leaves), a colocação dos
  brackets compila, o unwind-por-aresta é correto. Sem qualquer libertação precoce.
- **Gate:** `teko test .` verde; FIXPOINT gen2==gen3; `TEKO_MEM_PARANOID` limpo; `TEKO_ARENA_OBS`
  enters==leaves (pilha zerada no fim de cada função). **Ritual: SIM.** **BYTE-IDÊNTICO.**

### M2a — a RECLAMAÇÃO do scratch não-escapante (o gate DURO de completude dos brackets)

- `_tkfr = tk_region_new(tk_region_root())` (child REAL); reativar `tk_region_drop(_tkfr)` nas
  arestas. `_tkrr` FICA `tk_region_root()`.
- **Runtime:** scratch NÃO-escapante (call-results não retidos em escape) nasce em `_tkfr` e é
  reclamado na saída da função (a poupança de memória COMEÇA aqui). Escapante (nomeado `_tkrr` em M1
  + conduzido pelos brackets para `_tkrr == root`) continua a vazar para root — destino INALTERADO.
- **É AQUI que o UAF de `parse_function` APARECERIA sem brackets** — e é aqui que os brackets se
  PROVAM: com `_tkrr == root`, o raio de explosão é contido (escapante ainda em root; só scratch é
  novo a morrer). Um escapante sub-bracketado cairia em `_tkfr` e morreria → o gate apanha.
- **Precondição load-bearing:** completude dos brackets (regra cega: toda RHS escapante bracketada) +
  soundness da análise de escape (`escape.tks:9-12`).
- **Gate:** FIXPOINT gen2==gen3; `TEKO_MEM_PARANOID` limpo (rodar o self-host sob ASan — o repro do
  agente é o oráculo); `TEKO_ARENA_OBS` scoped>0; fixtures `mem_move_retain_struct` (o padrão
  `parse_function`), `mem_move_return`, `mem_move_transitive`. **Ritual: SIM.** **PERIGOSO (UAF —
  completude dos brackets).**

### M2b — o FLIP DO MOVE (`_tkrr`: root → current)

- `_tkrr = tk_region_current()`; `_tkfr = tk_region_new(_tkrr)` (pai passa de root para `_tkrr`,
  `cg_frame_region_parent_expr` `codegen.tks:10017`).
- **Runtime:** o `leave()` dos brackets e do return agora expõem `_tkrr == a região REAL do caller`.
  Valores escapantes (nomeados + conduzidos a `_tkrr`) materializam na região do caller → **MOVE** →
  reclamados com o caller em vez de vazar para root.
- **Não abre UAF novo:** o destino muda de root para `_tkrr`, e `_tkrr = current-na-entrada = a região
  que o caller escolheu ⊒ este frame ⊒ todos os usos neste frame`; o valor escapa para o caller que
  POSSUI `_tkrr` → `R_caller ⊒ usos` (LUB, §1). Ambos os destinos (root em M2a, `_tkrr` em M2b) são
  seguros; M2b só troca leak↑root por reclaim↑caller. A completude dos brackets já foi provada em M2a.
- **Gate:** FIXPOINT gen2==gen3; `TEKO_MEM_PARANOID` limpo; `TEKO_ARENA_OBS` regiões-largadas ≈ nº de
  escopos com locais, `unresolved`(root)=0 exceto o frame do main; fixtures `mem_no_root_leak`,
  `mem_move_retain_struct` (re-verificado sob o move real). **Ritual: SIM.** **É O FLIP PERIGOSO do
  MOVE** (semântica de destino; UAF já fechado em M2a).

### M2c (opcional, aditivo) — env de closure escapante → `_tkrr`

Rotear a alocação do env capturante (`codegen.tks:8013`, hoje `tk_region_root()` fixo) a `_tkrr`
quando a closure ∈ `fn_escaping_vars`. Fallback → root é leak-seguro (nunca UAF) até aterrar.
- **Gate:** fixture `closure_escape` (closure retornada, invocada pelo caller após o return).
  **Ritual: SIM.** Aditivo; leak-seguro sem ele.

### M2-nativo — herdar a conveyance (`lower.tks`), quando o link nativo fechar

Espelho de M2a0→M2b em `lower.tks`: enter/leave por escopo + brackets nas mesmas arestas derivados do
MESMO `fn_escaping_vars`; `return` pelo contrato §3b. Valida sob `TEKO_BACKEND=native`, FIXPOINT
gen2==gen3 BINÁRIO, quando o link `mem::*` fechar (outro agente). Não bloqueia a rota C (A1–M2b
valida-se INTEIRA por gen2==gen3 sem o link nativo). **Ritual: SIM (ao fechar o link).**

**Onde está o flip perigoso, explícito:** M2a (completude dos brackets — a via de UAF) e M2b (o move
de destino). M2a0 é a rede byte-idêntica que isola o balanceamento da pilha ANTES de qualquer
libertação. Ordem de risco: M2a0 (idêntico) → M2a (UAF-gate) → M2b (move) → M2c (aditivo) → nativo.

---

## 6. Fixtures — REUTILIZAM `.tkp` existente (zero `.tkp` novo)

Cada prova é um `Scenario`/entrypoint ADICIONADO a um projeto `mem_*` JÁ EXISTENTE (idealmente
consolidando a família num `mem` único). ASSERTAM STDOUT (`.tkr`, `Then stdout pattern = "…"`;
`Given env = ["TEKO_BACKEND=c"]` para a rota C). Corrupção = fingerprint errado, não só leak.

| prova (novo `.tkr`/entrypoint num `.tkp` existente) | o que pina | crumb |
|---|---|---|
| **`mem_move_retain_struct`** | o padrão `parse_function`: valor construído por uma chamada, RETIDO num local, um CAMPO embrulhado num struct RETORNADO; usado após a chamada. Sem brackets → UAF/valor-corrompido; com brackets → fingerprint exato | M2a |
| `mem_move_return` | valor construído no callee e retornado, usado após a chamada (a F do callee não o pode reter) | M2a |
| `mem_move_transitive` | valor retornado por N frames aninhados (bolha ao consumidor de topo) | M2a |
| `mem_no_root_leak` | corpo só com locais+retornos; `TEKO_ARENA_OBS` scoped>0 e root-unresolved=0 | M2b |
| `closure_escape` | closure retornada, invocada pelo caller após o return; env sobrevive | M2c |

`mem_move_retain_struct` é o DETETOR-CHAVE do fechamento de M2: é o repro-mínimo do UAF que parou a
rota C. Deve ser adicionado ao `.tkp` `mem_*` existente (p.ex. como novo `.tkr` no projeto que a
família consolidar), **NUNCA como projeto novo.** O regressor de ASan/PARANOID continua a ser o
self-host completo sob `TEKO_MEM_PARANOID` (o oráculo do agente), não um `.tkp` adicional.

---

## 7. Assinaturas Teko que o implementador adiciona/toca (full Javadoc — copiar verbatim)

O acessor `tk_region_current` já foi exposto por A1. As helpers-corrente (`tk_slice_push_fo`,
`tk_append_bytes_fo`, builders de str/byte) NÃO mudam — é o ganho do Mecanismo 1. A conveyance de
brackets adiciona um predicado e um contexto de profundidade de bracket ao codegen:

```teko
/**
 * binding_conveys_escape — true quando a RHS de um binding/assign de nome `name` deve ser envolvida
 * num bracket de conveyance (`enter(_tkrr) … leave()`) para que qualquer resultado-de-chamada retido
 * nasça na região de retorno `_tkrr` (a região do caller) em vez da corrente-de-frame `_tkfr`, que o
 * `tk_region_drop(_tkfr)` libertaria antes de o valor escapado ser lido (o UAF de `parse_function`).
 *
 * REGRA CEGA (soundness): true sse `name ∈ fn_escaping_vars`. Não deteta "contém uma chamada" — uma
 * RHS pura bracketada é inócua, e sub-detetar seria UAF. Sobre-bracketar é leak-seguro (`_tkrr ⊒`
 * todos os usos). A única via residual de UAF é a sub-marcação da análise de escape — a precondição
 * load-bearing que M2 já pressupõe (escape.tks:9-12, conservadora).
 *
 * @param escaping  o conjunto fn_escaping_vars da função (superconjunto do verdadeiro-escapante)
 * @param name      o nome LHS do binding/assign
 * @return          true sse a RHS de `name` deve ser bracketada para conveyance a `_tkrr`
 * @since 0.3.1
 */
fn binding_conveys_escape(escaping: []str, name: str): bool
```

```teko
/**
 * cg_open_conveyance_bracket — emitir o `tk_region_enter(<rr>);` de abertura de um bracket de
 * conveyance à volta de uma RHS escapante, e devolver o contexto de codegen com a profundidade de
 * bracket incrementada, para que cada aresta de saída (return/break/continue/fall-through) EMITA o
 * `tk_region_leave()` de reequilíbrio correspondente ANTES do seu próprio leave-de-return e
 * `tk_region_drop(_tkfr)`. Espelha a máquina de unwind-por-aresta dos defers e dos drops de região
 * de bloco (DeferCtx, codegen.tks:8299), pela qual não há aresta que deixe a pilha desbalanceada.
 *
 * @param out    o buffer de saída C
 * @param rr     o nome da região de retorno (`_tkrr`) a entrar
 * @param dctx   o contexto de defer/bracket a incrementar
 * @return       (buffer, dctx') com a profundidade de bracket +1
 * @since 0.3.1
 */
fn cg_open_conveyance_bracket(out: []byte, rr: str, dctx: DeferCtx): (out: []byte, dctx: DeferCtx)
```

**Funções existentes que a transição TOCA (rota C):** `codegen.tks` `emit_function_cov`
(`:10064-10127` no ramo impl — o enter de `_tkfr`, os brackets à volta de RHS escapantes, o
leave-antes-do-return e o balanceamento por aresta), `cg_frame_region_parent_expr` (`:10017` — pai de
`_tkfr` passa de root para `_tkrr` em M2b), a emissão do env de closure (`:8013` — → `_tkrr` em M2c);
`emit_binding`/`emit_assign` (envolver a RHS escapante nos brackets); o contexto `DeferCtx`
(`:8299` — ganha a profundidade de bracket para o unwind-por-aresta). **Nada em `teko_rt.{c,h}`** além
do que A1 já expôs. **Rota nativa (`lower.tks`, quando o link fechar):** o mesmo enter/leave/brackets
nas mesmas arestas.

---

## 8. Riscos, tensões de lei, HALT

| risco / tensão | resolução (law-first) |
|---|---|
| **R1 — RHS escapante não bracketada → UAF** | Regra CEGA: bracketar toda RHS de nome ∈ `fn_escaping_vars` (§2f). Via residual = sub-marcação de escape, a precondição já load-bearing de M2. Provado em M2a com `_tkrr==root` (raio contido). |
| **R2 — brackets a cruzar retorno-em-erro embutido desbalanceiam a pilha** | Unwind-por-aresta via a máquina de defers/drops existente (§2c); `tk_region_leave` auto-guardado (`teko_rt.c:1861`) como rede. M2a0 prova o balanceamento byte-idêntico. |
| **R3 — helper `_fo`/str/byte a devolver buffer escapante da corrente errada** | O Mecanismo 1 põe `current==_tkrr` durante a RHS → seguem-na automaticamente; ZERO gémeos `_r` novos (§2e). `tk_free_block` guarda `cur_rsp!=0` (`:4005`); guarda de geração do push-cache (`:3782`). |
| **R4 — env de closure escapante** | Sítio adicional (M2c), roteável a `_tkrr`; fallback root leak-seguro. Nunca UAF. |
| **R5 — ABI congelada vs. move** | Mecanismo 1 reusa a pilha-corrente thread-local — ABI INTOCADA (o Mecanismo 2 é que a tensionaria; recusado em parte por isto). |
| **R6 — runtime C congelada** | Nenhuma mudança em `teko_rt.{c,h}` além do acessor que A1 já expôs. |
| **R7 — fixpoint** | gen2==gen3 é determinismo; qualquer mudança determinística preserva-o. Detetor real = PARANOID + `teko test .` + o self-host sob ASan (o oráculo do agente). |
| **R8 — nova regra: zero `.tkp` novo** | Todas as fixtures são `.tkr`/entrypoints ADICIONADOS a um `.tkp` `mem_*` existente (§6). O regressor de ASan é o self-host, não um `.tkp`. |
| **R9 — nativo bloqueado no link `mem::*`** | A rota C (M2a0–M2c) valida-se INTEIRA por gen2==gen3, não bloqueada. O nativo herda quando o link fechar. Sequenciado. |

**Tensão de lei que force HALT: NENHUMA.** Tudo resolve via Constituição/Leis (M.1 nunca-UAF por LUB;
exceção de runtime C limitada ao acessor de A1; Teko-only; issue-100%; regra zero-`.tkp`-novo do
dono). **A DECISÃO entre Mecanismo 1 e 2 é do dono** — este documento recomenda o Mecanismo 1 com a
prova acima; a implementação aguarda o ruling.
