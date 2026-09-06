#!/usr/bin/env sh
# scripts/no_c_in_tests_gate_test.sh — the REGRESSION for `scripts/no_c_in_tests_gate.sh`, the gate
# that must fail a lane RED when a native-backend `teko test .` session leaves generated C behind.
#
# WHY THIS DESERVES A TEST OF ITS OWN, when the gate itself is only ever exercised inside a real CI
# run against a real `teko test .` session: the gate exists PRECISELY because a real session can go
# green while lying about the backend it exercised. A gate whose own pass/fail logic is unverified
# is the same shape of blind trust this train exists to remove — so its four load-bearing claims are
# each proven against a fixture, without needing a compiler or a network:
#
#   1. a clean tree (only tracked C) passes;
#   2. one generated (untracked) .c fails, named on stderr;
#   3. a VERSIONED .c/.h — the same shape as src/runtime/teko_rt.c — is never mistaken for a
#      generated one, even sitting right next to a real generated file;
#   4. an absent directory is FATAL (exit 2), never read as "nothing to find, so clean" (exit 0).
#
# Each fixture is a throwaway git repository built in a temp dir, so the test needs no compiler and
# no network and runs in the `CI gate` job alongside `scripts/degrau_test.sh`.
#
# Usage:  sh scripts/no_c_in_tests_gate_test.sh
# POSIX sh only.
set -eu

HERE="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
GATE="$HERE/no_c_in_tests_gate.sh"

FAILED=0
ROOT="$(mktemp -d)"
trap 'rm -rf "$ROOT"' EXIT INT TERM

pass() { printf '  ok   %s\n' "$1"; }
fail() { printf 'ERROR: %s\n' "$1" >&2; FAILED=1; }

# init_repo — a fresh git repository at $ROOT/repo, with the identity git needs to commit. Every
# fixture starts from this so a stale file from the PREVIOUS fixture can never leak into the next.
init_repo() {
    rm -rf "$ROOT/repo"
    mkdir -p "$ROOT/repo"
    ( cd "$ROOT/repo" && git init -q && git config user.email t@example.com && git config user.name t )
}

# commit_tracked PATH... — writes each PATH (relative to the repo) with throwaway content and
# commits it, so the gate's "tracked" set contains exactly the files a caller names.
commit_tracked() {
    for ct_path in "$@"; do
        mkdir -p "$ROOT/repo/$(dirname "$ct_path")"
        printf '/* versioned, tracked-in-git C — not a generated emission */\n' > "$ROOT/repo/$ct_path"
    done
    ( cd "$ROOT/repo" && git add -A && git commit -q -m "fixture: tracked C" )
}

# gate_rc — runs the gate against $ROOT/repo and echoes its exit code, never letting `set -e` end
# the whole test run on the FIRST fixture that is supposed to fail.
gate_rc() {
    gr_rc=0
    sh "$GATE" "$ROOT/repo" >"$ROOT/out" 2>"$ROOT/err" || gr_rc=$?
    printf '%s' "$gr_rc"
}

echo "--- no-c-in-tests gate ---"

# 1. a clean tree — only the tracked runtime twin — passes.
init_repo
commit_tracked "src/runtime/teko_rt.c" "src/runtime/teko_rt.h"
rc="$(gate_rc)"
[ "$rc" = "0" ] && pass "a clean tree (tracked C only) passes (rc=0)" \
    || fail "a clean tree should pass: got rc=$rc, stderr: $(cat "$ROOT/err")"

# 2. one generated (untracked) .c fails, and NAMES the file.
mkdir -p "$ROOT/repo/bin"
printf 'int f(void) { return 1; }\n' > "$ROOT/repo/bin/mini-tktest.c"
rc="$(gate_rc)"
[ "$rc" = "1" ] && pass "one generated .c fails the gate (rc=1)" \
    || fail "a generated .c should fail: got rc=$rc"
grep -q 'bin/mini-tktest\.c' "$ROOT/err" \
    && pass "the failing file is named on stderr" \
    || fail "the generated file was not named: $(cat "$ROOT/err")"

# 2b. a generated .h (not just .c) is caught the same way.
rm -f "$ROOT/repo/bin/mini-tktest.c"
printf '/* generated header */\n' > "$ROOT/repo/bin/mini-tktest.h"
rc="$(gate_rc)"
[ "$rc" = "1" ] && pass "a generated .h fails the gate too (rc=1)" \
    || fail "a generated .h should fail: got rc=$rc"
rm -f "$ROOT/repo/bin/mini-tktest.h"

# 3. a versioned .c/.h is NEVER mistaken for a generated one — even sitting beside a real one.
init_repo
commit_tracked "src/runtime/teko_rt.c" "src/runtime/teko_rt.h" "src/assert/assert.c" "bootstrap/teko.c"
rc="$(gate_rc)"
[ "$rc" = "0" ] && pass "several versioned C/H files together still pass (rc=0)" \
    || fail "versioned C/H should never fail the gate: got rc=$rc, stderr: $(cat "$ROOT/err")"

mkdir -p "$ROOT/repo/bin"
printf 'int f(void) { return 1; }\n' > "$ROOT/repo/bin/teko-regrcov.c"
rc="$(gate_rc)"
[ "$rc" = "1" ] && pass "a generated .c beside versioned ones still fails, and only the generated one" \
    || fail "the generated file beside versioned ones should fail: got rc=$rc"
grep -q 'bin/teko-regrcov\.c' "$ROOT/err" \
    && pass "the generated file is named even with versioned C/H present" \
    || fail "the generated file was not named: $(cat "$ROOT/err")"
grep -q 'teko_rt\.c' "$ROOT/err" \
    && fail "a VERSIONED file must never be named as generated: $(cat "$ROOT/err")" \
    || pass "the versioned files are never named as generated"

# 4. an absent directory is FATAL (exit 2) — never "nothing to find, so clean" (exit 0).
rm -rf "$ROOT/repo"
rc="$(gate_rc)"
[ "$rc" = "2" ] && pass "an absent test-session directory is FATAL (rc=2), never a silent pass" \
    || fail "an absent directory should be FATAL (rc=2): got rc=$rc"

# 5. a directory that is not a git working tree is ALSO FATAL — the gate has no way to tell a
# generated file from a versioned one without git, and a silent pass would be exactly the lie this
# gate exists to end.
mkdir -p "$ROOT/repo"
: > "$ROOT/repo/stray.c"
rc="$(gate_rc)"
[ "$rc" = "2" ] && pass "a non-git directory is FATAL (rc=2), not a false pass or a false fail" \
    || fail "a non-git directory should be FATAL (rc=2): got rc=$rc"

if [ "$FAILED" = "1" ]; then
    echo "no_c_in_tests_gate_test FAILED." >&2
    exit 1
fi
echo "no_c_in_tests_gate_test OK — clean passes, a generated .c/.h fails and is named, a versioned"
echo "one is never mistaken for it, and every unprovable state is FATAL rather than 'clean'."
