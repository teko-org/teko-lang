#!/usr/bin/env sh
# scripts/fetch_teko.sh — download the latest RELEASED teko compiler for LOCAL/agent use.
#
# The Teko-only ruling made the released binary the compiler: CI seeds from it, and so
# must every local/agent working copy — otherwise you would reinstall by hand on every
# release. Refresh your compiler WHENEVER a PR is opened or merged (a merge publishes a
# new release; open PRs must re-validate against it). This script is version-cached: it
# re-downloads only when the newest release differs from the one already in $TEKO_DEST.
#
# On macOS it strips the Gatekeeper quarantine (`xattr -d com.apple.quarantine`) so the
# downloaded binary is allowed to run.
#
# Usage:   sh scripts/fetch_teko.sh
#          Then add "$TEKO_DEST" (default ./.teko) to PATH, or call ./.teko/teko directly.
#
# Env:     TEKO_DEST — install dir (default ./.teko, per-worktree so parallel agents /
#                      worktrees do not clobber each other's compiler).
#          TEKO_REPO — owner/repo (default teko-org/teko-lang).
# Requires: gh (authenticated), tar or unzip.
set -eu

REPO="${TEKO_REPO:-teko-org/teko-lang}"
DEST="${TEKO_DEST:-.teko}"

os="$(uname -s)"
arch="$(uname -m)"
case "$os" in
  Linux)  o=linux;  ext=tar.gz ;;
  Darwin) o=macos;  ext=tar.gz ;;
  MINGW*|MSYS*|CYGWIN*) o=windows; ext=zip ;;
  *) echo "fetch_teko: unsupported OS '$os'" >&2; exit 1 ;;
esac
case "$arch" in
  x86_64|amd64)  a=x86_64 ;;
  arm64|aarch64) a=arm64 ;;
  *) echo "fetch_teko: unsupported arch '$arch'" >&2; exit 1 ;;
esac
# Linux ships glibc (dynamic) + musl (static) per arch — pick the one this system uses
# (musl distros expose /lib/ld-musl-*). macOS/Windows have a single libc, no suffix.
LABEL="${o}-${a}"
if [ "$o" = linux ]; then
  libc="${TEKO_LIBC:-}"
  if [ -z "$libc" ]; then
    if [ -n "$(ls /lib/ld-musl-* 2>/dev/null)" ] || (ldd --version 2>&1 | grep -qi musl); then
      libc=musl
    else
      libc=glibc
    fi
  fi
  LABEL="${o}-${a}-${libc}"
fi

BIN_PROBE="teko"
[ "$o" = windows ] && BIN_PROBE="teko.exe"

# SEM `gh`, HÁ UM SEGUNDO CAMINHO — e ele existe porque a sua ausência já custou meia hora a
# cada um de dois agentes, no mesmo dia, sem nenhum dos dois conseguir NOMEAR a parede.
#
# O que se passava (medido 2026-07-30): esta caixa não tem `gh` autenticado. O guião morria aqui,
# o chamador caía para a semente em cache — que estava uma versão atrasada, `0.3.0.30-beta` — e
# essa semente NÃO constrói a árvore de hoje: pára em `src/build/project.tks:2076: unknown
# function: arch`, porque `teko::arch()` só passou a builtin reconhecido pela semente DEPOIS do
# 0.3.0.30. O `build_with_seed_fallback.sh` esgotava então `MAX_PROBES=64` a procurar um degrau
# construível que não existia. Nada disto dizia "não tens compilador"; dizia coisas sobre `arch`.
#
# A cache partilhada é o remédio: quem TEM como buscar a semente (o integrador, pelo MCP do
# GitHub, ou qualquer humano com `gh`) deposita-a uma vez em `$TEKO_SEED_CACHE`, e todos os
# worktrees a encontram sem rede e sem credencial.
SEED_CACHE="${TEKO_SEED_CACHE:-$HOME/.teko-seed}"

# COMO SE DETECTA, e a primeira versão desta detecção estava errada — fica registado porque o
# erro é instrutivo. Eu escrevi `command -v gh || ! gh auth status` e ele NÃO disparou: nesta
# caixa o `gh` existe e o `gh auth status` sai 0. O que falha é o ACESSO AO REPOSITÓRIO, com um
# 403 que chega como corpo JSON no sítio onde se esperava uma etiqueta. Verifiquei um PROXY da
# condição em vez da condição — a mesma patologia que a barra do tronco recusa.
#
# A detecção certa é TENTAR A CHAMADA e validar a FORMA do que volta: uma etiqueta de versão
# casa `^v?N.N.N.N`. Um objecto de erro, uma string vazia, um `null` — nada disso casa, e todos
# caem no mesmo ramo, sem eu ter de enumerar os modos de falha do `gh`.
probe_tag() {
  gh api "repos/${REPO}/releases" --paginate \
    --jq 'map(select(.draft | not) | .tag_name)[] | select(test("^v?[0-9]+([.][0-9]+){3}"))' 2>/dev/null \
    | awk '{ orig=$0; ver=$0; sub(/^v/,"",ver); print ver"\t"orig }' | sort -V | tail -n1 | cut -f2
}
TAG=""
command -v gh >/dev/null 2>&1 && TAG="$(probe_tag)"
case "$TAG" in
  v[0-9]*.[0-9]*.[0-9]*.[0-9]*|[0-9]*.[0-9]*.[0-9]*.[0-9]*) ;;
  *) TAG="" ;;
esac

if [ -z "$TAG" ]; then
  if [ -x "${SEED_CACHE}/${BIN_PROBE}" ]; then
    echo "fetch_teko: o \`gh\` não devolveu etiqueta — a usar a cache partilhada ${SEED_CACHE}"
    mkdir -p "$DEST"
    cp "${SEED_CACHE}/${BIN_PROBE}" "${DEST}/${BIN_PROBE}"
    chmod +x "${DEST}/${BIN_PROBE}"
    [ -f "${SEED_CACHE}/.version" ] && cp "${SEED_CACHE}/.version" "${DEST}/.version"
    "${DEST}/${BIN_PROBE}" --version || true
    exit 0
  fi
  # FALHA ALTO E NOMEIA O QUE FALTA. O contrário — devolver silêncio e deixar o chamador cair
  # numa semente velha — é o erro escondido que esta secção existe para matar.
  cat >&2 <<EOF
fetch_teko: FATAL — o \`gh\` não devolveu nenhuma etiqueta de versão para ${REPO} (sem
  binário, sem autenticação, ou sem acesso ao repositório) E não há cache partilhada em
  ${SEED_CACHE}

NÃO caias para uma semente antiga: uma semente uma versão atrás não constrói esta árvore, e
falha a dizer coisas sobre \`arch\` em vez de dizer que está velha.

Como encher a cache (quem tem o MCP do GitHub — o integrador):
  1. mcp__github__actions_list  method=list_workflow_run_artifacts  resource_id=<run-id>
     → escolhe \`teko-assets-<label>\` de uma perna \`artifact\` VERDE
  2. mcp__github__actions_get   method=download_workflow_run_artifact  resource_id=<artifact-id>
  3. curl o URL, unzip, e instala o binário em ${SEED_CACHE}/${BIN_PROBE}

Ou, com \`gh\` autenticado, corre este guião normalmente.
EOF
  exit 1
fi

# A etiqueta mais recente POR VERSÃO já foi obtida e validada acima por `probe_tag` — a API
# /releases não vem ordenada por versão (0.0.1.9 podia aparecer à frente de 0.0.1.17), daí o
# filtro a MAJOR.MINOR.PATCH.BUILD e o `sort -V`. Chegar aqui significa que `$TAG` casa a forma.

BIN="teko"
[ "$o" = windows ] && BIN="teko.exe"
marker="${DEST}/.version"
if [ -f "$marker" ] && [ "$(cat "$marker")" = "$TAG" ] && [ -x "${DEST}/${BIN}" ]; then
  echo "fetch_teko: already at $TAG (${DEST}/${BIN})"
  exit 0
fi

echo "fetch_teko: updating to $TAG (asset teko-${LABEL}.${ext})"
rm -rf "$DEST"
mkdir -p "$DEST"
# Releases predating the glibc/musl split named the glibc asset without a suffix
# (teko-linux-x86_64.tar.gz), so fall back to that legacy name.
if ! gh release download "$TAG" -R "$REPO" -p "teko-${LABEL}.${ext}" -D "$DEST" 2>/dev/null; then
  ALT="${LABEL%-glibc}"
  if [ "$ALT" != "$LABEL" ]; then
    echo "fetch_teko: teko-${LABEL}.${ext} absent — falling back to legacy teko-${ALT}.${ext}"
    gh release download "$TAG" -R "$REPO" -p "teko-${ALT}.${ext}" -D "$DEST"
    LABEL="$ALT"
  else
    echo "fetch_teko: no asset teko-${LABEL}.${ext} in $TAG" >&2
    exit 1
  fi
fi
if [ "$ext" = zip ]; then
  unzip -o "${DEST}/teko-${LABEL}.zip" -d "$DEST"
else
  tar -xzf "${DEST}/teko-${LABEL}.tar.gz" -C "$DEST"
fi
rm -f "${DEST}/teko-${LABEL}.${ext}"
chmod +x "${DEST}/${BIN}" 2>/dev/null || true

# macOS: a downloaded binary is quarantined by Gatekeeper and refuses to run until the
# attribute is cleared.
if [ "$o" = macos ]; then
  xattr -d com.apple.quarantine "${DEST}/${BIN}" 2>/dev/null || true
fi

printf '%s\n' "$TAG" > "$marker"
echo "fetch_teko: teko $TAG ready at ${DEST}/${BIN}  (add ${DEST} to PATH)"
