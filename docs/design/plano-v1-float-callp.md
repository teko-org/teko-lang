# V1 — retorno float por chamada indireta (`(T) callp(...)`)

> **LANDADO** (2026-09-06, branch `feat/ngen-v1-float-callp`). Errata — o que divergiu deste
> desenho, os achados medidos e o §74(b) fechado — em `plano-ngen-entrega4.md` §79; o que ficou na
> árvore, em `ngen/HANDOFF.md` §5 (bloco V1) e §5.1 item 33.

Fecha o item 1 do §74(b) do `plano-ngen-entrega4.md`: `callp` é tipado `TY_I64` pelo núcleo, `walk_ret_type()` numa
chamada INDIRETA sempre responde inteiro, o `fa_result` de `<float>` nunca move `d0`/`xmm0` para o destino — e as
três formas indiretas (delegate, virtual, interface) só acertam por coincidência de registrador.

## 1. Estado do bloqueio: FECHADO

O mc **v0.15.13 está publicado** (`NOTICES-teko.md`, 2026-09-06; `minicompiler/mc` main `0648e1a`, PR #43). Nada em
V1 espera dependência. Contrato, lido no fonte (`mc/src/gen_resolve.mc`, arm `N_CAST` de `res_expr`): quando
`nd_kind(a) == N_CALL && res_kind(a) == RK_INTRIN && res_decl(a) == IN_CALLP` sobre `a = nd_a(n)`, o núcleo faz
`set_res_type(a, nd_type(n))` — o tipo do cast desce para o nó do `callp`. Um cast **DIRETAMENTE** sobre o `callp` declara o retorno. `gen_expr` grava `walk_ret = res_type(n)` antes de
rebaixar, logo o `MTASK_CALLP` de `<float>` (`fa_callp` → `fa_result`, `lib/machine_arm64_float.mc:499`) lê o tipo
certo e move `v0`. Cast EXTERNO vira identidade (`fa_cast` retorna em `src == ty`); `(i32) callp(...)` recebe o
estreitamento M45 em `gen_callp` e o cast repete um `sxtw` idempotente. Sem cast: `TY_I64`, inerte. **O item 2 do
§74(b) (`fa_w` sem as conversões single) também saiu na 0.15.13** (`FI_SCVTF_D..FI_UCVTF_D` e
`FI_FCVTZS_D..FI_FCVTZU_D` com `+2`); em V1 é **só re-conferir** o probe arm64 `(i64)(f32)`, zero código novo.

## 2. Censo — onde a teko emite `callp` com valor

Cinco construtores; todo call-site funila num deles. O retorno está em MÃO nos cinco, de tabela PRÓPRIA da teko
(nunca de `decl_*` do núcleo) — float é detectável em parse-time E pass-time.

| # | construtor | arquivo:linha (do `callp`) | despacho | quando | retorno vem de |
|---|---|---|---|---|---|
| 1 | `tk_deleg_build` | `ngen/teko_deleg.tk:344` | delegate (thunk) | pass (`tk_deleg_pass`) | `dg_ret_at(si)` |
| 2 | `tk_emit_call` | `ngen/teko_expr.tk:338` | virtual (vtable) | parse (`syntax_infix(".")`) | `mt_ret_at(mi)` |
| 3 | `tk_this_emit` | `ngen/teko_this.tk:340` | virtual, `this` implícito | pass | `mt_ret_at(mi)` |
| 4 | `tk_pend_emit_call` | `ngen/teko_typeof.tk:410` | virtual, receptor só o oráculo tipa | pass | `mt_ret_at(mi)` |
| 5 | `tk_itab_emit` | `ngen/teko_iface.tk:293` | interface (itab) | parse e pass | `im_ret_at(sr_m0_at(si) + j)` |

Chamadores, todos cobertos ao envolver DENTRO do construtor: `teko_expr.tk:244/354/376/395`, `teko_heaparr.tk:174`, `teko_deleg.tk:369`,
`teko_prop.tk:417/421` (propriedade virtual), `teko_this.tk:274/288/366/386/404`, `teko_typeof.tk:414/433/466/491`.
Não é sítio: `teko_typeof.tk:250` (walk do PRÓPRIO módulo, `void`). DI e lambda não têm forma própria — lambda é delegate (#1), serviço injetado é campo/local e cai em #2/#5.

## 3. Desenho — UM shaper de retorno, cinco sítios

`tk_cast` (`ngen/teko_array.tk:280`) já constrói exatamente o nó do contrato: `N_CAST` com `nd_type = ty` e
`nd_a = e`. Hoje só o #1 molda retorno, e só para inteiro estreito (`tk_deleg_ret_narrow`, `teko_deleg.tk:322`, que
devolve 0 para float **de propósito** — sem o contrato do núcleo, um cast ali CONVERTIA o resultado inteiro). Com a
0.15.13 essa exceção some: os dois casos são a MESMA declaração, na mesma sintaxe. Unificar num shaper só, em
`teko_array.tk` logo abaixo de `tk_cast` (incluído em `teko.tk:224`, antes dos cinco sítios), e **apagar**
`tk_deleg_ret_narrow`:

```
// o cast que uma chamada INDIRETA tem que carregar: `callp` não nomeia callee, então o núcleo tipa o nó TY_I64
// (mc src/gen_resolve.mc) a menos que um cast fique DIRETAMENTE sobre ele -- é o único lugar onde o retorno pode
// ser declarado, e o que faz walk_ret_type() responder float e o fa_result de <float> mover d0. Um INTEIRO
// estreito toma o mesmo cast, pela regra do próprio núcleo (walk_narrow, mc src/gen_walk.mc): uptr é a palavra
// da máquina e nunca é estreito, o que não é TK_INT/TK_SINT é do módulo, e void não toma cast nenhum.
i64 tk_callp_ret(i64 ret, i64 call) {
    if (ret == TY_VOID) return call;
    i64 k = type_kind(ret);
    if (k == TK_FLOAT) return tk_cast(ret, call);
    if (ret == TY_UPTR) return call;
    if (k != TK_INT && k != TK_SINT) return call;
    if (type_width(ret) < 8) return tk_cast(ret, call);
    return call;
}
```

Rejeitado: chamar `walk_narrow(ret)` do núcleo direto — é símbolo INTERNO de `gen_walk.mc`, e a 0.15.12 já quebrou a
teko renomeando internos (`cfg_file` → `cfg_file()`, HANDOFF §3.2). Espelhar a regra mantém V1 sem dependência nova.

| sítio | hoje | em V1 |
|---|---|---|
| #1 `tk_deleg_build` | `if (tk_deleg_ret_narrow(ret)) return tk_cast(ret, call); return call;` | `return tk_callp_ret(ret, call);` |
| #2 `tk_emit_call` | `r = tk_call("callp", …)` … `tk_xt_add(r, rs, 0)` | `r = tk_callp_ret(mt_ret_at(mi), tk_call("callp", …))` **antes** do `tk_xt_add` |
| #3 `tk_this_emit` | `return tk_call("callp", …)` | idem, com `mt_ret_at(mi)` |
| #4 `tk_pend_emit_call` | `return tk_call("callp", …)` | idem, `mt_ret_at(mi)` (o `st64(pty, …)` acima fica) |
| #5 `tk_itab_emit` | `return tk_call("callp", …)` | idem, `im_ret_at(sr_m0_at(si) + j)` |

**Regra de identidade (quebra em silêncio se ignorada):** o núcleo casa o cast com o filho IMEDIATO, e todo registro
por POSIÇÃO de nó (`tk_xt_put`/`tk_xt_add`, e o `node_assign`/`tk_node_replace` que copia o resultado para o nó da
árvore) vale sobre o nó que o shaper DEVOLVE, não sobre o `callp` interno. Em #1 (`tk_deleg_call:378`), #3 e #4 o
`node_assign`/`tk_node_replace` já copia o nó de fora — correto por construção; em #2 o `tk_xt_add` vem DEPOIS do
shaper (inerte hoje, float não é contado; armadilha quando o shaper cobrir contado).

## 4. Efeito no `--dump-ast`

O envelope só dispara com retorno float (o ramo de inteiro estreito é byte-idêntico ao de hoje no #1, e nos #2–#5
nenhuma fixture declara retorno estreito por despacho indireto). **Nenhuma das 45 fixtures atuais tem chamada
indireta que devolva float**: só `primitives_float.tk` e `surface_refout.tk` mencionam `f32`/`f64`, e nas duas o
float viaja por `ref`/`out`/campo, nunca pelo retorno de método virtual, de interface ou de delegate. Logo
`--dump-ast` fica byte-idêntico nas 45, **exceto** as três que V1 toca: **`same=42 diff=3`**; o diff em cada uma é o
caso novo mais uma linha `CAST type=f64` na indentação da chamada, com `CALL name=callp` e filhos um nível abaixo
(`mc/src/ast.mc:340`). O dump roda DEPOIS dos passes (`mc/src/cli.mc:323`), então o envelope dos sítios de pass sai
igual ao dos de parse. `mc limits` intocado: `intrin 8/16`, `passes 15/30`.

## 5. Fixtures e probes

Um caso novo por forma de despacho, na fixture que já é dona daquela forma; `expect-exit: 42` inalterado nas três;
nenhuma fixture nova. Cada caso devolve 0 e o `main` soma `N + bad` no padrão da própria fixture — o de
`types_class`/`types_interface` termina em aritmética, não em `return 42`, e o bloco novo entra ANTES dela.

| fixture | caso | forma |
|---|---|---|
| `ngen/tests/surface_delegate.tk` | `fdcheck` — `delegate f64 Scale(f64 x);` sobre função nua e sobre campo de classe; `1.0 + f(2.0)` (o aninhado expõe o defeito) e um `f32` | #1 |
| `ngen/tests/types_class.tk` | `fvcheck` — `public virtual f64 area()` + `override`, por nome tipado na BASE, por `this` implícito e por parâmetro | #2/#3/#4 |
| `ngen/tests/types_interface.tk` | `ficheck` — membro de interface `f64 span();` por receptor e por parâmetro de tipo interface | #5 |

**Probes** (fora de `ngen/tests/`), cada um rodado TAMBÉM contra o compilador da base, para separar correção de regressão:
1. o repro mínimo do §74(b), sem nada do ngen (`f64 direct` / `f64 nested`) — base: `nested` = 3.0; tip: 5.0;
2. `f32` pelas três formas, contra literal `f32` E com `(i64)` do resultado — a re-conferência do item 2 do §74(b);
3. mistura: `1.0 + d(2.0)`, `d(1.0) * v.area()`, float indireto como ARGUMENTO de outra chamada;
4. **inteiro estreito por despacho indireto** (`virtual u8`, `interface i32`) — o ramo que o shaper unificado cobre
   nos #2–#5, onde hoje não há molde. Medir a BASE primeiro: se ela já acerta (a extensão M45 do lado do CALLEE
   cobre), é cinto-e-suspensório e não muda AST; se erra, é defeito adjacente fechado de passagem — **reportar**;
5. float indireto dentro de `params`: `tk_va_arg_ty` (`teko_params.tk:474`) lê `N_CAST`, então a recusa de argumento
   float em `params` (dívida do §74(b)) passa a alcançar a chamada indireta — confirmar a mensagem, não miscompile.

## 6. Ordem de crumbs e gate

**Commit 0 — bump `ngen/MC_VERSION` 0.15.12 → 0.15.13**, pela ordem do HANDOFF §3.2 e só ela: (1) baixar a release e
rodar o baseline local — 45/45 contra o mc novo; (2) `sh ngen/scripts/bootstrap.sh` contra o mc novo — `FIXPOINT OK`;
**só depois** (3) trocar o arquivo e registrar a mudança no §3.2. Nunca o inverso; não toca `.tk` nenhum.
**Commit 1 — delegate (#1):** `tk_callp_ret` em `teko_array.tk`, `tk_deleg_ret_narrow` apagado, `tk_deleg_build` pelo
shaper, caso `fdcheck`. **Commit 2 — virtual (#2/#3/#4):** `tk_emit_call`, `tk_this_emit`, `tk_pend_emit_call`, caso
`fvcheck`. **Commit 3 — itab (#5):** `tk_itab_emit`, caso `ficheck`. **Commit 4 — docs:** `plano-ngen-entrega4.md`
(§74(b) fechado + seção V1) e `HANDOFF.md` §5 (fila).

**Ritual (gate completo) ao fim de CADA commit de 1 a 3** — cada um independentemente gate-able: `rm -rf ngen/build`,
build do zero, `--entry-only` **45/45** em exit 42/70; `--dump-ast` das 45 contra a base (`same=44 diff=1` por commit,
**`same=42 diff=3`** acumulado ao fim do #3); `mc limits ngen --config` `verdict ok`, `intrin 8/16`, `passes 15/30` —
os MESMOS da base; `sh ngen/scripts/bootstrap.sh` **FIXPOINT OK** com o bloco `provenance`; probes 1–5 base→tip.
`ngen/mc.toml` e `ngen/lib/rt.tk` intocados (V1 é só AST).

## 7. Riscos, e onde o envelope tem que acontecer

1. **O cast ser dobrado/removido por passe.** `fold_cast` (`mc/src/parse.mc:1124`) sai imediatamente quando o filho
   não é `N_INT`; um `callp` é `N_CALL`, então `fold` só recursa pelos argumentos. Do lado da teko nenhum passe
   reescreve `N_CAST`: os três sítios que o mencionam (`teko_typeof.tk:203`, `teko_params.tk:474`, e o próprio
   `tk_cast`) só LEEM `nd_type`. O probe 1 mede ponta a ponta.
2. **Ordem dos passes / onde envolver.** #2 e #5 emitem em PARSE, antes de todos os passes; #1, #3 e #4 no meio deles
   (`teko.tk:443-514`: di → array → params → typeof → ref → deleg → ternary → switch → ops → default → over → rc).
   Envolver DENTRO do construtor é o único ponto que serve às duas famílias e põe o cast na árvore antes de
   `tk_ops_pass`/`tk_over_pass`/`tk_rc_pass`. **PROIBIDO** envolver num passe posterior: um nó inserido entre o cast
   e o `callp` quebra o casamento do núcleo **em silêncio** (compila e devolve o valor errado de novo).
3. **`tk_ty_of` passa a responder onde respondia -1** (`xt` primeiro, depois o arm `N_CAST` → `nd_type`): a chamada
   indireta que devolve float deixa de ser "tipo desconhecido" para o oráculo, e isso atinge `tk_ops_pass`,
   `tk_ternary_pass`, `tk_over_pass` (sobrecarga por tipo de argumento) e a recusa de float em `params`. Em todos,
   `f64` não é linha da tabela de tipos e a resolução cai no caminho do núcleo como já caía com -1 — **análise, não
   medição**: os probes 3 e 5 provam. `tk_pty_of` (`teko_struct.tk:972`) NÃO tem arm `N_CAST`, segue em -1, e a
   checagem de argumento em parse-time não muda.
4. **`xt` por posição de nó** e **dependência de símbolo interno do núcleo**: resolvidos por desenho no §3 — revisão de diff, não medição.

## 8. Achados adjacentes — REPORTADOS, não virados em item

- Os sítios #2–#5 não moldam retorno INTEIRO ESTREITO nenhum, enquanto o #1 molda desde o K2w; o shaper unificado fecha isso de passagem e o probe 4 mede se era defeito vivo ou cinto-e-suspensório.
- `f64 total(params xs)` segue errado (lista de PALAVRAS, `tk_va_put`/`tk_va_at` em `i64`) — é superfície (`params T[]`), não V1; a recusa do `teko_params.tk` é o que segura.
