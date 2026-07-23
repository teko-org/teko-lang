# T-B3 — writers Mach-O + COFF emitem relocations com patch site DENTRO da seção de dados (#594 Tier-B)

Status: READY-TO-IMPLEMENT (architect, 2026-07-18). Track: Tier-B pointer-bearing
aggregate → rodata (`docs/design/const-module-level-plan.md` §8 crumb T-B3, §5.1
verdict, DECISION_LOG D2). Base: o working tree da const-wave com **T-B1 e T-B2 já
mergeados** (`RelocSect`/`sect` nos três `Reloc*`, `LDataReloc`/`LRodata.relocs` na
LIR, honest-stop `honest_data_reloc` em `encode_rodata`; ELF `ElfObject.rodata_relocs`
+ `.rela.rodata` + a COSTURA CANÔNICA da lista paralela). Predecessores:
`docs/design/const-tb1-design.md`, `docs/design/const-tb2-design.md`.

> Escopo de T-B3 (verbatim §8): "Mach-O + COFF writers: emit rodata-section (`.rdata`)
> local relocations whose patch site is inside the data section." Este crumb dá aos
> writers Mach-O (arm64) e COFF (x86-64) a CAPACIDADE de emitir uma relocation cujo
> campo relocado fica DENTRO de `__const`/`.rdata` (um ponteiro data→data de um const
> Tier-B). **Nenhum produtor a exercita ainda** — o honest-stop de T-B1 em
> `encode_rodata` permanece e só abre em T-B5 (quando a cadeia encoder→writer→VM
> estiver completa). Como hoje toda relocation que chega aos writers é `sect == Text`,
> a partição rodata é sempre vazia, o objeto é **byte-idêntico** e todos os
> goldens/fixpoint ficam intactos. **encode_*/wasm/VM intocados; honest-stop
> permanece; sem seed bump** (o 🔑 SEED BUMP #3 é depois de T-B5, plano §8).

---

## 0. DECISÃO CENTRAL — a costura para Mach-O/COFF: partição interna, SEM objeto neutro novo

**PONTO A RESOLVER #3 (com prova de spec).** Os writers Mach-O e COFF **consomem
`EncodedModule` / `EncodedModuleX86` DIRETAMENTE** — `emit_macho(enc: EncodedModule)`
(`objfile_macho.tks:735`) e `emit_coff(enc: EncodedModuleX86)`
(`objfile_coff.tks:708`). **NÃO existe** `MachoObject`/`CoffObject`: ao contrário do
ELF — cujo `ElfObject` (`objfile_elf.tks:986`) é um objeto neutro porque `emit_elf`
(x86) e `emit_elf_riscv` COMPARTILHAM o writer `emit_elf_object` (duas ISAs, um só
writer) — Mach-O é arm64-only e COFF é x86-only. Cada um tem UMA ISA, então nunca
houve motivo para um objeto neutro intermediário, e por isso consomem o
`EncodedModule*` da própria ISA.

**RECOMENDAÇÃO (law-first): a costura correta é a PARTIÇÃO INTERNA por `RelocSect`, e
NÃO criar `MachoObject`/`CoffObject`.** Justificativa pelas Leis:

1. **Smallest safe step / sem scaffolding morto (Lei "issues são 100%",
   gate-abilidade por passo).** Criar `MachoObject`/`CoffObject` espelhando
   `ElfObject.rodata_relocs` introduziria um TIPO NEUTRO que existe só para carregar
   uma lista, sem o motivo que o ELF tem (compartilhamento entre ISAs). Seria código
   novo sem consumidor real — dead scaffolding. A Lei manda o menor passo seguro.

2. **A costura CANÔNICA de T-B2 é o discriminador `RelocSect` consumido no
   BRIDGE/writer, e a lista paralela é a MATERIALIZAÇÃO dessa partição no ponto do
   writer.** T-B2 §0/§6.4 ratificou: "o discriminador `RelocSect` (T-B1) é consumido
   no bridge que particiona `Reloc*`→(text, rodata)"; para ELF a materialização são
   os dois campos `ElfObject.{relocs, rodata_relocs}` porque o ELF TEM um objeto
   neutro separado. Para Mach-O/COFF, que NÃO têm objeto neutro, a mesma partição se
   materializa como **duas listas locais dentro do writer** (`text` vs `rodata`),
   produzidas por um helper nomeado a partir do único `enc.relocs` `.sect`-tagueado.
   É a MESMA semântica (uma lista `.text`/`__text`-relativa e uma lista
   rodata-relativa, com bases de offset fisicamente distintas), no MESMO ponto lógico
   (a fronteira do writer). Consistente com o ELF, sem duplicar um campo redundante no
   output do encoder.

3. **Por que NÃO adicionar `rodata_relocs` ao `EncodedModule*` (o objeto que os
   writers consomem):** o `EncodedModuleX86` alimenta TANTO o COFF QUANTO o ELF (via
   `x86_reloc_reqs`, `objfile_elf.tks:988`). O produtor T-B futuro adiciona a
   relocation data→data à lista ÚNICA `enc.relocs` com `sect = Rodata` (é o modelo
   T-B1/T-B2: uma lista, tagueada). Um campo paralelo em `EncodedModuleX86` obrigaria
   o produtor a povoar DUAS estruturas (o tag `.sect` E a lista), redundante, e o ELF
   já particiona lendo `.sect` no bridge. Manter o carrier único
   (`enc.relocs` tagueado) e particionar em cada writer é a única forma sem
   redundância nem divergência de mecanismo.

**Costura ratificada:** cada writer, no seu topo, chama
`macho_partition_relocs(enc.relocs)` / `coff_partition_relocs(enc.relocs)` que devolve
`{ text; rodata }` (particionando por `r.sect`). O caminho `.text`/`__text` existente
consome a partição `text`; o novo caminho de dados consome a partição `rodata`. Hoje
`enc.relocs` é 100% `Text` (o encoder produz só text relocs; `encode_rodata`
honest-stopa antes de qualquer `Rodata`), então a partição `rodata` é sempre vazia →
byte-identidade trivial. T-B4 (wasm) herda a MESMA partição-por-`.sect` no seu emitter.

---

## 1. Estado atual (provado com linhas)

### 1.1 Mach-O — relocations são POR SEÇÃO; hoje só `__text` as emite

**PONTO A RESOLVER #1 (prova de spec).** No formato Mach-O um objeto relocável
(`MH_OBJECT`) expressa relocations POR SEÇÃO: cada `section_64` header carrega os
campos `reloff` (file offset da tabela de `relocation_info` daquela seção) e `nreloc`
(quantidade). Não há uma tabela global — cada seção aponta para o seu próprio
sub-array. Prova no writer atual:

- `emit_section` (`objfile_macho.tks:461`) escreve `reloff`/`nreloc` como os 7º/8º
  campos do `section_64` (bytes 64/68 dos 80 do header).
- `emit_segment` (`:503`) emite `__text` com `reloff = text_reloff`/`nreloc =
  lay.nreloc` (`:517-518`), MAS emite `__const` com **`reloff = 0`, `nreloc = 0`**
  hardcoded (`:520`). Logo hoje SÓ `__text` carrega relocations; `__const` é dados
  planos sem nenhuma.
- `emit_reloc_table` (`:600`) emite UMA tabela — a de `__text` — no `reloc_offset` do
  layout, com `r_length` **hardcoded em 2** (`(2 to u32) << 25`, `:608`) = 4 bytes, e
  `r_extern` sempre 1 (`(1 to u32) << 27`). Cada entrada resolve o alvo por NOME via
  `sym_index(enc.symbols, r.sym)` (`:607`) — uma relocation EXTERNA apontando para um
  índice da tabela de símbolos.
- `compute_macho_layout` (`:313`) computa `reloc_offset = align4(seg_data_end)`,
  `reloc_end = reloc_offset + nreloc*8`, `symtab_offset = align8(reloc_end)` — um só
  bloco de relocations.

**Como o writer resolve símbolos rodata locais hoje (a base do data→data).** O encoder
arm64 emite **UM `Symbol` `defined && local`, section-2 (`__const`), por entrada
`LRodata`** (`encode_rodata`, `encode_arm64.tks:2646`). Uma referência text→rodata
(par ADRP+ADD, `MRelocKind::PageHi`/`PageLo`) vira uma relocation EXTERNA
(`r_extern=1`) cujo `r_symbolnum` é o índice DESSE símbolo local — provado por
`rodata_reloc_module_pins_symtab_and_page_relocs` (`objfile_macho_test.tkt:362`):
`PAGE21`→`s0` tem `r_symbolnum = 0` (o símbolo local `s0`, índice 0), packed
`0x3D000000`. **O Mach-O NÃO colapsa rodata num símbolo de seção** (ao contrário de
ELF `STT_SECTION` e do símbolo de seção `.rdata` do COFF): cada datum rodata tem o seu
próprio símbolo local, e as relocations são extern-to-local. **É exatamente isto que
o data→data reusa** (§2.2): a relocation com patch site em `__const` aponta
(`r_extern=1`) para o símbolo local do datum-ALVO, com o addend 0 gravado in-place no
slot de 8 bytes.

**arm64 NÃO tem `MRelocKind::Abs64`.** `MRelocKind = enum { PageHi; PageLo; Call }`
(`minst.tks:42`) — sem kind de ponteiro absoluto. `reloc_type_value`
(`objfile_macho.tks:200`) mapeia só PAGE21(3)/PAGEOFF12(4)/BRANCH26(2). **Este crumb
adiciona `Abs64` + o seu número** (T-B1 §7.4 reservou isto para T-B3).

### 1.2 COFF — cada section header tem a sua reloc table; hoje só `.text` a usa

**PONTO A RESOLVER #2 (prova de spec).** No PE/COFF cada `IMAGE_SECTION_HEADER`
carrega `PointerToRelocations` + `NumberOfRelocations`. Prova:

- `emit_coff_section` (`objfile_coff.tks:570`) escreve `PointerToRelocations`
  (`raw_ptr`→`reloc_ptr`, byte 24 do header) e `NumberOfRelocations` (byte 32).
- `emit_coff_sections` (`:593`) emite `.text` com `reloc_ptr = text_reloc_ptr`/`nreloc
  = lay.num_relocs` (`:594-595`), MAS emite `.rdata` com **`reloc_ptr = 0`, `nreloc =
  0`** hardcoded (`:597`). Logo hoje SÓ `.text` carrega relocations.
- `emit_coff_relocs` (`:676`) emite UMA tabela (a de `.text`) de `IMAGE_RELOCATION`
  de 10 bytes: `VirtualAddress`(4) + `SymbolTableIndex`(4) + `Type`(2, u16).
- `compute_coff_layout` (`:512`) computa `reloc_offset = rdata_offset + rdata_size`,
  `symtab_offset = reloc_offset + num_relocs*10` — um só bloco.
- **Símbolo de seção `.rdata`:** `coff_build_symbols` (`:220`) emite UM símbolo
  `STATIC` de seção `.rdata` (`SectionNumber=2`, `StorageClass=3`) quando há datum
  local section-2 (`:222-223`), no ÍNDICE 0; os locais rodata NÃO viram símbolos
  individuais (colapsam nesse símbolo de seção). `coff_rodata_secsym_index` (`:317`)
  devolve o índice dele.
- **Reloc type absoluto JÁ existe:** `IMAGE_REL_AMD64_ADDR64 = 0x0001`
  (`objfile_coff.tks:43`, const S5), e `coff_reloc_type(Abs64) = ADDR64` (`:341`,
  `coff_reloc_type_maps_all_kinds` já testa, `objfile_coff_test.tkt:405`). O idioma
  data→data reusa exatamente o padrão text→rodata do COFF (`coff_rodata_hit` re-aponta
  para o símbolo de seção `.rdata` + dobra o offset in-place — `coff_build_relocs`
  `:423`, `coff_apply_rodata_addends` `:453`), só que gravando o offset num slot de 8
  bytes DENTRO de `.rdata` em vez de num disp32 dentro de `.text`.

### 1.3 O honest-stop e a inércia da partição

`encode_rodata` (T-B1) honest-stopa qualquer `LRodata` com `relocs` não-vazio, então
nenhum const Tier-B chega ao encoder; `encode_functions` produz só relocations
`sect=Text`. Logo `enc.relocs` é 100% `Text` em toda compilação real, a partição
`rodata` é sempre vazia, e nenhum byte muda. As fixtures (§4) alimentam
`EncodedModule*` À MÃO — o precedente exato de `co_abs64_module`
(`objfile_coff_test.tkt:130`, "`encode_module_x86` never produces this … feeding it
directly pins the encoding") e das fixtures ELF T-B2 (`ElfObject` à mão →
`emit_elf_object`).

---

## 2. Assinaturas exatas (W15 verbatim, copiar para os implementers)

### 2.0 Prova dos reloc types (data→data, arm64 Mach-O e x86-64 COFF)

**arm64 / Mach-O:** o campo dentro de `__const` guarda o ENDEREÇO ABSOLUTO de 64 bits
de outro datum de `__const`. Pela `mach-o/arm64/reloc.h` da Apple, o tipo de
relocation para um ponteiro é **`ARM64_RELOC_UNSIGNED` = value 0**, com `r_length = 3`
(8 bytes) e `r_pcrel = 0`. Numa relocation externa UNSIGNED, o linker calcula `*slot =
n_value(símbolo) + addend_in_place`; como cada datum rodata tem o seu próprio símbolo
local (§1.1) e apontamos para o símbolo do ALVO exatamente, o addend in-place é **0**
(o slot de 8 bytes permanece zero) e `*slot = n_value(alvo)` = endereço absoluto do
datum-alvo. **Nenhum scattered reloc é necessário** (scattered é forma legada de
32-bit; arm64 usa relocations externas, e para addend 0 basta o UNSIGNED simples, sem
o par `ARM64_RELOC_ADDEND`). É o mesmo idioma extern-to-local que o writer já usa para
text→rodata, só com `r_type=0`/`r_length=3`/`r_pcrel=0` em vez de PAGE21/PAGEOFF12.

**x86-64 / COFF:** o análogo absoluto de 64 bits é **`IMAGE_REL_AMD64_ADDR64` =
0x0001** (já const, `:43`). O COFF não tem campo de addend: o offset do datum-alvo
dentro de `.rdata` é gravado IN-PLACE no slot de 8 bytes, e a relocation aponta para o
símbolo de seção `.rdata` (`StorageClass=3`); o linker calcula `*slot =
section_base(.rdata) + in_place` = endereço absoluto do alvo. É o idioma text→rodata
do COFF (§1.2) transposto para um slot de 8 bytes dentro de `.rdata`.

### 2.1 arm64 `MRelocKind::Abs64` + o seu número (`src/backend/minst.tks`, `objfile_macho.tks`)

`MRelocKind` (`minst.tks:42`) ganha o membro absoluto:

```teko
pub type MRelocKind = enum { PageHi; PageLo; Call; Abs64 }
```

> Adicionar um membro a `MRelocKind` quebra (na compilação) TODOS os `match`
> exaustivos sobre ele — é o driver mecânico. São exatamente TRÊS sites hoje
> (grep provado): `minst.tks:1552` (nome de debug), `objfile_macho.tks:202`
> (`reloc_type_value`), `objfile_macho.tks:218` (`reloc_pcrel`). Os sites de
> CONSTRUÇÃO (`isel_arm64.tks`, `encode_arm64.tks`) usam só `Call`/`PageHi`/`PageLo`
> e não precisam de arm novo. O produtor de `Abs64` é o T-B futuro
> (`encode_rodata`, atrás do honest-stop), então nenhum golden de encoder muda.

`minst.tks:1552` (nome) ganha o arm:

```teko
    match k { PageHi => "page_hi"; PageLo => "page_lo"; Call => "call"; Abs64 => "abs64" }
```

`reloc_type_value` (`objfile_macho.tks:200`) — doc atualizado + o arm
`ARM64_RELOC_UNSIGNED`:

```teko
/**
 * reloc_type_value — the ARM64 Mach-O relocation type for an `MRelocKind`:
 * `ARM64_RELOC_PAGE21`(3) for `PageHi`, `ARM64_RELOC_PAGEOFF12`(4) for `PageLo`,
 * `ARM64_RELOC_BRANCH26`(2) for `Call`, and `ARM64_RELOC_UNSIGNED`(0) for `Abs64`
 * — the 64-bit absolute-pointer fixup a rodata-INTERNAL data→data pointer owes
 * (#594 T-B3; length 3, pcrel 0). The producer of `Abs64` lands in T-B5, behind the
 * `encode_rodata` honest-stop, so no real Mach-O object carries an UNSIGNED reloc yet
 * — the arm is covered by a hand-built fixture.
 *
 * @param MRelocKind k  the relocation kind
 * @return u32  the `r_type` field value
 */
fn reloc_type_value(k: MRelocKind) -> u32 {
    match k {
        PageHi => 3 to u32
        PageLo => 4 to u32
        Call => 2 to u32
        Abs64 => 0 to u32
    }
}
```

`reloc_pcrel` (`objfile_macho.tks:216`) ganha o arm `Abs64 => 0` (absoluto, não
PC-relativo):

```teko
fn reloc_pcrel(k: MRelocKind) -> u32 {
    match k {
        PageHi => 1 to u32
        PageLo => 0 to u32
        Call => 1 to u32
        Abs64 => 0 to u32
    }
}
```

NOVO `reloc_length` — o `r_length` deixa de ser hardcoded 2 e passa a depender do
kind (2 = 4 bytes para os fixups de instrução; 3 = 8 bytes para o ponteiro absoluto):

```teko
/**
 * reloc_length — the Mach-O `relocation_info.r_length` for an `MRelocKind`: 2 (a
 * 4-byte word) for every instruction fixup (`PageHi`/`PageLo`/`Call`, patched into a
 * 32-bit ARM64 instruction word), and 3 (an 8-byte quad) for `Abs64` — a 64-bit
 * absolute pointer slot inside `__const` (#594 T-B3). Extracted from the historical
 * hardcoded `2` in `emit_reloc_table` so the value is byte-identical for the three
 * instruction kinds (the `__text` relocation goldens prove it) and correct for the
 * new pointer kind.
 *
 * @param MRelocKind k  the relocation kind
 * @return u32  the `r_length` field value (2 or 3)
 */
fn reloc_length(k: MRelocKind) -> u32 {
    match k {
        Abs64 => 3 to u32
        PageHi => 2 to u32
        PageLo => 2 to u32
        Call => 2 to u32
    }
}
```

### 2.2 Mach-O: partição, layout, `__const` reloff, segunda reloc table

**Partição (novo helper).**

```teko
/**
 * MachoRelocParts — a `EncodedModule`'s relocations split by patch-site section
 * (#594 T-B3): `text` (the historical `__text`-base-relative fixups) and `rodata`
 * (the `__const`-base-relative data→data pointers). The canonical seam (§0): the
 * single `.sect`-tagged `enc.relocs` list is partitioned at the writer boundary, the
 * same logical point ELF's `x86_reloc_reqs` bridge partitions into
 * `ElfObject.{relocs, rodata_relocs}`. `rodata` is EMPTY in every real compile
 * (the `encode_rodata` honest-stop keeps producers out), so `__const` carries no
 * relocation and the object is byte-identical.
 *
 * @since #594 T-B3
 */
type MachoRelocParts = struct {
    /** text — the relocations whose patch site is in `__text` (`r.sect == Text`). */
    text: []Reloc
    /** rodata — the relocations whose patch site is in `__const` (`r.sect == Rodata`). */
    rodata: []Reloc
}

/**
 * macho_partition_relocs — split `relocs` into its `__text` and `__const` partitions
 * by each reloc's `RelocSect` tag (#594 T-B3, §0 seam). Preserves emission order
 * within each partition. Today every reloc is `Text`, so `rodata` is empty and the
 * writer's `__const` relocation path is never entered.
 *
 * @param []Reloc relocs  the module's relocations, `.sect`-tagged, in emission order
 * @return MachoRelocParts  the `text` and `rodata` partitions
 */
fn macho_partition_relocs(relocs: []Reloc) -> MachoRelocParts {
    mut text: []Reloc = teko::list::empty()
    mut rodata: []Reloc = teko::list::empty()
    mut i: u64 = 0
    loop {
        if i >= relocs.len { break }
        let r = relocs[i]
        match r.sect {
            Text => { text = teko::list::push(text, r) }
            Rodata => { rodata = teko::list::push(rodata, r) }
        }
        i++
    }
    MachoRelocParts { text = text; rodata = rodata }
}
```

**`MachoLayout` ganha os campos de `__const` relocs** (`objfile_macho.tks:232`, após
`nreloc`):

```teko
    /**
     * const_reloc_offset — the file offset of the `__const` relocation table
     * (contiguous, immediately after the `__text` table; #594 T-B3), or the
     * `__text`-table-end value when there is no rodata-internal relocation (nothing
     * is emitted there in that case).
     */
    const_reloc_offset: u32
    /**
     * nreloc_const — the number of `__const` relocation entries (a rodata-internal
     * data→data pointer per entry); 0 in every real compile, keeping the object
     * byte-identical to the pre-T-B3 writer (#594 T-B3).
     */
    nreloc_const: u32
```

**`compute_macho_layout`** (`:313`) passa a receber as duas contagens (o writer as
tira da partição) e a computar a segunda tabela:

```teko
fn compute_macho_layout(enc: EncodedModule, strtab_size: u32, nreloc_text: u32, nreloc_const: u32) -> MachoLayout
```

Corpo — trocar `let nreloc = enc.relocs.len to u32` por `nreloc_text`, e após
`reloc_offset`:

```teko
    let reloc_offset = align_up(seg_data_end, 4 to u32)
    let text_reloc_end = reloc_offset + (nreloc_text * (8 to u32))
    let const_reloc_offset = text_reloc_end
    let reloc_end = const_reloc_offset + (nreloc_const * (8 to u32))
    let symtab_offset = align_up(reloc_end, 8 to u32)
```

e no literal `MachoLayout`: `nreloc = nreloc_text`, `const_reloc_offset =
const_reloc_offset`, `nreloc_const = nreloc_const`. Prova de identidade: com
`nreloc_const=0`, `const_reloc_offset = text_reloc_end`, `reloc_end = text_reloc_end`,
`symtab_offset = align8(text_reloc_end)` = valor antigo. Idêntico.

**`emit_segment`** (`:519`) — o `__const` `section_64` passa a carregar o seu
`reloff`/`nreloc` condicionalmente:

```teko
    if lay.nsects > (1 to u32) {
        let const_reloff = if lay.nreloc_const > (0 to u32) { lay.const_reloc_offset } else { 0 to u32 }
        b = emit_section(b, "__const", "__TEXT", lay.text_size, lay.const_size, lay.const_offset, 0 to u32, const_reloff, lay.nreloc_const, 0 to u32)
    }
```

Quando `nreloc_const=0`: `const_reloff=0`, `nreloc=0` — byte-idêntico ao `emit_section(…,
0, 0, 0)` de hoje (`:520`).

**`emit_reloc_table`** (`:600`) — generalizada para receber uma LISTA de relocs (a
partição) e usar `reloc_length(r.kind)`:

```teko
/**
 * emit_reloc_table — a `relocation_info` array for ONE section's relocations
 * (`relocs`): one 8-byte entry each — the section-relative `r_address`, then the
 * packed `r_symbolnum | r_pcrel<<24 | r_length<<25 | r_extern<<27 | r_type<<28`.
 * Every fixup is external (`r_extern=1`), targeting a symbol by index; the length is
 * `reloc_length(r.kind)` (2 for the instruction fixups — byte-identical to the old
 * hardcoded `2` — and 3 for an `Abs64` 8-byte `__const` pointer, #594 T-B3). Called
 * once for the `__text` partition and once for the `__const` partition; the target of
 * a rodata pointer is the target datum's own file-local symbol (§1.1), so the in-place
 * addend stays 0.
 *
 * @param []byte buf  the buffer to extend
 * @param []Reloc relocs  one section's relocations, in emission order
 * @param []Symbol symbols  the module's symbol table (for the target indices)
 * @return []byte  `buf` followed by the relocation entries
 */
fn emit_reloc_table(buf: []byte, relocs: []Reloc, symbols: []Symbol) -> []byte {
    mut b = buf
    mut i: u64 = 0
    loop {
        if i >= relocs.len { break }
        let r = relocs[i]
        b = emit_u32_le(b, r.offset)
        let symnum = sym_index(symbols, r.sym)
        let packed = symnum | (reloc_pcrel(r.kind) << (24 to u32)) | (reloc_length(r.kind) << (25 to u32)) | ((1 to u32) << (27 to u32)) | (reloc_type_value(r.kind) << (28 to u32))
        b = emit_u32_le(b, packed)
        i++
    }
    b
}
```

**`emit_macho`** (`:735`) costura a partição + a segunda tabela:

```teko
pub fn emit_macho(enc: EncodedModule) -> []byte {
    let parts = macho_partition_relocs(enc.relocs)
    let strtab = build_strtab(enc.symbols)
    let lay = compute_macho_layout(enc, strtab.bytes.len to u32, parts.text.len to u32, parts.rodata.len to u32)
    mut b: []byte = teko::list::empty()
    b = emit_header(b, lay)
    b = emit_segment(b, lay)
    b = emit_build_version(b)
    b = emit_symtab_cmd(b, lay)
    b = emit_dysymtab_cmd(b, lay)
    b = append_bytes(b, enc.text)
    b = append_bytes(b, enc.rodata)
    b = pad_to_mult(b, 4 to u32)
    b = emit_reloc_table(b, parts.text, enc.symbols)
    b = emit_reloc_table(b, parts.rodata, enc.symbols)
    b = pad_to_mult(b, 8 to u32)
    b = emit_symtab(b, enc.symbols, strtab, lay.text_size)
    append_bytes(b, strtab.bytes)
}
```

> As duas tabelas são CONTÍGUAS, sem padding entre elas (cada `section_64` aponta para
> o seu sub-range via `reloff`/`nreloc`; entradas são de 8 bytes, então nada
> desalinha). Quando `parts.rodata` é vazia, `emit_reloc_table(b, parts.rodata, …)`
> não escreve nada e o `pad_to_mult(b, 8)` seguinte é o mesmo pad-antes-do-symtab de
> hoje. **Nenhum fold in-place é necessário no Mach-O** (o addend da relocation
> UNSIGNED externa é 0; o slot de 8 bytes já é zero no `__const` reservado pelo
> serializer T-B6).

### 2.3 COFF: partição, layout, `.rdata` reloc table, fold in-place de 8 bytes

**Partição** (análogo, sobre `RelocX86`):

```teko
/**
 * CoffRelocParts — a `EncodedModuleX86`'s relocations split by patch-site section
 * (#594 T-B3): `text` (`.text`-relative fixups) and `rdata` (`.rdata`-relative
 * data→data pointers). The canonical seam (§0), mirroring `MachoRelocParts` and
 * ELF's bridge partition; `rdata` is EMPTY in every real compile.
 *
 * @since #594 T-B3
 */
type CoffRelocParts = struct {
    /** text — the relocations whose patch site is in `.text` (`r.sect == Text`). */
    text: []RelocX86
    /** rdata — the relocations whose patch site is in `.rdata` (`r.sect == Rodata`). */
    rdata: []RelocX86
}

/**
 * coff_partition_relocs — split `relocs` into its `.text` and `.rdata` partitions by
 * each reloc's `RelocSect` tag (#594 T-B3, §0 seam), preserving emission order within
 * each. Today every reloc is `Text`, so `rdata` is empty and `.rdata` carries no
 * relocation.
 *
 * @param []RelocX86 relocs  the module's relocations, `.sect`-tagged, in order
 * @return CoffRelocParts  the `text` and `rdata` partitions
 */
fn coff_partition_relocs(relocs: []RelocX86) -> CoffRelocParts {
    mut text: []RelocX86 = teko::list::empty()
    mut rdata: []RelocX86 = teko::list::empty()
    mut i: u64 = 0
    loop {
        if i >= relocs.len { break }
        let r = relocs[i]
        match r.sect {
            Text => { text = teko::list::push(text, r) }
            Rodata => { rdata = teko::list::push(rdata, r) }
        }
        i++
    }
    CoffRelocParts { text = text; rdata = rdata }
}
```

**`coff_build_relocs`** (`:423`) é REUSADA para a partição `.rdata` (o mesmo corpo
resolve o alvo: um rodata local re-aponta para o símbolo de seção `.rdata` via
`coff_rodata_secsym_index`, e `coff_reloc_type(Abs64)=ADDR64`). Nenhuma edição nela.

**Fold in-place de 8 bytes em `.rdata` (novo).** O text→rodata dobra o offset num
disp32 (`write_u32_le_at`, `:460`); o data→data dobra num slot de 8 bytes DENTRO de
`.rdata`. Como `LDataReloc.offset`/`Symbol.offset` são `u32`, o offset-alvo cabe nos 4
bytes baixos e os 4 altos ficam 0 (o slot é zero-init pelo serializer). Novo helper:

```teko
/**
 * coff_apply_data_reloc_addends — fold every rodata-INTERNAL relocation's target
 * offset in-place into its 8-byte pointer slot inside `.rdata` (#594 T-B3; the COFF
 * data→data analog of `coff_apply_rodata_addends`, which folds into `.text`). For
 * each `RelocX86` whose target is a defined section-2 local, write that datum's
 * `.rdata` byte offset as a little-endian `u32` at the reloc's `.rdata` offset (the
 * low word; the high word stays 0 — a rodata offset never exceeds 32 bits, and the
 * slot is zero-initialized). The relocation itself (built by `coff_build_relocs`)
 * points at the `.rdata` STATIC section symbol, so the linker computes
 * `section_base(.rdata) + in_place` = the target datum's absolute address. Returns
 * `rodata` unchanged when no relocation is a rodata hit (the byte-identity path).
 *
 * @param []byte rodata  the `.rdata` section image (pointer slots zero-initialized)
 * @param []Symbol symbols  the module's neutral symbol list (for rodata resolution)
 * @param []RelocX86 relocs  the `.rdata` partition, in emission order
 * @return []byte  `rodata` with each data reloc's target offset folded in-place
 */
fn coff_apply_data_reloc_addends(rodata: []byte, symbols: []Symbol, relocs: []RelocX86) -> []byte {
    mut buf = rodata
    mut i: u64 = 0
    loop {
        if i >= relocs.len { break }
        let r = relocs[i]
        let hit = coff_rodata_hit(symbols, r.sym)
        if hit.found { buf = write_u32_le_at(buf, r.offset, hit.offset) }
        i++
    }
    buf
}
```

> A gravação do word baixo basta: o slot de ponteiro é reservado como 8 bytes zero
> pelo serializer Tier-B (T-B6), então o word alto (offset+4) permanece 0. (Se o
> implementer preferir robustez explícita, um `write_u32_le_at(buf, r.offset + 4, 0)`
> após o baixo é inerte hoje e documenta a largura de 8 bytes — opcional.)

**`CoffLayout` ganha os campos de `.rdata` relocs** (`objfile_coff.tks:477`, após
`num_relocs`):

```teko
    /** rdata_reloc_offset — the file offset of the `.rdata` relocation array (contiguous, after the `.text` array; #594 T-B3). */
    rdata_reloc_offset: u32
    /** num_rdata_relocs — the number of `.rdata` relocations (a data→data pointer each); 0 in every real compile, keeping the object byte-identical (#594 T-B3). */
    num_rdata_relocs: u32
```

**`compute_coff_layout`** (`:512`) recebe as duas contagens:

```teko
fn compute_coff_layout(enc: EncodedModuleX86, num_symbols: u32, num_text_relocs: u32, num_rdata_relocs: u32) -> CoffLayout
```

Corpo — trocar `num_relocs` por `num_text_relocs` e após `reloc_offset`:

```teko
    let reloc_offset = rdata_offset + rdata_size
    let text_reloc_end = reloc_offset + (num_text_relocs * (10 to u32))
    let rdata_reloc_offset = text_reloc_end
    let symtab_offset = rdata_reloc_offset + (num_rdata_relocs * (10 to u32))
```

e no literal: `num_relocs = num_text_relocs`, `rdata_reloc_offset =
rdata_reloc_offset`, `num_rdata_relocs = num_rdata_relocs`. Identidade com
`num_rdata_relocs=0`: `symtab_offset = reloc_offset + num_text_relocs*10` = valor
antigo.

**`emit_coff_sections`** (`:593`) — o `.rdata` header ganha o seu reloc pointer
condicional:

```teko
    if lay.num_sections > (1 to u32) {
        let rdata_reloc_ptr = if lay.num_rdata_relocs > (0 to u32) { lay.rdata_reloc_offset } else { 0 to u32 }
        b = emit_coff_section(b, ".rdata", lay.rdata_size, lay.rdata_offset, rdata_reloc_ptr, lay.num_rdata_relocs, COFF_RDATA_CHARACTERISTICS)
    }
```

Quando `num_rdata_relocs=0`: `rdata_reloc_ptr=0`, `nreloc=0` — byte-idêntico ao
`emit_coff_section(…, 0, 0, …)` de hoje (`:597`).

**`emit_coff`** (`:708`) costura a partição + a segunda tabela + o fold de dados:

```teko
pub fn emit_coff(enc: EncodedModuleX86) -> []byte {
    let parts = coff_partition_relocs(enc.relocs)
    let coffsyms = coff_build_symbols(enc.symbols)
    let strtab = build_coff_strtab(coffsyms)
    let text_relocs = coff_build_relocs(enc.symbols, coffsyms, parts.text)
    let rdata_relocs = coff_build_relocs(enc.symbols, coffsyms, parts.rdata)
    let patched_text = coff_apply_rodata_addends(enc.text, enc.symbols, parts.text)
    let patched_rodata = coff_apply_data_reloc_addends(enc.rodata, enc.symbols, parts.rdata)
    let lay = compute_coff_layout(enc, coffsyms.len to u32, text_relocs.len to u32, rdata_relocs.len to u32)
    mut b: []byte = teko::list::empty()
    b = emit_coff_header(b, lay)
    b = emit_coff_sections(b, lay)
    b = append_bytes(b, patched_text)
    b = append_bytes(b, patched_rodata)
    b = emit_coff_relocs(b, text_relocs)
    b = emit_coff_relocs(b, rdata_relocs)
    b = emit_coff_symtab(b, coffsyms, strtab)
    append_bytes(b, strtab.bytes)
}
```

> `coff_apply_rodata_addends` passa a receber `parts.text` (a partição text) em vez de
> `enc.relocs` — hoje idêntico (todas as relocs são `Text`), mas correto quando a
> cadeia abrir (não deve dobrar um data-reloc no `.text`). As duas reloc tables são
> contíguas (10 bytes cada, COFF não exige alinhamento); `rdata_relocs` vazia → nada
> escrito → byte-idêntico.

---

## 3. PROVA de byte-identidade quando não há rodata-reloc

Em TODA compilação real, `enc.relocs` é 100% `Text` (§1.3), então
`parts.rodata`/`parts.rdata` é vazia e:

**Mach-O.** (1) `compute_macho_layout` com `nreloc_const=0` colapsa `const_reloc_offset
= text_reloc_end` e `symtab_offset = align8(text_reloc_end)` = valor antigo (§2.2).
(2) `emit_segment` emite o `__const` `section_64` com `reloff=0`/`nreloc=0` — idêntico
ao `emit_section(…, 0, 0, 0)` de hoje. (3) `emit_reloc_table(b, parts.rodata, …)` não
escreve nada; `emit_reloc_table(b, parts.text, …)` produz os MESMOS bytes que o antigo
`emit_reloc_table(b, enc)` porque `reloc_length(Call/PageHi/PageLo)=2` reproduz o
`(2 << 25)` hardcoded e o resto do packing é idêntico. (4) `reloc_type_value`/
`reloc_pcrel` ganharam só o arm `Abs64`, inalcançável sem `parts.rodata`. ⇒ cada byte
idêntico. Goldens `objfile_macho_test.tkt` (ex.: `exit`-object 376 bytes,
`rodata_module` 443 bytes, `rodata_reloc_module` 500 bytes) intactos.

**COFF.** (1) `compute_coff_layout` com `num_rdata_relocs=0` colapsa `symtab_offset =
reloc_offset + num_text_relocs*10` = valor antigo. (2) `emit_coff_sections` emite o
`.rdata` header com `reloc_ptr=0`/`nreloc=0` — idêntico. (3) `emit_coff_relocs(b,
rdata_relocs)` (vazia) não escreve nada; `emit_coff_relocs(b, text_relocs)` produz os
mesmos bytes (a partição text == `enc.relocs` de hoje). (4) `coff_apply_rodata_addends`
sobre `parts.text` == sobre `enc.relocs` de hoje; `coff_apply_data_reloc_addends`
(vazia) devolve `enc.rodata` intacto. ⇒ cada byte idêntico. Goldens
`objfile_coff_test.tkt` (ex.: `exit` 120 bytes, `rodata` 198 bytes, `longname` 131
bytes) intactos.

O **fixpoint gen1==gen2** + ambas as engines são a prova viva final. **QED.**

---

## 4. Fixtures a ADICIONAR (campo-a-campo, padrão mo_*/co_*)

Todas constroem `EncodedModule`/`EncodedModuleX86` À MÃO e chamam o `pub fn`
`emit_macho`/`emit_coff` diretamente — o precedente de `co_abs64_module` e das
fixtures ELF T-B2. Rodam idênticas na VM e no harness nativo. Offsets computados
abaixo; o implementer os confirma na 1ª run verde e os fixa (como as fixtures ELF/
Mach-O existentes).

### 4.1 Mach-O (arm64): objeto rodata-only com 1 data→data reloc

Fixture: sem funções; `__const` de 16 bytes = slot de ponteiro (8 zero, offset 0) +
datum-alvo (`0x41` + 7 zero, offset 8); dois símbolos locais section-2 (`ptr`@0,
`target`@8); uma `Reloc { offset=0; sym="target"; kind=Abs64; sect=Rodata }`.

```teko
/**
 * mo_rodata_ptr_enc — a hand-built `EncodedModule` with no functions and a 16-byte
 * `__const` = an 8-byte pointer slot (offset 0) then the target datum `0x41` (offset
 * 8), plus a single `Abs64`/`Rodata` relocation at `__const` offset 0 targeting the
 * `target` local. Feeds `emit_macho` directly (`encode_module` honest-stops a
 * rodata-internal pointer), pinning the `__const` relocation the T-B3 writer emits.
 *
 * @return EncodedModule  the hand-built rodata-internal-pointer module
 */
fn mo_rodata_ptr_enc() -> EncodedModule {
    let rodata = [0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0x41 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte]
    let ptr = Symbol { name = "ptr"; defined = true; sect = 2 to u8; offset = 0 to u32; local = true }
    let target = Symbol { name = "target"; defined = true; sect = 2 to u8; offset = 8 to u32; local = true }
    let rr = Reloc { offset = 0 to u32; sym = "target"; kind = MRelocKind::Abs64; sect = RelocSect::Rodata }
    EncodedModule { text = teko::list::empty(); rodata = rodata; symbols = [ptr, target]; relocs = [rr] }
}
```

**Layout derivado** (nsects=2, sizeofcmds=360, text_offset=392, text_size=0,
const_offset=392, const_size=16, seg_data_end=408, reloc_offset=align4(408)=408,
nreloc_text=0, const_reloc_offset=408, nreloc_const=1, reloc_end=416,
symtab_offset=416, nsyms=2 → symtab 416..448, strtab 448..462 [NUL + `_ptr\0` +
`_target\0`]). **Total 462 bytes.**

**`__const` `section_64`** começa no byte 184 (header 32 + seg cmd: base 104 + 80 do
`__text` section_64); `reloff`@240, `nreloc`@244.

```
obj.len == 462
mo_u32_at(obj, 96)  == 2            // seg nsects = 2
mo_u32_at(obj, 240) == 408          // __const section_64 reloff
mo_u32_at(obj, 244) == 1            // __const section_64 nreloc
// tabela de relocation de __const em 408 (8 bytes):
mo_u32_at(obj, 408) == 0            // r_address = slot offset em __const (0)
mo_u32_at(obj, 412) == 0x0E000001   // symnum=1 | length=3(<<25) | extern=1(<<27) | type=UNSIGNED=0
// slot de ponteiro (in-place addend 0) e datum-alvo em __const (const_offset=392):
obj[392] == 0                       // slot zero (Mach-O UNSIGNED extern não dobra in-place)
obj[400] == 0x41                    // datum-alvo em __const offset 8 → file 400
// símbolo alvo (target, índice 1): n_value = section_addr(2, text_size=0)+8 = 8
```

> `0x0E000001` = `1` (symnum de `target`) `| (0<<24)` (pcrel 0) `| (3<<25=0x06000000)`
> (length 3) `| (1<<27=0x08000000)` (extern) `| (0<<28)` (type UNSIGNED). Prova o
> `r_length=3`/`r_type=0` do ponteiro absoluto de 64 bits.

### 4.2 Mach-O: colapso quando vazio (byte-identidade)

Mesma `mo_rodata_ptr_enc` mas com `relocs = teko::list::empty()`: `__const` reloff/
nreloc = 0, sem segunda tabela, symtab_offset=align8(408)=408 → **total 454 bytes**.

```
obj.len == 454
mo_u32_at(obj, 240) == 0            // __const reloff = 0 (colapsado)
mo_u32_at(obj, 244) == 0            // __const nreloc = 0
```

### 4.3 COFF (x86-64): módulo com 1 data→data reloc em `.rdata`

Fixture: `text=[0xC3]` (ret, `main`@0 global), `.rdata` de 16 bytes (slot@0 +
`0x41`@8), símbolos section-2 locais `ptr`@0/`target`@8 (colapsam no símbolo de seção
`.rdata`), `main`@0; uma `RelocX86 { offset=0; sym="target"; kind=Abs64; addend=0;
sect=Rodata }`.

```teko
/**
 * co_rodata_ptr_module — a hand-built `EncodedModuleX86`: a 1-byte `ret` `.text`, a
 * 16-byte `.rdata` (an 8-byte pointer slot at offset 0, the target datum `0x41` at
 * offset 8), two section-2 rodata locals collapsing into the `.rdata` STATIC section
 * symbol, and one `Abs64`/`Rodata` relocation at `.rdata` offset 0 targeting the
 * `target` datum. Feeds `emit_coff` directly, pinning the `.rdata` relocation +
 * 8-byte in-place fold the T-B3 writer emits.
 *
 * @return EncodedModuleX86  the hand-built rodata-internal-pointer module
 */
fn co_rodata_ptr_module() -> EncodedModuleX86 {
    let rodata = [0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0x41 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte]
    let ptr = co_sym("ptr", true, true, 2 to u8, 0 to u32)
    let target = co_sym("target", true, true, 2 to u8, 8 to u32)
    let main = co_sym("main", true, false, 1 to u8, 0 to u32)
    let rr = RelocX86 { offset = 0 to u32; sym = "target"; kind = RelocKindX86::Abs64; addend = 0 to i64; sect = RelocSect::Rodata }
    EncodedModuleX86 { text = [0xC3 to byte]; rodata = rodata; symbols = [ptr, target, main]; relocs = [rr] }
}
```

**Layout derivado** (num_sections=2, coffsyms=[`.rdata`(0), `main`(1)] → num_symbols=2,
text_offset=100, text_size=1, rdata_offset=101, rdata_size=16, reloc_offset=117,
num_text_relocs=0, rdata_reloc_offset=117, num_rdata_relocs=1, symtab_offset=127,
symtab 127..163, strtab 163..167 [só o size field=4]). **Total 167 bytes.**

`.rdata` `IMAGE_SECTION_HEADER` no byte 60; `PointerToRelocations`@84,
`NumberOfRelocations`@92.

```
obj.len == 167
co_u16_at(obj, 2)  == 2             // NumberOfSections = 2
co_u32_at(obj, 12) == 2             // NumberOfSymbols = 2 (.rdata secsym + main)
co_u32_at(obj, 44) == 0             // .text PointerToRelocations = 0 (nenhuma text reloc)
co_u16_at(obj, 52) == 0             // .text NumberOfRelocations = 0
co_u32_at(obj, 84) == 117           // .rdata PointerToRelocations
co_u16_at(obj, 92) == 1             // .rdata NumberOfRelocations
// tabela de relocation de .rdata em 117 (10 bytes):
co_u32_at(obj, 117) == 0            // VirtualAddress = slot offset em .rdata (0)
co_u32_at(obj, 121) == 0            // SymbolTableIndex = .rdata section symbol (índice 0)
co_u16_at(obj, 125) == 0x0001       // Type = IMAGE_REL_AMD64_ADDR64
// fold in-place de 8 bytes no slot de .rdata (rdata_offset=101):
co_u32_at(obj, 101) == 8            // word baixo = offset do datum-alvo (.rdata offset 8)
co_u32_at(obj, 105) == 0            // word alto = 0
obj[109] == 0x41                    // datum-alvo em .rdata offset 8 → file 109
```

### 4.4 COFF: colapso quando vazio (byte-identidade)

Mesma `co_rodata_ptr_module` mas com `relocs = teko::list::empty()`:
num_rdata_relocs=0, `.rdata` reloc_ptr/nreloc=0, symtab_offset=117 → **total 157
bytes**.

```
obj.len == 157
co_u32_at(obj, 84) == 0             // .rdata PointerToRelocations = 0 (colapsado)
co_u16_at(obj, 92) == 0             // .rdata NumberOfRelocations = 0
```

### 4.5 Fixtures unitárias dos helpers novos

- **`reloc_type_value(MRelocKind::Abs64) == 0`** e **`reloc_pcrel(MRelocKind::Abs64)
  == 0`** — estender `reloc_kind_maps_to_arm64_types` (`objfile_macho_test.tkt:299`)
  com os dois asserts (cobre o novo arm).
- **`reloc_length`**: `reloc_length(MRelocKind::Abs64) == 3`,
  `reloc_length(MRelocKind::Call) == 2`, `reloc_length(MRelocKind::PageHi) == 2`,
  `reloc_length(MRelocKind::PageLo) == 2` — cobre todos os arms.
- **`macho_partition_relocs`**: sobre `[Reloc{…Text}, Reloc{…Rodata}]` devolve
  `text.len==1`, `rodata.len==1`, ordem preservada; sobre lista só-`Text` devolve
  `rodata.len==0`.
- **`coff_partition_relocs`**: espelho x86 do acima.
- **`coff_apply_data_reloc_addends`** sobre `co_rodata_ptr_module`: o `.rdata` volta
  com `rodata[0]==8` (word baixo); sobre `relocs` vazia devolve `.rdata` intacto.
- `coff_reloc_type(Abs64)==ADDR64` já é coberto por `coff_reloc_type_maps_all_kinds`
  (`objfile_coff_test.tkt:405`) — nenhum novo teste preciso.

---

## 5. Sequência de edits + pontos de regressão

Cada edit é gate-able isoladamente; rodar o `.tkt` listado após cada um. Ordem tal que
o arquivo compila a cada passo.

| # | Arquivo | Edit | Gate de regressão (`.tkt`) |
|---|---|---|---|
| E1 | `src/backend/minst.tks` | `MRelocKind` ganha `Abs64` (§2.1); arm `Abs64 => "abs64"` no `match` de nome (`:1552`) | `minst_test.tkt` compila; `encode_arm64_test.tkt` |
| E2 | `src/backend/objfile_macho.tks` | `reloc_type_value`/`reloc_pcrel` ganham arm `Abs64` (§2.1) + novo `reloc_length` | `objfile_macho_test.tkt` (goldens byte-idênticos; `reloc_kind_maps` estendido) |
| E3 | `src/backend/objfile_macho.tks` | `MachoRelocParts` + `macho_partition_relocs`; `MachoLayout` +2 campos; `compute_macho_layout` recebe as 2 contagens (§2.2) | `objfile_macho_test.tkt` (mesmos offsets/tamanhos) |
| E4 | `src/backend/objfile_macho.tks` | `emit_reloc_table` recebe lista+symbols e usa `reloc_length`; `emit_segment` `__const` reloff condicional; `emit_macho` costura + 2ª tabela (§2.2) | **todos** os goldens Mach-O byte-idênticos (`parts.rodata` vazia) |
| E5 | `src/backend/objfile_coff.tks` | `CoffRelocParts` + `coff_partition_relocs`; `coff_apply_data_reloc_addends`; `CoffLayout` +2 campos; `compute_coff_layout` recebe as 2 contagens (§2.3) | `objfile_coff_test.tkt` (mesmos offsets/tamanhos) |
| E6 | `src/backend/objfile_coff.tks` | `emit_coff_sections` `.rdata` reloc-ptr condicional; `emit_coff` costura (partição + `coff_build_relocs` reusada + 2ª tabela + fold de dados) (§2.3) | **todos** os goldens COFF byte-idênticos (`parts.rdata` vazia) |
| E7 | `objfile_macho_test.tkt` + `objfile_coff_test.tkt` | fixtures §4 (mo_rodata_ptr + colapso; co_rodata_ptr + colapso; unitárias §4.5) | os próprios testes novos (verde) |

> Writers ELF/wasm e a VM **NÃO são tocados** em T-B3 (ELF é T-B2, wasm T-B4, VM
> T-B5). `encode_*.tks` só muda em `minst.tks` (o enum `MRelocKind` + o arm de nome) —
> o honest-stop `encode_rodata` de T-B1 **permanece** intocado. Sem edição de
> `lower.tks`/`lir_interp.tks`.

**Ritual points:**

- **Por-edit:** o `.tkt` do arquivo (tabela) — cada edit é gate-able só.
- **RITUAL POINT — fim de T-B3:** gate COMPLETO — todos os goldens de backend
  byte-idênticos (`objfile_macho_test.tkt`, `objfile_coff_test.tkt`,
  `objfile_elf_test.tkt`, `objfile_elf_riscv_test.tkt`, `encode_*_test.tkt`,
  `minst_test.tkt`, `lower_test.tkt`, `lir_interp_test.tkt`, `tkb_test.tkt`) +
  **fixpoint gen1==gen2** + ambas as engines (VM + nativo) + 100% de cobertura do
  delta (as fixtures §4.1/§4.3 cobrem o braço rodata-reloc; §4.2/§4.4 o colapso; §4.5
  os helpers/arms novos; os goldens existentes o braço `parts.rodata` vazia). **Sem
  seed bump** — T-B3 não adiciona capacidade que o corpus use (o 🔑 SEED BUMP #3 é
  depois de T-B5, plano §8).

---

## 6. Riscos + tensões (com resolução)

1. **Adicionar `MRelocKind::Abs64` quebra os `match` exaustivos.** *Resolução:* é o
   driver mecânico desejado — o compilador enumera os TRÊS sites (grep provado:
   `minst.tks:1552`, `objfile_macho.tks:202`, `:218`); os §2.1 dão os arms verbatim.
   `reloc_length` é função NOVA (sem risco de arm esquecido). Os sites de construção
   (isel/encode) só usam Call/PageHi/PageLo — não precisam de arm. Nenhum golden de
   encoder muda (o produtor de `Abs64` está atrás do honest-stop).

2. **LAYOUT/OFFSETS — a segunda reloc table desloca `symtab_offset` (Mach-O) /
   `symtab_offset` (COFF) QUANDO ativa.** *Resolução:* a nova tabela é estritamente
   condicional a `nreloc_const>0`/`num_rdata_relocs>0` em DOIS pontos acoplados por
   writer — o header da seção (`reloff`/`PointerToRelocations`) e os bytes da tabela —
   e `symtab_offset` deriva das mesmas contagens. A prova §3 mostra colapso byte-exato
   quando 0; os goldens + fixpoint são o guardrail. As tabelas são contíguas (entradas
   de tamanho fixo, sem padding entre elas).

3. **Generalizar `emit_reloc_table` (Mach-O) — fn byte-crítico.** *Resolução:* a
   mudança (lista em vez de `enc`, `reloc_length(r.kind)` em vez de `2` hardcoded) é
   behavior-preserving: `reloc_length(Call/PageHi/PageLo)=2` reproduz o `(2<<25)`
   antigo; os goldens `text_image_and_branch26_reloc` (`0x2D000001`) e as três palavras
   de `rodata_reloc_module` (`0x3D000000`/`0x4C000000`/`0x2D000002`) provam byte a byte.

4. **Consistência da costura (o requisito do brief, §0).** *Resolução:* ratificada
   law-first — Mach-O/COFF consomem `EncodedModule*` direto e particionam por `.sect`
   no writer (a materialização local da mesma partição que o ELF faz nos dois campos
   de `ElfObject`); NÃO se cria `MachoObject`/`CoffObject` (dead scaffolding) nem se
   adiciona campo redundante ao `EncodedModule*` (o produtor T-B povoa só a lista única
   tagueada). Uniforme e sem redundância. T-B4 (wasm) herda a mesma partição.

5. **Idioma de resolução do alvo diverge entre writers (Mach-O extern-to-local vs COFF
   section-symbol + in-place).** *Soft tension, não HALT.* Cada writer REUSA o idioma
   que já usa para text→rodata: Mach-O aponta para o símbolo local do datum
   (`r_extern=1`, addend 0 no slot); COFF aponta para o símbolo de seção `.rdata` +
   offset dobrado in-place (COFF não tem addend). Ambos produzem o mesmo efeito
   (endereço absoluto do alvo) e são idiomáticos ao formato; forçar um só idioma
   quebraria a consistência interna de cada writer. Provado §2.0 pelas specs
   (Apple arm64 reloc / MS PE-COFF). Passa em todas as Leis.

6. **Fold in-place de 8 bytes no COFF grava só o word baixo.** *Resolução:* o offset
   rodata é `u32` (nunca >32 bits) e o slot é zero-init (serializer T-B6), então o word
   alto permanece 0; a fixture §4.3 assera `co_u32_at(obj,105)==0`. Um
   `write_u32_le_at(…, offset+4, 0)` explícito é opcional (inerte hoje).

**Sem tensão genuína não resolvida → sem HALT.**

---

## 7. O que permanece BLOQUEADO (adiantado até o limite)

T-B3 entrega os writers Mach-O e COFF completos e provados HOJE (a relocation
data→data com patch site em `__const`/`.rdata`, testada por `EncodedModule*` à mão). O
que resta atrás do honest-stop de T-B1 (não faz parte de T-B3, mas nomeado para o
implementer):

- **Popular a partição `rodata`/`rdata`** a partir de um const Tier-B real: o
  `encode_rodata` deixar de honest-stopar e produzir `Reloc`/`RelocX86` com
  `sect=Rodata` (offset `__const`/`.rdata`-relativo, kind `Abs64`) a partir de
  `LRodata.relocs`. Isso é o crumb que fecha a cadeia (junto com T-B4 wasm + T-B5 VM);
  só então `parts.rodata` é não-vazia em compilação real. Até lá, o caminho `__const`/
  `.rdata` reloc só é exercitado pelas fixtures §4.
- **T-B4 (wasm)** e **T-B5 (VM)** completam a cadeia; o 🔑 SEED BUMP #3 (depois de
  T-B5) libera T-B6 (migrar os ABI descriptors). Nada disso bloqueia T-B3 — os dois
  writers estão prontos.
