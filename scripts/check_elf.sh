#!/usr/bin/env bash
# scripts/check_elf.sh — host-tool well-formedness gate for an own-backend ELF64
# object (issue #386 B1-7/B1-8). The own-backend ELF writers
# (`src/backend/objfile_elf.tks::emit_elf` for x86-64, `::emit_elf_arm64` for
# AArch64 — both feeding the shared, ISA-agnostic `emit_elf_object`) produce a
# relocatable `ET_REL` object; this script asserts a toolchain accepts it — the ELF
# parser reads the header/sections/symbols, the symbol lister finds a defined text
# symbol, the disassembler decodes `.text`, and a relocatable relink (`-r`, NOT an
# executable link) consumes it. The engine-agnostic byte layout is pinned by the
# golden tests in `src/backend/objfile_elf_test.tkt`; this script is the external
# cross-check, the ELF sibling of `scripts/check_macho.sh`.
#
# THREE DEFECTS CLOSED, EACH BY THE SAME PRINCIPLE: THE EXPECTATION IS AN ARGUMENT,
# NOT AN ASSUMPTION ABOUT THE HOST.
#
#   1. IT REFUSED AARCH64 (closed 2026-07-29, owner: "check_elf.sh é defeito de CI,
#      precisa corrigir"). The script gated on `uname -m == x86_64` and then asserted
#      the literal string `X86-64`, so on an aarch64 Linux host it printed "skipped"
#      and returned 0 — WHICH THE CI READS AS PASS.
#
#   2. AN ABSENT OBJECT WAS A PASS (closed 2026-07-29). `no object provided`/`file
#      missing` returned 0, so a producer that failed to write the .o scored a green
#      gate. Under the trunk bar — "só vai ao tronco se passar pelo CI, sem erros,
#      alertas ou mesmo erros escondidos (que não disparam)" — a gate that passes
#      when its input is absent is precisely a hidden error. Every skip is a HARD
#      FAILURE by default.
#
#   3. THE MACHINE EXPECTATION WAS STILL THE HOST'S, WHICH IS THE WRONG QUESTION FOR
#      A CROSS BUILD. Deriving `e_machine` from `uname -m` closed defect 1 but left a
#      second hole one dimension over: an object emitted FOR `arm64-linux` ON an
#      x86_64 host is correct AArch64 and the host-derived expectation would call it
#      "not an X86-64 object" — a FALSE RED — while the mirror case (an x86_64 host
#      accidentally emitting arm64 bytes for a `x86_64-linux` row) is what the check
#      exists to catch. The expected os-arch is therefore an ARGUMENT (`$2`), so the
#      assertion is PARAMETRIC on what the build was asked to emit; the host is only
#      the fallback for a caller that declares nothing.
#
# THE TOOLSET FOLLOWS THE OBJECT'S MACHINE, MEASURED RATHER THAN ASSUMED. GNU
# binutils are only partly cross-capable, and the split was measured on an x86_64
# host against a `clang --target=aarch64-linux-gnu` object:
#
#   readelf -h/-S   AArch64 object parsed fine        → cross-capable
#   nm              symbols listed fine               → cross-capable
#   objdump -d      "can't disassemble for architecture UNKNOWN!"   → NOT cross-capable
#   ld -r           "Relocations in generic ELF (EM: 183)"          → NOT cross-capable
#   llvm-objdump -d disassembled AArch64 correctly     → cross-capable
#   ld.lld -r       exit 0                             → cross-capable
#
# So a FOREIGN-machine object is inspected with the LLVM toolset and a NATIVE one
# keeps the GNU toolset it has always used — the native path's proof is unchanged
# byte for byte, and the cross path gets tools that can actually answer.
#
# SKIPPING IS NOT THIS SCRIPT'S JOB — SCHEDULING IS THE CALLER'S. `não agendada ≠
# saltada`: reaching a missing-tool branch here means the caller scheduled a check
# this host cannot perform, and that is a defect to shout about, not to absorb.
# `OBJ_CHECK_ALLOW_SKIP=1` restores the old lenient behaviour for a local sandbox
# that genuinely lacks the toolchain; CI never sets it.
#
# usage: scripts/check_elf.sh <object.o> [expected-os-arch]
#   expected-os-arch         e.g. "arm64-linux", "x86_64-linux", "aarch64-linux";
#                            an OS-only token ("linux") or an empty argument means
#                            "this host's own architecture".
#   OBJ_CHECK_ALLOW_SKIP=1   (default: unset) — downgrade a hard failure to an honest skip.

set -u

allow_skip="${OBJ_CHECK_ALLOW_SKIP:-0}"

fail() { echo "check_elf: FAIL — $1" >&2; exit 1; }

# Absent tools/objects are failures unless the caller explicitly opted out.
skip_or_fail() {
    if [[ "$allow_skip" == "1" ]]; then
        echo "check_elf: skipped (OBJ_CHECK_ALLOW_SKIP=1) — $1"
        exit 0
    fi
    fail "$1 — a gate that passes with nothing to check is a hidden error; set OBJ_CHECK_ALLOW_SKIP=1 only in a local sandbox"
}

# One CPU spelled one way, folding `uname -m`'s vocabulary and this project's
# canonical os-arch names onto the same token (the shell twin of
# `arch_token_canonical` in src/build/regression.tks).
canonical_arch() {
    case "$1" in
        aarch64|arm64) echo "arm64" ;;
        x86_64|amd64|x64) echo "x86_64" ;;
        *) echo "$1" ;;
    esac
}

# The architecture the object under test is EXPECTED to carry: the arch half of an
# os-arch name, else this host's own. A token with no `-` (the effective target of a
# row that declared none, which is a bare OS name) carries no architecture, so the
# host answers.
expected_arch_of() {
    case "$1" in
        *-*) canonical_arch "${1%%-*}" ;;
        *) canonical_arch "$(uname -m)" ;;
    esac
}

# The `readelf -h` machine string an object of `$1` must carry.
elf_machine_pattern() {
    case "$1" in
        x86_64) echo "X86-64" ;;
        arm64) echo "AArch64" ;;
        *) echo "" ;;
    esac
}

OBJ="${1:-}"
if [[ -z "$OBJ" ]]; then
    skip_or_fail "no object argument given (arg1 empty)"
fi
if [[ ! -f "$OBJ" ]]; then
    skip_or_fail "the object '$OBJ' does not exist — its producer did not write it"
fi

EXPECTED_TARGET="${2:-}"
EXPECTED_ARCH="$(expected_arch_of "$EXPECTED_TARGET")"
HOST_ARCH="$(canonical_arch "$(uname -m)")"

MACHINE_PATTERN="$(elf_machine_pattern "$EXPECTED_ARCH")"
if [[ -z "$MACHINE_PATTERN" ]]; then
    skip_or_fail "no expected ELF e_machine mapped for architecture '$EXPECTED_ARCH' (target '$EXPECTED_TARGET' on $(uname -s)-$(uname -m)) — teach elf_machine_pattern this architecture"
fi

# The first candidate on PATH, or "" when none is. Naming CANDIDATES rather than one
# tool is what lets the cross path work on a host that spells its LLVM binaries
# differently — macOS's `/usr/bin/objdump` IS llvm-objdump, and a host with LLVM
# installed but not first on PATH still has `llvm-objdump`.
first_on_path() {
    for cand in "$@"; do
        [[ -n "$cand" ]] || continue
        if command -v "$cand" >/dev/null 2>&1; then
            echo "$cand"
            return 0
        fi
    done
    echo ""
}

# The disassembler and the relinker do NOT share the parser's cross-capability: GNU's
# pair refuses a foreign `e_machine`, LLVM's pair accepts it. Selecting on the
# OBJECT's machine rather than on the host is what makes a cross check possible at
# all — and what keeps the native check's proof exactly the one it has always been.
if [[ "$EXPECTED_ARCH" == "$HOST_ARCH" && "$(uname -s)" == "Linux" ]]; then
    READELF="readelf"
    NM="nm"
    OBJDUMP="objdump"
    LD="ld"
    TOOLSET="host binutils"
else
    # THE PARSER AND THE SYMBOL LISTER ALSO GO THROUGH THE CANDIDATE LIST, and this is a
    # correction. They used to be hardcoded `readelf`/`nm` above this block, under the comment
    # *"cross-capable in both toolchains, so they are the same call on every path"*. The
    # capability claim is true; the SPELLING claim was not. A Darwin host has no bare `readelf`
    # at all — Homebrew's LLVM installs `llvm-readelf` — so the cross path died naming a tool
    # that never exists there, 883 seconds into the corpus. Measured, run 30568806559:
    #
    #   check_elf: FAIL — the cross-capable LLVM tool 'readelf' — needed to inspect an arm64
    #   object on Darwin-arm64 — is absent
    #
    # Apple's `/usr/bin/nm` is likewise Mach-O-shaped; `llvm-nm` is the one that reads a foreign
    # ELF. The native-Linux arm keeps binutils verbatim, so the proof that path has always had
    # does not move by a byte.
    READELF="$(first_on_path "${LLVM_READELF:-}" llvm-readelf readelf)"
    NM="$(first_on_path "${LLVM_NM:-}" llvm-nm nm)"
    OBJDUMP="$(first_on_path "${LLVM_OBJDUMP:-}" llvm-objdump)"
    LD="$(first_on_path "${LLVM_LD:-}" ld.lld ld64.lld)"
    TOOLSET="cross-capable LLVM"
fi

# A tool the FOREIGN path needs and this host does not have is a SCHEDULING defect,
# not a property of the object: the caller asked a host without an ELF toolchain for
# that machine to judge one. It fails loudly and names the tool, because the
# alternative — reporting green over a disassembly and a relink that never ran — is
# exactly the hidden error this gate was rewritten to remove.
for tool in "$READELF" "$NM" "$OBJDUMP" "$LD"; do
    if [[ -z "$tool" ]]; then
        skip_or_fail "no $TOOLSET disassembler/relinker for an $EXPECTED_ARCH object exists on $(uname -s)-$(uname -m) (looked for llvm-objdump / ld.lld) — install LLVM's lld+objdump on this host, or do not route a cross-ELF check here"
    fi
    command -v "$tool" >/dev/null 2>&1 || skip_or_fail "the $TOOLSET tool '$tool' — needed to inspect an $EXPECTED_ARCH object on $(uname -s)-$(uname -m) — is absent"
done

hdr="$("$READELF" -h "$OBJ" 2>/dev/null)" || fail "$READELF could not parse $OBJ"
echo "$hdr" | grep -q "ELF64"             || fail "not an ELF64 object"
echo "$hdr" | grep -qi "REL"              || fail "not an ET_REL (relocatable) object"
echo "$hdr" | grep -qi "$MACHINE_PATTERN"  || fail "not an $MACHINE_PATTERN object (expected target '$EXPECTED_TARGET' → $EXPECTED_ARCH; an object for the wrong architecture reached the checker)"

"$READELF" -S "$OBJ" 2>/dev/null | grep -q "\.text"      || fail "missing .text section"
"$READELF" -S "$OBJ" 2>/dev/null | grep -q "\.symtab"    || fail "missing .symtab section"

"$NM" "$OBJ" 2>/dev/null | grep -qE "^[0-9a-f]+ [Tt] " || fail "no defined text symbol in $OBJ"

"$OBJDUMP" -d "$OBJ" 2>/dev/null | grep -q "<.*>:" || fail "$OBJDUMP -d found no disassembled function in $OBJ"

"$LD" -r "$OBJ" -o /dev/null 2>/dev/null || fail "$LD -r rejected $OBJ (not linker-consumable)"

echo "check_elf: OK — $OBJ is a well-formed, linker-consumable $MACHINE_PATTERN ET_REL ELF64 object (checked with the $TOOLSET toolset)"
exit 0
