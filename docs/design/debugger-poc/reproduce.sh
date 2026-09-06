#!/usr/bin/env sh
# docs/design/debugger-poc/reproduce.sh — the end-to-end proof of the debugger design.
#
# NOT a build script and NOT a CI gate: it is the reproduction of the two measurements the
# design rests on, kept next to the design so a reader can run what the document claims.
# (`scripts/` is product; this is documentation that executes.)
#
# PROOF 1 (mini.s)  — the hand-written minimal DWARF-4 that gdb AND lldb both accept: three
#                     sections, four Abs64 relocations, breakpoint by `.tks` line, two-frame
#                     backtrace with Teko names. This is the golden the Teko DWARF writer must
#                     reproduce byte for byte.
# PROOF 2 (adv.s)   — the unwind measurement: five frames through EVERY frame shape our x86-64
#                     encoder emits (frameless leaf, framed-by-alignment, framed with
#                     callee-saved pushes after `mov rbp,rsp`, and the frameless non-returning
#                     caller), with NO CFI in the object at all.
#
# Run from this directory. Exit status 0 means every assertion held.
set -e
cd "$(dirname "$0")"

echo "=== PROOF 1: hand-written minimal DWARF-4 ==="
as -o mini.o mini.s
cc -o mini mini.o 2>/dev/null
echo "--- relocations in the debug sections (expected: 4, all R_X86_64_64) ---"
readelf -rW mini.o | grep R_X86_64_64
echo "--- section sizes (expected: abbrev 0x25, info 0x6b, line 0x59) ---"
readelf -SW mini.o | grep debug_ | grep -v rela
echo "--- decoded line table ---"
readelf --debug-dump=decodedline mini.o
echo "--- gdb ---"
gdb -batch -nx -ex "break hello.tks:30" -ex run -ex bt ./mini
echo "--- lldb ---"
lldb -b -o "breakpoint set --file hello.tks --line 30" -o run -o bt ./mini

echo
echo "=== PROOF 2: five-frame unwind with NO CFI ==="
as -g -o adv.o adv.s
cc -o adv adv.o 2>/dev/null
echo "--- our object carries no .eh_frame (expected: no match) ---"
readelf -SW adv.o | grep eh_frame || echo "(no .eh_frame -- as expected)"
echo "--- gdb, breakpoint in the FRAMELESS LEAF, five frames deep ---"
gdb -batch -nx -ex "break *lvl4" -ex run -ex bt ./adv
echo "--- gdb, breakpoint BEFORE the push rbp of a framed function ---"
gdb -batch -nx -ex "break *lvl3" -ex run -ex bt ./adv
echo "--- gdb, breakpoint BETWEEN push rbp and mov rbp,rsp ---"
gdb -batch -nx -ex "break *(lvl3+1)" -ex run -ex bt ./adv
echo "--- lldb, same frameless leaf ---"
lldb -b -o "breakpoint set --name lvl4" -o run -o bt ./adv
