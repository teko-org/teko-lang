#!/usr/bin/env sh
# scripts/native_linux_asset.sh — build ONE published Linux asset from the emitted teko.c with
# the TARGET'S OWN NATIVE TOOLCHAIN. No cross-compiler. Ever.
#
# THIS FILE REPLACES scripts/cross_compile_linux.sh, WHICH IS DELETED (owner order 2026-07-26:
# "parar de cross compilar em zig, usar compilação nativa na esteira que irá gerar os artefatos"
# and, the same day, "O zig deve morrer agora, imediatamente, sem espera"). Every caller that
# used to reach for `zig cc -target <triple>` now names a LABEL here and gets a binary produced
# by the compiler that actually ships on that platform.
#
# TRANSITIONAL SUBJECT (owner ruling — .31 self-hosting sequence): `teko.c`/`teko.h` exist ONLY
# for the seed→gen1 step; gen2 onward is produced by the NATIVE backend, no C at all. Everything
# this file does — pick a libc, run a C compiler over `teko.c` — is therefore scaffolding for
# gen1, not a permanent shape. The day the native backend emits gen1's own object directly, this
# file's job collapses from "which C compiler, in which container" to "which libc the LINKER
# targets" (owner, 2026-07-26: "não é .c que importa, importa o linker") — a flag, not a
# toolchain selection. Nothing here should be built with more permanence than that future implies.
#
# ── WHAT "NATIVE" MEANS FOR EACH OF THE FOUR LABELS ───────────────────────────────────────────
# The axis is (ARCH x LIBC), and every one of the four is now built by a toolchain ON THE RUNNER
# ITSELF. NO CONTAINER IS INVOLVED ANYWHERE IN THIS FILE (owner order, 2026-07-27: *"o host já é
# declarado ubuntu-latest, logo, é para remover o container, não é pedido nem contra argumento, é
# ordem"*, and the same day *"Sem fallback para container, se eu ver, vou eu mesmo remover"*).
#
#   linux-x86_64-glibc   the RUNNER'S OWN cc,       no container, host CPU (x86_64)
#   linux-arm64-glibc    the RUNNER'S OWN cc,       no container, host CPU (arm64 runner)
#   linux-x86_64-musl    the RUNNER'S OWN musl-gcc, no container, host CPU (x86_64)
#   linux-arm64-musl     the RUNNER'S OWN musl-gcc, no container, host CPU (arm64)
#
# ── THE glibc FLOOR: WHAT WAS TRADED, STATED PLAINLY ──────────────────────────────────────────
# A dynamically linked glibc binary carries a FLOOR — it will not start on a system whose glibc
# is older than the one it was linked against. This file used to build the glibc assets inside
# `quay.io/pypa/manylinux_2_28_*` precisely to PIN that floor at 2.28, and the release notes
# promised it. Building with the runner's own cc raises the floor to the runner's glibc.
#
# THAT IS THE ACCEPTED, AUTHORIZED COST, not an oversight (owner, 2026-07-27): *"Pq não me
# importo com o piso do glibc, pq é transitório, na próxima versão mataremos ele e usaremos
# apenas linker, depois entra o nosso linker e tudo isso vai por terra, sem mais dependências de
# tooling que não o próprio teko"*. The floor is a promise about a SCAFFOLD — the whole glibc
# asset exists only while gen1 still goes through C — so pinning it with a container was buying
# permanence for something already scheduled to die.
#
# THE FLOOR IS THEREFORE REPORTED, NEVER PINNED: every asset's PROVENANCE.txt records the exact
# `libc=` its toolchain linked against, so the floor a given asset actually carries is a MEASURED
# FACT in the published artifact rather than a claim in a release note. What must never happen
# again is the reverse — a promised floor kept by a container while nobody checks it.
#
# ── WHY EVERY LABEL IS NATIVE, AND WHAT THAT COSTS ────────────────────────────────────────────
# musl's published promise is "fully static": the binary carries its own libc, so there is no
# floor to protect at all. `musl-tools` (musl-gcc) comes from the runner's OWN package repository
# (Ubuntu noble/universe — see PROVENANCE.txt's `toolchain_libc` for what actually built a given
# asset). `musl-gcc` and Alpine's gcc are both, ultimately, gcc linked against musl; the
# difference this file cares about is which ONE built the shipped bytes, and that is recorded,
# never assumed.
#
# MEASURED, so nobody sells the container's removal as a speed-up: the image pull was ~1% of the
# producing job's wall clock (~7s on x86_64, ~4s on arm64). Removing it buys a DEPENDENCY, not
# time. The job's cost is dominated by the shared ladder — 83.1% on x86_64, 70.2% on arm64 — and
# that is why the ladder is a job of its own that the four asset legs consume (see
# scripts/produce_assets.sh and .github/workflows/pr.yml).
#
# musl-tools INSTALLS ON THE arm64 RUNNER — measured green on `ubuntu-24.04-arm` across three
# separate runs on 2026-07-27, so this is a confirmed fact and no longer the assertion an earlier
# revision of this header called it. If it ever regresses, this file FAILS THE JOB LOUD
# (`ensure_musl_toolchain`). There is no container to fall back to and none is coming back.
#
# ── REQUIREMENTS ──────────────────────────────────────────────────────────────────────────────
#   * glibc labels: `cc` on PATH (present on every GitHub-hosted Linux runner), and the CWD must
#     be the repo root with <teko_c>/<src_dir> inside it.
#   * musl labels: `musl-gcc` on PATH (installed by the caller's workflow step — see pr.yml /
#     nightly.yml's "Install musl-tools" step — or already present on a hand-run host).
#
# Usage: native_linux_asset.sh <label> <teko_c> <src_dir>
#   label     one of the four labels above
#   teko_c    path to the emitted teko.c (repo-relative, or absolute under the CWD)
#   src_dir   the repo's src/ (teko_rt.*, assert.*)
#
# The asset lands at ./gd-<label>/teko — the SAME layout cross_compile_linux.sh used, so every
# consumer (the staging step, package_release.sh) is unchanged by the toolchain swap.
#
# POSIX sh only.
set -eu

LABEL="${1:?usage: native_linux_asset.sh <label> <teko_c> <src_dir>}"
TEKO_C="${2:?missing teko_c}"
SRC="${3:?missing src_dir}"

log() { printf '%s\n' "native_linux_asset: $*" >&2; }

# rel PATH — echo PATH made relative to the CWD, refusing anything outside it. Every cc line below
# is built from repo-relative paths, so a path outside the tree is a caller mistake worth naming
# here rather than letting it fail deep inside gcc as a missing file.
rel() {
    case "$1" in
        /*)
            case "$1" in
                "$PWD"/*) printf '%s' "${1#"$PWD"/}" ;;
                *) log "'$1' is outside the working directory '$PWD'"; exit 1 ;;
            esac ;;
        *) printf '%s' "$1" ;;
    esac
}

R_TEKO_C="$(rel "$TEKO_C")"
R_SRC="$(rel "$SRC")"
[ -f "$R_TEKO_C" ] || { log "no emitted C at '$R_TEKO_C'"; exit 1; }
[ -d "$R_SRC/runtime" ] || { log "'$R_SRC' does not look like the repo's src/ (no runtime/)"; exit 1; }

# The embedded `teko --version` reads -DTEKO_VERSION_STRING at build time (teko_rt.c stringizes
# it), exactly as teko's own run_cc does (src/build/project.tks). A hand-rolled cc line must pass
# the SAME define or `--version` reports the 0.0.0.0-dev fallback instead of the manifest version
# — and scripts/cli_flags_test.sh, which runs against the PUBLISHED asset, asserts the manifest
# string exactly. derive_version.sh yields the git TAG `v<version>[-<suffix>]`; the binary reports
# the tag MINUS the leading `v`. The value is a BARE token — embedded quotes broke Windows arg
# re-parsing (project.tks).
TEKO_VERSION_TAG="$(sh scripts/derive_version.sh 2>/dev/null || echo v0.0.0.0-dev)"
TEKO_VERSION_STRING="${TEKO_VERSION_TAG#v}"

case "$LABEL" in
    linux-x86_64-glibc|linux-x86_64-musl) ARCH_KW="x86-64" ;;
    linux-arm64-glibc|linux-arm64-musl)   ARCH_KW="aarch64" ;;
    *)
        log "'$LABEL' is not one of the four Linux labels"
        log "  linux-{x86_64,arm64}-{glibc,musl}"
        exit 1 ;;
esac

GD="gd-$LABEL"
rm -rf "$GD"; mkdir -p "$GD"

# sha256_of FILE — same portable digest helper scripts/produce_assets.sh uses, duplicated here
# rather than sourced: this file has no dependency on that one and should not gain one just to
# share four lines.
sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        printf '%s' "no-digest-tool"
    fi
}

# heartbeat_start LABEL — begins a background "still working" pulse every 15s, and echoes its
# PID. THE DEFECT THIS CLOSES: `gcc`/`musl-gcc` run with `-w` and print NOTHING on success, so a
# 68-77s compile of the emitted teko.c produced zero log lines end to end — indistinguishable
# from a hung build to anyone who had not already memorised that silence is normal here. The
# watcher makes the silence itself a reported fact instead of an absence of facts.
heartbeat_start() {
    hb_label="$1"
    # `>/dev/null </dev/null` on the backgrounded subshell is NOT decoration: `heartbeat_start` is
    # always called as `x="$(heartbeat_start …)"`, and a background job that inherits the
    # command substitution's pipe on its OWN stdout keeps that pipe's write end open for as long
    # as the job runs — which is deliberately past the `$(...)` returning. Without this, the
    # substitution never sees EOF and the caller hangs forever waiting for a PID it already has.
    # stderr is left alone on purpose: it is not part of the pipe the command substitution reads.
    (
        hb_elapsed=0
        while :; do
            sleep 15
            hb_elapsed=$((hb_elapsed + 15))
            printf 'native_linux_asset: %s — still compiling (elapsed %ds; silence is expected, -w suppresses warnings)\n' "$hb_label" "$hb_elapsed" >&2
        done
    ) </dev/null >/dev/null &
    printf '%s' "$!"
}

# heartbeat_stop PID — ends the watcher started above, INCLUDING its current `sleep`. A non-
# interactive `sh script.sh` runs with job control off, so the subshell and every `sleep` it
# forks share this SCRIPT's OWN process group (measured: killing only the subshell PID leaves
# `sleep` running, reparented to init, for the REST of its 15s — still holding this script's
# stdout/stderr open the whole time, which is exactly the fd a log reader or `tail`-style
# consumer blocks on waiting for EOF). Killing the subshell's own children first, THEN the
# subshell itself, closes every fd the watcher held. Errors are swallowed throughout: the watcher
# may already be gone (killed by job cancellation, or reaped by a shell that got there first),
# and that is not a build failure.
heartbeat_stop() {
    for hs_child in $(pgrep -P "$1" 2>/dev/null || true); do
        kill "$hs_child" >/dev/null 2>&1 || true
    done
    kill "$1" >/dev/null 2>&1 || true
    wait "$1" 2>/dev/null || true
}

# ── glibc: the runner's OWN cc, no container ──────────────────────────────────────────────────

# glibc_version — the runner glibc's own version string, which is THE FLOOR the produced asset
# will carry. Read from `ldd --version`'s first line. This is reported, not pinned (see the
# header): PROVENANCE.txt carries it so the floor is a measured fact in the published artifact.
glibc_version() {
    (ldd --version 2>/dev/null | head -n1) || printf '%s' "unknown"
}

build_glibc() {
    ensure_cc_toolchain

    echo "=== $LABEL — native build with the runner's own cc (no container) ==="

    # `-std=c2x` not `-std=c23`: gcc only learned the `c23` spelling in 14, and this set spans
    # older stable gcc through 14. `-w` because the emitted C is machine-generated and its
    # warnings are not a signal any human acts on. `-O2` because every PUBLISHED asset is a
    # release link (teko's own `--release` passes it), so an asset built without it would not be
    # the asset.
    #
    # `-ldl` is KEPT, and deliberately. `teko_rt.c`'s `tk_obs_dump_table` calls `dladdr`, which
    # lives in `libdl` up to glibc 2.33 and was folded INTO `libc` at 2.34. The runner's glibc is
    # past that fold, so the flag is a no-op there — but it costs nothing, and dropping it would
    # silently make this line wrong for any host still on the pre-fold split.
    bg_cc_line="cc -std=c2x -w -O2 -pthread -DTEKO_VERSION_STRING=$TEKO_VERSION_STRING \
        -I$R_SRC/runtime -I$R_SRC/assert \
        $R_TEKO_C $R_SRC/runtime/teko_rt.c $R_SRC/assert/assert.c -ldl \
        -o $GD/teko"

    {
        echo "image=host (no container)"
        echo "arch=$(uname -m)"
        echo "cc=$(cc --version | head -n1)"
        echo "libc=$(glibc_version)"
    } > "$GD/TOOLCHAIN.txt"
    echo '--- host toolchain ---'
    sed 's/^/    /' "$GD/TOOLCHAIN.txt"

    bg_hb="$(heartbeat_start "$LABEL")"
    bg_build_start="$(date +%s)"
    set +e
    sh -c "$bg_cc_line"
    bg_rc=$?
    set -e
    heartbeat_stop "$bg_hb"
    log "$LABEL compiled in $(($(date +%s) - bg_build_start))s"
    [ "$bg_rc" = "0" ] || { log "FATAL: cc build failed (rc=$bg_rc)"; exit 1; }

    [ -f "$GD/teko" ] || { log "$LABEL: cc reported success but wrote no $GD/teko"; exit 1; }
}

# ── musl: the runner's OWN musl-gcc, no container ─────────────────────────────────────────────

# ensure_cc_toolchain / ensure_musl_toolchain — refuse to proceed silently. Each compiler is
# expected to already be on PATH (`cc` ships on every GitHub-hosted Linux runner; the caller's
# workflow installs `musl-tools` as its own step). These functions' ONLY job is to say so loudly
# and stop if it is not. THERE IS NO CONTAINER FALLBACK ANYWHERE IN THIS FILE AND NONE IS COMING
# BACK — a silent fallback would hide exactly the fact that needs reporting, and a documented one
# would be the "fallback para container" the owner barred outright.
ensure_cc_toolchain() {
    command -v cc >/dev/null 2>&1 && return 0
    log "FATAL: cc is not on PATH."
    log "The glibc asset is built with the runner's own cc — no container path exists here."
    log "Fix the runner image or the toolchain step; do not reintroduce a container."
    exit 1
}

ensure_musl_toolchain() {
    command -v musl-gcc >/dev/null 2>&1 && return 0
    log "FATAL: musl-gcc is not on PATH."
    log "musl-tools installs on both x86_64 and arm64 runners (measured green on ubuntu-24.04-arm"
    log "across three runs, 2026-07-27), so its absence here is a REGRESSION worth reporting, not"
    log "an expected gap. Fix the install step; there is no container to fall back to."
    exit 1
}

# musl_libc_version — the musl runtime's OWN version string, read from its dynamic loader.
# musl's `ldd` has no `--version` flag; the loader prints "musl libc (ARCH)\nVersion X.Y.Z\n…" to
# STDERR when invoked with no arguments (and exits 1 doing it) — THE DEFECT THIS FIXES:
# `2>/dev/null` around that exact invocation is why PROVENANCE.txt's `toolchain_libc` field came
# out empty for the old Alpine path (measured, not assumed — see this file's git history). Merging
# stderr into the pipe this reads, rather than discarding it, is the whole fix.
musl_libc_version() {
    ml_loader="/lib/ld-musl-$(uname -m).so.1"
    [ -e "$ml_loader" ] || { printf '%s' "unknown"; return 0; }
    "$ml_loader" 2>&1 | sed -n 's/^Version //p' | head -n1
}

build_musl() {
    ensure_musl_toolchain

    echo "=== $LABEL — native build with musl-gcc (no container) ==="

    bm_static="-static"
    bm_cc_line="musl-gcc -std=c2x -w -O2 -pthread -DTEKO_VERSION_STRING=$TEKO_VERSION_STRING $bm_static \
        -I$R_SRC/runtime -I$R_SRC/assert \
        $R_TEKO_C $R_SRC/runtime/teko_rt.c $R_SRC/assert/assert.c -ldl \
        -o $GD/teko"

    {
        echo "image=musl-tools (host, no container)"
        echo "arch=$(uname -m)"
        echo "cc=$(musl-gcc --version | head -n1)"
        echo "libc=musl $(musl_libc_version)"
    } > "$GD/TOOLCHAIN.txt"
    echo '--- host toolchain ---'
    sed 's/^/    /' "$GD/TOOLCHAIN.txt"

    bm_hb="$(heartbeat_start "$LABEL")"
    bm_build_start="$(date +%s)"
    set +e
    sh -c "$bm_cc_line"
    bm_rc=$?
    set -e
    heartbeat_stop "$bm_hb"
    log "$LABEL compiled in $(($(date +%s) - bm_build_start))s"
    [ "$bm_rc" = "0" ] || { log "FATAL: musl-gcc build failed (rc=$bm_rc)"; exit 1; }

    [ -f "$GD/teko" ] || { log "$LABEL: musl-gcc reported success but wrote no $GD/teko"; exit 1; }
}

case "$LABEL" in
    linux-x86_64-glibc) build_glibc ;;
    linux-arm64-glibc)  build_glibc ;;
    linux-x86_64-musl)  build_musl ;;
    linux-arm64-musl)   build_musl ;;
esac

# The architecture assertion survives the toolchain swap unchanged: it is what turns "the build
# ran" into "the build produced the thing it was asked for". A wrong-arch result here means the
# toolchain or the platform routing is wrong, and saying so beats shipping it.
file "$GD/teko"
if ! file "$GD/teko" | grep -q "$ARCH_KW"; then
    log "$LABEL produced the WRONG architecture (expected '$ARCH_KW')"
    exit 1
fi

log "$LABEL OK — $GD/teko built ($(sha256_of "$GD/teko"), $(wc -c < "$GD/teko" | tr -d ' ') bytes)"
