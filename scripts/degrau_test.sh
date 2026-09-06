#!/usr/bin/env sh
# scripts/degrau_test.sh — the REGRESSION for the degrau criterion, the switch that decides whether
# `bootstrap/teko.c` is a rung of the bootstrap chain or merely a file this train produced.
#
# WHY THIS DESERVES A TEST OF ITS OWN, when the rest of the chain is only ever exercised by a real
# CI run: every other failure in `build_with_seed_fallback.sh` and `fixpoint_gate.sh` is LOUD — a
# build stops, a comparison disagrees, a lane goes red. This one is SILENT by nature. A criterion
# that answers "declared" when nothing is declared re-routes gen0 through `cc` and the run still
# goes green; it just proves a compiler nobody asked for. The defect it replaces was exactly that
# shape (owner ruling 2026-07-28: *"só podemos usar teko.c se e somente se identificarmos
# degrau."*), so the criterion is checked by assertion rather than by hoping a lane notices.
#
# IT NEEDS NO COMPILER AND NO NETWORK — it exercises `scripts/degrau.sh` against fixtures it builds
# in a temp dir, so it runs in the `CI gate` job alongside the other structural checks, in under a
# second, on every PR.
#
# Usage:  sh scripts/degrau_test.sh
# POSIX sh only.
set -eu

HERE="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
# shellcheck source=scripts/degrau.sh
. "$HERE/degrau.sh"

FAILED=0
ROOT="$(mktemp -d)"
trap 'rm -rf "$ROOT"' EXIT INT TERM
mkdir -p "$ROOT/bootstrap"

pass() { printf '  ok   %s\n' "$1"; }

fail() {
    printf 'ERROR: %s\n' "$1" >&2
    FAILED=1
}

# scan_rc — the scanner's verdict for the current fixture: 0 declared, 1 none, 2 broken. Wrapped so
# the caller can compare a number instead of arranging its own `|| rc=$?` around `set -e`.
scan_rc() {
    sr_rc=0
    degrau_scan "$ROOT" >/dev/null 2>&1 || sr_rc=$?
    printf '%s' "$sr_rc"
}

# expect_rc WANT NAME — asserts the verdict for the fixture the caller just laid down.
expect_rc() {
    er_want="$1"; er_name="$2"
    er_got="$(scan_rc)"
    if [ "$er_got" = "$er_want" ]; then
        pass "$er_name (rc=$er_got)"
    else
        fail "$er_name: want rc=$er_want, got rc=$er_got"
    fi
}

echo "--- degrau criterion ---"

rm -f "$ROOT/bootstrap/DEGRAU" "$ROOT/bootstrap/teko.c"
expect_rc 1 "no declaration, no C — the normal chain"

# THE CASE THE WHOLE FILE EXISTS FOR: the C is present and NOT declared. Under the retired
# criterion this answered "bootstrap", diverting gen0 through `cc` forever and in silence.
: > "$ROOT/bootstrap/teko.c"
expect_rc 1 "committed C present but NOT declared — still the normal chain"

printf 'c: bootstrap/teko.c\nwhy: the release cannot lower the tip\nsince: 0.3.1.0\n' > "$ROOT/bootstrap/DEGRAU"
expect_rc 0 "a complete declaration whose bridge exists"
degrau_scan "$ROOT" >/dev/null 2>&1 || true
[ "$DEGRAU_WHY" = "the release cannot lower the tip" ] \
    && pass "the reason is published for the log" \
    || fail "the reason was not parsed: '$DEGRAU_WHY'"
[ "$DEGRAU_C" = "$ROOT/bootstrap/teko.c" ] \
    && pass "the bridge is resolved against the root" \
    || fail "the bridge resolved to '$DEGRAU_C'"

printf '# a comment only\nc: bootstrap/teko.c   # trailing comments are values, not noise\nwhy: x\n' > "$ROOT/bootstrap/DEGRAU"
degrau_scan "$ROOT" >/dev/null 2>&1 || true
[ "$DEGRAU_C" = "$ROOT/bootstrap/teko.c" ] \
    && pass "comments are stripped from a value" \
    || fail "a trailing comment leaked into the value: '$DEGRAU_C'"

# EVERY MALFORMED DECLARATION IS FATAL, NEVER "no degrau". A file that exists but does not parse
# means a human tried to declare a bridge and the bridge is not where they said — degrading that to
# "none declared" would walk the chain they already declared broken and report the wrong failure.
printf 'why: a reason with no bridge\n' > "$ROOT/bootstrap/DEGRAU"
expect_rc 2 "a declaration with no bridge is FATAL, not 'none'"

printf 'c: bootstrap/teko.c\n' > "$ROOT/bootstrap/DEGRAU"
expect_rc 2 "a bridge with no reason is FATAL, not 'none'"

printf 'c: bootstrap/absent.c\nwhy: x\n' > "$ROOT/bootstrap/DEGRAU"
expect_rc 2 "a declared bridge that does not exist is FATAL"

: > "$ROOT/bootstrap/DEGRAU"
expect_rc 2 "an empty declaration is FATAL"

if [ "$FAILED" = "1" ]; then
    echo "degrau_test FAILED." >&2
    exit 1
fi
echo "degrau_test OK — the criterion is a declaration, and a broken one never reads as absent."
