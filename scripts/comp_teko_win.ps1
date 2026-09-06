# comp_teko_win.ps1 — link a usable teko.exe compiler from the committed bootstrap/teko.c on Windows.
#
# Usage:  scripts\comp_teko_win.ps1 [out_path]
#   out_path  where to write the binary (default: <repo>\teko.exe)
# Env:
#   CC            C compiler to use (default: clang)
#   CFLAGS_EXTRA  extra flags (space-separated) appended after the fixed ones
#
# On Windows, clang targets the MSVC ABI and needs the MSVC CRT + Windows SDK headers
# (<stdlib.h>, <windows.h>, ...). Those live in the environment only inside a "Developer
# PowerShell for VS". This script auto-enters that environment (via vswhere) when it is not
# already set up, so it works from a plain PowerShell too. MSYS2/MinGW clang carries its own
# headers and skips this step.
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$CC   = if ($env:CC) { $env:CC } else { "clang" }
$Out  = if ($args.Count -ge 1) { $args[0] } else { Join-Path $Root "teko.exe" }

$TekoC = Join-Path $Root "bootstrap\teko.c"
if (-not (Test-Path $TekoC)) { Write-Error "comp_teko_win: missing $TekoC"; exit 1 }

# Set up a 64-bit MSVC developer environment: the CRT / Windows SDK headers AND the x64 import
# libs. This is done ALWAYS (not only when INCLUDE is unset), because a shell that defaulted to
# x86 leaves LIB pointing at 32-bit libs, and clang's x64 objects then fail to link with
# "libcmt.lib(chkstk.obj): machine type x86 conflicts with x64". Running VsDevCmd in a cmd
# subshell with VSCMD_VER cleared forces a fresh amd64 init even inside an already-open dev
# shell. -prerelease finds preview/insider builds such as VS 2026. MSYS2/MinGW clang users who
# have INCLUDE/LIB set up their own way can skip this by having no VS installed.
# Locate the VS install. Prefer VSINSTALLDIR (always set inside any VS developer shell); fall
# back to vswhere under both Program Files roots.
$vsPath = $null
if ($env:VSINSTALLDIR) { $vsPath = $env:VSINSTALLDIR.TrimEnd('\') }
if (-not $vsPath) {
    foreach ($vw in @((Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"),
                       (Join-Path $env:ProgramFiles "Microsoft Visual Studio\Installer\vswhere.exe"))) {
        if (Test-Path $vw) {
            $p = (& $vw -latest -prerelease -products * -property installationPath 2>$null | Select-Object -First 1)
            if ($p) { $vsPath = $p; break }
        }
    }
}
if ($vsPath) {
    $vsDevCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
    if (Test-Path $vsDevCmd) {
        Write-Host "comp_teko_win: loading x64 VS environment from $vsPath"
        cmd /c "set `"VSCMD_VER=`" && `"$vsDevCmd`" -arch=amd64 -host_arch=amd64 -no_logo && set" | ForEach-Object {
            if ($_ -match '^([^=]+)=(.*)$') {
                Set-Item -Path "env:$($matches[1])" -Value $matches[2] -ErrorAction SilentlyContinue
            }
        }
        Write-Host "comp_teko_win: target arch = $env:VSCMD_ARG_TGT_ARCH"
    } else {
        Write-Warning "comp_teko_win: found VS at $vsPath but no Common7\Tools\VsDevCmd.bat"
    }
} else {
    Write-Warning "comp_teko_win: could not locate a Visual Studio installation (VSINSTALLDIR/vswhere)."
}
if (-not $env:INCLUDE) {
    Write-Warning "comp_teko_win: MSVC/Windows SDK headers not in environment. Run from the 'x64 Native Tools Command Prompt for VS', install VS Build Tools with the 'Desktop development with C++' workload, or use MSYS2/MinGW clang (see COMPILE.md)."
}

$Ver = "0.0.0.0-dev"
$DeriveSh = Join-Path $Root "scripts\derive_version.sh"
if ((Get-Command sh -ErrorAction SilentlyContinue) -and (Test-Path $DeriveSh)) {
    try { $Ver = (& sh $DeriveSh).Trim().TrimStart("v") } catch { }
}

$Extra = @()
if ($env:CFLAGS_EXTRA) { $Extra = $env:CFLAGS_EXTRA.Split(" ", [StringSplitOptions]::RemoveEmptyEntries) }

# Standalone clang (the GNU-style driver) does NOT read %INCLUDE% and relies on its own MSVC
# auto-detection, which fails on newer VS layouts (e.g. VS 2026) — "'stdlib.h' not found" even
# inside a VS developer shell. Feed the dev shell's INCLUDE paths to clang explicitly as
# -isystem so header search does not depend on clang's detection. The linker reads %LIB% itself.
$SysInc = @()
if ($env:INCLUDE) {
    foreach ($p in $env:INCLUDE.Split(";", [StringSplitOptions]::RemoveEmptyEntries)) {
        $SysInc += "-isystem"; $SysInc += $p
    }
}

Write-Host "comp_teko_win: CC=$CC -> $Out (version $Ver)"
try { & $CC --version 2>$null | Select-Object -First 1 } catch { }

# No -pthread on Windows: teko_rt.c uses CreateThread there (guarded by #ifdef _WIN32).
& $CC -std=c2x -w -O2 "-DTEKO_VERSION_STRING=$Ver" @Extra @SysInc `
    "-I$Root\src\runtime" "-I$Root\src\assert" `
    $TekoC "$Root\src\runtime\teko_rt.c" "$Root\src\assert\assert.c" `
    -o $Out
if ($LASTEXITCODE -ne 0) { Write-Error "comp_teko_win: link failed ($LASTEXITCODE)"; exit $LASTEXITCODE }

if (-not (Test-Path $Out)) { Write-Error "comp_teko_win: link reported success but $Out is missing"; exit 1 }
Write-Host "comp_teko_win: wrote $Out"
