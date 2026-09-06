# O arena em Teko — projeto

Carga `cargo/20-arena-teko`, vagão 20. Decreto do owner, sem exceção: *"a arena vai
para teko"*. Este documento é a Fatia 2: a estrutura de dados, o mapa das 12 funções,
e — o achado que domina o projeto — as **duas lacunas de linguagem** que separam o
arena em Teko de existir.

Fonte de verdade semântica: `src/runtime/teko_rt.c`, linhas ~864-1400 (o bloco
"Arena allocation (S1)") mais `tk_free_block` / `tk_free_take` / `tk_free_purge` e o
`TEKO_MEM_PARANOID` em `tk_free_block`. A carga irmã `cargo/20-expurgo-c-linker`
remove esse C; se ele sumir da base, recupere com `git show`.

---

## 1. Fatia 1 — o fundo, PROVADO

`examples/probes/arena_bottom`. Um `extern fn` Teko alcança a libc diretamente, no
backend nativo, sem hop pela stdlib:

```
pub extern fn c_aligned_alloc(alignment: u64, size: u64): u64  = "aligned_alloc"
pub extern fn c_free(block: u64) = "free"
pub extern fn c_memset(block: u64, value: i32, count: u64): u64 = "memset"
pub extern fn c_memcmp(left: u64, right: u64, count: u64): i32 = "memcmp"
```

```
nm -u bin/arena_bottom  ->  U aligned_alloc@GLIBC_2.16
./bin/arena_bottom      ->  exit 42
```

O probe aloca chunks de 64 KB alinhados a 16 (o `TK_REGION_DEFAULT_CHUNK` e o
`TK_ARENA_ALIGN` do gêmeo em C), exige base não-nula e alinhada, **escreve** através
de offsets de bump calculados em Teko, lê de volta, libera e realoca. Qualquer exit
diferente de 42 nomeia a invariante que quebrou (10..17).

Três fatos que o projeto herda daí:

1. **O endereço é `u64`, não `uptr`.** O checker não define cast `uptr <-> u64`
   (`typer.tks::cast_kind` só admite `Prim` e `Byte`), e um arena É aritmética de
   endereço: sem carrier numérico não há bump pointer. `u64` é ABI-idêntico a
   `void *` em alvo de 64 bits, então o binding é fiel.
2. **Só o backend nativo hospeda esse fundo.** O backend C emite protótipo próprio
   para cada símbolo `extern` e colide com `<stdlib.h>` (`conflicting types for
   'aligned_alloc'`). O nativo emite a chamada ao símbolo, sem protótipo.
3. **`panic(msg)` com `str` local não passa no nativo** (`N1: not a fat-pointer
   local`), então cada invariante do arena falha com exit code próprio.

---

## 2. A resposta à pergunta — por que ainda não existe arena em Teko

O bloqueio **não** é o fundo: o fundo está provado acima. São duas lacunas, ambas
pequenas, ambas já presentes na LIR — o que falta é a superfície que as alcance.

### P1 — ler e escrever uma palavra em um endereço calculado

Um arena guarda o próprio estado **dentro** da memória crua que administra: em C,
`struct tk_chunk { next; cap; used; data[] }` mora nos primeiros 32 bytes do próprio
chunk. Teko hoje não tem como ler nem escrever essa palavra:

* não há `teko::mem::load_u64(addr)` nem `store_u64(addr, v)` em
  `src/checker/scope.tks::builtin_fn`;
* `teko::mem::bytes_from_ptr(p, n)` lê memória crua, **mas aloca** o `[]byte` de
  retorno — e alocar é justamente o que o arena está tentando fazer: circularidade,
  a mesma que travou `src/runtime/teko_rt.tks`;
* não existe cast `u64 -> ptr<byte>`, então nem o caminho alocante é alcançável a
  partir de um endereço calculado.

**Custo:** duas entradas em `builtin_fn` e dois braços em `src/lir/lower.tks`. Cada
um lowera para **uma** instrução LIR que já existe — `LLoad { addr; ty }` e
`LStore { addr; value; ty }` (`src/lir/lir.tks:92-98`), as mesmas que todo acesso a
campo de struct já emite. Nenhuma máquina nova.

**A propriedade que importa:** load/store crus **não alocam**. É isso, e só isso, que
quebra a circularidade. Toda a bookkeeping do arena passa a caber na memória que o
próprio arena administra, exatamente como no C.

### P2 — uma palavra mutável que sobrevive entre chamadas

`tk_alloc` é chamado por código gerado que não carrega estado nenhum. O arena precisa
de raízes de processo: `tk_g_root`, `tk_g_regs`, `tk_g_region_gen`, a pilha de marcas,
os bins da free-list. Teko tem `const` de módulo (#594) e `static fn` de tipo
(`oop-this-base-static.md`), **nenhum estado mutável de módulo**. O compilador em Teko
é escrito sem globais — ele threada estado por parâmetro, o que `tk_alloc` não pode.

**O arena precisa de exatamente UMA palavra**, não de seis: um slot
`ARENA_CONTROL: u64` com o endereço do bloco de controle. Todo o resto mora **dentro**
desse bloco, alcançado por P1.

**Custo:** superfície de declaração para um slot mutável de módulo, lowerando para
`LGlobalAddr { symbol }` (`lir.tks:100-104`) apontando para `.bss` em vez de rodata.
A instrução existe; o que muda é a seção.

**Alternativa sem mudança de linguagem (fallback):** mapear o bloco de controle em um
endereço virtual FIXO, escolhido em build time, via
`extern fn c_mmap(...): u64` com `MAP_FIXED|MAP_ANONYMOUS|MAP_PRIVATE` (e
`VirtualAlloc` no Windows). O endereço do bloco vira uma **constante**, e uma palavra
mágica no offset 0 diz se ele já foi inicializado — lida por P1. Isto elimina P2 por
completo ao custo de um endereço codificado e de introduzir `mmap` onde hoje só há
`posix_memalign`. Fica registrado como plano B; P2 é mais honesto.

### P3 — a string C do probe de ambiente (menor)

`TEKO_MEM_PARANOID` e lido por `getenv` uma vez, preguicosamente. `getenv` quer um
`char *` NUL-terminado, e Teko nao sabe pegar o endereco de um literal em rodata. O
caminho limpo e `LGlobalAddr` sobre a tabela de literais que a LIR ja mantem.

Sem P3, a implementacao de referencia SOLETRA o nome byte a byte num bloco cru
(`control.tks::spell_paranoid_var`) e chama `getenv` direto. Funciona, nao aloca no
caminho do arena, e nao e bonito. `teko::env::var` nao serve por dois motivos
independentes: devolve `str | error`, e o backend nativo nao lowera `match` sobre
receptor fat-pointer (secao 7, defeito 2); e o resultado dela seria alocado.

---

## 3. A estrutura de dados

Tudo em memória crua, endereçado por palavra. Todo bloco vem de
`c_aligned_alloc(16, n)` e volta por `c_free` — a mesma família de alocador do gêmeo
em C (`tk_chunk_alloc` / `tk_chunk_free`), então nenhum ponteiro interior chega ao
desalocador.

### 3.1 CHUNK — 32 bytes de cabeçalho, payload logo depois

Espelha `struct tk_chunk`, cujo `offsetof(data)` é 32 por causa do
`_Alignas(TK_ARENA_ALIGN)` no membro flexível. 32 é múltiplo de 16, então o payload
nasce alinhado.

| offset | campo   | significado                                        |
|-------:|---------|----------------------------------------------------|
|      0 | `next`  | próximo chunk da região (0 = fim); lista LIFO      |
|      8 | `cap`   | bytes de payload utilizáveis                       |
|     16 | `used`  | bump: bytes já entregues                           |
|     24 | —       | padding, mantém o payload em 32 (múltiplo de 16)   |
|     32 | payload | `cap` bytes                                        |

Tamanho do bloco = `32 + cap`. O chunk padrão pede `cap = 65536`
(`TK_REGION_DEFAULT_CHUNK`).

### 3.2 REGION — 64 bytes

Espelha `struct tk_region`. Sete palavras, arredondadas para 64.

| offset | campo         | significado                                                     |
|-------:|---------------|-----------------------------------------------------------------|
|      0 | `head`        | chunk corrente (0 = região vazia, alocação preguiçosa)          |
|      8 | `reg_next`    | ligação INTRUSIVA no registro global de regiões vivas           |
|     16 | `parent`      | aresta da ÁRVORE do arena (0 = raiz, ou região sem pai)         |
|     24 | `entries`     | array (type_id, instance) do registro por região (0 = ausente)  |
|     32 | `nentries`    | entradas em uso                                                 |
|     40 | `entries_cap` | capacidade do array                                             |
|     48 | `gen`         | carimbo de geração único (nunca reusado)                        |
|     56 | —             | padding                                                         |

`reg_next` e `parent` são **listas distintas**: `reg_next` é a lista plana global de
regiões vivas, sem relação com a forma da árvore; `parent` é a árvore que
`drop_subtree` percorre. Confundir as duas é o erro que corrompe silenciosamente.

### 3.3 ENTRIES — array de pares, crescimento por dobra

Par `(type_id@0, instance@8)`, 16 bytes. Capacidade 0 → 4 → 8 → 16 → … Crescer é
`c_aligned_alloc` do novo tamanho + `c_memcpy` do antigo + `c_free` do antigo (o C usa
`realloc`; a forma aqui mantém a família do alocador alinhado, e o efeito observável é
idêntico).

### 3.4 CONTROL — o bloco único que P2 endereça

| offset | campo            | espelha                                     |
|-------:|------------------|---------------------------------------------|
|      0 | `magic`          | sentinela de "já inicializado"              |
|      8 | `g_regs`         | `tk_g_regs` — cabeça do registro de regiões |
|     16 | `g_root`         | `tk_g_root` — a região raiz (preguiçosa)    |
|     24 | `g_region_gen`   | `tk_g_region_gen` — contador monotônico     |
|     32 | `arena_msp`      | `tk_arena_msp` — topo da pilha de marcas    |
|     40 | `paranoid`       | cache do probe de `TEKO_MEM_PARANOID`       |
|     48 | `free_large`     | `tk_free_large` — lista dos blocos > 4096   |
|     56 | —                | reservado                                   |
|     64 | `marks[64]`      | `tk_arena_marks` — 64 pares (chunk, used)   |
|   1088 | `free_bins[4096]`| `tk_free_bins` — classes de 16 em 16 bytes  |

Total 33 856 bytes, um bloco só, alocado uma vez.

### 3.5 FREENODE — o nó da free-list, dentro do bloco liberado

`{ next@0, bytes@8 }`, exatamente como `tk_freenode`. Um bloco com menos de 16 bytes
utilizáveis não é parkeável — vaza, porque o bump não encolhe. Igual ao C.

---

## 4. As 12 funções — o mapa

Todas operam sobre os layouts acima com `load`/`store` (P1). Nenhuma aloca em Teko;
toda memória vem de `c_aligned_alloc`.

| # | função | mapeamento |
|--:|--------|------------|
| 1 | `tk_region_root` | lê `CONTROL.g_root`; se 0, `region_new(0)` e grava. O `atexit(tk_regions_free_all)` do C vira responsabilidade do epílogo do `main` gerado + dos pontos de terminação (`exit`, `panic`) — Teko não tem como passar um ponteiro de função para `atexit` sem superfície nova (`LFuncAddr` existe, mas não é alcançável do fonte). **Ponto de atenção: sem isso o programa termina leak-dirty**, o que muda o comportamento observável sob ferramenta de vazamento. |
| 2 | `tk_region_new(parent)` | `c_aligned_alloc(16, 64)`; `head=0`; `reg_next=CONTROL.g_regs`; `CONTROL.g_regs=r`; `parent=parent`; `entries=nentries=entries_cap=0`; `gen = ++CONTROL.g_region_gen` (0 nunca é atribuído, então cache zerado nunca casa com região viva). |
| 3 | `tk_region_alloc(r, n)` | `n==0 -> 1`; `an = (n+15) & ~15`; se `r == g_root`, tenta `free_take(an)` primeiro; se `head != 0`, `base = (used+15) & ~15` e cabe quando `base <= cap && an <= cap - base` → `used = base+an`, devolve `head+32+base`; senão chunk novo `want = max(an, 65536)`, com **retry em `an`** se o `want` falhar (mantém o OOM na mesma borda do `malloc(n)` antigo), `used=an`, prepend, devolve `c+32`. |
| 4 | `tk_region_drop(r)` | `r==0` tolerado; desliga do registro global PRIMEIRO (para `free_all` nunca ver região já solta); `reg_next=0`; `c=head`; `head=0` ANTES de liberar (idempotência em segunda passada); libera cada chunk; libera `entries`; libera o header. |
| 5 | `tk_region_drop_subtree(root)` | `root==0` tolerado. Duas fases, como no C: **(a)** varre `g_regs` coletando toda região cuja cadeia `parent` alcança `root`, desligando cada uma do registro e prendendo numa lista LOCAL threadada pelo próprio `reg_next`; **(b)** libera cada uma da lista local. A separação é o que impede que a varredura seja perturbada pelo desligamento, e o que impede double-free numa segunda passada. |
| 6 | `tk_region_register(r, tid, inst)` | `r==0` tolerado; varre `entries` e SOBRESCREVE se `tid` já existe; senão cresce (`cap 0->4`, depois dobra) e anexa. Sobrescrever é primitiva de armazenamento — erro de registro duplicado é camada de DI, não do arena. |
| 7 | `tk_region_lookup(r, tid)` | sobe `r`, `r.parent`, `r.parent.parent`, … varrendo `entries` de cada; devolve `instance` ou 0. |
| 8 | `tk_regions_free_all()` | `free_purge()`; destaca a lista inteira ANTES de liberar (reentrância); `g_regs=0`; `g_root=0`; libera chunks + `entries` + header de cada. Segunda chamada é no-op. |
| 9 | `tk_arena_push()` | se `0 <= msp < 64`, grava `marks[msp] = (root.head, root.head ? root.head.used : 0)`; `msp += 1` **sempre** (push fundo demais não grava marca, mas mantém push/pop balanceados). |
| 10 | `tk_arena_pop()` | `msp<=0` → no-op; `msp -= 1`; `msp>=64` → retorna sem rebobinar (aquele push não gravou nada); `free_purge()` (blocos parkeados podem morar nos chunks que somem); purga do push-cache; libera cada chunk de `root.head` até `m.chunk`; `root.head = m.chunk`; se `m.chunk != 0`, `m.chunk.used = m.used`. |
| 11 | `tk_arena_commit()` | `msp<=0` → no-op; `msp -= 1`. Nada é liberado, então nada a purgar. |
| 12 | `tk_region` (o tipo) | o layout de §3.2. Em Teko o handle é `u64` — o `uptr` que `teko::mem::unsafe::Arena` já carrega. |

### 4.1 A free-list (overlay do root) — sem ela a semântica muda

`tk_free_take` / `tk_free_block` / `tk_free_purge` não estão na lista das 12, mas
`tk_region_alloc` e `tk_arena_pop` chamam. Portar as 12 sem elas **muda o
comportamento**, então entram no mesmo grupo:

* `free_take(an)`: `qa = ceil16(an)`; se `qa <= 65536`, bin `qa/16 - 1`, pop de sonda
  única; senão varredura limitada a 32 na lista grande, aceitando `bytes >= an &&
  bytes <= an*2` (sem split — não há header para splitar).
* `free_block(p, bytes)`: `usable = floor16(bytes)` — **piso, nunca teto**: o `bytes`
  do chamador é um limite INFERIOR verdadeiro, e servir um bloco maior do que ele
  realmente é invade o vizinho do bump.
* **Ceil no take, floor no park.** Essa assimetria é a correção do #148 Level-2 e não é
  cosmética: ceil impede que um pedido de 24 receba um bloco de 16 (overrun de 8);
  floor impede que um bloco seja anunciado maior do que é.

### 4.2 `TEKO_MEM_PARANOID` — o único oráculo de memória que sobrevive à morte do C

Vive dentro de `free_block`, antes de qualquer park:

```
if paranoid { if usable > 0 { c_memset(p, 0xDD, usable) }  return }
```

Poison e **nunca parkeia** — o bloco liberado jamais é reusado. Isto guarda o DECRETO
de aliasing: o emissor encadeia o buffer linearmente (`out = cb(out, …)`) e conta com
free-old seguro; se a prova de linearidade estiver errada, uma leitura pós-park devolve
`0xDD` e o gate falha alto, em vez de corromper em silêncio. O comentário do runtime
registra por quê nunca foi redundante: *"Arena reuse is invisible to ASan"*. Com
ASan/UBSan fora junto com o C, este é o oráculo restante — ele não pode se perder no
porte.

O probe do ambiente é feito **uma vez**, preguiçosamente, e cacheado em
`CONTROL.paranoid` (`-1` não sondado, `0` off, `1` on), igual ao `static int
tk_paranoid` do C. Ver P3 sobre a string.

---

## 5. Ordem de implementação

Cada grupo é coeso e testável sozinho. Todos dependem de P1; o grupo 1 em diante
depende de P2.

| grupo | conteúdo | teste que afirma comportamento |
|------:|----------|--------------------------------|
| 0 | P1 + P2 (superfície de compilador) | store 0xDEADBEEF em offset calculado, load de volta, valor idêntico; slot de módulo sobrevive a duas chamadas |
| 1 | CHUNK + `region_alloc` + `region_new` | ponteiros consecutivos distintos e 16-alinhados; um pedido > 64 KB ganha chunk exclusivo; `n=0` devolve ponteiro distinto; o retry-em-`an` acontece |
| 2 | `region_drop` + `regions_free_all` | drop libera todos os chunks; drop é idempotente numa segunda passada; `free_all` duas vezes é no-op; região dropada some do registro |
| 3 | `parent` + `drop_subtree` + `register`/`lookup` | subárvore de 3 níveis some inteira e irmã NÃO some; `lookup` sobe a cadeia; `register` do mesmo `type_id` sobrescreve; `drop_subtree` seguido de `free_all` não faz double-free |
| 4 | free-list + `arena_push`/`pop`/`commit` | rewind libera só o que veio depois da marca; `commit` preserva; push acima de 64 mantém o balanço e não rebobina; take/park casam ceil/floor |
| 5 | `TEKO_MEM_PARANOID` | com a variável ligada, um bloco liberado lê `0xDD`; a próxima alocação do mesmo tamanho devolve endereço DIFERENTE (reuso impossível) |

Prova de volume (item 4 da carga): alocar/dropar/realocar em volume e comparar contagem
de chunks e footprint com o gêmeo em C, para provar que o comportamento de chunk bate.

---

## 6. Riscos

* **`atexit`.** Sem ele, um programa que termina normalmente para de ser leak-clean
  (§4, função 1). Precisa de decisão: ou o epílogo do `main` gerado chama
  `regions_free_all`, ou surge superfície para passar função como ponteiro à libc.
* **Windows.** `aligned_alloc` é C11 e não existe no MSVC; lá é
  `_aligned_malloc`/`_aligned_free`. O `extern fn` precisa selecionar por alvo, o que
  hoje não tem forma na declaração.
* **Thread-safety.** O seed é single-threaded e o C anota "revisit at S8". O porte
  herda a mesma limitação — não a piora, mas também não a resolve.
* **Uma implementação sutilmente errada é pior que nenhuma**: ela passa nos testes e
  corrompe na memória de todo programa emitido, inclusive a do próprio compilador. É
  por isso que este documento vem antes do código, e é por isso que cada grupo de §5
  tem teste que afirma comportamento, não ausência de crash.


---

## 7. Estado da implementação de referência

`examples/probes/arena_teko` — projeto próprio, backend nativo, `exit 42` = tudo vale.
Ele não vive em `src/` porque pôr o arena lá exige recompilar o compilador, e o seed
correto (o binário RELEASED) não é alcançável neste container: todo seed em disco falha
no fonte da base em `const struct: initializer is not a struct literal (#594)`.

| arquivo | conteúdo |
|---------|----------|
| `word.tks` | a costura P1: `load_word`/`store_word`, `compare_bytes`, `round_up_align`/`round_down_align`, `raw_alloc` |
| `layout.tks` | todos os offsets da §3, como `const` |
| `control.tks` | a costura P2: o bloco de controle, o probe do `TEKO_MEM_PARANOID` |
| `chunk.tks` | `chunk_try`/`chunk_free`/`chunk_free_list`/`chunk_free_until`/`chunk_fit_offset` |
| `region.tks` | `region_new`/`region_root`/`region_alloc`/`region_drop`/`regions_free_all` |
| `tree.tks` | `region_drop_subtree`/`region_register`/`region_lookup` |
| `marks.tks` | `checkpoint_push`/`checkpoint_pop`/`checkpoint_commit` |
| `freelist.tks` | `free_take`/`free_park`/`free_block`/`free_purge` + o modo paranoico |
| `volume_gate.tks` | a prova de volume: empacotamento previsto pela regra e reclaim apos drop |

As 12 funções da carga estão todas implementadas e testadas. `marks.tks` não pode usar
os nomes `arena_push`/`arena_pop`/`arena_commit` — ver o defeito 3 abaixo.

Prova de que os gates detectam quebra (mutação, uma por grupo):

| mutação | resultado |
|---------|-----------|
| `chunk_fit_offset` devolve sempre 0 | exit 51 (aliasing) |
| `region_drop` sem o unlink do registro | segfault na varredura |
| `region_reaches` para no primeiro nível | exit 60 (subárvore sobrevive) |
| `free_block` sem o poison | exit 69 (oráculo mudo) |
| chunk nunca reusado depois do primeiro bloco | exit 53 (bump deixa de compartilhar) |
| `region_release` sem liberar os chunks | exit 72 (volume deixa de reclamar) |

A prova de volume compara com a REGRA do gêmeo em C, não com uma corrida anterior:
`predicted_chunks(count, size)` = `ceil(count / floor(65536 / ceil16(size)))`, conferido
em nove tamanhos (abaixo, em cima e acima do alinhamento; frações do chunk; o chunk
inteiro; três chunks). Depois 400 ciclos de criar-encher-dropar de 256 KB cada: a
maioria tem que cair no MESMO endereço de chunk do primeiro ciclo, o que só acontece se
cada drop devolveu os blocos ao alocador. Tudo em 0,17 s.

---

## 8. Defeitos do backend nativo achados construindo isto

Nenhum destes dá erro de compilação. Todos dão RESPOSTA ERRADA, que é a categoria que
um arena não pode carregar.

**1. O retorno `i32` de um `extern fn` não é narrowed.** O callee deixa os 32 bits
altos do registrador de retorno indefinidos (o SysV ABI permite; o `memcmp` da glibc
usa a permissão) e Teko compara o registrador de 64 bits inteiro. Medido:
`memcmp("\x07","\xc8",1)`, um -193 verdadeiro, lê como POSITIVO enorme; um `i32`
local negativo compara certo no mesmo binário; um `to i64` explícito não corrige.
Contorno: declarar `u64` e extrair `% 2^32`.

**2. `match` sobre receptor fat-pointer não lowera.**
`fat-pointer receiver match-expression not yet lowered (N2)` — atinge todo
`match x { str as v => …; error => … }`, que é a forma canônica de ler
`teko::env::var`. Também `panic(msg)` com `str` local
(`N1: not a fat-pointer local`).

**3. `a && b` com chamada do lado direito não lowera.**
`integer operator not yet lowered (N2)`. Vira guarda explícita.

**3b. `teko::mem::peak_rss()` não linka no backend nativo:** ele lowera para um símbolo
indefinido `peak_rss` em vez do símbolo do runtime, e o binário morre no `ld`.

**4. O PIOR: uma função do usuário com o nome de um builtin injetado é silenciosamente
substituída pelo builtin.** `pub fn arena_push(control: u64)` foi aceita pelo checker
com a aridade do usuário — a chamada `arena_push(control)` typechecou — e o backend
emitiu a chamada ao `tk_arena_push()` do runtime em C. O corpo do usuário nunca rodou,
sem um aviso sequer; o sintoma foi o mark stack ficar em zero depois de um push.
`builtin_fn` resolve por ÚLTIMO SEGMENTO do caminho, então `arena_push`, `arena_pop`,
`arena_commit`, `intern_get`, `cov_mark` e companhia são nomes minados em qualquer
namespace. Para o porte real do arena isto é bloqueante: as funções do arena têm
exatamente esses nomes.


---

## 9. O que NAO rodou, e por que

**`teko test .` (a baseline de ~1042 unitarios): NAO CONCLUIU.** Foi disparado com o
seed disponivel e morreu no timeout de 15 minutos sem produzir uma linha de saida. Nao
ha verde a declarar aqui.

O que da confianca estrutural, e nao substitui a medicao: esta carga **nao toca um so
arquivo sob `src/`**. O diff contra o ponto de branch e inteiramente
`examples/probes/` mais este documento, entao a suite de unitarios do compilador esta
literalmente inalterada.

**O compilador nao foi reconstruido, e nao podia ser.** `scripts/fetch_teko.sh` falha
com HTTP 403 (o acesso ao GitHub esta desabilitado nesta sessao), e **todo** binario
`teko` em disco falha ao compilar o fonte da base:

* `0.3.0.16-beta` (o instalado em `/usr/local/bin`) nem parseia doc-comment;
* `0.3.0.30-beta` para em `src/checker/comptime_fold.tks:1569:45`;
* os gen1 mais recentes de outras faixas param todos em
  `const struct: initializer is not a struct literal (Tier-A follow-up) (#594)`.

O compilador que construiu e rodou tudo desta carga foi `/tmp/gate20g/mp/teko`, um gen1
de faixa irma. Por isso a implementacao de referencia vive em `examples/probes/` e nao
em `src/`: mexer no compilador sem poder recompila-lo produziria codigo nao validado, e
o arena e a peca onde uma implementacao sutilmente errada e pior que nenhuma.

**As duas costuras (P1 e P2) nao foram implementadas no compilador** — sao mudanca de
`builtin_fn` + `lower.tks`, exatamente o que nao pode ser validado aqui. O que existe e
o projeto delas (secao 2), a medida do custo (uma instrucao LIR ja existente cada), e
uma implementacao completa que as usa por tras de uma funcao so, para que a troca seja
uma linha quando o seed certo estiver disponivel.
