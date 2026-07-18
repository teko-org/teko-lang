# Installing Teko

Teko ships two install mechanisms for macOS and Linux: a POSIX `install.sh` script and
a Homebrew tap. Both are covered below, along with a few platform notes.

> **Pre-alpha.** Teko is pre-alpha software. The prebuilt binaries are **not** Apple
> code-signed or notarized — that is intentionally out of scope for CLI tooling at this
> stage. The install script strips the macOS Gatekeeper quarantine flag from the
> downloaded binary; the Homebrew formula builds from source, so it never trips
> Gatekeeper at all.

## Method 1 — `install.sh` (macOS + Linux)

One-liner:

```sh
curl -fsSL https://raw.githubusercontent.com/teko-org/teko-lang/main/install.sh | sh
```

`install.sh` is a **from-release installer only** — it is not a toolchain. With no
arguments it downloads the prebuilt `teko-<label>.tar.gz` for your platform from the
**latest published GitHub release**, **verifies its SHA-256** against the release's
`SHA256SUMS.txt` (aborting on any mismatch), extracts the `teko` binary, strips the
macOS quarantine flag, and installs it to a directory on your `PATH`. It also stages
that release's `runtime`/`assert`/`win32_compat.h` sources under a `share/teko` dir
mirroring the chosen prefix, so `teko build` on the installed binary can find its C
runtime from any project directory — not just inside a `teko-lang` checkout.

A platform with no published release asset is an **honest error**: the script lists
whatever assets the release DOES publish and points at the issue tracker — it never
falls back to building from source.

### Options

| Option | Effect |
| --- | --- |
| `--version <tag>` | Install a specific release tag (default: latest). |
| `--prefix <dir>` | Install directory (default: `/usr/local/bin`, else `~/.local/bin`). |
| `--uninstall` | Remove the installed `teko` binary and its staged runtime. |
| `--help` | Show usage. |

Environment overrides: `TEKO_VERSION` (= `--version`), `PREFIX` (= `--prefix`).

Examples:

```sh
# Pin a version
./install.sh --version 0.0.1.3-bootstrap

# Install without sudo, into your home directory
./install.sh --prefix "$HOME/.local/bin"

# Remove it
./install.sh --uninstall
```

### Prefix and PATH

The script installs to `/usr/local/bin` when that is writable, otherwise it falls back
to `~/.local/bin`. If the chosen directory is not on your `PATH`, the script prints the
exact line to add, e.g.:

```sh
export PATH="$HOME/.local/bin:$PATH"
```

Append that to your shell profile (`~/.profile`, `~/.zshrc`, or `~/.bashrc`) and reload
the shell. The script is **idempotent** — re-running it reinstalls cleanly over an
existing install.

Once installed, run `teko` with no arguments to see the usage banner, then
`teko build <projectdir>` to compile a project (there is no `--version`/`--help` flag
in this pre-alpha).

## Method 2 — Homebrew (macOS + Linux/Linuxbrew)

```sh
brew tap schivei/teko
brew install teko
```

The Homebrew formula **builds from source** using your system C compiler, so the result
is architecture-native on both Apple Silicon (arm64) and Intel (x86_64) and is never
quarantined by Gatekeeper. Upgrades and uninstalls go through Homebrew as usual:

```sh
brew upgrade teko
brew uninstall teko
brew untap schivei/teko
```

## Building from source

`install.sh` never builds from source — it only installs published release assets. To
build `teko` yourself (contributing, or a platform with no published asset), use CMake
directly against a `teko-lang` checkout:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target teko
```

or, from a released portable bundle (`teko-bootstrap-src.tar.gz`), the documented
single command:

```sh
cc -std=c23 -w -Iruntime -Iassert teko.c runtime/teko_rt.c assert/assert.c -lm -o teko
```

Requirements: a C compiler with **C23** support (recent Clang or GCC) and, for the
checkout route, `cmake`.

## Platform notes and caveats

- **macOS prebuilt binaries are Apple Silicon (arm64) only.** There is no
  `macos-x86_64` release asset yet. On an **Intel Mac**, `install.sh` reports an
  honest error listing the assets that ARE published — see "Building from source"
  above. Homebrew already builds from source on every architecture.
- **Supported release labels:** `macos-arm64`, `linux-x86_64`, `linux-arm64` (plus
  Windows labels, which this script does not target). Windows users should build from
  the portable bundle or use WSL.
- **No signing / notarization.** For this pre-alpha, prebuilt binaries are neither
  code-signed nor notarized. `install.sh` strips the `com.apple.quarantine` extended
  attribute after download so Gatekeeper does not block the CLI; the Homebrew formula
  sidesteps the issue entirely by compiling locally. This decision is deliberate and
  will be revisited before a stable release.

## Uninstalling

- Script install: `./install.sh --uninstall` (removes `teko` from the default prefixes,
  or from `--prefix <dir>` if you pass one).
- Homebrew install: `brew uninstall teko` (and `brew untap schivei/teko` to drop the tap).
