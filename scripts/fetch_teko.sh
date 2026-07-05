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

# Newest release BY VERSION — the /releases API is not version-ordered, so `[0]` can be
# a stale tag (0.0.1.9 ahead of 0.0.1.17). Filter to MAJOR.MINOR.PATCH.BUILD + `sort -V`.
TAG="$(gh api "repos/${REPO}/releases" --paginate \
  --jq 'map(select(.draft | not) | .tag_name)[] | select(test("^v?[0-9]+([.][0-9]+){3}"))' \
  | awk '{ orig=$0; ver=$0; sub(/^v/,"",ver); print ver"\t"orig }' | sort -V | tail -n1 | cut -f2)"
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
