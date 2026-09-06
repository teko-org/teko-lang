# Migração `tk_str_concat`/`tk_str_slice` → Teko sob o ratchet (D112) — análise + veredito

> Operacionaliza D112 (DECISION_LOG). String = array; concat na nossa codebase é acúmulo →
> alocação-única/eager-free/view, nunca copy-grow. Baseline do build seco: **1146,1 MB**
> (`f8547e09`). O ratchet D68 exige pico que só CAI/flat. Este doc fixa o desenho; os crumbs
> `0129`–`0131` executam.

## O que a reconstrução do C faz hoje (fatos)

- `tk_str_slice`/`_to`/`_from` (`teko_rt.c:711-732`) são **VIEW zero-copy**: retornam
  `{s.ptr+start, end-start}` — NENHUMA alocação. Eliminaram "the dominant allocation in the
  compiler (108M tiny copies / 762 MB)" via `name_last_segment`.
- `tk_str_concat` (`teko_rt.c:310`) faz **malloc** (heap libc, INVISÍVEL à linha `teko: memory:
  peak` da arena) e o resultado vaza (nunca liberado).
- **Interpolação `$"..."`** (`codegen.tks:emit_interp` ~4311) lowera para `tk_str_concat` ANINHADO
  N-1 vezes → N-1 buffers intermediários por interpolação (tráfego quadrático de bytes). 90 sítios
  só em `codegen.tks`, cada um rodando MUITAS vezes por-declaração no build seco.
- **`concat` variádico** (`codegen.tks:emit_concat_fold` ~3539) faz fold PAIRWISE em loop com
  `tk_str_concat`/`_r` → N-1 intermediários.
- A impl Teko atual em `teko_rt.tks`: `str_concat` (`:5`) aloca EXATO `[a.len+b.len]byte`
  (MAP-nature, correto); `concat` variádico (`:52`) faz UMA passada de tamanho exato (correto);
  `str_slice` (`:98`) **COPIA** `[end-start]byte` — ERRADO (reintroduz os 762 MB se roteado).
- Eager-free-na-reatribuição já existe PARCIAL para ARRAY: `escape.tks:assign_frees_old` +
  `codegen.tks:emit_assign` (7089) emitem `free_block(x.ptr, x.len*sizeof(*x.ptr))` antes de
  sobrescrever, guardado por `assign_is_self_append` (idioma `x=[..x,y]`/`list::push`), birth `[]`,
  sem escape. NÃO cobre `x = concat(x,…)`.
- `arena.tks:free_block` (`:736`) é null-safe (`addr==0`→no-op) e no-op para `n` sub-mínimo
  (`ar_free_block`: `usable < FREENODE_BYTES_MIN`→return). Logo liberar `""` (len 0) é seguro.

## Por que rotear str_concat pra Teko "dobrou" pra 2992 MB

Duas causas somadas, NENHUMA é o alocador:
1. **Shift de contabilidade** — os intermediários saíram do heap libc (invisível ao pico) pra
   arena (contada). Bytes reais que o C só escondia.
2. **Acúmulo** — os N-1 intermediários da interpolação/fold + os `out=concat(out,…)` de loop não
   são reclamados na arena bump. O `str_slice` COPIANDO ainda somaria os 762 MB.

## Veredito (o que segura/baixa o pico)

**O eager-free SOZINHO NÃO segura o pico.** A alavanca dominante é colapsar o fold pairwise da
interpolação/variádico em UMA alocação exata (remove N-1 intermediários), e manter o slice como
VIEW. Três frentes, todas sancionadas por D112 (alocação-exata + eager-free + saída direta):

- **A (STR-CC1) — `str_slice` family = view zero-copy inline.** Migra `teko::str::slice`/`_to`/
  `_from` do símbolo C `tk_str_slice` para **emissão inline** `({ tk_str _s=…; …bounds…; (tk_str){
  _s.ptr+start, end-start}; })` (padrão `emit_str_from_c`). Byte-COMPORTAMENTO idêntico ao view C
  de hoje → **pico flat**; remove a dependência do símbolo C (habilita F9). NUNCA rotear pro
  `str_slice` copiante do `teko_rt.tks`. Sub-slice = view É a semântica de array (`ref []T` =
  ponteiro-de-posição, CLAUDE.md).

- **B (STR-CC2) — interpolação + `concat` variádico + `str_concat` binário → UMA alocação Teko.**
  `emit_interp`/`emit_concat_fold` reescritos para emitir UMA chamada
  `teko_teko__runtime__concat((tk_str[]){pieces…}, n)` (fn variádica `teko_rt.tks:52`, passada
  única de tamanho exato) em vez do aninhamento/loop pairwise. `str_concat` binário (3809) →
  símbolo Teko. Remove N-1 intermediários por interpolação (linear, 1 buffer, reclamado no pop de
  escopo — a saída do codegen JÁ streama, então o buffer do concat morre no escopo). **Esta é a
  alavanca de memória.**

- **C (STR-CC3) — eager-free de reatribuição self-consuming de string.** Estende
  `assign_frees_old` para `x = concat(x,…)`/`x = str_concat(x,…)` (str-typed, arg0==x): alocação
  FRESH → libera o backing antigo. **EXCLUI `x = slice(x,…)`** (view no backing antigo → liberar =
  UAF). Reclama os `out=concat(out,…)` quentes. Semântica de array completa (D112).

**Nenhum sítio precisa virar `FileStream`-stream além do que já streama** (a saída do codegen em
`cg_emit_c_file_mode` já é append-only ≤1024 B, fato #1 do recon). O mandato "stream, não concat"
de D112 é satisfeito por (a) saída do codegen já streamada + (b) colapso do fold em alocação-única
(sem acumulador). Os `out=concat(out,…)` de `minst*.tks`/`time.tks`/`project.tks`-cov são
DEBUG/native/test (frios no build seco, pós-F9) — NÃO converter agora (lei "não gastar em
lir_print/pontual O(1)").

## Segurança do eager-free de string (condição EXATA)

Reusa a prova do array (contagem de reads). Libera o backing antigo de `x` na reatribuição `x =
RHS` SÓ se TODAS valem:
1. `RHS` é alocação **FRESH** self-consuming: `concat`/`str_concat`/`teko::str::concat` com
   arg0==`x`. (VIEW — `slice(x,…)` — NUNCA entra: aliasaria o backing liberado.)
2. `x` NÃO escapa o corpo (`name_escapes_body`).
3. Birth único e VAZIO: a única criação de `x` é `x = ""` (empty-str) → o 1º free é no-op (len 0,
   `free_block` sub-mínimo). Birth não-vazio (`x = "PREFIX"` ou `x = f()`) → `births_other` →
   rejeita (conservador, backing pode não ser da arena).
4. `other_writes == 0` e a contagem de reads casa (tomar um view/qualquer read extra de `x`
   desqualifica — protege contra view-vivo do backing liberado).
5. Ordem de emissão (já garantida por `emit_assign`): materializa `_tkna = RHS` (copia bytes de
   `x`), DEPOIS libera o backing antigo, DEPOIS `x = _tkna`. O RHS lê `x` antes do free → seguro.

Backing liberado é sempre da arena (concat aloca na região corrente) pós-B; o 1º free (`""`) é
no-op. Correto só QUANDO concat já é Teko/arena → **C depende de B** (liberar um ptr malloc'd do
`tk_str_concat` C via freelist da arena = corrupção).

## Ordem, reseed, medição

1. **0129 STR-CC1** (slice view) — codegen só. `[fixpoint]` gen2==gen3, pico flat (≤1146). Reseed.
2. **0130 STR-CC2** (concat single-alloc) — codegen. `[fixpoint]`, **MEDIR pico ≤1146** (gate
   crítico). Reseed.
3. **0131 STR-CC3** (eager-free str) — checker+codegen. Depende de 0130. `[fixpoint]`, **MEDIR pico
   ≤ o de 0130**. Reseed.

**Acoplamento ratchet:** se 0130 medir REGRESSÃO por acumuladores `x=concat(x,…)` quentes não
reclamados, 0130 e 0131 DRENAM JUNTOS (um reseed) com o gate de pico na dupla — nunca landar 0130
regredido sozinho (D68: flat/queda por-landing). Cada crumb reseeda incondicionalmente (lei do
dono). Método D90: zero edição em `teko_rt.c` — os corpos C de `tk_str_concat`/`tk_str_slice` ficam
mortos até F9 deletar.
