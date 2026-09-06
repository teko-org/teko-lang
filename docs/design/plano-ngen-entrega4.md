# Entrega 4 do `ngen/` — superfície: default, `params`, sobrecarga, operadores

Plano executável. Escopo dado pelo dono (2026-09-04): **parâmetros default**,
**multiparâmetros à la C#**, **sobrecarga** por assinatura (a sobrescrita já
existe) e **sobrecarga de operadores**.

**Restrição dura do dono:** nada de `fn`/`func`/`def`/`function`. Funções e
métodos seguem o modelo do próprio mc — C/C++/C#, tipo de retorno primeiro
(`i64 area(uptr self)`). Não se ensina palavra introdutória de declaração e
não se reimplementa o `fn` do core (o `examples/lang` faz isso; nós não).

## 1. Descobertas MEDIDAS (probes em scratch, `mc 0.10.0`, a release do CI)

1. **As cinco `decl_*` do M31 existem e funcionam** (`mc/docs/reference/hooks.md`
   §"Asking about a declaration the core already parsed"; corpo em
   `mc/src/parse.mc:1886-1941`). **`decl_param_type` PRESERVA o id que
   `type_new` devolveu** — medido: `type_new("Ring")=8` e
   `decl_param_type(d,2)==8`, sem colapsar em `TY_*`. É o que torna sobrecarga
   e despacho de operador por tipo viáveis **sem ensinar o `fn`**.
2. **Só o que já foi parseado é visível.** `decl_find` de função declarada
   abaixo dá −1; um **protótipo** acima resolve. De dentro do próprio corpo a
   função ainda não está em `unit_head` — a ponte é um **`pass()`**, onde a
   unidade inteira existe.
3. **`syntax_infix` sobre operador do CORE é morto — e em SILÊNCIO.**
   `syntax_infix("+", …)` registra sem reclamar, mas `parse_unit()` chama
   `ops_init()` como primeira instrução (`mc/src/parse.mc:2110` → `:293-306`) e
   `infix_set` zera a coluna de handler (`:279`). Como `user_init()` roda ANTES,
   o handler nunca dispara: medido com `err_at` incondicional dentro dele —
   `1 + 2` compila e dá 3. **A rota de sobrecarga de operador é `pass()`, e só
   ela.** (Reportado à sessão que desenvolve o mc.)
4. **`MAXPARAMS` = 12**, não 8 (`mc/src/arena.mc:58`: 1..8 em registrador,
   9..12 na pilha). O teto de 8 em `mc/examples/lang/README.md:243-246` é do
   `lx`, não do core.
5. **Parâmetro default em função LIVRE é inalcançável hoje.** `parse_params`
   (`mc/src/parse.mc:1966-1993`) só aceita `tipo nome`, e não há hook no
   caminho: `parse_top` (`:2076`) consulta `syntax_find` só no primeiro token, e
   `word_add` recusa `i64/u8/…` (`mc/src/hooks.mc:233-238`). Em MÉTODO não há
   problema — o corpo do tipo é parseado por nós.
6. **A rota `pass()` foi provada ponta a ponta:** mangling de sobrecarga com
   reescrita do sítio de chamada, preenchimento de argumento default no
   `N_CALL`, e `N_BINARY`→`N_CALL` in-place preservando `nd_next`. O
   `function declared twice` mora em `mc/src/gen_resolve.mc:171`, depois dos
   passes — o pass chega primeiro.

**Correções que estas medições impõem ao `ngen/HANDOFF.md`:** a armadilha 6 do
§5.1 ("o core não reporta o tipo de um parâmetro") está **errada** — ele reporta,
só não antes de a declaração fechar; e o teto de parâmetros é 12, não 8.

## 2. Divisão que barateia tudo

O corpo de `class`/`struct`/`interface`/`trait` é parseado por **nós**
(`ngen/teko_class.mc:334` `tk_member`, lista em `:212` `tk_params`). Logo, para
**métodos** os quatro itens são alcançáveis **hoje**, sem API nova nem `pass()`.
Só **função livre** depende das `decl_*` e de passes — e o default de função
livre está bloqueado (§1.5).

## 3. Crumbs, em ordem

| # | o que ensina | rota | fixture |
|---|---|---|---|
| C0 | glob do CI aceita `surface_*.tk` (`.github/workflows/ngen.yml:129`) | — | nenhuma |
| C1 | **default em método** — `i64 scale(self, i64 k = 2)` | parse próprio | `surface_default_method.tk` |
| C2 | **sobrecarga de método** por assinatura | parse próprio | `surface_overload_method.tk` |
| C3 | **oráculo de tipo estático** em `pass()` (`teko_typeof.mc`) — sem superfície nova | `pass()` + `decl_*` | `surface_typeof_param.tk` |
| C4 | **sobrecarga de função livre** (mangling `nome__T0__T1`) | `pass()` | `surface_overload_free.tk` |
| C5 | **sobrecarga de operador** — `Vec operator+(self, Vec b)` | `pass()` sobre `N_BINARY` | `surface_operator.tk` |
| C6 | default em função livre — **BLOQUEADO** (§1.5); entrega só a metade de chamada | — | nenhuma |
| C7 | **`params`** — `i64 total(params xs)` | palavra-tipo + `pass()` | `surface_params.tk` |

Notas de forma: `operator` é membro **contextual** dentro do corpo do tipo (como
`virtual`/`override` já são), não palavra reservada no programa. `params` é
registrado com `type_new` — a grafia C# `params i64[] xs` **não** é alcançável
(o core não tem `[` em posição de parâmetro, e o `ngen` não tem tipo array).

Pontos perigosos, por crumb: em C2, `tk_slots_inherit` (`ngen/teko_class.mc:156`)
copia slots da base **por nome** e passa a precisar de (nome, assinatura), senão
uma derivada que sobrecarrega colide com o slot herdado. Em C3, a tabela
`tk_local` (`ngen/teko_struct.mc:392`) é global e sem escopo — o oráculo **não**
a herda, monta escopo por `N_FUNC`, e nome sombreado cai no caso conservador
(erro claro, nunca palpite). Em C7, o buffer variádico é estático e **não
reentra**: chamada variádica aninhada é recusada com mensagem própria.

## 4. Ritual por crumb

`mc build ngen --config <toml de host>` + **todas** as fixtures em exit 42 (as 9
atuais e as novas). Nos crumbs de `pass()` (C3-C5, C7), ritual extra: **prova de
no-op** — `--dump-ast` de `types_class.tk` e `types_interface.tk` idêntico antes
e depois de registrar o pass, quando o programa não usa a construção nova. É a
disciplina do `check-surface.sh` do próprio mc: um pass não mexe em árvore que
não é dele.

## 5. O único pedido de suporte ao mc

Para destravar o C6 (e simplificar o C7), o suporte mínimo seria **`on_param`** —
um hook na linha do `on_stmt`/`on_jump`, chamado por `parse_params` depois de
montar cada `N_PARAM`, com o parser no token seguinte, podendo consumir o
`= expr` e devolver o `N_PARAM`. Estimativa do architect: ~10 linhas no core, na
mesma forma dos hooks existentes. Alternativa mais estreita: o core aceitar
`= <constante>` e publicá-la por `decl_param_default(d, i)`.

## 6. Erratas e correção de rota (2026-09-04, depois de C1-C3)

**Errata do §2:** o corpo de `struct` **não** era parseado por `tk_member` — só
lia campos; `struct` não tinha método. O C1 puxou a correção para dentro do crumb
(lei do não-deferir): `struct` passa pela mesma máquina de membros do `class`, e
`virtual`/`override`/`use` num `struct` são recusados por nome.

**Dois defeitos silenciosos da entrega 3, expostos pelo C3** (medidos; código
errado, não mensagem ruim):
1. **Shadowing:** `i64 report(Shape s) { if (1) { Ledger s = new Ledger; }
   return s.area(); }` chama `ledger_area` sobre um `Shape` — a tabela `tk_local`
   (`ngen/teko_struct.mc:392`) é global e sem escopo, o `s` do bloco interno vaza.
2. **Resolução por nome único:** `a.extra()` com `a: A`, quando só `B` declara
   `extra`, compila e chama `b_extra(a)`. O correto é erro de compilação.

**Correção de rota — pelo mecanismo do mc, não por tabela própria.** O `ngen`
reinventou (mal) o que a doc e o `lx` já ensinam: rastreio de escopo é
`syntax_stmt("{")` (o módulo é dono de todo bloco — `mc/docs/reference/hooks.md:288-289`,
`examples/lang/lang_stmt.mc` `lg_block`), `p_blockdepth()` para a profundidade e
`on_jump` (M31) para as arestas de saída. Logo:
- a tabela de locais ganha **escopo por bloco** por essa via — empilha na entrada
  do `{`, desempilha na saída — e o shadowing resolve **no parse**, primeira
  linha de defesa; **não** se "defere todo `.` ao `pass()`" como paliativo;
- com o tipo do receptor conhecido, a busca de membro é **restrita àquele tipo**
  (e à cadeia de base/traits/interfaces dele) — nome que o tipo não declara é
  erro; o por-nome-único só sobrevive para receptor genuinamente sem tipo, e só
  até o oráculo (C3) responder;
- o oráculo do C3 fica para o que o parse **não alcança** (parâmetro de função de
  topo), como desenhado.
**Também no crumb do escopo (defeito de diagnóstico do C2, medido sem oráculo):**
`tk_call_refuse` (`ngen/teko_expr.mc:91-97`) mapeia `-2` para "the type of the left
side of `.` is not known here" — certo para `tk_method_by_name`, **errado** para
`tk_method_pick`/`tk_ifmeth_pick`, onde `-2` é "nenhuma assinatura aceita essa
quantidade de argumentos". `Alpha a; a.tally(1,2,3)` sem assinatura de 3 dá a frase
errada. Não miscompila; separar os dois significados de `-2` no contrato.

Esta correção entra como **crumb próprio, antes de C4/C5** — eles constroem em cima
da mesma resolução. Prova: os dois programas acima (o 1º sai 1; o 2º é rejeitado)
mais as fixtures existentes intactas e a prova de no-op do C3 preservada.

## 7. C7 landado com ressalva → C7b (2026-09-04)

**Landou** (`4521b3d8`): `i64 total(params xs)`, `xs[i]` por `syntax_infix("[")`
próprio (o core **não** constrói `N_INDEX`), sítio reescrito em
`total(tk_vaN(...), N)`. Teto real com `params`: 10 fixos e 12 argumentos por sítio.

**Ressalva medida pelo verificador:** `tk_va_at` (`ngen/lib/rt.mc`) faz `ld64` sem
guard — `xs[i]` fora do range devolve lixo do buffer em silêncio (provado: lê o
resto da chamada anterior). Fere a lei de falhar ruidosamente (`CLAUDE.md`, guard de
deref). **E a causa-raiz das duas restrições do crumb** (não-reentrância e lixo) é a
mesma: o pacote de argumentos mora num **buffer ESTÁTICO global** (`tk_va_buf[96]`).

**Orientação do dono:** o mc trabalha com **ponteiros 100% opacos** (`uptr`), e a
única extração é o **`&`** (address-of de local/global/função, `mc/docs/core-language.md:62`),
que os exemplos açucaram como **`ref`** (`examples/lang/README.md:55-75`, `lang.mc:72`).
Os agentes não incorporaram isso. **C7b, a correção:** o pacote passa a ser
**alocado por sítio** — na arena, por `rt_alloc(N*8)` (o mesmo caminho do `new`), ou
local + `&` — e o ponteiro opaco viaja com `xs_len`. Consequências: (1) chamada
variádica aninhada e variádica-dentro-de-variádica **deixam de ser recusadas** (não há
mais estado compartilhado); (2) `tk_va_at(xs, len, i)` ganha **guard de bounds com
`rt_panic`** (`arquivo:linha` no diagnóstico do pass onde couber); (3) `tk_va1..tk_va12`
somem — o pass emite a gravação por índice no bloco alocado.

**Ordem de passes (parecer do verificador):** registrar `pass(&tk_params_pass)`
**ANTES** do futuro pass de mangling do C4 — assim o C4 vê `total(ptr, n)` uniformizado
e não precisa saber o que é `params`. Guard que o C4 precisa: `total(params xs)` e
uma `total(uptr, i64)` colidem na mesma ABI depois do lowering.

**Errata de descrição (C2):** a mensagem de `fe750cdf` diz "a classe esconde as
sobrecargas da base, como em C#"; o que o código faz é **resolução por aridade,
nível a nível na cadeia** (cai na base quando a aridade não bate no nível atual).
Comportamento estável; a descrição é que está errada.

**C7b (em verificação, `49546d45`):** forma (a) — pacote por `rt_alloc(N*8)` com
`tk_va_put` encadeado na própria expressão; (b) local + `&` foi descartada com a doc:
`&` só aceita nome direto (`core-language.md:440`) e um pass sobre expressão não
insere statement antes de chamada que mora em condição/argumento/`return`. Guard nos
dois lados de `[0, n)` com `rt_panic`; recusas de reentrância removidas.
**Dívida nova, medida:** a arena bump do `rt.mc` não recupera — variádica de 2
argumentos em loop quente esgota os 4 MiB em ~262k chamadas (`teko: arena exhausted`,
exit 70). Falha **ruidosa**, nunca corrupção, mas é teto que o buffer estático não
tinha. A cura é reclaim/escopo de região (entrega de comportamento base, D214 item 3),
mesma dívida que o `new` já carrega — não deste crumb.

## 8. C8 — genéricos com constantes; C7c — `params` sobre C8 (dono, 2026-09-04)

**Ruling do dono:** o `params` com tamanho variável só se bloqueia pelo tamanho em
compile-time se o corpo for **instanciado por sítio com `N` constante** — é para isso
que ele pediu **genéricos com constantes** (`<T, const N: i64>`, provado no mc em
`examples/lang`: `Box<Circle, 4>`, D212). Fica **inline** e o índice literal é checado
contra `N` dentro da instância — sem análise interprocedural, sem guard de runtime
para o caso literal.

**C8 — genéricos com constantes (record/replay).** Precedente: `examples/lang/lang_class.mc:73-114`
(`lg_gen_record`: lê `<T, const N: i64>`, `p_start()` + `p_skip_balanced` — **não cria
a classe**) e `lang_type.mc:134-163` (`lg_replay`: monta `"class " + mangled + body`,
`p_subst_reset`/`p_subst_name`/`p_subst_int`, `p_push_source`, laço `top_add(parse_top())`;
`>>` por `p_resplit_punct(1)`, `lang_type.mc:91`). Guia: `mc/docs/guide/30-teaching.md`
§"Record and replay". Superfície C-like (D215): `class Box<T, const N: i64> { T items[N]; … }`
e `Box<Circle, 4> b = new Box<Circle, 4>;` — a forma do mc, sem `where` nesta fatia.
Instância é chave `(nome, args)` → mangling `Box__Circle__4`, uma vez por tupla.
Destrava também a forma genérica `<T>` de `wrap`/`unwrap` (handoff §5).

**C7c — `params` reescrito sobre C8.** Função com `params` vira **genérica em `N`**:
cada sítio com `k` argumentos instancia `total__k` com `N = k` substituído por
`p_subst_int`; `xs_len` deixa de ser argumento e vira a constante `N`; `xs[lit]` com
`lit < 0` ou `lit >= N` é **erro de compilação** na instância; índice não-literal
mantém o guard de runtime (`rt_panic`). O pacote continua alocado por sítio na arena
(C7b), até o reclaim. Teto `MAXPARAMS` = 12 permanece.

**Ordem revista da fila:** escopo pela via do mc (§6, corretude) → **C8** ∥ **C4**
(arquivos próprios: `teko_over.mc`) → **C7c** e **C5** (ambos tocam `teko_class.mc`
depois do C8) → C6 quando o mc der o hook.

## 9. C4 em verificação; C3b — o oráculo precisa tipar expressão (2026-09-04)

**C4 (`ecdd1a46`, em verificação):** `teko_over.mc`, pass registrado depois de
`params` e do oráculo. Decisão que difere do C2 de propósito: **toda** sobrecarga de
função de topo é renomeada (`pick__i64`, `pick__Vec`, `pick__i64__i64`, `tally__void`)
— nenhuma fica com o símbolo plano, para um sítio não reescrito virar **erro de link**
em vez de cair na primeira. Nome de assinatura única não muda. Guards: `&f` de
sobrecarregado; colisão ABI com `params` (`(uptr, i64)` homônima); `params` não se
sobrecarrega; `extern` e `main` não se sobrecarregam; ambiguidade recusada; o
`function declared twice` do core não é mascarado.

**Achado adjacente, medido — dívida do oráculo (C3b):** `tk_ty_of` responde por nome,
chamada, literal e cast, mas **não por `N_BINARY`/`N_UNARY`** (`pick(n - 1)` → "the type
of argument 1 of pick is not known here") nem por **acesso a membro escalar**
(`pick(self.side)` em método: `tk_pend_field` só registra no `xt` resultado de tipo
struct; escalar cai em −1). Ambos erram claro, não miscompilam — mas são formas comuns.
**A regra não é palpite, é a do core:** `mc/src/gen_resolve.mc` `res_binary` (~:486) —
tipo do binário = tipo do operando **esquerdo**; comparação e lógico = `i64`; `N_UNARY`
= tipo do operando, `!` = `i64`. Espelhar isso em `tk_ty_of`, e registrar no `xt` o tipo
escalar do campo em `tk_pend_field`. **Entra antes do C5** (operadores precisam tipar
`a + b` com `a` composto) e depois do escopo (mesmos arquivos: `teko_typeof.mc`,
`teko_expr.mc`). Fixture: `surface_typeof_expr.tk`.

**Fila revista:** escopo → C4 → **C3b** → C8 ∥ C5 → C7c → C6.

## 10. C3b em verificação; C3c — `.` sobre receptor escalar (2026-09-04)

**C3b (`9b73cd1f`, em verificação):** `tk_ty_of` tipa `N_BINARY`/`N_UNARY` espelhando
`res_binary` do core (usa o próprio `cmp_cond`); a tabela `xt` ganha `xt_ty` (tipo do
nó, escalar incluído) ao lado de `xt_str`; o placeholder deferido vira
`tk_call("tk_unresolved_member")` — sem o pass, o **`res_call` do core** recusa `call to
unknown function` com `arquivo:linha` (antes do linker; melhor que o pedido).

**C3c — achado adjacente, pré-existente:** `.` sobre receptor de tipo **escalar**
(`b.side.x` com `side: i64`, `x` declarado só por `Vec`) ainda cai no último recurso
por-nome e **compila**, emitindo load sobre um `i64`. Com o C3b o oráculo já distingue
"sem tipo" de "tem tipo e não tem membros" → recusar com `teko: i64 has no members`.
Entra junto do C5 (mesmos arquivos) ou como mini-crumb antes dele.

Nota do C4: literal em posição direta de argumento usa a preferência exata/frouxa
(`N_INT` sem tipo → desempata `i64`); literal dentro de binário (`pick(1 + 2)`) agora
responde `i64` pela regra do core — coerente, documentado.
**Fila revista:** escopo → C4 → **C8** → **C3b** ∥ C5 → C7c → C6.

## 11. C8 landado — o que ficou, e o que ele destrava (2026-09-04)

**Landou** em `ngen/teko_generic.mc` (módulo próprio; `teko.mc` só ganhou o
`#include`). O record é disparado no `class`/`struct` quando o nome é seguido de
`<` (`teko_class.mc:617`, `teko_struct.mc:713`): a lista de parâmetros é parseada,
tudo dali até o `}` é **gravado** por `p_skip_balanced`, nada é declarado, e o nome
do genérico é registrado com `syntax_stmt` — a única posição de onde
`Box<Circle, 4> b;` é alcançável, como o `lg_declstmt` do `examples/lang` faz.

O replay monta `class Box__Circle__4 <texto gravado>;`, liga os parâmetros
(`p_subst_name` para `T`, `p_subst_int` para `N`), empurra com `p_push_source` sob
`Box__Circle__4 instantiated from prog.tk:16` e drena `top_add(parse_top())` até a
profundidade voltar. Instância memoizada por `(nome, argumentos)`; ela entra na
tabela de tipos pelo **mesmo `type_new`** de uma classe qualquer, então `.`, `new`,
vtable e interface saem de graça. O `>>` de `Holder<Box<Circle, 2>>` é desmontado
por `p_resplit_punct(1)`.

**Três coisas medidas que o plano não previa:**

1. **O `;` do replay é obrigatório.** O `p_accept(K_SEMI)` que fecha um corpo de
   tipo roda depois do `}`; sem um `;` no fim do texto empurrado ele alcança o
   token seguinte da fonte de FORA e come o `;` do `new Box<Circle, 4>;`. O texto
   empurrado termina com `;` próprio.
2. **O replay tem de SALVAR o scratch de declaração.** Uma instanciação pode
   acontecer dentro do corpo de outro tipo (campo de tipo genérico, local numa
   método) — o `tk_class` aninhado zeraria a fila de traits (`tk_ntu`/`tk_nud`) e a
   lista de conformância (`tk_nconf`) do tipo de fora. `tk_gen_replay` salva e
   restaura os dois, mais `tk_line`/`tk_file`/`tk_own_methods`. Verificado por probe:
   classe com `use Counted;` + campo `Bag<Circle, 3>` + parâmetro e local genéricos.
3. **Campo array inline teve de ser ensinado junto** (`T items[N]`), porque é ele
   que faz a constante decidir o LAYOUT: `BOX__CIRCLE__2_SIZE` = 32 e
   `BOX__CIRCLE__4_SIZE` = 48, do mesmo texto. `p.items` é o ENDEREÇO do array,
   etiquetado com tipo do elemento e comprimento, e o `[` (`tk_bracket`) o consome
   no parse — não vira `N_INDEX`, então não cruza com o `params`.

**O bloqueio pelo tamanho constante — o que o dono pediu — está vivo:** `items[k]`
com `k` literal fora de `[0, N)` é erro de COMPILAÇÃO na instância
(`Box__Circle__4 instantiated from prog.tk:16:6: teko: index 5 is past the end of items[4]`).
Índice não-literal recebe guard de runtime `tk_ix`, **emitido pelo módulo uma única
vez e só no programa que indexa** — assim a prova de no-op continua exata (as 15
fixtures anteriores dão `--dump-ast` byte-idêntico a `043455b8`); pô-lo em
`lib/rt.mc` teria mudado a árvore de todo programa.

**Dívida medida:** `p.items[i]` sobre receptor que o parser não tipa (um parâmetro,
que só o oráculo resolve) não alcança o `[` de array e cai no `[` do `params`, que
recusa com `teko: `[` indexes a `params` list only`. Recusa clara, nunca
miscompilação; o fecho é no `teko_typeof.mc` (C3b).

**C7c fica pronto para escrever:** a máquina de que ele precisa — corpo instanciado
por sítio, `N` como literal substituído, índice literal barrado contra `N` — é
exatamente a que o C8 deixou.

## 12. C5 pronto (aguarda verificação), com duas regras que o plano não previa (2026-09-04)

**C5** (`feat/ngen-operators-v2` @ `c4124f26` = `299e1366` + costura com a API do C3c):
`teko_ops.mc`, `operator<op>` contextual em `tk_member`, pass entre o oráculo e o C4.
`N_BINARY` chega **intacto** ao pass (passes rodam antes de `fold()`), a troca in-place
sobrevive. 18/18; no-op nas 17.

**Armadilha medida, resolvida estruturalmente:** o próprio `ngen` constrói `N_BINARY`
com valor teko à esquerda — `p.side` é `ld64(p + SIDE)`, `items[i]` é
`ADD(ADD(obj, off), MUL(i, w))`. Sem distinguir, `operator+(self, i64)` declarado
transformaria todo acesso a campo em chamada. O pass trata o 1º argumento de
`ld8..st64` e a espinha esquerda desses `ADD`s como **endereço**, nunca operando.

**Regra de resolução (obrigatória por `surface_typeof_expr.tk:64`, `pick(v + zero)`):**
teko à esquerda que **não declara** o operador + valor do core à direita → o pass **não
toca** (é a aritmética de ponteiro do core). Teko dos dois lados, ou operador declarado
com assinatura que não casa → erro claro. Esquerdo do core + direito teko → recusa
("a reversed operator is not taught"). Unário → honest-stop.

**Adjacente (diagnóstico, não miscompila):** `operator` dentro de `interface` é recusado
por `teko_iface.mc` com "an interface declares methods, not fields: operator" — mensagem
confusa; dizer que operador em interface não é ensinado. Mini-ajuste em `teko_iface.mc`.

## 13. C7c pronto (aguarda verificação) — instância por cópia de AST (2026-09-04)

**Medido: record TEXTUAL de função de topo é inalcançável no mc 0.10.0** — `parse_top`
(`mc/src/parse.mc:2076-2082`) consulta `syntax_find` só no 1º token, que numa função é o
tipo de retorno, e `word_add` recusa keyword do core (`cannot redefine core keyword:
i64`). Confirma o bloqueio do C6. **Saída adotada (`feat/ngen-params-generic` @
`5104808a`):** como o único parâmetro genérico do `params` é a constante `N` — não há
tipo a substituir — a instância é **cópia da AST** com `N_INT` no lugar de `xs_len`
(`tk_va_inst`), memoizada por `k`, mangling `total__k`, moldura de erro `tk_gen_frame`
do C8. Sufixo numérico não colide com o `nome__Tipo` do C4. O template sai da unidade
(`tk_va_drop` → `N_NONE`) porque o `tk_ov_check_left` do C4 varre o array de nós.
`xs[lit]` fora de `[0, N)` → erro de compilação com prefixo de instância; não-literal
mantém `tk_va_at`. 17/17; no-op nas 16.

**Adjacentes:** (1) `teko_over.mc:177-192` `tk_ov_va_shape` ficou **obsoleta** — a lista
nunca mais lowera para `(uptr, i64)`; duas `g(uptr, i64)` escritas à mão num programa sem
`params` levam a mensagem errada ("a params list is (uptr, i64)…"). Apagar o guard.
(2) `params` em **método** é recusado (`wrong number of arguments`) — pré-existente;
`tk_method_pick` não sabe o que é lista. (3) O teto de 12 virou **política**: a instância
gasta um registrador a menos; se o dono quiser o teto real do ABI, são dois números.

## 14. Entrega 5 — comportamento base; crumb 1 = RECLAIM pela "arena automática" do mc (dono, 2026-09-04)

**Ruling do dono:** o reclaim segue o precedente do mc — a "arena automática" dos
exemplos. É o `examples/lang`: **arena fixa (4 MiB) com free lists por classe de
tamanho + reference counting por escopo**, tudo por hooks (`docs/guide/60-examples.md:171`,
`:264` "objects inside a 4 MiB arena, which is what proves the deallocation is real").
Peças, e onde o `ngen` já as tem:
- refcount no **word 1** do objeto (`+8`) — o `ngen` reservou desde o crumb do `class`
  (header 16 B: vtable@+0, refcount@+8); `Class_release` no **slot 0** da vtable;
- `rc_dec` para cada local de tipo classe na **saída do bloco**, em ordem reversa de
  declaração (`lang_stmt.mc:313-322`) — é o `syntax_stmt("{")` que o crumb de escopo já
  possui, agora injetando código; **`on_jump`** entra aqui, nas arestas `return`/`break`/
  `continue` (`hooks.md:495-541`);
- `return e` → `{ T $t = e; rc_inc($t); releases…; return $t; }` (`lang_stmt.mc:329-345`);
  `lg_eown` decide se a expressão já é dona (um `new`) ou precisa de `rc_inc`;
- `p.f = e` de campo classe: `rc_inc` do novo, `rc_dec` do antigo; `dispose(self)` roda
  quando o count chega a zero (`lang/README.md:78`); `rt_free` devolve ao free list.
Fecha a dívida de `new` e de `params` em loop quente (hoje: `arena exhausted` ruidoso).
**Prova:** fixture `surface_reclaim.tk` (`// expect-exit: 42`) com `live()` (contagem de
objetos vivos, como o `01-inherit.lx` imprime) voltando a 0 ao fim, e um loop de 1M
`new` + descarte que **não** esgota a arena. Ratchet: o pico de nós/heap do compilador não
cresce além do que o RC exige (`mc limits`).
Restrições: zero Variant (D217); nenhum toque no mc; forma C-like (D215).

## 15. Rulings D218 — o que muda na fila (2026-09-04, noite)

- **C5 está ERRADO e vai ser refeito (C5b):** operadores **como em C#** — estáticos, sem
  `self`, dois parâmetros explícitos (unário: um), `i64 + Vec` permitido (operador declarado
  em qualquer dos tipos dos operandos), resolução por sobrecarga sobre os dois tipos, pares
  obrigatórios (`==`/`!=`, `<`/`>`, `<=`/`>=`). O pass sobre `N_BINARY`/`N_UNARY` e a regra do
  §12 (endereço ≠ operando; core+core não se toca) **ficam**; muda a declaração, a tabela de
  candidatos e a resolução. A fixture `surface_operator.tk` é reescrita na forma nova.
- **Reclaim (entrega 5, crumb 1) redespachado** com **construtor/destrutor**: `Nome(params)`
  chamado por `new Nome(args)`; `~Nome()` chamado pelo release. Sem `dispose`.
- **Fila da entrega 5:** reclaim c/ ctor/dtor → C5b → `while`/`for` (prelude do mc) →
  `namespace`/`import`/`using` (lx) → `const` como açúcar sobre `#define` → (`match`/`when`
  em dúvida) → stdlib mínima. **Fora:** `var`, `type`. `break N` já é do core.
- **Pergunta ao dono em aberto:** C# escreve `public static … operator+` e `public Vec(...)`;
  o ngen ainda não ensina `public`/`static` (D196). Ensinar os modificadores junto, ou aceitar
  a forma sem modificador (`Vec operator+(Vec a, Vec b)`, `Vec(i64 x) { }`) até o D196 entrar?

## 16. D219 — `this` implícito e `base`: sweep ANTES do reclaim (2026-09-04, noite)

Métodos deixam de declarar `self`. O compilador injeta o receptor oculto (mesmo mecanismo
de hoje, `tk_params(... extra)`), `this` vira palavra contextual dentro de corpo de
tipo, nome não-qualificado que não é local/param resolve como `this.nome` (C#: local
sombreia campo), e `base.m()` chama a implementação da base direto (precedente `lx`:
`01-inherit.lx`, `README.md:71`). Interfaces: `i64 area();`. Construtor/destrutor
(D218) idem. Operador estático (D218) não tem `this`.
**Ordem:** este sweep toca `teko_class.mc`, `teko_iface.mc`, `teko_trait.mc`,
`teko_struct.mc`, `teko_expr.mc`, `teko_typeof.mc` e as 18 fixtures — por isso vem
**antes** do reclaim e do C5b, que escreveriam código na forma velha. Prova: 18/18 na
forma nova; `grep -c "self" ngen/tests` = 0; `base.m()` numa fixture existente.

## 17. D220 — visibilidade e `static`; fila revista (2026-09-04, noite)

Crumb **"membros C#"**: `public`/`private`/`protected`/`static` em membros, `public`/`internal`
em tipos; defaults do C# (tipo → `internal`, membro → `private`) **ratificados**; `internal` =
**código do projeto** (o que entra pelo `mc.toml` do próprio projeto; decidido pela origem da
declaração via `nd_file` — arquivo do projeto vs. bundle `<…>`/include externo), nem arquivo
nem namespace; checagem no `.`, na chamada, no
`new` e no `base.` — nos dois caminhos (parse e pass); `protected` = próprio tipo e derivadas;
`static` = sem receptor (é o que o operador do C5b usa; membro estático acessado por
`Tipo.m()`); nested **não**. Fixtures ganham `public` onde acessam de fora.
**Fila da entrega 5:** `this`/`base` (em voo) → **membros C#** → reclaim c/ ctor/dtor → C5b
operadores estáticos → `while`/`for` → `namespace`/`import`/`using` → `const`.

## 18. D221 — `loop` fica; closures sobre `&fn`/`callp` (architect-first) (2026-09-04, noite)

`loop`/`break N`/`continue`/`if` do mc **ficam**; `while`/`for` do prelude são adição.
**Closures:** crumb com desenho prévio (teko-architect): (1) função **local** (dentro de
outra) hoisted para o topo com nome manglado; (2) **lambda** inline; (3) tipo de função na
superfície e chamada `f(x)` açucarando `callp`; (4) passagem por `&fn` como `uptr`;
(5) **captura = PHP** (ruling do dono): explícita por **`use (a, b)`** na declaração da
função local/lambda; por valor por padrão, **`use (&a)`** por referência (o `&x` do mc);
objeto gerado com exatamente os campos do `use`; `use` é a mesma palavra contextual do
trait. Ao architect sobra: a forma C-like da lambda (ex.: `i64 (i64 x) use (a) { ... }`
como expressão, e função local nomeada `i64 f(i64 x) use (a) { }` dentro de outra),
tempo de vida do capturado por referência (escopo/RC), e recusas claras (capturar nome
que não é local; `use` sem função). Precedentes: `mc/docs/core-language.md`
(`&x` de função), `examples/desktop` (callbacks GTK bidirecionais), `examples/conc`
(`spawn` com `&fn`). Entra na fila depois de `const`.

## 19. D222 — `switch` (statement + expression), `break N` atravessa, `when` = guarda (2026-09-04, noite)

Crumb **`switch`** (`syntax_stmt("switch")` + `syntax_infix`/postfix para a forma expressão —
o `x switch { … }` tem `switch` à direita do operando; ver como o core deixa registrar uma
palavra em posição infixa sem ser operador do core): statement rebaixado a `loop` de uma
volta com `if`/`else` encadeados e `break` ao fim de cada braço (assim `break`/`break N`
do core atravessam como em C#); `default`; `case` com constante ou `when` guarda; expressão
com `=>` e `_`, tipada pelo oráculo (todos os braços do mesmo tipo, senão erro).
`match` sai da fila. Entra depois de `const`, antes das closures.

**§16 — sweep `this`/`base` pronto (`feat/ngen-this-base` @ `c224ac5e`, em verificação):**
módulo `teko_this.mc`; o receptor oculto chama-se **`this` também na AST** (`self` não
existe mais no `ngen/`); `this` é palavra (`syntax_expr`) válida só em corpo de tipo;
`base` é **contextual** (lido no `.`), rebaixado a chamada direta ao símbolo da base
escolhido por assinatura — contextual porque `offset_total(i64 base, params rest)` existe;
nome não-qualificado resolve no **pass** (o core entrega identificador cru; é onde locais/
parâmetros são legíveis), local/parâmetro sombreia campo. Prova: AST nova byte-idêntica à
velha em 16/18 módulo `name=self→this`; as 2 restantes divergem só pelo `base.m()`.
**Limite honesto:** campo array inline (C8) só por `this.items[k]` — recusa clara.

**§18, mecanismo (dono):** ponteiro de função, `ref T` e `out T` são **primitivas novas** por
`type_new` (Tier 4, `mc/docs/guide/96-a-new-primitive.md`: id ≥ `TY_MAX` é do módulo), não
`uptr` cru: o oráculo os distingue, `f(x)` sobre valor-função rebaixa a `callp` com o retorno
tipado pela assinatura; `ref`/`out` levam `&` no sítio e deref implícito no uso; `out` é o
DPS. Vai junto para o architect das closures — mesmo crumb de desenho.

## 20. D223 — propriedades, corpo default e `static` em `interface` (2026-09-04, noite)

Dois crumbs, logo após "membros C#":
- **Propriedades:** `get`/`set`/`value` contextuais em corpo de tipo; auto-propriedade gera
  campo de apoio (`private`); `p.X` → `get`, `p.X = e` → `set` nos dois caminhos (`tk_dot`
  no parse e `tk_pend_*` no pass); `virtual`/`override`/`static` e visibilidade por
  acessor; em `interface`, `i64 X { get; set; }` = assinaturas. Fixture
  `surface_property.tk`.
- **Interface v2:** corpo default (símbolo `Iface_m` no itab quando a classe não redefine;
  `this` dentro do default é o receptor implementador) e `static abstract` (o tipo
  implementador fornece `static`; `Tipo.m()`; conformidade checada como os métodos).
  Fixture `surface_iface_default.tk`. O crumb de membros em voo **recusa** `static` em
  interface com mensagem — este crumb substitui a recusa.

**§20 — propriedades e interface v2 PRONTOS** (`feat/ngen-properties`, 3 commits): módulo
`ngen/teko_prop.mc`; **acessor = método comum** da tabela do `teko_class.mc` (`get_X`/`set_X`),
de onde saem slot de vtable por acessor, `static`, sobrecarga e visibilidade por acessor;
auto-propriedade com campo de apoio `private` (`Nome__backing`); `set => ...` é STATEMENT
(`=` não é infixo do core); `get`/`set` lidos **só** dentro das chaves da propriedade e `value`
**só** no corpo de acessor — nenhuma palavra confiscada (`public T get()` de
`surface_generics.tk` intacto). Interface: corpo default vira `iface_m(uptr this)` no itab de
quem não redeclara, com `this` = receptor implementador e **todo** membro alcançado ali
despachando pelo itab; `static abstract` conforma como método e **não ocupa slot** (o slot é a
posição entre os de instância, `tk_ifslot`); propriedade de interface = assinaturas. 20/20 em
exit 42, AST das 18 anteriores byte-idêntica a `5579c34b`, `mc limits` ok. Limite: um default
só alcança membro declarado acima dele.

## 21. D224 — `abstract` (ruling) e `partial` (em avaliação) (2026-09-04, noite)

- **`abstract`:** entra no crumb de membros/propriedades ou logo após: `abstract class`
  (não instanciável), `abstract` método (sem corpo, slot de vtable, obriga `override` na
  primeira concreta), `abstract` só em classe `abstract`. Fixture `surface_abstract.tk`.
- **`partial class` (ratificado; método parcial NÃO — `partial` em método é erro claro):** custo a levantar — o tipo só fecha (layout, vtable,
  record de genérico) quando todas as partes foram lidas; como o mc parseia em uma passada e
  o `.` resolve no parse, parte declarada depois do 1º uso exige fechar o tipo no **pass**
  (o oráculo já resolve `.` deferido — é o mesmo mecanismo) ou exigir que as partes venham
  antes do uso. Sem método parcial. Fixture `surface_partial.tk`: a mesma classe em dois arquivos
  (`#include` do segundo), campos e métodos de ambas as partes, `new` depois das partes. Precedente do
  mc para "reabrir": `namespace` mergeando por prefixo (`examples/lang/README.md:265`).

**§17 — membros C# pronto (`feat/ngen-members` @ `0d3044a9`, em verificação):** módulo
`teko_access.mc`; **`internal`** = declaração lida de arquivo **dentro do diretório do
`mc.toml`** da build (sem config: o do arquivo de entrada); absoluto, `../` e `<bundle>` são
externos — decidido por prefixo de caminho normalizado, sem syscall. Duas origens apenas
(projeto / resto); instância de genérico herda a origem do template; membro de trait
copiado é da classe. `static`: campo → global mangled, método sem receptor, `Tipo.m()`.
AST 17/18 idêntica; `types_struct` diverge só pelo `static`.
**Achados:** (1) `p_start()` mente em token substituído (`subst_apply` troca `tok_start` pelo
lexema na arena) — usou `cp`; candidato a `p_cp()` público no mc; (2) `operator+` privado
ainda funciona de fora — o pass de `N_BINARY` não checa visibilidade; fica para o **C5b**
(operadores estáticos); (3) HANDOFF §5.1 item 8 estava obsoleto quanto a `base.m()`.

## 22. mc 0.12.0 — o que as releases 0.10.3/0.11.0/0.12.0 mudam na fila (2026-09-05, madrugada)

Baseline `fix/retirement` com **0.12.0**: 18/18 sem uma linha mudada. Do que entrou:
- **0.10.3 = M41.5 (PR #17), "the follow-ups the ngen consumer exposed":**
  1. **`syntax_param(&f)`** — hook na cabeça do laço de `parse_params`, antes de
     `type_of_token`, contrato de `syntax_lit`: `i64 f()` devolve um `N_PARAM` ou 0. Mais
     **`p_decl_name()`/`p_set_decl_name()`** (a que declaração o parâmetro pertence).
     **Desbloqueia o C6** (default em função de topo: gravar o default na declaração,
     completar no sítio por `pass()` + `decl_find` — a prova está em `lib/user_syntax_demo.mc`)
     e permite o `params` como palavra ensinada na declaração (hoje: `type_new` + pass).
     Guard novo: handler que **consome tokens e devolve 0 é recusado** (`tests/err/073`).
  2. **`syntax_infix` sobre operador do core FUNCIONA** (`ops_init` lazy; `syntax_infix` o
     chama primeiro); a precedência do módulo vence; `#infix` de fonte ainda derruba o
     handler; duplicata recusada. **Supersede o §1.3**: o C5b pode escolher entre
     `syntax_infix` (parse-time, tipa pelo oráculo do sítio) e o `pass()` sobre
     `N_BINARY` (já existe e resolve pelo tipo dos dois operandos). Preferir o **pass**
     (tem a regra de endereço do §12 e vê os dois tipos); registrar a escolha no C5b.
- **0.11.0 = M40**: AVR bare-metal, `uptr = 2` por `type_set_width` — não afeta o ngen.
- **0.12.0 = M42 (PR #19)**: **`--exe` em todo Linux sem `[linker]` e sem sysroot**
  (`elf-exe`/`elf-exe-x86_64`, dinâmico com musl/glibc por `[target].interp`/`.libc`).
  → as pernas Linux do CI podem dispensar `[linker] cc` (mini-crumb de CI; manter `cc`
  até medir que o `--exe` dinâmico roda no runner — o PR testou alpine e ubuntu 26.04).
**Fila (revista):** membros C# (em verificação) → propriedades/interface v2 → `abstract`/
`partial class` → reclaim c/ ctor/dtor → C5b → **C6 (agora alcançável, `syntax_param`)** →
`while`/`for` → `namespace`/`import`/`using` → `const` → `switch` → closures/`ref`/`out`.

## 23. Respostas da sessão do mc (canal `mini_compiler/build/NOTICES-teko.md`, 2026-09-05)

**Canal:** `send_message` mc→teko nunca é processado (esta sessão está sempre com agente em
voo); o arquivo `build/NOTICES-teko.md` (gitignored, no repo do mc) é o canal mc→teko — **ler
ao começar cada lote**. teko→mc por `send_message` funciona.
- **`syntax_param`:** a guarda ("consumed tokens and returned 0") é no fim da cadeia —
  registrar **por último** o handler que reivindica. A metade que o hook não faz: `f(1)` é
  parseado pelo core; completar o default é `pass()` + `decl_find`/`decl_nparams`
  (`lib/user_syntax_demo.mc` faz o ciclo). → C6.
- **Escopo — alerta:** tabela linear com marca no parse (`lg_block`, `lang_stmt.mc:466`)
  **OU** escopo pela árvore num `pass()` — **nunca híbrido**; foi a causa dos dois bugs
  silenciosos. O ngen hoje tem os dois (pilha no parse + oráculo por bloco no pass), verificados
  coerentes — **risco registrado**: qualquer divergência entre os dois é bug; candidato a
  unificar (o pass como fonte única) quando o reclaim/RC entrar, que também é por escopo.
- **`open`/`int` em `i64`:** hazard latente (bits altos de retorno de 32 bits). **M45** traz
  `i32` + retorno com o tipo declarado → `extern i32` para funções C que devolvem `int`. Até lá:
  retorno descartado (é o que `surface_overload_free.tk` faz com `chmod`).
- **Fila do mc:** patch pós-M42 (`--interp=`/`--libc=`; **`[target].libc = gnu|musl`** e
  `link = dynamic|static`) → trocar as pernas Linux do CI para `libc = "gnu"` quando sair;
  M45; M43 (sandbox); **M44 (pacotes estilo Go): o ngen seria o primeiro pacote "módulo de
  compilador"** — `[package]` com `lib`/`module`; regra: **um pacote nunca define `user_init`,
  exporta `<nome>_init()`** (`docs/specs/M44.md` §6 + emenda). Desenhar o `ngen` para virar
  `teko_init()` exportado.
- **Sem 1.0.0 sem coordenação com o ngen** — regra do dono.
- **`region crosses a file boundary`:** restrição de desenho com motivo (um `#include` dentro
  da região gravada muda o buffer; suportar exigiria copiar bytes com o include expandido e
  perder atribuição por arquivo). **Contorno certo = gravar a instância no arquivo declarante e
  replayar de lá.** Se `partial class`/genérico importado virar bloqueio real, mandar o caso.
- **`p_cp()`:** entra no lote do M45; até lá, acesso direto ao `cp` com comentário "temporário"
  (`teko_access.mc:253`).

## 24. O ngen como pacote do mc (M44, futuro) — forma já definida

`docs/specs/M44.md` §6: o pacote teko é do tipo **"both"**, como `<float>` — `[package]`
com `files`, `lib = "rt.mc"` (o que um PROGRAMA inclui) e `module = "teko.mc"` (o que um
COMPILADOR inclui, que **exporta `teko_init()` e nunca define `user_init`**; o projeto
consumidor escreve as seis linhas do `user_init` chamando `teko_init()`). Precedente:
`lib/user_float.mc`. Crumb quando o M44 sair: (1) `teko.mc` passa de `user_init()` para
`teko_init()` exportado + um `user_init` mínimo no projeto `ngen/` (o CI continua igual);
(2) `mc.toml` do `ngen/` ganha `[package] name = "teko"`, `files`, `lib`, `module`;
(3) regra do M44: todo arquivo lido sob a raiz do pacote tem de estar em `files`. Sem
mudança de superfície. Coordenar a numeração com o mc (sem 1.0.0 sem o ngen).

## 25. D225 — o rumo: auto-hospedagem da teko via re-arch do mc (M41)

Não é entrega agora; é o **destino** que ordena as entregas. Etapas, cada uma sem
mudança de superfície: (1) **recriar** o compilador teko das partes do `<mc/core>`
(`core_min` + as máquinas/writers que os alvos do CI usam), como `examples/avr` e o
`check-parts.sh` fazem — `[compiler].core` próprio em vez do bundle inteiro, medindo o
tamanho; (2) `subcommand("build", …)` → `teko build` como driver, `type_disable`/
`intrinsic_disable` para o que a teko redefine (candidatos: os tipos que a teko trata por
`type_new`); (3) M44: pacote `teko` com `teko_init()`; (4) **auto-hospedagem**: reescrever
`teko_*.mc` em teko (`.tk`), compilar com o `mc-teko` atual → `teko1`, com `teko1` → `teko2`,
`teko2` → `teko3`, `cmp teko2 teko3` byte-idêntico — o mesmo rito de bootstrap do mc, e o
único fixpoint que importa daqui para a frente (o `gen2==gen3` do `src/` congelado morreu com
o D211). Pré-requisitos de superfície para (4): tudo que os módulos `.mc` usam hoje — ponteiro
de função/`&fn`/`callp` (D221), `ref`/`out`, arrays globais, `#define`/`const`, `#include`,
`extern`, `switch` (D222), closures — logo a entrega 5 é, na prática, a lista do que a teko
precisa para escrever o próprio compilador.

## 26. Roadmap do mc até o 1.0.0 (da sessão do mc, `NOTICES-teko.md`, 2026-09-05)

Ordem: **(1) patch pós-M42** (0.12.x, em implementação): `[target].libc = "gnu"|"musl"`,
`[target].link = "dynamic"|"static"`, `--interp=`/`--libc=` → **trocar as pernas Linux do CI
para `libc = "gnu"`** quando sair. **(2) M45 `i32`** (0.13.0, em implementação): `i32` pelo
core via `type_new("i32", 4, 4, TK_SINT)` antes do `user_init`; kind **`TK_SINT`** (sinal por
kind: `type_new("i16", 2, 2, TK_SINT)` de um módulo ganha tudo); retorno de chamada e `return`
estendidos pelo tipo DECLARADO; `c_int()`; **`p_cp()` público** → no ngen: `extern i32` para C
que devolve `int`; `TK_SINT` nos inteiros assinados próprios; trocar o acesso ao `cp` por
`p_cp()`. **(3) M43 sandbox** (spec ratificada). **(4) M44 pacotes** (spec ratificada).
**(5) M46** linker estático de `.a` (candidato). **(6) M33 wasm** (último). Entre 4 e 1.0.0 só
correções; **1.0.0 = decisão do dono conosco**.

**M44 — o que vem de lá (NÃO desenhar do lado da teko):** identidade por nome de registro +
tag `vX.Y.Z`; `[deps]` = mínimo (MVS do Go); `#include <pack/lib.mc>`/`<pack>` resolvido por
lock → bundle → pacote `mc` (nunca o cwd); `mc.lock` com hash de conteúdo (dirhash sobre
`mc.toml` + `[package].files`; a lista é a fronteira — arquivo lido fora dela é erro); registro
`minicompiler/mc-registry` por PR; fetch por tarball da tag (`curl`/`wget`), `deps/` vendoring,
`[replace]`; comandos `mc pkg sync|add|list|vendor|verify|hash|check`, `mc install|update|
upgrade` (com `--yes`); binário `mc-slim`; `mc --version`. **O que é da teko:** o sistema de
pacotes da PRÓPRIA teko (imports/namespaces da linguagem), a forma do `teko_init()`, o
`user.mc` do compilador teko. O ngen = pacote "ambos" (`module = "mc_teko.mc"`,
`lib = "teko_rt.mc"`).

**Auto-hospedagem — já possível hoje (M41):** `mc build` com `[compiler].core =
"<mc/core_min>"` (+ partes) e `modules = ["<teko/mc_teko.mc>", "user.mc"]`; fixpoint
`teko2 == teko3` pelo protocolo do `scripts/bootstrap.sh` do mc (`cmp` dos objetos +
`--dump-asm` diff vazio). Com o M44 vira pacote pinado. O 1.0.0 dá a promessa de superfície
estável, não a capacidade. → **Crumb "compilador teko de `core_min`"** entra na fila da
entrega 5 (independente dos construtos): medir tamanho e provar que o CI de 5 pernas passa
com o core mínimo + as partes que os alvos usam.

**§21 — D224 pronto (`feat/ngen-abstract-partial` @ `65654014`, em verificação):** `abstract`
como C# (membro abstrato ocupa slot de vtable sem corpo; derivada concreta sem `override` é
erro nomeando propriedade e acessor); **`partial class` fecha no primeiro USO** (`new`, ou
derivação) ou no fim da unidade por um pass à frente dos demais — parte depois do uso é
erro claro; membro não precisa do fecho (nome nu resolve no pass). Pré-requisito feito:
tabelas por **posse** (`fd_cls`, `vs_cls`, `ci_cls`) em vez de fatias — tipo declarado
entre duas partes corrompia o layout em silêncio. Base só numa parte antes de membros;
interfaces em união livre. `partial` genérica em dois arquivos funciona (grava por parte no
arquivo declarante). **Defeito pego só pelo CI:** com config em caminho ABSOLUTO,
`tk_origin_of_file` diz "fora do projeto" para tudo e **nenhuma checagem de `internal`
dispara** — a validação local fica cega. **Regra: validar sempre com config RELATIVO e cwd
no repo** (o laço do coordenador já é assim). O `region crosses a file boundary` é
pré-existente: dispara quando a declaração gravada é a última coisa de um arquivo
incluído (`nopen` antes/depois); contorno `;` — reportado ao mc.

**§14/§15 — reclaim pronto (`feat/ngen-reclaim` @ `6212ab86`, em verificação; D227):** RC e
posse **só no pass** (`teko_rc.mc`, `tk_rc_pass` por último); `TK_VT_FIXED 2`; `refcount@+8`
reservado de fato (4 fixtures mudaram números de layout); ctor `public Nome(params)` com
`: base(args)`, `~Nome()`; release derivada→base→campos→`rt_free`; `rt_park`/`mark`/`sweep`
para valor possuído sem dono; 3 bugs fechados nas probes (marca de store no nó descartado
pelo `.` deferido → contagem negativa; possuído sem dono vazava; `decl_find` por nome errava
posse com sobrecarga). 23/23; nodes +3%; 1M `new` em 0,03 s com `rt_peak() <= 4096`.

**§26 — avisos do mc (2026-09-05, madrugada):** (1) patch pós-M42 pronto (release 0.12.x):
`[target].libc` vira família `"gnu"|"musl"` e a grafia soname é **recusada** → o CI do ngen
escolhe a grafia pela versão resolvida (`sort -V` vs 0.12.1); `[target].link`; flags. (2)
`region crosses a file boundary` no fim de arquivo incluído: **falso positivo confirmado** —
`p_skip_balanced` calcula `e` e só então chama `next()`, cujo lookahead fecha o arquivo; o fix
(decidir pelo frame do token de fechamento) entra no lote do M45. O `;` segue como contorno.

## 27. C5b landado — operadores como C#, resolvidos pelos dois operandos (2026-09-05)

Fecha o "C5 está ERRADO e vai ser refeito" do §15 (D218). Branch `feat/ngen-operators-cs`.

**Declaração.** `public static T operator<op>(A a[, B b])` em `class` e em `struct`:
membro **estático**, sem `this`, sem slot de vtable. Binários `+ - * / % == != < <= > >=
& | ^ << >>`; unários `- ! ~` — e `+`, aceito na declaração, mas **sem sítio**: o core
registra só `- ~ ! &` como prefixo (`mc/src/parse.mc` `ops_init`) e não há hook
`syntax_prefix`, logo `+v` é `expression expected`. Dívida do lado do mc, registrada.

**Resolução pelos DOIS operandos** (`teko_ops.mc`, tabela `op_mi/op_cls/op_tok/op_np/
op_t0/op_t1`): os candidatos são os operadores declarados pelo tipo de **qualquer**
operando e pelas **bases** dele; pelo menos um parâmetro tem de ser do tipo declarante.
Três rodadas, nessa ordem — **exata**; **literal** (a do C4: `N_INT` cai em `i64` na 1ª e
em qualquer inteiro do core na 2ª); **base** (operando de tipo DERIVADO num parâmetro da
base). A 3ª é uma precisão do "casamento exato" do ruling, e não uma folga: um objeto
derivado JÁ é um da base (campos base-first), a conversão é de zero bits, C# faz o mesmo,
e sem ela um operador herdado ficaria declarável e inalcançável. Como é a ÚLTIMA rodada, o
tipo que declara o seu próprio sempre vence. Duas declarações na mesma rodada = ambiguidade
recusada, **exceto na rodada base**, onde `tk_op_pick_best` aplica o desempate "ancestral
mais próximo" do C# (§12.6.4): entre `GrandBase`/`MidA`/`Kid`, `Kid + 2` escolhe o operador
de `MidA` por distância na cadeia de `base`; ambiguidade entre bases não-relacionadas segue
recusada (branch `feat/ngen-ops-nearest`).

**Pares obrigatórios** no `pass`, quando a unidade fecha — assim `partial class` escreve as
duas metades em partes diferentes. **Visibilidade no sítio** (`tk_check_member`): era o
achado 3 do crumb de membros, que o C5 não checava.

**A rota continua sendo o `pass()`**, apesar de o `syntax_infix` sobre operador do core ter
passado a funcionar no 0.10.3 (M41.5, §22): no parse o tipo de um operando que é parâmetro
ou `.` deferido não existe, e o handler não veria a regra de endereço do §12. Os dois
motivos ficam no cabeçalho do `teko_ops.mc`.

**Posse (D227):** o `tk_xt_put` do pass é o que diz ao `teko_rc.mc` que um operador com
retorno de classe entrega referência do caller. Medido com `rt_live()`: `(a+b)==c` não muda
a contagem (o temporário é parked/sweeped com a statement), `-a` e `2+a` sobem 1 cada, e a
saída do bloco volta a 0.

**Adjacente medido:** `p.side + 1` (campo `i64` de receptor teko) NÃO colide com a resolução
por dois operandos — todo endereço que o ngen constrói tem o valor teko à ESQUERDA e um
deslocamento à direita, e o marcador de endereço o pega antes de `tk_ops_binary`. O caso
novo (core à esquerda, teko à direita) não existe entre os nós que o projeto emite.

23/23; a AST das 22 fixtures que não usam operador é byte-idêntica à de `05dc7181`;
`mc limits` verdict `ok`.

## 28. C6 landado — default de parâmetro em função de topo, decisões (2026-09-05)

`syntax_param` (mc 0.10.3) desbloqueou o C6: `i64 add(i64 a, i64 b = 10)`, `add(1)` →
`add(1, 10)`. Módulo novo `ngen/teko_default.mc`; nada tocado em `ngen/mc.toml` nem no
`src/` do mc.

**Ordem do pass — decisão e porquê.** O crumb pedia para decidir se o preenchimento roda
antes do `tk_over_pass` ou dentro dele, e registrar o motivo: as DUAS coisas, cada uma na
metade certa.
- **Antes** (`tk_default_pass`, pass novo, registrado logo depois de `tk_ops_pass` e antes
  de `tk_over_pass`): resolve sozinho todo nome declarado EXATAMENTE UMA VEZ na unidade —
  não há tipo a comparar, só aridade contra a tabela de defaults, e a resposta nunca
  depende de outra declaração.
- **Dentro** (`tk_ov_fits_default`/`tk_ov_match_default`, uma QUARTA rodada em
  `tk_ov_resolve`, tentada só depois das duas de aridade exata falharem): um nome com MAIS
  de uma declaração é uma pergunta sobre TODAS as assinaturas ao mesmo tempo — só
  `tk_over_pass` tem a tabela de tipos dos candidatos. Tentar a rodada de default DEPOIS
  das duas exatas é o que dá de graça a regra do C# (§12.6.4.5): `add(1)` com `add(i64)` e
  `add(i64, i64 = 10)` resolve pela primeira, porque ela já venceu na rodada exata antes de
  a tabela de defaults ser sequer consultada — nunca há empate a desempatar.
- `tk_default_pass` DEIXA intocado todo nome com mais de uma declaração (contagem por
  varredura de `root`, não pela tabela de linhas — ver achado abaixo), justamente para não
  competir com a quarta rodada.

**Achado 1 — a contagem de "declarado uma vez" não pode ser pela tabela de parâmetros.**
Primeira versão contava linhas da própria tabela de `syntax_param` (uma por declaração com
≥1 parâmetro). Quebrou `surface_overload_free.tk`: `tally()` (aridade zero, nunca aciona
`syntax_param` — `parse_params` nem chama o handler quando o primeiro token já é `)`) ficava
INVISÍVEL para essa contagem, e a chamada `tally()` era preenchida contra o default de
`tally(i64 k)` por engano, virando `teko: tally takes at least 1 arguments`. Corrigido com
`tk_default_decl_count`, uma varredura de `root` contando toda declaração cujo nome bate
(`decl_valid` + `str_eq`), igual ao que `teko_over.mc` já faz para o seu próprio censo.

**Achado 2 — a quarta rodada não pode casar pelo `nd_name` do nó.** `tk_ov_rename` sobrescreve
o `nd_name` de CADA declaração (para o símbolo com sufixo) ANTES de `tk_ov_resolve` rodar
sobre qualquer chamada. Uma primeira versão de `tk_ov_fits_default` procurava a linha da
tabela de defaults por `nd_name(d) == fpd_name_at(i)` — comparação por IDENTIDADE de
ponteiro, que fazia sentido para o nome ORIGINAL mas não sobrevive ao rename (o ponteiro
mudou). Sintoma: uma sobrecarga genuína que precisava da quarta rodada (`foo(Vec v)` /
`foo(i64 a, i64 b = 9)`, chamando `foo(3)`) errava com `teko: no overload of foo matches
these arguments` mesmo com a rodada logicamente correta. Corrigido trocando a chave por
`od_name_at(i)` — o nome que `tk_ov_collect` guarda no INSTANTE da coleta, antes do rename
tocar o nó — e expondo `tk_default_ndef_of_name`/`tk_default_d0_of_name` (por NOME, não por
nó) em `teko_default.mc` para esse uso.

**Reuso, não duplicação.** `tk_param_default(mark)` (fold-para-constante, "deve ser
constante", "sem default depois de default", com as mesmas mensagens) e
`tk_fill_defaults(args, na, np, nreq, d0)` (append das constantes clonadas) são de
`teko_class.mc`, usadas como estão — a função de topo é só mais um chamador da MESMA tabela
`df_node`/`tk_ndflt` que método, construtor e assinatura de interface já usam.

**`p_decl_name()` distingue membro de função livre sem `p_set_decl_name` do C1.** Não
precisou de nenhuma coordenação: `tk_params` (membros, em `teko_class.mc`) tem laço PRÓPRIO
e nunca chama o `parse_params()` do core — é por isso que o C1 sequer precisou do
`syntax_param` — então o hook simplesmente nunca dispara para um parâmetro de membro. Zero
colisão, zero checagem extra.

**Recusas:** `params` com `=` no mesmo parâmetro, checado no PARSE (o handler já sabe que o
tipo é `tk_ty_params`); `extern` com qualquer default — checado por DECLARAÇÃO
(`tk_default_check_decls`, rodando sobre toda linha da tabela antes de olhar qualquer
chamada), não por chamada, porque um default nunca exercitado por nenhum call-site ainda é
uma recusa, não um default morto; `na < nreq` com a mensagem `teko: <fn> takes at least N
arguments`; e as duas regras herdadas do C1 (constante, sem-default-após-default), mesma
mensagem, mesmo código.

**O que NÃO coube:** nada. O crumb pedia para reportar se sobrou dívida — não sobrou nenhuma
das quatro combinações do escopo (função livre × sobrecarga × `params` × `extern`); os dois
achados acima foram bugs do PRÓPRIO trabalho, corrigidos antes de fechar, não dívida
deixada para depois.

Fixture nova `surface_default_free.tk` (1 e 2 defaults, chamada com 0/1/2/3 argumentos,
sobrecarga sem-default vencendo, chamada dentro do corpo de um método de classe). Gate:
24/24 em exit esperado; AST das 23 fixtures anteriores byte-idêntica à base `2af755e5`;
`mc limits` verdict `ok`.

## 29. `while`/`do`/`for` landados — rebaixamentos, rewrite de saltos, tokens (2026-09-05)

**Rebaixamentos** (`ngen/teko_loop.mc`, novo), literalmente os do crumb:
`while (c) stmt` → `loop { if (!(c)) break; stmt }`; `do stmt while (c);` →
`loop { loop { stmt break; } if (!(c)) break; }`; `for (init;cond;step) stmt` →
`{ init loop { if (!(cond)) break; loop { stmt break; } step; } }`. Os três nascem já em
`N_LOOP`/`N_IF`/`N_BREAK`/`N_BLOCK` do núcleo — nenhum pass a mais precisa saber que veio de
`while`/`for`, o `tk_rc_pass`/`tk_ops_pass`/`tk_typeof_pass` caminham a árvore igual à de um
`loop`/`if` escrito à mão.

**Rewrite de saltos** (`tk_loop_rewrite_stmt`, chamado só por `do`/`for` — `while` não precisa,
seu corpo já fica na profundidade que o programador escreveu): caminha o corpo ANTES de embrulhar,
contando `N_LOOP` do PRÓPRIO corpo entre ele e cada `break`/`continue`. Regra única, testada e
comprovada por indução na composição aninhada: `break k` com `k > profundidade` vira `break k+1`
(precisa ultrapassar o embrulho que este passe está prestes a acrescentar); `k <= profundidade`
fica intocado (já mira um loop que o PRÓPRIO corpo abriu, business as usual); `continue` na
profundidade 0 vira `break 1` (cai onde o `break;` do embrulho cairia — a condição/o passo);
`continue` em profundidade > 0 fica intocado. A dúvida que mais preocupou ao desenhar foi a
COMPOSIÇÃO: um `for` dentro de outro `for`, com `break 2` do usuário mirando os DOIS. Passo a
passo (fixture `surface_loops.tk`, bloco "nested"): o `for` interno aplica sua própria regra
primeiro (na sua própria chamada de `tk_for()`, que termina ANTES do `for` externo processar o
corpo dele), levando `break 2` a `break 3`; quando o `for` externo caminha ESSA árvore já
reescrita, ele vê o `break 3` na profundidade 2 (dois `N_LOOP` do `for` interno entre o topo do
corpo externo e o break) — `3 > 2` → vira `break 4`. `break 4` sai dos quatro `N_LOOP` nativos (os
dois pares um-tiro-mais-condição de cada `for`), que é exatamente sair dos dois `for`s por
completo, sem re-executar nada. A prova por `break N` (não sequencial: `language.md` §4, o
exemplo de dois `loop`s aninhados salta os DOIS de uma vez para `return s`, não um de cada vez) é
o que garante a composição: um `break` que o `for` interno já corrigiu para escapar DELE por
inteiro fica, para o `for` externo, "um break que já ultrapassa tudo que EU abri" — e ganha só
mais um `+1`, nunca dois. Confirmado rodando a fixture: `innerHits=1`, `outerHits=0` (o `for`
externo nunca chega a incrementar `outerHits`, prova de que o `break` saiu por completo antes da
1ª iteração terminar).

**Tokens de `++`/`--`/`+=`/`-=` — opção escolhida e por quê.** O crumb pedia (A) `syntax_infix`
devolvendo o nó de atribuição direto, ou (B) empurrar o `#token`+`#rule` do próprio
`lib/prelude.mc` por `p_push_source`. Nenhuma das duas é exatamente como o crumb descreveu, e o
motivo de desviar de cada uma é a razão de escolher a combinação final:
- **(A) puro não funciona:** `syntax_infix` roda em POSIÇÃO DE EXPRESSÃO — o nó que o handler
  devolve tem que ser uma EXPRESSÃO válida, e `N_ASSIGN` só é tratado no dispatch de STATEMENT
  (`gen_walk.mc`/`gen_resolve.mc`, ao lado de `N_LOOP`/`N_IF`/`N_BREAK`) — nunca no avaliador de
  expressão. Devolver um `N_ASSIGN` ali quebraria a passagem por `N_EXPRSTMT`. A ÚNICA forma de
  fazer (A) funcionar seria sintetizar `st64(&x, ld64(&x)+e)` (ponteiro cru), o que É uma
  expressão válida — mas perde a semântica: bypassa `operator+` de uma classe (C5b) e o RC de `x`
  (`teko_rc.mc` só reconhece `N_ASSIGN`, não uma chamada a `st64`), regredindo exatamente a
  garantia que D197 pede para primitivas de bypass. Registrar aqui por que (A) foi descartada, não
  só que foi.
- **(B) puro (`#token` PRÓPRIO na string empurrada) é redundante:** `word_add("+=")` (a MESMA
  chamada que `syntax`/`syntax_stmt`/`syntax_infix` fazem por baixo) já registra o token — chamar
  de novo dentro de um `#token` na string empurrada seria um segundo registro do mesmo lexema
  (inofensivo, `tok_add` deduplica por texto, mas sem propósito). E `word_add` sozinho NÃO chega
  na forma solta `x += e;`: passado o `ident = expr` do núcleo, o único fallback que
  `parse_stmt_core` tenta é `rule_find` no PRÓXIMO token — que só um `#rule` de verdade povoa.
- **A combinação landada:** `word_add` registra os quatro tokens (sem `#token`), e SÓ o texto das
  quatro linhas `#rule stmt: ...` de `lib/prelude.mc` é empurrado por `p_push_source` a partir de
  `tk_loop_init()` (chamado de `user_init()`). Isso funciona porque `drv_parse` (`driver.mc`) chama
  `lex_init(entry)` → `user_init()` → `parse_unit()`, NESSA ORDEM — o push acontece DEPOIS do
  arquivo de entrada já estar empilhado mas ANTES do primeiro `next()` de `parse_unit()`, então o
  texto empurrado é o PRIMEIRO a ser lido, processado como `#rule`/diretiva, e o lexer volta
  sozinho ao arquivo de entrada ao esgotar (a mesma semântica de `#include` que a doc de
  `p_push_source` promete). Medido: as quatro linhas landam como `#rule`s de verdade (confirmado
  pelo próprio `x += 3;`/`z++;`/`z--;`/`z -= 2;` da fixture rebaixando para `x = x + e;` e
  passando pelo `operator+`/RC genéricos, não por um caminho especial). O passo de `for` (`i++`
  etc.) NÃO passa pelo `#rule` — lê os mesmos tokens diretamente (não há `;` de fechamento ali
  para o `#rule` casar) e constrói o `N_ASSIGN` à mão, reaproveitando os MESMOS `tk_plus_tok`/
  `tk_minus_tok` que `word_add` já expôs.

Gate: `rm -rf ngen/build` + build do zero limpo; laço `--entry-only` 25/25 em exit esperado
(24 antigas + `surface_loops.tk` nova); AST das 24 fixtures anteriores **byte-idêntica**
(`diff -rq` vazio); `mc limits ngen` verdict `ok` (o `ld: unknown file type` que o mesmo comando
imprime depois é pré-existente — o `[target]` de `ngen/mc.toml` mira linux/x86_64 e o link do
`.o` ELF falha em QUALQUER host macOS, com ou sem este crumb; confirmado reproduzindo no commit
base antes da mudança). Cinco probes de recusa fora de `ngen/tests/`: `while` sem parênteses →
`expected ( after while`; `for` com um `;` faltando → `expected ; after for condition`;
`i64 while = 1;` → `name reserved by a syntax/type_alias registration: while`; `break 3` além de
um único `for` → `break out of range` (checagem do NÚCLEO, na compilação completa — não aparece
em `--dump-ast`, só no passe de resolve/codegen); variável do `init` usada depois do `for` →
`unknown name` (mesma checagem de escopo léxico que qualquer bloco já tem, porque o `for` é um
`N_BLOCK` de verdade — nada de tabela de escopo própria precisou ser ensinada).

## 30. mc 0.13.0 (M45) — `+` unário, `true`/`false`, `i32`/`p_cp()` (2026-09-05)

Três itens sem crumb próprio. **Adoção do 0.13.0**: `p_cp()` público troca a leitura crua de `cp`
em `tk_dot_follows`; `chmod` da `surface_overload_free.tk` vira `extern i32` (D5, uma chamada
devolve o que declara); nenhum contorno de "region crosses a file boundary" existia a remover.
**`+` unário** fecha a dívida do §27: `tk_unary_plus` (`syntax_expr("+")` + `parse_expr(11)`, a
precedência acima de `*`/`/`/`%`) devolve o mesmo `N_UNARY` do núcleo, `tk_ops_unary` resolve pelo
tipo e colapsa no próprio operando (`tk_ops_replace`) quando é um tipo do núcleo. **`true`/`false`**:
`N_INT` de 1/0 tipado `TY_I64` (como toda comparação, não `TY_U8`), reservados por `syntax_expr`.
Gate: 25/25; AST das 22 fixtures não tocadas **byte-idêntica** contra o compilador da base
`545b26b5` (0.13.0 dos dois lados); `mc limits` verdict `ok`.

## 31. `namespace` / `using` / `import` — desenho (architect-first, 2026-09-05)

Escopo: D218 ("`namespace`, açúcar sobre include; `using` ou `import`, tem exemplo pronto no mc"),
D215 (C-like), D220 (`internal` = dir do `mc.toml`), D226 (seguir C#), D227 (RC só no pass).
Precedente lido inteiro: `mini_compiler/examples/lang/lang_class.mc:568-666` (`lg_namespace`,
`lg_using`, `lg_ns_path`, `lg_import`, `lg_ns_register`), `lang_expr.mc:266` (`lg_ns_expr`),
`lang_type.mc:39` (`lg_resolve`), `lang_util.mc:33` (`lg_qualify`). A implementação é `.mc` — vale
o estilo dos 20 módulos do `ngen/` (cabeçalho `//`), não o Javadoc das leis de `.tks`.

### (a) Decisões

1. **`namespace A.B { … }` (bloco) e `namespace A.B;` (file-scoped, C# 10), as duas.** Reabertura
   = merge (é só um prefixo; não há nada a fundir). **Namespace aninhado em namespace: NÃO** —
   `A.B` já dá a mesma árvore com um handler linear; aninhar pediria pilha de prefixos por nada.
2. **Qualificado resolve por `syntax_stmt`/`syntax_expr` do 1º SEGMENTO, nunca pelo `tk_dot`.**
   `tk_dot` é infixo: só roda depois de um operando esquerdo, e `geo` não é valor nem tipo —
   ensiná-lo ao `.` exigiria um nó "sou namespace" (modelagem que o D217 recusa) e colidiria com a
   lógica de receptor. O handler consome `geo . Circle` inteiro antes de o laço de Pratt ver o `.`,
   então `tk_dot_follows` e o `Shape.made` estático ficam **intocados**: o namespace nunca chega a
   `tk_type_stmt`. `geo.Circle.made + 1` funciona porque `tk_static_member` consome `. made` e a
   Pratt retoma depois.
3. **Segmento de namespace é lido com `p_name()`+`p_next()`, jamais `p_ident()`** — o 1º segmento
   vira palavra reservada no registro e `p_ident()` exige `T_IDENT` (é o que `lg_declname` faz).
4. **Nome REAL = `A__B__Circle`**, separador `__`, namespace primeiro (precedente `lg_qualify`).
   `sr_name` guarda o nome cheio, então todo símbolo derivado (`A__B__Circle_new`, `_vt`, `_itab`,
   `A__B__Circle_area`) já sai prefixado sem tocar em emissor nenhum. `$` foi considerado (prova de
   não-colisão trivial) e **descartado**: exigiria provar `$` nas três tabelas de símbolo (ELF/
   Mach-O/COFF) e no `--dump-asm` das 5 pernas do CI, custo que a guarda de (c) evita.
5. **O nome CURTO de um tipo em namespace NÃO é `type_alias`.** É registrado como palavra com
   handlers do ngen nas três posições (`syntax` de topo, `syntax_stmt`, `syntax_expr`) e a
   identidade sai da lista de busca do ngen, no sítio. Causa-raiz: `alias_add`
   (`mc/src/hooks.mc:456`) é append-only e `alias_find` devolve o ÚLTIMO — dois `Circle` em dois
   namespaces dariam id errado **em silêncio**. Sem alias, `type_of_token` nunca responde pelo
   curto e o ngen decide sempre. É o que entrega o ponto REAL do namespace: `geo.Circle` e
   `mesh.Circle` coexistem.
6. **Lista de busca (C#):** do namespace corrente para fora, prefixo a prefixo (`A.B` → `A__B__X`,
   `A__X`, `X`), e só então os `using` do ARQUIVO. Declaração do nível vence `using` (é a ordem);
   dois `using` no mesmo nível → `teko: ambiguous name X (a, b)`. `using` e file-scoped são
   **por arquivo** (`nd_file`/`p_file()`, comparados por `str_eq`) — a semântica do C#.
7. **Posição de DECLARAÇÃO nunca usa a lista de busca:** qualifica com o namespace corrente e
   procura EXATO (`tk_struct_find(tk_ns_qualify(nome))`). Vale para o reabrir de `partial`
   (`teko_class.mc:1321`), senão uma parte escrita em outro namespace reabriria a classe errada.
8. **Função livre em namespace existe e é manglada** (`geo.area` → `geo__area`), por um `pass()`
   novo — o core parseia a declaração e o sítio de chamada, e só um pass vê a unidade inteira.
   `main` dentro de namespace é **recusado** (`teko: main is declared outside every namespace`);
   `extern` em namespace é **recusado** (mantém o ABI do C, mesma regra do `teko_over.mc`); global
   de topo em namespace é **recusado** (dívida, (e)).
9. **Renomear só o que ainda não está qualificado.** O pass pula toda declaração cujo nome já
   começa por um prefixo de namespace declarado — é o que impede `geo__Circle_new` (gerado pelo
   ngen a partir de `sr_name`) de virar `geo__geo__Circle_new`.
10. **Reescrita de chamada só quando o candidato qualificado EXISTE** (`decl_find >= 0`). Um
    `rt_alloc` dentro de corpo gerado num arquivo com namespace é sondado como `geo__rt_alloc`, não
    acha e fica plano. Nenhuma falha nova é silenciosa: o que não resolve já era erro de link.
11. **`import A.B;` = `lex_include("A/B.tk")` + `using A.B` implícito**, once-only (o `lex_seen` do
    `lex_include`, `mc/src/lex.mc:635`), relativo ao includer e depois a `[include].paths`. O
    `#include "x.tk"` cru **continua existindo** (as fixtures o usam para `../lib/rt.mc`): `import`
    é açúcar, não a única porta. Contrato do lookahead: chamar `lex_include` com o `;` ainda
    corrente e só então `p_next()` (`lg_import`, `mc/docs/surface.md:1204`).
12. **`internal` (D220) não muda uma linha.** `tk_origin_of_file` (`teko_access.mc:93`) decide por
    prefixo do dir do `mc.toml`: arquivo importado de dentro de `ngen/` é projeto; de um root de
    `[include].paths` fora dele (absoluto ou `../`) é externo. Verificado no código, não presumido.
13. **`using`/`import` só no topo, antes de qualquer namespace** — dentro de um bloco são recusados
    (o `using` é do arquivo; aceitá-lo aninhado prometeria um escopo que não temos).
14. **Instanciação de genérico qualificada (`geo.Box<T,4>`) é recusada** com mensagem: o nome curto
    do genérico resolve pela lista de busca e é a forma ensinada. Dívida barata, risco zero.

### (b) Hook → uso

| hook / API | uso |
|---|---|
| `syntax("namespace"/"using"/"import", &fn)` | substituem os três honest-stops de `teko.mc:155-157` |
| `parse_top()` + `top_add()` | corpo do bloco (precedente `lg_namespace`); `parse_top` devolve 0 quando um `syntax` já fez `top_add` |
| `do_directive()` | `#include`/`#define` dentro do bloco (o laço do `lg` não trata: seria erro cru) |
| `lex_include(path, line)` | o `import` |
| `type_new(cheio)` | identidade do tipo + a palavra `A__B__Circle` (inerte, como no `lg`) |
| `syntax(curto, &tk_ns_top)` | `Circle f(Circle c)` no topo — a ÚNICA posição de tipo que o core lê sem hook |
| `syntax_stmt(curto)` / `syntax_expr(curto)` | `tk_type_stmt`/`tk_type_expr` já existentes, agora com a lista de busca |
| `syntax_param` (o de `teko_default.mc:61`, um só) | parâmetro tipado pelo nome curto; ramo novo ANTES do de default |
| `syntax_stmt(seg0)` / `syntax_expr(seg0)` | `geo.Circle c = …;` / `geo.f(x)` / `geo.Circle.made` |
| `parse_params()`/`parse_function()`/`p_set_decl_name()` | o handler de topo do nome curto |
| `pass(&tk_ns_pass)` | mangle das funções livres + resolução do não-qualificado |
| `decl_find`/`decl_valid`/`nd_name`/`set_nd_name`/`nd_file` | o pass |

### (c) Mangling e prova de não-colisão

Formas geradas hoje: (1) `S`, identificador da fonte; (2) `X_m`, membro/estático/vt; (3) `X__T…`,
sufixo de sobrecarga (nomes de tipo) e de genérico (lexemas dos argumentos); (4) `f__k`, instância
de `params` (k é dígito). Nova: (5) `NS__S`, com `NS` = segmentos juntados por `__`.

(5) colide com (3) em princípio: `namespace A.B { class Circle }` e `A<B, Circle>` dão ambos
`A__B__Circle`; `namespace A { i64 f(…) }` e um `i64 A(f x)` sobrecarregado dão ambos `A__f`. Com
(4) não colide (segmento não é inteiro). A resposta **não é separador mágico, é a guarda**: toda
identidade gerada aterrissa ou na tabela de tipos ou na lista de declarações da unidade, e as duas
são consultáveis — `tk_type_add` recusa uma segunda linha com o mesmo nome cheio, e o passo de
rename recusa quando `decl_find(cheio) >= 0`. Logo **nenhuma colisão é silenciosa**: vira
`teko: the generated name is already declared: A__B__Circle`. Um nome de fonte escrito literalmente
como `geo__area` é tratado como já-qualificado (D31.9) — consequência aceita do `__`.

### (d) Crumbs

**N1 — `namespace` + `using` + tipos qualificados** (branch `feat/ngen-namespace`).
Arquivos: `ngen/teko_ns.mc` (novo), `teko.mc` (include + 3 `syntax`), `teko_struct.mc`
(`tk_struct_find:425` ganha o fallback; `tk_newname:698` aceita palavra que é nome curto de tipo em
namespace), `teko_access.mc` (`tk_type_word:379`, `tk_type_expr:281`, `tk_type_stmt:310`),
`teko_class.mc:1310` / `teko_iface.mc:460` / `teko_trait.mc:178` / `teko_struct.mc:859` /
`teko_generic.mc:254` (um `tk_ns_qualify` no nome declarado), `teko_generic.mc:495` (`tk_gen_ty`),
`teko_expr.mc:46` (`tk_new` lê nome qualificado), `teko_default.mc:61` (ramo de parâmetro).
Assinaturas novas (`teko_ns.mc`): `uptr tk_ns_current();` · `uptr tk_ns_qualify(uptr nome);` ·
`void tk_ns_add(uptr cheio);` · `i64 tk_ns_find(uptr cheio);` · `void tk_ns_register(uptr seg0);` ·
`uptr tk_ns_read_path(uptr pmem);` · `i64 tk_ns_resolve(uptr curto);` (lista de busca; −1 = nada,
erro em ambiguidade) · `void tk_ns_short_word(uptr curto);` · `i64 tk_ns_short_known(uptr curto);` ·
`i64 tk_ns_param_ty();` · `i64 tk_ns_top();` · `i64 tk_ns_stmt();` · `i64 tk_ns_expr();` ·
`void tk_namespace();` · `void tk_using();`. Extração sem mudança de comportamento:
`i64 tk_var_after_type(i64 ty, i64 line, uptr fl)` sai de `tk_gen_declstmt`
(`teko_generic.mc:507`) e serve aos dois. Fixture `surface_namespace.tk` (+
`ngen/tests/parts/ns_file.tk` para a forma file-scoped, trazido por `#include`, fora do glob): dois
namespaces com uma classe `Circle` cada, uso qualificado, `using geo;` deixando o curto resolver
dentro e fora, estático `geo.Circle.made`, reabertura do mesmo namespace, `A.B` aninhado;
`expect-exit: 42`. Gate: 26/26 no exit esperado; `--dump-ast` das 25 anteriores **byte-idêntico**;
`mc limits` `ok`; probes de recusa FORA de `tests/`: dois `using` ambíguos, `namespace` sem `{` nem
`;`, tipo curto sem `using` e sem qualificação, `extern`/global/`main` dentro de namespace.

**N2 — funções livres em namespace** (branch `feat/ngen-namespace-fn`). Arquivos: `teko_ns.mc`
(`i64 tk_ns_pass(i64 root);`, `uptr tk_ns_of_name(uptr nome);`,
`i64 tk_ns_rewrite_call(i64 n, uptr ns, uptr fl);`), `teko.mc` (`pass(&tk_ns_pass)` **depois de
`tk_partial_pass` e antes de `tk_params_pass`** — o nome tem de estar final antes de todo pass que
censa por nome), `teko_default.mc` (`void tk_default_rename(uptr velho, uptr novo);` trocando a
chave `fpd_name`, comparada por PONTEIRO em `tk_default_row_of_name:130` — sem isso o default de
uma função em namespace some). Duas varreduras, na ordem do `teko_over.mc`: renomeia todas as
declarações, depois resolve os sítios. Fixture `surface_namespace_fn.tk`: chamada qualificada,
chamada não-qualificada de dentro do próprio namespace, `using` resolvendo o curto, sobrecarga (C4)
e default (C6) sobre função em namespace, `main` no topo; `expect-exit: 42`. Gate igual ao N1
(27/27, AST das 26 idêntica) + prova de no-op do pass (AST idêntica quando não há namespace).

**N3 — `import`** (branch `feat/ngen-import`). Arquivos: `teko_ns.mc` (`void tk_import();`,
`uptr tk_ns_path_of(uptr cheio);`), `teko.mc` (troca do honest-stop). Fixture `surface_import.tk` +
`ngen/tests/parts/geo.tk` (este com `namespace parts.geo;` file-scoped): `import parts.geo;` duas
vezes (prova do once-only), uso pelo `using` implícito e pela forma qualificada, classe sem
modificador (`internal`) alcançada porque o arquivo está dentro do projeto; `expect-exit: 42`.
Gate igual + probe de recusa: `import` de namespace sem arquivo.

**Ritual (os três):** `rm -rf ngen/build` + build do zero, laço `--entry-only` com TODAS as
fixtures, `--dump-ast` byte-idêntico nas anteriores, `mc limits ngen` `ok`, e **config RELATIVO com
cwd no repo** (D224: config absoluto cega o `tk_origin_of_file` e nenhuma checagem de `internal`
dispara).

### (e) Fora do escopo (dívida) e pedido ao mc

Dívida, registrada e não escondida: cast para nome curto de namespace (`(Circle) x` cai no
`type_of_token` do core, que não responde pelo curto — a mensagem é a do core); global de topo,
`extern` e `main` dentro de namespace (recusados); `using` dentro de bloco; alias de `using`
(`using G = geo;`) e `using static`; genérico qualificado (D31.14); namespace aninhado (D31.1).

Pedido ao mc (texto pronto para enviar):
> O `ngen` precisa, para `namespace`/`import`, de três funções do core que o `examples/lang` já usa
> mas que não estão nas tabelas do `docs/reference/hooks.md` §4: `lex_include(path, line)`
> (`lang_class.mc:665`), `parse_top()` (`lang_class.mc:609`) e `do_directive()` (para `#include`
> dentro de um corpo que o módulo parseia). Pedimos publicá-las na doc de hooks — ou nomear a
> alternativa suportada. Achado adjacente: `alias_add` aceita registrar o mesmo lexema duas vezes e
> `alias_find` devolve o último, sem diagnóstico; um segundo `type_alias` do mesmo nome hoje troca
> o tipo de um programa em silêncio.

### (f) Riscos

1. **Colisão `__`** — mitigada pela guarda de (c); sem ela seria silenciosa. Reavaliar `$` se a
   guarda alguma vez disparar em código real.
2. **Ordem do pass do N2** — antes de `params`/`over`/`default`; errar aqui vira "wrong number of
   arguments" em vez de erro claro. A prova de no-op e a fixture de C4/C6 sobre namespace cobrem.
3. **`fpd_name` por ponteiro** (`teko_default.mc:124-135`) — o rename tem de gravar o MESMO ponteiro
   que foi para `set_nd_name`, senão a 4ª rodada do `teko_over.mc` erra.
4. **1º segmento vira palavra reservada program-wide** (`geo` deixa de ser nome de variável) —
   inerente ao `word_add` do mc, o mesmo do `lg`; documentar na fixture.
5. **Corpo de bloco sem ramo de `T_DIR`/`K_EXTERN`** — sem ele, um `#include` dentro do bloco morre
   com erro do core sem relação com a causa.
6. **`region crosses a file boundary`** (falso positivo conhecido, §23/§26) — pode aparecer se um
   genérico for a última declaração de um arquivo importado; contorno `;`, correção vem do mc.
7. **Escopo híbrido** (alerta da sessão do mc, §23): o namespace é decidido no PARSE para tipos e no
   PASS para funções. Não é a tabela de locais, são domínios disjuntos (tipo × declaração de topo),
   mas fica registrado: qualquer regra que precise dos dois ao mesmo tempo é sinal de erro.

**Tensões de lei:** nenhuma aberta. D215 × C#: `namespace`/`using` são forma C-like e o D218/D226
os nomeia — sem tensão. C# escopa nome por arquivo e o mc reserva palavra program-wide: resolvido
por D31.5 (o ngen é dono da resolução do nome curto), com o cast como única fresta, declarada. D217
(sem Variant): namespace é resolvido a NOME no parse, nada dinâmico. D227: o pass novo corre antes
do `tk_rc_pass`, que continua sendo o último.

## 32. N1 landado — `namespace`/`using` + tipos qualificados (2026-09-05, errata)

`feat/ngen-namespace`, módulo `teko_ns.mc` (novo). 26/26 em exit esperado; `--dump-ast` das 25
fixtures anteriores **byte-idêntico**; `mc limits` verdict `ok` (heap 520352→577696 B, dentro da
tolerância 1.00). Fixture `surface_namespace.tk` + `ngen/tests/parts/ns_file.tk` (file-scoped,
`#include`, fora do glob), cobrindo os 14 itens de (a) e as formas do (d).

**Desvios do desenho, medidos:**

1. **`tk_struct_find` não pôde virar diretamente "exact + fallback"** — um `tk_ns_resolve`
   chamando de volta `tk_struct_find` sobre uma string que ELE MESMO construiu (o prefixo de
   namespace, o candidato de `using`) recursa sem convergir: cada tentativa falha e o próximo
   candidato é maior, nunca repete o argumento anterior, então nunca bate a base da recursão —
   medido como `EXC_BAD_ACCESS` no topo da pilha (estouro de pilha) num `lldb bt`. Correção: o
   scan original vira `tk_struct_find_exact` (sem fallback), `tk_struct_find` passa a ser
   `exact-scan; se falhar, tk_ns_resolve`, e todo sítio INTERNO de `teko_ns.mc` que testa uma
   string que ele próprio montou (`tk_ns_try_prefixes`, o laço de `using` de `tk_ns_resolve`,
   `tk_ns_walk`) chama a versão exata. O mesmo troca em `teko_class.mc` (reopen check e
   `tk_class_reopen`) e `teko_generic.mc` (`tk_gen_close`/`tk_gen_struct`, que buscam por um
   nome já manglado) — nenhum desses precisa da lista de busca, só da resposta exata.
2. **`tk_type_stmt`/`tk_type_expr` (teko_access.mc) precisaram de um `if (si < 0) err_at2(...)`
   explícito**, não ficaram "unmodified" como a primeira leitura do (b) sugeria: com `Circle`
   um nome namespaced sem `using` nem qualificação, `tk_struct_find` agora PODE devolver -1, e
   sem o guard `sr_ty_at(-1)`/`tk_static_member(-1,...)` lia lixo fora da tabela em vez de
   reportar `teko: unresolved name` — é o que fecha a mensagem clara do probe "curto sem
   `using` nem qualificação".
3. **`tk_ns_seg_stmt` não podia reusar `tk_dot_follows`** (a peça que `tk_type_stmt` usa): essa
   função responde "um `.` segue o token ATUAL, ainda não lido" — certo quando o handler está
   sentado no PRÓPRIO nome do tipo, errado depois de `tk_ns_walk` já ter consumido `geo.Circle`
   inteiro, quando o token atual É o `.` ou já é o que vem depois dele. A checagem virou
   `p_id() != tk_ns_dot` (o parser está OU NÃO sobre um `.` agora). Sem essa correção o `.made`
   de `geo.Circle.made` era perdido silenciosamente e a leitura seguinte (`Circle` como se fosse
   o nome de uma variável) por acidente às vezes até compilava errado.
4. **`tk_new` precisou de uma segunda correção depois do `tk_ns_walk`**: o nome usado para
   montar o símbolo do alocador (`tk_new_pick`/`tk_ctor_name`) ficava o CURTO não-qualificado
   (`circle_new` em vez de `geo__circle_new`) quando a resolução vinha do fallback de
   `tk_struct_find` (namespace corrente ou `using`) em vez do `tk_ns_walk` explícito — o `si`
   resolvia certo, o texto do símbolo não. Corrigido lendo `name = sr_name_at(si)` assim que
   `si` é validado, antes de `tk_new_pick`. Invisível em código sem namespace (ali `name` já era
   `sr_name_at(si)` por construção).
5. **`teko_generic.mc:254` (`tk_gen_record`) NÃO ganhou `tk_ns_qualify`**, ao contrário do que a
   lista de toques do (d) sugeria: `tk_gen_find`/`tk_gen_declstmt` comparam pelo nome CURTO que o
   `syntax_stmt` carrega, e qualificar `gn_name` sem também reescrever essa busca quebraria o uso
   comum (não-namespaced) de generics. Como D31.14 já aceita "genérico qualificado é recusado"
   como dívida, um generic declarado dentro de um namespace continua registrado pelo nome CURTO
   simples, colidindo com o "duplicate generic" de hoje se outro namespace repetir o nome — dívida
   estreita, sem fixture que a exercite, registrada no cabeçalho de `tk_gen_record`.
6. **`main`/`extern`/global dentro de um namespace FILE-SCOPED não é pego** (só o BLOCO `{ }` é,
   via o laço que este módulo já possui): não há hook de "todo `parse_top` top-level" fora de um
   laço que o módulo mesmo controla, e ganhar um não é escopo do N1 (não registra `pass()`
   nenhum). Os probes usam a forma de bloco, que é pega. Fica para o `tk_ns_pass` do N2.
   **FECHADO pelo N2 (§33):** `tk_ns_scan_decls` varre `root` inteiro no `tk_ns_pass` e aplica o
   mesmo `tk_ns_reject_topkind` a todo nó cujo arquivo declarou um namespace file-scoped.
7. **N1b (2026-09-05): dois furos do verificador, ambos a mesma causa.** `tk_conf_name` (a lista
   `:`) e `tk_use` (o `use` de trait) liam o nome com um único `p_name()`/`p_ident()`, sem andar
   pelos segmentos `.` — o primeiro nunca via `geo.IShape` inteiro, o segundo nem sequer aceitava
   um nome namespaced (seu curto virou palavra reservada em `tk_ns_register`, e `p_ident()` exige
   `T_IDENT`). Corrigidos lendo por `tk_ns_read_path` (D31.3) e resolvendo bare pela lista de
   busca, qualificado por exato — `tk_conf_name` contra `tk_struct_find`, `tk_use` contra um novo
   `tk_trait_resolve` (a mesma busca de `tk_ns_resolve`, sobre a tabela de traits).

## 33. N2 landado — funções livres em namespace (2026-09-05, errata)

`feat/ngen-namespace-fn`, `teko_ns.mc` (`tk_ns_pass`, registrado logo depois de `tk_partial_pass`
e antes de `tk_params_pass`), `teko_default.mc` (`tk_default_rename`), `teko_class.mc` (o furo do
destrutor). 27/27 em exit esperado; `--dump-ast` das 26 fixtures anteriores **byte-idêntico**
(prova de no-op do pass quando não há função livre em namespace); `mc limits ngen` `ok`.

**Duas varreduras, uma tabela de site.** Sweep 1 (`tk_ns_scan_decls`) manglа toda declaração de
função livre/protótipo dentro de um namespace, bloco OU file-scoped, ANTES de qualquer sítio ser
lido; sweep 2 (`tk_ns_scan_calls`) resolve os sítios não-qualificados. O namespace de um BLOCO é
anotado no parse (`tk_ns_decl_note`, chamado no laço de `tk_namespace` sobre o nó que `parse_top`
devolveu, por identidade do nó — não por `nd_file`+linha, que a redação original do crumb sugeria,
mas o nó já é a chave exata que o resto do módulo usa) numa tabela nova (`nsb_node`/`nsb_ns`); o de
um FILE-SCOPED sai de graça de `tk_ns_file_get(nd_file(n))`. O de um SÍTIO (sweep 2) não precisa de
tabela nenhuma: por sweep 1 já ter rodado, o nome de toda função top-level namespaced já é o cheio
(`geo__area`), e `tk_ns_of_name` (novo, usado pelas DUAS pontas) extrai `geo` de volta por prefixo
— o `.` mais específico, não o primeiro que bater, para `namespace A` e `namespace A.B` coexistirem.

**`geo.area(x)` (qualificado) NÃO passa pelo pass.** `tk_ns_seg_expr`/`tk_ns_seg_stmt` do N1 só
resolviam tipo (`tk_struct_find_exact(acc)` bem-sucedido); estendidos com `tk_ns_qualified_call`:
quando `acc` é um namespace conhecido mas não um tipo, o token corrente (que `tk_ns_walk` já
deixou sentado exatamente sobre o nome da função, o `.` já consumido) é lido como identificador e
o `N_CALL` é montado com o nome cheio DIRETO, sem `decl_find` — a declaração pode vir mais abaixo
no arquivo, e mangling delas só acontece no pass; um `geo.nome` que não é tipo nem função vira
`unresolved qualified name`, nunca miscompila em silêncio.

**Achados que exigiram correção (mesma classe do §32):**

1. **Global sintetizado de uma classe namespaced apanhado pela recusa file-scoped.** A varredura
   unificada de sweep 1 passa por TODO nó de topo, incluindo o `_vt` global que `tk_class_close`
   emite bem depois do parse — e esse nó também é do arquivo namespaced. A guarda usa o MESMO
   `tk_ns_of_name`: só recusa um `N_GLOBAL` que ainda NÃO carrega prefixo de namespace (um global
   sintetizado já sai com o nome cheio, `tk_ns_qualify` correndo antes na declaração do tipo).
2. **A guarda de colisão de (c) não vale para `decl_find` em função.** Duas declarações de uma
   função namespaced com assinaturas diferentes (C4) aterrissam no MESMO nome cheio de propósito —
   é o que o `tk_over_pass` espera achar. `tk_ns_rename_decl` só recusa colisão contra a tabela de
   TIPOS (`tk_struct_find_exact`); `decl_find` fica de fora.
3. **O construtor JÁ estava correto** (`tk_gen_ty` → `tk_ns_param_ty` resolve o nome curto pela
   lista de busca antes de comparar com `sr_ty_at(ci)`, tipo contra tipo, não palavra contra
   palavra) — só o DESTRUTOR comparava `tk_word(name)` contra o nome QUALIFICADO
   (`teko_class.mc`, `tk_member_dtor`). Corrigido com `tk_ns_short_of` (novo, o inverso de
   `tk_ns_of_name`). A MESMA classe de bug estava latente no diagnóstico `void Name(...)` (C#'s own
   mistake) logo abaixo, também corrigida — um probe (`p7_void_ctor`, fora de `ngen/tests/`)
   confirma a mensagem certa em vez de aceitar `void Base(...)` como um método comum.

**Fixture** `surface_namespace_fn.tk` (`expect-exit: 42`): chamada qualificada (`geo.area`) e
sobrecarga C4 sobre função namespaced, chamada bare de DENTRO do namespace (`grow` chamando
`area`) e de FORA via `using geo;`, default C6 bare e qualificado, uma função namespaced chamando
uma PLANA bare (fica achatada, D31.10), e `Base`/`Derived` com construtor E destrutor pelo nome
curto mais `: base(v)`. **Probes de recusa** (fora de `ngen/tests/`): `main`/`extern`/global dentro
de namespace FILE-SCOPED; dois `using` com a mesma função ambígua (`teko: ambiguous name f (a,
b)`); chamada sem namespace nem `using` (erro do core, `call to unknown function`); chamada bare a
uma função do runtime (`rt_live`) de dentro de um namespace, achatada e ligada normalmente;
`void Name(...)` dentro de namespace.

## 34. N3 landado — `import` e o fecho da série namespace (2026-09-05, errata)

`feat/ngen-import`, `teko_ns.mc` (`tk_import`, `tk_ns_path_of`, `tk_ns_sep_replace`/
`tk_ns_dotted`, `tk_ns_file_saw_ns`/`tk_ns_mark_file_saw_ns`), `teko.mc` (troca do honest-stop),
`teko_class.mc`/`teko_trait.mc` (convenção de mensagem). 28/28 em exit esperado (`hello.tk` + as
27 do glob); `--dump-ast` das 27 fixtures anteriores **byte-idêntico** contra `5e401b01`; `mc
limits ngen` `ok`.

**`tk_import`** é sugar mecânico sobre o `lex_include` do core, no precedente exato de
`lang_class.mc`'s `lg_import` (`mini_compiler/examples/lang/lang_class.mc:657`): lê o caminho com
`tk_ns_read_path` (sem consumir o `;`), chama `lex_include` AINDA sobre o `;` (o contrato do
lookahead), só então `p_next()`, e adiciona a `using` implícita antes do include — o once-only é
inteiramente do `lex_seen` do core, nada de tabela própria. `tk_ns_path_of("A__B")` = `"A/B.tk"`,
via o mesmo scanner que converte "__" em um separador dado (`tk_ns_sep_replace`), reusado por
`tk_ns_dotted` (item 3 abaixo) trocando por `.` em vez de `/`.

**Posição — D31.13, "no topo, antes de qualquer namespace".** Dois guards: `tk_ns_current() != 0`
(dentro de um bloco de namespace aberto, mesma checagem que `tk_namespace` já faz contra
aninhamento) e uma tabela NOVA, `nsd_file`, que marca (por `p_file()`) todo arquivo em que
`tk_namespace` roda — bloco OU file-scoped, o import cheque contra ISSO, não contra um flag
global: um namespace declarado dentro do arquivo IMPORTADO é desse arquivo, nunca do importador,
então a fixture do once-only (duas `import parts.geo;` seguidas, cujo alvo declara seu próprio
`namespace parts.geo;`) não se autoderruba.

**Item 2, a dívida do verificador do N2 — `&f`/`&geo.f`.** `tk_ns_walk_calls_in` (sweep 2)
reescrevia só `N_CALL`; `N_ADDR` (o nó que `&nome` produz, carregando o nome bare do mesmo jeito)
entra na mesma condição — `tk_ns_rewrite_call` já opera por `nd_name`, então zero código novo
resolve `&f`. A forma qualificada precisou de ensino de verdade: o core exige que o operando de
`&` seja `N_IDENT` (`mc/src/parse.mc:892`), e `tk_ns_qualified_call` só sabia montar `N_CALL`.
Agora, sem um `(` a seguir, devolve `tk_id(full)` em vez de errar — a mesma filosofia D31.10 (uma
referência que não existe chega ao linker faltando, não é checada aqui) estendida de "chamada" a
"referência".

**Item 3, a convenção de mensagem — decidida e aplicada.** Grep completo de `sr_name_at`/nome
qualificado em mensagem ao dev por `teko_class.mc` e `teko_trait.mc` (o `use` de trait vive lá, a
mesma classe de bug que `tk_conf_name` do N1b já tinha). Duas categorias, nunca confundidas com a
resolução em si (que segue sobre o texto cru "__"-juntado, intocado):
- **nome da PRÓPRIA declaração** (a classe/`ci` sendo lida agora) → `tk_ns_short_of` — o dev nunca
  escreve o namespace ao se referir ao próprio tipo de dentro dele mesmo (o construtor sem tipo de
  retorno é o caso canônico: `Circle(...)`, nunca `geo.Circle(...)`);
- **nome REFERENCIADO** (a base/interface de `tk_conf_name`, o trait de `tk_use`, a base de
  `tk_base_ctor_call`/`tk_base_init`) → `tk_ns_dotted` — o texto que `tk_ns_read_path` leu É
  exatamente o que o dev escreveu, só com "__" no lugar de ".".
`teko_access.mc`'s `tk_deny_member` (a mensagem `X.m is private`) tem formato próprio e fica FORA
do grep pedido pelo crumb — achado adjacente, registrado, não tocado.

**Fixture** `surface_import.tk` + `ngen/tests/parts/geo.tk` (`namespace parts.geo;`
file-scoped): `import` duas vezes, `Circle` sem modificador (`internal`, D220) alcançada de
dentro do projeto, forma qualificada e bare (via o `using` implícito), `&twice`/
`&parts.geo.twice` cada um passado a `callp`. **Probes de recusa** (fora de `ngen/tests/`):
`import` de namespace sem arquivo (a mensagem crua do core, `cannot open`); `import` dentro de
`namespace { }`; `import` depois de um `namespace` no MESMO arquivo.

**Dívidas fechadas nesta série:** `&f` (item 2 acima). **Dívidas que seguem em aberto (fila,
`docs/design/port-teko-mc.md` + HANDOFF §5):** herança de interface, `using G = geo;`/`using
static`, genérico qualificado (D31.14), namespace aninhado (D31.1), ordem-livre de
tipo/declaração (§5.1 item 7). Próximo da fila: `const`, depois `switch` (D222).

## 35. N3b — o bug do verificador do N3: um local/parâmetro nunca perde para um `using` (2026-09-05)

`feat/ngen-namespace-shadow`, `teko_ns.mc` (`tk_ns_rewrite_call`, `tk_ns_walk_calls_in`,
`tk_ns_scan_calls`), `teko_access.mc` (`tk_deny_member`, achado adjacente do N3). 28/28 em exit
esperado; `--dump-ast` das 27 fixtures anteriores **byte-idêntico** contra `40814c22`; `mc limits
ngen` `ok`.

**O bug.** `tk_ns_walk_calls_in` (sweep 2) reescrevia todo `N_CALL`/`N_ADDR` cujo nome resolvesse
por prefixo de namespace ou `using`, sem checar se o nome bare já resolvia para algo mais próximo:
uma local/parâmetro do mesmo nome (`&f` virava o endereço da FUNÇÃO `geo.f`, não da local) e uma
declaração plana de topo com o nome exato (um `f` de fora de qualquer namespace perdia, em
silêncio, para o `geo.f` que um `using geo;` trazia).

**A ordem final de resolução de um nome bare** (C#, com a ressalva que o "atenção" do crumb
pediu confirmada): **(1)** local/parâmetro em escopo no sítio; **(2)** o namespace corrente do
sítio e seus prefixos, de dentro para fora (D31.6, inalterado -- um `f` dentro de `namespace geo`
que TAMBÉM declara `f` sempre vence, mesmo com uma `f` plana também visível); **(3)** SÓ quando
(2) não achou nada -- nem o sítio está dentro de um namespace, nem nenhum prefixo dele declara o
nome -- uma declaração plana de topo com o nome exato (`decl_find`); **(4)** os `using`s do
arquivo do sítio. Um `using` nunca vence o que já era visível sem ele; o passo (3) é o que fecha
essa fresta, sempre depois de (2), nunca antes -- se estivesse antes, o caso "namespace corrente
TAMBÉM declara o nome" quebraria, e é exatamente o que o crumb pediu para confirmar que não quebra.

**Onde vive o conjunto de nomes em escopo.** Reusada a MESMA tabela que `teko_typeof.mc` declara
para o seu próprio passe posterior (`sc_name`/`sc_ty`/`tk_nscope`, `tk_ty_scope_add`/
`tk_ty_scope_find`/`tk_ty_scope_var`/`tk_ty_scope_params`) -- a mesma que `teko_rc.mc` já reusa
para o seu passe, ainda mais tardio. `tk_ns_pass` roda ANTES de `tk_typeof_pass` (`teko.mc`), então
a tabela chega vazia; `tk_ns_scan_calls` a zera e a povoa do zero por função (parâmetros primeiro,
via `tk_ty_scope_params`), e `tk_ns_walk_calls_in` marca/restaura em cada `N_BLOCK` e registra
cada `N_VAR` só depois de caminhar seu próprio inicializador -- a MESMA disciplina de
`tk_ty_walk_list`. Nenhuma tabela nova: a chamada cruzada entre módulos `.mc` sem prototype
prévio já é o padrão do projeto (mc: "two top-level passes allow calling a function before it's
defined", `docs/core-language.md` -- `teko_ns.mc`, incluído antes de `teko_typeof.mc`/
`teko_access.mc` em `teko.mc`, já chamava símbolos dos dois antes desta mudança).

**Adjacente, fechado junto:** `teko_access.mc`'s `tk_deny_member` (a mensagem `X.m is private`)
usava `sr_name_at(owner)` cru -- o nome MANGLED (`geo__X`) -- em vez do pontilhado; agora
`tk_ns_dotted(sr_name_at(owner))`, a mesma conversão que o N3 já usa para todo nome REFERENCIADO
em mensagem.

**Fixture** `surface_namespace_fn.tk` estendida (exit 42 recalculado, códigos 12-14 novos): uma
local `f` sombreando `geo.f` sob `using geo;`, num bloco (`&f` é a local, provado por
`ld64(&f)==123`); uma `f` plana top-level vencendo o `using geo;` fora de qualquer namespace;
`geo.use_own_f` chamando `f(z)` de DENTRO de `geo`, resolvendo para o `geo.f` mesmo com a plana
também visível (a exceção do passo (2), confirmada). **Probes fora de `ngen/tests/`:** parâmetro
com o mesmo nome de uma função namespaced (`&f` do parâmetro, mesma prova por `ld64`); `f`
declarada num bloco interno e usada fora dele (não sombreia -- resolve `geo.f`, `f(2)` dá `1002`
mod 256 = `234`); a mensagem `geo.X.m is private` pontilhada.

**Fila:** inalterada -- `const`, `switch` (D222), closures/`ref`/`out` (D221).

### N3c -- o bug do verificador do N3b: um membro do tipo nunca perde para um `using` (2026-09-05)

`feat/ngen-namespace-member`, `teko_ns.mc` (`tk_ns_rewrite_call`, `tk_ns_scan_calls`,
`tk_ns_walk_calls_in`). 28/28 em exit esperado; `--dump-ast` das 27 fixtures anteriores
**byte-idêntico** contra `6ec5f55a`; `mc limits ngen` `ok`.

**O bug.** `tk_ns_pass` roda ANTES de `tk_typeof_pass` (`teko.mc`), então uma chamada bare dentro
de um MÉTODO já chegava reescrita para `geo__f` quando `tk_this_call` (`teko_this.mc`, "um nome que
o tipo declara como método vence uma função de mesmo nome de topo, igual em C#") sequer via o nome
-- `class Circle { public i64 f(i64 x) { ... } public i64 test(i64 x) { return f(x); } }` sob
`using geo;` chamava `geo.f`, não o próprio `Circle.f`, porque a reescrita do namespace já tinha
acontecido.

**A ordem final de resolução de um nome bare** (C#, a que o N3b já enunciava, com um degrau novo):
**(1)** local/parâmetro em escopo no sítio; **(2)** um MEMBRO (método, inclusive um herdado de uma
base, inclusive estático) do tipo/classe a que a função caminhada pertence -- o mesmo passo que um
`this.f()` escrito por extenso já dava, agora também para a forma bare; **(3)** o namespace
corrente do sítio e seus prefixos, de dentro para fora (D31.6, inalterado); **(4)** SÓ quando (2) e
(3) não acharam nada, uma declaração plana de topo com o nome exato (`decl_find`); **(5)** os
`using`s do arquivo do sítio.

**Onde vive a classe/struct do método corrente.** `tk_ns_call_cls` (novo, ao lado de
`tk_ns_call_site`), lido em `tk_ns_scan_calls` de `teko_class.mc`'s própria tabela de métodos via
`tk_method_of_fn` (a mesma função que `teko_this.mc`'s `tk_this_enter_fn` já usa para o seu passe
posterior -- cobre método, construtor, destrutor e acessor de propriedade, todos `N_FUNC` de
membro, sem tabela nova); a checagem em si é `tk_method_named_find(cls, name)` (`teko_class.mc`, já
caminha a cadeia de bases via `sr_base_at`). Duas declarações antecipadas (`teko_class.mc` e
`teko_this.mc` são incluídos DEPOIS de `teko_ns.mc` em `teko.mc`) -- o mesmo padrão de prototype
que este arquivo já usa para `teko_access.mc`/`teko_expr.mc`/`teko_default.mc`.

**Fixture** `surface_namespace_fn.tk` estendida (exit 42 recalculado, códigos 15-17 novos):
`Circle.test` chamando `f(x)` bare, resolvendo para o método da própria classe (não `geo.f`);
`Square : Shape` chamando `f(x)` bare dentro de um método que a DERIVADA não redeclara, resolvendo
para o método HERDADO da base (não `geo.f`); `Util.test_static` chamando `f(x)` bare dentro de um
método `static`, resolvendo para o membro estático (não `geo.f`). **Probes fora de `ngen/tests/`:**
um campo `f` (não um método) mais `&f` bare dentro de um método sem `this.` -- confirmado, no seed
`6ec5f55a` E nesta branch igualmente (não é regressão desta correção), que a superfície NÃO resolve
`&campo` bare para o endereço do campo: `teko_this.mc`'s `tk_this_fix` nunca trata `N_ADDR`, então
o nome cai na reescrita de namespace/`using` como qualquer outra chamada e o `&f` vira o endereço da
FUNÇÃO `geo__f` (exit 254, um crash, com um campo `i64 f` de valor 7 gravado antes) -- achado
adjacente, registrado, fora do escopo desta correção (que é só método); de FORA de `Circle`, `f(x)`
bare com `using geo;` em escopo e Circle.f existente (mas não chamado por `this`/um receptor)
resolve para `geo.f` (`f(5)` dá `1005 mod 256 = 237`) -- o membro de `Circle` não vaza para fora
da própria classe.

**Fila:** inalterada -- `const`, `switch` (D222), closures/`ref`/`out` (D221).

## 36. `const` landado -- açúcar sobre o `#define` do mc (2026-09-05)

D218: "o mc usa `#define`; construir `const` como açúcar". `ngen/teko_const.mc` (novo): topo
(bare ou `geo__N` namespaced, resolvido bare por um passe novo que estende `tk_ns_walk_calls_in`
a `N_IDENT` -- uma const não tem lookup em tempo de lowering como uma chamada tem, então o nó é
SUBSTITUÍDO por `N_INT`, não renomeado); membro (`Tipo__MAX`, checado antes de field/método em
`tk_static_member`, e no fallback de `tk_this_ident` para o bare); `Box<T, const N: i64>`
instanciado pelo NOME de um const (`tk_gen_targs` ganhou o ramo `T_IDENT`). Local recusado, de
propósito (`#define` é tabela única do programa). 29 fixtures, AST das 28 anteriores
byte-idêntica. HANDOFF.md §5 "CONST LANDADO" tem o detalhe completo, inclusive as dívidas
achadas (array local sem `[i]=v;`, redefinição cai num guard diferente do esperado).

## 37. Ternário `c ? a : b` landado -- hoist num pass() (D228, 2026-09-05)

D228: operador ternário, associativo à direita, mesma precedência de `||`. `ngen/teko_ternary.mc`
(novo). `syntax_infix("?", TK_TERN_PREC, &tk_tern_infix)` só constrói um placeholder
(`tk_ternary(c, a, b)`, o mesmo truque do `tk_defer_member` de `teko_typeof.mc` -- se o passe não
rodar, o núcleo recusa `call to unknown function`, nunca miscompila). **`TK_TERN_PREC` é 1, não
0** -- o crumb sugeria "0 ou o menor valor abaixo de `||`", mas `syntax_infix` recusa precedência
fora de 1..100 (`mc docs/reference/hooks.md` § `syntax_infix`), e 1 já é a linha mais baixa da
tabela (`language.md` §3), empatada com `||`; ler `b` com `parse_expr(TK_TERN_PREC)` (o MESMO
piso passado ao próprio operador, não piso+1) é o que dá a associatividade à direita -- um `?`
achado enquanto `b` está sendo lido é oferecido ao mesmo piso e cai no MESMO handler,
recursivamente.

**Posição do passe -- desvio do crumb, justificado.** O crumb sugeria registrar logo depois de
`tk_ns_pass` e antes de `tk_params_pass`. Medido que não dá: `tk_ty_of` (o oráculo de
`teko_typeof.mc` que este passe usa para tipar os dois braços) só enxerga o tipo de um `.` sobre
receptor que o parser não tipou (parâmetro, campo de tipo estático desconhecido) DEPOIS que
`tk_typeof_pass` reescreveu o placeholder deferido (`tk_unresolved_member`) no load/call que ele
representa -- antes disso o placeholder é uma chamada a um nome que nada declara, e `tk_ty_of`
responde -1, o mesmo "não sei" que daria pra um braço com tipo genuinamente desconhecido. Rodar
antes do oráculo faria um braço com `.` deferido falhar com "tipos diferentes" mesmo quando os
dois braços são, de fato, do mesmo tipo. `teko_rc.mc` roda por último pelo MESMO motivo (o
cabeçalho desse arquivo já registra: "depois que teko_typeof.mc resolveu todo acesso deferido e
todo nó tem seu tipo"). Registrado **logo depois de `tk_typeof_pass`, antes de `tk_ops_pass`**:
`params`/`typeof`, que rodam ANTES do ternário, nunca olham a FORMA da árvore (bloco/if/statement),
só censo por nome/tipo, então não perdem nada vendo o placeholder ainda intacto; `ops`/`default`/
`over`/`rc`, todos DEPOIS, passam a ver `if`/local comuns -- nenhum precisa aprender o que um
ternário é.

**Mecanismo do hoist (`tk_tern_lower`/`tk_tern_scan`/`tk_tern_stmt`/`tk_tern_branch`,
`teko_ternary.mc`):** cada `tk_ternary(c, a, b)` vira `T $t = 0; if (c) { $t = a; } else { $t = b;
}` inserido ANTES do statement envolvente, com o placeholder trocado por `$t` (`N_IDENT`) no
lugar exato -- a mesma forma "detach + node_assign + set_nd_next(keep)" que `teko_ns.mc`'s
`tk_ns_ident_to_const` e `teko_rc.mc`'s `tk_rc_park`/`tk_rc_hoist_cond` já usam. `T` sai de
`tk_ty_of(a)` comparado a `tk_ty_of(b)`; os dois braços são reduzidos (`tk_tern_scan`) ANTES da
comparação de tipo, e o `$t` recém-criado entra na MESMA tabela de escopo que `teko_typeof.mc`
mantém (`tk_ty_scope_add`) -- é o que deixa um ternário aninhado responder pelo seu próprio tipo
quando o externo pergunta.

**Preguiça com aninhamento -- o ponto que o crumb pedia atenção especial.** `c` é hoistado junto
da lista que o `if` também entra (roda incondicionalmente, então não custa nada); `a` e `b` são
cada um reduzido para dentro do SEU PRÓPRIO ramo (`thenOut`/`elseOut`), nunca para o preâmbulo
comum -- é essa escolha, não a ORDEM "de dentro pra fora" em si, que preserva a preguiça: um
ternário aninhado num braço (`c ? (x?y:z) : w`, ou o encadeamento à direita `c1 ? a : c2 ? b : d`,
que o parser right-recursivo já entrega como `tk_ternary(c1, a, tk_ternary(c2, b, d))` -- a MESMA
forma de árvore) tem seu próprio `if` construído DENTRO do ramo que o contém, então só roda quando
aquele ramo é tomado. Provado por probe fora de `tests/`: `0!=0 ? side(1) : (1!=0 ? side(2) :
side(3))` chama `side` uma vez.

**Condição de `while`/`for`:** como `teko_loop.mc` já rebaixa para `loop { if (!(c)) break; ... }`
antes deste passe rodar, um ternário na condição cai dentro do `if (!(c))` -- o "statement
envolvente" É esse `if`, que já está dentro do bloco do corpo do loop, então o hoist entra ali e
reavalia a cada volta. Provado na fixture (`while (i < 5 ? 1 : 0)`, laço termina com `i==5`).

**`return`/`if` sem chaves:** `tk_tern_branch` só embrulha o statement solto num bloco quando ele
de fato hoistou algo -- a mesma cerca que `tk_rc_branch` já usa para um temporário parked
(`teko_rc.mc`). Provado na fixture (`if (c != 0) return c == 1 ? 100 : 200;`).

**Fixture** `surface_ternary.tk` (30/30 em exit 42): inicializador, argumento, encadeamento à
direita, preguiça com contador provando UMA chamada só por lado, condição de `while`, braços de
objeto teko (`rt_live()` prova que a escolha não aloca um objeto novo), `return` dentro de `if`
sem chaves, ternário dentro de método. AST das **29 fixtures anteriores** byte-idêntica contra o
compilador da base `a4736222`. `mc limits ngen` `ok`. Probes fora de `tests/`: braços de tipos
diferentes (`i64`/`f64`) recusados com "the two arms of ?: have different types"; `?` sem `:`
recusado com "expected ':' in a ternary"; `a ? b` sem `:` como statement solto, mesma recusa;
ternário como lado esquerdo de atribuição recusado pelo NÚCLEO ("left side of assignment must be
a name" -- o resultado do ternário nunca é um `N_IDENT`).

**Limite conhecido (o próprio cabeçalho do arquivo documenta):** a avaliação de `c`/`a`/`b` é
hoistada para ANTES do statement que os continha -- um `f(x++, c ? a : b)` teria a chamada rodando
depois do hoist no MESMO statement (sem operador `++` de efeito em posição de expressão hoje, o
risco é teórico, registrado por completude).

## 38. `switch` landado -- statement como loop de uma volta, expression como açúcar sobre ternário (D222/D228, 2026-09-05)

D222 (statement + expression, `break N` atravessa, `when` = guarda) e D228 (expression = açúcar
sobre a cadeia de ternários, sem máquina própria). `ngen/teko_switch.mc` (novo); `ngen/teko_ternary.mc`
ganhou `tk_tern_hoist_var` (um segundo tipo de placeholder que `tk_tern_scan` reconhece: um `N_VAR`
real embutido no meio de uma expressão, hoisted incondicionalmente igual à condição `c` de um
ternário -- o que a `switch` expression usa para ler um `x` não-simples uma única vez).

**Statement, no parse:** `loop { if (t==1) {...break;} if (t==2||t==3) {...break;} ... {default;
break;} break; }`, `x` lido uma vez num `i64 $t` local. `break`/`break N` no corpo NÃO são
reescritos (o loop do switch já é o nível que a fonte vê); um `do`/`for` externo que envolve o
switch continua enxergando-o como só mais um `N_LOOP` no seu próprio `tk_loop_rewrite_stmt`, e a
composição sai certa sem nada extra aqui -- prova: `break 2` atravessando um `switch` dentro de um
`for` na fixture. `continue` no corpo do case é RECUSADO (não há `continue N` no núcleo; reescrever
para `break 1` sairia do switch, não continuaria o loop de fora -- a decisão que o próprio crumb já
antecipava, tomada sem precisar subir).

**Expression, sem máquina própria:** dobra `tk_ternary(cond, expr, tk_ternary(...))` da última
armação para trás; a condição da última NUNCA é testada (é a base incondicional), por isso exige-se
ao menos um `_` em algum lugar. `x` não-simples: um `N_VAR` embutido, hoisted pelo MESMO passe do
ternário.

**Dois achados corrigidos no processo:** `when` já estava reservado (`syntax_stmt`, entrega 1) --
`tk_kw` (só casa `T_IDENT`) nunca bate contra a palavra reservada; trocado por `tk_word` (a mesma
distinção que `teko_class.mc`'s próprio cabeçalho já documenta). E `syntax_infix` entrega o
operador JÁ CONSUMIDO ao handler -- `tk_switch_infix` não pode chamar `p_next()` de novo (o
`tk_tern_infix` do ternário já não chamava); o bug apareceu como "expected { after switch" comendo
o `{` de verdade.

**Fixture** `surface_switch.tk` (31/31 em exit 42). AST das 30 fixtures anteriores byte-idêntica
contra `3f223f9a`. `mc limits ngen` `ok`. Seis probes fora de `tests/`, todas recusadas com mensagem
clara: braço sem `break`, `case` duplicado, `case` não-constante, expression sem `_`, `continue`
dentro de `switch`, `switch` sem `{`.

**Dívidas registradas:** `case`/braço com um const NAMESPACED não resolve (o `#define` do núcleo só
dobra um nome bare no parse; um const qualificado só resolve num passe posterior); um `when` no
braço `_` textualmente último de uma expression não é testado (é a base incondicional da dobra).

**Errata (adoção do mc 0.14.1, `continue N`, 2026-09-05):** a recusa de `continue` num `case` acima
FECHOU o motivo original (o núcleo não tinha `continue N`) -- o 0.14.1 tem, espelhando `break N`.
`tk_switch_no_continue_stmt` virou `tk_switch_rewrite_continue_stmt` (`teko_switch.mc`): um
`continue k` na profundidade 0 do case vira `continue k + 1`, a MESMA regra de `break`
(`tk_loop_rewrite_stmt`, `teko_loop.mc`), nunca convertido a `break` -- o loop do switch é de uma
volta só, continuá-lo direto é sempre seguro. `teko_loop.mc` ganhou a mesma generalização para
`for`/`do`: um `continue k` que bate exatamente no nível do `for`/`do` corrente vira `break` (a
mesma proteção que já existia para o `continue` sem número, generalizada); um que aponta mais longe
segue como `continue k + 1`. Um `switch` sem laço envolvente é interceptado com mensagem própria
(`teko: continue inside a switch needs an enclosing loop`) em vez do "continue out of range" cru do
núcleo, que citaria um nível que a fonte nunca escreveu. `teko_rc.mc`'s `tk_rc_jump` lia `nd_val`
só de `N_BREAK`; agora lê de ambos (0.14.1 dá `N_CONTINUE` o mesmo campo). Fixtures estendidas:
`surface_switch.tk` (`continue` num case dentro de `for` e de `while`) e `surface_loops.tk`
(`continue 2` num `for` aninhado; dois `while` aninhados com um objeto de cada lado do salto,
provando que o release cobre os dois). De quebra: `tk_bracket_no_write` (a guarda do operando
sinkado por um `- ! ~`, `teko_prefix.mc`) protegia só o `[` de array GLOBAL deferido -- `tk_arr_
index_of` (`teko_array.mc`, array LOCAL) e `tk_array_index` (`teko_struct.mc`, campo-array) aceitavam
`=` sem consultá-la; `!b[1] = 3` só era recusado por acidente (`value of type void` sobre o `!`).
Os dois agora consultam a guarda e recusam com mensagem própria (`teko: the left side of = is not a
place`).

**Errata (crumb "guarda do continue sem laço", 2026-09-05):** a checagem "`switch` sem laço
envolvente" acima (`sawContinue && tk_realloop_depth == 0`) corria no PARSE contra
`tk_realloop_depth`, um contador que só `while`/`do`/`for` incrementam — um `loop { }` cru, palavra
do NÚCLEO, não passa por nenhum `syntax_stmt` de módulo, então o parser não tem como avisar
`teko_switch.mc` de que está dentro de um. Resultado: um `switch` com `continue` bare dentro de um
`loop { }` cru envolvente (superfície válida) era recusado à toa. Correção: a checagem virou um
`pass()` (`tk_switch_guard_pass`, registrado logo depois de `tk_ternary_pass`, ANTES de
`tk_rc_pass`) que caminha a árvore JÁ PRONTA, onde um `loop { }` bare é só mais um `N_LOOP` —
mesma composição que `tk_loop_rewrite_stmt`/`tk_switch_rewrite_continue_stmt` já usam para
`do`/`for`/`switch` aninhados. Só um `continue` BARE (`nd_val` original 0) que o rewrite do switch
empurrou pra além do próprio loop entra no radar — um `continue N;` explícito que passa do que
existe é problema do próprio número escrito, e o "continue out of range" cru do núcleo (levantado
bem mais tarde, no lowering) já é claro o bastante pra esse caso.

Marcação: nem o `continue` nem o `N_LOOP` do switch podiam ganhar um flag cru num campo comum
(`nd_a`/`nd_b`/`nd_c`/`nd_d`) — `--dump-ast` e `tk_clone` (`teko_struct.mc`) tratam os quatro como
índice de nó filho sempre, então gravar `1` ali faria os dois caminharem pro nó arbitrário de
índice 1. O `N_LOOP` do switch marca a si mesmo em `nd_val` (`TK_SWITCH_LOOP_MARK`, campo escalar
que nenhum walk recursa); o `continue` bare-e-empurrado é registrado por ÍNDICE DE NÓ numa tabela
à parte (`sw_bare[]`, `tk_nbaresw` como contagem e curto-circuito, o mesmo papel que `tk_ntern` já
tem pro `tk_ternary_pass`). Consequência: o `defaultBody = tk_clone_list(body)` (`case`+`default`
no MESMO grupo) precisou passar a clonar o corpo CRU e reescrever CADA cópia com o seu próprio
`tk_switch_rewrite_continue_list` — clonar um corpo JÁ reescrito copiaria o nível certo mas
NENHUMA marca (índices novos, tabela velha). O pass roda ANTES de `tk_rc_pass` porque esse último
relocaliza todo `break`/`continue` que envolve em release pra um ÍNDICE DE NÓ NOVO
(`tk_rc_jump`, `teko_rc.mc`), órfão de qualquer registro pela chave antiga.

`tk_realloop_depth` ficou sem leitor (só escrita) — removido de `teko_loop.mc`, junto dos três
incrementos/decrementos em `tk_while`/`tk_do`/`tk_for`.

**Fixture:** `surface_switch.tk` ganhou `sum_skip_even_loop` (o caso do bug: `continue` num case
dentro de um `loop { }` cru) e `sum_once_per_i` (`for` → `loop { }` cru → `switch`, provando que o
`continue` alcança só o laço mais interno). Probes fora de `tests/`: `switch` sem laço nenhum +
`continue` bare → mensagem própria; `continue 2` num case com só UM laço real → "continue out of
range" cru do núcleo (o número é do programador); as recusas (a)-(e) do crumb anterior seguem
idênticas. AST das 31 fixtures não tocadas byte-idêntica contra o compilador da base `6dcf771b`.

**Dívida adjacente, registrada, não perseguida:** `tk_switch_rewrite_continue_stmt` (este arquivo) e
`tk_loop_rewrite_stmt` (`teko_loop.mc`) caminham a MESMA forma (`N_BLOCK`/`N_LOOP`/`N_IF`) três
vezes agora — a marcação acima soma um TERCEIRO walk quase idêntico (`tk_switch_guard_stmt`).
Parametrizar os três por um único walker com callback/flag é cogitável, mas cada um faz algo
distinto o bastante (reescreve nível, converte `continue`→`break`, só lê e classifica) que a fusão
arrisca comportamento por um ganho de clareza incerto — fora do escopo desta correção pequena;
registrado para quem pegar o próximo crumb de faxina do `teko_loop.mc`/`teko_switch.mc`.

## 39. Arrays fixos landados -- registro no parse (local) e num pass() (global), larguras, o que ficou fora (2026-09-05)

`ngen/teko_array.mc` (novo). Um LOCAL é observável no parse (`on_stmt`, o mesmo mecanismo do
campo-array de `teko_struct.mc`) e resolvido AO MESMO TEMPO que `[` é lido -- tabela própria
(`av_*`/`tk_narr`), escopo por bloco (`tk_block` ganhou uma segunda marca/restauração). Um GLOBAL
não é observável no parse (`on_stmt` não vê topo, e não há hook público sobre um) -- um LEITURA
fica como o `N_INDEX` que o `[` de `params` também deixa (o fallback de sempre) e um `pass()`,
registrado ANTES de `tk_params_pass`, varre `nnodes` procurando um global array e reescreve só os
que acham dono; uma ESCRITA não pode esperar (o núcleo recusa `g[i] = e;` no próprio parse), então
fica um placeholder (`tk_call("tk_unresolved_array", 0)`, o idioma do `.` deferido de
`teko_typeof.mc`) resolvido pelo mesmo passe.

**Larguras:** `ld8/16/32/64`/`st8/16/32/64` por `type_width` (já existiam). `ld32` é sempre
zero-extending (`language.md` §2) -- um elemento `TK_SINT` mais estreito que a palavra (`i32`) é
casteado pro próprio tipo depois do load, o idioma que a própria doc do núcleo documenta.

**Bounds:** um índice literal fora de `[0, N)` é erro de compilação, nas duas rotas. Um índice
dinâmico NÃO tem guard em runtime (precisaria de `panic` de superfície, que não existe ainda).

**Fora do escopo, registrado:** array de tipo struct/classe (recusado, local e global -- sem nome
próprio pro RC percorrer); `T[]` em heap com RC; `T[]` como parâmetro; `.Length` sobre um global;
um `params xs[i]` dentro do corpo REPLAY de outro `params` que também usa um array global (o passe
de arrays roda uma vez, antes da instanciação de `params` -- aresta rara, nenhuma fixture combina
os dois).

**Fix à parte, no mesmo lote:** `tk_switch_check_end` só olhava o último nó de TOPO do corpo do
`case` -- um corpo escrito como bloco explícito (`case 1: { ...; break; }`) caía no "control cannot
fall out of a case" mesmo terminando em `break`. Recursa em `N_BLOCK` agora, como
`tk_switch_no_continue_stmt` já fazia ao lado.

**Fixture** `surface_arrays.tk` (32/32 em exit 42). AST das 31 fixtures anteriores byte-idêntica
contra `6cf49db1`, exceto `surface_switch.tk` (o fix acima) -- idêntica entre o commit do fix e
este. `mc limits ngen` `ok`.

## 40. Prefixos veem pós-fixos -- `.`/`[` mais apertado que `- ! ~` (2026-09-05)

`!b[1]` era `(!b)[1]`, `-a.x` era `(-a).x`: `parse_unary()` do núcleo acha `- ! ~` na sua PRÓPRIA
tabela de prefixo (`ops_init`) antes de `parse_primary` -- onde `syntax_expr` mora -- e lê o
operando por recursão direta em `parse_unary()`, que nunca consulta `.`/`[` (`syntax_infix`, prec
12). `syntax_expr("-", ...)` não conserta nada (código morto, medido); só `+` escapa da armadilha
porque `ops_init` nunca o registrou. Correção em `ngen/teko_prefix.mc` (novo): `tk_dot`/`tk_bracket`
sinkam pela cadeia de `- ! ~` que RECEBERAM como `left` até o operando de verdade, resolvem o
`.`/`[` nele, e reembrulham -- o mesmo `N_UNARY` que o núcleo constrói para `-(a.x)` escrito com
parênteses. `tk_bracket` ganhou uma guarda (`tk_bracket_no_write`) para o operando-base de uma
cadeia sinkada nunca virar alvo do deferral de array GLOBAL.

**Fixture** `surface_operator.tk` estendida (32/32 em exit 42, sem fixture nova): `!b[1]`, `-a[0]`,
`!f.on`, `-p.x`, `~g[2]`, `!c.flag()`, `-(-a[1])`. AST das 31 fixtures não tocadas byte-idêntica
contra `e2d4d936`. `mc limits ngen` `ok` (`intrin` 8/8).

## 41. Closures, ponteiro de função, `ref`/`out` e `T[]` de heap — desenho (architect-first, 2026-09-05)

Escopo: D221 (closures = lambda/função local + `use (a, &b)`; ponteiro de função, `ref T` e `out T`
como primitivas `type_new` Tier 4), D226 (C#/mercado, autonomia), D227 (RC só no pass), D217,
D218/D219/D220. Lido: `mc/docs/guide/96-a-new-primitive.md`, `hooks.md` §3/§4, `language.md`
§5/§6/§7, `mc/src/hooks.mc:236` (`word_add`), `mc/src/parse.mc:1119` (`parse_var`), e os módulos do
`ngen/` citados abaixo. Estilo `.mc` (cabeçalho `//`), como os outros 26.

### (a) Decisões

1. **Tipo de função = `delegate` NOMEADO (C# 1.0), não `Func<>`/`Action<>`.** `public delegate i64
   Op(i64 a, i64 b);` no topo ou em `namespace`. `Func<>`/`Action<>` são delegates GENÉRICOS: o
   `teko_generic.mc` faz record/replay de CORPO de `class`/`struct`, não de assinatura, e a BCL usa
   17 aridades — máquina nova por zero ganho. Um `type_new` por `delegate`, assinatura na tabela do
   módulo. `Func<>` como prelúdio de delegates nomeados = dívida barata, depois.
2. **O delegate É um objeto contado, com layout de CLASSE.** Fork técnico central do briefing,
   opção (a), refinada: `callp` leva UM `uptr`, então o valor-função é o PONTEIRO DO OBJETO e o
   código mora dentro dele (`+16`). Chamada = `callp(tk_deleg_code(d), d, args…)` — o objeto é o 1º
   argumento oculto, como `this` (D219). Trampolim gerado em runtime: recusado (o mc não emite
   código em runtime, e W^X).
3. **ABI UNIFORME: toda função alcançada por delegate recebe o objeto como 1º parâmetro.** Uma
   função livre sem captura ganha um **thunk** gerado, memoizado por (função, delegate):
   `i64 Op__thunk_add(uptr __env, i64 a, i64 b) { return add(a, b); }`, `code = &Op__thunk_add`. Sem
   isso, dois valores do MESMO tipo (um com captura, um sem) exigiriam formas de chamada diferentes
   no sítio, que só conhece o tipo — impossível.
4. **A identidade do TIPO é o delegate; o LAYOUT é por lambda.** Cada lambda/thunk emite vtable +
   release + alocador PRÓPRIOS (`TK_VT_FIXED 2`, release na palavra 0 — D227), e o release sabe o
   tamanho e quais capturas são contadas. Assim `rc_dec` libera um delegate cuja lambda ele não
   conhece, pelo mesmo caminho que já libera uma classe: zero linha nova em `teko_rc.mc`.
5. **Reuso de `sr_*` em vez de máquina nova.** Delegate e `T[]` entram na tabela de tipos do
   `teko_struct.mc` como formas novas (`TK_KDELEG`, `TK_KARRAY`) e `tk_is_counted` passa a
   respondê-las. Medido no código: `tk_on_stmt` (`teko_struct.mc:694`) já registra o local,
   `tk_struct_of_expr` já o tipa, `tk_ty_of` já o responde, o RC já o conta — é o que dispensa
   "estender o RC para tipos `type_new`" que o briefing previa.
6. **`f(x)` sobre valor-delegate rebaixa num `pass()`, não no parse.** Não há hook na posição de
   identificador (§5.1 item 6); `tk_deleg_pass` reescreve todo `N_CALL` cujo nome é local/parâmetro
   de tipo delegate em `callp`, com o retorno tipado pela assinatura (`tk_xt_put(n, si, dg_ret, 0)` —
   `pure=0` é o que faz `tk_rc_own` tratar o resultado como POSSUÍDO, o protocolo do `return` do
   `teko_rc.mc`). O que o pass não reescrever morre em `call to unknown function`, nunca em silêncio
   — o idioma do `.` deferido. Um local sombreia o `using` (N3b, já resolvido).
7. **Retorno estreito:** `callp` devolve `i64` cru por decisão do núcleo (`language.md` §7); um
   delegate que declara `i32`/`u8` recebe `CAST` no sítio, o idioma que `teko_array.mc` já usa.
8. **Aridade do delegate ≤ 10:** `callp` aceita 1..12 contando o ponteiro e o objeto gasta um —
   recusa na DECLARAÇÃO, com a frase do teto de método.
9. **`null` entra na superfície** (`syntax_expr("null")`, `N_INT 0` tipado `TY_UPTR`): `Op f = null;`
   / `Circle c = null;`; `rc_dec(0)`/`rt_own(0)` já são no-op. Chamar um delegate nulo é PÂNICO, não
   segfault: `tk_deleg_code(uptr d)` (nova, `lib/rt.mc`) checa e chama `rt_panic`.
10. **`ref`/`out` = DOIS `type_new`, mais tabela de apontado.** *(Errata K2w, §72: o parâmetro NÃO é
    declarado com o tipo do apontado — nasce com largura de PONTEIRO (`TY_UPTR`), porque carrega um
    endereço; o apontado fica só na tabela lateral, lido por `tk_param_ty`/`tk_decl_param_ty`.)*
    `type_new("ref", 8, 8, TK_INT)` e
    `type_new("out", …)` dão a identidade que o oráculo distingue de `uptr` (D221) e reservam as duas
    palavras do C#; um id por `ref T` explodiria a tabela de palavras, então o tipo APONTADO mora
    numa tabela do módulo, chaveada por (declaração, índice). **Medido, não presumido:** `word_add`
    (`mc/src/hooks.mc:236`) é `tok_add` + recusa de keyword do núcleo, sem recusa de duplicata —
    logo `type_new("ref")` + `syntax_expr("ref")` na MESMA palavra convivem.
11. **Sítio `f(ref a)`/`f(out a)` obrigatório, como em C#, por `syntax_expr`.** O argumento de uma
    chamada livre é parseado pelo NÚCLEO, sem hook — mas `syntax_expr` dispara dentro de
    `parse_primary`, que é exatamente onde o argumento aterrissa. O handler devolve o ENDEREÇO: `&a`
    para um local, e para `ref p.x`/`ref a[i]` o mesmo nó de endereço que o store de `.`/`[` já
    constrói (`teko_expr.mc` `tk_field_use`, `teko_array.mc` `tk_arr_addr`).
12. **Deref implícito num `pass()`, e o oráculo vê T desde já.** `tk_ty_scope_params` registra o
    parâmetro `ref T`/`out T` com o tipo APONTADO — *desde a K2w (§72) pela tabela lateral
    (`tk_param_ty`), não pelo `nd_type` do nó* —, então `tk_ty_of`, sobrecarga e operadores acertam
    sem esperar; só o rebaixamento (`x` → `ldW(x)`, `x = e` → `stW(x, e)`) é do pass, antes de
    `tk_ops_pass` e de `tk_rc_pass`. D227: escopo e posse têm UM dono, o pass.
13. **`ref T` de tipo contado escreve pelo SLOT do caller.** `x = e` no corpo vira `rt_store(x, e)`
    (o valor do parâmetro JÁ é o endereço do slot), não `rt_store(&x, e)`; a recusa "a parameter of
    class type is borrowed" (`teko_rc.mc` `tk_rc_assign`) ganha essa exceção, e só ela. O argumento
    `ref c` é `N_ADDR`, não contado — o RC não o parka nem o incrementa, correto por construção.
14. **`out` = DPS: o callee inicializa.** Um `out T` CONTADO recebe `st64(x, 0);` como primeiro
    statement do corpo (o frame do mc não é zerado — `parse_var` só reserva), e daí toda atribuição é
    `rt_store`, que libera o que estava lá. Sem isso o primeiro `rt_store` daria `rc_dec` em lixo.
15. **`out` checável: só a forma BARATA** — "o corpo não atribui NUNCA ao parâmetro" → recusa
    (`teko: the \`out\` parameter x is never assigned`). Atribuição definida por CAMINHO é cara e
    fica como **dívida declarada, não silêncio**: o slot foi zerado (14), o caller lê 0, não lixo.
    `f(out i64 a)` (declaração inline do C#) = dívida.
16. **Mangling `f__ref_i64` / `f__out_Circle`.** `teko_over.mc:122` e `tk_sig_of`
    (`teko_class.mc:222`) usam `type_name(ty)`, que responderia `ref` para todo apontado — colisão.
    Um helper único `tk_ty_sfx(d, i)` consulta a tabela de (10) e serve os dois. Regra do C#:
    `f(i64)` e `f(ref i64)` são sobrecargas distintas; `f(ref i64)` e `f(out i64)` NÃO — recusa
    (`teko: two overloads differ only by \`ref\`/\`out\``).
17. **`T[]` de heap é um objeto contado com vtable própria por T** (§(c)): `xs.Length` lê o
    cabeçalho, `xs[i]` tem guard de runtime, e o release percorre os slots com `rt_release_array`,
    que `lib/rt.mc` JÁ tem (hoje só serve campo-array inline). Fecha a recusa "array de tipo
    struct/classe" do §39.
18. **`panic` = a `rt_panic` que já existe, com nome de superfície.** `void panic(str msg)` em
    `lib/rt.mc` chama `rt_panic` (write(2) + `exit(70)`). **Divergência registrada do briefing** (que
    pedia `teko_panic` novo com `exit(134)`): um segundo abortador com outro código daria DOIS
    significados à mesma falha, e os guards landados (`params`, arena exausta) usam 70. Trocar
    70→134 é uma linha, se o dono preferir a convenção do `abort`.
19. **Lambda com tipos EXPLÍCITOS nos parâmetros e tipo ALVO conhecido.** `(i64 a, i64 b) => a + b`
    e `x => x * 2` (esta só quando o alvo dá o tipo); o retorno vem do delegate alvo — o C# também
    exige alvo. Duas grafias, ambas do C#: **contextual** (inicializador de local/campo de tipo
    delegate, `return` de função que devolve delegate, argumento de MÉTODO, que é o `tk_args` nosso)
    e **explícita** `new Op((i64 a, i64 b) => a + b)` / `new Op(add)` — a forma do
    `new EventHandler(...)` do C# 1.0, válida em qualquer posição, inclusive argumento de FUNÇÃO
    LIVRE (que o núcleo parseia sem hook). Alvo-tipagem nessa última posição = dívida.
20. **Captura só por `use (a, &b)`** (D221/PHP). Por VALOR = campo do objeto com cópia; contado ganha
    `rc_inc` na construção e `rc_dec` no release (`tk_release_fields`, reusado). Por REFERÊNCIA `&b`
    = o `&b` do mc, o ENDEREÇO do local do declarante, campo `uptr` NÃO contado (o declarante segue
    dono e libera no seu próprio escopo).
21. **Tempo de vida de `&`-captura: C-like, com DUAS recusas sintáticas baratas.** Sem escape
    analysis (lei do repo: UAF é do dev). Mas os dois pés-de-cabra do briefing são visíveis onde a
    lambda é rebaixada e são recusados: `&`-captura como operando de `return`, e como valor
    armazenado em campo/`static`/global. Qualquer outro escape é do dev, dito no cabeçalho do módulo.
22. **Função LOCAL nomeada = a mesma máquina**, lambda com nome: mesmo objeto gerado, e o nome vira
    um local de tipo delegate (`Op twice = (i64 x) => …;` é a forma canônica).
23. **`foreach (T x in xs)` = açúcar sobre `for` com índice** (dívida do §39 fechada), sobre `T[]` de
    heap, array fixo local e campo-array inline — as três fontes com `Length` conhecido. `in`
    contextual.
24. **`params T[]` fica FORA**, registrado: embalar o `params` (instanciado por contagem, §13/§28)
    num `T[]` de heap fecharia a dívida "o pacote nunca é devolvido" de `lib/rt.mc`, mas reescreve um
    crumb landado e mata a constante `xs_len`. Próximo natural depois do K3.
25. **Zero `intrinsic` novo nos cinco crumbs.** `mc limits ngen` mostra `intrin` 8/8 (os do
    `<float>`); um nono registro dispara um evento de `grow` e o veredito `grew` (exit 3,
    `mc/src/limits.mc`). Tudo aqui é função comum de `lib/rt.mc` ou `callp`, intrínseco do NÚCLEO,
    que não ocupa linha.

### (b) Hook → uso

| hook / API | uso |
|---|---|
| `type_new("Op", 8, 8, TK_INT)` | identidade do delegate; um por declaração (K1) |
| `type_new("ref"/"out", 8, 8, TK_INT)` | as duas primitivas de endereço (K2) |
| `type_new("arr__i64"/"arr__Circle", …)` | uma linha por tipo de elemento, preguiçosa (K3) |
| `syntax("delegate", &tk_delegate)` | a declaração de topo; `top_add` do thunk e do protótipo |
| `syntax_expr("null"/"ref"/"out")` | `null`; `f(ref a)`/`f(out a)` em `parse_primary` |
| `syntax_expr("new")` (o `tk_new` de hoje) | ramos novos `new T[n]` e `new Op(expr)` |
| `syntax_infix(".")` (`tk_dot`) | `xs.Length`; campo de tipo delegate seguido de `(` |
| `syntax_infix("[")` (`tk_bracket`) | `xs[i]` de heap, antes do fallback de `params` |
| `syntax_param(&tk_default_param)` — **o mesmo, estendido** | `ref`/`out`/`T[]`/delegate em parâmetro de função livre. NÃO uma segunda registração: a tabela de defaults é posicional por `mark` (`teko_class.mc:536`) e um handler que engolisse parâmetros furaria o `nreq` |
| `tk_gen_ty()` (`teko_generic.mc:520`) | as mesmas formas em parâmetro/campo/retorno de MEMBRO |
| `p_skip_balanced` + `p_push_source` + `p_subst_*` | corpo da lambda gravado e replayado (o record/replay do C8) |
| `parse_function`/`param_new`/`top_add`/`p_set_decl_name` | a função gerada da lambda e o thunk |
| `syntax_stmt("foreach")` + `tk_loop_rewrite_stmt` | K5, a rota do `for` do §29 |
| `pass(&tk_ref_pass)` / `pass(&tk_deleg_pass)` | ordem: `typeof` → **ref** → **deleg** → `ternary` → `ops` → `default` → `over` → `rc` |
| `decl_find`/`decl_param_type`/`decl_ret`/`decl_valid` | assinatura do callee nos dois passes |
| `type_name`/`type_width` | mangling e largura de `ldW`/`stW` |

### (c) Layouts, e o que o RC faz com eles

**Delegate / closure** (todo objeto de delegate, thunk ou lambda):

```
 vtable Op__lam3_vt        objeto (rt_alloc)
 +0  &Op__lam3_release     +0   vtable          <- rc_dec chega ao release por aqui
 +8  0 (sem itab)          +8   contagem
                           +16  code = &Op__lam3      (o corpo gerado)
                           +24  captura 0        (valor: contado -> rc_inc/rc_dec)
                           +32  captura 1        (`&b`: endereco cru, NAO contado)
```

`Op__lam3(uptr __env, <params>)` lê cada captura em `ld64(__env + off)`; chamada
`callp(tk_deleg_code(d), d, a1, …)`. RC: nada novo — linha `sr_` de forma `TK_KDELEG`, contada, com
campos em `fd_*`, então `tk_rc_var`/`tk_rc_assign`/`tk_rc_releases`/`tk_release_fields` já servem;
`Op__lam3_release` roda `rc_dec` em cada captura contada e `rt_free(p, 24 + 8*ncap)`.

**`T[]` de heap** (`new i64[n]`):

```
 vtable tkarr_vt_i64       objeto (rt_alloc(24 + n*W))
 +0  &tkarr_release_i64    +0   vtable
 +8  0                     +8   contagem
                           +16  len = n
                           +24  elementos, W = type_width(T)
```

`xs.Length` = `ld64(xs + 16)`; `xs[i]` = `ldW(tk_arr_at(xs, i, W))`, com `uptr tk_arr_at(uptr a,
i64 i, i64 w)` (`lib/rt.mc`) checando `0 <= i < ld64(a+16)` e chamando `panic`. `tkarr_release_i64` =
`rt_free(p, 24 + ld64(p+16)*8)`; `tkarr_release_<classe>` chama antes `rt_release_array(p + 24,
ld64(p + 16))`. O tamanho é lido do próprio objeto, então o TIPO não precisa carregá-lo.

**`ref T`/`out T`**: nenhum objeto — o valor do parâmetro É o endereço do slot do caller (local,
campo `p + OFF`, elemento `a + i*W`); não é contado, nunca é parkado, e o caller segue o único dono.

### (d) Sequência de crumbs

**K1 — delegate, ponteiro de função tipado, `callp` tipado, `null`.**
Arquivos: `ngen/teko_deleg.mc` (novo), `teko.mc` (include, `syntax("delegate")`,
`syntax_expr("null")`, `pass(&tk_deleg_pass)` logo depois de `tk_typeof_pass`), `teko_struct.mc`
(`TK_KDELEG`, `tk_is_counted`), `teko_expr.mc` (`tk_new` ganha `new Op(e)`; `tk_dot` chama campo de
tipo delegate), `teko_over.mc`/`teko_class.mc` (sufixo pelo nome do delegate — sai de graça de
`type_name`), `lib/rt.mc` (`tk_deleg_code`, `panic`).
Assinaturas novas: `void tk_delegate();` `i64 tk_deleg_find(uptr name);` `i64 tk_deleg_row(i64 ty);`
`uptr tk_deleg_thunk(i64 di, uptr fn);` `i64 tk_deleg_call(i64 n, i64 di);` `i64 tk_deleg_pass(i64
root);` `i64 tk_null();` — e em `lib/rt.mc` `uptr tk_deleg_code(uptr d)` / `void panic(str msg)`.
Fixture: `surface_delegate.tk` (`expect-exit: 42`) — declaração no topo e em `namespace`, `Op f =
add;` contextual e `new Op(add)`, `f(3,4)`, delegate como parâmetro e como retorno, campo de classe
de tipo delegate chamado (`h.cb(2)`), `null` guardado por `if`, sobrecarga `apply(Op)` vs
`apply(i64)`, `rt_live()` de volta ao piso. Mais `surface_panic_null.tk` (`expect-exit: 70`).
Gate: 5 pernas verdes; AST das 32 fixtures anteriores byte-idêntica; `mc limits ngen` `ok` com
`intrin` 8/8; probes fora de `tests/`: delegate com 11 parâmetros; `Op` sem alvo; atribuir função de
assinatura errada; `delegate` dentro de corpo de função.

**K2 — `ref` e `out`.**
Arquivos: `ngen/teko_ref.mc` (novo), `teko.mc` (`syntax_expr("ref"/"out")`, `pass(&tk_ref_pass)`
ANTES do `tk_deleg_pass`), `teko_default.mc` (ramo no `tk_default_param`), `teko_class.mc`
(`tk_params`/`tk_gen_ty` e `tk_sig_of`), `teko_over.mc` (`tk_ty_sfx`), `teko_typeof.mc`
(`tk_ty_scope_params` registra o apontado), `teko_rc.mc` (a exceção da decisão 13).
Assinaturas: `i64 tk_ref_param(i64 kind);` `void tk_rp_add(uptr owner, i64 idx, i64 kind, i64 ty);`
`i64 tk_rp_pointee(uptr owner, i64 idx);` `uptr tk_ty_sfx(i64 d, i64 i);` `i64 tk_ref_arg();`
`i64 tk_out_arg();` `i64 tk_ref_pass(i64 root);` `void tk_out_prologue(i64 f);`
Fixture: `surface_refout.tk` — `void bump(ref i64 x)`, `void split(i64 v, out i64 hi, out i64 lo)`,
`ref` sobre campo (`bump(ref p.x)`) e elemento (`bump(ref a[i])`), `ref Circle` reatribuído com
`rt_live()` provando a troca, `f(i64)` vs `f(ref i64)` resolvendo por sobrecarga, `ref` em método.
Gate: como K1 + probes: `f(a)` sem `ref` no sítio; `ref` num sítio de parâmetro por valor;
`ref i64 x = 1` (default proibido); `out` nunca atribuído; `f(ref i64)` + `f(out i64)`; `ref x;` como
declaração de local.

**K3 — `T[]` de heap, `Length`, RC dos elementos, `panic`.** (parcialmente BLOQUEADO, ver (e))
Arquivos: `ngen/teko_heaparr.mc` (novo), `teko_expr.mc` (`tk_new` → `new T[n]`; `tk_dot` → `Length`),
`teko_params.mc` (`tk_bracket` → ramo de heap antes do fallback), `teko_array.mc` (o deferral `gd_*`
passa a resolver também um `T[]` de parâmetro por `decl_param_type`), `teko_struct.mc` (`TK_KARRAY`,
`tk_is_counted`), `teko_default.mc`/`teko_class.mc` (a forma `T[]` em parâmetro/campo/retorno de
membro), `lib/rt.mc` (`tk_arr_new`, `tk_arr_at`).
Assinaturas: `i64 tk_ha_row(i64 ety);` `i64 tk_ha_new(i64 ety, i64 nexpr);` `i64 tk_ha_index(i64
base, i64 ety);` `i64 tk_ha_length(i64 base);` `void tk_ha_emit(i64 ai);` — e em `lib/rt.mc`
`uptr tk_arr_new(i64 n, i64 w, uptr vt)` / `uptr tk_arr_at(uptr a, i64 i, i64 w)`.
Fixtures: `surface_array_heap.tk` (n de runtime, leitura/escrita por índice, `.Length` num `for`,
`u8`/`i32` provando largura e sinal, `T[]` como parâmetro e como campo, `Circle[]` com `rt_live()`
voltando ao piso) e `surface_panic.tk` (`expect-exit: 70`, índice além do fim).
Gate: como K1; probes: `new i64[-1]`; `xs.Length = 3`; `xs[i]` sobre um `uptr` cru.

**K4 — lambda, função local e `use`.**
Arquivos: `teko_deleg.mc` (estendido), `teko_stmt.mc` (a função local dentro de corpo),
`teko_expr.mc` (`new Op(<lambda>)`), `teko_ns.mc` (o símbolo gerado nasce já qualificado).
Assinaturas: `i64 tk_lambda(i64 di);` `i64 tk_use_list(uptr pn);` `uptr tk_lam_emit(i64 di, i64
params, uptr body, i64 blen, i64 caps);` `i64 tk_cap_add(uptr name, i64 byref);`
`void tk_lam_deny_escape(i64 n);` `i64 tk_localfn();`
Fixture: `surface_lambda.tk` — lambda sem captura, `use (a)` por valor (o valor congelado não muda
quando `a` muda depois), `use (&b)` mutando o local do declarante, duas lambdas do mesmo tipo com
capturas diferentes, função local nomeada, lambda passada a método e chamada lá dentro, captura de
objeto contado com `rt_live()` provando que o release do closure a solta.
Gate: como K1 + probes: `use` de nome que não é local; lambda sem alvo; lambda com `&`-captura em
`return` e em campo (as duas recusas da decisão 21); aridade > 10; `use` duplicado.

**K5 — `foreach`.**
Arquivos: `ngen/teko_loop.mc` (`tk_foreach`, ao lado de `tk_for`), `teko.mc`
(`syntax_stmt("foreach")`).
Assinaturas: `i64 tk_foreach();` `i64 tk_fe_source(uptr pkind, uptr plen, uptr pety);`
Fixture: `surface_foreach.tk` — sobre `T[]` de heap, array fixo local e campo-array inline;
`break`/`continue` dentro; `foreach` aninhado com `break 2`; elemento de tipo classe com `rt_live()`.
Gate: como K1; probes: `foreach` sobre escalar; `in` faltando.

### (e) Fora do escopo (dívida) e o pedido ao mc

**BLOQUEADO no K3 — precisa de hook do mc.** `i64[] xs = …;` como LOCAL, como GLOBAL e como RETORNO
de função livre é lido pelo núcleo antes de qualquer hook: `parse_stmt` chama `type_of_token` sobre
`i64` (keyword do núcleo, `word_add` recusa sequestrá-la) e `parse_var` (`mc/src/parse.mc:1119`)
exige nome ou `[constante]` — `i64[] xs` morre em `variable name expected`, `i64 xs[]` em `array size
must be a positive constant`. **NÃO depende do hook e entra no K3:** `new T[n]`, `xs[i]`, `xs.Length`,
`foreach`, `T[]` como campo, como parâmetro (de membro por `tk_gen_ty`, de função livre por
`syntax_param`) e como local/retorno quando o ELEMENTO é tipo teko (`Circle[] cs = …`, cuja primeira
palavra é nossa). Texto pronto para `NOTICES-teko.md`:

> **Pedido (ngen → mc): `syntax_type(&fn)`, um hook na posição de TIPO.** Hoje um módulo não alcança
> a posição de tipo quando a palavra é do núcleo: `i64[] xs = …;` (array de heap, C#) e um futuro
> `i64? x` morrem em `parse_var` antes de qualquer registro. `syntax_param` (M41.5) resolveu
> exatamente esse problema um nível abaixo, na posição de PARÂMETRO — o pedido é o irmão dele:
> `void syntax_type(uptr fn)`, handler `i64 f(i64 ty)`, consultado logo DEPOIS de o núcleo ler uma
> palavra de tipo em `p_type()`/`parse_var`/`parse_top`/`parse_params`/cast/campo, recebendo o id
> lido e podendo consumir um SUFIXO (`[]`, `?`, `*`) para devolver outro id, ou 0 para "não é meu".
> Mesmas três guardas do `syntax_param` (consumiu-e-declinou, não-consumiu, devolveu id inválido).
> Com ele o `T[]` do C# fica uniforme nas sete posições de tipo, sem uma linha de `src/` por parte
> do ngen.

**Dívidas declaradas** (nenhuma escondida): `Func<>`/`Action<>` (prelúdio de delegates nomeados);
`op.Invoke(x)`; alvo-tipagem de lambda em argumento de função LIVRE (use `new Op(…)`); atribuição
definida por caminho para `out` (decisão 15); `f(out i64 a)` inline; `params T[]` (decisão 24);
`p.items[i]` sobre receptor que só o oráculo tipa (dívida do C8, herdada); array de heap como
elemento de outro array; `delegate` genérico; covariância/contravariância; `+=`/`-=` de delegate
(multicast — exige lista de invocação); `T[]` multidimensional e `T[][]`.

### (f) Riscos

1. **`tk_bracket` está sobrecarregado** (campo-array, array local fixo, `params`, global deferido,
   agora heap). O K3 entra pela mesma porta, na ordem "o que o parser JÁ sabe primeiro"; se a cadeia
   de `if` passar de cinco ramos, extrair um despachante nomeado é parte do crumb.
2. **Ordem dos passes.** `ref` antes de `deleg` antes de `ternary` é o que faz `op(x) ? a : b` e
   `x + 1` (com `x` sendo `ref i64`) tiparem certo. Um pass fora de ordem produz erro CLARO (tipo
   desconhecido / `call to unknown function`), nunca miscompilação — a propriedade que o
   `tk_defer_member` já garante. Cada crumb reconfirma a ordem no cabeçalho.
3. **`type_new` por delegate e por `T[]`** consome ids e RESERVA a palavra (`arr__i64`) — mesmo preço
   que `teko_ns.mc` já paga por `geo__Circle`, e nenhuma dessas grafias é escrevível por engano.
   `mc limits ngen` mede: tabela crescendo vira veredito `grew` e o crumb ajusta `[limits] tolerance`.
4. **`&`-captura é UAF por desenho** (decisão 21): as duas recusas cobrem os pés-de-cabra nomeados, o
   resto é do dev, dito em uma frase, sem prometer análise.
5. **Thunk por (função, delegate)** multiplica símbolos se a mesma função vira muitos delegates.
   Memoizado por par; o teto é o número de pares ESCRITOS na fonte — a ordem de grandeza das
   instâncias de `params`/genérico que já existem.
6. **O `mc` anda rápido** (0.14.1, `continue N` novo). Todo crumb relê `NOTICES-teko.md` antes de
   começar (§5.2), e o K5 lê `nd_val` do `N_CONTINUE` como já lê do `N_BREAK` — o aviso explícito da
   0.14.1 para quem faz limpeza por nível de laço.

## 42. K1 landado — errata curta (2026-09-05)

`tk_ns_pass` só reescreve `N_CALL`/`N_ADDR`/um `N_IDENT` de `const`; um `N_IDENT` de FUNÇÃO usado
como VALOR (`GOp f = gadd;` dentro de `namespace geo`) não é seu -- não previsto no §41(b)/(d).
`tk_deleg_pass` ganhou walk próprio (não o `tk_ty_pass_walk` genérico) para rastrear a namespace de
cada função e reusar `tk_ns_call_try_prefixes`/`tk_ns_call_try_usings` na coerção
(`tk_deleg_resolve_fn`, `ngen/teko_deleg.mc`). `new Op(fn)`, em tempo de parse, resolve antes da
renomeação e não precisou do mesmo tratamento. Resto do crumb saiu como desenhado; ver
`ngen/HANDOFF.md` § K1 LANDADO para o gate completo.

**K1b (2026-09-05).** `tk_deleg_build` registrava o tipo do retorno com `tk_xt_put` sobre o nó QUE
CONSTRUÍA, mas `tk_deleg_call` depois copiava esse nó para outra posição (`node_assign`) sem mover a
entrada -- um `Cell c = f(7);` via delegate lia "emprestado" e ganhava um `rt_own` que nunca zera.
Corrigido movendo o `tk_xt_put` para depois do `node_assign`, sobre o nó final (`tk_deleg_call`),
com o mesmo split em `tk_field_deleg_call` (nó fresco, sem cópia).

## 43. Item 0 e K2 landados — errata (2026-09-05)

**Item 0.** `tk_deleg_var` só interceptava um inicializador `N_IDENT`; qualquer outra expressão
(literal, aritmética) virava cópia de valor bruta num slot de delegate -- `Op f = 5;` compilava
limpo e segfaultava na primeira chamada. `tk_deleg_coerce` (`ngen/teko_deleg.mc`) é agora o único
validador que todo sítio de um slot de delegate usa: `null` passa, um valor já tipado (local/param/
campo, retorno de função comum, ou uma chamada aninhada através de um delegate-local, espiada por
`tk_deleg_expr_ty`) passa, um nome de função compatível é embrulhado no MESMO thunk que uma
declaração já ganhava, e qualquer outra coisa é recusa estática (`teko: Op takes a function, another
Op, or null`). Fiado nos quatro sítios: inicializador de var, atribuição de nome nu, `return` de uma
função que devolve delegate, e argumento de uma chamada a nome declarado UMA vez (guarda de
`tk_default_decl_count`, a mesma do C6 -- um nome sobrecarregado fica para o `tk_over_pass`). Zero
mudança de fixture: `surface_delegate.tk`/`surface_panic_null.tk` saem com `--dump-ast`
byte-idêntico; a atribuição ganhou de graça a mesma coerção de nome de função que a declaração já
tinha (`f = mul;` depois de `Op f = add;` agora embrulha igual).

**K2 -- `ref`/`out`, dois desvios medidos do §41(b)/(d):**

1. **Tabela de apontado chaveada por NÓ DO PARÂMETRO, não por `(owner uptr, idx i64)`.** Uma
   sobrecarga que difere só no TIPO apontado na mesma posição (`f(ref i64)` ao lado de `f(ref
   Circle)`) colidiria num par (nome, índice); e o parâmetro de uma função LIVRE não tem nó de
   declaração disponível em `syntax_param` (a função ainda não foi montada). `tk_rp_add(i64 pnode,
   i64 kind, i64 ty)` / `tk_rp_kind(pnode)` / `tk_rp_pointee(pnode)` usam o próprio nó do PARÂMETRO
   como chave -- já único, e disponível nos dois sítios (`teko_default.mc`, `teko_class.mc`) no
   instante em que `param_new` devolve o nó. `tk_ty_sfx(i64 p)` (mangling) segue a mesma forma, e
   `tk_ov_sig`/`tk_sig_of` passaram a caminhar os nós do parâmetro diretamente em vez de índice.
2. **O apontado de um `ref`/`out` sobre uma variável LOCAL BARE não é conhecido em tempo de parse**
   (só um local de tipo classe/struct é rastreado essa cedo, `teko_struct.mc`'s `tk_local_find`) --
   a tag do argumento (`tk_rfarg_tag`) grava `-1` nesse caso, e `teko_over.mc`'s `tk_ov_arg_ty`
   resolve preguiçosamente via `tk_ty_scope_find` quando o argumento é um `N_ADDR`, no instante em
   que o PRÓPRIO `tk_ty_pass_walk` (que `tk_over_pass` reusa) já mantém o escopo populado. Campo e
   elemento de array continuam conhecidos direto no parse (`fd_ty_at`/`av_ty_at`).

**Achado que exigiu correção (medido, não previsto):** o guard de entrada do `tk_ref_pass` só
olhava `tk_nrp` (parâmetros `ref`/`out` DECLARADOS) -- um programa que usa `ref`/`out` só no
ARGUMENTO de uma chamada para um parâmetro POR VALOR (`bump(i64 x)` chamado como `bump(ref a)`)
não registra nenhum parâmetro `ref`/`out` em lugar algum, e o pass saía sem tocar a árvore, então
`bump(ref a)` compilava e ligava por engano. Corrigido: o guard também olha `tk_nrf` (argumentos
`ref`/`out` ESCRITOS, em qualquer chamada). E o passe caminha TODA função (não só uma que
DECLARA `ref`/`out`), porque quem CHAMA `bump`/`split` raramente é uma delas -- `tk_ref_check_call`
tem que ver o sítio de chamada de qualquer função.

Fixture: `ngen/tests/surface_refout.tk` (`expect-exit: 42`) -- `ref` escalar com sobrecarga por
valor (`bump(i64)`/`bump(ref i64)`), `out` duplo (`split`), `ref` sobre campo (`bump(ref p.x)`) e
elemento de array local (`bump(ref a[1])`), `ref` em método (`Doubler.twice`), e `ref` de pointee
CONTADO (`replace(ref Cell c, ...)`) com `rt_live()`/destrutor provando a troca (a exceção única
que `teko_rc.mc`'s `tk_rc_assign` ganhou -- `dest = tk_id(name)` em vez de `tk_addr(name)` quando o
nome é um parâmetro `ref`/`out`).

Gate: 35/35 em exit esperado (as 34 anteriores + a nova); `--dump-ast` das 34 anteriores
**byte-idêntico** ao compilador da base `5e83bb3a`; `mc limits ngen` `ok`, `intrin` 8/16 (zero
crescimento). Probes (fora de `tests/`): `f(a)` sem `ref` no sítio (`argument 1 needs \`ref\` at
the call site`); `ref` num sítio de parâmetro por valor (`argument 1 is not passed by reference`,
provado também para uma chamada de MÉTODO); `ref i64 x = 1` (`a \`ref\` parameter has no default`);
`out` nunca atribuído (`the \`out\` parameter hi is never assigned`); `f(ref i64)` + `f(out i64)`
(`two overloads differ only by \`ref\`/\`out\``); `ref x;` como declaração de local (`\`ref\`/\`out\`
is only valid as a parameter type`).

**Dívidas declaradas (nenhuma escondida):** atribuição-por-caminho para `out` (decisão 15 do §41,
herdada); `f(out i64 a)` inline; `ref`/`out` sobre um campo/elemento alcançado por mais de um nível
(`ref p.inner.x`); `ref`/`out` sobre um parâmetro de tipo classe como receptor de `.`/campo
implícito (`this.x`) dentro do próprio corpo -- não testado, `tk_ref_addr` só resolve um local
BARE, `p.campo` explícito ou `a[i]` local.

## 44. K2b — correção de reprovação do K2 (2026-09-05)

Dois bugs medidos no K2 (`ngen/teko_ref.mc`/`teko_rc.mc`), ambos corrigidos:

1. **`out T` contado vazava a partir da 2ª chamada.** `tk_ref_out_prologue` emitia `st64(x, 0)` cru
   como 1º statement do corpo -- zerava o slot do caller sem `rc_dec` do que estava lá. Corrigido em
   duas partes: (a) `tk_rc_var` (`teko_rc.mc`) agora dá a TODA local de tipo contado declarada SEM
   inicializador (`Circle x;`) o mesmo `rt_own(0)` que um `null` explícito ganharia -- o slot nasce
   sempre liberado, nunca com o lixo que o frame `parse_var` reserva; (b) o prólogo passa a
   `rt_store(x, 0)` em vez do `st64` cru, liberando o valor anterior do slot do caller pelo mesmo
   `rc_dec` de qualquer outro store contado. `rt_alloc` (`lib/rt.mc`) JÁ zera (`rt_zero`) em ambas as
   rotas (freelist e bump) -- campos/elementos de heap não têm esse bug, só a local sem inicializador.
2. **`ref x` onde `x` já é parâmetro `ref`/`out` da função corrente repassava o endereço errado.**
   `tk_ref_addr`, para um identificador simples, sempre devolvia `&name` (`tk_addr`) -- o slot LOCAL
   do parâmetro, que morre no retorno; deveria devolver o VALOR do parâmetro (`x`, que já É o
   endereço do slot do caller). Corrigido no PASSE (`tk_ref_walk`, pós-oráculo, não no parse): um
   `N_ADDR` tageado (`tk_rfarg_kind`) cujo nome resolve a um `ref`/`out` do `tk_ref_cur_fn` corrente
   (`tk_ref_param_named`, a mesma consulta por-NÓ que a exceção do `tk_rc_assign` já usa) é reescrito
   para `tk_id(name)`; um `out` repassado assim conta como atribuído (`tk_rp_mark_seen`), porque o
   callee mais fundo é quem tem que escrevê-lo. Achado durante a correção: `node_assign` copia o nó
   INTEIRO, `nd_next` incluso -- reescrever um argumento no MEIO de uma lista (`replace(ref c, nv)`)
   sem preservar o `nd_next` truncava a lista de argumentos (`replace takes at least 2 arguments`
   num caso que tinha 2). `tk_ref_replace` (novo, preserva `nd_next` ao redor de `node_assign`)
   corrige isso tanto no rewrite novo quanto no rewrite de leitura já existente (`N_IDENT`), que
   tinha o mesmo defeito latente (não exercitado até aqui por nenhuma fixture).

**Adjacente corrigido (1 linha):** `tk_ref_addr` recusava `ref`/`out` sobre algo que não é
identificador (`bump(ref 5)`) com a mensagem genérica do núcleo (`name expected`, de dentro de
`p_ident()`); agora um guard antes de `p_ident()` dá a mensagem dedicada
`` teko: `ref`/`out` requires a variable: 5 ``.

**Adjacente verificado, sem correção:** a alegação de que o HANDOFF descreve o `syntax_param` lendo
o tipo apontado com `p_type()` não foi encontrada -- nem no HANDOFF, nem aqui, nem no doc-comment de
`tk_ref_param`; as três já descrevem o mesmo `tk_ns_param_ty()`/`type_of_token()` de dois passos que
o código usa.

Fixture: `surface_refout.tk` estendida (`expect-exit: 42` recalculado) -- `outcheck` (`out Circle`
2x com contador de destrutor, e uma 3ª chamada sobre local já inicializado), `chaincheck` (`ref`
escalar em 3 níveis, `1000+1+1`), `outrelaycheck` (`out` repassado), `rcheck_relay` (`ref Cell`
repassado e reatribuído no nível mais fundo, `rt_live()` a 0 no fim).

Gate: 35/35; `--dump-ast` das 34 fixtures não tocadas **byte-idêntico** contra `e175299d` (só
`surface_refout.ast` difere, esperado); `mc limits ngen` `ok`, `intrin` 8/16 (zero crescimento).
Probes (fora de `tests/`): os dois bugs revertidos isoladamente e re-testados (bug 1a crasha por
SIGBUS ao desreferenciar o lixo do slot; bug 1b perde o `dtors` da 2ª chamada; bug 2 falha em
compilar com `the \`out\` parameter y is never assigned` quando o rewrite/mark-seen é removido).

## 45. K3 landado — `T[]` de heap, o bloqueio do §41(e) caiu no mc 0.14.2 (2026-09-05)

O `syntax_type` pedido no §41(e) chegou (mc 0.14.2): um `T[]` é um objeto por elemento distinto
(`tk_ha_row`), do MESMO shape de um delegate (vtable de duas palavras, contagem, dados) -- `rc_dec`
libera um array sem saber nada sobre ele, pela mesma máquina que já libera classe/interface/
delegate (`tk_is_counted` estendido, zero linha nova em `teko_rc.mc`). `xs[i]` é um endereço
guardado em runtime (`tk_arr_at`, `lib/rt.mc`), não um bound compile-time como o array-FIELD/
local-fixo; `.Length` é `ld64(xs+16)`, só-leitura.

**Desvio 1, medido: `T[]` PARÂMETRO resolve no PARSE, não pelo `gd_*`/`decl_param_type` que o
§41(b) previa.** Esse caminho (i) corrompe `p_decl_name()` da PRÓPRIA declaração sendo lida se o
gerador (`tk_ha_ensure_gen`, que `top_add`) disparasse no meio do parâmetro -- `top_add` zera
`cur_decl`, e `teko_default.mc`'s teste de "nova declaração" leria 0 pro parâmetro seguinte,
iniciando uma linha nova por engano; (ii) deixaria `N_INDEX`/o placeholder de escrita invisíveis a
qualquer passe ANTES do oráculo, pendurados fora da árvore como o `pd_recv` de um `.` deferido é.
A saída medida: `teko_struct.mc`'s `tk_hp_*`, uma tabela PEQUENA resetada uma vez por declaração
(`tk_hp_reset`, no "nova declaração" de `teko_default.mc` e no topo de `tk_params`) e preenchida
no MESMO instante em que o parâmetro é lido -- `xs[i]`/`xs.Length` sobre ele resolvem no PARSE,
sem oráculo, sem placeholder, sem passe extra, a mesma cedo-demais-pro-core, tarde-o-bastante-pro-
módulo janela que `av_*`/`lv_*` já usam pra local/campo. Provado pelo caso que quebraria a
alternativa: `cs[i].area()` sobre um parâmetro `Circle[] cs` -- se `cs[i]` ficasse `N_INDEX` cru
até o oráculo, `.area()` deferiria sobre um receptor sem tipo e resolveria por NOME
(`tk_pend_by_name`), que "funciona" com uma classe só declarando `area` e mascara a ambiguidade
com duas (medido: adicionar uma segunda classe com `area` MUDAVA a resposta sob a alternativa).

**Desvio 2, medido: `T[][]` não é refusado no `ty` que chega em `tk_ha_type`, e sim no token
seguinte.** `take_type` (mc) despacha `syntax_type` UMA vez por posição de tipo -- um `ty` que já
é um `T[]` nunca chega aqui de novo (a palavra reservada `"i64[]"` é um lexema que o lexer não
forma, então nenhuma fonte a nomeia). A checagem certa lê o SEGUNDO `[` logo após consumir o
primeiro `[]`, antes de devolver `tk_ha_row(ty)`.

**Achado que exigiu correção (medido, não previsto pelo §41(b)):** `teko_default.mc`'s
`tk_default_param` e `teko_ref.mc`'s `tk_ref_param` liam o tipo de um parâmetro por
`type_of_token(p_id())` + `p_next()` manual -- o `syntax_type` só dispara dentro de `take_type`,
que só o `p_type()` público chama. Sem trocar os dois por `p_type()`, `i64[] x` como parâmetro de
função LIVRE (ou o apontado de `ref`/`out`) lia `i64` e deixava o `[` sobrando pro `p_ident()`
seguinte recusar com uma mensagem confusa. `teko_class.mc`'s `tk_params` já caía em `p_type()` de
graça, por `tk_gen_ty` (teko_generic.mc) -- membro (campo/parâmetro/retorno) funcionou sem tocar.

**Mangling:** a palavra reservada de um `T[]` carrega `[`/`]` (de propósito, pra nunca colidir com
o que a fonte escreve) -- inaceitável dentro de um SÍMBOLO gerado. `tk_ty_mangle_name` (o que
`teko_ref.mc`'s `tk_ty_sfx` agora chama em vez de `type_name` direto) devolve `"arr_i64"` só pra
essa forma; o TIPO em si (`--dump-ast`, `type_new`) segue com os colchetes.

Fixtures: `surface_array_heap.tk` (`expect-exit: 42`) -- `n` de runtime, `xs[i]`/escrita/`+=`/`-=`/
`++`, `.Length` como bound de `while` E `for`, `u8`/`i32` provando largura e sinal, `T[]` como
parâmetro de função livre E de método e como campo (`this.items`), `Circle[]` com `rt_live()`
provando o piso duas vezes, `Op[]` chamado por índice com uma função nua coagida no slot (a mesma
`tk_deleg_coerce` que um `Op` local/campo já usa). `surface_panic_index.tk` (`expect-exit: 70`).

Gate: 37/37; `--dump-ast` das 35 anteriores -- as 3 sem `rt.mc` byte-idênticas, as 32 com `rt.mc`
com diff puramente ADITIVO (só a nova `tk_arr_at`, `grep '^<'` vazio); `mc limits ngen` `ok`,
`intrin` 8/16 (zero crescimento), `passes` 13 (zero pass nova). Probes: `new i64[-1]` (exit 70);
`xs.Length = 3`; `xs[i]` sobre `uptr` cru; `i64[][]`; `new i64[]` sem tamanho; `ref i64[] x`
(recusa limpa -- `tk_hp_add` só registra a forma PLANA, nunca `ref`/`out`, cujo valor é o
ENDEREÇO do slot do caller, não o objeto).

Dívidas: `T[]` como GLOBAL -- o TIPO é aceito em toda posição, mas leitura/escrita/`.Length` sobre
a GLOBAL não resolvem; `ref`/`out T[]`; `params T[]` (já era dívida); `T[][]`/multidimensional;
`p.items[i]` sem `this.` explícito (herdada do D219); array de heap como elemento de outro.

## 46. K4 landado -- lambda só na grafia EXPLÍCITA, `(` de expressão nunca hookado (2026-09-05)

**K4 LANDADO** (entrega 5, D221/§41): lambda, função local nomeada e `use (a, &b)`, sobre
`new Op((T a, T b) [use (...)] => corpo)` -- a MESMA máquina de objeto do K1 (§41(c)), com um
allocator/release/vtable/corpo GERADOS POR LAMBDA (nunca memoizados por par, ao contrário do thunk
do K1: cada ocorrência pode capturar valores diferentes).

**Desvio medido, e por quê -- não é preguiça, é o risco que o §41(f) item 1 já antecipava para
`[`, agora medido para `(`.** O §41(a) decisão 19 pedia DUAS grafias: contextual (`Op f = (a,b) =>
e;`, sem `new`) e explícita (`new Op((a,b) => e)`). Só a explícita foi ensinada. `parse_primary`'s
próprio ramo de `(` (`mc/src/parse.mc:873`) decide cast-ou-agrupamento pelo TOKEN que vem logo
depois: `(i64 a, i64 b) => ...` começa EXATAMENTE como um cast `(i64)` começaria, então
`parse_expr(0)` sozinho morre em `expected ) in cast` antes de qualquer hook rodar -- medido, não
hipotético (probe abaixo). A única rota pra grafia contextual seria `syntax_expr("(", &f)`, que
INTERCEPTARIA TODO `(` de posição de expressão do programa inteiro (cast E agrupamento), e
`docs/reference/hooks.md` § 3 é explícito: essa posição NÃO TEM fallback ao núcleo ("an expression
position has no empty node to fall back on: 0 is an error") -- um handler aceito ali é EXCLUSIVO, e
teria que reimplementar cast+agrupamento byte-a-byte pra não quebrar as 37 fixtures que já usam
`(` livremente. A forma explícita evita isso por inteiro: `tk_new_deleg` (K1) já tem seu PRÓPRIO
ponto de parse logo depois de `new Op(`, então o `(` de um parâmetro de lambda NUNCA passa por
`parse_primary`. Cobre todo o fixture, inclusive "função local nomeada" (decisão 22), que vira
`Op twice = new Op((i64 x) => x*2);` em vez do `Op twice = (i64 x) => x*2;` que a decisão citava
como "forma canônica" -- registrado aqui como o motivo de não seguir a citação ao pé da letra. A
grafia contextual bare e a forma curta `x => e` (decisão 19, "só quando o alvo dá o tipo") ficam
DÍVIDA, não código morto: as duas são recusadas HOJE pelo próprio núcleo (`expected ) in cast` /
`expected ; after declaration`), nunca em silêncio.

**Como o corpo é lido -- nem record/replay, nem `parse_function` puro; os DOIS, por grafia.** A
lista de parâmetros da lambda é `parse_params()` (o MESMO leitor público de `delegate`/função
livre -- dá de graça `ref`/`out`/`T[]`/defaults num parâmetro de lambda, não exigido pelo §41 mas
sem custo extra). `use (...)` é lido a seguir, ANTES do `=>` (a posição sem ambiguidade: depois dos
parâmetros, antes do corpo). O corpo: `=> { ... }` chama `parse_function(ret, nome, params)` (o
reset de block-depth de M31 é dela); `=> expr` monta `tk_ret(parse_expr(0))`/`tk_stmt(...)` à mão,
no estilo do `tk_deleg_thunk_fn` do K1. `p_set_decl_name`/`p_decl_name` são salvos e restaurados ao
redor de tudo -- a lambda é uma declaração nova por IDENTIDADE (gensym `Op__lam0`, `Op__lam1`, ...,
`tk_ns_qualify`da como um `delegate`), então a tabela de defaults (`owner != tk_dflt_owner`,
teko_default.mc) e o `tk_hp_reset` do K3 disparam de graça, sem uma linha nova.

**Captura -- o allocator recebe UM PARÂMETRO POR CAPTURA**, o valor pra uma por-valor, o ENDEREÇO
cru pra uma por-referência -- o call site (`new Op(...)`) fornece `tk_id(nome)`/`tk_addr(nome)` NO
INSTANTE da construção, o "congelamento" que a decisão 20 pede: uma cópia por-valor muda de dono na
hora (`rc_inc` antes de gravar, se contada -- desfeito por `rc_dec`, um por captura por-valor
contada, no release). Dentro do corpo gerado, uma por-valor vira um LOCAL DE VERDADE (`T nome =
ld<W>(__env+off);`) -- o reclaim comum (`teko_rc.mc`) já borrow-to-own e libera no fim da chamada,
ZERO código de RC novo. Uma por-referência vira um `uptr` interno (`__lamrefN`) com o ENDEREÇO, e
toda leitura/escrita do nome original no corpo é reescrita pra `ld`/`st` através dele
(`teko_array.mc`'s `tk_arr_load`/`tk_arr_store`, os MESMOS dois helpers que `teko_ref.mc`'s
`tk_ref_walk` já usa pra dereferenciar `ref`/`out` -- zero máquina nova). Capturar por referência um
tipo CONTADO é recusado (dívida honesta: o slot seria o endereço do PONTEIRO do declarante, e um
`rt_store` correto ali pediria a MESMA exceção que `teko_rc.mc` já dá a um parâmetro `ref` contado
-- generalizar essa exceção pra uma captura ficou fora do K4).

**O nome capturado tem que já ser um local que a função contém:** `teko_struct.mc`'s `tk_on_stmt`
ganhou `tk_slv_add`/`tk_slv_find` -- toda declaração `N_VAR`, de QUALQUER tipo (não só struct/class,
que é tudo que `tk_local_add` já sabia), grava (nome, tipo); `use (nome)` consulta na hora que lê a
cláusula. **Dívida:** um PARÂMETRO da função declarante não é capturável hoje -- nenhuma tabela
existente (K2's `rp_*`, K3's `tk_hp_*`) rastreia parâmetro por NOME de forma genérica; generalizar
`tk_slv_add` pro site de `parse_params`/`teko_class.mc`'s `tk_params` fica pra quem pegar essa dívida.

**"não capturado" e as duas recusas de escape (decisão 21) reusam o QUE JÁ EXISTE, não inventam
tabela nova.** O corpo é percorrido por `tk_lam_walk`, o MESMO desenho de escopo em pilha de
`teko_typeof.mc`'s `tk_ty_scope_*` (`tk_nscope`/`tk_ty_scope_var`/`tk_ty_scope_find`), usado
DIRETO -- todo `N_IDENT` que não é parâmetro/local/captura E não é `decl_find`/`tk_struct_find`
(função ou tipo global) morre em `teko: X is not captured; add it to use (...)`, a frase exata do
§41. As duas recusas de `&`-captura escapando (por `return`, por campo) são UMA função,
`tk_lam_escapes(e)` -- "`e` é uma chamada ao allocator de uma lambda com captura por referência" --
chamada nos TRÊS pontos onde um slot de delegate é ESCRITO: `tk_deleg_return` (K1), `teko_expr.mc`'s
`tk_field_use` (campo de instância) e `teko_access.mc`'s `tk_static_use` (campo estático, forward-
declarada lá porque esse arquivo é incluído ANTES de `teko_deleg.mc`). GLOBAL não precisou de
checagem: `parse_global`'s próprio `global initializer must be constant` já recusa qualquer
inicializador não-literal antes que a pergunta exista pra fazer.

Fixture: `surface_lambda.tk` (`expect-exit: 42`) -- sem captura; `use (k)` por valor congelado
(mutar `k` depois não muda o que a closure leu); `use (&acc)` mutando o local do declarante entre
duas chamadas; duas lambdas do MESMO delegate com capturas diferentes; `Op twice = new Op(...)`
(função local nomeada); lambda guardada num campo e chamada via `this.cb(x)` + lambda passada como
argumento de MÉTODO e chamada lá dentro; captura de um objeto contado (`Box`) com `rt_live()`
provando que o release do closure solta o objeto só depois que a ÚLTIMA referência cai; a mesma
máquina dentro de um `namespace`; corpo em BLOCO; `new Op(...)` como argumento de função LIVRE.

Gate: 38/38 (37 anteriores + a nova); `--dump-ast` das 37 anteriores **byte-idêntico** ao
compilador da base `6ddae6a3` (`same=37 diff=0`, comparado via `mc-teko --dump-ast` dos dois
binários sobre os mesmos 37 arquivos); `mc limits ngen` `verdict ok`, zero linha `grew` (`intrin`
idêntico entre base e K4). Probes (fora de `tests/`): `use` de um nome de FUNÇÃO (recusado, não é
local); nome livre no corpo sem `use` (a frase exata, `k is not captured; add it to use (...)`);
`(i64 x) => e` sem `new Op(...)` (recusado pelo núcleo, `expected ) in cast`); `x => e` sem
parênteses (recusado, `expected ; after declaration`); 11 parâmetros de lambda contra um delegate
de 2 (`the lambda does not match the delegate`); `use (k, k)` duplicado; `&`-captura devolvida por
`return` de uma função que retorna o delegate; `&`-captura atribuída a um campo de instância dentro
de um método -- as duas últimas com a MESMA frase, "a lambda that captures by reference cannot
leave its scope".

Dívidas: a grafia CONTEXTUAL e a forma curta `x => e` (ambas pedem hookar `(` de expressão --
risco medido acima, fora de escopo); captura de um PARÂMETRO da função declarante; captura por
referência de tipo CONTADO; `op.Invoke(x)`/`Func<>`/`Action<>`/`params T[]` embalando lambda
(herdadas do §41(e)); alvo-tipagem de lambda em argumento de função LIVRE sem `new Op(...)` --
NA PRÁTICA fechada, já que a forma explícita já cobre essa posição
(`apply(new Op((i64 x) => x - 1), 43)` funciona, testado no fixture).

## 47. K4b landado -- as três ressalvas do verificador do K4 fecham (D221/§41, 2026-09-05)

Três commits, um por item.

**Item 1 -- captura de delegate por valor.** O prólogo (`tk_lambda_prologue`, teko_deleg.mc) gera
`Op inner = ld64(addr);` para uma captura por valor de tipo delegate: um `N_CALL` que a MESMA
passada (`tk_deleg_pass`, walking o corpo da própria lambda gerada, já que `top_add` a inclui no
`root`) visita como inicializador de `N_VAR` comum e recusa via `tk_deleg_coerce` -- nenhuma das
quatro formas que ele aceita bate com um `ld64`. Corrigido tageando o nó do `ld64` com o tipo do
delegate (`tk_xt_put(ld, dsi, sr_ty_at(dsi), 1)`) no INSTANTE em que o prólogo o constrói -- o
mesmo idioma que `tk_field_use` (teko_expr.mc) já usa para o load de um CAMPO de tipo delegate, e
seguro aqui porque esse nó É o final: nada o copia (`node_assign`) antes da passada lê-lo, ao
contrário do nó de `tk_deleg_call` que motivou a lição do K1b (§42) de tagear o nó FINAL, não o que
se está construindo.

**Item 2 -- taint flow-insensitivo, não forma literal.** `tk_lam_escapes(e)` só reconhecia `e` como
um `N_CALL` direto ao alocador de uma lambda com captura por referência. Dois buracos confirmados:
`Op f = new Op(...) use (&acc) => ...; return f;` (a variável `f`, não o `new Op(...)`, é o que
`return` lê) e `cb = new Op(...) use (&x) => ...;` com `this` implícito (o `N_ASSIGN` de nome bare
vai por `teko_this.mc`'s `tk_this_assign`, que nunca chamava `tk_lam_escapes`). Corrigido com:

- **(a)** `tk_lamref_add` continua marcando o ALOCADOR (por nome) no instante em que a lambda com
  `&`-captura é CONSTRUÍDA (parse time) -- inalterado.
- **(b)** um NOVO `on_stmt` (`tk_lam_taint_stmt`, registrado em `teko.mc` ao lado dos outros três)
  roda a CADA `N_VAR`/`N_ASSIGN` que o núcleo termina de ler, em QUALQUER lugar do programa,
  aninhamento incluído -- exatamente o ponto de que `Op g = f;` (ou `g = f;`) precisa para propagar:
  se `tk_lam_escapes(nd_a(n))` já é verdade (um `N_CALL` tainted OU um `N_IDENT` já tainted), o nome
  ESCRITO (`nd_name(n)`) entra numa tabela nova (`taint_name`/`tk_taint_add`/`tk_taint_find`,
  paralela a `lamref_name`). Sem reset de escopo -- flow-insensitivo por design, superset seguro do
  que a forma literal recusava, nunca falso-negativo.
- **(c)** `tk_lam_escapes` passa a aceitar as DUAS formas: `N_CALL` (via `tk_lamref_has`) OU
  `N_IDENT` tainted (via `tk_taint_find`). Chamada nos MESMOS três pontos que já existiam
  (`tk_deleg_return`, `tk_field_use`, `tk_static_use`) MAIS DOIS novos: `teko_this.mc`'s
  `tk_this_assign` (o `cb = ...;` implícito) e `teko_heaparr.mc`'s `tk_ha_index` (`arr[i] = f;`
  sobre `T[]`). Array FIXO (`teko_array.mc`) NÃO precisou de check: o módulo já recusa QUALQUER
  linha da tabela de tipos como elemento de array fixo (`tk_struct_by_ty(ety) >= 0` -> "an array of
  objects is not taught yet"), local e global -- um delegate é uma dessas linhas, então
  `Op tbl[4];` já não compila, tornando o sítio inalcançável independente desta mudança.
  Passar a lambda como ARGUMENTO de `new Op(...)` continua permitido, de propósito (D221/§41): o
  callee só LÊ, C-like, nunca armazena -- registrado no cabeçalho de `tk_lam_escapes` como o limite.
- Sem fixture nova para este item: as cinco recusas (return direto, return indireto, campo
  explícito, campo implícito, elemento de `T[]`) e as duas aceitações (retorno de uma cadeia
  `Op g = f;` sem vazamento até o ponto de retorno, argumento de função/método) viraram PROBES fora
  de `ngen/tests/`, rodadas manualmente e descartadas -- `git status` limpo confirma que nenhuma
  sobrou no worktree.

**Item 3 -- grafia contextual e curta, nos alvos que já conhecem o tipo ANTES do inicializador.**
`(` de expressão CONTINUA sem hook -- o §46 já media esse risco corretamente (`parse_primary`'s
próprio ramo de `(` decide cast-ou-agrupamento pelo token seguinte, sem fallback ao núcleo, e um
handler `syntax_expr("(", &f)` interceptaria TODO `(` do programa). A saída: entrar SÓ nos pontos
onde o MÓDULO, não o núcleo, já decide o que vem a seguir, e onde o tipo alvo já é conhecido antes
de sequer olhar o inicializador:

- **`Op f = <init>;`** -- `tk_type_stmt` (teko_access.mc) parava de decidir e delegava CEGAMENTE ao
  `parse_var` do núcleo assim que resolvia o tipo (`Point p = new Point;`, `Op f = add;`, QUALQUER
  tipo). Agora, quando o tipo é um delegate ESCALAR (`tk_is_deleg(si)`), desvia para
  `tk_deleg_var_stmt` (teko_deleg.mc) -- com UMA exceção que tem que continuar caindo no caminho
  velho: `Op[] ops = new Op[2];` (K3's `T[]`), cujo marcador `[` vem logo após o tipo, não após o
  nome -- checado por `tk_bracket_follows()` (o MESMO scan não-consumidor de `tk_dot_follows`,
  trocando `.` por `[`) ANTES de desviar. Sem esse guard, `p_ident()` dentro de `tk_deleg_var_stmt`
  lia `[` onde esperava um nome e morria em "name expected" -- pego pelo próprio
  `surface_array_heap.tk` no gate, não hipotético.
  `tk_deleg_var_stmt` lê nome/`=`/inicializador manualmente e monta o `N_VAR` com `tk_var(ty, nome,
  init)` -- a MESMA forma que `teko_ns.mc`'s `tk_var_after_type` já usa para o mesmo problema
  (namespaced/generic types que também não podem entregar ao `parse_var` do núcleo), e provado
  idêntica ao que o núcleo produziria pelo dump-ast byte-a-byte das 37 fixtures antigas (nenhuma
  delas tem `Op f = <init>;` cujo `N_VAR` mudasse de forma).
  A ARRAY FIXA de delegate (`Op tbl[4];`) é refutada com a MESMA frase de `tk_var_after_type`
  ("an array of this type is not taught yet") em vez de reimplementar o que já é dívida do §39
  (array de linha-de-tipo é recusado, local e global, independente de quem lê a declaração).
- **A decisão "é lambda?" é um LOOKAHEAD NÃO-CONSUMIDOR, não `p_skip_balanced`+`p_push_source`.**
  Tentativa inicial usou o par record/replay (`p_skip_balanced` grava o `(...)`, decide pelo token
  seguinte, `p_push_source` faz o replay) -- MEDIDO QUEBRADO: `p_push_source` logo após um `p_id()`
  peek DESCARTA o lookahead pendente daquele peek (`docs/reference/hooks.md` § Record and replay:
  "the push does not touch the pending lookahead token, so the p_next() after it discards the
  lookahead") -- exatamente o `=>`/`use` que a decisão christalizou ao chamar `p_id()`. Sintoma
  medido: `BoxOp f = new BoxOp(() use (b) => b.v);` (fixture já existente) passava a morrer em
  "expected => after the lambda parameters", porque o `=>` sumia no push e o parser resumia
  direto em `b.v`. Record/replay serve para REPLAY depois que a decisão já foi tomada por outro
  meio -- não para TOMAR a decisão. Corrigido com `tk_paren_lambda_follows()`: uma varredura de
  BYTES a partir de `p_cp()` (o mesmo princípio de `tk_dot_follows()`), contando profundidade de
  parênteses para achar o `)` que fecha o ATUAL, sem consumir NENHUM token -- e só então, com a
  resposta em mãos, o código real corre: lambda -> `tk_lambda_build(si, line, fl)` direto no stream
  AO VIVO (o mesmo estado que `new Op((...) => ...)` já entrega, sem replay algum); não-lambda ->
  `parse_expr(0)` direto no MESMO stream. Seguro para uma lista de parâmetros especificamente
  (nunca tem string literal que desbalancearia a contagem crua de parênteses) -- não é uma técnica
  geral para expressão arbitrária.
- **A forma curta `x => e`** -- `tk_arrow_follows()` (o mesmo scan de `p_cp()`, checando `=>` em vez
  de `[`/`.`) detecta um `T_IDENT` seguido de `=>`; `tk_deleg_short_lambda` lê o nome
  (`p_ident()`), monta UM parâmetro com o tipo que o delegate já declara (`dg_pty_at(si, 0)`) e
  entrega a `tk_lambda_finish` -- `tk_lambda_check_params` recusa sozinho se o delegate não tomar
  exatamente um parâmetro (o loop compara contagem e tipo, `param_new(garbage, nome)` quando
  `dg_np_at(si)==0` nunca chega a comparar tipo, já quebra na contagem).
- **`tk_lambda_build` vira wrapper fino sobre `tk_lambda_finish`** (o rabo compartilhado: check de
  parâmetros, `use`, corpo -- bloco ou expressão --, prólogo, walk de escopo, allocator/vtable/
  release, montagem da chamada) -- reusado por `tk_deleg_short_lambda` E pela chamada direta que a
  forma paren faz depois de decidir que é lambda. `name`/`saved` (o gensym e o `p_decl_name` salvo)
  são abertos pelo CALLER antes de `tk_lambda_finish`, porque `p_decl_name` tem que estar correto
  ANTES de `parse_params()` rodar (defaults/`tk_hp_reset`) -- a forma curta não chama
  `parse_params()`, mas abre os dois do mesmo jeito por uniformidade, sem custo (nenhuma linha a
  mais na leitura de UM identificador).
- **Argumento de MÉTODO.** `tk_args` (teko_expr.mc) é genérico demais para saber o tipo esperado de
  cada posição -- overload resolution (`tk_method_pick`) só decide DEPOIS de contar os argumentos.
  `tk_call_method_args`/`tk_args_typed` fecham essa lacuna SÓ quando o nome do método NÃO É
  sobrecarregado (`tk_method_name_count(si, m) == 1`, um contador NOVO em teko_class.mc que só
  soma o que `si` mesmo declara -- bases não entram, porque um `override` repete a assinatura do
  pai, e contar os dois dobraria à toa): nesse caso, e SÓ nesse, o tipo de cada posição vem direto
  de `decl_param_type(mt_fn_at(mi)'s decl, posição+1)` (o `+1` pula o receptor, que ocupa o
  parâmetro 0 da função mangled) -- se a posição for um delegate, o argumento passa por
  `tk_deleg_init_expr`, a MESMA função que a declaração usa; senão, `parse_expr(0)` de sempre. Um
  nome sobrecarregado cai de volta em `tk_args` puro -- dívida ACEITA, não código morto: bater a
  overload certa exigiria contar argumentos ANTES de saber os tipos, um problema de ordem que este
  crumb não resolve (C# resolve com inferência de tipo natural sobre lambda, máquina bem maior).

Fixture: `surface_lambda.tk` ganha `contextual_check` (`Op f = (i64 x) use (k) => x * k;`),
`short_check` (`Op g = x => x + 1;`) e uma chamada contextual em `method_check`
(`h.relay((i64 x) => x - 1, 43)`, sem `new`) -- `relay` não é sobrecarregado, então bate no caminho
novo.

**Gate:** 38/38 (37 anteriores + `surface_lambda.tk` recalculada -- os TRÊS itens landam na MESMA
fixture); `--dump-ast` das 37 anteriores byte-idêntico ao compilador da base `68b38174`
(`same=37 diff=0`, 0.15.0 dos dois lados); `mc limits ngen` `verdict ok`, `intrin` 8/16 em ambos os
lados (zero intrínseco novo). Probes (fora de `tests/`, descartadas depois de rodar): as cinco
recusas do item 2 (todas com a frase "a lambda that captures by reference cannot leave its scope");
passar uma lambda com `&`-captura como argumento (aceito, sem erro); `Op g = (add);` e
`Op f = (flag != 0 ? add : mul);` (o fallback não-lambda da grafia contextual -- o segundo confirma
uma dívida PRÉ-EXISTENTE e não-relacionada: coerção de delegate dentro dos braços de um ternário
nunca foi implementada, o mesmo erro sai idêntico do compilador da base `68b38174` sem os parênteses
extras, `Op f = flag != 0 ? add : mul;` puro).

**Achado adjacente, fora de escopo, reportado e NÃO corrigido:** `apply(Op f, i64 x) { return
f(x); }` (ou o equivalente como método) chamado com uma VARIÁVEL já existente como primeiro
argumento (`apply(f, 41)`, `f` uma `Op` local) morre em `call to unknown function` na posição de
`f(x)` dentro do callee -- reproduz IDÊNTICO no compilador da base `68b38174`, então não é regressão
deste crumb. Isolado: reproduz com QUALQUER segunda posição de parâmetro após o `Op` (livre OU
método, com OU sem overload, com OU sem captura), mas NÃO reproduz quando a chamada usa
`new Op(...)`/um literal diretamente como argumento (o padrão que TODAS as fixtures já usam) --
sugere que o problema está em como uma REFERÊNCIA a um delegate já existente, passada como
argumento, interage com a mangling/lowering de uma função de mais de um parâmetro, não com a
lambda em si. Fica para quem pegar essa dívida achá-la e corrigi-la; nenhuma fixture ou probe deste
crumb depende dela (a prova de "passar como argumento é permitido" usa `h.relay(f, 41)` livre de
capturas problemáticas, que não bate nesse caminho).

Dívidas que seguem abertas, herdadas ou reafirmadas: `return (params) => e;`/`return x => e;` (o
`return` é palavra do núcleo, sem hook -- fica `return new Op(...)`); captura por referência de um
tipo CONTADO (K4); captura de um PARÂMETRO da função declarante (K4); `op.Invoke(x)`/`Func<>`/
`Action<>`/`params T[]` embalando lambda (§41(e)); a grafia contextual como argumento de uma função
LIVRE ou de um método SOBRECARREGADO (a forma explícita `new Op(...)` já cobre as duas posições, na
prática).

## 48. K4c landado -- taint por (função, nome), coerção de ternário; o achado do §47 não reproduz (2026-09-05)

Três itens, três commits.

**Item 1 -- taint chaveado por (owner, nome), não por nome global.** `taint_name`/`tk_taint_find`
(`teko_deleg.mc`, K4b) era uma tabela de NOMES da unidade inteira, nunca resetada: um `f` com
`&`-captura numa função contaminava `return f;` de uma lambda LIMPA chamada `f` em OUTRA função.
Corrigido com uma segunda coluna, `taint_owner`, e um resolvedor único, `tk_taint_owner()`: durante
o PARSE (o `on_stmt` `tk_lam_taint_stmt`, e os três sítios que checam no próprio `parse_expr` --
`tk_field_use`, `tk_static_use`, `tk_ha_index`), `p_decl_name()` já é a função/método corrente
(`docs/reference/hooks.md`: setado por `parse_function` pela duração do corpo); depois que o parse
termina e o resto do `tk_lam_escapes` roda numa PASS (`tk_this_assign` via `tk_typeof_pass`,
`tk_deleg_return` via `tk_deleg_pass`), `p_decl_name()` responde 0 -- o resolvedor cai para
`tk_cur_fn_name` (novo global, `teko_typeof.mc`), setado pelos DOIS loops que andam por `N_FUNC`
numa pass (`tk_ty_pass_walk` e o próprio loop de `tk_deleg_pass`), o mesmo padrão de
`tk_pass_proj`/`tk_deleg_cur_ns`.

No caminho, uma causa-raiz de SIGSEGV latente (nunca disparado até este crumb precisar ler
`p_decl_name()` DEPOIS de uma lambda construída no mesmo corpo): `top_add()` limpa `p_decl_name()`
como efeito colateral (documentado), e `tk_lambda_finish` restaurava o nome salvo ANTES do seu
PRÓPRIO `top_add(f)` (e dos de vtable/release/allocator logo atrás) -- cada um desses re-zerava o
que acabara de ser restaurado, deixando o resto do corpo da função ENVOLVENTE com `p_decl_name()==0`
pelo resto daquela declaração. Corrigido movendo o `p_set_decl_name(saved)` para o fim de
`tk_lambda_finish`, depois de todo `top_add`.

Flow-insensitivo DENTRO da função continua (reatribuição pra uma lambda limpa ainda é tainted --
limite documentado, mantido). Os cinco casos de escape do K4b (return direto/indireto, campo
explícito/implícito, elemento de `T[]`) seguem recusados; verificado por probe fora de `tests/`.

**Item 2 -- o achado adjacente do §47 NÃO reproduz.** `apply(Op f, i64 x) { return f(x); }` +
`Op g = add; apply(g, 41);` compila e roda limpo (42) tanto no compilador desta branch quanto no
compilador da base `a11623ca`, sem tocar em código algum -- a dívida já não existe (fechada por
alguma correção landada entre o `68b38174` que a viu e o `a11623ca` que abre este crumb; nenhum
commit isolado aponta o culpado, e não vale caçar). Variações tentadas, todas verdes (42), a partir
do CONTEXTO de `surface_lambda.tk` (classes, delegates, `namespace`, várias funções, não um arquivo
minúsculo à parte):
  1. o mínimo do §47 isolado (`apply`/`add` livres, sem mais nada no arquivo).
  2. o mesmo, colado dentro de `surface_lambda.tk` inteiro (uma função nova, `free_arg_var_check`).
  3. via MÉTODO (`Holder.relay(h, 43)`, `h` uma variável já existente).
  4. o mesmo método, com o `Op` construído por uma lambda COM captura por valor.
  5. um NOME DE FUNÇÃO (não lambda) atribuído à variável antes de passá-la (`Op g = add1;`).
  6. dentro de um `namespace`, chamando uma função LIVRE sobrecarregada (`apply` com duas
     assinaturas, 2 e 3 parâmetros) com uma variável já existente.
  7. o mesmo de (6) mas testado junto com (1)-(5) no MESMO arquivo, todas as combinações vivas ao
     mesmo tempo.
Dívida fechada por não-reprodução -- não há regressão a corrigir, nem fixture a escrever (a lei do
handoff não pede oráculo para o que já funciona).

**Item 3 -- coerção de ternário num delegate.** `Op g = flag ? add : mul;` morria em
`teko: Op takes a function, another Op, or null` -- a hipótese do crumb (`__tN` do hoist do
ternário chegando como `i64` em `tk_deleg_coerce`) estava invertida: `tk_deleg_pass` roda ANTES de
`tk_ternary_pass` (a própria ordem do arquivo, `teko.mc`), então `tk_deleg_coerce` vê o placeholder
CRU (`tk_ternary(c, a, b)`, o `N_CALL` de nome `"tk_ternary"` que `tk_tern_infix` constrói) -- nenhuma
das quatro formas que ele reconhecia batia com isso. Ensinado UM shape a mais: um `tk_ternary` faz
`tk_deleg_coerce` recursar em cada braço (`a`/`b`), religando a mesma lista de irmãos com os braços
já coeridos, e devolve o MESMO placeholder. `tk_ternary_pass`, rodando depois, enxerga dali em
diante dois braços já do tipo do delegate (um nome de função virou `tk_deleg_wrap`, já tipado `si`
via `tk_xt_put`) -- o check de "os dois braços têm tipos diferentes" fecha sozinho, sem outra
mudança. Cobre as três chamadas de `tk_deleg_coerce` (`Op g = <ternário>;`, `g = <ternário>;`,
`return <ternário>;`) e ternário ANINHADO de graça (a recursão desce nos dois braços de cada nível).
9 linhas.

**Fixture:** `surface_lambda.tk` ganha `clean_f_returns_check` (item 14, item 1) e `ternary_check`
(item 15, item 3) -- zero fixture nova. Nada para o item 2 (não reproduziu).

**Gate:** 38/38 (`--entry-only`); `--dump-ast` das 37 fixtures não tocadas byte-idêntico ao
compilador da base `a11623ca` (`same=37 diff=0`); `mc limits ngen` `verdict ok`, zero linha `grew`
(crescimento aditivo pequeno: +2 funcs/lowered, +1 global — `tk_taint_owner`/`tk_cur_fn_name` —, e
as ~8 linhas do item 3, todos dentro da reserva). Probes fora de `tests/`, descartadas: as cinco
recusas de escape do K4b (ainda recusadas, mesma frase); reatribuição pra lambda limpa (ainda
tainted, limite mantido); as sete variações do item 2 acima; ternário direto, parentetizado, via
`return`, via atribuição, e aninhado (item 3), todos corretos em RUNTIME (não só compilam -- o
braço certo roda).

## 49. K5 landado -- `foreach`, fecha a série §41 (2026-09-05)

Decisão 23 do §41(a): `foreach (T x in xs)`, açúcar sobre a MESMA máquina de `tk_for` (o `loop` de
uma volta + `tk_loop_rewrite_stmt`), sobre as três fontes com `Length` conhecido sem oráculo --
`T[]` de heap (K3), array fixo local e campo-array inline (`teko_array.mc`/`teko_struct.mc`).
Arquivo único: `ngen/teko_loop.mc` (ao lado de `tk_for`), mais uma linha de registro em `teko.mc`
(`syntax_stmt("foreach", &tk_foreach)`).

**Desvio 1, medido: a fonte não é lida por `parse_expr(0)`.** Um campo-array inline sem `[`/`=`
logo depois (`this.items` como fonte, exatamente o que o crumb pede) é recusado por `tk_array_of`
(teko_struct.mc, "an array field is read one element at a time") -- a mesma guarda que pega
`p.items;` solto em qualquer parte do programa. `tk_fe_source` é por isso um parser PEQUENO e
dedicado (não um `syntax_expr`/hook novo): lê um nome bare ou `name.member` (`this` incluso, pela
mesma tabela `tk_local_find` que já registra `this`, D219) e resolve pela MESMA cedo-no-parse que
`[`/`.` já usam para cada uma das três formas -- nunca cai no "parser não sabe tipar -> defere ao
pass" que as três já evitam desde K3/K4.

**Desvio 2, a assinatura ficou maior que o `tk_fe_source(pkind, plen, pety)` do §41(d).** Saiu
`tk_fe_source(pkind, pety, pnel, psrc, poff)` -- um campo precisa do NOME do receptor (`psrc`) e do
OFFSET do campo (`poff`) para que o endereço seja reconstruído FRESCO em cada um dos dois lugares
que o usam (o bound do laço e a carga por elemento), nunca compartilhado -- a mesma razão pela qual
`tk_ha_len` (K3) já recebe um NOME em vez de um nó pronto: "um nó vive em uma lista de irmãos só".

**Achado que o build local teve que confirmar antes de reusar `tk_loop_rewrite_stmt` sem alteração
nenhuma (não hipotético -- rodado): `break;` puro já nasce `nd_val = 1` no núcleo (`continue;` puro
nasce 0).** Só por essa assimetria a MESMA chamada que `for`/`do` já fazem
(`tk_loop_rewrite_stmt(stmt, 0)`, sem uma linha de "foreach" na própria função) soma corretamente
os DOIS níveis extras que cada `foreach` introduz (o `once` de uma volta e o `loop` externo do
bound) em qualquer profundidade -- verificado com `break 2` escapando de dois `foreach` aninhados.
Registrado como armadilha nova, `ngen/HANDOFF.md` §5.1 item 19.

**Tipo do elemento, D131/D132 aplicado:** alarga implícito quando `T` declarado é mais largo que o
elemento (`tk_cast`, o idioma de `teko_array.mc`); mesma largura com base diferente, ou o inverso,
é recusado (`teko: the foreach variable's type does not fit the array element`).

Fixture: `surface_foreach.tk` (`expect-exit: 42`) -- `T[]` de heap com `break`/`continue`; array
fixo local; dois `foreach` aninhados com `break 2`; `Circle2[]` com `rt_live()` provando que a
variável de iteração só EMPRESTA (o array segue dono do elemento durante o laço, os destrutores só
disparam em `cs = null;`, piso batido nas duas pontas); `foreach` em MÉTODO sobre `this.xs` (`T[]`
campo) e `this.items` (campo-array inline); alargamento implícito (`u8[]` somado num `i64`).

Gate: `--entry-only` **39/39**; `--dump-ast` das 38 anteriores **byte-idêntico** à base `5b21e790`
(`same=38 diff=0`); `mc limits ngen` `verdict ok`, `intrin` 8/16 em ambos (zero intrínseco novo),
`passes` 13/13 (zero pass nova, tudo por sintaxe pura em `teko_loop.mc`). Probes fora de `tests/`,
descartadas: `foreach` sobre escalar; `in` faltando; `var x in xs` (recusado pelo núcleo, `type
expected`); tipo estreito (`u8 x in i64[]`); `x = 99;` no corpo -- **não recusado** (roda seguro,
só não é o `readonly` do C#, registrado como dívida, não como falha).

**Dívidas -- consolidado da série §41 inteira (K1-K5), nada escondido:** lambda aninhada com
localização de erro errada (não investigada); `return (…) => e;`/`return x => e;` (`return` é
palavra do núcleo, sem hook); `T[]` como GLOBAL (tipo aceito, leitura/escrita/`.Length` não);
`params T[]`; `ref`/`out T[]`; `T[][]`/multidimensional; `&`-captura de tipo CONTADO; atribuição-
por-CAMINHO para `out`; `f(out i64 a)` inline; `Func<>`/`Action<>`; `+=`/`-=` de delegate
(multicast); `x = 99;` num `foreach` não recusado (K5); `p.items`/`p.xs` como fonte de `foreach`
sobre um PARÂMETRO (só LOCAL/`this` resolvem, dívida herdada de K3/D219); captura de um PARÂMETRO
da função declarante; grafia contextual/curta de lambda como argumento de função LIVRE ou método
SOBRECARREGADO; `op.Invoke(x)`; covariância/contravariância de delegate; `delegate` genérico.

A série do §41 (closures, ponteiro de função, `ref`/`out`, `T[]` de heap, `foreach`) está FECHADA.
Próxima onda: revisar a lista de dívidas acima com o dono para priorizar o que entra no plano
seguinte (D214 segue mandando primitivas->tipos->superfície; a dívida de maior alavancagem
aparente é `T[]` como GLOBAL, por destravar `params T[]` em seguida).

## 50. Ordem LIVRE de declaração de tipos; herança de interface; `T[]` global — desenho (architect-first, 2026-09-05)

Escopo: a dívida do verificador do C6 (§5 do handoff — "o `ngen` é um parser de UMA passada: um tipo
precisa estar declarado ANTES do primeiro uso"), mais dois itens pequenos da mesma onda. Lei: D226
(C#/mercado), D213 (reusa o núcleo, ensina só o delta), D216 (trait não é tipo), D214, D218/D220/D221.
MEDIDO no código, não presumido: `mc/src/lex.mc:474`/`:692` (`lex_push_mem`/`lex_init` põem
`cp`/`cend` na fonte empurrada — logo, em `user_init`, `p_cp()`..`p_src_end()` É o arquivo de entrada
inteiro); `mc/src/ast.mc:350` (`--dump-ast` imprime `type=<nome>`, NUNCA o id de `type_new`);
`mc/src/parse.mc:226` (`cur_name` = fatia crua da fonte: serve para pontuação e palavra reservada); e
`ngen/teko_iface.mc:242` — `tk_call2("tk_itab", vt, tk_int(si))`: **o índice da linha `sr_*` é EMITIDO
na árvore**, o fato que governa o desenho abaixo.

### (a) Decisões

1. **Pré-registro de NOMES por varredura léxica da fonte (opção A), não hook novo do mc (opção D).**
   O sítio que falha (`Box b = new Box();` antes de `class Box`) é lido pelo núcleo como expressão
   porque `Box` ainda não é palavra. Um `syntax_ident` do mc NÃO resolveria sozinho: para responder
   "isto é um tipo" o handler varreria o resto da unidade de qualquer jeito — o hook só mudaria ONDE a
   varredura dispara. A varredura é a causa-raiz atacada, sem dependência do mc.
2. **A varredura registra a PALAVRA; a linha `sr_*` continua nascendo tarde.** `type_new` +
   `syntax_expr`/`syntax_stmt` (e `tk_ns_register` do nome curto) na varredura; a linha `sr_*` só é
   materializada na DECLARAÇÃO real ou no primeiro USO. Motivo duro: o índice da linha é emitido
   (`tk_itab`), então criar linhas na varredura reordenaria os itabs e quebraria o gate de AST das 39
   fixtures. Num programa já em ordem o primeiro uso vem DEPOIS da declaração — a ordem das linhas
   fica byte-a-byte a de hoje. O id de `type_new` pode mudar de ordem: não é emitido.
3. **A varredura só ADICIONA; na dúvida, não registra** — nunca recusa, nunca reporta erro, nunca
   consome token; nome já reclamado (keyword do núcleo, `alias_find`, outra registração) não é
   registrado e a declaração real dá a mensagem de sempre. **Regras do varredor** (autômato de 5
   estados, ~90 linhas): pula `//`, `/* */`, `"…"`, `'…'` e a LINHA de uma diretiva `#`; conta chaves;
   segue `namespace A.B {`/`namespace A.B;` para formar o nome qualificado; acumula `public`/`internal`/
   `abstract`/`partial` pendentes; ao ver `class|struct|interface|trait|delegate` **na profundidade de
   chave do namespace corrente** (nunca dentro de um corpo — tipo aninhado não é ensinado, D220) grava
   {nome qualificado, kind, vis, arquivo, linha, span}. Nome seguido de `<` é GENÉRICO → não registra.
4. **`tk_fwd_init()` é a PRIMEIRA linha de `user_init()`** — `tk_loop_init()` empurra o prelúdio e daí
   em diante `p_cp()` aponta para ele, não para a entrada (armadilha nova, §5.1).
5. **O arquivo de um `import` é varrido no `tk_import`,** logo depois de `lex_include(...)` devolver 1
   (o push já pôs `cp`/`cend` nele, medido); `#include "x.tk"` cru do núcleo não é varrido — dívida
   declarada; teko escreve `import`.
6. **Linha PFWD.** A linha materializada por uso carrega `sr_part = TK_PFWD` (quarto estado, ao lado
   de PWHOLE/POPEN/PDONE) e o `sr_form` que a varredura leu. A declaração real ADOTA a linha
   (`tk_fwd_adopt`) em vez de criar outra; `tk_newname` ganha o ramo "a palavra é um forward ainda NÃO
   declarado → aceita"; `tk_class_reopen` idem, para o `partial` cujo primeiro uso materializou.
7. **Quem precisa só da IDENTIDADE resolve no parse; quem precisa do CORPO defere para um `pass()`.**
   É a fronteira única do desenho, e a tabela (b2) enumera todo sítio.
8. **`.` sobre linha PFWD reusa o deferimento que JÁ existe** (`tk_defer_member`/`tk_pend_*`,
   teko_typeof.mc): no `pass` a linha está completa e `tk_ty_of` tipa o receptor pelo id declarado —
   resolve pelo TIPO, nunca pelo nome (a precisão que o K3 mediu e exigiu).
9. **`new` sobre linha PFWD defere com placeholder** `tk_unresolved_new` (o idioma de
   `tk_unresolved_member`/`tk_unresolved_array`): construtor, recusa de `abstract` e `tk_close_open`
   acontecem no `tk_fwd_pass`; o nó já é tipado no parse (`tk_xt_add(n, si, 0)`), logo oráculo e RC não
   perdem nada, e o que o pass não resolver morre em `call to unknown function`.
10. **`Tipo.membro` estático sobre linha PFWD defere pela mesma máquina** (`tk_fwd_static`): nome do
    membro e argumentos lidos no parse (o parser é quem tem os tokens), nó construído no pass.
11. **Base/interface declarada DEPOIS materializa a declaração no ATO, no sítio da lista `:`** — não é
    deferível: `tk_base_take` precisa de `sr_size`, slots e itab da base para dispor o objeto derivado.
    `tk_fwd_materialize(fi)` empurra o TEXTO gravado pela varredura com `p_push_source` e chama
    `parse_top()` UMA vez. O token gasto pelo `p_next()` do contrato de push (`hooks.md` §4) é o próprio
    nome da base, e ele é REGENERADO: o texto empurrado é `<declaração da base>` + ` ` + `<lexema do
    token corrente>` (`p_name()`). Alcançada a declaração real mais abaixo, a linha já está adotada
    DAQUELE span → `tk_class`/`tk_interface` PULA a declaração (`tk_fwd_skip_decl`), sem mensagem.
12. **Materialização por demanda só com nome BARE e no MESMO namespace** que a varredura gravou;
    qualificada (`geo.B`) ou de outro namespace cai na recusa de hoje ("unknown base class or
    interface"). Dívida estreita e honesta, não silêncio.
13. **Trait não precisa de espera: a varredura grava o SPAN do corpo** e preenche a linha da tabela de
    traits (nome, texto, len, vis, proj). `use T;` antes de `trait T` funciona com a máquina de
    `tk_flatten` intacta; a declaração real sobrescreve o texto com o span autoritativo do
    `p_skip_balanced`. D216 segue valendo: trait não ganha `type_new`.
14. **Ciclo** (`class A : B` / `class B : A`) é recusado onde a materialização recursa
    (`tk_fwd_cycle_check`, pilha de nomes em materialização): `teko: base class cycle`. Hoje é
    inescrevível, logo é recusa estritamente nova. **Forward usado e NUNCA declarado** (só por falso
    positivo da varredura) é recusado no `tk_fwd_pass`: `teko: <nome> is used but never declared`.
15. **Herança de interface NÃO achata o `im_*`; grava ARESTAS.** `interface I2 : I1` registra o par por
    `tk_impl_add(i2, i1)` (a tabela `(classe, interface)` já existe e nada exige que o "dono" seja
    classe); a classe que conforma a `I2` conforma ao fecho transitivo, o itab ganha uma linha por
    interface e o upcast `I1 x = <valor I2>` é NO-OP em runtime (o valor é o ponteiro do objeto; o sítio
    busca pelo id da interface ESTÁTICA). Achatar duplicaria a assinatura no diamante — que é **ACEITO,
    como em C#** (`I3 : I1, I2`, ambas com `m()`): a classe implementa `m` uma vez e as duas tabelas de
    método apontam para o mesmo símbolo; `tk_impl_has` já deduplica o conjunto.
16. **`T[]` GLOBAL resolve num `pass()`**, o precedente MEDIDO deste mesmo módulo para array fixo global
    (`teko_array.mc`: `on_stmt` não vê declaração de topo e não há hook sobre uma); a rota alternativa —
    farejar a posição no `syntax_type` — exige lookahead de DOIS tokens em bytes para separar `i64[] g;`
    de `i64[] f()` e criaria duas fontes de verdade. **O global é RAIZ:** guarda a referência
    corretamente e nunca é liberado, como o campo `static` de tipo classe. Nada de RC novo.

### (b1) Hook → uso

| hook / API | uso |
|---|---|
| `p_cp()` / `p_src_end()` em `user_init` e no `tk_import` | os bytes que a varredura lê (medido) |
| `type_new(nome, 8, 8, TK_INT)` + `syntax_expr`/`syntax_stmt` | a palavra do tipo, antes do 1º token (O1) |
| `tk_ns_register` (teko_ns.mc) | o nome CURTO de um tipo namespaced, a registração de hoje |
| `p_push_source` + `p_name()` (regeneração do lexema) + `parse_top()`; `p_skip_balanced` | materialização da base declarada abaixo, e o PULO da declaração já materializada (O3) |
| `pass(&tk_fwd_pass)` / `pass(&tk_array_pass)` estendido | `new`/estático/ciclo (O2); `g[i]`, `g[i] = e` (G1) |
| `tk_defer_member`/`tk_pend_*` (teko_typeof.mc) | `.` sobre linha PFWD, sem uma linha nova (O2) |
| `tk_impl_add`/`ci_if`/`ci_cls` (teko_iface.mc) | as arestas `I2 : I1` e o fecho na classe (I1) |

### (b2) Sítios que consultam `sr_*` no PARSE, e o que cada um passa a fazer

| sítio | precisa de | com linha PFWD |
|---|---|---|
| `tk_type_stmt` → `parse_var` (teko_access.mc:350) | só o id | resolve: identidade basta |
| `tk_gen_ty`/`p_type()` — campo, parâmetro, retorno, cast | id + `type_width` (8 p/ toda referência) | resolve |
| `tk_on_stmt` (teko_struct.mc:694) | a linha | materializa PFWD com o kind da varredura |
| `tk_is_counted` (teko_struct.mc:492) / `teko_rc.mc`; `tk_ha_row` (`:507`) | o kind; o id do elemento | resolve (kind conhecido desde a varredura) |
| `tk_dot` → `tk_member_of` (teko_expr.mc:300) | campos, props, métodos | DEFERE (`tk_defer_member`, já existe) |
| `tk_new` → `tk_new_pick` (teko_expr.mc:29) | construtores, `abstract`, `tk_close_open` | DEFERE (`tk_unresolved_new`) |
| `tk_type_expr` → `tk_static_member` (teko_access.mc:298) | const/campo/método estático | DEFERE (`tk_fwd_static`) |
| `tk_deleg_var_stmt`/`tk_deleg_coerce` (teko_deleg.mc) | assinatura `dg_*` | `parse_var` puro; coerção no `tk_deleg_pass` |
| `tk_conf_name` + `tk_base_take` (teko_class.mc:1063/1277) | `sr_size`, slots, itab | MATERIALIZA no ato (O3) |
| `tk_use` (teko_trait.mc:296) | texto do trait | a varredura já gravou o span (O1) |
| `tk_fe_source` (teko_loop.mc, `foreach`) | ety/len no parse | local/`this` só — global e forward = dívida |

### (c) Sequência de crumbs

**O1 — pré-registro de nomes, linha PFWD, adoção.** Arquivos: `ngen/teko_fwd.mc` (novo), `teko.mc`
(`#include`, `tk_fwd_init()` como PRIMEIRA linha de `user_init`, `pass(&tk_fwd_pass)` logo depois de
`tk_ns_pass`), `teko_struct.mc` (`TK_PFWD`, ramo de `tk_newname`, adoção em `tk_type_add`),
`teko_access.mc` (`tk_type_word` idempotente), `teko_ns.mc` (a varredura chama `tk_ns_register`),
`teko_trait.mc` (linha adotada do span), `teko_class.mc`/`teko_iface.mc`/`teko_deleg.mc` (adotam).
Assinaturas: `void tk_fwd_init(); void tk_fwd_scan(uptr p, uptr end, uptr file); i64 tk_fwd_find(uptr
qname); i64 tk_fwd_ty(i64 fi); i64 tk_fwd_kind(i64 fi); i64 tk_fwd_row(uptr qname); void
tk_fwd_adopt(i64 si, i64 vis, i64 proj); i64 tk_fwd_pass(i64 root);`
Fixture: `ngen/tests/order_types.tk` (`expect-exit: 42`) — campo, parâmetro e retorno de tipo
declarado ABAIXO; local `Box b = null;` de tipo posterior; `Op f = null;` de delegate posterior;
`use T;` acima de `trait T`; classe em `namespace` usada antes pelo nome curto via `using`.
Gate: 5 pernas; `--entry-only` 40/40; `--dump-ast` das 39 anteriores **byte-idêntico**; `mc limits
ngen` `ok`, `intrin` sem crescimento, `passes` +1. Probes fora de `tests/`: nome de tipo usado como
variável antes da declaração (recusa; divergência do C# documentada); `class` em string e em
comentário (não reserva palavra — `limits` é a régua).

**O2 — usos que precisam do CORPO, deferidos.** Arquivos: `teko_expr.mc` (`tk_dot` desvia para
`tk_defer_member` quando `sr_part == TK_PFWD`; `tk_new` desvia para o placeholder), `teko_access.mc`
(`tk_type_expr` estático), `teko_fwd.mc` (as duas tabelas e a resolução), `teko_deleg.mc` (coerção
contextual adiada). Assinaturas: `i64 tk_fwd_defer_new(i64 si, uptr name, i64 line, uptr fl); i64
tk_fwd_defer_static(i64 si, i64 line, uptr fl); void tk_fwd_resolve_new(i64 i); void
tk_fwd_resolve_static(i64 i);`
Fixture: `order_types.tk` cresce — `Box b = new Box(41); b.bump(); return b.get() + Box.made;` com
`class Box` no FIM do arquivo, construtor com argumento, `static`/`const` por `Tipo.X`, e método
chamado sobre parâmetro de tipo posterior.
Gate: 40/40 (a mesma fixture cresce, nenhuma nova); AST das 39 anteriores byte-idêntica; `limits ok`.
Probes: `new` de tipo posterior `abstract` (recusa no pass, com a posição do SÍTIO); membro
inexistente (`unknown member of Box`); forward nunca declarado (decisão 14).

**O3 — base/interface declarada depois.** Arquivos: `teko_class.mc` (`tk_conf_name` espia o nome antes
de consumir; `tk_class` pula a declaração já materializada), `teko_iface.mc` (idem), `teko_fwd.mc`
(materialização, pilha de ciclo, pulo). Assinaturas: `i64 tk_fwd_materialize(i64 fi); void
tk_fwd_replay(uptr text, i64 len, uptr frame); i64 tk_fwd_skip_decl(i64 fi); i64 tk_fwd_in_flight(uptr
qname);`
Fixture: `ngen/tests/order_bases.tk` (`expect-exit: 42`) — `class Dog : Animal` ACIMA de `class
Animal` (campo da base, `override` de método virtual, `base.m()`), `class Sq : IShape` acima de
`interface IShape`, cadeia de três níveis fora de ordem, `rt_live()` de volta ao piso.
Gate: 41/41; AST das 40 anteriores byte-idêntica; `limits ok`. Probes: ciclo `A : B`/`B : A`; base
qualificada declarada abaixo (recusa de hoje); base de outro namespace (recusa de hoje).

**I1 — herança de interface.** Arquivos: `teko_iface.mc` (`: I1, I2` na declaração; membro pelo fecho;
`tk_iface_conf_close`), `teko_class.mc` (conformidade e tabelas pelo fecho), `teko_typeof.mc`
(`tk_ifmeth_by_name` enxerga as bases). Assinaturas: `void tk_iface_conf(i64 si, i64 proj); i64
tk_iface_nbase(i64 si); i64 tk_iface_base_at(i64 si, i64 k); void tk_iface_conf_close(i64 ci, i64 fi);
i64 tk_ifmeth_find_deep(i64 si, uptr m, uptr pdecl);`
Fixture: `ngen/tests/surface_iface_inherit.tk` (`expect-exit: 42`) — `interface I2 : I1`, classe
implementando só `I2` e respondendo aos dois; `I1 x = <valor I2>` chamando o método de `I1`; corpo
default herdado de `I1`; diamante `I3 : I1, I2` com `m()` repetido e uma implementação só.
Gate: 42/42; AST das anteriores byte-idêntica; `limits ok`. Probes: base de interface que é
classe/struct/trait (recusa); ciclo `I1 : I2`/`I2 : I1`; classe que não implementa o método herdado.

**G1 — `T[]` GLOBAL.** Arquivos: `teko_array.mc` (tabela `hg_*` dentro do mesmo `tk_array_pass`),
`teko_heaparr.mc` (endereço/carga/armazenamento reusados), `teko_typeof.mc` (o oráculo responde o tipo
de um global **só** quando é linha `TK_KARRAY`), `teko_params.mc` (`tk_bracket` já deixa o `N_INDEX`/o
placeholder — zero linha nova). Assinaturas: `void tk_hg_collect(); i64 tk_hg_find(uptr name); void
tk_hg_rewrite_index(i64 n); i64 tk_ty_global_ha(uptr name);`
Fixture: `ngen/tests/surface_array_global.tk` (`expect-exit: 42`) — `i64[] g;` no topo, `g = new
i64[n];` com `n` de runtime, `g[i]`/`g[i] = e`/`+=`/`++`, `g.Length` em `while` e em `for`,
`u8[]`/`i32[]` provando largura e sinal, `Circle[] cs` global (raiz: `rt_live()` acima do piso, DITO na
fixture) e um `T[]` global em `namespace`.
Gate: 43/43; AST das anteriores byte-idêntica; `limits ok`. Probes: `g.Length = 3` (só-leitura);
índice além do fim (guard de runtime do K3, exit 70); `foreach` sobre `T[]` global (recusa clara).

### (d) Fora do escopo, e o pedido ao mc

**Nenhum pedido BLOQUEIA este bloco** (decisão 1). Um pedido OPCIONAL, de conveniência:

> **Pedido (ngen → mc, não bloqueante): `on_source(&fn)`, avisar o módulo de toda fonte empurrada.**
> Handler `void f(uptr name, uptr src, i64 len)`, chamado de `lex_push_mem` logo depois de `cp`/`cend`
> mudarem. Hoje o ngen varre a entrada em `user_init` e o arquivo de um `import` dentro do próprio
> handler (que chama `lex_include`), mas um `#include "x.tk"` do NÚCLEO nunca é visto — a varredura que
> dá ordem livre de declaração fica cega para os tipos daquele arquivo. Com o hook, a mesma varredura
> cobre toda fonte: incluída, embutida (`<bundle>`) ou empurrada.

**Dívidas declaradas** (nada escondido): `#include "x.tk"` cru não varrido (use `import`); base ou
interface QUALIFICADA (`geo.B`) declarada abaixo, e base em OUTRO namespace (decisão 12); lambda na
grafia contextual contra um `delegate` declarado abaixo (use `new Op(...)`); `foreach` sobre `T[]`
global e sobre um forward (a fonte do `foreach` resolve no parse); `b.x += 1` sobre receptor deferido
(herdada); tipo aninhado segue não ensinado (D220); `T[]` global nunca liberado (decisão 16);
`params T[]`, `T[][]`, `ref`/`out T[]` (herdadas do §41); covariância de interface e `I1 x = <valor
I2>` em posição de ARGUMENTO sobrecarregado (a sobrecarga não conhece o fecho de interfaces).

### (e) Riscos

1. **A régua é a ordem das linhas `sr_*`, porque o índice é EMITIDO** (`tk_int(si)` em `tk_itab_emit`,
   teko_iface.mc:242). A decisão 2 existe por isso: linha materializada tarde = ordem idêntica à de
   hoje para programa já em ordem = AST byte-idêntica. Um crumb que criar linha na varredura deixa o
   gate VERMELHO nas fixtures com interface — e é o sinal certo.
2. **Falso positivo do varredor reserva palavra** que o programa usava como identificador. Contido pela
   decisão 3, pelo gate de AST e por `mc limits ngen` (palavra a mais = `grow`); o modo de falha é
   mensagem do núcleo (`name reserved by a syntax/type_alias registration`), nunca miscompilação.
3. **Ordem de `user_init`** (decisão 4): varrer depois do prelúdio de `teko_loop.mc` varre o PRELÚDIO.
   É o primeiro item que o implementador confirma por probe (os 40 primeiros bytes vistos).
4. **O token gasto pelo push (O3).** O contrato de `p_push_source` come exatamente um token; a
   regeneração pelo lexema (decisão 11) o devolve. `T_EOF` não precisa (o lexer o reproduz ao esvaziar
   o frame) e o token regenerado carrega o `p_file()`/`p_line()` do frame — desvio cosmético em UM
   token. **Reentrância**: base de base fora de ordem recursa, e a pilha da decisão 14 dá terminação em
   vez de estouro (o precedente medido do N1, §5.1 item 16).
5. **Diagnóstico que migra de parse para pass** (O2): a posição TEM de viajar nas tabelas (`line`/
   `file`, como `pd_line`/`gd_line` já fazem), senão a recusa aponta para o fim do arquivo.
6. **`partial` + forward**: um uso pode materializar a linha antes da primeira parte, e `tk_class_reopen`
   tem de adotar PFWD em vez de acusar "the type is declared without `partial`". **Genérico**: nome
   seguido de `<` nunca é pré-registrado (decisão 3), senão colide com "the name is already a type".
7. **Emissão em outra ordem** para o programa que USA a liberdade nova (a base materializada é emitida
   no sítio que a pediu): correto para o linker, invisível para as 39 fixtures, e reconfirmado por
   `--dump-ast` a cada crumb.

## 51. Errata — O1 landado (2026-09-05)

O crumb O1 (§50 (c)) landou como desenhado, com dois ajustes medidos contra o código real:

1. **A gate `depth == top_depth` (decisão 3) não é uma constante fixada uma vez** — `top_depth` é
   **0 fora de qualquer namespace ou dentro de um `namespace A.B;` file-scoped, e `ns_depth`
   (a profundidade logo após o `{` do bloco) dentro de um `namespace A.B { ... }`**, recomputado a
   CADA palavra candidata da varredura (`ns_block`/`ns_depth` mudam ao longo do arquivo). Uma
   primeira versão fixava `top_depth = 0` uma vez e nunca a atualizava ao entrar num bloco de
   namespace — nenhuma classe DENTRO de um `namespace X { ... }` era registrada. Corrigido antes de
   landar (nenhuma fixture chegou a ver o bug).
2. **`p_file()` não responde nada útil no ponto em que a varredura roda** (nem na primeira linha de
   `user_init`, antes do primeiro token, nem logo após um `import` empurrar um arquivo — o
   contrato de `p_push_source` não toca o lookahead pendente). `lex_file()` é a resposta certa nos
   dois pontos (HANDOFF.md §5.1 item 20). Sem isso, o `tk_origin_of_file` que decide o `proj`
   default de um placeholder saía 0 (ou segfaultava, se algo tentasse ler o ponteiro nulo como
   string) — só apareceu porque `use T;` acima de `trait T` compara essa origem contra a da classe
   que usa o trait.
3. **`tk_fwd_pass` (decisão 14) não precisa de um bit "adotado" separado** — `sr_part_at(si) ==
   TK_PFWD` no fim da unidade já é exatamente "materializado por um uso, nunca adotado"; qualquer
   outro estado (inclusive `TK_PWHOLE` de uma declaração nunca usada antes de si mesma, o caminho
   comum das 39 fixtures) é "resolvido". Uma tabela `fw_adopted` paralela, setada só no ramo de
   adoção de `tk_type_add`, dava falso positivo para TODO tipo declarado na ordem normal (HANDOFF.md
   §5.1 item 21) — corrigido antes de landar.

Fora isso, o desenho do (a)-(e) resistiu sem desvio: a régua de ordem das linhas `sr_*` (risco 1)
segurou (`--dump-ast` das 39 anteriores byte-idêntico, `same=39 diff=0`), o ramo de `tk_newname`
aceita a palavra pendente exatamente como a decisão 6 previu, e o backstop do risco 2 (falso
positivo do varredor) não disparou em nenhuma das 40 fixtures nem nos quatro probes -- inclusive o
próprio probe pensado para `tk_fwd_pass` (decisão 14) não encontrou um programa Teko-sobre-mc
sintaticamente válido que dispare SÓ esse backstop sem abortar antes por outro erro, o que é o
sinal esperado de um desenho correto, registrado em HANDOFF.md em vez de forçado.

## 52. Errata — O2 landed (2026-09-06)

O crumb O2 (§50 (b2)/(c)) landou como desenhado — `.`/`new`/`Tipo.membro` estático sobre uma linha
`TK_PFWD` deferem para `tk_fwd_pass` — e fechou duas das três ressalvas do verificador do O1, com um
achado que o (b2) não previa:

1. **Ressalva 3 (a mais séria) fechou por guarda no CALL SITE, não por filtrar `tk_struct_find`
   globalmente.** A primeira ideia — fazer `tk_struct_find` ignorar `TK_PFWD` para todo chamador —
   quebraria os sítios de COLISÃO (`teko_generic.mc`/`teko_trait.mc` checando "o nome já é um tipo"
   antes de registrar um genérico/trait): esses PRECISAM enxergar a linha PFWD, porque o nome já
   está reservado. A correção ficou nos DOIS sítios que precisam do corpo e nada mais —
   `tk_deleg_find` (teko_deleg.mc) e `tk_conf_name` (teko_class.mc) — cada um tratando
   `sr_part_at(si) == TK_PFWD` como "não encontrado" localmente.
2. **Ressalva 2 (`A.Item` qualificado acima da declaração) escondia um segundo problema, fora do
   (b2): o SEGMENTO (`A`/`geo`) só vira palavra reservada (`tk_ns_seg_register`) quando a
   declaração REAL de `namespace A { ... }` é lida — nunca pela varredura.** Sem isso, `A.Item`
   escrito acima do namespace nem chega a `tk_ns_seg_stmt`; o núcleo lê dois tokens soltos e erra
   antes de qualquer tabela de forward existir. Fechado ensinando `tk_fwd_try_namespace` a chamar
   `tk_ns_seg_register(seg0)` no mesmo instante em que reconhece o cabeçalho (idempotente, mesma
   função que a declaração real chama) — só então `tk_ns_walk`/`tk_ns_seg_stmt`/`tk_ns_seg_expr`
   ganharam o tratamento de PFWD que a decisão 8/10 já dava ao caminho não-qualificado. Registrado
   em HANDOFF.md §5.1 item 22.
3. **Ressalva 1 (`#include "x.tk"` cru) permanece dívida — não fechada.** Nenhuma forma encontrada
   de varrer o arquivo incluído sem tocar o núcleo do `mc` (não há hook para um `#include` cru, e
   ler o arquivo do disco por fora do lexer exigiria uma primitiva de I/O que a superfície atual não
   dá a um módulo). O pedido `on_source(&fn)` do §50(d) resolveria isto de graça — não é um pedido
   NOVO, o mesmo já registrado permanece a resposta certa.
4. **Coerção de delegate posterior (`Op f = add;` com `delegate` abaixo) não precisou de código
   novo** — o (b2) já descrevia o estado correto (`tk_deleg_var_stmt`/`tk_deleg_coerce` rodam tarde
   o bastante, em `tk_deleg_pass`), só faltava provar com uma fixture.

Gate: `--entry-only` 40/40 (nenhuma fixture nova, `order_types.tk` cresceu); `--dump-ast` das 39
anteriores byte-idêntico (`same=39 diff=0`); `mc limits` `verdict ok`, `intrin` 8/16 nos dois lados
(zero intrínseco novo), `passes` 14/13 — a mesma `tk_fwd_pass` do O1, sem `pass()` nova.

## 53. Errata — O3 landed (2026-09-06)

O crumb O3 (§50 (c)) landou como desenhado, com um ponto que o (a)/(b2) deixavam em aberto (decisão
6, partial) resolvido e registrado, e um achado de posicionamento de código que o (c) não previa:

1. **A varredura ganhou o SPAN, não só a palavra (extensão aditiva sobre a decisão 2).** A decisão 2
   dizia que a LINHA (o row do type table) nasce tarde, para não reordenar o índice que `tk_itab`
   emite — isso continua intocado. O que o O3 acrescenta é ORTOGONAL: o SPAN de texto de cada
   `class`/`struct`/`interface` escaneada (`kwstart` até o byte depois do `}` que fecha o corpo,
   reaproveitando o mesmo `tk_fwd_brace_end` que `tk_fwd_try_trait` já usava para um trait) é
   capturado no MESMO passe da varredura, sem criar nenhuma linha — só o texto, guardado à parte
   (`fw_span`/`fw_spanlen`), para `tk_fwd_materialize` reler depois. Uma SEGUNDA ocorrência escaneada
   do mesmo nome qualificado (`fw_multi`) marca o candidato como não-materializável, em vez de
   sobrescrever o span com a parte errada.
2. **Decisão 6 (`partial` + forward) resolvida: materializa só com UMA parte; duas ou mais recusam.**
   Um `partial class Foo` com uma só parte no arquivo inteiro materializa normal — o span dessa parte
   É a declaração inteira, e a palavra `partial` em si vira um no-op no replay (ela nunca é lida:
   o texto pushado começa em `class`, não no modificador). Duas ou mais partes ESCANEADAS
   (`fw_multi`) recusam a materialização com uma mensagem dedicada (`a partial base is not
   forward-declarable yet`) — juntar os spans de partes espalhadas pelo arquivo não tem um único
   lugar para apontar erros, e a decisão foi não inventar essa costura agora. Dívida estreita e
   honesta, do mesmo jeito que a decisão 12 já trata o nome qualificado/de outro namespace.
3. **`tk_fwd_materialize` não mora em `teko_fwd.mc` — mora em `teko_class.mc`, achado que o (b1)/(c)
   não previram.** O estado que o save/restore ao redor do `p_push_source` precisa
   (`tk_own_methods`/`tk_nconf`/`tk_ntu`/`tk_nud`/`tu_tr`/`ud_tr`, de `teko_trait.mc`) só existe a
   partir do include de `teko_trait.mc`, que vem DEPOIS de `teko_fwd.mc` na cadeia de `teko.mc`. Uma
   função forward-se-declara neste código (o padrão já usado por `tk_trait_scan`/`tk_ns_register`),
   mas uma variável global não — então a função que precisa delas tem de morar textualmente depois de
   onde elas nascem. `teko_class.mc` é o primeiro arquivo da cadeia onde isso já vale, e é também
   onde o único chamador (`tk_conf_name`) mora, então a função entrou ali; a tabela do scanner
   (`fw_*`) e o que não depende desse estado (`tk_fwd_skip_decl`, a pilha de voo
   `tk_fwd_in_flight`/`tk_fwd_flight_push`/`tk_fwd_flight_pop`) continuam em `teko_fwd.mc`.
4. **O ciclo termina, mas não pelo caminho mais curto (risco 4 do §50(e), confirmado).** Para `class
   A : B` / `class B : A`, a materialização de `B` (a partir do uso de `A`) recursa na materialização
   de `A` — que ainda não tem uma linha (o `tk_type_add` de `A` só roda depois que a lista `:` dela
   termina de ser lida), então essa segunda materialização de `A` REENTRA de verdade, e é só na
   materialização de `B` que ela dispara (`B` já está em voo nesse ponto) que o ciclo é detectado.
   Termina (a pilha da decisão 14 limita pela profundidade do ciclo, não estoura), mas com uma
   passada a mais do que o mínimo teórico — aceitável, já que o programa nunca compilaria de outra
   forma, e é exatamente o modo de falha que o risco 4 já havia previsto.
5. **O item pequeno da ressalva do O2 (§52 nota final) fechou ensinando, não recusando.** `h.items[0]
   = 9` sobre um campo-array alcançado por um receptor cujo tipo só o pass conhece (parâmetro/local
   de tipo PFWD) ganhou `TK_PIXLOAD`/`TK_PIXSTORE` em `teko_typeof.mc`, lendo o índice no mesmo ponto
   que a chamada/atribuição já eram lidas em `tk_defer_member`, e reaproveitando `tk_ax_index`/
   `type_width` (teko_struct.mc) em `tk_pend_field` para o endereço e o guard — as três recusas do
   caminho estático (não é array / lido um elemento por vez / atribuído um elemento por vez)
   reproduzidas sem linha de runtime nova. Coube em bem menos que 40 linhas.

Gate: `rm -rf ngen/build`, build do zero; `--entry-only` **41/41** (as 40 anteriores + `order_bases`);
`--dump-ast` das **40 anteriores byte-idêntico** ao compilador da base `7113acbd` (`same=40 diff=0`);
`mc limits` `verdict ok`, `intrin`/`passes`/`syntax` idênticos nos dois lados (zero intrínseco, zero
pass, zero palavra nova).

**O3b (reprovação corrigida, 2026-09-06):** `kwstart` (item 1) parava na palavra `class`/`struct`/
`interface`, deixando `public`/`internal`/`abstract`/`partial` fora do span materializado -- uma
base `abstract` abaixo do uso nascia concreta. `tk_fwd_scan` agora recua `kwstart` sobre a sequência
de modificadores contígua antes da palavra; `tk_class`/`tk_interface` validam `vis`/`abst` contra a
linha materializada ao pular a declaração real (`tk_fwd_check_materialized`, backstop defensivo).
Gate: `--entry-only` **41/41**; `--dump-ast` das 40 fixtures não tocadas `same=40 diff=0` contra
`d23b17ec`; `mc limits` `verdict ok`.

## 54. Errata — I1 landed (2026-09-06)

O crumb I1 (§50 (c)) landou como desenhado, com dois pontos que o (c) deixava em aberto e um erro
de ordenação achado e corrigido antes de landar.

1. **`: I1, I0` da interface tem de ler ANTES de `set_sr_m0_at` marcar onde as assinaturas PRÓPRIAS
   de `I2` começam.** Uma base abaixo materializa DENTRO da leitura da lista, e a materialização
   acrescenta as assinaturas dela na MESMA tabela compartilhada (`im_*`) antes de `I2` ter lido as
   suas — marcar o watermark antes da lista dobraria o range da base materializada sobre o de `I2`,
   fazendo `sr_mn_at(I2)` contar métodos que não são dela. Primeira versão marcava antes; corrigido
   (nenhuma fixture chegou a ver o bug, achado por um probe combinando herança + base abaixo).
2. **O fecho é ADITIVO na mesma tabela `(classe, interface)` de sempre, não uma tabela nova (decisão
   15 do §50, confirmada sobre código real).** `tk_iface_conf_close(ci, fi)` grava `fi` e copia,
   SEM recursão, o conjunto já fechado de `fi` -- porque esse conjunto, por invariante, já é a
   transitiva inteira (toda vez que uma interface fecha uma base, ela também herda o que a base já
   tinha fechado). Isso faz `tk_iface_nbase`/`tk_iface_base_at` (o "base list" de uma interface) SER
   o mesmo `sr_ni_at`/`tk_impl_index` que uma classe já usa para o próprio itab -- zero tabela nova,
   zero campo novo além de `ci_via`.
3. **O ciclo não passa pela pilha de materialização do O3 -- passa pelo PRÓPRIO fecho.**
   `tk_fwd_in_flight` só guarda o lado que está em replay; o outro lado, cujo `tk_type_add` já
   rodou (é a declaração REAL, não uma materialização), resolve como consulta normal e não aparece
   na pilha. `tk_iface_conf_close` recusa examinando o conjunto já fechado do OUTRO lado
   (`tk_impl_has(fi, ci)`) antes de gravar -- quem fecha por último sempre encontra o primeiro já
   apontando de volta, porque fechar o primeiro achatou a cadeia nele. Mais barato que uma pilha
   nova e não precisa saber se o outro lado veio de replay ou de declaração direta.
4. **Despacho fundo em TRÊS sítios, não um** (achado ao rodar o probe do `.` sobre valor `I2`
   chamando membro só de `I1`): `tk_iface_call` (teko_expr.mc, receptor de tipo já resolvido no
   parse), `tk_pend_iface` (teko_typeof.mc, receptor que só o `pass()` tipa) e `tk_this_iface_call`
   (teko_this.mc, `this` dentro do corpo default de OUTRA interface). Os três já resolviam `m` só
   dentro de `si` (`tk_ifmeth_find`); trocado por `tk_ifmeth_find_deep(si, m, pdecl)`, que devolve a
   interface que de fato declara `m` para o `tk_itab_emit` indexar a tabela CERTA -- sem isso, um
   valor tipado `I2` chamando um método só em `I1` batia em "unknown member", já que `I2` não tem a
   assinatura na própria fatia de `im_*`.
5. **`interface I2 : I1 { }` com corpo vazio deixou de ser "sem métodos".** A checagem
   `sr_mn_at(si) == 0` de `tk_interface()` não sabia de bases; ganhou `&& tk_iface_nbase(si) == 0` --
   uma interface que só agrupa outras (padrão comum em C# para nomear um conjunto) não é mais
   recusada quando as bases têm o que ela não tem.

Gate: `rm -rf ngen/build`, build do zero; `--entry-only` **42/42** (as 41 anteriores + a nova);
`--dump-ast` das **41 anteriores byte-idêntico** ao compilador da base `86bc343d` (`same=41 diff=0`);
`mc limits` `verdict ok`, `intrin`/`passes`/`syntax` idênticos nos dois lados (zero intrínseco, zero
pass, zero palavra nova). Sem PR, sem dreno -- branch `feat/ngen-i1-iface`, forward-only para
`fix/retirement`.

## 55. `on_source` (mc 0.15.3) — a varredura migra para o callback (2026-09-06)

Item pequeno, sem crumb próprio no plano além deste registro curto (dono pediu o `on_source`
depois do O1/O2/O3/I1, §5.2 do HANDOFF tem o pedido e o ack). `ngen/teko_fwd.mc` ganhou
`tk_fwd_on_source`/`tk_fwd_is_source_name`; `tk_fwd_init` agora só registra o callback
(`on_source(&tk_fwd_on_source)`) em vez de varrer a entrada na mão; `tk_import`
(`ngen/teko_ns.mc`) parou de chamar `tk_fwd_scan` depois do próprio `lex_include` -- o push já
dispara o callback registrado, por conta própria, e o filtro por sufixo `.tk` mantém fora o
que não é fonte teko (`<teko-loop-prelude>`, os frames de replay de genérico/base/trait, um
`#include "../lib/rt.mc"`). Fecha a última dívida "not scanned" do O2: um `#include "x.tk"` CRU
(fora de `import`) agora tem seu conteúdo varrido no instante do push, então um tipo declarado
nele e usado ACIMA da sua própria declaração, mas ainda dentro do arquivo incluído, resolve.

Gate: `rm -rf ngen/build`, build do zero; `--entry-only` **42/42** (nenhuma fixture nova ou
tocada); `--dump-ast` das **42 fixtures** byte-idêntico ao compilador da base `0a0bd0f4`
(`same=42 diff=0`); `mc limits` `verdict ok`, `on_source` 1 (linha nova na tabela), `intrin`
8/8 (zero intrínseco novo), `passes`/`syntax` 14/14 (nenhuma pass nova, nenhuma palavra nova).
Probe (fora de `ngen/tests/`, descartado): `box_value(Box b) { return b.get(); }` acima de
`class Box { ... }`, os dois dentro de um `#include "parts/x.tk"` cru puxado por `main.tk` --
compilador da base recusa com `type expected in parameter`; com o callback, compila e roda
(exit 42). Sem PR, sem dreno -- branch `feat/ngen-onsource`, forward-only para `fix/retirement`.

## 56. Errata I1b — propriedade herdada de interface (2026-09-06)

A ressalva que o I1 (§50/§54) deixou aberta: `tk_iface_member_of` decidia "é propriedade?" por
`tk_prop_find(si, m)` (`teko_prop.mc`), que só percorre `sr_base_at` (cadeia de CLASSE), nunca as
arestas de herança de interface (`tk_iface_nbase`/`tk_iface_base_at`) que o I1 introduziu. Uma
`interface I2 : I1` com `I1` declarando uma propriedade e `I2` não a redeclarando dava `unknown
member of I2: X` num valor tipado `I2`, embora `I1 x = s; x.X` funcionasse.

Fechado com `tk_ifprop_find_deep(si, m, pdecl)` (`teko_prop.mc`, ao lado de `tk_prop_find`),
espelhando `tk_ifmeth_find_deep` -- própria primeiro, senão recursa nas bases de interface,
devolvendo a DECLARANTE em `pdecl`. Três sítios trocados para receber a declarante em vez do tipo
estático do receptor: `tk_iface_member_of`/`tk_iface_prop_use` (teko_expr.mc, parse), `tk_pend_
iface`/`tk_pend_iface_prop` (teko_typeof.mc, pass, receptor só o oráculo tipa) e `tk_this_iface_
prop` (teko_this.mc, `X` bare dentro de um corpo default que herda a propriedade de uma base).
`tk_prop_find` em si não muda -- fazer uma CLASSE também percorrer `tk_iface_nbase` aceitaria a
propriedade abstrata de uma interface implementada-mas-não-redeclarada, despachando para um
acessor nunca compilado.

`surface_iface_inherit.tk` cresceu: `I1.Value { get; set; }`, `I2.doubled()` (default lendo `Value`
bare, só de `I1`), `Sq.Value` (auto-propriedade própria, satisfaz a conformidade), `bump_via_i2(I2
v)` (parâmetro, sítio pass-deferred). `checks()` cobre os quatro sítios.

Gate: `rm -rf ngen/build`, build do zero; `--entry-only` **42/42**; `--dump-ast` das **41 fixtures
não tocadas byte-idêntico** ao compilador da base `0a0bd0f4` (`same=41 diff=0`); `mc limits`
`verdict ok`, `intrin` 8/8, `passes`/`syntax` 14/14 (zero intrínseco/pass/palavra nova). Probe
(fora de `ngen/tests/`, descartado): o mesmo programa contra o compilador PRÉ-fix reproduz
`teko: unknown member of I2: Value` ao pé da letra; com o fix, roda. Sem PR, sem dreno -- branch
`feat/ngen-onsource`, forward-only para `fix/retirement`.

## 57. Errata — G1 landed, bloco §50 fechado (2026-09-06)

O crumb G1 (§50 (c)) landou como desenhado, com dois achados que o (c) não previa.

1. **`i64[] g;` chaveia por TIPO, não por `nd_val`.** Um `T[]` global não carrega contagem própria
   na declaração (`g = new i64[n];` vem depois, como statement comum) -- `tk_hg_collect`
   (`teko_array.mc`) varre `N_GLOBAL` cuja `nd_type` é linha `TK_KARRAY` (`tk_is_ha`), ao lado da
   varredura por `nd_val != 0` que o array FIXO já usa. As duas coexistem na MESMA sweep, dentro do
   MESMO `tk_array_pass` -- nada de pass nova.
2. **Achado real, não hipotético: `cs[i].area()` sobre um GLOBAL morria em `expression with no
   codegen`.** `cs[i]` (N_INDEX) nunca chega à árvore que `tk_array_pass` percorre quando um `.`
   segue -- `tk_defer_member` (teko_typeof.mc) defere o RECEPTOR inteiro, e o nó vira órfão
   alcançável só por `pd_recv`, exatamente como o elo interno de uma cadeia `p.inner.x`. A correção:
   `tk_pend_do` (que já persegue essa cadeia via `tk_pend_at(recv)`) ganhou UMA linha --
   `if (nd_kind(recv) == N_INDEX) tk_array_maybe_rewrite_index(recv);` -- antes de perguntar
   `tk_ty_of(recv)`, reusando o MESMO dispatcher que a sweep de leitura já usa (fixo OU heap,
   `teko_array.mc`). Sem essa linha, a resolução por NOME (`tk_pend_by_name`) até ACERTAVA o método
   (`Circle` é a única classe com `area`), mas construía a chamada sobre o `N_INDEX` cru, que o core
   nunca soube baixar -- o próprio caso que o header do K3 já tinha avisado ("`cs[i].area()` sobre um
   PARÂMETRO -- se `cs[i]` ficasse `N_INDEX` cru... resolveria por nome"), agora medido para GLOBAL
   em vez de parâmetro.
3. **`T[]` global em `namespace` fica BARE, por decisão, não por limitação descoberta tarde.**
   Qualificar cada uso do nome (`geo__pts`) exigiria que `tk_ns_pass`'s próprio rewrite de
   identificador (`tk_ns_rewrite_ident`, hoje só para `const`) consultasse `hg_*` -- mas
   `tk_ns_pass` roda ANTES de `tk_array_pass` popular essa tabela (`pass()` são registradas nessa
   ordem em `teko.mc`). Threads dessa ordem para trás (fazer `tk_hg_collect` rodar cedo o bastante
   para o sweep de namespace) tocaria a arquitetura de FASES da §50 inteira por uma dívida que
   nenhuma fixture testa (acesso cross-namespace ao global). Escolha: `tk_ns_reject_topkind` ganha
   UMA exceção (`tk_ns_topglobal_ha`) que deixa a declaração passar sem renomear -- funciona para
   todo uso BARE de dentro do próprio namespace (o caso do fixture), registrado como dívida honesta
   no próprio comentário do código, não escondida.

Gate: `rm -rf ngen/build`, build do zero; `--entry-only` **43/43** (as 42 anteriores + a nova);
`--dump-ast` das **42 anteriores byte-idêntico** ao compilador da base `20d78560` (`same=42
diff=0`); `mc limits` `verdict ok`, `intrin`/`passes`/`syntax` idênticos nos dois lados (zero
intrínseco, zero pass, zero palavra nova -- G1 só estendeu `tk_array_pass`/`tk_pend_emit`/
`tk_ty_of`/`tk_pend_do`, todos já registrados). Probes (fora de `ngen/tests/`, descartados):
`g.Length = 3` -> `teko: is read-only: Length`; `g[n]` com `n == g.Length` -> exit 70 (`teko: index
past the end of an array`); `foreach (i64 x in g)` sobre o global -> `teko: not a known array: g`
(a dívida já registrada, recusa clara). Sem PR, sem dreno -- branch `feat/ngen-g1-global`,
forward-only para `fix/retirement`.

Com G1, o bloco §50 (ordem livre de declaração, herança de interface, `T[]` global) fecha por
inteiro. As dívidas que sobram (`#include` cru não varrido, base/interface qualificada abaixo,
lambda contextual, `foreach` sobre `T[]`/forward, `b.x += 1` deferido, tipo aninhado, `T[]` global
sem release e sem qualificação de namespace, `params T[]`/`T[][]`/`ref`/`out T[]`, covariância de
interface em argumento sobrecarregado) seguem consolidadas no HANDOFF §5, para quem pegar a
próxima fila.

## 58. DI resolvida em TEMPO DE COMPILAÇÃO — `IServiceSingleton`/`IServiceScoped`/`IServiceTransient` (architect-first, D229, 2026-09-06)

Escopo: o ruling D229 — o dev marca a IMPLEMENTAÇÃO com uma das três interfaces-marcador, o registro
acontece **no ato da compilação** e a injeção é resolvida **em comptime**, sem container de runtime e
sem reflexão; a diferença para o C# é que um **Singleton PODE receber um Scoped** (o Singleton tem
escopo PRÓPRIO) e um **Transient herda** o escopo de quem o recebe. Leis: D226 (C#/mercado), D229,
D213 (reusa o núcleo, ensina só o delta), D218/D227 (o RC por escopo é o dono da posse), D220
(visibilidade), §50 (ordem livre — o registro não precisa de varredura nova).

MEDIDO, não presumido: `teko_class.mc:1159` (`tk_conf_name`, o ÚNICO sítio que lê um nome da lista `:`
de uma classe) e `teko_iface.mc:565` (o mesmo para interface); `teko_iface.mc:249` (`tk_itab_emit` EMITE
o índice `si` — nenhuma LINHA de tipo nova pode nascer na inicialização, §50 risco 1) e `:643`
(interface sem membros é recusada hoje — marcador NÃO pode ser interface declarada);
`teko_struct.mc:990` (campo `static` = global `u8 sym[8]`, o molde do slot de singleton);
`lib/rt.mc:149` (`uptr rt_own(uptr p)` — atribuir chamada que devolve `uptr` a local de tipo classe é o
idioma do próprio RC); `teko_rc.mc:108` (`tk_rc_own` consulta a linha `xt` ANTES de tudo, e `xt_pure ==
1` é EMPRESTADO); `teko_struct.mc:745` (`tk_pure`, lido no PARSE por `teko_expr.mc:310`/
`teko_iface.mc:252`: receptor não-puro é RECUSADO, nunca duplicado).

### (a) Decisões

1. **Os três marcadores são NOMES, não linhas de tipo.** `tk_di_marker(nm)` reconhece os três no
   instante em que a lista `:` lê o nome (`tk_conf_name`), ANTES de qualquer busca na tabela de tipos.
   Nenhum `type_new`, nenhum global e nenhuma palavra reservada nascem na inicialização — a ordem das
   linhas `sr_*` (que é EMITIDA) fica byte-a-byte a de hoje e o gate de AST das 43 fixtures passa por
   construção. Um `interface IServiceSingleton { }` do usuário é recusado em `tk_newname`
   (`teko: IServiceSingleton is the service marker`).
2. **O marcador não vira itab, não vira slot, não é herdado.** A interceptação vem antes de
   `tk_conf_add`, logo o par (classe, marcador) nunca entra em `ci_*`; `tk_impls_inherit` não tem o que
   copiar e uma classe derivada de um serviço **não é** um serviço (C#: registra-se o tipo registrado).
   O lifetime fica num scratch (`tk_conf_life`, ao lado de `conf_if`/`tk_nconf`), consumido em
   `tk_conf_apply` (teko_class.mc:1405) por `tk_di_conf_apply(ci)` — onde a linha da classe já existe.
3. **Chaves = as interfaces que a classe implementa (fecho de `ci_*`, §50 I1) MAIS a própria classe.**
   É a convenção `AsImplementedInterfaces` do mercado; `inject IDb` e `inject Db` funcionam os dois, e
   a classe registrada não precisa implementar interface alguma.
4. **Duas implementações da mesma chave = RECUSA no sítio que PEDE, não no registro.** "Última vence"
   (C# `services.Add…`) é indefinível aqui: a ordem de declaração é LIVRE desde §50. Só é erro quando
   alguém injeta a chave — `teko: two services implement this interface`, nomeando as duas.
5. **`inject T` é a entrada.** Palavra de EXPRESSÃO (`syntax_expr("inject")`), lida como `new`
   (teko_expr.mc:131): nome de tipo, qualificação por `tk_ns_walk`, linha `TK_PFWD` aceita. `new` = "o
   dev constrói", `inject` = "o compilador constrói pelo registro"; é a grafia que o mercado C# usa
   (`[Inject]`/`@inject`), já que `Services.Get<T>()` exigiria função genérica, que o ngen não ensina.
   `main` não recebe parâmetros injetados.
6. **A injeção é por CONSTRUTOR, como em C#.** O construtor de injeção é o de MAIS parâmetros
   satisfazíveis (C# §12.6.4): cada parâmetro é um tipo de serviço resolvível OU tem DEFAULT (
   `tk_fill_defaults`, a máquina do C1/C6). Empate de contagem = `teko: two constructors of this
   service take the same number of injectable parameters`; sem construtor = o objeto zerado de sempre.
7. **`new Repo(...)` à mão numa classe registrada continua permitido** (C# permite): um serviço é uma
   classe comum; a DI só é acionada por `inject` e pela resolução de dependências.
8. **`scope { ... }` abre um escopo.** Statement (`syntax_stmt("scope")`), corpo obrigatoriamente
   bloco, aninhável — o `CreateScope()` do C# sem o provider (que aqui não existe).
9. **O escopo é LÉXICO.** O contexto de um sítio `inject` é o `scope { }` que o envolve, e a RAIZ do
   programa quando não há nenhum. Uma função chamada de dentro de um `scope { }` NÃO herda esse escopo
   — quem precisa de serviço com escopo ou é um serviço (recebe por construtor) ou abre o seu próprio
   `scope { }`. É a única regra que o comptime sabe provar, e cabe numa linha.
10. **A RAIZ é um escopo.** "Transient sem escopo abre o próprio" (D229) não acontece: o escopo-raiz
    sempre existe. Um Scoped resolvido na raiz tem UMA instância no programa, num slot global — e ganha
    instância nova dentro de cada `scope { }`.
11. **O escopo PRÓPRIO do Singleton (a diferença teko↔C#) é o corpo do seu builder.** Todo Scoped da
    subárvore de um singleton vira um local de tipo classe DENTRO do builder — um por tipo,
    compartilhado por aquela subárvore; dois singletons com o mesmo Scoped recebem instâncias
    DIFERENTES.
12. **Singleton e Scoped-de-raiz emitem um slot global + um getter; Scoped-de-bloco emite um LOCAL do
    bloco; Transient emite a construção no próprio sítio.** Uma máquina só: "construir X no escopo S".
13. **O RC não ganha uma única regra nova (D227).** O local de escopo é um `N_VAR` comum de tipo classe
    → `tk_rc_block` (teko_rc.mc:367) libera no `}` e em todo salto que sai; o construtor que guardar o
    parâmetro num campo faz `rt_store` (que incrementa), então capturar prolonga a vida. O getter é
    declarado `uptr` (idioma de `rt_own`) e devolve EMPRESTADO; o sítio leva `tk_xt_add(n, si, 1)` e o
    `rt_own` de `tk_rc_var` (teko_rc.mc:155) incrementa. Sítio Transient leva `pure = 0` (possuído), e
    é parkeado/varrido como qualquer temporário.
14. **O objeto de um singleton (e do Scoped de raiz) nunca é liberado** — é raiz, como o campo `static`
    de tipo classe (§50 decisão 16). `rt_live()` tem piso acima de zero; a fixture DIZ isso.
15. **Tudo resolve num `pass()`** (`tk_di_pass`), **depois de `tk_fwd_pass`** e **antes de
    `tk_array_pass`**: depois do `tk_partial_pass` (fecha toda classe parcial, logo os alocadores
    existem) e à frente de todo passe que censa por nome — o que este passe INSERE (locais de escopo,
    builders de topo) chega como declaração e statement comuns para o oráculo, o `ref`/`out`, o
    ternário e, por último, o RC.
16. **O sítio defere com placeholder** (`tk_di_defer`, o idioma de `tk_unresolved_new`): sem o passe o
    núcleo recusa `call to unknown function` — nunca miscompila. O nó já é tipado no parse
    (`tk_xt_add(n, si, 0)`), então oráculo, `.` e RC não perdem nada; o `pure` sobe para 1 no PASSE
    quando o lifetime resolvido for Singleton ou Scoped.
17. **`inject T.m()` direto é recusado** (`pure == 0` no parse: `teko_expr.mc:310`/`teko_iface.mc:252`,
    a recusa de `new C().m()`), para que um Transient nunca seja alocado duas vezes por um receptor
    clonado. Escreve-se `IDb db = inject IDb; db.query();`. Dívida declarada (§(g)).
18. **Ciclo é erro de compilação**, pego na resolução do grafo com pilha de nomes: `teko: cyclic
    service` + a cadeia `A -> B -> A`. Serviço abstrato, marcador em interface, dois marcadores na
    mesma classe e chave sem implementação são recusa própria (§(c)).
19. **Genéricos (`IRepo<T>`) ficam de fora** — instância de genérico é linha de tipo própria e `inject`
    teria de ler argumentos de tipo; dívida declarada com recusa clara, não silêncio.

### (b) Superfície

```
interface IDb { i64 query(); }                        // o CONTRATO: é o que se injeta
interface ILog { i64 write(i64 v); }
public class Db : IDb, IServiceSingleton {            // a IMPLEMENTAÇÃO carrega o marcador
    private i64 n;
    public i64 query() { n = n + 1; return n; }
}
public class Audit : ILog, IServiceScoped { public i64 write(i64 v) { return v; } }
public class Repo : IServiceTransient {               // registrada por si mesma (sem interface)
    private IDb db;
    public Repo(IDb db, ILog log) { this.db = db; }   // INJEÇÃO POR CONSTRUTOR
    public i64 load() { return this.db.query(); }
}

i64 main() {
    IDb a = inject IDb;                               // raiz: o singleton, uma instância no programa
    scope {                                           // abre um escopo
        Repo r = inject Repo;                         // transient: novo a cada injeção;
        i64 x = r.load();                             // o ILog dele é o Audit DESTE escopo
    }                                                 // o Audit do escopo morre aqui (RC, D227)
    return a.query() + 40;
}
```

### (c) Compatibilidade de lifetimes (teko ≠ C#) — nenhuma combinação é erro

| recebe ↓ / recebido → | Singleton | Scoped | Transient |
|---|---|---|---|
| **Singleton** | o slot global (mesma instância) | **teko ≠ C#** (D229): instância no escopo PRÓPRIO do singleton, uma por singleton, viva enquanto ele viver. C# lançaria (captive dependency) | construído dentro do escopo do singleton |
| **Scoped** | o slot global | a instância DAQUELE escopo (a mesma para toda a subárvore) | construído no escopo que o recebe |
| **Transient** | o slot global | a instância do escopo HERDADO (léxico, ou o do serviço que o construiu) | novo a cada injeção |
| **raiz** (fora de `scope { }`) | o slot global | um slot global: uma instância no programa (decisão 10) | novo a cada injeção |

O que É erro, e a mensagem: chave sem implementação (`teko: no service implements this type`); duas
implementações (decisão 4); ciclo (decisão 18); serviço sem construtor injetável (`teko: no constructor
of this service takes only services`); dois marcadores na mesma classe (`teko: a class names two
service lifetimes`); marcador numa interface (`teko: a service marker names a class`); classe abstrata
(`teko: an abstract class is not a service`); `inject` de tipo não-serviço (`teko: this type is not a
service`); e a visibilidade de sempre (`X is internal to another project`, `tk_check_type_use`).

### (d1) Hook → uso

| hook / API | uso |
|---|---|
| `syntax_expr("inject")` | o sítio de injeção, lido como `new` (teko_expr.mc:131) |
| `syntax_stmt("scope")` + `parse_stmt()` | o bloco de escopo (o molde de `tk_while`, teko_loop.mc:171) |
| `pass(&tk_di_pass)` | registro → grafo → emissão, logo depois de `tk_fwd_pass` |
| `tk_glb`/`tk_func`/`tk_var`/`tk_if`/`tk_ret`/`tk_call`/`tk_id`/`tk_int`/`top_add` | o slot, o getter e os locais de escopo (teko_struct.mc:389-422) |
| `tk_ctor_pick`/`mt_*`/`tk_fill_defaults`/`tk_new_sym` | o construtor de injeção e o símbolo do alocador |
| `tk_xt_add`/`set_xt_pure_at`; `ci_*`/`tk_impl_index`/`tk_iface_nbase` | tipo do sítio no parse e posse no passe; as chaves que a implementação atende (fecho §50 I1) |
| `node_assign`/`list_append`/`nd_a` | reescrita do placeholder; inserção na cabeça do bloco de escopo |

### (d2) Sítios tocados

| sítio | ação |
|---|---|
| `teko_class.mc:1159` `tk_conf_name` | reconhece o marcador ANTES da busca de tipo; grava o lifetime no scratch e devolve `base` |
| `teko_class.mc:1405` `tk_conf_apply` | `tk_di_conf_apply(ci)`: consome o scratch, registra (classe, lifetime), recusa abstrata/duplicada |
| `teko_iface.mc:565` `tk_iface_base_name` | marcador numa lista `:` de interface = recusa própria |
| `teko_struct.mc` `tk_newname` | recusa a DECLARAÇÃO de um tipo com o nome de um marcador |
| `teko.mc` `user_init` | `#include "teko_di.mc"`, as duas palavras, `pass(&tk_di_pass)` na posição da decisão 15 |
| `ngen/teko_di.mc` (novo) | tabelas `sv_*`/`si_*`/`sb_*`/`sl_*`, o grafo, a emissão, as mensagens |

Assinaturas: `i64 tk_di_marker(uptr nm); void tk_di_conf_apply(i64 ci); i64 tk_inject(); i64
tk_scope_stmt(); i64 tk_di_find_impl(i64 want); i64 tk_di_ctor_pick(i64 ci); uptr tk_di_getter(i64 sv);
i64 tk_di_local(i64 scope, i64 sv); i64 tk_di_build(i64 sv, i64 scope, i64 line, uptr fl); i64
tk_di_pass(i64 root);`

### (e) Mecanismo, em quatro frases

**Registro:** a lista `:` grava (classe, lifetime); as CHAVES saem do fecho `ci_*` que a classe já
publica, mais ela mesma. **Grafo:** `tk_di_build(sv, escopo)` resolve o construtor de injeção e recursa
em cada parâmetro de tipo-serviço — Singleton/Scoped-de-raiz devolvem a chamada ao getter (emitido uma
vez, memoizado), Scoped devolve o local daquele escopo (criado na primeira necessidade), Transient
devolve a construção inteira; a pilha de nomes fecha o ciclo. **Emissão:** `uptr <cls>_di_get() { uptr
p = ld64(<cls>_di_slot); if (p == 0) { <locais do escopo próprio> p = <cls>_new(args);
st64(<cls>_di_slot, p); } return p; }` — o `p` é `uptr`, então o RC não o toca; os locais de tipo
classe do escopo próprio SÃO tocados, que é o que se quer; os de um `scope { }` entram na CABEÇA do
bloco, em ordem de dependência. **RC:** nada de novo — o bloco libera em ordem reversa, o campo que
capturou já incrementou, e o sítio é emprestado (Singleton/Scoped) ou possuído (Transient).

### (f) Sequência de crumbs

**DI1 — marcadores, registro e diagnóstico; `inject` de um singleton sem dependência.** Arquivos:
`ngen/teko_di.mc` (novo), `teko.mc`, `teko_class.mc` (`tk_conf_name`/`tk_conf_apply`), `teko_iface.mc`
(recusa na interface), `teko_struct.mc` (`tk_newname`). Fixture: `ngen/tests/surface_di.tk`
(`expect-exit: 42`) — `Clock : IClock, IServiceSingleton` com estado, duas injeções provando a MESMA
instância (o contador chega a 2), `inject` pela classe concreta, `rt_live()` com o piso DITO. Gate: 5
pernas; `--entry-only` 44/44; `--dump-ast` das 43 anteriores **byte-idêntico**; `mc limits ngen` ok
(`syntax` +1, `passes` +1, `intrin` sem crescimento). Probes fora de `tests/`: marcador numa
`interface`; dois marcadores na mesma classe; `abstract class : IServiceScoped`; `interface
IServiceSingleton { }` do usuário; `inject` de tipo sem registro.

**DI2 — injeção por construtor, grafo, ciclo, emissão do singleton.** Arquivos: `teko_di.mc` (grafo,
emissão, `tk_di_ctor_pick`). Fixture: `surface_di.tk` cresce — cadeia de três níveis (`Svc(IRepo,
IClock)` ← `Repo(IDb)` ← `Db`), construtor com um parâmetro `= default` preenchido, chamada através da
interface provando o itab. Gate: 44/44; AST das 43 byte-idêntica; `limits ok`. Probes: ciclo `A -> B ->
A`; duas implementações da chave injetada; construtor que pede `i64` sem default; dois construtores com
a mesma contagem injetável.

**DI3 — `scope { }`, Scoped e Transient.** Arquivos: `teko_di.mc` (locais de escopo, herança de
escopo), `teko.mc` (`syntax_stmt("scope")`). Fixture: `ngen/tests/surface_di_scope.tk`
(`expect-exit: 42`) — duas injeções do mesmo Scoped no MESMO `scope { }` (mesma instância), dois
`scope { }` seguidos (instâncias diferentes), Transient novo a cada injeção, Transient que recebe o
Scoped do escopo hospedeiro, `scope { }` aninhado, `rt_live()` de volta ao piso depois do `}`, `return`
de dentro do escopo. Gate: 45/45; AST das 44 anteriores byte-idêntica; `limits ok` (`syntax` +1).
Probes: `scope` sem `{`; `scope { }` dentro de laço (uma instância por volta).

**DI4 — namespaces, herança de interface como chave, visibilidade, o Singleton que recebe Scoped.**
Arquivos: `teko_di.mc` (qualificação, `tk_check_type_use`, escopo próprio do singleton). Fixture: as
duas crescem — `namespace app { class Cache : ICache, IServiceSingleton { ... } }` injetada por `inject
app.ICache` e pelo nome curto via `using`; chave que é interface-BASE (`I2 : I1`, a classe implementa
`I2`, o sítio pede `I1`); um Singleton que recebe um Scoped (D229) provando UMA instância própria
compartilhada por dois dependentes dele; classe derivada de serviço que NÃO é serviço. Gate: 45/45; AST
das anteriores byte-idêntica; `limits ok`. Probes: serviço `internal` de outro projeto; `inject` de
interface implementada por duas classes via base comum.

### (g) Fora do escopo, dívidas e o que subir ao dono

**Fora do escopo (dívida declarada, com recusa clara, nunca silêncio):** genérico como chave
(`IRepo<T>`, decisão 19); `inject T.m()` direto (decisão 17 — hoistar o sítio é crumb futuro, molde
`teko_ternary.mc`); escopo dinâmico (decisão 9); `delegate`/`struct` como serviço (sem vtable, sem
contagem); `IDisposable` (o `~Name()` já roda no release); substituição/decoração de registro, factory
explícita e serviço com chave (`[FromKeyedServices]`); `inject` em inicializador de campo (não existe).

**Pedido ao mc: NENHUM** — tudo cabe em `syntax_expr`/`syntax_stmt`/`pass()`/`top_add`, já em uso aqui.

**Ao dono (duas confirmações de GRAFIA; nenhuma bloqueia — o desenho segue com o que está decidido):**

> 1. **`inject T`** é a forma de PEDIR um serviço (`IDb db = inject IDb;`), lida exatamente como `new`.
>    O C# pede pelo provider (`GetRequiredService<T>()`), o que exigiria função genérica — que a teko
>    sobre mc não ensina; a palavra `inject` é a que o próprio ecossistema C# usa para o mesmo pedido
>    (`[Inject]`/`@inject` do Blazor). Se preferir outra palavra (`svc`, `resolve`, `service`), é uma
>    linha no `user_init`.
> 2. **`scope { ... }`** é o que ABRE um escopo (C#: `using var scope = provider.CreateScope();`, que
>    não tem sentido sem container em runtime). Um `scope { }` = um conjunto de instâncias Scoped,
>    liberadas no `}` pelo RC do D227. Se preferir a grafia `using scope { }`, também é uma linha.

### (h) Riscos

1. **Gate de AST.** A régua é a ordem das linhas `sr_*`, porque o índice é EMITIDO (`tk_itab_emit`).
   A decisão 1 existe por isso: marcador é NOME, não linha de tipo — zero linha, zero global e zero
   símbolo novos num programa sem DI. Um crumb que criar linha na inicialização deixa as fixtures com
   interface VERMELHAS, e é o sinal certo.
2. **Posse (o risco de memória).** O getter devolve EMPRESTADO e o sítio incrementa via `rt_own`;
   devolver possuído E incrementar no sítio vaza uma referência por injeção. O local do getter é `uptr`
   de propósito (o RC ignora) e o local de escopo é de tipo CLASSE de propósito (o RC libera). Prova:
   `rt_live()` na fixture, antes/dentro/depois do `scope { }`.
3. **Parking de temporário.** Valor possuído em posição sem dono é parkeado e varrido no fim do
   statement (`tk_rc_park`, teko_rc.mc:305). O `p = <cls>_new(...)` do getter é o VALOR de uma
   atribuição, que `tk_rc_expr` marca como `owner` e não parkeia (teko_rc.mc:455) — se essa forma
   mudar, o singleton morre no ato; o implementador confirma por probe antes de seguir.
4. **`pure` no parse × `pure` no passe.** O único consumidor de `xt_pure` antes do RC é `tk_pure`, lido
   no PARSE pelo `.` (dois sítios medidos): por isso o parse grava 0 (recusa o receptor) e só o passe
   eleva a 1. Um terceiro consumidor no meio muda essa conta.
5. **Ordem dos passes.** `tk_di_pass` insere `N_VAR` em bloco e declaração de topo; roda DEPOIS de
   `tk_partial_pass` (alocadores existem) e ANTES de `tk_typeof_pass`/`tk_rc_pass` (que têm de ver os
   locais novos). Rodar depois do RC é a falha silenciosa: escopo que nunca libera.
6. **Colisão de símbolo e tabelas fixas.** `<cls>_di_slot`/`<cls>_di_get` seguem a convenção de
   `<cls>_vt`/`<cls>_new` e correm o mesmo risco (função livre com o nome exato) — recusa clara do
   núcleo, régua `mc limits ngen`; `TK_MAXSVC`/`TK_MAXSITE`/`TK_MAXSCOPE`/`TK_MAXSDEP` seguem o idioma
   do módulo, cada estouro com mensagem própria.

## 59. Errata — DI1 landado (2026-09-06)

1. **A régua "`syntax` +1" do risco (h)1/crumb DI1 não aparece como número visível — o gate real é
   `verdict ok`.** `mc` conta `syntax()`, `syntax_stmt()` e `syntax_expr()` na MESMA coluna do
   relatório (as três chamam `grow(T_SYNTAX, ...)`, `hooks.mc`), que publica o MÁXIMO das três, não
   a soma. `syntax()` (14, os honest-stops de topo) já domina `syntax_expr()` (8→9 com `inject`), e o
   número reportado fica em 14 dos dois lados — nada quebrou, só a métrica é o teto e não a soma. A
   régua do gate segue sendo `verdict ok` mais a checagem individual das tabelas que de fato
   crescem (`passes` 14→15, `intrin` 8→8).
2. **`teko_di.mc` entra DEPOIS de `teko_class.mc`** (não antes, nem junto de `teko_iface.mc`): a
   máquina de resolução (`tk_di_new_call`/`tk_di_find_impl`) precisa de `tk_ctor_named`/
   `tk_ctor_pick`/`tk_new_pick` (teko_class.mc) e de `ci_if_at`/`tk_nimpl` (teko_iface.mc) já
   inteiros, enquanto os TRÊS chamadores que precisam de `tk_di_marker` mais cedo
   (`teko_struct.mc`'s `tk_newname`, `teko_iface.mc`'s `tk_iface_base_name`, `teko_class.mc`'s
   `tk_conf_name`/`tk_conf_apply`) recebem um forward-declare de três linhas cada, o mesmo idioma
   que `teko_iface.mc` já usa para `tk_fwd_materialize`/`tk_check_type_use_from`/`tk_trait_find`
   (§50 I1). Não há necessidade de mover `teko_di.mc` para mais cedo na cadeia de `#include`.
3. **`tk_struct_find_fwd` (teko_struct.mc, "identity-only site") é o lookup certo para `inject`,
   não `tk_struct_find` + `tk_fwd_row` manual.** O crumb já pede TK_PFWD aceito (decisão 5); o par
   pronto que a base já dava para field/param/return/local materializa o placeholder sozinho e
   poupa uma reimplementação.

## 60. Errata — DI2 landado (2026-09-06)

1. **`nd_type(p)` de um PARÂMETRO é o id do `type_new` do NÚCLEO, não a linha da tabela de structs
   que `sv_cls_at`/`ci_if_at`/`ci_cls_at` indexam.** Medido ao vivo (probe fora de `tests/`): para um
   parâmetro `IDb db`, `nd_type(p)` deu `9` enquanto `tk_struct_find("IDb")` deu `1` -- dois espaços
   de numeração distintos, exatamente o que `tk_struct_by_ty(ty)` (teko_struct.mc:516, já em uso por
   `tk_field_use`/`tk_is_counted`/`tk_ha_index` para o mesmo problema) já converte. `tk_di_key_exists`/
   `tk_di_ctor_satisfiable`/`tk_di_ctor_args` chamam `tk_struct_by_ty(nd_type(p))` antes de comparar
   contra qualquer chave de serviço -- sem essa conversão, NENHUM parâmetro de tipo-serviço é
   reconhecido e todo construtor com dependência cai na mensagem "no constructor... takes only
   services", mesmo com o serviço registrado e a dependência correta.
2. **A recusa de "duas implementações" e de "sem implementação" de um parâmetro de construtor usa a
   LINHA DO PARÂMETRO (`nd_line(p)`/`nd_file(p)`), não a linha do sítio `inject` que disparou a
   cadeia.** É o que o §58 (c) já pedia ("na LINHA do construtor que a pede") e o que faz o erro
   apontar para `Repo(IDb db)` mesmo quando quem pediu `Repo` foi um `Svc` três níveis acima --
   confirmado por probe: duas implementações de `IDb` apontam para a linha do PARÂMETRO de `Repo`,
   não para o `inject Repo` de `main`.
3. **O item herdado (decisão 17) não precisou de tabela nova.** `ds_node`/`tk_nds` (já existentes
   desde o DI1, para o placeholder de cada `inject`) já são exatamente o conjunto "nós que um
   `inject` produziu" -- `tk_di_is_inject(n)` é uma busca sobre essa mesma tabela, e sobrevive ao
   `node_assign` de `tk_di_pass` porque este substitui o CONTEÚDO do nó, nunca seu índice. Nenhuma
   marcação nova em `xt_*` foi necessária.
4. **A posse da cadeia não precisou de regra nova no RC**, confirmando a decisão (e) do §58: `Svc_new`
   recebendo `Repo_di_get()`/`Clock_di_get()` como argumento é reconhecido pelo `tk_rc_call_owned`
   genérico (teko_rc.mc) puramente pelo TIPO DE RETORNO DECLARADO da função chamada -- `uptr` para um
   getter é emprestado, o tipo da própria classe para um alocador é próprio -- o mesmo mecanismo que
   já cobre qualquer `new X(new Y())` escrito à mão. `rt_live() == 4` na fixture (Clock, Db, Repo, Svc)
   prova que nada vazou nem foi contado em dobro.

## 61. Errata — DI3 landado (2026-09-06)

1. **`scope { ... }` não precisou de nó novo nem de gancho no RC.** `tk_scope_stmt` lê o corpo por
   `parse_stmt()` -- o token corrente é `{`, que já resolve para `tk_block` (teko_stmt.mc) -- e
   devolve o MESMO `N_BLOCK` que um bloco solto teria; `scope { }` degrada para um bloco comum aos
   olhos de TODO passe seguinte (oráculo, `ref`/`out`, RC), e é exatamente essa degradação que faz o
   "uma instância por volta de laço" (probe) sair de graça: o bloco é reexecutado a cada volta, então
   o `N_VAR` que `tk_di_pass` prependa à sua cabeça roda -- e é liberado pelo `tk_rc_block` de sempre
   -- uma vez por volta, sem regra de laço nova.
2. **A ORDEM de inserção dos locais de um escopo (decisão "em ordem de dependência") sai de graça da
   recursão de construção, não de um passe de ordenação.** `tk_di_scope_local(scope, sv, ...)`
   constrói o `N_VAR` de `sv` chamando `tk_di_new_call` -- e é DENTRO dessa chamada, ao resolver um
   parâmetro Scoped do construtor de `sv`, que uma dependência ainda não vista entra em `sc_head`
   PRIMEIRO (a chamada recursiva completa e o `list_append` do dependente só roda depois). Nenhum
   `sort`/pilha de prontidão foi necessário -- confirmado por probe (dois Scoped, um dependendo do
   outro, no mesmo `scope { }`: a ordem gerada é dependência-primeiro).
3. **O ciclo A -> B -> A entre dois Scoped do MESMO escopo continua pego pelo `di_stk` de DI2, sem
   ajuste.** A memoização `sl_*` só registra uma entrada DEPOIS que `tk_di_new_call` retorna -- então,
   em plena recursão de um ciclo, a segunda tentativa de resolver A ainda não a encontra em `sl_*` e
   tenta construir de novo, o que dispara o `tk_di_push` de DI2 e a mensagem de ciclo de sempre antes
   de qualquer estouro de pilha. Nenhuma tabela nova de detecção foi cogitada nem precisou existir.
4. **O Singleton que recebe um Scoped é RECUSADO, não silenciosamente resolvido pela raiz.** Sem o
   sentinela `tk_scope_svcbld`, o grafo de um Singleton (que já não passava `scope` algum antes desta
   crumb) cairia no mesmo `scope >= 0` falso que a raiz usa e devolveria a instância COMPARTILHADA da
   raiz -- errado por (c)/decisão 11, que reserva ao Singleton um escopo PRÓPRIO (DI4). A recusa
   explícita troca esse silêncio por um erro de compilação claro; confirmado por probe (fora de
   `tests/`, descartado): `class Svc : IServiceSingleton { public Svc(IAudit a) {...} }` com `Audit :
   IServiceScoped` dá `teko: a singleton taking a scoped service is not taught yet: Audit`, na linha
   do PARÂMETRO `IAudit a` (o mesmo idioma da errata DI2 §60 item 2 -- "a linha do construtor que a
   pede"), não na do `inject Svc` que disparou a cadeia.

## 62. Errata -- DI4 landed, §58 closed (2026-09-06)

1. **Namespace-qualified/bare `inject` and an interface's own BASE as a key needed ZERO new
   code.** `tk_inject` already read its name through `tk_ns_walk` + `tk_struct_find_fwd`, which
   already threads a qualified path and a `using`-brought bare short name through the same search
   order every other type reference takes (N1/O2, `tk_ns_resolve_fwd`); `tk_di_sv_matches` already
   walks `tk_nimpl`, where an interface's own base is already flattened into a conforming class's
   set the moment it is declared (§50 I1, `tk_iface_conf_close`). Confirmed by fixture growth, not
   by a diff: this crumb's ONLY new machinery is the Singleton's own scope and the two diagnostic
   ressalvas below.
2. **The Singleton's own scope is a real row of the SAME pool `scope { }` uses, not a parallel
   table.** `tk_di_scope_new` (extracted from `tk_di_scope_open`, which now calls it) allocates a
   fresh `sc_head`/`sc_block` row with no lexical push at all; `tk_di_singleton_scope(sv)` memoizes
   one such row per service row in a new `sv_ownscope` column, lazily, the first time that
   Singleton's own graph needs one. `tk_di_getter_sym` hands this row to `tk_di_new_call` as
   `buildscope` in place of DI3's sentinel, and `tk_di_resolve`'s existing `scope >= 0` branch
   (unchanged) treats it exactly like a `scope { }`'s own local -- the ENTIRE feature is "give the
   Singleton a real scope id instead of a sentinel that always refused."
3. **The locals a Singleton's own scope collects are spliced into its getter by the SAME function
   that built them, not by `tk_di_scopes_finish`.** `tk_di_getter_sym` reads `sc_head_at(buildscope)`
   right after the recursive `tk_di_new_call` that filled it, prepends it in front of the
   `p = <cls>_new(...)` assignment, and zeroes the row -- `tk_di_scopes_finish`'s own blind sweep
   over every scope row (which only ever intends to close a LEXICAL `scope { }`, whose `sc_block`
   is always set) never sees a Singleton's own row at all once this runs, because its `sc_head` is
   already 0 and its `sc_block` was never set to begin with.
4. **A private/protected constructor's message and an internal-service check moved OFF
   `tk_check_member`, not alongside it.** DI's own call site has no enclosing class a `protected`
   constructor could ever answer to, so `tk_check_member`'s branch for it was dead weight that just
   produced the wrong (generic) wording; `tk_di_new_call` now calls `tk_check_type_use` on the
   service's class directly (catching an `internal`-of-another-project class even when its own
   interface is public) and raises its own message the moment the picked constructor is not
   `TK_VPUBLIC`. Confirmed by probe: an internal INTERFACE and an internal CLASS (whose interface
   is public) each refuse at the expected site with the expected wording.
5. **The DI3 lambda gap (item 5 of §58 (g)) is refused, not resolved.** `tk_lam_body_depth`
   (teko_deleg.mc) counts lambda-body nesting live during parse; `tk_inject` refuses the instant
   that counter is nonzero AND a `scope { }` is open, because the lambda's own body is compiled as
   a SEPARATE top-level function (K4) and a Scoped local prepended to the enclosing `scope { }`'s
   block would not be a name that function's own scope ever sees. An `inject` of a Singleton inside
   a lambda with NO `scope { }` open is untouched (confirmed by probe): nothing is ever prepended
   for it, so there is nothing to be unreachable.

## 63. Assignment compatibility + K3 null guard + lambda statics (D226, 2026-09-06)

Three independent items, one crumb, each landed with its own fixture proof; 45/45 in exit,
`--dump-ast` of the 43 fixtures neither item touches byte-identical against `088bf795` except for
the K3 guard's own additive lines (item 2, universal via `#include "../lib/rt.mc"`).

1. **Item 1 -- C#'s implicit reference conversion, checked for real.** `Unrelated y = x;` used to
   compile in silence; now `tk_row_fits` (teko_struct.mc, next to `tk_struct_of_expr`) answers
   identical / class-derives-from / implements (`tk_impl_has` already flattens interface-extends-
   interface and a base's own interfaces, so one call answers both), and `null` (teko_struct.mc's
   `tk_is_null_lit`, the exact shape `tk_null` builds) fits any class/interface/struct/delegate/`T[]`
   slot. `T[]`/delegate accept only their own identity (checked ahead of the derives/implements
   rule). Two call sites, by TIMING, not by choice: **parse time** (`tk_check_field_store`,
   teko_struct.mc) for a field/array-field store on a receiver already typed then -- `tk_struct_of_expr`
   is the only oracle that exists yet, so an unknown-at-parse source (a call whose return the pass
   will resolve) is silently skipped, never guessed at; **pass time** (`tk_check_compat`,
   teko_typeof.mc, using the full `tk_ty_of` oracle) for `T x = e;`/`x = e;`/`return e;`
   (teko_rc.mc's own `tk_rc_var`/`tk_rc_assign`/`tk_rc_return`, run BEFORE the `tk_is_counted` gate
   that used to skip a `struct` target entirely), a deferred field/array-field store
   (`tk_pend_field`'s two STORE branches, teko_typeof.mc), a heap-array element store (`tk_ha_store`,
   teko_heaparr.mc, one choke point for local/global/compound), and a CALL's own arguments
   (`tk_rc_call_args`, teko_rc.mc, hooked into `tk_rc_walk`) -- the last one needed NO new table at
   all: `decl_param_type`, the core's own API, already answers by index for whatever `decl_find`
   resolves, and by the time `tk_rc_pass` runs (registered LAST) every overload already carries its
   final, unambiguous name (teko_over.mc's rename, teko_default.mc's fill, both ahead of it), so a
   free function, a direct (non-virtual) method and a constructor's own allocator are all covered for
   free -- a virtual/interface call (`callp`, never a name `decl_find` would know) is NOT, a
   registered gap, arity still the only thing checked there. Argument-type checking is scoped to
   "both sides a teko row" per the crumb's own hedge; the other four sites reject a scalar-typed
   source too (`Point p = 5;`), since the target alone decides whether the check is in scope.
   `tk_rc_pass`'s own gate widened from "the unit needs the reclaim" (`tk_rc_needed`, counted types
   only) to `tk_rc_needed() || tk_compat_needed()` (`tk_nstruct > 0`) so a `struct`-only unit (no
   class/interface/delegate/array at all) still gets the walk -- every RC-injecting function already
   gated its OWN rewrite on `tk_is_counted`, so the walk is a no-op reshuffle (still byte-identical,
   confirmed against `types_struct.tk`) everywhere the new check does not fire.
   - **Two real bugs the new check surfaced, fixed at the root, not banded over.** (a) A struct's own
     allocator (`tk_ctor`, teko_struct.mc) returns its `p` local -- declared `uptr`, the raw
     `rt_alloc` result -- as the function's own struct-typed return; untagged, the oracle saw `uptr`
     where the return type is the struct, and rejected every struct in the tree. Fixed by tagging the
     returned identifier with the struct's own row (`tk_xt_add`), the exact same move `tk_new_fn`
     (teko_class.mc, a class's own allocator) already made -- the struct path just never had it.
     (b) A ternary/switch-expression's own hoisted temporary (`tk_tern_lower`, teko_ternary.mc) is
     declared `i64 $t = 0;` ahead of the `if` that actually assigns it, `0` being a placeholder never
     read before both branches overwrite it -- for a teko-typed arm (the ternary/switch coerces two
     function names into one `Op`, `surface_lambda.tk`'s own `ternary_check`) that placeholder is now
     `tk_null()` instead of a plain `i64` literal, matching the shape the new check already accepts.
     Neither fix changes ANY existing fixture's behavior; the AST moves by exactly one node each
     (`INT type=i64` -> `INT type=uptr`/a struct's own row tag), confirmed by `--dump-ast` diff.
   - **A DI-resolved constructor argument (`tk_di_ctor_args`, teko_di.mc) needed the SAME tag,
     with `pure` doing real work.** A Singleton/root-resolved `inject` becomes a call to a memoized
     getter declared `uptr` (no type of its own to fall back on, unlike a Transient's own allocator
     or a Scoped's own local, both already correctly typed); tagged `tk_xt_add(a, sv_cls_at(implsv),
     sv_life_at(implsv) != TK_SVC_TRANSIENT)` -- the service's own class (not the parameter's key,
     so the derives/implements rule still does the work when they differ), `pure` set to 1 for
     anything that is NOT a fresh Transient allocation. Getting `pure` wrong here is not cosmetic:
     the first cut tagged every branch `pure=0` (matching `tk_new_expr`'s own `new` convention) and
     `surface_di.tk` failed at RUNTIME with "reference count below zero" -- the memoized getter hands
     back a BORROWED reference (no `rc_inc` on a cache hit), and `tk_rc_own`'s `xt_pure_at` check is
     exactly what tells the reclaim a value is borrowed regardless of whether its type is counted.
   - **Fixture:** `surface_iface_inherit.tk` grows an `Animal`/`Dog` pair (D226's own probe) proving
     derived-into-base ON TOP of the file's existing class-into-interface (`I2 i2 = q;`) and
     interface-diamond (`IA a = bx;`) coverage; the AST of the 18 lines before it is untouched
     (pure append, confirmed by `--dump-ast`). Rejections (unrelated class, un-implemented interface,
     downcast, `T[]` of a different element, wrong-typed argument, wrong-typed return) confirmed by
     probe outside `ngen/tests/` (deleted after verification, per this crumb's own instruction), not
     committed.

2. **Item 2 -- K3's own null-array guard.** `tk_arr_at` (`ngen/lib/rt.mc`) segfaulted on a `T[]`
   local/field/global never assigned (`a == 0`, `ld64(a + 16)` reading offset 16 of address 0); one
   guard line, `panic("index into a null array")`, ahead of the length load -- the same `exit(70)`
   every other guard in this file already raises. Additive only: every one of the 42 fixtures that
   `#include`s this file gains the SAME seven-line `IF`/`panic` block at the SAME position in its
   `--dump-ast`, nothing else moves. Confirmed by probe (`i64[] xs; return xs[0];` -> exit 70,
   outside `ngen/tests/`, not committed).

3. **Item 3 -- a lambda's own body sees the program's STATICS, C#'s own rule.** `tk_lam_check_name`
   (teko_deleg.mc) refused a global, a `const` or (in the one shape that actually reaches it, `&f`)
   a free function exactly as it refused a genuine typo -- there is no hook over a top-level
   declaration (teko_array.mc's own header explains why: `parse_top`, not `parse_stmt`), so the
   answer was not knowable while the lambda's own body was still being read. Deferred instead of
   guessed: a name none of the four existing checks places is recorded (`tk_lg_add`, a small
   `lg_node` table) rather than refused on the spot, and `tk_deleg_pass` -- ALREADY the pass every
   lambda-bearing unit runs (`tk_any_deleg`), with the whole unit's `N_GLOBAL` rows finally in the
   tree -- resolves each one at its end (`tk_lam_resolve_globals`, a plain root scan for a matching
   `N_GLOBAL`): found, it is a global, and an ordinary `N_IDENT` read/write already resolves it with
   no rewrite needed (the core's own codegen looks a global up by symbol, lambda or not); not found,
   the SAME "is not captured" message fires, at the SAME position, just later. A WRITE to a global
   from inside a lambda (`counter = counter + 1;`) was already unchecked and already correct (an
   `N_ASSIGN`'s own target name is never walked through `tk_lam_check_name` at all, only its RHS is)
   -- confirmed by fixture, not fixed, because there was nothing broken there. `const` (a plain,
   non-namespaced one) was already fine too: `def_add`'s `#define` is resolved by the core's own
   `parse_primary` at the moment the token is read, lambda body or not, so it never reaches an
   `N_IDENT` for this check to see in the first place. **Not implemented** (registered debt, the
   crumb's own "?"): `use (g)` on a global still refuses with the generic "captures a local; this
   name is not one" instead of a global-specific "globals are visible without use" -- doing that
   would mean deferring `tk_lambda_use`'s OWN eager rejection, which feeds `lc_ty` (the capture
   table's structural backbone) synchronously; a genuinely global-only wording was judged not worth
   that risk for a cosmetic improvement C# does not even surface this way.
   - **Fixture:** `surface_lambda.tk` gains `global_const_free_check` (item 16) -- a lambda with NO
     `use (...)` at all that reads a global TWICE across two calls (proving it reads the LIVE value,
     not a frozen one the way `use (k)` would), calls a free function, reads a `const`, and writes
     the global back from inside a SECOND lambda, the write visible to the caller afterward. The
     AST of the 15 checks before it is untouched (pure append, confirmed by `--dump-ast`); the one
     unrelated line inside the file that DOES change (`ternary_check`'s own hoisted temporary) is
     item 1's ternary fix, not this item's. Probe (a genuinely unknown name inside a lambda) confirms
     the deferred rejection still fires, outside `ngen/tests/`, not committed.

## 64. D225 — auto-hospedagem da teko sobre o mc: desenho das 4 etapas (architect-first, 2026-09-06)

Desenho, não entrega. Base `ce5b5863` (45 fixtures, 5 pernas verdes), mc 0.15.5. **Nenhuma
mudança de superfície em nenhuma das 4 etapas** — nada de fixture nova: o gate é sempre 45/45 nas
5 pernas, e o artefato novo é *script*, não teste (a lei "não se escreve teste para o que o
próprio build exercita" vale aqui: as 45 já são a suíte).

### (a) Decisões

**D64.1 — o conjunto de partes do compilador teko é `core_min + core_machines + core_writers +
core_build + core_bundle`, com `main()` próprio; ficam FORA `<mc/core_pkg>`, `<mc/core_sandbox>`
e `<mc/main>`.** Por perna: as 5 usam as DUAS máquinas (`<float>` deriva de `arm64` E de
`x86_64`) e os quatro writers (macOS = `macho`+`backend_exe`; linux = `backend_elf`+
`backend_elf_exe`; windows = `backend_coff`); `core_build` porque o CI compila CADA fixture com
`build … --entry-only`; `core_bundle` porque `lib/rt` inclui `<sys>`. `core_pkg` e `core_sandbox`
não têm um único chamador no ngen — precedente `tests/pkg/nopkg.mc`: sem `core_pkg` o compilador
ainda constrói de `mc.lock` + `deps/`.

**D64.2 — o bundle PRÓPRIO (§6 de `bundle.md`, −364 KB de blob) é REJEITADO.** É a maior economia
disponível na etapa 1 e mesmo assim não se faz: a etapa 4 (rota A, D64.9) exige `<mc/host>` +
`<mc/core_min>` + as partes DENTRO do blob do próprio teko, senão o teko não consegue compilar o
próprio compilador. O destino manda na otimização local.

**D64.3 — a etapa 2 não chama `mc_build_init()`; escreve a tabela de subcomandos do teko à mão.**
`mc_build_init()` (mc `src/core_build.mc`) é `lex_set_libs(&libs_open)` + `sysroots_init()` +
três `subcommand()` + `on_plan(&mc_plan)` — todas públicas. Registrar `build` por cima
duplicaria a linha de usage (`subcommand_usage()` imprime TODAS as entradas, embora o *dispatch*
seja last-wins — `src/hooks.mc:1029`). Chamando as quatro peças na mão o teko fica com a sua
própria tabela, sem duplicata e sem pedir nada ao mc.

**D64.4 — `teko build` = `tk_build` resolve o config e delega ao `drv_build` do núcleo.** Ordem:
`--config FILE` explícito → `DIR/teko.toml` → `DIR/mc.toml`. `drv_build(argc, argv)` já lê
`--config` do argv (`src/driver.mc:794`), então `tk_build` sintetiza o argv e delega — zero
reimplementação do driver.

**D64.5 — o config do dev teko é `teko.toml`, MESMO esquema do `mc.toml`, mesmo leitor.**
`[project]`, `[target]`, `[linker]`, `[include]`, `[limits]` — idênticos. `[compiler]` deixa de
fazer sentido para o dev (o compilador É o binário `teko`) e `teko build` o ignora. `[deps]` do
mc é de quem ENSINA o compilador; o sistema de pacotes DA TEKO (imports/namespaces) é superfície
da linguagem e não é escopo aqui (§26). O `ngen/mc.toml` CONTINUA, com o papel de sempre: é o que
`mc build` usa para construir o compilador. Dois arquivos, dois papéis.

**D64.6 — `type_disable`/`intrinsic_disable`: a lista concreta é VAZIA hoje, e a regra é dura.**
Censo: os `type_alias` da teko (`bool`→`u8`, `char`→`u32`, `byte`→`u8`, `isize`→`i64`,
`usize`→`u64`, `ptr`/`str`→`uptr`) são IDENTIDADE — não há segundo tipo, nada a remover;
`f32`/`f64` vêm do `type_new` do `<float>` e não substituem palavra; `i32` é do núcleo (M45) e a
teko o QUER; `ld64`/`st64`/`callp` são usados pelas fixtures e pelo núcleo. **Regra (liga a etapa
2 à 4): um disable só é legítimo se a palavra (i) for de fato proibida na superfície teko E
(ii) não aparecer nos fontes do núcleo** — senão, na rota A, o teko deixa de compilar a si mesmo.

**D64.7 — a etapa 3 é UM pacote, "ambos", com o runtime DENTRO.** `lib = "lib/rt.tk"` (o que um
PROGRAMA inclui), `module = "teko.tk"` (o que um COMPILADOR inclui). `teko_rt` NÃO vira pacote
separado: módulo e runtime são versionados juntos (o módulo EMITE chamadas `tk_*` que o runtime
define — defasagem entre os dois é miscompilação silenciosa), e o precedente do mc é esse
(`<float>` = módulo + `<float_rt>` num pacote só).

**D64.8 — `.tk` ⊇ superfície do núcleo mc: a "reescrita" da etapa 4 é transliteração + adoção por
ondas.** Medido: `extern`, `uptr`/`ptr`, `ld64`/`st64`/`ld8`/`st8`, `callp`, `&fn` cru
(`uptr p = &twice;`, `surface_import.tk:41`), `#define`, `#include` de `.mc`, arrays globais,
`switch`, `while`/`for`/`do` já são superfície `.tk` HOJE. Logo **um `teko_x.mc` já É um
`teko_x.tk` válido**, e "reescrever os módulos em teko" (D225) se parte em duas: (1) o arquivo
vira `.tk` e passa a ser compilado PELO PRÓPRIO TEKO — isso é a auto-hospedagem; (2) adotar
construtos teko é **onda por módulo, depois, com o fixpoint como prova**. Pedir (2) antes de (1)
é reescrever 16 838 linhas sem rede.

**D64.9 — a montagem do compilador auto-hospedado: rota A (UMA unidade) é o destino; rota B (dois
objetos) é o plano que não bloqueia.** Ver (e). Recomendação: **A**, com o pedido ao mc de (g).

### (b) Etapa 1 — o compilador das partes (MEDIDO)

Régua: `scripts/check-parts.sh` do mc (a mesma forma: `--dump-syms` por seção + `wc -c` do
executável) e a tabela de `docs/guide/98-recreating-the-compiler.md`. **Medição feita nesta
sessão** (host macOS/arm64, `mc` 0.15.5 do `PATH`, `mc --exe`, nada escrito no repo):

| grafia | bytes | Δ |
|---|---|---|
| `<mc/host>` + `<mc/core>` + `<user_default>` | 1 153 028 | — |
| as 5 partes de D64.1 + `main()` próprio | 1 037 139 | **−115 889 (−10,05 %)** |
| `<mc/host>` + `<mc/core>` + `ngen/teko.mc` (o mc-teko de hoje) | 1 588 677 | — |
| as 5 partes + `ngen/teko.mc` + `main()` próprio | **1 489 365** | **−99 312 (−6,25 %)** |

As duas grafias com `teko.mc` COMPILARAM e LINKARAM — o conjunto de D64.1 basta no nível de
compilação/link; falta a prova de comportamento (45/45 × 5 pernas), que é o gate de S1. `mc
limits` entra na mesma medição (`[limits].tolerance = 1.0`: menos partes = menos fonte no pré-scan).

Forma: `[compiler] core = "<mc/core_min>"` (a forma `<` é a única que garante o
`#include <mc/host>` emitido antes) e `modules = ["core_teko.mc", "teko.mc"]`, onde
`ngen/core_teko.mc` traz as outras quatro partes e o `main()`:

```c
#include <mc/core_machines>
#include <mc/core_writers>
#include <mc/core_build>
#include <mc/core_bundle>

i64 main(i64 argc, uptr argv, uptr envp) {
    host_init(envp);
    mc_machines_init(); mc_writers_init(); mc_bundle_init();
    return mc_main(argc, argv, envp);
}
```

(`mc_build_init()` sai daqui na S2, por D64.3; na S1 ele ainda é chamado, para a S1 ser
puramente "menos partes".) Riscos: **(i)** parte que esconde dependência de outra — o mc já pagou
isso no M41 (`tm_cat`, `MODE_755`, `R_X86_PC32`) e `check-parts.sh` prova o contrário hoje;
**(ii)** sumir uma usage line (`sandbox`/`pkg`/`update`) é observável — é o que se QUER, mas o CI
não olha usage.

### (c) Etapa 2 — `teko build`

`teko.mc`/`core_teko.mc` passam a registrar:

```c
subcommand("build",  &tk_build,     "usage: teko build [DIR] [--config FILE] [--entry-only] ...\n");
subcommand("limits", &drv_limits,   "       teko limits [DIR|FILE.tk]\n");
```

e `main()` chama `lex_set_libs(&libs_open)`, `sysroots_init()`, `on_plan(&mc_plan)` no lugar de
`mc_build_init()` (D64.3). `sysroot` fica de fora (só a perna Windows o usa, e ela já monta o
sysroot no workflow). `[compiler].out = "build/teko"`. O dev vê `teko build .` com `teko.toml`
(D64.5); o CI troca `mc-teko build ngen --config … --entry-only` por `teko build …` — mesma forma.

### (d) Etapa 3 — o pacote `teko`

```toml
[package]
name   = "teko"
lib    = "lib/rt.tk"
module = "teko.tk"
files  = ["teko.tk", "teko_access.tk", …, "lib/rt.tk"]
check  = ["teko.tk", "lib/rt.tk"]
```

`teko.tk` deixa de definir `user_init` e exporta `void teko_init()`; `ngen/user.mc` (do PROJETO,
não do pacote) é `void user_init() { teko_init(); }` — a regra M44 é dura: um pacote nunca define
`user_init`. `check`: duas unidades, compiladas isoladas na caixa linux/x86_64 do registro —
`teko.tk` (o módulo, que inclui os 30 irmãos) e `lib/rt.tk` (o runtime, que inclui `<sys>`);
toda entrada de `check` tem de estar em `files`. Publicação: Release do GitHub com tag `vX.Y.Z`,
`sha256` = o **tree hash** (não o do tarball), PR no `minicompiler/mc-registry`. Nome `teko` é
válido (`[a-z][a-z0-9_]*`) e não é reservado. **Bloqueio:** o registro do mc ainda não abriu; tudo
menos a publicação pode ser feito hoje (o `[package]` é inerte para `mc build`, e `mc pkg hash`
já dá o número).

### (e) Etapa 4 — auto-hospedagem

**Auditoria de superfície (MEDIDA sobre os 16 838 linhas de `ngen/*.mc` + `lib/rt.mc`):**

| construção usada no `.mc` | existe em `.tk`? | crumb |
|---|---|---|
| `#define` (110×), `#include "..."`/`<...>` (34×) | sim (diretiva do núcleo, e `.tk` já inclui `.mc`) | — |
| `extern T f(...)` | sim (`surface_overload_free.tk`) | — |
| `ld64`/`st64`/`ld8`/`st8`/`ld32` (680×) | sim (`primitives_ptr.tk`) | — |
| `callp` (8×) e `&fn` CRU → `uptr` | sim (`surface_import.tk:41,44`) — o delegate contado é OUTRA grafia, não substitui | — |
| `uptr`/`str`/`ptr` crus | sim (aliases de `TY_UPTR`, D213) | — |
| arrays globais fixos (233 decls) | sim (§39/G1) | — |
| `while`/`for`/`do`/`switch`/`if`/`return` | sim | — |
| API do núcleo: **110 nomes** (`p_*`, `nd_*`, `parse_*`, `syntax*`, `err_at`, `type_*`, …) | sim na rota A (mesma unidade); na rota B, `extern` gerado | S4.2 |
| `#define`s do núcleo: **71** (`N_*`, `K_*`, `TY_*`, `T_*`, `MAXPARAMS`, `TK_SINT`) | sim na rota A; na rota B, espelho gerado | S4.2 |
| **global do núcleo lido direto: `nnodes`** (2 sítios, `teko_class.mc`/`teko_ns.mc`) | rota A sim; **rota B NÃO** (`extern` do mc é só função) | S4.2/(g) |

**Não há lacuna de superfície na linguagem.** A única lacuna é de MONTAGEM, e é uma colisão de
palavras — medida e reproduzida:

* **Sonda de transparência EXECUTADA:** o mc-teko das partes, mandado compilar
  `<mc/host>+<mc/core_min>+…`, morre em `mc/objmodel:293: name expected` — a linha é
  `void reloc_add(i64 sec, i64 off, i64 sym, i64 type, i64 pcrel, i64 len)`. Causa-raiz:
  `syntax("type", …)` chama `word_add` → `tok_add` (`src/lex.mc:217`), e uma palavra na tabela de
  tokens deixa de lexar como identificador **em todo lugar**.
* **Censo completo das colisões** (comentários e strings descontados) entre as 36 palavras que a
  teko registra e os fontes das 5 partes: **`type` (23 sítios**: `objmodel.mc:293,309`,
  `gen_resolve.mc:167,172,180,197,201,217,221`, `parse.mc:1005`, `gen_walk.mc:443,447`, +11 nos
  writers/driver) e **`out` (43 sítios**: `sha256.mc` como parâmetro `uptr out`, `cli.mc:161,209,378`).
  Nenhuma outra. `base`, `params`, `value`, `static`, `operator`, `get`/`set`, `case`, `in`, `or`,
  `use` NÃO colidem — a teko os lê contextualmente, sem `tok_add`.
* **Dentro do próprio ngen** as colisões são `scope` (21) e `out` (37) — ~58 sítios mecânicos.

**Rota A (UMA unidade) — recomendada.** `mc_teko.tk` = `#include <mc/host>` + as 5 partes + os
módulos `.tk` + `main()`; teko0 compila tudo, um objeto, um link, nenhum espelho de API,
macOS segue sem `[linker]`, e o fixpoint cobre **100 % do binário**. Custo: o pedido de (g) —
renomear `type`/`out` nos fontes do núcleo (o mc já moveu 4 nomes por essa mesma razão no M41).

**Rota B (DOIS objetos) — não bloqueia, e é pior.** `core.o` (as partes + `main` + `user.mc`,
`.mc`, compilado por mc estoque) + `teko.o` (os módulos `.tk`, compilado por teko0), linkados
pelo `cc`/`lld-link` (a perna Windows já linka 3 objetos). Custo: ~100 `extern` + 71 `#define`
gerados e um check de deriva; `nnodes` sem solução (pede acessor ao mc); macOS perde o
`macho-exe` embutido e passa a exigir `cc`; e o fixpoint cobre só a METADE teko.

**Rito do fixpoint** (protocolo do `scripts/bootstrap.sh` do mc, verbatim):
`teko0` = `mc build ngen` (mc estoque, hoje) → `teko1` = `teko0 build . --config bootstrap.toml
--entry-only` sobre `mc_teko.tk` → `teko2` = idem com `teko1` → `teko3` = idem com `teko2` →
**`cmp` dos OBJETOS `teko2.o`/`teko3.o`** (não do executável: assinatura/`interp` variam por
perna) **+ `--dump-asm` com diff vazio**. `teko2 == teko3` é o único fixpoint que importa daqui
para a frente (o `gen2==gen3` do `src/` congelado morreu com o D211).

### (f) Sequência de crumbs

| # | o que | arquivos | gate |
|---|---|---|---|
| **S1** | partes em vez do bundle (D64.1) | `ngen/core_teko.mc` (novo), `ngen/mc.toml` (`core`, `modules`) | 45/45 × 5 pernas; tamanho MEDIDO cai (baseline 1 588 677 B); `mc limits` sem regressão |
| **S1m** | régua de medição | `ngen/scripts/measure.sh` (novo) | roda nas 5 pernas, imprime seções + bytes + `limits` |
| **S2** | `teko build`, tabela de subcomandos própria (D64.3/D64.4/D64.5) | `core_teko.mc`, `teko.mc` (`tk_build`), `mc.toml` (`out = "build/teko"`), `.github/workflows/ngen.yml` | 45/45; `teko` sem argumento imprime SÓ a usage do teko; `teko build` acha `teko.toml` e `mc.toml` |
| **S2d** | censo `type_disable`/`intrinsic_disable` (D64.6) | só §64 / `README.md` | lista vazia registrada com a regra |
| **S3** | `teko_init()` + `[package]` (D64.7) | `teko.mc`→`teko_init`, `ngen/user.mc` (novo), `ngen/mc.toml` `[package]` | 45/45; `mc pkg hash ngen` estável entre dois runs |
| **S4.0** | sonda de transparência | nenhum (script descartável) | **JÁ FEITA** — resultado em (e); repetir após S1 |
| **S4.1** | transliteração `.mc`→`.tk` + renome de `scope`/`out` internos (~58 sítios) | os 31 `ngen/*.mc` → `.tk`, `lib/rt.mc` → `lib/rt.tk`, `mc.toml` | 45/45; `--dump-ast` das 45 idêntico; objeto do compilador idêntico a menos das strings de nome de arquivo |
| **S4.2** | `mc_teko.tk` + `ngen/scripts/bootstrap.sh` | novos | teko1 compila as 45; **`cmp teko2.o teko3.o`** vazio; `--dump-asm` diff vazio — **g1 RESOLVIDO pelo `source_claim` do mc 0.15.8; bloqueio novo (o `while`/`for` do prelúdio) em §70(f)** |
| **S4.3** | perna de fixpoint no CI | `.github/workflows/ngen.yml` | a 6ª perna verde |
| **S4.4+** | teko-ificação por módulo (D64.8, uma onda por módulo) | um `.tk` por vez | 45/45 + fixpoint a CADA módulo |

S1→S2→S3 são independentes de S4 e podem ir já. S4.1 é independente da rota e pode ir já. Só
S4.2 espera o pedido.

### (g) Pedidos e forks

**(g1) Pedido ao mc — texto pronto.** "Ao montar um compilador de dialeto sobre `<mc/core_min>` e
as partes, uma palavra que o módulo registra (`syntax`/`syntax_stmt`/`syntax_expr` → `word_add` →
`tok_add`) vira token em TODO fonte, inclusive nos fontes do próprio núcleo servidos pelo bundle.
Com o dialeto teko o núcleo deixa de compilar em `mc/objmodel:293: name expected` (o parâmetro
`i64 type` de `reloc_add`). O censo completo das colisões com as 36 palavras da teko é `type`
(23 sítios: `objmodel.mc`, `gen_resolve.mc`, `parse.mc`, `gen_walk.mc` + writers/driver) e `out`
(43 sítios: `sha256.mc`, `cli.mc`) — mais nada. Pedimos UMA das duas: **(1)** renomear esses
parâmetros/locais (`type`→`rty`, `out`→`dst`), mudança mecânica e sem efeito em código gerado, na
mesma classe dos 4 nomes que o M41 moveu para as partes se sustentarem; ou **(2)**, melhor e
geral, um modo de escopar as palavras de um módulo às fontes que ele reivindica (o `on_source` já
diz qual fonte está aberta), que tornaria QUALQUER dialeto componível com o núcleo para sempre.
Se a resposta for (1), registrem que os fontes do núcleo passam a evitar 36 palavras — mandamos a
lista. Secundário: `subcommand_usage()` imprime todas as entradas enquanto o dispatch é
last-wins (`hooks.mc:1029`) — re-registrar um nome duplica a linha de usage; contornamos sem
pedir nada (D64.3), mas o desacordo entre dispatch e usage parece defeito. Terceiro (só se a
rota B for a escolhida): um acessor `i64 ast_nnodes()`, porque `extern` declara função e não
variável, e dois sítios nossos leem o global `nnodes`."

**(g2) Fork para o dono — rota A ou rota B.** A rota A (uma unidade) dá o fixpoint sobre o binário
INTEIRO, não pede espelho de API nem linker no macOS, e é o que "auto-hospedagem" quer dizer —
mas depende do mc aceitar (g1). A rota B fecha hoje, sem pedir nada, ao custo de ~175 linhas
geradas de espelho + um check de deriva + um acessor que falta + o fixpoint cobrindo só a metade
teko. **Recomendação: A**, com B escrita como fallback e S4.1 (que serve às duas) indo já.

**(g3) Ratificação — o que "reescrever os módulos em teko" significa (D64.8).** Como `.tk` já
contém a superfície do núcleo, a transliteração é imediata e a auto-hospedagem se prova no
fixpoint; a adoção de `class`/`foreach`/`T[]` dentro do compilador é uma onda POSTERIOR, por
módulo. Se o dono quiser o contrário (só considerar "reescrito" o que usa construtos teko), S4.4+
deixa de ser opcional e vira 31 crumbs obrigatórios — dizer qual.

### (h) Riscos

1. **Colisão de palavras (alta, medida).** É o único bloqueio real; (g1) é a saída. Mitigação
   local disponível: a teko pode DEIXAR de registrar `type` (é só honest-stop hoje) e cortar 23
   dos 66 sítios — `out` é load-bearing e não tem essa saída.
2. **Regressão de comportamento por parte omitida (média).** `core_pkg`/`core_sandbox` somem com
   as usage lines e com `mc pkg`/`mc sandbox`; o CI não olha usage — S1 acrescenta a asserção ao
   `measure.sh`, não uma fixture.
3. **`lex_set_libs`/`on_plan` esquecidos ao trocar `mc_build_init()` por peças (média).** Sintoma:
   `<name>` deixa de resolver por lock, ou as tabelas param de ser pré-dimensionadas (o pico de
   memória sobe e `mc limits` acusa). O `measure.sh` do S1m é quem pega.
4. **Deriva do espelho de API na rota B (alta, só na B).** 110 funções + 71 `#define` copiados de
   uma versão do mc que o CI resolve como "latest" — versão do mc tem de ser PINADA se a rota B
   for escolhida.
5. **Passes da teko sobre 8 500 linhas de núcleo, na rota A (média).** Tempo de compilação e
   pressão de tabela sobem; e um pass que reescrevesse nó de núcleo seria bug silencioso. O gate
   é forte e barato: o objeto que o teko0 produz para os fontes do núcleo tem de ser
   **byte-idêntico** ao que o mc estoque produz — a transparência vira fato medido, não promessa.
6. **Baixos:** a troca `mc-teko`→`teko` toca 4 lugares do workflow (falha barulhenta); e a
   publicação do pacote depende do registro do mc abrir (tudo menos o PR é fazível hoje).

## 65. Errata do §64 — S1/S1m landados (2026-09-06)

**S1 landado** (`ngen/core_teko.mc` novo, `ngen/mc.toml` `[compiler]` com `core`/`modules`):
compilador construído a partir de `<mc/core_min>` + as quatro partes de D64.1 (`core_teko.mc`)
+ `teko.mc`, `mc_build_init()` ainda chamado (S1 = só menos partes, D64.3 tira na S2). Medido
no host macOS/aarch64, `mc` 0.15.5:

| | antes (bundle) | depois (partes) | Δ |
|---|---|---|---|
| tamanho (`wc -c`) | 1 588 681 B | 1 489 364 B | **−99 317 (−6,25 %)** |
| `__TEXT,__text` | 782 140 | 699 404 | −82 736 |
| `__TEXT,__cstring` | 44 022 | 38 603 | −5 419 |
| `__DATA,__data` | 570 600 | 569 576 | −1 024 |
| `mc limits` `nodes`/`funcs`/`ins`/`symbols`/`heap` (compilação do próprio `teko.mc`) | maiores | menores, mesmo `verdict ok`, `grow 0` | menos fonte no pré-scan (D64.1 §(h) risco 3, sem regressão) |
| usage sem argumento | 6 linhas de `mc build\|limits\|sysroot` **+** 5 de `pkg`/`update`/`sandbox` | as mesmas 6, **sem** as 5 de `pkg`/`update`/`sandbox` | `<mc/core_pkg>`/`<mc/core_sandbox>` de fato ausentes (D64.1 §(h) risco 2) |

`--dump-ast` das 45 fixtures contra o compilador da base `63e28f90`: **`same=45 diff=0`**
(menos partes não mudou a árvore de nenhuma). `--entry-only` **45/45**, exit codes batendo com
`// expect-exit:`. A derivação do config por perna (`.github/workflows/ngen.yml`, o `awk`/`sed`
que remove `[linker]` e troca `[target]`) foi conferida contra o `mc.toml` novo: `[compiler]`
(agora com `core`+`modules`) atravessa intacto — a regra de espaçamento do `sed` (`out   = ` com
3 espaços, só o `[project].out`) não toca o `out     = ` de 5 espaços do `[compiler]`. Nenhuma
mudança em `ngen/mc.toml` além do bloco `[compiler]`; nenhuma mudança em `.github/workflows/ngen.yml`.

**S1m landado** (`ngen/scripts/measure.sh` novo): dado o binário e o config, imprime bytes,
seções (`--dump-syms` sobre o `.mc` gerado, mesma técnica de `scripts/check-parts.sh` do mc,
mas sem nome de seção hardcoded — Mach-O/ELF/COFF nomeiam diferente) e `mc limits DIR --config
CONFIG`. POSIX `sh`, sem bashismo; roda local hoje, não ligado ao CI ainda (S4.3 decide isso).

Nada no §64(f) mudou de rumo; isto só registra os números que a tabela da etapa 1 previu como
"medição feita nesta sessão" — a mesma sessão que agora landou o crumb.

## 66. Errata do §64 — S2 landado; censo S2d (2026-09-06)

**S2 landado** (`ngen/core_teko.mc`, `ngen/teko.mc`, `ngen/mc.toml`,
`.github/workflows/ngen.yml`): `main()` para de chamar `mc_build_init()`
(D64.3) e chama as três peças públicas que ela era feita de —
`lex_set_libs(&libs_open)`, `sysroots_init()`, `on_plan(&mc_plan)` — mais a
tabela própria de subcomandos, `subcommand("build", &tk_build, ...)` e
`subcommand("limits", &tk_limits, ...)`. `sysroot` fica de fora (só a perna
Windows do CI precisa, e ela já monta o sysroot com o `mc` de release, nunca
com `teko` — `ngen.yml`'s own "build the Windows sysroot" step).

Achado durante a implementação, fora do que o §64(c) previu: `mc` sem
subcomando reconhecido cai em `cli.mc`'s `usage()`, que imprime TRÊS linhas
fixas próprias (`usage: mc [--dump-tokens|...] source.mc [-o out]`,
`mc --host`, `mc --version`) ANTES de chamar `subcommand_usage()` — linhas
que este arquivo não pode editar (`src/` do mc é intocado). Registrar só a
tabela própria não bastava para "`teko` sem argumento imprime SÓ a usage do
teko": `main()` intercepta o caso `argc < 2` (o único que cai nesse caminho
sem nada de fato para compilar) e chama `subcommand_usage()` diretamente,
nunca `mc_main`'s own `usage()`. Prova: `ngen/build/teko` sem argumento
imprime exatamente as duas linhas de `teko build`/`teko limits`, exit 1.

D64.4: `tk_build(argc, argv)` (`ngen/teko.mc`) faz uma varredura leve (só
`--config`/`--sysroot-dir`/`--libs-dir`, o bastante para achar DIR e saber
se `--config` já foi dado) e delega a `drv_build` do núcleo — a lógica real
do build não é reimplementada. D64.5: sem `--config`, `DIR/teko.toml` vence
quando existe (`path_exists`, `path_norm(tm_cat(dir, "/teko.toml"))`);
senão, `DIR/mc.toml` (o default do próprio `drv_build`) aplica-se
inalterado — prova por probe: um `ngen/teko.toml` avulso (out distinto) fez
`teko build ngen --entry-only` (sem `--config`) escrever no `out` do
`teko.toml`, não no de `ngen/mc.toml`; `--config` explícito continuou
vencendo os dois.

Achado adicional, também fora do §64(c): `drv_limits` (núcleo) distingue
"arquivo único" de "diretório de projeto" com `drv_is_source`, que exige
sufixo `.mc` — uma fonte teko é `.tk`, então `teko limits
ngen/tests/hello.tk` com `&drv_limits` cru caía no ramo de diretório e
morria com `cannot open: .../mc.toml`. `tk_limits` (`ngen/teko.mc`) resolve:
mesma varredura leve de `tk_build`, e um caminho `.tk` (`tk_is_source`, o
mesmo teste de três bytes de `drv_is_source`, com `t`/`k` no lugar de
`m`/`c`) toma as três chamadas públicas que `drv_limits` tomaria para um
`.mc` (`lim_compile_file`/`lim_report`/`lim_exit_code`, `src/limits.mc`);
qualquer outro caso (diretório, `.mc`, sem argumento, flag desconhecida) cai
em `drv_limits` inalterado. Isto está dentro do escopo de S2 (não um item
adjacente): é exatamente o critério de aceite do §64(f) —
"`teko limits ngen/tests/hello.tk` funciona" — e sem ele não funcionava.

`[compiler].out` virou `"build/teko"` em `ngen/mc.toml` (única chave
tocada, D64.5's own authorization); `.github/workflows/ngen.yml` trocou a
UMA linha que nomeava `ngen/build/mc-teko` (o laço de fixtures) por
`ngen/build/teko` — nenhuma outra linha do workflow muda, `mc build ngen
--config ngen/mc.ci.toml` (a etapa que ENSINA o compilador com o `mc` de
release) continua igual, ela nunca nomeou o binário produzido.

Prova (host macOS/aarch64, `mc` 0.15.5, config derivado por `sed` como o
CI faz): build do zero, **45/45** fixtures via `teko build ngen --config
... --entry-only`; `--dump-ast` das 45 contra o compilador da base
(`477ea715`, `mc-teko` cru) — **`same=45 diff=0`**; `mc limits ngen`
`verdict ok`; `teko` sem argumento — as duas linhas, exit 1; `teko limits
ngen/tests/hello.tk` — roda e reporta (exit 3, o mesmo "grew" que qualquer
`mc limits arquivo.mc` avulso dá sem um plano de projeto — não é regressão,
é o comportamento correto de arquivo único).

### S2d — censo `type_disable`/`intrinsic_disable` (D64.6)

**Lista: VAZIA.** Confirmado sobre o `ngen/*.mc` atual (`ngen/teko_type.mc`,
`ngen/teko_float.mc`):

* `bool`, `char`, `byte`, `isize`, `usize`, `ptr`, `str` são `type_alias`
  sobre um tipo do núcleo (`TY_U8`/`TY_U32`/`TY_I64`/`TY_U64`/`TY_UPTR`) —
  IDENTIDADE, não um segundo tipo: não há o que desabilitar, a palavra do
  núcleo continua sendo o mesmo tipo.
* `f32`/`f64` vêm do `type_new` da própria lib `<float>` (M24) — a teko só
  liga (`float_init()` + as tabelas de máquina), não redefine nem substitui
  palavra nenhuma.
* `i32` é do núcleo (M45) — a teko o QUER, não o disputa.
* `ld64`/`st64`/`ld8`/`st8`/`ld32`/`callp` são usados diretamente pelas
  fixtures (`primitives_ptr.tk`, `surface_import.tk`) e pelos próprios
  fontes do núcleo — `intrinsic_disable` em qualquer um deles quebraria a
  auto-hospedagem da etapa 4 (rota A, §64(e)), que precisa compilar os
  fontes do núcleo tal como estão.

**Regra (D64.6, liga a etapa 2 à 4):** um `type_disable`/`intrinsic_disable`
só é legítimo quando a palavra (i) é de fato proibida na superfície teko E
(ii) não aparece nos fontes do núcleo — a lista de hoje não tem um único
candidato que passe as duas. Lista vazia é um resultado válido, registrado
aqui e em `ngen/README.md`; nenhum código muda.

## 67. Errata do §64 — S3 landado; o pacote `teko` (2026-09-06)

**S3 landado** (`ngen/teko.mc`, `ngen/user.mc` novo, `ngen/mc.toml` — D64.7): `teko.mc` para de
definir `user_init` e exporta `void teko_init()`, mesmo corpo; `ngen/user.mc`, do PROJETO e não do
pacote, é `void user_init() { teko_init(); }`; `ngen/mc.toml`'s `[compiler].modules` ganha
`"user.mc"` no fim. `[package]` novo:

```toml
[package]
name   = "teko"
lib    = "lib/rt.mc"
module = "teko.mc"
files  = [ "lib/rt.mc", "teko.mc", "teko_access.mc", … os 30 irmãos … ]
check  = ["teko.mc", "lib/rt.mc"]
```

Achado durante a implementação, fora do que o §64(d) previu: se `core_teko.mc` (o `main()` deste
repositório) pertence a `[package].files` ficou em aberto ali ("decida pelo packages.md e
registre"). O precedente `tests/pkg/src/teach-1.0.0` do mc resolve: um pacote "módulo de
compilador" mínimo tem `files = ["mc_teach.mc"]`, `module = "mc_teach.mc"` — o arquivo que
registra os hooks e exporta `<nome>_init()` — e NUNCA lista o `user.mc`/driver que o consome (esse
é do lado do CONSUMIDOR, não do pacote). `ngen/core_teko.mc` está para `ngen/teko.mc` exatamente
como o `user.mc` de qualquer consumidor do pacote `teach` está para `mc_teach.mc`: monta o
compilador (host layer via `[compiler].core`, as quatro partes do D64.1, `main()`), não é
conteúdo do pacote. `[package].files` fica só com os 30 `teko_*.mc` + `teko.mc` + `lib/rt.mc`;
`core_teko.mc` e `user.mc` ficam de fora.

`check = ["teko.mc", "lib/rt.mc"]` segue §64(d): o pacote é "ambos" (duas respostas, `lib` e
`module`, não uma só), e sem `check` a validação do registro cairia só em `lib` (packages.md §3:
"com no `check` key the unit is `lib`"). As duas entradas já estão em `files`, como a regra exige.

Prova (host macOS/aarch64, `mc` 0.15.5, config derivado por `sed` como o CI faz): build do zero,
**45/45** fixtures via `teko build ngen --config … --entry-only`; `--dump-ast` das 45 contra o
compilador da base `faac4d56` — **`same=45 diff=0`**; `mc limits ngen` (o binário de RELEASE, a
régua de S1/S2) `verdict ok`; `teko limits ngen/tests/hello.tk` inalterado (exit 3, "grew"); `mc
pkg hash ngen` estável entre dois runs —
`0f85d3fbbced52f69716fd36366c9209cea99a706121de3962061e1a8435fce4` — e idêntico via `mc pkg hash .`
de dentro de `ngen/` (confirma o fix do mc 0.15.4 para `dep_under` com `dir == "."`).

Achado adicional: `ngen/build/teko limits ngen` (o binário TAUGHT, não o de release, sobre um
DIRETÓRIO) recompila `build/teko.mc` consigo mesmo e bate em `mc/objmodel:293: name expected` —
exatamente a colisão de palavra `type` do §64(e)/S4.0, medida ali contra o mc estoque. Não é uma
regressão de S3: a medição de `mc limits ngen` sempre foi feita com o `mc` de release (S1/S2), e
`teko limits DIR` nunca foi o caminho certo para essa régua — só `teko limits FILE.tk` (S2's
próprio critério de aceite) o é. Registrado aqui para não ser repetido como falso alarme.

`.github/workflows/ngen.yml` não muda: conferido que o `awk`/`sed` que corta `[linker]` e
reescreve `[target]`/`entry`/`out` (linhas 333-340) não toca `[package]` — a seção não tem `out`
nem `[linker]`, atravessa inteira e inerte, o mesmo comportamento que `[compiler]` já demonstrou
em S1.

Publicação (GitHub Release na tag, tree hash, PR em `minicompiler/mc-registry`) fica de fora — o
registro do mc ainda não abriu para pacotes de terceiros (NOTICES-teko.md, 2026-09-06: só
`minicompiler/mc` está registrado, e mesmo essa entrada saiu vermelha de propósito até o S5b do
servidor). `mc pkg check` não roda localmente pela mesma razão (lê `index/<nome>.toml` de um
registro, não uma árvore local); `mc pkg verify ngen` roda offline e devolve "verified 0 packages
against mc.lock" (sem `[deps]`, exit 0).

## 68. Errata do §64 — S4.1 landado (transliteração `.mc` → `.tk` + renome de colisões, 2026-09-06)

Dois commits, `feat/ngen-s41-translit`.

**Commit 1 — renome de identificadores internos.** O censo do §64(e) auditara as colisões da
teko contra os fontes do NÚCLEO (`type`/`out`, 66 sítios) e, à parte, contra o próprio `ngen/`
("scope (21) e out (37) — ~58 sítios"). Reauditoria mecânica (grep-de-código, comentários e
strings descontados, contra as **39 palavras** que `ngen/*.mc` de fato registra por
`syntax`/`syntax_stmt`/`syntax_expr`/`type_new`/`type_alias`) confirma os 58 e acha um terceiro:
**`params`** (75 sítios, `teko_class.mc`, `teko_deleg.mc`, `teko_iface.mc`, `teko_ns.mc`,
`teko_ops.mc`, `teko_prop.mc`, `teko_struct.mc`). Causa: `tk_ty_params = type_new("params", 8, 8,
TK_INT)` (`teko_type.mc:68`) chama `alias_add`, que chama `word_add` (`hooks.mc:555-562`) — a
MESMA reserva program-wide que `syntax()` faz (o comentário do próprio núcleo, `hooks.mc:586-588`:
"the word is reserved PROGRAM-WIDE, exactly as `type_alias`'s is"). O §64(e) tratara só as chamadas
`syntax*`; `type_new`/`type_alias` reservam pela mesma tabela e ficaram de fora do censo original.
Os demais 36 candidatos (`base`, `value`, `static`, `get`/`set`, `case`, `in`, `or`, `use`, os
tipos-alias `bool`/`char`/`byte`/`isize`/`usize`/`ptr`/`str`/`f32`/`f64`, `class`/`interface`/
`trait`/`namespace`/`import`/`using`/`delegate`/`struct`/`new`/`inject`/`this`/`true`/`false`/
`null`/`var`/`const`/`match`/`when`/`while`/`for`/`do`/`foreach`/`switch`/`public`/`internal`/
`abstract`/`partial`/`ref`) não colidem dentro do `ngen/` — ou lidos contextualmente sem
`tok_add`, ou (`str`/`f64`) usados só em posição de TIPO, o uso para o qual foram registrados.

Renomes: `scope` → `dscope`, `out` → `dst`, `params` → `prs` — mecânico, comportamento
preservado, comentários e mensagens de erro intocados (só o identificador de código muda).

**Commit 2 — `git mv` .mc → .tk.** Os 31 módulos do pacote `teko` (`teko.mc` + os 30
`teko_*.mc`) e `lib/rt.mc` → `.tk`; `core_teko.mc` (o `main()` deste repositório) e `user.mc` (o
driver do projeto) ficam `.mc` — não são do pacote (D64.7/§67, mesmo precedente `teach-1.0.0` do
mc). Atualizados: os 30 `#include "teko_X.tk"` de `teko.tk`, os 39 fixtures + `parts/ns_file.tk`
que incluem `../lib/rt.tk`, `ngen/mc.toml` (`[compiler].modules`, `[package]` `lib`/`module`/
`files`/`check`) e as menções em prosa (doc comments) ao nome de arquivo — próprio E cruzado —
em TODOS os arquivos tocados (`ngen/*.tk`, `ngen/lib/rt.tk`, os 39 fixtures, `ngen/core_teko.mc`,
`ngen/user.mc`, `ngen/README.md`); a única referência a `rt.mc` que sobrevive é
`examples/lang/lib/rt.mc`, um arquivo DIFERENTE no repositório `mc` (`ngen/lib/rt.tk:4`).

**Auditoria do mc estoque — nenhum shim necessário.** Três pontos verificados contra
`minicompiler/mc`'s próprio `src/` (não presumidos):
1. `[compiler].modules` — `driver.mc`'s `drv_gen_compiler`/`drv_include` escrevem
   `#include "<path>"` literal para cada entrada; não há checagem de sufixo.
2. `#include "rel"` (dentro dos módulos e dos fixtures) — `lex.mc`'s `lex_include` resolve por
   caminho real em disco (`lex_find_path`) quando não está dentro de um frame bundled; sem
   exigência de `.mc`.
3. `[package]` (`lib`/`module`/`files`/`check`, e a resolução `<teko/x>` que um consumidor
   bundlaria) — `deps.mc`'s `libs_open` tenta o nome EXATO primeiro, só reapende `.mc` se a
   tentativa falhar; `docs/reference/packages.md`: "a trailing `.mc` is dropped from every
   `<...>` name... a payload with another extension keeps it".

Nenhum dos três exige sufixo `.mc` — `.tk` funciona como módulo de compilador e como membro de
pacote sem ajuste no mc. `mc limits`'s `drv_is_source` (só reconhece `.mc` para o modo
arquivo-avulso) é irrelevante aqui: já resolvido do lado do `ngen` desde S2 (`tk_limits`'s
`tk_is_source`, §66).

**Gate** (host macOS/aarch64, `mc` 0.15.5, config derivado por `sed` como o CI faz): dois builds
do zero (`rm -rf ngen/build`), um por commit — **45/45** em cada; `--dump-ast` das 45 byte-idêntico
contra a base `6c50aa98` nos dois; `mc limits ngen` `verdict ok`. Medição extra do commit 2: o
`build/teko` compilado ANTES do `git mv` (a partir do commit 1) e o compilado DEPOIS são
**byte-idênticos** (`cmp` limpo) — mais forte que "a menos das strings de nome de arquivo": o
único diff é textual, no `#include` do glue GERADO (`build/teko.mc:5`, `"../teko.mc"` →
`"../teko.tk"`); o binário resultante não embute o caminho-fonte de `ngen/*.tk` em lugar nenhum
(o `p_file()`/`err_at` do compilador TAUGHT só embute o caminho do arquivo que ELE compila em
seguida — `tests/*.tk` — não o do seu próprio código-fonte). `mc pkg hash ngen` mudou (nomes de
arquivo entram no hash) para
`331ee075474088484b875fefc2a740bbbb27863852e0109ff24b35c723a2a253`, estável entre duas execuções.

Sem PR, sem dreno — forward-only para `fix/retirement` como o resto do `ngen/`.

## 69. Higiene 2 — seis dívidas de checagem/ergonomia do HANDOFF §5 (2026-09-06)

Crumb independente do desenho da entrega 4 (nenhuma superfície nova): seis dívidas registradas
pelos verificadores de crumbs anteriores (COMPAT+HIGIENE/D226 item 3; K3/G1/K5 item 4/5; K4c item
6; itens 1/2 são achados novos deste crumb sobre a mesma checagem do D226). Base `4e9c87ea`, três
commits, um por item fechado — `46a0ab63` (item 1), `04588e39` (item 2), `91de53ca` (item 6).

**Item 1 — argumento via vtable/itab sem checagem de tipo.** `tk_rc_call_args` (teko_rc.tk, D226)
já checava o argumento de uma chamada DIRETA (função livre, método não-virtual, construtor) contra
`decl_param_type`, mas nunca alcançava uma chamada VIRTUAL (`tk_emit_call`'s branch `slot >= 0`)
nem uma chamada de INTERFACE (`tk_itab_emit`) — as duas rebaixam para `callp`, um nome opaco que
`decl_find` nunca resolve. `tk_vcall_args_check` (teko_expr.tk) resolve `d = decl_find(mt_fn_at(mi))`
— o método CONCRETO por trás do slot escolhido — e aplica `tk_check_compat(decl_param_type(d, i),
...)` por índice, exatamente como `tk_rc_call_args`. Para itab não há um `decl_find`-ável (um
membro `abstract` de interface não tem função própria) — `im_prs` (teko_iface.tk, nova coluna na
tabela de membros, o MESMO `prs` que `tk_ifmeth_add`/`tk_iface_accessor` já constroem pra virar
`sig`) guarda a lista de parâmetros da PRÓPRIA declaração da interface; `tk_ifargs_check` percorre
essa lista por índice (posição 0 é o receptor, mesma forma que `tk_params`/`tk_prop_params` já
dão) — válido porque toda implementação é OBRIGADA a bater a mesma assinatura (`im_sig`, a
checagem de conformidade já compara por string exata).

Achado que forçou uma peça nova: as duas checagens rodam no PARSE (onde `mi`/`k` — o método
escolhido — ainda está em mãos; guardar essa escolha pra uma pass geraria uma tabela paralela só
pra recuperar o que o parser já sabia), mas o oráculo de tipo pass-time (`tk_ty_of`,
`tk_ty_scope_find`) só existe DEPOIS que a árvore inteira foi lida — testado por probe, um
argumento local simples (`square`) respondia `ety < 0` (desconhecido) e a checagem nunca disparava.
`tk_pty_of` (teko_struct.tk) é o oráculo PARSE-TIME: a tag já gravada num nó (`tk_xt_ty`, ex. o
retorno de um `new C()` fresco) ou, pra um `N_IDENT` bare, o tipo declarado do local mais recente
com esse nome (`tk_slv_find` — a MESMA tabela que o `use (...)` do K4 já consulta pra capturas).
O que não é nem uma coisa nem outra (um load de campo ainda não tagueado, uma chamada ainda não
resolvida) responde -1 e a checagem SILENCIOSAMENTE não dispara — a mesma regra que
`tk_check_field_store` (D226) já dá a uma fonte desconhecida no parse.

Fixture: `types_class.tk` ganha `VShape`/`VSquare`/`Circle`, um método VIRTUAL
`useCircle(Circle c)` chamado através de um receptor `VShape`-tipado segurando um `VSquare` —
prova o caminho vtable aceito. O caminho itab só tem prova por probe (uma interface com um método
de parâmetro de classe, chamada correta E incorreta) — o crumb não pediu fixture nova.

**Item 2 — `ref`/`out` com tipo apontado errado.** `tk_ref_check_call` (teko_ref.tk) já checava a
FORMA do argumento (`ref` contra `ref`, `out` contra `out`, nenhum contra nenhum), nunca o
APONTADO — `setIt(ref Circle)` chamado com `ref square` (`Square`, uma classe não relacionada, OU
uma que DERIVA de `Circle`) compilava calado. `tk_ref_check_pointee` compara os dois apontados por
IDENTIDADE (`ti != ei`, nunca `tk_row_fits`'s deriva/implementa) — C#'s própria regra: `ref`/`out`
não é covariante, nem de uma classe pra sua base. O apontado de um `ref`/`out` de um LOCAL bare
(`tk_ref_addr`'s branch final) não era capturado (`set_pty(pty, 0 - 1)`) — trocado por
`tk_slv_find(name)`, o MESMO fallback do item 1.

Fixture: `surface_refout.tk` ganha `Gem`/`setGem`/`gcheck`, uma SEGUNDA classe distinta de `Cell`,
provando que o match idêntico segue aceito (não é um acidente de só existir uma classe no
arquivo). O mismatch (`ref Circle`/`ref Square`) E o caso derivado-pra-base (`ref Animal`/`ref
Dog`, também recusado — a prova de que a checagem é IDÊNTICO, não deriva) só têm prova por probe.

**Item 3 — `use (g)` de global: DÍVIDA, caminho mapeado.** A mensagem genérica
("`use` captures a local; this name is not one") persiste pra um global — D226 já tinha registrado
isso como dívida ("deferir a rejeição arriscaria a tabela de captura, não vale a melhoria
cosmética"). Investigado de novo aqui: não é só arriscado, é IMPOSSÍVEL sem uma tabela nova — não
existe hook `on_global` (só `on_stmt`/`on_source`/etc., `docs/reference/hooks.md`), `top_add` é
write-only (nenhuma API devolve "o que já foi adicionado à unidade"), e `decl_find`/`global_find`
(a API que RESOLVERIA um nome) só respondem — o segundo nem existe pro módulo, é interno ao
CODEGEN (`gen_resolve.mc`), tarde demais pro parse de `use (...)`. Caminho viável: um pré-scan
estilo O1 (`teko_fwd.tk` — os primitivos `tk_fwd_skip_ws`/`skip_quoted`/`skip_line`/
`skip_block_comment`/`word`/`word_eq` já são reusáveis, extraídos pro §50 O1) que reconheça
`TYPE IDENT (';'|'='|'[')` em profundidade 0 e registre `IDENT` numa tabela pequena — mas a parte
cara é EXCLUIR toda palavra que hoje abre outra coisa em duas posições (`namespace X.Y;`/
`using X;`/`import X;` leem exatamente "duas palavras terminando em `;`" também) pra não
mis-ensinar um nome de namespace/using como global. Maior que ~60 linhas feito com essa exclusão
correta — registrado, não implementado.

**Item 4 — `T[]` global em `namespace` fica BARE: DÍVIDA, premissa corrigida, caminho mapeado.**
O crumb assumia "`tk_ns_pass` já qualifica globais escalares" — FALSO, confirmado por probe:
`namespace geo { i64 counter; ... }` é recusado hoje (`a global is declared outside every
namespace`), a mesma recusa de sempre; só um `T[]` atravessa, pela exceção pontual que o G1 já
registrou como dívida. Não há "a mesma tabela" pra seguir. Caminho viável (que NÃO esbarra no
bloqueio de fase que o G1 apontou — "`hg_*` só existe depois de `tk_array_pass`"): qualificar a
DECLARAÇÃO em si no laço do bloco do `namespace` (`tk_namespace`, o mesmo instante em que
`tk_ns_decl_note` já qualifica uma função livre) e manter uma tabela PRÓPRIA e pequena — "nomes de
`T[]` global namespaced vistos até agora" — populada nesse MESMO laço (não depende de `hg_*`),
consultada pelo rewrite genérico de identificador que já existe pra `const`
(`tk_ns_rewrite_ident`/`tk_ns_walk_calls_in`). Tecnicamente possível dentro da ordem de fases
atual — mas é tabela nova + resolvedor por prefixo/using (o padrão do `const`) + wiring em dois
lugares, feature, não item de higiene de uma linha. Registrado, não implementado.

**Item 5 — `foreach` sobre `T[]` global: DÍVIDA, mesma raiz do item 4.** `tk_fe_source`
(teko_loop.tk, K5) resolve a fonte inteiramente no PARSE, por desenho deliberado ("nunca um
`parser cannot type -> defer`", o próprio header do K5) — `tk_hp_find`/`tk_local_find` (parâmetro/
local) respondem na hora; um `T[]` GLOBAL só existe em `hg_*`, POPULADO NO PASS. Ensinar isso
exigiria OU o mesmo pré-scan do item 3/4 (pra `tk_fe_bare` responder na hora), OU quebrar a
promessa de desenho do K5 com uma forma DEFERIDA de `foreach` — não um load/store isolado como as
outras dívidas desse arquivo, mas reescrever `cond`/a carga do elemento (todo o `once`/`outer`
desaçucarado) depois que `hg_*` existe. Qualquer um dos dois caminhos é maior que o orçamento
deste item. Registrado, não implementado.

**Item 6 — lambda aninhada, K4c: FECHADO, causa raiz achada.** A dívida do verificador K4c dizia
"`use (k)` em dois níveis → `k is not captured`; localização de erro errada" sem investigar a
causa. Achada: `tk_nlc` (o índice da tabela de captura `lc_name`/`lc_ty`/`lc_byref`) resetava pra 0
no INÍCIO e no FIM de `tk_lambda_finish` — uma lambda B construída DENTRO do corpo de uma lambda A
roda o PRÓPRIO ciclo inteiro (`use`, corpo, limpeza) enquanto o PARSE do corpo de A ainda está em
curso (B é só mais uma expressão dentro desse corpo) — o reset de B, nas duas pontas, apaga as
capturas de A que ainda estavam "em voo". Reproduzido por probe contra o código PRÉ-fix (`git
stash` das mudanças, rebuild, mesmo probe): `io:25: teko: k is not captured; add it to use (...)`
— bate a mensagem relatada, E confirma a segunda metade ("localização errada": `io` não é nome de
arquivo nenhum do probe).

Fix: `tk_lc_base`, uma janela `[tk_lc_base, tk_nlc)` — cada `tk_lambda_finish` salva a janela da
lambda ENVOLVENTE, abre a própria em `tk_nlc` (o valor corrente, não 0) e restaura as duas ao
sair, em vez de zerar. `tk_lc_find`/`tk_lc_dup` buscam só dentro da janela corrente. Os quatro
geradores que assumiam índice absoluto 0 pro offset dentro do objeto do closure
(`tk_lambda_prologue`, `tk_lambda_release_fn`, `tk_lambda_alloc_params`, `tk_lambda_alloc_stmts`)
passam a calcular `i - tk_lc_base` pro offset de byte (o objeto de CADA lambda começa fresco no
byte 24, não importa o índice absoluto na tabela global); o nome do parâmetro do alocador e o
`__lamrefN` do prólogo continuam pelo índice ABSOLUTO (preciso — é o mesmo índice que
`tk_lc_find` devolve e `tk_lam_walk` usa pra montar `tk_lam_refaddr`). `TK_MAXLAMCAP` sobe de 8
pra 32, já que a tabela agora soma as capturas de TODA lambda em voo, não só uma — sem essa folga,
uma lambda de 5 capturas dentro de outra de 4 estouraria um teto pensado pra UMA lambda de cada
vez.

Fixture: `surface_lambda.tk` ganha `nested_check` — `use (k)` nos dois níveis, o cenário exato do
relato, `outer(1)` fazendo `inner(1)=1+10=11` e retornando `11+10=21`.

**Gate** (host macOS/aarch64, `mc` 0.15.5, os três commits juntos): `rm -rf ngen/build`, build do
zero; `--entry-only` **45/45** (zero fixture nova, três tocadas); `--dump-ast` das **42 fixtures
não tocadas byte-idêntico** à base `4e9c87ea` (`same=42`, os 3 diffs restantes são exatamente as
tocadas); `mc limits ngen` `verdict ok`, `intrin` 8/8 nos dois lados (zero intrínseco novo, as
duas checagens novas dos itens 1/2 são funções puras de `err_at`, nenhum hook novo). Probes (fora
de `ngen/tests/`, descartados): item 1 — argumento errado via vtable (`Square` onde `Circle` era
esperado) e via itab (idem, através de uma interface), as duas recusadas; um itab correto
(mesmo tipo) confirmado sem falso positivo; item 2 — `ref Circle`/`ref Square` e
`ref Animal`/`ref Dog` (derivado), as duas recusadas; item 4 — `namespace geo { i64 counter; }`
confirma a premissa errada do crumb (recusa de sempre, não passa); item 6 — `nested_check` rodado
contra o código pré-fix (`git stash`) reproduz a mensagem e a localização erradas relatadas, ao pé
da letra.

Sem PR, sem dreno — branch `feat/ngen-hygiene2`, forward-only para `fix/retirement`.

## 70. Errata do §64 — S4.2 landado parcial; o fork g1 resolvido, um bloqueio novo (2026-09-06)

Branch `feat/ngen-s42-bootstrap`, base `c7357b9b`, `mc` 0.15.8, host macOS/aarch64.

### (a) O fork g1 está RESOLVIDO — pelo `source_claim`, não pelo renome do núcleo

O §64(g1) pedia ao `mc` UMA de duas saídas; o `mc` entregou a **(2)**, a geral: `source_claim`
(0.15.8, PR #37). A teko registra `tk_source_claim` em `tk_fwd_init()` (`ngen/teko_fwd.tk`), antes
de qualquer `syntax`/`type_alias`, e reivindica **duas** classes de fonte:

1. **todo nome terminado em `.tk`** — um programa, uma fixture, `lib/rt.tk` e, na rota A, os 31
   módulos do próprio pacote `teko`;
2. **todo quadro que a própria teko empurra**, por `tk_push_source(name, text, len)` — o mesmo
   `p_push_source` entre `tk_claim_own = 1` e `= 0`. São quatro sítios: instância de genérico
   (`teko_generic.tk:402`), corpo de trait (`teko_trait.tk:262`), declaração materializada do §50 O3
   (`teko_class.tk:1133`) e o prelúdio de `+=`/`-=`/`++`/`--` (`teko_loop.tk:74`). O NOME desses
   quadros é uma frase (`Box__Circle__4 instantiated from f.tk:12`, `<teko-loop-prelude>`), não um
   arquivo — o sufixo `.tk` responderia 0 e o texto que a TEKO escreveu seria lido com o vocabulário
   do núcleo. A doc do `mc` avisa exatamente isso ("a source the module pushes itself is asked too").

Tudo o mais fica de fora: `<mc/host>`, `<mc/core_min>` e as partes, `<sys>`, `<prelude>`,
`core_teko.mc`, `user.mc`. Ali `type`/`out`/`params` voltam a ser nomes de parâmetro — a colisão de
23+43 sítios do §64(e) **deixou de existir**, medida: o self-compile passa de `mc/objmodel:293` para
muito além. O renome interno da S4.1 (`scope`→`dscope`, `out`→`dst`, `params`→`prs`) **continua
necessário**: os módulos são `.tk`, logo SÃO reivindicados, e ali as palavras valem.

### (b) `ngen/mc_teko.tk` — a unidade única da rota A

Cinco `#include` e nada mais: `<mc/host>`, `<mc/core_min>`, `core_teko.mc` (as outras quatro partes
+ `main()`), `teko.tk` (os 30 irmãos) e `user.mc` (`user_init`) — a ordem do glue que `mc build`
gera. Sem identificador próprio, então nada nele pode colidir com palavra ensinada, embora seja
`.tk` e portanto reivindicado. O `mc` aceita `#include` de `.mc` dentro de `.tk` sem ajuste
(auditado na S4.1, §68).

### (c) `ngen/scripts/bootstrap.sh` — o rito, e como o `.o` sai do fluxo `build`

`teko0 = mc build ngen --config <cfg0> --compiler-only` → `teko1/2/3 = <estágio anterior> build ngen
--config <cfgN> --entry-only` sobre `mc_teko.tk`. Critérios: `cmp build/teko2.o build/teko3.o`,
`--dump-asm` de teko2 vs teko3 com diff vazio, e as **45 fixtures compiladas por teko1**. POSIX
`sh`, sem `set -e`, tempo por etapa e tamanho de cada objeto/binário, mensagem clara por passo.

O objeto **não precisa do modo cru**: o config derivado MANTÉM o bloco `[linker]` — com linker o
`mc` escreve `<out>.o` e o entrega ao `cc`, e o objeto fica em disco (sem linker o backend embutido
escreve só o executável). Os derivados nascem do `ngen/mc.toml` pelo mesmo `sed` do HANDOFF §4,
moram AO LADO dele (o `entry` resolve contra o diretório do CONFIG) e somem no `trap EXIT`;
`ngen/mc.toml` não é tocado. O modo cru (`teko mc_teko.tk -o x.o`) dá objeto do mesmo tamanho, mas
seu `--dump-asm` sai em x86-64 mesmo com `[target] macos/aarch64` — serve de diagnóstico, não de
alvo.

### (d) Tetos de tabela: a escala mudou de fixture para unidade

Seis tabelas globais da teko estouraram, uma por vez, na ordem em que o self-compile as alcança —
cada uma com mensagem própria, nenhuma com corrupção silenciosa. Subidas contra contagem medida na
árvore (grep de declarações em `mc/src` + `ngen`), não a olho:

| teto | era | é | o que conta | medido |
|---|---|---|---|---|
| `TK_MAXFDECL` | 256 | 4096 | declarações livres com lista de parâmetros | ~2 300 |
| `TK_MAXSLV` | 512 | 8192 | locais da unidade inteira (K4's `use`) | ~3 800 |
| `TK_MAXODECL` | 1024 | 8192 | declarações da unidade (mangling de sobrecarga) | ~2 300 |
| `TK_MAXGARR` | 64 | 512 | arrays globais | ~180 |
| `TK_MAXGDEF` | 64 | 512 | escritas diferidas em array possivelmente global | — |
| `TK_MAXARR` | 256 | 1024 | arrays locais em escopo | — |

`--dump-ast` das 45 fixtures fica byte-idêntico (`same=45 diff=0`): teto de tabela não move árvore.

### (e) O fixpoint FECHA — medido, com o bloqueio de (f) removido experimentalmente

Probe fora do commit (as duas linhas `syntax_stmt("while"/"for")` comentadas, que é exatamente o que
o pedido de (f) devolveria): teko0 compila `mc_teko.tk` em **4,0 s** → `teko1.o` **1 708 248 B**,
binário **1 529 192 B**; teko1 → teko2 em **3,9 s**; teko2 → teko3 em **4,4 s**; **`cmp teko2.o
teko3.o` limpo**. E mais forte: **`teko1.o == teko2.o`** — o compilador já está no ponto fixo na
primeira volta, o que diz que a árvore que a teko produz para os fontes do núcleo não muda de
geração para geração. O risco §64(h).5 ("passes da teko sobre 8 500 linhas de núcleo") não se
materializou: o binário auto-hospedado roda (`teko1 --version` → `mc 0.15.8`, usage própria).

**Ressalva (verificador independente, 2026-09-06):** a sonda prova os critérios 1 e 2 da tabela (f)
(objeto e `--dump-asm`), NÃO o 3º ("teko1 compila as 45"): o teko1 da sonda dá 38/45, falhando as 7
fixtures que usam `while`/`for` -- consequência direta de comentar `syntax_stmt("while"/"for")`. O
3º critério só fecha com o patch do `mc` de (f).

**FECHOU (2026-09-06, mc 0.15.10 = `TE_RULE`, PR #40 do mc):** `ngen/scripts/bootstrap.sh` sem nenhum
contorno: teko0 1,8 s → teko1 3,8 s (`teko1.o` 1 712 808 B) → teko2 4,2 s → teko3 3,8 s; `cmp teko2.o
teko3.o` limpo; `--dump-asm` 220 651 linhas, diff vazio; teko1 compila as 45 fixtures (45/45); `FIXPOINT OK`
em ~40 s (macOS/aarch64). Os três critérios da tabela (f) do §64 fecham: **a teko está auto-hospedada sobre
o mc, rota A.** S4.3 (perna de CI) destravada.

### (f) O bloqueio que sobra é do `mc`, e é UM — pedido registrado

`word_add` marca a ENTRADA de token (`TE_TAUGHT`), e `tok_add` é idempotente por lexema: a entrada é
a MESMA que o `#rule stmt: while (...)` do `<prelude>` usa. Como `lex_word_id` esconde toda entrada
marcada em fonte não reivindicada, o `syntax_stmt("while", &tk_while)` da teko apaga o `while` dos
fontes do NÚCLEO — `mc/objmodel:212: expected ; after expression`, na linha
`while (i < 16 && ld8(s + i)) {` de `name16`. Vale para `while` e `for`, os dois únicos literais de
despacho em forma de identificador do prelúdio (`+=`/`-=`/`++`/`--` são pontuação, e pontuação não é
escopada por construção).

A release note do 0.15.8 declara o oposto ("Fora do escopo: `#rule`/`#infix`/`#prefix`/`#token` — o
`while`/`for` do prelude, que o núcleo usa"), e isso é verdade **enquanto o dialeto não ensinar o
mesmo lexema**. A teko ensina, e ensina porque precisa: `tk_while`/`tk_for` (`teko_loop.tk`) fazem
escopo/RC e reescrita de nível de `break N`/`continue N`, e o `for` da teko aceita passos que a
regra do prelúdio não tem (`i++`, `i += k`, chamada) — a regra não substitui o handler.

**Pedido ao mc:** numa fonte NÃO reivindicada, um lexema que também é literal de despacho de
`#rule`/`#token` tem de continuar PALAVRA e despachar para a REGRA — nunca para o handler do módulo.
Note que devolver o id no `lex_word_id` não basta: `parse_stmt` consulta `syntax_stmt_find` ANTES de
`rule_find` (`parse.mc:1256-1258`), então o handler do módulo pegaria o `while` do núcleo. A forma
sugerida é um segundo bit por entrada (setado pela estrada de diretiva) mais um guard nas buscas
`syntax_*_find` quando a fonte corrente não é reivindicada. Repro mínimo: um módulo com
`syntax_stmt("while", &h)` + `source_claim` de `.tk`, compilando qualquer `.mc` que use `while`.

**Contorno recusado, de propósito:** desmarcar a entrada (`tok_set_taught(id, 0)`, o que
`core_types_init` faz pelo `i32`) devolveria o `while` a toda fonte, mas o despacho iria ao handler
DA TEKO, que passaria a parsear o núcleo com semântica de teko — exatamente o que a transparência do
§64(h).5 proíbe. Sem gambiarra: o item é do `mc`.

### (g) Gate

`rm -rf ngen/build`, build do zero; `--entry-only` **45/45**; `--dump-ast` das 45 byte-idêntico
contra a base `c7357b9b` (`same=45 diff=0`); `mc limits ngen --config` `verdict ok`, com a linha
nova `source_claim 1/8` e `intrin 8/8` (zero intrínseco novo); nenhuma fixture nova, `ngen/tests/`
intocado; `ngen/mc.toml` intocado; `ngen/scripts/bootstrap.sh` chega ao stage 1 e para no bloqueio
de (f), imprimindo a mensagem do compilador. S4.3 (a perna de fixpoint no CI) fica esperando o
patch do `mc`.

Sem PR, sem dreno — forward-only para `fix/retirement`.

## 71. Higiene 3 — as duas dívidas que o verificador da higiene 2 achou (2026-09-06)

Crumb independente do desenho da entrega 4 (nenhuma superfície nova). Base: o tip de
`fix/retirement` em `0fd12888`; dois commits, um por item — `4052205f` (item A), `6ef10a24`
(item B).

### (a) Item A — `ref`/`out` de ESCALAR agora é checado

`tk_ref_check_pointee` (teko_ref.tk, higiene 2 item 2) saía cedo quando `tk_struct_by_ty(pty)`
não respondia, ou seja para TODO apontado escalar: `void bumpi(ref i64 x)` chamado com um `u8`
compilava e escrevia oito bytes através de um slot de um. C# recusa `ref` de tipos distintos
sejam eles escalares ou não, e a checagem passa a valer para todo tipo — **identidade** do
apontado decide (nunca `tk_row_fits`, que é deriva/implementa: `ref` não é covariante).

O que a extensão exigiu foi trocar a FONTE do apontado do argumento. A tag de parse
(`tk_rfarg_pointee`) cai em `tk_slv_find`, tabela unit-wide "o mais recente vence" que **nunca vê
um PARÂMETRO** — e o repass `lvl_c(ref x)` dentro de `lvl_b(ref i64 x)`
(`surface_refout.tk`'s `chaincheck`) responderia com o `Box x` que `outcheck` declara páginas
antes, recusando uma fixture correta. Com classe-contra-classe isso nunca aparecia (o `pty`
escalar saía antes do lookup); com escalar, aparece na primeira volta. `tk_ref_arg_pointee`
(teko_ref.tk) pergunta à DECLARAÇÃO que a pass está percorrendo (`tk_ref_cur_fn`, já existente):
a lista de parâmetros primeiro (`tk_ref_param_ty`), depois o `N_VAR` que o corpo declara
(`tk_ref_scan_local`, que pula array pelo mesmo `nd_val != 0` do `on_stmt`). Um nome que não é
nem um nem outro (um global) responde -1; dois blocos irmãos declarando o mesmo nome sob tipos
diferentes também (ambiguidade não se adivinha) — e -1 é silêncio, a mesma regra que
`tk_check_field_store` dá a uma fonte desconhecida.

Limite documentado: os apelidos de um mesmo primitivo (`bool`/`byte`/`u8`, `char`/`u32`,
`str`/`ptr`/`uptr`) são o MESMO apontado aqui, porque `type_alias` é identidade pura (entrega 2)
— como em todo o resto do port.

Fixture: `surface_refout.tk` ganha `scopecheck` (um local de bloco interno e um do corpo, os
dois `ref i64`); o mismatch é probe.

### (b) Item B — as capturas de uma lambda não custam mais um parâmetro cada

`tk_lambda_alloc_params`/`tk_lambda_alloc_stmts` (teko_deleg.tk, K4) geravam o alocador do
closure com UM parâmetro real por captura, então o `MAXPARAMS` do ABI do mc (12) ficava NA
FRENTE do `TK_MAXLAMCAP` (32, higiene 2 item 6): treze capturas morriam na mensagem do NÚCLEO
(`io:25: at most 12 parameters`, com nome de arquivo que o programa nunca escreveu) e só a 33ª
alcançava a recusa própria da teko.

Desenho novo: **o alocador recebe o OBJETO e nada mais** (`uptr p`), e o bloco é alocado no
SÍTIO DE CRIAÇÃO já carregando as capturas — `tk_lambda_capture_chain` encadeia um
`tk_cap_put`/`tk_cap_own` (lib/rt.tk, duas funções novas de runtime) por captura sobre
`rt_alloc(objsize)`, cada elo devolvendo o bloco ao seguinte. É a mesma forma que a lista de um
`params` já usa (`tk_va_put`) e pela mesma razão: um store é instrução e o closure inteiro tem de
ser UMA expressão. `tk_cap_own` toma a referência própria do closure para uma captura de tipo
contado, exatamente onde o `rc_inc` do alocador tomava.

O que NÃO mudou, de propósito: o layout do objeto (`{vt, rc@+8, code@+16, captures@+24…}`), o
prólogo (`tk_lambda_prologue` segue lendo `ld*(env + 24 + 8*i)`), a função de release, e — o
ponto que preserva a maquinaria em volta — **o nó mais externo do inicializador continua sendo a
CHAMADA ao alocador**, com a cadeia como seu único argumento. Por isso `tk_lam_escapes`
(D221 decisão 21, que lê `nd_name` de um `N_CALL`), a coerção de delegate e a tag
`tk_xt_add(call, si, 0)` seguem lendo o que sempre leram, sem uma linha de mudança. A captura por
referência continua passando `&nome`; a por valor, o valor congelado no instante da criação
(D221 decisão 20).

Slot de captura é uma PALAVRA, qualquer que seja a largura do tipo capturado: `tk_cap_put` grava
com `st64` e o prólogo lê de volta com `tk_ldn` (`ld8`/`ld16`/`ld32`/`ld64`), que é onde a captura
mais estreita volta ao tamanho — o mesmo valor que o par `st8`/`ld8` anterior produzia. Medido
também para `f64` (probe): o mc move a palavra sem converter, então uma captura de ponto
flutuante atravessa o parâmetro `i64` de `tk_cap_put` bit a bit.

Teto real depois disso: `TK_MAXLAMCAP` sozinho — 32 linhas somadas sobre as lambdas EM VOO
(a tabela é uma pilha desde a higiene 2), com a mensagem própria da teko
(`too many captures in one lambda`) e arquivo/linha certos. Medido: 14, 20, 31 e 32 capturas
compilam e rodam; 33 é recusada.

Fixture: `surface_lambda.tk` ganha `manycap_check`, quinze capturas num só `use (...)`
(catorze por valor, uma por referência).

### (c) Gate

`rm -rf ngen/build`, build do zero (host macOS/aarch64, `mc` 0.15.8); `--entry-only` **45/45**
(nenhuma fixture nova; duas tocadas: `surface_refout.tk`, `surface_lambda.tk`);
`mc limits ngen --config` `verdict ok`, `intrin 8/8` (zero intrínseco novo), `ngen/mc.toml`
intocado.

`--dump-ast` das 45 contra o compilador da base `0fd12888`: **3 byte-idênticas** (`hello`,
`primitives_ptr`, `primitives_scalar` — as três que não incluem `lib/rt.tk`), **40 com um diff
byte-idêntico entre si** (hash igual, 29 linhas: exatamente as duas declarações novas
`tk_cap_put`/`tk_cap_own` que `lib/rt.tk` passou a exportar, inseridas no mesmo ponto), e **2 com
diff próprio** — as duas fixtures tocadas. Ou seja: fora das duas tocadas, nenhuma árvore muda de
FORMA; o que entra é a superfície nova de runtime, que toda unidade que inclui `lib/rt.tk`
enxerga por construção. (O critério "43 byte-idênticas" do crumb só valeria se os dois helpers
não morassem em `lib/rt.tk`; a alternativa era reaproveitar o `tk_va_put` do `params` para
closure, que economiza o diff mas acopla duas features por um nome que mente. Preferiu-se a
superfície própria e a prova mecânica do diff.)

Probes (em `ngen/_probe/`, fora de `ngen/tests/`, descartados): item A — `ref i64` recebendo um
`ref u8` recusado (`teko: a value of type u8 does not convert to i64`), `ref i64` com `ref i64`
(direto, repassado e de bloco interno) aceito, mismatch de classe (`Square` para `ref Circle`) e
derivado-para-base (`Dog` para `ref Animal`) recusados como antes; item B — 14/20/31/32 capturas
compilam e rodam, 33 recusada com a mensagem da teko, lambda aninhada em três níveis com captura
nos três níveis e RC fechando em zero, captura de `f64` preservada.

### (d) Dívida ADJACENTE achada (não é deste crumb)

**`ref T`/`out T` de um escalar mais ESTREITO que uma palavra é quebrado em runtime, desde o K2.**
Um parâmetro `ref T` é declarado com o tipo do APONTADO (desenho do K2: é o que faz o oráculo e o
overload verem `T` sem mudança nenhuma), mas o que ele carrega é um ENDEREÇO — e o mc trunca um
ponteiro passado a um parâmetro de largura 1/2/4. Medido nos dois níveis: `void bumpb(ref u8 x)`
com `u8 b` segfalta (139), e o mesmo em dialeto mc puro (`void poke(u8 x) { st8(x, 5); }` chamado
com `&b`) segfalta igual — não é a teko errando o lowering, é a consequência direta do tipo
declarado do parâmetro. `ref i64`/`ref uptr`/`ref` de classe (largura 8) não são afetados, e é o
que todas as fixtures usam. Corrigir exige o parâmetro nascer com largura de ponteiro e todo
consumidor de `nd_type(p)` (mangling `tk_ty_sfx`, `tk_arr_load`/`tk_arr_store` do deref,
`tk_is_counted` do prólogo de `out`, `tk_rc_assign`, o oráculo `tk_ty_scope_params`) passar a ler
o apontado da tabela lateral (`tk_rp_pointee`) — redesenho do K2, não higiene. Registrado aqui e
no HANDOFF §5; a checagem do item A NÃO o mascara (ela recusa a MISTURA de larguras, não o uso
correto de um `ref u8`).


## 72. K2w — `ref`/`out` nasce com largura de PONTEIRO (2026-09-06)

Fecha a dívida do §71(d). Base: o tip de `fix/retirement` em `cc539258`; dois commits —
`cf95af7d` (o parâmetro declarado) e `3a684eda` (o thunk de delegate).

### (a) O defeito, e por que era do desenho e não do mc

Um parâmetro `ref T`/`out T` era DECLARADO com o tipo do apontado (K2, D221, §41 decisões 10/12:
era o que dava oráculo, mangling e overload de graça), mas o que ele carrega é um ENDEREÇO. O mc
gera o store/load do slot de um parâmetro com `type_width(nd_type(param))`
(`machine_arm64.mc:190`/`:336`, `machine_x86_64.mc:227`/`:322`), então para todo `T` de largura
1/2/4 (`u8`/`i8`/`u16`/`i16`/`u32`/`i32`/`bool`) o ponteiro era TRUNCADO na entrada da função:
`void bumpb(ref u8 x) { x = x + 1; }` chamado com um `u8` faltava (SIGSEGV, 139). O mesmo em
dialeto mc puro (`void poke(u8 x) { st8(x, 5); }` com `&b`) falta igual — o mc está certo, o tipo
declarado do parâmetro é que estava errado. Largura 8 (`ref i64`, `ref uptr`, `ref` de classe) não
era afetada, e é o que todas as fixtures usavam.

### (b) A correção: um par de acessores, e o resto é mecânico

O slot passa a ser `TY_UPTR` nos DOIS sítios que constroem um (`tk_default_param` para função
livre/lambda, `tk_params` para método), e o apontado continua exatamente onde a K2 já o punha: a
tabela lateral chaveada pelo NÓ do parâmetro (`tk_rp_add`/`tk_rp_pointee`). O que muda é a FONTE
que todo consumidor lê:

- **`tk_param_ty(p)`** (teko_ref.tk) — o tipo que um nó de parâmetro REPRESENTA: o apontado quando
  há linha na tabela, o próprio `nd_type` quando não há.
- **`tk_decl_param_ty(d, i)`** — a mesma pergunta por ÍNDICE, para os sítios que chamavam
  `decl_param_type` (a API do núcleo, que responde o `uptr` DECLARADO).

Censo dos sítios convertidos (todos os que liam o tipo de um PARÂMETRO; depois da conversão, o
grep de `nd_type(p…)`/`decl_param_type(` sobre a árvore inteira do `ngen/` responde só quatro
linhas, todas legítimas: as duas comparações de `teko_params.tk` com `tk_ty_params` — que um
`ref`/`out` nunca é — e os dois acessores novos, que são quem lê o nó cru):

| arquivo | sítio | o que lia |
| --- | --- | --- |
| teko_ref.tk | `tk_ty_sfx` | mangling `ref_i64`/`out_Circle` (símbolo INALTERADO) |
| teko_ref.tk | `tk_ref_rewrite_assign` | a largura de `stW(x, e)` |
| teko_ref.tk | `tk_ref_walk` (N_IDENT) | a largura de `ldW(x)` |
| teko_ref.tk | `tk_ref_walk` (N_ASSIGN) | `tk_is_counted` do apontado |
| teko_ref.tk | `tk_ref_out_prologue` | `tk_is_counted` do `out` zerado |
| teko_ref.tk | `tk_ref_param_ty` | o apontado de um argumento REPASSADO (higiene 3) |
| teko_ref.tk | `tk_ref_check_pointee` | o apontado do PARÂMETRO (apontado × apontado) |
| teko_typeof.tk | `tk_ty_scope_params` | o oráculo (e, por ele, `tk_rc_assign`) |
| teko_over.tk | `tk_ov_args_fit` | o tipo do parâmetro contra o do argumento |
| teko_over.tk | `tk_ov_refout_pair_clash` | `f(ref i64)` × `f(out i64)` |
| teko_rc.tk | `tk_rc_call_args` | checagem de argumento por índice |
| teko_expr.tk | `tk_vcall_args_check` | idem, chamada virtual |
| teko_expr.tk | `tk_field_use` (slot de delegate) | o parâmetro é de tipo delegate? |
| teko_deleg.tk | `tk_deleg_set_sig`/`check_sig`/`check_call_args`/`tk_lambda_check_params` | a assinatura do delegate |
| teko_iface.tk | `tk_ifargs_check` | idem, chamada por itab |
| teko_ops.tk | `tk_op_owns_operand`/`tk_op_declare` | os operandos de um operador |
| teko_di.tk | `tk_di_ctor_satisfiable`/`tk_di_ctor_args` | a chave de serviço de um parâmetro |

Como o mangling lê o APONTADO, nenhum símbolo muda: `bump__ref_i64` continua `bump__ref_i64`, e o
`--dump-syms` das 45 fixtures é byte-idêntico ao da base.

### (c) O segundo commit: o thunk de delegate era um parâmetro gerado com o mesmo defeito

`tk_deleg_thunk_fn` (K1) declarava os parâmetros do forwarder com `dg_pty_at` — o APONTADO —, e o
thunk é gerado DURANTE o pass de delegate, depois de o `tk_ref_pass` já ter passado, então nada o
corrigia. `delegate void Bump(ref u8 x)` preenchido com uma função nua truncava o endereço na
entrada do thunk (`ref i64` não era afetado; a LAMBDA nunca foi, porque os parâmetros dela são
lidos por `parse_params` e portanto já nascem no slot certo).

A assinatura do delegate passa a registrar o KIND de cada parâmetro (`dg_pk`, ao lado de
`dg_pty`), e `dg_pslot_at` é o que o thunk declara: `uptr` para um slot `ref`/`out`, o apontado nos
demais. Com o kind na assinatura, três checagens que não podiam existir passam a existir:
`tk_deleg_check_sig` (uma função por valor não preenche mais um slot `ref`, nem o inverso),
`tk_lambda_check_params`, e `tk_deleg_check_arg_kinds` no SÍTIO DE CHAMADA — que dá as mesmas duas
mensagens que `tk_ref_check_call` já dá a uma chamada direta. `tk_deleg_sig_str` soletra o kind, e
uma incompatibilidade lê `Bump(ref u8)`.

### (d) Gate

Host macOS/aarch64, `mc` 0.15.8. `rm -rf ngen/build`, build do zero; laço `--entry-only`
**45/45**; **nenhuma fixture nova** (uma tocada, `surface_refout.tk`); `ngen/mc.toml` intocado;
`mc limits ngen --config` `verdict ok`, `intrin 8/16` e `passes 15/30` — os MESMOS da base (zero
intrínseco novo, zero pass nova); `lib/rt.tk` intocado.

`--dump-ast` das 45 contra o compilador da base `cc539258`: **44 byte-idênticas** (nenhuma outra
fixture declara `ref`/`out`) e **1 com diff próprio** — `surface_refout.tk`, cujo diff é
exatamente 14 linhas `PARAM type=<apontado>` viradas `PARAM type=uptr` mais o `narrowcheck` novo.
`--dump-syms` das 45, base contra tip, sobre as fixtures ATUAIS: **byte-idêntico nas 45**.

Fixture: `surface_refout.tk` ganha `narrowcheck` (`expect-exit: 42` mantido) — `ref u8` escrito e
lido pelo apontado, o wrap em 255 provando que o store é de UM byte e não de oito, um `ref u8`
repassado um nível abaixo, e um `i32` negativo escrito por um `out`.

Probes (em `ngen/_probe/`, fora de `ngen/tests/`, descartados), cada um rodado TAMBÉM contra o
compilador da base para separar correção de regressão: `ref u8`/`ref i32`/`out u16`/`ref bool` +
repasse (base 139, tip 42); `ref` de campo `u8` e de elemento de array `u8` (base 139, tip 42);
sobrecarga `g(u8)` × `g(ref u8)` e método `twice(ref u8)` (base 139, tip 42); `f(ref u8)` ao lado
de `f(out i64)` — apontados distintos, aceitos (base 139, tip 42; com o `nd_type` cru os dois
slots seriam `uptr` e o par seria recusado por engano); interface + `override` virtual com
`ref u8` e `ref` de classe, `rt_live()==0` no fim (base 139, tip 42); genérico
`Holder<i64,2>` com método `ref u8` (tip 42); `out C`/`ref C` de classe com destrutor e
`rt_live()==0` (base 42, tip 42 — largura 8 não regride); delegate com `ref u8` por função nua
(base 139, tip 42) e por lambda (tip 42); `ref i64` por delegate (base 42, tip 42). Recusas, todas
mantidas: `ref u8` recebendo `ref u16` e o mesmo por repasse (`a value of type u16 does not
convert to u8`), argumento sem `ref` (`argument 1 needs \`ref\` at the call site`), `ref` num
parâmetro por valor, `out` nunca atribuído, `f(ref u8)` + `f(out u8)`, `ref x;` como local,
função por valor num slot `ref` de delegate, lambda por valor idem, e chamada de delegate sem
`ref`.

### (e) Dívida ADJACENTE achada (não é deste crumb)

**`ref f64` compila e devolve o valor errado, no tip E na base** (`bumpf(ref f64 v)` sobre `1.5`
não vira `2.5`): a largura é 8, então NÃO é o defeito do K2w, e o comportamento é idêntico antes e
depois. A causa é outra — o deref usa `ld64`/`st64` (`tk_arr_load`/`tk_arr_store`, inteiros), e a
aritmética do corpo é de ponto flutuante; um `ref` de f64 precisa do par de load/store de FLOAT.
Registrado aqui e no HANDOFF §5; não foi tocado.
**Ampliação (verificador do K2w):** a causa é `tk_ldn`/`tk_stn` (`teko_struct.tk`) mapearem só pela largura;
atinge todo acesso indireto `f64` por esse par (`ref f64` e campo de classe `f64`), não o array fixo local. O
mc já expõe `ldf64`/`stf64`/`ldf32`/`stf32` (`lib/float.mc`). Conserto mecânico = higiene 4.

## 73. S4.3 — a perna de fixpoint no CI (2026-09-06)

Fecha a linha S4.3 da tabela §64(f) ("perna de fixpoint no CI | `.github/workflows/ngen.yml` |
a 6ª perna verde"). Base: o tip de `fix/retirement` em `e50ab97b`; branch `feat/ngen-s43-ci`,
dois commits — `5db4abc0` (o job + a action composta) e `57a5ade8` (o `[target]` de linux).

### (a) O job

`fixpoint`, FORA da matriz `leg`, matriz própria de dois runners:
**`fixpoint (linux/x86_64)`** (`ubuntu-latest`) e **`fixpoint (macos/aarch64)`**
(`macos-latest`). Cada um roda `sh ngen/scripts/bootstrap.sh --os <os> --arch <arch>` a partir
da raiz do repositório, e o script é quem prova os TRÊS critérios do §64(f): `cmp` dos OBJETOS
`teko2.o`/`teko3.o`, `--dump-asm` de teko2 vs teko3 com diff vazio, e teko1 compilando e
RODANDO as 45 fixtures. Os tempos por estágio e os tamanhos de todo objeto/binário já saem no
log do próprio script; um passo de `summary` publica tamanho + `sha256` de `teko1.o`/`teko2.o`.

Windows fica de fora desta fatia: a escada precisa do `<out>.o` em disco, o que exige
`[linker]`, o que no Windows é o `lld-link` mais o sysroot de três arquivos que a perna monta
para si — trabalho real, registrado como dívida (§(e)).

### (b) Um só caminho para obter o `mc`: `.github/actions/setup-mc`

Os dois passos que resolviam o `latest` de `minicompiler/mc`, baixavam o asset, conferiam o
`.sha256` e asseriam `mc --host` saíram do job `leg` para uma **action composta**
(`.github/actions/setup-mc/action.yml`), usada pelas cinco pernas E pelo job novo. Entradas:
`os`/`arch`/`asset`/`exe`/`token`; saídas: `mc` (caminho relativo — a forma que Git Bash, bash
e o próprio `mc` leem igual), `dir` (absoluto, para o `$GITHUB_PATH` do fixpoint, porque
`bootstrap.sh` chama `mc` pelo NOME), `tag` e `version`. Não há mais como dois jobs testarem
compiladores diferentes, e `latest` é resolvido por um código só.

### (c) O achado do CI: o `[target]` de linux, e por que a perna não sofria

Primeiro run: `fixpoint (linux/x86_64)` morreu no estágio 1 com
`ngen/build/teko: not found`, exit **127** — com o `ngen/build/teko` presente e com 1 525 584
bytes. Não é o arquivo que falta, é o **interpretador** nomeado dentro dele: o compilador
ensinado é escrito pelo backend de executável do **HOST** (mc `docs/build.md` § `[compiler]`),
cujo writer ELF põe `PT_INTERP`/soname **musl** por padrão, e o runner é glibc. As cinco pernas
não sofriam porque já carregam `interp`/`libc` na própria matriz; a escada, que deriva o config
do `ngen/mc.toml`, não carregava nada disso.

Correção no `bootstrap.sh` (POSIX `sh`, sem `set -e`, como o resto do arquivo): `write_target_tail`
escreve `interp = "<loader>"` + `libc   = "gnu"` num arquivo temporário quando **o alvo é linux E
aquele loader EXISTE na máquina** (`/lib64/ld-linux-x86-64.so.2`, `/lib/ld-linux-aarch64.so.1`), e
`derive` insere essas linhas logo depois do `arch = ` de `[target]` (`sed -e "/^arch = /r ..."`, a
mesma técnica do workflow). Máquina musl não anexa nada e as defaults do mc valem. O critério é o
certo porque a escada **executa** todo estágio que constrói: quem manda é o loader desta máquina,
não uma tabela por par. macOS não muda (nada é anexado); o `[linker] cc` do `ngen/mc.toml` segue
intocado, e é ele que mantém o `.o` em disco para o `cmp`.

### (d) O que o CI mediu (run 34043945146, mc 0.15.10)

| | linux/x86_64 | macos/aarch64 |
|---|---|---|
| teko0 (`mc build --compiler-only`) | 1,924 s | 3,032 s |
| teko0 → teko1 | 4,085 s | 6,927 s |
| teko1 → teko2 | 4,089 s | 4,963 s |
| teko2 → teko3 | 4,084 s | 5,232 s |
| `--dump-asm` | 221 221 linhas, diff vazio | 221 134 linhas, diff vazio |
| fixtures por teko1 | 45/45 | 45/45 |
| total do script | 26,887 s | 45,267 s |
| `teko1.o` = `teko2.o` | 2 062 312 B | 1 714 920 B |
| `sha256(teko2.o)` | `33e7df95…` | `689dc9a6…` |

`teko1.o == teko2.o` nos dois runs do CI -- **mas NÃO é estável (verificador, 2026-09-06):** em duas
escadas locais iguais (mesmo commit, máquina e mc 0.15.10, `ngen/build` limpo) a 1ª deu `teko1.o` =
`90485ed5…` ≠ `teko2.o` = `teko3.o` = `689dc9a6…`, a 2ª deu os três iguais. O ponto fixo
(`teko2.o == teko3.o`, hash final `689dc9a6…`) fechou em 100% das corridas; o que oscila é a SAÍDA DO
teko0 (mc estoque + módulos teko compilando `mc_teko.tk`) -- não-determinismo a caçar (higiene 4,
item B) ANTES de qualquer golden pinado. Quando é igual, o compilador está no ponto fixo na primeira volta, o mesmo que
o §70 mediu no host. O objeto de macos/aarch64 do CI é **byte-idêntico ao construído localmente**
(mesmo `689dc9a6…`), o que é a primeira evidência de reprodutibilidade entre máquinas — e os dois
runs consecutivos da branch (`34043945146` e `34044112521`, o segundo só com mudança de doc)
deram **o mesmo `sha256` nos dois pares**, que é a evidência entre runs. São esses os números que
um golden versionado pinaria.

### (e) O que NÃO é gate, e as dívidas

1. **`sha256` é reportado, não barrado.** O `cmp` prova o ponto fixo DENTRO do run; que
   `teko2.o` seja byte-idêntico ENTRE runs e máquinas é outra afirmação. Quando o número
   estabilizar, vira golden versionado, no molde do `tests/golden/mc2.sha256` do mc — e aí o
   job compara em vez de imprimir.
2. **O agregador não mudou.** `mc build ngen && run` continua dependendo só da matriz `leg`:
   é o nome que o ruleset de `main` exige e o significado dele fica sendo o que diz. Promover
   a escada a check obrigatório é decisão de ruleset, tomada fora deste workflow.
3. **Windows sem escada.** Precisa do sysroot (`winstart.obj`/`mcrt.obj`/`kernel32.lib`) e da
   linha `lld-link` dentro do job de fixpoint, mais um `--linker`/tail equivalente no
   `bootstrap.sh`. Nada disso é conceitualmente novo — é a mesma montagem que a perna Windows
   já faz — mas é obra, não configuração.
4. **linux/aarch64 sem escada.** O par tem perna (`ubuntu-24.04-arm`) e o `bootstrap.sh` já
   conhece o loader dele; ficou de fora só para o job novo custar dois runners, não quatro.

## 74. Higiene 4 — acesso indireto a float, e o não-determinismo do `teko1.o` (2026-09-06)

Dois itens sem relação entre si, um commit cada. O (a) fecha a dívida que o verificador do K2w
ampliou (`ref f64` devolve valor errado); o (b) caça o não-determinismo que o verificador do S4.3
registrou (`teko1.o` diferente entre corridas), pré-requisito de qualquer golden pinado.

### (a) Item A — o par de load/store escolhe pela LARGURA, e float mora no outro banco

`tk_ldn`/`tk_stn` (`ngen/teko_struct.tk`) mapeavam só pela largura -- 1/2/4/8 -> `ld8..ld64`/
`st8..st64`. Um `f32`/`f64` tem largura 4/8 como um inteiro qualquer, mas vive no OUTRO banco de
registradores (`v0..v7`/`x0..x7` sob AAPCS64; `xmm`/`gpr` no x86_64): mover os oito bytes certos
para o lugar errado é o mesmo que não movê-los. Todo acesso INDIRETO a float passava por esse par
-- apontado de `ref`/`out`, campo de classe/struct, elemento de array (local, global de classe,
heap `new f64[n]`), backing de propriedade, captura de closure. `<float>` (M24) já registra
`ldf32`/`ldf64`/`stf32`/`stf64` (`lib/float.mc:424-427`) e o `ngen` nunca os havia usado.

**Correção:** as duas funções perguntam `type_kind(ty) == TK_FLOAT` ANTES da largura e devolvem o
acessor do banco certo (4 -> `ldf32`/`stf32`, 8 -> `ldf64`/`stf64`); nenhuma outra largura de float
existe na superfície, então o resto cai no caminho inteiro de sempre. Isso conserta, de uma vez, os
sete sítios que chamam o par (`teko_access`/`teko_array`/`teko_deleg`/`teko_expr`/`teko_heaparr`/
`teko_prop`/`teko_struct`/`teko_this`/`teko_typeof`).

Três consertos que a mesma mudança exigiu, todos achados por probe:

1. **`tk_ops_is_mem` (`ngen/teko_ops.tk`)** lista os intrínsecos de memória cujo 1º argumento é um
   ENDEREÇO -- é o que impede a espinha `obj + OFF` de ser lida como operando de um `operator+` do
   tipo do objeto. Os quatro acessores de `<float>` entraram na lista pelo mesmo motivo.
2. **A escrita da CAPTURA de closure** (`tk_cap_put`, `lib/rt.tk`) recebe o valor num parâmetro
   `i64`: um argumento float viaja no banco de float e um parâmetro inteiro NUNCA o recebe -- a
   palavra gravada era lixo. Antes da higiene 4 isso não aparecia porque a leitura (`ld64`) também
   estava errada e o valor lido vinha, por acidente, do registrador de float que a lambda tinha
   deixado para trás. Agora o escritor é escolhido pelo kind (`tk_cap_writer`, `teko_deleg.tk`):
   `tk_cap_putf`/`tk_cap_putf32` (novos em `lib/rt.tk`, `f64`/`f32` DECLARADOS) gravam com
   `stf64`/`stf32`, o par exato que o prólogo lê.
3. **O cast de retorno estreito de delegate** (`tk_deleg_build`) era `type_width(ret) < 8` -- outra
   decisão por largura pura. Sobre um `f32` ele emitia `(f32) callp(...)`, isto é, uma CONVERSÃO
   numérica do resultado INTEIRO da chamada. A regra do próprio núcleo (`walk_narrow`,
   mc `src/gen_walk.mc`) conta só `TK_INT`/`TK_SINT` mais estreito que uma palavra -- float é do
   módulo. `tk_deleg_ret_narrow` passa a espelhá-la.

**Probes (fora de `ngen/tests/`), base -> tip:** `ref f64` (`1.5` intacto -> `2.5`); `out f64`
(0 -> 26); campo `f64` de classe (0 -> 25); array local `f64` (0 -> 25); `f32` por `ref`/`out`/campo/
array (1 -> 42); struct, propriedade auto, campo herdado, `new f64[3]`, `foreach` sobre `f64[]`,
campo `static f64`, campo array inline `f64[3]`, ternário de braços `f64` (2 -> 42); captura `f64` e
`f32` em lambda com o valor SOBRESCRITO depois da criação (1 -> 42), que é a prova de que a captura
é cópia congelada e não leitura do registrador que sobrou.

**Fixture:** `surface_refout.tk` ganha `floatcheck` (`ref f64`, `out f64`, campo `f64` lido+escrito,
`ref f64` como parâmetro de MÉTODO), `expect-exit: 42` inalterado. Contra o compilador da base a
mesma fixture sai **131** (`130 + 1`, o `ref f64`); contra o tip, 42.

### (b) O que o item A NÃO conserta — dois pedidos ao mc, sem contorno

> **FECHADO (2026-09-06, §79):** os dois pedidos saíram no **mc 0.15.13** e o V1 consumiu o
> contrato nos cinco construtores de `callp` da teko. O texto abaixo fica como o registro do
> defeito; a errata está no §79(a). A dívida adjacente do fim desta seção (`params` sem tipo de
> elemento) continua ABERTA.

1. **`callp` é tipado `TY_I64` pelo núcleo** (`src/gen_resolve.mc:446`), então `walk_ret_type()` numa
   chamada INDIRETA sempre responde inteiro e o `fa_result` de `<float>`
   (`lib/machine_arm64_float.mc:492` e o gêmeo x86_64) nunca move `d0`/`xmm0` para o registrador de
   destino. Toda chamada indireta que devolve float (delegate, método virtual, método de interface --
   as três usam `callp`) entrega o valor SÓ por coincidência, quando o registrador de destino é o
   mesmo que o callee deixou escrito. Repro mínimo, sem nada do `ngen`:

   ```
   f64 dbl(f64 x) { return x * 2.0; }
   i64 main() {
       uptr p = &dbl;
       f64 direct = callp(p, 2.0);        // 4.0 -- por coincidência (profundidade 0)
       f64 nested = 1.0 + callp(p, 2.0);  // 3.0 em vez de 5.0
       return (i64) nested;
   }
   ```

   **Pedido:** uma forma de a chamada indireta declarar o tipo de retorno -- `callp` honrando o tipo
   que o módulo põe no nó, ou uma grafia tipada. Sem isso não há conserto do lado da teko: o valor
   está em `d0` e nenhuma expressão da superfície o alcança.
2. **`fa_w` (`lib/machine_arm64_float.mc:193`) não mapeia as CONVERSÕES para a variante single.**
   `FI_SCVTF_S`/`FI_UCVTF_S`/`FI_FCVTZS_S`/`FI_FCVTZU_S` (132/133/136/137) existem e nunca são
   escolhidos, então em arm64 `(i64) <expr f32>` emite `fcvtzs x, d` (lê o valor como double) e
   `(f32) <expr i64>` emite `scvtf d, x`. Repro: `i64 main() { f32 y = 2.5f; return (i64) (y * 10.0f); }`
   -- 0 em arm64; o machine x86_64 de `<float>` está correto (`cvttss2si`). O `ngen` não contorna:
   as fixtures/probes de `f32` comparam contra literais `f32` em vez de castar.

**Dívida adjacente, do lado da teko, NÃO fechada aqui (precisa de decisão de superfície):** uma lista
`params` não tem tipo de ELEMENTO -- é uma lista de PALAVRAS (`tk_va_put(uptr, i64, i64)` grava e
`tk_va_at` devolve `i64`, `lib/rt.tk`), e o HANDOFF registra que `params i64[] xs` foi deliberadamente
recusado. Logo um argumento float numa `params` cai na MESMA armadilha da captura (um `f64` não entra
num parâmetro `i64`): `f64 total(params xs)` com `total(1.5, 2.5)` responde errado, no tip E na base
(medido, probe fora de `ngen/tests/`) -- não é regressão da higiene 4 e não é o par de load/store. O
conserto exige decidir COMO uma `params` ganha tipo de elemento (a grafia do C# é `params T[]`), que é
superfície, não higiene.

### (c) Item B — o `teko1.o` não determinístico: NÃO reproduz, e a instrumentação que o próximo divergente já traz pronto

O verificador do S4.3 registrou (mesma máquina, mesmo commit, `ngen/build` limpo, mc 0.15.10) duas
escadas em que a primeira deu `teko1.o` = `90485ed5…` ≠ `teko2.o` = `teko3.o` = `689dc9a6…` e a
segunda deu os três iguais. O item B foi caçar isso.

**Não reproduziu, em 44 corridas.** Todas em macOS/aarch64, mc 0.15.10, `ngen/build` apagado antes
de cada uma:

| experimento | corridas | resultado |
|---|---:|---|
| escada completa, árvore do tip | 10 | `teko1.o == teko2.o == teko3.o` = `3bf52b96…` em todas |
| escada completa, árvore da BASE `55ec9ffe` (exportada em `/tmp`) | 4 | os três = **`689dc9a6…`**, o hash documentado, em todas |
| estágio 0+1 só, árvore do tip | 6 | `teko0` = `30d9176b…`, `teko1.o` = `3bf52b96…` em todas |
| estágio 1 repetido com o MESMO teko0 (sem reconstruir) | 20 | `teko1.o` = `3bf52b96…` em todas |
| estágio 0+1 com ambiente perturbado (env +4 KB, env +60 KB, `TMPDIR` /tmp e /private/tmp, sob carga de I/O) | 6 | idem |
| estágio 0+1 em DUAS cópias da árvore, em paralelo, fora do repositório | 2 | idem, e igual ao serial |
| estágio 1 com o config em caminho ABSOLUTO | 1 | idem |

O `--dump-asm` do teko0 sobre `mc_teko.tk` também é byte-idêntico entre corridas
(`e2d9df9f…`, 6 medições). A árvore da base reproduzir o `689dc9a6…` publicado é o controle: a
medição do verificador é comparável e o número documentado é o que esta máquina produz.

**O que a investigação DESCARTOU, com evidência:**

1. **Versão do mc.** Só a 0.15.10 chega a produzir um `teko1.o` neste commit -- medido: com a 0.15.8
   o `teko0` é construído (`709e35c6…`) e o estágio 1 morre no bloqueio `TE_RULE` do §70(f), sem
   escrever objeto nenhum. A 0.15.9 é a 0.15.8 mais o registro padrão, logo idem.
2. **Endereço/ASLR.** A `heap[HEAP_SIZE]` da arena do mc (`src/arena.mc:208`) é BSS e o binário é
   PIE: o endereço-base muda a cada corrida. Se algum ponteiro vazasse para a saída (violação da
   regra 1 de `docs/determinism.md`), as 44 corridas teriam divergido. Os chunks de crescimento
   vêm de `mmap(0, …)` -- endereço escolhido pelo kernel, mesma conclusão.
3. **Slot de tabela lido além de `n`.** As tabelas do `ngen` são globais (BSS) e os chunks de
   arena vêm de `mmap` anônimo: as duas fontes são ZERADAS pelo carregador/kernel, então uma
   leitura fora de `n` responde 0 -- errado, se for o caso, mas **determinístico**, nunca um byte
   que varia entre corridas.
4. **Caminho/diretório de trabalho.** As duas cópias em `/tmp` (paths diferentes, fora do
   repositório) e o config absoluto dão o MESMO objeto: nada do caminho entra no `.o`.

Resta, como explicação compatível com o sintoma exato (`teko1.o` diferente **com** `teko2.o` igual),
uma diferença de ENTRADA na primeira escada -- o único estágio que o `mc` de prateleira escreve é o
teko0, e dois teko1 semanticamente iguais produzem o mesmo teko2.o por construção (é o que ponto
fixo significa). Sem o hash do teko0 daquela corrida não dá para ir além disso, e é exatamente essa
lacuna que o item fecha.

**Instrumentação (o entregável do item B):**

1. **`ngen/scripts/bootstrap.sh`** imprime, depois do critério 1, um bloco `provenance` não-gated:
   `mc --version`, o `sha256` de `ngen/build/teko` (**teko0**, que nenhum relatório tinha), de
   `teko1.o`, `teko2.o` e `teko3.o`, e a resposta explícita de `teko1.o == teko2.o`. Com isso, uma
   divergência futura é atribuível na hora: teko0 igual + teko1.o diferente = não-determinismo do
   compilador; teko0 diferente = entrada diferente (mc, árvore ou `ngen/build` sujo).
2. **`.github/workflows/ngen.yml`** publica `teko0`/`teko3.o` na tabela do `summary` (antes só
   `teko1.o`/`teko2.o`) e, **quando `teko1.o != teko2.o`**, arquiva um artefato de 14 dias com o
   `--dump-asm` do teko0 e o do teko1 sobre `mc_teko.tk` mais o `diff` dos dois -- a forma legível
   que localiza O QUE mudou, que um objeto não dá. Nada disso é gate.

**Consequência para o golden:** o `sha256` de `teko2.o` continua reportado e não comparado. Nesta
máquina ele é reprodutível (14 escadas completas entre base e tip); pinar um golden versionado
segue dependendo de ver o número estável também no CI, agora com a provenance impressa ao lado.

## 75. R1 — release do `ngen`: o workflow que corta uma versão (2026-09-06)

O dono vai cortar uma versão estável, mergear `fix/retirement` na `main` e abrir PR para
`teko-org/teko-lang`, que passa a ser o repositório de trabalho. Este item entrega o
`release.yml` do `ngen`, o `[package].check` publicável e a retirada dos workflows do
compilador velho. Base: tip de `fix/retirement` em `77019bd6`; branch `feat/ngen-release-ci`,
quatro commits.

### (a) O workflow: promove, não recompila

`.github/workflows/release.yml` era o *Bootstrap Release* — a promoção de um prerelease
noturno de `src/`. Mesmo arquivo, conteúdo novo, quatro jobs, disparado por push de tag `v*`
ou por `workflow_dispatch` com `version`:

| job | o que faz |
|---|---|
| `version` | deriva e valida tag/versão (`X.Y.Z[-sufixo]`), prova pela API que a tag existe |
| `gate` | `uses: ./.github/workflows/ngen.yml` sobre a TAG — as 5 pernas + os 2 fixpoint |
| `release` | os 10 arquivos + notas geradas na Release da tag (cria ou atualiza) |
| `publish-to-registry` | pré-voo do pacote sempre; anúncio atrás de `TEKO_REGISTRY_PUBLISH` |

**Não há job `assets`.** Um job que compilasse o compilador outra vez publicaria bytes que
nenhum portão viu — o argumento que já tinha reescrito a `release.yml` anterior ("se o binário
que passou todos os gates é válido, release só precisa promover"). Quem empacota é a PRÓPRIA
perna, com `.github/actions/package-teko`, imediatamente depois de rodar as fixtures contra
aquele binário. Para isso `ngen.yml` ganhou `workflow_call` com três entradas — `ref` (a tag a
conferir quando o dispatch não roda nela), `package` (liga o empacotamento por perna e a
provenance do fixpoint) e `version` (o nome do asset). Push/PR comum não passa nenhuma delas e
o workflow se comporta exatamente como antes; o `push` passou a filtrar `branches`, para a tag
não disparar a matriz duas vezes contra o mesmo `github.ref`.

**Os assets** seguem a grafia do `mc`: `teko-<ver>-<os>-<arch>.tar.gz` + `.sha256`, com `arm64`
no nome e `aarch64` no `[target]`. O empacotamento é reproduzível pela mesma receita do
`scripts/release-assets.sh` do `mc` (mtime fixo, lista de membros explícita e ordenada, `ustar`,
`gzip -n`) — verificado localmente: duas execuções da action sobre a mesma árvore dão o mesmo
`sha256`. Dentro do tarball vão o binário, `lib/rt.tk` (um programa teko inclui o runtime por
caminho), `INSTALL.txt` gerado, o `README.md` do `ngen/` e o `LICENSE`.

**As notas** citam a versão do `mc` que construiu, os checksums dos cinco tarballs e as tabelas
de provenance do fixpoint. A versão do `mc` é LIDA da própria tabela de provenance em vez de ser
passada à parte: uma derivação, dois leitores, sem chance de o número publicado divergir do
medido.

### (b) `[package].check` — o que o registro consegue compilar

O validador compila cada unidade de `check` **sozinha**, na caixa linux/x86_64, sem rede e pelo
`mc` DE PRATELEIRA (guia 27 §5). As duas entradas que estavam lá falhavam, medido com mc 0.15.12
de dentro de `ngen/`:

| unidade | o que o mc de prateleira diz |
|---|---|
| `teko.tk` | `machine_arm64_float:332: initializer must be constant` — é fragmento |
| `lib/rt.tk` | `lib/rt.tk:64: type expected in parameter` — o `str` de `void panic(str msg)` |
| `mc_teko.tk` | exit 0 |

Logo `check = ["mc_teko.tk"]`, e `files` ganhou `mc_teko.tk` + `core_teko.mc` + `user.mc` (o
hash de árvore cobre só `files`; unidade de `check` fora dele é recusada). É a forma do pacote
do próprio `mc`. Hash novo: `057e7aed61f2f244d52a53b522b06fb84eaf88d552247e37060ea651761424d5`
(era `d41a0c80…`), o MESMO calculado no runner linux do CI. A ressalva do §3.3 do handoff segue
viva: teko-ificar o compilador (S4.4+) torna `mc_teko.tk` ilegível ao parser de prateleira e o
pacote perde o `check`.

### (c) Os workflows do compilador velho saíram

Oito arquivos removidos — `pr.yml` (2940 linhas), `nightly.yml`, `reseed-bootstrap.yml`,
`seed-linux-fork.yml`, `tag-on-version-bump.yml`, `theory.yml`, `theory-generation-decay.yml`,
`mirror-pr-to-org.yml`. Ficam `ngen.yml`, `release.yml`, `codeql.yml` e `branch-policy.yml`.
O CodeQL perdeu a perna `c-cpp` (compilava `src/runtime/teko_rt.c` e `src/assert/assert.c`, árvore
congelada) e ficou com `actions`, que é o que o CI de hoje realmente é; `paths` virou
`['ngen/**', '.github/**']`. `branch-policy.yml` não foi tocada de propósito: ela recusa origem
`theory/**`/`cargo/**` contra `main`/`remodel/*`, namespaces do fluxo de vagão que morre com esta
limpeza — proposta de remoção registrada no §3.4 do handoff, decisão do dono.

### (d) A prova: o run de teste

Tag `v0.0.0-test` empurrada na branch, run **34050196515** — **11 jobs verdes**, incluindo as 5
pernas, os 2 fixpoint, o agregador `mc build ngen && run`, a Release e o registro. A Release de
teste trouxe os **10 arquivos** (5 tarballs + 5 `.sha256`), marcada como pre-release por causa do
sufixo `-test`, com as notas descritas em (a). `publish-to-registry` rodou o pré-voo — `mc pkg
hash ngen = 057e7aed…` e `ok: mc_teko.tk compiles on its own` — e **não** publicou: sem a
variável, imprimiu o plano e saiu 0. Tag e Release de teste foram apagadas em seguida.

Dois achados do run, ambos registrados no handoff:

1. **O arquivo estava DESLIGADO.** O GitHub identifica workflow por CAMINHO; `release.yml` era o
   *Bootstrap Release*, `disabled_manually` desde a limpeza de 2026-09-04, e a primeira tag não
   disparou NADA — sem erro, sem run. Foi preciso religar por
   `gh api -X PUT .../actions/workflows/316148340/enable`. Vai acontecer de novo na org.
2. **O `teko0`/`teko1.o`/`teko2.o` de macos/aarch64 do CI são byte-idênticos aos construídos
   localmente** (`aa102141…` e `d530d6b1…`) — mais uma corrida a favor de pinar um golden.
---

## 76. V0 — inventário de recusas (2026-09-06)

Primeiro crumb do desvio **"v0.1.0 estável"**. A regra do corte é uma só: **zero resultado errado
silencioso**. O que a teko-sobre-mc ainda não ensina não pode ser aceito e respondido errado — tem
de ser **recusado**, com mensagem no estilo do resto (`teko: <causa curta>`, arquivo:linha). Três
buracos conhecidos, um commit cada, nenhuma superfície nova, nenhuma fixture nova (o corpus de
`ngen/tests/` não tem fixture de erro — recusa se prova por probe), `ngen/mc.toml` intocado.

### (a) Item 1 — a lista `params` é de PALAVRAS, e um float não é uma

A dívida que a higiene 4 registrou (§74(b)): `tk_va_put`/`tk_va_at` (`ngen/lib/rt.tk`) gravam e
devolvem a lista por parâmetro `i64`. Um `f64`/`f32` viaja no OUTRO banco de registradores, então
nem entra pelo `put` nem sai pelo `at` — `f64 total(params xs)` com `total(1.5, 2.5)` respondia com
o que o banco inteiro tivesse, na base e no tip, sem uma palavra. Enquanto `params T[]` não existir
(o core não tem `[` em posição de parâmetro), as duas pontas são recusadas ONDE O TIPO É VISÍVEL:

1. **O argumento que cai na lista.** `tk_va_check_float_args`, chamada de `tk_va_lower_call` sobre a
   CAUDA (a parte que vira o pacote) e só sobre ela — um parâmetro fixo declarado `f64` continua
   valendo, porque ele não passa pela lista. O tipo de cada argumento sai de `tk_va_arg_ty`: o
   próprio nó para um literal (`1.5` é um `N_INT` cujo `nd_type` é `f64` — `fl_lit`, `lib/float.mc`)
   e para um cast, e o oráculo de parse `tk_pty_of` (teko_struct.tk) no resto. `-1` é "não sei", nunca
   recusa. Mensagem: ``teko: a `params` list holds words; a float argument is not taught yet``.
2. **O elemento lido como float.** `tk_va_check_float_read`, chamada no TOPO do laço de `tk_va_walk`
   — antes dos filhos, porque o `xs[i]` que ela precisa ver é justamente o que o walk rebaixa em
   seguida. `tk_va_reads_list` reconhece o índice sobre o parâmetro-lista da instância, direto ou sob
   aritmética (`xs[0] + xs[1]`, `0 - xs[0]`); uma leitura que atravessa uma CHAMADA não conta (a
   palavra entra como o `i64` que é, e o que volta é o tipo do callee). Os três destinos que soletram
   o tipo são o inicializador de um local float, uma atribuição a um, e o `return` de uma instância
   cujo retorno declarado é float (`tk_va_ret`, salvo/restaurado em `tk_va_inst` ao lado de
   `tk_va_n`). Mensagem: ``teko: a `params` list holds words; its element does not read as a float``.

**Limites, deliberados e documentados:** um PARÂMETRO float repassado para a lista não é pego — a
tabela `tk_slv_find` nunca vê parâmetro (o mesmo limite que a higiene 3 mediu) —, nem uma leitura que
uma chamada carrega. É a regra do `tk_check_field_store`: recusa-se o que se sabe, cala-se no resto.

### (b) Item 2 — o braço `_` textualmente último não carrega `when`

A expression dobra os braços do ÚLTIMO para trás (`tk_switch_infix`), então o último braço é a base
INCONDICIONAL da cadeia e a condição dele nunca é testada. Para um braço com rótulo isso é apenas um
braço morto (o `1` de um `_` anterior casa primeiro); para o `_` **com `when`** é resultado errado: a
guarda escrita é descartada e o braço é tomado assim mesmo. Recusado agora, na linha do próprio braço:
``teko: the last `_` arm of a switch expression cannot carry a `when` ``.

A mecânica é uma bandeira por braço (`isDefault` × `when` visto), zerada a cada volta e consultada
depois do `}` — quem sobra é o último. **Semântica dos demais braços inalterada:** `_ when c` no meio
continua dobrando para `1 && c` e sendo testado, `or` segue como estava, e um braço escrito depois de
um `_` puro segue morto em vez de errado.

### (c) Item 3 — o retorno de uma chamada passa a tipar o oráculo de parse

O verificador da higiene 2 registrou: campo de outro objeto já era pego (`tk_field_use` tagueia o
load), mas `b.useCircle(f())` com `f()` devolvendo `Square` onde se pede `Circle` passava em silêncio,
porque `tk_pty_of` não tipava `N_CALL`. Passa a tipar, por `decl_find`/`decl_ret` — **o mesmo par que
o oráculo PASS-TIME (`tk_ty_of`) já lê para o mesmo nó**, de modo que posição de ARGUMENTO passa a
responder o que posição de INICIALIZADOR sempre respondeu (um `Circle c = f_que_devolve_i64();` já era
recusado; o argumento não). Os dois consumidores do oráculo ganham a resposta juntos
(`tk_vcall_args_check`, teko_expr.tk; `tk_ifargs_check`, teko_iface.tk). `ref`/`out` recebe um NOME,
nunca uma chamada, e a DI não lê esse oráculo — não há terceiro sítio a tocar.

**O que continua em silêncio, medido por probe:**

- um callee declarado ABAIXO do sítio de chamada (`decl_find` responde sobre o que o core já parseou;
  um protótipo acima do sítio também não fechou o caso, medido);
- uma chamada INDIRETA (`callp` de delegate/virtual/interface em posição de argumento), que declaração
  nenhuma nomeia;
- **receptor que é PARÂMETRO** — limite ANTIGO da checagem, não deste oráculo: a classe do receptor não
  é conhecida no parse, então `tk_call_method` nem chega ao sítio onde os argumentos seriam checados.
  Probe: a MESMA chamada com receptor LOCAL é recusada e com receptor PARÂMETRO passa, na base e no tip.

### (d) Gate e probes

Host macOS/aarch64, `mc` **0.15.12**, base `77019bd6`, os três commits juntos: `rm -rf ngen/build`,
build do zero; `--entry-only` **45/45**; `--dump-ast` das **45 byte-idêntico** ao compilador da base
(`same=45 diff=0`) — uma recusa não muda o código aceito; `mc limits ngen --config` `verdict ok`, com
`intrin 8/16` e `passes 15/30`, os mesmos da base; `sh ngen/scripts/bootstrap.sh` → **`FIXPOINT OK`**
(`teko1.o == teko2.o == teko3.o`, `sha256` `034843cd…`, `--dump-asm` 191 586 linhas com diff vazio,
teko1 compila as 45, 62,4 s no total); `ngen/mc.toml`, `ngen/tests/` e `ngen/scripts/` intocados.

Probes em `ngen/_probe/` (apagado ao fim; cada um rodado TAMBÉM contra o compilador da base, para
separar correção de regressão):

| probe | o que prova | tip | base |
| --- | --- | --- | --- |
| literal float em `params` | item 1, argumento | recusa | aceita |
| local `f64` em `params` | item 1, argumento pelo oráculo | recusa | aceita |
| `f64 v = xs[0];` | item 1, leitura por inicializador | recusa | aceita |
| `v = xs[1];` (local `f64`) | item 1, leitura por atribuição | recusa | aceita |
| `return xs[0] + xs[1];` de `f64 total` | item 1, leitura por retorno | recusa | aceita |
| `_ when g` como último braço | item 2 | recusa | aceita |
| `_ when g` no meio + braço guardado depois do `_` | item 2, controle | roda 42 | roda 42 |
| `Square` por vtable / por itab | item 3 | recusa | aceita (exit 0) |
| tipo certo por vtable + itab + chamada escalar | item 3, controle | roda 42 | roda 42 |

### (e) Dívida ADJACENTE achada (não é deste crumb)

`xs[0]` de uma lista `params` usado como ARGUMENTO de uma chamada por vtable morre em `expression
with no codegen`: a chamada já foi rebaixada a `callp` no parse e o `N_INDEX` sobrevive ao walk da
instância. Reproduzido na BASE e no tip — não é regressão desta onda, e fica registrado.

## 77. V2 — pinar a versão do mc no CI + README do `ngen/` (2026-09-06)

Segundo crumb do desvio "v0.1.0 estável". Dois itens, nenhuma superfície nova.

### (a) Item 1 — o CI para de resolver `latest`

A 0.15.12 provou o risco: um patch release do mc pode renomear/mover superfície (o driver
perdeu 12 globais por acessores, §3.2) e `.github/actions/setup-mc` resolvia `releases/latest`
sempre, então a quebra entraria em CI **sem aviso**, no primeiro PR aberto depois do release.
Corrigido movendo a fonte da verdade para um arquivo: **`ngen/MC_VERSION`** (uma linha, `0.15.12`,
sem `v`). `setup-mc` ganhou o input `version` — vazio (o default, o que os três chamadores usam
hoje) lê o arquivo; `latest` só resolve quando `inputs.version` pede explicitamente; qualquer
outra string pina aquela tag (`releases/tags/v<versão>` em vez de `releases/latest`, que também
prova que a tag existe). Os TRÊS consumidores da action (as cinco pernas, os dois runners de
`fixpoint`, e `publish-to-registry` do `release.yml`) herdam o pin sem mudança própria — nenhum
passa `version`. `ngen/scripts/bootstrap.sh` e `ngen/HANDOFF.md` §3.1/§3.2/§4 passam a citar
`ngen/MC_VERSION` como a resposta a "qual mc o CI usa" (`cat ngen/MC_VERSION`), e §3.2 ganha o
processo de bump: baseline local 45/45 + `bootstrap.sh` → `FIXPOINT OK` contra o mc NOVO, **antes**
de trocar o arquivo — nunca o inverso. `[package].mc = ">= x"` (a proposta do lado do mc, D230
adendo 2 item 3) não entra: é superfície do validador deles, ainda não publicada; fica só citada
como rumo futuro, não em `ngen/mc.toml` (mexer no `[package]` muda o hash de árvore do pacote).

Gate: `ngen.yml` (5 pernas + `fixpoint`×2) verde na branch, com o log mostrando `pinned
minicompiler/mc v0.15.12` (não `resolved minicompiler/mc latest`); `sh ngen/scripts/bootstrap.sh`
local → `FIXPOINT OK`; `ngen/*.tk`, `ngen/mc.toml` e `ngen/tests/` intocados.

### (b) Item 2 — `ngen/README.md` reescrito

O README estava stale desde a migração de org (§3.1a): apontava `github.com/schivei/mc` e não
citava `ngen/MC_VERSION`, o registro `https://pkg.minicompiler.dev` nem a forma real de consumo
(`[deps] teko`/`#include <teko>`, D230). Reescrito curto (106 linhas): o que é o `ngen/`, como
buildar (`mc build ngen`), como rodar as fixtures e o `bootstrap.sh`, a versão pinada (aponta para
o item (a)), como será consumido, e a lista curta do que fica FORA da v0.1.0 (`Func<>`/`Action<>`,
multicast de delegate, `params T[]` embalando lambda, `T[][]`, namespace aninhado, DI genérica por
função — a forma por marcador de interface É implementada, D229 —, float em `params` — recusado
desde o V0 §76 — e `when` no braço `_` final — idem). O conteúdo antigo (a tabela hook-por-hook das
entregas 1-2, o discurso de "por que primitivo X é alias", a seção de validação offline sem rede)
não é história que o README deva carregar — mora nos §§ deste plano e no HANDOFF; o README aponta
para lá em vez de duplicar.

Gate: mesmo do item (a) — o README não toca código, então o gate é o mesmo run de CI que prova o
item (a); revisão de conteúdo (≤120 linhas, sem link `github.com/schivei/mc`) é o critério próprio.

## 78. S4.3b — o `fixpoint` cobre as cinco pernas (2026-09-06)

Terceiro crumb do desvio "v0.1.0 estável", e o fecho das duas dívidas que o §73(e) registrou
(itens 3 e 4). A escada do fixpoint deixa de rodar em dois pares e passa a rodar nos **mesmos
cinco** que as pernas nativas cobrem. Base: o tip de `fix/retirement` em `756fd924`; branch
`feat/ngen-s43b-fixpoint-all`, três commits — `a488063a` (linux/aarch64), `02b1106b` (Windows) e
o de docs. Nenhuma mudança em `ngen/*.tk`, `ngen/mc.toml` ou `ngen/tests/`; `ngen/MC_VERSION`
segue `0.15.12`.

**Por que cinco e não três.** O que a escada prova é propriedade **de uma máquina** — que o
compilador escrito ALI se reproduz ALI. Provar em três pares e afirmar em cinco é exatamente a
afirmação cross-compilada que a matriz de pernas existe para recusar (§64, "nada é cross-compilado
e deixado sem rodar"). O job `fixpoint` passa a espelhar a matriz `leg`, par a par.

### (a) linux/aarch64 — configuração, como o §73(e) item 4 previa

`ubuntu-24.04-arm` vira a terceira entrada da matriz e nada mais muda: o par já tinha perna e o
`write_target_tail` do `bootstrap.sh` já conhecia o loader glibc dele
(`/lib/ld-linux-aarch64.so.1`), escrito quando o §73(c) resolveu o `exit 127` do x86_64. Ficou
fora do S4.3 só para o job novo custar dois runners.

### (b) Windows — o sysroot fatorado, e o `[linker]` que a escada precisa

A escada precisa do `<out>.o` em disco, o que exige `[linker]`; no Windows não há `cc` nem C
runtime, então o link é `lld-link` contra três arquivos. Duas peças:

1. **`.github/actions/windows-sysroot`** (action composta, nova) é a montagem que a perna Windows
   fazia em dois passos inline: LLVM no `$PATH` em forma **Windows** (`cygpath -w` — o `mc` chama
   o linker por `CreateProcessA`, que lê o PATH do Win32), `TMPDIR` no temp do runner (o `mc`
   nativo não abre `/tmp/...`), e `winstart.obj`/`mcrt.obj`/`kernel32.lib` compilados do bundle do
   próprio `mc` (`--backend=coff-obj-{x86_64,arm64}` + `llvm-dlltool` sobre a lista de 15 exports).
   A perna e o fixpoint a usam — **zero duplicação**, e não há como os dois montarem sysroots
   diferentes.
2. **`bootstrap.sh --linker-toml FILE`**: os configs derivados removem o `[linker] cc` do
   `ngen/mc.toml` (mesmo `awk` do config das pernas) e recebem no fim os blocos
   `[sysroot]`/`[linker]` do arquivo. A matriz do job escreve a MESMA linha `lld-link` da perna
   (`-entry:mc_start -nodefaultlib -stack:8388608`). POSIX `sh`, sem `set -e`, status por passo.

**Nomes.** No Windows todo estágio é `<nome>.exe`. O `mc` anexa o sufixo do host ao
`[compiler].out` sozinho (`drv_teach`/`host_exe_suffix`); o `[project].out` é o script que nomeia,
e o objeto sai de `out + ".o"` — então o critério compara `teko2.exe.o` com `teko3.exe.o`, COFF,
mesmo `cmp`. O laço das 45 fixtures roda `.exe`. O `mc` também vai para o `$GITHUB_PATH` em forma
Windows.

**Nada foi preciso do lado do `mc`.** `--dump-asm` sobre um compilador COFF, `--entry-only`,
`--compiler-only` e o link por `[linker]` funcionaram como nos outros pares, na 0.15.12 pinada.

### (c) O achado: o sufixo do compilador ensinado é o do HOST

`drv_teach` monta o nome do binário com `host_exe_suffix()` — o compilador ensinado tem de RODAR
na máquina que o escreveu, então o sufixo é o do host, nunca o do `[target]`. Um probe com
`--os windows` numa máquina macOS escreveu `ngen/build/teko` e o script procurou
`ngen/build/teko.exe`: `exit 127`, com o binário ali (a mesma classe da armadilha 30, outra causa).
A correção não é adivinhar sufixo — é **recusar alvo ≠ máquina**: `bootstrap.sh` confere
`--os`/`--arch` contra `mc --host` e falha na hora, com a causa. A escada **executa** todo estágio
que constrói; um ponto fixo que não roda não é ponto fixo, e `--os`/`--arch` existem para dizer o
par em voz alta no log do CI, não para cross-compilar.

### (d) O que o CI mediu (run `34052547541`, mc 0.15.12, 11/11 verde)

| | teko0 | 0→1 | 1→2 | 2→3 | total | `--dump-asm` | `teko2.o` | `sha256(teko2.o)` |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| linux/x86_64 | 1,494 s | 3,183 s | 3,157 s | 3,163 s | 20,682 s | 192 502 linhas | 2 071 936 B | `d37e4cb3…` |
| linux/aarch64 | 2,578 s | 7,457 s | 7,189 s | 7,519 s | 41,938 s | 191 667 linhas | 2 247 992 B | `6e80a42e…` |
| macos/aarch64 | 2,805 s | 5,792 s | 4,166 s | 4,199 s | 33,751 s | 191 586 linhas | 1 722 368 B | `034843cd…` |
| windows/x86_64 | 2,360 s | 4,842 s | 4,835 s | 4,857 s | 41,930 s | 191 564 linhas | 1 820 893 B | `a45444fd…` |
| windows/aarch64 | 3,062 s | 7,491 s | 7,409 s | 7,405 s | 71,524 s | 191 564 linhas | 1 771 513 B | `30aed5f0…` |

`teko1.o == teko2.o == teko3.o` e 45/45 fixtures nos **cinco** — ponto fixo na primeira volta em
todos. O objeto de macos/aarch64 (`034843cd…`) é byte-idêntico ao construído no host local, a mesma
evidência entre-máquinas do §73(d), agora sobre a 0.15.12. As duas arquiteturas de Windows dão o
MESMO número de linhas de `--dump-asm` (191 564) e objetos diferentes, que é o esperado: o dump é o
mesmo programa, o objeto é COFF de máquinas diferentes.

### (e) O que continua NÃO sendo gate

1. **`sha256` reportado, não barrado** — inalterado desde o §73(e) item 1. Agora há cinco tabelas
   de provenance por run (e cinco no corpo de uma release: `release.yml` já lê
   `provenance/*/provenance.md` por glob, então nada muda lá).
2. **O agregador não mudou.** `mc build ngen && run` continua dependendo só da matriz `leg`.
   Promover a escada a check obrigatório segue sendo decisão de ruleset, tomada fora deste
   workflow; os cinco nomes de context estão listados em `docs/design/pr-org-ngen.md` §2/§5 para
   quem for configurá-lo.

**Gate local** (host macOS/aarch64, `mc` 0.15.12): `rm -rf ngen/build`;
`sh ngen/scripts/bootstrap.sh` → `FIXPOINT OK`, 45/45, `034843cd…` nos três objetos, `--dump-asm`
191 586 linhas diff vazio — e o MESMO run por `--linker-toml` com um bloco `cc` equivalente
reproduz os três hashes byte a byte, que é a prova de que a substituição do `[linker]` deriva um
config equivalente.

## 79. V1 — retorno float por chamada indireta: o que landou, e a errata do §74(b)

Crumb executado a partir de `docs/design/plano-v1-float-callp.md` (desenho antecipado), base
`a66b80c9`, branch `feat/ngen-v1-float-callp`, cinco commits (bump do `mc`, delegate, virtual,
itab, docs). Descrição do que landou no `ngen/HANDOFF.md` §5, bloco **V1**; a armadilha da regra de
identidade virou o item **33** do §5.1.

### (a) Errata do §74(b) — os dois pedidos ao mc estão FECHADOS

1. **`callp` tipado `TY_I64`** — fechado pelo **mc 0.15.13**: um cast aplicado DIRETAMENTE ao
   `callp` declara o retorno (`res_expr`, arm `N_CAST` de `mc/src/gen_resolve.mc`, empurra
   `nd_type` para o nó do intrínseco). Do lado da teko, `tk_callp_ret` (`ngen/teko_array.tk`) põe
   esse cast nos cinco construtores de `callp`. O repro do §74(b) responde **5.0** escrito
   `1.0 + (f64) callp(p, 2.0)` e **3.0** sem o cast, no MESMO binário — ou seja: o contrato do
   núcleo já valia antes deste crumb, o que faltava era a teko EMITIR o cast.
2. **Conversões single do `<float>` em arm64** — fechado na mesma release (`FI_SCVTF_D..FI_UCVTF_D`
   e `FI_FCVTZS_D..FI_FCVTZU_D` com `+2`). Re-conferido por probe: `i64 main() { f32 y = 2.5f;
   return (i64) (y * 10.0f); }` dá **25** na base e no tip. **Zero código do lado da teko**, e a
   ressalva do §74(a) ("fixture e probes de `f32` comparam contra literais em vez de castar") deixa
   de ser necessária — `fdcheck` compara contra literal `f32` por preferência de leitura, não por
   contorno.

A **dívida adjacente do §74(b) (lista `params` sem tipo de ELEMENTO) continua ABERTA** e é
superfície, não V1 — ver (c) item 3.

### (b) O que divergiu do `plano-v1-float-callp.md`

O desenho foi seguido inteiro: o shaper saiu como escrito (§3 do plano, incluído verbatim), os
cinco sítios são exatamente os do censo (§2), a ordem de commits e o gate por commit valeram, e a
partição de AST bateu a previsão do §4 na vírgula — `same=44 diff=1` por commit, **`same=42
diff=3`** acumulado. Divergências, todas de MEDIÇÃO, nenhuma de desenho:

1. **O diff das três fixtures tocadas não é só aditivo.** Além das linhas `CAST type=f64`/`f32` (6
   em `surface_delegate`, 5 em `types_class`, 3 em `types_interface`), cada uma tem linhas
   REMOVIDAS: são os temporários `$gN` renumerados, porque o caso novo entra antes do resto do
   corpo. Verificado que **toda** linha removida é um `$gN` deslocado (`grep '^<' | grep -v '\$g'`
   vazio nas três) — a previsão "byte-idêntico exceto as três" vale, mas a forma do diff não é a
   puramente aditiva que crumbs anteriores mediram.
2. **O probe 1 não roda no `mc` de prateleira.** O repro do §74(b) é código do dialeto do mc, mas
   os tipos `f32`/`f64` vêm do COMPILADOR (`<float>`, M24), não de um include: `mc --exe` sobre ele
   para em `float_rt:46: type expected in parameter`. Rodou-se pelo compilador ENSINADO, que é
   quem tem os tipos — e é também o que torna a medição interessante (mesmo binário, com e sem
   cast). O `mc` cobre o caso puro na própria suíte (`tests/float/023-callp-f64.mc`).
3. **O virtual erra mais fundo que o delegate na base.** O §74(b) descrevia "acerta por
   coincidência de registrador"; medido, o delegate acerta a forma DIRETA (`d(2.0)` = 4.0) e erra a
   aninhada, enquanto o virtual e a interface erram **já na forma direta** (`p.area() != 3.0`), com
   a profundidade zero. Não muda o conserto — muda o que um probe de uma linha só teria concluído.

### (c) Achados medidos (o §8 do plano pedia REPORTAR, não virar item)

1. **O estreitamento inteiro por despacho indireto JÁ estava certo na base** nos sítios #2–#5
   (`virtual i32`, `virtual u8`, `override`, por `this` implícito, por parâmetro de tipo base,
   `interface i32`): 42 na base e 42 no tip. Quem estende é o CALLEE (extensão M45), então o cast
   que o shaper unificado passa a pôr nesses quatro é cinto-e-suspensório — o `sxtw` idempotente
   que o próprio contrato do núcleo prevê. Não havia defeito vivo; o #1 (delegate) moldava desde o
   K2w e segue moldando pelo mesmo shaper.
2. **O reclaim não notou o envelope.** Laço de 100 closures `f64` com `rt_live() == 0` no fim: 42
   na base e no tip. A regra de identidade (§5.1 item 33) é o que garante isso — os quatro sítios
   que copiam o resultado para a árvore (`node_assign`/`tk_node_replace`) copiam o nó de FORA.
3. **`params` + float por chamada indireta: metade fecha, metade continua aberta.** Com o cast na
   árvore, `tk_va_arg_ty` (`teko_params.tk`, que lê `N_CAST`) passa a enxergar o float vindo das
   formas construídas no PARSE — `total(p.area())` agora é recusado com ``teko: a `params` list
   holds words; a float argument is not taught yet``, onde a base compilava e devolvia lixo. A
   forma DELEGATE continua silenciosa (base 48, tip 32 — lixo dos dois lados): `tk_params_pass`
   roda ANTES do `tk_deleg_pass` (ordem em `teko.tk`), então o argumento ainda é um `N_CALL` cru
   quando a checagem o examina. Fechar isso é a dívida de superfície do §74(b)/§76 (dar tipo de
   ELEMENTO a uma `params`, grafia `params T[]`) ou mover a checagem para depois do passe de
   delegate; palpitar pela tabela `tk_slv_find` recusaria programa CORRETO (armadilha 27), então
   não se palpita.
