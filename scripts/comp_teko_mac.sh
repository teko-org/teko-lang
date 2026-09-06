#!/usr/bin/env sh
# comp_teko_mac.sh — link a usable `teko` compiler from the committed bootstrap/teko.c on macOS.
#
# Usage:  scripts/comp_teko_mac.sh [out_path]
#   out_path  where to write the binary (default: <repo>/teko)
# Env:
#   CC            C compiler to use (default: clang — Apple clang from Xcode CLT)
#   CFLAGS_EXTRA  extra flags appended after the fixed ones
set -eu

ROOT="$(CDPATH= cd "$(dirname "$0")/.." && pwd)"
CC="${CC:-clang}"
OUT="${1:-$ROOT/teko}"

TEKO_C="$ROOT/bootstrap/teko.c"
[ -f "$TEKO_C" ] || { echo "comp_teko_mac: missing $TEKO_C" >&2; exit 1; }

VER="$(sh "$ROOT/scripts/derive_version.sh" 2>/dev/null || echo v0.0.0.0-dev)"
VER="${VER#v}"

echo "comp_teko_mac: CC=$CC -> $OUT (version $VER)"
"$CC" --version 2>/dev/null | head -n1 || true

"$CC" -std=c2x -w -O2 -pthread -DTEKO_VERSION_STRING="$VER" ${CFLAGS_EXTRA:-} \
    -I"$ROOT/src/runtime" -I"$ROOT/src/assert" \
    "$TEKO_C" "$ROOT/src/runtime/teko_rt.c" "$ROOT/src/assert/assert.c" \
    -o "$OUT"

[ -x "$OUT" ] || { echo "comp_teko_mac: link succeeded but $OUT is not executable" >&2; exit 1; }
echo "comp_teko_mac: wrote $OUT"
