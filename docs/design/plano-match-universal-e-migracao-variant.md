# Plano — `match` universal + migração dos `variant` nomeados para hierarquias sealed

> DESIGN-AHEAD. Documento de projeto (não implementação). A fase descrita aqui será
> implementada DEPOIS que os itens pequenos do §9 aterrissarem. Branch de contexto:
> `fix/retirement`. Disciplina de reseed: `cc -std=c2x`, `--no-verify`, nunca `teko test .`.
>
> Autoria: arquiteto do time. Escopo: investigar + especificar. Nenhum `.tks` de produto foi
> tocado. Todas as citações são `arquivo:linha` reais na árvore atual.

---

## 0. As duas partes acopladas

- **Parte 1 — `match` universal (a fundação).** Ampliar `match` para discriminar sobre QUALQUER
  tipo (primitivo, struct, enum, flag, service, interface, sum) sob o modelo SEALED de arms
  (abaixo, verbatim). Migrar os padrões de VALOR-literal na esquerda para guardas `when`.
- **Parte 2 — migrar os `variant` nomeados para hierarquias de classe sealed.** Regra do dono:
  nenhum `type` pode ser união nomeada — nem `type X = variant …` nem `type X = A | B`. Uniões
  ANÔNIMAS `A | B` permanecem em posição de var/param/constraint/return e `match` continua
  funcionando sobre elas.

A Parte 1 é pré-requisito da Parte 2: é ela que deixa `Type`/`MInst`/… manterem `match`
exaustivo depois de deixarem de ser `variant`.

---

## 1. Modelo SEALED de `match` (rulings do dono — VINCULANTES, projetar EM TORNO deles)

> Registro durável — copiado verbatim. Toda a engine desta fase obedece a isto.

Todo arm de match: `<discriminant> [as <bind>] [when <cond>] => <body>`.

- **Esquerda = discriminante, SEMPRE um teste-de-TIPO** (nunca um literal de valor): um arm de sum
  (`Prim as p`), um membro de enum (`U8`), uma subclasse sealed (`Sub1 as s`), uma impl concreta de
  interface/service (`ConcreteA as a`). Um struct/primitivo (sem sub-discriminante) usa o próprio
  tipo na esquerda (`i32 as x`), que sempre casa.
- **`when <cond>` = guarda de VALOR (como um `if`), opcional.** TODA asserção de valor mora aqui —
  inclusive `match n { i32 as x when x == 0 => … }`. Padrões de literal-de-valor na ESQUERDA
  (`0 =>`) NÃO EXISTEM — viram `<tipo> as x when x == <lit>`.
- **`match` deve discriminar sobre QUALQUER tipo:** primitivo, struct, enum, flag, service,
  interface, sum. Flag: o tipo-flag na esquerda, testes de bit via `when` (`F as x when x.has(F::A)`).
- **Catch-all `_ [when cond] =>` e bind-all `Tipo as x [when cond] =>`** — ambos permitidos, ambos
  podem carregar um `when`.
- **Exaustividade:** sobre um conjunto FECHADO (arms de sum / membros de enum / subclasses sealed)
  todo caso deve ser coberto; um arm que carrega `when` NÃO conta para cobrir o seu caso (a guarda
  pode ser falsa), então qualquer match que use `when` exige um fallback catch-all/bind-all SEM
  guarda. Sobre um conjunto ABERTO (interface com impls desconhecidas, espaço de valor primitivo) um
  catch-all/bind-all sem guarda é obrigatório.
- **O bind `as` é por REFERÊNCIA — alias transparente do subject, não cópia** (ruling do dono). `Tipo as t`
  liga `t` como um **alias-ref** do subject (nunca copia — o `as` copiando era o desperdício a matar). O
  readonly é **raso**: a variável `t` é imutável (não re-vincula; `t = outro` é erro), mas **props e métodos
  internos são transparentes** — através de `t` faz-se exatamente o que se faria mexendo no subject direto,
  nem mais nem menos. A mutabilidade **herda do subject**:
  - **classe** (qualquer binding) → `t.prop = v` OK (reference type, objeto mutável por identidade);
  - **value-struct `var s`** → `t.prop = v` OK, muta `s` **in-place** (item 14);
  - **value-struct `val s`** / **temporário / expressão** (`match foo()`) → escrita barrada, pela
    imutabilidade do próprio valor, não pelo alias.

  Sem sintaxe nova (nada de `ref T as t`). `t` é o único alias vivo do subject durante o braço; o
  borrow-checker garante que o subject não seja tocado por outro caminho ali dentro.

---

## 2. `match` hoje — como está, o que muda

Arquivos-âncora (todos Teko, os twins C estão CONGELADOS):
`src/parser/pattern.tks`, `src/parser/parse_pattern.tks`, `src/parser/parse_arm.tks`,
`src/checker/match.tks`, `src/lir/lower.tks` (lowering do tag).

### 2.1 AST de padrão (hoje)
`src/parser/pattern.tks:32` — `Pattern = variant LiteralPattern | RangePattern | AltPattern |
BindPattern | FieldPattern | WildcardPattern | NullPattern` (7 arms). O arm em si é
`Arm = struct { pattern; has_when; guard; body }` (`pattern.tks:34-40`), já com `has_when`/`guard`.

- `LiteralPattern { value: Expr }` (`pattern.tks:6`) — literal escalar na ESQUERDA. **A ser removido
  do modelo SEALED** (vira `<tipo> as x when x == <lit>`).
- `RangePattern { lo; hi }` (`pattern.tks:7`) — `lo ..= hi`. **A ser removido** (vira
  `<tipo> as x when x >= lo && x <= hi`).
- `BindPattern` (`pattern.tks:14-27`) — `Foo`, `Foo as x`, `[]T as x`, `Foo<i64> as x`. **É o
  discriminante teste-de-tipo do modelo SEALED — permanece, é o caso central.**
- `FieldPattern { type_name; fields }` (`pattern.tks:28`) — `Type { f; g }`. Permanece; já aceita
  `ClassBody` (ver 2.3).
- `WildcardPattern` (`_`) e `NullPattern` (`null`) — permanecem (catch-all e none-de-opcional).
- `AltPattern` — `a | b | …`, nenhuma opção pode ligar (`match.tks:278-279`). Permanece.

### 2.2 Parsing (hoje)
`src/parser/parse_pattern.tks`:
- `parse_pattern_primary` (`:7`) constrói `LiteralPattern` para Number/Str/Byte
  (`:22-38`) e `WildcardPattern`/`NullPattern` (`:10-21`). **Sob SEALED, os ramos de literal
  (Number/Str/Byte na esquerda) devem ser REMOVIDOS — vira erro de parser "um literal de valor não
  pode ser um discriminante; use `<tipo> as x when x == <lit>`".**
- `parse_pattern_range` (`:90`) monta `RangePattern`. **A ser removido do parser (mesmo erro
  guiado).**
- `Foo`/`Foo as x`/`Foo { … }` (`:55-84`) e `[]T as x` (`:39-53`) — permanecem intactos.
- `parse_arm.tks` já parseia `pattern [when guard] => body` com `has_when` (`parse_arm.tks:19-36`).
  **Nada muda no parse do arm em si.**

### 2.3 Checagem de padrão (hoje) — `src/checker/match.tks`
`check_pattern` (`match.tks:169-289`) é o dispatcher. Achados por caso:
- **Enum subject** (`match.tks:172-185`, `check_enum_pattern:20-45`): já é teste-de-membro. Alinhado
  ao modelo SEALED (`U8` na esquerda). **Sem mudança semântica.**
- **BindPattern** (`match.tks:198-217`): resolve o tipo do caso (`bind_pattern_type:353-364`),
  rejeita skip-level (`is_direct_case_of:54-68`), liga a binding IMUTÁVEL. **Este é o caminho
  teste-de-tipo do modelo SEALED.** Falta: aceitar como esquerda uma **subclasse sealed** e uma
  **impl concreta de interface/service**, e VERIFICAR a relação (hoje `is_direct_case_of` retorna
  `true` para qualquer subject não-variant — `match.tks:66` — logo NÃO verifica que `Sub` é de fato
  subclasse do subject; a Parte 2 precisa dessa verificação via `is_subclass_of`/`subclass_reaches`).
- **LiteralPattern** (`match.tks:218-224`) e **RangePattern** (`match.tks:258-270`): **caminhos a
  serem REMOVIDOS** (o modelo SEALED não tem literal-na-esquerda). A asserção de valor migra para
  `when`.
- **FieldPattern** (`match.tks:225-257`): já aceita `StructBody` E `ClassBody`
  (`match.tks:243-247`) — bom sinal de que match-sobre-classe já é meio-caminho andado.
- **Exaustividade** (`match.tks:291-480`): `exhaustive` (`:460`) cobre HOJE apenas enum
  (`enum_covered:433`) e variant (`variant_covered:418`). `has_wildcard:294` implementa "só `_` SEM
  guarda cobre tudo" e `some_arm_*` respeitam `!arms[i].has_when` — **a regra "`when` não conta para
  exaustividade" já existe** (`match.tks:298`, `:373`, `:402`, `:448`). Falta: (a) o ramo de
  **subclasse sealed** (`exhaustive` deve, para um subject de classe abstrata, cobrir o conjunto
  fechado de subclasses); (b) o **requisito de catch-all sem-guarda para conjunto ABERTO**
  (interface/primitivo) — hoje `exhaustive` sobre subject não-enum/não-variant retorna `false`
  (`:479`), o que já força `_`, mas a mensagem/So precisa ser o requisito explícito do modelo.

### 2.4 Resumo Parte 1 — o que muda em `match`
1. Parser: remover literal/range na esquerda; erro guiado apontando para `when`.
2. Checker `check_pattern`: remover ramos Literal/Range; estender BindPattern para
   subclasse-sealed e impl-concreta-de-interface/service COM verificação de relação.
3. Checker `exhaustive`: adicionar cobertura de conjunto-fechado de subclasses sealed; tornar
   explícito o requisito de catch-all sem-guarda no conjunto ABERTO (interface/primitivo/flag).
4. Migrar TODO o corpus: os **52 arms de literal** e **23 padrões `..=`** hoje existentes em
   `src/**/*.tks` (contagem heurística; ver §3.4) para `<tipo> as x when …`. Mecânico mas amplo.

---

## 3. Os `variant` nomeados — enumeração e classificação

Comando autoritativo:
`grep -rnE '^\s*(pub )?type [A-Za-z0-9_]+ = variant ' src --include=*.tks | grep -v _test`.

**Resultado: 27 declarações reais (não-teste)** — não 35. Ver §12 (a divergência com o "35" do
enunciado é uma pergunta ao dono; provavelmente contava uma árvore mais antiga ou incluía
`_test.tkt`). Todas as 27 caem sob a regra "nenhum `type` pode ser união nomeada" e migram.

### 3.1 Classificação por estratégia de migração

O achado central da Parte 2: **duas estratégias distintas**, decididas pela topologia dos arms.

**Estratégia A — hierarquia de classe SEALED** (arms são structs nominais de posse EXCLUSIVA de UMA
união; cada arm vira `type Arm = class : Base { … }`). Aplica-se aos ADTs de IR grandes:

| Tipo | arquivo:linha | #arms | notas |
|---|---|---|---|
| `Type` | `src/checker/type.tks:147` | 14 | inclui o arm `Variant` (meta) e `Named`/`Func`/`Ptr`/…; usado por TODO o checker/backend |
| `MInst` | `src/backend/minst.tks:924` | 31 | ADT de instrução ARM64 |
| `MInstX86` | `src/backend/minst_x86.tks:746` | 26 | ADT de instrução x86-64 |
| `LOp` | `src/lir/lir.tks:208` | 16 | operações LIR |
| `ExprKind` | `src/parser/ast.tks:258` | 24 | arms `Number`/`Var`/`Call`/… (structs standalone, `ast.tks:171-…`) |
| `TExprKind` | `src/checker/tast.tks:108` | 23 | gêmeo tipado de ExprKind |
| `Statement` | `src/parser/ast.tks:373` | 10 | `Binding`/`Assign`/… |
| `TStatement` | `src/checker/tast.tks:170` | 10 | gêmeo tipado |
| `Pattern` | `src/parser/pattern.tks:32` | 7 | pós-Parte-1: cai para 5 arms (Literal/Range removidos) |
| `RegexNode` | `src/regex/regex.tks:86` | 12 | AST de regex |
| `JsonValue` | `src/encoding/json/json.tks:73` | 6 | stdlib |
| `TypeBody` | `src/parser/ast.tks:558` | 9 | `StructBody`/…/`ClassBody`/… (structs de posse exclusiva) |
| `TypeExpr` | `src/parser/type.tks:10` | 4 | `NamedType`/`SliceType`/`UnionType`/`FunctionType` |
| `ConstraintExpr` | `src/parser/ast.tks:391` | 4 | |
| `ConstValueKind` | `src/checker/comptime_fold.tks:25` | 5 | |
| `RegAssignment` | `src/backend/regalloc.tks:1052` | 2 | `InReg`/`Spilled` |
| `ResidenceTier` | `src/checker/residence.tks:106` | 5 | discriminante enum-like (arms sem/pouco payload) |
| `PointsTo` | `src/checker/spine.tks:98` | 5 | idem |
| `BorrowedFrom` | `src/checker/spine.tks:146` | 4 | idem |
| `Unique` | `src/checker/spine.tks:165` | 3 | idem |
| `FSpecKind` | `src/parser/ast.tks:232` | 3 | idem |
| `TFSpecKind` | `src/checker/tast.tks:61` | 3 | idem |

> Nota sobre os "enum-like" (`ResidenceTier`, `PointsTo`, `BorrowedFrom`, `Unique`, `FSpecKind`,
> `TFSpecKind`, `RegAssignment`): se os arms carregam pouco/nenhum dado distinto, **avaliar
> convertê-los a `enum` puro** em vez de hierarquia de classe (mais barato em runtime; match-sobre-enum
> já funciona hoje — `check_enum_pattern`). Decisão por-tipo no passo de design próprio (§7).

**Estratégia B — INLINE como união anônima** (a união é um "dispatcher/wrapper" cujos arms são
COMPARTILHADOS entre várias uniões ou são eles mesmos uniões — single-inheritance sealed NÃO
consegue representar). Remover o NOME e inserir `A | B | C` anônimo em cada sítio de uso. `match`
continua funcionando (o dono permite união anônima em var/param/return/match):

| Tipo | arquivo:linha | por que não pode ser hierarquia sealed |
|---|---|---|
| `Decl` | `src/parser/ast.tks:616` | `Function`/`TypeDecl`/`ConstDecl` também aparecem em `ItemKind`/`TItem` (membro compartilhado) |
| `ItemKind` | `src/parser/ast.tks:633` | inclui `Statement` (que É união) + `TypeDecl`/`Function`/`ConstDecl` compartilhados |
| `TItem` | `src/checker/tast.tks:223` | inclui `parser::TypeDecl`/`parser::UseDecl` (FOREIGN) + `TStatement` (união) |
| `BindTarget` | `src/parser/ast.tks:265` | `SimpleName`/`DestructurePattern` — avaliar; provavelmente inline |
| `File` | `src/parser/ast.tks:619` | privado; `MainFile`/`Module` — avaliar inline |

**Prova da colisão** (`grep 'variant .*\bTypeDecl\b'`): `TypeDecl` é membro de `Decl` (`ast.tks:616`),
`ItemKind` (`ast.tks:633`) E `TItem` (`tast.tks:223`) — além de ser usado como struct standalone. Um
struct só pode ter UMA base; logo `TypeDecl` não pode ser subclasse de três hierarquias. `Statement`
e `TStatement` são uniões aninhadas dentro de `ItemKind`/`TItem` — uma base não pode ser
simultaneamente sua própria hierarquia E arm-subclasse de outra. **Portanto A e B são estratégias
disjuntas obrigatórias, não uma escolha de conveniência.**

### 3.2 Casos delicados a verificar (o implementador DEVE conferir)
- `Type` (`type.tks:147`) tem o arm `Variant` — o próprio modelo de "união interna do checker".
  Migrar `Type` para classe sealed sem eliminar a NOÇÃO de `Variant` como um `checker::Type` (a
  representação interna de uniões anônimas continua precisando de um portador). Passo de design
  próprio.
- `TItem`/`ItemKind` misturam tipos de namespaces diferentes (`parser::TypeDecl` dentro de
  `tast.tks`). Confirmar que o inline anônimo cross-ns é aceito em posição de campo/param.
- `TypeBody` (`ast.tks:558`): os 9 arms são structs de posse exclusiva → Estratégia A LIMPA. É o
  bom caso-piloto (isolado, serializado, exercita o codec).

### 3.3 Arms são structs nominais standalone?
Sim para o caminho A verificado: `Number`/`Var`/`Binary`/`Call` são `pub type X = struct { … }`
(`ast.tks:171,172,178,199`). O implementador confirma o mesmo para cada tipo A antes de migrar.

### 3.4 Match sites que MUDAM (quantificação Parte 1)
- **~52 arms de literal** na esquerda (heurística `^\s*(0x…|\d+|'.'|"…")\s*(when|=>)`) — ex.
  `src/text/text.tks:26-40` (tabela UTF-8 `0xC2 => …`). Cada um vira `byte as x when x == 0xC2 => …`.
- **~23 padrões `..=`** (`grep '\.\.='`) — cada um vira `when x >= lo && x <= hi`.
- Match sites com BindPattern/FieldPattern/enum-member (a VASTA maioria) **não mudam** — é o achado
  que valida "MINIMAL/NO rewrite" da Leitura 1: `match t { Prim as p => … }` continua igual, só que
  `Prim` passa a ser subclasse em vez de arm de variant.

---

## 4. O CRUX runtime — a classe carrega um discriminador hoje?

**Resposta curta: NÃO.** Um objeto de classe hoje NÃO carrega tag/vtable/identidade de tipo alguma.

Evidências:
- Variant HOJE é uma união com tag: `{ i32 tag@0; ptr@8; len@16 }` — o discriminante são 4 bytes no
  offset 0 (`src/lir/lower.tks:296-298`). Arms de match testam `subj.tag == index`
  (`lower_variant_tag_eq_test`, `lower.tks:9126-9139`); construção grava o tag
  (`store_variant_tag`, `lower.tks:3478`, `:9333`). Os membros são numerados em ordem de declaração.
- Classe HOJE é um agregado field-shaped puro, achatado base-first
  (`push_class_layout`/`class_instance_fields`, `lower_const.tks:1267-1268`), registrado como
  `LStructLayout`. Um objeto de classe **"não tem identidade de tipo nele, nenhuma"** — texto
  literal em `src/lir/lower.tks:5520-5524`. Dispatch dinâmico por base polimórfica é um HONEST-STOP
  N2: *"dynamic dispatch through the polymorphic base class … is not yet lowered — a class instance
  carries no vtable (N2)"* (`aggregate_receiver_dispatch_stop`, `lower.tks:5535-5541`).

**Nuance de boxing (favorável):** pelo modelo W10b.CLASS (TEKO_MASTER_PLAN §W10b.CLASS), uma classe
é **tipo de REFERÊNCIA** — objeto alocado em arena via `tk_region_alloc`, o VALOR é um ponteiro.
Logo o widening Sub→Base já existe (`resolve.tks:1274-1280`, `is_polymorphic_base`/`subclass_reaches`)
e é uma cópia-de-ponteiro (o objeto da subclasse, maior, cabe atrás do ponteiro base). **O boxing
que o variant faz com `ptr@8` já é inerente à classe (ela É um ponteiro).** O que FALTA é
exclusivamente o **discriminador dentro do objeto** e o **lowering do teste**.

### 4.1 Decisão de design recomendada (passo próprio, ABI)
Adicionar um **type-id/tag word em offset fixo (offset 0)** a todo objeto de base polimórfica
(sealed hierarchy). Cada subclasse recebe um id numérico (como o índice de membro do variant); a
construção grava o id; o arm de match carrega `load id@0; cmp id == <subclass_id>` — reaproveitando
conceitualmente `lower_variant_tag_eq_test`. Custo: +4/+8 bytes por objeto polimórfico e é decisão
de ABI (o próprio `lower.tks:5523` diz que dar slot a objeto polimórfico "muda o tamanho do objeto e
todo offset de campo"). Alternativa (B): um ponteiro de vtable (necessário de qualquer modo para
dispatch dinâmico, ROUND 3) do qual se lê o type-id. Recomendo (A) desacoplado do vtable por ora,
co-locável com o futuro vtable. **Este é o maior custo runtime da Parte 2 e merece design próprio.**

### 4.2 Consequência de valor→referência (RISCO DE PRIMEIRA ORDEM)
Os 27 ADTs de IR são HOJE `variant` = tipos de VALOR (união com tag inline, copiada por valor por
todo o compilador). Classes são tipos de REFERÊNCIA (ponteiro, mutável, arena-alocado). Migrar
variant→classe **muda a semântica de valor para referência** em `Type`, `MInst`, `LOp`,
`ExprKind`, … — que são passados por valor em milhares de sítios. Isso altera aliasing, mutação e
as análises de escape/residence/spine (`src/checker/spine.tks`, `residence.tks`) do compilador
inteiro. **Este é possivelmente o maior risco da fase e é uma tensão real para o dono** (ver §11 e
§12): ou (i) as classes sealed dos ADTs precisam ser tratadas como valor-imutável (uma noção de
"value class" que W10b não define), ou (ii) aceita-se a virada para referência e re-valida-se toda a
análise de ownership. HALT-level se não houver ruling.

---

## 5. Impacto de serialização (`.tkb` / `.tkh`)

Os ADTs migrados são serializados. Pontos:
- `TypeBody` é escrito/lido por tag-byte: `StructBody(0)/EnumBody(1)/VariantBody(2)/AliasBody(3)/
  ExternBody(4)/FlagsBody(5)/ClassBody(6)/InterfaceBody(7)/TraitBody(8)` — `write_typebody`
  (`src/emit/tkb_write.tks:446-458`) e o leitor gêmeo em `src/emit/tkb_read.tks`. Remover
  `VariantBody` (tag 2) do surface muda o wire; `ClassBody` (`write_class_body`, `tkb_write.tks:432`)
  já serializa kind/base/impls/fields/methods.
- Versões de wire a BUMPAR (`src/emit/tkb_frame.tks`): `TKB_EXPR_VERSION = 4` (`:416`),
  `TKB_PROGRAM_VERSION = 6` (`:427`), e `TKH_VERSION = 2` (`src/emit/tkh.tks:256`). Todo bump segue o
  procedimento padrão já documentado (magic + version + FNV-1a; artefato antigo é rejeitado pela
  checagem de versão — `tkh.tks:246-256`). **Bumpar os três** quando a forma de `TypeBody`/dos ADTs
  serializados mudar.
- Como o próprio compilador produz e consome esses artefatos, o reseed precisa cruzar a mudança de
  wire sem tentar ler um `.tkb`/`.tkh` de versão anterior (ver §6).

---

## 6. Estratégia de fixpoint / reseed (NÃO é aditivo)

Esta fase muda a REPRESENTAÇÃO da IR do próprio compilador — o compilador que compila a si mesmo tem
seus `Type`/`MInst`/… mudando de forma. O reseed precisa que cada fase auto-se-reproduza. Seed = o
binário `teko` liberado anterior (`.gen0/teko`, `bootstrap/teko.c`).

Processo do dono: **"des-ensina + altera os pontos necessários → reseed → limpa → reseed."**

Estágios que constroem em estados intermediários:
1. **Match universal INERTE primeiro (Parte 1).** Adicionar match-sobre-classe + exaustividade de
   subclasse sem remover NADA. A engine nova coexiste com `variant`. O seed atual entende os fontes.
   Reseed 1 valida que o compilador novo se reproduz com a engine de match ampliada.
2. **Remover literal/range da esquerda + migrar os ~75 sítios do corpus para `when`.** Ainda com
   `variant` vivo. Reseed 2. (O seed precisa entender `when` — já entende; `has_when` é antigo.)
3. **Migração dos ADTs, em ordem de dependência**, um grupo por vez, cada um gate-ável. Cada tipo A
   vira `abstract class` + subclasses; cada tipo B vira união anônima inline. Ordem sugerida
   (folhas → raízes) para que estados intermediários compilem:
   - Piloto isolado: `TypeBody` (exercita o codec) e os enum-like (`ResidenceTier`, `Unique`, …).
   - Backend: `RegAssignment`, `LOp`, `MInst`, `MInstX86`, `RegexNode`, `JsonValue`.
   - Parser: `ExprKind`, `Statement`, `Pattern`, `TypeExpr`, `ConstraintExpr`, `FSpecKind`.
   - Checker: `TExprKind`, `TStatement`, `TFSpecKind`, `ConstValueKind`, `Type` (por último — é o
     mais entrelaçado, contém o arm `Variant`).
   - Tipos B (inline): `Decl`, `ItemKind`, `TItem`, `BindTarget`, `File`.
   Cada grupo: **des-ensina o `variant` daquele tipo → altera os sítios → reseed → limpa → reseed.**
4. **Remover as FORMAS de declaração `type X = variant …` e `type X = A | B`** do parser/checker (a
   sintaxe some), mantendo união anônima em posições permitidas. Reseed final (limpa + reseed).
5. Bumpar as versões de wire (§5) no ponto em que a forma serializada muda — provavelmente junto do
   grupo de `TypeBody`/parser, num único cruzamento de wire para não multiplicar incompatibilidades.

Ferramentas de reseed presentes: `scripts/fixpoint_gate.sh`, `scripts/build_gen1_from_c.sh`,
`scripts/known_stop_gate.sh`, `scripts/degrau.sh`. Disciplina obrigatória: `cc -std=c2x`,
`--no-verify`, **nunca** `teko test .`.

---

## 7. Engine de exaustividade (o alvo de `src/checker/match.tks`)

Já existe: `has_wildcard` (`:294`, só `_` sem-guarda cobre tudo), `variant_covered` (`:418`),
`enum_covered` (`:433`), `exhaustive` (`:460`). A regra "`when` não conta" já está em cada
`!arms[i].has_when` (`:298,:373,:402,:448`).

A ADICIONAR:
1. **Conjunto FECHADO de subclasses sealed.** Para um subject de classe abstrata, enumerar todas as
   subclasses concretas (varrer a type-table por classes cuja cadeia de base alcança a base — HOJE
   só existe o sentido sub→base via `subclass_reaches` (`resolve.tks:1419`)/`is_subclass_of`
   (`collect.tks:734`); **falta a função base→[subclasses]**, a ser adicionada). Cobrir cada
   subclasse por algum arm sem-guarda; senão não-exaustivo.
2. **"`when` quebra exaustividade"** — já implementado; estender aos arms de subclasse.
3. **Requisito de catch-all sem-guarda para conjunto ABERTO** — interface (impls desconhecidas),
   primitivo (espaço de valor) e flag. Hoje `exhaustive` retorna `false` para não-enum/não-variant
   (`:479`), o que força `_`; tornar o requisito EXPLÍCITO e a mensagem guiada.
4. **Definir o que fecha o conjunto de subclasses.** Classes são abertas entre módulos em princípio;
   "sealed para exaustividade" precisa de uma regra (ex.: o conjunto fechado = subclasses visíveis no
   programa compilado; ou uma marca explícita). **Pergunta ao dono / passo de design próprio.**

---

## 8. Assinaturas/shapes que o implementador adiciona (em Teko, full-Javadoc)

Contratos contra a forma DECLARADA das dependências (compilam hoje como esqueleto honest-stop).

```
/**
 * subclasses_of — o conjunto FECHADO de subclasses concretas de uma classe abstrata/virtual, para a
 * exaustividade de `match` sobre hierarquia sealed. Varre a type-table por toda classe cuja cadeia
 * de base alcança `base` (o sentido inverso de `subclass_reaches`, que hoje só vai sub→base).
 *
 * @param base   o nome canônico da classe-base polimórfica
 * @param table  a type-table do programa
 * @return       os nomes das subclasses concretas (instanciáveis), em ordem de declaração
 * @throws       propagado de uma cadeia de base malformada (invariante já garantida pelo checker)
 * @see          teko::checker::subclass_reaches (o sentido sub→base existente)
 * @since         (fase match-universal + migração variant)
 */
fn subclasses_of(base: str, table: TypeTable): []str | error

/**
 * class_covered — todo membro do conjunto FECHADO de subclasses de um subject de classe sealed é
 * coberto por algum arm SEM guarda? Gêmeo de `variant_covered`/`enum_covered` para o eixo de classe.
 *
 * @param arms   os arms do match
 * @param subs   as subclasses concretas do subject (de `subclasses_of`)
 * @param table  a type-table
 * @param ref_ns o namespace do sítio de match (para resolução dos nomes de caso)
 * @return       true iff todo `subs[i]` é nomeado por um arm sem-guarda
 */
fn class_covered(arms: []parser::Arm, subs: []str, table: TypeTable, ref_ns: str): bool

/**
 * requires_open_catch_all — o subject é de conjunto ABERTO (interface/primitivo/flag) e portanto
 * EXIGE um catch-all/bind-all sem guarda? Torna explícita a regra do modelo SEALED que hoje só
 * emerge do `exhaustive` retornar false para não-enum/não-variant.
 *
 * @param subject  o tipo do subject resolvido
 * @param table    a type-table
 * @return         true iff o subject é interface/primitivo/flag (conjunto aberto)
 */
fn requires_open_catch_all(subject: Type, table: TypeTable): bool
```

Funções existentes que o implementador TOCA (não recriar):
- `src/checker/match.tks`: `check_pattern:169`, `exhaustive:460`, `is_direct_case_of:54` (passa a
  usar `is_subclass_of` para subjects de classe), remover ramos `LiteralPattern:218`/`RangePattern:258`.
- `src/parser/parse_pattern.tks`: remover `parse_pattern_range:90` e os ramos Number/Str/Byte de
  `parse_pattern_primary:22-38`.
- `src/checker/resolve.tks`: reusar `is_polymorphic_base:1406`, `subclass_reaches:1419`.
- `src/checker/collect.tks`: reusar `find_class_body:584`, `is_subclass_of:734`.
- `src/lir/lower.tks`: `lower_variant_tag_eq_test:9139`/`store_variant_tag` como MODELO do
  discriminador de classe; fechar o honest-stop `aggregate_receiver_dispatch_stop:5535`.
- `src/emit/tkb_write.tks:446` / `tkb_read.tks` / `tkb_frame.tks` (bumps de versão).

---

## 9. Fixtures de regressão a adicionar (input → exit code nativo esperado)

Grupo `own_native` (o compilador compilando corpus próprio). Exit code = valor observável do `main`.

1. **match sobre primitivo com `when`** — `match n { i32 as x when x == 0 => 7; i32 as x => 9 }`;
   `n=0` → exit `7`, `n=5` → exit `9`. (Substitui o antigo `0 => …`.)
2. **match sobre byte, tabela migrada** — porta de `text.tks`: `byte as b when b == 0xC2 => …`;
   confirma paridade com o comportamento pré-migração (mesmo exit).
3. **match sobre subclasse sealed exaustivo** — `abstract class Shape`, subclasses `Circ`/`Sq`;
   `match s { Circ as c => 1; Sq as q => 2 }` sem `_`; deve COMPILAR (exaustivo) e discriminar por
   tag (exit distinto por subclasse).
4. **exaustividade quebrada por `when`** — mesmo match, mas `Circ as c when …`; SEM catch-all → deve
   FALHAR na checagem (fixture de rejeição / exit não-zero do compilador).
5. **conjunto aberto exige catch-all** — `match ifaceVal { ConcreteA as a => …; ConcreteB as b => … }`
   sem `_` → rejeição; com `_ =>` → compila.
6. **flag via `when`** — `match f { F as x when x.has(F::A) => …; F as x => … }`.
7. **união anônima preservada** — um `A | B` em posição de param/return continua fazendo `match`
   após a remoção das formas nomeadas (regressão da Estratégia B).
8. **round-trip `.tkb`** — serializar/deserializar um `TypeBody` migrado (classe) e confirmar
   igualdade; artefato de versão anterior é REJEITADO pelo bump.
9. **literal-na-esquerda agora é erro de parser** — `match n { 0 => … }` → mensagem guiada apontando
   para `when` (fixture de rejeição).

---

## 10. Pontos de ritual (gate cheio obrigatório)

- **R1** — após Parte 1 inerte (match-sobre-classe + exaustividade novas, `variant` ainda vivo):
  reseed cheio, `scripts/fixpoint_gate.sh`, fixtures 1-6.
- **R2** — após remover literal/range da esquerda + migrar os ~75 sítios do corpus: reseed cheio,
  fixture 9.
- **R3** — após CADA grupo de migração de ADT (§6 passo 3): reseed (des-ensina → altera → reseed →
  limpa → reseed) + gate; o piloto `TypeBody` tem gate próprio com fixture 8.
- **R4** — após remover as formas de declaração `variant`/`A | B` nomeadas: reseed final cheio +
  fixture 7 + suíte completa.
- Bumps de wire (§5): gate dedicado no cruzamento único de versão.

---

## 11. Riscos + tensões de lei (com resolução recomendada)

1. **[MAIOR] valor→referência dos ADTs de IR (§4.2).** Migrar `variant`→`class` vira 27 tipos de
   valor em referência mutável arena-alocada, alterando aliasing/escape/spine em todo o compilador.
   *Resolução recomendada:* pedir ao dono um ruling — "value class" imutável para os ADTs, OU aceitar
   a virada e re-validar ownership. HALT-level sem ruling (§12).
2. **[MAIOR] lowering nativo de valor de classe polimórfica é honest-stop (N2)** (`lower.tks:5538`).
   O compilador se auto-compila para nativo; se os ADTs viram classes polimórficas e o backend não
   as lowera (nem tag, nem dispatch), o compilador não compila a si mesmo. *Resolução:* construir o
   discriminador de tag + o lowering do match-sobre-subclasse (§4.1) como parte da Parte 1/pré-Parte-2,
   ANTES de migrar qualquer ADT. Bloqueio duro caso contrário.
3. **Membros compartilhados / uniões-de-uniões (§3.1 Estratégia B).** `TypeDecl`/`Function`/
   `Statement`/`TStatement`/`UseDecl` em múltiplas uniões impedem single-inheritance sealed.
   *Resolução (law-first):* o próprio ruling do dono já autoriza união anônima em var/param/return —
   migrar esses por INLINE, não por classe. Sem tensão residual.
4. **Definição de "sealed/fechado" para exaustividade (§7.4).** Classes são abertas entre módulos.
   *Resolução:* pergunta ao dono — conjunto fechado = subclasses visíveis no programa compilado, ou
   marca explícita.
5. **Amplitude da migração de match sites (~75).** Mecânica mas ampla; risco de regressão silenciosa
   em tabelas como `text.tks`. *Resolução:* fixture 2 fixa a paridade exata de exit code.
6. **Cruzamento de wire (§5).** Bumpar `TKB_EXPR/PROGRAM/TKH` num único ponto para não multiplicar
   incompatibilidades de reseed.
7. **`Type` contém o arm `Variant` (§3.2).** Migrar `Type` sem perder o portador interno de uniões
   anônimas. Passo de design próprio, por último na ordem.

## 11.1 Sub-fases que precisam de passo de design PRÓPRIO (mais fundo)
- **DP-1** discriminador de tag de classe polimórfica + lowering do match (ABI). (§4.1)
- **DP-2** semântica valor-vs-referência dos ADTs migrados. (§4.2) — depende de ruling do dono.
- **DP-3** migração de `Type` (o mais entrelaçado; contém `Variant`). (§3.2)
- **DP-4** regra de fechamento do conjunto de subclasses para exaustividade. (§7.4)
- **DP-5** enum-like: converter a `enum` puro vs classe sealed, por-tipo. (§3.1 nota)

---

## 12. Perguntas em aberto para o dono (HALT points relevados pelo integrador)

1. **Contagem "35" vs 27 reais.** A grep autoritativa
   (`^\s*(pub )?type … = variant`, sem `_test`) encontra **27** declarações não-teste. O "35" do
   enunciado não bate. Confirmar o alvo (27 é a lista real em `fix/retirement` hoje).
2. **[TENSÃO REAL — pode HALTAR] valor vs referência (Risco 1/§4.2).** Os ADTs de IR são valor hoje;
   `class` é referência mutável. Precisamos de um ruling: "value class" imutável para eles, ou virada
   para referência com re-validação de ownership? Sem isso, a Parte 2 não pode fechar sem risco de
   miscompile/semântica alterada no coração do compilador.
3. **Fechamento de subclasses (§7.4).** O que torna o conjunto "sealed" para exaustividade?
4. **Enum-like (§3.1 nota / DP-5).** Autorizar converter `ResidenceTier`/`PointsTo`/`BorrowedFrom`/
   `Unique`/`FSpecKind`/`TFSpecKind`/`RegAssignment` a `enum` puro quando os arms não carregam dado
   distinto (mais barato que hierarquia sealed)?
5. **Estratégia B (inline) para `Decl`/`ItemKind`/`TItem`/`BindTarget`/`File`.** Confirmar que remover
   o nome e inserir a união anônima em cada sítio é a leitura correta do ruling (parece ser).
```
