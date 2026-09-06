# Compiling the Teko compiler

The Teko compiler ships as a single committed C translation unit, `bootstrap/teko.c`
(emitted by the compiler compiling itself). Linking it with the small C runtime
produces a runnable `teko` binary — no prior Teko binary is needed.

The link inputs are always the same three files:

- `bootstrap/teko.c` — the whole compiler, as one C file
- `src/runtime/teko_rt.c` — the C runtime
- `src/assert/assert.c` — the assert runtime

with include paths `-Isrc/runtime -Isrc/assert`.

Ready-made scripts do this per OS (default compiler `clang`, override with `CC`):

| OS | script | output |
|----|--------|--------|
| Linux (Ubuntu) | `scripts/comp_teko_linux.sh [out]` | `./teko` |
| macOS | `scripts/comp_teko_mac.sh [out]` | `./teko` |
| Windows | `scripts\comp_teko_win.ps1 [out]` | `.\teko.exe` |

`out` is an optional first argument (default `<repo>/teko`, `teko.exe` on Windows).

> The build is one `clang` invocation over a ~22 MB source file. At `-O2` (the script
> default, for a fast compiler) this takes a few minutes and a few GB of RAM. For a quick
> throwaway build, pass `CFLAGS_EXTRA=-O0` (seconds, slower `teko`).

---

## Common requirements

- **Git** — to clone the repository.
- **A C compiler with C2x support** — `clang` 14 or newer is recommended (tested with
  clang 18). GCC 12+ also works but is markedly slower on the single 22 MB unit, so the
  scripts default to `clang`.

Clone first:

```sh
git clone https://github.com/schivei/teko-lang.git
cd teko-lang
```

---

## Ubuntu / Debian Linux

Install clang and git:

```sh
sudo apt update
sudo apt install -y clang git
```

Build:

```sh
sh scripts/comp_teko_linux.sh
./teko --version
```

`pthread` is provided by glibc; no extra package is required.

---

## macOS

Install the Xcode Command Line Tools (provides `clang`, `git`, headers, linker):

```sh
xcode-select --install
```

Optionally, for a newer clang than Apple ships:

```sh
brew install llvm
export CC="$(brew --prefix llvm)/bin/clang"
```

Build:

```sh
sh scripts/comp_teko_mac.sh
./teko --version
```

Works on both Apple Silicon and Intel.

---

## Windows

Two pieces are needed: the **clang** compiler and the **Windows SDK + C runtime
headers** (`teko_rt.c` includes `<windows.h>`, `<direct.h>`, `<process.h>`, `<io.h>`).

### Option A — LLVM + Visual Studio Build Tools (recommended)

Using [winget](https://learn.microsoft.com/windows/package-manager/) in an elevated
PowerShell:

```powershell
winget install --id LLVM.LLVM -e
winget install --id Microsoft.VisualStudio.2022.BuildTools -e `
    --override "--quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

The **VCTools** workload supplies the Windows SDK and the MSVC CRT headers clang links
against. Ensure `clang` is on `PATH` (the LLVM installer offers to add it), then open a
new PowerShell and build:

```powershell
.\scripts\comp_teko_win.ps1
.\teko.exe --version
```

> The script sets up a 64-bit MSVC developer environment automatically (via `vswhere` +
> `VsDevCmd.bat -arch=amd64`, forced fresh so it works even inside an already-open dev shell)
> and feeds the resulting `INCLUDE` paths to clang as `-isystem`. This handles two common
> failures:
>
> - **`fatal error: 'stdlib.h' file not found`** — standalone clang (the GNU-style driver)
>   ignores `%INCLUDE%` and relies on its own MSVC auto-detection, which fails on newer VS
>   layouts (e.g. VS 2026). Feeding `INCLUDE` as `-isystem` fixes it.
> - **`libcmt.lib(chkstk.obj): machine type x86 conflicts with x64`** — the shell was set up
>   for 32-bit, so `LIB` pointed at x86 libs. Forcing an `amd64` environment fixes the link.
>
> If no VS is found the script warns; run it then from the **"x64 Native Tools Command Prompt
> for VS"**, or use MSYS2/MinGW clang (Option B), whose clang carries its own headers.

### Option B — MSYS2 / MinGW-w64

Install [MSYS2](https://www.msys2.org/), then in the MSYS2 MinGW64 shell:

```sh
pacman -S --needed mingw-w64-x86_64-clang git
```

The mingw-w64 toolchain provides `<windows.h>` and the CRT headers directly. Build with
the PowerShell script (from a normal PowerShell, with the MinGW `clang` on `PATH`) or run
the link line by hand:

```sh
clang -std=c2x -w -O2 -Isrc/runtime -Isrc/assert \
    bootstrap/teko.c src/runtime/teko_rt.c src/assert/assert.c -o teko.exe
```

> No `-pthread` on Windows: `teko_rt.c` uses `CreateThread` under `_WIN32`.

---

## Overrides

- `CC` — the C compiler to invoke (e.g. `CC=gcc`, `CC=/path/to/clang`).
- `CFLAGS_EXTRA` — extra flags appended after the fixed ones (e.g. `-O0` for a fast
  build, `-g` for debug symbols, sanitizer flags).
