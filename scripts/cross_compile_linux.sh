#!/usr/bin/env sh
# scripts/cross_compile_linux.sh — cross-compile the emitted teko.c into the six Linux
# artifacts with `zig cc`: glibc (dynamic, pinned to an old glibc for wide compat) and musl
# (fully static) for x86_64, arm64 and riscv64. ONE emitted teko.c → six targets, no qemu.
#
# A successful cross-COMPILE is NOT by itself a correctness argument: `zig cc` differs from the
# native clang/gcc that built every release <= v0.0.1.21 in two ways that the emitted teko.c's
# latent undefined behaviour makes fatal, and BOTH must be neutralised here:
#   1. `-O2` (release optimization — the seed-speed win, owner ruling). HISTORY: zig USED to exploit
#      residual UB into a MISCOMPILE at any optimizing level (the checker mis-resolved a call and
#      rejected the compiler's own valid source, x86_64-only). #283 removed ONE such UB (the
#      uninitialized match-as-value result temp, now `= {0}`); the SECOND x86_64 UB (a false
#      `argument type mismatch` at src/checker/match.tks:228 under `-O1`/`-O2`) was closed by #577's
#      `__attribute__((noinline))` on self-recursive functions (same LLVM inline+TRE class). VERIFIED
#      2026-07-15: zig `-O0`/`-O1`/`-O2` all self-host the corpus to exit 0, byte-identical emitted
#      teko.c, zero false errors — so Linux now ships at `-O2`. The x86_64 smoke (below) RUNS the
#      artifact, so any future miscompile re-reddens loudly instead of shipping.
#   2. `-fno-sanitize=undefined` — zig cc enables UBSan TRAPS by default (native cc does not), so
#      the one boundary INT64_MIN-negation in the checker's int_fits range-check aborts the
#      process the moment it type-checks any `i64` literal near the low bound. Disabling the trap
#      restores the native cc behaviour (the UB itself is benign here: the compared bound is
#      correct once the negation is allowed to wrap).
# With both, `zig cc -O2 -fno-sanitize=undefined` self-builds the whole corpus to exit 0.
# Correctness over the marginal speed of a transient bootstrap tool run a handful of times per
# release. The `file` architecture assertion still guards a wrong-arch/failed compile; the
# release-cross-smoke job (native.yml) additionally RUNS the x86_64 artifact so a future
# miscompile can never again pass a compile-only gate.
# This is the SINGLE source of truth shared by two callers:
#   • release.yml (mode=package) — builds + packages each target for publication;
#   • native.yml  (mode=smoke)   — PR-CI: builds + arch-checks only, so a broken zig version or
#                                  target is caught BEFORE merge (no qemu, no publish).
#
# Requires `zig` on PATH (the caller installs the pinned version). Uses package_release.sh for
# packaging so the smoke and the release exercise identical build flags.
#
# Usage: cross_compile_linux.sh <teko_c> <src_dir> <out_dir> [mode]
#   teko_c   path to the emitted teko.c
#   src_dir  the repo's src/ (teko_rt.*, assert.*, win32_compat.h)
#   out_dir  where packaged archives land (mode=package) or scratch (mode=smoke)
#   mode     "package" (default) or "smoke"
#
# POSIX sh only.
set -eu

TEKO_C="${1:?usage: cross_compile_linux.sh <teko_c> <src_dir> <out_dir> [mode]}"
SRC="${2:?missing src_dir}"
OUT="${3:?missing out_dir}"
MODE="${4:-package}"

mkdir -p "$OUT"

# The embedded `teko --version` reads -DTEKO_VERSION_STRING at build time (teko_rt.c stringizes
# it), exactly as teko's own run_cc does (src/build/project.tks). This hand-rolled zig path must
# pass the SAME define, or `--version` reports the 0.0.0.0-dev fallback instead of the release
# version. derive_version.sh yields the git TAG `v<version>[-<suffix>]`; the binary's --version is
# the tag MINUS the leading `v` (the RAW manifest `version[-suffix]` teko.tkp holds), so strip it.
# The value is a BARE token — embedded quotes broke Windows arg re-parsing (project.tks).
TEKO_VERSION_TAG="$(sh scripts/derive_version.sh 2>/dev/null || echo v0.0.0.0-dev)"
TEKO_VERSION_STRING="${TEKO_VERSION_TAG#v}"

# build_one LABEL TRIPLE STATIC EMIT_BUNDLE ARCH_KEYWORD
#   STATIC=1 → -static (musl); EMIT_BUNDLE=1 → package_release.sh also emits the portable src
#   bundle (once, on x86_64-glibc). ARCH_KEYWORD is grepped in `file` output to assert the target.
build_one() {
    label="$1"; triple="$2"; static="$3"; bundle="$4"; arch_kw="$5"
    echo "=== $label ($triple, static=$static) ==="
    gd="gd-$label"
    rm -rf "$gd"; mkdir -p "$gd"
    extra=""
    [ "$static" = "1" ] && extra="-static"
    zig cc -target "$triple" -std=c2x -w -O2 -fno-sanitize=undefined \
        "-DTEKO_VERSION_STRING=$TEKO_VERSION_STRING" $extra \
        -I"$SRC/runtime" -I"$SRC/assert" \
        "$TEKO_C" "$SRC/runtime/teko_rt.c" "$SRC/assert/assert.c" -lm \
        -o "$gd/teko"
    file "$gd/teko"
    if ! file "$gd/teko" | grep -q "$arch_kw"; then
        echo "cross_compile_linux: $label produced the WRONG architecture (expected '$arch_kw')" >&2
        exit 1
    fi
    if [ "$MODE" = "package" ]; then
        cp "$TEKO_C" "$gd/teko.c"
        EMIT_SRC_BUNDLE="$bundle" sh scripts/package_release.sh "$label" "$gd" "$SRC" "$OUT" posix
    fi
}

build_one linux-x86_64-glibc  x86_64-linux-gnu.2.28   0 1 "x86-64"
build_one linux-x86_64-musl   x86_64-linux-musl       1 0 "x86-64"
build_one linux-arm64-glibc   aarch64-linux-gnu.2.28  0 0 "aarch64"
build_one linux-arm64-musl    aarch64-linux-musl      1 0 "aarch64"
build_one linux-riscv64-glibc riscv64-linux-gnu.2.28  0 0 "RISC-V"
build_one linux-riscv64-musl  riscv64-linux-musl      1 0 "RISC-V"

echo "cross_compile_linux: all six Linux targets OK (mode=$MODE)"
