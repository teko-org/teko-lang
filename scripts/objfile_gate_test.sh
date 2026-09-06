#!/usr/bin/env bash
# scripts/objfile_gate_test.sh — proves the object/archive well-formedness gates FAIL when
# they have nothing to check.
#
# WHY THIS FILE EXISTS. Every one of the six gates — check_{elf,macho,coff}.sh and
# check_ar_{elf,macho,coff}.sh — used to `exit 0` when the object was absent, the host was
# wrong, or the tool was missing. CI reads exit 0 as PASS, so a producer that never wrote its
# .o scored a green row, and `check_elf.sh` on an aarch64 host reported PASS having validated
# nothing at all (owner, 2026-07-29: "check_elf.sh é defeito de CI, precisa corrigir então").
# The gates are now fail-closed; THIS script is what keeps them that way. Without it, one
# well-meant "let's be lenient in the sandbox" edit silently reopens the hole.
#
# THE PROOF IS BY INVERSION, both directions:
#   · with nothing to check and no opt-out  → the gate must exit NONZERO;
#   · with the documented opt-out armed     → the same call must exit ZERO.
# Asserting only the first direction would also pass against a gate that is broken and always
# fails; asserting both pins the actual behaviour.
#
# PORTABLE TO ALL SIX HOSTS BY CONSTRUCTION: these cases need no compiler, no binutils and no
# LLVM — they only ever hand a gate a path that does not exist. That is deliberate. A gate test
# that itself needed the toolchain could not run where the toolchain is what's missing, which
# is the exact case being defended. (The POSITIVE direction — a real object accepted — is
# proven by target_host_default_test.sh on the hosts where an object gets built.)
#
# NO GNU-ONLY TOOL SPELLINGS. `sed -E`, `sed -r` and `grep -P` are absent from Git-Bash on the
# Windows runner; one `sed -E` in a sibling gate cost this lane an opaque exit 127. A fixture
# below scans scripts/ for them so the next one is caught here rather than on Windows.
#
# usage: bash scripts/objfile_gate_test.sh
#   (invoke with `bash`, NOT `sh` — `sh scripts/...` ignores the shebang and drops to POSIX mode)

set -u

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$script_dir"

pass_count=0
fail_count=0

ok() { echo "  ok   — $1"; pass_count=$((pass_count + 1)); }
no() { echo "  FAIL — $1" >&2; fail_count=$((fail_count + 1)); }

OBJ_GATES="check_elf check_macho check_coff"
AR_GATES="check_ar_elf check_ar_macho check_ar_coff"

MISSING="$script_dir/this-object-was-never-written.o"
[[ -e "$MISSING" ]] && { echo "objfile_gate_test: FATAL — the deliberately-absent path exists" >&2; exit 2; }

echo "objfile_gate_test: running on $(uname -s)-$(uname -m)"

echo "1) a gate with NO argument must fail closed"
for g in $OBJ_GATES $AR_GATES; do
    if ./scripts/"$g".sh >/dev/null 2>&1; then
        no "$g.sh returned 0 with no argument — a gate that passes with nothing to check"
    else
        ok "$g.sh fails closed with no argument"
    fi
done

echo "2) a gate pointed at a NON-EXISTENT object must fail closed"
for g in $OBJ_GATES $AR_GATES; do
    if ./scripts/"$g".sh "$MISSING" >/dev/null 2>&1; then
        no "$g.sh returned 0 for an object that does not exist"
    else
        ok "$g.sh fails closed on a missing object"
    fi
done

echo "3) INVERSION — with its documented opt-out armed, the same call must return 0"
for g in $OBJ_GATES; do
    if OBJ_CHECK_ALLOW_SKIP=1 ./scripts/"$g".sh "$MISSING" >/dev/null 2>&1; then
        ok "$g.sh still honours OBJ_CHECK_ALLOW_SKIP=1"
    else
        no "$g.sh ignores OBJ_CHECK_ALLOW_SKIP=1 — the escape hatch is broken, so case 2 proves nothing"
    fi
done
for g in $AR_GATES; do
    if AR_CHECK_REQUIRE_TOOLS=0 ./scripts/"$g".sh "$MISSING" >/dev/null 2>&1; then
        ok "$g.sh still honours AR_CHECK_REQUIRE_TOOLS=0"
    else
        no "$g.sh ignores AR_CHECK_REQUIRE_TOOLS=0 — the escape hatch is broken, so case 2 proves nothing"
    fi
done

echo "4) the fail-closed DEFAULT is pinned in the source (a flip back must turn this red)"
for g in $OBJ_GATES; do
    if grep -q 'OBJ_CHECK_ALLOW_SKIP:-0' "scripts/$g.sh"; then
        ok "$g.sh defaults to fail-closed"
    else
        no "$g.sh no longer defaults OBJ_CHECK_ALLOW_SKIP to 0"
    fi
done
for g in $AR_GATES; do
    if grep -q 'AR_CHECK_REQUIRE_TOOLS:-1' "scripts/$g.sh"; then
        ok "$g.sh defaults to fail-closed"
    else
        no "$g.sh no longer defaults AR_CHECK_REQUIRE_TOOLS to 1"
    fi
done

echo "5) check_elf.sh must know AArch64, not just x86-64"
if grep -q 'AArch64' scripts/check_elf.sh; then
    ok "check_elf.sh carries an AArch64 e_machine expectation"
else
    no "check_elf.sh lost its AArch64 e_machine expectation — an arm64 ELF leg cannot be validated"
fi
if grep -q 'uname -m' scripts/check_elf.sh; then
    ok "check_elf.sh still knows the host architecture (its no-argument fallback)"
else
    no "check_elf.sh no longer consults the host architecture"
fi

echo "5b) check_elf.sh's expected machine must be an ARGUMENT, not only the host's architecture"
if grep -q 'expected_arch_of' scripts/check_elf.sh; then
    ok "check_elf.sh derives its expectation from the os-arch it is handed"
else
    no "check_elf.sh takes no expected os-arch — a cross-emitted object would be judged against the HOST's arch: a false red for an arm64-linux object on x86_64, and a hole in the other direction"
fi
# THE FIXTURE NEEDS NO TOOLCHAIN AND NO OBJECT, by the same construction as cases 1-3: it hands
# the gate the deliberately-absent path with the opt-out armed, so what it pins is that the
# os-arch argument is PARSED AND MAPPED at all — an argument the script could not map would
# reach skip_or_fail before the object check and, with the opt-out armed, still exit 0, so the
# assertion is on the mapping surviving, not on the exit alone. The POSITIVE direction — a real
# AArch64 object accepted on an x86_64 host — is proven by the own_cross_arm64_linux_emits_elf
# regression row, which builds one.
for want in arm64-linux aarch64-linux x86_64-linux linux; do
    if OBJ_CHECK_ALLOW_SKIP=1 ./scripts/check_elf.sh "$MISSING" "$want" >/dev/null 2>&1; then
        ok "check_elf.sh accepts the expected-os-arch argument '$want'"
    else
        no "check_elf.sh rejected the expected-os-arch argument '$want' outright"
    fi
done

echo "6) target_host_default_test.sh must route every host, and shout when it cannot"
if grep -q 'no well-formedness checker routed' scripts/target_host_default_test.sh; then
    ok "the unrouted-host fall-through is a hard failure"
else
    no "the unrouted-host fall-through no longer fails — an unvalidated object would read as PASS"
fi

# WHAT AN INVOCATION LOOKS LIKE, AND WHY LESS PRECISION LIES. Three false positives were
# measured on this fixture's first runs, and each one rules out a cruder scan:
#   · PROSE IN A COMMENT — several gates name the forbidden spellings in the very doc-comment
#     explaining why they are forbidden. Comment-only lines are stripped first.
#   · PROSE IN A STRING — known_stop_gate_test.sh prints "(no sed -E/-r, no grep -P)" as its
#     own pass message. Stripping comments does not reach that, so the tool name must sit in
#     COMMAND POSITION: at line start or right after a `|`, `;`, `&` or `(`.
#   · A DIFFERENT TOOL — `pgrep -P` contains the substring `grep -P`, and
#     native_linux_asset.sh legitimately uses pgrep's parent-pid flag. Command-position
#     anchoring excludes it, since what follows the `(` there is `pgrep`, not `grep`.
# Writing the patterns with a bracket (`-[Er]`, `-[P]`) also stops this file matching itself,
# so the scan covers every script including this one with no self-exclusion to go stale.
#
# HONEST LIMIT: an invocation buried mid-command (`xargs sed -E …`) is not command position and
# would slip through. The shape that actually cost this lane an opaque exit 127 was a plain
# top-level call, and that is the shape pinned here. `grep -E` is itself POSIX and safe.
echo "7) no GNU-only tool spellings in scripts/ (Git-Bash on Windows has none of them)"
cmd_start='(^|[|;&(])[[:space:]]*'
bad_sed_ere="${cmd_start}sed -[Er]"
bad_grep_ere="${cmd_start}grep -[P]"
offenders=""
for f in scripts/*.sh; do
    code="$(grep -v '^[[:space:]]*#' "$f")"
    if printf '%s\n' "$code" | grep -qE -e "$bad_sed_ere" -e "$bad_grep_ere"; then
        offenders="$offenders $f"
    fi
done
if [[ -z "$offenders" ]]; then
    ok "no GNU-only sed/grep spellings in any scripts/*.sh command position"
else
    no "GNU-only tool spellings found in:$offenders — these break on the Windows runner"
fi

echo "objfile_gate_test: $pass_count passed, $fail_count failed"
if [[ "$fail_count" -ne 0 ]]; then
    echo "objfile_gate_test: FAIL" >&2
    exit 1
fi
echo "objfile_gate_test: PASS"
exit 0
