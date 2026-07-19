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
# For each own-native build the harness also runs a host-tool well-formedness check
# on the REAL emitted `.o`: scripts/check_macho.sh (otool/nm/ld -r) on the macOS-arm64
# Mach-O lane, scripts/check_elf.sh (readelf/objdump/nm/ld -r) on the linux-x86_64 ELF
# lane, scripts/check_coff.sh (llvm-readobj/llvm-objdump/lld-link) on the windows-x86_64
# PE/COFF lane — the writer's bytes cross-checked by the same host toolchain that
# consumes them.
#
# ── harness-blind-spot guards (review finding F1) ───────────────────────────────────
# A silently-misconfigured `TEKO` (e.g. the C bootstrap, or any binary predating
# emit_native) makes the `TEKO_BACKEND=native` env seam a pure NO-OP: the "own" build
# silently falls back to the C backend, so "own == C" trivially holds — a blind spot
# that would mask ANY own-backend regression. Two positive guards close it:
#   (a) the "own" build's captured output MUST contain emit_native's EXCLUSIVE success
#       marker, `"(own backend)"` (`project.tks::emit_native`, printed ONLY by that
#       path, never by the C `backend()` arm) — its absence is a FAIL, not a pass.
#   (b) the own-native `.o` MUST exist on disk before `check_macho.sh` runs — an
#       absent object here is a harness FAILURE (never a skip; `check_macho.sh`'s own
#       honest-skip is for STANDALONE invocation with no object at all, not for this
#       harness where the own-native build already reported success + the marker).
#
# FOUR EXECUTING LANES (issue #386 B1-8; issue #387 B2-8; issue #388 B3-4):
#   macOS-arm64  → own emits arm64 Mach-O; TEKO_BACKEND=native; check_macho.sh.
#   linux-x86_64 → own emits x86-64 ELF;   TEKO_BACKEND=native TEKO_TARGET=x86_64-linux;
#                  check_elf.sh. This lane RUNS the ELF (the linux runner executes it),
#                  the executing C-vs-own differential the macOS lane cannot host.
#   windows-x86_64 → own emits x86-64 PE/COFF; TEKO_BACKEND=native TEKO_TARGET=x86_64-windows;
#                  check_coff.sh. Detected by a Git-Bash/MSYS/Cygwin `uname` (MINGW*/MSYS*/
#                  CYGWIN*) with x86_64. This lane RUNS the PE NATIVELY (no emulator — the
#                  windows runner executes the artifact directly, the first own==C
#                  differential to run on its target OS without emulation), the host cc
#                  (clang → lld-link) links, check_coff.sh cross-checks the object.
#   riscv64      → own emits riscv64 ELF;  selected EXPLICITLY by TEKO_DIFF_TARGET=riscv64-linux
#                  (NOT uname — the CI host is linux-x86_64), cross-linked by
#                  riscv64-linux-gnu-gcc (TEKO_CC), both C-native and own-native binaries
#                  RUN under qemu-riscv64-static, check_elf.sh cross-checks with the riscv
#                  binutils (ELF_TOOLCHAIN/ELF_MACHINE). Honest-skips when the cross-gcc /
#                  qemu are absent (e.g. a macOS dev host, where the encoder + emit_elf_riscv
#                  goldens in `teko test .` are the byte cross-check instead).
# On any OTHER host the harness HONEST-SKIPS with a named reason (exit 0). wasm is a
# later cluster (C1).
#
# usage: scripts/diff_c_own.sh [fixture-dir ...]
#   TEKO=<self-hosted-teko>   (default: ./bin/teko — MUST carry emit_native; the raw
#                              seed does NOT, so the env seam would be a silent no-op
#                              there — never use the seed itself as TEKO)
#   BUILDER=<seed-teko>       (default: ./.teko/teko — the fetch_teko.sh-cached seed —
#                              then `teko` on PATH) — used ONLY to self-host ./bin/teko
#                              when it is missing

set -u

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# TK_RT_DIR pinned ABSOLUTE (project.tks::rt_dir reads it verbatim; assert_dir derives
# the sibling `<parent-of-rt>/assert` from it, so one absolute anchor resolves BOTH
# teko_rt.c and assert.c). Every fixture build below runs with CWD == the fixture
# project dir, not $script_dir, so project.tks's compiler-relative probe
# (set_rt_dir_from_compiler) is the only other resolution path — and it fails on the
# Windows runner (a general out-of-tree-build gap, reported separately, not fixed
# here), falling back to a CWD-relative "src/runtime" that never resolves from a
# fixture dir. Exporting an absolute TK_RT_DIR here sidesteps both platform probes
# uniformly, so every lane (macOS/Linux/Windows) finds the runtime the same way.
export TK_RT_DIR="$script_dir/src/runtime"

host_os="$(uname -s)"
host_arch="$(uname -m)"
diff_target="${TEKO_DIFF_TARGET:-}"

# RUN_WRAP wraps BOTH the C-native and own-native executions (empty = run natively;
# `qemu-riscv64-static` for the cross riscv lane). TEKO_CC_ENV forces the compiler
# `build_cc_argv` invokes (empty = host cc; a cross-gcc token for a cross lane).
# OBJ_CHECK_ENV prefixes the OBJ_CHECK invocation (empty = host binutils; the
# riscv toolchain prefix + machine for check_elf). All three interpolate UNQUOTED,
# matching the existing `$OWN_TARGET_ENV` env-string convention (no embedded spaces).
RUN_WRAP=""
TEKO_CC_ENV=""
OBJ_CHECK_ENV=""

if [[ "$diff_target" == "riscv64-linux" ]]; then
    # The EXPLICITLY-selected riscv64 lane (issue #387 B2-8). The CI host is
    # linux-x86_64 (uname reports x86_64, NOT riscv), so the target is chosen by
    # TEKO_DIFF_TARGET, never uname. Both binaries are riscv64 ELF, cross-linked by
    # riscv64-linux-gnu-gcc and RUN under qemu-riscv64-static. Requires the riscv
    # cross-gcc + qemu; absent (e.g. a macOS dev host), honest-skip — the macOS byte
    # cross-check is the encoder + emit_elf_riscv goldens in `teko test .`.
    if ! command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
        echo "diff_c_own: skipped — TEKO_DIFF_TARGET=riscv64-linux needs riscv64-linux-gnu-gcc (the cross-linker); not found on this host"
        exit 0
    fi
    if ! command -v qemu-riscv64-static >/dev/null 2>&1; then
        echo "diff_c_own: skipped — TEKO_DIFF_TARGET=riscv64-linux needs qemu-riscv64-static (the executing emulator); not found on this host"
        exit 0
    fi
    FLAVOR="linux-riscv64 ELF (qemu)"
    OWN_TARGET_ENV="TEKO_TARGET=riscv64-linux"
    TEKO_CC_ENV="TEKO_CC=riscv64-linux-gnu-gcc"
    RUN_WRAP="qemu-riscv64-static"
    OBJ_CHECK="$script_dir/scripts/check_elf.sh"
    OBJ_CHECK_NAME="check_elf"
    OBJ_CHECK_ENV="ELF_TOOLCHAIN=riscv64-linux-gnu- ELF_MACHINE=RISC-V"
elif [[ "$host_os" == "Darwin" && "$host_arch" == "arm64" ]]; then
    FLAVOR="macOS-arm64 Mach-O"
    OWN_TARGET_ENV=""
    OBJ_CHECK="$script_dir/scripts/check_macho.sh"
    OBJ_CHECK_NAME="check_macho"
elif [[ "$host_os" == "Linux" && "$host_arch" == "x86_64" ]]; then
    FLAVOR="linux-x86_64 ELF"
    OWN_TARGET_ENV="TEKO_TARGET=x86_64-linux"
    OBJ_CHECK="$script_dir/scripts/check_elf.sh"
    OBJ_CHECK_NAME="check_elf"
elif [[ ( "$host_os" == MINGW* || "$host_os" == MSYS* || "$host_os" == CYGWIN* ) && "$host_arch" == "x86_64" ]]; then
    # The windows-x86_64 lane (issue #388 B3-4). A Git-Bash/MSYS/Cygwin host on the
    # GitHub windows-x86_64 runner: own emits an IMAGE_FILE_MACHINE_AMD64 PE/COFF
    # object (the SAME x86 ISA as the linux lane, under the win64 register file +
    # emit_coff), linked NATIVELY (host == target, TEKO_CC=cc — overriding
    # default_cc_for_target's x86_64-w64-mingw32-gcc, which is the CROSS-cc for a
    # non-Windows dev host; "cc" is the SAME literal command the pre-existing
    # windows-x86_64 build-test job already resolves to a working clang link) into a
    # `.exe` and RUN NATIVELY — the PE executes directly, no emulator (the key
    # advantage over the cross riscv lane's qemu). RUN_WRAP stays empty (native
    # execution); the MSYS exec layer auto-appends `.exe`, so the bare
    # `$cout/$name` / `$oout/$name` invocations run the produced executable.
    # check_coff.sh cross-checks the object with the LLVM tools the runner ships.
    FLAVOR="windows-x86_64 PE/COFF"
    OWN_TARGET_ENV="TEKO_TARGET=x86_64-windows"
    TEKO_CC_ENV="TEKO_CC=cc"
    OBJ_CHECK="$script_dir/scripts/check_coff.sh"
    OBJ_CHECK_NAME="check_coff"
else
    echo "diff_c_own: skipped — the own backend runs its executing differential on macOS-arm64 (Mach-O), linux-x86_64 (ELF) or windows-x86_64 (PE/COFF); host is $host_os-$host_arch"
    exit 0
fi

TEKO="${TEKO:-./bin/teko}"
BUILDER="${BUILDER:-./.teko/teko}"
OWN_BACKEND_MARKER="(own backend)"

resolve_abs() {
    case "$1" in
        /*) printf '%s' "$1" ;;
        *)  printf '%s/%s' "$script_dir" "${1#./}" ;;
    esac
}

teko_abs="$(resolve_abs "$TEKO")"
builder_abs="$(resolve_abs "$BUILDER")"

# Self-host ./bin/teko from the seed when it is missing (a fresh checkout / a dev
# host that only fetched the seed, not yet self-hosted). The RESULT carries
# emit_native because it is built from THIS branch's src/, so its
# TEKO_BACKEND=native seam is real.
if [[ ! -x "$teko_abs" ]]; then
    if [[ ! -x "$builder_abs" ]]; then
        if command -v teko >/dev/null 2>&1; then builder_abs="$(command -v teko)"; fi
    fi
    if [[ -x "$builder_abs" ]]; then
        echo "diff_c_own: $TEKO not found — self-hosting via $builder_abs build $script_dir -o bin"
        if ! "$builder_abs" build "$script_dir" -o "$script_dir/bin" --no-verify >/tmp/teko-diffcown-selfhost.log 2>&1; then
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
    own_logical_not
    own_print_exit
    own_loop_range_native
    own_loop_nested_range
    own_defer_arm_write_propagates
    own_value_if_rhs_reassign
    own_match_arm_reassign_vs_shadow
    own_if_value_rhs_shadow_then_outer_reassign
    own_match_reassign_then_shadow
)

# ── KNOWN-STOP list ────────────────────────────────────────────────────────────────────
# Fixtures the own backend cannot yet build end-to-end because they reach a NAMED,
# reproducible stop (§6, or a linker-surfaced gap below). For these the harness asserts
# the own build FAILS with the EXPECTED signature (parallel `KNOWN_STOP_ERR` entry) while
# C-native succeeds — never a fabricated artifact (M.3). The day the own build starts
# SUCCEEDING (the stop was closed) the fixture turns XSTOP and the harness fails LOUDLY,
# so the list is pruned and the fixture joins the compared exit(n) corpus.
#
# own_match_exit — PROMOTED to the compared corpus (A4-6): the false back-edge came from
#   the irrefutable-`_` dead fallback block (unreachable source counted as a retreat);
#   `has_back_edge` is now reachability-aware, `match` allocates and runs (own == C == 42).
#
# own_print_exit — the ORIGINAL stop (`teko::io::println` mangled to the wrong external
#   symbol, a linker-surfaced undefined-`println` failure) is CLOSED (#389 C1-6):
#   `call_symbol` (`src/lir/lower.tks`) now maps the io group (`print`/`println`/
#   `eprint`/`eprintln`/`write`/`ewrite`) to their `tk_*` runtime twins, mirroring
#   codegen.tks's own bare-name builtin dispatch (`codegen.tks:2289-2297`), and lowers
#   their lone `str` argument through its (ptr, len) FAT pair (`lower_fat_expr`) so the
#   two-eightbyte `tk_str`-by-value AAPCS64/SysV ABI is reproduced exactly (a lone
#   struct-by-value argument decomposes to two consecutive registers — no isel/regalloc
#   change needed). This fixture's OWN message is a STRING INTERPOLATION
#   (`$"answer={6*7}!"`), and `lower_fat_expr`'s CLOSED fat-pointer-producer set does
#   NOT (yet) include a call result — `tk_str_concat`/`tk_i64_to_str`'s OWN return-value
#   ABI (a `tk_str` returned in two registers, X0:X1/RAX:RDX) is a SEPARATE, wider,
#   PRE-EXISTING gap (A1-4, #382 — `lower_interp`'s own doc: "closes the LOWERING
#   honest-stop only") that `lower_fat_expr` already names explicitly ("a call result …
#   honest-stops … a later phase, N2") — REPORTED up, not this crumb's scope (fixing it
#   needs a wider LCall multi-register-return threading, well beyond a builtin-dispatch
#   fix). So the fixture NOW compiler-honest-stops one step later, with a DIFFERENT,
#   NAMED signature: `lower_fat_expr`'s own "fat-pointer receiver `string interpolation`
#   not yet lowered (N2)" — a COMPILER diagnostic, not a linker failure. The
#   KNOWN_STOP_ERR substring below matches THIS new signature.
#
declare -a KNOWN_STOP=(
    own_print_exit
)
declare -a KNOWN_STOP_ERR=(
    "fat-pointer receiver"
)

known_stop_index_of() {
    local n="$1" i
    for i in "${!KNOWN_STOP[@]}"; do
        if [[ "${KNOWN_STOP[$i]}" == "$n" ]]; then printf '%s' "$i"; return 0; fi
    done
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

echo "diff_c_own: teko=$teko_abs — ${#fixtures[@]} fixture(s) ($FLAVOR C-vs-own differential)"
echo

# check_known_stop — the KNOWN-STOP verdict for `name`: KSTOP when the own build FAILED
# with its expected signature (`$work/$name.obuild` carries the parallel `KNOWN_STOP_ERR`
# substring); XSTOP-as-FAIL otherwise (a silent-no-op seam OR a genuinely closed stop —
# either way, the harness must fail loudly rather than pass silently, F1).
check_known_stop() {
    local name="$1" idx err
    idx="$(known_stop_index_of "$name")"
    err="${KNOWN_STOP_ERR[$idx]}"
    if [[ "$own_build_rc" -ne 0 ]] && grep -qF "$err" "$work/$name.obuild"; then
        kstop=$((kstop + 1))
        printf 'KSTOP %-16s own backend honest-stops (%s), C-native exit=%s — joins the corpus when the stop lands\n' "$name" "$err" "$c_exit"
        return
    fi
    fail=$((fail + 1)); failed_names+=("$name"); xstop_names+=("$name")
    printf 'XSTOP %-16s KNOWN-STOP fixture no longer stops on %s (own build rc=%s) — prune KNOWN_STOP and add to the compared corpus\n' "$name" "$err" "$own_build_rc"
}

for proj in "${fixtures[@]}"; do
    name="$(basename "$proj")"
    cout="$work/$name-c"
    oout="$work/$name-own"

    ( unset TEKO_BACKEND; env $TEKO_CC_ENV "$teko_abs" build "$proj" -o "$cout" --no-verify ) >"$work/$name.cbuild" 2>&1
    if [[ $? -ne 0 ]]; then
        fail=$((fail + 1)); failed_names+=("$name")
        printf 'FAIL  %-16s C-native build failed\n' "$name"
        tail -8 "$work/$name.cbuild" | sed 's/^/      | /'
        continue
    fi
    $RUN_WRAP "$cout/$name" >"$work/$name.cout" 2>&1
    c_exit=$?

    env TEKO_BACKEND=native $OWN_TARGET_ENV $TEKO_CC_ENV "$teko_abs" build "$proj" -o "$oout" --no-verify >"$work/$name.obuild" 2>&1
    own_build_rc=$?

    if known_stop_index_of "$name" >/dev/null; then
        check_known_stop "$name"
        continue
    fi

    if [[ "$own_build_rc" -ne 0 ]]; then
        fail=$((fail + 1)); failed_names+=("$name")
        printf 'FAIL  %-16s own-native build failed (TEKO_BACKEND=native)\n' "$name"
        tail -8 "$work/$name.obuild" | sed 's/^/      | /'
        continue
    fi

    # F1(a) — positively assert the OWN backend actually ran (never trust a bare exit 0).
    if ! grep -qF "$OWN_BACKEND_MARKER" "$work/$name.obuild"; then
        fail=$((fail + 1)); failed_names+=("$name")
        printf 'FAIL  %-16s own-native build did NOT report "%s" — TEKO_BACKEND=native was a silent no-op (wrong/stale teko binary?), NOT a real own-backend build\n' "$name" "$OWN_BACKEND_MARKER"
        tail -8 "$work/$name.obuild" | sed 's/^/      | /'
        continue
    fi

    # F1(b) — the own-native `.o` MUST exist; its absence here is a FAIL, never a skip.
    objp="$oout/$name.o"
    if [[ ! -f "$objp" ]]; then
        fail=$((fail + 1)); failed_names+=("$name")
        printf 'FAIL  %-16s own-native build reported success but no %s.o was written\n' "$name" "$name"
        continue
    fi

    $RUN_WRAP "$oout/$name" >"$work/$name.oout" 2>&1
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

    if ! env $OBJ_CHECK_ENV "$OBJ_CHECK" "$objp" >"$work/$name.objcheck" 2>&1; then
        fail=$((fail + 1)); failed_names+=("$name")
        printf 'FAIL  %-16s %s rejected the emitted %s.o\n' "$name" "$OBJ_CHECK_NAME" "$name"
        tail -5 "$work/$name.objcheck" | sed 's/^/      | /'
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
