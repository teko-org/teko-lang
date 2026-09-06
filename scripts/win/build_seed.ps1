param([string]$OutDir = 'bin')
# scripts/win/build_seed.ps1 — build gen1 (the tip's compiler) on Windows, pwsh-native.
#
# The Windows mirror of scripts/build_with_seed_fallback.sh. The `.sh` stays authoritative for
# linux/macos; this duplicates the ONLY chain the Windows leg walks today, plus the gen0->gen1
# fold, so no Windows step shells out to `sh scripts/build_with_seed_fallback.sh`.
#
# ── THE CHAIN, ON WINDOWS, UNDER THE FORCED-DEGRAU RULING (owner 2026-08-18) ───────────────────
# A declared `bootstrap/DEGRAU` SHORT-CIRCUITS everything: the released seed and the pinned SHA
# ladder are NEVER tried. `bootstrap/teko.c` is compiled straight into the "declared-C compiler"
# (clang --target=x86_64-pc-windows-msvc -lSynchronization, via build_gen1_from_c.ps1); that
# compiler builds the tip = gen0; and `gen0` rebuilds the identical source = gen1 (the DOUBLING).
# If gen0 cannot build the tip, this FAILS ON THE SPOT — no release probe, no ladder, no
# version-old seed. The release predates this tree's syntax, so falling back to it would bury the
# real failure under the wrong one and publish gen0 from the release instead of from this tree.
# What ends the degrau is DELETING the declaration the day the released seed reaches the tip again.
#
# ── WHAT THIS PORT DELIBERATELY DOES NOT REIMPLEMENT ──────────────────────────────────────────
# The POSIX ladder's transitional tail — the committed-seed rung (bootstrap/seeds/, absent from the
# tree) and the pinned SHA ladder (pre-0.3.1 "poison" rungs, unreachable after squash-merge) — is
# NOT reimplemented in pwsh. On this branch the degrau is FORCED and always declared, so that tail
# is dead code here; the `.sh` keeps it for the POSIX legs. The no-degrau branch below is a
# best-effort released-seed FAST PATH only, and it says so if it has to give up. (Reported to the
# coordinator as a scoped decision, not an omission.)
#
# CI-ONLY: that clang links the 22 MB bootstrap C fast under -O2; that gen0 (built by the declared-C
# compiler) then rebuilds the tip; that teko emits `out/teko.exe` + `out/teko.c` on a Windows target.

. "$PSScriptRoot/common.ps1"
. "$PSScriptRoot/degrau.ps1"
. "$PSScriptRoot/build_gen1_from_c.ps1"

# Get-SelfhostBackend — the per-platform hook build_with_seed_fallback.sh calls TEKO_SELFHOST_BACKEND
# (default `c` — the only route that self-hosts today). Pins the backend for gen0/gen1 builds ONLY.
function Get-SelfhostBackend {
    if ($env:TEKO_SELFHOST_BACKEND) { return $env:TEKO_SELFHOST_BACKEND }
    return 'c'
}

# Invoke-Gen0ToGen1 — the DOUBLING (gen0_to_gen1). The compiler now in $OutDir was built from the
# tip's source by $Origin: that is gen0. Move it to .gen0/ and have IT rebuild the identical source,
# so $OutDir finally holds gen1 and $OutDir/teko.c is gen1's own emitted C. Returns $true/$false.
function Invoke-Gen0ToGen1 {
    param([string]$OutDir, [string]$Origin)
    $gen0 = Resolve-TekoBin $OutDir
    if (-not $gen0) {
        Write-CiLog 'teko-ci' "FATAL: $Origin reported success but left no teko binary in $OutDir — no gen0, no gen1."
        return $false
    }
    $stage = Join-Path (Get-Location).Path '.gen0'
    if (Test-Path -LiteralPath $stage) { Remove-Item -Recurse -Force -LiteralPath $stage }
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    $stagedBin = Join-Path $stage (Split-Path -Leaf $gen0)
    Move-Item -LiteralPath $gen0 -Destination $stagedBin
    $gen0C = Join-Path $OutDir 'teko.c'
    if (Test-Path -LiteralPath $gen0C -PathType Leaf) { Copy-Item -LiteralPath $gen0C -Destination (Join-Path $stage 'teko.c') }
    Write-CiLog 'teko-ci' "gen0 ready, built by $Origin ($(& $stagedBin --version 2>&1 | Select-Object -First 1)) — building gen1 = gen0(source)"
    $rtDir = Join-Path (Get-Location).Path 'src/runtime'
    if (-not (Test-Path -LiteralPath $rtDir)) { $rtDir = '' }
    $code = Invoke-TekoBuild -Bin $stagedBin -ProjDir (Get-Location).Path -Out $OutDir -RtDir $rtDir -Backend (Get-SelfhostBackend) -Tag 'teko-ci'
    if ($code -ne 0) {
        Write-CiLog 'teko-ci' 'FATAL: gen0 does not rebuild the source it came from — the chain breaks at gen1.'
        return $false
    }
    Write-CiLog 'teko-ci' "gen1 ready at $OutDir — and $OutDir/teko.c, when present, is gen1's own emitted C"
    return $true
}

# Invoke-DeclaredDegrauRung — RUNG -1: link the declared C into the "declared-C compiler", then have
# THAT build the tip into $OutDir (= gen0). Mirrors declared_degrau_rung. Returns $true/$false.
function Invoke-DeclaredDegrauRung {
    param([hashtable]$Degrau, [string]$OutDir)
    $ldflags = @()
    if ($env:TEKO_DEGRAU_LDFLAGS) { $ldflags = $env:TEKO_DEGRAU_LDFLAGS -split '\s+' | Where-Object { $_ } }
    $rungOut = Join-Path (Get-Location).Path '.rung-c'
    if (Test-Path -LiteralPath $rungOut) { Remove-Item -Recurse -Force -LiteralPath $rungOut }

    Write-CiLog 'teko-ci' "rung -1: building the degrau's compiler from $($Degrau.C) (cc=clang, target=x86_64-pc-windows-msvc)"
    $degBin = Invoke-BuildGen1FromC -TekoC $Degrau.C -SrcDir (Join-Path (Get-Location).Path 'src') -OutDir $rungOut -ExtraCFlags @('-O2') -ExtraLdFlags $ldflags -Tag 'teko-ci'
    if (-not $degBin) {
        Write-CiLog 'teko-ci' 'rung -1: the declared C did not compile — the forced seed cannot be built.'
        return $false
    }
    Write-CiLog 'teko-ci' "rung -1: degrau compiler ready ($(& $degBin --version 2>&1 | Select-Object -First 1))"

    $rtDir = Join-Path (Get-Location).Path 'src/runtime'
    $code = Invoke-TekoBuild -Bin $degBin -ProjDir (Get-Location).Path -Out $OutDir -RtDir $rtDir -Backend (Get-SelfhostBackend) -Tag 'teko-ci'
    if ($code -ne 0) {
        Write-CiLog 'teko-ci' 'rung -1: the degrau compiler could not build the tip — the forced seed does not reach it.'
        return $false
    }
    Write-CiLog 'teko-ci' 'rung -1: gen0 was built from the DECLARED C — NO LADDER WAS WALKED'
    return $true
}

# Invoke-BuildSeed — the entry logic. Returns an exit code (0 success). Never calls `exit`, so
# produce_assets.ps1 can dot-source this file and call it in-process without terminating the host.
function Invoke-BuildSeed {
    param([string]$OutDir = 'bin')
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

    $deg = Read-Degrau (Get-Location).Path
    if ($deg.Status -eq 2) {
        Write-CiLog 'teko-ci' 'FATAL: the degrau declaration is broken (see the lines above) — refusing to pick a chain.'
        return 1
    }
    Write-DegrauReport $deg
    if ($deg.Status -ne 0) { Write-DegrauUndeclaredCNote (Get-Location).Path }

    if ($deg.Status -eq 0) {
        if (Invoke-DeclaredDegrauRung -Degrau $deg -OutDir $OutDir) {
            if (Invoke-Gen0ToGen1 -OutDir $OutDir -Origin 'rung -1 (the DECLARED degrau — forced seed)') { return 0 }
            return 1
        }
        Write-CiLog 'teko-ci' "FATAL: a degrau is DECLARED ($($deg.File)) — it IS the forced seed — but it could not build"
        Write-CiLog 'teko-ci' 'the tip (the failure is above). Owner ruling 2026-08-18: with a declared degrau there is NO'
        Write-CiLog 'teko-ci' 'fallback to the published release and NO pinned ladder; the release predates this tree''s'
        Write-CiLog 'teko-ci' 'syntax and probing it would only report the wrong failure while burying this one.'
        Write-CiLog 'teko-ci' 'Fix bootstrap/teko.c so it compiles this tree, or DELETE bootstrap/DEGRAU.'
        return 1
    }

    # ── NO DEGRAU DECLARED: the released-seed FAST PATH only (see the header). ──────────────────
    $seed = Get-Command teko -ErrorAction SilentlyContinue
    if (-not $seed) {
        Write-CiLog 'teko-ci' "FATAL: no degrau is declared and 'teko' is not on PATH — the chain has no first link."
        Write-CiLog 'teko-ci' 'Provision the published release first, or declare the degrau (bootstrap/DEGRAU).'
        Write-CiLog 'teko-ci' 'The Windows path does not reimplement the POSIX committed-seed / pinned-SHA ladder.'
        return 1
    }
    $code = Invoke-TekoBuild -Bin $seed.Source -ProjDir (Get-Location).Path -Out $OutDir -RtDir '' -Backend (Get-SelfhostBackend) -Tag 'teko-ci'
    if ($code -eq 0) {
        Write-CiLog 'teko-ci' 'the published release built gen0 directly — fast path, no fallback engaged'
        if (Invoke-Gen0ToGen1 -OutDir $OutDir -Origin 'the published release') { return 0 }
        return 1
    }
    Write-CiLog 'teko-ci' 'FATAL: the released seed on PATH could not build the tip, and the Windows path does not'
    Write-CiLog 'teko-ci' 'reimplement the POSIX pinned-SHA ladder (pre-0.3.1 poison rungs). Declare a degrau instead.'
    return 1
}

if ($MyInvocation.InvocationName -ne '.') {
    exit (Invoke-BuildSeed -OutDir $OutDir)
}
