# LSP + tooling de editor — o que está DIFERIDO (para não esquecer)

Registro do dono (2026-08-03): o **servidor** LSP está pronto; o que falta é lado
**cliente/editor** e cores, adiado deliberadamente. Este ficheiro existe para que a
retomada não redescubra por acidente.

## Feito (não mexer, só referência)

- **Servidor `teko lsp`** — crumbs 0/A–G/D completos e testados, confirmado por
  compilação real da árvore inteira pela rota C (checker 9651/9651 ✓): stdin JSON-RPC,
  diagnósticos ao vivo (`didOpen/Change/Close` → `publishDiagnostics`), formatação
  (`fmt::format_source` in-process), warnings coletados, `Diagnostic`/`DiagLevel`
  estruturado, índice de símbolos + overrides, hover/goto-def, completion. Vive em
  `src/lsp/` + `src/checker/lsp_api.tks` + `src/checker/warnings.tks`.
- **`teko lsp --help`** — página de uso (drenado em fix/union, ex-`lsp-close-help`).

## Diferido — trabalho real que FALTA (por ordem de valor)

1. **Cliente de editor** (o maior item). VS Code `LanguageClient`, Vim/Neovim, Emacs,
   Nano. É **TypeScript / glue de editor** — o design (`docs/design/lsp-teko.md`)
   resolve a tensão de lei (TS é cola de editor, não viola Teko-only), mas **exige um
   agente NÃO-Teko** para escrever. `tooling/vscode/package.json` hoje é grammar-only
   (sem `main`, `activationEvents`, `vscode-languageclient`).
2. **🔴 SEGURANÇA (fazer junto do cliente VS Code):** `tooling/vscode` (tasks/extension)
   usa `cp.exec` com string interpolada → **injeção de comando**. Trocar por
   `execFile`/`spawn` com argv em array. Achado do scout 2026-08-03.
3. **Crumb H — cores TextMate ricas.** `tooling/shared/grammar-spec.json` só tem um
   bucket plano `"keywords"`; falta `entity.name.type`/`function`/`namespace`, atributo
   `#test`, keywords contextuais (`ref`/`adopt`/`unsafe`). **Bloqueado por falta de
   harness de verificação de gramática TextMate** — criar o meio de PROVAR a gramática
   gerada antes de escrevê-la (senão é prosa não verificada, proibida por lei).
4. **Crumb I — semantic tokens.** Fase 2 explícita do próprio design; não bloqueia v1.
   `initialize_result` ainda não anuncia `semanticTokensProvider`.
5. **Smoke `.tkr` fim-a-fim do LSP** (§9.2 do design, exit codes 0/1/2 via subprocesso).
   Não existe; criar precisa de fixture nova (hoje a lei proíbe novos `.tkp` de
   regressão) ou infraestrutura ainda a criar.

## Como VALIDAR o cliente de editor (ideia do dono, 2026-08-03)

Teste unitário `.tkt` não cobre uma integração de editor — o certo é rodar o VS Code
de verdade e conferir a UI. Abordagem viável NESTE ambiente (Chromium + Playwright já
pré-instalados; `PLAYWRIGHT_BROWSERS_PATH=/opt/pw-browsers`, não rodar `playwright install`):

1. Subir o VS Code em **versão web-server** — `openvscode-server` ou `code-server`
   (VS Code no browser, extensões funcionam).
2. Carregar a extensão `teko` e apontar o `LanguageClient` para o `teko lsp` (rota C,
   que compila e roda; não depende do backend nativo).
3. Abrir um `.tks` e **dirigir com Playwright** (Chromium headless) para ASSERTAR que
   hover, completion, diagnósticos ao vivo e formatação aparecem de fato na UI.

Isso é o end-to-end visual do lado cliente que os `.tkt` não cobrem — a versão UI do
smoke `.tkr` que o §9.2 do design pede. **Ressalva:** instalar `openvscode-server`
pode esbarrar na política de rede DO SANDBOX LOCAL — verificar no momento de fazer.

### Instrumentar em CI (GitHub Actions suporta — dono 2026-08-03)

Sim, dá pra virar gate de CI. Duas rotas:

- **Padrão, determinística (recomendada):** `@vscode/test-cli` + `@vscode/test-electron`
  baixa o VS Code real e roda **headless via `xvfb-run`** num runner Ubuntu, com a
  extensão carregada; asserções pela **Extension API** (hover/completion/diagnostics
  RESOLVEM?). É o jeito canônico e não-flaky de testar extensão VS Code em CI.
- **Web/visual:** `@vscode/test-web` (web-extension headless) OU
  `openvscode-server`/`code-server` + **Playwright** dirigindo o Chromium — para o sabor
  navegador / asserção visual.

Detalhes: o `teko lsp` (rota C) sobe como o language server, **independe do backend
nativo**. O runner Ubuntu do GH tem **rede aberta** (baixa VS Code/openvscode/npm), então
**o gate roda em CI mesmo que não rode no sandbox local**. Custo real (baixa VS Code +
browser, minutos) → **job dedicado, não em todo push**; preferir asserção via Extension
API (determinística) a pixel-diff.

## Dívida de doc a corrigir junto

`TEKO_ROADMAP_TOOLING.md` está **desatualizado**: ainda lista C1 (esqueleto `teko lsp`)
como "diferido", quando já está feito. Atualizar quando esta frente for retomada.
