#!/usr/bin/env bash
# scripts/install_share_runtime_test.sh — regression harness for issue #157.
#
# Proves install.sh's from-release path stages the native-build runtime under
# share/teko, and that the INSTALLED binary — invoked from a directory with NO
# relation to any teko-lang checkout, with TK_RT_DIR unset — can still `teko build`
# a real project fixture and produce a working native binary.
#
# The real GitHub release is not reachable/mutable from a PR branch, so this harness
# stands up a THROWAWAY local "release": scripts/package_release.sh packages the
# already-built LABEL binary + this checkout's src/ into a teko-<label>.tar.gz +
# teko-bootstrap-src.tar.gz + SHA256SUMS.txt, served over a local HTTP server. A
# scratch copy of install.sh points REPO_URL at that server (the only line rewritten;
# every other behavior — checksum verification, share-dir staging, fallback logging —
# runs UNMODIFIED).
#
# Usage: scripts/install_share_runtime_test.sh <teko-binary> [label]
#   TEKO_BINARY  the compiler to package as the fake release asset (e.g. ./bin/teko)
#   LABEL        release asset label (default: derived from `uname`)
set -eu

TEKO_BINARY="${1:?usage: install_share_runtime_test.sh <teko-binary> [label]}"
LABEL="${2:-}"

fail() { echo "install_share_runtime_test: FAIL: $*" >&2; exit 1; }
info() { echo "install_share_runtime_test: $*" >&2; }

SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
REPO_ROOT="$(CDPATH='' cd -- "$SCRIPT_DIR/.." && pwd)"
[ -f "$TEKO_BINARY" ] || fail "no such binary: $TEKO_BINARY"
TEKO_BINARY="$(CDPATH='' cd -- "$(dirname -- "$TEKO_BINARY")" && pwd)/$(basename -- "$TEKO_BINARY")"

derive_label() {
    case "$(uname -s)-$(uname -m)" in
        Darwin-arm64)          echo "macos-arm64" ;;
        Darwin-x86_64)         echo "macos-x86_64" ;;
        Linux-x86_64|Linux-amd64) echo "linux-x86_64" ;;
        Linux-aarch64|Linux-arm64) echo "linux-arm64" ;;
        *) fail "cannot derive a release label for $(uname -s)-$(uname -m); pass one explicitly" ;;
    esac
}
[ -n "$LABEL" ] || LABEL="$(derive_label)"

WORK="$(mktemp -d 2>/dev/null || mktemp -d -t issue157test)"
cleanup() {
    [ -n "${SERVER_PID:-}" ] && kill "$SERVER_PID" >/dev/null 2>&1 || true
    rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

# ── stage a throwaway "gen-3-like" dir + package it as a fake release ────────────
GEN_DIR="$WORK/gen"
mkdir -p "$GEN_DIR"
cp "$TEKO_BINARY" "$GEN_DIR/teko"
( cd "$REPO_ROOT" && "$TEKO_BINARY" . -o "$GEN_DIR/emit" --no-verify --release >/dev/null 2>&1 ) || true
[ -f "$GEN_DIR/emit/teko.c" ] && cp "$GEN_DIR/emit/teko.c" "$GEN_DIR/teko.c"
[ -f "$GEN_DIR/teko.c" ] || fail "could not obtain a gen teko.c to package (unexpected build failure)"

DIST="$WORK/dist"
mkdir -p "$DIST"
EMIT_SRC_BUNDLE=1 sh "$REPO_ROOT/scripts/package_release.sh" "$LABEL" "$GEN_DIR" "$REPO_ROOT/src" "$DIST" posix >&2
( cd "$DIST" && cat SHA256SUMS-*.txt | sort -u > SHA256SUMS.txt )

TAG="v0.0.0-issue157-test"
SERVER_ROOT="$WORK/server/releases/download/$TAG"
mkdir -p "$SERVER_ROOT"
cp "$DIST"/*.tar.gz "$DIST/SHA256SUMS.txt" "$SERVER_ROOT/"

# ── serve the fake release over a local HTTP port ────────────────────────────────
PORT=8934
pick_free_port() {
    p=$PORT
    while lsof -i ":$p" >/dev/null 2>&1; do p=$((p + 1)); done
    printf '%s' "$p"
}
have() { command -v "$1" >/dev/null 2>&1; }
if have lsof; then PORT="$(pick_free_port)"; fi

# `exec` replaces the subshell with python3 itself (same PID) so `kill "$SERVER_PID"`
# in cleanup() actually reaches the server — a plain `cmd &` subshell would otherwise
# keep a parent shell alive as SERVER_PID with python3 as an orphan-prone child.
( cd "$WORK/server" && exec python3 -m http.server "$PORT" >"$WORK/http.log" 2>&1 ) &
SERVER_PID=$!
wait_for_server() {
    tries=0
    while ! curl -fsS "http://127.0.0.1:$PORT/releases/download/$TAG/SHA256SUMS.txt" >/dev/null 2>&1; do
        tries=$((tries + 1))
        [ "$tries" -lt 50 ] || fail "local release server never came up"
        sleep 0.2
    done
}
wait_for_server

# ── a scratch install.sh with REPO_URL pointed at the local server ──────────────
# SC2016: the single-quoted pattern is install.sh's LITERAL source line (not meant to
# expand) — only the replacement (double-quoted, `$PORT` interpolated) is dynamic.
FAKE_INSTALL="$WORK/install.sh"
# shellcheck disable=SC2016
sed 's#REPO_URL="https://github.com/${REPO}"#REPO_URL="http://127.0.0.1:'"$PORT"'"#' \
    "$REPO_ROOT/install.sh" > "$FAKE_INSTALL"

# ── run the installer into an isolated HOME + prefix mirroring ~/.local/bin ──────
FAKE_HOME="$WORK/home"
INSTALL_PREFIX="$FAKE_HOME/.local/bin"
mkdir -p "$FAKE_HOME"
HOME="$FAKE_HOME" sh "$FAKE_INSTALL" --version "$TAG" --prefix "$INSTALL_PREFIX" >&2

INSTALLED_BIN="$INSTALL_PREFIX/teko"
[ -x "$INSTALLED_BIN" ] || fail "install.sh did not produce an executable at $INSTALLED_BIN"

SHARE_DIR="$FAKE_HOME/.local/share/teko"
[ -f "$SHARE_DIR/runtime/teko_rt.h" ] || fail "share dir missing runtime/teko_rt.h: $SHARE_DIR"
[ -f "$SHARE_DIR/runtime/teko_rt.c" ] || fail "share dir missing runtime/teko_rt.c: $SHARE_DIR"
[ -f "$SHARE_DIR/assert/assert.c" ]   || fail "share dir missing assert/assert.c: $SHARE_DIR"
[ -f "$SHARE_DIR/win32_compat.h" ]    || fail "share dir missing win32_compat.h: $SHARE_DIR"
info "share dir staged correctly: $SHARE_DIR"

# ── the acceptance test: build a REAL fixture from an isolated cwd, TK_RT_DIR unset ──
FIXTURE_SRC="$REPO_ROOT/examples/regressions/exit_success_path"
[ -d "$FIXTURE_SRC" ] || fail "missing fixture: $FIXTURE_SRC"
FIXTURE="$WORK/fixture"
cp -R "$FIXTURE_SRC" "$FIXTURE"
rm -rf "${FIXTURE:?}/out" "${FIXTURE:?}/bin"

BUILD_LOG="$WORK/build.log"
if ! ( cd "$FIXTURE" && env -i HOME="$FAKE_HOME" PATH="/usr/bin:/bin" "$INSTALLED_BIN" . -o out --no-verify --release >"$BUILD_LOG" 2>&1 ); then
    cat "$BUILD_LOG" >&2
    fail "the installed binary could not build a project outside a checkout (issue #157 regressed)"
fi

BIN_OUT="$FIXTURE/out/exit_success_path"
[ -x "$BIN_OUT" ] || fail "build reported success but produced no executable: $BIN_OUT"

set +e
"$BIN_OUT"
RC=$?
set -e
[ "$RC" -eq 5 ] || fail "installed-binary build exit code $RC (want 5 — the fixture's documented exit)"

info "PASS: install.sh staged share/teko and the installed binary built+ran a real project outside any checkout"
