#!/usr/bin/env bash
# Phase 13 native runner — executable proof harness.
#
# Compiles the native `.tks` samples with `teko --target=host`, links them against
# libteko_rt.a via the system `cc`, RUNS the produced executables and asserts their
# stdout. This is the native analogue of runtime/wasm/run-*.mjs: real source compiled
# to a real binary that actually executes (not a strstr golden).
#
# Usage: run-native.sh <teko-binary> <libteko_rt.a> [tmpdir]
set -euo pipefail

TEKO="${1:?usage: run-native.sh <teko-binary> <libteko_rt.a> [tmpdir]}"
RTLIB="${2:?missing libteko_rt.a path}"
TMP="${3:-$(mktemp -d)}"
HERE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$TMP"

fail() { echo "FAIL: $*" >&2; exit 1; }

# One case: <sample.tks> <expected-exact-stdout>
check() {
  local sample="$1" expected="$2"
  local base exe got
  base="$(basename "$sample" .tks)"
  exe="$TMP/$base"
  echo "--- $sample ---"
  "$TEKO" build "$HERE/samples/$sample" --target=host --rt-lib="$RTLIB" -o "$exe" \
    || fail "compile/link failed for $sample"
  got="$("$exe")" || fail "$base exited non-zero"
  if [ "$got" != "$expected" ]; then
    fail "$base: expected [$expected], got [$got]"
  fi
  echo "OK: $base -> [$got]"
}

check hello.tks "hello from teko native"
# FIPS 180-4 SHA-256("abc") known-answer vector.
check hash_sha256.tks "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"

echo "All native runner proofs passed."
