#!/usr/bin/env bash
# scripts/check_macho.sh — host-tool well-formedness gate for an A4 Mach-O object
# (issue #385, A4-4). The own-backend Mach-O writer (`src/backend/objfile_macho.tks`,
# `emit_macho`) produces a relocatable `MH_OBJECT` for arm64; this script asserts the
# HOST toolchain accepts it — `otool` parses the header/load-commands, `nm` lists the
# symbols, and `ld -r` (a relocatable relink, NOT an executable link — that is A4-5)
# consumes it. The engine-agnostic byte layout is pinned by the golden tests in
# `src/backend/objfile_macho_test.tkt`; this script is the external cross-check.
#
# A4-4 has no in-project object producer yet (writing bytes to a file is the A4-5
# `emit_native` path); pass an object PATH built by A4-5 (or a dev dump). With no
# argument, or off macOS-arm64, the script HONEST-SKIPS with a named reason.
#
# usage: scripts/check_macho.sh <object.o>

set -u

if [[ "$(uname)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
    echo "check_macho: skipped — needs a macOS-arm64 host (otool/nm/ld) on $(uname)-$(uname -m)"
    exit 0
fi

OBJ="${1:-}"
if [[ -z "$OBJ" || ! -f "$OBJ" ]]; then
    echo "check_macho: skipped — no object provided (the A4-5 emit_native path writes the .o; A4-4 pins bytes via golden tests)"
    exit 0
fi

fail() { echo "check_macho: FAIL — $1"; exit 1; }

hdr="$(otool -hv "$OBJ" 2>/dev/null)" || fail "otool could not parse $OBJ"
echo "$hdr" | grep -q "ARM64"  || fail "not an ARM64 object"
echo "$hdr" | grep -q "OBJECT" || fail "not an MH_OBJECT (relocatable)"

otool -l "$OBJ" 2>/dev/null | grep -q "LC_SYMTAB"       || fail "missing LC_SYMTAB"
otool -l "$OBJ" 2>/dev/null | grep -q "LC_DYSYMTAB"     || fail "missing LC_DYSYMTAB"
otool -l "$OBJ" 2>/dev/null | grep -q "LC_BUILD_VERSION" || fail "missing LC_BUILD_VERSION"
otool -l "$OBJ" 2>/dev/null | grep -q "__text"          || fail "missing __text section"

nm "$OBJ" 2>/dev/null | grep -qE "^[0-9a-f]+ [Tt] " || fail "no defined text symbol in $OBJ"

ld -r "$OBJ" -o /dev/null 2>/dev/null || fail "ld -r rejected $OBJ (not linker-consumable)"

echo "check_macho: OK — $OBJ is a well-formed, linker-consumable arm64 MH_OBJECT"
exit 0
