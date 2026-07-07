#!/usr/bin/env bash
# scripts/region_drop_subtree_test.sh — driver for the tk_region_drop_subtree unit test (#337).
#
# Compiles scripts/region_drop_subtree_test.c (which #includes src/runtime/teko_rt.c directly —
# teko_rt.c is self-contained and main()-less, see its own header) and runs it. The harness
# asserts the C-level effect of the `adopt` bulk-drop primitive directly (obs region/byte
# counters, registry membership) — see the .c file's own header for the full rationale (Teko has
# no raw-pointer surface, so this cannot be expressed as a `.tkt` test).
#
# Usage: scripts/region_drop_subtree_test.sh [cc]
#   cc   the C compiler to use (default: cc)
set -eu

CC="${1:-cc}"
SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
WORK="$(mktemp -d 2>/dev/null || mktemp -d -t regiondroptest)"
trap 'rm -rf "$WORK"' EXIT INT TERM

BIN="$WORK/region_drop_subtree_test"
"$CC" -std=c2x -Wall -Wextra -o "$BIN" "$SCRIPT_DIR/region_drop_subtree_test.c"
"$BIN"
