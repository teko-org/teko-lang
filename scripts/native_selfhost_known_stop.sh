#!/usr/bin/env bash
# scripts/native_selfhost_known_stop.sh — the KNOWN-STOP (.32) verdict for the native self-host,
# extracted from `mem-paranoid`'s inline step so it can be TESTED.
#
# WHY EXTRACTED. The step lives in a job that rides `artifact-linux-x86_64-glibc` — the native leg,
# red by design until the ladder closes — so `mem-paranoid` is skipped on every run and the step's
# logic is never exercised. Inline in the YAML it was unreachable AND untestable: its correctness
# rested on reading it, which is the one thing this lane does not accept as proof. As a script it
# gets `scripts/native_selfhost_known_stop_test.sh`, which proves all three verdicts by inversion
# against fabricated logs, on every host, needing no compiler.
#
# WHAT IT ASSERTS (owner signed off 2026-07-30) — the pair that stays stable across the whole
# degrau ladder, deliberately NOT the current degrau's text:
#   1. the native self-host does NOT complete; and
#   2. the way it fails is a NAMED HONEST STOP (`native backend N1`) — not a crash, not a silent
#      wrong answer, not an empty failure.
#
# (2) is the half that earns its keep every run. "Paragem barulhenta é sempre melhor que resposta
# errada calada": the day the native backend starts failing by segfault or by producing garbage
# instead of stopping by name, THIS is what catches it, and no degrau-specific pin ever could.
#
# The PREVIOUS shape grepped for "needs slice elements held BY VALUE" and failed when the stop was
# anything else — and that degrau CLOSED, so the pin was already stale (the self-host now stops at
# `resolve_generic_inst`). A pin keyed on the current degrau goes red on EVERY degrau the ladder
# closes: closing one is progress, and progress must not read as breakage.
#
# usage: scripts/native_selfhost_known_stop.sh <build_rc> <log_path>
#   build_rc  the exit status of the native self-host build
#   log_path  the file that build's combined output was written to

set -u

RC="${1:-}"
LOG="${2:-}"

if [ -z "$RC" ] || [ -z "$LOG" ]; then
    echo "native-selfhost-known-stop: FATAL — usage: $0 <build_rc> <log_path>" >&2
    exit 2
fi
if [ ! -f "$LOG" ]; then
    echo "native-selfhost-known-stop: FATAL — no log at '$LOG'; a verdict with nothing to read is not a verdict" >&2
    exit 2
fi

if [ "$RC" -eq 0 ]; then
    echo "KNOWN-STOP LIFTED: the native backend self-hosted."
    echo "This gate exists to assert that it CANNOT yet. Promote it:"
    echo "  delete this step from mem-paranoid and restore the plain build,"
    echo "  'run: ./dl/linux-x86_64-glibc/teko . -o gen2-mp --no-verify --release'"
    exit 1
fi

if ! grep -q 'native backend N1' "$LOG"; then
    echo "the native self-host failed, but NOT by a named honest stop — this is the case this gate" >&2
    echo "exists to catch: a crash or a silent wrong answer where a barulhenta stop belongs." >&2
    echo "The tail below is the whole evidence:" >&2
    tail -n 20 "$LOG" >&2
    exit 1
fi

echo "KNOWN-STOP held — it stops, and it stops by name. Today's degrau, for the record:"
grep -m1 'native backend N1' "$LOG"
exit 0
