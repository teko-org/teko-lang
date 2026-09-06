# scripts/win/common.ps1 — shared PowerShell helpers for the WINDOWS-ONLY native CI path.
#
# ── WHY THIS FILE EXISTS (owner ruling 2026-08-18) ────────────────────────────────────────────
# "a falha real é rodar em mingw, ao invés do powershell nativo usando as ferramentas corretas.
# Mingw é lento, obeso e fraco." The Windows legs of .github/workflows/pr.yml used to run every
# step under `shell: bash` — which on a windows-latest runner is Git-Bash/MinGW — and to shell out
# to the POSIX `scripts/*.sh`. This directory is the pwsh-native mirror of ONLY the Windows path:
# the `.sh` scripts stay exactly as they are and keep serving linux/macos. These `.ps1` files
# duplicate the LOGIC the Windows leg needs, in native PowerShell, calling native tools
# (clang --target=x86_64-pc-windows-msvc, lld-link, llvm-lib, dumpbin) and .NET/cmdlets
# (Invoke-WebRequest, Expand-Archive, Get-FileHash) — never MinGW curl/xz/gcc, never Git-Bash.
#
# This is a helper module: it is DOT-SOURCED (`. "$PSScriptRoot/common.ps1"`) by the entry
# scripts and defines functions only; it runs nothing on its own.
#
# ── THINGS THAT ONLY CI CAN CONFIRM (documented per the ruling) ───────────────────────────────
# There is no Windows here to test on. Each function below was written from the `.sh` it mirrors;
# the points that only a real windows-latest run can settle are marked `# CI-ONLY:` inline.

Set-StrictMode -Version Latest

# Native command non-zero exit must NOT throw — every native call in this path is followed by an
# explicit `$LASTEXITCODE` check, exactly like the `.sh` originals check `$?`. Without this,
# pwsh 7.3+ with $ErrorActionPreference='Stop' turns a compiler's honest non-zero into a
# terminating error and the diagnostic-printing branch below it never runs.
$PSNativeCommandUseErrorActionPreference = $false

# Write-CiLog TAG MESSAGE — the pwsh twin of the `.sh` `log()` helpers (`printf '%s\n' ... >&2`).
# Goes to stderr so a step's real product (a staged path, a version string) stays on stdout.
function Write-CiLog {
    param([string]$Tag, [string]$Message)
    [Console]::Error.WriteLine("${Tag}: $Message")
}

# Resolve-TekoBin DIR — the pwsh twin of build_with_seed_fallback.sh's `resolve_bin`: echoes the
# built teko binary under DIR (teko.exe preferred on Windows, then extensionless teko), or $null
# when neither is a file. Returns the FULL path.
function Resolve-TekoBin {
    param([string]$Dir)
    $exe = Join-Path $Dir 'teko.exe'
    $bare = Join-Path $Dir 'teko'
    if (Test-Path -LiteralPath $exe -PathType Leaf) { return (Resolve-Path -LiteralPath $exe).Path }
    if (Test-Path -LiteralPath $bare -PathType Leaf) { return (Resolve-Path -LiteralPath $bare).Path }
    return $null
}

# Resolve-ExistingBin PATHNAME — given a path the caller wrote WITHOUT an extension (the workflow
# passes `out/teko`, the POSIX habit), return the file that actually exists: PATHNAME itself, or
# PATHNAME with `.exe` appended. clang and teko both emit `teko.exe` on a Windows target, so a bare
# `& out/teko` would fail where git-bash would have appended `.exe` on exec. $null when neither
# exists.
function Resolve-ExistingBin {
    param([string]$PathName)
    if (Test-Path -LiteralPath $PathName -PathType Leaf) { return (Resolve-Path -LiteralPath $PathName).Path }
    $withExe = "$PathName.exe"
    if (Test-Path -LiteralPath $withExe -PathType Leaf) { return (Resolve-Path -LiteralPath $withExe).Path }
    return $null
}

# Get-Sha256Hex FILE — lowercase hex sha256, matching the `.sh` `sha256sum | awk '{print $1}'`
# shape (Get-FileHash returns UPPERCASE; the digests these compare against are lowercase). Prints
# 'absent' for a missing file so a gap can never read as a matching digest (produce_assets.sh's
# own rule).
function Get-Sha256Hex {
    param([string]$File)
    if (-not (Test-Path -LiteralPath $File -PathType Leaf)) { return 'absent' }
    return (Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash.ToLowerInvariant()
}

# Get-FileSizeBytes FILE — byte count, `0` for a missing file (produce_assets.sh's `size_of`).
function Get-FileSizeBytes {
    param([string]$File)
    if (-not (Test-Path -LiteralPath $File -PathType Leaf)) { return 0 }
    return (Get-Item -LiteralPath $File).Length
}

# Get-MachineWord BIN — the target machine of a PE, read from its OWN header with no external tool,
# the pwsh twin of produce_assets.sh's `machine_word_of` (which used `od`). Only the PE branch is
# needed on Windows (a `native` Windows asset is always a PE); Mach-O/ELF are never staged here.
#   PE: 'MZ' (0x5A4D LE) at 0, e_lfanew (u32 LE) at 0x3C, COFF Machine (u16) right after 'PE\0\0':
#       0x8664 -> x86_64, 0xAA64 -> arm64.
# Returns 'x86_64', 'arm64', or '' when the format is not a PE this understands.
function Get-MachineWord {
    param([string]$Bin)
    try {
        $fs = [System.IO.File]::OpenRead($Bin)
        try {
            if ($fs.Length -lt 0x40) { return '' }
            $br = New-Object System.IO.BinaryReader($fs)
            if ($br.ReadUInt16() -ne 0x5A4D) { return '' }   # 'MZ'
            $fs.Position = 0x3C
            $peOff = $br.ReadUInt32()
            if (($peOff + 6) -gt $fs.Length) { return '' }
            $fs.Position = $peOff
            if ($br.ReadUInt32() -ne 0x00004550) { return '' } # 'PE\0\0'
            switch ($br.ReadUInt16()) {
                0x8664 { return 'x86_64' }
                0xAA64 { return 'arm64' }
                default { return '' }
            }
        } finally { $fs.Dispose() }
    } catch {
        return ''
    }
}

# Get-ArchKeywordForLabel LABEL — the machine keyword an asset label PROMISES, or '' when the label
# carries no architecture claim (produce_assets.sh's `arch_keyword_for`).
function Get-ArchKeywordForLabel {
    param([string]$Label)
    if ($Label -match '(^|-)x86_64($|-)') { return 'x86_64' }
    if ($Label -match '(^|-)arm64($|-)')  { return 'arm64' }
    return ''
}

# Invoke-TekoBuild BIN PROJDIR OUT RTDIR BACKEND — the pwsh twin of build_with_seed_fallback.sh's
# `build_project`: runs "BIN . -o OUT --no-verify --release" with cwd PROJDIR, TK_RT_DIR pinned to
# RTDIR (when non-empty) and TEKO_BACKEND pinned to BACKEND. Streams the compiler's combined output
# live (prefixed), and RETURNS the build's own exit code — it does not throw. The env pins are
# set around this ONE call and cleared after, mirroring the `.sh` subshell so nothing leaks to the
# steps below (fixpoint_gate.sh's whole "a wide TEKO_BACKEND pin leaks into everything" warning).
function Invoke-TekoBuild {
    param(
        [string]$Bin,
        [string]$ProjDir,
        [string]$Out,
        [string]$RtDir = '',
        [string]$Backend = 'c',
        [string]$Tag = 'teko-ci',
        [hashtable]$ExtraEnv = $null
    )
    $savedRt = $env:TK_RT_DIR
    $savedBackend = $env:TEKO_BACKEND
    $savedExtra = @{}
    if ($ExtraEnv) { foreach ($k in $ExtraEnv.Keys) { $savedExtra[$k] = [Environment]::GetEnvironmentVariable($k) } }
    Push-Location -LiteralPath $ProjDir
    try {
        if ($RtDir) { $env:TK_RT_DIR = ($RtDir -replace '\\','/') } else { Remove-Item Env:TK_RT_DIR -ErrorAction SilentlyContinue }
        $env:TEKO_BACKEND = $Backend
        if ($ExtraEnv) { foreach ($k in $ExtraEnv.Keys) { Set-Item -Path "Env:$k" -Value $ExtraEnv[$k] } }
        & $Bin . -o $Out --no-verify --release 2>&1 | ForEach-Object { Write-CiLog $Tag "  | $_" }
        return $LASTEXITCODE
    } finally {
        Pop-Location
        if ($null -eq $savedRt) { Remove-Item Env:TK_RT_DIR -ErrorAction SilentlyContinue } else { $env:TK_RT_DIR = $savedRt }
        if ($null -eq $savedBackend) { Remove-Item Env:TEKO_BACKEND -ErrorAction SilentlyContinue } else { $env:TEKO_BACKEND = $savedBackend }
        if ($ExtraEnv) {
            foreach ($k in $ExtraEnv.Keys) {
                if ($null -eq $savedExtra[$k]) { Remove-Item "Env:$k" -ErrorAction SilentlyContinue } else { Set-Item -Path "Env:$k" -Value $savedExtra[$k] }
            }
        }
    }
}

# Get-ManifestTag [MANIFEST] — the pwsh twin of derive_version.sh: reads `version` and `suffix`
# from teko.tkp (TOML) and returns `v<version>[-<suffix>]`. Best-effort for PROVENANCE.txt; returns
# 'unknown' on any parse failure rather than aborting the producer (the `.sh` caller wraps it in
# `|| echo unknown` too).
function Get-ManifestTag {
    param([string]$Manifest = 'teko.tkp')
    try {
        if (-not (Test-Path -LiteralPath $Manifest -PathType Leaf)) { return 'unknown' }
        $version = ''
        $suffix = ''
        foreach ($line in Get-Content -LiteralPath $Manifest) {
            if ($line -match '^\s*#') { continue }
            if (-not $version -and $line -match '^\s*version\s*=\s*"([^"]*)"') { $version = $Matches[1] }
            if (-not $suffix  -and $line -match '^\s*suffix\s*=\s*"([^"]*)"')  { $suffix  = $Matches[1] }
        }
        if (-not $version) { return 'unknown' }
        if ($version -notmatch '^\d+\.\d+\.\d+\.\d+$') { return 'unknown' }
        if ($suffix) { return "v$version-$suffix" }
        return "v$version"
    } catch {
        return 'unknown'
    }
}

# Get-HostUname — a best-effort stand-in for `uname -srm`, recorded in PROVENANCE.txt. There is no
# `uname` on native Windows; this reports the same three facts (kernel name, release, machine) from
# .NET. CI-ONLY: the exact string differs from Git-Bash's `MINGW64_NT-10.0 ... x86_64`, but the
# field is informational and never compared.
function Get-HostUname {
    try {
        $os = [System.Environment]::OSVersion.Version.ToString()
        $arch = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
        return "Windows $os $arch"
    } catch {
        return 'Windows unknown'
    }
}
