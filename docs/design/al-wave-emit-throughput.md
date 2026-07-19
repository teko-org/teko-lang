# AL Wave — Emit Throughput (proof-first)

Status: **PROPOSTA (design-ahead, NÃO ratificado).** Architect draft, 2026-07-19.
The owner still reviews the proofs and the architecture before any fix lands.
No product code changes here; no version bump. Companion to
`docs/design/compile-time-architecture.md` (the CI wall-clock axis — this doc is the
*single-process emit-throughput* axis it named "Strategic / own backend" but which we
now show precedes Fase E entirely).

Disciplina inegociável do owner (verbatim): **"sem nadar no escuro, precisamos de
provas do problema"** — nenhum fix sem medição. Portanto **AL1 é um crumb de MEDIÇÃO,
não de fix**, e roda e produz números ANTES de AL2–AL5 serem finalizados. Nenhum fix
downstream é cravado além do que a medição de AL1 justificar.

---

## 0. TL;DR

| Crumb | Título | Tamanho | O que faz | Ritual |
|---|---|---|---|---|
| **AL1** | Prova: instrumentação discriminante do caminho codegen→emit | M | mede, não corrige | fixpoint + probe RODA em self-build completo e emite relatório máquina-legível |
| **AL2** | Endurecimento da amortização de runtime (push-cache) — PALIATIVO | S/M | perf, behavior-preserving | probe: `other-ptr` misses no buffer grande → ~0 |
| **AL3** | **NÚCLEO**: builder linear de primeira classe `{ptr,len,cap}` | L | representação + perf, bytes emitidos idênticos | fixpoint + probe: fator de amplificação de cópia no emit → ~1.0 |
| **AL4** | Writers nativos (stream do output em vez de buffer de 8.5MB) | M | perf + RSS | probe: pico de RSS + tempo de emit |
| **AL5** | Lifetime de pipeline: region-per-phase (arena por fase) | M/L | perf + memória | probe: reclaim-ratio ↑, pico RSS ↓, zero regressão |
| **AL6** | **Ataque pela FONTE**: migração `empty()+push` → 4 níveis `[]` / `T[N]` / `[...]` / `const` | M (mecânico) | mata a CONSTRUÇÃO; níveis 0/2/3 em paralelo a AL3, nível 1 (`T[N]`) depende do rep de AL3 | fixpoint por sub-lote + probe de tempo; convenção W15 |

**A hipótese-âncora, refinada e confirmada em file:line (§2).** O `push` do runtime JÁ
é amortizado O(1). O custo real NÃO é "copia o array a cada push". É este: um `[]byte`
é um valor de 2 palavras `{ptr,len}` **sem campo de capacidade**. Para conseguir
append amortizado sob semântica de valor, o runtime mantém uma **tabela lateral GLOBAL**
(`tk_push_cache`, 65536 buckets single-probe, `teko_rt.c:2091-2094`) que é o ÚNICO
lugar onde `cap` é lembrado. Um append in-place exige um HIT nessa tabela; um MISS força
um copy-grow do buffer inteiro. A tabela é finita e global → **colisões**. O próprio
código documenta o incidente ("the 11.5 GB fix", `teko_rt.c:2155-2160`): inserts minúsculos
clobbaram o slot do buffer de saída multi-MB ~2300×, gerando ~7.5k copy-grows espúrios
de ~1.5MB = 11.5GB de churn (85% do total). A eviction size-aware MITIGOU mas **não
eliminou** a classe de colisão. Esse é, ipsis litteris, o "rebind de arrays (push)
desnecessário" e "a realocação de memória é o maior problema" do owner. É semântico (não
o linker) → bate com "ocorre em C e native, Fase E não resolve".

**O núcleo da correção (AL3).** Um valor de builder que carrega `{ptr,len,cap}` no
próprio valor e que o **checker prova LINEAR** (dono único, threaded). Como `cap` está
no valor, o append é incondicionalmente in-place quando `cap > len` — sem hash, sem
tabela global, sem colisão, sem clobber, sem cópia defensiva. Realloc-parity por
construção, determinístico, não probabilístico. É o mesmo paralelo do `const`: um
agregado com semântica de valor que o compilador não conseguia provar dono-único ganhava
uma cópia defensiva; a cura, em ambos, é ownership no nível do valor/tipo.

**DOIS ATAQUES COMPLEMENTARES AO MESMO STORM (ruling do owner, 2026-07-19).** O builder
linear (AL3) ataca pelo lado do CODEGEN — mata a CÓPIA. O ataque pela FONTE (AL6) mata a
CONSTRUÇÃO. A divisão é por natureza do dado:

O espectro tem **4 níveis por grau de conhecimento na fonte** (ruling do owner,
2026-07-19) — quanto mais o compilador sabe, mais cedo o `cap` é fixado e menos a tabela
global `tk_push_cache` é tocada:

| Nível | Conhecimento na fonte | Sintaxe | O que morre |
|---|---|---|---|
| 0 | **nada** (vazio, elemento por contexto) | `[]` | a chamada de fn + o cache-touch de nascimento |
| 1 | **capacidade** (sei o tamanho, não os itens) | `T[100] = []` | os copy-grows: `cap` fixado na fonte ⇒ pushes preenchem in-place, zero MISS |
| 2 | **itens** (valores fixos, tamanho fixo) | `[1, 2, 3]` | a construção inteira: um `malloc` dimensionado, zero rebind |
| 3 | **itens const** (const-expr) | `const X = [...]` | TUDO: imagem em rodata, zero construção em runtime |
| — | **imprevisível** (loop sobre runtime, tamanho desconhecido) | builder linear (**AL3**) | a CÓPIA DEFENSIVA — append in-place incondicional via `cap` no valor |

Níveis 0–3 atacam pela FONTE (AL6); o imprevisível é a única fatia que precisa do builder
de codegen (AL3). O **nível 1 (`T[N]`) ataca a doença na raiz**: o storm vem de `cap` não
ser conhecido na hora do push (tabela global + colisões); declarar a capacidade na fonte
elimina os misses para TODA lista de tamanho conhecível — mesmo sem conhecer os itens.
**AL1 quantifica a fatia de cada nível** (§3.5): dos sites `empty()+push`, quantos têm
tamanho conhecível estaticamente (→`T[N]`), quantos têm itens conhecíveis (→literal/const),
quantos são genuinamente dinâmicos (→builder). Nenhum é cravado antes dessa fatia.

---

## 1. O que já sabemos (baseline medido, dado)

- Emit de 17.6MB: 201s macOS / 307s Windows / 520s ubuntu (~50–90 KB/s). O `cc`
  consome o MESMO `teko.c` em 9–26s. **Gerar o C é ~20× mais lento que compilá-lo.**
- Ocorre em AMBOS backends (C e native) → NÃO é o linker; Fase E não resolve; **AL
  precede Fase E.**
- Barra do owner: **teko builda mais rápido que rustc num codebase de mesmo tamanho.**

---

## 2. A prova-âncora, confirmada e aprofundada (file:line)

### 2.1 O runtime JÁ é amortizado — a hipótese ingênua está REFUTADA

- `src/runtime/teko_rt.h:22-38` — `TK_RT_LIST(T,Name)`: struct `{T *ptr; size_t len;
  size_t cap;}`; `push` faz cap-doubling via `realloc` in-place, O(1) amortizado. Isto
  é o rep de list dos programas GERADOS.
- `teko_rt.h:20-21`: "push CONSUMES and RETURNS ... the caller holds the one live copy"
  — semântica linear/move já é o contrato.

### 2.2 Mas o rep de `[]byte` do COMPILADOR não carrega `cap` — daí a tabela global

- `teko_rt.h:59-60` (`tk_slice_byte`) e `teko_rt.h:45-48` (`tk_str`): o `[]byte` (e
  `str`) é `{ptr,len}` — **duas palavras, SEM `cap`**. É o rep que o codegen levanta.
- Porque o valor não tem `cap`, o append amortizado precisa reconstruir "esse ponteiro
  tem capacidade sobrando?" por uma **tabela lateral global keyed por ponteiro**:
  - `teko_rt.c:2091-2094` — `#define TK_PUSH_HASH_SIZE (1u << 16)` (65536 buckets
    single-probe); `struct { const void *ptr; uint64_t len, cap, esz; tk_region
    *region; uint64_t region_gen; } tk_push_cache[...]`; `tk_push_slot(p)` = hash
    multiplicativo de `p>>4`.
  - `teko_rt.c:2104-2116` (`tk_slice_push_r`): in-place SÓ com HIT (ptr+len+esz+region+
    gen). Qualquer falha → **copy-grow geométrico** (`memcpy` do buffer inteiro,
    `:2152-2154`).
- `teko_rt.c:2178-2213` (`tk_append_bytes_fo`) — o alvo de `cb` do emit — tem o MESMO
  padrão: HIT → `memcpy` do fragmento in-place; MISS → `tk_alloc(cap)` + `memcpy` do
  buffer inteiro + free-old.

### 2.3 A colisão global é a doença — documentada no próprio código

- `teko_rt.c:2155-2160`, verbatim: "SIZE-AWARE eviction. Blind overwrite let 150M tiny
  cache inserts clobber the multi-MB output buffer's slot ~2300×; every clobber forced
  a FULL multi-MB copy-grow on its next append (measured: ~7.5k spurious grows averaging
  ~1.5 MB = 11.5 GB of 13.5 GB total churn — 85%)."
- A cura aplicada (`:2161-2166`) foi **size-aware eviction** (o incumbente maior mantém o
  slot; o novato menor simplesmente não é cacheado). Isto **mitiga o caso tiny-vs-huge**,
  mas **NÃO elimina a classe**: dois buffers vivos de tamanho comparável (o output de
  emit + qualquer outro builder quente — ex. um buffer de interning, um scratch de linha)
  que caiam no mesmo bucket ainda se despejam mutuamente, e cada despejo é um `memcpy`
  multi-MB na próxima escrita. Sobre milhares de arquivos, cauda ~O(n²).
- A maquinaria de realloc-parity/free-list (`teko_rt.c:2171-2213`, `teko_rt.h:137`)
  ATACA a memória (parkar o buffer velho), não a colisão: mesmo com free-old perfeito, um
  MISS por colisão ainda paga a cópia integral antes de parkar. **A cópia é o custo, não
  o leak.**

### 2.4 Os ganchos de medição JÁ EXISTEM — AL1 os formaliza, não os inventa

- `teko_rt.c:1036-1043` — `TEKO_ARENA_OBS` (env var) liga o dump.
- `teko_rt.c:1091-1094` — **histograma de miss-reason** (o discriminador central!):
  `empty | other-ptr | len | cap-full | esz/gen`, cada um em duas colunas (all | grows
  >1MB), alimentado em `teko_rt.c:2132-2138` (`why = 0..4`).
- `teko_rt.c:1089-1090` — PUSH copy-grow bytes por fn chamadora (RA1) e por caller (RA2).
- `teko_rt.c:1019-1028, 1097-1098` — **"dark matter"**: buffers malloc'd de
  str/format (`tk_str_concat`/slice/`of_bytes`/`u64_to_str`) atribuídos à fn chamadora
  — o gancho que separa a causa-`str` da causa-`[]byte`.

---

## 3. AL1 — a PROVA (medição discriminante). Roda ANTES de tudo.

**Objetivo:** produzir, de um self-build completo, os números que
CONFIRMAM-OU-REFUTAM a hipótese de colisão/cópia-defensiva e RANQUEIAM as causas
alternativas. AL1 não corrige nada.

### 3.1 Metodologia exata (o que rodar)

1. Build de gen1 do compilador COM `TEKO_ARENA_OBS=<path>` ligado, fazendo um self-build
   completo (o corpus do próprio compilador é o "codebase de mesmo tamanho" de
   referência). O dump já sai em `teko_rt.c:1071-1099`.
2. Colher, do dump, na coluna **">1MB grows"** do histograma de miss-reason
   (`teko_rt.c:1091-1094`):
   - `other-ptr` (why==1) alto ⇒ **COLISÃO/clobber confirmado** — o slot do buffer de
     saída foi despejado por um ponteiro colidente. Confirma AL2/AL3.
   - `cap-full` (why==3) alto ⇒ doublings geométricos genuínos — amortização SÃ, o
     villain está noutro lugar (NÃO cravar AL2/AL3 como núcleo).
   - `len` (why==2) alto ⇒ o buffer foi lido num ponto intermediário (alias/rebind) —
     um sítio verdadeiro de cópia-defensiva/aliasing no threading; aponta codegen/checker.
   - `empty` (why==0) alto ⇒ nascimentos, não cópias — ruído.
3. **Fator de amplificação de cópia** (a métrica-veredito):
   `copy_amp = (soma de bytes copiados em copy-grows do buffer de emit) / (bytes do
   output final)`. Sadio ≈ `log2(output_bytes)` (doublings geométricos). Se
   `copy_amp ≫ log2(n)` ⇒ cópia super-linear (defensiva/colisão). Derivável de
   `tk_obs_push_bytes` + o tamanho final.
4. **Razão allocs vs pushes**: `n_reallocs / (log2(n) · n_pushes_lógicos)`. `≫ 1` ⇒
   cópia defensiva; `≈ 1` ⇒ amortização sã. (Contadores novos em AL1, §3.3.)
5. **Timing por fase**: confirmar que emit domina. O reporter de fase já existe
   (`src/build/progress.tks`, `src/build/assemble.tks:149-192` — lexer/parser/checker/
   monomorph/codegen/emit/cc). AL1 acrescenta um dump máquina-legível de ns por fase
   (§3.3), para o veredito "emit domina" não depender de leitura de tela.

### 3.2 O número que PROVA/REFUTA

> A hipótese de colisão/cópia-defensiva é **PROVADA** sse, num self-build:
> `copy_amp(emit) > 4 × log2(output_bytes)` **E** a coluna ">1MB" de miss-reason é
> dominada por `other-ptr` (colisão) ou `len` (aliasing). É **REFUTADA** se `copy_amp ≈
> log2(output_bytes)` e a coluna ">1MB" é dominada por `cap-full` — nesse caso o custo
> está na tabela "dark matter" (str concat, §3.4) ou no front-end, e o núcleo passa a
> ser AL4/str-builder em vez de AL3-[]byte-builder.

### 3.3 Scaffolding que COMPILA HOJE (design-ahead)

AL1 adiciona um shim de contadores no seam de runtime mantido (`teko_rt.{c,h}` —
exceção da lei Teko-only) e um dump de fase Teko-side. Contratos (o implementador copia
verbatim; full Javadoc):

```teko
/**
 * Snapshot de timing de UMA fase do pipeline, para o dump máquina-legível de AL1.
 * Behavior-preserving: só é populado quando `TEKO_ARENA_OBS` (ou `TEKO_PHASE_OBS`)
 * está ligado; caminho quente não paga nada quando desligado (mesmo contrato de
 * `tk_obs_enabled`, teko_rt.c:1036).
 *
 * @field name       o nome da fase ("lexer","checker","monomorph","codegen","emit","cc")
 * @field elapsed_ns nanosegundos de parede gastos na fase
 * @field out_bytes  bytes produzidos pela fase (o tamanho do C gerado, para emit; 0 senão)
 * @since 0.x (#AL1)
 */
type PhaseSample = struct { name: str; elapsed_ns: u64; out_bytes: u64 }

/**
 * Dump máquina-legível do perfil de um self-build: uma linha por fase mais o veredito
 * de amplificação de cópia do emit. Escreve em `path` (uma linha `fase\tns\tbytes` por
 * fase, seguida de `copy_amp\t<f64>` e `emit_kbps\t<f64>`), para o gate de AL1 parsear
 * sem depender do layout de tela do reporter de progresso.
 *
 * @param path     arquivo de saída (do env `TEKO_PHASE_OBS`; vazio ⇒ no-op)
 * @param samples  as amostras coletadas, em ordem de fase
 * @return         void; em falha de I/O faz honest-stop (M.1 fail-loud), nunca silencia
 * @throws         escreve stderr e aborta se `path` não abrir (mesma política de tk_obs_dump)
 * @since 0.x (#AL1)
 */
pub fn phase_obs_dump(path: str, samples: []PhaseSample) -> void

/**
 * O fator de amplificação de cópia do buffer de emit: bytes copiados em copy-grows
 * dividido pelos bytes do output final. Sadio ≈ log2(output_bytes). AL1 usa isto como
 * a métrica-veredito (§3.2). Lê os contadores de runtime `tk_obs_push_bytes` via o
 * seam FFI `teko::mem::push_grow_bytes()`.
 *
 * @param grow_bytes    total de bytes movidos em copy-grows (de tk_obs_push_bytes)
 * @param output_bytes  tamanho do C gerado
 * @return              a razão; 0.0 quando output_bytes == 0 (guard, sem divisão por zero)
 * @since 0.x (#AL1)
 */
pub fn copy_amplification(grow_bytes: u64, output_bytes: u64) -> f64
```

No seam mantido `teko_rt.h` (novos contadores expostos, zero custo quando obs off):

```c
/**
 * @brief Total de bytes movidos por copy-grows de []byte/list desde o início do
 * processo (soma de `cap*esz` em cada copy-grow de tk_slice_push_r/tk_append_bytes_fo).
 * Alimenta copy_amplification() de AL1. Só incrementa sob tk_obs_enabled(); leitura é
 * sempre barata. NÃO é o buffer de saída isolado — é o agregado; AL1 correlaciona com
 * a tabela RA1 (por fn chamadora) para isolar o buffer de emit.
 * @return bytes agregados de copy-grow
 */
uint64_t tk_obs_push_grow_bytes(void);
```

### 3.4 Causas alternativas que AL1 DISTINGUE (owner's seed doubts — testar, não presumir)

| Dúvida-seed | Como AL1 distingue | Sinal que a implica |
|---|---|---|
| (a) interning de string/símbolos O(n²) scan | probe de timing dedicado no intern + tabela "dark matter" por fn (`teko_rt.c:1097`) | tempo no intern ≫ emit-copy; `tk_str_concat` no topo do dark-matter |
| (b) arena sem size-header força copy-grow (`teko_rt.h:137`) | reclaim-ratio + region bytes (`teko_rt.c:1078-1086`); miss-reason `esz/gen` | `esz/gen` (why==4) alto em ">1MB" |
| (c) concatenação de `str` (não `[]byte`) no emit | tabela MALLOC str dark-matter (`teko_rt.c:1097-1098`) | dark-matter MB ≫ PUSH copy-grow MB ⇒ o núcleo é str-builder, não []byte-builder |
| **hipótese-favorita: colisão/cópia-defensiva** | histograma de miss-reason ">1MB" + `copy_amp` | `other-ptr`/`len` dominam ">1MB" **E** `copy_amp ≫ log2(n)` |

AL1 tem que **rankear** essas quatro; o núcleo AL3 só é ratificado se a favorita ganhar.
Se (c) ganhar, o núcleo vira um str-builder linear (mesma arquitetura de AL3, alvo `str`).

### 3.5 AL1 quantifica a fatia de CADA ataque (source vs codegen) — a divisão de trabalho

AL1 também produz o **censo dos sites `empty()+push`** no compilador, classificando cada
um como ESTÁTICO (migrável a `[...]`/`const`, colhido por AL6) ou DINÂMICO (loop sobre
dados de runtime, colhido pelo builder AL3). Método:

1. **Contagem estática de sites** (análise de fonte, um passo sobre a AST/TAST): para cada
   `teko::list::empty()` seguido de `push`(es), classificar pelos 4 níveis (§2.5):
   - itens todos const-expr, nº fixo ⇒ **nível 3** → `const` (T-B6) ou **nível 2** →
     `[...]` (se const não suportar o tipo do item).
   - itens não-const-expr mas **nº de pushes / tamanho conhecível estaticamente** (loop de
     contagem fixa, ou N derivável de um `.len` conhecido) ⇒ **nível 1** → `T[N] = []` +
     preenchimento (cap fixado, zero MISS).
   - **tamanho E itens imprevisíveis** (loop sobre dados de runtime de contagem
     desconhecida) ⇒ **DINÂMICO** → precisa do builder AL3.
   A própria `scope.tks:369-371` (`empty()`+push+push para montar `param` types) e as
   dezenas de `teko::list::push(teko::list::empty(), X)` em `scope.tks:375-384`,
   `escape.tks`, etc., são candidatos de nível 1–2 óbvios (tamanho fixo conhecido) — AL1
   conta quantos caem em cada nível.
2. **Peso dinâmico de cada classe**: cruzar o censo com a atribuição RA1 do dump
   (`teko_rt.c:1089`, PUSH copy-grow bytes por fn chamadora) — quanto do custo de
   copy-grow vem de sites estáticos (que AL6 elimina de graça) vs dinâmicos (que só AL3
   resolve). Se o grosso do custo é estático ⇒ AL6 é o maior lever e é barato; se é
   dinâmico ⇒ AL3 é o núcleo confirmado.

O veredito de §3.2 (colisão/cópia) e este censo JUNTOS ratificam a divisão AL3/AL6. Nem a
migração nem o builder é cravado sem a fatia respectiva medida.

---

## 4. Localização das cópias no compilador (para AL3)

- **`cb` e a família (`src/codegen/codegen.tks:141-160`)**: `cb`→`teko::mem::append_fo`
  (`:148-150`), `cb_byte`→`teko::mem::push_fo` (`:160`). O comentário `:142-147` já
  declara o DECREE: o buffer é threaded LINEARMENTE (`out = cb(out, …)`). **A linearidade
  é uma CONVENÇÃO garantida por decreto, não uma propriedade que o rep force.** É aí que
  a colisão entra: mesmo threaded, o valor não carrega `cap`, então o in-place depende do
  cache global e pode perdê-lo por colisão.
- **A prova de linearidade existe mas só habilita free-old, não elimina cópia**:
  `src/checker/escape.tks:400-411` (`assign_frees_old`, `:554-556`) — prova a cadeia
  linear (nascida de `list::empty()`, todo write é self-append, equação de leitura sem
  captura mid-chain). Isso roteia para `tk_slice_push_fo` (parkar o velho). **Não** dá ao
  valor um `cap` próprio; a cópia num MISS de cache continua.
- **A intrínseca**: `src/checker/scope.tks:368-372` (`append_fo` builtin), tipagem em
  `src/checker/typer.tks:385-389` (`push_fo`). São os pontos onde AL3 acrescenta a
  intrínseca `builder`/`reserve`.
- **Borrow/Reference já existe como espinha** (`src/checker/borrow.tks:23`
  `type_reaches_ref`, `:1-20` a soundness de cópia-de-valor-copia-ponteiro): é a
  maquinaria de linearidade a estender para provar `Builder` dono-único. Hoje é
  "consulted NOWHERE yet (SP-1 is a pure, unwired fact)" (`borrow.tks:14`) — pronta para
  ser fiada por AL3.
- **O paralelo `const`** (owner correlaciona): `docs/design/const-tb*` e
  `src/checker/consteval.tks:433` (`rewrite_const_init` — "a copy of the aggregate const
  declaration"). Const teve o MESMO problema: um agregado com semântica de valor que o
  compilador não conseguia provar dono-único era copiado defensivamente. A cura de const
  (ownership no nível do dado, rodata compartilhada por reloc) é a mesma forma da cura de
  AL3 (ownership no nível do valor, `cap` no valor). Instrutivo: onde o tipo não carrega
  ownership, o compilador copia por segurança.

---

## 5. AL2–AL5 — arquitetura de correção (cada uma contingente ao que AL1 provar)

### 5.1 AL2 — endurecer a amortização de runtime (PALIATIVO, S/M)

Dispara SÓ se AL1 mostrar `other-ptr` (colisão) como contribuinte vivo porém limitado.
Opções: (i) tabela maior; (ii) associatividade N-way por bucket; (iii) cache por-região
em vez de global. **Explicitamente um stopgap** — AL3 remove a tabela para o caminho de
builder, então AL2 é ponte, não destino. Behavior-preserving; muda perf. Ritual: probe
mostra `other-ptr` misses no buffer grande → ~0.

### 5.2 AL3 — NÚCLEO: builder linear de primeira classe `{ptr,len,cap}` (L)

O valor carrega sua própria capacidade; o checker prova dono-único; append in-place é
incondicional quando `cap > len` — **sem tabela global, sem colisão, sem cópia
defensiva**. É realloc-parity determinístico.

**Superfície A — builder de biblioteca explícito** (opt-in, código novo):

```teko
/**
 * Um construtor linear de bytes: um buffer growable {ptr,len,cap} de dono ÚNICO. Ao
 * contrário de `[]byte` (um valor {ptr,len} sem capacidade, cuja amortização de append
 * depende do cache global tk_push_cache e sofre colisão de slot), o `Builder` carrega
 * `cap` no próprio valor, então `push`/`append` são in-place incondicionais enquanto
 * `cap > len` — zero cópia defensiva, zero lookup de cache. O checker impõe uso linear
 * (um único dono threaded; ver borrow.tks::type_reaches_ref estendido): uma cópia de
 * valor de um `Builder` é REJEITADA, garantindo que o buffer nunca é aliased.
 *
 * @field ptr  o buffer backing (arena-owned; liberado no drop da região da fase — AL5)
 * @field len  bytes escritos
 * @field cap  bytes alocados (a capacidade que o valor CARREGA — a cura da colisão)
 * @since 0.x (#AL3)
 */
type Builder = struct { ptr: RawPtr; len: u64; cap: u64 }

/**
 * Um builder vazio com capacidade reservada `hint` (0 ⇒ lazy no primeiro push). Aloca
 * na região corrente da fase (AL5), não no root — o buffer inteiro é bulk-freed no fim
 * da fase.
 * @param hint capacidade inicial em bytes (right-sized; 0 = lazy)
 * @return     um Builder vazio, dono único
 * @since 0.x (#AL3)
 */
pub fn builder_new(hint: u64) -> Builder

/**
 * Anexa os bytes de `s` a `b`, in-place quando cabe (cap - len >= s.len), senão um único
 * copy-grow geométrico. CONSOME e RETORNA `b` (semântica linear/move idêntica ao list
 * push de teko_rt.h:20). O checker garante que o `b` de entrada não é lido depois — nenhum
 * alias pode observar o buffer.
 * @param b o builder (consumido)
 * @param s os bytes a anexar
 * @return  o builder crescido (o único vivo)
 * @since 0.x (#AL3)
 */
pub fn builder_append(b: Builder, s: str) -> Builder

/**
 * Congela o builder num `[]byte` final (view {ptr,len} sobre o buffer). Após finish o
 * builder é consumido; escrever nele é erro de checker.
 * @param b o builder (consumido)
 * @return  o []byte final, sem cópia (mesmo ptr)
 * @since 0.x (#AL3)
 */
pub fn builder_finish(b: Builder) -> []byte
```

**Superfície B — `[]byte` linear provado, lowering interno (zero mudança de superfície)**:
o checker prova a cadeia `out = cb(out, …)` linear (a prova de `escape.tks:400-411` já
existe) e faz o **lowering do local `[]byte` para um `{ptr,len,cap}` de frame** — o
codegen do emit não muda de fonte, só para de copiar. `cap` vira um SSA local, não uma
consulta de cache.

**Recomendação:** **B para o caminho quente de emit** (não reescreve `cb`/codegen —
menor risco, byte-idêntico no C gerado), **A como opt-in explícito** para código novo. B
elimina a classe de colisão exatamente onde AL1 a mediu; A dá a ferramenta ergonômica sem
depender do provador. Ambos compartilham o rep `{ptr,len,cap}` e a intrínseca de runtime.
Ritual: fixpoint + probe: `copy_amp(emit) → ~1.0`; emit KB/s cumpre a barra (§6).
Behavior-preserving no nível da linguagem (mesmos bytes emitidos); só a perf muda.

### 5.3 AL4 — writers nativos (M)

Streama o output do emit direto para o fd/arquivo (writer chunked) em vez de construir um
buffer de 8.5MB em memória e só então escrever. Remove o buffer gigante do caminho
file-backed inteiramente (e com ele o alvo #1 de colisão). Perf + RSS. Contrato:

```teko
/**
 * Um sink de emit que escreve fragmentos direto num fd, em blocos de `chunk` bytes, sem
 * nunca materializar o C gerado inteiro em memória. Substitui o padrão "construir []byte
 * de 8.5MB, depois write" pelo qual o buffer de saída era o maior alvo de colisão de
 * cache (§2.3). Mantém um buffer de chunk pequeno e fixo (reusado), então NÃO há valor
 * multi-MB para colidir.
 *
 * @field fd     o descritor de arquivo de destino (aberto pelo caller)
 * @field buf    o chunk staging {ptr,len,cap} fixo (flushed quando cheio)
 * @since 0.x (#AL4)
 */
type EmitWriter = struct { fd: i32; buf: Builder }

/**
 * Anexa `s` ao writer, dando flush do chunk ao fd quando o staging enche. CONSOME e
 * RETORNA o writer (linear). Em falha de write faz honest-stop (M.1).
 * @param w o writer (consumido)
 * @param s os bytes a emitir
 * @return  o writer avançado, ou error num write curto/falho
 * @since 0.x (#AL4)
 */
pub fn ew_write(w: EmitWriter, s: str) -> EmitWriter | error
```

Ritual: probe: pico de RSS + tempo de emit. Contingente: prioridade alta se AL1 mostrar
que o buffer único domina; menor se a colisão for entre múltiplos builders (aí AL3 já
resolve).

### 5.4 AL5 — lifetime de pipeline: region-per-phase (M/L)

Cada fase (parse/check/mono/codegen/emit) recebe sua própria `tk_region`, dropada em um
passo no fim da fase — remove a acumulação de root process-lifetime (o "lifetime de
pipeline de build que não faz sentido" do owner). A primitiva existe:
`tk_region_new(parent)`/`tk_region_drop` (`teko_rt.h:148-152`), `tk_region_drop_subtree`
(`:151`), e o reclaim é observável (`teko_rt.c:1078-1086`). AL5 fia a região-da-fase
como o `parent` corrente que o codegen já threada (`teko_rt.h:141-147`). Perf + memória.
Ritual: probe: reclaim-ratio ↑, pico RSS ↓, zero regressão de correção (o gate diff
VM==native e o fixpoint). Contingente: sempre válido como higiene; magnitude depende do
quanto AL1 atribuir a alloc-pressure vs copy-churn.

### 5.5 AL6 — ataque pela FONTE: migração `empty()+push` → literal/`const` (M, mecânico)

O outro lado do storm: onde a lista é ESTATICAMENTE CONHECIDA (tamanho E/OU itens), não
há por que construir cegamente em runtime. Ruling do owner (2026-07-19). Quatro
reescritas, pelos 4 níveis de §2.5, em ordem de força:

0. `teko::list::empty()` (array vazio) → `[]`. Elimina a chamada de fn + o cache-touch de
   nascimento.
1. **`empty()` + pushes de contagem conhecida (itens ainda imprevisíveis)** →
   **`T[100] = []`** (pré-dimensiona: `cap=100, len=0`; os pushes preenchem in-place SEM
   copy-grow). É `with_capacity(100)` como sintaxe de tipo. **Ataca a raiz**: `cap`
   conhecido na fonte ⇒ zero MISS na `tk_push_cache` para toda lista de tamanho conhecível.
2. `empty()` + N pushes de valores fixos → `[a, b, c]`. UM `malloc` dimensionado, zero
   rebind. JÁ CONFIRMADO que o literal não-spread lowera a um único `malloc(N*sizeof)`
   preenchido por índice (`codegen.tks:2865-2887`) — não a `empty()+push` interno.
3. **Itens const-expr** → `const X = [...]` → **rodata, ZERO construção em runtime** (T-B6,
   `docs/design/const-tb*`).

### Respostas às 4 perguntas de design de `T[N]`

**Q1 — HARD CAP ou SOFT RESERVATION?** **Recomendo SOFT por default, HARD opt-in.**
- SOFT (`T[N] = []`): `N` é a capacidade inicial; push além de `N` faz copy-grow normal
  (como `with_capacity`). Tolera subestimar; behavior-preserving vs o append de hoje. É o
  caso comum do compilador (N = estimativa boa, ocasionalmente estoura).
- HARD (proposta de sintaxe distinta, ex. `T![N]` ou `fixed T[N]`): push além de `N` =
  panic/erro de checker. Ganho: permite **alocação em STACK/inline** (buffer de vida curta
  quente com zero heap — ganho enorme; ver Q4) e é bound-checkável estaticamente. Custo:
  estrito, exige que o programador acerte o bound.
- Trade-off e recomendação: SOFT como o `T[N]` default (segurança de perf sem risco de
  panic novo); HARD como um former SEPARADO opt-in para os hot-paths curtos onde o
  programador aceita o contrato e quer o stack-alloc. **Decisão de taste do owner** — o
  design suporta ambos com o mesmo rep (Q2); só o comportamento no overflow difere.

**Q2 — `T[N]` carrega `cap` NO VALOR? Relação com AL3?** **SIM, carrega `cap` no valor — e
é exatamente o builder linear de AL3 especializado para tamanho conhecido.** Mesmo rep
`{ptr, len, cap}` de `Builder` (§5.2). Por carregar `cap`, um push consulta a capacidade
NO VALOR e **sidestepa a `tk_push_cache` por completo** — sem hash, sem colisão. Logo:
**`T[N]` é o caso-ponte/especial do builder** onde a capacidade inicial é uma constante da
fonte em vez de crescer por doubling. Um UNIFICA o outro: AL3 entrega o rep + a prova de
linearidade; `T[N]` é a superfície-de-fonte que semeia `cap` inicial = N (nível 1), e o
literal `[...]` é a superfície que semeia `cap = len = N` já preenchido (nível 2).
Consequência de sequência: **`T[N]` e AL3 compartilham a MESMA implementação de rep e
prova** — `T[N]` não é um mecanismo paralelo, é a porta de entrada de fonte para o builder.

**Q3 — GRAMÁTICA: `T[N]` distinto ou `[]T` + atributo de capacidade?** **`[]T` + capacidade
inicial — MESMO tipo de runtime (`Slice`), com `cap` no rep; `T[N]` é açúcar de fonte que
carimba a capacidade inicial, não um tipo nominal novo.** Racional: o rep de runtime já é
`{ptr,len(,cap)}`; introduzir um tipo `FixedArray` nominal separado bifurcaria todo o
checker/codegen. Em vez disso, `Slice` ganha um `cap` (semeável), e a sintaxe `T[N]`
resolve para `Slice{element=T}` com `initial_cap=N`. Parser: hoje `SliceType` é
`{element}` (`parser/type.tks:6`); AL adiciona um `SliceType { element; initial_cap: Expr?
}` (postfix `[N]` no tipo do elemento — gramática nova, `parse_type.tks:131`). **Interação
com a sentinela `Slice{Void}`**: `T[100] = []` dá o elemento (T) E a capacidade (100) na
declaração, então o `[]` do lado direito é bem-tipado por contexto — **`T[N]` RESOLVE o
caveat de "`[]` precisa de contexto"** (a anotação `T[N]` É o contexto). SOFT/HARD (Q1)
distingue-se por um flag no `SliceType` (ou um former de sintaxe distinto para HARD), não
por dois tipos de runtime — no soft, `cap` é só a semente inicial.

**Q4 — ALOCAÇÃO: stack, arena, ou heap?** Amarrado a AL5 e AL4:
- **SOFT `T[N]`**: aloca `N*sizeof(T)` na **região da fase corrente** (AL5) — bulk-freed no
  fim da fase, sem `tk_push_cache`. Um estouro além de N cai no copy-grow da mesma região.
- **HARD `T![N]` de T pequeno e vida-de-frame** (Q1): **STACK/inline, zero heap** — o buffer
  vive no frame C (`T buf[N]`), o `{ptr=buf, len=0, cap=N}` aponta pra ele; escape analysis
  (`escape.tks`) prova frame-local (não escapa) antes de stack-alocar. Ganho enorme para
  buffers curtos quentes (linhas de emit, arg-lists de 1–3). Se escapa, cai na região da
  fase. É o caso onde HARD paga por si.
- **Writers (AL4)**: o `EmitWriter` de AL4 é um `T![chunk]` HARD em stack por natureza — um
  chunk fixo pequeno reusado, nunca um valor multi-MB. `T[N]`/HARD é a peça que torna o
  writer de AL4 zero-heap.

**Caveats (resolvidos no design):**
- **(a) `[]` vazio precisa do tipo do elemento por contexto/anotação.** Amarrar à
  sentinela `Slice { element = Void }` existente (`resolve.tks:1190,1296`; `typer.tks:364,
  397`). Regra W15: `[]` só onde o elemento é inferível por contexto/anotação
  (`let x: []T = []` ou `let x: T[100] = []`); nunca `[]` flutuando sem tipo. `T[N]` é o
  caminho preferido quando a capacidade é conhecida (dá tipo E cap de uma vez).
- **(b) `const` só onde os itens são const-expr.** Sites com item não-const-expr mas
  tamanho conhecido → `T[N]` (nível 1); tamanho E itens imprevisíveis → builder AL3.
- **(c) behavior-preserving onde possível, medido onde muda bytes.** `empty()`→`[]` e
  `empty()+push`→`T[N]`/`[...]`: byte-identidade OU delta justificado, fixpoint por
  sub-lote (gen2==gen3). `empty()+push`→`const`: MUDA bytes (runtime→rodata) — justificar
  com ganho de perf MEDIDO (copy-grow bytes daquele site → 0 no dump RA1 de AL1).

**Convenção W15 (nova, a ratificar junto):** "sem `teko::list::empty()`+`push` cego: se sei
o TAMANHO, `T[N]`; se sei os ITENS, `[...]`; se são const, `const`; só o genuinamente
dinâmico usa builder/append."

Tamanho: M mecânico (sweep fixpoint-gated, estilo crumbs S1–S4 de #594 "byte-idêntico"),
sub-lotes por módulo. Ritual: fixpoint por sub-lote + probe de tempo (o site migrado some
do dump RA1). Contingente à fatia de cada nível que AL1 medir (§3.5) — sweep-first onde AL1
atribui mais custo.

Bootstrap/sequência: os níveis 0/2/3 (`[]`,`[...]`,`const`) já estão no seed (lowering
existente + T-B6) → varrem o corpus logo após AL1. O **nível 1 (`T[N]`) introduz GRAMÁTICA
e rep novos (Q2/Q3): compartilha a implementação com AL3** — portanto `T[N]` NÃO pode ser
usado pelo corpus antes de AL3 semear o rep `{ptr,len,cap}` + a prova de linearidade.
Sequência: AL3 primeiro (rep+prova), depois a superfície `T[N]` entra em AL6 e só então o
corpus a usa. Os níveis 0/2/3 de AL6 correm em paralelo a AL3 (não dependem do rep novo).

---

## 6. Barra de aceitação — como MEDIR "mais rápido que rustc mesmo-tamanho"

Reprodutível, não impressionista:

1. **Corpus de referência fixo**: o próprio corpus do compilador (LOC conhecido) mais um
   par gerado de N-LOC sintético para escalar. Congelado por release.
2. **Métrica primária — throughput de emit**: `emit_kbps = output_bytes / emit_ns`. Piso
   de gate: derivado do baseline (hoje ~50–90 KB/s) — alvo ≥ ordem de grandeza (o `cc`
   consome o mesmo C ~20× mais rápido; o teto teórico é paridade com `cc`).
3. **Métrica de veredito — fator de amplificação de cópia**: `copy_amp(emit)` deve cair
   de `≫ log2(n)` (hoje, se AL1 confirmar) para `≈ log2(n)` (~1.0 normalizado). Este é o
   sinal direto de que a cópia defensiva/colisão morreu.
4. **Métrica comparativa — teko vs rustc**: total wall de `teko build` sobre o corpus de
   referência dividido pelo total wall de `rustc` sobre um codebase de LOC/IR comparável;
   gate: razão ≤ 1.0. Rodado como **lane de perf periódica** (não por-PR, para não sofrer
   ruído de runner), com threshold no probe.
5. **Ritual por crumb**: todo crumb que mexe em representação ou hot-path
   (AL2/AL3/AL4/AL5) tem **probe de tempo como ritual** além do fixpoint — o probe de AL1
   é o baseline contra o qual cada um mede seu delta.

---

## 7. Riscos + tensões de lei (com resolução recomendada)

- **Teko-only vs seam mantido C.** AL1 e AL3/AL4 tocam `teko_rt.{c,h}` (contadores,
  intrínseca de builder). Isso é a EXCEÇÃO explícita da lei Teko-only (runtime mantido).
  A superfície Teko (`Builder`, `EmitWriter`, `phase_obs_dump`) é `.tks`. Resolução:
  conforme; o C é só o seam de runtime, o resto é Teko.
- **Byte-identidade do C gerado (fixpoint).** AL3-Superfície-B DEVE ser byte-idêntico no
  C emitido (só a perf muda). Risco: a prova de linearidade rejeitar uma cadeia hoje
  aceita, ou o lowering mudar ordem de alloc. Resolução: B só dispara onde
  `escape.tks::assign_frees_old` já prova linear (mesmo conjunto), e o gate de fixpoint
  (gen2==gen3) + diff VM==native é o guarda. Se um sítio não prova, cai no caminho atual
  (safe, sem regressão) — nunca pior que hoje.
- **Semântica linear e cópia de valor (soundness §7).** Um `Builder` copiado por valor
  copiaria o ponteiro (o buraco de `borrow.tks:1-20`). Resolução: o checker REJEITA cópia
  de valor de `Builder` (uso linear imposto), reusando `type_reaches_ref`; sem isso, AL3-A
  não ratifica. AL3-B não expõe o tipo, então é imune por construção.
- **Bootstrap seed.** A intrínseca `builder`/`reserve` nova não pode ser USADA pelo
  corpus antes de estar no seed. Sequência: AL3 introduz a intrínseca e SÓ o lowering
  interno (B) a usa primeiro (invisível ao corpus); a superfície A entra no corpus só no
  release seguinte, quando o seed já a tem.
- **Tensão genuína pendente (HALT-candidata, NÃO um HALT ainda):** se AL1 REFUTAR a
  favorita e apontar (c) str-concat como núcleo, a decisão A-vs-B e o alvo (`[]byte` vs
  `str`) muda — mas isso é resolvido POR AL1, não por pergunta. Não há tensão de lei
  irresolvível aqui; a arquitetura é lei-first (passa-todas-as-Leis) em ambos os ramos.
  **Nenhum HALT necessário.** O owner ratifica sobre os números de AL1.

---

## 8. Sequência (ordem inegociável)

**AL1 (prova) SEMPRE primeiro. Nenhum fix antes da prova.** Depois, os dois ataques
complementares em paralelo, cada um dosado pela fatia que AL1 mediu:

- **Frente FONTE** (mata a construção): AL6, 4 níveis. Os níveis 0/2/3 (`[]`,`[...]`,
  `const`) são independentes e correm em paralelo a AL3, sweep-first nos módulos que AL1
  aponta. O nível 1 (`T[N]`) COMPARTILHA o rep `{ptr,len,cap}` e a prova de AL3 (Q2) →
  entra DEPOIS de AL3. Barato, mecânico, fixpoint-gated.
- **Frente CODEGEN** (mata a cópia): AL2 (paliativo, só se AL1 pedir) → AL3 (núcleo,
  builder linear — o rep que `T[N]` reusa) → AL4 (writers, um `T![chunk]` em stack) → AL5
  (lifetime, dono da região onde `T[N]` soft aloca).

AL3 é o destino da frente codegen; AL6 é o destino da frente fonte; juntos cobrem o storm.
AL2 é ponte opcional; AL4/AL5 são higiene contingente ao ranking de AL1. Cada um: fixpoint
+ probe de tempo como ritual, com o baseline de AL1 como régua.
