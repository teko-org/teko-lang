#!/usr/bin/env bash
# scripts/check_macho.sh — host-tool well-formedness gate for an A4 Mach-O object
# (issue #385, A4-4). The own-backend Mach-O writer (`src/backend/objfile_macho.tks`,
# `emit_macho`) produces a relocatable `MH_OBJECT` for arm64; this script asserts the
# HOST toolchain accepts it — `otool` parses the header/load-commands, `nm` lists the
# symbols, and `ld -r` (a relocatable relink, NOT an executable link — that is A4-5)
# consumes it. The engine-agnostic byte layout is pinned by the golden tests in
# `src/backend/objfile_macho_test.tkt`; this script is the external cross-check.
#
# FAIL-CLOSED SINCE 2026-07-29, in step with its ELF sibling. An absent object, an absent
# tool or the wrong host used to return 0, which the CI reads as PASS — a gate that passes
# with nothing to check is exactly the "erro escondido que não dispara" the trunk bar
# forbids. Scheduling belongs to the CALLER (target_host_default_test.sh routes each host to
# its own format's checker), so reaching one of those branches means the caller routed
# wrong. `OBJ_CHECK_ALLOW_SKIP=1` restores lenience for a local sandbox; CI never sets it.
#
# KNOWN COVERAGE GAP, NOT A SCRIPT DEFECT: `ld -r` here only sees whatever object the caller
# built, and target_host_default_test.sh deliberately builds the simplest possible body
# (`exit(42)`) because the own AOT backend honest-stops on richer builtins. A body that
# trivial contains no ADRP/ADD address pair, so this gate stayed green while the arm64
# address-pair relocation bug shipped and only surfaced when the own_native corpus linked.
# The corpus is the wider net; this script is the narrow, fast one.
#
# usage: scripts/check_macho.sh <object.o>
#   OBJ_CHECK_ALLOW_SKIP=1   (default: unset) — downgrade a hard failure to an honest skip.

set -u

allow_skip="${OBJ_CHECK_ALLOW_SKIP:-0}"

fail() { echo "check_macho: FAIL — $1" >&2; exit 1; }

skip_or_fail() {
    if [[ "$allow_skip" == "1" ]]; then
        echo "check_macho: skipped (OBJ_CHECK_ALLOW_SKIP=1) — $1"
        exit 0
    fi
    fail "$1 — a gate that passes with nothing to check is a hidden error; set OBJ_CHECK_ALLOW_SKIP=1 only in a local sandbox"
}

if [[ "$(uname)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
    skip_or_fail "called on $(uname)-$(uname -m), which does not produce arm64 Mach-O objects (the caller routed the wrong checker)"
fi

OBJ="${1:-}"
if [[ -z "$OBJ" ]]; then
    skip_or_fail "no object argument given (arg1 empty)"
fi
if [[ ! -f "$OBJ" ]]; then
    skip_or_fail "the object '$OBJ' does not exist — its producer did not write it"
fi

for tool in otool nm ld; do
    command -v "$tool" >/dev/null 2>&1 || skip_or_fail "the host tool '$tool' is absent on $(uname)-$(uname -m)"
done

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
