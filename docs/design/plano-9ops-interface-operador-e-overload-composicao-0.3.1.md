# Plano — 9-ops: interface-que-obriga-operador (§6/§9.4) + o ramo `Overload` da fusão-de-composição (§9A)

> **Versão:** v1 (2026-08-14). **Status:** DESIGN-AHEAD (architect). Read-only no código-produto —
> NENHUM `.tks` de produto editado, NENHUM build, NENHUM reseed, NENHUM `teko test .` (fuga de memória
> do `monomorph` — crasha o container; nunca correr). Este documento É o artefacto; o único commit desta
> crumb é ele próprio.
> **Branch:** `fix/retirement` (drena sequencial, SEM PRs). Worktree isolado (a checkout principal pode
> ter implementers de reseed — não se toca).
> **Fonte de lei (SELADA — desenha-se à volta, não se re-abre):**
> `docs/design/mudancas-superficie-0.3.1.md` §9.4 (a interface-que-obriga-operador + a regra de fusão
> AST-igual GERAL, linhas 508-607). Rulings de operador SELADOS em
> `docs/design/plano-secao9-operadores-e-value-type-methods.md` §0. Rulings de sobrecarga SELADOS em
> `docs/design/plano-secao9A-method-overload.md` §0.
> **Consolidação (não recriação):** este plano CONSOLIDA E DESTRAVA os crumbs marcados
> **[BLOQUEADO em §9-ops]** / **[BLOQUEADO em §9A]** de `docs/design/plano-onda-traits.md` (§6 inteiro,
> §4.4-ramo-`Overload`, e a crumb 10-11 do §8). Aquelas dependências ATERRARAM (ver RECON §1); aqui
> convertem-se num crumb-plan executável. Os outros três docs permanecem a fonte dos rulings — não se
> reabrem; estende-se.
> **Lei permanente:** Teko-only (.tks), W15 + Javadoc-completo em TODA declaração, law-first, reseed
> disciplinado (`cc -std=c2x`, `--no-verify`), fixpoint byte-idêntico.

---

## 1. RECON — o que JÁ aterrou (file:line, verificado no worktree off `origin/fix/retirement`)

O reseed `072823fb` ("§9 operators + value-type methods") e a onda-traits JÁ estão na árvore. **§9-ops e
§9A NÃO são mais bloqueios — a maquinaria-base existe e está reseedada.** O que a onda-traits chamava
"bloqueado" é agora um trabalho de EXTENSÃO sobre peças vivas.

### 1.1 Operadores em value-type (prim/enum/flags) — COMPLETO
- `dunder_of_binop` (`src/checker/typer.tks:758`), `value_op_owner` (`:810`), `owner_has_matching_operator`
  (`:873`), `dispatch_binop_operator` (`:952`), `build_operator_call` (`:912`), `operand_reduced_to_base`
  (`:836`), `NoOperator` (`:745`) — o lookup de dunder gated e o dispatch dos dois lados (ruling 8) VIVOS.
- Inserção em `type_binary` (`typer.tks:1034-1046`) — o ramo de operador corre após o gate de bignum,
  fallback ao primitivo por `operand_reduced_to_base`.
- Cast E9 (`value_type_base` `typer.tks:3729`; ramo EXTRACT/CONSTRUCT em `type_cast`) + lowering
  `NewtypeBody`→base no codegen (`emit_newtype_typedef` `codegen.tks:10309`) e LIR
  (`newtype_carrier_entry` `lower.tks:16498`) — VIVOS.
- `check_operator_invariants` (`typer.tks:8677`) + `check_comparator_counterparts` (`:8688`) —
  validação de DEFINIÇÃO (dunder, `self`, retorno, contrapartida) — VIVA, chamada de
  `type_struct_methods` (`typer.tks:8737`).
- Parser: `value_member_is_operator` (`parse_decl.tks:701`), `parse_operator_member` (`:722`, EXIGE
  corpo `{…}` @753), `parse_value_type_members` (`:773`) — VIVOS. `Function.is_operator`
  (`ast.tks`), `NewtypeBody` (`ast.tks:685`), codec `.tkb` (`tkb_write.tks:461`/`tkb_read.tks:776`) —
  VIVOS.

### 1.2 Sobrecarga de método/função §9A (`select_overload`) — COMPLETO
- `lookup_call_candidates` (`src/checker/scope.tks:485`), `env_is_overloaded` (`:555`), `select_overload`
  (`typer.tks:3097`), `overload_suffix_of` (`resolve.tks:2485`), `overload_suffix` na TAST + mangling nos
  dois backends — VIVOS. Um operador que baixa por `build_operator_call`→`type_call` já passa por
  `select_overload`, logo **operadores sobrecarregados NUM tipo (operandos mistos, ruling 6) já resolvem
  no call-site**. A resolução-de-operador de §9A, para um único dono, ESTÁ FEITA.

### 1.3 A fusão-de-composição AST-igual + o ramo `Overload` — PARCIAL (a metade que falta)
- `merge.tks` COMPLETO: `functions_ast_equal` (`:607`), `fields_ast_equal` (`:643`), `consts_ast_equal`
  (`:657`), `MergeDisposition = Absorb|Overload|Conflict` (`:31`), `merge_named_members` (`:678`) — a
  rotina PARTILHADA existe e devolve os três dispositions.
- **Consumidor incompleto:** `fold_trait_methods` (`collect.tks:1989-1993`) trata `Overload` como **ERRO
  DURO**: `"method overloading across composition is not yet resolved (§9A)"`. É o exato
  **[BLOQUEADO em §9A]** — a metade `Absorb`/`Conflict` está ligada, o ramo `Overload` está tapado.
- `effective_interface_methods` (`collect.tks:1335`) ainda **concatena sem fundir** (não chama
  `merge_named_members`) — a fusão interface∘interface por AST-igual (§9.4 regra GERAL) está por ligar.

### 1.4 A camada INTERFACE-OPERADOR (§6/§9.4) — AUSENTE (o grosso do trabalho)
Nada da capability-via-interface existe. Confirmado por grep no worktree:
- **Operador em struct/class:** `parse_operator_member` é chamado SÓ por `parse_value_type_members`
  (`parse_decl.tks:780`). `parse_struct_body` (`:1283`→`parse_fields`), `parse_class_fields` (`:939`) e
  `parse_interface_body` (`:1120`) **NÃO reconhecem `operator`**. Logo o exemplo SELADO da lei
  (`type Point = struct IEq & NeByEq { operator __eq(...) {…} }`, §9.4 linha 588) **não parseia hoje**.
- **Operador (bodyless) em interface:** idem — `parse_operator_member` exige corpo; interface não
  reconhece `operator` de todo. Logo `type IEq = interface { operator __eq(...): bool }` **não parseia**.
- **Extração:** `value_type_body_methods` (`typer.tks:787`) devolve `[]` para `StructBody`/`ClassBody`
  (só cobre Newtype/Enum/Flags) — mesmo que um operador de struct parseasse, não seria validado nem
  despachado.
- **Dispatch em struct/genérico:** `value_op_owner` (`typer.tks:810`) devolve "" para `StructBody`,
  `ClassBody` e para um **type-param** `T` — logo `a == b` com `a: Point` (struct) ou `a: T` (`T: IEq`)
  **não dispara** o lookup de operador; cai no primitivo e erra.
- **Corpus:** não há `IEq`/`IOrd`/`NeByEq`/`GtByLt` em `src/` (grep: só doc-comments/fixtures). `sort`
  é concreto (`sort_str`/`sort_i64`, `src/sort/sort.tks:81/157`), não há `sort<T: IOrd>`. `Map` continua
  `str`-keyed (a lei diz que foi por isto que desistiu).

### 1.5 Máquina de genéricos-com-métodos (#254) — DONE, é a base de reuso
`docs/design/drain-254-L4L5-class-factories.md` está `[HISTÓRICO]` (executado). A **dispatch de método
de interface sobre um type-param `T`** já vive: `typer.tks:2197-2222` resolve `x.metodo()` com `x: T`
via `iface_methods_by_name`/`constraint_interfaces`, e o `monomorph.tks` estampa a instância
(`instance_method_subst` `monomorph.tks:927`, `register_instance_methods` `collect.tks:206`). **O
dispatch genérico de operador (§6) REUTILIZA esta via** — não a reinventa (ver §3 crumb 4).

**Conclusão do RECON:** o "9-ops" restante é (i) DESTAPAR o ramo `Overload` (uma peça pequena, a base
está pronta), e (ii) CONSTRUIR a camada interface-operador — que exige **operadores em struct/class**,
**operadores bodyless em interface**, **dispatch de operador sobre struct e sobre `T` constrangido**, a
**contrapartida obrigatória ao nível do contrato**, e o **corpus** `IEq`/`IOrd` + `NeByEq`/`GtByLt`.

---

## 2. As lacunas (o âmbito exato do 9-ops)

| # | Lacuna | Estado hoje | Crumb |
|---|---|---|---|
| G1 | `operator` como membro de **struct/class** (com corpo) | não parseia; `value_type_body_methods`=[] | 1, 2 |
| G2 | `operator` **bodyless** como membro de **interface** | não parseia | 1, 2 |
| G3 | dispatch de `==`/`<`/… sobre operando **struct** | `value_op_owner`="" p/ StructBody | 3 |
| G4 | dispatch de `==`/`<`/… sobre **type-param** `T: IEq` (vtable/monomorph) | ausente; reusa #254 | 4 |
| G5 | contrapartida OBRIGATÓRIA ao nível do **contrato de interface** + joint (`NeByEq`) | `check_comparator_counterparts` é value-type-only | 2, 5 |
| G6 | ramo **`Overload`** da fusão (across-composition) via `select_overload` | erro duro `collect.tks:1992` | 5 |
| G7 | corpus `IEq`/`IOrd` + `NeByEq`/`GtByLt`/`LeByLt`/`GeByLt` (stdlib) | ausente | 6 |
| G8 | fusão interface∘interface por `merge_named_members` (§9.4 regra geral) | concatena sem fundir `collect.tks:1335` | 5 |

---

## 3. Crumb-plan ordenado (cada uma gate-ável isoladamente; RITUAL marcado)

Sequenciada por dependência de SEED: parser → extração/validação → dispatch concreto → dispatch genérico
→ fusão/overload → corpus → fixtures → reseed. **Argumento de fixpoint:** o corpus do compilador não
declara operadores em struct/class/interface, nem constraints de operador — logo cada crumk é INERTE
(árvores idênticas) até o corpus §6 (crumb 6) entrar, e esse é corpus/stdlib, não maquinaria — não
afeta o auto-fixpoint do compilador. `bin-a == bin-b` fecha na crumb 5.

### Crumb 1 — Parser: `operator` em struct/class (corpo) e em interface (bodyless)
**Toca:** `src/parser/parse_decl.tks`. **RECON-alvo:** `parse_fields`/`parse_member_fn_or_accessor`
(`:663`, o dispatcher de membro de struct), `parse_class_fields` (`:939`), `parse_interface_body`
(`:1120`).
- Generalizar `parse_operator_member` (`:722`) para aceitar um flag `allow_bodyless: bool` (paridade com
  `parse_member_fn_or_accessor(…, allow_bodyless)` `:663`): interface passa `true` (assinatura termina em
  `): T` sem `{`), struct/class passa `false` (exige corpo, ruling operador-é-comportamento).
- No dispatcher de membro de struct/class/interface, ANTES do ramo `fn`/`get`/`set`, testar
  `value_member_is_operator(tokens, p)` (`:701`, já contextual) e rotear para `parse_operator_member`. O
  `Function` resultante entra em `sb.methods`/`cb.methods`/`ib.methods` com `is_operator = true`.
- **Aperto NÃO-introduzido:** `operator` continua contextual (precedente `trait`); zero token novo.
**Fixpoint:** nenhum struct/class/interface do corpus usa `operator` → árvores idênticas.
**RITUAL: gate completo** (toca o parser + a forma de três body-nodes centrais).

### Crumb 2 — Extração + validação de definição para struct/class/interface
**Toca:** `src/checker/typer.tks`, `src/checker/collect.tks`.
- `value_type_body_methods` (`typer.tks:787`): estender o `match` para `StructBody as sb => sb.methods`,
  `ClassBody as cb => cb.methods`, `InterfaceBody as ib => ib.methods`. (Ou introduzir
  `type_body_operators(body): []Function` dedicado — ver **Opção M-A** §4.) Assim
  `check_operator_invariants` (já chamado de `type_struct_methods` `:8737`) passa a validar operadores de
  struct/class também.
- `check_operator_invariants` (`:8677`): confirmar que o ramo `[]`/ruling-4 (`__index` só em indexado)
  e o "no fields/statics" (ruling 10) NÃO se aplicam a struct/class (um struct TEM campos) — separar o
  invariante "value-type: sem campos" (fica value-type-only) do invariante "operador bem-formado"
  (dunder/self/retorno/contrapartida — GERAL). Ver **Opção M-D** §4 para a contrapartida.
- **Interface (bodyless):** `check_one_interface` (`collect.tks:1539`) trata um `operator` bodyless como
  um método de contrato requerido — um conformer tem de o FORNECER (por escrita própria ou por trait
  achatado). Confirmar que a comparação de assinatura (`method_sig_matches`) casa um operador
  (`is_operator=true` dos dois lados, params por posição, `self`→dono). Ver **Opção M-D**.
**Fixpoint:** inerte (nenhuma interface do corpus lista operador; nenhum struct define operador).

### Crumb 3 — Dispatch de operador sobre operando struct/class
**Toca:** `src/checker/typer.tks`.
- `value_op_owner` (`:810`): o dono do operador passa a incluir um `StructBody`/`ClassBody` **que declara
  ≥1 `operator`** (não todo struct — só os que carregam operador, senão `+` em structs comuns dispararia
  um lookup inútil; ainda cai em `NoOperator`, mas evita-se o custo). Ver **Opção M-A** §4 para as três
  formas de exprimir isto.
- Nada mais muda: `type_binary` (`:1034`) já chama `value_op_owner`→`dispatch_binop_operator`→
  `owner_has_matching_operator`→`build_operator_call`. Um `Point == Point` passa a baixar para
  `Point::__eq(a, b)` e resolve por `type_call`/`select_overload` como qualquer método estático.
- Fallback: um struct sem operador casado → `NoOperator`; MAS `operand_reduced_to_base` (`:836`) só
  reduz value-types — para struct devolve o operando intacto e o operador primitivo erra (correto: não
  há `+` primitivo entre structs). Confirmar a mensagem.
**Fixpoint:** inerte (nenhum struct do corpus declara operador → `value_op_owner`="" em todo o lado).

### Crumb 4 — Dispatch de operador GENÉRICO: `==`/`<`/… sobre `T` constrangido por interface-operador
**Toca:** `src/checker/typer.tks` (e reuso de `monomorph.tks` SEM edição). **É o coração do §6.**
- Novo `constraint_op_owner(t, env, table): ConstraintOp | NoConstraintOp` (molde de `value_op_owner`):
  se `t` é um type-param cuja constraint (via `constraint_interfaces`, `resolve.tks`) é uma interface que
  **declara o dunder procurado**, devolve o par (interface, dunder). Ver assinatura §4.
- Em `type_binary` (`:1034`), estender o gate: além de `value_op_owner(l)!=""||value_op_owner(r)!=""`,
  testar `constraint_op_owner`. Se casar, baixar para a **mesma via de dispatch-por-constraint que
  `x.metodo()` já usa** (`typer.tks:2197-2222`) — um `TCall` estático `<dunder>` cujo callee é resolvido
  contra a constraint e concretizado pelo `monomorph` (`instance_method_subst`, `monomorph.tks:927`) para
  o operador do tipo concreto. Ver **Opção M-B** §4 para a forma de lowering exata.
- Retorno: o do operador no contrato (`bool` p/ comparação, `self` p/ aritmético — ruling 3), lido do
  `iface_methods_by_name` do contrato.
- **Reuso #254:** esta crumb NÃO precisa de maquinaria de genérico nova — a dispatch-de-método-sobre-`T`
  e a estampagem monomorph já vivem (§1.5). É a mesma via, com o callee vindo do token de operador em vez
  de `.metodo`. **Marca-se a dependência: G4 ASSENTA sobre #254 (métodos-em-genérico), que está DONE.**
**Fixpoint:** inerte (nenhuma fn do compilador tem `<T: IEq>` com `==` sobre `T`).
**RITUAL: gate completo** (é o unlock do `Map<K: IEq & IHash>`/`sort<T: IOrd>`).

### Crumb 5 — Ramo `Overload` across-composition + fusão interface∘interface + contrapartida-de-contrato
**Toca:** `src/checker/collect.tks`.
- `fold_trait_methods` (`:1989-1993`): substituir o erro-duro do ramo `Overload` por **empurrar AMBOS**
  os membros (o do host/trait-anterior E o incoming) para a superfície achatada — coexistem como conjunto
  de sobrecarga. O `select_overload` (§9A, VIVO) resolve o call-site. Ver **Opção M-C** §4 para a forma
  (push-both vs dispatcher sintetizado).
- `effective_interface_methods` (`:1335`): ao concatenar métodos de `extends`, FUNDIR mesmo-nome por
  `merge_named_members` — `Absorb` (contrato idêntico → um só), `Conflict` (mesma assinatura, corpo
  diferente → erro), `Overload` (assinaturas distintas → coexistem). Fecha a regra §9.4-GERAL
  (interface∘interface) e a composição de operador-interfaces (`IEq & IOrd`) sai de graça daqui.
- **Contrapartida ao nível do contrato (G5):** um `check_interface_operator_counterparts(ib, …)` (molde
  de `check_comparator_counterparts` `typer.tks:8688`) corre sobre os operadores LISTADOS numa interface:
  se o contrato lista `__eq` sem `__ne` (ou qualquer par ⟂), **erro na interface** (o contrato tem de
  listar as duas — a lei diz "o contrato lista as duas assinaturas"). O **joint** (`Point & IEq & NeByEq`
  onde o host escreve `__eq` e o `NeByEq` achata `__ne`) fecha de graça porque `check_conformance`
  (`:1511`) corre sobre os métodos JÁ ACHATADOS (o fold precede a conformidade). Ver **Opção M-D**.
**Fixpoint:** o corpus não tem composição que sobrecarregue por `&` nem interface com `extends` homónimo
divergente (VERIFICAR na crumb — se houver, é erro legítimo, reportar para cima; NÃO abrir issue).
**RITUAL: gate completo** (toca a fusão partilhada + o consumo do `Overload`).

### Crumb 6 — Corpus: as interfaces de capacidade + os trait-mixins de contrapartida (stdlib)
**Toca:** novos `.tks` de stdlib (ex.: `src/core.tks` ou `src/cmp/cmp.tks` novo módulo — o implementer
segue a árvore de namespaces autoritativa). Escrever `IEq`/`IOrd` (interfaces, §4 snippets) +
`NeByEq`/`GtByLt`/`LeByLt`/`GeByLt` (trait-mixins, §4 snippets). Agora COMPILAM (crumbs 1-2 dão o parse
de operador bodyless-em-interface e bodied-em-trait). **[eventual]**: um `sort<T: IOrd>` genérico e a
migração do `Map` para `<K: IEq & IHash>` são ADJACENTES (stdlib-expansão) — REPORTAR como destravados,
NÃO construir neste plano (âmbito stdlib separado; `plano-stdlib-*`).
**Fixpoint:** corpus de biblioteca novo — INERTE para o auto-fixpoint do compilador (é corpus, não
maquinaria); as fns só são estampadas quando USADAS por um genérico monomorfizado (crumb 7 fixtures).

### Crumb 7 — Fixtures de regressão (§5). **RITUAL.**
### Crumb 8 — Reseed + PROVENANCE (§6). **RITUAL final.**

---

## 4. Mecânicas ABERTAS — 3+ opções COM exemplo + recomendação

### M-A — Como exprimir "este operando tem dono de operador" para struct/class
**Opção A1 — estender `value_op_owner` com um teste "declara operador".**
```teko
/**
 * value_op_owner — the value-type OR operator-carrying struct/class NAME that owns operator lookup for
 * `t`, or "" for a raw primitive / a plain struct with no operator (§9 ruling 7 trigger). A `Named`
 * whose decl is a `NewtypeBody`/`EnumBody`/`FlagsBody` always owns; a `StructBody`/`ClassBody` owns
 * ONLY when it declares at least one `operator` member (a plain data struct returns "" so `a + b` over
 * two plain structs never enters the dunder lookup). Modeled on `bignum_kind`.
 *
 * @param t      the operand's resolved type
 * @param table  the folded type table
 * @return       the owning type's canonical name, or ""
 * @since §6 (9-ops)
 */
fn value_op_owner(t: Type, table: TypeTable): str
```
Exemplo: `type Point = struct IEq { operator __eq(...) {…} x: i32 }` → `value_op_owner(Point)="Point"`;
`type Plain = struct { x: i32 }` → `""` (o `+` continua a errar como hoje).

**Opção A2 — novo `operator_owner(t, table)` separado, deixando `value_op_owner` intacto.** Dois
predicados: `value_op_owner` (prim/enum/flags) e `operator_owner` (struct/class-com-operador); o
`type_binary` chama os dois. Exemplo: mais código, mas isola o value-type (ruling 10) do struct (tem
campos) — não os confunde.

**Opção A3 — um `has_declared_operator(name, table): bool` chamado só quando o `value_op_owner` base
falha.** `type_binary` primeiro tenta value-types, depois, se ambos "", tenta o struct-owner. Exemplo:
minimiza o custo no caminho quente (dois primitivos nunca chegam ao segundo teste).

**RECOMENDAÇÃO: A1** — uma fonte da verdade para "quem despacha operador", o predicado "declara operador"
é um loop barato sobre `td.body` methods, e mantém `type_binary` com UM gate. A separação value-type-vs-
struct dos invariantes (sem-campos) é ortogonal e vive no checker de DEFINIÇÃO (crumb 2), não no
dispatch.

### M-B — Como o `==` genérico (`a: T`, `T: IEq`) baixa
**Opção B1 — desaçucarar para a chamada estática `<dunder>(a, b)` resolvida por constraint.**
`a == b` → um `TCall` cujo callee é o dunder, tipado pela MESMA via de `x.metodo()` sobre `T`
(`typer.tks:2197`), concretizado por `monomorph`.
```teko
/**
 * constraint_op_owner — the (interface, dunder) an operator on a CONSTRAINED type-parameter dispatches
 * through, or `NoConstraintOp` when `t` is not a type-param whose constraint interface declares the
 * operator (§6/§9.4 generic operator dispatch). Mirrors `value_op_owner` for the generic case: when
 * `a: T` and `T: IEq`, `a == b` resolves `__eq` against IEq's contract and lowers to the ordinary
 * constraint-method dispatch (typer.tks:2197) that `monomorph` concretizes to the argument type's own
 * operator (`instance_method_subst`). Reuses #254's shipped generic-method-on-`T` machinery.
 *
 * @param t      the operand's resolved type (possibly a constrained type-parameter)
 * @param dn     the dunder sought (`dunder_of_binop`)
 * @param env    the typing environment (carries the enclosing fn's type-param constraints)
 * @param table  the folded type table
 * @return       the (interface, dunder) to dispatch, or `NoConstraintOp`
 * @since §6 (9-ops)
 */
fn constraint_op_owner(t: Type, dn: str, env: Env, table: TypeTable): ConstraintOp | NoConstraintOp
```
Exemplo: `fn index_of<T: IEq>(xs: []T, needle: T): i32 { … if xs[i] == needle …}` — `==` baixa para o
`__eq` do contrato; `index_of<Point>` estampa `Point::__eq`.

**Opção B2 — um nó TAST novo `TOpDispatch { iface; dunder; l; r }` que o monomorph reescreve.** Explícito,
mas duplica o que a via de método-sobre-`T` já faz. Exemplo: mais superfície de TAST + codec + backend.

**Opção B3 — resolver eagerly no monomorph (deixar o typer aceitar `==` sobre `T: IEq` sem baixar, e o
`monomorph` injeta a chamada).** Exemplo: adia o erro para depois do typer — pior diagnóstico, quebra a
regra "resolução em comp-time no ponto de tipagem".

**RECOMENDAÇÃO: B1** — reusa a via de constraint-method já provada por #254 (§1.5), zero superfície TAST/
codec nova, diagnóstico no sítio de tipagem. O `monomorph` já sabe concretizar um callee-sobre-`T` para o
método da instância; um operador é só um callee cujo nome vem do token.

### M-C — Forma do ramo `Overload` across-composition
**Opção C1 — push-both na superfície achatada; `select_overload` resolve no call-site.**
```teko
match merge_named_members(out[idx to u64], tm, table, "", type_name) {
    Absorb => { }
    Conflict => return method_merge_error(type_name, tname, tm.name, own_methods)
    Overload => { out = teko::list::push(out, trait_method_as_member(tm)) }   // coexist — §9A picks at call
}
```
Exemplo: `trait A { fn k(x: i32){…} }` + host `fn k(x: str){…}` → ambos ficam; `host.k(3)`→A, `host.k("s")`
→host, por `select_overload`.

**Opção C2 — sintetizar um método dispatcher que ramifica por tipo.** Recria `select_overload` à mão —
redundante, sai da lei "resolução no call-site".

**Opção C3 — manter separados por sufixo já no fold (pré-manglar).** Fura a fonte-única-da-verdade do
sufixo (o checker, §9A §4) — risco de divergência de paridade C↔nativo.

**RECOMENDAÇÃO: C1** — a superfície achatada É o `Env` que `select_overload` consome; empurrar ambos
forma o conjunto de sobrecarga naturalmente. VERIFICAR que os métodos achatados de um host entram no
mesmo `(ns,name)` pseudo-namespace de método (`collect.tks:365`) para `env_is_overloaded` os ver como
conjunto (§9A §2 nota).

### M-D — Onde a contrapartida é OBRIGADA
**Opção D1 — no contrato de interface (a interface lista as duas) + conformidade exige ambas.** A lei:
"o contrato lista AS DUAS assinaturas". Exemplo: `IEq { operator __eq; operator __ne }` — um tipo com só
`__eq` (e sem `NeByEq`) FALHA `check_one_interface` (falta `__ne`).

**Opção D2 — no operador de DEFINIÇÃO (quem escreve `__eq` num tipo é obrigado a `__ne`).** Já existe para
value-types (`check_comparator_counterparts` `typer.tks:8688`). Exemplo: obriga mesmo sem interface — mas
a lei quer que o `NeByEq` composto SATISFAÇA, o que colide se a obrigação for no tipo e o `__ne` vier de
um trait.

**Opção D3 — ambos (contrato lista; e o value-type-que-define-solto também).** Exemplo: dupla guarda; o
value-type solto (sem interface) mantém D2, a interface-operador ganha D1.

**RECOMENDAÇÃO: D3** — a lei §9.4 põe a obrigação NO CONTRATO (D1) para a capacidade-via-interface, e o
ruling-5 de §9 já põe D2 no operador-de-value-type solto. São complementares e não colidem: o joint
(`__ne` via `NeByEq`) satisfaz D1 porque a conformidade vê os métodos ACHATADOS; D2 fica value-type-only
(um value-type solto que escreve `__eq` sem `__ne` nem trait). Fixar em Javadoc que D1 é "contrato lista
as duas" e D2 é "value-type solto define as duas".

### Snippets de corpus (crumb 6 — verbatim, full-Javadoc)
```teko
/**
 * IEq — the equality capability (§9.4): a type conforms by WRITING `operator __eq`; the counter-part
 * `operator __ne` is MANDATORY (equality by negation). A conformer writes `__ne` itself or composes the
 * `NeByEq` mixin. A generic `<T: IEq>` dispatches `==`/`!=` through T's contract (real vtable/monomorph
 * dispatch — the retired structural `Eq` could not), unlocking `Map<K: IEq & IHash, V>`.
 *
 * @since 0.3.1 (§9.4)
 */
type IEq = interface {
    operator __eq(left: self, right: self): bool
    operator __ne(left: self, right: self): bool
}

/**
 * IOrd — the ordering capability (§9.4): each comparator obliges its REFLECTION — `__lt`⟂`__gt`
 * (operands swapped), `__le`⟂`__ge`. `<T: IOrd>` dispatches ordering through T. Unlocks `sort<T: IOrd>`.
 *
 * @since 0.3.1 (§9.4)
 */
type IOrd = interface {
    operator __lt(left: self, right: self): bool
    operator __gt(left: self, right: self): bool
    operator __le(left: self, right: self): bool
    operator __ge(left: self, right: self): bool
}

/**
 * NeByEq — the optional counter-part mixin for `IEq` (§9.4): flattens the obvious `__ne` body
 * `!(left == right)` into a composer that already writes `__eq`, so the type satisfies IEq's mandatory
 * counter-part without hand-writing it. Self-contained: the body references only `==` on `self`, which
 * the composing host provides. A type wanting a non-trivial `__ne` (e.g. NaN) writes its own instead.
 *
 * @since 0.3.1 (§9.4)
 */
type NeByEq = trait {
    operator __ne(left: self, right: self): bool { !(left == right) }
}

/**
 * GtByLt — flattens `__gt(l, r) = r < l` (ordering by reflection) into an `IOrd` composer that writes
 * `__lt`. Sibling mixins `LeByLt`/`GeByLt` supply `__le`/`__ge` by the same reflection over `<`.
 *
 * @since 0.3.1 (§9.4)
 */
type GtByLt = trait {
    operator __gt(left: self, right: self): bool { right < left }
}
```

---

## 5. Fixtures de regressão (inputs → códigos de saída nativos)

Layout confirmado (herdado dos irmãos §9/§9A): ACEITAR = projeto `examples/regressions/<nome>/` com
`<nome>.tkp`, `<nome>.tkr` (`Then stdout pattern`/exit), `main.tks`, `src/`. REJEITAR dobra em
`examples/regressions/diagnostics/` com `src/<caso>/case.tks` + `Then diagnostic = "…"`. A aritmética do
`exit`/`println` codifica QUAL ramo correu (padrão `builtins`) — um relapso MOVE o valor, não passa em
silêncio.

### 5.1 ACEITAR — `examples/regressions/capability_iface/`
- **A6 — operador em struct + capacidade genérica por contrato (o unlock).**
  `type Point = struct IEq & NeByEq { x: i32; y: i32; operator __eq(left: self, right: self): bool
  { left.x == right.x && left.y == right.y } }` + `fn index_of<T: IEq>(xs: []T, needle: T): i32 { var i=0;
  loop { if i>=xs.len { return -1 }; if xs[i]==needle { return i }; i++ } }`. `index_of([P0,P1,P2], P1)`
  → `exit 1`. Prova: operador em struct (G1/G3), conformidade IEq com `__ne` via `NeByEq` joint (G5), e o
  dispatch genérico `==` por contrato (G4).
- **A6-ne — a contrapartida por negação REALMENTE despacha.** `P0 != P1` (via `NeByEq.__ne`) → `1`;
  `P0 != P0` → `0`. `exit` codifica os dois. Prova o corpo `!(left==right)` achatado.
- **A6-ord — ordem + reflexão.** `type Money = struct IOrd & GtByLt & LeByLt & GeByLt { c: i64;
  operator __lt(left: self, right: self): bool { left.c < right.c } }` + `fn min2<T: IOrd>(a: T, b: T): T
  { if a < b { a } else { b } }`. `min2(M5, M3).c` → `3`; um `M5 > M3` (via `GtByLt`) → `1`. Prova `<T:
  IOrd>` + reflexão.
- **A6-newtype — capacidade sobre value-type prim-backed (ponte §9 ↔ §6).**
  `type Ms = i64 { operator __lt(left: self, right: self): bool { (left to i64) < (right to i64) } }` que
  compõe `IOrd & GtByLt & …`? (value-type NÃO compõe traits — ver **Tensão T-1** §7; se resolvido por
  escrita direta, o value-type escreve as 4 e conforma IOrd sem mixin). `min2<Ms>` → ordinal. Prova a
  ponte se T-1 permitir; senão marca-se o limite.

### 5.2 ACEITAR — `examples/regressions/operator_overload_compose/`
- **OC1 — overload de método across-composition (destrava o ramo `Overload`).**
  `trait A { fn k(x: i32): i64 { (x to i64) + 10 } }`; host `type H = struct A { fn k(x: str): i64
  { (x.len to i64) + 20 } }`. `H{}.k(5)` → `15` (ramo trait, i32); `H{}.k("ab")` → `22` (ramo host, str).
  Soma → `exit 37`. Prova: `merge_named_members`→`Overload`→push-both (G6), resolvido por `select_overload`.
- **OC2 — overload de OPERADOR across-composition.** dois traits que cada um contribui um `operator
  __add` de assinatura distinta (mistos), compostos num host; `h + 3` e `h + other` escolhem ramos
  diferentes. `exit` codifica. Prova operador+overload+composição juntos.

### 5.3 REJEITAR — `examples/regressions/diagnostics/`
- **R-eq — contrato de interface incompleto.** `type IHalf = interface { operator __eq(left: self,
  right: self): bool }` (sem `__ne`) → `Then diagnostic = "counter-part"` nomeando `__ne` (G5/D1).
- **R-conf — conformidade parcial.** `type Bad = struct IEq { operator __eq(...) {…} }` sem `__ne` e sem
  `NeByEq` → `Then diagnostic` de conformidade citando `__ne` em falta (G2/G5).
- **R-op-struct-return — retorno errado num operador de struct.** `operator __eq(...): self` (devia
  `bool`) → `"bool"` (reuso `check_operator_invariants`, agora sobre struct).
- **R-gen — operador não garantido pela constraint.** `fn f<T>(a: T, b: T): bool { a == b }` (T SEM
  `IEq`) → `Then diagnostic` de "não garantido pela constraint" (molde `typer.tks:2205`). Prova que `==`
  sobre `T` exige a interface (constraint=interface-only).
- **R-iface-conflict — interface∘interface homónimo divergente.** `IA & IB` com o MESMO nome de método,
  assinatura idêntica, contrato incompatível → `Then diagnostic` de fusão (G8, via `merge_named_members`
  `Conflict`).

**Nota axis-law:** cada `exit`/token codifica QUAL ramo correu; testa-se o valor, nunca um efeito
incidental.

---

## 6. Dependências, sequenciamento, blast-radius, fixpoint

- **O que o 9-ops DESTRAVA (reportar como habilitado, NÃO construir aqui):**
  - `sort<T: IOrd>` genérico (hoje `sort_str`/`sort_i64` concretos, `src/sort/sort.tks`) — stdlib-expansão.
  - `Map<K: IEq & IHash, V>` genérico (hoje `str`-keyed) — precisa também de `IHash` (interface análoga,
    contrato `operator`? NÃO — hash não é operador; `IHash` é interface de MÉTODO `fn hash(): u64`, fora
    do âmbito operador; marca-se **[adjacente]** para o plano de collections). `IEq` é a metade que 9-ops
    entrega.
  - Comparação/igualdade estrutural opt-in da stdlib — cada tipo escreve o seu operador (a síntese
    structural foi aposentada; não volta).
  Estes são ADJACENTES (stdlib) — REPORTAM-SE para cima, não viram issues por mim.
- **Dependência #254 (métodos-em-genérico):** a crumb 4 (dispatch genérico de operador) ASSENTA sobre a
  via de método-sobre-`T` + estampagem monomorph de #254, que está **DONE** (`[HISTÓRICO]`,
  `drain-254-L4L5-class-factories.md`). Logo é REUSO, não bloqueio. Se surgir um caso onde o operador
  CONSTRÓI o tipo genérico (L5), a mesma `instance_method_subst` cobre (§1.5).
- **Ordem de fixpoint:** crumbs 1-5 são aditivo-inertes no corpus do compilador (zero operador em struct/
  class/interface; zero constraint de operador) → árvores idênticas, `bin-a == bin-b` fecha na crumb 5. A
  crumb 6 (corpus IEq/IOrd/mixins) é biblioteca INERTE até USADA por um genérico monomorfizado (as
  fixtures da crumb 7 são o primeiro uso — corpus de TESTE, não do compilador). O reseed (crumb 8) captura
  o seed novo.
- **Codec `.tkb` (R-tkb):** operadores em `StructBody.methods`/`ClassBody.methods`/`InterfaceBody.methods`
  já round-trip como `Function` (o `is_operator` já é serializado desde §9). VERIFICAR que
  `InterfaceBody` serializa um método bodyless com `is_operator=true` sem perda (crumb 1). Sem node novo.
- **Blast-radius:** `type_binary` (o gate ganha um segundo teste, crumb 3/4), `value_op_owner` (+struct),
  `value_type_body_methods` (+struct/class/interface), `fold_trait_methods`/`effective_interface_methods`
  (fusão), o parser de 3 body-nodes, e corpus novo. NÃO toca: codegen/LIR (o operador reusa o mangling de
  método existente — §9 já provou; sem símbolo novo), o cast E9, o `select_overload` (reusado tal-qual).

---

## 7. Tensões de lei (resolvidas law-first) + HALT

- **T-1 — value-type compõe trait-mixin de contrapartida?** A lei §9.4 diz "trait achata em class/struct/
  service" (NÃO lista value-type prim/enum/flags). Um `NeByEq`/`GtByLt` composto num `type Ms = i64`
  precisaria de o value-type aceitar `&`-compose de trait. **Resolução law-first:** a lei do §9 (value-
  type, ruling 10) diz que um value-type carrega "métodos readonly + operadores APENAS, sem campos, sem
  statics" e a §9.4 NÃO inclui value-type nos hosts de trait — logo **um value-type NÃO compõe mixin; ele
  ESCREVE as contrapartidas diretamente** (4 operadores à mão para IOrd). O `NeByEq`/`GtByLt` servem
  struct/class/service (os hosts listados). Sem tensão real — a fixture A6-newtype (§5.1) documenta o
  limite. **NÃO é HALT.**
- **T-2 — a constraint sobre `T` que carrega operador é interface-only.** §9.4: "Constraint = interface-
  only, sem exceção". `NeByEq` é TRAIT (não é tipo, não constraint). Logo `<T: NeByEq>` é erro (já
  coberto pelas guardas trait-não-é-tipo da onda-traits). A capacidade constrange SEMPRE na interface
  (`IEq`/`IOrd`), o mixin só ACHATA o corpo. Coerente. Sem tensão.
- **T-3 — fusão interface∘interface muda comportamento (concatena→funde).** Ligar `merge_named_members`
  a `effective_interface_methods` (G8) altera o caso de duas interfaces-extends com método homónimo.
  **Resolução:** VERIFICAR na crumb 5 que nenhum `extends` do corpus tem homónimo divergente VIVO (o
  vtable slot assume nome único; a concatenação-dupla nunca foi observada). Se houver, é erro legítimo a
  corrigir OU reportar para cima. Sem tensão de lei.
- **T-4 — contrapartida no contrato vs no tipo (M-D).** Resolvido por D3 (§4): D1 no contrato de
  interface, D2 no value-type solto, complementares, o joint satisfaz D1 via métodos achatados. Sem
  tensão.
- **Sem HALT.** Todos os rulings SELADOS (§9, §9A, §9.4) encaixam num desenho law-first coerente: os
  operadores estendem-se a struct/class/interface reusando a validação e o mangling de método, o dispatch
  genérico reusa a via de constraint-method de #254, o ramo `Overload` reusa `select_overload`, a
  contrapartida reusa `check_comparator_counterparts`. As dependências (#254, §9, §9A) ATERRARAM. **NENHUM
  HALT necessário.**

---

## 8. Ritual de reseed + PROVENANCE (crumb 8)

Só depois de todas as crumbs verdes e do gate completo:
1. `cc -std=c2x -w -O2 -I src/runtime -I src/assert bootstrap/teko.c src/runtime/teko_rt.c
   src/assert/assert.c -lm -o gen0`
2. `TEKO_BACKEND=c ./gen0 build . --no-verify --release` → `bin-a`.
3. Re-build com `bin-a` como seed → `bin-b`. **Fixpoint: `bin-a == bin-b`** byte-a-byte
   (`scripts/fixpoint_gate.sh`). Aditivo-inerte no corpus do compilador ⇒ fecha na crumb 5.
4. Harvest: `bootstrap/teko.c` (novo seed) + `bootstrap/PROVENANCE` (novo hash, notando operadores em
   struct/class/interface + o ramo `Overload` ligado + a camada interface-operador).
5. **NUNCA correr `teko test .`** (fuga de memória do `monomorph` — crasha o container). Gate por
   `--no-verify` + `scripts/*.sh`.

---

## 9. Resumo para o implementer — o que fecha JÁ e o que fica

Fecha JÁ, sem esperar ninguém (todas as deps aterraram): as 8 crumbs — operadores em struct/class/
interface (parse+validação), dispatch de operador sobre struct e sobre `T: IEq`/`IOrd` genérico (reuso
#254 + monomorph), o ramo `Overload` da fusão-de-composição ligado a `select_overload`, a fusão
interface∘interface, a contrapartida obrigatória ao nível do contrato + joint `NeByEq`, o corpus
`IEq`/`IOrd` + `NeByEq`/`GtByLt`/`LeByLt`/`GeByLt`, as fixtures e o reseed.

Fica ADJACENTE (reportado como destravado, âmbito stdlib SEPARADO — NÃO construído aqui): `sort<T: IOrd>`
genérico, a migração do `Map` para `<K: IEq & IHash>` (precisa de `IHash`, interface de MÉTODO, não de
operador), a comparação/igualdade estrutural opt-in da stdlib. Fica marcado o LIMITE de T-1 (value-type
não compõe mixin — escreve as contrapartidas à mão).
