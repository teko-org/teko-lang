# F3 — Array/slice Model A (`{ptr,len,cap}`) — crumb plan (0.3.1)

Status: **DESIGN (architect, 2026-08-06).** This turns the ratified F3 design
(`al-wave-emit-throughput.md` §4.3) into a concrete, ordered, gate-able crumb sequence with a
quantified blast radius. F3 is the **AL-Wave FUNDAÇÃO keystone**: it is the single rep change that
simultaneously unblocks **AL3** (ref-push, the O(n²) copy-grow cure, ~1.8 GB peak — plan §2) AND
**CK1' O(1)** (true open-addressing in the checker `NameIndex` — `ck1-scoped-hash-maps-0.3.1.md`
§11). No product code here; each crumb bumps under its own gate.

Base: `fix/retirement`. Reconciled with `docs/memory/achatamento-de-n2-plano-0.3.1.md` (F3 = the
cross-front keystone, §2/§8) and `al-wave-crumbs.md` (F1/F2/F3 foundation).

---

## 1. Veredito — F3 é o ÚNICO degrau que falta da FUNDAÇÃO; está DESBLOQUEADO

**F1 (borrow `&x`/`ref`) JÁ ATERRISSOU** — não é design-ahead, é fato no `fix/retirement`:
- Parser: nó `Borrow { operand }` produzido em `src/parser/parse_expr.tks:500` (prefixo `&x`).
- Checker: `type_borrow`/mutabilidade inferida + `type_reaches_ref` soundness spine em
  `src/checker/borrow.tks:1-40` (SP-1a) e `src/checker/typer.tks` (auto-ref).
- Codegen + native: `git log` `e075aefb` "lower TBorrow (&x) as lower_addr_of_place in native
  backend".
- **A PONTE de coexistência (AL-Wave F1.5) JÁ EXISTE**: `pub fn grow<T>(ref x: []T, v: T)`
  em `src/list/list.tks:17`, corpo write-through `x.value = teko::list::push(x.value, v)` —
  behavior-idêntico ao value-form, **cap-in-object explicitamente adiado para "F3+AL3"**
  (`src/list/list.tks:9,14`). O bridge está pronto e ESPERA a primitiva que F3 entrega.

**F2 (`let` imutável PROFUNDO) JÁ ESTÁ NO ESSENCIAL**: `src/checker/typer.tks:4262` — "a `let`/
`const` binding can never carry [a Reference] — deep immutability"; o borrow mutável de um `let`
é rejeitado por construção (mutabilidade inferida). O aperto index-write-de-`let` é baixo risco
(§8). **F2 não bloqueia F3.**

**Conclusão: F3 NÃO precisa de DESIGN-AHEAD — os pré-requisitos (F1, F2) estão na árvore.** O que
falta é a REP: hoje o slice é `{ptr,len}` SEM `cap` (o `cap` vive na tabela global
`tk_push_cache`, `src/runtime/teko_rt.c:1332,1356`). Evidência canônica:
`src/codegen/codegen.tks:1771-1773` — `cg_slice_typename` emite `struct { <elemC> *ptr; uint64_t
len; }` (**duas** palavras, MESMA forma de `tk_str`). É essa struct que F3 alarga para três.

---

## 2. O que F3 muda (a REP, não a superfície de mutação)

Da `al-wave-emit-throughput.md` §4.3, tornada concreta:

1. **`Slice` = `{ptr, len, cap}`** — um único rep de runtime. `[]T` = cap inicial 0; `[N]T` =
   açúcar de FONTE que semeia `initial_cap = N` (N é RESERVA, não teto — GROWABLE). NÃO é um tipo
   nominal novo: `checker::Slice = struct { element }` (`src/checker/type.tks:86`) permanece o
   tipo; `cap` é um campo de REP (runtime), não de identidade de tipo.
2. **Sem zero-fill**: leitura/índice limitados por `len` ⇒ a capacidade reservada NUNCA é
   observável ⇒ não se zera. (Ganho de perf vs. zerar N slots.)
3. **Índice (Model A)**: `x[i]` (i<len) sobrescreve in-place; `x[len]` (len<cap) OCUPA (≡ push,
   len++); `x[len]` (len==cap) cresce; `x[>len]` → pânico `tk_panic_oob`.
4. **`[N]T` frame-local de T pequeno → STACK** quando `escape.tks` prova não-escape (zero heap
   para buffers curtos quentes); senão a região da fase (AL5). O `cap` no valor **sidestepa a
   tk_push_cache** para todo `[N]T`.
5. **`[N]T` ↔ ref-push compartilham o rep**: `[N]T` é a DECLARAÇÃO (semeia cap=N); `grow(ref x,v)`
   é a OPERAÇÃO sobre o mesmo objeto. F3 e AL3 não são paralelos — dividem a struct.

**O idioma index-write JÁ EXISTE e é O(1) hoje** (não é invenção de F3): `x[i] = v` parseia
(`src/parser/parse_stmt.tks:84-89` `AssignKind::Index`), tipa (`src/checker/typer.tks:4432-4460`
`type_index_assign`, exige slice `mut`), e baixa para `<slice>.ptr[<i>] = v` in-place
(`src/codegen/codegen.tks:8709-8721` `emit_index_slot`). F3 ESTENDE esse idioma ao modelo cap
(regra 3 acima) e o torna SÃO cross-função via o `ref` de F1 (§8 soundness).

---

## 3. Sequência de crumbs (ordenada, cada uma gate-able sozinha, bootstrap-safe)

| # | Crumb | Tam | Behavior | Ritual (gate) |
|---|---|---|---|---|
| **F3.1** | **Alargar a REP para `{ptr,len,cap}`** — `cg_slice_typename` emite 3 campos; `teko_rt.h` `tk_slice_byte`/`tk_char` ganham `cap`; native LIR constrói 3 campos. `cap` presente mas INERTE (sempre 0). | L | muda-rep (2→3 campos, uma vez) | **fixpoint gen2==gen3** (a rep nova é estável); native validation exit-codes idênticos |
| **F3.2** | **Semear `cap` na construção** — `tk_slice_push`/`tk_slice_with_cap`/array-lit gravam o `cap` real (do `realloc`) no header. `cap` TRACKED, ainda cross-checado pela cache global (coexistência). | M | preserva (cap correto, ainda não consultado p/ decisão) | fixpoint gen2==gen3; probe: `cap` == backing real |
| **F3.3** | **Primitiva in-place `tk_slice_grow_inplace` + rewire do bridge `grow`** — nova primitiva no seam mantido que muta `{ptr,len,cap}` DIRETO (cap-doubling, pânico no teto u64), SEM tabela global; o corpo de `grow<T>(ref x, v)` (`list.tks:17`) passa a chamá-la. **Aqui morre a dependência da `tk_push_cache` no caminho ref-push.** | M | preserva alvo; muda C do compilador | fixpoint gen2==gen3; **probe: site ref-push → zero miss `other-ptr`**; `copy_amp(grow) → ~1.0` |
| **F3.4** | **`[N]T` reserve na superfície** — parser `SliceType` ganha `initial_cap: Expr?` (`parse_slice`); `[N]T = []` semeia cap=N via `tk_slice_with_cap`; resolve o caveat da sentinela `Slice{Void}` (o `[N]T` dá elemento+capacidade). **Bootstrap-safe: `src/` NÃO usa `[N]T` nesta carga** (só é ensinado; adotado na próxima). | M | aditivo (nada usa ainda) | fixpoint gen2==gen3 INALTERADO (aditivo); parser_test + checker_test |
| **F3.5** | **Sem zero-fill + `x[len]` occupy + stack-alloc frame-local** — `emit_index_assign` estende o in-bounds check ao `x[len]` (occupy≡push); `escape.tks` prova não-escape → `[N]T` em stack; reserva não zerada. | M | preserva alvo; muda C do compilador | fixpoint gen2==gen3; **probe: peak RSS ↓ em buffers stack-alloc'd**; zero MISS em tamanho conhecido |
| **F3.6** | **Fixtures de regressão + ritual final** (§9) — o degrau que PROVA a fundação. | S | — | **RITUAL: fixpoint gen2==gen3 byte-idêntico + `teko test .` + peak-RSS/fase** |

**Ordem inegociável**: F3.1 (rep) → F3.2 (semear) → F3.3 (in-place, mata a cache) → F3.4 (`[N]T`
superfície) → F3.5 (no-zero-fill/occupy/stack) → F3.6 (ritual). F3.1–F3.3 entregam a primitiva
de AL3; F3.4–F3.5 entregam a primitiva de CK1'. Cada crumb roda seu gate antes do próximo.

### 3.1 O SEAM: por que F3.1 acopla runtime + codegen + native num degrau

O `tk_slice_byte` do `teko_rt.h:62-65` é PRÉ-DECLARADO e o programa gerado emite o seu próprio
`tk_slice_byte` typedef (`codegen.tks:4251` "a []byte lowers to tk_slice_byte, a DISTINCT C type")
— os dois TÊM de concordar campo-a-campo, senão o `cc` falha. Logo o alargamento da struct no
seam mantido (`teko_rt.{c,h}` — a exceção Teko-only) e no `cg_slice_typename` é UM degrau atômico,
não dois. O native backend (`src/lir/lower.tks`) constrói o mesmo struct e entra no MESMO degrau.

### 3.2 Ponto de ritual (o full gate obrigatório)

- **F3.1** (a mudança de rep) e **F3.6** (o fechamento) são os PONTOS DE RITUAL: o gate completo
  tem de passar (fixpoint gen2==gen3 byte-idêntico + `teko test .` + medição de pico/fase).
- F3.2–F3.5 rodam o gate reduzido (fixpoint gen2==gen3 do sub-lote + probe do crumb), mas nenhum
  avança sem o seu probe VERDE.

---

## 4. A sutileza do fixpoint — F3 é gen2==gen3, NÃO gen1==gen2

**F1 foi ADITIVO** (nada no corpus usava `&x` ⇒ gen1==gen2 inalterado, `al-wave-crumbs.md:177`).
**F3 é DIFERENTE: muda a REP de TODO slice** — os bytes de C emitidos para qualquer programa
mudam (2→3 campos). Isso NÃO quebra a lei do fixpoint, porque o gate inegociável do plano é
**gen2==gen3** (`achatamento-de-n2-plano-0.3.1.md` §0/§6), não gen1==gen2:

- **gen1** = o compilador F3-aware COMPILADO pela SEMENTE antiga (emite rep 2-campos para a
  fonte do compilador). É um artefato de transição.
- **gen2** = a fonte compilada por gen1 — agora EMITINDO rep 3-campos.
- **gen3** = a fonte compilada por gen2 — também 3-campos. **gen2==gen3 é o ponto fixo.**

A mudança 2→3 campos é absorvida UMA vez, na fronteira gen1→gen2 (exatamente como qualquer degrau
de bootstrap que muda rep). O `al-wave-emit-throughput.md` §6 marca F3 "preserva-alvo": entenda-se
o COMPORTAMENTO/exit-codes do corpus-alvo preservado, e o C emitido byte-idêntico ENTRE gen2 e
gen3. Qualquer diff gen2↔gen3 é regressão. **Carga bootstrap-safe (`§5 do plano`): F3.4 só ENSINA
`[N]T`; `src/` só o ADOTA na carga seguinte, para a semente anterior continuar construindo gen1.**

**Interino (native #112 ABERTO)**: enquanto o fixpoint nativo `gen2==gen3` não fecha (PR #112), a
prova de cada crumb é **fixpoint C-path + `teko test .` + timing/peak-RSS** (a mesma disciplina que
`ck1-scoped-hash-maps-0.3.1.md` §11 adota). Ao fechar #112, re-rodar F3.1/F3.6 sob o fixpoint
nativo é confirmação, não redesenho.

---

## 5. Blast radius quantificado (o owner VÊ o custo)

**Distinção crucial**: o blast de **F3 (a REP)** é SEPARADO do blast de **AL3 (migração dos
push-sites)**. F3 muda a FORMA da struct; AL3 (crumb seguinte, fora deste doc) migra os ~1383
sites de value-thread para `grow(ref x)`.

| Eixo (medido hoje em `fix/retirement`) | Nº | Consequência |
|---|---|---|
| `cg_slice_typename` — o typedef central da rep | **1** (`codegen.tks:1773`) | o ÚNICO ponto que decide 2→3 campos |
| sites de emissão de slice-lit/`{0}`/`with_cap` no codegen | **~68** | `{0}` empties ganham `cap=0` de graça (zero-init); só os que setam `.ptr`/`.len` explícito precisam de `.cap` |
| refs a `tk_slice_` no codegen | **~60** | superfície de auditoria da rep |
| seam mantido `teko_rt.c` (`tk_slice_*`/`push_cache`/`append_bytes`) | **97 refs** | a primitiva in-place + o typedef `tk_slice_byte` (`teko_rt.h:62-65`) ganham `cap`; a exceção Teko-only autoriza |
| refs `tk_slice_byte`/`tk_char`/`cap` no `teko_rt.c` | **57** | consumidores que assumem layout 2-campos — auditar |
| native LIR (`src/lir/lower.tks`) refs a slice/`.len` | **603** | superfície de alargamento do backend nativo (a maioria é LEITURA `.len`; a CONSTRUÇÃO ganha cap) — **#112 aberto ⇒ gate C-path interino** |
| sites `Slice { element` no checker | **24** | inalterados (cap é rep, não tipo); só o `[N]T` carrega `initial_cap` |
| parser: `parse_slice` + `SliceType` | **2** (`parse_type.tks:236`, `type.tks:6`) | o `initial_cap: Expr?` do F3.4 |
| **— consumidores (NÃO F3, contexto) —** | | |
| push-sites totais (censo AL1, src não-test) | **~1383** | migração de AL3, via ponte, gradual |
| value-thread `x = push(x,…)` | **~789** | os sites que AL3 troca por `grow(ref x)` |
| `push_fo`/`append_fo` (emit hot-path) | **41** | prioridade de AL3 |
| index-writes `x[i]=…` HOJE (recount, todo `src/`) | **41** | **CONFIRMA que index-write já é idioma estabelecido**; o subconjunto slice-target não-test do censo AL1 é 7 (o aperto de F2) |
| `arr_replace_at`/`arr_drop_at`/`arr_drop_last` callers | **11** | os O(n)-copy que CK1'/collections trocam por index-write in-place |

**Leitura**: o blast de F3 é CENTRALIZADO (1 typedef decide a rep; ~68 sites de construção, a
maioria `{0}` grátis) + um SEAM mantido (teko_rt) + uma superfície nativa larga mas majoritariamente
de LEITURA (603 refs, o grosso `.len`). É L mas contido — não os ~1383 sites de AL3.

---

## 6. Os dois consumidores — a primitiva EXATA que cada um consome

### 6.1 AL3 (ref-push) consome: `tk_slice_grow_inplace` via `grow(ref x)`

O `grow<T>(ref x: []T, v: T)` (`list.tks:17`) HOJE write-throughs para `teko::list::push`
(value-form + cache global). **F3.3 troca o corpo por uma chamada à primitiva in-place** que muta
`{ptr,len,cap}` direto:
- lê `x.value.cap`/`x.value.len` do OBJETO (não da tabela global);
- se `len < cap`: escreve `ptr[len] = v`, `len++` (O(1), zero cópia, zero cache, zero colisão);
- se `len == cap`: `realloc` in-place para `cap*2` (pânico se estourar u64), atualiza `ptr`/`cap`,
  então escreve.

Como o `cap` VIVE no objeto mutado in-place, **a `tk_push_cache` e a cópia defensiva somem por
construção** — exatamente a cura WHOLESALE de AL3 (`al-wave §5.1`). AL3 então migra os ~1383 sites
de value-thread para `grow(ref x)`, cada um colhendo a primitiva. **F3 entrega a primitiva e a
auto-prova no ÚNICO bridge existente (`list.tks:17`); AL3 a espalha.**

### 6.2 CK1' (open-addressing O(1)) consome: `[N]T` reserve + index-overwrite in-place

O `ck1-scoped-hash-maps-0.3.1.md` §11 diz: quando F3 fechar, `NameIndex` troca o rep interno
(slots ordenados → tabela open-addressing com `a[i]=v`) **SEM mudar a API pública**
(`nidx_build`/`nidx_lower_bound` — ou um `nidx_probe` equivalente); os sítios CK1.1–CK1.4 não mudam
uma linha. **F3 confirma que dá exatamente isso:**
- **`[N]T` reserve (F3.4)**: `mut table: [cap]NameSlot = []` aloca `cap` slots sem zero-fill,
  cap-no-header — a espinha da tabela open-addressing dimensionada de uma vez (O(n), não O(n log
  n) do merge-sort de hoje, `nidx_sorted`).
- **index-overwrite in-place (já O(1), estendido em F3.5)**: `table[slot] = entry` baixa para
  `table.ptr[slot] = entry` (`emit_index_slot`, `codegen.tks:8709`) — o slot-write O(1) que o
  open-addressing exige.
- **Padrão de construção**: `nidx_build` preenche `cap` sentinelas EMPTY via `grow(ref t, EMPTY)`
  (len→cap) e então sobrescreve por probe (`t[slot]=entry`, i<len) — O(n) total. A consulta
  `nidx_lower_bound` (binary-search) vira `nidx_probe` (probe linear no bucket), **mas os SÍTIOS
  de consulta (CK1.1–1.4) já passam pelo padrão canônico** (`ck1 §3`), então trocam o rep interno
  sem tocar as chamadas. **Confirmado: F3 dá o O(1) com a MESMA API.**

Nota de soundness p/ CK1': a tabela é construída LOCAL a `nidx_build` (um `mut table` frame-local),
preenchida e então CONGELADA (threaded read-only). O index-write in-place é são porque o `mut`
local é o dono exclusivo durante a construção — não precisa nem do `ref` cross-função de F1 (o
mesmo motivo que os 41 index-writes de hoje já são sãos). **CK1' precisa só de F3, não de F1.**

---

## 7. Gate + medição (plano §6 — o oráculo)

Três números, medidos ANTES e DEPOIS de cada crumb (`phase_begin`/`phase_end_ok` já existem):

1. **fixpoint gen2==gen3 byte-idêntico** — inegociável (interino: C-path + `teko test .`, native
   #112 aberto).
2. **pico RSS do build** — alvo < 2.5 GiB (hoje ~2.53), meta 1.5 GiB. F3.3 (in-place, mata cache)
   e F3.5 (stack-alloc) são os que têm de MOSTRAR a queda.
3. **tempo por fase** + o probe específico do crumb:
   - F3.3: `other-ptr` miss no caminho ref-push → **zero**; `copy_amp(grow) → ~1.0`.
   - F3.4/F3.5: site de tamanho conhecido → **zero MISS**; peak-RSS de stack-alloc.

**Inversão obrigatória**: um crumb de lógica (F3.3/F3.5) cujo probe não mostre o ganho declarado
REPROVA — o número é o portão, não a intenção (`achatamento §6`).

---

## 8. Riscos + tensões de lei (com resolução)

- **[MAIOR] `cap`-no-objeto + semântica de valor = furo de aliasing.** Um slice é copiado por
  valor (`{ptr,len,cap}` compartilha o backing buffer). Se um `grow` in-place realoca e atualiza o
  header ORIGINAL, uma CÓPIA velha ainda aponta pro buffer antigo com `cap` velho — leitura/escrita
  stale. **Resolução (lei já existe, sem tensão nova)**: o grow in-place SÓ é são através do `ref`
  exclusivo de F1 — `spine is_unique_at` garante que NENHUMA cópia viva coexiste durante o grow
  (`borrow.tks:type_reaches_ref` SP-1a; `al-wave §10 "Soundness §7"`). O value-form `push`
  CONTINUA copiando (nunca muta in-place). Regra dura: **in-place APENAS via `ref`; value-form
  preserva cópia.** F1 já legisla e checa isso; F3 só não pode violá-lo.
- **Fixpoint é gen2==gen3, não gen1==gen2 (§4).** Risco de alguém tratar F3 como aditivo e ler
  gen1≠gen2 como regressão. Resolução: gate explícito em gen2==gen3; gen1 é transição.
- **Seam de layout (native #112 aberto).** O backend nativo (`lower.tks`, 603 refs) e qualquer C
  hand-written que assuma `{ptr,len}` 2-campos quebra. Resolução: F3.1 acopla rt+codegen+native num
  degrau atômico (§3.1); gate C-path interino; auditar os 57 refs `tk_slice_byte`/`tk_char` do rt.
- **`let`-profundo (F2) e o grow.** Um `[N]T` growable não pode ser mutável através de um `let`.
  Resolução: F2 já rejeita borrow mutável de `let` (`typer.tks:4262`); F3.5 só herda a lei. Blast
  do aperto index-write-de-`let`: 7 sites não-test (censo AL1) — migrar a `mut` primeiro.
- **Sigils unsafe-only.** Nenhuma tensão: `&x`/`ref` é a referência SEGURA já legislada
  (`TEKO_LEGISLATION.md:425`), e F3 não introduz sigil novo — só alarga uma struct. `*`/deref
  intocado. **Nenhum HALT.**
- **Sem zero-fill vs. observabilidade.** Ler a capacidade reservada seria ler lixo. Resolução: todo
  índice/leitura é limitado por `len` (regra 2/3 de §2); o não-inicializado nunca é observável.
  CK1' preenche EMPTY explicitamente antes de sondar (§6.2) — não lê slot não-escrito.

**Nenhuma tensão de lei genuína ⇒ nenhum HALT.** A única decisão é de ENGENHARIA (a sequência
acima), resolvida law-first.

---

## 9. Regression fixtures (inputs → exit-code nativo esperado)

Padrão do repo: `src/<mod>/<mod>_test.tkt` (asserts Teko) + programas end-to-end rodados
natively para exit-code. Adicionar:

| Fixture | Onde | Entrada | Esperado (native) |
|---|---|---|---|
| slice-rep-cap | `src/codegen/codegen_test.tkt` | emitir um `[]T`; inspecionar o typedef | 3 campos `{ptr,len,cap}`; exit 0 |
| grow-inplace-ok | `src/list/list_test.tkt` | `mut x=[]; grow(ref x, a); grow(ref x, b); x.len==2` | in-place; `x[0]==a`, `x[1]==b`; exit 0 |
| grow-many-nocopy | e2e probe | `mut x=[]; loop i in 0..100000 { grow(ref x, i) }; x[99999]==99999` | zero miss `other-ptr` no dump obs; exit 0 |
| reserve-N | `src/parser/parser_test.tkt` + `checker_test.tkt` | `mut t: [8]byte = []` | `SliceType{initial_cap=8}`; cap semeado 8, len 0; exit 0 |
| index-overwrite | `src/checker/checker_test.tkt` | `mut t: [4]i64 = []; grow×4; t[2]=9; t[2]==9` | in-place overwrite i<len; exit 0 |
| index-occupy-at-len | `src/codegen/codegen_test.tkt` | `mut t: [4]i64 = []; t[0]=1` (len==0, occupy) | ocupa slot, len→1; exit 0 |
| index-oob-panic | e2e probe | `mut t: [2]i64 = []; grow×1; t[5]=9` | pânico `tk_panic_oob`; exit ≠0 |
| reject-grow-let | `src/checker/checker_test.tkt` | `let x=[]; grow(ref x, v)` | erro "immutable"; exit ≠0 (F2) |
| stack-local-noheap | e2e probe | `[N]byte` frame-local, escape prova não-escapa | peak RSS ↓; zero heap p/ o buffer; exit 0 |
| nidx-open-addr | `src/checker/nidx_test.tkt` (CK1' antecipa) | `nidx_build` sobre `[N]NameSlot`, probe hit/miss | lookup correto; exit 0 (prova a primitiva CK1') |
| e2e-fixpoint | ritual | corpus completo, gen2 vs gen3 | **gen2==gen3 byte-idêntico** |

---

## 10. Assinaturas Teko que o implementador adiciona (verbatim, full-Javadoc)

O seam C (`tk_slice_grow_inplace`) é C mantido (exceção Teko-only) — declarado em `teko_rt.h`. O
lado Teko:

```teko
/**
 * A grafia de fonte `[N]T` — um slice `[]T` cuja capacidade inicial reservada é `N` (uma RESERVA,
 * não um teto: o slice cresce além de `N` por cap-doubling). Semeia `initial_cap = N` no rep
 * `{ptr,len,cap}` de Model A; `[N]T = []` produz len=0, cap=N SEM zero-fill (a capacidade reservada
 * nunca é observável porque todo índice/leitura é limitado por `len`). Não é um tipo nominal novo:
 * resolve para o mesmo `checker::Slice{element}`, só carregando a capacidade-semente. Resolve o
 * caveat da sentinela `Slice{Void}` — `[N]T = []` fixa elemento E capacidade, então `[]` é
 * bem-tipado por contexto.
 *
 * @field element      o tipo de elemento `T` do slice
 * @field initial_cap  a expressão de capacidade-semente `N` (ausente para o `[]T` cap=0)
 * @since 0.3.1 (#F3.4)
 */
pub type SliceType = struct { element: TypeExpr; initial_cap: TypeExpr | null }
```

```teko
/**
 * Cresce `x` anexando `v`, mutando `x` IN-PLACE via o `ref` exclusivo (F1) sobre o rep
 * `{ptr,len,cap}` de Model A (F3). Substitui o value-thread `x = push(x, v)`: como `x` carrega o
 * `cap` no PRÓPRIO objeto mutado in-place, NÃO há tabela global `tk_push_cache`, NÃO há cópia
 * defensiva, NÃO há colisão de slot. `len < cap` → escreve `ptr[len]`, `len++` (O(1)); `len == cap`
 * → cap-doubling in-place (pânico se `cap` estourar u64), então escreve. O borrow é exclusivo-
 * temporário (spine `is_unique_at` autoriza; nenhuma cópia viva durante a chamada), o que fecha o
 * furo de aliasing de cap-no-objeto (§8). Value-form `teko::list::push` PERMANECE copiando —
 * in-place é só via `ref`.
 *
 * @param x  o slice-alvo, referenciado mutável e exclusivamente (`ref`) — `is_unique_at` provado
 * @param v  o elemento a anexar, do `T` concreto do slice
 * @throws   pânico se a capacidade de backing estourar u64 (M.1 fail-loud)
 * @since 0.3.1 (#F3.3 — cap-in-object; corpo de list.tks:17 rewired para a primitiva in-place)
 */
pub fn grow<T>(ref x: []T, v: T)
```

O seam C que F3.3 adiciona ao `teko_rt.h` (C mantido — a assinatura que o codegen chama):

```c
/**
 * tk_slice_grow_inplace — the Model A (#F3) in-place append primitive: append `elem` to the slice
 * whose {ptr,len,cap} header lives at `*hdr`, mutating the header directly. len<cap writes ptr[len]
 * and bumps len (O(1)); len==cap reallocs to cap*2 (tk_panic on u64 overflow) then writes. NO global
 * push cache, NO defensive copy, NO slot collision — cap lives in the header. Sound ONLY under an
 * exclusive `ref` borrow (F1 is_unique_at proves no live copy). `esz` is sizeof(T); `region` is the
 * current allocation region (root/frame/phase). Distinct from tk_slice_push (the value form, kept).
 */
void tk_slice_grow_inplace(void *hdr, const void *elem, uint64_t esz, tk_region *region);
```

---

## 11. Arquivos tocados (roteiro do implementador)

- `src/runtime/teko_rt.h` — `tk_slice_byte`/`tk_char` ganham `cap` (F3.1); protótipo
  `tk_slice_grow_inplace` (F3.3). [seam mantido, exceção Teko-only]
- `src/runtime/teko_rt.c` — `tk_slice_grow_inplace` (F3.3); `tk_slice_push`/`with_cap` semeiam cap
  (F3.2); 57 refs de layout auditados. [seam mantido]
- `src/codegen/codegen.tks` — `cg_slice_typename:1773` emite 3 campos (F3.1); construtores/`{0}` e
  `with_cap` semeiam cap (F3.2/F3.4); `emit_index_assign:8635`/`emit_index_slot:8709` estendem o
  `x[len]` occupy (F3.5); `emit_array_lit:4741` / stack-alloc frame-local (F3.5).
- `src/parser/parse_type.tks:236` (`parse_slice`) + `src/parser/type.tks:6` (`SliceType`) —
  `initial_cap: Expr?` (F3.4).
- `src/checker/type.tks:86` (`Slice`) — inalterado no tipo; o `[N]T` propaga cap no lowering
  (F3.4); `src/checker/escape.tks` — prova de não-escape p/ stack-alloc (F3.5).
- `src/lir/lower.tks` — construção nativa do slice 3-campos (F3.1). [#112 aberto ⇒ gate C-path]
- `src/list/list.tks:17` — corpo de `grow` rewired para `tk_slice_grow_inplace` (F3.3).
- Fixtures §9 nos `_test.tkt` respectivos + probes e2e.

---

## 12. Report — o que segue para a delegação de implementação de F3

- **Sequência**: F3.1 (rep {ptr,len,cap}) → F3.2 (semear cap) → F3.3 (in-place, mata a cache) →
  F3.4 (`[N]T` reserve) → F3.5 (no-zero-fill/occupy/stack) → F3.6 (ritual). Pontos de ritual:
  F3.1 e F3.6.
- **Blast radius (F3, a rep)**: 1 typedef central (`cg_slice_typename`), ~68 sites de construção no
  codegen (a maioria `{0}` grátis), seam mantido de 97 refs em `teko_rt.c` + typedef em
  `teko_rt.h`, superfície nativa larga mas de LEITURA (603 refs em `lower.tks`). SEPARADO do blast
  de AL3 (~1383 push-sites).
- **Primitiva de AL3**: `tk_slice_grow_inplace` via `grow(ref x, v)` (`list.tks:17`, já existe como
  bridge write-through; F3.3 rewira o corpo). Mata a `tk_push_cache` por construção.
- **Primitiva de CK1'**: `[N]T` reserve (cap=N, sem zero-fill) + index-overwrite in-place
  `t[i]=v` (já O(1), `emit_index_slot`). Dá o open-addressing O(1) com a MESMA API NameIndex
  (`nidx_build`/`nidx_probe`); CK1.1–CK1.4 inalterados. CK1' precisa só de F3, não de F1.
- **F1/F2 pré-requisitos**: AMBOS já na árvore (`fix/retirement`). F1: `Borrow`
  (`parse_expr.tks:500`), `type_reaches_ref` (`borrow.tks`), native `lower_addr_of_place`, bridge
  `grow(ref x)` (`list.tks:17`). F2: deep-immutable `let` (`typer.tks:4262`). **F3 está
  DESBLOQUEADO — não é design-ahead.**
- **Gate**: fixpoint **gen2==gen3** byte-idêntico (F3 muda rep ⇒ gen1≠gen2 é a transição
  esperada, não regressão) + `teko test .` + pico RSS/tempo por fase. Interino C-path enquanto
  #112 (fixpoint nativo) está aberto.
- **Maior risco**: `cap`-no-objeto + semântica de valor = aliasing (cópia stale de um slice
  crescido). Fechado pela EXCLUSIVIDADE de F1 (`is_unique_at`): in-place SÓ via `ref`; value-form
  `push` continua copiando. Sem tensão de lei nova ⇒ nenhum HALT.
</content>
