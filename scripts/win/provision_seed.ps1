param([string]$Label = '', [switch]$PreferCommitted)
# scripts/win/provision_seed.ps1 — put a released (or committed) teko compiler on PATH, pwsh-native.
#
# The Windows mirror of scripts/ci_provision_teko.sh. Same mechanics as install.sh (seed from the
# CANONICAL repo's public releases over HTTPS), but with NATIVE tools only — Invoke-RestMethod /
# Invoke-WebRequest for the download, Get-FileHash for verification, Expand-Archive / the built-in
# `tar.exe` for extraction — NEVER MinGW curl/xz. The `.sh` stays for linux/macos.
#
# ── WHERE IT IS (AND IS NOT) USED ─────────────────────────────────────────────────────────────
# It exists so the Windows `seed-debut` leg (whose SUBJECT is the provisioning path) and any future
# no-degrau Windows producer can provision without Git-Bash. produce_assets.ps1 does NOT call it on
# the FORCED-DEGRAU path: a declared degrau makes the released seed irrelevant (owner 2026-08-18),
# so probing the network for a seed the build will never use is pure cost — it records
# seed_version=degrau instead. The release path here is therefore exercised by CI only when there
# is no degrau; the committed path only once bootstrap/seeds/ exists (absent today).
#
# Extracts into ./.seed and appends it to $GITHUB_PATH (when set) so later steps call `teko`.
#
# CI-ONLY: that the release ZIP unpacks to `.seed/teko.exe`; that the committed `.xz` path finds a
# decompressor (python3 is on the GitHub Windows image — it is NOT MinGW); that api.github.com is
# reachable within the runner's rate limit (GH_TOKEN, when set, is added as a bearer header).

Set-StrictMode -Version Latest
$PSNativeCommandUseErrorActionPreference = $false
$ProgressPreference = 'SilentlyContinue'

. "$PSScriptRoot/common.ps1"

$script:SeedRepo = if ($env:TEKO_SEED_REPO) { $env:TEKO_SEED_REPO } else { 'teko-org/teko-lang' }
$script:SeedsDir = if ($env:TEKO_SEEDS_DIR) { $env:TEKO_SEEDS_DIR } else { 'bootstrap/seeds' }

# Get-HostSeedLabel LABEL — the committed-seed label for a release LABEL, or '' (ci_provision's
# host_seed_label): a release target maps onto the HOST that runs the compiler.
function Get-HostSeedLabel {
    param([string]$Label)
    switch -Regex ($Label) {
        '^linux-x86_64(-.*)?$' { return 'linux-x86_64' }
        '^linux-arm64(-.*)?$'  { return 'linux-arm64' }
        '^macos-arm64$'        { return 'macos-arm64' }
        '^windows-x86_64$'     { return 'windows-x86_64' }
        default                { return '' }
    }
}

# Expand-XzToFile IN OUT — decompress a bare `.xz` to OUT. Native `tar.exe` cannot open a bare .xz
# (it is not an archive), so this uses python3's lzma (present on the GitHub Windows image; NOT
# MinGW). Returns $true/$false.
function Expand-XzToFile {
    param([string]$In, [string]$Out)
    foreach ($py in @('python3', 'python')) {
        if (Get-Command $py -ErrorAction SilentlyContinue) {
            $code = 'import lzma,sys' + "`n" + 'open(sys.argv[2],"wb").write(lzma.open(sys.argv[1]).read())'
            & $py -c $code $In $Out
            if ($LASTEXITCODE -eq 0) { return $true }
        }
    }
    Write-CiLog 'provision' 'no xz decompressor available (looked for python3, python)'
    return $false
}

# Get-Sha256FromSums SUMSFILE NAME — the digest listed for NAME in a `<sha>  <name>` sums file, or ''
function Get-Sha256FromSums {
    param([string]$SumsFile, [string]$Name)
    if (-not (Test-Path -LiteralPath $SumsFile -PathType Leaf)) { return '' }
    foreach ($line in Get-Content -LiteralPath $SumsFile) {
        $t = $line.Trim() -split '\s+'
        if ($t.Count -ge 2) {
            $n = $t[1] -replace '^\*', ''
            if ($n -eq $Name -or $t[1] -eq "*$Name") { return $t[0].ToLowerInvariant() }
        }
    }
    return ''
}

function Add-SeedToPath {
    param([string]$SeedDir)
    $abs = (Resolve-Path -LiteralPath $SeedDir).Path
    $env:PATH = "$abs;$env:PATH"
    if ($env:GITHUB_PATH) { Add-Content -LiteralPath $env:GITHUB_PATH -Value $abs }
    return $abs
}

# Invoke-SeedFromCommitted LABEL — provision from bootstrap/seeds/. sha256 is checked BEFORE
# decompression (ci_provision's ordering). Returns $true/$false.
function Invoke-SeedFromCommitted {
    param([string]$Label)
    $seedLabel = Get-HostSeedLabel $Label
    if (-not $seedLabel) { Write-CiLog 'provision' "no committed host seed exists for '$Label'"; return $false }
    $sums = Join-Path $script:SeedsDir 'SEEDS.sha256'
    if (-not (Test-Path -LiteralPath $sums -PathType Leaf)) { Write-CiLog 'provision' "no committed seed manifest at $sums"; return $false }

    $archive = Join-Path $script:SeedsDir "teko-$seedLabel.xz"
    $exeSuffix = ''
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
        $archive = Join-Path $script:SeedsDir "teko-$seedLabel.exe.xz"
        $exeSuffix = '.exe'
    }
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) { Write-CiLog 'provision' "no committed seed archive for '$seedLabel'"; return $false }

    $want = Get-Sha256FromSums $sums (Split-Path -Leaf $archive)
    if (-not $want) { Write-CiLog 'provision' "$archive is absent from $sums — refusing an unlisted seed"; return $false }
    $digest = Get-Sha256Hex $archive
    if ($want -ne $digest) { Write-CiLog 'provision' "$archive sha256 mismatch (manifest $want, file $digest) — refusing it"; return $false }

    if (Test-Path -LiteralPath '.seed') { Remove-Item -Recurse -Force -LiteralPath '.seed' }
    New-Item -ItemType Directory -Force -Path '.seed' | Out-Null
    $bin = Join-Path '.seed' "teko$exeSuffix"
    if (-not (Expand-XzToFile $archive $bin)) { Remove-Item -Recurse -Force -LiteralPath '.seed' -ErrorAction SilentlyContinue; return $false }
    $ver = (& $bin --version 2>$null | Select-Object -First 1)
    if (-not $ver) { Write-CiLog 'provision' "the committed seed for '$seedLabel' did not run"; Remove-Item -Recurse -Force -LiteralPath '.seed' -ErrorAction SilentlyContinue; return $false }
    $dir = Add-SeedToPath '.seed'
    Write-CiLog 'provision' "committed seed '$seedLabel' verified and ready at $dir (version $ver)"
    return $true
}

# Get-ReleaseTags — candidate release tags, NEWEST 4-part version first (ci_provision's TAGS).
function Get-ReleaseTags {
    $headers = @{ 'Accept' = 'application/vnd.github+json' }
    if ($env:GH_TOKEN) { $headers['Authorization'] = "Bearer $($env:GH_TOKEN)" }
    try {
        $rels = Invoke-RestMethod -Uri "https://api.github.com/repos/$($script:SeedRepo)/releases?per_page=100" -Headers $headers
    } catch {
        Write-CiLog 'provision' "cannot list releases for $($script:SeedRepo): $($_.Exception.Message)"
        return @()
    }
    return $rels |
        ForEach-Object { $_.tag_name } |
        Where-Object { $_ -match '^v?[0-9]+(\.[0-9]+){3}' } |
        Sort-Object -Descending { [version](($_ -replace '^v', '') -replace '-.*$', '') }
}

# Invoke-SeedFromTag TAG LABEL — try one release. Returns $true/$false.
function Invoke-SeedFromTag {
    param([string]$Tag, [string]$Label)
    $base = "https://github.com/$($script:SeedRepo)/releases/download/$Tag"
    Get-ChildItem -Path . -Filter 'teko-*.zip' -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath 'SHA256SUMS.txt' -Force -ErrorAction SilentlyContinue

    $archive = ''
    foreach ($ext in @('zip', 'tar.gz')) {
        $name = "teko-$Label.$ext"
        try {
            Invoke-WebRequest -Uri "$base/$name" -OutFile $name -ErrorAction Stop
            $archive = $name; break
        } catch { }
    }
    if (-not $archive) { Write-CiLog 'provision' "$Tag has no asset for '$Label' — trying older"; return $false }

    $digest = Get-Sha256Hex $archive
    try { Invoke-WebRequest -Uri "$base/SHA256SUMS.txt" -OutFile 'SHA256SUMS.txt' -ErrorAction Stop }
    catch { Write-CiLog 'provision' "$Tag has no SHA256SUMS.txt — refusing an unverified seed"; return $false }
    $want = Get-Sha256FromSums 'SHA256SUMS.txt' $archive
    if (-not $want -or $want -ne $digest) { Write-CiLog 'provision' "$Tag sha256 mismatch for $archive — trying older"; return $false }

    if (Test-Path -LiteralPath '.seed') { Remove-Item -Recurse -Force -LiteralPath '.seed' }
    New-Item -ItemType Directory -Force -Path '.seed' | Out-Null
    if ($archive -like '*.zip') {
        Expand-Archive -LiteralPath $archive -DestinationPath '.seed' -Force
    } else {
        & tar -xzf $archive -C '.seed' 2>&1 | ForEach-Object { Write-CiLog 'provision' $_ }
        if ($LASTEXITCODE -ne 0) { Write-CiLog 'provision' "$Tag $archive did not unpack"; return $false }
    }
    $bin = Resolve-TekoBin '.seed'
    if (-not $bin) { Write-CiLog 'provision' "$Tag unpacked but no teko[.exe] in .seed"; return $false }

    # VERSION SANITY: the seed must report EXACTLY its own tag's version number (anchored, not a
    # substring — ci_provision's rule; `v0.3.1.4` must not accept a binary reporting `0.3.1.40`).
    $expectNum = ($Tag -replace '^v', '') -replace '-.*$', ''
    $ver = (& $bin --version 2>$null | Select-Object -First 1)
    if (-not $ver) { Write-CiLog 'provision' "$Tag seed did not report a version — trying older"; return $false }
    $verNum = ($ver -split '\s+')[-1]; $verNum = $verNum -replace '-.*$', ''
    if ($verNum -ne $expectNum) { Write-CiLog 'provision' "$Tag version mismatch: tag $expectNum, binary '$ver' — trying older"; return $false }

    $dir = Add-SeedToPath '.seed'
    Write-CiLog 'provision' "teko $Tag ready at $dir (version $ver)"
    return $true
}

function Invoke-ProvisionSeed {
    param([string]$Label, [switch]$PreferCommitted)
    if (-not $Label) { Write-CiLog 'provision' 'usage: provision_seed.ps1 <LABEL> [-PreferCommitted]'; return 1 }

    if ($PreferCommitted -or $env:TEKO_SEED_PREFER_COMMITTED -eq '1') {
        Write-CiLog 'provision' "taking the committed seed for '$Label', no release probe"
        if (Invoke-SeedFromCommitted $Label) { return 0 }
        Write-CiLog 'provision' "the committed seed was demanded but is not usable for '$Label'"
        return 1
    }

    Write-CiLog 'provision' "newest-first seed from $($script:SeedRepo) (label '$Label')"
    foreach ($tag in (Get-ReleaseTags)) {
        if (Invoke-SeedFromTag $tag $Label) { return 0 }
    }
    Write-CiLog 'provision' "no usable release seed for '$Label' — trying the committed seed"
    if (Invoke-SeedFromCommitted $Label) { return 0 }
    Write-CiLog 'provision' "no usable seed for '$Label': neither a release nor $($script:SeedsDir) could serve one"
    return 1
}

if ($MyInvocation.InvocationName -ne '.') {
    exit (Invoke-ProvisionSeed -Label $Label -PreferCommitted:$PreferCommitted)
}
