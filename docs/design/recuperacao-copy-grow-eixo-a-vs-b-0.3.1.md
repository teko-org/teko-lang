# Campanha de recuperação do copy-grow — Eixo A vs Eixo B, ordem e primeiro bite

Arquiteto, 2026-08-24. Base: `origin/fix/retirement` (HEAD 5fb47adf, D70). Este documento é
PLANO — nenhum código de produto foi escrito. Fecha o pedido do D70: desenhar a campanha de
recuperação do copy-grow geométrico da arena.tks (58% do pico = ~1743 MB), decidir Eixo A
(exact-prealloc/two-pass) vs Eixo B (free-list reuse na root), analisar aliasing, ordenar, e
definir o bite 0 (instrumento) + bite 1 (o corte ratchet-medido) sob reseed-serial.

## 0. Verdade-base medida (D70) e o que a leitura do código acrescenta

- Pico do build seco = **3645,9 MB**. Métrica canônica do ratchet = a linha
  `teko: memory: peak <N> MB` (`src/build/project.tks:2324`), que sai de `teko::mem::peak_rss()`
  → `tk_peak_rss` (`getrusage`/`ru_maxrss`). **É RSS do SO — verdade honesta, independente de
  qualquer contabilidade de arena.** O ratchet já mede a coisa certa; o cego é só o instrumento
  de ATRIBUIÇÃO (`TEKO_ARENA_OBS`), não a régua.
- A arena real é `src/runtime/arena.tks`, que aloca por `ar_mmap` (syscall cru) → invisível ao
  `TEKO_ARENA_OBS`, que só instrumenta o caminho C (`posix_memalign`/`malloc`) em `teko_rt.c`.
- O funil é `region_alloc` (arena.tks:701) → `ar_region_alloc_w` (416) → bump em chunk existente
  (`ar_region_bump`) ou `ar_region_grow` (403) que faz `ar_chunk_try`→`ar_mmap` de um chunk novo.

### O sítio do copy-grow, no código emitido (achado desta leitura)

O idioma pós-expurgo `x = [..x, item]` (spread self-append) é lowerizado por
`emit_array_lit`/`cg_array_lit_*` (`src/codegen/codegen.tks:3709-3841`). Cada reatribuição:

1. liga operandos: `_ssub = x` (o sub-slice espalhado), `_sitm = item`;
2. `_stot = plain_count + _ssub.len` — tamanho **EXATO** `len+1` (crescimento **LINEAR**, não
   dobra — não há capacidade amortizada nesse idioma);
3. `_sacc = tk_region_alloc(tk_region_current(), _stot*sizeof)` — buffer **NOVO**;
4. copia `_ssub` para `_sacc` e apende `_sitm`;
5. resultado `{ .ptr=_sacc, .len=_stot }` reatribuído a `x` → **o backing antigo de `x` morre**.

Num `loop { x = [..x, item] }`, os buffers intermediários de tamanho 1,2,3,…,N são abandonados na
região root (bump append-only, nunca munmap) → **lixo residente**. Esse é o vilão do D70.

### O Eixo B JÁ EXISTE end-to-end — e a medição do D70 explica por que rende ≈0

Contra a suposição do dispatch ("a arena.tks não faz o free-old que o teko_rt.c fazia"), a
maquinaria de reciclagem já está **inteira e ligada**:

- **Análise de alias-safety:** `checker::assign_frees_old` (`src/checker/escape.tks:457`) prova
  quando o backing antigo está morto (ver §3). Correta.
- **Emissão do purge:** `emit_assign` (`codegen.tks:6568`) — quando `assign_frees_old` é verdade,
  computa o novo valor num temp, emite `free_block(old.ptr, old.len*sizeof)` e só então reatribui.
  `free_block` (arena.tks:707) → `ar_free_block` (690) → `ar_free_park` (669) na free-list.
- **Consumo da free-list:** `ar_region_alloc_w` (arena.tks:418-421) consulta `ar_free_take` para
  a **região root** antes de bumpar.

Ou seja: o "purge imediato na reatribuição" da lei já está implementado. **Por que então o D70
mede reuse de root ≈ 0?** A leitura do free-list responde, e é o achado decisivo:

- **Bins por classe de tamanho EXATA** (`ar_free_bin_slot`, arena.tks:607): bin = `bytes/16 - 1`.
  Um buffer liberado de tamanho `k*esz` cai no bin de `k*esz`; o **próximo** alloc do
  encadeamento pede `(k+1)*esz` → bin diferente (para `TExpr` esz=160, a diferença é 160B = 10
  unidades de bin) → **miss**.
- **Free-list grande** (`ar_free_take_large`/`ar_free_block_fits`, 638-666): exige
  `bytes >= size && bytes <= 2*size`. Num encadeamento crescente, o bloco liberado (`k`) é
  **sempre menor** que a próxima requisição (`k+1` ou, se fosse dobra, `2k+1`) → `bytes < size`
  → **falha**.

**Conclusão aritmética (vale para crescimento LINEAR e para DOBRA):** num encadeamento
**monotonicamente crescente**, todo buffer liberado é estritamente menor que toda alocação
posterior do mesmo encadeamento. **Nenhuma política de free-list pode reusar um bloco-menor-já-
liberado para uma requisição-maior-posterior.** O reuse ≈ 0 do D70 não é bug de wiring — é a
consequência matemática do formato do crescimento. O Eixo B só recuperaria reuso **cross-chain**
(cadeia A libera, cadeia B de tamanho parecido reusa), que a medição já encontrou ≈0.

- **Reforço (o Eixo B nem pode devolver ao SO):** os buffers do copy-grow são sub-alocações
  **dentro** de um chunk via bump (`ar_region_bump`), não mmaps próprios. `free_block` só os
  devolve à free-list (contabilidade), **nunca faz `munmap`** — o chunk inteiro só volta ao SO em
  `region_drop`. Logo um bloco parkeado-e-não-reusado permanece **residente**. Para o alvo
  primário (encadeamento monotônico na root), o Eixo B **é arquiteturalmente incapaz de baixar o
  RSS**. Não é questão de afinar a free-list: é impossível por construção.

## 1. Decisão law-first: Eixo A é a campanha; Eixo B é beco-sem-saída para o alvo primário

| | Eixo A (exact-prealloc / two-pass) | Eixo B (free-list reuse na root) |
|---|---|---|
| Remove o lixo residente do encadeamento monotônico? | **Sim** — não cria buffer intermediário nenhum | **Não** — provado incapaz (size-class e mid-chunk) |
| Devolve RSS ao SO? | Sim (o pico simplesmente não sobe) | Não (mid-chunk nunca munmapa) |
| Já é lei? | **Sim** (NO PUSHES, CLAUDE.md; as 4 naturezas MAP/PARSE/FILTER/BUFFER) | O que existe já está landado e correto |
| Risco de aliasing | Trivial (of_len + índice; sem raciocínio de escape) | Já resolvido por `assign_frees_old` |
| Corta também o tráfego O(n²) de cópia? | Sim | Não |

**Veredito:** o Eixo A dá **todos** os MB do alvo com o **menor** risco, e é engenharia sobre
piso já-decidido (a lei NO PUSHES). O Eixo B para o alvo primário é dead-end **provado** — não se
investe em expandir a free-list. Mantém-se `assign_frees_old` + `free_block` como estão (corretos,
baratos, ajudam cross-chain à margem e são o purge-seguro que a lei exige); **não se remove nem se
expande.**

Isto **não é um FORK** — é conclusão law-first + medição. A lei NO PUSHES já manda o Eixo A; a
matemática do free-list mostra que o Eixo B não alcança o alvo. Sem tensão de lei aberta.

## 2. Como os eixos compõem, e a ordem

- O Eixo A **elimina** o que o Eixo B teria que reciclar → depois do Eixo A não sobra encadeamento
  monotônico grande para reciclar. Fazer B primeiro (um arquivo só, `arena.tks`) seria delicado
  **e** inútil (rende ≈0, provado). Fazer A primeiro é incremental, seguro e é a lei.
- **Ordem recomendada:** bite 0 = instrumento (destrava a medição por sítio); depois Eixo A
  sítio-a-sítio pelo ranking do instrumento, **um por reseed-serial**, cada bite baixando o pico
  (ratchet estrito). Eixo B: não entra na fila.

## 3. Aliasing do Eixo B — o invariante, já enunciado no código

Mesmo mantendo (não expandindo) o Eixo B, registro o invariante que o torna correto, pois o
mesmo raciocínio protege o Eixo A quando o sítio convertido ainda precisa liberar um residual.

`assign_frees_old(fn_body, a)` (escape.tks:457) só libera o backing antigo quando **todas**:

1. `assign_is_self_append(a)` — a RHS é `x = [..x, …]` ou `x = list::push(x, …)` (o `x` da LHS
   é o mesmo `x` espalhado/empurrado);
2. `!name_escapes_body(x)` — `x` **não escapa**: `mark_expr` marca escaping em arg de chamada,
   struct-init, retorno, captura de lambda, índice/campo que sai. Se qualquer alias vivo de `x`
   pode existir, o gate reprova;
3. `births_empty == 1 && births_other == 0 && other_writes == 0` — `x` nasce **uma** vez de
   vazio e nunca é reescrito por outra coisa que não o self-append (encadeamento de dono-único);
4. reconciliação de leituras: as leituras de `x` fora do último statement == número de
   self-appends (cada self-append lê `x` uma vez) → **não há leitura viva do backing antigo além
   do próprio append que o substitui**.

**Invariante:** *o backing antigo de `x` no ponto da reatribuição não tem alias vivo — a única
referência era o próprio `x`, que a reatribuição sobrescreve.* É prova de dono-único puramente
sintática (sem análise de escape inter-procedural), o que a torna barata e conservadora-segura.
`ar_region_grow` **é** o funil único de crescimento, mas o buffer antigo pode, sim, ter alias
(slice/ponteiro pra dentro) — por isso o gate #2/#4 existe: reprova exatamente esses casos. Sem
o gate, o Eixo B seria inseguro (UAF). **Com** ele, é correto. Nada a mudar aqui.

## 4. Sequência de crumbs (ordenada, reseed-serial)

Cada crumb: SCOUT→IMPLEMENTER; reseed incondicional ao fim (deixa gen2/gen3 no scratchpad);
mede o `peak` do build seco e reporta maçã-com-maçã contra o commit anterior. Ratchet: só a
queda estrita landa; flat = regressão.

### Bite 0 — INSTRUMENTO: estender `TEKO_ARENA_OBS` à arena mmap  (ganho de pico: 0 MB; measure-only)

Pré-requisito de todo bite seguinte: sem ele, a atribuição por sítio é cega e não dá pra provar
qual sítio baixou o quê. **Inert em build normal** (gated por env) → não move o pico → não viola
ratchet (é a exceção measure-only, categoria do `tk_obs`). Reseed-class: nenhuma mudança de bytes
emitidos num build normal (o hook é `if (obs) …`), mas reseed incondicional por lei.

- **0a (Teko, `src/runtime/arena.tks`) — agregado + histograma de classe de tamanho.** Em
  `ar_region_grow`/`ar_region_bump`, sob leitura de env `TEKO_ARENA_OBS`, acumular: total de bytes
  mmap vivos (high-water), e um histograma por bucket (`<16KB`, `16–64KB`, `64KB–1MB`, `≥1MB`) +
  contagem de allocs com "cara de copy-grow". Dump no exit via `teko::io::eprintln`. Prova, sem C,
  que os bytes da arena mmap caem a cada bite (verifica o ratchet do lado da arena).
  - Assinaturas novas (arena.tks), todas doc-Javadoc, guardas cedo:
    ```
    /**
     * Records one mmap-arena growth for the TEKO_ARENA_OBS aggregate.
     *
     * Inert unless the observability channel is enabled; adds `bytes` to the
     * live high-water and to the size-class bucket for `bytes`.
     *
     * @param bytes the payload size just carved from the mmap arena
     */
    fn ar_obs_note(bytes: u64) { … }

    /**
     * Prints the mmap-arena size-class histogram and live high-water.
     *
     * No-op unless TEKO_ARENA_OBS is set; called once at process exit.
     */
    fn ar_obs_dump() { … }
    ```
    Toca: `ar_region_grow` (chama `ar_obs_note(size)`), o exit-path do build (chama `ar_obs_dump`).
    Estado do OBS mora no bloco `control` (novos offsets `CTRL_OBS_*`) — reusa `ar_ctrl_get/set`,
    zero alloc extra.
- **0b (maintained-C measure-only, `src/runtime/teko_rt.c`) — atribuição por call-site.** Extern
  `void tk_obs_arena_note(unsigned long long bytes)` que, se `tk_obs_enabled()`, faz
  `tk_obs_add(tk_obs_arena, __builtin_return_address(1), bytes)` (nível 1 porque é chamado de
  dentro de `region_alloc`, um frame acima do sítio emitido — espelha `tk_obs_mstr_note`). Feeder:
  `region_alloc` (arena.tks:701) chama `tk_obs_arena_note(n)` sob gate. Simboliza-se o binário com
  `-g -rdynamic` (como a medição antiga de `variant_siblings`) → **ranking dos sítios ≥16KB
  pós-expurgo**. Isto substitui o mapa velho (pré-expurgo, era do runtime C) pelo mapa atual.
  Exceção sancionada: `tk_obs` é o canal de observabilidade measure-only já existente em
  maintained-C; não é FFI de produto, é inert em build normal, e o expurgo não passa por ele.
- **Fixture/gate:** build normal byte-idêntico (fixpoint gen2==gen3) — prova que o hook é inert.
  Nenhum `.tkr` afirmativo (lei de testes). Entregável do bite: o RANKING (topo dos sítios ≥16KB
  por bytes) que ordena os bites 1..k.

### Bite 1 — EIXO A: converter o sítio #1 do ranking  (ganho estimado: 150–400 MB)

O sítio #1 do ranking 0b (provavelmente um `loop { xs = [..xs, …] }` construindo `[]TExpr`,
`[]@Type()` ou `[]@TStatement()` no checker — o histórico `variant_siblings` era 946 MB, 54% do
bucket, mas mudou de lugar no expurgo, por isso o ranking manda). Converter pela natureza:

- **MAP** (um item por elemento da fonte, tamanho = `fonte.len`): pré-aloca `var xs: [n]T = []`
  com `n = fonte.len` e preenche por índice `xs[i] = f(fonte[i])`. Zero buffer intermediário.
- **PARSE/SCAN** (n sai de varrer): **duas passadas** — 1ª conta `n` barato, 2ª pré-aloca
  `[n]T=[]` e grava por índice.
- **FILTRO** (subconjunto): **conta exato na 1ª passada** (NÃO pré-aloca `[fonte.len]T` com
  `count<<len` — isso é over-alloc e **viola o ratchet**, ver §5), depois `[count]T=[]` e grava.

Forma do que o implementer escreve (padrão MAP, doc-Javadoc; substitui o `loop` de self-append):
```
/**
 * Builds the typed siblings of `xs`, one output per input, minus `skip`.
 *
 * Pre-sized to the exact output length so no intermediate buffer is
 * allocated (NO PUSHES); fills by index.
 *
 * @param xs the source types
 * @param skip the index to omit, or `xs.len` for none
 * @return the sibling slice, length `xs.len` or `xs.len - 1`
 */
fn variant_siblings(xs: []@Type(), skip: u64): []@Type() {
    var n = if skip < xs.len { xs.len - 1 } else { xs.len }
    var out: [n]@Type() = []
    var k: u64 = 0
    var i: u64 = 0
    loop {
        if i >= xs.len { break }
        if i != skip { out[k] = xs[i]; k++ }
        i++
    }
    out
}
```
- **Toca:** o arquivo do sítio #1 (checker — `resolve.tks`/`typer.tks`/`collect.tks` conforme o
  ranking). Uma função por bite. `type_index_assign`, slice `[n]T=[]` e `loop { xs[i]=… }` já são
  suportados (CLAUDE.md), então é engenharia pura, sem feature de linguagem nova.
- **Ganho estimado:** se o #1 valer, como o histórico, ~40–55% do bucket ≥16KB (~1743 MB), um
  único sítio dominante rende **centenas de MB**. Conservador para o bite 1: **≥150 MB**. Alvo do
  bite: `peak` estritamente menor que 3645,9 MB.
- **Reseed** ao fim (o checker é compiler-core → o `teko.c` muda → reseed obrigatório).

### Bites 2..k — EIXO A: descer o ranking, um sítio por reseed

Repetir o bite 1 para os sítios #2, #3, … do ranking 0b, cada um com sua natureza (MAP/PARSE/
FILTRO), um por reseed-serial. Parar quando o bucket ≥16KB do 0a estiver drenado (o pico se
aproxima do rodapé residente: os ~482 MB de structs AST + scoped, que o D70 já classificou como
não-alvo). Cada bite: `peak` cai (ratchet), fixpoint gen2==gen3, reseed.

### Eixo B — NÃO entra na fila

Mantido como está (`assign_frees_old` + `free_block` + free-list da root). Não remover (é o purge
seguro da lei e ajuda cross-chain à margem), não expandir (dead-end provado para o alvo primário).

## 5. Riscos e tensões de lei

- **RATCHET vs over-alloc (o risco #1 do Eixo A).** Uma conversão de FILTRO que pré-aloca
  `[fonte.len]T` com `count << len` **aumenta** o pico e **viola** o ratchet (CLAUDE.md, D68). 
  **Resolução:** todo bite de FILTRO/PARSE conta EXATO numa 1ª passada barata antes de alocar —
  nunca o limite folgado. O gate do bite é o `peak` estritamente menor; se um bite sair flat/subir,
  é regressão → reverter/refazer com contagem exata, não landar.
- **Purga imediata e UAF.** O purge de `free_block` só é seguro sob o invariante do §3
  (`assign_frees_old`). Um bite do Eixo A que troque `x` in-place (`x = [..x, …]` vira `x[i]=…`)
  **elimina** a reatribuição → o purge nem dispara → sem risco de UAF novo. O Eixo A é mais seguro
  que o status quo aqui.
- **Reseed-serial.** Cada bite toca compiler-core → reseed obrigatório e serial (um por vez).
  Não paralelizar bites que tocam o mesmo arquivo do checker.
- **Sítio genuinamente dinâmico (o único candidato a FORK).** As 4 naturezas (MAP/PARSE/FILTRO/
  BUFFER) cobrem os sítios calculáveis, e o dono afirmou "tudo é calculável — nenhum sítio é
  impossível". SE o ranking 0b revelar um sítio que **não** é nenhuma das 4 (um worklist genuíno:
  cresce e encolhe, tamanho e ordem desconhecidos, sem contagem barata), esse **sítio específico**
  é FORK-pro-dono: decidir entre (a) chunk-chain/deque já existente (`COL-Q9 list-chunkchain`,
  `.crumbs/0077`) ou (b) manter e reportar. **Não é um HALT agora** — é um watch-point; a asserção
  do dono diz que não deve existir. Só vira HALT se a medição 0b exibir um contraexemplo concreto.

## 6. Resumo dos pontos de arquivo

- Métrica do ratchet (RSS honesto): `src/build/project.tks:2319-2324` (`peak_rss`→`tk_peak_rss`).
- Funil da arena mmap: `src/runtime/arena.tks` — `region_alloc`:701, `ar_region_alloc_w`:416,
  `ar_region_grow`:403, `ar_free_take`:663, `ar_free_park`:669, `ar_free_bin_slot`:607.
- Sítio do copy-grow emitido: `src/codegen/codegen.tks` — `emit_array_lit`:3709,
  `cg_array_lit_alloc`:3815; purge na reatribuição: `emit_assign`:6568 (`FreeBlock`:6578).
- Alias-safety (Eixo B, invariante do §3): `src/checker/escape.tks` — `assign_frees_old`:457,
  `assign_is_self_append`:264, `name_escapes_body`:451, `chain_stats_block`:444.
- Instrumento measure-only a estender: `src/runtime/teko_rt.c` `tk_obs_*` (:1380+), env
  `TEKO_ARENA_OBS`.
- Sítios do Eixo A a rankear: ~202 spread-self-append no checker, ~102 no lir (grep
  `= [..<ident>`); só os ≥16KB do ranking 0b entram na fila.
