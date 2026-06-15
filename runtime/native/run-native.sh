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

# Fixed-size hash family (one emit per line). Vectors: SHA-384/512 (FIPS 180-4),
# SHA3-256/512 (NIST), BLAKE3("") (spec), BLAKE2b-512("abc") (RFC 7693).
check hash_family.tks "$(cat <<'EXP'
cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7
ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f
3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532
b751850b1a57168a5693cd924b6b096e08f621827444f70d884f5d0240d2712e10e116e9192af3c91a7ec57647e3934057340b4cf408d5a56592f8274eec53f0
af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262
ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923
EXP
)"

echo "All native runner proofs passed."
