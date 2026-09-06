#!/usr/bin/env bash
# scripts/native_dry_gate.sh — A ASSERÇÃO PRINCIPAL: a build NATIVA SECA da gen2, e a sua ASSINATURA.
#
# ── PORQUE EXISTE, e é uma correcção a uma lei minha do mesmo dia ─────────────────────────────
#
# O dono, 2026-07-30, sobre os agentes: *"este é o ponto de falha dos agentes, estão testando `teko
# test .` mas a principal asserção que é o build nativo seco de gen2 não o fazem, aí normalmente vai
# passar verde mesmo."*
#
# Ele está certo, e o defeito era do enunciado. Eu escrevi a lei da emissão nativa como um
# COMPLEMENTO ao `teko test .`. É o contrário: a build nativa seca é a **asserção principal**, e o
# `teko test .` é a secundária. Um agente que mexe em lowering e corre só a suíte passa verde por
# construção — a suíte corre por uma gen2 construída pela **rota C**, que é precisamente o caminho
# que a mudança dele NÃO afecta.
#
# ── E A RAZÃO DE ISTO SER UM SCRIPT E NÃO UMA FRASE NO BRIEF ─────────────────────────────────
#
# Porque a frase já foi dita e não pegou. Duas vezes hoje um agente reportou verde e a paragem
# seguinte apareceu no CI. Uma regra que depende de o agente se lembrar de um comando é uma regra
# que falha em silêncio; um script que devolve uma ASSINATURA comparável não falha em silêncio.
#
# ── O PROBLEMA QUE ESTE GATE RESOLVE, e que a minha lei não resolvia ─────────────────────────
#
# A build nativa da gen2 **não pode ter sucesso hoje**: para no degrau vivo (à data,
# `builtin \`ftoa\` not yet lowered` em `teko::codegen::cb_f64_literal`). Um agente cumpridor
# perguntou, com razão, de que serve correr algo que sempre falha — e eu tive de admitir que a lei,
# como escrita, era uma rede de malha larga: apanhava só o que impedisse CHEGAR ao `ftoa`.
#
# A saída não é esperar pelo degrau 27. É **comparar**. A paragem é um observável estável, e o que
# interessa não é que ela exista — é que ela seja **a MESMA antes e depois** da mudança:
#
#   · assinatura IGUAL     → a mudança não introduziu nem desbloqueou paragem nenhuma. Verde honesto.
#   · assinatura DIFERENTE → uma de duas, e as duas são notícia:
#       – paragem NOVA/DIFERENTE, no mesmo sítio ou noutro → **regressão**, e é o que este gate caça;
#       – paragem MAIS ADIANTE (o degrau caiu)             → **progresso**, e tem de ser dito.
#
# É prova por inversão aplicada à própria paragem, que é o instrumento que funcionou em todos os
# achados desta lane.
#
# ── O QUE É "SECA" ──────────────────────────────────────────────────────────────────────────
#
# `--no-verify --release`, sem suíte. É a mesma invocação que `scripts/fixpoint_gate.sh`'s `build_gen`
# faz, deliberadamente: um gate que divergisse da forma que o CI usa mediria outra coisa.
#
# ── USO ─────────────────────────────────────────────────────────────────────────────────────
#
#   # 1. na BASE (antes de tocar em nada), grava a assinatura:
#   bash scripts/native_dry_gate.sh <gen1> --save .native-base.sig
#
#   # 2. na tua mudança, com a gen1 RECONSTRUÍDA da tua árvore, compara:
#   bash scripts/native_dry_gate.sh <gen1> --expect .native-base.sig
#
#   # sem modo: imprime a assinatura e sai 0. Serve para ver, não para provar.
#   bash scripts/native_dry_gate.sh <gen1>
#
# A GEN1 TEM DE VIR DA ÁRVORE QUE SE MEDE. Para uma mudança de lowering/isel/encode, a pergunta é o
# que o COMPILADOR passou a emitir — logo a gen1 do passo 2 tem de ser construída da branch. Usar a
# gen1 da base nos dois lados mede outra coisa (o efeito da mudança na FONTE, não no gerador), e essa
# é uma medição legítima mas diferente; se for essa que queres, di-lo no relatório.
#
# NÃO CUNHA KNOWN-STOP. Não pina nenhum degrau por nome, de propósito: no dia em que o degrau mudar,
# um gate que nomeasse `ftoa` teria de ser editado, e um gate que se edita a cada degrau é um gate que
# se ignora. Este compara com o que TU mediste na base, portanto sobrevive à escada inteira sem tocar.

set -u

GEN1="${1:-}"
MODE="${2:-}"
SIGFILE="${3:-}"

usage() {
    cat >&2 <<'USAGE'
native_dry_gate: uso

  bash scripts/native_dry_gate.sh <gen1> --save   <ficheiro>   # grava a assinatura da BASE
  bash scripts/native_dry_gate.sh <gen1> --expect <ficheiro>   # compara com a da base
  bash scripts/native_dry_gate.sh <gen1>                       # só imprime

O <gen1> tem de vir da MESMA árvore que se está a medir quando a mudança é no gerador de código.
USAGE
    exit 2
}

[ -n "$GEN1" ] || usage
[ -x "$GEN1" ] || { echo "native_dry_gate: FATAL — '$GEN1' não existe ou não é executável" >&2; exit 2; }
case "$MODE" in
    ""|--save|--expect) ;;
    *) usage ;;
esac
if [ -n "$MODE" ] && [ -z "$SIGFILE" ]; then usage; fi

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

work="$(mktemp -d 2>/dev/null || mktemp -d -t natdry)"
log="$work/native.log"
rt=""
[ -d "$root/src/runtime" ] && rt="$root/src/runtime"

echo "native_dry_gate: a build NATIVA SECA da gen2 — a asserção principal"
echo "native_dry_gate:   gen1   = $GEN1"
echo "native_dry_gate:   fonte  = $root"

# `set +e` em volta da captura: sem isto, `-e` mata o script na build que se ESPERA que falhe, e o
# diagnóstico nunca é lido. É o mesmo defeito que custou a esta lane um `exit 127` opaco no Windows.
set +e
if [ -n "$rt" ]; then
    ( cd "$root" && TK_RT_DIR="$rt" TEKO_BACKEND=native "$GEN1" . -o "$work/out" --no-verify --release ) >"$log" 2>&1
else
    ( cd "$root" && TEKO_BACKEND=native "$GEN1" . -o "$work/out" --no-verify --release ) >"$log" 2>&1
fi
rc=$?
set -e

# A ASSINATURA. Uma linha, e canónica: só a paragem, sem caminhos absolutos, sem tempos, sem
# contagens de ficheiros — tudo o que varia entre hosts e entre corridas e que faria o gate gritar
# por diferenças que não são a diferença.
sig="$(grep -o 'native backend N1:.*' "$log" 2>/dev/null | head -1)"
if [ -z "$sig" ]; then
    if [ "$rc" -eq 0 ]; then
        # FAIL-CLOSED, e isto foi encontrado por inversão a este próprio gate: passar-lhe `/bin/true`
        # como gen1 dava rc=0, log vazio, e a assinatura dizia COMPLETED. Um "compilador" que não faz
        # nada e sai 0 lido como sucesso é literalmente a classe de defeito que o cabeçalho de
        # `scripts/check_elf.sh` registra — *"AN ABSENT OBJECT WAS A PASS"*. Um sucesso tem de ser
        # CORROBORADO por um artefacto, nunca por um código de saída sozinho.
        if [ -x "$work/out/teko" ] || [ -x "$work/out/teko.exe" ]; then
            sig="COMPLETED — the native backend built the compiler"
        else
            sig="EXIT 0 WITH NO BINARY — '$GEN1' saiu 0 e não produziu executável; não é um sucesso, é um gen1 inválido"
        fi
    else
        # Uma falha SEM paragem nomeada é a pior das três: não é o degrau, é outra coisa, e é
        # exactamente o que o `teko test .` verde esconderia.
        sig="FAILED WITHOUT A NAMED N1 STOP (rc=$rc) — $(tail -3 "$log" | tr '\n' ' ' | cut -c1-200)"
    fi
fi

echo "native_dry_gate: assinatura:"
echo "    $sig"

case "$MODE" in
    --save)
        printf '%s\n' "$sig" > "$SIGFILE"
        echo "native_dry_gate: gravada em $SIGFILE — agora aplica a tua mudança, reconstrói a gen1 e corre com --expect"
        exit 0
        ;;
    "")
        echo "native_dry_gate: (sem modo — nada foi provado; usa --save na base e --expect na tua branch)"
        exit 0
        ;;
esac

[ -f "$SIGFILE" ] || { echo "native_dry_gate: FATAL — '$SIGFILE' não existe; corre --save na base primeiro" >&2; exit 2; }
base="$(head -1 "$SIGFILE")"

if [ "$sig" = "$base" ]; then
    echo "native_dry_gate: VERDE — a paragem é a MESMA da base. A mudança não introduziu nem desbloqueou nenhuma."
    exit 0
fi

echo "native_dry_gate: VERMELHO — a paragem MUDOU." >&2
echo "    base: $base" >&2
echo "    tua:  $sig"  >&2
cat >&2 <<'WHY'

Isto é notícia nos DOIS sentidos, e o relatório tem de dizer qual:

  · PROGRESSO — a paragem foi mais para a frente, ou desapareceu. O degrau caiu. Diz qual era e qual
    é agora; o handoff e a escada de degraus têm de ser actualizados, e a assinatura da base fica
    obsoleta para todos os agentes seguintes.

  · REGRESSÃO — a paragem é outra, ou aparece antes, ou é `FAILED WITHOUT A NAMED N1 STOP`. Esta
    última é a mais grave: uma falha nativa sem paragem nomeada não é a escada, é defeito, e é
    precisamente o que uma suíte verde esconde (a suíte corre por uma gen2 da ROTA C, que a tua
    mudança no gerador não atravessa).

Não trates nenhum dos dois como ruído, e não ajustes a assinatura da base para o gate passar.
WHY
exit 1
