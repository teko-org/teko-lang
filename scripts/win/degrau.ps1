# scripts/win/degrau.ps1 — the pwsh mirror of scripts/degrau.sh for the WINDOWS path.
#
# Answers the one question "IS THERE A DECLARED DEGRAU?" from the versioned `bootstrap/DEGRAU`
# claim, exactly as degrau.sh does for the POSIX legs. The `.sh` stays authoritative for
# linux/macos; this duplicates ONLY the parse + verdict the Windows build path needs, so no Windows
# step has to shell out to `sh scripts/degrau.sh`.
#
# Owner ruling 2026-07-28: "só podemos usar teko.c se e somente se identificarmos degrau." The
# criterion is a HUMAN CLAIM (a committed `bootstrap/DEGRAU`), not the mere presence of
# `bootstrap/teko.c` — under the 0.3.1.0 chain that C is an OUTPUT, harvested from gen1, so its
# presence means nothing and the declaration means everything.
#
# Dot-sourced (`. "$PSScriptRoot/degrau.ps1"`); defines functions only.

# Read-Degrau ROOT — the pwsh twin of degrau.sh's `degrau_scan`. Returns a hashtable:
#   @{ Status = 0|1|2; File; C; Why; Since }
# Status 0 = a degrau IS declared and complete; 1 = none declared (the normal chain); 2 = a
# declaration exists but is BROKEN — which every caller MUST treat as fatal, never as "no degrau".
# $env:TEKO_DEGRAU_FILE overrides the path (for testing), exactly as the `.sh` honours it.
function Read-Degrau {
    param([string]$Root = (Get-Location).Path)

    $file = $env:TEKO_DEGRAU_FILE
    if (-not $file) { $file = Join-Path $Root 'bootstrap/DEGRAU' }

    $res = @{ Status = 1; File = $file; C = ''; Why = ''; Since = '' }
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { return $res }

    $c     = Get-DegrauField $file 'c'
    $why   = Get-DegrauField $file 'why'
    $since = Get-DegrauField $file 'since'
    $res.C = $c; $res.Why = $why; $res.Since = $since

    if (-not $c -or -not $why) {
        Write-CiLog 'degrau' "FATAL: '$file' exists but is not a declaration."
        Write-CiLog 'degrau' 'A degrau needs BOTH a bridge and a reason. Required keys:'
        Write-CiLog 'degrau' '  c:    bootstrap/teko.c      the C that bridges the gap'
        Write-CiLog 'degrau' '  why:  <one sentence>        what the published release cannot build'
        Write-CiLog 'degrau' 'Delete the file if there is no degrau — an empty claim is worse than none.'
        $res.Status = 2
        return $res
    }

    # Resolve a relative bridge against ROOT (degrau.sh's `case "$DEGRAU_C" in /*) ...`).
    if (-not [System.IO.Path]::IsPathRooted($c)) {
        $c = Join-Path $Root $c
        $res.C = $c
    }
    if (-not (Test-Path -LiteralPath $c -PathType Leaf)) {
        Write-CiLog 'degrau' "FATAL: '$file' declares a degrau bridged by '$c', which does not exist."
        Write-CiLog 'degrau' 'The declaration and the bridge are committed together or not at all.'
        $res.Status = 2
        return $res
    }

    $res.Status = 0
    return $res
}

# Get-DegrauField FILE KEY — the pwsh twin of kv_field/degrau_field: the first value of `KEY:` in
# FILE with `# ...` comments stripped and surrounding blanks trimmed, or '' when absent. Blanking a
# comment rather than dropping the line keeps `c: bootstrap/teko.c  # note` readable as a value.
function Get-DegrauField {
    param([string]$File, [string]$Key)
    foreach ($raw in Get-Content -LiteralPath $File) {
        $line = ($raw -replace '#.*$', '')
        if ($line -match "^\s*$([regex]::Escape($Key))\s*:\s*(.*?)\s*$") {
            return $Matches[1]
        }
    }
    return ''
}

# Write-DegrauReport DEGRAU — prints what the scan found (degrau.sh's `degrau_report`). Called after
# a Status 0 or 1, never after a 2.
function Write-DegrauReport {
    param([hashtable]$Degrau)
    if ($Degrau.C) {
        Write-CiLog 'degrau' "DECLARED — $($Degrau.File)"
        Write-CiLog 'degrau' "  bridge: $($Degrau.C)"
        Write-CiLog 'degrau' "  why:    $($Degrau.Why)"
        if ($Degrau.Since) { Write-CiLog 'degrau' "  since:  $($Degrau.Since)" }
        return
    }
    Write-CiLog 'degrau' "none declared ($($Degrau.File) is absent) — the chain starts at the published release"
}

# Write-DegrauUndeclaredCNote ROOT — says out loud that a committed `teko.c` is being IGNORED as an
# input (degrau.sh's `degrau_note_undeclared_c`). Called only when no degrau is declared.
function Write-DegrauUndeclaredCNote {
    param([string]$Root = (Get-Location).Path)
    $c = Join-Path $Root 'bootstrap/teko.c'
    if (-not (Test-Path -LiteralPath $c -PathType Leaf)) { return }
    Write-CiLog 'degrau' "$c is present but NOT declared — it is this train's OUTPUT (harvested from gen1),"
    Write-CiLog 'degrau' 'so it is ignored as an input. To USE it, declare the degrau in bootstrap/DEGRAU.'
}
