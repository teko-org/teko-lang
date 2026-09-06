#!/usr/bin/env sh
# scripts/provenance_gate_test.sh — the REGRESSION for `scripts/provenance_gate.sh`, the gate that
# refuses to let `bootstrap/teko.c` be SWAPPED without a matching `bootstrap/PROVENANCE` record
# proving which CI run produced the new bytes.
#
# WHY THIS DESERVES A TEST OF ITS OWN, when the gate is only ever exercised against a real PR diff:
# the failure mode it closes is SILENT by nature — a hand-built C that happens to sit at the right
# path costs nothing to commit and nothing distinguishes it from a real harvest until something
# checks. A gate whose own verdict is unverified would be exactly that same blind trust, one layer
# up. Each load-bearing claim is proven here against throwaway fixtures:
#
#   1. no C at all — nothing to prove, PASS;
#   2. a C sitting outside any git working tree at all — FATAL (exit 2), undecidable;
#   3. a C present, but no base revision was given at all — FATAL (exit 2), never "assume
#      unchanged";
#   4. a base revision that does not resolve in this checkout — FATAL (exit 2);
#   5. `bootstrap/teko.c` UNCHANGED against the base — PASS, even with NO record at all: this is
#      the owner's literal reframing (2026-07-29) — "só podemos TROCAR o teko.c" — the gate has
#      nothing to judge about a file nobody is swapping;
#   6. `bootstrap/teko.c` CHANGED against the base with no record at all — REFUSED (exit 1): the
#      one case this gate actually exists to catch;
#   7. changed, but NOT tracked by git — REFUSED (exit 1), the sibling gate's own criterion
#      (`scripts/no_c_in_tests_gate.sh`) reused rather than re-derived;
#   8. changed, with a complete, matching, tracked record — PASS;
#   9. changed again after the record was written, without updating it — REFUSED (exit 1);
#  10. a record whose `sha256` was simply wrong from the start — REFUSED (exit 1);
#  11. a `run: unrecovered` legacy record — the ONLY sentinel this gate ever accepts, and only
#      for a file NOT being swapped: PASS when unchanged, REFUSED when it is a swap, by name;
#  12. every way a record can be malformed — a missing key, a `c:` that names the wrong file or
#      one that does not exist, an empty file — is FATAL (exit 2), never read as "no record".
#
# Each fixture is a throwaway git repository built in a temp dir (the same shape
# `scripts/no_c_in_tests_gate_test.sh` uses), so this needs no compiler and no network and runs in
# the `CI gate` job alongside `scripts/degrau_test.sh`, in under a second, on every PR.
#
# Usage:  sh scripts/provenance_gate_test.sh
# POSIX sh only.
set -eu

HERE="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
GATE="$HERE/provenance_gate.sh"

FAILED=0
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT INT TERM
ROOT="$WORK/repo"

pass() { printf '  ok   %s\n' "$1"; }
fail() { printf 'ERROR: %s\n' "$1" >&2; FAILED=1; }

# init_repo — a fresh git repository at $ROOT with an EMPTY first commit (`base`), so later
# fixtures have a stable point that predates anything they write. Every fixture that needs a base
# starts from this, so a stale index or history from the PREVIOUS fixture can never leak into the
# next. Sets $BASE_SHA to that commit.
init_repo() {
    rm -rf "$ROOT"
    mkdir -p "$ROOT/bootstrap"
    ( cd "$ROOT" \
        && git init -q \
        && git config user.email t@example.com \
        && git config user.name t \
        && git commit -q --allow-empty -m base )
    BASE_SHA="$(cd "$ROOT" && git rev-parse HEAD)"
}

# track_bootstrap — stages every path under `bootstrap/` so `git ls-files` reports it, WITHOUT
# committing. `git ls-files` reads the index, not HEAD, so this is enough to make a path "tracked"
# for the gate's purposes — the same fact `scripts/no_c_in_tests_gate.sh` relies on.
track_bootstrap() {
    ( cd "$ROOT" && git add -A -- bootstrap >/dev/null )
}

# commit_bootstrap — stages AND commits `bootstrap/`, so the fixture's "base" (the commit the gate
# diffs against) genuinely differs from an UNCOMMITTED, merely-staged change. Only fixtures that
# specifically test "unchanged since a REAL prior commit" need this; the rest stage-only.
commit_bootstrap() {
    ( cd "$ROOT" && git add -A -- bootstrap >/dev/null && git commit -q -m fixture )
}

# digest_of FILE — the same portable sha256, computed independently of the gate under test so a
# fixture can hand it a value the gate must AGREE with rather than one the gate produced itself.
digest_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

# write_c CONTENT — lays down the fixture's `bootstrap/teko.c` with the given throwaway content
# (never real compiler output — see the header), tracked in the fixture's index.
write_c() {
    printf '%s' "$1" > "$ROOT/bootstrap/teko.c"
    track_bootstrap
}

# write_provenance C RUN COMMIT SHA256 — lays down a complete-shaped bootstrap/PROVENANCE, tracked
# in the fixture's index.
write_provenance() {
    printf 'c:       %s\nrun:     %s\ncommit:  %s\nsha256:  %s\n' "$1" "$2" "$3" "$4" \
        > "$ROOT/bootstrap/PROVENANCE"
    track_bootstrap
}

# write_legacy_provenance C COMMIT SHA256 REASON — lays down a `run: unrecovered` legacy record.
write_legacy_provenance() {
    printf 'c:       %s\nrun:     unrecovered\ncommit:  %s\nsha256:  %s\nlegacy:  %s\n' \
        "$1" "$2" "$3" "$4" > "$ROOT/bootstrap/PROVENANCE"
    track_bootstrap
}

# gate_rc ROOT_DIR [BASE] — the gate's verdict for the current fixture: 0 pass, 1 refused, 2
# fatal. Wrapped so the caller can compare a number instead of arranging its own `|| rc=$?` around
# `set -e`.
gate_rc() {
    gr_rc=0
    sh "$GATE" "$1" "${2:-}" >"$WORK/out.log" 2>"$WORK/err.log" || gr_rc=$?
    printf '%s' "$gr_rc"
}

# expect_rc ROOT_DIR BASE WANT NAME — asserts the verdict for the fixture the caller just laid
# down, diffed against BASE.
expect_rc() {
    er_root="$1"; er_base="$2"; er_want="$3"; er_name="$4"
    er_got="$(gate_rc "$er_root" "$er_base")"
    if [ "$er_got" = "$er_want" ]; then
        pass "$er_name (rc=$er_got)"
    else
        fail "$er_name: want rc=$er_want, got rc=$er_got"
        sed 's/^/    /' "$WORK/err.log" >&2
    fi
}

echo "--- provenance gate ---"

init_repo
expect_rc "$ROOT" "$BASE_SHA" 0 "no C at all — nothing to prove"

NOTAGIT="$WORK/not-a-repo"
mkdir -p "$NOTAGIT/bootstrap"
printf 'x' > "$NOTAGIT/bootstrap/teko.c"
expect_rc "$NOTAGIT" "deadbeef" 2 "a C outside any git working tree at all is FATAL"

init_repo
write_c 'fixture: not a real compiler output, first revision'
expect_rc "$ROOT" "" 2 "a C present, but no base revision given at all, is FATAL"
grep -q 'no base revision given' "$WORK/err.log" \
    && pass "the no-base message says so plainly" \
    || fail "the FATAL message did not name the missing base: $(cat "$WORK/err.log")"

expect_rc "$ROOT" "0000000000000000000000000000000000000000" 2 \
    "a base revision that does not resolve in this checkout is FATAL"

# THE OWNER'S REFRAME (2026-07-29): "só podemos TROCAR o teko.c se ele passar por uma cadeia
# válida" — the verb is TROCAR. An UNCHANGED incumbent is never judged, record or no record.
init_repo
write_c 'fixture: the incumbent, already there before the base tag'
commit_bootstrap
UNCHANGED_BASE="$(cd "$ROOT" && git rev-parse HEAD)"
expect_rc "$ROOT" "$UNCHANGED_BASE" 0 \
    "bootstrap/teko.c UNCHANGED since the base — PASS, even with NO record at all"

# THE ONE CASE THIS GATE EXISTS TO CATCH: a swap with nothing behind it.
write_c 'fixture: a SWAPPED-IN revision, no record at all'
expect_rc "$ROOT" "$UNCHANGED_BASE" 1 \
    "bootstrap/teko.c CHANGED since the base, no record at all — REFUSED"
grep -q "PROVENANCE' does not exist" "$WORK/err.log" \
    && pass "the missing-record message names the file" \
    || fail "the FAIL message did not name the missing record: $(cat "$WORK/err.log")"

# THE TECHNIQUE REUSED FROM scripts/no_c_in_tests_gate.sh: a swapped-in file that git does not
# track is GENERATED (or otherwise stray), never the one legitimate committed C.
DIGEST="$(digest_of "$ROOT/bootstrap/teko.c")"
write_provenance bootstrap/teko.c 4242 deadbeefcafef00d "$DIGEST"
( cd "$ROOT" && git rm -q --cached bootstrap/teko.c )
expect_rc "$ROOT" "$UNCHANGED_BASE" 1 \
    "swapped-in C present but NOT tracked by git — refused before its hash is even read"
grep -q "NOT tracked by git" "$WORK/err.log" \
    && pass "the untracked-C message says so plainly" \
    || fail "the FAIL message did not name the untracked file: $(cat "$WORK/err.log")"

write_c 'fixture: a SWAPPED-IN revision, no record at all'
write_provenance bootstrap/teko.c 4242 deadbeefcafef00d "$DIGEST"
expect_rc "$ROOT" "$UNCHANGED_BASE" 0 "a swap with a complete, tracked, matching record — PASS"

write_c 'fixture: a SECOND swap, still unrecorded'
expect_rc "$ROOT" "$UNCHANGED_BASE" 1 "the C changed again after the record was written — REFUSED"

write_c 'fixture: a SWAPPED-IN revision, no record at all'
write_provenance bootstrap/teko.c 4242 deadbeefcafef00d \
    0000000000000000000000000000000000000000000000000000000000000000
expect_rc "$ROOT" "$UNCHANGED_BASE" 1 "a sha256 that was simply wrong from the start — REFUSED"

# THE LEGACY SENTINEL: `run: unrecovered` — the ONE case `run:` is allowed to admit defeat, and
# ONLY when the file it describes is not being swapped at all.
init_repo
write_c 'fixture: the incumbent, unchanged, recorded with an honestly-incomplete legacy record'
commit_bootstrap
LEGACY_BASE="$(cd "$ROOT" && git rev-parse HEAD)"
LEGACY_DIGEST="$(digest_of "$ROOT/bootstrap/teko.c")"
write_legacy_provenance bootstrap/teko.c deadbeefcafef00d "$LEGACY_DIGEST" \
    "predates the mechanism; run id not recovered"
expect_rc "$ROOT" "$LEGACY_BASE" 0 \
    "a legacy (run: unrecovered) record over an UNCHANGED incumbent — PASS"

write_c 'fixture: a SWAP that tries to reuse the legacy sentinel'
write_legacy_provenance bootstrap/teko.c deadbeefcafef00d "$(digest_of "$ROOT/bootstrap/teko.c")" \
    "predates the mechanism; run id not recovered"
expect_rc "$ROOT" "$LEGACY_BASE" 1 \
    "the SAME legacy sentinel over a SWAP — REFUSED, never recycled to authorize an exchange"
grep -q "legacy sentinel" "$WORK/err.log" \
    && pass "the legacy-on-a-swap message names the sentinel" \
    || fail "the FAIL message did not name the legacy sentinel: $(cat "$WORK/err.log")"

# EVERY MALFORMED RECORD IS FATAL, NEVER "no record" — checked only on a swap, where the gate
# parses the record at all.
init_repo
write_c 'fixture: the incumbent, before a swap tested against malformed records'
commit_bootstrap
MALFORMED_BASE="$(cd "$ROOT" && git rev-parse HEAD)"
write_c 'fixture: a swap, tested against malformed records'
write_provenance bootstrap/teko.c 4242 deadbeefcafef00d ""
expect_rc "$ROOT" "$MALFORMED_BASE" 2 "a record missing its sha256 is FATAL, not 'no record'"

DIGEST2="$(digest_of "$ROOT/bootstrap/teko.c")"
printf 'run: 4242\ncommit: deadbeefcafef00d\nsha256: %s\n' "$DIGEST2" > "$ROOT/bootstrap/PROVENANCE"
track_bootstrap
expect_rc "$ROOT" "$MALFORMED_BASE" 2 "a record missing its c: is FATAL"

printf 'c: bootstrap/teko.c\ncommit: deadbeefcafef00d\nsha256: %s\n' "$DIGEST2" \
    > "$ROOT/bootstrap/PROVENANCE"
track_bootstrap
expect_rc "$ROOT" "$MALFORMED_BASE" 2 "a record missing its run: is FATAL"

printf 'c: bootstrap/teko.c\nrun: 4242\nsha256: %s\n' "$DIGEST2" > "$ROOT/bootstrap/PROVENANCE"
track_bootstrap
expect_rc "$ROOT" "$MALFORMED_BASE" 2 "a record missing its commit: is FATAL"

write_provenance bootstrap/other.c 4242 deadbeefcafef00d "$DIGEST2"
expect_rc "$ROOT" "$MALFORMED_BASE" 2 "a record naming a file other than the one being checked is FATAL"

write_provenance bootstrap/absent.c 4242 deadbeefcafef00d "$DIGEST2"
expect_rc "$ROOT" "$MALFORMED_BASE" 2 "a record naming a file that does not exist is FATAL"

: > "$ROOT/bootstrap/PROVENANCE"
track_bootstrap
expect_rc "$ROOT" "$MALFORMED_BASE" 2 "an empty record is FATAL"

write_provenance bootstrap/teko.c 4242 deadbeefcafef00d "$DIGEST2"
expect_rc "$ROOT" "$MALFORMED_BASE" 0 "restored to a complete, matching record over a swap — PASS again"

if [ "$FAILED" = "1" ]; then
    echo "provenance_gate_test FAILED." >&2
    exit 1
fi
echo "provenance_gate_test OK — an unchanged incumbent is never judged, a swap is never accepted"
echo "without a matching, tracked, non-legacy record, a base that cannot be resolved is FATAL,"
echo "and a broken record never reads as absent."
