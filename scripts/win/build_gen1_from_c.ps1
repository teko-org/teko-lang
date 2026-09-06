# scripts/win/build_gen1_from_c.ps1 — link a runnable `teko.exe` from an emitted `teko.c`, with
# the CORRECT Windows toolchain: clang targeting x86_64-pc-windows-msvc, NO libm, and
# Synchronization.lib for the §16 WaitOnAddress/WakeByAddress primitives.
#
# ── WHAT IT MIRRORS, AND THE ONE THING IT FIXES ───────────────────────────────────────────────
# scripts/build_gen1_from_c.sh links a `teko.c` for the POSIX legs (cc + -pthread; -lm dropped in
# §16 F1 — the emitted C is libm-free). Its Windows branch used to pass `cc` and `-lm` — the very
# MinGW shape the owner ruled out (2026-08-05 / 2026-08-18): MinGW gcc is "lento, obeso e fraco", its
# MSVC-family linker has no `m.lib` so `-lm` is a hard link error, and its `cc1` breaks under any PATH wrapper.
# This pwsh twin serves ONLY the Windows path and applies the same rules build_with_seed_fallback.sh's
# `declared_degrau_rung` already applies for the degrau seed and that teko's own run_cc applies for
# gen1+ ([extern.libs.windows]):
#   * clang, monolithic and fast, `--target=x86_64-pc-windows-msvc`;
#   * `-std=c23` (matching the degrau rung), no `-lm` (clang/MSVC needs none);
#   * `-lSynchronization` — WaitOnAddress/WakeByAddressSingle/WakeByAddressAll live in
#     Synchronization.lib, an API-set lib NOT auto-linked like kernel32, so a raw C link that omits
#     it dies with LNK2019 unresolved externals.
#
# It emits `<OutDir>/teko.exe` explicitly (not the `.sh`'s extensionless `teko`): PowerShell runs a
# native binary by name, and an extensionless PE is not on PATHEXT, so the `.exe` is what makes the
# produced compiler runnable by every downstream `& $bin` on this path.
#
# Dot-sourced by build_seed.ps1; also runnable standalone for debugging.
#
# CI-ONLY (cannot be checked here — no Windows, no clang-for-Windows in this sandbox):
#   * that clang resolves on the runner as an x86_64-pc-windows-msvc driver (the toolchain report
#     step prints `clang -dumpmachine` for exactly this);
#   * that `-Wl,/STACK:...` reaches lld-link/link.exe intact through the clang driver;
#   * that the 22 MB bootstrap C links in reasonable time under clang -O2 (the MinGW 55-min
#     pathology this whole change exists to kill).

. "$PSScriptRoot/common.ps1"

# Invoke-BuildGen1FromC — link TekoC + the runtime into OutDir/teko.exe. Returns the FULL path to
# the produced binary on success, or $null on failure (never throws; the caller decides fatality).
function Invoke-BuildGen1FromC {
    param(
        [Parameter(Mandatory)][string]$TekoC,
        [Parameter(Mandatory)][string]$SrcDir,
        [Parameter(Mandatory)][string]$OutDir,
        [string[]]$ExtraCFlags = @(),   # e.g. @('-O2') for a release link, @('-g') for a debug one
        [string[]]$ExtraLdFlags = @(),  # e.g. the tokens of $env:TEKO_DEGRAU_LDFLAGS
        [string]$Tag = 'build-gen1-from-c'
    )

    if (-not (Test-Path -LiteralPath $TekoC -PathType Leaf)) {
        Write-CiLog $Tag "no emitted C at '$TekoC'"
        return $null
    }
    $rtC = Join-Path $SrcDir 'runtime/teko_rt.c'
    if (-not (Test-Path -LiteralPath $rtC -PathType Leaf)) {
        Write-CiLog $Tag "'$SrcDir' is not the repo's src/ (no runtime/teko_rt.c)"
        return $null
    }
    if (-not (Get-Command clang -ErrorAction SilentlyContinue)) {
        Write-CiLog $Tag 'no clang on PATH — the Windows link needs clang (x86_64-pc-windows-msvc), never MinGW cc'
        return $null
    }

    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    $outExe = Join-Path $OutDir 'teko.exe'

    Write-CiLog $Tag ("clang " + ((& clang --version 2>&1 | Select-Object -First 1)))

    $clangArgs = @('-std=c23', '--target=x86_64-pc-windows-msvc', '-w') `
        + $ExtraCFlags + $ExtraLdFlags `
        + @(
            '-I', (Join-Path $SrcDir 'runtime'),
            '-I', (Join-Path $SrcDir 'assert'),
            $TekoC,
            $rtC,
            (Join-Path $SrcDir 'assert/assert.c'),
            '-lSynchronization',
            '-o', $outExe
        )

    Write-CiLog $Tag "link: clang $($clangArgs -join ' ')"
    & clang @clangArgs 2>&1 | ForEach-Object { Write-CiLog $Tag "  | $_" }
    if ($LASTEXITCODE -ne 0) {
        Write-CiLog $Tag "the link FAILED (clang exit $LASTEXITCODE)"
        return $null
    }

    $bin = Resolve-TekoBin $OutDir
    if (-not $bin) {
        Write-CiLog $Tag "the link reported success but $outExe is not present"
        return $null
    }
    Write-CiLog $Tag "wrote $bin"
    return $bin
}

# Standalone entry: build_gen1_from_c.ps1 <teko_c> <src_dir> <out_dir>. Skipped when dot-sourced.
if ($MyInvocation.InvocationName -ne '.') {
    if ($args.Count -lt 3) {
        Write-CiLog 'build-gen1-from-c' 'usage: build_gen1_from_c.ps1 <teko_c> <src_dir> <out_dir>'
        exit 1
    }
    $ldflags = @()
    if ($env:TEKO_DEGRAU_LDFLAGS) { $ldflags = $env:TEKO_DEGRAU_LDFLAGS -split '\s+' | Where-Object { $_ } }
    $bin = Invoke-BuildGen1FromC -TekoC $args[0] -SrcDir $args[1] -OutDir $args[2] -ExtraCFlags @('-O2') -ExtraLdFlags $ldflags
    if (-not $bin) { exit 1 }
    exit 0
}
