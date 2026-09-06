#!/usr/bin/env sh
# scripts/no_skips_gate.sh — NO SKIPS IN THE COMPILER'S OWN CI.
#
# Owner ruling 2026-07-27: *"No caso do compilador (não para o desenvolvedor final, para ele, ele
# escolhe), não podem haver skips, se houver é falha (deve ser avaliado após a execução da lane
# para não imputar o comportamento no fonte do teko mas sim no CI)."*
#
# WHY THIS IS A SCRIPT AND NOT A CHANGE TO `teko test`. A skipped row is a legitimate outcome of the
# LANGUAGE: a user's project may declare a regressor whose capability (a cross-linker, a leaf
# checker) is genuinely absent on their machine, and refusing to run the other rows would be the
# tool bullying its user. That choice belongs to whoever runs the suite. What does NOT belong to
# them is OUR OWN CI reporting green over rows it never executed. So the rule lives HERE, at the
# lane, applied to the run's OUTPUT — the compiler's semantics are untouched and no `.tks` gains a
# CI-shaped behaviour it would have to carry into every user's build.
#
# WHY AFTER THE LANE, NOT DURING. Same reason, one layer down: evaluating mid-run would mean the
# executor deciding policy. Reading the transcript afterwards keeps the executor honest and dumb —
# it reports what happened, and this decides what that means for THIS pipeline.
#
# WHAT IT REJECTS, and each pattern is one the executor actually emits (measured against a real
# `teko test .` transcript, not guessed):
#
#   `teko: regressions 9 run, 2 skipped, 2 failed`   the file-level tally
#   `teko: regression skip <label> — <reason>`       one skipped scenario, with its reason
#   `… ; 2 scenario row(s) skipped — …`              a regressor reporting `ok` WITH skipped rows
#                                                    inside it — the most dangerous shape, because
#                                                    the row says `ok` and a reader stops there
#
# THE THIRD PATTERN IS THE POINT. `regression ok own_native.tkr (… 2 scenario row(s) skipped …)` is
# a green line over unexecuted work. A gate that only counted the tally would still let it through
# on a lane where the file-level count happened to be zero.
#
# Usage:  sh scripts/no_skips_gate.sh <test-transcript>
#
# Env:
#   TEKO_ALLOW_SKIPS  set to 1 to report findings without failing. For a DEVELOPER running the
#                     suite by hand on a machine that legitimately lacks a toolchain — never for a
#                     lane. A lane that needs this set has a provisioning bug, not a skip problem.
set -eu

LOG="${1:?usage: no_skips_gate.sh <test-transcript>}"
[ -f "$LOG" ] || { printf '%s\n' "no-skips: transcript '$LOG' does not exist" >&2; exit 1; }

log() { printf '%s\n' "no-skips: $*" >&2; }

# The executor renders live progress with CR, so normalize before matching or a skip line hiding
# behind a progress update on the same physical line is never seen.
NORM="$(mktemp)"
trap 'rm -f "$NORM"' EXIT
tr '\r' '\n' < "$LOG" > "$NORM"

FOUND=0

# 1) the file-level tally, any non-zero count
if grep -qE '^teko: regressions [0-9]+ run, [1-9][0-9]* skipped' "$NORM"; then
    FOUND=1
    log "FOUND — regressor files were skipped:"
    grep -E '^teko: regressions [0-9]+ run, [1-9][0-9]* skipped' "$NORM" | sed 's/^/no-skips:   | /' >&2
fi

# 2) individual skipped scenarios, each with the reason the executor gave
if grep -q '^teko: regression skip ' "$NORM"; then
    FOUND=1
    log "FOUND — individual scenarios were skipped:"
    grep '^teko: regression skip ' "$NORM" | sed 's/^/no-skips:   | /' >&2
fi

# 3) a regressor reporting `ok` while carrying skipped rows inside it
if grep -q 'scenario row(s) skipped' "$NORM"; then
    FOUND=1
    log "FOUND — a regressor reported OK with unexecuted rows inside it:"
    grep 'scenario row(s) skipped' "$NORM" | sed 's/^/no-skips:   | /' >&2
fi

if [ "$FOUND" = "0" ]; then
    log "PASSED — every declared row ran. No skips."
    exit 0
fi

log ""
log "VERDICT: FAILED — the compiler's own suite may not skip."
log "A skipped row is INCONCLUSIVE, not green: nobody knows whether that construct works."
log "The fix is to PROVISION the missing capability on this lane, never to accept the skip."
log "(A developer running this by hand on a machine without the toolchain: TEKO_ALLOW_SKIPS=1.)"

if [ "${TEKO_ALLOW_SKIPS:-0}" = "1" ]; then
    log "TEKO_ALLOW_SKIPS=1 — reporting without failing. The verdict above stands."
    exit 0
fi
exit 1
