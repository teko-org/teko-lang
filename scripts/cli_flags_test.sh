#!/usr/bin/env bash
# scripts/cli_flags_test.sh — assert the `teko` CLI version/help flags (feat/cli-version-flag).
#
# The version SINGLE SOURCE OF TRUTH is teko.tkp (`version` + `suffix`). This harness proves the
# built binary embeds and prints exactly the RAW manifest string (no "4th field = published gen"
# release substitution) and that the flags short-circuit correctly:
#
#   teko --version | -v   → stdout "teko <version>", exit 0
#   teko --help    | -h   → usage banner incl. "teko <version>", exit 0
#   teko                  → usage (stderr), exit 2  (UNCHANGED no-args path)
#
# Usage:  scripts/cli_flags_test.sh <path/to/teko-binary> [path/to/teko.tkp]
#         (default manifest: the teko.tkp at the repo root, relative to this script)
set -eu

BIN="${1:?usage: cli_flags_test.sh <teko-binary> [teko.tkp]}"
if [ "$#" -ge 2 ] && [ -n "$2" ]; then
  MANIFEST="$2"
else
  SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
  MANIFEST="$SCRIPT_DIR/../teko.tkp"
fi

fail() { echo "cli_flags_test: FAIL: $*" >&2; exit 1; }

# --- derive the EXPECTED version string RAW from teko.tkp (version + -suffix) -----------------
read_toml_string() {
  awk -v k="$1" '
    /^[[:space:]]*#/ { next }
    {
      if (match($0, "^[[:space:]]*" k "[[:space:]]*=")) {
        rest = substr($0, RSTART + RLENGTH)
        if (match(rest, /"/)) {
          rest = substr(rest, RSTART + 1)
          if (match(rest, /"/)) { print substr(rest, 1, RSTART - 1) } else { print "" }
          exit
        }
      }
    }' "$MANIFEST"
}
VERSION="$(read_toml_string version)"
[ -n "$VERSION" ] || fail "no 'version' in $MANIFEST"
SUFFIX="$(read_toml_string suffix || true)"
if [ -n "$SUFFIX" ]; then EXPECTED="teko $VERSION-$SUFFIX"; else EXPECTED="teko $VERSION"; fi
echo "cli_flags_test: expecting version line: '$EXPECTED' (binary: $BIN)"

# --- helper: run the binary with args, capture stdout, stderr, exit code ---------------------
run_flag() {   # $1..: args → sets OUT, ERR, RC
  set +e
  OUT="$("$BIN" "$@" 2>/tmp/cli_flags_err)"; RC=$?
  ERR="$(cat /tmp/cli_flags_err)"
  set -e
}

# --- --version / -v : stdout is exactly EXPECTED, exit 0 -------------------------------------
for f in --version -v; do
  run_flag "$f"
  [ "$RC" -eq 0 ]        || fail "'$f' exit $RC (want 0)"
  [ "$OUT" = "$EXPECTED" ] || fail "'$f' stdout '$OUT' (want '$EXPECTED')"
done

# --- --help / -h : exit 0, banner contains the version line AND the usage: line --------------
for f in --help -h; do
  run_flag "$f"
  [ "$RC" -eq 0 ] || fail "'$f' exit $RC (want 0)"
  # the banner goes to stderr (leads with the version line, like main.c's usage()).
  printf '%s\n' "$ERR" | grep -qF "$EXPECTED" || fail "'$f' banner missing version line '$EXPECTED'"
  printf '%s\n' "$ERR" | grep -q "usage: teko"   || fail "'$f' banner missing 'usage: teko'"
done

# --- bare `teko` (no args) : UNCHANGED → usage to stderr, exit 2 -----------------------------
run_flag
[ "$RC" -eq 2 ] || fail "bare teko exit $RC (want 2)"

echo "cli_flags_test: PASS ($BIN)"
