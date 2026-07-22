#!/usr/bin/env sh
# scripts/package_release.sh — assemble a Teko bootstrap RELEASE bundle for one platform.
#
# This is the packaging half of .github/workflows/release.yml. It runs AFTER the
# workflow has already produced the self-host fixpoint (gen-1 -> gen-2, with
# gen-1 == gen-2 asserted byte-identical) and been handed the gen-2 outputs. It emits,
# into $OUT_DIR:
#
#   1. teko-<LABEL>.tar.gz  (or .zip on Windows) — the per-platform gen-2 `teko` binary.
#   2. teko-bootstrap-src.tar.gz — the PORTABLE, any-cc bootstrap snapshot: the generated
#      gen-2 `teko.c` plus the EXACT minimal set of runtime/assert sources it links, in
#      the directory layout their #includes require. Same on every platform, so the
#      workflow builds it ONCE (on the source-of-truth runner) and publishes it once.
#   3. SHA256SUMS-<LABEL>.txt — checksums of every artifact this invocation produced.
#
# ── PROVEN minimal standalone-bootstrap file set ────────────────────────────────────
# A from-scratch `cc teko.c ... -o teko` needs exactly (verified by building + running it):
#   teko.c                 the gen-2 generated program (this is what gets published)
#   runtime/teko_rt.c      the execution runtime (tk_str, tk_print, tk_panic_*, arena, …)
#   runtime/teko_rt.h      its header — teko.c does #include "teko_rt.h"
#   assert/assert.c        the teko::assert seed (test assertions the corpus references)
#   assert/assert.h        its header — teko.c does #include "assert.h"
#   win32_compat.h         POSIX->Win32 shims; #ifdef _WIN32 no-op on POSIX, but
#                          runtime/teko_rt.c does #include "../win32_compat.h" so it must
#                          be present at the bundle root for a Windows `cc` to find it.
# assert/assert.h does #include "../runtime/teko_rt.h", so runtime/ and assert/ MUST be
# siblings and win32_compat.h MUST sit at the bundle root (teko_rt.c reaches it via ../).
# Build line (POSIX):   cc -std=c23 -Iruntime -Iassert teko.c runtime/teko_rt.c assert/assert.c -lm -o teko
#
# Usage:
#   package_release.sh <LABEL> <GEN2_DIR> <SRC_DIR> <OUT_DIR> [os]
#     LABEL     platform label, e.g. linux-x86_64 / macos-arm64 / windows-x86_64
#     GEN2_DIR  directory holding the gen-2 outputs (teko binary + teko.c)
#     SRC_DIR   the repo's src/ directory (source of teko_rt.*, assert.*, win32_compat.h)
#     OUT_DIR   where to write the archives + checksums
#     os        optional: "windows" selects .zip + teko.exe; anything else = tar.gz + teko
#
# POSIX sh only — no bashisms — so it runs identically on the Linux/macOS runners.
set -eu

LABEL="${1:?usage: package_release.sh LABEL GEN3_DIR SRC_DIR OUT_DIR [os]}"
GEN3_DIR="${2:?missing GEN3_DIR}"
SRC_DIR="${3:?missing SRC_DIR}"
OUT_DIR="${4:?missing OUT_DIR}"
OS="${5:-posix}"

mkdir -p "$OUT_DIR"

# Resolve the platform binary name (Windows carries the .exe suffix).
BIN_NAME="teko"
if [ "$OS" = "windows" ]; then
    BIN_NAME="teko.exe"
fi
BIN_PATH="$GEN3_DIR/$BIN_NAME"

if [ ! -f "$BIN_PATH" ]; then
    echo "package_release: gen-2 binary not found: $BIN_PATH" >&2
    exit 1
fi
if [ ! -f "$GEN3_DIR/teko.c" ]; then
    echo "package_release: gen-2 teko.c not found: $GEN3_DIR/teko.c" >&2
    exit 1
fi

# ── 1. per-platform gen-2 binary archive ────────────────────────────────────────────
BIN_ARCHIVE="teko-${LABEL}"
STAGE="$OUT_DIR/.stage-bin"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$BIN_PATH" "$STAGE/$BIN_NAME"

if [ "$OS" = "windows" ]; then
    # Git-Bash on the hosted Windows runners has NO `zip` binary (the first release run died
    # with exit 127 here); 7-Zip IS preinstalled. Prefer zip when present (local setups),
    # else 7z, else PowerShell Compress-Archive as the last resort.
    if command -v zip >/dev/null 2>&1; then
        ( cd "$STAGE" && zip -q -X "../${BIN_ARCHIVE}.zip" "$BIN_NAME" )
    elif command -v 7z >/dev/null 2>&1; then
        ( cd "$STAGE" && 7z a -tzip -bso0 -bsp0 "../${BIN_ARCHIVE}.zip" "$BIN_NAME" >/dev/null )
    else
        pwsh -NoProfile -Command "Compress-Archive -Path '$STAGE/$BIN_NAME' -DestinationPath '$OUT_DIR/${BIN_ARCHIVE}.zip' -Force"
    fi
    BIN_OUT="${BIN_ARCHIVE}.zip"
else
    tar -C "$STAGE" -czf "$OUT_DIR/${BIN_ARCHIVE}.tar.gz" "$BIN_NAME"
    BIN_OUT="${BIN_ARCHIVE}.tar.gz"
fi
rm -rf "$STAGE"
echo "package_release: wrote $OUT_DIR/$BIN_OUT"

# ── 2. portable bootstrap-source bundle (built once, gated by $EMIT_SRC_BUNDLE) ──────
# The workflow sets EMIT_SRC_BUNDLE=1 for exactly ONE platform (the teko.c is byte-
# identical across platforms — the self-host fixpoint — so it is published once).
SRC_OUT=""
if [ "${EMIT_SRC_BUNDLE:-0}" = "1" ]; then
    SRC_BUNDLE="teko-bootstrap-src"
    SSTAGE="$OUT_DIR/.stage-src/$SRC_BUNDLE"
    rm -rf "$OUT_DIR/.stage-src"
    mkdir -p "$SSTAGE/runtime" "$SSTAGE/assert"

    cp "$GEN3_DIR/teko.c"            "$SSTAGE/teko.c"
    cp "$SRC_DIR/runtime/teko_rt.c" "$SSTAGE/runtime/teko_rt.c"
    cp "$SRC_DIR/runtime/teko_rt.h" "$SSTAGE/runtime/teko_rt.h"
    cp "$SRC_DIR/assert/assert.c"   "$SSTAGE/assert/assert.c"
    cp "$SRC_DIR/assert/assert.h"   "$SSTAGE/assert/assert.h"
    cp "$SRC_DIR/win32_compat.h"    "$SSTAGE/win32_compat.h"

    # A ready-to-run build script inside the bundle (POSIX + a Windows note).
    cat > "$SSTAGE/build.sh" <<'EOF'
#!/usr/bin/env sh
# Build the Teko bootstrap compiler from this portable snapshot with any C23 cc.
# POSIX:   ./build.sh            (uses $CC or cc)
# Windows: clang -std=c23 -Iruntime -Iassert teko.c runtime/teko_rt.c assert/assert.c -o teko.exe
#          (MSVC is NOT supported — the runtime needs __int128 / GCC-Clang extensions.)
set -eu
CC="${CC:-cc}"
"$CC" -std=c23 -w -Iruntime -Iassert \
    teko.c runtime/teko_rt.c assert/assert.c \
    -lm -o teko
echo "built ./teko"
EOF
    chmod +x "$SSTAGE/build.sh"

    tar -C "$OUT_DIR/.stage-src" -czf "$OUT_DIR/${SRC_BUNDLE}.tar.gz" "$SRC_BUNDLE"
    rm -rf "$OUT_DIR/.stage-src"
    SRC_OUT="${SRC_BUNDLE}.tar.gz"
    echo "package_release: wrote $OUT_DIR/$SRC_OUT (portable bootstrap snapshot)"
fi

# ── 3. checksums for everything this invocation produced ─────────────────────────────
SUMS="SHA256SUMS-${LABEL}.txt"
(
    cd "$OUT_DIR"
    FILES="$BIN_OUT"
    [ -n "$SRC_OUT" ] && FILES="$FILES $SRC_OUT"
    if command -v sha256sum >/dev/null 2>&1; then
        # shellcheck disable=SC2086
        sha256sum $FILES > "$SUMS"
    else
        # macOS ships shasum, not sha256sum.
        # shellcheck disable=SC2086
        shasum -a 256 $FILES > "$SUMS"
    fi
)
echo "package_release: wrote $OUT_DIR/$SUMS"
