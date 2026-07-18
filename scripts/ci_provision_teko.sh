#!/usr/bin/env sh
# scripts/ci_provision_teko.sh — put the LATEST released teko compiler on PATH.
#
# The Teko-only ruling retired the C bootstrap: CI no longer builds the compiler,
# it downloads the previously-released per-platform binary and runs the PR corpus
# through it. This is the classic self-hosting seed — each merge publishes a new
# release, so an open PR always validates against the prior merge's compiler.
#
# SAME MECHANICS AS install.sh (owner ruling 2026-07-18): the seed comes from the
# CANONICAL repo's public releases over plain HTTPS (curl/wget), NOT from the
# repository CI happens to run in. The old form (`gh` + GH_TOKEN scoped to
# GITHUB_REPOSITORY) broke on every fork — a fork has no releases, so provisioning
# always failed there. Downloading from the canonical repo works identically on
# the canonical repo itself, on any fork, and on a bare container — no `gh`, no
# required token. GH_TOKEN, when present, is used ONLY as an optional
# Authorization header on the release-LIST API call (runner IPs share the
# unauthenticated rate limit); asset downloads are plain public URLs.
#
# Usage:   ci_provision_teko.sh <LABEL>
#          LABEL = the release asset platform label, e.g. linux-x86_64 /
#          macos-arm64 / windows-x86_64 (matches teko-<LABEL>.{tar.gz,zip}).
#
# Environment:
#   TEKO_SEED_REPO  owner/repo to seed from (default: teko-org/teko-lang).
#
# Requires: curl or wget, tar or unzip. Extracts into ./.seed and appends it to
# $GITHUB_PATH so subsequent steps call `teko` directly.
set -eu

LABEL="${1:?usage: ci_provision_teko.sh <LABEL>}"
REPO="${TEKO_SEED_REPO:-teko-org/teko-lang}"
REPO_URL="https://github.com/${REPO}"

log() { printf '%s\n' "ci_provision_teko: $*" >&2; }
have() { command -v "$1" >/dev/null 2>&1; }

# download_ok URL OUT — fetch URL to OUT, returning non-zero (not aborting) when it is
# missing, exactly like install.sh's probe helper. curl preferred, wget fallback.
download_ok() {
  url="$1"; out="$2"
  if have curl; then
    curl -fsSL "$url" -o "$out" 2>/dev/null
  elif have wget; then
    wget -qO "$out" "$url" 2>/dev/null
  else
    log "no downloader found (need curl or wget)"; exit 1
  fi
}

# api_get URL OUT — like download_ok, but adds the optional GH_TOKEN Authorization
# header when present (rate-limit relief on shared runner IPs; NEVER required — a
# public repo's releases list is readable without it).
api_get() {
  url="$1"; out="$2"
  if have curl; then
    if [ -n "${GH_TOKEN:-}" ]; then
      curl -fsSL -H "Authorization: Bearer ${GH_TOKEN}" -H "Accept: application/vnd.github+json" "$url" -o "$out" 2>/dev/null
    else
      curl -fsSL -H "Accept: application/vnd.github+json" "$url" -o "$out" 2>/dev/null
    fi
  elif have wget; then
    if [ -n "${GH_TOKEN:-}" ]; then
      wget -qO "$out" --header="Authorization: Bearer ${GH_TOKEN}" "$url" 2>/dev/null
    else
      wget -qO "$out" "$url" 2>/dev/null
    fi
  else
    log "no downloader found (need curl or wget)"; exit 1
  fi
}

# sha256_of FILE — portable digest (sha256sum on Linux, shasum -a 256 on macOS),
# mirroring install.sh. Empty output when neither tool exists (verification skipped).
sha256_of() {
  f="$1"
  if have sha256sum; then
    sha256sum "$f" | awk '{print $1}'
  elif have shasum; then
    shasum -a 256 "$f" | awk '{print $1}'
  else
    printf '%s' ""
  fi
}

# Candidate releases, NEWEST-VERSION FIRST (the /releases API is NOT version-ordered so
# `[0]` can be stale — filter to MAJOR.MINOR.PATCH.BUILD + reverse sort -V). Same tag
# discovery shape as install.sh's latest_tag() fallback, over the CANONICAL repo.
rels_json=".seed-releases.json"
if ! api_get "https://api.github.com/repos/${REPO}/releases?per_page=100" "$rels_json"; then
  log "cannot list releases for $REPO (network/API error)"; exit 1
fi
TAGS="$(sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$rels_json" \
  | grep -E '^v?[0-9]+([.][0-9]+){3}' \
  | awk '{ ver=$0; sub(/^v/,"",ver); print ver"\t"$0 }' | sort -rV | cut -f2)"
rm -f "$rels_json"
if [ -z "$TAGS" ]; then
  log "no published release found for $REPO"; exit 1
fi

# SEED = the NEWEST usable released seed, ALWAYS. Features are ADDITIVE, so the freshest
# compiler builds any older-or-equal source; there is no reason to pin an older one.
# `TAGS` is already newest-version-first, and the `seed_from_tag` loop below skips a tag
# whose asset is missing (an in-progress release building ITSELF), whose checksum does
# not verify, or whose binary miscompiled (version-sanity), falling to the next — so
# "newest first" self-heals the self-build case without a channel split.
log "newest-first seed from $REPO (label '$LABEL')"

seed_from_tag() {
  tag="$1"
  base="${REPO_URL}/releases/download/${tag}"
  # Candidate asset labels: exact; then the glibc-suffixed form (a bare linux-<arch> maps
  # to the glibc default); then the unsuffixed legacy form (pre-split releases).
  cands="$LABEL"
  case "$LABEL" in
    linux-*-glibc) cands="$cands ${LABEL%-glibc}" ;;
    linux-*-musl)  : ;;
    linux-*)       cands="$cands ${LABEL}-glibc" ;;
  esac
  rm -f teko-*.tar.gz teko-*.zip SHA256SUMS.txt
  archive=""
  for c in $cands; do
    for ext in tar.gz zip; do
      if download_ok "${base}/teko-${c}.${ext}" "teko-${c}.${ext}"; then
        archive="teko-${c}.${ext}"; break 2
      fi
    done
  done
  [ -n "$archive" ] || { log "$tag has no asset for '$LABEL' (tried: $cands) — trying older"; return 1; }

  # CHECKSUM (same policy as install.sh): verify against the release's SHA256SUMS.txt.
  # A release without sums, or with a mismatching sum, is not a usable seed — fall to the
  # next. (Skipped only when the runner has no sha256 tool at all.)
  digest="$(sha256_of "$archive")"
  if [ -n "$digest" ]; then
    if ! download_ok "${base}/SHA256SUMS.txt" "SHA256SUMS.txt"; then
      log "$tag has no SHA256SUMS.txt — refusing an unverified seed, trying older"; return 1
    fi
    want="$(grep "$archive" "SHA256SUMS.txt" | awk '{print $1}' | head -n1)"
    if [ -z "$want" ] || [ "$want" != "$digest" ]; then
      log "$tag sha256 mismatch for $archive (expected '${want:-<none>}', got $digest) — trying older"; return 1
    fi
  fi

  rm -rf .seed; mkdir -p .seed
  case "$archive" in
    *.zip) unzip -o "$archive" -d .seed >/dev/null ;;
    *)     tar -xzf "$archive" -C .seed ;;
  esac
  chmod +x .seed/teko .seed/teko.exe 2>/dev/null || true
  bin=".seed/teko"; [ -f .seed/teko.exe ] && bin=".seed/teko.exe"
  # VERSION SANITY: the seed must report its own tag's version. A miscompiled/misbuilt
  # binary (e.g. a cross build that dropped -DTEKO_VERSION_STRING and reports
  # 0.0.0.0-dev — such a build is also miscompiled) is rejected — skip to the next.
  expectnum="${tag#v}"; expectnum="${expectnum%%-*}"
  ver="$("$bin" --version 2>/dev/null || echo '')"
  case "$ver" in
    *"$expectnum"*) : ;;
    *) log "$tag seed reports '$ver' (expected version $expectnum) — broken build, trying older"; return 1 ;;
  esac
  seed_dir="$(CDPATH='' cd -- .seed && pwd)"
  [ -n "${GITHUB_PATH:-}" ] && printf '%s\n' "$seed_dir" >> "$GITHUB_PATH"
  log "teko $tag ready at $seed_dir (version $ver)"
  return 0
}

for TAG in $TAGS; do
  if seed_from_tag "$TAG"; then
    exit 0
  fi
done
log "no usable seed found for '$LABEL' in any release of $REPO"
exit 1
