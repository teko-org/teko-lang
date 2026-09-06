#!/usr/bin/env bash
# scripts/ci_full_log.sh — descarrega o log COMPLETO de uma execução de CI e desempacota-o por passo.
#
# PORQUE EXISTE, e é uma correção a mim mesmo. O dono, 2026-07-30: *"Quanto aos logs, que só consegue
# a cauda, pode instruir o CI a guardar o log completo como artefato quando uma falha ocorrer, assim
# consegue baixar o artefato para analisar."* Ele tinha razão sobre o problema — eu estava a ler CI
# através de `get_job_logs`, que devolve uma CAUDA (`tail_lines`, 500 por omissão), e a diagnosticar
# pernas de 180 KB de log por 500 linhas do fim. O que estava errado era a minha suposição de que
# fazia falta MUDAR o CI para o resolver.
#
# O QUE A MEDIÇÃO MOSTROU (2026-07-30, execuções 30509216571 e 30508737150 da PR #99):
#
#   · `GET /actions/runs/<id>/logs` devolve um ZIP com o log INTEGRAL de TODOS os passos de TODOS os
#     jobs. Medido: 660 KB comprimido, **225 ficheiros**, 2.5 MB de texto, 21 jobs. Nada truncado.
#   · Funciona **retroativamente** — execuções que já aconteceram, sem lá ter posto nada.
#   · Portanto o artefacto-em-falha NÃO era necessário para fechar esta cegueira. Um `upload-artifact`
#     em 27 jobs de `pr.yml` teria sido churn de CI a resolver um problema que era meu: não conhecia o
#     instrumento. E churn de CI na lane é exactamente o que a barra do tronco não quer.
#
# A FRONTEIRA REAL, também medida, e é o único sítio onde o artefacto-em-falha ainda ganharia:
#
#   · Enquanto a execução está EM CURSO dá **404**, e o 404 chega já no PEDIDO DO URL, não no download
#     (medido na 30509727122, ainda a correr: `get_workflow_run_logs_url` responde 404 e não devolve
#     URL nenhum). O ZIP só nasce quando a execução TERMINA.
#
# CORRECÇÃO A ESTE PRÓPRIO CABEÇALHO (2026-07-30, execução 30563221738, ainda a correr). Eu tinha
# escrito aqui que "para espiar uma perna vermelha antes do fim da execução, o log completo não serve"
# e que faria falta um artefacto-em-falha em `pr.yml`. **É falso, e a fronteira era minha, não do
# GitHub.** O 404 é só do ZIP DA EXECUÇÃO INTEIRA. Os logs POR JOB, INTEGRAIS, estão disponíveis
# enquanto a execução corre:
#
#     mcp__github__get_job_logs  run_id=<run-id>  failed_only=true  return_content=false
#         → devolve um `logs_url` ASSINADO por job falhado (expira em ~10 min)
#     curl -sS -o job.txt '<logs_url>'
#         → o log INTEGRAL desse job, não uma cauda
#
# Medido na 30563221738 com uma perna ainda `in_progress`: 4 jobs, 1465–3967 linhas cada, nada
# truncado, e a linha que interessava estava a 1538 — muito além de qualquer `tail_lines`. Nenhuma
# mudança em `pr.yml` foi precisa. O padrão repete-se: eu propus mexer no CI para resolver uma
# limitação do meu conhecimento do instrumento, e é a segunda vez neste mesmo ficheiro.
#   · O que NENHUM download desfaz é o truncamento que o NOSSO harness faz: quando uma linha de
#     regressão falha, ele imprime `captured output tail:` e corta. Esse limite é do produto, não do
#     GitHub, e é um achado à parte.
#
# COMO SE OBTÉM A URL. O URL vem assinado e expira em minutos, e só a camada MCP o sabe pedir:
#     mcp__github__actions_get  method=get_workflow_run_logs_url  resource_id=<run-id>
# Este script recebe esse URL já assinado, precisamente porque não há `gh` nem token nesta caixa.
#
# usage: bash scripts/ci_full_log.sh <signed-logs-url> [<destino>]
#
#   destino  onde desempacotar (por omissão, um directório temporário que o script imprime)
#
# Depois, o que se procura de facto (o `cut -c30-` corta o timestamp que o Actions põe em cada linha):
#     grep -rn 'assertion failed\|native backend N1\|Process completed with exit' <destino> | cut -c1-200
#     ls "<destino>/test _ macos-arm64/"          # um ficheiro por PASSO, na ordem em que correram

set -u

URL="${1:-}"
DEST="${2:-}"

if [ -z "$URL" ]; then
    cat >&2 <<'USAGE'
ci_full_log: FATAL — falta o URL assinado.

Obtém-no primeiro (o URL expira em minutos, portanto pede-o imediatamente antes de correr isto):
    mcp__github__actions_get  method=get_workflow_run_logs_url  owner=schivei  repo=teko-lang  resource_id=<run-id>

usage: bash scripts/ci_full_log.sh <signed-logs-url> [<destino>]
USAGE
    exit 2
fi

case "$URL" in
    https://*) ;;
    *) echo "ci_full_log: FATAL — '$URL' não parece um URL https assinado" >&2; exit 2 ;;
esac

if [ -z "$DEST" ]; then
    DEST="$(mktemp -d 2>/dev/null || mktemp -d -t cifulllog)"
fi
mkdir -p "$DEST" || { echo "ci_full_log: FATAL — não consigo criar '$DEST'" >&2; exit 2; }

zip="$DEST/_run-logs.zip"

# `set +e` em volta da captura de estado: sem isto, `-e` mataria o script antes de podermos explicar a
# falha — o mesmo defeito que custou a esta lane um `exit 127` opaco no Windows.
set +e
code="$(curl -sSL -o "$zip" -w '%{http_code}' "$URL" 2>/dev/null)"
rc=$?
set -e

if [ "$rc" -ne 0 ]; then
    echo "ci_full_log: FALHOU — curl saiu com $rc (rede, ou proxy)" >&2
    exit 1
fi

if [ "$code" = "404" ]; then
    cat >&2 <<'RUNNING'
ci_full_log: 404 — e isto quase sempre NÃO é um erro.

O ZIP de log completo só existe quando a execução TERMINA (medido: 404 numa execução em curso, 200 na
mesma workflow depois de acabar). Se a execução ainda está a correr, espera que acabe; para ver uma
perna vermelha ANTES disso, só a cauda por job serve:
    mcp__github__get_job_logs  job_id=<id>  return_content=true  tail_lines=2000
RUNNING
    exit 1
fi

if [ "$code" != "200" ]; then
    echo "ci_full_log: FALHOU — HTTP $code (o URL assinado expira em minutos; pede um novo)" >&2
    exit 1
fi

if ! unzip -q "$zip" -d "$DEST" 2>/dev/null; then
    echo "ci_full_log: FALHOU — o ficheiro descarregado não é um ZIP legível ($(wc -c < "$zip") bytes)" >&2
    exit 1
fi
rm -f "$zip"

files="$(find "$DEST" -type f -name '*.txt' | wc -l | tr -d ' ')"
jobs="$(find "$DEST" -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')"

echo "ci_full_log: OK — $files ficheiros de passo em $jobs jobs, sob:"
echo "    $DEST"
echo
echo "jobs:"
find "$DEST" -mindepth 1 -maxdepth 1 -type d -exec basename {} \; | sort | sed 's/^/    /'
echo
echo "o que falhou, em todos os jobs de uma vez:"
grep -rn 'assertion failed\|native backend N1\|Process completed with exit code [^0]' "$DEST" 2>/dev/null \
    | cut -c1-220 | sed 's/^/    /' | head -40
exit 0
