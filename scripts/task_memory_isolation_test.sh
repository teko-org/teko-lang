#!/usr/bin/env bash
# scripts/task_memory_isolation_test.sh — driver for the F1/F2 guard.
#
# Compiles scripts/task_memory_isolation_test.c (which #includes src/runtime/teko_rt.c directly —
# teko_rt.c is self-contained and main()-less, see its own header) and runs it. The harness proves
# the per-task memory discipline in three arms — LIVENESS first (two tasks really are two), then
# the cross-task corruption canary, then the REVERSAL that shreds the same canary under one shared
# root — plus F2's program region in both directions. See the .c file's own header for the full
# rationale (Teko has no raw-pointer surface, so this cannot be expressed as a `.tkt` test).
#
# Usage: scripts/task_memory_isolation_test.sh [cc]
#   cc   the C compiler to use (default: cc)
set -eu

CC="${1:-cc}"
SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
WORK="$(mktemp -d 2>/dev/null || mktemp -d -t taskmemtest)"
trap 'rm -rf "$WORK"' EXIT INT TERM

BIN="$WORK/task_memory_isolation_test"
"$CC" -std=c2x -Wall -Wextra -o "$BIN" "$SCRIPT_DIR/task_memory_isolation_test.c"
"$BIN"
