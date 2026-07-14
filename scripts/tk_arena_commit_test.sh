#!/usr/bin/env bash
# scripts/tk_arena_commit_test.sh — driver for the tk_arena_commit unit test (enabling primitive).
#
# Compiles scripts/tk_arena_commit_test.c (which #includes src/runtime/teko_rt.c directly — see
# region_drop_subtree_test.sh for the identical convention) and runs it. The harness asserts the
# C-level effect of tk_arena_commit directly (bump-offset movement, committed-byte survival) — see
# the .c file's own header for the full rationale (Teko has no raw-pointer surface, so this cannot
# be expressed as a `.tkt` test without reading freed memory, which would be undefined behavior).
#
# Usage: scripts/tk_arena_commit_test.sh [cc]
#   cc   the C compiler to use (default: cc)
set -eu

CC="${1:-cc}"
SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
WORK="$(mktemp -d 2>/dev/null || mktemp -d -t arenacommittest)"
trap 'rm -rf "$WORK"' EXIT INT TERM

BIN="$WORK/tk_arena_commit_test"
"$CC" -std=c2x -Wall -Wextra -o "$BIN" "$SCRIPT_DIR/tk_arena_commit_test.c"
"$BIN"
