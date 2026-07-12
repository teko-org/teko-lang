#!/usr/bin/env bash
# scripts/validate_wasm_own.sh — the own-wasm-backend end-to-end gate (issue #389 C1-5).
#
# The own AOT backend's wasm32-wasi target (TEKO_BACKEND=native TEKO_TARGET=wasm32-wasi)
# emits a FINISHED, SELF-CONTAINED `.wasm` module directly (docs/design/backend-wasm.md
# §1.2 — the module IMPORTS its runtime via WASI imports; there is NO relocatable-object
# + external-linker step). This harness backs the end-to-end claim with a committed
# check: over the exit(n)/arithmetic/control-flow corpus, it builds each fixture with the
# own wasm backend and asserts the emitted module VALIDATES (`wasm-validate`, WABT's
# structural verifier) — the automated wasm leg the C1-6 wasmtime execution differential
# later builds on (execution stays C1-6; this gate stops at structural validity).
#
# ── harness-blind-spot guard (mirrors diff_c_own.sh F1(a)) ──────────────────────────────
# A silently-misconfigured `TEKO` (the C bootstrap, or any binary predating emit_native's
# wasm tail) makes the env seam a NO-OP: the "own wasm" build silently falls back to the C
# backend and never writes a `.wasm`. The build's captured output MUST contain
# emit_native_wasm's EXCLUSIVE `"(own backend)"` marker AND the `<name>.wasm` MUST exist on
# disk — either absence is a FAIL, never a silent pass.
#
# HONEST-SKIP (exit 0, named reason): `wasm-validate` (WABT) absent — the emit path still
# runs, but its bytes go structurally unchecked, so the gate reports the skip rather than
# fabricating a pass.
#
# usage: scripts/validate_wasm_own.sh [fixture-dir ...]
#   TEKO=<self-hosted-teko>   (default: ./bin/teko — MUST carry emit_native's wasm tail)
#   BUILDER=<seed-teko>       (default: ./build/teko then `teko` on PATH) — self-hosts
#                             ./bin/teko when it is missing

set -u

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v wasm-validate >/dev/null 2>&1; then
    echo "validate_wasm_own: skipped — wasm-validate (WABT) not found on PATH; install wabt to structurally verify the own-wasm modules"
    exit 0
fi

TEKO="${TEKO:-./bin/teko}"
BUILDER="${BUILDER:-./build/teko}"
OWN_BACKEND_MARKER="(own backend)"

resolve_abs() {
    case "$1" in
        /*) printf '%s' "$1" ;;
        *)  printf '%s/%s' "$script_dir" "${1#./}" ;;
    esac
}

teko_abs="$(resolve_abs "$TEKO")"
builder_abs="$(resolve_abs "$BUILDER")"

if [[ ! -x "$teko_abs" ]]; then
    if [[ ! -x "$builder_abs" ]]; then
        if command -v teko >/dev/null 2>&1; then builder_abs="$(command -v teko)"; fi
    fi
    if [[ -x "$builder_abs" ]]; then
        echo "validate_wasm_own: $TEKO not found — self-hosting via $builder_abs build $script_dir -o bin"
        if ! "$builder_abs" build "$script_dir" -o "$script_dir/bin" >/tmp/teko-valwasm-selfhost.log 2>&1; then
            echo "validate_wasm_own: self-host build failed — see /tmp/teko-valwasm-selfhost.log" >&2
            tail -20 /tmp/teko-valwasm-selfhost.log | sed 's/^/      | /'
            exit 2
        fi
    fi
fi

if [[ ! -x "$teko_abs" ]]; then
    echo "validate_wasm_own: no self-hosted teko at '$TEKO' (build it: teko . -o bin) — cannot run the wasm gate" >&2
    exit 2
fi

fixture_root="$script_dir/examples/regressions"
# The exit(n)/arithmetic/control-flow corpus, mirroring diff_c_own.sh — every fixture
# whose own pipeline lowers end-to-end. own_print_exit is EXCLUDED: `teko::io::println`
# has no LIR builtin mapping yet (the same REPORTED gap diff_c_own.sh known-stops), so its
# own build never reaches a module.
CORPUS=(
    own_exit_zero
    own_exit_code
    own_arith_exit
    own_sub_exit
    own_if_exit
    own_match_exit
)

fixtures=()
if [[ "$#" -gt 0 ]]; then
    fixtures=("$@")
else
    for name in "${CORPUS[@]}"; do fixtures+=("$fixture_root/$name"); done
fi

work="$(mktemp -d "${TMPDIR:-/tmp}/teko-valwasm.XXXXXX")"
trap 'rm -rf "$work"' EXIT

pass=0 fail=0
failed_names=()

echo "validate_wasm_own: teko=$teko_abs — ${#fixtures[@]} fixture(s) (own wasm32-wasi module wasm-validate)"
echo

for proj in "${fixtures[@]}"; do
    name="$(basename "$proj")"
    oout="$work/$name-wasm"

    env TEKO_BACKEND=native TEKO_TARGET=wasm32-wasi "$teko_abs" build "$proj" -o "$oout" >"$work/$name.wbuild" 2>&1
    build_rc=$?

    if [[ "$build_rc" -ne 0 ]]; then
        fail=$((fail + 1)); failed_names+=("$name")
        printf 'FAIL  %-16s own-wasm build failed (TEKO_BACKEND=native TEKO_TARGET=wasm32-wasi)\n' "$name"
        tail -8 "$work/$name.wbuild" | sed 's/^/      | /'
        continue
    fi

    if ! grep -qF "$OWN_BACKEND_MARKER" "$work/$name.wbuild"; then
        fail=$((fail + 1)); failed_names+=("$name")
        printf 'FAIL  %-16s own-wasm build did NOT report "%s" — the env seam was a silent no-op (wrong/stale teko?), NOT a real own-wasm build\n' "$name" "$OWN_BACKEND_MARKER"
        tail -8 "$work/$name.wbuild" | sed 's/^/      | /'
        continue
    fi

    wasmp="$oout/$name.wasm"
    if [[ ! -f "$wasmp" ]]; then
        fail=$((fail + 1)); failed_names+=("$name")
        printf 'FAIL  %-16s own-wasm build reported success but no %s.wasm was written\n' "$name" "$name"
        continue
    fi

    if ! wasm-validate "$wasmp" >"$work/$name.validate" 2>&1; then
        fail=$((fail + 1)); failed_names+=("$name")
        printf 'FAIL  %-16s wasm-validate rejected the emitted %s.wasm\n' "$name" "$name"
        tail -8 "$work/$name.validate" | sed 's/^/      | /'
        continue
    fi

    pass=$((pass + 1))
    printf 'PASS  %-16s own-wasm module wasm-validate-clean\n' "$name"
done

echo
echo "validate_wasm_own: $pass passed, $fail failed"
[[ "$fail" -gt 0 ]] && echo "  FAILED: ${failed_names[*]}"
[[ "$fail" -gt 0 ]] && exit 1
exit 0
