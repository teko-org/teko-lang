# AL Wave — Emit Throughput + Mutability/Borrow/Array Model (proof-first)

Status: **RATIFICADO (owner, 2026-07-19).** A estrutura em três sub-ondas e o
decision-set abaixo são owner-ruled e convergidos; este doc é a base de implementação
da onda. A onda CRESCEU de "throughput do emit" para "throughput + modelo de
mutabilidade/borrow/array". Reconciliada em três sub-ondas: **FUNDAÇÃO** (modelo de
linguagem) → **THROUGHPUT** (consome a fundação) → **MIGRAÇÃO** (fonte). Execução é
proof-first: **AL1 (prova) roda ANTES de qualquer código de produto** e fecha os números
(blast radius de runtime + censo das constantes disfarçadas). No product code neste doc;
os crumbs bumpam sob seus próprios gates. Ordem: AL1 → AL0 (paralelo) → F1→F2→F3 →
AL2→AL3→AL4→AL5 → AL6.

Decision-set (owner rulings, 2026-07-19): CURA=`push(&x,v)` ref-push · `&x`=borrow mutável
SEGURO (não unsafe) · exclusivo-XOR-shared · mutabilidade INFERIDA (sem `&mut`) · `let`=
imutável PROFUNDO · `mut` fica / `var` não / `let` não morre · array Model A `[N]T`
`{ptr,len,cap}` sem zero-fill · crescimento cap-doubling+pânico no teto · AL6 migração por
nível de conhecimento. Companion: `docs/design/compile-time-architecture.md` (CI wall),
`docs/design/spine-build-plan.md` (o borrow-checker #331 que a FUNDAÇÃO consome),
`docs/design/const-tb*` (rodata const, T-B6).

Disciplina inegociável do owner: **"sem nadar no escuro, precisamos de provas do
problema"**. **AL1 é MEDIÇÃO, não fix**, roda e produz números ANTES de tudo. Nada
downstream é cravado além do que AL1 provar/dimensionar.

---

## 0. TL;DR — estrutura reordenada em três sub-ondas

| # | Crumb | Sub-onda | Tamanho | Behavior | Ritual |
|---|---|---|---|---|---|
| **AL1** | Prova: instrumentação discriminante + censo (push-sites, let-mutantes, níveis) | proof | M | preserva (obs off = zero custo) | fixpoint + probe RODA em self-build, relatório máquina-legível + blast-radius medido |
| **AL0** | **Const-ificação: build-and-return const → `const X=[...]` em rodata, ZERO construção** | MIGRAÇÃO (early, paralelo) | M (sem máquina nova) | muda C do compilador (runtime→rodata) | fixpoint por sub-lote + probe: copy-grow do site → 0 |
| **F1** | **Borrow mutável seguro `&x`** (superfície do `ref`/spine já legislados) | FUNDAÇÃO | L | preserva alvo; muda C do próprio compilador | fixpoint verde; spine `is_unique_at` autoriza |
| **F2** | `let` imutável PROFUNDO (aperto: sem index-write/grow de `let`) | FUNDAÇÃO | S/M | preserva (só 7 index-write hoje) | fixpoint + gate rejeita `grow(&let_x)` |
| **F3** | Array/slice Model A: `[N]T` = `{ptr,len,cap}`, sem zero-fill, growable | FUNDAÇÃO | L | preserva alvo; muda rep interno | fixpoint + probe: cap-hits, zero MISS em tamanho conhecido |
| **AL2** | Endurecer push-cache — PALIATIVO (ponte até F1/AL3 removerem a tabela) | throughput | S/M | preserva | probe: `other-ptr` misses no buffer grande → ~0 |
| **AL3** | **CURA: ref-push `push(&x,v)`** — fim do value-thread e da `tk_push_cache` | throughput | L | preserva alvo; muda C do compilador | fixpoint + probe: `copy_amp(emit) → ~1.0` |
| **AL4** | Writers nativos (`EmitWriter` = `[chunk]T` em stack, sem buffer 8.5MB) | throughput | M | preserva | probe: pico RSS + tempo emit |
| **AL5** | Region-per-phase (arena por fase; dona da região onde `[N]T` soft aloca) | throughput | M/L | preserva | probe: reclaim-ratio ↑, RSS ↓, zero regressão |
| **AL6** | Migração de fonte por nível: `mut []T=[]`+push / `[N]T` / `[...]` / `const` | MIGRAÇÃO | M mecânico (grande) | preserva alvo; muda C do compilador | fixpoint por sub-lote via PONTE de coexistência + probe |

**Ordem:** AL1 → **AL0 (const-ificação, EARLY, em PARALELO à fundação)** → [F1 → F2 → F3] →
AL2 → AL3 → AL4 → AL5 → AL6. AL0 é o topo do espectro de migração (§8) mas roda cedo e em
paralelo porque **não precisa de máquina nova** (literais + `const` T-B6 já no seed) e é o
**provável maior speedup, mais barato** — a "dor real" do owner: eliminar o push que constrói
constante em runtime. A FUNDAÇÃO é pré-requisito do ref-push; AL6 migra o resto dos ~1383
push-sites, gradual pela ponte.

**A hipótese-âncora (confirmada em file:line, §2) e A CURA.** O `push` do runtime já é
amortizado; a doença é que `[]byte`/`[]T` é um valor `{ptr,len}` **sem `cap`**, então o
append amortizado depende de uma **tabela lateral GLOBAL** (`tk_push_cache`, 65536 buckets,
`teko_rt.c:2091-2094`) keyed por ponteiro — finita → **colisões** → clobber do slot do
buffer de saída → copy-grow multi-MB (o próprio código documenta "the 11.5 GB fix",
`teko_rt.c:2155-2160`). **A CURA (owner): `push` recebe um BORROW, não um valor.**
`teko::list::push(&x, v)` muta `x` in-place pelo borrow → acaba o value-thread
`xs = push(xs,x)` → o `cap` vive no valor mutado in-place → **a tabela global e as cópias
defensivas somem por construção**, não por heurística. É o núcleo do AL3, agora surfaced
pela LINGUAGEM (borrow seguro), não por um cache de runtime.

---

## 1. Baseline medido (dado)

- Emit 17.6MB: 201s macOS / 307s Win / 520s ubuntu (~50–90 KB/s). O `cc` consome o MESMO
  `teko.c` em 9–26s → **gerar o C é ~20× mais lento que compilá-lo.**
- Ocorre em C E native → NÃO é o linker; Fase E não resolve; **AL precede Fase E.**
- Barra do owner: **teko builda mais rápido que rustc em codebase de mesmo tamanho.**

---

## 2. A prova-âncora, confirmada (file:line) — ainda load-bearing

- **Runtime JÁ amortizado** (hipótese ingênua REFUTADA): `TK_RT_LIST` faz cap-doubling via
  `realloc` in-place, O(1) amortizado (`teko_rt.h:22-38`); "the caller holds the one live
  copy" (`teko_rt.h:20-21`) — semântica linear já é o contrato.
- **Mas `[]byte`/`str` é `{ptr,len}` SEM `cap`** (`teko_rt.h:45-48, 59-60`). Logo o append
  reconstrói "tem capacidade?" por uma **tabela global keyed por ponteiro**:
  `teko_rt.c:2091-2094` (`TK_PUSH_HASH_SIZE (1<<16)`, `tk_push_slot` = hash de `p>>4`),
  in-place só com HIT (`:2104-2116`), MISS → copy-grow inteiro (`:2152-2154`). Idem
  `tk_append_bytes_fo` (o alvo de `cb` do emit, `:2178-2213`).
- **Colisão é a doença, documentada**: `teko_rt.c:2155-2160` verbatim — 150M inserts tiny
  clobbaram o slot do buffer multi-MB ~2300×, ~7.5k copy-grows espúrios de ~1.5MB = 11.5GB
  de churn (85% do total). Size-aware eviction MITIGOU, não ELIMINOU a classe (dois
  buffers vivos de tamanho comparável ainda brigam pelo slot).
- **A cura de runtime existente ataca a memória, não a colisão**: free-old/free-list
  (`teko_rt.c:2171-2213`, `teko_rt.h:137`) parka o buffer velho — mas um MISS por colisão
  ainda paga a cópia integral ANTES de parkar. **A cópia é o custo.**
- **Ganchos de medição JÁ EXISTEM** (AL1 formaliza): `TEKO_ARENA_OBS`
  (`teko_rt.c:1036-1099`), histograma de miss-reason `empty|other-ptr|len|cap-full|esz/gen`
  (`:1091-1094`, alimentado `:2132-2138`), tabela "dark matter" de malloc str (`:1097-1098`).

**Por que a CURA de linguagem, e não só mais cache**: enquanto `cap` não estiver NO VALOR,
toda amortização é reconstrução probabilística sujeita a colisão. `push(&x,v)` muta o
objeto que carrega `cap` → determinístico, sem tabela, sem colisão. O borrow é a única
forma de dar ao `push` acesso ao objeto-com-`cap` sem copiá-lo.

---

## 3. AL1 — a PROVA (medição). Roda ANTES de tudo. Agora também mede o BLAST RADIUS.

### 3.1 Metodologia e veredito

Self-build com `TEKO_ARENA_OBS` ligado. **Veredito**: hipótese de colisão/cópia PROVADA
sse `copy_amp(emit) > 4×log2(output_bytes)` E a coluna ">1MB" do histograma de miss-reason
é dominada por `other-ptr` (colisão) ou `len` (aliasing); REFUTADA se dominada por
`cap-full` (doublings sãos → villain é dark-matter/str-concat, §3.2). `copy_amp` = bytes
copiados em copy-grows / bytes do output final; sadio ≈ `log2(n)`.

### 3.2 Causas alternativas que AL1 DISTINGUE (testar, não presumir)

(a) interning O(n²) — probe de timing + dark-matter; (b) arena sem size-header — miss
`esz/gen`; (c) **str-concat** (`tk_str_concat`) — tabela dark-matter (se ganhar, a cura é
str-builder, mesmo modelo alvo `str`). AL1 RANKEIA; AL3 só ratifica se a favorita ganhar.

### 3.3 Censo (input do blast radius — AL1 CONTA, o owner VÊ o custo antes de implementar)

AL1 emite, de um passo sobre a AST/TAST:
1. **push-sites classificados pelos 4 níveis (§8)** — o **número-MANCHETE da prova é a
   categoria "constante disfarçada"**: funções build-and-return const-ificáveis (AL0), a dor
   real do owner. Depois: itens-conhecidos (→literal `[a,b,c]`), tamanho-conhecido (→`[N]T`),
   genuinamente dinâmico (→ref-push). Total medido hoje: **~1383 push-sites, ~789
   value-thread** (§7); AL1 quebra esse total por nível.
2. **let-mutantes**: nº de `let` cujo conteúdo é mutado (index-write/grow) — o blast de F2
   (medido hoje: **apenas 7 index-writes** no compilador inteiro, §7).
3. cruzar 1–2 com a atribuição RA1 do dump (`teko_rt.c:1089`) → quanto de custo cada
   nível/site colhe (sweep-first onde AL1 atribui mais custo; a fatia "constante disfarçada"
   dita a prioridade de AL0).

### 3.4 Scaffolding que COMPILA HOJE (design-ahead) — contadores + dump de fase

No seam mantido `teko_rt.{c,h}` (exceção Teko-only): `tk_obs_push_grow_bytes()` (agregado
de copy-grow). Teko-side (`.tks`, full Javadoc):

```teko
/**
 * Amostra de timing de UMA fase do pipeline, para o dump máquina-legível de AL1. Só
 * populado sob `TEKO_PHASE_OBS`; caminho quente não paga nada quando desligado (mesmo
 * contrato de tk_obs_enabled, teko_rt.c:1036).
 *
 * @field name       nome da fase ("lexer".."emit"/"cc")
 * @field elapsed_ns nanosegundos de parede na fase
 * @field out_bytes  bytes produzidos (C gerado, para emit; 0 senão)
 * @since 0.x (#AL1)
 */
type PhaseSample = struct { name: str; elapsed_ns: u64; out_bytes: u64 }

/**
 * Fator de amplificação de cópia do emit: bytes copiados em copy-grows / bytes do output.
 * Sadio ≈ log2(output_bytes). A métrica-veredito de §3.1.
 *
 * @param grow_bytes    bytes movidos em copy-grows (de tk_obs_push_grow_bytes)
 * @param output_bytes  tamanho do C gerado
 * @return              a razão; 0.0 quando output_bytes == 0 (guard)
 * @since 0.x (#AL1)
 */
pub fn copy_amplification(grow_bytes: u64, output_bytes: u64) -> f64
```

---

## 4. Sub-onda FUNDAÇÃO — o modelo de linguagem (pré-requisito do ref-push)

### 4.1 F1 — borrow mutável SEGURO `&x` (L)

**`&x` NÃO é ponteiro cru** (isso é `ptr`, unsafe, `#repr(C)`). É um **borrow exclusivo-
temporário, lifetime-bounded pelo borrow-checker que JÁ existe** (o spine #331,
`spine.tks`; `escape.tks`). Regra **exclusivo-XOR-shared** (à la Rust): um mutável OU
vários imutáveis.

**Mutabilidade INFERIDA, sem `&mut`:** bare `&x` num binding `mut` + posição de param
mutável ⇒ borrow **mutável**; num `let` ou posição de param shared ⇒ borrow **shared**. A
inferência lê o binding + o tipo do param. Sem sigil `&mut`.

**Relação com `ref`/`Reference` (A/B — o design suporta ambos; ruling de taste):**
- Já existe `Reference = struct { inner: Type }` (`checker/type.tks:90`) e a lei já
  legisla `ref` como "a SAFE, region-checked, never-null, Teko-internal reference"
  (`TEKO_LEGISLATION.md:425`). A MÁQUINA já é lei.
- **A — `&x` dessugar para o `Reference`/`ref` existente** (auto-ref de lvalue mut, `.value`
  implícito nos usos-through-ref). Reusa 100% da máquina do spine; `&` é só a grafia de
  superfície do `ref` já legislado. **Recomendado** — menor superfície nova, zero tensão de
  lei (§10).
- **B — `&` como sigil novo sobre a mesma máquina** (um `TBorrow` distinto que lowera pro
  mesmo `Reference`). Só se A colidir com a ergonomia do `ref` explícito.
- **Recomendação: A.** O spine fornece `is_unique_at`/`ref_target_outlives` (`spine.tks`
  PR-1 pura; PR-2/PR-3 as consomem — F1 é esse consumo). O `bf` (BorrowedFrom) do spine já
  é single-assignment, "a ref is parameter-only today ... PR-2's L2a relaxation" para um
  ref-bind local com UM sítio sintático de borrow — a máquina exata do `&x`.

**Corrige o SINK-TO-VALUE:** hoje `mut y = ref/&x` COPIA (perde o alias). Nova regra: quando
o uso é **mutação-through-ref** (`push(&x,…)`, `y` usado só como base de mutação), o borrow
**PRESERVA o alias**. Prova de não-quebra: os casos que HOJE dependem da cópia são LEITURAS
(valor lido é copiado por design); a nova regra só muda o caminho **mutável exclusivo** — que
hoje nem existe como borrow (a mutação é sempre value-thread `x = push(x)`), então **nenhum
caso existente depende da cópia num borrow mutável**. AL1 confirma com censo de `mut y=&x`
sites (esperado ~0 no padrão mutável).

```teko
/**
 * Empurra `v` para o fim de `x`, mutando `x` IN-PLACE via borrow mutável exclusivo `&x`.
 * Substitui o value-thread `x = push(x, v)`: como `x` carrega `{ptr,len,cap}` e é mutado no
 * lugar, o `cap` está sempre no objeto — NENHUMA tabela global tk_push_cache, NENHUMA cópia
 * defensiva, NENHUMA colisão. O borrow é exclusivo-temporário (spine is_unique_at autoriza;
 * nenhum outro alias vivo durante a chamada) e lifetime-bounded (ref_target_outlives).
 * Cresce por cap-doubling; estouro de cap além de u64 → pânico.
 *
 * @param x  o slice-alvo, emprestado mutável e exclusivamente (&x)
 * @param v  o elemento a anexar
 * @return   void — a mutação é o efeito; nada a re-atribuir
 * @throws   pânico se cap estourar u64 (M.1 fail-loud)
 * @since 0.x (#AL3/F1)
 */
pub fn push[T](x: &[]T, v: T) -> void
```

Ritual: fixpoint verde (o C do compilador muda, mas o C emitido para o CORPUS-ALVO não). Blast:
~1383 sites (§7) — gerido pela PONTE (§6).

### 4.2 F2 — `let` imutável PROFUNDO (S/M)

`let` = imutável em binding **E conteúdo**: sem push/grow/index-write. **Auto-enforçado**:
não existe borrow mutável de um `let` (F1 infere shared para `let`), então `push(&let_x,…)`/
`grow(&let_x,…)` é **erro de compilação** por construção. Se o `let` de HOJE é RASO (permite
index-write), F2 é um APERTO. **Blast medido: apenas 7 index-writes no compilador inteiro**
(§7) — aperto de BAIXO risco. Ritual: fixpoint + gate rejeita `grow(&let_x)`; migrar os ≤7
sites a `mut` primeiro (behavior-preserving).

### 4.3 F3 — array/slice Model A (cap/len) (L)

- **`[N]T` = `{ptr, len, cap}`** com cap inicial N; **`= []` → len=0, cap=N**; **GROWABLE**
  (N é RESERVA, não teto). **`[]T` = o mesmo com cap=0.** Um único rep — `Slice` ganha `cap`
  (Q3); `[N]T` é açúcar de fonte que semeia `initial_cap=N`, NÃO um tipo nominal novo.
- **Sem zero-fill**: leitura/índice limitados por `len` ⇒ não-inicializado nunca observável
  ⇒ **não zera a capacidade reservada** (ganho de perf vs zerar N slots).
- **Índice**: `x[i]` i<len sobrescreve; `x[len]` ocupa (≡ push); `x[>len]` → pânico.
- **Alocação**: `[N]T` frame-local de T pequeno → **STACK** (escape.tks prova não-escapa;
  zero heap — ganho enorme p/ buffers curtos quentes); senão a região da fase (AL5). O `cap`
  no valor **sidestepa a tk_push_cache** para todo `[N]T`.
- **`[N]T` ↔ ref-push (AL3)**: mesmo rep. `[N]T` é a DECLARAÇÃO (semeia cap=N); `push(&x)` é
  a OPERAÇÃO sobre o mesmo objeto. Não são paralelos — F3 e AL3 compartilham o rep.
- **Gramática (Q3)**: `[N]T`/`[]T` são o MESMO tipo de runtime (`Slice{element, cap}`), não
  bifurca o checker. Hoje `SliceType={element}` (`parser/type.tks:6`); F3 adiciona
  `initial_cap: Expr?`. **Resolve o caveat da sentinela `Slice{Void}`**: `[N]T = []` dá
  elemento + capacidade, então `[]` é bem-tipado por contexto.

Ritual: fixpoint (rep interno muda, C do alvo preservado) + probe: zero MISS em site de
tamanho conhecido; peak-RSS de stack-alloc.

---

## 5. Sub-onda THROUGHPUT — consome a fundação

### 5.1 AL2 — endurecer a push-cache (PALIATIVO, S/M)

Só se AL1 mostrar colisão viva porém limitada. Tabela maior / N-way / per-região.
**Explicitamente ponte** — F1/AL3 removem a tabela para o caminho de builder. Ritual: probe
`other-ptr` misses no buffer grande → ~0.

### 5.2 AL3 — a CURA: ref-push `push(&x, v)` (L)

O núcleo, surfaced por F1. O value-thread `x = push(x,v)` vira `push(&x, v)`; o `cap` vive no
objeto mutado in-place; **a tk_push_cache e a cópia defensiva somem por construção**. Emit:
`cb`/`append_fo` (`codegen.tks:141-160`) passa a mutar o buffer por `&` em vez de threaded
value — o codegen não muda de FONTE além do `&`, e para de copiar. Consome F3
(`{ptr,len,cap}`) e F1 (o borrow). Behavior-preserving no alvo; muda o C do compilador.
Ritual: fixpoint + probe `copy_amp(emit) → ~1.0`; emit KB/s cumpre a barra (§9). Blast pela
PONTE (§6).

### 5.3 AL4 — writers nativos (M)

`EmitWriter` = um `[chunk]T` frame-local em **stack** (F3), fixo e pequeno, flushed ao fd — em
vez de 8.5MB em memória. Remove o maior alvo de colisão. Ritual: probe pico RSS + tempo emit.

```teko
/**
 * Sink de emit que escreve fragmentos direto num fd em blocos de `chunk`, sem materializar o
 * C gerado inteiro. O staging é um [chunk]byte frame-local (stack, F3) reusado — nunca um
 * valor multi-MB para colidir na cache. Muta por &self (F1). Falha de write → honest-stop.
 *
 * @param w o writer (mutado in-place por &)
 * @param s os bytes a emitir
 * @return  void, ou error num write curto/falho (M.1)
 * @since 0.x (#AL4)
 */
pub fn ew_write(w: &EmitWriter, s: str) -> void | error
```

### 5.4 AL5 — region-per-phase (M/L)

Cada fase recebe uma `tk_region` própria, dropada em um passo no fim — remove a acumulação
root process-lifetime. Primitivas existem (`teko_rt.h:148-152`; reclaim observável
`teko_rt.c:1078-1086`). Dona da região onde `[N]T` soft aloca (F3). Ritual: probe
reclaim-ratio ↑, RSS ↓, zero regressão (diff VM==native + fixpoint).

---

## 6. PONTE DE COEXISTÊNCIA — o que torna o blast radius gerenciável

**Viável: SIM.** As duas assinaturas coexistem durante a migração:
- `teko::list::push(xs: []T, v: T) -> []T` — value-thread, ATUAL, intocada (byte-idêntica).
- ref-push, NOVA (F1/AL3) — a forma borrow.

Ponte por **nome novo durante a migração** (`push_into`/`grow` para a forma borrow),
mantendo `push` value-form intocado e todo código não-migrado compilando; rename final
opcional quando todos migrarem. (Alternativa: overload de `push` discriminando `x` vs `&x` —
mas o nome novo evita ambiguidade de resolução e mantém `push` value-form byte-idêntico.
**Recomendação: nome novo**, rename ao final.)

Isso permite migração **crumb-a-crumb com fixpoint verde a cada passo**: cada sub-lote de AL6
migra um módulo de value-thread → ref-push, roda o gate, segue. **Nenhum cutover duro.**

**FIXPOINT preservado**: a migração muda a espinha de alocação INTERNA e a grafia da fonte do
compilador, **NÃO os bytes emitidos do programa-alvo** (o compilador compila o corpus de
teste identicamente antes e depois). Marcação por crumb:
- **Preserva-alvo, muda-C-do-compilador** (gen2==gen3 entre si; C emitido para o corpus-alvo
  byte-idêntico): F1, F3, AL3, AL6 (níveis push/`[N]T`). Prova: golden do corpus-alvo + diff
  VM==native + fixpoint.
- **Preserva tudo**: AL1, AL2, F2 (≤7 sites), AL4, AL5.
- **Muda-bytes-com-justificativa-medida**: **AL0** const-ificação (runtime→rodata) — probe: o
  copy-grow daquele site → 0 no dump RA1.

---

## 7. BLAST RADIUS quantificado (medido hoje — o owner VÊ o custo)

| Eixo | Medido (src não-test) | Consequência |
|---|---|---|
| push-sites totais (`list::push`/`push_fo`) | **~1383** | cada ganha `&` no acumulador → ~1383 inserções de `&` (mecânico) |
| forma value-thread `x = push(x, …)` | **~789** | os sites que trocam para ref-push (perdem o `x =`) |
| `push_fo`/`append_fo` (emit hot-path) | **41** | o caminho quente de `cb` — prioridade de AL3 |
| index-writes `x[i] = …` (blast de F2) | **7** | `let`-profundo é aperto de BAIXO risco (só 7 sites) |
| `let` totais / `mut` totais | 4405 / 3410 | o value-thread já exige `mut` (reassignment) → `let`-profundo NÃO os toca |
| `&`-a-introduzir | ≈ push-sites migrados (~1383, gradual) | mecânico, sub-lote por módulo via ponte |
| arquivos .tks não-test | 132 | escopo do sweep de AL6 |

**Leitura**: blast GRANDE em push-sites (~1383) mas MECÂNICO e GRADUAL (ponte, fixpoint por
sub-lote). O `let`-profundo (F2) é quase de graça (7 sites). AL1 refina com o censo exato +
o peso de custo por site.

---

## 8. Migração de fonte por nível de conhecimento — AL0 (const, o topo) + AL6 (resto)

O owner: **o maior ganho NÃO é otimizar push — é ELIMINAR o push que constrói uma constante
em runtime.** Por isso a const-ificação (AL0) é crumb de PRIMEIRA CLASSE e EARLY, não o rabo
do AL6. Espectro de 4 níveis, do topo (mais ganho, mais barato) pra baixo:

| Nível | Conhecimento | Crumb | Sintaxe | O que morre |
|---|---|---|---|---|
| **const** | build-and-return const (nullary/args const, sem arg de runtime) | **AL0** | `const X = [...]` | **TUDO — rodata, construída ZERO vezes** (T-B6, já no seed) |
| itens | valores fixos, não-const | AL6 | `[t1, t2, t3]` | a construção (um malloc dimensionado, `codegen.tks:2865-2887`) |
| tamanho | sei o tamanho, não os itens | AL6 | `mut [N]T = []` + índice/push | os copy-grows (cap=N semeado, zero MISS) |
| dinâmico | nada (loop runtime, tamanho desconhecido) | AL6 | `mut []T = []` + `push(&x, v)` | a CÓPIA (ref-push, cap no objeto) |

### 8.1 AL0 — const-ificação (a dor real do owner). PRIMEIRA CLASSE, EARLY, PARALELO.

**Padrão-alvo:** uma função que só CONSTRÓI-E-RETORNA um array fixo — nullary, ou com args
const, resultado compile-time-constante, **sem depender de nenhum arg de runtime** — vira
`const X = [...]` em rodata, **construída ZERO vezes** no processo. É o padrão das tabelas de
ABI/opcode já migradas (backend `abi_*`/`encode_*`/`isel_*`); o owner afirma que **HÁ MUITO
MAIS** escondido como "constante disfarçada" (build-and-return em `empty()+push`).

- **Sem máquina nova**: literais + `const` agregada já funcionam no seed (T-B6). Por isso AL0
  roda EM PARALELO à FUNDAÇÃO e ao AL1 — não espera F1/F3/AL3. **Provável MAIOR speedup, mais
  barato.**
- **Chamador que muta (owner-confirmado):** a const FICA. Quem precisa mutar faz `mut c =
  CONSTANTE` — cópia eager da const pro `mut` (semântica de valor que JÁ existe) — e cresce
  com `push(&c,…)`/`grow(&c,…)`. **Leitor comum referencia a const DIRETO (zero cópia); só
  quem muta paga uma cópia**, uma vez, na materialização do `mut`.
- **Produtor PARAMETRIZADO** (arg de runtime muda o array) → **CONTINUA função**, com literais
  internos (`[a,b,c]` em vez de `empty()+push`). NÃO vira const única. AL0 NÃO toca esses — vão
  para o nível "itens" do AL6.
- Behavior: MUDA o C do compilador (construção em runtime → imagem em rodata) — justificado
  pelo ganho MEDIDO (o copy-grow daquele site → 0 no dump RA1 de AL1). Ritual: fixpoint por
  sub-lote + probe.

### 8.2 AL6 — o resto (itens/tamanho/dinâmico)

`empty()+push` de itens fixos não-const → literal `[a,b,c]` (rewrite simples, um malloc).
Tamanho conhecido → `[N]T` (F3). Dinâmico → `push(&x)` (AL3). Níveis itens correm em paralelo
à FUNDAÇÃO (literais já no seed); tamanho/dinâmico dependem de F1+F3+AL3 → entram DEPOIS.

**Convenção W15 (a ratificar junto):** "const disfarçada → `const X=[...]`; sei os ITENS →
`[...]`; sei o TAMANHO → `[N]T`; só o genuinamente dinâmico usa `push(&x)`."

Cada sub-lote (AL0 e AL6): fixpoint via ponte + probe (o site migrado some do dump RA1),
sweep-first onde AL1 atribui mais custo. Estilo dos crumbs "byte-idêntico" S1–S4 de #594.

---

## 9. Barra de aceitação — "mais rápido que rustc mesmo-tamanho"

Reprodutível: (1) corpus fixo (o compilador + N-LOC sintético); (2) **emit throughput**
`emit_kbps = output_bytes/emit_ns`, piso derivado do baseline (~50–90 KB/s), teto = paridade
com `cc`; (3) **`copy_amp(emit) → ~1.0`** (sinal direto de que a cópia morreu); (4) **razão
wall `teko/rustc ≤ 1.0`** em codebase de LOC/IR comparável, lane de perf PERIÓDICA (não
por-PR — evita ruído). Todo crumb que mexe em rep/hot-path tem probe de tempo como ritual,
baseline de AL1 como régua.

---

## 10. Riscos + tensões de lei (com resolução)

- **A regra "sigils unsafe-only" — o borrow seguro RELAXA a lei?** **NÃO — a lei JÁ carrega a
  carve-out.** `TEKO_LEGISLATION.md:425` legisla: "The SAFE, region-checked, never-null,
  Teko-internal reference is `ref` (evolution S3) — orthogonal to the unsafe foreign `ptr`."
  O UNSAFE é `ptr`/`*`/`#repr(C)` (`:369, :425`); o `ref`/`&x` é a referência SEGURA já
  prevista, checada pelo spine (região/lifetime/uniqueness). Logo `&x` NÃO é um sigil unsafe
  novo — é a superfície do `ref` (S3) ratificado. **Sem tensão de lei nova.** A única decisão
  ABERTA é de TASTE (grafia `&x` sigil vs keyword `ref`), não de lei — A/B de §4.1,
  recomendação A. **Nenhum HALT.**
- **Blast radius (§7).** ~1383 push-sites. Resolução: PONTE (§6) → gradual, fixpoint por
  sub-lote. Nunca cutover duro.
- **Fixpoint / byte-identidade.** F1/F3/AL3/AL6 mudam o C do compilador mas preservam o C
  emitido para o corpus-alvo. Resolução: golden do alvo + diff VM==native + fixpoint
  gen2==gen3; marcação por crumb em §6.
- **`let`-profundo pode quebrar código que muta conteúdo de `let`.** Blast: 7 index-writes
  (§7). Resolução: migrar os ≤7 a `mut`, depois apertar; baixo risco.
- **Sink-to-value (mudança de regra).** Não-quebra provado em §4.1: a preservação de alias só
  afeta o caminho mutável-exclusivo, inexistente hoje. AL1 confirma com censo.
- **Soundness §7 (cópia de valor copia ponteiro).** `borrow.tks:1-20`. Resolução:
  exclusivo-XOR-shared + spine `is_unique_at` garantem que um borrow mutável nunca coexiste
  com outro alias; máquina já existe.
- **Bootstrap seed.** `&x`/`[N]T`/ref-push introduzem grafia+rep novos → o corpus só os USA
  depois que F1/F3/AL3 os semearem. Níveis itens/const de AL6 (já no seed) varrem antes.

---

## 11. Sequência (inegociável)

**AL1 (prova) SEMPRE primeiro — nenhum fix antes dos números (incl. o censo de blast
radius, cujo número-manchete é a fatia "constante disfarçada").** **AL0 (const-ificação)
arranca CEDO, em paralelo à FUNDAÇÃO** — não precisa de máquina nova e é o provável maior
ganho. Depois a FUNDAÇÃO (F1 borrow-seguro → F2 `let`-profundo → F3 `[N]T` cap/len), que
habilita o ref-push; depois THROUGHPUT (AL2 paliativo opcional → AL3 cura → AL4 writers → AL5
lifetime); por fim MIGRAÇÃO (AL6, gradual pela ponte). Cada crumb: fixpoint + probe de tempo
como ritual, baseline de AL1 como régua. AL0 e o nível itens de AL6 correm em paralelo à
FUNDAÇÃO (não dependem do rep novo).

---

## 12. PROSPECÇÃO / exploração futura (PARKADO — NÃO nesta onda, NÃO nos crumbs)

Registro para não perder, SEM design aqui. O owner PARKOU a questão da superfície de
referência ("atacar em outra frente e refinar melhor"). Nesta onda, **F1 fica com `&x`
(grafia A, dessugar para a `Reference` existente) e a superfície `ref`/`Reference`
INALTERADA.**

Ideia parkada: **unificar a superfície de referência nos sigils `&`/`*`** — ex. `mut *x: T`
≡ `mut x: Reference<T>`, potencialmente aposentando o keyword `ref` e tirando `Ref<T>` da
superfície. Relacionado ao "repensar `Ref<T>`" que o owner tem parado. **Toca a lei "sigils
unsafe-only"** (hoje `*`/deref é território unsafe), então **pede design próprio, tipo
keystone, DEPOIS da AL** — não é avaliado, não é quantificado, não mexe na lei nesta onda.
