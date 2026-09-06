# Plano: expurgo do legado + rebase de `ngen/` para a raiz (na org, pós-PR)

Decisão do dono (2026-09-06): depois do PR para `teko-org/teko-lang`, remover o legado do compilador
antigo e fazer de `ngen/` a raiz do repositório. Recon somente-leitura contra `eb18f61b`, com probe
local do split do manifesto (mc 0.15.12, macOS/aarch64). Este é o plano do crumb; executa-se na org.

## 1. Inventário do legado (≈ 42 MB, ≈ 1 250 arquivos rastreados) — zero referência viva

`bootstrap/` (21 M), `docs/` menos as exceções do §1c (~12 M, 316 files), `src/` (4,7 M, 233),
`.crumbs/` (220), `examples/` (332), `TEKO_*.md` (22), `scripts/` (67 — nenhum workflow vivo invoca
script da raiz), `tooling/` (52), `DECISION_LOG.md` (citado só em prosa por `ngen/README.md`,
`ngen/mc.toml`, `teko_type.tk`, `primitives_*.tk`, `lib/rt.tk`, `ngen.yml` — decidir se fica como
registro histórico ou vira `docs/design/`), `REBOOT_PLAN.md`, `CLAUDE.md` (100% do compilador
antigo: reescrever do zero, ~1 página, só o meta-processo: PT-BR, sem AskUserQuestion, protocolo de
fork, scout→implementer, um agente por branch/worktree), `cases/`, `.claude/` (agents/skills do fluxo
antigo), `regressor.tkr`, `install.sh`, `INSTALL.md`/`NOTES.md`/`COMPILE.md`/`EXPURGO-PENDENTES.md`,
`teko.tkp`, `main.tks`, `packaging/`, `.github/{ci-lane-exceptions,sast-baseline,lsan-suppressions}.txt`.

### 1b. Fica (raiz cívica)

`LICENSE*`, `CODE_OF_CONDUCT.md`, `CONTRIBUTING.md` (reescrever), `GOVERNANCE.md`, `PRIVACY.md`,
`SECURITY.md`, `SUPPORT.md`, `CITATION.cff`, `.gitattributes` (podar o parágrafo morto dos seeds
`xz`), `.gitignore` (podar ~90% de regras do compilador antigo; remover `!.crumbs/`/`!.claude/` se
saírem; `build/` já cobre a raiz), `.github/{CODEOWNERS,FUNDING.yml,ISSUE_TEMPLATE/,PULL_REQUEST_TEMPLATE.md}`.

### 1c. `docs/` que NÃO pode sumir

`docs/design/plano-ngen-entrega4.md` (29 citações vivas), `docs/design/port-teko-mc.md` (16),
`docs/design/pr-org-ngen.md`, este arquivo, e `docs/brand/` (o README público usa `logo.svg`,
`mascot.svg`, `poses/*.svg`). `docs/design/embed-vfs.md`, `docs/BUILDING.md`, `docs/{memory,canonical,
medicoes}` = legado. Atenção: citações a `docs/reference/hooks.md`, `docs/build.md`,
`docs/reference/packages.md`, `docs/guide/*` dentro de `ngen/` são do repo do **mc**.

## 2. Mapa de caminhos (`ngen/` → raiz)

| arquivo | linhas com `ngen/` | natureza |
|---|---:|---|
| `ngen/HANDOFF.md` | 188 | receitas + prosa |
| `.github/workflows/ngen.yml` | 61 | estrutural |
| `ngen/scripts/bootstrap.sh` | 40 | estrutural |
| `ngen/README.md` | 24 | vira o `README.md` da raiz |
| `.github/actions/package-teko/action.yml` | 7 | `ngen/build/teko`, `ngen/lib/rt.tk`, `ngen/README.md` |
| `.github/actions/setup-mc/action.yml` | 6 | `ngen/MC_VERSION` → `MC_VERSION` |
| `.github/workflows/release.yml` | 5 | `pkg hash ngen`, `(cd ngen && …)`, `ngen/mc.toml` |
| `.github/workflows/codeql.yml` | 3 | `paths: ['ngen/**','.github/**']` → `['**']` ou sem filtro |
| `.github/actions/windows-sysroot/action.yml` | 3 | default `ngen/build/sysroot/windows-<arch>` |
| `ngen/mc.toml`, `ngen/scripts/measure.sh` | 4, 2 | comentários/receitas |
| `docs/design/*.md` | 206+ | só texto histórico — não tocar |

`ngen` NU como argumento DIR (não casa com `s#ngen/##g`, tratar à mão): `ngen.yml:257,307`,
`release.yml:299,319`, `bootstrap.sh:251,256,262,267,315`, README/HANDOFF (`mc build ngen`,
`mc pkg hash ngen`, `mc limits ngen`).

**Não muda:** `#include "../lib/rt.tk"` (42×, todas em `tests/*.tk` de profundidade 1),
`#include "parts/…"`, `[include] paths = ["lib"]`, `[package].files` (já sem prefixo),
`[project].entry = "tests/hello.tk"`, `[compiler].out = "build/teko"`.

## 3. Split do manifesto (regra do registro, D230 adendos 2/3) — VALIDADO

Docs do mc: `--config FILE` aceita qualquer nome e todo caminho é relativo ao diretório do config
(`toml.md`); o repo do mc é o precedente exato (`mc.toml` raiz só `[package]`, sem `[project]`;
`mc build .` dá `missing key: project.entry` de propósito; builds reais por `src/mc.<target>.toml`);
`mc pkg hash DIR` não tem `--config` → `[package]` TEM que ficar no `mc.toml` da raiz.

```
/mc.toml    -> só [package]  (name="teko", lib="lib/rt.tk", files=[…], check=["mc_teko.tk"]); sem [project]
/teko.toml  -> [project] [target] [compiler] [linker] [limits] [include];  mc build . --config teko.toml
```

Probe (`_probe/root/` = cópia de `ngen/`): `mc build . --config teko.toml` constrói; `mc build .`
recusa como no mc; 4 fixtures exit 42; `bootstrap.sh` adaptado → **`FIXPOINT OK`, 45/45**;
`mc pkg list/verify` ok. `mc pkg hash` fica para o CI.

**Regra dura do `--config`:** relativo, sem `./`, na RAIZ do pacote. `tk_access_init`
(`teko_access.tk:73-101`) toma a raiz do diretório do config: `teko.toml` → "o projeto é o diretório
inteiro" (correto); `./teko.toml` → o guard de `internal` fica cego (D224); `cfg/teko.toml` →
`cannot open: cfg/core_teko.mc` (quebra). `build/teko.toml` está descartado.

Efeitos: split + rebase mudam os bytes de `mc.toml` → nova tree hash (sem consequência antes da 1ª
publicação); o `awk` que remove `[linker]` passa a derivar de `teko.toml` e o config do CI deixa de
carregar `[package]` inútil; `release.yml` segue lendo `check`/`lib` de `mc.toml` (só some o prefixo).

**Pergunta ao mc (fork g4):** com a raiz = lib `teko`, o tool `tekoc` precisa de segundo manifesto —
o registro aceita dois subpaths do mesmo repo (raiz e `tool/`) ou o tool vai para outro repo?

## 4. Ordem (branch única, um commit por passo, CI verde a cada passo)

1. **Remover o legado** (§1) + podar `.gitignore`/`.gitattributes` — gate: 5 pernas + fixpoint
   (nada muda para o ngen). Remover ANTES do move elimina as duas únicas colisões (`scripts/`, `README.md`).
2+3. **`git mv ngen/* .` + ajustar caminhos** (`.github/**`, `scripts/bootstrap.sh`, `measure.sh`,
   CodeQL `paths`) **num único commit** (o `mv` sozinho deixa o CI vermelho) — gate: 5 pernas +
   `bootstrap.sh` (fixpoint + 45/45).
4. **Split do manifesto** (`mc.toml` = `[package]`; `teko.toml` = build; CI deriva de `teko.toml`) —
   gate: 5 pernas + `bootstrap.sh` + pré-voo do `check` no `release.yml`.
5. **Docs**: `README.md` raiz (do `ngen/README.md`), `HANDOFF.md` na raiz, `CLAUDE.md` novo,
   `CONTRIBUTING.md`; nota de rebase nos `docs/design/*` (sem reescrever histórico).

## 5. Riscos

- **R2 (crítico):** o agregador chama-se `mc build ngen && run` e é o único check exigido pelo ruleset
  de `main`. **Manter o nome literal** neste crumb; renomear só em passo coordenado com o ruleset da org.
- **R5:** CodeQL `paths` que nunca casa deixa PR em pending eterno se o ruleset de code scanning for
  ligado — trocar por `['**']` ou remover.
- **R7:** `mc.ci.toml`/`mc.<fixture>.toml`/`mc.linker.toml` passam a nascer na raiz — adicionar
  `/mc.*.toml` (menos `mc.toml`) ao `.gitignore`.
- **R4:** `HANDOFF.md` na raiz (destino mecânico do `s#ngen/##g`).
- **R8:** confirmar o ruleset `others` do fork/org por `gh api` antes do passo 2+3.
