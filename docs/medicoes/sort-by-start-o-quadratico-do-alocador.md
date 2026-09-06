# O `sort_by_start` quadrático do alocador — medido, substituído, e remedido

Ramo `cargo/0.3.1.0-sort-by-start`, sobre `remodel/0.3.1.0-linux-native-2` (`a2d832bc`).
Máquina: Linux x86_64, partilhada com outros agentes; cada medição corrida sozinha, com
`free -g` verificado antes.

---

## 1. O alvo, e a guarda que decide se ele corre

`src/backend/regalloc.tks:1113` (antes desta mudança) ordenava os intervalos virtuais por
inserção, **reconstruindo a lista de saída inteira a cada intervalo**: um `teko::list::push`
por elemento já colocado, por cada elemento novo. O(V²) em tempo E em bytes.

**Quem o chama, e quando.** `linear_scan` (`src/backend/regalloc.tks:1359` na base, `:1431` neste ramo) é a
única chamada, e `linear_scan` só corre no **backend próprio** — a rota `TEKO_BACKEND=c` não
tem alocador de registos. Confirmado por medição: a sonda densa foi construída com o backend
por omissão (nativo, `src/build/project.tks:1899`) e o `sort_by_start` aparece no mapa de
arena; nenhuma construção de rota C o mostra.

**Quem o consome.** `linear_scan` percorre `sorted` por índice (base `:1367`) e a ordem decide a
escolha de registo (`candidate_pool`, base `:1370`), a decisão de derrame (`furthest_active`,
base `:1377`) e a ORDEM das entradas de `assignments`. Logo a permutação não é um detalhe: qualquer
diferença nela move os bytes emitidos.

---

## 2. A premissa do arquitecto — PROVADA ANTES da substituição, e depois tornada dispensável

O encargo era: provar que `interval_before` (`src/backend/regalloc.tks:1102`) é uma ordem
TOTAL, sem empates, porque disso dependeria a unicidade da permutação.

### 2.1 A prova empírica

Construí um compilador instrumentado em que o `sort_by_start` original varre a saída ordenada
e grita em `stderr` por cada par adjacente que `interval_before` não ordena em nenhuma direcção
(numa lista ordenada, todo o par empatado é adjacente — uma passagem linear vê-os todos).

| corpus | pares empatados |
|---|---|
| 58 projectos de `examples/**` (todos os `.tkp`) | **0** |
| sonda densa N=250 | **0** |

### 2.2 A prova estrutural

`virt` (`compute_intervals`, `src/backend/regalloc.tks:1001`) tem exactamente uma entrada por
elemento de `distinct_regs` com `is_phys == false`. `distinct_regs`
(`src/backend/regalloc.tks:732`) deduplica por `reg_eq` (`:525`), que compara
`(id, is_phys, reg_class)`. Dois intervalos virtuais distintos só podem partilhar `reg_id` se
diferirem em `reg_class` — e a classe de um VReg é fixada pela forma da instrução LIR que o
define (`src/backend/isel_arm64.tks:499`, `:527`, `:554`, … — `MRegClass::GPR`/`FPR` literais,
um por forma), sobre um contador de VRegs **único por função** (`src/lir/lir.tks:547`,
`fresh_vreg`). Não há caminho que dê duas classes ao mesmo `id`.

**A premissa MANTÉM-SE.** Não houve par sem ordem; não há paragem a reportar.

### 2.3 Porque a substituição não depende dela — e isso é mais forte

`interval_before` é uma ordem lexicográfica sobre o par `(start, reg_id)`, logo uma **ordem
fraca estrita**, com ou sem empates. E a ordenação por inserção retirada colocava cada `v`
imediatamente ANTES do primeiro `o` já colocado com `interval_before(v, o)` — isto é, DEPOIS de
todos os que não o precediam estritamente, incluindo os empatados. **Era uma inserção estável.**

`merge_runs` (a substituição) toma do lado esquerdo salvo se a cabeça direita precede
ESTRITAMENTE a esquerda — **fusão estável**. Duas ordenações estáveis sob a mesma ordem fraca
estrita coincidem elemento a elemento. Logo a permutação é idêntica **mesmo que a premissa
caísse**. O documento regista as duas provas porque a instrução pedia a primeira; a segunda é a
que sustenta a alegação.

---

## 3. A substituição

`src/backend/regalloc.tks:1106-1209`: fusão ascendente (bottom-up merge sort) estável.

* `run_end` (`:1116`) — fim de corrida, cortado ao comprimento.
* `merge_runs` (`:1137`) — fusão estável de duas corridas adjacentes sobre a cauda da saída.
* `merge_pass` (`:1168`) — **UMA lista de saída por PASSAGEM**, nunca uma por elemento.
* `sort_by_start` (`:1200`) — `ceil(log2 V)` passagens.

O(V log V) em tempo e em bytes alocados, contra O(V²) nos dois eixos.

---

## 4. A prova de que os bytes não se movem

### 4.1 O `.o` e o binário, contra o compilador da BASE do ramo — a prova directa

Compilador da base (`a2d832bc`) e compilador deste ramo, sobre o mesmo corpus, comparando o
objecto emitido com `cmp`:

```
RESULT same=51 different=0 no_object=9 exit_mismatch=0
binários ligados: same=102 different=0
```

51 objectos byte-a-byte idênticos, **0 diferentes**, incluindo as sondas densas
(`n250/dense.o` 25 416 B e `n500/dense.o` 49 664 B, ambos IDÊNTICOS). Os 9 projectos sem
objecto são fixtures de diagnóstico/known-stop que não chegam a emitir. Nenhum divergiu no
código de saída.

### 4.2 O `teko.c` gerado, contra o da base — **NÃO bate, e a razão é aritmética, não uma racionalização**

O encargo pedia comparar o `teko.c` gerado contra o da base. **Ele difere, e tinha de diferir:
o `teko.c` é a tradução em C da FONTE que eu editei.** Substituí uma função por quatro; o
emissor de C escreve quatro definições onde havia uma, mais três declarações antecipadas. Um
`teko.c` idêntico só seria possível se a fonte não tivesse mudado — isto é, se o trabalho não
tivesse sido feito. A alegação de byte-identidade nunca foi sobre o `teko.c` do compilador: é
sobre o CÓDIGO QUE O COMPILADOR EMITE para outros programas, e essa é a prova de §4.1.

Dito isso, o `teko.c` **ainda serve como prova de não-perturbação**, e serve bem:

| comparação | linhas alteradas |
|---|---|
| `diff` cru (226 574 vs 226 640 linhas) | 137 448 |
| depois de normalizar os contadores globais do emissor (`_xxN`, `tk_xxN`) | **214** |
| depois de normalizar também `tk_panic_oob_at(linha, coluna)` | **162** |

O `diff` cru é enorme por uma razão só: o emissor numera temporários com um contador GLOBAL
(`_cs892198` → `_cs892525`, `_arr892226` → `_arr892553`), e três funções novas deslocam todos
os números seguintes. Normalizados esses contadores, restam 214 linhas; normalizadas também as
`tk_panic_oob_at(linha, coluna)` — que carregam o número de LINHA de `regalloc.tks`, deslocado
porque a substituição é mais longa — restam **162 linhas, todas num único bloco contíguo**
(`33248`–`33311` do ficheiro base, que começa exactamente em
`tk_slice_LiveInterval teko_teko__backend__sort_by_start(...)` e acaba imediatamente antes de
`teko_teko__backend__expire_active`), mais as 3 declarações antecipadas na linha 9858.

**Fora das quatro funções que escrevi, o `teko.c` é idêntico.**

---

## 5. A curva remedida — sonda densa, 13 operações por linha

Sonda: `probe()` com N linhas de 5 atribuições encadeadas (`a=acc+k`, `b=a*3-k`, `c=b+a*2`,
`d=c-b+k`, `acc=a+b+c+d`) ≈ 13 operações e 5 VRegs vivos por linha. Gerador em
`/tmp/.../scratchpad/gen_dense.py`; os números absolutos NÃO são comparáveis com os do agente
anterior (a fonte da sonda dele não sobreviveu), só os expoentes e os rácios.

**ANTES** = base do ramo `a2d832bc`. **DEPOIS** = este ramo. Pico = o auto-reportado
`teko: memory: peak`.

| N | ANTES tempo | ANTES pico | DEPOIS tempo | DEPOIS pico | Δ tempo | Δ pico |
|---|---|---|---|---|---|---|
| 125 | 0,539 s | 124,4 MB | 0,495 s | 32,0 MB | −8,2 % | **−74,3 %** |
| 250 | 1,089 s | 451,3 MB | 0,857 s | 87,2 MB | −21,3 % | **−80,7 %** |
| 500 | 3,324 s | 1723,8 MB | 2,141 s | 289,3 MB | −35,6 % | **−83,2 %** |
| 1000 | 17,410 s | 6894,8 MB | 7,611 s | 1074,4 MB | **−56,3 %** | **−84,4 %** |
| 2000 | não medida (≈26 GB projectados — a caixa não aguenta) | | 35,241 s | 4289,4 MB | | |

### O expoente

| série | expoente global (125→1000) | expoente local no topo |
|---|---|---|
| ANTES, pico | **1,931** | 500→1000: 2,00 |
| DEPOIS, pico | **1,690** | 1000→2000: 2,00 |
| ANTES, tempo | 1,671 | 500→1000: 2,39 |
| DEPOIS, tempo | 1,314 | 1000→2000: 2,21 |

**O expoente do pico cai de 1,93 para 1,69, e não cai mais — porque não era só um
quadrático.** A base deste ramo NÃO contém o conserto do `vinfo_set` do ramo
`cargo/0.3.1.0-isel-quadratico` (`src/backend/isel_arm64.tks:91` na base ainda reconstrói as
três listas por escrita). Removido o `sort_by_start`, é o `vinfo_set` que passa a mandar, e ele
volta a puxar o expoente para 2 no topo da gama. §6 e §7 dão-lhe número e nome.

### Com os DOIS consertos — a projecção da integração, medida e não suposta

Árvore de trabalho separada = este ramo + `7fe67987` (`vinfo_set`) aplicado; **não faz parte
deste ramo**, é medição:

| N | AMBOS tempo | AMBOS pico |
|---|---|---|
| 125 | 0,377 s | 17,0 MB |
| 250 | 0,444 s | 26,2 MB |
| 500 | 0,719 s | 46,3 MB |
| 1000 | 1,850 s | 91,0 MB |
| 2000 | 7,054 s | **195,7 MB** |

Expoente global do pico, 125→2000: **0,881**; local no topo (1000→2000): **1,10**. **Os dois
consertos juntos transformam N^1,93 em N^1,1.** Em N=2000 o pico é 195,7 MB contra os 4289,4 MB
deste ramo sozinho (21,9×) e contra os ≈26 GB projectados da base.

---

## 6. `TEKO_ARENA_OBS` — o mapa antes e depois, sobre binário simbolizado

Binários religados com `-rdynamic` (via `TEKO_CC=…/cc-rdyn`, o seam de
`src/build/project.tks:719`) só para a medição. Sonda densa N=500.

### 6.1 ANTES (base do ramo)

```
root (process-lifetime, never freed):     2154.1 MB
=== PUSH copy-grow bytes by CALLING fn (RA1, #148): 2145.03 MB total ===
   0     1628.2 MB   115759 allocs  teko_teko__backend__sort_by_start
   1      313.7 MB   104211 allocs  teko_teko__backend__vinfo_set
   2       82.7 MB   111409 allocs  teko_teko__backend__vinfo_set
   3       82.7 MB   110513 allocs  teko_teko__backend__vinfo_set
   4        5.2 MB     5517 allocs  teko_teko__lir__list_set_u32
```

`sort_by_start` (dois sítios de retorno): **1628,8 MB / 115 785 alocações = 75,9 %** do
copy-grow. `vinfo_set` (três sítios): 479,1 MB / 326 133 = 22,3 %. Juntos, **98,2 %**.

### 6.2 DEPOIS (este ramo)

```
root (process-lifetime, never freed):      529.4 MB
=== PUSH copy-grow bytes by CALLING fn (RA1, #148): 520.30 MB total ===
   0      311.9 MB   104282 allocs  teko_teko__backend__vinfo_set
   1       82.7 MB   110632 allocs  teko_teko__backend__vinfo_set
   2       82.6 MB   111592 allocs  teko_teko__backend__vinfo_set
   3        5.2 MB     5518 allocs  teko_teko__lir__list_set_u32
   4        4.9 MB      183 allocs  teko_teko__backend__merge_runs
   5        3.8 MB    39436 allocs  teko_teko__backend__append_minst_x86
```

* **`sort_by_start` desapareceu do mapa.** O seu sucessor `merge_runs` está em **5,8 MB / 228
  alocações** (dois sítios), contra 1628,8 MB / 115 785: **−99,64 % em bytes, −99,80 % em
  alocações.**
* Raiz do processo: 2154,1 → **529,4 MB** (−75,4 %). Copy-grow: 2145,03 → **520,30 MB**
  (−75,7 %).

### 6.3 Quem ficou no lugar dele — **`vinfo_set`**

`teko_teko__backend__vinfo_set`, três sítios, **477,2 MB / 326 506 alocações = 91,7 % do
copy-grow restante**. É `src/backend/isel_arm64.tks:91` (a definição é única e serve as duas
ISAs — `isel_x86_64.tks` não redefine `vinfo_set`, chama esta). **Já está consertado no ramo
`cargo/0.3.1.0-isel-quadratico` (`7fe67987`) e ainda não integrado em
`remodel/0.3.1.0-linux-native-2`.** Não é trabalho novo: é uma integração por fazer.

### 6.4 E depois DESSE — com os dois consertos, não há dominante

```
root (process-lifetime, never freed):       48.5 MB
=== PUSH copy-grow bytes by CALLING fn (RA1, #148): 39.39 MB total ===
   0        5.2 MB     5513 allocs  teko_teko__lir__list_set_u32
   1        4.1 MB      179 allocs  teko_teko__backend__merge_runs
   2        3.4 MB    37730 allocs  teko_teko__backend__append_minst_x86
   3        2.5 MB       15 allocs  teko_teko__checker__lp_block
   4        2.5 MB       15 allocs  teko_teko__checker__fold_block
   5        2.5 MB       14 allocs  teko_teko__checker__type_block
```

Raiz 48,5 MB (−97,7 % contra a base), copy-grow 39,39 MB (−98,2 %). **O maior sítio vale
13,2 %.** Acabaram os dominantes de uma cabeça só no backend, nesta forma de sonda.

---

## 7. A varredura dos irmãos que o agente anterior contou

Medido no mapa de N=500, não deduzido. `AUSENTE` = zero bytes atribuídos, isto é, a função
**não corre nesta rota**.

| # | sítio | ANTES | DEPOIS | veredicto |
|---|---|---|---|---|
| 1 | `backend/isel_arm64.tks:91` `vinfo_set` | 479,1 MB / 326 133 (22,3 %) | 477,2 MB / 326 506 (**91,7 %**) | **passa a dominar**; conserto já existe em `7fe67987`, por integrar |
| 2 | `backend/regalloc.tks` `sort_by_start` | 1628,8 MB / 115 785 (75,9 %) | **AUSENTE** (→ `merge_runs`, 5,8 MB / 228) | **CONSERTADO aqui** |
| 3 | `lir/lir_oracle.tks:73` `rf_set` | AUSENTE | AUSENTE | **não corre na compilação** — é do motor legado de LIR |
| 4 | `backend/minst_oracle.tks:156` `iv_set` | AUSENTE | AUSENTE | **não corre na compilação** — motor legado de máquina |
| 5 | `backend/minst_oracle.tks:246` `mem_set` | AUSENTE | AUSENTE | **não corre na compilação** — idem |
| 6 | `backend/minst.tks:1451` `mreg_list_set` (+ `minst_x86.tks:1246`) | AUSENTE | AUSENTE | não aparece: o custo é O(params), abaixo do limiar do mapa |
| 7 | `backend/minst.tks:1404` `append_minst` (gémeo x86) | 5,6 MB / 39 416 (0,26 %) | 5,6 MB / 39 480 (**1,08 %**) | sobe em fracção, não em bytes; 3º no mapa dos dois consertos |

**A correcção que a medição faz ao relatório anterior:** `rf_set`, `iv_set` e `mem_set` foram
contados como irmãos do defeito, e têm mesmo a mesma FORMA — mas **nenhum deles corre no
caminho de compilação**. Vivem nos motores legados (`lir_oracle`, `minst_oracle`), que só o
portão de teste invoca. Encontrar o símbolo não é encontrar o comportamento: custam zero num
`teko build`.

Um irmão que o relatório anterior não listou e que o mapa mostra:
`teko_teko__lir__list_set_u32` (`src/lir/`), **5,2 MB / 5 517 alocações** — o maior sítio único
restante depois dos dois consertos (13,2 %). Está NOMEADO, não consertado: é fora do âmbito
deste encargo.

---

## 8. Ritual

| passo | resultado |
|---|---|
| `TEKO_BACKEND=c ./.teko/teko . -o out --no-verify --release` | **exit 0, zero avisos** (`grep -ic warning` no log = 0), pico 1724,8 MB, 92,9 s |
| `sh scripts/fixpoint_gate.sh out/teko . .fixpoint` | **VERDICT: PASSED — gen2 == gen3 byte a byte**; gen2 = 4 176 008 B. Backend: **rota C** (`TEKO_FIXPOINT_BACKEND` por omissão `c`, `scripts/fixpoint_gate.sh:97`), gen2/gen3 ainda emitem C — reportado, não imposto, migra em 0.3.1.4 |
| `teko.c` contra o da base | difere; §4.2 dá a razão aritmética e mostra que fora das quatro funções escritas é idêntico |
| `.o`/binários contra o compilador da base, 60 projectos | **51 objectos e 102 binários idênticos, 0 diferentes, 0 divergências de exit** |
| `./out/teko test .` | ver §9 |

## 9. O portão de teste completo

`./out/teko test .` **não terminou dentro do tempo desta sessão** e não é declarado nem verde
nem falhado. A fase de regressão da árvore não termina (medido também noutro ramo, morta dentro
do `own_native`) e não emite linha durante esse período. Os factos que ficam:

* as fixtures novas de `src/backend/regalloc_test.tkt` compilam e o `checker` passa as 8562
  entradas da árvore de teste;
* a única falha que o portão apontou foi minha e está corrigida (`regalloc_test.tkt:608:
  expected an expression`, uma expressão booleana continuada na linha seguinte — achatada em
  guardas, `dd242d07`);
* o delta desta mudança está provado pelo corpus de §4.1 (60 projectos compilados pelos dois
  compiladores, objectos idênticos) e pelas fixtures de §10.

**A exaustão do `own_native` é precisamente o alvo desta lane**: a base pedia 6894,8 MB para
N=1000 na sonda densa e este ramo pede 1074,4 MB.

## 10. As fixtures de regressão

`src/backend/regalloc_test.tkt` guarda a **ordenação por inserção retirada, verbatim**, como
oráculo (`sbs_reference`), e compara elemento a elemento — campo a campo, não só a permutação:

* `sbs_agrees_with_reference_over_pseudo_corpora` — tamanhos 0…64, duas dispersões de `start`
  (4096 e 5, esta última forçando muitos empates de `start` e exercitando o desempate por
  `reg_id` em cada nível de fusão);
* `sbs_output_is_sorted_and_length_preserving`;
* `sbs_degenerate_sizes_match_reference` — 0, 1, 2 elementos;
* `sbs_already_sorted_and_reversed_match_reference`;
* `sbs_stable_on_a_key_the_comparator_cannot_separate` — três intervalos com `(start, reg_id)`
  IGUAIS, distinguidos só por `end`: fixa a estabilidade que §2.3 exige, o caso que o corpus
  real não produz mas que a prova precisa;
* `sbs_linear_scan_visits_vregs_in_start_order` — pina o CONSUMIDOR, não só a ordenação.

Se um dia alguém trocar a fusão por uma ordenação não estável, é `sbs_reference` que falha, em
vez do código de máquina mudar em silêncio.
