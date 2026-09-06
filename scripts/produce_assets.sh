#!/usr/bin/env sh
# scripts/produce_assets.sh — THE SINGLE PRODUCTION PATH for a published teko asset.
#
# ── WHY IT IS A SCRIPT AND NOT WORKFLOW STEPS ────────────────────────────────────────────────
# Two things now mint the seven published assets: `pr.yml`'s `artifact` root (per PR, so every
# asset is gated) and `nightly.yml` (per push to main, so the release has something to promote).
# If those two carried their own copies of "provision the seed, walk the ladder, dry-build, build
# the native Linux assets, stage them", they would drift — and a drift between what CI proved and
# what the release shipped is exactly the class of defect this train has been paying for. One
# script, two callers, no second path.
#
# WHAT A CALLER STILL OWNS, and why it cannot live here:
#   * `actions/checkout` — this script runs inside the tree.
#   * what to do with the result — pr.yml uploads `stage/` as a run artifact; nightly.yml packages
#     it with scripts/package_release.sh. Producing and publishing are different jobs.
#
# ── WHAT IT DOES, IN ORDER ───────────────────────────────────────────────────────────────────
#   1. Provision the host-runnable seed (scripts/ci_provision_teko.sh) and put it on PATH. The
#      seed label is NOT derived here: the label a producer MINTS is not always the label whose
#      binary RUNS on its runner, so the caller states which released binary does. A
#      derivation the caller can get wrong is a derivation that silently picks the wrong blob.
#   2. THE DRY BUILD + THE LADDER (scripts/build_with_seed_fallback.sh): the newest RELEASED seed
#      builds gen0 and gen0 builds gen1 — the owner's 0.3.1.0 chain, both links down the C route.
#      A DECLARED degrau, the COMMITTED seed in bootstrap/seeds/ or the pinned SHA ladder stand in
#      for the release only when it cannot reach the tip. `--no-verify --release` — DRY, no test
#      gate (the gate is the test layer's whole job) and `-O2` because every published asset is a
#      release link. Leaves out/teko[.exe] AND out/teko.c — that C is GEN1's own emission. Every
#      asset this producer mints comes from that one C, so all of them are the same generation by
#      construction, and it is what pr.yml harvests over a green fixpoint.
#   3. LINUX ONLY: one native build per label (scripts/native_linux_asset.sh) — glibc in a
#      manylinux_2_28 container, musl with the runner's own musl-gcc, no container. When a
#      producer promises MORE THAN ONE Linux label (glibc + musl), they run IN PARALLEL on this
#      one runner (owner ruling 2026-07-27), not serially: musl no longer waits behind glibc's
#      container pull, and there is no second job or artifact hand-off to buy that with — `out/
#      teko.c` never leaves this process's own filesystem. Non-Linux producers are already
#      standing on the target platform, so the dry build IS the asset.
#   4. STAGE: stage/<label>/teko[.exe] — one directory per asset label. That is THE CONTRACT a
#      consumer sees; a producer can change HOW it mints an asset without the consumer noticing.
#
# Usage:  produce_assets.sh <kind> <seed-label> <asset-label>…
#   kind         `linux`  — the assets are built by native_linux_asset.sh from out/teko.c
#                `native` — the runner IS the target; the dry build's own binary is the asset
#   seed-label   the released asset label whose binary RUNS on this runner
#   asset-label… the labels this producer promised to mint
#
# Env:  GH_TOKEN — optional, passed through to ci_provision_teko.sh for API rate-limit relief.
#
# POSIX sh only.
set -eu

# shellcheck source=scripts/ci_phase_clock.sh
. "$(dirname "$0")/ci_phase_clock.sh"
phase_clock_init

KIND="${1:?usage: produce_assets.sh <kind> <seed-label> <asset-label>...}"
SEED="${2:?missing seed-label}"
if [ "$#" -lt 3 ]; then
    echo "produce_assets: a producer must promise at least one asset label" >&2
    exit 1
fi
shift 2
PRODUCES="$*"

log() { printf '%s\n' "produce_assets: $*" >&2; }

# ── THE ARCHITECTURE ASSERTION FOR `native` ASSETS ────────────────────────────────────────────
#
# THE DEFECT THIS CLOSES, measured. A published asset's PROVENANCE.txt recorded a host whose
# machine and a toolchain whose machine DISAGREED — an emulated toolchain minting a PE for the
# machine it emulated, published under the label of the machine it ran on. Nobody chose it: the
# compiler asked for `cc` and the PATH answered.
#
# scripts/native_linux_asset.sh has asserted the architecture of its Linux assets since it was
# written (`file | grep ARCH_KW`). The `native` branch here asserted NOTHING, and that asymmetry
# is the whole hole: the wrong-arch PE passed because no gate on this side ever looked. Closing
# only the compiler's linker probe would leave this end open, and the defect would come back
# through it.
#
# NO SILENT SKIP (M.3). When neither `file` nor the header read can decide, this FAILS: an
# assertion that quietly opts out is indistinguishable from an assertion that passed, and it is
# precisely how the emulated build got published.

# machine_word_of BIN — the target machine of BIN read from its OWN header, with no external
# tool beyond `od` (POSIX, present on macOS and on Git Bash where `file` may not be). Prints a
# stable keyword, or "" when the format is not one this understands.
#   PE:     "MZ" at 0, the PE header offset (e_lfanew) as a little-endian u32 at 0x3C, then the
#           COFF Machine u16 right after the "PE\0\0" signature: 0x8664 x86-64, 0xAA64 ARM64.
#   Mach-O: magic 0xFEEDFACF (64-bit, little-endian on disk as cf fa ed fe) then cputype:
#           0x01000007 x86-64, 0x0100000C ARM64.
#   ELF:    e_machine u16 at 0x12: 0x3E x86-64, 0xB7 aarch64.
machine_word_of() {
    mw_bin="$1"
    command -v od >/dev/null 2>&1 || { printf '%s' ""; return 0; }
    mw_head="$(od -An -tx1 -N4 "$mw_bin" 2>/dev/null | tr -d ' \n')"
    case "$mw_head" in
        4d5a*)
            mw_off_le="$(od -An -tx1 -j60 -N4 "$mw_bin" 2>/dev/null | tr -d ' \n')"
            [ -n "$mw_off_le" ] || { printf '%s' ""; return 0; }
            mw_off_be="$(printf '%s' "$mw_off_le" | cut -c7-8)$(printf '%s' "$mw_off_le" | cut -c5-6)$(printf '%s' "$mw_off_le" | cut -c3-4)$(printf '%s' "$mw_off_le" | cut -c1-2)"
            mw_pe=$((0x$mw_off_be))
            mw_mach="$(od -An -tx1 -j$((mw_pe + 4)) -N2 "$mw_bin" 2>/dev/null | tr -d ' \n')"
            case "$mw_mach" in
                6486) printf '%s' "x86_64" ;;
                64aa) printf '%s' "arm64" ;;
                *)    printf '%s' "" ;;
            esac ;;
        cffaedfe)
            mw_cpu="$(od -An -tx1 -j4 -N4 "$mw_bin" 2>/dev/null | tr -d ' \n')"
            case "$mw_cpu" in
                07000001) printf '%s' "x86_64" ;;
                0c000001) printf '%s' "arm64" ;;
                *)        printf '%s' "" ;;
            esac ;;
        7f454c46)
            mw_em="$(od -An -tx1 -j18 -N2 "$mw_bin" 2>/dev/null | tr -d ' \n')"
            case "$mw_em" in
                3e00) printf '%s' "x86_64" ;;
                b700) printf '%s' "arm64" ;;
                *)    printf '%s' "" ;;
            esac ;;
        *) printf '%s' "" ;;
    esac
}

# arch_keyword_for LABEL — the machine keyword an asset label PROMISES, or "" when the label
# carries no architecture claim this can check.
arch_keyword_for() {
    case "$1" in
        *-x86_64|*-x86_64-*) printf '%s' "x86_64" ;;
        *-arm64|*-arm64-*)   printf '%s' "arm64" ;;
        *) printf '%s' "" ;;
    esac
}

# assert_asset_arch LABEL BIN — BIN must be built for the architecture LABEL promises.
# Applies to the `native` kind; the `linux` kind is already asserted, per target, inside
# scripts/native_linux_asset.sh, and re-asserting there would just duplicate it.
assert_asset_arch() {
    aa_label="$1"; aa_bin="$2"
    [ "$KIND" = "native" ] || return 0
    aa_want="$(arch_keyword_for "$aa_label")"
    if [ -z "$aa_want" ]; then
        log "'$aa_label' names no architecture — nothing to assert"
        return 0
    fi
    aa_got="$(machine_word_of "$aa_bin")"
    if [ -z "$aa_got" ] && command -v file >/dev/null 2>&1; then
        aa_desc="$(file "$aa_bin" 2>/dev/null)"
        case "$aa_desc" in
            *x86-64*|*x86_64*) aa_got="x86_64" ;;
            *aarch64*|*arm64*) aa_got="arm64" ;;
        esac
    fi
    if [ -z "$aa_got" ]; then
        log "FATAL: cannot determine the architecture of '$aa_bin' for label '$aa_label'."
        log "Neither the binary's own header nor 'file' identified it. This does NOT pass by"
        log "default: an unasserted asset is how a PE was once published under a foreign machine."
        exit 1
    fi
    if [ "$aa_got" != "$aa_want" ]; then
        log "FATAL: '$aa_label' promises $aa_want but '$aa_bin' is $aa_got."
        log "A runner that emulates the other architecture, or a toolchain resolved off the PATH"
        log "instead of chosen, produces exactly this. Refusing to stage a mislabelled asset."
        exit 1
    fi
    log "$aa_label architecture asserted: $aa_got"
}

# sha256_of FILE — portable digest (sha256sum on Linux, shasum -a 256 on macOS), the same shape
# scripts/ci_provision_teko.sh uses. Prints `absent` rather than an empty string so a missing file
# can never be mistaken for a matching digest downstream.
sha256_of() {
    [ -f "$1" ] || { printf '%s' "absent"; return 0; }
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        printf '%s' "no-digest-tool"
    fi
}

# size_of FILE — the binary's byte count, `wc -c` being the one tool this needs that is present
# on every host in the matrix (POSIX-mandated, unlike `stat`'s divergent -c/-f flags between GNU
# and BSD). Prints `0` for a missing file rather than failing the caller.
size_of() {
    [ -f "$1" ] || { printf '%s' "0"; return 0; }
    wc -c < "$1" | tr -d ' '
}

case "$KIND" in
    linux|native) ;;
    *) log "kind must be 'linux' or 'native', not '$KIND'"; exit 1 ;;
esac

log "kind=$KIND seed=$SEED produces='$PRODUCES'"

# ── 1. the seed ───────────────────────────────────────────────────────────────────────────────
# ci_provision_teko.sh appends `.seed` to $GITHUB_PATH, which only affects LATER STEPS — this
# script is one step, so it puts the seed on its own PATH as well. That also makes the script
# runnable by hand outside Actions, which is the only way anyone debugs a producer.
#
# PROVISIONING-PRODUCES-NOTHING IS NON-FATAL, and that is the whole recovery this path adds. When
# there is NO published release candidate for the seed source AND no committed seed in
# bootstrap/seeds/, ci_provision_teko.sh exits non-zero having staged nothing. That is not a broken
# producer — it is a release-availability gap, and the tree already carries its own answer: a
# DECLARED degrau (bootstrap/DEGRAU) names the C bridge that
# scripts/build_with_seed_fallback.sh's rung -1 compiles with `cc`, needing no provisioned `teko`
# at all. So a missing seed must not `exit 1` before the ladder; it must fall through to it. The
# FAST PATH is untouched: when a seed IS provisioned, `teko` is on PATH and everything below behaves
# exactly as it did before, the ladder still preferring the released seed and only falling back to
# the degrau when the seed cannot reach the tip.
sh scripts/ci_provision_teko.sh "$SEED" || log "no released/committed seed was available for '$SEED' — proceeding to the declared degrau ladder"
if [ -d .seed ]; then
    PATH="$PWD/.seed:$PATH"
    export PATH
fi

# THE SEED VERSION IS RECORDED, and that is not decoration. gen0 is `seed(tree)` and gen1 is
# `gen0(tree)`, so the emitted teko.c descends from the seed two links up: two runs of the SAME
# tree emit the same C only if they stood on the SAME seed. The doubling does not weaken that
# dependency, it only lengthens it — a different seed still lowers a different gen0, which emits a
# different C. ci_provision_teko.sh deliberately takes the NEWEST released seed ("SEED = the
# NEWEST usable released seed, ALWAYS"), which is a function of WHEN the lane ran, not of the tree.
# On a train that cuts a release per bump, a seed change between a PR's run and the merge push is
# ordinary — and it is the single most likely honest explanation for two builds of one tree
# disagreeing. nightly.yml's reproducibility gate reads this to tell that apart from a real defect.
#
# WHEN NO SEED WAS PROVISIONED, SEED_VERSION DEGRADES TO `degrau` rather than crashing: there is no
# `teko` to interrogate, and the gen0 that build_with_seed_fallback.sh will mint descends from the
# declared C bridge, not from a released seed. Recording `degrau` states that honestly for the
# reproducibility gate instead of pretending a seed version it never stood on.
if command -v teko >/dev/null 2>&1; then
    teko --version
    SEED_VERSION="$(teko --version 2>/dev/null || echo 'unknown')"
else
    log "no 'teko' on PATH after provisioning — the declared degrau (bootstrap/DEGRAU) is the seed of last resort"
    SEED_VERSION="degrau"
fi

# ── 2. the dry build + the ladder ─────────────────────────────────────────────────────────────
sh scripts/build_with_seed_fallback.sh out

[ -f out/teko.c ] || { log "the dry build produced no out/teko.c"; exit 1; }

# ── 3. the Linux assets, IN PARALLEL, each with its target's own native toolchain ──────────────
# THE WALL-CLOCK FIX (owner ruling 2026-07-27): glibc (containerized) and musl (the runner's own
# musl-gcc, no container — see scripts/native_linux_asset.sh) used to run ONE AFTER THE OTHER on
# this same runner, so a producer promising both paid for both in full. `out/teko.c` never leaves
# this process — there is no second job and nothing to hand off — so backgrounding each label's
# build and waiting on all of them is the WHOLE mechanism; nothing beyond the shell's own job
# control is introduced to get it.
#
# Each label's combined stdout+stderr is captured to its own file rather than interleaved live:
# two concurrent builds writing directly to this script's stdout would shuffle their lines
# together mid-word, which is unreadable. The captured log is played back, one label at a time,
# once that label's job has been waited on — so the ORDER in the log is still deterministic even
# though the WORK was not.
if [ "$KIND" = "linux" ]; then
    ASSET_PIDS=""
    for label in $PRODUCES; do
        rm -f "asset-log-$label.txt"
        ( sh scripts/native_linux_asset.sh "$label" out/teko.c src >"asset-log-$label.txt" 2>&1 ) &
        ASSET_PIDS="$ASSET_PIDS $!:$label"
    done
    ASSET_FAILED=0
    for pair in $ASSET_PIDS; do
        pid="${pair%%:*}"; label="${pair#*:}"
        if wait "$pid"; then
            log "asset '$label' — build succeeded"
        else
            log "asset '$label' — BUILD FAILED (log follows)"
            ASSET_FAILED=1
        fi
        echo "--- $label build log ---"
        cat "asset-log-$label.txt"
        rm -f "asset-log-$label.txt"
    done
    [ "$ASSET_FAILED" = 0 ] || { log "one or more Linux asset builds failed"; exit 1; }
    phase_mark "assets (parallel: $PRODUCES)"
fi

# ── 4. stage ──────────────────────────────────────────────────────────────────────────────────
# The loop FAILS if a promised asset is missing. A producer that stages less than it promised
# would otherwise surface as a confusing download error one job away from its cause.
rm -rf stage
for label in $PRODUCES; do
    mkdir -p "stage/$label"
    if [ "$KIND" = "linux" ]; then
        src="gd-$label/teko"
    else
        src="out/teko"
        [ -f out/teko.exe ] && src="out/teko.exe"
    fi
    if [ ! -f "$src" ]; then
        log "promised '$label' but $src does not exist"
        exit 1
    fi
    assert_asset_arch "$label" "$src"
    case "$src" in
        *.exe) cp "$src" "stage/$label/teko.exe" ;;
        *)     cp "$src" "stage/$label/teko" ;;
    esac
    echo "staged $label <- $src"
done

# ── 5. PROVENANCE, ONE FILE PER ASSET ─────────────────────────────────────────────────────────
# The three ANTECEDENTS of the reproducibility law, recorded so the law can be CHECKED instead of
# assumed:
#
#     same tree  +  same seed  +  same toolchain   ⇒   same bytes
#
# The tree is the checkout. The seed is recorded above. The toolchain is per-label: for `linux`
# producers scripts/native_linux_asset.sh captured the container's own image and gcc; for `native`
# producers the runner's own cc IS the toolchain — and that one is pinned by nothing, it drifts
# with the runner image.
#
# `teko_c_sha256` is the discriminator that makes a failure DIAGNOSABLE rather than merely red.
# teko.c is what gen0 emitted while producing gen1; each asset is that C compiled. So when two
# runs disagree:
#     teko.c differs                → the FRONT half moved (a different seed, or a different rung)
#     teko.c matches, bytes differ  → the BACK half moved (the C toolchain)
# One comparison and the cause is already bisected.
#
# ONE FILE PER LABEL, not one per producer, and that is load-bearing: nightly.yml's gate merges
# every producer's upload into one tree, and a `stage/PROVENANCE.txt` at the root would collide
# across producers and silently keep only the last. Labels are unique, so `stage/<label>/` is not
# just tidier — it is the only layout that survives the merge.
for label in $PRODUCES; do
    bin="stage/$label/teko"
    [ -f "$bin" ] || bin="stage/$label/teko.exe"
    {
        echo "label=$label"
        echo "producer_kind=$KIND"
        echo "seed_label=$SEED"
        echo "seed_version=$SEED_VERSION"
        echo "manifest_tag=$(sh scripts/derive_version.sh 2>/dev/null || echo unknown)"
        echo "host_uname=$(uname -srm 2>/dev/null || echo unknown)"
        echo "teko_c_sha256=$(sha256_of out/teko.c)"
        echo "asset_sha256=$(sha256_of "$bin")"
        if [ -f "gd-$label/TOOLCHAIN.txt" ]; then
            sed 's/^/toolchain_/' "gd-$label/TOOLCHAIN.txt"
        else
            echo "toolchain_image=runner"
            echo "toolchain_cc=$( (cc --version 2>/dev/null || clang --version 2>/dev/null || gcc --version 2>/dev/null) | head -n1 || echo unknown)"
        fi
    } > "stage/$label/PROVENANCE.txt"
    echo "--- stage/$label/PROVENANCE.txt ---"
    cat "stage/$label/PROVENANCE.txt"
    # THE ONE LINE A READER ACTUALLY WANTS: which asset, how big, which bytes — without opening
    # PROVENANCE.txt. This is the "uma linha por asset com tamanho e sha256" this wagon asked for.
    printf 'asset summary: %-24s size=%12s bytes  sha256=%s\n' "$label" "$(size_of "$bin")" "$(sha256_of "$bin")"
done

ls -lR stage

phase_mark "stage"
log "OK — staged: $PRODUCES"
