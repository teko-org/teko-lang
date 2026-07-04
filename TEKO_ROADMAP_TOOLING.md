# TEKO_ROADMAP_TOOLING — ferramental de editor (cores, intellisense, build) para VS Code, Vim/Neovim, Emacs, Nano

> **Escopo.** Este roadmap cobre o ferramental de **editor** para Teko: quatro alvos (**VS Code**,
> **Vi/Vim + Neovim**, **Emacs**, **Nano**) em três capacidades (**cores** = realce de sintaxe,
> **intellisense** = language server, **ferramental de compilação** = build/run/test integrados ao
> editor). É um roadmap de **consumo**, não de linguagem: nada aqui altera `src/`.

> **Não-escopo / relação com a SUPREME RULE.** Para os Eixos A, B, D e E, a SUPREME RULE (zero
> desalinhamento `.c`/`.h` ↔ `.tks`) não se aplica — esse ferramental vive **fora** do compilador, num
> diretório `tooling/` na raiz do repo, e consome apenas **interfaces estáveis** já existentes: a CLI
> (`teko build|run|test <dir>` — `main.c:119-127`) e o formato de diagnóstico
> `teko: <arquivo>:<linha>:<coluna>: <mensagem>` (já emitido — `src/checker/check_modules.c:18`,
> `src/driver.c:198-213`). **Exceção: o Eixo C (intellisense/LSP)** — RATIFICADO (2026-07-01) para viver
> **dentro** do próprio compilador, como um subcomando `teko lsp` escrito nativamente em Teko; ali a
> SUPREME RULE **se aplica normalmente** (par `.tks`+`.c`/`.h`, como todo o resto de `src/`), e a
> implementação é **diferida** até o compilador atingir uma versão estabilizada (ver Eixo C).

> **Cross-ref.** Depende do Eixo E do `TEKO_ROADMAP_INDEPENDENCE.md` (posição `file:line` — já
> parcialmente fiada no checker, ver acima) para diagnósticos ricos, e do Eixo A do mesmo documento
> (compilação por `.tkp`) para os comandos de build que o editor invoca. Não altera o `TEKO_MASTER_PLAN.md`
> (não é uma fase do compilador); pode ser referenciado lá como uma trilha paralela quando o Eixo C aqui
> amadurecer.

> **Princípio DRY aplicado ao próprio ferramental.** Palavras-chave, operadores, delimitadores de
> comentário e regras de string do Teko vivem em **um lugar** (`src/lexer/token.tks` + `lexer.tks`).
> É proibido manter 4 cópias manuais divergentes (uma por editor) dessa mesma informação — o Eixo A
> gera as quatro saídas a partir de **uma** fonte.

---

## Prior art na linha pré-reboot (histórico do git de `main`; NÃO portada para a linha atual)

`main` já tem uma tentativa de extensão VS Code (`extensions/vscode/`) e um esqueleto JetBrains
(`extensions/jetbrains/com.schivei.teko/`, TextMate-only — fora do escopo de editores deste roadmap,
mas confirma que "gramática compartilhada" é o caminho certo). **Nenhum dos dois existe em
a linha atual** — o reboot do front-end (`REBOOT_PLAN.md`) não os carregou. Auditoria do que está lá,
para reaproveitar o que serve e não repetir o que não serve:

- **`language-configuration.json`** — comentários (`//`, `/* */`), brackets, auto-closing e
  surrounding pairs. **Genérico e reaproveitável tal como está** (não depende de keywords específicas).
- **`syntaxes/teko.tmLanguage.json`** — gramática TextMate **escrita à mão** (30 linhas), não gerada.
  Está **desatualizada contra a legislação atual**: lista `for`/`switch`/`when`/`service`/`handler`/
  `decorates`/`command`/`notification`/`query`/`raised`/`where`/`with` (construções de uma fase anterior
  do projeto, hoje removidas ou nunca canônicas — ver [[teko-only-loop]]/[[teko-no-match-on-bool]] na
  memória: Teko só tem `loop`, não `for`/`switch`) e não lista `loop`/`match`/`variant`/o vocabulário
  atual. **Não portar esta gramática tal qual** — é exatamente a divergência manual que o Eixo A existe
  para evitar; regerar do zero via A1→A2 a partir do lexer real da linha atual.
- **`src/extension.js` — comandos `teko.run`/`teko.build`.** Invocam o compilador via
  `child_process.exec` com o caminho **interpolado numa string de shell**:
  `` cp.exec(`"${compilerPath}" run "${tkpPath}"`, …) ``. **Achado de segurança:** isso é uma superfície
  de **injeção de comando** — um caminho de workspace contendo aspas, `` ` ``, `$()` ou `;` escapa a
  interpolação e executa no shell do usuário. Ao portar (Eixo D2), substituir por `execFile`/`spawn` com
  **argv em array** (nunca concatenação de string para shell) — não é uma reescrita cosmética, é a
  correção do problema.
- **`src/extension.js` — client LSP.** Registra um `vscode-languageclient` cujo `serverOptions` chama
  `<compilerPath> check` como se fosse o processo do language server. **Esse subcomando não existe** —
  nem no compilador pré-reboot, nem no compilador atual — e, mesmo que existisse,
  nada indica que falaria o protocolo LSP (JSON-RPC sobre stdio) exigido pelo `LanguageClient`. É um
  client sem servidor: o wiring existe, o servidor (Eixo C — subcomando `teko lsp`) nunca foi construído
  e está **diferido** (ver Eixo C). Ao portar, **não reativar este wiring até `teko lsp` existir de
  verdade** — do contrário a extensão anuncia um recurso (intellisense) que silenciosamente não funciona.
- **Correção de segurança já aplicada em `main` (commit `48eb91c`, Dependabot).** A `devDependency`
  obsoleta `vscode` (`^1.1.37`) puxava `minimist` (prototype pollution crítico), `mocha`, `mkdirp`,
  `http-proxy-agent` e `diff`/`minimatch` vulneráveis; foi trocada por `@types/vscode` (`^1.75.0`,
  alinhado ao `engines.vscode`), zerando o `npm audit`. As dependências de **produção**
  (`vscode-languageclient`, `minimatch`, `glob`) já estavam limpas — essa correção é válida e deve ser
  preservada ao reconstruir `tooling/vscode/package.json` (não reintroduzir a `devDependency` `vscode`
  antiga).

> **Resumo para quem for portar:** aproveitar `language-configuration.json`; descartar e regerar a
> gramática TextMate (Eixo A/B1); descartar o `cp.exec` de string e o wiring do `LanguageClient` tal
> como estão (Eixo D1/D2, ambos precisam ser refeitos — um por segurança, outro por não ter servidor);
> manter a lição da correção de dependência (`@types/vscode`, não `vscode`).

---

## Eixo A — Fonte única de léxico para realce (evita 4 cópias divergentes)

**Cânone:** a lista de keywords/operadores/literais/comentários é definida **uma vez**, lida do lexer
real, e cada editor consome uma **saída gerada**, nunca uma cópia digitada à mão.

| # | Entrega | Estado |
|---|---|---|
| A1 | **Extrator de especificação** (`tooling/shared/grammar-spec.json`) — lê `src/lexer/token.tks` (+ `lexer.tks`) e emite um JSON canônico: `{keywords, operators, literals, line_comment, block_comment, string_escapes}`. | falta |
| A2 | **Gerador TextMate** (`tooling/vscode/syntaxes/teko.tmLanguage.json`) a partir do A1. | falta (dep A1) |
| A3 | **Gerador de sintaxe Vim** (`tooling/vim/syntax/teko.vim`, `syntax match`/`syntax keyword`) a partir do A1. | falta (dep A1) |
| A4 | **Gerador de font-lock Emacs** (`tooling/emacs/teko-mode.el` — lista `teko-font-lock-keywords`) a partir do A1. | falta (dep A1) |
| A5 | **Gerador de regras Nano** (`tooling/nano/teko.nanorc`) a partir do A1 — sem aninhamento/contexto (limitação do próprio Nano), cobertura best-effort. | falta (dep A1) |

> **Resultado do Eixo A:** uma mudança de sintaxe da linguagem (nova keyword, novo operador) se propaga
> para os 4 editores regerando as saídas de A2–A5, nunca editando 4 arquivos à mão.

---

## Eixo B — Cores (realce de sintaxe) por editor

| # | Entrega | Estado |
|---|---|---|
| B1 | **VS Code** — gramática TextMate embutida (consome A2) + `language-configuration.json` (comentários `//`/`/* */`, pares de bracket/aspas, indentação por bloco). | falta na linha atual (dep A2) — existe esqueleto no histórico pré-reboot (`extensions/vscode/`), mas com gramática manual e desatualizada; ver "Prior art" acima. Reaproveitar só o `language-configuration.json`. |
| B2 | **Vim/Neovim** — `syntax/teko.vim` (consome A3), `ftdetect/teko.vim` mapeando `.tks`/`.tkt` → filetype `teko` e `.tkp` → `toml` (o manifesto já É TOML — `TEKO_ROADMAP_INDEPENDENCE.md` Eixo A). *Opcional/futuro:* gramática Tree-sitter (`queries/teko/highlights.scm`) para Neovim ≥0.9, mais precisa que regex. | falta (dep A3) |
| B3 | **Emacs** — `teko-mode.el`, modo maior derivado de `prog-mode`, `teko-font-lock-keywords` (consome A4) + `syntax-table` para comentários/strings + `.tkp` associado a `toml-mode` se instalado. | falta (dep A4) |
| B4 | **Nano** — `teko.nanorc` (consome A5): keywords, tipos primitivos, comentários de linha/bloco, strings/interpolação. Sem contexto/aninhamento — aceita falso-positivo ocasional (limite conhecido do Nano). | falta (dep A5) |

---

## Eixo C — Intellisense (`teko lsp`, Language Server Protocol) — DIFERIDO

> **RATIFICADO (2026-07-01).** O LSP **não** é um binário/processo `tooling/teko-lsp/` externo escrito
> num degrau intermediário (C) e migrado depois — vai direto para dentro do **próprio compilador**, como
> um **subcomando** `teko lsp` (ao lado de `build`/`run`/`test` em `main.c`/`main.tks`), escrito
> **nativamente em Teko**. Isso resolve as duas questões que este documento deixava "abertas": a
> linguagem de implementação (Teko, não C) e binário-separado-vs-subcomando (subcomando). **Implementação
> DIFERIDA até o compilador atingir uma versão estabilizada** — recomendo como marco o fechamento de
> **★ THE VALIDATION GATE** do `TEKO_MASTER_PLAN.md` (self-host nativo fechando, hoje 🔶 Fase 6); o marco
> exato de "estabilizada" é do legislador, não deste documento.

**Cânone:** o intellisense **reaproveita** o front-end real de Teko (`teko::lexer`/`teko::parser`/
`teko::checker`), nunca reimplementa análise léxica/semântica em paralelo — o mesmo princípio DRY/SUPREME
RULE do resto do compilador, agora aplicado ao servidor. Por viver em `src/`, o par C/`.tks` é
obrigatório enquanto o C for a base de bootstrap (como todo o resto do compilador — ver
`TEKO_ROADMAP_INDEPENDENCE.md` Eixo C4); some naturalmente quando o self-hosting aposentar o C.

| # | Entrega | Estado |
|---|---|---|
| C1 | **Esqueleto `teko lsp`** — novo subcomando (`src/lsp/lsp.{tks,c,h}` + `lsp_test.tkt`), processo stdio, handshake LSP (`initialize`/`initialized`/`shutdown`/`exit`), reaproveita `teko::lexer`/`teko::parser`/`teko::checker` (não duplica). | **diferido** — aguarda versão estabilizada do compilador |
| C2 | **Diagnostics** — reanálise por arquivo ao editar/salvar (debounce), publica `textDocument/publishDiagnostics` a partir dos `tk_error` do checker — mesmo `arquivo:linha:coluna` que a CLI já emite (ver Eixo E do ROADMAP_INDEPENDENCE para a doutrina de posição). Erro de parse → diagnóstico honesto, nunca trava o editor. | **diferido** (dep C1) |
| C3 | **Hover** — tipo inferido da expressão sob o cursor, lido direto da árvore tipada (`tast`) já produzida pelo checker. | **diferido** (dep C1) |
| C4 | **Completion** — símbolos em escopo (vars/fns), membros de `struct`/`interface`, namespaces alcançáveis via `use`, e keywords da linguagem. | **diferido** (dep C1) |
| C5 | **Go-to-definition / find-references** — usa a mesma tabela de símbolos/escopo do checker (`resolve.tks`/`scope.tks`), não uma reconstrução própria. | **diferido** (dep C1) |
| C6 | **Document symbols / outline** — lista de `fn`/`struct`/`variant`/`interface` de um arquivo. | **diferido** (dep C1) |
| C7 | *(avançado, opcional)* **Semantic tokens** — realce preciso baseado no checker (distingue tipo vs valor vs namespace), superior ao regex do Eixo B quando o cliente suporta. | **diferido** (dep C1–C6) |

> **Nano fica de fora do Eixo C por design, não por atraso** — não tem API de plugin/processo externo
> para falar LSP. Recebe só o Eixo B (cores) e a documentação do Eixo D5 (fluxo manual de build).

> **Enquanto C está diferido:** os Eixos A, B, D e E não esperam por ele — cores (Eixo A/B) e tasks de
> build (D2/D3-`:make`/D4-`compile`) são entregáveis desde já, porque não dependem de intellisense.

---

## Eixo D — Clientes LSP + integração de build/run/test por editor

**Ferramental de compilação já existe hoje** (`teko build|run|test <dir> [-o <out>] [--coverage]
[--no-test]` — `main.c:119-127`, `src/driver.c`); D2 não tem dependência do Eixo C e pode ser entregue
imediatamente.

| # | Entrega | Estado |
|---|---|---|
| D1 | **VS Code — client LSP** — extensão contribui `vscode-languageclient`, conecta ao subcomando `teko lsp` via stdio, ativa para linguagem `teko` (`.tks`/`.tkt`) e `toml`+schema para `.tkp`. | **diferido** (dep C1) — `main:extensions/vscode/src/extension.js` já tem o `LanguageClient` registrado, mas apontando para um subcomando `check` inexistente/sem protocolo LSP; **não portar o wiring, refazer quando `teko lsp` existir**. |
| D2 | **VS Code — tasks + problemMatcher** — comandos "Teko: Build" / "Teko: Run" / "Teko: Test" chamando a CLI via **Tasks API** (nunca `child_process.exec` com string interpolada); `problemMatcher` `^teko: (.*):(\d+):(\d+): (.*)$` casa o formato hoje emitido. | falta (sem dependência — CLI já existe) — `main:extensions/vscode/src/extension.js` tem `teko.run`/`teko.build` via `cp.exec` de string (achado de segurança: injeção de comando — ver "Prior art" acima); a versão nova troca por `execFile`/`spawn` com argv-array ou pela Tasks API nativa do VS Code. |
| D3 | **Vim/Neovim — client LSP** — `nvim-lspconfig` (Neovim nativo) ou `vim-lsp`/`ale` (Vim 8+), apontando para `teko lsp` (subcomando do mesmo binário `teko`, sem instalar nada à parte). `:make`: `makeprg=teko\ build\ %:h`, `errorformat=teko\\:\\ %f:%l:%c:\\ %m`. | client: **diferido** (dep C1) · `:make`: falta (sem dependência) |
| D4 | **Emacs — client LSP** — `eglot` (nativo desde Emacs 29) ou `lsp-mode`, apontando para `teko lsp`. `compile-command="teko build ."` + `compilation-error-regexp-alist` casando o mesmo formato. | client: **diferido** (dep C1) · `compile`: falta (sem dependência) |
| D5 | **Nano — fluxo manual** — sem client LSP nem task runner nativos; documentar rodar `teko build|run|test .` no terminal ao lado do editor. Só documentação, sem código. | falta |

---

## Eixo E — Empacotamento e distribuição dos plugins

**Layout de diretório proposto** (paralelo a `src/` — não mistura com o compilador). O Eixo C
(`teko lsp`) **não mora aqui** — vive em `src/lsp/` como parte do próprio compilador (ver Eixo C); os
clientes de editor abaixo apenas **apontam** para o subcomando `teko lsp` do binário já instalado, sem
distribuir um segundo binário:

```
tooling/
├── shared/     # A1: grammar-spec.json (fonte única)
├── vscode/     # B1, D1, D2: a extensão
├── vim/        # B2, D3: plugin Vim/Neovim
├── emacs/      # B3, D4: teko-mode.el
└── nano/       # B4, D5: teko.nanorc + doc
```

| # | Entrega | Estado |
|---|---|---|
| E1 | Criar a árvore `tooling/` acima (esqueletos vazios, sem lógica). | falta |
| E2 | **VS Code** — empacotar `.vsix` (`vsce package`); instalação local sempre suportada; publicação no Marketplace é decisão aberta. Metadados (`publisher: schivei`, `name: teko`) podem ser reaproveitados de `main:extensions/vscode/package.json`. | falta (dep B1, D1, D2) |
| E3 | **Vim/Neovim** — plugin instalável por gerenciador padrão (`vim-plug`/`packer.nvim`/`lazy.nvim`) apontando para `tooling/vim/`; sem gerenciador, `:set rtp+=` manual. | falta (dep B2, D3) |
| E4 | **Emacs** — pacote via `use-package :load-path` apontando para `tooling/emacs/`; publicação em MELPA é decisão aberta (futuro). | falta (dep B3, D4) |
| E5 | **Nano** — arquivo único; instalação = `include "~/.nano/teko.nanorc"` em `~/.nanorc`, documentado, sem instalador. | falta (dep B4) |

---

## Decisões — ratificadas e abertas

**Ratificadas (Law-first — DRY/SUPREME RULE aplicados ao ferramental):**
- Fonte única de léxico (Eixo A) — proibido manter 4 cópias manuais de keywords/operadores.
- `teko lsp` reaproveita `teko::lexer`/`teko::parser`/`teko::checker` diretamente (mesmo binário,
  subcomando) — proibido reimplementar análise léxica/semântica em paralelo dentro do servidor.
- Nano recebe só cores (Eixo B) + doc de build manual (D5) — sem API de plugin, não é uma lacuna a fechar.
- **Nenhuma invocação de processo externo a partir de um editor usa concatenação de string para shell**
  (`cp.exec`/`os.system`/equivalentes com template string). Sempre `execFile`/`spawn` com argv em array,
  ou a API de Tasks nativa do editor. **Motivo:** achado de segurança na prova de conceito em
  `main:extensions/vscode/src/extension.js` (caminho de workspace interpolado em `cp.exec`, sem
  sanitização — injeção de comando). Aplica-se a D2/D3/D4 igualmente.
- **Gramática de cores nunca é portada manualmente de `main`** — mesmo onde `main` já tem um arquivo
  pronto (`teko.tmLanguage.json`), ele é descartado e regerado pelo Eixo A, porque foi escrito à mão e já
  está comprovadamente desatualizado contra a legislação atual (ver "Prior art").
- **LSP dentro do compilador, em Teko, diferido (2026-07-01).** `teko lsp` é um subcomando nativo de
  `teko` (não um binário/processo `tooling/teko-lsp/` externo), implementado em Teko (não em C como
  degrau intermediário), e sua implementação **não começa** antes de o compilador atingir uma versão
  estabilizada. Isso substitui as duas questões que este documento antes deixava abertas (linguagem de
  implementação; binário separado vs subcomando) por uma decisão única já fechada.

**Abertas (a decidir quando o crumb chegar):**
- **Marco exato de "versão estabilizada"** que libera o início do Eixo C — candidato natural: o
  fechamento de ★ THE VALIDATION GATE (self-host nativo, `TEKO_MASTER_PLAN.md` Fase 6, hoje 🔶); decisão
  do legislador quando o crumb C1 for cogitado.
- **Tree-sitter para Neovim (B2):** nice-to-have, não bloqueia o `syntax/teko.vim` via regex.
- **Publicação em marketplaces** (VS Code Marketplace, MELPA): decisão de distribuição, diferida até o
  ferramental estar funcional localmente (E2/E4 cobrem instalação local sempre).

---

## Caminho crítico

**A (fonte única)** → **B (cores, 4 editores em paralelo)** → **E (empacotamento das partes de cor/build)**.
**C (LSP) está fora deste caminho** — diferido até a versão estabilizada do compilador; quando destravar,
segue **C → D1/D3-client/D4-client → E** (clients LSP). Duas exceções já entregáveis hoje, sem esperar
nada: **D2** (VS Code tasks + problemMatcher) e as partes `:make`/`compile` de **D3/D4** — a CLI já existe
e já emite `arquivo:linha:coluna`.

---

## Breadcrumbs (segmentação para agentes)

> Cada crumb = uma unidade de agente / um sub-PR. Eixos A, B, D e E vivem em `tooling/` (nunca tocam
> `src/`); o Eixo C é a exceção — vive em `src/lsp/` e segue a convenção normal do compilador (par
> `.tks`+`.c`/`.h` — SUPREME RULE) e está **diferido**. Convenção: todo crumb do Eixo A entrega gerador +
> saída regenerada; todo crumb do Eixo C entrega o par `.tks`/`.c`+`.h` + `.tkt` + smoke-test manual
> documentado.

### Eixo A — fonte única
- **[A1] Extrator de grammar-spec** — deps: — · par: `tooling/shared/` → lê `src/lexer/token.tks`, emite `grammar-spec.json`. **Aceite:** JSON contém todas as keywords/operadores atuais do lexer, sem hardcode paralelo.
- **[A2]–[A5] Geradores por editor** — deps: A1 · um crumb por editor (TextMate/Vim/Emacs/Nano). **Aceite:** cada saída gerada abre corretamente no editor-alvo sem edição manual pós-geração.

### Eixo B — cores
- **[B1]–[B4] Realce por editor** — deps: A2/A3/A4/A5 respectivamente. **Aceite:** um arquivo `.tks` de exemplo (`examples/`) mostra keywords, tipos, strings, comentários e literais numéricos coloridos distintamente no editor.

### Eixo C — intellisense (DIFERIDO — não pegar antes do marco de estabilização)
- **[C1] Esqueleto `teko lsp`** — deps: **versão estabilizada do compilador** (marco em aberto — ver Decisões) · par: `src/lsp/lsp.{tks,c,h}` + `lsp_test.tkt` (SUPREME RULE — par obrigatório enquanto o C for a base de bootstrap). **Aceite:** `teko lsp` responde a `initialize` e mantém o processo vivo sobre stdio, sem travar.
- **[C2] Diagnostics** — deps: C1. **Aceite:** abrir um `.tks` com erro de tipo mostra o erro sublinhado no editor, mesma mensagem da CLI.
- **[C3]–[C6] Hover/Completion/Definition/Symbols** — deps: C1 (podem paralelizar entre si). **Aceite:** cada recurso responde corretamente sobre um projeto de exemplo do corpus.
- **[C7] Semantic tokens** — deps: C2–C6. **Aceite:** realce via LSP substitui visualmente o regex do Eixo B quando ativo, sem regressão de cobertura de tokens.

### Eixo D — clientes + build
- **[D1] Client VS Code** — deps: C1 (**diferido**, ver Eixo C). **Aceite:** diagnostics/hover/completion aparecem na aba correta do VS Code.
- **[D2] Tasks + problemMatcher VS Code** — deps: — (CLI já existe). **Aceite:** "Teko: Build" roda `teko build .` e popula a aba Problems a partir de um erro real.
- **[D3] Client + `:make` Vim/Neovim** — client deps: C1 (**diferido**) · `:make` deps: — . **Aceite:** `:make` navega para `arquivo:linha:coluna` de um erro real via quickfix.
- **[D4] Client + `compile` Emacs** — client deps: C1 (**diferido**) · `compile` deps: —. **Aceite:** `M-x compile` navega para o erro via `next-error`.
- **[D5] Doc Nano** — deps: —. **Aceite:** um parágrafo no `tooling/nano/README` explica o fluxo de terminal-ao-lado.

### Eixo E — empacotamento
- **[E1]–[E5]** — deps: conforme tabela do Eixo E. **Aceite (cada um):** instalação local funcional documentada num README por editor.

---

## Estado
- **✓ Feitos:** nenhum — documento novo, define o roadmap.
- **▶ Prontos agora (sem dependência):** **A1** (extrator de spec) · **D2** (tasks/problemMatcher VS Code — a CLI já existe e já emite `arquivo:linha:coluna`) · as partes `:make`/`compile` de **D3/D4** · **D5** (doc Nano).
- **⏸ Bloqueado (não pegar ainda):** todo o **Eixo C** (C1–C7) e os clients LSP de **D1/D3/D4** — diferidos até a versão estabilizada do compilador (marco em aberto, ver Decisões).
- **Em seguida:** A1 → {A2, A3, A4, A5} → {B1, B2, B3, B4} → Eixo E; independentemente, D2 e as partes `:make`/`compile` de D3/D4 podem sair já. C1 só abre quando o marco de estabilização for declarado.
