# Plano — item 14: value-struct mutável (remover o bloqueio "struct é readonly")

> **Status:** DESIGN. Read+design apenas — NENHUM `.tks` de produto editado, NENHUM build, NENHUM
> reseed, NENHUM `teko test .` (fuga de memória do `monomorph` derruba o container — proibido em
> qualquer forma). Este documento É o artefacto; o único commit desta crumb é ele próprio.
> **Branch:** `fix/retirement` (drena sequencial, SEM PRs).
> **Fonte de lei:** `docs/design/mudancas-superficie-0.3.1.md` §13.1–§13.3 — SELADO (rulings do dono,
> abaixo em §0). O plano desenha À VOLTA deles; nunca os re-abre.
> **Irmão modelo (estrutura):** `docs/design/plano-secao9-operadores-e-value-type-methods.md`.
> **Lei permanente:** Teko-only (`.tks`; os gémeos C de checker/codegen/build CONGELADOS, exceção
> `src/runtime/teko_rt.{c,h}` + assert seed = C mantida), W15 + Javadoc-completo em TODA declaração,
> law-first, reseed disciplinado.
> **Seam com a onda §9 (operadores + value-type-methods, a aterrar AGORA):** ver §0.3 e §10 — os dois
> compõem, e este plano DELEGA a §9 a representação THIN dos subtipos de primitivo/enum/flags.

---

## 0. Rulings SELADOS (LEI — desenha-se à volta, não se re-abre)

### 0.1 Modelo de mutabilidade (§13.1)
1. **Tudo é `var`** — toda variável é mutável por default; NÃO há marca de tipo "mutable struct".
   Qualquer struct é mutável.
2. **`val` NÃO é binding de usuário** — é marca INTERNA: o alias de match (`as`, readonly raso) e os
   literais. Fora disso não se escreve `val`.
3. **Parâmetros de função/método comportam-se como `var`** (mutáveis no corpo).
4. **Mutação = escrita direta:** `s.x = v` para campo acessível; **property `set`** para o
   controlado/encapsulado (ex.: Intent — `_value` privado, só `pub set` escreve).
5. **Value semantics (struct) vs identidade (objeto):** struct atribuir/passar **copia o valor** (mutar
   um parâmetro/local é local); classe/serviço copia a **referência** (`a.b = v` muta o objeto
   compartilhado, propaga por identidade, sem `ref`). **`ref`** é a ponte do value type: alias do slot
   do chamador, reatribui tudo (`func(ref a: T) { a = T { … } }`) — preserva-se só o ponteiro.

### 0.2 `self` — ref implícito, e representação runtime (§13.2)
6. **No acesso, `self` é `ref`:** num método, `self` é ref implícito do receptor → a mutação **gruda** na
   instância e **não copia** o struct a cada chamada (a performance que motiva o item 14).
7. **Método mutante INFERIDO do corpo — SEM `mut fn`.** Quem escreve `self` é mutante; quem só lê é
   não-mutante; o compilador infere. **Chamar método mutante sobre receptor imutável** (literal, alias
   `val`) = **erro**.
8. **`self` serve em `struct`/`class`/`trait`/`service`** — **exceto** subtipos de primitivo/enum/flags,
   que são **`val-ref` readonly** (self ref só-leitura; não se mutam).
9. **Posições de `self`:** como TIPO (`static fn new(): self`, `p: self`); construção (`self { … }`,
   Block-B); acesso (`self.x` / `self::y` — aqui `self` é `ref`).
10. **Representação runtime:**
    - **objeto (class/service):** arena própria → ponteiro/identidade intrínseco; `ref` natural.
    - **struct (value):** é **FAT** — carrega um **cabeçalho** (mais simples que o de objeto: só
      `uptr`/`ptr`, o ponteiro para si) que habilita `ref`/`self`. **Cópia = nova materialização — novo
      cabeçalho, novo ponteiro** (preserva value semantics: mutar a cópia não toca o original).
    - **subtipo de primitivo/enum/flags (readonly):** é **THIN** — do tamanho do primitivo, **SEM
      cabeçalho**; o runtime **define `self` na hora da chamada** (self **transiente**). Por serem
      readonly, o self-ref transiente basta.

### 0.3 `readonly` — imutabilidade opt-in (§13.3)
11. Tudo é `var` por default; **`readonly` é o opt-in** (modelo C#), em dois níveis: **struct inteira**
    (`readonly struct Foo { … }` — todos os campos readonly) e **campo** (`readonly id: u64`).
12. **Regra:** um campo readonly (solo ou de struct readonly) só pode ser definido na **construção**
    (`self { … }`) ou por **valor default** na declaração; **nunca mutado depois**, nem por fora nem por
    dentro (um método que tentasse `self.id = …` sobre campo readonly = **erro**). É a garantia à prova
    de mutação INTERNA que a property só-`get` não dá — coexistem: `get`-only encapsula, `readonly`
    congela.

**Ordem do dono (§11):** item 14 é **ANTECIPADO** — caminho crítico da Intent (`pub set` que escreve
`self._x`, §10.3) e do `Ctx` performático. Entra cedo na onda de superfície.

---

## 1. Estado de HOJE — onde o item 14 aterra (achados, file:line)

### 1.1 Onde "struct é readonly" está bloqueado hoje
Não existe um único gate nomeado "struct é readonly"; o bloqueio é a soma de DUAS coisas:

- **Superfície (checker) — a mutabilidade da RAIZ do lvalue.** `field_write_root_writable`
  (`src/checker/typer.tks:5141`) admite `recv.field = v` só quando a raiz da cadeia é um binding
  gravável; caminha `a.b.c → a` (`lvalue_root`, `typer.tks:5111`), então protege a QUALQUER profundidade.
  A mensagem @5146 ("cannot assign to a field of immutable … there is no `ref mut` (B.21)") é o gate de
  mutabilidade. **Sob §1 (`var`-tudo) este gate passa-sempre para locais** — logo NÃO é o bloqueio real
  do item 14; é onde o **enforcement de `readonly` (§0.3) entra** (§3.2).
- **Semântica (runtime/codegen) — struct É passado por VALOR.** Um struct vive na pilha C por valor
  (`src/codegen/codegen.tks:1293-1294`, "a bare local is a safe stack-member copy … an ordinary STRUCT
  lives ON THE C STACK"). Um método recebe uma **cópia** do struct; `self.x = v` muta a cópia e a
  mutação **perde-se ao retornar**. É ESTE o "struct é readonly de facto" que o item 14 remove — pelo
  **cabeçalho fat** (§4) que torna `self` um ref cuja escrita gruda.

O receptor JÁ é definido gravável hoje: `is_method_receiver` (`typer.tks:7023`) devolve true para o 1º
parâmetro sem tipo (o `self` sintético), e `type_method` @7080 faz
`define(local, name, pt, is_method_receiver(f, i))` — logo `self.x = v` **tipa** hoje; só não **gruda**.

### 1.2 Onde `self`/receptor é baixado
- **Checker:** `type_method` (`typer.tks:7028`) reescreve o `type_ann` do receptor para `Named(struct)`
  (@7059-7068) e define-o gravável (@7080). `is_method_receiver` (@7023) = "1º param sem tipo".
- **Dispatch de instância:** `type_method_call` (`typer.tks:1739`; núcleo @1900-1929) resolve a decl do
  receptor, faz `match decl.body { StructBody => sb.methods; ClassBody => … ; _ => "method typing is
  deferred" }` (@1902-1913), acha o método por nome (@1918-1922) e valida `is_instance` (@1924). **É
  AQUI que entra o check "método mutante sobre receptor imutável" (§3.3)** — logo após `found`, antes do
  desugar da chamada. NOTA: `is_flags_named` @1741 devolve hoje "method typing is deferred" — esse
  honest-stop é LEVANTADO pela onda §9 (não por este plano; §10).
- **Codegen:** o receptor de um método de struct é emitido **por valor** (cópia de pilha, §1.1). O modelo
  de identidade JÁ existe para classes: `emit_struct_init_framed`/`__region` (`codegen.tks:~1422`,
  W10b.CLASS) e o fat-pointer de base/interface (`codegen.tks:1670-1672`, "`tk_base_<name>` data +
  vtable"). O cabeçalho de struct (§4) é uma versão MAIS SIMPLES disto (só `uptr`/`ptr`, sem vtable).

### 1.3 Onde a representação FAT/THIN viveria
- **Runtime C (MANTIDA — exceção à congelação):** `src/runtime/teko_rt.{c,h}` — o tipo do **cabeçalho de
  struct** (a palavra `uptr`/`ptr` que aponta para si) e os helpers de materialização/cópia moram aqui
  (é o único C que este plano toca; o codegen C gémeo fica CONGELADO).
- **Codegen nativo (`.tks`):** `src/codegen/codegen.tks` — o typedef de tipo de usuário (`tk_t_<mangle>`,
  `cb_tysym` @519-535) passa a EMITIR o cabeçalho como primeiro membro do struct; a construção
  (`self { … }`/`StructInit`) materializa um cabeçalho novo; a cópia de valor (atribuição/passagem)
  re-materializa (novo cabeçalho, novo ponteiro); o receptor de método passa a ser o **ponteiro do
  cabeçalho** (self = ref), não uma cópia.
- **LIR (`src/lir/lower.tks`):** o precedente EXATO do "self como ref" é o **`ref<T>` PARAMETER-position
  table** (`lower.tks:1141-1170`, "degrau 18") — um parâmetro cujo endereço escapa é baixado para MEMÓRIA
  e escrito através. O `self` de um struct é um ref auto-injetado com a MESMA maquinaria.
- **Subtipo de primitivo/enum/flags (THIN):** NÃO ganha cabeçalho — fica do tamanho do primitivo. O
  `self` transiente é montado na chamada. **Este caso é a representação que a onda §9 já baixa** (o
  lowering Named-value-type→tipo-máquina-base, §9 plano §4.2) — item 14 DELEGA (§10).

### 1.4 Gramática de struct/campo (onde o `readonly` entra)
- `StructBody` (`src/parser/ast.tks:602`) = `struct { fields; methods; implements; consts }` — **sem
  marca readonly**. `Field` (`ast.tks:621`) = `struct { name; type_ann; vis; is_intern; has_doc; doc }`
  — **sem marca readonly**.
- `parse_type_body` (`src/parser/parse_decl.tks:1034`): o ramo `struct` @1053-1060 consome
  `TokenKind::Struct`, a lista `& I …` (@1055) e o `{`; produz `StructBody`. **O marcador `readonly`
  precede `struct` aqui.**
- `parse_fields` (`parse_decl.tks:710`): o loop de membros lê `name : T` @735-740 e empurra `Field{…}`.
  **O marcador `readonly` precede o nome do campo aqui** (@732, antes de `is_name_at`).
- **`readonly` NÃO é token nem aparece no corpus:** grep no `src/**/*.tks` = **0 ocorrências** como
  identificador; não está em `keyword_kind` (`src/lexer/lexer.tks`). **Decisão law-first (precedente
  `trait` `parse_decl.tks:1049`, `operator` do §9):** `readonly` é **CONTEXTUAL** — só significa o
  marcador quando `Ident("readonly")` está imediatamente antes de `struct` (no body-position) ou antes de
  `name :` (no field-position). Em todo o resto continua um Ident vulgar. **Zero tokens novos no lexer.**

### 1.5 Codec `.tkb` (superfície dependente)
- `write_fields` (`src/emit/tkb_write.tks:372`) / `read_fields` (`src/emit/tkb_read.tks:616`): serializam
  cada `Field` (name+type_ann+vis+is_intern+has_doc+doc). Um `is_readonly` novo RIDE aqui, em paridade.
- `write_struct_body` (`tkb_write.tks:420`) / o leitor de StructBody (`tkb_read.tks:759-763`): um
  `is_readonly` de struct inteira RIDE aqui, em paridade.

---

## 2. Gramática — o marcador `readonly` (aditivo, seed-safe)

**Decisão de forma (law-first):** um `bool` novo em `StructBody` e em `Field`; `readonly` contextual (§1.4).
Um struct/campo SEM o marcador tem `is_readonly == false` e comporta-se exatamente como hoje
(byte-idêntico).

### 2.1 AST (`src/parser/ast.tks`)
`StructBody` (@602) e `Field` (@621) ganham um campo:

```
    /**
     * is_readonly — this struct is a `readonly struct` (§13.3): EVERY field freezes after
     * construction. A readonly struct's fields may be set ONLY inside a `self { … }` construction or
     * via a declaration default; any later write (external `s.f = v` or internal `self.f = v`) is a
     * checker error (§7). A bare `struct` (the default, all `var`) carries false and is byte-identical
     * to today's tree.
     *
     * @since item-14
     */
    is_readonly: bool
```

```
    /**
     * is_readonly — this individual field is `readonly` (§13.3), even inside an otherwise-mutable
     * struct: it may be set ONLY at construction (`self { … }`) or by a declaration default, never
     * mutated afterwards. A struct marked `readonly` propagates the freeze to every field, so each
     * Field also reads back true there. An ordinary field carries false. Orthogonal to `vis`/`is_intern`
     * (reach) and to a get-only property (encapsulation): `readonly` freezes, `get`-only encapsulates.
     *
     * @since item-14
     */
    is_readonly: bool
```

Todos os construtores existentes de `StructBody`/`Field` (parser, `synth.tks`, reconstrução `.tkb`,
LSP) passam `is_readonly = false` — aditivo-inerte.

### 2.2 Parser (`src/parser/parse_decl.tks`)
- **struct inteira** — em `parse_type_body` @1053, antes de `is_kind_at(…, Struct)`: se
  `Ident("readonly")` for seguido de `TokenKind::Struct`, consumir o `readonly`, entrar no ramo struct
  com `is_readonly = true` e propagar o flag a CADA `Field` do corpo (a regra §0.3: struct readonly ⇒
  todos os campos readonly).
- **campo** — em `parse_fields` @732 (e simetricamente em `parse_class_fields` se se decidir estender a
  classes; ver R-class-field §11): se `Ident("readonly")` preceder o nome do campo, consumir e produzir
  `Field { …; is_readonly = true }`.

### 2.3 Nova função de parse do marcador

```
/**
 * parse_readonly_marker — consume a CONTEXTUAL `readonly` marker at `pos` and report whether it was
 * present (§13.3). `readonly` is NOT a reserved word: it marks a `readonly struct` when
 * `Ident("readonly")` sits immediately before `struct`, or a `readonly` field when it sits immediately
 * before a `name :` field head; anywhere else it stays an ordinary identifier (precedent `trait`,
 * parse_decl.tks:1049). Returns the (possibly advanced) index plus a present-flag.
 *
 * @param tokens  the token stream
 * @param pos     the candidate marker index
 * @return        `{ present, next }` — `next` is past the marker when present, else `pos` unchanged
 * @since item-14
 */
fn parse_readonly_marker(tokens: []lexer::Token, pos: u64): ReadonlyMarker
```

`ReadonlyMarker = struct { present: bool; next: u64 }` (novo, em `parse_decl.tks` ou `result.tks`).

---

## 3. Checker — mutação de struct, `self`-como-ref, inferência de mutante, enforcement de `readonly`

Quatro peças, todas na fronteira do `typer.tks`. As três primeiras são SUPERFÍCIE (aditivo-inerte no
corpus; §5); a quarta (fat-header) é RUNTIME (§4).

### 3.1 `self` grava — já é permitido; o que muda é GRUDAR (runtime, §4)
O receptor já é definido gravável (`is_method_receiver` @7023, `define(… true)` @7080). Nada muda no
checker para PERMITIR `self.x = v` num struct — já tipa. O item 14 faz a escrita GRUDAR, e isso é o
cabeçalho fat (§4). O checker só precisa das novas RESTRIÇÕES (§3.2/§3.3) e da regra dos subtipos thin
(§3.4).

### 3.2 Enforcement de `readonly` (ruling 11/12)

```
/**
 * field_is_readonly — is field `fname` of the Named struct `sname` frozen (§13.3)? True when the field
 * itself is `readonly`, OR its declaring struct is a `readonly struct` (which freezes every field). A
 * frozen field is writable ONLY through a `self { … }` construction or a declaration default; every
 * `recv.field = v` / `self.field = v` targeting it is rejected (the construction path is a StructInit,
 * NOT a field-assign, so it never reaches this guard). Returns false for a non-struct, an unknown type,
 * or an ordinary field — inert on today's corpus (no struct is marked readonly).
 *
 * @param sname  the receiver's resolved struct name
 * @param fname  the field being written
 * @param table  the folded type table
 * @return       true iff the write must be rejected as a readonly violation
 * @since item-14
 */
fn field_is_readonly(sname: str, fname: str, table: TypeTable): bool
```

Inserção em `type_field_assign` (`typer.tks:5228`), depois de resolver o campo (@5238) e ANTES de
`field_write_root_writable` (@5251):

```
    // item-14 §13.3 — a readonly field (solo, or from a `readonly struct`) freezes after construction:
    // no field-assign may target it, inside OR outside the declaring method (the sole writer is the
    // `self { … }` StructInit, which never routes here). Inert on the corpus (no struct is readonly).
    match recv.type {
        Named as n => if field_is_readonly(n.name, lhs_fa.field, table) {
            return error { message = $"cannot assign to readonly field `{lhs_fa.field}` of `{n.name}` — a readonly field is set only at construction (`{n.name} {{ … }}`) or by a declaration default (§13.3)" }
        }
        _ => { }
    }
```

O mesmo guard cobre a property `set` que escreveria um campo backing readonly — mas isso já cai no
field-assign do corpo do setter, logo o guard acima basta (o setter escreve `self._x = v`, que reentra
em `type_field_assign`).

### 3.3 Inferência de método mutante + "mutante sobre receptor imutável = erro" (ruling 7)

```
/**
 * method_mutates_self — does instance method `m` of struct/class `sname` mutate its receiver (§13.2)?
 * True when the body (flattened, guard-first) WRITES self: a `self.field = …` / `self[i] = …`
 * assignment, a property `set` on `self`, or a call to another method that itself mutates self (or
 * mutates a struct-typed `self.field`). A method that only READS self is non-mutating. Inference is
 * from the BODY — there is NO `mut fn` marker (ruling 7). Memoized per (sname, method) to bound the
 * transitive-call walk; a recursion cycle is treated as non-mutating until a write is proven (least
 * fixpoint).
 *
 * @param sname  the declaring struct/class name
 * @param m      the method
 * @param table  the folded type table (to resolve callee methods for the transitive step)
 * @return       true iff calling `m` may mutate the receiver
 * @since item-14
 */
fn method_mutates_self(sname: str, m: parser::Function, table: TypeTable): bool
```

```
/**
 * receiver_is_immutable_target — is the receiver EXPRESSION of a method call an immutable target
 * (§13.2)? True for a value on which a mutating method must be rejected: a literal / temporary (roots
 * at no binding — `lvalue_root.reachable == false`), a `val` match-alias binding (readonly-shallow),
 * or a value whose STATIC type is a `readonly struct` (ruling 11). Under §13.1 a plain `var` local is
 * mutable, so an ordinary struct receiver returns false and the call is admitted. Mirrors
 * `lvalue_root` (typer.tks:5111) for the root walk.
 *
 * @param recv  the untyped receiver expression
 * @param rt    the receiver's resolved type
 * @param env   the typing environment (for the root binding's mutability / val-alias mark)
 * @param table the folded type table (for the readonly-struct test)
 * @return      true iff a mutating method call on this receiver must be rejected
 * @since item-14
 */
fn receiver_is_immutable_target(recv: parser::Expr, rt: Type, env: Env, table: TypeTable): bool
```

Inserção em `type_method_call` (`typer.tks:1739`), logo após achar `mfn` e validar `is_instance`
(@1924-1925), antes do desugar da chamada:

```
    // item-14 §13.2 — a MUTATING method may not run on an IMMUTABLE receiver (a literal, a `val`
    // match-alias, or a `readonly struct` value). Inference is from the body (no `mut fn`). Inert on
    // the corpus: no struct method mutates self today (structs are by-value), so nothing is rejected.
    if method_mutates_self(struct_name, mfn, table) && receiver_is_immutable_target(mc.receiver, recv_t, env, table) {
        return error { message = $"cannot call mutating method `{mc.method}` on an immutable `{struct_name}` — the receiver is a literal, a `val` alias, or a `readonly struct` (§13.2)" }
    }
```

### 3.4 Subtipo de primitivo/enum/flags = `val-ref` readonly (ruling 8/10)
Um método de instância declarado num subtipo de primitivo/enum/flags (a "casa" que a onda §9 abre) tem
`self` **readonly transiente** — qualquer corpo que MUTE `self` é **erro de DEFINIÇÃO**. Este check
casa 1-para-1 com o **ruling 10 da §9** ("instância readonly, sem campos"). **RECOMENDAÇÃO (seam §10):**
este invariante pertence à validação de value-type da §9 (`check_operator_invariants` / a coleta de
métodos de value-type); item 14 apenas CONFIRMA que a inferência `method_mutates_self` corre também
sobre esses corpos e que, para um dono prim/enum/flags, `mutates_self == true` ⇒ erro. Se a §9 aterrar
primeiro, item 14 só liga a chamada de `method_mutates_self` no ponto de coleta value-type; se item 14
aterrar primeiro, deixa um honest-stop nomeado que a §9 remove. Fixar em Javadoc no ponto de coleta.

---

## 4. Representação runtime — cabeçalho fat (struct) e self transiente (thin)

**Esta é a única metade NÃO puramente-aditiva do item 14** (muda a representação de TODO struct). O
argumento de fixpoint honesto está em §5; a sequência que a torna segura está em §8/§9.

### 4.1 O cabeçalho de struct (fat)
- **Runtime C (`src/runtime/teko_rt.{c,h}` — MANTIDA):** definir o tipo do cabeçalho — uma palavra
  `uptr`/`ptr` (o ponteiro para si), o mínimo que habilita `ref`/`self` (mais simples que o cabeçalho
  de objeto: SEM vtable, SEM `__region` de identidade — o struct continua value). Helpers:
  materializar um cabeçalho na construção; re-materializar (novo cabeçalho, novo ponteiro) numa cópia.

```
/**
 * tk_struct_hdr — the fat-struct header (item-14 §13.2): a single self-pointer word that makes `self`
 * a ref so an in-place mutation STICKS, WITHOUT turning the struct into an object (no vtable, no
 * identity region — value semantics hold). Every value-struct C typedef embeds this as its first
 * member; a COPY re-materializes a fresh header (new pointer), which is exactly what preserves value
 * semantics (mutating the copy never touches the original).
 *
 * @field self_ptr  the opaque word (`uptr`) pointing at this materialization
 * @since item-14
 */
typedef struct { uintptr_t self_ptr; } tk_struct_hdr;
```

- **Codegen nativo (`src/codegen/codegen.tks`):** (a) o typedef `tk_t_<mangle>` (`cb_tysym` @519-535)
  emite `tk_struct_hdr __hdr;` como PRIMEIRO membro; (b) a construção `StructInit`/`self { … }`
  materializa `__hdr` (o modelo é `emit_struct_init_framed`, @~1422); (c) a cópia de valor (atribuição,
  passagem por valor, retorno) re-materializa `__hdr`; (d) o receptor de método de struct passa a ser
  `tk_t_<mangle> *self` (ponteiro do cabeçalho), não `tk_t_<mangle> self` por valor — `self.x = v` grava
  através do ponteiro.
- **LIR (`src/lir/lower.tks`):** reusar a maquinaria do `ref<T>` PARAMETER-position (@1141-1170, "degrau
  18") — o `self` de um método de struct é um parâmetro auto-`ref`; o seu endereço é a MEMÓRIA do
  cabeçalho, escrita através. O implementador liga a auto-injeção do `self`-ref na mesma tabela.

### 4.2 O self transiente (thin) — DELEGADO à §9
Subtipo de primitivo/enum/flags fica **thin** (tamanho do primitivo, sem cabeçalho); o runtime define
`self` na chamada (transiente, readonly). **Este lowering É o da onda §9** (Named-value-type→tipo-máquina
-base, §9 plano §4.2). Item 14 NÃO adiciona cabeçalho a estes tipos e NÃO reimplementa o transiente —
apenas garante (via §3.4) que são readonly. Ver seam §10.

### 4.3 Codec/símbolos
- O membro `__hdr` no typedef de struct muda o LAYOUT de todo struct — os leitores/escritores de layout
  (`.tsym`/DWARF, `src/emit/*`) veem-no naturalmente (é um membro a mais). CONFIRMAR paridade
  C↔nativo do offset de campos (o `__hdr` desloca todos os campos em uma palavra).
- O mangling de método de struct não muda (o receptor passar a ponteiro é uma mudança de ABI de
  parâmetro, não de símbolo).

---

## 5. Segurança de FIXPOINT — o argumento honesto (DUAS metades)

Item 14 NÃO é uniformemente aditivo-inerte como a §9. Separa-se em duas metades com garantias
diferentes:

**Metade A — SUPERFÍCIE (aditivo-inerte, byte-idêntico).** Gramática `readonly` (contextual, `bool`
novo default-false), enforcement de `readonly` (§3.2), inferência de mutante + check de receptor
imutável (§3.3), regra thin-readonly (§3.4). Todas gated em predicados que são FALSOS no corpus atual:
- Nenhum struct do corpus é `readonly` ⇒ `field_is_readonly == false` em todo lado ⇒ o guard §3.2 nunca
  dispara ⇒ `type_field_assign` byte-idêntico.
- Nenhum método de struct MUTA self hoje (structs são by-value, a mutação seria perdida — padrão morto)
  ⇒ `method_mutates_self == false` no self-build ⇒ o check §3.3 nunca rejeita. **O implementador
  CONFIRMA por build**; se algum método do compilador mutar self e for chamado sobre receptor imutável,
  é um padrão pré-existente semanticamente morto — REPORTAR para cima, não inventar issue.
- `readonly` contextual = zero tokens novos ⇒ o lexer produz o mesmo stream.

Esta metade fecha o fixpoint byte-a-byte como a §9.

**Metade B — RUNTIME (cabeçalho fat — MUDANÇA DE REPRESENTAÇÃO, não byte-idêntica).** O `__hdr` em todo
struct muda o layout de TODO struct do compilador. Isto **quebra a byte-identidade com o seed
congelado** por construção — é uma migração de representação legítima, não uma regressão. O caminho
honesto é o do próprio doc de superfície, **§11**:
- O gémeo C de codegen está **CONGELADO** e emite structs THIN. Logo `gen0` (produzido pelo C) tem
  structs thin; `bin-a` (produzido por `gen0`) também; mas o codegen `.tks` de `bin-a` EMITE structs
  fat, então `bin-b` (produzido por `bin-a`) tem structs fat internamente. **`bin-a != bin-b` nativo** —
  esperado.
- **Resolução (ruling §11.3, VINCULATIVO):** *"O `gen2==gen3` nativo NÃO é gate do reseed nesta ordem —
  ele vira o marco que fecha a fase seguinte."* O gate do reseed da onda de superfície é o
  **self-reproduce da ROTA C** (thin, congelada, INTOCADA pelo `__hdr`) + provenance + **testes de
  superfície verdes**. A byte-identidade NATIVA com structs fat (`gen2==gen3`) é o **marco da onda de
  backend** (arena), NÃO um gate do item 14.
- Consequência prática: a Metade B aterra no codegen `.tks`, o reseed é gated pela rota C, e o fixpoint
  nativo fat é DEFERIDO. Isto é exatamente a disciplina "superfície → reseed → backend" do §11.

---

## 6. Codec `.tkb`/`.tsym` e superfícies dependentes (verificar, não presumir)
- **`.tkb`:** `Field.is_readonly` e `StructBody.is_readonly` TÊM de round-trip em paridade:
  `write_fields`/`read_fields` (`tkb_write.tks:372` / `tkb_read.tks:616`), `write_struct_body`/o leitor
  StructBody (`tkb_write.tks:420` / `tkb_read.tks:759`). Um `u8` por flag, em ordem fixa. Um round-trip
  que perca o flag quebra o enforcement. **Fixar por teste de round-trip** (crumb 1).
- **`.tsym`/DWARF (layout):** o `__hdr` desloca os offsets de campo — os emissores de símbolo de layout
  (`src/emit/tkh.tks`, `src/emit/tkb_frame.tks`) veem o membro extra; CONFIRMAR paridade de offset
  C↔nativo.
- **LSP (`src/lsp/*`):** um `readonly struct`/campo readonly deve simbolizar/navegar como os outros;
  verificar `symbols.tks`/`nav.tks` não estoiram no flag novo (honest-fallback já cobre o default).
- **`fmt` (`src/fmt/fmt.tks`):** o formatador deve re-emitir o marcador `readonly` (struct e campo) na
  posição canónica; sem isso um `teko fmt` apagaria o marcador. Ponto de sweep obrigatório.

---

## 7. Fixtures de regressão (inputs → códigos de saída nativos)

Layout (do corpus `examples/regressions/`): projeto `examples/regressions/<nome>/` com `.tkp`, `.tkr`
(Gherkin — `Then stdout pattern = "…"` no ACEITAR; `When compilation fails` + `Then diagnostic = "…"` no
REJEITAR), `main.tks`. A aritmética de `exit`/`println` codifica QUAL ramo correu.

> **NOTA de dependência de seed:** as fixtures ACEITAR que dependem de mutação GRUDAR (A1/A2) só passam
> DEPOIS da crumb do cabeçalho fat (§8 crumb 6). As de checker (A3, R1–R4) passam já na Metade A. O
> implementer sequencia as fixtures conforme a crumb que as habilita (§8).

### 7.1 ACEITAR — `examples/regressions/value_struct_mut/`
- **A1 — mutação in-place gruda (o coração).** `type Counter = struct { n: i32; fn bump() { self.n =
  self.n + 1 } }`; `var c = Counter { n = 0 }; c.bump(); c.bump()`; `exit c.n` → `2`. Prova que `self.x
  = v` GRUDA (cabeçalho fat). Sem o fat-header, sairia `0`. *(habilitada pela crumb 6.)*
- **A2 — value semantics: a cópia não toca o original.** `var a = Counter { n = 5 }; var b = a; b.bump()`;
  `exit a.n * 10 + b.n` → `56` (a=5, b=6). Prova nova materialização na cópia. *(crumb 6.)*
- **A3 — property `set` sobre struct (o caso Intent).** Um struct com `_v` privado, `get v` e `pub set v`;
  `s.v = 7`; `exit s.v` → `7`. Prova o desbloqueio do `pub set self._x` que motiva o item 14. *(crumb 6.)*
- **A4 — `ref` reatribui o slot inteiro.** `fn reset(ref c: Counter) { c = Counter { n = 0 } }`; muta o
  slot do chamador. `exit` codifica o antes/depois. Prova a ponte `ref` (ruling 5).

### 7.2 REJEITAR — `examples/regressions/value_struct_mut_reject/` (ou dobra em `diagnostics/`)
- **R1 — escrita a campo readonly, de FORA.** `readonly id: u64` num struct; `s.id = 9` →
  `Then diagnostic = "readonly field"`.
- **R2 — escrita a campo readonly, de DENTRO (self).** um método faz `self.id = …` sobre campo readonly
  → `Then diagnostic = "readonly field"`. Prova que readonly congela interna E externamente.
- **R3 — `readonly struct` inteira congela todos os campos.** `readonly struct` cujo método tenta
  `self.x = v` → `Then diagnostic = "readonly field"` (o flag propagou a cada campo).
- **R4 — método mutante sobre receptor imutável (literal).** `Counter { n = 0 }.bump()` (chamar mutante
  sobre um literal) → `Then diagnostic = "immutable"`.
- **R5 — método mutante sobre subtipo de primitivo (thin readonly).** um método de `type Celsius = i32`
  que faz `self = …` → `Then diagnostic` de self readonly. *(coordenar com a §9 — seam §10; se a §9
  ainda não aterrou, esta fixture entra com ela.)*

---

## 8. Sequência de crumbs (ordenada; cada uma gate-ável isoladamente)

Pontos de RITUAL (gate completo) marcados. Sequenciada por dependência de SEED: gramática primeiro,
depois checker (Metade A, byte-idêntica), depois runtime (Metade B, representação), depois fixtures,
depois reseed.

1. **Gramática — os flags `readonly`.** `StructBody.is_readonly` + `Field.is_readonly` (`ast.tks`);
   `parse_readonly_marker` + os dois hooks (`parse_type_body` struct @1053, `parse_fields` @732);
   propagação struct-readonly ⇒ campos; todos os construtores inicializam `is_readonly=false`. Codec
   `.tkb`/`.tsym` estendido em paridade + teste de round-trip. `fmt` re-emite o marcador. — *inerte:
   nenhum struct do corpus é readonly ⇒ árvore idêntica.* **RITUAL: gate completo** (toca tipos centrais
   + codec).
2. **Checker — enforcement de `readonly`.** `field_is_readonly` + o guard em `type_field_assign` (§3.2).
   — *inerte: `field_is_readonly == false` em todo o corpus.*
3. **Checker — inferência de mutante + receptor imutável.** `method_mutates_self` +
   `receiver_is_immutable_target` + o check em `type_method_call` (§3.3). — *inerte: nenhum método de
   struct muta self hoje; o implementer CONFIRMA por build.* **RITUAL: gate completo.**
4. **Checker — subtipo prim/enum/flags readonly (§3.4).** ligar `method_mutates_self` sobre value-types e
   erro se mutante; OU honest-stop nomeado se a §9 ainda não abriu a casa (seam §10). — *inerte.*
5. **Runtime C — o cabeçalho.** `tk_struct_hdr` + helpers de materialização/cópia em
   `src/runtime/teko_rt.{c,h}` (a exceção C mantida). Sozinho não muda codegen — inerte até a crumb 6
   emitir referências. — *aditivo em C.*
6. **Codegen/LIR — struct fica FAT.** `__hdr` no typedef `tk_t_<mangle>`; materialização na construção;
   re-materialização na cópia; receptor de método = ponteiro do cabeçalho (self=ref); auto-`ref` do self
   na tabela do `ref<T>` (`lower.tks`). CONFIRMAR paridade de offset C↔nativo. — *A CRUMB QUE QUEBRA A
   BYTE-IDENTIDADE NATIVA (Metade B, §5); a rede é o gate-por-rota-C do §11.* **RITUAL: gate completo
   (rota C reproduz + superfície verde; o `gen2==gen3` nativo fat é DEFERIDO ao backend, §5/§11).**
7. **Fixtures ACEITAR** (`value_struct_mut/`, A1–A4). Primeiro struct mutável REAL no corpus de teste ⇒
   exercita ponta-a-ponta (A1/A2/A3 provam o cabeçalho da crumb 6). **RITUAL.**
8. **Fixtures REJEITAR** (R1–R5). **RITUAL.**
9. **Reseed + PROVENANCE** (crumb final, §9).

---

## 9. Ritual de reseed + PROVENANCE (crumb final)

Só depois de todas as crumbs verdes e do gate da rota C:
1. `cc -std=c2x -w -O2 -I src/runtime -I src/assert bootstrap/teko.c src/runtime/teko_rt.c
   src/assert/assert.c -lm -o gen0` (o `teko_rt.c` inclui o `tk_struct_hdr` novo — é a exceção C mantida).
2. `TEKO_BACKEND=c ./gen0 build . --no-verify --release` → `bin-a` (rota C, structs THIN).
3. **Gate do reseed (§11.3):** self-reproduce da **rota C** (`bin-a` reproduz-se pela rota C) +
   provenance + **testes de superfície verdes**. **O `gen2==gen3` NATIVO (structs fat) NÃO é gate deste
   reseed** — é o marco que fecha a onda de backend/arena.
4. Harvest: `bootstrap/teko.c` (novo seed) + `bootstrap/PROVENANCE` (novo hash/proveniência).
5. **NUNCA correr `teko test .` em forma alguma** (fuga de memória do `monomorph` derruba o container).
   Gate por `--no-verify` + os `scripts/*.sh`.

---

## 10. Seam com a onda §9 (operadores + value-type-methods) — os dois compõem

A onda §9 (`plano-secao9-operadores-e-value-type-methods.md`) está a aterrar AGORA. Os dois trabalhos
TÊM de compor. Os pontos de contacto:

1. **Representação THIN dos subtipos de primitivo/enum/flags — PROPRIEDADE DA §9.** A §9 baixa um
   `type Celsius = i32 { … }` / `enum` / `flags` para o seu **tipo-máquina base** (§9 plano §4.2,
   Named-value-type→base). Item 14 CONCORDA: estes são **thin**, sem cabeçalho, self transiente readonly
   (§0.2 ruling 8/10). **Item 14 NÃO adiciona cabeçalho a estes tipos e NÃO reimplementa o transiente** —
   delega à §9. O cabeçalho fat é **STRUCT-ONLY**. Não-sobreposição explícita: struct → fat header
   (§4.1); prim/enum/flags subtype → thin (§9).
2. **`method_mutates_self` sobre value-types = ruling 10 da §9.** A §9 declara métodos de instância
   readonly em value-types (ruling 10). Item 14 §3.4 ergue o MESMO invariante do outro lado: um método
   que mutaria `self` num subtipo prim/enum/flags = erro. **RECOMENDAÇÃO:** o invariante mora em
   `check_operator_invariants` da §9 (que já valida "instância readonly"); item 14 só liga a inferência.
   Coordenar para NÃO duplicar o diagnóstico.
3. **`type_method_call` `match decl.body` (`typer.tks:1902-1913`) — ambos estendem.** A §9 acrescenta os
   value-type nodes ao `match`; item 14 insere o check de mutante-sobre-imutável (§3.3) no MESMO bloco
   (após `found`). **Seam:** aplicar as duas edições em ORDEM — a §9 primeiro alarga o `match` (mais body
   kinds resolvem métodos), depois item 14 adiciona o gate de mutabilidade que corre para TODOS eles.
   Sem conflito lógico; é um merge de vizinhança no mesmo `fn`.
4. **`collect_type_member_signatures` (`collect.tks:363`) — ambos tocam o `_ =>`.** A §9 despacha os
   value-type nodes para `collect_method_signatures`; item 14 não precisa de nada NOVO aqui, mas a
   inferência de mutante (§3.3) LÊ os method sets que a §9 popula. Item 14 depende do que a §9 coleta —
   NÃO o contrário.
5. **NÃO adicionar um campo a `Function` para "mutante".** A §9 já adiciona `Function.is_operator` (§9
   plano §2.1) — mais um `bool` no mesmo node força um merge de codec (`write_functions`/
   `read_functions`). **RECOMENDAÇÃO (evita a briga):** item 14 computa a mutação por ANÁLISE on-demand
   (`method_mutates_self`, memoizada por identidade de método), **sem** um flag armazenado — logo NÃO
   toca `Function` nem o seu codec, e não colide com a adição da §9.

**Resumo do seam:** item 14 = **cabeçalho fat de struct** + enforcement de `readonly` + inferência de
mutante. §9 = **value-type methods thin** (prim/enum/flags) + operadores. A única sobreposição física é
o `match decl.body` de `type_method_call` e a validação readonly de value-type — ambas resolvidas por
ordem de aplicação e por delegação (item 14 delega o thin à §9). Nenhuma tensão de lei.

---

## 11. Riscos + tensões de lei (com resolução recomendada)

- **R-repr (o busílis).** O cabeçalho fat muda a representação de TODO struct ⇒ quebra a byte-identidade
  nativa com o seed. **Resolução:** é a Metade B (§5), gated pela ordem `superfície → reseed → backend`
  do §11.3 — o gate do reseed é a **rota C** (thin, congelada); o `gen2==gen3` nativo fat é o marco da
  onda de backend, NÃO um gate do item 14. É a letra do doc de superfície selado. **Sem tensão de lei**
  (a byte-preservação é da rota C; a mudança de representação é explicitamente mandada e sequenciada).
- **R-inference-corpus.** `method_mutates_self` + check de receptor imutável são inertes SÓ se nenhum
  método de struct do compilador muta self e é chamado sobre um receptor imutável. **Resolução:** o
  implementer CONFIRMA por build (crumb 3); qualquer caso surgido é um padrão pré-existente
  semanticamente morto (struct era by-value) — REPORTAR para cima, não converter em issue. Sem tensão de
  lei.
- **R-tkb (codec).** `is_readonly` (campo + struct) e o `__hdr` (offsets) têm de round-trip/paridade.
  **Resolução:** crumb 1 fá-lo em paridade + teste de round-trip antes de qualquer semântica depender.
  Sem tensão de lei.
- **R-class-field readonly (âmbito).** O §13.3 enquadra `readonly` em STRUCT (campo e struct inteira).
  Um campo readonly de CLASSE é uma extensão natural (`parse_class_fields`), mas o design sela struct.
  **Resolução:** manter o âmbito em struct; se surgir necessidade de readonly em campo de classe, é
  ADJACENTE — REPORTAR para cima, não alargar por conta própria. Sem tensão de lei.
- **R-self-como-ref (LIR).** O `self` de struct passar a auto-`ref` é novo; a rede é o precedente do
  `ref<T>` PARAMETER-table (`lower.tks:1141-1170`). **Resolução:** reusar a maquinaria existente; sem
  esquema novo. Sem tensão de lei.
- **R-seam-§9.** Duas ondas tocam `type_method_call` e a validação de value-type. **Resolução:** §10 —
  ordem de aplicação + delegação do thin à §9 + análise on-demand (sem campo novo em `Function`). Sem
  tensão de lei.
- **Sem tensão de lei genuína identificada — NENHUM HALT necessário.** O item 14 encaixa num desenho
  law-first coerente: a mutação-que-gruda é o cabeçalho fat (mandado, §13.2); o `readonly` é opt-in
  C#-style (mandado, §13.3); o fixpoint nativo é deferido pela ordem selada do §11; o seam com a §9 é
  resolvido por delegação. A Metade A fecha byte-idêntica; a Metade B é uma migração de representação
  sequenciada, não uma regressão.
