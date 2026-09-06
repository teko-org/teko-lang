# Plano — §9 B: sobrecarga de TIPO por aridade genérica + construção `self { … }`

> **Status:** DESIGN. Read + Write(este .md) apenas — nenhum código de produto editado, nenhum build,
> nenhum `teko test .`, nenhum reseed nesta crumb. Este documento É o artefacto; o único commit desta
> crumb é ele próprio.
> **Branch:** `fix/retirement` (onda de migração-de-superfície 0.3.1; drena sequencial, SEM PRs).
> **Fonte de lei:** rulings SELADOS do dono para §9 B (abaixo, VINCULATIVOS — o plano desenha À VOLTA
> deles, nunca os re-abre).
> **Irmãos:** §9 A (sobrecarga de método/função — `docs/design/plano-secao9A-method-overload.md`),
> §9 C (defaults + args nomeados `:=`). §9 B é o lado-TIPO: re-chaveia a `TypeTable` por aridade e
> troca a magia `Foo{}`-é-Self pela construção EXPLÍCITA `self { … }`. Independente de §9 A/§9 C.
> **Lei permanente:** Teko-only (.tks), W15 + Javadoc-completo em TODA declaração, law-first. TODOS os
> snippets abaixo já estão em estilo full-Javadoc — o implementador copia-os verbatim.

---

## 0. Rulings SELADOS do dono (LEI — desenha-se à volta, não se re-abre)

1. **Chave de decl-de-tipo passa a `(namespace, name, ARITY)`.** Hoje é `(namespace, name)` em
   `check_no_duplicate_types` / `duplicates_of_reg` (`src/checker/collect.tks:2013` / `:2035`). Assim
   `Foo`, `Foo<T>`, `Foo<T,U>` coexistem (aridades 0..16, `max_generic_arity() == 16`,
   `resolve.tks:1074`).
2. **Mesma aridade = ERRO na DEFINIÇÃO**, independentemente da constraint OU do nome do type-param. A
   constraint NUNCA desambigua. Cross-kind mesma-aridade (`struct Foo<T>` + `enum Foo<T>`) TAMBÉM
   colide pela mesma chave.
3. **Uso é explícito/sintático:** `Foo` nu = aridade-0 SEMPRE; `Foo<X>` = aridade-1. SEM aplicação
   parcial (`Foo<i64>` para um `Foo<T,U>` = erro de aridade).
4. **Construção é SEMPRE um prefixo de tipo:** `Foo { … }` (aridade-0) | `Foo<i64> { … }` (nome + args
   explícitos) | `self { … }` (dentro de um método static OU de instância → constrói o tipo envolvente
   / Self). **SEM construção anónima `{ … }`** — um `{` em posição de expressão é SEMPRE um bloco
   (block-B), nunca uma construção. É isto que resolve a colisão `{}`-vs-block-B.
5. **`self` ganha um 3º papel: INICIALIZAÇÃO-de-tipo** (`self { … }`), por cima de `self` como tipo em
   param/var/return (já existe) e de `self` como receptor-valor (já existe, `parse_expr.tks:445`).
6. **Apagar a inferência-de-self-construct** (`is_self_generic_construct` / `phantom_self_inst_name`,
   `typer.tks:2990-2994` / `resolve.tks:2187` / `:2206`) — substituir a magia `Foo{}`-é-Self pelo
   caminho EXPLÍCITO `self { … }`. Varrer os ~2 sítios de self-construct
   (`src/collections/list.tks:26` `List::make`, `src/collections/map.tks:44` `Map::make`) para
   `self { … }`.
7. **Retirado:** `Foo { … }` para um Foo GENÉRICO com type-args emprestados da anotação (bare Foo =
   aridade-0 sempre; a retarget-por-anotação em `typer.tks:2987-3018` desaparece).

---

## 1. Estado de HOJE — onde a "unicidade por nome" realmente vive (achados)

### 1.1 O gate de duplicados (lado-registo)

`check_no_duplicate_types` (`collect.tks:2013`) é um VALIDADOR PURO: lê a `TypeTable` e não regista
nada, logo a tabela que o próximo passo vê é byte-idêntica com ou sem rejeição (mesma disciplina que
§9 A não tem no lado-função). O corpo por-registo é `duplicates_of_reg` (`collect.tks:2035`), cuja
igualdade de colisão é HOJE (`collect.tks:2043`):

```
if !w0_is_stamped_generic_reg(other, table) && other.name == reg.name && other.namespace == reg.namespace {
```

Ou seja a chave é `(namespace, name)` — SEM aridade. Duas decls `Foo` / `Foo<T>` colidiriam HOJE
(mesmo `(ns,name)`). É exatamente esta linha que o ruling 1 re-chaveia. Instâncias genéricas stampadas
(`Box__g__i64`) estão EXENTAS via `w0_is_stamped_generic_reg` (ns vazio + infixo `__g__`).

### 1.2 A resolução (lado-referência) — namespace-ciente mas aridade-CEGA

Uma referência de tipo resolve por `resolve_type` (`resolve.tks:1919`), que ramifica em
`parser::NamedType` (`:1925`):
- `nt.args.len == 0` → `resolve_named` (`:1159`) — o caminho ARIDADE-0.
- `nt.args.len > 0` → `resolve_generic_inst` (`:2244`) — o caminho ARIDADE-N.

Ambos descem a `resolve_type_ref` (`:714`) → `resolve_type_reg` → `resolve_bare_type_reg` (`:599`) /
`resolve_qualified_type_reg` (`:676`), que casam pelo **nome (last-segment) + namespace** e devolvem o
PRIMEIRO match — **sem olhar à aridade**. `resolve_generic_inst` (`:2277-2282`) só DEPOIS confirma
`td.type_params.len == nt.args.len` e emite "não-genérico com args" / "número diferente de args".

**Consequência crítica (o cerne de §9 B):** se `Foo` (aridade-0) e `Foo<T>` (aridade-1) coexistissem,
`resolve_type_ref` devolveria o PRIMEIRO por nome e a checagem-de-aridade RE-JEITARIA a outra forma —
nunca SELECIONARIA a certa. Logo o re-key do registo (1.1) é NECESSÁRIO mas NÃO SUFICIENTE: os
resolvers de referência têm de passar de aridade-CEGOS a aridade-SELETIVOS.

A mesma cegueira vive em `type_table_find` (`resolve.tks:297`) — namespace-blind, casa nome-exato /
last-segment, devolve o primeiro — e nos seus 46 call-sites (`type_table_find(` × 46 em 7 ficheiros).
A maioria passa um nome JÁ-resolvido/stampado (`Box__g__i64`), unívoco por construção; o subconjunto
que passa um nome BARE de utilizador é o que ganha ambiguidade sob overload.

### 1.3 A construção (`type_struct_lit`, `typer.tks:2943`)

Fluxo atual:
1. `:2944` `resolve_named(sl.type_path)` — resolve o nome do prefixo (SEMPRE, mesmo quando há
   `type_args`).
2. `:2965` `type_table_find(table, name, "")` — obtém a `decl`.
3. `:2977-2986` (W9.4) caminho de `sl.type_args` explícito (`Foo<i64>{…}`) → `resolve_generic_inst`,
   valida aridade, retarget para a instância concreta.
4. `:2987-3018` (S4) para uma `decl` GENÉRICA sem args explícitos: (a) `is_self_generic_construct` →
   trata como self-construct fantasma; (b) senão RETARGET-POR-ANOTAÇÃO — usa `expected` (`tgt`) para
   nomear a instância (`Box{…}` sob `: Box<i64>` → `Box__g__i64`).
5. `:3019-3042` gate de classe (abstract não-instanciável; literal-de-classe só dentro dos próprios
   métodos, `env.owner_type`), campos, tipagem, emissão da `TStructInit` com `type = Named{name}`
   (`:3104`).

O ruling reescreve os passos 1, 3, 4: o prefixo passa a decidir a aridade; a retarget-por-anotação
(4b) é APAGADA; a inferência-de-self (4a) é substituída por um ramo `self { … }` explícito no topo.

### 1.4 A superfície `self` no parser

`self` é palavra-reservada: `parse_primary` (`parse_expr.tks:445`) devolve `Var{name="self"}` e
PÁRA (`next = pos + 1`), deixando um `{` seguinte para o parser de bloco. Logo HOJE `self { … }` NÃO
parseia como construção — o `{` abre um bloco. É esta a colisão que o ruling 4 fecha, e é o único
ponto do PARSER que §9 B toca.

### 1.5 `.tkb` (serialização)

`RTypeDecl` (`emit/tkb_read.tks:827`) já serializa `type_params` no `parser::TypeDecl`; a ARIDADE é
`type_params.len` — **derivável do que já é serializado**. Nenhum campo novo no `.tkb`, nenhuma
mudança de formato. A chave `(ns,name,arity)` é sempre computada, nunca persistida.

---

## 2. Re-key do registo — a chave `(namespace, name, arity)` (ruling 1/2)

Mudança cirúrgica em `duplicates_of_reg` (`collect.tks:2043`): a igualdade de colisão ganha a aridade
(`decl.type_params.len`). Cross-kind (struct/enum/class) já colide por nome — a aridade é o único eixo
novo, e a constraint/nome-de-param NUNCA entram (ruling 2, já respeitado: a igualdade nunca os olhou).

```
/**
 * Every LATER registration in `table` that duplicates the entry at `at` under the §9 B key
 * `(namespace, name, ARITY)` — the inner half of the W0 ban, extracted so the outer walk stays flat.
 * The ARITY (`decl.type_params.len`) is the §9 B discriminant: `Foo`, `Foo<T>` and `Foo<T,U>` are
 * THREE distinct declarations that coexist, so only a SAME-arity redeclaration is a duplicate. The
 * constraint and the type-param NAMES never enter the key (ruling 2) — two `Foo<T>` collide even when
 * one is `Foo<T: Ord>` and the other `Foo<U>`. Cross-kind is already keyed by name, so `struct Foo<T>`
 * and `enum Foo<T>` collide through this same arity comparison. Reported against the DUPLICATE's own
 * position, not the first declaration's, so the diagnostic points at the redeclaration to delete.
 *
 * @param table  the collected type table
 * @param at     the index of the registration being compared against
 * @return       one diagnostic per same-arity duplicate found after `at` (empty when there is none)
 * @since §9 B
 */
fn duplicates_of_reg(table: TypeTable, at: u64): []str
```

Corpo: a condição em `:2043` passa a incluir
`&& other.decl.type_params.len == reg.decl.type_params.len`. A mensagem ganha a aridade para
legibilidade, p.ex. `duplicate type 'Foo' (arity {n}) in namespace '{ns_label}'`. **Nada mais em
`collect.tks` muda** — `check_no_duplicate_types` (`:2013`), `validate_type_decls` (`:2061`) e o walk
externo ficam intactos (a exempção `w0_is_stamped_generic_reg` continua a valer).

**Nota de aridade-máxima (verificar):** aridades 0..16 (`max_generic_arity()`), já imposta na
declaração de genéricos noutro sítio — §9 B não altera esse teto, só permite coexistência de aridades
distintas até ele.

---

## 3. Resolução aridade-SELETIVA (o coração de §9 B)

O ruling 3 fixa a aridade da referência SINTATICAMENTE: `Foo` nu ⇒ 0, `Foo<X>` ⇒ 1. Os resolvers têm
de SELECIONAR o registo cuja `decl.type_params.len` iguala a aridade da referência, em vez de casar
por nome e depois rejeitar. Centraliza-se num núcleo aridade-ciente reusado pelos dois pontos de
entrada de `resolve_type`.

### 3.1 Núcleo aridade-ciente

Nova fn em `resolve.tks`, junto de `type_table_find` (`:297`):

```
/**
 * type_table_find_arity — the §9 B arity-SELECTIVE sibling of `type_table_find`: among the
 * registrations whose last segment (or canonical/qualified name) matches `name`, return the one whose
 * declared generic ARITY equals `arity` (`decl.type_params.len`). This is what lets `Foo`, `Foo<T>`
 * and `Foo<T,U>` coexist under one bare name and still resolve unambiguously — a bare reference asks
 * for arity 0, a `Foo<X>` reference for arity 1, and each gets its OWN declaration instead of the
 * first-by-name match `type_table_find` returns.
 *
 * A name with a SINGLE registration is returned only when its arity matches `arity`; a mismatch is a
 * located error (`no type named 'Foo' with N type-parameter(s)`), which preserves today's
 * "non-generic given type arguments" / "wrong number of type arguments" behaviour for the
 * single-declaration corpus (there is exactly one such reg, so the answer is identical). A stamped
 * generic-instance name (`Box__g__i64`, arity 0 by construction) is unaffected — it is unique and
 * matches at arity 0.
 *
 * @param table  the folded type table to search
 * @param name   the type name to find (bare or canonical)
 * @param arity  the reference's syntactic arity (0 for a bare `Foo`, N for `Foo<A1..An>`)
 * @return       the registration declared at exactly `arity`, or a located error when none matches
 * @since §9 B
 */
pub fn type_table_find_arity(table: TypeTable, name: str, arity: u64): parser::TypeDecl | error
```

Corpo: o mesmo walk de `tt_cands` / bucket de `type_table_find`, mas o predicado de aceitação ganha
`r.decl.type_params.len == arity`. Se nenhum casar a aridade mas ALGUM casar o nome, erro localizado
(nomeando a aridade pedida e as disponíveis). Reusa `qualify_eq` / `name_last_segment` idênticos.

### 3.2 `resolve_bare_type_reg` / `resolve_qualified_type_reg` ganham aridade

Os dois resolvers de referência-de-fonte (`resolve.tks:599` / `:676`), e o seu chamador
`resolve_type_reg` / `resolve_type_ref` (`:714`), ganham um parâmetro `arity: u64` threaded do ponto
de entrada. O predicado de match (`r.name == last && r.namespace == ref_ns`, `:605`; e o análogo
qualified `:687`) ganha `&& r.decl.type_params.len == arity`. Assinaturas:

```
/**
 * resolve_bare_type_reg — the R1/root bare-reference registration lookup, now ARITY-SELECTIVE (§9 B):
 * among same-name registrations in `ref_ns` (then root scope, then a stamped `__g__` instance) it
 * returns the one declared at exactly `arity`. A bare source reference `Foo` passes arity 0; a
 * `Foo<X>` reference passes 1 (through `resolve_generic_inst`). This is what makes `Foo` name the
 * arity-0 declaration ALWAYS (ruling 3) even when a `Foo<T>` shares the namespace.
 *
 * @param last    the referenced bare type name
 * @param table   the folded type table
 * @param ref_ns  the namespace of the code that wrote the reference
 * @param arity   the reference's syntactic generic arity
 * @return        the matching registration, `NotUserType`, or a fail-loud error
 * @since §9 B
 */
fn resolve_bare_type_reg(last: str, table: TypeTable, ref_ns: str, arity: u64): TypeReg | NotUserType | error
```

(idem `resolve_qualified_type_reg`, `resolve_type_reg`, `resolve_type_ref`, `resolve_root_scope_reg`,
`reject_or_unknown_bare_type` — todos ganham `arity` e propagam-no; o root-scope e a rejeição só
precisam do arity para a mensagem "declarada nas aridades {…}").

**Threading a partir dos pontos de entrada (`resolve_type`, `:1925-1928`):**
- `resolve_named(nt.path, table, ref_ns)` → passa `arity = 0` (é o ramo `nt.args.len == 0`).
- `resolve_generic_inst(nt, table, ref_ns)` → passa `arity = nt.args.len`; o check redundante
  `td.type_params.len != nt.args.len` (`:2280`) DESAPARECE (a seleção já garantiu a aridade) — ou
  mantém-se como assert-de-invariante. O check `td.type_params.len == 0` "não-genérico com args"
  (`:2277`) também é subsumido (nenhum reg de aridade-0 casaria `arity = args.len > 0`); a mensagem de
  erro migra para `type_table_find_arity` / `resolve_bare_type_reg`.

**`resolve_named` (`:1159`) ganha `arity`** e propaga-o ao `resolve_type_ref` (`:1172`). O ramo bare
(`nt.args.len == 0`, arity 0) passa a EXIGIR um reg de aridade-0. Se o único `Foo` visível for
genérico, é ERRO (ruling 3/7: bare `Foo` NUNCA empresta a aridade da anotação). Mensagem sugerida:
`` `Foo` names a generic type — write `Foo<…>` (or `self { … }` to construct the enclosing type) ``.

### 3.3 Os outros 46 call-sites de `type_table_find`

A regra: um call-site que passa um nome **já-resolvido/stampado** (um `Named.name` canónico ou uma
instância `__g__`) permanece em `type_table_find` (nome unívoco — a aridade não desambigua nada). Só
os sítios que resolvem uma referência de FONTE-BARE de utilizador migram para `type_table_find_arity`
com a aridade sintática. O implementador DEVE auditar os 46 (grep `type_table_find(`), mas o design
prevê que a esmagadora maioria são nomes canónicos (ex.: `expand_variant` `:1197`, os probes de mono)
e ficam intactos — a única mudança de comportamento é para nomes bare de utilizador que agora podem
ter >1 aridade, que hoje NÃO existem no corpus (secção 7). Regra de segurança: **para um nome com um
único reg, `type_table_find` e `type_table_find_arity(name, esse_arity)` devolvem o MESMO reg** — logo
migrar um call-site é inócuo no corpus atual e só passa a discriminar quando um conjunto de overload
existir.

---

## 4. Construção — o prefixo de tipo obrigatório + `self { … }` (rulings 4/5/6/7)

### 4.1 Parser — reconhecer `self { … }` (o único toque de parser)

Em `parse_primary` (`parse_expr.tks:445`), o ramo `SelfKw` passa a olhar para a frente: se o token
seguinte é `{` E `allow_struct` (fora de scrutinee de if/match — mesma gate que `Name { … }` em
`:482`), parseia uma construção-de-self em vez de devolver o `Var{self}` nu.

**Representação (menos invasiva, SEM mudança de AST nem de `.tkb`):** reusar `parse_struct_lit`
(`parse_expr.tks:125`) com um `type_path` SENTINELA = um `Path` de um único segmento cujo nome é a
palavra reservada `"self"`. Como `self` é reservada e NUNCA passa `is_name_at` (não pode ser nome de
tipo de utilizador — `parse_expr.tks:442-444`), um `StructLit.type_path == [self]` é um sentinela
inequívoco. **Nenhum campo novo em `StructLit`** (`ast.tks:223`), logo `tkb_read`/`tkb_write` e todos
os construtores de `StructLit` ficam byte-idênticos.

```
/**
 * A `self { … }` type-INITIALIZATION (§9 B, ruling 5) — the third role of `self`, on top of the value
 * receiver (`parse_expr.tks:445`) and the `self`-as-type annotation. Parsed only when a `{` follows
 * `self` AND struct literals are allowed (never in an if/match scrutinee, where the `{` opens the
 * block — mirroring `Name { … }` at :482). It reuses `parse_struct_lit` with a SENTINEL `type_path`
 * of the single reserved segment `self`, which no user type can spell, so no new AST field is needed
 * and the `.tkb` shape is unchanged; the checker (`type_struct_lit`) recognises the sentinel and
 * resolves it against the enclosing type (`env.owner_type`).
 */
```

Ramo concreto (após `parse_expr.tks:446`, antes do `return Var{self}`): se
`allow_struct && is_kind_at(tokens, pos + 1, LBrace)`, construir um `Path` de segmento `self` e
`return parse_struct_lit(tokens, pos + 1, self_path, teko::list::empty())`. `self { … }` NÃO aceita
type-args explícitos (`self<T>{…}` é rejeitado: a aridade de Self vem do tipo envolvente, não do
call-site) — o ramo `Lt` não é ativado para `SelfKw`.

`allow_struct` tem de chegar a este ponto: `parse_primary` já o recebe (é o mesmo que gateia `:471`,
`:482`). Confirmar que a assinatura de `parse_primary` o expõe ao ramo `SelfKw` (hoje o ramo está
ACIMA da secção `is_name_at`; o `allow_struct` já está em escopo na fn).

### 4.2 Checker — `type_struct_lit` reescrito nos passos 1/3/4

Reestruturação do topo de `type_struct_lit` (`typer.tks:2943`). A ORDEM passa a ser decidida pelo
PREFIXO:

```
/**
 * type_struct_lit — type a construction literal, whose prefix is ALWAYS an explicit type (§9 B,
 * ruling 4): `Foo { … }` (arity 0), `Foo<i64> { … }` (name + explicit type-args) or `self { … }`
 * (the enclosing type / Self, inside a static OR instance method). There is NO nameless `{ … }`
 * construction — a bare `{` in expression position is always a block, which is what dissolves the
 * `{}`-vs-block collision. The prefix decides the arity: a bare `Foo` resolves the ARITY-0
 * declaration always (never borrowing type-args from `expected`, ruling 7); `Foo<A..>` resolves the
 * instance at that arity; `self` resolves `env.owner_type` (its own arity, phantom-stamped when the
 * owner is generic — ruling 6's explicit replacement for the deleted self-construct inference).
 *
 * @param sl        the parsed construction literal (`type_path` = the sentinel `self` for `self {…}`)
 * @param expected  the flow-target type — used ONLY for a field value's nested expectation, NEVER to
 *                  retarget a bare generic prefix (ruling 7)
 * @param env       the typing environment (its `owner_type` resolves `self`)
 * @param table     the collected type table
 * @return          the typed `TStructInit`, or a located error
 * @since §9 B (rewrites the W4a/S4/W9.4 construction path)
 */
fn type_struct_lit(sl: parser::StructLit, expected: Type, env: Env, table: TypeTable): TExpr | error
```

Passos concretos:

1. **Ramo `self` (novo, no TOPO, antes do `resolve_named` de `:2944`):** se
   `sl.type_path.segments.len == 1 && sl.type_path.segments[0].name == "self"`:
   - `env.owner_type == ""` ⇒ erro `` `self { … }` is only valid inside a method (no enclosing type) ``.
   - `name = env.owner_type` (canónico do tipo envolvente). `decl = type_table_find(table, name, "")`.
   - Se `decl.type_params.len > 0` (owner GENÉRICO): `name = generic_inst_name(base, owner-params-as-Named)`
     — o nome-FANTASMA `Base__g__<params>` (a computação que `phantom_self_inst_name` fazia; ver §5),
     `is_self_construct = true`. A `decl` mantém-se o template abstrato (campos tipam contra `T`); o
     passo-mono remapeia o fantasma → instância concreta (`monomorph.tks:1258`).
   - Segue para o gate-de-classe/campos (passo 5 original) com `is_self_construct = true` (que já
     bypassa o gate `owner_type != name` em `:3034`).
   - `self { … }` é legal em método static E de instância porque `env.owner_type` está setado em ambos
     (`with_owner`, `scope.tks:134`).
2. **Ramo `Foo<A..> { … }` (args explícitos):** se `sl.type_args.len > 0`, resolver via
   `resolve_generic_inst`-equivalente (a lógica de `:2977-2986` já o faz: `NamedType{path; args}` →
   `resolve_type` → instância concreta `Foo__g__…`). `decl` obtém-se por
   `type_table_find_arity(table, base, sl.type_args.len)` (o template) e `name = ginst`. A verificação
   de concordância com `expected` (`:2982`) MANTÉM-SE (se a anotação nomeia outra instância da mesma
   base, erro). Aridade errada (`Foo<i64>` para `Foo<T,U>`) ⇒ erro de `resolve_generic_inst` / da
   seleção-por-aridade (ruling 3, sem aplicação parcial).
3. **Ramo `Foo { … }` bare (aridade-0):** `nt = resolve_named(sl.type_path, table, env.cur_ns, 0)`
   (arity 0, §3.2), `decl = type_table_find_arity(table, name, 0)`. Se só existir um `Foo` genérico,
   ERRO (ruling 7) — **a retarget-por-anotação de `:2987-3018` é APAGADA por completo**. O bloco
   inteiro `if decl.type_params.len > 0 { … else { annotation-retarget } }` desaparece; o único
   caminho para construir uma instância genérica passa a ser o ramo 2 (`Foo<A..>{}`) ou o ramo 1
   (`self{}` dentro do template).

O RESTO de `type_struct_lit` (`error{message}` builtin `:2946-2958`, gate-de-classe `:3019-3042`,
loop de campos `:3043-3103`, `TStructInit` final `:3104`) fica INTACTO. O caminho fantasma-de-instância
para genéricos que constroem OUTRO genérico a partir do seu próprio `T` (o bloco
`name_is_phantom_instance`, `:3003-3016`) permanece SÓ dentro do ramo 2 (args explícitos) onde ainda
é alcançável; confirmar que nenhum teste dependia dele via o caminho de anotação apagado (secção 7).

### 4.3 Sweep dos sítios de self-construct (ruling 6)

Dois sítios de produto migram de `Nome { … }`-é-Self para `self { … }`:
- `src/collections/list.tks:26` — `pub static fn make(): List<T> { List { items = teko::list::empty() } }`
  → `{ self { items = teko::list::empty() } }`.
- `src/collections/map.tks:44` — `Map { keys = …; hashes = …; vals = … }`
  → `self { keys = …; hashes = …; vals = … }`.

Ambos estão dentro de `static fn make()` de um tipo genérico (`List<T>` / `Map<V>`), logo
`env.owner_type` está setado e o ramo `self` (§4.2 passo 1) produz o mesmo fantasma
(`List__g__T` / `Map__g__V`) que a inferência apagada produzia. **Estes são os ÚNICOS 2 sítios cuja
mudança altera bytes emitidos** (o nome do construtor muda de spelling na fonte, mas o `TStructInit`
final e o fantasma são idênticos — ver risco de fixpoint §7). Auditar por
`grep -n "^\s*[A-Z][A-Za-z0-9_]*\s*{" src/collections/*.tks` e por qualquer outro self-construct que o
scan revele; REPORTAR para cima se aparecer um 3º sítio (não inventar issue).

---

## 5. Apagar a inferência-de-self — e o que TEM de sobreviver (ruling 6)

`is_self_generic_construct` (`resolve.tks:2187`) e a sua invocação (`typer.tks:2992`) APAGAM-SE: a
deteção "bare `Foo{}` onde Foo==owner é self" deixa de existir; o `self { … }` explícito toma o lugar.

`phantom_self_inst_name` (`resolve.tks:2206`) tem um **SEGUNDO consumidor** —
`instance_method_subst_l5` (`monomorph.tks:1258`) — que constrói o remap `fantasma → instância` para o
mono pass. Esse remap TEM de sobreviver (o `self { … }` num template genérico ainda tag-eia o init com
o nome-fantasma). **Recomendação law-first:** NÃO apagar a COMPUTAÇÃO do spelling-fantasma; apagar só a
INFERÊNCIA. Concretamente:
- Apagar `is_self_generic_construct` (a inferência) e o ramo `:2992-2994` que o usa.
- MANTER a produção do spelling-fantasma. Como o ruling nomeia `phantom_self_inst_name` para deleção,
  o desenho recomendado é DOBRAR o seu corpo (`generic_inst_name(base, owner-params-as-Named)`) tanto
  no ramo `self` de `type_struct_lit` (§4.2) como em `instance_method_subst_l5` (`monomorph.tks:1258`),
  removendo o helper nomeado. As DUAS produções TÊM de ficar byte-idênticas (mesmo `generic_inst_name`,
  mesma ordem de params) senão o remap do mono falha o match. **Alternativa mais segura (recomendo):**
  RENOMEAR `phantom_self_inst_name` para um helper interno neutro (ex. `self_inst_spelling`) e reusá-lo
  em ambos os sítios — cumpre o espírito do ruling (a *inferência* morre) sem duplicar uma string
  cuja divergência quebraria o mono silenciosamente. Isto é uma **tensão-de-lei menor** (o ruling
  lista `phantom_self_inst_name` para deleção, mas tem um consumidor não-self-construct) — ver §9,
  resolução recomendada, NÃO bloqueante.

---

## 6. `.tkb` + impacto de serialização

Nenhuma mudança de formato `.tkb` (§1.5): `type_params` já é serializado, a aridade é `type_params.len`.
Confirmar que `RTypeDecl` (`tkb_read.tks:827`) e o writer gémeo (`tkb_write.tks`) continuam a ler/gravar
`type_params` sem tocar — são byte-idênticos. A `TypeTable` NÃO persiste a chave `(ns,name,arity)`; é
recomputada em cada build a partir das decls. Logo um `.tkb` produzido antes de §9 B lê idêntico depois
(a aridade sempre lá esteve).

---

## 7. Segurança de FIXPOINT (o argumento + o scan de risco)

**Argumento (aditivo, exceto o sweep dos 2 sítios).**
- **Re-key do registo (§2):** só RELAXA. Um par `Foo`/`Foo<T>` do MESMO `(ns,name)` colidiria HOJE
  (mesma chave sem-aridade) ⇒ NÃO compila hoje ⇒ **nenhum tal par existe no corpus**. Para todo nome
  de tipo atual há uma só aridade, logo `duplicates_of_reg` com a aridade extra dá o MESMO veredicto
  (0 duplicados) que hoje. Byte-idêntico.
- **Resolução aridade-seletiva (§3):** para um nome com um único reg,
  `type_table_find_arity(name, esse_arity)` devolve o MESMO reg que `type_table_find(name)` e
  `resolve_bare_type_reg`-com-arity casa exatamente onde o bare casava. A seleção só passa a
  DISCRIMINAR quando >1 aridade partilha o nome — o que não ocorre no corpus. Byte-idêntico.
- **Apagar inferência-self + `self {}` (§4/§5):** para os 2 sítios varridos, o CAMINHO muda (deteção
  → sentinela explícito) mas o RESULTADO — a `decl` template, o nome-fantasma, a `TStructInit` — é o
  MESMO objeto. A ÚNICA diferença observável é o texto-fonte (`List {` → `self {`), que não é
  serializado; a árvore tipada é idêntica. **PORÉM** o self-init passava antes pelo ramo
  `is_self_generic_construct`; agora passa pelo ramo `self`. O implementador DEVE confirmar por diff da
  TAST/bytes que os dois ramos produzem `name` e `type` byte-idênticos para `List__g__T`/`Map__g__V`.
  Se produzirem, o compilador auto-recompila byte-a-byte SEM reseed dos próprios internals; o reseed é
  exigido só porque `list.tks`/`map.tks` fazem PARTE do corpus emitido e a sua fonte mudou.

**Risco-fixpoint 1 — o sweep dos 2 sítios muda bytes emitidos ⇒ RESEED obrigatório.** Diferente de
§9 A (puramente inerte), §9 B toca fonte de PRODUTO (`list.tks:26`, `map.tks:44`). O output do
compilador para esses módulos muda (o nó de construção deriva de `self`), logo `bin-a != seed-antigo`
por design; o fixpoint prova `bin-a == bin-b` (re-build com `bin-a`). Reseed no fim (§8).

**Risco-fixpoint 2 — bare-generic-ref fora de construção.** A nova regra "bare `Foo` = aridade-0
sempre" pode quebrar qualquer referência de TIPO bare a um genérico que hoje resolva por nome (ex. um
`Foo` sem `<>` num param/return/var/field que emprestava a aridade). O scan: procurar referências bare
a `List`/`Map`/qualquer `type X<…>` sem `<>`. As decls de `List`/`Map` usam `List<T>`/`Map<V>`
explícitos nos returns (`list.tks:26`, `map.tks:43`), logo à partida OK. O implementador DEVE confirmar
por build que nenhum bare-generic-ref sobrevivia no corpus; se aparecer, é uma migração-de-superfície
adjacente — REPORTAR para cima (§9 B não a resolve por invenção de issue).

**Verificação obrigatória do implementador (não pude correr build):** após implementar, correr o
fixpoint gate; qualquer divergência além dos 2 sítios varridos aponta um caminho de resolução que
mudou onde não devia (regressão), não um efeito esperado.

---

## 8. Fixtures de regressão

Layout (padrão dos irmãos): projeto ACEITAR `examples/regressions/<nome>/` (`.tkp`/`.tkr`/`main.tks`/
`src/`), com `Then stdout pattern = "…"` codificando em aritmética qual construção correu; REJEITAR
dobra em `examples/regressions/diagnostics/` (`src/<caso>/case.tks` + Scenario com
`Then diagnostic = "<substring>"` contra o build-que-falha partilhado). Descoberta por diretório.

### 8.1 ACEITAR — `examples/regressions/type_overload/` (novo)

- **A1 — coexistência por aridade.** `type Pair = struct { a: i64 }`, `type Pair<T> = struct { x: T }`,
  `type Pair<T,U> = struct { x: T; y: U }`. Construir `Pair { a = 7 }`, `Pair<i64> { x = 3 }`,
  `Pair<i64,i64> { x = 4; y = 5 }`; somar campos → um único número que só as três decls certas
  produzem. Prova ruling 1/3 (três aridades coexistem, uso sintático seleciona).
- **A2 — `self { … }` static.** Um tipo genérico com `static fn make()` que usa `self { … }` (o mesmo
  padrão que `List<T>::make`); instanciar em duas larguras (`i64`, `str`) e ler de volta. Prova
  ruling 4/5 e o phantom-stamp por largura.
- **A3 — `self { … }` de instância.** Um método de instância que constrói `self { … }` (p.ex. um
  `with_x` funcional que devolve uma cópia); prova que `self {}` é legal fora de um static.
- **A4 — `Foo { }` aridade-0 ao lado de `Foo<T>`.** Com `Pair`/`Pair<T>` de A1, confirmar que
  `Pair { a = 1 }` NUNCA constrói `Pair<T>` mesmo sob uma anotação `: Pair<i64>` no alvo (ruling 7 —
  bare não empresta aridade). O valor lido distingue.

### 8.2 REJEITAR — dobrar em `examples/regressions/diagnostics/`

- **R1 — mesma aridade duplicada.** `type Foo<T> = struct { x: T }` + `type Foo<U> = enum { A }` no
  mesmo namespace → `Then diagnostic = "duplicate type 'Foo'"` (ruling 2: constraint/nome-de-param e
  cross-kind não desambiguam; erro na DEFINIÇÃO).
- **R2 — aplicação parcial.** `type Foo<T,U> = struct { x: T; y: U }`; usar `Foo<i64>` (uma só arg) →
  `Then diagnostic = "type arguments"` (aridade errada; sem aplicação parcial, ruling 3).
- **R3 — bare de um genérico.** Só `type Foo<T> = struct { x: T }`; escrever `Foo { x = 1 }` (bare,
  aridade-0) → `Then diagnostic = "generic"` (bare `Foo` é aridade-0; não há decl aridade-0). Prova
  ruling 7.
- **R4 — `self { … }` fora de método.** `self { … }` numa fn livre → `Then diagnostic = "self"` (só
  válido dentro de um método). Prova ruling 5.
- **R5 — construção anónima proibida.** `{ x = 1 }` em posição de expressão como tentativa de
  construção → parseia como bloco e falha o checker (não é uma construção). `Then diagnostic` afirma
  que um `{` é um bloco, não uma construção. Prova ruling 4 (a resolução `{}`-vs-block-B).

---

## 9. Sequência de crumbs (ordenada; cada uma gate-ável isoladamente)

Cada crumb compila e passa o gate rápido; os pontos de RITUAL (gate completo) estão marcados. §9 B é
INDEPENDENTE de §9 A/§9 C (não partilha mecanismo). O seed atual suporta tudo o que as crumbs usam
(nada depende de sobrecarga-de-função).

1. **Re-key do registo.** Adicionar a aridade à igualdade em `duplicates_of_reg` (`collect.tks:2043`) +
   aridade na mensagem. Teste-de-checker: `Foo`/`Foo<T>` no mesmo ns → 0 duplicados; `Foo<T>`/`Foo<U>`
   → 1 duplicado. — *aditivo: no corpus atual todo nome tem uma aridade só ⇒ veredicto idêntico.*
   **RITUAL: gate completo.**
2. **Núcleo aridade-ciente.** `type_table_find_arity` (`resolve.tks`, junto de `:297`); `arity` em
   `resolve_bare_type_reg`/`resolve_qualified_type_reg`/`resolve_type_reg`/`resolve_type_ref`/
   `resolve_root_scope_reg`/`reject_or_unknown_bare_type`; threading a partir de `resolve_type`
   (`:1925`, arity 0 / `nt.args.len`) e `resolve_named` (`:1159`). Remover os checks redundantes de
   aridade em `resolve_generic_inst` (`:2277-2282`) → mensagens migram para o núcleo. Teste: um nome
   com um só reg resolve idêntico; um par de aridades resolve cada forma à sua decl. — *byte-idêntico
   para nomes de aridade-única (todo o corpus).* **RITUAL: gate completo** (toca o resolver central).
3. **Parser `self { … }`.** Ramo `SelfKw` + `{` em `parse_primary` (`parse_expr.tks:445`) →
   `parse_struct_lit` com `type_path` sentinela `[self]`. Sem mudança de AST/`.tkb`. Teste-de-parser:
   `self { a = 1 }` parseia StructLit(self); `self` nu continua `Var{self}`; `if self { … }` (scrutinee)
   continua a abrir bloco. — *inerte: nenhum `self {…}` no corpus ainda.*
4. **Checker — ramo `self` + ramo `Foo<A..>` + apagar retarget-por-anotação.** Reescrever o topo de
   `type_struct_lit` (§4.2): ramo `self` (resolve `env.owner_type`, phantom para owner genérico); ramo
   `Foo<A..>` via `type_table_find_arity`; ramo bare aridade-0; APAGAR o bloco `:2987-3018` de
   retarget-por-anotação. Apagar `is_self_generic_construct` (`resolve.tks:2187`) e a sua invocação.
   Tratar o spelling-fantasma (§5: renomear/dobrar). — *ainda inerte (nenhum `self{}`/overload no
   corpus até o sweep).* **RITUAL: gate completo** (é a crumb que reescreve a construção).
5. **Sweep dos 2 sítios de self-construct.** `list.tks:26` e `map.tks:44` → `self { … }`. Confirmar
   `instance_method_subst_l5` (`monomorph.tks:1258`) continua a produzir o remap correto (o
   spelling-fantasma de §5). Primeira crumb que muda bytes emitidos de PRODUTO. **RITUAL: gate
   completo** (a rede é o fixpoint da crumb 8; verificar TAST de `List__g__…`/`Map__g__…`).
6. **Fixtures ACEITAR** (`examples/regressions/type_overload/`, A1–A4). Primeiro overload-de-tipo REAL
   + `self{}` ponta-a-ponta. **RITUAL.**
7. **Fixtures REJEITAR** (R1–R5 em `examples/regressions/diagnostics/`). **RITUAL.**
8. **Reseed + PROVENANCE** (crumb final, §10) — obrigatório (crumb 5 mudou fonte de produto).

---

## 10. Ritual de reseed + PROVENANCE (crumb final)

Só depois de todas as crumbs verdes e do gate completo passar:

1. `cc -std=c2x -w -O2 -I src/runtime -I src/assert bootstrap/teko.c src/runtime/teko_rt.c
   src/assert/assert.c -lm -o gen0`
2. `TEKO_BACKEND=c ./gen0 build . --no-verify --release` → `bin-a`.
3. Re-build com `bin-a` como seed → `bin-b`. **Fixpoint: `bin-a == bin-b`** byte-a-byte
   (`scripts/fixpoint_gate.sh`). §9 B é aditivo salvo o sweep dos 2 sítios (crumb 5), que muda o output
   dos módulos `collections`; o fixpoint fecha em `bin-a == bin-b` (a mudança é determinística).
4. Harvest: `bootstrap/teko.c` (novo seed) + atualizar `bootstrap/PROVENANCE`.
5. NUNCA correr `teko test .` (fuga de memória). Gate por `--no-verify` + `scripts/*.sh`.

---

## 11. Riscos + tensões de lei (com resolução recomendada)

- **R-fixpoint sweep (crumb 5).** O único ponto que muda bytes de produto. Neutralizado por reseed
  (§10) + verificação de TAST idêntica dos dois ramos self (§7). Recomendação FIRME.
- **`phantom_self_inst_name` tem 2º consumidor (`monomorph.tks:1258`).** Tensão-de-lei MENOR: o ruling
  6 lista-o para deleção, mas o mono pass precisa do spelling-fantasma para o remap. Resolução
  recomendada (§5): renomear para um helper neutro (`self_inst_spelling`) reusado nos DOIS sítios — a
  *inferência* (`is_self_generic_construct`) morre como o ruling manda; a *string* fica única (evita
  divergência silenciosa que quebraria o mono). NÃO bloqueante; law-first (Doc "fail-loud / uma-fonte-
  de-verdade" favorece um helper partilhado sobre duas cópias). Se o dono insistir na deleção literal,
  o fallback é dobrar `generic_inst_name(base, params)` verbatim em ambos os sítios (frágil, não
  recomendado).
- **Bare-generic-ref fora de construção (risco-fixpoint 2, §7).** A regra "bare = aridade-0" pode
  rejeitar uma referência de tipo bare-genérica pré-existente. Scan indica que `List`/`Map` usam `<>`
  explícito; o implementador confirma por build. Se aparecer, é migração-de-superfície adjacente —
  REPORTAR para cima, não resolver por invenção de issue.
- **46 call-sites de `type_table_find`.** A maioria passa nomes canónicos/stampados (unívocos) e fica
  intacta; só os probes de referência-bare migram para `type_table_find_arity`. Resolução: para
  nome-de-aridade-única os dois são equivalentes (§3.3), logo migrar é inócuo hoje. O implementador
  audita a lista; sem tensão de lei.
- **Sem tensão de lei GENUÍNA bloqueante.** Todos os rulings selados encaixam num desenho law-first
  coerente; a única fricção (o 2º consumidor de `phantom_self_inst_name`) resolve-se law-first sem
  reabrir o ruling. Nenhum HALT necessário.

---

## 12. Perguntas ao dono/integrador (não-bloqueantes; adiantei tudo o que não depende delas)

1. **Spelling-fantasma:** confirmar a resolução recomendada de §5/§11 (renomear
   `phantom_self_inst_name` → helper neutro partilhado com o mono, em vez de deleção literal + dobra).
   Adiantei ambos os caminhos; recomendo o helper partilhado. Não bloqueia as crumbs 1–3.
2. **Mensagem exata do erro bare-de-genérico** (ruling 7, R3) — adiantei
   `` `Foo` names a generic type — write `Foo<…>` (or `self { … }`) ``; o dono pode fixar a grafia.
   Afinação local, sem impacto nas outras crumbs.
3. **Cross-kind na mensagem de duplicado** (ruling 2) — struct+enum mesma-aridade colidem; a mensagem
   diz só `duplicate type`. Se o dono quiser nomear os dois kinds, é afinação local de
   `duplicates_of_reg`. Recomendo a mensagem uniforme (mais simples, passa o ruling).
