# Memória: arquitetura do LSP da Teko (0.3.1.0)

**Data:** 2026-08-02. **Ramo:** `cargo/0.3.1.0-lsp-arquitetura` (de `origin/fix/union`).
**Papel:** ARQUITETO (só leu código; escreveu design). Plano completo em
`docs/design/lsp-teko.md`.

## Decisão central
Servidor LSP em Teko puro em `src/lsp/` (namespace `teko::lsp`), subcomando `teko lsp` em
`main.tks` (espelha a arma `fmt`, `main.tks:61-63`), JSON-RPC 2.0 sobre stdio, chamando
**in-process** o front-end já-biblioteca: `teko::lexer::tokenize`,
`teko::parser::parse_module`/`parse_main_file`, `teko::checker`, `teko::fmt::format_source`.
Alinha com a régua `teko lsp` já registrada (`TEKO_ROADMAP_DEVTOOLS.md:94`).

## Achados confirmados (arquivo:linha)
- doc-comments **preservados no AST**: `parser/ast.tks:407-408` (Function), `:481` (Field),
  `:556-557` (TypeDecl), `:631` (ConstDecl) — hover não precisa mexer no parser.
- diags do checker já `file:line:col: msg` via `checker/diagnostics.tks:37` `located_diag`;
  seam estruturado `pub` `type_program_with_deps_pre_mono` → `PreMono.diags: []str`
  (`typer.tks:6657`), coletados em `pre_walk` (`typer.tks:6797`).
- `fmt::format_source(src): str|error` puro (`fmt.tks:649`) — formatação in-process.
- json `decode`/`encode` `pub` (`encoding/json/json.tks:463`/`:476`).
- `Token` posiciona tudo: `line`/`col`/`text` (`lexer/token.tks:184-189`).

## Alarme PROVADO (§4 do design)
Transporte stdin: `read_line` (`io.tks:26`) para no `\n` e sobre-lê o corpo JSON-RPC (que não
tem newline terminal); `read_stdin` (`io.tks:37`) bloqueia até EOF (impossível p/ servidor de
vida longa). Não há leitura byte-exata. **Exige** primitiva nova `read_stdin_n(n):str` em
`teko_rt.{c,h}` (exceção C mantida) + extern em io.tks + lift no codegen Teko — mesmo padrão de
`read_line`/`read_stdin`. Crumb 0, sequenciado primeiro (seed precisa carregá-la antes de
`src/lsp/` usá-la).

## Lacunas nomeadas = crumbs
1. **collect_diagnostics(program):[]str** `pub` no checker (invólucro do pre_walk). §5.1.
2. **Warnings escapam pelo stderr** (`typer.tks:2008`, `initanalysis.tks:251`) — coletar em
   vez de imprimir (crumb C).
3. **Diagnostic estruturado** (`{file;line;col;end_line;end_col;severity;message}`) — crumb D.
4. **Índice de símbolos** `build_symbol_index(parser::Program):[]Symbol` (nome/pos/doc/kind)
   — motor de hover/def/completion; deriva só do parse. `src/lsp/symbols.tks` (crumb E).
5. **assemble_with_overrides** (buffer não-salvo; `assemble` só lê disco em `assemble.tks:228`)
   — aditivo (crumb E).
6. **read_stdin_n** (crumb 0).

## Ordem de crumbs (valor cedo)
0 primitiva stdin · A diagnósticos ao vivo · B formatação in-process · C warnings coletados ·
E índice+overrides · F hover+go-to-def · G completion · H cores TextMate (paralelo) ·
cliente VS Code · D Diagnostic estruturado · I semantic tokens (fase 2).

## Mapeamento capacidade→fonte
diagnósticos←checker(located_diag); hover←doc-comment do índice; completion←índice+keywords;
go-to-def←índice(line/col do nome); formatação←`fmt::format_source` (in-process);
cores: TextMate(`grammar-spec.json`/`tm_gen.tks`) sem LSP + semantic tokens com LSP (fase 2).

## Incremental
v1: parse por-tecla do buffer único (rápido) + check completo debounced no save
(`assemble_with_overrides`+`collect_diagnostics`). type_program re-tipa tudo (`typer.tks:6814`)
— memoização é fase 2 nomeada, não v1.

## Tensão de lei resolvida (não HALT)
Cliente VS Code em TS/JS é glue de editor (estatuto de `tooling/` `.vim`/`.el`/`.nanorc`), não
produto — Teko-only intacto. Nenhuma tensão genuína não-resolvida.

## Fixtures
`.tkt` sobre puras (framing encode/decode incl. o caso que read_line erraria; build_symbol_index;
dispatch initialize; collect_diagnostics com 1 erro+1 warning). Smoke `.tkr` nativo: exit 0
(shutdown→exit), 1 (exit sem shutdown / EOF prematuro), 2 (arg inválido).
