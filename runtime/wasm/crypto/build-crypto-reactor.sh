#!/usr/bin/env bash
# Build the wasm32 crypto reactor (crypto.wasm) from the single C crypto runtime.
# Phase 13 Sub-phase C ("big step"): instead of re-emitting each primitive in WAT, we
# compile src/runtime/teko_crypto_*.c + the teko_rt_* hex wrappers to a freestanding
# wasm32 "reactor" module that the Teko-emitted module imports (namespace "crypto") and
# shares one linear memory with. See libc_shim.c for the memory model.
#
# Toolchain (no wasi-sdk): a wasm32-capable clang + wasm-ld (LLVM lld) + this dir's libc
# shim. Override the tools via $TEKO_WASM_CLANG / $TEKO_WASM_LD; otherwise we autodetect
# (system clang/wasm-ld if wasm32-capable, then Homebrew LLVM/lld).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT="${1:-$HERE/crypto.wasm}"

# --- locate a wasm32-capable clang -------------------------------------------------
have_wasm32() { "$1" --print-targets 2>/dev/null | grep -qi wasm32; }
CLANG="${TEKO_WASM_CLANG:-}"
if [ -z "$CLANG" ]; then
  for c in clang /opt/homebrew/opt/llvm/bin/clang /usr/local/opt/llvm/bin/clang \
           /usr/lib/llvm-*/bin/clang; do
    if command -v "$c" >/dev/null 2>&1 && have_wasm32 "$c"; then CLANG="$c"; break; fi
  done
fi
[ -n "$CLANG" ] || { echo "error: no wasm32-capable clang found (set TEKO_WASM_CLANG)"; exit 1; }

# --- locate wasm-ld ----------------------------------------------------------------
WASMLD="${TEKO_WASM_LD:-}"
if [ -z "$WASMLD" ]; then
  for l in wasm-ld /opt/homebrew/bin/wasm-ld /opt/homebrew/opt/lld/bin/wasm-ld \
           /usr/local/opt/lld/bin/wasm-ld /usr/lib/llvm-*/bin/wasm-ld \
           wasm-ld-20 wasm-ld-19 wasm-ld-18 wasm-ld-17 wasm-ld-16 wasm-ld-15 wasm-ld-14; do
    if command -v "$l" >/dev/null 2>&1; then WASMLD="$l"; break; fi
  done
fi
[ -n "$WASMLD" ] || { echo "error: no wasm-ld found (install LLVM lld; set TEKO_WASM_LD)"; exit 1; }

echo "[crypto-reactor] clang  = $CLANG"
echo "[crypto-reactor] wasm-ld= $WASMLD"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

CFLAGS=(--target=wasm32 -O2 -ffreestanding -nostdlib
        -I "$ROOT/src/runtime" -I "$ROOT/runtime/native" -I "$HERE/include")

# Sources: the whole crypto runtime + UUID + the teko_rt_* hex-at-surface wrappers + shim.
SRCS=("$HERE/libc_shim.c" "$ROOT/runtime/native/teko_rt.c" "$ROOT/src/runtime/teko_uuid.c"
      "$ROOT/src/runtime/teko_duplex.c"   # Phase 14: duplex channel runtime (source of truth)
      "$ROOT/src/runtime/teko_delayed.c"   # Phase 14: delayed (timed) channel runtime
      "$ROOT/src/runtime/teko_broadcast.c" # Phase 14: broadcast (1:N pub-sub) channel runtime
      "$ROOT/src/runtime/teko_shared.c"    # Phase 14: shared memory (coarse lock + atomic cells)
      "$ROOT/src/runtime/teko_retry.c"     # Phase 14: resilience policy (retry/circuit)
      "$ROOT/src/runtime/teko_time.c"      # Phase 14: civil time formatter (wall-clock/timezone)
      "$ROOT/src/runtime/teko_object.c"    # Phase 15: object instance store (class field cells)
      "$ROOT/src/runtime/teko_array.c"     # Phase 18.E.1: fixed-size contiguous array (checked)
      "$ROOT/src/runtime/teko_iarray.c"    # Phase 18.E.2: typed i32[] packed array (checked, SIMD substrate)
      "$ROOT/src/runtime/teko_vtable.c"    # Phase 15.B: static vtable (abstract/trait dispatch)
      "$ROOT/src/runtime/teko_convert.c"   # Phase 16: culture-invariant conversion runtime
      "$ROOT/src/runtime/teko_convert_f64.c" # Phase 17.C: Ryu f64->string (17.D EXPORTS teko_rt_float_to_string)
      "$ROOT/src/runtime/teko_decimal.c") # Phase 17.F.1: exact base-10 decimal core (17.F.3 EXPORTS the teko_rt_decimal_* wrappers)
for f in "$ROOT"/src/runtime/teko_crypto_*.c; do SRCS+=("$f"); done

OBJS=()
for f in "${SRCS[@]}"; do
  o="$WORK/$(basename "$f").o"
  # Phase 17.F.3: teko_decimal.c's hand-rolled 64x64->128 umul64 (NO __int128 in the source) is
  # folded by a recent clang's 128-bit-multiply IDIOM RECOGNIZER (an -O2+ pass) into a `__multi3`
  # builtin call, which wasm-ld can't resolve (no compiler-rt) — and providing __multi3 would
  # reintroduce __int128. That idiom pass does NOT run at -O1, so compile JUST this file at -O1
  # (trailing -O1 overrides the -O2 in CFLAGS): the object stays 64-bit-limb-only and self-contained
  # on every clang version, with negligible perf cost for the decimal ops. (Older Linux clang never
  # folded it even at -O2; -O1 makes it robust across toolchains.)
  extra=""
  case "$(basename "$f")" in teko_decimal.c) extra="-O1" ;; esac
  "$CLANG" "${CFLAGS[@]}" $extra -c "$f" -o "$o"
  OBJS+=("$o")
done

# Exported reactor entry points = OP_CALL_RUNTIME ids 5,10-40 (wasm_is_crypto_ext_id in
# src/codegen/bare_metal/emit_wasm.c). Keep this list in sync with that switch.
EXPORTS=(teko_rt_sha512_hex teko_rt_sha384_hex teko_rt_sha3_256_hex teko_rt_sha3_512_hex
         teko_rt_blake3_hex teko_rt_blake2b_hex
         teko_rt_hmac_sha256 teko_rt_hmac_sha384 teko_rt_hmac_sha512
         teko_rt_aes_gcm_seal teko_rt_aes_gcm_open
         teko_rt_chacha20poly1305_seal teko_rt_chacha20poly1305_open
         teko_rt_ed25519_sign teko_rt_ed25519_verify teko_rt_x25519
         teko_rt_hkdf_sha256 teko_rt_pbkdf2_sha256
         teko_rt_ecdsa_p256_sign teko_rt_ecdsa_p256_verify
         teko_rt_ecdsa_p384_sign teko_rt_ecdsa_p384_verify
         teko_rt_shake128 teko_rt_shake256
         teko_rt_rsa_pss_sign teko_rt_rsa_pss_verify
         teko_rt_rsa_oaep_encrypt teko_rt_rsa_oaep_decrypt
         # Phase 14 (14.B): duplex channel ops (OP_DUPLEX_* import these from the reactor).
         teko_rt_duplex_open teko_rt_duplex_send teko_rt_duplex_recv
         teko_rt_duplex_poll teko_rt_duplex_close
         # Phase 14 (14.C): delayed (timed) channel ops (OP_DELAYED_* import these).
         teko_rt_delayed_open teko_rt_delayed_send
         teko_rt_delayed_recv teko_rt_delayed_poll teko_rt_delayed_close
         # Phase 14 (14.D): broadcast (1:N pub-sub) channel ops (OP_BCAST_* import these).
         teko_rt_bcast_open teko_rt_bcast_subscribe teko_rt_bcast_publish
         teko_rt_bcast_recv teko_rt_bcast_poll teko_rt_bcast_close
         # Phase 14 (14.E): shared-memory ops (OP_SHARED_*/OP_ATOMIC_* import these directly).
         teko_shared_enter teko_shared_leave
         teko_atomic_cell teko_atomic_add teko_atomic_load teko_atomic_store
         # Phase 14 (14.F): resilience policy ops (OP_RETRY_*/OP_CIRCUIT_* import these).
         teko_rt_retry_new teko_rt_retry_should_continue teko_rt_retry_next_delay
         teko_rt_circuit_new teko_rt_circuit_allow teko_rt_circuit_record
         # Phase 14 (wall-clock / timezone surface): OS-sourced civil time (ids 44-48).
         teko_rt_time_now_unix teko_rt_time_now_local teko_rt_time_now_utc
         teko_rt_time_format_local teko_rt_time_format_utc
         # Phase 15 (15.A): object instance-store ops (OP_OBJ_* import these from the reactor).
         teko_rt_object_new teko_rt_object_set teko_rt_object_get teko_rt_object_free
         # Phase 18 (18.E.1): fixed-size array ops (OP_ARR_* import these; get/set trap on OOB).
         teko_rt_array_new teko_rt_array_get teko_rt_array_set teko_rt_array_len
         # Phase 18 (18.E.2): typed i32[] packed-array ops (OP_IARR_* import these; get/set trap on OOB).
         teko_rt_iarray_new teko_rt_iarray_get teko_rt_iarray_set teko_rt_iarray_len
         # Phase 18 (18.E.4): SIMD substrate access — data ptr (the run the in-module simd128 kernel
         # walks) + the scalar reference reduction (the honest fallback / self-check oracle).
         teko_rt_iarray_data teko_rt_iarray_sum
         # Phase 15 (15.B): static-vtable dispatch ops (OP_VTABLE_* import these from the reactor).
         teko_rt_vtable_set teko_rt_vtable_get
         # Phase 16 (16.A): culture-invariant conversion surface (OP_CALL_RUNTIME ids 49/51/52).
         teko_rt_int_to_string teko_rt_bool_to_string teko_rt_str_concat
         # Phase 17.D: float->string (id 50; the f64-arg reactor entry, (double)->char*).
         teko_rt_float_to_string
         # Phase 16.E: explicit integer formats (ids 56/57/58).
         teko_rt_to_radix teko_rt_pad teko_rt_group
         # Phase 17.E: checked string->f64 (id 54; the f64-RESULT reactor entry, (char*)->double;
         # traps on malformed/overflow input via teko_rt_die's __builtin_trap).
         teko_rt_parse_float
         # Phase 16.F: checked parse (ids 53/55; traps on malformed input).
         teko_rt_parse_int teko_rt_parse_bool
         # Phase 17.F.3: 256-byte decimal value-model ops (OP_D* import these; by-pointer ABI over
         # the SHARED linear memory — i32 slot offsets — exactly like the crypto hex-string ABI).
         teko_rt_decimal_add teko_rt_decimal_sub teko_rt_decimal_mul
         teko_rt_decimal_div teko_rt_decimal_mod teko_rt_decimal_cmp
         # Phase 17.F.4: int/float ↔ decimal casts + the decimal.to_string/parse surface (ids 59/60).
         teko_rt_decimal_from_i32 teko_rt_decimal_from_f64
         teko_rt_decimal_to_i32 teko_rt_decimal_to_f64
         teko_rt_decimal_to_string teko_rt_decimal_parse)
LDEXPORTS=(); for e in "${EXPORTS[@]}"; do LDEXPORTS+=("--export=$e"); done

# Layout: keep the whole reactor image (data + shadow stack + heap) ABOVE Teko's
# [0..65536) region so the two allocators never alias. --global-base=65536 places the
# DATA there; the stack must land after the data (not at address 0). Newer lld defaults to
# --stack-first (stack at [0..stack-size) — would collide with Teko) and offers
# --no-stack-first to opt out; older lld is already data-first and does NOT know that flag.
# Probe for it: pass it when supported, omit it otherwise (old default is what we want).
# -z stack-size=1MiB gives RSA bignum scratch room and works on every lld version.
STACK_FLAG=()
if "$WASMLD" --help 2>/dev/null | grep -q -- '--no-stack-first'; then
  STACK_FLAG=(--no-stack-first)
fi
"$WASMLD" --no-entry --import-memory "${STACK_FLAG[@]}" --global-base=65536 \
  -z stack-size=1048576 "${LDEXPORTS[@]}" "${OBJS[@]}" -o "$OUT"

echo "[crypto-reactor] wrote $OUT ($(wc -c < "$OUT") bytes)"
