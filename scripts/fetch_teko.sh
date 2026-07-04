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
#          TEKO_REPO — owner/repo (default schivei/teko-lang).
# Requires: gh (authenticated), tar or unzip.
set -eu

REPO="${TEKO_REPO:-schivei/teko-lang}"
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
LABEL="${o}-${a}"

TAG="$(gh api "repos/${REPO}/releases" --jq 'map(select(.draft | not))[0].tag_name')"
if [ -z "$TAG" ] || [ "$TAG" = "null" ]; then
  echo "fetch_teko: no published release found for $REPO" >&2
  exit 1
fi

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
gh release download "$TAG" -R "$REPO" -p "teko-${LABEL}.${ext}" -D "$DEST"
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
