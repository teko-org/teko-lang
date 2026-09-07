# Migração fork → org: gates, releases e o PR (recon 2026-09-06)

Levantamento somente-leitura feito para o desvio "v0.1.0 estável" (dono, 2026-09-06): mergear
`fix/retirement` na `main` do fork `schivei/teko-lang`, abrir PR para `teko-org/teko-lang`, e daí
trabalhar dentro da org. Fonte: `gh api` sobre rulesets/workflows dos dois repos + leitura dos
workflows em `fix/retirement`.

## 1. Veredito

Nada bloqueia tecnicamente. `upstream/main` e `origin/main` são ancestrais de `fix/retirement`
(2976/2975 commits à frente, 0 atrás): o PR fork:main → org:main é fast-forward, sem conflito. Os
riscos são operacionais: workflows legados ATIVOS na org (desativados no fork) acordam no merge, e o
estado `disabled_manually` não viaja entre repositórios.

## 2. Rulesets da org

| id | nome | alvo | estado | regras |
|---|---|---|---|---|
| 17770407 | `main` | `~DEFAULT_BRANCH` | ativo | deletion, non_fast_forward, pull_request (0 aprovações, `require_extra_approval_for_unattributed_changes`, **squash-only**) |
| 17650105 | `All Green` | `~ALL` − `fix/**`,`docs/**`,`recon/**`,`agent-**`,`worktree-**`,`chore/**`,`bump/*` | ativo | pull_request squash-only; SEM status checks; governa a `main` junto com o anterior |
| 18592155 | `Feature branch guardrail` | `fix/**`,`docs/**`,`recon/**` | ativo | non_fast_forward |
| 18529573 | `versions` | tags `v*` | ativo | criar permitido; mover/apagar não |
| 18592101 | `Merge gate` | `main`, `remodel/**` | **desativado** | exige `CI gate`, `Sanitizer gate` (do `pr.yml` congelado), `SAST gate`, `Heavy sanitizer gate (main)` — os dois últimos NÃO existem em workflow algum: reativar trava PR em pending eterno |
| 18593042 | `coverage` | — | desativado | apagar (`coverage.yml` some com o merge) |

Hoje nenhum ruleset ativo da org exige status check. Os contexts que os workflows vivos produzem:
`ngen (linux/x86_64)`, `ngen (linux/aarch64)`, `ngen (macos/aarch64)`, `ngen (windows/x86_64)`,
`ngen (windows/aarch64)`, `fixpoint (linux/x86_64)`, `fixpoint (linux/aarch64)`,
`fixpoint (macos/aarch64)`, `fixpoint (windows/x86_64)`, `fixpoint (windows/aarch64)` (o S4.3b levou
a escada aos cinco pares — plano §78), o agregador **`mc build ngen && run`** (o que o ruleset deve
exigir), `Branch policy gate` e `Analyze (actions)` (o CodeQL perdeu a perna `c-cpp` no R1:
compilava o `src/runtime/teko_rt.c` congelado).

## 3. Workflows: org × fork

| arquivo | org | fork | no merge |
|---|---|---|---|
| `ngen.yml` | ausente | ativo | novo, passa a rodar |
| `codeql.yml`, `branch-policy.yml` | ativo | ativo | ok |
| `pr.yml` | **ativo** | desativado | acorda (roda já no PR, sem `paths`) |
| `nightly.yml` | **ativo** | desativado | acorda a cada push |
| `release.yml` (legado) | **ativo** | desativado | dispara em tag `v*` |
| `tag-on-version-bump.yml` | **ativo** | desativado | `teko.tkp` muda no merge → deriva `v0.3.0.31-beta` (já existe) |
| `theory.yml` | ausente | desativado | novo na org, `on: push` |
| `mirror-pr-to-org.yml` | ativo | ativo | inerte na org; no fork dispara no push da `main` e abriria PR draft com o título do `docs/bump_v0.3.0.31.md` (release já publicada) |
| `seed-linux-fork.yml`, `theory-generation-decay.yml`, `reseed-bootstrap.yml` | ativo/ausente | desativado | só `workflow_dispatch`, inertes |
| `coverage.yml`, `agent-fast-lane.yml` | ativo | não existem | removidos pelo merge |

Como um `pull_request` roda os workflows da versão do PR, remover os legados no próprio PR (crumb R1)
evita que rodem nele.

## 4. Squash × merge

`origin/main..fix/retirement` = 2975 commits (2268 `Claude`, 301 `schivei`, 206 `Elton`, ~200 de
agentes). Squash-only no fork (19595705) e na org (17770407 + 17650105); `allow_merge_commit` está
desligado nos dois repos. Recomendação: manter squash — a história fica em `fix/retirement` (não
apagar; tag anotada `historia/pre-ngen-2975`), o commit único fica atribuído ao dono (resolve o
`unattributed` na org), e 2975 commits de campanha na `main` pública é ruído. Trocar o título default
do squash de #110 ("converge scattered native work…", obsoleto).

Mensagem sugerida:

```
ngen: o teko passa a ser uma linguagem ensinada ao mc (D211–D230)

Substitui o compilador próprio (`src/`, congelado) pelo port `ngen/`: o teko
vira um conjunto de módulos-hook do mc (minicompiler/mc), 31 módulos `.tk` +
`lib/rt.tk`, dirigidos por `core_teko.mc`/`user.mc`.

CI novo (`ngen.yml`): cinco pernas NATIVAS (linux/x86_64, linux/aarch64,
macos/aarch64, windows/x86_64, windows/aarch64), cada uma no runner do próprio
par, `mc --host` asserido, 45 fixtures executadas (`// expect-exit`);
agregador `mc build ngen && run`. Sexta perna `fixpoint`, nos MESMOS cinco pares:
teko0→teko1→teko2→teko3 sobre `ngen/mc_teko.tk`, `cmp` dos objetos, diff de
`--dump-asm`, 45 fixtures pelo teko1.

O CI legado do compilador antigo sai do repo; `src/` não se toca.
Squash de 2975 commits de `fix/retirement`; a história permanece nessa branch.
```

## 5. Corpo do PR para a org

```markdown
## O que este PR é

O teko deixa de ser um compilador próprio e passa a ser uma **linguagem ensinada
ao `mc`** (minicompiler.dev, `minicompiler/mc`). O port vive em `ngen/`: 31
módulos `.tk` do pacote `teko` + `lib/rt.tk`, dirigidos por `core_teko.mc` e
`user.mc`. Contexto: `docs/design/port-teko-mc.md`, `ngen/HANDOFF.md`, D211–D230.

## O que isto substitui

`src/` (o compilador self-hosted) está **congelado** e não é tocado por este PR
nem pelo trabalho seguinte. Fica no repositório como registro.

## Gates novos

| check | o que prova |
|---|---|
| `ngen (<os>/<arch>)` ×5 | cada perna no runner do próprio SO e arquitetura, `mc --host` asserido, 45 fixtures executadas — nada cross-compilado e deixado sem rodar |
| `mc build ngen && run` | agregador das cinco pernas; o nome que o ruleset deve exigir |
| `fixpoint (<os>/<arch>)` ×5 | os MESMOS cinco pares: teko0→teko1→teko2→teko3 sobre `ngen/mc_teko.tk`, `cmp` dos objetos, `--dump-asm` idêntico, 45 fixtures — o compilador se reproduz na máquina onde foi escrito |
| `Branch policy gate`, `Analyze (actions)` | inalterados (CodeQL sem a perna `c-cpp`) |

## Releases

Tag `v*` → `release.yml`: gate das pernas + fixpoint, assets
`teko-<ver>-<os>-<arch>.tar.gz` + `.sha256`, GitHub Release, publicação no
registro do mc gated por variável até o pacote `teko` estar registrado.

## O que a org precisa mudar

- Ruleset `main` (17770407): exigir `mc build ngen && run` (opcional: as cinco
  pernas `fixpoint`).
- Ruleset `Merge gate` (18592101): apagar ou reescrever (contexts fantasma).
- Ruleset `coverage` (18593042): apagar.
- Merge methods: manter squash-only (ou liberar `merge` em 17770407 + 17650105
  + settings do repo).
- `versions` (18529573): manter.
- Desativar na org os workflows legados que este PR remove do tree, caso
  ainda existam lá: `pr.yml`, `nightly.yml`, `release.yml` legado,
  `tag-on-version-bump.yml`, `theory.yml`, `seed-linux-fork.yml`,
  `theory-generation-decay.yml`, `reseed-bootstrap.yml`.
```

## 6. Checklist — só o dono

1. Decidir squash × merge (recomendado: squash) e trocar o título do squash de #110.
2. Desativar na org os 8 workflows legados — antes do merge, se possível (`pr.yml` roda já no PR).
3. Apagar/reescrever o ruleset `Merge gate`; apagar `coverage`.
4. Exigir `mc build ngen && run` no ruleset `main` da org.
5. Não apagar `fix/retirement`; cravar tag anotada `historia/pre-ngen-2975`.
6. Aprovar o extra-approval se a org pedir (`unattributed`).
7. Registrar o pacote `teko` em minicompiler.dev/me; criar a variável de Actions que o
   `release.yml` lê para liberar a publicação (nome no HANDOFF §3.1).
8. `ORG_PR_TOKEN` deixa de ser necessário quando `mirror-pr-to-org.yml` sair.

## 7. Registro do mc (resposta da sessão do mc, 2026-09-06)

O registro opera (`mc` 0.15.6..0.15.12 publicados). Para o `teko`: o manifesto do pacote tem que estar
na **raiz do repo** (`mc.toml` raiz com `[package]` e `lib`/`check`/`files` em `ngen/...`; subpath só
depois do R1 do registro); **sem `[project]`** no manifesto do pacote (a caixa roda sobre mc.toml + files);
validador com mc **pinado** (0.15.12); proposta `[package].mc = ">= x"`. Consequência: `[package]` sai de
`ngen/mc.toml` (que fica com `[project]`/`[compiler]`/… para `mc build ngen`) e vai para `/mc.toml`.

**R1 do registro (2026-09-06 18:50Z):** kind pelo `[project]` (`ausente`/`obj` = lib; `exe` = tool; presente sem
kind = recusado; fixo pelo nome na 1ª publicação); `[[permission]]`/`[tools]` validados; subpath `ngen/` via
admin. Consequência: o manifesto do pacote-biblioteca `teko` NÃO pode carregar o `[project] kind = "exe"` de
hoje (viraria tool); a ferramenta (binário `teko`) precisa de nome próprio — fork g4 no D230 (proposta: lib
`teko`, tool `tekoc`).
