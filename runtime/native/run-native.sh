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

# Real-time waiters: assert exact stdout AND a LOWER BOUND on real wall-clock elapsed (the time
# base is the real monotonic clock, so we assert >= min_ms with tolerance, not an exact duration).
# Timed via perl Time::HiRes (portable; macOS `date` lacks %N).
check_timed() {
  local sample="$1" expected="$2" min_ms="$3"
  local base exe got ms
  base="$(basename "$sample" .tks)"
  exe="$TMP/$base"
  echo "--- $sample (timed >= ${min_ms}ms) ---"
  "$TEKO" build "$HERE/samples/$sample" --target=host --rt-lib="$RTLIB" -o "$exe" \
    || fail "compile/link failed for $sample"
  got="$("$exe")" || fail "$base exited non-zero"
  [ "$got" = "$expected" ] || fail "$base: expected [$expected], got [$got]"
  ms="$(perl -MTime::HiRes=time -e 'my $t=time; system("$ARGV[0] >/dev/null 2>&1"); printf "%.0f", (time-$t)*1000' "$exe")"
  [ "$ms" -ge "$min_ms" ] || fail "$base: real elapsed ${ms}ms < ${min_ms}ms (real-time wait not honored)"
  echo "OK: $base -> [$got] in ~${ms}ms (>= ${min_ms}ms real)"
}

# Wall-clock / timezone surface: format_* of a fixed epoch is deterministic (run under TZ=UTC);
# now_* are real OS time, so pattern-check them. Asserts the OS-sourced civil-time surface works.
check_time() {
  local sample="time.tks" exe got l1 l2 l3 l4
  exe="$TMP/time"
  echo "--- $sample (wall-clock/timezone, TZ=UTC) ---"
  TZ=UTC "$TEKO" build "$HERE/samples/$sample" --target=host --rt-lib="$RTLIB" -o "$exe" \
    || fail "compile/link failed for $sample"
  got="$(TZ=UTC "$exe")" || fail "time exited non-zero"
  l1="$(printf '%s\n' "$got" | sed -n 1p)"; l2="$(printf '%s\n' "$got" | sed -n 2p)"
  l3="$(printf '%s\n' "$got" | sed -n 3p)"; l4="$(printf '%s\n' "$got" | sed -n 4p)"
  [ "$l1" = "2001-09-09T01:46:40Z" ] || fail "time.format_utc: [$l1]"
  [ "$l2" = "2001-09-09T01:46:40Z" ] || fail "time.format_local (TZ=UTC): [$l2]"
  printf '%s' "$l3" | grep -Eq '^[0-9]+$' || fail "time.now_unix not digits: [$l3]"
  printf '%s' "$l4" | grep -Eq '^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$' \
    || fail "time.now_utc not ISO-8601 UTC: [$l4]"
  echo "OK: time -> format_utc/local fixed + now_unix [$l3] + now_utc [$l4]"
}

# CSPRNG: non-deterministic, so assert format (two 64-hex-char lines) + that they differ.
check_random() {
  local sample="$1" exe got l1 l2
  exe="$TMP/$(basename "$sample" .tks)"
  echo "--- $sample (random) ---"
  "$TEKO" build "$HERE/samples/$sample" --target=host --rt-lib="$RTLIB" -o "$exe" \
    || fail "compile/link failed for $sample"
  got="$("$exe")" || fail "$sample exited non-zero"
  l1="$(printf '%s\n' "$got" | sed -n 1p)"; l2="$(printf '%s\n' "$got" | sed -n 2p)"
  printf '%s' "$l1" | grep -Eq '^[0-9a-f]{64}$' || fail "random line1 not 64 hex: [$l1]"
  printf '%s' "$l2" | grep -Eq '^[0-9a-f]{64}$' || fail "random line2 not 64 hex: [$l2]"
  [ "$l1" != "$l2" ] || fail "random: two draws were identical"
  echo "OK: random -> two distinct 32-byte draws"
}

# UUID v4/v7: non-deterministic, so assert the canonical layout + version/variant nibbles.
check_uuid() {
  local sample="$1" exe got l1 l2
  exe="$TMP/$(basename "$sample" .tks)"
  echo "--- $sample (uuid) ---"
  "$TEKO" build "$HERE/samples/$sample" --target=host --rt-lib="$RTLIB" -o "$exe" \
    || fail "compile/link failed for $sample"
  got="$("$exe")" || fail "$sample exited non-zero"
  l1="$(printf '%s\n' "$got" | sed -n 1p)"; l2="$(printf '%s\n' "$got" | sed -n 2p)"
  printf '%s' "$l1" | grep -Eq '^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$' \
    || fail "uuid v4 malformed: [$l1]"
  printf '%s' "$l2" | grep -Eq '^[0-9a-f]{8}-[0-9a-f]{4}-7[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$' \
    || fail "uuid v7 malformed: [$l2]"
  echo "OK: uuid -> v4 [$l1], v7 [$l2]"
}

check hello.tks "hello from teko native"
# Phase 15 (15.A): concrete class — fields + methods + STATIC dispatch, zero runtime reflection.
# `Point()` -> OP_OBJ_NEW; `p.x = 3` -> OP_OBJ_SET; `p.sum()`/`p.scale(10)` -> OP_CALL_FUNC
# (the method routine reads `self.x`/`self.y` via OP_OBJ_GET). Prints 7 (3+4) then 70 ((3+4)*10).
check class.tks "$(cat <<'EXP'
7
70
EXP
)"
# Phase 15 (15.B): abstract/trait dynamic dispatch via a compile-time STATIC VTABLE. A Shape-typed
# fat reference dispatches `area()`/`to_string()` to Circle then (after reassignment) Square by the
# runtime type_id: vtable_get -> slot -> OP_CALL_FUNC. `to_string` rides the same vtable (Phase-16
# hook). 12 (Circle.area), 112 (Circle.to_string), 9 (Square.area), 209 (Square.to_string).
check traits.tks "$(cat <<'EXP'
12
112
9
209
EXP
)"
# Phase 15 (15.C): generics via real per-type MONOMORPHIZATION. Factory<T> is specialized per
# instantiation (Factory$Circle/Factory$Square); inside make(), T() instantiates the concrete type
# and t.tag() statically dispatches — resolved at compile time, no runtime type param. 11, 22.
check generics.tks "$(cat <<'EXP'
11
22
EXP
)"
# Phase 15 (15.D): event subsystem — `event`/`subscribe`/`raise` with fanout + fire_and_forget.
# `raise Ping(5)` fan-outs to both subscribers, spawned over the cooperative scheduler + drained at
# exit, so handlers run AFTER the main body (deferred). 1, 2 (main), then 15 (onA), 25 (onB).
check eventbus.tks "$(cat <<'EXP'
1
2
15
25
EXP
)"
# Phase 14 (14.A): `routines { worker(); worker(); }` fires two background tasks. The native
# scheduler (teko_rt_run) drains them at $main exit, so they run AFTER main's body — the two
# "worker ran" lines follow "main start"/"main end", proving deferred (not inline) execution.
check routines.tks "$(cat <<'EXP'
main start
main end
worker ran
worker ran
EXP
)"
# Phase 14 (14.B): duplex channel — bidirectional (0->1 then 1->0) + a structured CLOSED
# status (3) from poll() after close, instead of blocking. Lowers to OP_DUPLEX_* -> teko_rt.
check duplex.tks "$(cat <<'EXP'
111
222
3
EXP
)"
# Phase 14 (14.C, real-time clock): delayed (timed) channel on the REAL monotonic clock. Sent out
# of deadline order (10@2ms, 20@6ms, 30@4ms), they are poll-drained in real-deadline order
# (10,30,20) — timing-robust (recv returns the earliest-due). Last is due at 6ms, so real elapsed
# >= ~5ms. Lowers to OP_DELAYED_* -> teko_rt_delayed_* (the wrapper reads teko_rt_now_ns).
check_timed delayed.tks "$(cat <<'EXP'
10
30
20
EXP
)" 5
# Phase 14 (14.D): broadcast (non-destructive 1:N pub-sub) — one publisher, two subscribers;
# each value is written once but BOTH subscribers read it independently (10,20 / 10,20).
check broadcast.tks "$(cat <<'EXP'
10
20
10
20
EXP
)"
# Phase 14 (14.E): shared memory — a `shared { }` coarse-locked block with `atomic.*` cells.
# Atomic accumulation (5+3 inside the block = 8, then +2 = 10). Lowers to teko_shared_*/teko_atomic_*.
check shared.tks "$(cat <<'EXP'
8
10
EXP
)"
# Phase 14 (real-time clock): timespan waiters — `await 5ms;` cooperatively drains the run queue so
# the queued worker runs AT the await (output 1,2,3); `wait 10ms;` waits on the REAL monotonic clock.
# Lowers to teko_rt_await_ns / teko_rt_wait_ns. Assert order + real elapsed >= 12ms (await 5 + wait
# 10 ≈ 15ms; lower bound with tolerance — the time source is real, so no exact duration).
check_timed waiters.tks "$(cat <<'EXP'
1
2
3
EXP
)" 12

# Phase 14 (control-flow foundation): structured loops + branches lowered from source — a
# while-loop sums 0..4 = 10; a loop{}+if+break/continue counts to 5. Lowers to OP_LOOP_*/OP_IF_*.
check controlflow.tks "$(cat <<'EXP'
10
5
EXP
)"

# Phase 14 (14.F): resilience — retry { } fallback { } + circuit cb { } fallback { } driving the
# teko_retry C policy. succeed-on-3rd (3); exhaust->fallback (777, t2=2); timeout->fallback
# (555, tt=1); logarithmic (444, lg=3); breaker trips after 2 failures (ran=2, fallback=5).
check resilience.tks "$(cat <<'EXP'
3
777
2
555
1
444
3
2
5
EXP
)"

# Phase 14 capstone (14.H): the whole phase in one program — atomic accumulator (15), delayed
# channel drain (6), a background routine with a LOOP inside it run at an `await` (1,2,3), and a
# final marker after `wait` (42). Combines functions + routines + loops + channels + waiters.
check capstone.tks "$(cat <<'EXP'
15
6
1
2
3
42
EXP
)"

# Phase 14 (14.I): real concurrent producer/consumer — a background routine takes MULTIPLE args
# (Go-style: a shared channel handle + a count), fills the channel; the consumer drains it with a
# poll loop until EMPTY. Proves routine argument passing + handle sharing. 1+2+3+4+5 = 15.
check producer_consumer.tks "15"

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

# X25519 ECDH (RFC 7748 §5 test vectors 1 & 2).
check x25519.tks "$(cat <<'EXP'
c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552
95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957
EXP
)"

# KDF: HKDF-SHA-256 (RFC 5869 TC1, L=42) and PBKDF2-HMAC-SHA256("passwd","salt",1,64).
check kdf.tks "$(cat <<'EXP'
3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865
55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783
EXP
)"

# ECDSA P-256 (RFC 6979 A.2.5) and P-384 (A.2.6), message "sample": deterministic sig,
# valid verify (1), tampered verify (0).
check sign_ecdsa.tks "$(cat <<'EXP'
efd48b2aacb6a8fd1140dd9cd45e81d69d2c877b56aaf991c34d0ea84eaf3716f7cb1c942d657c41d436c7a1b6e29f65f3e900dbb9aff4064dc4ab2f843acda8
1
0
94edbb92a5ecb8aad4736e56c691916b3f88140666ce9fa73d64c4ea95ad133c81a648152e44acf96e36dd1e80fabe4699ef4aeb15f178cea1fe40db2603138f130e740a19624526203b6351d0a3a94fa329c145786e679e7b82c71a38628ac8
1
0
EXP
)"

# SHAKE128/256 (FIPS 202, empty message, 32-byte output).
check shake.tks "$(cat <<'EXP'
7f9c2ba4e88f827d616045507605853ed73b8093f6efbc88eb1a6eacfa66ef26
46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762f
EXP
)"

# RSA PSS + OAEP: sign->verify (1), wrong-message verify (0), OAEP encrypt->decrypt round-trip
# recovering the plaintext "Hello, RSA!" (48656c6c6f2c2052534121).
check rsa.tks "$(cat <<'EXP'
1
0
48656c6c6f2c2052534121
EXP
)"

check_random random.tks
check_uuid uuid_rng.tks
check_time

echo "All native runner proofs passed."
