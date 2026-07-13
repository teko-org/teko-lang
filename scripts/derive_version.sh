#!/usr/bin/env sh
# scripts/derive_version.sh — derive the release tag from teko.tkp.
#
# The manifest (teko.tkp, TOML, repo root) is the SINGLE SOURCE OF TRUTH for the
# release version (owner ruling, memory: teko-release-versioning-rule). This script
# reads `version` and `suffix` VERBATIM and prints the tag the pipeline must publish.
#
# Rule (ruling 2026-07-04):
#   version = "A.B.C.D"   → published AS-IS; the 4th component D IS the build number,
#                           which the integrator bumps +1 in the PR when merging a code
#                           change (resetting it to 0 on a major/minor/patch bump). No
#                           Action detects the increment — the manifest carries the truth.
#   suffix  = "alpha"     → non-empty suffix is appended as "-<suffix>" (a prerelease).
#   suffix  = ""          → no suffix; a stable (non-prerelease) tag.
#
# So   version="0.0.2.0", suffix="alpha"  →  v0.0.2.0-alpha
# and  version="0.0.2.3", suffix=""       →  v0.0.2.3
#
# Because the embedded `teko --version` also reads teko.tkp verbatim, the released
# binary's version MATCHES this tag exactly. Both the release workflow AND local devs
# call this, so the derivation lives in one place. Prints the tag on stdout, nothing else.
#
# Usage:   sh scripts/derive_version.sh [path/to/teko.tkp]
#          (default manifest: the teko.tkp next to this script's repo root)
set -eu

# Locate the manifest: explicit arg wins; else the repo root relative to this script.
if [ "$#" -ge 1 ] && [ -n "$1" ]; then
  MANIFEST="$1"
else
  SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
  MANIFEST="$SCRIPT_DIR/../teko.tkp"
fi

if [ ! -f "$MANIFEST" ]; then
  echo "derive_version: manifest not found: $MANIFEST" >&2
  exit 1
fi

# Extract a top-level `key = "value"` string from the TOML manifest.
#   - tolerates leading/trailing whitespace and spaces around `=`
#   - ignores full-line comments and inline `# …` trailing comments (the value is
#     captured from inside the quotes, so a trailing comment never leaks in)
#   - first matching assignment wins
# Emits the unquoted value (empty string if the key is absent or its value is "").
read_toml_string() {
  key="$1"
  awk -v k="$key" '
    # strip a leading-whitespace comment line
    /^[[:space:]]*#/ { next }
    {
      # match:  <space> key <space> = <space> "value"
      # capture the contents between the first pair of double quotes on the line
      line = $0
      # require the key at the start (after optional whitespace) followed by =
      if (match(line, "^[[:space:]]*" k "[[:space:]]*=")) {
        rest = substr(line, RSTART + RLENGTH)
        # find the opening quote
        if (match(rest, /"/)) {
          rest = substr(rest, RSTART + 1)
          # value is everything up to the next quote
          if (match(rest, /"/)) {
            print substr(rest, 1, RSTART - 1)
          } else {
            print ""
          }
          found = 1
          exit
        }
      }
    }
    END { if (!found) exit 2 }
  ' "$MANIFEST"
}

VERSION="$(read_toml_string version || true)"
if [ -z "$VERSION" ]; then
  echo "derive_version: no non-empty 'version' in $MANIFEST" >&2
  exit 1
fi

# suffix may legitimately be empty (stable release) or absent.
SUFFIX="$(read_toml_string suffix || true)"

# Validate the version shape: exactly four dot-separated numeric components A.B.C.D.
case "$VERSION" in
  *.*.*.*)
    # ensure there are exactly 3 dots (4 fields) and each field is numeric
    ;;
  *)
    echo "derive_version: version '$VERSION' is not 4-part A.B.C.D" >&2
    exit 1
    ;;
esac

A="${VERSION%%.*}";           rest="${VERSION#*.}"
B="${rest%%.*}";              rest="${rest#*.}"
C="${rest%%.*}";              D="${rest#*.}"

# D must be a single component (no extra dots) and all four must be numeric.
case "$A$B$C$D" in
  *[!0-9]*)
    echo "derive_version: version '$VERSION' has non-numeric components" >&2
    exit 1
    ;;
esac
case "$D" in
  *.*)
    echo "derive_version: version '$VERSION' has more than 4 components" >&2
    exit 1
    ;;
esac

# The published tag/release carries a `v` prefix (git convention: `git tag v1.2.3`), while
# teko.tkp's `version` and `teko --version` stay the bare number (`git --version` → 2.43.0).
# So this derived TAG is `v<version>[-<suffix>]`; the binary's --version (built from teko.tkp
# RAW via project.tks's build_cc_argv/run_cc) is `<version>[-<suffix>]` — the tag minus the `v`.
TAG="v$VERSION"
if [ -n "$SUFFIX" ]; then
  TAG="$TAG-$SUFFIX"
fi

printf '%s\n' "$TAG"
