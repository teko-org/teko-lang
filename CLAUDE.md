# Instruções permanentes — teko-lang

## Idioma (REGRA DURA, persistente)
O dono (schivei) **NÃO fala inglês**. **TODA** comunicação no chat é em **PT-BR**,
sempre. A conversa com o dono é PT-BR; código, commits, docs públicas (`docs/**`,
`README.md`, `CONTRIBUTING.md`) e nomes técnicos seguem a convenção do repo (inglês).

## Como perguntar (REGRA DURA)
**NUNCA usar quiz/menu de opções (ferramenta AskUserQuestion).** Toda pergunta é em
prosa curta, PT-BR. Quando o dono estiver respondendo outras coisas, **espere** —
não empilhe perguntas.

## Protocolo de fork (decisão, owner-gate, ambiguidade) — REGRA DURA
Antes de parar com dúvida, **TER CERTEZA** que não está já deliberado:
1. **Checar se já está deliberado** — buscar em `DECISION_LOG.md` (D211+ nesta raiz;
   D1–D210 em `docs/history/decision-log-legacy.md`), `docs/history/design/port-teko-mc.md`,
   `docs/history/handoff-2026-09.md`, `docs/history/design/pr-org-ngen.md`.
2. **Mais recente vence** — aplicar a decisão mais recente por data/ID.
3. **Só HALT em fork real** — parar e notificar o dono **apenas** quando NÃO existe
   deliberação, enunciando o fork curto e claro.

## O que é o repo
**Teko** é uma linguagem **ensinada ao `mc`** (minicompiler.dev, `minicompiler/mc`), não
um compilador próprio. O port é a raiz do repositório: 31 módulos `.tk` do pacote `teko`
+ `lib/rt.tk`, dirigidos por `core_teko.mc`/`user.mc`. **`mc` é pinado por `MC_VERSION`**
— baixe a release exata daquela versão, nenhuma outra roda.

O compilador standalone anterior foi **retirado e removido da árvore**
(`DECISION_LOG.md` D211/D212); seu registro fica em `docs/history/` (o
`decision-log-legacy.md`, o antigo `HANDOFF.md` e os planos de design), nunca reescrito.
Todo trabalho novo vive no port.

## Onde estão as docs

`docs/README.md` é o mapa: `guide/` (task-oriented), `reference/` (por lookup), `specs/`
(desenhado, não implementado), `internals/` (como o port é construído — hoje um ponteiro
para `docs/history/handoff-2026-09.md`, reescrito por assunto conforme os planos avançam)
e `history/` (registro congelado — o compilador antigo, `DECISION_LOG.md` D1-D210, os
planos de design). `sh scripts/check-docs.sh` prova a árvore: links resolvem, nenhuma
página viva cita caminho do compilador antigo, todo exemplo `teko` compilado tem oráculo,
e todo diagnóstico `teko: …` do fonte está listado em `docs/reference/diagnostics.md`.

## Como buildar e validar localmente

```sh
# 1. Ter o mc de MC_VERSION no PATH
cat MC_VERSION        # => x.y.z
mc --version          # => precisa dizer x.y.z

# 2. Derive o config do host (a partir de teko.toml)
sed -e "s#^os   =.*#os   = \"linux\"#" -e "s#^arch =.*#arch = \"x86_64\"#" \
    teko.toml >mc.host.toml

# 3. Build e run dos testes
mc build . --config mc.host.toml   # => constrói o compilador ensinado
for t in tests/*.tk; do
  n=$(basename "$t" .tk)
  w=$(grep -m1 '// expect-exit:' "$t" | sed 's/.*expect-exit: *//')
  sed -e "s#^entry = .*#entry = \"tests/$n.tk\"#" -e "s#^out   = .*#out   = \"build/$n\"#" \
      mc.host.toml >"mc.$n.toml"
  ./build/teko build . --config "mc.$n.toml" --entry-only && ./build/$n
  echo "$n exit=$?  want=$w"; rm -f "mc.$n.toml"
done

# 4. Fixpoint (teko0→teko1→teko2→teko3)
sh scripts/bootstrap.sh --os linux --arch x86_64
# Se printa "FIXPOINT OK", o compilador se reproduz.

# 5. Gate das docs
sh scripts/check-docs.sh
```

## Gates do CI obrigatório na org

| check | o que prova |
|---|---|
| `ngen (<os>/<arch>)` ×5 | cada perna (linux×2, macos/aarch64, windows×2) no runner nativo; `mc --host` asserido; 45 fixtures compiladas e executadas com exit code correto |
| `mc build ngen && run` | agregador que o ruleset exige; falha se qualquer perna falhar |
| `fixpoint (<os>/<arch>)` ×5 | teko0→teko1→teko2→teko3 sobre `mc_teko.tk`; `cmp` dos objetos byte-idêntico; `--dump-asm` idêntico; teko1 roda as 45 fixtures |
| `docs` | `sh scripts/check-docs.sh` sobre `docs/**` |
| `Branch policy gate`, `Analyze (actions)` | inalterados (CodeQL sem C/C++) |

## Leis de código do port

- **Zero mudança em `src/` do mc.** O `mc` é a base; teko ensina só o delta.
- **Zero intrínseco novo.** Toda função tem código próprio (`exp fn`, não hardcoded no backend).
  Se um construto "pede" intrínseco, é fork — parar e perguntar.
- **Toda fixture tem `// expect-exit: N`.** Sem oráculo não roda nem no CI.
- **Recusas com `teko: <causa curta>`.** Mensagem de erro padrão compilador: `arquivo:linha:coluna: "causa"`.
  Nada de história, referências ou explicação de design.
- **`--dump-ast` idêntico quando o crumb não muda código aceito.** Prova de no-op.
- **Superfície fora da v0.1.0 (recusada com mensagem, não implementada em silêncio):** `Func<>`/`Action<>`,
  multicast, `params T[]`, `T[][]`, `namespace` aninhado, DI genérica, float em `params`, `when` no `_` final.
- **Doc pública em inglês (`docs/**`, `README.md`, `CONTRIBUTING.md`); PT-BR só em
  `CLAUDE.md`, `DECISION_LOG.md` e `docs/history/`.**
- **Coordenação:** o coordenador não manda mensagem a agente em voo (`SendMessage` indisponível); errou →
  kill e re-dispatch limpo, nunca remendo em voo.

## Processo (uma passada scout → implementer)

Quando um crumb chega: (1) **SCOUT** verifica contra a raiz atual — as citações estão
certas? A superfície já está landada? As deps satisfeitas? Há drift? (2) **IMPLEMENTER**
só se scout disser PRECISA ou INCERTO, COM os achados do scout.

**Um agente por branch/worktree isolado.** Nunca compartilhe o checkout principal. Push
frequente na branch do agente; PR contra `main`.

## Canal com o mc (NOTICES)

Decisões que impactam o mc, ou pedidos de check do mc, vão a `NOTICES` no
`minicompiler/mc` (`NOTICES-teko.md` do repo do mc, lido no começo de cada lote).
Exemplos: mudança de hook signature, limite de feature do mc, pacote registrado, tool
`tekoc` vs lib `teko`. **O mc pinado é autoridade** — se o mc não compila uma feature, é
fork (parar e perguntar).

## Sem workarounds
Achar e resolver a **causa raiz**, nunca dar voltas para contornar. Se um dispatch saiu
errado, interromper e reiniciar certo — não remendar um agente em voo.
