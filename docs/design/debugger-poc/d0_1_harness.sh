#!/usr/bin/env sh
# docs/design/debugger-poc/d0_1_harness.sh — the D0.1 debugger harness.
#
# The crumb D0.1 of docs/design/debugger-superficie-e-contramedida-0.3.1.md (Peca 4, section 9.1):
# a debugger harness that READS the debugger's own output and ASSERTS the TEXT it printed, never a
# bare process exit code. Every assertion below prints "PASS <name>" or "FAIL <name>" after matching
# a STRING in gdb's stdout, so the verdict is the text a human reads, not a hidden status.
#
# It is BORN WITH A NEGATIVE PROOF, and that proof still asserts the DEFAULT (opt-in-not-taken)
# case: `--debug=lines` (D1.6) has since landed, but debug info stays OFF unless requested (a
# deliberate, documented choice — it grows the object and must never enter a release build by
# omission). So a Teko binary built WITHOUT the flag still carries no `.debug_*` sections, and
# `gdb` still finds no Teko source in it. The day the NEGATIVE assertion below starts failing is
# the day that default silently changed — a real regression, named by this harness.
#
# What this harness does NOT cover: the opt-in POSITIVE end-to-end path (`--debug=lines` on a
# real Teko-compiled native binary actually stopping in gdb/lldb). `.debug_line`/`.debug_info`/
# `.debug_abbrev` are written by the NATIVE object writers only (`src/backend/objfile_elf.tks`,
# `objfile_macho.tks`); the C route never touches them. Until the native backend reliably emits a
# runnable binary for arbitrary programs, that positive assertion cannot be added here without
# silently depending on the native backend's own health — a different, already-tracked concern.
#
# The POSITIVE section below is the section-5.2 defence (also F0.2 of
# docs/design/tdb-proposta-0.3.1.md §3.0): it asserts the backtrace depth THROUGH A FRAMELESS leaf
# (adv.s's `lvl4`), not a weak ">= N frames". A frameless function returns RSP exactly as it
# received it; if the unwind survives it with the CALLER frame (`lvl3`) recovered and named, the
# claim that unwinding needs CFI (red-flag 3) is refuted with no CFI in the object at all.
#
# The BOUNDARY section is F0.3 (same document, same section): a stack that crosses a PLATFORM
# LIBRARY frame in the MIDDLE (not just at the bottom, where mini.s/adv.s already show one
# incidentally via libc's own `_start`/`__libc_start_main`). It is the one case the first two
# fixtures do not cover, and the one a real `tdb` backtrace will hit constantly (any program that
# calls into libc and back). See `boundary.s` for the frame layout.
#
# Run from this directory. It is documentation that executes, not a CI gate: it needs `as`, `cc`,
# `gdb`, and — for the negative proof — the `teko` compiler on PATH (override with $TEKO). Every
# precondition failure (missing tool, missing compiler, a build that itself fails) is a LOUD
# FAIL naming what was missing — never a silent early exit, which is the exact defect
# `scripts/check_elf.sh`'s header warns against ("AN ABSENT OBJECT WAS A PASS").
set -u
cd "$(dirname "$0")"

TEKO="${TEKO:-teko}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

FAILED=0

pass_if_contains() {
    name="$1"
    needle="$2"
    haystack="$3"
    case "$haystack" in
        *"$needle"*) echo "PASS $name" ;;
        *) echo "FAIL $name (expected to read: $needle)"; FAILED=1 ;;
    esac
}

# require_tool NAME — a missing prerequisite is a named FAIL, never a silent early exit.
require_tool() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "FAIL prerequisite_$1 (not found on PATH)"
        FAILED=1
        return 1
    fi
    return 0
}

echo "=== D0.1 POSITIVE: frameless five-frame unwind, NO CFI (adv.s) ==="
if require_tool as && require_tool cc && require_tool gdb; then
    if as -g -o "$WORK/adv.o" adv.s && cc -o "$WORK/adv" "$WORK/adv.o" 2>/dev/null; then
        BT="$(gdb -batch -nx -ex 'break *lvl4' -ex run -ex bt "$WORK/adv" 2>&1)"
        pass_if_contains frameless_leaf_named   '#0  lvl4 () at hello.tks:30' "$BT"
        pass_if_contains caller_of_frameless    'in lvl3 () at hello.tks:42'  "$BT"
        pass_if_contains frameless_caller_hole  'in lvl2 () at hello.tks:20'  "$BT"
        pass_if_contains framed_by_alignment    'in lvl1 () at hello.tks:17'  "$BT"
        pass_if_contains bottom_frame_main      'in main () at hello.tks:41'  "$BT"
    else
        echo "FAIL adv_build (as/cc failed to produce $WORK/adv from adv.s)"
        FAILED=1
    fi
fi

echo "=== F0.3 BOUNDARY: a mid-stack platform-library frame is neither swallowed nor fabricated ==="
if require_tool as && require_tool cc && require_tool gdb; then
    if as -g -o "$WORK/boundary.o" boundary.s && cc -o "$WORK/boundary" "$WORK/boundary.o" 2>/dev/null; then
        BT="$(gdb -batch -nx -ex 'break leaf' -ex run -ex bt "$WORK/boundary" 2>&1)"
        pass_if_contains boundary_leaf_named   '#0  leaf () at hello.tks:30' "$BT"
        pass_if_contains boundary_b_named      'in b () at hello.tks:42'     "$BT"
        pass_if_contains boundary_cmp_named    'in cmp () at hello.tks:21'   "$BT"
        pass_if_contains boundary_resumes_at_a 'in a () at hello.tks:17'     "$BT"
        pass_if_contains boundary_bottom_main  'in main () at hello.tks:41' "$BT"
        # Between the comparator (our last frame before the boundary) and `a` (our first frame
        # after it) must sit ONE OR MORE library frames — and NONE of them may claim `hello.tks`,
        # which would mean the debugger fabricated a Teko frame for glibc's own qsort/msort code.
        MIDDLE="$(printf '%s\n' "$BT" | awk '/in cmp \(\) at hello\.tks:21/{f=1;next} /in a \(\) at hello\.tks:17/{f=0} f')"
        if [ -z "$MIDDLE" ]; then
            echo "FAIL boundary_frame_present (no frame recorded between cmp and a — cmp/a anchors did not both match)"
            FAILED=1
        else
            case "$MIDDLE" in
                *"hello.tks"*)
                    echo "FAIL boundary_no_fabrication (a library-side frame claimed hello.tks): $MIDDLE"
                    FAILED=1
                    ;;
                *) echo "PASS boundary_no_fabrication" ;;
            esac
        fi
    else
        echo "FAIL boundary_build (as/cc failed to produce $WORK/boundary from boundary.s)"
        FAILED=1
    fi
fi

echo "=== D0.1 NEGATIVE: a Teko binary built WITHOUT --debug=lines still carries no DWARF ==="
if require_tool gdb && require_tool "$TEKO"; then
    mkdir -p "$WORK/neg/src"
    cat > "$WORK/neg/teko.tkp" <<'TKP'
name = "neg"
source = "src"
version = "0.0.1"
suffix = "alpha"
description = "the born-negative fixture: a Teko binary carries no DWARF unless --debug=lines is passed"
[artifact]
kind = "binary"
TKP
    cat > "$WORK/neg/main.tks" <<'TKS'
teko::io::println("hi")
teko::exit(0)
TKS
    if "$TEKO" build "$WORK/neg" -o "$WORK/neg/out" --no-verify >"$WORK/neg_build.log" 2>&1; then
        NEG="$(gdb -batch -nx -ex 'info line main' "$WORK/neg/out/neg" 2>&1)"
        pass_if_contains no_teko_line_info 'No line number information available' "$NEG"
    else
        echo "FAIL neg_build ('\$TEKO build' itself failed — see $WORK/neg_build.log before it is removed)"
        cat "$WORK/neg_build.log" >&2
        FAILED=1
    fi
fi

exit "$FAILED"
