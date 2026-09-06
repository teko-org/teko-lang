#!/usr/bin/env sh
# scripts/provenance_gate.sh — THE PROVENANCE GATE: refuses to let `bootstrap/teko.c` be SWAPPED
# — added, replaced, or removed-and-reintroduced — without a declaration proving WHICH CI run
# produced the new bytes, and that the file on disk still hashes to what that run measured.
#
# Owner ruling 2026-07-29, literal, first pass: "arquivos C de gerações ou teko C precisam ser
# construídos via CI, não pelos agentes, e somente quando o CI passar verde. Assim que se cria
# degrau." — and, naming the gap directly: "nada no repositório diz de que corrida de CI veio o
# bootstrap/teko.c que lá está, nem impede alguém de commitar um C construído à mão que pareça
# igual."
#
# ── THE SCOPE, REFRAMED — "TROCAR", NOT "EXISTIR" ───────────────────────────────────────────────
# Owner ruling 2026-07-29, second pass, the one this file actually implements: *"não tem que
# segurar se está pronto, mas só podemos trocar o teko.c se ele passar por toda uma cadeia válida,
# senão não há garantia e 'perdemos' a origem."* The verb is TROCAR. This gate does not ask "does
# a provenance record exist for whatever sits at `bootstrap/teko.c`" — an unconditional version of
# that question stays red on ANY tree whose incumbent C predates this mechanism, which is every
# tree today, and a gate that is always red stops being read. It asks "did THIS diff swap the
# file", and only THEN demands the chain:
#
#   `bootstrap/teko.c` UNCHANGED since the PR's base → PASS. There is nothing to judge: the
#     incumbent was already there, is not being traded for anything, and this gate's job is to
#     guard the moment of exchange, not to re-litigate history on every unrelated PR.
#   `bootstrap/teko.c` ADDED, MODIFIED, or REMOVED-then-back since the base → the full chain is
#     required: tracked by git, a complete `bootstrap/PROVENANCE`, and its `sha256` matching the
#     file exactly as it now sits on disk. No exceptions.
#
# ── WHAT bootstrap/DEGRAU ALREADY PROVES, AND WHAT IT DOES NOT ─────────────────────────────────
# `scripts/degrau.sh` answers "is `bootstrap/teko.c` DECLARED AS A RUNG?" — a claim about USE. A
# tree with no `bootstrap/DEGRAU` at all is the NORMAL, EXPECTED state even while `bootstrap/teko.c`
# sits committed: the C is harvested OUTPUT, versioned after a green fixpoint, and ignored as input
# until a human declares a bridge. That design is correct and this gate does not touch it. What
# NEITHER `bootstrap/DEGRAU` NOR `degrau_scan` ever asked is "where did THIS FILE come from" — a
# `why` sentence explains a bootstrap emergency, not which CI run produced the bytes. That is this
# gate's entire job, and only at the one moment it can still be answered honestly: the swap itself.
#
# ── THE RECORD: `bootstrap/PROVENANCE`, A SIBLING FILE, NOT A DEGRAU FIELD ─────────────────────
# The SAME `key: value`, `#`-comment format `bootstrap/DEGRAU` already uses — parsed by the SAME
# kernel (`kv_field`, `scripts/degrau.sh`) — but a DIFFERENT file. `bootstrap/DEGRAU` is deleted
# "the day the degrau closes" (an ACTIVE bootstrap emergency ending); `bootstrap/teko.c` keeps
# being re-harvested whether or not any degrau is ever open, so its provenance must SURVIVE a
# degrau closing. A sibling file with the IDENTICAL parser closes the format without duplicating it
# or forcing the two unrelated lifecycles into one.
#
#     c:       bootstrap/teko.c    # REQUIRED — the file this record describes, relative to root
#     run:     123456789           # REQUIRED — the CI run id that produced the swap, OR the
#                                   #   literal sentinel `unrecovered` (see LEGACY RECORDS below)
#     commit:  <sha>                # REQUIRED — the git SHA that run measured
#     sha256:  <hex digest>         # REQUIRED — sha256 of the swapped-in file; MUST match it now
#     since:   0.3.1.0              # optional — the version the swap landed in
#     legacy:  <one sentence>       # REQUIRED whenever run: is `unrecovered` — see below
#
# ── LEGACY RECORDS: THE ONE CASE `run:` IS ALLOWED TO ADMIT DEFEAT ─────────────────────────────
# Owner ruling 2026-07-29, third pass, on the file that ALREADY sits at `bootstrap/teko.c` today,
# committed before this mechanism existed: *"a origem que ainda temos fica escrita em vez de se
# perder... não inventes um run: — se não o encontrares, di-lo no próprio ficheiro."* `run:
# unrecovered` is that admission, and it is legal ONLY paired with a `legacy:` explanation — but it
# is NEVER accepted where this gate actually judges anything. An UNCHANGED incumbent is never
# judged at all (see above), so a legacy record sitting quietly under it costs nothing; the moment
# `bootstrap/teko.c` is SWAPPED, `run: unrecovered` is refused outright, RED, by name — a legacy
# claim only vouches for the file that was already there before this mechanism existed, and can
# never be recycled to authorize a NEW exchange.
#
# ── EVERY MALFORMED DECLARATION IS FATAL, NEVER "no record" ────────────────────────────────────
# Same law as `scripts/degrau.sh`: a `bootstrap/PROVENANCE` that exists but does not parse, or
# names a file other than the one being checked, means a human started this ceremony and did not
# finish it — reporting that as "no record" would hide the exact state a reviewer most needs to
# see. Checked only on a SWAP, where the gate parses the record at all.
#
# ── A RECORD PROVES NOTHING ABOUT A FILE GIT DOES NOT KNOW ─────────────────────────────────────
# A `sha256:` that matches bytes on disk only proves the record and the WORKING TREE agree — not
# that the file is the one actually committed. `scripts/no_c_in_tests_gate.sh` (a sibling gate,
# `cargo/0.3.1.0-sem-c-nos-testes`) already answers "is this .c GENERATED or VERSIONED?" for a
# project directory by asking `git ls-files`; that exact technique — not a re-derived one — is
# reused here on a SWAP: before trusting any record, this gate asks git whether the new
# `bootstrap/teko.c` is TRACKED at all.
#
# ── DETERMINING THE SWAP: A REQUIRED, NEVER-GUESSED BASE ────────────────────────────────────────
# "Unchanged since the base" needs a base, and a wrong or silently-assumed one is worse than none —
# it would either judge an untouched incumbent (a false RED on every unrelated PR) or wave a real
# swap through as "unchanged" (a false PASS on the one thing this gate exists to catch). So there is
# NO fallback and NO auto-detected default: the caller supplies a resolvable git revision, and its
# absence — or its unresolvability in THIS checkout — is FATAL, never "assume unchanged".
#
# ── THE VERDICT ──────────────────────────────────────────────────────────────────────────────────
#   PASS  (exit 0)  no C at the target path; or the target is unchanged against BASE (nothing to
#                    judge); or it changed and carries a complete, tracked, matching, non-legacy
#                    record.
#   RED   (exit 1)  the target changed against BASE and either: has no record at all; is not
#                    tracked by git; its record is the `unrecovered` legacy sentinel (never valid
#                    for a swap); or its digest does not match the recorded one.
#   FATAL (exit 2)  no BASE was given, or BASE cannot be resolved in this checkout; the target
#                    exists outside any git working tree; or, on a swap, the record is malformed —
#                    a required key missing, its `c:` names the wrong file, or no digest tool
#                    exists to verify it. Never degrades to RED or PASS.
#
# Usage:  sh scripts/provenance_gate.sh [ROOT] [BASE]
#         ROOT defaults to the current directory. BASE is a git revision (sha or resolvable ref,
#         e.g. after `git fetch origin <base-sha>`) to diff `bootstrap/teko.c` against; with no C
#         at the target path BASE is never even consulted.
#
# Env:
#   TEKO_PROVENANCE_BASE  BASE, when not given positionally (CI wires this from the pull request's
#                         base sha; see .github/workflows/pr.yml).
#   TEKO_PROVENANCE_FILE  the declaration's path (default: <ROOT>/bootstrap/PROVENANCE). For
#                         testing the gate itself; CI must let it read the versioned file.
#   TEKO_PROVENANCE_C     the C this gate checks (default: <ROOT>/bootstrap/teko.c). Same override
#                         reason as TEKO_PROVENANCE_FILE.
set -eu

HERE="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
# shellcheck source=scripts/degrau.sh
. "$HERE/degrau.sh"

ROOT="${1:-$PWD}"
BASE="${2:-${TEKO_PROVENANCE_BASE:-}}"
PROVENANCE_FILE="${TEKO_PROVENANCE_FILE:-$ROOT/bootstrap/PROVENANCE}"
TARGET_C="${TEKO_PROVENANCE_C:-$ROOT/bootstrap/teko.c}"
LEGACY_SENTINEL="unrecovered"

log() { printf '%s\n' "provenance: $*" >&2; }

# sha256_of FILE — portable digest (sha256sum on Linux, shasum -a 256 elsewhere), the SAME idiom
# `scripts/ci_provision_teko.sh` already uses to verify a downloaded seed. Empty when neither tool
# is on PATH, which the caller must treat as "cannot verify", never as "matches".
sha256_of() {
    sc_f="$1"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$sc_f" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$sc_f" | awk '{print $1}'
    else
        printf '%s' ""
    fi
}

# provenance_recipe — the exact act that turns an unrecorded swap into a declared one, printed by
# the branch that fails because a changed C has no record at all. A failure that names the remedy
# is the difference between a gate and a wall (the same law `degrau_recipe` follows).
provenance_recipe() {
    log "To record a swap, after a fixpoint that HELD (see scripts/fixpoint_gate.sh's harvest):"
    log "  1. take the harvested C from that green CI run's artifact, as bootstrap/teko.c;"
    log "  2. compute its digest: sha256sum bootstrap/teko.c;"
    log "  3. write bootstrap/PROVENANCE:"
    log "        c:       bootstrap/teko.c"
    log "        run:     <the CI run id that produced it>"
    log "        commit:  <the git SHA that run measured>"
    log "        sha256:  <the digest from step 2>"
    log "  4. commit the C and the record TOGETHER — never one without the other."
    return 0
}

if [ ! -f "$TARGET_C" ]; then
    log "PASS — no '$TARGET_C' present, nothing to prove provenance for."
    exit 0
fi

GIT_ROOT="$(cd "$ROOT" 2>/dev/null && git rev-parse --show-toplevel 2>/dev/null)" || GIT_ROOT=""
if [ -z "$GIT_ROOT" ]; then
    log "FATAL: '$ROOT' is not inside a git working tree."
    log "This gate tells UNCHANGED from SWAPPED, and a committed C from one merely on disk, by"
    log "asking git; with no repository there is no way to make either distinction."
    exit 2
fi

if [ -z "$BASE" ]; then
    log "FATAL: no base revision given (arg 2 / TEKO_PROVENANCE_BASE) and '$TARGET_C' is present."
    log "Whether this is a SWAP cannot be decided without a base to diff against, and guessing"
    log "'unchanged' would be exactly the silent pass this gate exists to end."
    exit 2
fi

if ! git -C "$GIT_ROOT" rev-parse -q --verify "${BASE}^{commit}" >/dev/null 2>&1; then
    log "FATAL: base revision '$BASE' does not resolve in this checkout."
    log "Fetch it first (e.g. 'git fetch origin $BASE') — this gate never assumes 'unchanged' for"
    log "a base it cannot see."
    exit 2
fi

CHANGED="$(git -C "$GIT_ROOT" diff --name-only "$BASE" -- "$TARGET_C")"
if [ -z "$CHANGED" ]; then
    log "PASS — '$TARGET_C' is unchanged against $BASE. Nothing to judge: this is not a swap."
    exit 0
fi

log "'$TARGET_C' changed against $BASE — this IS a swap. The full chain is required."

if ! git -C "$GIT_ROOT" ls-files --error-unmatch -- "$TARGET_C" >/dev/null 2>&1; then
    log "FAIL — the swapped-in '$TARGET_C' is present but NOT tracked by git."
    log "The one legitimate C at this path is a deliberate commit, harvested from a green CI run"
    log "and versioned together with its provenance record — never a file merely left on disk."
    log "(scripts/no_c_in_tests_gate.sh catches this same shape, generated vs. versioned C, after"
    log "a test session; this gate catches it here, at the one path a committed C may occupy.)"
    exit 1
fi

if [ ! -f "$PROVENANCE_FILE" ]; then
    log "FAIL — '$TARGET_C' was swapped but '$PROVENANCE_FILE' does not exist."
    log "Every swap needs a record of WHICH CI run produced it."
    provenance_recipe
    exit 1
fi

PV_C="$(kv_field "$PROVENANCE_FILE" c)"
PV_RUN="$(kv_field "$PROVENANCE_FILE" run)"
PV_COMMIT="$(kv_field "$PROVENANCE_FILE" commit)"
PV_SHA256="$(kv_field "$PROVENANCE_FILE" sha256)"

if [ -z "$PV_C" ] || [ -z "$PV_RUN" ] || [ -z "$PV_COMMIT" ] || [ -z "$PV_SHA256" ]; then
    log "FATAL: '$PROVENANCE_FILE' exists but is not a complete record. Required keys:"
    log "  c:       bootstrap/teko.c   the file this record describes"
    log "  run:     <CI run id>        the run that produced it"
    log "  commit:  <sha>              the git SHA that run measured"
    log "  sha256:  <hex digest>       the digest of the swapped-in file"
    log "Delete the file if there is no record to make — an incomplete claim is worse than none."
    exit 2
fi

if [ "$PV_RUN" = "$LEGACY_SENTINEL" ]; then
    log "FAIL — '$PROVENANCE_FILE' names the legacy sentinel 'run: $LEGACY_SENTINEL', and this is"
    log "a SWAP (the file changed against $BASE). A legacy record only vouches for the file that"
    log "was ALREADY there before this mechanism existed — it can never authorize a NEW exchange."
    log "Name the real CI run that produced this swap."
    exit 1
fi

case "$PV_C" in
    /*) PV_C_RESOLVED="$PV_C" ;;
    *) PV_C_RESOLVED="$ROOT/$PV_C" ;;
esac

if [ "$PV_C_RESOLVED" != "$TARGET_C" ]; then
    log "FATAL: '$PROVENANCE_FILE' names '$PV_C_RESOLVED', not the file being checked ('$TARGET_C')."
    log "A record only proves provenance for the file it names — point it at the right one."
    exit 2
fi

ACTUAL_SHA256="$(sha256_of "$TARGET_C")"
if [ -z "$ACTUAL_SHA256" ]; then
    log "FATAL: no sha256sum/shasum on PATH — this gate cannot verify '$TARGET_C' at all."
    log "Refusing to report a pass it cannot back up."
    exit 2
fi

if [ "$ACTUAL_SHA256" != "$PV_SHA256" ]; then
    log "FAIL — the swapped-in '$TARGET_C' does not match the recorded provenance."
    log "  recorded sha256: $PV_SHA256"
    log "  actual   sha256: $ACTUAL_SHA256"
    log "'$TARGET_C' changed after '$PROVENANCE_FILE' was written and nobody updated the record."
    exit 1
fi

log "PASS — the swapped-in '$TARGET_C' matches CI run $PV_RUN (commit $PV_COMMIT), recorded in"
log "  '$PROVENANCE_FILE'."
exit 0
