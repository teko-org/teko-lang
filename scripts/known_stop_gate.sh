#!/usr/bin/env sh
# scripts/known_stop_gate.sh — THE KNOWN-STOP ENVELOPE, made to absorb ONLY the diagnostic it pins.
#
# Owner ruling: a KNOWN-STOP envelope may swallow ONE named failure — never anything else, on the
# same project or a different one. Measured directly in run `30400712261` (job `90416003791`,
# `test / windows-x86_64`): the Windows leg's inline PowerShell decided "held" by checking whether
# the test log contained the bare SUBSTRING `B1-args`, and `src/backend/isel_x86_64.tks` names
# THREE distinct honest-stops that all share that substring —
#
#   1. `select_param_x86`  (callee):  "B1-args — an integer PARAMETER past the ABI's
#      argument-register window needs the stack-arg slot (0.3.1)"
#   2. `pin_args_x86`      (caller, variadic): "B1-args — a variadic call needs the
#      register/stack split and the AL vector count; LCall carries no arity (0.3.1)"
#   3. `pin_args_x86`      (caller, the ONE this envelope actually pins): "B1-args — an
#      integer CALL ARGUMENT past the ABI's argument-register window needs the stack-arg
#      slot (0.3.1)"
#
# A substring match cannot tell these apart: any one of the three firing would have read as
# "KNOWN-STOP held" and turned a genuinely DIFFERENT regression green. This gate instead matches
# the pinned diagnostic's FULL text — see PINNED_DIAG below — so a sibling stop, or an unrelated
# failure that happens to mention the same family name, stays RED.
#
# THE CONTRACT, unchanged from the envelope this replaces: the leg is held (exit 0) ONLY when ALL
# of —
#   1. the `teko test .` child actually failed (RC != 0) — a pass means the stop is LIFTED and the
#      envelope must be promoted out, never silently kept;
#   2. exactly one regression row failed (`regressions N run, S skipped, 1 failed`) — more than one
#      means a SECOND break is hiding behind this pin;
#   3. no unit `#test` failed (`test <label> ... ` with no trailing "ok" on its own line) — this pin
#      covers the regression tier only;
#   4. the log names the EXACT pinned diagnostic text, not merely the shared "B1-args" family name.
#
# Usage:  sh scripts/known_stop_gate.sh RC LOG_FILE
#         RC        the exit status of the `teko test .` invocation that produced LOG_FILE
#         LOG_FILE  the captured combined stdout+stderr of that invocation
#
# Exit codes:  0  KNOWN-STOP held — the suite failed for EXACTLY the pinned reason and nothing else
#              1  RED — the stop is lifted (RC == 0), or the log shows a DIFFERENT failure shape;
#                 the caller must not treat this as a pass
#              2  FATAL — the inputs themselves could not be read/parsed
#
# WINDOWS PORTABILITY (this gate's OWN Windows leg is the caller — see the workflow step this
# replaces). Measured directly in run `30496810306` (job `90728702483`): the first version of this
# file used `sed -E` (extended-regex mode) to pull the failed-row count out of the summary line.
# `-E` is a GNU/BSD sed EXTENSION, never a POSIX `sed` option (POSIX only standardizes `-n`/`-e`/
# `-f`); unlike `grep -E`, which POSIX itself defines and which this file already relies on
# elsewhere (proven on this exact Windows runner by `scripts/ci_provision_teko.sh`'s own `grep -E`
# call), `sed -E` is not a portability guarantee anywhere, it is a convenience this file had not
# earned. That command failed to resolve on the Windows leg's `C:\Program Files\Git\bin\bash.EXE`
# before this gate could print a single `known-stop:` line — the caller saw a bare exit 127, not a
# verdict, so the very fix this gate exists to make testable never even ran. The extraction below
# is now plain POSIX BRE (`\( \)` capture groups, `[0-9][0-9]*` where ERE would spell `[0-9]+`) —
# the same dialect `sed 's/^/known-stop:   /'` already uses three times below without incident.
# `scripts/known_stop_gate_test.sh` now asserts this file's OWN source never regresses to `-E`/`-r`
# on `sed` again (the inversion proof for THIS bug, the same shape as the B1-args inversion proof
# above).
set -eu

PINNED_DIAG="isel x86-64: B1-args — an integer call argument past the ABI's argument-register window needs the stack-arg slot (0.3.1)"

log() { printf 'known-stop: %s\n' "$1" >&2; }

RC="${1:?usage: known_stop_gate.sh RC LOG_FILE}"
LOG_FILE="${2:?missing LOG_FILE}"

[ -f "$LOG_FILE" ] || { log "FATAL: no such log file '$LOG_FILE'"; exit 2; }

case "$RC" in
    *[!0-9]*|'') log "FATAL: RC must be a non-negative integer, got '$RC'"; exit 2 ;;
esac

if [ "$RC" -eq 0 ]; then
    log "KNOWN-STOP LIFTED: the suite passed. Promote this envelope: restore the plain"
    log "'teko test .' invocation and drop this gate — a passing run must never read as held."
    exit 1
fi

SUMMARY_LINE="$(grep -E 'regressions [0-9]+ run, [0-9]+ skipped, [0-9]+ failed' "$LOG_FILE" | tail -n 1 || true)"
if [ -z "$SUMMARY_LINE" ]; then
    log "the suite failed, but printed no regression summary line at all — this is NOT the"
    log "pinned stop (which reaches the regression tier); the whole log follows:"
    sed 's/^/known-stop:   /' "$LOG_FILE" >&2
    exit 1
fi

FAILED_COUNT="$(printf '%s\n' "$SUMMARY_LINE" | sed 's/.*, \([0-9][0-9]*\) failed.*/\1/')"
if [ "$FAILED_COUNT" != "1" ]; then
    log "more than the pinned row failed (or none did) — this envelope must not cover a second"
    log "break: '$SUMMARY_LINE'"
    exit 1
fi

UNIT_FAIL_LINES="$(grep -E '^test [^ ]+ \.\.\. ' "$LOG_FILE" | grep -v ' ok$' || true)"
if [ -n "$UNIT_FAIL_LINES" ]; then
    log "a UNIT test failed on this host; the B1-args pin covers the regression tier only:"
    printf '%s\n' "$UNIT_FAIL_LINES" | sed 's/^/known-stop:   /' >&2
    exit 1
fi

if ! grep -qF "$PINNED_DIAG" "$LOG_FILE"; then
    log "the suite failed, but NOT with the exact diagnostic this envelope pins. Either a"
    log "sibling B1-args stop fired (a DIFFERENT bug — 'parameter' or 'variadic call', never the"
    log "'call argument' this pin names) or an unrelated regression broke:"
    sed 's/^/known-stop:   /' "$LOG_FILE" >&2
    exit 1
fi

log "KNOWN-STOP held — the suite stops only where the pinned fix will pick it up:"
log "$PINNED_DIAG"
exit 0
