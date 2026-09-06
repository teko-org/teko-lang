# Desenho — emit native WRITE-THROUGH (não aloca nada; um-arquivo-por-seção; offsets = math) (0.3.1)

> **Papel:** arquiteto (SÓ design; nenhuma linha de produto tocada). Base: `origin/fix/retirement`.
> Branch `arch/emit-native-streaming`. **Desenho WHOLESALE (uma unidade), não ladder de crumbs** — pra
> um implementer Opus executar numa passada, com a metodologia do expurgo (§7).
>
> **PRINCÍPIO ÚNICO (dono, LEI):** **write-direct = NÃO ALOCA NADA.** Igual ao codegen C: o stream direto
> pra disco não *otimizou* o acumulador, **ELIMINOU** — não há mais o que crescer/copiar/vazar. O
> "copy-grow da lista de metadado" (os 13,5 GB medidos no C1) **NÃO é um eixo rival** do streaming — é a
> MESMA cura: se escreve direto, não acumula, e a lista simplesmente DEIXA DE EXISTIR. Um princípio só:
> **não alocar / escrever direto.** O container chunked/`Segmented` está **DESCARTADO** (não é mais o
> caminho) — a cura é as 4 regras abaixo, não um container novo.
>
> **CONTINUA do C1 (seed), NÃO reverte.** O C1 (rodata já grava os BYTES direto na section) é o começo
> certo — o SEED. Falta o RESTO (a lista de metadado da rodata que ainda copy-grow, o LIR, o LEnv, as
> demais seções, a montagem do `.o`). Este doc descreve a conversão INTEIRA.
>
> **Endgame:** gen2-native COMPLETA → `gen2==gen3` native (fixpoint) → `teko.c` APOSENTA → reseed muda pra
> objeto native → sweep da emissão C. Região=param (D130), ZERO C (D148).

---

## 1. As 4 regras da arquitetura (dono — governam TODO o desenho)

1. **N É CONHECIDO — não acumular pra descobrir.** Se o emit sabe O QUE grava e a ORDEM, sabe EXATAMENTE
   quantos itens. Se algum ponto não sabe N, o defeito está num estágio ANTERIOR da pipeline → **conserta o
   upstream pra tornar N conhecido**, NÃO contorna com acumulador/chunked.
2. **ANOTA O OFFSET NA PRÓPRIA ESTRUTURA ITERADA, IN-PLACE.** Ao gravar, itera a estrutura de entrada que
   JÁ existe e grava no MESMO item (`estrutura[i].offset = onde_gravou`, index-assign). NÃO se constrói uma
   segunda lista de metadado que cresce. NO-PUSHES: index-assign, nunca `[..lista,x]`.
3. **OFFSETS = MATEMÁTICA PURA.** Com cada endereço registrado, todo cross-reference do `.o` (header,
   section offsets, symtab, relocs) é aritmética sobre tamanhos conhecidos. Zero acumulação pra calcular.
4. **UM ARQUIVO POR SEÇÃO DO `.o`, JUNTA NO FIM.** Cada seção (text/rodata/data/symtab/strtab/relocs/DWARF)
   grava DIRETO no seu próprio arquivo-de-seção conforme produzida. No fim, todos os tamanhos são conhecidos
   → os offsets do header são math → **concatena os arquivos-de-seção no `.o` final**. Nenhum `[]byte` do
   `.o` inteiro em RAM; a passada de montagem = juntar arquivos + calcular header.

---

## 2. A arquitetura resultante (3 movimentos)

O pipeline HOJE materializa o programa inteiro em cada estágio: `lower_program → LModule` (funcs+rodata+
globals+layouts, TODOS em RAM, `lower.tks:6550`); `encode_module → EncodedModule` (text/rodata `[]byte`,
`encode_x86_64.tks:1015`); `emit_*_object → []byte` (o `.o` inteiro); `finish_native_object(obj:[]byte) →
write_file_bytes` (`project.tks:2199`). Cada `[..lista,x]`/`append_bytes`/`var b:[]byte=[]` é acumulação.

O write-through reorganiza em 3 movimentos:

### M-A — UM ARQUIVO POR SEÇÃO (`SectionBuf`, regra 4)
O `SectionBuf` do C0, **generalizado pra TODAS as seções** (não só rodata): `text.sect`, `rodata.sect`,
`data.sect`, `symtab.meta`, `strtab.sect`, `relocs.meta`, `dwarf_{abbrev,info,line}.sect`. Cada byte de
conteúdo escreve DIRETO no arquivo da sua seção conforme produzido — **qualquer tamanho, mesmo 1 bit** (a
rodata de 165 KB inclusa; não se pula streaming por ser pequena). Em RAM: só o **contador de tamanho** por
seção (o `.len` do `SectionBuf`). (Contrato do `SectionBuf` no §4.)

### M-B — PIPELINE PER-FUNÇÃO (mata a acumulação module-wide de LIR: funcs/insts/blocks)
O driver itera as funções do programa (N_funcs é CONHECIDO — a `TProgram` lista todas as decls +
instâncias monomorfizadas; regra 1). Para CADA função: `lower → isel → regalloc → encode` num **buffer
POR-FUNÇÃO limitado** (reusável entre funções; patch de branch in-place DENTRO da função), depois
`text.sect.append_prefix(fnbuf, fnlen)`; registra o **offset do símbolo da função = tamanho corrente de
text.sect** (math, regra 3); rebase + write-through das relocs da função pra `relocs.meta`. A LIR daquela
função é DESCARTADA antes da próxima. **O módulo NUNCA segura todas as funções em RAM** — some o
`add_func`/`add_inst`/`add_block` program-wide. (Regalloc precisa da função inteira p/ liveness — OK, é
per-função, limitado à MAIOR função, não ao módulo.)

### M-C — MONTAGEM POR CONCATENAÇÃO + MATH (mata o `var b:[]byte=[]` do `.o` inteiro)
Fechadas todas as section-files, os tamanhos são conhecidos → o layout (offsets de seção no header,
`sh_offset`/`sh_size`/load-commands/COFF-header, `sh_info`=1º símbolo global) é **math puro** (regra 3;
os 3 formatos já têm `compute_*_layout` up-front — `elf:711`/`macho:495`/`coff:358`). A passada de
montagem abre o `.o` (`open_write(objp)`), escreve o **header** (buffer fixo pequeno, do layout), e
**concatena** cada section-file em ordem via `SectionBuf.copy_into(o)` (chunked ≤1024, seek-read→write),
com `copy_range_into` onde o rodata-partition reordena spans. Symtab/relocs: lidas de `symtab.meta`/
`relocs.meta` (§5). NENHUM `[]byte` do `.o` em RAM.

**O funil compartilhado (keystone dos 4 formatos):** `finish_native_object` (`project.tks:2199`) e
`finish_static_archive` (`:2289`) deixam de receber `obj:[]byte`; ABREM o FileStream de saída e o passam
aos `write_{elf,macho,coff}_object(out, …)` / `write_static_archive_{gnu,bsd,coff}(out, …)`. Os 4 formatos
(ELF/Mach-O/COFF/ar) sob o MESMO funil — monólito cross-emit, nenhum deferido (lei "emite tudo, todos os
alvos").

---

## 3. O caso da rodata (aplicação canônica das 4 regras — mata os 13,5 GB, CONTINUA do C1)

**O defeito atual:** `intern_rodata` (`lower.tks:5225`) faz `[..ctx.rodata, entry]` — uma SEGUNDA lista
que cresce só pra guardar {symbol, bytes, relocs} de cada string, e serve pra (a) numerar `.LstrN`
(`rodata_symbol(ctx.rodata.len)`) e (b) o encode calcular offsets iterando-a. O C1 já mandou os BYTES pra
`rodata.sect`; a LISTA (24000×48 B recopiada N vezes, vazando = 13,8 GB) ficou. **Aplicando as regras:**

- **Regra 4:** os bytes já vão direto pra `rodata.sect` (C1) — mantém. O `offset` de cada string = tamanho
  de `rodata.sect` ANTES do append (math, regra 3), conhecido no ato do intern.
- **Regra 2 (anota in-place, sem 2ª lista):** no ato do intern, o símbolo `.LstrN` e seu `offset`/`len`
  são um SÍMBOLO do `.o` → escreve-se a entrada de símbolo DIRETO em `symtab.meta` (write-through, regra 4),
  NÃO numa lista de rodata em RAM. A "lista de metadado" DEIXA DE EXISTIR — o que sobra é: (i) `rodata.sect`
  (conteúdo, já C1); (ii) uma entrada em `symtab.meta` por `.LstrN` (write-through). Zero acumulação.
- **Regra 1 (N conhecido — numeração `.LstrN`):** o `N` de `rodata_symbol` (o índice sequencial) é um
  **contador `u64` no `LowerCtx`** (`ctx.rodata_count`, incrementado no intern), NÃO `ctx.rodata.len` de
  uma lista. Um contador não acumula. Se algum consumidor precisa "achar a rodata de nome X"
  (`find_const_rodata:778` — dedup de const), isso é LOOKUP: hoje varre a lista; passa a um `symtab.meta`
  já-suficiente OU (se o dedup for hot) um índice pequeno pré-dimensionado pelo contável upstream — investigar
  no scout (o `find_const_rodata` é usado só p/ const globais nomeadas, N pequeno; provável array-fixo).

**Resultado:** a rodata vira 100% write-through (bytes→`rodata.sect`, símbolo→`symtab.meta`, número→
contador). Nenhuma lista. Os 13,5 GB somem por CONSTRUÇÃO (nada a recopiar/vazar). É a continuação natural
do C1 (que já fez os bytes) — o passo que falta é matar a lista-de-metadado via regra 2+4.

---

## 4. `SectionBuf` — um-arquivo-por-seção (regra 4; generaliza o C0; Teko puro, zero C)

Wrapper fino sobre `FileStream` (I/O-stream D 2026-08-19; chunks ≤1024 B sobre syscalls, zero `teko_rt`).

```teko
/**
 * One `.o` section streamed to its own temp file instead of accumulated in RAM: emit appends content as
 * produced (any size, even one byte); the assembly pass seek-reads/concatenates it into the final `.o`.
 * Only the running byte size stays live. Applies to EVERY section: text/rodata/data/symtab/strtab/
 * relocs/DWARF. Reuses the D-2026-08-19 I/O machinery (FileStream, <=1024 B chunks, syscalls, zero
 * teko_rt).
 */
exp type SectionBuf = class {
    intern stream: teko::io::FileStream
    intern path: str
    intern size: u64

    /**
     * Opens a fresh empty section buffer over a temp path derived from `stem`+`tag`; the temp path never
     * leaks into the emitted `.o`.
     * @param stem the object stem (unique temp path)
     * @param tag  the section tag (text/rodata/data/symtab/strtab/rela/dwinfo/…)
     * @return an open empty section buffer, or an I/O error
     */
    pub static fn open(stem: str, tag: str): SectionBuf | error

    /**
     * Appends `data` at the tail (chunked); advances the running size. No payload copy.
     * @param data the bytes to append, in emission order
     * @return the new running size, or a write error
     */
    pub fn append(data: []byte): u64 | error

    /**
     * Appends the first `n` bytes of a reusable fixed buffer (partly filled) — per-instruction/word emit
     * that never allocates a growing array.
     * @param data the reusable buffer
     * @param n    the count of leading bytes
     * @return the new running size, or a write error
     */
    pub fn append_prefix(data: []byte, n: u64): u64 | error

    /**
     * The section byte length produced so far (the only live-RAM datum for content) — the input to
     * every offset computation (rule 3).
     * @return the running size in bytes
     */
    pub fn len(): u64 { self.size }

    /**
     * Concatenates the whole section into `out` in order (assembly pass), chunked.
     * @param out the destination stream (the final `.o`)
     * @return the bytes copied, or an I/O error
     */
    pub fn copy_into(out: teko::io::FileStream): u64 | error

    /**
     * Copies byte range `[start,end)` into `out` via seek-read (rodata partition reorders spans without
     * holding the section in RAM).
     * @param out   the destination stream
     * @param start inclusive start offset
     * @param end   exclusive end offset
     * @return the bytes copied, or an I/O error
     */
    pub fn copy_range_into(out: teko::io::FileStream, start: u64, end: u64): u64 | error

    /**
     * Reopens the section for record read-back (assembly pass reads symtab.meta/relocs.meta records).
     * @return null on success, or an I/O error
     */
    pub fn rewind_for_read(): error | null

    /**
     * Reads the next `n` bytes into `into` (a fixed record); 0 at EOF.
     * @param into the fixed destination buffer
     * @param n    the record size
     * @return bytes read, or an I/O error
     */
    pub fn read_record(into: []byte, n: u64): u64 | error

    /**
     * Closes and removes the temp file, once the `.o` is assembled.
     * @return null on success, or an I/O error
     */
    pub fn dispose(): error | null
}
```

Primitivas já existem (`io/file_stream.tks:272-520`): `open_write`/`stream_write`/`stream_write_prefix`/
`stream_seek`/`stream_size`/`stream_read`/`stream_close` + `gensym`. Buffer de coalescência FIXO ≤1024 por
`SectionBuf` amortiza appends pequenos (natureza BUFFER-DE-SAÍDA), sem acumular.

---

## 5. Metadado que persiste até a montagem (símbolos, relocs) — write-through + array-fixo, nunca copy-grow

Alguns dados precisam sobreviver da produção até a passada de montagem (o header/symtab/relocs referenciam
tamanhos e índices finais). REGRA: write-through na produção; na montagem, lê de volta pra um array-fixo
**pré-dimensionado pelo N conhecido** (regra 1) — nunca `[..lista,x]`.

- **Símbolos** (`.LstrN` locais + funções + globais + undefined): cada símbolo definido → uma entrada
  fixa `{name_off, value, size, sect, bind}` write-through em `symtab.meta` no ato da definição (regra 2/4).
  `name` → `strtab.sect` (write-through; `name_off` = tamanho corrente de strtab = math). Na montagem: N_sym
  = tamanho(`symtab.meta`)/record_size (conhecido) → lê pra `[N_sym]SymMeta`, **ordena locais-antes-de-
  globais** (exigência ELF; ordenação de N pequeno, array pré-dimensionado, O(N log N), NÃO copy-grow),
  emite o symtab no formato do alvo (Elf64_Sym 24 B / Mach-O nlist 16 B / COFF 18 B) por index-assign, e
  `sh_info` = 1º índice global (math).
- **Relocs** `{sect, offset, sym_ref, kind, addend}`: cada reloc → write-through em `relocs.meta` no ato
  da emissão da instrução/dado. Na montagem: N_rel conhecido → `[N_rel]RelMeta`, resolve `symidx` (lookup na
  tabela de símbolos ordenada — busca, não acumulação), emite as relas por index-assign.

Isto é a regra 4 aplicada ao bookkeeping: o metadado é ele mesmo uma section-file write-through; o único
momento em RAM é a leitura pra um array-fixo dimensionado pelo N-do-arquivo (contável), pra ordenar/resolver.

---

## 6. Censo COMPLETO — cada acumulador classificado (a)/(b)/(c) + cura pelas 4 regras

- **(a)** estrutura que JÁ existe → anota offset in-place (regra 2), nenhuma lista nova.
- **(b)** `[..lista,x]` que existe só pra descobrir N → torna N conhecido upstream (regra 1) + `[N]T`+
  index-assign; OU write-through se é bookkeeping que persiste (regra 4, §5).
- **(c)** conteúdo de seção materializado → arquivo-por-seção (regra 4, §4).

| # | Acumulador | arquivo:linha | Classe | Cura (regra) |
|---|---|---|---|---|
| 1 | `intern_rodata` `[..ctx.rodata, entry]` | `lower.tks:5225` (+`ctx_with_rodata:478`, `add_rodata` `lir.tks:247`, `with_rodata:254`, merges `:6565,6984`) | **(b)** existe só p/ numerar `.LstrN`+offset | contador `u64` (N via contador, R1) + símbolo write-through em `symtab.meta` (R2/4); bytes já em `rodata.sect` (C1). §3 |
| 2 | `find_const_rodata` varre a lista | `lower.tks:778` | (b) lookup | N pequeno (const globais) → array-fixo pré-dim. ou consulta `symtab.meta` |
| 3 | `add_func` `[..m.funcs, f]` | `lir.tks:235` | **(b)** module-wide | pipeline PER-FUNÇÃO (M-B): N_funcs conhecido (TProgram); função lowerizada→encodada→`text.sect`→descartada. Some a lista |
| 4 | `add_inst` `[..b.insts, inst]` | `lir.tks:207` | (b) per-função | dentro da função (M-B): insts vivem só na função corrente (buffer limitado); OU `[N_inst]` se contável do stmt. Não module-wide |
| 5 | `add_block` `[..f.blocks, b]` (+`add_block_param:283`) | `lir.tks:265` | (b) per-função | idem #4, per-função |
| 6 | `LEnv` 7 arrays paralelos (`lenv_bind*`) | `lower.tks:18-63` | **(b)** per-função | N_binds da função = conhecido do checker (locais/params) → `[N_binds]LBinding`+index-assign (R1/2); lookup = varredura (como hoje) |
| 7 | `add_global`/`add_layout` | `lir.tks:243,251` | (b) | N conhecido (globais/tipos do prog) → `[N]T`+índice |
| 8 | `str_to_bytes` `[..out, s[i]]` (JÁ array-fixo) | `lower.tks:5209` | (c) | **JÁ** `[s.len]byte`+índice — verify-only |
| 9 | encode `text` `append_bytes`/`push_byte`/`emit_u32_le` | `encode_x86_64.tks:35,+`; `encode_arm64.tks:57,+` | **(c)** | buffer POR-FUNÇÃO (patch in-place) → `text.sect.append_prefix` (M-A/B) |
| 10 | rodata section content | `encode_*.tks encode_rodata` | (c) | `rodata.sect` (já C1) |
| 11 | `emit_elf_object` `var b:[]byte=[]` | `objfile_elf.tks:712` | **(c)** o `.o` inteiro | montagem por concatenação (M-C): header math + `copy_into` das sections |
| 12 | `emit_macho` `var b:[]byte=[]` | `objfile_macho.tks:496` | (c) | idem #11 (arm64 macOS) |
| 13 | `emit_coff` `var b:[]byte=[]` | `objfile_coff.tks:359` | (c) | idem #11 (x86_64 Windows) |
| 14 | os 3 `ar` `var b:[]byte=[]` | `objfile_ar.tks:136,161`; `_macho:104,162,186`; `_coff:134,148` | (c) | archive escreve DIRETO no `.a`/`.lib`; membro `.o` via `copy_into` do `.o` em disco |
| 15 | FUNIL `finish_native_object(obj:[]byte)`/`finish_static_archive` → `write_file_bytes` | `project.tks:2199,2289,2281` | **(c)** keystone | abre/recebe o FileStream; dispatch → `write_*_object(out,…)`. Remove `write_file_bytes(obj)` |
| 16 | DWARF body (abbrev/info/line) | `objfile_elf.tks:736-747`; `dwarf.tks:53-623` | (c) | `dwarf_*.sect` write-through; montagem `copy_into` |
| 17 | symtab/strtab/relas metadata lists (`elf_build_symbols`/`_relas`/`build_*_strtab`/`elf_collect_const_entries`) | `objfile_elf.tks:76-118,232,462,559`; macho/coff equiv. | **(b)** | write-through `symtab.meta`/`strtab.sect`/`relocs.meta` + array-fixo na montagem (§5) |
| 18 | env-snapshots / arg-lists / layout-por-tipo (cauda) | `lower.tks:2216,2869-2874,3173-3553,6519-7092`; `lower_const.tks:31-637` | (b) | derivam de N conhecido → `[N]T`+índice (MAP/FILTRO+watermark) |
| 19 | regalloc RPO/eventos/intervalos/pins/subst | `regalloc.tks:61-1061`; `regalloc_x86.tks:86-397` | (b) per-função | derivados de N conhecido → array-fixo/FILTRO+watermark |
| 20 | `copy_*_to_current_region`/`commit_rodata_delta` | `project.tks:1706-1830,1974-2018` | (b/c) | copia array de `.len` conhecido → `[src.len]T`; com M-C some (a rodata já está em `rodata.sect`) |

**Nenhum deixado pra trás:** os 3 emitters de objeto (#11/12/13), os 3 `ar` (#14), o funil (#15), o DWARF
(#16), o LIR module-wide (#3/4/5), o LEnv (#6), a rodata (#1), o encode text (#9), o metadado de montagem
(#17), e as caudas contáveis (#7/18/19/20). Os 4 formatos cobertos.

---

## 7. Metodologia de execução WHOLESALE (não ladder — expurgo em uma passada)

Ordem (metodologia do expurgo D125/D181 — constrói a maquinaria, seed, o compilador ENUMERA o resto):

1. **CONSTRÓI a maquinaria (aditivo, convive com o velho):** `SectionBuf` generalizado (§4, todas as
   seções) + o `symtab.meta`/`relocs.meta`/`strtab.sect` write-through (§5) + o driver PER-FUNÇÃO (M-B) +
   a montagem por concatenação (M-C) + o funil FileStream (os 4 formatos, `write_*_object`/`write_static_
   archive_*`). NÃO remove o velho ainda; NÃO varre.
2. **SEED (gen0 ganha a maquinaria nova).** A superfície nova entra no `.tkh`/`teko.c`; reseed (rota C:
   fixpoint gen0→gen1 + ASan/UBSan + 3 harnesses).
3. **DESENSINA + REMOVE AS RAÍZES do estado velho:** `[..ctx.rodata,entry]`/`add_func`/`add_inst`/
   `add_block`/`lenv_bind`-copy-grow/`emit_*_object → []byte`/`write_file_bytes(obj)`/`var b:[]byte=[]`.
   A remoção faz o próprio compilador **ERRAR CRU** onde ainda referencia o velho.
4. **SEED.** Com o seed novo, o compilador tenta se auto-compilar e **os erros SÃO a lista de limpezas** —
   cada erro aponta um sítio a converter pro write-through/array-fixo. NÃO caçar à mão; **o compilador/
   linker ENUMERA**. Corrige → seed → repete até verde.
5. **VARREDURA (critério):** grep zero-ref dos removidos (`[..*.rodata`, `add_func(`, `add_inst(`, `var b:
   []byte`, `write_file_bytes(obj`) em `src/` (o emit native não toca `cases/examples/tklib/tooling`, mas o
   D191 manda conferir a árvore se algum símbolo `exp` mudou). Zero-ref = varrido.
6. **UMA VALIDAÇÃO no fim (não per-crumb):** a ladder native — `scripts/fixpoint_gate.sh
   TEKO_FIXPOINT_BACKEND=native` single-target host — com o **pico do passo-3 CAINDO** medido por
   `scripts/native_dry_gate.sh`, até **gen2-native COMPLETAR** (hoje OOM 13,5 GB) → **`gen2.o == gen3.o`
   byte-idêntico por-alvo**. Gate `native ≤ C+10%` (D206), marco `<2 GB` (D203). Rota native: `*San` N/A
   (D203) — checador = MEM_PARANOID; o `.o` reproduz byte-idêntico 2× + roda.

O reseed é iterativo dentro do expurgo (passos 2/4), mas a CONVERSÃO é uma unidade (uma passada do
implementer), não 12 crumbs bisectáveis. O funil dos 4 formatos + o keystone da rodata entram JUNTOS.

---

## 8. Determinismo do `.o` (`gen2.o==gen3.o`) sob write-through

Byte-preservante — muda ONDE os bytes moram, não a ORDEM nem o CONTEÚDO:
1. **Ordem preservada:** `append`/`append_prefix`/`copy_into` gravam/concatenam em ordem de produção
   (mesma que `append_bytes`/`[..x,e]`); o contador `.LstrN` numera igual; o pipeline per-função visita as
   funções na MESMA ordem que o `lower_program` monta o `LModule` hoje.
2. **Layout/offsets = math idêntica:** `compute_*_layout` sobre os mesmos tamanhos (agora `SectionBuf.len`)
   → mesmos offsets; ordem de seções fixa; up-front nos 3 formatos.
3. **Símbolos/relocs estáveis:** a ordenação locais-antes-globais (§5) é determinística (chave estável);
   `symidx` = math da posição ordenada. `relocs.meta` lida em ordem de gravação.
4. **Sem timestamp/path absoluto** (D203); os temp-paths dos `SectionBuf` NUNCA entram no `.o` (só conteúdo
   copiado); `ar` `mtime`/`mode` já zerados.

Encenação (D203/D205): fixpoint-C transitório (reseed, `teko.c` byte-idêntico) durante a bootstrap via
`teko.c`; `gen2.o==gen3.o` por-alvo = a validação final (§7.6) quando o subset native self-hospeda.

---

## 9. Riscos + tensões de lei (resolução recomendada)

1. **R1 — "rodata é streaming de N-desconhecido" (premissa das minhas versões anteriores):** REFUTADA pela
   regra 1 do dono. **Resolução:** o N do `.LstrN` é um CONTADOR (não `list.len`); os bytes vão pra
   `rodata.sect` (C1) e o símbolo pra `symtab.meta` (write-through) — nenhuma lista, nenhum chunked. O
   `Segmented` sai do desenho. Sem HALT (regras do dono deliberam).
2. **R2 — N_funcs / N_binds realmente conhecidos upstream?** N_funcs = decls + instâncias monomorfizadas da
   `TProgram` (o driver já as itera). N_binds = locais/params da função (checker). **Scout confirma** o
   ponto exato onde a contagem é lida ANTES do lowering da função; se um estágio não expõe N, **conserta o
   upstream** (regra 1) — não contorna. Baixo risco (a contagem existe na AST checada).
3. **R3 — pipeline per-função quebra o whole-program lowering?** Alguns passos são whole-program (rodata
   compartilhada, símbolos forward-ref). **Resolução:** rodata/símbolos são write-through GLOBAIS
   (section-files acumulam no disco entre funções, regra 4); só a LIR/regalloc é per-função (liveness
   local). Forward-refs entre funções = relocs por símbolo (resolvidas na montagem, §5) — já é assim.
4. **R4 — superfície: `SectionBuf` record read-back, `[N]T` init via local, genéricos:** precedentes
   existem (`[count]byte=[]` `rtio.tks:195`; read chunked `read_stream:503`). Scout valida; se faltar,
   ensina AGORA (lei "ensino agora").
5. **R5 — I/O temp-file no LOWER/ENCODE:** buffer de coalescência ≤1024 amortiza; troca GBs de recópia+
   vazamento por I/O de KB-MB → net-win. Determinismo intacto (temp-path fora do `.o`).
6. **R6 — partition data_rel_ro reordena rodata:** decisão = metadado puro (símbolos+relocs); montagem faz
   `copy_range_into` por span na ordem nova. Byte-preservante.
7. **R7 — ordenação de símbolos na montagem lê N_sym em RAM:** é array-fixo pré-dimensionado (N do arquivo),
   O(N log N) de sort, NÃO copy-grow; N_sym ~dezenas de mil de records pequenos = MB, folgado sob 2 GB.
8. **R8 — `Map`/`Dictionary`/`Hashset` copy-grow** (`collections/map.tks:31`): achado adjacente REPORTADO
   (dívida NO-PUSHES da rota C, fora deste desenho; `intern_rodata` não dedupa).

**Nenhuma tensão genuinamente aberta → sem HALT.** (Protocolo de fork: DECISION_LOG D182/D207/D208/D206/
D203 + as 4 regras do dono + a medição do coordenador — convergem no desenho write-through UNIFICADO; o
container chunked é descartado por decisão mais-recente do dono; continua do C1, não reverte.)
