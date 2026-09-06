---
section: design
created: 2026-08-02
source: ruling do dono ("compilação nativa multi-thread; o fix backend-memoria já isolou a memória por função — essa isolação É o que habilita paralelizar"), docs/design/backend-memoria-por-funcao-0.3.1.md (a isolação por função — a base direta), docs/design/concorrencia-adiantada-s8.md §3.2/§6.2 (fork_join determinístico; a ameaça de nomes ordinais), src/build/project.tks:2687-2746 (o laço fundido, JÁ aterrado)
status: DESENHO — nenhuma linha de produto. A execução do paralelismo depende das primitivas S8 (fork_join/isolate/cabi fn), DESIGN-only não-aterradas; tudo o que NÃO depende delas (o esquema, a prova, os contratos, as fixtures) está aqui e compila contra a forma DECLARADA.
branch: cargo/0.3.1.0-paralelizacao-arq (de origin/fix/union)
---

# Paralelização — Eixo 2: compilação NATIVA multi-thread

Arquiteto, 2026-08-02. Documento de DESENHO. `teko.c` é SAÍDA; todo o produto novo é `.tks`
(a exceção C mantida é só `teko_rt.{c,h}`, e este eixo mal a toca). Regra do dono: proposta, não
contra-argumento; alarme só com arquivo:linha.

---

## 0. A base já aterrada — a isolação por função É o habilitador

O fix backend-memoria (C1-C3, no vagão; head `e399db23`) fundiu os três passes de módulo num laço
POR FUNÇÃO com região de scratch isolada. A estrutura-alvo está VIVA em `src/build/project.tks`:

```
// encode_module_fused_x86 (project.tks:2737-2746)
mut mt = empty_module_text_x86()
loop over entry.funcs[i]:
    mt = fold_lfunc_scoped_x86(mt, entry.funcs[i])   // region_new/enter → encode → leave → fold → drop
finish_encoded_module_x86(mt, rodata, globals)
```

E o tijolo por-função (`fold_lfunc_scoped_x86`, `project.tks:2709-2722`):

```
child = region_new(region_root())          // região-filha, chunks SEPARADOS da raiz
region_enter(child)                          // todo scratch de isel/regalloc/encode bump-aloca aqui
encoded = encode_lfunc_in_region_x86(f)      // select_lfunc → regalloc_func → encode_func (:2687-2691)
region_leave()
out = fold_encoded_func_x86(mt, encoded)     // COPIA bytes/syms/relocs para a raiz (Grupo B)
region_drop(child)                           // larga LIR+MInst+intervalos+encode-scratch de uma vez
```

**Este desenho não inventa a fronteira — herda-a.** `encode_lfunc_in_region_x86(f)` já é uma função
PURA de `f`: consome só a `LFunc` e o descritor de ABI constante (`SYSV64`), toca só memória
per-task (a filha entrada), e produz um `EncodedFuncX86` cujos offsets são **relativos ao início do
`.text` da própria função** (doc-comment do backend-memoria §7). O backend-memoria §2 já PROVOU a
não-escape: os únicos ponteiros que sobrevivem ao drop são os nomes `str` de `Symbol`/`RelocX86`, e
esses originam em `prog` (Grupo B, imutável, partilhado), não no scratch. **Paralelizar é exatamente:
correr o `encode_lfunc_in_region_x86` de funções independentes em threads distintas, cada uma na sua
filha, e sincronizar SÓ no fold.**

---

## 1. O escopo do paralelismo — o BACKEND pós-lowering, e por que NÃO o lowering

Decisão de escopo, e ela é o que torna o eixo seguro:

**Paralelizamos `select`/`regalloc`/`encode` por função. NÃO paralelizamos `lower_program`.** O
lowering fica SERIAL, e isto não é conservadorismo — é o que desarma a ameaça mais perigosa que o
concorrencia-doc §6.2 nomeou: os **nomes ordinais** (`.Lstr<n>`, `.Lclofn<n>`, `lower.tks:4616`/`1619`)
são atribuídos DURANTE o lowering, na ordem de visita. Paralelizar o lowering renomearia símbolos e
mataria o fixpoint. Mas quando o fluxo chega a `encode_module_fused_x86`, o `LModule` já está TODO
baixado, com cada nome já fixado em ordem serial. **O backend paralelo só CONSOME nomes já fixos — a
ameaça ordinal não se aplica ao Eixo 2.** (Paralelizar o lowering é um eixo DIFERENTE e mais arriscado,
que este desenho explicitamente NÃO toma; fica para depois de os pontos abertos de
`bare-name-probe-family.md` fecharem.)

**Verificação de que o per-função não lê estado global mutável que afeta bytes** (a prova §3 depende
disto):
- `tk_alloc` → per-task (a filha entrada, `cur_regions` topo). ✔
- `region_new`/`region_drop` → `tk_task_current():regs` (per-task) + `tk_g_region_gen` ATÔMICO
  (`teko_rt.c:1281`); o `gen` só afeta a validade do `push_cache`, NUNCA os bytes. ✔
- interning → **medido: o backend NÃO interna.** `grep intern src/backend` = só palavras de
  doc-comment ("internal", "rodata-internal"); os builtins `intern_get`/`put` só têm binding em
  `scope.tks:743` e `codegen.tks:4322` (a rota C), nenhum call-site no caminho isel/regalloc/encode. ✔
- `rodata`/`globals`/`layouts` (Grupo B, exceção E2) → lidos por-referência SÓ em
  `finish_encoded_module_x86` (o fold serial no fim), nunca por função. ✔
- `Symbol.name`/`RelocX86.sym` → originam em `prog`, imutável partilhado (E1). ✔

Nada no per-função lê estado global mutável que afete um byte emitido. É a pré-condição da prova.

---

## 2. O esquema — thread-pool, região-por-thread, fold ordenado

### 2a. Duas fases: MAP paralelo (encode) → REDUCE serial ordenado (fold)

O laço fundido de hoje entrelaça encode e fold numa iteração. O desenho paralelo SEPARA-OS:

```
// FASE MAP (paralela): funções independentes → EncodedFuncX86, em `lanes` workers
// função i → worker (i % lanes)   [atribuição ESTÁTICA, fork_join §3.2]
parallel for i in 0..funcs.len:
    encoded[i] = encode_lfunc_in_region_x86(funcs[i])   // na region-por-lane do worker
// BARREIRA (join)
// FASE REDUCE (serial, no pai, em ordem de ÍNDICE):
mut mt = empty_module_text_x86()
for i in 0..funcs.len:                                    // 0,1,2,… — ordem de função, NÃO de conclusão
    mt = fold_encoded_func_x86(mt, encoded[i])
finish_encoded_module_x86(mt, entry.rodata, entry.globals)
```

O ÚNICO ponto de sincronização é a barreira. O fold é serial e em ordem de índice — é O(n) de cópias
de bytes, barato face ao select/regalloc/encode que agora correm em paralelo.

### 2b. A região-por-thread — cada worker com a sua `tk_task`/árvore de regiões

Cada worker corre numa `tk_task` própria (`tk_task_begin()` no topo do worker → task fresca com root
próprio; o acessor `tk_task_current()` é `_Thread_local`, `teko_rt.c:1240`). Dentro do worker, a
disciplina de região do backend-memoria REUSA-SE tal-e-qual: uma **região-de-raia** (o segmento do
journal, §2c) como base, e por função uma filha de scratch entrada/largada. Como cada worker entra a
SUA região, **dois workers nunca bump-alocam a mesma região** → sem corrida de alocação, sem lock no
caminho quente. `tk_g_region_gen` atômico garante que `push_cache` não false-hita entre tasks
(ruling `teko_rt.c:1180-1187`, já aterrado).

### 2c. O ponto de junção — a região-de-raia sobrevive à barreira, o fold copia dela

O `EncodedFuncX86` de A tem de sobreviver ao fim do scratch de A E à barreira, para o pai o folder.
Espelhando a "cópia-antes-do-drop" que o backend-memoria já faz (`fold_encoded_func_x86` copia bytes
para a raiz ANTES de `region_drop`), o worker aqui **copia o `EncodedFuncX86` para a sua
região-de-raia** (parent-visível, sobrevive à barreira) e SÓ ENTÃO larga o scratch da função:

```
// corpo do worker, por função i que lhe pertence:
scratch = region_new(lane_region)      // filha da região-de-raia
region_enter(scratch)
ef = encode_lfunc_in_region_x86(funcs[i])
region_leave()
encoded[i] = clone_encoded_into(lane_region, ef)   // cópia p/ a região-de-raia (bytes + spines;
                                                    // os str de nome ficam em prog, não copiados)
region_drop(scratch)                                // larga o scratch da função — o pico per-lane
                                                    // é 1 scratch vivo + os EncodedFuncX86 acumulados
```

- `lane_region` é criada pelo PAI (filha de `tk_region_program()`, task-agnóstica, sobrevive ao
  `tk_task_end` do worker) e passada ao worker no `ctx`. O worker só a ENTRA (não a cria), pelo que ela
  não entra no `regs` do worker e não morre com ele.
- `encoded[]` é um array de slots DISJUNTOS (escrita por índice próprio — fork_join §3.2), residente
  na região de programa; o worker escreve só `encoded[i]` dos seus `i`.
- Depois da barreira, o pai folda `encoded[0..n]` em ordem, então larga as `lanes` regiões-de-raia.

**Preserva o ganho de memória do backend-memoria por-raia:** o scratch da função é largado a cada
função (não acumula); só o `EncodedFuncX86` final (pequeno) fica na região-de-raia. Pico por-raia =
`1 scratch vivo + os encoded acumulados dessa raia`, contra `LIR + 2×MModule` de antes.

### 2d. A superfície S8 que isto consome (forma DECLARADA — DESIGN-only, ver §6)

O MAP paralelo é um `fork_join` (concorrencia-doc §3.2): `entry` = uma função `cabi` de worker,
`count` = `funcs.len`, `lanes` = nproc (§4), `ctx` = o plano (funcs, encoded[], lane_regions). A
atribuição estática `i → i % lanes`, a escrita disjunta e a leitura-após-barreira são exatamente o
contrato que `fork_join` já declara. **Nenhuma capacidade nova de linguagem é pedida** além da que a
concorrencia-doc já projeta.

---

## 3. A prova de FIXPOINT sob paralelismo (byte-idêntico)

**Afirmação.** O objeto emitido é byte-idêntico quer o encode corra em série (o laço fundido de hoje)
quer em `lanes` workers, para qualquer `lanes ≥ 1`.

**O objeto é uma função pura da sequência de fold.** O produto final é

```
finish( fold(… fold(fold(empty, e[0]), e[1]) …, e[n-1]), rodata, globals )
```

onde `e[i] = encode_lfunc_in_region_x86(funcs[i])`. Basta provar duas invariantes:

**(I1) Cada `e[i]` tem o MESMO valor no caminho serial e no paralelo, independente de quem/quando o
computa.** `encode_lfunc_in_region_x86(funcs[i])` é função pura de `funcs[i]`: os seus únicos inputs
são a `LFunc funcs[i]` e o descritor de ABI constante `SYSV64`; toca só memória per-task (a filha de
scratch); e os seus outputs (bytes, syms com offsets relativos ao próprio `.text`, relocs) não
dependem de nenhum outro `funcs[j]` nem de estado global mutável (§1, verificado item-a-item: sem
interning, `region_gen` atômico e irrelevante para bytes, rodata/globals lidos só no `finish`). Logo
`e[i]` é o mesmo valor em série, em paralelo, e sob qualquer entrelaçamento. ∎(I1)

**(I2) A sequência de fold é aplicada na MESMA ordem `i = 0,1,…,n-1` nos dois caminhos.** No paralelo,
a FASE REDUCE itera `i` de `0` a `n-1` LENDO o slot disjunto `encoded[i]` — por ÍNDICE, nunca por
ordem de conclusão. A barreira garante que todos os slots estão preenchidos antes de o reduce começar,
logo cada `encoded[i]` já contém `e[i]` (por I1). A ordem de conclusão dos workers afeta apenas QUANDO
`encoded[i]` é preenchido, nunca a ORDEM em que o reduce o consome. ∎(I2)

**Conclusão.** Por I1 os inputs do fold são idênticos; por I2 a ordem do fold é idêntica;
`fold_encoded_func_x86` e `finish_encoded_module_x86` são determinísticos. Logo o output é
byte-idêntico. **O fixpoint gen2==gen3 é preservado, e é ele próprio o detector**: se um byte mudar,
alguma das duas invariantes foi violada (um escape para região errada, ou um fold em ordem de
conclusão) — PARAR e reexaminar §1/§3, nunca avançar. ∎

**Por que a ordem de conclusão NÃO pode vazar para os bytes:** o único sítio onde a ordem poderia
entrar seria o re-base de offsets `.text` (cada função soma o comprimento corrente de `mt`). Mas esse
re-base acontece SÓ no `fold_encoded_func_x86` da FASE REDUCE serial, em ordem de índice — os
`EncodedFuncX86` da fase MAP carregam offsets RELATIVOS ao próprio `.text`, sem qualquer referência à
posição no módulo. A absolutização é serial e ordenada. É a mesma verdade do backend-memoria §2 (o
output de A é copiado para o Grupo B antes de A ser largada), aqui estendida: a ABSOLUTIZAÇÃO de A é
adiada para o reduce ordenado.

---

## 4. Política de nproc — default OS-granted, cap OS max (regra do dono)

`lanes` default = **o paralelismo que o SO CONCEDE a este processo**, capado ao máximo do SO:

- `hardware_parallelism()` (builtin S8, concorrencia-doc §3.2) devolve a contagem CONCEDIDA, não o
  `nproc` cru: Linux `sched_getaffinity` popcount (respeita cpuset de cgroup / `taskset`), Windows
  o popcount da máscara de afinidade do processo (`GetProcessAffinityMask`) / `GetActiveProcessorCount`,
  macOS `sysctl hw.logicalcpu` (sem API de afinidade), sempre clampado a `≥ 1`.
- O CAP do SO: um override explícito (env de build — reusar o idioma `TEKO_TEST_JOBS`, p.ex.
  `TEKO_BUILD_LANES`) é clampado a `[1, os_max]` onde `os_max = _SC_NPROCESSORS_ONLN` /
  `GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)`. Nunca sobre-subscreve para além da máquina.
- `lanes = 1` reproduz o caminho serial de HOJE byte-a-byte (o laço fundido intocado) — é o modo de
  bisecção quando o fixpoint acusa.

O mesmo `hardware_parallelism()` serve o Eixo 1 (`TEST_JOBS_DEFAULT`/`REGR_JOBS_DEFAULT` deixam o
literal `4` de `project.tks:3783` e passam a esta política) — uma regra, dois eixos.

---

## 5. O modelo de build (`wall ≤ Σ fases`, `dark = 0`) sob paralelismo

A lei de observabilidade — `wall ≤ Σ fases` e `dark = max(0, wall − Σ fases) = 0` — SOBREVIVE, e o
desenho tem de a honrar num ponto concreto:

**A região paralela inteira fica DENTRO de UMA fase `phase_begin`/`phase_end`** (a fase "codegen",
`project.tks:842`). Não se abre uma fase por worker nem por função — isso fragmentaria a contabilidade
e criaria corrida de medição. O fork-join corre TODO dentro do relógio de parede dessa fase, pelo que:

- **`dark` continua 0:** as fronteiras de fase seguem bracketando todo o trabalho; nenhum tempo de
  worker escapa para fora de uma fase. `wall` da fase codegen é medido de ponta a ponta como hoje.
- **O paralelismo AUMENTA o overlap, não o dark:** o `wall` da fase codegen passa a ser MENOR que a
  soma dos tempos-CPU per-função internos (é o ganho). A lei `wall ≤ Σ fases` é sobre as FASES externas
  somarem ≥ wall — e continua verdadeira: os tempos per-função não são fases, são trabalho DENTRO de
  uma fase. O overlap encolhe o `wall` da fase; a soma das fases externas não desce abaixo dele.
- O par `phase_begin`/`phase_end` da fase codegen é tocado pelo pai (a thread que faz o fork-join),
  antes de lançar e depois de juntar — a instrumentação não entra nos workers.

---

## 6. O que está BLOQUEADO, e o que se adianta HOJE (DESIGN-AHEAD)

**BLOQUEADO — a execução do paralelismo depende das primitivas S8, DESIGN-only não-aterradas:**
`fork_join`/`spawn`/`join`/`Isolate`/`hardware_parallelism` (concorrencia-doc §3.2) e a coerção
`cabi fn` (§2.4) — `grep fork_join|hardware_parallelism|cabi src = vazio`. Enquanto elas não fecharem,
o MAP paralelo (§2a) não pousa.

**ADIANTADO HOJE (não depende do bloqueio):**
1. **A refatoração MAP/REDUCE do laço fundido, com `lanes = 1`** — separar `encode_module_fused_x86`
   (`project.tks:2737`) em (i) um MAP que preenche `encoded[]` e (ii) um REDUCE que folda em ordem, JÁ
   entregando byte-idêntico a hoje com um único "worker" serial. Isto é executável AGORA, sem S8, e
   deixa o esqueleto pronto para o `parallel for` entrar como troca de UMA construção. É o crumb E2-C1.
2. **`clone_encoded_into` e os contratos** (`encode_func_x86` já é `pub`, backend-memoria C2) contra a
   forma DECLARADA de `fork_join`/`Isolate`.
3. **As fixtures de FIXPOINT** (`fixpoint_lanes_invariant` — `lanes=1` vs `lanes=8`, objetos comparados
   byte-a-byte), escritas para correr com `lanes=1` HOJE e com `lanes=N` quando S8 fechar.
4. **A política de nproc** (§4) e o wiring do `phase_begin` único (§5) — ambos sem S8.

Quando S8 fechar, o implementador resume em minutos: troca o `for` serial do MAP por `fork_join` sobre
a MESMA função de worker `cabi`, sobre o MESMO `encoded[]` disjunto.

---

## 7. Assinaturas Teko que o implementador adiciona (full Javadoc — copiar verbatim)

```teko
/**
 * encode_module_mapped_x86 — a FASE MAP do encode nativo paralelizável: para cada função de
 * `entry.funcs`, computa o seu `EncodedFuncX86` (select → regalloc → encode) na sua própria região
 * de scratch e deposita-o no slot DISJUNTO `encoded[i]`. Com `lanes == 1` é um laço serial
 * byte-idêntico ao caminho fundido de hoje; com `lanes > 1` é um `fork_join` (atribuição estática
 * `i % lanes`, escrita disjunta, leitura só após a barreira). NÃO folda nada — a absolutização de
 * offsets é adiada para a FASE REDUCE ordenada (`encode_module_reduced_x86`), o que preserva o
 * fixpoint byte-idêntico (ver §3 do design).
 *
 * @param entry  o módulo baixado (LIR pronto; nomes ordinais JÁ fixados em série no lowering)
 * @param lanes  o grau de paralelismo (1 = serial; default = política de nproc, §4)
 * @return       o vetor de `EncodedFuncX86` em ordem de índice de função, ou o honest-stop propagado
 * @since 0.3.1
 */
fn encode_module_mapped_x86(entry: teko::lir::LModule, lanes: u64): []teko::backend::EncodedFuncX86 | error

/**
 * encode_module_reduced_x86 — a FASE REDUCE serial e ORDENADA: folda `encoded[0..n]` no acumulador
 * `.text` da raiz em ordem de ÍNDICE (nunca de conclusão), absolutizando os offsets `.text` de cada
 * função pela posição corrente do acumulador, e monta a `EncodedModuleX86` final com a rodata/globais
 * persistentes (Grupo B, por-referência). É a lógica de `encode_module_x86` de sempre, agora
 * incremental e determinística por construção.
 *
 * @param encoded  os `EncodedFuncX86` da FASE MAP, em ordem de índice de função
 * @param rodata   a rodata persistente do módulo (Grupo B)
 * @param globals  os globais persistentes do módulo (Grupo B)
 * @return         o módulo codificado (imagens de secção + símbolos ordenados + relocs), ou honest-stop
 * @since 0.3.1
 */
fn encode_module_reduced_x86(encoded: []teko::backend::EncodedFuncX86, rodata: teko::lir::Rodata, globals: teko::lir::Globals): teko::backend::EncodedModuleX86 | error

/**
 * clone_encoded_into — copia um `EncodedFuncX86` da região de scratch da função para `dst` (a
 * região-de-raia, parent-visível), copiando os bytes de `.text` e os spines de `syms`/`relocs`. Os
 * `str` de NOME (`Symbol.name`/`RelocX86.sym`) NÃO são copiados: originam em `prog` (Grupo B,
 * imutável, sobrevive a tudo). Espelha a cópia-antes-do-drop do backend-memoria — é o que permite
 * largar o scratch da função enquanto o resultado sobrevive à barreira do fork-join.
 *
 * @param dst  a região-de-raia que recebe a cópia (sobrevive ao `tk_task_end` do worker)
 * @param ef   o `EncodedFuncX86` residente no scratch da função, a copiar antes do drop
 * @return     o `EncodedFuncX86` residente em `dst`
 * @since 0.3.1
 */
fn clone_encoded_into(dst: u64, ef: teko::backend::EncodedFuncX86): teko::backend::EncodedFuncX86
```

Funções existentes TOCADAS (chamadas, não editadas): `encode_lfunc_in_region_x86`
(`project.tks:2687`), `fold_encoded_func_x86`, `finish_encoded_module_x86` (`project.tks:2745`),
`region_new`/`region_enter`/`region_leave`/`region_drop` (`project.tks:2640-2673`). Editado de facto:
`encode_module_fused_x86` (`project.tks:2737`) e os espelhos das outras três caudas.

---

## 8. Fixtures de regressão (verdito por STDOUT; NUNCA exit) — gen2 NATIVE

**A lei que o dono mais bate: está PROIBIDO olhar para o exit — olha-se para o STDOUT.** Espelhando o
§5 do Eixo 1, nenhuma fixture aqui asserta sobre exit-code. Há dois modos de verdito, e ambos são
comparação de BYTES, nunca de código de saída de um processo:

- **fixtures de CORREÇÃO DE EXECUÇÃO** — o programa gerado IMPRIME o valor (a soma / o valor
  propagado / a igualdade) em stdout, e o teste compara o **stdout** contra o esperado;
- **fixtures de FIXPOINT** — comparam os **bytes do OBJETO** emitido com `cmp` do artefato (`.o`),
  não o exit-code de um processo de teste; a igualdade é do ficheiro, e o verdito dela vai por stdout.

| fixture | forma | verdito (o que se compara) | o que prova |
|---|---|---|---|
| `par_map_reduce_serial` | módulo de N funções, `lanes=1` | **`cmp` dos BYTES do `.o`** contra o objeto do caminho fundido de hoje (comparação de artefato, NÃO exit) | a refatoração MAP/REDUCE é byte-idêntica ao serial (E2-C1, HOJE) |
| `fixpoint_lanes_invariant` | mesmo projeto com `lanes=1` e `lanes=8` | **`cmp` dos BYTES do `.o`** entre as duas execuções (comparação de artefato, NÃO exit) | a prova §3 na prática (BLOQUEADO em S8 para N>1) |
| `par_call_chain` | `main`→`f`→`g` (relocs de call cruzam funções), `lanes=4`; o programa **imprime o valor propagado** | **stdout** = o valor propagado esperado (diff de stdout) | re-base ordenado no reduce; `RelocX86.sym` (E1) sobrevive à barreira |
| `par_rodata_shared` | duas funções que usam o MESMO literal (rodata Grupo B), `lanes=4`; o programa **imprime a igualdade** dos dois usos | **stdout** = a igualdade/valor esperado (diff de stdout) | E2: rodata lida só no `finish`, não cai na região-de-raia; partilha preservada |
| `par_many_small` | 64+ funções pequenas somadas, `lanes=nproc`; o programa **imprime a soma** | **stdout** = a soma conhecida (diff de stdout) | `lanes` regiões-de-raia; pico per-raia = 1 scratch + encoded acumulados |
| `par_fixpoint_selfbuild` | o próprio `src/` (self-build), gen2 vs gen3 sob `lanes>1` | **`cmp` dos BYTES** de gen2 vs gen3 (comparação de artefato, NÃO exit) | ritual: paralelizar não altera um byte (BLOQUEADO em S8) |

`par_map_reduce_serial` é a prova MAIS BARATA e a primeira — corre HOJE, sem uma única thread, e é o
que fecha o crumb E2-C1; o seu verdito é o `cmp` de bytes do `.o`, não um exit-code.

---

## 9. Crumbs ordenados (com colisões e rituais)

Ponto ritual = gate completo: gen2 nativo + `teko test .` verde + FIXPOINT gen2==gen3 byte-idêntico +
diff C-vs-own inalterado.

| # | crumb | entrega | colisão | ritual |
|---|---|---|---|---|
| **E2-C0** | **[FEITO]** commit vazio + push | proteção contra restart | — | não |
| **E2-C1** | **MAP/REDUCE serial (`lanes=1`)** — separar `encode_module_fused_x86` (`project.tks:2737`) em `encode_module_mapped_x86` + `encode_module_reduced_x86`; `clone_encoded_into`; byte-idêntico a hoje | `project.tks` (as caudas — longe dos ficheiros quentes); ADITIVO ao backend | **sim** (FIXPOINT gen2==gen3) |
| **E2-C2** | **Trocar o MAP serial por `fork_join`** (x86-linux, a prova) — worker `cabi`, `lanes`=nproc, `encoded[]` disjunto, região-de-raia; **DEPENDE de S8** | `project.tks` | **sim** (FIXPOINT + `fixpoint_lanes_invariant`) |
| **E2-C3** | **Replicar às outras três caudas** — `emit_native_win`/`emit_native_arm64`/`emit_native_arm64_linux` (`project.tks:2801`/`2587`/`2612`), mesmo padrão MAP/REDUCE + fork_join | `project.tks` | **sim** por cauda |
| **E2-C4** | **nproc + fase única** — política §4 (`hardware_parallelism()` capado); `phase_begin` codegen único envolvendo o fork-join (§5); `dark==0` verificado | `project.tks` | **sim** |
| **E2-C5** | **[limpeza]** retirar `encode_module_fused_x86` original se sem chamador; manter se `*_test.tkt` o exercita | `project.tks` | **sim** |

**Colisão coordenada:** o Eixo 1 e o Eixo 2 partilham `hardware_parallelism()` (§4) e ambos tocam
`project.tks` — o Eixo 1 no runner do gate (`native_gate_run:3716`), o Eixo 2 nas caudas
`emit_native_*` (`:2587-2801`). Sítios DISJUNTOS do mesmo ficheiro; sequenciar (Eixo 1 primeiro
desbloqueia o gate native que valida o Eixo 2) evita rebase quente.

---

## 10. Riscos e tensões de lei — com resolução

| risco / tensão | resolução (law-first) |
|---|---|
| **R1 — fold em ordem de conclusão quebra o fixpoint** | O REDUCE itera por ÍNDICE, lê `encoded[i]`, absolutiza offsets só aqui (§3-I2). A ordem de conclusão não entra nos bytes. `fixpoint_lanes_invariant` é o detector. |
| **R2 — resultado morre com a task do worker** | `clone_encoded_into` copia para a região-de-raia (filha de `tk_region_program()`, task-agnóstica, sobrevive ao `tk_task_end`) ANTES do drop do scratch — espelha a cópia-antes-do-drop do backend-memoria. |
| **R3 — nomes ordinais renomeiam sob paralelismo** | Escopo: paralelizamos o BACKEND pós-lowering; os nomes são fixados no lowering SERIAL antes (§1). A ameaça §6.2 do concorrencia-doc não se aplica ao Eixo 2. |
| **R4 — corrida no per-função** | §1 verificou item-a-item: sem interning, `region_gen` atômico e irrelevante para bytes, rodata só no `finish`, memória per-task. Nenhum estado global mutável que afete bytes. |
| **R5 — `dark` cresce com o paralelismo** | Uma fase `phase_begin`/`phase_end` única em volta do fork-join (§5); nenhum tempo de worker escapa; `dark` continua 0; o overlap encolhe o `wall` da fase, o que a lei permite. |
| **R6 — execução bloqueada em S8** | DESIGN-AHEAD: E2-C1 (MAP/REDUCE serial byte-idêntico), os contratos, a política de nproc e as fixtures `lanes=1` são executáveis HOJE; E2-C2+ pousam quando `fork_join`/`cabi fn` fecharem, como troca de UMA construção. |

**Nenhuma tensão de lei genuína permanece.** Sem HALT.
