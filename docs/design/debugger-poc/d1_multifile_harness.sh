#!/usr/bin/env sh
# docs/design/debugger-poc/d1_multifile_harness.sh — the CROSS-FILE line-table harness.
#
# The continuation of D0.1 toward usable debug info: D0.1 asserted a single-file backtrace; this
# asserts that a backtrace resolves each frame to its OWN `.tks` source when the program spans more
# than one file. That is the piece a REAL Teko program needs and mini.s never exercised — a
# `.debug_line` file_names table with two entries plus the DW_LNS_set_file opcode, which
# src/backend/dwarf.tks now emits (line_header_body's file list + emit_line_row's file register).
#
# Like d0_1_harness.sh it READS the debugger's own output and ASSERTS the TEXT it printed, never a
# bare exit code. It ALSO asserts the two files gdb-independently, straight out of
# `readelf --debug-dump=decodedline`, so a broken file table is named even where gdb is absent.
#
# The object under test (multi.s) carries the EXACT three sections src/backend/dwarf.tks emits for
# this unit; keeping them next to the writer is what lets a byte drift in either be caught here.
#
# Run from this directory. It is documentation that executes, not a CI gate: it needs `as`, `cc`,
# `gdb`, and `readelf`.
set -e
cd "$(dirname "$0")"

pass_if_contains() {
    name="$1"
    needle="$2"
    haystack="$3"
    case "$haystack" in
        *"$needle"*) echo "PASS $name" ;;
        *) echo "FAIL $name (expected to read: $needle)" ;;
    esac
}

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

as -o "$WORK/multi.o" multi.s
cc -o "$WORK/multi" "$WORK/multi.o" 2>/dev/null

echo "=== D1.x GDB-INDEPENDENT: the decoded line table names BOTH files ==="
DECODED="$(readelf --debug-dump=decodedline "$WORK/multi.o" 2>&1)"
pass_if_contains line_table_file_util  'util.tks' "$DECODED"
pass_if_contains line_table_file_hello 'hello.tks' "$DECODED"

echo "=== D1.x POSITIVE: cross-file backtrace, each frame in its own .tks (multi.s) ==="
BT="$(gdb -batch -nx -ex 'break util.tks:18' -ex run -ex bt "$WORK/multi" 2>&1)"
pass_if_contains crossfile_callee_named 'add () at util.tks:18'  "$BT"
pass_if_contains crossfile_caller_named 'in main () at hello.tks:41' "$BT"
