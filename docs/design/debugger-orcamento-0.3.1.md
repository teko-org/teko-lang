# Orçamento e desenho de um debugger para Teko (arquiteto, 2026-07-30)

> Ordem do dono: *"mande um arquiteto orçar a implementação de um debugger, que funcione em
> terminal (pode ser gdb) mas que eu tbm consiga usar em um VSCode, por exemplo"*

Este documento **orça e desenha**. Não implementa. O `pode ser gdb` é lido como a parte mais
importante da ordem: o pedido é **poder depurar**, não *ter um depurador nosso*. O orçamento
abaixo é organizado para gastar o mínimo possível em ferramenta própria e o máximo possível em
ferramenta que já existe — e para dizer, com medição, **onde a ferramenta existente para de
servir**.

---

## 0. Sumário executivo — o que recomendo

| | |
|---|---|
| **Parar em** | Camada 1 **redefinida** (tabela de linha + CU mínima + DIE por função), alvos ELF e Mach-O |
| **O que isso entrega** | breakpoint por linha de `.tks`, `step`, `next`, `bt` com nomes Teko — no **terminal** (gdb *e* lldb) e no **VSCode** (`cppdbg`/CodeLLDB), **sem servidor nosso** |
| **Custo** | 6 crumbs, todos aditivos; o escritor DWARF é um módulo novo isolado |
| **Adiado sem juros** | Camada 2 (variáveis/tipos), Camada 3 (legibilidade), Windows/PDB |
| **Maior correção ao esboço** | a Camada 1 do esboço (**`.debug_line` sozinho**) **não funciona** — medido. Mas o mínimo que funciona é muito mais barato do que "DWARF completo" sugere. |
| **Red-flags** | 3 (§9): o `str` em obra, o desempate de nomes através do regalloc, a confiança no desenrolar de pilha |

---

## 1. As três medições do dono — confirmadas, e o que elas não diziam

### Medição 1 — "não existe informação de depuração em lado nenhum" — **CONFIRMADA, e mais forte**

`grep -niE "dwarf|debug_line|debug_info|\.debug"` em `src/backend/` e `src/lir/` devolve vazio.
Estendi a busca a **todo** o repositório, incluindo `.md` e `.sh`, e a nomes que o dono não
procurou (`debug_abbrev`, `debug_str`, `DW_TAG`, `DW_AT_`, `stmt_list`): **também vazio**. O
terreno é virgem — não há meia-implementação para resgatar nem para respeitar.

### Medição 2 — "o emissor de C não põe `#line`" — **CONFIRMADA**

`grep '#line'` em `src/codegen/` devolve vazio.

Mas há um detalhe que muda o custo da Camada 0, e é a favor: `src/codegen/codegen.tks` **já
consome posição de fonte** em quatro lugares — `emit_cov_line` (que emite
`tk_cov_line_at(<fn_idx>, <line>)`), `emit_cov_branch`, `tk_error_loc` e `tk_panic_oob_at`. Ou
seja, a forma "no ponto de emissão de um statement, escrever um marcador que carrega
`e.line`" **já existe no mesmo ficheiro, quatro vezes**. `#line` é a quinta.

E o nome do ficheiro-fonte, que `#line` exige, **também já existe**: `checker::TFunction`
carrega `file: str`, `line: u32` e `col: u32` (`src/checker/tast.tks`, campos marcados `(E3)`).

### Medição 3 — "`LInst` já carrega `line`/`col` em toda instrução" — **CONFIRMADA, e mais forte
num ponto, mas FURADA noutro**

Mais forte: os construtores não só *recebem* `line`/`col`, eles recebem **valores reais**. Em
`src/lir/lower.tks` os sítios de chamada passam `e.line`/`e.col` do nó da árvore tipada, não
zeros — verifiquei dezenas de sítios (`load_inst(r.vreg, sslot.slot, sslot.ty, e.line, e.col)`,
`bin_inst(r.vreg, op, lo.vreg, ro.vreg, e.line, e.col)`, …). A fiação está viva, não é um campo
morto que alguém declarou e nunca preencheu. Isto é o melhor achado do dia e sustenta a
conclusão do dono.

**Onde a conclusão fura.** O dono concluiu: *"a parte difícil já está feita… o que falta é
consumir essa posição."* Corrijo em duas frentes, e a correção é o que dá forma à Camada 1:

1. **A posição morre na seleção de instruções.** `LInst` carrega `line`/`col`; `MInst`
   (`src/backend/minst.tks`) e `MInstX86` (`src/backend/minst_x86.tks`) **não carregam nada**.
   `MBlock`/`MFunc`/`MModule` e os equivalentes x86 também não. Portanto a posição chega ao LIR
   e **é descartada antes de existir um byte de código de máquina** — e é exatamente o
   par (endereço de máquina → linha) que um breakpoint precisa. Não há o que "consumir" no
   backend hoje; há o que **reconduzir**.

2. **O ficheiro nunca entra no LIR.** `LFunc` é
   `struct { symbol; n_params; param_types; ret_type; blocks; next_vreg }` — sem `file`, sem
   linha de declaração. `LModule` não tem tabela de ficheiros. A informação existe em
   `TFunction` e é **perdida** em `lower_function`, que recebe o `TFunction` inteiro. DWARF
   precisa dela (a CU precisa de um nome de ficheiro; cada `DW_TAG_subprogram` precisa de
   `DW_AT_decl_file`/`DW_AT_decl_line`).

**Mas a reparação é barata, e é por uma razão que só se vê olhando o código.** Ambos os
selecionadores canalizam **toda** emissão de instrução por **uma única função**:

- arm64: `selctx_emit(ctx: SelCtx, inst: MInst): SelCtx` (`src/backend/isel_arm64.tks`)
- x86-64: `selctx_x86_emit(ctx: SelCtxX86, inst: MInstX86): SelCtxX86` (`src/backend/isel_x86_64.tks`)

e ambos despacham por `LOp` numa única função que **já recebe o `lir::LInst` inteiro**
(`select_inst`, `select_inst_x86`), chamada de um único laço (`select_insts`,
`select_insts_x86`). Existe, portanto, **um** sítio por arquitetura onde a linha corrente é
conhecida e **um** sítio por arquitetura por onde toda instrução passa. Não é uma reescrita de
28 casos de variante; é um funil.

**Veredito sobre a conclusão do dono:** sobrevive, mas por um motivo diferente do que ele
enunciou. Não é que a posição já chega ao gerador de código — ela não chega. É que o backend
tem **a forma certa** para a levar até lá com pouquíssimo toque. O custo que ele orçou a zero
não é zero, mas é **um crumb**, não uma onda.

### Uma quarta medição, que ninguém pediu e que muda o desenho: `.tsym` já existe

`TEKO_LEGISLATION.md` **já legisla** o artefacto de símbolos de depuração:

> **`.tsym`** — *Teko Symbols* (debug symbols: file:line + names for the debugger + stack traces — Eixo E).

E `codegen::tk_emit_tsym` **já o emite**: uma linha por função, `<símbolo-C>\t<nome-teko>\t<file>:<line>`.
`src/build/project.tks` escreve-o ao lado do binário na rota nativa e mete-o dentro do `.tkl`.
`TEKO_ROADMAP_NATIVE_BACKEND.md` diz explicitamente que *"DWARF/PDB está fora de escopo do M1;
o `.tsym` (E3) já resolve stack traces sem DWARF"*.

**Resolução law-first, sem tensão:** não há conflito a levar ao dono. `.tsym` é
**por-função** (`file:line` da declaração); um breakpoint precisa de **por-endereço**. São
granularidades diferentes, não formatos rivais. O desenho abaixo trata a tabela de linha
interna como **a única fonte**, e `.tsym` e `.debug_line` como **dois consumidores** dela —
que é palavra por palavra o padrão que o roadmap já manda seguir para a tabela de símbolos
(*"mesma informação… dois consumidores"*). Nada do que existe é revogado; `.tsym` continua a
servir stack traces sem depender de DWARF.

---

## 2. As medições novas — as que realmente fixaram o orçamento

Fiz quatro experimentos neste host (gdb 15.1, lldb, binutils, `cc`), fora da árvore de
produto. Estão aqui porque **dois deles movem a fronteira entre camadas**.

### Experimento A — `#line` na rota C entrega mais do que o dono esperava

Um `.c` com `#line <n> "hello.tks"` compilado com `cc -g -O0`:

```
Breakpoint 1, tk_add (a=2, b=3) at …/hello.tks:2
2	    let s = a + b
Source language is c.
#0  tk_add (a=2, b=3) at …/hello.tks:2
#1  0x… in main () at …/hello.tks:6
$1 = 2
$2 = 3
```

Breakpoint por linha de `.tks`, **listagem do texto real do `.tks`**, backtrace em `.tks`, **e
`print a` a funcionar**. O dono orçou a inspeção de variáveis na Camada 2; na rota C ela vem
**de graça**, porque os locais do C emitido herdam DWARF completo do `cc`. Isto é a favor da
Camada 0 — e é também o aviso de que a Camada 0 cria uma expectativa que a Camada 1 **não** vai
cumprir (§6).

### Experimento B — `.debug_line` sozinho **não funciona**. Esta é a correção que importa.

Removi de um binário com DWARF tudo menos `.debug_line`:

```
$ gdb -batch -ex "break hello.tks:2" -ex run ./hello_lineonly
No symbol table is loaded.  Use the "file" command.
[Inferior 1 exited with code 05]
No stack.
```

gdb **não indexa `.debug_line` isoladamente**. Ele descobre tabelas de linha a partir das
unidades de compilação em `.debug_info` (via `DW_AT_stmt_list`). Uma seção `.debug_line`
perfeita, sem CU, é bytes mortos.

**Consequência para o esboço:** a Camada 1 tal como definida — *"`.debug_line` no backend
nativo. O mínimo para pôr breakpoint por linha e ver stack em `.tks`"* — **não é atingível**.
`.debug_info` e `.debug_abbrev`, que o esboço pôs na Camada 2, são **pré-requisito do primeiro
breakpoint**. A fronteira tem de descer.

### Experimento C — mas o mínimo que funciona é **muito** mais barato do que "DWARF completo"

`cc -gdwarf-4 -g1` é exatamente a fronteira útil, medida:

```
$ gdb -batch -ex "break hello.tks:2" -ex run -ex bt -ex "print a" ./hello_g1
Breakpoint 1, tk_add () at hello.tks:2
2	    let s = a + b
#0  tk_add () at hello.tks:2
#1  0x… in main () at hello.tks:6
No symbol "a" in current context.
```

Breakpoint ✓, listagem ✓, backtrace com nomes ✓, variáveis ✗. É precisamente a experiência que
o dono quer da Camada 1, e o `gcc` produz para 2 funções apenas: `.debug_info` = **138 bytes**,
`.debug_line` = 95 bytes, mais `.debug_abbrev` e `.debug_str`.

### Experimento D — o mínimo cabe nas relocações que o backend **já** tem. Este é o achado mais valioso.

O `-g1` do `gcc` gera 13 relocações nas seções de depuração, de **dois** tipos
(`R_X86_64_64` e `R_X86_64_32`) — e `R_X86_64_32` não existe no nosso backend, que só conhece
`Abs64`/`Pc32`/`Plt32`. Se fosse preciso, a Camada 1 pagaria: kind novo de relocação, símbolos
`STT_SECTION` para cada seção de depuração, `.rela.debug_*`, nos três formatos.

Escrevi então **à mão** o DWARF-4 mínimo — CU + abbrev + programa de linha — evitando
deliberadamente tudo que exige relocação de 32 bits: **strings inline** (`DW_FORM_string` em
vez de `DW_FORM_strp`, o que elimina `.debug_str` inteiro), e `abbrev_offset`/`stmt_list`
como **literal 0**, o que é correto porque há **uma única CU por objeto** — e Teko emite **um
objeto por programa**. Montei, liguei e testei:

```
$ readelf -rW mini.o | grep -c R_X86_64_64        →  4      (as únicas nas seções de depuração)
$ gdb -batch -ex "break hello.tks:2" -ex run -ex bt ./mini
Breakpoint 1 at 0x1003: file hello.tks, line 2.
Breakpoint 1, add () at hello.tks:2
2	    let s = a + b
#0  add () at hello.tks:2
#1  0x… in main () at hello.tks:6
```

E o **mesmo objeto**, sem uma alteração, no lldb:

```
$ lldb -b -o "breakpoint set --file hello.tks --line 2" -o run -o bt ./mini
Breakpoint 1: where = mini`add + 3 at hello.tks:2:1
* frame #0: 0x… mini`add at hello.tks:2:1
    frame #1: 0x… mini`main at hello.tks:6:1
```

**O que isto compra, medido:**

| custo que o esboço temia | veredito medido |
|---|---|
| `.debug_str` | **não é preciso** — strings inline |
| `.debug_aranges`, `.debug_line_str`, `.debug_rnglists` | **não são precisos** |
| kind novo de relocação (`R_X86_64_32`) | **não é preciso** |
| símbolos de seção para as seções de depuração | **não são precisos** |
| total de relocações | **4**, todas `Abs64`, contra símbolos de `.text` que a tabela de símbolos **já** tem |
| um escritor por debugger (gdb vs lldb) | **não** — um escritor serve os dois, bytes idênticos |

Só três seções novas (`.debug_abbrev`, `.debug_info`, `.debug_line`), zero conceitos novos de
relocação. `RelocSect` (`src/backend/minst.tks`, hoje `enum { Text; Rodata }`) precisa de dois
membros novos, e essa extensão tem **precedente exato**: `Rodata` foi acrescentado por esta
razão em #594 T-B1, e os três escritores já **particionam** por `RelocSect`
(`coff_partition_relocs`, `macho_partition_relocs`, e a partição do ELF) — o verificador de
tipos aponta cada sítio que falta.

---

## 3. Uma medição que muda *como* a posição viaja (e evita o crumb caro)

Antes de desenhar, medi como o fluxo de instruções é tratado depois da seleção, porque a
solução óbvia — um array paralelo de posições ao lado de `MBlock.insts` — só é correta se
ninguém reescrever o fluxo.

`src/backend/regalloc.tks` expõe:

```
pub fn rewrite_inst(abi: AbiDescriptor, sr: ScanResult, inst: MInst, frame_base: u64): []MInst
```

**Uma instrução vira N.** O alocador de registos expande derramamentos e recargas. Um array
paralelo **dessincroniza** no primeiro derramamento e passa a mentir sobre posições — o pior
defeito possível num debugger, porque mente em silêncio.

Restam duas formas honestas, e a diferença de custo entre elas é grande:

**(a) Envolver cada instrução** num `struct { inst; line; col }`, como `LInst` envolve `LOp`.
Correto, mas toca **todo** `match inst` do backend — `inst_regs`, `map_minst_regs`,
`rewrite_inst`, `encode_inst_word`, `encode_inst_x86`, os dois selecionadores — vezes duas
arquiteturas. É a onda que o dono temia, e cai exatamente sobre os ficheiros onde há agentes
vivos.

**(b) Uma pseudo-instrução marcadora de zero bytes** — `MLineMark` — emitida pelo selecionador
quando a linha do `LInst` corrente muda, propagada pelo alocador como identidade, e traduzida
pelo codificador numa **linha da tabela** no deslocamento de byte corrente, emitindo **zero
bytes** de código.

**Recomendo (b)**, e a razão é de lei, não de gosto:

- **É aditiva.** Um membro novo na variante e um braço novo em cada `match` exaustivo. Nenhuma
  assinatura existente muda, nenhum golden de bytes existente muda (zero bytes emitidos). É o
  mínimo de colisão com os cinco agentes vivos.
- **É reversível por construção** — a régua desta lane. Apagar o membro da variante remove a
  funcionalidade inteira; nada mais foi tocado.
- **Sobrevive à reescrita 1→N**: `rewrite_inst` devolve `[MLineMark]` para o marcador, e o
  código de derramamento fica *dentro* do intervalo do marcador anterior — que é
  semanticamente correto, porque um derramamento pertence ao statement que o causou.
- **Dá granularidade de statement de graça**, que é o que um breakpoint quer (uma linha por
  fronteira de statement, `is_stmt`), em vez de uma linha por instrução de máquina — programa
  de linha menor e `step` que se comporta.

---

## 4. A tabela de camadas — custo e o que o dono ganha

Custo em **crumbs** (a unidade desta lane: um passo pequeno, com prova própria e reversível).
"Camada 1" está **redefinida** face ao esboço, pela razão do Experimento B.

| Camada | Conteúdo | Custo | No **terminal** o dono consegue | No **VSCode** o dono consegue |
|---|---|---|---|---|
| **0** — `#line` na rota C | `#line` no C emitido + `-g` na linha do `cc` + **o arnês de prova** | **1 crumb** (+1 do arnês, que é reaproveitado) | breakpoint, `step`, `bt`, **e ver variáveis** — em `.tks` | tudo isso, via `cppdbg`, sem servidor nosso | 
| **1** — posição no backend nativo (**redefinida**) | `.debug_line` **+ `.debug_abbrev` + `.debug_info`** (1 CU + 1 DIE por função) | **6 crumbs** | breakpoint por linha de `.tks`, `step`/`next`, `bt` com nomes Teko. **Não** vê variáveis | idem, via `cppdbg` (Linux) e CodeLLDB (macOS), **sem servidor nosso** | 
| **2** — variáveis, tipos, frames | tabela de locais, tipos DWARF, `DW_AT_location`, CFI | **o penhasco — 5+ crumbs, um deles perigoso** | `print x`, ver membros de struct, `frame`/`up`/`down` fiáveis | painel *Variables* e *Watch* do VSCode a funcionar | 
| **3** — legibilidade | pretty-printers Python do gdb para `str`, uniões, slices | **1 crumb**, mas **bloqueado** (§7) | `str` como texto, união pelo membro ativo | idem (o `cppdbg` carrega pretty-printers do gdb) | 
| **3-DAP** — servidor próprio | adaptador DAP nosso | **alto, e recomendo NÃO fazer** | nada de novo | nada que `cppdbg` já não dê | 

### O que a Camada 1 deixa de fora, dito sem maquiagem

Depois da Camada 1 o dono põe um breakpoint em `main.tks:410`, para lá, vê a linha do `.tks` na
tela, faz `next` linha a linha, e faz `bt` para ver a cadeia de chamadas com nomes Teko. Ele
**não** consegue `print x`. Esta é a diferença exata entre `-g1` e `-g2` no gcc, e é a fronteira
que medi no Experimento C.

Vale dizer com clareza: **a fatia que resolve "onde estou e como cheguei aqui" é a Camada 1**, e
"onde estou e como cheguei aqui" é o que resolve a esmagadora maioria das sessões de depuração
de um compilador. A Camada 2 resolve "e quanto vale `x`", que é onde o `print` de diagnóstico
temporário já serve hoje.

---

## 5. A sequência de crumbs

Cada crumb tem **ficheiro/função onde mexe** e **o teste que o prova**. Um crumb sem forma de
prova não é um crumb — nenhum abaixo está sem.

### Fase 0 — o arnês vem primeiro (nenhum código de produto)

> Isto é deliberado, e é a maior lição que retiro de `scripts/check_elf.sh`, cujo cabeçalho
> registra dois defeitos de CI que custaram caro: *"AN ABSENT OBJECT WAS A PASS"*. Um arnês de
> depuração tem o mesmo risco elevado ao quadrado — um teste que "não conseguiu pôr o
> breakpoint" e devolve 0 dá verde para sempre. O arnês nasce com prova **negativa**.

**Crumb D0.1 — `scripts/check_debug_pos.sh` + o seu binário de referência**

- **Mexe em:** `scripts/check_debug_pos.sh` (novo), `scripts/debug_pos_reference.c` (novo — um
  `.c` escrito à mão com directivas `#line` para um `.tks` de referência). O precedente de um
  `.c` auxiliar em `scripts/` já existe: `scripts/ar_link_run_consumer.c`.
- **O que faz:** dado um binário, um `.tks`, uma linha e uma profundidade de pilha esperada,
  corre `gdb -batch` e afirma **quatro** coisas: (i) o breakpoint **resolveu** para
  `<ficheiro>.tks:<linha>` (não ficou pendente), (ii) o processo **parou** lá, (iii) a listagem
  de fonte mostra o **texto do `.tks`**, (iv) `bt` tem pelo menos a profundidade pedida. Toda
  ausência (sem gdb, sem binário, breakpoint pendente) é **falha dura**, nunca "skipped" —
  agendar é trabalho do chamador, pela regra que `check_elf.sh` já fixa.
- **Prova:** verde contra `debug_pos_reference.c` compilado com `-g`; **vermelho** contra o
  mesmo compilado com `-g0`. A prova negativa é metade do crumb.
- **Reversão:** apagar dois ficheiros.
- **Colisão:** nenhuma — não toca em `src/`.

**Crumb D0.2 — `#line` no C emitido (opcional; ver §6 antes de gastar)**

- **Mexe em:** `src/codegen/codegen.tks` — uma função nova espelhando `emit_cov_line`, chamada
  do mesmo sítio onde `emit_cov_line` é chamada; lê `f.file` do `TFunction` corrente e
  `s.line`/`e.line` do nó. `src/build/project.tks` — `-g` na linha do `cc` sob o perfil pedido.
- **Prova:** `check_debug_pos.sh` contra um binário construído por `teko build` na rota C.
- **Colisão: ALTA** — há agentes vivos em `src/codegen/codegen.tks` **e** em
  `src/build/project.tks`. Se a Camada 0 for aprovada, agendar depois deles.

A forma, para ser copiada literalmente (Javadoc completo, sem `//`, e note-se que uma função
sem valor de retorno **não leva seta** — `-> void` é banido):

```teko
/**
 * emit_line_directive — append a `#line <n> "<file>"` directive to the emitted C, so the host
 * `cc`'s own DWARF points the debugger at the `.tks` source instead of at the generated C.
 *
 * A no-op when `line` is 0 (an unpositioned node), matching `emit_cov_line`'s guard: a zero
 * line would make `cc` renumber from an unknown origin and corrupt every position after it.
 * The directive is emitted at statement granularity only — one per statement, never per
 * expression — because a breakpoint resolves to a statement and a finer stream only bloats
 * the C without moving a breakpoint.
 *
 * @param buf     the emission buffer to append to
 * @param file    the owning function's source file (`checker::TFunction.file`)
 * @param line    the statement's 1-based source line (0 = skip)
 * @param indent  the current indentation prefix
 * @return []byte  `buf` followed by the directive, or `buf` unchanged when `line` is 0
 * @since debugger camada 0
 * @see emit_cov_line  the coverage-mark sibling this mirrors, same call site
 */
fn emit_line_directive(buf: []byte, file: str, line: u32, indent: str): []byte {
    if line == 0 { return buf }
    if file.len == 0 { return buf }
    mut b = cb(cb(buf, indent), "#line ")
    b = cb(cb_u64_digits(b, line to u64), " \"")
    cb(cb(b, file), "\"\n")
}
```

### Fase 1 — a posição sobrevive até ao objeto (a camada que recomendo entregar)

**Crumb D1.1 — `LFunc` passa a levar o ficheiro e a linha de declaração**

- **Mexe em:** `src/lir/lir.tks` — `LFunc` ganha `file: str`, `decl_line: u32`, `decl_col: u32`.
  `src/lir/lower.tks` — `lower_function` já recebe `f: checker::TFunction`, portanto os três
  valores são uma cópia direta de `f.file`/`f.line`/`f.col`.
- **Prova:** teste unitário em `src/lir/lower_test.tkt` (ou o `.tkt` correspondente): dado um
  `TFunction` com `file = "a/b.tks"`, `line = 7`, o `LFunc` produzido devolve os mesmos valores.
- **Reversão:** três campos.
- **Colisão: ALTA** — agente vivo em `src/lir/lower.tks`.

**Crumb D1.2 — a posição chega às instruções de máquina (`MLineMark`)**

- **Mexe em:**
  - `src/backend/minst.tks` — o tipo `MLineMark` e o seu membro em `MInst`; dois membros novos
    em `RelocSect`.
  - `src/backend/minst_x86.tks` — `MLineMarkX86` e o seu membro em `MInstX86`.
  - `src/backend/isel_arm64.tks` — `SelCtx` ganha `cur_line`/`cur_col`; `select_insts` atualiza
    antes de despachar e emite o marcador quando a linha **muda**; `selctx_emit` intocado.
  - `src/backend/isel_x86_64.tks` — o mesmo em `SelCtxX86`/`select_insts_x86`.
  - `src/backend/regalloc.tks` (`inst_regs`, `map_minst_regs`, `rewrite_inst`) e
    `src/backend/regalloc_x86.tks` — um braço cada: sem registos, identidade, `[inst]`.
  - `src/backend/encode_arm64.tks` (`encode_inst_word`, `encode_func`) e
    `src/backend/encode_x86_64.tks` (`encode_inst_x86`, `emit_one_x86`) — zero bytes emitidos,
    uma linha registada no acumulador (`FuncEmitX86` / o equivalente arm64 ganham a tabela).
- **Prova:** três testes unitários, cada um a fechar um furo distinto:
  1. `isel_x86_64_test.tkt` / `isel_arm64_test.tkt` — dois `LInst` com linhas diferentes
     produzem **dois** marcadores; dois com a mesma linha produzem **um**.
  2. `regalloc_test.tkt` — um `MLineMark` atravessa `rewrite_inst` **numa função com
     derramamento forçado**, e continua **antes** do código derramado. É este teste que
     defende a decisão de §3; sem ele a decisão é uma opinião.
  3. `encode_x86_64_test.tkt` / `encode_arm64_test.tkt` — os bytes de `.text` ficam
     **byte-idênticos** ao golden atual (o marcador não emite nada) e a tabela de linha sai com
     os pares `(deslocamento, linha)` esperados. A identidade de bytes é o que torna o crumb
     seguro.
- **Colisão: ALTA** — agentes vivos em `isel_x86_64` e `encode_x86_64`. A escolha aditiva de §3
  é o que mantém este crumb aplicável mesmo com esses ficheiros a mudar.

```teko
/**
 * MLineMark — a zero-byte pseudo-instruction marking the start of a source statement's machine
 * code: the join point between `lir::LInst`'s carried `line`/`col` and the emitted byte offset,
 * and therefore the single origin of every line-table row.
 *
 * It exists as an instruction rather than as a side array because register allocation rewrites
 * the stream ONE-TO-MANY (`regalloc::rewrite_inst` returns `[]MInst`, expanding spills and
 * reloads), which desynchronises any array kept parallel to `MBlock.insts`. Travelling INSIDE
 * the stream makes the position survive that expansion, and puts spill code inside the range of
 * the statement that caused it — which is where it belongs.
 *
 * It encodes to NO bytes, so every existing `.text` golden stays byte-identical.
 *
 * @since debugger camada 1
 * @see lir::LInst  the carrier this reads `line`/`col` from
 */
pub type MLineMark = struct {
    /**
     * line — the statement's 1-based source line, copied from the `lir::LInst` being selected.
     */
    line: u32
    /**
     * col — the statement's 1-based source column, copied from the same `lir::LInst`. Carried
     * because DWARF's line program has a column register and `lldb` reports `file:line:col`.
     */
    col: u32
}

/**
 * LineRow — one resolved row of the line table: a byte offset within the function's emitted
 * `.text` image, and the source position that offset belongs to.
 *
 * Produced by the encoder (which is the only pass that knows byte offsets) from the
 * `MLineMark`s the selector planted, and consumed by `dwarf::emit_line_program`. Offsets are
 * FUNCTION-relative; the object writer rebases them onto the section.
 *
 * @since debugger camada 1
 */
pub type LineRow = struct {
    /**
     * offset — the function-relative byte offset the row starts at.
     */
    offset: u32
    /**
     * line — the 1-based source line in effect from `offset` onward.
     */
    line: u32
    /**
     * col — the 1-based source column in effect from `offset` onward.
     */
    col: u32
}
```

**Crumb D1.3 — `src/backend/dwarf.tks`: o escritor DWARF, agnóstico de ISA e de formato de objeto**

- **Mexe em:** ficheiro **novo**. Nenhum ficheiro existente. É o crumb mais seguro do conjunto
  e o mais substancial.
- **Conteúdo:** ULEB128/SLEB128; o cabeçalho e o programa de linha DWARF-4; a tabela de
  abreviaturas mínima (duas abreviaturas: `DW_TAG_compile_unit`, `DW_TAG_subprogram`); a CU
  mínima com strings **inline**. Nada de `.debug_str`, nada de `.debug_aranges` — pelo
  Experimento D.
- **Prova:** goldens de bytes em `src/backend/dwarf_test.tkt`, **fixados contra os bytes do
  objeto que verifiquei à mão** no Experimento D. Isto é o que torna o crumb provável sem
  correr um debugger: o alvo não é uma leitura da especificação, é um artefacto que **já** fiz
  gdb e lldb aceitarem. Mais um teste de mesa por primitiva (ULEB128 de 0, 127, 128, 624485).
- **Reversão:** apagar um ficheiro e um teste.
- **Colisão:** nenhuma.

```teko
/**
 * DwarfUnit — everything the DWARF writer needs about one compiled program to emit the three
 * minimal sections. Deliberately ISA-agnostic and object-format-agnostic: `emit_elf`/`emit_macho`
 * consume the SAME bytes (verified — one hand-built object satisfied both gdb and lldb
 * unchanged), so this type is the whole contract between the backend and every debugger.
 *
 * @since debugger camada 1
 */
pub type DwarfUnit = struct {
    /**
     * comp_dir — the compilation directory the debugger resolves a relative `file` against
     * (DW_AT_comp_dir). An absolute path; a wrong one makes the debugger find the breakpoint
     * but fail to LIST the source, which is the confusing half-failure to avoid.
     */
    comp_dir: str
    /**
     * name — the unit's primary source file (DW_AT_name), the `.tks` path a `break <file>:<n>`
     * must name.
     */
    name: str
    /**
     * funcs — one entry per emitted function, in `.text` emission order.
     */
    funcs: []DwarfFunc
}

/**
 * DwarfFunc — one function's debug description: the symbol its address relocates against, the
 * Teko name the debugger shows in a backtrace, its declaration position, and its line rows.
 *
 * @since debugger camada 1
 */
pub type DwarfFunc = struct {
    /**
     * symbol — the already-mangled symbol the `DW_AT_low_pc` relocation targets. The symbol
     * table already carries it, so no new symbol is created for debug info.
     */
    symbol: str
    /**
     * teko_name — the qualified Teko name (`ns::fn`) shown in a backtrace, the SAME string
     * `codegen::tk_emit_tsym` writes into the `.tsym` map: one origin, two consumers.
     */
    teko_name: str
    /**
     * decl_line — the function's 1-based declaration line (DW_AT_decl_line), from
     * `lir::LFunc.decl_line`.
     */
    decl_line: u32
    /**
     * code_size — the function's emitted `.text` byte length, which becomes `DW_AT_high_pc` in
     * its DWARF-4 `data8` (length, not address) form — the form that needs NO relocation.
     */
    code_size: u32
    /**
     * rows — the function's line rows in ascending `offset` order, from the encoder.
     */
    rows: []LineRow
}

/**
 * DwarfSections — the emitted images plus the relocations the object writer must place. The
 * relocation list is the reason this type exists instead of three loose byte arrays: a line
 * program and a CU both hold ABSOLUTE code addresses only the linker can fill.
 *
 * Every relocation here is `Abs64` against a `.text` symbol — MEASURED, not assumed: a
 * hand-built minimal unit needed exactly four, all of that one kind, which the backend already
 * supports. No new relocation kind, and no section symbol, is required by camada 1.
 *
 * @since debugger camada 1
 */
pub type DwarfSections = struct {
    /**
     * info — the `.debug_info` image (one compile-unit DIE plus one subprogram DIE per function).
     */
    info: []byte
    /**
     * abbrev — the `.debug_abbrev` image (the two abbreviations the DIEs above reference).
     */
    abbrev: []byte
    /**
     * line — the `.debug_line` image (header plus line-number program).
     */
    line: []byte
    /**
     * relocs — the absolute code addresses to patch, each tagged with the section its patch
     * site lives in, for the writer to route to `.rela.debug_info` / `.rela.debug_line`.
     */
    relocs: []DwarfReloc
}

/**
 * emit_dwarf — the whole camada-1 writer: one unit in, three section images plus their
 * relocations out. Pure and total — no honest-stop, because every input shape is representable
 * (an empty `funcs` yields a valid CU with no children, which every debugger accepts).
 *
 * @param DwarfUnit unit  the program's debug description
 * @return DwarfSections  the three section images plus the relocations to place
 * @since debugger camada 1
 * @example
 *   let secs = dwarf::emit_dwarf(DwarfUnit { comp_dir = "/src"; name = "main.tks"; funcs = fs })
 */
pub fn emit_dwarf(unit: DwarfUnit): DwarfSections
```

> O corpo do `emit_dwarf` é o trabalho do D1.3 e **não** está esboçado aqui de propósito: um
> corpo-fantasma neste documento seria copiado. O que está fixado, e é o que o implementador
> precisa, é a **assinatura**, os **tipos**, e o **alvo em bytes** — os goldens do
> `dwarf_test.tkt` saem do objeto que verifiquei no Experimento D, não de uma leitura da
> especificação.

As três primitivas internas que o D1.3 precisa, com as suas formas fixadas:

```teko
/**
 * emit_uleb128 — append `value` in DWARF's unsigned little-endian base-128 encoding: seven
 * payload bits per byte, low group first, the high bit set on every byte but the last.
 *
 * The encoding every DWARF length, abbreviation code, attribute code and form code uses, so it
 * is the most-called function in the writer.
 *
 * @param buf     the buffer to append to
 * @param value   the value to encode
 * @return []byte  `buf` followed by the encoded bytes (always at least one, `0` encoding as a
 *                 single zero byte)
 * @since debugger camada 1
 */
pub fn emit_uleb128(buf: []byte, value: u64): []byte

/**
 * emit_sleb128 — append `value` in DWARF's SIGNED little-endian base-128 encoding, whose sign
 * rule differs from `emit_uleb128`'s: the terminating byte's bit 6 must equal the sign, so a
 * negative value may need one byte MORE than its magnitude suggests.
 *
 * Needed by `DW_LNS_advance_line`, which is the only signed field the camada-1 writer emits —
 * and the reason a line table can step BACKWARD, which happens whenever a loop's condition is
 * re-evaluated after its body.
 *
 * @param buf     the buffer to append to
 * @param value   the signed value to encode
 * @return []byte  `buf` followed by the encoded bytes
 * @since debugger camada 1
 */
pub fn emit_sleb128(buf: []byte, value: i64): []byte

/**
 * emit_line_program — one function's `LineRow`s rendered as a DWARF line-number program: a
 * `DW_LNE_set_address` anchoring the function's first row (the ONLY relocated field in the
 * section), then `DW_LNS_advance_pc` / `DW_LNS_advance_line` / `DW_LNS_set_column` /
 * `DW_LNS_copy` steps per row, closed by `DW_LNE_end_sequence`.
 *
 * Emits the plain (non-special) opcodes deliberately: the special-opcode packing that shrinks
 * the program is an optimization with its own arithmetic to get wrong, and the section is
 * already tiny (95 bytes for two functions in the measured reference). Correct and verifiable
 * first.
 *
 * @param buf   the buffer to append to
 * @param f     the function whose rows to render
 * @return LineProgramOut  `buf` followed by the program, plus the byte offset of the
 *                         `DW_LNE_set_address` operand the object writer must relocate
 * @since debugger camada 1
 */
pub fn emit_line_program(buf: []byte, f: DwarfFunc): LineProgramOut
```

**Crumb D1.4 — o escoadouro ELF**

- **Mexe em:** `src/backend/objfile_elf.tks`. `ElfObject` ganha um `DwarfSections`;
  `elf_section_names` (hoje um array literal de sete nomes com um oitavo condicional) passa a
  incluir `.debug_abbrev`, `.debug_info`, `.debug_line`, `.rela.debug_info`, `.rela.debug_line`;
  `emit_elf_shdrs` e a contagem de cabeçalhos acompanham. As seções são `SHT_PROGBITS` **não
  alocadas** (sem `SHF_ALLOC`) — não entram na imagem de execução.
- **Prova:** (i) golden de bytes em `objfile_elf_test.tkt`, e — a proteção que importa — um
  golden que afirma que um objeto **sem** informação de depuração continua **byte-idêntico**
  ao de hoje; (ii) `scripts/check_elf.sh` estendido com `readelf --debug-dump=decodedline` e
  `--debug-dump=info` a analisar sem erro; (iii) **`scripts/check_debug_pos.sh` do D0.1, agora
  apontado a um binário nativo real** — o momento em que o arnês deixa de provar uma referência
  e passa a provar o produto.
- **Ponto ritual:** aqui o portão completo tem de passar (§8).

**Crumb D1.5 — o escoadouro Mach-O**

- **Mexe em:** `src/backend/objfile_macho.tks` — as mesmas três seções dentro do segmento
  `__DWARF`, com os nomes de seção do Mach-O (`__debug_info`, `__debug_abbrev`, `__debug_line`),
  `compute_macho_layout` e `macho_partition_relocs` acompanhando.
- **Prova:** golden em `objfile_macho_test.tkt` + `check_macho.sh` estendido; o arnês na perna
  macOS com lldb. **Já sei que os bytes servem ao lldb** — o Experimento D provou-o sobre o
  mesmo DWARF —, portanto o risco deste crumb é de *contentor*, não de *conteúdo*.

**Crumb D1.6 — a superfície do utilizador**

- **Mexe em:** o interruptor de perfil que liga a emissão (a informação de depuração é
  **opcional**, nunca imposta — ela cresce o objeto e não deve entrar num release por omissão);
  um `.vscode/launch.json` de exemplo em `docs/`; a documentação de utilização em
  `docs/BUILDING.md`.
- **Prova:** o arnês corre nas duas rotas; e uma prova de mesa registada em `docs/` do VSCode a
  parar num `.tks` via `cppdbg` — que **não** precisa de código nosso, e é isso que o crumb
  demonstra.

### Fase 2 e 3 — desenhadas, não orçadas para agora

**D2.1** — tabela de locais (`nome → vreg/slot`, `Type` do checker) capturada da `LEnv` durante
o lowering. A `LEnv` **já** é `struct { names: []str; vregs: []u32; …; is_slot: []bool;
slot_ltype: []LType }`, isto é, o mapa nome→localização **existe** — e é descartado.

**D2.2** — **o crumb perigoso, e a razão de a Camada 2 ser um penhasco.** O nome tem de
atravessar `regalloc` até uma localização final (deslocamento de frame ou registo físico). A
cadeia hoje é `LEnv(nome→vreg)` → `regalloc(vreg→registo ou slot)` → `compute_frame_layout(slot→
deslocamento de rsp)`. **Cada elo existe; nenhum carrega o nome.** E a `LEnv` é por-escopo e
transitória, enquanto DWARF quer intervalos de vida de variável. Este é o único crumb do
documento que eu **não** conseguiria orçar com confiança sem uma sondagem própria.

**D2.3** — tipos DWARF. Aqui há uma tensão arquitetural que vale nomear: **o LIR é apagado de
tipos por desenho** (`LType` é I64/Ptr/F64, tipo de máquina, não tipo Teko). DWARF quer o tipo
Teko. A resolução mais barata é uma **tabela lateral** construída no lowering — nunca
re-tipificar o LIR, que desfaria uma decisão de desenho deliberada por uma razão de
ferramentaria.

**D3.1** — pretty-printers Python do gdb para `str`, uniões e slices. **1 crumb, e é a resposta
certa** à pergunta do dono sobre matar a Camada 3 por muito menos dinheiro: sim, mata. Mas
**depende da Camada 2** — um pretty-printer imprime uma *variável*, e sem a Camada 2 não há
variável visível para imprimir (Experimento C: `No symbol "a" in current context`). E está
bloqueado por segunda razão em §9.

**D3-DAP** — **recomendo não fazer, e o custo de não fazer é zero.** O VSCode fala DAP, mas
`cppdbg` e CodeLLDB **já** são adaptadores DAP que conduzem gdb/lldb. Com DWARF padrão, o
VSCode funciona sem uma linha nossa. Um DAP próprio só se pagaria por valor que um
pretty-printer não dê — e o pretty-printer dá quase todo (`str` legível, união pelo membro
ativo). O que se perde sem DAP: inspeção de arena como uma árvore navegável, e comandos
próprios de compilador. Nada disso é depuração; é ferramentaria de luxo.

---

## 6. A Camada 0 vale? Uma resposta parcialmente contrária à intuição do dono

A intuição do dono foi: *"vale mesmo assim — por ser quase grátis e por dar a **forma** que a
camada 1 vai ter de replicar"*.

**Concordo com a primeira metade e discordo da segunda**, e a segunda é a que ele usou como
argumento principal.

**É quase grátis: confirmado.** O Experimento A funciona, `TFunction.file` já existe,
`emit_cov_line` é o molde exato, e a Camada 0 entrega **mais** do que a Camada 1 vai entregar
(inclui variáveis, de graça).

**Não dá a forma da Camada 1: medido.** `#line` é uma **directiva de texto** ancorada em
statements, delegando tudo ao `cc`. `.debug_line` é uma **máquina de estados de bytes** ancorada
em endereços de máquina. Não partilham codificação, ancoragem, nem sítio no pipeline. O
`emit_line_directive` do D0.2 não é reaproveitável em uma única linha pelo `dwarf.tks` do D1.3.
O que **é** reaproveitável a 100% é **o arnês de prova** — e é por isso que o parti em dois
crumbs, com o arnês (D0.1) autónomo e provável **sem** produto.

**E há um contra que o dono não pôs na balança:** a remoção da rota C não é um plano distante, é
uma fatia **pendente e já enumerada**. `docs/design/expurgo-do-c-e-a-busca-por-linker-0.3.1.md`
lista, na sua tabela de estado: *"6 | passo 1 do ruling: **REMOVER DO FONTE** a sondagem inteira
e **a emissão de C** | pendente"*. Investir no emissor de C é investir num ficheiro cuja
**deleção já está na fila**, e o D0.2 cai sobre dois ficheiros com agentes vivos.

**Recomendação:** fazer **D0.1** (o arnês) **sempre** — ele é da Camada 1 tanto quanto da 0, e
ter o arnês verde contra uma referência conhecida **antes** de escrever DWARF é o que permite
bissetar a Camada 1. Fazer **D0.2** apenas se a rota C ainda tiver semanas de vida útil, e
sabendo que **o código é descartável e o teste é o ativo**. Se a fatia 6 do expurgo estiver
para entrar, saltar D0.2 e ir direto ao D1.1 — não se perde nada além de algumas semanas de
conforto na rota que vai morrer.

---

## 7. O ponto de decisão do Windows/PDB — isolado, para o dono decidir

**A assimetria é real e o dono nomeou-a corretamente.** O `link.exe` da MSVC não consome DWARF.
Windows **não herda** o escritor DWARF que serve ELF e Mach-O, e portanto **nenhum** dos crumbs
D1.3–D1.5 rende um byte de valor no Windows.

**Um refinamento que reduz o custo, e que corrijo face ao enunciado do dono.** Ele escreveu *"a
MSVC não usa DWARF, usa **PDB**"* — verdade no resultado, mas o caminho não é escrever um PDB.
Um `.pdb` é um contentor MSF, complexo e mal servido por especificação pública. O caminho da
cadeia MSVC é: o compilador emite registos **CodeView** em seções `.debug$S`/`.debug$T` **dentro
do `.obj` COFF**, e é o `link.exe /DEBUG` que **constrói o PDB** a partir delas. Portanto o
trabalho do nosso lado é *um escritor de CodeView*, não *um escritor de PDB* — mais barato do
que o enunciado sugere, mas ainda assim **um segundo formato de informação de depuração, do
zero, sem reaproveitar nada da Camada 1**.

**A referência que o dono mandou espelhar diz para adiar — e verifiquei isso antes de a
invocar.** O dono pediu Zig como referência de controlo, com o argumento de que *"ele enfrentou
exatamente a assimetria PDB no Windows"*, e mandou-me verificar a referência antes de a citar
(citando o próprio escorregão dele com `-> void`). Fui ver: no ligador COFF auto-hospedado do
Zig (`src/link/Coff.zig`) **não há uma única menção a CodeView, PDB, `.debug$S`, `.debug$T` ou
DWARF**, e a flag `.DEBUG_STRIPPED` do cabeçalho COFF está fixada em `true`. **O Zig não
resolveu a assimetria — ele despacha Windows com a informação de depuração removida.**

Isto **inverte** o uso da referência, de forma útil: Zig não é precedente de *como resolver*
Windows, é precedente de **adiar Windows**, tomado por um projeto que emite o próprio DWARF para
os outros dois formatos e tem muito mais anos de backend nativo que nós.

**O que estou a espelhar do Zig, então, e é só isto:** (i) um **único** produtor de tabela de
linha alimentando **vários** escritores de objeto — que é a forma que `dwarf.tks` toma no D1.3;
(ii) DWARF próprio para ELF e Mach-O; (iii) **Windows explicitamente sem informação de
depuração** até que alguém o compre. As três coisas cabem na nossa superfície hoje.

**Decisão a tomar (isolada, do dono):**

| Opção | Custo | Consequência |
|---|---|---|
| **A — adiar** (recomendada) | zero | Windows constrói e corre; não se depura. O escritor CodeView entra na versão que trata o Windows, junto com o `link.exe`. Alinhado com a referência escolhida. |
| **B — entrar agora** | um segundo escritor completo, mais uma perna de CI num host Windows com MSVC | Windows depurável no Visual Studio e no VSCode; nenhum byte partilhado com a Camada 1 |

Recomendo **A**, e recomendo dizê-lo em voz alta na documentação em vez de deixar o utilizador
de Windows descobrir sozinho — recusa nomeada, não omissão, que é a regra que o próprio expurgo
já fixou para o MinGW (*"MinGW é RECUSA EXPLÍCITA, nomeando o motivo — não omissão"*).

---

## 8. Pontos rituais — onde o portão completo tem de passar

O portão é `gate = true` em `teko.tkp`, com os dez canais de regressão declarados em `[tests]
regression`. **Não crio canal novo**: `regressor.tkr` é explícito — *"IT BUILDS NOTHING, and
that is the whole point"*, e *"a variation in SOURCE needs a separate FILE inside one project;
only a variation in BUILD CONFIGURATION earns a separate build"*. Uma variação de informação de
depuração **é** de configuração de construção, o que compraria um build — e é exatamente por
isso que a prova principal vive em `.tkt` (bytes) e em `scripts/` (ferramenta externa), como já
acontece com ELF/Mach-O/COFF: golden de bytes no `.tkt`, verificação cruzada externa no
`scripts/check_*.sh`.

| Ponto ritual | Depois de | Porquê aqui |
|---|---|---|
| **R1** | D0.1 | o arnês tem de estar verde no positivo **e vermelho no negativo** antes de existir produtor. É o único momento em que a prova negativa é fácil de montar. |
| **R2** | D1.2 | é o crumb com maior superfície de colisão. O portão tem de confirmar `.text` **byte-idêntico** — a garantia de que a posição viaja sem mover código. |
| **R3** | D1.4 | primeira entrega de valor de ponta a ponta. `check_debug_pos.sh` sobre um binário nativo real. Se este ponto passar, a ordem do dono está cumprida no Linux. |
| **R4** | D1.5 | equalização de plataforma; a perna macOS com lldb. |
| **R5** | D1.6 | superfície e documentação; a prova de mesa no VSCode. |

---

## 9. Riscos, tensões de lei, e red-flags

**Não cunho KNOWN-STOP** — não é meu. Levanto o que merece atenção.

### RED-FLAG 1 — o `str` está em obra; qualquer pretty-printer escrito hoje nasce errado

O dono deu como restrição: *"`str` é `{ptr,len}` UTF-8, e o dono decidiu que `len` de `str` é
número de caracteres"*. Medi o estado real e ele **não** é esse:

- `src/runtime/teko_rt.h` define `tk_str` como **dois** words, e comenta o segundo como
  `// length in BYTES`.
- `docs/memory/raiz-comum-dos-degraus-0.3.1.0.md` registra a decisão do dono de 2026-07-29
  (*"`.len` conta CARACTERES"*) **e** que o `str` passa a levar **os dois contadores** —
  *"`str` leva os DOIS contadores — caracteres e bytes | é o terceiro word"*.

Ou seja: a lei que o dono citou está **decidida mas não aterrada**, e a aterragem **muda o
número de words do `str`**. Um pretty-printer escrito contra o layout de hoje erra **duas
vezes** quando ela aterrar: no tamanho da estrutura e no significado de `len`.

**Resolução law-first, sem tensão a arbitrar:** M.4 (construir sobre o que já está verificado)
resolve por sequenciamento — **D3.1 não começa antes de o `str` de três words aterrar**. Não é
uma tensão que precise do dono; é uma ordenação que a lei já determina. Registo-a porque a
restrição, como enunciada na ordem, descreve um layout que não é o do código, e alguém a
implementar poderia acreditar no enunciado.

E o corolário que responde diretamente à pergunta do dono: **os pretty-printers matam a Camada 3
por muito menos dinheiro, sim — mas não vêm antes da Camada 2**, e a Camada 2 é o penhasco. A
economia é real; a ordem não é a que a ordem sugere.

### RED-FLAG 2 — os nomes de locais não atravessam o alocador de registos

Detalhado em D2.2. É o único item que não sei orçar com honestidade sem sondar. Se a Camada 2
for aprovada, recomendo **um crumb de sondagem** antes de qualquer orçamento — e recomendo que
o dono não aceite número meu para a Camada 2 antes dessa sondagem.

### RED-FLAG 3 — o backtrace da Camada 1 apoia-se em heurística, não em CFI

O Experimento D obteve um `bt` correto de duas molduras **sem** nenhuma seção de frame,
porque o gdb analisa prólogos em x86-64. As nossas funções têm **duas** formas de frame
(`frame_is_framed_x86` distingue com e sem `rbp`), e uma função sem `rbp` em pilha profunda pode
derrotar a heurística. **Não é bloqueante para a Camada 1**, mas é a razão de eu ter posto
"profundidade de `bt` ≥ N" como afirmação **explícita** do arnês em D0.1: se o desenrolar
degradar, é o arnês que grita, e não o dono numa sessão real. `.eh_frame` mora na Camada 2, onde
já é necessário por outra razão.

### Riscos de execução, não de desenho

- **Colisão com os cinco agentes vivos.** D1.1 (`lower.tks`), D1.2 (`isel_x86_64`,
  `encode_x86_64`) e D0.2 (`codegen.tks`, `project.tks`) caem **todos** sobre ficheiros
  ocupados. Mitigação já embutida no desenho: **tudo aditivo** (membro de variante + braço de
  `match`), nada de assinatura alterada, e ancoragem em **nomes de função**, nunca em números de
  linha. D0.1 e D1.3 não colidem com nada e podem avançar já.
- **Semente de bootstrap.** Reli o corpus à procura de uma dependência de funcionalidade nova:
  não há. O desenho usa `struct`, `variant`, `enum`, `[]T`, `match`, e `teko::list::push` — tudo
  no que a semente já entende. Nada a sequenciar por causa da semente.
- `dwarf.tks` é chamado dos escritores ELF e Mach-O apenas.

---

## 10. Onde parar — a recomendação

**Parar na Camada 1 redefinida (D0.1 + D1.1…D1.6).** Seis crumbs, mais um opcional.

A razão é de razão valor/custo, e o Experimento C é o que a torna defensável em vez de opinião:
a fronteira entre "sei onde estou e como cheguei aqui" e "sei quanto vale `x`" é **exatamente** a
fronteira entre `-g1` e `-g2`, e é uma fronteira **desproporcionalmente** barata do lado de
baixo. Abaixo dela: três seções, quatro relocações de um tipo que já temos, um módulo novo
isolado, e um funil por arquitetura que já existe. Acima dela: nomes através do alocador de
registos, tipos re-introduzidos num IR deliberadamente apagado de tipos, intervalos de vida, e
CFI.

Depois da Camada 1 o dono põe um breakpoint num `.tks`, no terminal com gdb ou lldb, **e no
VSCode sem uma linha de servidor nosso** — o que é literalmente a ordem que ele deu, incluindo o
`pode ser gdb`.

**Adiado sem juros:** Camada 2 (precedida de sondagem — RED-FLAG 2), Camada 3 (bloqueada pelo
`str` — RED-FLAG 1), Windows/PDB via CodeView (decisão §7, recomendação: adiar), DAP próprio
(recomendação: nunca, salvo valor novo que um pretty-printer não dê).

**O que corrige o esboço do dono, em uma linha cada:**

1. A Camada 1 dele, `.debug_line` sozinho, **não funciona** — gdb não indexa uma tabela de linha
   sem CU (Experimento B). A fronteira desce: `.debug_info` + `.debug_abbrev` são pré-requisito
   do primeiro breakpoint.
2. Mas essa Camada 1 aumentada é **mais barata** que o esboço temia: sem `.debug_str`, sem
   `.debug_aranges`, e com **4 relocações, todas de um tipo que o backend já emite**
   (Experimento D). Nenhum kind novo de relocação, nos três formatos.
3. A medição 3 dele está certa e é **mais forte** do que ele disse (as posições são reais, não
   zeros) — mas **fura na seleção de instruções**: `MInst`/`MInstX86` não carregam posição, e
   `LFunc` não carrega ficheiro. O custo que ele orçou a zero é **um crumb**, não zero.
4. A Camada 0 é quase grátis (confirmado) mas **não** dá a forma da Camada 1 (contra a intuição
   dele); o que dá é o **arnês** — por isso o arnês vira crumb próprio, independente e primeiro.
5. Um único escritor DWARF serve gdb **e** lldb, bytes idênticos (Experimento D) — a economia de
   plataforma é maior do que o esboço supunha.
6. Windows: o trabalho é **CodeView dentro do COFF**, não escrever PDB — mais barato do que
   enunciado; e o Zig, a referência escolhida, **não** resolveu a assimetria (despacha COFF com
   `.DEBUG_STRIPPED = true`), logo serve de precedente para **adiar**, não para fazer.
7. Pretty-printers **matam** a Camada 3 por muito menos, como ele suspeitava — mas **não vêm
   antes da Camada 2**, e estão bloqueados pelo `str` em obra.
