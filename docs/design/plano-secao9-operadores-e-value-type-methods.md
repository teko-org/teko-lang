# Plano — §9: sobrecarga de operadores + métodos em value-types (primitive/enum/flags)

> **Status:** DESIGN. Read+design apenas — NENHUM `.tks` de produto editado, NENHUM build, NENHUM
> reseed, NENHUM `teko test .`. Este documento É o artefacto; o único commit desta crumb é ele próprio.
> **Branch:** `fix/retirement` (drena sequencial, SEM PRs).
> **Fonte de lei:** rulings SELADOS do dono para §9 (abaixo, VINCULATIVOS — o plano desenha À VOLTA
> deles, nunca os re-abre).
> **Irmão modelo:** `docs/design/plano-secao9A-method-overload.md` (mesma estrutura; §9 e §9 A são
> ORTOGONAIS — §9 A é resolução-de-sobrecarga no call-site, §9 aqui é operadores + corpos em
> value-types. Um operador PODE ser sobrecarregado por §9 A, mas §9 não depende de §9 A ter aterrado).
> **Lei permanente:** Teko-only (.tks), W15 + Javadoc-completo em TODA declaração, law-first,
> reseed disciplinado (`cc -std=c2x`, `--no-verify`).

---

## 0. Rulings SELADOS do dono (LEI — desenha-se à volta, não se re-abre)

### 0.1 Operadores (COMPORTAMENTO, não casting — sem "implicit/explicit operator")
1. Operador = método ESTÁTICO com a palavra-chave `operator`, operandos EXPLÍCITOS por posição:
   `operator __add(left: self, right: i32): self { … }` / `operator __add(left: i32, right: self): self`.
   Pelo menos um de left/right tem de ser `self`. Validação POR POSIÇÃO.
2. Sobrecarregáveis: aritméticos (`+ - * / %`), comparação (`== != < > <= >=`), índice (`[]`),
   bitwise (`& | ^ ~ << >>`). Atribuição/incremento (`+=`, `++`) FICAM DE FORA — operadores são
   READONLY (sem mutação/memória).
3. RETORNOS: aritmético/bitwise → o tipo SELF; comparação → `bool`; índice → o tipo do ELEMENTO.
4. Índice (`[]`) definível SÓ em subtipos INDEXADOS (sobre map/array/list/dict), nunca num newtype
   escalar (`Celsius`).
5. CONTRAPARTIDA de comparador OBRIGATÓRIA: define `__eq` ⇒ o compilador exige `__ne`; `__lt`⟂`__ge`;
   `__gt`⟂`__le`. (Aritmético/bitwise/índice: individuais, sem contrapartida.)
6. Operandos MISTOS + dispatch pelos DOIS lados (central para DSLs). Ordem left/right explícita
   resolve não-comutativos.
7. TRIGGER (byte-idêntico preservado): correr o lookup de dunder SÓ se pelo menos um operando NÃO é um
   primitivo cru. Um `type X = <prim>` NOMEADO CONTA como não-primitivo → dispara. Dois primitivos
   crus → o operador primitivo diretamente, sem lookup. O lookup é comp-time → sem dunder a casar,
   emite o operador primitivo.
8. DISPATCH: left tem `operator(left,right)` → usa-o; senão right → usa-o; senão o primitivo (sobre a
   base).
9. COMPANHEIRO OBRIGATÓRIO: o CAST não-transparente newtype↔base (hoje DIFERIDO, `typer.tks:2254-2266`)
   — EXTRACT (`Celsius to f32`) e CONSTRUCT (`f32 to Celsius`). Os operadores não fecham sem ele. (Um
   alias TRANSPARENTE `type X = <type>` sem corpo já resolve-through via `resolve.tks:1176-1181`.)
   É a DEPENDÊNCIA-CHAVE — desenhada em §4.

### 0.2 Value-types com métodos (a casa dos operadores)
10. Um `type` respaldado por um PRIMITIVO, ou um `enum`, ou `flags`, PODE carregar um corpo com:
    **métodos de instância readonly + operadores APENAS**. **SEM campos** (o valor É o primitivo).
    **SEM métodos estáticos** (ruling do dono — quem quer factory escreve uma função estática própria
    e separada; construção é via CAST `i32 to Celsius`, ou membro `Dir::N` / `F::A | F::B`).
11. Sem corpo ⇒ TRANSPARENTE (comporta-se como o primitivo cru — um newtype simples).
12. Aplica-se a enum e flags também: `enum Dir { N,S,E,W } { fn oposto(): Dir { … } }`,
    `flags F { … } { … }`.

---

## 1. Estado de HOJE — os quatro sub-sistemas que §9 toca (achados, file:line)

### 1.1 A gramática de tipos NÃO tem `operator` nem corpos em enum/flags/newtype
- `EnumBody` (`src/parser/ast.tks:475`) = `struct { members: []str }` — SEM `methods`. Logo
  `enum Dir { N,S } { fn … }` NÃO parseia hoje.
- `FlagsBody` (`src/parser/ast.tks:485`) = `struct { members; values }` — SEM `methods`. Idem.
- `AliasBody` (`src/parser/ast.tks:487`) = `struct { alias: TypeExpr }` — o `type Celsius = i32`
  TRANSPARENTE. SEM corpo. Logo `type Celsius = i32 { … }` NÃO parseia hoje.
- `TypeBody` (`src/parser/ast.tks:558`) é a variant que enumera os body-kinds; qualquer node novo
  entra aqui E no codec `.tkb` (C7.16 — ver risco R-tkb §9).
- `parse_type_body` (`src/parser/parse_decl.tks:810`): enum @838 e flags @846 chamam
  `parse_field_names` e devolvem imediatamente (consomem o `}` da lista de membros); o ramo alias
  @858-861 parseia um type-expr e devolve `AliasBody`. NENHUM olha para um `{` a seguir.
- `Function` (`src/parser/ast.tks:402`) NÃO tem `is_operator`. `parse_function`
  (`src/parser/parse_decl.tks:320`) não conhece `operator`.
- **`operator` NÃO é reservado** e NÃO deve passar a ser: `keyword_kind` (`src/lexer/lexer.tks:331`)
  não o lista, e há **219 ocorrências** de `operator` no corpus (grep §investigação) — a esmagadora
  maioria em doc-comments, mas reservá-lo arrisca partir o seed. **Decisão law-first (precedente
  `trait`, `token.tks:820-824`):** `operator` é CONTEXTUAL — só significa "declaração de operador"
  na posição de membro de corpo, `Ident("operator")` imediatamente seguido de um nome dunder `__…`.
  Em todo o resto continua um Ident vulgar. Zero tokens novos no lexer.

### 1.2 A tipagem de operadores binários — onde inserir o lookup de dunder
- `type_binary` (`src/checker/typer.tks:732`): tipa os dois lados, rejeita `void`/`ptr`, depois
  **`bignum_kind(l0.type)`/`bignum_kind(r0.type)` (@738-740) — se ≠ "", desvia para
  `type_bignum_binop`.** ESTE É O TEMPLATE EXATO do §9: um gate sobre um tipo `Named` que desvia o
  operador para uma chamada de biblioteca. O ramo de dunder do §9 entra no MESMO sítio, LOGO A SEGUIR
  ao gate de bignum (@740), antes de `adopt_binop_operands` (@741).
- `type_bignum_binop` (`src/checker/typer.tks:512`) + `bignum_call` (`typer.tks:442`): mostram como
  BAIXAR um operador para uma CHAMADA namespaced (`TCall` com `call_ns`), reusando a UMA implementação
  — sem doubling. O operador do §9 baixa para uma CHAMADA DE MÉTODO ESTÁTICO `TypeName::__add(l,r)`,
  cujo símbolo É o mangling de método normal (§5) — logo NÃO precisa de mangling novo.
- `bignum_kind` (`src/checker/typer.tks:396`): o molde do helper novo `value_op_owner` (§3.2) — um
  `match Type { Named as n => … }` que devolve o nome-de-tipo dono dos operadores, ou `""`.
- `type_arith_binop`/`type_bitwise_binop` (`typer.tks:697`)/`type_shift_binop`: os caminhos
  primitivos INTACTOS (o fallback do ruling 7/8).
- `type_compare` (`src/checker/typer.tks:793`): tipa a cadeia `a < b <= c` termo-a-termo. O dunder de
  comparação entra por-termo (§3.4); o retorno permanece `bool`.
- `type_index` (`src/checker/typer.tks:3125`): `match recv.type { Str/Slice/... }` com um
  `_ => error "cannot index"`. O dunder `__index` entra nesse `_` para um `Named` cuja decl o define
  (ruling 4 — só subtipos indexados).
- `type_unary` (`src/checker/typer.tks:754`): o `~` unário (bitwise NOT) é sobrecarregável (ruling 2);
  o `-`/`!` NÃO estão na lista de sobrecarregáveis do ruling 2 → ficam primitivos. O dunder `__bnot`
  entra no ramo `Tilde` @767, com o mesmo gate.

### 1.3 O cast newtype↔base — o companheiro DIFERIDO (o busílis)
- A nota @`src/checker/typer.tks:2254-2266`: "…newtype↔base awaits `resolve_named` lowering a newtype
  to its base — both deferred (M.4)." É EXATAMENTE o ruling 9.
- `type_cast` (`src/checker/typer.tks:2597`): já tem DOIS precedentes de cast identidade-de-representação
  entre um `Named` e um escalar, ambos fora do `cast_check` prim-only:
  - **E7** (@2622-2627): enum ↔ inteiro ORDINAL — `is_enum_named(...) && is_int_cast_end(...)`.
  - **E8** (@2628): `is_flags_wire_cast` — flags ↔ o seu inteiro-fio.
  Ambos devolvem um `TCast` cujo tipo É o alvo, sem tocar bits. O EXTRACT/CONSTRUCT do §9 é o TERCEIRO
  desta família (**E9**, §4).
- `resolve_named` (`src/checker/resolve.tks:1159`): o ramo @1176-1181 faz um `AliasBody` resolver-through
  ao tipo aliased; QUALQUER outro body-kind cai no `_ =>` @1179 e permanece NOMINAL (`Named{name}`).
  **É por isto que o value-type com corpo TEM de ser um body-node DISTINTO de `AliasBody`** (§2): assim
  fica nominal e o cast E9 é NECESSÁRIO (não resolve-through), exatamente como o ruling 9/11 manda.

### 1.4 Coleção de métodos e dispatch de instância — já existe, estende-se
- `collect_type_member_signatures` (`src/checker/collect.tks:363`): `match td.body { StructBody =>
  collect_method_signatures(...); ClassBody => …; _ => sig_unchanged(env) }`. Os value-types novos
  entram no `_` a passar a chamar `collect_method_signatures` (que já é genérico).
- `collect_method_signatures` (`src/checker/collect.tks:394`) + `method_ns` (@364,
  `"<owner-ns>::<TypeName>"`): reutilizável VERBATIM — os métodos/operadores de um value-type ligam-se
  sob o mesmo pseudo-namespace, e o operador estático é chamável como `TypeName::__add(...)`.
- `type_method_call` (`src/checker/typer.tks:1739`): dispatch de `x.metodo()`. Resolve `recv_t` a um
  `Named`, acha a decl, e faz `match decl.body { StructBody => sb.methods; ClassBody => … }`. Estende-se
  o `match` para os body-nodes de value-type (§6). NOTA @1741: `is_flags_named` hoje devolve
  "method typing is deferred" — esse honest-stop LEVANTA-SE para flags/enum/prim com corpo.
- `type_path_expr` (`src/checker/typer.tks:3663`): `Dir::N` (enum) e `F::A` (flags) já tipam como
  `TPathExpr`; a adição de um corpo ao enum/flags NÃO mexe aqui (o corpo é ortogonal aos membros) —
  só é preciso garantir que `resolve_member_const_hit`/o loop de membros continua a correr para um
  body-node que também traz `members`.

---

## 2. Gramática — os body-nodes de value-type (aditivo, seed-safe)

**Decisão de forma (law-first):** manter `AliasBody` como o transparente puro (ruling 11 — sem corpo);
adicionar UM node novo para o prim-backed COM corpo, e um campo `methods` a `EnumBody`/`FlagsBody`. Um
enum/flags SEM segundo bloco tem `methods == []` e comporta-se exatamente como hoje (byte-idêntico).

### 2.1 AST (`src/parser/ast.tks`)

```
/**
 * NewtypeBody — a PRIMITIVE-backed value-type WITH a body: `type Celsius = f32 { <members> }`
 * (§9 ruling 10/11). NOMINAL and NON-transparent — unlike `AliasBody` it does NOT resolve-through to
 * its base (`resolve.tks` keeps it a `Named`), so the base↔newtype conversion needs the explicit E9
 * cast (§9 ruling 9). The value IS the primitive: `base` is the backing type-expr (a scalar prim, or
 * — ONLY for an `[]`-operator carrier — an indexed type map/array/list/dict, ruling 4); `members`
 * holds ONLY readonly instance methods and `operator` declarations (NO fields, NO statics — ruling
 * 10, enforced by the checker in §7).
 *
 * A `type X = <prim>` with NO trailing `{ … }` block parses as `AliasBody` (transparent) exactly as
 * today — this node is produced ONLY when a body block follows.
 *
 * @field base     the backing type-expr (the primitive the value IS, or an indexed type for `[]`)
 * @field methods  the interleaved readonly instance methods + operators, in source order
 * @since §9
 */
pub type NewtypeBody = struct { base: TypeExpr; methods: []Function }
```

- `EnumBody` (`ast.tks:475`) passa a `struct { members: []str; methods: []Function }`.
- `FlagsBody` (`ast.tks:485`) passa a `struct { members: []str; values: []u64; methods: []Function }`.
- `TypeBody` (`ast.tks:558`): acrescentar `| NewtypeBody`.
- `Function` (`ast.tks:402`): acrescentar

```
    /**
     * is_operator — this member is an `operator __dunder(left: …, right: …)` declaration (§9
     * ruling 1), NOT an ordinary method. Operators are STATIC (no untyped receiver — every operand
     * is explicitly typed) and READONLY; the checker (§7) validates the dunder name, the operand
     * arity/positions (at least one operand is `self`), and the mandated return type per class
     * (arith/bitwise → self, comparison → bool, index → element). Ordinary methods carry false.
     */
    is_operator: bool
```

  Todos os construtores existentes de `Function` (parser, `synth.tks`, reconstrução `.tkb`) passam
  `is_operator = false` — aditivo-inerte.

### 2.2 Parser (`src/parser/parse_decl.tks`)
- `parse_type_body` (@810): três edições cirúrgicas:
  - enum @838-844: depois de `parse_field_names`, se o token em `ms.next` for `{`, parsear um
    bloco-de-métodos (nova `parse_value_type_members`, §2.3) e produzir `EnumBody { members; methods }`;
    senão `methods = []`.
  - flags @846-852: idem, `FlagsBody { members; values; methods }`.
  - alias @858-861: depois de `parse_type(...)`, se o token em `al.next` for `{`, produzir
    `NewtypeBody { base = al.node; methods = <parse_value_type_members> }`; senão `AliasBody` como hoje.
- `parse_function` (@320) ganha o reconhecimento contextual de `operator` — ver §2.3.

### 2.3 Nova função de parse dos membros

```
/**
 * parse_value_type_members — parse the trailing `{ <method-or-operator>* }` block of a value-type
 * body (§9): the second brace-block of `enum E { … } { … }` / `flags F { … } { … }`, or the sole
 * body block of `type X = <prim> { … }`. Each member is either an ordinary readonly instance method
 * (`fn name(self, …): T { … }`) or an `operator __dunder(...)` declaration; NO fields, NO consts, NO
 * statics are accepted here (a field/const/static token is a parse error naming ruling 10). `pos` is
 * at the opening `{`.
 *
 * `operator` is recognized CONTEXTUALLY: `Ident("operator")` at member-start makes the following
 * `Function` carry `is_operator = true`; anywhere else `operator` stays an ordinary identifier (no
 * reserved token — precedent `trait`).
 *
 * @param tokens  the token stream
 * @param pos     the index of the members block's opening `{`
 * @return        the parsed member list + the index past the closing `}`, or a located parse error
 * @since §9
 */
fn parse_value_type_members(tokens: []lexer::Token, pos: u64): ParsedValueMembers | error
```

`ParsedValueMembers = struct { methods: []Function; next: u64 }` (novo, em `result.tks`).

---

## 3. Tipagem dos operadores — o lookup de dunder gated no trigger

### 3.1 O mapa operador→dunder (fonte única)

```
/**
 * dunder_of_binop — the dunder method name a binary operator token dispatches to (§9 ruling 2), or
 * "" when the token is not an overloadable binary operator (`&&`/`||` are NOT — they stay bool-only).
 * Arithmetic: + __add, - __sub, * __mul, / __div, % __mod. Comparison: == __eq, != __ne, < __lt,
 * > __gt, <= __le, >= __ge. Bitwise: & __band, | __bor, ^ __bxor, << __shl, >> __shr. (Index `[]`
 * and unary `~` __bnot have their own sites — `type_index` / `type_unary`.)
 *
 * @param op  the binary operator token
 * @return    the dunder name, or "" if `op` is not an overloadable binary operator
 * @since §9
 */
fn dunder_of_binop(op: lexer::TokenKind): str
```

### 3.2 O trigger + o dono do operador

```
/**
 * value_op_owner — the value-type NAME that owns operator/method lookup for `t`, or "" when `t` is a
 * RAW primitive (ruling 7's trigger: lookup runs iff at least one operand is NOT a raw primitive). A
 * `Named` type whose decl is a `NewtypeBody`, or an `EnumBody`/`FlagsBody` (with or without a body —
 * a NAMED enum/flags counts as non-primitive), returns that name; every `Prim`/`Byte`/`Char`/`Str`/
 * `Slice`/`Ptr`/… returns "". Modeled on `bignum_kind` (typer.tks:396).
 *
 * Because the current corpus declares ZERO value-types carrying operators, and because two raw
 * primitives both yield "" here, the operator branch (§3.3) is never entered for any primitive pair —
 * the historic primitive path is byte-identical (fixpoint argument, §8).
 *
 * @param t      the operand's resolved type
 * @param table  the folded type table (to read the Named decl's body kind)
 * @return       the owning value-type name, or "" for a raw primitive
 * @since §9
 */
fn value_op_owner(t: Type, table: TypeTable): str
```

### 3.3 A inserção em `type_binary` (após o gate de bignum, `typer.tks:740`)

```
    // §9 — operator dispatch: fires iff at least one operand is a NAMED value-type (ruling 7). Two
    // raw primitives skip this entirely (value_op_owner both "") → the primitive path is untouched.
    if value_op_owner(l0.type, table) != "" || value_op_owner(r0.type, table) != "" {
        var dn = dunder_of_binop(b.op)
        if dn != "" {
            match dispatch_binop_operator(dn, l0, r0, env, table) {
                TExpr as te => return te            // a matching operator on left, then right (ruling 8)
                NoOperator => { }                   // fall through to the primitive op over the base
                error as e => return e
            }
        }
    }
```

`dispatch_binop_operator` implementa o ruling 8:

```
/**
 * dispatch_binop_operator — resolve a binary operator to a user `operator` on one of its operands and
 * lower it to a STATIC method call, per ruling 8's ordered dispatch: (1) the LEFT operand's owner
 * declares an `operator <dn>(left: L, right: R)` whose declared operand types accept this (l, r) pair
 * → call `Lowner::<dn>(l, r)`; else (2) the RIGHT operand's owner declares a matching one → call it;
 * else (3) `NoOperator` (the caller emits the primitive op over the base).
 *
 * Operand matching is BY POSITION (ruling 1): the first parameter type must accept `l`, the second
 * `r`, under the ordinary argument rule (`assignable_to`), with at least one being `self` (the owner).
 * The lowered `TCall` targets the operator's method pseudo-namespace (`<owner-ns>::<TypeName>`,
 * collect.tks:364) exactly like `bignum_call` (typer.tks:442) targets a library symbol — so codegen
 * reaches it through the ordinary method mangling (§5), no new symbol scheme. The result type is the
 * operator's declared return (checker-validated to self for arith/bitwise per ruling 3).
 *
 * @param dn     the dunder name (`dunder_of_binop`)
 * @param l      the typed left operand
 * @param r      the typed right operand
 * @param env    the typing environment
 * @param table  the folded type table
 * @return       the lowered static-call TExpr, `NoOperator` when neither side matches, or a type error
 * @since §9
 */
fn dispatch_binop_operator(dn: str, l: TExpr, r: TExpr, env: Env, table: TypeTable): TExpr | NoOperator | error
```

`NoOperator = struct { }` (marcador tri-state, molde `NotListBuiltin` `typer.tks:824`).

### 3.4 Comparação (`type_compare`, `typer.tks:793`)
No loop por-termo (@801-816), antes de `is_comparable`: se `value_op_owner(adopted.l.type)` ou
`value_op_owner(adopted.r.type)` ≠ "", chamar `dispatch_binop_operator(dunder_of_binop(op), l, r, …)`.
Um comparador de utilizador devolve `bool` (ruling 3); a cadeia continua a compor `bool`. O termo baixa
para a chamada estática e entra na cadeia como um `TCmpTerm` cujo `operand` já é a chamada — OU, mais
simples, a cadeia de comparação com operandos value-type deixa de ser uma `TCompare` e passa a um
`&&`-fold de chamadas booleanas (o implementador escolhe; recomendo o fold, que reusa `type_logical_binop`
e mantém a semântica de encadeamento). Fixar num Javadoc.

### 3.5 Índice (`type_index`, `typer.tks:3125`) e unário `~` (`type_unary`, `typer.tks:767`)
- Índice: no `_ =>` @3134, se `value_op_owner(recv.type)` ≠ "" e a decl define `operator __index`,
  baixar para `Owner::__index(recv, idx)`; o retorno é o tipo do ELEMENTO declarado (ruling 3). O
  checker (§7) já garantiu que `__index` só existe num NewtypeBody cuja base é indexada (ruling 4).
- `~` unário: no ramo `Tilde` @767, mesmo gate → `Owner::__bnot(operand)`.

---

## 4. O COMPANHEIRO: o cast newtype↔base (E9) — o busílis (ruling 9)

O cast é a peça sem a qual os operadores não fecham: um `Celsius` respaldado por `f32` é NOMINAL
(§1.3), logo mover valor entre `Celsius` e `f32` exige um `to` explícito. Representação é IDENTIDADE (o
valor É o `f32`), logo E9 é, no metal, um no-op de reinterpretação — exatamente como E7/E8.

### 4.1 Checker — o ramo E9 em `type_cast` (`typer.tks:2597`, junto a E7/E8 @2622-2631)

```
/**
 * value_type_base — the backing PRIMITIVE type of a nominal value-type, or `NotValueType` when `t`
 * is not a body-carrying prim-backed newtype. For `type Celsius = f32 { … }` (NewtypeBody) it returns
 * the resolved `f32`; an enum/flags value-type reports its ORDINAL/wire integer through the existing
 * E7/E8 paths instead, so this helper covers ONLY the NewtypeBody case (ruling 9's new work). A
 * TRANSPARENT `AliasBody` never reaches here — it resolves-through (resolve.tks:1176) and needs no cast.
 *
 * @param t      the type to inspect
 * @param table  the folded type table
 * @return       the resolved backing prim, or `NotValueType`
 * @since §9
 */
fn value_type_base(t: Type, table: TypeTable): Type | NotValueType
```

Inserção (E9), depois de E8 @2628, antes do `cast_check` @2629-2631:

```
    // §9 E9 — the nominal newtype↔base conversion (ruling 9). Representation-identity, like E7/E8.
    //   EXTRACT: `Celsius to f32` — inner is the newtype, target is (assignable from) its base.
    //   CONSTRUCT: `f32 to Celsius` — target is the newtype, inner fits its base (via cast_check on
    //   the base pair first, so `f64 to Celsius` narrows f64→f32 under the same runtime guard, then
    //   rewraps). Both emit a TCast whose type IS the target; codegen lowers the newtype to its base
    //   machine type (§5), so no bits move.
    match value_type_base(inner.type, table) {          // EXTRACT
        Type as base => {
            match cast_check(base, target) { null => return TExpr { kind = TCast { expr = inner }; type = target; line = 0; col = 0 }; error => { } }
            // (base itself IS target, or a defined numeric step off base) — accept; else fall to construct/error
        }
        NotValueType => { }
    }
    match value_type_base(target, table) {              // CONSTRUCT
        Type as base => {
            match cast_check(inner.type, base) { null => return TExpr { kind = TCast { expr = inner }; type = target; line = 0; col = 0 }; error as e => return e }
        }
        NotValueType => { }
    }
```

(O implementador refina a composição EXTRACT+narrow — recomendo: EXTRACT só aceita `base == target`
ou um widening lossless de `base`; um estreitamento extra escreve-se `c to f32 to i16`. Fixar em Javadoc.)

### 4.2 Codegen/LIR — a metade de baixo do companheiro (o trabalho REAL)
A nota @`typer.tks:2264` diz "awaits `resolve_named` lowering a newtype to its base". EXTRACT/CONSTRUCT
só são no-ops SE o `Named` prim-backed com corpo LOWERAR para o seu tipo-máquina base em TODO o lado
(layout, ABI, protótipos, `.tsym`). Pontos a confirmar/estender (o implementador VERIFICA por build):
- A resolução de tipo do codegen C e do LIR (o gémeo de `resolve_named`) tem de mapear um `Named` cuja
  decl é `NewtypeBody` para o C-type / classe-de-registo da base — hoje o `_ =>` nominal trataria-o como
  um struct (errado). É a única mudança NÃO-aditiva do §9 e vive nos backends CONGELADOS? NÃO — os
  gémeos C (`bootstrap/teko.c`) estão congelados; o lowering vive nos `.tks` de codegen/lir
  (`src/codegen/codegen.tks`, `src/lir/lower.tks`). Localizar a função de mapeamento tipo→C/LIR e
  acrescentar o ramo NewtypeBody→base. **Enum/flags value-type já lowered como o seu inteiro** (E7/E8
  existem), logo só o NewtypeBody prim-backed é novo.
- O `TCast` E9 lowera para o MESMO no-op que E7/E8 (reinterpretação); confirmar que o emit de `TCast`
  trata um par (Named-newtype, base-prim) como identidade e não invoca uma conversão numérica.

### 4.3 Porque isto é aditivo-seguro
Nenhum tipo do corpus atual é um `NewtypeBody` (o node não existe hoje). `value_type_base` devolve
`NotValueType` para todo o tipo existente ⇒ E9 nunca dispara ⇒ `type_cast` byte-idêntico no self-build.

---

## 5. Mangling e dispatch estático — REUTILIZAÇÃO, sem esquema novo

O operador baixa para uma chamada de método estático `TypeName::__add(l, r)`. O símbolo de um método já
é produzido pelo mangling de método existente sob o pseudo-namespace `<owner-ns>::<TypeName>`
(`collect.tks:364`), em paridade C↔nativo (`cgt_mangle_parity_c_and_native`). Um nome dunder `__add` é
`[A-Za-z0-9_]` — C-legal. **Logo §9 NÃO introduz um esquema de símbolo novo** (contraste com §9 A, que
sufixa). A resolução do método estático `TypeName::__dunder` passa pelo caminho de chamada estática já
existente (o mesmo que resolveria um `static fn` de utilizador, D3). O implementador confirma que
`type_static_call`/o resolvedor de `Path`-callee acha um membro `is_operator` sob o pseudo-ns.

---

## 6. Métodos de instância em value-types (a casa)

- `collect_type_member_signatures` (`collect.tks:363`): o `_ =>` passa a despachar `NewtypeBody`,
  `EnumBody` (methods≠[]), `FlagsBody` (methods≠[]) para `collect_method_signatures(item, td,
  <body>.methods, method_ns, table, env)` — genérico, reutilizado verbatim.
- `type_method_call` (`typer.tks:1739`): estender o `match decl.body` @1756-1760 (que hoje só conhece
  Struct/Class) para devolver `<body>.methods` dos três value-type nodes; LEVANTAR o honest-stop
  `is_flags_named` @1741 (que hoje devolve "method typing is deferred") para permitir método em flags/
  enum/newtype com corpo. O receiver de um método de instância é o `self` (1º param SEM tipo, a
  convenção existente @1771 `is_instance`), cujo tipo resolvido é o value-type.
- `self` como TIPO nos operandos do operador (`left: self`): `self` NÃO é reservado
  (`token.tks:163`); em posição de tipo dentro de um membro de value-type, resolve para o tipo dono. O
  implementador liga isto em `method_func_type`/na resolução de tipo com contexto de dono (o mesmo
  ponto onde o receiver de instância é sintetizado). Fixar em Javadoc.

---

## 7. Validações de DEFINIÇÃO (o checker, novo passo)

Ao contrário de §9 A (ambiguidade é call-site), os invariantes de operador são de DEFINIÇÃO. Um novo
passo por value-type decl (junto de `collect_type_member_signatures`, ou um check dedicado no
`check.tks`):

```
/**
 * check_operator_invariants — validate a value-type's `operator` members at DEFINITION (§9 rulings
 * 1–5, 10). Per operator: the dunder name is known (`dunder_of_binop`/`__index`/`__bnot`); every
 * operand is explicitly typed and at least one is `self` (ruling 1); the return type matches the
 * class (arith/bitwise → self, comparison → bool, index → element — ruling 3); `[]` (`__index`)
 * appears ONLY on a NewtypeBody whose base is an indexed type (ruling 4). Comparator COUNTERPARTS
 * are mandatory (ruling 5): `__eq`⟂`__ne`, `__lt`⟂`__ge`, `__gt`⟂`__le` — one present without its
 * counterpart is an error naming the missing dunder. Also enforces ruling 10: NO fields, NO static
 * methods, NO consts in a value-type body (the parser already rejects the tokens; this is the
 * checker backstop).
 *
 * @param td     the value-type declaration
 * @param table  the folded type table
 * @return       null when every operator is well-formed, else the first located diagnostic
 * @since §9
 */
fn check_operator_invariants(td: parser::TypeDecl, table: TypeTable): null | error
```

Contrapartida: um simples scan do conjunto de dunders declarados — se contém `__eq` e não `__ne` (ou
qualquer par ⟂), erro localizado nomeando o dunder em falta.

---

## 8. Segurança de FIXPOINT — o argumento byte-idêntico do trigger

**Aditivo em quatro camadas, cada uma inerte no corpus atual:**
1. **Gramática:** `NewtypeBody` e os campos `methods`/`is_operator` são novos; `operator` é contextual
   (zero tokens novos). Nenhuma decl do corpus é um value-type-com-corpo nem usa `operator` como
   construtor ⇒ o parser produz árvores idênticas (enum/flags com `methods == []`).
2. **Operadores:** a inserção em `type_binary`/`type_compare`/`type_index`/`type_unary` é gated em
   `value_op_owner(...) != ""`. Para DOIS primitivos crus (todo par de operandos no corpus do
   compilador, que não tem value-types), ambos os `value_op_owner` são `""` ⇒ o ramo NUNCA é entrado
   ⇒ `type_arith_binop`/`type_bitwise_binop`/`type_compare`/`type_index` correm PALAVRA-POR-PALAVRA
   como hoje. É o mesmo padrão do gate `bignum_kind` que já vive @738-740 e é fixpoint-safe.
3. **Cast E9:** gated em `value_type_base(...) != NotValueType`; nenhum tipo atual é um `NewtypeBody`
   ⇒ E9 morto ⇒ `type_cast` byte-idêntico.
4. **Dispatch/mangling:** o operador reusa o mangling de método existente; sem símbolo novo ⇒ nada a
   mover no auto-mangling do compilador.

**O corpus define algum operador hoje?** NÃO — `operator` não é um construtor existente; as 219
ocorrências são identificadores/doc. Logo `value_op_owner` é `""` em todo o self-build, todas as quatro
camadas são inertes, e `bin-a == bin-b` fecha byte-a-byte. As fixtures (§10) introduzem value-types no
CORPUS DE TESTE, nunca no compilador — não afetam o auto-fixpoint.

**Risco-fixpoint identificado:** a mudança de lowering-de-tipo do §4.2 (Named-NewtypeBody→base) NÃO é
gated por um flag — é uma extensão do resolvedor de tipo do backend. É segura SÓ porque nenhum
`NewtypeBody` existe no corpus; o `_ =>` nominal que hoje trata um `Named` continua a tratar structs/
classes/enums igual. O implementador CONFIRMA por build que nenhum tipo do compilador passa a lowered
diferente. Se algum passar, REPORTAR para cima (não é âmbito do §9 mexer nisso).

---

## 9. Codec `.tkb` e superfícies dependentes (verificar, não presumir)
- `TypeBody` alimenta o codec `.tkb` (C7.16, `ast.tks:558`) e `.tsym`/DWARF. Um node novo (`NewtypeBody`)
  e campos novos (`EnumBody.methods`, `FlagsBody.methods`, `Function.is_operator`) TÊM de ser
  serializados/desserializados em paridade (`src/emit/tkb_*.tks`, `src/emit/tkh.tks`). Aditivo mas
  OBRIGATÓRIO — um round-trip que perca `methods` parte a reconstrução. Fixar por um teste de round-trip.
- LSP (`src/lsp/*`): um value-type-com-corpo deve navegar/simbolizar como os outros; verificar
  `symbols.tks`/`nav.tks` não estoiram no node novo (honest-fallback já cobre o `_`).

---

## 10. Fixtures de regressão (inputs → códigos de saída nativos)

Layout (do §9 A): projeto `examples/regressions/<nome>/` com `.tkp`, `.tkr` (`Then stdout pattern`),
`main.tks`; REJEITAR dobra em `examples/regressions/diagnostics/` com `src/<caso>/case.tks` +
`Then diagnostic = "…"`. A aritmética do `exit`/`println` codifica QUAL ramo correu (padrão `builtins`).

### 10.1 ACEITAR — `examples/regressions/value_type_operators/`
- **A1 — aritmético prim-backed + EXTRACT/CONSTRUCT.** `type Celsius = i32 { operator __add(left:
  self, right: self): self { (left to i32 + right to i32) to Celsius } }`. `(20 to Celsius) + (5 to
  Celsius)` extrai, soma, reconstrói; `resultado to i32` → `25`. Prova operador + ambos os sentidos do
  cast E9. `exit 25`.
- **A2 — operandos MISTOS + dispatch dos dois lados (ruling 6/8).** `operator __add(left: self, right:
  i32): self` E `operator __add(left: i32, right: self): self`. `(10 to Celsius) + 3` → left-dispatch;
  `3 + (10 to Celsius)` → right-dispatch. Soma dos dois `to i32` → `26`. Prova a ordem explícita.
- **A3 — comparação + contrapartida.** `operator __eq(left: self, right: self): bool` + `__ne`. Duas
  `Celsius` iguais → `1`, diferentes → `0`. `exit` codifica os dois. Prova retorno `bool` e a
  contrapartida a compilar.
- **A4 — método de instância em enum (ruling 12).** `enum Dir { N,S,E,W } { fn oposto(): Dir { … } }`;
  `Dir::N.oposto()` == `Dir::S` → `exit` codifica o ordinal. Prova corpo em enum + `type_method_call`.
- **A5 — índice num subtipo indexado (ruling 4).** `type Grid = []i32 { operator __index(self: self,
  i: i32): i32 { … } }`; `g[2]` → o elemento. Prova `__index` retornando o tipo do elemento.
- **A6 — flags com corpo.** `flags Perm { R,W,X } { fn can_write(): bool { … } }`; `(Perm::W).can_write()`
  → `1`. Prova corpo em flags + honest-stop levantado.

### 10.2 REJEITAR — `examples/regressions/diagnostics/`
- **R1 — contrapartida em falta (ruling 5).** value-type com `__lt` e sem `__ge` →
  `Then diagnostic = "counterpart"` (mensagem nomeia `__ge`).
- **R2 — campo num value-type (ruling 10).** `type Bad = i32 { x: i32 }` → `Then diagnostic = "no
  fields"` (o value IS the primitive).
- **R3 — método estático num value-type (ruling 10).** `type Bad = i32 { static fn mk(): Bad {…} }` →
  `Then diagnostic = "static"`.
- **R4 — `__index` num newtype escalar (ruling 4).** `type Celsius = i32 { operator __index(...) }` →
  `Then diagnostic = "indexed"` (só sobre map/array/list/dict).
- **R5 — nenhum operando é `self` (ruling 1).** `operator __add(left: i32, right: i32): self` →
  `Then diagnostic = "self"`.
- **R6 — retorno errado (ruling 3).** `operator __eq(...): self` (devia ser `bool`) →
  `Then diagnostic = "bool"`.
- **R7 — sem cast, conversão nominal implícita rejeitada.** `var f: f32 = (20 to Celsius)` (sem `to
  f32`) → `Then diagnostic` de tipos incompatíveis. Prova que o newtype-com-corpo é NOMINAL (não
  resolve-through) — o inverso do alias transparente.

---

## 11. Sequência de crumbs (ordenada; cada uma gate-ável isoladamente)

Pontos de RITUAL (gate completo) marcados. Sequenciada por dependência de SEED: a gramática primeiro
(o resto precisa dos nodes), depois checker, depois codegen, depois fixtures.

1. **Gramática — nodes e campos.** `NewtypeBody` + `EnumBody.methods` + `FlagsBody.methods` +
   `Function.is_operator` (`ast.tks`); `TypeBody |= NewtypeBody`; todos os construtores existentes
   inicializam os campos novos (`methods=[]`, `is_operator=false`). Codec `.tkb`/`.tsym` (§9)
   estendido em paridade + teste de round-trip. — *inerte: enum/flags com methods=[] ⇒ árvore
   idêntica.* **RITUAL: gate completo** (toca tipos centrais + codec).
2. **Parser — corpos e `operator` contextual.** `parse_value_type_members` + os três hooks em
   `parse_type_body` + o `operator` contextual em `parse_function`. — *inerte: nenhuma fonte usa a
   sintaxe nova.*
3. **Collect + method dispatch.** `collect_type_member_signatures` despacha os value-type nodes para
   `collect_method_signatures`; `type_method_call` estende o `match decl.body` e levanta o honest-stop
   flags/enum; `self`-como-tipo resolve ao dono. — *inerte: sem value-types no corpus.*
4. **Validações de definição.** `check_operator_invariants` (dunder, operandos, `self`, retorno,
   índice-só-indexado, contrapartidas, no-fields/statics). — *inerte.* **RITUAL: gate completo.**
5. **Cast E9 (companheiro), metade do checker.** `value_type_base` + o ramo EXTRACT/CONSTRUCT em
   `type_cast`. — *inerte: `value_type_base` = NotValueType em todo o corpus.*
6. **Cast E9, metade do codegen/LIR.** Lowering Named-NewtypeBody→base no resolvedor de tipo de
   `codegen.tks`/`lower.tks`; emit de `TCast` E9 como no-op de reinterpretação. Confirmar
   `cgt_mangle_parity_c_and_native` + o fixpoint intactos. — *a crumb que pode mover lowering; a
   ausência de NewtypeBody no corpus é a rede.* **RITUAL: gate completo.**
7. **Operadores — o lookup de dunder.** `dunder_of_binop` + `value_op_owner` + `dispatch_binop_operator`
   + `NoOperator`; inserções em `type_binary`/`type_compare`/`type_index`/`type_unary`. Resolução do
   método estático `Type::__dunder`. — *inerte: `value_op_owner` = "" em todo o self-build.*
   **RITUAL: gate completo** (é o coração; o argumento byte-idêntico §8 é a rede).
8. **Fixtures ACEITAR** (`value_type_operators/`, A1–A6). Primeiro value-type-com-operadores REAL no
   corpus de teste ⇒ exercita ponta-a-ponta. **RITUAL.**
9. **Fixtures REJEITAR** (R1–R7 em `diagnostics/`). **RITUAL.**
10. **Reseed + PROVENANCE** (crumb final, §12).

---

## 12. Ritual de reseed + PROVENANCE (crumb final)

Só depois de todas as crumbs verdes e do gate completo:
1. `cc -std=c2x -w -O2 -I src/runtime -I src/assert bootstrap/teko.c src/runtime/teko_rt.c
   src/assert/assert.c -lm -o gen0`
2. `TEKO_BACKEND=c ./gen0 build . --no-verify --release` → `bin-a`.
3. Re-build com `bin-a` como seed → `bin-b`. **Fixpoint: `bin-a == bin-b`** byte-a-byte
   (`scripts/fixpoint_gate.sh`). §9 é aditivo-inerte no corpus do compilador ⇒ o fixpoint DEVE fechar
   já na crumb 7; 8/9 introduzem value-types só no corpus de TESTE.
4. Harvest: `bootstrap/teko.c` (novo seed) + `bootstrap/PROVENANCE` (novo hash/proveniência).
5. NUNCA correr `teko test .` (fuga de memória). Gate por `--no-verify` + os `scripts/*.sh`.

---

## 13. Riscos + tensões de lei (com resolução recomendada)

- **R-lowering (§4.2, o busílis técnico).** O único trabalho NÃO puramente-aditivo: o backend tem de
  lowered um `Named`-NewtypeBody para o tipo-máquina da base. Resolução: gate implícito pela ausência
  de NewtypeBody no corpus; o implementador CONFIRMA por build que nenhum tipo existente muda de
  lowering. Sem tensão de lei (é a implementação do ruling 9, explicitamente mandado).
- **R-tkb (codec).** Os nodes/campos novos têm de round-trip em `.tkb`/`.tsym`. Resolução: crumb 1 fá-lo
  em paridade + teste de round-trip antes de qualquer semântica depender deles. Sem tensão de lei.
- **R-self-como-tipo.** `self` em posição de tipo de operando é novo. Resolução: liga-se no ponto de
  síntese do receiver (§6); precedente = o receiver de instância. Sem tensão de lei.
- **R-comparação-encadeada (§3.4).** `a < b < c` com operandos value-type: fold de `&&` de chamadas
  vs. `TCompare`. Resolução: recomendo o fold (reusa `type_logical_binop`, semântica preservada);
  afinação LOCAL, sem impacto noutras crumbs. Sem tensão de lei.
- **R-fallback-primitivo (ruling 7/8).** "sem dunder a casar, emite o operador primitivo (sobre a
  base)" implica um EXTRACT implícito de um operando nominal quando nenhum operador casa. É a letra do
  ruling; documentar que o fallback reduz cada operando value-type à sua base. Sem tensão de lei.
- **Sem tensão de lei genuína identificada.** Todos os rulings encaixam num desenho law-first coerente;
  o cast E9 é o TERCEIRO de uma família existente (E7/E8), o operador reusa o dispatch de método, o
  trigger reusa o padrão `bignum_kind`. NENHUM HALT necessário.

---

## 14. Perguntas ao dono/integrador (não-bloqueantes; adiantei tudo o que não depende delas)

1. **Composição EXTRACT+narrow (§4.1).** `f64 to Celsius` (Celsius=f32) faz o estreitamento num só
   `to`, ou exige `f64 to f32 to Celsius`? Adiantei a forma recomendada (CONSTRUCT passa `cast_check`
   na base, logo um só `to` compõe o guard de estreitamento); o dono pode preferir a forma explícita.
   Não bloqueia as crumbs 1–4.
2. **Comparação encadeada (§3.4).** Fold de `&&` vs. `TCompare` para operandos value-type. Recomendo o
   fold; afinação local de `type_compare`.
3. **Fallback primitivo em operandos nominais sem operador casado (ruling 7/8).** Confirmo a leitura:
   reduz à base e aplica o operador primitivo. Se o dono quiser antes um ERRO ("nenhum operador
   definido") para um newtype nominal, é uma troca de uma linha em `dispatch_binop_operator` (devolver
   `error` em vez de `NoOperator`), sem impacto noutras crumbs.
