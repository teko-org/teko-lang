# Passada 1 — Correção do copy-grow (emit native) + completar lowering N2 + expurgo unsafe/manual

Documento de design (ARQUITETO). UMA passada coerente, ataque único por um implementer.
NÃO é ladder. Streaming-em-disco = Passada 2 (outro agente) e NÃO é desenhado aqui.

## PASSO 0 — base provada

- `git rev-parse HEAD` = `b3027418b425f580bb05fd1fd5bc43b56ce8a984` (≥ `b3027418`).
- `git log -1 --format=%ci HEAD` = `2026-09-02 19:19:06 +0000` (setembro, NÃO julho).
- Canário `grep "BindKind = enum" src/parser/ast.tks` → `pub type BindKind = enum { Var; Const }` (sem `Mut`/`Let`).

Base correta. Todas as citações de linha abaixo são contra este HEAD.

---

## Eixo A — CORREÇÃO DO COPY-GROW do emit native (mata o OOM de 13,5 GB)

### Diagnóstico exato (confirmado lendo o código)

O OOM é O(n²) no ASSEMBLY item-a-item, com reclaim 0% (cada cópia intermediária do
acumulador VAZA na região que só larga no fim). Três acumuladores no laço principal de
`emit_native_x86` (`src/build/project.tks:1922`), espelhados em `emit_native_arm64_fused`
(`:2054`) e `emit_native_win` (`:2145`):

1. **Texto/símbolos/relocs do módulo** — `fold_encoded_funcs_x86` (`:1751`) chama
   `teko::backend::fold_encoded_func_x86` (`encode_x86_64.tks:957`) por função. Este faz:
   - `text = append_bytes(acc.text, ef.bytes)` — recopia o texto acumulado INTEIRO a cada
     função (o `mt.text` cresce até o binário completo, ~dezenas de MB) → O(texto_total × nº_funcs).
   - `relocs = rebase_relocs_x86(acc.relocs, …)` (`:940`) — recopia `acc.relocs` inteiro por função.
   - `syms = [..acc.syms, sym]` — recopia `acc.syms` inteiro por função.
   Este é o dominante (o "22 MB × milhares de funcs" = os 13,5 GB vazados).

2. **rodata** — `commit_rodata_delta(robase, grown)` (`:1785`): `out = robase; loop { out = [..out, copy_lrodata(grown[i])] }` — recopia `robase` inteiro a cada item → O(rodata_total × nº_items).

3. **loose** — `commit_loose(acc, delta)` (`:1796`): `out = acc; loop { out = [..out, delta[i]] }` — recopia `acc` inteiro a cada item → O(loose_total × nº_items).

Acessórios (não-quadráticos no total, mas copy-grow por-item a limpar no mesmo passo):
- `lifted_ph` via `extend_lifted_placeholders` (`:1840`) — cresce um array de placeholders
  VAZIOS só para ler `.len` (numeração de lambdas lifted). É desperdício puro.
- `copy_bytes_to_current_region` (`:1706`) — `[..out, src[i]]` byte-a-byte → O(bytes²) por
  rodata/func movido pra região-pai. Bounded por item mas O(n²) local.
- `copy_relocs_to_current_region_*`, `copy_encoded_func_to_current_region_*`,
  `lifted_suffix`, `concat_lfuncs`, DWARF `dwarf_collect_facts`/`dwarf_subprograms_of`/
  `dwarf_rows_of` (`:1524/1595/1614/1727/1772`) — todos `[..out, x]` em laço.

### Fato-chave que destrava o design (LIDO, não presumido)

`rodata_symbol(index)` (`lower.tks:5205`) = `".Lstr" + index`, com `index = ctx.rodata.len`
(`intern_rodata`, `:5222`). **Os símbolos de rodata são POSICIONAIS**: a numeração de cada
item começa em `robase.len`. É POR ISSO que `robase`/`lifted_ph` são threaded — não pelos
BYTES, mas pela CONTAGEM (manter `.Lstr<n>` monotônico e sem colisão). Logo o threading do
array inteiro é acidental: o que precisa viajar é um `u64` de offset, não o backing.

### A correção (4-naturezas, por-sítio — de onde vem o N)

O N total (texto/funcs/relocs/rodata) só é conhecido DEPOIS de baixar todos os itens — mas
os itens já são baixados UMA vez no laço. Reestrutura `emit_native_x86` em **duas fases sem
re-lowering**:

**Fase 1 — laço por-item (N = `prog.items.len`, CONHECIDO; array fixo, index-fill):**
```
var results: [prog.items.len + 1]FusedItemOut = []
var rodata_base: u64 = robase.len      // offset de numeração, threaded como ESCALAR
var lifted_base: u64 = 0               // idem, substitui lifted_ph[]
```
Cada iteração: baixa+encoda o item na região-filha (mantém — é o ganho de memória do
descarte do lixo pesado de lowering/regalloc), MOVE a saída sobrevivente pra região-pai
(bytes+relocs+símbolos do item, bounded por-item), e grava `results[i] = out` (index-fill,
ZERO copy-grow do `results`). Threading vira escalar: `rodata_base += out.rodata_delta.len`,
`lifted_base += out.new_lifted_count`. `region_drop(child)` por item como hoje.

**Upstream necessário (numeração por offset — pré-requisito, "adiantar o que der"):**
- `intern_rodata` passa a numerar `rodata_symbol(ctx.rodata_base + ctx.rodata.len)`; adiciona
  campo `rodata_base: u64` ao `LowerCtx`/seed. Cada item baixa com `ctx.rodata = []` (só o
  delta dele) e `rodata_base = <running>`. Símbolos globais seguem únicos.
- `lifted_ph: []LFunc` (usado SÓ por `.len` em `lifted_suffix` e como base de numeração) vira
  `lifted_base: u64`. Verificar que `lower_item`/`lower_virtual_main` consomem `lifted` apenas
  por `.len` (não indexam conteúdo); se indexarem, thread um placeholder de tamanho exato via
  `[lifted_base]LFunc` alocado UMA vez na fase 2 — não por item.
- `loose`: verificar que `lower_item` trata `loose` como append-only (não lê entradas
  anteriores para numeração). Sendo append-only, coletar `results[i].loose_delta` e concatenar
  UMA vez para o virtual-main. (loose = statements top-level, poucos — risco baixo.)

**Fase 2 — assembly única (N agora CONHECIDO por somatório barato sobre `results`):**
```
total_funcs  = Σ results[i].encoded.len
total_text   = Σ_i Σ_f results[i].encoded[f].bytes.len
total_relocs = Σ_i Σ_f results[i].encoded[f].relocs.len
total_rodata = prelude_rodata.len + Σ results[i].rodata_delta.len
```
Pré-aloca `text: [total_text]byte`, `syms: [total_funcs]Symbol`,
`relocs: [total_relocs]RelocX86`, `rodata: [total_rodata]LRodata` e faz UM passo index-fill,
com `text_off` corrente (aritmética) rebaseando relocs por `+text_off` no ato. Isto é a
natureza-2 (PARSE/SCAN: conta na 1ª passada barata = o somatório, grava por índice na 2ª).
Substitui `fold_encoded_func_x86`, `fold_encoded_funcs_*`, `commit_rodata_delta`,
`commit_loose`. O `mt`/`ModuleTextX86` growing SOME; o objeto final é montado uma vez.

**Correções locais de cópia (natureza-1 MAP, N = `.len` da fonte):**
- `copy_bytes_to_current_region` (`:1706`): `var out: [src.len]byte = []; loop { out[i]=src[i] }`.
- `copy_relocs_to_current_region_x86`/`_arm64`, `copy_relocs_to_current_region`,
  `rebase_relocs_x86`, `rodata_relocs_x86`, `append_relocs_x86`, `lifted_suffix`,
  `concat_lfuncs`: idem, pré-aloca `[fonte.len]T` + index-fill.
- DWARF (`dwarf_collect_facts`/`dwarf_files_of`/`dwarf_subprograms_of`/`dwarf_rows_of`/
  `dwarf_insert_by_offset`): `dwarf_collect_facts` é natureza-3 FILTRO (pré-aloca
  `[prog.items.len]` + `count`, corta); os `_of` são natureza-1 MAP sobre `facts`.
  `dwarf_files_of` é FILTRO de distintos (pré-aloca `[facts.len]`, corta por `count`).

**Nota sobre a MOVE-pra-pai:** `copy_encoded_func_to_current_region_x86` (`:1733`) hoje NÃO
copia `bytes` (só símbolo+relocs) — funciona porque o `append_bytes` seguinte copia os bytes
pra pai antes do `region_drop`. Com o assembly diferido pra fase 2 (após todos os drops), a
move de bytes pra pai passa a ser OBRIGATÓRIA na fase 1: `copy_encoded_func_to_current_region_*`
tem que copiar `bytes` também (via o `copy_bytes_to_current_region` já corrigido acima). Sem
isso, os bytes ficam na região-filha já dropada = UAF.

### Espelhamento arm64 / win

- `commit_rodata_delta` e `commit_loose` são COMPARTILHADOS pelos três emitters — corrigir uma
  vez cobre os três.
- `fold_encoded_funcs_x86` (`ModuleTextX86`) é usado por `emit_native_x86` E `emit_native_win`.
  `fold_encoded_funcs_arm64` (`ModuleText`) por `emit_native_arm64_fused`.
- `fold_encoded_func` (arm64, `encode_arm64`) espelha `fold_encoded_func_x86`: mesma
  restruturação (assembly em fase 2, pré-sizado).
- `copy_encoded_func_to_current_region_arm64` (`:1989`) idem à x86.

### Critério de aceitação (ratchet D68)

O pico do `teko: memory: peak <N> MB` do build seco tem que CAIR (estrito) — o alvo é matar
o O(n²): de 13,5 GB para O(tamanho do objeto final) ~ dezenas-de-MB no assembly. Sem O(n²)
em nenhum acumulador de módulo. Arrays em RAM mas BOUNDED (D148: zero C novo — tudo Teko).

---

## Eixo B — completar o lowering (N2 + variadic + const-aggregate)

### O coração: fat-pointer / vtable / closure-dispatch

**A infra fat JÁ EXISTE e funciona (lida no `lower.tks`):**
- `IfaceFatPtr { data, vtable }` com layout `{data@0, vtable@ptr_size}` — `lower_iface_fatptr`
  (`:2428`) carrega os dois meios; `lower_iface_slot` (`:2441`) faz `vtable[slot]`.
- `ClosureFatPtr { target, env }` — `lower_closure_fatptr` (`:2583`).
- `lower_len_out_call_indirect` (`:5298`) = a **convenção fat-return** (DPS): passa um slot
  escondido (alloca i64) como último arg; a chamada devolve `ptr` no result + `len` carregado
  do slot. É EXATAMENTE o mecanismo de retorno str/slice em duas metades.
- `lenv_lookup_fat`/`lenv_lookup_fat_slot` — bindings fat carregam os dois meios (ptr+len).
- `is_fat_type` (`:4669`) = str ∨ char ∨ slice.

Ou seja: dispatch por interface com resultado NÃO-fat **já roteia certo** (`lower_iface_call`
`:2463` funciona quando `!is_fat_type(e.type)`). Os honest-stops são de duas classes:

**(B-fat-result) O RESULTADO do dispatch é fat (str/slice) — 6 sítios.** `2464` (iface),
`2593` (closure), `4702` (fat interface-dispatch result), `6391`/`3102`/`3137` (retorno lambda
fat). **Fix mecânico:** rotear ao caminho fat-return já existente. O par já existe: compare
`lower_closure_call` (`:2592`, resultado escalar via `lower_call_indirect`) com
`lower_closure_call_fat` (`:2603`, resultado fat via `lower_len_out_call_indirect`). O stop é
só o ramo `if is_fat_type(e.type) { return error }` que deveria chamar a variante `_fat`.
Fazer o mesmo par para `lower_iface_call` (criar `lower_iface_call_fat` usando
`lower_len_out_call_indirect` com `iface_call_args(fp.data, rest.vregs)`) e para os retornos
de lambda. É plumbing da convenção existente, ZERO máquina nova. Assinaturas a adicionar:

```
/**
 * lower_iface_call_fat — interface dynamic dispatch whose result is a fat value (str/slice).
 *
 * Mirrors lower_iface_call but returns through the len-out convention so the two halves of
 * the fat result survive.
 *
 * @param ctx  the current lowering context
 * @param e    the call expression (carries the fat result type)
 * @param c    the resolved interface-dispatch call node
 * @return     the fat result (ptr + len) or a lowering error
 */
fn lower_iface_call_fat(ctx: LowerCtx, e: checker::TExpr, c: checker::TCall): LoweredFat | error
```

**(B-vtable-classe) Dispatch por CLASSE-BASE polimórfica — o único item genuinamente novo.**
`aggregate_receiver_dispatch_stop` (`:2474`, disparado em `:2466`) para porque uma instância de
classe-base polimórfica é vista como struct plano (tem `LStructLayout`) e "não carrega vtable".
`2477`/`2647`/`4696`/`5263`/`5021` são facetas disto (receptor fat, cast fat, if-sem-else fat).

**A referência é a rota-C, que FUNCIONA (`codegen.tks`) — espelhar a semântica dela:**
- Classe-base polimórfica é representada como **fat `{ .data, .vtable }`** — IDÊNTICO a interface
  (`codegen.tks:10031` emite `{ .data = self, .vtable = tk_vt_… }`; `:4161` faz `.vtable[slot]`;
  `:6525` compara `.vtable == tk_vt_…`). O `tk_base_<Name>` (`:2866`) é esse fat.
- vtables são globais `tk_vt_<Classe>_<Iface>` (rodata), thunks por slot (`cg_emit_vtable_slot`
  `:10055`). Como o dispatch de INTERFACE já funciona no native, **os globais de vtable já são
  emitidos pelo native** (senão interface quebraria) — confirmar em `lower_const.tks`/emissão de
  globais; se faltar o caso classe-base, é a mesma emissão com a chave `<Classe>_<BaseClasse>`.

**Desenho native (unifica classe-base com interface — SEM máquina nova):**
1. **Reconhecimento de tipo:** o valor de tipo classe-base polimórfica é FAT `{data,vtable}`,
   não struct plano. `is_fat_type` NÃO deve incluí-lo (fat aqui é 2×ptr, não ptr+len) — é uma
   categoria fat distinta. Introduzir `is_class_dispatch_type(t, layouts, table)` = `t` é Named
   e é base polimórfica (usar o mesmo predicado que a rota-C: `cg_is_polymorphic_base`
   equivalente no checker — provavelmente já há `is_polymorphic_base`/marca na TypeTable).
2. **Layout do fat:** `{ data: ptr @0, vtable: ptr @ptr_size }` — REUSA `IfaceFatPtr` e
   `lower_iface_fatptr` (o layout é o mesmo). O upcast instância→base constrói esse par:
   `data = &instância`, `vtable = &tk_vt_<Classe>_<Base>` (via `global_addr_inst`).
3. **Dispatch:** `aggregate_receiver_dispatch_stop` deixa de ser um stop; quando o receptor é
   classe-base polimórfica, rotear por `lower_iface_fatptr` + `lower_iface_slot(vtable, slot)` +
   `iface_call_args(data, rest)` + `lower_call_indirect` (ou `_fat` se resultado fat). É
   LITERALMENTE o corpo de `lower_iface_call` — a diferença iface-vs-classe some no lowering.
4. **Sítios de construção do fat:** upcast em `4696`/`5263`/`5021`/`6227`/`6252` (espelhar os
   sítios da rota-C `codegen.tks:6227/6252/5797` que fazem `.vtable = tk_vt_…`). O if-sem-else
   como valor fat (`5021`) e o cast fat (`5263`) são o mesmo padrão de materializar `{data,vtable}`.

**ABI de chamada fat-pointer (o "peso"):** já é a convenção existente —
`iface_call_args(data, rest)` prepende `data` como receptor (arg 0);
`closure_call_args(env, rest)` prepende `env`. O target é `vtable[slot]` (indireto). Resultado
fat usa `lower_len_out_call_indirect` (slot escondido de len). NÃO há ABI nova a inventar;
o desenho é ESTENDER classe-base ao caminho iface e plumbar os 6 fat-result stops à variante `_fat`.

**Env-slot de captura MUTÁVEL de str/slice (`2647`).** `store_lambda_captures` (`:2639`) já
trata captura fat imutável: quando o slot é `Ptr` e o tipo é fat, grava AMBOS os meios
(`ptr` em `off`, `len` em `off + ltype_size(Ptr)`) — o env-slot já carrega o par (`:2651-2659`).
O stop (`2646-2648`) é só quando a captura é MUTÁVEL (`lenv_lookup_fat_slot` found = o local
tem um slot de frame endereçável). Desenho: o layout do env (`lambda_env_layout` `:2635`) já
reserva 2×ptr para campo fat; para a captura mutável, gravar o par lido do FRAME-SLOT do local
(carregar ptr e len do endereço do slot, gravar no env) em vez de tentar guardar o endereço do
slot de frame. Ou seja: mesmo idioma de `:2651-2659`, mas a fonte é o frame-slot (load antes de
store) — não o endereço. É o "carregar as duas metades do par" que a própria mensagem pede.

### Comparação (7): `1091/1103` (default), `1162` (str encadeada), `1215` ([]T encadeada),
`1191` ([]T== elemento não-numérico), `1239` (ordenada de agregado), `1281` (tagged-union vs membro).
Padrão único: comparação estrutural elemento-a-elemento. Desenho: uma função de comparação
lowered por TIPO (`lower_eq_of_type(t)` / `lower_ord_of_type(t)`) que recursa: primitivo → opcode
(magic L2); str → `str_eq` de superfície (chamada genérica); slice → laço `len`-guardado sobre
elementos chamando `lower_eq_of_type(elem)`; struct/tagged-union → AND dos campos / match de tag
+ compara payload. Sem name-detect: a comparação de agregado é chamada de função de superfície
gerada por tipo (consistente com D161 "toda função tem corpo"). O `==`/`<` primitivo continua
opcode inline; o de agregado desce à fn por-tipo. Reusar a lógica da rota-C `codegen.tks` como
referência de semântica (ela já compara agregados).

### Match não-variante (6): `4397` (slice), `4429/4496` (field), `4439/4506` (grouped-bind),
`3079` (destructuring — gap também na rota C `codegen.tks:7630`). Desenho: o match sobre não-
variante é uma cadeia de testes de igualdade (reusa a maquinaria de Comparação acima) + binds
por índice/field-offset. Slice-pattern = teste de `len` + compara prefixo/sufixo + bind do resto
como sub-slice (`slice_view`, NÃO cópia — D197). Grouped-bind e destructuring = múltiplos
`field_addr_inst`/index + bind no LEnv. Fazer a rota C (`codegen.tks:7630`) JUNTO (mesma passada,
mesma semântica) — é o único gap C listado.

### Boxing de agregado por largura (3): `4839` (push elemento agregado), `6159` (deref-assign
agregado), `6312` (element-write agregado). Raiz única: escrita de elemento agregado por-endereço.
Desenho: quando o elemento é agregado (largura > registrador), a escrita é `memcpy`-por-campo no
endereço do slot (`field_addr_inst` do slot + store por campo), não um store escalar. Uma função
`lower_store_aggregate(dst_addr, value, layout)` que os três sítios chamam.

### Diversos (8): `255/290/735/2393` (defaults do dispatch — ramos `_ =>` de honest-stop a
completar), `1066/1074` (defaults), `6093` (compound-assign — `x[i] += y` = load elemento +
opcode + store, plumbing), `5451` (in-vazio — `x in []` = false constante), `5480/5726`
(field/index como RECEPTOR de método — plumbing do endereço), `5998` (`zero<T>` de elemento —
emite zero-fill do tamanho de `T`), `3742` (if-as-value — materializa nos dois braços num slot
comum). Todos plumbing de padrões já existentes noutros caminhos.

### Float (4, estreito): `239` (float `%` — chamar `fmod` de superfície/opcode conforme ABI),
`4274/4340` (match-float — compara por igualdade de bits/valor), `2389` (`float::parse`
result-class — devolve `valor | error`, plumbing da convenção result). `+ - * /` de float já
funciona nos dois isel.

### Interpolação (2): `5328` (bool — a rota-C renderiza ternário sobre dois literais `tk_str`
estáticos; native emite select entre dois rodata `.Lstr` de "true"/"false"), `5344` (genérico —
rota ao `to_str` de superfície do tipo). **Outros (3):** `1365` (capture-frame panic-flag —
plumbing do flag), `4732` (`str::concat` sobre `[]str` — laço de concat por índice, N = soma dos
`.len`, pré-sizado — natureza-2), `6441` (função genérica — plumbing da monomorfização já feita).

### Variadic (1 causa, 2 sítios simétricos)

`isel_x86_64.tks:531` (`pin_args_x86`) e `isel_arm64.tks:738` (`pin_args`) param porque **`LCall`
não carrega a aridade** — SysV precisa do contador AL de vetores, AAPCS64 precisa da aridade fixa
para rotear a cauda à stack. **Fix (destrava os dois):** adicionar a aridade fixa (nº de params
fixos do callee) ao nó `LCall` na construção (em `lower.tks`, onde `call_inst`/`call_indirect_inst`
são montados) e propagar `variadic`+`fixed_arity` até `pin_args*`. Assinatura:

```
/**
 * LCall — an emitted call; `fixed_arity` is the count of the callee's declared fixed
 * parameters, needed by variadic ABIs to route the overflow tail to the stack (AAPCS64 §3.5)
 * and to set the SysV AL vector count.
 *
 * @param fixed_arity  number of fixed (non-variadic) parameters the callee declares
 */
```
Depois `pin_args_x86`/`pin_args`: para os args `[0..fixed_arity)` roteia por registrador como
hoje; para os variádicos, empilha na stack (AAPCS64) e (SysV) computa AL = nº de args FPR
passados por registrador. Remover os dois honest-stops.

### Const-aggregate (~8, `lower_const.tks`, trilha compartilhada C+native)

Float `55/232/59`, pointer/slice `216/231/392`, payload de variante `304`, const-fold só +/-
`49`. Desenho: `lower_const` já emite a imagem de bytes de const agregado (`add_rodata` com
`img.bytes`+`img.relocs`). Os stops são casos de elemento faltando: float const → emitir os bytes
IEEE-754 (reusa a serialização de float já existente no emit de literais); pointer/slice const →
emitir `{ptr-reloc, len}` como par com um `LDataReloc` ao símbolo alvo; payload de variante →
emitir tag + bytes do payload no offset do layout da variante; const-fold estender além de +/-
para `* / % << >> & | ^` (aritmética const de inteiros — reusa os opcodes). Trilha compartilhada:
o mesmo `lower_const` alimenta rota-C e native, então fecha os dois de uma vez.

---

## Eixo C — expurgo do reconhecimento morto de superfície (lexer + parser + threading)

Lei "NÃO DETECTAR/BARRAR O QUE NÃO EXISTE": o compilador não pode CONHECER/LEXAR/PARSEAR o que
não tem mais suporte. Censo abaixo, cada item com prova de morte.

### CENSO do lexer (tabela de keywords + `TokenKind`) — RESULTADO: nenhum token morto

Varri `keyword_kind` (`src/lexer/lexer.tks:235-277`) e o enum `TokenKind`
(`src/lexer/token.tks:2`). **Provas:**
- `unsafe` **NÃO é keyword do lexer** — não está em `keyword_kind`; é lexado como `Ident` e o
  parser casa o TEXTO `"unsafe"`. Logo o lexer NÃO muda para o expurgo de `unsafe`.
- `let`/`mut` — ausentes da tabela e do enum (canário confirma `BindKind { Var; Const }`). Já mortos.
- `#singleton`/`#arena_size`/`#arena_depth` — NÃO são keywords; seriam `Hash`+`Ident`. Nenhuma
  detecção residual no parser (grep `arena_size`/`arena_depth` em `src/parser` = 0). `singleton`
  ainda VIVO como `ServiceLifetime` (`service singleton`, D130 manteve) — NÃO é morto.
- Cada keyword de `keyword_kind` mapeia a um `TokenKind` CONSUMIDO no parser (medido: `Flags` 5,
  `Variant` 1@parse_decl:993, `When` 1@parse_arm:5, `Walrus` 1@parse_expr:31 (`name := val`
  named-arg), `Interface` 1@parse_decl:949, `Base` 1@parse_expr:384, `Defer` 1@parse_stmt:70,
  `Intern`/`Service`/`Macro`/`Comptime`/`Static`/`Global`/`Abstract`/`Virtual`/`Override`/
  `Class`/`SelfKw` todos ≥1). **Nenhum token/keyword morto no lexer.** (Enunciar explicitamente:
  o censo do lexer foi feito e deu ZERO — não pular.)

### `unsafe` — remoção total (parser deixa de conhecer o token)

`unsafe` foi aposentado (§6 aposentar-`unsafe`, base do D131). Remover à RAIZ:
- **`is_unsafe_modifier_at`** (`parse_decl.tks:171-172`) — DELETAR a função inteira.
- **Call-site `:1430`** (`if is_unsafe_modifier_at(tokens, k) { k = k + 1 }`) — DELETAR a linha.
- **Call-site `:1498`** (o `|| is_unsafe_modifier_at(tokens, pos)` no `is_core_decl_keyword_at`)
  — DELETAR o termo do OR.
Após: `grep -ri "unsafe" src/parser src/lexer` = 0 (fora de nomes não-relacionados como
`CallUnsafeAssignment` do regalloc, que é outro conceito). Zero detecção/ramo/mensagem residual.

### `manual` (modo memória-manual) — arrancar de ponta a ponta

O modo `manual` (memória-manual) não existe mais (D130: região=param SEMPRE; não há modo sem
auto-inserção de região). **Origem do flag:** CLI `--manual` → `manual_of(args)`
(`project.tks:2971`) + o parse-e-descarta em `:3039`. **Prova de morte:** `grep -rn "\-\-manual"
scripts/ .github/` = 0 — NENHUM script/CI passa `--manual`; na prática `manual` é sempre falso, e
o conceito está aposentado. Remover à RAIZ (não "threadar false"; o conceito SUMIR):
- **CLI:** `manual_of` (`:2971`) + o ramo `else if args[i] == "--manual" { i = i + 1 }` (`:3039`).
- **Threading em `project.tks`:** parâmetro `manual` de `checked_program_of` (`:204`),
  `frontend_parse` (`:412`, incl. o ramo `if !manual && wants_base_prelude` `:424` → vira
  incondicional `wants_base_prelude`), `frontend_check` (`:454`), `frontend_body` (`:469`),
  `build_ungated` (`:2485`), `compile_project_g` (`:2875`) — e as duas chamadas em `main.tks:62`
  e `:98` (dropar o arg `teko::build::manual_of(args)`).
- **`TProgram.manual`:** campo em `checker::TProgram` (remover do tipo em `tast.tks`), e
  `tprog_with_manual` (`tast.tks:107`). Todos os literais `TProgram { … manual = … }` perdem o
  campo (`typer.tks:5682/5690/5786/5870`, `consteval.tks:806/855/896`, `comptime_fold.tks:1161/
  1483`, `comptime_expand.tks:32`, `monomorph.tks:1189`, `lsp_api.tks:6/37`, `tkb_read.tks:827`,
  `project.tks:175/2439/2716/3410`).
- **`TypeTable.manual`:** campo em `resolve.tks` (o tipo), `tt_set_manual` (`resolve.tks:29`), e o
  campo em cada literal `TypeTable { … manual = … }` (`collect.tks:47/330/1707/1734`,
  `resolve.tks:19/42/63/67/228/721/2092/2214`). A chamada `tt_set_manual(…, prog.manual)` em
  `lower.tks:6536` (`lower_prelude`) some.
- **Ramos que consultam `table.manual` (código morto de detecção):**
  - `resolve.tks:754` (`resolve_named_builtin_fallback`: `if … && table.manual`) e `:767`
    (`resolve_named`: `if … && !table.manual`) — colapsar para o caminho `!manual` (o único vivo):
    builtins SEMPRE resolvem na frente (o fallback manual-only some).
  - `check_modules.tks:78` (`if !table.manual { … builtin_type … }`) → incondicional; `:164`
    (`if table.manual { return diags }` antes de `check_reserved_type_redefs`) → remover o
    early-return (reserved-type-redefs SEMPRE roda).
  - `lower.tks:596` (`open_frame_region`), `:671` (`open_native_region`), `:685`
    (`close_native_region`) — os `if ctx.table.manual { return ctx }` são código morto (região
    SEMPRE abre; D130). DELETAR os guards.
- Após: `grep -ri "\.manual\b\|tt_set_manual\|--manual\|tprog_with_manual" src/parser src/checker
  src/lir src/build main.tks` = 0.

**Cross-cutting:** `TProgram`/`TypeTable` mudam de forma → o C emitido muda → **exige reseed**
(fixpoint gen2==gen3). Vai no mesmo lote da Passada 1.

---

## Ordem de ataque (UMA passada) + metodologia expurgo

Metodologia (dono): CONSTRÓI a maquinária nova → SEED → o compilador ENUMERA o resto (erros de
self-build apontam os sítios) → varre. Ordem para maximizar destravamento cedo:

1. **Variadic (aridade no `LCall`) + Eixo A (copy-grow)** PRIMEIRO. Variadic destrava o lower a
   completar (`pin_args*` deixa de parar); copy-grow para o OOM → o build native COMPLETA o
   lower em todas as pernas e revela o PRÓXIMO stop real. Sem isso, não dá para validar o resto.
2. **Cluster vtable/fat-dispatch** (o coração): classe-base→fat unificado com iface, os 6
   fat-result plumbados à variante `_fat`, env-slot de captura mutável. Maior risco de byte-mover
   — encenar em degraus verdes (rota-C é a referência de semântica).
3. **Match não-variante + Comparação + Boxing de agregado + Const-aggregate** (a fn de comparação/
   store por-tipo é compartilhada entre Comparação, Match e Boxing — fazer junto). Const-aggregate
   fecha C+native na mesma passada.
4. **Cauda** (Diversos + Float + Interpolação) — plumbing de padrões já prontos.
5. **Eixo C (unsafe/manual)** — pode ir em paralelo/junto (independente do B); é remoção
   cross-cutting que força reseed, então casa com o reseed final da passada.

Ao longo: cada camada fechada builda no checkpoint (não build-por-linha, D "MENOS BUILD"); o
compilador auto-compilando enumera os sítios remanescentes. Zero C novo (D148). Região=param.

---

## Gate (prova de conclusão)

- **Ladder native** (`scripts/fixpoint_gate.sh` com `TEKO_FIXPOINT_BACKEND=native`, single-target
  host): o build native PARA de OOMar, passa pelo lower em TODAS as pernas, e revela o PRÓXIMO
  stop (ou fecha). O sucesso do Eixo A+variadic = o lower não aborta por falta de aridade nem o
  processo por OOM; o pico do build seco CAI (ratchet D68, estrito — a linha canônica
  `teko: memory: peak <N> MB`).
- **Rota-C regressão (o gate completo, D163/D164/D166/D185):**
  1. fixpoint **gen2.c == gen3.c byte-idêntico** (gen0 do `bootstrap/teko.c` commitado builda o
     tip; ladder inteira do seed novo, não de artefato cacheado).
  2. **ASan+UBSan** limpo (`clang -fsanitize=address,undefined -fno-omit-frame-pointer -g` do gen0
     compilando o tip) — pega stack-use-after-scope/UAF/OOB (o Eixo A mexe em posse de bytes entre
     regiões — classe de bug que o ASan pega e o build seco esconde).
  3. **3 harnesses C** `scripts/*_test.sh` verdes (se tocar `teko_rt.{c,h}` — o Eixo A NÃO deve,
     mas o `manual` remove guards de região; conferir grep dos nomes em `scripts/**/*.{c,h}`).
- **Varredura árvore-inteira** (D191/D197): `grep -ri "unsafe\|\.manual\b\|tt_set_manual\|--manual"`
  em `src/ + cases/ + examples/ + tklib/ + tooling/ + main.tks` = 0 (fora de `CallUnsafe…` do
  regalloc). Const-aggregate e match tocam `cases/`/`examples/` → validar por probe isolado (gen0
  compila o arquivo; `teko test .` local dá OOM).
- **Reseed INCONDICIONAL** ao fim (D164): `bootstrap/teko.c` reseedado (TProgram/TypeTable mudaram
  de forma), gen2/gen3 no scratchpad da worktree, verificador independente confirma antes do dreno.

---

## Riscos + tensões de lei (resoluções law-first)

1. **[Eixo A] O threading de `robase`/`loose`/`lifted_ph` é só contagem?** Provado para rodata
   (`.Lstr<index>` posicional → basta `rodata_base: u64`). Para `loose` e `lifted_ph`, o
   implementer DEVE confirmar que `lower_item`/`lower_virtual_main` os consomem append-only / só
   por `.len` (não indexam conteúdo prévio). Se INDEXAREM conteúdo → threading do array persiste,
   mas então aloca-se o buffer de tamanho EXATO uma vez na fase 2 (não copy-grow por item). Não é
   fork de design — é verificação; o design não-quadrático vale nos dois ramos. **Sem HALT.**
2. **[Eixo A] Manter os child-regions por-item + diferir assembly** exige mover os BYTES pra pai
   na fase 1 (senão UAF pós-`region_drop`). Resolvido no design (`copy_encoded_func` passa a
   copiar bytes via index-fill). Alternativa se a residência dos bytes de todos os itens
   simultânea for cara: contar numa 1ª passada (re-lowering barato) — porém dobra CPU; preferir a
   keep-results. **Sem tensão.**
3. **[Eixo B vtable] Emissão dos globais `tk_vt_<Classe>_<Base>` no native.** Como interface-
   dispatch já funciona no native, os vtables JÁ são emitidos; o caso classe-base é a mesma chave.
   Se faltar SÓ o par `<Classe>_<BaseClasse>`, é aditivo (mesma emissão). Confirmar em
   `lower_const.tks`/emissão de globais. Risco baixo, referência-C existe. **Sem HALT.**
4. **[Eixo B] Comparação/Match de agregado como fn de superfície por-tipo (D161) vs inline.** Lei
   D161/D187: toda função tem corpo de superfície; a comparação de agregado é chamada genérica, o
   `==`/`<` PRIMITIVO fica opcode inline (magic L2, caso-2 da lista fechada). Sem name-detect novo.
   Resolvido law-first.
5. **[Eixo C] `manual` remove os guards `if ctx.table.manual` de abertura de região** — isso
   ALTERA comportamento SÓ no caminho `--manual` (que ninguém usa; sempre-false hoje). Como o
   guard só disparava com `manual=true` (morto), remover é no-op semântico no caminho vivo +
   remoção de código morto (lei "não detectar o que não existe"). Reseed cobre a mudança de C.
   **Sem tensão.**
6. **[Escopo] Adjacências reportadas, NÃO viradas issue (lei):** `str.slice`/`arr_slice`/
   `str_slice_bytes_view`/`bytes_slice_view` que copiam apesar de "view" (D197, onda futura) NÃO
   entram nesta passada; os `append_bytes`/`[..x,y]` de módulos-folha (mime/protobuf/lower_const)
   fora do caminho de emit native NÃO são o OOM e ficam para a dívida NO-PUSHES geral. Reportado.

Nenhuma tensão de lei genuinamente aberta → **sem HALT**. O design resolve tudo law-first.
