param([string]$Kind = '', [string]$Seed = '', [Parameter(ValueFromRemainingArguments = $true)][string[]]$Produces = @())
# scripts/win/produce_assets.ps1 — THE Windows production path for a published teko asset, pwsh-native.
#
# The Windows mirror of scripts/produce_assets.sh (the `.sh` stays for linux/macos). Same shape:
#   1. the seed — on the FORCED-DEGRAU path (owner 2026-08-18) the released seed is never the
#      build's seed, so this SKIPS the release probe and records seed_version=degrau. With no
#      degrau it provisions natively via provision_seed.ps1 (Invoke-WebRequest/Expand-Archive),
#      non-fatally, exactly as produce_assets.sh treats provisioning as non-fatal.
#   2. the dry build + the fold — build_seed.ps1: declared-C compiler -> gen0 -> gen1, leaving
#      out/teko.exe AND out/teko.c (gen1's own emitted C, what pr.yml harvests over a green fixpoint).
#   3. stage — stage/<label>/teko.exe, one dir per label: THE CONTRACT a consumer downloads.
#   4. provenance — one PROVENANCE.txt per label, the antecedents of the reproducibility law.
#
# `kind` is always `native` on Windows (the runner IS the target, so the dry build's own binary is
# the asset — there is no separate native-Linux-asset step). The `linux` branch of the `.sh` has no
# Windows analogue and is intentionally absent here.
#
# Usage:  produce_assets.ps1 <kind> <seed-label> <asset-label>...
# CI-ONLY: the whole chain (clang link, gen0/gen1 self-host, teko emitting out/teko.exe) runs only
# on windows-latest; the arch assertion reads the staged PE's own COFF machine word (no external tool).

Set-StrictMode -Version Latest
$PSNativeCommandUseErrorActionPreference = $false

. "$PSScriptRoot/common.ps1"
. "$PSScriptRoot/degrau.ps1"
. "$PSScriptRoot/build_seed.ps1"
. "$PSScriptRoot/provision_seed.ps1"

$Tag = 'produce_assets'

# Assert-AssetArch LABEL BIN — BIN must be built for the architecture LABEL promises (native kind).
# No silent skip: when neither the PE header nor anything else can decide, this FAILS — an
# unasserted asset is how a PE was once published under a foreign machine (produce_assets.sh, M.3).
function Assert-AssetArch {
    param([string]$Label, [string]$Bin, [string]$Kind)
    if ($Kind -ne 'native') { return }
    $want = Get-ArchKeywordForLabel $Label
    if (-not $want) { Write-CiLog $Tag "'$Label' names no architecture — nothing to assert"; return }
    $got = Get-MachineWord $Bin
    if (-not $got) {
        Write-CiLog $Tag "FATAL: cannot determine the architecture of '$Bin' for label '$Label'."
        Write-CiLog $Tag 'This does NOT pass by default: an unasserted asset is how a PE was once published under a foreign machine.'
        exit 1
    }
    if ($got -ne $want) {
        Write-CiLog $Tag "FATAL: '$Label' promises $want but '$Bin' is $got. Refusing to stage a mislabelled asset."
        exit 1
    }
    Write-CiLog $Tag "$Label architecture asserted: $got"
}

function Invoke-ProduceAssets {
    param([string]$Kind, [string]$Seed, [string[]]$Produces)

    if (-not $Kind) { Write-CiLog $Tag 'usage: produce_assets.ps1 <kind> <seed-label> <asset-label>...'; return 1 }
    if (-not $Seed) { Write-CiLog $Tag 'missing seed-label'; return 1 }
    if (-not $Produces -or $Produces.Count -lt 1) { Write-CiLog $Tag 'a producer must promise at least one asset label'; return 1 }
    if ($Kind -ne 'native' -and $Kind -ne 'linux') { Write-CiLog $Tag "kind must be 'native' or 'linux', not '$Kind'"; return 1 }
    if ($Kind -eq 'linux') { Write-CiLog $Tag "kind 'linux' has no Windows path — use the POSIX scripts/produce_assets.sh"; return 1 }

    Write-CiLog $Tag "kind=$Kind seed=$Seed produces='$($Produces -join ' ')'"

    # ── 1. the seed ────────────────────────────────────────────────────────────────────────────
    $deg = Read-Degrau (Get-Location).Path
    if ($deg.Status -eq 2) { Write-CiLog $Tag 'FATAL: the degrau declaration is broken (see above).'; return 1 }
    $seedVersion = 'unknown'
    if ($deg.Status -eq 0) {
        Write-CiLog $Tag "degrau declared ($($deg.File)) — the released seed is never the build's seed (owner 2026-08-18);"
        Write-CiLog $Tag 'skipping the release probe. gen0 descends from the DECLARED C, so seed_version=degrau.'
        $seedVersion = 'degrau'
    } else {
        $rc = Invoke-ProvisionSeed -Label $Seed
        if ($rc -ne 0) { Write-CiLog $Tag "no released/committed seed for '$Seed' — proceeding to the declared degrau ladder" }
        $teko = Get-Command teko -ErrorAction SilentlyContinue
        if ($teko) {
            $seedVersion = (& $teko.Source --version 2>$null | Select-Object -First 1)
            if (-not $seedVersion) { $seedVersion = 'unknown' }
        } else {
            $seedVersion = 'degrau'
        }
    }

    # ── 2. the dry build + the fold ──────────────────────────────────────────────────────────────
    $rc = Invoke-BuildSeed -OutDir 'out'
    if ($rc -ne 0) { Write-CiLog $Tag 'the dry build failed'; return 1 }
    if (-not (Test-Path -LiteralPath 'out/teko.c' -PathType Leaf)) { Write-CiLog $Tag 'the dry build produced no out/teko.c'; return 1 }

    # ── 3. stage ─────────────────────────────────────────────────────────────────────────────────
    if (Test-Path -LiteralPath 'stage') { Remove-Item -Recurse -Force -LiteralPath 'stage' }
    foreach ($label in $Produces) {
        New-Item -ItemType Directory -Force -Path "stage/$label" | Out-Null
        $src = 'out/teko'
        if (Test-Path -LiteralPath 'out/teko.exe' -PathType Leaf) { $src = 'out/teko.exe' }
        if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { Write-CiLog $Tag "promised '$label' but $src does not exist"; return 1 }
        Assert-AssetArch -Label $label -Bin $src -Kind $Kind
        if ($src -like '*.exe') { Copy-Item -LiteralPath $src -Destination "stage/$label/teko.exe" }
        else { Copy-Item -LiteralPath $src -Destination "stage/$label/teko" }
        Write-Host "staged $label <- $src"
    }

    # ── 4. provenance, one file per asset ────────────────────────────────────────────────────────
    $manifestTag = Get-ManifestTag 'teko.tkp'
    $hostUname = Get-HostUname
    $tekoCSha = Get-Sha256Hex 'out/teko.c'
    $ccVer = 'unknown'
    $clang = Get-Command clang -ErrorAction SilentlyContinue
    if ($clang) { $ccVer = (& $clang.Source --version 2>$null | Select-Object -First 1) }

    foreach ($label in $Produces) {
        $bin = "stage/$label/teko"
        if (-not (Test-Path -LiteralPath $bin -PathType Leaf)) { $bin = "stage/$label/teko.exe" }
        $prov = "stage/$label/PROVENANCE.txt"
        $lines = @(
            "label=$label",
            "producer_kind=$Kind",
            "seed_label=$Seed",
            "seed_version=$seedVersion",
            "manifest_tag=$manifestTag",
            "host_uname=$hostUname",
            "teko_c_sha256=$tekoCSha",
            "asset_sha256=$(Get-Sha256Hex $bin)",
            'toolchain_image=runner',
            "toolchain_cc=$ccVer"
        )
        Set-Content -LiteralPath $prov -Value $lines
        Write-Host "--- $prov ---"
        Get-Content -LiteralPath $prov | ForEach-Object { Write-Host $_ }
        $size = Get-FileSizeBytes $bin
        Write-Host ("asset summary: {0,-24} size={1,12} bytes  sha256={2}" -f $label, $size, (Get-Sha256Hex $bin))
    }

    Get-ChildItem -Recurse -Path 'stage' | ForEach-Object { Write-Host $_.FullName }
    Write-CiLog $Tag "OK — staged: $($Produces -join ' ')"
    return 0
}

if ($MyInvocation.InvocationName -ne '.') {
    exit (Invoke-ProduceAssets -Kind $Kind -Seed $Seed -Produces $Produces)
}
