#!/bin/sh
# check-limits.sh MC CONFIG -- the limits gate. `mc limits . --config CONFIG`
# prints one table per build step this project takes: the compiler
# ([compiler], `build/teko.mc` -> `build/teko`) and then the entry
# ([project], `tests/hello.tk` compiled BY that binary), each ending in its
# own `tolerance T.TT, verdict V` line. A `grow` verdict means some row's
# `used` outgrew `reserved * tolerance` -- `[limits] tolerance` in
# `teko.toml`, inherited from a much smaller project (docs/internals/debts.md)
# -- and nobody re-runs this by hand, so a budget row can move for merges
# without anyone saying so.
#
# `rm -rf build` runs first: `mc limits` reuses whatever is already in
# `build/`, and a stale glue file or object changes the numbers it reports
# (an incremental measurement is not the same claim as a clean one).
#
# ONLY THE FIRST TABLE (the compiler) IS GATED. Measured at head, it is
# clean: every row's grow column is 0, tolerance 1.0, verdict ok. The SECOND
# table (the entry, `tests/hello.tk` built by the taught compiler) already
# answers `grew` today -- `passes`, `syntax`, `alias` and `types` each outgrew
# their reserved slot at this same tolerance -- and gating on it would either
# fail on the first run or need its cap raised to pass, which is exactly the
# silent weakening this check exists to refuse. It is printed and left as a
# debt (docs/internals/debts.md), not gated.
#
# `heap` is never cited as a pass/fail signal on its own: it is `mc limits`'s
# own estimate, not a budget any table here reserves by hand.

set -u

mc="${1:-}"
config="${2:-}"

if [ -z "$mc" ] || [ -z "$config" ]; then
    echo "usage: check-limits.sh MC CONFIG" >&2
    exit 1
fi
if command -v "$mc" >/dev/null 2>&1; then
    mc=$(command -v "$mc")
elif [ ! -x "$mc" ]; then
    echo "FAIL: compiler '$mc' not found or not executable"
    exit 1
fi
if [ ! -f "$config" ]; then
    echo "FAIL: no such config: $config"
    exit 1
fi

rm -rf build
out=$("$mc" limits . --config "$config" 2>&1)
rc=$?

# Each block starts at a `limits <name>` line and ends at the `tolerance ...,
# verdict ...` line that follows it; there are exactly two, one per build
# step `mc limits` takes for this project ([compiler] then [project]).
names=$(printf '%s\n' "$out" | awk '/^limits /{print $2}')
verdicts=$(printf '%s\n' "$out" | awk '/^tolerance/{ for (i=1;i<=NF;i++) if ($i=="verdict") print $(i+1) }' | tr -d ,)
nblocks=$(printf '%s\n' "$names" | grep -c .)

if [ "$nblocks" -lt 2 ]; then
    echo "FAIL: expected 2 limits tables (compiler, entry), got $nblocks"
    printf '%s\n' "$out"
    exit 1
fi

compiler_name=$(printf '%s\n' "$names" | sed -n '1p')
compiler_verdict=$(printf '%s\n' "$verdicts" | sed -n '1p')
entry_name=$(printf '%s\n' "$names" | sed -n '2p')
entry_verdict=$(printf '%s\n' "$verdicts" | sed -n '2p')

# Measured on this project's own tables, `mc limits`'s exit code is 3 when
# the entry table's own known state (its `grow` plus `intrin` sitting at
# `tight`) is present and 1 on a config it refuses outright (no table at
# all, e.g. an out-of-range `[limits] tolerance`) -- 0-3 is the range this
# script has actually seen and accounted for. `nblocks < 2` above already
# catches the "refused before printing anything" case; this catches the
# other one: `mc limits` prints two tables that both parse `ok`/`grew` fine
# but STILL exits somewhere this script has never seen, which is not a
# verdict on a row -- more likely an internal error or a misuse of the
# command -- and is a hard failure on its own rather than silently passing
# just because the tables it did print happened to read clean.
if [ "$rc" -gt 3 ]; then
    echo "FAIL limits: mc limits exited $rc, outside the range this check has measured -- not a verdict it can read"
    printf '%s\n' "$out"
    exit 1
fi

fails=0
if [ "$compiler_verdict" = "ok" ]; then
    echo "ok limits: compiler ($compiler_name) verdict ok, tolerance holds"
else
    echo "FAIL limits: compiler ($compiler_name) verdict $compiler_verdict -- a budget row moved"
    printf '%s\n' "$out"
    fails=1
fi
echo "owed limits: entry ($entry_name) verdict $entry_verdict (not gated, docs/internals/debts.md)"

exit "$fails"
