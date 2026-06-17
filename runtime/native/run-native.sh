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

# Phase 16 (16.F): a FAIL-LOUD case — the program must exit NON-ZERO and print <diag> to stderr
# (a checked conversion that rejects its input must not silently truncate). <pre> is the stdout
# emitted before the abort.
check_fail() {
  local sample="$1" pre="$2" diag="$3"
  local base exe out err rc
  base="$(basename "$sample" .tks)"
  exe="$TMP/$base"
  echo "--- $sample (fail-loud) ---"
  "$TEKO" build "$HERE/samples/$sample" --target=host --rt-lib="$RTLIB" -o "$exe" \
    || fail "compile/link failed for $sample"
  set +e; out="$("$exe" 2>"$TMP/$base.err")"; rc=$?; set -e   # the abort is EXPECTED (errexit off)
  err="$(cat "$TMP/$base.err")"
  [ "$rc" -ne 0 ] || fail "$base: expected non-zero exit (fail-loud), got 0"
  [ "$out" = "$pre" ] || fail "$base: expected stdout [$pre], got [$out]"
  case "$err" in *"$diag"*) : ;; *) fail "$base: stderr [$err] missing [$diag]";; esac
  echo "OK: $base -> exit $rc, stderr contains [$diag]"
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
# Phase 16 (16.A): culture-invariant conversion surface — int/bool to_string + str_concat
# (OP_CALL_RUNTIME ids 49/51/52 -> teko_rt_* over the portable teko_convert.c source of truth).
# Asserts the locale-invariant default: `.`-decimal, NO digit grouping, canonical "true"/"false".
check convert.tks "$(cat <<'EXP'
42
1000000
true
false
x = 42
EXP
)"
# Phase 16 (16.B): auto-`to_string` on string concatenation (the core deliverable). `+` with a
# string operand becomes culture-invariant concatenation; non-string operands auto-convert via
# to_string (id 49) + str_concat (id 52). No explicit conversion call appears in the source.
check concat.tks "$(cat <<'EXP'
x = 42
sum = 50
42 items
count: 42
n=42
EXP
)"
# Phase 16 (16.C): string INTERPOLATION — `"…{expr}…"` interpolates each hole (auto-to_string),
# `{{`/`}}` are literal braces. Same str_concat machinery as 16.B; culture-invariant.
check interp.tks "$(cat <<'EXP'
x = 42
42 items, 42 total
sum = 50
count: 42
braces { } kept
[42]
EXP
)"
# Phase 16 (16.D): user-defined-type to_string in concat/interpolation. A class with a to_string
# method dispatches it (via OP_CALL_FUNC); a class without one gets the synthesized default
# ClassName(fields). Zero runtime reflection.
check tostring.tks "$(cat <<'EXP'
temp is T=25
[T=25]
point = Point(3, 4)
p=Point(3, 4)
EXP
)"
# Phase 16 (16.E): EXPLICIT integer formats (developer-supplied spec) — radix / zero-pad /
# thousands grouping (ids 56/57/58). Distinct from the culture-invariant default; still locale-free.
check format.tks "$(cat <<'EXP'
ff
1010
100
00042
1,000,000
hex = ff
EXP
)"
# Phase 16 (16.F): CHECKED string->primitive parse (ids 53/55). Happy path returns the value
# (auto-to_string'd back on concat); the fail-loud path aborts non-zero with a stderr diagnostic.
check parse.tks "$(cat <<'EXP'
n = 123
neg = -42
ws = 7
true
false
EXP
)"
check_fail parse_fail.tks "before" "convert.parse_int: invalid integer"
# Phase 17 (17.A): the f64 VALUE MODEL — float literals, a float local, float arithmetic + mixed
# int→float promotion, and float comparisons, carried through a PARALLEL float accumulator (xmm/d
# on native), additive to the integer path. No float formatter yet (17.C/D), so each comparison's
# 0/1 result is observed via convert.int_to_str (id 49). Byte-identical to the WASM proof.
check float.tks "$(cat <<'EXP'
a = 0
b = 1
c = 1
EXP
)"
# Phase 17 (17.B): checked int↔float casts (convert.to_int / convert.to_float) + float modulo (`%`).
# to_float promotes int→f64 (OP_I2F); to_int truncates toward zero with a CHECKED fail-loud guard
# (OP_F2I); `%` on floats is OP_FMOD (inline a-trunc(a/b)*b — no libm). Byte-identical to the WASM proof (run-cast.mjs).
check cast.tks "$(cat <<'EXP'
a = 1
n = 7
m = 1
EXP
)"
# Phase 17 (17.B) FAIL-LOUD: convert.to_int(3e9) is > INT32_MAX (outside i32.trunc_f64_s's range), so
# it does NOT silently truncate/wrap — the emitted inline guard calls teko_rt_f2i_fail (exit 70 +
# stderr diag). WASM traps on the same value (run-cast.mjs), so the behavior is identical on both.
check_fail cast_fail.tks "before" "convert.to_int: float out of i32 range"
# Phase 17 (17.D): convert.float_to_str (id 50, the f64-ARG runtime call) + auto-`to_string` for
# floats in `+` concat (right- AND left-float) and `"{…}"` interpolation (a float hole + a float
# EXPRESSION hole). The value rides the FP-arg register (xmm0/d0 = $f0), not $w0; the formatter is
# the 17.C Ryu shortest-round-trip core. Byte-identical to the WASM proof (run-floatstr.mjs).
check floatstr.tks "$(cat <<'EXP'
f = 3.14
0.1
pi~ 3.14, dbl 6.28
2.5
3.14 = pi
EXP
)"
# Phase 17 (17.E): CHECKED `convert.parse_float` (id 54, str->f64, fail-loud). The INVERSE of id 50:
# string arg in $w0, parsed double in $f0 (VT_FLOAT). The parser is the freestanding correctly-rounded
# teko_convert_parse_f64 (same C as the reactor) — parse->format round-trips. Byte-identical to WASM
# (run-parsefloat.mjs). `parsefloat_fail` proves fail-loud: malformed input aborts non-zero (not 0.0).
check parsefloat.tks "$(cat <<'EXP'
3.14
got 0.5
3.0
EXP
)"
check_fail parsefloat_fail.tks "before" "convert.parse_float: invalid float"
# Phase 17.F.3: the 256-byte EXACT base-10 `decimal` VALUE MODEL — `dec` literals, a decimal local,
# decimal arithmetic (`+`), and decimal comparisons (`== <`), carried through a SEPARATE 256-byte
# memory-slot accumulator ($d0/$d1, native stack slots passed BY POINTER to teko_rt_decimal_*),
# additive to the integer + f64 paths. No decimal formatter yet (17.F.4), so each comparison's 0/1
# result is observed via convert.int_to_str (id 49). The exactness is the point: 9.99 + 0.01 ==
# 10.00 holds in base-10 (it does NOT in binary f64). Byte-identical to the WASM proof (run-decimal.mjs).
check decimal.tks "$(cat <<'EXP'
eq = 1
lt = 1
EXP
)"
# Phase 17.F.4: the decimal LANGUAGE SURFACE + casts + auto-`to_string`. `decimal.to_string` (id 59)
# renders a decimal; `decimal.parse` (id 60, checked) parses one; `convert.to_decimal`/`to_int`/
# `to_float` cast int/float ↔ decimal (OP_I2D/F2D/D2I/D2F); a decimal in `+` concat / `"{…}"`
# interpolation auto-`to_string`s via id 59; mixed `int + decimal` promotes the int (I2D). 10.00 is
# 9.99+0.01 exact in base-10; D2I truncates toward zero (int 10). Byte-identical to the WASM proof
# (run-decimal-surface.mjs).
check decimal_surface.tks "$(cat <<'EXP'
10.00
total = 10.00
[10.00]
3.50
grand = 15.00
bumped = 13.00
2.5
int 10
EXP
)"
# Fail-loud: decimal.parse("abc") aborts non-zero (exit 70 + stderr) — no silent zero. WASM traps on
# the same value (run-decimal-surface.mjs), so the behavior is identical on both targets.
check_fail decimal_fail.tks "before" "decimal.parse: invalid decimal"
# Phase 18 (18.E.1): the FIXED-size CONTIGUOUS `array` substrate — literal build, index read/write,
# and `.len` (O(1) metadata). Byte-identical to the WASM proof (run-arrays.mjs). The array store is
# the SAME teko_array.c source of truth (linked here, compiled into the wasm32 reactor there).
check arrays.tks "$(cat <<'EXP'
a[1] = 20
a[0] = 99
len = 3
EXP
)"
# Fail-loud: an out-of-range index aborts non-zero (exit 70 + stderr) — no silent zero / corruption.
# WASM traps on the same access (run-arrays-fail.mjs), identical behavior on both targets.
check_fail arrays_fail.tks "before" "array: index out of bounds"
# Phase 18 (18.E.2): `for NAME in ARR { }` iteration over an i64 array (control-flow foundation).
# Byte-identical to the WASM proof (run-foreach.mjs).
check foreach.tks "sum = 60"
# Phase 18 (18.E.2): the TYPED `i32[]` PACKED numeric array (the SIMD substrate) — `: i32[]` literal,
# index read/write, `.len`, and `for x in a`. Byte-identical to the WASM proof (run-iarray.mjs). The
# packed-i32 store is the SAME teko_iarray.c source of truth (linked here, in the wasm32 reactor there).
check iarray.tks "$(cat <<'EXP'
a[2] = 6
len = 3
sum = 15
a[0] = 40
EXP
)"
# Fail-loud: an out-of-range index on a typed `i32[]` aborts non-zero (exit 70 + stderr). WASM traps on
# the same access (run-iarray-fail.mjs), identical behavior on both targets.
check_fail iarray_fail.tks "before" "iarray: index out of bounds"
# Phase 18 (18.E.3): SoA (structure-of-arrays) layout — `soa Point[N]` = k CONTIGUOUS typed-i32 field
# runs; `s[i].field` r/w, `s.len`, and the whole-run accessor `s.field` (the i32[] SIMD hook, usable as
# a typed array). FRONTEND-only over the iarray runtime (NO new opcode/runtime) — byte-identical to the
# WASM proof (run-soa.mjs).
check soa.tks "$(cat <<'EXP'
s[1].x = 20
s[2].y = 3
len = 3
sum_x = 60
col.len = 3
col[1] = 20
EXP
)"
# Phase 18 (18.E.3): AoS (array-of-objects) layout, the contrast to SoA — `[Point(), …]` is an `array`
# of object handles, `a[i].field` is index-then-member (ARR_GET then OBJ_GET; fields interleaved per
# object). Same logical result as SoA (sum of x = 60), AoS layout. Byte-identical to the WASM proof
# (run-aos.mjs).
check aos.tks "$(cat <<'EXP'
a[1].x = 20
a[2].y = 3
len = 3
sum_x = 60
EXP
)"
# Phase 18 (18.E.4): REAL per-ISA SIMD reduction — `simd.sum(run)` over a contiguous typed i32[] run,
# the vector loop emitted as REAL backend instructions (this machine is arm64 → the NEON kernel;
# CI Linux x86_64 → SSE2). The proof self-checks: the vectorized sum MUST equal an in-program scalar
# reference loop (a mis-emitted vector kernel diverges and fails HERE). N=10 (8+2) and the SoA field
# run N=6 (4+2) both exercise the scalar TAIL. Byte-identical stdout to the WASM proof (run-simd.mjs).
check simd.tks "$(cat <<'EXP'
simd = 55
scalar = 55
soa_simd = 210
soa_scalar = 210
EXP
)"
# Phase 18 (18.A): Zero-Overhead Optionals — `?T` nullability + `null` + the Elvis `??`. An optional
# local is compacted (payload slot + a hidden 1-word present companion); `a ?? d` branches on the
# present flag via OP_IF (→ native je/cbz), choosing the payload when present else the default. No new
# IL/runtime (reuses OP_IF + load/store-local). Byte-identical to the WASM proof (run-optionals.mjs).
check optionals.tks "$(cat <<'EXP'
b = 7
d = 5
e = 5
EXP
)"
# Phase 18 (18.B): SAFE NAVIGATION `?.` over optional objects — `obj?.field`/`obj?.method()` guarded
# by OP_IF on the 18.A present flag: present → the member access runs; null → it is SKIPPED (no deref
# of a null handle) and the empty-optional result lets a trailing `?? d` default. The optional's class
# comes from the `?Box` annotation (so a null receiver still resolves the member statically). No new
# IL/runtime. Byte-identical to the WASM proof (run-safenav.mjs).
check safenav.tks "$(cat <<'EXP'
a = 21
b = 42
c = -1
d = -1
EXP
)"
# Phase 18 (18.C): `defer <stmt>;` — scope-closing registration, run in LIFO (reverse) order at $main
# close. The frontend captures each deferred statement's source and re-lexes + lowers it just before
# OP_HALT; deferred statements may reference locals (still live) incl. auto-to_string concat. No new
# IL/runtime (the statements lower to ordinary IL, relocated to scope end). Byte-identical to WASM
# (run-defer.mjs): immediate start/middle/end, then LIFO "last registered" then "deferred n = 42".
check defer.tks "$(cat <<'EXP'
start
middle
end
last registered
deferred n = 42
EXP
)"
# Phase 18 (18.D): `comptime` — compile-time evaluation. `comptime let NAME = <const-expr>;` is folded
# by the frontend's constant evaluator (int literals, other comptime constants, parens, + - * / %); no
# IL arithmetic is emitted for the expression, so the runtime carries only the folded constant (a read
# of NAME is a single iconst). Comptime constants compose (B references A). No new IL/runtime. Byte-
# identical to the WASM proof (run-comptime.mjs).
check comptime.tks "$(cat <<'EXP'
A = 42
B = 50
C = 10
D = 2
EXP
)"
# Phase 15 (15.A): concrete class — fields + methods + STATIC dispatch, zero runtime reflection.
# `Point()` -> OP_OBJ_NEW; `p.x = 3` -> OP_OBJ_SET; `p.sum()`/`p.scale(10)` -> OP_CALL_FUNC
# (the method routine reads `self.x`/`self.y` via OP_OBJ_GET). Prints 7 (3+4) then 70 ((3+4)*10).
check class.tks "$(cat <<'EXP'
7
70
EXP
)"
# Phase 15 (15.A) regression: a class METHOD CALL used directly as an EXPRESSION ARGUMENT
# (`emit_int(p.raw())`) and as an arithmetic operand (`p.raw() + 100`), plus a method returning a
# bare `self.<field>`. Before the fix the call was dropped in argument/sub-expression position (the
# evaluator emitted iconst 0); now each `obj.method(args)` head lowers to OP_CALL_FUNC. 42,42,47,142.
check method_arg.tks "$(cat <<'EXP'
42
42
47
142
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
# Phase 15 (15.B) regression: a DYNAMIC trait dispatch `g.method(...)` used directly as an EXPRESSION
# ARGUMENT (`emit_int(g.area())`) and as an arithmetic operand (`g.area() + 100`). Before the fix the
# fat trait-typed `g.method()` head was unhandled in argument/sub-expression position (only static
# `obj.method()` was), so the call was dropped (iconst 0); now it lowers to vtable_get + OP_CALL_FUNC.
# 12 (Circle.area), 112 (Circle.area+100), 25 (Square.area after reassignment).
check trait_arg.tks "$(cat <<'EXP'
12
112
25
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
