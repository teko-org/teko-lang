# A TRANSIÇÃO SEGURA — move-on-return (§5) + seletor N-níveis de slices (§4), C+nativo unificados — 0.3.1

Arquiteto, 2026-08-02. Ramo `cargo/0.3.1.0-move-on-return-arq` (de `origin/fix/union`).
Documento de DESENHO — nenhuma linha de produto. Regra do dono honrada: **proposta com arquivo:linha,
alarme só se provado.** Prova de não-UAF em CADA crumb (o alarme do dono é absoluto).

Complementa `docs/design/modelo-de-memoria-por-escopo-0.3.1.md` (§4 seletor N-níveis, §5 move-on-return,
§1 prova não-UAF por LUB). Aterra a TRANSIÇÃO que entrega os dois itens UAF-críticos que faltam,
unificando a disciplina de região-corrente entre a rota C e a nativa.

---

## 0. O estado medido (confirmação do achado honesto do implementador de C3)

Antes de desenhar, o que EXISTE hoje no vagão (`origin/fix/union`), com arquivo:linha:

| peça | estado medido | consequência |
|---|---|---|
| **`tk_region_enter`/`leave`/`current`** | JÁ no runtime — `teko_rt.c:1710` (`tk_region_current`, **`static`**), `:1719` (`enter`), `:1724` (`leave`), pilha `cur_regions[64]`/`cur_rsp` (`:1232-1233`), cap `TK_REGION_STACK_MAX 64` (`:1217`) | a primitiva C1 do `backend-memoria` **já aterrou**; NÃO reimplementar |
| **`tk_str_concat_r`** | JÁ no runtime — `teko_rt.c:158`, `teko_rt.h:486` | §9 do modelo satisfeito |
| **`tk_slice_push` (bare)** | **já mira `tk_region_current()`** — `teko_rt.c:3759` (`return tk_slice_push_r(…, tk_region_current())`); idem `tk_alloc` (`:1965`), `tk_slice_with_cap` (`:3840`) | o LANDMINE: uma alocação bare segue a corrente, não root fixo |
| **rota C emite enter/leave?** | **NÃO** — `grep -c 'tk_region_enter\|tk_region_leave' codegen.tks` = **0**. Só regiões NOMEADAS: `_tkfr` (`codegen.tks:9788-9827`), `_tkbr<n>` (`:5528`, `:7521`, `:8301`, `:8931`), roteadas por `tk_slice_push_r(…, frame)` (`:3729-3733`) | ∴ em C gerado `tk_region_current()==root` SEMPRE → bare == root == leak-seguro. Consistente HOJE |
| **`residence_plan` (o oráculo)** | EXISTE e computa tiers — `residence.tks:193` (`residence_plan`), `:166` (`residence_tier`), `:375` (`plan_return`) | mas **NINGUÉM o consome** (`grep residence_plan codegen.tks lower.tks` = 0) |
| **`#singleton` para bindings (C2b)** | threaded parse→ast→tast: `parse_stmt.tks:251,268` (`parse_binding_di`), `ast.tks:292`, `tast.tks:123` (`TBinding.di_kind`); o oráculo força `Root` (`residence.tks:167`) | falta só o CONSUMO (rotear a root no codegen) |
| **C3 parcial (str por escopo)** | codegen rota concat de str por `tk_str_concat_r(<enclosing>)` usando `cg_enclosing_region_expr` — NÃO via o plano | str já morre no escopo na rota C |
| **rota NATIVA (`lower.tks`)** | SEM ciclo de vida: `buf_ptr` → `tk_region_root()` fixo (`lower.tks:3392,3430-3437`); seletor de push só bare `tk_slice_push` (`:9451-9459`, *"every push is a root push"*). NÃO emite enter/leave | tudo cai na corrente==root → nada morre → o OOM ~15,8 GB |

**Os DOIS itens UAF-críticos que faltam, confirmados:**

**(1) Seletor N-níveis de SLICES (§4) — o acumulador self-append é EXCLUÍDO de block-local por
construção.** Prova no código: `binding_is_block_local` (`escape.tks:808`) compara
`count_reads_block(fn_body)` (total, `:814`) com `count_block_local_reads(block_body)` (inside, `:815`).
Para `xs = teko::list::push(xs, i)`: o total CONTA a leitura-base `xs` (arg0) — `count_reads_stmt` no
braço `TAssign` soma `count_reads_expr(a.value)` (`escape.tks:754`), e `a.value` é `push(xs,i)` que lê
`xs`. Mas `count_block_local_reads` no braço `TAssign => { }` (`escape.tks:790`) SALTA o assign inteiro
(contribui 0). Logo `total > inside` → **não block-local** → o slice roteia ao frame (`_tkfr`, via
`assign_routes_to_frame` `escape.tks:410`), nunca à região do bloco. O caso COMUM do acumulador é
excluído.

**(2) Move-on-return (§5) — inexistente.** Um `return` é posição de escape → nome no conjunto de
`fn_escaping_vars` → `binding_is_frame_local` devolve `false` (`escape.tks:364`) → `frame=""` → bare →
`tk_region_current()`==root → **leak-seguro, nunca movido**. O `_tkfr` já nasce de `tk_region_root()`
(`codegen.tks:9731`), não da corrente-do-caller. A rota C aloca retornos em ROOT com regiões nomeadas;
as alocações bare DEPENDEM de current==root ser leak-seguro. É o de maior risco.

---

## 1. O predicado self-append CIENTE-DE-SELF-APPEND para `escape.tks` (item 1)

### 1a. A correção mínima e cirúrgica

A exclusão vive num único sítio: `count_block_local_reads` (`escape.tks:781-800`) salta TODO `TAssign`
de topo-de-bloco (o guarda `outerVar = x`), o que também salta a leitura-base do self-append. A
correção: quando o assign é EXATAMENTE um self-append DO PRÓPRIO nome testado, contar as suas leituras
de RHS como leituras-inside seguras — idênticas ao que `count_reads_stmt` soma no total — em vez de
saltar. Assim os dois lados contam a mesma leitura-base e a equação `total == inside` fecha.

O predicado `assign_is_self_append` (`escape.tks:379`) e `tcall_is_list_push` (`:370`) JÁ existem e são
a MESMA forma que o roteamento consome (`assign_routes_to_frame`, `:410`) — reusá-los mantém checker e
codegen incapazes de divergir (o contrato `escape.tks:406-409`). A mudança de `count_block_local_reads`
(em Javadoc, W15, para a versão nova):

```teko
/**
 * count_block_local_reads — leituras de `name` estritamente DENTRO do bloco B que são SEGURAS de
 * libertar na aresta de saída de B. Toda leitura EXCETO (a) o valor-TAIL do bloco quando B rende um
 * valor (um valor de braço/if usado fora de B — a armadilha UAF), e (b) qualquer leitura que seja o
 * RHS de um ASSIGN de topo-de-bloco (podia escrever numa variável envolvente).
 *
 * A EXCEÇÃO CIENTE-DE-SELF-APPEND (§4): um assign de topo-de-bloco que seja EXATAMENTE um self-append
 * do PRÓPRIO `name` (`name = list::push(name, item)`) NÃO é uma fuga — o alvo da escrita é o próprio
 * acumulador block-local, não uma variável envolvente. As suas leituras de RHS (a base `name` mais o
 * item) são leituras-inside seguras e são CONTADAS, espelhando exatamente o que `count_reads_stmt`
 * soma no total — de outro modo o total contaria a leitura-base e o inside não, e o acumulador seria
 * eternamente excluído de block-local (o achado de C3). Um assign de QUALQUER outra forma (alvo
 * diferente, `ys = push(ys, name)`, `outer = name`) continua SALTADO — a fuga conservadora preservada.
 *
 * @param body      as statements do bloco B
 * @param name      o nome do binding testado
 * @param is_value  B rende um valor (o seu tail é escapante)?
 * @return          a contagem de leituras-inside seguras
 * @since 0.3.1
 */
fn count_block_local_reads(body: []TStatement, name: str, is_value: bool): i64
```

Corpo (a única linha que muda é o braço `TAssign`): onde hoje está `TAssign => { }`, passa a

```teko
        TAssign as a => {
            if a.name == name && assign_is_self_append(a) { n = n + count_reads_expr(a.value, name) }
        }
```

`count_reads_expr(a.value, name)` conta a base arg0 (1) mais quaisquer leituras de `name` no item —
IDÊNTICO ao que `count_reads_stmt(TAssign)` soma no total para um self-append (que é `AssignKind::Simple`,
logo `assign_has_target` é falso, logo o total é só `count_reads_expr(a.value)`, `escape.tks:754`). Os
lados casam exatamente.

### 1b. Prova de que o predicado NÃO classifica como block-local um slice que de facto ESCAPA

O único conjunto de leituras novamente admitido como inside-seguro são self-appends de `name` cuja
ESCRITA volta ao próprio `name`, todos lexicamente dentro de B. Cada uma das quatro portas de fuga
permanece FECHADA — prova por casos:

1. **Fuga de frame (o nome escapa da função).** `name ∈ fn_escaping_vars` → `binding_is_frame_local`
   devolve `false` (`escape.tks:364`) → `binding_is_block_local` corta na primeira linha (`:809`),
   ANTES de qualquer contagem. Inalterado.
2. **Leitura FORA de B.** `count_block_local_reads` só percorre `block_body`; a exceção só soma para
   self-appends lexicalmente dentro de B. Uma leitura (ou self-append) fora de B conta no `total` e não
   no `inside` → `total > inside` → não block-local. Inalterado.
3. **Valor-TAIL de B escapa.** O tail é excluído do inside (`:793`) → `total > inside`. Inalterado.
4. **Fluxo para outro alvo (`outer = name`, `ys = push(ys, name)`).** Não é self-append DE `name` →
   continua saltado no inside (`n += 0`) mas conta no total → `total > inside` → não block-local. A
   captura por um alias que sobrevive a B é rejeitada. Inalterado.

Logo cada leitura recém-admitida ocorre enquanto a região de B está VIVA (a self-referência do
acumulador cresce-o em-lugar; o buffer reside na região de B e morre na sua aresta de saída). Um UAF
exigiria um uso APÓS a morte da residência — impossível: todos os usos são inside-B. ∎ (É o teorema do
LUB do §1 aplicado ao acumulador: residência = região de B ⊒ todos os usos.)

**Interação com o loop aninhado (a subtileza de roteamento).** Um self-append DECLARADO no topo de B
mas EXECUTADO dentro de um `loop` dentro de B (`mut xs = empty(); loop { xs = push(xs,i) }; use(xs)`):
o predicado JÁ o classifica block-local hoje (o self-append aninhado cai no braço `_ → count_reads_stmt`
de `count_block_local_reads`, que conta o assign via `count_reads_block(l.body)`, casando o total). A
correção 1a alinha o caso de TOPO-de-bloco com este. **Mas a região de roteamento tem de ser a do
escopo DECLARANTE (B), nunca a do corpo do loop** — senão o buffer morreria por-iteração enquanto o
acumulador persiste (UAF). Isto é o coração do seletor N-níveis (§2), não do predicado.

---

## 2. O seletor N-níveis de slices — o roteamento à região do escopo DECLARANTE

O predicado (1) diz QUE o acumulador é block-local; o seletor diz PARA ONDE cresce. Hoje o `frame`
passado a `emit_list_push` é `_tkfr` (`codegen.tks:8708`, o self-append emitido por `emit_assign`) ou
`""`. O seletor `:3729-3733` já sabe rotear a QUALQUER região nomeada (`tk_slice_push_r(…, frame)`).
Falta o `frame` correto: **a região do escopo onde o acumulador foi DECLARADO** (R_decl), não a moldura
nem a região-corrente-innermost.

**A regra sã (prova de dominância):** todo self-append e toda leitura do acumulador block-local estão
lexicalmente dentro do escopo declarante (é o que (1) provou). R_decl está aberta e DOMINA cada
self-append (todos estão dentro dela). ∴ rotear cada crescimento a R_decl vive tanto quanto todos os
usos → nunca UAF, e morre na saída de R_decl → recuperação por escopo. O buffer de iteração de um loop
interno cresce em R_decl (o bloco externo), que atravessa todas as iterações — correto.

**Como cada motor resolve R_decl (o mecanismo PARTILHADO, derivado do MESMO plano+TAST):** ambos os
motores percorrem a TAST na MESMA pré-ordem de escopos, abrindo uma região por escopo. No sítio da
DECLARAÇÃO de um binding `Scope`-tier (o plano diz o tier, `residence.tks:176`), o motor regista
`nome → região-do-escopo-corrente`. Um self-append de `nome` consulta esse mapa e roteia a essa região.
Como ambos percorrem a mesma árvore na mesma ordem, concordam por construção (a UNIFICAÇÃO — nenhuma
divergência C-vs-nativo possível). Nenhuma mudança estrutural no `ResidencePlan`: o tier `Scope` +
o mapa nome→região-declarante (função determinística da TAST) bastam.

- **Rota C:** o mapa é `nome → variável `_tkbr<n>`/`_tkfr` da RegionFrame aberta na declaração`. O
  `regions: []RegionFrame` (`codegen.tks:7955`) já é a pilha; regista-se o nome ao emitir o binding de
  topo do bloco. O self-append passa `frame = <_tkbr do escopo declarante>` em vez de `_tkfr`.
- **Rota nativa:** o mapa é `nome → handle da região entrada (`tk_region_enter`) no escopo declarante`.
  O push bare segue a corrente; para um self-append cujo R_decl NÃO é a corrente (loop interno), o motor
  emite o crescimento com a região declarante explícita — via `tk_slice_push_r` (o nativo GANHA o
  seletor `_r`, hoje ausente `lower.tks:9451`) OU um bracket `enter(R_decl)`/`leave` em torno do push.
  Recomendação: expor `tk_slice_push_r` ao nativo (aditivo, mesmo símbolo do runtime) — mais barato que
  um par enter/leave por push.

**Fallback conservador de primeira entrega (sem mapa completo):** se um motor ainda não resolve R_decl
num sítio (ex.: antes do mapa nome→região aterrar), roteia a `_tkfr` (frame) — leak-seguro (frame ⊒
bloco ⊒ usos), NUNCA UAF. A precisão bloco-exata é aditiva sobre este chão seguro.

---

## 3. A TRANSIÇÃO do move-on-return SEM janela de UAF (item 2 — o de maior risco)

### 3a. A tensão exata, nomeada

A rota C aloca retornos em ROOT com regiões nomeadas; as alocações **bare** (`tk_slice_push`/`tk_alloc`,
`frame=""`) DEPENDEM de `tk_region_current()==root` ser leak-seguro (`teko_rt.c:3759`). Adotar a
disciplina enter/leave incrementalmente redireciona-as silenciosamente para regiões que morrem → UAF.
A resolução é uma ORDEM de dois crumbs que separa "nomear todo escapante" de "flip da conveyance",
de modo que no primeiro NADA morre cedo e no segundo NADA escapante está bare.

### 3b. O contrato de conveyance PARTILHADO (C e nativo, uma vez)

O único canal para a região-do-caller é a pilha de região-corrente (thread-local, SEM parâmetro → ABI
intocada, design §5(A), R3). O contrato que ambos os motores realizam:

- **R_ret (região de retorno)** = `tk_region_current()` capturada na ENTRADA da função = a região que o
  caller escolheu para o resultado desta chamada. É para onde os `Caller`-tier movem.
- **F (região de frame)** = `tk_region_new(R_ret)` — filha do caller. Torna-se a corrente do CORPO
  (`tk_region_enter(F)`) — assim as chamadas que a função FAZ depositam os seus resultados em F por
  omissão (o resultado de um callee vira um local desta função, salvo re-retorno).
- **No RETURN:** ANTES de avaliar a expressão retornada, `tk_region_leave()` (corrente := R_ret). A
  expressão retornada aloca então em R_ret = a região do caller → o MOVE. Depois `tk_region_drop(F)`;
  `return`. Um valor JÁ construído como `Caller`-tier já reside em R_ret (roteado na construção) — o
  leave-antes-do-return só garante que uma tail-call retornada deposite o SEU resultado em R_ret.

**Prova de composição transitiva (N frames):** `a()` retorna `b()` retorna `value`.
`a` entra: `R_ret_a = current` (a região do caller de `a`), `enter(F_a)`. No `return b()`: `a` faz
`leave` → corrente = `R_ret_a`; chama `b` com corrente `R_ret_a`. `b` entra: `R_ret_b = current =
R_ret_a`, `enter(F_b)`. No `return value`: `b` faz `leave` → corrente = `R_ret_b = R_ret_a`; `value`
(marcado `Caller`, construído em `R_ret_b`) já lá está; `drop(F_b)`; retorna. O valor bolha por N
retornos e aterra DIRETO na região do consumidor de topo — cada frame deixou a sua antes de avaliar o
retorno. `R_caller ⊒ R_callee` sempre (a disciplina de pilha: F do callee nasce DEPOIS e é largada
ANTES do controlo voltar) → o move aloca numa região que vive MAIS que a origem → o INVERSO do UAF. ∎

### 3c. A ordem de dois crumbs (rota C) — não-UAF em CADA passo

**Crumb M1 — nomear todo escapante, com `_tkrr := tk_region_root()` LITERAL.** Onde o plano dá um
binding/return `Caller`-tier, introduz na entrada `tk_region *_tkrr = tk_region_root();` e roteia as
alocações desses valores por `tk_slice_push_r(…, _tkrr)` / `tk_str_concat_r(_tkrr, …)` em vez de bare.
Como `_tkrr == root`, o comportamento de runtime é IDÊNTICO ao leak-atual (nada morre cedo). **Prova
não-UAF:** nenhuma residência aperta abaixo de root neste crumb; `root ⊒ tudo` (a segurança do modelo
antigo, §1). O que muda: ZERO alocações bare-escapantes permanecem — TODO sítio escapante nomeia `_tkrr`
(ou root/programa). **Alocações que mudam de destino:** nenhuma em runtime (root→root); só o TEXTO
emitido (`_r` nomeado). **Gate:** `teko test .` verde; FIXPOINT gen2==gen3 (determinístico);
TEKO_MEM_PARANOID limpo (ainda a vazar para root = seguro).

**Crumb M2 — o flip da conveyance (o único ponto onde leak→move).** (i) `_tkrr` passa de
`tk_region_root()` para `tk_region_current()`; (ii) o pai de `_tkfr` passa de `tk_region_root()`
(`codegen.tks:9731`) para `_tkrr`; (iii) `tk_region_enter(_tkfr)` após abrir F; (iv) `tk_region_leave()`
antes de avaliar cada expr de retorno E antes de cada `tk_region_drop(_tkfr)`, balanceado em TODAS as
arestas (return/fall-through — o drop já é emitido em todas, `:9827`/`:9809`). **Alocações que mudam de
destino, e por que continuam dominadas:**
  - **`Caller`-tier (já nomeadas `_tkrr` em M1):** `_tkrr` resolve agora à região REAL do caller (porque
    os callers entram as suas F). Movem-se: morrem com o caller em vez de vazar. Dominadas: `R_caller ⊒`
    todos os usos do valor retornado (§1 LUB; o valor deixa de ser usado no callee após o `return`).
  - **bare remanescentes (só NÃO-escapantes):** flipam root→F. Dominadas: um valor não-escapante tem
    TODOS os usos dentro da função → `F ⊒ usos`. (Se escapasse, estaria no conjunto de
    `fn_escaping_vars` → nomeado em M1, não bare.)
  - **`_tkbr<n>` (bloco):** filhas de F, inalteradas (nomeadas, ignoram a corrente) — morrem na saída do
    bloco, usos dentro do bloco.
  **A ÚNICA via de UAF em M2:** um valor escapante que a análise de escape FALHASSE em marcar → ficaria
  bare → flip para F → morre no return → UAF. Isto é IMPOSSÍVEL sob a análise de escape SÃ existente
  (`escape.tks:9-12`, conservadora: dúvida → escapante). Essa sanidade é a precondição load-bearing de
  M2 — citada, não assumida. **Gate:** FIXPOINT gen2==gen3; TEKO_MEM_PARANOID limpo; TEKO_ARENA_OBS
  scoped>0; fixtures `mem_move_return`/`mem_move_transitive` (valor usado após a chamada — se ficasse na
  F do callee seria valor-corrompido, não só leak).

**Lever de de-risking (opcional):** M2 pode partir-se em M2a (só `enter/leave` + pai=`_tkrr`, mas
`_tkrr` fica `tk_region_root()`) e M2b (`_tkrr := tk_region_current()`). M2a isola o flip "bare→F" do
flip "move"; cada um gate-ável em separado. Recomendo a partição só se M2 acender PARANOID — senão o par
M1+M2 já não tem janela (M1 sem morte-cedo; M2 com todo escapante nomeado).

### 3d. Por que a rota C NÃO abandona as regiões nomeadas

O contrato mantém `_tkfr`/`_tkbr<n>` (precisão que a rota C já tem) E adota enter/leave só para a
CONVEYANCE da região-do-caller (o único canal para R_ret). Frame/bloco-locais continuam nomeados
(ignoram a corrente); só `Caller`-tier e as bare-remanescentes usam a corrente. Isto responde à pergunta
do dono "migrar para enter/leave OU fazer move-on-return sem abandonar as nomeadas": **ambas** — nomeadas
para frame/bloco, enter/leave para o move. A conveyance da região-do-caller é irredutivelmente
current-stack (sem ABI nova); as nomeadas ficam para o que é local.

---

## 4. A UNIFICAÇÃO C + nativo (C4) — a MESMA disciplina, um plano

O `ResidencePlan` (`residence.tks:144`) é o artefacto ÚNICO; ambos os motores consomem os MESMOS `pub`.
O que é PARTILHADO vs. o que cada motor lowera à sua maneira:

| aspeto | PARTILHADO (uma vez) | rota C (`codegen.tks`) | rota nativa (`lower.tks`) |
|---|---|---|---|
| decisão de residência | `residence_plan`/`residence_tier` (o oráculo) | lê o tier | lê o MESMO tier |
| R_decl (seletor N-níveis) | tier `Scope` + mapa nome→região-declarante (função da TAST, mesma pré-ordem) | `frame = _tkbr` do escopo declarante | `tk_slice_push_r(…, handle)` do escopo declarante |
| conveyance R_ret/F | contrato §3b (current-stack) | `_tkrr=current`, `_tkfr` nomeado, enter/leave | `_tkrr=current`, F entrada, enter/leave |
| frame/bloco-local | — | regiões NOMEADAS `_tkfr`/`_tkbr` | corrente (bare push) + `enter(child)` por escopo |
| símbolos de runtime | `tk_region_new/enter/leave/drop/current`, `tk_slice_push_r`, `tk_str_concat_r` | emitidos como TEXTO-C | emitidos como `call_inst` |

**A única adição de runtime (C MANTIDA, exceção ao congelamento):** um acessor PÚBLICO da corrente. Hoje
`tk_region_current` é `static` (`teko_rt.c:1710`). Expor `tk_region *tk_region_current(void)` (des-static
+ protótipo em `teko_rt.h`) — ambos os motores emitem chamada ao MESMO símbolo para capturar R_ret. Twin
opcional `uint64_t tk_region_current_u(void)` (como `tk_region_root_u`, `teko_rt.c:1736`) se o nativo
preferir o handle-u64. Aditivo, comportamento-idêntico.

**C4 (nativo) herda o ciclo de vida (`lower.tks`):** na entrada de escopo `{}` com locais emite
`region_enter(tk_region_new(<corrente>))`; na saída `region_leave` + `tk_region_drop`; no loop, o
enter/leave POR ITERAÇÃO (a arena da iteração larga na saída de cada volta, §4/§7 — nunca achatada);
`buf_ptr` (`lower.tks:3392`) e o push default passam a mirar a corrente em vez de `tk_region_root()`
fixo; o `return` segue o contrato §3b (leave antes da expr). Valida quando o LINK NATIVO fechar (`mem::*`
em curso por outro agente) — não bloqueia o design; sequencia (a rota C valida-se por gen2==gen3 já).

---

## 5. Crumbs ordenados (colisões + rituais)

Seed = o `teko` lançado; a primitiva de runtime precede o uso (já aterrada). Colisões nos ficheiros
quentes: `escape.tks`, `codegen.tks`, `lower.tks`. Fixtures ASSERTAM STDOUT (`.tkr`, `Then stdout
pattern = "…"`).

**A0 — [FEITO] commit vazio + push.** Proteção contra restart.

**A1 — Runtime: acessor público da corrente.** `teko_rt.{c,h}` (C MANTIDA). Des-static
`tk_region_current` + protótipo; twin `tk_region_current_u` se preciso. **Colisão:** nenhuma (aditivo).
**Gate:** builda; `teko test .` verde; sem chamadores ⇒ FIXPOINT trivial. **Ritual:** NÃO.

**A2 — Checker: predicado self-append em `escape.tks`.** A correção 1a a `count_block_local_reads`
(§1). Predicado-only; muda a classificação de acumuladores não-escapantes de Frame→Scope. **Prova:**
§1b (as quatro portas de fuga fechadas). Sozinho, a mudança abre `_tkbr` para mais bindings mas o
roteamento do buffer ainda vai a `_tkfr` (assign_routes_to_frame inalterado) → o `_tkbr` liberta só o
header/outros locais, o buffer sobrevive em `_tkfr` → leak-seguro, nunca UAF. **Colisão:** `escape.tks`
(spine/escape agentes). **Gate:** FIXPOINT gen2==gen3; TEKO_MEM_PARANOID limpo. **Ritual:** SIM.

**A3 — Rota C: seletor N-níveis (roteamento a R_decl).** `codegen.tks` mantém o mapa nome→região-
declarante ao emitir bindings `Scope`-tier de topo-de-bloco; o self-append passa `frame = <_tkbr
declarante>` (§2). Fallback a `_tkfr` onde R_decl não resolve (leak-seguro). **Colisão:** `codegen.tks`
(coordenar com `arena-escopo-local`). **Gate — RITUAL:** `teko test .` verde; FIXPOINT gen2==gen3;
TEKO_ARENA_OBS scoped>0; TEKO_MEM_PARANOID limpo; fixtures `mem_accum_block`, `mem_loop_per_iter`.
**Ritual:** SIM.

**A4 — Rota C: move-on-return M1 (nomear escapante, `_tkrr := root`).** §3c. **Prova:** root ⊒ tudo,
nada morre cedo. **Colisão:** `codegen.tks`. **Gate — RITUAL:** FIXPOINT gen2==gen3; TEKO_MEM_PARANOID
limpo (a vazar para root); comportamento idêntico. **Ritual:** SIM.

**A5 — Rota C: move-on-return M2 (flip da conveyance).** §3c. O único ponto leak→move. **Prova:** todo
escapante nomeado em M1; a análise de escape sã fecha a via de UAF. **Colisão:** `codegen.tks`. **Gate —
RITUAL:** FIXPOINT gen2==gen3; TEKO_MEM_PARANOID limpo; TEKO_ARENA_OBS regiões-largadas ≈ nº escopos com
locais; fixtures `mem_move_return`, `mem_move_transitive`, `mem_no_root_leak`. **Ritual:** SIM.

**A6 — Rota C: consumir o plano para `#singleton` (fechar C2b).** Um binding `#singleton` → `Root` já é
computado (`residence.tks:167`); o codegen roteia-o a `tk_region_root()` (reusa `di_scope_expr`,
`codegen.tks:9564`), lido de novo após o escopo declarante fechar. **Colisão:** `codegen.tks`. **Gate:**
fixture `mem_singleton_root` (valor lido após o `{}` fechar — prova que não morreu). **Ritual:** SIM.

**A7 — Rota NATIVA: herdar o ciclo de vida (`lower.tks`), C4.** §4: enter/leave por escopo, `buf_ptr`/
push à corrente, `tk_slice_push_r` exposto ao nativo, `return` pelo contrato §3b. **Colisão:** `lower.tks`
(muito quente — coordenar `backend-memoria`, `all-legs-native-map`). **Gate — RITUAL, quando o link
nativo fechar:** gen2 `TEKO_BACKEND=native`; `teko test .` verde; **FIXPOINT gen2==gen3 BINÁRIO**;
TEKO_ARENA_OBS scoped>0, arenas VIVAS ≈ profundidade de aninhamento (não o total executado, §7);
TEKO_MEM_PARANOID limpo. **Ritual:** SIM (ao fechar o link).

Ordem de risco: A1→A2 (checker, baixo)→A3 (seletor slice, médio)→A4→A5 (move, alto, gate duplo)→A6→A7
(nativo, ao fechar o link). A rota C (A1–A6) valida-se INTEIRA por gen2==gen3 SEM o link nativo.

---

## 6. Fixtures (ASSERTAM STDOUT — `.tkr`, gen2; A7 sob gen2 native)

Três já existem (`examples/regressions/mem_block_dies`, `mem_singleton_root`, `mem_str_scope`, de C3).
Adicionar:

| fixture | o que prova | stdout |
|---|---|---|
| `mem_accum_block` | acumulador self-append `xs = push(xs,i)` no topo de um `{}` roteia à região do bloco e morre na saída (churn N ciclos que netem a zero; corrupção = valor errado, não só leak) | soma conhecida |
| `mem_loop_per_iter` | acumulador declarado fora, crescido num loop interno: roteia a R_decl (bloco externo), NÃO à arena da iteração; pico plano ao longo de N iterações (recuperação por iteração, não achatada) | soma / linha de pico |
| `mem_move_return` | valor construído no callee e retornado é MOVIDO para o caller (usado após a chamada; se ficasse na F do callee seria UAF/valor-corrompido) | valor conhecido |
| `mem_move_transitive` | valor retornado por N frames aninhados (move N vezes; aterra na região do consumidor de topo) | valor conhecido |
| `mem_no_root_leak` | corpo com só locais e retornos; TEKO_ARENA_OBS scoped>0 e unresolved=0 (nada em root exceto o frame do main) | linha de contagem |
| `mem_accum_return` | acumulador construído num bloco e RETORNADO: o predicado (1) NÃO o classifica block-local (escapa via return) → `Caller`-tier → move, não morre no bloco (o caso-armadilha do §1b porta 2/4) | soma conhecida |

`mem_accum_return` é o detector-chave de que o predicado self-append não sobre-classifica: um acumulador
que ESCAPA via return tem de continuar excluído de Scope e ser movido, não libertado no bloco.

---

## 7. Assinaturas Teko que o implementador adiciona/toca (full Javadoc — copiar verbatim)

O `ResidencePlan`/`ResidenceTier`/`residence_plan`/`region_enter`/`region_leave` já existem
(`residence.tks`, e §14 do modelo). A transição adiciona/toca:

```teko
/**
 * scope_region_of — o mapa nome→região-declarante que realiza o seletor N-níveis (§2). Registado ao
 * emitir um binding `Scope`-tier de topo-de-bloco; consultado por um self-append de `name` para
 * rotear o crescimento à região do escopo DECLARANTE (R_decl), não à corrente-innermost (o buffer de
 * um acumulador crescido num loop interno tem de viver na região do bloco externo, senão morre
 * por-iteração — UAF). Determinístico sobre a TAST: ambos os motores o computam na mesma pré-ordem,
 * logo concordam por construção (a unificação C-vs-nativo, sem estrutura nova no plano).
 *
 * @param frames  a pilha de escopos abertos (RegionFrame na rota C; handles entrados no nativo)
 * @param name    o nome do acumulador self-append
 * @return        a região do escopo declarante de `name`, ou o frame `_tkfr` se não resolvida
 *                (fallback leak-seguro: frame ⊒ bloco ⊒ usos)
 * @since 0.3.1
 */
fn scope_region_of(frames: []RegionFrame, name: str): str
```

E a adição de runtime (C MANTIDA, `teko_rt.{c,h}`): `tk_region *tk_region_current(void)` (des-static +
protótipo) — o acessor público que ambos os motores emitem para capturar R_ret na entrada da função.

**Funções existentes que a transição TOCA:** `escape.tks` `count_block_local_reads:781` (a exceção
self-append, §1a), reusando `assign_is_self_append:379`; `codegen.tks` `emit_list_push:3704` (o `frame`
passa a ser R_decl, §2), `emit_function_cov:9769`/`:9788-9827` (`_tkrr`, `_tkfr` pai=`_tkrr`,
enter/leave, leave-antes-do-return, §3c), `cg_frame_region_parent_expr:9730` (pai passa a
`tk_region_current()`), `di_scope_expr:9564` (`#singleton`→root, A6); `lower.tks` `lower_buf_ptr_call:3423`
(corrente em vez de root fixo), o seletor de push `:9451` (ganha `tk_slice_push_r`), e o lowering de
entrada/saída de escopo + return (o ciclo de vida ausente, A7); `residence.tks` `residence_plan:193`
(o oráculo, passa a ser CONSUMIDO — inalterado estruturalmente).

---

## 8. Riscos, tensões de lei, pergunta ao dono

| risco / tensão | resolução (law-first) |
|---|---|
| **R1 — o flip enter/leave redireciona bare-escapantes → UAF** | A ordem M1(nomear, `_tkrr=root`)→M2(flip): M1 não mata cedo; M2 só flipa com todo escapante já nomeado. A via residual (escape não-marcado) é fechada pela análise de escape SÃ existente (`escape.tks:9-12`). Sem janela. |
| **R2 — self-append sobre-classifica um slice que escapa** | §1b: as quatro portas de fuga permanecem fechadas; só self-appends de topo-de-bloco DO PRÓPRIO nome, todos inside-B, são admitidos. Fixture `mem_accum_return` é o detector. |
| **R3 — loop interno: buffer morre por-iteração** | Rotear a R_decl (escopo declarante), não à corrente-innermost (§2). Fallback `_tkfr` leak-seguro onde R_decl não resolve. Fixture `mem_loop_per_iter`. |
| **R4 — ABI congelada vs. move** | R_ret = corrente thread-local, SEM parâmetro (§3b, design R3). ABI intocada. |
| **R5 — runtime C congelada** | `teko_rt.{c,h}` é exceção explícita. Só des-static de `tk_region_current` + protótipo — aditivo, comportamento-idêntico. |
| **R6 — colisão nos quentes** | `escape.tks`(A2)/`codegen.tks`(A3–A6)/`lower.tks`(A7) separados por crumb; coordenar `arena-escopo-local`, `backend-memoria`, `all-legs-native-map`. |
| **R7 — FIXPOINT quebra** | gen2==gen3 é auto-consistência (determinismo), não gen2==gen1 — qualquer mudança determinística mantém-no. O detector real é PARANOID + `teko test .`. |
| **R8 — nativo bloqueado no link `mem::*`** | A rota C (A1–A6) valida-se INTEIRA por gen2==gen3, não bloqueada. A7 valida quando o link fechar. Sequenciado, não bloqueante. |

**Tensão de lei residual: NENHUMA que force HALT.** Tudo resolve via Constituição/Leis (M.1 nunca-UAF
preservado por LUB; exceção de runtime C; Teko-only; issue-100%). O move-on-return e o seletor N-níveis
fecham sem a superfície cross-thread (que não existe).

**A única pergunta ao dono (não-bloqueante), se surgir tensão genuína no nativo:** o seletor N-níveis do
nativo (§2) prefere `tk_slice_push_r` exposto (uma adição de mapeamento de símbolo) OU um bracket
`enter(R_decl)`/`leave` por push (sem símbolo novo, mais instruções)? Recomendação: `tk_slice_push_r`
(mais barato, paridade exata com a rota C). Não bloqueia o design — o fallback `_tkfr` cobre até decidir.
