# Modelo de referências transparentes — `Ref<T>` / `ref` (design 0.3.1)

> **Status:** design ratificado pelo dono (2026-07-11), verificado adversarialmente; **7 casos fechados 2026-07-13** (tripartite law, mandatory `ref` modifier, never-null `Ref`, null-operator transparency, swap-value, unsafe-only sigils, mutating methods). **NÃO implementado.** O `Ref<T>` atual do compilador é `.value`-based; este doc especifica o redesign. Issue-mãe: **#498**. Companheiro: **#497** (remoção de `-> void`). Pendente: ratificação Marshall (#539) + soundness-pass auto-deref.

Este documento consolida o modelo de referências da Teko: a superfície (sintaxe + acesso), a semântica (as regras R1–R11), o resultado da verificação de soundness (a raiz única de falha), as 6 emendas que a fecham (A1–A6, = os requisitos da *spine*), e os casos abertos.

---

## 1. Intenção

`Ref<T>` é um **invólucro transparente**: age como `T`. A intenção original nunca foi acessar `.ptr`/`.value` — é acesso **direto**, e quem quer o ponteiro cru desce por **Marshall** (território `unsafe`/`Ptr<T>`). O modelo fecha dois flancos ergonômicos:

- **verbosidade** — não há `.value`; `Ref<T>` lê como `T`;
- **conta/bits** — nunca há sigilo de deref competindo por precedência (o "Lisp de parênteses").

E dá uma garantia de memória **sem GC** (arenas lexicais): sob as regras + a *spine*, não se constrói um dangling por acidente.

---

## 2. A matriz binding × aliasing

Dois eixos **ortogonais**: `mut` (escrita) × `Ref<T>` no tipo (aliasing). 2×2, **3 legais + 1 ilegal**:

|            | valor `T`               | referência `Ref<T>`              |
|------------|-------------------------|----------------------------------|
| **`let`**  | snapshot **imutável**   | **ILEGAL** (referência imutável)  |
| **`mut`**  | valor local **mutável** | **alias** mutável (o ponteiro)   |

As três legais **não se confundem**:

- `let x: T` — não rebinda, não muta. Cópia congelada.
- `mut x: T` — muta o local, mas **não é ponteiro**; não aliasa nada. Anexar isto a um `Ref<>` **copia**.
- `ref x: T` — o único que aliasa; write-through (tipo subjacente `Ref<T>`).

A célula proibida (`let x: Ref<T>`) diz: **não existe referência imutável.** Leitura imutável ⟹ cópia (`let x: T`). Consequência deliberada: **referência ⟹ sempre canal de escrita; imutabilidade ⟹ sempre posse de valor.**

### Lei tripartida (dono 2026-07-13)
**`let` protege tudo** (imutabilidade profunda sobre semântica de valor — nada dentro mutua); **`mut` desbloqueia o binding e seu interior**; **`ref` desbloqueia o valor no ponteiro**. EXCEÇÃO: **objetos de classe detêm suas arenas** (semântica de referência) — mesmo sob `let`, mutação via MÉTODOS é permitida (`let a: Classe = {}; a.set_name("B")` ok — a mutação ocorre no escopo da classe); escritas diretas externas de campo permanecem barradas em `let` (R10). **Consequências:**
  - (i) Métodos de struct (tipo-valor) que escrevem `this.field` são barrados em binding `let` — o checker infere mutabilidade a partir do corpo, sem sintaxe nova.
  - (ii) Escrita através de um campo `Ref` dentro de um valor `let` é barrada (o canal de mutação vive dentro do valor congelado).

---

## 3. Sintaxe: o modificador `ref` — forma PRIMÁRIA e EXCLUSIVA

`ref` é o **3º modificador de binding**, **forma PRIMÁRIA e EXCLUSIVA** de declarar uma referência. **`mut x: Ref<T>` NÃO EXISTE como binding syntax** — `Ref<T>` é só tipo (em posições-de-tipo). A distinção valor-vs-referência só importa no binding (nos use-sites a transparência iguala tudo), então mora ali; o tipo anotado fica limpo.

| binding      | é                      | tipo subjacente |
|--------------|------------------------|-----------------|
| `let x: T`   | valor imutável         | `T`             |
| `mut x: T`   | valor mutável          | `T`             |
| `ref x: T`   | referência mutável     | `Ref<T>` (subjacente) |

- `ref` é **inerentemente** alias-mutável — não há `let ref`/`mut ref`. A regra "sem referência imutável" cai estruturalmente.
- Param-borrow: `fn accumulate(ref acc: []int, x: int)`.
- **O modificador `ref` é OBRIGATÓRIO em TODA posição de declaração** (dono 2026-07-13): campo de struct, campo de classe, variável, argumento. O tipo `Ref<T>` descreve os **níveis internos**; o modificador descreve o nível **mais externo**. Exemplos:
  - `type a = struct { b: Ref<Ref<T>> }` = ERRO COMPTIME (falta modificador `ref` no campo)
  - `type b = struct { ref c: Ref<T> }` = ok (modificador = nível externo, anotação = níveis internos)
- **O tipo `Ref<T>` NÃO desaparece** — vive em posições-de-tipo sem modificador de binding (args genéricos `[]Ref<T>`, `Map<K, Ref<V>>`, aninhamento interno), e retornos (`-> ref T`). Binding **sempre** usa o modificador `ref`; posições fora de binding usam anotação `Ref<T>` para níveis internos.

### Aninhamento — `Ref<Ref<T>>` / `**T`
O modificador `ref` dá o nível **mais externo**; os tipos `Ref<T>` (anotações) dão os internos. Regra: **modificador = nível externo, anotações = níveis internos, em toda posição de declaração** (dono 2026-07-13):

```
ref x: T            → Ref<T>            (*T)
ref x: Ref<T>       → Ref<Ref<T>>       (**T)
ref x: Ref<Ref<T>>  → Ref<Ref<Ref<T>>>  (***T)
```

Cada `Ref<>` além do primeiro vai na anotação de tipo. Aninhamento profundo é raro e **verboso, sem açúcar** — é o preço a pagar. Posições fora de binding (args genéricos, aninhamento interno) usam só a anotação `Ref<T>`, sem modificador.

### `ref x: []T` — tipo subjacente `Ref<[]T>`, mas inicializa com o VALOR
O **tipo subjacente** é `Ref<[]T>` (**alias do array inteiro**, uma indireção sobre o array todo; ≠ `[]Ref<T>`, array de aliases). Mas você **NÃO** inicializa com um `Ref<[]T>` — inicializa com o **próprio valor** (o desugar encapsula, §4.1):

```
ref x: []T = [t1, t2]           // inicializa com o VALOR; o desugar ENCAPSULA num Ref (= R5)
ref x: []T = marshall(algumptr) // inicializar a partir de um ptr cru precisa de Marshall (explícito)
```

`fn fill(ref buf: []int) { buf[0] = 9 }` faz write-through no array do caller. O modificador aplica no nível externo (o array); `[]` é parte do tipo apontado.

---

## 4. Transparência & auto-deref dirigido por tipo

`Ref<T>` **age como T** — auto-deref para campos/métodos/operadores. Sem `.value`/`.ptr`/`.deref` de superfície.

O auto-deref é **dirigido por tipo**: descasca **exatamente** quantas camadas `Ref` o tipo esperado no ponto de uso exigir — não "até o fim" cego.

```
x: Ref<Ref<T>>
x + 1              // contexto quer T      → descasca 2 → o valor
ref y: T = x       // contexto quer Ref<T> → descasca 1 → o Ref do meio
f<U>(x) // U livre → 0 descasca, passa o Ref inteiro
```

Consequências: (a) acesso a chained-refs é transparente **pro valor** e ao mesmo tempo o nível do meio é **endereçável via tipo**, sem sigilo; (b) contra um `U` genérico livre não há descasca-surpresa (é por isso que genéricos são sãos); (c) é o mecanismo de refs+overload do C++ / conversão implícita encadeada. Custo: acopla o deref à resolução de tipos no checker.

### 4.1 O desugar é BIDIRECIONAL (dono 2026-07-11)
A transparência não é só descascar — é uma fronteira de **mão dupla** entre **valor** e **Ref**, implícita nos dois sentidos; e a fronteira **`ptr` ↔ `Ref`** é sempre explícita via **Marshall**.

| | valor ↔ Ref — **desugar IMPLÍCITO** | ptr ↔ Ref — **Marshall EXPLÍCITO** |
|---|---|---|
| **Encapsular** (valor → Ref, no init/attach) | `ref x: T = <valor>` → embrulha o valor num Ref (= R5, copia na arena) | `ref x: T = marshall(p)` |
| **Descascar** (Ref → valor, no uso) | usar `ref x` em contexto de valor → auto-deref dirigido por tipo (§4) | `marshall(x)` → o ptr cru |

Ou seja: você **inicializa/atribui** um `ref` com um **valor** (o desugar encapsula) e o **usa** como valor (o desugar descasca) — em nenhum dos dois você escreve `Ref<>` nem sigilo. Só o cruzamento com o **`ptr` cru** exige o Marshall explícito (é a fronteira `unsafe`). A direção *encapsular* é a que faltava: o `= Ref<[]T>` do rascunho anterior estava errado — o RHS é o **valor**, não um `Ref`.

### 4.2 Transparência null em `Ref<T?>` (dono 2026-07-13)
**Uma referência é NUNCA nula** — se algo é nulo, é o VALOR no ponteiro. Assim `ref x: T?` = `Ref<T?>` (pointee-opcional, não ponteiro-opcional). **`Ref<…>?` é ERRO DE COMPILAÇÃO em toda posição de tipo e profundidade** (ex.: `Ref<T>?`, `Ref<Ref<T>?>` — mensagem: "uma referência sempre existe uma vez declarada — coloque o `?` no tipo do valor (`Ref<T?>`)"). `Ref` em qualquer tipo soma é REJEITADO. O lowering permanece sempre bare `T*` não-nulo em cada camada.

**Operadores null (`?.`, `??`, `==`, `match`) descascam um nível `Ref` e aplicam os operadores T?** normais (uma extra load, sem novo layout); escrever `t = null` = escrever-through nulo no slot (R4 intacto). **LEI: nenhum narrowing flow-sensitive através de `ref`** (outro alias pode nular o slot entre check e uso) — cada acesso relê a presença via `?.`/`??`/`match`. Binding deve ser anotado `ref t: T? = null` (anotar `T` com init nulo é rejeitado).

---

## 5. Marshall & a fronteira `Ref`/`Ptr`

- **Marshall** é a fronteira explícita do **`ptr` cru** (FFI, `Ptr<T>`), nos **dois sentidos**: `marshall(p)` embrulha um ptr num `ref`; `marshall(x)` extrai o ptr de um `ref`. É o único cruzamento `unsafe`. *(Spelling TBD — ver casos abertos.)*
- **Correção (dono 2026-07-11):** Marshall **NÃO** cobre o `valor ↔ Ref` — esse é o **desugar implícito** (§4.1, encapsular/descascar). A cópia `Ref→let` (R8) é a direção *descascar* do desugar, **não** Marshall.
- **`Ptr<T>` é UNSAFE-only.** `Ptr → Ref` pode **panicar** (tipo incompatível, estouro de memória; checado). Até FFI, se não for `unsafe`, usa **`Ref`**. Relação: `Ref` = seguro/transparente/default; `Ptr` = cru/explícito/`unsafe`, via Marshall.

### Rebind de referências & swap
Atribuição `ref x = ...` é sempre **write-through** (R4), nunca **rebind** (re-apontar a referência pra outro lugar). O binding é **fixo** por design (C++-style). Para trocar o conteúdo de duas referências — operação rara e pinned — use a função **SAFE** `teko::marshall::swap<T>(ref a: T, ref b: T)`: troca VALUE-level (ambas apontam aos mesmos slots após, mas os valores trocados); swap de referências cru (rebind de ponteiro) fica impossível fora de `Ptr`/`unsafe`. Marshall especifica a função completa em `docs/design/marshall-spec.md` (PR #539, design-ahead) — ver lá. Ponteiro rebind cru só via `Ptr<T>` / Marshall (territorio `unsafe`).

---

## 6. Semântica (as regras base R1–R11)

- **R1. Tripartida: protect, unlock, allow (dono 2026-07-13).** `let` protege tudo (nada mutua); `mut` desbloqueia o binding e interior; `ref` desbloqueia o valor no ponteiro. EXCEÇÃO: objetos de classe — mesmo sob `let`, mutação via métodos é permitida (occurs na arena da classe); escrita direta externa barrada em `let` (R10). Dois eixos ortogonais **em binding**: `mut` = escrita do local; `ref` = alias de escrita. Tipos diferentes.
- **R2. Transparente.** `Ref<T>` age como `T` (auto-deref dirigido por tipo, §4). Ponteiro cru só via Marshall.
- **R3. Exige mut & nunca nula.** `let x: Ref<T>` é ilegal (referência imutável = contradição). `ref` já é inerentemente mut. **`Ref<…>?` é ERRO em toda posição de tipo** (dono 2026-07-13); pointee-null é `Ref<T?>`, não `Ref<T>?`.
- **R4. Atribuição = write-through, nunca rebind.** `r = v` escreve `v` **através** de `r` no `T` aliasado; o ponteiro não muda. Rebind não se faz por `=` (default: binding fixo, estilo C++).
- **R5. Copy-on-attach.** Anexar um valor **não-Ref** a um destino `Ref<>` materializa uma **cópia** na arena do destino. Ponteiro nunca acontece implícito.
- **R6. Retorno materializa na arena do caller.** Retornar um valor local como `ref T` copia-o pra arena do caller (R5) → seguro.
- **R7. Borrow-down aliasa.** Passar uma variável `mut` viva a um param `ref` aliasa o storage do caller (write-through muta o do caller). Seguro porque o storage do caller sobrevive à chamada. *(Restringido por A2/A5.)*
- **R8. Ref→let = desugar-descasca-copia** (§4.1, direção *descascar* — NÃO Marshall). `let y = someRef` copia o valor; `y` é `T` (cópia). O `let` é firewall de imutabilidade + de escape.
- **R9. "Referenciável" é opt-in.** `mut x: T` é mutável mas não referenciável-pra-cima; um `Ref` construído sobre ele (fora do borrow-down R7) é **sobre cópia** (R5). Ponteiro genuíno que escapa exige `ref`/`Ref<>` explícito → controle manual.
- **R10. Encapsulamento.** Escrita direta externa de campo em `let a` **é barrada** — exige binding `mut`. Exceção: métodos de classe podem mutar o objeto mesmo sob `let` (mutação no escopo da classe, via R1). Métodos de struct que escrevem `this.field` são barrados em binding `let` (checker infere mutabilidade a partir do corpo).
- **R11. Sem GC; arenas lexicais.** Um `Ref` é válido só enquanto viva a arena que segura seu alvo.

### Closures
- Uma closure captura `ref x` como **alias** (o "sem keyword ref" antigo está aposentado).
- **Closure literal pertence à arena onde é definida** → **não escapa por default** (o default conservador que a análise pede). Escapar só se as capturas forem escape-safe (por-valor materializam à la R6; captura `ref` exige o alvo sobreviver ao destino, senão erro).

---

## 7. Verificação de soundness — por que R1–R11 sozinhas falham

Verificação adversarial (45 agentes, 8 frentes) **derrubou** a afirmação "não se constrói dangling por acidente; o único resíduo é Ref→Ref subindo". **12 buracos confirmados** (frentes `generics` e `copy-alias-seam` voltaram limpas).

### A raiz única (10 dos 12)
**Copy-on-attach dissolve só o caso ESCALAR/folha. Um `Ref` se esconde DENTRO de um agregado — e copiar o container copia o PONTEIRO, não o alvo.**

```teko
struct Holder { r: Ref<int> }
fn leak() -> Holder {
    mut local: int = 42
    return Holder { r = local }   // R5 copia `local` na arena de leak; r aponta pra lá
}                                  // retorno-por-valor copia os BYTES = o ponteiro r, não o pointee
mut h = leak();  h.r = 99          // write-through na arena MORTA → UAF, sem Ref no retorno
```

Mesmo bug por 4 caminhos: campo Ref em struct; `[]Ref<T>` retornada (elementos não migram); closure capturando Ref que escapa; `let b = colecaoDeRefs`. A análise atual (`escape.tks`) é one-depth, sem points-to graph.

### Duas classes de resíduo além do "Ref→Ref subindo"
- **`defer { mem::free(r) }` num `ref` emprestado** libera o storage vivo do *caller* — não é up-flow; é "free de algo aliasado por Ref vivo". O modelo não tem noção de **posse**.
- **DI `#singleton` com campo `Ref` pra `#scoped`** dangla quando a região da request cai — up-flow via `#wire` gerado, driblando o check de código.

---

## 8. As 6 emendas (A1–A6) — os requisitos da *spine*

As emendas **não são remendos**: são a especificação da *spine* (L1 do remodel, "the whole safety bet", hoje unbuilt). A ergonomia (§2–§6) permanece intacta; o que muda é que a **enforcement** de segurança é uma engine de lifetime (tipo Rust/Cyclone), não um checker one-depth.

- **A1 — Copy-on-attach + escape TRANSITIVOS.** Materializar/verificar **cada aresta `Ref` alcançável** dentro de agregado/coleção/closure no retorno/store que cruza arena — não só o topo. O escape checker vira **field-sensitive + transitivo**. *(Fecha: struct-field, collection-return, collection-copy, closure-capture, o soundness-hole duro.)*
- **A2 — Borrow-down é NÃO-ESCAPANTE.** Um `Ref` de borrow-down só flui **pra baixo**; guardá-lo pra cima (out-param mut, campo, coleção, closure que escapa) = erro de compilação. *(Fecha: collection-store, closure-two-hop, parte do DI.)*
- **A3 — Precedência R5-vs-R7 no param.** Não-Ref **lvalue** → param `ref` = **R7 alias**; não-Ref **rvalue/temporário** → param `ref` = **R5 cópia** (na arena do callee, não-escapante). Distinção lvalue/rvalue explícita. *(Fecha: o conflito de regra genuine-escape/copy-alias-seam.)*
- **A4 — `ref x: T` EXIGE init na declaração** (definite-assignment, estilo C++). R3 só barra `let Ref`; um `ref` sem init + R4 (write-through never rebind) = deref de lixo no 1º `=`. Sem binding `ref` não-inicializado. *(Fecha: uninit-Ref.)*
- **A5 — Borrow-down composto no path + R10.** Borrow-down do place `p` só se **todo segmento** do access-path é `mut` (binding-raiz mut E cada campo mut). Write-through via `ref` emprestado **é escrita externa** (sujeita a R10). Path enraizado em `let` nunca é borrow-elegível. *(Fecha: referenceable-path.)*
- **A6 — Free exige posse + DI monotônico.** `mem::free` (e todo free consumidor) **ILEGAL** via `ref` não-dono (borrow-down/não-owning); o resíduo ganha uma **2ª classe**: "free de storage aliasado por Ref vivo", rastreada interproceduralmente. `#wire` roda check de **monotonicidade de lifetime** (Ref de holder-L só liga a provider ≥L; `singleton ≤ scoped ≤ transient` por profundidade de região) **ou** desugar antes do pass de escape. *(Fecha: defer-free, DI-captive.)*

### A lei que resume a análise
**Ir em direção ao valor (descascar) é sempre seguro; afastar-se dele (embrulhar +1 nível) exige um lastro mais-longevo que um local não tem.**

```teko
fn a<T>(ref b: Ref<T>) -> ref T        // SEGURA: descasca 1 (T** → T*); o alvo sobrevive ao caller ⟹ a `a`
fn a<T>(ref b: T)      -> ref Ref<T>   // REJEITADA (genérico): produzir Ref<Ref<T>> exige aliasar um slot
                                       // Ref<T> ≥ caller; o único é o param b, cujo slot morre com `a` → escape (A2)
```

---

## 9. Estratégia de enforcement — honest-stop progressivo

Até a análise transitiva (a *spine*) existir: `Ref`-dentro-de-agregado-que-escapa, free-via-borrow, e `#wire` cross-lifetime viram **erro de compilação conservador — nunca aceitos em silêncio**. Versões cedo são **seguras** (rejeitam o que não provam); ficam mais permissivas conforme a spine amadurece. É o honest-stop do projeto aplicado ao checker de memória: *reject-what-you-can't-prove*.

---

## 10. Casos abertos / decisões pendentes

1. **RESOLVIDO (dono 2026-07-13):** Spelling & escopo Marshall → especificado em `docs/design/marshall-spec.md` (PR #539, design-ahead); referência cruzada aqui (§5), não duplicação.
2. **RESOLVIDO (dono 2026-07-13):** Os "casos específicos" de `let → mut` = **cópias sim, aliases não** (A1, ruling 2).
3. **RESOLVIDO (dono 2026-07-13):** Sem borrow imutável (`&T`) — **FECHADO DURO, unsafe-only `&`** (ruling 6); a matriz 2×2 é final (PGO-softening DROPPED). Recomendação: aceitar o custo de cópia de arena (barato, caso raro).
4. **Soundness do auto-deref dirigido por tipo (§4)** — regra nova; rodar um mini soundness-pass dedicado antes de cravar (garantir que o descascamento type-directed não reabre nenhum buraco fechado). *(ABERTO)*
5. **RESOLVIDO (dono 2026-07-13):** `Ref` como campo de struct — coberto por A1 (transitividade) + A5 (path) + regra de modificador obrigatório (ruling 3); escape conservador até a spine provar casos.
6. **RESOLVIDO (dono 2026-07-13):** Método mutante num `let a` — **exceção de classe via R1** (ruling 1); métodos de struct que escrevem `this.field` barrados em `let` (checker infere).

---

## 11. Migração & relação com #497

- **`-> void` (#497):** some a annotation de superfície; o **tipo Void interno fica**. 57 sites .tks + 2 .tkt migram. `fn` sem `-> T` = retorna nada.
- **Gotcha stable-seed:** remover keyword / trocar `Ref<T>` por `ref` na superfície exige que o seed estável parseie a forma antiga até src/ migrar; ordem: migrar src/ → remover a aceitação antiga. Ou deprecated-alias na transição.
- **`escape.tks`:** o checker one-depth atual é substrato; A1–A6 o reescrevem para field-sensitive/transitivo/interprocedural (a *spine*).

---

*Fonte da verificação: workflow `ref-model-soundness` (45 agentes). Discussão completa: #498.*
