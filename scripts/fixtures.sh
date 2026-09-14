#!/bin/sh
# fixtures.sh -- the one corridor for both fixture kinds this repository has
# (DECISION_LOG.md D52; D33 first named the gap): `tests/*.tk`, which compile
# and RUN, judged by `// expect-exit: N`, and `tests/refuse/*.tk`, which must
# FAIL to compile, judged by a two-line header naming the exact diagnostic.
#
# Usage: sh scripts/fixtures.sh <compiler> <config-base> [exe-suffix]
#
#   <compiler>     the teko binary fixtures are compiled WITH (e.g.
#                  ./build/teko, or a stage of the bootstrap ladder)
#   <config-base>  a config derived from teko.toml ([target] already set for
#                  the host); [project].entry/out are overwritten per fixture,
#                  everything else -- [compiler], [linker], [limits] -- stands
#   [exe-suffix]   appended to every binary this script builds and runs
#                  (".exe" on Windows; empty everywhere else)
#
# `tests/*.tk` (every file directly under `tests/`, `tests/refuse/` is a
# different glob and never doubles into this one): each one MUST carry
# `// expect-exit: N`; a build --entry-only, the run, and the exit code
# compared to N is the same three steps CONTRIBUTING.md's recipe always ran,
# now written once.
#
# `tests/refuse/*.tk`: each one MUST carry TWO headers,
#   // expect-refuse: teko: <the exact message>
#   // expect-refuse-line: <the exact line>
# and the build MUST fail (exit != 0); its stderr, CR-stripped (a Windows
# leg's `mc` still writes CRLF), MUST contain the literal substring
# `:<line>: teko: <message>` -- no file name in the match, `grep -F` so a
# message with regex metacharacters (`?`, `[`, `.`) is still literal. Exact
# text AND exact line, because a refusal moving to the wrong line is exactly
# as wrong as a refusal with the wrong words (a scout measured both moving
# independently across some constructs).
#
# A `.tk` missing its header(s) is a harness bug, not a fixture result: it
# fails the run with a clear message instead of being silently skipped.
#
# Prints one summary line, "N passed, M refused as expected, K failed", and
# exits 0 iff K = 0.

set -u

compiler="${1:?usage: fixtures.sh <compiler> <config-base> [exe-suffix]}"
base="${2:?usage: fixtures.sh <compiler> <config-base> [exe-suffix]}"
exe="${3:-}"

pass=0
refused=0
fail=0

cfg="mc.fixtures.$$.toml"
err="${TMPDIR:-/tmp}/fixtures.$$.err"
trap 'rm -f "$cfg" "$err"' EXIT

for src in tests/*.tk; do
    [ -e "$src" ] || continue
    n=$(basename "$src" .tk)
    want=$(grep -m1 '// expect-exit:' "$src" | sed 's/.*expect-exit: *//')
    if [ -z "$want" ]; then
        echo "FAIL: $src has no // expect-exit: N header" >&2
        fail=$((fail + 1))
        continue
    fi
    sed -e "s#^entry = .*#entry = \"tests/$n.tk\"#" -e "s#^out   = .*#out   = \"build/$n$exe\"#" \
        "$base" >"$cfg"
    if ! "$compiler" build . --config "$cfg" --entry-only >"$err" 2>&1; then
        echo "FAIL: $src failed to compile" >&2
        sed 's/^/  /' "$err" >&2
        fail=$((fail + 1))
        continue
    fi
    "build/$n$exe"
    got=$?
    if [ "$got" -ne "$want" ]; then
        echo "FAIL: $src exited $got, want $want" >&2
        fail=$((fail + 1))
        continue
    fi
    pass=$((pass + 1))
done

for src in tests/refuse/*.tk; do
    [ -e "$src" ] || continue
    n=$(basename "$src" .tk)
    msg=$(grep -m1 '// expect-refuse:' "$src" | sed 's#^// expect-refuse: *##')
    line=$(grep -m1 '// expect-refuse-line:' "$src" | sed 's/.*expect-refuse-line: *//')
    if [ -z "$msg" ] || [ -z "$line" ]; then
        echo "FAIL: $src is missing // expect-refuse: and/or // expect-refuse-line: header" >&2
        fail=$((fail + 1))
        continue
    fi
    sed -e "s#^entry = .*#entry = \"tests/refuse/$n.tk\"#" -e "s#^out   = .*#out   = \"build/refuse-$n$exe\"#" \
        "$base" >"$cfg"
    if "$compiler" build . --config "$cfg" --entry-only >"$err" 2>&1; then
        echo "FAIL: $src compiled, expected it to be refused ($msg)" >&2
        fail=$((fail + 1))
        continue
    fi
    if ! tr -d '\r' <"$err" | grep -qF ":$line: $msg"; then
        echo "FAIL: $src -- stderr does not contain ':$line: $msg'" >&2
        sed 's/^/  /' "$err" >&2
        fail=$((fail + 1))
        continue
    fi
    refused=$((refused + 1))
done

echo "$pass passed, $refused refused as expected, $fail failed"
[ "$fail" -eq 0 ]
