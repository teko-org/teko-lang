#!/bin/sh
# install.sh — installer for the Teko compiler (`teko`), macOS + Linux.
#
# from-release ONLY (ruling 2026-07-04, issue #157): this installer is not a toolchain —
# it downloads the prebuilt `teko-<label>.tar.gz` from a GitHub release (default: the
# latest published one), verifies its sha256 against SHA256SUMS.txt, extracts, strips the
# macOS quarantine flag, and installs the binary to a prefix on your PATH. It also stages
# that release's `teko-bootstrap-src.tar.gz` runtime/assert/win32_compat.h sources under a
# `share/teko` dir mirroring the chosen prefix — this is what lets `teko build` on the
# installed binary find its C runtime outside a teko-lang checkout (see `rt_dir()` /
# `probe_share_rt_dir()` in src/build/project.tks). A platform with no published asset is
# an honest error listing the assets that DO exist — never a silent from-source build.
#
# Quick start:
#   curl -fsSL https://raw.githubusercontent.com/teko-org/teko-lang/main/install.sh | sh
#
# Options:
#   --version <tag>      install a specific release tag (default: latest)
#   --prefix <dir>       install directory (default: /usr/local/bin, else ~/.local/bin)
#   --uninstall          remove the installed `teko` binary (and its share/teko runtime)
#   --help               show this help
#
# Environment overrides:
#   TEKO_VERSION   same as --version <tag>
#   PREFIX         same as --prefix <dir>
#
# POSIX sh only — no bashisms. Safe to pipe from curl.
set -eu

REPO="teko-org/teko-lang"
REPO_URL="https://github.com/${REPO}"
RAW_BRANCH="main"

# ── configurable state (filled from flags / env) ─────────────────────────────
VERSION="${TEKO_VERSION:-}"       # release tag, empty = latest
PREFIX="${PREFIX:-}"              # install dir, empty = auto-resolve
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
  --version <tag>    install a specific release tag (default: latest)
  --prefix <dir>     install directory (default: /usr/local/bin, else ~/.local/bin)
  --uninstall        remove the installed teko binary and exit
  --help             show this help and exit

Environment:
  TEKO_VERSION   same as --version
  PREFIX         same as --prefix

Examples:
  curl -fsSL https://raw.githubusercontent.com/${REPO}/${RAW_BRANCH}/install.sh | sh
  ./install.sh --version 0.0.1.3-bootstrap
  ./install.sh --prefix "\$HOME/.local/bin"
  ./install.sh --uninstall
EOF
}

# ── argument parsing (POSIX getopts can't do long options, so parse by hand) ──
while [ "$#" -gt 0 ]; do
    case "$1" in
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
# Sets OS (macos|linux), ARCH (x86_64|arm64|riscv64), LIBC (glibc|musl, Linux only), and
# LABEL (release asset label, empty when no prebuilt asset is published for this platform —
# e.g. Intel macOS).
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
        riscv64)               ARCH="riscv64" ;;
        *)                     die "unsupported architecture: $uname_m" ;;
    esac

    # Linux ships glibc (dynamic) and musl (static) per arch. Pick the C library this system
    # actually uses so the binary's dynamic deps resolve: musl distros (Alpine) get the
    # static musl build; everything else gets glibc. TEKO_LIBC overrides the auto-detection.
    LIBC=""
    if [ "$OS" = "linux" ]; then
        LIBC="${TEKO_LIBC:-}"
        if [ -z "$LIBC" ]; then
            if [ -n "$(ls /lib/ld-musl-* 2>/dev/null)" ] || (ldd --version 2>&1 | grep -qi musl); then
                LIBC="musl"
            else
                LIBC="glibc"
            fi
        fi
    fi

    # Map (OS, ARCH[, LIBC]) to the published release label. macOS ships arm64 only today.
    LABEL=""
    case "$OS" in
        macos)  [ "$ARCH" = "arm64" ] && LABEL="macos-arm64" ;;
        linux)  LABEL="linux-${ARCH}-${LIBC}" ;;
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
cleanup() { if [ -n "$WORKDIR" ]; then rm -rf "$WORKDIR"; fi; }
trap cleanup EXIT INT TERM
mktmp() {
    WORKDIR="$(mktemp -d 2>/dev/null || mktemp -d -t teko-install)"
}

# ── resolve latest release tag via the GitHub API (best effort) ──────────────
# Prefers the latest STABLE release; falls back to the newest prerelease when no stable
# exists yet (pre-alpha: every release is `-alpha`), so `curl | sh` with no args works.
latest_tag() {
    # 1) latest STABLE: GitHub's /releases/latest excludes prereleases and drafts.
    api="https://api.github.com/repos/${REPO}/releases/latest"
    tmp="$WORKDIR/latest.json"
    if download_ok "$api" "$tmp"; then
        # Extract "tag_name": "..." without jq.
        tag="$(sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$tmp" | head -n1)"
        [ -n "$tag" ] && { printf '%s' "$tag"; return 0; }
    fi
    # 2) no stable yet — newest published release of ANY kind, BY VERSION. The /releases
    # API is NOT version-ordered (its `[0]` can be a stale 0.0.1.9 ahead of 0.0.1.17), so
    # filter to MAJOR.MINOR.PATCH.BUILD tags and take the highest via `sort -V`. Honest
    # notice on stderr (stdout is the tag the caller captures).
    api="https://api.github.com/repos/${REPO}/releases?per_page=100"
    tmp="$WORKDIR/releases.json"
    if download_ok "$api" "$tmp"; then
        tag="$(sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$tmp" \
            | grep -E '^v?[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+' \
            | awk '{ orig=$0; ver=$0; sub(/^v/,"",ver); print ver"\t"orig }' \
            | sort -V | tail -n1 | cut -f2)"
        [ -n "$tag" ] && {
            log "no stable release published yet — installing the latest prerelease: $tag"
            printf '%s' "$tag"
            return 0
        }
    fi
    return 1
}

# list_release_assets TAG — best-effort list of asset names published under TAG, for the
# honest "no asset for your platform" error. Empty output when the API call fails.
list_release_assets() {
    tag="$1"
    api="https://api.github.com/repos/${REPO}/releases/tags/${tag}"
    tmp="$WORKDIR/assets.json"
    download_ok "$api" "$tmp" || return 0
    sed -n 's/.*"name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$tmp"
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

# share_dir_for_prefix PREFIX — the `share/teko` dir mirroring an install prefix (issue
# #157): `/usr/local/bin` → `/usr/local/share/teko`; anything under `$HOME` (e.g. the
# `~/.local/bin` fallback) → `$HOME/.local/share/teko`; any other custom --prefix mirrors
# the same `<prefix-parent>/share/teko` shape. This is where the native-build runtime
# (`teko_rt.{c,h}`, `assert/`, `win32_compat.h`) lives once installed — `rt_dir()`
# (src/build/project.tks) probes it as a last resort so a project built with the
# installed binary can still find its C runtime.
share_dir_for_prefix() {
    prefix="$1"
    case "$prefix" in
        "$HOME"/*) printf '%s' "$HOME/.local/share/teko" ;;
        *)         printf '%s' "/usr/local/share/teko" ;;
    esac
}

# install_share_runtime TAG — download this release's `teko-bootstrap-src.tar.gz` and
# extract its `runtime/`, `assert/`, and `win32_compat.h` into the share dir mirroring
# INSTALL_DIR (issue #157). Best-effort: a missing/broken bundle logs a warning and
# leaves the binary installed — `teko build` still works for a project whose OWN
# checkout happens to carry `src/runtime` (or `TK_RT_DIR` is set by hand); only a
# from-scratch native build on a clean machine needs this share dir.
install_share_runtime() {
    tag="$1"
    bundle_url="${REPO_URL}/releases/download/${tag}/teko-bootstrap-src.tar.gz"
    bundle_tar="$WORKDIR/teko-bootstrap-src.tar.gz"

    log "downloading $bundle_url (native-build runtime)"
    if ! download_ok "$bundle_url" "$bundle_tar"; then
        log "warning: no teko-bootstrap-src.tar.gz for $tag — 'teko build' may not find its C runtime outside a checkout"
        return 1
    fi

    # Verify against the SHA256SUMS.txt already fetched for the binary (same release):
    # present but no matching entry → refuse (tampered/corrupt); absent entirely → the
    # binary install already logged that and fell back, so just skip verification here.
    if [ -f "$WORKDIR/SHA256SUMS.txt" ]; then
        want="$(grep "teko-bootstrap-src.tar.gz\$" "$WORKDIR/SHA256SUMS.txt" | awk '{print $1}' | head -n1)"
        if [ -n "$want" ]; then
            got="$(sha256_of "$bundle_tar")"
            if [ "$want" != "$got" ]; then
                log "warning: sha256 MISMATCH for teko-bootstrap-src.tar.gz (expected $want, got $got) — skipping share install"
                return 1
            fi
            log "sha256 verified: $got"
        fi
    fi

    ( cd "$WORKDIR" && tar -xzf teko-bootstrap-src.tar.gz ) || { log "warning: failed to extract teko-bootstrap-src.tar.gz"; return 1; }
    bundle_dir="$WORKDIR/teko-bootstrap-src"
    if [ ! -d "$bundle_dir/runtime" ] || [ ! -d "$bundle_dir/assert" ] || [ ! -f "$bundle_dir/win32_compat.h" ]; then
        log "warning: teko-bootstrap-src.tar.gz is missing runtime/assert/win32_compat.h — skipping share install"
        return 1
    fi

    share_dir="$(share_dir_for_prefix "$INSTALL_DIR")"
    mkdir -p "$share_dir" || { log "warning: cannot create $share_dir — skipping share install"; return 1; }
    cp -R "$bundle_dir/runtime" "$share_dir/runtime" || { log "warning: cannot write $share_dir/runtime"; return 1; }
    cp -R "$bundle_dir/assert" "$share_dir/assert" || { log "warning: cannot write $share_dir/assert"; return 1; }
    cp "$bundle_dir/win32_compat.h" "$share_dir/win32_compat.h" || { log "warning: cannot write $share_dir/win32_compat.h"; return 1; }
    log "installed native-build runtime to $share_dir"
    return 0
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
    remove_share_runtime "$dirs"
}

# remove_share_runtime DIRS — best-effort cleanup of the share/teko runtime this
# installer stages (issue #157), mirroring each bin dir in DIRS via share_dir_for_prefix.
remove_share_runtime() {
    dirs="$1"
    for d in $dirs; do
        share_dir="$(share_dir_for_prefix "$d")"
        if [ -d "$share_dir" ]; then
            if [ -w "$(dirname "$share_dir")" ]; then
                rm -rf "$share_dir" && log "removed $share_dir"
            else
                err "cannot remove $share_dir (no write permission) — try: sudo rm -rf '$share_dir'"
            fi
        fi
    done
}

# no_asset_error TAG — an honest error when this platform has no published release asset:
# list whatever assets DO exist for TAG and point at the support issue. Never falls back
# to a silent build (ruling 2026-07-04, issue #157: install.sh is not a toolchain).
no_asset_error() {
    tag="$1"
    assets="$(list_release_assets "$tag")"
    err "no release asset published for ${OS}-${ARCH} in $tag."
    if [ -n "$assets" ]; then
        err "assets available in $tag:"
        printf '%s\n' "$assets" | while IFS= read -r a; do err "  - $a"; done
    fi
    err "this installer only installs published binaries (no from-source build)."
    err "please open an issue requesting a ${OS}-${ARCH} asset: ${REPO_URL}/issues"
    exit 1
}

# ── from-release install (the ONLY install path) ─────────────────────────────
install_from_release() {
    mktmp
    tag="$VERSION"
    if [ -z "$tag" ]; then
        tag="$(latest_tag)" || die "could not resolve the latest release (none published yet?)"
        log "latest release: $tag"
    fi

    [ -n "$LABEL" ] || no_asset_error "$tag"

    base="${REPO_URL}/releases/download/${tag}"
    sums_url="${base}/SHA256SUMS.txt"

    # Primary asset name; for glibc, releases predating the glibc/musl split named it without
    # the `-glibc` suffix (teko-linux-x86_64.tar.gz), so fall back to that legacy name.
    asset="teko-${LABEL}.tar.gz"
    log "downloading ${base}/${asset}"
    if ! download_ok "${base}/${asset}" "$WORKDIR/$asset"; then
        if [ "${LIBC:-}" = "glibc" ]; then
            legacy="teko-linux-${ARCH}.tar.gz"
            log "$asset not found — trying legacy $legacy"
            if download_ok "${base}/${legacy}" "$WORKDIR/$legacy"; then
                asset="$legacy"
            else
                no_asset_error "$tag"
            fi
        else
            no_asset_error "$tag"
        fi
    fi

    # Verify checksum against the release's SHA256SUMS.txt. Refuse to install without a
    # verified match (tampered/corrupt download, or a release published without sums).
    download_ok "$sums_url" "$WORKDIR/SHA256SUMS.txt" \
        || die "SHA256SUMS.txt not available for $tag — refusing to install an unverified download"
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

    # Extract the binary.
    ( cd "$WORKDIR" && tar -xzf "$asset" ) || die "failed to extract $asset"
    [ -f "$WORKDIR/$BIN_NAME" ] || die "$BIN_NAME not found inside $asset"

    resolve_prefix
    install_binary "$WORKDIR/$BIN_NAME"
    # (issue #157) stage the native-build runtime under share/teko so a `teko build` run
    # from ANY project directory — not just a teko-lang checkout — finds teko_rt.c.
    # Best-effort: install_share_runtime already logs a warning and returns non-zero on
    # failure; a missing bundle must not fail the (already-verified) binary install.
    install_share_runtime "$tag" || true
    finish
}

# ── main ─────────────────────────────────────────────────────────────────────
main() {
    if [ "$DO_UNINSTALL" -eq 1 ]; then
        do_uninstall
        exit 0
    fi

    detect_platform
    log "platform: ${OS}-${ARCH}${LABEL:+ (release label: $LABEL)}"
    install_from_release
}

main
