#!/usr/bin/env sh
# scripts/no_c_in_tests_gate.sh — THE NO-C-IN-TESTS GATE: after a `teko test .` session run under
# the native backend, fails the lane RED if any generated `.c`/`.h` was left in the tree.
#
# Owner ruling 2026-07-29, literal: "esse é outro que não pode emitir C quando for compilar
# nativo, pode até instruir o agente a adicionar uma validação para verificar, se ao final de uma
# sessão de testes houver um C gerado, tem que fechar vermelha a lane."
#
# ── THE LIE THIS CLOSED (owner ruling 2026-07-29) — AND WHAT REMAINS OPEN (0.3.1.0 tests-native-no-c) ──
# `teko test .`'s unit half (`run_native_gate`/`native_gate_build`, src/build/project.tks) USED TO
# emit the `#test` corpus as a C translation unit and compile it with `cc` UNCONDITIONALLY — it
# never read `TEKO_BACKEND`. As of 0.3.1.0 (tests-native-no-c) that emission site is GONE: the gate
# binary (`bin/<name>-tktest`) is built through the own-AOT-backend driver (`emit_native`, the same
# one `TEKO_BACKEND=native` uses for a normal program) — no `.c` translation unit is written for the
# test program at all any more. `cc` still runs, but only as the LINKER (`link_object`, exactly as
# every native build already used it to resolve `teko_rt`/`assert`) — never as a compiler of a
# `teko`-generated `.c`.
#
# WHAT STILL EMITS C, AND WHY THIS GATE STILL HAS WORK TO DO. On the "teko" project itself the
# regression tier emits a SEPARATE C translation unit (`build_regression_cov_exe`: a
# coverage-instrumented copy of the compiler the regressor runs against, `bin/teko-regrcov.c`) —
# UNTOUCHED by the 0.3.1.0 fix, because it depends on the C emitter's `ProgramCov` coverage
# instrumentation, which the native backend does not have yet (a distinct, larger gap — native
# per-line/per-branch coverage marks). So this script still correctly fails RED on the "teko"
# project's own `teko test .` today, for `bin/teko-regrcov.c` alone; a project with no
# `[tests] regression` files (`m.test_regression_files.len == 0`) now passes this gate cleanly.
#
# THIS SCRIPT DOES NOT FIX EITHER EMISSION SITE ITSELF — that is (part) done, (part) separately
# scoped. It makes any surviving `.c`/`.h` IMPOSSIBLE to pass unnoticed: run it right after a
# native-backend `teko test .`, and any `.c`/`.h` that session left behind fails the lane, loud, by
# name.
#
# ── WHAT COUNTS AS "GENERATED" ────────────────────────────────────────────────────────────────
# Every `.c`/`.h` under PROJECT_DIR that git does NOT track. The versioned C twins this project
# still carries on purpose (`src/runtime/teko_rt.{c,h}`, `src/assert/assert.{c,h}`, the bootstrap
# seed `bootstrap/teko.c`, the hand-written C consumers under `scripts/`) are TRACKED, so they are
# never flagged — nobody else in this tree writes an untracked `.c`/`.h` except a compiler
# emission. This is the same discriminator `scripts/no_emitted_c.sh` already uses.
#
# ── WHY THIS IS NOT `scripts/no_emitted_c.sh` WEARING A NEW NAME ────────────────────────────────
# `no_emitted_c.sh` is a RATCHET: it accepts a DECLARED BASELINE of known emission sites (today,
# exactly the two named above) and fails only when that set changes shape. It exists for the world
# where `teko test` is still allowed to emit C while the native gate is being built out.
#
# This gate accepts NO baseline. On a leg that claims to run its tests under the native backend,
# ANY emitted `.c`/`.h` — including the two `no_emitted_c.sh` currently tolerates — is a defect,
# full stop. The two scripts read the SAME evidence and answer two DIFFERENT questions: "did the
# known emission sites change?" vs. "did a native-backend test session touch `cc` at all?".
#
# ── EVERY UNPROVABLE STATE IS FATAL, NEVER "clean" ──────────────────────────────────────────────
# A directory that does not exist or cannot be read, or a `git` failure (not a repository,
# `ls-files` fails, a subdirectory the scan cannot enter), means this script CANNOT tell a
# generated file from a versioned one — the absence of proof is not proof of absence. Those states
# exit 2, distinct from exit 1 (generated C found), so a caller can tell "the gate ran and caught
# something" apart from "the gate could not be trusted to run at all".
#
# Usage:  sh scripts/no_c_in_tests_gate.sh [PROJECT_DIR]
#         PROJECT_DIR defaults to the current directory — the test session's own working directory.
#         Must be inside a git working tree (any checkout of this repository qualifies).
#
# Exit codes:  0  clean — no generated .c/.h under PROJECT_DIR
#              1  generated .c/.h found — named on stderr
#              2  FATAL — the check itself could not be trusted (see the message on stderr)
set -eu

log() { printf '%s\n' "no-c-in-tests: $*" >&2; }

# fatal MESSAGE... — prints every argument as its own log line and exits 2. The one exit path for
# "this scan cannot be trusted", so every caller of it looks the same on the page.
fatal() {
    for f_line in "$@"; do log "$f_line"; done
    exit 2
}

PROJECT_DIR="${1:-$PWD}"

[ -d "$PROJECT_DIR" ] || fatal \
    "FATAL: '$PROJECT_DIR' is not a directory (or is unreadable)." \
    "A test session this script cannot see is not proven clean, it is UNPROVEN — refusing to report a pass."

PROJECT_DIR="$(cd "$PROJECT_DIR" && pwd)" || fatal "FATAL: cannot resolve '$1' to an absolute path"

ROOT="$(cd "$PROJECT_DIR" && git rev-parse --show-toplevel 2>/dev/null)" || ROOT=""
[ -n "$ROOT" ] || fatal \
    "FATAL: '$PROJECT_DIR' is not inside a git working tree." \
    "This gate tells a GENERATED .c/.h apart from a VERSIONED one by asking git which files are" \
    "tracked; with no repository there is no way to make that distinction, and guessing 'probably" \
    "fine' is exactly the silent pass this gate exists to end."

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT INT TERM

# ── the tracked set: every .c/.h git already knows about (the legitimate, versioned C) ─────────
if ! (cd "$ROOT" && git ls-files -- '*.c' '*.h') >"$W/tracked.raw" 2>"$W/tracked.err"; then
    fatal "FATAL: 'git ls-files' failed in '$ROOT':" "$(sed 's/^/  /' "$W/tracked.err")"
fi
LC_ALL=C sort "$W/tracked.raw" > "$W/tracked"

# ── the present set: every .c/.h actually sitting under PROJECT_DIR right now ──────────────────
# `find`'s OWN exit status is captured before anything pipes it further — a permission error on a
# subdirectory it cannot enter must fail this script, and laundering that exit code through a
# `sed | sort` pipeline (whose own exit status only reflects the LAST stage) would have hidden it.
if ! find "$PROJECT_DIR" \( -name '*.c' -o -name '*.h' \) -not -path '*/.git/*' \
        >"$W/present.raw" 2>"$W/present.err"; then
    fatal "FATAL: could not fully scan '$PROJECT_DIR' for .c/.h files:" "$(sed 's/^/  /' "$W/present.err")"
fi
sed "s|^$ROOT/||" "$W/present.raw" | LC_ALL=C sort > "$W/present"

# ── the verdict: present, but not tracked, is GENERATED ─────────────────────────────────────────
comm -13 "$W/tracked" "$W/present" > "$W/generated"

if [ -s "$W/generated" ]; then
    n="$(wc -l < "$W/generated" | tr -d ' \t')"
    log "FAIL — $n generated .c/.h file(s) survived a native-backend test session:"
    while IFS= read -r gc_file; do log "  $gc_file"; done < "$W/generated"
    log "A native backend must never touch cc. Fix the emission site named above — or, if this"
    log "leg genuinely still runs the C route, this gate must not be wired onto it."
    exit 1
fi

log "PASS — no generated .c/.h under '$PROJECT_DIR' (native backend, zero C, ruling 2026-07-29)."
exit 0
