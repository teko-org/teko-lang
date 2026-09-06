#!/usr/bin/env bash
# scripts/target_host_default_test.sh — proves the R1 host-target default (owner
# ruling 2026-07-24, teko-target-crosslink-0.3.1.md §0/§2.1): a build with
# TEKO_BACKEND=native and TEKO_TARGET UNSET must emit an object in THIS HOST's own
# format. The bug this replaces (teko-target-crosslink §1): `native_target()` used
# to fall back to a hardcoded `Arm64Macho` whenever `TEKO_TARGET` was absent,
# regardless of the real host — a linux x86_64 host silently emitted an arm64
# Mach-O object that `cc`/`ld` then rejected with an opaque cross-format error.
#
# regressor.tkr's F5 backend Examples ALWAYS pin TEKO_TARGET explicitly for their
# differential rows (so a host-arch mismatch can never mask a regression there); this script
# deliberately LEAVES TEKO_TARGET unset, so it is the one harness that actually
# exercises native_target()'s R1 default path end-to-end: build, well-formedness
# check via the host-appropriate scripts/check_{elf,macho,coff}.sh, and run.
#
# The fixture is a trivial `exit(42)` scratch project WRITTEN BY THIS SCRIPT (0.3.1
# regressor-principal wave — a fixed directory name under examples/regressions/ is exactly
# the fragile-orphan-reference shape that wave closed; the own AOT backend also needs the
# SIMPLEST possible body here, since it honest-stops on builtins like is_alpha/str_slice_chars
# that a richer examples/regressions/ fixture would pull in).
#
# usage: scripts/target_host_default_test.sh
#   TEKO=<self-hosted-teko>   (default: ./bin/teko — MUST carry emit_native)

set -u

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEKO="${TEKO:-$script_dir/bin/teko}"
name="target_host_default"

if [[ ! -x "$TEKO" ]]; then
    echo "target_host_default_test: no self-hosted teko at '$TEKO' (build it: teko . -o bin) — cannot run" >&2
    exit 2
fi

host_os="$(uname -s)"
host_arch="$(uname -m)"

work="$(mktemp -d "${TMPDIR:-/tmp}/teko-target-default.XXXXXX")"
trap 'rm -rf "$work"' EXIT

fixture="$work/$name"
mkdir -p "$fixture/src"
cat >"$fixture/$name.tkp" <<EOF
name = "$name"
source = "src"

[artifact]
kind = "binary"
EOF
printf 'exit(42)\n' >"$fixture/src/main.tks"

out="$work/out"
( unset TEKO_TARGET; env TEKO_BACKEND=native "$TEKO" build "$fixture" -o "$out" --no-verify --release ) >"$work/build.log" 2>&1
build_rc=$?

if [[ "$build_rc" -ne 0 ]]; then
    echo "target_host_default_test: FAIL — build with TEKO_BACKEND=native and TEKO_TARGET unset failed on $host_os-$host_arch"
    tail -20 "$work/build.log" | sed 's/^/      | /'
    exit 1
fi

if ! grep -qF "(own backend)" "$work/build.log"; then
    echo "target_host_default_test: FAIL — TEKO_BACKEND=native did not report the own-backend marker (silent no-op — wrong/stale teko binary?)"
    exit 1
fi

objp="$out/$name.o"
if [[ ! -f "$objp" ]]; then
    echo "target_host_default_test: FAIL — build reported success but no $name.o was written"
    exit 1
fi

# ROUTING IS THIS SCRIPT'S JOB, NOT THE CHECKER'S. Each checker now fails closed on a host it
# cannot serve, so every host we schedule has to be routed HERE. `Linux` now covers aarch64
# too: check_elf.sh derives the expected e_machine from the os-arch it is HANDED, falling back
# to the host when handed none, and refusing aarch64 was how an arm64 Linux leg would report
# PASS having validated no object at all.
#
# THIS SCRIPT DELIBERATELY DECLARES NO EXPECTED OS-ARCH TO THE CHECKER. Its whole subject is
# the R1 host DEFAULT: `TEKO_TARGET` is unset, so what the object must carry is precisely "this
# host's own architecture", which is exactly check_elf.sh's no-argument fallback. Naming an
# os-arch here would assert the host's arch against itself and make the check tautological.
#
# The fall-through is a HARD FAILURE, not a note. "The build itself already proved it" is not
# the same claim as "the bytes are well-formed" — the very bug this script exists to catch (a
# host silently emitting another host's object format) is invisible to a successful build, and
# a row that quietly downgrades its own assertion is the "erro escondido que não dispara" the
# trunk bar forbids. A new host in the matrix arrives with its checker, or it makes noise.
if [[ "$host_os" == "Linux" ]]; then
    "$script_dir/scripts/check_elf.sh" "$objp" || exit 1
elif [[ "$host_os" == "Darwin" && "$host_arch" == "arm64" ]]; then
    "$script_dir/scripts/check_macho.sh" "$objp" || exit 1
elif [[ "$host_os" == MINGW* || "$host_os" == MSYS* || "$host_os" == CYGWIN* ]]; then
    "$script_dir/scripts/check_coff.sh" "$objp" || exit 1
else
    echo "target_host_default_test: FAIL — no well-formedness checker routed for $host_os-$host_arch; a scheduled host without a checker holds an UNVALIDATED object, which is not a pass" >&2
    exit 1
fi

"$out/$name" >"$work/run.log" 2>&1
run_rc=$?
if [[ "$run_rc" -ne 42 ]]; then
    echo "target_host_default_test: FAIL — the host-default own-native binary exited $run_rc (want 42)"
    cat "$work/run.log" | sed 's/^/      | /'
    exit 1
fi

echo "target_host_default_test: PASS — TEKO_BACKEND=native with TEKO_TARGET unset emits the correct $host_os-$host_arch object format and runs (exit 42)"
exit 0
