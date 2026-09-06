# Plano — §9: propriedades (acessores `get`/`set`) em interfaces e implementadores

> **Status:** DESIGN. Read+design apenas — nenhum código de produto editado, nenhum build, nenhum
> reseed. Este documento É o artefacto; o único commit desta crumb é ele próprio.
> **Branch:** `fix/retirement` (onda de aposentação; drena sequencial, SEM PRs).
> **Fonte de lei:** rulings SELADOS do dono para §9 propriedades (abaixo, VINCULATIVOS — o plano
> desenha À VOLTA deles, nunca os re-abre).
> **Papel na migração:** este é o HABILITADOR do §9 D (variant→struct+interface): a interface `Type`
> usa propriedades para declarar um *contrato-de-campo* que os implementadores têm de satisfazer. A
> feature é geral (get/set em class e struct); o uso struct+get-only é preocupação da migração, não
> restrição da feature.
> **Irmãos:** §9 A (sobrecarga, `plano-secao9A-method-overload.md`), §9
> operadores/value-type-methods (`plano-secao9-operadores-e-value-type-methods.md` — precedente do
> contextual-keyword `operator` e do flag inerte `is_operator`), interface-value
> (`interface-value-type.md` — o fat-ptr `{data,vtable}` que a dispatch de acessor herda).
> **Lei permanente:** Teko-only (.tks), W15+Javadoc-completo em TODA declaração, law-first.

---

## 0. Rulings SELADOS do dono (LEI — desenha-se à volta, não se re-abre)

1. **Interface declara propriedade por acessores.** `get nome(): T` (legível) e `set nome(value: T)`
   (gravável). A PRESENÇA determina o modo: só `get` ⇒ **read-only**; só `set` ⇒ **write-only**;
   ambos ⇒ **read-write**.
2. **Implementador fornece os corpos**, tipicamente sobre um campo privado `_nome`:
   ```teko
   type Type = interface {
       get propriedade(): T
       set propriedade(value: T)
   }
   type Imp = class Type {
       _propriedade: T
       pub get propriedade(): T { self._propriedade }
       pub set propriedade(value: T) { self._propriedade = value }
   }
   var a: Type = Imp {}          // constrói o implementador, guarda no tipo-interface
   a.propriedade = valor         // chama set — field-like, SEM parênteses
   var b: T = a.propriedade      // chama get — field-like, SEM parênteses
   ```
3. **Use-site é field-like:** `a.prop` invoca o getter, `a.prop = v` invoca o setter — SEM parênteses
   de chamada.
4. **Aplica-se a class (mutável, get+set) E a struct (value-type).** Para a migração-de-IR (IR
   imutável), impls struct usam **get-only**. A FEATURE é geral; o uso struct+get-only é preocupação
   da migração, não restrição da feature.
5. **Habilitador do §9 D:** a interface `Type` usa propriedades como contrato-de-campo dos
   implementadores.

**Corolário inerte (derivado, não novo ruling):** nenhuma propriedade existe no corpus atual ⇒ a
feature é ADITIVA-INERTE ⇒ o reseed é byte-idêntico até a primeira propriedade ser escrita (ver §7).

---

## 1. Estado de HOJE — as superfícies que a feature toca (achados, com file:line)

### 1.1 Modelo de interface (declaração do contrato)
- **AST:** `InterfaceBody = struct { extends: []str; methods: []Function }`
  (`src/parser/ast.tks:544`). Um contrato é HOJE só uma lista de `Function` bodyless.
- **Parser:** `parse_interface_body` (`src/parser/parse_decl.tks:731`); o loop de membros
  (`:744-756`) exige que cada membro parseie como `Function` via `parse_function(... allow_bodyless =
  true)` (`:748`) e rejeita qualquer outra coisa (`:749`). `parse_type_body` despacha para cá em
  `:814`.
- **Método efetivo / slots de vtable:** `effective_interface_methods` (`src/checker/collect.tks:986`)
  produz (extends-transitivos primeiro, depois próprios) a lista cuja ORDEM É o esquema de slot;
  `iface_methods_by_name` (`:1029`) é a query pública que checker, codegen e o esquema de slot leem.

### 1.2 Conformidade (implementador satisfaz o contrato)
- **Nominal:** `type_conforms_to` (`src/checker/resolve.tks:1504`) — só verifica que o nome do
  contrato está no `implements`/`extends`; NÃO verifica presença de método.
- **Presença de método (o gate real):** `check_conformance` (`src/checker/collect.tks:1141`) →
  `check_one_interface` (`:1169`) → `check_one_requirement` (`:1196`). Cada requisito é casado POR
  NOME (`:1200`), assinatura via `method_sig_matches` (`:1201`), e — para uma class — exige
  `pub`/`exp` (`:1206-1212`); ausente ⇒ `"'…' does not implement interface '…' — missing method
  '…'"` (`:1217`). `conformance_diags` (`:2179`) e os coletores de struct/class body (`:2197`,
  `:2203`) invocam-no.
- **Registo de método:** `collect_method_signatures` (`src/checker/collect.tks:394`) faz `define_fn`
  do método sob o pseudo-namespace `method_ns = "<owning-ns>::<TypeName>"` (`:364`, `:401`).

### 1.3 Dispatch de interface (o valor-interface)
- **Fat-ptr:** `IfaceFatPtr = struct { ctx; data; vtable }` (`src/lir/lower.tks:5382`),
  `lower_iface_fatptr` (`:5385`) carrega `{data@0, vtable@ptrsize}` — espelha o layout C
  `{ .data, .vtable = tk_vt_<Class>_<Iface> }` (`src/codegen/codegen.tks:4088-4116`, emissão de
  vtable em `:11718`/`:11839`).
- **Checker:** `type_contract_dispatch` (`src/checker/typer.tks:1878`) resolve `recv.metodo(args)`
  num contrato para `slot` + assinatura efetiva e emite um `TCall` com `is_iface_dispatch = true;
  iface_slot = slot` (`:1908`). A entrada é `type_method_call` (`:1739`), alcançada pelo despacho de
  `MethodCall` em `type_expr` (`:3940-3957`).

### 1.4 Acesso a campo — os DOIS sítios de desugar (o núcleo desta feature)
- **Leitura `a.field`:** `type_field_access` (`src/checker/typer.tks:2859`). Trata `.len`/`error`/
  `Ref.value` cedo, depois resolve o receiver a um `Named` (`:2895`), acha o decl (`:2899`) e o tipo
  do campo por corpo: struct (`:2904`), class (`:2905-2914`, incl. visibilidade privada `:2910`),
  **e HOJE ERRA para interface** — `"an interface value exposes no fields — only its contract
  methods"` (`:2916`). Devolve `TFieldAccess` (`:2919`).
- **Escrita `a.field = v`:** `type_field_assign` (`src/checker/typer.tks:4708`). Tipa o LHS via
  `type_expr(a.target)` (`:4709`), extrai o `FieldAccess` (`:4710`), trata `Ref.value`-deref
  (`:4714-4723`), verifica raiz mutável (`:4725`, `field_write_root_writable`), coage o valor
  (`:4729`) e emite `TAssign { kind = Field }` (`:4730`). Alcançado por `type_assign` (`:4765`) via
  `AssignKind::Field` (`:4768`). O parser marca o LHS como `AssignKind::Field` — `AssignKind`/`Assign`
  em `src/parser/ast.tks:307-308`; `FieldAccess`/`MethodCall` em `:209`/`:210`.
- **Statement de expressão** (para envolver a chamada-setter): `TExprStmt = struct { expr: TExpr }`
  (`src/checker/tast.tks:160`), no variant `TStatement` (`:188`); `TypedStmt` (`:260`).

### 1.5 `get`/`set` são livres HOJE (contextual, nunca reservar)
- `keyword_kind` (`src/lexer/lexer.tks:331-372`) NÃO lista `get`/`set` — são `Ident` em todo o lado.
- **Scan de risco (obrigatório antes de reservar):** `get`/`set` como PALAVRA-INTEIRA ocorrem às
  centenas no corpus como nomes reais — `set` em `src/collections/map.tks`, `get`/`set` em
  `src/build/project.tks` (48×`set`), `src/codegen/codegen.tks` (157×`set`), `src/checker/typer.tks`
  (40×`set`), etc. **Reservar `get`/`set` PARTIRIA o corpus massivamente.** ⇒ **Contextual
  obrigatório**, exatamente como `operator` (219 ocorrências, `plano-secao9-operadores…` §0/§2.3) e
  `trait` (`parse_decl.tks:825`): `get`/`set` só significam acessor na posição de MEMBRO de corpo,
  quando `Ident("get")`/`Ident("set")` é imediatamente seguido de um NOME e de `(`.

---

## 2. Representação — acessor É um método (reuso máximo, mudança mínima)

**Decisão law-first (chave do desenho):** um acessor é um `Function` com dois flags inertes + o nome
da propriedade, e é REGISTADO sob um nome de método SINTÉTICO reservado. Isto faz TODA a maquinaria
existente de método (registo, conformidade por-nome, slot de vtable, dispatch fat-ptr/direto,
mangling, codegen) servir acessores SEM alteração — o único trabalho novo vive no parser (reconhecer
`get`/`set`) e nos dois sítios de use-site (§4). É o mesmo padrão que §9-operadores usa para
`operator __dunder`.

### 2.1 Novos campos em `Function` (`src/parser/ast.tks:402`) — todos inertes

```
    /**
     * is_getter — this Function is a property GETTER accessor `get <prop>(): T { … }` (§9
     * properties). A non-accessor Function carries false, so every existing `fn`/method reproduces
     * byte-for-byte (additive-inert). When true, the accessor takes NO user parameters (only the
     * injected `self`), declares a return type T, and `accessor_prop` names the property.
     */
    is_getter: bool
    /**
     * is_setter — this Function is a property SETTER accessor `set <prop>(value: T) { … }` (§9
     * properties). False for every non-accessor (additive-inert). When true, the accessor takes
     * exactly ONE user parameter `value: T` after the injected `self`, returns Unit, and
     * `accessor_prop` names the property.
     */
    is_setter: bool
    /**
     * accessor_prop — the SOURCE property name an accessor serves (`""` for a non-accessor). The
     * `name` field carries the SYNTHETIC dispatch symbol (`__get_<prop>` / `__set_<prop>`); this
     * keeps the human-facing property name for diagnostics and for the use-site property registry
     * (`find_property`, §4). Empty on every ordinary fn/method — additive-inert.
     */
    accessor_prop: str
```

Nota (inércia): ADICIONAR os três campos obriga a atualizar TODOS os construtores de `Function`
(grep `Function {` — parser, `collect` reconstruções, folds de trait). Todos inicializam
`is_getter = false; is_setter = false; accessor_prop = ""`. Só isto já é RITUAL (toca um tipo
central) — ver §9 crumb 1.

### 2.2 Esquema de nome sintético (a fonte de unicidade e do reuso grátis)

O parser produz, para `get prop`, um `Function` com `name = "__get_" ~ prop`, `is_getter = true`,
`accessor_prop = prop`; para `set prop`, `name = "__set_" ~ prop`, `is_setter = true`,
`accessor_prop = prop`. Consequências (todas GRÁTIS, sem código novo):
- **Conformidade:** o getter da interface é `__get_prop`; o do impl é `__get_prop` ⇒
  `check_one_requirement` (`collect.tks:1200`) casa-os por nome, `method_sig_matches` valida a
  assinatura, e o gate `pub` (`:1206`) exige o acessor de class ser `pub`/`exp` — TUDO reusado.
- **Slot de vtable:** `__get_prop`/`__set_prop` entram em `effective_interface_methods`
  (`collect.tks:986`) na ordem de declaração ⇒ ganham slot como qualquer método.
- **Dispatch:** o use-site desugar (§4) sintetiza um `MethodCall{ method = "__get_prop" }` /
  `"__set_prop"` e passa por `type_method_call` (`typer.tks:1739`) ⇒ interface via
  `type_contract_dispatch` (fat-ptr), class via vtable/direto, struct direto. Zero codegen novo.
- **Mangling:** o símbolo do acessor é o símbolo de método normal do nome sintético — nenhum mangler
  novo.

**Invariante de segurança (registry é a fonte da verdade, não o prefixo):** a resolução de use-site
(§4) NUNCA reescreve por olhar o nome nu; olha o REGISTRY construído a partir dos flags
`is_getter`/`is_setter`. Assim um utilizador que (patologicamente) escrevesse `fn __get_x(self): T`
como método ordinário NÃO cria uma propriedade (flag false) — `a.x` continuaria a errar como campo
inexistente, não a despachar. O prefixo `__get_`/`__set_` serve só de símbolo distinto; a
autoridade-de-propriedade é o flag.

---

## 3. Parsing — `get`/`set` contextuais na posição de membro

### 3.1 Reconhecedor de membro-acessor (novo helper puro)

```
/**
 * accessor_kind_at — is the member at `pos` a property accessor, and which? Returns 1 for a getter
 * (`get <name>(`), 2 for a setter (`set <name>(`), 0 for neither. CONTEXTUAL, exactly like
 * `operator`/`trait`: `get`/`set` are ordinary identifiers everywhere and only start an accessor
 * when `Ident("get")`/`Ident("set")` is immediately followed by a NAME and a `(` (past an optional
 * leading doc and a `pub`/`exp`/`intern` visibility run — the same prefix `class_item_is_method`
 * skips). Never reserve `get`/`set`: they name real members across the corpus (§1.5).
 *
 * @param tokens  the token stream
 * @param pos     the member's first token (leading doc/visibility allowed)
 * @return        1 = getter, 2 = setter, 0 = not an accessor
 * @since §9 properties
 */
fn accessor_kind_at(tokens: []lexer::Token, pos: u64): u64
```

`struct_item_is_method` (`parse_decl.tks:456`) e `class_item_is_method` (`:534`) passam a devolver
`true` também quando `accessor_kind_at(tokens, pos) != 0` (senão o membro cai no ramo de campo e
falha). O loop de `parse_interface_body` (`:744`), `parse_fields` (`:487`) e `parse_class_fields`
(`:550`) roteiam um membro-acessor para `parse_accessor` (abaixo) em vez de `parse_function`.

### 3.2 `parse_accessor` — delega o grosso a `parse_function`

```
/**
 * parse_accessor — parse a property accessor member `[pub|exp|intern] (get|set) <prop> ( … ) [: T]
 * [{ … }]` into a `Function` flagged `is_getter`/`is_setter` with `accessor_prop = <prop>` and a
 * SYNTHETIC dispatch name (`__get_<prop>` / `__set_<prop>`). `allow_bodyless` is true in an
 * interface body (a bodyless contract accessor) and false in a struct/class body (a real accessor
 * body is required). The signature/body/visibility parse REUSES `parse_function`'s machinery by
 * treating the contextual `get`/`set` as the `fn` keyword position; this routine then post-validates
 * the accessor SHAPE and rewrites the name.
 *
 * Shape rules (post-parse, precise diagnostics):
 *   - getter: ZERO user parameters (only the injected `self`), a return type `: T` REQUIRED
 *             (`get <prop>()` with no `: T` is `"a getter must declare a return type"`).
 *   - setter: exactly ONE user parameter named `value` (after injected `self`), NO return type
 *             (a `: T` on a setter is `"a setter returns nothing — drop the `: T`"`); the setter's
 *             value type IS the property type.
 *   - the receiver is ALWAYS injected (`is_type_method = true`); an explicit `self` is a parse error
 *             (inherited from `parse_function`, `:395`).
 *
 * @param tokens          the token stream
 * @param pos             the accessor's first token (leading doc/visibility allowed)
 * @param is_type_method  always true (accessors are type-body members)
 * @param allow_bodyless  true in an interface body, false in a struct/class body
 * @return                the parsed accessor `Function` + the resume index
 * @throws                on a malformed accessor shape or body
 * @since §9 properties
 */
fn parse_accessor(tokens: []lexer::Token, pos: u64, is_type_method: bool, allow_bodyless: bool): Parsed<Decl> | error
```

Nota de implementação: a via de menor risco é FATORAR o corpo comum de `parse_function`
(pós-`fn`: `<T>`-params, `(params)`, `-> ret`, corpo/bodyless) num helper partilhado e chamá-lo tanto
de `parse_function` como de `parse_accessor`, para que a injeção de `self` (`:395`), o `allow_bodyless`
(`:314`) e o parse de corpo fiquem byte-idênticos. Se o seed não suportar a refatoração
comodamente, `parse_accessor` pode montar um stream local e chamar `parse_function`, corrigindo
depois `name`/`is_getter`/`is_setter`/`accessor_prop` no `Function` devolvido — o resultado é o mesmo
`Function`. O implementador escolhe a menos invasiva que preserva as mensagens de erro existentes.

### 3.3 Nada muda na gramática de tipos

`InterfaceBody`/`StructBody`/`ClassBody` (`ast.tks:544`/`:455`/`~:504`) permanecem
`{ …; methods: []Function }` — o acessor VIVE na lista `methods` como um `Function` flagado. Zero
campo novo de body ⇒ o codec `.tkb` de body (`TypeBody`, `ast.tks:558`) não muda de forma; só o
codec de `Function` ganha os três campos inertes (mesmo padrão que `is_operator` em §9-operadores).

---

## 4. Resolução de use-site — o desugar para chamadas de acessor (o coração)

Um REGISTRY puro é a fonte da verdade:

```
/**
 * PropInfo — the accessor surface of a property `prop` on a resolved type: whether a getter and/or
 * a setter exist, and the accessors themselves (for the return/value type and the dispatch name).
 * Presence decides the mode (ruling 1): get-only = read-only, set-only = write-only, both = rw.
 *
 * @field has_get  a `get prop` accessor exists on the type's effective method surface
 * @field has_set  a `set prop` accessor exists on the type's effective method surface
 * @field getter   the getter Function (valid iff has_get)
 * @field setter   the setter Function (valid iff has_set)
 * @since §9 properties
 */
pub type PropInfo = struct { has_get: bool; has_set: bool; getter: parser::Function; setter: parser::Function }

/**
 * find_property — the property `prop` declared on the resolved type `type_name` (or `null` when
 * `prop` is not a property of it). Scans the type's EFFECTIVE method surface — `iface_methods_by_name`
 * for an interface receiver, the effective struct/class methods otherwise — for accessors flagged
 * `is_getter`/`is_setter` whose `accessor_prop == prop`. The flags (never the bare name) are the
 * authority, so an ordinary method that merely happens to be named `__get_x` is NOT a property.
 *
 * @param type_name  the receiver's resolved type name
 * @param prop       the accessed member name
 * @param table      the folded type table
 * @return           the property's accessor surface, or null when `prop` is not a property
 * @since §9 properties
 */
pub fn find_property(type_name: str, prop: str, table: TypeTable): PropInfo | null
```

Helper de desugar partilhado — sintetiza um `MethodCall` e reusa `type_method_call`:

```
/**
 * desugar_getter — type `recv.prop` as the accessor CALL `recv.__get_prop()` (ruling 3, field-like
 * read). Synthesizes a `parser::MethodCall { receiver = recv; method = "__get_" ~ prop; args = [] }`
 * and routes it through `type_method_call` (typer.tks:1739), so an interface receiver dispatches
 * through the fat-ptr vtable (`type_contract_dispatch`, `:1878`) and a struct/class receiver takes
 * the direct/virtual method path — NO new codegen. The result type is the getter's return type.
 *
 * @param recv   the RAW receiver expression (`recv.prop`'s `recv`)
 * @param prop   the property name
 * @param env    the typing environment
 * @param table  the folded type table
 * @return       the typed accessor-call TExpr (a `TCall`), or a type error
 * @since §9 properties
 */
fn desugar_getter(recv: parser::Expr, prop: str, env: Env, table: TypeTable): TExpr | error
```

(O setter é o análogo: `MethodCall { method = "__set_" ~ prop; args = [value] }`, tipado por
`type_method_call`, devolvendo o `TCall` void que se envolve num `TExprStmt`.)

### 4.1 Leitura — em `type_field_access` (`typer.tks:2859`)

A propriedade é probada APENAS depois de o caminho de CAMPO falhar por AUSÊNCIA, para inércia total:
- **Receiver interface:** hoje é erro-duro em `:2916`. SUBSTITUIR esse ramo por: `match
  find_property(name, fa.field, table) { PropInfo as p => if p.has_get { return desugar_getter(...) }
  else return error{ "property '<prop>' is write-only" }; null => return error{ <a mensagem
  existente> } }`. Na interface, sem propriedade, a mensagem histórica mantém-se.
- **Receiver struct/class:** manter a resolução de campo existente (`:2904`/`:2905`) INTACTA. Só
  quando o campo está genuinamente AUSENTE (o `field_type` devolve "no such field" — NÃO uma falha de
  VISIBILIDADE `:2910`, que continua a errar como hoje) é que se proba `find_property`; se houver
  getter ⇒ `desugar_getter`; se a propriedade existe mas é write-only ⇒ erro write-only; senão
  re-emitir o erro de campo original. Isto garante que TODO acesso a campo que compila hoje fica
  byte-idêntico (fixpoint, §7).

### 4.2 Escrita — em `type_field_assign` (`typer.tks:4708`)

A propriedade é probada PRIMEIRO (antes de `type_expr(a.target)` em `:4709`), porque um LHS-propriedade
não é um `FieldAccess` real — se caísse em `type_field_access` seria reescrito para uma chamada-getter
e partiria a extração `a.target.kind { FieldAccess }` (`:4710`). Como (a) uma propriedade `prop` e um
campo homónimo NUNCA coexistem (o backing é `_prop`) e (b) zero propriedades existem no corpus,
probar-propriedade-primeiro é inerte hoje:

1. Extrair o `FieldAccess` do `a.target` (a forma sintática já é garantida pelo parser).
2. Tipar SÓ o receiver (`type_expr(fa.receiver)`), resolver a um `Named` name.
3. `match find_property(name, fa.field, table)`:
   - `PropInfo` com `has_set` ⇒ tipar `a.value`, coagir ao tipo do parâmetro `value` do setter
     (`coerce_argument_into`), sintetizar `recv.__set_prop(value)` via `type_method_call`, e devolver
     `TypedStmt { node = TExprStmt { expr = <o TCall> }; env }`. Para um `op=` COMPOSTO (`a.op !=
     `=``), exige-se `has_get` também e expande-se para `recv.__set_prop(recv.__get_prop() op value)`
     (ver §11 aberto/risco).
   - `PropInfo` com `!has_set` (get-only) ⇒ erro `"property '<prop>' is read-only"`.
   - `null` ⇒ cair no caminho de escrita-de-campo existente (`:4711` em diante), byte-idêntico.

### 4.3 Enforcement read-only / write-only (ruling 1)

Emerge dos dois sítios acima, sem gate separado:
- **write-only lido** (`a.prop` com só setter): `type_field_access` acha `PropInfo{has_get=false}` ⇒
  `"property '<prop>' is write-only — it has a setter but no getter"`.
- **read-only escrito** (`a.prop = v` com só getter): `type_field_assign` acha
  `PropInfo{has_set=false}` ⇒ `"property '<prop>' is read-only — it has a getter but no setter"`.

---

## 5. Conformidade — grátis, com uma verificação de PARIDADE DE MODO

Como acessores são métodos `__get_prop`/`__set_prop`, `check_conformance`
(`collect.tks:1141-1217`) JÁ força que um impl que declara a interface forneça cada acessor exigido,
com assinatura casada (`method_sig_matches`) e `pub` numa class. O ÚNICO reforço novo:

- **Paridade de modo (get/set present-or-exceed):** a interface exige get e/ou set; o impl deve
  fornecer PELO MENOS os mesmos acessores. Isto já é implicado por `check_one_requirement` (cada
  acessor exigido é um "método exigido" casado por nome), portanto NÃO precisa de código novo — um
  impl sem o getter exigido falha com `"missing method '__get_<prop>'"`. **Recomendação de
  qualidade-de-diagnóstico (opcional, sem tensão de lei):** traduzir a mensagem `missing method
  '__get_<prop>'` para `"'<Imp>' does not implement property '<prop>' of interface '<Iface>' —
  missing getter"` num pós-processador em `check_one_requirement` (`:1217`) quando o requisito é um
  acessor (flag `is_getter`/`is_setter`). Puro-cosmético; o gate já rejeita corretamente sem isto.
- **Excesso é permitido:** um impl com set ALÉM do get exigido conforma (o set extra é só um método a
  mais) — nenhuma ação.

Struct-conforma-mas-não-class (`struct_conforms_but_not_class`, `resolve.tks:1392`) e o upcast de
interface-valor (`interface-value-type.md`) não mudam: uma propriedade não altera a forma do
fat-ptr, só adiciona slots de método (`__get_`/`__set_`) à vtable.

---

## 6. Codegen / lowering — NADA novo (o dividendo do desenho §2)

Porque o acessor é um método e o use-site desugara para um `TCall` (direto ou `is_iface_dispatch`),
todo o backend já sabe emiti-lo:
- **C:** a chamada de método/dispatch de interface existente emite o corpo do getter/setter e o
  `tk_vt_<Class>_<Iface>` já inclui os slots `__get_`/`__set_` (emissão de vtable
  `codegen.tks:11718`/`:11839`).
- **Nativo/LIR:** `lower_iface_fatptr` (`lower.tks:5385`) + o índice de slot existente resolvem a
  chamada de acessor exatamente como qualquer método de contrato; o `IfaceFatPtr` (`:5382`) é o
  mesmo.
- **Paridade C↔nativo:** garantida por construção — não há novo símbolo nem novo caminho; os testes
  de paridade de mangling/dispatch existentes cobrem-no.

⇒ Esta feature é PARSER + CHECKER apenas. Zero `.tks` de codegen/lower editado (além de, se aplicável,
qualquer serialização `.tkb` dos três novos campos de `Function` — ver §9 crumb 1).

---

## 7. Segurança de FIXPOINT (o argumento, e o scan de risco)

**Argumento (aditivo-inerte):** a feature só ATIVA quando existe um acessor `get`/`set`, o que exige
a nova sintaxe contextual. **Zero acessores existem no corpus atual** (nenhum `get X(): T`/`set
X(value: T)` em posição de membro). Logo:
- `is_getter`/`is_setter` são `false` e `accessor_prop` é `""` em TODA `Function` reconstruída ⇒ o
  codec `.tkb` e todos os construtores produzem a mesma forma lógica; o símbolo/dispatch de todo
  método existente é byte-idêntico.
- `find_property` devolve `null` para todo acesso de campo do corpus ⇒ `type_field_access` e
  `type_field_assign` correm o caminho de campo HISTÓRICO, na ORDEM histórica (probe só após
  ausência de campo, na leitura; probe-primeiro-mas-`null` na escrita) ⇒ TAST idêntica ⇒ codegen
  idêntico.
- `parse_accessor`/`accessor_kind_at` nunca disparam (nenhum `get`/`set` em posição de membro seguido
  de nome+`(`), e como `get`/`set` continuam `Ident` (NÃO reservados), todo uso corrente de `get`/
  `set` como nome (§1.5) parseia byte-idêntico.

⇒ O bootstrap reproduz-se byte-a-byte no corpus atual. INERTE.

**Risco-fixpoint identificado e neutralizado — NÃO reservar `get`/`set`.** Se o lexer passasse `get`/
`set` a `TokenKind`, as centenas de usos-nome (§1.5) re-tokenizariam e a árvore não compilaria. O
desenho é EXPLÍCITO: contextual-only em `keyword_kind` (deixar `:331-372` intocado), reconhecimento
só em posição de membro via `accessor_kind_at`. Cobrir por um teste-de-parser que afirma que `var set
= 1; get.foo()` (nomes ordinários) continua a parsear.

**Ponto de partilha-de-sintaxe a vigiar (o "campo simples fica campo simples"):** a leitura proba
propriedade SÓ após ausência-de-campo, e NUNCA em cima de uma falha de VISIBILIDADE — assim um acesso
a campo privado continua a errar como hoje, não a cair para propriedade. A escrita proba-primeiro,
mas `find_property` devolve `null` para todo campo real (o backing é `_prop`, nome distinto). O
implementador DEVE confirmar por build que nenhum acesso de campo existente muda de veredicto.

---

## 8. Fixtures de regressão

Layout (confirmado, igual a §9 A): ACEITAR = projeto `examples/regressions/<nome>/` com `.tkp`/`.tkr`/
`main.tks`/`src/`, veredicto por `Then stdout pattern = "…"` (aritmética que só o caminho certo
produz); REJEITAR = um `src/<caso>/case.tks` sob o canal partilhado
`examples/regressions/diagnostics/` + um Scenario `Then diagnostic = "<substring>"`.

### 8.1 ACEITAR — `examples/regressions/properties/` (novo projeto)

- **A1 — get+set read-write via class implementando interface.**
  `type Cell = interface { get value(): i64; set value(v: i64) }`,
  `type IntCell = class Cell { _v: i64; pub get value(): i64 { self._v }; pub set value(v: i64) {
  self._v = v } }`. `var c: Cell = IntCell {}; c.value = 7; exit(c.value)` → **exit 7** (prova
  set-depois-get via dispatch de interface fat-ptr).
- **A2 — read-only computada (só getter, sem backing directo).**
  `type Sq = interface { get squared(): i64 }`, impl com `_n: i64` e `pub get squared(): i64 {
  self._n * self._n }`. `var s: Sq = …; exit(s.squared)` com `_n=6` → **exit 36** (prova que o getter
  corre um corpo, não lê um campo).
- **A3 — propriedade em struct, get-only (uso da migração-de-IR).**
  `type P = struct Point { _x: i64; pub get x(): i64 { self._x } }` (ruling 4: struct get-only). `var
  p = Point { _x = 5 }; exit(p.x)` → **exit 5** (dispatch DIRETO, sem fat-ptr — prova o caminho
  struct).
- **A4 — set-only aceita a escrita (o par de A2, exercita write-only).**
  `type Sink = interface { set push(v: i64) }` + impl que soma num campo; duas escritas `k.push = 10;
  k.push = 22` e um método de leitura ordinário `total()` → **exit 32**.

### 8.2 REJEITAR — dobrar no canal `examples/regressions/diagnostics/`

- **R1 — escrever propriedade read-only.** interface só com `get value(): i64`; `x.value = 3` →
  `Then diagnostic = "read-only"`.
- **R2 — ler propriedade write-only.** interface só com `set value(v: i64)`; `var y = x.value` →
  `Then diagnostic = "write-only"`.
- **R3 — impl não fornece o setter exigido.** interface `get/set value`; class que só declara o
  getter → `Then diagnostic = "value"` (a mensagem missing-getter/setter cita o nome da
  propriedade).
- **R4 — getter sem tipo de retorno.** `get value() { 0 }` (falta `: T`) → `Then diagnostic =
  "getter must declare a return type"`.
- **R5 — `get`/`set` continua nome livre (parser-inércia).** ACEITAR (não rejeitar): um caso onde
  `set`/`get` são um nome de variável/método ordinário compila e corre — dobrar como A5 no projeto
  ACEITAR, `stdout pattern`. Prova que a contextualização não regrediu (guarda o §7).

---

## 9. Sequência de crumbs (ordenada; cada uma gate-ável isoladamente)

Cada crumb compila e passa o gate rápido; RITUAL = gate completo. Dependências de seed: a feature usa
só mecanismos JÁ no seed (`Function`+flags à la `is_operator`, `type_method_call`, fat-ptr dispatch),
portanto pode aterrar independentemente dos irmãos §9.

1. **Campos inertes em `Function`.** Adicionar `is_getter`/`is_setter`/`accessor_prop` a
   `Function` (`ast.tks:402`), inicializados a `false`/`false`/`""` em TODOS os construtores (grep
   `Function {`), e ao codec `.tkb` de `Function` (paridade escrita/leitura). — *inerte: false/""
   em todo o lado.* **RITUAL: gate completo** (toca um tipo central + o codec).
2. **Parser: `accessor_kind_at` + `parse_accessor` + roteamento.** Adicionar o reconhecedor
   contextual e o parse de acessor; ligar `struct_item_is_method`/`class_item_is_method`
   (`parse_decl.tks:456`/`:534`) e os loops de membro de `parse_interface_body`/`parse_fields`/
   `parse_class_fields`. Sem use-site ainda ⇒ um programa que DECLARA um acessor parseia (e o checker
   ainda o vê só como um método sintético). Teste-de-parser: acessor parseia; `get`/`set`-como-nome
   continua livre (§7). — *inerte no corpus (nenhum acessor lá).*
3. **Registry `find_property` + `PropInfo`.** Funções puras no checker (`resolve.tks`/`typer.tks`),
   sem call-site novo. Teste-de-checker: `find_property` acha get/set numa interface sintética e
   devolve `null` para um campo comum. — *inerte.*
4. **Desugar de LEITURA em `type_field_access`.** Substituir o erro-de-interface (`typer.tks:2916`)
   pelo probe de propriedade e adicionar o probe-após-ausência no caminho struct/class; `desugar_getter`
   via `type_method_call`; enforcement write-only. — *inerte: `find_property` é `null` no corpus.*
   **RITUAL: gate completo** (mexe no sítio quente de acesso a campo).
5. **Desugar de ESCRITA em `type_field_assign`.** Probe-primeiro de propriedade (`typer.tks:4708`),
   chamada-setter via `type_method_call` envolvida em `TExprStmt`; enforcement read-only;
   `op=`-composto expande get+set. — *inerte: `null` no corpus.* **RITUAL: gate completo.**
6. **Conformidade (cosmético) + fixtures ACEITAR** (`examples/regressions/properties/`, A1–A5).
   Primeira introdução de acessores REAIS — exercita parse+conformidade+dispatch ponta-a-ponta.
   **RITUAL.**
7. **Fixtures REJEITAR** (R1–R4 no canal `diagnostics/`). **RITUAL.**
8. **Reseed + PROVENANCE** (crumb final, §10).

---

## 10. Ritual de reseed + PROVENANCE (crumb final)

Só depois de todas as crumbs verdes e do gate completo passar:

1. `cc -std=c2x -w -O2 -I src/runtime -I src/assert bootstrap/teko.c src/runtime/teko_rt.c
   src/assert/assert.c -lm -o gen0`
2. `TEKO_BACKEND=c ./gen0 build . --no-verify --release` → `bin-a`.
3. Re-build com `bin-a` como seed → `bin-b`. **Fixpoint: `bin-a == bin-b`** byte-a-byte
   (`scripts/fixpoint_gate.sh`). Como a feature é aditiva-inerte no corpus, o fixpoint DEVE fechar já
   na crumb 5; as crumbs 6/7 introduzem acessores no CORPUS DE TESTE (fixtures), não no compilador.
4. Harvest: `bootstrap/teko.c` (novo seed) + atualizar `bootstrap/PROVENANCE`.
5. NUNCA correr `teko test .` (fuga de memória). Gate por `--no-verify` + os `scripts/*.sh`.

---

## 11. Riscos + tensões de lei (com resolução recomendada)

- **R-reservar `get`/`set` (fixpoint).** Neutralizado: contextual-only, `keyword_kind` intocado
  (§1.5/§7). Recomendação FIRME.
- **Ordem de probe no acesso a campo.** Leitura: probe SÓ após ausência-de-campo e NUNCA sobre falha
  de visibilidade (senão um campo privado cairia para propriedade). Escrita: probe-primeiro, seguro
  porque backing é `_prop` e `find_property` dá `null` a todo campo real. Cobrir por fixture que fixa
  o veredicto de um acesso a campo privado. Sem tensão de lei.
- **`op=` composto numa propriedade.** `a.prop += v` precisa de get E set; expande-se para
  `set(get() op v)`. Recomendação: suportar `op=` só quando a propriedade é read-write; caso
  contrário erro claro (`"'+=' on property '<prop>' requires both a getter and a setter"`). Sem
  tensão de lei; é escolha local em `type_field_assign`.
- **Mutabilidade de struct + setter (ruling 4).** Um `set` numa struct muta `self._x`, o que exige um
  receiver mutável/por-referência — semântica que a migração-de-IR resolve preferindo struct
  get-only. Recomendação: PARSEAR `set` em struct (a feature é geral), mas o enforcement de "setter de
  struct exige receiver `mut`" segue a regra de escrita-de-campo já existente
  (`field_write_root_writable`, `typer.tks:4725`) aplicada ao `self` do setter; se o modelo de
  `mut self` em métodos de struct ainda não existir no seed, o uso struct+set fica REPORTADO como
  remanescente e o corpus da migração usa struct get-only (que compila hoje). Sem tensão de lei — é
  faseamento, não conflito.
- **Sem tensão de lei genuína identificada.** Todos os rulings selados encaixam num desenho law-first
  (acessor-é-método) coerente. Nenhum HALT necessário.

---

## 12. Perguntas ao dono/integrador (não-bloqueantes; adiantei tudo o que não depende delas)

1. **Grafia do símbolo sintético** — o desenho usa `__get_<prop>`/`__set_<prop>` (reservado,
   C-legal, colide-livre). Se o dono preferir outra grafia (p.ex. `<prop>$get`), é uma constante
   local em `parse_accessor`/`find_property`/desugar, sem impacto de arquitetura. Recomendo
   `__get_`/`__set_`.
2. **Auto-propriedades sem backing explícito** — permitir `pub get value(): i64` numa class SEM o
   utilizador declarar `_value` (o compilador sintetiza o campo)? O desenho atual NÃO auto-sintetiza
   (o utilizador escreve o backing, como nos rulings/exemplo). Recomendo manter explícito neste corte
   (mais simples, passa os rulings); auto-propriedades são uma extensão aditiva futura sobre esta
   base. Não bloqueia nenhuma crumb.
3. **`set` em struct + `mut self`** — confirmar se o corpus da migração-de-IR precisa APENAS de
   struct get-only (ruling 4). Se sim, o faseamento de §11 basta e nada bloqueia; se o dono quiser
   struct set já agora, o modelo de `mut self` em métodos de struct precisa de estar no seed —
   REPORTO o estado real após o gate, não invento issue.
4. **`op=` composto em propriedade** — confirmar a regra "requer get+set, expande para
   `set(get() op v)`". Adiantei-a como escolha local; o dono pode vetá-la (só `=` simples) sem mexer
   noutras crumbs.
