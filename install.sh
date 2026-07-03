#!/bin/sh
# install.sh — installer for the Teko compiler (`teko`), macOS + Linux.
#
# Two install paths:
#   1. from-release (DEFAULT) — download the prebuilt `teko-<label>.tar.gz` from the
#      latest GitHub release, verify its sha256 against SHA256SUMS.txt, extract, strip
#      the macOS quarantine flag, and install the binary to a prefix on your PATH.
#   2. from-source (--from-source, or AUTOMATIC when no prebuilt asset exists for this
#      architecture — e.g. Intel macOS x86_64) — build `teko` with the system C compiler,
#      preferring CMake in a teko-lang checkout, else the portable bootstrap-src bundle.
#
# Quick start:
#   curl -fsSL https://raw.githubusercontent.com/schivei/teko-lang/chore/reboot/install.sh | sh
#
# Options:
#   --from-source        force the from-source build path
#   --version <tag>      install a specific release tag (default: latest)
#   --prefix <dir>       install directory (default: /usr/local/bin, else ~/.local/bin)
#   --uninstall          remove the installed `teko` binary
#   --help               show this help
#
# Environment overrides:
#   TEKO_VERSION   same as --version <tag>
#   PREFIX         same as --prefix <dir>
#   CC             C compiler to use for the from-source build (default: cc)
#
# POSIX sh only — no bashisms. Safe to pipe from curl.
set -eu

REPO="schivei/teko-lang"
REPO_URL="https://github.com/${REPO}"
RAW_BRANCH="chore/reboot"

# ── configurable state (filled from flags / env) ─────────────────────────────
VERSION="${TEKO_VERSION:-}"       # release tag, empty = latest
PREFIX="${PREFIX:-}"              # install dir, empty = auto-resolve
FROM_SOURCE=0
DO_UNINSTALL=0
BIN_NAME="teko"

# ── tiny helpers ─────────────────────────────────────────────────────────────
log()  { printf '%s\n' "teko-install: $*" >&2; }
err()  { printf '%s\n' "teko-install: error: $*" >&2; }
die()  { err "$*"; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

usage() {
    cat >&2 <<EOF
Teko installer — installs the \`teko\` compiler on macOS and Linux.

Usage:
  install.sh [options]

Options:
  --from-source      build teko from source instead of downloading a release
  --version <tag>    install a specific release tag (default: latest)
  --prefix <dir>     install directory (default: /usr/local/bin, else ~/.local/bin)
  --uninstall        remove the installed teko binary and exit
  --help             show this help and exit

Environment:
  TEKO_VERSION   same as --version
  PREFIX         same as --prefix
  CC             C compiler for the from-source build (default: cc)

Examples:
  curl -fsSL https://raw.githubusercontent.com/${REPO}/${RAW_BRANCH}/install.sh | sh
  ./install.sh --version 0.0.1.3-bootstrap
  ./install.sh --from-source --prefix "\$HOME/.local/bin"
  ./install.sh --uninstall
EOF
}

# ── argument parsing (POSIX getopts can't do long options, so parse by hand) ──
while [ "$#" -gt 0 ]; do
    case "$1" in
        --from-source)  FROM_SOURCE=1 ;;
        --uninstall)    DO_UNINSTALL=1 ;;
        --help|-h)      usage; exit 0 ;;
        --version)      shift; [ "$#" -gt 0 ] || die "--version needs a tag"; VERSION="$1" ;;
        --version=*)    VERSION="${1#--version=}" ;;
        --prefix)       shift; [ "$#" -gt 0 ] || die "--prefix needs a directory"; PREFIX="$1" ;;
        --prefix=*)     PREFIX="${1#--prefix=}" ;;
        *)              err "unknown option: $1"; usage; exit 2 ;;
    esac
    shift
done

# ── OS / arch detection ──────────────────────────────────────────────────────
# Sets OS (macos|linux), ARCH (x86_64|arm64), LABEL (release asset label, may be empty
# when no prebuilt asset exists for this platform, e.g. macOS x86_64).
detect_platform() {
    uname_s="$(uname -s)"
    uname_m="$(uname -m)"

    case "$uname_s" in
        Darwin) OS="macos" ;;
        Linux)  OS="linux" ;;
        *)      die "unsupported OS: $uname_s (this installer supports macOS and Linux)" ;;
    esac

    case "$uname_m" in
        x86_64|amd64)          ARCH="x86_64" ;;
        arm64|aarch64)         ARCH="arm64" ;;
        *)                     die "unsupported architecture: $uname_m" ;;
    esac

    # Map (OS, ARCH) to the published release label. macOS ships arm64 only; Intel
    # macOS has no prebuilt asset and must build from source (LABEL stays empty).
    LABEL=""
    case "${OS}-${ARCH}" in
        macos-arm64)   LABEL="macos-arm64" ;;
        macos-x86_64)  LABEL="" ;;             # no macos-x86_64 asset → from-source
        linux-x86_64)  LABEL="linux-x86_64" ;;
        linux-arm64)   LABEL="linux-arm64" ;;
    esac
}

# ── prefix resolution ────────────────────────────────────────────────────────
# Choose an install dir: explicit --prefix/PREFIX wins; else /usr/local/bin if writable
# (or creatable), else ~/.local/bin. Exports INSTALL_DIR.
resolve_prefix() {
    if [ -n "$PREFIX" ]; then
        INSTALL_DIR="$PREFIX"
        return
    fi
    default="/usr/local/bin"
    if dir_installable "$default"; then
        INSTALL_DIR="$default"
    else
        INSTALL_DIR="$HOME/.local/bin"
        log "$default is not writable; falling back to $INSTALL_DIR"
    fi
}

# dir_installable DIR — true if we can create files in DIR (existing+writable, or the
# parent lets us create it).
dir_installable() {
    d="$1"
    if [ -d "$d" ]; then
        [ -w "$d" ]
    else
        parent="$(dirname "$d")"
        [ -d "$parent" ] && [ -w "$parent" ]
    fi
}

# ── sha256 (portable: sha256sum on Linux, shasum -a 256 on macOS) ─────────────
sha256_of() {
    f="$1"
    if have sha256sum; then
        sha256sum "$f" | awk '{print $1}'
    elif have shasum; then
        shasum -a 256 "$f" | awk '{print $1}'
    else
        die "no sha256 tool found (need sha256sum or shasum)"
    fi
}

# ── downloader (curl or wget) ────────────────────────────────────────────────
download() {
    url="$1"; out="$2"
    if have curl; then
        curl -fsSL "$url" -o "$out"
    elif have wget; then
        wget -qO "$out" "$url"
    else
        die "no downloader found (need curl or wget)"
    fi
}

# download_ok URL OUT — like download() but returns non-zero instead of aborting when
# the URL is missing (used to probe optional release assets).
download_ok() {
    url="$1"; out="$2"
    if have curl; then
        curl -fsSL "$url" -o "$out" 2>/dev/null
    elif have wget; then
        wget -qO "$out" "$url" 2>/dev/null
    else
        die "no downloader found (need curl or wget)"
    fi
}

# ── temp workspace with guaranteed cleanup ───────────────────────────────────
WORKDIR=""
cleanup() { [ -n "$WORKDIR" ] && rm -rf "$WORKDIR"; }
trap cleanup EXIT INT TERM
mktmp() {
    WORKDIR="$(mktemp -d 2>/dev/null || mktemp -d -t teko-install)"
}

# ── resolve latest release tag via the GitHub API (best effort) ──────────────
latest_tag() {
    api="https://api.github.com/repos/${REPO}/releases/latest"
    tmp="$WORKDIR/latest.json"
    if download_ok "$api" "$tmp"; then
        # Extract "tag_name": "..." without jq.
        tag="$(sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$tmp" | head -n1)"
        [ -n "$tag" ] && { printf '%s' "$tag"; return 0; }
    fi
    return 1
}

# ── install a binary file into INSTALL_DIR ───────────────────────────────────
install_binary() {
    src="$1"
    mkdir -p "$INSTALL_DIR" || die "cannot create install dir: $INSTALL_DIR"
    dest="$INSTALL_DIR/$BIN_NAME"
    cp "$src" "$dest" || die "cannot write $dest (try a different --prefix)"
    chmod +x "$dest"
    # macOS: clear the quarantine flag so Gatekeeper doesn't block the CLI. We do NOT
    # sign/notarize (out of scope for pre-alpha CLI tooling) — stripping quarantine is
    # the sanctioned path for locally-installed command-line tools.
    if [ "$OS" = "macos" ]; then
        xattr -d com.apple.quarantine "$dest" 2>/dev/null || true
    fi
    INSTALLED_BIN="$dest"
}

# ── verify the installed binary actually runs; print PATH hint if needed ──────
finish() {
    log "installed teko to $INSTALLED_BIN"
    # `teko` is project-oriented: with no args it prints a usage banner and exits 2.
    # That banner reaching us is proof the binary loads and runs, so we capture its
    # output and look for the "usage:"/"teko" marker rather than relying on an exit 0.
    probe="$("$INSTALLED_BIN" 2>&1 || true)"
    case "$probe" in
        *usage:*|*"teko compiles"*|*"teko build"*)
            log "verified: $INSTALLED_BIN runs" ;;
        *)
            # No recognized banner. A binary that can't exec at all is fatal; a
            # pre-alpha whose output simply changed is not. Distinguish the two by
            # exit code 127 (command could not be executed).
            "$INSTALLED_BIN" >/dev/null 2>&1 || rc="$?"
            rc="${rc:-0}"
            if [ -x "$INSTALLED_BIN" ] && [ "$rc" != 127 ]; then
                log "note: '$BIN_NAME' is installed and executable (pre-alpha; unrecognized banner)."
            else
                die "installed binary is not executable / did not run: $INSTALLED_BIN"
            fi ;;
    esac

    # PATH hint: is INSTALL_DIR on PATH?
    case ":$PATH:" in
        *":$INSTALL_DIR:"*) : ;;  # already on PATH
        *)
            printf '\n' >&2
            log "'$INSTALL_DIR' is not on your PATH. Add it with:"
            # SC2016: $PATH is meant to stay literal here — it's the line the USER runs.
            # shellcheck disable=SC2016
            printf '\n    export PATH="%s:$PATH"\n\n' "$INSTALL_DIR" >&2
            log "(append that line to your shell profile, e.g. ~/.profile, ~/.zshrc, or ~/.bashrc)"
            ;;
    esac
    log "done. Run 'teko' (with no arguments) to see usage, or 'teko build <projectdir>'."
}

# ── uninstall ────────────────────────────────────────────────────────────────
do_uninstall() {
    detect_platform
    removed=0
    # Search the explicit prefix (if given) plus the two default locations.
    dirs="/usr/local/bin $HOME/.local/bin"
    [ -n "$PREFIX" ] && dirs="$PREFIX $dirs"
    for d in $dirs; do
        target="$d/$BIN_NAME"
        if [ -f "$target" ]; then
            if [ -w "$d" ]; then
                rm -f "$target" && { log "removed $target"; removed=1; }
            else
                err "cannot remove $target (no write permission to $d) — try: sudo rm '$target'"
            fi
        fi
    done
    [ "$removed" -eq 1 ] || log "no installed teko binary found in: $dirs"
}

# ── from-release install path ────────────────────────────────────────────────
install_from_release() {
    [ -n "$LABEL" ] || return 1   # no prebuilt asset for this platform

    mktmp
    tag="$VERSION"
    if [ -z "$tag" ]; then
        if tag="$(latest_tag)"; then
            log "latest release: $tag"
        else
            log "could not resolve a latest release (none published yet?)"
            return 1
        fi
    fi

    base="${REPO_URL}/releases/download/${tag}"
    asset="teko-${LABEL}.tar.gz"
    asset_url="${base}/${asset}"
    sums_url="${base}/SHA256SUMS.txt"

    log "downloading $asset_url"
    if ! download_ok "$asset_url" "$WORKDIR/$asset"; then
        log "release asset not found: $asset_url"
        return 1
    fi

    # Verify checksum against the release's SHA256SUMS.txt. If the sums file is present
    # we REQUIRE a match; a mismatch aborts. If it's absent, we refuse to trust the
    # download and fall back to source.
    if download_ok "$sums_url" "$WORKDIR/SHA256SUMS.txt"; then
        want="$(grep " $asset\$" "$WORKDIR/SHA256SUMS.txt" | awk '{print $1}' | head -n1)"
        if [ -z "$want" ]; then
            # Some checksum files use "*name" (binary marker) or bare names; retry loosely.
            want="$(grep "$asset" "$WORKDIR/SHA256SUMS.txt" | awk '{print $1}' | head -n1)"
        fi
        [ -n "$want" ] || die "no checksum for $asset in SHA256SUMS.txt (refusing to install unverified download)"
        got="$(sha256_of "$WORKDIR/$asset")"
        if [ "$want" != "$got" ]; then
            die "sha256 MISMATCH for $asset
    expected: $want
    got:      $got
Refusing to install a tampered/corrupt download."
        fi
        log "sha256 verified: $got"
    else
        log "SHA256SUMS.txt not available for $tag — cannot verify download, falling back to source"
        return 1
    fi

    # Extract the binary.
    ( cd "$WORKDIR" && tar -xzf "$asset" ) || die "failed to extract $asset"
    [ -f "$WORKDIR/$BIN_NAME" ] || die "$BIN_NAME not found inside $asset"

    resolve_prefix
    install_binary "$WORKDIR/$BIN_NAME"
    finish
    return 0
}

# ── from-source install path ─────────────────────────────────────────────────
# Build order of preference:
#   1. inside a teko-lang checkout with CMakeLists.txt → cmake build target `teko`
#   2. else git clone --depth 1 the repo → cmake build
#   3. else (only a released teko-bootstrap-src.tar.gz) → cc line from build.sh
install_from_source() {
    log "building teko from source with the system C compiler"

    src_root=""
    cloned=0

    # (1) Are we already inside a teko-lang checkout?
    if [ -f "./CMakeLists.txt" ] && [ -f "./main.c" ] && [ -d "./src/runtime" ]; then
        src_root="$(pwd)"
        log "using current checkout: $src_root"
    fi

    # (2) Try the portable source bundle from a release (no git/cmake needed).
    if [ -z "$src_root" ]; then
        mktmp
        tag="$VERSION"
        [ -n "$tag" ] || tag="$(latest_tag 2>/dev/null || true)"
        if [ -n "$tag" ]; then
            bundle_url="${REPO_URL}/releases/download/${tag}/teko-bootstrap-src.tar.gz"
            log "trying portable source bundle: $bundle_url"
            if download_ok "$bundle_url" "$WORKDIR/teko-bootstrap-src.tar.gz"; then
                ( cd "$WORKDIR" && tar -xzf teko-bootstrap-src.tar.gz )
                bdir="$WORKDIR/teko-bootstrap-src"
                if [ -f "$bdir/teko.c" ] && [ -f "$bdir/runtime/teko_rt.c" ]; then
                    build_bundle "$bdir" || die "bundle build failed"
                    resolve_prefix
                    install_binary "$bdir/$BIN_NAME"
                    finish
                    return 0
                fi
            fi
            log "no usable source bundle for $tag"
        fi
    fi

    # (3) No checkout and no bundle → clone the repo.
    if [ -z "$src_root" ]; then
        have git || die "git is required to clone the source (or run this from a teko-lang checkout)"
        [ -n "${WORKDIR:-}" ] || mktmp
        log "cloning $REPO_URL (branch $RAW_BRANCH)"
        git clone --depth 1 --branch "$RAW_BRANCH" "$REPO_URL" "$WORKDIR/teko-lang" \
            || git clone --depth 1 "$REPO_URL" "$WORKDIR/teko-lang" \
            || die "git clone failed"
        src_root="$WORKDIR/teko-lang"
        cloned=1
    fi

    # CMake build of the `teko` target.
    have cmake || die "cmake is required for the from-source build; install cmake or use a released source bundle"
    have "${CC:-cc}" || die "no C compiler found (set CC or install a compiler)"
    build_dir="$src_root/build-install"
    log "cmake configure (Release)"
    cmake -S "$src_root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release >&2 || die "cmake configure failed"
    log "cmake build (target teko)"
    cmake --build "$build_dir" --target teko >&2 || die "cmake build failed"

    built="$build_dir/teko"
    [ -f "$built" ] || built="$(find "$build_dir" -name teko -type f -perm -u+x 2>/dev/null | head -n1)"
    [ -n "$built" ] && [ -f "$built" ] || die "cmake build did not produce a teko binary"

    resolve_prefix
    install_binary "$built"
    # Clean the throwaway build dir we created inside a user checkout (leave clones to
    # the temp cleanup trap).
    [ "$cloned" -eq 0 ] && rm -rf "$build_dir"
    finish
    return 0
}

# build_bundle DIR — compile the portable bootstrap-src bundle with the documented cc
# line (kept byte-identical to the bundle's build.sh / scripts/package_release.sh).
build_bundle() {
    d="$1"
    cc="${CC:-cc}"
    have "$cc" || die "no C compiler found (set CC or install a compiler)"
    log "compiling bundle with: $cc -std=c23 -Iruntime -Iassert teko.c runtime/teko_rt.c assert/assert.c -lm -o teko"
    ( cd "$d" && "$cc" -std=c23 -w -Iruntime -Iassert \
        teko.c runtime/teko_rt.c assert/assert.c -lm -o teko ) \
        || return 1
    [ -f "$d/$BIN_NAME" ]
}

# ── main ─────────────────────────────────────────────────────────────────────
main() {
    if [ "$DO_UNINSTALL" -eq 1 ]; then
        do_uninstall
        exit 0
    fi

    detect_platform
    log "platform: ${OS}-${ARCH}${LABEL:+ (release label: $LABEL)}"

    if [ "$FROM_SOURCE" -eq 1 ]; then
        install_from_source
        exit $?
    fi

    if [ -z "$LABEL" ]; then
        log "no prebuilt binary for ${OS}-${ARCH} (e.g. Intel macOS is source-only) — building from source"
        install_from_source
        exit $?
    fi

    # Default: try the release, gracefully fall back to source.
    if install_from_release; then
        exit 0
    fi
    log "falling back to the from-source build"
    install_from_source
}

main
