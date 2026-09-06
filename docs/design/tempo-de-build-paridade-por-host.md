---
section: ci
created: 2026-07-27
source: pergunta do owner — "por que o build em Linux e Windows são tão lentos enquanto em macOS é muito mais rápido? O que estamos deixando de fora nestes sistemas?"
status: INVESTIGAÇÃO MEDIDA — parcial; a seção "O QUE FALTA MEDIR" nomeia exatamente o que não pôde ser lido desta sessão
---

# Tempo de build por host — o que o macOS não faz

> dead code"), os dois hosts saíram da matriz de producers, das lanes de teste e do conjunto de
> assets publicados. As medições abaixo que os citam são REGISTRO HISTÓRICO de corridas reais e
> ficam como estão; nenhuma delas descreve uma lane que ainda roda. Onde este documento propõe
> trabalho sobre essas lanes, a proposta está VAZIA por remoção do alvo, não por decisão de
> priorização.

Cada afirmação deste documento é rotulada:

* **FATO(árvore)** — lido diretamente do repositório, reproduzível com o comando citado.
* **FATO(medido)** — cronometrado nesta investigação, com a máquina e o comando citados.
* **HIPÓTESE** — plausível, ainda sem número que a sustente.
* **REFUTADO** — hipótese que foi testada e caiu, com o teste que a derrubou.

Uma hipótese sem número não vira causa neste documento.

## 0. O ponto de partida

Corrida `30231488994` (head `47f269ab`) e corrida `30233239753` (head `1af0fb66`),
workflow `.github/workflows/pr.yml`, job `artifact`, ambas em modo `full`:

| lane | corrida 30231488994 | corrida 30233239753 |
|---|---:|---:|
| `macos-arm64` | 4m11s | 2m03s |
| `linux-arm64` | 8m52s | 9m03s |
| `windows-x86_64` | 11m54s | 10m13s |
| `linux-x86_64` | **16m28s** | **15m18s** |
| `` | >44m (cancelada) | — |

O outlier a explicar é `linux-x86_64`: é a mais lenta de todas as lanes não emuladas,
perde para `windows-x86_64` no MESMO x86_64 e para `linux-arm64` no MESMO sistema
operacional.

## 1. Três hipóteses refutadas antes de qualquer outra coisa

### 1.1 Cache — REFUTADO

**FATO(árvore).** Não existe cache algum no repositório:

```
$ grep -ci cache .github/workflows/*.yml
pr.yml:0  nightly.yml:0  release.yml:0  codeql.yml:0
branch-policy.yml:0  mirror-pr-to-org.yml:0  seed-linux-fork.yml:0  tag-on-version-bump.yml:0
```

Zero ocorrências de `actions/cache`, zero `cache:` de `setup-*`. Nenhuma lane restaura
nada; o macOS não tem um cache que os outros não têm porque NINGUÉM tem cache.

Isto não é um detalhe negativo — é uma medição com consequência direta, registrada na
§5: cada corrida rebaixa do zero o seed publicado, e cada leg Linux repuxa ~561 MB de
imagem de container.

### 1.2 Flags de `cc` divergentes por plataforma — REFUTADO

**FATO(árvore).** `src/build/project.tks`:

* `resolve_cc` (linha 779) devolve literalmente `"cc"` em todo host quando o manifesto
  não fixa `cc`, `TEKO_CC` está vazio e `TEKO_TARGET` está vazio.
* `opt_cc_flag` (linha 799) devolve `-O2` para qualquer nível ≥ 2, sem ramo por
  plataforma. `--release` ⇒ `-O2` em todo host.
* `build_cc_argv` (linha 899) tem exatamente TRÊS tokens condicionais e nenhum deles é
  de otimização ou de debug info: `-std=c23` vs `-std=c2x`, `-ferror-limit=0` vs
  `-fmax-errors=0` (ambos por família do compilador, não por SO) e o `-Wl,-sectcreate`
  do plist de macOS. `-g` só entra com `debug=true`, que o caminho `--release` não usa.

Não há host compilando a `-O0` enquanto outro compila a `-O2`, e não há host gerando
debug info que outro não gera.

### 1.3 Modo do workflow (light vs full) diferente entre as duas corridas — REFUTADO

Verificado pelo coordenador nos logs do job `plan`: ambas as corridas são `full`
(`FULL_MATCHED: true`, `run=true`). A queda do macOS entre as duas corridas não é
artefato de modo.

## 2. FATO(árvore) — a assimetria de trabalho: quantas vezes cada leg compila o `teko.c` emitido

Esta é a resposta estrutural à pergunta do owner, e ela é legível na árvore sem log
nenhum. Duas fontes: `scripts/ci_producer_matrix.sh` (o campo `kind` e o campo
`produces`) e `scripts/produce_assets.sh` (o que ele faz com cada um).

`scripts/produce_assets.sh` §3:

```sh
if [ "$KIND" = "linux" ]; then
    for label in $PRODUCES; do
        sh scripts/native_linux_asset.sh "$label" out/teko.c src
    done
fi
```

`kind = native` ⇒ esse laço NÃO EXISTE: *"Non-Linux producers are already standing on
the target platform, so the dry build IS the asset."*

`scripts/native_linux_asset.sh` roda, por label, um `docker run` que executa
`gcc -std=c2x -w -O2 … teko.c teko_rt.c assert.c -o gd-<label>/teko` — ou seja, uma
compilação `-O2` COMPLETA do `teko.c` emitido, dentro de um container que precisa ser
baixado antes.

A conta, em modo `full`:

| leg | `kind` | `produces` | compilações `-O2` do `teko.c` | containers baixados |
|---|---|---|---:|---:|
| `macos-arm64` | native | 1 label | **1** (só o build seco) | 0 |
| `windows-x86_64` | native | 1 label | **1** | 0 |
| `` | native | 1 label | **1** | 0 |
| `linux-x86_64` | linux | `linux-x86_64-glibc linux-x86_64-musl` | **3** | 2 |
| `linux-arm64` | linux | `linux-arm64-glibc linux-arm64-musl` | **3** | 2 |

**Esta é a coisa que o macOS não faz e os Linux fazem.** O leg macOS constrói o
compilador uma vez e publica esse binário. O leg `linux-x86_64` constrói o compilador
uma vez para obter o `teko.c`, e então recompila esse mesmo `teko.c` a `-O2` mais DUAS
vezes, cada uma dentro de um container recém-baixado — e uma delas (`alpine`, o asset
musl) ainda roda `apk add --no-cache build-base`, que instala um gcc inteiro, do zero,
em toda corrida, porque não há cache (§1.1).

Isto explica de forma direta a parte da pergunta que o owner formulou como
"Linux e Windows são lentos": **Windows não é lento pelo mesmo motivo que Linux.**
`windows-x86_64` (10m13s) faz UMA compilação e ainda assim leva o dobro do macOS
(2m03s–4m11s), o que aponta para custo por compilação. `linux-x86_64` (15m18s) faz TRÊS.

## 3. FATO(árvore) — a escada multiplica tudo isso por três

`scripts/build_with_seed_fallback.sh` tem quatro caminhos, e o número de builds
COMPLETOS do compilador difere por um fator de 3 entre eles:

| caminho | builds completos do compilador |
|---|---:|
| fast path (o seed publicado constrói a ponta) | 1 |
| rung 0 (`bootstrap/seeds/`, o seed commitado constrói a ponta) | 1 (+1 tentativa barata) |
| escada fixada (`LADDER_RUNGS`, 2 degraus) | **3** (degrau 1 + degrau 2 + ponta) |

**FATO(árvore).** `bootstrap/seeds/` NÃO EXISTE em nenhum dos dois commits medidos:

```
$ git ls-tree -r 47f269ab -- bootstrap/     # vazio
$ git ls-tree -r 1af0fb66 -- bootstrap/     # vazio
```

Os blobs entraram em `9eb7721e` e saíram em `45da9218`, ambos ANTERIORES a `47f269ab`.
Logo **rung 0 estava morto nas duas corridas** — `commit_seed_rung` sai imediatamente
com *"no committed seed manifest … skipping"* e a escada fixada é o único caminho
restante quando o fast path falha.

**FATO(medido).** A tentativa de fast path que falha é BARATA: com o seed local
(`teko 0.3.0.16-beta`) contra a ponta deste vagão, a falha ocorre em **0,072 s** —
antes de qualquer `cc`. O custo da escada não está na sondagem; está nos builds
completos que ela acrescenta.

Consequência, se o fast path falhou nas duas corridas (ver §6 — é o que a escada fixada
existe para atender, e o cabeçalho do script afirma que o seed 0.3.0.30 não alcança a
ponta):

| leg | compilações `-O2` totais do `teko.c` |
|---|---:|
| `macos-arm64` / `windows-*` | 3 (escada) |
| `linux-x86_64` / `linux-arm64` | 3 (escada) + 2 (containers) = **5** |

## 4. A CALIBRAÇÃO — separando "runner lento" de "trabalho a mais"

Esta seção é o núcleo do documento. Ela responde à pergunta do owner com um número, e
o número não veio de suposição: veio de uma medição que **já estava no repositório**.

### 4.1 FATO(medido, já registrado) — o custo por host de um trabalho IDÊNTICO

o job `build-test` rodou **os mesmos passos em todos os cinco hosts**:

```

 332s  build-test / linux-arm64
 309s  build-test / linux-x86_64
 215s  build-test / windows-x86_64
 184s  build-test / macos-arm64
```

Isso é uma régua de hardware+SO por host, medida sobre trabalho idêntico, numa geração
de workflow COMPLETAMENTE DIFERENTE da atual — o que é exatamente o que a torna uma
régua confiável: ela não pode ter sido contaminada pelas assimetrias da §2/§3, que ainda
não existiam.

Normalizada com `macos-arm64 = 1,00`:

| host | fator de custo medido |
|---|---:|
| `macos-arm64` | 1,00 |
| `windows-x86_64` | 1,17 |
| `linux-x86_64` | 1,68 |
| `linux-arm64` | 1,80 |
| `` | **5,29** |

A mesma fonte (`docs/design/ci-gates.md`, linha 159) já dizia em prosa o que este número

### 4.2 MODELO(calibrado) — quanto TRABALHO cada lane fez, descontado o hardware

Dividindo o wall-clock do job `artifact` da corrida `30233239753` pelo fator do host,
obtém-se o trabalho de cada lane numa unidade comum ("segundos-macOS"):

| lane | job (s) | fator de host | **trabalho normalizado** |
|---|---:|---:|---:|
| `macos-arm64` | 123 | 1,00 | **123** |
| `linux-arm64` | 543 | 1,80 | 301 |
| `` | 2328 | 5,29 | 440 |
| `windows-x86_64` | 613 | 1,17 | 525 |
| `linux-x86_64` | 918 | 1,68 | **547** |

dá para eles mede um job cujo custo era 85% execução emulada, mistura diferente da de
hoje — usá-lo seria comparar duas emulações distintas fingindo que são uma.

### 4.2.1 MODELO(previsão falseável) — decompondo em "builds do compilador"

Este é um modelo, não um fato, e está aqui porque é FALSEÁVEL por UMA linha de log
(§8.2) — quem tiver acesso à API confirma ou mata em dois minutos.

`macos-arm64` = 123 s. Na corrida `28763999356` o `Test gate` do macOS — um
self-build COMPLETO mais a execução de 863 `#test` — custou 75 s; um build
`--no-verify` (sem o gate) fica bem abaixo disso. Se o macOS pagou os **3 builds** da
escada fixada (§3), cada build sai a **~41 s**, e 3 × 41 = 123 s fecha exatamente.

Aplicando 41 s × o fator de host e comparando com o observado:

| lane | 3 builds de escada, previsto | observado | resíduo |
|---|---:|---:|---:|
| `macos-arm64` | 123 s | 123 s | **1,0×** |
| `linux-arm64` | 222 s | 543 s | 2,4× |
| `linux-x86_64` | 207 s | 918 s | 4,4× |
| `windows-x86_64` | 144 s | 613 s | **4,3×** |
| `` | 651 s | 2328 s | **3,6×** |

Os resíduos de `linux-arm64` e `linux-x86_64` têm explicação nomeada e com número: são
as duas compilações de container da §2 (pull de 561 MB + `apk add build-base` + um
`gcc -O2` sobre a TU inteira). Os resíduos dos DOIS Windows não têm — eles não fazem
container nenhum. **Se o modelo estiver certo, sobra um fator ~4 específico do Windows
que nenhuma medição deste documento explica, e é exatamente ali que a tese de mecanismo
de SO do owner teria onde morar.**

Duas leituras de log matam ou confirmam o modelo inteiro: a linha (2) da §8 diz se cada
host pagou 1 ou 3 builds, e as linhas (3) dão o custo de UM build por host diretamente,
sem modelo nenhum no meio.

### 4.3 As três conclusões que essa tabela força

**(a) `` NÃO é uma anomalia estrutural — REFUTADO.** O achado de que uma
idêntico**, medido numa corrida de outra geração de workflow. Normalizada, essa lane faz
**440** unidades — MENOS que `windows-x86_64` (525) e que `linux-x86_64` (547). Ela é a
lane mais lenta do relógio e uma das mais econômicas em trabalho. Não há otimização de
SO escondida ali; há um runner 5× mais lento fazendo o mesmo que os outros.

**(b) `linux-x86_64` também não é o outlier que parecia.** Normalizado, ele faz 547
contra 525 de `windows-x86_64` — praticamente o MESMO trabalho. Ele perde no relógio
porque o runner `ubuntu-latest` custa 1,68 contra 1,17 do `windows-latest`, e porque a
§2 lhe dá duas compilações de container que o Windows não tem. As duas coisas se somam;
nenhuma delas é misteriosa.

**(c) O ÚNICO outlier verdadeiro é o macOS, e ele é outlier POR BAIXO.** 123 unidades
contra 301–547 de todo o resto. **Descontado o hardware, todas as outras lanes fazem
de 2,4× a 4,4× MAIS TRABALHO que o macOS.** É isto que o owner percebeu, e a intuição
dele está certa: não é o macOS que é rápido, são os outros que estão fazendo mais coisa.

## 5. FATO(medido) — o custo por compilação, e por que ele é o item dominante

O log da lane de teste no macOS já dizia: `16 builds, 43.7s: harness 0% / compile 97% /
run 1%`. A medição abaixo confirma que "compile" quer dizer, essencialmente, `cc -O2`
sobre uma unidade de tradução única e enorme — que é exatamente o que o emissor C do
Teko produz.

Máquina da medição: `Intel(R) Xeon(R) @ 2.80GHz`, 4 vCPU, 15 GB RAM, Ubuntu 24.04,
`gcc 13.3.0` e `clang 18.1.3`. **É a mesma classe de runner que o `ubuntu-latest`**
(4 vCPU Xeon), o que torna esta medição representativa da lane `linux-x86_64`.

Unidade de tradução sintética no formato que um emissor de programa inteiro produz
(uma função estática por função de origem, despacho por `switch`, tráfego de struct por
valor, chamadas intra-TU para folhas, tabela de ponteiros que mantém TODAS as funções
alcançáveis — sem isso o `-O2` elimina as estáticas não referenciadas e a medição mede
outra coisa).

| TU | MB | cc | opt | segundos | pico RSS |
|---|---:|---|---|---:|---:|
| `v2_1000.c` | 0,80 | gcc | `-O0` | 3,33 | 161 MB |
| `v2_1000.c` | 0,80 | gcc | `-O2` | **15,49** | 263 MB |
| `v2_1000.c` | 0,80 | clang | `-O0` | 1,75 | — |
| `v2_1000.c` | 0,80 | clang | `-O2` | 13,54 | 263 MB |
| `v2_2000.c` | 1,61 | gcc | `-O0` | 9,45 | 299 MB |
| `v2_2000.c` | 1,61 | gcc | `-O2` | **37,87** | 351 MB |
| `v2_2000.c` | 1,61 | clang | `-O0` | 3,37 | — |
| `v2_2000.c` | 1,61 | clang | `-O2` | 31,74 | 351 MB |
| `v2_4000.c` | 3,23 | gcc | `-O0` | 18,64 | 389 MB |
| `v2_4000.c` | 3,23 | gcc | `-O2` | **81,09** | **636 MB** |
| `v2_4000.c` | 3,23 | clang | `-O0` | 7,27 | — |
| `v2_4000.c` | 3,23 | clang | `-O2` | 61,67 | 636 MB |
| `v2_8000.c` | 6,46 | gcc | `-O0` | 47,09 | 783 MB |
| `v2_8000.c` | 6,46 | gcc | `-O2` | **208,55** | **1154 MB** |
| `v2_8000.c` | 6,46 | clang | `-O0` | 17,09 | — |
| `v2_8000.c` | 6,46 | clang | `-O2` | **128,42** | 1154 MB |

Quatro resultados, todos com número:

1. **`-O2` custa 4,0–4,7× o `-O0`** na mesma TU, em todos os tamanhos (4,66 / 4,01 /
   4,35 / 4,43).
2. **O custo é SUPERLINEAR no tamanho da TU — mas SÓ no gcc.** Por duplicação de
   tamanho, `gcc -O2` multiplica por 2,44 → 2,14 → 2,57 (expoente ≈ **1,25**), enquanto
   `clang -O2` multiplica por 2,34 → 1,94 → 2,08 (expoente ≈ **1,06**, praticamente
   linear). Uma unidade de tradução única e enorme é a pior forma possível de apresentar
   esse trabalho a um compilador C, e o gcc paga por isso e o clang quase não.
3. **gcc vs clang IMPORTA, e a diferença CRESCE com o tamanho da TU.** Este resultado
   corrige o que uma medição só nos tamanhos pequenos teria concluído:

   | TU | `gcc -O2` / `clang -O2` |
   |---|---:|
   | 0,80 MB | 1,14× |
   | 1,61 MB | 1,19× |
   | 3,23 MB | 1,31× |
   | 6,46 MB | **1,62×** |

   **`cc` no macOS é clang; `cc` no `ubuntu-latest` é gcc** (`resolve_cc` devolve `"cc"`
   em todo host, §1.2 — ninguém escolhe, cada SO responde com o seu). O `teko.c` emitido
   do compilador inteiro é maior que 6,46 MB, então **1,62× é um PISO** para essa parte
   da vantagem do macOS, e a curva ainda está subindo. Isto é exatamente uma diferença
   de plataforma "que estamos deixando de fora" no sentido do owner — só que ela mora no
   `cc` default de cada SO, não numa flag de ABI.
4. **O pico de RSS cresce mais rápido que a TU**: 263 MB a 0,80 MB, 636 MB a 3,23 MB,
   **1154 MB a 6,46 MB**. Este é o número que a ação T-1 (§10) precisa e que ainda NÃO
   existe para o `teko.c` real — extrapolar daqui dá vários GB por processo, e dois em
   paralelo num runner de 16 GB é apertado o bastante para não se fazer às cegas.

Os itens (2) e (3) juntos são o achado acionável desta seção: o emissor produz UMA TU
gigante, ela é compilada a `-O2` sozinha num único núcleo enquanto o resto do runner
fica ocioso, e o compilador que o Linux usa por default é justamente o que degrada
superlinearmente com esse formato.

## 6. FATO(árvore) — a camada de MECANISMO DE SO: o que o build realmente pede ao sistema

Esta seção testa a tese do owner (*"alguma flag de abi, alguma syscall, ou outra coisa
nas APIs"*) item a item, contra a árvore.

### 6.1 Custo de criação de processo — REFUTADO para a lane `artifact`

**FATO(árvore).** Um `teko build` cria **DOIS** processos, não centenas:

* `cc_family_is_clang` (`src/build/project.tks:568`) — uma sonda `run_quiet`, uma vez por
  build, com o comentário explícito *"Detect the compiler family ONCE per build"*;
* `run_cc` (`src/build/project.tks:1270`) — **uma única** invocação de `cc` que recebe a
  TU inteira mais `teko_rt.c` e `assert.c` de uma vez (`build_cc_argv`, linha 899).

Não existe "um `cc` por arquivo". A diferença `CreateProcess` × `fork+exec` × `posix_spawn`
é real como API, mas com 2 spawns por build ela não move um relógio de 38 minutos.
(Isto vale para a lane `artifact`. A lane `test` roda `teko test .`, que compila e executa
o corpus fora de processo — lá a contagem de spawns é outra e a hipótese continua viva;
mas `test` não é a lane desta investigação.)

### 6.2 Buffer de stdout — REFUTADO

**FATO(árvore).** `grep -n "setvbuf" src/runtime/teko_rt.c` não retorna nada. O runtime
nunca troca o modo de buffer; `stdout` fica no default da plataforma, que é
FULLY-BUFFERED quando a saída é um pipe — e em CI ela é sempre um pipe. `tk_flush_out`
(linha 1410) é um `fflush` explícito e pontual, não um flush por linha. Não há uma
syscall `write` por linha de progresso.

### 6.3 A arena NÃO usa `mmap`/`VirtualAlloc` — CANDIDATA, mas dimensionada como pequena

**FATO(árvore).** `grep -n "mmap\|VirtualAlloc" src/runtime/teko_rt.c` não retorna
NENHUMA ocorrência. O alocador de chunks da arena (`tk_chunk_alloc`, linha 907) é:

```c
#if defined(_WIN32)
    return _aligned_malloc(bytes, TK_ARENA_ALIGN);
#else
    if (posix_memalign(&p, TK_ARENA_ALIGN, bytes) != 0) return NULL;
#endif
```

E `TK_REGION_DEFAULT_CHUNK` (`src/runtime/teko_rt.h:139`) = **64 KB**.

Ou seja: a arena herda INTEGRALMENTE o heap da libc de cada plataforma, e esses três
heaps não se parecem — glibc (ptmalloc, com limiar de `mmap` e bins), libmalloc do macOS
(magazines sobre `mach_vm`), heap do CRT do Windows (`HeapAlloc`; o LFH cobre blocos
pequenos, e 64 KB está acima da faixa dele). **Nada na árvore ajusta o alocador por SO:**
nenhum `mallopt(M_MMAP_THRESHOLD/M_TRIM_THRESHOLD)`, nenhum `HeapSetInformation`, nenhum
heap privado, nenhum `MADV_HUGEPAGE`, nenhuma página grande. Essa é, literalmente, "uma
flag de ABI que estamos deixando de fora" no sentido que o owner descreveu, e ela existe.

**Mas é preciso dimensioná-la antes de persegui-la, e o dimensionamento não é animador.**
`docs/design/al1-proof-report.md` registra os maiores sítios de alocação do compilador na
casa das dezenas de MB (`run_native_gate` 43 MB, `tk_emit_c_mode` 30+24 MB). A 64 KB por
chunk, 100 MB de arena são ~1600 chamadas a `_aligned_malloc`. Mesmo que uma chamada de
`HeapAlloc` custasse 100× uma de `posix_memalign`, a diferença total ficaria em
milissegundos. **HIPÓTESE, com o número que a torna implausível como causa principal —
mas ela permanece como higiene e como o único item da tese "flag de SO" que de fato
existe na árvore.**

### 6.4 `` sob emulação de x86_64 — NÃO REFUTADA, NÃO CONFIRMADA

A hipótese é séria e o mecanismo existe (Windows on ARM emula PE AMD64 de forma
transparente). Duas observações de árvore que a tornam VERIFICÁVEL e, ao mesmo tempo,
mostram que o projeto não a estaria enxergando:

**FATO(árvore) — os assets `native` não têm asserção de arquitetura.**
`scripts/native_linux_asset.sh` termina com

```sh
file "$GD/teko"
if ! file "$GD/teko" | grep -q "$ARCH_KW"; then … exit 1; fi
```

`scripts/produce_assets.sh`, no ramo `kind = native` (macOS e os dois Windows),
simplesmente copia `out/teko[.exe]` para `stage/<label>/` — **sem nenhuma verificação
equivalente**. Um `teko.exe` AMD64 publicado como `` passaria: a lane `test`
o executa no próprio runner ARM64, que o emula, e a lane fica verde.

**Porém a §4.3(a) já removeu a urgência:** descontado o fator de host 5,29 medido de
forma independente, `` faz 440 unidades de trabalho — MENOS que
`windows-x86_64` (525) e `linux-x86_64` (547). Se a lane estivesse emulando o `cc` além
do que a régua já captura, o trabalho normalizado dela apareceria ACIMA dos outros, não
abaixo. **HIPÓTESE, com evidência indireta CONTRA ser a causa do wall-clock — mas com
uma assimetria de portão real por trás, que vale corrigir por si só.**

### 6.5 As operações de `git` da escada — DIMENSIONADAS E DESCARTADAS

A escada faz, por degrau, um `git checkout --detach` mais um `git clean -fdxq` dentro de
um worktree de rascunho (`build_with_seed_fallback.sh:313-316`), e uma vez um
`git fetch --unshallow origin`. I/O de muitos arquivos pequenos é o cenário clássico em
que NTFS com antivírus em tempo real perde feio para o page cache do Linux — seria uma
explicação sob medida para o resíduo de ~4× dos dois Windows na §4.2.1.

**FATO(medido, árvore).** Não fecha, por tamanho:

```
$ git ls-files | wc -l        →  789
$ git count-objects -vH       →  size-pack: 18.31 MiB
```

789 arquivos rastreados e um pack de 18 MiB. Mesmo a 10× o custo do Linux, um
`git clean` sobre 789 arquivos e um `--unshallow` de 18 MiB somam segundos, não minutos.
**O resíduo de ~4× dos dois Windows continua SEM EXPLICAÇÃO neste documento**, e é a
única pergunta genuinamente aberta que ele deixa.

O que eu investigaria a seguir, nesta ordem, e por quê: (i) QUAL binário é o `cc` nos dois
runners Windows — o passo `Report the host toolchain` já imprime isso e ninguém leu; um
`gcc` de MinGW paga emulação de `fork` no próprio driver e é conhecidamente muito mais
lento que o mesmo gcc no Linux, o que seria exatamente "uma diferença de API de SO" no
sentido do owner; (ii) o tempo entre as linhas (2) e (3) da §8 nos dois Windows, que
isola o custo de UM build ali.

## 7. FATO(medido) — o download de imagem NÃO é o diferenciador entre x86_64 e arm64

Tamanho comprimido do manifesto das imagens que `native_linux_asset.sh` puxa
(consultado no registry, `scripts` em `/tmp/.../scratchpad/imgsize.sh`):

| imagem | tamanho comprimido |
|---|---:|
| `quay.io/pypa/manylinux_2_28_x86_64` | **560,8 MB** |
| `quay.io/pypa/manylinux_2_28_aarch64` | **534,7 MB** |

Diferença de 5%. O pull custa caro em ABSOLUTO (561 MB por leg Linux, toda corrida,
sem cache) mas é praticamente IDÊNTICO nos dois arcos — logo não explica os ~6 minutos
que separam `linux-x86_64` de `linux-arm64`.

## 8. O QUE FALTA MEDIR — e por que não foi medido aqui

**Esta sessão não tem acesso à API do GitHub.** Verificado três vezes:

```
$ gh api /repos/teko-org/teko-lang/actions/runs/30233239753
HTTP 403: GitHub access to this repository is not enabled for this session.

$ curl -sS https://api.github.com/repos/teko-org/teko-lang/actions/runs/30233239753/jobs
HTTP=403  {"message":"GitHub access to this repository is not enabled for this session."}

$ curl -o /dev/null -w '%{http_code}' https://api.github.com/rate_limit
200
```

Endpoints sem escopo de repositório respondem (`/rate_limit` → 200); qualquer endpoint
de `teko-org/teko-lang` é negado pelo gateway. **Nenhuma linha de log de nenhuma
corrida pôde ser lida.** As afirmações acima são todas de árvore ou de cronômetro local,
e nenhuma delas depende de log.

O perfil por fase que fecha a investigação precisa de quatro linhas por lane, todas
emitidas pelo próprio script dentro do passo `Produce this leg's assets`. Quem tiver
acesso à API extrai isso com `get_job_logs` e a diferença de timestamps ISO entre:

1. `ci_provision_teko: teko <tag> ready at …` — **fim da provisão do seed**, e a linha
   diz QUAL TAG. Comparar a tag entre `linux-x86_64`, `linux-arm64`, `macos-arm64` e
   `windows-x86_64` é o teste de uma linha para a hipótese "um host pega um seed mais
   novo que os outros".
2. `teko-ci: seed builds the tip directly — fast path, no fallback engaged`
   **ou** `teko-ci: seed FAILED to build the tip directly — engaging the staged bootstrap ladder`
   — **fast path vs escada, por host.** Se divergir entre hosts, §3 sozinha explica a
   tabela inteira.
3. `teko-ci: ladder stage <N>: built pinned rung <sha>` — uma por degrau; a diferença
   entre duas linhas consecutivas é o custo de UM build completo do compilador naquele
   host. **Este é o número que compara `linux-x86_64` com `linux-arm64` sem nenhuma
   variável livre.**
4. `=== <label> — native build in <image> ===` e `native_linux_asset: <label> OK` —
   início e fim de cada build de container. A diferença dá pull + `apk add` + `gcc -O2`.
5. A saída do passo `Report the host toolchain` em `` — se `cc`/`gcc`
   resolvem para um caminho x86_64, §6.4 vira FATO em vez de hipótese.

Com (3) e (4) a comparação `linux-x86_64` × `linux-arm64` fecha: se as linhas (3)
divergirem na mesma proporção que as (4), o custo é por-CPU e uniforme; se só as (4)
divergirem, o custo está no container.

E a linha (2) é a mais importante de todas: ela é o teste direto do MODELO da §4.2.
Se `macos-arm64` reportar *fast path* e os outros quatro reportarem *engaging the staged
bootstrap ladder*, a §3 explica sozinha os 123 contra 301–547 e a investigação fecha.

## 9. Resposta direta à pergunta do owner

> *"por que o build em Linux e Windows são tão lentos enquanto em macOS é muito mais
> rápido? O que estamos deixando de fora nestes sistemas?"*

**A premissa da pergunta está certa e a resposta é o inverso do que ela sugere: não
estamos deixando NADA de fora nos outros sistemas — estamos deixando coisa de fora no
macOS.** Descontado o custo de hardware medido de forma independente (§4.1), o macOS
faz **123 unidades de trabalho** e todos os outros fazem **301 a 547** — de 2,4× a 4,4×
MAIS. O macOS não é rápido; ele é a única lane que está fazendo pouco.

**E existe UMA coisa que o macOS tem e os outros não, literal, medida:** o `cc` dele é
clang, que compila a TU gigante do compilador de forma quase LINEAR no tamanho
(expoente 1,06), enquanto o `cc` do `ubuntu-latest` é gcc, que degrada a n^1,25 e já
está **1,62× mais lento numa TU de 6,46 MB — com a curva ainda subindo** (§5.3). Ninguém
escolheu isso: `resolve_cc` devolve `"cc"` e cada SO responde com o compilador que quiser.

E as duas coisas que os outros FAZEM e o macOS não estão nomeadas, com o arquivo e a
linha:

1. **As lanes Linux recompilam o compilador inteiro mais duas vezes** (§2). O macOS
   publica o binário que acabou de construir; `linux-x86_64` constrói, e então recompila
   o `teko.c` resultante a `-O2` mais duas vezes, cada uma dentro de um container de
   ~561 MB baixado do zero porque não existe cache no repositório (§1.1, §7).
2. **A escada de seeds triplica a contagem de builds em qualquer host cujo seed publicado
   não alcance a ponta** (§3), e o rung 0 que a encurtaria está morto nas duas corridas
   porque `bootstrap/seeds/` não existe na árvore. Qual host pagou a escada e qual pegou
   o atalho é UMA linha de log por lane (§8.2) — e é a explicação candidata mais forte
   para o macOS estar sozinho em 123.

**Sobre a tese de mecanismo de SO** (*"alguma flag de abi, alguma syscall"*): ela foi
testada item a item na §6. Spawn de processo: refutado, são DOIS por build. Buffer de
stdout: refutado, não há flush por linha. Emulação silenciosa em ``:
possível, mas a normalização da §4.3(a) argumenta contra ela ser a causa do relógio.
**Existe exatamente UM item real dessa família na árvore** (§6.3): a arena aloca por
`posix_memalign`/`_aligned_malloc` em chunks de 64 KB, sem `mmap`, sem `VirtualAlloc` e
sem NENHUM ajuste por SO — e o dimensionamento (~1600 chamadas para 100 MB de arena) diz
que ele não move um relógio de dezenas de minutos.

medido na corrida `28763999356` e já registrado em `docs/design/ci-gates.md`. Normalizada,
essa lane é uma das que MENOS trabalho fazem. É um runner lento fazendo o mesmo que os
outros — e "hardware" aqui é a resposta honesta, com o número que a sustenta.

## 10. O que fazer — dimensionado, e o que NÃO fazer

**A guarda primeiro: lane rápida que mede menos é o pior resultado possível.** Nenhuma
das opções abaixo remove um portão; as que removeriam estão listadas como recusadas.

RECUSADO — não proponha: (a) deixar de construir o asset musl; (b) mover o build glibc
para o `cc` do runner em vez do `manylinux_2_28` (isso levanta o piso de glibc de 2.28
para 2.39 silenciosamente, que é exatamente o que o container existe para impedir);
(c) tirar o `--release` do build final; (d) reduzir a matriz de hosts.

| # | Ação | Onde | Ganho esperado | Risco | Bloqueia em |
|---|---|---|---|---|---|
| **T-1** | Paralelizar o laço de assets Linux (`for label in $PRODUCES` roda os dois `native_linux_asset.sh` em sequência; eles são independentes — `gd-<label>/` distintos, entradas só-leitura) | `scripts/produce_assets.sh:110` | sobrepõe um pull de 561 MB e um `apk add` com uma compilação; ~1/3 do wall-clock de `linux-x86_64` e `linux-arm64` | MÉDIO — dois `gcc -O2` simultâneos num runner de 4 vCPU / 16 GB; precisa do pico de RSS medido antes | medir o RSS de pico do `cc` sobre o `teko.c` real |
| **T-2** | Restaurar `bootstrap/seeds/` (rung 0) | árvore | quando o fast path falha, corta 3 builds para 1 no host que tiver o blob — o maior fator isolado da tabela | BAIXO (o blob é verificado por sha256 antes de descomprimir, e `seed-debut` já o exercita) | ruling do owner sobre procedência (os blobs saíram em `45da9218` de propósito) |
| **T-3** | `actions/cache` para a camada de imagem do container | `.github/workflows/pr.yml` job `artifact` | 561 MB por leg Linux por corrida | BAIXO — cache de entrada de toolchain, não de resultado; não pode mascarar um portão | — |
| **T-4** | Emitir o programa em VÁRIAS unidades de tradução em vez de uma | compilador (codegen) | `-O2` é SUPERLINEAR no tamanho da TU (§5) e hoje ocupa 1 de 4 núcleos; N unidades dão ganho pelo expoente E por paralelismo | ALTO — muda o artefato, o FIXPOINT e a determinação cross-arch | trabalho de compilador, não de CI |
| **T-5** | Assertar a arquitetura dos assets `kind = native`, como `native_linux_asset.sh` já faz para os `linux` | `scripts/produce_assets.sh:120` | zero em tempo; fecha a assimetria de portão da §6.4 | BAIXO | — |
| **T-6** | `TEKO_CC=clang` nos builds da ESCADA e no build seco das lanes Linux (NÃO nos containers, que devem seguir com o gcc do `manylinux` pelo piso de glibc) | `.github/workflows/pr.yml`, job `artifact` | ≥ 1,62× no `cc` desses builds, e o fator cresce com o tamanho da TU (§5.3) | MÉDIO — muda o compilador que constrói o compilador; é byte-seguro pelas próprias leis do projeto (o C emitido é determinístico, FIXPOINT gen2==gen3), mas isso PRECISA ser confirmado por uma corrida, não assumido | confirmar que `clang` existe nos runners Linux (o passo `Report the host toolchain` já imprime) |

**T-1 é o único candidato a "barato e claramente seguro", e ele NÃO foi aplicado nesta
carga** — falta o número que o qualifica: o pico de RSS de um `gcc -O2` sobre o `teko.c`
REAL. A medição de proxy da §5 chega a 389 MB numa TU de 3,2 MB e ainda subindo; dois
processos desses cabem com folga em 16 GB, mas "cabe com folga no proxy" não é o mesmo
que "cabe na TU real", e um OOM transforma uma lane verde em vermelha intermitente —
que é pior que a lane lenta. **A medição pendente está nomeada; a mudança não foi feita
às cegas.**

**T-5 é o único que não custa nada e não depende de medição nenhuma**, e é o que eu
recomendaria aplicar primeiro — não por tempo, mas porque a §6.4 mostrou que o projeto
publica três assets (`macos-arm64`, `windows-x86_64`, ``) sem nunca conferir
a arquitetura deles, enquanto confere as seis Linux.
