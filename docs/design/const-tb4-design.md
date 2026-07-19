# T-B4 — wasm resolve ponteiros rodata-INTERNOS em emit-time (sem relocation) (#594 Tier-B)

Status: READY-TO-IMPLEMENT (architect, 2026-07-18). Track: Tier-B pointer-bearing
aggregate → rodata (`docs/design/const-module-level-plan.md` §8 crumb T-B4, §5.1
verdict, DECISION_LOG D2). Base: o working tree da const-wave com **T-B1, T-B2 e T-B3
já mergeados** (`RelocSect`/`sect` nos três `Reloc*`, `LDataReloc`/`LRodata.relocs` na
LIR — provado em `lir.tks:154/165/175`; honest-stop `honest_data_reloc` em
`encode_rodata` NATIVO; ELF `.rela.rodata`; Mach-O/COFF partição + `__const`/`.rdata`
relocs). Predecessores: `docs/design/const-tb{1,2,3}-design.md`.

> Escopo de T-B4 (verbatim §8): "wasm: compute+write intra-data i32 offsets in the
> active data segment (`objfile_wasm.tks:640`) — no reloc, but new emit-time
> resolution." Este crumb ensina o assembler wasm a MATERIALIZAR um ponteiro
> rodata→rodata: como o data segment ativo fica num offset de memória linear
> conhecido na emissão e o wasm não tem linker, o endereço-alvo é uma **constante i32
> de compile-time** escrita direto no slot — sem relocation, sem `Reloc`, sem
> `EncodedModule`. **Nenhum produtor a exercita ainda** (o `serialize_const` produtor
> honest-stopa qualquer agregado pointer-bearing até T-B6), então toda `LRodata` chega
> com `relocs` vazio, o módulo é **byte-idêntico** e todos os goldens/fixpoint ficam
> intactos. **encode_*/ELF/Mach-O/COFF/VM intocados; sem seed bump** (o 🔑 SEED BUMP #3
> é depois de T-B5, plano §8).

> **Correção de referência de linha.** O plano §8 aponta `objfile_wasm.tks:640`, mas a
> montagem real do data segment (a concatenação das `LRodata` + a tabela de offsets que
> o `LGlobalAddr` lê) vive HOJE em `stackify.tks` — `wasm_layout_rodata`
> (`stackify.tks:4931`), não em `objfile_wasm.tks`. O `objfile_wasm.tks` só serializa
> um `WasmDataSegment` JÁ-MONTADO (`wasm_append_data_segment`, `objfile_wasm.tks:754`;
> `emit_wasm`, `:799`). Logo **a edição de produto de T-B4 é inteiramente em
> `stackify.tks`; `objfile_wasm.tks` NÃO é tocado** (o ponteiro já está gravado nos
> bytes quando chegam ao emitter). Registrado para o implementer não procurar a costura
> no lugar errado.

---

## 1. Estado atual provado (com linhas)

### 1.1 Como o data segment ativo é montado — base fixa, ordem de `LModule.rodata`

O caminho wasm NÃO passa pelo encoder nativo. `wasm_assemble_program(lmod, ptr64)`
(`stackify.tks:5993`) monta o módulo direto do `LModule`:

1. **Layout (`wasm_layout_rodata`, `stackify.tks:4931`).** Percorre `lmod.rodata` UMA
   vez, colocando cada `LRodata` back-to-back a partir de `base = wasm_rodata_base()`
   (`:6002`). Para a entrada `i` registra `offsets[i] = off` (o endereço absoluto na
   memória linear DAQUELA entrada — `off` começa em `base`), concatena `rd.bytes`, e
   avança `off = off + rd.bytes.len`. Devolve `WasmRodataLayout { segment =
   WasmDataSegment { offset = base; bytes }; symbols; offsets; end }`
   (`:4946`). **Sem padding entre entradas** (strings têm alinhamento 1). A ORDEM é a
   de `lmod.rodata`; a BASE é `wasm_rodata_base()` (`:5115` — logo acima do scratch
   io/panic + a mensagem de narrow, um offset baixo fixo, conhecido na emissão).
2. **Registro (`wasm_symtab_add_rodata`, `:4959`).** Registra cada `symbols[i]`→
   `offsets[i]` na `WasmSymbolTable` (`:6006`), a tabela que o `LGlobalAddr` resolve.
3. **Segmento (`wasm_program_data_segments`, `:5848`).** Emite exatamente UM
   `WasmDataSegment` (offset=base, bytes=concatenação) quando há bytes (`:6026`).

**Prova de que o offset de cada `LRodata` é conhecido na emissão:** `offsets[i]` é
computado deterministicamente por concatenação a partir de `base` constante. Não há
linker, não há símbolo a resolver depois — `wasm_layout_rodata_places_entries_back_to_back`
(`stackify_test.tkt:2170`) já fixa `offsets[0]==0`, `offsets[1]==2` para `["hi","!"]`
em base 0, e `..._honors_a_nonzero_base` (`:2188`) fixa `offsets[0]==100` em base 100.

### 1.2 Onde um ponteiro data→data DENTRO do segment vira endereço i32 absoluto

Hoje existem DOIS pontos onde um endereço rodata aparece; um é text→data, o outro
(data→data) NÃO existe ainda:

- **text→data (EXISTE):** `stackify_global_addr` (`stackify.tks:2725`) baixa um
  `LGlobalAddr` para `i32.const off; local.set $r`, onde `off = wasm_resolve_data(
  symbols, g.symbol)` (`:2726`) — o endereço absoluto do datum. É o análogo wasm do
  `ADRP+ADD` do arm64, mas UMA instrução porque um `Ptr` wasm JÁ é o endereço plano de
  memória linear (`:2716`). O ponteiro é materializado **no CÓDIGO** (uma `i32.const`),
  nunca dentro dos bytes do segment. **Sem relocation.**
- **data→data (NÃO EXISTE — é o que T-B4 adiciona):** um agregado Tier-B (ex.: um
  descritor ABI com campos slice `[]u32`) serializado em rodata teria, DENTRO dos seus
  próprios bytes, um slot de ponteiro que deve conter o endereço absoluto de OUTRO
  datum rodata. Hoje `wasm_layout_rodata` só concatena `rd.bytes` verbatim (`:4942`) e
  **IGNORA `rd.relocs`** — um slot de ponteiro ficaria zerado (miscompile silencioso).
  T-B4 preenche esse slot com `offsets[alvo]` na emissão.

### 1.3 Por que NÃO há relocation (memory64 idem, com i64)

O data segment ativo é colocado num offset de memória linear FIXO e conhecido na
emissão (`base = wasm_rodata_base()`), e o módulo wasm é **self-contained** — não há
etapa de linker/objeto relocável (`emit_wasm` doc, `objfile_wasm.tks:782`: "there is NO
relocatable-object + external-linker step"). Logo o endereço absoluto de qualquer
datum é `offsets[i]`, um `u32` (wasm32) ou `u64` (wasm64) **constante em emit-time**.
Um ponteiro data→data é portanto escrito DIRETO no slot como bytes little-endian; não
há `Reloc`, não há `.rela.rodata`/`__const`, nada a re-apontar depois. Isto contrasta
com os writers nativos (T-B2/T-B3), onde o endereço absoluto do alvo só é conhecido
pelo linker e por isso precisa de uma relocation (R_X86_64_64 / ARM64_RELOC_UNSIGNED /
ADDR64). **memory64:** idêntico em espírito, mas o slot guarda um `i64` (o endereço de
memória linear é de 64 bits) — ver §3 (o arm wasm64 está honest-stopado a montante
hoje).

---

## 2. A costura: quem carrega o ponteiro interno até o writer wasm

### 2.1 O fluxo real — lower → stackify → objfile_wasm (o wasm NÃO usa `EncodedModule`)

Provado com linhas: o caminho wasm é `LModule` → `wasm_assemble_program`
(`stackify.tks:5993`) → `WasmModule` → `emit_wasm` (`objfile_wasm.tks:799`). **NÃO
existe `EncodedModule`/`EncodedModuleX86` no caminho wasm** — esses são saídas dos
encoders NATIVOS (x86/arm64/riscv). Consequência decisiva para a costura:

> **O wasm consome `LRodata.relocs` DIRETAMENTE (nível LIR), não `Reloc*.sect`.** Em
> T-B2/T-B3 os writers ELF/Mach-O/COFF particionam a lista `Reloc*` pelo discriminador
> `RelocSect` (T-B1). No wasm NÃO há `Reloc*` — o discriminador `RelocSect` **não é
> consumido no caminho wasm**. O carrier é o `LDataReloc { offset; target }`
> (`lir.tks:154`) já anexado a cada `LRodata` (`lir.tks:175`), que chega intacto a
> `wasm_layout_rodata`. A costura wasm é: `wasm_layout_rodata` computa a tabela de
> offsets (endereços absolutos), depois um passo de resolução escreve cada
> `LRodata.relocs` no slot correspondente. Nenhuma nova estrutura neutra, nenhuma
> partição — a informação já está na LIR.

### 2.2 A semântica exata do slot (espelhando os outros writers)

Quando `LRodata.relocs` chega não-vazio, o wasm ESCREVE o endereço absoluto do alvo no
slot de ponteiro DENTRO dos bytes do segment. O endereço = **base do segment + offset
do alvo dentro do segment = `offsets[alvo]`** (já é absoluto, pois `offsets` começa em
`base`). Definição EXATA da largura (o slot que o serializer Tier-B reserva é de 8
bytes, em paridade com o `Abs64` nativo):

- **wasm32 (o único arm alcançável hoje):** escreve o endereço como um **`i32`
  little-endian nos 4 bytes baixos do slot** `[p, p+4)`; o word alto `[p+4, p+8)`
  permanece **zero** (um endereço de memória linear nunca excede 2^32, e o slot é
  zero-init pelo serializer). Espelha o fold in-place de 8 bytes do COFF (T-B3 §2.3:
  "grava só o word baixo… o word alto permanece 0"), diferindo APENAS em que o wasm
  grava o endereço **ABSOLUTO** (sem linker), enquanto o COFF grava um offset
  `.rdata`-relativo que o linker rebaseia. **Robustez:** um `write_u32_le_at` (4 bytes)
  é correto mesmo se o serializer reservar só 4 bytes no wasm32 — preenche o slot
  exato; se reservar 8, o word alto zero-init fica intacto. Logo o write wasm32 de T-B4
  é INDEPENDENTE da decisão de largura de slot do T-B6.
- **wasm64:** o mesmo slot guarda um **`i64` little-endian** (endereço de 64 bits). Este
  arm está **honest-stopado a montante hoje** (§3), então T-B4 NÃO o implementa — fica
  como remainder bloqueado nomeado.

O valor é escrito no índice **segment-relativo** `(offsets[i] - base) + rel.offset`
(o início da entrada `i` no segment + o offset do slot dentro da entrada).

### 2.3 Assinaturas exatas a adicionar (W15 Javadoc, copiar verbatim)

Colocadas em `stackify.tks` logo após `wasm_layout_rodata` (`:4947`), antes de
`wasm_symtab_add_rodata` (`:4949`). Reusam `write_u32_le_at` (`encode_x86_64.tks:2084`)
— um `fn` privado do MESMO namespace `teko::backend` (path-derivado; `objfile_coff.tks`
já o chama unqualified em `:487`/`:530`, provando o acesso cross-file same-namespace).

```teko
/**
 * wasm_data_addr_of — o endereço ABSOLUTO de memória linear da entrada rodata cujo
 * símbolo é `target`, buscado nas tabelas paralelas `symbols`/`offsets` de um layout
 * de rodata (#594 T-B4). É a resolução do ALVO de um ponteiro data→data no wasm:
 * como o data segment ativo fica numa base fixa e conhecida na emissão
 * (`wasm_rodata_base`), o offset de cada entrada JÁ é o seu endereço absoluto final,
 * então um ponteiro intra-data é uma constante i32 de compile-time — sem relocation,
 * o endereço é gravado direto no slot. Devolve um erro NOMEADO (não um skip silencioso)
 * quando nenhuma entrada carrega esse símbolo, mantendo o miscompile impossível.
 *
 * @param []str symbols  os símbolos das entradas laid-out, paralelo a `offsets`
 * @param []u32 offsets  o endereço absoluto de memória linear de cada entrada
 * @param str target  o símbolo da entrada referenciada
 * @return u32 | error  o endereço absoluto do alvo, ou um erro nomeado
 */
fn wasm_data_addr_of(symbols: []str, offsets: []u32, target: str) -> u32 | error {
    mut i: u64 = 0
    loop {
        if i >= symbols.len { break }
        if symbols[i] == target { return offsets[i] }
        i++
    }
    error { message = $"stackify: rodata-internal pointer targets unknown rodata symbol '{target}' (#594 T-B4)" }
}

/**
 * wasm_relocate_rodata — resolve as relocations INTERNAS de ponteiro de cada entrada
 * rodata num layout já montado, devolvendo o layout com os bytes do seu data segment
 * patcheados in-place (#594 T-B4). Para cada `LDataReloc { offset; target }` da
 * entrada `i`, o endereço absoluto de `target` (`wasm_data_addr_of`) é escrito no slot
 * de ponteiro no byte segment-relativo `(layout.offsets[i] - base) + offset`. O wasm
 * NÃO tem linker e o data segment ativo fica numa base fixa conhecida na emissão
 * (`wasm_rodata_base`), então um ponteiro intra-data é uma constante i32 de
 * compile-time escrita direto no segment — o análogo wasm das relocations data→data
 * `.rela.rodata`/`__const` dos writers nativos (T-B2/T-B3), emitido SEM registro de
 * relocation (§8 T-B4). O slot de 8 bytes que o serializer Tier-B reserva é preenchido
 * low-word-first: no wasm32 o endereço i32 ocupa os bytes [p, p+4) e o word alto
 * [p+4, p+8) permanece zero (um endereço linear nunca excede 2^32). Toda entrada
 * rodata que o compilador produz HOJE tem `relocs` VAZIO (o produtor `serialize_const`
 * honest-stopa agregados pointer-bearing até T-B6), então isto devolve o layout
 * byte-idêntico e todo golden wasm fica intacto. O arm wasm64 (i64) NÃO é implementado
 * aqui: um programa wasm64 com rodata é honest-stopado a montante por
 * `wasm_scope_stop_wasm64` (§3), então o único arm alcançável é o write i32 do wasm32.
 *
 * @param WasmRodataLayout layout  o rodata laid-out (`wasm_layout_rodata`)
 * @param []lir::LRodata rodata  as entradas na MESMA ordem (as suas relocs)
 * @return WasmRodataLayout | error  o layout com os slots de ponteiro escritos, ou o
 *                                   erro nomeado de alvo desconhecido propagado
 */
pub fn wasm_relocate_rodata(layout: WasmRodataLayout, rodata: []lir::LRodata) -> WasmRodataLayout | error {
    let base = layout.segment.offset
    mut buf = layout.segment.bytes
    mut i: u64 = 0
    loop {
        if i >= rodata.len { break }
        let entry_start = layout.offsets[i] - base
        mut j: u64 = 0
        loop {
            if j >= rodata[i].relocs.len { break }
            let rel = rodata[i].relocs[j]
            let addr = match wasm_data_addr_of(layout.symbols, layout.offsets, rel.target) { u32 as a => a; error as e => return e }
            buf = write_u32_le_at(buf, entry_start + rel.offset, addr)
            j++
        }
        i++
    }
    WasmRodataLayout { segment = WasmDataSegment { offset = base; bytes = buf }; symbols = layout.symbols; offsets = layout.offsets; end = layout.end }
}
```

### 2.4 Costura no assembler (a ÚNICA edição de produção fora dos fns novos)

`wasm_assemble_program` (`stackify.tks:6002`) — o `let rodata = wasm_layout_rodata(…)`
único vira DUAS linhas (layout, depois resolução):

```teko
    let rodata0 = wasm_layout_rodata(lmod.rodata, wasm_rodata_base())
    let rodata = match wasm_relocate_rodata(rodata0, lmod.rodata) { WasmRodataLayout as x => x; error as e => return e }
```

Todos os usos seguintes de `rodata` (`:6006` `wasm_symtab_add_rodata`, `:6020`
`wasm_program_memory_plan`) veem o layout patcheado — `symbols`/`offsets`/`end`
inalterados, só `segment.bytes` mudou, então o symtab e o memory-plan são idênticos.

> **Por que `wasm_layout_rodata` NÃO muda de assinatura** (design de menor-ripple):
> mantém-se `-> WasmRodataLayout` (sem `| error`), então os SETE call sites de teste
> existentes (`stackify_test.tkt:2172,2190,2206,2597,2610,2681,2698`) ficam intocados.
> A resolução (que PODE falhar num alvo desconhecido) vive no fn NOVO
> `wasm_relocate_rodata`, e o único `match` novo é no assembler. Confina o erro a um
> ponto e não mexe em teste verde existente.

---

## 3. Honest-stop vs implementação — decisão law-first: IMPLEMENTAR a resolução real

**O caminho wasm NÃO passa pelo `encode_rodata` nativo.** Provado: `encode_rodata` (e o
seu honest-stop `honest_data_reloc` de T-B1) vive nos encoders NATIVOS
(`encode_arm64.tks`/x86/riscv); `wasm_layout_rodata` é **PARALELO, não reusado** — o
próprio doc o diz (`stackify.tks:4922-4923`: "Mirrors `encode_arm64.tks`'s
`encode_rodata`, PARALLELED rather than reused"). Logo o honest-stop de T-B1 **não
protege o caminho wasm**: hoje, se uma `LRodata` chegasse com `relocs` não-vazio ao
wasm, `wasm_layout_rodata` a ignoraria silenciosamente (§1.2). T-B4 precisa então OU do
seu próprio gate OU da resolução real.

**Decisão (law-first): IMPLEMENTAR A RESOLUÇÃO REAL, sem gate wasm.** Justificativa:

1. **A resolução é BARATA e testável à mão.** Ao contrário dos encoders nativos — que
   precisam de um registro de relocation que os writers/VM não consumiam até
   T-B2..T-B5 — o wasm precisa de ZERO relocation: o endereço-alvo é uma constante i32
   conhecida na emissão (`offsets[alvo]`), escrita com um `write_u32_le_at` já
   existente. São ~25 linhas (dois fns), e cada braço é fixável por `WasmRodataLayout`
   à mão (fixtures §5). É exatamente o que o §8 T-B4 pede: "no reloc, but new emit-time
   resolution". A Lei "issues são 100%" + "smallest safe step" favorece entregar o
   end-state real quando ele é barato e completo, em vez de um gate que só adia.
2. **O caminho wasm é self-contained e fica COMPLETO com T-B4.** wasm não depende de
   linker, writer nativo, nem VM. Implementar a resolução real fecha o wasm32
   inteiramente — sem gate, porque não há nada incompleto a proteger no wasm32.
3. **Nenhum vazamento prematuro de capacidade.** O produtor (`serialize_const`,
   `lower.tks`) continua honest-stopando agregados pointer-bearing até T-B6, então
   `LRodata.relocs` é sempre vazio em compilação real até lá. T-B4 entregar a resolução
   real NÃO faz um const Tier-B existir antes da hora — só garante que, quando T-B6
   abrir o produtor, o wasm32 já grava o ponteiro certo.

**O arm wasm64 fica honest-stopado — pela guarda que JÁ existe.** `wasm_assemble_program`
honest-stopa qualquer programa wasm64 que interne rodata ANTES de chegar ao layout:
`if ptr64 && (… || lmod.rodata.len > (0 to u64) || …) { return wasm_scope_stop_wasm64() }`
(`stackify.tks:5998-5999`; a mensagem, `:5938`, diz "wasm64 today only supports a
program with no LAlloca, no rodata, and no Ptr-typed signature"). Como um const Tier-B
vive em rodata, um programa wasm64 com um só const agregado é honest-stopado por essa
guarda existente, ANTES de `wasm_relocate_rodata`. Logo o arm i64 é **inalcançável
com rodata hoje** e T-B4 não precisa escrevê-lo — implementa só o write i32 do wasm32 e
documenta o i64 como remainder que "pega carona" no `wasm_scope_stop_wasm64` até que
esse scope-stop de wasm64 seja levantado (um follow-up nomeado, fora de T-B4).

---

## 4. Byte-identidade quando não há ponteiro interno (goldens wasm intactos)

Em TODA compilação real, `serialize_const` (`lower.tks`, o produtor) honest-stopa
agregados pointer-bearing (é a fronteira Tier-B do lado produtor, inalterada por
T-B1..T-B4), então **nenhuma `LRodata` carrega `relocs` não-vazio**. Portanto:

1. `wasm_layout_rodata` produz `bytes`/`symbols`/`offsets`/`end` EXATAMENTE como hoje
   (o passo 1 é literalmente o corpo atual, inalterado).
2. `wasm_relocate_rodata` itera as entradas, encontra `rodata[i].relocs.len == 0` em
   todas, **não escreve nada**, e devolve um `WasmRodataLayout` com `segment.bytes`
   igual à lista de entrada — byte-por-byte. (O loop externo roda; o interno nunca
   entra; `write_u32_le_at` nunca é chamado.)
3. `wasm_assemble_program` passa esse layout inalterado a `wasm_symtab_add_rodata` e
   `wasm_program_memory_plan` — `WasmModule.data`, `.mem_min_pages`, `.globals`
   idênticos — e `emit_wasm` produz os MESMOS bytes.

⇒ Cada byte idêntico ao pré-T-B4. Os goldens wasm são a prova viva:
`data_section_matches_wat2wasm_reference` (`objfile_wasm_test.tkt:467`),
`narrow_msg_data_segment_matches_the_traced_golden` (`:490`), os testes de módulo
`assemble_program_*` (`stackify_test.tkt`, ex. `:2847`/`:2886` que fixam
`m.data[i].offset == wasm_rodata_base()`), e `wasm_layout_rodata_*`
(`:2170`/`:2188`/`:2204`) — todos permanecem verbatim. **fixpoint gen2==gen3** + ambas
as engines é a prova final. **QED.**

---

## 5. Fixtures a ADICIONAR (campo-a-campo, padrão dos testes wasm)

Todas em `stackify_test.tkt` (onde vivem os testes de `wasm_layout_rodata`),
construindo `WasmRodataLayout`/`LModule` à mão — o precedente de
`wasm_layout_rodata_places_entries_back_to_back` (`:2170`). Um helper novo constrói uma
`LRodata` COM relocs (o `srt_rodata` existente, `:2158`, força `relocs` vazio):

```teko
/**
 * srt_rodata_rel — uma `LRodata` com relocations internas de ponteiro, o fixture que
 * os testes de `wasm_relocate_rodata` constroem (#594 T-B4).
 *
 * @param str symbol  o símbolo da entrada
 * @param []byte bytes  os bytes crus (com o slot de ponteiro zero-init)
 * @param []lir::LDataReloc relocs  as relocations internas
 * @return lir::LRodata  a entrada
 */
fn srt_rodata_rel(symbol: str, bytes: []byte, relocs: []lir::LDataReloc) -> lir::LRodata {
    lir::LRodata { symbol = symbol; bytes = bytes; relocs = relocs }
}
```

### 5.1 (i) A resolução dispara — slot recebe o endereço absoluto (novo comportamento)

Entrada 0 = slot de ponteiro de 8 bytes zero, com uma reloc no offset 0 → alvo `"s1"`;
entrada 1 = o datum-alvo (`0x41`). base 0 → `offsets[0]=0`, `offsets[1]=8`.

```teko
#test
fn wasm_relocate_rodata_writes_the_target_abs_addr() {
    let slot = [0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte]
    let rodata = [srt_rodata_rel("s0", slot, [lir::data_reloc(0 to u32, "s1")]), srt_rodata("s1", [0x41 to byte])]
    let layout = wasm_layout_rodata(rodata, 0 to u32)
    match wasm_relocate_rodata(layout, rodata) {
        WasmRodataLayout as out => {
            // offsets[1] == 8 → i32 LE nos 4 bytes baixos do slot; word alto zero:
            teko::assert::is_true(wasm_bytes_eq(out.segment.bytes, [0x08 to byte, 0x00 to byte, 0x00 to byte, 0x00 to byte, 0x00 to byte, 0x00 to byte, 0x00 to byte, 0x00 to byte, 0x41 to byte]))
            teko::assert::is_true(out.offsets[0] == (0 to u32) && out.offsets[1] == (8 to u32))
            teko::assert::is_true(out.end == (9 to u32))
        }
        error => teko::assert::is_true(false)
    }
}
```

### 5.2 (ii) Base não-zero — o endereço absoluto inclui a base

Mesma forma, base 100 → `offsets[1] = 108` (`0x6C`) escrito nos 4 bytes baixos do slot.

```teko
#test
fn wasm_relocate_rodata_abs_addr_includes_base() {
    let slot = [0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte]
    let rodata = [srt_rodata_rel("s0", slot, [lir::data_reloc(0 to u32, "s1")]), srt_rodata("s1", [0x41 to byte])]
    let layout = wasm_layout_rodata(rodata, 100 to u32)
    match wasm_relocate_rodata(layout, rodata) {
        WasmRodataLayout as out => teko::assert::is_true(out.segment.bytes[0] == (0x6C to byte) && out.segment.bytes[1] == (0 to byte))
        error => teko::assert::is_true(false)
    }
}
```

### 5.3 (iii) Colapso byte-idêntico quando `relocs` vazio

```teko
#test
fn wasm_relocate_rodata_is_byte_identical_with_no_relocs() {
    let rodata = [srt_rodata("s0", [0x68 to byte, 0x69 to byte]), srt_rodata("s1", [0x21 to byte])]
    let layout = wasm_layout_rodata(rodata, 0 to u32)
    match wasm_relocate_rodata(layout, rodata) {
        WasmRodataLayout as out => teko::assert::is_true(wasm_bytes_eq(out.segment.bytes, [0x68 to byte, 0x69 to byte, 0x21 to byte]))
        error => teko::assert::is_true(false)
    }
}
```

### 5.4 (iv) Alvo desconhecido → erro nomeado (nunca um skip silencioso)

```teko
#test
fn wasm_relocate_rodata_errors_on_unknown_target() {
    let slot = [0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte]
    let rodata = [srt_rodata_rel("s0", slot, [lir::data_reloc(0 to u32, "nope")])]
    let layout = wasm_layout_rodata(rodata, 0 to u32)
    match wasm_relocate_rodata(layout, rodata) {
        WasmRodataLayout => teko::assert::is_true(false)
        error as e => teko::assert::str_contains(e.message, "T-B4")
    }
}
```

### 5.5 (v) `wasm_data_addr_of` unitário — hit e miss

```teko
#test
fn wasm_data_addr_of_resolves_and_errors() {
    match wasm_data_addr_of(["a", "b"], [10 to u32, 20 to u32], "b") { u32 as a => teko::assert::is_true(a == (20 to u32)); error => teko::assert::is_true(false) }
    match wasm_data_addr_of(["a"], [10 to u32], "z") { u32 => teko::assert::is_true(false); error as e => teko::assert::str_contains(e.message, "unknown rodata symbol") }
}
```

### 5.6 (vi) End-to-end no assembler (ambas as engines) — o ponteiro chega ao segment

Um `LModule` cujo rodata carrega uma reloc interna → `wasm_assemble_program(lmod,
false)` → o `WasmModule.data`'s segment contém o endereço absoluto
`wasm_rodata_base() + offset_do_alvo` nos 4 bytes baixos do slot. Prova que a costura
§2.4 encadeia. Roda idêntico na VM e no harness nativo (é um `.tkt` de backend).

```teko
#test
fn assemble_program_resolves_a_rodata_internal_pointer() {
    let slot = [0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte]
    let rodata = [srt_rodata_rel("p", slot, [lir::data_reloc(0 to u32, "t")]), srt_rodata("t", [0x41 to byte])]
    let lmod = lir::LModule { funcs = [sft_main_func()]; rodata = rodata; globals = teko::list::empty(); layouts = teko::list::empty() }
    match wasm_assemble_program(lmod, false) {
        WasmModule as m => {
            // o alvo "t" fica em wasm_rodata_base() + 8 (após o slot de 8 bytes);
            // o narrow-msg segment pode preceder o rodata → o rodata é o ÚLTIMO data segment.
            let seg = m.data[m.data.len - (1 to u64)]
            let want = (wasm_rodata_base() to u32) + (8 to u32)
            teko::assert::is_true(seg.bytes[0] == ((want & (0xFF to u32)) to byte))
            teko::assert::is_true(seg.bytes[1] == (((want >> (8 to u32)) & (0xFF to u32)) to byte))
        }
        error => teko::assert::is_true(false)
    }
}
```

### 5.7 (vii) wasm64 com rodata continua honest-stopado (o arm i64 é bloqueado, não errado)

Prova que o arm wasm64 NÃO cai silenciosamente em `wasm_relocate_rodata` — é barrado
por `wasm_scope_stop_wasm64` a montante.

```teko
#test
fn assemble_program_wasm64_with_rodata_honest_stops_before_reloc() {
    let slot = [0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte]
    let rodata = [srt_rodata_rel("p", slot, [lir::data_reloc(0 to u32, "t")]), srt_rodata("t", [0x41 to byte])]
    let lmod = lir::LModule { funcs = [sft_main_func()]; rodata = rodata; globals = teko::list::empty(); layouts = teko::list::empty() }
    match wasm_assemble_program(lmod, true) {
        WasmModule => teko::assert::is_true(false)
        error as e => teko::assert::str_contains(e.message, "wasm64")
    }
}
```

> Cobertura do delta: §5.1/§5.2/§5.6 cobrem o braço com-reloc (wasm32); §5.3 o colapso
> vazio; §5.4/§5.5 os dois braços de `wasm_data_addr_of` (hit/miss) e o erro nomeado;
> §5.7 a guarda wasm64. Os goldens existentes cobrem o braço `relocs` vazio no
> `emit_wasm`. 100% do delta.

---

## 6. Sequência de edits + pontos de regressão

Cada edit é gate-able isoladamente; rodar o `.tkt` listado após cada um.

| # | Arquivo | Edit | Gate de regressão (`.tkt`) |
|---|---|---|---|
| E1 | `src/backend/stackify.tks` | `wasm_data_addr_of` + `wasm_relocate_rodata` (§2.3), após `wasm_layout_rodata` (`:4947`); reusa `write_u32_le_at` | `stackify_test.tkt` compila |
| E2 | `src/backend/stackify.tks` | costura em `wasm_assemble_program` (`:6002`): `wasm_layout_rodata` → `wasm_relocate_rodata` via `match` (§2.4) | `stackify_test.tkt` (goldens de módulo byte-idênticos, `relocs` vazio); `objfile_wasm_test.tkt` |
| E3 | `src/backend/stackify_test.tkt` | helper `srt_rodata_rel` + fixtures §5.1–§5.7 | os próprios testes novos (verde) |

> `objfile_wasm.tks` **NÃO é tocado** (§correção de linha) — o segment já chega
> patcheado. Nenhum encoder nativo, writer ELF/Mach-O/COFF, ou VM é tocado (esses são
> T-B1/T-B2/T-B3/T-B5). O honest-stop `encode_rodata` de T-B1 **permanece** intocado
> (protege os caminhos NATIVOS; o wasm nunca passou por ele).

**Ritual points:**

- **Por-edit:** o `.tkt` do arquivo (tabela) — cada edit é gate-able só.
- **RITUAL POINT — fim de T-B4:** gate COMPLETO — todos os goldens wasm byte-idênticos
  (`stackify_test.tkt`, `objfile_wasm_test.tkt`) + os demais goldens de backend
  (encoders/writers/`lower`/`lir_interp`/`tkb`) intactos + **fixpoint gen2==gen3** +
  ambas as engines (VM + nativo) + 100% de cobertura do delta (§5). **Sem seed bump** —
  T-B4 não adiciona capacidade que o corpus use (o 🔑 SEED BUMP #3 é depois de T-B5,
  plano §8).

---

## 7. Riscos + tensões (com resolução)

1. **Miscompile silencioso: `wasm_layout_rodata` hoje IGNORA `relocs`.** É a razão de
   T-B4 existir. *Resolução:* a resolução real (§2.3) + o erro nomeado de alvo
   desconhecido (`wasm_data_addr_of`) tornam impossível um slot ficar zerado ou apontar
   errado; a fixture §5.4 fixa o erro. Nunca um skip silencioso.

2. **Acesso cross-file a `write_u32_le_at` (privado, `encode_x86_64.tks:2084`).** É
   load-bearing: T-B4 o reusa de `stackify.tks`. *Resolução:* mesmo namespace
   `teko::backend` (path-derivado; nenhum arquivo declara `namespace`), e
   `objfile_coff.tks` (mesmo namespace) já o chama unqualified (`:487`/`:530`) — acesso
   same-namespace-private confirmado. Se por algum motivo o build reclamar, alternativa
   inerte: um `wasm_write_u32_le_at` local em `stackify.tks` (cópia verbatim de 12
   linhas). Recomendado reusar (W15, sem duplicação).

3. **Índice do patch: `(offsets[i] - base) + rel.offset`.** Um erro aqui gravaria no
   slot errado. *Resolução:* `offsets[i]` é absoluto e `base = layout.segment.offset`,
   então `offsets[i] - base` é o início segment-relativo exato da entrada `i`; a
   fixture §5.2 (base 100) e §5.6 (base real) provam o cálculo com base ≠ 0.

4. **Largura do slot (8 vs 4 bytes) depende do serializer T-B6.** *Soft tension, não
   HALT.* O write i32 de T-B4 é correto para AMBAS: se T-B6 reservar 8 bytes
   (paridade-nativa), o word alto zero-init fica intacto; se reservar 4, o write
   preenche o slot exato. §2.2. wasm32 é independente da decisão de T-B6.

5. **wasm64/i64 não implementado.** *Resolução (não é gap):* honest-stopado a montante
   por `wasm_scope_stop_wasm64` para QUALQUER programa wasm64 com rodata
   (`stackify.tks:5998`); a fixture §5.7 prova o barramento. O write i64 é um remainder
   nomeado, desbloqueado só quando o scope-stop de wasm64-com-rodata for levantado —
   fora de T-B4.

6. **`RelocSect` (T-B1) não é consumido no wasm.** *Resolução:* correto e intencional —
   o wasm não constrói `Reloc*` (não usa `EncodedModule`); consome `LDataReloc`
   diretamente da LIR (§2.1). O discriminador `RelocSect` serve os writers nativos
   (partição em T-B2/T-B3); o wasm não precisa dele. Sem tensão.

**Sem tensão genuína não resolvida → sem HALT.**

---

## 8. O que permanece BLOQUEADO (adiantado até o limite)

T-B4 entrega o caminho wasm32 COMPLETO e provado hoje (o ponteiro data→data escrito no
segment ativo, testado por `WasmRodataLayout`/`LModule` à mão). O que resta atrás de
gates a montante (não faz parte de T-B4, mas nomeado):

- **Popular `LRodata.relocs`** a partir de um const Tier-B real: `serialize_const`
  (`lower.tks`) deixar de honest-stopar e emitir bytes + `data_reloc` entries. É o
  crumb produtor (T-B6, atrás do 🔑 SEED BUMP #3 pós-T-B5). Só então
  `wasm_relocate_rodata` recebe `relocs` não-vazio em compilação real. Até lá, o caminho
  é exercitado só pelas fixtures §5.
- **T-B5 (VM):** resolver o ponteiro rodata-interno no `LGlobalAddr`/load tipado do
  interpretador (`lir_interp.tks:527`) — completa a cadeia junto com os writers.
- **wasm64/i64:** o write de 8 bytes, quando o `wasm_scope_stop_wasm64`-com-rodata for
  levantado (§7.5). Nada disso bloqueia T-B4 — o wasm32 está pronto.
