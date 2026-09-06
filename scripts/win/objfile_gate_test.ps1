# scripts/win/objfile_gate_test.ps1 — proves the WINDOWS object/archive gates fail closed when they
# have nothing to check. The pwsh-native, Windows-scoped counterpart of scripts/objfile_gate_test.sh.
#
# ── WHY IT IS SCOPED TO THE COFF GATES, AND WHY THAT IS NOT A LOSS ─────────────────────────────
# The `.sh` runs on all six hosts and tests all six gates (ELF, Mach-O, COFF and their `check_ar_*`
# siblings) PLUS a lint that scans scripts/*.sh for GNU-only `sed -E`/`grep -P` spellings — a
# defense that exists SPECIFICALLY because Git-Bash on the Windows runner lacks them. On the pwsh
# Windows path there is no Git-Bash and no `.sh` in play, so:
#   * the ELF/Mach-O gates never run on Windows — they are validated by the five POSIX legs, which
#     still run the full scripts/objfile_gate_test.sh unchanged;
#   * the GNU-spelling lint's whole rationale (surviving Git-Bash) is moot here.
# What remains windows-relevant is that the COFF gates this path DOES use — check_coff.ps1 and
# check_ar_coff.ps1 — fail closed with nothing to check. That is what this proves, by inversion in
# both directions (the same M.3 shape as the `.sh`): with no opt-out the gate must return NONZERO;
# with its documented opt-out armed the same call must return ZERO. Neither case needs a compiler,
# binutils or LLVM — they only ever hand a gate a path that does not exist.

Set-StrictMode -Version Latest

. "$PSScriptRoot/common.ps1"
. "$PSScriptRoot/check_coff.ps1"
. "$PSScriptRoot/check_ar_coff.ps1"

$tag = 'objfile_gate_test'
$pass = 0
$fail = 0
function ok([string]$m) { Write-Host "  ok   — $m"; $script:pass++ }
function no([string]$m) { Write-CiLog $tag "  FAIL — $m"; $script:fail++ }

# A path guaranteed absent.
$missing = Join-Path ([System.IO.Path]::GetTempPath()) 'this-object-was-never-written.o'
if (Test-Path -LiteralPath $missing) { Write-CiLog $tag 'FATAL — the deliberately-absent path exists'; exit 2 }

# Clean env so the DEFAULTS (fail-closed) are what cases 1-2 exercise.
Remove-Item Env:OBJ_CHECK_ALLOW_SKIP -ErrorAction SilentlyContinue
Remove-Item Env:AR_CHECK_REQUIRE_TOOLS -ErrorAction SilentlyContinue

Write-Host 'objfile_gate_test.ps1: running the Windows COFF gate fail-closed proof'

Write-Host '1) a gate with NO argument must fail closed'
if ((Invoke-CheckCoff -Obj '') -ne 0) { ok 'check_coff.ps1 fails closed with no argument' } else { no 'check_coff.ps1 returned 0 with no argument' }
if ((Invoke-CheckArCoff -Archive '' -Symbol '') -ne 0) { ok 'check_ar_coff.ps1 fails closed with no argument' } else { no 'check_ar_coff.ps1 returned 0 with no argument' }

Write-Host '2) a gate pointed at a NON-EXISTENT object must fail closed'
if ((Invoke-CheckCoff -Obj $missing) -ne 0) { ok 'check_coff.ps1 fails closed on a missing object' } else { no 'check_coff.ps1 returned 0 for an object that does not exist' }
if ((Invoke-CheckArCoff -Archive $missing -Symbol '') -ne 0) { ok 'check_ar_coff.ps1 fails closed on a missing archive' } else { no 'check_ar_coff.ps1 returned 0 for an archive that does not exist' }

Write-Host '3) INVERSION — with its documented opt-out armed, the same call must return 0'
$env:OBJ_CHECK_ALLOW_SKIP = '1'
if ((Invoke-CheckCoff -Obj $missing) -eq 0) { ok 'check_coff.ps1 honours OBJ_CHECK_ALLOW_SKIP=1' } else { no 'check_coff.ps1 ignores OBJ_CHECK_ALLOW_SKIP=1 — the escape hatch is broken, so case 2 proves nothing' }
Remove-Item Env:OBJ_CHECK_ALLOW_SKIP -ErrorAction SilentlyContinue
$env:AR_CHECK_REQUIRE_TOOLS = '0'
if ((Invoke-CheckArCoff -Archive $missing -Symbol '') -eq 0) { ok 'check_ar_coff.ps1 honours AR_CHECK_REQUIRE_TOOLS=0' } else { no 'check_ar_coff.ps1 ignores AR_CHECK_REQUIRE_TOOLS=0 — the escape hatch is broken, so case 2 proves nothing' }
Remove-Item Env:AR_CHECK_REQUIRE_TOOLS -ErrorAction SilentlyContinue

Write-Host "objfile_gate_test.ps1: $pass passed, $fail failed"
if ($fail -ne 0) { Write-CiLog $tag 'FAIL'; exit 1 }
Write-Host 'objfile_gate_test.ps1: PASS'
exit 0
