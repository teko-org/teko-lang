#!/usr/bin/env bash
# scripts/compile_fail_regressions.sh — assert every EXPECT_COMPILE_FAIL fixture actually FAILS
# to build (issue #610).
#
# scripts/diff_vm_native.sh (the historical home of this assertion) was retired with the VM lane
# (#524/#395); scripts/native_regressions.sh only smokes ONE positive fixture (char_ops);
# .github/workflows/sanitizers.yml (~L99/191) only SKIPS every dir carrying an EXPECT_COMPILE_FAIL
# marker file. The net effect: ~30 negative regression fixtures under examples/regressions/ pass
# green WITHOUT ever being built — a fixture that silently starts compiling again (a real checker
# regression) would go unnoticed forever. This script closes that gap: it iterates every
# examples/regressions/*/ dir carrying the EXPECT_COMPILE_FAIL marker file, builds it, and asserts
# the build FAILS (exit != 0).
#
# Per-fixture message pin (opt-in, back-compatible): the marker file today holds free-form prose
# documentation for humans (why the fixture is rejected) — NOT a literal stderr substring, so this
# script does not treat the whole file as a marker. A fixture MAY additionally pin the exact
# rejection message by adding one or more lines of the form:
#
#   EXPECT_STDERR: <substring>
#
# Any such line makes this script assert the given substring literally appears in the build's
# stderr, in addition to the exit-code check. A marker file with no such line (every current
# fixture) only asserts exit != 0 — so today's ~30 fixtures are exercised unchanged.
#
# usage: scripts/compile_fail_regressions.sh
#   TEKO=<path-to-teko-binary>       (default: ./.teko/teko — the fetch_teko.sh-cached seed;
#                                     CI sets TEKO=./bin/teko, the self-hosted gen1 under test)
#   REGRESSIONS_DIR=<dir>            (default: examples/regressions)

set -euo pipefail

TEKO="${TEKO:-./.teko/teko}"
REGRESSIONS_DIR="${REGRESSIONS_DIR:-examples/regressions}"
MARKER_FILE_NAME="EXPECT_COMPILE_FAIL"
STDERR_MARKER_PREFIX="EXPECT_STDERR:"

# run_limited — run a command with a timeout when `timeout` exists (Linux CI); raw otherwise
# (macOS dev, where `timeout` is not installed by default).
run_limited() {
    if command -v timeout >/dev/null 2>&1; then
        timeout 120 "$@"
    else
        "$@"
    fi
}

# assert_stderr_markers — check every `EXPECT_STDERR: <substring>` line in a fixture's marker
# file against the build's captured stderr. Prints a FAIL line per unmet marker and returns
# non-zero when any marker is missing; a marker file with no such line always succeeds (the
# back-compatible default — plain prose is not matched).
assert_stderr_markers() {
    local name="$1" marker_file="$2" stderr_file="$3"
    local ok=0 line needle

    while IFS= read -r line; do
        case "$line" in
        "$STDERR_MARKER_PREFIX"*)
            needle="${line#"$STDERR_MARKER_PREFIX"}"
            needle="${needle# }"
            if ! grep -qF -- "$needle" "$stderr_file"; then
                ok=1
                printf 'FAIL  %-42s stderr missing pinned marker: %s\n' "$name" "$needle"
            fi
            ;;
        esac
    done <"$marker_file"

    return "$ok"
}

if [ ! -x "$TEKO" ]; then
    echo "compile_fail_regressions: teko binary not found/executable at '$TEKO' (set TEKO=...)" >&2
    exit 2
fi

pass=0 fail=0 exercised=0
failed_names=()

echo "compile_fail_regressions: teko=$TEKO"
echo "compile_fail_regressions: scanning $REGRESSIONS_DIR/*/ for $MARKER_FILE_NAME"
echo

for proj in "$REGRESSIONS_DIR"/*/; do
    marker_file="${proj}${MARKER_FILE_NAME}"
    [ -f "$marker_file" ] || continue

    name="$(basename "$proj")"
    exercised=$((exercised + 1))
    out="$(mktemp -d "${TMPDIR:-/tmp}/teko-compile-fail.XXXXXX")"

    build_exit=0
    run_limited "$TEKO" build "$proj" -o "$out/bin" >"$out/stdout" 2>"$out/stderr" || build_exit=$?

    if [ "$build_exit" -eq 0 ]; then
        fail=$((fail + 1)); failed_names+=("$name")
        printf 'FAIL  %-42s built successfully (exit=0) but MUST be rejected\n' "$name"
        rm -rf "$out"
        continue
    fi

    if ! assert_stderr_markers "$name" "$marker_file" "$out/stderr"; then
        fail=$((fail + 1)); failed_names+=("$name")
        rm -rf "$out"
        continue
    fi

    pass=$((pass + 1))
    printf 'PASS  %-42s build correctly rejected (exit=%s)\n' "$name" "$build_exit"
    rm -rf "$out"
done

echo
echo "compile_fail_regressions: exercised $exercised EXPECT_COMPILE_FAIL fixture(s); $pass passed, $fail failed"
[ "$fail" -gt 0 ] && echo "  FAILED: ${failed_names[*]}"

if [ "$exercised" -eq 0 ]; then
    echo "compile_fail_regressions: no EXPECT_COMPILE_FAIL fixture found under $REGRESSIONS_DIR — nothing exercised" >&2
    exit 1
fi

if [ "$fail" -gt 0 ]; then exit 1; fi
exit 0
