# Installing Teko

Teko ships two install mechanisms for macOS and Linux: a POSIX `install.sh` script and
a Homebrew tap. Both are covered below, along with the from-source fallback and a few
platform notes.

> **Pre-alpha.** Teko is pre-alpha software. The prebuilt binaries are **not** Apple
> code-signed or notarized — that is intentionally out of scope for CLI tooling at this
> stage. The install script strips the macOS Gatekeeper quarantine flag from the
> downloaded binary; the Homebrew formula builds from source, so it never trips
> Gatekeeper at all.

## Method 1 — `install.sh` (macOS + Linux)

One-liner:

```sh
curl -fsSL https://raw.githubusercontent.com/schivei/teko-lang/main/install.sh | sh
```

By default this downloads the prebuilt `teko-<label>.tar.gz` for your platform from the
latest GitHub release, **verifies its SHA-256** against the release's `SHA256SUMS.txt`
(aborting on any mismatch), extracts the `teko` binary, strips the macOS quarantine flag,
and installs it to a directory on your `PATH`.

### Options

| Option | Effect |
| --- | --- |
| `--from-source` | Build `teko` from source instead of downloading a release. |
| `--version <tag>` | Install a specific release tag (default: latest). |
| `--prefix <dir>` | Install directory (default: `/usr/local/bin`, else `~/.local/bin`). |
| `--uninstall` | Remove the installed `teko` binary. |
| `--help` | Show usage. |

Environment overrides: `TEKO_VERSION` (= `--version`), `PREFIX` (= `--prefix`), and
`CC` (C compiler for the from-source build, default `cc`).

Examples:

```sh
# Pin a version
./install.sh --version 0.0.1.3-bootstrap

# Install without sudo, into your home directory
./install.sh --prefix "$HOME/.local/bin"

# Force a source build
./install.sh --from-source

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

## From source (fallback)

The `install.sh` from-source path (via `--from-source`, or automatically when no
prebuilt asset exists for your architecture — see the caveat below) builds `teko` with
your system C compiler. It picks the best available source in this order:

1. **Inside a `teko-lang` checkout** — configures and builds the `teko` CMake target:
   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --target teko
   ```
2. **A released portable bundle** (`teko-bootstrap-src.tar.gz`) — builds it with the
   documented single command:
   ```sh
   cc -std=c23 -w -Iruntime -Iassert teko.c runtime/teko_rt.c assert/assert.c -lm -o teko
   ```
3. **Otherwise** — `git clone --depth 1` the repo into a temp directory and CMake-build
   it there.

Requirements for the from-source path: a C compiler with **C23** support (recent Clang
or GCC) and, for the checkout/clone route, `cmake` and `git`.

## Platform notes and caveats

- **macOS prebuilt binaries are Apple Silicon (arm64) only.** There is no
  `macos-x86_64` release asset. On an **Intel Mac**, `install.sh` detects this and
  builds from source automatically — no extra flags needed. Homebrew already builds
  from source on every architecture.
- **Supported release labels:** `macos-arm64`, `linux-x86_64`, `linux-arm64` (plus
  Windows labels, which this script does not target). Windows users should build from
  the portable bundle or use WSL.
- **No signing / notarization.** For this pre-alpha, prebuilt binaries are neither
  code-signed nor notarized. `install.sh` strips the `com.apple.quarantine` extended
  attribute after download so Gatekeeper does not block the CLI; the Homebrew formula
  sidesteps the issue entirely by compiling locally. This decision is deliberate and
  will be revisited before a stable release.
- **No release published yet.** Until `0.0.1.3-bootstrap` is released, the from-release
  path has nothing to download; the script degrades gracefully and builds from source.
  Use `--from-source` explicitly to skip the release probe.

## Uninstalling

- Script install: `./install.sh --uninstall` (removes `teko` from the default prefixes,
  or from `--prefix <dir>` if you pass one).
- Homebrew install: `brew uninstall teko` (and `brew untap schivei/teko` to drop the tap).
