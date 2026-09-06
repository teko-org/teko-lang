#!/usr/bin/env bash
# scripts/native_selfhost_known_stop_test.sh — proves all three verdicts of
# `scripts/native_selfhost_known_stop.sh` BY INVERSION, against fabricated logs.
#
# WHY IT EXISTS. The gate it tests lives in `mem-paranoid`, a job that rides the NATIVE leg and is
# therefore skipped on every run until the degrau ladder closes. Without this file the gate's logic
# would ship on the strength of having been read — and "I read it and it looks right" is precisely
# the proof this lane refuses. A gate nobody can exercise is a gate nobody can trust.
#
# PORTABLE TO ALL SIX HOSTS BY CONSTRUCTION: it fabricates its own logs and never builds anything,
# so it runs where no compiler and no native backend exist. No `sed -E`, no `grep -P` — Git-Bash on
# the Windows runner has neither, and one `sed -E` already cost this lane an opaque exit 127.
#
# usage: bash scripts/native_selfhost_known_stop_test.sh

set -u

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$script_dir"
GATE="scripts/native_selfhost_known_stop.sh"

pass_count=0
fail_count=0
ok() { echo "  ok   — $1"; pass_count=$((pass_count + 1)); }
no() { echo "  FAIL — $1" >&2; fail_count=$((fail_count + 1)); }

work="$(mktemp -d "${TMPDIR:-/tmp}/teko-nsks.XXXXXX")"
trap 'rm -rf "$work"' EXIT

# An honest stop, shaped like the one the native self-host actually produces today.
honest="$work/honest.log"
{
  echo "teko 0.3.0.31-beta"
  echo "  checker    6228/6228 items            6.4s"
  echo "teko: .: native backend N1: a \`null\` in this position needs the null-union wrapper the placement does not declare a type for (N2) [in \`teko::checker::resolve_generic_inst\`]"
} > "$honest"

# The SAME shape but naming a DIFFERENT degrau — the previous, stale pin failed on this. The new
# one must accept it, because closing a degrau is progress, not breakage.
other="$work/other-degrau.log"
{
  echo "teko 0.3.0.31-beta"
  echo "teko: .: native backend N1: builtin \`f64_bits\` not yet lowered (N2) [in \`teko::checker::eval_float_literal\`]"
} > "$other"

# A crash: it failed, but not by a named stop. This is the case the gate earns its keep on.
crash="$work/crash.log"
{
  echo "teko 0.3.0.31-beta"
  echo "  checker    6228/6228 items"
  echo "Segmentation fault (core dumped)"
} > "$crash"

# A success log: the self-host completed.
success="$work/success.log"
echo "teko 0.3.0.31-beta" > "$success"

echo "native_selfhost_known_stop_test: running on $(uname -s)-$(uname -m)"

echo "1) it HOLDS (green) on an honest named stop"
if bash "$GATE" 1 "$honest" >/dev/null 2>&1; then
    ok "an honest 'native backend N1' stop with a failing build is GREEN"
else
    no "the gate rejected an honest named stop — the KNOWN-STOP cannot hold, so the lane can never be green"
fi

echo "2) it HOLDS on a DIFFERENT degrau — the stale-pin defect, proven fixed by inversion"
if bash "$GATE" 1 "$other" >/dev/null 2>&1; then
    ok "a different degrau is still GREEN (closing one is progress, not breakage)"
else
    no "the gate is pinned to a specific degrau again — it will go red on every degrau the ladder closes"
fi

echo "3) it goes RED when the self-host SUCCEEDS — the one real event"
if bash "$GATE" 0 "$success" >/dev/null 2>&1; then
    no "a successful self-host was accepted — the KNOWN-STOP would never lift, and .32 closing would pass unnoticed"
else
    ok "a successful self-host is RED, which is the signal to promote"
fi

echo "4) it goes RED on a crash — a failure that is NOT a named honest stop"
if bash "$GATE" 139 "$crash" >/dev/null 2>&1; then
    no "a segfault was absorbed as if it were an honest stop — this is the silent-wrong-answer hole"
else
    ok "a crash is RED (paragem barulhenta, nunca resposta errada calada)"
fi

echo "5) a missing log is FATAL, never a pass"
if bash "$GATE" 1 "$work/does-not-exist.log" >/dev/null 2>&1; then
    no "a verdict was issued with no log to read"
else
    ok "no log means no verdict"
fi

echo "6) the honest-stop diagnostic is REPORTED, not swallowed"
if bash "$GATE" 1 "$honest" 2>/dev/null | grep -q 'resolve_generic_inst'; then
    ok "the degrau reached today is printed for the record"
else
    no "the gate holds but prints nothing identifying the stop — a green with no evidence"
fi

echo "native_selfhost_known_stop_test: $pass_count passed, $fail_count failed"
if [ "$fail_count" -ne 0 ]; then
    echo "native_selfhost_known_stop_test: FAIL" >&2
    exit 1
fi
echo "native_selfhost_known_stop_test: PASS"
exit 0
