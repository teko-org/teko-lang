#!/usr/bin/env bash
# scripts/crossmodule_regressions.sh — E2E cross-module `.tkl` dependency harness (#594
# crumb 8d, design doc §c8d: docs/design/const-crumb8-preseed-subsequence.md).
#
# The cross-module const capability (serialize -> .tkl -> deserialize -> seed_from_dep ->
# inline_consts -> lower -> run, #594 crumb 8) is only proven by CHECKER unit tests, whose
# dep is an in-memory `TProgram` — no test builds a REAL `.tkl` and consumes it through the
# actual CLI. examples/regressions/ never provisions a `packages/` directory (`teko build
# <consumer>` would fail to find the dep otherwise), so a dep-consuming fixture cannot ride
# the existing generic build-all loops (sanitizers.yml, scripts/compile_fail_regressions.sh)
# — both glob a `.tkp` directly inside each fixture dir, and a cross-module fixture (a DEP +
# a CONSUMER pair, see below) has none at its own root, so those loops skip it unchanged.
#
# A cross-module fixture is a directory under examples/regressions/ containing a `dep/`
# subdir (a `kind = "package"` library project), a `consumer/` subdir (a `kind = "binary"`
# project with a `[dependencies]` entry naming the dep), and an `EXPECT_EXIT` file (the
# consumer binary's expected process exit code). For each such fixture, and for BOTH the
# provisioned seed compiler and the self-hosted engine (mirroring
# scripts/native_regressions.sh's bootstrap/selfhosted pair — VM vs native is retired,
# issue #524; "both engines" here means both COMPILER GENERATIONS building the same native
# path), this script:
#
#   1. builds the dep (`kind = package`) into a scratch dir -> `<name>-<version>.tkl`;
#   2. copies the consumer project into a scratch working copy, provisions its
#      `packages/` directory with the built `.tkl` (NEVER written into the committed tree —
#      everything lives under a per-check `mktemp -d` scratch root, explicitly `rm -rf` on
#      every exit path, mirroring scripts/native_regressions.sh, so the working tree and
#      fixpoint/byte-identity stay unaffected);
#   3. builds the scratch consumer copy and runs the produced binary, asserting its exit
#      code equals the fixture's `EXPECT_EXIT`.
#
# usage: scripts/crossmodule_regressions.sh
#   TEKO=<path-to-teko-binary>   (default: ./.teko/teko — the fetch_teko.sh-cached seed;
#                                 CI always sets TEKO=./bin/teko, the self-hosted gen1)
#   REGRESSIONS_DIR=<dir>        (default: examples/regressions)

set -euo pipefail

TEKO="${TEKO:-./.teko/teko}"
REGRESSIONS_DIR="${REGRESSIONS_DIR:-examples/regressions}"

# run_limited — run a command with a timeout when `timeout` exists (Linux CI); raw otherwise
# (macOS dev, where `timeout` is not installed by default).
run_limited() {
    if command -v timeout >/dev/null 2>&1; then
        timeout 120 "$@"
    else
        "$@"
    fi
}

# manifest_name — the `name = "…"` value from a `.tkp` manifest file (the FIRST such line;
# a manifest has exactly one). Used to locate the built binary/`.tkl` without hard-coding
# the fixture's directory naming.
manifest_name() {
    local tkp="$1"
    grep -m1 '^name[[:space:]]*=' "$tkp" | sed -E 's/^name[[:space:]]*=[[:space:]]*"([^"]*)".*/\1/'
}

# find_manifest_in — the single `*.tkp` file directly inside a project directory, or empty
# when none/more-than-one is present (mirrors project.tks::find_manifest's "exactly one"
# rule closely enough for the harness to fail loudly on a malformed fixture).
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

# build_dep_tkl — build `dep_dir` (a `kind = package` project) with `compiler_abs` into a
# fresh scratch subdir of `scratch_root`, and print the produced `.tkl` path on success.
# Prints nothing and returns non-zero on any failure (missing manifest, build failure, no
# `.tkl` produced) — the caller reports the fixture FAIL.
build_dep_tkl() {
    local compiler_abs="$1" dep_dir="$2" scratch_root="$3"
    local dep_out="$scratch_root/dep-out"
    mkdir -p "$dep_out"

    local dep_manifest; dep_manifest="$(find_manifest_in "$dep_dir")"
    if [ -z "$dep_manifest" ]; then
        echo "crossmodule_regressions: dep '$dep_dir' has no single .tkp manifest" >&2
        return 1
    fi

    local build_exit=0
    run_limited "$compiler_abs" build "$dep_dir" -o "$dep_out" \
        >"$dep_out/build.stdout" 2>"$dep_out/build.stderr" || build_exit=$?
    if [ "$build_exit" -ne 0 ]; then
        echo "crossmodule_regressions: dep build failed (exit=$build_exit)" >&2
        sed 's/^/      | /' "$dep_out/build.stderr" | tail -20 >&2
        return 1
    fi

    local tkl=""
    for f in "$dep_out"/*.tkl; do
        [ -f "$f" ] || continue
        tkl="$f"
        break
    done
    if [ -z "$tkl" ]; then
        echo "crossmodule_regressions: dep build produced no .tkl in $dep_out" >&2
        return 1
    fi
    echo "$tkl"
}

# provision_consumer_copy — copy `consumer_src` into a fresh scratch subdir of
# `scratch_root`, create its `packages/` directory, and drop `tkl_path` into it. Prints the
# scratch consumer directory's path. The committed `consumer/` fixture tree is NEVER
# written to — the copy is the only thing mutated (#594 crumb 8d design doc R3).
provision_consumer_copy() {
    local consumer_src="$1" tkl_path="$2" scratch_root="$3"
    local work_consumer="$scratch_root/consumer-copy"
    cp -R "$consumer_src" "$work_consumer"
    mkdir -p "$work_consumer/packages"
    cp "$tkl_path" "$work_consumer/packages/"
    echo "$work_consumer"
}

# run_crossmodule_fixture — the full build-dep -> provision -> build-consumer -> run ->
# assert cycle for one fixture, on one compiler. Appends to the shared pass/fail counters
# and PASS/FAIL-prints a `<fixture>_<label>` check name (mirrors
# scripts/native_regressions.sh's `check_name` reporting shape).
run_crossmodule_fixture() {
    local compiler_abs="$1" label="$2" fixture_dir="$3" fixture_name="$4" expect_exit="$5"
    local check_name="crossmodule_${fixture_name}_${label}"
    local scratch_root; scratch_root="$(mktemp -d "${TMPDIR:-/tmp}/teko-crossmodule.XXXXXX")"

    if [ ! -x "$compiler_abs" ]; then
        fail=$((fail + 1)); failed_names+=("$check_name")
        printf 'FAIL  %-42s compiler not found/executable at %s\n' "$check_name" "$compiler_abs"
        rm -rf "$scratch_root"
        return
    fi

    local tkl
    if ! tkl="$(build_dep_tkl "$compiler_abs" "${fixture_dir}dep" "$scratch_root")"; then
        fail=$((fail + 1)); failed_names+=("$check_name")
        printf 'FAIL  %-42s dep build did not produce a .tkl\n' "$check_name"
        rm -rf "$scratch_root"
        return
    fi

    local work_consumer
    if ! work_consumer="$(provision_consumer_copy "${fixture_dir}consumer" "$tkl" "$scratch_root")"; then
        fail=$((fail + 1)); failed_names+=("$check_name")
        printf 'FAIL  %-42s could not provision the consumer working copy\n' "$check_name"
        rm -rf "$scratch_root"
        return
    fi
    local consumer_manifest; consumer_manifest="$(find_manifest_in "$work_consumer")"
    if [ -z "$consumer_manifest" ]; then
        fail=$((fail + 1)); failed_names+=("$check_name")
        printf 'FAIL  %-42s consumer has no single .tkp manifest\n' "$check_name"
        rm -rf "$scratch_root"
        return
    fi
    local consumer_name
    if ! consumer_name="$(manifest_name "$consumer_manifest")" || [ -z "$consumer_name" ]; then
        fail=$((fail + 1)); failed_names+=("$check_name")
        printf 'FAIL  %-42s consumer manifest has no name = "…" line\n' "$check_name"
        rm -rf "$scratch_root"
        return
    fi

    local consumer_out="$scratch_root/consumer-out"
    local build_exit=0
    run_limited "$compiler_abs" build "$work_consumer" -o "$consumer_out" \
        >"$scratch_root/consumer-build.stdout" 2>"$scratch_root/consumer-build.stderr" || build_exit=$?
    if [ "$build_exit" -ne 0 ]; then
        fail=$((fail + 1)); failed_names+=("$check_name")
        printf 'FAIL  %-42s consumer build failed (exit=%s) — dep provisioning or seed_from_dep broke\n' "$check_name" "$build_exit"
        sed 's/^/      | /' "$scratch_root/consumer-build.stderr" | tail -20
        rm -rf "$scratch_root"
        return
    fi

    local binary="$consumer_out/$consumer_name"
    if [ ! -x "$binary" ]; then
        fail=$((fail + 1)); failed_names+=("$check_name")
        printf 'FAIL  %-42s build exited 0 but no working binary at %s\n' "$check_name" "$binary"
        rm -rf "$scratch_root"
        return
    fi

    local run_exit=0
    run_limited "$binary" >"$scratch_root/run.stdout" 2>"$scratch_root/run.stderr" || run_exit=$?
    if [ "$run_exit" -ne "$expect_exit" ]; then
        fail=$((fail + 1)); failed_names+=("$check_name")
        printf 'FAIL  %-42s binary exit=%s, expected %s\n' "$check_name" "$run_exit" "$expect_exit"
        [ -s "$scratch_root/run.stderr" ] && tail -5 "$scratch_root/run.stderr" | sed 's/^/      | /'
        rm -rf "$scratch_root"
        return
    fi

    pass=$((pass + 1))
    printf 'PASS  %-42s dep -> packages/ -> consumer -> run (exit=%s)\n' "$check_name" "$run_exit"
    rm -rf "$scratch_root"
}

if [ ! -x "$TEKO" ]; then
    echo "crossmodule_regressions: teko binary not found/executable at '$TEKO' (set TEKO=...)" >&2
    exit 2
fi

pass=0 fail=0 exercised=0
failed_names=()

echo "crossmodule_regressions: teko=$TEKO"
echo "crossmodule_regressions: scanning $REGRESSIONS_DIR/*/ for dep/+consumer/+EXPECT_EXIT fixtures"
echo

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bootstrap_abs="$script_dir/${TEKO#./}"
selfhosted_abs="$script_dir/bin/teko"

# Self-host on the fly if ./bin/teko is not already there (mirrors
# scripts/native_regressions.sh — a fresh checkout / local agent run builds gen1 itself;
# CI always passes TEKO=./bin/teko already built, so this fallback is a no-op there).
if [ ! -x "$selfhosted_abs" ] && [ -x "$bootstrap_abs" ]; then
    echo "crossmodule_regressions: bin/teko not found — self-hosting via $bootstrap_abs build . -o bin"
    if ! run_limited "$bootstrap_abs" build "$script_dir" -o "$script_dir/bin" --no-verify --release >/tmp/teko-crossmodule-selfhost.log 2>&1; then
        echo "crossmodule_regressions: self-host build failed — see /tmp/teko-crossmodule-selfhost.log" >&2
        tail -20 /tmp/teko-crossmodule-selfhost.log | sed 's/^/      | /'
    fi
fi

for proj in "$REGRESSIONS_DIR"/*/; do
    [ -d "${proj}dep" ] || continue
    [ -d "${proj}consumer" ] || continue
    [ -f "${proj}EXPECT_EXIT" ] || continue

    fixture_name="$(basename "$proj")"
    expect_exit="$(tr -d '[:space:]' <"${proj}EXPECT_EXIT")"
    exercised=$((exercised + 1))

    echo "crossmodule_regressions: fixture $fixture_name (expect exit=$expect_exit)"

    if [ -x "$bootstrap_abs" ]; then
        run_crossmodule_fixture "$bootstrap_abs" "bootstrap" "$proj" "$fixture_name" "$expect_exit"
    else
        fail=$((fail + 1)); failed_names+=("crossmodule_${fixture_name}_bootstrap")
        printf 'FAIL  %-42s bootstrap compiler not found/executable at %s\n' "crossmodule_${fixture_name}_bootstrap" "$bootstrap_abs"
    fi

    if [ -x "$selfhosted_abs" ]; then
        run_crossmodule_fixture "$selfhosted_abs" "selfhosted" "$proj" "$fixture_name" "$expect_exit"
    else
        fail=$((fail + 1)); failed_names+=("crossmodule_${fixture_name}_selfhosted")
        printf 'FAIL  %-42s self-hosted compiler not found/executable at %s (self-host build unavailable)\n' "crossmodule_${fixture_name}_selfhosted" "$selfhosted_abs"
    fi
done

echo
echo "crossmodule_regressions: exercised $exercised fixture(s); $pass passed, $fail failed"
[ "$fail" -gt 0 ] && echo "  FAILED: ${failed_names[*]}"

if [ "$exercised" -eq 0 ]; then
    echo "crossmodule_regressions: no dep/+consumer/+EXPECT_EXIT fixture found under $REGRESSIONS_DIR — nothing exercised" >&2
    exit 1
fi

if [ "$fail" -gt 0 ]; then exit 1; fi
exit 0
