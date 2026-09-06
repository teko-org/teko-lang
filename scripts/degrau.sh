#!/usr/bin/env sh
# scripts/degrau.sh — THE ONE ANSWER TO "IS THERE A DECLARED DEGRAU?", sourced by every script
# that could otherwise be tempted to read `bootstrap/teko.c` as an INPUT. It also PUBLISHES the
# `key: value` parsing kernel (`kv_field`) that `scripts/provenance_gate.sh` reuses for a SIBLING
# declaration, `bootstrap/PROVENANCE` — one format, one parser, two questions ("is there a rung?"
# vs "where did this file come from?"). See that gate's header for why they are two files and not
# one.
#
# Owner ruling 2026-07-28: *"só podemos usar teko.c se e somente se identificarmos degrau."*
#
# ── WHY A DECLARATION AND NOT A FILE TEST ─────────────────────────────────────────────────────
# Until this file existed, two scripts asked the same question the same wrong way: *"does
# `bootstrap/teko.c` exist?"* — `build_with_seed_fallback.sh`'s rung -1 and `fixpoint_gate.sh`'s
# mode switch. That test was correct while the C could only ever be an INPUT: the file was only
# committed BECAUSE the released seed could not build the tip, so its presence and the degrau were
# the same event.
#
# The 0.3.1.0 chain breaks that identity. The C is now an OUTPUT, versioned after a green run and
# harvested from gen1 (the last generation that emits C), so it is present in a tree whose seed
# builds it perfectly well. Under the old test that mere presence would re-route gen1 through `cc`
# FOREVER, silently, and nobody would ever see the chain that was actually walked — the loudest
# possible defect wearing the quietest possible clothes.
#
# So the criterion is no longer a file's existence but a HUMAN CLAIM: someone looked at a broken
# chain, understood why the published release cannot reach this tree, wrote it down, and committed
# the C that bridges it. No claim, no bridge — the committed C is data, not a rung.
#
# ── THE FORM: `bootstrap/DEGRAU`, A VERSIONED PLAIN-TEXT DECLARATION ──────────────────────────
#
#     # anything after a hash is a comment
#     c:      bootstrap/teko.c        # REQUIRED — the C that bridges the gap, relative to the root
#     why:    <one sentence>          # REQUIRED — what the published release cannot build, and why
#     since:  0.3.1.0                 # optional — the version that opened the degrau
#
# WHY A FILE AND NOT A MANIFEST FIELD (`teko.tkp`): `teko.tkp` is PRODUCT — the compiler parses it,
# every user project has one, and its keys are a language surface. A bootstrap emergency is not a
# property of the project being built; it is a property of the CI chain that builds it, and putting
# it in the manifest would ask the compiler's own parser to accept a key that exists to work around
# the compiler. A standalone file also shows up in `git status` and in a PR diff as one line of
# prose that a reviewer reads, and it is DELETED in one act the day the degrau closes — which is
# the whole ceremony the owner's rule asks for.
#
# ── EVERY MALFORMED DECLARATION IS FATAL, NEVER "no degrau" ───────────────────────────────────
# A file that exists but does not parse, or that names a C which is not there, is the one state
# that must never degrade quietly: it means a human tried to declare a bridge and the bridge is not
# where they said. Treating that as "no degrau declared" would fall back to a chain we already know
# is broken and report the fallback's failure instead of the declaration's. `degrau_scan` returns 2
# for it, and every caller turns a 2 into an exit.
#
# Usage (POSIX sh, SOURCED — never executed):
#     . "$(dirname "$0")/degrau.sh"
#     DG=0; degrau_scan "$ROOT" || DG=$?
#     [ "$DG" != 2 ] && exit 1        # a broken declaration is fatal for the caller too
#     if [ "$DG" = 0 ]; then ... $DEGRAU_C ... ; else ... the normal chain ... ; fi
#
# Env:
#   TEKO_DEGRAU_FILE  the declaration's path (default: <ROOT>/bootstrap/DEGRAU). For testing the
#                     scanner itself; CI must let it read the versioned file.

DEGRAU_FILE=""
DEGRAU_C=""
DEGRAU_WHY=""
DEGRAU_SINCE=""

degrau_log() { printf '%s\n' "degrau: $*" >&2; }

# kv_field FILE KEY — echoes the first value of `KEY:` in FILE, with comments stripped and
# surrounding blanks trimmed, or the empty string when the key is absent. Blanking a comment rather
# than dropping the line keeps `c: bootstrap/teko.c   # the wagon-15 C` readable as a value. THE
# SHARED KERNEL: any script that speaks this repository's `key: value` declaration format sources
# this file and calls it directly, instead of re-deriving the same three-stage sed pipeline against
# a file of its own — `degrau_field` below is nothing but this, aimed at $DEGRAU_FILE.
kv_field() {
    kf_file="$1"
    kf_key="$2"
    sed -e 's/#.*$//' "$kf_file" \
        | sed -n "s/^[[:space:]]*${kf_key}:[[:space:]]*//p" \
        | sed -e 's/[[:space:]]*$//' \
        | sed -n '1p'
}

# degrau_field KEY — echoes the first value of `KEY:` in $DEGRAU_FILE. See `kv_field`, which does
# the actual parsing; this is the DEGRAU-specific specialisation every caller in this file uses.
degrau_field() {
    kv_field "$DEGRAU_FILE" "$1"
}

# degrau_scan [ROOT] — reads the declaration and publishes it in $DEGRAU_C / $DEGRAU_WHY /
# $DEGRAU_SINCE. Returns 0 when a degrau IS declared and complete, 1 when none is declared (the
# normal chain), 2 when a declaration exists but is broken — which the caller MUST treat as fatal.
degrau_scan() {
    ds_root="${1:-$PWD}"
    DEGRAU_FILE="${TEKO_DEGRAU_FILE:-$ds_root/bootstrap/DEGRAU}"
    DEGRAU_C=""
    DEGRAU_WHY=""
    DEGRAU_SINCE=""
    [ -f "$DEGRAU_FILE" ] || return 1
    DEGRAU_C="$(degrau_field c)"
    DEGRAU_WHY="$(degrau_field why)"
    DEGRAU_SINCE="$(degrau_field since)"
    if [ -z "$DEGRAU_C" ] || [ -z "$DEGRAU_WHY" ]; then
        degrau_log "FATAL: '$DEGRAU_FILE' exists but is not a declaration."
        degrau_log "A degrau needs BOTH a bridge and a reason. Required keys:"
        degrau_log "  c:    bootstrap/teko.c      the C that bridges the gap"
        degrau_log "  why:  <one sentence>        what the published release cannot build"
        degrau_log "Delete the file if there is no degrau — an empty claim is worse than none."
        return 2
    fi
    case "$DEGRAU_C" in
        /*) ;;
        *) DEGRAU_C="$ds_root/$DEGRAU_C" ;;
    esac
    if [ ! -f "$DEGRAU_C" ]; then
        degrau_log "FATAL: '$DEGRAU_FILE' declares a degrau bridged by '$DEGRAU_C', which does not exist."
        degrau_log "The declaration and the bridge are committed together or not at all."
        return 2
    fi
    return 0
}

# degrau_report — prints what the scan found, in the log of the run that acted on it. Called after
# a 0 or a 1, never after a 2 (that path has already said more than this could).
degrau_report() {
    if [ -n "$DEGRAU_C" ]; then
        degrau_log "DECLARED — $DEGRAU_FILE"
        degrau_log "  bridge: $DEGRAU_C"
        degrau_log "  why:    $DEGRAU_WHY"
        [ -n "$DEGRAU_SINCE" ] && degrau_log "  since:  $DEGRAU_SINCE"
        return 0
    fi
    degrau_log "none declared ($DEGRAU_FILE is absent) — the chain starts at the published release"
    return 0
}

# degrau_note_undeclared_c ROOT — says out loud that a committed `teko.c` is being IGNORED as an
# input. Without this line a reader of the log sees a C in the tree, sees a chain that never
# touched it, and has to re-derive the rule from source. Called only when no degrau is declared.
degrau_note_undeclared_c() {
    dn_c="${1:-$PWD}/bootstrap/teko.c"
    [ -f "$dn_c" ] || return 0
    degrau_log "$dn_c is present but NOT declared — it is this train's OUTPUT (harvested from gen1),"
    degrau_log "so it is ignored as an input. To USE it, declare the degrau in bootstrap/DEGRAU."
    return 0
}

# degrau_recipe — the exact act that turns a broken chain into a declared one, printed by the
# scripts that FAIL because the published release cannot reach the tip. A failure that names the
# remedy is the difference between a gate and a wall.
degrau_recipe() {
    degrau_log "If the published release genuinely cannot build this tree, DECLARE THE DEGRAU:"
    degrau_log "  1. commit the C that bridges it (the gen1 C harvested from the last green run)"
    degrau_log "     as bootstrap/teko.c;"
    degrau_log "  2. create bootstrap/DEGRAU with:"
    degrau_log "        c:     bootstrap/teko.c"
    degrau_log "        why:   <what the release cannot build — one sentence>"
    degrau_log "        since: <this version>"
    degrau_log "  3. delete BOTH the day the released seed reaches the tip again."
    return 0
}
