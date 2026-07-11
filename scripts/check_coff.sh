#!/usr/bin/env bash
# scripts/check_coff.sh — host-tool well-formedness gate for an own-backend PE/COFF
# object (issue #388 B3-4, the COFF sibling of check_elf.sh / check_macho.sh). The
# own-backend COFF writer (`src/backend/objfile_coff.tks::emit_coff`) produces an
# `IMAGE_FILE_MACHINE_AMD64` relocatable object; this script asserts the LLVM
# toolchain accepts it — `llvm-readobj --file-headers --sections --symbols
# --relocations` parses the header/sections/symbols/relocations (the format-generic
# flags, stable across llvm-readobj versions and cross-format on COFF), `llvm-objdump
# -d` disassembles `.text`, and `lld-link` (with `/force:unresolved` so an unresolved
# `tk_exit` — the executable link is the diff_c_own.sh windows lane, not this check —
# never masquerades as a malformed object) confirms the object is linker-acceptable.
# The engine-agnostic byte layout is pinned by the golden tests in
# `src/backend/objfile_coff_test.tkt`; this script is the external cross-check.
#
# CROSS-FORMAT BY DESIGN (unlike check_elf/check_macho, which gate on their native
# host): `llvm-readobj` / `llvm-objdump` / `lld-link` parse a COFF object on ANY host,
# so this script runs on macOS-arm64 and linux-x86_64 too — the machine-free proof the
# emitted bytes are well-formed even where the PE cannot EXECUTE (the executing
# C-vs-own differential is the windows-x86_64 runner, diff_c_own.sh). It honest-skips
# (exit 0) when no object is provided, the object is absent, or `llvm-readobj` is not
# installed — the byte goldens in `teko test .` are the fallback proof there.
#
# usage: scripts/check_coff.sh <object.o>

set -u

OBJ="${1:-}"
if [[ -z "$OBJ" || ! -f "$OBJ" ]]; then
    echo "check_coff: skipped — no object provided (the diff_c_own.sh windows lane writes the .o; goldens pin bytes)"
    exit 0
fi

READOBJ="${LLVM_READOBJ:-llvm-readobj}"
OBJDUMP="${LLVM_OBJDUMP:-llvm-objdump}"
LLD_LINK="${LLD_LINK:-lld-link}"

if ! command -v "$READOBJ" >/dev/null 2>&1; then
    echo "check_coff: skipped — needs llvm-readobj (cross-format COFF parser); not found on $(uname -s)-$(uname -m)"
    exit 0
fi

fail() { echo "check_coff: FAIL — $1"; exit 1; }

hdr="$("$READOBJ" --file-headers "$OBJ" 2>/dev/null)" || fail "$READOBJ could not parse $OBJ"
echo "$hdr" | grep -q "IMAGE_FILE_MACHINE_AMD64" || fail "not an IMAGE_FILE_MACHINE_AMD64 object"

"$READOBJ" --sections "$OBJ" 2>/dev/null | grep -q "Name: .text" || fail "missing .text section"

"$READOBJ" --symbols "$OBJ" 2>/dev/null | grep -q "Section: .text" || fail "no defined .text symbol in $OBJ"

"$READOBJ" --relocations "$OBJ" 2>/dev/null | grep -q "IMAGE_REL_AMD64_REL32" || fail "no IMAGE_REL_AMD64_REL32 relocation in $OBJ"

if command -v "$OBJDUMP" >/dev/null 2>&1; then
    "$OBJDUMP" -d "$OBJ" 2>/dev/null | grep -q "Disassembly of section .text" || fail "llvm-objdump -d found no .text disassembly in $OBJ"
fi

if command -v "$LLD_LINK" >/dev/null 2>&1; then
    echo "check_coff: $("$LLD_LINK" --version 2>&1 | head -1)"
    tmp_exe="$(mktemp -u "${TMPDIR:-/tmp}/check-coff.XXXXXX").exe"
    # lld-link is a NATIVE Windows tool using single-leading-slash switches
    # (/nologo, /entry:, /subsystem:, /force:). Invoked from a Git-Bash/MSYS shell
    # (the windows-selfhost CI lane), MSYS's automatic POSIX-to-Windows path
    # conversion mangles any argument that LOOKS like a POSIX absolute path — which
    # every one of these switches does (a bare leading `/`) — before lld-link ever
    # sees it, turning `/nologo` into a bogus filesystem path and making a
    # perfectly well-formed object look "rejected". MSYS_NO_PATHCONV (Git-Bash) and
    # MSYS2_ARG_CONV_EXCL="*" (MSYS2) disable that conversion for this one invocation.
    # BUT disabling conversion also stops the FILE-PATH args ($OBJ, /out:) from being
    # translated — a POSIX `/tmp/…` path the native lld-link.exe cannot open. So convert
    # the file paths to Windows form explicitly with `cygpath -w` (present only under
    # MSYS/Cygwin; absent on macOS/Linux, where the paths pass through and the host
    # lld-link accepts POSIX). Net: /switches reach lld-link intact AND the files open.
    obj_arg="$OBJ"; out_arg="$tmp_exe"
    if command -v cygpath >/dev/null 2>&1; then
        obj_arg="$(cygpath -w "$OBJ")"
        out_arg="$(cygpath -w "$tmp_exe")"
    fi
    link_out="$(MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' "$LLD_LINK" /nologo "/out:$out_arg" /entry:main /subsystem:console /force:unresolved "$obj_arg" 2>&1)"
    link_rc=$?
    rm -f "$tmp_exe"
    if [[ "$link_rc" -ne 0 ]]; then
        echo "check_coff: lld-link rejected $OBJ (rc=$link_rc) — full output below:"
        echo "$link_out" | sed 's/^/      | /'
        fail "lld-link rejected $OBJ (not linker-consumable)"
    fi
fi

echo "check_coff: OK — $OBJ is a well-formed, linker-consumable IMAGE_FILE_MACHINE_AMD64 PE/COFF object"
exit 0
