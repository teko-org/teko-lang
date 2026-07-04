#!/usr/bin/env sh
# scripts/derive_version.sh — derive the bootstrap release tag from teko.tkp.
#
# The manifest (teko.tkp, TOML, repo root) is the SINGLE SOURCE OF TRUTH for the
# release version (user ruling, memory: teko-release-versioning-rule). This script
# reads `version` and `suffix` from it and prints the tag the pipeline must publish.
#
# Rule:
#   version = "A.B.C.D"   → the 4th component D is REPLACED by the published gen (3),
#                           because release.yml publishes the gen-3 artifact.
#   suffix  = "bootstrap" → non-empty suffix is appended as "-<suffix>".
#   suffix  = ""          → no suffix; a stable (non-prerelease) tag.
#
# So   version="0.0.1.0", suffix="bootstrap"  →  0.0.1.3-bootstrap
# and  version="0.0.2.0", suffix=""           →  0.0.2.3
#
# Both the release workflow AND local devs call this, so the derivation lives in
# exactly one place. It prints the tag on stdout and nothing else on success.
#
# Usage:   sh scripts/derive_version.sh [path/to/teko.tkp]
#          (default manifest: the teko.tkp next to this script's repo root)
#
# Env:     PUBLISHED_GEN — override the gen component (default 3). The pipeline
#          publishes gen-3, so this is 3; exposed only for testing/future gens.
set -eu

PUBLISHED_GEN="${PUBLISHED_GEN:-3}"

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

TAG="$A.$B.$C.$PUBLISHED_GEN"
if [ -n "$SUFFIX" ]; then
  TAG="$TAG-$SUFFIX"
fi

printf '%s\n' "$TAG"
