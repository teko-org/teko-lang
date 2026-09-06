# CK1 — scoped hash maps: busca direta no checker (achatamento + de-O(n²), item 2) — 0.3.1

Arquiteto, 2026-08-06. Ramo `design/ck1-scoped-hash-maps` (de `origin/fix/retirement`).
Documento de DESENHO — nenhuma linha de produto. `bootstrap/teko.c` é SAÍDA.
Regra do dono honrada: **proposta com arquivo:linha, não contra-argumento; alarme só se provado;
medido e afirmado sempre separados.**

Este doc realiza o crumb **CK1** do plano `docs/memory/achatamento-de-n2-plano-0.3.1.md` (§3, frente
CHECKER): trocar as **varreduras lineares** de `Env`/`TypeTable` por **busca direta** por um índice de
hash lateral, mudando a complexidade sem mover **um byte** do emit (o fixpoint `gen2==gen3` é
inegociável) e sem mudar **nenhuma ordem observável** (a lei da família da sonda por nome nu,
`docs/design/bare-name-probe-family.md`).

---

## 0. A tese, aplicada a CK1

O checker é o maior consumidor de tempo do self-host e hoje resolve nome por **varredura linear** —
O(n) por nome, O(n²) sobre um corpo (plano §3). CK1 é a metade **LÓGICA**: muda a **complexidade** da
consulta, não o **resultado**. A metade **FORMA** de CK1 é declarar a tabela já com o índice ao lado —
uma estrutura de dados nova, escrita uma vez, consultada milhões de vezes.

**Ideia central (a que preserva tudo):** o índice de hash é uma estrutura **LATERAL**. As listas
ordenadas (`Env.base`, `Env.bindings`, `TypeTable`'s `[]TypeReg`) permanecem **intactas e na mesma
ordem** — elas continuam sendo a fonte de toda iteração observável (emissão de símbolos, enumeração
de diagnósticos, o ponto-fixo). O índice é consultado **apenas** pelas consultas pontuais
(`lookup_*`, `type_table_find`), e cada consulta devolve **exatamente** o mesmo registro que a
varredura reversa devolveria. Hash para consultar; lista para iterar.

---

## 1. Pergunta 1 — existe primitiva de hash hoje? (evidência)

Resposta curta: **existe a FUNÇÃO de hash; NÃO existe a ESTRUTURA de índice adequada ao checker. A
primeira crumb constrói a estrutura.**

| o quê | existe? | evidência (arquivo:linha) |
|---|---|---|
| função de hash `str -> u64` (FNV-1a) | **SIM**, na semente | `src/runtime/teko_rt.tks:529` `exp fn str_hash`; twin C `tk_str_hash` `src/runtime/teko_rt.c:942`. Já chamada pelo checker por vizinhança (`teko::runtime::str_ends_with`, `resolve.tks:224`) — bootstrap-safe |
| conjunto open-addressing O(1) | **SIM, mas só em C** | `tk_line_insert_packed` `src/runtime/teko_rt.c:4167` (grow-then-probe, dedup) — chaveado por `u64`, lado runtime, não reaproveitável para dados de checker Teko |
| classe `Map<V>` de propósito geral | **SIM, mas inadequada** | `src/collections/map.tks:30`. Dois bloqueios: (a) o `map_find_index` interno (`map.tks:143`) é **varredura LINEAR** — não é O(1), é um scan com hash cacheado; (b) é uma **CLASSE de semântica de referência**, e o `Env`/`TypeTable` do checker são **threaded por valor** (funcionais: `define`/`with_*`/`seal` reconstroem o struct). Encher esse `Map` custa O(n) cópias funcionais por `insert` = O(n²) para popular. NÃO serve |
| mutação de array in-place O(1) (`a[i]=v`) | **NÃO em Teko puro** | `arr_replace_at` COPIA O(n) e o doc-comment é explícito: *"Teko has no array index-assignment"* (`src/collections/collections.tks:21,29`). Bloqueia open-addressing O(1) em Teko puro (depende da fundação F1/F2/F3 do plano §2, ou de uma primitiva em `teko_rt.c`) |
| merge sort estável | **SIM, na semente** | `teko::sort::sort_i64`/`sort_str` `src/sort/sort.tks:81,157` (merge-sort, estável — `sort_test.tkt:74`). Habilita um índice **ordenado + busca binária** em Teko puro, hoje |

**Conclusão de viabilidade.** Sem `a[i]=v`, o open-addressing O(1) em Teko puro está BLOQUEADO na
fundação F3 (§11). O que está DESBLOQUEADO hoje, em Teko puro, é um **índice ordenado por hash +
busca binária**, construído uma vez com o merge-sort existente: consulta **O(log n + c)** (c =
colisões nesse hash, ≈1). Isto já **colapsa o O(n²)** do corpo para O(n log n) e é o que CK1 entrega.
A troca para O(1) verdadeiro (mesma API) é a follow-up CK1' atrás de F3 (§11). Medido vs afirmado: o
ganho de O(n²)→O(n log n) é o oráculo de §9; qualquer afirmação de "O(1)" é reservada para CK1'.

---

## 2. Pergunta 2 — os sítios a converter, ranqueados por hotness

Toda consulta abaixo é **por nome nu**, chamada por referência resolvida — o núcleo do O(n²). Ranque
por hotness (frequência × custo por chamada) e por raio de mudança (menor raio primeiro, para gate
isolado).

| # | sítio (arquivo:linha) | o que varre | hotness | raio |
|---|---|---|---|---|
| 1 | `lookup_binding` `src/checker/scope.tks:204` | `bindings` reverso, depois `base` reverso | **máxima** — por CADA referência a nome (todo `TVar`) | mínimo (só `scope.tks`+`seal`) |
| 2 | `lookup_call` `scope.tks:320` / `call_ns` `scope.tks:349` | idem, com predicado `call_binding_matches` | alta — por CADA chamada | pequeno |
| 3 | `lookup_value_in_ns` `scope.tks:238` | idem, com `qualifier_selects_ns` | média — refs `ns::NAME` | pequeno |
| 4 | `type_table_find` `src/checker/resolve.tks:97` (DOIS passes) | `TypeTable` inteira ×2, com `qualify_eq`/`name_last_segment` por candidato | **máxima** — funil de `is_interface_name`/`is_class_name`/`is_struct_name`/`is_trait_name`/`resolve_named`/`expand_variant`/`widens_into` | **grande** (34+ sítios usam `TypeTable` como `[]TypeReg`) |
| 5 | família `resolve_*_type_reg` (`resolve.tks:288,350,422,457`, `type_ns_of:261`, `type_ref_ns:507`, `type_table_find_path:139`) | `TypeTable` por referência de tipo | alta | grande (mesma tabela do #4) |
| — | `lookup_inject` `scope.tks:102` | `env.injects` (poucas binds DI) | baixa — NÃO hot | — (deixa linear) |

O #4 já é O(n²) **medido com alocação**: o doc-comment de `resolve.tks:33-35` registra *"63M concats
/ ~1 GB+overhead on a self-build"* nas varreduras da tabela de tipos, mitigado por `qualify_eq` mas
ainda O(n) por consulta. Os #1 e #4 são co-primos suspeitos; #1 vai **primeiro** por raio mínimo.

Nota de segurança (família da sonda por nome nu): #2/#3/#5 filtram por namespace. O índice **não pode
introduzir uma resposta por nome nu onde hoje há filtro por namespace** — o predicado é preservado
byte-a-byte (§5). A pergunta de revisão de `bare-name-probe-family.md` — *"qual namespace pergunta?"*
— continua respondida exatamente como hoje.

---

## 3. A primitiva a construir — `NameIndex` (CK1.0)

Novo módulo `src/checker/nidx.tks` (namespace `teko::checker`). Um índice de hash **imutável, por
valor, construído uma vez** sobre um array read-only, com consulta por busca binária. Estrutura:
slots ordenados por hash; cada slot leva a POSIÇÃO original no array indexado (nunca o valor), então o
chamador re-testa nome/predicado no array real e escolhe o candidato certo.

```teko
/**
 * NameSlot — one entry of a NameIndex: a name's FNV-1a hash paired with the POSITION of that name
 * in the ORIGINAL indexed array (an `Env.base` index, or a `TypeTable` index). The slot never holds
 * the value or the name itself — the caller re-reads the original array at `pos` to confirm the
 * exact name and apply its own namespace predicate, so a hash collision is always resolved against
 * real bytes and the ordered array stays the single source of truth.
 *
 * @param hash  the FNV-1a hash (teko::runtime::str_hash) of the indexed name
 * @param pos   the index of that name in the original ordered array it was built from
 * @since 0.3.1
 */
type NameSlot = struct {
    hash: u64
    pos: u64
}

/**
 * NameIndex — a build-once, value-threaded hash index over an ordered `[]str` of names, its slots
 * sorted by `hash` ascending so a lookup binary-searches to the run of slots sharing a hash. It is a
 * LATERAL structure: the array it indexes is never reordered, so every observable iteration over that
 * array (symbol emission, diagnostic order, the fixpoint) is untouched; the index only accelerates
 * point lookups. Built once (over a sealed/collected read-only table) and copied by handle through
 * the functional `Env`/table threading, because nothing mutates the indexed array after the build.
 *
 * COMPLEXITY. Construction is O(n log n) once (a stable merge sort over the slots). A lookup is
 * O(log n + c), where c is the number of slots colliding on the sought hash (≈1). This collapses the
 * per-body O(n²) linear-scan cost to O(n log n) with no behavioural change. The O(1) open-addressing
 * form (same API) is deferred to CK1' behind the in-place-array foundation (F3) — see the design doc
 * §11; today's Teko has no `a[i] = v` (collections.tks:21), so open-addressing cannot be built in
 * O(n) in pure Teko yet.
 *
 * @param slots  the (hash, pos) slots, sorted by `hash` ascending
 * @since 0.3.1
 */
type NameIndex = struct {
    slots: []NameSlot
}

/**
 * nidx_empty — the empty index (no slots). The pre-seal / pre-collect state: a lookup against it
 * finds nothing and the caller falls through to its (small) linear segment, exactly as today.
 *
 * @return an empty NameIndex
 * @since 0.3.1
 */
fn nidx_empty(): NameIndex { NameIndex { slots = teko::list::empty() } }

/**
 * nidx_build — build a NameIndex over `names` (the ordered name-of each entry in the array being
 * indexed, e.g. `b.name` for each `Env.base` binding). Hashes each name (teko::runtime::str_hash),
 * pairs it with its position, and stable-merge-sorts the slots by hash. O(n log n), called ONCE at
 * the seal/collect boundary; the result is threaded read-only thereafter.
 *
 * @param names  the ordered names of the array to index (position i ↔ names[i])
 * @return       the sorted hash index over `names`
 * @since 0.3.1
 */
fn nidx_build(names: []str): NameIndex {
    mut slots: []NameSlot = teko::list::empty()
    mut i: u64 = 0
    loop {
        if i >= names.len { break }
        slots = teko::list::push(slots, NameSlot { hash = teko::runtime::str_hash(names[i]); pos = i })
        i++
    }
    NameIndex { slots = nidx_sorted(slots) }
}

/**
 * nidx_lower_bound — the index of the first slot whose `hash` is >= `h` (binary search over the
 * hash-sorted slots). The caller then walks forward from here while `slots[k].hash == h`, reading
 * each `slots[k].pos`, to visit every candidate that could carry the sought name.
 *
 * @param idx  the built index
 * @param h    the sought name's hash
 * @return     the first slot index with `slots[k].hash >= h` (idx.slots.len when none)
 * @since 0.3.1
 */
fn nidx_lower_bound(idx: NameIndex, h: u64): u64 {
    mut lo: u64 = 0
    mut hi: u64 = idx.slots.len
    loop {
        if lo >= hi { break }
        let mid = lo + (hi - lo) / 2
        if idx.slots[mid].hash < h { lo = mid + 1 } else { hi = mid }
    }
    lo
}
```

`nidx_sorted` é um merge-sort estável interno sobre `[]NameSlot` por `hash` (o mesmo idioma provado
de `src/sort/sort.tks:137` `msort_i64`, adaptado ao struct). A estabilidade NÃO é carregada pela
correção — §5 mostra que o resultado é um **máximo de posição**, independente da ordem intra-bucket —
mas o merge-sort é o caminho puro-funcional disponível hoje. Unit tests em `src/checker/nidx_test.tkt`
(§8).

**O padrão de consulta canônico** (copiado verbatim em cada sítio convertido — Teko não tem closures,
então a walk é inlined; é sempre estas ~6 linhas):

```teko
    // walk the bucket of slots sharing this hash; keep the candidate with the LARGEST position
    // that matches the exact name AND the caller's predicate — identical to a reverse linear scan's
    // first accepted match (§5).
    let h = teko::runtime::str_hash(name)
    mut best: i64 = -1
    mut k = nidx_lower_bound(idx, h)
    loop {
        if k >= idx.slots.len { break }
        if idx.slots[k].hash != h { break }
        let p = idx.slots[k].pos
        if arr[p].name == name /* && predicate(arr[p]) */ && (p to i64) > best { best = p to i64 }
        k++
    }
```

---

## 4. Integração no `Env` (CK1.1–CK1.3) — assinaturas

O `Env` (`scope.tks:72`) ganha UM campo `base_index: NameIndex`. `base` é selado UMA vez por `seal`
(`scope.tks:114`, doc: *"Called ONCE per collected env"*) e compartilhado read-only por todas as forks
(cada função/braço forka da MESMA base). Logo o índice é construído uma vez em `seal` e copiado por
handle em todo `with_*`/`define*` sem invalidar (esses só empurram em `bindings`, nunca em `base`).

Deltas de assinatura (o campo entra em TODO construtor de `Env` do arquivo — mecânico):

```teko
pub type Env = struct { base: []ValBinding; bindings: []ValBinding; cur_ns: str; owner_type: str; di: DiRegistry; injects: []DiInjectBind; fn_unsafe: bool; file: str; base_index: NameIndex }
```

- `env_empty` → `base_index = nidx_empty()`.
- `seal` → após mover `bindings` para `b`, `base_index = nidx_build(nidx_names_of(b))`, onde
  `nidx_names_of(b: []ValBinding): []str` extrai `b[i].name` em ordem.
- `with_di`/`with_injects`/`with_fn_unsafe`/`with_ns`/`with_file`/`with_owner`/`define`/`define_fn`/
  `define_const` → carregam `base_index = env.base_index` inalterado.

`lookup_binding` reescrito (CK1.1) — mantém o scan de `bindings` (pequeno, tail-first, INALTERADO) e
troca só o scan de `base`:

```teko
/**
 * lookup_binding — the WHOLE binding (type + mutability) for `name`, innermost-first: the mutable
 * `bindings` are scanned tail-first (locals shadow, small — unchanged), then the SEALED globals are
 * resolved through the hash index `env.base_index` by taking the base entry with the LARGEST position
 * whose name matches — byte-identical to the old reverse linear scan over `base` (design §5), but
 * O(log n + c) instead of O(n).
 *
 * @param env   the type-checking environment
 * @param name  the sought bare name
 * @return      the matching binding, or an "undefined name" error
 * @throws      when no local or global binding is named `name`
 */
fn lookup_binding(env: Env, name: str): ValBinding | error {
    mut i = env.bindings.len
    loop {
        if i == 0 { break }
        i = i - 1
        if env.bindings[i].name == name { return env.bindings[i] }
    }
    let h = teko::runtime::str_hash(name)
    mut best: i64 = -1
    mut k = nidx_lower_bound(env.base_index, h)
    loop {
        if k >= env.base_index.slots.len { break }
        if env.base_index.slots[k].hash != h { break }
        let p = env.base_index.slots[k].pos
        if env.base[p].name == name && (p to i64) > best { best = p to i64 }
        k++
    }
    if best >= 0 { return env.base[best to u64] }
    error { message = $"undefined name: {name}" }
}
```

`lookup_call`/`call_ns` (CK1.2) e `lookup_value_in_ns` (CK1.3) usam o MESMO walk, com a única
diferença de o teste de aceitação somar o predicado existente (`call_binding_matches(env.base[p],
callee, env.cur_ns, qualified)` / `qualifier_selects_ns(env.base[p].ns, ns)`), tomando o `p` máximo
que satisfaz `name == && predicado`. `lookup_inject` fica linear (não hot).

---

## 5. Como determinismo + fixpoint são preservados (a prova)

**Afirmação.** Cada `lookup_*` reescrito devolve o MESMO `ValBinding` (ou o mesmo `error`) que a
varredura reversa devolve, para toda entrada. Portanto: mesmo tipo resolvido → mesma AST tipada →
mesmo emit → **fixpoint `gen2==gen3` byte-idêntico**; mesma resolução → mesma ordem/texto de
diagnóstico.

**Prova (por que o "máximo de posição" ≡ "primeiro do scan reverso").** A varredura original percorre
`base` de `len-1` a `0` e retorna `base[p]` para o MAIOR `p` com `base[p].name == name` E
`predicado(base[p])`. O walk indexado visita exatamente os `p` cujo slot tem `hash == str_hash(name)`.
Como `str_hash` é função pura dos bytes, TODO `p` com `base[p].name == name` tem
`str_hash(base[p].name) == str_hash(name)` — logo **nenhum candidato de nome-igual é perdido** (o
bucket é superconjunto do conjunto de nomes-iguais). Dentre eles tomamos o `p` máximo que satisfaz
`name ==` (re-teste explícito de bytes) e o predicado — idêntico ao primeiro-do-reverso. Slots que
colidem no hash mas têm nome diferente são rejeitados pelo re-teste `base[p].name == name`,
exatamente como o `==` do scan linear os rejeitava. A ordem intra-bucket é irrelevante porque o
resultado é um **máximo**, não um "primeiro visitado". ∎

**Ordem observável fica intacta por construção.** O índice é LATERAL: `base`, `bindings` e
`[]TypeReg` não são reordenados. Toda passada que ITERA essas listas em ordem — emissão de símbolos,
enumeração de diagnósticos, o walk do ponto-fixo — não toca no índice e vê a mesma sequência de sempre
(a mesma lei da família da sonda por nome nu, `bare-name-probe-family.md`: "tornar uma ordem estável
não conserta dependência de ordem — mas aqui não MUDAMOS ordem nenhuma"). O índice só é lido pelas
consultas pontuais, e cada uma é provada equivalente acima.

**Para `type_table_find` (CK1.4)** a mesma prova vale com o índice chaveado pelo `name_last_segment` de
cada entrada: em ambos os passes originais, um match implica igualdade do último segmento (pass 1:
match exato ou `qualify_eq` — ambos forçam mesmo último segmento; pass 2: é literalmente igualdade de
último segmento). Logo o bucket de `str_hash(name_last_segment(query))` é superconjunto de todos os
matches possíveis de AMBOS os passes; dentro do bucket aplicam-se os DOIS passes na MESMA ordem
(pass-1-primeiro, primeiro-a-casar) restrita aos candidatos do bucket → primeiro-match idêntico. ∎

---

## 6. Bootstrap-safety (plano §5, carga aditiva)

- **CK1.0** adiciona `src/checker/nidx.tks` **sem uso** por `src/` na mesma carga. `str_hash` já está
  na semente; `nidx.tks` compila com a semente anterior. Gate: semente constrói gen1.
- **CK1.1+** só passam a USAR `nidx_*` DEPOIS de CK1.0 já estar na semente (gen1 anterior). A regra
  §5 do plano: um crumb que ensina um idioma novo não é adotado por `src/` na mesma carga. Sequência:
  CK1.0 entra e vira semente; só então CK1.1 (que chama `nidx_build`/`nidx_lower_bound`) é seguro,
  porque a semente que constrói gen1 já conhece essas funções. Nenhum crumb usa feature ausente na
  sua própria semente — `NameIndex` é struct + arrays + `str_hash` + `loop`, tudo já na semente.

---

## 7. Ordem dos crumbs + pontos de ritual

Cada crumb é **gate-able sozinho** (fixpoint via caminho-C + `teko test .`; o fixpoint nativo
`gen2==gen3` está ABERTO em PR #112, então a prova interim é caminho-C + timing de fase). "Ritual" =
o gate completo (build gen1→gen2→gen3, C-fixpoint, `teko test .`, timing) TEM de passar.

| crumb | o quê | toca | prova | ritual? |
|---|---|---|---|---|
| **CK1.0** | `NameIndex` + `nidx_*` + `nidx_test.tkt` (não usado por src) | +`src/checker/nidx.tks`, +`src/checker/nidx_test.tkt`, `src/checker/module índice` | `teko test .` (unit) + C-fixpoint idêntico (código morto) | **SIM** (semente nova) |
| **CK1.1** | `base_index` no `Env`+`seal`; converte `lookup_binding` | `scope.tks` | C-fixpoint idêntico + **timing do checker cai** | **SIM** (primeira conversão de comportamento — prova a equivalência §5 no corpus real) |
| **CK1.2** | converte `lookup_call`+`call_ns` | `scope.tks` | C-fixpoint idêntico + timing | não (mesmo mecanismo) |
| **CK1.3** | converte `lookup_value_in_ns` | `scope.tks` | C-fixpoint idêntico + timing | não |
| **CK1.4** | `TypeTable` embrulhado (`{ regs; by_last }`) + acessores; converte `type_table_find` e a família `resolve_*_type_reg` | `resolve.tks` (+ collect/typer nos sítios `table[i]`/`table.len`/`push`) | C-fixpoint idêntico + **timing do checker cai** | **SIM** (maior raio) |
| **CK1-fim** | medição consolidada antes/depois | — | timing por fase documentado; fixpoint verde | **SIM** |

CK1.4 é o maior risco de raio (34+ sítios leem `TypeTable` como `[]TypeReg`). Recomendação: **embrulho
+ acessores** (`tt_len`/`tt_at`/`tt_push`/`tt_freeze`), sweep mecânico, e — se o diff crescer demais —
CK1.4 vira o seu PR próprio empilhado. `TypeTable` passa de `= []TypeReg` para
`= struct { regs: []TypeReg; by_last: NameIndex }`; `by_last` construído por `tt_freeze` no fim do
collect (espelho de `seal`), chaveado por `str_hash(name_last_segment(reg.name))`.

---

## 8. Fixtures de regressão (inputs → exit codes nativos)

Cada fixture FALHA se o índice mudar uma resolução. Regressões nativas em
`examples/regressions/own_native/` (o gate existente executa e checa o exit code):

1. **`ck1_shadow_local` (exit 7)** — um `let x` local sombreia um `const x` global do mesmo nome; o
   programa retorna um valor que só bate se o LOCAL vencer. Prova: `bindings` antes de `base`
   preservado (o scan de bindings não passa pelo índice).
2. **`ck1_ns_call` (exit 11)** — duas fns de MESMO nome nu em namespaces distintos (`a::f`, `b::f`);
   uma chamada qualificada `a::f()` deve resolver a de `a`. Prova: predicado `call_binding_matches`
   preservado no walk (§5) — a família da sonda por nome nu não regride.
3. **`ck1_redef_last_wins` (exit 3)** — o mesmo nome global definido N vezes (via re-collect); a
   ÚLTIMA definição (maior posição) vence. Prova: "máximo de posição" ≡ scan reverso.
4. **`ck1_type_same_bare` (exit 5)** — dois tipos de mesmo último segmento em namespaces distintos
   resolvidos por referência qualificada; o correto é escolhido. Prova de CK1.4 (§5, tabela de tipos).
5. **`src/checker/nidx_test.tkt`** (`#test`, exit 0) — `nidx_build` sobre `["a","b","a","c"]`;
   `nidx_lower_bound` + walk acham o bucket de `"a"` com posições `{0,2}` e devolvem max=2; nome
   ausente `"z"` → bucket vazio; array vazio → `nidx_empty` não acha nada; colisão sintética (dois
   nomes forçados ao mesmo `str_hash` por construção do teste) → re-teste de bytes separa os dois.

Inversão obrigatória (o número é o portão): se o mecanismo do índice for removido de um `lookup_*`
mas o `str_hash` continuar, os fixtures 1–4 devem continuar PASSANDO (equivalência), e o timing deve
REGREDIR — se o timing NÃO piorar ao remover o índice, o crumb não provou ganho e reprova (§9).

---

## 9. Medição — o oráculo (plano §6)

Três números, medidos ANTES e DEPOIS de cada crumb de comportamento (CK1.1, CK1.4):

1. **tempo da fase CHECKER** (`phase_begin`/`phase_end_ok` já existem, plano §6) — o número
   PRINCIPAL de CK1. O ganho O(n²)→O(n log n) TEM de aparecer aqui; sem queda medida, o crumb não
   entra.
2. **fixpoint `gen2==gen3` byte-idêntico** (interim: C-path fixpoint, PR #112) — inegociável.
3. **pico RSS** — SECUNDÁRIO para CK1. Honestidade: CK1 é otimização de TEMPO; o índice adiciona
   `slots` (16 B × n_globais, uma vez) então o RSS pode subir de leve — aceitável SE o tempo cai e o
   fixpoint se mantém. O alvo de RSS (< 2.5 GiB) é da frente EMIT (AL Wave), não de CK1.

---

## 10. Riscos e tensões de lei

| risco / tensão | resolução (lei-primeiro) |
|---|---|
| o índice muda a ORDEM de resolução → um diagnóstico/símbolo muda | **§5**: resultado é máximo-de-posição ≡ scan reverso; listas ordenadas intactas; índice é lateral. Provado, não afirmado |
| CK1.4 (embrulho de `TypeTable`) toca 34+ sítios → risco de fixpoint | sweep mecânico com acessores; ritual (gate completo) obrigatório; se o diff crescer, CK1.4 é PR próprio empilhado |
| tentação de pôr o hash em `teko_rt.c` (open-addressing O(1) já) | **tensão Teko-only**: `teko_rt.{c,h}` é exceção de RUNTIME mantido, não depósito para estrutura de dados do CHECKER. Um scoped-hash do checker é lógica de checker → **Teko** (`nidx.tks`). O(1) verdadeiro fica para CK1' atrás de F3 (§11), não via C novo |
| "O(1) amortizado" do plano §3 vs "O(log n)" entregue | honestidade medido/afirmado: Teko puro hoje dá O(log n) (sem `a[i]=v`); já mata o O(n²). O(1) é CK1' atrás de F3, mesma API. Declarado, não escondido |
| colisão de hash devolve o registro errado | re-teste explícito `arr[p].name == name` em bytes no walk (§3/§5). Colisão é correta por construção |
| `base_index` inválido após uma mutação de `base` | `base` só muda em `seal` (uma vez); `define*` empurra em `bindings`. Índice construído em `seal`, imutável depois. Invariante documentado no doc-comment de `seal` |

**Nenhuma tensão genuína não-resolvida.** CK1 passa por todas as Leis (Teko-only via `nidx.tks`;
W15/Javadoc nas assinaturas acima; determinismo provado; carga aditiva). Sem HALT.

---

## 11. O que fica BLOQUEADO (design-ahead, adiantado)

- **CK1' — open-addressing O(1) verdadeiro.** BLOQUEADO na fundação **F1/F2/F3** (plano §2: arrays
  cap/len + mutação in-place / `push(ref)`), ou numa primitiva `teko_rt.c` dedicada. Quando F3 fechar,
  `NameIndex` troca o representação interna (slots ordenados → tabela open-addressing com `a[i]=v`)
  **sem mudar a API pública** (`nidx_build`/`nidx_lower_bound` — ou um `nidx_probe` equivalente): os
  sítios de CK1.1–CK1.4 não mudam uma linha. O design (esta seção) e a API (§3) já estão prontos
  contra a forma DECLARADA de F3; o implementador de CK1' resume em minutos.
- **Fixpoint nativo `gen2==gen3`** está ABERTO (PR #112). Até fechar, a prova de cada crumb é
  C-path fixpoint + `teko test .` + timing. Quando #112 fechar, re-rodar os rituais de CK1.1/CK1.4
  sob o fixpoint nativo é um passo de confirmação (não de redesenho).

---

## 12. Arquivos

- NOVO: `src/checker/nidx.tks` (a primitiva), `src/checker/nidx_test.tkt` (unit).
- EDITADO: `src/checker/scope.tks` (`Env`+`seal`+`lookup_binding`/`lookup_call`/`call_ns`/
  `lookup_value_in_ns`), `src/checker/resolve.tks` (`TypeTable`+`type_table_find`+família
  `resolve_*_type_reg`), e os sítios `table[i]`/`table.len`/`push(table,…)` em collect/typer/monomorph.
- NOVO (fixtures): `examples/regressions/own_native/ck1_*` (§8).
