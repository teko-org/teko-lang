#!/usr/bin/env sh
# docs/design/debugger-poc/d2_locals_harness.sh — the LOCALS/variables structural harness (D2.x).
#
# Continues the debugger floor from a backtrace to variable information. It READS the DWARF decoder's
# own output and ASSERTS the TEXT it printed — here from `readelf --debug-dump=info`, which is
# gdb-INDEPENDENT, so the DIE tree that names and locates parameters and locals is proved even where
# gdb is absent. The object under test (locals.s) carries the EXACT three sections src/backend/
# dwarf.tks emits (debug_abbrev_locals / debug_info_locals), so a byte drift in either is caught.
#
# The DW_OP_fbreg offsets and DW_AT_frame_base are PLACEHOLDERS (see locals.s): this asserts the
# STRUCTURE, not a live `print x`, which awaits the real frame layout being wired in.
#
# Run from this directory. It is documentation that executes, not a CI gate: it needs `as` and
# `readelf`.
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

as -o "$WORK/locals.o" locals.s
INFO="$(readelf --debug-dump=info "$WORK/locals.o" 2>&1)"

echo "=== D2.x STRUCTURE: the info DIE tree names and locates the variables ==="
pass_if_contains base_type_i32     'DW_TAG_base_type' "$INFO"
pass_if_contains subprogram_add    'DW_TAG_subprogram' "$INFO"
pass_if_contains frame_base_reg6   'DW_OP_reg6' "$INFO"
pass_if_contains param_dies        'DW_TAG_formal_parameter' "$INFO"
pass_if_contains variable_die      'DW_TAG_variable' "$INFO"
pass_if_contains lexical_block     'DW_TAG_lexical_block' "$INFO"
pass_if_contains fbreg_location    'DW_OP_fbreg' "$INFO"
pass_if_contains param_a_offset    'DW_OP_fbreg: -20' "$INFO"
pass_if_contains shadow_s_offset   'DW_OP_fbreg: -8' "$INFO"
