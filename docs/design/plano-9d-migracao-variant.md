# Plano — §9.D: banir `type X = variant` e migrar os ~28 ADTs nomeados SEM a virada valor→referência

> **⚑ SUPERSEDED em parte (2026-08-14).** A RECOMENDAÇÃO deste doc (Solução A / wrapper
> `struct { case: … }`) foi retirada por ruling do dono: a forma-alvo é a **união `|` inline
> por-extenso**. Uma BUILD do implementer provou a premissa antiga FALSA (a união bare `A | B`
> funciona, mas a forma que a migração exige não parseia/codegen/match). O re-plano vigente —
> capability-PRs segmentados (parser `(A|B)`, mangle de união, match de grupo anónimo), a correção da
> contagem para **29** (faltava `MergeDisposition`), e a dependência do §14/`@Type()` — está em
> **`docs/design/plano-9d-capabilities-inline-uniao-v1.md`**. A ESPINHA deste doc (classificação
> por-ADT, ordem folhas→raízes, blast-radius, wire `.tkb`, dissolução do crux valor→referência)
> permanece válida e é citada por aquele; só a representação-alvo e o segmento de capacidades mudam.


> **⚑ RULING DO DONO (supera a recomendação abaixo).** A forma-alvo **NÃO** é a "Solução A /
> newtype-tagged-value-union" (wrapper `struct { case: … }`) que este doc recomenda. É a **união `|`
> estrutural inline, escrita POR EXTENSO** em cada campo — sem wrapper, sem alias, sem `newtype`. O
> agregado nomeado (`Type`, `MInst`, …) simplesmente **some**; cada campo carrega a união dos membros por
> extenso (`Slice = struct { element: Prim | Byte | … | Null }`). **Verbosidade é o preço, aceito**
> (verbatim: *"Vai ser verboso mesmo, e isso não é problema, é o preço."*). A recursão fecha pelo box que a
> própria união `|` já emite (o descritor `{tag;ptr;len}`). **A espinha deste plano** — classificação
> por-ADT, campos recursivos de valor-direto, ordem de fixpoint folhas→raízes, fixtures, wire `.tkb` —
> **segue válida**; só a representação-alvo muda (união inline, não wrapper). Ver Doc 2 **§9.D**
> (`mudancas-superficie-0.3.1.md`). O corpo abaixo fica como registro da deliberação.

> **Status:** DESIGN. Read-only no código de produto — NENHUM `.tks` editado, NENHUM build, NENHUM
> reseed, NENHUM `teko test .` em forma alguma (a fuga do `monomorph` derruba o container). Este
> documento É o artefacto; o único commit desta crumb é ele próprio. Worktree isolado off
> `origin/fix/retirement` (o tree principal está a ser editado por um implementer de reseed — INTOCADO).
> **Fonte de lei SELADA (desenha-se à volta, não se re-abre):**
> - §9.2b / §9.D — `type X = variant` **BANIDO**; `type X = A | B` estrutural **já rejeitado**. A união
>   `|` sobrevive SÓ em posição de var/param/return/campo/constraint, NUNCA como `type` nomeado
>   (`docs/design/mudancas-superficie-0.3.1.md:376-410`).
> - §9.4 — um **struct PODE implementar interface** (`struct Point & IEq { … }`); interfaces despacham
>   por **vtable**; a regra de composição por igualdade-de-AST aplica-se
>   (`mudancas-superficie-0.3.1.md:469-495`).
> - **item 14** — value-structs são **mutáveis** e **FAT**: cada struct carrega um **cabeçalho de
>   auto-ponteiro** (`uptr`); `self` é um `ref`; uma cópia é uma nova materialização com novo ponteiro
>   (`docs/design/plano-item14-value-struct-mutavel.md:43-51,328-346`).
> - **match-universal** é a engine existente + a casa da migração
>   (`docs/design/plano-match-universal-e-migracao-variant.md`).
> **Este doc RESOLVE** o "risco de 1ª ordem" que o `plano-match-universal §4.2` deixou em aberto (a
> virada valor→referência). Ele traz 3 soluções concretas, a classificação por-ADT, como o `match`
> discrimina cada forma, o plano de fixpoint e a tabela de blast-radius, e RECOMENDA uma.

---

## 0. O crux, re-enunciado, e a chave que o resolve

Os ~28 ADTs nomeados são as PRÓPRIAS ADTs de IR do compilador (`Type`, `Expr`/`ExprKind`, `LOp`,
`MInst`, `Statement`, …), passadas **por VALOR** em milhares de sítios. A leitura ingénua
(`variant`→`class`) vira valor→referência (mutável, arena-alocado), mexendo em aliasing/mutação/escape
por todo o compilador — o `plano-match-universal §4.2` marcou isto como HALT-level.

**A chave que dissolve o crux — dois factos representacionais SELADOS já na árvore:**

1. **`variant` HOJE JÁ É "descritor fat + payload BOXED", passado por valor.** A imagem é o
   `{ i32 tag@0; ptr@8; len@16 }` de 24 bytes (`src/lir/lower.tks:327,9209,9350`,
   `variant_wrapper_bytes`); o payload do arm vive ATRÁS do `ptr@8`. Copiar um `variant` copia os 24
   bytes (o ponteiro incluído) — dois "valores" partilham o MESMO payload boxed. Como a IR é imutável
   depois de construída, isto observa-se como semântica de valor. **Ou seja: o "valor" que anda pelo
   compilador já É um descritor com um ponteiro lá dentro. Não há um `struct` inteiro a ser copiado
   campo-a-campo hoje; há um descritor de 24 bytes.** Migrar para uma forma que preserve exatamente este
   descritor NÃO vira valor→referência — mantém o que já existe.

2. **O idioma "struct-wrapper sobre a variant" JÁ é a prática do próprio corpus.** `Expr = struct {
   kind: ExprKind; line; col }` (`src/parser/ast.tks:293`), `TExpr = struct { kind: TExprKind; … }`
   (`src/checker/tast.tks:10-11`), `FSpec = struct { kind: FSpecKind }` (`ast.tks:255`), `TFSpec =
   struct { kind: TFSpecKind }` (`tast.tks:90`). O compilador já embrulha uniões em campos de struct e
   já faz `match e.kind { Number as n => … }`. A migração §9.D, para estes, é apenas **inline da união
   no campo do wrapper que já existe** — o nome da variant desaparece, o `.kind` continua igual, a
   representação fica byte-idêntica.

Estes dois factos apontam para uma migração que **NÃO toca na semântica de valor** — a Solução A abaixo.

---

## 1. As três soluções (mecânica + exemplo + tradeoffs)

### Solução A — **newtype-tagged-value-union** (o wrapper-struct sobre a união anónima). RECOMENDADA.

**Ideia.** Um `type X = variant A | B | C` migra para um **`struct` de valor cujo único conteúdo
discriminante é uma UNIÃO ANÓNIMA** `A | B | C` num campo. O struct dá o NOME (permitido — struct não é
união); a união fica anónima num CAMPO (posição permitida: campos carregam `A | B`/`T | null` hoje em
todo o lado — ex. `iface_value_field`, os `T | null`). Zero classe, zero vtable, zero referência.

Duas sub-formas, escolhidas pela topologia (é ergonomia, NÃO um constraint duro — ver §1.4):

- **A-inline** (para os já-embrulhados): inline a união no campo do wrapper existente.
  ```teko
  // ANTES:  pub type ExprKind = variant Number | Var | … | Block
  //         pub type Expr = struct { kind: ExprKind; line: u32; col: u32 }
  // DEPOIS: (ExprKind deixa de existir como nome; a união vive no campo)
  /**
   * Expr — a parsed expression node: its discriminated kind plus 1-based source position.
   * The kind field is the anonymous sum of the concrete node structs (§9.D: no named union); the
   * checker discriminates it with `match e.kind { Number as n => … }` exactly as before — the wire
   * tag layout is byte-identical to the retired `ExprKind` variant (same members, same order).
   *
   * @field kind  the node's concrete case, an anonymous `Number | Var | … | Block`
   * @field line  1-based source line the parser stamped
   * @field col   1-based source column
   * @since §9.D
   */
  pub type Expr = struct {
      kind: Number | Var | Call | IfExpr | MatchExpr | StrLit | ByteLit | CharLit | BoolLit | NullLit
          | Binary | Unary | Compare | FieldAccess | MethodCall | Cast | PathExpr | StructLit | Index
          | Interp | InExpr | ArrayLit | Lambda | Borrow | Block
      line: u32
      col: u32
  }
  ```
  Sítios de `match e.kind { … }` e de construção `Expr { kind = Number { … } }`: **INALTERADOS**.
  Sítios que nomeavam `ExprKind` bare (ex. `fn form_name(k: TExprKind)`, `consteval_form.tks:33`)
  passam a nomear o wrapper (`TExpr`) e ler `.kind`, ou a própria união anónima. Blast-radius mínimo.

- **A-wrap** (para os não-embrulhados — `Type`, `MInst`, `LOp`, `Pattern`, …): cria-se o wrapper.
  ```teko
  /**
   * MInst — one ARM64 machine instruction (§9.D newtype over the anonymous instruction sum). A
   * single-`case` value struct: `case` is the anonymous `MAlu | MAluImm | … | MInArg`, discriminated
   * by `match mi.case { MAlu as a => … }`. Value semantics preserved (struct, immutable); the tag
   * layout matches the retired `MInst` variant member-for-member, so lowering (`lower_variant_*`) and
   * `.tkb` wire numbering are unchanged.
   *
   * @field case  the concrete instruction, an anonymous union of the 31 instruction structs
   * @since §9.D
   */
  pub readonly type MInst = struct { case: MAlu | MAluImm | /* … */ | MInArg }
  ```
  Para NÃO tocar os milhares de sítios de construção/match, duas ponte de checker (§1.1):
  **auto-widen** (`MAlu { … }` num slot `MInst` embrulha-se sozinho) e **auto-descend** (`match mi { MAlu
  as a => … }` desce sozinho ao `.case`). Com as duas pontes, A-wrap é quase source-idêntico ao de hoje.

**Discriminação do `match`.** O discriminante é a **tag-word da união** — `subj.tag == index`, via o
EXISTENTE `lower_variant_tag_eq_test` (`src/lir/lower.tks:9205-9209`); a exaustividade via o EXISTENTE
`variant_covered` (`src/checker/match.tks:418`). **Nada de novo na engine de match** — é o achado
decisivo: `match` sobre união anónima num campo é EXATAMENTE o que o compilador já faz para `Expr.kind`.

**Valor→referência?** NENHUMA. A-inline é byte-idêntico ao variant de hoje (mesma tag, mesma ordem, mesmo
`{tag;ptr;len}`). A-wrap acrescenta apenas o cabeçalho fat do item-14 (1 `uptr`) à volta do descritor de
24 bytes → 32 bytes, semântica de valor intacta. (Optimização opcional em §5: um wrapper de campo-único
`case` pode lowerar-se TRANSPARENTE — representação = a da união interna, sem cabeçalho — devolvendo
byte-identidade também no A-wrap; é um carve-out do item-14, ver §5.2.)

**Tradeoffs.** (+) Zero mudança de semântica; reusa 100% da engine de tag/exaustividade; fixpoint
byte-idêntico (A-inline) ou quase (A-wrap); **NÃO depende da spine** (L1); dissolve o problema
membro-partilhado (§1.4). (−) O A-wrap adiciona um `.case` conceptual (mitigado pelas pontes de checker);
uniões de 24-31 arms escritas por extenso são verbosas na DECLARAÇÃO (uma vez só, no ponto de decl).

### Solução B — **value-struct + interface SELADA** (discriminação por vtable). Futuro, spine-gated.

**Ideia.** Cada `type X = variant A | B | C` vira uma **`interface X`** (marcador/contrato) e cada arm
vira um **`struct A & X { … }`** (value-struct que implementa X — §9.4 SELA que struct implementa
interface). Um valor de `X` é um **fat pointer de interface** `{ data@0; vtable@8 }`
(`src/lir/lower.tks:5455`, `codegen.tks:4412`): `.data` aponta a materialização do value-struct (o
auto-ponteiro do item-14), `.vtable` é a tabela estática por-`(struct, X)` `tk_vt_A_X`.

```teko
/** Type — the checker's semantic-type contract (§9.D value-struct-interface form). */
pub interface Type { }
/** Prim — a fixed-width primitive type, a value-struct implementing the Type contract. */
pub readonly type Prim & Type = struct { kind: PrimKind }
/** Named — a nominal user type by canonical name, a value-struct implementing Type. */
pub readonly type Named & Type = struct { name: str }
// … match t { Prim as p => …; Named as n => … }   (discrimina pela impl concreta)
```

**Discriminação do `match`.** O discriminante é o **`.vtable`** do fat pointer — cada `(concreteType,
interface)` tem UM símbolo de vtable estático distinto (`tk_vt_A_X`), logo `subj.vtable == &tk_vt_A_X` é
o teste-de-tipo; ou um type-id lido via vtable (slot 0). **Isto é NOVO no lowering** (não existe hoje um
"compare-vtable-symbol"; existe o *dispatch* por vtable, `emit_iface_call` `codegen.tks:2179`, mas não a
*discriminação*). **Exaustividade precisa de "interface SELADA"** — um conjunto FECHADO de impls (as ADTs
exigem `match` exaustivo SEM catch-all: adicionar um `MInst` novo tem de forçar cada `match` a tratá-lo).
Interface é ABERTA por default → precisa da noção nova "sealed interface" (conjunto de impls fechado no
programa) + a função `impls_of_sealed_iface` (análoga ao `subclasses_of` do plano-match-universal §8).

**O cabeçalho fat (item-14) dá o discriminante?** NÃO. O cabeçalho do item-14 é SÓ um auto-ponteiro
(`uptr`), sem type-tag (`plano-item14 §4.1`, `tk_struct_hdr { uintptr_t self_ptr; }`). O discriminante em
B vem do `.vtable` do FAT POINTER DE INTERFACE, não do cabeçalho do struct.

**Fica VALUE? Sim, mas com uma costura.** O value-struct continua valor. O fat pointer de interface
`{data;vtable}` é 16 bytes passados por valor; `.data` aponta o value-struct. Copiar o fat pointer
partilha `.data` (aliasing do backing) — MESMA semântica do `ptr@8` do variant (§0 facto 1). PORÉM:
**boxar um value-struct num fat pointer de interface está spine-gated (L1, POR CONSTRUIR).** O
`interface-value-type.md §4.2` sela: um struct não tinha endereço estável → o boxing exige um lastro
mais-longevo (A1 = copy-on-attach transitivo, a spine). O item-14 dá um endereço (o auto-ponteiro), MAS o
endereço é a materialização no arena/frame local; se o fat pointer ESCAPA (guardado num `[]MInst`,
retornado — o que as ADTs de IR fazem CONSTANTEMENTE), `.data` fica dangling. Para escapar em segurança,
o box tem de MATERIALIZAR o struct no arena que sobrevive — que é exatamente A1/L1. **Logo B está
bloqueada na spine para as ADTs do próprio compilador.**

**Tradeoffs.** (+) É a forma mais "OO", dá dispatch de método por-arm de graça, alinha com §9.4. (−)
Discriminação por vtable é lowering NOVO; exige "interface selada" NOVA (exaustividade); **depende da
spine (L1) para o caso escapante — que é o caso das ADTs de IR**; muda 27+ decls para o par
interface+impls. **Bloqueada AGORA.**

### Solução C — **hierarquia de classe SELADA** (a leitura original — aceita a virada valor→referência).

**Ideia.** `type X = variant A | B` → `abstract class X` + `class A : X`, `class B : X`. Classes são
**tipos de REFERÊNCIA** (arena-alocado, o valor É um ponteiro — `resolve.tks:1435` `is_class_name`,
W10b.CLASS). Discriminante: um **type-id em offset fixo** no objeto (o "DP-1" do plano-match-universal
§4.1; hoje um objeto de classe "não tem identidade de tipo nele, nenhuma" —
`src/lir/lower.tks:5596-5616`, honest-stop N2). Exaustividade: `subclasses_of` (base→[subclasses]), a
adicionar (`plano-match-universal §8`).

**Isto é a virada valor→referência que o crux manda EVITAR.** `Type`/`MInst`/`LOp`/… deixam de ser
valor e passam a referência mutável arena-alocada, alterando aliasing/escape/spine/residence no
compilador inteiro (`src/checker/spine.tks`, `residence.tks`). Além disso o **membro-partilhado**
(`TypeDecl`/`Function`/`Statement` em `Decl`/`ItemKind`/`TItem`) IMPEDE single-inheritance (um struct só
tem uma base) — força um sub-conjunto a ficar inline de qualquer modo.

**Tradeoffs.** (+) É a forma canónica de sum-type-selado; dispatch de método natural. (−) **Vira o
coração do compilador de valor para referência** — re-validação de ownership em todo o lado; type-id-word
= decisão de ABI nova (DP-1); quebra byte-identidade; membro-partilhado força split; maior blast-radius
possível. **Rejeitada pelo crux** (é precisamente a virada não-gerida a evitar).

### 1.4 O problema membro-partilhado DISSOLVE-SE na Solução A

O `plano-match-universal §3.1` foi FORÇADO a partir os 27 em "Estratégia A (classe selada)" vs "Estratégia
B (inline)" porque `TypeDecl`/`Function`/`Statement`/`TStatement`/`UseDecl` aparecem em MÚLTIPLAS uniões
(`Decl`/`ItemKind`/`TItem`) e um struct só pode ter UMA base sealed. **Na Solução A NÃO há herança** — um
struct entra livremente em quantas uniões anónimas quiser. Logo o split wrap-vs-inline é PURA ERGONOMIA
(blast-radius), não um constraint topológico. É outra vantagem decisiva de A sobre C.

---

## 2. Avaliação contra os quatro eixos + recomendação

| Eixo | A — newtype-tagged-union | B — value-struct+interface | C — classe selada |
|---|---|---|---|
| **match exaustividade/discriminação** | reusa 100% (`lower_variant_tag_eq_test`, `variant_covered`) — NADA novo | discrim. por vtable = lowering NOVO; exaustiv. precisa "interface selada" NOVA | type-id-word NOVO (DP-1); `subclasses_of` NOVO |
| **risco valor→referência** | **NENHUM** (A-inline byte-idêntico; A-wrap = +hdr item-14, valor) | value preservado NA FORMA, mas o box escapante = **spine-gated (L1)** | **É a virada** — referência mutável arena; re-valida ownership |
| **fixpoint/reseed** | byte-idêntico (A-inline) / quase (A-wrap, route-C-gated como item-14) | bloqueado (L1) + mudança de repr grande | quebra byte-identidade + ABI type-id novo |
| **blast-radius nos 28** | decl muda; sítios ~inalterados (pontes de checker); membro-partilhado dissolve | 28 decls → interface+impls; escape das ADTs bloqueia | máximo; membro-partilhado força split; ownership re-validado |
| **dependências** | só o ban do `variant` + as 2 pontes de checker | **spine L1 (por construir)** + interface-selada | DP-1 ABI + spine re-valid |

**RECOMENDAÇÃO: Solução A (newtype-tagged-value-union).** É a única que (i) NÃO vira valor→referência,
(ii) reusa a engine de match verbatim, (iii) é byte-idêntica/quase no fixpoint, (iv) NÃO depende da spine,
(v) dissolve o membro-partilhado. **B é o alvo de LONGO PRAZO** para ADTs que queiram dispatch por-arm,
DEPOIS que a spine (L1) e "interface selada" existam — migrar A→B é local (o wrapper vira interface, os
arms ganham `& X`), sem re-tocar os sítios. **C rejeitada** (é o crux). Sem tensão de lei residual (§8).

---

## 3. Classificação por-ADT (28 declarações reais, `fix/retirement` hoje)

> **Contagem corrigida: 28** (o `plano-match-universal` contou 27; falta-lhe `BindElem`,
> `src/parser/ast.tks:448`). Grep autoritativa:
> `grep -rnE '^\s*(pub )?type [A-Za-z0-9_]+ = variant ' src --include=*.tks | grep -v _test`.

**Grupo 1 — já-embrulhado → A-inline (inline a união no wrapper existente; near-zero surface):**

| ADT | file:line | wrapper existente | #arms |
|---|---|---|---|
| `ExprKind` | `src/parser/ast.tks:292` | `Expr` (`ast.tks:293`) | 24 |
| `TExprKind` | `src/checker/tast.tks:148` | `TExpr` (`tast.tks:10`) | 23 |
| `FSpecKind` | `src/parser/ast.tks:253` | `FSpec` (`ast.tks:255`) | 3 |
| `TFSpecKind` | `src/checker/tast.tks:89` | `TFSpec` (`tast.tks:90`) | 3 |

**Grupo 2 — discriminante SEM payload → `enum` puro (mais barato; `match`-sobre-enum já existe):**

| ADT | file:line | arms | verificação |
|---|---|---|---|
| `Unique` | `src/checker/spine.tks:165` | `UsUnique`/`UsShared`/`UsTop` — TODOS `struct {}` (`spine.tks:158-164`) | arms só usados como membros de `Unique` → converter SEGURO |
| `ResidenceTier` | `src/checker/residence.tks:106` | `Scope`/`Frame`/`Caller`/`Root`/`Unresolved` — TODOS `struct {}` (`residence.tks:57-94`) | CONFIRMAR que os nomes-de-arm (genéricos!) não são usados como structs fora de `ResidenceTier` antes de converter |

**Grupo 3 — membro-partilhado / união-de-uniões → A-inline (união anónima; SEM wrapper — os membros são
partilhados/foreign, não podem ter um wrapper próprio):**

| ADT | file:line | porquê inline |
|---|---|---|
| `Decl` | `src/parser/ast.tks:819` | `Function`/`TypeDecl`/`ConstDecl` também em `ItemKind`/`TItem` |
| `ItemKind` | `src/parser/ast.tks:836` | inclui `Statement` (união) + membros partilhados |
| `TItem` | `src/checker/tast.tks:290` | inclui `parser::TypeDecl`/`parser::UseDecl` (foreign) + `TStatement` |
| `File` | `src/parser/ast.tks:822` | privado, 2 arms `MainFile`/`Module` — inline trivial |
| `BindTarget` | `src/parser/ast.tks:299` | 2 arms `SimpleName`/`DestructurePattern` — inline |

**Grupo 4 — payload distinto, dono-único → A-wrap (`type X = struct { case: <união anónima> }`,
com auto-widen/auto-descend):**

| ADT | file:line | #arms | nota |
|---|---|---|---|
| `Type` | `src/checker/type.tks:166` | 14 | o mais entrelaçado; contém o arm `Variant` (portador interno de uniões anónimas — §6) |
| `MInst` | `src/backend/minst.tks:924` | 31 | |
| `MInstX86` | `src/backend/minst_x86.tks:746` | 26 | |
| `LOp` | `src/lir/lir.tks:208` | 16 | |
| `Statement` | `src/parser/ast.tks:490` | 11 | |
| `TStatement` | `src/checker/tast.tks:220` | 10 | |
| `Pattern` | `src/parser/pattern.tks:32` | 7 (→5 pós-parte-1) | |
| `TypeBody` | `src/parser/ast.tks:761` | 10 | piloto do codec `.tkb` (§5) |
| `TypeExpr` | `src/parser/type.tks:10` | 4 | |
| `ConstraintExpr` | `src/parser/ast.tks:508` | 4 | |
| `ConstValueKind` | `src/checker/comptime_fold.tks:25` | 5 | |
| `RegexNode` | `src/regex/regex.tks:86` | 12 | stdlib |
| `JsonValue` | `src/encoding/json/json.tks:73` | 6 | stdlib |
| `PointsTo` | `src/checker/spine.tks:98` | 5 | `PtParam`/`PtAdopter` carregam payload → NÃO é enum |
| `BorrowedFrom` | `src/checker/spine.tks:146` | 4 | `BfParam`/`BfLocal` carregam payload → NÃO é enum |
| `RegAssignment` | `src/backend/regalloc.tks:1052` | 2 | `InReg`/`Spilled` carregam payload (`regalloc.tks:1016,1033`) → NÃO é enum |
| `BindElem` | `src/parser/ast.tks:448` | 3 | `BindName`/`BindSkip`/`BindRest` |

> Grupos 3/4 são intercambiáveis por ergonomia (inline vs wrap) SEM constraint duro (§1.4). A regra de
> bolso: **≤3 arms e/ou membros-partilhados/foreign → inline; muitos arms e dono-único → wrap** (o wrapper
> evita repetir uma união de 31 arms em cada assinatura).

---

## 4. Como o `match` discrimina a forma migrada (por solução)

- **A (recomendada).** Subject = a união anónima (direta no campo `Expr.kind` / no `.case` do wrapper /
  inline no param). O discriminante é a tag-word `subj.tag == member_index`, EXATAMENTE o
  `lower_variant_tag_eq_test` (`lower.tks:9205`); a numeração de membros = ordem de declaração
  (`store_variant_tag`, `lower.tks:3556,9333`) — preservada, logo byte-compatível. Exaustividade =
  `variant_covered` (`match.tks:418`), inalterado. **Para o A-wrap:** o checker AUTO-DESCE `match mi {
  MAlu as a => … }` para `match mi.case { … }` (ponte §1.1) — ou o autor escreve `.case`. Nenhuma
  extensão da engine.
- **B.** Subject = fat pointer de interface `{data;vtable}`. Discriminante = `subj.vtable ==
  &tk_vt_<Impl>_<Iface>` (lowering NOVO — comparar símbolo de vtable) OU type-id no slot 0 da vtable.
  Exaustividade = `impls_of_sealed_iface` sobre uma interface SELADA (conjunto de impls fechado) — engine
  e conceito NOVOS.
- **C.** Subject = ponteiro de objeto de classe. Discriminante = type-id-word em offset fixo (DP-1, ABI
  nova). Exaustividade = `subclasses_of` (base→[subclasses], NOVO).

O `plano-match-universal` (Parte 1) já prepara a base do `match`-universal (remover literal/range da
esquerda → `when`, exaustividade de conjunto-fechado). A Solução A precisa APENAS dessa Parte 1 + as 2
pontes de checker; NÃO precisa do discriminador de tag-de-classe (DP-1) nem de interface-selada.

---

## 5. Plano de fixpoint / reseed (Solução A)

Seed = o binário `teko` liberado anterior (`.gen0/teko`, `bootstrap/teko.c`). Disciplina: `cc -std=c2x`,
`--no-verify`, **NUNCA** `teko test .`.

### 5.1 Ordem (folhas → raízes; cada grupo gate-ável; des-ensina → altera → reseed → limpa → reseed)

1. **Pré-requisito (do `plano-match-universal`, Parte 1 INERTE):** `match`-sobre-classe/união
   generalizado + as pontes **auto-widen/auto-descend** do newtype (§1.1), com o `variant` AINDA vivo.
   Reseed 1 valida auto-reprodução. Este é o único passo que ADICIONA capacidade de checker; é aditivo.
2. **Grupo 2 (enum puro):** `Unique`, `ResidenceTier` → `enum`. Representação ENCOLHE (tagged→int);
   `match`-sobre-enum já existe (`check_enum_pattern`). Reseed.
3. **Grupo 1 (A-inline nos já-embrulhados):** `ExprKind`/`TExprKind`/`FSpecKind`/`TFSpecKind` inline nos
   wrappers. **Byte-idêntico** (mesma tag/ordem). Reseed.
4. **Grupo 4 (A-wrap), em ordem de dependência** (folhas primeiro): piloto `TypeBody` (exercita o codec,
   §5.3), depois `RegAssignment`/`LOp`/`MInst`/`MInstX86`/`RegexNode`/`JsonValue`, depois
   `Pattern`/`Statement`/`TypeExpr`/`ConstraintExpr`/`ConstValueKind`/`BindElem`/`PointsTo`/`BorrowedFrom`/
   `TStatement`, e `Type` POR ÚLTIMO (§6). Cada tipo: des-ensina o `variant` daquele nome → altera a decl
   + (se sem pontes) os sítios → reseed → limpa → reseed.
5. **Grupo 3 (inline):** `Decl`/`ItemKind`/`TItem`/`File`/`BindTarget` — remover o nome, inserir a união
   anónima em cada sítio. Reseed.
6. **Remover a FORMA `type X = variant …`** (e a rejeição de `type X = A | B` já existe) do parser/checker
   — a sintaxe `variant` some; a união anónima em var/param/return/campo/constraint fica. Reseed final.

### 5.2 Byte-identidade e o cabeçalho fat

- **A-inline (grupos 1/3) e enum (grupo 2):** byte-idêntico ou encolhe — fecham fixpoint como aditivo.
- **A-wrap (grupo 4):** o wrapper `struct` ganha o cabeçalho fat do item-14 (1 `uptr`, +8 bytes à frente
  do descritor de 24). Isto é a mesma "Metade B" de representação do item-14 — **gated pela rota C**
  (`plano-item14 §5`, ruling §11.3: o `gen2==gen3` NATIVO não é gate deste reseed; o gate é o
  self-reproduce da rota C congelada + superfície verde). **Optimização recomendada (carve-out do
  item-14):** um wrapper de **campo-único `case`, `readonly`, sem métodos que tomem `self`** pode
  lowerar-se TRANSPARENTE — representação = a da união interna, SEM cabeçalho — devolvendo byte-identidade
  também ao A-wrap. As ADTs de IR são imutáveis → todas `readonly type … = struct { case: … }` →
  candidatas ao transparente. Marcar isto como um pedido a item-14 (não re-abrir item-14 aqui; §8).

### 5.3 Wire `.tkb`/`.tkh` (`TypeBody` é o piloto)

`TypeBody` é escrito por tag-byte (`write_typebody`, `src/emit/tkb_write.tks:446-458`; leitor gémeo em
`tkb_read.tks`). Migrar `TypeBody` para newtype não muda a NUMERAÇÃO dos membros (mantém a ordem) → o wire
pode ficar estável. Se algum layout serializado mudar, bumpar `TKB_EXPR_VERSION`/`TKB_PROGRAM_VERSION`
(`tkb_frame.tks`) + `TKH_VERSION` (`tkh.tks:256`) NUM ÚNICO cruzamento. Round-trip de `TypeBody` migrado =
fixture 6.

---

## 6. `Type` — o caso mais entrelaçado (`src/checker/type.tks:166`)

`Type` tem o arm `Variant` (`type.tks` — `Variant as v => v.members`), que É o portador interno das
uniões anónimas do checker (`type_eq`, `expand_variant`, `variant_has_null_member`, `null_widens_into`).
Sob a Solução A, `Type` vira `readonly type Type = struct { case: Prim | Byte | … | Variant | … | Null }`;
o arm `Variant = struct { members: []Type }` PERMANECE — é a representação meta de `A | B` anónimo, que a
§9.D EXPLICITAMENTE preserva (a união anónima sobrevive). Ou seja: migrar `Type` NÃO elimina a noção de
`Variant`; só tira o NOME `Type` de ser um `variant` (passa a struct-wrapper). Fazê-lo POR ÚLTIMO (todos
os outros já migrados) minimiza o entrelaçamento. As ~15 funções TOTAL-sobre-`Type`
(`type_eq`/`subst_type`/`type_mangle`/…) ganham `match t.case { … }` (ou auto-descend).

---

## 7. Shapes que o implementer adiciona (Teko, full-Javadoc) — contra a forma declarada das deps

As 2 pontes de checker que tornam o A-wrap quase source-idêntico (compilam como esqueleto honest-stop hoje):

```teko
/**
 * newtype_union_field — if `name` is a §9.D newtype-over-union struct (exactly one field, whose type
 * is an anonymous `Variant`), return that field's union Type; else null. This is the predicate the
 * auto-widen and auto-descend bridges key off, so `Foo { … }` widens into the newtype and
 * `match x { Foo as f => … }` descends into the sole field WITHOUT a source `.case` hop. A struct with
 * zero, two+, or a non-union sole field answers null (an ordinary struct — inert).
 *
 * @param name   the resolved struct name
 * @param table  the folded type table
 * @return       the sole field's Variant Type when `name` is a newtype-over-union, else null
 * @see          teko::checker::expand_variant (the anonymous-union view this reuses)
 * @since §9.D
 */
fn newtype_union_field(name: str, table: TypeTable): Type | null

/**
 * widen_into_newtype — does a value of member type `from` auto-widen into the §9.D newtype struct
 * `to` (i.e. `to` is a newtype-over-union and `from` is one of its sole field's union members)? Lets
 * `MAlu { … }` flow into an `MInst` slot with no explicit `MInst { case = … }`, mirroring how a
 * member value widened into the retired `variant`. Inert unless `to` is a newtype-over-union.
 *
 * @param from   the source value's resolved type (a candidate union member)
 * @param to     the destination newtype-over-union struct type
 * @param table  the folded type table
 * @return       true iff `from` is a member of `to`'s sole union field
 * @since §9.D
 */
fn widen_into_newtype(from: Type, to: Type, table: TypeTable): bool
```

**Funções existentes que o implementer TOCA (não recriar):** `src/checker/match.tks` — `check_pattern:230`
(auto-descend quando o subject é newtype-over-union), `exhaustive:460` (inalterado — corre sobre o campo
união); `src/checker/resolve.tks` — `widens_into*` (chama `widen_into_newtype`), reusa `is_struct_name`;
`src/lir/lower.tks` — `lower_variant_construct`/`lower_variant_tag_eq_test` (inalterados; o newtype
transparente lowera para eles), `expand_variant`; `src/emit/tkb_write.tks:446`/`tkb_read.tks` (piloto
`TypeBody`).

---

## 8. Riscos + tensões de lei (com resolução) — HALT check

1. **[resolvido] valor→referência (o crux).** A Solução A não vira valor→referência (§0/§2). O variant já
   é descritor-fat+payload-boxed; A-inline preserva-o byte-a-byte; A-wrap é value + cabeçalho item-14.
   **Sem HALT** — o crux que o `plano-match-universal §4.2` marcou HALT-level resolve-se law-first
   escolhendo A. B (o caminho de interface) seria o gatilho da spine, mas é DIFERIDO, não escolhido agora.
2. **[reportar, não decidir] carve-out transparente do item-14 (§5.2).** Para A-wrap ser byte-idêntico,
   um newtype-de-campo-único-`case`-readonly deve lowerar SEM o cabeçalho fat. Isto é uma optimização do
   item-14, cujo doc está SELADO. **Resolução:** NÃO re-abrir item-14 aqui; REPORTAR o pedido para cima
   (o dono/integrador do item-14 decide). Se negado, A-wrap fica com +8 bytes de header, route-C-gated
   (custo, não bloqueio). Sem tensão de lei.
3. **[verificar] nomes-de-arm genéricos no grupo-2 enum.** `ResidenceTier` tem arms `Scope`/`Frame`/`Root`
   — nomes que poderiam ser usados como structs noutro lado. **Resolução:** o implementer CONFIRMA por
   grep que cada arm só é usado como membro do seu variant antes de converter a enum; senão fica A-wrap.
4. **[selado, sem tensão] posição de campo para a união anónima.** A §9.D lista var/return/param/constraint;
   a Solução A usa a união em CAMPO. Campos-com-união já existem (`T | null` em todo o lado, o campo `r:
   Reader`, `iface_value_field`) → campo é posição de variável admitida. Sem tensão.
5. **[dissolvido] membro-partilhado.** Sem herança na Solução A → sem single-base constraint (§1.4). O
   split A/C do plano-match-universal deixa de ser obrigatório.
6. **`Type`/arm `Variant` (§6).** Preservado como portador de união anónima; migrar por último. Sem tensão.

**Nenhuma tensão de lei genuína → NENHUM HALT necessário.** A Solução A passa todas as Leis: Teko-only
(newtype/enum são formas Teko existentes), W15/Javadoc (shapes acima), law-first (o crux resolve-se
escolhendo a forma que preserva valor), issue-100% (todas as 28 ADTs classificadas), seed-safe (ordem
folhas→raízes, A-inline byte-idêntico). O único item a RELEVAR para cima é o pedido de carve-out
transparente ao item-14 (risco 2) — melhoria de custo, não bloqueio.

---

## 9. Fixtures de regressão (input → exit code nativo esperado)

Grupo `own_native` (o compilador a compilar corpus próprio). Exit = valor observável do `main`.

1. **newtype tagged discrimina** — `type Sh = struct { case: Circ | Sq }`; `var s = Sh { case = Circ{…} }`;
   `match s { Circ as c => 1; Sq as q => 2 }` (auto-descend) → exit distinto por arm.
2. **auto-widen na construção** — `Circ{…}` fluindo direto num slot `Sh` (sem `Sh { case = … }`) compila e
   corre igual à fixture 1.
3. **exaustividade preservada** — `match s { Circ as c => … }` SEM `Sq` nem `_` → REJEITADO (o
   `variant_covered` conta os membros do campo união).
4. **enum puro (grupo 2)** — `Unique`/`ResidenceTier` como `enum`; `match u { UsUnique => 1; UsShared =>
   2; UsTop => 3 }` → exit por membro.
5. **A-inline byte-parity** — um programa que constrói/matcheia `Expr.kind` produz o MESMO exit
   antes/depois do inline de `ExprKind` (fixa a byte-identidade do grupo 1).
6. **round-trip `.tkb` do `TypeBody` migrado** — serializar/deserializar um `TypeBody` newtype e confirmar
   igualdade; artefacto de versão anterior rejeitado se houver bump.
7. **união anónima em campo/param preservada** — `A | B` num campo e num param continua a fazer `match`
   após remover as formas nomeadas (regressão do grupo 3 inline).
8. **`type X = variant …` agora é erro de parser** — mensagem guiada apontando para newtype/enum
   (fixture de rejeição), após o passo 6 da ordem.

---

## 10. Pontos de ritual (gate cheio obrigatório)

- **R1** — após o pré-requisito Parte-1 + as pontes de checker (aditivo, `variant` vivo): reseed cheio,
  fixtures 1-4.
- **R2** — após grupos 2 (enum) e 1 (A-inline): reseed; fixture 5 (byte-parity).
- **R3** — após CADA grupo/tipo do grupo 4 (A-wrap), des-ensina→altera→reseed→limpa→reseed; o piloto
  `TypeBody` tem gate próprio com fixture 6.
- **R4** — após grupo 3 (inline) e a remoção da forma `variant`: reseed final + fixtures 7, 8 + suíte.
- Bump de wire (§5.3), se necessário, num único cruzamento com gate dedicado.

---

*Grounding: todas as citações são `arquivo:linha` reais em `origin/fix/retirement`. Docs companheiros:
`plano-match-universal-e-migracao-variant.md` (a fundação do match + o §4.2 que este doc RESOLVE),
`plano-item14-value-struct-mutavel.md` (cabeçalho fat), `interface-value-type.md` (fat pointer + a
spine-gate do struct→interface), `mudancas-superficie-0.3.1.md` §9.2b/§9.4 (o ban selado).*

---

## 12. Os dois últimos (`TStatement`, `Type`) — SELADO (2026-08-14): falsos-forks, migração mecânica

A sessão filha `§9.D sweep` travou em `TStatement` (`src/checker/tast.tks:226`) e `Type`
(`src/checker/type.tks:166`) alegando "2 design calls". **Ruling do dono: ambos são falsos-forks — a
migração é 100% mecânica, sem uma linha de capacidade nova.**

**Q1 — união-de-uniões (`@TItem()` contém `@TStatement()`).** NÃO é fork. O expansor JÁ splica/achata
`@X()` aninhado no corpo `lowering` — precedente PROVADO: `src/parser/ast.tks:944`
`macro ItemKind() { lowering { UseDecl | @Statement() | Function | … } }` já chama `@Statement()` dentro
do corpo. Suporte via `rewrite_type_decl`→`walk_type` (`macro_expand.tks:269-294`) + recursão
depth-bounded (`max_macro_depth`, `macro_expand.tks:361`). E `union_collect` (`resolve.tks:1676`) achata
+ dedupa + null-normaliza.

**Q2 — o "duplo-null" de `@Type() | null`.** NÃO é colisão. Regra do dono: o `null`/`error` de
SUPERFÍCIE e o `Null`/`Error` de BACKEND são o MESMO elemento — o swap superfície→backend acontece no
front-end (`lexer.tks:357` `"null" → TokenKind::Null`; `resolve.tks:2032` `named_type_is_null(nt) →
Null {}`). No backend só existe `Null`; não há "null de superfície" solto pra colidir. Elementos
repetidos DEDUPAM sem erro (`union_collect` colapsa null repetido num líder — `resolve.tks:1929/1945/2623`).
Sempre foi assim (ex. `type T = S | error | null` com `S = A | null`). **Sem marcador novo, sem família
nova.** Verbatim do dono: *"o agente não precisa se preocupar com colisões, nunca ocorrerão, e precisa
usar as ferramentas que já deliberamos. Se uma macro não consegue chamar outra, então é ensinar a ser
feito."* — verificado: já sabe chamar (`ItemKind`), nada a ensinar.

**Crumb mecânico (para a filha fechar 32/32):**
1. `type TStatement = variant TBinding | … | TBlockStmt` (`tast.tks:226`) →
   `macro TStatement() { lowering { TBinding | … | TBlockStmt } }`.
2. `type Type = variant Prim | … | Null` (`type.tks:166`) →
   `macro Type() { lowering { Prim | … | Null } }`.
3. No corpo de `macro TItem()` (`tast.tks:302`): `checker::TStatement` → `@TStatement()`.
4. Reescrever os usos de `TStatement`/`Type` → `@TStatement()`/`@Type()`; os sites `Type | null` viram
   `@Type() | null` (o `union_collect` colapsa o null — sem tratamento especial).
5. Gate: reseed + `teko test .` no CI. Fixtures novas: round-trip de união-de-uniões (`@TItem()`
   contendo `@TStatement()`) + `@Type()`-anulável.

**Ciclo direto união→união (`A=B|C; B=A|C`) — NÃO existe (verificado 2026-08-14, ruling do dono: YAGNI).**
Grafo de arestas união→união mapeado: só `ItemKind→Statement` e `TItem→TStatement`, ambos SEM volta
(DAG). A recursão real é SEMPRE mediada por struct (ex. `Type`→`Variant` struct→`[]Type` slice), que
fecha pelo box + nome nominal do struct — não é expansão infinita. Como não há caso de ciclo direto,
**nenhum mecanismo de ciclo é necessário** (nem invariante "interpõe struct", nem box-no-ciclo). Se um
ciclo direto for introduzido no futuro, decide-se ali; hoje, não há o que resolver.
