# Plano — documentação da teko refeita do zero + site (passo 5 do rebase)

Escopo ampliado pelo dono (2026-09-06): **toda a documentação refeita, legado expurgado, e o SITE
reconstruído**. Base: `origin/main` `7bb8643c` na org `teko-org/teko-lang`, com o PR #660 (split
`mc.toml` = só `[package]` / `teko.toml` = build) assumido como mergeado. Molde: o repo do `mc`
(somente leitura) — `docs/README.md` como mapa, `guide/` numerado, `reference/` por lookup,
`specs/`, `scripts/check-docs.sh` como portão, e `mcsite` (`site/gen/*.mc`) como gerador.
Fonte da superfície: `docs/design/superficie-v0.1.0.md` (inventário construto→fixture) e
`tests/*.tk` (a superfície REAL, com `// expect-exit`).

Regra que governa tudo: **a doc fala só do que existe hoje e é PROVADO por fixture**; o não
ensinado aparece só como recusa (`reference/diagnostics.md`) ou desenho marcado (`specs/`).

## 1. A árvore nova de `docs/`

```
docs/  README.md (o mapa)  guide/NN-*.md (em ordem)  reference/*.md (por lookup)
       specs/*.md (desenhado, ainda NÃO existe)  internals/*.md (o que hoje é o HANDOFF)
       history/*.md (congelado)  brand/ (inalterado, consumido pelo site)
```

### 1.1 `guide/` (nasce; ordem = leitura)

| página | conteúdo |
|---|---|
| `00-getting-started.md` | instalar o `mc` PINADO (`cat MC_VERSION`, release de `minicompiler/mc`, checksum); dois modos de consumir a teko — `[deps] teko = "x.y.z"` + `#include <teko>` (runtime) e `[compiler] modules = ["<teko/teko.tk>", "user.mc"]` (compilador ensinado); primeiro programa; `teko build`; o exit code como resultado |
| `10-project.md` | `teko.toml` (build) × `mc.toml` (pacote); `[project]/[target]/[compiler]/[linker]/[include]/[limits]`; derivar o config do host; `teko limits` |
| `20-types.md` | `class`, `struct`, campos, métodos, `this` implícito, `base`, construtor/destrutor, `new`, `virtual`/`override`, `abstract`, `partial`, modificadores (`public`/`private`/`protected`/`internal`/`static`) |
| `30-interfaces-traits.md` | `interface` (corpo default, `static abstract`, herança), `trait` (modelo PHP, `use`), propriedades, sobrecarga de operador |
| `40-generics.md` | `class Box<T, const N: i64>`, instanciação, aninhamento e `>>`, mangling visível nos erros |
| `50-delegates-lambdas.md` | `delegate`, lambda explícita, função local, captura `use (a, &b)`, `null` e o pânico |
| `60-arrays.md` | array fixo, `T[]` de heap, `new T[n]`, `.Length`, array global, campo array inline, `foreach`, guard de índice |
| `70-namespaces.md` | `namespace` (bloco e file-scoped), `using`, `import`, função livre em namespace, `internal` = código do projeto |
| `80-control-flow.md` | `loop`/`break N`/`continue`, `while`, `do…while`, `for`, `switch` statement e expression, `when`, ternário |
| `85-parameters.md` | default, sobrecarga (método e função livre), `ref`, `out`, `params` |
| `90-di.md` | DI em compile-time: `IServiceSingleton`/`IServiceScoped`/`IServiceTransient`, `inject`, `scope { }`, o grafo por construtor |
| `95-memory.md` | arena de 4 MiB, refcount por escopo, posse, destrutor, `panic` e `exit 70` |
| `98-fixpoint.md` | `scripts/bootstrap.sh`: teko0→teko1→teko2→teko3, os três critérios, o que o ponto fixo prova |

### 1.2 `reference/` (nasce; derivado do inventário `superficie-v0.1.0.md` §1)

`language.md` (léxico, primitivos `bool/byte/char/isize/usize/ptr/uptr/str/f32/f64`, expressões,
escopo, controle de fluxo) · `types.md` (class/struct/interface/trait/genéricos/membros/
propriedades/operadores) · `functions.md` (funções livres, sobrecarga, default, `ref`/`out`/
`params`, delegates/lambdas) · `arrays.md` · `namespaces.md` · `di.md` · `memory.md` ·
`runtime.md` (o que `lib/rt.tk` exporta: `panic`, `rt_alloc`/`rt_free`, `rc_inc`/`rc_dec`,
`rt_used`/`rt_live`/`rt_peak`, `tk_str_len`/`tk_str_slice`, `tk_f64_bits`/`tk_f64_from_bits`, …) ·
`cli.md` (`teko build [DIR] [--config FILE] [--entry-only] [--compiler-only]`, `teko limits`,
os dumps herdados do núcleo, exit codes) · `toml.md` (toda chave de `teko.toml` e de `mc.toml`
`[package]`) · `diagnostics.md` (**toda** mensagem `teko: …`, com causa e conserto).

Cada linha vira: grafia · semântica · **limite conhecido** · fixture que prova.

### 1.3 `specs/` (o que está desenhado e NÃO existe)

`self-host.md` (D225: recriar o compilador das partes do mc, `subcommand`, auto-hospedagem) ·
`di.md` (D229, o desenho além do implementado) · `distribution.md` (D230 + adendos: lib `teko` sem
`[project]`, tool `tekoc` `kind="exe"`, registro `pkg.minicompiler.dev`, `mc tool install`) ·
`params-t.md` (de `plano-params-t.md`) · `roadmap.md` (a lista FORA da v0.1.0, §2 do inventário).

### 1.4 `internals/` (o HANDOFF, reescrito por assunto)

`README.md` (a teko como módulo do mc: os 31 `teko_*.tk`, o que cada um ensina, ordem de
`#include`, `core_teko.mc`/`user.mc`) · `passes.md` (o que é parse e o que é `pass()`, o oráculo
de tipo `teko_typeof.tk`/`tk_xt`, por que o `.` é deferido) · `rc.md` (RC e posse resolvidos no
pass, vtable, `refcount@+8`) · `bootstrap.md` (a escada, os critérios, provenance) ·
`ci.md` (5 pernas nativas + 5 `fixpoint`, agregador `mc build ngen && run`, `setup-mc`,
`windows-sysroot`, `release.yml`, bump de `MC_VERSION`) · `pitfalls.md` (as armadilhas do
HANDOFF §5.1, item a item) · `debts.md` (dívidas conhecidas, hoje espalhadas no §5).

### 1.5 Expurgo — o que morre, o que vira histórico

| hoje | destino |
|---|---|
| `docs/design/superficie-v0.1.0.md` | **absorvido** por `reference/*` + `specs/roadmap.md` → `docs/history/` |
| `docs/design/plano-ngen-entrega4.md` (5 060 linhas), `port-teko-mc.md`, `plano-v1-float-callp.md`, `plano-rebase-raiz.md`, `pr-org-ngen.md` | `docs/history/` intactos (registro do como se chegou aqui) |
| `docs/design/plano-params-t.md` | vira `specs/params-t.md` (é desenho vivo, não histórico) |
| `docs/design/draft-CLAUDE.md`, `draft-CONTRIBUTING.md` | viram `CLAUDE.md` e `CONTRIBUTING.md` na raiz; os drafts somem |
| `docs/design/` como diretório | **deixa de existir** (vira `docs/history/`) |
| `HANDOFF.md` (3 423 linhas) | **morre como arquivo**: reescrito em `internals/*` + `CONTRIBUTING.md`; o texto original vai INTEIRO para `docs/history/handoff-2026-09.md`, sem reescrita — nada se perde, só deixa de ser a porta de entrada |
| `DECISION_LOG.md` (530 KB) | **fica na raiz**, mas cortado: **D211+ ficam** (o port); D1–D210 vão para `docs/history/decision-log-legacy.md` (o compilador aposentado) |
| `README.md` | reescrito: 40 linhas + links para `docs/` e para o site |

## 2. `site/` — o site da teko

Espelha o mc: `site/site.toml`, `templates/{base,page,home,404}.html`, `static/{site.css,search.js,
icon.svg,favicon.svg,social.svg,icon-*.png,social.png}`, `home-extra.md` e `tools/{checkhtml,
contrast}.py` (ausentes, o `--check` os PULA — copiá-los é o que mantém a11y/contraste no portão).
Ativos derivados de `docs/brand/` (`logo.svg`, `mascot.svg`, `poses/sticker.svg` no ícone) e a
paleta pastel do `docs/brand/README.md` (`#F2A59C`, ink `#2B2027`, fundo `#FDF3EC`) na `site.css`.

```toml
[site]
title    = "teko"
tagline  = "a C#-like language taught to a compiler that compiles itself"
description = "teko is a C#-like surface language taught to mc as hook modules: classes, interfaces, traits, generics, delegates, compile-time dependency injection and scope-based reference counting, on five native (os, arch) pairs."
base_url = "/"
origin   = "https://teko-lang.org"
title_separator = " - "
year     = "2026"
edit_url = "https://github.com/teko-org/teko-lang/blob/main/"
search   = true
home     = "home-extra.md"
highlight = ["teko", "tk"]
docs = "../docs"   out = "public"   templates = "templates"   static = "static"   repo = ".."
nav  = ["guide", "reference", "specs", "internals"]

[section.guide]     title = "Guide"      dir = "guide"     url = "guide/"
[section.reference] title = "Reference"  dir = "reference" url = "reference/"
[section.specs]     title = "Specs"      dir = "specs"     url = "specs/"
[section.internals] title = "Internals"  dir = "internals" url = "internals/"
```

**Como o gerador entra — recomendação (b): NÃO duplicar `site/gen/*.mc`.** O `site.yml` faz
checkout de `minicompiler/mc` na tag `v$(cat MC_VERSION)`, constrói o `mcsite` DE LÁ com o `mc`
de release e o roda sobre o `docs/` + `site.toml` da teko. Razões: (i) `mcsite [DIR]` já aceita
qualquer diretório com `site.toml` e resolve todo caminho relativo a esse arquivo — nada nele
aponta para o repo do mc; (ii) evita vendorar ~3 000 linhas de `.mc` sob outra licença (mc = MIT,
teko = MIT/Apache-2.0 dual) e o dever de mantê-las; (iii) o gerador acompanha a versão que o
`MC_VERSION` já pina, e um bump é um lugar só. Custo: o site depende de um segundo repo no CI.

```yaml
name: Site
on:
  push: { branches: [main], paths: ["site/**", "docs/**", ".github/workflows/site.yml"] }
  workflow_dispatch:
permissions: { contents: read, pages: write, id-token: write }
concurrency: { group: pages, cancel-in-progress: false }
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v5
      - uses: actions/configure-pages@v5
      - uses: ./.github/actions/setup-mc
        with: { os: linux, arch: x86_64, asset: x86_64, token: "${{ secrets.GITHUB_TOKEN }}" }
      - name: Check out the pinned mc (the mcsite generator)
        uses: actions/checkout@v5
        with: { repository: minicompiler/mc, ref: "v${{ steps.mc.outputs.version }}", path: _mc }
      - name: Build mcsite and render docs/
        run: |
          set -eu
          (cd _mc && "$GITHUB_WORKSPACE/${{ steps.mc.outputs.mc }}" build site --config site/mc.linux.toml)
          _mc/build/mcsite site --check
      - run: rm -rf public && cp -R site/public public && echo teko-lang.org >public/CNAME
      - uses: actions/upload-pages-artifact@v3
        with: { path: public }
```

O job `deploy` é o do mc, sem mudança: `needs: build`, `environment: github-pages`,
um passo `actions/deploy-pages@v4`.

Configuração do Pages no repo (uma vez, dono ou admin da org):

```sh
gh api -X POST repos/teko-org/teko-lang/pages -f build_type=workflow
gh api -X PUT  repos/teko-org/teko-lang/pages -f cname=teko-lang.org -F https_enforced=true
```

DNS (Cloudflare, dono/sessão do mc): `teko-lang.org` → Pages (A `185.199.108-111.153` + AAAA
`2606:50c0:800x::153`); `.com`/`.online` → 301 para o canônico; `painel.teko-lang.online` → VPS.

## 3. Portões

**`scripts/check-docs.sh`** (novo, no molde do mc, rodando no CI e localmente):

1. **Links** — todo link relativo em `docs/` resolve; `/static/*` resolve em `site/static/`.
2. **Amostras** — todo fence ` ```teko ` é COMPILADO pelo compilador ensinado (`build/teko`);
   com `// expect-exit: N` é linkado e **executado**, e o código conferido; com
   `// expect-error: <texto>` tem de ser RECUSADO com esse texto. Fence marcado
   `nocompile` (fragmento ilustrativo) é o único isento, e o script conta quantos são.
3. **Cobertura, extraída do FONTE (nunca escrita no script):** toda string `"teko: …"` que
   aparece em `*.tk` tem de aparecer em `reference/diagnostics.md`; toda chave que os módulos
   leem do TOML tem de aparecer em `reference/toml.md`; todo `tests/*.tk` tem de ser citado por
   alguma página (é o que amarra doc↔fixture).
4. **Proibições** — zero ocorrência de `ngen/`, `src/`, `.tks`, `teko.tkp`, `fetch_teko.sh`,
   `bootstrap/teko.c` fora de `docs/history/`.
5. **`mcsite --check`** no `site.yml` (links internos + `checkhtml.py` + `contrast.py`).

## 4. Sequência de crumbs (cada um = PR squash na org, CI verde, revisão do Copilot)

| # | entrega | tam. | autor sugerido | precisa de |
|---|---|---|---|---|
| **D1** | esqueleto `docs/{guide,reference,specs,internals,history}/` + `docs/README.md` (mapa) + expurgo (§1.5: `design/`→`history/`, HANDOFF→`history/handoff-2026-09.md`, corte do DECISION_LOG) + `scripts/check-docs.sh` (checks 1, 4 e 3-parcial) ligado no CI | M | teko-docs (haiku) — é mecânico | — |
| **D2** | `guide/` inteiro (13 páginas), cada exemplo com fixture equivalente | L | opus (prosa técnica + exemplos que TÊM de compilar) | D1 |
| **D3** | `reference/` inteiro (11 páginas) a partir do inventário + `diagnostics.md` extraído do fonte; liga o check 2 (amostras) e o 3 (cobertura) | L | opus | D1 |
| **D4** | `internals/` (7 páginas, do HANDOFF) + `specs/` (5) + `CLAUDE.md` e `CONTRIBUTING.md` finais + `README.md` da raiz | L | opus | D1 |
| **S1** | `site/` (site.toml, templates, static com a marca, home-extra, tools) + `.github/workflows/site.yml` + Pages | M | teko-docs (haiku) para copiar/adaptar; opus revisa a marca | D1; **dono/mc**: DNS, domínio no Pages, validação de domínio |

D2/D3/D4 são independentes entre si depois do D1 e podem ir em paralelo em worktrees separadas;
S1 pode começar junto do D2 (o `mcsite` renderiza seção vazia sem quebrar link).

## 5. Riscos e decisões que precisam do dono

1. **IDIOMA — decisão do dono.** O `mc` é inglês; o README, o `mc.toml` e as fixtures da teko já
   são inglês; o HANDOFF e o DECISION_LOG são PT-BR. **Recomendação:** `docs/**`, `README.md`,
   `CONTRIBUTING.md` e o site em **inglês** (público, e o vocabulário técnico já é o do mc);
   **PT-BR** só em `CLAUDE.md`, `DECISION_LOG.md` e `docs/history/`. Sem ruling, D1 assume isto.
2. **Corte do DECISION_LOG em D211** (o port) — precisa de OK do dono; alternativa é não cortar.
3. **Morte do `HANDOFF.md` como arquivo** — precisa de OK. O risco real é perder o *tom de log*
   (medições datadas, "armadilha já paga", provenance de cada release do mc): mitigado por copiar
   o arquivo inteiro para `docs/history/` e reescrever só a parte operacional.
4. **Docs citando o que não existe** — a lista FORA (`superficie-v0.1.0.md` §2: `Func<>`/`Action<>`,
   multicast, `params T[]`, `T[][]`, `namespace` aninhado, DI genérica, float em `params`, `when`
   no `_` final, `using static`, genérico qualificado) só pode aparecer em `diagnostics.md` (com a
   mensagem exata) ou em `specs/`. O check 2 (amostras compiladas) é o que torna isso mecânico.
5. **Acoplamento site↔`MC_VERSION`**: um bump do mc pode quebrar o `mcsite` (o gerador vive no
   repo do mc). O `site.yml` não é gate de PR, então quebra não barra merge — mas o bump de
   `MC_VERSION` passa a ter um terceiro verde a conferir (§`internals/ci.md`).
6. **Realce de sintaxe**: o `mcsite` realça com o lexer do **mc**; `class`/`public`/`interface`
   sairão como identificadores. Aceito como cosmético; ensinar as palavras da teko ao realce é
   pedido ao mc, fora deste plano.
7. **Nome literal `mc build ngen && run`** (agregador exigido pelo ruleset) e `[package].files`
   (o hash de árvore) **não são tocados** por nenhum crumb daqui — `docs/` e `site/` ficam fora
   de `files`, então o hash do pacote não muda.
8. **Pergunta ao mc**: confirmar que `mcsite` rodado de FORA do repo do mc não resolve nada
   relativo ao próprio repo (templates/static/tools vêm do `site.toml` da teko) e se a licença
   MIT cobre a cópia de `templates/`, `static/site.css` e `tools/*.py` para o repo da teko.
