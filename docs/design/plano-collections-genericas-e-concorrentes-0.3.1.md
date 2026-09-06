# Plano — expansão de `teko::collections`: coleções **genéricas** + **concorrentes** (estilo C#)

> **Versão:** v2 (2026-08-14) — §3 (concorrentes) REESCRITO por ruling do dono (2026-08-13): modelo B
> (shared-memory sob lock/interlocked), SUPERSEDE o plano chan-actor da v1. §1–§2 (genéricas) intactos.
> **Status:** DESIGN-AHEAD (architect). Read-only no código-produto —
> NENHUM `.tks` de produto editado, NENHUM build, NENHUM reseed, NENHUM `teko test .` (fuga de
> memória do `monomorph` — crasha o container; nunca correr). Este documento É o artefacto; o único
> commit desta carga é ele próprio.
> **Branch:** `fix/retirement` (drena sequencial, SEM PRs). Worktree isolado off `origin/fix/retirement`
> (a checkout principal pode ter um implementer de reseed + outros design agents — não se toca).
> **Norte do dono:** *"Collections: generic e concurrency collections, como em C#."* Duas famílias:
> genéricas (≈ `System.Collections.Generic`) e concorrentes (≈ `System.Collections.Concurrent`).
> **Fontes de lei (SELADAS — desenha-se à volta, não se re-abrem):**
> - Coleções-como-classe + a régua `Map<K: Hashable & Eq, V>` → hoje `str`-keyed: `src/collections/map.tks`
>   linhas 8-18 (a ruling que fez o Map desistir do `K` genérico) e DECISION_LOG.md D18 (:161).
> - **9-ops** (interface-que-obriga-operador `IEq`/`IOrd` + contrapartida): plano pronto
>   `docs/design/plano-9ops-interface-operador-e-overload-composicao-0.3.1.md` (`61cf6bf7`), o **próximo
>   reseed**. Entrega `IEq`/`IOrd` + dispatch de operador sobre `T` constrangido (crumb 4, §3 daquele
>   doc). **É a dependência-chave das genéricas com chave/ordem.**
> - **#254** (métodos-em-genérico + static factories `T::make()`): DONE
>   (`docs/design/drain-254-L4L5-class-factories.md`, `[HISTÓRICO]`). É a base viva que `List`/`Map` já usam.
> - **Modelo de concorrência §10** (ISOLAMENTO por omissão: memória isolada + `chan<T>` MPSC +
>   `spawn`/`join`; partilha-sob-lock quando EXPLICITAMENTE declarada — o padrão `Arc<Mutex<T>>`, §3.2):
>   `docs/design/concorrencia-isolate-spawn-chan-0.3.1.md` (SELADO) e
>   `docs/design/concorrencia-adiantada-s8.md`. Dependência-chave das concorrentes: a fundação F1
>   (thread por tarefa) + F2 (região partilhada, já semeada em `tk_region_program`).
> **Lei permanente:** Teko-only (`.tks`), W15 + Javadoc-completo em TODA declaração, law-first, reseed
> disciplinado (`cc -std=c2x`, `--no-verify`), fixpoint byte-idêntico.

---

## 1. RECON — o que JÁ existe (file:line, verificado no worktree off `origin/fix/retirement`)

### 1.1 A superfície `teko::collections` atual — o que reusar, NÃO recriar
- **`src/collections/collections.tks`** — o unit-raiz: **combinadores de array puros e genéricos** que
  `List`/`Map` dobram por cima. `arr_replace_at<T>` (`:29`), `arr_drop_at<T>` (`:49`),
  `arr_drop_last<T>` (`:67`). São **free functions** deliberadamente (não métodos privados): um método
  de instância genérico que chama um SIBLING genérico em `self` pode falhar a ligar nativamente (o
  mono/codegen não estampa o sibling) — `collections.tks:9-15` documenta a regra. **Toda a expansão
  reusa este cofre e ESTENDE-O com os shapes que faltam (`arr_insert_at`, `arr_swap`, `arr_reverse`,
  `arr_slice`), pela mesma forma free-function.**
- **`src/collections/list.tks`** — `pub type List<T> = class` (`:17`): `make()` static factory (`:26`,
  a via #254 L5), `len`/`is_empty`/`push`/`get`/`set`/`pop`/`remove_at`/`to_array`. Backing store é um
  `[]T` que a classe POSSUI e reconstrói (Teko arrays são snapshots de valor; cada mutação reatribui o
  campo `items`). Semântica de REFERÊNCIA (é `class`, não `struct`) — aliases veem a mutação.
- **`src/collections/map.tks`** — `pub type Map<V> = class` (`:30`): `str`-keyed (NÃO genérico em `K`),
  três arrays paralelos (`keys`/`hashes`/`vals`), hash FNV-1a cacheado, `map_find_index` free-function
  (`:143`). **A ruling `map.tks:8-18` explica PORQUÊ `str` e não `K`:** na pilha genérica atual um átomo
  de trait estrutural (`Hashable`/`Eq`) é OPACO num type-param — um corpo genérico não podia chamar
  `k.hash()`/`k.eq()` nem `==` sobre `K`. **É exatamente essa barreira que o 9-ops (`IEq`) + a `IHash`
  nova (§1.3) removem.** `Map<V>` continua a valer como o caso `str`-keyed já provado.
- **`src/collections/{list,map}_test.tkt`** — os `.tkt` que provam as duas instâncias por largura
  (`List<i64>`/`List<str>`, `Map<i64>`/`Map<str>`). **A expansão herda o padrão two-instantiation.**
- **`src/list/list.tks`** — `pub fn grow<T>(ref x: []T, v: T)` (`:17`): o par mutável-por-`ref` do
  `teko::list::push` value-thread. `teko::list::push`/`empty` são **builtins de checker**
  (`type_list_builtin`) — a base de array de tudo. Não é a mesma camada que `collections`: `list` é o
  primitivo de array, `collections` são as classes growable.

### 1.2 Peças transversais reusáveis
- **`src/runtime/teko_rt.tks:529` — `exp fn str_hash(s: str): u64`** (FNV-1a, gêmeo Teko do
  `tk_str_hash`). É o hash que `Map` já usa; **é o corpo-de-referência do `IHash` para `str`** (§2.3).
- **`src/runtime/teko_rt.tks:536` — `str_cmp(a, b): i32`** (lexicográfico) — o corpo-de-referência do
  `IOrd` para `str`.
- **`src/sort/sort.tks`** — `sort_str`/`sort_i64` CONCRETOS (`:81`/`:157`), NÃO `sort<T: IOrd>`. O
  9-ops destrava o genérico; é ADJACENTE (reportado, não construído aqui).
- **`src/sort/cmp.tks`** — `cmp(a,b): i32`/`cmp_natural` (`:141`/`:167`), comparadores `str` concretos.
- **`src/iter/iter.tks:28` — `pub type Iterator<T> = interface { fn next(): T | null }`**. **Já existe
  o contrato de iteração genérico** — as novas coleções expõem `iter(): Iterator<T>` conformando-o (a
  porta que hoje é `to_array()`).

### 1.3 As interfaces de CAPACIDADE que faltam (o gate de tudo)
| capacidade | o que é | quem entrega | estado |
|---|---|---|---|
| **`IEq`** | igualdade por `operator __eq`/`__ne` (contrapartida obrigatória) | **9-ops** (crumb 6, snippet SELADO no §4 daquele doc) | plano pronto, próximo reseed |
| **`IOrd`** | ordem por `__lt`/`__gt`/`__le`/`__ge` (reflexão obrigatória) | **9-ops** (crumb 6) | plano pronto, próximo reseed |
| **`IHash`** | hash por **método** `fn hash(): u64` — **NÃO é operador** | **ESTE doc o desenha** (§2.3) | ausente; é a peça nova aqui |
| dispatch de `==`/`<` sobre `T` constrangido | `constraint_op_owner` + reuso da via de método-sobre-`T` de #254 | **9-ops** (crumb 4, RITUAL) | plano pronto |

**Conclusão do RECON:** `List`/`Map`/os combinadores existem e são a fundação. O que falta para as
GENÉRICAS é (i) `IHash` (interface de método, desenhada aqui), (ii) o dispatch genérico de operador
`IEq`/`IOrd` (que o 9-ops entrega), (iii) os tipos-coleção novos por cima dos combinadores. O que
falta para as CONCORRENTES é a fundação F1+F2 do §10 sob o modelo B (shared-memory sob lock/interlocked,
ruling do dono), reconciliado com o §10 pelo padrão `Arc<Mutex<T>>` — o desenho do §3.

---

## 2. Coleções GENÉRICAS (≈ `System.Collections.Generic`)

### 2.1 Nota de convenção — `pub` vs `exp`, e a régua de constraint
O dono pediu "superfície `exp` por tipo". Precisão law-first: em Teko puro `exp` é o marcador de
**export C-ABI** (`.tkh`, `kill-c-pull-forward-0.3.0.30.md` KP10; `teko::runtime::str_hash` é `exp`
porque tem gêmeo C). Uma classe stdlib PURA-Teko consumida cross-module usa **`pub type`** — é como
`List<T>`/`Map<V>` já estão escritos e como `teko::env` já consome `Map` cross-module. **Portanto a
superfície abaixo é `pub type`/`pub fn`** (o "exp" do dono = "a superfície exposta"; o token real é
`pub` para pure-Teko). Se e quando alguma coleção ganhar gêmeo C-ABI, a declaração migra para `exp` —
não hoje.

**A régua de constraint (§9.4, via 9-ops):** constraint = **interface-only, sem exceção** (mixins como
`NeByEq`/`GtByLt` são traits, ACHATAM o corpo mas NÃO constrangem). Logo:
- ordenação → `T: IOrd`; igualdade → `T: IEq`; chave-de-hash → `K: IEq & IHash`.
- um valor SEM capacidade requerida (`V` num `List<V>`) fica **sem constraint** — `List` não compara
  nem ordena `V`, só o carrega.

### 2.2 A `IHash` — a interface de MÉTODO nova (desenhada AQUI; fora do âmbito operador do 9-ops)
Hash NÃO é operador — não há token `operator hash`. `IHash` é uma interface de **método** comum,
parseável e despachável HOJE pela via de método-sobre-`T` de #254 (não precisa da camada
interface-operador). É a metade que o 9-ops NÃO entrega e que `Dictionary`/`HashSet` exigem além de
`IEq`. Corpus novo (módulo `src/cmp/` ou `src/core.tks` — o implementer segue a árvore de namespaces
autoritativa; alinhar com onde o 9-ops porá `IEq`/`IOrd`, para as três capacidades coabitarem):

```teko
/**
 * IHash — the hashing capability (a METHOD interface, not an operator): a type conforms by writing
 * `fn hash(): u64`. Distinct from `IEq`/`IOrd` (which the 9-ops interface-operator layer entrusts to
 * `operator __eq`/`__lt`) — hashing has no operator token, so `IHash` dispatches through the ordinary
 * generic-method-on-`T` path (#254, typer.tks:2197). A hash-keyed collection constrains its key on
 * `K: IEq & IHash`: `IHash` places a key in a bucket, `IEq` disambiguates a collision. The contract:
 * two keys that compare `__eq`-equal MUST return the same `hash()` (the hash-eq consistency law);
 * violating it silently corrupts lookup, so a conformer's `hash` and `__eq` must read the same fields.
 *
 * @return a 64-bit hash of the receiver, stable within one process run
 * @see IEq (the equality half a hash-keyed collection also requires)
 * @since 0.3.1
 */
type IHash = interface {
    fn hash(): u64
}
```

Adaptadores de capacidade para os primitivos-chave já com corpo de referência na árvore (o implementer
escolhe entre newtype-wrapper ou, se o 9-ops permitir capacidade em value-type prim-backed, escrita
direta — ver T-1 do doc 9-ops):

```teko
/**
 * StrKey — the `str` hash/eq capability wrapper, so `Dictionary<StrKey, V>` reuses the exact FNV-1a
 * (`teko::runtime::str_hash`) and byte-equality (`tk_str_eq`) that the shipped `Map<V>` already proves
 * correct. A conformer of `IEq & IHash` over a `str` payload; the identity map-key of today's `Map`.
 *
 * @see teko::collections::Map (the str-keyed map this generalises)
 * @since 0.3.1
 */
type StrKey = struct IEq & IHash & NeByEq {
    /** The wrapped key bytes — hashed and compared to place and disambiguate the entry. */
    key: str

    /**
     * FNV-1a of the wrapped bytes — the same hash `Map<V>` caches, so a `Dictionary<StrKey, V>` buckets
     * identically to the shipped `str`-keyed map.
     *
     * @return the FNV-1a hash of `self.key`
     */
    fn hash(): u64 { teko::runtime::str_hash(self.key) }

    /**
     * Byte-exact equality of the wrapped keys — the collision-disambiguating compare.
     *
     * @param left   the left key
     * @param right  the right key
     * @return       `true` iff the wrapped byte strings are equal
     */
    operator __eq(left: self, right: self): bool { left.key == right.key }
}
```

### 2.3 A superfície por tipo — constraints certos, gated

Cada tipo é uma `class` (semântica de referência, mutação através do receiver — a ruling de coleções
`map.tks:1-6`). Todos reusam os combinadores de `collections.tks` (estendidos). A coluna **GATE** diz
o que precisa aterrar; **[254✓]** = já disponível.

| tipo | constraint | GATE | fundação de reuso |
|---|---|---|---|
| `List<T>` | — (nenhuma) | **[254✓] existe** | `arr_*` combinadores |
| `Deque<T>` | — | **[254✓]** | `arr_insert_at`/`arr_drop_at` nas duas pontas |
| `Queue<T>` | — | **[254✓]** | `List<T>` interno (enqueue=push, dequeue=drop-front) |
| `Stack<T>` | — | **[254✓]** | `List<T>` interno (push/pop no fim) |
| `LinkedList<T>` | — | **[254✓]** | nós-por-índice sobre arrays paralelos (ver nota §2.4) |
| `HashSet<T>` | `T: IEq & IHash` | **9-ops (IEq) + IHash** | parallel `hashes`/`items`, molde `map_find_index` |
| `Dictionary<K,V>` | `K: IEq & IHash` | **9-ops (IEq) + IHash** | generaliza `Map<V>` (troca `[]str keys` por `[]K`) |
| `SortedSet<T>` | `T: IOrd` | **9-ops (IOrd)** | inserção ordenada por busca binária sobre `<` |
| `SortedDictionary<K,V>` | `K: IOrd` | **9-ops (IOrd)** | chaves ordenadas paralelas |
| `PriorityQueue<T>` | `T: IOrd` (ou `P: IOrd` separado) | **9-ops (IOrd)** | binary-heap sobre `[]T` |
| `ReadOnlyList<T>` | — | **[254✓]** | wrapper que segura um `List<T>` e omite mutadores |
| `ReadOnlyDictionary<K,V>` | `K: IEq & IHash` | 9-ops + IHash | wrapper sobre `Dictionary` |

**Assinaturas-chave (shapes que o implementer copia, full-Javadoc — amostra representativa; as demais
seguem o molde `List`/`Map`):**

```teko
/**
 * Dictionary<K, V> — a growable, reference-semantic hash map generalising the shipped str-keyed
 * `Map<V>` (issue #163) to an ARBITRARY key type. `K` is constrained `IEq & IHash`: `IHash.hash()`
 * buckets a key (compared cheaply as a `u64` before the full key), `IEq.__eq` disambiguates a
 * collision. Three parallel owned arrays (`keys`/`hashes`/`vals`) re-grown on each mutation — the same
 * representation `Map<V>` proved (a nested `Entry<K,V>` still fails to resolve on the generic stack,
 * map.tks:20-25). Reference semantics: every alias observes a mutation.
 *
 * GATE: requires 9-ops (generic `==` dispatch over `K: IEq`, crumb 4) AND `IHash` (§2.2). Until both
 * land, `Map<V>` is the shipped str-keyed stand-in; `Dictionary<StrKey, V>` is its drop-in successor.
 *
 * @see teko::collections::Map (the str-keyed map this generalises)
 * @since 0.3.1
 */
pub type Dictionary<K: IEq & IHash, V> = class {
    /** The keys, insertion order — parallel to `hashes` and `vals`. */
    intern keys: []K
    /** Each key's cached `hash()`, compared before the full key for cheap collision rejection. */
    intern hashes: []u64
    /** The values, parallel to `keys`. */
    intern vals: []V

    /**
     * Build an empty `Dictionary<K, V>` — the generic static factory.
     *
     * @return a fresh, empty dictionary at the caller's concrete `K`/`V`
     */
    pub static fn make(): Dictionary<K, V> {
        .{ keys = teko::list::empty(); hashes = teko::list::empty(); vals = teko::list::empty() }
    }

    /**
     * Insert or update the value for `k`. An existing key is overwritten in place (count unchanged);
     * otherwise a new entry is appended. Reference semantics: every alias observes the change.
     *
     * @param k  the key to insert or update
     * @param v  the value to associate with `k`
     */
    pub fn insert(k: K, v: V) {
        var h = k.hash()
        var at = dict_find_index<K>(self.keys, self.hashes, h, k)
        if at < self.keys.len { self.vals = arr_replace_at(self.vals, at, v); return }
        self.keys = teko::list::push(self.keys, k)
        self.hashes = teko::list::push(self.hashes, h)
        self.vals = teko::list::push(self.vals, v)
    }

    /**
     * The value associated with `k`, or `null` when absent — the null-propagating lookup (`V?`, the
     * disjoint-domain null path per the OOP ruling, never an `error` union).
     *
     * @param k  the key to look up
     * @return   the associated value, or `null` if `k` is not present
     */
    pub fn get(k: K): V | null {
        var at = dict_find_index<K>(self.keys, self.hashes, k.hash(), k)
        if at >= self.keys.len { return null }
        self.vals[at]
    }
}

/**
 * Locate key `k` (whose precomputed hash is `h`) in the parallel `keys`/`hashes` arrays, or `keys.len`
 * when absent — the generalisation of `map_find_index` (map.tks:143) from `str` to any `K: IEq`. The
 * `u64` hash rejects a non-match without an `__eq` call; an equal hash falls through to `__eq` so
 * collisions stay correct. A FREE generic function, not a private method, for the sibling-link reason
 * that governs `map_find_index` (collections.tks:9-15).
 *
 * @param keys    the key array
 * @param hashes  the parallel cached-hash array
 * @param h       the precomputed hash of the sought key
 * @param k       the sought key
 * @return        the index of `k`, or `keys.len` if absent
 */
fn dict_find_index<K: IEq & IHash>(keys: []K, hashes: []u64, h: u64, k: K): u64 {
    var i: u64 = 0
    loop {
        if i >= keys.len { break }
        if hashes[i] == h && keys[i] == k { return i }
        i++
    }
    keys.len
}

/**
 * SortedSet<T> — a growable set kept in ascending `T` order (`T: IOrd`), so iteration is sorted and
 * membership is an O(log n) binary search over `<`. Reference-semantic class; the ordered dual of
 * `HashSet<T>` (which needs `IEq & IHash` instead and gives O(1) unordered membership).
 *
 * GATE: requires 9-ops generic `<` dispatch over `T: IOrd` (crumb 4).
 *
 * @since 0.3.1
 */
pub type SortedSet<T: IOrd> = class {
    /** The elements, strictly ascending by `<` — binary-searchable. */
    intern items: []T

    /**
     * Build an empty `SortedSet<T>`.
     *
     * @return a fresh, empty sorted set at the caller's concrete `T`
     */
    pub static fn make(): SortedSet<T> { .{ items = teko::list::empty() } }

    /**
     * Insert `x` at its sorted position if absent; a present element (by `!(x < e) && !(e < x)`) is a
     * no-op. Reference semantics: every alias observes the insert.
     *
     * @param x  the element to insert
     * @return   `true` iff `x` was newly inserted (was absent)
     */
    pub fn add(x: T): bool { sorted_insert<T>(ref self.items, x) }
}
```

```teko
/**
 * PriorityQueue<T> — a min-first priority queue (`T: IOrd`) backed by a binary heap over an owned
 * `[]T`. `enqueue` sifts up, `dequeue` pops the minimum and sifts down — both O(log n). Reference
 * semantics. For a separate priority key, a `PriorityQueue<E, P: IOrd>` two-array shape is the additive
 * follow-up; the single-`T` form (element IS its own priority) ships first.
 *
 * GATE: requires 9-ops generic `<` dispatch over `T: IOrd` (crumb 4).
 *
 * @since 0.3.1
 */
pub type PriorityQueue<T: IOrd> = class {
    /** The binary-heap array — `heap[0]` is the current minimum. */
    intern heap: []T

    /**
     * Build an empty `PriorityQueue<T>`.
     *
     * @return a fresh, empty priority queue at the caller's concrete `T`
     */
    pub static fn make(): PriorityQueue<T> { .{ heap = teko::list::empty() } }

    /**
     * Insert `x`, restoring the heap property by sifting up.
     *
     * @param x  the element to enqueue
     */
    pub fn enqueue(x: T) { self.heap = heap_sift_up<T>(teko::list::push(self.heap, x)) }

    /**
     * Remove and return the minimum, or `null` when empty (the disjoint-domain null path).
     *
     * @return the former minimum, or `null` if the queue was empty
     */
    pub fn dequeue(): T | null { heap_pop_min<T>(ref self.heap) }
}
```

### 2.4 Combinadores novos a acrescentar em `collections.tks` (free generic, molde existente)
`arr_insert_at<T>(xs, at, v): []T`, `arr_swap<T>(xs, i, j): []T`, `arr_reverse<T>(xs): []T`,
`arr_slice<T>(xs, from, to): []T` — todos value-functional (Teko arrays são snapshots), todos free
functions pela regra sibling-link (`collections.tks:9-15`). `sorted_insert<T: IOrd>(ref items, x)`,
`heap_sift_up<T: IOrd>`, `heap_pop_min<T: IOrd>` são os helpers de busca-binária/heap — GATED em 9-ops
(usam `<` sobre `T`). **`LinkedList<T>` nota:** um linked-list de nós-por-ponteiro exige um tipo-nó
auto-referente que a pilha genérica ainda estampa mal (`map.tks:20-25`); a forma segura HOJE é
**arrays paralelos `next`/`prev`/`items` com índices** (free-list), que dá as mesmas garantias O(1)
de splice sem o nó genérico auto-referente. Documentar o limite; a forma por-ponteiro é ADJACENTE.

### 2.5 Read-only wrappers
`ReadOnlyList<T>`/`ReadOnlyDictionary<K,V>` seguram uma referência à coleção mutável e expõem só os
leitores (`get`/`len`/`contains`/`iter`). NÃO copiam (a coleção subjacente pode mutar; a view é uma
janela, como C# `IReadOnlyList`). Sem gate próprio além do da coleção envolvida. Um `to_readonly()` na
coleção mutável devolve o wrapper.

---

## 3. Coleções CONCORRENTES — o modelo B (shared-memory sob lock/interlocked), sancionado pelo dono

> **Esta secção SUPERSEDE o plano chan-actor anterior** (Opções A/C por `chan`), por ruling do dono
> (2026-08-13). O que muda é o MODELO de memória concorrente, não as coleções genéricas do §1–§2 (que
> ficam intactas). A ruling e a sua reconciliação com o §10 são a lei desta secção.

### 3.1 A ruling do dono — B (shared-memory + lock/interlocked), não o actor-via-`chan`
O dono **recusou** as opções baseadas em `chan` que a versão anterior deste doc recomendava — Opção A
(actor: a coleção vive numa tarefa dona, acesso por comando/resposta em `chan`) e Opção C
(`BlockingCollection` como wrapper fino sobre `chan<T>`) — com o veredicto: *"arquitetura demais para
algo simples"*. O modelo escolhido é **Opção B: coleções concorrentes de MEMÓRIA PARTILHADA protegidas
por lock / interlocked (CAS lock-free)**, textual: *"(B) é simples e metal, como a maioria das
linguagens, foco em velocidade e economia de memória."*

**Precisão que a versão anterior errou:** ela rotulou o `System.Collections.Concurrent` do C# como
"lock" (`Monitor`). É impreciso. O C# usa **`Interlocked` (CAS lock-free) para o estado de palavra
única** (contadores, cabeças de pilha/fila, flags de estado) e **locks FINOS/segmentados** (o
*striping* por bucket do `ConcurrentDictionary`) só onde a estrutura obriga, com **esperas apoiadas em
futex** (`SemaphoreSlim`/`Monitor` degradam para futex no Linux, `WaitOnAddress` no Windows) — **NÃO**
um mutex global por objeto. Capturar isto com precisão É o pedido do dono ("velocidade e economia"):
lock-free onde der, lock fino onde não der, nunca um lock global.

### 3.2 A reconciliação com o §10 — o padrão `Arc<Mutex<T>>`, NÃO uma violação de lei
A versão anterior enquadrava B como *"viola o §10"*. **Sob a ruling, esse enquadramento está errado, e
corrige-se law-first.** O §10 (`concorrencia-isolate-spawn-chan-0.3.1.md`) sela o ISOLAMENTO como o
**default**: uma tarefa nasce com raiz de heap própria — *"como se fosse outro programa"*
(`codegen.tks:8828`, a ruling de fronteira de isolate do dono, 2026-07-27), e dados cruzam a fronteira
por cópia via `chan<T>` (§5) ou por retorno de `join` (§4.1). Uma coleção concorrente de memória
partilhada é o **`Arc<Mutex<T>>`**: a fuga EXPLÍCITA, marcada e opt-in, para partilha-mutável-sob-lock —
exatamente o que Rust (`Arc<Mutex<T>>`), C# (`System.Collections.Concurrent`), Java
(`java.util.concurrent`) e C++ (`std::mutex`) TODOS oferecem por cima de um default de isolamento.

**O §10 passa a ler-se "isolado por omissão, partilhado-sob-lock quando EXPLICITAMENTE declarado", não
"isolado sempre".** Uma coleção concorrente a partilhar memória entre tarefas **não dissolve** o §10 —
é uma **exceção sancionada** dele, pela MESMA lógica que já abençoa `chan`: estado partilhado gerido
pelo runtime, atrás de uma API estreita, nunca heap-de-utilizador cru a atravessar a fronteira. O que o
§10 §8.1 mede como *"o pior, e é bloqueante"* é a partilha **acidental e não-marcada** de arena entre
tarefas (duas raias a fazer `arena_push`/`pop` na mesma pilha, sem fronteira) — B **não** é isso: é uma
região partilhada DELIBERADA (F2, §3.4), com sincronização real por baixo, marcada no tipo.

### 3.3 O que B exige do runtime C — a exceção sancionada (`teko_rt.{c,h}`)
A lei Teko-only NÃO cobre o runtime C (`src/runtime/teko_rt.{c,h}` é a exceção mantida). B precisa de
duas famílias de primitiva, e **o runtime já carrega o idioma de ambas** — isto é promoção, não
partida do zero:
- o idioma atómico C11 já está em produto: `tk_region_gen_next` usa `__atomic_add_fetch(...,
  __ATOMIC_RELAXED)` (`teko_rt.c:1570`) para carimbar gerações sem colisão entre tarefas;
- um `tk_spin_lock`/`tk_spin_unlock` test-and-set (`__atomic_exchange_n`/`__atomic_store_n`,
  `teko_rt.c:1588`/`:1595`) já guarda os singletons Categoria-B, **sem `<pthread.h>`**.

O que falta é promover isto a primitivas de bloqueio de verdade (apoiadas em futex, não spin puro) e
expor RMW atómico nomeado. Prototipos C a acrescentar (metal, sem dependência nova além do que o SO já
dá — `futex(2)`/`WaitOnAddress`):

```c
/* Interlocked (lock-free CAS) — o caminho de velocidade do owner, sobre o idioma __atomic já em
 * teko_rt.c:1570. u64-wide, o suficiente para cabeças de pilha/fila, contadores e flags de estado. */
uint64_t tk_atomic_load (const uint64_t *cell);
void     tk_atomic_store(uint64_t *cell, uint64_t v);
uint64_t tk_atomic_add  (uint64_t *cell, uint64_t delta);          /* fetch-add, devolve o ANTERIOR  */
bool     tk_atomic_cas  (uint64_t *cell, uint64_t expected, uint64_t desired); /* compare-and-swap */

/* Lock fino apoiado em futex — a promoção do tk_spin_lock (teko_rt.c:1588) para bloqueio real:
 * spin curto e depois park no futex, para que um lock de bucket contido não queime CPU. */
void tk_mutex_lock  (uint64_t *word);                              /* 0=livre, 1=tomado; futex-park */
void tk_mutex_unlock(uint64_t *word);                              /* liberta + futex-wake um esperante */
```

Os gêmeos Teko (`exp fn`, como `str_hash`/`str_cmp` em `teko_rt.tks:529`/`:536`) são a superfície que os
`.tks` de coleção chamam. **Nenhum é um `chan`; nenhum copia dados na fronteira — são os tijolos de
sincronização de memória partilhada que B exige e que o actor evitava pagando cópia por operação.**

### 3.4 O assento de memória — a região do programa (F2), JÁ semeada no runtime
Uma coleção concorrente **não pode viver numa arena por-tarefa**: seria libertada quando a tarefa
criadora rebobina o seu `tk_arena_pop`, pendurando toda tarefa que ainda a partilha. Tem de morar numa
região **imortal, de nenhuma tarefa** — a região do programa (F2, §3.3 do doc §10). **Achado que muda o
cálculo de bloqueio:** essa região **já existe** — `tk_region_program()` (`teko_rt.c:2305`) constrói
*"uma região que NÃO é raiz de nenhuma tarefa, logo sobrevive tanto ao `tk_arena_pop` de uma tarefa
(que só rebobina a raiz DELA) quanto à saída da tarefa (que só liberta o registo DELA)"*, libertada no
fim do processo por `tk_regions_free_all` (logo continua leak-clean). É o assento exato da coleção
concorrente — o mesmo assento que o `chan` singleton já reclamava.

**O que ainda falta (o bloqueio real, idêntico ao da família `chan`):** o **modo de threads de SO
concorrentes (F1)** não aterrou — o comentário do próprio runtime crava que os atómicos/spinlock só se
armam onde uma corrida é *"realmente possível"* e que *"o modo de thread que precisa da coisa real (S8)
não aterrou"* (`teko_rt.c:1585`); sem GNU atomics degradam para no-op, correto para a realidade
single-threaded de hoje. Logo a família concorrente é **FASE-2, bloqueada em F1+F2** — F2 já semeada
(`tk_region_program`), F1 (raiz+thread por tarefa, `tk_task`/`tk_task_current`, §3.2 do doc §10) por
aterrar. É a MESMA dependência-chave que a família `chan` tem; B não acrescenta uma dependência nova de
fundação — só troca o transporte-por-cópia por sincronização-sobre-a-região-partilhada.

### 3.5 Postura de VELOCIDADE — lock-free onde a estrutura permite, striped onde não
A régua do "foco em velocidade" do dono, por forma de estrutura:

| estrutura | sincronização | porquê |
|---|---|---|
| contador / flag / estado de palavra única | **lock-free (`tk_atomic_add`/`tk_atomic_cas`)** | um CAS numa palavra; zero park no caminho quente |
| `ConcurrentStack<T>` | **CAS lock-free na cabeça** (Treiber) | push/pop = um CAS na cabeça sobre um nó por índice |
| `ConcurrentQueue<T>` | **CAS lock-free** (Michael-Scott) ou **dois locks** (head/tail) | produtor e consumidor não disputam a mesma palavra |
| `ConcurrentDictionary<K,V>` | **locks FINOS striped por bucket/segmento** | escritas em buckets distintos não se serializam; leitura pode ser lock-free |
| `ConcurrentBag<T>` | **buffer thread-local + roubo sob lock fino** | o caso sem-ordem: cada tarefa escreve no seu, rouba raramente |
| `BlockingCollection<T>` (bounded) | **mutex fino + condvar (futex) para bloqueio/contrapressão** | espera de "cheio"/"vazio" precisa de park, não de spin |

**Lei de desenho:** NUNCA um lock global por coleção. O `ConcurrentDictionary` estripa (um array de
palavras de lock, `hash(k) % n_stripes` escolhe qual) — é o coração da "velocidade" do C# e o que o
dono está a pedir por nome. O lock-free (Treiber/Michael-Scott) é preferido onde a estrutura é uma
cabeça de palavra única; o striped entra onde há N slots independentes.

### 3.6 Economia de MEMÓRIA — por que B bate o actor (pungente em pleno incidente de memória)
O outro eixo do dono ("economia de memória"). Contabilidade concreta de um `ConcurrentDictionary` sob N
tarefas produtoras:

- **Modelo B (escolhido):** UM objeto — os três arrays paralelos (`keys`/`hashes`/`vals`, §2) na região
  do programa — mais um array de palavras de lock (o striping, `n_stripes × 8 bytes`, tipicamente
  16–64 palavras). Fim. Cada operação muta em-lugar sob o lock do seu bucket; **zero cópia por
  operação, zero buffer intermédio.**
- **Modelo actor (recusado):** uma **tarefa dona** viva o tempo todo (a sua própria raiz de arena +
  pilha) segurando a coleção, MAIS um `chan<Cmd>` MPSC com um **buffer limitado** (cada slot uma cópia
  do comando + operandos), MAIS um `chan` de resposta **por cliente** (mais buffers), MAIS **uma cópia
  do comando na escrita e uma cópia da resposta na leitura** por CADA operação. Para N clientes a fazer
  M operações: O(N) canais + O(buffer) memória parada + O(M) cópias transitórias.

**B é uma ordem de grandeza mais magro em memória parada e não faz cópia por operação** — exatamente o
que se quer em pleno incidente de fuga de memória do `monomorph`. O custo que B paga em troca é
CPU-de-sincronização (o CAS/lock), não memória — e o dono pediu velocidade, que o lock-free/striped
entrega sem o imposto de memória do actor.

### 3.7 A superfície por tipo concorrente — todos Opção B agora
Vivem em `teko::collections::concurrent` (paralelo a `System.Collections.Concurrent`). A coluna GATE é
**F1+F2** para todos (§3.4) — o assento partilhado + o modo de thread.

| tipo C# | forma B em Teko | sincronização | fundação de reuso |
|---|---|---|---|
| `ConcurrentDictionary<K,V>` | dict com buckets striped | **locks finos striped** (§3.5) | os arrays paralelos de `Dictionary<K,V>` (§2.3), na região F2 |
| `ConcurrentQueue<T>` | fila lock-free | **CAS (Michael-Scott)** ou dois locks | nós por índice sobre `[]T` em F2 |
| `ConcurrentStack<T>` | pilha lock-free | **CAS na cabeça (Treiber)** | cabeça `u64` atómica + nós por índice |
| `ConcurrentBag<T>` | multiset sem ordem | **thread-local + roubo sob lock fino** | um `List<T>` por tarefa + índice de roubo |
| `BlockingCollection<T>` | fila limitada bloqueante | **mutex fino + condvar/futex** | `ConcurrentQueue` + contagem de capacidade |

```teko
/**
 * ConcurrentDictionary<K, V> — o `System.Collections.Concurrent.ConcurrentDictionary` do C#: UM mapa de
 * hash de memória PARTILHADA que N tarefas mutam ao mesmo tempo, seguro por STRIPING de locks finos
 * (nunca um lock global — §3.5). Generaliza `Dictionary<K, V>` (§2.3) para acesso concorrente: a mesma
 * representação de três arrays paralelos, mas ancorada na REGIÃO DO PROGRAMA (F2, `tk_region_program`,
 * teko_rt.c:2305) — imortal, de nenhuma tarefa — para que sobreviva ao `tk_arena_pop` da tarefa que a
 * criou. Cada bucket cai num de `n_stripes` locks por `hash(k) % n_stripes`: escritas em buckets
 * distintos NÃO se serializam. É o padrão `Arc<Mutex<T>>` sancionado (§3.2): a fuga explícita, marcada,
 * opt-in, para partilha-sob-lock — o default do §10 continua a ser ISOLAMENTO.
 *
 * GATE: FASE-2, bloqueado em F1 (thread por tarefa, §3.2 do doc §10) + F2 (região partilhada — já
 * semeada). Requer as primitivas `tk_mutex_lock`/`tk_atomic_*` do runtime (§3.3). Constrangimento de
 * chave idêntico ao `Dictionary`: `K: IEq & IHash` (§2.2, via 9-ops).
 *
 * @see teko::collections::Dictionary (o mapa sequencial que este torna concorrente)
 * @see concorrencia-isolate-spawn-chan-0.3.1.md §3.3 (a região F2 onde vive)
 * @since 0.3.1
 */
pub type ConcurrentDictionary<K: IEq & IHash, V> = class {
    /** O id da região-do-programa (F2) onde os arrays vivem — resolvido pelo runtime, nunca cacheado como ponteiro (§3.4 do doc §10). */
    intern region: u64
    /** As palavras de lock do striping — `stripes[hash(k) % stripes.len]` guarda o bucket de `k`. Um lock fino, nunca global. */
    intern stripes: []u64
    /** As chaves, paralelas a `hashes`/`vals`, na região F2. */
    intern keys: []K
    /** O hash cacheado de cada chave, comparado antes da chave completa (rejeição barata de colisão). */
    intern hashes: []u64
    /** Os valores, paralelos a `keys`. */
    intern vals: []V

    /**
     * Constrói um `ConcurrentDictionary<K, V>` vazio na região do programa (F2), com `n_stripes` locks.
     *
     * @param n_stripes  quantos locks finos repartir os buckets (potência de 2; mais = menos contenção, mais memória)
     * @return           um dicionário concorrente vazio, ancorado em F2
     */
    pub static fn make(n_stripes: u64): ConcurrentDictionary<K, V> {
        .{ region = teko::runtime::region_program(); stripes = stripes_make(n_stripes);
           keys = teko::list::empty(); hashes = teko::list::empty(); vals = teko::list::empty() }
    }

    /**
     * Insere ou atualiza `v` para `k`, tomando SÓ o lock do bucket de `k` (striping — §3.5): tarefas a
     * escrever chaves de buckets distintos não se serializam. Semântica de referência partilhada.
     *
     * @param k  a chave a inserir ou atualizar
     * @param v  o valor a associar a `k`
     */
    pub fn insert(k: K, v: V) {
        var s = k.hash() % self.stripes.len
        teko::threads::mutex_lock(ref self.stripes[s])
        defer teko::threads::mutex_unlock(ref self.stripes[s])
        var at = dict_find_index<K>(self.keys, self.hashes, k.hash(), k)
        if at < self.keys.len { self.vals = arr_replace_at(self.vals, at, v); return }
        self.keys = teko::list::push(self.keys, k)
        self.hashes = teko::list::push(self.hashes, k.hash())
        self.vals = teko::list::push(self.vals, v)
    }

    /**
     * O valor de `k`, ou `null` quando ausente — sob o lock do bucket de `k` (a leitura toma o mesmo
     * lock fino; uma variante lock-free de leitura é ADITIVA quando a representação estabilizar).
     *
     * @param k  a chave a procurar
     * @return   o valor associado, ou `null` se `k` não estiver presente
     */
    pub fn get(k: K): V | null {
        var s = k.hash() % self.stripes.len
        teko::threads::mutex_lock(ref self.stripes[s])
        defer teko::threads::mutex_unlock(ref self.stripes[s])
        var at = dict_find_index<K>(self.keys, self.hashes, k.hash(), k)
        if at >= self.keys.len { return null }
        self.vals[at]
    }
}
```

```teko
/**
 * ConcurrentStack<T> — pilha concorrente LOCK-FREE (a pilha de Treiber): push e pop são cada um UM
 * `tk_atomic_cas` (§3.3) na palavra da cabeça, sem lock nenhum — o caminho de velocidade puro do dono
 * (§3.5). Os nós vivem por índice num `[]T` na região do programa (F2); `head` é o índice do topo,
 * mutado só por CAS. Um `pop` que perde a corrida do CAS relê a cabeça e tenta de novo (o retry-loop
 * clássico). Zero cópia por operação, zero park — pura economia de memória (§3.6).
 *
 * GATE: FASE-2, F1+F2 (§3.4). Requer `tk_atomic_cas`/`tk_atomic_load` (§3.3). Sem constrangimento em
 * `T` (a pilha não compara nem ordena `T`, só o carrega).
 *
 * @since 0.3.1
 */
pub type ConcurrentStack<T> = class {
    /** O id da região-do-programa (F2) onde os nós vivem. */
    intern region: u64
    /** O índice do topo, mutado SÓ por `tk_atomic_cas` — a única palavra de sincronização (lock-free). */
    intern head: u64
    /** Os nós por índice; `next[i]` é o índice do nó abaixo de `i`. */
    intern next: []u64
    /** O payload paralelo a `next`. */
    intern vals: []T

    /**
     * Constrói um `ConcurrentStack<T>` vazio na região do programa (F2).
     *
     * @return uma pilha concorrente vazia, ancorada em F2
     */
    pub static fn make(): ConcurrentStack<T> {
        .{ region = teko::runtime::region_program(); head = 0; next = teko::list::empty(); vals = teko::list::empty() }
    }

    /**
     * Empilha `x` por CAS na cabeça (Treiber): monta o nó, aponta o seu `next` para a cabeça atual, e
     * faz `tk_atomic_cas` para publicar; repete se outra tarefa venceu a corrida.
     *
     * @param x  o elemento a empilhar
     */
    pub fn push(x: T) { concurrent_stack_push<T>(ref self.head, ref self.next, ref self.vals, x) }

    /**
     * Desempilha o topo por CAS, ou `null` quando vazia (o caminho null de domínio disjunto).
     *
     * @return o antigo topo, ou `null` se a pilha estava vazia
     */
    pub fn pop(): T | null { concurrent_stack_pop<T>(ref self.head, self.next, self.vals) }
}
```

### 3.8 A sub-família NOVA que o doc antigo perdeu — primitivas de COORDENAÇÃO (`teko::threads::sync`)
Distintas das coleções: **não guardam dados de utilizador, coordenam tarefas.** `WaitGroup`
(≈ `sync.WaitGroup` do Go / `CountdownEvent` do .NET), `Barrier`, `Semaphore`, `Latch`, e um contador
`Atomic<T>`. Desenham-se como **handles-por-id (como `ChanId`: um `u64` NOME, apoiado em futex), a expor
ops atómicas estreitas** (`add`/`done`/`wait`, `acquire`/`release`, …). São **§10-compatíveis pela
MESMA razão que já abençoa o `chan` e o `WaitGroup` do §7 do doc §10**: estado partilhado gerido pelo
runtime atrás de um handle estreito, resolvido por consulta ao registo de nomes (F3,
`teko_rt.c:2317`) a cada uso — **nunca heap-de-utilizador cru a atravessar a fronteira**, nunca um
ponteiro cacheado (§3.4 do doc §10). Vivem em `teko::threads::sync`, ao lado de `chan`/`spawn` do §10.

```teko
/**
 * WaitGroup — um contador de tarefas em voo (≈ `sync.WaitGroup` do Go / `CountdownEvent` do .NET), pela
 * MESMA disciplina de handle-por-id do §3.4 do doc §10: o handle carrega SÓ o id, todo predicado é uma
 * consulta ao registo (F3). `add`/`done` deixam a contagem crescer DEPOIS do lançamento (o caso que
 * `fork_join`, de contagem estática, não cobre — §7.2 do doc §10). Implementado sobre `tk_atomic_add`
 * (a contagem) + futex (o `wait` que bloqueia até zero) — §3.3, sem máquina de segurança de memória
 * nova. §10-compatível pela razão que abençoa `chan`: shared-state gerido pelo runtime atrás de um id.
 *
 * GATE: FASE-2, F1+F2 (§3.4). É a formalização do `wg_*` que o §7.2 do doc §10 deixou integrator-pinned.
 *
 * @see concorrencia-isolate-spawn-chan-0.3.1.md §7.2 (o WaitGroup por fechar que este fixa)
 * @since 0.3.1
 */
pub type WaitGroup = struct {
    /** O id no registo de nomes (F3) — resolvido a cada uso, nunca um ponteiro cacheado (§3.4 do doc §10). */
    id: u64

    /**
     * Soma `n` tarefas esperadas à contagem, por `tk_atomic_add` (§3.3).
     *
     * @param n  quantas tarefas a mais esperar
     * @return   `null` em sucesso, `error` se o grupo já disparou
     */
    pub fn add(n: u64): null | error { teko::threads::sync::wg_add(self.id, n) }

    /**
     * Assinala que uma tarefa terminou (decrementa por CAS); acorda os esperantes se chegar a zero.
     *
     * @return `null` em sucesso, `error` se a contagem já era zero (done a mais)
     */
    pub fn done(): null | error { teko::threads::sync::wg_done(self.id) }

    /**
     * Bloqueia (park no futex) até a contagem chegar a zero.
     *
     * @return `null` quando todas terminaram, `error` se o grupo foi destruído sob espera
     */
    pub fn wait(): null | error { teko::threads::sync::wg_wait(self.id) }
}
```

```teko
/**
 * Atomic<T> — um contador/célula de palavra única com RMW lock-free (≈ `Interlocked`/`std::atomic`),
 * a peça de "interlocked" que o dono nomeou. Handle-por-id (§3.4 do doc §10) sobre uma palavra na
 * região do programa (F2); toda a op é um `tk_atomic_*` (§3.3), nunca um lock. É o tijolo mais barato
 * da família concorrente — uma palavra, um CAS.
 *
 * GATE: FASE-2, F1+F2. `T` restrito hoje a um valor que caiba numa palavra (`u64`/`i64`/`bool`/enum);
 * a forma genérica larga é ADITIVA. Requer `tk_atomic_load/store/add/cas` (§3.3).
 *
 * @since 0.3.1
 */
pub type Atomic<T> = struct {
    /** O id da célula no registo (F3), resolvido a cada op. */
    id: u64

    /**
     * Lê o valor atual (leitura atómica).
     *
     * @return o valor corrente da célula
     */
    pub fn load(): T { teko::threads::sync::atomic_load<T>(self.id) }

    /**
     * Soma `delta` e devolve o valor ANTERIOR (fetch-add atómico).
     *
     * @param delta  a quantia a somar
     * @return       o valor antes da soma
     */
    pub fn fetch_add(delta: T): T { teko::threads::sync::atomic_add<T>(self.id, delta) }

    /**
     * Compare-and-swap: escreve `desired` só se o valor atual for `expected`.
     *
     * @param expected  o valor que se espera encontrar
     * @param desired   o valor a escrever se a expectativa bater
     * @return          `true` se o swap ocorreu (o valor era `expected`)
     */
    pub fn compare_and_swap(expected: T, desired: T): bool { teko::threads::sync::atomic_cas<T>(self.id, expected, desired) }
}
```

`Semaphore` (`acquire`/`release`, contagem sob `tk_atomic_add` + park no futex quando a zero),
`Barrier` (N tarefas esperam umas pelas outras num ponto; a última liberta todas), e `Latch` (uma
contagem regressiva de disparo único, ≈ `CountDownLatch` do Java) seguem o MESMO molde: `struct
{ id: u64 }`, ops atómicas estreitas, park no futex, ancoradas em F2, resolvidas por F3. Todas
FASE-2/F1+F2; todas §10-compatíveis pela razão do `chan`.

### 3.9 O que fica FASE-2 (após F1+F2) e o que se ADIANTA agora
Toda a família concorrente + coordenação é **BLOQUEADA em F1+F2** (§3.4): F2 já semeada
(`tk_region_program`, `teko_rt.c:2305`), F1 (thread+raiz por tarefa) por aterrar, e as primitivas
`tk_mutex_lock`/`tk_atomic_*` (§3.3) por acrescentar ao runtime. O que se **ADIANTA agora** (compila/
valida sem F1): os **tipos-handle** (`WaitGroup`/`Atomic<T>`/`Semaphore` como `struct { id: u64 }`,
value puros), os **doc-contratos** Javadoc de toda a família, e os **skeletons com honest-stop**
(`panic("blocked on F1+F2 shared-region + thread mode")`) para cada tipo concorrente. Quando F1
aterrar (e as 4+2 primitivas C entrarem no `teko_rt.{c,h}`), o implementer troca os honest-stops pelas
chamadas `mutex_lock`/`atomic_cas`/`atomic_add` em minutos — a representação (arrays paralelos em F2,
striping, cabeça atómica) já está desenhada aqui.

---

## 4. Dependências, ordem de fixpoint, blast-radius, fixtures

### 4.1 Grafo de dependências
```
#254 (DONE) ──► List/Map/Deque/Queue/Stack/LinkedList  ............  FASE 1a (constrói JÁ)
                       (sem constraint de capacidade)
9-ops (próximo reseed) ─┬─► IEq  ─► HashSet, Dictionary  ┐
                        │            (com IHash abaixo)   ├─ FASE 1b (após 9-ops)
                        └─► IOrd ─► SortedSet, SortedDictionary, PriorityQueue
IHash (ESTE doc, §2.2) ──► HashSet, Dictionary  ..................  FASE 1b (junto com 9-ops)
§10 F2 (região partilhada — JÁ semeada, tk_region_program) ─┐
§10 F1 (thread+raiz por tarefa — BLOQUEADO) ───────────────┼─► Concurrent* + teko::threads::sync  ... FASE 2
runtime C: tk_mutex_lock/tk_atomic_* (§3.3, por acrescentar)┘      (modelo B: shared-memory sob lock/interlocked)
```

### 4.2 Faseamento (genéricas primeiro, concorrentes após F1+F2)
- **Fase 1a — genéricas SEM capacidade (constrói já, só depende de #254 DONE):** os combinadores novos
  (`arr_insert_at`/`arr_swap`/`arr_reverse`/`arr_slice`), `Deque`, `Queue`, `Stack`, `LinkedList`
  (forma free-list), read-only wrappers das sequências. `.tkt` two-instantiation cada.
- **Fase 1b — genéricas COM capacidade (após 9-ops aterrar + `IHash` deste doc):** `IHash` + `StrKey`
  + adaptadores prim; `Dictionary<K: IEq & IHash, V>`, `HashSet<T: IEq & IHash>`, `SortedSet<T: IOrd>`,
  `SortedDictionary`, `PriorityQueue`. Migração do `Map<V>` → `Dictionary<StrKey,V>` é ADITIVA (o `Map`
  fica como alias/atalho `str`; não se remove — quebraria `teko::env`).
- **Fase 2 — concorrentes + coordenação (após F1 aterrar; F2 já semeada, `tk_region_program`):** o
  modelo B (§3) — `ConcurrentDictionary` (striped), `ConcurrentQueue`/`ConcurrentStack` (lock-free CAS),
  `ConcurrentBag`, `BlockingCollection` (mutex+futex), e a sub-família `teko::threads::sync`
  (`WaitGroup`/`Atomic<T>`/`Semaphore`/`Barrier`/`Latch`). Requer as primitivas C
  `tk_mutex_lock`/`tk_atomic_*` (§3.3) no `teko_rt.{c,h}`. Adianta-se AGORA os tipos-handle value puros,
  o Javadoc-contrato de toda a família, e os skeletons com honest-stop (§3.9).

### 4.3 Ordem de fixpoint e blast-radius
- **Auto-fixpoint do compilador:** TODA esta carga é **corpus de stdlib novo** — não toca maquinaria de
  checker/codegen/parser. As fns só são estampadas quando USADAS por um genérico monomorfizado (as
  fixtures são o primeiro uso). Logo é **aditivo-inerte** para o fixpoint do compilador: `bin-a == bin-b`
  fecha trivialmente (o corpus do compilador não instancia `Dictionary`/`SortedSet`). Blast-radius de
  MAQUINARIA = **zero** (nenhuma edição em `src/checker`/`src/codegen`/`src/parser`). O blast-radius é
  só de biblioteca: novos `.tks` em `src/collections/` (+ `src/cmp/` para `IHash`, alinhado com onde o
  9-ops porá `IEq`/`IOrd`).
- **Dependência de SEED (bootstrap):** o corpus da stdlib NÃO pode usar uma feature ausente do seed. As
  coleções de Fase 1b usam `T: IEq`/`operator __eq` sobre `T` — features que só existem DEPOIS do reseed
  do 9-ops. **Logo Fase 1b só entra num seed que já contenha o 9-ops** (o próximo reseed). Fase 1a entra
  já (só usa #254, no seed atual). Sequenciar: 9-ops reseed → Fase 1b. Fase 2 → após reseed do §10.

### 4.4 Fixtures de regressão (`.tkt` unit + `.tkr`/`.tkp` projeto)
Padrão herdado (`src/collections/{list,map}_test.tkt` two-instantiation; `examples/regressions/
value_type_operators/` para projeto com `.tkr` `Then stdout pattern`). Cada `exit`/token codifica QUAL
ramo correu (axis-law: testa-se o valor, nunca um efeito incidental).

**Fase 1a (`.tkt`, constrói já):**
- `deque_test.tkt` — `Deque<i64>`/`Deque<str>`: push/pop nas duas pontas, ordem, snapshot independente.
- `queue_stack_test.tkt` — FIFO (`Queue`) vs LIFO (`Stack`) em i64+str; empty-dequeue/pop = null.
- `readonly_test.tkt` — a view reflete mutações da coleção-dona; mutadores ausentes (não compila = doc).

**Fase 1b (`.tkt` + `.tkr`, após 9-ops):**
- `dictionary_test.tkt` — `Dictionary<StrKey,i64>` casa o comportamento de `Map<i64>` (round-trip,
  update-not-grow, collision distinctness, remove present/absent) + uma segunda instância `<StrKey,str>`.
- `hashset_test.tkt` — add/contains/remove; add duplicado = no-op; duas instâncias.
- `sortedset_test.tkt` — `SortedSet<i64>`: inserção fora de ordem sai ordenada; `add` duplicado = false.
- `priorityqueue_test.tkt` — `PriorityQueue<i64>`: dequeue devolve o mínimo em ordem crescente.
- **REJEITAR** (`examples/regressions/diagnostics/`): `dict_key_no_ihash` — `Dictionary<Plain, V>` com
  `Plain` sem `IHash` → `Then diagnostic` de "constraint IHash não satisfeita" (prova o gate).

**Fase 2 (`.tkr` projeto, após F1+F2 — modelo B):**
- `examples/regressions/concurrent_dict_striped/` — `main.tks` cria um `ConcurrentDictionary<StrKey,i64>`
  na região F2, faz `spawn` de N tarefas que `insert` em chaves disjuntas (buckets distintos → striping
  sem serializar), `join` todas, e soma os `get` → `exit`/stdout codifica a soma. Prova a partilha-sob-
  lock-fino end-to-end (o padrão `Arc<Mutex>` sancionado, §3.2).
- `examples/regressions/concurrent_stack_cas/` — `ConcurrentStack<i64>` sob 2 tarefas a `push` em
  paralelo e uma a `pop`; asserção de que a contagem final e a soma batem (prova o CAS lock-free de
  Treiber sem itens perdidos/duplicados, §3.7).
- `examples/regressions/waitgroup_barrier/` — `WaitGroup` a esperar N tarefas cuja contagem cresce em
  runtime (`add` após lançamento); `exit` codifica que o `wait` só retorna após o último `done`. Prova a
  coordenação `teko::threads::sync` (§3.8) e o `tk_atomic_add` + futex.

### 4.5 Codec `.tkb` / backend
Nenhum node novo. `Dictionary`/`SortedSet`/etc. são `class`/`struct` genéricos comuns — round-trip pelo
codec de classe/struct existente. `IHash` é uma interface de método comum. `operator __eq` em `StrKey`
já round-trip desde §9 (`is_operator` serializado). **Zero superfície de codec/backend nova.**

---

## 5. Tensões de lei (law-first) + HALT

- **T-1 — o modelo de memória das coleções concorrentes: shared-memory-sob-lock (B) vs. actor-via-chan.**
  RESOLVIDO por RULING DO DONO (2026-08-13), reconciliado law-first (§3.2). O plano anterior recusava a
  Opção B como "viola o §10" e recomendava chan-actor. O dono INVERTEU: recusou o chan-actor como
  *"arquitetura demais"* e escolheu **B — shared-memory sob lock/interlocked (CAS lock-free)**, *"simples
  e metal... velocidade e economia de memória"*. A reconciliação com o §10 é o padrão **`Arc<Mutex<T>>`**:
  o ISOLAMENTO continua o default (§10 = "isolado por omissão, partilhado-sob-lock quando explicitamente
  declarado"); a coleção concorrente é a fuga EXPLÍCITA, marcada, opt-in — uma exceção SANCIONADA do §10,
  não a sua dissolução, pela MESMA lógica que já abençoa `chan` (shared-state gerido pelo runtime atrás de
  um handle estreito, §3.8). O C# é a inspiração do modelo AGORA, com a precisão que o doc antigo errou:
  `Interlocked`/striped/futex, não um lock global (§3.1). **Não é HALT** — a ruling é a lei; o design
  contrata contra a fundação F1+F2 do §10 (F2 já semeada, `tk_region_program`).
- **T-2 — `IHash` como interface de método vs. a ruling D18 (`Hashable`≡`Hash` structural).** DECISION_LOG
  D18 (:161) colapsou `Hashable`/`Comparable` em traits ESTRUTURAIS derivados. Mas `map.tks:8-18` mede
  que o trait estrutural é OPACO num type-param (não despacha por `K`). A régua §9.4 do 9-ops substitui a
  capacidade-estrutural-opaca por **interface real que despacha** (`IEq`/`IOrd`). `IHash` segue a MESMA
  filosofia: interface de método que DESPACHA sobre `T` (via #254), não um derive estrutural opaco.
  **Coerente com a direção pós-9-ops; a D18 fica como HISTÓRICO da era pré-interface.** Reportar para
  cima que D18 deve ser marcada superseded quando `IHash` aterrar (NÃO abro issue). **Não é HALT.**
- **T-3 — o `Map<V>` `str`-keyed permanece ou é removido quando `Dictionary` chega?** RESOLVIDO:
  PERMANECE (aditivo). `teko::env = Map<str, str?>` e os `.tkt` dependem dele; removê-lo é regressão.
  `Dictionary<StrKey,V>` é o sucessor genérico; `Map<V>` fica como atalho `str` documentado. **Não é HALT.**
- **T-4 — `LinkedList<T>` por-ponteiro vs free-list.** O nó genérico auto-referente estampa mal na pilha
  atual (`map.tks:20-25`). RESOLVIDO: forma free-list (arrays paralelos de índice) HOJE; por-ponteiro é
  ADJACENTE quando a pilha genérica suportar nós auto-referentes. Limite documentado. **Não é HALT.**
- **T-5 — `exp` (pedido do dono) vs `pub` (convenção pure-Teko).** RESOLVIDO (§2.1): `pub type` para
  pure-Teko (precedente `List`/`Map`); `exp` fica para export C-ABI. **Não é HALT.**

**SEM HALT.** Todas as tensões resolvem law-first: as genéricas reusam #254 + o 9-ops + `IHash`; as
concorrentes adotam o modelo B (shared-memory sob lock/interlocked) por ruling do dono, reconciliado com
o §10 pelo padrão `Arc<Mutex<T>>` (fuga explícita e sancionada, não violação). As dependências abertas
(9-ops; §10 F1; as primitivas C `tk_mutex_lock`/`tk_atomic_*`) têm plano/design SELADO ou seam já em
produto (F2 = `tk_region_program`, o idioma atómico = `teko_rt.c:1570`/`:1588`) — este doc contrata
contra a forma DECLARADA delas e adianta tudo o que não precisa da API bloqueada (§3.9, §4.2).

---

## 6. Resumo para o implementer — o que fecha JÁ e o que fica bloqueado

**Constrói JÁ (só #254, DONE):** os combinadores novos em `collections.tks`; `Deque`/`Queue`/`Stack`/
`LinkedList`(free-list); read-only wrappers; os `.tkt` two-instantiation. Adianta AGORA (compila sem a
dep): `IHash` + `StrKey` + adaptadores (interface de método, não precisa da camada operador); os
tipos-handle da família concorrente/coordenação (`ConcurrentDictionary`/`ConcurrentStack` como classes
com skeleton, `WaitGroup`/`Atomic<T>`/`Semaphore` como `struct { id: u64 }` value puros); os skeletons
com honest-stop (`panic("blocked on F1+F2")`); o Javadoc-contrato de toda a família.

**Bloqueado em 9-ops (próximo reseed) —** resume em minutos quando aterrar: `Dictionary<K: IEq & IHash,
V>`, `HashSet<T: IEq & IHash>`, `SortedSet`/`SortedDictionary`/`PriorityQueue<T: IOrd>`, e a migração
aditiva `Map`→`Dictionary<StrKey,V>`. (O `==`/`<` genérico sobre `T` é a crumb 4 do doc 9-ops.)

**Bloqueado em §10 F1 + primitivas C (modelo B) —** resume trocando os honest-stops por chamadas
`mutex_lock`/`atomic_cas`/`atomic_add`: `ConcurrentDictionary` (striped, §3.5), `ConcurrentQueue`/
`ConcurrentStack` (CAS lock-free), `ConcurrentBag`, `BlockingCollection` (mutex+futex), e a sub-família
`teko::threads::sync` (`WaitGroup`/`Atomic<T>`/`Semaphore`/`Barrier`/`Latch`). **Modelo B, sancionado
pelo dono — shared-memory sob lock/interlocked, reconciliado com o §10 pelo padrão `Arc<Mutex<T>>`
(§3.2).** F2 (a região partilhada) já está semeada no runtime (`tk_region_program`, `teko_rt.c:2305`);
faltam F1 (thread por tarefa) e as primitivas C `tk_mutex_lock`/`tk_atomic_*` (§3.3).

**ADJACENTE (reportado, NÃO construído aqui):** `sort<T: IOrd>` genérico (destravado pelo 9-ops);
`LinkedList` por-ponteiro (quando a pilha genérica suportar nós auto-referentes); `PriorityQueue<E,P>`
com chave de prioridade separada; leitura lock-free do `ConcurrentDictionary` (variante aditiva quando a
representação estabilizar, §3.7); broadcast/multi-processo dos canais (§6.4 do doc §10). **Reportar para
cima:** a versão anterior deste doc (v1, chan-actor) fica SUPERSEDED por esta secção §3 reescrita — o
integrador dobra em `fix/retirement`.
