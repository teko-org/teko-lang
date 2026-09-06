# Design: o LSP da Teko — reusando o front-end do compilador

> **Papel deste documento:** PLANO de arquitetura. Escrito pelo ARQUITETO (só lê código,
> escreve design). Nenhuma linha de código de produto é implementada aqui.
> **Ramo:** `cargo/0.3.1.0-lsp-arquitetura` (a partir de `origin/fix/union`).
> **Data:** 2026-08-02.
> **Lei aplicável:** Teko-only (novo trabalho em `.tks`); W15 full-Javadoc em toda
> declaração; `src/runtime/teko_rt.{c,h}` é a ÚNICA C mantida (exceção); os gêmeos C
> (checker/codegen/build) estão CONGELADOS. `bootstrap/teko.c` é SAÍDA, nunca entrada.

---

## 0. Resumo executivo (a proposta, não contra-argumentos)

**Caminho proposto, em uma frase:** um servidor LSP escrito em Teko puro em `src/lsp/`
(namespace `teko::lsp`), exposto como o subcomando `teko lsp` em `main.tks`, que fala
JSON-RPC 2.0 sobre stdio e chama **in-process** as funções de biblioteca que o compilador
já expõe — `teko::lexer::tokenize`, `teko::parser::parse_module`/`parse_main_file`,
`teko::checker` e `teko::fmt::format_source`. Zero IPC, zero re-parse de strings de erro
para o caso feliz, reuso direto.

Isto está **alinhado com a lei existente**: o `TEKO_ROADMAP_DEVTOOLS.md:94` já registra a
"régua `teko lsp`" — todos os devtools são subcomandos nativos do compilador, não binários
externos. O `fmt` é o precedente vivo (`teko fmt` reusa `teko::fmt`, `main.tks:61-63`).

**A entrega de valor mais cedo** é diagnósticos ao vivo (warnings/errors) — crumb A. Ela
não depende de nenhuma API nova de compilador além de um invólucro `pub` fino.

**O único alarme provado** (§4) é o transporte de stdin: o runtime hoje só oferece leitura
por-linha (`read_line`) ou slurp-total (`read_stdin`), e **nenhuma** delas consegue ler
exatamente `Content-Length` bytes de um corpo JSON-RPC sem terminador de linha. Isto exige
UMA primitiva nova de runtime (leitura de N bytes / de 1 byte), que cai dentro da exceção
`teko_rt.{c,h}` mantida e segue o padrão já estabelecido por `read_line`/`read_stdin`
(`src/io/io.tks:20-37`). É um crumb sequenciado primeiro, não um bloqueio do design.

---

## 1. Confirmação e aprofundamento dos achados do dono (arquivo:linha)

| Achado do dono | Confirmado? | Evidência |
|---|---|---|
| Extensão é só realce (TextMate gerado por `main.tks`→`generate`) | **Sim** | `tooling/vscode/package.json` v0.3.2, publisher `schivei`: só `contributes.grammars`/`languages`, SEM `main`, SEM `activationEvents`, SEM `vscode-languageclient`. |
| `teko fmt` existe e pode ser reusado | **Sim, e in-process** | `src/fmt/fmt.tks:649` `pub fn format_source(source: str): str \| error` — função pura, str→str. O LSP a chama direto (não precisa de subprocesso). |
| JSON existe (JSON-RPC sobre stdio) | **Sim** | `src/encoding/json/json.tks:463` `pub fn decode(text: str): JsonValue \| error`, `:476` `pub fn encode(value: JsonValue): str`. |
| Diagnósticos já posicionados (`file:line:col`) | **Sim, mas STRING** | `src/checker/diagnostics.tks:14` `diag_at` monta `"{file}:{line}:{col}: {msg}"`; `:37` `located_diag`. O `error` builtin carrega `.line`/`.col`/`.message` (lido em `diagnostics.tks:38-40`). |
| doc-comments preservados no AST (fonte do hover) | **Sim — CONFIRMADO no ponto exato** | `src/parser/ast.tks:407-408` `Function.has_doc`/`doc`; `:481` `Field.has_doc`/`doc`; `:556-557` `TypeDecl.has_doc`/`doc`; `:631` `ConstDecl.has_doc`/`doc`. **O parser JÁ preserva os doc-comments** — o hover não precisa de mudança no parser para tê-los. |

**Aprofundamentos que mudam o desenho:**

1. **Toda declaração top-level carrega a posição do NOME + doc.** `Function`/`TypeDecl`/
   `ConstDecl` têm `line`/`col` (a posição do nome — `ast.tks:409-410`, `:558-559`, `:631`).
   Isto, somado ao `doc`, dá o índice de símbolos para hover/go-to-def SEM tocar no checker.

2. **Referências NÃO têm span próprio.** Cada `Expr`/`TExpr` carrega só a posição de
   INÍCIO (`ast.tks:259` `Expr = struct { kind; line; col }`), e `Path`/`Segment`
   (`src/parser/type.tks:2-3`) **não carregam posição nenhuma**. Logo, mapear cursor→símbolo
   por caminhada de AST é grosseiro. **A saída limpa (que evita mexer no AST):** re-tokenizar
   o buffer com `teko::lexer::tokenize` — `Token` carrega `line`/`col`/`text`
   (`src/lexer/token.tks:184-189`) — achar o token sob o cursor e resolver o texto no índice
   de símbolos. O lexer já posiciona cada token; nenhum span novo de AST é necessário para v1.

3. **Warnings NÃO entram na lista de diagnósticos — são impressos direto no stderr.**
   `src/checker/typer.tks:2007-2008` (`warn_redundant_cast`) e
   `src/checker/initanalysis.tks:250-251` (`warn_unused_fn`) chamam `teko::io::eprintln`. Um
   LSP que só lê a lista de diags retornada **não veria** esses warnings. Lacuna real, nomeada
   no crumb C.

4. **O pipeline é whole-project e stringly-typed na saída.** `checker::type_program`
   (`typer.tks:6814`) retorna `TProgram | error` com o erro JÁ achatado em uma string
   newline-separada. A maquinaria estruturada existe logo abaixo:
   `type_program_with_deps_pre_mono` (`typer.tks:6657`, **`pub`**) retorna
   `PreMono { prog; table; diags: []str }`, e `pre_walk` (`typer.tks:6797`) já coleta TODOS
   os diags como `[]str` no formato `file:line:col: msg`. É desse seam que o LSP puxa.

---

## 2. A arquitetura

### 2.1 Localização e forma

- **Servidor:** novo diretório `src/lsp/` (namespace `teko::lsp`), compilado DENTRO do
  binário `teko`. **Não** `tooling/lsp/`: um projeto separado não conseguiria importar a
  superfície interna do `teko::checker` (a maioria é `fn` privada, e é enorme). In-process é
  o reuso mais barato e é o que a régua `teko lsp` (`TEKO_ROADMAP_DEVTOOLS.md:94`) já decidiu.
- **Despacho:** nova arma em `main.tks` (hoje o despacho vive em `main.tks:55-99`), espelhando
  a arma `fmt` (`main.tks:61-63`):
  `if cmd == "lsp" { teko::exit(teko::lsp::serve(args)) }`.
- **Ajuda:** nova página em `src/build/help.tks` (mesmo padrão de `print_help_fmt`,
  `help.tks:132`).
- **Cliente:** `tooling/vscode/` ganha um `client/` (TypeScript/JS) com um `LanguageClient`
  que sobe `teko lsp` (§6).

### 2.2 O laço JSON-RPC (o coração do servidor)

```
inicializar transporte (stdin/stdout em modo binário)
loop {
    msg = ler_uma_mensagem()          // framing Content-Length (§4)
    match método(msg) {
        "initialize"              => responder ServerCapabilities; guardar rootUri
        "initialized"             => (notificação; nada)
        "textDocument/didOpen"    => guardar texto do doc; re-checar; publicar diagnósticos
        "textDocument/didChange"  => atualizar texto; agendar re-check (debounce); publicar
        "textDocument/didSave"    => re-check completo do projeto; publicar
        "textDocument/didClose"   => esquecer o doc; limpar diagnósticos
        "textDocument/hover"       => resolver símbolo sob o cursor → doc-comment
        "textDocument/completion"  => símbolos visíveis na posição
        "textDocument/definition"  => símbolo → (arquivo, linha, col)
        "textDocument/formatting"  => teko::fmt::format_source(texto) → TextEdit
        "textDocument/semanticTokens/full" => classificar tokens (fase 2)
        "shutdown"                => marcar; responder null
        "exit"                    => sair (0 se houve shutdown, senão 1)
        _                         => responder MethodNotFound (para request) / ignorar (notif)
    }
}
```

### 2.3 Estado do servidor (documentos abertos)

O servidor mantém um mapa `uri -> texto` dos documentos abertos (o cliente é a fonte da
verdade do buffer não-salvo; o disco pode estar defasado). Para o re-check semântico, o
servidor monta um `parser::Program` juntando os itens dos arquivos do projeto — reusando a
mesma lógica de `teko::build::assemble` (`src/build/assemble.tks`), porém substituindo o
conteúdo do arquivo aberto pelo buffer do editor em vez de reler do disco. **Lacuna:**
`assemble_sel` (`assemble.tks:211`) lê SEMPRE do disco (`teko::io::read_file`, `:228`) — não
há hook para injetar um buffer em memória. Crumb E resolve com um `assemble_with_overrides`
aditivo (§5.5).

### 2.4 Funções de biblioteca que o servidor chama, e se são chamáveis hoje

| Chamada | Assinatura atual | `pub`? | Chamável in-process? |
|---|---|---|---|
| Lexer | `teko::lexer::tokenize(src): []Token \| error` | sim | **sim** |
| Parser (módulo) | `teko::parser::parse_module(toks, 0): Parsed<Module> \| error` | sim | **sim** |
| Parser (main) | `teko::parser::parse_main_file(toks, 0): Parsed<MainFile> \| error` | sim | **sim** |
| Checker (diag) | `teko::checker::type_program_with_deps_pre_mono(prog, empty, on_item): PreMono \| error` | sim | **sim, mas** ver §5.1 (invólucro `pub` mais limpo recomendado) |
| Formatador | `teko::fmt::format_source(src): str \| error` | sim | **sim** |

Conclusão: o front-end **já é uma biblioteca**. Nenhuma reescrita CLI→lib é necessária. As
lacunas são aditivas (invólucros `pub`, um tipo `Diagnostic` estruturado, o override de
buffer, e a primitiva de stdin) — não refatorações.

---

## 3. Cada capacidade, mapeada a uma fonte existente + a lacuna nomeada

### 3.1 Diagnósticos (warnings / errors / hints) — **crumb A + C**

- **Fonte:** o checker. `pre_walk` (`typer.tks:6797`) coleta tudo como `[]str`
  `file:line:col: msg` via `located_diag` (`diagnostics.tks:37`).
- **Lacuna 1 (v1, contornável):** os diags são STRINGS, sem severidade nem range. Para o LSP
  o servidor **re-parseia** cada string `file:line:col: msg` de volta para
  `{ file, line, col, msg }` (crumb A), severidade = Error por padrão, Warning quando a
  mensagem contém `"warning:"`. Range = de `(line,col)` até o fim do token sob esse ponto
  (obtido re-tokenizando a linha). Zero mudança no checker — diagnósticos ao vivo no dia 1.
- **Lacuna 2 (endurecimento, crumb C):** os warnings (`warn_redundant_cast` typer.tks:2007,
  `warn_unused_fn` initanalysis.tks:250) escapam pelo stderr e **nunca** entram na lista.
  Para o LSP vê-los, eles precisam ser COLETADOS, não impressos. Crumb C troca o `eprintln`
  por um append num acumulador de diagnósticos.
- **Lacuna 3 (endurecimento, crumb D):** tipo `Diagnostic` estruturado no checker
  (`{ file; line; col; end_line; end_col; severity; message }`) e um invólucro `pub`
  `collect_diagnostics(program): []Diagnostic`, eliminando o re-parse de string. Fica para
  depois porque o crumb A já entrega o valor.

### 3.2 Hover — **crumb F**

- **Fonte:** os doc-comments no AST (`ast.tks:407-408` etc.), CONFIRMADOS preservados.
- **Mapeamento cursor→símbolo:** re-tokenizar o buffer (`lexer::tokenize`, tokens com
  `line`/`col`/`text`), achar o `Word` cujo span cobre o cursor, pegar seu texto.
- **Resolução texto→declaração:** o **índice de símbolos** (crumb E): um walk do
  `parser::Program` que, para cada `Function`/`TypeDecl`/`ConstDecl`/`Field`, registra
  `{ nome; namespace; arquivo; line; col; kind; doc; assinatura }`. O hover devolve o
  `doc` + a assinatura renderizada do símbolo.
- **Lacuna:** ambiguidade de nomes iguais entre namespaces sem resolução de escopo — v1
  mostra a primeira/todas as correspondências; refinamento por escopo é fase 2 (usaria
  `resolve.tks::resolve_type_reg` e `scope.tks::lookup_call`, hoje `fn` privadas).

### 3.3 Completion (intellisense) — **crumb G**

- **Fonte:** os símbolos visíveis na posição. v1: todos os nomes do índice de símbolos
  (§3.2) + as palavras-chave do lexer + a superfície `teko::` builtin.
- **Escopo:** `src/checker/scope.tks` (`Env`, `lookup_binding`) é o resolvedor de escopo
  real, mas suas funções são `fn` privadas e operam sobre `Env` typed, não sobre "candidatos
  na posição X". v1 NÃO usa o `Env` — usa o índice de símbolos (grosseiro mas útil). Fase 2:
  expor os locais em escopo exige um walk posicional do corpo da função (lacuna maior).

### 3.4 Go-to-definition (navegação) — **crumb F (compartilha o índice)**

- **Fonte:** o índice de símbolos dá `(arquivo, line, col)` do NOME de cada declaração
  (`ast.tks:409-410` etc.) — exatamente o alvo de um go-to-def.
- **Mapeamento:** mesmo cursor→token→texto→índice do hover.
- **Resolvedor "próprio" (fase 2):** `resolve.tks::resolve_type_reg`/`resolve_name_ref`
  (`resolve.tks:491`) resolveria referências de TIPO com precisão de namespace; hoje recebem
  um `Path` escrito, não um cursor — fase 2 conectaria o token à `Path`.

### 3.5 Formatação — **crumb B — in-process**

- **Fonte:** `teko::fmt::format_source(src): str \| error` (`fmt.tks:649`). Função pura.
- **Decisão (responde à pergunta do dono):** **in-process**, não subprocesso. O servidor
  chama `format_source` diretamente e devolve um único `TextEdit` que substitui o documento
  inteiro (o `fmt` é whole-file por design — `fmt.tks` re-emite o stream inteiro). Nenhum
  processo filho, nenhuma serialização.

### 3.6 Diferenciação por cores — **dois níveis**

- **Sem LSP (TextMate, crumb H — valor imediato):** melhorar `tooling/shared/grammar-spec.json`
  (lido por `tooling/vscode/src/tm_gen.tks`) para distinguir mais escopos: tipos vs funções vs
  namespaces vs atributos (`#test`) vs doc-comments (`/** */`) vs keywords contextuais
  (`ref`/`adopt`/`unsafe`). É regex estático, client-only, não precisa do servidor.
- **Com LSP (semantic tokens, crumb I — fase 2):** `textDocument/semanticTokens/full`. O
  servidor classifica cada token pelo significado RESOLVIDO (este identificador é um TIPO / um
  parâmetro / um local `mut` / uma função / um namespace) — algo que o TextMate **não** sabe
  (não distingue tipo de variável). Reusa o índice de símbolos + a re-tokenização. Mais rico,
  mais caro, depois.

---

## 4. O transporte stdio — o alarme, PROVADO com arquivo:linha

**A afirmação:** o LSP fala JSON-RPC com framing `Content-Length: N\r\n\r\n<N bytes de corpo>`.
O corpo tem exatamente `N` bytes e **não** tem terminador de linha (spec LSP). Para lê-lo é
preciso ler **exatamente N bytes** de stdin.

**O que o runtime oferece hoje (`src/io/io.tks`):**
- `read_line(): str` (`:26`) — lê até `\n`, **strip do newline**. Bloqueante.
- `read_stdin(): str` (`:37`) — slurpa TODO o stdin até EOF. Bloqueante até EOF.
- `stdin_eof(): bool` (`:30`).

**Prova de que nenhuma serve:**
1. `read_stdin()` bloqueia até EOF. O stdin de um servidor de linguagem só vê EOF quando o
   cliente fecha o canal (no `exit`). Slurp-total é impossível para um servidor interativo
   de vida longa. **Descartada.**
2. `read_line()` lê até `\n`. O corpo JSON-RPC não termina em `\n`; imediatamente após os
   `N` bytes vem o próximo header `Content-Length: M\r\n`. Um `read_line()` após os headers
   retornaria `<corpoN>Content-Length: M` (até o primeiro `\n`, que está DENTRO do próximo
   header), **sobre-lendo** a próxima mensagem. Framing por linha é **incorreto** para
   corpos sem newline terminal. **Descartada.**
3. Não existe primitiva de 1 byte / N bytes (`grep` em `src/io/io.tks` só acha
   `read_line`/`read_stdin`/`read_file`). **Não há como compor uma leitura exata.**

**Conclusão provada:** o transporte exige UMA primitiva nova de runtime que leia um número
exato de bytes de stdin sem parar no newline. Ela pertence a `src/runtime/teko_rt.{c,h}` — a
**única C mantida** (exceção explícita à lei) — mais um `extern fn` em `src/io/io.tks`,
seguindo EXATAMENTE o padrão que `read_line`/`read_stdin` já estabeleceram (io.tks:20-37,
inclusive a ressalva de retorno bare-`str` sem união-`error` para o codegen do seed lançado
conseguir lower — io.tks:32-37). Forma declarada (contra a qual todo o resto do design já
compila):

```teko
/**
 * read_stdin_n — lê EXATAMENTE `n` bytes de stdin, bloqueando até tê-los ou até EOF. O corpo
 * de uma mensagem JSON-RPC tem um comprimento exato em bytes (`Content-Length`) e NENHUM
 * terminador de linha, então `read_line` (que para no `\n`) não consegue lê-lo sem invadir a
 * próxima mensagem — a razão de esta primitiva existir (ver docs/design/lsp-teko.md §4).
 *
 * Retorno bare-`str` (sem união `error`), pela mesma restrição registrada em
 * `read_line`/`read_stdin`: um `extern fn` de forma-`error` nova precisa de um lift por-nome
 * no codegen que o seed lançado (congelado) não aprende pós-release. Um EOF prematuro devolve
 * menos de `n` bytes; o chamador compara `.len` para detectar o fim do canal.
 *
 * @param n  a quantidade exata de bytes a ler
 * @return   os bytes lidos como str (até `n`; menos que `n` só em EOF)
 * @since 0.3.1.0 (transporte LSP)
 */
pub extern fn read_stdin_n(n: u64): str = "tk_rt_read_stdin_n" from "teko_rt"
```

**Sequenciamento (regra do bootstrap):** o seed que compila `src/lsp/` precisa saber lower
`read_stdin_n`. Logo a ordem é: (1) primitiva em `teko_rt.c/.h` + `extern fn` em io.tks +
lift no codegen editável em Teko; (2) um seed que a carrega; (3) só então `src/lsp/` a usa.
Idêntico ao que já foi feito para `read_line`/`read_stdin`.

**DESIGN-AHEAD:** todo o resto (§2, §3, §5, §6) compila contra a forma declarada acima. O que
fica bloqueado ATÉ a primitiva existir é APENAS a leitura real do corpo no `LspTransport`; o
resto (encode/decode de mensagens, roteamento, índice de símbolos, diagnósticos, formatação)
é testável isoladamente hoje.

---

## 5. As lacunas do compilador, nomeadas (= os crumbs de implementação)

### 5.1 Invólucro `pub` de diagnósticos no checker
`type_program` (typer.tks:6814) achata o erro em string; `type_program_with_deps_pre_mono`
(typer.tks:6657, `pub`) devolve `PreMono.diags: []str` mas exige um `dep_prog` e um
`ProgressFn`. Adicionar um seam limpo:
```teko
/**
 * collect_diagnostics — todos os diagnósticos do programa como uma lista de strings
 * `file:line:col: msg`, SEM erguer um erro (o caminho que o LSP consome). Reusa o pre-walk
 * existente com um programa de dependências vazio e um hook de progresso no-op, então
 * reporta exatamente o mesmo conjunto que uma build reportaria — sem ser abortado no
 * primeiro item ruim.
 *
 * @param program  o programa mesclado e não-tipado (itens do projeto)
 * @return         cada diagnóstico do projeto, em ordem de report (vazio = programa limpo)
 * @since 0.3.1.0 (LSP)
 */
pub fn collect_diagnostics(program: parser::Program): []str { ... }
```
Arquivo: `src/checker/` (novo `lsp_api.tks` ou dentro de `check.tks`). Aditivo, `pub`.

### 5.2 Coletar warnings em vez de imprimir (endurecimento)
Trocar `teko::io::eprintln` por append num acumulador em `warn_redundant_cast`
(typer.tks:2007-2008) e `warn_unused_fn` (initanalysis.tks:250-251). Necessário para o LSP
mostrar warnings. Requer threading de um acumulador — por isso é crumb C separado (toca o
typer), não o MVP.

### 5.3 Tipo `Diagnostic` estruturado (endurecimento)
```teko
/**
 * DiagSeverity — a severidade LSP de um diagnóstico: Error (1), Warning (2), Info (3),
 * Hint (4). Espelha `DiagnosticSeverity` do protocolo para o encode ser um cast direto.
 *
 * @since 0.3.1.0 (LSP)
 */
pub type DiagSeverity = enum { Error; Warning; Info; Hint }

/**
 * Diagnostic — um diagnóstico posicionado e estruturado, o que o LSP publica. Substitui a
 * string `file:line:col: msg` por campos que carregam o RANGE (início E fim) e a severidade,
 * que a forma-string perde. `end_line`/`end_col` default = fim do token sob o ponto inicial
 * quando o produtor não conhece um range mais largo.
 *
 * @field file       o arquivo-fonte do diagnóstico
 * @field line       a linha 1-based do início
 * @field col        a coluna 1-based do início
 * @field end_line   a linha 1-based do fim do range
 * @field end_col    a coluna 1-based do fim do range
 * @field severity   Error / Warning / Info / Hint
 * @field message    o texto do diagnóstico (sem o prefixo file:line:col)
 * @since 0.3.1.0 (LSP)
 */
pub type Diagnostic = struct {
    file: str; line: u32; col: u32; end_line: u32; end_col: u32
    severity: DiagSeverity; message: str
}
```
Arquivo: `src/checker/diagnostics.tks` (junto do `located_diag` existente). Depois, migrar os
produtores para emitir `Diagnostic` e o `collect_diagnostics` a devolver `[]Diagnostic`.

### 5.4 Índice de símbolos (novo, o motor de hover/def/completion)
```teko
/**
 * SymKind — que espécie de declaração um símbolo é, para o ícone/rótulo do editor.
 * @since 0.3.1.0 (LSP)
 */
pub type SymKind = enum { Function; Type; Const; Field; Method }

/**
 * Symbol — uma entrada do índice de símbolos: uma declaração navegável do programa, com sua
 * posição de definição, seu doc-comment (a fonte do hover) e sua assinatura renderizada.
 * Construído por um walk do `parser::Program` — não exige o checker (o hover básico e o
 * go-to-def funcionam só com o parse; o checker enriquece a assinatura depois).
 *
 * @field name       o nome como escrito
 * @field namespace  o namespace declarante (provenância A3)
 * @field file       o arquivo-fonte da definição
 * @field line       a linha 1-based do nome
 * @field col        a coluna 1-based do nome
 * @field kind       Function / Type / Const / Field / Method
 * @field doc        o doc-comment (`""` quando ausente)
 * @field signature  a assinatura renderizada para o hover (ex.: `fn f(x: i64): str`)
 * @since 0.3.1.0 (LSP)
 */
pub type Symbol = struct {
    name: str; namespace: str; file: str; line: u32; col: u32
    kind: SymKind; doc: str; signature: str
}

/**
 * build_symbol_index — varre um `parser::Program` e emite um `Symbol` por declaração
 * top-level (fn / type / const) e por membro (campo / método), lendo nome, posição, doc e
 * visibilidade direto do AST — sem tipar. É a fonte única de hover, go-to-def e completion v1.
 *
 * @param program  o programa mesclado (de `assemble`)
 * @return         a lista de símbolos, em ordem de arquivo/declaração
 * @since 0.3.1.0 (LSP)
 */
pub fn build_symbol_index(program: parser::Program): []Symbol { ... }
```
Arquivo: `src/lsp/symbols.tks`. Pode viver em `teko::lsp` (não precisa ser do checker).

### 5.5 Override de buffer no assemble (para o buffer não-salvo)
`assemble_sel` (assemble.tks:211) sempre lê do disco (`:228`). O LSP precisa injetar o texto
do editor para o arquivo aberto. Seam aditivo:
```teko
/**
 * FileOverride — um par (caminho, conteúdo) que substitui o que está no disco para aquele
 * arquivo durante a montagem — o buffer não-salvo que o editor mantém para o documento aberto.
 * @since 0.3.1.0 (LSP)
 */
pub type FileOverride = struct { path: str; content: str }

/**
 * assemble_with_overrides — como `assemble`, mas cada arquivo cujo caminho casa um
 * `FileOverride` é lexado/parseado a partir do conteúdo dado em vez do disco. Todo o resto do
 * caminho (discovery, merge, isolamento por-arquivo) é idêntico, então o LSP re-checa o
 * projeto exatamente como uma build o veria, com uma única substituição em memória.
 *
 * @param files      o conjunto de arquivos descoberto
 * @param overrides  os buffers em memória que vencem o disco
 * @return           o programa mesclado + os diagnósticos de lex/parse por-arquivo
 * @since 0.3.1.0 (LSP)
 */
pub fn assemble_with_overrides(files: []SourceFile, overrides: []FileOverride): Assembled | error { ... }
```
Arquivo: `src/build/assemble.tks` (aditivo). O `assemble` atual vira um caso de zero overrides.

### 5.6 A primitiva de stdin (§4) — o único item com C mantida
`read_stdin_n` em `teko_rt.{c,h}` + `extern fn` em `io.tks` + lift no codegen Teko. Crumb 0
(sequenciado antes de tudo que usa o transporte real).

---

## 6. O lado cliente (VS Code)

### 6.1 O que muda no `package.json`
Hoje é grammar-only (§1). Para um LSP, adicionar:
- `"main": "./client/out/extension.js"` — o ponto de ativação.
- `"activationEvents": ["onLanguage:teko"]`.
- `"dependencies": { "vscode-languageclient": "^9.x" }`.
- `"contributes.configuration"` — um setting `teko.serverPath` (caminho do binário `teko`).
- `"scripts"` de build (esbuild/tsc) para compilar `client/src/extension.ts`.

### 6.2 O `LanguageClient` mínimo (novo `tooling/vscode/client/src/extension.ts`)
- No `activate`, monta um `ServerOptions` que roda `teko lsp` (comando + args) com
  `transport: stdio`, resolvendo o binário via `teko.serverPath` (ou `PATH`).
- `DocumentSelector` = `{ language: "teko" }`; inicia `new LanguageClient(...)`.
- Isto é ~40 linhas de TS boilerplate padrão de LSP. NÃO é código Teko (é o cliente do
  editor). Não viola a lei Teko-only — a lei governa o produto/compilador; o cliente VS Code é
  glue de editor, análogo ao `.vim`/`.el`/`.nanorc` já mantidos em `tooling/`.

### 6.3 Como o `.vsix` empacota
O `vsce package` precisa dos `client/out/*.js` + o node_modules de `vscode-languageclient`
(ou bundlado com esbuild num único `extension.js` — recomendado, evita empacotar node_modules).
O `files` do package.json (hoje `tooling/vscode/package.json:16-22`) ganha `client/out/**`. O
binário `teko` NÃO é empacotado no `.vsix` (é resolvido em runtime via setting/PATH) — o
`.vsix` fica leve e independente da plataforma.

### 6.4 Cores: TextMate vs semantic tokens (responde à pergunta do dono)
- **`grammar-spec.json` / `tm_gen.tks` melhora o realce SEM LSP** — é regex estático:
  distinguir mais categorias léxicas (tipos por convenção de maiúscula, atributos `#…`,
  doc-comments, keywords contextuais). Client-only, valor imediato (crumb H).
- **Semantic tokens EXIGEM o LSP** — só o servidor sabe que `Foo` naquela posição é um tipo e
  não uma variável (resolução), que `x` é um parâmetro e não um global. TextMate é incapaz
  disso. Crumb I, fase 2.

---

## 7. Incremental, não whole-file

**Hoje:** `assemble` lê todos os arquivos; `type_program` (typer.tks:6814) re-coleta a tabela
de tipos e re-tipa o programa INTEIRO a cada chamada — O(programa) por chamada, reconstruindo
`env`/`table` do zero (`pre_walk`, typer.tks:6797). Não há entrada incremental.

**Estratégia por fases:**
1. **v1 (barato e correto):** duas velocidades.
   - **Por tecla (didChange):** só lex+parse do BUFFER único (`tokenize` + `parse_module`) —
     O(arquivo), instantâneo. Publica diagnósticos de sintaxe imediatamente.
   - **Debounced / no save (didSave):** re-check semântico do projeto inteiro via
     `assemble_with_overrides` + `collect_diagnostics`. Um timer de debounce (ex. 300ms)
     evita re-checar a cada tecla.
2. **Fase 2 (cache):** memoizar a tabela de tipos coletada e o `TProgram` das dependências
   entre chamadas, re-tipando só o item mudado. Custo hoje: cada `type_program` reconstrói
   tudo — a memoização é uma reforma separada e grande do checker; o design a NOMEIA como
   futura e entrega o interino (parse por-tecla + check debounced) que já dá feedback ao vivo.

**Custo declarado honestamente:** um projeto grande (o próprio compilador, centenas de KB de
fontes) leva o tempo de uma build de front-end por re-check completo. O debounce + o parse
por-tecla mascaram isso para o caso de digitação; o check completo só roda no save/idle.

---

## 8. A ordem em crumbs (cada um fechável sozinho, valor mais cedo primeiro)

| Crumb | Entrega | Depende de | Toca |
|---|---|---|---|
| **0** | Primitiva `read_stdin_n` (runtime + extern + lift) | — | `teko_rt.{c,h}` (C mantida), `io.tks`, codegen Teko |
| **A** | Diagnósticos ao vivo: transporte JSON-RPC + `initialize`/`didOpen`/`didChange` + `collect_diagnostics` (re-parse de string) + publish | 0, 5.1 | `src/lsp/*`, `src/checker` (invólucro `pub`), `main.tks`, `help.tks` |
| **B** | Formatação in-process (`textDocument/formatting` → `fmt::format_source`) | A | `src/lsp/*` |
| **C** | Coletar warnings (redundant-cast, unused-fn) em vez de imprimir → visíveis no LSP | A | `typer.tks`, `initanalysis.tks` |
| **E** | Índice de símbolos + `assemble_with_overrides` (buffer não-salvo) | A | `src/lsp/symbols.tks`, `assemble.tks` |
| **F** | Hover + go-to-definition (cursor→token→índice) | E | `src/lsp/*` |
| **G** | Completion (símbolos do índice + keywords) | E | `src/lsp/*` |
| **H** | Cores TextMate melhoradas (client-only, paralelo a tudo) | — | `grammar-spec.json`, `tm_gen.tks` |
| **cliente** | `LanguageClient` no VS Code + package.json + esbuild | A | `tooling/vscode/client/*`, `package.json` |
| **D** | `Diagnostic` estruturado (`[]Diagnostic`, severidade/range reais) — endurece A/C | C | `diagnostics.tks`, `src/lsp/*` |
| **I** | Semantic tokens (classificação resolvida) — fase 2 | E, D | `src/lsp/*` |

**Sequência de valor:** A (diagnósticos ao vivo) e H (cores) primeiro — ambos dão retorno
visível cedo e A é o pedido central. B (formatação) é quase de graça (função pura já existe).
E→F→G desbloqueiam navegação/hover/intellisense. D e I endurecem.

---

## 9. Fixtures de regressão (entradas → saídas esperadas)

O LSP é stdio-interativo; testa-se em duas camadas:

### 9.1 Unitários `.tkt` sobre as funções puras (o grosso da confiança)
- `src/lsp/framing_test.tkt` — encode/decode do envelope `Content-Length`: dado um corpo
  JSON conhecido, o header montado é `Content-Length: N\r\n\r\n` com N = bytes do corpo; e o
  parser de framing, dado `header+corpo+header2+corpo2`, devolve exatamente `corpo` e depois
  `corpo2` (o caso que `read_line` erraria — §4).
- `src/lsp/symbols_test.tkt` — `build_symbol_index` sobre um `parser::Program` fixture com
  uma fn documentada, um type e um const: assevera nome/line/col/kind/doc de cada `Symbol`.
- `src/lsp/dispatch_test.tkt` — dado um `initialize` decodificado, a resposta carrega as
  capabilities esperadas; um método desconhecido gera `MethodNotFound`.
- `src/checker/…_test.tkt` — `collect_diagnostics` sobre um programa com 1 erro + 1 warning
  devolve DOIS diagnósticos (o warning presente é a regressão do crumb C).

### 9.2 Smoke nativo scriptado (`.tkr`-style, exit code)
Um cenário que alimenta `teko lsp` com uma sequência gravada de mensagens JSON-RPC no stdin
(`initialize` → `initialized` → `didOpen` de um `.tks` com um erro conhecido → `shutdown` →
`exit`) e casa a saída contra as respostas esperadas (a `publishDiagnostics` deve conter a
mensagem do erro conhecido). **Exit codes nativos esperados:**
- sessão bem-formada terminada por `exit` após `shutdown` → **exit 0**.
- `exit` SEM `shutdown` prévio → **exit 1** (regra do protocolo).
- stdin fechado no meio (EOF prematuro) → **exit 1**.
- argumento inválido a `teko lsp` → **exit 2** (paridade com o resto do CLI, `main.tks`).

Estes exit codes são o contrato de gate do crumb A.

---

## 10. Riscos, tensões de lei, e resolução recomendada

1. **[ALARME PROVADO — §4] Transporte stdin.** Não há leitura byte-exata; `read_line`
   sobre-lê e `read_stdin` bloqueia até EOF. **Resolução:** primitiva `read_stdin_n` em
   `teko_rt.{c,h}` (dentro da exceção C mantida) + extern em io.tks, padrão já estabelecido
   por `read_line`/`read_stdin`. Crumb 0, sequenciado primeiro. **Sem tensão de lei** — a
   exceção `teko_rt.{c,h}` cobre exatamente isto.

2. **Warnings escapam pelo stderr (typer.tks:2008, initanalysis.tks:251).** Invisíveis a um
   LSP que lê a lista de diags. **Resolução:** crumb C coleta em vez de imprimir. Toca o
   typer (não-congelado — é `.tks`).

3. **Diags stringly-typed sem severidade/range.** **Resolução:** v1 re-parseia a string
   (crumb A, zero mudança no checker); crumb D introduz `Diagnostic` estruturado. Sem tensão.

4. **Referências sem span; `Path`/`Segment` sem posição (type.tks:2-3).** Mapear cursor→AST é
   grosseiro. **Resolução:** re-tokenizar (o `Token` posiciona tudo — token.tks:187-188) e
   resolver pelo índice de símbolos. Evita mexer no AST. Precisão fina de namespace = fase 2.

5. **`assemble` só lê do disco (assemble.tks:228).** Não vê o buffer não-salvo.
   **Resolução:** `assemble_with_overrides` aditivo (crumb E). Sem tensão.

6. **Sem check incremental (type_program re-tipa tudo, typer.tks:6814).** **Resolução:**
   parse por-tecla + check completo debounced (v1); memoização = fase 2 nomeada. Custo
   declarado (§7). Sem tensão de lei.

7. **[TENSÃO POTENCIAL DE LEI — resolvida law-first, NÃO HALT] Cliente VS Code em TS/JS vs
   "Teko-only".** A lei Teko-only governa o PRODUTO/compilador (`.tks`). O cliente do editor é
   glue de plataforma do editor — o mesmo estatuto do que já existe em `tooling/` (`.vim`,
   `.el`, `.nanorc`, e o próprio `tm_gen` que GERA config de editor). O `vscode-languageclient`
   é uma dependência do editor, não do compilador. **Resolução law-first:** permitido, por
   analogia direta ao conteúdo de `tooling/` já mantido; nenhuma lógica de linguagem vive no
   TS (só o spawn do servidor). Não HALT.

**Nenhuma tensão genuína não-resolvida resta.** O design é ratificável como está; o único
pré-requisito material (a primitiva de stdin) tem caminho conhecido dentro da lei.

---

## 11. Pontos de ritual (onde o gate cheio deve passar)

- Após o **crumb 0** (primitiva + novo seed): gate cheio — muda runtime C + codegen, é
  fundação de tudo.
- Após o **crumb A** (primeiro servidor funcional): gate cheio + o smoke `.tkr` de §9.2.
- Após o **crumb C** (warnings coletados): gate cheio — toca o typer, superfície sensível.
- Após o **crumb E** (assemble_with_overrides): gate cheio — toca o caminho de build.
- Após o **crumb D** (Diagnostic estruturado): gate cheio — muda a superfície de diagnóstico
  que a build também usa.
- Crumbs B, F, G, H, I e o cliente: gate normal do crumb (unitários `.tkt` + o gate padrão),
  por serem aditivos e isolados em `src/lsp/`/`tooling/`.

---

## 12. O que fica bloqueado vs adiantado (mandato "adiantar o que der")

**Adiantável HOJE (não depende de nada bloqueado):**
- Todo o §2/§3/§5/§6 de DESIGN (feito, este documento).
- Os tipos/contratos: `Diagnostic`, `Symbol`, `FileOverride`, `read_stdin_n` (formas
  declaradas acima) — compilam contra as shapes existentes.
- As fixtures §9.1 podem ser escritas contra as funções puras assim que elas existirem.
- Cores TextMate (crumb H) — 100% independente do servidor.
- O cliente VS Code (boilerplate) — pode ser escrito contra o contrato `teko lsp` stdio.

**Bloqueado até o crumb 0 fechar:** apenas a LEITURA real do corpo no `LspTransport`
(precisa de `read_stdin_n`). Todo o roteamento, encode/decode, índice e diagnósticos são
testáveis sem ela.
