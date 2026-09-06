---
section: design
created: 2026-09-03
status: DESIGN — plano executável (uma unidade p/ um implementer Opus) do redesign dos IR-BUILDERS
        append-one (o hog dominante do pico NATIVE). Escopo: substituir o crescimento copy-grow
        O(n²) das listas de IR (LIR + máquina) por um container CHUNKED que nunca recopia elemento.
        Habilitador: fix 11a (classe genérica recursiva) LANDADO e PROVADO no tip (ver §0).
        Governa: D207 (chunked, sem geométrico), D208 (streaming≠container — container p/ IR residente),
        D210 (métrica = pico native no CI; ratchet C-seco pode subir modestamente), D148 (zero C novo).
---

# IR-builders → container CHUNKED (0.3.1)

## 0. PASSO 0 + prova do habilitador

- **Base:** `arch-irbuilder` de `origin/fix/retirement`, HEAD `92cdf56` (2026-09-03).
  Canário `grep "BindKind = enum" src/parser/ast.tks` → `pub type BindKind = enum { Var; Const }`. OK.
- **Habilitador (fix 11a — classe genérica recursiva):** PROVADO no tip. Um gen0 FRESCO linkado do
  `bootstrap/teko.c` commitado (`scripts/build_gen1_from_c.sh`) compilou fim-a-fim (checker ✓ / monomorph
  `SegNode__g__i32` ✓ / codegen ✓ / cc ✓) e RODOU um probe com
  `type SegNode<T> = class { intern data: [4]T; intern used: u32; intern next: SegNode<T>|null; ... }`
  + método recursivo `count()` → **exit 7** (3+4) correto. Logo `Chunked<T>` com nó `SegChunk<T>` de
  campo `next: SegChunk<T>|null` COMPILA hoje. (O `.work/gen0/teko` pré-existente está STALE vs o tip —
  não usar; sempre gen0-do-seed-commitado.)

## 1. O diagnóstico (auditado, arquivo:linha)

Duas FAMÍLIAS de builders append-one, ambas **crescimento dinâmico de contagem irredutivelmente
desconhecida** (não se sabe quantas insts/blocks/funcs até terminar de lowerar/selecionar) → NÃO é
4-naturezas mecânica. Cada append reconstrói o array inteiro por `[..xs, x]` = O(n²) escala-programa; com
reclaim-0% cada versão intermediária VAZA.

### 1a. Lado LIR (dirigido pelo lowering) — `src/lir/lir.tks`
- `push_inst_block` (lir.tks:206-208): `insts = [..b.insts, inst]` — **O(n²) por instrução no bloco.**
- `append_inst` (lir.tks:210-222): acha o bloco e o SUBSTITUI inteiro (`blocks[i] = push_inst_block(...)`);
  reconstrói `LFunc` inteiro no fim. Chamado por `ctx_append` (lower.tks:552) — o caminho quente.
- `alloc_block` (lir.tks:260-268): `blocks = [..f.blocks, b]` — copy-grow de `LFunc.blocks`.
- `set_block_param` (lir.tks:277-289): `params = [..blocks[i].params, vreg]` + rebuild.
- `add_func` (lir.tks:234-236): `funcs = [..m.funcs, f]` — copy-grow de `LModule.funcs` (usado em
  lower.tks:6631, 6720, 7052).

### 1b. Lado MÁQUINA (dirigido pelo isel) — `src/backend/minst.tks` + `minst_x86.tks`
- `append_minst` (minst.tks:417 / `append_minst_x86` minst_x86.tks:~334): `insts = [..b.insts, inst]`.
  Chamado por `selctx_emit` (isel_arm64.tks:66-67 / isel_x86_64 gêmeo) — **uma machine-inst por padrão
  selecionado, contagem desconhecida** → O(n²).
- `append_block` (minst.tks:431 / gêmeo x86): `blocks = [..f.blocks, blk]`. Chamado por `selctx_new_block`
  (isel_arm64.tks:82).
- `add_func` (minst.tks:465 / minst_x86.tks:385): `funcs = [..m.funcs, f]` no `select_module`.

> NOTA de escopo: os `add_func`/`add_block` do minst como fn NOMEADA não têm caller (o isel usa
> `append_minst`/`append_block`/`select_module`). O caminho quente do lado-máquina é
> `append_minst`/`append_block`. As MIRROR-blocks (`build_mirror_blocks` isel_arm64.tks:923) já são
> prealloc-MAP `[f.blocks.len]MBlock` (correto) — mas os INSTS dentro delas crescem por `append_minst`
> (hog). Logo o container aplica-se a `MBlock.insts` e `MFunc.blocks`, NÃO às mirror-blocks.

### 1c. Fora de escopo (já corretos — NÃO tocar como builders)
- `regalloc*`/`encode*`/`isel` READ-side: `[f.blocks.len]MBlock`, `[b.insts.len][]@MInst()`,
  `[m.funcs.len]MFunc` = prealloc-MAP (4-naturezas, O(n)). São CONSUMIDORES (precisam adaptar
  `.len`→`.len()` / `[i]`→`.get(i)` — ver §4), mas a construção deles não é hog.
- `LEnv` (lower.tks:5, 7 arrays paralelos): copy-grow por-BIND — **secundário** (bindings << instruções)
  e com semântica save/restore delicada (`lenv_prefix`, lower.tks:756). **DEFERIDO** ao último degrau,
  com desenho próprio (§6).

## 2. O container: `Chunked<T>`

### 2.1 Escolha da estrutura — justificada pelo padrão de acesso REAL

Auditoria dos leitores de `.insts`/`.blocks`/`.funcs` (grep tree-wide, §4):
- **100% dos acessos indexados são SEQUENCIAIS** — `loop { if i >= xs.len { break } ...xs[i]...; i++ }`
  (encode_*, regalloc_*, isel_*, lir_print, frame_escape, minst print).
- Não-sequenciais: só `xs[0]` (entry block), `xs[len-1]` (último inst — lower.tks:6546), e buscas
  lineares-forward por id (`if xs[i].id == id`). **ZERO acesso aleatório quente.**
- Prealloc-MAP `[xs.len]T` que depois indexa `xs[i]` num loop 0..len (sequencial).

**Veredito:** como não há random-index quente, a estrutura correta é a **cadeia ligada de chunks
(linked SegChunk) com iterador forward** — zero-recopy PURO (D207 estrito), sem a complexidade/desperdício
de um diretório, e **byte-idêntica** (a ordem de iteração = ordem de append). O diretório-de-ponteiros
(sugestão "salta por índice, math") fica DOCUMENTADO como alternativa (§2.4) para o caso de surgir um
consumidor random-index quente — hoje NENHUM existe, então não se paga o custo dele.

### 2.2 Estrutura (Teko, full-Javadoc — copiar verbatim para `src/collections/chunked.tks`)

```
use teko::collections

/**
 * A fixed leaf node of a `Chunked<T>`: up to `CHUNK` elements written once and never moved,
 * linked in append order.
 *
 * @see Chunked
 */
type SegChunk<T> = class {
    intern data: [teko::collections::CHUNK]T
    intern used: u32
    intern next: SegChunk<T>|null

    /**
     * Builds an empty leaf with no successor.
     *
     * @return a fresh chunk with `used == 0` and `next == null`
     */
    pub static fn make(): SegChunk<T> { .{ data = []; used = 0; next = null } }
}

/**
 * A grow-without-recopy sequence of `T` for the IR builders: appends land in fixed leaf chunks
 * (`SegChunk`) that are never reallocated or moved, so building `n` elements costs O(n) writes and
 * ZERO element recopy (unlike `[..xs, x]` copy-grow). Reference-semantic: `push` mutates the receiver.
 *
 * Access is O(1) append (`push`), O(1) `len`/`first`/`last`, O(1)-amortized forward iteration
 * (`iter`), and O(n/CHUNK) random `get` (rare — see the module audit). NEVER geometric/capacity-
 * doubling (D207): the chain grows by one fixed chunk when the tail fills.
 *
 * @see SegChunk
 * @see ChunkedIter
 */
exp type Chunked<T> = class {
    intern head: SegChunk<T>|null
    intern tail: SegChunk<T>|null
    intern count: u64

    /**
     * Builds an empty `Chunked<T>`.
     *
     * @return a sequence with `len() == 0`
     */
    pub static fn make(): Chunked<T> { .{ head = null; tail = null; count = 0 } }

    /**
     * The number of elements appended so far.
     *
     * @return the element count
     */
    pub fn len(): u64 { self.count }

    /**
     * True iff `len() == 0`.
     *
     * @return whether the sequence is empty
     */
    pub fn is_empty(): bool { self.count == 0 }

    /**
     * Appends `x` after the last element, in place; opens a fresh tail chunk when the current one is
     * full. The element is written once and never moved.
     *
     * @param x the value to append
     */
    pub fn push(x: T) {
        match self.tail {
            teko::collections::SegChunk<T> as t => {
                if t.used < teko::collections::CHUNK to u32 { chunk_write(t, x); self.count = self.count + 1; return }
                var nx = SegChunk<T>::make()
                chunk_write(nx, x)
                chunk_link(t, nx)
                self.tail = nx
                self.count = self.count + 1
            }
            null => {
                var first = SegChunk<T>::make()
                chunk_write(first, x)
                self.head = first
                self.tail = first
                self.count = 1
            }
        }
    }

    /**
     * Reads the first element; the sequence must be non-empty.
     *
     * @return element 0
     * @throws panic when `is_empty()`
     */
    pub fn first(): T { chunk_at(self.head, 0) }

    /**
     * Reads the last element; the sequence must be non-empty.
     *
     * @return element `len()-1`
     * @throws panic when `is_empty()`
     */
    pub fn last(): T { chunk_last(self.tail) }

    /**
     * Reads the element at `i`; walks the chunk chain (O(i/CHUNK)). Prefer `iter` for sequential scans.
     *
     * @param i a 0-based index in `[0, len())`
     * @return the element at `i`
     * @throws panic when `i >= len()`
     */
    pub fn get(i: u64): T { chunk_at(self.head, i) }

    /**
     * A forward cursor over the elements in append order.
     *
     * @return a fresh iterator positioned before element 0
     */
    pub fn iter(): ChunkedIter<T> { ChunkedIter<T>::over(self.head) }
}
```

`ChunkedIter<T>` (cursor forward, o caminho quente de leitura):

```
/**
 * A single-pass forward cursor over a `Chunked<T>`; `next` yields elements in append order until the
 * chain is exhausted.
 *
 * @see Chunked
 */
exp type ChunkedIter<T> = class {
    intern node: SegChunk<T>|null
    intern pos: u32

    /**
     * Builds a cursor positioned before the first element of the chain rooted at `head`.
     *
     * @param head the first chunk, or `null` for an empty sequence
     * @return a fresh cursor
     */
    pub static fn over(head: SegChunk<T>|null): ChunkedIter<T> { .{ node = head; pos = 0 } }

    /**
     * Yields the next element, or `null` when the sequence is exhausted; advances the cursor.
     *
     * @return the next element wrapped, or `null` at the end
     */
    pub fn next(): T|null {
        match self.node {
            teko::collections::SegChunk<T> as n => {
                if self.pos >= n.used { self.node = n.next; self.pos = 0; return self.next() }
                var v = chunk_read(n, self.pos)
                self.pos = self.pos + 1
                v
            }
            null => null
        }
    }
}
```

Helpers `intern` (leem/escrevem a base `[CHUNK]T` — o dev não os toca; unchecked/panic por serem
não-`exp`, lei CHECKED/UNCHECKED): `chunk_write(c, x)` grava `c.data[c.used]=x; c.used++`; `chunk_read(c,
p)`→`c.data[p]`; `chunk_at(head, i)` caminha `i/CHUNK` nós + `data[i%CHUNK]`; `chunk_last(tail)`→
`tail.data[tail.used-1]`; `chunk_link(a, b)`→`a.next=b`. Doc-comment SÓ nos `exp` (§ estilo); os
`intern` NÃO ganham doc.

### 2.3 `CHUNK`

`exp const CHUNK: u64 = 256` em `src/collections/collections.tks` (tunável). 256 balanceia nº-de-chunks
vs. desperdício da cauda (um bloco típico tem < 256 insts = 1 chunk; funções grandes encadeiam). **O
valor ótimo mede-se pelo pico NATIVE no CI (D210) — inmensurável no sandbox**; começar em 256, ajustar
por medição de CI se preciso. NÃO geométrico (D207): a cauda cresce +1 chunk fixo, nunca dobra.

### 2.4 Alternativa documentada — diretório paginado (só se surgir random-index quente)

Se um consumidor futuro exigir `get(i)` O(1) em loop quente: adicionar ao `Chunked<T>` um diretório
`dir` de ponteiros-de-chunk, ele mesmo uma cadeia de `DirChunk { slots: [DIRFAN]SegChunk<T>; next:
DirChunk|null }` (nunca recopia — D207). Index `i`: `c = i/CHUNK; dn = c/DIRFAN; ds = c%DIRFAN` → caminha
`dn` DirChunks (cada um cobre `DIRFAN*CHUNK` elementos) → `slots[ds].data[i%CHUNK]`. Com DIRFAN=CHUNK=256,
um DirChunk cobre 65 536 elementos → 2 níveis cobrem 16M. **NÃO adotar agora** (a auditoria não achou
random-index quente); registrado para não se perder.

## 3. Semântica de mutação in-place (o coração do ganho) + risco #1

Hoje os builders são funcionais (recebem valor, devolvem novo valor-struct). Com `Chunked<T>`
reference-semantic, `append_inst` deixa de reconstruir o `LFunc`/`LBlock`: **acha o bloco e chama
`block.insts.push(inst)` in-place** — ZERO rebuild da cadeia de blocos e ZERO rebuild da lista de insts.
Isso mata as DUAS fontes: o O(n²) do `[..insts, x]` E o rebuild-do-func-por-inst.

**RISCO #1 (aliasing) — o único risco de correção real.** A fiação funcional do lowering assume que cada
`LFunc` devolvido é independente. Com `insts`/`blocks` reference-semantic, "novo LFunc com o mesmo
`Chunked`" == mutação in-place. Isso é CORRETO **desde que o `LFunc` antigo não seja reusado após a
reatribuição** (lei purge-na-reatribuição — o `ctx.func` antigo morre no ato). O lowering encaminha
exatamente UM func vivo por vez, então é seguro — MAS o implementer DEVE auditar pontos de FORK antes de
declarar pronto:
1. Algum sítio COPIA um `LFunc`/`LBlock`/`LModule` e muta as DUAS cópias? (grep por `LFunc {`/`LBlock {`
   fora dos builders + `alloc_block`-then-discard especulativo).
2. `alloc_block` devolve um `BlockRef { func = widened }` — o caller usa o func VELHO depois? (deve
   descartar).
3. Lowering especulativo (tenta lowerar, descarta se falha) que reuse um func pré-mutação?

**A GATE PROTEGE:** o fixpoint gen2.c==gen3.c byte-idêntico DIVERGE imediatamente se um aliasing corromper
a saída. Logo qualquer bug de aliasing é PEGO pelo gate, não escapa. (Determinismo: a ordem de append é
idêntica → a saída C/`.o` é byte-idêntica.)

> Se a auditoria achar um fork genuíno, a mitigação é `Chunked.clone()` (cópia rasa da cadeia — O(chunks),
> não O(n²)) NAQUELE sítio, não reverter o design.

## 4. Censo dos consumidores (arquivo:linha — a troca toca TODOS)

Ao mudar o TIPO do campo (`[]LInst` → `Chunked<LInst>` etc.), todo leitor muda `xs.len`→`xs.len()`,
`xs[i]`→iterador OU `xs.get(i)`, `xs[0]`→`xs.first()`, `xs[len-1]`→`xs.last()`. Preferir ITERADOR nos
loops 0..len quentes (O(1) amortizado); `get(i)` só onde o índice é usado fora-de-ordem.

### 4a. `LBlock.insts` (Degrau 1)
- `src/lir/lir.tks`: push_inst_block:207, append_inst:214-217, lir_print via.
- `src/lir/lower.tks`: 6545-6546 (`b.insts.len`, `b.insts[len-1]`).
- `src/lir/frame_escape.tks`: 53-54, 87-88 (scan sequencial).
- `src/lir/lir_print.tks`: 219-220.
- `src/backend/isel_arm64.tks`: 536, 552, 580, 952 (`lb.insts` passado a `select_insts`); assinatura
  `select_insts(..., insts: []lir::LInst)`:899 — mudar para `Chunked<LInst>` OU passar `lb.insts` (o
  campo já é Chunked) e iterar dentro.
- `src/backend/isel_x86_64.tks`: 856 + `select_insts_x86(..., insts: []lir::LInst)`:803.
- `src/backend/regalloc.tks`: 34 (retorna `f.blocks[i].insts`), 1120 (`[b.insts.len][]@MInst()`),
  1124-1125, 1168-1169.
- `src/backend/regalloc_x86.tks`: 64, 461, 465-466, 509-510.
- `src/backend/encode_arm64.tks`: 523-524, 570-571, 580-581, 629-630, 1109-1110.
- `src/backend/encode_x86_64.tks`: 510-511, 557-558, 579-580, 621-622, 882-883.

### 4b. `LFunc.blocks` (Degrau 2)
- `src/lir/lir.tks`: new_func:105-111, append_inst:211-221, alloc_block:261-266, add_block_param/
  set_block_param:278-288, alloc_vreg/with_next_vreg (campo blocks propagado).
- `src/lir/lower.tks`: 6536-6537.
- `src/lir/frame_escape.tks`: 43-44, 76-77, 120-121.
- `src/lir/lir_print.tks`: 242-243.
- `src/backend/isel_arm64.tks`: 535, 551, 579, 601-602, 923-927 (`build_mirror_blocks` prealloc-MAP),
  938-939, 949-950.
- `src/backend/isel_x86_64.tks`: 827-843, 853-854.
- `src/backend/regalloc.tks`: 33-34, 72, 81-82, 93-94, 100-101, 1151-1155, 1164-1165.
- `src/backend/regalloc_x86.tks`: 63-64, 97, 106-107, 118-119, 125-126, 492-496, 505-506.
- `src/backend/encode_arm64.tks`: 534-535, 590-591, 600-601, 641-642, 1151-1155.
- `src/backend/encode_x86_64.tks`: 521-522, 568-569, 590-591, 617-618, 918-919.
- `src/backend/dwarf.tks`: 577 (`sub.blocks` — VERIFICAR: é `MFunc.blocks`? adaptar).

### 4c. `LModule.funcs` (Degrau 3)
- `src/lir/lir.tks`: empty_module:101, add_func:234-235, add_rodata/add_global/add_layout/with_rodata
  (campo funcs propagado).
- `src/lir/lower.tks`: 6631, 6720, 7052 (`add_func`), 7122 (`native_entry_rename_vmain(m.funcs)` →
  assinatura recebe `[]LFunc`; adaptar p/ `Chunked` ou materializar).
- `src/lir/frame_escape.tks`: 19-20 (`m.funcs`, `found=[..found, sym]` — nota: `found` é copy-grow
  SEPARADO, pequeno; converter p/ prealloc-FILTRO `[m.funcs.len()]str`+count OU deixar — decidir no
  degrau; ver R5).
- `src/lir/lir_print.tks`: 320-322.
- `src/backend/isel_arm64.tks`: 969-970 (`select_module`), encode_arm64:1404 (`encode_functions(abi,
  m.funcs)` → `funcs: []MFunc`).
- `src/backend/isel_x86_64.tks`: 873-874, encode_x86_64:1031.
- `src/backend/regalloc.tks`: 1199-1203; regalloc_x86:540-544.
- `src/build/project.tks`: 1697-1698, 1837 (`concat_lfuncs(a: []LFunc, b: []LFunc)`), 1866, 1876, 2210,
  2221, 2303, 2314, 2408-2413 (`[lmod.funcs.len]LFunc` FILTRO — kept). **project.tks é ponto-cego
  recorrente (D197) — incluir sempre.**

### 4d. Máquina x86 (Degrau 4)
- `src/backend/minst_x86.tks`: MBlockX86:189, MFuncX86:195, MModuleX86:201, append_minst_x86:334,
  set param:376, append_block:352, add_func:385, print 639-640, 650-651, 661-663.
- `src/backend/isel_x86_64.tks`: selctx_emit/selctx_new_block gêmeos.
- `src/backend/regalloc_x86.tks`, `encode_x86_64.tks`: já listados em 4a/4b (compartilham).

### 4e. Máquina arm64 (Degrau 5)
- `src/backend/minst.tks`: MBlock:243, MFunc:249, MModule:255, append_minst:417, append_block:431,
  add_func:465, set param:456, print 749-750, 760-761, 771-773.
- `src/backend/isel_arm64.tks`: selctx_emit:66-67, selctx_new_block:82.
- `regalloc.tks`, `encode_arm64.tks`: já listados.

### 4f. `tooling/` + raiz `main.tks` (varredura obrigatória D197)
- grep `\.insts\b|\.blocks\b|\.funcs\b` em `tooling/**`, `main.tks`, `cases/**`, `examples/**` — esperado
  ZERO (IR é interno ao compilador), mas CONFIRMAR zero antes de declarar pronto (o self-build não
  exercita esses).

## 5. Ordem de conversão — degraus verdes por fixpoint-C

Cada degrau: converte 1 estrutura + TODOS os leitores dela; builda gen0-do-seed; **fixpoint gen2.c==
gen3.c byte-idêntico**; grep no-geométrico (`\[\.\.` zero nos builders convertidos); reseed; commit+push.
NÃO avança sem verde. O container é folha → adicioná-lo NÃO exige staging de bootstrap (o 11a já está no
seed, provado §0); a MUDANÇA de tipo de campo é mudança-de-compilador normal → reseed por degrau.

- **Degrau 0 — landar o container.** `src/collections/chunked.tks` (`Chunked`/`SegChunk`/`ChunkedIter`)
  + `CHUNK` em collections.tks. Ainda SEM consumidor no compilador. Fixpoint (só compila o módulo novo,
  não muda emissão). Oráculo `.tkr` isolado permitido AQUI (path que o self-build ainda não exercita):
  UM `chunked_grow.tkr` — push N>2*CHUNK, valida `len()`/`first()`/`last()`/`get(k)`/iteração-completa
  em ordem. É o ÚNICO fixture novo autorizado; some quando os degraus 1-5 passarem a exercitar o
  container (self-build o exercita então). Nomeado aqui = cumpre a lei "só oráculos nomeados no crumb".
- **Degrau 1 — `LBlock.insts: Chunked<LInst>`.** O hog #1 (uma inst por statement lowerado). Reescreve
  push_inst_block/append_inst in-place (§3) + leitores 4a. Fixpoint.
- **Degrau 2 — `LFunc.blocks: Chunked<LBlock>`.** alloc_block/set_block_param in-place + leitores 4b.
  Fixpoint.
- **Degrau 3 — `LModule.funcs: Chunked<LFunc>`.** add_func in-place + leitores 4c (incl. project.tks).
  Fixpoint.
- **Degrau 4 — máquina x86** (`MBlockX86.insts`/`MFuncX86.blocks`/`MModuleX86.funcs`): append_minst_x86/
  append_block/select_module + leitores 4d. Fixpoint.
- **Degrau 5 — máquina arm64** (gêmeo): append_minst/append_block/select_module + leitores 4e. Fixpoint.
- **Degrau 6 (ÚLTIMO, condicional) — `LEnv`.** Ver §6. Só depois de 1-5 verdes e medido no CI.

Rationale da ordem: LIR antes de máquina (o LIR é a entrada da máquina; estabilizar a fonte primeiro).
x86 antes de arm64 é arbitrário (gêmeos independentes) — ou paralelizável em duas sub-branches se o
implementer preferir, drenadas em sequência. Cada degrau é bisectável e reverte sozinho.

## 6. LEnv — veredito e desenho (Degrau 6, deferido)

**Fica como 7 arrays paralelos ATÉ 1-5 fecharem; então reavaliar com medição de CI.** Motivos do defer:
1. **Secundário:** copy-grow por-BIND (bindings por função << instruções) — contribuição O(bindings²),
   ordens de magnitude menor que insts.
2. **Save/restore delicado:** `lenv_prefix(env, n)` (lower.tks:15,756) faz SNAPSHOT (copia os primeiros n
   de cada array em `[n]T` fresco) para RESTAURAR o env do pai ao sair de escopo. Um `Chunked<LBinding>`
   reference-semantic com `truncate(n)` in-place CORROMPERIA o env do pai (compartilhado). A conversão
   exige repensar o scoping (mark/restore snapshot-seguro), não é mecânica.

**Desenho quando for a hora:** `LEnv` vira `intern binds: Chunked<LBinding>` com
`LBinding = struct { name: str; vreg: u32; len_vreg: u32; has_len: bool; is_slot: bool; is_scalar_slot:
bool; slot_ltype: LType }` (funde os 7 arrays num struct — mata os 7 `[..arr,x]` por bind). Bind =
`push` O(1). `lenv_newest_index` = varredura reversa (iterar chunks de trás p/ frente — adicionar
`iter_rev`/índice reverso ao container). `lenv_reassign` = `get`/`set(idx)`. O **prefix/restore** NÃO
pode ser truncate in-place: manter SNAPSHOT (clone raso da cadeia até o mark = O(chunks), melhor que o
O(n)×7 de hoje) OU tornar `LEnv` estrutura persistente por cauda-imutável. Decidir com o número do CI na
mão — se LEnv não mover o pico native, NÃO converter (custo/risco > ganho).

## 7. Gate (D210) + metodologia resiliente

- **Correção (local):** fixpoint rota-C **gen2.c==gen3.c byte-idêntico** (behavior-preserving) +
  no-geométrico (grep `\[\.\.` = zero nos builders convertidos) + zero acumulador-de-loop remanescente
  na estrutura convertida + estilo (doc só em `exp`, zero `//`). Todo build local sob `ulimit -v
  4718592`; gen0 SEMPRE do `bootstrap/teko.c` commitado.
- **Memória:** o **pico NATIVE valida-se no CI (PR #110)** — o sandbox NÃO mede (base >8 GB; medir
  8-15 GB TRAVA a máquina; native sem cap = crash). NÃO rodar o build native localmente sem cap. NÃO
  ASan (N/A ao native — emite `.o` direto). O **ratchet do build-seco-C NÃO é FAIL** para este trabalho
  de caminho-native (D210): a verbosidade NO-PUSHES/Chunked pode subir o C-seco modestamente — aceito.
- **Verificador independente (coordenador, D164/D166):** gen0-do-seed-commitado builda o tip + gen2==gen3
  confirmado por um 2º agente ANTES de drenar cada degrau — nunca na palavra do implementer.
- **Harnesses C standalone (D185):** este trabalho NÃO toca `teko_rt.{c,h}`/`assert.*` → os 3
  `scripts/*_test.sh` não são afetados; ainda assim o verificador confirma que seguem verdes.
- **Resiliência (tarefa longa):** commit+PUSH por DEGRAU numa branch real no origin (o worktree some no
  restart). O container reinicia limpo; cada degrau drena por ff/cherry-pick. Deixar gen2/gen3 no
  scratchpad da worktree (evita rebuild à toa no dreno). Reseed INCONDICIONAL por degrau (prova do
  fixpoint), mesmo que o número-alvo só apareça no CI.

## 8. Riscos + tensões de lei

- **R1 (aliasing, §3):** ÚNICO risco de correção. Mitigação: auditoria de fork + o gate byte-idêntico
  pega qualquer regressão. Fallback local `Chunked.clone()` no sítio, não reverter design.
- **R2 (tensão D208 "container MOOT"):** RESOLVIDA, não é fork. D208 declara o container moot **para as
  SEÇÕES do `.o` na fase de EMIT** (rodata/bytes/símbolos → gravam a disco por streaming, D208). Os
  IR-builders (insts/blocks/funcs) NÃO são saída streamável a disco: são o IR RESIDENTE que o backend
  relê em múltiplos passes (isel→regalloc→encode). Não dá pra streamar a disco e rodar regalloc. Logo o
  container permanece o caminho certo PARA OS IR-BUILDERS; o dispatch do dono (2026-09-03, mais recente)
  re-escopa exatamente isso. Sem tensão pendente → sem HALT.
- **R3 (D207 estrito "nunca recopia"):** honrado — a cadeia linked-chunk nunca recopia elemento nem
  ponteiro; cresce +1 chunk fixo (sem geométrico). O diretório (§2.4) fica de fora justamente por não
  ser necessário e para não introduzir recopy de ponteiros.
- **R4 (`select_insts`/`encode_functions`/`concat_lfuncs` recebem `[]T`):** ao mudar o campo, ou muda-se
  a assinatura para `Chunked<T>`/iterar dentro, OU materializa-se via `to_array()` no call-site. Preferir
  mudar a assinatura (materializar reintroduz uma cópia O(n) que, com reclaim-0%, vaza — D197: não
  regredir view→cópia). `concat_lfuncs` (project.tks:1837) vira concat de dois `Chunked` (push do 2º no
  1º, ou um novo que encadeia) — NÃO `[..a, ..b]`.
- **R5 (`found`/`kept` copy-grow adjacentes, frame_escape:20 / project.tks:2408):** achados adjacentes
  pequenos no mesmo arquivo. `kept` já é prealloc-FILTRO (ok). `found=[..found,sym]` é copy-grow pequeno —
  converter no MESMO degrau (prealloc-FILTRO `[m.funcs.len()]str`+count) por higiene, NÃO abrir issue
  (reportado aqui, resolve-se in-degrau).

## 9. O que permanece BLOQUEADO

Nada. O habilitador (11a) está provado no tip (§0); todas as estruturas e consumidores estão auditados
contra o `src/` atual; o container compila hoje. O implementer pode começar no Degrau 0 imediatamente.
