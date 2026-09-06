#!/usr/bin/env bash
# scripts/check_ar_macho.sh — host-tool well-formedness gate for the BSD-format `.a`
# static archive (`src/backend/objfile_ar_macho.tks::emit_static_archive_macho`) — the
# Mach-O sibling of `scripts/check_ar_elf.sh`. Asserts the REAL macOS toolchain accepts
# the archive: `ar t` lists the member, `nm -a` sees the `__.SYMDEF SORTED`-indexed
# symbols, and `ld -r -arch <host-arch>` (ld64 needs the explicit `-arch` GNU `ld`
# infers on its own) relinks the extracted REAL object member — not the `__.SYMDEF
# SORTED` index member `ar t` lists first. The engine-agnostic byte layout is
# pinned by the golden tests in `src/backend/objfile_ar_macho_test.tkt`; this script is
# the external cross-check, run on pr.yml's `ar-validation` macos-latest
# leg (real Apple `ar`/`nm`/`ld` — the ONE toolchain that can validate a BSD
# `__.SYMDEF SORTED` table). Ported from the `theory/kp16-ar-macho-coff` validation
# branch (run d44f63c9 = SUCCESS) once real-toolchain validation proved the writer.
#
# FAIL LOUD (M.3, owner ruling on run 30061110006): every check TRACES to stderr before it
# runs and prints the underlying tool's OWN stdout+stderr on failure — a silent "exit 1,
# no output" gate is never acceptable. `AR_CHECK_REQUIRE_TOOLS=1` turns an honest-skip
# (host/tool absent, or no archive given) into a HARD FAILURE instead of exit 0 — set this
# on a runner where the toolchain is guaranteed present (pr.yml's `ar-validation` macos leg), so a
# provisioning gap can never masquerade as a silent pass.
#
# usage: scripts/check_ar_macho.sh <archive.a> [symbol_that_must_resolve]
#   AR_CHECK_REQUIRE_TOOLS=1   (default: unset) — an honest-skip becomes a hard failure.

set -u

trace() { echo "check_ar_macho: $*" >&2; }
fail() { echo "check_ar_macho: FAIL — $1" >&2; exit 1; }

# FAIL-CLOSED BY DEFAULT SINCE 2026-07-29. The seam existed but NOBODY ARMED IT: pr.yml
# called this gate without AR_CHECK_REQUIRE_TOOLS=1, so a missing archive or a missing
# binutils would have scored a green row having validated nothing. Defaulting to 1 makes the
# safe behaviour the one you get by forgetting; AR_CHECK_REQUIRE_TOOLS=0 is now the explicit
# opt-out for a local sandbox that genuinely lacks the toolchain.
require="${AR_CHECK_REQUIRE_TOOLS:-1}"

skip_or_fail() {
    local reason="$1"
    if [[ "$require" == "1" ]]; then
        fail "AR_CHECK_REQUIRE_TOOLS=1 but $reason — failing closed (this mode never honest-skips)"
    fi
    trace "skipped — $reason"
    exit 0
}

trace "starting on $(uname -s)-$(uname -m), AR_CHECK_REQUIRE_TOOLS=$require"

if [[ "$(uname -s)" != "Darwin" ]]; then
    skip_or_fail "needs a macOS host (ar/nm/ld) on $(uname -s)"
fi

ARCHIVE="${1:-}"
if [[ -z "$ARCHIVE" || ! -f "$ARCHIVE" ]]; then
    skip_or_fail "no archive provided (arg1='${ARCHIVE:-<empty>}')"
fi

# Absolutize BEFORE any cd (run 30074280021, macos-arm64: the ar-x-into-a-tempdir step
# below `cd`s into a scratch dir, so a RELATIVE $ARCHIVE — the common case, a caller
# passing a repo-relative path — would silently resolve against the wrong directory
# there and "ar: ...: No such file or directory". A relative $1 is otherwise valid input
# (the well-formedness checks above run BEFORE any cd, so they never hit this).
ARCHIVE="$(cd "$(dirname "$ARCHIVE")" && pwd)/$(basename "$ARCHIVE")"

SYMBOL="${2:-}"
trace "checking archive=$ARCHIVE symbol=${SYMBOL:-<none>}"

trace "step: global header magic"
magic_hex="$(head -c 8 "$ARCHIVE" | od -An -tx1 | tr -d ' \n')"
[[ "$magic_hex" == "213c617263683e0a" ]] || fail "$ARCHIVE has no '!<arch>\\n' global header (got 0x$magic_hex)"

trace "step: ar t (list members)"
ar_t_out="$(ar t "$ARCHIVE" 2>&1)"
ar_t_rc=$?
if [[ "$ar_t_rc" -ne 0 ]]; then
    trace "ar t output:"; printf '%s\n' "$ar_t_out" | sed 's/^/      | /' >&2
    fail "ar t rejected $ARCHIVE (rc=$ar_t_rc)"
fi
# The REAL object member, not `ar t`'s first listed entry (run 30074603155: on a BSD
# archive `__.SYMDEF SORTED` — the synthetic symbol-table member `emit_bsd_ar_archive`
# always writes FIRST — is that first entry, not the real relocatable object; `ld -r`
# needs the actual `.o` member. Every real member this writer ever produces is named
# `"<stem>.o"` (`emit_static_archive_macho`'s sole caller), so the first `*.o`-suffixed
# entry IS the real member, in every member order the writer could ever emit.
member="$(printf '%s\n' "$ar_t_out" | grep '\.o$' | head -1)"
[[ -n "$member" ]] || fail "ar t listed no *.o object member in $ARCHIVE (only: $(printf '%s' "$ar_t_out" | tr '\n' ' '))"
trace "ar t: object member = '$member'"

trace "step: nm -a (symbol index)"
nm_out="$(nm -a "$ARCHIVE" 2>&1)"
nm_rc=$?
if [[ "$nm_rc" -ne 0 ]]; then
    trace "nm -a output:"; printf '%s\n' "$nm_out" | sed 's/^/      | /' >&2
    fail "nm -a could not read $ARCHIVE (rc=$nm_rc)"
fi

if [[ -n "$SYMBOL" ]]; then
    trace "step: symbol index lists '$SYMBOL' (whole word, not substring)"
    # A plain `grep -q "$SYMBOL"` is a SUBSTRING match anywhere on the line — it silently PASSED
    # when the own-native mangler leaked a `teko_` prefix into the archived symbol
    # (`teko_ar_link_run__add` CONTAINS `ar_link_run__add`), a "gate that does not gate" that
    # only the later real-linker step caught. Anchored to (start-of-line|whitespace) on the left
    # and (whitespace|end-of-line) on the right — nm's own column boundaries — so a longer
    # identifier merely containing `$SYMBOL` can never satisfy it. The ONE allowed prefix is a
    # SINGLE leading `_`: Mach-O's own C-symbol decoration (`_ar_link_run__add`), which every
    # object this writer emits carries legitimately and which `$SYMBOL` (the bare Teko-facing
    # name) never itself includes.
    sym_pattern="(^|[[:space:]])_?${SYMBOL}([[:space:]]|\$)"
    if ! printf '%s\n' "$nm_out" | grep -Eq "$sym_pattern"; then
        trace "nm -a output:"; printf '%s\n' "$nm_out" | sed 's/^/      | /' >&2
        fail "the archive symbol index does not list '$SYMBOL' as a whole word (with an optional Mach-O leading underscore)"
    fi
fi

# ld64's `-r` relocatable-link mode cannot infer the target architecture from the
# object alone (run 30074603155: "Missing -arch option") the way GNU `ld -r` does on
# linux — Apple's `ld` always needs an explicit `-arch`. `uname -m` on Darwin already
# returns ld64's own arch name verbatim (`arm64` on Apple Silicon, `x86_64` on Intel),
# so no translation table is needed; this is Darwin-only (the script has already
# skip_or_fail'd on any non-Darwin host above) — GNU `ld -r` (check_ar_elf.sh's own
# relink step) takes no such flag and must stay untouched.
arch="$(uname -m)"
trace "step: ar x (extract '$member') + ld -r -arch $arch (relocatable relink)"
tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/check-ar-macho.XXXXXX")"
ar_x_out="$(cd "$tmp_dir" && ar x "$ARCHIVE" "$member" 2>&1)"
ar_x_rc=$?
extracted="$tmp_dir/$member"
if [[ "$ar_x_rc" -ne 0 || ! -f "$extracted" ]]; then
    trace "ar x output:"; printf '%s\n' "$ar_x_out" | sed 's/^/      | /' >&2
    fail "ar x could not extract member $member from $ARCHIVE (rc=$ar_x_rc)"
fi
ld_out="$(ld -r -arch "$arch" "$extracted" -o /dev/null 2>&1)"
ld_rc=$?
rm -rf "$tmp_dir"
if [[ "$ld_rc" -ne 0 ]]; then
    trace "ld -r output:"; printf '%s\n' "$ld_out" | sed 's/^/      | /' >&2
    fail "ld -r -arch $arch rejected the extracted member $member (rc=$ld_rc)"
fi

echo "check_ar_macho: OK — $ARCHIVE is a well-formed, ar/nm/ld-consumable BSD static archive"
exit 0
