# Plano — Onda Traits: trait = mixin concreto auto-contido (achatável) + aposentadoria das structural traits (§9.4 REVISTO)

> **Status:** DESIGN. Read+design apenas — NENHUM `.tks` de produto editado, NENHUM build, NENHUM
> reseed, NENHUM `teko test .` (fuga de memória do `monomorph` — CRASHA O CONTAINER; nunca correr,
> nem guardado). Este documento É o artefacto; o único commit desta crumb é ele próprio.
> **Branch:** `fix/retirement` (drena sequencial, SEM PRs).
> **Fonte de lei:** `docs/design/mudancas-superficie-0.3.1.md` §9.4 (SELADO e REVISTO em HEAD:
> commits `fa2cd3c8`/`729414d3`/`c195e515` — *self-contained concrete mixin* + *AST-equality
> composition-disambiguation*). Desenha-se à volta, não se re-abre.
> **Irmãos modelo:** `docs/design/plano-secao9-operadores-e-value-type-methods.md` (DEPENDÊNCIA da
> camada interface-operador §6), `docs/design/plano-secao9A-method-overload.md` (o ramo overload da
> fusão §4.4 desenha contra a sua forma declarada), `plano-secao9-properties.md` (`get`/`set` no corpo
> do mixin).
> **Lei permanente:** Teko-only (.tks), W15 + Javadoc-completo em TODA declaração, law-first, reseed
> disciplinado (`cc -std=c2x`, `--no-verify`).
> **REVISÃO (o que mudou face à versão b2096c71):** o modelo selado foi SUBSTANCIALMENTE revisto. A
> versão anterior encolhia `TraitBody` para `{methods; consts}`, BANIA campos, e desenhava um
> "contrato estrutural host-provê-backing" com erro-de-composição nomeado. **TUDO ISSO CAIU.** Agora:
> trait é um **mixin concreto AUTO-CONTIDO** — **campos VOLTAM** (com default opcional), o backing
> `_nome` mora NA trait, traits **compõem traits** (`trait C & A & B`), traits **carregam interface**
> (`trait T & IFoo`), e a colisão é **identidade estrutural comparada pela AST** (igual → absorve;
> diferente → erro), regra que é **GERAL** a toda a composição por `&` (interface∘interface,
> tipo∘(trait/interface), operador-interfaces). Ver §0 abaixo para os rulings integrais.
> **Dependência declarada:** a camada interface-operador + contrapartida (§6) assenta sobre
> **operadores + métodos-em-value-type** (`plano-secao9…`). O ramo overload da fusão-AST (§4.4)
> assenta sobre a distinção-de-assinatura de §9A. Tudo o resto — o mixin auto-contido e a REMOÇÃO das
> structural traits — é INDEPENDENTE e adianta-se já. O bloqueado está marcado **[BLOQUEADO em §9-ops]**
> / **[BLOQUEADO em §9A]**.

---

## 0. Rulings SELADOS REVISTOS (LEI — desenha-se à volta, não se re-abre)

### 0.1 O que uma `trait` É — mixin concreto AUTO-CONTIDO (achatável)
1. **Só membros CONCRETOS:** **campos** (tipados, default opcional) **MANTIDOS**, **métodos-com-corpo**
   (properties `get`/`set`, `static fn`, métodos de instância) e **`const`s**. **ZERO membro
   abstrato/sem-corpo (bodyless)** — isso é papel da *interface*. (Campos VOLTAM: como tudo achata, um
   campo de trait é só um campo que achata no host.)
2. **Auto-contida:** todo `self.X` que um corpo referencia tem de ser **declarado na própria trait**
   (ou numa trait que ela compôs, ruling 5). Uma trait apontando para algo que não declara/desconhece
   está **visivelmente errada** → **erro NA PRÓPRIA TRAIT**, não na composição. O antigo
   "contrato-sobre-host" CAIU — o backing `_nome` do getter `nome` mora **na trait** (o getter lê
   `self._nome`, nunca a property — senão recursão).
3. **NÃO é tipo:** sem `var`/parâmetro/retorno/campo/constraint tipado por trait; sem dispatch; sem
   trait-object. Reside APENAS (a) na definição da própria trait e (b) no `&`-compose de um tipo.
4. **`self` = o host que a compõe**, como valor (`self._x`) e como tipo (`static fn new(): self`).
5. **`&` compõe, e traits compõem traits:** `trait C & A & B { … }` achata A e B em C — e C **conhece**
   A/B, logo pode referenciá-los (a auto-contenção estende-se ao que a trait compôs). Achata em **class
   (selada/virtual/abstrata), struct e service** (os hosts).
6. **Traits carregam interface:** `trait T & IFoo { … }` = T implementa **IFoo inteira** (auto-contida);
   todo host que compõe T passa a **satisfazer** IFoo — é o **host** (o tipo) que despacha, a trait não.
   **Padrão parcial (joint):** uma trait pode só **contribuir** métodos sem declarar `& IFoo`; aí o
   **host** declara `& IFoo` e a composição (host + traits) fecha o contrato juntos (o padrão `NeByEq`).
   É o substituto **explícito** da structural aposentada.
7. **Colisão = identidade estrutural, comparada pela AST.** Dois membros de mesmo nome ao achatar:
   **compara-se a AST** dos conflitantes (pós-parse — normaliza espaços/comentários, mas é estrutural:
   identificadores e ordem contam, `a+b` ≠ `b+a`, nome de parâmetro conta). **AST igual → absorve num
   só**; **qualquer diferença → erro de compilação**. Subsume o **diamante** (mesma trait 2× = idêntica
   = absorvida, idempotente) e integra **sobrecarga** (assinaturas diferentes = overload, coexistem; só
   entram na regra de comparação os de mesma assinatura). Resolve o micro-fork struct-vs-trait:
   redefinir um membro com corpo diferente = **erro**, sem override silencioso.
8. **A regra AST-igual é GERAL, não específica de trait.** Onde a composição por `&` **funde membros de
   mesmo nome**, aplica-se: **interface∘interface** (`IFoo & IBar`), **tipo∘(trait/interface)** e a
   composição de **operador-interfaces** (`IEq & IOrd`). Planeia-se a peça do checker como uma **rotina
   de fusão-de-composição PARTILHADA**, não trait-only.
9. **Sem `match` sobre trait:** trait não tem discriminante; nome de trait em *subject* OU *case* de
   match = **erro NOMEADO**. Traits compostas idem.
10. **Constraint = interface-only, SEM exceção.**

### 0.2 A aposentadoria das *structural traits* (inalterada)
11. `Eq`/`Ord`/`Hash`/`Clone`/`Default` (+ sinónimos `Hashable`/`Comparable`) — **APOSENTADAS.** Eram
    *compiler-shadow* (nomes hardcoded cujos corpos o compilador sintetizava campo-a-campo). **Uso real
    em `/src/**/*.tks` = ZERO** (confirmado §5.1). **REMOVER:** a síntese (`synth.tks` inteiro), os
    reconhecedores (`is_structural_trait`/`structural_trait_canonical`, `resolve.tks`), o split de
    derive (`collect.tks`), a satisfação de constraint (`monomorph.tks`). Deleção
    **behavior-preserving** (zero uso vivo).

### 0.3 A capacidade renasce como INTERFACE-com-operador (§6 — depende de §9-ops) (inalterada)
12. `Eq`/`Ord`/… voltam como **interfaces cujo contrato é um operador** (`operator __eq`/`__lt`/…, §9),
    cumpridas ESCREVENDO o operador (visível). O genérico constrange na interface e **despacha o
    operador através de T** (por vtable) — destrava `Map<K: IEq & IHash, V>`.
13. A interface **OBRIGA a contrapartida:** igualdade por negação (`__eq` ⇒ `__ne`), ordem por reflexão
    (`__lt` ⇒ `__gt`; `__le` ⇒ `__ge`). O contrato lista AS DUAS assinaturas; um **trait-mixin opcional**
    (`NeByEq`/`GtByLt`) entrega o corpo óbvio da contrapartida por `&`-compose (zero shadow).

### 0.4 O quadro final — UM construto de capacidade sobra ao lado da interface
| construto | é tipo? | pode constraint? | corpo |
|---|---|---|---|
| **interface** | sim (dispatch) | **sim** | assinaturas (o dev implementa) |
| **trait-mixin** | **não** (achata) | **não** | membros concretos: campos + métodos-com-corpo + const |

---

## 1. Estado de HOJE — o que uma `trait`/`TraitBody` é (file:line)

### 1.1 O `TraitBody` de hoje — perto do mixin, mas com bodyless e sem composição
- **AST** (`src/parser/ast.tks:704`): `pub type TraitBody = struct { fields: []Field; methods:
  []Function; consts: []ConstDecl }`. Carrega **`fields`** (que o modelo REVISTO MANTÉM — ver §3) e as
  `methods` PODEM ser **bodyless** (que o REVISTO REJEITA). NÃO tem `implements` (o modelo REVISTO
  ADICIONA — trait compõe trait/interface). `TypeBody |= TraitBody` (`ast.tks:705`).
- **Parser** (`src/parser/parse_decl.tks`): `trait` é CONTEXTUAL — só declara quando `Ident("trait")` é
  imediatamente seguido de `{` (`parse_decl.tks:1049`), e o comentário `:1048` afirma "A trait declares
  NO derive-list of its own" — **isso muda** (§2). `parse_trait_fields` (`:988`) é STRUCT-SHAPED:
  aceita campos `name: T` interleaved E métodos (**mantém-se** no REVISTO), e chama
  `parse_member_fn_or_accessor(tokens, p, /*allow_bodyless=*/true)` (`:998`) — logo aceita **requisitos
  bodyless** (**rejeita-se** no REVISTO). Aceita `const` membros (`:1004-1008`, mantém-se).
- **Doc-comment velho do AST** (`ast.tks:698-702`) descreve a semântica ANTIGA ("a bodyless one is a
  REQUIREMENT the deriver must satisfy") — reescreve-se para o mixin auto-contido (§3.1).

### 1.2 A trait como TIPO/DISPATCH (TR1) — a REMOVER (§0.1 ruling 3)
- `atom_surface` (`resolve.tks:861`): `is_trait_name` → `trait_methods_by_name` (trait como superfície
  de CONSTRAINT). → **erro nomeado** (§4.5).
- Constraint atom collection (`resolve.tks:1024`): um trait atom contribui o próprio nome como
  constraint. → **erro nomeado** (§4.5).
- Upcast de valor a trait (`resolve.tks:1366`, `:1482`): um struct/class conforma a um trait `to` e faz
  up-cast a ELE como valor (trait-object). → **remover** (§4.5).
- Slice de contrato (`typer.tks:4857`): `is_trait_name` num elemento de tipo → **remover**.
- Arg upcast (`typer.tks:6086`): passar um class onde o parâmetro é um trait → **remover**.
- **`is_trait_name`** (`resolve.tks:1520`): o predicado de kind. **FICA** (o fold e as guardas ainda o
  precisam); os call-sites de TIPO/CONSTRAINT/DISPATCH acima passam a ERRO nomeado ou somem.
- **`iface_methods_by_name`** (`collect.tks:1371`): roteia `TraitBody => trait_methods_by_name`
  (`:1375`). Após §0.1-3 o trait DEIXA de ser contrato de dispatch; esse desvio kind-agnóstico some
  (`trait_methods_by_name` passa a servir SÓ o fold e a checagem de conformidade-carregada §4.3).

### 1.3 O FOLD de user-traits — o que fica (revisto) e o que morre
- **Fica:** `find_trait_body` (`collect.tks:1651`), `trait_methods_by_name` (`:1671`),
  `split_trait_derives` (`:1691`, **sem** o bucket `structural`), `fold_trait_members` (`:1749`),
  `fold_user_traits`/`fold_one_item`/`fold_struct_item`/`fold_class_item` (`:2002`, `:2040`, `:2060`,
  `:2082`).
- **`fold_trait_members` (`:1749`) — REVISTO, não encolhido:**
  - O loop de `tb.fields` (`:1764-1774`) **FICA** (campos mantidos). A colisão de campo (`:1768-1770`,
    hoje erro incondicional) passa a **fusão AST-igual** (§4.4): campo igual → absorve; diferente → erro.
  - O loop de `consts` (`:1776-1784`) fica; a colisão (`:1779-1781`) passa igualmente a fusão AST-igual.
  - O loop de métodos (`:1785-1800`): o `if tm.body.len > 0` (`:1789`) — **hoje** só achata bodied e
    ignora bodyless; após §2 **não há** bodyless (o parser rejeita), logo a guarda simplifica. A colisão
    de método (`:1790-1794`, hoje "own vence, else erro") passa a **fusão AST-igual** (§4.4).
- **Morre (o eixo bodyless-requirement — ZERO membro sem-corpo, §0.1-1):** `TraitDerive` (`:1679`),
  `record_user_trait_derives` (`:2117`), `check_trait_requirements` (`:1811`),
  `one_derive_requirement_diags`/`one_requirement_diag` (o cluster :1811-1892). Um trait sem bodyless
  não tem requisito a satisfazer pelo host.
- **NOVO — trait-compõe-trait:** hoje o fold só corre sobre `implements` de struct/class (o
  `split_trait_derives` sobre `cb.implements`/`sb.implements`). O REVISTO precisa que o `implements` de
  um **trait** também flatten (achatar A/B em C) — ver §4.2. Hoje `TraitBody` nem tem `implements`.

### 1.4 A síntese structural (TR3) — REMOÇÃO INTEGRAL (inalterada face à versão anterior)
- **`src/checker/synth.tks` — FICHEIRO INTEIRO.** É 100% maquinaria TR3:
  `synthesize_structural_methods` (pub, `:632`) + todos os construtores de AST sintética
  (`mk_*`, `:10-228`) + os sintetizadores por-trait (`synthesize_eq`/`synthesize_hash`/… `:342-579`) +
  helpers. **Verificado:** os `mk_*` NÃO têm consumidor fora de `synth.tks` (§5.1) → **deletar o
  ficheiro inteiro** é limpo. Remover do manifesto de build/módulos + qualquer `use teko::checker::synth`.
- **`resolve.tks`:** `is_structural_trait` (`:1539`), `structural_trait_canonical` (`:1553`), e o ramo
  `atom_surface` structural (`:863`) — REMOVER.
- **`monomorph.tks`:** o ramo structural em `constraint_atom_satisfied` (`:56-59`) — REMOVER (fica só o
  caminho interface/variant/nominal).
- **`collect.tks` (PASS 2 structural + TR2 field view):** `SplitImpls.structural` (`:1688`) e o bucket
  no `split_trait_derives` (`:1698-1702`, `:1694`); `FieldView`/`deriver_field_view` (`:1627`) + os
  helpers de field-view; `any_structural` (`:1922-1926`) + o ramo structural em
  `program_needs_trait_fold` (`:1893`); `synthesize_structural`/`synthesize_one_decl` (`:2196`, `:2160`)
  + `struct_with_methods`/`class_with_methods`/`method_name_set`/`append_methods`; a chamada PASS 2 em
  `fold_traits_collected` (`:2302`). O skip structural em `check_conformance` (`:1488`,
  `!is_structural_trait(iname)`) — o predicado some, o skip fica `!is_trait_name(iname, table)` só.

### 1.5 A checagem de conformidade e o match (hoje)
- **Conformidade de interface** (`check_conformance`, `collect.tks:1483`; `check_one_interface`, `:1511`):
  hoje pula nomes que são trait OU structural (`:1488`). Após a onda, trait NUNCA está em `implements`
  como contrato-de-dispatch a validar por assinatura (foi achatado); o skip `!is_trait_name` fica como
  guarda. A CONFORMIDADE-CARREGADA nova (host que compõe `T & IFoo` satisfaz IFoo) reutiliza este mesmo
  `check_conformance` sobre os métodos JÁ ACHATADOS (§4.3).
- **`effective_interface_methods`** (`collect.tks:1328`): concatena os métodos dos `extends` + os
  próprios, **SEM dedup/fusão** de mesmo-nome. É o ponto onde a regra AST-igual de interface∘interface
  (§0.1-8) passa a fundir (§4.4).
- **Match** (`src/checker/match.tks`): `is_direct_case_of` (`:66`) e o caminho de subject/case. Um nome
  de trait pode HOJE aparecer como subject (via TR1 trait-object) ou case; o §0.1-9 exige um **erro
  NOMEADO** em ambas as posições (§4.6).

---

## 2. Gramática / AST — os deltas do mixin (aditivo-restritivo, seed-safe)

**Decisão de forma (law-first):** face à versão anterior, o `TraitBody` **NÃO perde `fields`** — só
**ganha `implements`** (trait-compõe-trait/interface) e as `methods` deixam de aceitar bodyless. O ganho
de `implements` é aditivo; a rejeição de bodyless é um APERTO — mas o corpus atual não tem traits com
métodos bodyless VIVOS (§5.1), logo o seed constrói na mesma. (Janela aditiva: durante o sweep a
gramática velha [bodyless] ainda parseia para o seed velho reler o `src/`; o reseed captura o seed novo;
só então a rejeição entra. Aqui não há `src/` a migrar — nenhum trait de produto usa bodyless — então o
aperto entra direto na crumb 3, sem janela.)

### 2.1 AST (`src/parser/ast.tks:704`)
```teko
/**
 * TraitBody — a `trait [<& composition>] { <concrete members> }` MIXIN body (§9.4 revised): a
 * self-contained, flattenable bundle of CONCRETE members ONLY, composed into a struct/class/service
 * (or into another trait) through the `&`-list. A trait is NOT a type — it never reaches a value,
 * param, return, field, constraint or dispatch position; it resides only in its own declaration and
 * in a composer's `&`-list. It carries FIELDS (the mixin's own state — the `_name` backing a `get
 * name` lives HERE, read as `self._name`, never the property, or it recurses), BODIED methods
 * (instance methods, `get`/`set` accessors, `static fn` factories) and member consts. It carries NO
 * bodyless members (that is the interface's role). Self-containment: every `self.X` a body references
 * MUST be declared by this trait or by a trait it composes (`implements`) — otherwise it is an error
 * IN THE TRAIT (check_trait_self_contained, §4.1). The `implements` list names traits it composes
 * (flattened in, flatten_trait_composition §4.2) and/or interfaces it fully implements
 * (trait-carries-interface, §4.3).
 *
 * @field fields      the mixin's own fields (with optional default), flattened into the composer
 * @field methods     the interleaved bodied instance/accessor/static methods, in source order
 * @field consts      the static member consts (D-consts, §4.7)
 * @field implements  the `&`-composed trait names (flattened in) and/or interface names (carried)
 * @since 0.3.1 (§9.4 revised)
 */
pub type TraitBody = struct { fields: []Field; methods: []Function; consts: []ConstDecl; implements: []str }
```
- `TypeBody` (`ast.tks:705`) mantém `| TraitBody` (a variant não muda de membros; o `TraitBody` **ganha**
  `implements`).
- O codec `.tkb` (C7.16) do `TraitBody` **ganha** o campo `implements` (uma lista de `str`) — round-trip
  em paridade (`src/emit/tkb_*.tks`), teste de round-trip (§6, R-tkb).
- Todos os construtores de `TraitBody` (parser `:1051`, reconstrução `.tkb`, testes) passam a incluir
  `implements`.

### 2.2 Parser (`src/parser/parse_decl.tks`)
- **Reconhecimento contextual + `&`-list (`:1049`):** hoje é `Ident("trait")` + `LBrace` estrito. O
  REVISTO aceita `Ident("trait")` seguido de **um `&`-list opcional** e depois `{`:
  `trait A & B { … }`. A recognição contextual precisa de olhar para além do `trait`: continua a ser
  trait-decl quando `Ident("trait")` é seguido de `LBrace` **OU** de `Amp` (o começo do `& …`). Um
  `type X = trait` alias-transparente continua sem `{`/`&` a seguir. Reutiliza `parse_amp_name_list`
  (o mesmo helper que `struct I1 & I2` usa, `:1055`).
  ```teko
  // (revisto) trait com composição opcional:
  if is_kind_at(tokens, pos, lexer::TokenKind::Ident) && tokens[pos].text == "trait"
     && (is_kind_at(tokens, pos + 1, lexer::TokenKind::LBrace) || is_kind_at(tokens, pos + 1, lexer::TokenKind::Amp)) {
      var impls = match parse_amp_name_list(tokens, pos + 1) { ParsedList<str> as x => x; error as e => return e }
      if !is_kind_at(tokens, impls.next, lexer::TokenKind::LBrace) {
          return err_at(tokens, impls.next, "expected '{' after `trait` (or its `& …` composition list)")
      }
      var fs = match parse_trait_members(tokens, impls.next) { ParsedStructBody as x => x; error as e => return e }
      return Parsed<TypeBody> { node = TraitBody { fields = fs.fields; methods = fs.methods; consts = fs.consts; implements = impls.items }; next = fs.next }
  }
  ```
  (`parse_amp_name_list` sobre um `LBrace` imediato devolve lista vazia — o caso `trait { }` sem
  composição sai por aqui também; confirmar que ele aceita a lista-vazia no `{`.)
- **`parse_trait_fields` → `parse_trait_members` (`:988`):**
  - **MANTÉM campos:** um `name: T` em posição de membro continua a parsear para um `Field` (`:1017`)
    — os campos VOLTAM/ficam. (NÃO se rejeita, ao contrário da versão anterior.)
  - **REJEITAR bodyless:** chamar `parse_member_fn_or_accessor(tokens, p, /*allow_bodyless=*/false)`
    (era `true`, `:998`). Diagnóstico: `"a trait method must have a body — a trait is a concrete mixin,
    not a contract of signatures; a bodyless member belongs on an interface (§9.4)"`.
  - **ACEITAR** `static fn` e `get`/`set` accessors (já roteados por `parse_member_fn_or_accessor`) e
    `const` (já roteado, `:1004`).

### 2.3 Sem tokens novos
`trait` continua contextual (não reservado); `self` continua não-reservado e resolve ao dono em posição
de tipo (o MESMO ponto que os value-type-methods §9 usam — reutiliza). O `&` de composição é o mesmo
`Amp` já usado por `struct I & J`.

---

## 3. O corpo do mixin — campos, self=host, get/set/static (a semântica base)

### 3.1 self=host e o achatamento (reutiliza o fold + o self-como-tipo dos value-type-methods)
O fold já ACHATA métodos de trait no deriver (`fold_trait_members`, `collect.tks:1749`). O corpo achatado
é re-tipado contra o deriver (`type_struct_methods`/`type_method` re-carimba contra o nome corrente),
logo `self._x`/`self` resolvem ao host automaticamente — o MESMO mecanismo `self`-como-tipo dos
value-type-methods (§9). Os campos do mixin achatam via o loop `tb.fields` que FICA (§1.3). Sem trabalho
novo além da fusão-AST (§4.4) — CONFIRMAR por fixture (§5.A1).

### 3.2 Campos do mixin — visibilidade obedece ao kind do host
O loop `:1764-1774` já carimba a visibilidade do campo achatado pelo kind do deriver (`into_class`:
class = privado/encapsulado, struct = público). Um campo de mixin com **default** (o REVISTO permite
`_n: i64 = 0`) precisa do default preservado no `Field` empurrado — CONFIRMAR que o `Field` carrega o
default (se `Field` ainda não tem slot de default, é §9-defaults quem o adiciona; até lá o mixin declara
campo sem default e o host inicializa). Marca-se **[eventual §9-defaults]** só para o default do campo;
o campo em si é independente.

### 3.3 Properties `get`/`set`, static fn — no corpo do mixin
Depende de `plano-secao9-properties.md` (get/set) e do suporte a `static fn` em membro de tipo (§4/§9).
Ambos já ATERRAM antes/junto desta onda. O fold achata um accessor/static como qualquer método bodied;
CONFIRMAR que `fold_trait_members` preserva `is_getter`/`is_setter`/`accessor_prop`/`is_static` do
`Function` ao empurrar (`:1796` já os copia — preserva). Fixture §5.A2/A3.

---

## 4. Checker — a semântica do mixin auto-contido (self-containment + composição + fusão-AST)

### 4.1 Auto-contenção — check_trait_self_contained (NOVO, corre NA TRAIT, §0.1-2)
O antigo `check_trait_composition_contract` (host-provê-backing) **NÃO existe** neste modelo. No lugar,
uma checagem que corre sobre a **própria trait** (na coleta/typing da trait-decl), ANTES de qualquer
composição:
```teko
/**
 * check_trait_self_contained — every `self.X` a trait body references MUST be declared by the trait
 * itself or by a trait it composes (§9.4 self-containment). A trait pointing at a member it neither
 * declares nor composes is visibly wrong — the error is IN THE TRAIT, not at the composing host (the
 * old host-provides-backing contract is retired). For each `self.<member>` FieldAccess and each
 * `self.<method>(…)` MethodCall in the trait's bodies, `<member>`/`<method>` must resolve against the
 * trait's OWN members (fields ∪ methods ∪ consts) UNIONED with every composed trait's flattened
 * members (via flatten_trait_composition, §4.2). The `_name` ↔ property `name` convention is
 * documentation, not enforced spelling: the check is purely "is the referenced member declared in
 * this trait's self-contained surface". Emits a NAMED error citing the trait and the missing member.
 *
 * @param trait_name  the trait being checked (for the diagnostic)
 * @param trait_body  the trait's own members (fields/methods/consts) to scan and to resolve against
 * @param composed    the flattened members of the traits this trait composes (`implements`)
 * @param table       the type table
 * @return            null when every `self.X` is declared, else the first named error
 * @since 0.3.1 (§9.4 revised)
 */
fn check_trait_self_contained(trait_name: str, trait_body: parser::TraitBody, composed: FoldedMembers, table: TypeTable): null | error
```
**Nota de desenho:** um scan sintático de `self.<ident>` (FieldAccess/MethodCall cujo receiver é `self`)
nos corpos da trait, cruzado com a superfície auto-contida (própria + composta). Corre na trait-decl —
independente de qualquer host. Fixar a forma exata da mensagem em Javadoc. Fixture §5.R7.

### 4.2 Trait-compõe-trait — flatten_trait_composition (NOVO, §0.1-5)
`trait C & A & B` achata A e B em C. Reutiliza o mecanismo de fold mas ao nível da trait:
```teko
/**
 * flatten_trait_composition — flatten every trait named in a trait's `implements` list into this
 * trait's effective member surface (§9.4 traits-compose-traits). `trait C & A & B` absorbs A's and
 * B's fields/methods/consts into C so C may reference them (self-containment extends to what C
 * composed). Interface names in `implements` are NOT flattened here (they are carried, not merged —
 * check_trait_carries_interface, §4.3). Same-name members across A and B (or with C's own) are
 * reconciled by the SHARED merge routine (merge_named_members, §4.4): identical AST → absorbed once;
 * any difference → error. The walk is transitive (a composed trait may itself compose) and cycle-
 * guarded by the composing-trait chain, mirroring effective_interface_methods' `seen` set.
 *
 * @param trait_name  the composing trait
 * @param trait_body  its own members + its `implements` list
 * @param table       the type table
 * @param seen        the composing-trait chain (cycle guard)
 * @return            the flattened member surface (own ∪ composed, merged), or a merge/cycle error
 * @since 0.3.1 (§9.4 revised)
 */
fn flatten_trait_composition(trait_name: str, trait_body: parser::TraitBody, table: TypeTable, seen: []str): FoldedMembers | error
```
- No fold do HOST (`fold_trait_members`), quando um `trait_name` é achatado, achata-se a sua **superfície
  efetiva** (`flatten_trait_composition`, transitiva), não só os membros diretos. `split_trait_derives`
  já separa nomes-trait de nomes-interface no `implements` do host; a mesma separação aplica-se ao
  `implements` de uma trait (nomes-trait flatten aqui; nomes-interface são o §4.3).
- `find_trait_body`/`trait_methods_by_name` ganham o parceiro `flatten_trait_composition` para a
  superfície efetiva (análogo ao par `iface_methods_by_name`/`effective_interface_methods`).

### 4.3 Trait-carrega-interface — conformidade em dois níveis (NOVO, §0.1-6)
```teko
/**
 * check_trait_carries_interface — a trait declaring `& IFoo` in its `implements` must FULLY implement
 * IFoo from its own self-contained surface (§9.4 traits-carry-interface): every method IFoo requires
 * (transitively via extends) is provided, by name + matching signature, among the trait's flattened
 * members (flatten_trait_composition, §4.2). This is the TRAIT-LEVEL full-impl check — it runs on the
 * trait decl, so a `trait T & IFoo` that is missing a method fails AT THE TRAIT. Reuses the interface
 * conformance machinery (check_one_interface) with the trait's flattened methods as `own_methods` and
 * `members_all_public = true` (a mixin's methods flatten as the host's public surface).
 *
 * @param trait_name  the carrying trait
 * @param flattened   the trait's flattened member surface (§4.2)
 * @param iface_name  the carried interface (from the trait's `implements`)
 * @param table       the type table
 * @return            null when the trait fully implements the interface, else the conformance error
 * @since 0.3.1 (§9.4 revised)
 */
fn check_trait_carries_interface(trait_name: str, flattened: FoldedMembers, iface_name: str, table: TypeTable): error | null
```
Dois níveis, ambos reutilizando `check_conformance`/`check_one_interface` (`collect.tks:1483`/`:1511`):
1. **Nível-trait (full-impl):** `check_trait_carries_interface` acima. Um `trait T & IFoo` que não
   implementa IFoo inteira erra na trait.
2. **Nível-host (satisfação + joint):** um host que compõe T passa a satisfazer IFoo automaticamente —
   porque após o fold os métodos de T estão ACHATADOS no host, e `check_conformance` corre sobre os
   métodos achatados. Para o **padrão joint** (`NeByEq`): o host declara `& IFoo` e a composição (host +
   traits) fecha o contrato juntos — sai de graça, porque `check_conformance` já vê a UNIÃO dos métodos
   do host e dos seus traits achatados. **Não precisa de código novo** além de garantir que o fold corre
   ANTES do `check_conformance` (já corre — o fold produz a folded_table sobre a qual a conformidade
   tipa). CONFIRMAR a ordem por fixture (§5.A6-joint).

### 4.4 A fusão-de-composição AST-igual — merge_named_members (NOVO, PARTILHADA, §0.1-7/8)
O coração da revisão. Uma rotina **partilhada** por: (a) o fold de traits no host, (b)
`flatten_trait_composition` (trait∘trait), (c) `effective_interface_methods` (interface∘interface), (d)
a composição de operador-interfaces. Substitui a colisão incondicional atual (`collect.tks:1794`) e a
concatenação-sem-dedup de `effective_interface_methods` (`:1348`).
```teko
/**
 * MergeDisposition — the outcome of reconciling two same-name members during any `&` composition
 * (§9.4 AST-equality composition-disambiguation). `Absorb` = identical AST, keep ONE (subsumes the
 * idempotent diamond); `Overload` = same name, DISTINCT signature, keep BOTH (a coexisting overload
 * set — the resolution machinery is §9A); `Conflict` = same name, same signature, DIFFERENT body/AST,
 * a compile error (no silent override, resolves struct-vs-trait redefinition).
 *
 * @since 0.3.1 (§9.4 revised)
 */
type MergeDisposition = variant Absorb | Overload | Conflict

/**
 * functions_ast_equal — STRUCTURAL AST equality of two Function nodes (§9.4). Compares POST-PARSE:
 * whitespace and comments are already normalized away by the parser, but identifiers, operand order
 * (`a+b` ≠ `b+a`), parameter NAMES, statement order, and nested expression shape all COUNT. Location
 * fields (line/col), doc spans, and `vis` are IGNORED (they are not structure). This is the decidable
 * primitive the whole composition-merge rests on; it recurses over params, return type-expr, and the
 * body statement/expression trees.
 *
 * @param a  one member
 * @param b  the other member (same name)
 * @return   true iff the two nodes are structurally identical
 * @since 0.3.1 (§9.4 revised)
 */
fn functions_ast_equal(a: parser::Function, b: parser::Function): bool

/**
 * fields_ast_equal — structural equality of two same-name Field nodes: same declared type-expr (and
 * same default expr, when §9-defaults lands). Two identical private fields absorb into ONE shared
 * field (§9.4 note: absorption is structural, it does not know intent).
 *
 * @param a  one field
 * @param b  the other field (same name)
 * @return   true iff structurally identical
 * @since 0.3.1 (§9.4 revised)
 */
fn fields_ast_equal(a: parser::Field, b: parser::Field): bool

/**
 * merge_named_members — the SHARED composition-merge disposition for a same-name member pair, used by
 * EVERY `&` composition that folds same-name members: type∘trait (fold_trait_members), trait∘trait
 * (flatten_trait_composition), interface∘interface (effective_interface_methods) and operator-
 * interface composition (§9.4 the rule is general, not trait-specific). Decides: identical AST →
 * `Absorb`; distinct signature → `Overload` (coexist — §9A resolves the call site); same signature,
 * different body → `Conflict` (named error). Signature distinctness reuses `method_sig_matches`
 * (arity + non-receiver param types + return); the body/AST comparison is `functions_ast_equal`.
 *
 * @param existing  the member already in the accumulated surface
 * @param incoming  the member being merged in
 * @param table     the type table (for signature resolution)
 * @return          the merge disposition
 * @since 0.3.1 (§9.4 revised)
 */
fn merge_named_members(existing: parser::Function, incoming: parser::Function, table: TypeTable): MergeDisposition
```
**Ligação nos quatro pontos:**
- `fold_trait_members` (`collect.tks:1785-1800`): substituir o ramo de colisão `:1790-1794` por
  `merge_named_members`: `Absorb` → não empurrar (já lá está, idêntico); `Overload` → empurrar ambos
  **[metade BLOQUEADA em §9A]**; `Conflict` → erro nomeado. O ramo "own vence" (`:1790-1792`) deixa de
  ser um caso especial: o método próprio do host e o do trait passam pela MESMA fusão — igual absorve,
  diferente-mesma-assinatura erra (redefinir com corpo diferente = erro, §0.1-7), assinatura-distinta =
  overload. Campos (`:1768-1770`) e consts (`:1779-1781`) usam `fields_ast_equal` (absorve/erra; consts
  análogo).
- `flatten_trait_composition` (§4.2): mesma rotina ao fundir A∘B.
- `effective_interface_methods` (`collect.tks:1348`): ao concatenar métodos de `extends`, fundir
  mesmo-nome por `merge_named_members` (interface∘interface — duas assinaturas homónimas idênticas viram
  um contrato; conflitantes erram; distintas coexistem).
- Composição de operador-interfaces (§6): sai de graça de (c), pois `IEq & IOrd` é interface∘interface.

**Dependência de §9A (honesta):** o ramo `Overload` (coexistir assinaturas distintas) só é ÚTIL quando
`select_overload` de §9A resolve o call-site entre elas. Até lá, `merge_named_members` é totalmente
especificável, mas o ramo `Overload` reporta-se conservadoramente como `Conflict` (comportamento atual —
byte-idêntico) OU o call-site erra. A metade `Absorb`/`Conflict` (mesma-assinatura) é INDEPENDENTE e
adianta-se já. **[ramo Overload BLOQUEADO em §9A]**.

### 4.5 Constraint = interface-only (§0.1-10) — trait em constraint/tipo = ERRO nomeado
- `atom_surface` (`resolve.tks:861`): o ramo `if is_trait_name(name, table) { return
  trait_methods_by_name(...) }` passa a **erro nomeado**: `"a trait is not a type and cannot appear in a
  constraint — use an interface (§9.4)"`.
- Constraint atom collection (`resolve.tks:1024`): o ramo trait → mesmo erro nomeado.
- `constraint_atom_satisfied` (`monomorph.tks`): sem o ramo structural (removido §4/§1.4) e sem trait
  (inalcançável — `atom_surface` já rejeitou). Guarda defensiva opcional.
- Trait em posição de TIPO (`typer.tks:4857`, `:6086`; upcast `resolve.tks:1366`/`:1482`): remover os
  ramos que aceitam um trait como tipo/slice-element/arg-upcast → o nome resolve a "não é tipo" (§5.R4).

### 4.6 Sem match sobre trait (§0.1-9) — guarda NOMEADA em subject E case
```teko
/**
 * reject_trait_in_match — a trait has no discriminant (§9.4): a trait name in a match SUBJECT or in a
 * left-of-arm CASE is a NAMED error, never a silent no-match. Called on the resolved subject type and
 * on each arm's resolved discriminant; a composed trait (flattened away) never reaches here as a
 * Named trait, so this fires only on a direct `trait` name a user wrote in a match position.
 *
 * @param t      the resolved subject or case type
 * @param table  the type table
 * @param where  "subject" or "case" — placed into the diagnostic
 * @return       null when `t` is not a trait, else the named error
 * @since 0.3.1 (§9.4 revised)
 */
fn reject_trait_in_match(t: Type, table: TypeTable, where: str): null | error
```
Ligar no typing do subject (antes de `expand_variant`) e em `is_direct_case_of` (`match.tks:66`) / o
resolve de cada case: se `is_trait_name(n.name, table)`, erro `"cannot match on trait `<name>` — a trait
has no discriminant (§9.4)"`.

### 4.7 D-consts (decisão ratificável) — member consts num trait
§9.4 lista os concretos como "campos, métodos-com-corpo e `const`s" — o REVISTO **inclui explicitamente
`const`** ("um `const` não é campo, é comp-time; sempre coube"). **MANTER** member consts no trait
(a maquinaria #594 já os achata; a colisão passa a fusão-AST §4.4). Sem tensão — o REVISTO ratifica.

---

## 5. A REMOÇÃO da maquinaria structural + fixtures

### 5.1 Prova de "zero uso vivo" (o que torna a remoção behavior-preserving)
- `grep -rE "&\s*(Eq|Ord|Hash|Clone|Default|Hashable|Comparable)\b" src/ --include=*.tks` → **só
  doc-comments** (`monomorph.tks`, `map.tks`). Nenhum `derive & Eq…` VIVO, nenhuma chamada
  `.eq()`/`.compare()`/`.hash()`/`.clone()`/`::default()` de origem structural. O `Map` já é `str`-keyed
  (o que a interface-operador §6 vem destravar).
- `mk_*`/`synthesize_*` de `synth.tks`: sem consumidor externo → deleção de ficheiro limpa.
- **VERIFICAR no inventário (crumb 1):** trait-como-tipo/constraint VIVO (`: <TraitName>` em
  param/retorno/campo/var; `<T: <TraitName>>`); trait com **bodyless** VIVO (o REVISTO já não os
  proíbe-por-campo — campos ficam — só bodyless). Se algum existir: migrar bodyless → interface na
  janela aditiva OU reportar para cima (adjacente; NÃO abro issue).

### 5.2 Ordem de deleção (cada passo isolado, build verde entre eles — uso zero)
1. **`monomorph.tks`** — o ramo structural em `constraint_atom_satisfied` (`:56-59`).
2. **`collect.tks`** — remover `synthesize_structural` de `fold_traits_collected` (`:2302`), depois as
   funções PASS 2 (`synthesize_structural`/`synthesize_one_decl` + `*_with_methods`), depois
   `FieldView`/`deriver_field_view` + helpers de field-view, `any_structural`, o bucket
   `SplitImpls.structural` + o ramo no `split_trait_derives`, o ramo structural em
   `program_needs_trait_fold`. `check_trait_requirements` + cluster morre (§1.3, bodyless-requirement).
3. **`resolve.tks`** — `is_structural_trait` (`:1539`), `structural_trait_canonical` (`:1553`), o ramo
   `atom_surface` structural (`:863`). Ajustar o skip em `check_conformance` (`:1488`) para
   `!is_trait_name` só.
4. **`synth.tks`** — deletar o FICHEIRO INTEIRO + removê-lo do build/módulos + qualquer `use`.
5. **Testes** (`checker_test.tkt`) — deletar `structural_*`/`field_view_*` + helpers órfãos (§7).

**Prova behavior-preserving:** nenhum derive/chamada structural vivo, `mk_*` sem consumidor externo. O
self-build produz árvores idênticas exceto onde a maquinaria morta corria (nunca corria). Fixpoint fecha.

### 5.3 Fixtures de regressão (inputs → códigos de saída nativos)
Layout (§9B): ACEITAR = projeto `examples/regressions/<nome>/` (`.tkp`, `.tkr`, `main.tks`); REJEITAR =
`examples/regressions/diagnostics/` com `src/<caso>/case.tks` + `Then diagnostic = "…"`.

**ACEITAR — `examples/regressions/trait_mixin/` (independente de §9):**
- **A1 — flatten de método + self=host.** `trait Stamp { fn tag(): i64 { self._id * 10 } }` composto em
  `type Post = struct Stamp { _id: i64 }`. `Post { _id = 4 }.tag()` → `exit 40`. Prova achatamento +
  `self._id` a resolver ao host. (Nota: o backing `_id` mora no HOST aqui; ver A1b para o backing NA
  trait.)
- **A1b — backing NA trait (auto-contido).** `trait Counter { _n: i64; fn bump(): i64 { self._n = self._n
  + 1; self._n } }` composto num struct. O `_n` vem DA TRAIT (achatado). Prova §0.1-1/2 (campo do mixin +
  auto-contenção satisfeita). `exit` do contador.
- **A2 — property get/set sobre backing da trait.** `trait Named { _name: str; get name(): str
  { self._name }; set name(v: str) { self._name = v } }` — backing E accessor ambos NA trait. Ler/
  escrever → token. **[depende de §9-properties]**
- **A3 — static fn factory.** `trait Zeroed { _n: i64; static fn zero(): self { self { _n = 0 } } }`.
  `Post::zero()._n` → `exit 0`. Prova `static fn` + `self`-como-tipo.
- **A4 — diamante idempotente (absorve).** `trait A { fn k(): i64 { 7 } }`; `trait B & A { }`;
  `trait C & A { }`; host compõe `& B & C` → `A::k` chega por dois caminhos, AST idêntica → **absorve**,
  sem erro. `host.k()` → `exit 7`. Prova §0.1-7 (diamante idempotente pela fusão-AST).
- **A5 — trait-compõe-trait.** `trait Base { _v: i64; fn v(): i64 { self._v } }`; `trait Ext & Base { fn
  twice(): i64 { self.v() * 2 } }` — `Ext` referencia `self.v()` de `Base` que compôs (auto-contenção
  estende-se ao composto). Host compõe `& Ext`. `exit` de `twice`.
- **A5b — compose em `service`.** `service S singleton & SomeTrait { … }` achata o método do trait.
  Prova §0.1-5 (achata em service). `exit` codifica o método. CONFIRMAR que um service flui pelo fold
  (usa `ClassBody`; se não chega, estender `fold_one_item`).
- **A6 — trait carrega interface + capacidade por vtable** `examples/regressions/capability_iface/`.
  `trait EqPoint & IEq { … escreve __eq … }` + `index_of<T: IEq>` sobre `[]Point` com `Point & EqPoint`.
  Acha o índice → token. Prova a conformidade-carregada (§4.3) + o dispatch REAL por vtable. **[BLOQUEADO
  em §9-ops]** — esqueleto `.tkr`/`.tks` escreve-se já; corre quando §9 fecha.
- **A6-joint — padrão joint (NeByEq).** `Point & IEq & NeByEq` — o host declara `& IEq`, escreve `__eq`,
  e o `NeByEq` contribui `__ne`; a composição fecha IEq juntos. Prova §4.3 nível-host joint. **[BLOQUEADO
  em §9-ops]**.

**REJEITAR — `examples/regressions/diagnostics/` (a maioria independente de §9):**
- **R1 — no-match-over-trait (subject).** `Then diagnostic = "cannot match on trait"` (§4.6).
- **R2 — no-match-over-trait (case).** `Then diagnostic = "has no discriminant"` (§4.6).
- **R3 — trait em constraint.** `fn f<T: SomeTrait>(x: T) { }` → `"cannot appear in a constraint"` (§4.5).
- **R4 — trait em posição de tipo.** `fn f(x: SomeTrait) { }` → `"not a type"` (§0.1-3).
- **R5 — método bodyless num trait.** `trait Bad { fn k(): i64 }` (sem corpo) → `"must have a body"`
  (§2.2). (NÃO há mais R "campo num trait" — campos são LEGAIS agora.)
- **R6 — auto-contenção violada.** `trait Bad { fn k(): i64 { self._x } }` sem declarar `_x` (nem via
  compose) → `Then diagnostic` NOMEADO citando a trait + `_x` (§4.1). Prova o erro-NA-TRAIT.
- **R7 — colisão AST-diferente (mesma assinatura, corpo diferente).** `trait A { fn k(): i64 { 1 } }`;
  `trait B { fn k(): i64 { 2 } }`; host compõe `& A & B` sem redefinir `k` → `Then diagnostic` de
  conflito (AST diferente, mesma assinatura) exigindo o host resolver (§4.4). Prova §0.1-7 (diferença →
  erro, sem override silencioso).
- **R8 — redefinir membro de trait com corpo diferente.** host compõe `& A` (com `fn k(): i64 { 1 }`) e
  define o SEU `fn k(): i64 { 2 }` → **erro** (mesma assinatura, AST diferente), NÃO override silencioso
  (§0.1-7). (Contraste com o antigo modelo, onde o host vencia.)
- **R9 — trait-carrega-interface incompleta.** `trait T & IFoo { }` sem implementar todos os métodos de
  `IFoo` → `Then diagnostic` de conformidade AT THE TRAIT (§4.3 nível-trait). **[BLOQUEADO em §9-ops se
  IFoo for operador-interface; usar uma IFoo de método comum para adiantar]**.

**Nota axis-law:** a aritmética do `exit`/token codifica QUAL ramo correu (testa o token/exit, nunca um
efeito incidental).

---

## 6. A camada INTERFACE-OPERADOR + contrapartida (§0.3) — **[BLOQUEADO em §9-ops]**

Assenta sobre `operator __eq`/`__lt`/… (o dispatch de dunder de `plano-secao9-operadores-e-value-type-
methods.md`). Adianta-se todo o desenho que NÃO precisa da API de operador fechar. É corpus/stdlib `.tks`
(NÃO maquinaria de compilador) — não afeta o auto-fixpoint.

### 6.1 As interfaces de capacidade (o contrato lista AS DUAS assinaturas)
```teko
/**
 * IEq — the equality capability: a type conforms by WRITING the `__eq` operator (§9.4). The
 * counter-part `__ne` is MANDATORY (equality by negation, ruling 13); a conformer either writes it or
 * composes the `NeByEq` mixin for the obvious body. A generic `<T: IEq>` dispatches `==`/`!=` through
 * T's vtable — the real dispatch the retired structural `Eq` could never do.
 *
 * @since 0.3.1 (§9.4)
 */
type IEq = interface {
    operator __eq(left: self, right: self): bool
    operator __ne(left: self, right: self): bool
}

/**
 * IOrd — the ordering capability (§9.4). Each comparator obliges its REFLECTION: `__lt`⟂`__gt`
 * (swapped operands), `__le`⟂`__ge`. `<T: IOrd>` dispatches ordering through T.
 *
 * @since 0.3.1 (§9.4)
 */
type IOrd = interface {
    operator __lt(left: self, right: self): bool
    operator __gt(left: self, right: self): bool
    operator __le(left: self, right: self): bool
    operator __ge(left: self, right: self): bool
}
```

### 6.2 Os trait-mixins opcionais de contrapartida (o corpo óbvio, zero shadow)
```teko
/**
 * NeByEq — the optional counter-part mixin for `IEq` (§9.4): flattens the obvious `__ne` body
 * `!(left == right)` into a composer that already writes `__eq`, so the type satisfies `IEq`'s
 * mandatory counter-part without hand-writing it. Self-contained: `__ne`'s body references only the
 * `==` operator on `self`, which the composing host provides. A type wanting a non-trivial `__ne`
 * (e.g. NaN) writes its own instead of composing this — no compiler magic.
 *
 * @since 0.3.1 (§9.4)
 */
type NeByEq = trait {
    operator __ne(left: self, right: self): bool { !(left == right) }
}

/**
 * GtByLt — flattens `__gt(l, r) = r < l` (ordering by reflection) into an `IOrd` composer that writes
 * `__lt`. Sibling mixins `LeByLt`/`GeByLt` supply `__le`/`__ge` by the same reflection.
 *
 * @since 0.3.1 (§9.4)
 */
type GtByLt = trait {
    operator __gt(left: self, right: self): bool { right < left }
}
```
**Nota:** estes mixins só compilam quando `operator` (membro) + `self`-como-operando existem —
**[BLOQUEADO em §9-ops]**. O ESQUELETO (`.tks` com os doc-comments + as declarações) escreve-se HOJE
como honest-stop se `operator` ainda não parseia; caso contrário entra como corpus real quando §9 fecha.

### 6.3 Conformidade da contrapartida — a interface OBRIGA as duas assinaturas
- **Na interface:** `IEq`/`IOrd` listam ambas as assinaturas; `check_one_interface` já exige TODAS —
  um tipo com `__eq` sem `__ne` (nem via `NeByEq`) FALHA a conformidade. **Sem código novo** além de
  escrever as interfaces §6.1. Com o padrão joint (§4.3), o `NeByEq` composto contribui o `__ne` e a
  conformidade fecha.
- **No operador (§9, `check_operator_invariants`):** o `plano-secao9…` obriga a contrapartida ao nível
  do OPERADOR (quem DEFINE). Complementar, sem duplicação.

### 6.4 O que a §6 destrava
`Map<K: IEq & IHash, V>` genérico: o constraint interface despacha `==`/`hash` através de K por vtable —
o que a structural (opaca-em-T) nunca conseguiu. `IEq & IHash` é interface∘interface, fundida pela
rotina partilhada §4.4. Fixture A6.

---

## 7. Segurança de FIXPOINT + sweep `.tkt`/`.tkr`

### 7.1 O argumento de fixpoint (byte-idêntico)
- **Remoção structural (§5):** zero derive vivo, zero chamada de método structural, `mk_*` sem
  consumidor externo. O código removido NUNCA corria → `bin-a` idêntico nas rotas vivas.
- **TR1-dispatch (trait como tipo/constraint):** o corpus do compilador usa trait-como-tipo/constraint
  VIVO? **VERIFICAR** (crumb 1): grep por `: <TraitName>` e `<T: <TraitName>>`. Se houver, migrar para
  interface na janela aditiva OU reportar para cima. O desenho ASSUME zero.
- **Aperto de gramática (§2):** `TraitBody` agora GANHA `implements` (aditivo, seed-safe) e as `methods`
  perdem bodyless (aperto). Campos FICAM (sem aperto aí, ao contrário da versão anterior). Se algum trait
  de PRODUTO usa método bodyless VIVO, o parser novo rejeita-o. **VERIFICAR** (crumb 1); um bodyless vivo
  migra para a interface correspondente. **Este é o maior risco de fixpoint** — resolvido por inventário.
- **Codec `.tkb` (R-tkb):** o `TraitBody` GANHA `implements` → round-trip tem de bater (crumb 2/3).

### 7.2 Sweep `.tkt`/`.tkr`
- **`.tkt`** (`src/checker/checker_test.tkt`): deletar `structural_*`/`field_view_*` + helpers órfãos
  (`trait_item`, `tr_fields1`, `fv_*`). **Atualizar** testes de trait que construam um `TraitBody` — o
  construtor agora exige `implements` (novo campo). Deletar/atualizar testes que exercitem
  trait-como-constraint/valor (TR1) ou trait com método bodyless. NÃO deletar testes que usem trait com
  CAMPOS — campos são legais.
- **`.tkr`** (`examples/regressions/`): fixtures novos §5.3 entram como projetos novos. Se algum `.tkr`
  existente usa `& Eq…`/trait-como-tipo/trait-bodyless, migrar/remover.
- **`bootstrap/teko.c`** (gémeo C CONGELADO): contém `is_structural_trait`/`synthesize_structural`. NÃO
  se edita o gémeo congelado; a divergência é aceitável até o reseed capturar o seed novo a partir do
  `.tks` (o seed velho ainda tem a maquinaria, mas o `.tks` já não a chama → inerte). Documentar no
  PROVENANCE.

---

## 8. Sequência de crumbs (ordenada; cada uma gate-ável isoladamente) + RITUAL + reseed

Pontos de RITUAL (gate completo) marcados. Sequenciada por dependência de SEED: inventário → remoção de
código-morto → AST+parser → semântica do mixin → fusão-AST → guardas → (bloqueado) camada operador →
fixtures → reseed. O bloco INDEPENDENTE (1-9) fecha SEM esperar §9; o §9-dependente (10-11) entra depois.

1. **Inventário (read-only, sem edição de produto).** Grep vivo de: `& Eq/Ord/Hash/Clone/Default`;
   trait-como-tipo (`: <TraitName>`); trait-como-constraint (`<T: <TraitName>>`); `trait {` com **método
   bodyless** (campos são OK — não inventariar); chamadas `.eq()/.compare()/.hash()/.clone()/::default()`
   structural. **Produz a lista de migração** (esperada VAZIA). Se não-vazia: migrar para interface na
   janela aditiva OU reportar para cima. — *fundamento do fixpoint §7.*
2. **Remoção da maquinaria structural (§5.2) + sweep de teste (§7.2).** `monomorph` → `collect` (PASS 2 +
   TR2 + bodyless-requirement) → `resolve` (`is_structural_trait`/`canonical`/`atom_surface`) → deletar
   `synth.tks` inteiro → deletar `structural_*`/`field_view_*` tests. — *behavior-preserving.*
   **RITUAL: gate completo.**
3. **AST + parser do mixin (§2).** `TraitBody` GANHA `implements`; codec `.tkb` em paridade + teste de
   round-trip; `parse_trait_members` rejeita bodyless (mantém campos), aceita `&`-list de composição,
   static/get/set/const. Todos os construtores de `TraitBody` passam `implements`. — *aditivo
   (`implements`) + aperto (bodyless) sobre zero-uso-vivo.* **RITUAL: gate completo** (toca tipo central
   + codec).
4. **Semântica base + fusão-AST (§3, §4.4).** `functions_ast_equal`/`fields_ast_equal`/
   `merge_named_members`/`MergeDisposition`; ligar em `fold_trait_members` (campos/consts/métodos) e em
   `effective_interface_methods` (interface∘interface). A metade `Absorb`/`Conflict` adianta-se; o ramo
   `Overload` reporta `Conflict` até §9A. Confirmar `self`/get/set/static/campos preservados no push.
   — *a peça partilhada; inerte fora de composição.* **RITUAL: gate completo** (toca a fusão de
   interface também).
5. **Trait-compõe-trait + auto-contenção (§4.1, §4.2).** `flatten_trait_composition` (transitiva,
   cycle-guarded) + `check_trait_self_contained` (scan de `self.X` na trait). Ligar o
   `flatten_trait_composition` no fold do host (achata a superfície efetiva do trait). — *dispara na
   trait-decl + na composição.*
6. **Trait-carrega-interface (§4.3).** `check_trait_carries_interface` (nível-trait, reutiliza
   `check_one_interface`); confirmar nível-host (fold antes de `check_conformance`, joint sai de graça).
   — *reutiliza a conformidade existente.*
7. **Guardas de não-tipo (§4.5, §4.6).** trait-em-constraint → erro nomeado (`atom_surface` + constraint
   collection); `reject_trait_in_match` em subject e case; remover TR1-dispatch
   (trait-como-valor/slice/arg-upcast — `resolve.tks:1366/1482`, `typer.tks:4857/6086`). — *o inventário
   (crumb 1) confirmou zero uso vivo a cortar.* **RITUAL: gate completo** (remove superfície de tipo).
8. **Fixtures ACEITAR independentes (§5.3 A1/A1b/A3/A4/A5/A5b) + REJEITAR independentes (R1-R9 exceto os
   marcados §9-ops).** Primeiro mixin REAL no corpus de teste. **RITUAL.**
9. **Reseed + PROVENANCE** do bloco independente (§9). **RITUAL.** — *fecha a onda mixin + remoção
   structural SEM esperar §9.*

— fronteira de dependência: as crumbs abaixo esperam `plano-secao9-operadores…` (e §9A para o overload) —

10. **[BLOQUEADO em §9-ops]** Camada interface-operador (§6): escrever `IEq`/`IOrd` + os mixins
    `NeByEq`/`GtByLt` como corpus `.tks`; conformidade obriga contrapartida via a estrutura de interface
    existente + o padrão joint (§4.3). Fixtures A2 (properties), A6, A6-joint, R9-operador passam a correr.
11. **[BLOQUEADO em §9A]** Ligar o ramo `Overload` de `merge_named_members` (§4.4) a `select_overload`;
    reseed final se algo tocar superfície do compilador (não deve — §6 é corpus/stdlib).

### 8.1 Ritual de reseed + PROVENANCE (crumb 9 / final)
Só depois de todas as crumbs do bloco verde e do gate completo:
1. `cc -std=c2x -w -O2 -I src/runtime -I src/assert bootstrap/teko.c src/runtime/teko_rt.c
   src/assert/assert.c -lm -o gen0`
2. `TEKO_BACKEND=c ./gen0 build . --no-verify --release` → `bin-a`.
3. Re-build com `bin-a` como seed → `bin-b`. **Fixpoint: `bin-a == bin-b`** byte-a-byte
   (`scripts/fixpoint_gate.sh`). Remoção = código-morto + aperto sobre zero-uso-vivo ⇒ fecha na crumb 8.
4. Harvest: `bootstrap/teko.c` (novo seed, sem `synthesize_structural`/`is_structural_trait`) +
   `bootstrap/PROVENANCE` (novo hash, notando a remoção structural + o `implements` no `TraitBody` +
   a fusão-AST).
5. **NUNCA correr `teko test .`** (fuga de memória do `monomorph` — crasha o container). Gate por
   `--no-verify` + os `scripts/*.sh`.

---

## 9. Riscos + tensões de lei (com resolução recomendada)

- **R-TR1-uso-vivo (o maior risco de fixpoint).** O §9.4 confirma "zero uso vivo" SÓ para a structural;
  a parte "trait não é tipo/constraint" (TR1) precisa da mesma prova antes de cortar os call-sites.
  **Resolução:** a crumb 1 (inventário) é PRÉ-REQUISITO — migra-se para interface OU reporta-se para cima
  (adjacente; NÃO abro issue). Sem tensão de lei.
- **R-bodyless de produto.** Traits de produto podem ter métodos bodyless (o modelo velho "contrato de
  assinaturas" TINHA bodyless). **Resolução:** inventário (crumb 1); um bodyless vivo migra para a
  interface correspondente. Sem tensão de lei. (Campos JÁ NÃO são risco — o REVISTO mantém-nos.)
- **R-fusão-AST cobertura (`functions_ast_equal`).** A equality estrutural tem de cobrir TODA a forma do
  `Function` (params, nomes de param, return-expr, corpo recursivo) e IGNORAR line/col/doc/vis. **Risco:**
  uma comparação incompleta absorve dois membros semanticamente distintos (falso-igual) ou erra dois
  idênticos (falso-diferente). **Resolução:** especificar `functions_ast_equal` exaustivo sobre a AST;
  fixtures A4 (absorve idêntico), R7 (erra diferente-mesma-assinatura) fecham as duas pontas. Sem tensão
  de lei — é rigor de implementação. É o item de MAIOR risco técnico da onda.
- **R-fusão partilhada (regressão em interface∘interface).** Ligar `merge_named_members` a
  `effective_interface_methods` muda o comportamento de duas interfaces com método homónimo (hoje
  concatena os dois; passa a fundir/errar). **Resolução:** confirmar no inventário que nenhuma interface
  do corpus tem `extends` com método homónimo divergente VIVO; se houver, é erro legítimo a corrigir ou
  reportar. A concatenação-dupla atual nunca foi observada (o vtable slot assume nomes únicos). Sem
  tensão de lei; verificar na crumb 4.
- **R-tkb (codec).** `TraitBody` GANHA `implements` → round-trip tem de bater. **Resolução:** crumb 3 em
  paridade + teste de round-trip. Sem tensão.
- **R-service-no-fold.** §0.1-5 exige achatar em `service`. **Resolução:** confirmar que um service
  (ClassBody) flui por `fold_class_item`; se não, estender `fold_one_item`. Fixture A5b. Sem tensão.
- **R-campo-default (§3.2).** Um campo de mixin com default depende de §9-defaults ter o slot no `Field`.
  **Resolução:** o campo SEM default é independente; o default marca-se **[eventual §9-defaults]**. Sem
  tensão.
- **R-dependência-§9-ops (a camada interface-operador §6).** `NeByEq`/`IEq`/… só compilam com `operator`
  membro + `self`-como-operando. **Resolução:** o bloco independente (crumbs 1-9) fecha a onda mixin +
  remoção structural SEM §9; a §6 entra como corpus quando §9 aterra (crumb 10). Esqueleto `.tks` +
  doc-comments escreve-se já. **[BLOQUEADO em §9-ops]**, sequenciado à volta.
- **R-ramo-overload (§4.4).** O ramo `Overload` de `merge_named_members` consome `select_overload` de
  §9A. **Resolução:** a metade `Absorb`/`Conflict` (mesma-assinatura) adianta-se; o ramo `Overload`
  reporta `Conflict` (comportamento atual, byte-idêntico) até §9A ligar (crumb 11). **[BLOQUEADO em §9A]**
  só para esse ramo.
- **Sem tensão de lei genuína identificada — NENHUM HALT necessário.** Todos os rulings do §9.4 revisto
  encaixam num desenho law-first coerente: a remoção é código-morto provado, o mixin reutiliza o fold + o
  `self`-como-tipo dos value-type-methods, a fusão-AST é uma rotina partilhada decidível, a capacidade
  renasce sobre a interface+operador já planeados. As três dependências (§9-ops para §6; §9A para o ramo
  overload; §9-defaults para o default de campo) são de SEQUÊNCIA, não de lei.

---

## 10. O que fica BLOQUEADO (resumo honesto para o implementer)

Fecha JÁ, sem esperar ninguém: crumbs 1-9 — inventário, remoção integral da structural, o `TraitBody` a
ganhar `implements` (mantendo campos), rejeição de bodyless, a rotina de **fusão-AST partilhada**
(`functions_ast_equal`/`merge_named_members`, ligada a traits E a interface∘interface), auto-contenção
(`check_trait_self_contained`), trait-compõe-trait (`flatten_trait_composition`), trait-carrega-interface
(nível-trait + nível-host joint), as guardas trait-não-é-tipo (constraint/match/valor), a metade
`Absorb`/`Conflict` da fusão, os fixtures independentes (§5.3 A1/A1b/A3/A4/A5/A5b, R1-R9 exceto o ramo
§9-ops), e o reseed do bloco.

Fica BLOQUEADO até `plano-secao9-operadores-e-value-type-methods.md` aterrar: a camada interface-operador
(§6 — `IEq`/`IOrd` + `NeByEq`/`GtByLt`, os fixtures A6/A6-joint `capability_iface`, A2 properties,
R9-operador). O esqueleto `.tks` + doc-comments escreve-se hoje. Fica BLOQUEADO até
`plano-secao9A-method-overload.md` aterrar: o ramo `Overload` de `merge_named_members` (§4.4) e os
fixtures de overload. Fica marcado **[eventual §9-defaults]**: o default de um campo de mixin (o campo
sem default é independente).
