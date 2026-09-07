# Instruções permanentes — teko-lang (pós-rebase, raiz do repo)

## Idioma (REGRA DURA, persistente)
O dono (schivei) **NÃO fala inglês**. **TODA** comunicação no chat é em **PT-BR**,
sempre. A conversa com o dono é PT-BR; código, commits e nomes técnicos seguem a
convenção do repo.

## Como perguntar (REGRA DURA)
**NUNCA usar quiz/menu de opções (ferramenta AskUserQuestion).** Toda pergunta é em
prosa curta, PT-BR. Quando o dono estiver respondendo outras coisas, **espere** —
não empilhe perguntas.

## Protocolo de fork (decisão, owner-gate, ambiguidade) — REGRA DURA
Antes de parar com dúvida, **TER CERTEZA** que não está já deliberado:
1. **Checar se já está deliberado** — buscar em `DECISION_LOG.md` (D225–D230 relevantes para ngen),
   `docs/design/port-teko-mc.md`, `HANDOFF.md`, `docs/design/pr-org-ngen.md`.
2. **Mais recente vence** — aplicar a decisão mais recente por data/ID.
3. **Só HALT em fork real** — parar e notificar o dono **apenas** quando NÃO existe deliberação,
   enunciando o fork curto e claro.

## O que é o repo
**Teko** é uma linguagem **ensinada ao `mc`** (minicompiler.dev, `minicompiler/mc`), não um
compilador próprio. O port vive na raiz pós-rebase: 31 módulos `.tk` do pacote `teko` +
`lib/rt.tk`, dirigidos por `core_teko.mc`/`user.mc`. **`mc` é pinado por `MC_VERSION`**
— baixe a release exata daquela versão, nenhuma outra roda.

**`src/` está CONGELADO.** Não é tocado, é registro histórico. Todo trabalho novo vive no port.

Contexto: `docs/design/port-teko-mc.md`, entradas D211–D214 e D225–D230 de `DECISION_LOG.md`.

## Como buildar e validar localmente (receita pós-rebase)

```sh
# 1. Ter o mc de MC_VERSION no PATH
cat MC_VERSION        # => x.y.z
mc --version          # => precisa dizer x.y.z

# 2. Derive o config da perna (a partir de teko.toml)
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
```

## Gates do CI obrigatório na org

| check | o que prova |
|---|---|
| `ngen (<os>/<arch>)` ×5 | cada perna (linux×2, macos/aarch64, windows×2) no runner nativo; `mc --host` asserido; 45 fixtures compiladas e executadas com exit code correto |
| `mc build ngen && run` | agregador que o ruleset exige; falha se qualquer perna falhar |
| `fixpoint (<os>/<arch>)` ×5 | teko0→teko1→teko2→teko3 sobre `mc_teko.tk`; `cmp` dos objetos byte-idêntico; `--dump-asm` idêntico; teko1 roda as 45 fixtures |
| `Branch policy gate`, `Analyze (actions)` | inalterados (CodeQL sem C/C++) |

## Leis de código do ngen

- **Zero mudança em `src/` do mc.** O `mc` é a base; teko ensina só o delta.
- **Zero intrínseco novo.** Toda função tem código próprio (`exp fn`, não hardcoded no backend).
  Se um construto "pede" intrínseco, é fork — parar e perguntar.
- **Toda fixture tem `// expect-exit: N`.** Sem oráculo não roda nem no CI.
- **Recusas com `teko: <causa curta>`.** Mensagem de erro padrão compilador: `arquivo:linha:coluna: "causa"`.
  Nada de história, referências ou explicação de design.
- **`--dump-ast` idêntico quando o crumb não muda código aceito.** Prova de no-op.
- **Superfície fora da v0.1.0 (recusada com mensagem, não implementada em silêncio):** `Func<>`/`Action<>`,
  multicast, `params T[]`, `T[][]`, `namespace` aninhado, DI genérica, float em `params`, `when` no `_` final.
- **Coordenação:** o coordenador não manda mensagem a agente em voo (`SendMessage` indisponível); errou →
  kill e re-dispatch limpo, nunca remendo em voo.
  Construções fora de v0.1.0; recusa se aparecerem.

## Processo (uma passada scout → implementer)

Quando um crumb chega: (1) **SCOUT** verifica contra o `ngen/` atual — as citações estão certas?
A superfície já está landada? As deps satisfeitas? Há drift? (2) **IMPLEMENTER** só se scout disser
PRECISA ou INCERTO, COM os achados do scout.

**Um agente por branch/worktree isolado.** Nunca compartilhe o checkout principal. Push frequente
na branch do agente; dreno por ff/cherry-pick para `fix/retirement`.

## Onde estão as docs

- **`HANDOFF.md`**: guia completo de operação local, CI, armadilhas recorrentes.
- **`docs/design/port-teko-mc.md`**: design do port.
- **`docs/design/pr-org-ngen.md`**: gates para merge na org, checklist do dono.
- **`docs/design/plano-ngen-entrega4.md`**: plano de trabalho, recusas v0.1.0 explícitas.
- **`DECISION_LOG.md`**: decisões ratificadas (D211–D214 = congelamento `src/`, port ao mc; D225–D230 = ngen e registro).

## Canal com o mc (NOTICES)

Decisões que impactam o mc, ou pedidos de check do mc, vão a `NOTICES` no `minicompiler/mc`
no canal de avisos do mc (`NOTICES-teko.md` do repo do mc, lido no começo de cada lote). Exemplos: mudança de hook signature, limite de feature do mc,
pacote registrado, tool `tekoc` vs lib `teko`. **O mc pinado é autoridade** — se o mc não compila
uma feature, é fork (parar e perguntar).

## Sem workarounds
Achar e resolver a **causa raiz**, nunca dar voltas para contornar. Se um dispatch saiu errado,
interromper e reiniciar certo — não remendar um agente em voo.
