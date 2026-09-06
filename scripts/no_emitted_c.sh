#!/usr/bin/env sh
# scripts/no_emitted_c.sh — the ZERO-C RATCHET for the test gate.
#
# Owner, 2026-07-26: "vi que o self-test emitiu arquivo C, para testes, mas emitiu. O que eu disse
# foi ZERO C, isso inclui os testes." The decree admits no exception for the harness, so the proof
# has to be MECHANICAL: emitting C "just to test" is still emitting C, and a manual eyeball is how
# a rule comes back silently.
#
# WHAT IT CHECKS
#   Every `.c` file present in the worktree that git does NOT track is, by definition, an EMITTED
#   one — nobody else writes `.c` here. The script lists them (relative to the repo root, sorted)
#   and compares that list, EXACTLY, against the declared baseline below. It fails when the list
#   GROWS (a new emission site appeared) and equally when it SHRINKS (an emission site died and the
#   baseline was not updated), so the ratchet cannot rot into a rubber stamp.
#
#   The versioned `.c` are NOT emissions and are excluded by construction (they are tracked):
#   `src/runtime/teko_rt.c` + `src/assert/assert.c` are the C runtime seed the linker takes — the
#   owner's ruling keeps the LINKER — and `scripts/*_test.c` / `scripts/ar_link_run_consumer.c` are
#   hand-written C CONSUMERS that prove the object/archive writers, not compiler output.
#
# HOW TO RUN
#   Run a `teko test .` (or `teko build .`) in this worktree FIRST, then:
#       sh scripts/no_emitted_c.sh
#   Env: TEKO_EMITTED_C_BASELINE — override the baseline (newline-separated paths, "" = expect none).
#
# THE BASELINE, AND WHY IT IS NOT YET EMPTY
#   `bin/teko-tktest.c` SHRANK OUT of this baseline in 0.3.1.0 (tests-native-no-c): the gate binary
#   (`run_native_gate`/`native_gate_build`, src/build/project.tks) now builds through the own-AOT
#   backend (`emit_native`) — the SAME driver `TEKO_BACKEND=native` uses for a normal program — so no
#   `.c` translation unit is written for the test program at all any more (`cc` still runs, but only
#   as the LINKER, exactly as every native build already used it).
#
#   `bin/teko-regrcov.c` remains, and is not an oversight: `docs/design/gate-sem-c-0.3.0.31.md` §2.3
#   measured the own/native backend against the compiler's own program and it is not there yet, and
#   separately `build_regression_cov_exe` needs the C emitter's `ProgramCov` coverage instrumentation
#   (per-line/per-branch marks) to feed the regression tier's coverage — the native backend has no
#   such instrumentation pass yet (the SAME gap `native_gate_build`'s own doc names for the test
#   gate's floors). Until that lands, this ONE emission is the measured, declared truth — not an
#   exemption.
#
#     bin/teko-regrcov.c   build_regression_cov_exe (src/build/project.tks) — the self-test's regression tier
#
#   `--per-test-cov` and `--analyzer` add their own (`bin/teko-tkanalyze.c`, the per-test scratch) —
#   both STILL C-only paths, untouched by 0.3.1.0 (dev-time/advisory surfaces, never wired into CI);
#   pass TEKO_EMITTED_C_BASELINE when running the gate under those flags.
set -eu

root="$(git rev-parse --show-toplevel)"
cd "$root"

tracked="$(mktemp)"
present="$(mktemp)"
emitted="$(mktemp)"
trap 'rm -f "$tracked" "$present" "$emitted"' EXIT

git ls-files '*.c' | LC_ALL=C sort > "$tracked"
find . -name '*.c' -not -path './.git/*' | sed 's|^\./||' | LC_ALL=C sort > "$present"
comm -13 "$tracked" "$present" > "$emitted"

baseline_default='bin/teko-regrcov.c'
baseline="${TEKO_EMITTED_C_BASELINE-$baseline_default}"

expected="$(mktemp)"
trap 'rm -f "$tracked" "$present" "$emitted" "$expected"' EXIT
printf '%s' "$baseline" | sed '/^$/d' | LC_ALL=C sort > "$expected"

if cmp -s "$emitted" "$expected"; then
  n="$(wc -l < "$expected" | tr -d ' ')"
  if [ "$n" = 0 ]; then
    echo "no_emitted_c: ZERO emitted .c — the decree holds."
  else
    echo "no_emitted_c: $n emitted .c, exactly the declared baseline (see the header)."
    cat "$expected"
  fi
  exit 0
fi

echo "no_emitted_c: FAIL — the emitted-.c set does not match the declared baseline." >&2
echo "--- unexpected (emitted but NOT in the baseline: a new C emission site) ---" >&2
comm -23 "$emitted" "$expected" >&2
echo "--- missing (in the baseline but NOT emitted: a site died — shrink the baseline) ---" >&2
comm -13 "$emitted" "$expected" >&2
exit 1
