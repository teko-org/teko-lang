#!/usr/bin/env bash
# scripts/fmt_cli_test.sh — assert the `teko fmt` CLI contract (issue #229).
#
# `teko fmt` is implemented pure-Teko (src/fmt/fmt.tks) and only exists in a SELF-HOSTED
# binary (the C seed refuses it honestly) — diff_vm_native.sh has no stdin-piping harness,
# so this is a dedicated shell-level smoke test, run against bin/teko (the self-hosted
# binary this PR's own gen1 produces), mirroring cli_flags_test.sh's shape:
#
#   teko fmt --check <file>       unformatted file → prints its path, exit 1
#   teko fmt --check <file>       canonical file    → prints nothing, exit 0
#   teko fmt <file>                rewrites in place, prints the path, exit 0 (then idempotent)
#   teko fmt -                     stdin -> stdout, no file touched, exit 0
#   teko fmt --check -             stdin would change -> "<stdin>", exit 1 ; canonical -> exit 0
#   teko fmt -                     empty stdin -> empty stdout, exit 0
#   teko fmt -                     invalid stdin -> a diagnostic on stderr, exit 2
#
# Usage:  scripts/fmt_cli_test.sh <path/to/teko-binary>
set -eu

BIN="${1:?usage: fmt_cli_test.sh <teko-binary>}"

fail() { echo "fmt_cli_test: FAIL: $*" >&2; exit 1; }

work="$(mktemp -d "${TMPDIR:-/tmp}/teko-fmt-cli.XXXXXX")"
trap 'rm -rf "$work"' EXIT

UNFORMATTED=$'fn a() -> i64 {\nreturn 1\n}\n'
CANONICAL=$'fn a() -> i64 {\n    return 1\n}\n'
INVALID=$'fn (\n'

assert_check_mode() {
    local file="$1" want_rc="$2" want_listed="$3"
    set +e
    out="$("$BIN" fmt --check "$file" 2>"$work/err")"; rc=$?
    set -e
    [ "$rc" -eq "$want_rc" ] || fail "--check $file exit $rc (want $want_rc)"
    if [ "$want_listed" = "yes" ]; then
        [ "$out" = "$file" ] || fail "--check $file stdout '$out' (want '$file')"
    else
        [ -z "$out" ] || fail "--check $file stdout '$out' (want empty)"
    fi
}

# --- file mode: --check on an unformatted file lists it and exits 1 --------------------------
unformatted_file="$work/unformatted.tks"
printf '%s' "$UNFORMATTED" >"$unformatted_file"
assert_check_mode "$unformatted_file" 1 yes

# --- file mode: --check on an already-canonical file is silent and exits 0 -------------------
canonical_file="$work/canonical.tks"
printf '%s' "$CANONICAL" >"$canonical_file"
assert_check_mode "$canonical_file" 0 no

# --- file mode: a plain rewrite fixes the file in place, then --check on it is a no-op -------
rewrite_file="$work/rewrite.tks"
printf '%s' "$UNFORMATTED" >"$rewrite_file"
set +e
out="$("$BIN" fmt "$rewrite_file" 2>"$work/err")"; rc=$?
set -e
[ "$rc" -eq 0 ] || fail "fmt $rewrite_file exit $rc (want 0)"
[ "$out" = "$rewrite_file" ] || fail "fmt $rewrite_file stdout '$out' (want '$rewrite_file')"
got="$(cat "$rewrite_file")"
want="$(printf '%s' "$CANONICAL")"
[ "$got" = "$want" ] || fail "fmt $rewrite_file did not rewrite to the canonical form"
assert_check_mode "$rewrite_file" 0 no

# --- stdin mode: `teko fmt -` formats to stdout, touching no file ----------------------------
set +e
out="$(printf '%s' "$UNFORMATTED" | "$BIN" fmt - 2>"$work/err")"; rc=$?
set -e
[ "$rc" -eq 0 ] || fail "fmt - exit $rc (want 0)"
want="$(printf '%s' "$CANONICAL")"
[ "$out" = "$want" ] || fail "fmt - stdout did not match the canonical form"

# --- stdin mode: `teko fmt --check -` on unformatted input reports "<stdin>", exits 1 --------
set +e
out="$(printf '%s' "$UNFORMATTED" | "$BIN" fmt --check - 2>"$work/err")"; rc=$?
set -e
[ "$rc" -eq 1 ] || fail "fmt --check - (unformatted) exit $rc (want 1)"
[ "$out" = "<stdin>" ] || fail "fmt --check - (unformatted) stdout '$out' (want '<stdin>')"

# --- stdin mode: `teko fmt --check -` on canonical input is silent, exits 0 ------------------
set +e
out="$(printf '%s' "$CANONICAL" | "$BIN" fmt --check - 2>"$work/err")"; rc=$?
set -e
[ "$rc" -eq 0 ] || fail "fmt --check - (canonical) exit $rc (want 0)"
[ -z "$out" ] || fail "fmt --check - (canonical) stdout '$out' (want empty)"

# --- stdin mode: empty input formats to empty output, exit 0 ---------------------------------
set +e
out="$(printf '' | "$BIN" fmt - 2>"$work/err")"; rc=$?
set -e
[ "$rc" -eq 0 ] || fail "fmt - (empty) exit $rc (want 0)"
[ -z "$out" ] || fail "fmt - (empty) stdout '$out' (want empty)"

# --- stdin mode: invalid input is refused with a diagnostic, exit 2 --------------------------
set +e
out="$(printf '%s' "$INVALID" | "$BIN" fmt - 2>"$work/err")"; rc=$?
set -e
[ "$rc" -eq 2 ] || fail "fmt - (invalid) exit $rc (want 2)"
err="$(cat "$work/err")"
printf '%s\n' "$err" | grep -q "teko fmt: <stdin>:" || fail "fmt - (invalid) stderr missing the '<stdin>:' diagnostic prefix"

echo "fmt_cli_test: PASS ($BIN)"
