param([string]$Archive = '', [string]$Symbol = '')
# scripts/win/check_ar_coff.ps1 — host-tool well-formedness gate for a Microsoft COFF `.lib` static
# archive, pwsh-native (mirror of scripts/check_ar_coff.sh). Asserts a real Windows/LLVM archive
# tool accepts it: `llvm-lib /list` (or `lib.exe /list`) enumerates the member, `dumpbin
# /LINKERMEMBER` parses the two linker members, and — when a symbol is given — it appears as a whole
# word in the linker-member listing.
#
# NATIVE, NO MSYS DANCE: the `.sh` needs MSYS_NO_PATHCONV / cygpath -w so `/list` and the archive
# path survive Git-Bash. In native PowerShell there is no MSYS layer, so this passes native paths
# and `/switches` to the native tools directly.
#
# FAIL-CLOSED by default: AR_CHECK_REQUIRE_TOOLS=1 (the CI default) turns an honest-skip (no archive,
# or no llvm-lib/lib.exe) into a HARD failure; AR_CHECK_REQUIRE_TOOLS=0 is the local-sandbox opt-out.
# Dot-source to reuse Invoke-CheckArCoff; run standalone as check_ar_coff.ps1 <archive.lib> [symbol].

Set-StrictMode -Version Latest
$PSNativeCommandUseErrorActionPreference = $false

. "$PSScriptRoot/common.ps1"

function Invoke-CheckArCoff {
    param([string]$Archive, [string]$Symbol)
    $tag = 'check_ar_coff'
    $require = if ($null -ne $env:AR_CHECK_REQUIRE_TOOLS) { $env:AR_CHECK_REQUIRE_TOOLS } else { '1' }

    function skipOrFail([string]$why) {
        if ($require -eq '1') { Write-CiLog $tag "FAIL — AR_CHECK_REQUIRE_TOOLS=1 but $why — failing closed (this mode never honest-skips)"; return 1 }
        Write-CiLog $tag "skipped — $why"; return 0
    }

    Write-CiLog $tag "starting, AR_CHECK_REQUIRE_TOOLS=$require"

    if (-not $Archive -or -not (Test-Path -LiteralPath $Archive -PathType Leaf)) {
        return (skipOrFail "no archive provided (arg1='$Archive')")
    }
    $Archive = (Resolve-Path -LiteralPath $Archive).Path
    Write-CiLog $tag "checking archive=$Archive symbol=$(if ($Symbol) { $Symbol } else { '<none>' })"

    # Global header magic: '!<arch>\n' = 21 3c 61 72 63 68 3e 0a.
    $magic = [byte[]]::new(8)
    $fs = [System.IO.File]::OpenRead($Archive)
    try { $null = $fs.Read($magic, 0, 8) } finally { $fs.Dispose() }
    $magicHex = ($magic | ForEach-Object { $_.ToString('x2') }) -join ''
    if ($magicHex -ne '213c617263683e0a') { Write-CiLog $tag "FAIL — $Archive has no '!<arch>' global header (got 0x$magicHex)"; return 1 }

    $libTool = $null
    foreach ($t in @('llvm-lib', 'lib.exe', 'lib')) {
        if (Get-Command $t -ErrorAction SilentlyContinue) { $libTool = $t; break }
    }
    if (-not $libTool) { return (skipOrFail 'needs llvm-lib or lib.exe; neither found') }
    Write-CiLog $tag "using LIB_TOOL=$libTool"

    $listOut = & $libTool /list $Archive 2>&1
    if ($LASTEXITCODE -ne 0) {
        $listOut | ForEach-Object { Write-CiLog $tag "  | $_" }
        Write-CiLog $tag "FAIL — $libTool /list rejected $Archive"; return 1
    }
    if (-not ($listOut | Select-String -SimpleMatch '.o' -Quiet)) {
        $listOut | ForEach-Object { Write-CiLog $tag "  | $_" }
        Write-CiLog $tag "FAIL — $libTool /list did not enumerate an .o member in $Archive"; return 1
    }
    Write-CiLog $tag "$libTool /list: enumerated an .o member"

    if (Get-Command dumpbin -ErrorAction SilentlyContinue) {
        $lm = & dumpbin /linkermember $Archive 2>&1
        if ($LASTEXITCODE -ne 0) {
            $lm | ForEach-Object { Write-CiLog $tag "  | $_" }
            Write-CiLog $tag "FAIL — dumpbin /LINKERMEMBER rejected $Archive"; return 1
        }
        if (-not ($lm | Select-String -Pattern 'linker member' -Quiet)) {
            $lm | ForEach-Object { Write-CiLog $tag "  | $_" }
            Write-CiLog $tag "FAIL — dumpbin /LINKERMEMBER found no linker member in $Archive"; return 1
        }
        if ($Symbol) {
            # Whole-word match on dumpbin's own column boundaries — a decorated/prefixed neighbor
            # (e.g. teko_ar_link_run__add contains ar_link_run__add) must not satisfy it.
            $pat = "(^|\s)$([regex]::Escape($Symbol))(\s|$)"
            if (-not ($lm | Select-String -Pattern $pat -Quiet)) {
                $lm | ForEach-Object { Write-CiLog $tag "  | $_" }
                Write-CiLog $tag "FAIL — dumpbin /LINKERMEMBER does not list '$Symbol' as a whole word"; return 1
            }
        }
    } else {
        Write-CiLog $tag 'dumpbin /LINKERMEMBER — skipped, dumpbin not on PATH (not required)'
    }

    Write-CiLog $tag "OK — $Archive is a well-formed, $libTool-consumable COFF static archive"
    return 0
}

if ($MyInvocation.InvocationName -ne '.') {
    exit (Invoke-CheckArCoff -Archive $Archive -Symbol $Symbol)
}
