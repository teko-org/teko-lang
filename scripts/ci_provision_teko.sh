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

# Candidate releases, NEWEST-VERSION FIRST (drafts excluded; the /releases API is NOT
# version-ordered so `[0]` can be stale — filter to MAJOR.MINOR.PATCH.BUILD + reverse sort -V).
# We walk this list and accept the first release that yields a USABLE seed: its asset exists AND
# the extracted binary reports the release's own version. A binary that reports a DIFFERENT
# version than its tag is a broken build (e.g. a cross-compile that dropped -DTEKO_VERSION_STRING
# and reports 0.0.0.0-dev — such a build is also miscompiled) — skip it and fall to the next.
TAGS="$(gh api "repos/${REPO}/releases" --paginate \
  --jq 'map(select(.draft | not) | .tag_name)[] | select(test("^v?[0-9]+([.][0-9]+){3}"))' \
  | awk '{ ver=$0; sub(/^v/,"",ver); print ver"\t"$0 }' | sort -rV | cut -f2)"
if [ -z "$TAGS" ]; then
  echo "ci_provision_teko: no published release found for $REPO" >&2
  exit 1
fi

# SEED = the NEWEST usable released seed, ALWAYS. Features are ADDITIVE, so the freshest compiler
# builds any older-or-equal source; there is no reason to pin an older one. `TAGS` is already
# newest-version-first, and the `seed_from_tag` loop below skips a tag whose asset is missing (an
# in-progress release building ITSELF) or whose binary miscompiled (version-sanity), falling to the
# next — so "newest first" self-heals the self-build case without a channel split.
#
# This SUPERSEDES the old stable/beta channel selection (owner ruling 2026-07-08). That split
# EXCLUDED `*-beta` tags from a `main`/`*-alpha` lane so it "never seeded from an in-progress
# umbrella" — but every released seed is gated + working, and once `main` moved onto the `-beta`
# line (0.1.0.0-beta shipped) the exclusion pinned the umbrella->main lane to the pre-remodel
# `0.0.1.x-alpha`, which cannot parse wave-new `src/` syntax (`unsafe`/`#must_free`). Always-newest
# fixes that: during the whole `0.X` beta era the newest release supplies every feature, and at
# `1.0.0.0` the newest is simply the stable release.
echo "ci_provision_teko: newest-first seed (base='${GITHUB_BASE_REF:-}' ref='${GITHUB_REF_NAME:-}' type='${GITHUB_REF_TYPE:-}')"

seed_from_tag() {
  tag="$1"
  # Candidate asset labels: exact; then the glibc-suffixed form (a bare linux-<arch> maps to the
  # glibc default); then the unsuffixed legacy form (-glibc stripped, for a pre-split release).
  cands="$LABEL"
  case "$LABEL" in
    linux-*-glibc) cands="$cands ${LABEL%-glibc}" ;;
    linux-*-musl)  : ;;
    linux-*)       cands="$cands ${LABEL}-glibc" ;;
  esac
  rm -f teko-*.tar.gz teko-*.zip
  got=""
  for c in $cands; do
    if gh release download "$tag" -R "$REPO" -p "teko-${c}.tar.gz" -p "teko-${c}.zip" --clobber 2>/dev/null; then
      got=1; break
    fi
  done
  [ -n "$got" ] || { echo "ci_provision_teko: $tag has no asset for '$LABEL' (tried: $cands) — trying older"; return 1; }
  rm -rf .seed; mkdir -p .seed
  archive="$(ls teko-*.zip teko-*.tar.gz 2>/dev/null | head -n1)"
  [ -n "$archive" ] || { echo "ci_provision_teko: $tag download produced no archive — trying older"; return 1; }
  case "$archive" in
    *.zip) unzip -o "$archive" -d .seed >/dev/null ;;
    *)     tar -xzf "$archive" -C .seed ;;
  esac
  chmod +x .seed/teko .seed/teko.exe 2>/dev/null || true
  bin=".seed/teko"; [ -f .seed/teko.exe ] && bin=".seed/teko.exe"
  # VERSION SANITY: the seed must report its own tag's version. A miscompiled/misbuilt binary
  # (e.g. the -DTEKO_VERSION_STRING-less cross build reporting 0.0.0.0-dev) is rejected.
  expectnum="${tag#v}"; expectnum="${expectnum%%-*}"
  ver="$("$bin" --version 2>/dev/null || echo '')"
  case "$ver" in
    *"$expectnum"*) : ;;
    *) echo "ci_provision_teko: $tag seed reports '$ver' (expected version $expectnum) — broken build, trying older"; return 1 ;;
  esac
  seed_dir="$(CDPATH='' cd -- .seed && pwd)"
  [ -n "${GITHUB_PATH:-}" ] && printf '%s\n' "$seed_dir" >> "$GITHUB_PATH"
  echo "ci_provision_teko: teko $tag ready at $seed_dir (version $ver)"
  return 0
}

for TAG in $TAGS; do
  if seed_from_tag "$TAG"; then
    exit 0
  fi
done
echo "ci_provision_teko: no usable seed found for '$LABEL' in any release" >&2
exit 1
