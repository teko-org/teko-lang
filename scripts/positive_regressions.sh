#!/usr/bin/env bash
# scripts/positive_regressions.sh — assert every EXPECT_EXIT positive const fixture actually
# BUILDS and RUNS with the expected exit code (issue #594 crumb 8f).
#
# The positive const fixtures under examples/regressions/ (const_scalar_inline,
# const_agg_bytes_rodata, const_agg_struct_rodata, const_pub_export_survives,
# member_const_scalar, member_const_aggregate, member_const_inherit_shadow,
# member_const_trait_fold, member_const_cross_ns_pub_inherit) each carry an `EXPECT_EXIT`
# marker, but no CI leg ever built and ran them: scripts/native_regressions.sh only smokes
# ONE fixture (char_ops), and scripts/compile_fail_regressions.sh (#610) only exercises the
# NEGATIVE (EXPECT_COMPILE_FAIL) fixtures. A fundamental bug in const resolution
# (collect_with_seed breaking a real project build) was caught only by a unit test — this
# script closes that gap by actually building + running every single-project EXPECT_EXIT
# fixture, mirroring compile_fail_regressions.sh's iteration/reporting shape.
#
# A fixture is exercised when its directory carries an `EXPECT_EXIT` marker file AND is a
# SINGLE-project fixture: exactly one `.tkp` manifest directly inside the directory, plus a
# `src/main.tks` entry point. Two categories are deliberately EXCLUDED, each already owned by
# its own harness:
#
#   - a directory also carrying `EXPECT_COMPILE_FAIL` (the negative fixtures, #610, owned by
#     scripts/compile_fail_regressions.sh — a fixture can't be both positive and negative);
#   - a directory shaped as a dep/+consumer/ pair (the cross-module fixtures, #594 crumb 8d,
#     owned by scripts/crossmodule_regressions.sh — it has no top-level `.tkp` of its own).
#
# VM is retired (issue #524) — this is native-only: `teko build <dir>` then run the produced
# binary.
#
# usage: scripts/positive_regressions.sh
#   TEKO=<path-to-teko-binary>   (default: ./.teko/teko — the fetch_teko.sh-cached seed;
#                                 CI sets TEKO=./bin/teko, the self-hosted gen1 under test)
#   REGRESSIONS_DIR=<dir>        (default: examples/regressions)

set -euo pipefail

TEKO="${TEKO:-./.teko/teko}"
REGRESSIONS_DIR="${REGRESSIONS_DIR:-examples/regressions}"
MARKER_FILE_NAME="EXPECT_EXIT"
COMPILE_FAIL_MARKER_NAME="EXPECT_COMPILE_FAIL"

# run_limited — run a command with a timeout when `timeout` exists (Linux CI); raw otherwise
# (macOS dev, where `timeout` is not installed by default).
run_limited() {
    if command -v timeout >/dev/null 2>&1; then
        timeout 120 "$@"
    else
        "$@"
    fi
}

# manifest_name — the `name = "…"` value from a `.tkp` manifest file (the FIRST such line; a
# manifest has exactly one). Used to locate the built binary without hard-coding the
# fixture's directory naming.
manifest_name() {
    local tkp="$1"
    grep -m1 '^name[[:space:]]*=' "$tkp" | sed -E 's/^name[[:space:]]*=[[:space:]]*"([^"]*)".*/\1/'
}

# find_manifest_in — the single `*.tkp` file directly inside a project directory, or empty
# when none/more-than-one is present.
find_manifest_in() {
    local dir="$1" hits=0 found=""
    for f in "$dir"/*.tkp; do
        [ -f "$f" ] || continue
        found="$f"
        hits=$((hits + 1))
    done
    if [ "$hits" -ne 1 ]; then
        echo ""
        return
    fi
    echo "$found"
}

# is_single_project_fixture — true when `proj` is a single-project fixture: exactly one
# top-level `.tkp` manifest AND a `src/main.tks` entry point. Cross-module fixtures (a
# dep/+consumer/ pair, #594 crumb 8d) have neither at their own root, so this excludes them
# without needing to special-case the dep/consumer shape directly.
is_single_project_fixture() {
    local proj="$1" manifest
    manifest="$(find_manifest_in "$proj")"
    [ -n "$manifest" ] && [ -f "${proj}src/main.tks" ]
}

if [ ! -x "$TEKO" ]; then
    echo "positive_regressions: teko binary not found/executable at '$TEKO' (set TEKO=...)" >&2
    exit 2
fi

pass=0 fail=0 exercised=0
failed_names=()

echo "positive_regressions: teko=$TEKO"
echo "positive_regressions: scanning $REGRESSIONS_DIR/*/ for $MARKER_FILE_NAME (single-project only)"
echo

for proj in "$REGRESSIONS_DIR"/*/; do
    marker_file="${proj}${MARKER_FILE_NAME}"
    [ -f "$marker_file" ] || continue
    [ -f "${proj}${COMPILE_FAIL_MARKER_NAME}" ] && continue
    is_single_project_fixture "$proj" || continue

    name="$(basename "$proj")"
    expect_exit="$(tr -d '[:space:]' <"$marker_file")"
    exercised=$((exercised + 1))
    out="$(mktemp -d "${TMPDIR:-/tmp}/teko-positive.XXXXXX")"

    manifest="$(find_manifest_in "$proj")"
    binary_name="$(manifest_name "$manifest")"

    build_exit=0
    run_limited "$TEKO" build "$proj" -o "$out/bin" >"$out/build.stdout" 2>"$out/build.stderr" || build_exit=$?

    if [ "$build_exit" -ne 0 ]; then
        fail=$((fail + 1)); failed_names+=("$name")
        printf 'FAIL  %-42s build failed (exit=%s), expected a working binary\n' "$name" "$build_exit"
        sed 's/^/      | /' "$out/build.stderr" | tail -20
        rm -rf "$out"
        continue
    fi

    binary="$out/bin/$binary_name"
    if [ ! -x "$binary" ]; then
        fail=$((fail + 1)); failed_names+=("$name")
        printf 'FAIL  %-42s build exited 0 but no working binary at %s\n' "$name" "$binary"
        rm -rf "$out"
        continue
    fi

    run_exit=0
    run_limited "$binary" >"$out/run.stdout" 2>"$out/run.stderr" || run_exit=$?

    if [ "$run_exit" -ne "$expect_exit" ]; then
        fail=$((fail + 1)); failed_names+=("$name")
        printf 'FAIL  %-42s binary exit=%s, expected %s\n' "$name" "$run_exit" "$expect_exit"
        [ -s "$out/run.stderr" ] && tail -5 "$out/run.stderr" | sed 's/^/      | /'
        rm -rf "$out"
        continue
    fi

    pass=$((pass + 1))
    printf 'PASS  %-42s build + run OK (exit=%s)\n' "$name" "$run_exit"
    rm -rf "$out"
done

echo
echo "positive_regressions: exercised $exercised $MARKER_FILE_NAME fixture(s); $pass passed, $fail failed"
[ "$fail" -gt 0 ] && echo "  FAILED: ${failed_names[*]}"

if [ "$exercised" -eq 0 ]; then
    echo "positive_regressions: no single-project $MARKER_FILE_NAME fixture found under $REGRESSIONS_DIR — nothing exercised" >&2
    exit 1
fi

if [ "$fail" -gt 0 ]; then exit 1; fi
exit 0
