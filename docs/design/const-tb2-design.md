# T-B2 — writer ELF emite `.rela.rodata` (#594 Tier-B)

Status: READY-TO-IMPLEMENT (architect, 2026-07-18). Track: Tier-B pointer-bearing
aggregate → rodata (`docs/design/const-module-level-plan.md` §8 crumb T-B2, §5.1
verdict, DECISION_LOG D2). Base: o working tree da const-wave com **T-B1 já mergeado**
(`RelocSect`/`sect` nos três `Reloc*`, `LDataReloc`/`LRodata.relocs` na LIR,
honest-stop `honest_data_reloc` em `encode_rodata`). Predecessor:
`docs/design/const-tb1-design.md`.

> Escopo de T-B2 (verbatim §8): "ELF writer: emit a `.rela.rodata` section (add to
> the section set at `objfile_elf.tks:455`) + its relas." Este crumb dá ao writer ELF
> a CAPACIDADE de emitir `.rela.rodata`; **nenhum produtor a exercita ainda** — o
> honest-stop de T-B1 em `encode_rodata` permanece e só abre em T-B5 (quando a cadeia
> encoder→writer→VM estiver completa). Como hoje `rodata_relocs` chega sempre vazia
> aos writers, o objeto é **byte-idêntico** e todos os goldens/fixpoint ficam intactos.

---

## 0. DECISÃO CENTRAL — até onde T-B2 abre o gate: **opção (a)** (só o writer)

**RECOMENDAÇÃO (law-first): opção (a).** T-B2 adiciona o eixo rodata ao writer ELF
(`ElfObject`/`ElfLayout` + `emit_elf_object`), testado por `ElfObject` construído à
mão; o honest-stop de `encode_rodata` **FICA**. A opção (b) — descer o fio por
`encode_rodata`→`x86_reloc_reqs` e mover o honest-stop para os writers não-ELF — é
**incorreta e insegura** para este crumb. Justificativa pelas Leis:

1. **Smallest safe step + independência de crumb (Lei "issues são 100%", gate-abilidade
   por passo).** A proposta de T-B2 é exatamente "o writer ELF emite `.rela.rodata`".
   A opção (b) removeria o único choke point (`encode_rodata`) que protege os writers
   AINDA-INCOMPLETOS. Mach-O/COFF (T-B3), wasm (T-B4) e a VM (T-B5) não sabem resolver
   um ponteiro rodata-interno. Se o honest-stop descesse para "o writer não-ELF" em
   T-B2, um módulo Tier-B compilado para Mach-O/COFF/wasm/VM passaria a depender de
   guardas espalhadas em quatro lugares diferentes, em vez de um só — e um esquecimento
   emitiria **bytes errados silenciosamente**. O honest-stop em `encode_rodata` é o
   ponto mais estreito e deve permanecer até o ÚLTIMO writer/VM (T-B5). Isto é
   exatamente o que a decisão §2.5 do doc T-B1 já ratificou ("T-B1's honest-stop lives
   at `encode_rodata` … The writer `sect` handling is introduced by the crumb that
   actually emits it") e o que este brief antecipou ("o honest-stop FICA, abre em
   T-B5").

2. **Costura consistente entre os 3 writers (o requisito do brief).** A opção (a) fixa
   a costura correta e uniforme: cada objeto neutro de writer ganha uma **lista
   paralela de relocations rodata-internas** (`ElfObject.rodata_relocs`), separada da
   lista `.text`-relativa existente. Quando a cadeia completa (crumb futuro), o bridge
   `x86_reloc_reqs`/`riscv_reloc_reqs` (e os equivalentes Mach-O/COFF) **particiona os
   `Reloc*` por `.sect`** em duas listas: `Text`→`relocs`, `Rodata`→`rodata_relocs`.
   O discriminador `RelocSect` (T-B1) é **consumido no bridge**; o writer recebe listas
   já pré-classificadas, com semânticas de offset fisicamente separadas
   (`.text`-relativo vs `.rodata`-relativo). T-B3 (Mach-O/COFF) e T-B4 (wasm) adotam a
   MESMA lista-paralela nos seus objetos neutros. Isto é mais limpo e menos superfície
   de erro do que carregar um `sect` no request neutro e particionar em tempo de
   emissão em cada writer.

3. **Precedente estabelecido.** `el_abs64_module` (`objfile_elf_test.tkt:284`) e
   `el_rodata_off_module` (`:246`) já constroem entradas à mão para fixar codificações
   que `encode_module_x86` honest-stopa antes de produzir. T-B2 segue o MESMO padrão:
   `ElfObject` à mão com um `rodata_reloc` alimenta `emit_elf_object` (que é `pub fn`),
   fixando os bytes de `.rela.rodata` sem tocar no encoder nem no honest-stop.

Nada em (a) precisa da API bloqueada (produtor de `LDataReloc`); tudo o que resta
bloqueado é apenas a POPULAÇÃO da `rodata_relocs` a partir de um const Tier-B real
(crumb futuro, atrás do honest-stop). O writer fica pronto e provado hoje.

---

## 1. Estado atual (provado com linhas)

### 1.1 O "section set" — sete seções, índices fixos

`elf_section_names()` (`objfile_elf.tks:524`) devolve, em ordem de section-header:

```
["", ".text", ".rodata", ".symtab", ".strtab", ".shstrtab", ".rela.text"]
   0     1        2          3          4           5             6
```

- `e_shnum = 7` (hardcoded `emit_elf_header` `:707`), `e_shstrndx = 5` (`.shstrtab`,
  `:708`). Há **`.rela.text` mas nenhum `.rela.rodata`** — o objeto não consegue
  carregar uma relocation aplicada DENTRO de `.rodata` (§5.1 do plano).

### 1.2 Como `.rela.text` é emitida hoje

- **Requests neutros:** `ElfRelocReq { offset; sym; rtype; addend }`
  (`objfile_elf.tks:878`), com `offset` documentado como `.text`-section-relative
  (`:882`). `ElfObject` (`:911`) carrega `text`, `rodata`, `symbols`, `relocs:
  []ElfRelocReq` (`:937`).
- **Resolução:** `elf_build_relas(obj, syms)` (`:490`) percorre `obj.relocs`; para cada
  request faz `elf_rodata_hit(obj.symbols, r.sym)` (`:432`) — se o alvo é um local
  section-2 (`.rodata`), re-aponta para o **símbolo de seção `.rodata`** (`secidx =
  elf_rodata_secsym_index`, `:491`) e **soma o offset do datum ao addend** (`:498-499`);
  senão resolve o índice do símbolo (`elf_symbol_index`). Produz `ElfRela { offset;
  symidx; rtype; addend }` (`:453`).
- **Emissão dos bytes:** `emit_elf_relas(buf, relas)` (`:853`) escreve por linha
  `r_offset`(u64), `r_info = (symidx<<32)|rtype` (`elf_rela_info` `:513`), `r_addend`
  (i64, via `push_imm64_x86`).
- **Header da seção:** em `emit_elf_shdrs` (`:792`) a linha `.rela.text` (`:799`) é
  `sh_type=RELA(4)`, `sh_flags=SHF_INFO_LINK(0x40)`, `sh_offset=lay.rela_offset`,
  `sh_size=lay.rela_size`, **`sh_link=3` (`.symtab`)**, **`sh_info=1` (`.text`)**,
  `sh_addralign=8`, `sh_entsize=24`.
- **Layout físico** (`compute_elf_layout` `:624`, emissão `emit_elf_object` `:956`):
  `[Ehdr(64)][.text][pad8][.rodata][pad8][.symtab][.strtab][.shstrtab][pad8]
  [.rela.text][pad8][SHT]`. `rela_offset = align8(shstrtab_offset + shstrtab_len)`,
  `rela_size = nrela*24`, `shoff = align8(rela_offset + rela_size)`.
- **Bridges (onde entrariam os offsets rodata-relativos):** `x86_reloc_reqs`
  (`:988`) e `riscv_reloc_reqs` (`objfile_elf_riscv.tks:15`) mapeiam cada `RelocX86`/
  `RelocRiscv` → `ElfRelocReq`, com `rtype = elf_reloc_type(r.kind)` /
  `riscv_reloc_type(r.kind)`. Hoje **só produzem relocations `.text`-relativas**.
  `elf_reloc_type(Abs64)=1` (`:392`, R_X86_64_64) e `riscv_reloc_type(Abs64)=2`
  (`encode_riscv.tks:746`, R_RISCV_64) — o tipo absoluto que um ponteiro data→data usa
  JÁ existe e está mapeado.
- **Símbolo de seção `.rodata`:** `elf_build_symbols` (`:287`) emite UM símbolo
  `STT_SECTION` para `.rodata` no índice **1** (logo após o null, quando há rodata,
  `:289-290`); os locais rodata-nomeados NÃO viram símbolos individuais. `first_global`
  = `elf_first_global_index` (`:326`).

### 1.3 Compartilhamento x86/riscv

`emit_elf_object` (`:956`) é ISA-agnóstico. `emit_elf` (`:1013`) e `emit_elf_riscv`
(`objfile_elf_riscv.tks:43`) constroem `ElfObject` e delegam. **Uma única edição do
writer serve as duas ISAs**: o mesmo `.rela.rodata` sai para x86-64 (R_X86_64_64) e
riscv64 (R_RISCV_64), com `objfile_elf_riscv_test.tkt` como gate paralelo. Os DOIS
sites de construção de `ElfObject` (`:1014`, `objfile_elf_riscv.tks:44`) são os únicos
em produção; nenhum teste constrói `ElfObject` diretamente hoje.

---

## 2. Assinaturas exatas do widening + emissão da nova seção (W15 verbatim)

### 2.0 Prova do reloc type (data→data, x86-64 e riscv64)

**x86-64:** o campo dentro de `.rodata` guarda o ENDEREÇO ABSOLUTO de 64 bits de outro
datum de `.rodata`. Pela System V AMD64 psABI (tabela "Relocation Types"):
`R_X86_64_64` = **value 1**, field `word64`, cálculo `S + A`. Num objeto `ET_REL`,
`S` = endereço do símbolo de seção `.rodata`, `A` = offset do datum-alvo dentro de
`.rodata` ⇒ `S + A` = endereço absoluto do alvo. É exatamente o idioma
"section-symbol + addend" que o writer JÁ usa para text→rodata (só que aqui com o tipo
absoluto `64` em vez de `PC32`, e r_offset dentro de `.rodata`). Isto é o que `gas`
emite para um `.quad label` com `label` local em `.rodata`. `elf_reloc_type(Abs64)=1`
já cobre.

**riscv64:** o análogo absoluto de 64 bits é `R_RISCV_64` = **value 2**, `S + A`
(RISC-V ELF psABI). `riscv_reloc_type(Abs64)=2` já cobre. **Nenhum tipo de reloc novo é
necessário** — o `rtype` numérico chega pré-mapeado ao writer ISA-agnóstico.

### 2.1 `ElfObject` ganha o eixo rodata: `rodata_relocs` (`objfile_elf.tks:911`)

Adicionar UM campo, doc'd, após `relocs`. Lista separada (não um `sect` no request):
as duas listas têm semânticas de offset distintas (`.text`-relativo vs
`.rodata`-relativo) e a lista vazia torna a byte-identidade trivial.

```teko
    /**
     * rodata_relocs — as relocations cujo PATCH SITE fica DENTRO de `.rodata` (um
     * ponteiro data→data de um const Tier-B; #594 T-B2), separadas das `.text`-
     * relativas em `relocs` porque o `offset` de cada uma é `.rodata`-base-relativo,
     * não `.text`-base-relativo. VAZIA em toda compilação real de hoje (o honest-stop
     * `encode_rodata` de T-B1 impede qualquer produtor de povoá-la), o que mantém o
     * objeto byte-idêntico e todos os goldens intactos; o bridge por-ISA a popula
     * (particionando os `Reloc*` por `RelocSect`) só quando a cadeia T-B abrir. Cada
     * entrada é resolvida como uma linha de `.rela.rodata` (R_X86_64_64 / R_RISCV_64
     * contra o símbolo de seção `.rodata`, com o offset do alvo dobrado no addend).
     */
    rodata_relocs: []ElfRelocReq
```

O doc do campo `offset` de `ElfRelocReq` (`:882`) generaliza (mudança só de doc,
byte-inerte): de "`.text`-section-relative byte offset" para

```teko
    /**
     * offset — o byte offset do campo relocado, relativo à base da seção que a
     * relocation ataca: `.text`-base-relativo em `ElfObject.relocs`, `.rodata`-base-
     * relativo em `ElfObject.rodata_relocs` (#594 T-B2).
     */
    offset: u32
```

Edições de literal (ambos os sites de produção passam a lista vazia):

- `emit_elf` (`objfile_elf.tks:1014`): adicionar `rodata_relocs = teko::list::empty()`.
- `emit_elf_riscv` (`objfile_elf_riscv.tks:44`): idem.

### 2.2 `ElfLayout` ganha os campos de `.rela.rodata` (`objfile_elf.tks:538`)

```teko
    /**
     * rela_rodata_offset — o file offset 8-alinhado da seção `.rela.rodata`
     * (logo após `.rela.text`), ou o valor 8-alinhado de fim-de-`.rela.text` quando
     * não há relocation rodata-interna (nesse caso nada é emitido ali) (#594 T-B2).
     */
    rela_rodata_offset: u32
    /**
     * rela_rodata_size — o tamanho em bytes de `.rela.rodata` (`nrela_rodata * 24`);
     * 0 quando não há relocation rodata-interna, tornando o objeto byte-idêntico ao
     * pré-T-B2 (#594 T-B2).
     */
    rela_rodata_size: u32
    /**
     * nrela_rodata — a contagem de `Elf64_Rela` em `.rela.rodata` (0 hoje) (#594 T-B2).
     */
    nrela_rodata: u32
    /**
     * nsects — o número de section headers (`e_shnum`): 7 sem `.rela.rodata`, 8 com
     * (#594 T-B2). Derivado da contagem de nomes de seção, evitando o literal 7.
     */
    nsects: u32
```

### 2.3 `elf_section_names` condicional (`objfile_elf.tks:524`)

`.rela.rodata` é anexada como índice **7** (por último), SÓ quando há relocation
rodata-interna. Anexar por último é a ÚNICA posição que preserva os índices 0–6 e
`e_shstrndx=5` (inserir antes deslocaria índices e quebraria `sh_link`/`sh_info` e
`e_shstrndx`). Quando ausente, `.shstrtab` não recebe a string `.rela.rodata`, então
os bytes de `.shstrtab` — e todos os offsets subsequentes — ficam idênticos.

```teko
/**
 * elf_section_names — os nomes das seções em ordem de section-header-table (`""`,
 * `.text`, `.rodata`, `.symtab`, `.strtab`, `.shstrtab`, `.rela.text`), com
 * `.rela.rodata` anexada por último (índice 7) SÓ quando `has_rodata_relocs` — a
 * posição final é a única que preserva os índices 0–6 e `e_shstrndx=5`; quando não há
 * relocation rodata-interna a string não entra em `.shstrtab`, mantendo o objeto
 * byte-idêntico ao pré-T-B2 (#594 T-B2). Interned em `.shstrtab` para render cada
 * `sh_name`.
 *
 * @param has_rodata_relocs  se há ao menos uma relocation cujo patch site é `.rodata`
 * @return  os nomes das seções, em ordem de header (7 ou 8)
 */
fn elf_section_names(has_rodata_relocs: bool) -> []str {
    let base = ["", ".text", ".rodata", ".symtab", ".strtab", ".shstrtab", ".rela.text"]
    if has_rodata_relocs { return teko::list::push(base, ".rela.rodata") }
    base
}
```

### 2.4 `emit_elf_header` recebe `nsects` (`objfile_elf.tks:680`)

`e_shnum` deixa de ser o literal `7` (`:707`) e passa a ser `nsects`; `e_shstrndx`
segue `5` (`.shstrtab` não muda de índice). Assinatura:

```teko
fn emit_elf_header(buf: []byte, lay: ElfLayout, e_machine: u32, e_flags: u32, nsects: u32) -> []byte
```

Corpo: trocar `b = emit_u16_le_elf(b, 7 to u32)` por `b = emit_u16_le_elf(b, nsects)`.
Callsite `emit_elf_object` (`:964`) passa `lay.nsects`. Byte-inerte quando
`nsects==7`.

### 2.5 `compute_elf_layout` computa `.rela.rodata` (`objfile_elf.tks:624`)

```teko
fn compute_elf_layout(obj: ElfObject, nsyms: u32, nrela: u32, nrela_rodata: u32, strtab_len: u32, shstrtab_len: u32, first_global: u32, nsects: u32) -> ElfLayout
```

Corpo (após `rela_size`, antes de `shoff`):

```teko
    let rela_rodata_offset = align_up(rela_offset + rela_size, 8 to u32)
    let rela_rodata_size = nrela_rodata * (24 to u32)
    let shoff = align_up(rela_rodata_offset + rela_rodata_size, 8 to u32)
```

e os novos campos no literal `ElfLayout { … rela_rodata_offset = rela_rodata_offset;
rela_rodata_size = rela_rodata_size; nrela_rodata = nrela_rodata; nsects = nsects }`.
Prova de identidade: com `nrela_rodata=0`, `rela_rodata_offset = align8(rela_offset +
rela_size)` (= antigo `shoff`), `rela_rodata_size=0`, `shoff = align8(mesmo valor)` =
antigo `shoff`. Idêntico.

### 2.6 Resolução das relas rodata — extrair `elf_resolve_rela` + `elf_build_rodata_relas`

A resolução por-reloc (`elf_rodata_hit` → re-target do símbolo de seção + dobra do
offset no addend + índice de símbolo) é IDÊNTICA para text e rodata. Extrair o corpo
do loop de `elf_build_relas` (`:495-501`) num helper e reusá-lo nos dois builders
(o corpo extraído é o corpo atual verbatim — os goldens de `.rela.text` provam a
preservação de comportamento):

```teko
/**
 * elf_resolve_rela — resolve UM `ElfRelocReq` `r` numa linha `Elf64_Rela`: se `r.sym`
 * é um datum `.rodata` local (section-2), re-aponta para o símbolo de seção `.rodata`
 * (`secidx`) e dobra o offset do datum no addend; senão toma o índice pós-partição do
 * símbolo alvo. O `rtype` numérico e o `r.offset` (base-relativo à seção da relocation)
 * passam intactos. É o corpo compartilhado por `.rela.text` e `.rela.rodata` (#594 T-B2).
 *
 * @param r        o request neutro de relocation
 * @param symbols  a lista de símbolos neutros do objeto (para `elf_rodata_hit`)
 * @param syms     a tabela de símbolos resolvida (para os índices)
 * @param secidx   o índice do símbolo de seção `.rodata`
 * @return         a linha `Elf64_Rela` resolvida
 */
fn elf_resolve_rela(r: ElfRelocReq, symbols: []Symbol, syms: []ElfSym, secidx: u32) -> ElfRela {
    let hit = elf_rodata_hit(symbols, r.sym)
    let symidx = if hit.found { secidx } else { elf_symbol_index(syms, r.sym) }
    let addend = if hit.found { r.addend + (hit.offset to i64) } else { r.addend }
    ElfRela { offset = r.offset; symidx = symidx; rtype = r.rtype; addend = addend }
}

/**
 * elf_build_rodata_relas — resolve cada `rodata_relocs` de `obj` numa linha
 * `Elf64_Rela` de `.rela.rodata`: um ponteiro data→data cujo alvo é um datum `.rodata`
 * local vira R_X86_64_64/R_RISCV_64 contra o símbolo de seção `.rodata` com o offset
 * do alvo dobrado no addend, e cujo `r_offset` é o byte offset do slot de ponteiro
 * DENTRO de `.rodata` (#594 T-B2). Vazia hoje (o honest-stop `encode_rodata` de T-B1
 * impede qualquer produtor), então `.rela.rodata` nunca é emitida em compilação real.
 *
 * @param obj   o objeto neutro (`rodata_relocs` + símbolos)
 * @param syms  a tabela de símbolos resolvida (para os índices)
 * @return      as linhas de relocation de `.rela.rodata`, em ordem de emissão
 */
fn elf_build_rodata_relas(obj: ElfObject, syms: []ElfSym) -> []ElfRela {
    let secidx = elf_rodata_secsym_index(syms)
    mut out: []ElfRela = teko::list::empty()
    mut i: u64 = 0
    loop {
        if i >= obj.rodata_relocs.len { break }
        out = teko::list::push(out, elf_resolve_rela(obj.rodata_relocs[i], obj.symbols, syms, secidx))
        i++
    }
    out
}
```

`elf_build_relas` (`:490`) passa a chamar `elf_resolve_rela` no seu loop (mesmo
comportamento; os goldens de `.rela.text` são o guardrail). Alternativa de risco mínimo
se o implementer preferir NÃO tocar `elf_build_relas`: duplicar o loop-body em
`elf_build_rodata_relas` (leve duplicação, zero edição no fn byte-crítico). Recomendado
extrair (W15, menor complexidade ciclomática), com os goldens como prova.

### 2.7 `emit_elf_shdrs` emite o 8º header (`objfile_elf.tks:792`)

Após a linha `.rela.text` (`:799`), emitir condicionalmente o header de `.rela.rodata`
(índice 7): `sh_type=RELA(4)`, `sh_flags=SHF_INFO_LINK(0x40)`, **`sh_link=3`
(`.symtab`)**, **`sh_info=2` (`.rodata`)**, `sh_addralign=8`, `sh_entsize=24`.

> Prova de `sh_link`/`sh_info` (SysV gABI, "sh_link and sh_info Interpretation", SHT_RELA):
> `sh_link` = índice da tabela de símbolos associada (`.symtab` = 3); `sh_info` = índice
> da seção à qual a relocation se aplica (`.rodata` = 2). `.rela.text` usa `sh_info=1`
> (`.text`); `.rela.rodata` usa `sh_info=2` (`.rodata`).

```teko
    if shname.len > (7 to u64) {
        b = emit_elf_shdr(b, shname[7], 4 to u32, SHF_INFO_LINK, lay.rela_rodata_offset, lay.rela_rodata_size, 3 to u32, 2 to u32, 8 to u32, 24 to u32)
    }
```

(Retorna `b` no fim; a última linha de `.rela.text` deixa de ser o `return` implícito —
passa a `b = emit_elf_shdr(...)` seguida do bloco condicional e de `b` como valor final.)

### 2.8 `emit_elf_object` costura tudo (`objfile_elf.tks:956`)

```teko
pub fn emit_elf_object(obj: ElfObject) -> []byte {
    let has_rr = obj.rodata_relocs.len > (0 to u64)
    let syms = elf_build_symbols(obj.symbols)
    let strtab = build_elf_strtab(elf_sym_names(syms))
    let secnames = elf_section_names(has_rr)
    let shstrtab = build_elf_strtab(secnames)
    let relas = elf_build_relas(obj, syms)
    let rodata_relas = elf_build_rodata_relas(obj, syms)
    let first_global = elf_first_global_index(syms)
    let lay = compute_elf_layout(obj, syms.len to u32, relas.len to u32, rodata_relas.len to u32, strtab.bytes.len to u32, shstrtab.bytes.len to u32, first_global, secnames.len to u32)
    mut b: []byte = teko::list::empty()
    b = emit_elf_header(b, lay, obj.e_machine, obj.e_flags, lay.nsects)
    b = append_bytes(b, obj.text)
    b = pad_to_mult(b, 8 to u32)
    b = append_bytes(b, obj.rodata)
    b = pad_to_mult(b, 8 to u32)
    b = emit_elf_symtab(b, syms, strtab)
    b = append_bytes(b, strtab.bytes)
    b = append_bytes(b, shstrtab.bytes)
    b = pad_to_mult(b, 8 to u32)
    b = emit_elf_relas(b, relas)
    if has_rr {
        b = pad_to_mult(b, 8 to u32)
        b = emit_elf_relas(b, rodata_relas)
    }
    b = pad_to_mult(b, 8 to u32)
    emit_elf_shdrs(b, lay, shstrtab.strx)
}
```

(Doc-comment de `emit_elf_object` atualiza "seven section images" → "seven ou eight",
mencionando `.rela.rodata` condicional; mudança só de doc.)

---

## 3. PROVA de byte-identidade quando não há rodata-reloc

Nenhum produtor emite um `rodata_relocs` não-vazio hoje: os bridges `x86_reloc_reqs`/
`riscv_reloc_reqs` setam `rodata_relocs = teko::list::empty()`, e o honest-stop
`encode_rodata` de T-B1 impede qualquer const Tier-B de existir a montante. Logo, em
TODA compilação real `has_rr = false`, e:

1. **`.shstrtab` idêntico:** `elf_section_names(false)` devolve os MESMOS 7 nomes (a
   string `.rela.rodata` não é anexada) ⇒ `shstrtab.bytes` e `shstrtab_len` idênticos.
2. **Ehdr idêntico:** `nsects = 7` ⇒ `e_shnum = 7`; `.rela.rodata` (se existisse) seria
   índice 7, DEPOIS de `.shstrtab`(5), então `e_shstrndx = 5` inalterado; `shoff` (§2.5)
   colapsa ao valor antigo.
3. **Layout físico idêntico:** o bloco `if has_rr { … }` é pulado; a sequência de
   emissão vira `…[.rela.text][pad8][SHT]` — exatamente a de hoje. `emit_elf_relas(b,
   relas)` é a única emissão de relas; o `pad_to_mult` final é o mesmo pad-antes-do-SHT
   de hoje.
4. **SHT idêntico:** `emit_elf_shdrs` não emite o 8º header (`shname.len==7`) ⇒ SHT =
   7×64 bytes como antes.

⇒ Cada byte idêntico ao pré-T-B2. Os goldens de `objfile_elf_test.tkt` /
`objfile_elf_riscv_test.tkt` (ex.: `exit`-object = 688 bytes, `shoff=240`, `e_shnum=7`,
`e_shstrndx=5`) e o **fixpoint gen1==gen2** são a prova viva. **QED.**

---

## 4. Fixtures a ADICIONAR

Todas em `objfile_elf_test.tkt` (x86) com espelho em `objfile_elf_riscv_test.tkt`
(riscv), construindo `ElfObject` À MÃO e chamando o `pub fn emit_elf_object`
diretamente — o padrão-precedente de `el_abs64_module`/`el_rodata_off_module`, agora um
nível abaixo (no writer neutro) porque o eixo rodata vive em `ElfObject`, não em
`EncodedModuleX86`. Rodam idênticas na VM e no harness nativo (mesma saída de bytes).

### 4.1 (i) `ElfObject` com 1 rodata-reloc → bytes de `.rela.rodata` (novo comportamento)

Fixture mínimo: objeto rodata-only, `.rodata` de 16 bytes = slot de ponteiro (8 bytes
zero, offset 0) + datum-alvo (`0x41` + 7 zeros, offset 8); um `rodata_reloc` no slot 0
apontando para o alvo em offset 8.

```teko
fn eo_rodata_ptr_object() -> ElfObject {
    let rodata = [0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0x41 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte, 0 to byte]
    let ptr = Symbol { name = "ptr"; defined = true; sect = 2 to u8; offset = 0 to u32; local = true }
    let target = Symbol { name = "target"; defined = true; sect = 2 to u8; offset = 8 to u32; local = true }
    let rr = ElfRelocReq { offset = 0 to u32; sym = "target"; rtype = 1 to u32; addend = 0 to i64 }
    ElfObject { e_machine = EM_X86_64; e_flags = 0 to u32; text = teko::list::empty(); rodata = rodata; symbols = [ptr, target]; relocs = teko::list::empty(); rodata_relocs = [rr] }
}
```

**Resolução esperada** (`elf_build_rodata_relas`): `elf_rodata_hit("target")` →
found, offset 8; símbolo de seção `.rodata` no índice **1**; addend `0 + 8 = 8`;
rtype 1. Linha `.rela.rodata`: `r_offset=0`, `r_info=(1<<32)|1`, `r_addend=8`.

**Layout derivado** (`text=[]`, `nsyms=2` [null + secsym `.rodata`], `nrela=0`,
`nrela_rodata=1`, `e_shnum=8`): `.rodata` 64..80; `.symtab` 80..128; `.strtab` 128..129
(só o NUL); `.shstrtab` 129..194 (65 bytes, inclui `.rela.rodata\0` no offset 52);
pad→200; `.rela.text` vazia em 200; `.rela.rodata` **em 200..224**; pad→224;
`SHT` 224..736. Total **736 bytes**.

Asserts campo-a-campo (mesmo estilo dos testes ELF; o implementer confirma os
literais na 1ª run verde e os fixa):

```
obj.len == 736
el_u16_at(obj, 60) == 8            // e_shnum
el_u16_at(obj, 62) == 5            // e_shstrndx (inalterado)
el_u32_at(obj, 40) == 224          // e_shoff
// linha de .rela.rodata em 200:
el_u32_at(obj, 200) == 0           // r_offset (slot de ponteiro em .rodata[0])
el_u32_at(obj, 208) == 1           // r_info baixo = rtype R_X86_64_64
el_u32_at(obj, 212) == 1           // r_info alto = symidx (símbolo de seção .rodata)
el_u32_at(obj, 216) == 8           // r_addend = offset do alvo dobrado
// 8º section header (.rela.rodata) em 224 + 7*64 = 672:
el_u32_at(obj, 672) == 52          // sh_name (offset de ".rela.rodata" em .shstrtab)
el_u32_at(obj, 676) == 4           // sh_type = RELA
el_u32_at(obj, 680) == 0x40        // sh_flags = SHF_INFO_LINK
el_u32_at(obj, 696) == 200         // sh_offset
el_u32_at(obj, 704) == 24          // sh_size = 1*24
el_u32_at(obj, 712) == 3           // sh_link = .symtab
el_u32_at(obj, 716) == 2           // sh_info = .rodata
el_u32_at(obj, 728) == 24          // sh_entsize
```

Espelho riscv em `objfile_elf_riscv_test.tkt`: idêntico, mas `e_machine=EM_RISCV`,
`e_flags=EF_RISCV_FLOAT_ABI_DOUBLE`, `rtype = 2` (R_RISCV_64) — assert `el_u32_at(...,
208) == 2`. (Os demais offsets são iguais; `e_flags` não muda o tamanho.)

### 4.2 (ii) `ElfObject` SEM rodata-reloc → objeto byte-idêntico ao golden

Reafirma §3 com um teste dedicado: construir o MESMO `eo_rodata_ptr_object` mas com
`rodata_relocs = teko::list::empty()`, e assertar `e_shnum==7`, `e_shstrndx==5`, e que
**não existe** um 8º header (o objeto termina no SHT de 7×64). Além disso, os goldens
EXISTENTES (`elf_header_is_x86_64_relocatable` etc.) permanecem verbatim — são a prova
primária. Nenhum golden atual muda de valor.

### 4.3 (iii) sanity estilo readelf

Não há precedente de `readelf` externo na suíte — os testes ELF validam por leitura de
campo (`el_u32_at`/`el_u16_at`), que É o método de sanity. As asserts de §4.1 sobre o
8º header (`sh_type=4`, `sh_link=3`, `sh_info=2`, `sh_entsize=24`) são o equivalente
in-suite de `readelf -S`/`readelf -r`; nenhuma dependência de ferramenta externa é
introduzida.

### 4.4 Fixtures unitárias dos helpers novos

- `elf_build_rodata_relas` sobre `eo_rodata_ptr_object`: assert a linha resolvida
  (`symidx=1`, `addend=8`, `rtype=1`).
- `elf_section_names(true).len == 8` e `elf_section_names(false).len == 7`;
  `elf_section_names(true)[7] == ".rela.rodata"`.
- `elf_resolve_rela` sobre um request text (miss de rodata) devolve `symidx =
  elf_symbol_index`, addend inalterado — cobre o braço não-rodata do helper extraído.

---

## 5. Sequência de edits + pontos de regressão

Cada edit é gate-able isoladamente; rodar o `.tkt` listado após cada um. Ordem tal que
o arquivo compila a cada passo.

| # | Arquivo | Edit | Gate de regressão (`.tkt`) |
|---|---|---|---|
| E1 | `src/backend/objfile_elf.tks` | `ElfObject.rodata_relocs` (§2.1) + generalizar doc de `offset`; setar `rodata_relocs = teko::list::empty()` em `emit_elf` (`:1014`) | `objfile_elf_test.tkt`, `objfile_coff_test.tkt` (compila; goldens byte-idênticos) |
| E2 | `src/backend/objfile_elf_riscv.tks` | `rodata_relocs = teko::list::empty()` em `emit_elf_riscv` (`:44`) | `objfile_elf_riscv_test.tkt` |
| E3 | `src/backend/objfile_elf.tks` | `ElfLayout` +4 campos (§2.2); `compute_elf_layout` (§2.5) | goldens ELF byte-idênticos (mesmos offsets/shoff) |
| E4 | `src/backend/objfile_elf.tks` | `elf_section_names(has_rodata_relocs)` (§2.3) + callsite | goldens ELF (7 nomes quando `false`) |
| E5 | `src/backend/objfile_elf.tks` | `emit_elf_header` recebe `nsects` (§2.4) + callsite | goldens ELF (`e_shnum==7` inalterado) |
| E6 | `src/backend/objfile_elf.tks` | extrair `elf_resolve_rela` + `elf_build_rodata_relas` (§2.6); `elf_build_relas` reusa | goldens de `.rela.text` byte-idênticos (prova a extração) |
| E7 | `src/backend/objfile_elf.tks` | `emit_elf_shdrs` 8º header condicional (§2.7); `emit_elf_object` costura (§2.8) | **todos** os goldens ELF/riscv byte-idênticos (`has_rr=false`) |
| E8 | `objfile_elf_test.tkt` + `objfile_elf_riscv_test.tkt` | fixtures §4 | os próprios testes novos (verde) |

> Writers Mach-O/COFF/wasm e a VM **NÃO são tocados** em T-B2 (são T-B3/T-B4/T-B5). O
> honest-stop `encode_rodata` de T-B1 **permanece** — não editar `encode_*.tks`.

**Ritual points:**

- **Por-edit:** o `.tkt` do arquivo (tabela) — cada edit é gate-able só.
- **RITUAL POINT — fim de T-B2:** gate COMPLETO — todos os goldens de backend
  byte-idênticos (`objfile_elf_test.tkt`, `objfile_elf_riscv_test.tkt`,
  `objfile_coff_test.tkt`, `objfile_macho_test.tkt`, `encode_*_test.tkt`,
  `lower_test.tkt`, `lir_interp_test.tkt`, `tkb_test.tkt`) + **fixpoint gen1==gen2** +
  ambas as engines (VM + nativo) + 100% de cobertura do delta (as fixtures §4 cobrem o
  braço `has_rr=true`; os goldens cobrem `has_rr=false`; §4.4 cobre os dois braços de
  `elf_resolve_rela`). **Sem seed bump** — T-B2 não adiciona capacidade que o corpus
  use (o 🔑 SEED BUMP #3 é depois de T-B5, plano §8).

---

## 6. Riscos + tensões (com resolução)

1. **LAYOUT/OFFSETS — o perigo dominante.** A inserção condicional de `.rela.rodata`
   mexe em TODOS os offsets subsequentes QUANDO ativa; se acidentalmente ativasse
   quando vazia (ou se `.shstrtab` recebesse a string incondicionalmente), TODOS os
   goldens quebrariam. *Resolução:* a nova seção é **estritamente condicional** a
   `has_rr` em TRÊS pontos acoplados — o nome em `.shstrtab` (`elf_section_names`), o
   header (`emit_elf_shdrs`), e os bytes (`if has_rr` em `emit_elf_object`) — e
   `e_shnum`/`shoff` derivam dessa mesma condição via `nsects`/`nrela_rodata`. A prova
   §3 mostra colapso byte-exato quando `false`; os goldens + fixpoint são o guardrail
   inescapável. Anexar por ÚLTIMO (índice 7) garante que nenhum índice 0–6 nem
   `e_shstrndx` se move.

2. **Tocar `elf_build_relas` (fn byte-crítico) na extração de `elf_resolve_rela`.**
   *Resolução:* a extração é behavior-preserving (corpo verbatim); os goldens de
   `.rela.text` provam byte-identidade. Alternativa de risco zero: duplicar o loop-body
   (§2.6). Recomendado extrair (W15).

3. **`e_shnum` deixa de ser literal.** *Resolução:* `nsects` = contagem de nomes de
   seção; `==7` quando `has_rr=false` ⇒ byte-inerte. Golden `elf_header_*` (`e_shnum
   ==7`) é o guarda.

4. **Consistência da costura entre os 3 writers (requisito do brief).** A lista
   paralela `rodata_relocs` no objeto neutro é o padrão que T-B3 (Mach-O/COFF) e T-B4
   (wasm) replicam; o discriminador `RelocSect` (T-B1) é consumido no bridge que
   particiona `Reloc*`→(text, rodata). *Resolução:* documentado aqui como a costura
   canônica; T-B3/T-B4 herdam-na sem redescobrir. Sem tensão.

5. **`rtype` do rodata-reloc precisa ser absoluto (R_X86_64_64 / R_RISCV_64).** Um
   `PC32` dobrado em `.rela.rodata` produziria um ponteiro relativo errado.
   *Resolução:* provado §2.0 pela SysV/RISC-V psABI; o bridge futuro mapeará
   `RelocX86::Abs64`/`RelocRiscv::Abs64` (já em `elf_reloc_type`/`riscv_reloc_type`); a
   fixture §4.1 fixa `rtype=1`/`rtype=2` explicitamente.

**Sem tensão genuína não resolvida → sem HALT.**

---

## 7. O que permanece BLOQUEADO (adiantado até o limite)

T-B2 entrega o writer ELF completo e provado HOJE. O que resta atrás do honest-stop de
T-B1 (não faz parte de T-B2, mas nomeado para o implementer):

- **Popular `rodata_relocs`** a partir de um const Tier-B real: o bridge
  `x86_reloc_reqs`/`riscv_reloc_reqs` particionar `Reloc*` por `.sect`, e
  `encode_rodata` deixar de honest-stopar (produzindo `Reloc` com `sect=Rodata` a
  partir de `LRodata.relocs`). Isso é o crumb que fecha a cadeia (junto com T-B5); só
  então `has_rr=true` ocorre em compilação real. Até lá, `.rela.rodata` só é exercitada
  por `ElfObject` à mão (fixtures §4). Nada disso bloqueia T-B2 — o writer está pronto.
</content>
</invoke>
