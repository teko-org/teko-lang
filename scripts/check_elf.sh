#!/usr/bin/env bash
# scripts/check_elf.sh — host-tool well-formedness gate for a B1 ELF64 object
# (issue #386, B1-7/B1-8). The own-backend ELF writer (`src/backend/objfile_elf.tks`,
# `emit_elf`) produces a relocatable `ET_REL` object for x86-64; this script asserts
# the HOST toolchain accepts it — `readelf` parses the header/sections/symbols/
# relocations, `objdump -d` disassembles `.text`, and `ld -r` (a relocatable relink,
# NOT an executable link — that is the diff_c_own.sh executing lane) consumes it. The
# engine-agnostic byte layout is pinned by the golden tests in
# `src/backend/objfile_elf_test.tkt`; this script is the external cross-check, the
# ELF sibling of `scripts/check_macho.sh`.
#
# GATED on linux-x86_64 (needs the host `readelf`/`objdump`/`ld` binutils reachable);
# on any other host the script HONEST-SKIPS with a named reason (exit 0). With no
# argument, or no object at the path, it also honest-skips.
#
# usage: scripts/check_elf.sh <object.o>

set -u

if [[ "$(uname -s)" != "Linux" || "$(uname -m)" != "x86_64" ]]; then
    echo "check_elf: skipped — needs a linux-x86_64 host (readelf/objdump/ld) on $(uname -s)-$(uname -m)"
    exit 0
fi

OBJ="${1:-}"
if [[ -z "$OBJ" || ! -f "$OBJ" ]]; then
    echo "check_elf: skipped — no object provided (the diff_c_own.sh x86 lane writes the .o; B1-7 pins bytes via golden tests)"
    exit 0
fi

fail() { echo "check_elf: FAIL — $1"; exit 1; }

hdr="$(readelf -h "$OBJ" 2>/dev/null)" || fail "readelf could not parse $OBJ"
echo "$hdr" | grep -q "ELF64"       || fail "not an ELF64 object"
echo "$hdr" | grep -qi "REL"        || fail "not an ET_REL (relocatable) object"
echo "$hdr" | grep -qi "X86-64"     || fail "not an EM_X86_64 object"

readelf -S "$OBJ" 2>/dev/null | grep -q "\.text"      || fail "missing .text section"
readelf -S "$OBJ" 2>/dev/null | grep -q "\.symtab"    || fail "missing .symtab section"

nm "$OBJ" 2>/dev/null | grep -qE "^[0-9a-f]+ [Tt] " || fail "no defined text symbol in $OBJ"

objdump -d "$OBJ" 2>/dev/null | grep -q "<.*>:" || fail "objdump -d found no disassembled function in $OBJ"

ld -r "$OBJ" -o /dev/null 2>/dev/null || fail "ld -r rejected $OBJ (not linker-consumable)"

echo "check_elf: OK — $OBJ is a well-formed, linker-consumable x86-64 ET_REL ELF64 object"
exit 0
