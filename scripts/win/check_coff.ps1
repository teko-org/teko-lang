param([string]$Obj = '')
# scripts/win/check_coff.ps1 — host-tool well-formedness gate for an own-backend PE/COFF object,
# pwsh-native (mirror of scripts/check_coff.sh). Asserts the LLVM toolchain accepts the object:
# llvm-readobj parses header/sections/symbols/relocations, llvm-objdump disassembles .text, and
# lld-link (with /force:unresolved) confirms it is linker-acceptable.
#
# WHY THIS IS SIMPLER THAN THE .sh: the `.sh` runs under Git-Bash and must fight MSYS path
# conversion (MSYS_NO_PATHCONV, cygpath -w) so lld-link's `/switches` and file paths survive. In
# native PowerShell there is no MSYS layer — native paths pass to native tools unchanged — so all of
# that machinery is simply gone.
#
# FAIL-CLOSED by default (OBJ_CHECK_ALLOW_SKIP=1 restores lenience for a local sandbox; CI never
# sets it). Dot-source to reuse Invoke-CheckCoff; run standalone as check_coff.ps1 <object.o>.

Set-StrictMode -Version Latest
$PSNativeCommandUseErrorActionPreference = $false

. "$PSScriptRoot/common.ps1"

function Invoke-CheckCoff {
    param([string]$Obj)
    $tag = 'check_coff'
    $allowSkip = ($env:OBJ_CHECK_ALLOW_SKIP -eq '1')

    function skipOrFail([string]$why) {
        if ($allowSkip) { Write-CiLog $tag "skipped (OBJ_CHECK_ALLOW_SKIP=1) — $why"; return 0 }
        Write-CiLog $tag "FAIL — $why — a gate that passes with nothing to check is a hidden error; set OBJ_CHECK_ALLOW_SKIP=1 only in a local sandbox"
        return 1
    }

    if (-not $Obj) { return (skipOrFail 'no object argument given') }
    if (-not (Test-Path -LiteralPath $Obj -PathType Leaf)) { return (skipOrFail "the object '$Obj' does not exist — its producer did not write it") }

    $readobj = if ($env:LLVM_READOBJ) { $env:LLVM_READOBJ } else { 'llvm-readobj' }
    $objdump = if ($env:LLVM_OBJDUMP) { $env:LLVM_OBJDUMP } else { 'llvm-objdump' }
    $lldLink = if ($env:LLD_LINK) { $env:LLD_LINK } else { 'lld-link' }

    if (-not (Get-Command $readobj -ErrorAction SilentlyContinue)) {
        return (skipOrFail "the cross-format COFF parser '$readobj' is absent on this host")
    }

    $hdr = & $readobj --file-headers $Obj 2>&1
    if ($LASTEXITCODE -ne 0) { Write-CiLog $tag "FAIL — $readobj could not parse $Obj"; return 1 }
    if (-not ($hdr | Select-String -SimpleMatch 'IMAGE_FILE_MACHINE_AMD64' -Quiet)) { Write-CiLog $tag 'FAIL — not an IMAGE_FILE_MACHINE_AMD64 object'; return 1 }

    $sections = & $readobj --sections $Obj 2>&1
    if (-not ($sections | Select-String -SimpleMatch 'Name: .text' -Quiet)) { Write-CiLog $tag 'FAIL — missing .text section'; return 1 }

    $symbols = & $readobj --symbols $Obj 2>&1
    if (-not ($symbols | Select-String -SimpleMatch 'Section: .text' -Quiet)) { Write-CiLog $tag "FAIL — no defined .text symbol in $Obj"; return 1 }

    $relocs = & $readobj --relocations $Obj 2>&1
    if (-not ($relocs | Select-String -SimpleMatch 'IMAGE_REL_AMD64_REL32' -Quiet)) { Write-CiLog $tag "FAIL — no IMAGE_REL_AMD64_REL32 relocation in $Obj"; return 1 }

    if (Get-Command $objdump -ErrorAction SilentlyContinue) {
        $dis = & $objdump -d $Obj 2>&1
        if (-not ($dis | Select-String -SimpleMatch 'Disassembly of section .text' -Quiet)) { Write-CiLog $tag "FAIL — llvm-objdump -d found no .text disassembly in $Obj"; return 1 }
    }

    if (Get-Command $lldLink -ErrorAction SilentlyContinue) {
        Write-CiLog $tag ((& $lldLink --version 2>&1 | Select-Object -First 1))
        $tmpExe = Join-Path ([System.IO.Path]::GetTempPath()) ("check-coff-" + [System.IO.Path]::GetRandomFileName() + ".exe")
        $linkOut = & $lldLink /nologo "/out:$tmpExe" /entry:main /subsystem:console /force:unresolved $Obj 2>&1
        $linkRc = $LASTEXITCODE
        Remove-Item -LiteralPath $tmpExe -Force -ErrorAction SilentlyContinue
        if ($linkRc -ne 0) {
            Write-CiLog $tag "lld-link rejected $Obj (rc=$linkRc) — full output below:"
            $linkOut | ForEach-Object { Write-CiLog $tag "  | $_" }
            return 1
        }
    }

    Write-CiLog $tag "OK — $Obj is a well-formed, linker-consumable IMAGE_FILE_MACHINE_AMD64 PE/COFF object"
    return 0
}

if ($MyInvocation.InvocationName -ne '.') {
    exit (Invoke-CheckCoff -Obj $Obj)
}
