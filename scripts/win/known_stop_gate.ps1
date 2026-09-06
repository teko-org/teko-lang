param([string]$Rc = '', [string]$LogFile = '')
# scripts/win/known_stop_gate.ps1 — the KNOWN-STOP envelope, pwsh-native (mirror of
# scripts/known_stop_gate.sh). Absorbs ONLY the one pinned diagnostic; anything else stays RED.
#
# The Windows `test` leg is the sole caller of this gate (owner ruling 2026-07-28: KNOWN-STOP for
# the Windows tests, the Win64 4-integer-register ABI overflowing where System V's 6 fits). The
# `.sh` stays for reference/POSIX; this duplicates the exact contract so the Windows step never
# shells out to `sh scripts/known_stop_gate.sh`.
#
# Held (exit 0) ONLY when ALL of: (1) the `teko test .` child failed (RC != 0); (2) exactly one
# regression row failed; (3) no unit #test failed; (4) the log names the FULL pinned diagnostic,
# not merely the shared "B1-args" family substring. Exit 1 = RED (stop lifted, or a different
# failure shape). Exit 2 = FATAL (inputs unreadable).

Set-StrictMode -Version Latest

. "$PSScriptRoot/common.ps1"

$script:PinnedDiag = "isel x86-64: B1-args — an integer call argument past the ABI's argument-register window needs the stack-arg slot (0.3.1)"

function Invoke-KnownStopGate {
    param([string]$Rc, [string]$LogFile)

    if (-not $LogFile -or -not (Test-Path -LiteralPath $LogFile -PathType Leaf)) {
        Write-CiLog 'known-stop' "FATAL: no such log file '$LogFile'"; return 2
    }
    if ($Rc -notmatch '^\d+$') {
        Write-CiLog 'known-stop' "FATAL: RC must be a non-negative integer, got '$Rc'"; return 2
    }
    $rcNum = [int]$Rc
    $log = Get-Content -LiteralPath $LogFile

    if ($rcNum -eq 0) {
        Write-CiLog 'known-stop' 'KNOWN-STOP LIFTED: the suite passed. Promote this envelope: restore the plain'
        Write-CiLog 'known-stop' "'teko test .' invocation and drop this gate — a passing run must never read as held."
        return 1
    }

    $summary = $log | Where-Object { $_ -match 'regressions \d+ run, \d+ skipped, \d+ failed' } | Select-Object -Last 1
    if (-not $summary) {
        Write-CiLog 'known-stop' 'the suite failed, but printed no regression summary line at all — this is NOT the pinned stop:'
        $log | ForEach-Object { Write-CiLog 'known-stop' "  $_" }
        return 1
    }

    $failedCount = if ($summary -match ',\s*(\d+)\s+failed') { $Matches[1] } else { '' }
    if ($failedCount -ne '1') {
        Write-CiLog 'known-stop' "more than the pinned row failed (or none did) — this envelope must not cover a second break: '$summary'"
        return 1
    }

    $unitFails = $log | Where-Object { $_ -match '^test \S+ \.\.\. ' -and $_ -notmatch ' ok$' }
    if ($unitFails) {
        Write-CiLog 'known-stop' 'a UNIT test failed on this host; the B1-args pin covers the regression tier only:'
        $unitFails | ForEach-Object { Write-CiLog 'known-stop' "  $_" }
        return 1
    }

    if (-not ($log | Where-Object { $_.Contains($script:PinnedDiag) })) {
        Write-CiLog 'known-stop' 'the suite failed, but NOT with the exact diagnostic this envelope pins. Either a'
        Write-CiLog 'known-stop' "sibling B1-args stop fired (a DIFFERENT bug) or an unrelated regression broke:"
        $log | ForEach-Object { Write-CiLog 'known-stop' "  $_" }
        return 1
    }

    Write-CiLog 'known-stop' 'KNOWN-STOP held — the suite stops only where the pinned fix will pick it up:'
    Write-CiLog 'known-stop' $script:PinnedDiag
    return 0
}

if ($MyInvocation.InvocationName -ne '.') {
    exit (Invoke-KnownStopGate -Rc $Rc -LogFile $LogFile)
}
