# T-B6 — o FINALE do Tier-B: abrir o PRODUTOR (`serialize_const`) para agregados com campo slice/ponteiro + migrar os 4 descritores ABI para `const` rodata (#594 Tier-B)

Status: READY-TO-IMPLEMENT (architect, 2026-07-18). Track: Tier-B pointer-bearing
aggregate → rodata (`docs/design/const-module-level-plan.md` §8 crumb **T-B6**, §5.1
verdict, DECISION_LOG/plano D2/D7). Base: o working tree da const-wave com **T-B1..T-B5
mergeados + 🔑 SEED BUMP #3 (0.3.0.25 / T25)** — a cadeia data→data CONSUMIDORA está
completa e no seed: `LDataReloc`/`LRodata.relocs` na LIR (`lir.tks:154/175`); a VM resolve
o ponteiro rodata-interno na semeadura (`lir_interp.tks:181 resolve_rodata_relocs`); o
encoder-bridge nativo produz `Reloc sect=Rodata` (`encode_arm64.tks:2679 encode_rodata` +
irmãos x86/riscv); ELF `.rela.rodata` / Mach-O `__const` / COFF `.rdata` / wasm
`wasm_relocate_rodata`. Predecessores: `const-tb{1,2,3,4,5}-design.md`. crumb 6 (Tier-A →
rodata): `lower.tks:5137 serialize_const`.

> **Escopo de T-B6 (verbatim §8):** "migrate the ABI descriptors
> (`SYSV64`/`AAPCS64`/`RISCV64_LP64D`/`WIN64`, `UPPER_SNAKE` per D7) and any other
> pointer-bearing aggregate to rodata consts." Ritual (§8): "regalloc golden tests (they
> consume the ABI descriptors) must be byte-identical after T-B6." Este é o crumb PRODUTOR:
> `serialize_const` deixa de honest-stopar agregados pointer-bearing, emite os bytes do
> struct + `LDataReloc` por campo slice + interna cada array-alvo como entrada rodata
> própria; a cadeia T-B1..T-B5 (que hoje recebe `relocs` SEMPRE vazio) passa a receber
> relocs reais e a resolvê-los ponta-a-ponta nas duas engines.

---

## 0. TL;DR — as decisões, de cima

1. **Layout do slice em rodata (ponto 1):** o ABI interno de um `[]T` em teko é um **fat
   pointer `{ptr, len}`** (provado por `LoweredFat`, `lower_const_fat`). Um campo slice
   DENTRO de um struct rodata ocupa **16 bytes** = `ptr@fieldoff` (8 bytes, zero-init,
   recebe `LDataReloc`) + `len@fieldoff+8` (8 bytes, **inline** = a contagem de elementos).
   O array-alvo é uma **entrada rodata SEPARADA** (símbolo próprio), não inline no mesmo
   blob — porque `LDataReloc.target` é um NOME DE SÍMBOLO (`lir.tks:154`), o mesmo modelo
   que uma string literal aninhada.
2. **`serialize_const` (ponto 2):** troca o retorno de `[]byte` por um `ConstImage {
   bytes; relocs; leaves }`; para cada campo slice, escreve `len` inline, deixa o slot de
   ponteiro zero, emite `data_reloc(fieldoff, leaf_sym)` e devolve o array-alvo como uma
   `LRodata` leaf. `intern_aggregate_const_decl` interna as leaves ANTES do struct.
   Determinismo do símbolo da leaf: **função pura de (nome-do-const, nome-do-campo)** —
   `.Lconst_AAPCS64.gpr_arg` — nunca de um contador global.
3. **PRÉ-REQUISITO DE LEITURA (constatação crítica — ponto 1/3):** a lowering
   COMPARTILHADA (VM + nativo) **NÃO lê hoje um campo slice de struct** — `lower_fat_expr`
   (`lower.tks:3769`) honest-stopa num `TFieldAccess` fat-valued ("N2"), e o layout dá ao
   campo slice só 8 bytes (`ltype_of(Slice)=Ptr`, `ltype_size(Ptr)=8`). Logo os descritores
   ABI só são lidos hoje pelo **C backend** (que os inline e usa `tk_slice_u32{ptr,len}`).
   Para o espelho SOURCE-level dual-engine que T-B6 exige (§5.8 do T-B5), T-B6 tem de
   ADICIONAR o reader: campo fat = 16 bytes no layout + o braço `TFieldAccess` em
   `lower_fat_expr`. É a única capability genuinamente nova além do produtor.
4. **Migração (ponto 3):** as 4 fábricas viram `pub const UPPER_SNAKE` com **array
   LITERAIS** (os corpos atuais usam `push_range`/helpers com LOOP — NÃO são const-expr,
   têm de ser expandidos para `[0 to u32, 1 to u32, …]`). Os VALORES são idênticos; só a
   materialização muda (fn-call → rodata-load/inline). Prova de equivalência: um golden
   `AAPCS64 == aapcs64()` campo-a-campo ANTES de remover as fns.
5. **Goldens/fixpoint (ponto 4):** regalloc goldens byte-idênticos (valores iguais); os
   object goldens re-baseiam UMA vez SE o compilador for nativo-compilado (o `.rela.rodata`
   dos descritores passa a existir), intactos se C-compilado; **fixpoint gen1==gen2** é a
   barra final.
6. **O perigo nº1 é o determinismo dos símbolos de rodata** — resolvido por símbolos
   função-pura de (const, campo) + ordem de intern determinística (leaves em ordem de campo,
   depois o struct).

---

## 1. Estado atual PROVADO (com linhas) — o produtor, o slice, o layout, o gap de leitura

### 1.1 O gate produtor de HOJE — `serialize_const` honest-stopa pointer-bearing

`serialize_const` (`lower.tks:5137`) despacha pelo tipo resolvido do const:

```
checker::Str      => const_str_bytes(cd.init)
checker::Slice    => const_slice_bytes(s.element, cd.init)      // rejeita elemento ptr-bearing
checker::Named    => const_named_bytes(n.name, cd.init, ...)    // struct/variant
_                 => error "pointer/slice-bearing value is Tier-B (T-B), not crumb 6"   // :5142
```

Os três gates internos que T-B6 abre:
- `const_struct_field_bytes` (`:4968`): `if ltype_is_ptr(lt) { return error "…Tier-B…" }`
  — REJEITA qualquer campo cujo `LType` seja `Ptr` (um slice, `str`, `char`, Named-aninhado
  ou `ptr<T>`). É AQUI que um campo `[]u32` de `AbiDescriptor` morre hoje.
- `const_slice_bytes` (`:4929`): `if !const_is_flat_scalar(element) { return error "…slice
  element is pointer/slice-bearing -> Tier-B…" }` — para um const cujo TIPO é `[]([]T)` etc.
- `serialize_const:5142`: o braço `_` para tipos que não são Str/Slice/Named.

Para os descritores ABI, o gate relevante é o de `const_struct_field_bytes` (campo slice
dentro de um struct Named).

### 1.2 O ABI interno de um slice em teko — fat pointer `{ptr, len}`, provado

- `LoweredFat = struct { ctx; ptr: u32; len: u32 }` (`lower.tks:3761`): lowering de um
  valor `str`/`[]T` é o PAR (ptr VReg, len VReg). É o "two-VReg FatVReg threading" (A1-4).
- **Const slice standalone** (`lower_const_fat`, `:3812`): o ptr é `global_addr(symbol)` e o
  len é um `const_int` = contagem de elementos (`const_fat_len`, `:3833` — `str` = byte_len,
  `[]T` = `byte_len / stride`). Os BYTES vivem num blob rodata; o header `{ptr,len}` é
  materializado no USO em REGISTRADORES, não em memória. Isto é o modelo de string literal
  (F14).
- **`.len` de um receptor fat** (`lower_len_field`, `:4139`): lê o `len` VReg do par já
  baixado — sem `load`. **`[i]`** (`lower_index`/`index_addr`, `:4177`) folda
  `ptr + index*stride` num `field_addr`.

> Conclusão-chave: um slice tem DUAS representações. (a) VALOR/const standalone → par de
> registradores (ptr=global_addr, len=imediato), bytes num blob separado. (b) CAMPO em
> memória → tem de estar STORED inline como `{ptr, len}` no offset do campo, porque um
> `LFieldAddr`+`LLoad` lê da memória do struct. T-B6 usa a forma (b) para os 8 campos slice
> do `AbiDescriptor`; cada array-alvo é um blob separado (forma-a dos bytes).

### 1.3 O layout de HOJE dá a um campo slice apenas 8 bytes (Ptr) — insuficiente para `{ptr,len}`

`layout_of_fields` (`lower.tks:5333`) mapeia cada campo por `ltype_of_typeexpr(te)`. Um
`[]u32` é um `parser::SliceType` (não `NamedType`) → cai no fallback total `LType::Ptr`
(`ltype_of_typeexpr:210`), e `struct_layout_of_field` devolve `null` (slice não é Named).
`field_size_of(Ptr, null)` = `ltype_size(Ptr)` = **8** (`lir.tks:494`). Logo:

- Um campo slice ocupa **8 bytes** no layout atual — só o ptr, **sem slot de len**.
- `store_struct_fields` (`:4105`) grava um campo slice com `ltype_of(field.type)` = `Ptr`
  (8 bytes) — grava só o ptr, PERDE o len.

Isto é coerente com o fato de que o caminho nativo/VM **nunca materializa** um `AbiDescriptor`
hoje (honest-stopa antes). O layout de 8 bytes é um estado morto — nenhum teste nativo/VM
que passa depende dele.

### 1.4 A lowering compartilhada NÃO lê um campo slice de struct — honest-stop "N2"

`lower_fat_expr` (`lower.tks:3769`) reconhece o CONJUNTO FECHADO de produtores fat:
`TStrLit`, `TArrayLit`, `TVar` — e **qualquer outro** (inclusive `TFieldAccess`, i.e.
`abi.gpr_arg`) → `error "fat-pointer receiver `TFieldAccess` not yet lowered (N2)"`. Como
`arg_reg`/`allocatable_pool`/`is_caller_saved` (`abi_aapcs64.tks:182/199/211`) LEEM
`abi.gpr_arg`/`abi.gpr_allocatable`/… como valores fat, essas leituras honest-stopam na
lowering compartilhada → **nem a VM nem o nativo** as compilam hoje. Os testes ABI/regalloc
passam porque o binário do compilador e o harness desses testes rodam pelo **C backend**
(`codegen.tks`, que representa slice como `tk_slice_<elem>{ptr,len}` e faz INLINE dos consts
agregados — plano §5.2 tabela). A VM (`lir_interp`) consome a MESMA LIR de `lower.tks`, então
herda o mesmo honest-stop.

**Consequência para T-B6:** o espelho SOURCE-level dual-engine (um `const AAPCS64` lido
IDÊNTICO na VM e no nativo — o objetivo declarado de T-B6, T-B5 §5.8) exige que a lowering
compartilhada saiba LER um campo fat de struct. Isso é o reader (§4), o PRÉ-REQUISITO deste
crumb. Sem ele, T-B6 entrega só o produtor (bytes corretos em rodata) mas nenhuma engine
que consuma LIR consegue LER o const — só o C backend (que ignora rodata e inline).

### 1.5 A cadeia consumidora T-B1..T-B5 (já pronta, recebe `relocs` vazio hoje)

- `LRodata { symbol; bytes; relocs: []LDataReloc }` (`lir.tks:175`) — o carrier.
- VM: `resolve_rodata_relocs` (`lir_interp.tks:236`) sobrescreve cada slot de ponteiro
  rodata-interno com o índice-de-célula do datum-alvo na semeadura (T-B5 §2). Alvo
  desconhecido → erro nomeado → `interp_lmodule` mapeia a `-1`.
- Nativo: `encode_rodata` (`encode_arm64.tks:2679` + x86/riscv) produz `Reloc sect=Rodata`
  por `LDataReloc`, re-baseado ao offset do blob no `.rodata`/`__const` concatenado; os
  writers ELF/Mach-O/COFF/wasm aplicam a relocation UMA vez na emissão.
- **Hoje `serialize_const` honest-stopa pointer-bearing → todo `LRodata.relocs` é vazio →
  a cadeia inteira é byte-idêntica.** T-B6 é o PRIMEIRO produtor a popular `relocs`.

---

## 2. Ponto 1 — LAYOUT do `AbiDescriptor` em rodata (a decisão + a prova)

### 2.1 A decisão

Um `const AAPCS64: AbiDescriptor = AbiDescriptor { … }` materializa em rodata assim:

```
Entrada rodata  .Lconst_AAPCS64              (o BLOB do struct, com relocs)
    campo gpr_arg          @ off0 : { ptr@off0    = 0  (8B, LDataReloc → .Lconst_AAPCS64.gpr_arg),
                                       len@off0+8  = 8  (8B inline, LE) }
    campo fpr_arg          @ off1 : { ptr = 0 (reloc → .Lconst_AAPCS64.fpr_arg),          len = 8  }
    campo gpr_allocatable  @ off2 : { ptr = 0 (reloc → .Lconst_AAPCS64.gpr_allocatable),  len = 25 }
    campo fpr_allocatable  @ off3 : { ptr = 0 (reloc → .Lconst_AAPCS64.fpr_allocatable),  len = 30 }
    campo gpr_caller_saved @ off4 : { ptr = 0 (reloc → …gpr_caller_saved),                len = 19 }
    campo fpr_caller_saved @ off5 : { ptr = 0 (reloc → …fpr_caller_saved),                len = 24 }
    campo gpr_spill_scratch@ off6 : { ptr = 0 (reloc → …gpr_spill_scratch),               len = 3  }
    campo fpr_spill_scratch@ off7 : { ptr = 0 (reloc → …fpr_spill_scratch),               len = 2  }
    campo spill_slot_bytes @ off8 : 8    (u32, 4B inline)
    campo spill_slot_align @ off9 : 8    (u32, 4B inline)
    campo shadow_space     @ off10: 0    (u32, 4B inline)

Entrada rodata  .Lconst_AAPCS64.gpr_arg      = [0,1,2,3,4,5,6,7]   como u32 LE (32B), relocs vazio
Entrada rodata  .Lconst_AAPCS64.fpr_arg      = [0,1,2,3,4,5,6,7]   (32B), relocs vazio
Entrada rodata  .Lconst_AAPCS64.gpr_allocatable = [0..14,19..28]   (100B), relocs vazio
…  (uma leaf por campo slice, 8 leaves no total)
```

- **`{ptr, len}`, NÃO só ptr.** O len TEM de ser inline porque um `.len` / `[i]` lê da
  memória do struct (o const não carrega o len em registrador como um slice standalone —
  quando lido como CAMPO, ambos vêm de `LFieldAddr`+`LLoad`).
- **Slot de ponteiro = 8 bytes zero + `LDataReloc(fieldoff, leaf_sym)`.** Resolvido pela VM
  (célula-índice do alvo) e pelos writers nativos (endereço absoluto), T-B1..T-B5.
- **`len` inline = contagem de elementos** (não byte-length), no offset `fieldoff+8`, 8
  bytes LE — casa com `const_fat_len`/`lower_len_field` que devolvem a CONTAGEM.
- **Array-alvo = entrada rodata SEPARADA**, símbolo próprio, `relocs` vazio (é flat-scalar
  `[]u32`, já materializável por `const_array_bytes`). NÃO inline no mesmo blob: `LDataReloc.
  target` é um NOME DE SÍMBOLO (`lir.tks:154`), e a resolução (VM/writers) é por símbolo —
  um reloc intra-blob não existe no modelo. É o mesmo padrão de uma string literal aninhada.

### 2.2 A largura do campo fat = 16 bytes — a MUDANÇA de layout

O layout de HOJE dá 8 bytes (§1.3). T-B6 muda `layout_of_fields`/`field_size_of` para
dimensionar um campo `str`/`[]T` como **16 bytes, align 8** (`2 * ltype_size(Ptr)`). Prova de
segurança:
- Nenhum struct do corpus com campo fat é materializado hoje no caminho nativo/VM (§1.4),
  então nenhum teste nativo/VM que passa depende do layout de 8 bytes → a ampliação não
  regride nada.
- O C backend usa o layout C próprio (`tk_slice_<elem>` já é `{ptr,len}` = 16B) — inalterado.
- A ampliação ALINHA o layout nativo/VM com o C (a direção correta) e é o que o serializer
  e o reader precisam concordar.

### 2.3 A PROVA pela lowering existente

- **Forma standalone provada:** `lower_const_fat` (`:3812`) — um `const GZIP_MAGIC: []byte`
  lê ptr=`global_addr(.Lconst_GZIP_MAGIC)` + len=`const_int(2)`. Confirma `{ptr, len}` como o
  ABI do slice, e o array como blob rodata separado (crumb 6, já no seed).
- **Forma campo-em-memória provada pelo indexer:** `index_addr`/`const_index_addr`
  (`:4155`) computa `base + index*stride` — o `base` é o ptr do campo. Um `[i]` de
  `abi.gpr_arg` precisa que o ptr do campo aponte para o array-alvo → é exatamente o slot
  relocado. `const_slice_len`/`const_fat_len` fixam `stride = ltype_size(ltype_of(u32)) = 4`
  → 8 elementos = 32 bytes na leaf, `len = 32/4 = 8`. As duas contagens (o `len` inline e o
  que `const_slice_len` derivaria) concordam por construção.
- **Leitura via LFieldAddr/LLoad nas duas engines:** o reader (§4) baixa `abi.gpr_arg` para
  `ptr = LLoad(LFieldAddr(base, fieldoff))` (Ptr) + `len = LLoad(LFieldAddr(base, fieldoff+8))`
  (I64). Na VM (T-B5 §1.2) um `LLoad` lê UMA célula: o ptr vem da célula `base+fieldoff`
  (sobrescrita por `resolve_rodata_relocs` com o índice-célula do alvo → correto), o len vem
  da célula `base+fieldoff+8` (byte baixo = a contagem; ≤ 32 < 256, dentro da tolerância do
  oráculo "not a byte-accurate layout verifier"). No nativo os loads são width-corretos (8B).
  As duas engines dereferenciam os MESMOS bytes-alvo (§3 do T-B5) → exit idêntico.

---

## 3. Ponto 2 — o que muda em `serialize_const` (o produtor)

### 3.1 Shape novo: `serialize_const` devolve bytes + relocs + leaves

O produtor precisa emitir TRÊS coisas: os bytes do blob do struct, as relocs internas, e as
entradas rodata leaf. Troca-se o retorno `[]byte | error` por um carrier:

```teko
/**
 * ConstImage — a materialização completa de UM const agregado em rodata (#594 T-B6): os
 * bytes do blob principal, as suas relocations internas de ponteiro (uma por campo
 * slice/ponteiro, vazias para um agregado Tier-A flat), e as entradas rodata LEAF que os
 * seus slots de ponteiro referenciam (um array-alvo por campo slice; vazias para Tier-A).
 * Um agregado Tier-A (crumb 6) devolve `relocs`/`leaves` vazios e é byte-idêntico ao
 * caminho anterior; um agregado Tier-B (descritor ABI) devolve uma reloc + uma leaf por
 * campo slice.
 *
 * @param bytes   os bytes do blob principal (slots de ponteiro zero-init, `len`/escalares inline)
 * @param relocs  as relocations internas do blob (offset do slot → símbolo da leaf)
 * @param leaves  as entradas rodata dos arrays-alvo, a internar ANTES do blob principal
 * @since #594 T-B6
 */
pub type ConstImage = struct { bytes: []byte; relocs: []lir::LDataReloc; leaves: []LRodata }
```

`serialize_const` e a cadeia `const_named_bytes`/`const_struct_bytes`/`const_struct_blob`/
`const_struct_field_bytes` passam a devolver `ConstImage` (Tier-A: `relocs=[]`, `leaves=[]`;
o comportamento crumb-6 é o caso vazio). Assinatura nova:

```teko
/**
 * serialize_const — a imagem rodata COMPLETA de um agregado const (#594 crumb 6 + T-B6):
 * um `str`/`[]T`-flat/struct-flat/variant-empty Tier-A devolve os seus bytes com
 * `relocs`/`leaves` VAZIOS (byte-idêntico ao crumb 6); um struct pointer-bearing (campo
 * slice/`str`/ponteiro — os descritores ABI) devolve o blob com slots de ponteiro zero, uma
 * `LDataReloc` por campo slice, e a `LRodata` leaf de cada array-alvo. O braço `_` (nem
 * Str/Slice/Named) continua honest-stopando (nenhum const do corpus o alcança).
 *
 * @param cd       o TConstDecl residual (tipo resolvido + inicializador checado)
 * @param layouts  a tabela de layouts de struct do programa
 * @param variants a tabela de variants do programa
 * @return         a imagem rodata completa (bytes + relocs internas + leaves)
 * @throws         um honest-stop para uma forma agregada ainda não materializável
 * @since #594 crumb 6 (bytes) + T-B6 (relocs + leaves)
 */
fn serialize_const(cd: checker::TConstDecl, layouts: []LStructLayout, variants: []LEnumInfo) -> ConstImage | error
```

### 3.2 O campo slice: `const_struct_field_bytes` deixa de rejeitar `Ptr`

Hoje (`:4968`) um campo `Ptr` → erro Tier-B. T-B6 substitui: um campo cujo `LType` é `Ptr` E
cujo tipo checado é um `Slice`/`Str` (fat) é MATERIALIZADO como `{ptr=0, len=inline}` + uma
reloc + uma leaf. O helper novo produz, para o campo `j` no offset `layout.field_offsets[j]`:

```teko
/**
 * const_fat_field — a imagem de 16 bytes de um campo fat (`[]T`/`str`) de um struct const, com
 * o slot de ponteiro zero e a contagem de elementos inline no slot alto (#594 T-B6): devolve
 * `{ slot_bytes = <8 zero> ++ serialize_le(count, 8>, reloc = data_reloc(fieldoff, leaf_sym),
 * leaf = LRodata{ leaf_sym, array_bytes, relocs=[] } }`. Os `array_bytes` reusam
 * `const_array_bytes`/`const_str_bytes` (o MESMO stride que o indexer folda), e `count` é a
 * contagem de elementos (`array_bytes.len / stride`, ou `array_bytes.len` para `str`) — a
 * MESMA que `lower_len_field` devolveria, então `.len` concorda byte-a-byte. O símbolo da leaf
 * é `const_leaf_symbol(const_name, field_name)` (função pura → determinístico).
 *
 * @param const_name  o nome do const dono (para o símbolo da leaf)
 * @param field_name  o nome do campo (para o símbolo da leaf)
 * @param fieldoff    o offset do campo no blob do struct (o offset do slot de ponteiro)
 * @param field_ty    o tipo checado do campo (`Slice`/`Str`)
 * @param vexpr       o inicializador do campo (um array/string literal)
 * @return            os 16 bytes do slot, a reloc, e a leaf
 * @throws            quando o inicializador não é materializável (não-literal, spread, elemento ptr)
 * @since #594 T-B6
 */
fn const_fat_field(const_name: str, field_name: str, fieldoff: u32, field_ty: checker::Type, vexpr: checker::TExpr) -> FatFieldImage | error
```

onde `FatFieldImage = struct { slot_bytes: []byte; reloc: lir::LDataReloc; leaf: LRodata }`.
`const_struct_blob` escreve `slot_bytes` no `fieldoff` (via `write_bytes_at`), acumula a
`reloc` e a `leaf` na `ConstImage`. Um campo escalar segue por `const_struct_field_bytes`
(inalterado). A distinção fat-vs-escalar: `field_ty` é `Slice`/`Str` (não `ltype_is_ptr` só,
porque um `char`/Named-aninhado também é `Ptr` mas NÃO é fat — esses continuam honest-stopando
como "Tier-A follow-up" até terem o seu próprio design).

> **Nota de escopo:** os descritores ABI só têm campos `[]u32` (slice) e `u32` (escalar).
> Um campo `str` (fat) é coberto pelo mesmo caminho. Um campo `char`/Named-by-value/`ptr<T>`
> permanece honest-stopando (não há const no corpus que o exija; abrir esses é follow-up
> nomeado, não T-B6).

### 3.3 Determinismo do símbolo da leaf — o perigo nº1 (ponto 6)

```teko
/**
 * const_leaf_symbol — o símbolo rodata determinístico do array-alvo do campo slice
 * `field_name` do const agregado `const_name` (#594 T-B6): `.Lconst_<const_name>.<field_name>`
 * — uma FUNÇÃO PURA de (nome-do-const, nome-do-campo), NUNCA de um contador de intern global.
 * O prefixo `.Lconst_` nunca colide com um `.Lstr<N>` de string literal nem com o blob
 * principal `.Lconst_<const_name>` (este tem um `.` + campo a mais). Como cada `(const,
 * campo)` é único e o nome não depende da ordem de travessia, o mesmo `.tks` gera sempre os
 * mesmos símbolos → `LDataReloc.target`, as bases cumulativas da VM e os offsets re-baseados
 * dos writers são idênticos entre gerações (fixpoint-safe).
 *
 * @param const_name  o nome do const agregado dono
 * @param field_name  o nome do campo slice
 * @return            o símbolo rodata da leaf
 * @since #594 T-B6
 */
fn const_leaf_symbol(const_name: str, field_name: str) -> str {
    teko::str::concat(const_rodata_symbol(const_name), teko::str::concat(".", field_name))
}
```

### 3.4 A ordem de intern — leaves ANTES do blob, em ordem de campo

`intern_aggregate_const_decl` (`:5159`) passa a internar, para um const pointer-bearing: cada
leaf (em ordem de campo declarada) e DEPOIS o blob principal com as suas relocs.

```teko
/**
 * intern_aggregate_const_decl — materializa um TConstDecl residual agregado em rodata (#594
 * crumb 6 + T-B6): um const escalar é um skip defensivo; um agregado Tier-A interna UMA
 * entrada (relocs vazio); um agregado pointer-bearing (descritor ABI) interna primeiro cada
 * entrada LEAF de array-alvo (em ordem de campo, símbolos determinísticos) e DEPOIS o blob
 * principal com as suas `LDataReloc`. A ordem leaves-antes-do-blob é determinística (ordem de
 * campo declarada), então as bases cumulativas (VM) e os offsets re-baseados (writers) são
 * estáveis entre gerações.
 *
 * @param m        o módulo acumulado
 * @param cd       o TConstDecl residual
 * @param layouts  a tabela de layouts
 * @param variants a tabela de variants
 * @return         o módulo com as leaves + o blob do const anexados (inalterado p/ escalar)
 * @throws         um honest-stop para um agregado não materializável
 * @since #594 crumb 6 + T-B6
 */
fn intern_aggregate_const_decl(m: LModule, cd: checker::TConstDecl, layouts: []LStructLayout, variants: []LEnumInfo) -> LModule | error {
    if const_decl_is_scalar(cd.ty) { return m }
    let img = match serialize_const(cd, layouts, variants) { ConstImage as x => x; error as e => return e }
    mut cur = m
    mut i: u64 = 0
    loop {
        if i >= img.leaves.len { break }
        cur = add_rodata(cur, img.leaves[i])
        i++
    }
    add_rodata(cur, LRodata { symbol = const_rodata_symbol(cd.name); bytes = img.bytes; relocs = img.relocs })
}
```

> A leaf `[]u32` é flat-scalar → `const_array_bytes` já existe e produz os bytes; a leaf
> `LRodata.relocs` é vazio. Se um dia uma leaf FOR ela mesma pointer-bearing (`[][]T`),
> recursa — fora do corpus atual, honest-stop nomeado.

---

## 4. O READER — o pré-requisito de leitura (a capability genuinamente nova)

Sem isto, nenhuma engine que consuma LIR lê o const (só o C backend, que inline). Para o
espelho dual-engine (§5.8 do T-B5) T-B6 adiciona:

### 4.1 Campo fat = 16 bytes no layout

`layout_of_fields` (`lower.tks:5333`) usa um dimensionamento novo por campo fat:

```teko
/**
 * typeexpr_is_fat — true iff a anotação sintática `te` é um tipo fat-pointer (`[]T` slice ou
 * `str`), o campo cujo layout ocupa 16 bytes `{ptr, len}` em vez do `Ptr` de 8 bytes do
 * fallback escalar (#594 T-B6). Um `char`/Named-by-value/`ptr<T>` NÃO é fat (é um `Ptr` de 8
 * bytes ou um struct aninhado), então não entra aqui.
 *
 * @param te  a anotação sintática do campo
 * @return    se o campo é um fat-pointer (16 bytes {ptr,len})
 * @since #594 T-B6
 */
fn typeexpr_is_fat(te: parser::TypeExpr) -> bool {
    match te {
        parser::SliceType => true
        parser::NamedType as nt => single_segment_name_is(nt.path, "str")
        _ => false
    }
}
```

Em `layout_of_fields`, para um campo fat: `field_sizes.push(16)`, `field_aligns.push(8)`, e
`field_types.push(Ptr)` (a CLASSE de load/store do slot BAIXO continua `Ptr`; o len é lido
por um segundo load no offset+8 — o mesmo idioma que um struct-by-value aninhado carrega
`Ptr` mas com size/align próprios, `:553`). `size` do struct arredonda ao maior align (8).

### 4.2 `lower_fat_expr` ganha o braço `TFieldAccess`

```teko
/**
 * lower_fat_field — o par (ptr, len) de um campo `[]T`/`str` de um struct (#594 T-B6): baixa o
 * receptor ao endereço-base do struct, e lê ptr = `LLoad(LFieldAddr(base, fieldoff))` (a
 * classe Ptr; num const rodata o slot foi relocado ao array-alvo por T-B5/os writers) e len =
 * `LLoad(LFieldAddr(base, fieldoff + POINTER_SIZE))` (I64, a contagem inline). Casa com o
 * layout de 16 bytes (§4.1): ptr@fieldoff, len@fieldoff+8. É o braço que faltava para ler um
 * descritor ABI const nas duas engines (o C backend usa o seu próprio `.ptr`/`.len`).
 *
 * @param ctx  o contexto de lowering
 * @param fa   o acesso a campo (receptor struct + nome do campo fat)
 * @return     o par (ptr, len)
 * @throws     quando o receptor não é um struct com layout registrado, ou o campo não existe
 * @since #594 T-B6
 */
fn lower_fat_field(ctx: LowerCtx, fa: checker::TFieldAccess) -> LoweredFat | error {
    let ro = match lower_expr(ctx, fa.receiver) { Lowered as x => x; error as e => return e }
    let name = match named_type_name(fa.receiver.type) { str as s => s; error as e => return e }
    let layout = match find_struct_layout(ro.ctx.layouts, name) { LStructLayout as l => l; error as e => return e }
    let off = match field_offset_of(layout, fa.field) { u32 as o => o; error as e => return e }
    let pa = ctx_alloc(ro.ctx)
    let with_pa = ctx_append(pa.ctx, field_addr_inst(pa.vreg, ro.vreg, off, fa.receiver.line, fa.receiver.col))
    let pr = ctx_alloc(with_pa)
    let with_pr = ctx_append(pr.ctx, load_inst(pr.vreg, pa.vreg, LType::Ptr, fa.receiver.line, fa.receiver.col))
    let la = ctx_alloc(with_pr)
    let with_la = ctx_append(la.ctx, field_addr_inst(la.vreg, ro.vreg, off + ltype_size(LType::Ptr), fa.receiver.line, fa.receiver.col))
    let lr = ctx_alloc(with_la)
    Lowered_fat_ret(ctx_append(lr.ctx, load_inst(lr.vreg, la.vreg, LType::I64, fa.receiver.line, fa.receiver.col)), pr.vreg, lr.vreg)
}
```

(`Lowered_fat_ret` = construir `LoweredFat { ctx; ptr; len }`; inline no site — mostrado como
helper só para caber o Javadoc.) `lower_fat_expr` (`:3769`) ganha:

```teko
        checker::TFieldAccess as fa => lower_fat_field(ctx, fa)
```

ANTES do braço `_` de honest-stop.

### 4.3 `store_struct_fields` grava as duas metades (completude, sem regressão)

Para um struct fat CONSTRUÍDO em runtime (não const), `store_struct_fields` (`:4105`) passa a
gravar ptr@off + len@off+8 quando o campo é fat (via `lower_fat_expr` do valor + dois stores).
Hoje esses casos honest-stopam (o valor fat não baixa como campo), então é um ganho estrito,
zero regressão. **Não é exigido pelos descritores ABI** (que viram CONST, serializados pelo
produtor, não construídos por store) — mas fecha o buraco para o próximo struct-fat runtime e
mantém build+read simétricos. Pode ser um sub-passo separado se o gate quiser minimalidade.

---

## 5. Ponto 3 — migração das 4 fábricas → `const UPPER_SNAKE`

### 5.1 O problema: os corpos atuais NÃO são const-expr

`aapcs64()`/`sysv64()`/`riscv64_lp64d()`/`win64()` constroem os `[]u32` via `push_range`
(um LOOP) e helpers (`sysv_gpr_arg_seq`, …). `push_range` NÃO está no allowlist Tier-5 (D1) —
é uma fn com loop, não um construtor de literal único. Logo o inicializador do const tem de
ser um **array LITERAL** (Tier 4). Cada `push_range(a, b)` é expandido para a sua sequência
explícita. Exemplo (a referência AAPCS64, o implementer copia e expande os irmãos):

```teko
/**
 * AAPCS64 — o descritor de register-file AAPCS64 (AArch64) como const rodata (#594 T-B6, D7
 * UPPER_SNAKE): x0..x7/v0..v7 argumentos, x19..x28+v8..v15(low) callee-saved, x0..x18+v0..v7+
 * v16..v31 caller-saved, x15..x17/v30..v31 spill scratch. Substitui a fábrica `aapcs64()`
 * (removida): os VALORES são idênticos (provados campo-a-campo pelo golden de equivalência),
 * só a materialização muda de chamada-de-fn (arena por chamada) para load-de-rodata (uma
 * imagem estática). Cada `[]u32` é uma leaf rodata; o campo carrega `{ptr→leaf, len}`.
 *
 * @since #594 T-B6
 */
pub const AAPCS64: AbiDescriptor = AbiDescriptor {
    gpr_arg = [0 to u32, 1 to u32, 2 to u32, 3 to u32, 4 to u32, 5 to u32, 6 to u32, 7 to u32]
    fpr_arg = [0 to u32, 1 to u32, 2 to u32, 3 to u32, 4 to u32, 5 to u32, 6 to u32, 7 to u32]
    gpr_allocatable = [0 to u32, 1 to u32, 2 to u32, 3 to u32, 4 to u32, 5 to u32, 6 to u32, 7 to u32, 8 to u32, 9 to u32, 10 to u32, 11 to u32, 12 to u32, 13 to u32, 14 to u32, 19 to u32, 20 to u32, 21 to u32, 22 to u32, 23 to u32, 24 to u32, 25 to u32, 26 to u32, 27 to u32, 28 to u32]
    fpr_allocatable = [0 to u32, 1 to u32, 2 to u32, 3 to u32, 4 to u32, 5 to u32, 6 to u32, 7 to u32, 16 to u32, 17 to u32, 18 to u32, 19 to u32, 20 to u32, 21 to u32, 22 to u32, 23 to u32, 24 to u32, 25 to u32, 26 to u32, 27 to u32, 28 to u32, 29 to u32, 8 to u32, 9 to u32, 10 to u32, 11 to u32, 12 to u32, 13 to u32, 14 to u32, 15 to u32]
    gpr_caller_saved = [0 to u32, 1 to u32, 2 to u32, 3 to u32, 4 to u32, 5 to u32, 6 to u32, 7 to u32, 8 to u32, 9 to u32, 10 to u32, 11 to u32, 12 to u32, 13 to u32, 14 to u32, 15 to u32, 16 to u32, 17 to u32, 18 to u32]
    fpr_caller_saved = [0 to u32, 1 to u32, 2 to u32, 3 to u32, 4 to u32, 5 to u32, 6 to u32, 7 to u32, 16 to u32, 17 to u32, 18 to u32, 19 to u32, 20 to u32, 21 to u32, 22 to u32, 23 to u32, 24 to u32, 25 to u32, 26 to u32, 27 to u32, 28 to u32, 29 to u32, 30 to u32, 31 to u32]
    gpr_spill_scratch = [15 to u32, 16 to u32, 17 to u32]
    fpr_spill_scratch = [30 to u32, 31 to u32]
    spill_slot_bytes = 8 to u32
    spill_slot_align = 8 to u32
    shadow_space = 0 to u32
}
```

Os irmãos expandem os seus `push_range`/helpers idem:
- **`SYSV64`**: `gpr_arg = [7,6,2,1,8,9]`; `gpr_allocatable = [0,1,2,6,7,8,9,3,12,13,14,15]`;
  `gpr_caller_saved = [0,1,2,6,7,8,9,10,11]`; `fpr_arg = [0..7]`; `fpr_allocatable = [0..13]`;
  `fpr_caller_saved = [0..15]`; `gpr_spill_scratch = [10,11]`; `fpr_spill_scratch = [14,15]`;
  `shadow_space = 0`. (todos `to u32`.)
- **`RISCV64_LP64D`**: `gpr_arg = [10..17]`; `gpr_allocatable = [5,6,7,10..17,28,9,18..27]`;
  `gpr_caller_saved = [1,5,6,7,10..17,28..31]`; `fpr_arg = [10..17]`;
  `fpr_allocatable = [0..7,10..17,28,29,8,9,18..27]`; `fpr_caller_saved = [0..7,10..17,28..31]`;
  `gpr_spill_scratch = [29,30,31]`; `fpr_spill_scratch = [30,31]`; `shadow_space = 0`.
- **`WIN64`**: `gpr_arg = [1,2,8,9]`; `gpr_allocatable = [0,1,2,8,9,3,6,7,12..15]`;
  `gpr_caller_saved = [0,1,2,8,9,10,11]`; `fpr_arg = [0..3]`; `fpr_allocatable = [0..3,6..15]`;
  `fpr_caller_saved = [0..5]`; `gpr_spill_scratch = [10,11]`; `fpr_spill_scratch = [4,5]`;
  `shadow_space = 32`.

> Os `..` acima são a SEQUÊNCIA inclusiva expandida elemento-a-elemento (o array literal não
> tem sintaxe de range). Recomendo GERAR os literais por um script que roda `push_range` uma
> vez e imprime, para eliminar erro de digitação (o `push_range` original é a fonte de
> verdade); o golden §6.1 então prova a igualdade.

### 5.2 Remover as fns mortas + atualizar use-sites

- Remover: `push_range`, `sysv_gpr_arg_seq`, `sysv_gpr_allocatable`, `sysv_gpr_caller_saved`,
  `riscv_gpr_arg_seq`, `riscv_gpr_allocatable`, `riscv_gpr_caller_saved`,
  `riscv_fpr_allocatable`, `riscv_fpr_caller_saved`, `win64_gpr_arg_seq`,
  `win64_gpr_allocatable`, `win64_gpr_caller_saved`, `win64_fpr_allocatable`, e as 4 fábricas
  `aapcs64`/`sysv64`/`riscv64_lp64d`/`win64` (agora consts). **MANTER** `contains_u32` (usado
  por `is_caller_saved`/`is_callee_saved`), `arg_reg`, `allocatable_pool`, `is_caller_saved`,
  `is_callee_saved`, `spill_scratch`, `AbiDescriptor`, `ArgReg` — inalterados (consomem o
  descritor por parâmetro).
- Use-sites (grep `aapcs64\(\)|sysv64\(\)|win64\(\)|riscv64_lp64d\(\)`):
  - `src/build/project.tks:1168,1169` → `teko::backend::AAPCS64`;
    `:1193,1194,1195` → `SYSV64`; `:1223,1224,1225` → `WIN64`; `:1252,1253` → `RISCV64_LP64D`.
  - `src/backend/isel_riscv.tks:150,636,1041` → `arg_reg(RISCV64_LP64D, …)`.
  - Doc-comments que citam `sysv64()`/`aapcs64()`/`riscv64_lp64d()` (encode_x86_64.tks:2466,
    isel_x86_64.tks:472,1133-1134, regalloc_x86.tks:869/893, regalloc_riscv.tks:862/886,
    isel_riscv.tks:610,1013) → atualizar o texto para `SYSV64`/… (Javadoc W15).

### 5.3 A EXIGÊNCIA de byte-identidade dos goldens de regalloc

`regalloc_module`/`_x86`/`_riscv` recebem o descritor por parâmetro e LEEM os mesmos campos.
Como os VALORES de `AAPCS64` são idênticos aos de `aapcs64()` (§6.1 prova), o resultado de
regalloc é byte-a-byte o mesmo. **Só a materialização do descritor muda** (fn-call+arena →
rodata-load no nativo/VM, ou inline no C backend) — os bytes que regalloc EMITE não mudam.
Essa é a barra do ritual.

---

## 6. Ponto 4 — goldens que re-baseiam, prova de equivalência, papel do fixpoint

### 6.1 Prova de equivalência semântica (a barra: valores idênticos)

- **Golden de equivalência (adicionar ANTES de remover as fns):** em `abi_*_test.tkt`, para
  cada descritor, asserir campo-a-campo `AAPCS64.gpr_arg == aapcs64().gpr_arg`, …, e os
  escalares — os 12 campos, 4 descritores. Roda no C backend (as fns ainda existem). Verde =
  a expansão de array-literal é EXATA. Depois de verde, remover as fns e o golden de
  equivalência (a fn some).
- **Regalloc goldens (`regalloc_test.tkt`, `regalloc_x86_test.tkt`, `regalloc_riscv_test.tkt`,
  `regalloc_match_test.tkt`): INALTERADOS e byte-idênticos.** São a prova de que os valores
  lidos não mudaram. Se um byte muda, a expansão está errada.

### 6.2 Goldens que re-baseiam (mudança LEGÍTIMA de bytes emitidos)

Este crumb é o PRIMEIRO a popular `LRodata.relocs`, então quando o compilador é compilado
PELO CAMINHO NATIVO/WASM (ou quando uma fixture compila um `const AAPCS64` por esses
caminhos), aparecem entradas rodata reais + relocs data→data:
- **Object goldens (`objfile_elf_test.tkt`, `objfile_elf_riscv_test.tkt`, `objfile_macho_test.tkt`,
  `objfile_coff_test.tkt`, `objfile_wasm_test.tkt`):** se alguma fixture passar a compilar um
  descritor const pelo caminho nativo, a partição `.rela.rodata`/`__const`/`.rdata`/data-segment
  ganha entradas. Re-baseiam UMA vez, depois congelam. As fixtures T-B2/T-B3/T-B4 já fixaram os
  BYTES de uma reloc data→data à mão; aqui elas passam a ser dirigidas pelo produtor real.
- **`lower_test.tkt`:** ganha as fixtures do produtor (§7) — bytes/relocs/leaves novos, esperado.
- **VM goldens (`lir_interp_test.tkt`):** os testes existentes (relocs vazio) ficam
  byte-idênticos; os novos (§7) exercitam o reader.
- **O binário do compilador em si:** muda (a fonte migrou: inline de struct-literal em vez de
  fn-call no C backend; ou rodata no nativo). Isso é esperado e NÃO é uma regressão.

### 6.3 O papel do fixpoint gen1==gen2

`fixpoint` compila o compilador com ele mesmo duas vezes e exige gen1==gen2. É a prova de que
a migração é AUTO-CONSISTENTE: qualquer não-determinismo nos símbolos de leaf, na ordem de
intern, ou nos offsets re-baseados quebraria gen1==gen2 imediatamente. Como os símbolos são
função-pura de (const, campo) e a ordem de intern é determinística (§3.3/§3.4), gen1==gen2
segura. **O fixpoint é o guarda central do perigo nº1.** No CI: o gate T-B6 exige o fixpoint
verde além dos goldens.

---

## 7. Fixtures a ADICIONAR

### 7.1 (produtor, `lower_test.tkt`) `serialize_const` de um struct com campo slice

Construir à mão um `TConstDecl` `AAPCS64: AbiDescriptor = { gpr_arg = [0..7 to u32]; …
spill_slot_bytes = 8; … }` (ou um mini-struct `type S = struct { xs: []u32; n: u32 }`,
`const K: S = { xs = [10 to u32, 20 to u32]; n = 2 }` para reduzir o boilerplate). Asserir a
`ConstImage`:
- `img.leaves.len == 1` (ou 8 p/ o descritor), símbolo `.Lconst_K.xs`, bytes = `[10,0,0,0,
  20,0,0,0]` (u32 LE).
- `img.relocs.len == 1`, `img.relocs[0] == data_reloc(0, ".Lconst_K.xs")` (offset do campo
  `xs`).
- `img.bytes` no offset do slot de ponteiro = 8 zeros; no offset+8 = `serialize_le(2, 8)` (o
  `len` inline); o campo `n` escalar no seu offset.
- Determinismo: dois `serialize_const` do mesmo `cd` devolvem `ConstImage` idêntico.

### 7.2 (produtor) o gate `serialize_const:5142` deixa de disparar p/ campo slice

O `const K: S` acima NÃO honest-stopa (antes de T-B6 dava o erro "pointer/slice-bearing…").
Contraprova: um campo `char`/Named-by-value ainda honest-stopa (fora do escopo).

### 7.3 (reader, `lir_interp_test.tkt`) ler um campo slice de struct const — deref end-to-end

`LModule` à mão: as leaves (`.Lconst_K.xs` = `[10,20]` u32) + o blob de `K` (slot ptr zero +
reloc→leaf, len inline) + um `main` que faz `K.xs[0]` → `field_addr`/`load` → `exit`. Espera
**exit 10** na VM. (`K.xs[1]` → 20; `K.xs.len` → 2.) Prova que T-B5 resolve o slot e o reader
o dereferencia. Dual-engine: o mesmo `.tkt` roda no harness nativo.

### 7.4 (layout) campo fat = 16 bytes, `sizeof` correto

Asserir que `layout_of_fields` de `type S = struct { xs: []u32; n: u32 }` dá `xs`@0 (16B),
`n`@16 (4B), `size` = 24 (align 8). Guard do §4.1.

### 7.5 (dual-engine SOURCE-level, o espelho §5.8 do T-B5) — o AAPCS64 real

Um `.tkt` both-engine: `fn main() -> i64 { let r = arg_reg(AAPCS64, MRegClass::GPR, 0); exit(r.reg.id to i64) }` → **exit 0** (x0) na VM E no nativo; `arg_reg(AAPCS64, GPR, 7).reg.id` → 7;
`allocatable_pool(SYSV64, GPR).len` → 12; `is_caller_saved(WIN64, preg(6, GPR))` → false (RSI
callee-saved no Win64). Este é o teste que T-B5 §5.8 nomeou como bloqueado atrás do BUMP #3 —
T-B6 o destrava e o entrega.

### 7.6 (migração) regalloc + abi goldens inalterados

`abi_sysv64_test.tkt`/`abi_win64_test.tkt`/`abi_riscv64_test.tkt` + os 4 `regalloc_*_test.tkt`
passam byte-idênticos (só as chamadas `sysv64()`→`SYSV64` nos próprios testes, se houver, e o
golden de equivalência §6.1 temporário).

> Cobertura do delta: §7.1/§7.2 o produtor (bytes/relocs/leaves + gate aberto); §7.3 o reader
> deref; §7.4 o layout de 16 bytes; §7.5 o espelho dual-engine real; §7.6 a byte-identidade da
> migração. 100% do delta.

---

## 8. Ponto 5 — validação local (seed 0.3.0.25 / T25) + sequência de gates

- **O fonte T-B6 compila no seed T25?** SIM. T25 (0.3.0.25) tem a capability data→data
  CONSUMIDORA (VM/writers/encoder-bridge/wasm) no seed. O fonte de T-B6 (o `const AAPCS64` +
  as leituras `abi.gpr_arg` + o novo produtor/reader) é compilado pelo seed via o **C backend**
  (o caminho default do binário liberado — inline dos consts agregados, `tk_slice{ptr,len}`),
  que já sabe tudo isso. Logo o self-hosting fecha sem precisar do reader nativo no seed. O
  reader nativo/VM que T-B6 adiciona é exercitado pelas fixtures dual-engine (§7.3/§7.5), não
  pelo bootstrap.
- **Escada de gates:** `T25 × branch gated`:
  1. gen1 = seed T25 compila o working tree T-B6 (C backend) → OK.
  2. gen2 = gen1 compila o working tree → OK.
  3. gen2 = gen1 compila o working tree → **assert gen1==gen2** (fixpoint).
  4. `.tkt` de cada arquivo editado (por-edit, tabela §9) + o gate COMPLETO no ritual.
- **Ambas as engines:** as fixtures §7.3/§7.5 rodam VM + nativo pelo harness `.tkt` — a prova
  dual-engine ao nível SOURCE que faltava até o BUMP #3.

---

## 9. Sequência de edits (cada um gate-able) + o ritual

| # | Arquivo | Edit | Gate (`.tkt`) |
|---|---|---|---|
| E1 | `src/lir/lir.tks` | `ConstImage` + `FatFieldImage` types (§3.1) | compila |
| E2 | `src/lir/lower.tks` | `typeexpr_is_fat` + campo fat = 16B em `layout_of_fields`/`field_size_of`/`field_align_of` (§4.1) | `lower_test.tkt` §7.4 |
| E3 | `src/lir/lower.tks` | `lower_fat_field` + braço `TFieldAccess` em `lower_fat_expr`; (opc.) `store_struct_fields` grava as 2 metades (§4.2/§4.3) | `lir_interp_test.tkt` §7.3 |
| E4 | `src/lir/lower.tks` | `const_leaf_symbol`, `const_fat_field`, `serialize_const`/`const_named_bytes`/`const_struct_bytes`/`const_struct_blob` → `ConstImage`, `intern_aggregate_const_decl` interna leaves+blob (§3) — ABRE o gate `:5142` p/ campo slice | `lower_test.tkt` §7.1/§7.2 |
| E5 | `src/backend/abi_aapcs64.tks` + `abi_sysv64.tks` + `abi_riscv64.tks` + `abi_win64.tks` | golden de equivalência (§6.1) → migrar as 4 fábricas p/ `const UPPER_SNAKE` array-literal → remover as fns mortas (§5) | `abi_*_test.tkt` (equivalência verde, depois byte-idêntico) |
| E6 | `src/build/project.tks`, `src/backend/isel_riscv.tks` + doc-comments | use-sites `x()` → `X` (§5.2) | `regalloc_*_test.tkt` byte-idêntico |
| E7 | fixtures | §7.5 dual-engine mirror + §7.3 reader + §7.1 produtor | os próprios testes |

Ordem tal que o arquivo compila a cada passo: E1→E2→E3 dão o reader/layout; E4 o produtor
(gate aberto); só ENTÃO E5/E6 podem escrever os consts (o produtor tem de existir antes de o
corpus usar um const Tier-B — mas como o self-host é via C backend, E5 já compilaria mesmo
sem E4; ainda assim sequencia-se E4 antes p/ que as fixtures dual-engine E7 tenham o produtor).

**Ritual points:**
- **Por-edit:** o `.tkt` da linha.
- **RITUAL POINT — fim de T-B6:** gate COMPLETO — todos os goldens VM (`lir_interp_test.tkt`)
  + backend (`encode_*_test.tkt`, `objfile_*_test.tkt`, `lower_test.tkt`, `tkb_test.tkt`) +
  **regalloc goldens BYTE-IDÊNTICOS** (`regalloc_*_test.tkt`, `abi_*_test.tkt`) +
  **fixpoint gen1==gen2** + ambas as engines (VM + nativo) nas fixtures dual-engine + 100% de
  cobertura do delta (§7). Object goldens re-baseados UMA vez (se aplicável) e congelados.
- **Fecho do Tier-B / do #594 pointer-bearing:** T-B6 é o FINALE — a partir daqui `serialize_
  const` não tem mais gate produtor fechado para slice/`str`; a cadeia data→data está viva
  ponta-a-ponta com um produtor real. Um `-beta` (D36) tag o merge.

---

## 10. Ponto 6 — riscos + tensões (com resolução)

1. **Determinismo dos símbolos de rodata (o perigo nº1 pro fixpoint).** Um símbolo de leaf
   derivado de um contador de intern global mudaria quando outro const fosse adicionado →
   quebra gen1==gen2. *Resolução:* `const_leaf_symbol` é FUNÇÃO PURA de (nome-do-const,
   nome-do-campo) (§3.3); a ordem de intern é a ordem de campo declarada (§3.4). O fixpoint
   (§6.3) é o guarda. Nenhum contador global toca os símbolos de leaf.

2. **A expansão de `push_range` → array literal pode divergir de valor (byte-drift).** Um
   único id trocado muda um register list → regalloc emite bytes diferentes. *Resolução:* o
   golden de equivalência campo-a-campo `AAPCS64 == aapcs64()` (§6.1) roda ANTES de remover as
   fns; recomendo gerar os literais por script a partir do `push_range` original. Só depois de
   verde as fns somem. Os regalloc goldens byte-idênticos são a barra final.

3. **O reader não existe na lowering compartilhada (a constatação crítica, §1.4).** Sem ele o
   espelho dual-engine é impossível e T-B6 entregaria só o produtor. *Resolução:* T-B6 ADICIONA
   o reader (campo fat = 16B + `lower_fat_expr` TFieldAccess, §4). É a única capability nova
   além do produtor; law-first (smallest step que fecha o objetivo §5.8 do T-B5). Não é um HALT
   — é trabalho identificado e desenhado aqui. *Reporte adjacente:* o reader torna campos fat
   de struct first-class no nativo/VM (build via §4.3 + read via §4.2) — um ganho geral que
   destrava qualquer struct-fat futuro, não só os descritores. NÃO vira issue nova (adjacente,
   folded no crumb).

4. **Ampliar o campo fat p/ 16 bytes muda o layout de TODO struct com campo fat.** *Resolução
   (não é regressão):* nenhum struct-fat é materializado hoje no caminho nativo/VM (honest-stopa,
   §1.3/§1.4), então nenhum golden nativo/VM que passa depende dos 8 bytes; o C backend usa
   layout C próprio (já 16B). A ampliação alinha nativo/VM com o C. Asserido em §7.4; fixpoint
   confirma.

5. **Tolerância multi-byte da VM no slot de `len`.** A VM semeia byte-por-célula e um `LLoad`
   lê UMA célula (T-B5 §1.2); o `len` de 8 bytes é lido pelo byte baixo. *Resolução:* toda
   contagem de descritor ABI é ≤ 32 < 256 → cabe num byte; dentro da tolerância declarada do
   oráculo ("not a byte-accurate layout verifier"). O ptr é resolvido para o índice-célula
   cheio (T-B5), então o deref é exato. Se um dia uma leaf tiver > 255 elementos, é um follow-up
   nomeado (não no corpus).

6. **Object goldens re-baseiam (bytes emitidos mudam legitimamente).** *Resolução:* re-baseiam
   UMA vez, congelam; as fixtures T-B2/T-B3/T-B4 já provaram os BYTES de uma reloc data→data à
   mão, agora dirigidos pelo produtor real. É evolução esperada do gate, não regressão.

**Sem tensão genuína não resolvida → sem HALT.** A única decisão que amplia o escopo além do
literal "abrir o produtor" (o reader, §4) é EXIGIDA pelo objetivo declarado de T-B6 (o espelho
dual-engine, T-B5 §5.8) e resolvida law-first; se o dono quiser T-B6 MÍNIMO (só produtor +
migração, self-host via C backend, espelho dual-engine adiado), o reader (E2/E3) e a fixture
§7.5 destacam-se como um sub-crumb T-B6b sem afetar E4/E5/E6 — registrado como a única escolha
de granularidade em aberto, com recomendação de INCLUIR (fecha o Tier-B de verdade).

---

## 11. O que permanece / follow-ups nomeados (não T-B6)

- Campos const `char` / Named-by-value / `ptr<T>` / `float` (Tier-A follow-up) continuam
  honest-stopando em `const_struct_field_bytes` — nenhum const do corpus os exige.
- Leaf ela mesma pointer-bearing (`[][]T` const) — recursão do produtor, fora do corpus.
- `store_struct_fields` gravar as 2 metades (§4.3) é o único item marcável como opcional/
  sub-passo (não exigido pelos descritores, que são const); recomendo incluir p/ simetria
  build/read.

## 12. Arquivos que o implementer toca (resumo, caminhos absolutos)

- `/home/user/teko-lang/src/lir/lir.tks` — `ConstImage`, `FatFieldImage`.
- `/home/user/teko-lang/src/lir/lower.tks` — `typeexpr_is_fat`, campo fat 16B em
  `layout_of_fields`; `lower_fat_field` + braço em `lower_fat_expr`; `const_leaf_symbol`,
  `const_fat_field`, `serialize_const`/`const_named_bytes`/`const_struct_bytes`/
  `const_struct_blob` → `ConstImage`, `intern_aggregate_const_decl` (abre `:5142`).
- `/home/user/teko-lang/src/backend/abi_aapcs64.tks` — `AAPCS64` const, remover
  `push_range`/`aapcs64`.
- `/home/user/teko-lang/src/backend/abi_sysv64.tks` — `SYSV64` const, remover helpers/`sysv64`.
- `/home/user/teko-lang/src/backend/abi_riscv64.tks` — `RISCV64_LP64D` const, remover helpers.
- `/home/user/teko-lang/src/backend/abi_win64.tks` — `WIN64` const, remover helpers.
- `/home/user/teko-lang/src/build/project.tks` — use-sites `:1168-1253`.
- `/home/user/teko-lang/src/backend/isel_riscv.tks` — use-sites `:150,636,1041` + docs.
- doc-comments em `encode_x86_64.tks`, `isel_x86_64.tks`, `regalloc_x86.tks`,
  `regalloc_riscv.tks`.
- fixtures: `lower_test.tkt`, `lir_interp_test.tkt`, `abi_*_test.tkt`, `regalloc_*_test.tkt`,
  object goldens (re-baseline se nativo-compilado).
- **Sem tocar:** os C twins; `serialize_const:5142` braço `_` (mantido p/ formas
  não-Str/Slice/Named); a cadeia T-B1..T-B5 (recebe os relocs reais SEM edição — os writers
  Mach-O/COFF/wasm/ELF e a VM já resolvem).
</content>
</invoke>
