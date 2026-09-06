# `tdb`, a superfície, e o DWARF como interop — desenho 0.3.1

> **Companheiro, não substituto.** `docs/design/debugger-orcamento-0.3.1.md` fica como está. Este
> documento acrescenta o que o dono cobrou (prova que se corre, superfície até ao texto, uso em cada
> debugger, camadas com número, contra-medida orçada), redesenha a fronteira por **honestidade**
> (ruling de hoje), e reorganiza tudo em volta da **decisão** de hoje: **o debugger próprio vai ser
> feito**. §11 aponta os erros factuais que encontrei no orçamento anterior — não o editei.
>
> **Prova executável:** `docs/design/debugger-poc/reproduce.sh` — corre e sai 0.

## Os quatro rulings do dono que este documento obedece

| # | Ruling | Consequência aqui |
|---|---|---|
| **R1** | *"assim como uma LSP, precisaremos de um debugger próprio, mas o escreveria em native e não agora em C"* | `tdb` **vai ser feito**, em Teko, depois da escada de degraus. A Peça 5 deixou de ser "orçar para decidir" e é **um plano** (§8). |
| **R2** | *"gastar energia marcando `#line` em C é desnecessário"* | **A Camada 0 do orçamento anterior DEIXA DE EXISTIR.** Não há D0.2. Não é opcional, não é "se a rota C tiver semanas de vida". Sai. |
| **R3** | *"não colocaria o código dentro do `src` do teko, começaria por um diretório `/tdb` … deveria ser um pacote de tooling … depois poderia migrar para um repo próprio"* | §11.2 — **`/tdb` na raiz**, a forma, e o acoplamento **por formato**. |
| **R3b** | *"quando digo tooling de pacote, e um `tkp` que emite um `tkl` de um executavel sob um novo tipo no `tkp` `kind=tool` … compilado na maquina do dev … mas sem adicionar como dependencia de projeto"* | **§11.5 — feature de manifesto ORÇADA em 5 crumbs**, com a referência C# verificada e **dois defeitos medidos que a bloqueiam**. |
| **R4** (anterior, em vigor) | *uma camada só clama o que garante* | §5 — cada clamação medida, e três red-flags resolvidas por medição. |

---

## 0. O eixo novo, em uma frase

> **Se `tdb` lê as NOSSAS tabelas, o DWARF deixa de ser pré-requisito e passa a ser INTEROP.**

Tudo o que segue é a consequência disso, com número. Os dez veredictos:

| # | Veredicto | Onde |
|---|---|---|
| 1 | **O `MLineMark` sobrevive intacto, e agora é a peça mais valiosa do desenho:** é o **único produtor** que serve **os dois** consumidores (`tdb` e DWARF). A razão que o justificava — o alocador reescreve o fluxo 1→N — é **independente do consumidor**. | §3.1 |
| 2 | **`tdb` mata a RED-FLAG 3 de vez, e não por a heurística funcionar:** nós **emitimos** o descritor de frame que já calculamos (`FrameLayoutX86`/`FrameLayout`). Verdade do compilador, não inferência do debugger. **~1 crumb, contra os 4 de CFI DWARF.** | §5.1 |
| 3 | **E a heurística do gdb também funciona — medido.** Cinco molduras, **todas** as formas de frame do nosso encoder x86-64, **zero CFI**: gdb **e** lldb recuperam a cadeia inteira. Logo a red-flag 3 morre **duas vezes**, por caminhos independentes. | §5.2 |
| 4 | **CFI DWARF: −4 crumbs. CodeView: −6 crumbs.** Ambos morrem. `tdb` não os precisa; o gdb, medido, não precisa de CFI; e Windows resolve-se com DWARF-em-PE (verificado no Go) para quem não é nosso. | §5.1, §7.4 |
| 5 | **O DWARF AINDA VALE, e vale AGORA — por três razões, e a terceira é a que não se dispensa:** (a) `tdb` é *"não agora"* e o dono quer depurar antes disso; (b) interop com quem não é nosso, para sempre; (c) **é o único leitor INDEPENDENTE da nossa tabela de linha** — sem ele, `tdb` a mentir e o compilador a mentir são indistinguíveis. | §6 |
| 6 | **`.tsym` é a semente, e a legislação já o disse.** `TEKO_LEGISLATION.md:350`: *"`.tsym` — Teko Symbols (debug symbols: file:line + names **for the debugger** + stack traces)"*. Estender `.tsym` é obedecer; inventar formato novo é abrir um segundo. E o cabeçalho **já leva versão** (`.tsym v1`). | §4 |
| 7 | **O `tdb` como projeto próprio e o `teko lsp` como subcomando NÃO se contradizem** — e a regra que unifica os dois é medível: *quem precisa do FRONT-END vive em `src/`; quem precisa só de um FORMATO vive fora.* O LSP precisa do checker; `tdb` precisa de um ficheiro. **Sem tensão para o dono.** | §11.1 |
| 7b | **`kind = "tool"` tem DOIS defeitos medidos a bloqueá-lo, e o segundo é pior que "não tratado":** um `kind` desconhecido é `Binary` **em silêncio** hoje (logo `kind = "tool"` já dá um verde falso), e `check_main_file_rule` **REJEITA activamente** um kind executável que não seja `Binary` — um `Tool` no enum sem tocar `tkp_rule.tks` faz toda ferramenta falhar, com uma mensagem que mente sobre porquê. | §11.5.2 |
| 7c | **`Tool` não é um kind do zero: é `Binary` ∘ `Package`** — as duas metades já estão escritas em `backend()`. E o `.tkl` de um `tool` **não leva binário pré-construído**: leva o `.tkb` + a declaração do comando, e a máquina do dev compila — que é o que o dono descreveu, sem tensão. | §11.5.2 |
| 8 | **A galinha e o ovo tem saída, e a ordem do dono não a fecha:** *"não escrever em C"* ≠ *"não compilar pela rota C"*. `teko build tooling/tdb` usa hoje `Backend::C` por omissão. | §10 |
| 9 | **VSCode + DAP não é grátis como parece:** DAP sozinho não basta — o VSCode exige uma extensão que **registe o tipo de debugger**. É 1 crumb e **zero JavaScript** (o que também esquiva a decisão de segurança já ratificada sobre `cp.exec`). | §8.5 |
| 10 | **O delve é o molde verificado:** `dlv dap` é **modo do próprio binário**, não adaptador separado. `tdb dap` copia isso. E o alcance do delve — 5 pares de plataforma, **sem `darwin/arm64`** — é o aviso de escopo que `tdb` tem de respeitar. | §8.6 |

---

# PEÇA 1 — A prova de conceito, que se corre

**Entrego DUAS provas, e a segunda é a que o eixo novo exigiu.** Cabem as duas, e são
complementares: uma prova o interop, a outra prova o caminho de `tdb`.

| prova | o que estabelece | serve |
|---|---|---|
| **P1 — o DWARF-4 mínimo escrito à mão** (`mini.s`) | três seções, **4** relocações `Abs64`, breakpoint por linha de `.tks`, `bt` de duas molduras com nomes Teko, aceito por gdb **e** lldb sem alteração | o **interop** (§6) e o **golden** do escritor DWARF |
| **P2 — cinco molduras através de TODAS as formas de frame, sem CFI** (`adv.s`) | que o desenrolar não depende de sorte, em nenhum dos dois caminhos | mata a **red-flag 3** (§5.2) e é o **fixture** do arnês |

**Refiz o Experimento D do documento anterior de ponta a ponta** e confirmo o número 4 — com a razão
que lá faltava (§11.1). Tudo em `docs/design/debugger-poc/`.

## 1.1 Os ficheiros

| Ficheiro | O que é |
|---|---|
| `hello.tks` | o `.tks` de referência: `main` chama `add`, duas molduras para o `bt`. **Nunca é compilado** — é a fonte que o DWARF escrito à mão APONTA, e essa isolação é o ponto: o debugger lista texto Teko enquanto passa por código de máquina que nenhum compilador Teko produziu. Prova-se o DWARF, não o compilador. |
| `mini.s` | o objeto mínimo: `.text` com `add`+`main`, e as três seções de depuração escritas **byte a byte com `.byte`** — não geradas por ferramenta nenhuma. É literalmente o golden. |
| `adv.s` | P2: cinco molduras através de **todas** as formas de frame do nosso encoder, sem CFI. |
| `reproduce.sh` | corre as duas provas e sai 0. Vive em `docs/`, não em `scripts/`: é documentação que executa, não portão. |

**Linhas do `.tks` que o resto do documento cita:** `add` declarada em **29**, primeiro statement em
**30**, retorno em **31**. `main` declarada em **40**, chamada em **41**, retorno em **42**.

## 1.2 `.debug_abbrev` — 37 bytes (0x25), anotado campo a campo

```
01              ULEB  código de abreviatura 1
11              ULEB  DW_TAG_compile_unit
01                    DW_CHILDREN_yes
25 08                 DW_AT_producer      DW_FORM_string
13 05                 DW_AT_language      DW_FORM_data2
03 08                 DW_AT_name          DW_FORM_string
1b 08                 DW_AT_comp_dir      DW_FORM_string
11 01                 DW_AT_low_pc        DW_FORM_addr
12 07                 DW_AT_high_pc       DW_FORM_data8
10 17                 DW_AT_stmt_list     DW_FORM_sec_offset
00 00                 fim da lista de atributos da abreviatura 1
02              ULEB  código de abreviatura 2
2e              ULEB  DW_TAG_subprogram
00                    DW_CHILDREN_no
03 08                 DW_AT_name          DW_FORM_string
3a 0d                 DW_AT_decl_file     DW_FORM_udata
3b 05                 DW_AT_decl_line     DW_FORM_data2
11 01                 DW_AT_low_pc        DW_FORM_addr
12 07                 DW_AT_high_pc       DW_FORM_data8
3f 19                 DW_AT_external      DW_FORM_flag_present
00 00                 fim da lista de atributos da abreviatura 2
00                    fim da tabela de abreviaturas
```

**Duas escolhas de forma que o documento anterior não fixou e que a produção precisa:**
`DW_AT_decl_line` é **`DW_FORM_data2`**, não `data1` — o próprio orçamento anterior deu como
exemplo um breakpoint em `main.tks:410`, que **não cabe** em `data1`. E `DW_AT_decl_file` é
**`DW_FORM_udata`** (ULEB) porque Teko emite **um objeto por programa** com funções vindas de
**muitos** `.tks`, e a tabela de ficheiros passa de 255 entradas num corpus do nosso tamanho.

## 1.3 `.debug_info` — 107 bytes (0x6b), anotado campo a campo

```
67 00 00 00                       unit_length = 0x67 (103) = tamanho da seção menos estes 4 bytes
04 00                             version = 4
00 00 00 00                       debug_abbrev_offset = 0  (UMA CU por objeto -> literal, sem reloc)
08                                address_size = 8
                                  --- DIE da unidade de compilação ---
01                                código de abreviatura 1
74 65 6b 6f 20 30 2e 33 2e 31 00  DW_AT_producer  = "teko 0.3.1\0"
0c 00                             DW_AT_language  = 0x000c (DW_LANG_C99)  [ver §1.5]
68 65 6c 6c 6f 2e 74 6b 73 00     DW_AT_name      = "hello.tks\0"
2e 00                             DW_AT_comp_dir  = ".\0"                 [ver §1.5]
00 00 00 00 00 00 00 00           DW_AT_low_pc    -> RELOCAÇÃO 1: Abs64 contra `add`
3b 00 00 00 00 00 00 00           DW_AT_high_pc   = 0x3b (59) — um COMPRIMENTO (data8), sem reloc
00 00 00 00                       DW_AT_stmt_list = 0
                                  --- DW_TAG_subprogram `add` ---
02                                código de abreviatura 2
61 64 64 00                       DW_AT_name      = "add\0"
01                                DW_AT_decl_file = 1
1d 00                             DW_AT_decl_line = 29
00 00 00 00 00 00 00 00           DW_AT_low_pc    -> RELOCAÇÃO 2: Abs64 contra `add`
18 00 00 00 00 00 00 00           DW_AT_high_pc   = 0x18 (24) comprimento
                                  --- DW_TAG_subprogram `main` ---
02                                código de abreviatura 2
6d 61 69 6e 00                    DW_AT_name      = "main\0"
01                                DW_AT_decl_file = 1
28 00                             DW_AT_decl_line = 40
00 00 00 00 00 00 00 00           DW_AT_low_pc    -> RELOCAÇÃO 3: Abs64 contra `main`
23 00 00 00 00 00 00 00           DW_AT_high_pc   = 0x23 (35) comprimento
00                                fim dos filhos do DIE da CU
```

## 1.4 `.debug_line` — 89 bytes (0x59), anotado campo a campo

```
55 00 00 00                       unit_length = 0x55 (85)
04 00                             version = 4
21 00 00 00                       header_length = 0x21 (33) — daqui até ao primeiro opcode
01                                minimum_instruction_length = 1
01                                maximum_operations_per_instruction = 1   (só DWARF 4)
01                                default_is_stmt = 1     <- é isto que faz cada linha ser breakpoint
fb                                line_base  = -5
0e                                line_range = 14
0d                                opcode_base = 13
00 01 01 01 01 00 00 00 01 00 00 01   standard_opcode_lengths[1..12]
00                                include_directories: terminador da lista vazia
68 65 6c 6c 6f 2e 74 6b 73 00     file_names[1].name            = "hello.tks\0"
00                                file_names[1].directory_index = 0
00                                file_names[1].mtime           = 0
00                                file_names[1].length          = 0
00                                file_names: terminador da lista
                                  --- o programa de números de linha ---
00 09 02                          DW_LNE_set_address (op 0 estendido, ULEB comprimento 9, sub-op 2)
00 00 00 00 00 00 00 00           o seu operando de 8 bytes -> RELOCAÇÃO 4: Abs64 contra `add`
                                  (a ÚNICA relocação desta seção — ver §11.1)
03 1c                             DW_LNS_advance_line, SLEB +28  -> linha 29
05 01                             DW_LNS_set_column,   ULEB 1
01                                DW_LNS_copy                    => LINHA (offset 0x00, linha 29)
02 0a                             DW_LNS_advance_pc,   ULEB +10
03 01                             DW_LNS_advance_line, SLEB +1   -> linha 30
01                                DW_LNS_copy                    => LINHA (offset 0x0a, linha 30)
02 09                             DW_LNS_advance_pc,   ULEB +9
03 01                             DW_LNS_advance_line, SLEB +1   -> linha 31
01                                DW_LNS_copy                    => LINHA (offset 0x13, linha 31)
02 05                             DW_LNS_advance_pc,   ULEB +5   (entra em `main`, em 0x18)
03 09                             DW_LNS_advance_line, SLEB +9   -> linha 40
01                                DW_LNS_copy                    => LINHA (offset 0x18, linha 40)
02 08                             DW_LNS_advance_pc,   ULEB +8
03 01                             DW_LNS_advance_line, SLEB +1   -> linha 41
01                                DW_LNS_copy                    => LINHA (offset 0x20, linha 41)
02 12                             DW_LNS_advance_pc,   ULEB +18
03 01                             DW_LNS_advance_line, SLEB +1   -> linha 42
01                                DW_LNS_copy                    => LINHA (offset 0x32, linha 42)
02 09                             DW_LNS_advance_pc,   ULEB +9   (até 0x3b, um byte além do fim)
00 01 01                          DW_LNE_end_sequence
```

**Estes três blocos são o golden do `dwarf_test.tkt`.** Não são uma leitura da especificação: são
os bytes de um objeto que gdb 15.1 **e** lldb 18.1.3 aceitaram sem alteração.

## 1.5 O comando de ponta a ponta, e a saída medida

```sh
cd docs/design/debugger-poc && ./reproduce.sh          # as duas provas, sai 0
```

O mínimo que o dono cola para ver o breakpoint parar:

```sh
cd docs/design/debugger-poc
as -o mini.o mini.s && cc -o mini mini.o
gdb -batch -nx -ex "break hello.tks:30" -ex run -ex bt ./mini
```

```
Breakpoint 1 at 0x1133: file hello.tks, line 30.
Breakpoint 1, add () at hello.tks:30
30	    let s = a + b
#0  add () at hello.tks:30
#1  0x0000555555555158 in main () at hello.tks:41
```

O **mesmo objeto**, sem uma alteração, no lldb 18.1.3:

```
$ lldb -b -o "breakpoint set --file hello.tks --line 30" -o run -o bt ./mini
Breakpoint 1: where = mini`add + 10 at hello.tks:30:1, address = 0x0000000000001133
    frame #0: 0x0000555555555133 mini`add at hello.tks:30:1
   29  	fn add(a: i32, b: i32): i32 {
-> 30  	    let s = a + b
   31  	    s
  * frame #0: 0x0000555555555133 mini`add at hello.tks:30:1
    frame #1: 0x0000555555555158 mini`main at hello.tks:41:1
```

Nos dois: `print a` / `frame variable` falham — `No symbol "a" in current context` /
`no variable information is available in debug info for this compile unit`. **É a fronteira, e é
exactamente esse texto que a superfície tem de prometer e nada mais.**

**Duas anotações honestas sobre o golden.** `DW_AT_comp_dir = "."` é assim de propósito, para que
**todo** byte de `mini.s` seja literal e o golden não dependa do host (medido: gdb e lldb resolvem
`./hello.tks` a partir do cwd). **A produção tem de emitir a raiz de projeto ABSOLUTA**, porque é o
que dispensa `sourceFileMap`/`sourceMap` no VSCode (§7.2, §7.3); um `comp_dir` errado dá o
meio-falho confuso — o breakpoint resolve e a **listagem** falha. E `DW_AT_language = DW_LANG_C99`
não é acidente: não existe código DWARF atribuído a Teko, e com C99 o gdb diz `source language c` e
**liga o seu analisador de expressões**, que é a pré-condição de `print x`. Um `lo_user` desligá-lo-ia.
Registar como decisão: **C99 é o código de transporte** até haver um atribuído.

---

# PEÇA NOVA (A) — O que `tdb` precisa que exista no compilador

Esta secção é o eixo novo. É curta de propósito: **a maior parte já existe.**

## 3.1 O `MLineMark` sobrevive, e é agora a peça central

O orçamento anterior escolheu, entre duas formas de levar a posição até aos bytes, uma
pseudo-instrução marcadora de zero bytes (`MLineMark`) em vez de envolver cada instrução. Reli a
razão e ela **não depende do consumidor**:

```teko
pub fn rewrite_inst(abi: AbiDescriptor, sr: ScanResult, inst: MInst, frame_base: u64): []MInst
```

**Uma instrução vira N.** O alocador expande derramamentos e recargas, logo um array paralelo a
`MBlock.insts` **dessincroniza no primeiro derramamento** e passa a mentir sobre posições em
silêncio. Isso é verdade para `tdb` e para DWARF igualmente.

**A conclusão que o eixo novo acrescenta:** `MLineMark` passa a ser **um produtor, dois consumidores**
— exactamente o padrão que o roadmap já manda seguir para a tabela de símbolos. **Não há duplicação
a temer, e a decisão do orçamento anterior fica confirmada, não revista.** Recomendo mantê-la
verbatim (o `MLineMark`/`LineRow` de lá, com o Javadoc que lá está).

## 3.2 As quatro tabelas, e o estado medido de cada uma

| tabela | `tdb` precisa? | DWARF precisa? | estado medido |
|---|---|---|---|
| **endereço → linha** | **sim, é o coração** | sim | **não existe** — vem do `MLineMark` (D1.2). `LInst` já carrega `line`/`col` com valores reais; `MInst`/`MInstX86` não carregam nada |
| **função → nome Teko, ficheiro, linha de declaração** | sim | sim | **JÁ EXISTE** — `codegen::tk_emit_tsym` emite `<símbolo-C>\t<nome-teko>\t<file>:<line>`, e `LFunc` ganha `file`/`decl_line` em D1.1 |
| **descritor de frame** (framed?, tamanho, regra de CFA) | **sim — e é o que mata a red-flag 3** | não (§5.2) | **JÁ É CALCULADO** — `FrameLayoutX86` / `FrameLayout` são exactos. Falta **serializá-lo** |
| **locais** (nome → slot → tipo) | sim, para `print x` | sim | **JÁ EXISTE em `LEnv`** (`names`/`vregs`/`is_scalar_slot`/`slot_ltype`) e é **descartado**. §5.3 |

**Duas linhas desta tabela dizem "já existe" e uma diz "já é calculado".** É isso que separa
"construir um debugger" de "construir um leitor sobre tabelas que já temos", e é o argumento
quantitativo de §8.7.

---

# PEÇA NOVA (B) — `.tsym` v2: a semente, e o que lhe falta

## 4.1 Porque é `.tsym` v2 e não um formato novo — law-first, com a lei citada

`TEKO_LEGISLATION.md:350`, literal:

> **`.tsym`** — *Teko Symbols* (debug symbols: file:line + names **for the debugger** + stack traces
> — Eixo E).

**A legislação já designou `.tsym` como o artefacto de informação de depuração "for the debugger".**
Inventar um `.tkd` seria abrir um segundo formato para um propósito já legislado. **Sem tensão para o
dono: a lei já decidiu.**

E medi que o formato **foi desenhado para evoluir**. `codegen.tks:12001`:

```
# teko symbol map (.tsym v1): <c-symbol>\t<teko-name>\t<file>:<line>
```

**A versão está no cabeçalho.** Um leitor v2 aceita v1 (degradando: sem linhas, sem frames, sem
locais) e um leitor v1 vê um cabeçalho que não reconhece e para honestamente. Nada a inventar.

## 4.2 O que v1 tem, o que lhe falta, e o custo

| | v1 hoje | v2 para `tdb` |
|---|---|---|
| por função: símbolo, nome Teko, ficheiro, linha de declaração | ✅ | mantém-se, **byte-idêntico** — v2 é aditivo |
| **endereço → linha** | ❌ | `L <sym> <offset-hex> <linha> <coluna>`, uma linha por `LineRow` |
| **descritor de frame** | ❌ | `F <sym> <framed 0\|1> <frame-size> <cfa-reg> <ra-offset>` |
| **locais** | ❌ | `V <sym> <nome> <slot-offset> <tipo> <linha-de-declaração>` |
| endereço/tamanho da função | ❌ | **não é preciso** — `tdb` resolve o símbolo pela tabela de símbolos ELF/Mach-O/COFF, que já emitimos |

**Texto, tab-separated, como v1.** Deliberado: legível, diffável, e o leitor não precisa de um
decodificador binário — o que poupa um crumb. Medi que **não há `teko::str::split`** no corpus
(`contains`, `ends_with`, `slice_to`, `slice_from`, `char_at`, `chars` existem), logo o leitor
precisa de um separador feito à mão; o precedente é o parser TOML de `manifest.tks`, que faz
exactamente isso. **Custo: 1 crumb no leitor, dentro do `tdb`, não no compilador.**

**Ordem de grandeza, para não haver surpresa:** uma linha `L` por statement por função. No corpus do
compilador, ~50 k linhas × ~24 bytes ≈ **1,2 MB** de `.tsym`. É opt-in (`--debug=lines`), fica fora
do binário, e é um ficheiro de texto. Aceitável, e vale dizê-lo antes de alguém o descobrir.

## 4.3 O achado que se reporta para cima (não é issue minha)

Depois de v2, o **stack trace nativo de produção** hoje servido por `.tsym` v1 (por-função) pode
passar a resolver **linha exacta** — a mesma informação, melhor granularidade, mesmo ficheiro. É o
padrão que o Zig segue (`lib/std/debug.zig` → `SelfInfo` → `Dwarf.zig`/`Pdb.zig`, §12.3). **Não orço
isto aqui** e não abro issue: **REPORTO**, porque muda o valor do arco — o `MLineMark` não paga só
depuração interactiva, paga também a qualidade dos stack traces de produção.

---

# PEÇA 6 — A fronteira redesenhada por HONESTIDADE

**Regra em vigor (R4):** *uma camada só pode CLAMAR o que consegue GARANTIR; se a garantia depende
de outra camada, ou a garantia sai da lista, ou a dependência entra na camada.* O dono está certo
sobre a causa: as camadas antigas foram cortadas por custo. Abaixo, cada red-flag **medida**.

## 5.1 RED-FLAG 3 — morta pelo caminho de `tdb`: nós temos a verdade, o gdb tem inferência

A afirmação era: *"as nossas funções têm duas formas de frame e uma função sem `rbp` em pilha
profunda pode derrotar a heurística de análise de prólogo"*.

**Com `tdb`, a heurística deixa de existir.** Nós somos o compilador: `compute_frame_layout` produz
`FrameLayoutX86 { size, saved_gpr, saved_fpr, slot_offsets, call_align, … }` — **exacto, não
inferido**. Serializá-lo é uma linha `F` por função em `.tsym` v2 (§4.2), e `tdb` desenrola com
verdade do produtor.

| | CFI DWARF (F1…F4 de §5.4) | linha `F` do `.tsym` v2 |
|---|---|---|
| custo | **4 crumbs** (CIE/FDE + programa de CFA × 2 arquiteturas + routing em 2 escritores) | **~1 crumb** (uma linha de texto por função) |
| relocações | **+1 `Abs64` por função** (cada FDE tem `initial_location`) | **zero** — o `tdb` resolve o símbolo pela tabela de símbolos |
| correção | codifica a mesma verdade, com uma máquina de estados pelo meio | a verdade, sem codificação |

**Veredicto: a CFI DWARF morre. −4 crumbs.** E `bt` deixa de ser uma clamação frágil: passa a ser a
clamação **mais forte** do arco, porque é a única que não depende de inferência de ninguém.

## 5.2 E a heurística do gdb TAMBÉM funciona — medido, o que mata a red-flag 3 uma segunda vez

Isto importa porque o DWARF é interop (§6) e o interop tem de ser honesto sem CFI.

**Primeiro, o argumento estrutural.** `encode_x86_64.tks:1532`:

```teko
fn frame_is_framed_x86(layout: FrameLayoutX86): bool {
    let n_saved = (layout.saved_gpr.len + layout.saved_fpr.len) to u32
    (layout.size > (0 to u32)) || (n_saved > (0 to u32)) || layout.call_align
}
```

com `needs_call_align_x86(f) = func_makes_call_x86(f) && func_any_ret_x86(f)`. Logo: há slots ⇒
framed; há callee-saved ⇒ framed; **faz uma chamada e retorna ⇒ framed**. E uma função frameless
emite prólogo e epílogo **vazios**. **O invariante: RSP só se move dentro de uma função que já pôs
`rbp`. Uma função frameless devolve RSP exactamente como o recebeu** — o caso mais fácil para um
analisador de prólogo, não o mais difícil.

**Segundo, a medição.** `adv.s` constrói cinco molduras através de **todas** as formas, **incluindo o
único furo teórico** (a função frameless que *chama* e nunca retorna, `func_any_ret_x86` falso), e
confirma que o objeto **não tem `.eh_frame`**:

| moldura | forma | bytes |
|---|---|---|
| `#0 lvl4` | **frameless leaf** — zero bytes de prólogo | `mov $7,%eax; ret` |
| `#1 lvl3` | **framed com callee-saved DEPOIS de `mov rbp,rsp`, e `sub rsp`** — a nossa ordem exacta | `push %rbp; mov %rsp,%rbp; push %rbx; push %r12; sub $0x20,%rsp` |
| `#2 lvl2` | **frameless CALLER** — chama e nunca retorna, o furo | `call lvl3; …; hlt` |
| `#3 lvl1` | **framed só por alinhamento** (`size` 0, zero callee-saved) | `push %rbp; mov %rsp,%rbp; sub $0,%rsp` |
| `#4 main` | framed | — |

gdb 15.1, sem uma linha de CFI:

```
Breakpoint 1, lvl4 () at hello.tks:30
#0  lvl4 () at hello.tks:30
#1  0x000055555555514f in lvl3 () at hello.tks:42
#2  0x0000555555555163 in lvl2 () at hello.tks:20
#3  0x0000555555555181 in lvl1 () at hello.tks:17
#4  0x000055555555518c in main () at hello.tks:41
```

lldb 18.1.3, o mesmo objeto, as mesmas cinco molduras. E os **dois pontos de fronteira de prólogo**,
que são o modo de falha real de um analisador: breakpoint no **primeiro byte** de `lvl3`, **antes**
do `push %rbp` → 4 molduras corretas; breakpoint **entre** o `push %rbp` e o `mov %rsp,%rbp`
(`lvl3+1`) → 4 molduras corretas.

**Veredicto: o `bt` fica na lista de entregas do interop DWARF, com prova e fixture.** A red-flag 3
estava errada, e o que a tornava plausível era não ter sido medida.

**O que fica por medir, e é honesto dizê-lo: arm64.** Não há `aarch64-linux-gnu-as` nem
`qemu-aarch64` neste host. O invariante, lido em `encode_arm64.tks:1514`, é
`sub sp,sp,#size; stp x29,x30,[sp,#size-16]; add x29,sp,#size-16` — `x29` aponta **para** o par
salvo, logo `[x29]` = `x29` do chamador e `[x29+8]` = endereço de retorno: **o invariante de cadeia
do AAPCS64**, mesmo com o par no topo do frame em vez da base. **Isso é um argumento, e um argumento
não é uma medição** → crumb **D1.7** (§7.6), com dois ramos orçados. **Note-se que `tdb` não depende
deste ramo**: a linha `F` do `.tsym` v2 dá a verdade em qualquer arquitetura.

## 5.3 RED-FLAG 2 — redimensionada, e o perigo verdadeiro é outro

A red-flag dizia que a cadeia `LEnv(nome→vreg)` → `regalloc(vreg→registo|slot)` →
`compute_frame_layout(slot→offset)` existe elo por elo mas nenhum elo carrega o nome. Verdade — e
**irrelevante**, porque medi a chave:

```teko
pub fn assign_lookup(sr: ScanResult, vreg_id: u32): AssignLookup   // regalloc.tks:1475
pub type InReg   = struct { vreg_id: u32; phys: u32 }
pub type Spilled = struct { vreg_id: u32; slot: u64 }
```

`ScanResult` é **indexado por `vreg_id`** e `assign_lookup` é **pública**. O nome não tem de *viajar*
por elo nenhum: uma tabela lateral `(nome, vreg_id)` capturada no lowering **junta-se** ao
`ScanResult` depois do scan, pela chave que já existe. **É um JOIN, não plumbing** — e nenhum
`match inst` do backend é tocado.

**Mas há um perigo que a red-flag não nomeou e que é pior.** Uma alocação `InReg` só é válida
**dentro do intervalo de vida do vreg**; fora dele o registo já tem outra coisa. Um `DW_AT_location`
(ou uma linha `V`) que dissesse "`x` está em `rbx`" faria `print x` imprimir, **em silêncio e com
confiança**, o valor de outra variável. Isso não é camada em falta — **é camada que ensina errado**,
e é exactamente o defeito que o dono rejeitou.

**A resolução honesta já está no código.** `src/lir/lower.tks:113`:

```teko
pub fn lenv_bind_scalar_slot(env: LEnv, name: str, slot: u32, ty: LType): LEnv
```

Um local nomeado **pode já ser ligado a um slot de frame** com o seu `LType`. Sob o perfil de
depuração de variáveis, **todo local nomeado é fixado a um slot**, e a localização passa a ser um
único offset válido em **todo** o escopo. É literalmente o que `cc -O0` faz. **A Camada de variáveis
honesta é mais barata que a rápida, e mais correta.**

## 5.4 RED-FLAG 1 (`str`) — confinada, e a regra que a torna inofensiva

Estado medido: `src/runtime/teko_rt.h:44-48` define `tk_str` como **duas** words, comentário
`length in BYTES`. `docs/memory/raiz-comum-dos-degraus-0.3.1.0.md:29` registra a decisão do dono de
que `.len` conta **CARACTERES** e que `str` leva **os dois contadores** — terceira word.
**Decidido, não aterrado.**

1. **Afeta a listagem de fonte?** **NÃO.** Medido em §1.5: o lldb imprimiu as linhas 27–33 de
   `hello.tks` num processo onde não existe um `str` Teko. A listagem é o debugger a ler o ficheiro.
2. **Afeta a tabela de linha?** **NÃO.** É `(offset, linha, coluna)` — inteiros.
3. **Afeta as variáveis?** **SIM, e só nos locais de tipo `str`.** Um tipo de duas words escrito hoje
   erra **duas vezes** quando a terceira word aterrar: no tamanho e no significado de `len`.

**A regra que resolve, generalizável em vez de remendo:**

> **Só se descreve tipo cujo layout esteja CONGELADO.** Um local cujo tipo não está congelado **não
> recebe descrição nenhuma** — o debugger diz `No symbol "s" in current context`, que é honesto, em
> vez de mostrar lixo com confiança.

Com essa regra, **`str` deixa de bloquear as variáveis**: bloqueia os locais de tipo `str`, e o
resto avança. Só a legibilidade (pretty-printer / formatador de `tdb`) fica bloqueada, porque
formatar um `str` **é** o layout de `str`. **Isto corrige o orçamento anterior**, que fazia `str`
parecer pré-condição de mais do que é.

## 5.5 A fronteira nova, camada por camada

| Camada | Clama | Garante? | Veredicto |
|---|---|---|---|
| **Piso** | breakpoint por linha `.tks`, listagem, `step`/`next` | **sim** — §1.5 | fica |
| **Piso** | `bt` com nomes Teko, x86-64, via gdb/lldb | **sim** — §5.2, 5 molduras, todas as formas, zero CFI | **fica, com prova** |
| **Piso** | `bt`, arm64, via gdb/lldb | **por medir** | condicionado ao **D1.7**; até lá a doc diz "medido em x86-64" |
| **Piso** | `bt` via **`tdb`**, qualquer arquitetura | **sim** — a linha `F` é verdade do compilador | fica, e é a clamação mais forte |
| **Piso** | `print x` | **não** | **sai da lista, e a saída é NOMEADA** no `--help`, nos cinco `launch.json`, e em §7.7 |
| **Variáveis** | `print x`, membros de struct | sim **se** locais fixados a slot | a fixação **entra na camada**; não é opcional |
| **Variáveis** | tipos | sim **se** só layout congelado | a regra do congelamento **entra na camada**; `str` fica fora até aterrar |
| **Legibilidade** | `str` legível, união pelo membro ativo | **não hoje** | bloqueada por `str`; e §11.6 — a via do `variant_part` **não se aplica** |
| ~~**Camada 0** (`#line` em C)~~ | — | — | **ELIMINADA por R2.** Não existe neste desenho. |

---

# 6. O DWARF ainda vale? Sim — e a terceira razão é a que não se dispensa

Pergunta honesta do brief: se `tdb` fala DAP, o VSCode está servido sem `cppdbg`; se `tdb` corre no
terminal, o gdb está servido sem DWARF. **Então para quem é o DWARF?**

| razão | força |
|---|---|
| **(a) `tdb` é "não agora".** O dono decidiu o `tdb` **e** decidiu que ele vem depois da escada de degraus. Entre hoje e esse dia, o DWARF é a **única** forma de depurar. | **forte, e temporária** |
| **(b) Interop com quem não é nosso, para sempre.** gdb, lldb, `cppdbg`, CodeLLDB, e — medido, §12.1 — o alcance de um debugger próprio maduro é **5 pares de plataforma** (o delve não cobre `darwin/arm64`). Onde `tdb` não chegar, o DWARF chega. E há um caso que `tdb` não cobre sem custo próprio: **um programa Teko que chama uma biblioteca C** — o gdb entra no C com o DWARF do C; `tdb` mostraria endereços crus. | **forte, e permanente** |
| **(c) É o único leitor INDEPENDENTE da nossa tabela de linha.** `tdb` lê `.tsym` v2, que sai do `MLineMark`. Se o `MLineMark` estiver errado, **`tdb` reporta o erro fielmente** e nada o contradiz. Com DWARF, gdb e lldb são leitores independentes da **mesma** origem: uma discordância entre gdb e `tdb` sobre o mesmo binário é um sinal de defeito que **nenhum dos dois produz sozinho**. | **é a que decide** |

**Veredicto: o Piso de DWARF (as três seções) vale, e vale AGORA. O que NÃO vale mais é o que `tdb`
torna redundante:**

| item | veredicto novo | poupança |
|---|---|---|
| `.debug_line` + `.debug_info` + `.debug_abbrev` | **compra-se** | — |
| **CFI DWARF** (`.debug_frame`) | **NÃO se compra** — §5.1 (o `tdb` tem a verdade) + §5.2 (o gdb, medido, não precisa) | **−4 crumbs** |
| **CodeView** (`.debug$S`/`.debug$T`) | **NÃO se compra** — §7.4 | **−6 crumbs** (no ramo mau) |
| `#line` na rota C | **NÃO se compra** — R2 | −1 crumb, e a energia |
| tipos DWARF (variáveis) | compra-se **se** o dono quiser `print x` no gdb; se `tdb` bastar, `.tsym` v2 basta e é mais barato | decisão do dono, §7.5 |

---

# PEÇA 2 — A superfície: `teko build`, e a superfície do `tdb`

## 7.1 O que a CLI já aceita — medido, não suposto

| forma | flags que a usam |
|---|---|
| booleana longa | `--coverage`, `--no-verify`, `--cov-validation`, `--no-tty`, `--allow-undef`, `--release`, `--analyzer`, `--arith-cast-gate` |
| valor separado | `-o <dir>`, `--per-commit-test <base>` |
| **nivelada com `=`** | **`--opt=<0\|1\|2>`** (`opt_arg_has_prefix`, prefixo de 6 bytes + `opt_level_of_value`) |
| **rejeição honesta** | `--backend` / `--backend=…` — retirada, e **nomeada** em vez de ignorada (`is_backend_flag`, `has_backend_flag`) |

**A armadilha silenciosa que decide o desenho:** `project_arg_of` devolve o **primeiro positional que
não reconhece como flag**. Uma flag nova que não entre na sua lista de saltos passa a ser lida como
**o caminho do projeto** — sem erro de flag desconhecida. **Todo crumb que acrescente flag TEM de
tocar `project_arg_of`, e o teste que o prova é obrigatório.** (RED-FLAG 5, §13.)

**E `-g` já é falado internamente:** `build_cc_argv(…, debug: bool, opt: i32)` acrescenta `-g` quando
`debug`, e `run_cc_debug`/`build_debug_binary` (o caminho de `teko run`) passam `true`. **O perfil de
depuração da rota C existe e está ligado a `teko run`** — só não tem superfície em `teko build`.
Registo-o como medição; **não o orço**, porque R2 mata a rota C como investimento.

## 7.2 A flag — `--debug=lines`, e `-g` REJEITADO com mensagem

**A referência de superfície é Rust**, verificada antes de invocada
(`doc.rust-lang.org/rustc/codegen-options/index.html`, literal):

> * `0` ou `none`: no debug info at all (the default).
> * `line-tables-only`: line tables only. **Generates the minimal amount of debug info for
>   backtraces with filename/line number info, but not anything else, i.e. no variable or function
>   parameter info.**
> * `1` ou `limited`: debug info without type or variable-level information.
> * `2` ou `full`: full debug info.
> * Note: The `-g` flag is an alias for `-C debuginfo=2`.

**É o achado de superfície do documento.** A fronteira que o Experimento C mediu — breakpoint e `bt`
sim, variáveis não — **não é invenção nossa: é um nível de primeira classe, documentado e nomeado,
da referência que o dono atribuiu.** O nosso Piso **é** `line-tables-only`. E a mesma citação diz
que **`-g` significa `full`**, como em gcc e clang.

| forma | efeito |
|---|---|
| **`--debug=lines`** | emite as três seções DWARF **e** o `.tsym` v2 (as duas saídas do mesmo `MLineMark`). Nome do vocabulário verificado do Rust. |
| **`--debug=none`** | **O DEFAULT**, incluindo sob `--release`. Grafia explícita aceita para scripts. |
| **`-g`** | **REJEITADO, com mensagem que diz o que escrever.** Precedente exacto no mesmo ficheiro: `has_backend_flag`. Uma flag que prometesse `full` e entregasse `lines` é o defeito que o dono acabou de rejeitar. |
| valor desconhecido | **ERRO DURO**, nunca degradação silenciosa (M.3) — o **oposto** de `opt_level_of_value`, que resolve o malformado para 2. Ali um palpite errado custa velocidade; aqui custa uma sessão inteira a perseguir um breakpoint que nunca resolveu. |

Quando as variáveis aterrarem, acrescenta-se **`--debug=vars`** e **só então** `-g` passa a ser
alias legal. **Não há duas formas da mesma operação:** `--debug=` é a única, com valores nomeados.

## 7.3 Nada no `teko.tkp` — medido, com precedente exacto

Não há precedente de perfil de construção no manifesto: `Manifest` tem `[artifact]`, `[platforms]`,
`[aliases]`, `[coverage]`, `[tests]`, `[extern]`, `[extern.libs.*]` — e **otimização, o eixo mais
próximo, é exclusivamente de CLI**. O doc-comment de `build_cc_argv` legisla a razão:
*"Optimization is an AXIS of the build, not a global"*. Informação de depuração é da mesma classe:
dois `teko build` da mesma árvore têm de poder diferir nela sem editar um ficheiro versionado.
**Nenhuma chave nova no `.tkp`.**

## 7.4 Onde vivem os bytes — e Windows deixa de precisar de CodeView

**No objeto/binário, e no `.tsym` ao lado.** Medido: o `.tkl` é um ZIP-STORE com **exactamente três**
entradas (`<name>.tkh`, `<name>.tkb`, `<name>.tsym`) e **nenhum objeto** — não há lá nada a que
agarrar DWARF. **O `.tkl` fica intocado por este desenho**, o que é boa notícia de reversibilidade;
e o `.tsym` v2 entra por **a porta que o v1 já usa**, em ambos os sítios (solto ao lado do binário e
dentro do `.tkl`).

**Windows, e é aqui que o item mais caro do orçamento anterior morre — duas vezes:**

1. **Para `tdb`: o contentor é irrelevante.** `tdb` lê `.tsym` v2, um ficheiro de texto ao lado do
   `.exe`. **PE, COFF, CodeView e PDB não entram na conversa.** O item de §7 do orçamento anterior —
   *"um segundo formato de informação de depuração, do zero, sem reaproveitar nada"* — **deixa de
   existir para o nosso debugger.**
2. **Para gdb/lldb em Windows: é o mesmo DWARF, noutro contentor** — verificado no
   `cmd/link/internal/ld/pe.go` do Go, que põe DWARF dentro do PE: `pefile.addDWARF()`; *"DWARF
   section names are longer than 8 characters. PE format requires such names to be stored in string
   table"* → *"section names replaced with slash (/) followed by correspondent string table index"*;
   `h.characteristics = IMAGE_SCN_ALIGN_1BYTES | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_DISCARDABLE |
   IMAGE_SCN_CNT_INITIALIZED_DATA`; **nenhum CodeView, nenhum PDB.**

Medi o nosso lado: `objfile_coff.tks` **tem** máquina de string table (`CoffStrtab`,
`build_coff_strtab`, prefixo de 4 bytes) **mas só para nomes de SÍMBOLO**; nomes de **seção** usam
`emit_coff_name8_str` — pad cru a 8 bytes, **sem** a forma `/N`. É exactamente o mecanismo que o Go
teve de implementar. **Orçado em §7.6 (W0…W2, 3 crumbs, um deles sondagem).** CodeView só seria
preciso para **WinDbg e Visual Studio**, e não é preciso para nada que o dono pediu.

## 7.5 A regra escrita sobre `--release`

1. **A informação de depuração nunca é imposta.** Default `--debug=none` em **todos** os comandos e
   perfis, `--release` incluído.
2. **`--release --debug=lines` é LEGAL e não é recusado.** É como se depura um crash de release, e
   recusá-lo obrigaria a reconstruir com outro perfil — que muda o código e apaga o defeito. Os eixos
   são ortogonais **por medição**: na rota nativa não há otimizador, logo nenhuma reordenação faz a
   tabela mentir.
3. **`teko run` mantém o seu perfil de depuração** e **não** passa a exigir a flag.

## 7.6 O texto exacto do `--help`, e as formas em Teko

Em `print_help_general` (após `--release`) e em `print_help_build`, verbatim:

```
       --debug=lines          emit debug info: breakpoint by .tks line, step, and backtrace in
                              tdb, gdb, lldb and VSCode. Does NOT include variables
       --debug=none           no debug information (the default, including under --release)
```

E a rejeição, em `main.tks`, **antes** de `project_arg_of` correr (o sítio onde `--backend` é
rejeitado hoje):

```
teko: `-g` is not a Teko flag.
      In gcc, clang and rustc `-g` means FULL debug information (types and variables), and this
      compiler cannot emit that yet — promising it would be a lie.
      Use `--debug=lines` for the line table (breakpoint by .tks line, step, backtrace).
```

`void` é banido (nenhuma destas devolve nada sem valor) e não há sobrecarga (cada operação, um nome).

```teko
/**
 * DebugInfo — how much debug information a build emits, the `--debug=<value>` axis.
 *
 * An ENUM and not a bool because the axis is LEVELLED at the reference this surface mirrors:
 * rustc documents `line-tables-only` ("the minimal amount of debug info for backtraces with
 * filename/line number info, but not anything else") as a first-class level distinct from
 * `full`. `Lines` IS that level. When variables land, a `Vars` member joins here and no existing
 * spelling changes meaning — which is the whole reason this is not a bool.
 *
 * One level drives TWO outputs from ONE origin: the DWARF sections (for gdb/lldb/VSCode) and the
 * `.tsym` v2 map (for `tdb`). They are consumers, never rival formats.
 *
 * @since 0.3.1 debugger piso
 * @see opt_level_of  the sibling build AXIS, likewise CLI-only and absent from `teko.tkp`
 */
pub type DebugInfo = enum { None; Lines }

/**
 * debug_arg_has_prefix — does `a` begin with the `--debug=` selector prefix, so its remainder is
 * the requested level?
 *
 * An 8-byte prefix, guarded on length so the slice never runs past the string — deliberately the
 * exact shape of `opt_arg_has_prefix`'s 6-byte guard: two selector flags that parse differently
 * is how one of them ends up wrong.
 *
 * @param a  a single CLI argument token
 * @return   true iff `a` is a `--debug=<value>` form
 */
fn debug_arg_has_prefix(a: str): bool {
    a.len > 8 && teko::str::slice_to(a, 8) == "--debug="
}

/**
 * debug_info_of_value — map the text after `--debug=` to a level, or FAIL.
 *
 * DELIBERATELY UNLIKE `opt_level_of_value`, which resolves a malformed suffix to level 2. An
 * unrecognised optimization level costs speed; an unrecognised DEBUG level costs a whole
 * debugging session spent chasing a breakpoint that silently never resolved, which is the
 * "teaches wrong" failure this surface exists to avoid. M.3: no silent coercion.
 *
 * @param v  the substring after `--debug=`
 * @return   the level for "none"/"lines"
 * @throws   when `v` is neither "none" nor "lines", naming both accepted values
 */
fn debug_info_of_value(v: str): DebugInfo | error {
    if v == "none" { return DebugInfo::None }
    if v == "lines" { return DebugInfo::Lines }
    error { message = teko::str::concat("teko: --debug=", teko::str::concat(v, ": unknown level — accepted: none, lines")) }
}

/**
 * debug_info_of — resolve the requested debug level from the build flags.
 *
 * With no `--debug=` flag the level is `None` — THE DEFAULT AT EVERY PROFILE, `--release`
 * included: debug information grows the artifacts and is never imposed. The LAST matching flag on
 * the line wins, matching `opt_level_of`'s rule so the two axes behave the same under repetition.
 *
 * `--release --debug=lines` is a LEGAL, SUPPORTED pair and is not refused: it is how a release
 * crash gets debugged, and refusing it would force a rebuild at another profile — which changes
 * the code and erases the defect.
 *
 * @param args  the full CLI argument vector
 * @return      the resolved level (`None` by default)
 * @throws      when a `--debug=` value is not a recognised level
 */
fn debug_info_of(args: []str): DebugInfo | error {
    mut i: u64 = 0
    mut level = DebugInfo::None
    loop {
        if i >= args.len { break }
        if debug_arg_has_prefix(args[i]) {
            level = match debug_info_of_value(teko::str::slice_from(args[i], 8)) {
                DebugInfo as d => d
                error as e => return e
            }
        }
        i = i + 1
    }
    level
}

/**
 * has_bare_g_flag — does a bare `-g` appear anywhere in `args` from `start`?
 *
 * Consulted at CLI dispatch BEFORE `project_arg_of` runs, so a `-g` gets the honest rejection that
 * names `--debug=lines` instead of being read as the project path. `-g` is refused rather than
 * aliased because in gcc, clang and rustc it means FULL debug information (rustc documents it as
 * an alias for `-C debuginfo=2`), and this compiler cannot emit that yet — an alias would promise
 * types and variables and deliver a line table.
 *
 * The precedent is exact and lives in this same file: `has_backend_flag` refuses the retired
 * `--backend` by NAMING the reason rather than silently ignoring it.
 *
 * @param args   the full CLI argument vector
 * @param start  the index to begin scanning from (2 for a subcommand, 1 for a bare project)
 * @return       true iff a bare `-g` token appears anywhere from `start` onward
 */
fn has_bare_g_flag(args: []str, start: u64): bool {
    mut i = start
    loop {
        if i >= args.len { break }
        if args[i] == "-g" { return true }
        i = i + 1
    }
    false
}
```

**E a linha que não pode faltar**, em `project_arg_of`, sob pena da armadilha de §7.1:

```teko
        else if debug_arg_has_prefix(args[i]) { i = i + 1 }
```

## 7.7 A superfície do `tdb` — o que o dono escreve

**O molde é o delve, verificado** (`Documentation/usage/dlv.md`): `debug` ("Compile and begin
debugging main package"), `exec` ("Execute a precompiled binary, and begin a debug session"),
`attach`, `test`, `dap` ("Starts a headless TCP server communicating via Debug Adaptor Protocol"),
`connect`, `trace`, `core`, `version`. E a decisão de forma que copio: **`dlv dap` é um MODO DO
PRÓPRIO BINÁRIO, não um adaptador separado** (§12.4).

**Subcomandos do `tdb`, e o que deixo deliberadamente de fora:**

| subcomando | efeito | fase |
|---|---|---|
| `tdb run <projdir> [-- args…]` | `teko build --debug=lines` + spawn traçado + sessão interativa. O irmão de `dlv debug`. | T3 |
| `tdb exec <binário> [-- args…]` | spawn traçado de um binário já construído. **O primeiro a existir**, porque não depende do builder. | T1 |
| `tdb attach <pid>` | anexa a um processo vivo | T2 |
| `tdb dap [--port <n>]` | servidor DAP; sem `--port`, stdio | T4 |
| `tdb version` | versão | T1 |
| ~~`core`~~ | **fora, e nomeado:** ler core dumps é um leitor de ELF-core próprio. Onde é preciso, **o gdb com DWARF serve** — é o caso (b) de §6 a pagar-se. |
| ~~`trace`~~, ~~`test`~~, ~~`connect`~~ | fora do arco inicial |

**Comandos interativos.** Escolho a grafia do **gdb**, não a do lldb, e a razão é de lei —
comportamentos → Go: o delve escolheu a grafia do gdb pelo mesmo motivo (memória muscular de quem
depura). Aliases de CLI **não são sobrecarga de função**: são um nome com abreviatura, e é assim que
todo debugger se escreve.

```
break <file.tks>:<line>      b     põe breakpoint
delete <n>                   d     remove
run                          r     arranca
continue                     c     continua
next                         n     próxima linha, sem entrar
step                         s     próxima linha, entrando
finish                             corre até ao retorno da moldura corrente
bt                                 pilha, com nomes Teko
frame <n>                    f     selecciona moldura
list                         l     lista o .tks em volta da linha corrente
info breakpoints                   estado dos breakpoints
print <nome>                 p     (fase T5 — variáveis)
locals                             (fase T5)
quit                         q
```

**O que o `tdb` NÃO faz, dito antes de ser prometido:** avaliar expressões (só nomes simples, em
T5); watchpoints; alterar valores; core dumps; e **passar por dentro de código C** — uma chamada a
uma biblioteca C aparece como uma moldura com endereço cru, e é aí que o gdb com DWARF continua a
ser a ferramenta certa.

---

# PEÇA 3 — Uso em cada debugger: cinco, completos, coláveis

Pressuposto: `teko build . --debug=lines -o bin`, com o `DW_AT_comp_dir` absoluto de §1.5.
O exemplo assume `main` em `src/main.tks` e binário em `bin/hello`.

## 8.1 gdb no terminal

```sh
teko build . --debug=lines -o bin
gdb ./bin/hello
```

```
(gdb) break src/main.tks:41
Breakpoint 1 at 0x1133: file src/main.tks, line 41.
(gdb) run
Breakpoint 1, add () at src/main.tks:41
41	    let s = a + b
(gdb) next
42	    s
(gdb) bt
#0  add () at src/main.tks:42
#1  0x0000555555555158 in main () at src/main.tks:52
(gdb) continue
```

Numa linha, para script:

```sh
gdb -batch -ex "break src/main.tks:41" -ex run -ex bt ./bin/hello
```

**O que NÃO funciona:** `print s` → `No symbol "s" in current context`; `info locals` → `No symbol
table info available.`; `ptype` de qualquer coisa nossa → nada. Literal, medido em §1.5.

## 8.2 lldb no terminal — sintaxe diferente, mesmo objeto

```sh
lldb ./bin/hello
```

```
(lldb) breakpoint set --file src/main.tks --line 41
Breakpoint 1: where = hello`add + 10 at src/main.tks:41:1, address = 0x0000000000001133
(lldb) run
(lldb) next
(lldb) bt
(lldb) continue
```

A forma curta `b src/main.tks:41` também resolve. Numa linha:

```sh
lldb -b -o "breakpoint set --file src/main.tks --line 41" -o run -o bt ./bin/hello
```

**O que NÃO funciona:** `frame variable` → `no variable information is available in debug info for
this compile unit`; `expression s` falha.

**Duas diferenças de forma que custam tempo a descobrir:** o lldb reporta `ficheiro:linha:coluna`
(usa o `DW_LNS_set_column` que emitimos; o gdb ignora-o); e o lldb desenrola **para dentro da
libc** por omissão, logo o `bt` tem 8 molduras, não 2 — as 5 extra são corretas, não ruído nosso.

## 8.3 VSCode via `cppdbg` (Linux) — completo

**A peça que falta em qualquer tutorial e sem a qual nada disto funciona:** o VSCode **recusa pôr
breakpoint num ficheiro cuja linguagem não conhece**. `.tks` não tem extensão de linguagem
registada, logo o gutter não aceita o clique. **Sem a primeira linha abaixo, o resto é inútil.**

`.vscode/settings.json`

```json
{
  "debug.allowBreakpointsEverywhere": true,
  "files.associations": { "*.tks": "plaintext", "*.tkt": "plaintext" }
}
```

`.vscode/tasks.json`

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "teko: build --debug=lines",
      "type": "shell",
      "command": "teko",
      "args": ["build", ".", "--debug=lines", "-o", "bin"],
      "options": { "cwd": "${workspaceFolder}" },
      "group": { "kind": "build", "isDefault": true },
      "problemMatcher": []
    }
  ]
}
```

`.vscode/launch.json`

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "teko: depurar (gdb / cppdbg)",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/bin/hello",
      "args": [],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}",
      "environment": [],
      "externalConsole": false,
      "MIMode": "gdb",
      "miDebuggerPath": "/usr/bin/gdb",
      "preLaunchTask": "teko: build --debug=lines",
      "sourceFileMap": {},
      "setupCommands": [
        {
          "description": "onde procurar os .tks quando o comp_dir nao bastar",
          "text": "-gdb-set directories ${workspaceFolder}",
          "ignoreFailures": true
        },
        {
          "description": "aceitar breakpoint ainda nao resolvido no arranque",
          "text": "-gdb-set breakpoint pending on",
          "ignoreFailures": true
        }
      ]
    }
  ]
}
```

Requer `ms-vscode.cpptools`. `sourceFileMap` fica **vazio de propósito**: com o `comp_dir` absoluto
de §1.5 não há nada para remapear — e é esse o argumento de fundo daquela decisão.

**O que NÃO funciona:** painel **Variables** vazio; **Watch** responde `-var-create: unable to
create variable object`; `-exec print x` dá o mesmo `No symbol`; **hover** não mostra valor.
**Funciona:** breakpoints no gutter, Continue/StepOver/StepInto/StepOut, **Call Stack** com nomes
Teko, e clicar numa moldura para saltar para o `.tks`.

## 8.4 VSCode via CodeLLDB (macOS) — completo

`.vscode/launch.json`

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "teko: depurar (lldb / CodeLLDB)",
      "type": "lldb",
      "request": "launch",
      "program": "${workspaceFolder}/bin/hello",
      "args": [],
      "cwd": "${workspaceFolder}",
      "env": {},
      "stopOnEntry": false,
      "terminal": "integrated",
      "preLaunchTask": "teko: build --debug=lines",
      "sourceMap": {},
      "initCommands": ["settings set target.inline-breakpoint-strategy always"],
      "sourceLanguages": ["c"]
    }
  ]
}
```

O `settings.json` de §8.3 é **igualmente obrigatório**. Requer `vadimcn.vscode-lldb`.

**Onde difere do `cppdbg`, campo a campo** — não é o mesmo ficheiro com o `type` trocado:

| `cppdbg` | CodeLLDB | porquê |
|---|---|---|
| `"type": "cppdbg"` + `"MIMode"` + `"miDebuggerPath"` | `"type": "lldb"`, **sem** MIMode nem caminho | o `cppdbg` **conduz** o gdb por MI; o CodeLLDB **embute** o lldb via API |
| `"stopAtEntry"` | `"stopOnEntry"` | grafias diferentes; a errada é ignorada em silêncio |
| `"environment": [{"name":…,"value":…}]` | `"env": { "K": "V" }` | array de registos vs. objeto |
| `"sourceFileMap"` | `"sourceMap"` | mesma função, nome diferente |
| `"setupCommands"` com `-gdb-set …` (MI) | `"initCommands"`/`"preRunCommands"` com comandos **de lldb** | `-gdb-set` não existe no lldb |
| — | **`"sourceLanguages": ["c"]`** | só o CodeLLDB tem, e **importa para nós**: casa com o `DW_LANG_C99` de §1.5 |
| `"externalConsole"` | `"terminal"` | — |

**O que NÃO funciona:** o painel **Variables** mostra `no variable information is available in debug
info for this compile unit` — o CodeLLDB é mais honesto aqui que o `cppdbg`, que mostra vazio.
**Funciona:** o mesmo conjunto de §8.3.

## 8.5 `tdb` no terminal — o quinto

```sh
teko build . --debug=lines -o bin
tdb exec ./bin/hello
```

```
tdb 0.3.1 — teko debugger
loaded bin/hello.tsym (v2): 412 functions, 5108 line rows, 412 frame descriptors
(tdb) break src/main.tks:41
Breakpoint 1 at 0x1133: src/main.tks:41 (in teko::demo::add)
(tdb) run
Breakpoint 1, teko::demo::add at src/main.tks:41
41	    let s = a + b
(tdb) bt
#0  teko::demo::add   at src/main.tks:41
#1  teko::demo::main  at src/main.tks:52
(tdb) next
42	    s
(tdb) continue
process exited with status 5
(tdb) quit
```

**Três coisas que o `tdb` faz melhor que o gdb, e é bom nomeá-las porque são a razão de ele existir:**
o `bt` mostra **`teko::demo::add`**, o nome qualificado Teko, não o símbolo manglado (o `.tsym` já
carrega os dois — medido em §3.2); o `bt` é construído do **descritor de frame** que o compilador
emitiu, não de análise de prólogo (§5.1); e **não desenrola para dentro da libc** por omissão, logo
não há 5 molduras de ruído.

**O que NÃO funciona no `tdb` inicial:** `print s` (fase T5); entrar em código C; core dumps.

## 8.6 `tdb` no VSCode via DAP — e a peça que não é grátis

`tdb dap` fala DAP, **mas DAP sozinho não basta**: o VSCode só oferece um `"type"` de depuração se
uma extensão o **registar** com `contributes.debuggers`. Isso é 1 crumb e **zero JavaScript** —
apenas um `package.json` a apontar o executável do adaptador. **Zero JS é uma virtude de lei aqui:**
o roadmap de tooling já ratificou que *"nenhuma invocação de processo externo a partir de um editor
usa concatenação de string para shell"*, por causa de um achado real de injeção em
`extensions/vscode/src/extension.js`. Um adaptador registado por `package.json` **não tem código
onde essa falha caiba**.

`tooling/vscode/package.json` (o fragmento que importa)

```json
{
  "contributes": {
    "debuggers": [
      {
        "type": "tdb",
        "label": "Teko (tdb)",
        "program": "./bin/tdb",
        "args": ["dap"],
        "languages": ["teko"],
        "configurationAttributes": {
          "launch": {
            "required": ["program"],
            "properties": {
              "program": { "type": "string", "description": "o binario Teko a depurar" },
              "args": { "type": "array", "items": { "type": "string" }, "default": [] },
              "cwd": { "type": "string", "default": "${workspaceFolder}" },
              "stopOnEntry": { "type": "boolean", "default": false }
            }
          },
          "attach": {
            "required": ["pid"],
            "properties": { "pid": { "type": "number", "description": "o pid a anexar" } }
          }
        }
      }
    ]
  }
}
```

`.vscode/launch.json`

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "teko: depurar (tdb / DAP)",
      "type": "tdb",
      "request": "launch",
      "program": "${workspaceFolder}/bin/hello",
      "args": [],
      "cwd": "${workspaceFolder}",
      "stopOnEntry": false,
      "preLaunchTask": "teko: build --debug=lines"
    }
  ]
}
```

**Onde difere dos outros dois:** não há `MIMode`, `miDebuggerPath`, `sourceFileMap` nem
`sourceMap` — o adaptador é nosso e resolve os caminhos com o **nosso** `.tsym`, logo não há
remapeamento a configurar. E `"languages": ["teko"]` **dispensa
`debug.allowBreakpointsEverywhere`**, porque a extensão registou a linguagem. **Isso é uma vantagem
concreta e mensurável do `tdb` sobre `cppdbg`/CodeLLDB**, e vale registá-la: as duas vias de
terceiros exigem que o utilizador ligue uma opção global do editor; a nossa não.

## 8.7 A frase que os cinco levam na documentação

> `--debug=lines` responde **"onde estou e como cheguei aqui"**. Não responde **"quanto vale `x`"**.
> Se o painel Variables está vazio, não é defeito — é este nível. O nível que responde a isso
> chamar-se-á `--debug=vars`.

Recusa nomeada, não omissão — a regra que o expurgo já fixou para o MinGW.

---

# PEÇA 4 — As camadas, orçadas com número

Unidade: **crumb** (passo pequeno, prova própria, reversível). **A Camada 0 não existe (R2).**

## 9.1 O Piso do compilador — 8 crumbs

Serve **os dois** consumidores. D1.1…D1.6 mantêm-se como no orçamento anterior (§5 de lá), com
**três alterações** e **um crumb novo**:

* **D0.1 (arnês) ganha a afirmação que defende §5.2:** afirma a profundidade do `bt` **através de uma
  função frameless**, não ">= N". Fixture: `adv.s`. Sem isso, a refutação é verdadeira hoje e
  indefesa amanhã.
* **D1.2 (`MLineMark`) passa a ter DOIS escoadouros declarados** — DWARF e `.tsym` v2. Mesma
  produção, mesmos testes; um consumidor a mais, e é o que confirma a escolha de forma (§3.1).
* **D1.3 (`src/backend/dwarf.tks`) tem o golden fixado por este documento** — §1.2, §1.3, §1.4,
  bytes, não prosa. **Não colide com nada, não está bloqueado por nada: pode começar hoje.**
* **D1.6 (superfície)** é o que a Peça 2 desenha, **mais** o emissor de `.tsym` v2 (as linhas `L`,
  `F`, `V`), **mais** os cinco `launch.json` de §8 em `docs/`.

**D1.7 — CRUMB NOVO: a sondagem de desenrolar em arm64**

* **Mexe em:** nada em `src/`. Um `.s` de sondagem irmão de `adv.s`, corrido na perna aarch64
  (`cargo/0.3.1-aarch64-elf` já tem lane).
* **Mede:** as cinco molduras traduzidas para a nossa forma arm64
  (`sub sp; stp x29,x30,[sp,#top]; add x29,sp,#top`), com lldb e gdb, sem CFI.
* **Ramos:** se a cadeia de `x29` recuperar (o esperado, §5.2), **+0 crumbs**; se não,
  **ou** retirar a clamação de `bt` da perna arm64 **para o interop DWARF** até alguém comprar CFI,
  **ou** pagar CFI (+4). **`tdb` não depende deste ramo** — a linha `F` dá a verdade.

## 9.2 Variáveis — 6 crumbs, e a sondagem já foi feita (por leitura)

O orçamento anterior disse "5+ crumbs, um deles perigoso" e pôs a sondagem primeiro. **A sondagem
está feita em §5.3**: a chave é `vreg_id`, `assign_lookup` é pública, é um JOIN. O item perigoso
mudou de identidade: não é plumbing, é **validade de localização**.

| crumb | mexe em | prova |
|---|---|---|
| **D2.1** | `src/lir/lower.tks` — sob o perfil, todo `let` nomeado passa por `lenv_bind_scalar_slot` em vez de `lenv_bind`. O mecanismo **já existe**; o crumb é a bifurcação por perfil. | `lower_test.tkt`: sob o perfil, `is_scalar_slot = true`; **sem** o perfil, o LIR é **byte-idêntico** ao de hoje. A segunda metade é o que torna o crumb seguro. |
| **D2.2** | `src/lir/lir.tks` — `LFunc` ganha `local_names`, `local_slots`, `local_types`, `local_decl_lines`, populados de `LEnv` no fim de `lower_function`. | `lower_test.tkt`: duas `let` + um shadow ⇒ **três** entradas, na ordem de ligação. O shadow é o teste que importa. |
| **D2.3** | o emissor de `.tsym` v2 — as linhas `V`, com o offset de `compute_frame_layout`. **Só isto já dá `print x` no `tdb`.** | o arnês do `tdb`: `print s` devolve o **valor** |
| **D2.4** | `src/backend/dwarf.tks` — `DW_TAG_variable`/`DW_TAG_formal_parameter` com `DW_AT_location = DW_OP_fbreg <sleb>` e `DW_TAG_lexical_block` para shadows. **Só é preciso se o dono quiser `print x` no gdb** (§6). | golden + `gdb -ex "print s"` devolve o valor |
| **D2.5** | o **registo de tipos congelados** + `DW_TAG_base_type` dos escalares. A lista é **explícita e enumerada**, nunca "tudo o que não sabemos que muda". | `dwarf_test.tkt`: um tipo **não** congelado produz **nenhuma** descrição; um congelado produz a esperada. **O ramo negativo é metade do crumb.** |
| **D2.6** | superfície `--debug=vars`, e **só aqui** `-g` passa a ser alias legal. | `help_test.tkt` + o teste que prova que `-g` deixou de ser rejeitado |

**Total: 6 crumbs, e 4 se o dono aceitar `print x` só no `tdb`** (D2.4 e D2.5 são a metade DWARF).
Nenhum é o penhasco do orçamento anterior, e a razão é §5.3 + §5.4: a fixação a slot mata os
intervalos de vida, e o registo de congelados mata a re-tipificação do LIR.

## 9.3 Legibilidade — 3 crumbs (não 1), e bloqueada

**Pré-condição nomeada:** `str` de três words aterrado. M.4 decide por sequenciamento; não é tensão.

| crumb | conteúdo |
|---|---|
| **D3.1** | o formatador do **`tdb`** para `str`, slices e uniões — **em Teko, dentro do `tdb`**, e é aqui que o `tdb` ganha a sua maior vantagem: **não é um ficheiro Python fora da árvore de tipos** |
| **D3.2** | os pretty-printers Python **do gdb** + a seção `.debug_gdb_scripts` que os auto-carrega (o `cppdbg` carrega-os, logo o VSCode Linux vem de graça) |
| **D3.3** | os synthetic providers **do lldb** — API **diferente**, ficheiro **diferente** |

**D3.3 é uma correção ao orçamento anterior, e vem de referência verificada:** Rust ship**a os dois**
— `gdb_load_rust_pretty_printers.py` **e** `lldb_lookup.py`/`lldb_providers.py`. Não há um ficheiro
que sirva gdb e lldb. O "1 crumb" de lá subestimava a **paridade de plataforma**.

**E D3.1 vs. D3.2/D3.3 é a decisão que o `tdb` torna possível:** se o formatador vive no `tdb`, ele
é **código Teko testado com o resto**, e a RED-FLAG 4 (§11.6 — um Python a replicar um invariante
interno do codegen) **desaparece para o nosso caminho**. Continua a existir para o interop.

## 9.4 Windows — 3 crumbs, e só para o interop

Ver §7.4 para o mecanismo verificado. **`tdb` não precisa de nada disto.**

| crumb | conteúdo | prova |
|---|---|---|
| **W0 — SONDAGEM, e é PRIMEIRO** | Ligamos com `clang --target=x86_64-pc-windows-msvc`. **A seção `.debug_*` do nosso `.obj` COFF sobrevive até ao `.exe`, ou o linker descarta-a?** `lld-link` preserva; `link.exe` é a incógnita. **Não posso medir aqui — não há host Windows.** | um `.obj` com as três seções, ligado na perna Windows do CI, e `llvm-dwarfdump` sobre o `.exe` |
| **W1** | generalizar `build_coff_strtab` para colocar também nomes de **seção** longos, e emitir a forma `/N` | golden: um objeto **sem** seções de depuração fica **byte-idêntico** ao de hoje |
| **W2** | routing das três seções + relocações, com as características **exactas** do Go (§7.4) | golden + `llvm-dwarfdump` + arnês na perna Windows |

**Se W0 disser que o linker descarta:** **não se paga CodeView.** Declara-se o interop DWARF
indisponível em Windows, com **recusa nomeada** (a regra do MinGW), e **`tdb` é o debugger de
Windows** — que é, de qualquer modo, o plano do dono. **Isto elimina os +6 crumbs de CodeView do
orçamento anterior sem deixar o Windows sem debugger.**

## 9.5 `tdb` — 5 fases, e o número honesto

Orçado pela via **Teko + shim no `teko_rt.c`**, e o porquê é medido. `teko::process` expõe `run`,
`run_quiet`, `spawn_redirected`, `wait_one` — **nada de `ptrace`, nada de sinais, nada de memória de
outro processo**. E a superfície de ponteiros que `PTRACE_GETREGS` exigiria: `uptr`/`ptr<byte>`
existem no checker (`scope.tks:388`, `:393`), **`unsafe` NÃO é palavra reservada**
(`parse_decl.tks:197` trata-a como `Ident`), e `c-types-and-marshalling-0.3.1.md` diz na primeira
linha **"DESIGN-AHEAD, doc-only. NOT implemented."**

**Logo: `tdb` em Teko puro está bloqueado em `unsafe`/`ptr<T>`/`c_types`. A via que o desbloqueia é
`src/runtime/teko_rt.{c,h}` — a exceção MANTIDA à lei Teko-only**, onde C novo e deliberado é legal.
Isso **não viola R1**: o dono disse *"não … em C"* sobre **o debugger**, e o debugger fica em Teko;
o que vai para o `teko_rt.c` é a fachada de syscall, exactamente como `tk_rt_run` já é. **Medi que
`ptrace` funciona neste ambiente**: `PTRACE_TRACEME` + `fork`/`execl` + `waitpid` + `PTRACE_CONT` →
`stopped=1`, `rc=0`, filho continua e sai.

| fase | conteúdo | crumbs |
|---|---|---|
| **T1 — o chão de controlo** | `tk_rt_dbg_spawn_traced(argv)`; `tk_rt_dbg_wait(pid)` com status **decodificado** (a regra 128+N que `tk_rt_run` já usa); `tk_rt_dbg_cont/step/detach`; `tk_rt_dbg_getregs -> []byte` + `setreg`; `tk_rt_dbg_peek/poke`. Mais o esqueleto `tdb exec` + `tdb version`. | **5** |
| **T2 — breakpoints** | x86-64: `peek` 1 byte → guardar → `poke 0xCC`; no `SIGTRAP` **recuar `rip`**, restaurar, e re-armar **depois** de um single-step (esquecer o ciclo é o defeito clássico "o breakpoint dispara uma vez"). arm64: `BRK #0` = `0xD4200000`, palavra de 4 bytes, **e o PC NÃO recua**. Mais a tabela endereço→byte-original. Mais `tdb attach`. | **3** |
| **T3 — posição** | o leitor de `.tsym` v2 (separador de linhas/campos à mão — não há `teko::str::split`; precedente: o parser TOML de `manifest.tks`); resolução `linha → endereço` e `endereço → linha`; o desenrolar pela linha `F`; `break`/`bt`/`next`/`step`/`finish`/`list`; `tdb run`. | **4** |
| **T4 — DAP** | o enquadramento `Content-Length: <n>\r\n\r\n<json>` + laço de pedidos; `initialize`/`launch`/`attach`/`setBreakpoints`/`configurationDone`; os eventos `stopped`/`continued`/`exited`/`terminated` e `threads`/`stackTrace`/`continue`/`next`/`stepIn`/`stepOut`; a extensão de registo de §8.6. **A camada JSON é grátis** — `src/encoding/json/json.tks` já expõe `decode`/`encode`. | **4** |
| **T5 — variáveis** | `scopes`/`variables`/`evaluate` + `print`/`locals`, sobre as linhas `V` de §9.2 | **2** |
| **portes** | macOS: `mach_vm_read_overwrite`/`task_for_pid` — **e exige assinatura de código com o entitlement `com.apple.security.cs.debugger`**, um custo de *release engineering*, não de crumbs, e o mais desagradável do lote. Windows: `DebugActiveProcess` + `WaitForDebugEvent` + `ReadProcessMemory`/`WriteProcessMemory` + `GetThreadContext`. | **+3 cada** |

**Total: 18 crumbs para Linux (T1…T5), 24 para as três plataformas.**

**E aqui está a resposta à pergunta que o brief mandou fazer — "quanto é que ter a tabela muda o
orçamento?":** **muda 2 crumbs em 18.** T3 custa 4 porque lê uma tabela que já temos; um leitor de
DWARF de verdade custaria ~6. **A tabela não é o que faz o `tdb` caro — o controlo de processo é.**
Vale dizê-lo alto, porque "já temos a tabela, logo o debugger é barato" é o argumento mais atraente e
o mais errado do assunto.

## 9.6 O total

| bloco | crumbs | bloqueado por |
|---|---|---|
| **Piso do compilador** (D0.1 + D1.1…D1.7) | **8** | nada. **D0.1 e D1.3 começam hoje.** |
| **Variáveis** (D2.1…D2.6) | **6** (ou **4** se só no `tdb`) | nada — §5.4 tira `str` do caminho crítico |
| **Windows interop** (W0…W2) | **3** | uma sondagem de um dia. **Ramo mau: 0**, não 6 (§9.4) |
| **Legibilidade** (D3.1…D3.3) | **3** | `str` de três words |
| **`kind = "tool"`** (K1…K4) | **4** | nada. **K1 começa hoje e vale por si** (§11.5) |
| **`kind = "tool"`** instalação (K5) | **1**, mas abre uma família de subcomandos nova | desenho próprio; `tdb` não espera por ele |
| **`tdb`** (T1…T5, Linux) | **18** | a escada de degraus (R1: *"não agora"*) + o shim em `teko_rt.c` |
| **`tdb`** portes macOS + Windows | **+6** | + entitlement de assinatura em macOS |
| ~~CFI DWARF~~ | **−4** | morto (§5.1, §5.2) |
| ~~CodeView~~ | **−6** | morto (§9.4) |
| ~~`#line` em C~~ | **−1** | morto (R2) |

---

# 10. A galinha e o ovo — nomeada, e com saída

`tdb` é escrito em Teko, compilado pelo backend nativo, e serve para depurar programas Teko —
**incluindo o próprio compilador**. Se o backend nativo estiver quebrado, `tdb` está quebrado.
**Três circularidades distintas, e não se resolvem da mesma maneira:**

**(1) A construção do `tdb`.** *"Não escrever em C"* ≠ *"não compilar pela rota C"*. Medi: `backend_of`
lê `TEKO_BACKEND`, e o **default é `Backend::C`**. Logo `teko build tooling/tdb` hoje **passa pela
rota C**, e isso é **a rede de segurança de arranque**: se o backend nativo se quebrar, `tdb`
continua a construir. **Isto é decisão do dono, não minha** — apresento o custo: manter a rota C viva
para construir o `tdb` **prolonga a vida de um ficheiro cuja deleção já está na fila** (fatia 6 do
expurgo). A alternativa honesta é `tdb` construir só pela rota nativa e **aceitar** que um backend
quebrado tira o debugger — o que é aceitável **precisamente porque o interop DWARF existe**: gdb com
o binário anterior depura o compilador quebrado. **Recomendo a segunda, e ela só é recomendável por
causa de §6(c).**

**(2) A correção das tabelas.** `tdb` lê `.tsym` v2, que sai do `MLineMark`. Se o `MLineMark`
mentir, `tdb` reporta a mentira fielmente e **nada o contradiz**. **A saída é o DWARF** — gdb e lldb
são leitores **independentes** da **mesma** origem. Uma discordância entre gdb e `tdb` sobre o mesmo
binário é um sinal de defeito que nenhum dos dois produz sozinho. **É por isto que §6(c) é a razão
que decide, e não um bónus.**

**(3) Os testes do `tdb`.** A suíte do `tdb` **não pode** depender do `tdb` nem de um binário Teko
correto. As suas fixtures são **objetos escritos à mão com tabelas de correção conhecida** — que é
exactamente o que `mini.s` e `adv.s` são. **Os dois ficheiros deste PoC são o primeiro fixture do
`tdb`**, e essa é a segunda razão de os versionar.

---

# 11. `tdb` como projeto: sítio, forma, e o "pulo do gato"

## 11.1 A regra que unifica `tdb`-projeto e `teko lsp`-subcomando — sem tensão para o dono

O dono invocou o LSP como analogia (*"assim como uma LSP"*) mas prescreveu a `tdb` uma **forma
diferente** da que o LSP tem ratificada. Medi a decisão do LSP,
`TEKO_ROADMAP_TOOLING.md` Eixo C, **RATIFICADO 2026-07-01**:

> O LSP **não** é um binário/processo `tooling/teko-lsp/` externo … vai direto para dentro do
> **próprio compilador**, como um **subcomando** `teko lsp` … escrito **nativamente em Teko**.
> **Cânone:** o intellisense **reaproveita** o front-end real de Teko
> (`teko::lexer`/`teko::parser`/`teko::checker`), nunca reimplementa.

**Parecem contraditórios; não são.** A regra que os unifica, e que a própria ratificação enuncia:

> **Quem precisa do FRONT-END vive em `src/` como subcomando. Quem precisa só de um FORMATO vive
> fora, como projeto próprio.**

O LSP **precisa** do checker — sem ele reimplementaria análise semântica, o que a lei DRY proíbe.
`tdb` **não precisa de nada do front-end**: precisa de um `.tsym` e de um processo. **A instrução do
dono é law-consistent, e nomeio a regra para que o próximo caso não a redescubra.**

## 11.2 O sítio: `/tdb` na raiz — DECIDIDO, e com o eixo corrigido

**O dono decidiu: `/tdb` na raiz.** Não apresento alternativa.

**E corrijo o registo, porque a razão enunciada não se mede.** O dono escreveu *"o que tem em
`/tooling` nao e escrito em teko, e ainda precisam ser reescritos, do zero"*. Medi:

| projeto | ficheiros `.tks` |
|---|---|
| `tooling/shared` | **6** |
| `tooling/vim` | **6** |
| `tooling/nano` | **6** |
| `tooling/emacs` | **6** |
| `tooling/vscode` | **6** |

**Os cinco JÁ SÃO escritos em Teko** — 30 `.tks` no total, cada um com o seu `.tkp` e
`kind = "binary"`. O que **não** é Teko é o que eles **produzem**: ficheiros de gramática
(`teko.tmLanguage.json`, `syntax/teko.vim`, …) para vim, nano, emacs e vscode.

**A decisão dele fica de pé, e por um eixo mais forte do que o que ele enunciou — de PAPEL, não de
linguagem:**

> `tooling/*` são **geradores de integração de editor** — utilitários de uma vez, cuja saída é um
> ficheiro de configuração de terceiros e cujo consumidor é um editor. `tdb` é **componente da cadeia
> de ferramentas Teko** — um executável que o utilizador corre, par do `teko`, e cuja saída é uma
> sessão de depuração.

Essa distinção sobrevive ao facto de os cinco serem Teko, e é a que se deve escrever. `/tdb` na raiz
diz "componente da cadeia"; `tooling/tdb/` diria "integração de editor", que é falso.

**A forma, como o dono a pediu:**

```
/tdb/tdb.tkp          # name = "tdb"; source = "src"; [artifact] kind = "tool"; command = "tdb"
/tdb/main.tks         # o virtual-main (sem declarações), como o main.tks do compilador
/tdb/src/…            # cli.tks, session.tks, breakpoints.tks, tsym.tks, unwind.tks, dap.tks
/tdb/tests/…          # fixtures: os .s escritos à mão (§10.3)
```

## 11.3 O acoplamento deixa de ser disciplina e passa a ser CONSTRUÇÃO

**Medi o padrão que `tooling/` já pratica, e ele é exactamente o que `tdb` precisa:** `grep` por
`../src`, `teko::checker`, `teko::lexer` em `tooling/` devolve **vazio**. `tooling/vscode` lê o
**ficheiro JSON** que `tooling/shared` emite — **acoplamento por FORMATO, não por dependência de
código**. Nenhum deles alcança `../src`.

> **`tdb` acopla-se ao compilador por FORMATO (`.tsym` v2), NUNCA por importar `src/`.** Um `tdb`
> que importe o checker nunca sai deste repo.

**E o `kind = "tool"` que o dono pediu é exactamente o que torna essa regra estrutural em vez de
voluntária.** Um `tool` **não declara o compilador em `[deps]`** — é a própria definição do tipo
(§11.5). Logo o `tdb` **não tem por onde** importar `teko::checker`: não há dependência declarada
que lhe dê acesso. **A migração para repo próprio deixa de ser opcional e passa a ser inevitável**,
que é o objectivo do dono, obtido por construção e não por vigilância.

**O corolário que protege o formato:** **`.tsym` v2 precisa de especificação escrita** — não "o que
o emissor faz". Emissor e leitor viverão em repos diferentes; um formato definido por implementação
não sobrevive à separação. **O crumb D1.6 entrega a especificação em `docs/`, e o leitor do `tdb`
(T3) é escrito contra a especificação, não contra o emissor.** (RED-FLAG 6, §14.3.)

## 11.5 `kind = "tool"` — a feature de manifesto, orçada, e o defeito que a bloqueia

### 11.5.1 A referência é C#, e é a única dos quatro com um TIPO declarado — verificada

`learn.microsoft.com/dotnet/core/tools/global-tools-how-to-create`, literal:

```xml
<PackAsTool>true</PackAsTool>
<ToolCommandName>dotnet-env</ToolCommandName>
<PackageOutputPath>./nupkg</PackageOutputPath>
```

> *"`<ToolCommandName>` is an optional element that specifies the command that invokes the tool
> after installation. If this element isn't provided, the command name for the tool is the assembly
> name…"*
>
> *"**Choose a unique value for `<ToolCommandName>`.** Avoid using file extensions (like `.exe` or
> `.cmd`) because the tool is installed as an app host and the command shouldn't include an
> extension. **This helps prevent conflicts with existing commands** and ensures a smooth
> installation experience."*

E: *".NET tools are NuGet packages that are installed from the .NET CLI"*, com `dotnet pack` a
produzir o `.nupkg` e `dotnet tool install` a instalá-lo — **global ou local**, e **nunca** como
`PackageReference`.

**O que o modelo do C# resolve e que `cargo install`/`go install` deixam ambíguo** — as duas coisas
que a pergunta do dono implica e que valem entrar no desenho:

| ambiguidade | `cargo install` / `go install` | C# (`PackAsTool`) |
|---|---|---|
| **duas ferramentas com o mesmo nome de executável** | o nome do comando **é** o nome do binário do crate/módulo; um segundo `install` **sobrescreve em silêncio** | o comando é **declarado à parte** (`ToolCommandName`), e a doc **avisa explicitamente** para o escolher único |
| **escopo global vs. local** | só global, de facto (`~/.cargo/bin`, `GOBIN`); um projeto **não consegue fixar** a versão de uma ferramenta | **os dois**, e o local tem manifesto próprio que fixa versão por repositório, reprodutível |
| **a ferramenta é dependência?** | é um **efeito colateral** de instalar algo que por acaso tem binário — nada o proíbe de também ser dependência | o **tipo do pacote** proíbe: um pacote de ferramenta não é referenciável como dependência |

**Portanto adoto do C#, nomeadamente:** (i) o **tipo declarado** (`kind = "tool"`, o que o dono
pediu); (ii) **o nome do comando declarado à parte do nome do projeto** (`command = "tdb"`), porque é
a única das três que evita a colisão silenciosa; (iii) a regra de que **o tipo, não a disciplina**,
proíbe a entrada em `[deps]`.

### 11.5.2 O DEFEITO que bloqueia a feature, e é PIOR do que "não tratado"

**Medi dois sítios, e o segundo é o que mata.**

**(a) O silêncio no parser.** `src/build/manifest.tks:558-566`:

```teko
// `kind = "binary" | "static" | "shared" | "package"` (C7.1m); unknown → Binary.
if q.text == "static" { artifact = Artifact::Static }
else if q.text == "shared" { artifact = Artifact::Shared }
else if q.text == "package" { artifact = Artifact::Package }
else { artifact = Artifact::Binary }
```

**Um `kind` desconhecido torna-se `Binary` EM SILÊNCIO**, e o doc-comment por cima até o admite.
Duas consequências, ambas da classe que esta lane já pagou: `kind = "tool"` escrito **hoje** já é um
binário comum (alguém pode adoptar a grafia antes de a feature existir e ter um verde que não
significa nada), e `kind = "binari"` também é um binário — erro de escrita no manifesto **sem
diagnóstico**.

**(b) E o defeito que ninguém previu: `check_main_file_rule` REJEITA um `tool` activamente.**
`src/build/tkp_rule.tks:16-22`:

```teko
if artifact == Artifact::Binary && !has_main {
    return error { message = "a binary project requires a main.tks" }
}
if artifact != Artifact::Binary && has_main {
    return error { message = "a library project (static/shared/package) may not have a main.tks" }
}
```

**Um `tool` É um executável e TEM `main.tks`.** Acrescentar `Tool` ao enum **sem** tocar esta função
faz **todo** projeto de ferramenta **falhar a construir**, com uma mensagem que enumera três kinds
que não o incluem. Isso não é "um `Tool` que nenhum consumidor trata" — é **um `Tool` que um
consumidor trata ERRADO, e o diagnóstico mente sobre porquê.** É o achado desta secção.

**Terceiro sítio, menor mas real:** `project.tks:3094`,
`artifact_path_for` → `if m.artifact != Artifact::Binary { return base }`. Um `tool` construído pelo
backend nativo que não liga receberia o caminho sem o sufixo `.o`. `Tool` tem de ficar **do lado do
`Binary`** aqui também.

**E a forma do `Tool`, medida em `project.tks:1759-1800`:** `backend()` despacha `Static` → tail
próprio, `Shared` → honest-stop, `Package` → o tail que monta o ZIP (`.tkh` + `.tkb` + `.tsym` via
`compress::write_zip`), e **cai** no caminho `Binary` para tudo o mais. Logo:

> **`Tool` = o caminho do `Binary` **e** o tail do `Package`.** Não é um kind novo do zero: é a
> **composição de dois que já existem**, e as duas metades já estão escritas.

**E isso resolve a tensão aparente no enunciado do dono** (*"emite um `.tkl`"* **e** *"sera compilado
na maquina do dev"*): o `.tkl` de um `tool` **não leva binário pré-construído** — leva o `.tkb` (a
árvore tipada serializada, que já é o payload do `Package`) **mais** a declaração do comando. A
máquina do dev compila-o para o seu alvo, como o dono descreveu. Nada de binários não-portáveis num
pacote.

### 11.5.3 O orçamento — 5 crumbs, e a ordem não é negociável

| crumb | conteúdo | prova |
|---|---|---|
| **K1 — FECHAR O SILÊNCIO. É PRIMEIRO, e vale por si.** | `manifest.tks` — um `kind` desconhecido passa a **ERRO DURO**, com a lista dos aceites na mensagem. O doc-comment perde o *"unknown → Binary"*. **Nenhum `Tool` ainda.** | `manifest_test.tkt`: `kind = "binari"` é **erro** (o ramo que hoje não existe e é metade do crumb); `kind = "tool"` é **erro** enquanto K2 não aterrar — que é exactamente o diagnóstico honesto de "a feature não existe"; e os quatro kinds válidos continuam a resolver como hoje |
| **K2** | `tkp_rule.tks` — `Tool` no enum, `check_main_file_rule` a tratá-lo **como `Binary`** (exige `main.tks`), e as **duas** mensagens re-escritas para nomear os kinds certos. `manifest.tks` reconhece a grafia. | `tkp_rule_test.tkt`: um `tool` **com** main passa; um `tool` **sem** main erra com a mensagem de binário; e a mensagem de biblioteca deixa de dizer "static/shared/package" quando o kind é outro |
| **K3** | `[artifact] command = "<nome>"` no `Manifest` (o `ToolCommandName` da referência), com a regra: ausente ⇒ o `name` do projeto; **e a recusa nomeada** de um comando que colida com um subcomando do `teko` | `manifest_test.tkt` + o teste de colisão |
| **K4** | `project.tks` — `Tool` no despacho de `backend()` como **`Binary` + o tail do `Package`** (uma entrada a mais no ZIP: a declaração do comando); `artifact_path_for` trata `Tool` como `Binary` | `project_test.tkt` + golden da lista de entradas do `.tkl`; e o golden que afirma que um `.tkl` de `kind = "package"` fica **byte-idêntico** ao de hoje |
| **K5 — SEPARÁVEL, e é o menos medido** | o lado do consumo: instalar um `.tkl` de ferramenta (compilar na máquina do dev, colocar o comando), e a regra de que **nunca entra em `[deps]`** | um fixture de ponta a ponta |

**K5 é separável e digo porquê:** medi os subcomandos existentes — `build`, `run`, `test`, `fmt`,
`init`. **Não há superfície de instalação nenhuma**, logo K5 abre uma **família de subcomandos nova**
(`teko tool install` / `list` / `uninstall`, na forma do `dotnet tool`). **`tdb` constrói e corre sem
K5** (`teko build tdb -o tdb/bin` e `tdb/bin/tdb`); K5 é o que o torna **distribuível**. Recomendo
K1–K4 com o `tdb`, e K5 como carga própria, com o desenho da família de subcomandos feito antes.

**Nota de risco sobre K1, e é a razão de ele vir primeiro:** K1 é o único crumb deste documento que
pode **quebrar um `.tkp` existente** — qualquer manifesto na árvore com um `kind` mal escrito passa a
falhar. **Isso é a feature, não o defeito.** Mas exige uma varredura: medi `teko.tkp`
(`kind = "binary"`) e os cinco de `tooling/` (`kind = "binary"`) — **todos válidos**. Nenhuma quebra
esperada, e o crumb deve afirmá-lo com um teste que varre a árvore.

## 11.6 `TEKO_ROADMAP_TOOLING.md` — sim, eixo novo

Medi a estrutura: Eixos **A** (fonte única de léxico), **B** (cores), **C** (LSP, DIFERIDO), **D**
(clientes + build), **E** (empacotamento). `tdb` não cabe em nenhum: não é cor, não é intellisense,
não é cliente de LSP, não é empacotamento.

**Recomendo `Eixo F — Depuração (`tdb`)`**, e recomendo que ele **espelhe a forma do Eixo C**, porque
o Eixo C é o único precedente de "peça grande, em Teko, DIFERIDA por marco de estabilização": tabela
de entregas F1…Fn com estado `diferido`, o marco de liberação nomeado, e o cânone escrito (aqui: o
acoplamento por formato de §11.3, que é o inverso exacto do cânone do C — reaproveitar o front-end).
**É um crumb de documentação, e reporto-o; não abro issue.**

---

# 12. As quatro referências — nomeadas, verificadas por fonte, e a nossa superfície medida

Atribuições do dono: superfície → Rust, controlo → Zig, addins → C#, comportamentos → Go.

## 12.1 A tabela que me foi passada, verificada linha a linha

| | Rust | Zig | Go |
|---|---|---|---|
| **formato** | ✅ `split-debuginfo`: `off` para ELF (*"DWARF debug information can be found in the final artifact in sections"*), `packed` **default em Windows MSVC** = `*.pdb`, macOS = `*.dSYM` | ✅ DWARF; **e mais forte no leitor** (linha abaixo) | ✅ **e é o achado de §7.4.** `pe.go` chama `pefile.addDWARF()`; nomes longos pela string table na forma `/N`; **nenhum CodeView** |
| **debugger próprio** | ✅ **nenhum**; `rust-gdb`/`rust-lldb` são wrappers que carregam printers | ✅ **nenhum**, **e o leitor confirma-se**: `lib/std/debug.zig` importa `debug/Dwarf.zig`, `debug/Pdb.zig`, `debug/ElfFile.zig`, `debug/MachOFile.zig`, e escolhe `SelfInfo` por `ObjectFormat.default(...)` | ✅ **delve, próprio.** `go.dev/doc/gdb`: *"Delve is a better alternative to GDB … It understands the Go runtime, data structures, and expressions better than GDB"* |
| **tipos** | ✅ printers; **e ver §11.6** sobre `DW_TAG_variant_part` | ✅ DWARF padrão | ✅ `runtime-gdb.py` existe, **e a doc avisa** — §12.4 |
| **Windows** | ✅ PDB real, `packed` é o **default** lá | ✅ fraco na emissão | ⚠️ **CORRIJO A NUANCE:** `go.dev/doc/gdb` diz DWARFv4 em *"Linux, macOS, FreeBSD or NetBSD"* e **não menciona Windows** — mas isso é sobre **usar o gdb**, não sobre **emitir**. O `pe.go` **emite** DWARF, e o delve suporta `windows/amd64` e lê-o. Emissão ✅; gdb-em-Windows não documentado. |

**Duas correções, e mais nada:** (a) "Windows / Go: funciona" é verdade pelo **delve**, não pelo gdb;
(b) a linha do Zig é **mais forte** — não é um leitor "para stack traces", é uma abstração
`SelfInfo` por formato de objeto, com leitor de **PDB** incluído.

**Escopo do delve, medido** (FAQ oficial): `linux/amd64`, `linux/arm64`, `linux/386`,
`windows/amd64`, `darwin/amd64`. **Nem `darwin/arm64`.** Um debugger próprio, com uma década de
manutenção e patrocínio corporativo, cobre **cinco** pares — e não cobre o Mac de hoje. **Isto não é
argumento contra o `tdb` (o dono decidiu); é o aviso de escopo do §9.5:** `tdb` cobrirá uma
plataforma de cada vez, e é **por isso** que o interop DWARF de §6(b) não é dispensável.

## 12.2 Rust → superfície: **aplica-se, e é a peça que adotei**

`line-tables-only` é o nosso Piso, nomeado por eles (§7.2). Adotei o **vocabulário**
(`--debug=lines`) e o **nível**. Não adotei `split-debuginfo`: `dsymutil`/`dwp`/`pdb` são otimizações
de tamanho que só fazem sentido depois de haver DWARF que valha separar. **A nossa superfície
suporta:** sim — `--opt=<n>` é o precedente exacto de flag nivelada com `=`.

## 12.3 Zig → controlo: **aplica-se, e nós já o seguimos sem lhe dar o nome**

O `.tsym` é o padrão do Zig: **informação de posição própria, lida pelo nosso runtime, para stack
traces sem depender de terceiros**. Com a referência verificada ao lado, deixa de ser medição solta e
passa a **precedente de desenho**.

| | `.tsym` v1 hoje | `SelfInfo` do Zig |
|---|---|---|
| granularidade | **por função** | **por endereço** |
| fonte | ficheiro de texto ao lado / no `.tkl` | **as seções de depuração do próprio binário** |
| o que resolve | *que função* estava na pilha | *que linha* estava na pilha |

**A diferença de desenho que registo, e é uma escolha consciente:** o Zig lê **DWARF/PDB de verdade**,
nós propomos `.tsym` v2, um formato nosso. A favor do nosso: é 4 crumbs mais barato de ler (§9.5) e é
texto. A favor do Zig: um formato só, e funciona sobre binários que não são seus. **Recomendo o nosso
para T3 e registo que, no dia em que `tdb` quiser entrar em código C, o leitor de DWARF entra** — e
aí `.tsym` v2 fica como o caminho rápido, não como o único.

## 12.4 Go → comportamentos: **aplica-se em três sítios, e é o precedente directo do `tdb`**

**(a) O critério.** Verificado (`go.dev/doc/gdb`, literal):

> *"GDB does not understand Go programs well. The **stack management, threading, and runtime**
> contain aspects that differ enough from the execution model GDB expects that they can confuse the
> debugger and cause **incorrect results** … it is not a reliable debugger for Go programs,
> **particularly heavily concurrent ones**."*

Apliquei o critério a Teko, e o resultado importa **mesmo com o `tdb` já decidido**, porque diz **de
onde vem o valor** do `tdb` e portanto **o que priorizar** nele:

| propriedade do nosso runtime | torna gdb/lldb **ERRADOS**? | consequência para o `tdb` |
|---|---|---|
| **arena** (região raiz, bump, `tk_arena_push`/`pop`) | **não** — incompleto, não errado | o formatador de arena (D3.1) é **valor**, não correção |
| **erros como valores** (`T \| error`) | **não** — o gdb vê a caixa, não o membro ativo | §11.6: e o `tdb` resolve-o **melhor** que um Python |
| **monomorfização** | **não**, e medi: `tk_emit_tsym` já emite `<símbolo>\t<nome-teko>`, e o `bt` do `tdb` mostra `ns::fn` | vantagem já paga pelo `.tsym` |
| **`defer`** | **não, e é bom que não** — com granularidade de statement, o `step` **entra** nas linhas do `defer`, que é o comportamento certo: esse código corre | nada a fazer; **não esconder** |
| **concorrência** | **não hoje.** Medi `concorrencia-adiantada-s8.md` (`cargo/20-concorrencia-adiantada`): o chão é `pthread_create`/`pthread_join`/`pthread_self` — **threads 1:1 do SO**, sem escalonador nosso, sem pilhas geridas. É exactamente o que gdb/lldb modelam nativamente. | **`tdb` não precisa de modelo de tarefas na fase inicial** — e é isto que mantém T1…T5 em 18 crumbs em vez de o dobro |

**O gatilho que MUDARIA o escopo do `tdb`, nomeado:** no dia em que Teko ganhar **escalonador
próprio, tarefas verdes ou pilhas geridas/crescíveis**, `tdb` passa a precisar de um modelo de
tarefas (listar, comutar, desenrolar pilhas segmentadas) — e o orçamento de §9.5 **sobe**. **Estado
medido hoje: a condição NÃO está satisfeita**, e a palavra "corrotina" naquele desenho tem como chão
uma thread de SO. **Reabrir nesta secção quando mudar.**

**(b) O mecanismo de DWARF-em-PE** — §7.4, que apaga o item mais caro do orçamento anterior.

**(c) A forma de empacotamento e de DAP** — verificado
(`Documentation/api/dap/README.md`): `dlv dap` *"starts a single-use DAP-only server"*, e
*"The primary user of this mode is VS Code Go"*, com o VSCode a **lançar ele mesmo o servidor** ou a
ligar-se a `host:port`. **É um MODO DO PRÓPRIO BINÁRIO, não um adaptador separado.** `tdb dap`
copia isso (§7.7, §8.6), e é o que evita um segundo executável para manter.

## 12.5 C# → addins: **aplica-se a UMA metade, e não à outra — e a metade que se aplica é `kind = "tool"`**

**A metade que NÃO se aplica: o modelo de depuração.** O do .NET é um **runtime gerido** com
ICorDebug e um debugger *in-process*: pressupõe CLR, metadados, e um contrato de depuração dentro do
runtime. Teko compila para nativo sem runtime gerido. **Invocar o ICorDebug aqui seria o erro que
esta lane já pagou**, e não o invoco.

**A metade que SE aplica, e é a peça de empacotamento que o dono pediu: `dotnet tool`.** Verificada
em §11.5.1 — `PackAsTool` + `ToolCommandName` + `dotnet pack` → `.nupkg` instalável global ou
localmente, **nunca** como `PackageReference`. **É a única das quatro referências com um TIPO de
pacote declarado** para "executável que se instala mas não é dependência"; `cargo install` e
`go install` obtêm o mesmo efeito **sem tipo**, e é isso que os deixa ambíguos nos dois pontos de
§11.5.1 (colisão de nome de comando, e escopo global vs. local fixável).

**A nossa superfície suporta o que a referência oferece?** Medido, e a resposta é *quase*:
`[artifact] kind` existe e já tem quatro valores; o escritor de `.tkl` existe (`compress::write_zip`,
o tail do `Package`); o `.tkb` que a máquina do dev compilaria existe. **O que falta é o que §11.5.3
orça — e um dos cinco crumbs é fechar um silêncio que já hoje aceita `kind = "tool"` como binário
comum.** O que **não** copio do C# é o `PackageOutputPath`: já temos `-o`.

---

# 13. Correções e achados sobre o orçamento anterior

**Não editei `debugger-orcamento-0.3.1.md`.**

## 13.1 As 4 relocações estão CERTAS — e a razão que faltava é o que torna o número reproduzível

O documento diz *"4 relocações, todas `Abs64`"* e não diz **de onde vem o 4**. Refiz e obtive 4, mas
só depois de descobrir que **duas sequências de programa de linha — uma por função — dão 5** (dois
`DW_LNE_set_address`). O 4 exige **UMA sequência por unidade de compilação**, atravessando as funções
com `DW_LNS_advance_pc`:

```
.rela.debug_info  contains 3 entries:  R_X86_64_64 add, R_X86_64_64 add, R_X86_64_64 main
.rela.debug_line  contains 1 entry:    R_X86_64_64 add
```

Sem essa nota, um implementador que emita uma sequência por função obtém N+3 e conclui, com razão,
que o golden está errado. **Regra a fixar no D1.3: uma sequência por CU, uma única
`DW_LNE_set_address`.** É o que o gcc faz e o que o Go faz.

## 13.2 `DW_FORM_data1` para `decl_line` transborda no nosso próprio corpus

O orçamento não fixa formas. `data1` (o que o `gcc -g1` usa em programas pequenos) **não serve**: o
próprio documento usa `main.tks:410` como exemplo. §1.2 fixa `data2` para a linha e `udata` para o
ficheiro.

## 13.3 RED-FLAG 3 estava errada — §5.2

E estava errada **por não ter sido medida**. Registo-o sem sarcasmo: **levantá-la foi a coisa certa**
e é o que fez esta medição existir. O defeito não foi levantá-la; foi **vender o `bt` na tabela de
camadas ao mesmo tempo**.

## 13.4 "1 crumb" para pretty-printers subestima a paridade — §9.3

Rust ship**a dois** ficheiros (gdb e lldb), com APIs diferentes. São 3 crumbs, não 1 — e com o `tdb`,
o terceiro (D3.1) é o **melhor** dos três.

## 13.5 O §7 (Windows) parte de uma premissa que a fonte do Go desmente — §7.4, §9.4

*"nenhum dos crumbs D1.3–D1.5 rende um byte de valor no Windows"* é **falso**: rende **todos** os
bytes, se o contentor os aceitar. E *"o Zig não resolveu a assimetria"* continua **certo** — mas o
Zig deixou de ser a única referência: o **Go resolveu-a, sem CodeView**. E com `tdb`, Windows nem
precisa de contentor.

## 13.6 A via do `variant_part` para as uniões **não se aplica** — a correção grande

O orçamento recomenda pretty-printers como resposta à legibilidade, com a referência do Rust por
trás. Medi duas coisas que obrigam a mudar de forma:

1. **`DW_TAG_variant_part` é DWARF 5.** O nosso PoC, verificado, é DWARF **4**. A via do Rust exige
   subir a versão do escritor — **um custo que ninguém orçou**.
2. **As nossas uniões têm TRÊS rails, não um** (`codegen.tks:2048` e vizinhança):
   * **niche** — `T | null` de uma word: `{ptr = NULL}` **significa** `null`, e **não há palavra de
     tag nenhuma**. Um `variant_part` **exige** um discriminante; um rail sem tag não tem o membro
     que o `DW_AT_discr` aponta.
   * **InlineTag** — `{ uint8_t tag; payload }`, em frame.
   * **box-em-arena** — sem `.tag`; o padrão do próprio ponteiro discrimina.

**Resolução, e é mais barata que subir para DWARF 5:** descrever a união pela sua **forma literal**
(estrutura com membro de tag quando existe + união para o payload) e deixar o **formatador** ler a
tag e escolher. Funciona em DWARF 4.

**E é aqui que a red-flag nova aparece, e é aqui que o `tdb` a resolve:**

> **RED-FLAG 4 (nova) — o formatador do rail *niche* codifica um invariante interno do codegen.**
> Um printer Python que diga "se o ponteiro é NULL, o valor é `null`" replica, num ficheiro fora da
> árvore de tipos, uma decisão de `cg_union_niche_member`. Se a regra de niche mudar — e ela é uma
> otimização, logo **vai** mudar — o printer mente **em silêncio**, e nenhum teste de compilador o
> apanha.
>
> **Mitigação, e é dupla.** (i) Para o **interop**: o rail vai no **formato**, não no Python — um
> campo por tipo no `.tsym` v2 (ou um atributo `lo_user` no DWARF), e o printer **lê** o rail em vez
> de o adivinhar. Custa um campo; poupa uma mentira silenciosa. (ii) Para o **nosso** caminho: o
> formatador do `tdb` é **código Teko, testado com o resto**, logo a red-flag **desaparece** — e isto
> é uma vantagem concreta e nomeável do `tdb` sobre a via dos pretty-printers.

---

# 14. O que fazer, por onde começar, e o que fica em risco

## 14.1 O menor conjunto que não ensina mal

**O Piso do compilador: 8 crumbs** (D0.1, D1.1…D1.7). Clama, e garante: **breakpoint por linha de
`.tks`, listagem do texto Teko, `step`/`next`, e `bt` com nomes Teko** — no terminal (gdb **e** lldb)
e no VSCode (`cppdbg` **e** CodeLLDB), **sem uma linha de servidor nosso**, e produzindo **de
imediato** o `.tsym` v2 de que o `tdb` viverá. Não clama `print x`, e a não-clamação está **escrita**
em três sítios.

**Isto ensina mal?** Respondo de frente: **não, desde que diga o que não faz** — e o desenho força
que diga. O que ensinaria mal é o que o dono rejeitou: vender `bt` sem prova (agora tem, §5.2), ou
mostrar um `str` com o comprimento errado (a regra de §5.4 impede-o).

**Com `print x`: 12 crumbs** (Piso + as 4 variáveis do lado `tdb`/`.tsym`), ou **14** se as
variáveis também forem para o DWARF. As duas pré-condições que o orçamento anterior temia **caíram
por medição**: a sondagem do regalloc está feita (§5.3, é um JOIN) e `str` saiu do caminho crítico
(§5.4).

## 14.2 Vale começar agora? Sim — e só um bloco espera

| bloco | pode começar? |
|---|---|
| **D0.1** (arnês) + **D1.3** (`dwarf.tks`) | **HOJE.** Zero colisão (ficheiros novos), zero bloqueio, e o golden do D1.3 está **fixado em §1.2–§1.4 deste documento**. |
| **W0** (sondagem Windows) | **HOJE.** Sem produto; decide 3 crumbs vs. "Windows é do `tdb`". |
| **D1.7** (sondagem arm64) | **HOJE**, na lane aarch64. Sem produto. |
| **a especificação do `.tsym` v2** (§11.3) | **HOJE.** Doc-only, e é o contrato de que o `tdb` dependerá num repo diferente. |
| **o Eixo F do `TEKO_ROADMAP_TOOLING.md`** (§11.6) | **HOJE.** Doc-only. |
| **K1** — fechar o silêncio do `kind` (§11.5.3) | **HOJE, e independentemente de todo o resto.** É um defeito vivo (`kind = "binari"` constrói), não uma preparação. Não colide com nada nesta lane. |
| K2…K4 (`kind = "tool"`) | depois de K1, e **antes** de o `tdb` precisar do seu `.tkp` |
| D1.1, D1.2, D1.4, D1.5, D1.6 | assim que os agentes vivos saírem de `lower.tks`, `isel_x86_64`, `encode_x86_64`. O desenho **aditivo** (`MLineMark`) é o que mantém isto aplicável. |
| D2.* | depois do Piso. **Não espera pelo `str`.** |
| **D3.*** | **ESPERA** pelo `str` de três words. Único bloco bloqueado, e a lei (M.4) decide. |
| **`tdb` T1…T5** | **ESPERA**, por R1 (*"não agora"*) — depois da escada de degraus. **Mas T1 não tem dependência técnica nenhuma além do shim**: é o único que poderia arrancar cedo se o dono quisesse. |

**A peça que não expira, se o dono quiser gastar o mínimo:** **D0.1, o arnês.** Activo de teste,
agnóstico de camada e de debugger, e o seu fixture (`mini.s` + `adv.s`) **já existe neste commit** —
e é, por §10.3, também o primeiro fixture do `tdb`.

## 14.3 Riscos, red-flags, e a ausência de HALT

**Não cunho KNOWN-STOP.**

* **RED-FLAG 1 (`str`), CONFINADA** — §5.4. Não bloqueia o Piso nem as variáveis (com a regra do
  congelamento). Bloqueia a legibilidade. Sequenciamento, não tensão.
* **RED-FLAG 2 (nomes no regalloc), REDIMENSIONADA** — §5.3. É um JOIN. O perigo real é a validade da
  localização, e a mitigação (`lenv_bind_scalar_slot`) já está no código.
* **RED-FLAG 3 (desenrolar), MORTA DUAS VEZES** — §5.1 (o `tdb` tem a verdade do compilador) e §5.2
  (a heurística do gdb, medida, recupera todas as formas). **Aberta só em arm64 para o interop** —
  crumb D1.7, dois ramos orçados.
* **RED-FLAG 4 (NOVA) — o formatador do rail *niche* replica um invariante interno do codegen** —
  §13.6. Mitigação dupla: o rail vai no formato para o interop; e o formatador do `tdb` é Teko
  testado.
* **RED-FLAG 5 (NOVA) — a armadilha de `project_arg_of`** — §7.1. Uma flag nova que não entre na sua
  lista de saltos torna-se **o caminho do projeto**, sem erro de flag desconhecida. Todo crumb que
  acrescente flag paga um teste que o prova.
* **RED-FLAG 6 (NOVA) — `.tsym` v2 sem especificação escrita não sobrevive à separação de repos** —
  §11.3. Se o formato for definido pelo emissor, o dia da migração encontra um leitor e um escritor a
  divergir sem contrato. Mitigação: a especificação é entregável do D1.6, e o leitor do `tdb` é
  escrito contra ela.
* **Sondagem W0** é bloqueante para o número do Windows-interop, não para o Piso nem para o `tdb`.
* **Semente de bootstrap:** reli o desenho. `struct`, `enum`, `variant`, `[]T`, `match`,
  `teko::list::push`, `teko::str::slice_to`/`slice_from`/`concat`/`contains`, `teko::encoding::json`
  — **tudo já na semente**. Nada a sequenciar por causa dela. O único bloco que precisaria de
  funcionalidade não aterrada é o `tdb` em Teko **puro** (`unsafe`/`ptr<T>`/`c_types`) — e é por isso
  que ele é orçado pela via do shim em `teko_rt.c` (§9.5).

**Sem HALT.** As tensões resolveram-se por lei ou por medição: `.tsym` vs. formato novo pela
legislação que já designou `.tsym` *"for the debugger"*; `tdb`-projeto vs. `teko lsp`-subcomando pela
regra front-end/formato de §11.1; `str` por M.4; `-g` vs. `--debug=lines` pelo precedente
`has_backend_flag` no mesmo ficheiro + a citação verificada do rustc; nada no `.tkp` pelo *"an AXIS
of the build, not a global"* do próprio `build_cc_argv`; o shim em C pela exceção mantida de
`teko_rt.{c,h}`, que **não** contradiz R1.

* **RED-FLAG 7 (NOVA) — `kind` desconhecido é `Binary` em silêncio, HOJE** — §11.5.2. Não é um risco
  do futuro: `kind = "binari"` já hoje constrói um binário sem diagnóstico, e `kind = "tool"` escrito
  antes de K2 dá um verde que não significa nada. **K1 fecha-o e é o primeiro crumb da feature.**
* **RED-FLAG 8 (NOVA) — `check_main_file_rule` REJEITA um `tool`** — §11.5.2. Acrescentar `Tool` ao
  enum sem tocar `tkp_rule.tks:19` faz todo projeto de ferramenta falhar, com uma mensagem que
  enumera três kinds que não o incluem. É o achado que ordena K1 antes de K2.

**As DUAS coisas que são do dono, e são escolhas, não tensões** (o **sítio já não está aqui** —
`/tdb` na raiz está decidido, §11.2):

1. **A rota de construção do `tdb`** — rota C como rede de segurança de arranque (prolonga um
   ficheiro cuja deleção está na fila) ou só rota nativa (um backend quebrado tira o debugger, e é
   aceitável **porque** o interop DWARF existe). §10(1). **Recomendo só-nativa.**
2. **`print x` no gdb, ou só no `tdb`?** — 6 crumbs vs. 4. §9.2. Se `tdb` for o debugger de casa, os
   D2.4/D2.5 (a metade DWARF) são interop, e o interop de variáveis é o mais caro e o menos usado.
   **Recomendo adiar a metade DWARF e reavaliar quando o `tdb` existir.**

**E uma coisa que NÃO é escolha e que reporto:** K5 (instalar uma ferramenta) abre uma **família de
subcomandos que não existe** (`teko tool install/list/uninstall`). Medi que os subcomandos hoje são
`build`, `run`, `test`, `fmt`, `init` — zero superfície de instalação. **Isso é maior que um crumb de
debugger e merece desenho próprio.** REPORTO; não abro issue.
