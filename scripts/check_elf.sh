#!/usr/bin/env bash
# scripts/check_elf.sh — host-tool well-formedness gate for an own-backend ELF64
# object (issue #386 B1-7/B1-8 for x86-64; issue #387 B2-8 for riscv64). The
# own-backend ELF writer (`src/backend/objfile_elf.tks::emit_elf` /
# `objfile_elf_riscv.tks::emit_elf_riscv` → the shared `emit_elf_object`) produces
# a relocatable `ET_REL` object; this script asserts the HOST toolchain accepts it
# — `readelf` parses the header/sections/symbols/relocations, `objdump -d`
# disassembles `.text`, and `ld -r` (a relocatable relink, NOT an executable link —
# that is the diff_c_own.sh executing lane) consumes it. The engine-agnostic byte
# layout is pinned by the golden tests in `src/backend/objfile_elf_test.tkt` /
# `objfile_elf_riscv_test.tkt`; this script is the external cross-check, the ELF
# sibling of `scripts/check_macho.sh`.
#
# TARGET PARAMETRIZATION (issue #387 B2-8): the same script consumes an x86-64 OR a
# riscv64 object. It defaults to the x86-64 toolchain (empty tool prefix, EM_X86_64);
# the riscv lane exports:
#   ELF_TOOLCHAIN=riscv64-linux-gnu-   (the cross-binutils prefix: readelf/objdump/nm/ld)
#   ELF_MACHINE="RISC-V"               (the `readelf -h` machine string to assert)
# so a linux-x86_64 CI host cross-checks a riscv64 object with the riscv binutils it
# installs alongside qemu.
#
# GATED on linux-x86_64 (needs the host `readelf`/`objdump`/`ld` binutils, native or
# cross-prefixed, reachable); on any other host the script HONEST-SKIPS with a named
# reason (exit 0). With no argument, or no object at the path, it also honest-skips.
#
# usage: scripts/check_elf.sh <object.o>

set -u

if [[ "$(uname -s)" != "Linux" || "$(uname -m)" != "x86_64" ]]; then
    echo "check_elf: skipped — needs a linux-x86_64 host (readelf/objdump/ld) on $(uname -s)-$(uname -m)"
    exit 0
fi

OBJ="${1:-}"
if [[ -z "$OBJ" || ! -f "$OBJ" ]]; then
    echo "check_elf: skipped — no object provided (the diff_c_own.sh ELF lane writes the .o; goldens pin bytes)"
    exit 0
fi

ELF_TOOLCHAIN="${ELF_TOOLCHAIN:-}"
ELF_MACHINE="${ELF_MACHINE:-X86-64}"
READELF="${ELF_TOOLCHAIN}readelf"
OBJDUMP="${ELF_TOOLCHAIN}objdump"
NM="${ELF_TOOLCHAIN}nm"
LD="${ELF_TOOLCHAIN}ld"

fail() { echo "check_elf: FAIL — $1"; exit 1; }

hdr="$("$READELF" -h "$OBJ" 2>/dev/null)" || fail "$READELF could not parse $OBJ"
echo "$hdr" | grep -q "ELF64"          || fail "not an ELF64 object"
echo "$hdr" | grep -qi "REL"           || fail "not an ET_REL (relocatable) object"
echo "$hdr" | grep -qi "$ELF_MACHINE"  || fail "not an EM_${ELF_MACHINE} object"

"$READELF" -S "$OBJ" 2>/dev/null | grep -q "\.text"      || fail "missing .text section"
"$READELF" -S "$OBJ" 2>/dev/null | grep -q "\.symtab"    || fail "missing .symtab section"

"$NM" "$OBJ" 2>/dev/null | grep -qE "^[0-9a-f]+ [Tt] " || fail "no defined text symbol in $OBJ"

"$OBJDUMP" -d "$OBJ" 2>/dev/null | grep -q "<.*>:" || fail "objdump -d found no disassembled function in $OBJ"

"$LD" -r "$OBJ" -o /dev/null 2>/dev/null || fail "ld -r rejected $OBJ (not linker-consumable)"

echo "check_elf: OK — $OBJ is a well-formed, linker-consumable ${ELF_MACHINE} ET_REL ELF64 object"
exit 0
