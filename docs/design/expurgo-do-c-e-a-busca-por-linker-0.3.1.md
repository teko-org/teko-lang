# Expurgo do C e a virada para o LINKER — mapa medido (0.3.1)

Ruling do owner (2026-07-26):

> "não é .c que importa, importa o linker, dito isso, nem mesmo cc ou gcc importam, importa o
> linker pq não devemos mais emitir nenhum arquivo .c e todos os arquivos .c e .h presentes no
> repositório devem ser imediatamente removidos e drenados com o que já está pronto no projeto,
> incluindo ajustes nas lanes e no compilador para procurar pelo linker e não pelo compilador C."

Ruling adicional (2026-07-26):

> "E sobre mingw, proibido, tem que usar o linker nativo e não emulado"

Este documento é o MAPA MEDIDO que precede o expurgo. Cada afirmação abaixo carrega a evidência
que a produziu; nada aqui é inferência de leitura.

---

## 1. O inventário — o que sai

`git ls-files '*.c' '*.h'` no vagão `ci/0.3.1-lanes-e-seeds`, oito arquivos, 3.894 linhas:

| linhas | arquivo | papel |
|---:|---|---|
| 2.580 | `src/runtime/teko_rt.c` | runtime de execução dos programas gerados |
| 865 | `src/runtime/teko_rt.h` | seu cabeçalho — o `teko.c` gerado faz `#include "teko_rt.h"` |
| 172 | `src/win32_compat.h` | shims Win32 que `teko_rt.c` alcança por `../win32_compat.h` |
| 55 | `src/assert/assert.c` | seed de `teko::assert` |
| 23 | `src/assert/assert.h` | seu cabeçalho |
| 91 | `scripts/region_drop_subtree_test.c` | teste de arena, em C, contra `teko_rt.c` |
| 85 | `scripts/tk_arena_commit_test.c` | idem |
| 23 | `scripts/ar_link_run_consumer.c` | consumidor C de um `.a` produzido pelo backend próprio |

A lista bate exatamente com a do briefing. Nenhum arquivo `.c`/`.h` fora dela existe no vagão.

---

## 2. A superfície REAL do runtime no caminho nativo — 11 símbolos, não 156

`teko_rt.c` define 156 símbolos. O caminho nativo (`src/lir/**` + `src/backend/**`) referencia,
como literal de string que vira símbolo externo, exatamente estes:

| símbolo | quem o nomeia | por quê |
|---|---|---|
| `tk_print` | `lir/lower.tks:1095` | `teko::io::print` |
| `tk_println` | `lir/lower.tks:1096` | `teko::io::println` |
| `tk_write` | `lir/lower.tks:1099` | `teko::io::write` |
| `tk_eprint` | `lir/lower.tks:1097` | `teko::io::eprint` |
| `tk_eprintln` | `lir/lower.tks:1098` | `teko::io::eprintln` |
| `tk_ewrite` | `lir/lower.tks:1100` | `teko::io::ewrite` |
| `tk_panic_str` | `lir/lower.tks:1094` | `panic` |
| `tk_exit` | `lir/lower.tks:1093` | `teko::process::exit` |
| `tk_str_concat` | `lir/lower.tks:4479,4482` | interpolação `$"…"` |
| `tk_i64_to_str` | `lir/lower.tks:4435` | interpolação de inteiro com sinal |
| `tk_u64_to_str` | `lir/lower.tks:4435` | interpolação de inteiro sem sinal |

**Correção ao briefing.** A lista de dez do briefing incluía `tk_str` e omitia `tk_panic_str` e
`tk_exit`. `tk_str`, `tk_region`, `tk_closure`, `tk_vt_*` e `tk_fmt_*` aparecem no caminho nativo
**apenas dentro de doc-comments** que descrevem o que o emissor de C fazia — nenhum deles é
emitido como referência. A superfície viva é de **onze** símbolos.

Os outros 145 símbolos de `teko_rt.c` existem para o `teko.c` gerado — morrem com o emissor.

---

## 3. O FFI bottom que `teko_rt.tks` cita como diferido — CONFIRMADO fechado no backend nativo

`src/runtime/teko_rt.tks` (707 linhas, o gêmeo canônico em Teko) se declara não realizável assim:

> "print/println bottom out at the host write (the FFI bottom — crumb C1, DEFERRED). panic
> bottoms out at the host abort/exit"

Essa condição **acabou de ser satisfeita**. O commit `afdb1fd8` (já no vagão) fez a chamada a um
`extern fn` mirar o SÍMBOLO C declarado, e não o mangling Teko:

- nasce `LExternFn { namespace; name; c_symbol }` e a tabela `collect_externs(prog)`, threaded
  read-only pelo `LowerCtx` na mesma forma que `enums`;
- `call_symbol` consulta `find_extern_symbol` antes de cair no mangle;
- `collect_undefined_x86` já derivava `GLOBAL|NOTYPE / SHN_UNDEF` de qualquer alvo de relocation
  não definido, e `objfile_elf.tks` já emitia `R_X86_64_PLT32`. **Só o NOME estava errado.**

A prova do commit: `pub extern fn c_getpid(): i32 = "getpid"` passou de
`ld: undefined reference to 'externprobe__p__c_getpid'` para `nm -u → U getpid`, binário roda.

**Consequência:** `teko_rt.tks` pode declarar `extern fn write(fd, buf, n)`, `extern fn _exit(c)`,
`extern fn abort()` e fechar o bottom sem uma linha de C. Os onze símbolos da tabela acima passam
a ser funções Teko `exp fn` compiladas no próprio objeto do programa — não há runtime separado
para linkar, e portanto não há runtime separado para EMBUTIR no compilador distribuído.

Isso torna a "Fase 7" (`src/build/project.tks:525` — *"bundling the runtime is Phase 7"*)
desnecessária por construção: não há o que empacotar quando o runtime é Teko compilado junto.

**O item que NÃO se resolve deletando:** o arena. `tk_arena_push/pop/commit`,
`tk_region_new/alloc/drop/drop_subtree/lookup/register/root`, `tk_region`, `tk_regions_free_all`.
Decreto do owner: vai para Teko. É projeto, não tradução.

---

## 4. A escada `seed .30 → gen1` — MEDIDO, e a resposta tem duas metades

A pergunta: se o runtime sair do repositório, de onde o seed 0.3.0.30 (que ainda emite C) tira o
dele?

### 4.1 O seed NÃO carrega runtime embutido — medido

```
$ tar -tzf teko-linux-x86_64-glibc.tar.gz
teko
```

Um único arquivo. `scripts/package_release.sh:67-79` monta o `$STAGE` do arquivo binário com
`$BIN_NAME` e nada mais. O runtime viaja num asset SEPARADO,
`teko-bootstrap-src.tar.gz` (`package_release.sh:95-126`, atrás de `EMIT_SRC_BUNDLE=1`).

Prova direta de que o seed morre sem runtime em disco — seed 0.3.0.30 num diretório neutro, com
`TK_RT_DIR` apontando para um diretório vazio:

```
emit C     out/rtprobe.c   ✓
cc         out/rtprobe.c:6:10: fatal error: teko_rt.h: No such file or directory
           cc1: fatal error: …/emptyrt/teko_rt.c: No such file or directory
           cc1: fatal error: …/assert/assert.c: No such file or directory
teko: .: cc failed to build the generated C          EXIT=1
```

### 4.2 Máquina de desenvolvedor: SEGURA

`install.sh:280-300` (`install_share_runtime`) baixa o `teko-bootstrap-src.tar.gz` do release e
extrai `runtime/`, `assert/` e `win32_compat.h` para `<prefix>/share/teko`. A sondagem
`ensure_rt_dir_abs` (`project.tks:2445`) tem esse diretório como camada 4
(`probe_share_rt_dir` → `share_rt_bases`). Medido neste container, que tem
`/usr/local/share/teko/runtime` populado por um `install.sh` anterior: o mesmo seed, no mesmo
diretório neutro, **sem** `TK_RT_DIR`, compila e linka com sucesso (`EXIT=0`).

### 4.3 CI: **NÃO SEGURA** — e é aqui que o expurgo quebra a escada

`scripts/ci_provision_teko.sh:243-271` é o ÚNICO provisionador de seed das lanes. Ele baixa
apenas `teko-<LABEL>.{tar.gz,zip}` e extrai em `<checkout>/.seed`. **Ele nunca baixa o
`teko-bootstrap-src.tar.gz`, e nada num runner do GitHub popula um `share/teko`.**

Logo, no CI a resolução do runtime cai na camada 1 de `ensure_rt_dir_abs`:
`<bindir>/..` = `dirname(<checkout>/.seed)` = a raiz do checkout → `<checkout>/src/runtime`.
**Hoje, o seed do CI lê o runtime do REPOSITÓRIO.** É exatamente por isso que
`scripts/build_with_seed_fallback.sh` precisa pinar `TK_RT_DIR` por estágio: sem pinar, "mistura
eras de runtime e o link falha".

Apagar os oito arquivos sem mais nada quebra `seed → gen1` em **todas** as lanes, no passo `cc`,
com o erro medido em 4.1.

### 4.4 O conserto, e ele não exige manter uma linha de C

Rota escolhida — **`ci_provision_teko.sh` passa a provisionar o runtime DA ERA DO SEED junto do
seed**: baixar o `teko-bootstrap-src.tar.gz` do MESMO tag, e extrair `runtime/`, `assert/` e
`win32_compat.h` para dentro de `.seed/`. A camada 2 da sondagem (`<bindir>` — "a flat
bundled-install layout") resolve, e resolve para a era CERTA por construção.

Três propriedades que a tornam a rota certa:

1. **Funciona com o release 0.3.0.30 JÁ PUBLICADO** — o asset existe em todo release
   (`release.yml:228`, `nightly.yml:478`), não é preciso cortar release novo.
2. **Não adiciona nenhum C ao repositório.** O C que o seed usa passa a vir do release do seed,
   que é de onde ele sempre deveria ter vindo.
3. **Torna o pin de `TK_RT_DIR` em `build_with_seed_fallback.sh` desnecessário para o estágio do
   SEED** — o seed passa a achar o seu próprio runtime, sem chance de misturar eras. Os degraus
   intermediários da escada continuam sendo COMMITS ANTIGOS do próprio repositório, que ainda
   carregam `src/runtime`, e continuam pinando o `rt_dir_of` da árvore que estão compilando.

Quando a `.32` cortar um seed que não emite C, essa provisão de runtime se auto-desabilita (o
asset deixa de existir) e o resíduo sai inteiro.

---

## 5. `__int128` no emissor — a capacidade que parecia se perder JÁ ESTÁ MORTA

Quatro usos de `__int128` em `src/codegen/codegen.tks:8418-8440`, todos no caminho de `flags`:

```
let uint_type = if n <= 8 { "uint8_t" } … else if n <= 64 { "uint64_t" } else { "unsigned __int128" }
```

O braço `else` é **inalcançável**. O checker rejeita antes, em dois pontos independentes:

- `src/checker/check_modules.tks:183` `check_flags_member_cap` — *"a flags type may declare at
  most 64 members (bit 63 is the last) — write this as two flags types instead"*;
- `src/checker/typer.tks:3082` — a mesma guarda, na resolução de `Type::Member`.

`src/checker/tast.tks:88` registra a razão: *"u64 cap — drop-128, 2026-07-24 — so a flags type
declares at most 64 members"*.

**Veredito: nenhuma capacidade desaparece com o emissor.** Os quatro usos são resíduo da era
pré-drop-128 preservado por um braço `else` que o checker proíbe. O backend nativo não precisa
ganhar nada, e nada precisa ser aposentado explicitamente — já foi, em 2026-07-24.

---

## 6. A virada no compilador — de `cc` para LINKER

### 6.1 O defeito que o ruling do owner conserta

`resolve_cc` (`project.tks:779`) devolve `"cc"` em todo host, e quem decide o que é `cc` é o

```
host_uname=MINGW64_NT-10.0-26200-ARM64 3.6.9-b4195d69.x86_64 x86_64
toolchain_cc=cc.exe (x86_64-posix-seh-rev2, Built by MinGW-Builds project) 14.2.0
```

Um MinGW **x86_64**, sob emulação x86 do Windows-on-ARM, numa máquina ARM64, produzindo um PE
**x86_64** rotulado ``. Ninguém escolheu: pediu-se `cc` e o PATH respondeu.

### 6.2 Requisitos da nova sondagem (M.3)

1. **Procura LINKER, não compilador C.** `build_cc_argv` hoje é compartilhado por `run_cc` (input
   `.c`) e `link_object` (input `.o`) *"so both link the SAME way"* (`project.tks:876-878`). As
   duas responsabilidades se separam: a de compilar C morre com `run_cc`; a de linkar objeto
   sobrevive e passa a montar linha de linker.
2. **Asserta a ARQUITETURA do linker, não só a existência.** Um linker que existe e mira a
   arquitetura errada é pior que nenhum: produz artefato errado e ninguém percebe.
3. **Nenhum default silencioso.** Sem linker nativo para o alvo, erro nomeado dizendo qual alvo se
   queria, o que se achou, e por que não serve. Nunca cair para o que estiver no PATH.
4. **MinGW é RECUSA EXPLÍCITA, nomeando o motivo** — não omissão. O erro tem que ensinar por que
   ele foi barrado, senão alguém o recoloca no PATH em seis meses e o defeito volta.
5. **A asserção de arquitetura vale também para o ARTEFATO PUBLICADO.** `scripts/produce_assets.sh:120`
   publica os assets `kind = native`
   asserção de arquitetura**, enquanto `scripts/native_linux_asset.sh:217` faz `file | grep ARCH_KW`
   nos seis alvos Linux. Foi essa assimetria que deixou o PE x86_64 ser publicado como ARM64.
   Fechar só a sondagem deixa a outra ponta aberta.

### 6.3 Inventário de linkers por host

Medido neste container (linux-x86_64, Ubuntu 24.04):

| linker | caminho | versão | alvo |
|---|---|---|---|
| `ld` (GNU BFD) | `/usr/bin/ld` | GNU Binutils for Ubuntu 2.42 | x86_64-linux-gnu |
| `ld.lld` / `lld` | `/usr/bin/ld.lld` | LLVM | multi-alvo |
| `ld.gold` / `gold` | `/usr/bin/ld.gold` | GNU gold | x86_64-linux-gnu |

macOS e Windows-x86_64 **não foram medidos** — este agente só tem host Linux.
Candidatos a verificar nos runners, não a assumir: no Windows, `lld-link` do LLVM (o LLVM está
instalado — `/c/Program Files/LLVM/bin/clang` aparece no diagnóstico de toolchain do runner ARM64
— e é cross-capaz por natureza), e `link.exe` da MSVC, que exige ambiente de desenvolvedor; no
macOS, o `ld` da Apple. **Essa tabela é entregável por si**: é ela que diz se o ruling é
exequível hoje ou se falta ferramenta em algum runner.

### 6.4 O que o ruling do MinGW SIMPLIFICA

Como não emitimos mais `.c`, não precisamos de gcc, clang **nem** MSVC como compiladores. A
discussão MinGW × MSVC × clang era sobre uma peça que deixa de existir. Sobra uma única pergunta
por host: **qual linker, e ele é nativo do alvo?**

---

## 7. Resíduo de resolução de runtime que fica órfão

Sem `teko_rt.{c,h}` no repositório, o caminho de resolução de runtime do COMPILADOR NOVO não tem
o que resolver. Sai junto (owner: "sem remanescentes ou sobressalentes"):

| símbolo | arquivo |
|---|---|
| `rt_dir()` | `src/build/project.tks:526` |
| `assert_dir()` | `src/build/project.tks:540` |
| `probe_rt_dir()` | `src/build/project.tks:2383` |
| `share_rt_bases()` | `src/build/project.tks:2404` |
| `probe_share_rt_dir()` | `src/build/project.tks:2413` |
| `ensure_rt_dir_abs()` | `src/build/project.tks:2445` |
| `home_dir()` | `src/build/project.tks:2396` (só existe para `share_rt_bases`) |
| `cc_family_is_clang()` | `src/build/project.tks:556` (família de COMPILADOR C) |
| `TK_RT_DIR` | o env override inteiro |
| `share_dir_for_prefix` / `install_share_runtime` | `install.sh:267,278` |
| `scripts/install_share_runtime_test.sh` | testa exatamente o que deixa de existir |

**Atenção:** o item de `install.sh` só pode sair DEPOIS que o CI parar de precisar do runtime do
seed (§4.4), e o de `ci_provision_teko.sh` é o oposto — ele PASSA a existir. Enquanto o seed
publicado emitir C, o `teko-bootstrap-src.tar.gz` do release do seed é o que alimenta a escada.

---

## 8. Triagem das lanes — linka × compila

27 scripts em `scripts/`. Dos 13 que nomeiam C:

**Morrem por assunto (o assunto é o runtime C):**
- `scripts/region_drop_subtree_test.sh` + `scripts/region_drop_subtree_test.c`
- `scripts/tk_arena_commit_test.sh` + `scripts/tk_arena_commit_test.c`
- `scripts/ar_link_run_consumer.c` (o consumidor C do `.a`; o teste que o usa passa a ter um
  consumidor Teko)
- `scripts/install_share_runtime_test.sh` (§7)
- `scripts/build_gen1_from_c.sh` — existe só para linkar um `teko.c` já emitido; sem emissão de C
  não há entrada

**Ficam (só linkam, ou usam C apenas para o SEED):**
- `scripts/build_with_seed_fallback.sh` — a escada; o C que ela toca é o do seed (§4.4)
- `scripts/ci_provision_teko.sh` — ganha a provisão do runtime do seed
- `scripts/package_release.sh` — enquanto publicar o bundle de bootstrap para o seed seguinte
- `scripts/produce_assets.sh` / `scripts/native_linux_asset.sh` — ganham a asserção de
  arquitetura simétrica (§6.2 item 5)
- `scripts/ci_cc_wrap.sh` — o shim de PATH; vira shim de LINKER ou sai com o sanitizador
- `scripts/ci_producer_matrix.sh`, `scripts/ci_full_mode.sh`, `scripts/ci_gate_coverage.sh` —
  mencionam C só em prosa/rótulo

**`codeql.yml`: NÃO deletar o job `c-cpp`.** O nome sai do ruleset de `main` ANTES do job, senão o
check requerido fica PENDENTE PARA SEMPRE e trava o dreno. O job fica sem entrada (não há mais C
para analisar) e isso precisa ir ao owner pelo integrador.

---

## 9. A SEQUÊNCIA — ruling do owner 2026-07-26 (a deleção é o TESTE, não a consequência)

> "O que deverá fazer é que no degrau que constrói o nativo, tem que remover os arquivos .c e .h,
> aí então gerar a versão que não depende mais de C."

Apagar os `.c`/`.h` ANTES de gerar o compilador nativo torna a ausência de C uma **pré-condição
verificada por construção**. Portar tudo e apagar depois prova muito menos: nada impede um
resquício de continuar sendo achado por alguma sondagem.

E a correção de ordem que o owner emitiu logo em seguida, porque a primeira redação juntava dois
passos que morrem em momentos DIFERENTES:

> "A sondagem tem que morrer antes de gerar a gen1, senão vai dar erro na gen2 mesmo assim,
> .30->gen1 (já sem sondagem)"

A sondagem vive no FONTE do compilador. Se ela ainda estiver lá quando o gen1 for gerado, **o gen1
nasce com a sondagem compilada dentro dele** — e é o gen1 que constrói o gen2. Com os arquivos já
apagados, ele procura, não acha, e o gen2 falha do mesmo jeito.

| passo | estado do FONTE | estado dos ARQUIVOS em disco |
|---|---|---|
| 1. mudar o fonte | **sondagem e emissão de C REMOVIDAS** | ainda presentes |
| 2. `seed .30 → gen1` | idem | **ainda presentes** — e é obrigatório |
| 3. apagar | idem | **removidos** |
| 4. `gen1 → gen2` nativo | idem | ausentes ← **o teste** |
| 5. `gen2 → gen3` nativo | idem | ausentes, byte-idêntico |

**Por que os arquivos precisam SOBREVIVER ao passo 2.** O seed .30 emite C e linka contra
`teko_rt.c` por comportamento próprio, já compilado dentro dele. Nenhuma mudança nossa no fonte
muda o que o seed faz. No momento em que o seed constrói o gen1 os arquivos têm que estar em
disco — não porque o fonte do gen1 precisa, mas porque o **seed** precisa. O gen1 que sai desse
passo já é um compilador sem sondagem e sem emissão de C; a partir dele os arquivos são inúteis, e
é aí que se apagam.

### 9.0 O PASSO 1 CRESCEU — refinamento do owner

> "quando .30 for compilar, já não devem existir os emissores ou quaisquer códigos tks dependentes
> de C, depois para gen2 tem que remover os arquivos"

O passo 1 não é "remover a sondagem": é remover o **EMISSOR** e **todo `.tks` dependente de C**. O
fonte que o seed .30 compila já não pode saber emitir C — o gen1 nasce, por construção, incapaz.

**A VARREDURA, medida.** Todo `.tks`/`.tkt` que toca C é este conjunto, e ele é pequeno:

| arquivo | linhas | veredito |
|---|---:|---|
| `src/codegen/codegen.tks` | 10.727 | **morre inteiro** — é o emissor |
| `src/codegen/ffi_export.tks` | 407 | **morre** — só existe para `emit_c_header` |
| `src/codegen/ffi_export_test.tkt` | — | morre com ele |
| `src/build/project.tks` | 4.446 | **sobrevive**, perde os membros de C |
| `src/build/project_test.tkt` | — | sobrevive, perde os testes de linha de `cc` |
| `src/checker/consteval.tks:989` | 1 ref | `tk_emit_c_mode` — inspecionar; é nome de modo, não emissão |
| `src/lir/lower.tks:5184` | 1 ref | `tk_emit_c` **só em doc-comment** — nada a deletar, texto a atualizar |

**A ARMADILHA de "deletar 10.727 linhas", e ela é real.** `project.tks` chama SETE entradas de
`codegen::`, e **duas delas não são emissão de C**:

| chamada | linha | o que é |
|---|---|---|
| `codegen::tk_emit_tsym` | 1366 | o mapa `.tsym` — **consumido pelo caminho NATIVO** (escreve `<bin>.tsym` depois de `finish_native_object`), é o que resolve o stack trace E4 |
| `codegen::check_ffi_export` | 1392 | uma REGRA DE CHECKER sobre `exp`/`abi="c"`, não um emissor |
| `codegen::emit_c_header` | 2528 | morre com `ffi_export.tks` |
| `codegen::tk_emit_c_test` | 2707 | o harness de teste — **pré-requisito da carga irmã** |
| `codegen::tk_emit_meta` | 2711 | morre |
| `codegen::tk_emit_c_test_analyze` | 3597 | morre com o harness |
| `codegen::tk_emit_c_cov` | 4310 | a TU de cobertura, morre com o harness |

`tk_emit_tsym` e `check_ffi_export` precisam ser **RELOCADOS antes** de `codegen.tks` morrer.
Apagar o arquivo inteiro leva junto o emissor do `.tsym` e a regra de FFI-export, e o sintoma —
stack trace nativo sem símbolos — aparece longe da causa.

**ACOPLAMENTO A SEQUENCIAR:** `codegen::tk_emit_c_test` / `tk_emit_c_test_analyze` /
`tk_emit_c_cov` são o harness de teste, e a carga irmã `cargo/20-gate-sem-c` está portando
`run_native_gate` para fora do emissor. **O trabalho dela é PRÉ-REQUISITO deste passo 1**, não
paralelo: deletar o harness antes de o gate ter caminho nativo deixa o projeto sem gate no meio da
maior deleção da versão. Se o porte dela não fechou, faz-se todo o resto do passo 1 e o harness
fica por último.

**O que SOBREVIVE, para não deletar demais:** o **linker**. `run_cc`/`build_cc_argv` sobrevivem na
função de LINKAR objeto nativo em binário; o que morre é COMPILAR C. São duas responsabilidades no
mesmo corpo (`project.tks:876-878` — "Shared verbatim by `run_cc` … and `link_object`"), e a
entrega é separá-las, não apagar o arquivo.

**O que "remover a sondagem" inclui**, para não sobrar meio caminho: `TK_RT_DIR` (leitura E
escrita), `ensure_rt_dir_abs`, `probe_rt_dir`, `share_rt_bases`, `probe_share_rt_dir`, e todo call
site que dependia de `TK_RT_DIR` estar setado — inclusive o que monta a linha de `cc` com
`teko_rt.c` (`project.tks:923-924`). Uma sondagem que sempre falha é um caminho morto dentro do
gen1 que ainda pode disparar erro.

### 9.1 O CRITÉRIO DE PRONTO — o hello world sozinho

Projeto isolado, só o binário, nenhum `.c`/`.h` em lugar nenhum, `TK_RT_DIR` desarmado,
`share/teko` inexistente. Tem que sair `hello, teko` e exit 0. Não é a suíte, não é o gate.

**Baseline medido HOJE com o seed 0.3.0.30** (`/usr/local/share/teko` neutralizado com restauração
garantida por `trap`, porque esta máquina TEM o share instalado e medi-lo seria medir a instalação
e não o binário):

```
emit C     bin/hello.c   ✓
cc         bin/hello.c:6:10: fatal error: teko_rt.h: No such file or directory
           cc1: fatal error: src/runtime/teko_rt.c: No such file or directory
           cc1: fatal error: src/assert/assert.c: No such file or directory
           ✗  cc failed to build the generated C
BUILD_EXIT=1     FECHO: NAO
```

Um hello world hoje depende de **três** coisas externas ao executável: as fontes do runtime em C
estagiadas ao lado, um `cc` instalado, e os headers de sistema desse `cc`. É esse estado que o
passo 3 tem que tornar impossível. O critério exige também que **nenhum `bin/hello.c` seja
emitido** — construir via C num diretório que por acaso tem o share instalado passa no exit code
e falha no propósito.

### 9.2 O que bloqueia o passo 3 HOJE — com evidência, para ser resolvido e não contornado

| # | bloqueio | evidência |
|---|---|---|
| B1 | o link nativo ainda COMPILA C: `link_object` reusa `build_cc_argv`, que empurra `<rt>/teko_rt.c` e `<asrt>/assert.c` para a linha | `project.tks:923-924`, e `link_object` "Reuses `build_cc_argv` verbatim" (`project.tks:1276`) |
| B2 | `teko_rt.tks` ainda é circular: `print`/`write` chamam `teko::io::write`, que o lowering resolve para `tk_write` — o próprio símbolo do runtime C | `runtime/teko_rt.tks:44-80`, `lir/lower.tks:1099` |
| B3 | o gate de teste emite uma unidade de tradução C e a compila com `run_cc` | `project.tks:2681` |
| B4 | o arena não tem original em Teko — é projeto, não tradução | §3 |

B1 e B2 são a substância do passo 2→3 e são **desta carga**. B3 é da carga irmã
`cargo/20-gate-sem-c`; enquanto o gate de teste emitir C, o emissor não pode morrer. B4 é o item
que o owner separou explicitamente.

### 9.3 Fatias

| # | fatia | estado |
|---|---|---|
| 1 | este mapa | **entregue** |
| 2 | `ci_provision_teko.sh` provisiona o runtime da era do seed (§4.4) — o passo 1 da sequência deixa de depender do repositório | **entregue** |
| 3a | asserção de arquitetura no asset publicado (`produce_assets.sh`, §6.2 item 5) | **entregue** |
| 3b | `src/build/linker.tks` — a busca por LINKER, asserção de arquitetura, recusa nomeada de MinGW (§6) | **entregue** |
| 4 | B2: `teko_rt.tks` fecha o bottom com `extern fn` (write/exit/abort) + o arena vai para Teko | pendente |
| 5 | B1: `build_cc_argv` se parte — a de compilar C morre, a de linkar objeto vira linha de linker | pendente |
| 6 | passo 1 do ruling: REMOVER DO FONTE a sondagem inteira e a emissão de C (§7) | pendente |
| 7 | passo 3: apagar os oito arquivos, DEPOIS do gen1 | pendente |
| 8 | passos 4 e 5: gen2 e gen3 nativos com os arquivos ausentes + o hello world sozinho (§9.1) | pendente |
| 9 | as lanes (§8) | pendente |

**O que a fatia 2 vale depois da correção de ordem — honestamente, menos do que eu escrevi
primeiro.** Ela NÃO é mais o que salva a escada: com a ordem corrigida, os `.c`/`.h` do
repositório estão em disco exatamente quando o seed precisa deles, e a camada 1 da sondagem os
acha como sempre. O que a fatia 2 continua entregando é higiene de ERA e independência do
checkout: o seed passa a carregar o runtime do PRÓPRIO release em vez de ler o do repositório que
está sendo testado (§4.1-4.3, medido), o que é a razão de `build_with_seed_fallback.sh` ter de
pinar `TK_RT_DIR` por estágio. Ela também é o que mantém o passo 2 possível no dia em que alguém
apagar os arquivos antes da hora. Vale, mas como higiene — não como pré-requisito.
