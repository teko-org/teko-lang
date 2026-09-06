#!/usr/bin/env sh
# scripts/ci_lane_purity.sh — prove that only the producer legs build, and that zig is dead.
#
# ── WHY THIS IS A GATE STEP AND NOT A ONE-OFF AUDIT ───────────────────────────────────────────
# The owner's ruling of 2026-07-26 — "todas as lanes de pull_request precisam depender dessa lane
# de artefatos, a lane de artefatos deve executar build seco … e ela deve ter a inteligência sobre
# o que deve gerar (a escada)" — was applied once before and drifted back within a wagon: eleven
# `pull_request` jobs were still seeding and self-hosting after the lane existed. A ruling that
# lives only in review comments is a ruling that gets re-litigated by accident. This turns it into
# a check: any job that reaches for the seed, the ladder or a cross toolchain fails the gate that
# runs it.
#
# THE ALLOWED SET IS NOW SIX JOBS, NOT ONE (2026-07-29). There used to be a single `artifact` job
# — a matrix over every producer leg — so "only the root builds" meant "only that one job name may
# call these scripts". Owner correction, 2026-07-29: the matrix was removed (see pr.yml's header)
# so that a red leg stops only its own chain instead of hiding behind a shared aggregate result.
# Each producer leg is now its own job, `artifact-<label>`, and ALL SIX of them legitimately call
# these scripts — so the allowed set passed to this script is the six job names, space-separated
# in one shell word. `check_token`'s membership test already worked on a SET (`case " $allowed "
# in *" $job "*`) before this wagon; only the caller's single name became six.
#
# ── WHAT IS FORBIDDEN, AND WHERE ──────────────────────────────────────────────────────────────
#   produce_assets             the SHARED PRODUCTION PATH   — a producer leg only
#   build_with_seed_fallback   the LADDER                   — a producer leg only
#   ci_provision_teko          the SEED                     — a producer leg only (+ exceptions)
#   cross_compile_linux        the deleted cross script     — nowhere
#   zig                        the deleted toolchain        — nowhere, in ANY workflow or script
#
# `produce_assets` IS THE ONE THAT MATTERS NOW. When the seed/ladder/native-build steps were
# extracted into scripts/produce_assets.sh so that pr.yml and nightly.yml could share one
# production path, the older two tokens stopped appearing in pr.yml at all — and a check that only
# looked for them would have started passing for the wrong reason. Guarding the new name keeps the
# law exactly as strong as it was: whatever a producer leg does to build, no other lane may do.
#
# The zig sweep is deliberately wider than this workflow: the owner's order was "O zig deve morrer
# agora, imediatamente, sem espera, assim que entregar a carga, já deve estar morto", so the check
# covers every workflow and every script, including the `push`-triggered ones this consolidation
# does not otherwise touch.
#
# ── COMMENTS ARE STRIPPED, AND THAT IS LOAD-BEARING ───────────────────────────────────────────
# Every one of these tokens must remain WRITEABLE in prose — the files explain at length why zig
# died and why only a producer leg may seed, and a check that forbade the words would forbid the
# explanation. So full-line comments (`#` as the first non-blank character) are removed before
# scanning. The consequence, stated so nobody trips on it: a forbidden token in a TRAILING comment
# on a line of code is NOT stripped and will fail the check. Put the explanation on its own line.
#
# ── EXCEPTIONS ────────────────────────────────────────────────────────────────────────────────
# `.github/ci-lane-exceptions.txt` lists `<job> <token> # reason` pairs. An exception is a visible,
# reviewable, single-line admission — the opposite of a silent carve-out in the checker. An
# UNDECLARED violation fails; a declared one is printed on every run so it cannot be forgotten.
#
# Usage:  ci_lane_purity.sh <workflow.yml> "<allowed-job>…" [exceptions-file] [scan-dir…]
#   <allowed-job>…  ONE argument: the job names allowed to build, space-separated (quote it as a
#                   single shell word) — the six `artifact-<label>` jobs today.
#
# POSIX sh + awk only.
set -eu

WF="${1:?usage: ci_lane_purity.sh <workflow.yml> \"<allowed-job>...\" [exceptions] [scan-dir...]}"
ROOT="${2:?missing allowed-job(s)}"
EXC="${3:-.github/ci-lane-exceptions.txt}"
# `shift 3` on a 2-argument call is an ERROR IN A SPECIAL BUILTIN, which POSIX says terminates a
# non-interactive shell — `|| true` does not save it (measured: exit 2, no output). Guard on `$#`
# instead, exactly as the deleted cross_compile_linux.sh had to for the same reason.
if [ "$#" -gt 3 ]; then shift 3; SCAN_DIRS="$*"; else SCAN_DIRS=""; fi
[ -n "$SCAN_DIRS" ] || SCAN_DIRS=".github/workflows scripts"

[ -f "$WF" ] || { echo "ci_lane_purity: no such workflow: $WF" >&2; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT INT TERM
FAILED=0

# strip_comments FILE — drop full-line comments, keep everything else verbatim (line numbers are
# preserved by blanking rather than deleting, so a reported line matches the file).
strip_comments() {
    awk '{ if ($0 ~ /^[[:space:]]*#/) print ""; else print }' "$1"
}

# ── 1. per-job scan of the consolidated workflow ──────────────────────────────────────────────
# Tag every line inside the top-level `jobs:` block with the job it belongs to.
strip_comments "$WF" | awk '
  /^jobs:[[:space:]]*$/ { inj = 1; next }
  inj && /^[^[:space:]]/ { inj = 0 }
  inj && /^  [A-Za-z0-9_-]+:[[:space:]]*$/ {
      job = $0; sub(/:[[:space:]]*$/, "", job); sub(/^  /, "", job)
  }
  inj && job != "" { print job "\t" NR "\t" $0 }
' > "$TMP/tagged"

# The exception table: `<job> <token>` pairs, comments and blanks ignored.
: > "$TMP/exc"
if [ -f "$EXC" ]; then
    sed 's/#.*//' "$EXC" | awk 'NF >= 2 { print $1 " " $2 }' | sort -u > "$TMP/exc"
    if [ -s "$TMP/exc" ]; then
        echo "declared lane exceptions (from $EXC):"
        sed 's/^/  /' "$TMP/exc"
    fi
fi

check_token() {
    tok="$1"; allowed="$2"
    hits="$(awk -F'\t' -v t="$tok" '$3 ~ t { print $1 "\t" $2 "\t" $3 }' "$TMP/tagged" || true)"
    [ -n "$hits" ] || return 0
    printf '%s\n' "$hits" | while IFS="$(printf '\t')" read -r job line text; do
        [ -n "$job" ] || continue
        case " $allowed " in
            *" $job "*) echo "  ok   $job:$line uses '$tok' (an allowed producer leg)"; continue ;;
        esac
        if grep -qx "$job $tok" "$TMP/exc"; then
            echo "  DECL $job:$line uses '$tok' — declared exception in $EXC"
            continue
        fi
        echo "ERROR: $WF job '$job' line $line reaches for '$tok' outside the producer legs:" >&2
        printf '       %s\n' "$text" >&2
        echo "VIOLATION" >> "$TMP/violations"
    done
}

: > "$TMP/violations"
echo "--- lane purity: who may build ---"
check_token 'produce_assets'           "$ROOT"
check_token 'build_with_seed_fallback' "$ROOT"
check_token 'ci_provision_teko'        "$ROOT"
check_token 'cross_compile_linux'      ""
[ -s "$TMP/violations" ] && FAILED=1

# ── 2. the zig sweep, over every workflow and every script ────────────────────────────────────
echo "--- zig sweep over: $SCAN_DIRS ---"
ZIG_HITS=0
for d in $SCAN_DIRS; do
    [ -d "$d" ] || continue
    for f in "$d"/*; do
        [ -f "$f" ] || continue
        # THIS FILE IS EXEMPT FROM ITS OWN SWEEP, and the exemption is narrow and named: the
        # checker has to SAY the word it forbids, in code and not only in prose (the grep
        # pattern, the variable, the error message). Nothing else is skipped.
        case "$f" in */ci_lane_purity.sh) continue ;; esac
        # No `-w`: `ziglang.org` in a download URL is exactly the shape that must not slip
        # through, and a word-boundary match would let it (the `l` that follows is a word char).
        out="$(strip_comments "$f" | grep -ni 'zig' || true)"
        if [ -n "$out" ]; then
            echo "ERROR: $f still references zig outside a comment:" >&2
            printf '%s\n' "$out" | sed "s|^|       $f:|" >&2
            ZIG_HITS=1
        fi
    done
done
if [ "$ZIG_HITS" = "1" ]; then
    echo "zig was ordered dead (owner, 2026-07-26): every Linux asset is now produced by the" >&2
    echo "target's own native toolchain via scripts/native_linux_asset.sh." >&2
    FAILED=1
else
    echo "  ok   no functional zig reference in $SCAN_DIRS"
fi

if [ "$FAILED" = "1" ]; then
    echo "ci_lane_purity FAILED."
    exit 1
fi
echo "ci_lane_purity OK — only the producer legs ($ROOT) seed and build; zig is gone."
