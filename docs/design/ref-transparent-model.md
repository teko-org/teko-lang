# Modelo de referências transparentes — `Ref<T>` / `ref` (design 0.3.1)

> **Status:** design ratificado pelo dono (2026-07-11), verificado adversarialmente. **NÃO implementado.** O `Ref<T>` atual do compilador é `.value`-based; este doc especifica o redesign. Issue-mãe: **#498**. Companheiro: **#497** (remoção de `-> void`).

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
- `mut x: Ref<T>` — o único que aliasa; write-through.

A célula proibida (`let x: Ref<T>`) diz: **não existe referência imutável.** Leitura imutável ⟹ cópia (`let x: T`). Consequência deliberada: **referência ⟹ sempre canal de escrita; imutabilidade ⟹ sempre posse de valor.**

---

## 3. Sintaxe: o modificador `ref`

`ref` é o **3º modificador de binding**, açúcar para `mut x: Ref<T>`. A distinção valor-vs-referência só importa no binding (nos use-sites a transparência iguala tudo), então mora ali; o tipo anotado fica limpo.

| binding      | é                   | tipo subjacente |
|--------------|---------------------|-----------------|
| `let x: T`   | valor imutável      | `T`             |
| `mut x: T`   | valor mutável       | `T`             |
| `ref x: T`   | referência mutável  | `Ref<T>`        |

- `ref` é **inerentemente** alias-mutável — não há `let ref`/`mut ref`. A regra "sem referência imutável" cai estruturalmente.
- Param-borrow: `fn accumulate(ref acc: []int, x: int)`.
- **O tipo `Ref<T>` NÃO desaparece** — vive em toda posição-de-tipo que um modificador de binding não alcança: campos de struct (`r: Ref<T>`), args genéricos (`[]Ref<T>`, `Map<K, Ref<V>>`), retornos (`-> ref T`), e o aninhamento.

### Aninhamento — `Ref<Ref<T>>` / `**T`
O modificador `ref` dá o nível **mais externo**; o tipo `Ref<T>` dá os internos:

```
ref x: T            → Ref<T>            (*T)
ref x: Ref<T>       → Ref<Ref<T>>       (**T)
ref x: Ref<Ref<T>>  → Ref<Ref<Ref<T>>>  (***T)
```

Cada `Ref<>` além do primeiro vai no tipo. Aninhamento profundo é raro e **verboso, sem açúcar** — é o preço a pagar.

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

---

## 5. Marshall & a fronteira `Ref`/`Ptr`

- **Marshall** é a fronteira explícita do **`ptr` cru** (FFI, `Ptr<T>`), nos **dois sentidos**: `marshall(p)` embrulha um ptr num `ref`; `marshall(x)` extrai o ptr de um `ref`. É o único cruzamento `unsafe`. *(Spelling TBD — ver casos abertos.)*
- **Correção (dono 2026-07-11):** Marshall **NÃO** cobre o `valor ↔ Ref` — esse é o **desugar implícito** (§4.1, encapsular/descascar). A cópia `Ref→let` (R8) é a direção *descascar* do desugar, **não** Marshall.
- **`Ptr<T>` é UNSAFE-only.** `Ptr → Ref` pode **panicar** (tipo incompatível, estouro de memória; checado). Até FFI, se não for `unsafe`, usa **`Ref`**. Relação: `Ref` = seguro/transparente/default; `Ptr` = cru/explícito/`unsafe`, via Marshall.

---

## 6. Semântica (as regras base R1–R11)

- **R1. Dois eixos ortogonais.** `mut` = escrita; `Ref<T>` (no tipo) = aliasing. `mut x: T` é valor gravável; `ref x: T` (`mut x: Ref<T>`) é alias gravável. Tipos diferentes.
- **R2. Transparente.** `Ref<T>` age como `T` (auto-deref dirigido por tipo, §4). Ponteiro cru só via Marshall.
- **R3. Exige mut.** `let x: Ref<T>` é ilegal (referência imutável = contradição). `ref` já é inerentemente mut.
- **R4. Atribuição = write-through, nunca rebind.** `r = v` escreve `v` **através** de `r` no `T` aliasado; o ponteiro não muda. Rebind não se faz por `=` (default: binding fixo, estilo C++).
- **R5. Copy-on-attach.** Anexar um valor **não-Ref** a um destino `Ref<>` materializa uma **cópia** na arena do destino. Ponteiro nunca acontece implícito.
- **R6. Retorno materializa na arena do caller.** Retornar um valor local como `ref T` copia-o pra arena do caller (R5) → seguro.
- **R7. Borrow-down aliasa.** Passar uma variável `mut` viva a um param `ref` aliasa o storage do caller (write-through muta o do caller). Seguro porque o storage do caller sobrevive à chamada. *(Restringido por A2/A5.)*
- **R8. Ref→let = desugar-descasca-copia** (§4.1, direção *descascar* — NÃO Marshall). `let y = someRef` copia o valor; `y` é `T` (cópia). O `let` é firewall de imutabilidade + de escape.
- **R9. "Referenciável" é opt-in.** `mut x: T` é mutável mas não referenciável-pra-cima; um `Ref` construído sobre ele (fora do borrow-down R7) é **sobre cópia** (R5). Ponteiro genuíno que escapa exige `ref`/`Ref<>` explícito → controle manual.
- **R10. Encapsulamento.** `a.field = v` num `let a` **panica** — escrita direta externa de campo exige binding `mut`, mesmo com campo mutável. O objeto muta os próprios campos via seus métodos.
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
- **A4 — `mut x: Ref<T>` / `ref x: T` EXIGE init na declaração** (definite-assignment, estilo C++). R3 só barra `let Ref`; um `ref` sem init + R4 (write-through never rebind) = deref de lixo no 1º `=`. Sem binding `ref` não-inicializado. *(Fecha: uninit-Ref.)*
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

1. **Spelling do Marshall** — keyword `marshall`? `teko::marshall(r)`? `r as ptr<T>`? E escopo (só `unsafe`/FFI, ou geral pra a cópia-pra-let?).
2. **Os "casos específicos" de `let → mut`** — o dono deixou pra discutir; hoje `let ↛ mut`.
3. **Rebind de `ref`** — default recomendado: **binding fixo** (C++-style, sem rebind por `=`); rebind cru só via `Ptr`/Marshall. Confirmar.
4. **Sem borrow imutável (`&T`)** — passar objeto grande `let`-possuído só-pra-ler força cópia (a matriz é 2×2, não 2×3). Recomendação: **aceitar o custo** (cópia de arena barata, caso raro) e reavaliar via PGO; introduzir um view read-only só se o profiler apontar cópias grandes quentes.
5. **Soundness do auto-deref dirigido por tipo (§4)** — regra nova; rodar um mini soundness-pass dedicado antes de cravar (garantir que o descascamento type-directed não reabre nenhum buraco fechado).
6. **`Ref` como campo de struct** — coberto por A1 (transitividade) + A5 (path); confirmar a interação com tipos adotados/DI-managed.
7. **Método mutante num `let a`** — recomendação: exige `mut` também (senão `let` não é firewall real). Confirmar (R10 hoje fala só de escrita direta de campo).

---

## 11. Migração & relação com #497

- **`-> void` (#497):** some a annotation de superfície; o **tipo Void interno fica**. 57 sites .tks + 2 .tkt migram. `fn` sem `-> T` = retorna nada.
- **Gotcha stable-seed:** remover keyword / trocar `Ref<T>` por `ref` na superfície exige que o seed estável parseie a forma antiga até src/ migrar; ordem: migrar src/ → remover a aceitação antiga. Ou deprecated-alias na transição.
- **`escape.tks`:** o checker one-depth atual é substrato; A1–A6 o reescrevem para field-sensitive/transitivo/interprocedural (a *spine*).

---

*Fonte da verificação: workflow `ref-model-soundness` (45 agentes). Discussão completa: #498.*
