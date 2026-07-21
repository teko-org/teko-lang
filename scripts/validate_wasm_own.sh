#!/usr/bin/env bash
# scripts/validate_wasm_own.sh — the own-wasm-backend end-to-end gate (issue #389 C1-5/C1-6/C1-7).
#
# The own AOT backend's wasm32-wasi target (TEKO_BACKEND=native TEKO_TARGET=wasm32-wasi)
# emits a FINISHED, SELF-CONTAINED `.wasm` module directly (docs/design/backend-wasm.md
# §1.2 — the module IMPORTS its runtime via WASI imports; there is NO relocatable-object
# + external-linker step). Two independent checks, each own-committed and each honest-
# skipped alone when its own tool is absent (never silently fabricating a pass):
#
# C1-7 adds the wasm64 switch (§6): one corpus fixture (`wasm64_arith_exit`) builds
# under TEKO_TARGET=wasm64-wasi instead (the per-fixture TARGET array, `target_of`) —
# its own-wasm leg passes wasmtime's `-W memory64=y` and wasm-validate's own
# `--enable-memory64` (the memory64 proposal is not enabled by default in either tool).
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
#   REQUIRE_WASM_ENGINE=1     (default: unset) — issue #389 C1-8c keystone seam. The dev-
#                             laptop default (unset) keeps the honest-skip below: a missing
#                             wasm-validate/wasmtime degrades the gate rather than failing
#                             the build. CI's wasm leg PROVISIONS both engines and sets this
#                             so a broken/absent provisioning step is a HARD FAILURE instead
#                             of a silently-green skip — the whole point of the keystone is
#                             that #389 never closes on a vacuously-passing leg.

set -u

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

require_engine="${REQUIRE_WASM_ENGINE:-0}"

have_validate=1
command -v wasm-validate >/dev/null 2>&1 || have_validate=0
have_wasmtime=1
command -v wasmtime >/dev/null 2>&1 || have_wasmtime=0

if [[ "$require_engine" -eq 1 && ( "$have_validate" -eq 0 || "$have_wasmtime" -eq 0 ) ]]; then
    echo "validate_wasm_own: REQUIRE_WASM_ENGINE=1 but wasm-validate=$have_validate wasmtime=$have_wasmtime — failing closed (this mode never honest-skips)" >&2
    exit 2
fi

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
        if ! "$builder_abs" build "$script_dir" -o "$script_dir/bin" --no-verify --release >/tmp/teko-valwasm-selfhost.log 2>&1; then
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
# `exit`-kind fixture would silently miss. `wasm64_arith_exit` (#389 C1-7, backend-
# wasm.md §6) is the wasm64-switch proof — TARGET is a THIRD parallel array (default
# "wasm32-wasi" via `target_of`'s own fallback) so this one fixture alone builds under
# TEKO_TARGET=wasm64-wasi and its own-wasm leg runs with wasmtime's `-W memory64=y`/
# wasm-validate's `--enable-memory64` (the memory64 proposal is NOT on by default).
#
# Three of the five §14.1/§12.2 FIX-1/FIX-2 engine-level control-flow proofs (#389 C1-8b)
# join the corpus here: `wasm_if_both_diverge` proves FIX-1(b) (an if/else whose both arms
# diverge has no merge block, so neither arm gets an arm label); `wasm_break_in_if` proves
# FIX-1(c) (a break nested in an if inside a loop still gets its single-predecessor loop-exit
# labeled unconditionally); `wasm_labeled_break` proves #520 (a labeled break resolves to a
# multi-level `br N` at the right scope depth). All three are PLAIN `loop { }` shapes (no
# range/`for` head) — verified own-wasm(wasmtime) == C-native.
#
# `own_if_reassign_exit` joins the corpus as F1's (#389) own-wasm proof: `mut x = 0; if 1 == 1
# { x = 5 }; exit(x)` -> 5, own-wasm(wasmtime) == C-native, EMPIRICALLY VERIFIED (self-hosted
# build + wasmtime run). `own_if_mut_shadow_no_leak` joins as F1's review round 1, Finding 2(i)
# proof: `mut x = 1; if 1 == 1 { mut x = 99; x = x + 1 }; exit(x)` -> 1, ALSO EMPIRICALLY
# VERIFIED own-wasm(wasmtime) == C-native. Both prove the LIR-level merge-scalar fix
# (`reassigned_scalars`/`collect_bound_stmts`, `src/lir/lower.tks`) is correct for the own-wasm
# backend on a NON-LOOP if/match merge.
#
# `wasm_loop_count` (FIX-2, sum(0..4) = 6) and `wasm_continue_step` (§11.5, sum of the even i
# in 0..6 = 6) join the corpus with the F1c fix (#389). F1c RETARGETS the earlier "stackifier
# gap" diagnosis, which was WRONG: `src/backend/stackify.tks` is correct and untouched. The
# real root was in the SHARED LIR — `src/lir/lower.tks` lowered `!` (logical-not) to the
# BITWISE one's-complement (LUnOp::INot), and for a bool operand `!1 = 0xFFFFFFFE` / `!0 =
# 0xFFFFFFFF` are BOTH nonzero, so a conditional branch (tests nonzero) took the then-arm for
# `!true` AND `!false` alike. The range-`for` desugar (`src/parser/loop_head.tks`: `if !_first
# { i += 1 }` and `if !(i < _hi) { break }`) therefore broke on iteration 1 -> exit 0. F1c
# lowers `!` to a LOGICAL not (`operand == 0`), so these loops now iterate correctly and
# own-wasm(wasmtime) == C-native == 6. `own_logical_not` joins as the DIRECT proof of `!` on
# both own backends (the first own-backend fixture to exercise `!` at all).
#
# `own_logical_not` (F1c, #389): `mut b = true; if !b { exit(1) }; if !(1 < 2) { exit(2) };
# if !(2 < 1) { exit(7) }; exit(0)` -> 7. `!true` (both `!b` and `!(1<2)`) is false so exit(1)
# and exit(2) are SKIPPED; `!(2<1)` = `!false` is TRUE so exit(7) FIRES. The old bitwise-INot
# would have fired exit(1) first. own-wasm(wasmtime) == C-native on exit 7.
#
# `own_match_pattern_binding_no_collision` (F1's review round 1, Finding 2(ii) proof — a
# `Shape` variant's `Circle as x` arm collides by name with the enclosing `x` a sibling arm
# reassigns) is verified own-native == C-native == 1 via `scripts/diff_c_own.sh` (EMPIRICALLY
# RE-VERIFIED), but is WITHHELD from THIS (wasm) corpus: its own-wasm build's `.wasm` fails
# `wasm-validate` with a genuine engine rejection ("type mismatch in i64.store, expected [i32,
# i64] but got [i32, i32]") — a SEPARATE, pre-existing own-wasm struct/variant field-store gap
# (unrelated to scalar merge-threading), exposed because no prior wasm-corpus fixture combined
# a variant CONSTRUCTION with a pattern-bind arm. REPORTED alongside the F1c stackifier finding
# above; also out of this crumb's scope.
#
# `own_match_arm_reassign_vs_shadow` (6), `own_if_value_rhs_shadow_then_outer_reassign` (7) and
# `own_match_reassign_then_shadow` (3) join with F1b item 3 (#389, §6.2 — the across-all-arms
# over-exclusion fix). Enclosing-IDENTITY threading (`arm_scalar_args` reads each arm's jump-arg
# at the threaded name's fixed PRE-BRANCH index) drops the coarse shadow exclusion: a legitimately
# reassigned enclosing scalar is threaded even when a SIBLING arm shadows the name, and the
# reassign-then-shadow hard case threads the pre-shadow enclosing value. All three run plain
# if/match merges (no struct/variant field-store), so own-wasm(wasmtime) == C-native on exit
# 6/7/3 respectively.
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
    wasm64_arith_exit
    wasm_if_both_diverge
    wasm_break_in_if
    wasm_labeled_break
    own_if_reassign_exit
    own_if_mut_shadow_no_leak
    own_logical_not
    wasm_loop_count
    wasm_continue_step
    own_defer_arm_write_propagates
    own_value_if_rhs_reassign
    own_match_arm_reassign_vs_shadow
    own_if_value_rhs_shadow_then_outer_reassign
    own_match_reassign_then_shadow
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
    exit
    exit
    exit
    exit
    exit
    exit
    exit
    exit
    exit
    exit
    exit
    exit
    exit
    exit
)
TARGET=(
    wasm32-wasi
    wasm32-wasi
    wasm32-wasi
    wasm32-wasi
    wasm32-wasi
    wasm32-wasi
    wasm32-wasi
    wasm32-wasi
    wasm32-wasi
    wasm64-wasi
    wasm32-wasi
    wasm32-wasi
    wasm32-wasi
    wasm32-wasi
    wasm32-wasi
    wasm32-wasi
    wasm32-wasi
    wasm32-wasi
    wasm32-wasi
    wasm32-wasi
    wasm32-wasi
    wasm32-wasi
    wasm32-wasi
)

kind_of() {
    local n="$1" i
    for i in "${!CORPUS[@]}"; do
        if [[ "${CORPUS[$i]}" == "$n" ]]; then printf '%s' "${KIND[$i]}"; return 0; fi
    done
    printf 'exit'
}

# target_of — the `TEKO_TARGET` value fixture `n` builds under: `TARGET`'s own entry
# when `n` is a known corpus name, else the wasm32-wasi default (a fixture passed on the
# command line, outside the named corpus, still gets a sane default rather than an
# empty/invalid target).
target_of() {
    local n="$1" i
    for i in "${!CORPUS[@]}"; do
        if [[ "${CORPUS[$i]}" == "$n" ]]; then printf '%s' "${TARGET[$i]}"; return 0; fi
    done
    printf 'wasm32-wasi'
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
# non-zero on failure so the caller can bump its own counters. `target`'s own wasm64
# arm (#389 C1-7) passes `-W memory64=y` — the memory64 proposal is not on by default.
check_execution() {
    local name="$1" proj="$2" wasmp="$3" kind="$4" target="$5"
    local cout="$work/$name-c"
    ( unset TEKO_BACKEND; "$teko_abs" build "$proj" -o "$cout" --no-verify --release ) >"$work/$name.cbuild" 2>&1
    if [[ $? -ne 0 ]]; then
        printf 'FAIL  %-16s C-native oracle build failed (wasmtime leg)\n' "$name"
        tail -8 "$work/$name.cbuild" | sed 's/^/      | /'
        return 1
    fi
    "$cout/$name" >"$work/$name.cout" 2>&1
    local c_exit=$?
    if [[ "$target" == "wasm64-wasi" ]]; then
        wasmtime run -W memory64=y "$wasmp" >"$work/$name.wout" 2>"$work/$name.werr"
    else
        wasmtime run "$wasmp" >"$work/$name.wout" 2>"$work/$name.werr"
    fi
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
    target="$(target_of "$name")"

    env TEKO_BACKEND=native TEKO_TARGET="$target" "$teko_abs" build "$proj" -o "$oout" --no-verify --release >"$work/$name.wbuild" 2>&1
    build_rc=$?

    if [[ "$build_rc" -ne 0 ]]; then
        fail=$((fail + 1)); failed_names+=("$name")
        printf 'FAIL  %-16s own-wasm build failed (TEKO_BACKEND=native TEKO_TARGET=%s)\n' "$name" "$target"
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
        validate_flags=()
        [[ "$target" == "wasm64-wasi" ]] && validate_flags+=(--enable-memory64)
        if ! wasm-validate "${validate_flags[@]}" "$wasmp" >"$work/$name.validate" 2>&1; then
            fail=$((fail + 1)); failed_names+=("$name")
            printf 'FAIL  %-16s wasm-validate rejected the emitted %s.wasm\n' "$name" "$name"
            tail -8 "$work/$name.validate" | sed 's/^/      | /'
            ok=0
        else
            printf 'PASS  %-16s own-wasm module wasm-validate-clean (target=%s)\n' "$name" "$target"
        fi
    fi
    if [[ "$ok" -eq 1 && "$have_wasmtime" -eq 1 ]]; then
        if ! check_execution "$name" "$proj" "$wasmp" "$(kind_of "$name")" "$target"; then
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
