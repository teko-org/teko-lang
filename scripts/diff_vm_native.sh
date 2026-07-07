#!/usr/bin/env bash
# scripts/diff_vm_native.sh — differential VM==native harness (issue #54).
#
# Every confirmed VM/native divergence in the chore/reboot audit existed because nothing
# cross-runs the same program through both engines, and because examples/regressions/*
# were never fed to a real `cc`. This harness closes that structural blind spot:
#
#   For every project under examples/regressions/ (any directory containing a *.tkp),
#     1. run it on the VM        : teko run   <proj>
#     2. build it natively       : teko build <proj> -o <tmp>   (front-end -> C -> host cc)
#        and execute the binary
#   and assert IDENTICAL exit codes and identical program stdout.
#
# `teko run` prints compiler diagnostics on stdout ("teko: ..." + the file-mapping lines);
# those are stripped before comparing so only PROGRAM output is diffed.
#
# EXPECTED_FAIL: fixtures that document a KNOWN engine divergence. They must KEEP
# diverging — the day both engines agree (someone closed the gap, or native regressed
# into the VM's behavior) the fixture turns XPASS and the harness fails LOUDLY so the
# list gets pruned and the fixture starts gating normally.
#
# In addition to the per-fixture VM==native comparison above, the harness runs one
# CWD-REGRESSION check (issue #64): `cd <project-dir> && teko build .` — the exact
# invocation shape a real user takes — for BOTH the C-bootstrap (./build/teko, whose
# TK_RT_DIR/TK_SRC_DIR are baked in absolute by CMake and were never affected) and the
# self-hosted engine (./bin/teko, built on the fly from the bootstrap if missing, whose
# src/build/project.tks::ensure_rt_dir_abs used to resolve the runtime relative to the
# CWD instead of the compiler binary's own location — PR #66). Both must exit 0 and
# produce a working (executable, non-crashing) binary; see check_cwd_build_regression().
#
# usage: scripts/diff_vm_native.sh [fixture-dir ...]
#   TEKO=<path-to-teko-binary>   (default: ./build/teko)
#   FIXTURE_ROOT=<dir>           (default: examples/regressions)
#   CWD_REGRESSION_FIXTURE=<dir> (default: examples/regressions/char_ops)

set -u

TEKO="${TEKO:-./build/teko}"
FIXTURE_ROOT="${FIXTURE_ROOT:-examples/regressions}"
CWD_REGRESSION_FIXTURE="${CWD_REGRESSION_FIXTURE:-examples/regressions/char_ops}"

# ── EXPECTED-FAIL list ─────────────────────────────────────────────────────────────────
# class_destruct_effects — the VM fires NO class destructors: W10b.CLASS increment 4's
#   `destruct` injection lives ONLY in native codegen (src/codegen/codegen.c,
#   cg_find_destruct_ns / the exit-edge emission); src/vm has zero destructor logic.
#   Known divergence (TEKO_MASTER_PLAN.md W10b.CLASS increment 4 / issue #54 audit).
#   Native exits 7 from inside Tracker::destruct, the VM falls off the end with 0. When
#   the VM gains destructors (or native stops firing them) this becomes XPASS and MUST
#   be removed here.
EXPECTED_FAIL=(
    class_destruct_effects
)

# ── NATIVE-ONLY list ───────────────────────────────────────────────────────────────────
# Fixtures the VM refuses BY DESIGN (not a divergence bug): `extern` FFI cannot run on the
# VM (C7.1a — "use `teko build` to compile it natively"). These are built through the host
# cc and EXECUTED, asserting only that the binary neither fails to build nor crashes
# (exit < 126 rules out panic/abort/signal deaths); no VM comparison is possible.
# time_types — calls the extern host-clock FFI (teko::time).
# extern_reachability — C7.20: two declared [extern.libs], only one called (extern FFI).
# buf_ptr_memset_roundtrip — calls a raw `extern` (libc memset) over a `teko::mem::buf_ptr`
#   arena buffer (C7.19); `buf_ptr` is a host-only builtin with no VM interception, same
#   honest-stop shape as any other raw extern.
# io_file_copy — IO1 (#184): file -> store -> file -> unstore -> file over the ReadFn/WriteFn
#   copy seam; calls the read_file / write_file_bytes externs, which the VM refuses by design.
# crypto_rand_secure_bytes — calls teko::crypto::rand::secure_bytes, a `from "teko_rt"`
#   extern (#194 C6); the VM rejects EVERY extern (not just raw platform ones — C7.1a), so
#   this CSPRNG primitive is native-only, same shape as time_types.
# unsafe_rawbuf_roundtrip — U3 (#334): an `unsafe fn` allocs a `RawBuf` (raw `ptr<byte>` +
#   len) and a raw `extern` (libc memset, NOT `from "teko_rt"`) writes through its raw field;
#   same host-only `buf_ptr` builtin the VM refuses, same honest-stop shape as
#   buf_ptr_memset_roundtrip.
NATIVE_ONLY=(
    time_types
    extern_reachability
    buf_ptr_memset_roundtrip
    io_file_copy
    crypto_rand_secure_bytes
    unsafe_rawbuf_roundtrip
)

# ── COMPILE-FAIL list ──────────────────────────────────────────────────────────────────
# NEGATIVE fixtures — a project the checker must REJECT (a compile error), on BOTH
# engines, rather than run-and-compare exit codes/stdout (there is no successful run to
# compare). Asserts `teko run` AND `teko build` both exit non-zero, proving the two
# engines reject the SAME project identically.
# must_free_leak — S2 (#336): a `#must_free` handle dropped without being freed on some
#   path is a compile-time error (the local consume-or-fail dataflow), not a runtime one.
# unsafe_field_in_safe_struct — U3 (#334) AC#4: a SAFE struct naming an unsafe-typed field
#   (without the `unsafe` modifier itself) is a compile-time error (U2's #333 field-contagion
#   gate), not a runtime one.
# adopt_return_type_mismatch / adopt_break_outside_loop / adopt_break_unknown_label /
#   adopt_unused_local — #337: `adopt { }` allows return/break/continue (unlike `defer`), so
#   check_returns/check_labels/check_locals must still validate its body — these four fixtures
#   each trip one of those checks from inside an adopt block.
COMPILE_FAIL=(
    must_free_leak
    unsafe_field_in_safe_struct
    adopt_return_type_mismatch
    adopt_break_outside_loop
    adopt_break_unknown_label
    adopt_unused_local
)

is_expected_fail() {
    local name="$1" x
    for x in "${EXPECTED_FAIL[@]}"; do [ "$x" = "$name" ] && return 0; done
    return 1
}

is_native_only() {
    local name="$1" x
    for x in "${NATIVE_ONLY[@]}"; do [ "$x" = "$name" ] && return 0; done
    return 1
}

is_compile_fail() {
    local name="$1" x
    for x in "${COMPILE_FAIL[@]}"; do [ "$x" = "$name" ] && return 0; done
    return 1
}

# Strip the compiler's own stdout diagnostics from a `teko run` capture, leaving only
# program output: "teko: ..." status lines and the indented "<file>\t-> <ns>" mapping lines.
filter_compiler_diag() {
    sed -e '/^teko: /d' -e $'/^  [^\t]*\t-> /d' "$1"
}

# Run a command with a timeout when `timeout` exists (Linux CI); raw otherwise (macOS dev).
run_limited() {
    if command -v timeout >/dev/null 2>&1; then
        timeout 120 "$@"
    else
        "$@"
    fi
}

# check_cwd_build_regression — issue #64: `cd <project> && teko build .` (the exact shape
# a real user runs from inside their own project) must exit 0 and produce a working binary,
# on BOTH engines. Takes the compiler binary's ABSOLUTE path and a short label ("bootstrap"
# / "self-hosted") for PASS/FAIL reporting; appends to the shared pass/fail counters.
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
    echo "diff_vm_native: teko binary not found/executable at '$TEKO' (set TEKO=...)" >&2
    exit 2
fi

# Fixture set: explicit args, else every directory under FIXTURE_ROOT with a *.tkp.
fixtures=()
if [ "$#" -gt 0 ]; then
    fixtures=("$@")
else
    while IFS= read -r tkp; do
        fixtures+=("$(dirname "$tkp")")
    done < <(find "$FIXTURE_ROOT" -mindepth 2 -maxdepth 2 -name '*.tkp' | sort)
fi
if [ "${#fixtures[@]}" -eq 0 ]; then
    echo "diff_vm_native: no fixtures found under '$FIXTURE_ROOT'" >&2
    exit 2
fi

work="$(mktemp -d "${TMPDIR:-/tmp}/teko-diff.XXXXXX")"
trap 'rm -rf "$work"' EXIT

pass=0 fail=0 xfail=0 xpass=0
failed_names=() xpass_names=()

echo "diff_vm_native: teko=$TEKO — ${#fixtures[@]} fixture(s)"
echo

for proj in "${fixtures[@]}"; do
    name="$(basename "$proj")"
    out="$work/$name"
    mkdir -p "$out"

    # ── NEGATIVE fixture: both engines must REJECT it (a compile error), not run ───
    if is_compile_fail "$name"; then
        run_limited "$TEKO" run "$proj" >"$out/vm.stdout" 2>"$out/vm.stderr"
        vm_rc=$?
        run_limited "$TEKO" build "$proj" -o "$out/bin" >"$out/build.stdout" 2>"$out/build.stderr"
        native_rc=$?
        if [ "$vm_rc" -ne 0 ] && [ "$native_rc" -ne 0 ]; then
            pass=$((pass + 1))
            printf 'PASS  %-28s both engines reject (compile-fail) vm=%s native=%s\n' "$name" "$vm_rc" "$native_rc"
        else
            fail=$((fail + 1)); failed_names+=("$name")
            printf 'FAIL  %-28s expected BOTH engines to reject; vm=%s native=%s\n' "$name" "$vm_rc" "$native_rc"
            [ "$vm_rc" -eq 0 ] && tail -5 "$out/vm.stdout" | sed 's/^/      | vm: /'
            [ "$native_rc" -eq 0 ] && tail -5 "$out/build.stdout" | sed 's/^/      | native: /'
        fi
        continue
    fi

    # ── engine 1: the VM (skipped for native-only fixtures) ────────────────────────
    if ! is_native_only "$name"; then
        run_limited "$TEKO" run "$proj" >"$out/vm.stdout" 2>"$out/vm.stderr"
        vm_exit=$?
        filter_compiler_diag "$out/vm.stdout" >"$out/vm.prog"
    fi

    # ── engine 2: native (front-end -> C -> host cc -> execute) ────────────────────
    if ! run_limited "$TEKO" build "$proj" -o "$out/bin" >"$out/build.stdout" 2>"$out/build.stderr"; then
        detail="native build failed (teko build/cc):"
        if is_expected_fail "$name"; then
            xfail=$((xfail + 1))
            printf 'XFAIL %-28s %s\n' "$name" "$detail known divergence, still diverging"
        else
            fail=$((fail + 1)); failed_names+=("$name")
            printf 'FAIL  %-28s %s\n' "$name" "$detail"
            sed 's/^/      | /' "$out/build.stderr" | tail -20
            tail -5 "$out/build.stdout" | sed 's/^/      | /'
        fi
        continue
    fi
    run_limited "$out/bin/$name" >"$out/native.stdout" 2>"$out/native.stderr"
    native_exit=$?

    # ── native-only: no VM to compare against; assert the binary did not crash ─────
    if is_native_only "$name"; then
        if [ "$native_exit" -lt 126 ]; then
            pass=$((pass + 1))
            printf 'PASS  %-28s native-only (extern FFI, no VM run) exit=%s\n' "$name" "$native_exit"
        else
            fail=$((fail + 1)); failed_names+=("$name")
            printf 'FAIL  %-28s native-only binary crashed (exit=%s)\n' "$name" "$native_exit"
            [ -s "$out/native.stderr" ] && tail -5 "$out/native.stderr" | sed 's/^/      | /'
        fi
        continue
    fi

    # ── compare ─────────────────────────────────────────────────────────────────────
    verdict=ok detail=""
    if [ "$vm_exit" -ne "$native_exit" ]; then
        verdict=diverge detail="exit codes differ: vm=$vm_exit native=$native_exit"
    elif ! cmp -s "$out/vm.prog" "$out/native.stdout"; then
        verdict=diverge detail="stdout differs (vm vs native):"
    fi

    if [ "$verdict" = ok ]; then
        if is_expected_fail "$name"; then
            xpass=$((xpass + 1)); xpass_names+=("$name")
            printf 'XPASS %-28s engines AGREE (exit %s) but fixture is on EXPECTED_FAIL — gap closed or native regressed; prune the list\n' "$name" "$vm_exit"
        else
            pass=$((pass + 1))
            printf 'PASS  %-28s exit=%s\n' "$name" "$vm_exit"
        fi
    else
        if is_expected_fail "$name"; then
            xfail=$((xfail + 1))
            printf 'XFAIL %-28s known divergence, still diverging (%s)\n' "$name" "$detail"
        else
            fail=$((fail + 1)); failed_names+=("$name")
            printf 'FAIL  %-28s %s\n' "$name" "$detail"
            if [ "$vm_exit" -eq "$native_exit" ]; then
                diff -u "$out/vm.prog" "$out/native.stdout" | sed 's/^/      | /' | head -30
            fi
            [ -s "$out/vm.stderr" ] && tail -3 "$out/vm.stderr" | sed 's/^/      | vm: /'
        fi
    fi
done

# ── CWD-regression check (issue #64) ────────────────────────────────────────────────────
# `cd <project> && teko build .` must work on BOTH engines: the C-bootstrap (whose
# TK_RT_DIR/TK_SRC_DIR are baked in absolute by CMake) and the self-hosted engine (which
# resolves its own runtime at runtime — PR #66 fixed it to use the compiler binary's own
# location instead of the CWD). Absolute paths throughout: the check itself `cd`s.
echo
echo "diff_vm_native: CWD-regression check (issue #64) — cd \"$CWD_REGRESSION_FIXTURE\" && teko build ."

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bootstrap_abs="$script_dir/${TEKO#./}"
selfhosted_abs="$script_dir/bin/teko"

if [ -x "$bootstrap_abs" ]; then
    check_cwd_build_regression "$bootstrap_abs" "bootstrap"
else
    fail=$((fail + 1)); failed_names+=("cwd_build_bootstrap")
    printf 'FAIL  %-28s bootstrap compiler not found/executable at %s\n' "cwd_build_bootstrap" "$bootstrap_abs"
fi

# Self-host on the fly if ./bin/teko is not already there (a fresh checkout / a CI job
# that only built ./build/teko, which is the norm — this is what makes the check work
# with ZERO workflow changes: the harness builds what it needs to test).
if [ ! -x "$selfhosted_abs" ]; then
    if [ -x "$bootstrap_abs" ]; then
        echo "diff_vm_native: bin/teko not found — self-hosting via $bootstrap_abs build . -o bin"
        if ! run_limited "$bootstrap_abs" build "$script_dir" -o "$script_dir/bin" >/tmp/teko-selfhost-build.log 2>&1; then
            echo "diff_vm_native: self-host build failed — see /tmp/teko-selfhost-build.log" >&2
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
echo "diff_vm_native: $pass passed, $fail failed, $xfail expected-fail (still diverging), $xpass unexpectedly passing"
[ "$fail" -gt 0 ] && echo "  FAILED: ${failed_names[*]}"
[ "$xpass" -gt 0 ] && echo "  XPASS (prune EXPECTED_FAIL or investigate a native regression): ${xpass_names[*]}"

if [ "$fail" -gt 0 ] || [ "$xpass" -gt 0 ]; then exit 1; fi
exit 0
