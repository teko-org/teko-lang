# Plano — `params T[]` tipado como em C# (`ngen/`, primeiro item de superfície pós-v0.1.0)

Desenho antecipado (architect-first), base `70ec0827`, `mc` **0.15.13** pinado por `ngen/MC_VERSION`. Fecha a dívida
**`params` + float** (HANDOFF §5 bloco V1; plano §74(b), §76(a), §79(c)) e a dívida **"pacote de `params` sem
reclaim"** (`ngen/lib/rt.tk`), trocando a lista de PALAVRAS de hoje (`i64 total(params xs)`, `xs_len`,
`tk_va_put`/`tk_va_at`) pelo `T[]` de heap que o K3 já entrega.

**Nada é preciso do `mc`.** `syntax_param` (0.10.3) dá a posição de parâmetro de função livre e `syntax_type` (0.14.2)
dá o sufixo `[]` em TODA posição de tipo — as duas já em uso. `params` deixa de ser o TIPO do parâmetro e passa a ser
um **modificador** lido antes do tipo, exatamente como `ref`/`out` (`tk_ref_kind`/`tk_ref_param`, `teko_ref.tk`), que
já funcionam nos dois sítios de parâmetro. **Estilo:** `ngen/*.tk` é dialeto mc e a casa é o cabeçalho `//` dos outros
31 módulos (D213, D64.7); a lei Javadoc/W15 governa `src/*.tks`, congelado — não há tensão.

## 1. Semântica C# a adotar

1. **Declaração:** `void f(params i64[] xs)` — ÚLTIMO parâmetro, um só, sem `ref`/`out`, sem `default`, nunca em
   `extern`. O tipo é um `T[]` de verdade (linha `TK_KARRAY` de `teko_struct.tk`).
2. **Corpo:** `xs` é um `T[]` comum — `xs.Length`, `xs[i]`, `xs[i] = e`, `foreach (T v in xs)`, passar adiante,
   devolver. **Zero código novo:** `tk_hp_add`/`tk_hp_find` já respondem por parâmetro `T[]` nos três consumidores
   (`teko_params.tk:126` índice, `teko_expr.tk:477` `.Length`, `teko_loop.tk:421` `foreach`); o oráculo tipa `xs` como
   qualquer parâmetro, sem ramo novo.
3. **Chamada, forma EXPANDIDA:** N argumentos de tipo `T`, N ≥ 0 → o compilador constrói o `T[]`; zero argumentos =
   array de comprimento 0, não `null`. **Forma NORMAL:** um único argumento já de tipo `T[]` passa DIRETO, sem cópia
   (D197 — o que era view continua view).
4. **`T`:** escalar (largura/sinal por `tk_ldn`/`tk_stn`), **float** (`f32`/`f64`, declarado float em todo slot por
   onde passa — armadilha 31; a dívida do V0 item 1 fechada por FUNCIONAR, não por recusar), classe/interface/delegate
   (o array possui as referências: `rc_inc` no store, `tkarr_release_T` solta slot a slot), `str`/`ptr` (idênticos a
   `uptr[]`, sem contagem).
5. **Sobrecarga (C# §12.6.4):** candidato aplicável em forma NORMAL vence o expandido — sai de graça, as duas rodadas
   exatas de `tk_ov_resolve` casam `pick(1)` em `pick(i64)` antes de a rodada nova rodar, o mesmo desenho da 4ª rodada
   de default (C6). **Conversão de elemento:** identidade ou derivada→base/interface (`tk_row_fits`); literal inteiro
   cai em qualquer inteiro do núcleo (a regra `loose` do C4); **sem** conversão numérica implícita — `total(1)` em
   `params f64[]` é RECUSADO.
6. **Fora desta onda (dívida declarada, reportada):** `params` em MÉTODO/construtor (os 5 sítios de despacho e o slot
   de vtable por assinatura são outro byte-mover; hoje o caminho virtual já é silenciosamente errado, e a recusa TROCA
   um furo por uma mensagem); `params` genérico; `T[][]`; lambda como elemento; default + expansão na MESMA chamada.

## 2. Desenho na teko

**Grafia (`tk_va_kind`, `teko_params.tk`)** — o irmão de `tk_ref_kind`, chamado nos DOIS sítios de parâmetro, antes do
tipo:

```
// `params` diante de um tipo: o modificador, nunca mais o tipo do parametro
i64 tk_va_kind() { return type_of_token(p_id()) == tk_ty_params; }

// `params T[] xs` -- le o tipo depois do modificador e exige que seja um `T[]`
i64 tk_va_param(i64 line, uptr fl) {
    p_next();
    i64 ty = tk_ns_param_ty();
    if (ty >= 0) p_next(); else ty = p_type();
    if (!tk_is_ha(tk_struct_by_ty(ty)))
        err_at(fl, line, "teko: `params` names an array type: write `params T[] xs`");
    return ty;
}

void tk_va_mark(i64 pnode); i64 tk_va_marked(i64 pnode);   // a marca por NO (precedente `tk_rp_add`)
i64  tk_va_expand_call(i64 n, i64 vi);                     // o empacotador, usado pelos DOIS passes
```

- **`teko_default.tk` `tk_default_param`** (função livre): ramo novo ANTES do de `ref`/`out`; marca o nó
  (`tk_va_mark`), recusa `=` (mensagem existente), chama `tk_hp_add` como qualquer `T[]`. **`teko_class.tk`
  `tk_params`** (membro): vê o modificador e RECUSA.
- `tk_ty_params` continua `type_new` (é o que faz `type_of_token` reconhecer a palavra); nenhum nó de parâmetro
  carrega mais esse id, então `tk_va_check_stray` segue pegando `params` fora de posição.
- **Tabela** chaveada por NÓ; `tk_va_collect(root)` deriva no passe `(nome, nfixed, rowty, ety)` — `ety =
  ha_ety_at(tk_struct_by_ty(rowty))`, sem tabela paralela.

**Sítio de chamada** — uma cadeia de expressões, como o `tk_cap_put` do K4 e o `tk_va_put` de hoje (armadilha 28: N
nunca vira N parâmetros):

```
total(a, b)  ->  total(tkarr_put_i64(tkarr_put_i64(tkarr_new_i64(2), 24, a), 32, b))
```

`tkarr_put_T` é **gerado por linha de elemento**, ao lado do release/alloc do K3, por um `tk_ha_ensure_put(si)` de
bandeira própria (só quem usa `params` paga; a AST de quem não usa não muda): `uptr tkarr_put_T(uptr a, i64 off, T v)
{ [rc_inc(v);] stW(a + off, v); return a; }` — `T` DECLARADO no parâmetro (armadilha 31), offset constante no sítio,
`rc_inc` só se o elemento é contado.

**Posse / destruição (fecha "pacote sem reclaim"), sem maquinaria nova:** o `T[]` é contado (`tk_is_counted` = 1 para
`TK_KARRAY`), o nó do `tkarr_new_T` já nasce possuído (`tk_xt_add(.., 0)`), e valor possuído em posição de argumento é
**parkado** por `teko_rc.tk` (`rt_mark`/`rt_park`/`rt_sweep`) e solto no fim da statement — junto com cada elemento
contado, via `tkarr_release_T`. O nó EXTERNO da cadeia leva `tk_xt_put(chain, si, rowty, 1)`: o tipo é a linha `T[]`
(o que `tk_rc_call_args` lê) e `pure=1` diz **emprestado**, para o park acontecer UMA vez, no alocador de dentro.
Elemento contado é sempre `rc_inc` no store; a posse do argumento em si segue do park/sweep que já existe.

**Interações:** `ref`/`out` e `default` não se misturam; `typeof` não precisa de ramo; DI não é tocada; `&total` passa a ser LEGAL — a recusa `tk_va_check_addr` some.

**Sobrecarga** (`teko_over.tk`): **quinta rodada** de `tk_ov_resolve`, tentada só depois das duas exatas e das duas de
default — `tk_ov_fits_params(d, args, na, tys, loose)`: `na >= nfixed`, os fixos casam por índice, cada elemento da
cauda casa `ety`. Escolhido o candidato, `tk_va_expand_call(n, vi)` empacota ANTES do
`set_nd_name`/`tk_fill_defaults`. O teto `na > MAXPARAMS` (e o `i64 tys[MAXPARAMS]` do mesmo frame) sobe para
`TK_MAXVAARGS 32` quando o nome tem candidato `params`.

**Migração (mata o corpo morto na própria onda):** saem `tk_va_clone`/`_drop`/`_inst`/`_lower_len`/
`_lower_index`/`_elem`/`_pack`/`_check_float_*`/`_check_addr`/`_unlink`, as tabelas `va_node`/`vi_*`, os ramos
`tk_va_*` de `tk_bracket` e `tk_va_new`/`_put`/`_at` de `lib/rt.tk`; `surface_params.tk` é reescrita e as mensagens
sobre "lista de palavras" somem. O teto por sítio deixa de ser 12 (a cauda vai para a MEMÓRIA, não para o ABI): só
`nfixed + 1 <= MAXPARAMS`.

## 3. Ordem de passes

`pass(&tk_params_pass)` **MUDA DE LUGAR** (não é passe novo — `mc limits` segue `passes 15/30`): sai de antes do
oráculo e vai para **depois de `tk_ops_pass`, antes de `tk_default_pass`**. Por quê:

- **depois de `typeof`/`ref`/`deleg`/`ops`**: é o que dá TIPO ao argumento — `total(d(2.0))` só é um `callp` com o
  cast `(f64)` colado depois do `tk_deleg_pass` (o furo silencioso do §79(c)), e operador sobrecarregado só é chamada
  tipada depois do `tk_ops_pass`.
- **antes de `default`/`over`/`rc`**: os dois primeiros censam por ARIDADE (a chamada precisa já ter 1 argumento); o
  `rc` é quem parka o array.
- **a razão do "ahead of tk_params_pass" no `tk_array_pass` dissolve**: o passe não caminha mais todo `N_INDEX` (o
  índice resolve no PARSE, por `tk_hp_find`); o cabeçalho de `teko_array.tk` é corrigido junto — probe obrigatório:
  array GLOBAL indexado + `params` no mesmo programa. O passe é **pós-ordem** (`total(total(1,2), 3)` empacota o
  interno primeiro) e DESLIGA cada argumento da lista de irmãos (`set_nd_next(t, 0)`) antes de torná-lo argumento do
  put.

Armadilhas do §5.1 que este crumb pisa: **33** (o cast que DECLARA só vale colado — o argumento entra INTEIRO, nunca
se desembrulha o `(f64)`, e todo `tk_xt_put` cai no nó de FORA); **27** (não se palpita tipo: `-1` é silêncio, e nome
SOBRECARREGADO é silêncio no `tk_params_pass` — resolve a quinta rodada); **18** (`top_add` do `tkarr_put_T` só do
PASSE, nunca dentro da lista de parâmetros ainda sendo lida).

## 4. Fixtures e recusas

`ngen/tests/surface_params.tk` reescrita (`expect-exit: 42`), um caso por linha de prova: `total(params i64[] xs)` com
`.Length`, `foreach` e índice, chamado com 0, 1 e 5 argumentos; `offset_total(i64 base, params i64[] rest)`;
`bytes(params u8[])` com 250/10 (largura, `st8`); **`favg(params f64[] xs)`** com literal, float de delegate
(`favg(d(2.0))`) e de virtual (`favg(p.area())`); **`count_of(params Circle[] cs)`** com `cs[0].area()` e `rt_live()
== 0` depois da statement e no fim do bloco; **forma normal** `total(pre)` e o repasse `total(xs)` de dentro de um
corpo `params`; **sobrecarga** `pick(i64)`/`pick(params i64[])` com `pick(1)`/`pick(1,2)`/`pick()`; aninhamento
`total(total(1,2), 3)`.

Recusas por probe (`ngen/_probe/`, apagado; cada uma com o gêmeo de CONTROLE que compila):

| probe | mensagem |
| --- | --- |
| `params xs` (grafia velha) | ``teko: `params` names an array type: write `params T[] xs` `` |
| não-último / dois `params` | ``teko: `params` must be the last parameter, and there is only one`` |
| `params ref i64[] xs` | ``teko: a `params` list is not `ref` or `out` `` |
| `params i64[] xs = ...` | ``teko: a `params` list has no default`` |
| em método/construtor | ``teko: `params` is taught on a free function only`` |
| `extern` com `params` | ``teko: an `extern` symbol takes no `params` list`` |
| `total(1.5)` em `i64[]`; `Square` em `Circle[]` | ``teko: a value of type f64 does not convert to i64`` (`tk_reject_compat`) |
| `params i64[][] xs` | ``teko: an array of arrays is not taught yet`` (já existe) |
| 33 argumentos num sítio | ``teko: too many arguments for a `params` list`` |
| `params` fora de parâmetro | ``teko: `params` declares a parameter list, nothing else`` |

## 5. Crumbs e gate

| # | crumb | tam. | AST esperada |
| --- | --- | --- | --- |
| P0 | **scout/probe, zero código**: medir no tip `params` em método (direto E virtual), `&total`, e global-array + `params` juntos | S | — |
| C1 | **`tkarr_put_T` gerado** (`teko_heaparr.tk`: `tk_ha_put_fn` + `tk_ha_ensure_put`) — aditivo, ninguém chama | M | `same=45 diff=0` |
| C2 | **O FLIP** (atômico): grafia + recusas + `tk_va_expand_call` + movimento do passe + fixture reescrita + remoção da máquina velha de `teko_params.tk` | L | `same=44 diff=1` |
| C3 | **sobrecarga**: quinta rodada + `TK_MAXVAARGS` + o caso na fixture | M | `same=44 diff=1` |
| C4 | **limpeza**: `tk_va_new`/`_put`/`_at` saem de `lib/rt.tk`; cabeçalhos de `teko.tk`, `teko_type.tk`, `teko_over.tk:54`, `teko_default.tk:243`, `teko_array.tk:450` | S | `same=0 diff=45`, todo `<` do diff é uma das 3 decls removidas |
| C5 | **docs**: HANDOFF §5 (bloco novo) + plano §80 | S | sem build |

**Ritual (gate completo em C1, C2, C3, C4 — o crumb não fecha sem ele):** `rm -rf ngen/build`, build do zero;
`--entry-only` **45/45**; `--dump-ast` das 45 contra o compilador+árvore da base, MESMO `mc` dos dois lados; `mc
limits ngen --config` → `verdict ok`, `intrin 8/16`, **`passes 15/30`** (o passe é MOVIDO, não somado — delta aqui é
erro de desenho); `sh ngen/scripts/bootstrap.sh` → **`FIXPOINT OK`** (`teko1.o == teko2.o == teko3.o`, teko1 compila
as 45); `ngen/mc.toml` e `ngen/scripts/` intocados; `git status` limpo. `mc_teko.tk` não usa `params` (grep = 0): o
fixpoint não deve sentir o FLIP.

## 6. Riscos, e o que fica reportado

1. **Park duplo** (array solto duas vezes → `reference count below zero`), se o nó externo da cadeia for tagueado como
   POSSUÍDO — o desenho manda `pure=1` nele e deixa o park no `tkarr_new_T` de dentro. Prova: `rt_live() == 0` na
   fixture `count_of` + probe de 100 chamadas em laço.
2. **Menores:** elemento float no banco inteiro (armadilha 31 — mitigado por construção: `T` declarado no put gerado,
   store por `tk_stn`; probe `f32` além do `f64`); desembrulhar `(f64) callp(...)` ao empacotar (armadilha 33); o que
   o `tk_array_pass` cobria (probe P0-c); `tys[MAXPARAMS]` transbordando antes da quinta rodada, se o teto não subir
   junto.
3. **Tensão de lei resolvida (não é HALT):** D226 (superfície segue C#) pediria `params` em membro; a lei do corte
   v0.1.0 ("o que não se ensina, recusa-se") e a ordem D214 mandam recusar agora — a recusa troca um furo silencioso
   (o despacho virtual de hoje) por mensagem. **Reportado ao dono:** membro e construtor são um crumb M seguinte
   (empacotar nos 5 construtores de chamada; slot de vtable por assinatura via `tk_ty_sfx`), reaproveitando este
   desenho inteiro. Também reportado, não virou item: conversão numérica implícita do C# e default+expansão na mesma
   chamada seguem recusados.
