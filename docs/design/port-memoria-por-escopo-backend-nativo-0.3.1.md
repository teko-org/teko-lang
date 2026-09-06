# PORT do modelo de memória POR ESCOPO para o backend NATIVO (`lower.tks`) — 0.3.1

> **DESIGN-AHEAD — a implementação AGUARDA o drain do 5º-gap/FFI em `lower.tks`.**
> Nenhuma linha de produto foi tocada. Nenhum bump, nenhum PR. Este documento é o mapa executável
> do port; um implementer retoma em minutos quando as duas branches que hoje tocam `lower.tks` —
> o front do 5º gap (`cargo/0.3.1.0-native-agg-slice-box`, que reescreve exatamente
> `own_returned_value`/`box_aggregate_value_at`/`returned_aggregate_box_bytes`/`lower_list_push`)
> e a de FFI — drenarem no vagão. **O que fica BLOQUEADO até o drain é apenas a EDIÇÃO de `lower.tks`**
> (ficheiro quente, colisão garantida); tudo abaixo — o desenho, os contratos contra a forma
> DECLARADA das dependências, as fixtures, as assinaturas em Javadoc, a adição de runtime — está
> pronto e compila contra a árvore de hoje.

Arquiteto, 2026-08-02. Ramo `fix/union`. Regra do dono honrada: **proposta com arquivo:linha; alarme
só se provado; prova de não-UAF em CADA crumb.**

Complementa (é a expansão INTEGRAL do degrau **A7/C4** de) `docs/design/transicao-move-on-return-e-seletor-n-niveis-0.3.1.md` (o §4 unificação C+nativo, o contrato de conveyance §3b), `docs/design/fechamento-seguro-m2-move-on-return-0.3.1.md` (o Mecanismo 1 — brackets `leave/enter` — recomendado, e o ladder M2a0→M2b) e `docs/design/modelo-de-memoria-por-escopo-0.3.1.md` (a regra de residência do dono, §0; a prova não-UAF por LUB, §1). **O modelo é de LINGUAGEM, não de rota:** o nativo reusa os MESMOS primitivos de runtime que a rota C, não uma segunda disciplina.

---

## 0. O estado medido do NATIVO hoje (`origin/fix/union`), com arquivo:linha

O backend nativo NÃO tem ciclo de vida de região por escopo. Tudo vaza para a task-root. Medido:

| peça | estado medido (arquivo:linha) | consequência |
|---|---|---|
| **frame region (`_tkfr`)** | INEXISTENTE. `lower_function` (`lower.tks:13120`) monta o `LowerCtx` (`:13130`) e lowera o corpo (`lower_fn_body:12914`) SEM abrir região de frame, SEM `tk_region_enter`, SEM `tk_region_leave`/`drop` no retorno | nenhum scratch de função morre; tudo persiste na corrente |
| **região por bloco/loop/arm** | INEXISTENTE. `lower_block` (`:12863`) itera statements; `lower_block_value` (`:7178`) idem; o loop (`close_loop_body:6986`) não abre/larga região por-iteração | nada morre na aresta de escopo léxico |
| **`buf_ptr`** | roteado a `tk_region_root()` FIXO — `lower_buf_ptr_call` (`:3435-3451`), doc admite: *"the enclosing region may not be directly accessible at lowering time for the native backend (unlike C codegen, where `cg_enclosing_region_expr` tracks the open region frame stack)"* (`:3402-3405`) | toda alocação de buffer bare cai em root |
| **slice push** | só o bare `tk_slice_push`/`tk_slice_push_fo` — `slice_push_symbol` (`:9758`); a doc declara *"The region-scoped third variant (`tk_slice_push_r`) is NOT [emittable]"* (`:9752`) | o crescimento segue `tk_region_current()` (== root, pois nunca se entra) |
| **retorno de agregado** | `own_returned_value` (`:10100`) COPIA o agregado para storage próprio via `box_aggregate_value_at` (`:10027`) → `tk_slice_elem_box` (`teko_rt.c:3911`), largura por `returned_aggregate_box_bytes` (`:10078`) | **este é o território EXATO do 5º gap** — a cópia de retorno já existe, mas o seu destino (`tk_alloc`→`tk_region_current()`) é root |
| **primitivas de current-stack** | JÁ no runtime: `tk_region_enter`/`leave` (`teko_rt.h:198-199`), u-twins `tk_region_new_u`/`root_u`/`drop_u`/`enter_u` (`:205-208`). **`tk_region_current` é `static`** (`teko_rt.c:1846`); NÃO há `tk_region_current_u` público | falta só o acessor da corrente para o nativo capturar R_ret (a A1 da rota C, no dialecto u64) |
| **helpers bare seguem a corrente** | `tk_slice_push`→`tk_slice_push_r(…, tk_region_current())` (`teko_rt.c:3902`); `tk_slice_elem_box`→`tk_alloc`→corrente (`:3911-3913`); `tk_alloc` idem | **o achado decisivo:** entrar/sair regiões faz TODAS estas helpers materializar na região certa SEM um único símbolo `_r` novo (a propriedade do Mecanismo 1, `fechamento-…:148-163`) |
| **escape info disponível?** | `checker::fn_escaping_vars(f)` é `pub` (`escape.tks:347`) e recebe uma `TFunction` — a MESMA que `lower_function` já tem em mão (`:13120`). Consumível DIRETO, como `collect_addr_taken_locals(f.body, …)` já é (`:13137`) | **o nativo NÃO precisa de nova análise nem de threading cross-módulo** — lê o MESMO oráculo que a rota C |

**Conclusão do estado:** o nativo está no degrau **pré-C1** face ao modelo de linguagem. A rota C já ganhou C3 (residência de str, `c7d6d9e1`) e desenha A1–A6/M1–M2. O port replay a MESMA escada DENTRO de `lower.tks`, emitindo LIR (`call_inst`/`void_call_inst`) onde a rota C emite texto-C, consumindo o MESMO `fn_escaping_vars` e as MESMAS primitivas de runtime.

---

## 1. O mapa do port — o que o nativo passa a emitir (espelho degrau-a-degrau da rota C)

A regra de residência do dono (INEGOCIÁVEL, `modelo-…:§0`) que o port tem de satisfazer no nativo, idêntica à rota C:

- **escapante (return)** → arena do CALLER (MOVE);
- **não-escapante em QUALQUER escopo léxico** (bloco `{}` / loop-iter / braço if / braço when / corpo fn) → morre no fim do escopo;
- **só wide** (`wait_group`, `chan<>`, `#singleton`) → root; `#arena_depth` default = 1;
- **NUNCA** vazamento permanente em root como "conserto".

O mapa PARTILHADO vs. o que cada rota lowera à sua maneira (o §4 da transição, instanciado para o nativo):

| aspeto | PARTILHADO (o oráculo, uma vez) | rota C (`codegen.tks`, TEXTO) | rota NATIVA (`lower.tks`, LIR) |
|---|---|---|---|
| decisão de residência | `checker::fn_escaping_vars` (`escape.tks:347`), `residence_plan` (`residence.tks`) | lê o conjunto/tier | **lê o MESMO conjunto** (`fn_escaping_vars(f)` em `lower_function`) |
| região de frame | contrato §3b | `_tkfr` variável-C `= tk_region_new(_tkrr)`; `enter`/`leave` | **VReg** capturado de `tk_region_new_u(rr_vreg)`; `enter`/`leave` como `void_call_inst` |
| R_ret / conveyance | corrente thread-local (SEM parâmetro, ABI intocada) | `_tkrr = tk_region_current()` | `_tkrr` VReg `= tk_region_current_u()` (o único símbolo novo, §3) |
| escopo léxico (bloco/loop/arm) | tier `Scope` | região NOMEADA `_tkbr<n>` (`emit_block_region:8288`) | `enter(tk_region_new_u(cur))` na entrada; `leave`+`drop_u` na saída |
| seletor N-níveis (self-append em loop interno) | R_decl = região do escopo DECLARANTE | `frame = _tkbr` declarante | **VReg da região declarante**, do mapa `region_stack` no `LowerCtx` |
| move-on-return | leave-antes-do-return + brackets por RHS escapante retida (Mec. 1) | `leave()` antes da expr; brackets `enter(_tkrr)…leave()` | `leave` antes de `own_returned_value`/box; brackets idem |
| `#singleton` | tier `Root` | `di_scope_expr → tk_region_root()` (`codegen.tks:9564`) | `tk_region_root_u()` explícito no binding |
| símbolos de runtime | `tk_region_new/enter/leave/drop/current`, helpers-corrente | texto-C | `call_inst`/`void_call_inst` (MESMOS símbolos) |

**O ponto que fecha a divergência:** ambos os motores percorrem a MESMA TAST na MESMA pré-ordem de escopos, abrindo uma região por escopo, lendo o MESMO `fn_escaping_vars`. Concordam por construção — a unificação C-vs-nativo é estrutural, não um contrato a manter à mão.

---

## 2. As diferenças C-vs-nativo que EXIGEM cuidado

Estas são as quatro divergências estruturais que o implementer tem de tratar — o modelo é o mesmo, a MECÂNICA de emissão difere:

### 2a. O nativo emite OBJETO próprio (LIR), não texto-C

A rota C nomeia regiões por STRING (`_tkfr`, `_tkbr<len>`, `cg_enclosing_region_expr:8052` devolve o nome textual) e deixa o compilador-C resolver a variável. O nativo NÃO tem variáveis-C: uma região é um **VReg** que segura o handle u64 devolvido por `call_inst(vreg, "tk_region_new_u", …)`. Consequência: onde a rota C passa um `frame: str`, o nativo passa um `region_vreg: u32`. Todo sítio de alocação que a rota C rotearia por nome (`tk_slice_push_r(…, _tkbr)`) o nativo ou (i) usa a corrente (entra a região certa e deixa a helper bare segui-la — a via preferida, zero símbolo novo) ou (ii) passaria o VReg a um `tk_slice_push_r` explícito (via NÃO recomendada — ver §6, R-selector).

### 2b. A pilha de regiões tem de ser RASTREADA no `LowerCtx` (a rota C thread-a como parâmetro)

A rota C thread `regions: []RegionFrame` (`codegen.tks:8044`) por CADA função de emissão (dezenas de assinaturas). O nativo thread um ÚNICO `LowerCtx` imutável (`lower.tks:1039`) reconstruído por `ctx_with`/`ctx_with_rodata`/`ctx_with_lifted`/`ctx_with_defers`/`ctx_with_loops` (`:1108`/`:1129`/`:1150`/`:1171`/`:1192`). **O port adiciona DOIS campos ao `LowerCtx`** — a pilha de regiões abertas e o conjunto escapante da função corrente — e TEM de os propagar nos cinco `ctx_with*` E em cada literal de `LowerCtx` (o de `lower_function:13130` e os de `lower_test.tkt`). Este é o mesmo chore que o campo `table` já sofreu (`git 55c2c890`: *"LowerCtx literals … ganham o campo table (drenado)"*). É a fonte de colisão com o 5º gap: **ambas as branches editam `LowerCtx` e os `ctx_with*`.** Daí o bloqueio.

### 2c. A cópia de RETORNO já existe e é o território do 5º gap — o move ASSENTA sobre ela

A rota C aloca o retorno construindo-o na região certa (roteamento no sítio da construção). O nativo faz DIFERENTE: `own_returned_value` (`:10100`) já COPIA o agregado por `tk_slice_elem_box` (o box "NATIVE-AGG-SLICE-BY-ADDRESS", `:10061`) para storage que sobrevive ao frame. **Hoje esse box segue `tk_region_current()` == root → leak-seguro.** O move-on-return do nativo é, então, cirúrgico: fazer o box do retorno EXECUTAR com `current == _tkrr` (a região do caller). Basta emitir `tk_region_leave()` ANTES de `own_returned_value` (entre o `replay_defers` e o `own`, em `lower_return:5864-5871` e `lower_fn_body:12919-12921`). O box aterra em `_tkrr` → o MOVE, sem símbolo novo. **MAS `own_returned_value`/`box_aggregate_value_at`/`returned_aggregate_box_bytes` são EXATAMENTE o que o 5º gap reescreve** — por isso o crumb do move (NP6) assenta DEPOIS do drain, sobre a forma final do box. O retorno FAT (`lower_return_fat:5890`, `lower_fn_body_fat:12941`) tem o mesmo tratamento: o seu ptr vem dos builders de str/slice que TAMBÉM seguem a corrente, logo o mesmo `leave`-antes basta para os mover.

### 2d. O balanceamento por-aresta reusa o sítio de `replay_defers`, não um `DeferCtx`

A rota C desfaz brackets/drops por-aresta via a máquina de `DeferCtx` (`codegen.tks:8077`, unwind em return/break/continue/fall-through). O nativo já replaya defers exatamente nessas arestas: `lower_return` (`:5864`), `close_function` (`:13192`), o break/continue (`find_loop_target` + `close_loop_body:6986`). **O `leave`/`drop` de região e o desfazer-brackets do nativo piggyback nesses MESMOS sítios** — um contador de profundidade-de-região no `LowerCtx` (twin do `dctx` de brackets abertos, `fechamento-…:388`) é desenrolado onde o `replay_defers` já corre. Nenhuma máquina de unwind nova; a existente ganha um segundo eixo (regiões, além de defers). `tk_region_leave` é auto-guardado contra underflow (`teko_rt.c` — o "over-deep enter é contado mas não armazenado", `teko_rt.h:196`), a rede de segurança; o balanceamento explícito é o contrato.

---

## 3. A ÚNICA adição de runtime (C MANTIDA — exceção ao congelamento, aditiva)

O nativo captura R_ret (a região que o caller escolheu para o resultado) na ENTRADA da função. Precisa do acessor u64 da corrente — o dialecto-nativo da A1 que a rota C fez (des-static de `tk_region_current`). Hoje há `tk_region_root_u` (`teko_rt.h:206`) mas NÃO `tk_region_current_u`.

Adição (aditiva, comportamento-idêntico, exceção explícita do congelamento em `teko_rt.{c,h}`):

```c
/* teko_rt.h — junto aos u-twins de :205-208 */
uint64_t   tk_region_current_u(void);   /* tk_region_current() as a handle (native captures R_ret) */
```
```c
/* teko_rt.c — junto a tk_region_root_u */
uint64_t tk_region_current_u(void) { return (uint64_t)(uintptr_t)tk_region_current(); }
```

`tk_region_current` (`teko_rt.c:1846`) fica `static` — o twin público chama-o internamente (não é preciso des-static como a rota C fez, porque o nativo só precisa do handle-u64). **Símbolo único, aditivo, sem chamador até NP6 ⇒ FIXPOINT trivial ao aterrar.** Nenhuma outra mudança de runtime: as helpers bare (`tk_slice_push`, `tk_slice_elem_box`, `tk_alloc`, builders de str/byte) já seguem `tk_region_current()` — entrar/sair regiões move-as automaticamente (§0, a linha "helpers bare seguem a corrente"). **Zero gémeos `_r` novos** — a propriedade decisiva do Mecanismo 1.

---

## 4. As adições ao `LowerCtx` (o rastreio da pilha, §2b)

Dois campos novos (Javadoc W15 — copiar verbatim), mais o tipo do frame:

```teko
/**
 * NativeRegionFrame — uma região léxica ABERTA na pilha de escopos do lowering nativo: o VReg que
 * segura o handle u64 da região (devolvido por `tk_region_new_u`) e, para o seletor N-níveis, o
 * nome do binding `Scope`-tier declarado no topo deste escopo cujo self-append tem de crescer AQUI
 * (não na corrente-innermost). Twin funcional do `RegionFrame` da rota C (`codegen.tks:8044`), com
 * o VReg no lugar do nome textual `_tkbr<n>` — a diferença C-vs-nativo do §2a.
 *
 * @param region_vreg  o VReg que segura o handle u64 desta região (de `tk_region_new_u`)
 * @param decl_name    o nome do acumulador `Scope`-tier declarado neste escopo, ou "" se nenhum
 * @param is_loop      esta região é o corpo de um `loop` (larga-se por ITERAÇÃO, não uma vez)?
 * @since 0.3.1
 */
type NativeRegionFrame = struct { region_vreg: u32; decl_name: str; is_loop: bool }
```

Campos adicionados a `pub type LowerCtx` (`lower.tks:1039`), com Javadoc no membro:

```teko
    /**
     * region_stack — a pilha de regiões léxicas ABERTAS (frame + blocos/loops/arms), da mais
     * externa (índice 0, o frame) à mais interna (o topo). A corrente de `tk_alloc` é o handle do
     * TOPO. `region_current_vreg` lê o topo; um self-append de um acumulador `Scope`-tier consulta
     * esta pilha por `decl_name` para rotear o crescimento à região DECLARANTE (o seletor
     * N-níveis, §1) em vez da corrente-innermost — senão o buffer morreria por-iteração num loop
     * interno (UAF). Twin do `regions: []RegionFrame` que a rota C thread por parâmetro
     * (`codegen.tks:8044`); aqui vive no `LowerCtx` porque o nativo thread um único contexto.
     */
    region_stack: []NativeRegionFrame
    /**
     * escaping — o conjunto `checker::fn_escaping_vars(f)` (`escape.tks:347`) da função CORRENTE,
     * computado UMA vez em `lower_function` (como `addr_taken`, `:13137`). O superconjunto
     * conservador dos nomes que escapam o frame; consumido por `binding_conveys_escape` para
     * decidir que RHS bracketar (`enter(_tkrr)…leave()`, o move-on-return via Mecanismo 1) e por
     * `own_returned_value` (o `leave`-antes-do-box). Lê o MESMO oráculo que a rota C — nenhuma
     * análise nativa própria, nenhuma divergência possível.
     */
    escaping: []str
```

**Chore de propagação (colide com o 5º gap — daí o bloqueio):** os cinco `ctx_with*` (`:1108`/`:1129`/`:1150`/`:1171`/`:1192`) ganham `region_stack = ctx.region_stack; escaping = ctx.escaping` na sua lista; o literal de `lower_function` (`:13130`) inicializa `region_stack = teko::list::empty(); escaping = checker::fn_escaping_vars(f)`; os literais de `lower_test.tkt` ganham os dois campos default. Este é o degrau NP2 — byte-idêntico na SAÍDA (campos threaded mas ainda não consumidos).

---

## 5. Sequência ORDENADA de crumbs (espelho do ladder C: fundação → seletor → move-on-return)

Seed = o `teko` lançado; a primitiva de runtime precede o uso. Prova por **FIXPOINT NATIVO** (gen2==gen3 BINÁRIO — o compilador nativo compila-se a si mesmo duas vezes e os binários batem) **+ `TEKO_MEM_PARANOID`** (o self-host sob ASan é o oráculo real de UAF). Marca-se o BYTE/COMPORTAMENTO-IDÊNTICO e o FLIP PERIGOSO. **Zero `.tkp` novo** — as provas reusam a família `mem_*` (§6).

> **BLOQUEIO:** NP2–NP7 editam `lower.tks` e colidem com o 5º gap (LowerCtx/box de retorno) e o FFI. NP1 (runtime) e NP0 podem aterrar ANTES do drain. A ORDEM abaixo é o que o implementer executa no instante do drain.

**NP0 — [housekeeping] branch de `origin/fix/union` após o drain; commit vazio + push** (proteção contra restart). **Ritual:** não.

**NP1 — Runtime: `tk_region_current_u()` público** (§3). `teko_rt.{c,h}` (C MANTIDA, aditivo). **Não bloqueado pelo 5º gap** (não toca `lower.tks`) — pode aterrar já. **Prova:** sem chamador ⇒ FIXPOINT trivial; `teko test .` verde. **Ritual:** não. **BYTE-IDÊNTICO** (nenhum emissor muda).

**NP2 — Plumbing: `LowerCtx` ganha `region_stack` + `escaping`** (§4). Os dois campos, os cinco `ctx_with*`, o literal de `lower_function`, os literais de `lower_test.tkt`. **NADA os consome ainda.** **Prova:** a SAÍDA emitida é idêntica ⇒ gen2==gen3 trivial; `teko test .` verde. **Colisão:** `lower.tks` LowerCtx/`ctx_with*` (o 5º gap). **Ritual:** não. **BYTE-IDÊNTICO na saída.**

**NP3 — Conveyance NEUTRALIZADA EM ROOT (o net de balanceamento — espelho de M2a0).** Em `lower_function`/`lower_fn_body`/`close_function` emitir o TEXTO-LIR completo da conveyance, mas ALIASADO a root:
- na entrada: `rr_vreg = call tk_region_root_u()` (alias, NÃO `current_u` ainda); `fr_vreg = call tk_region_new_u(rr_vreg)`; `void call tk_region_enter_u(fr_vreg)`;
- brackets `enter(rr_vreg) … leave()` à volta de cada RHS de binding/assign com nome ∈ `ctx.escaping` (`binding_conveys_escape`);
- `void call tk_region_leave()` antes de cada `own_returned_value`/box e antes de cada `drop`, balanceado em TODAS as arestas (return/break/continue/fall-through) via o contador de profundidade-de-região, nos sítios onde `replay_defers` já corre (§2d);
- MAS SUPRIMIR o `tk_region_drop_u(fr_vreg)` (a região de frame vive como filha de root que nunca larga = leak-seguro, comportamento == hoje: tudo em root).

**Prova não-UAF:** nenhuma residência aperta abaixo de root; `root ⊒ tudo` (§1 LUB). O que muda: o TEXTO de enter/leave/brackets existe e BALANCEIA. **Gate:** gen2==gen3 native; `teko test .` verde; `TEKO_MEM_PARANOID` limpo; `TEKO_ARENA_OBS` enters==leaves (pilha zerada no fim de cada função). **Colisão:** `lower.tks` (5º gap). **Ritual:** SIM. **COMPORTAMENTO-IDÊNTICO** (tudo em root, nada morre cedo — só o balanceamento é provado).

**NP4 — RECLAMA o scratch NÃO-ESCAPANTE (o gate DURO de completude dos brackets — FLIP PERIGOSO #1).** `fr_vreg = tk_region_new_u(rr_vreg)` filha REAL (parente == root, pois `rr_vreg` ainda é `root_u`); REATIVAR `tk_region_drop_u(fr_vreg)` nas arestas. `rr_vreg` FICA `root_u`.
- **Runtime:** o scratch NÃO-escapante (call-results não retidos em escape) nasce em `fr_vreg` e é reclamado na saída da função (a poupança COMEÇA aqui). O escapante (conduzido pelos brackets para `rr_vreg == root`) continua a vazar para root — destino INALTERADO.
- **É AQUI que o UAF do padrão `parse_function` APARECERIA sem brackets completos** (o repro que parou a rota C, `fechamento-…:29-48`): um valor escapante retido num local e embrulhado num struct de retorno, se sub-bracketado, cairia em `fr_vreg` e morreria no `drop` → UAF. Com `rr_vreg == root`, o raio de explosão é contido (escapante ainda em root; só scratch é novo a morrer) e o gate apanha uma incompletude.
- **Precondição load-bearing:** completude dos brackets (regra CEGA: toda RHS de nome ∈ `ctx.escaping` bracketada — sub-detetar seria UAF, sobre-bracketar é inócuo) + soundness da análise de escape (`escape.tks:9-12`, conservadora).
- **Interação com o 5º gap:** o `own_returned_value` (`:10100`) — reescrito pelo 5º gap — passa a ter o `leave`-antes-do-box AQUI (o box de um NÃO-escapante fica em `fr_vreg`; de um escapante, conduzido a root). **Gate:** gen2==gen3 native; `TEKO_MEM_PARANOID` limpo (self-host sob ASan — o oráculo); `TEKO_ARENA_OBS` scoped>0; fixtures `mem_move_retain_struct` (o padrão `parse_function`), `mem_move_return`, `mem_move_transitive`. **Ritual:** SIM. **PERIGOSO (UAF — completude dos brackets).**

**NP5 — Regiões por ESCOPO LÉXICO + seletor N-níveis (a fundação C1/C3 + o seletor A3, no nativo).** Espelho de `emit_block_region` (`codegen.tks:8288`): em cada `{}`/braço-if/braço-when/corpo-de-loop com locais NÃO-escapantes, `lower_block`/`lower_block_value`/o loop emitem, na entrada, `bl_vreg = tk_region_new_u(region_current_vreg(ctx))`; `void call tk_region_enter_u(bl_vreg)`; empurram um `NativeRegionFrame` no `region_stack`; na saída `void call tk_region_leave()` + `tk_region_drop_u(bl_vreg)` (loop: POR ITERAÇÃO, a arena da volta larga na saída de cada iteração — nunca achatada, `modelo-…:§4/§7`). `buf_ptr` (`lower_buf_ptr_call:3435`) passa a mirar `region_current_vreg(ctx)` em vez de `tk_region_root()` fixo. **Seletor N-níveis:** um binding `Scope`-tier de topo-de-bloco regista `decl_name` no `NativeRegionFrame` do escopo; um self-append de `name` roteia o crescimento à região DECLARANTE (`native_scope_region_of`) — como as helpers seguem a corrente, isto realiza-se ENTRANDO a região declarante à volta do push (bracket `enter(R_decl)…leave()`), NÃO por um `tk_slice_push_r` explícito (§6, R-selector; via preferida = zero símbolo novo). **Fallback conservador:** onde R_decl não resolve, roteia à corrente/frame (leak-seguro, frame ⊒ bloco ⊒ usos). **Prova não-UAF:** R_decl DOMINA cada self-append e cada leitura do acumulador (todos lexicamente dentro do escopo declarante) → o crescimento vive tanto quanto todos os usos, morre na saída de R_decl (§1 LUB). **Gate:** gen2==gen3 native; `TEKO_MEM_PARANOID` limpo; `TEKO_ARENA_OBS` scoped>0 e arenas VIVAS ≈ profundidade de aninhamento (não o total executado); fixtures `mem_accum_block`, `mem_loop_per_iter`. **Ritual:** SIM. **PERIGOSO (morte por escopo/iteração — o seletor é o guarda anti-UAF).**

**NP6 — O FLIP DO MOVE (`rr_vreg`: root → current — FLIP PERIGOSO #2).** `rr_vreg = call tk_region_current_u()` (o símbolo da NP1) na entrada; `fr_vreg = tk_region_new_u(rr_vreg)` (parente passa de root para `rr_vreg`).
- **Runtime:** o `leave()` dos brackets e do return agora expõem `rr_vreg == a região REAL do caller` (porque os callers entram os seus `fr`). Valores escapantes (conduzidos a `rr_vreg`) — incluindo o box de `own_returned_value` — materializam na região do caller → **MOVE** → reclamados com o caller em vez de vazar para root.
- **Não abre UAF novo:** o destino muda de root para `rr_vreg`, e `rr_vreg = current-na-entrada = a região que o caller escolheu ⊒ este frame ⊒ todos os usos neste frame`; o valor escapa para o caller que POSSUI `rr_vreg` → `R_caller ⊒ usos` (LUB). Ambos os destinos (root em NP4/NP5, `rr_vreg` em NP6) são seguros; NP6 só troca leak↑root por reclaim↑caller. A completude dos brackets já foi provada em NP4.
- **A ÚNICA via de UAF:** um escapante que a análise de escape FALHASSE em marcar → ficaria bare → flip para `fr` → morre no return → UAF. IMPOSSÍVEL sob a análise sã existente (`escape.tks:9-12`: dúvida → escapante). Precondição citada, não assumida.
- **Prova de composição transitiva (N frames):** `a()` retorna `b()` retorna `value`. Cada frame faz `leave` antes de avaliar/box-ar o seu retorno → o callee nasce com `current == R_ret` do caller → o valor bolha por N retornos direto à região do consumidor de topo (o §3b da transição, idêntico no nativo). **Gate:** gen2==gen3 native; `TEKO_MEM_PARANOID` limpo; `TEKO_ARENA_OBS` regiões-largadas ≈ nº escopos com locais, root-`unresolved`=0 exceto o frame do `main`; fixtures `mem_move_return`, `mem_move_transitive`, `mem_no_root_leak`, `mem_move_retain_struct` (re-verificado sob o move real). **Ritual:** SIM. **É O FLIP PERIGOSO DO MOVE.**

**NP7 — `#singleton` → root (fechar C2b no nativo, aditivo).** Um binding `#singleton` (o oráculo força `Root`, `residence.tks:167`) roteia à `tk_region_root_u()` explícita em vez da corrente — o twin do `di_scope_expr → tk_region_root()` da rota C (`codegen.tks:9564`). **Prova:** lido após o escopo declarante fechar, prova que não morreu. **Gate:** fixture `mem_singleton_root` (valor lido após o `{}` fechar), sob `TEKO_BACKEND=native`. **Ritual:** SIM. **Aditivo.**

**Ordem de risco:** NP1 (runtime, baixo) → NP2 (plumbing, byte-idêntico) → NP3 (net de balanceamento, comportamento-idêntico) → NP4 (reclaim scratch — UAF-gate) → NP5 (escopo/seletor — UAF-gate) → NP6 (move — flip) → NP7 (singleton, aditivo). Os degraus byte/comportamento-idênticos são NP1/NP2/NP3; os flips perigosos são NP4/NP5/NP6.

---

## 6. Fixtures — REUTILIZAM a família `mem_*` (zero `.tkp` novo)

> **REGRA DO DONO (obrigatória):** cada `.tkp` = +1 build; a contagem tem de CAIR, não subir. Toda prova abaixo ADICIONA um `.tkr` (cenário) e/ou um entrypoint ao `src` de um `.tkp` `mem_*` que JÁ existe (`examples/regressions/mem_block_dies`, `mem_str_scope`, `mem_singleton_root`, `mem_region_new_ok`, `mem_free_arena_ok`, `mem_free_slice_ok`), idealmente consolidando a família num `mem` único. Um `.tkr` suporta múltiplos `Scenario` partilhando UM build. **Zero `.tkp` novo.**

Cada prova ASSERTA STDOUT (`.tkr`, `Then stdout pattern = "…"`; corrupção = fingerprint errado, não só leak) e roda sob `Given env = ["TEKO_BACKEND=native"]`. O regressor de UAF continua a ser o self-host completo sob `TEKO_MEM_PARANOID`/ASan com o backend nativo — não um `.tkp`.

| fixture (novo `.tkr`/entrypoint num `.tkp` existente) | o que pina | crumb |
|---|---|---|
| `mem_move_retain_struct` | o padrão `parse_function`: valor construído por uma chamada, RETIDO num local, um CAMPO embrulhado num struct RETORNADO, usado após a chamada. Sem brackets → UAF/valor-corrompido; com → fingerprint exato | NP4 (repro) → NP6 (move real) |
| `mem_move_return` | valor construído no callee e retornado, usado após a chamada (a `fr` do callee não o pode reter) | NP4/NP6 |
| `mem_move_transitive` | valor retornado por N frames aninhados (bolha ao consumidor de topo) | NP4/NP6 |
| `mem_accum_block` | acumulador self-append `xs = push(xs,i)` no topo de um `{}` roteia à região do bloco e morre na saída (churn N ciclos; corrupção = valor errado) | NP5 |
| `mem_loop_per_iter` | acumulador declarado fora, crescido num loop interno: roteia a R_decl (bloco externo), NÃO à arena da iteração; pico plano ao longo de N iterações | NP5 |
| `mem_accum_return` | acumulador construído num bloco e RETORNADO: NÃO é block-local (escapa) → move, não morre no bloco (o detetor de que o seletor NÃO sobre-classifica) | NP5/NP6 |
| `mem_no_root_leak` | corpo só com locais+retornos; `TEKO_ARENA_OBS` scoped>0 e root-`unresolved`=0 exceto o frame do `main` | NP6 |
| `mem_singleton_root` (existe — +cenário native) | `#singleton` lido após o `{}` declarante fechar (prova que não morreu) | NP7 |

`mem_move_retain_struct` é o DETETOR-CHAVE do fechamento (o repro-mínimo do UAF que parou a rota C). `mem_accum_return` é o detetor de que o seletor N-níveis não sobre-classifica um acumulador que escapa.

---

## 7. Assinaturas Teko que o implementer adiciona/toca (full Javadoc — copiar verbatim)

O `NativeRegionFrame` e os campos de `LowerCtx` estão no §4. As funções de emissão que o port adiciona:

```teko
/**
 * region_current_vreg — o VReg do handle da região CORRENTE (o topo de `ctx.region_stack`): a
 * região de que `tk_alloc`/`tk_slice_push`/`tk_slice_elem_box` bump-alocam AGORA, e o parente de
 * uma `tk_region_new_u` de um escopo aninhado. Quando a pilha está vazia (antes de o frame abrir,
 * ou num sítio value-form sem frame vivo), devolve o VReg de uma chamada `tk_region_root_u` — o
 * twin do `cg_enclosing_region_expr` da rota C (`codegen.tks:8052`), que cai em `tk_region_root()`
 * numa pilha vazia. Aloca o VReg root sob demanda (emite `tk_region_root_u` só quando preciso).
 *
 * @param ctx  o contexto de lowering (a sua `region_stack`)
 * @return     (ctx', vreg) — o contexto (possivelmente com um `tk_region_root_u` emitido) e o VReg
 *             da corrente
 * @since 0.3.1
 */
fn region_current_vreg(ctx: LowerCtx): (ctx: LowerCtx, vreg: u32)
```

```teko
/**
 * open_native_region — abrir uma região léxica: emitir `bl = tk_region_new_u(<corrente>)` e
 * `void tk_region_enter_u(bl)`, empurrar um `NativeRegionFrame` em `ctx.region_stack`, e devolver o
 * contexto com a pilha crescida. Chamado na entrada de cada escopo com locais não-escapantes
 * (`{}`/braço/loop-iter) e na entrada da função (o frame, com `decl_name=""`). Twin do open de
 * `emit_block_region` (`codegen.tks:8302-8304`), com VReg no lugar do nome `_tkbr<n>`.
 *
 * @param ctx        o contexto de lowering
 * @param decl_name  o acumulador `Scope`-tier declarado neste escopo (para o seletor), ou ""
 * @param is_loop    esta região larga-se por iteração (corpo de loop)?
 * @param line       a linha-fonte a carimbar nas instruções emitidas
 * @param col        a coluna-fonte a carimbar
 * @return LowerCtx | error  o contexto com a região aberta e empilhada
 * @throws  propagado da emissão das chamadas de runtime
 * @since 0.3.1
 */
fn open_native_region(ctx: LowerCtx, decl_name: str, is_loop: bool, line: u32, col: u32): LowerCtx | error
```

```teko
/**
 * close_native_region — largar a região léxica no TOPO de `ctx.region_stack` numa aresta de saída:
 * emitir `void tk_region_leave()` e `void tk_region_drop_u(<topo>.region_vreg)`, e devolver o
 * contexto com a pilha des-empilhada. Emitido em CADA aresta (fall-through/return/break/continue) —
 * nos MESMOS sítios onde `replay_defers` já corre (`lower_return:5864`, `close_function:13192`,
 * `close_loop_body:6986`), o eixo-região da máquina de unwind-por-aresta (§2d). Twin do drop de
 * `emit_block_region` (`codegen.tks:8311`). Durante NP3 (aliased-root) o drop é SUPRIMIDO (a região
 * é alias de root); a partir de NP4 é real.
 *
 * @param ctx   o contexto de lowering (a sua `region_stack` não-vazia)
 * @param line  a linha-fonte a carimbar
 * @param col   a coluna-fonte a carimbar
 * @return LowerCtx | error  o contexto com o topo largado e des-empilhado
 * @throws  propagado da emissão, ou um erro nomeado se a pilha estiver vazia (invariante partido)
 * @since 0.3.1
 */
fn close_native_region(ctx: LowerCtx, line: u32, col: u32): LowerCtx | error
```

```teko
/**
 * binding_conveys_escape — true quando a RHS de um binding/assign de nome `name` deve ser envolvida
 * num bracket de conveyance (`enter(rr_vreg) … leave()`) para que qualquer resultado-de-chamada
 * retido nasça na região de retorno `rr_vreg` (a região do caller) em vez da corrente-de-frame, que
 * o `tk_region_drop_u(fr_vreg)` libertaria antes de o valor escapado ser lido (o UAF do padrão
 * `parse_function`). REGRA CEGA (soundness): true sse `name ∈ ctx.escaping`. Não deteta "contém uma
 * chamada" — bracketar uma RHS pura é inócuo, sub-detetar seria UAF; sobre-bracketar é leak-seguro
 * (`rr_vreg ⊒` todos os usos). A via residual de UAF é só a sub-marcação de escape — a precondição
 * já load-bearing (`escape.tks:9-12`). Twin EXATO do predicado homónimo da rota C
 * (`fechamento-…:364-382`); o nativo lê o MESMO `fn_escaping_vars`.
 *
 * @param escaping  o conjunto `ctx.escaping` da função (superconjunto do verdadeiro-escapante)
 * @param name      o nome LHS do binding/assign
 * @return          true sse a RHS de `name` deve ser bracketada para conveyance a `rr_vreg`
 * @since 0.3.1
 */
fn binding_conveys_escape(escaping: []str, name: str): bool
```

```teko
/**
 * native_scope_region_of — o VReg da região do escopo DECLARANTE de um acumulador `Scope`-tier
 * `name` (o seletor N-níveis, §1): percorre `ctx.region_stack` do topo para a base à procura do
 * `NativeRegionFrame` cujo `decl_name == name`, e devolve o seu `region_vreg`, para que um
 * self-append de `name` cresça AÍ (o buffer de um acumulador crescido num loop interno vive na
 * região do bloco externo, senão morre por-iteração — UAF). Devolve `null` se `name` não foi
 * registado como `Scope`-declarante (fallback leak-seguro à corrente/frame). Twin de
 * `scope_region_of` da rota C (`transicao-…:372`), com VReg no lugar do nome `_tkbr`.
 *
 * @param stack  a pilha de regiões abertas
 * @param name   o nome do acumulador self-append
 * @return       o VReg da região declarante, ou `null` se não resolvida
 * @since 0.3.1
 */
fn native_scope_region_of(stack: []NativeRegionFrame, name: str): u32 | null
```

**Funções existentes que o port TOCA (`lower.tks`):**
- `lower_function` (`:13120`) — inicializa `escaping`/`region_stack`; abre o frame (`open_native_region` com `rr`/`fr`); o `LowerCtx` literal (`:13130`) ganha os campos;
- `lower_fn_body`/`lower_fn_body_fat` (`:12914`/`:12941`) e `lower_return`/`lower_return_fat` (`:5863`/`:5890`) — o `tk_region_leave()` ANTES de `own_returned_value`/do store-fat, entre o `replay_defers` e o box (§2c); o `close_native_region` do frame na aresta;
- `own_returned_value` (`:10100`) e `box_aggregate_value_at` (`:10027`) — **território do 5º gap**; o move assenta sobre a sua forma FINAL (o box já emitido, agora executado sob `current == rr_vreg`);
- `close_function` (`:13190`) — o `close_native_region` do frame na fall-through;
- `lower_block`/`lower_block_value` (`:12863`/`:7178`) e o loop (`close_loop_body:6986`) — `open_native_region`/`close_native_region` por escopo (NP5);
- `lower_buf_ptr_call` (`:3435`) — `region_current_vreg(ctx)` em vez de `tk_region_root()` fixo; a doc de `is_buf_ptr_call` (`:3402-3405`) atualiza-se (a corrente É acessível agora);
- `lower_binding`/`lower_assign` (`:5596` / o assign) — brackets `enter(rr)…leave()` à volta da RHS quando `binding_conveys_escape`;
- os cinco `ctx_with*` (`:1108`/`:1129`/`:1150`/`:1171`/`:1192`) — propagam os dois campos novos;
- `lower_test.tkt` — os literais de `LowerCtx` ganham `region_stack`/`escaping` default.

**Runtime (C MANTIDA):** só `tk_region_current_u()` (§3). **Zero gémeos `_r`** — as helpers bare seguem a corrente.

---

## 8. Riscos, tensões de lei

| risco / tensão | resolução (law-first) |
|---|---|
| **R1 — flip enter/leave redireciona bare-escapantes → UAF** | A ORDEM NP3(aliased-root, net)→NP4(reclaim, `rr==root`)→NP6(flip): NP3 não mata; NP4 prova a completude dos brackets com raio contido (escapante ainda em root); NP6 só flipa o destino. A via residual (escape não-marcado) é fechada pela análise sã existente. Sem janela. |
| **R2 — o padrão `parse_function` (valor retido+embrulhado) sub-conduzido → UAF** | Mecanismo 1: bracket CEGO de toda RHS de nome ∈ `ctx.escaping` (`binding_conveys_escape`); o box de retorno sob `current==rr` (§2c). Provado em NP4 (`mem_move_retain_struct`, o repro que parou a rota C). |
| **R-selector — self-append em loop interno morre por-iteração** | `native_scope_region_of` roteia a R_decl (escopo declarante); realiza-se ENTRANDO a região declarante à volta do push (helpers seguem a corrente — via preferida, ZERO símbolo novo), não por `tk_slice_push_r` explícito. Fallback à corrente/frame leak-seguro. Fixture `mem_loop_per_iter`. |
| **R3 — brackets a cruzar retorno-em-erro embutido desbalanceiam a pilha** | Unwind-por-aresta via o sítio de `replay_defers` já existente (§2d); `tk_region_leave` auto-guardado (`teko_rt.h:196`) como rede. NP3 prova o balanceamento comportamento-idêntico. |
| **R4 — colisão com o 5º gap no box de retorno** | O move é cirúrgico: `leave()` ANTES do box (§2c), sem reescrever o box. Assenta sobre a forma FINAL de `own_returned_value` pós-drain. Por isso NP2–NP7 aguardam o drain (o bloqueio declarado). |
| **R5 — colisão em `LowerCtx`/`ctx_with*` (5º gap + FFI)** | Sequenciado após o drain; NP2 é o chore de campo isolado (byte-idêntico), mesmo shape do campo `table` já drenado (`git 55c2c890`). |
| **R6 — ABI congelada vs. move** | R_ret = corrente thread-local (`tk_region_current_u`), SEM parâmetro. ABI intocada (o Mecanismo 2/return-slot é que a tensionaria; recusado, `fechamento-…:§3`). |
| **R7 — runtime C congelada** | Exceção explícita a `teko_rt.{c,h}`: só `tk_region_current_u` — aditivo, comportamento-idêntico. Zero gémeos `_r` (o ganho do Mec. 1). |
| **R8 — FIXPOINT quebra** | gen2==gen3 native é auto-consistência (determinismo), não gen2==gen1; qualquer mudança determinística mantém-no. O detetor real é `TEKO_MEM_PARANOID` + `teko test .` + o self-host nativo sob ASan. |
| **R9 — divergência C-vs-nativo** | Ambos leem `fn_escaping_vars`/`residence_plan` e percorrem a MESMA TAST na mesma pré-ordem, com os MESMOS primitivos de runtime. Concordam por construção; o nativo não tem análise própria. |

**Tensão de lei que force HALT: NENHUMA.** Tudo resolve via Constituição/Leis (M.1 nunca-UAF por LUB; exceção de runtime C limitada a `tk_region_current_u`; Teko-only — o port é 100% `.tks` mais um acessor aditivo; issue-100%; regra zero-`.tkp`-novo do dono). O port fecha sem superfície cross-thread (que não existe). A recomendação Mecanismo 1 (brackets, `fechamento-…:§4`) é a que o nativo herda — o que aguarda ruling do dono na rota C aguarda-o AQUI pela mesma via (o nativo é espelho, não decisão independente).

---

## 9. O que fica EXPLICITAMENTE bloqueado (e o que não)

**Bloqueado até o drain do 5º-gap + FFI em `lower.tks`:** NP2–NP7 (toda EDIÇÃO de `lower.tks` — `LowerCtx`/`ctx_with*` colide com ambas; `own_returned_value`/box colide com o 5º gap).

**NÃO bloqueado, pode aterrar já:** NP1 (o `tk_region_current_u()` de runtime — não toca `lower.tks`, aditivo, FIXPOINT trivial). E TODO o design acima — os contratos contra a forma DECLARADA das dependências (`fn_escaping_vars`, os u-twins de runtime, o box de retorno), as fixtures `mem_*` (podem ser escritas como `.tkr`/entrypoints e ficar prontas), as assinaturas em Javadoc (copiáveis verbatim).

**Retoma em minutos no drain:** aplicar NP2 (o chore de campo, mecânico), depois NP3→NP7 pela ordem de risco, cada um com o seu gate ritual (gen2==gen3 native + PARANOID). O implementer não desenha nada — executa o mapa.
