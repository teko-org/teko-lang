# TEKO_ROADMAP_BINARY — caminho até o PRIMEIRO binário executável

> Sucede o roadmap E7 (front-end + checker + emissores `.tkb`/`.tkh`, concluídos).
> **Alvo:** o bootstrap `teko` (o espelho **C** compilado pelo `cc` do host) compila um
> `main.tks` mínimo e produz um **binário nativo que roda**.
> **Backend — ⚠️ REVOGADO pelo legislador (2026-06-24):** o **transpile-para-C** deixa de ser
> a arquitetura-destino. Teko terá seu **PRÓPRIO backend nativo** (codegen nativo direto,
> realizando o **estágio-2 (AOT-nativo)** da Constituição sem o `cc` como intermediário).
> **Sequenciamento (ordem do legislador):** *concluir TODO o trabalho atual ANTES* de construir o
> backend nativo. O transpile-para-C é **revogado como backend PRIMÁRIO/de release, porém RETIDO —
> mantido equalizado — como FALLBACK permanente e COMPARATIVO de corretude diferencial** ("we need to
> keep a fallback and comparative"). Logo **três** caminhos devem concordar: a **VM `.tkb`**, o
> **transpile-para-C/`cc`** (fallback+comparativo) e o futuro **backend nativo** (primário). **Cada onda
> (W4/W5/…) entra em TODOS os caminhos ativos — o codegen NÃO é congelado.**
> *(↺ supersede a decisão original transpile-para-C — HISTORY §B.34/§B.35; ver LEGISLATION "Backend nativo próprio".)*
>
> _(histórico, superado)_ ~~**Backend (decidido pelo legislador):** transpile-para-C — o codegen baixa a
> árvore tipada para C, e o `cc` do host compila → binário. Reusa a toolchain (M.5).~~
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
| Bm1c | Alvo **`teko`** (esqueleto standalone em `src/main.c`, **sem** depender do espelho) **compila + linka + roda** (exit 0) | ✓ |

**Marco F-1:** `cmake --build build --target teko && ./build/teko` funciona. O baseline está
de pé; F0 passa a crescer o espelho C sobre uma infra comprovada. (`build/` no `.gitignore`.)

## Fase 0 — `teko` compila & linka (tornar o espelho C construível)

| # | Entrega | Lei | Esf. |
|---|---|---|---|
| B0a ✓ | **Lexer C completo** — **feito**: `token.h` completo (enum `tk_token_kind` 1:1 com Teko, 76 kinds, ordinais preservados), `lexer.h`/`lexer.c` (scanner fiel a `lexer.tks`: skip/number/ident/str/byte/symbol maximal-munch + `tk_lit_int`/`tk_lit_byte`). `-fsyntax-only` limpo; em CMake (`TEKO_LEXER_SOURCES`) | M.4 | M |
| B0b ✓ | **Headers C self-contained** — **feito**: `parser/ast.h`/`result.h` viraram headers de TIPO reais (guard+includes, sem corpos); `scope.h` inclui o AST (`tk_bind_kind`); `tkb_read.h` path corrigido. Colisão `tk_type_expr` (TYPE vs FUNÇÃO) resolvida → função renomeada `tk_typer_expr` (decisão do legislador). Bugs latentes: `resolve_named` exposto, `<string.h>` em typer/match, deref `*c.rest[i].operand` | M.4 | M |
| B0c-1 ✓ | **AST/headers do parser** — **feito** (acima): definem `tk_expr`/`TK_EXPR_*`/`tk_pattern`/`tk_statement`/`tk_item`/… ; os 12 helpers de append + box + predicados de cursor declarados | M.4 | M |
| B0c-2 ✓ | **Corpos do parser** — **feito**: `parser.c` com todos os `parse_*`/cursor/optokens + 12 helpers de append + box, transcritos de `parser/*.tks`. `-fsyntax-only` limpo; em CMake (`TEKO_PARSER_SOURCES`). Sem stubs/tensões | M.4 | M |
| B0d | **Runtime/prelude C** — `core.h` ok; promover `tk_str_eq` (hoje `static` duplicado em type.c/typer.c) p/ `text.h` — *cleanup M.5, NÃO bloqueia* (pendente) | M.5 | P |
| B0e ✓ | **CMake linka tudo** — **feito**: `parser.c` na lib; `EXCLUDE_FROM_ALL` removido; `cmake --build` (padrão) arquiva `libteko_bootstrap.a` + builda `teko` — verde | M.4 | P |

> **F0 CONCLUÍDO ✅:** **17/17 `.c` do espelho compilam limpos**; `cmake --build` (padrão) produz
> **`libteko_bootstrap.a`** (front-end + checker + emit inteiros) **+ `teko`** (smoke). Único resíduo: **B0d**
> (`tk_str_eq` duplicado — cleanup M.5, não bloqueia). A lib ainda não é linkada num executável — isso é a
> **F1** (conectar o driver: read→lex→parse→check, e linkar `teko` contra a lib).

**Marco F0:** `cmake --build` produz `teko`; `./teko` roda (sem fazer nada ainda).

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
| B1a ✓ | **IO** — **feito**: `tk_read_file` (host `fopen`/`fread` → `tk_str_from_utf8`, a única IO contida do bootstrap, M.1) em `src/driver.c` | M.1 | P |
| B1b | **`.tkp` reader** — parser TOML → `Artifact`/`source`/`[dependencies]`; fiar `check_main_file_rule`. **PROMOVIDO** ao `TEKO_ROADMAP_INDEPENDENCE.md` Eixo A (compilação por projeto): o basename-heurístico do driver é falha conceitual, corrigida lá. | M.3 | M |
| B1c ✓ | **R-main** — **feito**: `tk_main_file_to_program`/`tk_module_to_program` achatam `MainFile`/`Module` em `tk_program` de `tk_item` (uses→USE, body→STATEMENT, decls→FUNCTION/TYPE_DECL). Sem tensão (flatten fiel) | M.4 | M |
| B1d ✓ | **Driver** — **feito**: `tk_compile(path)` = read→lex→parse→reconcile→`tk_type_program`; `main()` mínimo (`teko <file>`); erros→stderr+exit 1, OK→stdout+exit 0. `teko` linkado contra `libteko_bootstrap.a` | M.4 | M |

> **F1 CONCLUÍDO ✅:** `teko` é um **front-end real** — `read→lex→parse→reconcile→check` end-to-end, verificado:
> `let x: u32 = 1` / `200 to u8` → `checked OK`; `let y: u8 = 999` (fora de faixa, M.1) e erro de parse →
> reportados com mensagem + exit 1; `main.tks` vazio → OK (0 items). *(`.tkp` reader = B1b, deferido.)*

## Fase 2 — Backend: transpile-para-C (TC)

| # | Entrega | Lei | Esf. |
|---|---|---|---|
| B2a ✓ | **Emissor C** — **feito**: `src/codegen/codegen_c.{c,h}` `tk_emit_c(tk_tprogram)`; `Type`→C (`u8..u64`/`i8..i64`→`stdint`, `bool`, byte→`uint8_t`, Unit→`void`). str/named/slice/variant → erro honesto "not yet supported" | M.0 | M |
| B2b ✓ | **Baixar expressões** — **feito**: Number/Var/Binary/Unary/Compare/Cast/Call/FieldAccess. (sign-check + cast-pânico = refinamento F3; if/match-como-valor diferido) | M.0 | M |
| B2c ✓ | **Baixar statements** — **feito**: Binding(simple)/Assign/Return/ExprStmt/Loop/Break/Continue. (if/match-stmt, destructure → diferidos) | M.0 | M |
| B2d ✓ | **Funções + programa** — **feito**: `tk_tfunction`→função C (sem params, por ora); virtual-main→`main` C, **`return n`=exit n / default 0** (early exit, decisão do legislador) | M.0 | P |
| B2e ✓ | **Chamar o `cc`** — **feito**: driver emite `<stem>.c` → `cc -std=c23 … -o <stem>` → binário nativo | M.5 | P |

> **Marco F2 ✅ + correção:** `tk_type_program` agora fia o env entre statements top-level (espelha `type_block`) —
> o virtual-main compartilha escopo (bug F1 descoberto na verificação do M0).

## Fase 3 — Runtime mínimo de execução (`libteko_rt` em C)

| # | Entrega | Lei | Esf. |
|---|---|---|---|
| B3a ◑ | **Runtime de valor** — **str feito**: `tk_str` (view `[]byte` UTF-8, B.36) em `runtime/teko_rt.h`, literais baixados com **comprimento explícito** (NUL-safe, não `strlen` — M.1); inteiros/bool já nativos (stdint). `list` (`TK_LIST`) **diferido** (não bloqueia o 1º programa com saída) | M.0 | P |
| B3b ◑ | **Os pânicos** — **superfície criada** em `runtime/teko_rt.{h,c}`: `tk_panic` (→ stderr + abort não-zero) + `tk_panic_div0`/`oob`/`cast`/`overflow` (`_Noreturn`). **Cablagem no codegen pendente** (guards de ÷0 / conversão-impossível / overflow-debug que o C gerado chama) — próximo incremento | M.1 | M |
| B3c ✓ | **IO** — **feito**: `print`/`println : (str) -> Unit` injetados (stdlib não-importada, não-sombreável — `tk_builtin_fn` espelha `tk_builtin_type`); codegen mapeia `print`/`println` → `tk_print`/`tk_println` (`fwrite` byte-exato, sem assumir NUL); driver passa `-I$TK_RT_DIR` + compila `teko_rt.c` no mesmo `cc` (M.5) | M.1 | P |
| B3d ✓ | **Entry + saída** — virtual-main → `main` C; exit codes (early-exit) **+ saída real via `print`**; pânico → `abort`/stderr com mensagem (`tk_panic`) | M.1 | P |

> **MARCO ZERO (M0) ✅ ALCANÇADO no F2** — via **exit-code** (não precisou de runtime): `main.tks` de aritmética
> inteira → `teko` → C → `cc` → binário nativo que **roda** (`return n`=exit n, default 0). Verificado:
> `1+2`→3, `6*7`→42, `(1<<4)|2`→18, fall-through→0; nó não-suportado → erro honesto.
>
> **F3 = M1 (próximo nível — runtime de verdade):** o que destrava programas além de aritmética-com-exit-code —
> **B3c `print`/IO** (saída real), **B3b pânicos** (÷0/OOB/cast-impossível/overflow), **B3a runtime de valor**
> (`str`/`list`) e **funções com params** + str/byte literais no codegen. Cada um habilita mais do escopo M1.
>
> **F3 — incremento 1 ✅ (saída observável de verdade):** `print("hello, teko\n")` → `teko` → C+`teko_rt.c` → `cc` →
> binário que **imprime** e sai 0. Entregue: `runtime/teko_rt.{h,c}` (str/IO + superfície de pânicos), typer injeta
> `print`/`println:(str)->Unit`, codegen baixa literais `str` (escaping + len explícito) / `byte` e mapeia as chamadas
> built-in, driver+CMake fiam o runtime no `cc`. Verificado: hello-world imprime; `let s: str` + `println(s)`; em-dash
> UTF-8 → octal byte-exato; `print(42)` → erro honesto de tipo (M.3); aritmética-exit-code sem regressão.
> **Incremento 2 (próximo):** cablar **B3b** no codegen — guard de **conversão-impossível** (`x to T` runtime → `tk_panic_cast`),
> **÷0** (`tk_panic_div0`), **overflow** (panic-debug); depois **funções com params** + `list`.

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
