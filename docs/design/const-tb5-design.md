# T-B5 — VM resolve o ponteiro rodata-INTERNO + o encoder nativo fecha a cadeia data→data (#594 Tier-B)

Status: READY-TO-IMPLEMENT (architect, 2026-07-18). Track: Tier-B pointer-bearing
aggregate → rodata (`docs/design/const-module-level-plan.md` §8 crumb T-B5, §5.1
verdict, DECISION_LOG D2). Base: o working tree da const-wave com **T-B1, T-B2, T-B3 e
T-B4 já mergeados** na branch (`RelocSect`/`sect` nos três `Reloc*`,
`LDataReloc`/`LRodata.relocs` na LIR — `lir.tks:154/175`; honest-stop
`honest_data_reloc` em `encode_rodata` NATIVO — `encode_arm64.tks:2645/2679`; ELF
`.rela.rodata`; Mach-O `__const`/COFF `.rdata` relocs + as partições
`macho_partition_relocs`/`coff_partition_relocs`; wasm `wasm_relocate_rodata`).
Predecessores: `docs/design/const-tb{1,2,3,4}-design.md`. crumb 6 (Tier-A → rodata)
landado: `lower.tks:5137 serialize_const`.

> Escopo de T-B5 (verbatim §8): "VM (`lir_interp.tks:527`): resolve a rodata-INTERNAL
> pointer field to its target rodata base at typed load. **🔑 SEED BUMP #3 after
> T-B5** — the data-reloc capability is now in the seed; T-B6's source may use
> pointer-bearing aggregate consts." Este é o ÚLTIMO elo da capability data→data:
> ensina a VM a materializar o endereço-alvo de um ponteiro rodata→rodata, E — porque
> o bump depois deste crumb tem de carregar a cadeia consumidora COMPLETA (ponto 4,
> §4) — abre o honest-stop de `encode_rodata` nos três encoders nativos, deixando o
> produtor (`serialize_const`) como o ÚNICO gate restante até T-B6. **Nenhum produtor
> real exercita a cadeia ainda**, então todo `LRodata.relocs` chega vazio, a memória
> da VM e os objetos nativos são **byte-idênticos**, e todos os goldens/fixpoint ficam
> intactos.

> **Correção de referência de linha.** O plano §8 aponta `lir_interp.tks:527`, que hoje
> cai DENTRO de `interp_global_addr` (`:529-533`) — o site que materializa um
> `LGlobalAddr`. Mas a resolução real do ponteiro NÃO vive no load (não é per-load):
> vive na SEMEADURA da memória, `seed_rodata` (`:171-181`), o único ponto onde a
> imagem rodata da VM é construída — espelhando o emit-time do T-B4 (`wasm_relocate_
> rodata`) e dos writers nativos (a relocation é aplicada UMA vez). Registrado para o
> implementer não procurar a costura no load.

---

## 1. Estado atual provado (com linhas)

### 1.1 A representação de rodata na VM — memória linear própria, célula-por-byte, endereço = índice de célula

A VM tem a sua PRÓPRIA memória linear, `MemStore` (`lir_interp.tks:98`): uma lista
crescente de células i128, **uma célula por unidade de endereço byte-granular — um
endereço É um índice de célula** (`:93-97`, doc: "the interp is an exit-code ORACLE,
not a byte-accurate layout verifier"). Não há tabela de rodata por símbolo separada; a
memória é o único estado de endereço.

- **Semeadura (`seed_rodata`, `:171`).** Antes de a entry rodar, percorre `m.rodata`
  UMA vez; para cada entrada faz `mem_alloc(mem, bytes.len)` (`:176`, reserva `len`
  células no cursor bump) e `store_bytes(al.mem, al.addr, bytes)` (`:177`). Como cada
  entrada é alocada contígua a partir do cursor, a base de cada entrada = a soma dos
  comprimentos das anteriores.
- **`store_bytes` (`:188`)** grava **cada byte na sua PRÓPRIA célula** (`:193`,
  `mem_store(cur, base + i, bytes[i] to i128)`) — byte-por-célula, o modelo que casa
  com um `load.i8` de string (F14).
- **`rodata_base_of` (`:206`)** re-deriva a base de um símbolo somando os comprimentos
  em bytes das entradas anteriores (`:212`) — a MESMA soma cumulativa que `seed_rodata`
  faz, então as duas sempre concordam. É o "endereço virtual" (índice de célula da
  primeira célula) do blob daquele símbolo. Devolve `RodataHit { ok; addr }` (`:201`).
- **`LGlobalAddr` (`interp_global_addr`, `:529`)** resolve `g.symbol` →
  `rodata_base_of(m.rodata, g.symbol).addr` e guarda esse índice de célula no VReg
  resultado; símbolo desconhecido = erro interno (`:531`).

### 1.2 O typed load do crumb 6 (LFieldAddr/LLoad sobre agregado rodata) — um load lê UMA célula

- **`LFieldAddr` (`interp_field_addr`, `:497`)** produz `base_value + fa.offset` — o
  offset é aritmética de índice de célula (byte-offset ao qual a célula responde),
  `:500`.
- **`LLoad` (`interp_load`, `:505`)** lê **UMA célula** no endereço em `ld.addr`
  (`mem_load(mem, a.value)`, `:508`) e guarda o valor no VReg. Um único `load` devolve
  o conteúdo de UMA célula.
- **Consequência para agregados rodata (crumb 6).** Um const Tier-A serializado por
  `serialize_const` (`lower.tks:5137`) tem os seus bytes semeados byte-por-célula. Um
  campo é lido por `LGlobalAddr`(base do blob) → `LFieldAddr`(byte-offset do campo) →
  `LLoad`. Como o load lê UMA célula, ele devolve o BYTE naquele offset. Isto é exato
  para `[]byte`/`str` (F14: `"abc"[0]` → célula do byte 0 → 97) e para os campos POD do
  corpus atual, cujos VALORES cabem num byte (`MReg{id=0…}`, tags de enum 0/1, bools) —
  a limitação de multi-byte-escalar é tolerada de propósito pelo oráculo ("not a
  byte-accurate layout verifier"). **O ponto decisivo para T-B5: o load de um ponteiro
  é EXATAMENTE este padrão — um único `LLoad` numa célula.**

> Contraste (para não confundir): um agregado em `alloca` (F10/F12) grava o VALOR
> inteiro numa célula via `LStore` no offset do campo (`:515`, uma célula), então
> load-de-alloca devolve o valor cheio; rodata é semeado byte-por-célula. As duas
> memórias coexistem no mesmo `MemStore`; a diferença é só como foram preenchidas.

### 1.3 O que acontece HOJE se um slot de ponteiro fosse lido (lixo zero)

`seed_rodata` (`:171-181`) **IGNORA `m.rodata[i].relocs`** — semeia só `bytes`. Um
const Tier-B (produzido só a partir de T-B6) traria um slot de ponteiro de 8 bytes
zero DENTRO dos seus bytes. Semeado byte-por-célula, esse slot vira 8 células ZERO. Um
`LLoad` subsequente do ponteiro leria a célula do slot = **0**, e um deref
(`field_addr(0, k)` → `load`) leria a célula 0 = o primeiro byte do PRIMEIRO datum
rodata — um **miscompile silencioso** (o ponteiro apontaria para o início da rodata, não
para o alvo). Nada hoje protege esse caminho na VM (`encode_rodata`/`honest_data_reloc`
é do caminho NATIVO; a VM consome `LModule` direto, como o wasm). T-B5 preenche esse
slot com o endereço do alvo na semeadura, tornando o miscompile impossível.

---

## 2. Design da resolução — PRÉ-RESOLVER na semeadura (um lugar, zero custo por load)

### 2.1 Decisão law-first: pré-resolver em `seed_rodata`, NÃO por load

**RECOMENDAÇÃO (law-first): pré-resolver na carga do módulo (em `seed_rodata`), não
por load.** Justificativa pelas Leis:

1. **Espelha T-B4 e os writers nativos — "um lugar só, zero custo por load".** T-B4
   resolve o ponteiro data→data UMA vez, no layout do data segment
   (`wasm_relocate_rodata`); os writers ELF/Mach-O/COFF aplicam a relocation UMA vez na
   emissão. A VM já tem o análogo perfeito: `seed_rodata` constrói a imagem rodata UMA
   vez antes de a entry rodar. Resolver ali é o mesmo idioma "recompute-don't-thread"
   que a VM já usa (`rodata_base_of` re-deriva a base a cada `LGlobalAddr`; aqui
   re-derivamos o alvo UMA vez na semeadura). **Smallest safe step.**
2. **Resolver por load é errado e caro.** `interp_load` (`:505`) não sabe — nem pode
   saber barato — que a célula que lê é um slot de ponteiro relocável; teria de threadar
   a lista de relocs de todo blob por cada load, e distinguir "célula é slot" de "célula
   é dado". Isso espalha a decisão por um hot path e abre superfície de miscompile.
   Pré-resolver confina tudo a UM ponto e deixa `interp_load` intocado.
3. **A memória JÁ é o estado resolvido.** `seed_rodata` já pré-carrega os bytes; anexar
   um segundo passo que grava os endereços-alvo é a extensão natural — a memória
   semeada passa a ser a imagem final (bytes + ponteiros resolvidos), exatamente como a
   imagem nativa pós-relocation.

### 2.2 Como a VM SABE que um slot é ponteiro interno — lê `LRodata.relocs` (não infere)

A VM **não infere** nada dos bytes: lê `m.rodata[i].relocs` (a lista
`LDataReloc { offset; target }`, `lir.tks:154`), o carrier LIR que T-B1 anexou a cada
`LRodata`. É o MESMO carrier que o wasm consome direto em T-B4 (§2.1 do T-B4: "O wasm
consome `LRodata.relocs` DIRETAMENTE (nível LIR)"). A VM, como o wasm, não usa
`Reloc*`/`RelocSect` (esses são dos writers nativos) — consome o `LDataReloc` da LIR.

### 2.3 A representação do endereço gravado — índice de célula do alvo (o que um LLoad de ptr dereferencia)

O valor gravado no slot tem de ser exatamente o que um `LLoad` subsequente do ponteiro
devolve e um deref posterior usa como base. Na VM um endereço É um índice de célula
(§1.1), e um `LLoad` lê UMA célula (§1.2). Logo:

- Para a entrada `i` com base `B_i` (= `rodata_base_of(entry_i.symbol).addr`, a soma
  cumulativa), e uma reloc `LDataReloc { offset; target }`, o slot ocupa as células
  `[B_i + offset, B_i + offset + 8)` (8 bytes zero). A VM **sobrescreve a célula
  `B_i + offset`** com `rodata_base_of(m.rodata, target).addr` — o índice de célula da
  primeira célula do datum-alvo.
- Um `LLoad` de `field_addr(global_addr(entry_i.symbol), offset)` lê exatamente a célula
  `B_i + offset` → devolve o índice de célula do alvo → um deref
  `field_addr(ptr, k)` → `load` lê a célula `alvo + k` = o k-ésimo byte do alvo.
  **Correto.**
- As 7 células altas do slot (`B_i + offset + 1 .. +7`) permanecem 0 — o ponteiro é lido
  por UM load de UMA célula, então elas nunca são lidas como parte do ponteiro. A largura
  de 8 bytes do slot (paridade nativa/wasm) é irrelevante para a VM: a VM usa a célula
  baixa como o ponteiro inteiro, análoga ao word baixo i32 que o wasm32 grava (T-B4
  §2.2).

### 2.4 Assinaturas exatas a adicionar (W15 Javadoc, copiar verbatim)

Colocadas em `lir_interp.tks` logo após `store_bytes` (`:197`) e antes de `RodataHit`
(`:199`). Reusam `rodata_base_of` (`:206`) e `mem_store` (`:137`), ambos já no arquivo.

```teko
/**
 * resolve_entry_relocs — grava, para uma única entrada rodata cujo blob começa na
 * célula `base`, o endereço-alvo de cada uma das suas relocations internas de ponteiro
 * na célula do slot correspondente (#594 T-B5). Para cada `LDataReloc { offset;
 * target }` o valor gravado é `rodata_base_of(rodata, target).addr` — o índice de
 * célula (o "endereço" na memória da VM) da primeira célula do datum-alvo — na célula
 * `base + offset`, que é a célula que um `LLoad` subsequente do ponteiro lê. Um
 * `target` que não nomeia nenhuma entrada rodata é um erro interno NOMEADO (não um skip
 * silencioso): a lowering só emite um `LDataReloc` para um símbolo que ela mesma
 * internou, então um miss é um bug de compilador, e devolvê-lo como erro mantém o
 * miscompile impossível (espelha o erro de alvo desconhecido do `wasm_data_addr_of`,
 * T-B4). O slot de 8 bytes ocupa as células `[base+offset, base+offset+8)`; só a célula
 * baixa recebe o endereço (a VM lê o ponteiro por um load de UMA célula), as altas
 * permanecem zero.
 *
 * @param MemStore mem  a memória com os bytes já semeados
 * @param []LRodata rodata  a tabela rodata inteira (para resolver o `target`)
 * @param LRodata entry  a entrada cujas relocs se resolvem
 * @param i128 base  a célula-base do blob de `entry` (a soma cumulativa de comprimentos)
 * @return MemStore | error  a memória com os slots de ponteiro escritos, ou o erro
 *                           nomeado de alvo desconhecido / escrita interna
 */
fn resolve_entry_relocs(mem: MemStore, rodata: []LRodata, entry: LRodata, base: i128) -> MemStore | error {
    mut cur = mem
    mut j: u64 = 0
    loop {
        if j >= entry.relocs.len { break }
        let r = entry.relocs[j]
        let hit = rodata_base_of(rodata, r.target)
        if !hit.ok { return error { message = teko::str::concat("lir interp: rodata-internal pointer targets unknown rodata symbol @", r.target) } }
        cur = match mem_store(cur, base + (r.offset to i128), hit.addr) { MemStore as m2 => m2; error as e => return e }
        j++
    }
    cur
}

/**
 * resolve_rodata_relocs — depois de `seed_rodata` ter posto cada blob rodata
 * byte-por-célula, sobrescreve cada slot de ponteiro rodata-INTERNO com o endereço (o
 * índice de célula) do datum-alvo (#594 T-B5). Percorre `rodata` re-derivando a base
 * cumulativa de cada entrada — a MESMA soma que `seed_rodata`/`rodata_base_of` fazem,
 * então concordam sempre — e delega a resolução dos slots daquela entrada a
 * `resolve_entry_relocs`. É o análogo VM do `wasm_relocate_rodata` (T-B4) e das
 * relocations `.rela.rodata`/`__const`/`.rdata` dos writers nativos (T-B2/T-B3): a
 * relocation aplicada UMA vez na carga do módulo, custo zero por load. Toda entrada tem
 * `relocs` VAZIO até T-B6 (o produtor `serialize_const` honest-stopa agregados
 * pointer-bearing), então isto reescreve nada e a memória fica byte-idêntica.
 *
 * @param MemStore mem  a memória com os bytes já semeados por `seed_rodata`
 * @param []LRodata rodata  a tabela rodata inteira, na ordem de semeadura
 * @return MemStore | error  a memória com todos os slots de ponteiro resolvidos, ou o
 *                           erro nomeado propagado
 */
fn resolve_rodata_relocs(mem: MemStore, rodata: []LRodata) -> MemStore | error {
    mut cur = mem
    mut base: i128 = 0 to i128
    mut i: u64 = 0
    loop {
        if i >= rodata.len { break }
        cur = match resolve_entry_relocs(cur, rodata, rodata[i], base) { MemStore as m2 => m2; error as e => return e }
        base = base + (rodata[i].bytes.len to i128)
        i++
    }
    cur
}
```

`seed_rodata` (`:171`) passa a devolver `MemStore | error` e a encadear a resolução no
fim (o corpo de semeadura atual fica intocado — o passo 2 é anexado):

```teko
/** seed_rodata — a fresh memory with every entry of `m.rodata` pre-loaded, ONE CELL PER
    BYTE, in table order, THEN every rodata-internal pointer slot resolved to its
    target's base cell (#594 T-B5) — `rodata_base_of` recomputes the same cumulative
    offsets from `m.rodata` (a STATIC table), so no separate address-table threads
    through the interpreter. Every entry's `relocs` is empty until T-B6, so the resolve
    pass rewrites nothing and the seeded memory is byte-identical (A1-4, #382; T-B5). */
fn seed_rodata(m: LModule) -> MemStore | error {
    mut mem = empty_memstore()
    mut i: u64 = 0
    loop {
        if i >= m.rodata.len { break }
        let al = mem_alloc(mem, m.rodata[i].bytes.len to u32)
        mem = store_bytes(al.mem, al.addr, m.rodata[i].bytes)
        i++
    }
    resolve_rodata_relocs(mem, m.rodata)
}
```

`interp_lmodule` (`:157`) mapeia o novo erro ao sentinela negativo do oráculo (o mesmo
`-1` que uma função ausente ou um op fora do subset já produzem, `:159/163`):

```teko
    let mem = match seed_rodata(m) { MemStore as x => x; error => return -1 to i128 }
    let r = interp_call(m, f.func, args, 0, mem)
```

> **Por que `seed_rodata` fica fallível** e `resolve_*` propagam erro em vez de skip
> silencioso: um `target` desconhecido é um bug interno de compilador (a lowering só
> emite `LDataReloc` para símbolos que internou), e a Lei "nunca um skip silencioso"
> (o padrão de `wasm_data_addr_of`/`func_slot_addr`) manda falhar nomeadamente. Como
> `relocs` é vazio hoje, o braço de erro é INALCANÇÁVEL e o `match` sempre toma o braço
> `MemStore` → comportamento byte-idêntico (§3).

---

## 3. Consistência VM≡native (Lei D4 / dual-engine) — o valor resolvido é o análogo semântico do endereço absoluto

O mesmo programa Tier-B tem de se comportar idêntico na VM e no nativo/wasm. Prova de
que o índice de célula que a VM grava é o análogo semântico do endereço absoluto:

1. **O que cada engine grava no slot.**
   - Nativo (T-B2/T-B3): o endereço ABSOLUTO de 64 bits do datum-alvo = `section_base(.rodata/__const/.rdata) + offset_do_alvo` (o linker resolve via símbolo de seção + addend, ou UNSIGNED extern-to-local).
   - wasm32 (T-B4): o endereço ABSOLUTO de memória linear = `wasm_rodata_base() + offset_do_alvo` (constante i32 de emit-time).
   - VM (T-B5): `rodata_base_of(target).addr` = `soma_cumulativa_até_o_alvo` = a célula-base do alvo na memória da VM.
   - **Os três são a MESMA quantidade semântica:** "o endereço, no espaço de endereços daquela engine, da primeira unidade do datum-alvo." A VM tem base 0 e passo 1-célula-por-byte; o nativo tem `section_base` e passo 1-byte; o wasm tem `wasm_rodata_base()`. A base e a escala diferem, mas o REFERENTE é idêntico.
2. **O ponteiro é OPACO — só observável por deref.** Um const Tier-B (descritor ABI: `{ptr,len}` de `[]u32`) NUNCA expõe o valor numérico do ponteiro (não o compara, não o imprime); o único uso é dereferenciar (`load ptr` → endereço → `field_addr`+`load` → o u32 do array). Logo o valor numérico absoluto é irrelevante para o comportamento observável; só importa que o deref alcance os MESMOS bytes-alvo.
3. **O deref alcança os mesmos bytes nas duas engines.** Nativo: `load` do slot → endereço absoluto do alvo → `load` através dele → os bytes do alvo. VM: `LLoad` da célula do slot → `rodata_base_of(target)` → `field_addr`+`LLoad` → a célula do alvo → os mesmos bytes (semeados byte-por-célula a partir da MESMA imagem `serialize_const`). Como o exit code depende só dos bytes dereferenciados, **o exit code é idêntico nas duas engines.** QED (D4).

> Nota de largura (harmoniza com T-B4 §2.2): o slot é 8 bytes na imagem; o wasm32 usa o
> word baixo i32, a VM usa a célula baixa, o nativo usa os 8 bytes. As três leem o
> ponteiro pela unidade baixa; nenhuma engine observa as unidades altas do slot. A
> decisão de largura do serializer (T-B6) não afeta nenhuma delas.

---

## 4. Ponto 4 — abrir o honest-stop de `encode_rodata` NESTE crumb (decisão law-first: SIM)

### 4.1 Decisão: `encode_rodata` ABRE em T-B5; `serialize_const` (produtor) fica o único gate até T-B6

**RECOMENDAÇÃO (law-first): abrir `encode_rodata` (e o bridge de partição ELF) em
T-B5, junto da resolução da VM.** Não é opcional — é EXIGIDO pela regra de bootstrap,
e é o que faz T-B5 ser "o último elo antes do 🔑 SEED BUMP #3". Prova:

1. **A regra de bootstrap-seed obriga.** "O corpus não pode USAR uma feature que ainda
   não está no seed." T-B6 USA a capability data→data (introduz consts Tier-B reais —
   os descritores ABI — no PRÓPRIO source do compilador). Para T-B6 ser
   compilável/self-hosting APÓS o BUMP #3, a cadeia CONSUMIDORA inteira tem de estar no
   seed NO bump. O bump é depois de T-B5. Os writers (T-B2/T-B3), o wasm (T-B4) e a VM
   (T-B5) já entram. O ÚNICO elo consumidor que falta no caminho NATIVO é o **encoder
   bridge**: `encode_rodata` produzir `Reloc sect=Rodata` a partir de `LRodata.relocs`,
   e o bridge ELF (`x86_reloc_reqs`/`riscv_reloc_reqs`) particionar por `.sect`
   (Mach-O/COFF já particionam, T-B3). Se esse bridge ficasse em T-B6, o seed do bump
   NÃO o teria → quando o seed compilasse o source Tier-B de T-B6, `encode_rodata`
   honest-stoparia no primeiro const Tier-B → **quebra de self-hosting**. Logo o bridge
   TEM de abrir em T-B5.
2. **O brief confirma a moldura:** "DEPOIS de T-B5 a cadeia consumidora está completa."
   Se `encode_rodata` só abrisse em T-B6, a cadeia NÃO estaria completa após T-B5 —
   contradição. E §8: "🔑 SEED BUMP #3 after T-B5 — the data-reloc capability is now in
   the seed." Capability completa após T-B5 ⇒ o encoder abre em T-B5.
3. **Nada quebra — byte-idêntico HOJE.** O PRODUTOR `serialize_const` (`lower.tks:5142`)
   continua honest-stopando agregados pointer-bearing, então todo `LRodata.relocs` é
   vazio em compilação real. `encode_rodata` produz então ZERO `Reloc sect=Rodata`, a
   partição rodata dos writers fica vazia, e cada objeto sai byte-idêntico (a prova é a
   MESMA dos goldens T-B2/T-B3/T-B4 + fixpoint). O gate se move do CONSUMIDOR
   (`encode_rodata`) para o PRODUTOR (`serialize_const`), que fica o único fechado até
   T-B6.

### 4.2 Shapes do encoder bridge (os três encoders + o bridge ELF)

**(a) `ModuleRodata` ganha `relocs` e `encode_rodata` produz em vez de honest-stopar.**
Hoje `encode_rodata` (`encode_arm64.tks:2679`) honest-stopa via `honest_data_reloc`
(`:2645`) + `rodata_has_internal_relocs` (`:2658`). Em T-B5 esses DOIS helpers são
REMOVIDOS (scaffolding de T-B1 cujo trabalho terminou) e `encode_rodata` emite um
`Reloc`/`RelocX86`/`RelocRiscv` por `LDataReloc`, com o offset re-baseado ao offset
corrente do blob dentro do `.rodata`/`__const` concatenado:

```teko
/**
 * rodata_reloc_of — o `Reloc` data→data para um `LDataReloc` de um blob rodata cuja
 * base corrente no `__const` concatenado é `blob_off` (#594 T-B5): o patch site fica no
 * offset `.rodata`-relativo `blob_off + r.offset`, aponta para o símbolo do datum-alvo
 * `r.target`, e usa o kind absoluto de 64 bits `Abs64` com `sect = Rodata`. O writer
 * (T-B3 Mach-O `macho_partition_relocs`) resolve o alvo pelo seu símbolo local
 * section-2; nada a dobrar aqui (addend implícito 0).
 *
 * @param LDataReloc r  a relocation interna do blob
 * @param u32 blob_off  o offset corrente do blob dentro do `__const` concatenado
 * @return Reloc  a relocation data→data, `Rodata`-tagueada
 */
fn rodata_reloc_of(r: lir::LDataReloc, blob_off: u32) -> Reloc {
    Reloc { offset = blob_off + r.offset; sym = r.target; kind = MRelocKind::Abs64; sect = RelocSect::Rodata }
}
```

`ModuleRodata` (`encode_arm64.tks:2605`) ganha um campo `relocs: []Reloc`; o loop de
`encode_rodata` acumula `relocs = append_rodata_relocs(relocs, rd.relocs, off)` no ponto
`off` de cada blob, e a assinatura simplifica para `-> ModuleRodata` (total — não há mais
honest-stop; os três callers `encode_module` largam o `match`):

```teko
fn encode_rodata(rodata: []lir::LRodata) -> ModuleRodata {
    mut bytes: []byte = teko::list::empty()
    mut syms: []Symbol = teko::list::empty()
    mut relocs: []Reloc = teko::list::empty()
    mut off: u32 = 0
    mut i: u64 = 0
    loop {
        if i >= rodata.len { break }
        let rd = rodata[i]
        syms = teko::list::push(syms, Symbol { name = rd.symbol; defined = true; sect = 2 to u8; offset = off; local = true })
        relocs = append_rodata_relocs(relocs, rd.relocs, off)
        bytes = append_bytes(bytes, rd.bytes)
        off = off + (rd.bytes.len to u32)
        i++
    }
    ModuleRodata { bytes = bytes; syms = syms; relocs = relocs }
}
```

`encode_module` (`encode_arm64.tks:2774`) concatena as relocs de rodata na lista ÚNICA
tagueada do módulo (o modelo T-B1/T-B2/T-B3: uma lista, tagueada por `.sect`):

```teko
    let rod = encode_rodata(m.rodata)
    ...
    EncodedModule { text = mt.text; rodata = rod.bytes; symbols = symbols; relocs = append_relocs(mt.relocs, rod.relocs) }
```

> x86 (`encode_x86_64.tks`) e riscv (`encode_riscv.tks`) espelham: `ModuleRodataX86`/
> `ModuleRodataRiscv` ganham `relocs: []RelocX86`/`[]RelocRiscv`; `rodata_reloc_of`
> usa `RelocKindX86::Abs64` (addend `0`) / `RelocKindRiscv::Abs64`; `EncodedModuleX86`/
> `EncodedModuleRiscv.relocs` recebem `append_relocs(mt.relocs, rod.relocs)`. Os kinds
> `Abs64` já existem para os três (arm64 ganhou em T-B3; x86/riscv desde sempre).

**(b) Bridge ELF particiona por `.sect`.** `x86_reloc_reqs` (`objfile_elf.tks:1083`) e
`riscv_reloc_reqs` (`objfile_elf_riscv.tks:15`) hoje mapeiam TODA reloc → `relocs` e
setam `rodata_relocs = teko::list::empty()` (`:1116`/`:51`). Em T-B5 particionam por
`r.sect`: um `Text` → `relocs` (o offset é `.text`-relativo, como hoje), um `Rodata` →
`rodata_relocs` (o offset é `.rodata`-relativo). Como toda reloc é `Text` hoje,
`rodata_relocs` fica vazia e o objeto é byte-idêntico. Mach-O (`emit_macho` →
`macho_partition_relocs`) e COFF (`emit_coff` → `coff_partition_relocs`) JÁ particionam
(T-B3), então recebem os `Rodata` relocs automaticamente assim que `encode_module` os
produz — sem edição de writer.

### 4.3 Prova de que os goldens continuam intactos

`serialize_const` honest-stopa pointer-bearing (`lower.tks:5142`, INTOCADO), então em
TODA compilação real `LRodata.relocs` é vazio ⇒ `encode_rodata` produz `relocs` vazio ⇒
`append_relocs(mt.relocs, [])` = `mt.relocs` (idêntico) ⇒ a partição ELF/Mach-O/COFF põe
tudo em `text`/`relocs` e a partição rodata vazia ⇒ `.rela.rodata`/`__const`/`.rdata`
relocs vazios ⇒ cada objeto byte-idêntico. Os goldens `encode_*_test.tkt`,
`objfile_{elf,elf_riscv,macho,coff}_test.tkt` + **fixpoint gen1==gen2** são a prova. As
únicas fixtures que MUDAM de expectativa são as três de T-B1 (§5, "o gate dispara") que
assertavam o honest-stop `encode_module*` — agora invertem para "produz a `Rodata`
reloc" (§5.7 abaixo).

---

## 5. Fixtures a ADICIONAR (campo-a-campo)

### 5.1 (VM) resolve grava o endereço do alvo — deref end-to-end lê o byte-alvo

`lir_interp_test.tkt`, construindo `LModule` à mão. Helper novo `iwt_rodata_rel`
(espelho do `iwt_one_rodata` `:431`, mas com relocs):

```teko
/**
 * iwt_rodata_rel — uma entrada rodata com relocations internas de ponteiro, o fixture
 * dos testes de resolução da VM (#594 T-B5).
 *
 * @param str symbol  o símbolo da entrada
 * @param []byte bytes  os bytes crus (o slot de ponteiro zero-init incluso)
 * @param []LDataReloc relocs  as relocations internas
 * @return LRodata  a entrada
 */
fn iwt_rodata_rel(symbol: str, bytes: []byte, relocs: []LDataReloc) -> LRodata {
    LRodata { symbol = symbol; bytes = bytes; relocs = relocs }
}
```

Programa: entrada `"p"` = slot de 8 bytes zero + reloc(offset 0 → `"t"`); entrada `"t"`
= `[0x41]`. Bases: `"p"`@0 (len 8), `"t"`@8. `main`: `global_addr("p")` →
`field_addr(0)` → `load` (lê célula 0 = o ponteiro resolvido = 8) → `field_addr(ptr,0)`
→ `load.i8` (lê célula 8 = 0x41) → `zext` → `exit`. Espera **exit 65** (0x41).

```teko
#test
fn iwt_tb5_data_pointer_derefs_to_the_target_byte() {
    let slot = [0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte]
    let rodata = teko::list::push(teko::list::push(teko::list::empty(), iwt_rodata_rel("p", slot, teko::list::push(teko::list::empty(), data_reloc(0 to u32, "t")))), iwt_one_rodata("t", "A")[0])
    // (main hand-built: global_addr "p" -> field_addr 0 -> load ptr -> field_addr ptr,0 -> load.i8 -> zext -> exit)
    let mdl = iwt_tb5_deref_module(rodata)
    teko::assert::is_true(interp_lmodule(mdl, "main", iwt_no_args()) == iwt_i128(65))
}
```

### 5.2 (VM) unitário `resolve_rodata_relocs` — a célula do slot recebe o índice do alvo

Semeia via `seed_rodata` e assere campo-a-campo: célula 0 == 8 (base de `"t"`), células
1..7 == 0, célula 8 == 0x41. Prova o valor gravado sem passar pelo interpretador de
instruções.

### 5.3 (VM) base cumulativa não-trivial — o alvo depois de um datum de padding

Três entradas: `"pad"` `[0xAA,0xBB]` (len 2), `"p"` slot@base 2 reloc→`"t"`, `"t"`@base
10. Assere que a célula `2` (slot baixo) == 10 após resolve (o endereço inclui a soma
cumulativa), e o deref lê a célula 10. Prova que a base não é hard-zero.

### 5.4 (VM) colapso byte-idêntico quando `relocs` vazio

Mesmas duas entradas SEM relocs: assere que a célula do slot permanece 0 (nada gravado)
e que o módulo semeia/roda idêntico ao pré-T-B5 (`iwt_f13`/`iwt_f14` existentes já
cobrem rodata sem relocs; este teste fixa o contraste explícito com §5.1).

### 5.5 (VM) alvo desconhecido → run falha com o sentinela negativo (nunca miscompile silencioso)

Reloc target `"nope"`: `interp_lmodule` devolve **-1** (o sentinela; `seed_rodata` erra,
`interp_lmodule` mapeia). Prova que um alvo inválido nunca vira um ponteiro-lixo.

```teko
#test
fn iwt_tb5_unknown_target_fails_the_run() {
    let slot = [0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte]
    let rodata = teko::list::push(teko::list::empty(), iwt_rodata_rel("p", slot, teko::list::push(teko::list::empty(), data_reloc(0 to u32, "nope"))))
    let mdl = iwt_tb5_deref_module(rodata)
    teko::assert::is_true(interp_lmodule(mdl, "main", iwt_no_args()) < iwt_i128(0))
}
```

### 5.6 (VM) `rodata_base_of` inalterado

Assere que `rodata_base_of` continua devolvendo as bases cumulativas corretas (guard de
regressão do helper que a resolução reusa) — reafirma o comportamento existente.

### 5.7 (encoder bridge) o honest-stop INVERTE + a `Rodata` reloc é produzida

- **Inverter T-B1 §5-fixture 1** em `encode_x86_64_test.tkt`, `encode_arm64_test.tkt`,
  `encode_riscv_test.tkt`: o mesmo `LModule` à mão com `LRodata{ bytes=<8 zero>;
  relocs=[data_reloc(0,"other")] }` que ANTES assertava o erro `honest_data_reloc` agora
  assere que `encode_module*` devolve um `EncodedModule*` cujo `relocs` contém UMA
  `Reloc`/`RelocX86`/`RelocRiscv` com `sect == RelocSect::Rodata`, `kind == Abs64`,
  `sym == "other"`, `offset == 0`.
- **End-to-end encoder→writer**: um `LModule` de 2 entradas (slot + alvo) →
  `encode_module_x86` → `emit_elf` → assere a linha `.rela.rodata` (os MESMOS bytes que
  a fixture T-B2 §4.1 fixou à mão, agora dirigidos pelo encoder). Espelho arm64→`emit_macho`
  (a `__const` reloc `0x0E000001` de T-B3 §4.1) e x86→`emit_coff` (a `.rdata` reloc de
  T-B3 §4.3). Prova que a cadeia produtor-de-reloc→writer encadeia.
- **Partição ELF unitária**: `x86_reloc_reqs`/`riscv_reloc_reqs` sobre um `enc.relocs`
  com uma `Text` + uma `Rodata` devolve `relocs.len==1` (a text) e `rodata_relocs.len==1`
  (a rodata); sobre só-`Text` devolve `rodata_relocs` vazia (o braço byte-idêntico).

### 5.8 (dual-engine) espelho do programa nas duas engines

Todas as fixtures §5.1–§5.6 são testes unitários de `interp_lmodule`/backend chamados via
`.tkt`, então **rodam idênticas na VM e no harness nativo** (mesma saída de `error`/exit)
— isso já satisfaz o requisito dual-engine ao nível LIR/encoder. O espelho SOURCE-level
verdadeiro (um `const AAPCS64: AbiDescriptor = …` lido na VM E no nativo produzindo o
MESMO exit) exige o PRODUTOR (`serialize_const` deixar de honest-stopar) — **isso é T-B6,
bloqueado atrás do 🔑 SEED BUMP #3** (§8). Até lá, o both-engine é provado ao nível de
`LModule`/`EncodedModule` à mão, que é o análogo exato do padrão `co_abs64_module` /
fixtures T-B2/T-B3/T-B4.

> Cobertura do delta: §5.1/§5.3 o braço com-reloc (deref end-to-end, base não-trivial);
> §5.2 o valor gravado; §5.4 o colapso vazio; §5.5 o erro nomeado; §5.6 o helper reusado;
> §5.7 os três braços do encoder bridge + a partição ELF (hit/miss). 100% do delta.

---

## 6. Sequência de edits + riscos + o 🔑 BUMP #3 (ritual de fechamento)

Cada edit é gate-able isoladamente; rodar o `.tkt` listado após cada um. Ordem tal que o
arquivo compila a cada passo.

| # | Arquivo | Edit | Gate de regressão (`.tkt`) |
|---|---|---|---|
| E1 | `src/lir/lir_interp.tks` | `resolve_entry_relocs` + `resolve_rodata_relocs` (§2.4), após `store_bytes` (`:197`) | `lir_interp_test.tkt` compila |
| E2 | `src/lir/lir_interp.tks` | `seed_rodata -> MemStore \| error` + passo de resolução (§2.4); `interp_lmodule` mapeia erro→`-1` (`:160`) | `lir_interp_test.tkt` (F13/F14 e todos os goldens de VM byte-idênticos, relocs vazio) |
| E3 | `src/lir/lir_interp_test.tkt` | helper `iwt_rodata_rel` + `iwt_tb5_deref_module` + fixtures §5.1–§5.6 | os próprios testes novos (verde) |
| E4 | `src/backend/encode_arm64.tks` | remover `honest_data_reloc`+`rodata_has_internal_relocs`; `ModuleRodata.relocs`+`rodata_reloc_of`+`append_rodata_relocs`; `encode_rodata -> ModuleRodata`; `encode_module` concatena (§4.2) | `encode_arm64_test.tkt`, `objfile_macho_test.tkt` (goldens byte-idênticos) |
| E5 | `src/backend/encode_x86_64.tks` | espelho x86 (`RelocX86::Abs64`, addend 0) | `encode_x86_64_test.tkt`, `objfile_coff_test.tkt`, `objfile_elf_test.tkt` |
| E6 | `src/backend/encode_riscv.tks` | espelho riscv (`RelocRiscv::Abs64`) | `encode_riscv_test.tkt`, `objfile_elf_riscv_test.tkt` |
| E7 | `src/backend/objfile_elf.tks` + `objfile_elf_riscv.tks` | `x86_reloc_reqs`/`riscv_reloc_reqs` particionam por `.sect` em `relocs`/`rodata_relocs` (§4.2b) | `objfile_elf_test.tkt`, `objfile_elf_riscv_test.tkt` (byte-idênticos) |
| E8 | `encode_{x86_64,arm64,riscv}_test.tkt` | inverter as 3 fixtures T-B1 §5-1 (honest-stop→reloc produzida) + as end-to-end §5.7 | os próprios testes |

> Mach-O/COFF writers **NÃO são tocados** — `macho_partition_relocs`/`coff_partition_relocs`
> (T-B3) já leem `enc.relocs` por `.sect`; recebem os `Rodata` relocs assim que E4/E5 os
> produzem. `serialize_const` (`lower.tks:5142`) **fica INTOCADO** — é o gate produtor
> até T-B6.

**Ritual points:**

- **Por-edit:** o `.tkt` do arquivo (tabela) — cada edit é gate-able só.
- **RITUAL POINT — fim de T-B5:** gate COMPLETO — todos os goldens de VM
  (`lir_interp_test.tkt`) e de backend (`encode_*_test.tkt`, `objfile_*_test.tkt`,
  `lower_test.tkt`, `tkb_test.tkt`) byte-idênticos + **fixpoint gen1==gen2** + ambas as
  engines (VM + nativo) + 100% de cobertura do delta (§5). Zero bytes mudam (produtor
  ainda fechado), então um gate verde É a prova de compatibilidade.
- **🔑 SEED BUMP #3 (0.3.0.25) — DEPOIS do gate de T-B5.** A capability data→data está
  completa no seed (encoders nativos + writers ELF/Mach-O/COFF + wasm + VM). Isto
  DESBLOQUEIA T-B6: o source do compilador pode passar a usar consts agregados
  pointer-bearing (os descritores ABI). O bump é o ritual de fechamento do crumb; segue
  o mecanismo `-beta` de D36.

## 7. Riscos + tensões (com resolução)

1. **Miscompile silencioso: `seed_rodata` hoje IGNORA `relocs`.** É a razão de T-B5
   existir. *Resolução:* a resolução real (§2.4) + o erro nomeado de alvo desconhecido
   (`resolve_entry_relocs`) tornam impossível um slot ficar zerado/apontando errado; a
   fixture §5.5 fixa o `-1`. Nunca um skip silencioso.

2. **`seed_rodata` muda de assinatura (`-> MemStore \| error`) — path byte-crítico.**
   *Resolução:* é privada, um único caller (`interp_lmodule:160`); o braço de erro é
   INALCANÇÁVEL hoje (relocs vazio), então o `match` sempre toma `MemStore` e o
   comportamento é byte-idêntico; os goldens F13/F14 + fixpoint provam.

3. **O modelo célula-por-byte vs a largura de 8 bytes do slot.** Um erro aqui gravaria
   o endereço na célula errada. *Resolução:* a VM lê o ponteiro por UM `LLoad` de UMA
   célula (§1.2), então gravar na célula baixa `base+offset` é exatamente o que o load lê
   (§2.3); as 7 células altas ficam zero e nunca são lidas. §5.2 assere célula-a-célula.

4. **Abrir `encode_rodata` amplia o crumb além da VM.** *Soft tension, não HALT.* É
   EXIGIDO pela regra de bootstrap (§4.1): o BUMP #3 depois de T-B5 tem de carregar a
   cadeia consumidora completa, e o encoder bridge é o único elo nativo faltante. É
   byte-seguro (produtor fechado) e testável à mão hoje (§5.7). Confinar o bridge a T-B6
   quebraria o self-hosting de T-B6. Resolvido: abre em T-B5.

5. **Inverter as 3 fixtures T-B1 (honest-stop → reloc).** *Resolução:* é a evolução
   esperada do gate — T-B1 fixou o honest-stop como o placement seam que "o crumb que
   emite" substitui (T-B1 §2.5); T-B5 é esse crumb. As fixtures invertidas (§5.7) provam
   a produção da reloc; os goldens de writer (T-B2/T-B3) já provam os bytes emitidos.

6. **Análogo-semântico VM vs endereço absoluto nativo.** *Resolução (não é gap):* provado
   §3 — índice de célula, endereço de memória linear e endereço absoluto são o MESMO
   referente ("a base do datum-alvo") em espaços de endereço diferentes; o ponteiro é
   opaco (só dereferenciado), então o exit code é idêntico. D4 satisfeita.

**Sem tensão genuína não resolvida → sem HALT.**

## 8. O que permanece BLOQUEADO (adiantado até o limite)

T-B5 entrega a resolução da VM COMPLETA e o encoder bridge nativo COMPLETO, provados
hoje por `LModule`/`EncodedModule` à mão. Após o 🔑 SEED BUMP #3, o que resta (T-B6, fora
deste crumb, nomeado):

- **Popular `LRodata.relocs`** a partir de um const Tier-B real: `serialize_const`
  (`lower.tks:5137`) deixar de honest-stopar agregados pointer-bearing e emitir bytes +
  `data_reloc` entries para cada campo slice/ponteiro. É o crumb produtor (T-B6). Só
  então `resolve_rodata_relocs`/`encode_rodata`/os writers/`wasm_relocate_rodata` recebem
  `relocs` não-vazio em compilação real.
- **Migrar os descritores ABI** (`SYSV64`/`AAPCS64`/`RISCV64_LP64D`/`WIN64`, `UPPER_SNAKE`
  por D7) para consts rodata e o **espelho source-level dual-engine** (§5.8) — o programa
  Tier-B real lido idêntico na VM e no nativo. Ambos são T-B6, atrás do BUMP #3. Nada
  disso bloqueia T-B5 — a cadeia consumidora inteira está pronta e provada.
