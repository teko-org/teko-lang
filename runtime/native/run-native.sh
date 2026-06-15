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

# HMAC (multi-arg runtime lowering). RFC 4231 Test Case 2: key "Jefe" (4a656665),
# data "what do ya want for nothing?" — HMAC-SHA-256/384/512.
check hmac.tks "$(cat <<'EXP'
5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843
af45d2e376484031617f78d2b58a6b1b9c7ef464f5a01b47e42ec3736322445e8e2240ca5e69e2c78b3239ecfab21649
164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737
EXP
)"

# AEAD (4-arg runtime lowering). AES-GCM = NIST Test Case 3; ChaCha20-Poly1305 = RFC 8439
# §2.8.2. Lines: AES seal (ct‖tag), AES open (pt), AES open tampered (REJECT), ChaCha seal
# (ct‖tag), ChaCha open (pt).
check aead.tks "$(cat <<'EXP'
42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091473f59854d5c2af327cd64a62cf35abd2ba6fab4
d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255
REJECT
d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc3ff4def08e4b7a9de576d26586cec64b61161ae10b594f09e26a7e902ecbd0600691
4c616469657320616e642047656e746c656d656e206f662074686520636c617373206f66202739393a204966204920636f756c64206f6666657220796f75206f6e6c79206f6e652074697020666f7220746865206675747572652c2073756e73637265656e20776f756c642062652069742e
EXP
)"

# Ed25519 sign/verify (RFC 8032 Test 3): deterministic signature, valid verify (1),
# tampered verify (0).
check sign_ed25519.tks "$(cat <<'EXP'
6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a
1
0
EXP
)"

echo "All native runner proofs passed."
