#!/usr/bin/env sh
# scripts/build_gen1_from_c.sh — link a runnable `teko` from an ALREADY-EMITTED teko.c.
#
# WHY THIS EXISTS (owner ruling 2026-07-26 — the artifact lane is the single root): every
# `pull_request` lane that used to run `scripts/build_with_seed_fallback.sh` was paying for a seed
# provision plus a full self-host before it could do its own work. The artifact lane pays that
# ONCE and publishes what it produced; a consumer that needs a *differently compiled* compiler —
# which is exactly what a sanitizer lane needs, and the ONLY thing a downloaded binary cannot give
# it — takes the published `teko.c` and links it here with the flags it cares about.
#
# This is not a second build ladder. There is no seed, no git history, no fallback: the input is
# the C the root already emitted, and the only work is one `cc` invocation. A sanitizer lane's
# cost drops from "seed + ladder + instrumented self-host" to "one instrumented link".
#
# Usage:  build_gen1_from_c.sh <teko_c> <src_dir> <out_dir>
# Env:
#   CC            the compiler to invoke (default: cc — which is how scripts/ci_cc_wrap.sh's
#                 PATH shim gets in front of the link without this script knowing about it)
#   CFLAGS_EXTRA  flags appended after the fixed ones (sanitizer flags, -g, …)
#
# The build line is the SAME one scripts/package_release.sh documents for the portable bootstrap
# bundle and that teko's own run_cc uses, minus the version define (no consumer of this script
# asserts `--version`; the PUBLISHED assets, which do, are built by native_linux_asset.sh which
# passes it).
#
# POSIX sh only.
set -eu

TEKO_C="${1:?usage: build_gen1_from_c.sh <teko_c> <src_dir> <out_dir>}"
SRC="${2:?missing src_dir}"
OUT="${3:?missing out_dir}"
CC="${CC:-cc}"

[ -f "$TEKO_C" ] || { echo "build_gen1_from_c: no emitted C at '$TEKO_C'" >&2; exit 1; }
[ -f "$SRC/runtime/teko_rt.c" ] || { echo "build_gen1_from_c: '$SRC' is not the repo's src/" >&2; exit 1; }

mkdir -p "$OUT"

echo "build_gen1_from_c: CC=$CC CFLAGS_EXTRA='${CFLAGS_EXTRA:-}'"
"$CC" --version 2>/dev/null | head -n1 || true

# shellcheck disable=SC2086  # CFLAGS_EXTRA is a flag LIST and must word-split.
# -pthread: teko_rt.c's tk_thread_spawn (§10 C0a) calls pthread_create/detach/join on
# non-Windows (guarded by `#ifndef _WIN32`; Windows uses CreateThread, no flag). Kept out of the
# Windows lane, which links its own thread API. Retired when teko_rt.c goes to FFI (§16).
GEN1_PTHREAD="-pthread"
case "$(uname -s 2>/dev/null)" in MINGW*|MSYS*|CYGWIN*|Windows_NT) GEN1_PTHREAD="" ;; esac
# shellcheck disable=SC2086  # GEN1_PTHREAD/CFLAGS_EXTRA are flag LISTs and must word-split.
"$CC" -std=c2x -w -g $GEN1_PTHREAD ${CFLAGS_EXTRA:-} \
    -I"$SRC/runtime" -I"$SRC/assert" \
    "$TEKO_C" "$SRC/runtime/teko_rt.c" "$SRC/assert/assert.c" \
    -o "$OUT/teko"

[ -x "$OUT/teko" ] || { echo "build_gen1_from_c: the link reported success but $OUT/teko is not executable" >&2; exit 1; }
echo "build_gen1_from_c: wrote $OUT/teko"
