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
SRCS=("$HERE/libc_shim.c" "$ROOT/runtime/native/teko_rt.c" "$ROOT/src/runtime/teko_uuid.c")
for f in "$ROOT"/src/runtime/teko_crypto_*.c; do SRCS+=("$f"); done

OBJS=()
for f in "${SRCS[@]}"; do
  o="$WORK/$(basename "$f").o"
  "$CLANG" "${CFLAGS[@]}" -c "$f" -o "$o"
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
         teko_rt_rsa_oaep_encrypt teko_rt_rsa_oaep_decrypt)
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
