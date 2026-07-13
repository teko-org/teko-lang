#!/usr/bin/env bash
# scripts/validate_wasm_own.sh — the own-wasm-backend end-to-end gate (issue #389 C1-5/C1-6).
#
# The own AOT backend's wasm32-wasi target (TEKO_BACKEND=native TEKO_TARGET=wasm32-wasi)
# emits a FINISHED, SELF-CONTAINED `.wasm` module directly (docs/design/backend-wasm.md
# §1.2 — the module IMPORTS its runtime via WASI imports; there is NO relocatable-object
# + external-linker step). Two independent checks, each own-committed and each honest-
# skipped alone when its own tool is absent (never silently fabricating a pass):
#
#   1. STRUCTURAL — `wasm-validate` (WABT) accepts the emitted module (C1-5).
#   2. EXECUTING  — `wasmtime run` actually RUNS it (C1-6, the leg #1's own header
#      pointed at: "the automated wasm leg the C1-6 wasmtime execution differential
#      later builds on"). Every CORPUS fixture is also built C-native (TEKO_BACKEND
#      unset, the trusted oracle) and RUN; own-wasm's own wasmtime exit code (+ stdout,
#      for the `print`-kind fixtures) is compared against it — except the `trap`-kind
#      fixture (`wasm_panic_hook`), where a wasm `unreachable` trap and native's SIGABRT
#      are DIFFERENT termination mechanisms with DIFFERENT raw exit codes by construction
#      (backend-wasm.md §8) — only NONZERO-ness is asserted there, never byte equality.
#
# ── harness-blind-spot guard (mirrors diff_c_own.sh F1(a)) ──────────────────────────────
# A silently-misconfigured `TEKO` (the C bootstrap, or any binary predating emit_native's
# wasm tail) makes the env seam a NO-OP: the "own wasm" build silently falls back to the C
# backend and never writes a `.wasm`. The build's captured output MUST contain
# emit_native_wasm's EXCLUSIVE `"(own backend)"` marker AND the `<name>.wasm` MUST exist on
# disk — either absence is a FAIL, never a silent pass.
#
# HONEST-SKIP (per-check, exit 0, named reason): `wasm-validate` (WABT) absent skips ONLY
# the structural check; `wasmtime` absent skips ONLY the execution check (falls back to
# structural-only, per the crumb's own "try hard, else record + fall back" instruction).
# Both absent skips the whole gate — the build+marker guard above still isn't exercised,
# so nothing is silently claimed to pass.
#
# own_print_exit is EXCLUDED from CORPUS: its message is a STRING INTERPOLATION, and
# `lower_fat_expr`'s closed fat-pointer-producer set does not (yet) include a call result
# (`tk_str_concat`/`tk_i64_to_str`'s own two-register RETURN-value ABI is a separate, wider,
# pre-existing N2 gap, REPORTED — see scripts/diff_c_own.sh's KNOWN_STOP note) — its own-wasm
# build compiler-honest-stops before it ever reaches a module. `wasm_print_exit` (a plain
# string LITERAL, no interpolation) is the C1-6 proof fixture for the same fd_write path
# instead.
#
# usage: scripts/validate_wasm_own.sh [fixture-dir ...]
#   TEKO=<self-hosted-teko>   (default: ./bin/teko — MUST carry emit_native's wasm tail)
#   BUILDER=<seed-teko>       (default: ./build/teko then `teko` on PATH) — self-hosts
#                             ./bin/teko when it is missing

set -u

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

have_validate=1
command -v wasm-validate >/dev/null 2>&1 || have_validate=0
have_wasmtime=1
command -v wasmtime >/dev/null 2>&1 || have_wasmtime=0

if [[ "$have_validate" -eq 0 && "$have_wasmtime" -eq 0 ]]; then
    echo "validate_wasm_own: skipped — neither wasm-validate (WABT) nor wasmtime found on PATH; install one to check the own-wasm modules"
    exit 0
fi
if [[ "$have_validate" -eq 0 ]]; then
    echo "validate_wasm_own: wasm-validate (WABT) not found — the structural check is skipped, wasmtime execution still runs"
fi
if [[ "$have_wasmtime" -eq 0 ]]; then
    echo "validate_wasm_own: wasmtime not found — the C1-6 execution differential is skipped, falling back to wasm-validate structural-only"
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
# The exit(n)/arithmetic/control-flow corpus (mirroring diff_c_own.sh) plus the C1-6 proof
# fixtures. KIND is a parallel array (no `declare -A` — macOS ships bash 3.2, no associative
# arrays, mirroring diff_c_own.sh's own KNOWN_STOP/KNOWN_STOP_ERR shape):
#   exit  — compare own-wasm's wasmtime exit code against the C-native oracle's, exactly.
#   print — exit code AND stdout compared against the C-native oracle, exactly.
#   trap  — own-wasm's wasmtime exit is asserted NONZERO only (a wasm trap and native's
#           SIGABRT are different termination mechanisms — never byte-compared, §8).
# wasm_defer_arm_scope (PR #553 review finding) is `print`-kind ON PURPOSE: a leaked
# then-arm `defer` firing unconditionally past the if's merge is a STDOUT divergence
# ("AB" vs the C-native oracle's "B"), not an exit-code one — the exact shape a bare
# `exit`-kind fixture would silently miss.
CORPUS=(
    own_exit_zero
    own_exit_code
    own_arith_exit
    own_sub_exit
    own_if_exit
    own_match_exit
    wasm_print_exit
    wasm_panic_hook
    wasm_defer_arm_scope
)
KIND=(
    exit
    exit
    exit
    exit
    exit
    exit
    print
    trap
    print
)

kind_of() {
    local n="$1" i
    for i in "${!CORPUS[@]}"; do
        if [[ "${CORPUS[$i]}" == "$n" ]]; then printf '%s' "${KIND[$i]}"; return 0; fi
    done
    printf 'exit'
}

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

echo "validate_wasm_own: teko=$teko_abs — ${#fixtures[@]} fixture(s) (validate=$have_validate wasmtime=$have_wasmtime)"
echo

# check_execution — the C1-6 wasmtime leg for one already-validated fixture: build +
# run the C-native oracle, run own-wasm under `wasmtime run`, then compare per `kind`
# (`exit`/`print` — exact; `trap` — nonzero-only). Prints PASS/FAIL itself and returns
# non-zero on failure so the caller can bump its own counters.
check_execution() {
    local name="$1" proj="$2" wasmp="$3" kind="$4"
    local cout="$work/$name-c"
    ( unset TEKO_BACKEND; "$teko_abs" build "$proj" -o "$cout" ) >"$work/$name.cbuild" 2>&1
    if [[ $? -ne 0 ]]; then
        printf 'FAIL  %-16s C-native oracle build failed (wasmtime leg)\n' "$name"
        tail -8 "$work/$name.cbuild" | sed 's/^/      | /'
        return 1
    fi
    "$cout/$name" >"$work/$name.cout" 2>&1
    local c_exit=$?
    wasmtime run "$wasmp" >"$work/$name.wout" 2>"$work/$name.werr"
    local w_exit=$?
    if [[ "$kind" == "trap" ]]; then
        if [[ "$w_exit" -eq 0 ]]; then
            printf 'FAIL  %-16s own-wasm wasmtime exit=0 — the panic-hook trap did NOT surface as a nonzero exit\n' "$name"
            return 1
        fi
        printf 'PASS  %-16s own-wasm traps under wasmtime (exit=%s, C-native SIGABRT exit=%s) — observe+trap, never byte-compared\n' "$name" "$w_exit" "$c_exit"
        return 0
    fi
    if [[ "$w_exit" -ne "$c_exit" ]]; then
        printf 'FAIL  %-16s exit mismatch: own-wasm(wasmtime)=%s C-native=%s\n' "$name" "$w_exit" "$c_exit"
        return 1
    fi
    if [[ "$kind" == "print" ]] && ! diff -q "$work/$name.cout" "$work/$name.wout" >/dev/null 2>&1; then
        printf 'FAIL  %-16s stdout mismatch: own-wasm(wasmtime) vs C-native\n' "$name"
        diff "$work/$name.cout" "$work/$name.wout" | sed 's/^/      | /'
        return 1
    fi
    printf 'PASS  %-16s own-wasm(wasmtime) == C-native (exit=%s%s)\n' "$name" "$c_exit" "$([[ "$kind" == "print" ]] && echo ", stdout matched")"
    return 0
}

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

    ok=1
    if [[ "$have_validate" -eq 1 ]]; then
        if ! wasm-validate "$wasmp" >"$work/$name.validate" 2>&1; then
            fail=$((fail + 1)); failed_names+=("$name")
            printf 'FAIL  %-16s wasm-validate rejected the emitted %s.wasm\n' "$name" "$name"
            tail -8 "$work/$name.validate" | sed 's/^/      | /'
            ok=0
        else
            printf 'PASS  %-16s own-wasm module wasm-validate-clean\n' "$name"
        fi
    fi
    if [[ "$ok" -eq 1 && "$have_wasmtime" -eq 1 ]]; then
        if ! check_execution "$name" "$proj" "$wasmp" "$(kind_of "$name")"; then
            fail=$((fail + 1)); failed_names+=("$name")
            ok=0
        fi
    fi
    [[ "$ok" -eq 1 ]] && pass=$((pass + 1))
done

echo
echo "validate_wasm_own: $pass passed, $fail failed"
[[ "$fail" -gt 0 ]] && echo "  FAILED: ${failed_names[*]}"
[[ "$fail" -gt 0 ]] && exit 1
exit 0
