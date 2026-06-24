# TEKO_ROADMAP_BINARY — caminho até o PRIMEIRO binário executável

> Sucede o TEKO_ROADMAP_E7_V2 (front-end + checker + emissores `.tkb`/`.tkh`, concluídos).
> **Alvo:** o bootstrap `tekoc` (o espelho **C** compilado pelo `cc` do host) compila um
> `main.tks` mínimo e produz um **binário nativo que roda**.
> **Backend (decidido pelo legislador):** **transpile-para-C** — o codegen baixa a árvore
> tipada para **C**, e o `cc` do host compila → binário. Reusa a toolchain (M.5); é a forma
> de realizar o **estágio-2 (AOT-nativo)** sem escrever codegen nativo.
> **Dois modos de execução são planejados:** (1) **transpile-para-C / AOT** — este, o primeiro;
> (2) **VM / interpretador do `.tkb`** (o estágio-1 da doutrina) — **também virá, é futuro**, só
> não no primeiro binário. A VM não foi descartada; está adiada.
> Ordem = **M.4** (cada fase repousa na anterior). `src/` é canônico para código.

## Escopo do PRIMEIRO binário (mínimo — M.4/M.5)
Um `main.tks` com **aritmética inteira + um `print`**, rodando end-to-end. É o menor laço
que prova a pipeline inteira (read→lex→parse→check→emit-C→cc→exec). Tudo além entra depois.

---

## Fase -1 — baseline de build (a infra funciona?) ✓

Antes de fazer o espelho C (incompleto) compilar, garantir que **CMake + o `cc` do host
compilam/linkam/rodam** um alvo trivial — separando "a infra de build funciona" de "nosso
código compila" (F0).

| # | Entrega | Status |
|---|---|---|
| Bm1a | Toolchain confirmada: **cmake 4.3.3**, **Apple clang 21**, **`-std=c23` aceito** | ✓ |
| Bm1b | `cmake -B build` **configura limpo** — valida o `CMakeLists.txt` inteiro (incl. a lib-espelho `EXCLUDE_FROM_ALL`) | ✓ |
| Bm1c | Alvo **`tekoc`** (esqueleto standalone em `src/main.c`, **sem** depender do espelho) **compila + linka + roda** (exit 0) | ✓ |

**Marco F-1:** `cmake --build build --target tekoc && ./build/tekoc` funciona. O baseline está
de pé; F0 passa a crescer o espelho C sobre uma infra comprovada. (`build/` no `.gitignore`.)

## Fase 0 — `tekoc` compila & linka (tornar o espelho C construível)

| # | Entrega | Lei | Esf. |
|---|---|---|---|
| B0a ✓ | **Lexer C completo** — **feito**: `token.h` completo (enum `tk_token_kind` 1:1 com Teko, 76 kinds, ordinais preservados), `lexer.h`/`lexer.c` (scanner fiel a `lexer.tks`: skip/number/ident/str/byte/symbol maximal-munch + `tk_lit_int`/`tk_lit_byte`). `-fsyntax-only` limpo; em CMake (`TEKO_LEXER_SOURCES`) | M.4 | M |
| B0b | **Headers C self-contained** — auditar TU a TU; consertar ordem de include + defs faltantes (`tk_token`, `tk_expr`, `tk_pattern`, `tk_bind_kind`, AST do parser) para cada `.c` compilar isolado | M.4 | M |
| B0c | **Parser C completo** — verificar/materializar todos os `parse_*` em C (não só os headers consolidados) | M.4 | M |
| B0d | **Runtime/prelude C** — `core.h` concreto: `tk_str`, `TK_LIST`/`tk_list`, `tk_error`, `panic`/`abort`, igualdade/`tk_str_eq`; compilável e completo | M.5 | M |
| B0e | **CMake linka** — `tekoc` (alvo executável, `main` no-op por ora); remover `EXCLUDE_FROM_ALL`; `cmake --build` verde no host | M.4 | P |

**Marco F0:** `cmake --build` produz `tekoc`; `./tekoc` roda (sem fazer nada ainda).

### Resultado da auditoria de compilação (clang `-fsyntax-only -I src`)
**Já limpos (prontos p/ F0):** `text.c`, `type.c`, `tkb_buf.c`, `main.c`. `tast.h` (AST **tipada**) está
completo — só herda erros de `scope.h`/`ast.h` a montante. **Funil estreito:** todos os 10 que falham
morrem em 2 raízes — `scope.h:19` usa `tk_bind_kind` sem incluir; `parser/ast.h` referencia `tk_expr`
sem definir. **Realização:** o **lexer e o parser em C nunca foram materializados inteiros** (só
delta/snippets) — F0 é **transcrever Teko→C**, não só consertar includes. Lista exata:
- **B0a (lexer C — não existe):** `tk_token`, `tk_token_kind` (enum base inteiro; hoje só `TO`/`DOT` delta),
  `tk_keyword_kind`/`tk_read_symbol` + scanner de número/string/byte/comentário, `tk_lit_int`/`tk_lit_byte`,
  e os predicados de cursor (`tk_has_token`, `tk_is_kind_at`, `tk_skip_seps`, `tk_is_unary/additive/...`).
- **B0b (headers não self-contained):** `scope.h` não puxa quem define `tk_bind_kind` (1ª falha em 10/11);
  `emit/tkb_read.h:4` inclui `"tast.h"` com caminho errado (→ `"../checker/tast.h"`); `parser/ast.h`/`result.h`
  **não são headers** (sem `#include`, sem include-guard — snippets com corpos `static` colados).
- **B0c (parser C incompleto):** definir `tk_expr` + enum `TK_EXPR_*` + `as`-union, `tk_cmp_term`, `tk_use_decl`;
  os 12 helpers de append (`tk_exprs_push`/`tk_stmts_push`/…), `tk_box_expr`/`tk_box_type`, e corpos faltantes
  (`tk_parse_type_primary`, `parse_shift`/`parse_multiplicative`).
- **B0d (prelúdio):** `core.h` quase completo (`tk_error`/`TK_RESULT`/`TK_LIST` ok); promover `tk_str_eq`
  (hoje `static` duplicado em type.c/typer.c) para `text.h`; `panic` wrapper (menor).

**Ordem (linchpin):** os checker/emit `.c` já tipam contra `tast.h` (completo) — caem sozinhos quando o
front-end fechar. Sequência: **B0b trivial (scope.h `tk_bind_kind` + tkb_read.h path) → B0a lexer → B0c
ast.h/result.h como headers reais + `tk_expr`/helpers/corpos → B0d `tk_str_eq`**.

## Fase 1 — Conectar a pipeline (read→lex→parse→check)

| # | Entrega | Lei | Esf. |
|---|---|---|---|
| B1a | **IO** — ler um arquivo do disco → `str` (a fronteira **insegura** do host; o único `unsafe`/FFI do bootstrap) | M.1 (contida) | P |
| B1b | **`.tkp` reader** — parser TOML → `Artifact`/`source`/`[dependencies]`; fiar `check_main_file_rule` (executável exige `main.tks`) | M.3 | M |
| B1c | **R-main** — reconciliar `MainFile`/`Module` (parser) ↔ o driver do checker (`Program`/`Item`): montar o *virtual-main* e checar o AST real (fecha o gap nomeado) | M.4 | M |
| B1d | **Driver** — `compile(path)` = read→lex→parse→check; `main()` C fia tudo (erros → stderr + exit code) | M.4 | M |

**Marco F1:** `tekoc main.tks` lexa+parseia+checa um arquivo trivial e reporta erros (ainda sem saída executável).

## Fase 2 — Backend: transpile-para-C (TC)

| # | Entrega | Lei | Esf. |
|---|---|---|---|
| B2a | **Emissor C (esqueleto)** — `TProgram` → texto C (um writer `tkc_*`); baixar `Type`→tipo C (`u8..u64`/`i8..i64`→`stdint`, `bool`, `byte`, `str`→struct do runtime) | M.0 | M |
| B2b | **Baixar expressões** — `TExpr`→C: literais, var, binário/unário/comparação (sign-check B.22 + overflow **panic-debug/wrap-release**), call, **cast (pânico de runtime na impossibilidade)**, field-access | M.0+M.1 | M |
| B2c | **Baixar statements/blocos** — `TStatement`→C: binding, assign (`mut`), return, loop/break/continue, exprstmt, `if`, `match` (discriminação → `switch`/cadeia; exaustividade já checada) | M.0 | M |
| B2d | **Baixar funções + programa** — `TFunction`→função C; o *virtual-main*→`main` C | M.0 | P |
| B2e | **Chamar o `cc` do host** — o driver invoca o `cc` sobre o C emitido → binário nativo (M.5 — reusa a toolchain) | M.5 | P |

**Marco F2:** `tekoc main.tks` emite C; `cc` compila; `./a.out` existe.

## Fase 3 — Runtime mínimo de execução (`libteko_rt` em C)

| # | Entrega | Lei | Esf. |
|---|---|---|---|
| B3a | **Runtime de valor** — inteiros (stdint), `str` (`[]byte` UTF-8, B.36), `list` (`TK_LIST`), `bool` | M.0 | P |
| B3b | **Os pânicos** — ÷0, OOB de array, **conversão impossível** (a guarda de cast em runtime), overflow (panic-debug) — como chamadas/macros que o C gerado usa | M.1 | M |
| B3c | **IO** — `print`/`println` mínimo (→ `write`/`fputs`); a superfície IO do prelúdio | M.1 | P |
| B3d | **Entry + saída** — virtual-main → `main` C; exit codes; pânico → `abort`/trap com mensagem | M.1 | P |

**Marco F3 = MARCO ZERO (M0):** `main.tks` (aritmética inteira + `print`) → `tekoc` → C → `cc` → `./a.out` imprime o resultado; um pânico sai **alto**.

---

## Fora de escopo do primeiro binário (diferido, nomeado)
**float** · **métodos/`MethodCall`** · genéricos · coleções além de `list` · concorrência · crypto ·
**pacotes `.tkh` + pré-linker estático** (multi-arquivo/deps — programa é single-file por ora) ·
**self-hosting** (o compilador `.tks` compilando a si) · **codegen nativo puro** (LLVM/asm — estágio-2
"puro") · codec `.tkb` de statement (→ `if`/`match`) · análise de divergência (C5) · reconciliação
geral newtype↔base · `str↔bytes` transcodificação.

> **Modo VM (planejado, futuro).** O segundo modo de execução — a **VM/interpretador do `.tkb`**
> (estágio-1) — entra **depois** do M0. Pré-requisito real dele: o **codec `.tkb` de statement/programa**
> (hoje o codec só serializa `TExpr`; `if`/`match`/funções/programa faltam). Não está descartado — é o
> caminho do dev-loop rápido e do bootstrap interpretado; só não bloqueia o primeiro binário.

## Caminho crítico
**F0 (compila) → F1 (pipeline) → F2 (emite C + cc) → F3 (runtime) = M0.**
F3 pode correr em paralelo parcial com F2 (o runtime que o C gerado chama). F1b (`.tkp`) pode
adiar para depois do M0 se o primeiro alvo for um `main.tks` passado direto na linha de comando.
