#!/usr/bin/env bash
# scripts/native_regressions.sh — native-only CWD-build-regression check (issue #64).
#
# Formerly the native leg of scripts/diff_vm_native.sh (the VM==native differential,
# issue #54); the VM comparison itself was retired in issue #524 (see
# docs/design/vm-retirement.md §3 — the own==C differential, scripts/diff_c_own.sh, is
# its replacement). This script keeps the ONE check that was never about the VM: a
# CWD-relative build regression.
#
#   cd <project-dir> && teko build .    (the exact invocation shape a real user takes)
#
# must exit 0 and produce a working (executable, non-crashing) binary, run for BOTH the
# provisioned seed binary ($TEKO) and the self-hosted engine (./bin/teko, built on the
# fly from the seed if missing), whose src/build/project.tks::ensure_rt_dir_abs used to
# resolve the runtime relative to the CWD instead of the compiler binary's own location
# (PR #66).
#
# usage: scripts/native_regressions.sh
#   TEKO=<path-to-teko-binary>   (default: ./.teko/teko — the fetch_teko.sh-cached seed;
#                                 CI always sets TEKO=./bin/teko, the self-hosted gen1)
#   CWD_REGRESSION_FIXTURE=<dir> (default: examples/regressions/char_ops)

set -u

TEKO="${TEKO:-./.teko/teko}"
CWD_REGRESSION_FIXTURE="${CWD_REGRESSION_FIXTURE:-examples/regressions/char_ops}"

# Run a command with a timeout when `timeout` exists (Linux CI); raw otherwise (macOS dev).
run_limited() {
    if command -v timeout >/dev/null 2>&1; then
        timeout 120 "$@"
    else
        "$@"
    fi
}

# check_cwd_build_regression — issue #64: `cd <project> && teko build .` (the exact
# shape a real user runs from inside their own project) must exit 0 and produce a
# working binary. Takes the compiler binary's ABSOLUTE path and a short label
# ("bootstrap" / "selfhosted") for PASS/FAIL reporting; appends to the shared
# pass/fail counters.
check_cwd_build_regression() {
    local compiler_abs="$1" label="$2"
    local name="$(basename "$CWD_REGRESSION_FIXTURE")"
    local check_name="cwd_build_${label}"
    local out; out="$(mktemp -d "${TMPDIR:-/tmp}/teko-cwd-regress.XXXXXX")"

    if [ ! -x "$compiler_abs" ]; then
        fail=$((fail + 1)); failed_names+=("$check_name")
        printf 'FAIL  %-28s compiler not found/executable at %s\n' "$check_name" "$compiler_abs"
        rm -rf "$out"
        return
    fi

    ( cd "$CWD_REGRESSION_FIXTURE" && run_limited "$compiler_abs" build . -o "$out" ) \
        >"$out/build.stdout" 2>"$out/build.stderr"
    local build_exit=$?

    if [ "$build_exit" -ne 0 ]; then
        fail=$((fail + 1)); failed_names+=("$check_name")
        printf 'FAIL  %-28s cd-into-project build failed (exit=%s), the #64 CWD-relative runtime bug\n' "$check_name" "$build_exit"
        sed 's/^/      | /' "$out/build.stderr" | tail -20
        tail -5 "$out/build.stdout" | sed 's/^/      | /'
        rm -rf "$out"
        return
    fi

    if [ ! -x "$out/$name" ]; then
        fail=$((fail + 1)); failed_names+=("$check_name")
        printf 'FAIL  %-28s build exited 0 but no working binary at %s/%s\n' "$check_name" "$out" "$name"
        rm -rf "$out"
        return
    fi

    run_limited "$out/$name" >"$out/run.stdout" 2>"$out/run.stderr"
    local run_exit=$?
    if [ "$run_exit" -ge 126 ]; then
        fail=$((fail + 1)); failed_names+=("$check_name")
        printf 'FAIL  %-28s binary built but crashed (exit=%s)\n' "$check_name" "$run_exit"
        [ -s "$out/run.stderr" ] && tail -5 "$out/run.stderr" | sed 's/^/      | /'
        rm -rf "$out"
        return
    fi

    pass=$((pass + 1))
    printf 'PASS  %-28s cd-into-project build OK (build exit=0, binary exit=%s)\n' "$check_name" "$run_exit"
    rm -rf "$out"
}

if [ ! -x "$TEKO" ]; then
    echo "native_regressions: teko binary not found/executable at '$TEKO' (set TEKO=...)" >&2
    exit 2
fi

pass=0 fail=0
failed_names=()

echo "native_regressions: teko=$TEKO"
echo

# ── CWD-regression check (issue #64) ────────────────────────────────────────────────────
# `cd <project> && teko build .` must work on BOTH engines: the provisioned seed (whose
# TK_RT_DIR/TK_SRC_DIR may be baked in absolute) and the self-hosted engine (which
# resolves its own runtime at runtime — PR #66 fixed it to use the compiler binary's own
# location instead of the CWD). Absolute paths throughout: the check itself `cd`s.
echo "native_regressions: CWD-regression check (issue #64) — cd \"$CWD_REGRESSION_FIXTURE\" && teko build ."

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bootstrap_abs="$script_dir/${TEKO#./}"
selfhosted_abs="$script_dir/bin/teko"

if [ -x "$bootstrap_abs" ]; then
    check_cwd_build_regression "$bootstrap_abs" "bootstrap"
else
    fail=$((fail + 1)); failed_names+=("cwd_build_bootstrap")
    printf 'FAIL  %-28s bootstrap compiler not found/executable at %s\n' "cwd_build_bootstrap" "$bootstrap_abs"
fi

# Self-host on the fly if ./bin/teko is not already there (a fresh checkout / a dev
# host that only fetched the seed — CI always passes TEKO=./bin/teko already built,
# so this fallback fires only in local/agent use — this is what makes the check work
# with ZERO workflow changes: the harness builds what it needs to test).
if [ ! -x "$selfhosted_abs" ]; then
    if [ -x "$bootstrap_abs" ]; then
        echo "native_regressions: bin/teko not found — self-hosting via $bootstrap_abs build . -o bin"
        if ! run_limited "$bootstrap_abs" build "$script_dir" -o "$script_dir/bin" >/tmp/teko-selfhost-build.log 2>&1; then
            echo "native_regressions: self-host build failed — see /tmp/teko-selfhost-build.log" >&2
            tail -20 /tmp/teko-selfhost-build.log | sed 's/^/      | /'
        fi
    fi
fi

if [ -x "$selfhosted_abs" ]; then
    check_cwd_build_regression "$selfhosted_abs" "selfhosted"
else
    fail=$((fail + 1)); failed_names+=("cwd_build_selfhosted")
    printf 'FAIL  %-28s self-hosted compiler not found/executable at %s (self-host build unavailable)\n' "cwd_build_selfhosted" "$selfhosted_abs"
fi

echo
echo "native_regressions: $pass passed, $fail failed"
[ "$fail" -gt 0 ] && echo "  FAILED: ${failed_names[*]}"

if [ "$fail" -gt 0 ]; then exit 1; fi
exit 0
