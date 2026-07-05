#!/usr/bin/env sh
# scripts/cross_compile_linux.sh — cross-compile the emitted teko.c into the six Linux
# artifacts with `zig cc`: glibc (dynamic, pinned to an old glibc for wide compat) and musl
# (fully static) for x86_64, arm64 and riscv64. ONE emitted teko.c → six targets, no qemu.
#
# Cross-compilation is deterministic: a successful compile of the (CI-proven, fixpoint) teko.c
# for a target IS the correctness argument — the binary need not be RUN under emulation. Each
# artifact's architecture is asserted from `file` output; a wrong-arch or failed compile aborts.
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
    zig cc -target "$triple" -std=c2x -w -O2 $extra \
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
