#!/usr/bin/env bash
# scripts/diff_c_own.sh — the C-vs-own differential (issue #385, A4-5 KEYSTONE).
#
# The own AOT backend (src/backend/{isel,regalloc,encode_arm64,objfile_macho}.tks +
# src/build/project.tks::emit_native) now produces a REAL native executable. This
# harness is the first light of the C-vs-own differential: over an exit(n)-terminated
# integer/control corpus, it builds each fixture TWICE with the SAME self-hosted
# compiler —
#
#   1. C-native   (the trusted, default backend)      : teko build <fix> -o <cbin>
#   2. own-native (TEKO_BACKEND=native, the own AOT)   : teko build <fix> -o <ownbin>
#
# and asserts C-native exit == own-native exit AND identical program stdout. The
# corpus is exit(n)-scoped BY DESIGN (§9.2): the own-backend `main` for exit(n) is
# frameless (a `bl _tk_exit`, no `MRet`), exactly the subset the LIR interp oracle
# and A2/A3 already validate. Both backends honor the #423 trailing-exit ruling
# (a trailing loose expression IS the exit code — verified: C-native and own-native
# both return the tail value), so the exit(n) scope is a corpus convention, not a
# semantic divergence.
#
# For each own-native build the harness also runs scripts/check_macho.sh on the REAL
# emitted `.o` (which A4-4 could only pin via golden bytes — now there is an object
# on disk), so the host toolchain (otool/nm/ld -r) cross-checks the writer's bytes.
#
# GATED on macOS-arm64 (needs the host Mach-O `ld` reachable through `cc`); on any
# other host the harness HONEST-SKIPS with a named reason (exit 0). The own arm64
# encoder emits Mach-O only — ELF/PE are Phases B/C.
#
# usage: scripts/diff_c_own.sh [fixture-dir ...]
#   TEKO=<self-hosted-teko>   (default: ./bin/teko — MUST carry emit_native; the C
#                              bootstrap ./build/teko / the seed does NOT, so the env
#                              seam would be a silent no-op there — never use the seed)
#   BUILDER=<seed-teko>       (default: ./build/teko then `teko` on PATH) — used ONLY
#                              to self-host ./bin/teko when it is missing

set -u

if [[ "$(uname)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
    echo "diff_c_own: skipped — the own backend emits arm64 Mach-O; needs a macOS-arm64 host (cc/ld) on $(uname)-$(uname -m)"
    exit 0
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEKO="${TEKO:-./bin/teko}"
BUILDER="${BUILDER:-./build/teko}"
CHECK_MACHO="$script_dir/scripts/check_macho.sh"

resolve_abs() {
    case "$1" in
        /*) printf '%s' "$1" ;;
        *)  printf '%s/%s' "$script_dir" "${1#./}" ;;
    esac
}

teko_abs="$(resolve_abs "$TEKO")"
builder_abs="$(resolve_abs "$BUILDER")"

# Self-host ./bin/teko from the seed when it is missing (a fresh checkout / a CI job
# that only built ./build/teko). The RESULT carries emit_native because it is built
# from THIS branch's src/, so its TEKO_BACKEND=native seam is real.
if [[ ! -x "$teko_abs" ]]; then
    if [[ ! -x "$builder_abs" ]]; then
        if command -v teko >/dev/null 2>&1; then builder_abs="$(command -v teko)"; fi
    fi
    if [[ -x "$builder_abs" ]]; then
        echo "diff_c_own: $TEKO not found — self-hosting via $builder_abs build $script_dir -o bin"
        if ! "$builder_abs" build "$script_dir" -o "$script_dir/bin" >/tmp/teko-diffcown-selfhost.log 2>&1; then
            echo "diff_c_own: self-host build failed — see /tmp/teko-diffcown-selfhost.log" >&2
            tail -20 /tmp/teko-diffcown-selfhost.log | sed 's/^/      | /'
            exit 2
        fi
    fi
fi

if [[ ! -x "$teko_abs" ]]; then
    echo "diff_c_own: no self-hosted teko at '$TEKO' (build it: teko . -o bin) — cannot run the differential" >&2
    exit 2
fi

fixture_root="$script_dir/examples/regressions"
CORPUS=(
    own_exit_zero
    own_exit_code
    own_arith_exit
    own_sub_exit
    own_if_exit
    own_match_exit
)

# ── KNOWN-STOP list ────────────────────────────────────────────────────────────────────
# Fixtures the own backend cannot yet build end-to-end because they reach a NAMED,
# INHERITED honest-stop BEFORE encode (§6). For these the harness asserts the own build
# HONESTLY STOPS with the named error (never a fabricated artifact — M.3) while C-native
# succeeds. The day the own build starts SUCCEEDING (the stop was closed) the fixture turns
# XSTOP and the harness fails LOUDLY, so the list is pruned and the fixture joins the
# compared exit(n) corpus.
#
# own_match_exit — `match` lowering trips the regalloc back-edge over-approximation
#   (A3-loop, backend-a3-regalloc.md §6.2): #443's RPO numbering unblocked acyclic `if`/`match`
#   merges for allocation, but a `match` over a literal discriminant still presents a branch the
#   linear back-edge test misclassifies, so regalloc honest-stops. `if`/`else` (own_if_exit)
#   allocates fine, proving the multi-block path works; `match` joins the compared corpus when
#   A3-loop lands.
declare -a KNOWN_STOP=(
    own_match_exit
)
KNOWN_STOP_ERR="A3-loop"

is_known_stop() {
    local n="$1" x
    for x in "${KNOWN_STOP[@]}"; do [[ "$x" == "$n" ]] && return 0; done
    return 1
}

fixtures=()
if [[ "$#" -gt 0 ]]; then
    fixtures=("$@")
else
    for name in "${CORPUS[@]}"; do fixtures+=("$fixture_root/$name"); done
fi

work="$(mktemp -d "${TMPDIR:-/tmp}/teko-diffcown.XXXXXX")"
trap 'rm -rf "$work"' EXIT

pass=0 fail=0 kstop=0
failed_names=() xstop_names=()

echo "diff_c_own: teko=$teko_abs — ${#fixtures[@]} fixture(s) (macOS-arm64 C-vs-own differential)"
echo

for proj in "${fixtures[@]}"; do
    name="$(basename "$proj")"
    cout="$work/$name-c"
    oout="$work/$name-own"

    ( unset TEKO_BACKEND; "$teko_abs" build "$proj" -o "$cout" ) >"$work/$name.cbuild" 2>&1
    if [[ $? -ne 0 ]]; then
        fail=$((fail + 1)); failed_names+=("$name")
        printf 'FAIL  %-16s C-native build failed\n' "$name"
        tail -8 "$work/$name.cbuild" | sed 's/^/      | /'
        continue
    fi
    "$cout/$name" >"$work/$name.cout" 2>&1
    c_exit=$?

    TEKO_BACKEND=native "$teko_abs" build "$proj" -o "$oout" >"$work/$name.obuild" 2>&1
    own_build_rc=$?

    # ── KNOWN-STOP fixture: assert the own backend HONESTLY stops (named error) ─────
    if is_known_stop "$name"; then
        if [[ "$own_build_rc" -ne 0 ]] && grep -q "$KNOWN_STOP_ERR" "$work/$name.obuild"; then
            kstop=$((kstop + 1))
            printf 'KSTOP %-16s own backend honest-stops (%s), C-native exit=%s — joins the corpus when the stop lands\n' "$name" "$KNOWN_STOP_ERR" "$c_exit"
        else
            fail=$((fail + 1)); failed_names+=("$name"); xstop_names+=("$name")
            printf 'XSTOP %-16s KNOWN-STOP fixture no longer stops on %s (own build rc=%s) — prune KNOWN_STOP and add to the compared corpus\n' "$name" "$KNOWN_STOP_ERR" "$own_build_rc"
        fi
        continue
    fi

    if [[ "$own_build_rc" -ne 0 ]]; then
        fail=$((fail + 1)); failed_names+=("$name")
        printf 'FAIL  %-16s own-native build failed (TEKO_BACKEND=native)\n' "$name"
        tail -8 "$work/$name.obuild" | sed 's/^/      | /'
        continue
    fi
    "$oout/$name" >"$work/$name.oout" 2>&1
    own_exit=$?

    if [[ "$c_exit" -ne "$own_exit" ]]; then
        fail=$((fail + 1)); failed_names+=("$name")
        printf 'FAIL  %-16s exit codes differ: C-native=%s own-native=%s\n' "$name" "$c_exit" "$own_exit"
        continue
    fi
    if ! cmp -s "$work/$name.cout" "$work/$name.oout"; then
        fail=$((fail + 1)); failed_names+=("$name")
        printf 'FAIL  %-16s stdout differs (C-native vs own-native):\n' "$name"
        diff -u "$work/$name.cout" "$work/$name.oout" | sed 's/^/      | /' | head -20
        continue
    fi

    if ! "$CHECK_MACHO" "$oout/$name.o" >"$work/$name.macho" 2>&1; then
        fail=$((fail + 1)); failed_names+=("$name")
        printf 'FAIL  %-16s check_macho rejected the emitted %s.o\n' "$name" "$name"
        tail -5 "$work/$name.macho" | sed 's/^/      | /'
        continue
    fi

    pass=$((pass + 1))
    printf 'PASS  %-16s own-native exit == C-native exit == %s, .o linker-consumable\n' "$name" "$c_exit"
done

echo
echo "diff_c_own: $pass passed, $fail failed, $kstop known-stop (inherited, own backend honest-stops)"
[[ "$fail" -gt 0 ]] && echo "  FAILED: ${failed_names[*]}"
[[ "${#xstop_names[@]}" -gt 0 ]] && echo "  XSTOP (prune KNOWN_STOP — the inherited stop was closed): ${xstop_names[*]}"
[[ "$fail" -gt 0 ]] && exit 1
exit 0
