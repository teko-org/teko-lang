param([string]$Gen1 = 'out/teko', [string]$Proj = '.', [string]$Work = '.fixpoint')
# scripts/win/fixpoint_gate.ps1 — THE SELF-HOSTING FIXPOINT on Windows, pwsh-native.
#
# The Windows mirror of scripts/fixpoint_gate.sh (the `.sh` stays for linux/macos). Same verdict,
# unchanged in every state of the world (owner 2026-07-28): gen2 == gen3.
#
#   gen1 --(TEKO_FIXPOINT_BACKEND)--> gen2 --(same)--> gen3        ASSERT gen2 == gen3
#
# build_seed.ps1 hands this its gen1 (built down the C route); this owns the last two links and the
# verdict. gen1 is NOT compared — it comes out of clang over emitted C while gen2 comes out of
# teko's own backend; two different generators cannot agree byte for byte.
#
# ZERO-C is checked too, ARMED BY THE BACKEND, not beside it: a `native` generation that emits C is
# a defect (armed -> fails); a `c` generation emitting C is the designed state (reported, not
# enforced). $env:TEKO_FIXPOINT_ZERO_C overrides in both directions. The Windows leg's
# fixpoint_backend is looked up from scripts/ci_producer_matrix.sh (passed as
# $env:TEKO_FIXPOINT_BACKEND by pr.yml) — `native` as of this wagon, so the emission check is armed
# and the leg is EXPECTED RED until the native backend builds the compiler; that red is the
# measurement, not a regression to paper over.
#
# CI-ONLY: that gen1 rebuilds the source under the native backend at all (it may honest-stop); the
# byte-identity of two Windows PEs; that a native gen2/gen3 emits no out/teko.c.

Set-StrictMode -Version Latest
$PSNativeCommandUseErrorActionPreference = $false

. "$PSScriptRoot/common.ps1"
. "$PSScriptRoot/degrau.ps1"

$Tag = 'fixpoint'
$script:Soft = ($env:TEKO_FIXPOINT_SOFT -eq '1')

# Get-ProjectCFiles — the sorted set of every *.c in the project tree, excluding .git and this
# gate's own work dir. Two of these (before/after a build) attribute a .c to the generation that
# wrote it — the attribution scripts/no_emitted_c.sh cannot do.
function Get-ProjectCFiles {
    param([string]$ProjAbs, [string]$WorkAbs)
    Get-ChildItem -LiteralPath $ProjAbs -Recurse -Filter '*.c' -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notlike (Join-Path $ProjAbs '.git*') -and -not $_.FullName.StartsWith($WorkAbs) } |
        ForEach-Object { $_.FullName } | Sort-Object
}

function Invoke-Fixpoint {
    param([string]$Gen1, [string]$Proj, [string]$Work)

    $gen1Path = Resolve-ExistingBin $Gen1
    if (-not $gen1Path) { Write-CiLog $Tag "gen1 '$Gen1' is not a file"; return 1 }
    $projAbs = (Resolve-Path -LiteralPath $Proj).Path

    $rtDir = $env:TEKO_FIXPOINT_RT_DIR
    if (-not $rtDir) { $rtDir = Join-Path $projAbs 'src/runtime' }
    if (-not (Test-Path -LiteralPath $rtDir)) { $rtDir = '' }

    $backend = $env:TEKO_FIXPOINT_BACKEND
    if (-not $backend) { $backend = 'c' }

    if (Test-Path -LiteralPath $Work) { Remove-Item -Recurse -Force -LiteralPath $Work }
    New-Item -ItemType Directory -Force -Path $Work | Out-Null
    $workAbs = (Resolve-Path -LiteralPath $Work).Path
    $outDir = Join-Path $workAbs 'out'

    # Native-only diagnostic gates (fixpoint_gate.sh's TEKO_NATIVE_* env), so a native honest-stop
    # names the exact item/const/region it died on in the streamed log. Byte-identical when off.
    $extraEnv = $null
    if ($backend -eq 'native') {
        $extraEnv = @{
            'TEKO_NATIVE_TRACE_ITEMS'        = '1'
            'TEKO_NATIVE_RODATA_ALIGN_CHECK' = '1'
            'TEKO_NATIVE_CALL_SAFETY_CHECK'  = '1'
            'TEKO_NATIVE_REGION_CHECK'       = '1'
            'TEKO_NATIVE_CONST_SIZE_CHECK'   = '1'
        }
    }

    $script:Emissions = @{}

    # Build-Gen BIN — scan-before, build BIN(source) at the FIXED path $outDir, return exit code.
    function Build-Gen {
        param([string]$Bin)
        if (Test-Path -LiteralPath $outDir) { Remove-Item -Recurse -Force -LiteralPath $outDir }
        $script:BeforeC = Get-ProjectCFiles $projAbs $workAbs
        return (Invoke-TekoBuild -Bin $Bin -ProjDir $projAbs -Out $outDir -RtDir $rtDir -Backend $backend -Tag $Tag -ExtraEnv $extraEnv)
    }

    # Record-Emission NAME — the sorted list of every .c that generation wrote (out/*.c + the
    # before/after project delta), and its count.
    function Record-Emission {
        param([string]$Name)
        $after = Get-ProjectCFiles $projAbs $workAbs
        $delta = @($after | Where-Object { $_ -notin $script:BeforeC })
        $outC = @(Get-ChildItem -LiteralPath $outDir -Recurse -Filter '*.c' -File -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
        $emitted = @($delta + $outC | Sort-Object -Unique)
        $script:Emissions[$Name] = $emitted
    }

    # Take-Gen NAME — record emission, preserve out/teko.c as $Work/NAME.c, move the binary to
    # $Work/NAME.exe (kept runnable for the next generation), free $outDir. Returns $true/$false.
    function Take-Gen {
        param([string]$Name)
        Record-Emission $Name
        $outC = Join-Path $outDir 'teko.c'
        if (Test-Path -LiteralPath $outC -PathType Leaf) { Copy-Item -LiteralPath $outC -Destination (Join-Path $workAbs "$Name.c") }
        $bin = Resolve-TekoBin $outDir
        if (-not $bin) { return $false }
        Move-Item -LiteralPath $bin -Destination (Join-Path $workAbs "$Name.exe")
        Remove-Item -Recurse -Force -LiteralPath $outDir -ErrorAction SilentlyContinue
        return $true
    }

    # ── observe the PROVENANCE of gen1 (explains the run; selects nothing) ──────────────────────
    $deg = Read-Degrau $projAbs
    if ($deg.Status -eq 2) { Write-CiLog $Tag 'the degrau declaration is broken — refusing to guess which chain produced gen1'; return 1 }
    if ($deg.Status -eq 0) { Write-DegrauReport $deg; Write-CiLog $Tag 'provenance = degrau — gen1 descends from the DECLARED C, through clang' }
    else { Write-DegrauReport $deg; Write-DegrauUndeclaredCNote $projAbs; Write-CiLog $Tag 'provenance = normal — release -> gen0 -> gen1' }

    Write-CiLog $Tag "gen1 = $gen1Path"
    Write-CiLog $Tag "source = $projAbs"
    Write-CiLog $Tag "backend (gen2/gen3) = $backend"
    & $gen1Path --version 2>&1 | ForEach-Object { Write-CiLog $Tag "  | $_" }

    # stage gen1's emitted C beside the generations that vouch for it.
    $gen1C = $env:TEKO_FIXPOINT_GEN1_C
    if (-not $gen1C) { $gen1C = Join-Path (Split-Path -Parent $gen1Path) 'teko.c' }
    if (Test-Path -LiteralPath $gen1C -PathType Leaf) {
        Copy-Item -LiteralPath $gen1C -Destination (Join-Path $workAbs 'gen1.c')
        Write-CiLog $Tag "gen1's emitted C staged: $(Join-Path $workAbs 'gen1.c') ($((Get-Item -LiteralPath $gen1C).Length) bytes)"
        $script:Emissions['gen1'] = @($gen1C)
    } else {
        Write-CiLog $Tag "gen1 emitted no C beside it ($gen1C is absent) — nothing to harvest from this run."
        $script:Emissions['gen1'] = @()
    }

    # ── gen2 = gen1(source) ──────────────────────────────────────────────────────────────────────
    Write-CiLog $Tag 'building gen2 = gen1(source) ...'
    if ((Build-Gen $gen1Path) -ne 0) { return (Fail-Verdict 'gen1 does not build the source it came from — the compiler does not self-host, so gen2 does not exist. See the address above.') }
    if (-not (Take-Gen 'gen2')) { return (Fail-Verdict 'the gen2 build reported success but left no binary') }
    Write-CiLog $Tag "gen2 ready ($((Get-Item -LiteralPath (Join-Path $workAbs 'gen2.exe')).Length) bytes)"

    # ── gen3 = gen2(source) ──────────────────────────────────────────────────────────────────────
    Write-CiLog $Tag 'building gen3 = gen2(source) ...'
    if ((Build-Gen (Join-Path $workAbs 'gen2.exe')) -ne 0) { return (Fail-Verdict 'gen2 built but does not rebuild the source — the chain breaks at the second generation.') }
    if (-not (Take-Gen 'gen3')) { return (Fail-Verdict 'the gen3 build reported success but left no binary') }
    Write-CiLog $Tag "gen3 ready ($((Get-Item -LiteralPath (Join-Path $workAbs 'gen3.exe')).Length) bytes)"

    # ── the two assertions, reported separately ──────────────────────────────────────────────────
    $fixOk = $true
    $gen2Hash = (Get-FileHash -LiteralPath (Join-Path $workAbs 'gen2.exe') -Algorithm SHA256).Hash
    $gen3Hash = (Get-FileHash -LiteralPath (Join-Path $workAbs 'gen3.exe') -Algorithm SHA256).Hash
    if ($gen2Hash -eq $gen3Hash) {
        Write-CiLog $Tag 'byte-identity: gen2 == gen3  OK'
    } else {
        $fixOk = $false
        Write-CiLog $Tag 'byte-identity: gen2 != gen3  X'
        Write-CiLog $Tag "  gen2 $((Get-Item -LiteralPath (Join-Path $workAbs 'gen2.exe')).Length) bytes, gen3 $((Get-Item -LiteralPath (Join-Path $workAbs 'gen3.exe')).Length) bytes"
    }

    # emitted-C identity (only while gen2/gen3 still run the C route and leave a .c)
    $g2c = Join-Path $workAbs 'gen2.c'; $g3c = Join-Path $workAbs 'gen3.c'
    if ((Test-Path -LiteralPath $g2c) -and (Test-Path -LiteralPath $g3c)) {
        if ((Get-FileHash -LiteralPath $g2c).Hash -eq (Get-FileHash -LiteralPath $g3c).Hash) {
            Write-CiLog $Tag "emitted-C identity: gen2.c == gen3.c  OK ($((Get-Item -LiteralPath $g3c).Length) bytes)"
        } else {
            Write-CiLog $Tag 'emitted-C identity: gen2.c != gen3.c — the binaries agree but their C does not'
        }
    }
    if (Test-Path -LiteralPath (Join-Path $workAbs 'gen1.c')) {
        Write-CiLog $Tag "harvest: $(Join-Path $workAbs 'gen1.c') is this run's candidate for bootstrap/teko.c"
    }

    # ── THE EMISSION CHECK, armed by the backend ────────────────────────────────────────────────
    $zeroCEnforced = if ($null -ne $env:TEKO_FIXPOINT_ZERO_C) { ($env:TEKO_FIXPOINT_ZERO_C -eq '1') } else { ($backend -eq 'native') }
    $zeroCOk = $true
    $tally = @()
    foreach ($g in @('gen2', 'gen3')) {
        $n = $script:Emissions[$g].Count
        $tally += "$g=$n"
        if ($n -ne 0) { $zeroCOk = $false }
    }
    $tallyStr = $tally -join ' '
    if ($zeroCOk) {
        Write-CiLog $Tag "zero-C: no generation under test emitted any .c  OK ($tallyStr)"
    } elseif ($zeroCEnforced) {
        $reason = if ($backend -eq 'native') { 'a native generation must emit no C at all' } else { "zero-C armed by hand (TEKO_FIXPOINT_ZERO_C=1) on the $backend route" }
        Write-CiLog $Tag "zero-C: $tallyStr  X (ENFORCED — $reason)"
        foreach ($g in @('gen2', 'gen3')) { foreach ($p in $script:Emissions[$g]) { Write-CiLog $Tag "        $g emitted: $p" } }
        $fixOk = $false
    } else {
        Write-CiLog $Tag "zero-C: $tallyStr  — REPORTED, not enforced (this leg still builds gen2/gen3 down the C route)"
        foreach ($g in @('gen2', 'gen3')) { foreach ($p in $script:Emissions[$g]) { Write-CiLog $Tag "        $g emitted: $p" } }
    }

    if (-not $fixOk) { return (Fail-Verdict 'the fixpoint has not arrived — see the two lines above for which half is outstanding') }

    if ($zeroCOk) { Write-CiLog $Tag 'VERDICT: PASSED — gen2 == gen3 byte for byte, and neither emitted C' }
    else { Write-CiLog $Tag 'VERDICT: PASSED — gen2 == gen3 byte for byte. (They still emit C on the C route.)' }

    Remove-Item -Recurse -Force -LiteralPath $outDir -ErrorAction SilentlyContinue
    return 0
}

# Fail-Verdict MESSAGE — the fixpoint does not hold. Returns the exit code (1), or 0 under
# TEKO_FIXPOINT_SOFT (report without failing). There is no outcome besides PASSED or this.
function Fail-Verdict {
    param([string]$Message)
    Write-CiLog $Tag "VERDICT: FAILED — $Message"
    if ($script:Soft) {
        Write-CiLog $Tag 'TEKO_FIXPOINT_SOFT=1 — reporting without failing the lane. The verdict above stands.'
        return 0
    }
    return 1
}

if ($MyInvocation.InvocationName -ne '.') {
    exit (Invoke-Fixpoint -Gen1 $Gen1 -Proj $Proj -Work $Work)
}
