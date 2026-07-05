#!/usr/bin/env sh
# scripts/ci_provision_teko.sh — put the LATEST released teko compiler on PATH.
#
# The Teko-only ruling retired the C bootstrap: CI no longer builds the compiler,
# it downloads the previously-released per-platform binary and runs the PR corpus
# through it. This is the classic self-hosting seed — each merge publishes a new
# release, so an open PR always validates against the prior merge's compiler.
#
# Usage:   ci_provision_teko.sh <LABEL>
#          LABEL = the release asset platform label, e.g. linux-x86_64 /
#          macos-arm64 / windows-x86_64 (matches teko-<LABEL>.{tar.gz,zip}).
#
# Requires: gh (authenticated via GH_TOKEN), tar or unzip. Extracts into ./.seed
# and appends it to $GITHUB_PATH so subsequent steps call `teko` directly.
set -eu

LABEL="${1:?usage: ci_provision_teko.sh <LABEL>}"
REPO="${GITHUB_REPOSITORY:?GITHUB_REPOSITORY must be set}"

# Newest published release BY VERSION (prereleases included, drafts excluded). The
# GitHub /releases API is NOT ordered by version — `[0]` can return a stale tag (e.g.
# 0.0.1.9 ahead of 0.0.1.17). Filter to MAJOR.MINOR.PATCH.BUILD tags and pick the
# highest with `sort -V`, so CI always seeds from the newest compiler.
TAG="$(gh api "repos/${REPO}/releases" --paginate \
  --jq 'map(select(.draft | not) | .tag_name)[] | select(test("^v?[0-9]+([.][0-9]+){3}"))' \
  | awk '{ orig=$0; ver=$0; sub(/^v/,"",ver); print ver"\t"orig }' | sort -V | tail -n1 | cut -f2)"
if [ -z "$TAG" ] || [ "$TAG" = "null" ]; then
  echo "ci_provision_teko: no published release found for $REPO" >&2
  exit 1
fi
echo "ci_provision_teko: seeding compiler from release $TAG (asset teko-${LABEL}.*)"

# Download the asset. The Linux labels gained a libc suffix (linux-x86_64 -> linux-x86_64-glibc)
# when the release started shipping glibc+musl separately; a release predating that split has
# only the unsuffixed asset, so fall back to `<label>` with the trailing `-glibc` stripped.
rm -f teko-*.tar.gz teko-*.zip
if gh release download "$TAG" -R "$REPO" -p "teko-${LABEL}.tar.gz" -p "teko-${LABEL}.zip" --clobber 2>/dev/null; then
  :
else
  ALT="${LABEL%-glibc}"
  if [ "$ALT" != "$LABEL" ]; then
    echo "ci_provision_teko: teko-${LABEL}.* absent in $TAG — falling back to legacy teko-${ALT}.*"
    gh release download "$TAG" -R "$REPO" -p "teko-${ALT}.tar.gz" -p "teko-${ALT}.zip" --clobber
  else
    echo "ci_provision_teko: no seed asset teko-${LABEL}.* in $TAG" >&2
    exit 1
  fi
fi

rm -rf .seed
mkdir -p .seed
# Extract whichever archive actually landed (name may be the label or the legacy fallback).
ARCHIVE="$(ls teko-*.zip teko-*.tar.gz 2>/dev/null | head -n1)"
if [ -z "$ARCHIVE" ]; then
  echo "ci_provision_teko: no seed archive downloaded" >&2
  exit 1
fi
case "$ARCHIVE" in
  *.zip) unzip -o "$ARCHIVE" -d .seed ;;
  *)     tar -xzf "$ARCHIVE" -C .seed ;;
esac
chmod +x .seed/teko .seed/teko.exe 2>/dev/null || true

SEED_DIR="$(CDPATH='' cd -- .seed && pwd)"
if [ -n "${GITHUB_PATH:-}" ]; then
  printf '%s\n' "$SEED_DIR" >> "$GITHUB_PATH"
fi
echo "ci_provision_teko: teko $TAG ready at $SEED_DIR"
